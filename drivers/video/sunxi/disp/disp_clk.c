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

#include "disp_display_i.h"
#include "disp_display.h"
#include "disp_lcd.h"
#include "disp_clk.h"
#include "OSAL_Clock.h"

#define CLK_ON 1
#define CLK_OFF 0
#define RST_INVALID 0
#define RST_VALID   1

#define CLK_DEBE0_AHB_ON	0x00000001
#define CLK_DEBE0_MOD_ON	0x00000002
#define CLK_DEBE0_DRAM_ON	0x00000004
#define CLK_DEBE1_AHB_ON	0x00000010
#define CLK_DEBE1_MOD_ON	0x00000020
#define CLK_DEBE1_DRAM_ON	0x00000040
#define CLK_DEFE0_AHB_ON	0x00000100
#define CLK_DEFE0_MOD_ON	0x00000200
#define CLK_DEFE0_DRAM_ON	0x00000400
#define CLK_DEFE1_AHB_ON	0x00001000
#define CLK_DEFE1_MOD_ON	0x00002000
#define CLK_DEFE1_DRAM_ON	0x00004000
#define CLK_LCDC0_AHB_ON	0x00010000
#define CLK_LCDC0_MOD0_ON	0x00020000
/* represent lcd0-ch1-clk1 and lcd0-ch1-clk2 */
#define CLK_LCDC0_MOD1_ON	0x00040000
#define CLK_LCDC1_AHB_ON	0x00100000
#define CLK_LCDC1_MOD0_ON	0x00200000
/* represent lcd1-ch1-clk1 and lcd1-ch1-clk2 */
#define CLK_LCDC1_MOD1_ON	0x00400000
#define CLK_TVENC0_AHB_ON	0x01000000
#define CLK_TVENC1_AHB_ON	0x02000000
#define CLK_HDMI_AHB_ON		0x10000000
#define CLK_HDMI_MOD_ON		0x20000000
//#define CLK_LVDS_MOD_ON

static __hdle h_debe0ahbclk, h_debe0mclk, h_debe0dramclk;
static __hdle h_debe1ahbclk, h_debe1mclk, h_debe1dramclk;
static __hdle h_defe0ahbclk, h_defe0mclk, h_defe0dramclk;
static __hdle h_defe1ahbclk, h_defe1mclk, h_defe1dramclk;
static __hdle h_tvenc0ahbclk;
static __hdle h_tvenc1ahbclk;
static __hdle h_lcd0ahbclk, h_lcd0ch0mclk0, h_lcd0ch1mclk1, h_lcd0ch1mclk2;
static __hdle h_lcd1ahbclk, h_lcd1ch0mclk0, h_lcd1ch1mclk1, h_lcd1ch1mclk2;
static __hdle h_lvdsmclk;		/* only for reset */
static __hdle h_hdmiahbclk, h_hdmimclk;

__u32 g_clk_status;

#define RESET_OSAL

/* record tv/vga/hdmi mode clock requirement */
__disp_clk_tab clk_tab = {
	/* { LCDx_CH1_CLK2, CLK2/CLK1, HDMI_CLK, PLL_CLK, PLLX2 req}, MODE, INDEX (FOLLOW enum order) */
	{ /* TV mode and HDMI mode */
	  /* HDG: Note only HDMI_CLK is used now, and only in TV mode. */
		{ 27000000, 2,  27000000, 270000000, 0}, /* DISP_TV_MOD_480I, 0x0 */
		{ 27000000, 2,  27000000, 270000000, 0}, /* DISP_TV_MOD_576I, 0x1 */
		{ 54000000, 2,  27000000, 270000000, 0}, /* DISP_TV_MOD_480P, 0x2 */
		{ 54000000, 2,  27000000, 270000000, 0}, /* DISP_TV_MOD_576P, 0x3 */
		{ 74250000, 1,  74250000, 297000000, 0}, /* DISP_TV_MOD_720P_50HZ, 0x4 */
		{ 74250000, 1,  74250000, 297000000, 0}, /* DISP_TV_MOD_720P_60HZ , 0x5 */
		{ 74250000, 1,  74250000, 297000000, 0}, /* DISP_TV_MOD_1080I_50HZ, 0x6 */
		{ 74250000, 1,  74250000, 297000000, 0}, /* DISP_TV_MOD_1080I_60HZ, 0x7 */
		{ 74250000, 1,  74250000, 297000000, 0}, /* DISP_TV_MOD_1080P_24HZ, 0x8 */
		{148500000, 1, 148500000, 297000000, 0}, /* DISP_TV_MOD_1080P_50HZ, 0x9 */
		{148500000, 1, 148500000, 297000000, 0}, /* DISP_TV_MOD_1080P_60HZ, 0xa */
		{ 27000000, 2,  27000000, 270000000, 0}, /* DISP_TV_MOD_PAL//0xb */
		{ 27000000, 2,  27000000, 270000000, 0}, /* DISP_TV_MOD_PAL_SVIDEO, 0xc */
		{0, 1, 0, 0, 0}, /* reserved//0xd */
		{ 27000000, 2,  27000000, 270000000, 0}, /* DISP_TV_MOD_NTSC, 0xe */
		{ 27000000, 2,  27000000, 270000000, 0}, /* DISP_TV_MOD_NTSC_SVIDEO//0xf */
		{0, 1, 0, 0, 0}, /* reserved , 0x10 */
		{ 27000000, 2,  27000000, 270000000, 0}, /* DISP_TV_MOD_PAL_M, 0x11 */
		{ 27000000, 2,  27000000, 270000000, 0}, /* DISP_TV_MOD_PAL_M_SVIDEO, 0x12 */
		{0, 1, 0, 0, 0}, /* reserved, 0x13 */
		{ 27000000, 2,  27000000, 270000000, 0}, /* DISP_TV_MOD_PAL_NC, 0x14 */
		{ 27000000, 2,  27000000, 270000000, 0}, /* DISP_TV_MOD_PAL_NC_SVIDEO, 0x15 */
		{0, 1, 0, 0, 0}, /* reserved//0x16 */
		{148500000, 1, 148500000, 297000000, 0}, /* DISP_TV_MOD_1080P_24HZ_3D_FP, 0x17 */
		{148500000, 1, 148500000, 297000000, 0}, /* DISP_TV_MOD_720P_50HZ_3D_FP, 0x18 */
		{148500000, 1, 148500000, 297000000, 0}, /* DISP_TV_MOD_720P_60HZ_3D_FP, 0x19 */
		{ 85500000, 1,  85500000, 342000000, 0}, /* DISP_TV_H1360_V768_60HZ, 0x1a */
		{108000000, 1, 108000000, 108000000, 0}, /* DISP_TV_H1280_V1024_60HZ, 0x1b */
		{0, 1, 0, 0, 0}, /* reserved, 0x1c */
		{0, 1, 0, 0, 0}, /* reserved, 0x1d */
	}, { /* VGA mode */
		{147000000, 1, 147000000, 294000000, 0}, /* DISP_VGA_H1680_V1050, 0X0 */
		{106800000, 1, 106800000, 267000000, 1}, /* DISP_VGA_H1440_V900, 0X1 */
		{ 86000000, 1,  86000000, 258000000, 0}, /* DISP_VGA_H1360_V768, 0X2 */
		{108000000, 1, 108000000, 270000000, 1}, /* DISP_VGA_H1280_V1024, 0X3 */
		{ 65250000, 1,  65250000, 261000000, 0}, /* DISP_VGA_H1024_V768, 0X4 */
		{ 39857143, 1,  39857143, 279000000, 0}, /* DISP_VGA_H800_V600, 0X5 */
		{ 25090909, 1,  25090909, 276000000, 0}, /* DISP_VGA_H640_V480  0X6 */
		{0, 1, 0, 0, 0}, /* DISP_VGA_H1440_V900_RB, 0X7 */
		{0, 1, 0, 0, 0}, /* DISP_VGA_H1680_V1050_RB, 0X8 */
		{138000000, 1, 138000000, 276000000, 0}, /* DISP_VGA_H1920_V1080_RB, 0X9 */
		{148500000, 1, 148500000, 297000000, 0}, /* DISP_VGA_H1920_V1080, 0xa */
		{ 74250000, 1,  74250000, 297000000, 0}, /* DISP_VGA_H1280_V720, 0xb */
	}
};

