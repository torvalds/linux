// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 *
 * Author: Guochun Huang <hero.huang@rock-chips.com>
 */

#include <asm/unaligned.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include "rkx120_reg.h"
#include "rkx120_dsi_tx.h"
#include "serdes_combphy.h"

#define REG(x)				(x + RKX120_DSI_TX_BASE)
#define DSI_VERSION			REG(0x0000)
#define DSI_PWR_UP			REG(0x0004)
#define RESET				0
#define POWER_UP			BIT(0)
#define DSI_CLKMGR_CFG			REG(0x0008)
#define TO_CLK_DIVISION(x)		UPDATE(x, 15,  8)
#define TX_ESC_CLK_DIVISION(x)		UPDATE(x,  7,  0)
#define DSI_DPI_VCID			REG(0x000c)
#define DPI_VID(x)			UPDATE(x,  1,  0)
#define DSI_DPI_COLOR_CODING		REG(0x0010)
#define LOOSELY18_EN			BIT(8)
#define DPI_COLOR_CODING(x)		UPDATE(x,  3,  0)
#define DSI_DPI_CFG_POL			REG(0x0014)
#define COLORM_ACTIVE_LOW		BIT(4)
#define SHUTD_ACTIVE_LOW		BIT(3)
#define HSYNC_ACTIVE_LOW		BIT(2)
#define VSYNC_ACTIVE_LOW		BIT(1)
#define DATAEN_ACTIVE_LOW		BIT(0)
#define DSI_DPI_LP_CMD_TIM		REG(0x0018)
#define OUTVACT_LPCMD_TIME(x)		UPDATE(x, 23, 16)
#define INVACT_LPCMD_TIME(x)		UPDATE(x,  7,  0)
#define DSI_PCKHDL_CFG			REG(0x002c)
#define CRC_RX_EN			BIT(4)
#define ECC_RX_EN			BIT(3)
#define BTA_EN				BIT(2)
#define EOTP_RX_EN			BIT(1)
#define EOTP_TX_EN			BIT(0)
#define DSI_GEN_VCID			REG(0x0030)
#define DSI_MODE_CFG			REG(0x0034)
#define CMD_VIDEO_MODE(x)		UPDATE(x,  0,  0)
#define DSI_VID_MODE_CFG		REG(0x0038)
#define VPG_EN				BIT(16)
#define LP_CMD_EN			BIT(15)
#define FRAME_BTA_ACK_EN		BIT(14)
#define LP_HFP_EN			BIT(13)
#define LP_HBP_EN			BIT(12)
#define LP_VACT_EN			BIT(11)
#define LP_VFP_EN			BIT(10)
#define LP_VBP_EN			BIT(9)
#define LP_VSA_EN			BIT(8)
#define VID_MODE_TYPE(x)		UPDATE(x,  1,  0)
#define DSI_VID_PKT_SIZE		REG(0x003c)
#define VID_PKT_SIZE(x)			UPDATE(x, 13,  0)
#define DSI_VID_NUM_CHUNKS		REG(0x0040)
#define DSI_VID_NULL_SIZE		REG(0x0044)
#define DSI_VID_HSA_TIME		REG(0x0048)
#define VID_HSA_TIME(x)			UPDATE(x, 11,  0)
#define DSI_VID_HBP_TIME		REG(0x004c)
#define VID_HBP_TIME(x)			UPDATE(x, 11,  0)
#define DSI_VID_HLINE_TIME		REG(0x0050)
#define VID_HLINE_TIME(x)		UPDATE(x, 14,  0)
#define DSI_VID_VSA_LINES		REG(0x0054)
#define VSA_LINES(x)			UPDATE(x,  9,  0)
#define DSI_VID_VBP_LINES		REG(0x0058)
#define VBP_LINES(x)			UPDATE(x,  9,  0)
#define DSI_VID_VFP_LINES		REG(0x005c)
#define VFP_LINES(x)			UPDATE(x,  9,  0)
#define DSI_VID_VACTIVE_LINES		REG(0x0060)
#define V_ACTIVE_LINES(x)		UPDATE(x, 13,  0)
#define DSI_EDPI_CMD_SIZE		REG(0x0064)
#define EDPI_ALLOWED_CMD_SIZE(x)	UPDATE(x, 15,  0)
#define DSI_CMD_MODE_CFG		REG(0x0068)
#define MAX_RD_PKT_SIZE			BIT(24)
#define DCS_LW_TX			BIT(19)
#define DCS_SR_0P_TX			BIT(18)
#define DCS_SW_1P_TX			BIT(17)
#define DCS_SW_0P_TX			BIT(16)
#define GEN_LW_TX			BIT(14)
#define GEN_SR_2P_TX			BIT(13)
#define GEN_SR_1P_TX			BIT(12)
#define GEN_SR_0P_TX			BIT(11)
#define GEN_SW_2P_TX			BIT(10)
#define GEN_SW_1P_TX			BIT(9)
#define GEN_SW_0P_TX			BIT(8)
#define ACK_RQST_EN			BIT(1)
#define TEAR_FX_EN			BIT(0)
#define DSI_GEN_HDR			REG(0x006c)
#define GEN_WC_MSBYTE(x)		UPDATE(x, 23, 16)
#define GEN_WC_LSBYTE(x)		UPDATE(x, 15,  8)
#define GEN_VC(x)			UPDATE(x,  7,  6)
#define GEN_DT(x)			UPDATE(x,  5,  0)
#define DSI_GEN_PLD_DATA		REG(0x0070)
#define DSI_CMD_PKT_STATUS		REG(0x0074)
#define GEN_RD_CMD_BUSY			BIT(6)
#define GEN_PLD_R_FULL			BIT(5)
#define GEN_PLD_R_EMPTY			BIT(4)
#define GEN_PLD_W_FULL			BIT(3)
#define GEN_PLD_W_EMPTY			BIT(2)
#define GEN_CMD_FULL			BIT(1)
#define GEN_CMD_EMPTY			BIT(0)
#define DSI_TO_CNT_CFG			REG(0x0078)
#define HSTX_TO_CNT(x)			UPDATE(x, 31, 16)
#define LPRX_TO_CNT(x)			UPDATE(x, 15,  0)
#define DSI_HS_RD_TO_CNT		REG(0x007c)
#define HS_RD_TO_CNT(x)			UPDATE(x, 15,  0)
#define DSI_LP_RD_TO_CNT		REG(0x0080)
#define LP_RD_TO_CNT(x)			UPDATE(x, 15,  0)
#define DSI_HS_WR_TO_CNT		REG(0x0084)
#define HS_WR_TO_CNT(x)			UPDATE(x, 15,  0)
#define DSI_LP_WR_TO_CNT		REG(0x0088)
#define LP_WR_TO_CNT(x)			UPDATE(x, 15,  0)
#define DSI_BTA_TO_CNT			REG(0x008c)
#define BTA_TO_CNT(x)			UPDATE(x, 15,  0)
#define DSI_SDF_3D			REG(0x0090)
#define DSI_LPCLK_CTRL			REG(0x0094)
#define AUTO_CLKLANE_CTRL		BIT(1)
#define PHY_TXREQUESTCLKHS		BIT(0)
#define DSI_PHY_TMR_LPCLK_CFG		REG(0x0098)
#define PHY_CLKHS2LP_TIME(x)		UPDATE(x, 25, 16)
#define PHY_CLKLP2HS_TIME(x)		UPDATE(x,  9,  0)
#define DSI_PHY_TMR_CFG			REG(0x009c)
#define PHY_HS2LP_TIME(x)		UPDATE(x, 31, 24)
#define PHY_LP2HS_TIME(x)		UPDATE(x, 23, 16)
#define MAX_RD_TIME(x)			UPDATE(x, 14,  0)
#define DSI_PHY_RSTZ			REG(0x00a0)
#define PHY_FORCEPLL			BIT(3)
#define PHY_ENABLECLK			BIT(2)
#define PHY_RSTZ			BIT(1)
#define PHY_SHUTDOWNZ			BIT(0)
#define DSI_PHY_IF_CFG			REG(0x00a4)
#define PHY_STOP_WAIT_TIME(x)		UPDATE(x, 15,  8)
#define N_LANES(x)			UPDATE(x,  1,  0)
#define DSI_PHY_STATUS			REG(0x00b0)
#define PHY_STOPSTATE3LANE		BIT(11)
#define PHY_STOPSTATE2LANE		BIT(9)
#define PHY_STOPSTATE1LANE		BIT(7)
#define PHY_STOPSTATE0LANE		BIT(4)
#define PHY_STOPSTATECLKLANE		BIT(2)
#define PHY_LOCK			BIT(0)
#define PHY_STOPSTATELANE		(PHY_STOPSTATE0LANE | \
					 PHY_STOPSTATECLKLANE)
