// SPDX-License-Identifier: GPL-2.0+
/*
 * i.MX8 NWL MIPI DSI host driver
 *
 * Copyright (C) 2017 NXP
 * Copyright (C) 2020 Purism SPC
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/irq.h>
#include <linux/math64.h>
#include <linux/mfd/syscon.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/mux/consumer.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/phy/phy.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/sys_soc.h>
#include <linux/time64.h>

#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_of.h>
#include <drm/drm_print.h>

#include <video/mipi_display.h>

#include "nwl-dsi.h"

#define DRV_NAME "nwl-dsi"

/* i.MX8 NWL quirks */
/* i.MX8MQ errata E11418 */
#define E11418_HS_MODE_QUIRK	BIT(0)

#define NWL_DSI_MIPI_FIFO_TIMEOUT msecs_to_jiffies(500)

enum transfer_direction {
	DSI_PACKET_SEND,
	DSI_PACKET_RECEIVE,
};

#define NWL_DSI_ENDPOINT_LCDIF 0
#define NWL_DSI_ENDPOINT_DCSS 1

struct nwl_dsi_transfer {
	const struct mipi_dsi_msg *msg;
	struct mipi_dsi_packet packet;
	struct completion completed;

	int status; /* status of transmission */
	enum transfer_direction direction;
	bool need_bta;
	u8 cmd;
	u16 rx_word_count;
	size_t tx_len; /* in bytes */
	size_t rx_len; /* in bytes */
};

struct nwl_dsi {
	struct drm_bridge bridge;
	struct mipi_dsi_host dsi_host;
	struct device *dev;
	struct phy *phy;
	union phy_configure_opts phy_cfg;
	unsigned int quirks;

	struct regmap *regmap;
	int irq;
	/*
	 * The DSI host controller needs this reset sequence according to NWL:
	 * 1. Deassert pclk reset to get access to DSI regs
	 * 2. Configure DSI Host and DPHY and enable DPHY
	 * 3. Deassert ESC and BYTE resets to allow host TX operations)
	 * 4. Send DSI cmds to configure peripheral (handled by panel drv)
	 * 5. Deassert DPI reset so DPI receives pixels and starts sending
	 *    DSI data
	 *
	 * TODO: Since panel_bridges do their DSI setup in enable we
	 * currently have 4. and 5. swapped.
	 */
	struct reset_control *rst_byte;
	struct reset_control *rst_esc;
	struct reset_control *rst_dpi;
	struct reset_control *rst_pclk;
	struct mux_control *mux;

	/* DSI clocks */
	struct clk *phy_ref_clk;
	struct clk *rx_esc_clk;
	struct clk *tx_esc_clk;
	struct clk *core_clk;
	/*
	 * hardware bug: the i.MX8MQ needs this clock on during reset
	 * even when not using LCDIF.
	 */
	struct clk *lcdif_clk;

	/* dsi lanes */
	u32 lanes;
	enum mipi_dsi_pixel_format format;
	struct drm_display_mode mode;
	unsigned long dsi_mode_flags;
	int error;

	struct nwl_dsi_transfer *xfer;
};

static const struct regmap_config nwl_dsi_regmap_config = {
	.reg_bits = 16,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = NWL_DSI_IRQ_MASK2,
	.name = DRV_NAME,
};

static inline struct nwl_dsi *bridge_to_dsi(struct drm_bridge *bridge)
{
	return container_of(bridge, struct nwl_dsi, bridge);
}

static int nwl_dsi_clear_error(struct nwl_dsi *dsi)
{
	int ret = dsi->error;

	dsi->error = 0;
	return ret;
}

static void nwl_dsi_write(struct nwl_dsi *dsi, unsigned int reg, u32 val)
{
	int ret;

	if (dsi->error)
		return;

	ret = regmap_write(dsi->regmap, reg, val);
	if (ret < 0) {
		DRM_DEV_ERROR(dsi->dev,
			      "Failed to write NWL DSI reg 0x%x: %d\n", reg,
			      ret);
		dsi->error = ret;
	}
}

static u32 nwl_dsi_read(struct nwl_dsi *dsi, u32 reg)
{
	unsigned int val;
	int ret;

	if (dsi->error)
		return 0;

	ret = regmap_read(dsi->regmap, reg, &val);
	if (ret < 0) {
		DRM_DEV_ERROR(dsi->dev, "Failed to read NWL DSI reg 0x%x: %d\n",
			      reg, ret);
		dsi->error = ret;
	}
	return val;
}

static int nwl_dsi_get_dpi_pixel_format(enum mipi_dsi_pixel_format format)
{
	switch (format) {
	case MIPI_DSI_FMT_RGB565:
		return NWL_DSI_PIXEL_FORMAT_16;
	case MIPI_DSI_FMT_RGB666:
		return NWL_DSI_PIXEL_FORMAT_18L;
	case MIPI_DSI_FMT_RGB666_PACKED:
		return NWL_DSI_PIXEL_FORMAT_18;
	case MIPI_DSI_FMT_RGB888:
		return NWL_DSI_PIXEL_FORMAT_24;
	default:
		return -EINVAL;
	}
}

/*
 * ps2bc - Picoseconds to byte clock cycles
 */
static u32 ps2bc(struct nwl_dsi *dsi, unsigned long long ps)
{
	u32 bpp = mipi_dsi_pixel_format_to_bpp(dsi->format);

	return DIV64_U64_ROUND_UP(ps * dsi->mode.clock * bpp,
				  dsi->lanes * 8ULL * NSEC_PER_SEC);
}

/*
 * ui2bc - UI time periods to byte clock cycles
 */
static u32 ui2bc(unsigned int ui)
{
	return DIV_ROUND_UP(ui, BITS_PER_BYTE);
}

/*
 * us2bc - micro seconds to lp clock cycles
 */
