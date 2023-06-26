// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Guochun Huang <hero.huang@rock-chips.com>
 */

#include <asm/unaligned.h>
#include "rk628.h"
#include "rk628_cru.h"
#include "rk628_dsi.h"
#include "rk628_combtxphy.h"
#include "rk628_config.h"
#include "panel.h"

/* Test Code: 0x44 (HS RX Control of Lane 0) */
#define HSFREQRANGE(x)			UPDATE(x, 6, 1)

/* request ACK from peripheral */
#define MIPI_DSI_MSG_REQ_ACK	BIT(0)
/* use Low Power Mode to transmit message */
#define MIPI_DSI_MSG_USE_LPM	BIT(1)

static u32 lane_mbps;

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

static inline int dsi_write(struct rk628 *rk628, const struct rk628_dsi *dsi,
			    u32 reg, u32 val)
{
	unsigned int dsi_base;

	dsi_base = dsi->id ? DSI1_BASE : DSI0_BASE;

	return rk628_i2c_write(rk628, dsi_base + reg, val);
}

static inline int dsi_read(struct rk628 *rk628, const struct rk628_dsi *dsi,
			   u32 reg, u32 *val)
{
	unsigned int dsi_base;

	dsi_base = dsi->id ? DSI1_BASE : DSI0_BASE;

	return rk628_i2c_read(rk628, dsi_base + reg, val);
}

static inline int dsi_update_bits(struct rk628 *rk628,
				  const struct rk628_dsi *dsi,
				  u32 reg, u32 mask, u32 val)
{
	unsigned int dsi_base;

	dsi_base = dsi->id ? DSI1_BASE : DSI0_BASE;

	return rk628_i2c_update_bits(rk628, dsi_base + reg, mask, val);
}

int rk628_dsi_parse(struct rk628 *rk628, struct device_node *dsi_np)
{
	u32 val;
	const char *string;
	int ret;

	if (!of_device_is_available(dsi_np))
		return -EINVAL;

	rk628->output_mode = OUTPUT_MODE_DSI;
	rk628->dsi0.id = 0;
	rk628->dsi0.channel = 0;
	rk628->dsi0.rk628 = rk628;

	if (!of_property_read_u32(dsi_np, "dsi,lanes", &val))
		rk628->dsi0.lanes = val;

	if (of_property_read_bool(dsi_np, "dsi,video-mode"))
		rk628->dsi0.mode_flags |= MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_VIDEO |
					  MIPI_DSI_MODE_VIDEO_BURST;
	else
		rk628->dsi0.mode_flags |= MIPI_DSI_MODE_LPM;

	if (of_property_read_bool(dsi_np, "dsi,eotp"))
		rk628->dsi0.mode_flags |= MIPI_DSI_MODE_EOT_PACKET;

	if (!of_property_read_string(dsi_np, "dsi,format", &string)) {
		if (!strcmp(string, "rgb666")) {
			rk628->dsi0.bus_format = MIPI_DSI_FMT_RGB666;
			rk628->dsi0.bpp = 24;
		} else if (!strcmp(string, "rgb666-packed")) {
			rk628->dsi0.bus_format = MIPI_DSI_FMT_RGB666_PACKED;
			rk628->dsi0.bpp = 18;
		} else if (!strcmp(string, "rgb565")) {
			rk628->dsi0.bus_format = MIPI_DSI_FMT_RGB565;
			rk628->dsi0.bpp = 16;
		} else {
			rk628->dsi0.bus_format = MIPI_DSI_FMT_RGB888;
			rk628->dsi0.bpp = 24;
		}
	}

	if (of_property_read_bool(dsi_np, "rockchip,dual-channel")) {
		rk628->dsi0.master = false;
		rk628->dsi0.slave = true;

		memcpy(&rk628->dsi1, &rk628->dsi0, sizeof(struct rk628_dsi));
		rk628->dsi1.id = 1;
		rk628->dsi1.master = true;
		rk628->dsi1.slave = false;
	}

	ret = rk628_panel_info_get(rk628, dsi_np);
	if (ret)
		return ret;


	return 0;
}

static int genif_wait_w_pld_fifo_not_full(struct rk628 *rk628,
					  const struct rk628_dsi *dsi)
{
	u32 sts;
	int ret;
	int dev_id;
	unsigned int dsi_base;

	dev_id = dsi->id ? RK628_DEV_DSI1 : RK628_DEV_DSI0;
	dsi_base = dsi->id ? DSI1_BASE : DSI0_BASE;

	ret = regmap_read_poll_timeout(rk628->regmap[dev_id],
				       dsi_base + DSI_CMD_PKT_STATUS,
				       sts, !(sts & GEN_PLD_W_FULL),
				       0, 1000);
	if (ret < 0) {
		dev_err(rk628->dev, "generic write payload fifo is full\n");
		return ret;
	}

	return 0;
}