#define DSI_INT_ST0			REB(0x00bc)
#define DSI_INT_ST1			REB(0x00c0)
#define DSI_INT_MSK0			REB(0x00c4)
#define DSI_INT_MSK1			REB(0x00c8)
#define DSI_INT_FORCE0			REB(0x00d8)
#define DSI_INT_FORCE1			REB(0x00dc)
#define DSI_MAX_REGISTER		DSI_INT_FORCE1

/* request ACK from peripheral */
#define MIPI_DSI_MSG_REQ_ACK	BIT(0)
/* use Low Power Mode to transmit message */
#define MIPI_DSI_MSG_USE_LPM	BIT(1)

#define DISPLAY_FLAGS_HSYNC_LOW			BIT(0)
#define DISPLAY_FLAGS_HSYNC_HIGH		BIT(1)
#define DISPLAY_FLAGS_VSYNC_LOW			BIT(2)
#define DISPLAY_FLAGS_VSYNC_HIGH		BIT(3)

static u64 lane_kbps;

enum vid_mode_type {
	VIDEO_MODE,
	COMMAND_MODE,
};

enum dpi_color_coding {
	DPI_COLOR_CODING_16BIT_1,
	DPI_COLOR_CODING_16BIT_2,
	DPI_COLOR_CODING_16BIT_3,
	DPI_COLOR_CODING_18BIT_1,
	DPI_COLOR_CODING_18BIT_2,
	DPI_COLOR_CODING_24BIT,
};

enum {
	VID_MODE_TYPE_NON_BURST_SYNC_PULSES,
	VID_MODE_TYPE_NON_BURST_SYNC_EVENTS,
	VID_MODE_TYPE_BURST,
};

/* MIPI DSI Processor-to-Peripheral transaction types */
enum {
	MIPI_DSI_V_SYNC_START				= 0x01,
	MIPI_DSI_V_SYNC_END				= 0x11,
	MIPI_DSI_H_SYNC_START				= 0x21,
	MIPI_DSI_H_SYNC_END				= 0x31,

	MIPI_DSI_COLOR_MODE_OFF				= 0x02,
	MIPI_DSI_COLOR_MODE_ON				= 0x12,
	MIPI_DSI_SHUTDOWN_PERIPHERAL			= 0x22,
	MIPI_DSI_TURN_ON_PERIPHERAL			= 0x32,

	MIPI_DSI_GENERIC_SHORT_WRITE_0_PARAM		= 0x03,
	MIPI_DSI_GENERIC_SHORT_WRITE_1_PARAM		= 0x13,
	MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM		= 0x23,

	MIPI_DSI_GENERIC_READ_REQUEST_0_PARAM		= 0x04,
	MIPI_DSI_GENERIC_READ_REQUEST_1_PARAM		= 0x14,
	MIPI_DSI_GENERIC_READ_REQUEST_2_PARAM		= 0x24,

	MIPI_DSI_DCS_SHORT_WRITE			= 0x05,
	MIPI_DSI_DCS_SHORT_WRITE_PARAM			= 0x15,

	MIPI_DSI_DCS_READ				= 0x06,

	MIPI_DSI_DCS_COMPRESSION_MODE                   = 0x07,
	MIPI_DSI_PPS_LONG_WRITE                         = 0x0A,

	MIPI_DSI_SET_MAXIMUM_RETURN_PACKET_SIZE		= 0x37,

	MIPI_DSI_END_OF_TRANSMISSION			= 0x08,

	MIPI_DSI_NULL_PACKET				= 0x09,
	MIPI_DSI_BLANKING_PACKET			= 0x19,
	MIPI_DSI_GENERIC_LONG_WRITE			= 0x29,
	MIPI_DSI_DCS_LONG_WRITE				= 0x39,

	MIPI_DSI_LOOSELY_PACKED_PIXEL_STREAM_YCBCR20	= 0x0c,
	MIPI_DSI_PACKED_PIXEL_STREAM_YCBCR24		= 0x1c,
	MIPI_DSI_PACKED_PIXEL_STREAM_YCBCR16		= 0x2c,

	MIPI_DSI_PACKED_PIXEL_STREAM_30			= 0x0d,
	MIPI_DSI_PACKED_PIXEL_STREAM_36			= 0x1d,
	MIPI_DSI_PACKED_PIXEL_STREAM_YCBCR12		= 0x3d,

	MIPI_DSI_PACKED_PIXEL_STREAM_16			= 0x0e,
	MIPI_DSI_PACKED_PIXEL_STREAM_18			= 0x1e,
	MIPI_DSI_PIXEL_STREAM_3BYTE_18			= 0x2e,
	MIPI_DSI_PACKED_PIXEL_STREAM_24			= 0x3e,
};