/* 300MHz */
#define DEBE_CLOCK_SPEED_LIMIT 300000000

__s32 image_clk_init(__u32 sel)
{
	__u32 dram_pll, pll5_div;

	if (sel == 0) {
		h_debe0ahbclk = OSAL_CCMU_OpenMclk(AW_MOD_CLK_AHB_DEBE0);
		h_debe0mclk = OSAL_CCMU_OpenMclk(AW_MOD_CLK_DEBE0);
		h_debe0dramclk = OSAL_CCMU_OpenMclk(AW_MOD_CLK_SDRAM_DEBE0);

		/* NEW OSAL_clk reset */
#ifdef RESET_OSAL
		OSAL_CCMU_MclkReset(h_debe0mclk, RST_INVALID);
#endif
		/* FIX CONNECT TO DRAM PLL */
		OSAL_CCMU_SetMclkSrc(h_debe0mclk, AW_SYS_CLK_PLL5P);

		dram_pll = OSAL_CCMU_GetSrcFreq(AW_SYS_CLK_PLL5P);
		pll5_div = DIV_ROUND_UP(dram_pll, DEBE_CLOCK_SPEED_LIMIT);
		OSAL_CCMU_SetMclkDiv(h_debe0mclk, pll5_div);

		OSAL_CCMU_MclkOnOff(h_debe0ahbclk, CLK_ON);
		if (sunxi_is_sun4i()) {
			OSAL_CCMU_MclkOnOff(h_debe0dramclk, CLK_ON);
			OSAL_CCMU_MclkOnOff(h_debe0dramclk, CLK_OFF);
		}
		OSAL_CCMU_MclkOnOff(h_debe0mclk, CLK_ON);

		g_clk_status |= (CLK_DEBE0_AHB_ON | CLK_DEBE0_MOD_ON);
	} else if (sel == 1) {
		h_debe1ahbclk = OSAL_CCMU_OpenMclk(AW_MOD_CLK_AHB_DEBE1);
		h_debe1mclk = OSAL_CCMU_OpenMclk(AW_MOD_CLK_DEBE1);
		h_debe1dramclk = OSAL_CCMU_OpenMclk(AW_MOD_CLK_SDRAM_DEBE1);
#ifdef RESET_OSAL

		OSAL_CCMU_MclkReset(h_debe1mclk, RST_INVALID);
#endif
		/* FIX CONNECT TO DRAM PLL */
		OSAL_CCMU_SetMclkSrc(h_debe1mclk, AW_SYS_CLK_PLL5P);

		dram_pll = OSAL_CCMU_GetSrcFreq(AW_SYS_CLK_PLL5P);
		pll5_div = DIV_ROUND_UP(dram_pll, DEBE_CLOCK_SPEED_LIMIT);
		OSAL_CCMU_SetMclkDiv(h_debe1mclk, pll5_div);

		OSAL_CCMU_MclkOnOff(h_debe1ahbclk, CLK_ON);
		if (sunxi_is_sun4i()) {
			OSAL_CCMU_MclkOnOff(h_debe1dramclk, CLK_ON);
			OSAL_CCMU_MclkOnOff(h_debe1dramclk, CLK_OFF);
		}
		OSAL_CCMU_MclkOnOff(h_debe1mclk, CLK_ON);

		g_clk_status |= (CLK_DEBE1_AHB_ON | CLK_DEBE1_MOD_ON);
	}
	return DIS_SUCCESS;
}

__s32 image_clk_exit(__u32 sel)
{
	if (sel == 0) {
#ifdef RESET_OSAL
		OSAL_CCMU_MclkReset(h_debe0mclk, RST_VALID);
#endif
		OSAL_CCMU_MclkOnOff(h_debe0ahbclk, CLK_OFF);
		OSAL_CCMU_MclkOnOff(h_debe0dramclk, CLK_OFF);
		OSAL_CCMU_MclkOnOff(h_debe0mclk, CLK_OFF);
		OSAL_CCMU_CloseMclk(h_debe0ahbclk);
		OSAL_CCMU_CloseMclk(h_debe0dramclk);
		OSAL_CCMU_CloseMclk(h_debe0mclk);

		g_clk_status &= ~(CLK_DEBE0_AHB_ON | CLK_DEBE0_MOD_ON |
				  CLK_DEBE0_DRAM_ON);
	} else if (sel == 1) {
#ifdef RESET_OSAL
		OSAL_CCMU_MclkReset(h_debe1mclk, RST_VALID);
#endif
		OSAL_CCMU_MclkOnOff(h_debe1ahbclk, CLK_OFF);
		OSAL_CCMU_MclkOnOff(h_debe1dramclk, CLK_OFF);
		OSAL_CCMU_MclkOnOff(h_debe1mclk, CLK_OFF);
		OSAL_CCMU_CloseMclk(h_debe1ahbclk);
		OSAL_CCMU_CloseMclk(h_debe1dramclk);
		OSAL_CCMU_CloseMclk(h_debe1mclk);

		g_clk_status &= ~(CLK_DEBE1_AHB_ON | CLK_DEBE1_MOD_ON |
				  CLK_DEBE1_DRAM_ON);
	}

	return DIS_SUCCESS;
}

__s32 image_clk_on(__u32 sel)
{
	if (sel == 0) {
		/*
		 * need to comfirm:
		 * REGisters can be accessed if  be_mclk was close.
		 */
		OSAL_CCMU_MclkOnOff(h_debe0dramclk, CLK_ON);
		g_clk_status |= CLK_DEBE0_DRAM_ON;
	} else if (sel == 1) {
		OSAL_CCMU_MclkOnOff(h_debe1dramclk, CLK_ON);
		g_clk_status |= CLK_DEBE1_DRAM_ON;
	}
	return DIS_SUCCESS;
}

__s32 image_clk_off(__u32 sel)
{
	if (sel == 0) {
		OSAL_CCMU_MclkOnOff(h_debe0dramclk, CLK_OFF);
		g_clk_status &= ~CLK_DEBE0_DRAM_ON;
	} else if (sel == 1) {
		OSAL_CCMU_MclkOnOff(h_debe1dramclk, CLK_OFF);
		g_clk_status &= ~CLK_DEBE1_DRAM_ON;
	}
	return DIS_SUCCESS;
}

