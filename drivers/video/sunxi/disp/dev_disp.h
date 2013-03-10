/*
 * Copyright (C) 2007-2012 Allwinner Technology Co., Ltd.
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

#ifndef __DEV_DISP_H__
#define __DEV_DISP_H__

enum {
	SUNXI_LCD,
	SUNXI_HDMI
};

__s32 disp_create_heap(__u32 pHeapHead, __u32 nHeapSize);
void *disp_malloc(__u32 num_bytes);
void disp_free(void *p);

__s32 DRV_disp_int_process(__u32 sel);

__s32 DRV_DISP_Init(void);
__s32 DRV_DISP_Exit(void);

int disp_suspend(int clk, int status);
int disp_resume(int clk, int status);

void hdmi_edid_received(unsigned char *edid, int block);
__s32 Fb_Init(__u32 from);
__s32 DRV_lcd_open(__u32 sel);
__s32 DRV_lcd_close(__u32 sel);

__s32 disp_set_hdmi_func(__disp_hdmi_func *func);
__s32 disp_get_pll_freq(__u32 pclk, __u32 *pll_freq,  __u32 *pll_2x);

#endif