static int genif_wait_cmd_fifo_not_full(struct rk628 *rk628,
					const struct rk628_dsi *dsi)
{
	u32 sts;
	int ret = 0;
	int dev_id;
	unsigned int dsi_base;

	dev_id = dsi->id ? RK628_DEV_DSI1 : RK628_DEV_DSI0;
	dsi_base = dsi->id ? DSI1_BASE : DSI0_BASE;

	ret = regmap_read_poll_timeout(rk628->regmap[dev_id],
				       dsi_base + DSI_CMD_PKT_STATUS,
				       sts, !(sts & GEN_CMD_FULL),
				       0, 1000);
	if (ret < 0) {
		dev_err(rk628->dev, "generic write cmd fifo is full\n");
		return ret;
	}

	return 0;
}

static int genif_wait_write_fifo_empty(struct rk628 *rk628, const struct rk628_dsi *dsi)
{
	u32 sts;
	u32 mask;
	int ret;
	int dev_id;
	unsigned int dsi_base;

	dev_id = dsi->id ? RK628_DEV_DSI1 : RK628_DEV_DSI0;
	dsi_base = dsi->id ? DSI1_BASE : DSI0_BASE;

	mask = GEN_CMD_EMPTY | GEN_PLD_W_EMPTY;

	ret = regmap_read_poll_timeout(rk628->regmap[dev_id],
				       dsi_base + DSI_CMD_PKT_STATUS,
				       sts, (sts & mask) == mask,
				       0, 1000);

	if (ret < 0) {
		dev_err(rk628->dev, "generic write fifo is full\n");
		return ret;
	}

	return 0;
}