__s32 scaler_clk_init(__u32 sel)
{
	if (sel == 0) {
		h_defe0ahbclk = OSAL_CCMU_OpenMclk(AW_MOD_CLK_AHB_DEFE0);
		h_defe0dramclk = OSAL_CCMU_OpenMclk(AW_MOD_CLK_SDRAM_DEFE0);
		h_defe0mclk = OSAL_CCMU_OpenMclk(AW_MOD_CLK_DEFE0);
#ifdef RESET_OSAL

		OSAL_CCMU_MclkReset(h_defe0mclk, RST_INVALID);
#endif

		/* FIX CONNECT TO VIDEO PLL1 */
		OSAL_CCMU_SetMclkSrc(h_defe0mclk, AW_SYS_CLK_PLL7);
		OSAL_CCMU_SetMclkDiv(h_defe0mclk, 1);

		OSAL_CCMU_MclkOnOff(h_defe0ahbclk, CLK_ON);
		OSAL_CCMU_MclkOnOff(h_defe0mclk, CLK_ON);
		OSAL_CCMU_MclkOnOff(h_defe0mclk, CLK_OFF);
		OSAL_CCMU_MclkOnOff(h_defe0dramclk, CLK_ON);
		OSAL_CCMU_MclkOnOff(h_defe0dramclk, CLK_OFF);

		g_clk_status |= CLK_DEFE0_AHB_ON;
	} else if (sel == 1) {
		h_defe1ahbclk = OSAL_CCMU_OpenMclk(AW_MOD_CLK_AHB_DEFE1);
		h_defe1dramclk = OSAL_CCMU_OpenMclk(AW_MOD_CLK_SDRAM_DEFE1);
		h_defe1mclk = OSAL_CCMU_OpenMclk(AW_MOD_CLK_DEFE1);
#ifdef RESET_OSAL
		OSAL_CCMU_MclkReset(h_defe1mclk, RST_INVALID);
#endif
		/* FIX CONNECT TO VIDEO PLL1 */
		OSAL_CCMU_SetMclkSrc(h_defe1mclk, AW_SYS_CLK_PLL7);
		OSAL_CCMU_SetMclkDiv(h_defe1mclk, 1);

		OSAL_CCMU_MclkOnOff(h_defe1ahbclk, CLK_ON);
		OSAL_CCMU_MclkOnOff(h_defe1mclk, CLK_ON);
		OSAL_CCMU_MclkOnOff(h_defe1mclk, CLK_OFF);
		OSAL_CCMU_MclkOnOff(h_defe1dramclk, CLK_ON);
		OSAL_CCMU_MclkOnOff(h_defe1dramclk, CLK_OFF);

		g_clk_status |= CLK_DEFE1_AHB_ON;
	}
	return DIS_SUCCESS;
}

__s32 scaler_clk_exit(__u32 sel)
{
	if (sel == 0) {
#ifdef RESET_OSAL
		OSAL_CCMU_MclkReset(h_defe0mclk, RST_VALID);
#endif
		OSAL_CCMU_MclkOnOff(h_defe0ahbclk, CLK_OFF);
		OSAL_CCMU_MclkOnOff(h_defe0dramclk, CLK_OFF);
		OSAL_CCMU_MclkOnOff(h_defe0mclk, CLK_OFF);
		OSAL_CCMU_CloseMclk(h_defe0ahbclk);
		OSAL_CCMU_CloseMclk(h_defe0dramclk);
		OSAL_CCMU_CloseMclk(h_defe0mclk);

		g_clk_status &= ~(CLK_DEFE0_AHB_ON | CLK_DEFE0_MOD_ON |
				  CLK_DEFE0_DRAM_ON);

	} else if (sel == 1) {
#ifdef RESET_OSAL
		OSAL_CCMU_MclkReset(h_defe1mclk, RST_VALID);
#endif
		OSAL_CCMU_MclkOnOff(h_defe1ahbclk, CLK_OFF);
		OSAL_CCMU_MclkOnOff(h_defe1dramclk, CLK_OFF);
		OSAL_CCMU_MclkOnOff(h_defe1mclk, CLK_OFF);
		OSAL_CCMU_CloseMclk(h_defe1ahbclk);
		OSAL_CCMU_CloseMclk(h_defe1dramclk);
		OSAL_CCMU_CloseMclk(h_defe1mclk);

		g_clk_status &= ~(CLK_DEFE1_AHB_ON | CLK_DEFE1_MOD_ON |
				  CLK_DEFE1_DRAM_ON);
	}

	return DIS_SUCCESS;
}

__s32 scaler_clk_on(__u32 sel)
{
	if (sel == 0) {
		OSAL_CCMU_MclkOnOff(h_defe0mclk, CLK_ON);
		OSAL_CCMU_MclkOnOff(h_defe0dramclk, CLK_ON);

		g_clk_status |= (CLK_DEFE0_MOD_ON | CLK_DEFE0_DRAM_ON);
	} else if (sel == 1) {
		OSAL_CCMU_MclkOnOff(h_defe1mclk, CLK_ON);
		OSAL_CCMU_MclkOnOff(h_defe1dramclk, CLK_ON);

		g_clk_status |= (CLK_DEFE1_MOD_ON | CLK_DEFE1_DRAM_ON);
	}
	return DIS_SUCCESS;
}

__s32 scaler_clk_off(__u32 sel)
{
	if (sel == 0) {
		OSAL_CCMU_MclkOnOff(h_defe0mclk, CLK_OFF);
		OSAL_CCMU_MclkOnOff(h_defe0dramclk, CLK_OFF);

		g_clk_status &= ~(CLK_DEFE0_MOD_ON | CLK_DEFE0_DRAM_ON);
	} else if (sel == 1) {
		OSAL_CCMU_MclkOnOff(h_defe1mclk, CLK_OFF);
		OSAL_CCMU_MclkOnOff(h_defe1dramclk, CLK_OFF);

		g_clk_status &= ~(CLK_DEFE1_MOD_ON | CLK_DEFE1_DRAM_ON);
	}
	return DIS_SUCCESS;
}

__s32 lcdc_clk_init(__u32 sel)
{
	if (sel == 0) {
		h_lcd0ahbclk = OSAL_CCMU_OpenMclk(AW_MOD_CLK_AHB_LCD0);
		h_lcd0ch0mclk0 = OSAL_CCMU_OpenMclk(AW_MOD_CLK_LCD0CH0);
		h_lcd0ch1mclk1 = OSAL_CCMU_OpenMclk(AW_MOD_CLK_LCD0CH1_S1);
		h_lcd0ch1mclk2 = OSAL_CCMU_OpenMclk(AW_MOD_CLK_LCD0CH1_S2);

		if (!sunxi_is_sun5i()) {
			/* Default to Video Pll0 */
			OSAL_CCMU_SetMclkSrc(h_lcd0ch0mclk0, AW_SYS_CLK_PLL7);
			/* Default to Video Pll0 */
			OSAL_CCMU_SetMclkSrc(h_lcd0ch1mclk1, AW_SYS_CLK_PLL7);
			/* Default to Video Pll0 */
			//OSAL_CCMU_SetMclkSrc(h_lcd0ch1mclk2, AW_SYS_CLK_PLL7);
		} else {
			/* Default to Video Pll0 */
			OSAL_CCMU_SetMclkSrc(h_lcd0ch0mclk0, AW_SYS_CLK_PLL3);
			/* Default to Video Pll0 */
			OSAL_CCMU_SetMclkSrc(h_lcd0ch1mclk1, AW_SYS_CLK_PLL3);
		}

		OSAL_CCMU_SetMclkDiv(h_lcd0ch1mclk2, 10);
		OSAL_CCMU_SetMclkDiv(h_lcd0ch1mclk1, 10);
#ifdef RESET_OSAL
		OSAL_CCMU_MclkReset(h_lcd0ch0mclk0, RST_INVALID);
#endif
		OSAL_CCMU_MclkOnOff(h_lcd0ahbclk, CLK_ON);
		OSAL_CCMU_MclkOnOff(h_lcd0ch0mclk0, CLK_ON);
		OSAL_CCMU_MclkOnOff(h_lcd0ch0mclk0, CLK_OFF);
		OSAL_CCMU_MclkOnOff(h_lcd0ch1mclk1, CLK_ON);
		OSAL_CCMU_MclkOnOff(h_lcd0ch1mclk1, CLK_OFF);
		OSAL_CCMU_MclkOnOff(h_lcd0ch1mclk2, CLK_ON);
		OSAL_CCMU_MclkOnOff(h_lcd0ch1mclk2, CLK_OFF);

		g_clk_status |= CLK_LCDC0_AHB_ON;
	} else if (sel == 1) {
		h_lcd1ahbclk = OSAL_CCMU_OpenMclk(AW_MOD_CLK_AHB_LCD1);
		h_lcd1ch0mclk0 = OSAL_CCMU_OpenMclk(AW_MOD_CLK_LCD1CH0);
		h_lcd1ch1mclk1 = OSAL_CCMU_OpenMclk(AW_MOD_CLK_LCD1CH1_S1);
		h_lcd1ch1mclk2 = OSAL_CCMU_OpenMclk(AW_MOD_CLK_LCD1CH1_S2);

		if (!sunxi_is_sun5i()) {
			/* Default to Video Pll0 */
			OSAL_CCMU_SetMclkSrc(h_lcd1ch0mclk0, AW_SYS_CLK_PLL7);
			/* Default to Video Pll0 */
			OSAL_CCMU_SetMclkSrc(h_lcd1ch1mclk1, AW_SYS_CLK_PLL7);
			/* Default to Video Pll0 */
			//OSAL_CCMU_SetMclkSrc(h_lcd1ch1mclk2, AW_SYS_CLK_PLL7);
		} else {
			/* Default to Video Pll0 */
			OSAL_CCMU_SetMclkSrc(h_lcd1ch0mclk0, AW_SYS_CLK_PLL3);
			/* Default to Video Pll0 */
			OSAL_CCMU_SetMclkSrc(h_lcd1ch1mclk1, AW_SYS_CLK_PLL3);
		}

		OSAL_CCMU_SetMclkDiv(h_lcd1ch1mclk2, 10);
		OSAL_CCMU_SetMclkDiv(h_lcd1ch1mclk1, 10);
#ifdef RESET_OSAL
		OSAL_CCMU_MclkReset(h_lcd1ch0mclk0, RST_INVALID);
#endif
		OSAL_CCMU_MclkOnOff(h_lcd1ahbclk, CLK_ON);
		OSAL_CCMU_MclkOnOff(h_lcd1ch0mclk0, CLK_ON);
		OSAL_CCMU_MclkOnOff(h_lcd1ch0mclk0, CLK_OFF);
		OSAL_CCMU_MclkOnOff(h_lcd1ch1mclk1, CLK_ON);
		OSAL_CCMU_MclkOnOff(h_lcd1ch1mclk1, CLK_OFF);
		OSAL_CCMU_MclkOnOff(h_lcd1ch1mclk2, CLK_ON);
		OSAL_CCMU_MclkOnOff(h_lcd1ch1mclk2, CLK_OFF);

		g_clk_status |= CLK_LCDC1_AHB_ON;
	}
	return DIS_SUCCESS;
}

