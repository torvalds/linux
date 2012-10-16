#include <linux/fb.h>
#include <linux/delay.h>
#include "../../rk29_fb.h"
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <mach/board.h>

#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/gpio.h>
#include <asm/uaccess.h>
#include <asm/atomic.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/freezer.h>
#include <linux/input/mt.h>

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <linux/ts-auto.h>
#include "screen.h"

extern struct rk29_bl_info rk29_bl_info;


//FOR ID0
/* Base */
#define OUT_TYPE_ID0			SCREEN_RGB
#define OUT_FACE_ID0			OUT_P888
#define OUT_CLK_ID0			71000000
#define LCDC_ACLK_ID0       		300000000     //29 lcdc axi DMA ÆµÂÊ

/* Timing */
#define H_PW_ID0			10
#define H_BP_ID0			64
#define H_VD_ID0			800
#define H_FP_ID0			16

#define V_PW_ID0			3
#define V_BP_ID0			8
#define V_VD_ID0			1280
#define V_FP_ID0			10


/* Other */
#define DCLK_POL_ID0			0
#define SWAP_RB_ID0			0

#define LCD_WIDTH_ID0                  152
#define LCD_HEIGHT_ID0                 202



//FOR ID2
#define OUT_TYPE_ID2			SCREEN_RGB

#define OUT_FACE_ID2			OUT_P888
#define OUT_CLK_ID2			65000000
#define LCDC_ACLK_ID2       		500000000

/* Timing */
#define H_PW_ID2			100
#define H_BP_ID2			100
#define H_VD_ID2			1024
#define H_FP_ID2			120

#define V_PW_ID2			10
#define V_BP_ID2			10
#define V_VD_ID2			768
#define V_FP_ID2			15

#define LCD_WIDTH_ID2       		216
#define LCD_HEIGHT_ID2      		162
/* Other */
#define DCLK_POL_ID2			0
#define SWAP_RB_ID2			0 

//FOR ID2
/* Base */
#define OUT_TYPE_ID3			SCREEN_RGB
#define OUT_FACE_ID3			OUT_P888
#define OUT_CLK_ID3			71000000
#define LCDC_ACLK_ID3       		500000000 	

/* Timing */
#define H_PW_ID3			10
#define H_BP_ID3			160
#define H_VD_ID3			1280
#define H_FP_ID3			16

#define V_PW_ID3			3
#define V_BP_ID3			23
#define V_VD_ID3			800
#define V_FP_ID3			12


/* Other */
#define DCLK_POL_ID3			0
#define SWAP_RB_ID3			0

#define LCD_WIDTH_ID3       		270
#define LCD_HEIGHT_ID3      		202

#if defined(CONFIG_TS_AUTO)
extern struct ts_private_data *g_ts;
#else
static struct ts_private_data *g_ts = NULL;
#endif

#if defined(CONFIG_RK_BOARD_ID)
extern int rk_get_board_id(void);
#else
static int rk_get_board_id(void)
{
	return -1;
}
#endif
static int lcd_get_id(void)
{
	int id = -1;
	int ts_id = -1;
	
#if defined(CONFIG_RK_BOARD_ID)
	id = rk_get_board_id();	
#elif defined(CONFIG_TS_AUTO)
	if(!g_ts)
		return -1;

	ts_id = g_ts->ops->ts_id;

	switch(ts_id)
	{
		case TS_ID_FT5306:	
			id = 2;
			break;
		case TS_ID_GT8110:	
			id = 3;
			break;
		case TS_ID_GT828:
			id = 0;
			break;
		default:
			break;
	}

#endif
	return id;
}