static int rk628_dsi_read_from_fifo(struct rk628 *rk628,
				    const struct rk628_dsi *dsi,
				    const struct mipi_dsi_msg *msg)
{
	u8 *payload = msg->rx_buf;
	unsigned int vrefresh = 60;
	u16 length;
	u32 val;
	int ret;
	int dev_id;
	unsigned int dsi_base;

	dev_id = dsi->id ? RK628_DEV_DSI1 : RK628_DEV_DSI0;
	dsi_base = dsi->id ? DSI1_BASE : DSI0_BASE;

	ret = regmap_read_poll_timeout(rk628->regmap[dev_id],
				       dsi_base + DSI_CMD_PKT_STATUS,
				       val, !(val & GEN_RD_CMD_BUSY),
				       0, DIV_ROUND_UP(1000000, vrefresh));
	if (ret) {
		dev_err(rk628->dev, "entire response isn't stored in the FIFO\n");
		return ret;
	}

	/* Receive payload */
	for (length = msg->rx_len; length; length -= 4) {
		dsi_read(rk628, dsi, DSI_CMD_PKT_STATUS, &val);
		if (val & GEN_PLD_R_EMPTY)
			ret = -ETIMEDOUT;
		if (ret) {
			dev_err(rk628->dev, "dsi Read payload FIFO is empty\n");
			return ret;
		}

		dsi_read(rk628, dsi, DSI_GEN_PLD_DATA, &val);

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

static int rk628_dsi_transfer(struct rk628 *rk628, const struct rk628_dsi *dsi,
			      const struct mipi_dsi_msg *msg)
{
	struct mipi_dsi_packet packet;
	int ret;
	u32 val;

	if (msg->flags & MIPI_DSI_MSG_REQ_ACK)
		dsi_update_bits(rk628, dsi, DSI_CMD_MODE_CFG,
				ACK_RQST_EN, ACK_RQST_EN);

	if (msg->flags & MIPI_DSI_MSG_USE_LPM) {
		dsi_update_bits(rk628, dsi, DSI_VID_MODE_CFG,
				LP_CMD_EN, LP_CMD_EN);
	} else {
		dsi_update_bits(rk628, dsi, DSI_VID_MODE_CFG, LP_CMD_EN, 0);
		dsi_update_bits(rk628, dsi, DSI_LPCLK_CTRL,
				PHY_TXREQUESTCLKHS, PHY_TXREQUESTCLKHS);
	}

	switch (msg->type) {
	case MIPI_DSI_SHUTDOWN_PERIPHERAL:
		//return rk628_dsi_shutdown_peripheral(dsi);
	case MIPI_DSI_TURN_ON_PERIPHERAL:
		//return rk628_dsi_turn_on_peripheral(dsi);
	case MIPI_DSI_DCS_SHORT_WRITE:
		dsi_update_bits(rk628, dsi, DSI_CMD_MODE_CFG, DCS_SW_0P_TX,
				msg->flags & MIPI_DSI_MSG_USE_LPM ?
				DCS_SW_0P_TX : 0);
		break;
	case MIPI_DSI_DCS_SHORT_WRITE_PARAM:
		dsi_update_bits(rk628, dsi, DSI_CMD_MODE_CFG, DCS_SW_1P_TX,
				msg->flags & MIPI_DSI_MSG_USE_LPM ?
				DCS_SW_1P_TX : 0);
		break;
	case MIPI_DSI_DCS_LONG_WRITE:
		dsi_update_bits(rk628, dsi, DSI_CMD_MODE_CFG, DCS_LW_TX,
				msg->flags & MIPI_DSI_MSG_USE_LPM ?
				DCS_LW_TX : 0);
		break;
	case MIPI_DSI_DCS_READ:
		dsi_update_bits(rk628, dsi, DSI_CMD_MODE_CFG, DCS_SR_0P_TX,
				msg->flags & MIPI_DSI_MSG_USE_LPM ?
				DCS_SR_0P_TX : 0);
		break;
	case MIPI_DSI_SET_MAXIMUM_RETURN_PACKET_SIZE:
		dsi_update_bits(rk628, dsi, DSI_CMD_MODE_CFG, MAX_RD_PKT_SIZE,
				msg->flags & MIPI_DSI_MSG_USE_LPM ?
				MAX_RD_PKT_SIZE : 0);
		break;
	case MIPI_DSI_GENERIC_SHORT_WRITE_0_PARAM:
		dsi_update_bits(rk628, dsi, DSI_CMD_MODE_CFG, GEN_SW_0P_TX,
				msg->flags & MIPI_DSI_MSG_USE_LPM ?
				GEN_SW_0P_TX : 0);
		break;
	case MIPI_DSI_GENERIC_SHORT_WRITE_1_PARAM:
		dsi_update_bits(rk628, dsi, DSI_CMD_MODE_CFG, GEN_SW_1P_TX,
				msg->flags & MIPI_DSI_MSG_USE_LPM ?
				GEN_SW_1P_TX : 0);
		break;
	case MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM:
		dsi_update_bits(rk628, dsi, DSI_CMD_MODE_CFG, GEN_SW_2P_TX,
				msg->flags & MIPI_DSI_MSG_USE_LPM ?
				GEN_SW_2P_TX : 0);
		break;
	case MIPI_DSI_GENERIC_LONG_WRITE:
		dsi_update_bits(rk628, dsi, DSI_CMD_MODE_CFG, GEN_LW_TX,
				msg->flags & MIPI_DSI_MSG_USE_LPM ?
				GEN_LW_TX : 0);
		break;
	case MIPI_DSI_GENERIC_READ_REQUEST_0_PARAM:
		dsi_update_bits(rk628, dsi, DSI_CMD_MODE_CFG, GEN_SR_0P_TX,
		msg->flags & MIPI_DSI_MSG_USE_LPM ? GEN_SR_0P_TX : 0);
		break;
	case MIPI_DSI_GENERIC_READ_REQUEST_1_PARAM:
		dsi_update_bits(rk628, dsi, DSI_CMD_MODE_CFG, GEN_SR_1P_TX,
		msg->flags & MIPI_DSI_MSG_USE_LPM ? GEN_SR_1P_TX : 0);
		break;
	case MIPI_DSI_GENERIC_READ_REQUEST_2_PARAM:
		dsi_update_bits(rk628, dsi, DSI_CMD_MODE_CFG, GEN_SR_2P_TX,
				msg->flags & MIPI_DSI_MSG_USE_LPM ? GEN_SR_2P_TX : 0);
		break;
	default:
		return -EINVAL;
	}

	/* create a packet to the DSI protocol */
	ret = mipi_dsi_create_packet(&packet, msg);
	if (ret) {
		dev_err(rk628->dev, "failed to create packet\n");
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
		ret = genif_wait_w_pld_fifo_not_full(rk628, dsi);
		if (ret)
			return ret;

		val = get_unaligned_le32(packet.payload);

		dsi_write(rk628, dsi, DSI_GEN_PLD_DATA, val);


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

		dsi_write(rk628, dsi, DSI_GEN_PLD_DATA, val);
		break;
	}

	ret = genif_wait_cmd_fifo_not_full(rk628, dsi);
	if (ret)
		return ret;

	/* Send packet header */
	val = get_unaligned_le32(packet.header);

	dsi_write(rk628, dsi, DSI_GEN_HDR, val);

	ret = genif_wait_write_fifo_empty(rk628, dsi);
	if (ret)
		return ret;

	if (msg->rx_len) {
		ret = rk628_dsi_read_from_fifo(rk628, dsi, msg);
		if (ret < 0)
			return ret;
	}

	if (dsi->slave) {
		const struct rk628_dsi *dsi1 = &rk628->dsi1;

		rk628_dsi_transfer(rk628, dsi1, msg);
	}

	return msg->tx_len;
}

int rk628_mipi_dsi_generic_write(struct rk628 *rk628,
				 const void *payload, size_t size)
{
	const struct rk628_dsi *dsi = &rk628->dsi0;
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

		if (dsi->mode_flags & MIPI_DSI_MODE_LPM)
			msg.flags |= MIPI_DSI_MSG_USE_LPM;

		return rk628_dsi_transfer(rk628, dsi, &msg);
}

int rk628_mipi_dsi_dcs_write_buffer(struct rk628 *rk628,
				    const void *data, size_t len)
{
	const struct rk628_dsi *dsi = &rk628->dsi0;
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

	if (dsi->mode_flags & MIPI_DSI_MODE_LPM)
		msg.flags |= MIPI_DSI_MSG_USE_LPM;

	return rk628_dsi_transfer(rk628, dsi, &msg);
}

int rk628_mipi_dsi_dcs_read(struct rk628 *rk628, u8 cmd, void *data, size_t len)
{
	const struct rk628_dsi *dsi = &rk628->dsi0;
	struct mipi_dsi_msg msg;

	memset(&msg, 0, sizeof(msg));
	msg.channel = dsi->channel;
	msg.type = MIPI_DSI_DCS_READ;
	msg.tx_buf = &cmd;
	msg.tx_len = 1;
	msg.rx_buf = data;
	msg.rx_len = len;

	return rk628_dsi_transfer(rk628, dsi, &msg);
}

static int
panel_simple_xfer_dsi_cmd_seq(struct rk628 *rk628, struct panel_cmds *cmds)
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
			err = rk628_mipi_dsi_generic_write(rk628, cmd->payload,
							   cmd->dchdr.dlen);
			break;
		case MIPI_DSI_DCS_SHORT_WRITE:
		case MIPI_DSI_DCS_SHORT_WRITE_PARAM:
		case MIPI_DSI_DCS_LONG_WRITE:
			err = rk628_mipi_dsi_dcs_write_buffer(rk628, cmd->payload,
							      cmd->dchdr.dlen);
			break;
		default:
			dev_err(rk628->dev, "panel cmd desc invalid data type\n");
			return -EINVAL;
		}

		if (err < 0)
			dev_err(rk628->dev, "failed to write cmd\n");

		if (cmd->dchdr.wait)
			mdelay(cmd->dchdr.wait);
	}

	return 0;
}

