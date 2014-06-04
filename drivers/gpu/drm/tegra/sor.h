/*
 * Copyright (C) 2013 NVIDIA Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef DRM_TEGRA_SOR_H
#define DRM_TEGRA_SOR_H

#define SOR_CTXSW 0x00

#define SOR_SUPER_STATE_0 0x01

#define SOR_SUPER_STATE_1 0x02
#define  SOR_SUPER_STATE_ATTACHED		(1 << 3)
#define  SOR_SUPER_STATE_MODE_NORMAL		(1 << 2)
#define  SOR_SUPER_STATE_HEAD_MODE_MASK		(3 << 0)
#define  SOR_SUPER_STATE_HEAD_MODE_AWAKE	(2 << 0)
#define  SOR_SUPER_STATE_HEAD_MODE_SNOOZE	(1 << 0)
#define  SOR_SUPER_STATE_HEAD_MODE_SLEEP	(0 << 0)

#define SOR_STATE_0 0x03

#define SOR_STATE_1 0x04
#define  SOR_STATE_ASY_PIXELDEPTH_MASK		(0xf << 17)
#define  SOR_STATE_ASY_PIXELDEPTH_BPP_18_444	(0x2 << 17)
#define  SOR_STATE_ASY_PIXELDEPTH_BPP_24_444	(0x5 << 17)
#define  SOR_STATE_ASY_VSYNCPOL			(1 << 13)
#define  SOR_STATE_ASY_HSYNCPOL			(1 << 12)
#define  SOR_STATE_ASY_PROTOCOL_MASK		(0xf << 8)
#define  SOR_STATE_ASY_PROTOCOL_CUSTOM		(0xf << 8)
#define  SOR_STATE_ASY_PROTOCOL_DP_A		(0x8 << 8)
#define  SOR_STATE_ASY_PROTOCOL_DP_B		(0x9 << 8)
#define  SOR_STATE_ASY_PROTOCOL_LVDS		(0x0 << 8)
#define  SOR_STATE_ASY_CRC_MODE_MASK		(0x3 << 6)
#define  SOR_STATE_ASY_CRC_MODE_NON_ACTIVE	(0x2 << 6)
#define  SOR_STATE_ASY_CRC_MODE_COMPLETE	(0x1 << 6)
#define  SOR_STATE_ASY_CRC_MODE_ACTIVE		(0x0 << 6)
#define  SOR_STATE_ASY_OWNER(x)			(((x) & 0xf) << 0)

#define SOR_HEAD_STATE_0(x) (0x05 + (x))
#define SOR_HEAD_STATE_1(x) (0x07 + (x))
#define SOR_HEAD_STATE_2(x) (0x09 + (x))
#define SOR_HEAD_STATE_3(x) (0x0b + (x))
#define SOR_HEAD_STATE_4(x) (0x0d + (x))
#define SOR_HEAD_STATE_5(x) (0x0f + (x))
#define SOR_CRC_CNTRL 0x11
#define SOR_DP_DEBUG_MVID 0x12

#define SOR_CLK_CNTRL 0x13
#define  SOR_CLK_CNTRL_DP_LINK_SPEED_MASK	(0x1f << 2)
#define  SOR_CLK_CNTRL_DP_LINK_SPEED(x)		(((x) & 0x1f) << 2)
#define  SOR_CLK_CNTRL_DP_LINK_SPEED_G1_62	(0x06 << 2)
#define  SOR_CLK_CNTRL_DP_LINK_SPEED_G2_70	(0x0a << 2)
#define  SOR_CLK_CNTRL_DP_LINK_SPEED_G5_40	(0x14 << 2)
#define  SOR_CLK_CNTRL_DP_CLK_SEL_MASK		(3 << 0)
#define  SOR_CLK_CNTRL_DP_CLK_SEL_SINGLE_PCLK	(0 << 0)
#define  SOR_CLK_CNTRL_DP_CLK_SEL_DIFF_PCLK	(1 << 0)
#define  SOR_CLK_CNTRL_DP_CLK_SEL_SINGLE_DPCLK	(2 << 0)
#define  SOR_CLK_CNTRL_DP_CLK_SEL_DIFF_DPCLK	(3 << 0)

#define SOR_CAP 0x14

#define SOR_PWR 0x15
#define  SOR_PWR_TRIGGER			(1 << 31)
#define  SOR_PWR_MODE_SAFE			(1 << 28)
#define  SOR_PWR_NORMAL_STATE_PU		(1 << 0)

#define SOR_TEST 0x16
#define  SOR_TEST_ATTACHED			(1 << 10)
#define  SOR_TEST_HEAD_MODE_MASK		(3 << 8)
#define  SOR_TEST_HEAD_MODE_AWAKE		(2 << 8)

#define SOR_PLL_0 0x17
#define  SOR_PLL_0_ICHPMP_MASK			(0xf << 24)
#define  SOR_PLL_0_ICHPMP(x)			(((x) & 0xf) << 24)
#define  SOR_PLL_0_VCOCAP_MASK			(0xf << 8)
#define  SOR_PLL_0_VCOCAP(x)			(((x) & 0xf) << 8)
#define  SOR_PLL_0_VCOCAP_RST			SOR_PLL_0_VCOCAP(3)
#define  SOR_PLL_0_PLLREG_MASK			(0x3 << 6)
#define  SOR_PLL_0_PLLREG_LEVEL(x)		(((x) & 0x3) << 6)
#define  SOR_PLL_0_PLLREG_LEVEL_V25		SOR_PLL_0_PLLREG_LEVEL(0)
#define  SOR_PLL_0_PLLREG_LEVEL_V15		SOR_PLL_0_PLLREG_LEVEL(1)
#define  SOR_PLL_0_PLLREG_LEVEL_V35		SOR_PLL_0_PLLREG_LEVEL(2)
#define  SOR_PLL_0_PLLREG_LEVEL_V45		SOR_PLL_0_PLLREG_LEVEL(3)
#define  SOR_PLL_0_PULLDOWN			(1 << 5)
#define  SOR_PLL_0_RESISTOR_EXT			(1 << 4)
#define  SOR_PLL_0_VCOPD			(1 << 2)
#define  SOR_PLL_0_POWER_OFF			(1 << 0)

#define SOR_PLL_1 0x18
/* XXX: read-only bit? */
#define  SOR_PLL_1_TERM_COMPOUT			(1 << 15)
#define  SOR_PLL_1_TMDS_TERM			(1 << 8)

