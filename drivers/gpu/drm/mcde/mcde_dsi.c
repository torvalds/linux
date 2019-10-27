// SPDX-License-Identifier: GPL-2.0+
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <video/mipi_display.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_encoder.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

#include "mcde_drm.h"
#include "mcde_dsi_regs.h"

#define DSI_DEFAULT_LP_FREQ_HZ	19200000
#define DSI_DEFAULT_HS_FREQ_HZ	420160000

/* PRCMU DSI reset registers */
#define PRCM_DSI_SW_RESET 0x324
#define PRCM_DSI_SW_RESET_DSI0_SW_RESETN BIT(0)
#define PRCM_DSI_SW_RESET_DSI1_SW_RESETN BIT(1)
#define PRCM_DSI_SW_RESET_DSI2_SW_RESETN BIT(2)

struct mcde_dsi {
	struct device *dev;
	struct mcde *mcde;
	struct drm_bridge bridge;
	struct drm_connector connector;
	struct drm_panel *panel;
	struct drm_bridge *bridge_out;
	struct mipi_dsi_host dsi_host;
	struct mipi_dsi_device *mdsi;
	struct clk *hs_clk;
	struct clk *lp_clk;
	unsigned long hs_freq;
	unsigned long lp_freq;
	bool unused;

	void __iomem *regs;
	struct regmap *prcmu;
};

static inline struct mcde_dsi *bridge_to_mcde_dsi(struct drm_bridge *bridge)
{
	return container_of(bridge, struct mcde_dsi, bridge);
}

static inline struct mcde_dsi *host_to_mcde_dsi(struct mipi_dsi_host *h)
{
	return container_of(h, struct mcde_dsi, dsi_host);
}

static inline struct mcde_dsi *connector_to_mcde_dsi(struct drm_connector *c)
{
	return container_of(c, struct mcde_dsi, connector);
}

bool mcde_dsi_irq(struct mipi_dsi_device *mdsi)
{
	struct mcde_dsi *d;
	u32 val;
	bool te_received = false;

	d = host_to_mcde_dsi(mdsi->host);

	dev_dbg(d->dev, "%s called\n", __func__);

	val = readl(d->regs + DSI_DIRECT_CMD_STS_FLAG);
	if (val)
		dev_dbg(d->dev, "DSI_DIRECT_CMD_STS_FLAG = %08x\n", val);
	if (val & DSI_DIRECT_CMD_STS_WRITE_COMPLETED)
		dev_dbg(d->dev, "direct command write completed\n");
	if (val & DSI_DIRECT_CMD_STS_TE_RECEIVED) {
		te_received = true;
		dev_dbg(d->dev, "direct command TE received\n");
	}
	if (val & DSI_DIRECT_CMD_STS_ACKNOWLEDGE_WITH_ERR_RECEIVED)
		dev_err(d->dev, "direct command ACK ERR received\n");
	if (val & DSI_DIRECT_CMD_STS_READ_COMPLETED_WITH_ERR)
		dev_err(d->dev, "direct command read ERR received\n");
	/* Mask off the ACK value and clear status */
	writel(val, d->regs + DSI_DIRECT_CMD_STS_CLR);

	val = readl(d->regs + DSI_CMD_MODE_STS_FLAG);
	if (val)
		dev_dbg(d->dev, "DSI_CMD_MODE_STS_FLAG = %08x\n", val);
	if (val & DSI_CMD_MODE_STS_ERR_NO_TE)
		/* This happens all the time (safe to ignore) */
		dev_dbg(d->dev, "CMD mode no TE\n");
	if (val & DSI_CMD_MODE_STS_ERR_TE_MISS)
		/* This happens all the time (safe to ignore) */
		dev_dbg(d->dev, "CMD mode TE miss\n");
	if (val & DSI_CMD_MODE_STS_ERR_SDI1_UNDERRUN)
		dev_err(d->dev, "CMD mode SD1 underrun\n");
	if (val & DSI_CMD_MODE_STS_ERR_SDI2_UNDERRUN)
		dev_err(d->dev, "CMD mode SD2 underrun\n");
	if (val & DSI_CMD_MODE_STS_ERR_UNWANTED_RD)
		dev_err(d->dev, "CMD mode unwanted RD\n");
	writel(val, d->regs + DSI_CMD_MODE_STS_CLR);

	val = readl(d->regs + DSI_DIRECT_CMD_RD_STS_FLAG);
	if (val)
		dev_dbg(d->dev, "DSI_DIRECT_CMD_RD_STS_FLAG = %08x\n", val);
	writel(val, d->regs + DSI_DIRECT_CMD_RD_STS_CLR);

	val = readl(d->regs + DSI_TG_STS_FLAG);
	if (val)
		dev_dbg(d->dev, "DSI_TG_STS_FLAG = %08x\n", val);
	writel(val, d->regs + DSI_TG_STS_CLR);

	val = readl(d->regs + DSI_VID_MODE_STS_FLAG);
	if (val)
		dev_err(d->dev, "some video mode error status\n");
	writel(val, d->regs + DSI_VID_MODE_STS_CLR);

	return te_received;
}

static int mcde_dsi_host_attach(struct mipi_dsi_host *host,
				struct mipi_dsi_device *mdsi)
{
	struct mcde_dsi *d = host_to_mcde_dsi(host);

	if (mdsi->lanes < 1 || mdsi->lanes > 2) {
		DRM_ERROR("dsi device params invalid, 1 or 2 lanes supported\n");
		return -EINVAL;
	}

	dev_info(d->dev, "attached DSI device with %d lanes\n", mdsi->lanes);
	/* MIPI_DSI_FMT_RGB88 etc */
	dev_info(d->dev, "format %08x, %dbpp\n", mdsi->format,
		 mipi_dsi_pixel_format_to_bpp(mdsi->format));
	dev_info(d->dev, "mode flags: %08lx\n", mdsi->mode_flags);

