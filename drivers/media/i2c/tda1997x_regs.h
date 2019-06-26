// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Gateworks Corporation
 */

/* Page 0x00 - General Control */
#define REG_VERSION		0x0000
#define REG_INPUT_SEL		0x0001
#define REG_SVC_MODE		0x0002
#define REG_HPD_MAN_CTRL	0x0003
#define REG_RT_MAN_CTRL		0x0004
#define REG_STANDBY_SOFT_RST	0x000A
#define REG_HDMI_SOFT_RST	0x000B
#define REG_HDMI_INFO_RST	0x000C
#define REG_INT_FLG_CLR_TOP	0x000E
#define REG_INT_FLG_CLR_SUS	0x000F
#define REG_INT_FLG_CLR_DDC	0x0010
#define REG_INT_FLG_CLR_RATE	0x0011
#define REG_INT_FLG_CLR_MODE	0x0012
#define REG_INT_FLG_CLR_INFO	0x0013
#define REG_INT_FLG_CLR_AUDIO	0x0014
#define REG_INT_FLG_CLR_HDCP	0x0015
#define REG_INT_FLG_CLR_AFE	0x0016
#define REG_INT_MASK_TOP	0x0017
#define REG_INT_MASK_SUS	0x0018
#define REG_INT_MASK_DDC	0x0019
#define REG_INT_MASK_RATE	0x001A
#define REG_INT_MASK_MODE	0x001B
#define REG_INT_MASK_INFO	0x001C
#define REG_INT_MASK_AUDIO	0x001D
#define REG_INT_MASK_HDCP	0x001E
#define REG_INT_MASK_AFE	0x001F
#define REG_DETECT_5V		0x0020
#define REG_SUS_STATUS		0x0021
#define REG_V_PER		0x0022
#define REG_H_PER		0x0025
#define REG_HS_WIDTH		0x0027
#define REG_FMT_H_TOT		0x0029
#define REG_FMT_H_ACT		0x002b
#define REG_FMT_H_FRONT		0x002d
#define REG_FMT_H_SYNC		0x002f
#define REG_FMT_H_BACK		0x0031
#define REG_FMT_V_TOT		0x0033
#define REG_FMT_V_ACT		0x0035
#define REG_FMT_V_FRONT_F1	0x0037
#define REG_FMT_V_FRONT_F2	0x0038
#define REG_FMT_V_SYNC		0x0039
#define REG_FMT_V_BACK_F1	0x003a
#define REG_FMT_V_BACK_F2	0x003b
#define REG_FMT_DE_ACT		0x003c
#define REG_RATE_CTRL		0x0040
#define REG_CLK_MIN_RATE	0x0043
#define REG_CLK_MAX_RATE	0x0046
#define REG_CLK_A_STATUS	0x0049
#define REG_CLK_A_RATE		0x004A
#define REG_DRIFT_CLK_A_REG	0x004D
#define REG_CLK_B_STATUS	0x004E
#define REG_CLK_B_RATE		0x004F
#define REG_DRIFT_CLK_B_REG	0x0052
#define REG_HDCP_CTRL		0x0060
#define REG_HDCP_KDS		0x0061
#define REG_HDCP_BCAPS		0x0063
#define REG_HDCP_KEY_CTRL	0x0064
#define REG_INFO_CTRL		0x0076
#define REG_INFO_EXCEED		0x0077
#define REG_PIX_REPEAT		0x007B
#define REG_AUDIO_PATH		0x007C
#define REG_AUDCFG		0x007D
#define REG_AUDIO_OUT_ENABLE	0x007E
#define REG_AUDIO_OUT_HIZ	0x007F
#define REG_VDP_CTRL		0x0080
#define REG_VDP_MATRIX		0x0081
#define REG_VHREF_CTRL		0x00A0
#define REG_PXCNT_PR		0x00A2
#define REG_PXCNT_NPIX		0x00A4
#define REG_LCNT_PR		0x00A6
#define REG_LCNT_NLIN		0x00A8
#define REG_HREF_S		0x00AA
#define REG_HREF_E		0x00AC
#define REG_HS_S		0x00AE
#define REG_HS_E		0x00B0
#define REG_VREF_F1_S		0x00B2
#define REG_VREF_F1_WIDTH	0x00B4
#define REG_VREF_F2_S		0x00B5
#define REG_VREF_F2_WIDTH	0x00B7
#define REG_VS_F1_LINE_S	0x00B8
#define REG_VS_F1_LINE_WIDTH	0x00BA
#define REG_VS_F2_LINE_S	0x00BB
#define REG_VS_F2_LINE_WIDTH	0x00BD
#define REG_VS_F1_PIX_S		0x00BE
#define REG_VS_F1_PIX_E		0x00C0
#define REG_VS_F2_PIX_S		0x00C2
#define REG_VS_F2_PIX_E		0x00C4
#define REG_FREF_F1_S		0x00C6
#define REG_FREF_F2_S		0x00C8
#define REG_FDW_S		0x00ca
#define REG_FDW_E		0x00cc
#define REG_BLK_GY		0x00da
#define REG_BLK_BU		0x00dc
#define REG_BLK_RV		0x00de
#define REG_FILTERS_CTRL	0x00e0
#define REG_DITHERING_CTRL	0x00E9
#define REG_OF			0x00EA
#define REG_PCLK		0x00EB
#define REG_HS_HREF		0x00EC
#define REG_VS_VREF		0x00ED
#define REG_DE_FREF		0x00EE
#define REG_VP35_32_CTRL	0x00EF
#define REG_VP31_28_CTRL	0x00F0
#define REG_VP27_24_CTRL	0x00F1
#define REG_VP23_20_CTRL	0x00F2
#define REG_VP19_16_CTRL	0x00F3
#define REG_VP15_12_CTRL	0x00F4
#define REG_VP11_08_CTRL	0x00F5
#define REG_VP07_04_CTRL	0x00F6
#define REG_VP03_00_CTRL	0x00F7
#define REG_CURPAGE_00H		0xFF

