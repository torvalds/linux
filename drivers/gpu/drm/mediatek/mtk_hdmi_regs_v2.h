/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 MediaTek Inc.
 * Copyright (c) 2021 BayLibre, SAS
 * Copyright (c) 2024 Collabora Ltd.
 *                    AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#ifndef _MTK_HDMI_REGS_H
#define _MTK_HDMI_REGS_H

/* HDMI_TOP Config */
#define TOP_CFG00			0x000
#define  HDMI2_ON			BIT(2)
#define  HDMI_MODE_HDMI			BIT(3)
#define  SCR_ON				BIT(4)
#define  TMDS_PACK_MODE			GENMASK(9, 8)
#define   TMDS_PACK_MODE_8BPP		0
#define   TMDS_PACK_MODE_10BPP		1
#define   TMDS_PACK_MODE_12BPP		2
#define   TMDS_PACK_MODE_16BPP		3
#define  DEEPCOLOR_PKT_EN		BIT(12)
#define  HDMI_ABIST_VIDEO_FORMAT	GENMASK(21, 16)
#define  HDMI_ABIST_ENABLE		BIT(31)
#define TOP_CFG01 0x004
#define  CP_SET_MUTE_EN			BIT(0)
#define  CP_CLR_MUTE_EN			BIT(1)
#define  NULL_PKT_EN			BIT(2)
#define  NULL_PKT_VSYNC_HIGH_EN		BIT(3)

/* HDMI_TOP Audio: Channel Mapping */
#define TOP_AUD_MAP			0x00c
#define  SD0_MAP			GENMASK(2, 0)
#define  SD1_MAP			GENMASK(6, 4)
#define  SD2_MAP			GENMASK(10, 8)
#define  SD3_MAP			GENMASK(14, 12)
#define  SD4_MAP			GENMASK(18, 16)
#define  SD5_MAP			GENMASK(22, 20)
#define  SD6_MAP			GENMASK(26, 24)
#define  SD7_MAP			GENMASK(30, 28)

/* Auxiliary Video Information (AVI) Infoframe */
#define TOP_AVI_HEADER			0x024
#define TOP_AVI_PKT00			0x028
#define TOP_AVI_PKT01			0x02C
#define TOP_AVI_PKT02			0x030
#define TOP_AVI_PKT03			0x034
#define TOP_AVI_PKT04			0x038
#define TOP_AVI_PKT05			0x03C

/* Audio Interface Infoframe */
#define TOP_AIF_HEADER			0x040
#define TOP_AIF_PKT00			0x044
#define TOP_AIF_PKT01			0x048
#define TOP_AIF_PKT02			0x04c
#define TOP_AIF_PKT03			0x050

/* Audio SPDIF Infoframe */
#define TOP_SPDIF_HEADER		0x054
#define TOP_SPDIF_PKT00			0x058
#define TOP_SPDIF_PKT01			0x05c
#define TOP_SPDIF_PKT02			0x060
#define TOP_SPDIF_PKT03			0x064
#define TOP_SPDIF_PKT04			0x068
#define TOP_SPDIF_PKT05			0x06c
#define TOP_SPDIF_PKT06			0x070
#define TOP_SPDIF_PKT07			0x074

/* Infoframes Configuration */
#define TOP_INFO_EN			0x01c
#define  AVI_EN				BIT(0)
#define  SPD_EN				BIT(1)
#define  AUD_EN				BIT(2)
#define  CP_EN				BIT(5)
#define  VSIF_EN			BIT(11)
#define  AVI_EN_WR			BIT(16)
#define  SPD_EN_WR			BIT(17)
#define  AUD_EN_WR			BIT(18)
#define  CP_EN_WR			BIT(21)
#define  VSIF_EN_WR			BIT(27)
#define TOP_INFO_RPT			0x020
#define  AVI_RPT_EN			BIT(0)
#define  SPD_RPT_EN			BIT(1)
#define  AUD_RPT_EN			BIT(2)
#define  CP_RPT_EN			BIT(5)
#define  VSIF_RPT_EN			BIT(11)

/* Vendor Specific Infoframe */
#define TOP_VSIF_HEADER			0x174
#define TOP_VSIF_PKT00			0x178
#define TOP_VSIF_PKT01			0x17c
#define TOP_VSIF_PKT02			0x180
#define TOP_VSIF_PKT03			0x184
#define TOP_VSIF_PKT04			0x188
#define TOP_VSIF_PKT05			0x18c
#define TOP_VSIF_PKT06			0x190
#define TOP_VSIF_PKT07			0x194