/* MIPI DSI Peripheral-to-Processor transaction types */
enum {
	MIPI_DSI_RX_ACKNOWLEDGE_AND_ERROR_REPORT	= 0x02,
	MIPI_DSI_RX_END_OF_TRANSMISSION			= 0x08,
	MIPI_DSI_RX_GENERIC_SHORT_READ_RESPONSE_1BYTE	= 0x11,
	MIPI_DSI_RX_GENERIC_SHORT_READ_RESPONSE_2BYTE	= 0x12,
	MIPI_DSI_RX_GENERIC_LONG_READ_RESPONSE		= 0x1a,
	MIPI_DSI_RX_DCS_LONG_READ_RESPONSE		= 0x1c,
	MIPI_DSI_RX_DCS_SHORT_READ_RESPONSE_1BYTE	= 0x21,
	MIPI_DSI_RX_DCS_SHORT_READ_RESPONSE_2BYTE	= 0x22,
};

/* MIPI DCS commands */
enum {
	MIPI_DCS_NOP			= 0x00,
	MIPI_DCS_SOFT_RESET		= 0x01,
	MIPI_DCS_GET_DISPLAY_ID		= 0x04,
	MIPI_DCS_GET_RED_CHANNEL	= 0x06,
	MIPI_DCS_GET_GREEN_CHANNEL	= 0x07,
	MIPI_DCS_GET_BLUE_CHANNEL	= 0x08,
	MIPI_DCS_GET_DISPLAY_STATUS	= 0x09,
	MIPI_DCS_GET_POWER_MODE		= 0x0A,
	MIPI_DCS_GET_ADDRESS_MODE	= 0x0B,
	MIPI_DCS_GET_PIXEL_FORMAT	= 0x0C,
	MIPI_DCS_GET_DISPLAY_MODE	= 0x0D,
	MIPI_DCS_GET_SIGNAL_MODE	= 0x0E,
	MIPI_DCS_GET_DIAGNOSTIC_RESULT	= 0x0F,
	MIPI_DCS_ENTER_SLEEP_MODE	= 0x10,
	MIPI_DCS_EXIT_SLEEP_MODE	= 0x11,
	MIPI_DCS_ENTER_PARTIAL_MODE	= 0x12,
	MIPI_DCS_ENTER_NORMAL_MODE	= 0x13,
	MIPI_DCS_EXIT_INVERT_MODE	= 0x20,
	MIPI_DCS_ENTER_INVERT_MODE	= 0x21,
	MIPI_DCS_SET_GAMMA_CURVE	= 0x26,
	MIPI_DCS_SET_DISPLAY_OFF	= 0x28,
	MIPI_DCS_SET_DISPLAY_ON		= 0x29,
	MIPI_DCS_SET_COLUMN_ADDRESS	= 0x2A,
	MIPI_DCS_SET_PAGE_ADDRESS	= 0x2B,
	MIPI_DCS_WRITE_MEMORY_START	= 0x2C,
	MIPI_DCS_WRITE_LUT		= 0x2D,
	MIPI_DCS_READ_MEMORY_START	= 0x2E,
	MIPI_DCS_SET_PARTIAL_AREA	= 0x30,
	MIPI_DCS_SET_SCROLL_AREA	= 0x33,
	MIPI_DCS_SET_TEAR_OFF		= 0x34,
	MIPI_DCS_SET_TEAR_ON		= 0x35,
	MIPI_DCS_SET_ADDRESS_MODE	= 0x36,
	MIPI_DCS_SET_SCROLL_START	= 0x37,
	MIPI_DCS_EXIT_IDLE_MODE		= 0x38,
	MIPI_DCS_ENTER_IDLE_MODE	= 0x39,
	MIPI_DCS_SET_PIXEL_FORMAT	= 0x3A,
	MIPI_DCS_WRITE_MEMORY_CONTINUE	= 0x3C,
	MIPI_DCS_READ_MEMORY_CONTINUE	= 0x3E,
	MIPI_DCS_SET_TEAR_SCANLINE	= 0x44,
	MIPI_DCS_GET_SCANLINE		= 0x45,
	MIPI_DCS_SET_DISPLAY_BRIGHTNESS = 0x51,		/* MIPI DCS 1.3 */
	MIPI_DCS_GET_DISPLAY_BRIGHTNESS = 0x52,		/* MIPI DCS 1.3 */
	MIPI_DCS_WRITE_CONTROL_DISPLAY  = 0x53,		/* MIPI DCS 1.3 */
	MIPI_DCS_GET_CONTROL_DISPLAY	= 0x54,		/* MIPI DCS 1.3 */
	MIPI_DCS_WRITE_POWER_SAVE	= 0x55,		/* MIPI DCS 1.3 */
	MIPI_DCS_GET_POWER_SAVE		= 0x56,		/* MIPI DCS 1.3 */
	MIPI_DCS_SET_CABC_MIN_BRIGHTNESS = 0x5E,	/* MIPI DCS 1.3 */
	MIPI_DCS_GET_CABC_MIN_BRIGHTNESS = 0x5F,	/* MIPI DCS 1.3 */
	MIPI_DCS_READ_DDB_START		= 0xA1,
	MIPI_DCS_READ_DDB_CONTINUE	= 0xA8,
};

/**
 * struct mipi_dsi_msg - read/write DSI buffer
 * @channel: virtual channel id
 * @type: payload data type
 * @flags: flags controlling this message transmission
 * @tx_len: length of @tx_buf
 * @tx_buf: data to be written
 * @rx_len: length of @rx_buf
 * @rx_buf: data to be read, or NULL
 */
struct mipi_dsi_msg {
	u8 channel;
	u8 type;
	u16 flags;

	size_t tx_len;
	const void *tx_buf;

	size_t rx_len;
	void *rx_buf;
};

/**
 * struct mipi_dsi_packet - represents a MIPI DSI packet in protocol format
 * @size: size (in bytes) of the packet
 * @header: the four bytes that make up the header (Data ID, Word Count or
 * Packet Data, and ECC)
 * @payload_length: number of bytes in the payload
 * @payload: a pointer to a buffer containing the payload, if any
 */
struct mipi_dsi_packet {
	size_t size;
	u8 header[4];
	size_t payload_length;
	const u8 *payload;
};

static inline int
dsi_write(struct rk_serdes *des, u8 remote_id, u32 reg, u32 val)
{
	struct i2c_client *client = des->chip[remote_id].client;

	return des->i2c_write_reg(client, reg, val);
}

static inline int
dsi_read(struct rk_serdes *des, u8 remote_id, u32 reg, u32 *val)
{
	struct i2c_client *client = des->chip[remote_id].client;

	return des->i2c_read_reg(client, reg, val);
}

static inline int dsi_update_bits(struct rk_serdes *des, u8 remote_id,
				  u32 reg, u32 mask, u32 val)
{
	struct i2c_client *client = des->chip[remote_id].client;

	return des->i2c_update_bits(client, reg, mask, val);
}