#define SOR_PLL_2 0x19
#define  SOR_PLL_2_LVDS_ENABLE			(1 << 25)
#define  SOR_PLL_2_SEQ_PLLCAPPD_ENFORCE		(1 << 24)
#define  SOR_PLL_2_PORT_POWERDOWN		(1 << 23)
#define  SOR_PLL_2_BANDGAP_POWERDOWN		(1 << 22)
#define  SOR_PLL_2_POWERDOWN_OVERRIDE		(1 << 18)
#define  SOR_PLL_2_SEQ_PLLCAPPD			(1 << 17)

#define SOR_PLL_3 0x1a
#define  SOR_PLL_3_PLL_VDD_MODE_V1_8 (0 << 13)
#define  SOR_PLL_3_PLL_VDD_MODE_V3_3 (1 << 13)

#define SOR_CSTM 0x1b
#define  SOR_CSTM_LVDS				(1 << 16)
#define  SOR_CSTM_LINK_ACT_B			(1 << 15)
#define  SOR_CSTM_LINK_ACT_A			(1 << 14)
#define  SOR_CSTM_UPPER				(1 << 11)

#define SOR_LVDS 0x1c
#define SOR_CRC_A 0x1d
#define SOR_CRC_B 0x1e
#define SOR_BLANK 0x1f
#define SOR_SEQ_CTL 0x20

#define SOR_LANE_SEQ_CTL 0x21
#define  SOR_LANE_SEQ_CTL_TRIGGER		(1 << 31)
#define  SOR_LANE_SEQ_CTL_SEQUENCE_UP		(0 << 20)
#define  SOR_LANE_SEQ_CTL_SEQUENCE_DOWN		(1 << 20)
#define  SOR_LANE_SEQ_CTL_POWER_STATE_UP	(0 << 16)
#define  SOR_LANE_SEQ_CTL_POWER_STATE_DOWN	(1 << 16)

#define SOR_SEQ_INST(x) (0x22 + (x))

#define SOR_PWM_DIV 0x32
#define  SOR_PWM_DIV_MASK			0xffffff

#define SOR_PWM_CTL 0x33
#define  SOR_PWM_CTL_TRIGGER			(1 << 31)
#define  SOR_PWM_CTL_CLK_SEL			(1 << 30)
#define  SOR_PWM_CTL_DUTY_CYCLE_MASK		0xffffff

