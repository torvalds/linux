#include <linux/fb.h>
#include <linux/delay.h>
#include "../../rk29_fb.h"
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <mach/board.h>
#include <mach/config.h>
#include "../../rockchip/hdmi/rk_hdmi.h"
#include "screen.h"

enum {
        OUT_TYPE_INDEX = 0,
        OUT_FACE_INDEX,
        OUT_CLK_INDEX,
        LCDC_ACLK_INDEX,
        H_PW_INDEX,
        H_BP_INDEX,
        H_VD_INDEX,
        H_FP_INDEX,
        V_PW_INDEX,
        V_BP_INDEX,
        V_VD_INDEX,
        V_FP_INDEX,
        LCD_WIDTH_INDEX,
        LCD_HEIGHT_INDEX,
        DCLK_POL_INDEX,
        SWAP_RB_INDEX,
        LCD_PARAM_MAX,
};
uint lcd_param[LCD_PARAM_MAX] = DEF_LCD_PARAM;
module_param_array(lcd_param, uint, NULL, 0644);

#define set_scaler_info	 NULL

void set_lcd_info(struct rk29fb_screen *screen, struct rk29lcd_info *lcd_info )
{
	/* screen type & face */
	screen->type = lcd_param[OUT_TYPE_INDEX];
	screen->face = lcd_param[OUT_FACE_INDEX];
	screen->hw_format = 1;

	/* Screen size */
	screen->x_res = lcd_param[H_VD_INDEX];
	screen->y_res = lcd_param[V_VD_INDEX];

	screen->width = lcd_param[LCD_WIDTH_INDEX];
	screen->height = lcd_param[LCD_HEIGHT_INDEX];

	/* Timing */
	screen->lcdc_aclk = lcd_param[LCDC_ACLK_INDEX];
	screen->pixclock = lcd_param[OUT_CLK_INDEX];
	screen->left_margin = lcd_param[H_BP_INDEX];
	screen->right_margin = lcd_param[H_FP_INDEX];
	screen->hsync_len = lcd_param[H_PW_INDEX];
	screen->upper_margin = lcd_param[V_BP_INDEX];
	screen->lower_margin = lcd_param[V_FP_INDEX];
	screen->vsync_len = lcd_param[V_PW_INDEX];

	/* Pin polarity */
	screen->pin_hsync = 0;
	screen->pin_vsync = 0;
	screen->pin_den = 0;
	screen->pin_dclk = lcd_param[DCLK_POL_INDEX];

	/* Swap rule */
	screen->swap_rb = lcd_param[SWAP_RB_INDEX];
	screen->swap_rg = 0;
	screen->swap_gb = 0;
	screen->swap_delta = 0;
	screen->swap_dumy = 0;

	/* Operation function*/
	screen->init = NULL;
	screen->standby = NULL;
}