/* HDMI_TOP Misc */
#define TOP_MISC_CTLR			0x1a4
#define  DEEP_COLOR_ADD			BIT(4)

/* Hardware interrupts */
#define TOP_INT_STA00			0x1a8
#define TOP_INT_ENABLE00		0x1b0
#define  HTPLG_R_INT			BIT(0)
#define  HTPLG_F_INT			BIT(1)
#define  PORD_R_INT			BIT(2)
#define  PORD_F_INT			BIT(3)
#define  HDMI_VSYNC_INT			BIT(4)
#define  HDMI_AUDIO_INT			BIT(5)
#define  HDCP2X_RX_REAUTH_REQ_DDCM_INT	BIT(25)
#define TOP_INT_ENABLE01		0x1b4
#define TOP_INT_CLR00			0x1b8
#define TOP_INT_CLR01			0x1bc


/* Video Mute */
#define TOP_VMUTE_CFG1			0x1c8
#define  REG_VMUTE_EN			BIT(16)

/* HDMI Audio IP */
#define AIP_CTRL			0x400
#define  CTS_SW_SEL			BIT(0)
#define  CTS_REQ_EN			BIT(1)
#define  MCLK_EN			BIT(2)
#define  NO_MCLK_CTSGEN_SEL		BIT(3)
#define  AUD_IN_EN			BIT(8)
#define  AUD_SEL_OWRT			BIT(9)
#define  SPDIF_EN			BIT(13)
#define  HBRA_ON			BIT(14)
#define  DSD_EN				BIT(15)
#define  I2S_EN				GENMASK(19, 16)
#define  HBR_FROM_SPDIF			BIT(20)
#define  CTS_CAL_N4			BIT(23)
#define  SPDIF_INTERNAL_MODULE		BIT(24)
#define AIP_N_VAL			0x404
#define AIP_CTS_SVAL			0x408
#define AIP_SPDIF_CTRL			0x40c
#define  WR_1UI_LOCK			BIT(0)
#define  FS_OVERRIDE_WRITE		BIT(1)
#define  WR_2UI_LOCK			BIT(2)
#define  MAX_1UI_WRITE			GENMASK(15, 8)
#define  MAX_2UI_SPDIF_WRITE		GENMASK(23, 16)
#define  MAX_2UI_I2S_HI_WRITE		GENMASK(23, 20)
#define   MAX_2UI_I2S_LFE_CC_SWAP	BIT(1)
#define  MAX_2UI_I2S_LO_WRITE		GENMASK(19, 16)
#define  AUD_ERR_THRESH			GENMASK(29, 24)
#define  I2S2DSD_EN			BIT(30)
#define AIP_I2S_CTRL			0x410
#define  FIFO0_MAP			GENMASK(1, 0)
#define  FIFO1_MAP			GENMASK(3, 2)
#define  FIFO2_MAP			GENMASK(5, 4)
#define  FIFO3_MAP			GENMASK(7, 6)
#define  I2S_1ST_BIT_NOSHIFT		BIT(8)
#define  I2S_DATA_DIR_LSB		BIT(9)
#define  JUSTIFY_RIGHT			BIT(10)
#define  WS_HIGH			BIT(11)
#define  VBIT_COMPRESSED		BIT(12)
#define  CBIT_ORDER_SAME		BIT(13)
#define  SCK_EDGE_RISE			BIT(14)
#define AIP_I2S_CHST0			0x414
#define AIP_I2S_CHST1			0x418
#define AIP_TXCTRL			0x424
#define  RST4AUDIO			BIT(0)
#define  RST4AUDIO_FIFO			BIT(1)
#define  RST4AUDIO_ACR			BIT(2)
#define  AUD_LAYOUT_1			BIT(4)
#define  AUD_MUTE_FIFO_EN		BIT(5)
#define  AUD_PACKET_DROP		BIT(6)
#define  DSD_MUTE_EN			BIT(7)
#define AIP_TPI_CTRL			0x428
#define  TPI_AUDIO_LOOKUP_EN		BIT(2)

/* Video downsampling configuration */
#define VID_DOWNSAMPLE_CONFIG		0x8d0
#define  C444_C422_CONFIG_ENABLE	BIT(0)
#define  C422_C420_CONFIG_ENABLE	BIT(4)
#define  C422_C420_CONFIG_BYPASS	BIT(5)
#define  C422_C420_CONFIG_OUT_CB_OR_CR	BIT(6)
#define VID_OUT_FORMAT			0x8fc
#define  OUTPUT_FORMAT_DEMUX_420_ENABLE	BIT(10)