static u32 us2lp(u32 lp_clk_rate, unsigned long us)
{
	return DIV_ROUND_UP(us * lp_clk_rate, USEC_PER_SEC);
}

static int nwl_dsi_config_host(struct nwl_dsi *dsi)
{
	u32 cycles;
	struct phy_configure_opts_mipi_dphy *cfg = &dsi->phy_cfg.mipi_dphy;

	if (dsi->lanes < 1 || dsi->lanes > 4)
		return -EINVAL;

	DRM_DEV_DEBUG_DRIVER(dsi->dev, "DSI Lanes %d\n", dsi->lanes);
	nwl_dsi_write(dsi, NWL_DSI_CFG_NUM_LANES, dsi->lanes - 1);

	if (dsi->dsi_mode_flags & MIPI_DSI_CLOCK_NON_CONTINUOUS) {
		nwl_dsi_write(dsi, NWL_DSI_CFG_NONCONTINUOUS_CLK, 0x01);
		nwl_dsi_write(dsi, NWL_DSI_CFG_AUTOINSERT_EOTP, 0x01);
	} else {
		nwl_dsi_write(dsi, NWL_DSI_CFG_NONCONTINUOUS_CLK, 0x00);
		nwl_dsi_write(dsi, NWL_DSI_CFG_AUTOINSERT_EOTP, 0x00);
	}

	/* values in byte clock cycles */
	cycles = ui2bc(cfg->clk_pre);
	DRM_DEV_DEBUG_DRIVER(dsi->dev, "cfg_t_pre: 0x%x\n", cycles);
	nwl_dsi_write(dsi, NWL_DSI_CFG_T_PRE, cycles);
	cycles = ps2bc(dsi, cfg->lpx + cfg->clk_prepare + cfg->clk_zero);
	DRM_DEV_DEBUG_DRIVER(dsi->dev, "cfg_tx_gap (pre): 0x%x\n", cycles);
	cycles += ui2bc(cfg->clk_pre);
	DRM_DEV_DEBUG_DRIVER(dsi->dev, "cfg_t_post: 0x%x\n", cycles);
	nwl_dsi_write(dsi, NWL_DSI_CFG_T_POST, cycles);
	cycles = ps2bc(dsi, cfg->hs_exit);
	DRM_DEV_DEBUG_DRIVER(dsi->dev, "cfg_tx_gap: 0x%x\n", cycles);
	nwl_dsi_write(dsi, NWL_DSI_CFG_TX_GAP, cycles);

	nwl_dsi_write(dsi, NWL_DSI_CFG_EXTRA_CMDS_AFTER_EOTP, 0x01);
	nwl_dsi_write(dsi, NWL_DSI_CFG_HTX_TO_COUNT, 0x00);
	nwl_dsi_write(dsi, NWL_DSI_CFG_LRX_H_TO_COUNT, 0x00);
	nwl_dsi_write(dsi, NWL_DSI_CFG_BTA_H_TO_COUNT, 0x00);
	/* In LP clock cycles */
	cycles = us2lp(cfg->lp_clk_rate, cfg->wakeup);
	DRM_DEV_DEBUG_DRIVER(dsi->dev, "cfg_twakeup: 0x%x\n", cycles);
	nwl_dsi_write(dsi, NWL_DSI_CFG_TWAKEUP, cycles);

	return nwl_dsi_clear_error(dsi);
}

