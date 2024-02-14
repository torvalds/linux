/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2016 Amlogic, Inc.
 * Author: Michael Turquette <mturquette@baylibre.com>
 *
 * Copyright (c) 2018 Amlogic, inc.
 * Author: Qiufang Dai <qiufang.dai@amlogic.com>
 * Author: Jian Hu <jian.hu@amlogic.com>
 *
 */
#ifndef __G12A_H
#define __G12A_H

/*
 * Clock controller register offsets
 *
 * Register offsets from the data sheet must be multiplied by 4 before
 * adding them to the base address to get the right value.
 */
#define HHI_MIPI_CNTL0			0x000
#define HHI_MIPI_CNTL1			0x004
#define HHI_MIPI_CNTL2			0x008
#define HHI_MIPI_STS			0x00C
#define HHI_GP0_PLL_CNTL0		0x040
#define HHI_GP0_PLL_CNTL1		0x044
#define HHI_GP0_PLL_CNTL2		0x048
#define HHI_GP0_PLL_CNTL3		0x04C
#define HHI_GP0_PLL_CNTL4		0x050
#define HHI_GP0_PLL_CNTL5		0x054
#define HHI_GP0_PLL_CNTL6		0x058
#define HHI_GP0_PLL_STS			0x05C
#define HHI_GP1_PLL_CNTL0		0x060
#define HHI_GP1_PLL_CNTL1		0x064
#define HHI_GP1_PLL_CNTL2		0x068
#define HHI_GP1_PLL_CNTL3		0x06C
#define HHI_GP1_PLL_CNTL4		0x070
#define HHI_GP1_PLL_CNTL5		0x074
#define HHI_GP1_PLL_CNTL6		0x078
#define HHI_GP1_PLL_STS			0x07C
#define HHI_PCIE_PLL_CNTL0		0x098
#define HHI_PCIE_PLL_CNTL1		0x09C
#define HHI_PCIE_PLL_CNTL2		0x0A0
#define HHI_PCIE_PLL_CNTL3		0x0A4
#define HHI_PCIE_PLL_CNTL4		0x0A8
#define HHI_PCIE_PLL_CNTL5		0x0AC
#define HHI_PCIE_PLL_STS		0x0B8
#define HHI_HIFI_PLL_CNTL0		0x0D8
#define HHI_HIFI_PLL_CNTL1		0x0DC
#define HHI_HIFI_PLL_CNTL2		0x0E0
#define HHI_HIFI_PLL_CNTL3		0x0E4
#define HHI_HIFI_PLL_CNTL4		0x0E8
#define HHI_HIFI_PLL_CNTL5		0x0EC
#define HHI_HIFI_PLL_CNTL6		0x0F0
#define HHI_VIID_CLK_DIV		0x128
#define HHI_VIID_CLK_CNTL		0x12C
#define HHI_GCLK_MPEG0			0x140
#define HHI_GCLK_MPEG1			0x144
#define HHI_GCLK_MPEG2			0x148
#define HHI_GCLK_OTHER			0x150
#define HHI_GCLK_OTHER2			0x154
#define HHI_SYS_CPU_CLK_CNTL1		0x15c
#define HHI_VID_CLK_DIV			0x164
#define HHI_MPEG_CLK_CNTL		0x174
#define HHI_AUD_CLK_CNTL		0x178
#define HHI_VID_CLK_CNTL		0x17c
#define HHI_TS_CLK_CNTL			0x190
#define HHI_VID_CLK_CNTL2		0x194
#define HHI_SYS_CPU_CLK_CNTL0		0x19c
#define HHI_VID_PLL_CLK_DIV		0x1A0
#define HHI_MALI_CLK_CNTL		0x1b0
#define HHI_VPU_CLKC_CNTL		0x1b4
#define HHI_VPU_CLK_CNTL		0x1bC
#define HHI_ISP_CLK_CNTL		0x1C0
#define HHI_NNA_CLK_CNTL		0x1C8
#define HHI_HDMI_CLK_CNTL		0x1CC
#define HHI_VDEC_CLK_CNTL		0x1E0
#define HHI_VDEC2_CLK_CNTL		0x1E4
#define HHI_VDEC3_CLK_CNTL		0x1E8
#define HHI_VDEC4_CLK_CNTL		0x1EC
#define HHI_HDCP22_CLK_CNTL		0x1F0
#define HHI_VAPBCLK_CNTL		0x1F4
#define HHI_SYS_CPUB_CLK_CNTL1		0x200
#define HHI_SYS_CPUB_CLK_CNTL		0x208
#define HHI_VPU_CLKB_CNTL		0x20C
#define HHI_SYS_CPU_CLK_CNTL2		0x210
#define HHI_SYS_CPU_CLK_CNTL3		0x214
#define HHI_SYS_CPU_CLK_CNTL4		0x218
#define HHI_SYS_CPU_CLK_CNTL5		0x21c
#define HHI_SYS_CPU_CLK_CNTL6		0x220
#define HHI_GEN_CLK_CNTL		0x228
#define HHI_VDIN_MEAS_CLK_CNTL		0x250
#define HHI_MIPIDSI_PHY_CLK_CNTL	0x254
#define HHI_NAND_CLK_CNTL		0x25C
#define HHI_SD_EMMC_CLK_CNTL		0x264
#define HHI_MPLL_CNTL0			0x278
#define HHI_MPLL_CNTL1			0x27C
#define HHI_MPLL_CNTL2			0x280
#define HHI_MPLL_CNTL3			0x284
#define HHI_MPLL_CNTL4			0x288
#define HHI_MPLL_CNTL5			0x28c
#define HHI_MPLL_CNTL6			0x290
#define HHI_MPLL_CNTL7			0x294
#define HHI_MPLL_CNTL8			0x298
#define HHI_FIX_PLL_CNTL0		0x2A0
#define HHI_FIX_PLL_CNTL1		0x2A4
#define HHI_FIX_PLL_CNTL3		0x2AC
#define HHI_SYS_PLL_CNTL0		0x2f4
#define HHI_SYS_PLL_CNTL1		0x2f8
#define HHI_SYS_PLL_CNTL2		0x2fc
#define HHI_SYS_PLL_CNTL3		0x300
#define HHI_SYS_PLL_CNTL4		0x304
#define HHI_SYS_PLL_CNTL5		0x308
#define HHI_SYS_PLL_CNTL6		0x30c
#define HHI_HDMI_PLL_CNTL0		0x320
#define HHI_HDMI_PLL_CNTL1		0x324
#define HHI_HDMI_PLL_CNTL2		0x328
#define HHI_HDMI_PLL_CNTL3		0x32c
#define HHI_HDMI_PLL_CNTL4		0x330
#define HHI_HDMI_PLL_CNTL5		0x334
#define HHI_HDMI_PLL_CNTL6		0x338
#define HHI_SPICC_CLK_CNTL		0x3dc
#define HHI_SYS1_PLL_CNTL0		0x380
#define HHI_SYS1_PLL_CNTL1		0x384
#define HHI_SYS1_PLL_CNTL2		0x388
#define HHI_SYS1_PLL_CNTL3		0x38c
#define HHI_SYS1_PLL_CNTL4		0x390
#define HHI_SYS1_PLL_CNTL5		0x394
#define HHI_SYS1_PLL_CNTL6		0x398

#endif /* __G12A_H */