__s32 lcdc_clk_exit(__u32 sel)
{
	if (sel == 0) {
#ifdef RESET_OSAL
		OSAL_CCMU_MclkReset(h_lcd0ch0mclk0, RST_VALID);
#endif
		OSAL_CCMU_MclkOnOff(h_lcd0ahbclk, CLK_OFF);
		OSAL_CCMU_MclkOnOff(h_lcd0ch0mclk0, CLK_OFF);
		OSAL_CCMU_MclkOnOff(h_lcd0ch1mclk1, CLK_OFF);
		OSAL_CCMU_MclkOnOff(h_lcd0ch1mclk2, CLK_OFF);
		OSAL_CCMU_CloseMclk(h_lcd0ahbclk);
		OSAL_CCMU_CloseMclk(h_lcd0ch0mclk0);
		OSAL_CCMU_CloseMclk(h_lcd0ch1mclk1);
		OSAL_CCMU_CloseMclk(h_lcd0ch1mclk2);

		g_clk_status &= ~(CLK_LCDC0_AHB_ON | CLK_LCDC0_MOD0_ON |
				  CLK_LCDC0_MOD1_ON);
	} else if (sel == 1) {
#ifdef RESET_OSAL
		OSAL_CCMU_MclkReset(h_lcd1ch0mclk0, RST_VALID);
#endif
		OSAL_CCMU_MclkOnOff(h_lcd1ahbclk, CLK_OFF);
		OSAL_CCMU_MclkOnOff(h_lcd1ch0mclk0, CLK_OFF);
		OSAL_CCMU_MclkOnOff(h_lcd1ch1mclk1, CLK_OFF);
		OSAL_CCMU_MclkOnOff(h_lcd1ch1mclk2, CLK_OFF);
		OSAL_CCMU_CloseMclk(h_lcd1ahbclk);
		OSAL_CCMU_CloseMclk(h_lcd1ch0mclk0);
		OSAL_CCMU_CloseMclk(h_lcd1ch1mclk1);
		OSAL_CCMU_CloseMclk(h_lcd1ch1mclk2);

		g_clk_status &= ~(CLK_LCDC1_AHB_ON | CLK_LCDC1_MOD0_ON |
				  CLK_LCDC1_MOD1_ON);
	}
	return DIS_SUCCESS;
}

__s32 lcdc_clk_on(__u32 sel)
{
	if (sel == 0) {
		OSAL_CCMU_MclkOnOff(h_lcd0ch0mclk0, CLK_ON);
		OSAL_CCMU_MclkOnOff(h_lcd0ch1mclk1, CLK_ON);
		OSAL_CCMU_MclkOnOff(h_lcd0ch1mclk2, CLK_ON);

		g_clk_status |= CLK_LCDC0_MOD0_ON | CLK_LCDC0_MOD1_ON;
	} else if (sel == 1) {
		OSAL_CCMU_MclkOnOff(h_lcd1ch0mclk0, CLK_ON);
		OSAL_CCMU_MclkOnOff(h_lcd1ch1mclk1, CLK_ON);
		OSAL_CCMU_MclkOnOff(h_lcd1ch1mclk2, CLK_ON);

		g_clk_status |= CLK_LCDC1_MOD0_ON | CLK_LCDC1_MOD1_ON;
	}
	return DIS_SUCCESS;
}

__s32 lcdc_clk_off(__u32 sel)
{
	if (sel == 0) {
		OSAL_CCMU_MclkOnOff(h_lcd0ch0mclk0, CLK_OFF);
		OSAL_CCMU_MclkOnOff(h_lcd0ch1mclk1, CLK_OFF);
		OSAL_CCMU_MclkOnOff(h_lcd0ch1mclk2, CLK_OFF);

		g_clk_status &= ~(CLK_LCDC0_MOD0_ON | CLK_LCDC0_MOD1_ON);
	} else if (sel == 1) {
		OSAL_CCMU_MclkOnOff(h_lcd1ch0mclk0, CLK_OFF);
		OSAL_CCMU_MclkOnOff(h_lcd1ch1mclk1, CLK_OFF);
		OSAL_CCMU_MclkOnOff(h_lcd1ch1mclk2, CLK_OFF);

		g_clk_status &= ~(CLK_LCDC1_MOD0_ON | CLK_LCDC1_MOD1_ON);
	}
	return DIS_SUCCESS;
}

__s32 tve_clk_init(__u32 sel)
{
	if (sel == 0) {
		if (sunxi_is_sun5i())
			OSAL_CCMU_MclkReset(h_lcd0ch1mclk2, RST_INVALID);
		h_tvenc0ahbclk = OSAL_CCMU_OpenMclk(AW_MOD_CLK_AHB_TVE0);
		OSAL_CCMU_MclkOnOff(h_tvenc0ahbclk, CLK_ON);

		g_clk_status |= CLK_TVENC0_AHB_ON;
	} else if (sel == 1) {
		h_tvenc1ahbclk = OSAL_CCMU_OpenMclk(AW_MOD_CLK_AHB_TVE1);
		OSAL_CCMU_MclkOnOff(h_tvenc1ahbclk, CLK_ON);

		g_clk_status |= CLK_TVENC1_AHB_ON;
	}
	return DIS_SUCCESS;
}