static int nwl_dsi_config_dpi(struct nwl_dsi *dsi)
{
	u32 mode;
	int color_format;
	bool burst_mode;
	int hfront_porch, hback_porch, vfront_porch, vback_porch;
	int hsync_len, vsync_len;

	hfront_porch = dsi->mode.hsync_start - dsi->mode.hdisplay;
	hsync_len = dsi->mode.hsync_end - dsi->mode.hsync_start;
	hback_porch = dsi->mode.htotal - dsi->mode.hsync_end;

	vfront_porch = dsi->mode.vsync_start - dsi->mode.vdisplay;
	vsync_len = dsi->mode.vsync_end - dsi->mode.vsync_start;
	vback_porch = dsi->mode.vtotal - dsi->mode.vsync_end;

	DRM_DEV_DEBUG_DRIVER(dsi->dev, "hfront_porch = %d\n", hfront_porch);
	DRM_DEV_DEBUG_DRIVER(dsi->dev, "hback_porch = %d\n", hback_porch);
	DRM_DEV_DEBUG_DRIVER(dsi->dev, "hsync_len = %d\n", hsync_len);
	DRM_DEV_DEBUG_DRIVER(dsi->dev, "hdisplay = %d\n", dsi->mode.hdisplay);
	DRM_DEV_DEBUG_DRIVER(dsi->dev, "vfront_porch = %d\n", vfront_porch);
	DRM_DEV_DEBUG_DRIVER(dsi->dev, "vback_porch = %d\n", vback_porch);
	DRM_DEV_DEBUG_DRIVER(dsi->dev, "vsync_len = %d\n", vsync_len);
	DRM_DEV_DEBUG_DRIVER(dsi->dev, "vactive = %d\n", dsi->mode.vdisplay);
	DRM_DEV_DEBUG_DRIVER(dsi->dev, "clock = %d kHz\n", dsi->mode.clock);

	color_format = nwl_dsi_get_dpi_pixel_format(dsi->format);
	if (color_format < 0) {
		DRM_DEV_ERROR(dsi->dev, "Invalid color format 0x%x\n",
			      dsi->format);
		return color_format;
	}
	DRM_DEV_DEBUG_DRIVER(dsi->dev, "pixel fmt = %d\n", dsi->format);

	nwl_dsi_write(dsi, NWL_DSI_INTERFACE_COLOR_CODING, NWL_DSI_DPI_24_BIT);
	nwl_dsi_write(dsi, NWL_DSI_PIXEL_FORMAT, color_format);
	/*
	 * Adjusting input polarity based on the video mode results in
	 * a black screen so always pick active low:
	 */
	nwl_dsi_write(dsi, NWL_DSI_VSYNC_POLARITY,
		      NWL_DSI_VSYNC_POLARITY_ACTIVE_LOW);
	nwl_dsi_write(dsi, NWL_DSI_HSYNC_POLARITY,
		      NWL_DSI_HSYNC_POLARITY_ACTIVE_LOW);

	burst_mode = (dsi->dsi_mode_flags & MIPI_DSI_MODE_VIDEO_BURST) &&
		     !(dsi->dsi_mode_flags & MIPI_DSI_MODE_VIDEO_SYNC_PULSE);

	if (burst_mode) {
		nwl_dsi_write(dsi, NWL_DSI_VIDEO_MODE, NWL_DSI_VM_BURST_MODE);
		nwl_dsi_write(dsi, NWL_DSI_PIXEL_FIFO_SEND_LEVEL, 256);
	} else {
		mode = ((dsi->dsi_mode_flags & MIPI_DSI_MODE_VIDEO_SYNC_PULSE) ?
				NWL_DSI_VM_BURST_MODE_WITH_SYNC_PULSES :
				NWL_DSI_VM_NON_BURST_MODE_WITH_SYNC_EVENTS);
		nwl_dsi_write(dsi, NWL_DSI_VIDEO_MODE, mode);
		nwl_dsi_write(dsi, NWL_DSI_PIXEL_FIFO_SEND_LEVEL,
			      dsi->mode.hdisplay);
	}

	nwl_dsi_write(dsi, NWL_DSI_HFP, hfront_porch);
	nwl_dsi_write(dsi, NWL_DSI_HBP, hback_porch);
	nwl_dsi_write(dsi, NWL_DSI_HSA, hsync_len);

	nwl_dsi_write(dsi, NWL_DSI_ENABLE_MULT_PKTS, 0x0);
	nwl_dsi_write(dsi, NWL_DSI_BLLP_MODE, 0x1);
	nwl_dsi_write(dsi, NWL_DSI_USE_NULL_PKT_BLLP, 0x0);
	nwl_dsi_write(dsi, NWL_DSI_VC, 0x0);

	nwl_dsi_write(dsi, NWL_DSI_PIXEL_PAYLOAD_SIZE, dsi->mode.hdisplay);
	nwl_dsi_write(dsi, NWL_DSI_VACTIVE, dsi->mode.vdisplay - 1);
	nwl_dsi_write(dsi, NWL_DSI_VBP, vback_porch);
	nwl_dsi_write(dsi, NWL_DSI_VFP, vfront_porch);

	return nwl_dsi_clear_error(dsi);
}

static int nwl_dsi_init_interrupts(struct nwl_dsi *dsi)
{
	u32 irq_enable = ~(u32)(NWL_DSI_TX_PKT_DONE_MASK |
				NWL_DSI_RX_PKT_HDR_RCVD_MASK |
				NWL_DSI_TX_FIFO_OVFLW_MASK |
				NWL_DSI_HS_TX_TIMEOUT_MASK);

	nwl_dsi_write(dsi, NWL_DSI_IRQ_MASK, irq_enable);
	nwl_dsi_write(dsi, NWL_DSI_IRQ_MASK2, 0x7);

	return nwl_dsi_clear_error(dsi);
}

static int nwl_dsi_host_attach(struct mipi_dsi_host *dsi_host,
			       struct mipi_dsi_device *device)
{
	struct nwl_dsi *dsi = container_of(dsi_host, struct nwl_dsi, dsi_host);
	struct device *dev = dsi->dev;

	DRM_DEV_INFO(dev, "lanes=%u, format=0x%x flags=0x%lx\n", device->lanes,
		     device->format, device->mode_flags);

	if (device->lanes < 1 || device->lanes > 4)
		return -EINVAL;

	dsi->lanes = device->lanes;
	dsi->format = device->format;
	dsi->dsi_mode_flags = device->mode_flags;

	return 0;
}