#define SOR_VCRC_A_0 0x34
#define SOR_VCRC_A_1 0x35
#define SOR_VCRC_B_0 0x36
#define SOR_VCRC_B_1 0x37
#define SOR_CCRC_A_0 0x38
#define SOR_CCRC_A_1 0x39
#define SOR_CCRC_B_0 0x3a
#define SOR_CCRC_B_1 0x3b
#define SOR_EDATA_A_0 0x3c
#define SOR_EDATA_A_1 0x3d
#define SOR_EDATA_B_0 0x3e
#define SOR_EDATA_B_1 0x3f
#define SOR_COUNT_A_0 0x40
#define SOR_COUNT_A_1 0x41
#define SOR_COUNT_B_0 0x42
#define SOR_COUNT_B_1 0x43
#define SOR_DEBUG_A_0 0x44
#define SOR_DEBUG_A_1 0x45
#define SOR_DEBUG_B_0 0x46
#define SOR_DEBUG_B_1 0x47
#define SOR_TRIG 0x48
#define SOR_MSCHECK 0x49
#define SOR_XBAR_CTRL 0x4a
#define SOR_XBAR_POL 0x4b

#define SOR_DP_LINKCTL_0 0x4c
#define  SOR_DP_LINKCTL_LANE_COUNT_MASK		(0x1f << 16)
#define  SOR_DP_LINKCTL_LANE_COUNT(x)		(((1 << (x)) - 1) << 16)
#define  SOR_DP_LINKCTL_ENHANCED_FRAME		(1 << 14)
#define  SOR_DP_LINKCTL_TU_SIZE_MASK		(0x7f << 2)
#define  SOR_DP_LINKCTL_TU_SIZE(x)		(((x) & 0x7f) << 2)
#define  SOR_DP_LINKCTL_ENABLE			(1 << 0)

#define SOR_DP_LINKCTL_1 0x4d

#define SOR_LANE_DRIVE_CURRENT_0 0x4e
#define SOR_LANE_DRIVE_CURRENT_1 0x4f
#define SOR_LANE4_DRIVE_CURRENT_0 0x50
#define SOR_LANE4_DRIVE_CURRENT_1 0x51
#define  SOR_LANE_DRIVE_CURRENT_LANE3(x) (((x) & 0xff) << 24)
#define  SOR_LANE_DRIVE_CURRENT_LANE2(x) (((x) & 0xff) << 16)
#define  SOR_LANE_DRIVE_CURRENT_LANE1(x) (((x) & 0xff) << 8)
#define  SOR_LANE_DRIVE_CURRENT_LANE0(x) (((x) & 0xff) << 0)

#define SOR_LANE_PREEMPHASIS_0 0x52
#define SOR_LANE_PREEMPHASIS_1 0x53
#define SOR_LANE4_PREEMPHASIS_0 0x54
#define SOR_LANE4_PREEMPHASIS_1 0x55
#define  SOR_LANE_PREEMPHASIS_LANE3(x) (((x) & 0xff) << 24)
#define  SOR_LANE_PREEMPHASIS_LANE2(x) (((x) & 0xff) << 16)
#define  SOR_LANE_PREEMPHASIS_LANE1(x) (((x) & 0xff) << 8)
#define  SOR_LANE_PREEMPHASIS_LANE0(x) (((x) & 0xff) << 0)

#define SOR_LANE_POST_CURSOR_0 0x56
#define SOR_LANE_POST_CURSOR_1 0x57
#define  SOR_LANE_POST_CURSOR_LANE3(x) (((x) & 0xff) << 24)
#define  SOR_LANE_POST_CURSOR_LANE2(x) (((x) & 0xff) << 16)
#define  SOR_LANE_POST_CURSOR_LANE1(x) (((x) & 0xff) << 8)
#define  SOR_LANE_POST_CURSOR_LANE0(x) (((x) & 0xff) << 0)

#define SOR_DP_CONFIG_0 0x58
#define SOR_DP_CONFIG_DISPARITY_NEGATIVE	(1 << 31)
#define SOR_DP_CONFIG_ACTIVE_SYM_ENABLE		(1 << 26)
#define SOR_DP_CONFIG_ACTIVE_SYM_POLARITY	(1 << 24)
#define SOR_DP_CONFIG_ACTIVE_SYM_FRAC_MASK	(0xf << 16)
#define SOR_DP_CONFIG_ACTIVE_SYM_FRAC(x)	(((x) & 0xf) << 16)
#define SOR_DP_CONFIG_ACTIVE_SYM_COUNT_MASK	(0x7f << 8)
#define SOR_DP_CONFIG_ACTIVE_SYM_COUNT(x)	(((x) & 0x7f) << 8)
#define SOR_DP_CONFIG_WATERMARK_MASK	(0x3f << 0)
#define SOR_DP_CONFIG_WATERMARK(x)	(((x) & 0x3f) << 0)

