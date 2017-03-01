/*
 * Registers of Silicon Image SiI8620 Mobile HD Transmitter
 *
 * Copyright (C) 2015, Samsung Electronics Co., Ltd.
 * Andrzej Hajda <a.hajda@samsung.com>
 *
 * Based on MHL driver for Android devices.
 * Copyright (C) 2013-2014 Silicon Image, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __SIL_SII8620_H__
#define __SIL_SII8620_H__

/* Vendor ID Low byte, default value: 0x01 */
#define REG_VND_IDL				0x0000

/* Vendor ID High byte, default value: 0x00 */
#define REG_VND_IDH				0x0001

/* Device ID Low byte, default value: 0x60 */
#define REG_DEV_IDL				0x0002

/* Device ID High byte, default value: 0x86 */
#define REG_DEV_IDH				0x0003

/* Device Revision, default value: 0x10 */
#define REG_DEV_REV				0x0004

/* OTP DBYTE510, default value: 0x00 */
#define REG_OTP_DBYTE510			0x0006

/* System Control #1, default value: 0x00 */
#define REG_SYS_CTRL1				0x0008
#define BIT_SYS_CTRL1_OTPVMUTEOVR_SET		BIT(7)
#define BIT_SYS_CTRL1_VSYNCPIN			BIT(6)
#define BIT_SYS_CTRL1_OTPADROPOVR_SET		BIT(5)
#define BIT_SYS_CTRL1_BLOCK_DDC_BY_HPD		BIT(4)
#define BIT_SYS_CTRL1_OTP2XVOVR_EN		BIT(3)
#define BIT_SYS_CTRL1_OTP2XAOVR_EN		BIT(2)
#define BIT_SYS_CTRL1_TX_CTRL_HDMI		BIT(1)
#define BIT_SYS_CTRL1_OTPAMUTEOVR_SET		BIT(0)

/* System Control DPD, default value: 0x90 */
#define REG_DPD					0x000b
#define BIT_DPD_PWRON_PLL			BIT(7)
#define BIT_DPD_PDNTX12				BIT(6)
#define BIT_DPD_PDNRX12				BIT(5)
#define BIT_DPD_OSC_EN				BIT(4)
#define BIT_DPD_PWRON_HSIC			BIT(3)
#define BIT_DPD_PDIDCK_N			BIT(2)
#define BIT_DPD_PD_MHL_CLK_N			BIT(1)

/* Dual link Control, default value: 0x00 */
#define REG_DCTL				0x000d
#define BIT_DCTL_TDM_LCLK_PHASE			BIT(7)
#define BIT_DCTL_HSIC_CLK_PHASE			BIT(6)
#define BIT_DCTL_CTS_TCK_PHASE			BIT(5)
#define BIT_DCTL_EXT_DDC_SEL			BIT(4)
#define BIT_DCTL_TRANSCODE			BIT(3)
#define BIT_DCTL_HSIC_RX_STROBE_PHASE		BIT(2)
#define BIT_DCTL_HSIC_TX_BIST_START_SEL		BIT(1)
#define BIT_DCTL_TCLKNX_PHASE			BIT(0)

/* PWD Software Reset, default value: 0x20 */
#define REG_PWD_SRST				0x000e
#define BIT_PWD_SRST_COC_DOC_RST		BIT(7)
#define BIT_PWD_SRST_CBUS_RST_SW		BIT(6)
#define BIT_PWD_SRST_CBUS_RST_SW_EN		BIT(5)
#define BIT_PWD_SRST_MHLFIFO_RST		BIT(4)
#define BIT_PWD_SRST_CBUS_RST			BIT(3)
#define BIT_PWD_SRST_SW_RST_AUTO		BIT(2)
#define BIT_PWD_SRST_HDCP2X_SW_RST		BIT(1)
#define BIT_PWD_SRST_SW_RST			BIT(0)

/* AKSV_1, default value: 0x00 */
#define REG_AKSV_1				0x001d

/* Video H Resolution #1, default value: 0x00 */
#define REG_H_RESL				0x003a

/* Video Mode, default value: 0x00 */
#define REG_VID_MODE				0x004a
#define BIT_VID_MODE_M1080P			BIT(6)

/* Video Input Mode, default value: 0xc0 */
#define REG_VID_OVRRD				0x0051
#define BIT_VID_OVRRD_PP_AUTO_DISABLE		BIT(7)
#define BIT_VID_OVRRD_M1080P_OVRRD		BIT(6)
#define BIT_VID_OVRRD_MINIVSYNC_ON		BIT(5)
#define BIT_VID_OVRRD_3DCONV_EN_FRAME_PACK	BIT(4)
#define BIT_VID_OVRRD_ENABLE_AUTO_PATH_EN	BIT(3)
#define BIT_VID_OVRRD_ENRGB2YCBCR_OVRRD		BIT(2)
#define BIT_VID_OVRRD_ENDOWNSAMPLE_OVRRD	BIT(0)

/* I2C Address reassignment, default value: 0x00 */
#define REG_PAGE_MHLSPEC_ADDR			0x0057
#define REG_PAGE7_ADDR				0x0058
#define REG_PAGE8_ADDR				0x005c

/* Fast Interrupt Status, default value: 0x00 */
#define REG_FAST_INTR_STAT			0x005f
#define LEN_FAST_INTR_STAT			7
#define BIT_FAST_INTR_STAT_TIMR			8
#define BIT_FAST_INTR_STAT_INT2			9
#define BIT_FAST_INTR_STAT_DDC			10
#define BIT_FAST_INTR_STAT_SCDT			11
#define BIT_FAST_INTR_STAT_INFR			13
#define BIT_FAST_INTR_STAT_EDID			14
#define BIT_FAST_INTR_STAT_HDCP			15
#define BIT_FAST_INTR_STAT_MSC			16
#define BIT_FAST_INTR_STAT_MERR			17
#define BIT_FAST_INTR_STAT_G2WB			18
#define BIT_FAST_INTR_STAT_G2WB_ERR		19
#define BIT_FAST_INTR_STAT_DISC			28
#define BIT_FAST_INTR_STAT_BLOCK		30
#define BIT_FAST_INTR_STAT_LTRN			31
#define BIT_FAST_INTR_STAT_HDCP2		32
#define BIT_FAST_INTR_STAT_TDM			42
#define BIT_FAST_INTR_STAT_COC			51

/* GPIO Control, default value: 0x15 */
#define REG_GPIO_CTRL1				0x006e
#define BIT_CTRL1_GPIO_I_8			BIT(5)
#define BIT_CTRL1_GPIO_OEN_8			BIT(4)
#define BIT_CTRL1_GPIO_I_7			BIT(3)
#define BIT_CTRL1_GPIO_OEN_7			BIT(2)
#define BIT_CTRL1_GPIO_I_6			BIT(1)
#define BIT_CTRL1_GPIO_OEN_6			BIT(0)

/* Interrupt Control, default value: 0x06 */
#define REG_INT_CTRL				0x006f
#define BIT_INT_CTRL_SOFTWARE_WP		BIT(7)
#define BIT_INT_CTRL_INTR_OD			BIT(2)
#define BIT_INT_CTRL_INTR_POLARITY		BIT(1)

/* Interrupt State, default value: 0x00 */
#define REG_INTR_STATE				0x0070
#define BIT_INTR_STATE_INTR_STATE		BIT(0)

/* Interrupt Source #1, default value: 0x00 */
#define REG_INTR1				0x0071

/* Interrupt Source #2, default value: 0x00 */
#define REG_INTR2				0x0072

/* Interrupt Source #3, default value: 0x01 */
#define REG_INTR3				0x0073
#define BIT_DDC_CMD_DONE			BIT(3)

/* Interrupt Source #5, default value: 0x00 */
#define REG_INTR5				0x0074

/* Interrupt #1 Mask, default value: 0x00 */
#define REG_INTR1_MASK				0x0075

/* Interrupt #2 Mask, default value: 0x00 */
#define REG_INTR2_MASK				0x0076

/* Interrupt #3 Mask, default value: 0x00 */
#define REG_INTR3_MASK				0x0077

/* Interrupt #5 Mask, default value: 0x00 */
#define REG_INTR5_MASK				0x0078
#define BIT_INTR_SCDT_CHANGE			BIT(0)

/* Hot Plug Connection Control, default value: 0x45 */
#define REG_HPD_CTRL				0x0079
#define BIT_HPD_CTRL_HPD_DS_SIGNAL		BIT(7)
#define BIT_HPD_CTRL_HPD_OUT_OD_EN		BIT(6)
#define BIT_HPD_CTRL_HPD_HIGH			BIT(5)
#define BIT_HPD_CTRL_HPD_OUT_OVR_EN		BIT(4)
#define BIT_HPD_CTRL_GPIO_I_1			BIT(3)
#define BIT_HPD_CTRL_GPIO_OEN_1			BIT(2)
#define BIT_HPD_CTRL_GPIO_I_0			BIT(1)
#define BIT_HPD_CTRL_GPIO_OEN_0			BIT(0)

/* GPIO Control, default value: 0x55 */
#define REG_GPIO_CTRL				0x007a
#define BIT_CTRL_GPIO_I_5			BIT(7)
#define BIT_CTRL_GPIO_OEN_5			BIT(6)
#define BIT_CTRL_GPIO_I_4			BIT(5)
#define BIT_CTRL_GPIO_OEN_4			BIT(4)
#define BIT_CTRL_GPIO_I_3			BIT(3)
#define BIT_CTRL_GPIO_OEN_3			BIT(2)
#define BIT_CTRL_GPIO_I_2			BIT(1)
#define BIT_CTRL_GPIO_OEN_2			BIT(0)

/* Interrupt Source 7, default value: 0x00 */
#define REG_INTR7				0x007b

/* Interrupt Source 8, default value: 0x00 */
#define REG_INTR8				0x007c

/* Interrupt #7 Mask, default value: 0x00 */
#define REG_INTR7_MASK				0x007d

/* Interrupt #8 Mask, default value: 0x00 */
#define REG_INTR8_MASK				0x007e
#define BIT_CEA_NEW_VSI				BIT(2)
#define BIT_CEA_NEW_AVI				BIT(1)

/* IEEE, default value: 0x10 */
#define REG_TMDS_CCTRL				0x0080
#define BIT_TMDS_CCTRL_TMDS_OE			BIT(4)

/* TMDS Control #4, default value: 0x02 */
#define REG_TMDS_CTRL4				0x0085
#define BIT_TMDS_CTRL4_SCDT_CKDT_SEL		BIT(1)
#define BIT_TMDS_CTRL4_TX_EN_BY_SCDT		BIT(0)

/* BIST CNTL, default value: 0x00 */
#define REG_BIST_CTRL				0x00bb
#define BIT_RXBIST_VGB_EN			BIT(7)
#define BIT_TXBIST_VGB_EN			BIT(6)
#define BIT_BIST_START_SEL			BIT(5)
#define BIT_BIST_START_BIT			BIT(4)
#define BIT_BIST_ALWAYS_ON			BIT(3)
#define BIT_BIST_TRANS				BIT(2)
#define BIT_BIST_RESET				BIT(1)
#define BIT_BIST_EN				BIT(0)