#define MASK_VPER		0x3fffff
#define MASK_VHREF		0x3fff
#define MASK_HPER		0x0fff
#define MASK_HSWIDTH		0x03ff

/* HPD Detection */
#define DETECT_UTIL		BIT(7)	/* utility of HDMI level */
#define DETECT_HPD		BIT(6)	/* HPD of HDMI level */
#define DETECT_5V_SEL		BIT(2)	/* 5V present on selected input */
#define DETECT_5V_B		BIT(1)	/* 5V present on input B */
#define DETECT_5V_A		BIT(0)	/* 5V present on input A */

/* Input Select */
#define INPUT_SEL_RST_FMT	BIT(7)	/* 1=reset format measurement */
#define INPUT_SEL_RST_VDP	BIT(2)	/* 1=reset video data path */
#define INPUT_SEL_OUT_MODE	BIT(1)	/* 0=loop 1=bypass */
#define INPUT_SEL_B		BIT(0)	/* 0=inputA 1=inputB */

/* Service Mode */
#define SVC_MODE_CLK2_MASK	0xc0
#define SVC_MODE_CLK2_SHIFT	6
#define SVC_MODE_CLK2_XTL	0L
#define SVC_MODE_CLK2_XTLDIV2	1L
#define SVC_MODE_CLK2_HDMIX2	3L
#define SVC_MODE_CLK1_MASK	0x30
#define SVC_MODE_CLK1_SHIFT	4
#define SVC_MODE_CLK1_XTAL	0L
#define SVC_MODE_CLK1_XTLDIV2	1L
#define SVC_MODE_CLK1_HDMI	3L
#define SVC_MODE_RAMP		BIT(3)	/* 0=colorbar 1=ramp */
#define SVC_MODE_PAL		BIT(2)	/* 0=NTSC(480i/p) 1=PAL(576i/p) */
#define SVC_MODE_INT_PROG	BIT(1)	/* 0=interlaced 1=progressive */
#define SVC_MODE_SM_ON		BIT(0)	/* Enable color bars and tone gen */

/* HDP Manual Control */
#define HPD_MAN_CTRL_HPD_PULSE	BIT(7)	/* HPD Pulse low 110ms */
#define HPD_MAN_CTRL_5VEN	BIT(2)	/* Output 5V */
#define HPD_MAN_CTRL_HPD_B	BIT(1)	/* Assert HPD High for Input A */
#define HPD_MAN_CTRL_HPD_A	BIT(0)	/* Assert HPD High for Input A */

/* RT_MAN_CTRL */
#define RT_MAN_CTRL_RT_AUTO	BIT(7)
#define RT_MAN_CTRL_RT		BIT(6)
#define RT_MAN_CTRL_RT_B	BIT(1)	/* enable TMDS pull-up on Input B */
#define RT_MAN_CTRL_RT_A	BIT(0)	/* enable TMDS pull-up on Input A */

/* VDP_CTRL */
#define VDP_CTRL_COMPDEL_BP	BIT(5)	/* bypass compdel */
#define VDP_CTRL_FORMATTER_BP	BIT(4)	/* bypass formatter */
#define VDP_CTRL_PREFILTER_BP	BIT(1)	/* bypass prefilter */
#define VDP_CTRL_MATRIX_BP	BIT(0)	/* bypass matrix conversion */

