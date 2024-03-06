/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2020 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 * Copyright (C) 2018 Amlogic, Inc. All rights reserved.
 */

#ifndef __MESON_DW_MIPI_DSI_H
#define __MESON_DW_MIPI_DSI_H

/* Top-level registers */
/* [31: 4]    Reserved.     Default 0.
 *     [3] RW timing_rst_n: Default 1.
 *		1=Assert SW reset of timing feature.   0=Release reset.
 *     [2] RW dpi_rst_n: Default 1.
 *		1=Assert SW reset on mipi_dsi_host_dpi block.   0=Release reset.
 *     [1] RW intr_rst_n: Default 1.
 *		1=Assert SW reset on mipi_dsi_host_intr block.  0=Release reset.
 *     [0] RW dwc_rst_n:  Default 1.
 *		1=Assert SW reset on IP core.   0=Release reset.
 */
#define MIPI_DSI_TOP_SW_RESET                      0x3c0

#define MIPI_DSI_TOP_SW_RESET_DWC	BIT(0)
#define MIPI_DSI_TOP_SW_RESET_INTR	BIT(1)
#define MIPI_DSI_TOP_SW_RESET_DPI	BIT(2)
#define MIPI_DSI_TOP_SW_RESET_TIMING	BIT(3)

/* [31: 5] Reserved.   Default 0.
 *     [4] RW manual_edpihalt: Default 0.
 *		1=Manual suspend VencL; 0=do not suspend VencL.
 *     [3] RW auto_edpihalt_en: Default 0.
 *		1=Enable IP's edpihalt signal to suspend VencL;
 *		0=IP's edpihalt signal does not affect VencL.
 *     [2] RW clock_freerun: Apply to auto-clock gate only. Default 0.
 *		0=Default, use auto-clock gating to save power;
 *		1=use free-run clock, disable auto-clock gating, for debug mode.
 *     [1] RW enable_pixclk: A manual clock gate option, due to DWC IP does not
 *		have auto-clock gating. 1=Enable pixclk.      Default 0.
 *     [0] RW enable_sysclk: A manual clock gate option, due to DWC IP does not
 *		have auto-clock gating. 1=Enable sysclk.      Default 0.
 */
#define MIPI_DSI_TOP_CLK_CNTL                      0x3c4

#define MIPI_DSI_TOP_CLK_SYSCLK_EN	BIT(0)
#define MIPI_DSI_TOP_CLK_PIXCLK_EN	BIT(1)

/* [31:24]    Reserved. Default 0.
 * [23:20] RW dpi_color_mode: Define DPI pixel format. Default 0.
 *		0=16-bit RGB565 config 1;
 *		1=16-bit RGB565 config 2;
 *		2=16-bit RGB565 config 3;
 *		3=18-bit RGB666 config 1;
 *		4=18-bit RGB666 config 2;
 *		5=24-bit RGB888;
 *		6=20-bit YCbCr 4:2:2;
 *		7=24-bit YCbCr 4:2:2;
 *		8=16-bit YCbCr 4:2:2;
 *		9=30-bit RGB;
 *		10=36-bit RGB;
 *		11=12-bit YCbCr 4:2:0.
 *    [19] Reserved. Default 0.
 * [18:16] RW in_color_mode:  Define VENC data width. Default 0.
 *		0=30-bit pixel;
 *		1=24-bit pixel;
 *		2=18-bit pixel, RGB666;
 *		3=16-bit pixel, RGB565.
 * [15:14] RW chroma_subsample: Define method of chroma subsampling. Default 0.
 *		Applicable to YUV422 or YUV420 only.
 *		0=Use even pixel's chroma;
 *		1=Use odd pixel's chroma;
 *		2=Use averaged value between even and odd pair.
 * [13:12] RW comp2_sel:  Select which component to be Cr or B: Default 2.
 *		0=comp0; 1=comp1; 2=comp2.
 * [11:10] RW comp1_sel:  Select which component to be Cb or G: Default 1.
 *		0=comp0; 1=comp1; 2=comp2.
 *  [9: 8] RW comp0_sel:  Select which component to be Y  or R: Default 0.
 *		0=comp0; 1=comp1; 2=comp2.
 *     [7]    Reserved. Default 0.
 *     [6] RW de_pol:  Default 0.
 *		If DE input is active low, set to 1 to invert to active high.
 *     [5] RW hsync_pol: Default 0.
 *		If HS input is active low, set to 1 to invert to active high.
 *     [4] RW vsync_pol: Default 0.
 *		If VS input is active low, set to 1 to invert to active high.
 *     [3] RW dpicolorm: Signal to IP.   Default 0.
 *     [2] RW dpishutdn: Signal to IP.   Default 0.
 *     [1]    Reserved.  Default 0.
 *     [0]    Reserved.  Default 0.
 */
