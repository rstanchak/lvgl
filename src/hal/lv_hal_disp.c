/**
 * @file lv_hal_disp.c
 *
 * @description HAL layer for display driver
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include <stdint.h>
#include <stddef.h>
#include "lv_hal.h"
#include "../misc/lv_mem.h"
#include "../misc/lv_gc.h"
#include "../misc/lv_assert.h"
#include "../core/lv_obj.h"
#include "../core/lv_refr.h"
#include "../core/lv_theme.h"
#if LV_USE_THEME_DEFAULT
#include "../extra/themes/default/lv_theme_default.h"
#endif

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**********************
 *  STATIC VARIABLES
 **********************/
static lv_disp_t * disp_def;

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/**
 * Initialize a display driver with default values.
 * It is used to surly have known values in the fields ant not memory junk.
 * After it you can set the fields.
 * @param driver pointer to driver variable to initialize
 */
void lv_disp_drv_init(lv_disp_drv_t * driver)
{
    lv_memset_00(driver, sizeof(lv_disp_drv_t));

    driver->hor_res          = 320;
    driver->ver_res          = 240;
    driver->antialiasing     = LV_COLOR_DEPTH > 8 ? 1: 0;
    driver->screen_transp    = LV_COLOR_SCREEN_TRANSP;
    driver->dpi              = LV_DPI_DEF;
    driver->color_chroma_key = LV_COLOR_CHROMA_KEY;
}

/**
 * Initialize a display buffer
 * @param draw_buf pointer `lv_disp_draw_buf_t` variable to initialize
 * @param buf1 A buffer to be used by LVGL to draw the image.
 *             Always has to specified and can't be NULL.
 *             Can be an array allocated by the user. E.g. `static lv_color_t disp_buf1[1024 * 10]`
 *             Or a memory address e.g. in external SRAM
 * @param buf2 Optionally specify a second buffer to make image rendering and image flushing
 *             (sending to the display) parallel.
 *             In the `disp_drv->flush` you should use DMA or similar hardware to send
 *             the image to the display in the background.
 *             It lets LVGL to render next frame into the other buffer while previous is being
 * sent. Set to `NULL` if unused.
 * @param size_in_px_cnt size of the `buf1` and `buf2` in pixel count.
 */
void lv_disp_draw_buf_init(lv_disp_draw_buf_t * draw_buf, void * buf1, void * buf2, uint32_t size_in_px_cnt)
{
    lv_memset_00(draw_buf, sizeof(lv_disp_draw_buf_t));

    draw_buf->buf1    = buf1;
    draw_buf->buf2    = buf2;
    draw_buf->buf_act = draw_buf->buf1;
    draw_buf->size    = size_in_px_cnt;
}

/**
 * Register an initialized display driver.
 * Automatically set the first display as active.
 * @param driver pointer to an initialized 'lv_disp_drv_t' variable. Only its pointer is saved!
 * @return pointer to the new display or NULL on error
 */
