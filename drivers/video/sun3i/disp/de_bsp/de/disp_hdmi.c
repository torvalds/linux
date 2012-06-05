/*
 * drivers/video/sun3i/disp/de_bsp/de/disp_hdmi.c
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

#include "disp_hdmi.h"
#include "disp_display.h"
#include "disp_event.h"
#include "disp_de.h"
#include "disp_tv.h"
#include "disp_lcd.h"
#include "disp_clk.h"


__s32 Display_Hdmi_Init(void)
{
	gdisp.screen[0].hdmi_mode = DISP_TV_MOD_720P_50HZ;
	gdisp.screen[1].hdmi_mode = DISP_TV_MOD_720P_50HZ;

	return DIS_SUCCESS;
}

__s32 Display_Hdmi_Exit(void)
{
	return DIS_SUCCESS;
}

__s32 BSP_disp_hdmi_open(__u32 sel)
{
    if(!(gdisp.screen[sel].status & HDMI_ON))
    {
    	__disp_tv_mode_t     tv_mod;

    	tv_mod = gdisp.screen[sel].hdmi_mode;

    	lcdc_clk_on(sel);
    	image_clk_on(sel);
		Image_open(sel);//set image normal channel start bit , because every de_clk_off( )will reset this bit
    	//tve_clk_on();
    	disp_clk_cfg(sel,DISP_OUTPUT_TYPE_HDMI, tv_mod);
    	Disp_lcdc_pin_cfg(sel, DISP_OUTPUT_TYPE_HDMI, 1);

    	TCON1_set_hdmi_mode(sel,tv_mod);
    	TCON1_open(sel);
    	gdisp.init_para.Hdmi_open();

    	Disp_Switch_Dram_Mode(DISP_OUTPUT_TYPE_HDMI, tv_mod);

    	gdisp.screen[sel].status |= HDMI_ON;
        gdisp.screen[sel].lcdc_status |= LCDC_TCON1_USED;
        gdisp.screen[sel].output_type = DISP_OUTPUT_TYPE_HDMI;
    }

    return DIS_SUCCESS;
}

__s32 BSP_disp_hdmi_close(__u32 sel)
{
    if(gdisp.screen[sel].status & HDMI_ON)
    {
        gdisp.init_para.Hdmi_close();
    	TCON1_close(sel);

    	//tve_clk_off();
    	image_clk_off(sel);
    	lcdc_clk_off(sel);

        gdisp.screen[sel].lcdc_status &= LCDC_TCON1_USED_MASK;
    	gdisp.screen[sel].status &= HDMI_OFF;
    	gdisp.screen[sel].output_type = DISP_OUTPUT_TYPE_NONE;
		gdisp.screen[sel].pll_use_status &= ((gdisp.screen[sel].pll_use_status == VIDEO_PLL0_USED)? VIDEO_PLL0_USED_MASK : VIDEO_PLL1_USED_MASK);

		Disp_lcdc_pin_cfg(sel, DISP_OUTPUT_TYPE_HDMI, 0);
    }

	return DIS_SUCCESS;
}

__s32 BSP_disp_hdmi_set_mode(__u32 sel, __disp_tv_mode_t  mode)
{
    if(mode < DISP_TV_MOD_480I ||  mode > DISP_TV_MOD_1080P_60HZ)
    {
        DE_WRN("unsupported hdmi mode in BSP_disp_hdmi_set_mode\n");
        return DIS_FAIL;
    }

	gdisp.init_para.hdmi_set_mode(mode);

	gdisp.screen[sel].hdmi_mode = mode;
	gdisp.screen[sel].output_type = DISP_OUTPUT_TYPE_HDMI;

	return DIS_SUCCESS;
}

__s32 BSP_disp_hdmi_get_mode(__u32 sel)
{
    return gdisp.screen[sel].hdmi_mode;
}

__s32 BSP_disp_hdmi_check_support_mode(__u32 sel, __u8  mode)
{
	__s32          ret = 0;

	ret = gdisp.init_para.hdmi_mode_support(mode);

	return ret;
}

__s32 BSP_disp_hdmi_get_hpd_status(__u32 sel)
{
	__s32          ret = 0;

	ret = gdisp.init_para.hdmi_get_HPD_status();

	return ret;
}

__s32 BSP_disp_hdmi_set_src(__u32 sel, __disp_lcdc_src_t src)
{
    switch (src)
    {
        case DISP_LCDC_SRC_DE_CH1:
            TCON1_select_src(sel, SRC_DE_CH1);
            break;

        case DISP_LCDC_SRC_DE_CH2:
            TCON1_select_src(sel, SRC_DE_CH2);
            break;

        case DISP_LCDC_SRC_BLUT:
            TCON1_select_src(sel, SRC_BLUE);
            break;

        default:
            DE_WRN("not supported lcdc src:%d in BSP_disp_tv_set_src\n", src);
            return DIS_NOT_SUPPORT;
    }
    return DIS_SUCCESS;
}

