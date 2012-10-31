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

#include "bsp_display.h"
#include "OSAL_Clock.h"

static char *_sysClkName[AW_SYS_CLK_CNT] = {
	"none",		/*  0 */

	"losc",		/*  1 */
	"hosc",		/*  2 */

	"core_pll",	/*  3 */
	"audio_pll",	/*  4 */
	"audio_pllx8",	/*  5 */
	"video_pll0",	/*  6 */
	"video_pll0x2",	/*  7 */
	"ve_pll",	/*  8 */
	"sdram_pll",	/*  9 */
	"sdram_pll_m",	/* 10 */
	"sdram_pll_p",	/* 11 */
	"sata_pll",	/* 12 */
	"video_pll1",	/* 13 */
	"video_pll1x2",	/* 14 */
	"200m_pll",	/* 15 */

	"cpu",		/* 16 */
	"axi",		/* 17 */
	"ahb",		/* 18 */
	"apb",		/* 19 */
	"apb1",		/* 20 */

	"sata_pll_m",
	"sata_pll_2",
};

static char *_modClkName[AW_MOD_CLK_CNT] = {
	"none",

	"nfc",
	"msc",
	"sdc0",
	"sdc1",
	"sdc2",
	"sdc3",
	"ts",
	"ss",
	"spi0",
	"spi1",
	"spi2",
	"pata",
	"ir0",
	"ir1",
	"i2s",
	"ac97",
	"spdif",
	"key_pad",
	"sata",
	"usb_phy",
	"usb_phy0",
	"usb_phy1",
	"usb_phy2",
	"usb_ohci0",
	"usb_ohci1",
	"com",
	"spi3",
	"de_image0",
	"de_image1",
	"de_scale0",
	"de_scale1",
	"de_mix",
	"lcd0_ch0",
	"lcd1_ch0",
	"csi_isp",
	"tvd",
	"lcd0_ch1_s1",
	"lcd0_ch1_s2",
	"lcd1_ch1_s1",
	"lcd1_ch1_s2",
	"csi0",
	"csi1",
	"ve",
	"audio_codec",
	"avs",
	"ace",
	"lvds",
	"hdmi",
	"mali",
	"twi0",
	"twi1",
	"twi2",
	"can",
	"scr",
	"ps0",
	"ps1",
	"uart0",
	"uart1",
	"uart2",
	"uart3",
	"uart4",
	"uart5",
	"uart6",
	"uart7",

	/* clock gating for hang to AXI bus */
	"axi_dram",

	/* clock gating for hang to AHB bus */
	"ahb_usb0",
	"ahb_usb1",
	"ahb_usb2",
	"ahb_ss",
	"ahb_dma",
	"ahb_bist",
	"ahb_sdc0",
	"ahb_sdc1",
	"ahb_sdc2",
	"ahb_sdc3",
	"ahb_msc",
	"ahb_nfc",
	"ahb_sdramc",
	"ahb_ace",
	"ahb_emac",
	"ahb_ts",
	"ahb_spi0",
	"ahb_spi1",
	"ahb_spi2",
	"ahb_spi3",
	"ahb_pata",
	"ahb_sata",
	"ahb_com",
	"ahb_ve",
	"ahb_tvd",
	"ahb_tve0",
	"ahb_tve1",
	"ahb_lcd0",
	"ahb_lcd1",
	"ahb_csi0",
	"ahb_csi1",
	"ahb_hdmi",
	"ahb_de_image0",
	"ahb_de_image1",
	"ahb_de_scale0",
	"ahb_de_scale1",
	"ahb_de_mix",
	"ahb_mali",

	/* clock gating for hang APB bus */
	"apb_audio_codec",
	"apb_spdif",
	"apb_ac97",
	"apb_i2s",
	"apb_pio",
	"apb_ir0",
	"apb_ir1",
	"apb_key_pad",
	"apb_twi0",
	"apb_twi1",
	"apb_twi2",
	"apb_can",
	"apb_scr",
	"apb_ps0",
	"apb_ps1",
	"apb_uart0",
	"apb_uart1",
	"apb_uart2",
	"apb_uart3",
	"apb_uart4",
	"apb_uart5",
	"apb_uart6",
	"apb_uart7",

	/* clock gating for access dram */
	"sdram_ve",
	"sdram_csi0",
	"sdram_csi1",
	"sdram_ts",
	"sdram_tvd",
	"sdram_tve0",
	"sdram_tve1",
	"sdram_de_scale0",
	"sdram_de_scale1",
	"sdram_de_image0",
	"sdram_de_image1",
	"sdram_de_mix",
	"sdram_ace",
	"ahb_ehci1",
	"ahb_ohci1",

#ifdef CONFIG_ARCH_SUN5I
	"iep",
	"ahb_iep",
	"sdram_iep",
#endif
};