/* REG_VHREF_CTRL */
#define VHREF_INT_DET		BIT(7)	/* interlace detect: 1=alt 0=frame */
#define VHREF_VSYNC_MASK	0x60
#define VHREF_VSYNC_SHIFT	6
#define VHREF_VSYNC_AUTO	0L
#define VHREF_VSYNC_FDW		1L
#define VHREF_VSYNC_EVEN	2L
#define VHREF_VSYNC_ODD		3L
#define VHREF_STD_DET_MASK	0x18
#define VHREF_STD_DET_SHIFT	3
#define VHREF_STD_DET_PAL	0L
#define VHREF_STD_DET_NTSC	1L
#define VHREF_STD_DET_AUTO	2L
#define VHREF_STD_DET_OFF	3L
#define VHREF_VREF_SRC_STD	BIT(2)	/* 1=from standard 0=manual */
#define VHREF_HREF_SRC_STD	BIT(1)	/* 1=from standard 0=manual */
#define VHREF_HSYNC_SEL_HS	BIT(0)	/* 1=HS 0=VS */

/* AUDIO_OUT_ENABLE */
#define AUDIO_OUT_ENABLE_ACLK	BIT(5)
#define AUDIO_OUT_ENABLE_WS	BIT(4)
#define AUDIO_OUT_ENABLE_AP3	BIT(3)
#define AUDIO_OUT_ENABLE_AP2	BIT(2)
#define AUDIO_OUT_ENABLE_AP1	BIT(1)
#define AUDIO_OUT_ENABLE_AP0	BIT(0)

/* Prefilter Control */
#define FILTERS_CTRL_BU_MASK	0x0c
#define FILTERS_CTRL_BU_SHIFT	2
#define FILTERS_CTRL_RV_MASK	0x03
#define FILTERS_CTRL_RV_SHIFT	0
#define FILTERS_CTRL_OFF	0L	/* off */
#define FILTERS_CTRL_2TAP	1L	/* 2 Taps */
#define FILTERS_CTRL_7TAP	2L	/* 7 Taps */
#define FILTERS_CTRL_2_7TAP	3L	/* 2/7 Taps */

/* PCLK Configuration */
#define PCLK_DELAY_MASK		0x70
#define PCLK_DELAY_SHIFT	4	/* Pixel delay (-8..+7) */
#define PCLK_INV_SHIFT		2
#define PCLK_SEL_MASK		0x03	/* clock scaler */
#define PCLK_SEL_SHIFT		0
#define PCLK_SEL_X1		0L
#define PCLK_SEL_X2		1L
#define PCLK_SEL_DIV2		2L
#define PCLK_SEL_DIV4		3L

/* Pixel Repeater */
#define PIX_REPEAT_MASK_UP_SEL	0x30
#define PIX_REPEAT_MASK_REP	0x0f
#define PIX_REPEAT_SHIFT	4
#define PIX_REPEAT_CHROMA	1

/* Page 0x01 - HDMI info and packets */
#define REG_HDMI_FLAGS		0x0100
#define REG_DEEP_COLOR_MODE	0x0101
#define REG_AUDIO_FLAGS		0x0108
#define REG_AUDIO_FREQ		0x0109
#define REG_ACP_PACKET_TYPE	0x0141
#define REG_ISRC1_PACKET_TYPE	0x0161
#define REG_ISRC2_PACKET_TYPE	0x0181
#define REG_GBD_PACKET_TYPE	0x01a1

/* HDMI_FLAGS */
#define HDMI_FLAGS_AUDIO	BIT(7)	/* Audio packet in last videoframe */
#define HDMI_FLAGS_HDMI		BIT(6)	/* HDMI detected */
#define HDMI_FLAGS_EESS		BIT(5)	/* EESS detected */
#define HDMI_FLAGS_HDCP		BIT(4)	/* HDCP detected */
#define HDMI_FLAGS_AVMUTE	BIT(3)	/* AVMUTE */
#define HDMI_FLAGS_AUD_LAYOUT	BIT(2)	/* Layout status Audio sample packet */
#define HDMI_FLAGS_AUD_FIFO_OF	BIT(1)	/* FIFO read/write pointers crossed */
#define HDMI_FLAGS_AUD_FIFO_LOW	BIT(0)	/* FIFO read ptr within 2 of write */

