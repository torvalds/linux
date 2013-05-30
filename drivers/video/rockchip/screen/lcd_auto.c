
#ifndef __LCD_AUTO__
#define __LCD_AUTO__

#include <mach/board.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/freezer.h>
#include <linux/input/mt.h>
#if defined(CONFIG_HAS_EARLYSUSPEND)
#include<linux/earlysuspend.h>
#endif
#include <linux/ts-auto.h>
#include <linux/rk_board_id.h>


//FOR ID0
/* Base */
#define SCREEN_TYPE_ID0			SCREEN_RGB

#define OUT_FACE_ID0			OUT_P888
#define DCLK_ID0			71000000
#define LCDC_ACLK_ID0       		500000000//312000000           //29 lcdc axi DMA ÆµÂÊ

/* Timing */
#define H_PW_ID0			100
#define H_BP_ID0			100
#define H_VD_ID0			1024
#define H_FP_ID0			120

#define V_PW_ID0			10
#define V_BP_ID0			10
#define V_VD_ID0			600
#define V_FP_ID0			15

#define LCD_WIDTH_ID0       		202
#define LCD_HEIGHT_ID0      		152
/* Other */
#define DCLK_POL_ID0			0
#define DEN_POL_ID0			0
#define VSYNC_POL_ID0			0
#define HSYNC_POL_ID0			0

#define SWAP_RB_ID0			0
#define SWAP_DUMMY_ID0			0
#define SWAP_GB_ID0			0
#define SWAP_RG_ID0			0



//FOR ID1
/* Base */
#define SCREEN_TYPE_ID1			SCREEN_RGB
#define OUT_FACE_ID1			OUT_P888
#define DCLK_ID1			71000000
#define LCDC_ACLK_ID1       		500000000

/* Timing */
#define H_PW_ID1			10
#define H_BP_ID1			160
#define H_VD_ID1			1024
#define H_FP_ID1			16

#define V_PW_ID1			3
#define V_BP_ID1			23
#define V_VD_ID1			768
#define V_FP_ID1			12


/* Other */
#define DCLK_POL_ID1			0
#define DEN_POL_ID1			0
#define VSYNC_POL_ID1			0
#define HSYNC_POL_ID1			0

#define SWAP_RB_ID1			0
#define SWAP_DUMMY_ID1			0
#define SWAP_GB_ID1			0
#define SWAP_RG_ID1			0


#define LCD_WIDTH_ID1      		270
#define LCD_HEIGHT_ID1      		202



//FOR ID2
#define SCREEN_TYPE_ID2			SCREEN_RGB

#define OUT_FACE_ID2			OUT_P888
#define DCLK_ID2			65000000
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
#define DEN_POL_ID2			0
#define VSYNC_POL_ID2			0
#define HSYNC_POL_ID2			0

#define SWAP_RB_ID2			0
#define SWAP_DUMMY_ID2			0
#define SWAP_GB_ID2			0
#define SWAP_RG_ID2			0


//FOR ID3
/* Base */
#define SCREEN_TYPE_ID3			SCREEN_RGB
#define OUT_FACE_ID3			OUT_P888
#define DCLK_ID3			71000000
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
#define DEN_POL_ID3			0
#define VSYNC_POL_ID3			0
#define HSYNC_POL_ID3			0

#define SWAP_RB_ID3			0
#define SWAP_DUMMY_ID3			0
#define SWAP_GB_ID3			0
#define SWAP_RG_ID3			0


#define LCD_WIDTH_ID3       		270
#define LCD_HEIGHT_ID3      		202


//FOR ID4
/* Base */
#define SCREEN_TYPE_ID4			SCREEN_RGB
#define OUT_FACE_ID4			OUT_P888
#define DCLK_ID4			71000000
#define LCDC_ACLK_ID4       		300000000     //29 lcdc axi DMA ÆµÂÊ

/* Timing */
#define H_PW_ID4			10
#define H_BP_ID4			64
#define H_VD_ID4			800
#define H_FP_ID4			16

#define V_PW_ID4			3
#define V_BP_ID4			8
#define V_VD_ID4			1280
#define V_FP_ID4			10


/* Other */
#define DCLK_POL_ID4			0
#define DEN_POL_ID4			0
#define VSYNC_POL_ID4			0
#define HSYNC_POL_ID4			0