	d->mdsi = mdsi;
	if (d->mcde)
		d->mcde->mdsi = mdsi;

	return 0;
}

static int mcde_dsi_host_detach(struct mipi_dsi_host *host,
				struct mipi_dsi_device *mdsi)
{
	struct mcde_dsi *d = host_to_mcde_dsi(host);

	d->mdsi = NULL;
	if (d->mcde)
		d->mcde->mdsi = NULL;

	return 0;
}

#define MCDE_DSI_HOST_IS_READ(type)			    \
	((type == MIPI_DSI_GENERIC_READ_REQUEST_0_PARAM) || \
	 (type == MIPI_DSI_GENERIC_READ_REQUEST_1_PARAM) || \
	 (type == MIPI_DSI_GENERIC_READ_REQUEST_2_PARAM) || \
	 (type == MIPI_DSI_DCS_READ))

static ssize_t mcde_dsi_host_transfer(struct mipi_dsi_host *host,
				      const struct mipi_dsi_msg *msg)
{
	struct mcde_dsi *d = host_to_mcde_dsi(host);
	const u32 loop_delay_us = 10; /* us */
	const u8 *tx = msg->tx_buf;
	u32 loop_counter;
	size_t txlen = msg->tx_len;
	size_t rxlen = msg->rx_len;
	u32 val;
	int ret;
	int i;

	if (txlen > 16) {
		dev_err(d->dev,
			"dunno how to write more than 16 bytes yet\n");
		return -EIO;
	}
	if (rxlen > 4) {
		dev_err(d->dev,
			"dunno how to read more than 4 bytes yet\n");
		return -EIO;
	}

	dev_dbg(d->dev,
		"message to channel %d, write %zd bytes read %zd bytes\n",
		msg->channel, txlen, rxlen);

	/* Command "nature" */
	if (MCDE_DSI_HOST_IS_READ(msg->type))
		/* MCTL_MAIN_DATA_CTL already set up */
		val = DSI_DIRECT_CMD_MAIN_SETTINGS_CMD_NAT_READ;
	else
		val = DSI_DIRECT_CMD_MAIN_SETTINGS_CMD_NAT_WRITE;
	/*
	 * More than 2 bytes will not fit in a single packet, so it's
	 * time to set the "long not short" bit. One byte is used by
	 * the MIPI DCS command leaving just one byte for the payload
	 * in a short package.
	 */
	if (mipi_dsi_packet_format_is_long(msg->type))
		val |= DSI_DIRECT_CMD_MAIN_SETTINGS_CMD_LONGNOTSHORT;
	val |= 0 << DSI_DIRECT_CMD_MAIN_SETTINGS_CMD_ID_SHIFT;
	val |= txlen << DSI_DIRECT_CMD_MAIN_SETTINGS_CMD_SIZE_SHIFT;
	val |= DSI_DIRECT_CMD_MAIN_SETTINGS_CMD_LP_EN;
	val |= msg->type << DSI_DIRECT_CMD_MAIN_SETTINGS_CMD_HEAD_SHIFT;
	writel(val, d->regs + DSI_DIRECT_CMD_MAIN_SETTINGS);

	/* MIPI DCS command is part of the data */
	if (txlen > 0) {
		val = 0;
		for (i = 0; i < 4 && i < txlen; i++)
			val |= tx[i] << (i & 3) * 8;
	}
	writel(val, d->regs + DSI_DIRECT_CMD_WRDAT0);
	if (txlen > 4) {
		val = 0;
		for (i = 0; i < 4 && (i + 4) < txlen; i++)
			val |= tx[i + 4] << (i & 3) * 8;
		writel(val, d->regs + DSI_DIRECT_CMD_WRDAT1);
	}
	if (txlen > 8) {
		val = 0;
		for (i = 0; i < 4 && (i + 8) < txlen; i++)
			val |= tx[i + 8] << (i & 3) * 8;
		writel(val, d->regs + DSI_DIRECT_CMD_WRDAT2);
	}
	if (txlen > 12) {
		val = 0;
		for (i = 0; i < 4 && (i + 12) < txlen; i++)
			val |= tx[i + 12] << (i & 3) * 8;
		writel(val, d->regs + DSI_DIRECT_CMD_WRDAT3);
	}

	writel(~0, d->regs + DSI_DIRECT_CMD_STS_CLR);
	writel(~0, d->regs + DSI_CMD_MODE_STS_CLR);
	/* Send command */
	writel(1, d->regs + DSI_DIRECT_CMD_SEND);

	loop_counter = 1000 * 1000 / loop_delay_us;
	if (MCDE_DSI_HOST_IS_READ(msg->type)) {
		/* Read command */
		while (!(readl(d->regs + DSI_DIRECT_CMD_STS) &
			 (DSI_DIRECT_CMD_STS_READ_COMPLETED |
			  DSI_DIRECT_CMD_STS_READ_COMPLETED_WITH_ERR))
		       && --loop_counter)
			usleep_range(loop_delay_us, (loop_delay_us * 3) / 2);
		if (!loop_counter) {
			dev_err(d->dev, "DSI read timeout!\n");
			return -ETIME;
		}
	} else {
		/* Writing only */
		while (!(readl(d->regs + DSI_DIRECT_CMD_STS) &
			 DSI_DIRECT_CMD_STS_WRITE_COMPLETED)
		       && --loop_counter)
			usleep_range(loop_delay_us, (loop_delay_us * 3) / 2);

		if (!loop_counter) {
			dev_err(d->dev, "DSI write timeout!\n");
			return -ETIME;
		}
	}

	val = readl(d->regs + DSI_DIRECT_CMD_STS);
	if (val & DSI_DIRECT_CMD_STS_READ_COMPLETED_WITH_ERR) {
		dev_err(d->dev, "read completed with error\n");
		writel(1, d->regs + DSI_DIRECT_CMD_RD_INIT);
		return -EIO;
	}
	if (val & DSI_DIRECT_CMD_STS_ACKNOWLEDGE_WITH_ERR_RECEIVED) {
		val >>= DSI_DIRECT_CMD_STS_ACK_VAL_SHIFT;
		dev_err(d->dev, "error during transmission: %04x\n",
			val);
		return -EIO;
	}

	if (!MCDE_DSI_HOST_IS_READ(msg->type)) {
		/* Return number of bytes written */
		ret = txlen;
	} else {
		/* OK this is a read command, get the response */
		u32 rdsz;
		u32 rddat;
		u8 *rx = msg->rx_buf;

		rdsz = readl(d->regs + DSI_DIRECT_CMD_RD_PROPERTY);
		rdsz &= DSI_DIRECT_CMD_RD_PROPERTY_RD_SIZE_MASK;
		rddat = readl(d->regs + DSI_DIRECT_CMD_RDDAT);
		if (rdsz < rxlen) {
			dev_err(d->dev, "read error, requested %zd got %d\n",
				rxlen, rdsz);
			return -EIO;
		}
		/* FIXME: read more than 4 bytes */
		for (i = 0; i < 4 && i < rxlen; i++)
			rx[i] = (rddat >> (i * 8)) & 0xff;
		ret = rdsz;
	}

	writel(~0, d->regs + DSI_DIRECT_CMD_STS_CLR);
	writel(~0, d->regs + DSI_CMD_MODE_STS_CLR);

	return ret;
}