/* Page 0x12 - HDMI Extra control and debug */
#define REG_CLK_CFG		0x1200
#define REG_CLK_OUT_CFG		0x1201
#define REG_CFG1		0x1202
#define REG_CFG2		0x1203
#define REG_WDL_CFG		0x1210
#define REG_DELOCK_DELAY	0x1212
#define REG_PON_OVR_EN		0x12A0
#define REG_PON_CBIAS		0x12A1
#define REG_PON_RESCAL		0x12A2
#define REG_PON_RES		0x12A3
#define REG_PON_CLK		0x12A4
#define REG_PON_PLL		0x12A5
#define REG_PON_EQ		0x12A6
#define REG_PON_DES		0x12A7
#define REG_PON_OUT		0x12A8
#define REG_PON_MUX		0x12A9
#define REG_MODE_REC_CFG1	0x12F8
#define REG_MODE_REC_CFG2	0x12F9
#define REG_MODE_REC_STS	0x12FA
#define REG_AUDIO_LAYOUT	0x12D0

#define PON_EN			1
#define PON_DIS			0

/* CLK CFG */
#define CLK_CFG_INV_OUT_CLK	BIT(7)
#define CLK_CFG_INV_BUS_CLK	BIT(6)
#define CLK_CFG_SEL_ACLK_EN	BIT(1)
#define CLK_CFG_SEL_ACLK	BIT(0)
#define CLK_CFG_DIS		0

/* Page 0x13 - HDMI Extra control and debug */
#define REG_DEEP_COLOR_CTRL	0x1300
#define REG_CGU_DBG_SEL		0x1305
#define REG_HDCP_DDC_ADDR	0x1310
#define REG_HDCP_KIDX		0x1316
#define REG_DEEP_PLL7_BYP	0x1347
#define REG_HDCP_DE_CTRL	0x1370
#define REG_HDCP_EP_FILT_CTRL	0x1371
#define REG_HDMI_CTRL		0x1377
#define REG_HMTP_CTRL		0x137a
#define REG_TIMER_D		0x13CF
#define REG_SUS_SET_RGB0	0x13E1
#define REG_SUS_SET_RGB1	0x13E2
#define REG_SUS_SET_RGB2	0x13E3
#define REG_SUS_SET_RGB3	0x13E4
#define REG_SUS_SET_RGB4	0x13E5
#define REG_MAN_SUS_HDMI_SEL	0x13E8
#define REG_MAN_HDMI_SET	0x13E9
#define REG_SUS_CLOCK_GOOD	0x13EF

/* HDCP DE Control */
#define HDCP_DE_MODE_MASK	0xc0	/* DE Measurement mode */
#define HDCP_DE_MODE_SHIFT	6
#define HDCP_DE_REGEN_EN	BIT(5)	/* enable regen mode */
#define HDCP_DE_FILTER_MASK	0x18	/* DE filter sensitivity */
#define HDCP_DE_FILTER_SHIFT	3
#define HDCP_DE_COMP_MASK	0x07	/* DE Composition mode */
#define HDCP_DE_COMP_MIXED	6L
#define HDCP_DE_COMP_OR		5L
#define HDCP_DE_COMP_AND	4L
#define HDCP_DE_COMP_CH3	3L
#define HDCP_DE_COMP_CH2	2L
#define HDCP_DE_COMP_CH1	1L
#define HDCP_DE_COMP_CH0	0L

/* HDCP EP Filter Control */
#define HDCP_EP_FIL_CTL_MASK	0x30
#define HDCP_EP_FIL_CTL_SHIFT	4
#define HDCP_EP_FIL_VS_MASK	0x0c
#define HDCP_EP_FIL_VS_SHIFT	2
#define HDCP_EP_FIL_HS_MASK	0x03
#define HDCP_EP_FIL_HS_SHIFT	0

/* HDMI_CTRL */
#define HDMI_CTRL_MUTE_MASK	0x0c
#define HDMI_CTRL_MUTE_SHIFT	2
#define HDMI_CTRL_MUTE_AUTO	0L
#define HDMI_CTRL_MUTE_OFF	1L
#define HDMI_CTRL_MUTE_ON	2L
#define HDMI_CTRL_HDCP_MASK	0x03
#define HDMI_CTRL_HDCP_SHIFT	0
#define HDMI_CTRL_HDCP_EESS	2L
#define HDMI_CTRL_HDCP_OESS	1L
#define HDMI_CTRL_HDCP_AUTO	0L

/* CGU_DBG_SEL bits */
#define CGU_DBG_CLK_SEL_MASK	0x18
#define CGU_DBG_CLK_SEL_SHIFT	3
#define CGU_DBG_XO_FRO_SEL	BIT(2)
#define CGU_DBG_VDP_CLK_SEL	BIT(1)
#define CGU_DBG_PIX_CLK_SEL	BIT(0)

