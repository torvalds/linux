/*
 * drivers/video/sunxi/lcd/lcd_bak/hv_800x480.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Danling <danliang@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

/**********************************************************************
 *
 *  hv_800x480.c
 *
 **********************************************************************/

#include "lcd_panel_cfg.h"

//delete this line if you want to use the lcd para define in sys_config1.fex
//#define LCD_PARA_USE_CONFIG

#ifdef LCD_PARA_USE_CONFIG

/**********************************************************************
 *
 *  tcon parameters
 *
 **********************************************************************/
static void LCD_cfg_panel_info(__panel_para_t * info)
{
    memset(info,0,sizeof(__panel_para_t));

	//interface
    info->lcd_if            = 0;        	//0:hv; 		1:cpu/8080; 	2:reserved; 	3:lvds
    info->lcd_hv_if         = 0;        	//0:hv para;	1:hv serial; 	2:ccir656

    //timing
    info->lcd_x             = 800;			//Hor Pixels
    info->lcd_y             = 480;			//Ver Pixels
    info->lcd_dclk_freq     = 33;       	//Pixel Data Cycle,in MHz
    info->lcd_ht            = 1056;     	//Hor Total Time
    info->lcd_hbp           = 216;      	//Hor Back Porch
    info->lcd_vt            = 525*2;  		//Ver Total Time*2
    info->lcd_vbp           = 35;       	//Ver Back Porch
    info->lcd_hv_hspw       = 10;       	//Hor Sync Time
    info->lcd_hv_vspw       = 10;       	//Ver Sync Time
    info->lcd_io_cfg0       = 0x10000000;	//Clock Phase

	//color
    info->lcd_frm           = 0;        	//0: direct; 	1: rgb666 dither;	2:rgb656 dither
    info->lcd_gamma_correction_en = 0;

    info->lcd_pwm_not_used  = 0;
    info->lcd_pwm_ch        = 0;
    info->lcd_pwm_freq      = 12500;		//Hz
    info->lcd_pwm_pol       = 0;
}

#endif

/**********************************************************************
 *
 *  do not modify
 *
 **********************************************************************/
void LCD_get_panel_funs_0(__lcd_panel_fun_t * fun)
{
#ifdef LCD_PARA_USE_CONFIG
    fun->cfg_panel_info = LCD_cfg_panel_info;
#endif
}