static int genif_wait_w_pld_fifo_not_full(struct rk_serdes *des, u8 remote_id,
					  const struct rkx120_dsi_tx *dsi)
{
	struct i2c_client *client = des->chip[remote_id].client;
	u32 sts;
	int ret;

	ret = read_poll_timeout(des->i2c_read_reg, ret,
				!(ret < 0) && !(sts & GEN_PLD_W_FULL),
				0, MSEC_PER_SEC, false, client,
				DSI_CMD_PKT_STATUS, &sts);
	if (ret < 0) {
		dev_err(des->dev, "generic write payload fifo is full\n");
		return ret;
	}

	return 0;
}

static int genif_wait_cmd_fifo_not_full(struct rk_serdes *des, u8 remote_id,
					const struct rkx120_dsi_tx *dsi)
{
	struct i2c_client *client = des->chip[remote_id].client;
	u32 sts;
	int ret = 0;

	ret = read_poll_timeout(des->i2c_read_reg, ret,
				!(ret < 0) && !(sts & GEN_CMD_FULL),
				0, MSEC_PER_SEC, false, client,
				DSI_CMD_PKT_STATUS, &sts);
	if (ret < 0) {
		dev_err(des->dev, "generic write cmd fifo is full\n");
		return ret;
	}

	return 0;
}

static int
genif_wait_write_fifo_empty(struct rk_serdes *des, u8 remote_id,
			    const struct rkx120_dsi_tx *dsi)
{
	struct i2c_client *client = des->chip[remote_id].client;
	u32 sts;
	u32 mask;
	int ret;

	mask = GEN_CMD_EMPTY | GEN_PLD_W_EMPTY;

	ret = read_poll_timeout(des->i2c_read_reg, ret,
				!(ret < 0) && (sts & mask) == mask,
				0, MSEC_PER_SEC, false, client,
				DSI_CMD_PKT_STATUS, &sts);
	if (ret < 0) {
		dev_err(des->dev, "generic write fifo is full\n");
		return ret;
	}

	return 0;
}

static int rkx120_dsi_tx_read_from_fifo(struct rk_serdes *des, u8 remote_id,
				    const struct rkx120_dsi_tx *dsi,
				    const struct mipi_dsi_msg *msg)
{
	struct i2c_client *client = des->chip[remote_id].client;
	u8 *payload = msg->rx_buf;
	unsigned int vrefresh = 60;
	u16 length;
	u32 val;
	int ret;

	ret = read_poll_timeout(des->i2c_read_reg, ret,
				!(ret < 0) && !(val & GEN_RD_CMD_BUSY),
				0, DIV_ROUND_UP(MSEC_PER_SEC, vrefresh),
				false, client, DSI_CMD_PKT_STATUS, &val);
	if (ret < 0) {
		dev_err(des->dev, "entire response isn't stored in the FIFO\n");
		return ret;
	}

	/* Receive payload */
	for (length = msg->rx_len; length; length -= 4) {
		dsi_read(des, remote_id, DSI_CMD_PKT_STATUS, &val);
		if (val & GEN_PLD_R_EMPTY)
			ret = -ETIMEDOUT;
		if (ret) {
			dev_err(des->dev, "dsi Read payload FIFO is empty\n");
			return ret;
		}

		dsi_read(des, remote_id, DSI_GEN_PLD_DATA, &val);

		switch (length) {
		case 3:
			payload[2] = (val >> 16) & 0xff;
			fallthrough;
		case 2:
			payload[1] = (val >> 8) & 0xff;
			fallthrough;
		case 1:
			payload[0] = val & 0xff;
			return 0;
		}

		payload[0] = (val >>  0) & 0xff;
		payload[1] = (val >>  8) & 0xff;
		payload[2] = (val >> 16) & 0xff;
		payload[3] = (val >> 24) & 0xff;
		payload += 4;
	}

	return 0;
}

/**
 * mipi_dsi_packet_format_is_short - check if a packet is of the short format
 * @type: MIPI DSI data type of the packet
 *
 * Return: true if the packet for the given data type is a short packet, false
 * otherwise.
 */
static bool mipi_dsi_packet_format_is_short(u8 type)
{
	switch (type) {
	case MIPI_DSI_V_SYNC_START:
	case MIPI_DSI_V_SYNC_END:
	case MIPI_DSI_H_SYNC_START:
	case MIPI_DSI_H_SYNC_END:
	case MIPI_DSI_END_OF_TRANSMISSION:
	case MIPI_DSI_COLOR_MODE_OFF:
	case MIPI_DSI_COLOR_MODE_ON:
	case MIPI_DSI_SHUTDOWN_PERIPHERAL:
	case MIPI_DSI_TURN_ON_PERIPHERAL:
	case MIPI_DSI_GENERIC_SHORT_WRITE_0_PARAM:
	case MIPI_DSI_GENERIC_SHORT_WRITE_1_PARAM:
	case MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM:
	case MIPI_DSI_GENERIC_READ_REQUEST_0_PARAM:
	case MIPI_DSI_GENERIC_READ_REQUEST_1_PARAM:
	case MIPI_DSI_GENERIC_READ_REQUEST_2_PARAM:
	case MIPI_DSI_DCS_SHORT_WRITE:
	case MIPI_DSI_DCS_SHORT_WRITE_PARAM:
	case MIPI_DSI_DCS_READ:
	case MIPI_DSI_DCS_COMPRESSION_MODE:
	case MIPI_DSI_SET_MAXIMUM_RETURN_PACKET_SIZE:
		return true;
	}

	return false;
}

/**
 * mipi_dsi_packet_format_is_long - check if a packet is of the long format
 * @type: MIPI DSI data type of the packet
 *
 * Return: true if the packet for the given data type is a long packet, false
 * otherwise.
 */
static bool mipi_dsi_packet_format_is_long(u8 type)
{
	switch (type) {
	case MIPI_DSI_PPS_LONG_WRITE:
	case MIPI_DSI_NULL_PACKET:
	case MIPI_DSI_BLANKING_PACKET:
	case MIPI_DSI_GENERIC_LONG_WRITE:
	case MIPI_DSI_DCS_LONG_WRITE:
	case MIPI_DSI_LOOSELY_PACKED_PIXEL_STREAM_YCBCR20:
	case MIPI_DSI_PACKED_PIXEL_STREAM_YCBCR24:
	case MIPI_DSI_PACKED_PIXEL_STREAM_YCBCR16:
	case MIPI_DSI_PACKED_PIXEL_STREAM_30:
	case MIPI_DSI_PACKED_PIXEL_STREAM_36:
	case MIPI_DSI_PACKED_PIXEL_STREAM_YCBCR12:
	case MIPI_DSI_PACKED_PIXEL_STREAM_16:
	case MIPI_DSI_PACKED_PIXEL_STREAM_18:
	case MIPI_DSI_PIXEL_STREAM_3BYTE_18:
	case MIPI_DSI_PACKED_PIXEL_STREAM_24:
		return true;
	}

	return false;
}

/**
 * mipi_dsi_create_packet - create a packet from a message according to the
 *     DSI protocol
 * @packet: pointer to a DSI packet structure
 * @msg: message to translate into a packet
 *
 * Return: 0 on success or a negative error code on failure.
 */