static u32 rk628_dsi_get_lane_rate(const struct rk628_dsi *dsi)
{
	const struct rk628_display_mode *mode = &dsi->rk628->dst_mode;
	u32 lane_rate;
	u32 max_lane_rate = 1500;
	u8 bpp, lanes;

	bpp = dsi->bpp;
	lanes = dsi->slave ? dsi->lanes * 2 : dsi->lanes;
	lane_rate = mode->clock / 1000 * bpp / lanes;
	lane_rate = DIV_ROUND_UP(lane_rate * 5, 4);

	if (lane_rate > max_lane_rate)
		lane_rate = max_lane_rate;

	return lane_rate;
}

static void testif_testclk_assert(struct rk628 *rk628,
				  const struct rk628_dsi *dsi)
{
	rk628_i2c_update_bits(rk628, dsi->id ?
			   GRF_MIPI_TX1_CON : GRF_MIPI_TX0_CON,
			   PHY_TESTCLK, PHY_TESTCLK);
	udelay(1);
}

static void testif_testclk_deassert(struct rk628 *rk628,
				    const struct rk628_dsi *dsi)
{
	rk628_i2c_update_bits(rk628, dsi->id ?
			   GRF_MIPI_TX1_CON : GRF_MIPI_TX0_CON,
			   PHY_TESTCLK, 0);
	udelay(1);
}

static void testif_testclr_assert(struct rk628 *rk628,
				  const struct rk628_dsi *dsi)
{
	rk628_i2c_update_bits(rk628, dsi->id ?
			   GRF_MIPI_TX1_CON : GRF_MIPI_TX0_CON,
			   PHY_TESTCLR, PHY_TESTCLR);
	udelay(1);
}

static void testif_testclr_deassert(struct rk628 *rk628,
				    const struct rk628_dsi *dsi)
{
	rk628_i2c_update_bits(rk628, dsi->id ?
			   GRF_MIPI_TX1_CON : GRF_MIPI_TX0_CON,
			   PHY_TESTCLR, 0);
	udelay(1);
}

static void testif_set_data(struct rk628 *rk628,
			    const struct rk628_dsi *dsi, u8 data)
{
	rk628_i2c_update_bits(rk628, dsi->id ?
			   GRF_MIPI_TX1_CON : GRF_MIPI_TX0_CON,
			   PHY_TESTDIN_MASK, PHY_TESTDIN(data));
	udelay(1);
}

static void testif_testen_assert(struct rk628 *rk628,
				 const struct rk628_dsi *dsi)
{
	rk628_i2c_update_bits(rk628, dsi->id ?
			   GRF_MIPI_TX1_CON : GRF_MIPI_TX0_CON,
			   PHY_TESTEN, PHY_TESTEN);
	udelay(1);
}