#define SOR_DP_CONFIG_1 0x59
#define SOR_DP_MN_0 0x5a
#define SOR_DP_MN_1 0x5b

#define SOR_DP_PADCTL_0 0x5c
#define  SOR_DP_PADCTL_PAD_CAL_PD	(1 << 23)
#define  SOR_DP_PADCTL_TX_PU_ENABLE	(1 << 22)
#define  SOR_DP_PADCTL_TX_PU_MASK	(0xff << 8)
#define  SOR_DP_PADCTL_TX_PU(x)		(((x) & 0xff) << 8)
#define  SOR_DP_PADCTL_CM_TXD_3		(1 << 7)
#define  SOR_DP_PADCTL_CM_TXD_2		(1 << 6)
#define  SOR_DP_PADCTL_CM_TXD_1		(1 << 5)
#define  SOR_DP_PADCTL_CM_TXD_0		(1 << 4)
#define  SOR_DP_PADCTL_PD_TXD_3		(1 << 3)
#define  SOR_DP_PADCTL_PD_TXD_0		(1 << 2)
#define  SOR_DP_PADCTL_PD_TXD_1		(1 << 1)
#define  SOR_DP_PADCTL_PD_TXD_2		(1 << 0)

#define SOR_DP_PADCTL_1 0x5d

#define SOR_DP_DEBUG_0 0x5e
#define SOR_DP_DEBUG_1 0x5f

#define SOR_DP_SPARE_0 0x60
#define  SOR_DP_SPARE_MACRO_SOR_CLK	(1 << 2)
#define  SOR_DP_SPARE_PANEL_INTERNAL	(1 << 1)
#define  SOR_DP_SPARE_SEQ_ENABLE	(1 << 0)

#define SOR_DP_SPARE_1 0x61
#define SOR_DP_AUDIO_CTRL 0x62

#define SOR_DP_AUDIO_HBLANK_SYMBOLS 0x63
#define SOR_DP_AUDIO_HBLANK_SYMBOLS_MASK (0x01ffff << 0)

#define SOR_DP_AUDIO_VBLANK_SYMBOLS 0x64
#define SOR_DP_AUDIO_VBLANK_SYMBOLS_MASK (0x1fffff << 0)

#define SOR_DP_GENERIC_INFOFRAME_HEADER 0x65
#define SOR_DP_GENERIC_INFOFRAME_SUBPACK_0 0x66
#define SOR_DP_GENERIC_INFOFRAME_SUBPACK_1 0x67
#define SOR_DP_GENERIC_INFOFRAME_SUBPACK_2 0x68
#define SOR_DP_GENERIC_INFOFRAME_SUBPACK_3 0x69
#define SOR_DP_GENERIC_INFOFRAME_SUBPACK_4 0x6a
#define SOR_DP_GENERIC_INFOFRAME_SUBPACK_5 0x6b
#define SOR_DP_GENERIC_INFOFRAME_SUBPACK_6 0x6c

#define SOR_DP_TPG 0x6d
#define  SOR_DP_TPG_CHANNEL_CODING	(1 << 6)
#define  SOR_DP_TPG_SCRAMBLER_MASK	(3 << 4)
#define  SOR_DP_TPG_SCRAMBLER_FIBONACCI	(2 << 4)
#define  SOR_DP_TPG_SCRAMBLER_GALIOS	(1 << 4)
#define  SOR_DP_TPG_SCRAMBLER_NONE	(0 << 4)
#define  SOR_DP_TPG_PATTERN_MASK	(0xf << 0)
#define  SOR_DP_TPG_PATTERN_HBR2	(0x8 << 0)
#define  SOR_DP_TPG_PATTERN_CSTM	(0x7 << 0)
#define  SOR_DP_TPG_PATTERN_PRBS7	(0x6 << 0)
#define  SOR_DP_TPG_PATTERN_SBLERRRATE	(0x5 << 0)
#define  SOR_DP_TPG_PATTERN_D102	(0x4 << 0)
#define  SOR_DP_TPG_PATTERN_TRAIN3	(0x3 << 0)
#define  SOR_DP_TPG_PATTERN_TRAIN2	(0x2 << 0)
#define  SOR_DP_TPG_PATTERN_TRAIN1	(0x1 << 0)
#define  SOR_DP_TPG_PATTERN_NONE	(0x0 << 0)

#define SOR_DP_TPG_CONFIG 0x6e
#define SOR_DP_LQ_CSTM_0 0x6f
#define SOR_DP_LQ_CSTM_1 0x70
#define SOR_DP_LQ_CSTM_2 0x71

#endif