static bool nwl_dsi_read_packet(struct nwl_dsi *dsi, u32 status)
{
	struct device *dev = dsi->dev;
	struct nwl_dsi_transfer *xfer = dsi->xfer;
	int err;
	u8 *payload = xfer->msg->rx_buf;
	u32 val;
	u16 word_count;
	u8 channel;
	u8 data_type;

	xfer->status = 0;

	if (xfer->rx_word_count == 0) {
		if (!(status & NWL_DSI_RX_PKT_HDR_RCVD))
			return false;
		/* Get the RX header and parse it */
		val = nwl_dsi_read(dsi, NWL_DSI_RX_PKT_HEADER);
		err = nwl_dsi_clear_error(dsi);
		if (err)
			xfer->status = err;
		word_count = NWL_DSI_WC(val);
		channel = NWL_DSI_RX_VC(val);
		data_type = NWL_DSI_RX_DT(val);

		if (channel != xfer->msg->channel) {
			DRM_DEV_ERROR(dev,
				      "[%02X] Channel mismatch (%u != %u)\n",
				      xfer->cmd, channel, xfer->msg->channel);
			xfer->status = -EINVAL;
			return true;
		}

		switch (data_type) {
		case MIPI_DSI_RX_GENERIC_SHORT_READ_RESPONSE_2BYTE:
		case MIPI_DSI_RX_DCS_SHORT_READ_RESPONSE_2BYTE:
			if (xfer->msg->rx_len > 1) {
				/* read second byte */
				payload[1] = word_count >> 8;
				++xfer->rx_len;
			}
			fallthrough;
		case MIPI_DSI_RX_GENERIC_SHORT_READ_RESPONSE_1BYTE:
		case MIPI_DSI_RX_DCS_SHORT_READ_RESPONSE_1BYTE:
			if (xfer->msg->rx_len > 0) {
				/* read first byte */
				payload[0] = word_count & 0xff;
				++xfer->rx_len;
			}
			xfer->status = xfer->rx_len;
			return true;
		case MIPI_DSI_RX_ACKNOWLEDGE_AND_ERROR_REPORT:
			word_count &= 0xff;
			DRM_DEV_ERROR(dev, "[%02X] DSI error report: 0x%02x\n",
				      xfer->cmd, word_count);
			xfer->status = -EPROTO;
			return true;
		}

		if (word_count > xfer->msg->rx_len) {
			DRM_DEV_ERROR(dev,
				"[%02X] Receive buffer too small: %zu (< %u)\n",
				xfer->cmd, xfer->msg->rx_len, word_count);
			xfer->status = -EINVAL;
			return true;
		}

		xfer->rx_word_count = word_count;
	} else {
		/* Set word_count from previous header read */
		word_count = xfer->rx_word_count;
	}

	/* If RX payload is not yet received, wait for it */
	if (!(status & NWL_DSI_RX_PKT_PAYLOAD_DATA_RCVD))
		return false;

	/* Read the RX payload */
	while (word_count >= 4) {
		val = nwl_dsi_read(dsi, NWL_DSI_RX_PAYLOAD);
		payload[0] = (val >> 0) & 0xff;
		payload[1] = (val >> 8) & 0xff;
		payload[2] = (val >> 16) & 0xff;
		payload[3] = (val >> 24) & 0xff;
		payload += 4;
		xfer->rx_len += 4;
		word_count -= 4;
	}

	if (word_count > 0) {
		val = nwl_dsi_read(dsi, NWL_DSI_RX_PAYLOAD);
		switch (word_count) {
		case 3:
			payload[2] = (val >> 16) & 0xff;
			++xfer->rx_len;
			fallthrough;
		case 2:
			payload[1] = (val >> 8) & 0xff;
			++xfer->rx_len;
			fallthrough;
		case 1:
			payload[0] = (val >> 0) & 0xff;
			++xfer->rx_len;
			break;
		}
	}

	xfer->status = xfer->rx_len;
	err = nwl_dsi_clear_error(dsi);
	if (err)
		xfer->status = err;

	return true;
}

static void nwl_dsi_finish_transmission(struct nwl_dsi *dsi, u32 status)
{
	struct nwl_dsi_transfer *xfer = dsi->xfer;
	bool end_packet = false;

	if (!xfer)
		return;

	if (xfer->direction == DSI_PACKET_SEND &&
	    status & NWL_DSI_TX_PKT_DONE) {
		xfer->status = xfer->tx_len;
		end_packet = true;
	} else if (status & NWL_DSI_DPHY_DIRECTION &&
		   ((status & (NWL_DSI_RX_PKT_HDR_RCVD |
			       NWL_DSI_RX_PKT_PAYLOAD_DATA_RCVD)))) {
		end_packet = nwl_dsi_read_packet(dsi, status);
	}

	if (end_packet)
		complete(&xfer->completed);
}

static void nwl_dsi_begin_transmission(struct nwl_dsi *dsi)
{
	struct nwl_dsi_transfer *xfer = dsi->xfer;
	struct mipi_dsi_packet *pkt = &xfer->packet;
	const u8 *payload;
	size_t length;
	u16 word_count;
	u8 hs_mode;
	u32 val;
	u32 hs_workaround = 0;

	/* Send the payload, if any */
	length = pkt->payload_length;
	payload = pkt->payload;

	while (length >= 4) {
		val = *(u32 *)payload;
		hs_workaround |= !(val & 0xFFFF00);
		nwl_dsi_write(dsi, NWL_DSI_TX_PAYLOAD, val);
		payload += 4;
		length -= 4;
	}
	/* Send the rest of the payload */
	val = 0;
	switch (length) {
	case 3:
		val |= payload[2] << 16;
		fallthrough;
	case 2:
		val |= payload[1] << 8;
		hs_workaround |= !(val & 0xFFFF00);
		fallthrough;
	case 1:
		val |= payload[0];
		nwl_dsi_write(dsi, NWL_DSI_TX_PAYLOAD, val);
		break;
	}
	xfer->tx_len = pkt->payload_length;

	/*
	 * Send the header
	 * header[0] = Virtual Channel + Data Type
	 * header[1] = Word Count LSB (LP) or first param (SP)
	 * header[2] = Word Count MSB (LP) or second param (SP)
	 */
	word_count = pkt->header[1] | (pkt->header[2] << 8);
	if (hs_workaround && (dsi->quirks & E11418_HS_MODE_QUIRK)) {
		DRM_DEV_DEBUG_DRIVER(dsi->dev,
				     "Using hs mode workaround for cmd 0x%x\n",
				     xfer->cmd);
		hs_mode = 1;
	} else {
		hs_mode = (xfer->msg->flags & MIPI_DSI_MSG_USE_LPM) ? 0 : 1;
	}
	val = NWL_DSI_WC(word_count) | NWL_DSI_TX_VC(xfer->msg->channel) |
	      NWL_DSI_TX_DT(xfer->msg->type) | NWL_DSI_HS_SEL(hs_mode) |
	      NWL_DSI_BTA_TX(xfer->need_bta);
	nwl_dsi_write(dsi, NWL_DSI_PKT_CONTROL, val);

	/* Send packet command */
	nwl_dsi_write(dsi, NWL_DSI_SEND_PACKET, 0x1);
}