static int mipi_dsi_create_packet(struct mipi_dsi_packet *packet,
			   const struct mipi_dsi_msg *msg)
{
	if (!packet || !msg)
		return -EINVAL;

	/* do some minimum sanity checking */
	if (!mipi_dsi_packet_format_is_short(msg->type) &&
	    !mipi_dsi_packet_format_is_long(msg->type))
		return -EINVAL;

	if (msg->channel > 3)
		return -EINVAL;

	memset(packet, 0, sizeof(*packet));
	packet->header[0] = ((msg->channel & 0x3) << 6) | (msg->type & 0x3f);

	/* TODO: compute ECC if hardware support is not available */

	/*
	 * Long write packets contain the word count in header bytes 1 and 2.
	 * The payload follows the header and is word count bytes long.
	 *
	 * Short write packets encode up to two parameters in header bytes 1
	 * and 2.
	 */
	if (mipi_dsi_packet_format_is_long(msg->type)) {
		packet->header[1] = (msg->tx_len >> 0) & 0xff;
		packet->header[2] = (msg->tx_len >> 8) & 0xff;

		packet->payload_length = msg->tx_len;
		packet->payload = msg->tx_buf;
	} else {
		const u8 *tx = msg->tx_buf;

		packet->header[1] = (msg->tx_len > 0) ? tx[0] : 0;
		packet->header[2] = (msg->tx_len > 1) ? tx[1] : 0;
	}

	packet->size = sizeof(packet->header) + packet->payload_length;

	return 0;
}

static int rkx120_dsi_tx_transfer(struct rk_serdes *des, u8 remote_id,
				  const struct rkx120_dsi_tx *dsi,
				  const struct mipi_dsi_msg *msg)
{
	struct mipi_dsi_packet packet;
	int ret;
	u32 val;

	if (msg->flags & MIPI_DSI_MSG_REQ_ACK)
		dsi_update_bits(des, remote_id, DSI_CMD_MODE_CFG,
				ACK_RQST_EN, ACK_RQST_EN);

	if (msg->flags & MIPI_DSI_MSG_USE_LPM) {
		dsi_update_bits(des, remote_id, DSI_VID_MODE_CFG,
				LP_CMD_EN, LP_CMD_EN);
	} else {
		dsi_update_bits(des, remote_id, DSI_VID_MODE_CFG, LP_CMD_EN, 0);
		dsi_update_bits(des, remote_id, DSI_LPCLK_CTRL,
				PHY_TXREQUESTCLKHS, PHY_TXREQUESTCLKHS);
	}

	switch (msg->type) {
	case MIPI_DSI_SHUTDOWN_PERIPHERAL:
		//return rkx120_dsi_tx_shutdown_peripheral(dsi);
	case MIPI_DSI_TURN_ON_PERIPHERAL:
		//return rkx120_dsi_tx_turn_on_peripheral(dsi);
	case MIPI_DSI_DCS_SHORT_WRITE:
		dsi_update_bits(des, remote_id, DSI_CMD_MODE_CFG, DCS_SW_0P_TX,
				msg->flags & MIPI_DSI_MSG_USE_LPM ?
				DCS_SW_0P_TX : 0);
		break;
	case MIPI_DSI_DCS_SHORT_WRITE_PARAM:
		dsi_update_bits(des, remote_id, DSI_CMD_MODE_CFG, DCS_SW_1P_TX,
				msg->flags & MIPI_DSI_MSG_USE_LPM ?
				DCS_SW_1P_TX : 0);
		break;
	case MIPI_DSI_DCS_LONG_WRITE:
		dsi_update_bits(des, remote_id, DSI_CMD_MODE_CFG, DCS_LW_TX,
				msg->flags & MIPI_DSI_MSG_USE_LPM ?
				DCS_LW_TX : 0);
		break;
	case MIPI_DSI_DCS_READ:
		dsi_update_bits(des, remote_id, DSI_CMD_MODE_CFG, DCS_SR_0P_TX,
				msg->flags & MIPI_DSI_MSG_USE_LPM ?
				DCS_SR_0P_TX : 0);
		break;
	case MIPI_DSI_SET_MAXIMUM_RETURN_PACKET_SIZE:
		dsi_update_bits(des, remote_id, DSI_CMD_MODE_CFG, MAX_RD_PKT_SIZE,
				msg->flags & MIPI_DSI_MSG_USE_LPM ?
				MAX_RD_PKT_SIZE : 0);
		break;
	case MIPI_DSI_GENERIC_SHORT_WRITE_0_PARAM:
		dsi_update_bits(des, remote_id, DSI_CMD_MODE_CFG, GEN_SW_0P_TX,
				msg->flags & MIPI_DSI_MSG_USE_LPM ?
				GEN_SW_0P_TX : 0);
		break;
	case MIPI_DSI_GENERIC_SHORT_WRITE_1_PARAM:
		dsi_update_bits(des, remote_id, DSI_CMD_MODE_CFG, GEN_SW_1P_TX,
				msg->flags & MIPI_DSI_MSG_USE_LPM ?
				GEN_SW_1P_TX : 0);
		break;
	case MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM:
		dsi_update_bits(des, remote_id, DSI_CMD_MODE_CFG, GEN_SW_2P_TX,
				msg->flags & MIPI_DSI_MSG_USE_LPM ?
				GEN_SW_2P_TX : 0);
		break;
	case MIPI_DSI_GENERIC_LONG_WRITE:
		dsi_update_bits(des, remote_id, DSI_CMD_MODE_CFG, GEN_LW_TX,
				msg->flags & MIPI_DSI_MSG_USE_LPM ?
				GEN_LW_TX : 0);
		break;
	case MIPI_DSI_GENERIC_READ_REQUEST_0_PARAM:
		dsi_update_bits(des, remote_id, DSI_CMD_MODE_CFG, GEN_SR_0P_TX,
		msg->flags & MIPI_DSI_MSG_USE_LPM ? GEN_SR_0P_TX : 0);
		break;
	case MIPI_DSI_GENERIC_READ_REQUEST_1_PARAM:
		dsi_update_bits(des, remote_id, DSI_CMD_MODE_CFG, GEN_SR_1P_TX,
		msg->flags & MIPI_DSI_MSG_USE_LPM ? GEN_SR_1P_TX : 0);
		break;
	case MIPI_DSI_GENERIC_READ_REQUEST_2_PARAM:
		dsi_update_bits(des, remote_id, DSI_CMD_MODE_CFG, GEN_SR_2P_TX,
				msg->flags & MIPI_DSI_MSG_USE_LPM ? GEN_SR_2P_TX : 0);
		break;
	default:
		return -EINVAL;
	}

	/* create a packet to the DSI protocol */
	ret = mipi_dsi_create_packet(&packet, msg);
	if (ret) {
		dev_err(des->dev, "failed to create packet\n");
		return ret;
	}

	/* Send payload */
	while (packet.payload_length >= 4) {
		/*
		 * Alternatively, you can always keep the FIFO
		 * nearly full by monitoring the FIFO state until
		 * it is not full, and then writea single word of data.
		 * This solution is more resource consuming
		 * but it simultaneously avoids FIFO starvation,
		 * making it possible to use FIFO sizes smaller than
		 * the amount of data of the longest packet to be written.
		 */
		ret = genif_wait_w_pld_fifo_not_full(des, remote_id, dsi);
		if (ret)
			return ret;

		val = get_unaligned_le32(packet.payload);

		dsi_write(des, remote_id, DSI_GEN_PLD_DATA, val);


		packet.payload += 4;
		packet.payload_length -= 4;
	}

	val = 0;
	switch (packet.payload_length) {
	case 3:
		val |= packet.payload[2] << 16;
		fallthrough;
	case 2:
		val |= packet.payload[1] << 8;
		fallthrough;
	case 1:
		val |= packet.payload[0];

		dsi_write(des, remote_id, DSI_GEN_PLD_DATA, val);
		break;
	}

	ret = genif_wait_cmd_fifo_not_full(des, remote_id, dsi);
	if (ret)
		return ret;

	/* Send packet header */
	val = get_unaligned_le32(packet.header);

	dsi_write(des, remote_id, DSI_GEN_HDR, val);

	ret = genif_wait_write_fifo_empty(des, remote_id, dsi);
	if (ret)
		return ret;

	if (msg->rx_len) {
		ret = rkx120_dsi_tx_read_from_fifo(des, remote_id, dsi, msg);
		if (ret < 0)
			return ret;
	}

	return msg->tx_len;
}