#define MIPI_DSI_TOP_CNTL                          0x3c8

/* VENC data width */
#define VENC_IN_COLOR_30B   0x0
#define VENC_IN_COLOR_24B   0x1
#define VENC_IN_COLOR_18B   0x2
#define VENC_IN_COLOR_16B   0x3

/* DPI pixel format */
#define DPI_COLOR_16BIT_CFG_1		0
#define DPI_COLOR_16BIT_CFG_2		1
#define DPI_COLOR_16BIT_CFG_3		2
#define DPI_COLOR_18BIT_CFG_1		3
#define DPI_COLOR_18BIT_CFG_2		4
#define DPI_COLOR_24BIT			5
#define DPI_COLOR_20BIT_YCBCR_422	6
#define DPI_COLOR_24BIT_YCBCR_422	7
#define DPI_COLOR_16BIT_YCBCR_422	8
#define DPI_COLOR_30BIT			9
#define DPI_COLOR_36BIT			10
#define DPI_COLOR_12BIT_YCBCR_420	11

#define MIPI_DSI_TOP_DPI_COLOR_MODE	GENMASK(23, 20)
#define MIPI_DSI_TOP_IN_COLOR_MODE	GENMASK(18, 16)
#define MIPI_DSI_TOP_CHROMA_SUBSAMPLE	GENMASK(15, 14)
#define MIPI_DSI_TOP_COMP2_SEL		GENMASK(13, 12)
#define MIPI_DSI_TOP_COMP1_SEL		GENMASK(11, 10)
#define MIPI_DSI_TOP_COMP0_SEL		GENMASK(9, 8)
#define MIPI_DSI_TOP_DE_INVERT		BIT(6)
#define MIPI_DSI_TOP_HSYNC_INVERT	BIT(5)
#define MIPI_DSI_TOP_VSYNC_INVERT	BIT(4)
#define MIPI_DSI_TOP_DPICOLORM		BIT(3)
#define MIPI_DSI_TOP_DPISHUTDN		BIT(2)

#define MIPI_DSI_TOP_SUSPEND_CNTL                  0x3cc
#define MIPI_DSI_TOP_SUSPEND_LINE                  0x3d0
#define MIPI_DSI_TOP_SUSPEND_PIX                   0x3d4
#define MIPI_DSI_TOP_MEAS_CNTL                     0x3d8
/* [0] R  stat_edpihalt:  edpihalt signal from IP.    Default 0. */
#define MIPI_DSI_TOP_STAT                          0x3dc
#define MIPI_DSI_TOP_MEAS_STAT_TE0                 0x3e0
#define MIPI_DSI_TOP_MEAS_STAT_TE1                 0x3e4
#define MIPI_DSI_TOP_MEAS_STAT_VS0                 0x3e8
#define MIPI_DSI_TOP_MEAS_STAT_VS1                 0x3ec
/* [31:16] RW intr_stat/clr. Default 0.
 *		For each bit, read as this interrupt level status,
 *		write 1 to clear.
 * [31:22] Reserved
 * [   21] stat/clr of eof interrupt
 * [   21] vde_fall interrupt
 * [   19] stat/clr of de_rise interrupt
 * [   18] stat/clr of vs_fall interrupt
 * [   17] stat/clr of vs_rise interrupt
 * [   16] stat/clr of dwc_edpite interrupt
 * [15: 0] RW intr_enable. Default 0.
 *		For each bit, 1=enable this interrupt, 0=disable.
 *	[15: 6] Reserved
 *	[    5] eof interrupt
 *	[    4] de_fall interrupt
 *	[    3] de_rise interrupt
 *	[    2] vs_fall interrupt
 *	[    1] vs_rise interrupt
 *	[    0] dwc_edpite interrupt
 */
#define MIPI_DSI_TOP_INTR_CNTL_STAT                0x3f0
// 31: 2    Reserved.   Default 0.
//  1: 0 RW mem_pd.     Default 3.
#define MIPI_DSI_TOP_MEM_PD                        0x3f4

#endif /* __MESON_DW_MIPI_DSI_H */