static const struct mipi_dsi_host_ops mcde_dsi_host_ops = {
	.attach = mcde_dsi_host_attach,
	.detach = mcde_dsi_host_detach,
	.transfer = mcde_dsi_host_transfer,
};

/* This sends a direct (short) command to request TE */
void mcde_dsi_te_request(struct mipi_dsi_device *mdsi)
{
	struct mcde_dsi *d;
	u32 val;

	d = host_to_mcde_dsi(mdsi->host);

	/* Command "nature" TE request */
	val = DSI_DIRECT_CMD_MAIN_SETTINGS_CMD_NAT_TE_REQ;
	val |= 0 << DSI_DIRECT_CMD_MAIN_SETTINGS_CMD_ID_SHIFT;
	val |= 2 << DSI_DIRECT_CMD_MAIN_SETTINGS_CMD_SIZE_SHIFT;
	val |= DSI_DIRECT_CMD_MAIN_SETTINGS_CMD_LP_EN;
	val |= DSI_DIRECT_CMD_MAIN_SETTINGS_CMD_HEAD_DCS_SHORT_WRITE_1 <<
		DSI_DIRECT_CMD_MAIN_SETTINGS_CMD_HEAD_SHIFT;
	writel(val, d->regs + DSI_DIRECT_CMD_MAIN_SETTINGS);

	/* Clear TE reveived and error status bits and enables them */
	writel(DSI_DIRECT_CMD_STS_CLR_TE_RECEIVED_CLR |
	       DSI_DIRECT_CMD_STS_CLR_ACKNOWLEDGE_WITH_ERR_RECEIVED_CLR,
	       d->regs + DSI_DIRECT_CMD_STS_CLR);
	val = readl(d->regs + DSI_DIRECT_CMD_STS_CTL);
	val |= DSI_DIRECT_CMD_STS_CTL_TE_RECEIVED_EN;
	val |= DSI_DIRECT_CMD_STS_CTL_ACKNOWLEDGE_WITH_ERR_EN;
	writel(val, d->regs + DSI_DIRECT_CMD_STS_CTL);

	/* Clear and enable no TE or TE missing status */
	writel(DSI_CMD_MODE_STS_CLR_ERR_NO_TE_CLR |
	       DSI_CMD_MODE_STS_CLR_ERR_TE_MISS_CLR,
	       d->regs + DSI_CMD_MODE_STS_CLR);
	val = readl(d->regs + DSI_CMD_MODE_STS_CTL);
	val |= DSI_CMD_MODE_STS_CTL_ERR_NO_TE_EN;
	val |= DSI_CMD_MODE_STS_CTL_ERR_TE_MISS_EN;
	writel(val, d->regs + DSI_CMD_MODE_STS_CTL);

	/* Send this TE request command */
	writel(1, d->regs + DSI_DIRECT_CMD_SEND);
}