static int rkx120_mipi_dsi_generic_write(struct rk_serdes *des, u8 remote_id,
				  const void *payload, size_t size)
{
	const struct rkx120_dsi_tx *dsi = &des->dsi_tx;
	struct mipi_dsi_msg msg;

	memset(&msg, 0, sizeof(msg));
	msg.channel = dsi->channel;
	msg.tx_buf = payload;
	msg.tx_len = size;
	msg.rx_len = 0;

	switch (size) {
	case 0:
		msg.type = MIPI_DSI_GENERIC_SHORT_WRITE_0_PARAM;
		break;

	case 1:
		msg.type = MIPI_DSI_GENERIC_SHORT_WRITE_1_PARAM;
		break;
	case 2:
		msg.type = MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM;
		break;
	default:
		msg.type = MIPI_DSI_GENERIC_LONG_WRITE;
		break;
	}

	if (dsi->mode_flags & SERDES_MIPI_DSI_MODE_LPM)
		msg.flags |= MIPI_DSI_MSG_USE_LPM;

	return rkx120_dsi_tx_transfer(des, remote_id, dsi, &msg);
}

static int rkx120_mipi_dsi_dcs_write_buffer(struct rk_serdes *des, u8 remote_id,
				     const void *data, size_t len)
{
	const struct rkx120_dsi_tx *dsi = &des->dsi_tx;
	struct mipi_dsi_msg msg;

	memset(&msg, 0, sizeof(msg));
	msg.channel = dsi->channel;
	msg.tx_buf = data;
	msg.tx_len = len;
	msg.rx_len = 0;

	switch (len) {
	case 0:
		return -EINVAL;
	case 1:
		msg.type = MIPI_DSI_DCS_SHORT_WRITE;
		break;
	case 2:
		msg.type = MIPI_DSI_DCS_SHORT_WRITE_PARAM;
		break;
	default:
		msg.type = MIPI_DSI_DCS_LONG_WRITE;
		break;
	}

	if (dsi->mode_flags & SERDES_MIPI_DSI_MODE_LPM)
		msg.flags |= MIPI_DSI_MSG_USE_LPM;

	return rkx120_dsi_tx_transfer(des, remote_id, dsi, &msg);
}

static __maybe_unused int
rkx120_mipi_dsi_dcs_read(struct rk_serdes *des, u8 remote_id,
			 u8 cmd, void *data, size_t len)
{
	const struct rkx120_dsi_tx *dsi = &des->dsi_tx;
	struct mipi_dsi_msg msg;

	memset(&msg, 0, sizeof(msg));
	msg.channel = dsi->channel;
	msg.type = MIPI_DSI_DCS_READ;
	msg.tx_buf = &cmd;
	msg.tx_len = 1;
	msg.rx_buf = data;
	msg.rx_len = len;

	return rkx120_dsi_tx_transfer(des, remote_id, dsi, &msg);
}

int rkx120_dsi_tx_cmd_seq_xfer(struct rk_serdes *des, u8 remote_id,
			       struct panel_cmds *cmds)
{
	u16 i;
	int err;

	if (!cmds)
		return 0;

	for (i = 0; i < cmds->cmd_cnt; i++) {
		struct cmd_desc *cmd = &cmds->cmds[i];

		switch (cmd->dchdr.dtype) {
		case MIPI_DSI_GENERIC_SHORT_WRITE_0_PARAM:
		case MIPI_DSI_GENERIC_SHORT_WRITE_1_PARAM:
		case MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM:
		case MIPI_DSI_GENERIC_LONG_WRITE:
			err = rkx120_mipi_dsi_generic_write(des, remote_id, cmd->payload,
							    cmd->dchdr.dlen);
			break;
		case MIPI_DSI_DCS_SHORT_WRITE:
		case MIPI_DSI_DCS_SHORT_WRITE_PARAM:
		case MIPI_DSI_DCS_LONG_WRITE:
			err = rkx120_mipi_dsi_dcs_write_buffer(des, remote_id, cmd->payload,
							       cmd->dchdr.dlen);
			break;
		default:
			dev_err(des->dev, "panel cmd desc invalid data type\n");
			return -EINVAL;
		}

		if (err < 0)
			dev_err(des->dev, "failed to write cmd\n");

		if (cmd->dchdr.wait)
			mdelay(cmd->dchdr.wait);
	}

	return 0;
}
EXPORT_SYMBOL(rkx120_dsi_tx_cmd_seq_xfer);

static u64 rkx120_dsi_tx_get_lane_rate(const struct rkx120_dsi_tx *dsi)
{
	struct videomode *vm = dsi->vm;
	u64 lane_rate;
	u32 max_lane_rate = 1500000000ULL;
	u8 bpp, lanes;

	bpp = dsi->bpp;
	lanes = dsi->lanes;
	lane_rate = (u64)vm->pixelclock * bpp;
	do_div(lane_rate, lanes);

	if (dsi->mode_flags & SERDES_MIPI_DSI_MODE_VIDEO_BURST) {
		lane_rate *= 10;
		do_div(lane_rate, 9);
	}

	if (lane_rate > max_lane_rate)
		lane_rate = max_lane_rate;

	return lane_rate;
}

