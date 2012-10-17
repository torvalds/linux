/*
 * drivers/video/sunxi/disp/de_bsp_sun5i/de/disp_de.c
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

#include "disp_de.h"
#include "disp_display.h"
#include "disp_event.h"
#include "disp_scaler.h"
#include "disp_clk.h"
#include "disp_lcd.h"

__s32 Image_init(__u32 sel)
{
    
    image_clk_init(sel);
	image_clk_on(sel);	//when access image registers, must open MODULE CLOCK of image
	DE_BE_Reg_Init(sel);
	
    BSP_disp_sprite_init(sel);
    BSP_disp_set_output_csc(sel, DISP_OUTPUT_TYPE_LCD,gdisp.screen[sel].iep_status&DRC_USED);

    Image_open(sel);

    DE_BE_EnableINT(sel, DE_IMG_REG_LOAD_FINISH);
    DE_BE_reg_auto_load_en(sel, 0);

    return DIS_SUCCESS;
}
      
__s32 Image_exit(__u32 sel)
{
    DE_BE_DisableINT(sel, DE_IMG_REG_LOAD_FINISH);
    BSP_disp_sprite_exit(sel);
    image_clk_exit(sel);
        
    return DIS_SUCCESS;
}

__s32 Image_open(__u32  sel)
{
   DE_BE_Enable(sel);
      
   return DIS_SUCCESS;
}
      

__s32 Image_close(__u32 sel)
{
   DE_BE_Disable(sel);
   
   gdisp.screen[sel].status &= IMAGE_USED_MASK;
   
   return DIS_SUCCESS;
}


__s32 BSP_disp_set_bright(__u32 sel, __u32 bright)
{
    gdisp.screen[sel].bright = bright;
    BSP_disp_set_output_csc(sel, gdisp.screen[sel].output_type, gdisp.screen[sel].iep_status&DRC_USED);

    return DIS_SUCCESS;
}

__s32 BSP_disp_get_bright(__u32 sel)
{
    return gdisp.screen[sel].bright;
}

__s32 BSP_disp_set_contrast(__u32 sel, __u32 contrast)
{
    gdisp.screen[sel].contrast = contrast;
    BSP_disp_set_output_csc(sel, gdisp.screen[sel].output_type, gdisp.screen[sel].iep_status&DRC_USED);

    return DIS_SUCCESS;
}

__s32 BSP_disp_get_contrast(__u32 sel)
{
    return gdisp.screen[sel].contrast;
}

__s32 BSP_disp_set_saturation(__u32 sel, __u32 saturation)
{
    gdisp.screen[sel].saturation = saturation;
    BSP_disp_set_output_csc(sel, gdisp.screen[sel].output_type, gdisp.screen[sel].iep_status&DRC_USED);

    return DIS_SUCCESS;
}

__s32 BSP_disp_get_saturation(__u32 sel)
{
    return gdisp.screen[sel].saturation;
}

__s32 BSP_disp_set_hue(__u32 sel, __u32 hue)
{
    gdisp.screen[sel].hue = hue;
    BSP_disp_set_output_csc(sel, gdisp.screen[sel].output_type, gdisp.screen[sel].iep_status&DRC_USED);

    return DIS_SUCCESS;
}

__s32 BSP_disp_get_hue(__u32 sel)
{
    return gdisp.screen[sel].hue;
}


__s32 BSP_disp_set_screen_size(__u32 sel, __disp_rectsz_t * size)
{    
    DE_BE_set_display_size(sel, size->width, size->height);

    gdisp.screen[sel].screen_width = size->width;
    gdisp.screen[sel].screen_height= size->height;

    return DIS_SUCCESS;
}

__s32 BSP_disp_set_output_csc(__u32 sel, __u32 out_type, __u32 drc_en)
{
    __disp_color_range_t out_color_range = DISP_COLOR_RANGE_0_255;
    __u32 out_csc = 0;//out_csc: 0:rgb  1:yuv  2:igb

    if(out_type == DISP_OUTPUT_TYPE_HDMI)
    {
        __s32 ret = 0;
        __s32 value = 0;
        
        out_color_range = DISP_COLOR_RANGE_16_255;

        ret = OSAL_Script_FetchParser_Data("disp_init", "screen0_out_color_range", &value, 1);
        if(ret < 0)
        {
            DE_INF("fetch script data disp_init.screen0_out_color_range fail\n");
        }
        else
        {
            out_color_range = value;
            DE_INF("screen0_out_color_range = %d\n", value);
        }
        out_csc = 0;
    }
    else if(out_type == DISP_OUTPUT_TYPE_LCD)
    {
        out_csc = 0;
    }
    else if(out_type == DISP_OUTPUT_TYPE_TV)
    {
        out_csc = 1;
    }

    if(drc_en)
    {
        out_csc = 2;
    }

    DE_BE_Set_Enhance(sel, out_csc, out_color_range, gdisp.screen[sel].bright, gdisp.screen[sel].contrast, gdisp.screen[sel].saturation, gdisp.screen[sel].hue);

    return DIS_SUCCESS;
}
