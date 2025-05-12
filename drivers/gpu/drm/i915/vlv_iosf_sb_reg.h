/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _VLV_IOSF_SB_REG_H_
#define _VLV_IOSF_SB_REG_H_

/* See configdb bunit SB addr map */
#define BUNIT_REG_BISOC				0x11

/* PUNIT_REG_*SSPM0 */
#define   _SSPM0_SSC(val)			((val) << 0)
#define   SSPM0_SSC_MASK			_SSPM0_SSC(0x3)
#define   SSPM0_SSC_PWR_ON			_SSPM0_SSC(0x0)
#define   SSPM0_SSC_CLK_GATE			_SSPM0_SSC(0x1)
#define   SSPM0_SSC_RESET			_SSPM0_SSC(0x2)
#define   SSPM0_SSC_PWR_GATE			_SSPM0_SSC(0x3)
#define   _SSPM0_SSS(val)			((val) << 24)
#define   SSPM0_SSS_MASK			_SSPM0_SSS(0x3)
#define   SSPM0_SSS_PWR_ON			_SSPM0_SSS(0x0)
#define   SSPM0_SSS_CLK_GATE			_SSPM0_SSS(0x1)
#define   SSPM0_SSS_RESET			_SSPM0_SSS(0x2)
#define   SSPM0_SSS_PWR_GATE			_SSPM0_SSS(0x3)

/* PUNIT_REG_*SSPM1 */
#define   SSPM1_FREQSTAT_SHIFT			24
#define   SSPM1_FREQSTAT_MASK			(0x1f << SSPM1_FREQSTAT_SHIFT)
#define   SSPM1_FREQGUAR_SHIFT			8
#define   SSPM1_FREQGUAR_MASK			(0x1f << SSPM1_FREQGUAR_SHIFT)
#define   SSPM1_FREQ_SHIFT			0
#define   SSPM1_FREQ_MASK			(0x1f << SSPM1_FREQ_SHIFT)

#define PUNIT_REG_VEDSSPM0			0x32
#define PUNIT_REG_VEDSSPM1			0x33

#define PUNIT_REG_DSPSSPM			0x36
#define   DSPFREQSTAT_SHIFT_CHV			24
#define   DSPFREQSTAT_MASK_CHV			(0x1f << DSPFREQSTAT_SHIFT_CHV)
#define   DSPFREQGUAR_SHIFT_CHV			8
#define   DSPFREQGUAR_MASK_CHV			(0x1f << DSPFREQGUAR_SHIFT_CHV)
#define   DSPFREQSTAT_SHIFT			30
#define   DSPFREQSTAT_MASK			(0x3 << DSPFREQSTAT_SHIFT)
#define   DSPFREQGUAR_SHIFT			14
#define   DSPFREQGUAR_MASK			(0x3 << DSPFREQGUAR_SHIFT)
#define   DSP_MAXFIFO_PM5_STATUS		(1 << 22) /* chv */
#define   DSP_AUTO_CDCLK_GATE_DISABLE		(1 << 7) /* chv */
#define   DSP_MAXFIFO_PM5_ENABLE		(1 << 6) /* chv */
#define   _DP_SSC(val, pipe)			((val) << (2 * (pipe)))
#define   DP_SSC_MASK(pipe)			_DP_SSC(0x3, (pipe))
#define   DP_SSC_PWR_ON(pipe)			_DP_SSC(0x0, (pipe))
#define   DP_SSC_CLK_GATE(pipe)			_DP_SSC(0x1, (pipe))
#define   DP_SSC_RESET(pipe)			_DP_SSC(0x2, (pipe))
#define   DP_SSC_PWR_GATE(pipe)			_DP_SSC(0x3, (pipe))
#define   _DP_SSS(val, pipe)			((val) << (2 * (pipe) + 16))
#define   DP_SSS_MASK(pipe)			_DP_SSS(0x3, (pipe))
#define   DP_SSS_PWR_ON(pipe)			_DP_SSS(0x0, (pipe))
#define   DP_SSS_CLK_GATE(pipe)			_DP_SSS(0x1, (pipe))
#define   DP_SSS_RESET(pipe)			_DP_SSS(0x2, (pipe))
#define   DP_SSS_PWR_GATE(pipe)			_DP_SSS(0x3, (pipe))