static void
mipi_dphy_power_on(struct rk_serdes *des, const struct rkx120_dsi_tx *dsi, u8 remote_id)
{
	struct i2c_client *client = des->chip[remote_id].client;
	u32 val, mask;
	int ret = 0;

	dsi_update_bits(des, remote_id, DSI_PHY_RSTZ, PHY_ENABLECLK, 0);
	dsi_update_bits(des, remote_id, DSI_PHY_RSTZ, PHY_SHUTDOWNZ, 0);
	dsi_update_bits(des, remote_id, DSI_PHY_RSTZ, PHY_RSTZ, 0);

	udelay(1);

	dsi_update_bits(des, remote_id, DSI_PHY_RSTZ,
			PHY_ENABLECLK, PHY_ENABLECLK);
	dsi_update_bits(des, remote_id, DSI_PHY_RSTZ,
			PHY_SHUTDOWNZ, PHY_SHUTDOWNZ);
	dsi_update_bits(des, remote_id, DSI_PHY_RSTZ, PHY_RSTZ, PHY_RSTZ);
	usleep_range(1500, 2000);

	rkx120_combtxphy_power_on(des, remote_id, 0);

	ret = read_poll_timeout(des->i2c_read_reg, ret,
				!(ret < 0) && (val & PHY_LOCK),
				0, MSEC_PER_SEC, false, client,
				DSI_PHY_STATUS, &val);
	if (ret < 0)
		dev_err(des->dev, "PHY is not locked\n");

	usleep_range(100, 200);

	mask = PHY_STOPSTATELANE;
	ret = read_poll_timeout(des->i2c_read_reg, ret,
				!(ret < 0) && ((val & mask) == mask),
				0, MSEC_PER_SEC, false, client,
				DSI_PHY_STATUS, &val);
	if (ret < 0)
		dev_err(des->dev, "lane module is not in stop state\n");

	udelay(10);
}

static void rkx120_dsi_tx_reset_control_assert(struct rk_serdes *des, u8 remote_id)
{
	//TXCRU_SOFTRST_CON03 bit[8]: presetn_dsitx
	//dsi_write(des, remote_id, CRU_SOFTRST_CON02, 0x400040, remote_id);
}

static void rkx120_dsi_tx_reset_control_deassert(struct rk_serdes *des, u8 remote_id)
{
	//TXCRU_SOFTRST_CON03 bit[8]: presetn_dsitx
	//dsi_i2c_write(des, remote_id, CRU_SOFTRST_CON02, 0x400000);
}

static void rkx120_dsi_tx_bridge_pre_enable(struct rk_serdes *des, u8 remote_id)
{
	struct rkx120_dsi_tx *dsi = &des->dsi_tx;
	u32 val;

	dsi_write(des, remote_id, DSI_PWR_UP, RESET);
	dsi_write(des, remote_id, DSI_MODE_CFG, CMD_VIDEO_MODE(COMMAND_MODE));

	val = DIV_ROUND_UP(lane_kbps >> 3, 20 * MSEC_PER_SEC);
	dsi_write(des, remote_id, DSI_CLKMGR_CFG,
		  TO_CLK_DIVISION(10) | TX_ESC_CLK_DIVISION(val));

	val = CRC_RX_EN | ECC_RX_EN | BTA_EN | EOTP_TX_EN;
	if (dsi->mode_flags & SERDES_MIPI_DSI_MODE_EOT_PACKET)
		val &= ~EOTP_TX_EN;

	dsi_write(des, remote_id, DSI_PCKHDL_CFG, val);

	dsi_write(des, remote_id, DSI_TO_CNT_CFG,
		  HSTX_TO_CNT(1000) | LPRX_TO_CNT(1000));
	dsi_write(des, remote_id, DSI_BTA_TO_CNT, 0xd00);
	dsi_write(des, remote_id, DSI_PHY_TMR_CFG,
		  PHY_HS2LP_TIME(0x14) | PHY_LP2HS_TIME(0x10) |
		  MAX_RD_TIME(10000));
	dsi_write(des, remote_id, DSI_PHY_TMR_LPCLK_CFG,
		  PHY_CLKHS2LP_TIME(0x40) | PHY_CLKLP2HS_TIME(0x40));
	dsi_write(des, remote_id, DSI_PHY_IF_CFG,
		  PHY_STOP_WAIT_TIME(0x20) | N_LANES(dsi->lanes - 1));

	mipi_dphy_power_on(des, dsi, remote_id);

	dsi_write(des, remote_id, DSI_PWR_UP, POWER_UP);
}

static void rkx120_dsi_tx_set_vid_mode(struct rk_serdes *des, u8 remote_id,
				       const struct rkx120_dsi_tx *dsi,
				       const struct videomode *vm)
{
	u64 lanebyteclk = (lane_kbps * MSEC_PER_SEC) >> 3;
	unsigned int dpipclk = vm->pixelclock;
	u32 hline, hsa, hbp, hline_time, hsa_time, hbp_time;
	u32 vactive, vsa, vfp, vbp;
	u32 val;
	int pkt_size;

	val = LP_HFP_EN | LP_HBP_EN | LP_VACT_EN | LP_VFP_EN | LP_VBP_EN |
	      LP_VSA_EN;

	if (dsi->mode_flags & SERDES_MIPI_DSI_MODE_VIDEO_HFP)
		val &= ~LP_HFP_EN;

	if (dsi->mode_flags & SERDES_MIPI_DSI_MODE_VIDEO_HBP)
		val &= ~LP_HBP_EN;

	if (dsi->mode_flags & SERDES_MIPI_DSI_MODE_VIDEO_BURST)
		val |= VID_MODE_TYPE_BURST;
	else if (dsi->mode_flags & SERDES_MIPI_DSI_MODE_VIDEO_SYNC_PULSE)
		val |= VID_MODE_TYPE_NON_BURST_SYNC_PULSES;
	else
		val |= VID_MODE_TYPE_NON_BURST_SYNC_EVENTS;

	dsi_write(des, remote_id, DSI_VID_MODE_CFG, val);

	if (dsi->mode_flags & SERDES_MIPI_DSI_CLOCK_NON_CONTINUOUS)
		dsi_update_bits(des, remote_id, DSI_LPCLK_CTRL,
				AUTO_CLKLANE_CTRL, AUTO_CLKLANE_CTRL);

	pkt_size = VID_PKT_SIZE(vm->hactive);

	dsi_write(des, remote_id, DSI_VID_PKT_SIZE, pkt_size);

	vactive = vm->vactive;
	vsa = vm->vsync_len;
	vfp = vm->vfront_porch;
	vbp = vm->vback_porch;
	hsa = vm->hsync_len;
	hbp = vm->hback_porch;
	hline = vm->hactive + vm->hfront_porch +
		vm->hback_porch + vm->hsync_len;

	//hline_time = hline * lanebyteclk / dpipclk;
	hline_time = DIV_ROUND_CLOSEST_ULL(hline * lanebyteclk, dpipclk);
	dsi_write(des, remote_id, DSI_VID_HLINE_TIME,
		  VID_HLINE_TIME(hline_time));
	//hsa_time = hsa * lanebyteclk / dpipclk;
	hsa_time = DIV_ROUND_CLOSEST_ULL(hsa * lanebyteclk, dpipclk);
	dsi_write(des, remote_id, DSI_VID_HSA_TIME, VID_HSA_TIME(hsa_time));
	//hbp_time = hbp * lanebyteclk / dpipclk;
	hbp_time = DIV_ROUND_CLOSEST_ULL(hbp * lanebyteclk, dpipclk);
	dsi_write(des, remote_id, DSI_VID_HBP_TIME, VID_HBP_TIME(hbp_time));

	dsi_write(des, remote_id, DSI_VID_VACTIVE_LINES, vactive);
	dsi_write(des, remote_id, DSI_VID_VSA_LINES, vsa);
	dsi_write(des, remote_id, DSI_VID_VFP_LINES, vfp);
	dsi_write(des, remote_id, DSI_VID_VBP_LINES, vbp);

	dsi_write(des, remote_id, DSI_MODE_CFG, CMD_VIDEO_MODE(VIDEO_MODE));
}