static void mcde_dsi_setup_video_mode(struct mcde_dsi *d,
				      const struct drm_display_mode *mode)
{
	u8 bpp = mipi_dsi_pixel_format_to_bpp(d->mdsi->format);
	u64 bpl;
	u32 hfp;
	u32 hbp;
	u32 hsa;
	u32 blkline_pck, line_duration;
	u32 blkeol_pck, blkeol_duration;
	u32 val;

	val = 0;
	if (d->mdsi->mode_flags & MIPI_DSI_MODE_VIDEO_BURST)
		val |= DSI_VID_MAIN_CTL_BURST_MODE;
	if (d->mdsi->mode_flags & MIPI_DSI_MODE_VIDEO_SYNC_PULSE) {
		val |= DSI_VID_MAIN_CTL_SYNC_PULSE_ACTIVE;
		val |= DSI_VID_MAIN_CTL_SYNC_PULSE_HORIZONTAL;
	}
	/* RGB header and pixel mode */
	switch (d->mdsi->format) {
	case MIPI_DSI_FMT_RGB565:
		val |= MIPI_DSI_PACKED_PIXEL_STREAM_16 <<
			DSI_VID_MAIN_CTL_HEADER_SHIFT;
		val |= DSI_VID_MAIN_CTL_VID_PIXEL_MODE_16BITS;
		break;
	case MIPI_DSI_FMT_RGB666_PACKED:
		val |= MIPI_DSI_PACKED_PIXEL_STREAM_18 <<
			DSI_VID_MAIN_CTL_HEADER_SHIFT;
		val |= DSI_VID_MAIN_CTL_VID_PIXEL_MODE_18BITS;
		break;
	case MIPI_DSI_FMT_RGB666:
		val |= MIPI_DSI_PIXEL_STREAM_3BYTE_18
			<< DSI_VID_MAIN_CTL_HEADER_SHIFT;
		val |= DSI_VID_MAIN_CTL_VID_PIXEL_MODE_18BITS_LOOSE;
		break;
	case MIPI_DSI_FMT_RGB888:
		val |= MIPI_DSI_PACKED_PIXEL_STREAM_24 <<
			DSI_VID_MAIN_CTL_HEADER_SHIFT;
		val |= DSI_VID_MAIN_CTL_VID_PIXEL_MODE_24BITS;
		break;
	default:
		dev_err(d->dev, "unknown pixel mode\n");
		return;
	}

	/* TODO: TVG could be enabled here */

	/* Send blanking packet */
	val |= DSI_VID_MAIN_CTL_REG_BLKLINE_MODE_LP_0;
	/* Send EOL packet */
	val |= DSI_VID_MAIN_CTL_REG_BLKEOL_MODE_LP_0;
	/* Recovery mode 1 */
	val |= 1 << DSI_VID_MAIN_CTL_RECOVERY_MODE_SHIFT;
	/* All other fields zero */
	writel(val, d->regs + DSI_VID_MAIN_CTL);

	/* Vertical frame parameters are pretty straight-forward */
	val = mode->vdisplay << DSI_VID_VSIZE_VSA_LENGTH_SHIFT;
	/* vertical front porch */
	val |= (mode->vsync_start - mode->vdisplay)
		<< DSI_VID_VSIZE_VFP_LENGTH_SHIFT;
	/* vertical sync active */
	val |= (mode->vsync_end - mode->vsync_start)
		<< DSI_VID_VSIZE_VACT_LENGTH_SHIFT;
	/* vertical back porch */
	val |= (mode->vtotal - mode->vsync_end)
		<< DSI_VID_VSIZE_VBP_LENGTH_SHIFT;
	writel(val, d->regs + DSI_VID_VSIZE);

	/*
	 * Horizontal frame parameters:
	 * horizontal resolution is given in pixels and must be re-calculated
	 * into bytes since this is what the hardware expects.
	 *
	 * 6 + 2 is HFP header + checksum
	 */
	hfp = (mode->hsync_start - mode->hdisplay) * bpp - 6 - 2;
	if (d->mdsi->mode_flags & MIPI_DSI_MODE_VIDEO_SYNC_PULSE) {
		/*
		 * 6 is HBP header + checksum
		 * 4 is RGB header + checksum
		 */
		hbp = (mode->htotal - mode->hsync_end) * bpp - 4 - 6;
		/*
		 * 6 is HBP header + checksum
		 * 4 is HSW packet bytes
		 * 4 is RGB header + checksum
		 */
		hsa = (mode->hsync_end - mode->hsync_start) * bpp - 4 - 4 - 6;
	} else {
		/*
		 * HBP includes both back porch and sync
		 * 6 is HBP header + checksum
		 * 4 is HSW packet bytes
		 * 4 is RGB header + checksum
		 */
		hbp = (mode->htotal - mode->hsync_start) * bpp - 4 - 4 - 6;
		/* HSA is not considered in this mode and set to 0 */
		hsa = 0;
	}
	dev_dbg(d->dev, "hfp: %u, hbp: %u, hsa: %u\n",
		hfp, hbp, hsa);

	/* Frame parameters: horizontal sync active */
	val = hsa << DSI_VID_HSIZE1_HSA_LENGTH_SHIFT;
	/* horizontal back porch */
	val |= hbp << DSI_VID_HSIZE1_HBP_LENGTH_SHIFT;
	/* horizontal front porch */
	val |= hfp << DSI_VID_HSIZE1_HFP_LENGTH_SHIFT;
	writel(val, d->regs + DSI_VID_HSIZE1);

	/* RGB data length (bytes on one scanline) */
	val = mode->hdisplay * (bpp / 8);
	writel(val, d->regs + DSI_VID_HSIZE2);

	/* TODO: further adjustments for TVG mode here */

	/*
	 * EOL packet length from bits per line calculations: pixel clock
	 * is given in kHz, calculate the time between two pixels in
	 * picoseconds.
	 */
	bpl = mode->clock * mode->htotal;
	bpl *= (d->hs_freq / 8);
	do_div(bpl, 1000000); /* microseconds */
	do_div(bpl, 1000000); /* seconds */
	bpl *= d->mdsi->lanes;
	dev_dbg(d->dev, "calculated bytes per line: %llu\n", bpl);
	/*
	 * 6 is header + checksum, header = 4 bytes, checksum = 2 bytes
	 * 4 is short packet for vsync/hsync
	 */
	if (d->mdsi->mode_flags & MIPI_DSI_MODE_VIDEO_SYNC_PULSE) {
		/* Fixme: isn't the hsync width in pixels? */
		blkline_pck = bpl - (mode->hsync_end - mode->hsync_start) - 6;
		val = blkline_pck << DSI_VID_BLKSIZE2_BLKLINE_PULSE_PCK_SHIFT;
		writel(val, d->regs + DSI_VID_BLKSIZE2);
	} else {
		blkline_pck = bpl - 4 - 6;
		val = blkline_pck << DSI_VID_BLKSIZE1_BLKLINE_EVENT_PCK_SHIFT;
		writel(val, d->regs + DSI_VID_BLKSIZE1);
	}

	line_duration = (blkline_pck + 6) / d->mdsi->lanes;
	dev_dbg(d->dev, "line duration %u\n", line_duration);
	val = line_duration << DSI_VID_DPHY_TIME_REG_LINE_DURATION_SHIFT;
	/*
	 * This is the time to perform LP->HS on D-PHY
	 * FIXME: nowhere to get this from: DT property on the DSI?
	 */
	val |= 0 << DSI_VID_DPHY_TIME_REG_WAKEUP_TIME_SHIFT;
	writel(val, d->regs + DSI_VID_DPHY_TIME);

	/* Calculate block end of line */
	blkeol_pck = bpl - mode->hdisplay * bpp - 6;
	blkeol_duration = (blkeol_pck + 6) / d->mdsi->lanes;
	dev_dbg(d->dev, "blkeol pck: %u, duration: %u\n",
		 blkeol_pck, blkeol_duration);

	if (d->mdsi->mode_flags & MIPI_DSI_MODE_VIDEO_BURST) {
		/* Set up EOL clock for burst mode */
		val = readl(d->regs + DSI_VID_BLKSIZE1);
		val |= blkeol_pck << DSI_VID_BLKSIZE1_BLKEOL_PCK_SHIFT;
		writel(val, d->regs + DSI_VID_BLKSIZE1);
		writel(blkeol_pck, d->regs + DSI_VID_VCA_SETTING2);

		writel(blkeol_duration, d->regs + DSI_VID_PCK_TIME);
		writel(blkeol_duration - 6, d->regs + DSI_VID_VCA_SETTING1);
	}

	/* Maximum line limit */
	val = readl(d->regs + DSI_VID_VCA_SETTING2);
	val |= blkline_pck <<
		DSI_VID_VCA_SETTING2_EXACT_BURST_LIMIT_SHIFT;
	writel(val, d->regs + DSI_VID_VCA_SETTING2);

	/* Put IF1 into video mode */
	val = readl(d->regs + DSI_MCTL_MAIN_DATA_CTL);
	val |= DSI_MCTL_MAIN_DATA_CTL_IF1_MODE;
	writel(val, d->regs + DSI_MCTL_MAIN_DATA_CTL);

	/* Disable command mode on IF1 */
	val = readl(d->regs + DSI_CMD_MODE_CTL);
	val &= ~DSI_CMD_MODE_CTL_IF1_LP_EN;
	writel(val, d->regs + DSI_CMD_MODE_CTL);

	/* Enable some error interrupts */
	val = readl(d->regs + DSI_VID_MODE_STS_CTL);
	val |= DSI_VID_MODE_STS_CTL_ERR_MISSING_VSYNC;
	val |= DSI_VID_MODE_STS_CTL_ERR_MISSING_DATA;
	writel(val, d->regs + DSI_VID_MODE_STS_CTL);

	/* Enable video mode */
	val = readl(d->regs + DSI_MCTL_MAIN_DATA_CTL);
	val |= DSI_MCTL_MAIN_DATA_CTL_VID_EN;
	writel(val, d->regs + DSI_MCTL_MAIN_DATA_CTL);
}