static ssize_t nwl_dsi_host_transfer(struct mipi_dsi_host *dsi_host,
				     const struct mipi_dsi_msg *msg)
{
	struct nwl_dsi *dsi = container_of(dsi_host, struct nwl_dsi, dsi_host);
	struct nwl_dsi_transfer xfer;
	ssize_t ret = 0;

	/* Create packet to be sent */
	dsi->xfer = &xfer;
	ret = mipi_dsi_create_packet(&xfer.packet, msg);
	if (ret < 0) {
		dsi->xfer = NULL;
		return ret;
	}

	if ((msg->type & MIPI_DSI_GENERIC_READ_REQUEST_0_PARAM ||
	     msg->type & MIPI_DSI_GENERIC_READ_REQUEST_1_PARAM ||
	     msg->type & MIPI_DSI_GENERIC_READ_REQUEST_2_PARAM ||
	     msg->type & MIPI_DSI_DCS_READ) &&
	    msg->rx_len > 0 && msg->rx_buf)
		xfer.direction = DSI_PACKET_RECEIVE;
	else
		xfer.direction = DSI_PACKET_SEND;

	xfer.need_bta = (xfer.direction == DSI_PACKET_RECEIVE);
	xfer.need_bta |= (msg->flags & MIPI_DSI_MSG_REQ_ACK) ? 1 : 0;
	xfer.msg = msg;
	xfer.status = -ETIMEDOUT;
	xfer.rx_word_count = 0;
	xfer.rx_len = 0;
	xfer.cmd = 0x00;
	if (msg->tx_len > 0)
		xfer.cmd = ((u8 *)(msg->tx_buf))[0];
	init_completion(&xfer.completed);

	ret = clk_prepare_enable(dsi->rx_esc_clk);
	if (ret < 0) {
		DRM_DEV_ERROR(dsi->dev, "Failed to enable rx_esc clk: %zd\n",
			      ret);
		return ret;
	}
	DRM_DEV_DEBUG_DRIVER(dsi->dev, "Enabled rx_esc clk @%lu Hz\n",
			     clk_get_rate(dsi->rx_esc_clk));

	/* Initiate the DSI packet transmision */
	nwl_dsi_begin_transmission(dsi);

	if (!wait_for_completion_timeout(&xfer.completed,
					 NWL_DSI_MIPI_FIFO_TIMEOUT)) {
		DRM_DEV_ERROR(dsi_host->dev, "[%02X] DSI transfer timed out\n",
			      xfer.cmd);
		ret = -ETIMEDOUT;
	} else {
		ret = xfer.status;
	}

	clk_disable_unprepare(dsi->rx_esc_clk);

	return ret;
}

static const struct mipi_dsi_host_ops nwl_dsi_host_ops = {
	.attach = nwl_dsi_host_attach,
	.transfer = nwl_dsi_host_transfer,
};

static irqreturn_t nwl_dsi_irq_handler(int irq, void *data)
{
	u32 irq_status;
	struct nwl_dsi *dsi = data;

	irq_status = nwl_dsi_read(dsi, NWL_DSI_IRQ_STATUS);

	if (irq_status & NWL_DSI_TX_FIFO_OVFLW)
		DRM_DEV_ERROR_RATELIMITED(dsi->dev, "tx fifo overflow\n");

	if (irq_status & NWL_DSI_HS_TX_TIMEOUT)
		DRM_DEV_ERROR_RATELIMITED(dsi->dev, "HS tx timeout\n");

	if (irq_status & NWL_DSI_TX_PKT_DONE ||
	    irq_status & NWL_DSI_RX_PKT_HDR_RCVD ||
	    irq_status & NWL_DSI_RX_PKT_PAYLOAD_DATA_RCVD)
		nwl_dsi_finish_transmission(dsi, irq_status);

	return IRQ_HANDLED;
}

static int nwl_dsi_mode_set(struct nwl_dsi *dsi)
{
	struct device *dev = dsi->dev;
	union phy_configure_opts *phy_cfg = &dsi->phy_cfg;
	int ret;

	if (!dsi->lanes) {
		DRM_DEV_ERROR(dev, "Need DSI lanes: %d\n", dsi->lanes);
		return -EINVAL;
	}

	ret = phy_init(dsi->phy);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "Failed to init DSI phy: %d\n", ret);
		return ret;
	}

	ret = phy_set_mode(dsi->phy, PHY_MODE_MIPI_DPHY);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "Failed to set DSI phy mode: %d\n", ret);
		goto uninit_phy;
	}

	ret = phy_configure(dsi->phy, phy_cfg);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "Failed to configure DSI phy: %d\n", ret);
		goto uninit_phy;
	}

	ret = clk_prepare_enable(dsi->tx_esc_clk);
	if (ret < 0) {
		DRM_DEV_ERROR(dsi->dev, "Failed to enable tx_esc clk: %d\n",
			      ret);
		goto uninit_phy;
	}
	DRM_DEV_DEBUG_DRIVER(dsi->dev, "Enabled tx_esc clk @%lu Hz\n",
			     clk_get_rate(dsi->tx_esc_clk));

	ret = nwl_dsi_config_host(dsi);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "Failed to set up DSI: %d", ret);
		goto disable_clock;
	}

	ret = nwl_dsi_config_dpi(dsi);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "Failed to set up DPI: %d", ret);
		goto disable_clock;
	}

	ret = phy_power_on(dsi->phy);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "Failed to power on DPHY (%d)\n", ret);
		goto disable_clock;
	}

	ret = nwl_dsi_init_interrupts(dsi);
	if (ret < 0)
		goto power_off_phy;

	return ret;

power_off_phy:
	phy_power_off(dsi->phy);
disable_clock:
	clk_disable_unprepare(dsi->tx_esc_clk);
uninit_phy:
	phy_exit(dsi->phy);

	return ret;
}

static int nwl_dsi_disable(struct nwl_dsi *dsi)
{
	struct device *dev = dsi->dev;

	DRM_DEV_DEBUG_DRIVER(dev, "Disabling clocks and phy\n");

	phy_power_off(dsi->phy);
	phy_exit(dsi->phy);

	/* Disabling the clock before the phy breaks enabling dsi again */
	clk_disable_unprepare(dsi->tx_esc_clk);

	return 0;
}