__s32 tve_clk_exit(__u32 sel)
{
	if (sel == 0) {
		OSAL_CCMU_MclkOnOff(h_tvenc0ahbclk, CLK_OFF);
		OSAL_CCMU_CloseMclk(h_tvenc0ahbclk);
		if (sunxi_is_sun5i())
			OSAL_CCMU_MclkReset(h_lcd0ch1mclk2, RST_VALID);

		g_clk_status &= ~CLK_TVENC0_AHB_ON;
	} else if (sel == 1) {
		OSAL_CCMU_MclkOnOff(h_tvenc1ahbclk, CLK_OFF);
		OSAL_CCMU_CloseMclk(h_tvenc1ahbclk);

		g_clk_status &= ~CLK_TVENC1_AHB_ON;
	}
	return DIS_SUCCESS;
}

__s32 tve_clk_on(__u32 sel)
{
	return DIS_SUCCESS;
}

__s32 tve_clk_off(__u32 sel)
{
	return DIS_SUCCESS;
}

__s32 hdmi_clk_init(void)
{
	h_hdmiahbclk = OSAL_CCMU_OpenMclk(AW_MOD_CLK_AHB_HDMI);
	h_hdmimclk = OSAL_CCMU_OpenMclk(AW_MOD_CLK_HDMI);
#ifdef RESET_OSAL
	OSAL_CCMU_MclkReset(h_hdmimclk, RST_INVALID);
#endif
	if (!sunxi_is_sun5i())
		OSAL_CCMU_SetMclkSrc(h_hdmimclk, AW_SYS_CLK_PLL7);
	else
		OSAL_CCMU_SetMclkSrc(h_hdmimclk, AW_SYS_CLK_PLL3);
	OSAL_CCMU_SetMclkDiv(h_hdmimclk, 1);

	OSAL_CCMU_MclkOnOff(h_hdmiahbclk, CLK_ON);
	g_clk_status |= CLK_HDMI_AHB_ON;

	/* We need to turn on the hdmi clk early for sun5i and sun7i
	   (and make hdmi_clk_on a nop) */
	if (!sunxi_is_sun4i()) {
		OSAL_CCMU_MclkOnOff(h_hdmimclk, CLK_ON);
		g_clk_status |= CLK_HDMI_MOD_ON;
	}

	return DIS_SUCCESS;
}

__s32 hdmi_clk_exit(void)
{
#ifdef RESET_OSAL
	OSAL_CCMU_MclkReset(h_hdmimclk, RST_VALID);
#endif
	OSAL_CCMU_MclkOnOff(h_hdmimclk, CLK_OFF);
	OSAL_CCMU_MclkOnOff(h_hdmiahbclk, CLK_OFF);
	OSAL_CCMU_CloseMclk(h_hdmiahbclk);
	OSAL_CCMU_CloseMclk(h_hdmimclk);
	if (!sunxi_is_sun4i())
		OSAL_CCMU_MclkOnOff(h_hdmimclk, CLK_OFF);
	g_clk_status &= ~(CLK_HDMI_AHB_ON | CLK_HDMI_MOD_ON);

	return DIS_SUCCESS;
}

__s32 hdmi_clk_on(void)
{
	if (sunxi_is_sun4i()) {
		OSAL_CCMU_MclkOnOff(h_hdmimclk, CLK_ON);
		g_clk_status |= CLK_HDMI_MOD_ON;
	}

	return DIS_SUCCESS;
}

__s32 hdmi_clk_off(void)
{
	if (sunxi_is_sun4i()) {
		OSAL_CCMU_MclkOnOff(h_hdmimclk, CLK_OFF);
		g_clk_status &= ~CLK_HDMI_MOD_ON;
	}

	return DIS_SUCCESS;
}

__s32 lvds_clk_init(void)
{
	h_lvdsmclk = OSAL_CCMU_OpenMclk(AW_MOD_CLK_LVDS);
#ifdef RESET_OSAL
	OSAL_CCMU_MclkReset(h_lvdsmclk, RST_INVALID);
#endif
	return DIS_SUCCESS;
}

__s32 lvds_clk_exit(void)
{
#ifdef RESET_OSAL
	OSAL_CCMU_MclkReset(h_lvdsmclk, RST_VALID);
#endif
	OSAL_CCMU_CloseMclk(AW_MOD_CLK_LVDS);

	return DIS_SUCCESS;
}

__s32 lvds_clk_on(void)
{
	return DIS_SUCCESS;
}

__s32 lvds_clk_off(void)
{
	return DIS_SUCCESS;
}

__s32 disp_pll_init(void)
{
	OSAL_CCMU_SetSrcFreq(AW_SYS_CLK_PLL3, 297000000);
	OSAL_CCMU_SetSrcFreq(AW_SYS_CLK_PLL7, 297000000);

	return DIS_SUCCESS;
}

/*
 * Calculate PLL frequence and divider depend on all kinds of lcd panel
 *
 * 1. support hv/cpu/ttl panels which pixel frequence between 2MHz~297MHz
 * 2. support all lvds panels, when pll can't reach  (pixel clk x7),
 *    set pll to 381MHz(pllx1), which will depress the frame rate.
 */
static __s32 LCD_PLL_Calc(__u32 sel, __panel_para_t *info, __u32 *divider)
{
	__u32 lcd_dclk_freq; /* Hz */
	__s32 pll_freq = -1;

	lcd_dclk_freq = info->lcd_dclk_freq * 1000000;

	/* hv panel, CPU panel and ttl panel */
	if (info->lcd_if == 0 || info->lcd_if == 1 || info->lcd_if == 2) {
		/* MHz */
		if (lcd_dclk_freq > 2000000 && lcd_dclk_freq <= 297000000) {
			/* divider for dclk in tcon0 */
			*divider = 297000000 / (lcd_dclk_freq);
			pll_freq = lcd_dclk_freq * (*divider);
		} else {
			return -1;
		}

	} else if (info->lcd_if == 3) {	/* lvds panel */
		__u32 clk_max;

		if (sw_get_ic_ver() != SUNXI_VER_A10A)
			clk_max = 150000000;
		else
			/*
			 * pixel clock can't be larger than 108MHz,
			 * limited by Video pll frequency
			 */
			clk_max = 108000000;

		if (lcd_dclk_freq > clk_max)
			lcd_dclk_freq = clk_max;


		if (lcd_dclk_freq > 4000000) { /* pixel clk */
			pll_freq = lcd_dclk_freq * 7;
			*divider = 7;
		} else
			return -1;
	}
	return pll_freq;
}

/*
 * Select a video pll for the display device under configuration by specific
 * rules
 *
 * returns:
 *   0:video pll0;
 *   1:video pll1;
 *   2:sata pll
 *   -1: fail
 *
 * ASSIGNMENT RULES
 * RULE1: video pll1(1x) work between [250,300]MHz, when no lcdc using video
 *        pll1 and required freq is in [250,300]MHz, choose video pll1;
 * RULE2: when video pll1 used by another lcdc, but running frequency is equal
 *        to required frequency, choose video pll1;
 * RULE3. when video pll1 used by another lcdc, and running frequency is NOT
 * equal to required frequency, choose video pll0;
 *
 * CONDICTION CAN'T BE HANDLE
 * 1.two lvds panel are both require a pll freq outside [250,300], and pll
 * freq are different, the second panel will fail to assign.
 */