static void mcde_dsi_start(struct mcde_dsi *d)
{
	unsigned long hs_freq;
	u32 val;
	int i;

	/* No integration mode */
	writel(0, d->regs + DSI_MCTL_INTEGRATION_MODE);

	/* Enable the DSI port, from drivers/video/mcde/dsilink_v2.c */
	val = DSI_MCTL_MAIN_DATA_CTL_LINK_EN |
		DSI_MCTL_MAIN_DATA_CTL_BTA_EN |
		DSI_MCTL_MAIN_DATA_CTL_READ_EN |
		DSI_MCTL_MAIN_DATA_CTL_REG_TE_EN;
	if (d->mdsi->mode_flags & MIPI_DSI_MODE_EOT_PACKET)
		val |= DSI_MCTL_MAIN_DATA_CTL_HOST_EOT_GEN;
	writel(val, d->regs + DSI_MCTL_MAIN_DATA_CTL);

	/* Set a high command timeout, clear other fields */
	val = 0x3ff << DSI_CMD_MODE_CTL_TE_TIMEOUT_SHIFT;
	writel(val, d->regs + DSI_CMD_MODE_CTL);

	/*
	 * UI_X4 is described as "unit interval times four"
	 * I guess since DSI packets are 4 bytes wide, one unit
	 * is one byte.
	 */
	hs_freq = clk_get_rate(d->hs_clk);
	hs_freq /= 1000000; /* MHz */
	val = 4000 / hs_freq;
	dev_dbg(d->dev, "UI value: %d\n", val);
	val <<= DSI_MCTL_DPHY_STATIC_UI_X4_SHIFT;
	val &= DSI_MCTL_DPHY_STATIC_UI_X4_MASK;
	writel(val, d->regs + DSI_MCTL_DPHY_STATIC);

	/*
	 * Enable clocking: 0x0f (something?) between each burst,
	 * enable the second lane if needed, enable continuous clock if
	 * needed, enable switch into ULPM (ultra-low power mode) on
	 * all the lines.
	 */
	val = 0x0f << DSI_MCTL_MAIN_PHY_CTL_WAIT_BURST_TIME_SHIFT;
	if (d->mdsi->lanes == 2)
		val |= DSI_MCTL_MAIN_PHY_CTL_LANE2_EN;
	if (!(d->mdsi->mode_flags & MIPI_DSI_CLOCK_NON_CONTINUOUS))
		val |= DSI_MCTL_MAIN_PHY_CTL_CLK_CONTINUOUS;
	val |= DSI_MCTL_MAIN_PHY_CTL_CLK_ULPM_EN |
		DSI_MCTL_MAIN_PHY_CTL_DAT1_ULPM_EN |
		DSI_MCTL_MAIN_PHY_CTL_DAT2_ULPM_EN;
	writel(val, d->regs + DSI_MCTL_MAIN_PHY_CTL);

	val = (1 << DSI_MCTL_ULPOUT_TIME_CKLANE_ULPOUT_TIME_SHIFT) |
		(1 << DSI_MCTL_ULPOUT_TIME_DATA_ULPOUT_TIME_SHIFT);
	writel(val, d->regs + DSI_MCTL_ULPOUT_TIME);

	writel(DSI_DPHY_LANES_TRIM_DPHY_SPECS_90_81B_0_90,
	       d->regs + DSI_DPHY_LANES_TRIM);

	/* High PHY timeout */
	val = (0x0f << DSI_MCTL_DPHY_TIMEOUT_CLK_DIV_SHIFT) |
		(0x3fff << DSI_MCTL_DPHY_TIMEOUT_HSTX_TO_VAL_SHIFT) |
		(0x3fff << DSI_MCTL_DPHY_TIMEOUT_LPRX_TO_VAL_SHIFT);
	writel(val, d->regs + DSI_MCTL_DPHY_TIMEOUT);

	val = DSI_MCTL_MAIN_EN_PLL_START |
		DSI_MCTL_MAIN_EN_CKLANE_EN |
		DSI_MCTL_MAIN_EN_DAT1_EN |
		DSI_MCTL_MAIN_EN_IF1_EN;
	if (d->mdsi->lanes == 2)
		val |= DSI_MCTL_MAIN_EN_DAT2_EN;
	writel(val, d->regs + DSI_MCTL_MAIN_EN);

	/* Wait for the PLL to lock and the clock and data lines to come up */
	i = 0;
	val = DSI_MCTL_MAIN_STS_PLL_LOCK |
		DSI_MCTL_MAIN_STS_CLKLANE_READY |
		DSI_MCTL_MAIN_STS_DAT1_READY;
	if (d->mdsi->lanes == 2)
		val |= DSI_MCTL_MAIN_STS_DAT2_READY;
	while ((readl(d->regs + DSI_MCTL_MAIN_STS) & val) != val) {
		/* Sleep for a millisecond */
		usleep_range(1000, 1500);
		if (i++ == 100) {
			dev_warn(d->dev, "DSI lanes did not start up\n");
			return;
		}
	}

	/* TODO needed? */

	/* Command mode, clear IF1 ID */
	val = readl(d->regs + DSI_CMD_MODE_CTL);
	/*
	 * If we enable low-power mode here, with
	 * val |= DSI_CMD_MODE_CTL_IF1_LP_EN
	 * then display updates become really slow.
	 */
	val &= ~DSI_CMD_MODE_CTL_IF1_ID_MASK;
	writel(val, d->regs + DSI_CMD_MODE_CTL);

	/* Wait for DSI PHY to initialize */
	usleep_range(100, 200);
	dev_info(d->dev, "DSI link enabled\n");
}