static void
nwl_dsi_bridge_atomic_disable(struct drm_bridge *bridge,
			      struct drm_bridge_state *old_bridge_state)
{
	struct nwl_dsi *dsi = bridge_to_dsi(bridge);
	int ret;

	nwl_dsi_disable(dsi);

	ret = reset_control_assert(dsi->rst_dpi);
	if (ret < 0) {
		DRM_DEV_ERROR(dsi->dev, "Failed to assert DPI: %d\n", ret);
		return;
	}
	ret = reset_control_assert(dsi->rst_byte);
	if (ret < 0) {
		DRM_DEV_ERROR(dsi->dev, "Failed to assert ESC: %d\n", ret);
		return;
	}
	ret = reset_control_assert(dsi->rst_esc);
	if (ret < 0) {
		DRM_DEV_ERROR(dsi->dev, "Failed to assert BYTE: %d\n", ret);
		return;
	}
	ret = reset_control_assert(dsi->rst_pclk);
	if (ret < 0) {
		DRM_DEV_ERROR(dsi->dev, "Failed to assert PCLK: %d\n", ret);
		return;
	}

	clk_disable_unprepare(dsi->core_clk);
	clk_disable_unprepare(dsi->lcdif_clk);

	pm_runtime_put(dsi->dev);
}

static int nwl_dsi_get_dphy_params(struct nwl_dsi *dsi,
				   const struct drm_display_mode *mode,
				   union phy_configure_opts *phy_opts)
{
	unsigned long rate;
	int ret;

	if (dsi->lanes < 1 || dsi->lanes > 4)
		return -EINVAL;

	/*
	 * So far the DPHY spec minimal timings work for both mixel
	 * dphy and nwl dsi host
	 */
	ret = phy_mipi_dphy_get_default_config(mode->clock * 1000,
		mipi_dsi_pixel_format_to_bpp(dsi->format), dsi->lanes,
		&phy_opts->mipi_dphy);
	if (ret < 0)
		return ret;

	rate = clk_get_rate(dsi->tx_esc_clk);
	DRM_DEV_DEBUG_DRIVER(dsi->dev, "LP clk is @%lu Hz\n", rate);
	phy_opts->mipi_dphy.lp_clk_rate = rate;

	return 0;
}

static enum drm_mode_status
nwl_dsi_bridge_mode_valid(struct drm_bridge *bridge,
			  const struct drm_display_info *info,
			  const struct drm_display_mode *mode)
{
	struct nwl_dsi *dsi = bridge_to_dsi(bridge);
	int bpp = mipi_dsi_pixel_format_to_bpp(dsi->format);

	if (mode->clock * bpp > 15000000 * dsi->lanes)
		return MODE_CLOCK_HIGH;

	if (mode->clock * bpp < 80000 * dsi->lanes)
		return MODE_CLOCK_LOW;

	return MODE_OK;
}

static int nwl_dsi_bridge_atomic_check(struct drm_bridge *bridge,
				       struct drm_bridge_state *bridge_state,
				       struct drm_crtc_state *crtc_state,
				       struct drm_connector_state *conn_state)
{
	struct drm_display_mode *adjusted_mode = &crtc_state->adjusted_mode;

	/* At least LCDIF + NWL needs active high sync */
	adjusted_mode->flags |= (DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC);
	adjusted_mode->flags &= ~(DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC);

	/*
	 * Do a full modeset if crtc_state->active is changed to be true.
	 * This ensures our ->mode_set() is called to get the DSI controller
	 * and the PHY ready to send DCS commands, when only the connector's
	 * DPMS is brought out of "Off" status.
	 */
	if (crtc_state->active_changed && crtc_state->active)
		crtc_state->mode_changed = true;

	return 0;
}

static void
nwl_dsi_bridge_mode_set(struct drm_bridge *bridge,
			const struct drm_display_mode *mode,
			const struct drm_display_mode *adjusted_mode)
{
	struct nwl_dsi *dsi = bridge_to_dsi(bridge);
	struct device *dev = dsi->dev;
	union phy_configure_opts new_cfg;
	unsigned long phy_ref_rate;
	int ret;

	ret = nwl_dsi_get_dphy_params(dsi, adjusted_mode, &new_cfg);
	if (ret < 0)
		return;

	phy_ref_rate = clk_get_rate(dsi->phy_ref_clk);
	DRM_DEV_DEBUG_DRIVER(dev, "PHY at ref rate: %lu\n", phy_ref_rate);
	/* Save the new desired phy config */
	memcpy(&dsi->phy_cfg, &new_cfg, sizeof(new_cfg));

	drm_mode_copy(&dsi->mode, adjusted_mode);
	drm_mode_debug_printmodeline(adjusted_mode);

	if (pm_runtime_resume_and_get(dev) < 0)
		return;

	if (clk_prepare_enable(dsi->lcdif_clk) < 0)
		goto runtime_put;
	if (clk_prepare_enable(dsi->core_clk) < 0)
		goto runtime_put;

	/* Step 1 from DSI reset-out instructions */
	ret = reset_control_deassert(dsi->rst_pclk);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "Failed to deassert PCLK: %d\n", ret);
		goto runtime_put;
	}

	/* Step 2 from DSI reset-out instructions */
	nwl_dsi_mode_set(dsi);

	/* Step 3 from DSI reset-out instructions */
	ret = reset_control_deassert(dsi->rst_esc);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "Failed to deassert ESC: %d\n", ret);
		goto runtime_put;
	}
	ret = reset_control_deassert(dsi->rst_byte);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "Failed to deassert BYTE: %d\n", ret);
		goto runtime_put;
	}

	return;

runtime_put:
	pm_runtime_put_sync(dev);
}

static void
nwl_dsi_bridge_atomic_enable(struct drm_bridge *bridge,
			     struct drm_bridge_state *old_bridge_state)
{
	struct nwl_dsi *dsi = bridge_to_dsi(bridge);
	int ret;