/* HDCP registers */
#define HDCP_TOP_CTRL			0xc00
#define HDCP2X_CTRL_0			0xc20
#define  HDCP2X_EN			BIT(0)
#define  HDCP2X_ENCRYPT_EN		BIT(7)
#define  HDCP2X_HPD_OVR			BIT(10)
#define  HDCP2X_HPD_SW			BIT(11)
#define HDCP2X_POL_CTRL			0xc54
#define  HDCP2X_DIS_POLL_EN		BIT(16)
#define HDCP1X_CTRL			0xcd0
#define  HDCP1X_ENC_EN			BIT(6)

/* HDMI DDC registers */
#define HPD_DDC_CTRL			0xc08
#define  HPD_DDC_DELAY_CNT		GENMASK(31, 16)
#define  HPD_DDC_HPD_DBNC_EN		BIT(2)
#define  HPD_DDC_PORD_DBNC_EN		BIT(3)
#define DDC_CTRL			0xc10
#define  DDC_CTRL_ADDR			GENMASK(7, 1)
#define  DDC_CTRL_OFFSET		GENMASK(15, 8)
#define  DDC_CTRL_DIN_CNT		GENMASK(25, 16)
#define  DDC_CTRL_CMD			GENMASK(31, 28)
#define SCDC_CTRL			0xc18
#define  SCDC_DDC_SEGMENT		GENMASK(15, 8)
#define HPD_DDC_STATUS			0xc60
#define  HPD_STATE			GENMASK(1, 0)
#define   HPD_STATE_CONNECTED		2
#define  HPD_PIN_STA			BIT(4)
#define  PORD_PIN_STA			BIT(5)
#define  DDC_I2C_IN_PROG		BIT(13)
#define  DDC_DATA_OUT			GENMASK(23, 16)
#define SI2C_CTRL			0xcac
#define  SI2C_WR			BIT(0)
#define  SI2C_RD			BIT(1)
#define  SI2C_CONFIRM_READ		BIT(2)
#define  SI2C_WDATA			GENMASK(15, 8)
#define  SI2C_ADDR			GENMASK(23, 16)

/* HDCP DDC registers */
#define HDCP2X_DDCM_STATUS		0xc68
#define  DDC_I2C_NO_ACK			BIT(10)
#define  DDC_I2C_BUS_LOW		BIT(11)

/* HDMI TX registers */
#define HDMITX_CONFIG_MT8188		0xea0
#define HDMITX_CONFIG_MT8195		0x900
#define  HDMI_YUV420_MODE		BIT(10)
#define  HDMITX_SW_HPD			BIT(29)
#define  HDMITX_SW_RSTB			BIT(31)

/**
 * enum mtk_hdmi_ddc_v2_cmds - DDC_CMD register commands
 * @DDC_CMD_READ_NOACK:      Current address read with no ACK on last byte
 * @DDC_CMD_READ:            Current address read with ACK on last byte
 * @DDC_CMD_SEQ_READ_NOACK:  Sequential read with no ACK on last byte
 * @DDC_CMD_SEQ_READ:        Sequential read with ACK on last byte
 * @DDC_CMD_ENH_READ_NOACK:  Enhanced read with no ACK on last byte
 * @DDC_CMD_ENH_READ:        Enhanced read with ACK on last byte
 * @DDC_CMD_SEQ_WRITE_NOACK: Sequential write ignoring ACK on last byte
 * @DDC_CMD_SEQ_WRITE:       Sequential write requiring ACK on last byte
 * @DDC_CMD_RSVD:            Reserved for future use
 * @DDC_CMD_CLEAR_FIFO:      Clear DDC I2C FIFO
 * @DDC_CMD_CLOCK_SCL:       Start clocking DDC I2C SCL
 * @DDC_CMD_ABORT_XFER:      Abort DDC I2C transaction
 */
enum mtk_hdmi_ddc_v2_cmds {
	DDC_CMD_READ_NOACK = 0x0,
	DDC_CMD_READ,
	DDC_CMD_SEQ_READ_NOACK,
	DDC_CMD_SEQ_READ,
	DDC_CMD_ENH_READ_NOACK,
	DDC_CMD_ENH_READ,
	DDC_CMD_SEQ_WRITE_NOACK,
	DDC_CMD_SEQ_WRITE = 0x07,
	DDC_CMD_CLEAR_FIFO = 0x09,
	DDC_CMD_CLOCK_SCL = 0x0a,
	DDC_CMD_ABORT_XFER = 0x0f
};

#endif /* _MTK_HDMI_REGS_H */