/* BIST DURATION0, default value: 0x00 */
#define REG_BIST_TEST_SEL			0x00bd
#define MSK_BIST_TEST_SEL_BIST_PATT_SEL		0x0f

/* BIST VIDEO_MODE, default value: 0x00 */
#define REG_BIST_VIDEO_MODE			0x00be
#define MSK_BIST_VIDEO_MODE_BIST_VIDEO_MODE_3_0	0x0f

/* BIST DURATION0, default value: 0x00 */
#define REG_BIST_DURATION_0			0x00bf

/* BIST DURATION1, default value: 0x00 */
#define REG_BIST_DURATION_1			0x00c0

/* BIST DURATION2, default value: 0x00 */
#define REG_BIST_DURATION_2			0x00c1

/* BIST 8BIT_PATTERN, default value: 0x00 */
#define REG_BIST_8BIT_PATTERN			0x00c2

/* LM DDC, default value: 0x80 */
#define REG_LM_DDC				0x00c7
#define BIT_LM_DDC_SW_TPI_EN_DISABLED		BIT(7)

#define BIT_LM_DDC_VIDEO_MUTE_EN		BIT(5)
#define BIT_LM_DDC_DDC_TPI_SW			BIT(2)
#define BIT_LM_DDC_DDC_GRANT			BIT(1)
#define BIT_LM_DDC_DDC_GPU_REQUEST		BIT(0)

/* DDC I2C Manual, default value: 0x03 */
#define REG_DDC_MANUAL				0x00ec
#define BIT_DDC_MANUAL_MAN_DDC			BIT(7)
#define BIT_DDC_MANUAL_VP_SEL			BIT(6)
#define BIT_DDC_MANUAL_DSDA			BIT(5)
#define BIT_DDC_MANUAL_DSCL			BIT(4)
#define BIT_DDC_MANUAL_GCP_HW_CTL_EN		BIT(3)
#define BIT_DDC_MANUAL_DDCM_ABORT_WP		BIT(2)
#define BIT_DDC_MANUAL_IO_DSDA			BIT(1)
#define BIT_DDC_MANUAL_IO_DSCL			BIT(0)

/* DDC I2C Target Slave Address, default value: 0x00 */
#define REG_DDC_ADDR				0x00ed
#define MSK_DDC_ADDR_DDC_ADDR			0xfe

/* DDC I2C Target Segment Address, default value: 0x00 */
#define REG_DDC_SEGM				0x00ee

/* DDC I2C Target Offset Address, default value: 0x00 */
#define REG_DDC_OFFSET				0x00ef

/* DDC I2C Data In count #1, default value: 0x00 */
#define REG_DDC_DIN_CNT1			0x00f0

/* DDC I2C Data In count #2, default value: 0x00 */
#define REG_DDC_DIN_CNT2			0x00f1
#define MSK_DDC_DIN_CNT2_DDC_DIN_CNT_9_8	0x03

/* DDC I2C Status, default value: 0x04 */
#define REG_DDC_STATUS				0x00f2
#define BIT_DDC_STATUS_DDC_BUS_LOW		BIT(6)
#define BIT_DDC_STATUS_DDC_NO_ACK		BIT(5)
#define BIT_DDC_STATUS_DDC_I2C_IN_PROG		BIT(4)
#define BIT_DDC_STATUS_DDC_FIFO_FULL		BIT(3)
#define BIT_DDC_STATUS_DDC_FIFO_EMPTY		BIT(2)
#define BIT_DDC_STATUS_DDC_FIFO_READ_IN_SUE	BIT(1)
#define BIT_DDC_STATUS_DDC_FIFO_WRITE_IN_USE	BIT(0)

/* DDC I2C Command, default value: 0x70 */
#define REG_DDC_CMD				0x00f3
#define BIT_DDC_CMD_HDCP_DDC_EN			BIT(6)
#define BIT_DDC_CMD_SDA_DEL_EN			BIT(5)
#define BIT_DDC_CMD_DDC_FLT_EN			BIT(4)

#define MSK_DDC_CMD_DDC_CMD			0x0f
#define VAL_DDC_CMD_ENH_DDC_READ_NO_ACK		0x04
#define VAL_DDC_CMD_DDC_CMD_CLEAR_FIFO		0x09
#define VAL_DDC_CMD_DDC_CMD_ABORT		0x0f

/* DDC I2C FIFO Data In/Out, default value: 0x00 */
#define REG_DDC_DATA				0x00f4

/* DDC I2C Data Out Counter, default value: 0x00 */
#define REG_DDC_DOUT_CNT			0x00f5
#define BIT_DDC_DOUT_CNT_DDC_DELAY_CNT_8	BIT(7)
#define MSK_DDC_DOUT_CNT_DDC_DATA_OUT_CNT	0x1f

/* DDC I2C Delay Count, default value: 0x14 */
#define REG_DDC_DELAY_CNT			0x00f6

/* Test Control, default value: 0x80 */
#define REG_TEST_TXCTRL				0x00f7
#define BIT_TEST_TXCTRL_RCLK_REF_SEL		BIT(7)
#define BIT_TEST_TXCTRL_PCLK_REF_SEL		BIT(6)
#define MSK_TEST_TXCTRL_BYPASS_PLL_CLK		0x3c
#define BIT_TEST_TXCTRL_HDMI_MODE		BIT(1)
#define BIT_TEST_TXCTRL_TST_PLLCK		BIT(0)

/* CBUS Address, default value: 0x00 */
#define REG_PAGE_CBUS_ADDR			0x00f8

/* I2C Device Address re-assignment */
#define REG_PAGE1_ADDR				0x00fc
#define REG_PAGE2_ADDR				0x00fd
#define REG_PAGE3_ADDR				0x00fe
#define REG_HW_TPI_ADDR				0x00ff

/* USBT CTRL0, default value: 0x00 */
#define REG_UTSRST				0x0100
#define BIT_UTSRST_FC_SRST			BIT(5)
#define BIT_UTSRST_KEEPER_SRST			BIT(4)
#define BIT_UTSRST_HTX_SRST			BIT(3)
#define BIT_UTSRST_TRX_SRST			BIT(2)
#define BIT_UTSRST_TTX_SRST			BIT(1)
#define BIT_UTSRST_HRX_SRST			BIT(0)

/* HSIC RX Control3, default value: 0x07 */
#define REG_HRXCTRL3				0x0104
#define MSK_HRXCTRL3_HRX_AFFCTRL		0xf0
#define BIT_HRXCTRL3_HRX_OUT_EN			BIT(2)
#define BIT_HRXCTRL3_STATUS_EN			BIT(1)
#define BIT_HRXCTRL3_HRX_STAY_RESET		BIT(0)

/* HSIC RX INT Registers */
#define REG_HRXINTL				0x0111
#define REG_HRXINTH				0x0112

/* TDM TX NUMBITS, default value: 0x0c */
#define REG_TTXNUMB				0x0116
#define MSK_TTXNUMB_TTX_AFFCTRL_3_0		0xf0
#define BIT_TTXNUMB_TTX_COM1_AT_SYNC_WAIT	BIT(3)
#define MSK_TTXNUMB_TTX_NUMBPS			0x07

/* TDM TX NUMSPISYM, default value: 0x04 */
#define REG_TTXSPINUMS				0x0117

/* TDM TX NUMHSICSYM, default value: 0x14 */
#define REG_TTXHSICNUMS				0x0118

/* TDM TX NUMTOTSYM, default value: 0x18 */
#define REG_TTXTOTNUMS				0x0119

/* TDM TX INT Low, default value: 0x00 */
#define REG_TTXINTL				0x0136
#define BIT_TTXINTL_TTX_INTR7			BIT(7)
#define BIT_TTXINTL_TTX_INTR6			BIT(6)
#define BIT_TTXINTL_TTX_INTR5			BIT(5)
#define BIT_TTXINTL_TTX_INTR4			BIT(4)
#define BIT_TTXINTL_TTX_INTR3			BIT(3)
#define BIT_TTXINTL_TTX_INTR2			BIT(2)
#define BIT_TTXINTL_TTX_INTR1			BIT(1)
#define BIT_TTXINTL_TTX_INTR0			BIT(0)

/* TDM TX INT High, default value: 0x00 */
#define REG_TTXINTH				0x0137
#define BIT_TTXINTH_TTX_INTR15			BIT(7)
#define BIT_TTXINTH_TTX_INTR14			BIT(6)
#define BIT_TTXINTH_TTX_INTR13			BIT(5)
#define BIT_TTXINTH_TTX_INTR12			BIT(4)
#define BIT_TTXINTH_TTX_INTR11			BIT(3)
#define BIT_TTXINTH_TTX_INTR10			BIT(2)
#define BIT_TTXINTH_TTX_INTR9			BIT(1)
#define BIT_TTXINTH_TTX_INTR8			BIT(0)

/* TDM RX Control, default value: 0x1c */
#define REG_TRXCTRL				0x013b
#define BIT_TRXCTRL_TRX_CLR_WVALLOW		BIT(4)
#define BIT_TRXCTRL_TRX_FROM_SE_COC		BIT(3)
#define MSK_TRXCTRL_TRX_NUMBPS_2_0		0x07

/* TDM RX NUMSPISYM, default value: 0x04 */
#define REG_TRXSPINUMS				0x013c

/* TDM RX NUMHSICSYM, default value: 0x14 */
#define REG_TRXHSICNUMS				0x013d

/* TDM RX NUMTOTSYM, default value: 0x18 */
#define REG_TRXTOTNUMS				0x013e

/* TDM RX Status 2nd, default value: 0x00 */
#define REG_TRXSTA2				0x015c
#define MSK_TDM_SYNCHRONIZED			0xc0
#define VAL_TDM_SYNCHRONIZED			0x80

/* TDM RX INT Low, default value: 0x00 */
#define REG_TRXINTL				0x0163

/* TDM RX INT High, default value: 0x00 */
#define REG_TRXINTH				0x0164
#define BIT_TDM_INTR_SYNC_DATA			BIT(0)
#define BIT_TDM_INTR_SYNC_WAIT			BIT(1)

/* TDM RX INTMASK High, default value: 0x00 */
#define REG_TRXINTMH				0x0166

/* HSIC TX CRTL, default value: 0x00 */
#define REG_HTXCTRL				0x0169
#define BIT_HTXCTRL_HTX_ALLSBE_SOP		BIT(4)
#define BIT_HTXCTRL_HTX_RGDINV_USB		BIT(3)
#define BIT_HTXCTRL_HTX_RSPTDM_BUSY		BIT(2)
#define BIT_HTXCTRL_HTX_DRVCONN1		BIT(1)
#define BIT_HTXCTRL_HTX_DRVRST1			BIT(0)

/* HSIC TX INT Low, default value: 0x00 */
#define REG_HTXINTL				0x017d

/* HSIC TX INT High, default value: 0x00 */
#define REG_HTXINTH				0x017e

/* HSIC Keeper, default value: 0x00 */
#define REG_KEEPER				0x0181
#define MSK_KEEPER_MODE				0x03
#define VAL_KEEPER_MODE_HOST			0
#define VAL_KEEPER_MODE_DEVICE			2