#define SWAP_RB_ID4			0
#define SWAP_DUMMY_ID4			0
#define SWAP_GB_ID4			0
#define SWAP_RG_ID4			0


#define LCD_WIDTH_ID4                  152
#define LCD_HEIGHT_ID4                 202


#define H_VD 				1280
#define V_VD 				800
#if defined(CONFIG_TS_AUTO)
extern struct ts_private_data *g_ts;
#else
static struct ts_private_data *g_ts = NULL;
#endif

#if defined(CONFIG_RK_BOARD_ID)
extern enum rk_board_id rk_get_board_id(void);
#else
static enum rk_board_id rk_get_board_id(void)
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
			id = BOARD_ID_C8003;
			break;
		case TS_ID_GT8110:	
			id = BOARD_ID_C1014;
			break;
		case TS_ID_GT828:
			id = BOARD_ID_C7018;
			break;
		case TS_ID_GT8005:
			id = BOARD_ID_C8002;
			break;
		case TS_ID_CT360:
			id = BOARD_ID_DS763;
			break;
		default:
			break;
	}

#endif
	return id;
}

#define RK_USE_SCREEN_ID

#if defined(RK_USE_SCREEN_ID)
void set_lcd_info_by_id(struct rk29fb_screen *screen, struct rk29lcd_info *lcd_info )
{
	int id;
	id = lcd_get_id();

	switch(id)
	{
	case BOARD_ID_DS763:
			
		/* screen type & face */
		screen->type = SCREEN_TYPE_ID0;
		screen->face = OUT_FACE_ID0;

		/* Screen size */
		screen->x_res = H_VD_ID0;
		screen->y_res = V_VD_ID0;

		screen->width = LCD_WIDTH_ID0;
		screen->height = LCD_HEIGHT_ID0;

		/* Timing */
		screen->lcdc_aclk = LCDC_ACLK_ID0;
		screen->pixclock = DCLK_ID0;
		screen->left_margin = H_BP_ID0;
		screen->right_margin = H_FP_ID0;
		screen->hsync_len = H_PW_ID0;
		screen->upper_margin = V_BP_ID0;
		screen->lower_margin = V_FP_ID0;
		screen->vsync_len = V_PW_ID0;

		/* Pin polarity */
		screen->pin_hsync = HSYNC_POL_ID0;
		screen->pin_vsync = VSYNC_POL_ID0;
		screen->pin_den = DEN_POL_ID0;
		screen->pin_dclk = DCLK_POL_ID0;

		/* Swap rule */
		screen->swap_rb = SWAP_RB_ID0;
		screen->swap_rg = SWAP_RG_ID0;
		screen->swap_gb = SWAP_GB_ID0;
		screen->swap_delta = 0;
		screen->swap_dumy = 0;

		/* Operation function*/
		screen->init = NULL;
		screen->standby = NULL;

		break;

	case BOARD_ID_C8002:

		/* screen type & face */
		screen->type = SCREEN_TYPE_ID1;
		screen->face = OUT_FACE_ID1;

		/* Screen size */
		screen->x_res = H_VD_ID1;
		screen->y_res = V_VD_ID1;

		screen->width = LCD_WIDTH_ID1;
		screen->height = LCD_HEIGHT_ID1;

		/* Timing */
		screen->lcdc_aclk = LCDC_ACLK_ID1;
		screen->pixclock = DCLK_ID1;
		screen->left_margin = H_BP_ID1;
		screen->right_margin = H_FP_ID1;
		screen->hsync_len = H_PW_ID1;
		screen->upper_margin = V_BP_ID1;
		screen->lower_margin = V_FP_ID1;
		screen->vsync_len = V_PW_ID1;

		/* Pin polarity */
		screen->pin_hsync = HSYNC_POL_ID1;
		screen->pin_vsync = VSYNC_POL_ID1;
		screen->pin_den =   DEN_POL_ID1;
		screen->pin_dclk = DCLK_POL_ID1;

		/* Swap rule */
		screen->swap_rb = SWAP_RB_ID1;
		screen->swap_rg = SWAP_RG_ID1;
		screen->swap_gb = SWAP_GB_ID1;
		screen->swap_delta = 0;
		screen->swap_dumy = 0;

		/* Operation function*/
		screen->init = NULL;
		screen->standby = NULL;
		break;

	case BOARD_ID_C8003:
			
		/* screen type & face */
		screen->type = SCREEN_TYPE_ID2;
		screen->face = OUT_FACE_ID2;

		/* Screen size */
		screen->x_res = H_VD_ID2;
		screen->y_res = V_VD_ID2;

		screen->width = LCD_WIDTH_ID2;
		screen->height = LCD_HEIGHT_ID2;

		/* Timing */
		screen->lcdc_aclk = LCDC_ACLK_ID2;
		screen->pixclock = DCLK_ID2;
		screen->left_margin = H_BP_ID2;
		screen->right_margin = H_FP_ID2;
		screen->hsync_len = H_PW_ID2;
		screen->upper_margin = V_BP_ID2;
		screen->lower_margin = V_FP_ID2;
		screen->vsync_len = V_PW_ID2;

		/* Pin polarity */
		screen->pin_hsync = HSYNC_POL_ID2;
		screen->pin_vsync = VSYNC_POL_ID2;
		screen->pin_den = DEN_POL_ID2;
		screen->pin_dclk = DCLK_POL_ID2;

		/* Swap rule */
		screen->swap_rb = SWAP_RB_ID2;
		screen->swap_rg = SWAP_RG_ID2;
		screen->swap_gb = SWAP_GB_ID2;
		screen->swap_delta = 0;
		screen->swap_dumy = 0;

		/* Operation function*/
		screen->init = NULL;
		screen->standby = NULL;

		break;

	case BOARD_ID_C1014:
		default:
			
		/* screen type & face */
		screen->type = SCREEN_TYPE_ID3;
		screen->face = OUT_FACE_ID3;

		/* Screen size */
		screen->x_res = H_VD_ID3;
		screen->y_res = V_VD_ID3;

		screen->width = LCD_WIDTH_ID3;
		screen->height = LCD_HEIGHT_ID3;

		/* Timing */
		screen->lcdc_aclk = LCDC_ACLK_ID3;
		screen->pixclock = DCLK_ID3;
		screen->left_margin = H_BP_ID3;
		screen->right_margin = H_FP_ID3;
		screen->hsync_len = H_PW_ID3;
		screen->upper_margin = V_BP_ID3;
		screen->lower_margin = V_FP_ID3;
		screen->vsync_len = V_PW_ID3;

		/* Pin polarity */
		screen->pin_hsync = HSYNC_POL_ID3;
		screen->pin_vsync = VSYNC_POL_ID3;
		screen->pin_den = DEN_POL_ID3;
		screen->pin_dclk = DCLK_POL_ID3;

		/* Swap rule */
		screen->swap_rb = SWAP_RB_ID3;
		screen->swap_rg = SWAP_RG_ID3;
		screen->swap_gb = SWAP_GB_ID3;
		screen->swap_delta = 0;
		screen->swap_dumy = 0;

		/* Operation function*/
		screen->init = NULL;
		screen->standby = NULL;

		break;

	case BOARD_ID_C7018:
			
		/* screen type & face */
		screen->type = SCREEN_TYPE_ID4;
		screen->face = OUT_FACE_ID4;

		/* Screen size */
		screen->x_res = H_VD_ID4;
		screen->y_res = V_VD_ID4;

		screen->width = LCD_WIDTH_ID4;
		screen->height = LCD_HEIGHT_ID4;

		/* Timing */
		screen->lcdc_aclk = LCDC_ACLK_ID4;
		screen->pixclock = DCLK_ID4;
		screen->left_margin = H_BP_ID4;
		screen->right_margin = H_FP_ID4;
		screen->hsync_len = H_PW_ID4;
		screen->upper_margin = V_BP_ID4;
		screen->lower_margin = V_FP_ID4;
		screen->vsync_len = V_PW_ID4;

		/* Pin polarity */
		screen->pin_hsync = HSYNC_POL_ID4;
		screen->pin_vsync = VSYNC_POL_ID4;
		screen->pin_den = DEN_POL_ID4;
		screen->pin_dclk = DCLK_POL_ID4;

		/* Swap rule */
		screen->swap_rb = SWAP_RB_ID4;
		screen->swap_rg = SWAP_RG_ID4;
		screen->swap_gb = SWAP_GB_ID4;
		screen->swap_delta = 0;
		screen->swap_dumy = 0;

		/* Operation function*/
		screen->init = NULL;
		screen->standby = NULL;

		break;
		

	}


	printk("%s:board_id=%d\n",__func__,id);
   
}

#endif

#endif