/* REG_MAN_SUS_HDMI_SEL / REG_MAN_HDMI_SET bits */
#define MAN_DIS_OUT_BUF		BIT(7)
#define MAN_DIS_ANA_PATH	BIT(6)
#define MAN_DIS_HDCP		BIT(5)
#define MAN_DIS_TMDS_ENC	BIT(4)
#define MAN_DIS_TMDS_FLOW	BIT(3)
#define MAN_RST_HDCP		BIT(2)
#define MAN_RST_TMDS_ENC	BIT(1)
#define MAN_RST_TMDS_FLOW	BIT(0)

/* Page 0x14 - Audio Extra control and debug */
#define REG_FIFO_LATENCY_VAL	0x1403
#define REG_AUDIO_CLOCK		0x1411
#define REG_TEST_NCTS_CTRL	0x1415
#define REG_TEST_AUDIO_FREQ	0x1426
#define REG_TEST_MODE		0x1437

/* Audio Clock Configuration */
#define AUDIO_CLOCK_PLL_PD	BIT(7)	/* powerdown PLL */
#define AUDIO_CLOCK_SEL_MASK	0x7f
#define AUDIO_CLOCK_SEL_16FS	0L	/* 16*fs */
#define AUDIO_CLOCK_SEL_32FS	1L	/* 32*fs */
#define AUDIO_CLOCK_SEL_64FS	2L	/* 64*fs */
#define AUDIO_CLOCK_SEL_128FS	3L	/* 128*fs */
#define AUDIO_CLOCK_SEL_256FS	4L	/* 256*fs */
#define AUDIO_CLOCK_SEL_512FS	5L	/* 512*fs */

/* Page 0x20: EDID and Hotplug Detect */
#define REG_EDID_IN_BYTE0	0x2000 /* EDID base */
#define REG_EDID_IN_VERSION	0x2080
#define REG_EDID_ENABLE		0x2081
#define REG_HPD_POWER		0x2084
#define REG_HPD_AUTO_CTRL	0x2085
#define REG_HPD_DURATION	0x2086
#define REG_RX_HPD_HEAC		0x2087

/* EDID_ENABLE */
#define EDID_ENABLE_NACK_OFF	BIT(7)
#define EDID_ENABLE_EDID_ONLY	BIT(6)
#define EDID_ENABLE_B_EN	BIT(1)
#define EDID_ENABLE_A_EN	BIT(0)

/* HPD Power */
#define HPD_POWER_BP_MASK	0x0c
#define HPD_POWER_BP_SHIFT	2
#define HPD_POWER_BP_LOW	0L
#define HPD_POWER_BP_HIGH	1L
#define HPD_POWER_EDID_ONLY	BIT(1)

/* HPD Auto control */
#define HPD_AUTO_READ_EDID	BIT(7)
#define HPD_AUTO_HPD_F3TECH	BIT(5)
#define HPD_AUTO_HP_OTHER	BIT(4)
#define HPD_AUTO_HPD_UNSEL	BIT(3)
#define HPD_AUTO_HPD_ALL_CH	BIT(2)
#define HPD_AUTO_HPD_PRV_CH	BIT(1)
#define HPD_AUTO_HPD_NEW_CH	BIT(0)

/* Page 0x21 - EDID content */
#define REG_EDID_IN_BYTE128	0x2100 /* CEA Extension block */
#define REG_EDID_IN_SPA_SUB	0x2180
#define REG_EDID_IN_SPA_AB_A	0x2181
#define REG_EDID_IN_SPA_CD_A	0x2182
#define REG_EDID_IN_CKSUM_A	0x2183
#define REG_EDID_IN_SPA_AB_B	0x2184
#define REG_EDID_IN_SPA_CD_B	0x2185
#define REG_EDID_IN_CKSUM_B	0x2186

/* Page 0x30 - NV Configuration */
#define REG_RT_AUTO_CTRL	0x3000
#define REG_EQ_MAN_CTRL0	0x3001
#define REG_EQ_MAN_CTRL1	0x3002
#define REG_OUTPUT_CFG		0x3003
#define REG_MUTE_CTRL		0x3004
#define REG_SLAVE_ADDR		0x3005
#define REG_CMTP_REG6		0x3006
#define REG_CMTP_REG7		0x3007
#define REG_CMTP_REG8		0x3008
#define REG_CMTP_REG9		0x3009
#define REG_CMTP_REGA		0x300A
#define REG_CMTP_REGB		0x300B
#define REG_CMTP_REGC		0x300C
#define REG_CMTP_REGD		0x300D
#define REG_CMTP_REGE		0x300E
#define REG_CMTP_REGF		0x300F
#define REG_CMTP_REG10		0x3010
#define REG_CMTP_REG11		0x3011

/* Page 0x80 - CEC */
#define REG_PWR_CONTROL		0x80F4
#define REG_OSC_DIVIDER		0x80F5
#define REG_EN_OSC_PERIOD_LSB	0x80F8
#define REG_CONTROL		0x80FF