/* HSIC Flow Control General, default value: 0x02 */
#define REG_FCGC				0x0183
#define BIT_FCGC_HSIC_HOSTMODE			BIT(1)
#define BIT_FCGC_HSIC_ENABLE			BIT(0)

/* HSIC Flow Control CTR13, default value: 0xfc */
#define REG_FCCTR13				0x0191

/* HSIC Flow Control CTR14, default value: 0xff */
#define REG_FCCTR14				0x0192

/* HSIC Flow Control CTR15, default value: 0xff */
#define REG_FCCTR15				0x0193

/* HSIC Flow Control CTR50, default value: 0x03 */
#define REG_FCCTR50				0x01b6

/* HSIC Flow Control INTR0, default value: 0x00 */
#define REG_FCINTR0				0x01ec
#define REG_FCINTR1				0x01ed
#define REG_FCINTR2				0x01ee
#define REG_FCINTR3				0x01ef
#define REG_FCINTR4				0x01f0
#define REG_FCINTR5				0x01f1
#define REG_FCINTR6				0x01f2
#define REG_FCINTR7				0x01f3

/* TDM Low Latency, default value: 0x20 */
#define REG_TDMLLCTL				0x01fc
#define MSK_TDMLLCTL_TRX_LL_SEL_MANUAL		0xc0
#define MSK_TDMLLCTL_TRX_LL_SEL_MODE		0x30
#define MSK_TDMLLCTL_TTX_LL_SEL_MANUAL		0x0c
#define BIT_TDMLLCTL_TTX_LL_TIE_LOW		BIT(1)
#define BIT_TDMLLCTL_TTX_LL_SEL_MODE		BIT(0)

/* TMDS 0 Clock Control, default value: 0x10 */
#define REG_TMDS0_CCTRL1			0x0210
#define MSK_TMDS0_CCTRL1_TEST_SEL		0xc0
#define MSK_TMDS0_CCTRL1_CLK1X_CTL		0x30

/* TMDS Clock Enable, default value: 0x00 */
#define REG_TMDS_CLK_EN				0x0211
#define BIT_TMDS_CLK_EN_CLK_EN			BIT(0)

/* TMDS Channel Enable, default value: 0x00 */
#define REG_TMDS_CH_EN				0x0212
#define BIT_TMDS_CH_EN_CH0_EN			BIT(4)
#define BIT_TMDS_CH_EN_CH12_EN			BIT(0)

/* BGR_BIAS, default value: 0x07 */
#define REG_BGR_BIAS				0x0215
#define BIT_BGR_BIAS_BGR_EN			BIT(7)
#define MSK_BGR_BIAS_BIAS_BGR_D			0x0f

/* TMDS 0 Digital I2C BW, default value: 0x0a */
#define REG_ALICE0_BW_I2C			0x0231

/* TMDS 0 Digital Zone Control, default value: 0xe0 */
#define REG_ALICE0_ZONE_CTRL			0x024c
#define BIT_ALICE0_ZONE_CTRL_ICRST_N		BIT(7)
#define BIT_ALICE0_ZONE_CTRL_USE_INT_DIV20	BIT(6)
#define MSK_ALICE0_ZONE_CTRL_SZONE_I2C		0x30
#define MSK_ALICE0_ZONE_CTRL_ZONE_CTRL		0x0f

/* TMDS 0 Digital PLL Mode Control, default value: 0x00 */
#define REG_ALICE0_MODE_CTRL			0x024d
#define MSK_ALICE0_MODE_CTRL_PLL_MODE_I2C	0x0c
#define MSK_ALICE0_MODE_CTRL_DIV20_CTRL		0x03

/* MHL Tx Control 6th, default value: 0xa0 */
#define REG_MHLTX_CTL6				0x0285
#define MSK_MHLTX_CTL6_EMI_SEL			0xe0
#define MSK_MHLTX_CTL6_TX_CLK_SHAPE_9_8		0x03

/* Packet Filter0, default value: 0x00 */
#define REG_PKT_FILTER_0			0x0290
#define BIT_PKT_FILTER_0_DROP_CEA_GAMUT_PKT	BIT(7)
#define BIT_PKT_FILTER_0_DROP_CEA_CP_PKT	BIT(6)
#define BIT_PKT_FILTER_0_DROP_MPEG_PKT		BIT(5)
#define BIT_PKT_FILTER_0_DROP_SPIF_PKT		BIT(4)
#define BIT_PKT_FILTER_0_DROP_AIF_PKT		BIT(3)
#define BIT_PKT_FILTER_0_DROP_AVI_PKT		BIT(2)
#define BIT_PKT_FILTER_0_DROP_CTS_PKT		BIT(1)
#define BIT_PKT_FILTER_0_DROP_GCP_PKT		BIT(0)

/* Packet Filter1, default value: 0x00 */
#define REG_PKT_FILTER_1			0x0291
#define BIT_PKT_FILTER_1_VSI_OVERRIDE_DIS	BIT(7)
#define BIT_PKT_FILTER_1_AVI_OVERRIDE_DIS	BIT(6)
#define BIT_PKT_FILTER_1_DROP_AUDIO_PKT		BIT(3)
#define BIT_PKT_FILTER_1_DROP_GEN2_PKT		BIT(2)
#define BIT_PKT_FILTER_1_DROP_GEN_PKT		BIT(1)
#define BIT_PKT_FILTER_1_DROP_VSIF_PKT		BIT(0)

/* TMDS Clock Status, default value: 0x10 */
#define REG_TMDS_CSTAT_P3			0x02a0
#define BIT_TMDS_CSTAT_P3_RX_HDMI_CP_CLR_MUTE	BIT(7)
#define BIT_TMDS_CSTAT_P3_RX_HDMI_CP_SET_MUTE	BIT(6)
#define BIT_TMDS_CSTAT_P3_RX_HDMI_CP_NEW_CP	BIT(5)
#define BIT_TMDS_CSTAT_P3_CLR_AVI		BIT(3)
#define BIT_TMDS_CSTAT_P3_SCDT_CLR_AVI_DIS	BIT(2)
#define BIT_TMDS_CSTAT_P3_SCDT			BIT(1)
#define BIT_TMDS_CSTAT_P3_CKDT			BIT(0)

/* RX_HDMI Control, default value: 0x10 */
#define REG_RX_HDMI_CTRL0			0x02a1
#define BIT_RX_HDMI_CTRL0_BYP_DVIFILT_SYNC	BIT(5)
#define BIT_RX_HDMI_CTRL0_HDMI_MODE_EN_ITSELF_CLR BIT(4)
#define BIT_RX_HDMI_CTRL0_HDMI_MODE_SW_VALUE	BIT(3)
#define BIT_RX_HDMI_CTRL0_HDMI_MODE_OVERWRITE	BIT(2)
#define BIT_RX_HDMI_CTRL0_RX_HDMI_HDMI_MODE_EN	BIT(1)
#define BIT_RX_HDMI_CTRL0_RX_HDMI_HDMI_MODE	BIT(0)

/* RX_HDMI Control, default value: 0x38 */
#define REG_RX_HDMI_CTRL2			0x02a3
#define MSK_RX_HDMI_CTRL2_IDLE_CNT		0xf0
#define VAL_RX_HDMI_CTRL2_IDLE_CNT(n)		((n) << 4)
#define BIT_RX_HDMI_CTRL2_USE_AV_MUTE		BIT(3)
#define BIT_RX_HDMI_CTRL2_VSI_MON_SEL_VSI	BIT(0)

/* RX_HDMI Control, default value: 0x0f */
#define REG_RX_HDMI_CTRL3			0x02a4
#define MSK_RX_HDMI_CTRL3_PP_MODE_CLK_EN	0x0f

/* rx_hdmi Clear Buffer, default value: 0x00 */
#define REG_RX_HDMI_CLR_BUFFER			0x02ac
#define MSK_RX_HDMI_CLR_BUFFER_AIF4VSI_CMP	0xc0
#define BIT_RX_HDMI_CLR_BUFFER_USE_AIF4VSI	BIT(5)
#define BIT_RX_HDMI_CLR_BUFFER_VSI_CLR_W_AVI	BIT(4)
#define BIT_RX_HDMI_CLR_BUFFER_VSI_IEEE_ID_CHK_EN BIT(3)
#define BIT_RX_HDMI_CLR_BUFFER_SWAP_VSI_IEEE_ID	BIT(2)
#define BIT_RX_HDMI_CLR_BUFFER_AIF_CLR_EN	BIT(1)
#define BIT_RX_HDMI_CLR_BUFFER_VSI_CLR_EN	BIT(0)

/* RX_HDMI VSI Header1, default value: 0x00 */
#define REG_RX_HDMI_MON_PKT_HEADER1		0x02b8

/* RX_HDMI VSI MHL Monitor, default value: 0x3c */
#define REG_RX_HDMI_VSIF_MHL_MON		0x02d7

#define MSK_RX_HDMI_VSIF_MHL_MON_RX_HDMI_MHL_3D_FORMAT 0x3c
#define MSK_RX_HDMI_VSIF_MHL_MON_RX_HDMI_MHL_VID_FORMAT 0x03

/* Interrupt Source 9, default value: 0x00 */
#define REG_INTR9				0x02e0
#define BIT_INTR9_EDID_ERROR			BIT(6)
#define BIT_INTR9_EDID_DONE			BIT(5)
#define BIT_INTR9_DEVCAP_DONE			BIT(4)

/* Interrupt 9 Mask, default value: 0x00 */
#define REG_INTR9_MASK				0x02e1

/* TPI CBUS Start, default value: 0x00 */
#define REG_TPI_CBUS_START			0x02e2
#define BIT_TPI_CBUS_START_RCP_REQ_START	BIT(7)
#define BIT_TPI_CBUS_START_RCPK_REPLY_START	BIT(6)
#define BIT_TPI_CBUS_START_RCPE_REPLY_START	BIT(5)
#define BIT_TPI_CBUS_START_PUT_LINK_MODE_START	BIT(4)
#define BIT_TPI_CBUS_START_PUT_DCAPCHG_START	BIT(3)
#define BIT_TPI_CBUS_START_PUT_DCAPRDY_START	BIT(2)
#define BIT_TPI_CBUS_START_GET_EDID_START_0	BIT(1)
#define BIT_TPI_CBUS_START_GET_DEVCAP_START	BIT(0)

/* EDID Control, default value: 0x10 */
#define REG_EDID_CTRL				0x02e3
#define BIT_EDID_CTRL_EDID_PRIME_VALID		BIT(7)
#define BIT_EDID_CTRL_XDEVCAP_EN		BIT(6)
#define BIT_EDID_CTRL_DEVCAP_SELECT_DEVCAP	BIT(5)
#define BIT_EDID_CTRL_EDID_FIFO_ADDR_AUTO	BIT(4)
#define BIT_EDID_CTRL_EDID_FIFO_ACCESS_ALWAYS_EN BIT(3)
#define BIT_EDID_CTRL_EDID_FIFO_BLOCK_SEL	BIT(2)
#define BIT_EDID_CTRL_INVALID_BKSV		BIT(1)
#define BIT_EDID_CTRL_EDID_MODE_EN		BIT(0)