static void testif_testen_deassert(struct rk628 *rk628,
				   const struct rk628_dsi *dsi)
{
	rk628_i2c_update_bits(rk628,  dsi->id ?
			   GRF_MIPI_TX1_CON : GRF_MIPI_TX0_CON,
			   PHY_TESTEN, 0);
	udelay(1);
}

static void testif_test_code_write(struct rk628 *rk628,
				   const struct rk628_dsi *dsi, u8 test_code)
{
	testif_testclk_assert(rk628, dsi);
	testif_set_data(rk628, dsi, test_code);
	testif_testen_assert(rk628, dsi);
	testif_testclk_deassert(rk628, dsi);
	testif_testen_deassert(rk628, dsi);
}

static void testif_test_data_write(struct rk628 *rk628,
				   const struct rk628_dsi *dsi, u8 test_data)
{
	testif_testclk_deassert(rk628, dsi);
	testif_set_data(rk628, dsi, test_data);
	testif_testclk_assert(rk628, dsi);
}

static u8 testif_get_data(struct rk628 *rk628, const struct rk628_dsi *dsi)
{
	u32 data = 0;

	rk628_i2c_read(rk628, dsi->id ? GRF_DPHY1_STATUS : GRF_DPHY0_STATUS, &data);

	return data >> PHY_TESTDOUT_SHIFT;
}

static void testif_write(struct rk628 *rk628, const struct rk628_dsi *dsi,
			 u8 reg, u8 value)
{
	u8 monitor_data;

	testif_test_code_write(rk628, dsi, reg);
	testif_test_data_write(rk628, dsi, value);
	monitor_data = testif_get_data(rk628, dsi);
	dev_info(rk628->dev, "monitor_data: 0x%x\n", monitor_data);
}

static void mipi_dphy_init(struct rk628 *rk628, const struct rk628_dsi *dsi)
{
	const struct {
		unsigned long max_lane_mbps;
		u8 hsfreqrange;
	} hsfreqrange_table[] = {
		{  90, 0x00}, { 100, 0x10}, { 110, 0x20}, { 130, 0x01},
		{ 140, 0x11}, { 150, 0x21}, { 170, 0x02}, { 180, 0x12},
		{ 200, 0x22}, { 220, 0x03}, { 240, 0x13}, { 250, 0x23},
		{ 270, 0x04}, { 300, 0x14}, { 330, 0x05}, { 360, 0x15},
		{ 400, 0x25}, { 450, 0x06}, { 500, 0x16}, { 550, 0x07},
		{ 600, 0x17}, { 650, 0x08}, { 700, 0x18}, { 750, 0x09},
		{ 800, 0x19}, { 850, 0x29}, { 900, 0x39}, { 950, 0x0a},
		{1000, 0x1a}, {1050, 0x2a}, {1100, 0x3a}, {1150, 0x0b},
		{1200, 0x1b}, {1250, 0x2b}, {1300, 0x3b}, {1350, 0x0c},
		{1400, 0x1c}, {1450, 0x2c}, {1500, 0x3c}
	};
	u8 hsfreqrange;
	unsigned int index;

	for (index = 0; index < ARRAY_SIZE(hsfreqrange_table); index++)
		if (lane_mbps <= hsfreqrange_table[index].max_lane_mbps)
			break;

	if (index == ARRAY_SIZE(hsfreqrange_table))
		--index;

	hsfreqrange = hsfreqrange_table[index].hsfreqrange;
	testif_write(rk628, dsi, 0x44, HSFREQRANGE(hsfreqrange));
}

static void mipi_dphy_power_on(struct rk628 *rk628, const struct rk628_dsi *dsi)
{
	int dev_id;
	unsigned int dsi_base;
	unsigned int val, mask;
	int ret;

	dev_id = dsi->id ? RK628_DEV_DSI1 : RK628_DEV_DSI0;
	dsi_base = dsi->id ? DSI1_BASE : DSI0_BASE;

	dsi_update_bits(rk628, dsi, DSI_PHY_RSTZ, PHY_ENABLECLK, 0);
	dsi_update_bits(rk628, dsi, DSI_PHY_RSTZ, PHY_SHUTDOWNZ, 0);
	dsi_update_bits(rk628, dsi, DSI_PHY_RSTZ, PHY_RSTZ, 0);
	testif_testclr_assert(rk628, dsi);

	/* Set all REQUEST inputs to zero */
	rk628_i2c_update_bits(rk628, dsi->id ?
			      GRF_MIPI_TX1_CON : GRF_MIPI_TX0_CON,
			      FORCERXMODE_MASK | FORCETXSTOPMODE_MASK,
			      FORCETXSTOPMODE(0) | FORCERXMODE(0));
	udelay(1);

	testif_testclr_deassert(rk628, dsi);

	mipi_dphy_init(rk628, dsi);

	dsi_update_bits(rk628, dsi, DSI_PHY_RSTZ,
			PHY_ENABLECLK, PHY_ENABLECLK);
	dsi_update_bits(rk628, dsi, DSI_PHY_RSTZ,
			PHY_SHUTDOWNZ, PHY_SHUTDOWNZ);
	dsi_update_bits(rk628, dsi, DSI_PHY_RSTZ, PHY_RSTZ, PHY_RSTZ);
	usleep_range(1500, 2000);

	rk628_combtxphy_power_on(rk628);

	ret = regmap_read_poll_timeout(rk628->regmap[dev_id],
				       dsi_base + DSI_PHY_STATUS,
				       val, val & PHY_LOCK, 0, 1000);
	if (ret < 0)
		dev_err(rk628->dev, "PHY is not locked\n");

	usleep_range(100, 200);

	mask = PHY_STOPSTATELANE;
	ret = regmap_read_poll_timeout(rk628->regmap[dev_id],
				       dsi_base + DSI_PHY_STATUS,
				       val, (val & mask) == mask,
				       0, 1000);
	if (ret < 0)
		dev_err(rk628->dev, "lane module is not in stop state\n");

	udelay(10);
}