#define PUNIT_REG_ISPSSPM0			0x39
#define PUNIT_REG_ISPSSPM1			0x3a

#define PUNIT_REG_PWRGT_CTRL			0x60
#define PUNIT_REG_PWRGT_STATUS			0x61
#define   PUNIT_PWRGT_MASK(pw_idx)		(3 << ((pw_idx) * 2))
#define   PUNIT_PWRGT_PWR_ON(pw_idx)		(0 << ((pw_idx) * 2))
#define   PUNIT_PWRGT_CLK_GATE(pw_idx)		(1 << ((pw_idx) * 2))
#define   PUNIT_PWRGT_RESET(pw_idx)		(2 << ((pw_idx) * 2))
#define   PUNIT_PWRGT_PWR_GATE(pw_idx)		(3 << ((pw_idx) * 2))

#define PUNIT_PWGT_IDX_RENDER			0
#define PUNIT_PWGT_IDX_MEDIA			1
#define PUNIT_PWGT_IDX_DISP2D			3
#define PUNIT_PWGT_IDX_DPIO_CMN_BC		5
#define PUNIT_PWGT_IDX_DPIO_TX_B_LANES_01	6
#define PUNIT_PWGT_IDX_DPIO_TX_B_LANES_23	7
#define PUNIT_PWGT_IDX_DPIO_TX_C_LANES_01	8
#define PUNIT_PWGT_IDX_DPIO_TX_C_LANES_23	9
#define PUNIT_PWGT_IDX_DPIO_RX0			10
#define PUNIT_PWGT_IDX_DPIO_RX1			11
#define PUNIT_PWGT_IDX_DPIO_CMN_D		12

#define PUNIT_REG_GPU_LFM			0xd3
#define PUNIT_REG_GPU_FREQ_REQ			0xd4
#define PUNIT_REG_GPU_FREQ_STS			0xd8
#define   GPLLENABLE				(1 << 4)
#define   GENFREQSTATUS				(1 << 0)
#define PUNIT_REG_MEDIA_TURBO_FREQ_REQ		0xdc
#define PUNIT_REG_CZ_TIMESTAMP			0xce

#define PUNIT_FUSE_BUS2				0xf6 /* bits 47:40 */
#define PUNIT_FUSE_BUS1				0xf5 /* bits 55:48 */

#define FB_GFX_FMAX_AT_VMAX_FUSE		0x136
#define FB_GFX_FREQ_FUSE_MASK			0xff
#define FB_GFX_FMAX_AT_VMAX_2SS4EU_FUSE_SHIFT	24
#define FB_GFX_FMAX_AT_VMAX_2SS6EU_FUSE_SHIFT	16
#define FB_GFX_FMAX_AT_VMAX_2SS8EU_FUSE_SHIFT	8

#define FB_GFX_FMIN_AT_VMIN_FUSE		0x137
#define FB_GFX_FMIN_AT_VMIN_FUSE_SHIFT		8

#define PUNIT_REG_DDR_SETUP2			0x139
#define   FORCE_DDR_FREQ_REQ_ACK		(1 << 8)
#define   FORCE_DDR_LOW_FREQ			(1 << 1)
#define   FORCE_DDR_HIGH_FREQ			(1 << 0)

#define PUNIT_GPU_STATUS_REG			0xdb
#define PUNIT_GPU_STATUS_MAX_FREQ_SHIFT	16
#define PUNIT_GPU_STATUS_MAX_FREQ_MASK		0xff
#define PUNIT_GPU_STATIS_GFX_MIN_FREQ_SHIFT	8
#define PUNIT_GPU_STATUS_GFX_MIN_FREQ_MASK	0xff

#define PUNIT_GPU_DUTYCYCLE_REG		0xdf
#define PUNIT_GPU_DUTYCYCLE_RPE_FREQ_SHIFT	8
#define PUNIT_GPU_DUTYCYCLE_RPE_FREQ_MASK	0xff