/* EDID FIFO Addr, default value: 0x00 */
#define REG_EDID_FIFO_ADDR			0x02e9

/* EDID FIFO Write Data, default value: 0x00 */
#define REG_EDID_FIFO_WR_DATA			0x02ea

/* EDID/DEVCAP FIFO Internal Addr, default value: 0x00 */
#define REG_EDID_FIFO_ADDR_MON			0x02eb

/* EDID FIFO Read Data, default value: 0x00 */
#define REG_EDID_FIFO_RD_DATA			0x02ec

/* EDID DDC Segment Pointer, default value: 0x00 */
#define REG_EDID_START_EXT			0x02ed

/* TX IP BIST CNTL and Status, default value: 0x00 */
#define REG_TX_IP_BIST_CNTLSTA			0x02f2
#define BIT_TX_IP_BIST_CNTLSTA_TXBIST_QUARTER_CLK_SEL BIT(6)
#define BIT_TX_IP_BIST_CNTLSTA_TXBIST_DONE	BIT(5)
#define BIT_TX_IP_BIST_CNTLSTA_TXBIST_ON	BIT(4)
#define BIT_TX_IP_BIST_CNTLSTA_TXBIST_RUN	BIT(3)
#define BIT_TX_IP_BIST_CNTLSTA_TXCLK_HALF_SEL	BIT(2)
#define BIT_TX_IP_BIST_CNTLSTA_TXBIST_EN	BIT(1)
#define BIT_TX_IP_BIST_CNTLSTA_TXBIST_SEL	BIT(0)

/* TX IP BIST INST LOW, default value: 0x00 */
#define REG_TX_IP_BIST_INST_LOW			0x02f3
#define REG_TX_IP_BIST_INST_HIGH		0x02f4

/* TX IP BIST PATTERN LOW, default value: 0x00 */
#define REG_TX_IP_BIST_PAT_LOW			0x02f5
#define REG_TX_IP_BIST_PAT_HIGH			0x02f6

/* TX IP BIST CONFIGURE LOW, default value: 0x00 */
#define REG_TX_IP_BIST_CONF_LOW			0x02f7
#define REG_TX_IP_BIST_CONF_HIGH		0x02f8

/* E-MSC General Control, default value: 0x80 */
#define REG_GENCTL				0x0300
#define BIT_GENCTL_SPEC_TRANS_DIS		BIT(7)
#define BIT_GENCTL_DIS_XMIT_ERR_STATE		BIT(6)
#define BIT_GENCTL_SPI_MISO_EDGE		BIT(5)
#define BIT_GENCTL_SPI_MOSI_EDGE		BIT(4)
#define BIT_GENCTL_CLR_EMSC_RFIFO		BIT(3)
#define BIT_GENCTL_CLR_EMSC_XFIFO		BIT(2)
#define BIT_GENCTL_START_TRAIN_SEQ		BIT(1)
#define BIT_GENCTL_EMSC_EN			BIT(0)

/* E-MSC Comma ErrorCNT, default value: 0x03 */
#define REG_COMMECNT				0x0305
#define BIT_COMMECNT_I2C_TO_EMSC_EN		BIT(7)
#define MSK_COMMECNT_COMMA_CHAR_ERR_CNT		0x0f

/* E-MSC RFIFO ByteCnt, default value: 0x00 */
#define REG_EMSCRFIFOBCNTL			0x031a
#define REG_EMSCRFIFOBCNTH			0x031b

/* SPI Burst Cnt Status, default value: 0x00 */
#define REG_SPIBURSTCNT				0x031e

/* SPI Burst Status and SWRST, default value: 0x00 */
#define REG_SPIBURSTSTAT			0x0322
#define BIT_SPIBURSTSTAT_SPI_HDCPRST		BIT(7)
#define BIT_SPIBURSTSTAT_SPI_CBUSRST		BIT(6)
#define BIT_SPIBURSTSTAT_SPI_SRST		BIT(5)
#define BIT_SPIBURSTSTAT_EMSC_NORMAL_MODE	BIT(0)

/* E-MSC 1st Interrupt, default value: 0x00 */
#define REG_EMSCINTR				0x0323
#define BIT_EMSCINTR_EMSC_XFIFO_EMPTY		BIT(7)
#define BIT_EMSCINTR_EMSC_XMIT_ACK_TOUT		BIT(6)
#define BIT_EMSCINTR_EMSC_RFIFO_READ_ERR	BIT(5)
#define BIT_EMSCINTR_EMSC_XFIFO_WRITE_ERR	BIT(4)
#define BIT_EMSCINTR_EMSC_COMMA_CHAR_ERR	BIT(3)
#define BIT_EMSCINTR_EMSC_XMIT_DONE		BIT(2)
#define BIT_EMSCINTR_EMSC_XMIT_GNT_TOUT		BIT(1)
#define BIT_EMSCINTR_SPI_DVLD		BIT(0)

/* E-MSC Interrupt Mask, default value: 0x00 */
#define REG_EMSCINTRMASK			0x0324

/* I2C E-MSC XMIT FIFO Write Port, default value: 0x00 */
#define REG_EMSC_XMIT_WRITE_PORT		0x032a

/* I2C E-MSC RCV FIFO Write Port, default value: 0x00 */
#define REG_EMSC_RCV_READ_PORT			0x032b

/* E-MSC 2nd Interrupt, default value: 0x00 */
#define REG_EMSCINTR1				0x032c
#define BIT_EMSCINTR1_EMSC_TRAINING_COMMA_ERR	BIT(0)

/* E-MSC Interrupt Mask, default value: 0x00 */
#define REG_EMSCINTRMASK1			0x032d
#define BIT_EMSCINTRMASK1_EMSC_INTRMASK1_0	BIT(0)

/* MHL Top Ctl, default value: 0x00 */
#define REG_MHL_TOP_CTL				0x0330
#define BIT_MHL_TOP_CTL_MHL3_DOC_SEL		BIT(7)
#define BIT_MHL_TOP_CTL_MHL_PP_SEL		BIT(6)
#define MSK_MHL_TOP_CTL_IF_TIMING_CTL		0x03

/* MHL DataPath 1st Ctl, default value: 0xbc */
#define REG_MHL_DP_CTL0				0x0331
#define BIT_MHL_DP_CTL0_DP_OE			BIT(7)
#define BIT_MHL_DP_CTL0_TX_OE_OVR		BIT(6)
#define MSK_MHL_DP_CTL0_TX_OE			0x3f

/* MHL DataPath 2nd Ctl, default value: 0xbb */
#define REG_MHL_DP_CTL1				0x0332
#define MSK_MHL_DP_CTL1_CK_SWING_CTL		0xf0
#define MSK_MHL_DP_CTL1_DT_SWING_CTL		0x0f

/* MHL DataPath 3rd Ctl, default value: 0x2f */
#define REG_MHL_DP_CTL2				0x0333
#define BIT_MHL_DP_CTL2_CLK_BYPASS_EN		BIT(7)
#define MSK_MHL_DP_CTL2_DAMP_TERM_SEL		0x30
#define MSK_MHL_DP_CTL2_CK_TERM_SEL		0x0c
#define MSK_MHL_DP_CTL2_DT_TERM_SEL		0x03

/* MHL DataPath 4th Ctl, default value: 0x48 */
#define REG_MHL_DP_CTL3				0x0334
#define MSK_MHL_DP_CTL3_DT_DRV_VNBC_CTL		0xf0
#define MSK_MHL_DP_CTL3_DT_DRV_VNB_CTL		0x0f

/* MHL DataPath 5th Ctl, default value: 0x48 */
#define REG_MHL_DP_CTL4				0x0335
#define MSK_MHL_DP_CTL4_CK_DRV_VNBC_CTL		0xf0
#define MSK_MHL_DP_CTL4_CK_DRV_VNB_CTL		0x0f

/* MHL DataPath 6th Ctl, default value: 0x3f */
#define REG_MHL_DP_CTL5				0x0336
#define BIT_MHL_DP_CTL5_RSEN_EN_OVR		BIT(7)
#define BIT_MHL_DP_CTL5_RSEN_EN			BIT(6)
#define MSK_MHL_DP_CTL5_DAMP_TERM_VGS_CTL	0x30
#define MSK_MHL_DP_CTL5_CK_TERM_VGS_CTL		0x0c
#define MSK_MHL_DP_CTL5_DT_TERM_VGS_CTL		0x03

/* MHL PLL 1st Ctl, default value: 0x05 */
#define REG_MHL_PLL_CTL0			0x0337
#define BIT_MHL_PLL_CTL0_AUD_CLK_EN		BIT(7)

#define MSK_MHL_PLL_CTL0_AUD_CLK_RATIO		0x70
#define VAL_MHL_PLL_CTL0_AUD_CLK_RATIO_5_10	0x70
#define VAL_MHL_PLL_CTL0_AUD_CLK_RATIO_5_6	0x60
#define VAL_MHL_PLL_CTL0_AUD_CLK_RATIO_5_4	0x50
#define VAL_MHL_PLL_CTL0_AUD_CLK_RATIO_5_2	0x40
#define VAL_MHL_PLL_CTL0_AUD_CLK_RATIO_5_5	0x30
#define VAL_MHL_PLL_CTL0_AUD_CLK_RATIO_5_3	0x20
#define VAL_MHL_PLL_CTL0_AUD_CLK_RATIO_5_2_PRIME 0x10
#define VAL_MHL_PLL_CTL0_AUD_CLK_RATIO_5_1	0x00

#define MSK_MHL_PLL_CTL0_HDMI_CLK_RATIO		0x0c
#define VAL_MHL_PLL_CTL0_HDMI_CLK_RATIO_4X	0x0c
#define VAL_MHL_PLL_CTL0_HDMI_CLK_RATIO_2X	0x08
#define VAL_MHL_PLL_CTL0_HDMI_CLK_RATIO_1X	0x04
#define VAL_MHL_PLL_CTL0_HDMI_CLK_RATIO_HALF_X	0x00

#define BIT_MHL_PLL_CTL0_CRYSTAL_CLK_SEL	BIT(1)
#define BIT_MHL_PLL_CTL0_ZONE_MASK_OE		BIT(0)

/* MHL PLL 3rd Ctl, default value: 0x80 */
#define REG_MHL_PLL_CTL2			0x0339
#define BIT_MHL_PLL_CTL2_CLKDETECT_EN		BIT(7)
#define BIT_MHL_PLL_CTL2_MEAS_FVCO		BIT(3)
#define BIT_MHL_PLL_CTL2_PLL_FAST_LOCK		BIT(2)
#define MSK_MHL_PLL_CTL2_PLL_LF_SEL		0x03

/* MHL CBUS 1st Ctl, default value: 0x12 */
#define REG_MHL_CBUS_CTL0			0x0340
#define BIT_MHL_CBUS_CTL0_CBUS_RGND_TEST_MODE	BIT(7)