static void mcde_dsi_bridge_enable(struct drm_bridge *bridge)
{
	struct mcde_dsi *d = bridge_to_mcde_dsi(bridge);

	dev_info(d->dev, "enable DSI master\n");
};

static void mcde_dsi_bridge_mode_set(struct drm_bridge *bridge,
				     const struct drm_display_mode *mode,
				     const struct drm_display_mode *adj)
{
	struct mcde_dsi *d = bridge_to_mcde_dsi(bridge);
	unsigned long pixel_clock_hz = mode->clock * 1000;
	unsigned long hs_freq, lp_freq;
	u32 val;
	int ret;

	if (!d->mdsi) {
		dev_err(d->dev, "no DSI device attached to encoder!\n");
		return;
	}

	dev_info(d->dev, "set DSI master to %dx%d %lu Hz %s mode\n",
		 mode->hdisplay, mode->vdisplay, pixel_clock_hz,
		 (d->mdsi->mode_flags & MIPI_DSI_MODE_VIDEO) ? "VIDEO" : "CMD"
		);

	/* Copy maximum clock frequencies */
	if (d->mdsi->lp_rate)
		lp_freq = d->mdsi->lp_rate;
	else
		lp_freq = DSI_DEFAULT_LP_FREQ_HZ;
	if (d->mdsi->hs_rate)
		hs_freq = d->mdsi->hs_rate;
	else
		hs_freq = DSI_DEFAULT_HS_FREQ_HZ;

	/* Enable LP (Low Power, Energy Save, ES) and HS (High Speed) clocks */
	d->lp_freq = clk_round_rate(d->lp_clk, lp_freq);
	ret = clk_set_rate(d->lp_clk, d->lp_freq);
	if (ret)
		dev_err(d->dev, "failed to set LP clock rate %lu Hz\n",
			d->lp_freq);

	d->hs_freq = clk_round_rate(d->hs_clk, hs_freq);
	ret = clk_set_rate(d->hs_clk, d->hs_freq);
	if (ret)
		dev_err(d->dev, "failed to set HS clock rate %lu Hz\n",
			d->hs_freq);

	/* Start clocks */
	ret = clk_prepare_enable(d->lp_clk);
	if (ret)
		dev_err(d->dev, "failed to enable LP clock\n");
	else
		dev_info(d->dev, "DSI LP clock rate %lu Hz\n",
			 d->lp_freq);
	ret = clk_prepare_enable(d->hs_clk);
	if (ret)
		dev_err(d->dev, "failed to enable HS clock\n");
	else
		dev_info(d->dev, "DSI HS clock rate %lu Hz\n",
			 d->hs_freq);

	if (d->mdsi->mode_flags & MIPI_DSI_MODE_VIDEO) {
		mcde_dsi_setup_video_mode(d, mode);
	} else {
		/* Command mode, clear IF1 ID */
		val = readl(d->regs + DSI_CMD_MODE_CTL);
		/*
		 * If we enable low-power mode here with
		 * val |= DSI_CMD_MODE_CTL_IF1_LP_EN
		 * the display updates become really slow.
		 */
		val &= ~DSI_CMD_MODE_CTL_IF1_ID_MASK;
		writel(val, d->regs + DSI_CMD_MODE_CTL);
	}
}