/* global interrupt flags (INT_FLG_CRL_TOP) */
#define INTERRUPT_AFE		BIT(7) /* AFE module */
#define INTERRUPT_HDCP		BIT(6) /* HDCP module */
#define INTERRUPT_AUDIO		BIT(5) /* Audio module */
#define INTERRUPT_INFO		BIT(4) /* Infoframe module */
#define INTERRUPT_MODE		BIT(3) /* HDMI mode module */
#define INTERRUPT_RATE		BIT(2) /* rate module */
#define INTERRUPT_DDC		BIT(1) /* DDC module */
#define INTERRUPT_SUS		BIT(0) /* SUS module */

/* INT_FLG_CLR_HDCP bits */
#define MASK_HDCP_MTP		BIT(7) /* HDCP MTP busy */
#define MASK_HDCP_DLMTP		BIT(4) /* HDCP end download MTP to SRAM */
#define MASK_HDCP_DLRAM		BIT(3) /* HDCP end download keys from SRAM */
#define MASK_HDCP_ENC		BIT(2) /* HDCP ENC */
#define MASK_STATE_C5		BIT(1) /* HDCP State C5 reached */
#define MASK_AKSV		BIT(0) /* AKSV received (start of auth) */

/* INT_FLG_CLR_RATE bits */
#define MASK_RATE_B_DRIFT	BIT(7) /* Rate measurement drifted */
#define MASK_RATE_B_ST		BIT(6) /* Rate measurement stability change */
#define MASK_RATE_B_ACT		BIT(5) /* Rate measurement activity change */
#define MASK_RATE_B_PST		BIT(4) /* Rate measreument presence change */
#define MASK_RATE_A_DRIFT	BIT(3) /* Rate measurement drifted */
#define MASK_RATE_A_ST		BIT(2) /* Rate measurement stability change */
#define MASK_RATE_A_ACT		BIT(1) /* Rate measurement presence change */
#define MASK_RATE_A_PST		BIT(0) /* Rate measreument presence change */

/* INT_FLG_CLR_SUS (Start Up Sequencer) bits */
#define MASK_MPT		BIT(7) /* Config MTP end of process */
#define MASK_FMT		BIT(5) /* Video format changed */
#define MASK_RT_PULSE		BIT(4) /* End of termination resistance pulse */
#define MASK_SUS_END		BIT(3) /* SUS last state reached */
#define MASK_SUS_ACT		BIT(2) /* Activity of selected input changed */
#define MASK_SUS_CH		BIT(1) /* Selected input changed */
#define MASK_SUS_ST		BIT(0) /* SUS state changed */

/* INT_FLG_CLR_DDC bits */
#define MASK_EDID_MTP		BIT(7) /* EDID MTP end of process */
#define MASK_DDC_ERR		BIT(6) /* master DDC error */
#define MASK_DDC_CMD_DONE	BIT(5) /* master DDC cmd send correct */
#define MASK_READ_DONE		BIT(4) /* End of down EDID read */
#define MASK_RX_DDC_SW		BIT(3) /* Output DDC switching finished */
#define MASK_HDCP_DDC_SW	BIT(2) /* HDCP DDC switching finished */
#define MASK_HDP_PULSE_END	BIT(1) /* End of Hot Plug Detect pulse */
#define MASK_DET_5V		BIT(0) /* Detection of +5V */

/* INT_FLG_CLR_MODE bits */
#define MASK_HDMI_FLG		BIT(7) /* HDMI mode/avmute/encrypt/FIFO fail */
#define MASK_GAMUT		BIT(6) /* Gamut packet */
#define MASK_ISRC2		BIT(5) /* ISRC2 packet */
#define MASK_ISRC1		BIT(4) /* ISRC1 packet */
#define MASK_ACP		BIT(3) /* Audio Content Protection packet */
#define MASK_DC_NO_GCP		BIT(2) /* GCP not received in 5 frames */
#define MASK_DC_PHASE		BIT(1) /* deepcolor pixel phase needs update */
#define MASK_DC_MODE		BIT(0) /* deepcolor color depth changed */

/* INT_FLG_CLR_INFO bits (Infoframe Change Status) */
#define MASK_MPS_IF		BIT(6) /* MPEG Source Product */
#define MASK_AUD_IF		BIT(5) /* Audio */
#define MASK_SPD_IF		BIT(4) /* Source Product Descriptor */
#define MASK_AVI_IF		BIT(3) /* Auxiliary Video IF */
#define MASK_VS_IF_OTHER_BK2	BIT(2) /* Vendor Specific (bank2) */
#define MASK_VS_IF_OTHER_BK1	BIT(1) /* Vendor Specific (bank1) */
#define MASK_VS_IF_HDMI		BIT(0) /* Vendor Specific (w/ HDMI LLC code) */

