/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * Copyright (c) 2016 AmLogic, Inc.
 * Author: Michael Turquette <mturquette@baylibre.com>
 */

#ifndef __GXBB_H
#define __GXBB_H

/*
 * Clock controller register offsets
 *
 * Register offsets from the data sheet are listed in comment blocks below.
 * Those offsets must be multiplied by 4 before adding them to the base address
 * to get the right value
 */
#define SCR				0x2C /* 0x0b offset in data sheet */
#define TIMEOUT_VALUE			0x3c /* 0x0f offset in data sheet */

#define HHI_GP0_PLL_CNTL		0x40 /* 0x10 offset in data sheet */
#define HHI_GP0_PLL_CNTL2		0x44 /* 0x11 offset in data sheet */
#define HHI_GP0_PLL_CNTL3		0x48 /* 0x12 offset in data sheet */
#define HHI_GP0_PLL_CNTL4		0x4c /* 0x13 offset in data sheet */
#define	HHI_GP0_PLL_CNTL5		0x50 /* 0x14 offset in data sheet */
#define	HHI_GP0_PLL_CNTL1		0x58 /* 0x16 offset in data sheet */

#define HHI_XTAL_DIVN_CNTL		0xbc /* 0x2f offset in data sheet */
#define HHI_TIMER90K			0xec /* 0x3b offset in data sheet */

#define HHI_MEM_PD_REG0			0x100 /* 0x40 offset in data sheet */
#define HHI_MEM_PD_REG1			0x104 /* 0x41 offset in data sheet */
#define HHI_VPU_MEM_PD_REG1		0x108 /* 0x42 offset in data sheet */
#define HHI_VIID_CLK_DIV		0x128 /* 0x4a offset in data sheet */
#define HHI_VIID_CLK_CNTL		0x12c /* 0x4b offset in data sheet */

#define HHI_GCLK_MPEG0			0x140 /* 0x50 offset in data sheet */
#define HHI_GCLK_MPEG1			0x144 /* 0x51 offset in data sheet */
#define HHI_GCLK_MPEG2			0x148 /* 0x52 offset in data sheet */
#define HHI_GCLK_OTHER			0x150 /* 0x54 offset in data sheet */
#define HHI_GCLK_AO			0x154 /* 0x55 offset in data sheet */
#define HHI_SYS_OSCIN_CNTL		0x158 /* 0x56 offset in data sheet */
#define HHI_SYS_CPU_CLK_CNTL1		0x15c /* 0x57 offset in data sheet */
#define HHI_SYS_CPU_RESET_CNTL		0x160 /* 0x58 offset in data sheet */
#define HHI_VID_CLK_DIV			0x164 /* 0x59 offset in data sheet */

#define HHI_MPEG_CLK_CNTL		0x174 /* 0x5d offset in data sheet */
#define HHI_AUD_CLK_CNTL		0x178 /* 0x5e offset in data sheet */
#define HHI_VID_CLK_CNTL		0x17c /* 0x5f offset in data sheet */
#define HHI_AUD_CLK_CNTL2		0x190 /* 0x64 offset in data sheet */
#define HHI_VID_CLK_CNTL2		0x194 /* 0x65 offset in data sheet */
#define HHI_SYS_CPU_CLK_CNTL0		0x19c /* 0x67 offset in data sheet */
#define HHI_VID_PLL_CLK_DIV		0x1a0 /* 0x68 offset in data sheet */
#define HHI_AUD_CLK_CNTL3		0x1a4 /* 0x69 offset in data sheet */
#define HHI_MALI_CLK_CNTL		0x1b0 /* 0x6c offset in data sheet */
#define HHI_VPU_CLK_CNTL		0x1bC /* 0x6f offset in data sheet */

#define HHI_HDMI_CLK_CNTL		0x1CC /* 0x73 offset in data sheet */
#define HHI_VDEC_CLK_CNTL		0x1E0 /* 0x78 offset in data sheet */
#define HHI_VDEC2_CLK_CNTL		0x1E4 /* 0x79 offset in data sheet */
#define HHI_VDEC3_CLK_CNTL		0x1E8 /* 0x7a offset in data sheet */
#define HHI_VDEC4_CLK_CNTL		0x1EC /* 0x7b offset in data sheet */
#define HHI_HDCP22_CLK_CNTL		0x1F0 /* 0x7c offset in data sheet */
#define HHI_VAPBCLK_CNTL		0x1F4 /* 0x7d offset in data sheet */

