/*
 * drivers/video/sun3i/disp/de_bsp/de/disp_event.c
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

#include "disp_event.h"
#include "disp_display.h"
#include "disp_de.h"
#include "disp_video.h"
#include "disp_scaler.h"

__bool Is_In_Valid_Regn(__u8 sel,__u32 line)
{
    if(gdisp.screen[sel].lcdc_status & LCDC_TCON0_USED)
    {
         return TCON0_in_valid_regn(sel,line);
    }
    else if(gdisp.screen[sel].lcdc_status & LCDC_TCON1_USED)
    {
         return TCON1_in_valid_regn(sel,line);
    }
    else
    {
        return TRUE;
    }
}

__s32 BSP_disp_cmd_cache(__u32 sel)
{
    gdisp.screen[sel].cache_flag = TRUE;
    return DIS_SUCCESS;
}

__s32 BSP_disp_cmd_submit(__u32 sel)
{
    gdisp.screen[sel].cache_flag = FALSE;

    return DIS_SUCCESS;
}

__s32 BSP_disp_cfg_start(__u32 sel)
{
	gdisp.screen[sel].cfg_cnt++;

	return DIS_SUCCESS;
}

__s32 BSP_disp_cfg_finish(__u32 sel)
{
	gdisp.screen[sel].cfg_cnt--;

	return DIS_SUCCESS;
}

void LCD_vbi_event_proc(__u32 sel)
{
    __u32 i = 0;

	Video_Operation_In_Vblanking(sel);

    if(Is_In_Valid_Regn(sel,LCDC_get_start_delay(0)-3) == FALSE)
	{
		return ;
	}

    if(gdisp.screen[sel].LCD_CPUIF_ISR)
    {
    	(*gdisp.screen[sel].LCD_CPUIF_ISR)();
    }

    if(gdisp.screen[sel].cache_flag == FALSE && gdisp.screen[sel].cfg_cnt == 0)
    {
        for(i=0; i<2; i++)
        {
            if(gdisp.scaler[i].b_reg_change && (gdisp.scaler[i].status & SCALER_USED) && (gdisp.scaler[i].screen_index == sel))
            {
                DE_SCAL_Set_Reg_Rdy(i);
                DE_SCAL_Reset(i);
                DE_SCAL_Start(i);
                gdisp.scaler[i].b_reg_change = FALSE;
            }
            if(gdisp.scaler[i].b_close == TRUE)
            {
                Scaler_close(i);
                gdisp.scaler[i].b_close = FALSE;
            }
        }
        DE_BE_Cfg_Ready(sel);
		gdisp.screen[sel].have_cfg_reg = TRUE;
    }

#if 0
{
    __u32 num;

    if(gdisp.screen[sel].lcdc_status & LCDC_TCON0_USED)
    {
        LCDC_get_cur_line_num(sel, 0);
    }
    else if(gdisp.screen[sel].lcdc_status & LCDC_TCON1_USED)
    {
        LCDC_get_cur_line_num(sel, 1);
    }
	if(num > 5)
	{
    	DE_INF("%d\n",num);
    }
}
#endif

    return ;
}

void LCD_line_event_proc(__u32 sel)
{
	if(gdisp.screen[sel].have_cfg_reg)
	{
	    gdisp.init_para.disp_int_process(sel);
	    gdisp.screen[sel].have_cfg_reg = FALSE;
	}
}