/* INT_FLG_CLR_AUDIO bits */
#define MASK_AUDIO_FREQ_FLG	BIT(5) /* Audio freq change */
#define MASK_AUDIO_FLG		BIT(4) /* DST, OBA, HBR, ASP change */
#define MASK_MUTE_FLG		BIT(3) /* Audio Mute */
#define MASK_CH_STATE		BIT(2) /* Channel status */
#define MASK_UNMUTE_FIFO	BIT(1) /* Audio Unmute */
#define MASK_ERROR_FIFO_PT	BIT(0) /* Audio FIFO pointer error */

/* INT_FLG_CLR_AFE bits */
#define MASK_AFE_WDL_UNLOCKED	BIT(7) /* Wordlocker was unlocked */
#define MASK_AFE_GAIN_DONE	BIT(6) /* Gain calibration done */
#define MASK_AFE_OFFSET_DONE	BIT(5) /* Offset calibration done */
#define MASK_AFE_ACTIVITY_DET	BIT(4) /* Activity detected on data */
#define MASK_AFE_PLL_LOCK	BIT(3) /* TMDS PLL is locked */
#define MASK_AFE_TRMCAL_DONE	BIT(2) /* Termination calibration done */
#define MASK_AFE_ASU_STATE	BIT(1) /* ASU state is reached */
#define MASK_AFE_ASU_READY	BIT(0) /* AFE calibration done: TMDS ready */

/* Audio Output */
#define AUDCFG_CLK_INVERT	BIT(7)	/* invert A_CLK polarity */
#define AUDCFG_TEST_TONE	BIT(6)	/* enable test tone generator */
#define AUDCFG_BUS_SHIFT	5
#define AUDCFG_BUS_I2S		0L
#define AUDCFG_BUS_SPDIF	1L
#define AUDCFG_I2SW_SHIFT	4
#define AUDCFG_I2SW_16		0L
#define AUDCFG_I2SW_32		1L
#define AUDCFG_AUTO_MUTE_EN	BIT(3)	/* Enable Automatic audio mute */
#define AUDCFG_HBR_SHIFT	2
#define AUDCFG_HBR_STRAIGHT	0L	/* straight via AP0 */
#define AUDCFG_HBR_DEMUX	1L	/* demuxed via AP0:AP3 */
#define AUDCFG_TYPE_MASK	0x03
#define AUDCFG_TYPE_SHIFT	0
#define AUDCFG_TYPE_DST		3L	/* Direct Stream Transfer (DST) */
#define AUDCFG_TYPE_OBA		2L	/* One Bit Audio (OBA) */
#define AUDCFG_TYPE_HBR		1L	/* High Bit Rate (HBR) */
#define AUDCFG_TYPE_PCM		0L	/* Audio samples */

/* Video Formatter */
#define OF_VP_ENABLE		BIT(7)	/* VP[35:0]/HS/VS/DE/CLK */
#define OF_BLK			BIT(4)	/* blanking codes */
#define OF_TRC			BIT(3)	/* timing codes (SAV/EAV) */
#define OF_FMT_MASK		0x3
#define OF_FMT_444		0L	/* RGB444/YUV444 */
#define OF_FMT_422_SMPT		1L	/* YUV422 semi-planar */
#define OF_FMT_422_CCIR		2L	/* YUV422 CCIR656 */

/* HS/HREF output control */
#define HS_HREF_DELAY_MASK	0xf0
#define HS_HREF_DELAY_SHIFT	4	/* Pixel delay (-8..+7) */
#define HS_HREF_PXQ_SHIFT	3	/* Timing codes from HREF */
#define HS_HREF_INV_SHIFT	2	/* polarity (1=invert) */
#define HS_HREF_SEL_MASK	0x03
#define HS_HREF_SEL_SHIFT	0
#define HS_HREF_SEL_HS_VHREF	0L	/* HS from VHREF */
#define HS_HREF_SEL_HREF_VHREF	1L	/* HREF from VHREF */
#define HS_HREF_SEL_HREF_HDMI	2L	/* HREF from HDMI */
#define HS_HREF_SEL_NONE	3L	/* not generated */

/* VS output control */
#define VS_VREF_DELAY_MASK	0xf0
#define VS_VREF_DELAY_SHIFT	4	/* Pixel delay (-8..+7) */
#define VS_VREF_INV_SHIFT	2	/* polarity (1=invert) */
#define VS_VREF_SEL_MASK	0x03
#define VS_VREF_SEL_SHIFT	0
#define VS_VREF_SEL_VS_VHREF	0L	/* VS from VHREF */
#define VS_VREF_SEL_VREF_VHREF	1L	/* VREF from VHREF */
#define VS_VREF_SEL_VREF_HDMI	2L	/* VREF from HDMI */
#define VS_VREF_SEL_NONE	3L	/* not generated */