lv_disp_t * lv_disp_drv_register(lv_disp_drv_t * driver)
{
    lv_disp_t * disp = _lv_ll_ins_head(&LV_GC_ROOT(_lv_disp_ll));
    if(!disp) {
        LV_ASSERT_MALLOC(disp);
        return NULL;
    }

    lv_memset_00(disp, sizeof(lv_disp_t));

    disp->driver = driver;

    lv_disp_t * disp_def_tmp = disp_def;
    disp_def                 = disp; /*Temporarily change the default screen to create the default screens on the
                                        new display*/
    /*Create a refresh timer*/
    disp->refr_timer = lv_timer_create(_lv_disp_refr_timer, LV_DISP_DEF_REFR_PERIOD, disp);
    LV_ASSERT_MALLOC(disp->refr_timer);
    if(disp->refr_timer == NULL) {
        lv_mem_free(disp);
        return NULL;
    }

    if(driver->full_refresh && driver->draw_buf->size < (uint32_t)driver->hor_res * driver->ver_res) {
        driver->full_refresh = 0;
        LV_LOG_WARN("full_refresh requires at least screen sized draw buffer(s)")
    }

    disp->bg_color = lv_color_white();
#if LV_COLOR_SCREEN_TRANSP
    disp->bg_opa = LV_OPA_TRANSP;
#else
    disp->bg_opa = LV_OPA_COVER;
#endif

#if LV_USE_THEME_DEFAULT
    if(lv_theme_default_is_inited() == false) {
        disp->theme = lv_theme_default_init(disp, LV_COLOR_PALETTE_BLUE, LV_COLOR_PALETTE_CYAN, LV_FONT_DEFAULT, LV_FONT_DEFAULT, LV_FONT_DEFAULT);
    }
#endif

    disp->act_scr   = lv_obj_create(NULL); /*Create a default screen on the display*/
    disp->top_layer = lv_obj_create(NULL); /*Create top layer on the display*/
    disp->sys_layer = lv_obj_create(NULL); /*Create sys layer on the display*/
    lv_obj_remove_style_all(disp->top_layer);
    lv_obj_remove_style_all(disp->sys_layer);
    lv_obj_clear_flag(disp->top_layer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(disp->sys_layer, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_set_scrollbar_mode(disp->top_layer, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scrollbar_mode(disp->sys_layer, LV_SCROLLBAR_MODE_OFF);

    lv_obj_invalidate(disp->act_scr);

    disp_def = disp_def_tmp; /*Revert the default display*/
    if(disp_def == NULL) disp_def = disp; /*Initialize the default display*/

    lv_timer_ready(disp->refr_timer); /*Be sure the screen will be refreshed immediately on start up*/

    return disp;
}

/**
 * Update the driver in run time.
 * @param disp pointer to a display. (return value of `lv_disp_drv_register`)
 * @param new_drv pointer to the new driver
 */
void lv_disp_drv_update(lv_disp_t * disp, lv_disp_drv_t * new_drv)
{
    disp->driver = new_drv;

    if(disp->driver->full_refresh && disp->driver->draw_buf->size < (uint32_t)disp->driver->hor_res * disp->driver->ver_res) {
        disp->driver->full_refresh = 0;
        LV_LOG_WARN("full_refresh requires at least screen sized draw buffer(s)")
    }

    lv_coord_t w = lv_disp_get_hor_res(disp);
    lv_coord_t h = lv_disp_get_ver_res(disp);
    uint32_t i;
    for(i = 0; i < disp->screen_cnt; i++) {
        lv_area_t prev_coords;
        lv_obj_get_coords(disp->screens[i], &prev_coords);
        lv_area_set_width(&disp->screens[i]->coords, w);
        lv_area_set_height(&disp->screens[i]->coords, h);
        lv_event_send(disp->screens[i], LV_EVENT_SIZE_CHANGED, &prev_coords);
    }

    /*
     * This method is usually called upon orientation change, thus the screen is now a
     * different size.
     * The object invalidated its previous area. That area is now out of the screen area
     * so we reset all invalidated areas and invalidate the active screen's new area only.
     */
    lv_memset_00(disp->inv_areas, sizeof(disp->inv_areas));
    lv_memset_00(disp->inv_area_joined, sizeof(disp->inv_area_joined));
    disp->inv_p = 0;
    if(disp->act_scr != NULL) lv_obj_invalidate(disp->act_scr);

    if(disp->driver->drv_update_cb) disp->driver->drv_update_cb(disp->driver);
}

/**
 * Remove a display
 * @param disp pointer to display
 */
void lv_disp_remove(lv_disp_t * disp)
{
    bool was_default = false;
    if(disp == lv_disp_get_default()) was_default = true;

    /*Detach the input devices*/
    lv_indev_t * indev;
    indev = lv_indev_get_next(NULL);
    while(indev) {
        if(indev->driver->disp == disp) {
            indev->driver->disp = NULL;
        }
        indev = lv_indev_get_next(indev);
    }

    _lv_ll_remove(&LV_GC_ROOT(_lv_disp_ll), disp);
    lv_timer_del(disp->refr_timer);
    lv_mem_free(disp);

    if(was_default) lv_disp_set_default(_lv_ll_get_head(&LV_GC_ROOT(_lv_disp_ll)));
}

/**
 * Set a default display. The new screens will be created on it by default.
 * @param disp pointer to a display
 */
void lv_disp_set_default(lv_disp_t * disp)
{
    disp_def = disp;
}

/**
 * Get the default display
 * @return pointer to the default display
 */
lv_disp_t * lv_disp_get_default(void)
{
    return disp_def;
}

/**
 * Get the horizontal resolution of a display
 * @param disp pointer to a display (NULL to use the default display)
 * @return the horizontal resolution of the display
 */
lv_coord_t lv_disp_get_hor_res(lv_disp_t * disp)
{
    if(disp == NULL) disp = lv_disp_get_default();

    if(disp == NULL) {
        return 0;
    } else {
        switch(disp->driver->rotated) {
            case LV_DISP_ROT_90:
            case LV_DISP_ROT_270:
                return disp->driver->ver_res;
            default:
                return disp->driver->hor_res;
        }
    }
}

/**
 * Get the vertical resolution of a display
 * @param disp pointer to a display (NULL to use the default display)
 * @return the vertical resolution of the display
 */
lv_coord_t lv_disp_get_ver_res(lv_disp_t * disp)
{
    if(disp == NULL) disp = lv_disp_get_default();

    if(disp == NULL) {
        return 0;
    } else {
        switch(disp->driver->rotated) {
            case LV_DISP_ROT_90:
            case LV_DISP_ROT_270:
                return disp->driver->hor_res;
            default:
                return disp->driver->ver_res;
        }
    }
}

/**
 * Get if anti-aliasing is enabled for a display or not
 * @param disp pointer to a display (NULL to use the default display)
 * @return true: anti-aliasing is enabled; false: disabled
 */
bool lv_disp_get_antialiasing(lv_disp_t * disp)
{
    if(disp == NULL) disp = lv_disp_get_default();
    if(disp == NULL) return false;

    return disp->driver->antialiasing ? true : false;
}

/**
 * Get the DPI of the display
 * @param disp pointer to a display (NULL to use the default display)
 * @return dpi of the display
 */
lv_coord_t lv_disp_get_dpi(lv_disp_t * disp)
{
    if(disp == NULL) disp = lv_disp_get_default();
    if(disp == NULL) return LV_DPI_DEF;  /*Do not return 0 because it might be a divider*/
    return disp->driver->dpi;
}

/**
 * Call in the display driver's `flush_cb` function when the flushing is finished
 * @param disp_drv pointer to display driver in `flush_cb` where this function is called
 */
LV_ATTRIBUTE_FLUSH_READY void lv_disp_flush_ready(lv_disp_drv_t * disp_drv)
{
    /*If the screen is transparent initialize it when the flushing is ready*/
#if LV_COLOR_SCREEN_TRANSP
    if(disp_drv->screen_transp) {
        lv_memset_00(disp_drv->draw_buf->buf_act, disp_drv->draw_buf->size * sizeof(lv_color32_t));
    }
#endif

    disp_drv->draw_buf->flushing = 0;
    disp_drv->draw_buf->flushing_last = 0;
}

/**
 * Tell if it's the last area of the refreshing process.
 * Can be called from `flush_cb` to execute some special display refreshing if needed when all areas area flushed.
 * @param disp_drv pointer to display driver
 * @return true: it's the last area to flush; false: there are other areas too which will be refreshed soon
 */
LV_ATTRIBUTE_FLUSH_READY bool lv_disp_flush_is_last(lv_disp_drv_t * disp_drv)
{
    return disp_drv->draw_buf->flushing_last;
}

/**
 * Get the next display.
 * @param disp pointer to the current display. NULL to initialize.
 * @return the next display or NULL if no more. Give the first display when the parameter is NULL
 */
lv_disp_t * lv_disp_get_next(lv_disp_t * disp)
{
    if(disp == NULL)
        return _lv_ll_get_head(&LV_GC_ROOT(_lv_disp_ll));
    else
        return _lv_ll_get_next(&LV_GC_ROOT(_lv_disp_ll), disp);
}

/**
 * Get the internal buffer of a display
 * @param disp pointer to a display
 * @return pointer to the internal buffers
 */
lv_disp_draw_buf_t * lv_disp_get_draw_buf(lv_disp_t * disp)
{
    return disp->driver->draw_buf;
}

/**
 * Set the rotation of this display.
 * @param disp pointer to a display (NULL to use the default display)
 * @param rotation rotation angle
 */
void lv_disp_set_rotation(lv_disp_t * disp, lv_disp_rot_t rotation)
{
    if(disp == NULL) disp = lv_disp_get_default();
    if(disp == NULL) return;

    disp->driver->rotated = rotation;
    lv_disp_drv_update(disp, disp->driver);
}

/**
 * Get the current rotation of this display.
 * @param disp pointer to a display (NULL to use the default display)
 * @return rotation angle
 */
lv_disp_rot_t lv_disp_get_rotation(lv_disp_t * disp)
{
    if(disp == NULL) disp = lv_disp_get_default();
    if(disp == NULL) return LV_DISP_ROT_NONE;
    return disp->driver->rotated;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