__s32 OSAL_CCMU_SetSrcFreq(__u32 nSclkNo, __u32 nFreq)
{
	struct clk *hSysClk = NULL;
	s32 retCode = -1;

	hSysClk = clk_get(NULL, _sysClkName[nSclkNo]);

	__inf("OSAL_CCMU_SetSrcFreq<%s,%d>\n", hSysClk->clk->name, nFreq);

	if (NULL == hSysClk) {
		__wrn("Fail to get handle for system clock [%d].\n", nSclkNo);
		return -1;
	}
	if (nFreq == clk_get_rate(hSysClk)) {
#if 0
		__inf("Sys clk[%d] freq is alreay %d, not need to set.\n",
		      nSclkNo, nFreq);
#endif

		clk_put(hSysClk);
		return 0;
	}
	retCode = clk_set_rate(hSysClk, nFreq);
	if (retCode == -1) {
		__wrn("Fail to set nFreq[%d] for sys clk[%d].\n", nFreq,
		      nSclkNo);
		clk_put(hSysClk);
		return retCode;
	}
	clk_put(hSysClk);
	hSysClk = NULL;

	return retCode;
}

__u32 OSAL_CCMU_GetSrcFreq(__u32 nSclkNo)
{
	struct clk *hSysClk = NULL;
	u32 nFreq = 0;

	hSysClk = clk_get(NULL, _sysClkName[nSclkNo]);
	if (NULL == hSysClk) {
		__wrn("Fail to get handle for system clock [%d].\n", nSclkNo);
		return -1;
	}
	nFreq = clk_get_rate(hSysClk);
	clk_put(hSysClk);
	hSysClk = NULL;

	return nFreq;
}

__hdle OSAL_CCMU_OpenMclk(__s32 nMclkNo)
{
	struct clk *hModClk = NULL;

	hModClk = clk_get(NULL, _modClkName[nMclkNo]);

	return (__hdle) hModClk;
}

__s32 OSAL_CCMU_CloseMclk(__hdle hMclk)
{
	struct clk *hModClk = (struct clk *)hMclk;

	clk_put(hModClk);

	return 0;
}

__s32 OSAL_CCMU_SetMclkSrc(__hdle hMclk, __u32 nSclkNo)
{
	struct clk *hSysClk = NULL;
	struct clk *hModClk = (struct clk *)hMclk;
	s32 retCode = -1;

	hSysClk = clk_get(NULL, _sysClkName[nSclkNo]);

	__inf("OSAL_CCMU_SetMclkSrc<%s,%s>\n", hModClk->clk->name,
	      hSysClk->clk->name);

	if (NULL == hSysClk) {
		__wrn("Fail to get handle for system clock [%d].\n", nSclkNo);
		return -1;
	}
	if (clk_get_parent(hModClk) == hSysClk) {
		__inf("Parent is alreay %d, not need to set.\n", nSclkNo);
		clk_put(hSysClk);
		return 0;
	}
	retCode = clk_set_parent(hModClk, hSysClk);
	if (-1 == retCode) {
		__wrn("Fail to set parent for clk.\n");
		clk_put(hSysClk);
		return -1;
	}

	clk_put(hSysClk);

	return retCode;
}

__s32 OSAL_CCMU_SetMclkDiv(__hdle hMclk, __s32 nDiv)
{
	struct clk *hModClk = (struct clk *)hMclk;
	struct clk *hParentClk = clk_get_parent(hModClk);
	u32 srcRate = clk_get_rate(hParentClk);

	__inf("OSAL_CCMU_SetMclkDiv<p:%s,m:%s,%d>\n", hParentClk->clk->name,
	      hModClk->clk->name, nDiv);

	if (nDiv == 0)
		return -1;

	return clk_set_rate(hModClk, srcRate / nDiv);
}

__s32 OSAL_CCMU_MclkOnOff(__hdle hMclk, __s32 bOnOff)
{
	struct clk *hModClk = (struct clk *)hMclk;
	__s32 ret = 0;

	__inf("OSAL_CCMU_MclkOnOff<%s,%d>\n", hModClk->clk->name, bOnOff);

	if (bOnOff) {
		if (!hModClk->enable)
			ret = clk_enable(hModClk);
	} else {
		while (hModClk->enable)
			clk_disable(hModClk);
	}
	return ret;
}

__s32 OSAL_CCMU_MclkReset(__hdle hMclk, __s32 bReset)
{
	struct clk *hModClk = (struct clk *)hMclk;

	__inf("OSAL_CCMU_MclkReset<%s,%d>\n", hModClk->clk->name, bReset);

	return clk_reset(hModClk, bReset);
}