static __s32 disp_pll_assign(__u32 sel, __u32 pll_clk)
{
	__u32 another_lcdc, another_pll_use_status;
	__s32 ret = -1;

	if (sunxi_is_sun5i()) {
		if (pll_clk <= (381000000 * 2))
			ret = 0;
		else
			DE_WRN("Can't assign PLL for screen%d, pll_clk:%d\n",
				sel, pll_clk);
		return ret;
	}

	another_lcdc = (sel == 0) ? 1 : 0;
	another_pll_use_status = gdisp.screen[another_lcdc].pll_use_status;

	if (pll_clk >= 250000000 && pll_clk <= 300000000) {
		if ((!(another_pll_use_status & VIDEO_PLL1_USED)) ||
		    (OSAL_CCMU_GetSrcFreq(AW_SYS_CLK_PLL7) == pll_clk))
			ret = 1;
		else if ((!(another_pll_use_status & VIDEO_PLL0_USED)) ||
			 (OSAL_CCMU_GetSrcFreq(AW_SYS_CLK_PLL3) == pll_clk))
			ret = 0;
	} else if (pll_clk <= (381000000 * 2)) {
		if ((!(another_pll_use_status & VIDEO_PLL0_USED)) ||
		    (OSAL_CCMU_GetSrcFreq(AW_SYS_CLK_PLL3) == pll_clk))
			ret = 0;
		else if ((!(another_pll_use_status & VIDEO_PLL1_USED)) ||
			 (OSAL_CCMU_GetSrcFreq(AW_SYS_CLK_PLL7) == pll_clk))
			ret = 1;
	} else if (pll_clk <= 1200000000) {
		if (sw_get_ic_ver() != SUNXI_VER_A10A)
			ret = 2; /* sata pll */
	}

	if (ret == -1)
		DE_WRN("Can't assign PLL for screen%d, pll_clk:%d\n", sel,
		       pll_clk);

	DE_INF("====disp_pll_assign====: sel:%d,pll_clk:%d,pll_sel:%d\n", sel,
	       pll_clk, ret);

	return ret;
}

/*
 * Set clock control module
 *
 * Arguments:
 *  sel: display channel
 *  videopll_sel: sel pll
 *  pll_freq: sel pll freq
 *  tve_freq: lcdx_ch1_clk2 freq
 *  pre_scale: lcdx_ch1_clk2/lcdx_ch1_ch1
 *  lcd_clk_div: lcd panel clk div
 *  hdmi_freq: hdmi module clk freq
 *  pll_2x: pll 2x required
 *  type: display device type: tv/vga/hdmi/lcd
 */
static __s32 disp_pll_set(__u32 sel, __s32 videopll_sel, __u32 pll_freq,
			  __u32 tve_freq, __s32 pre_scale, __u32 lcd_clk_div,
			  __u32 hdmi_freq, __u32 pll_2x, __u32 type)
{
	__u32 videopll;
	__hdle h_lcdmclk0, h_lcdmclk1, h_lcdmclk2;
	__s32 pll_2x_req;
	__u32 lcdmclk1_div, lcdmclk2_div, hdmiclk_div;

	if (type == DISP_OUTPUT_TYPE_LCD) { /* lcd panel */
		if (videopll_sel == 2) { /* sata pll, fix to 960M */
			videopll = AW_SYS_CLK_PLL7X2;
#if 0
			pll_freq = ((pll_freq + 12000000) / 24000000) *
				24000000;
			OSAL_CCMU_SetSrcFreq(AW_SYS_CLK_PLL6, pll_freq);
#endif
		} else { /* video pll0 or video pll1 */
			pll_2x_req = (pll_freq > 381000000) ? 1 : 0;
			if (pll_2x_req)
				pll_freq /= 2;

			/* in 3M unit */
			pll_freq = (pll_freq + 1500000) / 3000000;
			pll_freq = pll_freq * 3000000;

			videopll = (videopll_sel == 0) ?
				AW_SYS_CLK_PLL3 : AW_SYS_CLK_PLL7;
			OSAL_CCMU_SetSrcFreq(videopll, pll_freq);
			if (pll_2x_req)
				videopll = (videopll == AW_SYS_CLK_PLL3) ?
					AW_SYS_CLK_PLL3X2 : AW_SYS_CLK_PLL7X2;
		}

		if (gpanel_info[sel].tcon_index == 0) {
			/* tcon0 drive lcd panel */

			h_lcdmclk0 = (sel == 0) ?
				h_lcd0ch0mclk0 : h_lcd1ch0mclk0;
			OSAL_CCMU_SetMclkSrc(h_lcdmclk0, videopll);
			TCON0_set_dclk_div(sel, lcd_clk_div);
		} else {
			/* tcon1 drive lcd panel */
			h_lcdmclk1 = (sel == 0) ?
				h_lcd0ch1mclk1 : h_lcd1ch1mclk1;
			h_lcdmclk2 = (sel == 0) ?
				h_lcd0ch1mclk2 : h_lcd1ch1mclk2;
			OSAL_CCMU_SetMclkSrc(h_lcdmclk2, videopll);
			OSAL_CCMU_SetMclkSrc(h_lcdmclk1, videopll);
			OSAL_CCMU_SetMclkDiv(h_lcdmclk2, lcd_clk_div);
			OSAL_CCMU_SetMclkDiv(h_lcdmclk1, lcd_clk_div);
		}
	} else { /* tv/vga/hdmi */
		__u32 pll_freq_used;

		pll_2x_req = pll_2x;
		videopll = (videopll_sel == 0) ?
			AW_SYS_CLK_PLL3 : AW_SYS_CLK_PLL7;

		/* Set related Video Pll Frequency */
		OSAL_CCMU_SetSrcFreq(videopll, pll_freq);

		videopll = (videopll_sel == 0) ?
		    ((pll_2x_req) ? AW_SYS_CLK_PLL3X2 : AW_SYS_CLK_PLL3) :
		    ((pll_2x_req) ? AW_SYS_CLK_PLL7X2 : AW_SYS_CLK_PLL7);

		pll_freq_used = pll_freq * (pll_2x_req + 1);

		lcdmclk2_div = (pll_freq_used + (tve_freq / 2)) / tve_freq;
		lcdmclk1_div = lcdmclk2_div * pre_scale;
		hdmiclk_div = (pll_freq_used + (hdmi_freq / 2)) / hdmi_freq;

		h_lcdmclk1 = (sel == 0) ? h_lcd0ch1mclk1 : h_lcd1ch1mclk1;
		h_lcdmclk2 = (sel == 0) ? h_lcd0ch1mclk2 : h_lcd1ch1mclk2;
		OSAL_CCMU_SetMclkSrc(h_lcdmclk2, videopll);
		OSAL_CCMU_SetMclkSrc(h_lcdmclk1, videopll);
		OSAL_CCMU_SetMclkDiv(h_lcdmclk2, lcdmclk2_div);
		OSAL_CCMU_SetMclkDiv(h_lcdmclk1, lcdmclk1_div);

		/* hdmi internal mode */
		if (type == DISP_OUTPUT_TYPE_HDMI &&
		    gdisp.screen[sel].hdmi_index == 0) {
			OSAL_CCMU_SetMclkSrc(h_hdmimclk, videopll);
			OSAL_CCMU_SetMclkDiv(h_hdmimclk, hdmiclk_div);

			if (gdisp.init_para.hdmi_set_pll != NULL) {
				gdisp.init_para.hdmi_set_pll(videopll,
							     pll_freq_used);
			} else
				DE_WRN("gdisp.init_para.hdmi_set_pll is "
				       "NULL\n");
		}
	}

	return DIS_SUCCESS;
}