#define HHI_VPU_CLKB_CNTL		0x20C /* 0x83 offset in data sheet */
#define HHI_USB_CLK_CNTL		0x220 /* 0x88 offset in data sheet */
#define HHI_32K_CLK_CNTL		0x224 /* 0x89 offset in data sheet */
#define HHI_GEN_CLK_CNTL		0x228 /* 0x8a offset in data sheet */

#define HHI_PCM_CLK_CNTL		0x258 /* 0x96 offset in data sheet */
#define HHI_NAND_CLK_CNTL		0x25C /* 0x97 offset in data sheet */
#define HHI_SD_EMMC_CLK_CNTL		0x264 /* 0x99 offset in data sheet */

#define HHI_MPLL_CNTL			0x280 /* 0xa0 offset in data sheet */
#define HHI_MPLL_CNTL2			0x284 /* 0xa1 offset in data sheet */
#define HHI_MPLL_CNTL3			0x288 /* 0xa2 offset in data sheet */
#define HHI_MPLL_CNTL4			0x28C /* 0xa3 offset in data sheet */
#define HHI_MPLL_CNTL5			0x290 /* 0xa4 offset in data sheet */
#define HHI_MPLL_CNTL6			0x294 /* 0xa5 offset in data sheet */
#define HHI_MPLL_CNTL7			0x298 /* MP0, 0xa6 offset in data sheet */
#define HHI_MPLL_CNTL8			0x29C /* MP1, 0xa7 offset in data sheet */
#define HHI_MPLL_CNTL9			0x2A0 /* MP2, 0xa8 offset in data sheet */
#define HHI_MPLL_CNTL10			0x2A4 /* MP2, 0xa9 offset in data sheet */

#define HHI_MPLL3_CNTL0			0x2E0 /* 0xb8 offset in data sheet */
#define HHI_MPLL3_CNTL1			0x2E4 /* 0xb9 offset in data sheet */
#define HHI_VDAC_CNTL0			0x2F4 /* 0xbd offset in data sheet */
#define HHI_VDAC_CNTL1			0x2F8 /* 0xbe offset in data sheet */

#define HHI_SYS_PLL_CNTL		0x300 /* 0xc0 offset in data sheet */
#define HHI_SYS_PLL_CNTL2		0x304 /* 0xc1 offset in data sheet */
#define HHI_SYS_PLL_CNTL3		0x308 /* 0xc2 offset in data sheet */
#define HHI_SYS_PLL_CNTL4		0x30c /* 0xc3 offset in data sheet */
#define HHI_SYS_PLL_CNTL5		0x310 /* 0xc4 offset in data sheet */
#define HHI_DPLL_TOP_I			0x318 /* 0xc6 offset in data sheet */
#define HHI_DPLL_TOP2_I			0x31C /* 0xc7 offset in data sheet */
#define HHI_HDMI_PLL_CNTL		0x320 /* 0xc8 offset in data sheet */
#define HHI_HDMI_PLL_CNTL2		0x324 /* 0xc9 offset in data sheet */
#define HHI_HDMI_PLL_CNTL3		0x328 /* 0xca offset in data sheet */
#define HHI_HDMI_PLL_CNTL4		0x32C /* 0xcb offset in data sheet */
#define HHI_HDMI_PLL_CNTL5		0x330 /* 0xcc offset in data sheet */
#define HHI_HDMI_PLL_CNTL6		0x334 /* 0xcd offset in data sheet */
#define HHI_HDMI_PLL_CNTL_I		0x338 /* 0xce offset in data sheet */
#define HHI_HDMI_PLL_CNTL7		0x33C /* 0xcf offset in data sheet */

#define HHI_HDMI_PHY_CNTL0		0x3A0 /* 0xe8 offset in data sheet */
#define HHI_HDMI_PHY_CNTL1		0x3A4 /* 0xe9 offset in data sheet */
#define HHI_HDMI_PHY_CNTL2		0x3A8 /* 0xea offset in data sheet */
#define HHI_HDMI_PHY_CNTL3		0x3AC /* 0xeb offset in data sheet */

#define HHI_VID_LOCK_CLK_CNTL		0x3C8 /* 0xf2 offset in data sheet */
#define HHI_BT656_CLK_CNTL		0x3D4 /* 0xf5 offset in data sheet */
#define HHI_SAR_CLK_CNTL		0x3D8 /* 0xf6 offset in data sheet */

