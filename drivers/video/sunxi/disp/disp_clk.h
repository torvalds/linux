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

#ifndef __DISP_CLK_H__
#define __DISP_CLK_H__

#include "disp_display_i.h"

/*
 * record tv/vga/hdmi mode clock requirement
 */
typedef struct {
	/* required clock frequency for LCDx_ch1_clk2, for tv output used ,Hz */
	__u32 tve_clk;
	/*
	 * required divide LCDx_ch1_clk2 by 2 for LCDx_ch1_clk1 or NOT:
	 * 1:not divided , 2: divide by two
	 */
	__u32 pre_scale;
	/* required clock frequency for internal hdmi module, Hz */
	__u32 hdmi_clk;
	/* required pll frequency for VIDEO_PLL0(1x) or VIDEO_PLL1(1x), Hz */
	__u32 pll_clk;
	/* required 2x VIDEO_PLL or NOT: 0:no, 1: required */
	__u32 pll_2x;

} __disp_tv_vga_clk_t;

typedef struct {
	/* number related to number of tv mode supported */
	__disp_tv_vga_clk_t tv_clk_tab[30];
	/* number related to number of vga mode supported */
	__disp_tv_vga_clk_t vga_clk_tab[12];
} __disp_clk_tab;

__s32 image_clk_init(__u32 sel);
__s32 image_clk_exit(__u32 sel);
__s32 image_clk_on(__u32 sel);
__s32 image_clk_off(__u32 sel);

__s32 scaler_clk_init(__u32 sel);
__s32 scaler_clk_exit(__u32 sel);
__s32 scaler_clk_on(__u32 sel);
__s32 scaler_clk_off(__u32 sel);

__s32 lcdc_clk_init(__u32 sel);
__s32 lcdc_clk_exit(__u32 sel);
__s32 lcdc_clk_on(__u32 sel);
__s32 lcdc_clk_off(__u32 sel);

__s32 tve_clk_init(__u32 sel);
__s32 tve_clk_exit(__u32 sel);
__s32 tve_clk_on(__u32 sel);
__s32 tve_clk_off(__u32 sel);

__s32 hdmi_clk_init(void);
__s32 hdmi_clk_exit(void);
__s32 hdmi_clk_on(void);
__s32 hdmi_clk_off(void);

__s32 lvds_clk_init(void);
__s32 lvds_clk_exit(void);
__s32 lvds_clk_on(void);
__s32 lvds_clk_off(void);

__s32 disp_pll_init(void);
__s32 disp_clk_cfg(__u32 sel, __u32 type, __u8 mode);

extern __disp_clk_tab clk_tab;
extern __u32 g_clk_status;

#endif