	/* Step 5 from DSI reset-out instructions */
	ret = reset_control_deassert(dsi->rst_dpi);
	if (ret < 0)
		DRM_DEV_ERROR(dsi->dev, "Failed to deassert DPI: %d\n", ret);
}

static int nwl_dsi_bridge_attach(struct drm_bridge *bridge,
				 enum drm_bridge_attach_flags flags)
{
	struct nwl_dsi *dsi = bridge_to_dsi(bridge);
	struct drm_bridge *panel_bridge;

	panel_bridge = devm_drm_of_get_bridge(dsi->dev, dsi->dev->of_node, 1, 0);
	if (IS_ERR(panel_bridge))
		return PTR_ERR(panel_bridge);

	return drm_bridge_attach(bridge->encoder, panel_bridge, bridge, flags);
}

static u32 *nwl_bridge_atomic_get_input_bus_fmts(struct drm_bridge *bridge,
						 struct drm_bridge_state *bridge_state,
						 struct drm_crtc_state *crtc_state,
						 struct drm_connector_state *conn_state,
						 u32 output_fmt,
						 unsigned int *num_input_fmts)
{
	u32 *input_fmts, input_fmt;

	*num_input_fmts = 0;

	switch (output_fmt) {
	/* If MEDIA_BUS_FMT_FIXED is tested, return default bus format */
	case MEDIA_BUS_FMT_FIXED:
		input_fmt = MEDIA_BUS_FMT_RGB888_1X24;
		break;
	case MEDIA_BUS_FMT_RGB888_1X24:
	case MEDIA_BUS_FMT_RGB666_1X18:
	case MEDIA_BUS_FMT_RGB565_1X16:
		input_fmt = output_fmt;
		break;
	default:
		return NULL;
	}

	input_fmts = kcalloc(1, sizeof(*input_fmts), GFP_KERNEL);
	if (!input_fmts)
		return NULL;
	input_fmts[0] = input_fmt;
	*num_input_fmts = 1;

	return input_fmts;
}

static const struct drm_bridge_funcs nwl_dsi_bridge_funcs = {
	.atomic_duplicate_state	= drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_bridge_destroy_state,
	.atomic_reset		= drm_atomic_helper_bridge_reset,
	.atomic_check		= nwl_dsi_bridge_atomic_check,
	.atomic_enable		= nwl_dsi_bridge_atomic_enable,
	.atomic_disable		= nwl_dsi_bridge_atomic_disable,
	.atomic_get_input_bus_fmts = nwl_bridge_atomic_get_input_bus_fmts,
	.mode_set		= nwl_dsi_bridge_mode_set,
	.mode_valid		= nwl_dsi_bridge_mode_valid,
	.attach			= nwl_dsi_bridge_attach,
};

static int nwl_dsi_parse_dt(struct nwl_dsi *dsi)
{
	struct platform_device *pdev = to_platform_device(dsi->dev);
	struct clk *clk;
	void __iomem *base;
	int ret;

	dsi->phy = devm_phy_get(dsi->dev, "dphy");
	if (IS_ERR(dsi->phy)) {
		ret = PTR_ERR(dsi->phy);
		if (ret != -EPROBE_DEFER)
			DRM_DEV_ERROR(dsi->dev, "Could not get PHY: %d\n", ret);
		return ret;
	}

	clk = devm_clk_get(dsi->dev, "lcdif");
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		DRM_DEV_ERROR(dsi->dev, "Failed to get lcdif clock: %d\n",
			      ret);
		return ret;
	}
	dsi->lcdif_clk = clk;

	clk = devm_clk_get(dsi->dev, "core");
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		DRM_DEV_ERROR(dsi->dev, "Failed to get core clock: %d\n",
			      ret);
		return ret;
	}
	dsi->core_clk = clk;

	clk = devm_clk_get(dsi->dev, "phy_ref");
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		DRM_DEV_ERROR(dsi->dev, "Failed to get phy_ref clock: %d\n",
			      ret);
		return ret;
	}
	dsi->phy_ref_clk = clk;

	clk = devm_clk_get(dsi->dev, "rx_esc");
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		DRM_DEV_ERROR(dsi->dev, "Failed to get rx_esc clock: %d\n",
			      ret);
		return ret;
	}
	dsi->rx_esc_clk = clk;

	clk = devm_clk_get(dsi->dev, "tx_esc");
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		DRM_DEV_ERROR(dsi->dev, "Failed to get tx_esc clock: %d\n",
			      ret);
		return ret;
	}
	dsi->tx_esc_clk = clk;

	dsi->mux = devm_mux_control_get(dsi->dev, NULL);
	if (IS_ERR(dsi->mux)) {
		ret = PTR_ERR(dsi->mux);
		if (ret != -EPROBE_DEFER)
			DRM_DEV_ERROR(dsi->dev, "Failed to get mux: %d\n", ret);
		return ret;
	}

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	dsi->regmap =
		devm_regmap_init_mmio(dsi->dev, base, &nwl_dsi_regmap_config);
	if (IS_ERR(dsi->regmap)) {
		ret = PTR_ERR(dsi->regmap);
		DRM_DEV_ERROR(dsi->dev, "Failed to create NWL DSI regmap: %d\n",
			      ret);
		return ret;
	}

	dsi->irq = platform_get_irq(pdev, 0);
	if (dsi->irq < 0) {
		DRM_DEV_ERROR(dsi->dev, "Failed to get device IRQ: %d\n",
			      dsi->irq);
		return dsi->irq;
	}

	dsi->rst_pclk = devm_reset_control_get_exclusive(dsi->dev, "pclk");
	if (IS_ERR(dsi->rst_pclk)) {
		DRM_DEV_ERROR(dsi->dev, "Failed to get pclk reset: %ld\n",
			      PTR_ERR(dsi->rst_pclk));
		return PTR_ERR(dsi->rst_pclk);
	}
	dsi->rst_byte = devm_reset_control_get_exclusive(dsi->dev, "byte");
	if (IS_ERR(dsi->rst_byte)) {
		DRM_DEV_ERROR(dsi->dev, "Failed to get byte reset: %ld\n",
			      PTR_ERR(dsi->rst_byte));
		return PTR_ERR(dsi->rst_byte);
	}
	dsi->rst_esc = devm_reset_control_get_exclusive(dsi->dev, "esc");
	if (IS_ERR(dsi->rst_esc)) {
		DRM_DEV_ERROR(dsi->dev, "Failed to get esc reset: %ld\n",
			      PTR_ERR(dsi->rst_esc));
		return PTR_ERR(dsi->rst_esc);
	}
	dsi->rst_dpi = devm_reset_control_get_exclusive(dsi->dev, "dpi");
	if (IS_ERR(dsi->rst_dpi)) {
		DRM_DEV_ERROR(dsi->dev, "Failed to get dpi reset: %ld\n",
			      PTR_ERR(dsi->rst_dpi));
		return PTR_ERR(dsi->rst_dpi);
	}
	return 0;
}