static void mcde_dsi_wait_for_command_mode_stop(struct mcde_dsi *d)
{
	u32 val;
	int i;

	/*
	 * Wait until we get out of command mode
	 * CSM = Command State Machine
	 */
	i = 0;
	val = DSI_CMD_MODE_STS_CSM_RUNNING;
	while ((readl(d->regs + DSI_CMD_MODE_STS) & val) == val) {
		/* Sleep for a millisecond */
		usleep_range(1000, 2000);
		if (i++ == 100) {
			dev_warn(d->dev,
				 "could not get out of command mode\n");
			return;
		}
	}
}

static void mcde_dsi_wait_for_video_mode_stop(struct mcde_dsi *d)
{
	u32 val;
	int i;

	/* Wait until we get out og video mode */
	i = 0;
	val = DSI_VID_MODE_STS_VSG_RUNNING;
	while ((readl(d->regs + DSI_VID_MODE_STS) & val) == val) {
		/* Sleep for a millisecond */
		usleep_range(1000, 2000);
		if (i++ == 100) {
			dev_warn(d->dev,
				 "could not get out of video mode\n");
			return;
		}
	}
}

static void mcde_dsi_bridge_disable(struct drm_bridge *bridge)
{
	struct mcde_dsi *d = bridge_to_mcde_dsi(bridge);
	u32 val;

	/* Disable all error interrupts */
	writel(0, d->regs + DSI_VID_MODE_STS_CTL);

	if (d->mdsi->mode_flags & MIPI_DSI_MODE_VIDEO) {
		/* Stop video mode */
		val = readl(d->regs + DSI_MCTL_MAIN_DATA_CTL);
		val &= ~DSI_MCTL_MAIN_DATA_CTL_VID_EN;
		writel(val, d->regs + DSI_MCTL_MAIN_DATA_CTL);
		mcde_dsi_wait_for_video_mode_stop(d);
	} else {
		/* Stop command mode */
		mcde_dsi_wait_for_command_mode_stop(d);
	}

	/* Stop clocks */
	clk_disable_unprepare(d->hs_clk);
	clk_disable_unprepare(d->lp_clk);
}

/*
 * This connector needs no special handling, just use the default
 * helpers for everything. It's pretty dummy.
 */
static const struct drm_connector_funcs mcde_dsi_connector_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int mcde_dsi_get_modes(struct drm_connector *connector)
{
	struct mcde_dsi *d = connector_to_mcde_dsi(connector);

	/* Just pass the question to the panel */
	if (d->panel)
		return drm_panel_get_modes(d->panel);

	/* TODO: deal with bridges */

	return 0;
}

static const struct drm_connector_helper_funcs
mcde_dsi_connector_helper_funcs = {
	.get_modes = mcde_dsi_get_modes,
};

static int mcde_dsi_bridge_attach(struct drm_bridge *bridge)
{
	struct mcde_dsi *d = bridge_to_mcde_dsi(bridge);
	struct drm_device *drm = bridge->dev;
	int ret;

	drm_connector_helper_add(&d->connector,
				 &mcde_dsi_connector_helper_funcs);

	if (!drm_core_check_feature(drm, DRIVER_ATOMIC)) {
		dev_err(d->dev, "we need atomic updates\n");
		return -ENOTSUPP;
	}

	ret = drm_connector_init(drm, &d->connector,
				 &mcde_dsi_connector_funcs,
				 DRM_MODE_CONNECTOR_DSI);
	if (ret) {
		dev_err(d->dev, "failed to initialize DSI bridge connector\n");
		return ret;
	}
	d->connector.polled = DRM_CONNECTOR_POLL_CONNECT;
	/* The encoder in the bridge attached to the DSI bridge */
	drm_connector_attach_encoder(&d->connector, bridge->encoder);
	/* Then we attach the DSI bridge to the output (panel etc) bridge */
	ret = drm_bridge_attach(bridge->encoder, d->bridge_out, bridge);
	if (ret) {
		dev_err(d->dev, "failed to attach the DSI bridge\n");
		return ret;
	}
	d->connector.status = connector_status_connected;

	return 0;
}

static const struct drm_bridge_funcs mcde_dsi_bridge_funcs = {
	.attach = mcde_dsi_bridge_attach,
	.mode_set = mcde_dsi_bridge_mode_set,
	.disable = mcde_dsi_bridge_disable,
	.enable = mcde_dsi_bridge_enable,
};