/*
 * CLKID index values
 *
 * These indices are entirely contrived and do not map onto the hardware.
 * It has now been decided to expose everything by default in the DT header:
 * include/dt-bindings/clock/gxbb-clkc.h. Only the clocks ids we don't want
 * to expose, such as the internal muxes and dividers of composite clocks,
 * will remain defined here.
 */
/* ID 1 is unused (it was used by the non-existing CLKID_CPUCLK before) */
#define CLKID_MPEG_SEL		  10
#define CLKID_MPEG_DIV		  11
#define CLKID_SAR_ADC_DIV	  99
#define CLKID_MALI_0_DIV	  101
#define CLKID_MALI_1_DIV	  104
#define CLKID_CTS_AMCLK_SEL	  108
#define CLKID_CTS_AMCLK_DIV	  109
#define CLKID_CTS_MCLK_I958_SEL	  111
#define CLKID_CTS_MCLK_I958_DIV	  112
#define CLKID_32K_CLK_SEL	  115
#define CLKID_32K_CLK_DIV	  116
#define CLKID_SD_EMMC_A_CLK0_SEL  117
#define CLKID_SD_EMMC_A_CLK0_DIV  118
#define CLKID_SD_EMMC_B_CLK0_SEL  120
#define CLKID_SD_EMMC_B_CLK0_DIV  121
#define CLKID_SD_EMMC_C_CLK0_SEL  123
#define CLKID_SD_EMMC_C_CLK0_DIV  124
#define CLKID_VPU_0_DIV		  127
#define CLKID_VPU_1_DIV		  130
#define CLKID_VAPB_0_DIV	  134
#define CLKID_VAPB_1_DIV	  137
#define CLKID_HDMI_PLL_PRE_MULT	  141
#define CLKID_MPLL0_DIV		  142
#define CLKID_MPLL1_DIV		  143
#define CLKID_MPLL2_DIV		  144
#define CLKID_MPLL_PREDIV	  145
#define CLKID_FCLK_DIV2_DIV	  146
#define CLKID_FCLK_DIV3_DIV	  147
#define CLKID_FCLK_DIV4_DIV	  148
#define CLKID_FCLK_DIV5_DIV	  149
#define CLKID_FCLK_DIV7_DIV	  150
#define CLKID_VDEC_1_SEL	  151
#define CLKID_VDEC_1_DIV	  152
#define CLKID_VDEC_HEVC_SEL	  154
#define CLKID_VDEC_HEVC_DIV	  155
#define CLKID_GEN_CLK_SEL	  157
#define CLKID_GEN_CLK_DIV	  158
#define CLKID_FIXED_PLL_DCO	  160
#define CLKID_HDMI_PLL_DCO	  161
#define CLKID_HDMI_PLL_OD	  162
#define CLKID_HDMI_PLL_OD2	  163
#define CLKID_SYS_PLL_DCO	  164
#define CLKID_GP0_PLL_DCO	  165
#define CLKID_VID_PLL_SEL	  167
#define CLKID_VID_PLL_DIV	  168
#define CLKID_VCLK_SEL		  169
#define CLKID_VCLK2_SEL		  170
#define CLKID_VCLK_INPUT	  171
#define CLKID_VCLK2_INPUT	  172
#define CLKID_VCLK_DIV		  173
#define CLKID_VCLK2_DIV		  174
#define CLKID_VCLK_DIV2_EN	  177
#define CLKID_VCLK_DIV4_EN	  178
#define CLKID_VCLK_DIV6_EN	  179
#define CLKID_VCLK_DIV12_EN	  180
#define CLKID_VCLK2_DIV2_EN	  181
#define CLKID_VCLK2_DIV4_EN	  182
#define CLKID_VCLK2_DIV6_EN	  183
#define CLKID_VCLK2_DIV12_EN	  184
#define CLKID_CTS_ENCI_SEL	  195
#define CLKID_CTS_ENCP_SEL	  196
#define CLKID_CTS_VDAC_SEL	  197
#define CLKID_HDMI_TX_SEL	  198
#define CLKID_HDMI_SEL		  203
#define CLKID_HDMI_DIV		  204

#define NR_CLKS			  206

/* include the CLKIDs that have been made part of the DT binding */
#include <dt-bindings/clock/gxbb-clkc.h>

#endif /* __GXBB_H */