void rk628_dsi0_reset_control_assert(struct rk628 *rk628)
{
	rk628_i2c_write(rk628, CRU_SOFTRST_CON02, 0x400040);
}

void rk628_dsi0_reset_control_deassert(struct rk628 *rk628)
{
	rk628_i2c_write(rk628, CRU_SOFTRST_CON02, 0x400000);
}

void rk628_dsi1_reset_control_assert(struct rk628 *rk628)
{
	rk628_i2c_write(rk628, CRU_SOFTRST_CON02, 0x800080);
}

void rk628_dsi1_reset_control_deassert(struct rk628 *rk628)
{
	rk628_i2c_write(rk628, CRU_SOFTRST_CON02, 0x800000);
}

void rk628_dsi_bridge_pre_enable(struct rk628 *rk628,
				 const struct rk628_dsi *dsi)
{
	u32 val;

	dsi_write(rk628, dsi, DSI_PWR_UP, RESET);
	dsi_write(rk628, dsi, DSI_MODE_CFG, CMD_VIDEO_MODE(COMMAND_MODE));

	val = DIV_ROUND_UP(lane_mbps >> 3, 20);
	dsi_write(rk628, dsi, DSI_CLKMGR_CFG,
		  TO_CLK_DIVISION(10) | TX_ESC_CLK_DIVISION(val));

	val = CRC_RX_EN | ECC_RX_EN | BTA_EN | EOTP_TX_EN;
	if (dsi->mode_flags & MIPI_DSI_MODE_EOT_PACKET)
		val &= ~EOTP_TX_EN;

	dsi_write(rk628, dsi, DSI_PCKHDL_CFG, val);

	dsi_write(rk628, dsi, DSI_TO_CNT_CFG,
		  HSTX_TO_CNT(1000) | LPRX_TO_CNT(1000));
	dsi_write(rk628, dsi, DSI_BTA_TO_CNT, 0xd00);
	dsi_write(rk628, dsi, DSI_PHY_TMR_CFG,
		  PHY_HS2LP_TIME(0x14) | PHY_LP2HS_TIME(0x10) |
		  MAX_RD_TIME(10000));
	dsi_write(rk628, dsi, DSI_PHY_TMR_LPCLK_CFG,
		  PHY_CLKHS2LP_TIME(0x40) | PHY_CLKLP2HS_TIME(0x40));
	dsi_write(rk628, dsi, DSI_PHY_IF_CFG,
		  PHY_STOP_WAIT_TIME(0x20) | N_LANES(dsi->lanes - 1));

	mipi_dphy_power_on(rk628, dsi);

	dsi_write(rk628, dsi, DSI_PWR_UP, POWER_UP);
}