static int nwl_dsi_select_input(struct nwl_dsi *dsi)
{
	struct device_node *remote;
	u32 use_dcss = 1;
	int ret;

	remote = of_graph_get_remote_node(dsi->dev->of_node, 0,
					  NWL_DSI_ENDPOINT_LCDIF);
	if (remote) {
		use_dcss = 0;
	} else {
		remote = of_graph_get_remote_node(dsi->dev->of_node, 0,
						  NWL_DSI_ENDPOINT_DCSS);
		if (!remote) {
			DRM_DEV_ERROR(dsi->dev,
				      "No valid input endpoint found\n");
			return -EINVAL;
		}
	}

	DRM_DEV_INFO(dsi->dev, "Using %s as input source\n",
		     (use_dcss) ? "DCSS" : "LCDIF");
	ret = mux_control_try_select(dsi->mux, use_dcss);
	if (ret < 0)
		DRM_DEV_ERROR(dsi->dev, "Failed to select input: %d\n", ret);

	of_node_put(remote);
	return ret;
}

static int nwl_dsi_deselect_input(struct nwl_dsi *dsi)
{
	int ret;

	ret = mux_control_deselect(dsi->mux);
	if (ret < 0)
		DRM_DEV_ERROR(dsi->dev, "Failed to deselect input: %d\n", ret);

	return ret;
}

static const struct drm_bridge_timings nwl_dsi_timings = {
	.input_bus_flags = DRM_BUS_FLAG_DE_LOW,
};

static const struct of_device_id nwl_dsi_dt_ids[] = {
	{ .compatible = "fsl,imx8mq-nwl-dsi", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, nwl_dsi_dt_ids);

static const struct soc_device_attribute nwl_dsi_quirks_match[] = {
	{ .soc_id = "i.MX8MQ", .revision = "2.0",
	  .data = (void *)E11418_HS_MODE_QUIRK },
	{ /* sentinel. */ }
};

static int nwl_dsi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct soc_device_attribute *attr;
	struct nwl_dsi *dsi;
	int ret;

	dsi = devm_kzalloc(dev, sizeof(*dsi), GFP_KERNEL);
	if (!dsi)
		return -ENOMEM;

	dsi->dev = dev;

	ret = nwl_dsi_parse_dt(dsi);
	if (ret)
		return ret;

	ret = devm_request_irq(dev, dsi->irq, nwl_dsi_irq_handler, 0,
			       dev_name(dev), dsi);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "Failed to request IRQ %d: %d\n", dsi->irq,
			      ret);
		return ret;
	}

	dsi->dsi_host.ops = &nwl_dsi_host_ops;
	dsi->dsi_host.dev = dev;
	ret = mipi_dsi_host_register(&dsi->dsi_host);
	if (ret) {
		DRM_DEV_ERROR(dev, "Failed to register MIPI host: %d\n", ret);
		return ret;
	}

	attr = soc_device_match(nwl_dsi_quirks_match);
	if (attr)
		dsi->quirks = (uintptr_t)attr->data;

	dsi->bridge.driver_private = dsi;
	dsi->bridge.funcs = &nwl_dsi_bridge_funcs;
	dsi->bridge.of_node = dev->of_node;
	dsi->bridge.timings = &nwl_dsi_timings;

	dev_set_drvdata(dev, dsi);
	pm_runtime_enable(dev);

	ret = nwl_dsi_select_input(dsi);
	if (ret < 0) {
		pm_runtime_disable(dev);
		mipi_dsi_host_unregister(&dsi->dsi_host);
		return ret;
	}

	drm_bridge_add(&dsi->bridge);
	return 0;
}

static void nwl_dsi_remove(struct platform_device *pdev)
{
	struct nwl_dsi *dsi = platform_get_drvdata(pdev);

	nwl_dsi_deselect_input(dsi);
	mipi_dsi_host_unregister(&dsi->dsi_host);
	drm_bridge_remove(&dsi->bridge);
	pm_runtime_disable(&pdev->dev);
}

static struct platform_driver nwl_dsi_driver = {
	.probe		= nwl_dsi_probe,
	.remove_new	= nwl_dsi_remove,
	.driver		= {
		.of_match_table = nwl_dsi_dt_ids,
		.name	= DRV_NAME,
	},
};

module_platform_driver(nwl_dsi_driver);

MODULE_AUTHOR("NXP Semiconductor");
MODULE_AUTHOR("Purism SPC");
MODULE_DESCRIPTION("Northwest Logic MIPI-DSI driver");
MODULE_LICENSE("GPL"); /* GPLv2 or later */