void set_lcd_info(struct rk29fb_screen *screen, struct rk29lcd_info *lcd_info )
{
	int id;
	id = lcd_get_id();

	switch(id)
	{
		case 0:
			
		/* screen type & face */
		screen->type = OUT_TYPE_ID0;
		screen->face = OUT_FACE_ID0;

		/* Screen size */
		screen->x_res = H_VD_ID0;
		screen->y_res = V_VD_ID0;

		screen->width = LCD_WIDTH_ID0;
		screen->height = LCD_HEIGHT_ID0;

		/* Timing */
		screen->lcdc_aclk = LCDC_ACLK_ID0;
		screen->pixclock = OUT_CLK_ID0;
		screen->left_margin = H_BP_ID0;
		screen->right_margin = H_FP_ID0;
		screen->hsync_len = H_PW_ID0;
		screen->upper_margin = V_BP_ID0;
		screen->lower_margin = V_FP_ID0;
		screen->vsync_len = V_PW_ID0;

		/* Pin polarity */
		screen->pin_hsync = 0;
		screen->pin_vsync = 0;
		screen->pin_den = 0;
		screen->pin_dclk = DCLK_POL_ID0;

		/* Swap rule */
		screen->swap_rb = SWAP_RB_ID0;
		screen->swap_rg = 0;
		screen->swap_gb = 0;
		screen->swap_delta = 0;
		screen->swap_dumy = 0;

		/* Operation function*/
		screen->init = NULL;
		screen->standby = NULL;

		break;

		case 2:
			
		/* screen type & face */
		screen->type = OUT_TYPE_ID2;
		screen->face = OUT_FACE_ID2;

		/* Screen size */
		screen->x_res = H_VD_ID2;
		screen->y_res = V_VD_ID2;

		screen->width = LCD_WIDTH_ID2;
		screen->height = LCD_HEIGHT_ID2;

		/* Timing */
		screen->lcdc_aclk = LCDC_ACLK_ID2;
		screen->pixclock = OUT_CLK_ID2;
		screen->left_margin = H_BP_ID2;
		screen->right_margin = H_FP_ID2;
		screen->hsync_len = H_PW_ID2;
		screen->upper_margin = V_BP_ID2;
		screen->lower_margin = V_FP_ID2;
		screen->vsync_len = V_PW_ID2;

		/* Pin polarity */
		screen->pin_hsync = 0;
		screen->pin_vsync = 0;
		screen->pin_den = 0;
		screen->pin_dclk = DCLK_POL_ID2;

		/* Swap rule */
		screen->swap_rb = SWAP_RB_ID2;
		screen->swap_rg = 0;
		screen->swap_gb = 0;
		screen->swap_delta = 0;
		screen->swap_dumy = 0;

		/* Operation function*/
		screen->init = NULL;
		screen->standby = NULL;

		break;

		case 3:
		default:
			
		/* screen type & face */
		screen->type = OUT_TYPE_ID3;
		screen->face = OUT_FACE_ID3;

		/* Screen size */
		screen->x_res = H_VD_ID3;
		screen->y_res = V_VD_ID3;

		screen->width = LCD_WIDTH_ID3;
		screen->height = LCD_HEIGHT_ID3;

		/* Timing */
		screen->lcdc_aclk = LCDC_ACLK_ID3;
		screen->pixclock = OUT_CLK_ID3;
		screen->left_margin = H_BP_ID3;
		screen->right_margin = H_FP_ID3;
		screen->hsync_len = H_PW_ID3;
		screen->upper_margin = V_BP_ID3;
		screen->lower_margin = V_FP_ID3;
		screen->vsync_len = V_PW_ID3;

		/* Pin polarity */
		screen->pin_hsync = 0;
		screen->pin_vsync = 0;
		screen->pin_den = 0;
		screen->pin_dclk = DCLK_POL_ID3;

		/* Swap rule */
		screen->swap_rb = SWAP_RB_ID3;
		screen->swap_rg = 0;
		screen->swap_gb = 0;
		screen->swap_delta = 0;
		screen->swap_dumy = 0;

		/* Operation function*/
		screen->init = NULL;
		screen->standby = NULL;

		break;

	}


	printk("%s:board_id=%d\n",__func__,id);
   
}