static void rk628_dsi_set_vid_mode(struct rk628 *rk628,
				   const struct rk628_dsi *dsi,
				   const struct rk628_display_mode *mode)
{
	unsigned int lanebyteclk = (lane_mbps * 1000L) >> 3;
	unsigned int dpipclk = mode->clock;
	u32 hline, hsa, hbp, hline_time, hsa_time, hbp_time;
	u32 vactive, vsa, vfp, vbp;
	u32 val;
	int pkt_size;

	val = LP_HFP_EN | LP_HBP_EN | LP_VACT_EN | LP_VFP_EN | LP_VBP_EN |
	      LP_VSA_EN;

	if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO_HFP)
		val &= ~LP_HFP_EN;

	if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO_HBP)
		val &= ~LP_HBP_EN;

	if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO_BURST)
		val |= VID_MODE_TYPE_BURST;
	else if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO_SYNC_PULSE)
		val |= VID_MODE_TYPE_NON_BURST_SYNC_PULSES;
	else
		val |= VID_MODE_TYPE_NON_BURST_SYNC_EVENTS;

	dsi_write(rk628, dsi, DSI_VID_MODE_CFG, val);

	if (dsi->mode_flags & MIPI_DSI_CLOCK_NON_CONTINUOUS)
		dsi_update_bits(rk628, dsi, DSI_LPCLK_CTRL,
				AUTO_CLKLANE_CTRL, AUTO_CLKLANE_CTRL);

	if (dsi->slave || dsi->master)
		pkt_size = VID_PKT_SIZE(mode->hdisplay / 2);
	else
		pkt_size = VID_PKT_SIZE(mode->hdisplay);

	dsi_write(rk628, dsi, DSI_VID_PKT_SIZE, pkt_size);

	vactive = mode->vdisplay;
	vsa = mode->vsync_end - mode->vsync_start;
	vfp = mode->vsync_start - mode->vdisplay;
	vbp = mode->vtotal - mode->vsync_end;
	hsa = mode->hsync_end - mode->hsync_start;
	hbp = mode->htotal - mode->hsync_end;
	hline = mode->htotal;

	//hline_time = hline * lanebyteclk / dpipclk;
	hline_time = DIV_ROUND_CLOSEST_ULL(hline * lanebyteclk, dpipclk);
	dsi_write(rk628, dsi, DSI_VID_HLINE_TIME,
		  VID_HLINE_TIME(hline_time));
	//hsa_time = hsa * lanebyteclk / dpipclk;
	hsa_time = DIV_ROUND_CLOSEST_ULL(hsa * lanebyteclk, dpipclk);
	dsi_write(rk628, dsi, DSI_VID_HSA_TIME, VID_HSA_TIME(hsa_time));
	//hbp_time = hbp * lanebyteclk / dpipclk;
	hbp_time = DIV_ROUND_CLOSEST_ULL(hbp * lanebyteclk, dpipclk);
	dsi_write(rk628, dsi, DSI_VID_HBP_TIME, VID_HBP_TIME(hbp_time));

	dsi_write(rk628, dsi, DSI_VID_VACTIVE_LINES, vactive);
	dsi_write(rk628, dsi, DSI_VID_VSA_LINES, vsa);
	dsi_write(rk628, dsi, DSI_VID_VFP_LINES, vfp);
	dsi_write(rk628, dsi, DSI_VID_VBP_LINES, vbp);

	dsi_write(rk628, dsi, DSI_MODE_CFG, CMD_VIDEO_MODE(VIDEO_MODE));
}

static void rk628_dsi_set_cmd_mode(struct rk628 *rk628,
				   const struct rk628_dsi *dsi,
				   const struct rk628_display_mode *mode)
{
	dsi_update_bits(rk628, dsi, DSI_CMD_MODE_CFG, DCS_LW_TX, 0);
	dsi_write(rk628, dsi, DSI_EDPI_CMD_SIZE,
		  EDPI_ALLOWED_CMD_SIZE(mode->hdisplay));
	dsi_write(rk628, dsi, DSI_MODE_CFG, CMD_VIDEO_MODE(COMMAND_MODE));
}

static void rk628_dsi_bridge_enable(struct rk628 *rk628,
				    const struct rk628_dsi *dsi)
{
	u32 val;
	const struct rk628_display_mode *mode = &rk628->dst_mode;

	dsi_write(rk628, dsi, DSI_PWR_UP, RESET);

	switch (dsi->bus_format) {
	case MIPI_DSI_FMT_RGB666:
		val = DPI_COLOR_CODING(DPI_COLOR_CODING_18BIT_2) |
		      LOOSELY18_EN;
		break;
	case MIPI_DSI_FMT_RGB666_PACKED:
		val = DPI_COLOR_CODING(DPI_COLOR_CODING_18BIT_1);
		break;
	case MIPI_DSI_FMT_RGB565:
		val = DPI_COLOR_CODING(DPI_COLOR_CODING_16BIT_1);
		break;
	case MIPI_DSI_FMT_RGB888:
	default:
		val = DPI_COLOR_CODING(DPI_COLOR_CODING_24BIT);
		break;
	}

	dsi_write(rk628, dsi, DSI_DPI_COLOR_CODING, val);

	val = 0;
	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		val |= VSYNC_ACTIVE_LOW;
	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		val |= HSYNC_ACTIVE_LOW;

	dsi_write(rk628, dsi, DSI_DPI_CFG_POL, val);

	dsi_write(rk628, dsi, DSI_DPI_VCID, DPI_VID(0));
	dsi_write(rk628, dsi, DSI_DPI_LP_CMD_TIM,
		  OUTVACT_LPCMD_TIME(4) | INVACT_LPCMD_TIME(4));
	dsi_update_bits(rk628, dsi, DSI_LPCLK_CTRL,
			PHY_TXREQUESTCLKHS, PHY_TXREQUESTCLKHS);

	if (dsi->mode_flags & MIPI_DSI_MODE_VIDEO)
		rk628_dsi_set_vid_mode(rk628, dsi, mode);
	else
		rk628_dsi_set_cmd_mode(rk628, dsi, mode);

	dsi_write(rk628, dsi, DSI_PWR_UP, POWER_UP);
}