static int mcde_dsi_bind(struct device *dev, struct device *master,
			 void *data)
{
	struct drm_device *drm = data;
	struct mcde *mcde = drm->dev_private;
	struct mcde_dsi *d = dev_get_drvdata(dev);
	struct device_node *child;
	struct drm_panel *panel = NULL;
	struct drm_bridge *bridge = NULL;

	if (!of_get_available_child_count(dev->of_node)) {
		dev_info(dev, "unused DSI interface\n");
		d->unused = true;
		return 0;
	}
	d->mcde = mcde;
	/* If the display attached before binding, set this up */
	if (d->mdsi)
		d->mcde->mdsi = d->mdsi;

	/* Obtain the clocks */
	d->hs_clk = devm_clk_get(dev, "hs");
	if (IS_ERR(d->hs_clk)) {
		dev_err(dev, "unable to get HS clock\n");
		return PTR_ERR(d->hs_clk);
	}

	d->lp_clk = devm_clk_get(dev, "lp");
	if (IS_ERR(d->lp_clk)) {
		dev_err(dev, "unable to get LP clock\n");
		return PTR_ERR(d->lp_clk);
	}

	/* Assert RESET through the PRCMU, active low */
	/* FIXME: which DSI block? */
	regmap_update_bits(d->prcmu, PRCM_DSI_SW_RESET,
			   PRCM_DSI_SW_RESET_DSI0_SW_RESETN, 0);

	usleep_range(100, 200);

	/* De-assert RESET again */
	regmap_update_bits(d->prcmu, PRCM_DSI_SW_RESET,
			   PRCM_DSI_SW_RESET_DSI0_SW_RESETN,
			   PRCM_DSI_SW_RESET_DSI0_SW_RESETN);

	/* Start up the hardware */
	mcde_dsi_start(d);

	/* Look for a panel as a child to this node */
	for_each_available_child_of_node(dev->of_node, child) {
		panel = of_drm_find_panel(child);
		if (IS_ERR(panel)) {
			dev_err(dev, "failed to find panel try bridge (%lu)\n",
				PTR_ERR(panel));
			bridge = of_drm_find_bridge(child);
			if (IS_ERR(bridge)) {
				dev_err(dev, "failed to find bridge (%lu)\n",
					PTR_ERR(bridge));
				return PTR_ERR(bridge);
			}
		}
	}
	if (panel) {
		bridge = drm_panel_bridge_add(panel,
					      DRM_MODE_CONNECTOR_DSI);
		if (IS_ERR(bridge)) {
			dev_err(dev, "error adding panel bridge\n");
			return PTR_ERR(bridge);
		}
		dev_info(dev, "connected to panel\n");
		d->panel = panel;
	} else if (bridge) {
		/* TODO: AV8100 HDMI encoder goes here for example */
		dev_info(dev, "connected to non-panel bridge (unsupported)\n");
		return -ENODEV;
	} else {
		dev_err(dev, "no panel or bridge\n");
		return -ENODEV;
	}

	d->bridge_out = bridge;

	/* Create a bridge for this DSI channel */
	d->bridge.funcs = &mcde_dsi_bridge_funcs;
	d->bridge.of_node = dev->of_node;
	drm_bridge_add(&d->bridge);

	/* TODO: first come first serve, use a list */
	mcde->bridge = &d->bridge;

	dev_info(dev, "initialized MCDE DSI bridge\n");

	return 0;
}

static void mcde_dsi_unbind(struct device *dev, struct device *master,
			    void *data)
{
	struct mcde_dsi *d = dev_get_drvdata(dev);

	if (d->panel)
		drm_panel_bridge_remove(d->bridge_out);
	regmap_update_bits(d->prcmu, PRCM_DSI_SW_RESET,
			   PRCM_DSI_SW_RESET_DSI0_SW_RESETN, 0);
}

static const struct component_ops mcde_dsi_component_ops = {
	.bind   = mcde_dsi_bind,
	.unbind = mcde_dsi_unbind,
};

static int mcde_dsi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mcde_dsi *d;
	struct mipi_dsi_host *host;
	struct resource *res;
	u32 dsi_id;
	int ret;

	d = devm_kzalloc(dev, sizeof(*d), GFP_KERNEL);
	if (!d)
		return -ENOMEM;
	d->dev = dev;
	platform_set_drvdata(pdev, d);

	/* Get a handle on the PRCMU so we can do reset */
	d->prcmu =
		syscon_regmap_lookup_by_compatible("stericsson,db8500-prcmu");
	if (IS_ERR(d->prcmu)) {
		dev_err(dev, "no PRCMU regmap\n");
		return PTR_ERR(d->prcmu);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	d->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(d->regs)) {
		dev_err(dev, "no DSI regs\n");
		return PTR_ERR(d->regs);
	}

	dsi_id = readl(d->regs + DSI_ID_REG);
	dev_info(dev, "HW revision 0x%08x\n", dsi_id);

	host = &d->dsi_host;
	host->dev = dev;
	host->ops = &mcde_dsi_host_ops;
	ret = mipi_dsi_host_register(host);
	if (ret < 0) {
		dev_err(dev, "failed to register DSI host: %d\n", ret);
		return ret;
	}
	dev_info(dev, "registered DSI host\n");

	platform_set_drvdata(pdev, d);
	return component_add(dev, &mcde_dsi_component_ops);
}

static int mcde_dsi_remove(struct platform_device *pdev)
{
	struct mcde_dsi *d = platform_get_drvdata(pdev);

	component_del(&pdev->dev, &mcde_dsi_component_ops);
	mipi_dsi_host_unregister(&d->dsi_host);

	return 0;
}

static const struct of_device_id mcde_dsi_of_match[] = {
	{
		.compatible = "ste,mcde-dsi",
	},
	{},
};

struct platform_driver mcde_dsi_driver = {
	.driver = {
		.name           = "mcde-dsi",
		.of_match_table = of_match_ptr(mcde_dsi_of_match),
	},
	.probe = mcde_dsi_probe,
	.remove = mcde_dsi_remove,
};