#define MSK_MHL_CBUS_CTL0_CBUS_RGND_VTH_CTL	0x30
#define VAL_MHL_CBUS_CTL0_CBUS_RGND_VBIAS_734	0x00
#define VAL_MHL_CBUS_CTL0_CBUS_RGND_VBIAS_747	0x10
#define VAL_MHL_CBUS_CTL0_CBUS_RGND_VBIAS_740	0x20
#define VAL_MHL_CBUS_CTL0_CBUS_RGND_VBIAS_754	0x30

#define MSK_MHL_CBUS_CTL0_CBUS_RES_TEST_SEL	0x0c

#define MSK_MHL_CBUS_CTL0_CBUS_DRV_SEL		0x03
#define VAL_MHL_CBUS_CTL0_CBUS_DRV_SEL_WEAKEST	0x00
#define VAL_MHL_CBUS_CTL0_CBUS_DRV_SEL_WEAK	0x01
#define VAL_MHL_CBUS_CTL0_CBUS_DRV_SEL_STRONG	0x02
#define VAL_MHL_CBUS_CTL0_CBUS_DRV_SEL_STRONGEST 0x03

/* MHL CBUS 2nd Ctl, default value: 0x03 */
#define REG_MHL_CBUS_CTL1			0x0341
#define MSK_MHL_CBUS_CTL1_CBUS_RGND_RES_CTL	0x07
#define VAL_MHL_CBUS_CTL1_0888_OHM		0x00
#define VAL_MHL_CBUS_CTL1_1115_OHM		0x04
#define VAL_MHL_CBUS_CTL1_1378_OHM		0x07

/* MHL CoC 1st Ctl, default value: 0xc3 */
#define REG_MHL_COC_CTL0			0x0342
#define BIT_MHL_COC_CTL0_COC_BIAS_EN		BIT(7)
#define MSK_MHL_COC_CTL0_COC_BIAS_CTL		0x70
#define MSK_MHL_COC_CTL0_COC_TERM_CTL		0x07

/* MHL CoC 2nd Ctl, default value: 0x87 */
#define REG_MHL_COC_CTL1			0x0343
#define BIT_MHL_COC_CTL1_COC_EN			BIT(7)
#define MSK_MHL_COC_CTL1_COC_DRV_CTL		0x3f

/* MHL CoC 4th Ctl, default value: 0x00 */
#define REG_MHL_COC_CTL3			0x0345
#define BIT_MHL_COC_CTL3_COC_AECHO_EN		BIT(0)

/* MHL CoC 5th Ctl, default value: 0x28 */
#define REG_MHL_COC_CTL4			0x0346
#define MSK_MHL_COC_CTL4_COC_IF_CTL		0xf0
#define MSK_MHL_COC_CTL4_COC_SLEW_CTL		0x0f

/* MHL CoC 6th Ctl, default value: 0x0d */
#define REG_MHL_COC_CTL5			0x0347

/* MHL DoC 1st Ctl, default value: 0x18 */
#define REG_MHL_DOC_CTL0			0x0349
#define BIT_MHL_DOC_CTL0_DOC_RXDATA_EN		BIT(7)
#define MSK_MHL_DOC_CTL0_DOC_DM_TERM		0x38
#define MSK_MHL_DOC_CTL0_DOC_OPMODE		0x06
#define BIT_MHL_DOC_CTL0_DOC_RXBIAS_EN		BIT(0)

/* MHL DataPath 7th Ctl, default value: 0x2a */
#define REG_MHL_DP_CTL6				0x0350
#define BIT_MHL_DP_CTL6_DP_TAP2_SGN		BIT(5)
#define BIT_MHL_DP_CTL6_DP_TAP2_EN		BIT(4)
#define BIT_MHL_DP_CTL6_DP_TAP1_SGN		BIT(3)
#define BIT_MHL_DP_CTL6_DP_TAP1_EN		BIT(2)
#define BIT_MHL_DP_CTL6_DT_PREDRV_FEEDCAP_EN	BIT(1)
#define BIT_MHL_DP_CTL6_DP_PRE_POST_SEL		BIT(0)

/* MHL DataPath 8th Ctl, default value: 0x06 */
#define REG_MHL_DP_CTL7				0x0351
#define MSK_MHL_DP_CTL7_DT_DRV_VBIAS_CASCTL	0xf0
#define MSK_MHL_DP_CTL7_DT_DRV_IREF_CTL		0x0f

#define REG_MHL_DP_CTL8				0x0352

/* Tx Zone Ctl1, default value: 0x00 */
#define REG_TX_ZONE_CTL1			0x0361
#define VAL_TX_ZONE_CTL1_TX_ZONE_CTRL_MODE	0x08

/* MHL3 Tx Zone Ctl, default value: 0x00 */
#define REG_MHL3_TX_ZONE_CTL			0x0364
#define BIT_MHL3_TX_ZONE_CTL_MHL2_INTPLT_ZONE_MANU_EN BIT(7)
#define MSK_MHL3_TX_ZONE_CTL_MHL3_TX_ZONE	0x03

#define MSK_TX_ZONE_CTL3_TX_ZONE		0x03
#define VAL_TX_ZONE_CTL3_TX_ZONE_6GBPS		0x00
#define VAL_TX_ZONE_CTL3_TX_ZONE_3GBPS		0x01
#define VAL_TX_ZONE_CTL3_TX_ZONE_1_5GBPS	0x02

/* HDCP Polling Control and Status, default value: 0x70 */
#define REG_HDCP2X_POLL_CS			0x0391

#define BIT_HDCP2X_POLL_CS_HDCP2X_MSG_SZ_CLR_OPTION BIT(6)
#define BIT_HDCP2X_POLL_CS_HDCP2X_RPT_READY_CLR_OPTION BIT(5)
#define BIT_HDCP2X_POLL_CS_HDCP2X_REAUTH_REQ_CLR_OPTION BIT(4)
#define MSK_HDCP2X_POLL_CS_			0x0c
#define BIT_HDCP2X_POLL_CS_HDCP2X_DIS_POLL_GNT	BIT(1)
#define BIT_HDCP2X_POLL_CS_HDCP2X_DIS_POLL_EN	BIT(0)

/* HDCP Interrupt 0, default value: 0x00 */
#define REG_HDCP2X_INTR0			0x0398

/* HDCP Interrupt 0 Mask, default value: 0x00 */
#define REG_HDCP2X_INTR0_MASK			0x0399

/* HDCP General Control 0, default value: 0x02 */
#define REG_HDCP2X_CTRL_0			0x03a0
#define BIT_HDCP2X_CTRL_0_HDCP2X_ENCRYPT_EN	BIT(7)
#define BIT_HDCP2X_CTRL_0_HDCP2X_POLINT_SEL	BIT(6)
#define BIT_HDCP2X_CTRL_0_HDCP2X_POLINT_OVR	BIT(5)
#define BIT_HDCP2X_CTRL_0_HDCP2X_PRECOMPUTE	BIT(4)
#define BIT_HDCP2X_CTRL_0_HDCP2X_HDMIMODE	BIT(3)
#define BIT_HDCP2X_CTRL_0_HDCP2X_REPEATER	BIT(2)
#define BIT_HDCP2X_CTRL_0_HDCP2X_HDCPTX		BIT(1)
#define BIT_HDCP2X_CTRL_0_HDCP2X_EN		BIT(0)

/* HDCP General Control 1, default value: 0x08 */
#define REG_HDCP2X_CTRL_1			0x03a1
#define MSK_HDCP2X_CTRL_1_HDCP2X_REAUTH_MSK_3_0	0xf0
#define BIT_HDCP2X_CTRL_1_HDCP2X_HPD_SW		BIT(3)
#define BIT_HDCP2X_CTRL_1_HDCP2X_HPD_OVR	BIT(2)
#define BIT_HDCP2X_CTRL_1_HDCP2X_CTL3MSK	BIT(1)
#define BIT_HDCP2X_CTRL_1_HDCP2X_REAUTH_SW	BIT(0)

/* HDCP Misc Control, default value: 0x00 */
#define REG_HDCP2X_MISC_CTRL			0x03a5
#define BIT_HDCP2X_MISC_CTRL_HDCP2X_RPT_SMNG_XFER_START BIT(4)
#define BIT_HDCP2X_MISC_CTRL_HDCP2X_RPT_SMNG_WR_START BIT(3)
#define BIT_HDCP2X_MISC_CTRL_HDCP2X_RPT_SMNG_WR	BIT(2)
#define BIT_HDCP2X_MISC_CTRL_HDCP2X_RPT_RCVID_RD_START BIT(1)
#define BIT_HDCP2X_MISC_CTRL_HDCP2X_RPT_RCVID_RD	BIT(0)

/* HDCP RPT SMNG K, default value: 0x00 */
#define REG_HDCP2X_RPT_SMNG_K			0x03a6

/* HDCP RPT SMNG In, default value: 0x00 */
#define REG_HDCP2X_RPT_SMNG_IN			0x03a7

/* HDCP Auth Status, default value: 0x00 */
#define REG_HDCP2X_AUTH_STAT			0x03aa

/* HDCP RPT RCVID Out, default value: 0x00 */
#define REG_HDCP2X_RPT_RCVID_OUT		0x03ac

/* HDCP TP1, default value: 0x62 */
#define REG_HDCP2X_TP1				0x03b4

/* HDCP GP Out 0, default value: 0x00 */
#define REG_HDCP2X_GP_OUT0			0x03c7

/* HDCP Repeater RCVR ID 0, default value: 0x00 */
#define REG_HDCP2X_RPT_RCVR_ID0			0x03d1

/* HDCP DDCM Status, default value: 0x00 */
#define REG_HDCP2X_DDCM_STS			0x03d8
#define MSK_HDCP2X_DDCM_STS_HDCP2X_DDCM_ERR_STS_3_0 0xf0
#define MSK_HDCP2X_DDCM_STS_HDCP2X_DDCM_CTL_CS_3_0 0x0f

/* HDMI2MHL3 Control, default value: 0x0a */
#define REG_M3_CTRL				0x03e0
#define BIT_M3_CTRL_H2M_SWRST			BIT(4)
#define BIT_M3_CTRL_SW_MHL3_SEL			BIT(3)
#define BIT_M3_CTRL_M3AV_EN			BIT(2)
#define BIT_M3_CTRL_ENC_TMDS			BIT(1)
#define BIT_M3_CTRL_MHL3_MASTER_EN		BIT(0)

#define VAL_M3_CTRL_MHL1_2_VALUE (BIT_M3_CTRL_SW_MHL3_SEL \
				  | BIT_M3_CTRL_ENC_TMDS)
#define VAL_M3_CTRL_MHL3_VALUE (BIT_M3_CTRL_SW_MHL3_SEL \
				| BIT_M3_CTRL_M3AV_EN \
				| BIT_M3_CTRL_ENC_TMDS \
				| BIT_M3_CTRL_MHL3_MASTER_EN)

/* HDMI2MHL3 Port0 Control, default value: 0x04 */
#define REG_M3_P0CTRL				0x03e1
#define BIT_M3_P0CTRL_MHL3_P0_HDCP_ENC_EN	BIT(4)
#define BIT_M3_P0CTRL_MHL3_P0_UNLIMIT_EN	BIT(3)
#define BIT_M3_P0CTRL_MHL3_P0_HDCP_EN		BIT(2)
#define BIT_M3_P0CTRL_MHL3_P0_PIXEL_MODE_PACKED	BIT(1)
#define BIT_M3_P0CTRL_MHL3_P0_PORT_EN		BIT(0)