void rk628_mipi_dsi_pre_enable(struct rk628 *rk628)
{
	const struct rk628_dsi *dsi = &rk628->dsi0;
	const struct rk628_dsi *dsi1 = &rk628->dsi1;
	u32 rate = rk628_dsi_get_lane_rate(dsi);
	int bus_width;

	rk628_i2c_update_bits(rk628, GRF_SYSTEM_CON0, SW_OUTPUT_MODE_MASK,
			      SW_OUTPUT_MODE(OUTPUT_MODE_DSI));
	rk628_i2c_update_bits(rk628, GRF_POST_PROC_CON, SW_SPLIT_EN,
			      dsi->slave ? SW_SPLIT_EN : 0);

	bus_width = rate << 8;
	if (dsi->slave)
		bus_width |= COMBTXPHY_MODULEA_EN | COMBTXPHY_MODULEB_EN;
	else if (dsi->id)
		bus_width |= COMBTXPHY_MODULEB_EN;
	else
		bus_width |= COMBTXPHY_MODULEA_EN;

	rk628_combtxphy_set_bus_width(rk628, bus_width);
	rk628_combtxphy_set_mode(rk628, PHY_MODE_VIDEO_MIPI);
	lane_mbps = rk628_combtxphy_get_bus_width(rk628);

	if (dsi->slave)
		lane_mbps = rk628_combtxphy_get_bus_width(rk628);

	/* rst for dsi0 */
	rk628_dsi0_reset_control_assert(rk628);
	usleep_range(20, 40);
	rk628_dsi0_reset_control_deassert(rk628);
	usleep_range(20, 40);

	rk628_dsi_bridge_pre_enable(rk628, dsi);

	if (dsi->slave) {
		/* rst for dsi1 */
		rk628_dsi1_reset_control_assert(rk628);
		usleep_range(20, 40);
		rk628_dsi1_reset_control_deassert(rk628);
		usleep_range(20, 40);

		rk628_dsi_bridge_pre_enable(rk628, dsi1);
	}

	rk628_panel_prepare(rk628);
	panel_simple_xfer_dsi_cmd_seq(rk628, rk628->panel->on_cmds);

#ifdef DSI_READ_POWER_MODE
	u8 mode;

	rk628_mipi_dsi_dcs_read(rk628, MIPI_DCS_GET_POWER_MODE,
				&mode, sizeof(mode));

	dev_info(rk628->dev, "dsi: mode: 0x%x\n", mode);
#endif

	dev_info(rk628->dev, "rk628_dsi final DSI-Link bandwidth: %d x %d\n",
		 lane_mbps, dsi->slave ? dsi->lanes * 2 : dsi->lanes);
}

void rk628_mipi_dsi_enable(struct rk628 *rk628)
{
	const struct rk628_dsi *dsi = &rk628->dsi0;
	const struct rk628_dsi *dsi1 = &rk628->dsi1;

	rk628_dsi_bridge_enable(rk628, dsi);

	if (dsi->slave)
		rk628_dsi_bridge_enable(rk628, dsi1);

	rk628_panel_enable(rk628);
}

void rk628_dsi_disable(struct rk628 *rk628)
{
	const struct rk628_dsi *dsi = &rk628->dsi0;
	const struct rk628_dsi *dsi1 = &rk628->dsi1;

	rk628_panel_disable(rk628);

	dsi_write(rk628, dsi, DSI_PWR_UP, RESET);
	dsi_write(rk628, dsi, DSI_LPCLK_CTRL, 0);
	dsi_write(rk628, dsi, DSI_EDPI_CMD_SIZE, 0);
	dsi_write(rk628, dsi, DSI_MODE_CFG, CMD_VIDEO_MODE(COMMAND_MODE));
	dsi_write(rk628, dsi, DSI_PWR_UP, POWER_UP);

	//dsi_write(rk628, dsi, DSI_PHY_RSTZ, 0);

	if (dsi->slave) {
		dsi_write(rk628, dsi1, DSI_PWR_UP, RESET);
		dsi_write(rk628, dsi1, DSI_LPCLK_CTRL, 0);
		dsi_write(rk628, dsi1, DSI_EDPI_CMD_SIZE, 0);
		dsi_write(rk628, dsi1, DSI_MODE_CFG,
			  CMD_VIDEO_MODE(COMMAND_MODE));
		dsi_write(rk628, dsi1, DSI_PWR_UP, POWER_UP);

		//dsi_write(rk628, dsi1, DSI_PHY_RSTZ, 0);
	}

	panel_simple_xfer_dsi_cmd_seq(rk628, rk628->panel->off_cmds);
	rk628_panel_unprepare(rk628);
	rk628_combtxphy_power_off(rk628);
}