static void rkx120_dsi_tx_set_cmd_mode(struct rk_serdes *des, u8 remote_id,
				       const struct rkx120_dsi_tx *dsi,
				       const struct videomode *vm)
{
	dsi_update_bits(des, remote_id, DSI_CMD_MODE_CFG, DCS_LW_TX, 0);
	dsi_write(des, remote_id, DSI_EDPI_CMD_SIZE,
		  EDPI_ALLOWED_CMD_SIZE(vm->hactive));
	dsi_write(des, remote_id, DSI_MODE_CFG, CMD_VIDEO_MODE(COMMAND_MODE));
}

static void
rkx120_dsi_tx_bridge_enable(struct rk_serdes *des, u8 remote_id)
{
	struct rkx120_dsi_tx *dsi = &des->dsi_tx;
	const struct videomode *vm = dsi->vm;
	u32 val;

	dsi_write(des, remote_id, DSI_PWR_UP, RESET);

	switch (dsi->bus_format) {
	case SERDES_MIPI_DSI_FMT_RGB666:
		val = DPI_COLOR_CODING(DPI_COLOR_CODING_18BIT_2) |
		      LOOSELY18_EN;
			break;
	case SERDES_MIPI_DSI_FMT_RGB666_PACKED:
		val = DPI_COLOR_CODING(DPI_COLOR_CODING_18BIT_1);
		break;
	case SERDES_MIPI_DSI_FMT_RGB565:
		val = DPI_COLOR_CODING(DPI_COLOR_CODING_16BIT_1);
		break;
	case SERDES_MIPI_DSI_FMT_RGB888:
	default:
		val = DPI_COLOR_CODING(DPI_COLOR_CODING_24BIT);
		break;
	}

	dsi_write(des, remote_id, DSI_DPI_COLOR_CODING, val);

	val = 0;
	if (vm->flags & DISPLAY_FLAGS_VSYNC_LOW)
		val |= VSYNC_ACTIVE_LOW;
	if (vm->flags & DISPLAY_FLAGS_HSYNC_LOW)
		val |= HSYNC_ACTIVE_LOW;

	dsi_write(des, remote_id, DSI_DPI_CFG_POL, val);

	dsi_write(des, remote_id, DSI_DPI_VCID, DPI_VID(0));
	dsi_write(des, remote_id, DSI_DPI_LP_CMD_TIM,
		  OUTVACT_LPCMD_TIME(4) | INVACT_LPCMD_TIME(4));
	dsi_update_bits(des, remote_id, DSI_LPCLK_CTRL,
			PHY_TXREQUESTCLKHS, PHY_TXREQUESTCLKHS);

	if (dsi->mode_flags & SERDES_MIPI_DSI_MODE_VIDEO)
		rkx120_dsi_tx_set_vid_mode(des, remote_id, dsi, vm);
	else
		rkx120_dsi_tx_set_cmd_mode(des, remote_id, dsi, vm);

	dsi_write(des, remote_id, DSI_PWR_UP, POWER_UP);
}

void rkx120_dsi_tx_pre_enable(struct rk_serdes *des,
			      struct rk_serdes_route *route,
			      u8 remote_id)
{
	struct rkx120_dsi_tx *dsi = &des->dsi_tx;
	u64 rate;

	dsi->vm = &route->vm;
	rate = rkx120_dsi_tx_get_lane_rate(dsi);

	rkx120_combtxphy_set_mode(des, COMBTX_PHY_MODE_VIDEO_MIPI);
	rkx120_combtxphy_set_rate(des, rate);
	lane_kbps = rkx120_combtxphy_get_rate(des) / MSEC_PER_SEC;

	/* rst for dsi */
	rkx120_dsi_tx_reset_control_assert(des, remote_id);
	usleep_range(20, 40);
	rkx120_dsi_tx_reset_control_deassert(des, remote_id);
	usleep_range(20, 40);

	rkx120_dsi_tx_bridge_pre_enable(des, remote_id);

#ifdef DSI_READ_POWER_MODE
	u8 mode;

	rkx120_mipi_dsi_dcs_read(des, remote_id, MIPI_DCS_GET_POWER_MODE,
				 &mode, sizeof(mode));

	dev_info(rkx120->dev, "dsi: mode: 0x%x\n", mode);
#endif
}

void rkx120_dsi_tx_enable(struct rk_serdes *des,
			  struct rk_serdes_route *route,
			  u8 remote_id)
{
	struct rkx120_dsi_tx *dsi = &des->dsi_tx;

#ifdef DSI_READ_POWER_MODE
	u8 mode;

	rkx120_mipi_dsi_dcs_read(des, remote_id, MIPI_DCS_GET_POWER_MODE,
				 &mode, sizeof(mode));

	dev_info(rkx120->dev, "dsi: mode: 0x%x\n", mode);
#endif
	rkx120_dsi_tx_bridge_enable(des, remote_id);

	dev_info(des->dev, "rkx120_dsi_tx final DSI-Link bandwidth: %llu Kbps x %d lanes\n",
		 lane_kbps, dsi->lanes);
}

void rkx120_dsi_tx_post_disable(struct rk_serdes *des,
				struct rk_serdes_route *route,
				u8 remote_id)
{
	rkx120_combtxphy_power_off(des, remote_id);
}

void rkx120_dsi_tx_disable(struct rk_serdes *des, struct rk_serdes_route *route, u8 remote_id)
{
	dsi_write(des, remote_id, DSI_PWR_UP, RESET);
	dsi_write(des, remote_id, DSI_LPCLK_CTRL, 0);
	dsi_write(des, remote_id, DSI_EDPI_CMD_SIZE, 0);
	dsi_write(des, remote_id, DSI_MODE_CFG, CMD_VIDEO_MODE(COMMAND_MODE));
	dsi_write(des, remote_id, DSI_PWR_UP, POWER_UP);
}