#define IOSF_NC_FB_GFX_FREQ_FUSE		0x1c
#define   FB_GFX_MAX_FREQ_FUSE_SHIFT		3
#define   FB_GFX_MAX_FREQ_FUSE_MASK		0x000007f8
#define   FB_GFX_FGUARANTEED_FREQ_FUSE_SHIFT	11
#define   FB_GFX_FGUARANTEED_FREQ_FUSE_MASK	0x0007f800
#define IOSF_NC_FB_GFX_FMAX_FUSE_HI		0x34
#define   FB_FMAX_VMIN_FREQ_HI_MASK		0x00000007
#define IOSF_NC_FB_GFX_FMAX_FUSE_LO		0x30
#define   FB_FMAX_VMIN_FREQ_LO_SHIFT		27
#define   FB_FMAX_VMIN_FREQ_LO_MASK		0xf8000000

#define VLV_TURBO_SOC_OVERRIDE		0x04
#define   VLV_OVERRIDE_EN		1
#define   VLV_SOC_TDP_EN		(1 << 1)
#define   VLV_BIAS_CPU_125_SOC_875	(6 << 2)
#define   CHV_BIAS_CPU_50_SOC_50	(3 << 2)

/* vlv2 north clock has */
#define CCK_FUSE_REG				0x8
#define  CCK_FUSE_HPLL_FREQ_MASK		0x3
#define CCK_REG_DSI_PLL_FUSE			0x44
#define CCK_REG_DSI_PLL_CONTROL			0x48
#define  DSI_PLL_VCO_EN				(1 << 31)
#define  DSI_PLL_LDO_GATE			(1 << 30)
#define  DSI_PLL_P1_POST_DIV_SHIFT		17
#define  DSI_PLL_P1_POST_DIV_MASK		(0x1ff << 17)
#define  DSI_PLL_P2_MUX_DSI0_DIV2		(1 << 13)
#define  DSI_PLL_P3_MUX_DSI1_DIV2		(1 << 12)
#define  DSI_PLL_MUX_MASK			(3 << 9)
#define  DSI_PLL_MUX_DSI0_DSIPLL		(0 << 10)
#define  DSI_PLL_MUX_DSI0_CCK			(1 << 10)
#define  DSI_PLL_MUX_DSI1_DSIPLL		(0 << 9)
#define  DSI_PLL_MUX_DSI1_CCK			(1 << 9)
#define  DSI_PLL_CLK_GATE_MASK			(0xf << 5)
#define  DSI_PLL_CLK_GATE_DSI0_DSIPLL		(1 << 8)
#define  DSI_PLL_CLK_GATE_DSI1_DSIPLL		(1 << 7)
#define  DSI_PLL_CLK_GATE_DSI0_CCK		(1 << 6)
#define  DSI_PLL_CLK_GATE_DSI1_CCK		(1 << 5)
#define  DSI_PLL_LOCK				(1 << 0)
#define CCK_REG_DSI_PLL_DIVIDER			0x4c
#define  DSI_PLL_LFSR				(1 << 31)
#define  DSI_PLL_FRACTION_EN			(1 << 30)
#define  DSI_PLL_FRAC_COUNTER_SHIFT		27
#define  DSI_PLL_FRAC_COUNTER_MASK		(7 << 27)
#define  DSI_PLL_USYNC_CNT_SHIFT		18
#define  DSI_PLL_USYNC_CNT_MASK			(0x1ff << 18)
#define  DSI_PLL_N1_DIV_SHIFT			16
#define  DSI_PLL_N1_DIV_MASK			(3 << 16)
#define  DSI_PLL_M1_DIV_SHIFT			0
#define  DSI_PLL_M1_DIV_MASK			(0x1ff << 0)
#define CCK_CZ_CLOCK_CONTROL			0x62
#define CCK_GPLL_CLOCK_CONTROL			0x67
#define CCK_DISPLAY_CLOCK_CONTROL		0x6b
#define CCK_DISPLAY_REF_CLOCK_CONTROL		0x6c
#define  CCK_TRUNK_FORCE_ON			(1 << 17)
#define  CCK_TRUNK_FORCE_OFF			(1 << 16)
#define  CCK_FREQUENCY_STATUS			(0x1f << 8)
#define  CCK_FREQUENCY_STATUS_SHIFT		8
#define  CCK_FREQUENCY_VALUES			(0x1f << 0)

#endif /* _VLV_IOSF_SB_REG_H_ */