#define REG_M3_POSTM				0x03e2
#define MSK_M3_POSTM_RRP_DECODE			0xf8
#define MSK_M3_POSTM_MHL3_P0_STM_ID		0x07

/* HDMI2MHL3 Scramble Control, default value: 0x41 */
#define REG_M3_SCTRL				0x03e6
#define MSK_M3_SCTRL_MHL3_SR_LENGTH		0xf0
#define BIT_M3_SCTRL_MHL3_SCRAMBLER_EN		BIT(0)

/* HSIC Div Ctl, default value: 0x05 */
#define REG_DIV_CTL_MAIN			0x03f2
#define MSK_DIV_CTL_MAIN_PRE_DIV_CTL_MAIN	0x1c
#define MSK_DIV_CTL_MAIN_FB_DIV_CTL_MAIN	0x03

/* MHL Capability 1st Byte, default value: 0x00 */
#define REG_MHL_DEVCAP_0			0x0400

/* MHL Interrupt 1st Byte, default value: 0x00 */
#define REG_MHL_INT_0				0x0420

/* Device Status 1st byte, default value: 0x00 */
#define REG_MHL_STAT_0				0x0430

/* CBUS Scratch Pad 1st Byte, default value: 0x00 */
#define REG_MHL_SCRPAD_0			0x0440

/* MHL Extended Capability 1st Byte, default value: 0x00 */
#define REG_MHL_EXTDEVCAP_0			0x0480

/* Device Extended Status 1st byte, default value: 0x00 */
#define REG_MHL_EXTSTAT_0			0x0490

/* TPI DTD Byte2, default value: 0x00 */
#define REG_TPI_DTD_B2				0x0602