static __s32 disp_get_pll_freq_between(__u32 pclk, __u32 min, __u32 max,
				       __u32 *pll_freq, __u32 *pll_2x)
{
	__u32 mult, mult_min, mult_max, freq;

	mult_min = min / pclk;
	mult_max = (max * 2) / pclk;
	if (mult_max > 15)
		mult_max = 15;
	for (mult = mult_min; mult <= mult_max; mult++) {
		freq = pclk * mult;
		if (freq >= min && freq <= max && (freq % 3000000) == 0) {
			*pll_freq = freq;
			*pll_2x = 0;
			return 0;
		}
		if (freq >= (min * 2) && freq <= (max * 2) &&
		    (freq % 6000000) == 0) {
			*pll_freq = freq / 2;
			*pll_2x = 1;
			return 0;
		}
	}
	return -1;
}

__s32 disp_get_pll_freq(__u32 pclk, __u32 *pll_freq,  __u32 *pll_2x)
{
	if (pclk == 0)
		return -1;

	/* First try the 2 special video pll clks */
	if ((270000000 % pclk) == 0) {
		*pll_freq = 270000000;
		*pll_2x = 0;
		return 0;
	}
	if ((297000000 % pclk) == 0) {
		*pll_freq = 297000000;
		*pll_2x = 0;
		return 0;
	}

	/* Not working, try to find a frequency between 250 and 300 Mhz */
	if (disp_get_pll_freq_between(pclk, 250000000, 300000000,
				      pll_freq, pll_2x) == 0)
		return 0;

	/* Not working, try to find a frequency between 27 and 381 Mhz */
	if (disp_get_pll_freq_between(pclk, 27000000, 381000000,
				      pll_freq, pll_2x) == 0)
		return 0;

	pr_warn("disp_clk: Could not find a matching pll-freq for %d pclk\n",
		pclk);
	return -1;
}
EXPORT_SYMBOL(disp_get_pll_freq);

/*
 * Config PLL and mclk depend on all kinds of display devices
 */
__s32 disp_clk_cfg(__u32 sel, __u32 type, __u8 mode)
{
	__u32 pll_freq = 297000000, tve_freq = 27000000;
	__u32 hdmi_freq = 74250000;
	__s32 videopll_sel, pre_scale = 1;
	__u32 lcd_clk_div = 0;
	__u32 pll_2x = 0;

	if (type == DISP_OUTPUT_TYPE_TV || type == DISP_OUTPUT_TYPE_HDMI) {
		if (type == DISP_OUTPUT_TYPE_HDMI) {
			struct __disp_video_timing video_timing;
			if (gdisp.init_para.hdmi_get_video_timing(mode,
							&video_timing) != 0)
				return -1;
			hdmi_freq = video_timing.PCLK;
		} else {
			hdmi_freq = clk_tab.tv_clk_tab[mode].hdmi_clk;
		}

		/* Special handling for standard tv modes */
		if (hdmi_freq == 27000000) {
			if (Disp_get_screen_scan_mode(mode))
				tve_freq  = 27000000;
			else
				tve_freq  = 54000000;
			pre_scale = 2;
		} else {
			tve_freq = hdmi_freq;
			pre_scale = 1;
		}

		if (disp_get_pll_freq(hdmi_freq, &pll_freq, &pll_2x) != 0)
			return -1;

		pr_info("disp clks: lcd %d pre_scale %d hdmi %d pll %d 2x %d\n",
			tve_freq, pre_scale, hdmi_freq, pll_freq, pll_2x);
	} else if (type == DISP_OUTPUT_TYPE_VGA) {
		pll_freq = clk_tab.vga_clk_tab[mode].pll_clk;
		tve_freq = clk_tab.vga_clk_tab[mode].tve_clk;
		pre_scale = clk_tab.vga_clk_tab[mode].pre_scale;
		pll_2x = clk_tab.vga_clk_tab[mode].pll_2x;
	} else if (type == DISP_OUTPUT_TYPE_LCD) {
		pll_freq =
		    LCD_PLL_Calc(sel, (__panel_para_t *) &gpanel_info[sel],
				 &lcd_clk_div);
		pre_scale = 1;
	} else {
		return DIS_SUCCESS;
	}

	videopll_sel = disp_pll_assign(sel, pll_freq);
	if (videopll_sel == -1)
		return DIS_FAIL;

	disp_pll_set(sel, videopll_sel, pll_freq, tve_freq, pre_scale,
		     lcd_clk_div, hdmi_freq, pll_2x, type);
	if (videopll_sel == 0)
		gdisp.screen[sel].pll_use_status |= VIDEO_PLL0_USED;
	else if (videopll_sel == 1)
		gdisp.screen[sel].pll_use_status |= VIDEO_PLL1_USED;

	return DIS_SUCCESS;
}

/*
 * type==1: open ahb clk and image mclk
 * type==2: open all clk except ahb clk and image mclk
 * type==3: open all clk
 */
__s32 BSP_disp_clk_on(__u32 type)
{
	if (type & 1) {
		/* AHB CLK */
		if ((g_clk_status & CLK_DEFE0_AHB_ON) == CLK_DEFE0_AHB_ON)
			OSAL_CCMU_MclkOnOff(h_defe0ahbclk, CLK_ON);

		if ((g_clk_status & CLK_DEFE1_AHB_ON) == CLK_DEFE1_AHB_ON)
			OSAL_CCMU_MclkOnOff(h_defe1ahbclk, CLK_ON);

		if ((g_clk_status & CLK_DEBE0_AHB_ON) == CLK_DEBE0_AHB_ON)
			OSAL_CCMU_MclkOnOff(h_debe0ahbclk, CLK_ON);

		if ((g_clk_status & CLK_DEBE1_AHB_ON) == CLK_DEBE1_AHB_ON)
			OSAL_CCMU_MclkOnOff(h_debe1ahbclk, CLK_ON);

		// OK?? REG wont clear?
		if ((g_clk_status & CLK_LCDC0_AHB_ON) == CLK_LCDC0_AHB_ON)
			OSAL_CCMU_MclkOnOff(h_lcd0ahbclk, CLK_ON);

		// OK?? REG wont clear?
		if ((g_clk_status & CLK_LCDC1_AHB_ON) == CLK_LCDC1_AHB_ON)
			OSAL_CCMU_MclkOnOff(h_lcd1ahbclk, CLK_ON);

		if ((g_clk_status & CLK_HDMI_AHB_ON) == CLK_HDMI_AHB_ON)
			OSAL_CCMU_MclkOnOff(h_hdmiahbclk, CLK_ON);

		//OSAL_CCMU_MclkOnOff(h_tveahbclk, CLK_ON);

		/* MODULE CLK */
		if ((g_clk_status & CLK_DEBE0_MOD_ON) == CLK_DEBE0_MOD_ON)
			OSAL_CCMU_MclkOnOff(h_debe0mclk, CLK_ON);

		if ((g_clk_status & CLK_DEBE1_MOD_ON) == CLK_DEBE1_MOD_ON)
			OSAL_CCMU_MclkOnOff(h_debe1mclk, CLK_ON);
	}

	if (type & 2) {
		/* DRAM CLK */
		if ((g_clk_status & CLK_DEFE0_DRAM_ON) == CLK_DEFE0_DRAM_ON)
			OSAL_CCMU_MclkOnOff(h_defe0dramclk, CLK_ON);

		if ((g_clk_status & CLK_DEFE1_DRAM_ON) == CLK_DEFE1_DRAM_ON)
			OSAL_CCMU_MclkOnOff(h_defe1dramclk, CLK_ON);

		if ((g_clk_status & CLK_DEBE0_DRAM_ON) == CLK_DEBE0_DRAM_ON)
			OSAL_CCMU_MclkOnOff(h_debe0dramclk, CLK_ON);

		if ((g_clk_status & CLK_DEBE1_DRAM_ON) == CLK_DEBE1_DRAM_ON)
			OSAL_CCMU_MclkOnOff(h_debe1dramclk, CLK_ON);

		/* MODULE CLK */
		if ((g_clk_status & CLK_DEFE0_MOD_ON) == CLK_DEFE0_MOD_ON)
			OSAL_CCMU_MclkOnOff(h_defe0mclk, CLK_ON);

		if ((g_clk_status & CLK_DEFE1_MOD_ON) == CLK_DEFE1_MOD_ON)
			OSAL_CCMU_MclkOnOff(h_defe1mclk, CLK_ON);

		if ((g_clk_status & CLK_LCDC0_MOD0_ON) == CLK_LCDC0_MOD0_ON)
			OSAL_CCMU_MclkOnOff(h_lcd0ch0mclk0, CLK_ON);

		if ((g_clk_status & CLK_LCDC0_MOD1_ON) == CLK_LCDC0_MOD1_ON) {
			OSAL_CCMU_MclkOnOff(h_lcd0ch1mclk1, CLK_ON);
			OSAL_CCMU_MclkOnOff(h_lcd0ch1mclk2, CLK_ON);
		}
		if ((g_clk_status & CLK_LCDC1_MOD0_ON) == CLK_LCDC1_MOD0_ON)
			OSAL_CCMU_MclkOnOff(h_lcd1ch0mclk0, CLK_ON);

		if ((g_clk_status & CLK_LCDC1_MOD1_ON) == CLK_LCDC1_MOD1_ON) {
			OSAL_CCMU_MclkOnOff(h_lcd1ch1mclk1, CLK_ON);
			OSAL_CCMU_MclkOnOff(h_lcd1ch1mclk2, CLK_ON);
		}
		if (sunxi_is_sun4i() &&
		    (g_clk_status & CLK_HDMI_MOD_ON) == CLK_HDMI_MOD_ON)
			OSAL_CCMU_MclkOnOff(h_hdmimclk, CLK_ON);
	}

	if (type == 2) {
		if ((g_clk_status & CLK_DEBE0_MOD_ON) == CLK_DEBE0_MOD_ON)
			OSAL_CCMU_SetMclkDiv(h_debe0mclk, 2);

		if ((g_clk_status & CLK_DEBE1_MOD_ON) == CLK_DEBE1_MOD_ON)
			OSAL_CCMU_SetMclkDiv(h_debe1mclk, 2);
	}

	return DIS_SUCCESS;
}