/* DE/FREF output control */
#define DE_FREF_DELAY_MASK	0xf0
#define DE_FREF_DELAY_SHIFT	4	/* Pixel delay (-8..+7) */
#define DE_FREF_DE_PXQ_SHIFT	3	/* Timing codes from DE */
#define DE_FREF_INV_SHIFT	2	/* polarity (1=invert) */
#define DE_FREF_SEL_MASK	0x03
#define DE_FREF_SEL_SHIFT	0
#define DE_FREF_SEL_DE_VHREF	0L	/* DE from VHREF (HREF and not(VREF) */
#define DE_FREF_SEL_FREF_VHREF	1L	/* FREF from VHREF */
#define DE_FREF_SEL_FREF_HDMI	2L	/* FREF from HDMI */
#define DE_FREF_SEL_NONE	3L	/* not generated */

/* HDMI_SOFT_RST bits */
#define RESET_DC		BIT(7)	/* Reset deep color module */
#define RESET_HDCP		BIT(6)	/* Reset HDCP module */
#define RESET_KSV		BIT(5)	/* Reset KSV-FIFO */
#define RESET_SCFG		BIT(4)	/* Reset HDCP and repeater function */
#define RESET_HCFG		BIT(3)	/* Reset HDCP DDC part */
#define RESET_PA		BIT(2)	/* Reset polarity adjust */
#define RESET_EP		BIT(1)	/* Reset Error protection */
#define RESET_TMDS		BIT(0)	/* Reset TMDS (calib, encoding, flow) */

/* HDMI_INFO_RST bits */
#define NACK_HDCP		BIT(7)	/* No ACK on HDCP request */
#define RESET_FIFO		BIT(4)	/* Reset Audio FIFO control */
#define RESET_GAMUT		BIT(3)	/* Clear Gamut packet */
#define RESET_AI		BIT(2)	/* Clear ACP and ISRC packets */
#define RESET_IF		BIT(1)	/* Clear all Audio infoframe packets */
#define RESET_AUDIO		BIT(0)	/* Reset Audio FIFO control */

/* HDCP_BCAPS bits */
#define HDCP_HDMI		BIT(7)	/* HDCP supports HDMI (vs DVI only) */
#define HDCP_REPEATER		BIT(6)	/* HDCP supports repeater function */
#define HDCP_READY		BIT(5)	/* set by repeater function */
#define HDCP_FAST		BIT(4)	/* Up to 400kHz */
#define HDCP_11			BIT(1)	/* HDCP 1.1 supported */
#define HDCP_FAST_REAUTH	BIT(0)	/* fast reauthentication supported */

/* Audio output formatter */
#define AUDIO_LAYOUT_SP_FLAG	BIT(2)	/* sp flag used by FIFO */
#define AUDIO_LAYOUT_MANUAL	BIT(1)	/* manual layout (vs per pkt) */
#define AUDIO_LAYOUT_LAYOUT1	BIT(0)  /* Layout1: AP0-3 vs Layout0:AP0 */

/* masks for interrupt status registers */
#define MASK_SUS_STATUS		0x1F
#define LAST_STATE_REACHED	0x1B
#define MASK_CLK_STABLE		0x04
#define MASK_CLK_ACTIVE		0x02
#define MASK_SUS_STATE		0x10
#define MASK_SR_FIFO_FIFO_CTRL	0x30
#define MASK_AUDIO_FLAG		0x10

/* Rate measurement */
#define RATE_REFTIM_ENABLE	0x01
#define CLK_MIN_RATE		0x0057e4
#define CLK_MAX_RATE		0x0395f8
#define WDL_CFG_VAL		0x82
#define DC_FILTER_VAL		0x31

/* Infoframe */
#define VS_HDMI_IF_UPDATE	0x0200
#define VS_HDMI_IF		0x0201
#define VS_BK1_IF_UPDATE	0x0220
#define VS_BK1_IF		0x0221
#define VS_BK2_IF_UPDATE	0x0240
#define VS_BK2_IF		0x0241
#define AVI_IF_UPDATE		0x0260
#define AVI_IF			0x0261
#define SPD_IF_UPDATE		0x0280
#define SPD_IF			0x0281
#define AUD_IF_UPDATE		0x02a0
#define AUD_IF			0x02a1
#define MPS_IF_UPDATE		0x02c0
#define MPS_IF			0x02c1