#define VAL_TPI_QUAN_RANGE_LIMITED		0x01
#define VAL_TPI_QUAN_RANGE_FULL			0x02
#define VAL_TPI_FORMAT_RGB			0x00
#define VAL_TPI_FORMAT_YCBCR444			0x01
#define VAL_TPI_FORMAT_YCBCR422			0x02
#define VAL_TPI_FORMAT_INTERNAL_RGB		0x03
#define VAL_TPI_FORMAT(_fmt, _qr) \
		(VAL_TPI_FORMAT_##_fmt | (VAL_TPI_QUAN_RANGE_##_qr << 2))

/* Input Format, default value: 0x00 */
#define REG_TPI_INPUT				0x0609
#define BIT_TPI_INPUT_EXTENDEDBITMODE		BIT(7)
#define BIT_TPI_INPUT_ENDITHER			BIT(6)
#define MSK_TPI_INPUT_INPUT_QUAN_RANGE		0x0c
#define MSK_TPI_INPUT_INPUT_FORMAT		0x03

/* Output Format, default value: 0x00 */
#define REG_TPI_OUTPUT				0x060a
#define BIT_TPI_OUTPUT_CSCMODE709		BIT(4)
#define MSK_TPI_OUTPUT_OUTPUT_QUAN_RANGE	0x0c
#define MSK_TPI_OUTPUT_OUTPUT_FORMAT		0x03

/* TPI AVI Check Sum, default value: 0x00 */
#define REG_TPI_AVI_CHSUM			0x060c

/* TPI System Control, default value: 0x00 */
#define REG_TPI_SC				0x061a
#define BIT_TPI_SC_TPI_UPDATE_FLG		BIT(7)
#define BIT_TPI_SC_TPI_REAUTH_CTL		BIT(6)
#define BIT_TPI_SC_TPI_OUTPUT_MODE_1		BIT(5)
#define BIT_TPI_SC_REG_TMDS_OE_POWER_DOWN	BIT(4)
#define BIT_TPI_SC_TPI_AV_MUTE			BIT(3)
#define BIT_TPI_SC_DDC_GPU_REQUEST		BIT(2)
#define BIT_TPI_SC_DDC_TPI_SW			BIT(1)
#define BIT_TPI_SC_TPI_OUTPUT_MODE_0_HDMI	BIT(0)

/* TPI COPP Query Data, default value: 0x00 */
#define REG_TPI_COPP_DATA1			0x0629
#define BIT_TPI_COPP_DATA1_COPP_GPROT		BIT(7)
#define BIT_TPI_COPP_DATA1_COPP_LPROT		BIT(6)
#define MSK_TPI_COPP_DATA1_COPP_LINK_STATUS	0x30
#define VAL_TPI_COPP_LINK_STATUS_NORMAL		0x00
#define VAL_TPI_COPP_LINK_STATUS_LINK_LOST	0x10
#define VAL_TPI_COPP_LINK_STATUS_RENEGOTIATION_REQ 0x20
#define VAL_TPI_COPP_LINK_STATUS_LINK_SUSPENDED	0x30
#define BIT_TPI_COPP_DATA1_COPP_HDCP_REP	BIT(3)
#define BIT_TPI_COPP_DATA1_COPP_CONNTYPE_0	BIT(2)
#define BIT_TPI_COPP_DATA1_COPP_PROTYPE		BIT(1)
#define BIT_TPI_COPP_DATA1_COPP_CONNTYPE_1	BIT(0)

/* TPI COPP Control Data, default value: 0x00 */
#define REG_TPI_COPP_DATA2			0x062a
#define BIT_TPI_COPP_DATA2_INTR_ENCRYPTION	BIT(5)
#define BIT_TPI_COPP_DATA2_KSV_FORWARD		BIT(4)
#define BIT_TPI_COPP_DATA2_INTERM_RI_CHECK_EN	BIT(3)
#define BIT_TPI_COPP_DATA2_DOUBLE_RI_CHECK	BIT(2)
#define BIT_TPI_COPP_DATA2_DDC_SHORT_RI_RD	BIT(1)
#define BIT_TPI_COPP_DATA2_COPP_PROTLEVEL	BIT(0)

/* TPI Interrupt Enable, default value: 0x00 */
#define REG_TPI_INTR_EN				0x063c

/* TPI Interrupt Status Low Byte, default value: 0x00 */
#define REG_TPI_INTR_ST0			0x063d
#define BIT_TPI_INTR_ST0_TPI_AUTH_CHNGE_STAT	BIT(7)
#define BIT_TPI_INTR_ST0_TPI_V_RDY_STAT		BIT(6)
#define BIT_TPI_INTR_ST0_TPI_COPP_CHNGE_STAT	BIT(5)
#define BIT_TPI_INTR_ST0_KSV_FIFO_FIRST_STAT	BIT(3)
#define BIT_TPI_INTR_ST0_READ_BKSV_BCAPS_DONE_STAT BIT(2)
#define BIT_TPI_INTR_ST0_READ_BKSV_BCAPS_ERR_STAT BIT(1)
#define BIT_TPI_INTR_ST0_READ_BKSV_ERR_STAT	BIT(0)

/* TPI DS BCAPS Status, default value: 0x00 */
#define REG_TPI_DS_BCAPS			0x0644

/* TPI BStatus1, default value: 0x00 */
#define REG_TPI_BSTATUS1			0x0645
#define BIT_TPI_BSTATUS1_DS_DEV_EXCEED		BIT(7)
#define MSK_TPI_BSTATUS1_DS_DEV_CNT		0x7f

/* TPI BStatus2, default value: 0x10 */
#define REG_TPI_BSTATUS2			0x0646
#define MSK_TPI_BSTATUS2_DS_BSTATUS		0xe0
#define BIT_TPI_BSTATUS2_DS_HDMI_MODE		BIT(4)
#define BIT_TPI_BSTATUS2_DS_CASC_EXCEED		BIT(3)
#define MSK_TPI_BSTATUS2_DS_DEPTH		0x07

/* TPI HW Optimization Control #3, default value: 0x00 */
#define REG_TPI_HW_OPT3				0x06bb
#define BIT_TPI_HW_OPT3_DDC_DEBUG		BIT(7)
#define BIT_TPI_HW_OPT3_RI_CHECK_SKIP		BIT(3)
#define BIT_TPI_HW_OPT3_TPI_DDC_BURST_MODE	BIT(2)
#define MSK_TPI_HW_OPT3_TPI_DDC_REQ_LEVEL	0x03

/* TPI Info Frame Select, default value: 0x00 */
#define REG_TPI_INFO_FSEL			0x06bf
#define BIT_TPI_INFO_FSEL_EN			BIT(7)
#define BIT_TPI_INFO_FSEL_RPT			BIT(6)
#define BIT_TPI_INFO_FSEL_READ_FLAG		BIT(5)
#define MSK_TPI_INFO_FSEL_PKT			0x07
#define VAL_TPI_INFO_FSEL_AVI			0x00
#define VAL_TPI_INFO_FSEL_SPD			0x01
#define VAL_TPI_INFO_FSEL_AUD			0x02
#define VAL_TPI_INFO_FSEL_MPG			0x03
#define VAL_TPI_INFO_FSEL_GEN			0x04
#define VAL_TPI_INFO_FSEL_GEN2			0x05
#define VAL_TPI_INFO_FSEL_VSI			0x06

/* TPI Info Byte #0, default value: 0x00 */
#define REG_TPI_INFO_B0				0x06c0

/* CoC Status, default value: 0x00 */
#define REG_COC_STAT_0				0x0700
#define BIT_COC_STAT_0_PLL_LOCKED		BIT(7)
#define MSK_COC_STAT_0_FSM_STATE		0x0f

#define REG_COC_STAT_1				0x0701
#define REG_COC_STAT_2				0x0702
#define REG_COC_STAT_3				0x0703
#define REG_COC_STAT_4				0x0704
#define REG_COC_STAT_5				0x0705

/* CoC 1st Ctl, default value: 0x40 */
#define REG_COC_CTL0				0x0710

/* CoC 2nd Ctl, default value: 0x0a */
#define REG_COC_CTL1				0x0711
#define MSK_COC_CTL1_COC_CTRL1_7_6		0xc0
#define MSK_COC_CTL1_COC_CTRL1_5_0		0x3f

/* CoC 3rd Ctl, default value: 0x14 */
#define REG_COC_CTL2				0x0712
#define MSK_COC_CTL2_COC_CTRL2_7_6		0xc0
#define MSK_COC_CTL2_COC_CTRL2_5_0		0x3f

/* CoC 4th Ctl, default value: 0x40 */
#define REG_COC_CTL3				0x0713
#define BIT_COC_CTL3_COC_CTRL3_7		BIT(7)
#define MSK_COC_CTL3_COC_CTRL3_6_0		0x7f

/* CoC 7th Ctl, default value: 0x00 */
#define REG_COC_CTL6				0x0716
#define BIT_COC_CTL6_COC_CTRL6_7		BIT(7)
#define BIT_COC_CTL6_COC_CTRL6_6		BIT(6)
#define MSK_COC_CTL6_COC_CTRL6_5_0		0x3f

/* CoC 8th Ctl, default value: 0x06 */
#define REG_COC_CTL7				0x0717
#define BIT_COC_CTL7_COC_CTRL7_7		BIT(7)
#define BIT_COC_CTL7_COC_CTRL7_6		BIT(6)
#define BIT_COC_CTL7_COC_CTRL7_5		BIT(5)
#define MSK_COC_CTL7_COC_CTRL7_4_3		0x18
#define MSK_COC_CTL7_COC_CTRL7_2_0		0x07

/* CoC 10th Ctl, default value: 0x00 */
#define REG_COC_CTL9				0x0719

/* CoC 11th Ctl, default value: 0x00 */
#define REG_COC_CTLA				0x071a

/* CoC 12th Ctl, default value: 0x00 */
#define REG_COC_CTLB				0x071b

/* CoC 13th Ctl, default value: 0x0f */
#define REG_COC_CTLC				0x071c

/* CoC 14th Ctl, default value: 0x0a */
#define REG_COC_CTLD				0x071d
#define BIT_COC_CTLD_COC_CTRLD_7		BIT(7)
#define MSK_COC_CTLD_COC_CTRLD_6_0		0x7f

/* CoC 15th Ctl, default value: 0x0a */
#define REG_COC_CTLE				0x071e
#define BIT_COC_CTLE_COC_CTRLE_7		BIT(7)
#define MSK_COC_CTLE_COC_CTRLE_6_0		0x7f

/* CoC 16th Ctl, default value: 0x00 */
#define REG_COC_CTLF				0x071f
#define MSK_COC_CTLF_COC_CTRLF_7_3		0xf8
#define MSK_COC_CTLF_COC_CTRLF_2_0		0x07

/* CoC 18th Ctl, default value: 0x32 */
#define REG_COC_CTL11				0x0721
#define MSK_COC_CTL11_COC_CTRL11_7_4		0xf0
#define MSK_COC_CTL11_COC_CTRL11_3_0		0x0f

/* CoC 21st Ctl, default value: 0x00 */
#define REG_COC_CTL14				0x0724
#define MSK_COC_CTL14_COC_CTRL14_7_4		0xf0
#define MSK_COC_CTL14_COC_CTRL14_3_0		0x0f

/* CoC 22nd Ctl, default value: 0x00 */
#define REG_COC_CTL15				0x0725
#define BIT_COC_CTL15_COC_CTRL15_7		BIT(7)
#define MSK_COC_CTL15_COC_CTRL15_6_4		0x70
#define MSK_COC_CTL15_COC_CTRL15_3_0		0x0f

/* CoC Interrupt, default value: 0x00 */
#define REG_COC_INTR				0x0726

/* CoC Interrupt Mask, default value: 0x00 */
#define REG_COC_INTR_MASK			0x0727
#define BIT_COC_PLL_LOCK_STATUS_CHANGE		BIT(0)
#define BIT_COC_CALIBRATION_DONE		BIT(1)

/* CoC Misc Ctl, default value: 0x00 */
#define REG_COC_MISC_CTL0			0x0728
#define BIT_COC_MISC_CTL0_FSM_MON		BIT(7)

/* CoC 24th Ctl, default value: 0x00 */
#define REG_COC_CTL17				0x072a
#define MSK_COC_CTL17_COC_CTRL17_7_4		0xf0
#define MSK_COC_CTL17_COC_CTRL17_3_0		0x0f

/* CoC 25th Ctl, default value: 0x00 */
#define REG_COC_CTL18				0x072b
#define MSK_COC_CTL18_COC_CTRL18_7_4		0xf0
#define MSK_COC_CTL18_COC_CTRL18_3_0		0x0f

/* CoC 26th Ctl, default value: 0x00 */
#define REG_COC_CTL19				0x072c
#define MSK_COC_CTL19_COC_CTRL19_7_4		0xf0
#define MSK_COC_CTL19_COC_CTRL19_3_0		0x0f

/* CoC 27th Ctl, default value: 0x00 */
#define REG_COC_CTL1A				0x072d
#define MSK_COC_CTL1A_COC_CTRL1A_7_2		0xfc
#define MSK_COC_CTL1A_COC_CTRL1A_1_0		0x03

/* DoC 9th Status, default value: 0x00 */
#define REG_DOC_STAT_8				0x0740

/* DoC 10th Status, default value: 0x00 */
#define REG_DOC_STAT_9				0x0741

/* DoC 5th CFG, default value: 0x00 */
#define REG_DOC_CFG4				0x074e
#define MSK_DOC_CFG4_DBG_STATE_DOC_FSM		0x0f

/* DoC 1st Ctl, default value: 0x40 */
#define REG_DOC_CTL0				0x0751

/* DoC 7th Ctl, default value: 0x00 */
#define REG_DOC_CTL6				0x0757
#define BIT_DOC_CTL6_DOC_CTRL6_7		BIT(7)
#define BIT_DOC_CTL6_DOC_CTRL6_6		BIT(6)
#define MSK_DOC_CTL6_DOC_CTRL6_5_4		0x30
#define MSK_DOC_CTL6_DOC_CTRL6_3_0		0x0f

/* DoC 8th Ctl, default value: 0x00 */
#define REG_DOC_CTL7				0x0758
#define BIT_DOC_CTL7_DOC_CTRL7_7		BIT(7)
#define BIT_DOC_CTL7_DOC_CTRL7_6		BIT(6)
#define BIT_DOC_CTL7_DOC_CTRL7_5		BIT(5)
#define MSK_DOC_CTL7_DOC_CTRL7_4_3		0x18
#define MSK_DOC_CTL7_DOC_CTRL7_2_0		0x07

/* DoC 9th Ctl, default value: 0x00 */
#define REG_DOC_CTL8				0x076c
#define BIT_DOC_CTL8_DOC_CTRL8_7		BIT(7)
#define MSK_DOC_CTL8_DOC_CTRL8_6_4		0x70
#define MSK_DOC_CTL8_DOC_CTRL8_3_2		0x0c
#define MSK_DOC_CTL8_DOC_CTRL8_1_0		0x03

/* DoC 10th Ctl, default value: 0x00 */
#define REG_DOC_CTL9				0x076d

/* DoC 11th Ctl, default value: 0x00 */
#define REG_DOC_CTLA				0x076e

/* DoC 15th Ctl, default value: 0x00 */
#define REG_DOC_CTLE				0x0772
#define BIT_DOC_CTLE_DOC_CTRLE_7		BIT(7)
#define BIT_DOC_CTLE_DOC_CTRLE_6		BIT(6)
#define MSK_DOC_CTLE_DOC_CTRLE_5_4		0x30
#define MSK_DOC_CTLE_DOC_CTRLE_3_0		0x0f

/* Interrupt Mask 1st, default value: 0x00 */
#define REG_MHL_INT_0_MASK			0x0580

/* Interrupt Mask 2nd, default value: 0x00 */
#define REG_MHL_INT_1_MASK			0x0581

/* Interrupt Mask 3rd, default value: 0x00 */
#define REG_MHL_INT_2_MASK			0x0582

/* Interrupt Mask 4th, default value: 0x00 */
#define REG_MHL_INT_3_MASK			0x0583

/* MDT Receive Time Out, default value: 0x00 */
#define REG_MDT_RCV_TIMEOUT			0x0584

/* MDT Transmit Time Out, default value: 0x00 */
#define REG_MDT_XMIT_TIMEOUT			0x0585

/* MDT Receive Control, default value: 0x00 */
#define REG_MDT_RCV_CTRL			0x0586
#define BIT_MDT_RCV_CTRL_MDT_RCV_EN		BIT(7)
#define BIT_MDT_RCV_CTRL_MDT_DELAY_RCV_EN	BIT(6)
#define BIT_MDT_RCV_CTRL_MDT_RFIFO_OVER_WR_EN	BIT(4)
#define BIT_MDT_RCV_CTRL_MDT_XFIFO_OVER_WR_EN	BIT(3)
#define BIT_MDT_RCV_CTRL_MDT_DISABLE		BIT(2)
#define BIT_MDT_RCV_CTRL_MDT_RFIFO_CLR_ALL	BIT(1)
#define BIT_MDT_RCV_CTRL_MDT_RFIFO_CLR_CUR	BIT(0)

/* MDT Receive Read Port, default value: 0x00 */
#define REG_MDT_RCV_READ_PORT			0x0587

/* MDT Transmit Control, default value: 0x70 */
#define REG_MDT_XMIT_CTRL			0x0588
#define BIT_MDT_XMIT_CTRL_EN			BIT(7)
#define BIT_MDT_XMIT_CTRL_CMD_MERGE_EN		BIT(6)
#define BIT_MDT_XMIT_CTRL_FIXED_BURST_LEN	BIT(5)
#define BIT_MDT_XMIT_CTRL_FIXED_AID		BIT(4)
#define BIT_MDT_XMIT_CTRL_SINGLE_RUN_EN		BIT(3)
#define BIT_MDT_XMIT_CTRL_CLR_ABORT_WAIT	BIT(2)
#define BIT_MDT_XMIT_CTRL_XFIFO_CLR_ALL		BIT(1)
#define BIT_MDT_XMIT_CTRL_XFIFO_CLR_CUR		BIT(0)

/* MDT Receive WRITE Port, default value: 0x00 */
#define REG_MDT_XMIT_WRITE_PORT			0x0589

/* MDT RFIFO Status, default value: 0x00 */
#define REG_MDT_RFIFO_STAT			0x058a
#define MSK_MDT_RFIFO_STAT_MDT_RFIFO_CNT	0xe0
#define MSK_MDT_RFIFO_STAT_MDT_RFIFO_CUR_BYTE_CNT 0x1f

/* MDT XFIFO Status, default value: 0x80 */
#define REG_MDT_XFIFO_STAT			0x058b
#define MSK_MDT_XFIFO_STAT_MDT_XFIFO_LEVEL_AVAIL 0xe0
#define BIT_MDT_XFIFO_STAT_MDT_XMIT_PRE_HS_EN	BIT(4)
#define MSK_MDT_XFIFO_STAT_MDT_WRITE_BURST_LEN	0x0f

/* MDT Interrupt 0, default value: 0x0c */
#define REG_MDT_INT_0				0x058c
#define BIT_MDT_RFIFO_DATA_RDY			BIT(0)
#define BIT_MDT_IDLE_AFTER_HAWB_DISABLE		BIT(2)
#define BIT_MDT_XFIFO_EMPTY			BIT(3)

/* MDT Interrupt 0 Mask, default value: 0x00 */
#define REG_MDT_INT_0_MASK			0x058d

/* MDT Interrupt 1, default value: 0x00 */
#define REG_MDT_INT_1				0x058e
#define BIT_MDT_RCV_TIMEOUT			BIT(0)
#define BIT_MDT_RCV_SM_ABORT_PKT_RCVD		BIT(1)
#define BIT_MDT_RCV_SM_ERROR			BIT(2)
#define BIT_MDT_XMIT_TIMEOUT			BIT(5)
#define BIT_MDT_XMIT_SM_ABORT_PKT_RCVD		BIT(6)
#define BIT_MDT_XMIT_SM_ERROR			BIT(7)

/* MDT Interrupt 1 Mask, default value: 0x00 */
#define REG_MDT_INT_1_MASK			0x058f

/* CBUS Vendor ID, default value: 0x01 */
#define REG_CBUS_VENDOR_ID			0x0590

/* CBUS Connection Status, default value: 0x00 */
#define REG_CBUS_STATUS				0x0591
#define BIT_CBUS_STATUS_MHL_CABLE_PRESENT	BIT(4)
#define BIT_CBUS_STATUS_MSC_HB_SUCCESS		BIT(3)
#define BIT_CBUS_STATUS_CBUS_HPD		BIT(2)
#define BIT_CBUS_STATUS_MHL_MODE		BIT(1)
#define BIT_CBUS_STATUS_CBUS_CONNECTED		BIT(0)

/* CBUS Interrupt 1st, default value: 0x00 */
#define REG_CBUS_INT_0				0x0592
#define BIT_CBUS_MSC_MT_DONE_NACK		BIT(7)
#define BIT_CBUS_MSC_MR_SET_INT			BIT(6)
#define BIT_CBUS_MSC_MR_WRITE_BURST		BIT(5)
#define BIT_CBUS_MSC_MR_MSC_MSG			BIT(4)
#define BIT_CBUS_MSC_MR_WRITE_STAT		BIT(3)
#define BIT_CBUS_HPD_CHG			BIT(2)
#define BIT_CBUS_MSC_MT_DONE			BIT(1)
#define BIT_CBUS_CNX_CHG			BIT(0)

/* CBUS Interrupt Mask 1st, default value: 0x00 */
#define REG_CBUS_INT_0_MASK			0x0593

/* CBUS Interrupt 2nd, default value: 0x00 */
#define REG_CBUS_INT_1				0x0594
#define BIT_CBUS_CMD_ABORT			BIT(6)
#define BIT_CBUS_MSC_ABORT_RCVD			BIT(3)
#define BIT_CBUS_DDC_ABORT			BIT(2)
#define BIT_CBUS_CEC_ABORT			BIT(1)

/* CBUS Interrupt Mask 2nd, default value: 0x00 */
#define REG_CBUS_INT_1_MASK			0x0595

/* CBUS DDC Abort Interrupt, default value: 0x00 */
#define REG_DDC_ABORT_INT			0x0598

/* CBUS DDC Abort Interrupt Mask, default value: 0x00 */
#define REG_DDC_ABORT_INT_MASK			0x0599

/* CBUS MSC Requester Abort Interrupt, default value: 0x00 */
#define REG_MSC_MT_ABORT_INT			0x059a

/* CBUS MSC Requester Abort Interrupt Mask, default value: 0x00 */
#define REG_MSC_MT_ABORT_INT_MASK		0x059b

/* CBUS MSC Responder Abort Interrupt, default value: 0x00 */
#define REG_MSC_MR_ABORT_INT			0x059c

/* CBUS MSC Responder Abort Interrupt Mask, default value: 0x00 */
#define REG_MSC_MR_ABORT_INT_MASK		0x059d

/* CBUS RX DISCOVERY interrupt, default value: 0x00 */
#define REG_CBUS_RX_DISC_INT0			0x059e

/* CBUS RX DISCOVERY Interrupt Mask, default value: 0x00 */
#define REG_CBUS_RX_DISC_INT0_MASK		0x059f

/* CBUS_Link_Layer Control #8, default value: 0x00 */
#define REG_CBUS_LINK_CTRL_8			0x05a7

/* MDT State Machine Status, default value: 0x00 */
#define REG_MDT_SM_STAT				0x05b5
#define MSK_MDT_SM_STAT_MDT_RCV_STATE		0xf0
#define MSK_MDT_SM_STAT_MDT_XMIT_STATE		0x0f

/* CBUS MSC command trigger, default value: 0x00 */
#define REG_MSC_COMMAND_START			0x05b8
#define BIT_MSC_COMMAND_START_DEBUG		BIT(5)
#define BIT_MSC_COMMAND_START_WRITE_BURST	BIT(4)
#define BIT_MSC_COMMAND_START_WRITE_STAT	BIT(3)
#define BIT_MSC_COMMAND_START_READ_DEVCAP	BIT(2)
#define BIT_MSC_COMMAND_START_MSC_MSG		BIT(1)
#define BIT_MSC_COMMAND_START_PEER		BIT(0)

/* CBUS MSC Command/Offset, default value: 0x00 */
#define REG_MSC_CMD_OR_OFFSET			0x05b9

/* CBUS MSC Transmit Data */
#define REG_MSC_1ST_TRANSMIT_DATA		0x05ba
#define REG_MSC_2ND_TRANSMIT_DATA		0x05bb

/* CBUS MSC Requester Received Data */
#define REG_MSC_MT_RCVD_DATA0			0x05bc
#define REG_MSC_MT_RCVD_DATA1			0x05bd

/* CBUS MSC Responder MSC_MSG Received Data */
#define REG_MSC_MR_MSC_MSG_RCVD_1ST_DATA	0x05bf
#define REG_MSC_MR_MSC_MSG_RCVD_2ND_DATA	0x05c0

/* CBUS MSC Heartbeat Control, default value: 0x27 */
#define REG_MSC_HEARTBEAT_CTRL			0x05c4
#define BIT_MSC_HEARTBEAT_CTRL_MSC_HB_EN	BIT(7)
#define MSK_MSC_HEARTBEAT_CTRL_MSC_HB_FAIL_LIMIT 0x70
#define MSK_MSC_HEARTBEAT_CTRL_MSC_HB_PERIOD_MSB 0x0f

/* CBUS MSC Compatibility Control, default value: 0x02 */
#define REG_CBUS_MSC_COMPAT_CTRL		0x05c7
#define BIT_CBUS_MSC_COMPAT_CTRL_XDEVCAP_EN	BIT(7)
#define BIT_CBUS_MSC_COMPAT_CTRL_DISABLE_MSC_ON_CBUS BIT(6)
#define BIT_CBUS_MSC_COMPAT_CTRL_DISABLE_DDC_ON_CBUS BIT(5)
#define BIT_CBUS_MSC_COMPAT_CTRL_DISABLE_GET_DDC_ERRORCODE BIT(3)
#define BIT_CBUS_MSC_COMPAT_CTRL_DISABLE_GET_VS1_ERRORCODE BIT(2)

/* CBUS3 Converter Control, default value: 0x24 */
#define REG_CBUS3_CNVT				0x05dc
#define MSK_CBUS3_CNVT_CBUS3_RETRYLMT		0xf0
#define MSK_CBUS3_CNVT_CBUS3_PEERTOUT_SEL	0x0c
#define BIT_CBUS3_CNVT_TEARCBUS_EN		BIT(1)
#define BIT_CBUS3_CNVT_CBUS3CNVT_EN		BIT(0)

/* Discovery Control1, default value: 0x24 */
#define REG_DISC_CTRL1				0x05e0
#define BIT_DISC_CTRL1_CBUS_INTR_EN		BIT(7)
#define BIT_DISC_CTRL1_HB_ONLY			BIT(6)
#define MSK_DISC_CTRL1_DISC_ATT			0x30
#define MSK_DISC_CTRL1_DISC_CYC			0x0c
#define BIT_DISC_CTRL1_DISC_EN			BIT(0)

#define VAL_PUP_OFF				0
#define VAL_PUP_20K				1
#define VAL_PUP_5K				2

/* Discovery Control4, default value: 0x80 */
#define REG_DISC_CTRL4				0x05e3
#define MSK_DISC_CTRL4_CBUSDISC_PUP_SEL		0xc0
#define MSK_DISC_CTRL4_CBUSIDLE_PUP_SEL		0x30
#define VAL_DISC_CTRL4(pup_disc, pup_idle) (((pup_disc) << 6) | (pup_idle << 4))

/* Discovery Control5, default value: 0x03 */
#define REG_DISC_CTRL5				0x05e4
#define BIT_DISC_CTRL5_DSM_OVRIDE		BIT(3)
#define MSK_DISC_CTRL5_CBUSMHL_PUP_SEL		0x03

/* Discovery Control8, default value: 0x81 */
#define REG_DISC_CTRL8				0x05e7
#define BIT_DISC_CTRL8_NOMHLINT_CLR_BYPASS	BIT(7)
#define BIT_DISC_CTRL8_DELAY_CBUS_INTR_EN	BIT(0)

/* Discovery Control9, default value: 0x54 */
#define REG_DISC_CTRL9			0x05e8
#define BIT_DISC_CTRL9_MHL3_RSEN_BYP		BIT(7)
#define BIT_DISC_CTRL9_MHL3DISC_EN		BIT(6)
#define BIT_DISC_CTRL9_WAKE_DRVFLT		BIT(4)
#define BIT_DISC_CTRL9_NOMHL_EST		BIT(3)
#define BIT_DISC_CTRL9_DISC_PULSE_PROCEED	BIT(2)
#define BIT_DISC_CTRL9_WAKE_PULSE_BYPASS	BIT(1)
#define BIT_DISC_CTRL9_VBUS_OUTPUT_CAPABILITY_SRC BIT(0)

/* Discovery Status1, default value: 0x00 */
#define REG_DISC_STAT1				0x05eb
#define BIT_DISC_STAT1_PSM_OVRIDE		BIT(5)
#define MSK_DISC_STAT1_DISC_SM			0x0f

/* Discovery Status2, default value: 0x00 */
#define REG_DISC_STAT2				0x05ec
#define BIT_DISC_STAT2_CBUS_OE_POL		BIT(6)
#define BIT_DISC_STAT2_CBUS_SATUS		BIT(5)
#define BIT_DISC_STAT2_RSEN			BIT(4)

#define MSK_DISC_STAT2_MHL_VRSN			0x0c
#define VAL_DISC_STAT2_DEFAULT			0x00
#define VAL_DISC_STAT2_MHL1_2			0x04
#define VAL_DISC_STAT2_MHL3			0x08
#define VAL_DISC_STAT2_RESERVED			0x0c

#define MSK_DISC_STAT2_RGND			0x03
#define VAL_RGND_OPEN				0x00
#define VAL_RGND_2K				0x01
#define VAL_RGND_1K				0x02
#define VAL_RGND_SHORT				0x03

/* Interrupt CBUS_reg1 INTR0, default value: 0x00 */
#define REG_CBUS_DISC_INTR0			0x05ed
#define BIT_RGND_READY_INT			BIT(6)
#define BIT_CBUS_MHL12_DISCON_INT		BIT(5)
#define BIT_CBUS_MHL3_DISCON_INT		BIT(4)
#define BIT_NOT_MHL_EST_INT			BIT(3)
#define BIT_MHL_EST_INT				BIT(2)
#define BIT_MHL3_EST_INT			BIT(1)
#define VAL_CBUS_MHL_DISCON (BIT_CBUS_MHL12_DISCON_INT \
			    | BIT_CBUS_MHL3_DISCON_INT \
			    | BIT_NOT_MHL_EST_INT)

/* Interrupt CBUS_reg1 INTR0 Mask, default value: 0x00 */
#define REG_CBUS_DISC_INTR0_MASK		0x05ee

#endif /* __SIL_SII8620_H__ */