/*
 * type==1: close ahb clk and image mclk
 * type==2: close all clk except ahb clk and image mclk
 * type==3: close all clk
 */
__s32 BSP_disp_clk_off(__u32 type)
{
	if (type & 1) {
		/* AHB CLK */
		if ((g_clk_status & CLK_DEFE0_AHB_ON) == CLK_DEFE0_AHB_ON)
			OSAL_CCMU_MclkOnOff(h_defe0ahbclk, CLK_OFF);

		if ((g_clk_status & CLK_DEFE1_AHB_ON) == CLK_DEFE1_AHB_ON)
			OSAL_CCMU_MclkOnOff(h_defe1ahbclk, CLK_OFF);

		if ((g_clk_status & CLK_DEBE0_AHB_ON) == CLK_DEBE0_AHB_ON)
			OSAL_CCMU_MclkOnOff(h_debe0ahbclk, CLK_OFF);

		if ((g_clk_status & CLK_DEBE1_AHB_ON) == CLK_DEBE1_AHB_ON)
			OSAL_CCMU_MclkOnOff(h_debe1ahbclk, CLK_OFF);

		//OK?? REG wont clear?
		if ((g_clk_status & CLK_LCDC0_AHB_ON) == CLK_LCDC0_AHB_ON)
			OSAL_CCMU_MclkOnOff(h_lcd0ahbclk, CLK_OFF);

		//OK?? REG wont clear?
		if ((g_clk_status & CLK_LCDC1_AHB_ON) == CLK_LCDC1_AHB_ON)
			OSAL_CCMU_MclkOnOff(h_lcd1ahbclk, CLK_OFF);

		if ((g_clk_status & CLK_HDMI_AHB_ON) == CLK_HDMI_AHB_ON)
			OSAL_CCMU_MclkOnOff(h_hdmiahbclk, CLK_OFF);

		//OSAL_CCMU_MclkOnOff(h_tveahbclk, CLK_OFF);

		/* MODULE CLK */
		if ((g_clk_status & CLK_DEBE0_MOD_ON) == CLK_DEBE0_MOD_ON)
			OSAL_CCMU_MclkOnOff(h_debe0mclk, CLK_OFF);

		if ((g_clk_status & CLK_DEBE1_MOD_ON) == CLK_DEBE1_MOD_ON)
			OSAL_CCMU_MclkOnOff(h_debe1mclk, CLK_OFF);
	}

	if (type & 2) {
		/* DRAM CLK */
		if ((g_clk_status & CLK_DEFE0_DRAM_ON) == CLK_DEFE0_DRAM_ON)
			OSAL_CCMU_MclkOnOff(h_defe0dramclk, CLK_OFF);

		if ((g_clk_status & CLK_DEFE1_DRAM_ON) == CLK_DEFE1_DRAM_ON)
			OSAL_CCMU_MclkOnOff(h_defe1dramclk, CLK_OFF);

		if ((g_clk_status & CLK_DEBE0_DRAM_ON) == CLK_DEBE0_DRAM_ON)
			OSAL_CCMU_MclkOnOff(h_debe0dramclk, CLK_OFF);

		if ((g_clk_status & CLK_DEBE1_DRAM_ON) == CLK_DEBE1_DRAM_ON)
			OSAL_CCMU_MclkOnOff(h_debe1dramclk, CLK_OFF);

		/* MODULE CLK */
		if ((g_clk_status & CLK_DEFE0_MOD_ON) == CLK_DEFE0_MOD_ON)
			OSAL_CCMU_MclkOnOff(h_defe0mclk, CLK_OFF);

		if ((g_clk_status & CLK_DEFE1_MOD_ON) == CLK_DEFE1_MOD_ON)
			OSAL_CCMU_MclkOnOff(h_defe1mclk, CLK_OFF);

		if ((g_clk_status & CLK_LCDC0_MOD0_ON) == CLK_LCDC0_MOD0_ON)
			OSAL_CCMU_MclkOnOff(h_lcd0ch0mclk0, CLK_OFF);

		if ((g_clk_status & CLK_LCDC0_MOD1_ON) == CLK_LCDC0_MOD1_ON) {
			OSAL_CCMU_MclkOnOff(h_lcd0ch1mclk1, CLK_OFF);
			OSAL_CCMU_MclkOnOff(h_lcd0ch1mclk2, CLK_OFF);
		}
		if ((g_clk_status & CLK_LCDC1_MOD0_ON) == CLK_LCDC1_MOD0_ON)
			OSAL_CCMU_MclkOnOff(h_lcd1ch0mclk0, CLK_OFF);

		if ((g_clk_status & CLK_LCDC1_MOD1_ON) == CLK_LCDC1_MOD1_ON) {
			OSAL_CCMU_MclkOnOff(h_lcd1ch1mclk1, CLK_OFF);
			OSAL_CCMU_MclkOnOff(h_lcd1ch1mclk2, CLK_OFF);
		}
		if (sunxi_is_sun4i() &&
		    (g_clk_status & CLK_HDMI_MOD_ON) == CLK_HDMI_MOD_ON)
			OSAL_CCMU_MclkOnOff(h_hdmimclk, CLK_OFF);
	}

	if (type == 2) {
		if ((g_clk_status & CLK_DEBE0_MOD_ON) == CLK_DEBE0_MOD_ON)
			OSAL_CCMU_SetMclkDiv(h_debe0mclk, 16);

		if ((g_clk_status & CLK_DEBE1_MOD_ON) == CLK_DEBE1_MOD_ON)
			OSAL_CCMU_SetMclkDiv(h_debe1mclk, 16);
	}

	return DIS_SUCCESS;
}
