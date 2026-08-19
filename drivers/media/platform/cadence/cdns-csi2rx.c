// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for Cadence MIPI-CSI2 RX Controller v1.3
 *
 * Copyright (C) 2017 Cadence Design Systems Inc.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/slab.h>

#include <media/cadence/cdns-csi2rx.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#define CSI2RX_DEVICE_CFG_REG			0x000

#define CSI2RX_SOFT_RESET_REG			0x004
#define CSI2RX_SOFT_RESET_PROTOCOL			BIT(1)
#define CSI2RX_SOFT_RESET_FRONT				BIT(0)

#define CSI2RX_STATIC_CFG_REG			0x008
#define CSI2RX_STATIC_CFG_DLANE_MAP(llane, plane)	((plane) << (16 + (llane) * 4))
#define CSI2RX_STATIC_CFG_LANES_MASK			GENMASK(11, 8)

#define CSI2RX_DPHY_LANE_CTRL_REG		0x40
#define CSI2RX_DPHY_CL_RST			BIT(16)
#define CSI2RX_DPHY_DL_RST(i)			BIT((i) + 12)
#define CSI2RX_DPHY_CL_EN			BIT(4)
#define CSI2RX_DPHY_DL_EN(i)			BIT(i)

#define CSI2RX_STREAM_BASE(n)		(((n) + 1) * 0x100)

#define CSI2RX_STREAM_CTRL_REG(n)		(CSI2RX_STREAM_BASE(n) + 0x000)
#define CSI2RX_STREAM_CTRL_SOFT_RST			BIT(4)
#define CSI2RX_STREAM_CTRL_STOP				BIT(1)
#define CSI2RX_STREAM_CTRL_START			BIT(0)

#define CSI2RX_STREAM_STATUS_REG(n)		(CSI2RX_STREAM_BASE(n) + 0x004)
#define CSI2RX_STREAM_STATUS_RDY			BIT(31)

#define CSI2RX_STREAM_DATA_CFG_REG(n)		(CSI2RX_STREAM_BASE(n) + 0x008)
#define CSI2RX_STREAM_DATA_CFG_VC_SELECT(n)		BIT((n) + 16)

#define CSI2RX_STREAM_CFG_REG(n)		(CSI2RX_STREAM_BASE(n) + 0x00c)
#define CSI2RX_STREAM_CFG_FIFO_MODE_LARGE_BUF		BIT(8)
#define CSI2RX_STREAM_CFG_NUM_PIXELS_MASK		GENMASK(5, 4)
#define CSI2RX_STREAM_CFG_NUM_PIXELS(n)			((n) >> 1U)

#define CSI2RX_LANES_MAX	4
#define CSI2RX_STREAMS_MAX	4

#define CSI2RX_ERROR_IRQS_REG			0x28
#define CSI2RX_ERROR_IRQS_MASK_REG		0x2C

#define CSI2RX_STREAM3_FIFO_OVERFLOW_IRQ	BIT(19)
#define CSI2RX_STREAM2_FIFO_OVERFLOW_IRQ	BIT(18)
#define CSI2RX_STREAM1_FIFO_OVERFLOW_IRQ	BIT(17)
#define CSI2RX_STREAM0_FIFO_OVERFLOW_IRQ	BIT(16)
#define CSI2RX_FRONT_TRUNC_HDR_IRQ		BIT(12)
#define CSI2RX_PROT_TRUNCATED_PACKET_IRQ	BIT(11)
#define CSI2RX_FRONT_LP_NO_PAYLOAD_IRQ		BIT(10)
#define CSI2RX_SP_INVALID_RCVD_IRQ		BIT(9)
#define CSI2RX_DATA_ID_IRQ			BIT(7)
#define CSI2RX_HEADER_CORRECTED_ECC_IRQ	BIT(6)
#define CSI2RX_HEADER_ECC_IRQ			BIT(5)
#define CSI2RX_PAYLOAD_CRC_IRQ			BIT(4)

#define CSI2RX_ECC_ERRORS		GENMASK(7, 4)
#define CSI2RX_PACKET_ERRORS		GENMASK(12, 9)

enum csi2rx_pads {
	CSI2RX_PAD_SINK,
	CSI2RX_PAD_SOURCE_STREAM0,
	CSI2RX_PAD_SOURCE_STREAM1,
	CSI2RX_PAD_SOURCE_STREAM2,
	CSI2RX_PAD_SOURCE_STREAM3,
	CSI2RX_PAD_MAX,
};

struct csi2rx_fmt {
	u32				code;
	/* width of a single pixel on CSI-2 bus */
	u8				bpp;
	/* max pixels per clock supported on output bus */
	u8				max_pixels;
};

struct csi2rx_event {
	u32 mask;
	const char *name;
};

static const struct csi2rx_event csi2rx_events[] = {
	{ CSI2RX_STREAM3_FIFO_OVERFLOW_IRQ, "Overflow of the Stream 3 FIFO detected" },
	{ CSI2RX_STREAM2_FIFO_OVERFLOW_IRQ, "Overflow of the Stream 2 FIFO detected" },
	{ CSI2RX_STREAM1_FIFO_OVERFLOW_IRQ, "Overflow of the Stream 1 FIFO detected" },
	{ CSI2RX_STREAM0_FIFO_OVERFLOW_IRQ, "Overflow of the Stream 0 FIFO detected" },
	{ CSI2RX_FRONT_TRUNC_HDR_IRQ, "A truncated header [short or long] has been received" },
	{ CSI2RX_PROT_TRUNCATED_PACKET_IRQ, "A truncated long packet has been received" },
	{ CSI2RX_FRONT_LP_NO_PAYLOAD_IRQ, "A truncated long packet has been received. No payload" },
	{ CSI2RX_SP_INVALID_RCVD_IRQ, "A reserved or invalid short packet has been received" },
	{ CSI2RX_DATA_ID_IRQ, "Data ID error in the header packet" },
	{ CSI2RX_HEADER_CORRECTED_ECC_IRQ, "ECC error detected and corrected" },
	{ CSI2RX_HEADER_ECC_IRQ, "Unrecoverable ECC error" },
	{ CSI2RX_PAYLOAD_CRC_IRQ, "CRC error" },
};

#define CSI2RX_NUM_EVENTS		ARRAY_SIZE(csi2rx_events)

struct csi2rx_priv {
	struct device			*dev;
	unsigned int			count;
	int				error_irq;

	void __iomem			*base;
	struct clk			*sys_clk;
	struct clk			*p_clk;
	struct clk			*pixel_clk[CSI2RX_STREAMS_MAX];
	struct reset_control		*sys_rst;
	struct reset_control		*p_rst;
	struct reset_control		*pixel_rst[CSI2RX_STREAMS_MAX];
	struct phy			*dphy;

	u8				num_pixels[CSI2RX_STREAMS_MAX];
	u32				vc_select[CSI2RX_STREAMS_MAX];
	u8				lanes[CSI2RX_LANES_MAX];
	u8				num_lanes;
	u8				max_lanes;
	u8				max_streams;
	bool				has_internal_dphy;
	u32				events[CSI2RX_NUM_EVENTS];

	struct v4l2_subdev		subdev;
	struct v4l2_async_notifier	notifier;
	struct media_pad		pads[CSI2RX_PAD_MAX];

	/* Remote source */
	struct v4l2_subdev		*source_subdev;
	int				source_pad;
};

static const struct csi2rx_fmt formats[] = {
	{ .code	= MEDIA_BUS_FMT_YUYV8_1X16, .bpp = 16, .max_pixels = 2, },
	{ .code	= MEDIA_BUS_FMT_UYVY8_1X16, .bpp = 16, .max_pixels = 2, },
	{ .code	= MEDIA_BUS_FMT_YVYU8_1X16, .bpp = 16, .max_pixels = 2, },
	{ .code	= MEDIA_BUS_FMT_VYUY8_1X16, .bpp = 16, .max_pixels = 2, },
	{ .code	= MEDIA_BUS_FMT_SBGGR8_1X8, .bpp = 8, .max_pixels = 4, },
	{ .code	= MEDIA_BUS_FMT_SGBRG8_1X8, .bpp = 8, .max_pixels = 4, },
	{ .code	= MEDIA_BUS_FMT_SGRBG8_1X8, .bpp = 8, .max_pixels = 4, },
	{ .code	= MEDIA_BUS_FMT_SRGGB8_1X8, .bpp = 8, .max_pixels = 4, },
	{ .code	= MEDIA_BUS_FMT_Y8_1X8,     .bpp = 8, .max_pixels = 4, },
	{ .code	= MEDIA_BUS_FMT_SBGGR10_1X10, .bpp = 10, .max_pixels = 2, },
	{ .code	= MEDIA_BUS_FMT_SGBRG10_1X10, .bpp = 10, .max_pixels = 2, },
	{ .code	= MEDIA_BUS_FMT_SGRBG10_1X10, .bpp = 10, .max_pixels = 2, },
	{ .code	= MEDIA_BUS_FMT_SRGGB10_1X10, .bpp = 10, .max_pixels = 2, },
	{ .code	= MEDIA_BUS_FMT_RGB565_1X16,  .bpp = 16, .max_pixels = 1, },
	{ .code	= MEDIA_BUS_FMT_RGB888_1X24,  .bpp = 24, .max_pixels = 1, },
	{ .code	= MEDIA_BUS_FMT_BGR888_1X24,  .bpp = 24, .max_pixels = 1, },
};

static void csi2rx_configure_error_irq_mask(void __iomem *base,
					    struct csi2rx_priv *csi2rx)
{
	u32 error_irq_mask = 0;

	error_irq_mask |= CSI2RX_ECC_ERRORS;
	error_irq_mask |= CSI2RX_PACKET_ERRORS;

	/*
	 * Iterate through all source pads and check if they are linked
	 * to an active remote pad. If an active remote pad is found,
	 * calculate the corresponding bit position and set it in
	 * mask, enabling the stream overflow error in the mask.
	 */
	for (int i = CSI2RX_PAD_SOURCE_STREAM0; i < CSI2RX_PAD_MAX; i++) {
		struct media_pad *remote_pad;

		remote_pad = media_pad_remote_pad_first(&csi2rx->pads[i]);
		if (remote_pad) {
			int pad = i - CSI2RX_PAD_SOURCE_STREAM0;
			u32 bit_mask = CSI2RX_STREAM0_FIFO_OVERFLOW_IRQ << pad;

			error_irq_mask |= bit_mask;
		}
	}

	writel(error_irq_mask, base + CSI2RX_ERROR_IRQS_MASK_REG);
}

static irqreturn_t csi2rx_irq_handler(int irq, void *dev_id)
{
	struct csi2rx_priv *csi2rx = dev_id;
	int i;
	u32 error_status, error_mask;

	error_status = readl(csi2rx->base + CSI2RX_ERROR_IRQS_REG);
	error_mask = readl(csi2rx->base + CSI2RX_ERROR_IRQS_MASK_REG);

	for (i = 0; i < CSI2RX_NUM_EVENTS; i++)
		if ((error_status & csi2rx_events[i].mask) &&
		    (error_mask & csi2rx_events[i].mask))
			csi2rx->events[i]++;

	writel(error_status, csi2rx->base + CSI2RX_ERROR_IRQS_REG);

	return IRQ_HANDLED;
}

static const struct csi2rx_fmt *csi2rx_get_fmt_by_code(u32 code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(formats); i++)
		if (formats[i].code == code)
			return &formats[i];

	return NULL;
}

static int csi2rx_get_frame_desc_from_source(struct csi2rx_priv *csi2rx,
					     struct v4l2_mbus_frame_desc *fd)
{
	struct media_pad *remote_pad;

	remote_pad = media_entity_remote_source_pad_unique(&csi2rx->subdev.entity);
	if (IS_ERR(remote_pad)) {
		dev_err(csi2rx->dev, "No remote pad found for sink\n");
		return PTR_ERR(remote_pad);
	}

	return v4l2_subdev_call(csi2rx->source_subdev, pad, get_frame_desc,
				remote_pad->index, fd);
}

static inline
struct csi2rx_priv *v4l2_subdev_to_csi2rx(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct csi2rx_priv, subdev);
}

static void csi2rx_reset(struct csi2rx_priv *csi2rx)
{
	unsigned int i;

	/* Reset module */
	writel(CSI2RX_SOFT_RESET_PROTOCOL | CSI2RX_SOFT_RESET_FRONT,
	       csi2rx->base + CSI2RX_SOFT_RESET_REG);
	/* Reset individual streams. */
	for (i = 0; i < csi2rx->max_streams; i++) {
		writel(CSI2RX_STREAM_CTRL_SOFT_RST,
		       csi2rx->base + CSI2RX_STREAM_CTRL_REG(i));
	}

	usleep_range(10, 20);

	/* Clear resets */
	writel(0, csi2rx->base + CSI2RX_SOFT_RESET_REG);
	for (i = 0; i < csi2rx->max_streams; i++)
		writel(0, csi2rx->base + CSI2RX_STREAM_CTRL_REG(i));
}

static int csi2rx_configure_ext_dphy(struct csi2rx_priv *csi2rx)
{
	union phy_configure_opts opts = { };
	struct phy_configure_opts_mipi_dphy *cfg = &opts.mipi_dphy;
	struct v4l2_mbus_framefmt *framefmt;
	struct v4l2_subdev_state *state;
	const struct csi2rx_fmt *fmt;
	struct v4l2_subdev_route *route;
	int source_pad = csi2rx->source_pad;
	struct media_pad *pad = &csi2rx->source_subdev->entity.pads[source_pad];
	s64 link_freq;
	int ret;
	u32 bpp;

	state = v4l2_subdev_get_locked_active_state(&csi2rx->subdev);

	/*
	 * For multi-stream transmitters there is no single pixel rate.
	 *
	 * In multistream usecase pass bpp as 0 so that v4l2_get_link_freq()
	 * returns an error if it falls back to V4L2_CID_PIXEL_RATE.
	 */
	if (state->routing.num_routes > 1) {
		bpp = 0;
	} else {
		route = &state->routing.routes[0];
		framefmt = v4l2_subdev_state_get_format(state, CSI2RX_PAD_SINK,
							route->sink_stream);
		if (!framefmt) {
			dev_err(csi2rx->dev, "Did not find active sink format\n");
			return -EINVAL;
		}

		fmt = csi2rx_get_fmt_by_code(framefmt->code);
		bpp = fmt->bpp;
	}

	link_freq = v4l2_get_link_freq(pad, bpp, 2 * csi2rx->num_lanes);
	if (link_freq < 0) {
		dev_err(csi2rx->dev, "Unable to calculate link frequency\n");
		return link_freq;
	}

	ret = phy_mipi_dphy_get_default_config_for_hsclk(link_freq,
							 csi2rx->num_lanes, cfg);
	if (ret)
		return ret;

	ret = phy_power_on(csi2rx->dphy);
	if (ret)
		return ret;

	ret = phy_configure(csi2rx->dphy, &opts);
	if (ret) {
		phy_power_off(csi2rx->dphy);
		return ret;
	}

	return 0;
}

static int csi2rx_start(struct csi2rx_priv *csi2rx)
{
	unsigned int i;
	unsigned long lanes_used = 0;
	u32 reg;
	int ret;

	csi2rx_reset(csi2rx);

	if (csi2rx->error_irq >= 0)
		csi2rx_configure_error_irq_mask(csi2rx->base, csi2rx);

	reg = csi2rx->num_lanes << 8;
	for (i = 0; i < csi2rx->num_lanes; i++) {
		reg |= CSI2RX_STATIC_CFG_DLANE_MAP(i, csi2rx->lanes[i]);
		set_bit(csi2rx->lanes[i], &lanes_used);
	}

	/*
	 * Even the unused lanes need to be mapped. In order to avoid
	 * to map twice to the same physical lane, keep the lanes used
	 * in the previous loop, and only map unused physical lanes to
	 * the rest of our logical lanes.
	 */
	for (i = csi2rx->num_lanes; i < csi2rx->max_lanes; i++) {
		unsigned int idx = find_first_zero_bit(&lanes_used,
						       csi2rx->max_lanes);
		set_bit(idx, &lanes_used);
		reg |= CSI2RX_STATIC_CFG_DLANE_MAP(i, i + 1);
	}

	writel(reg, csi2rx->base + CSI2RX_STATIC_CFG_REG);

	/* Enable DPHY clk and data lanes. */
	if (csi2rx->dphy) {
		reg = CSI2RX_DPHY_CL_EN | CSI2RX_DPHY_CL_RST;
		for (i = 0; i < csi2rx->num_lanes; i++) {
			reg |= CSI2RX_DPHY_DL_EN(csi2rx->lanes[i] - 1);
			reg |= CSI2RX_DPHY_DL_RST(csi2rx->lanes[i] - 1);
		}

		writel(reg, csi2rx->base + CSI2RX_DPHY_LANE_CTRL_REG);

		ret = csi2rx_configure_ext_dphy(csi2rx);
		if (ret) {
			dev_err(csi2rx->dev,
				"Failed to configure external DPHY: %d\n", ret);
			return ret;
		}
	}

	/*
	 * Create a static mapping between the CSI virtual channels
	 * and the output stream.
	 *
	 * This should be enhanced, but v4l2 lacks the support for
	 * changing that mapping dynamically.
	 *
	 * We also cannot enable and disable independent streams here,
	 * hence the reference counting.
	 */
	for (i = 0; i < csi2rx->max_streams; i++) {
		writel(CSI2RX_STREAM_CFG_FIFO_MODE_LARGE_BUF |
			       FIELD_PREP(CSI2RX_STREAM_CFG_NUM_PIXELS_MASK,
					  csi2rx->num_pixels[i]),
		       csi2rx->base + CSI2RX_STREAM_CFG_REG(i));

		writel(csi2rx->vc_select[i],
		       csi2rx->base + CSI2RX_STREAM_DATA_CFG_REG(i));

		writel(CSI2RX_STREAM_CTRL_START,
		       csi2rx->base + CSI2RX_STREAM_CTRL_REG(i));
	}


	return 0;
}

static void csi2rx_stop(struct csi2rx_priv *csi2rx)
{
	unsigned int i;
	u32 val;
	int ret;

	writel(0, csi2rx->base + CSI2RX_ERROR_IRQS_MASK_REG);

	for (i = 0; i < csi2rx->max_streams; i++) {
		writel(CSI2RX_STREAM_CTRL_STOP,
		       csi2rx->base + CSI2RX_STREAM_CTRL_REG(i));

		ret = readl_relaxed_poll_timeout(csi2rx->base +
						 CSI2RX_STREAM_STATUS_REG(i),
						 val,
						 !(val & CSI2RX_STREAM_STATUS_RDY),
						 10, 10000);
		if (ret)
			dev_warn(csi2rx->dev,
				 "Failed to stop streaming on pad%u\n", i);
	}

	if (csi2rx->dphy) {
		writel(0, csi2rx->base + CSI2RX_DPHY_LANE_CTRL_REG);

		if (phy_power_off(csi2rx->dphy))
			dev_warn(csi2rx->dev, "Couldn't power off DPHY\n");
	}
}

static int csi2rx_log_status(struct v4l2_subdev *sd)
{
	struct csi2rx_priv *csi2rx = v4l2_subdev_to_csi2rx(sd);
	unsigned int i;

	for (i = 0; i < CSI2RX_NUM_EVENTS; i++) {
		if (csi2rx->events[i])
			dev_info(csi2rx->dev, "%s events: %d\n",
				 csi2rx_events[i].name,
				 csi2rx->events[i]);
	}

	return 0;
}

static void csi2rx_update_vc_select(struct csi2rx_priv *csi2rx,
				    struct v4l2_subdev_state *state)
{
	struct v4l2_mbus_frame_desc fd = {0};
	struct v4l2_subdev_route *route;
	unsigned int i;
	int ret;

	ret = csi2rx_get_frame_desc_from_source(csi2rx, &fd);
	if (ret || fd.type != V4L2_MBUS_FRAME_DESC_TYPE_CSI2) {
		dev_dbg(csi2rx->dev,
			"Failed to get source frame desc, allowing only VC=0\n");
		for (i = 0; i < CSI2RX_STREAMS_MAX; i++)
			csi2rx->vc_select[i] = CSI2RX_STREAM_DATA_CFG_VC_SELECT(0);
		return;
	}

	/* If source provides per-stream VC info, use it to filter by VC */
	memset(csi2rx->vc_select, 0, sizeof(csi2rx->vc_select));

	for_each_active_route(&state->routing, route) {
		u32 cdns_stream = route->source_pad - CSI2RX_PAD_SOURCE_STREAM0;

		for (i = 0; i < fd.num_entries; i++) {
			if (fd.entry[i].stream != route->sink_stream)
				continue;

			csi2rx->vc_select[cdns_stream] |=
				CSI2RX_STREAM_DATA_CFG_VC_SELECT(fd.entry[i].bus.csi2.vc);
		}
	}
}

static int csi2rx_enable_streams(struct v4l2_subdev *subdev,
				 struct v4l2_subdev_state *state, u32 pad,
				 u64 streams_mask)
{
	struct csi2rx_priv *csi2rx = v4l2_subdev_to_csi2rx(subdev);
	u64 sink_streams;
	int ret;

	sink_streams = v4l2_subdev_state_xlate_streams(state, pad,
						       CSI2RX_PAD_SINK,
						       &streams_mask);

	/*
	 * If we're not the first users, there's no need to
	 * enable the whole controller.
	 */
	if (!csi2rx->count) {
		ret = pm_runtime_resume_and_get(csi2rx->dev);
		if (ret < 0)
			goto err;

		csi2rx_update_vc_select(csi2rx, state);

		ret = csi2rx_start(csi2rx);
		if (ret)
			goto err_put_pm;
	}

	/* Start streaming on the source */
	ret = v4l2_subdev_enable_streams(csi2rx->source_subdev, csi2rx->source_pad,
					 sink_streams);
	if (ret) {
		dev_err(csi2rx->dev,
			"Failed to start streams %#llx on subdev\n",
			sink_streams);
		goto err_stop_csi;
	}

	csi2rx->count++;
	return 0;

err_stop_csi:
	if (!csi2rx->count)
		csi2rx_stop(csi2rx);
err_put_pm:
	if (!csi2rx->count)
		pm_runtime_put(csi2rx->dev);
err:
	return ret;
}

static int csi2rx_disable_streams(struct v4l2_subdev *subdev,
				  struct v4l2_subdev_state *state, u32 pad,
				  u64 streams_mask)
{
	struct csi2rx_priv *csi2rx = v4l2_subdev_to_csi2rx(subdev);
	u64 sink_streams;

	sink_streams = v4l2_subdev_state_xlate_streams(state, pad,
						       CSI2RX_PAD_SINK,
						       &streams_mask);

	if (v4l2_subdev_disable_streams(csi2rx->source_subdev,
						 csi2rx->source_pad, sink_streams)) {
		dev_err(csi2rx->dev, "Couldn't disable our subdev\n");
	}

	csi2rx->count--;

	/* Let the last user turn off the lights. */
	if (!csi2rx->count) {
		csi2rx_stop(csi2rx);
		pm_runtime_put(csi2rx->dev);
	}

	return 0;
}

static int csi2rx_enum_mbus_code(struct v4l2_subdev *subdev,
				 struct v4l2_subdev_state *state,
				 struct v4l2_subdev_mbus_code_enum *code_enum)
{
	if (code_enum->index >= ARRAY_SIZE(formats))
		return -EINVAL;

	code_enum->code = formats[code_enum->index].code;

	return 0;
}

static int _csi2rx_set_routing(struct v4l2_subdev *subdev,
			       struct v4l2_subdev_state *state,
			       struct v4l2_subdev_krouting *routing)
{
	static const struct v4l2_mbus_framefmt format = {
		.width = 640,
		.height = 480,
		.code = MEDIA_BUS_FMT_UYVY8_1X16,
		.field = V4L2_FIELD_NONE,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.ycbcr_enc = V4L2_YCBCR_ENC_601,
		.quantization = V4L2_QUANTIZATION_LIM_RANGE,
		.xfer_func = V4L2_XFER_FUNC_SRGB,
	};
	int ret;

	ret = v4l2_subdev_routing_validate(subdev, routing,
					   V4L2_SUBDEV_ROUTING_ONLY_1_TO_1);
	if (ret)
		return ret;

	return v4l2_subdev_set_routing_with_fmt(subdev, state, routing, &format);
}

static int csi2rx_set_routing(struct v4l2_subdev *subdev,
			      struct v4l2_subdev_state *state,
			      enum v4l2_subdev_format_whence which,
			      struct v4l2_subdev_krouting *routing)
{
	struct csi2rx_priv *csi2rx = v4l2_subdev_to_csi2rx(subdev);
	int ret;

	if (which == V4L2_SUBDEV_FORMAT_ACTIVE && csi2rx->count)
		return -EBUSY;

	ret = _csi2rx_set_routing(subdev, state, routing);
	if (ret)
		return ret;

	return 0;
}

static int csi2rx_set_fmt(struct v4l2_subdev *subdev,
			  struct v4l2_subdev_state *state,
			  struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *fmt;

	/* No transcoding, source and sink formats must match. */
	if (format->pad != CSI2RX_PAD_SINK)
		return v4l2_subdev_get_fmt(subdev, state, format);

	if (!csi2rx_get_fmt_by_code(format->format.code))
		format->format.code = formats[0].code;

	format->format.field = V4L2_FIELD_NONE;

	/* Set sink format */
	fmt = v4l2_subdev_state_get_format(state, format->pad, format->stream);
	*fmt = format->format;

	/* Propagate to source format */
	fmt = v4l2_subdev_state_get_opposite_stream_format(state, format->pad,
							   format->stream);
	if (!fmt)
		return -EINVAL;

	*fmt = format->format;

	return 0;
}

static int csi2rx_init_state(struct v4l2_subdev *subdev,
			     struct v4l2_subdev_state *state)
{
	struct v4l2_subdev_route routes[] = {
		{
			.sink_pad = CSI2RX_PAD_SINK,
			.sink_stream = 0,
			.source_pad = CSI2RX_PAD_SOURCE_STREAM0,
			.source_stream = 0,
			.flags = V4L2_SUBDEV_ROUTE_FL_ACTIVE,
		},
	};

	struct v4l2_subdev_krouting routing = {
		.num_routes = ARRAY_SIZE(routes),
		.routes = routes,
	};

	return _csi2rx_set_routing(subdev, state, &routing);
}

int cdns_csi2rx_negotiate_ppc(struct v4l2_subdev *subdev, unsigned int pad,
			      u8 *ppc)
{
	struct csi2rx_priv *csi2rx = v4l2_subdev_to_csi2rx(subdev);
	const struct csi2rx_fmt *csi_fmt;
	struct v4l2_subdev_route *route;
	struct v4l2_subdev_state *state;
	struct v4l2_mbus_framefmt *fmt;
	int ret = 0;

	if (!ppc || pad < CSI2RX_PAD_SOURCE_STREAM0 || pad >= CSI2RX_PAD_MAX)
		return -EINVAL;

	state = v4l2_subdev_lock_and_get_active_state(subdev);
	/* Check all streams on requested pad */
	for_each_active_route(&state->routing, route) {
		if (route->source_pad != pad)
			continue;

		fmt = v4l2_subdev_state_get_format(state, route->source_pad,
						   route->source_stream);
		if (!fmt) {
			ret = -EPIPE;
			*ppc = 1;
			break;
		}

		csi_fmt = csi2rx_get_fmt_by_code(fmt->code);
		if (!csi_fmt) {
			ret = -EINVAL;
			*ppc = 1;
			break;
		}

		/* Reduce requested PPC if it is too high for this stream */
		*ppc = min(*ppc, csi_fmt->max_pixels);
	}
	v4l2_subdev_unlock_state(state);

	csi2rx->num_pixels[pad - CSI2RX_PAD_SOURCE_STREAM0] =
		CSI2RX_STREAM_CFG_NUM_PIXELS(*ppc);

	return ret;
}
EXPORT_SYMBOL_FOR_MODULES(cdns_csi2rx_negotiate_ppc, "j721e-csi2rx");

static const struct v4l2_subdev_pad_ops csi2rx_pad_ops = {
	.enum_mbus_code		= csi2rx_enum_mbus_code,
	.get_fmt		= v4l2_subdev_get_fmt,
	.set_fmt		= csi2rx_set_fmt,
	.get_frame_desc		= v4l2_subdev_get_frame_desc_passthrough,
	.set_routing		= csi2rx_set_routing,
	.enable_streams		= csi2rx_enable_streams,
	.disable_streams	= csi2rx_disable_streams,
};

static const struct v4l2_subdev_core_ops csi2rx_core_ops = {
	.log_status	= csi2rx_log_status,
};

static const struct v4l2_subdev_ops csi2rx_subdev_ops = {
	.core		= &csi2rx_core_ops,
	.pad		= &csi2rx_pad_ops,
};

static const struct v4l2_subdev_internal_ops csi2rx_internal_ops = {
	.init_state	= csi2rx_init_state,
};

static const struct media_entity_operations csi2rx_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
	.get_fwnode_pad = v4l2_subdev_get_fwnode_pad_1_to_1,
	.has_pad_interdep = v4l2_subdev_has_pad_interdep,
};

static int csi2rx_async_bound(struct v4l2_async_notifier *notifier,
			      struct v4l2_subdev *s_subdev,
			      struct v4l2_async_connection *asd)
{
	struct v4l2_subdev *subdev = notifier->sd;
	struct csi2rx_priv *csi2rx = v4l2_subdev_to_csi2rx(subdev);

	csi2rx->source_pad = media_entity_get_fwnode_pad(&s_subdev->entity,
							 asd->match.fwnode,
							 MEDIA_PAD_FL_SOURCE);
	if (csi2rx->source_pad < 0) {
		dev_err(csi2rx->dev, "Couldn't find output pad for subdev %s\n",
			s_subdev->name);
		return csi2rx->source_pad;
	}

	csi2rx->source_subdev = s_subdev;

	dev_dbg(csi2rx->dev, "Bound %s pad: %d\n", s_subdev->name,
		csi2rx->source_pad);

	return media_create_pad_link(&csi2rx->source_subdev->entity,
				     csi2rx->source_pad,
				     &csi2rx->subdev.entity, 0,
				     MEDIA_LNK_FL_ENABLED |
				     MEDIA_LNK_FL_IMMUTABLE);
}

static const struct v4l2_async_notifier_operations csi2rx_notifier_ops = {
	.bound		= csi2rx_async_bound,
};

static int csi2rx_get_resources(struct csi2rx_priv *csi2rx,
				struct platform_device *pdev)
{
	unsigned char i;
	u32 dev_cfg;
	int ret;

	csi2rx->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(csi2rx->base))
		return PTR_ERR(csi2rx->base);

	csi2rx->sys_clk = devm_clk_get(&pdev->dev, "sys_clk");
	if (IS_ERR(csi2rx->sys_clk)) {
		dev_err(&pdev->dev, "Couldn't get sys clock\n");
		return PTR_ERR(csi2rx->sys_clk);
	}

	csi2rx->p_clk = devm_clk_get(&pdev->dev, "p_clk");
	if (IS_ERR(csi2rx->p_clk)) {
		dev_err(&pdev->dev, "Couldn't get P clock\n");
		return PTR_ERR(csi2rx->p_clk);
	}

	csi2rx->sys_rst = devm_reset_control_get_optional_exclusive(&pdev->dev,
								    "sys");
	if (IS_ERR(csi2rx->sys_rst))
		return PTR_ERR(csi2rx->sys_rst);

	csi2rx->p_rst = devm_reset_control_get_optional_exclusive(&pdev->dev,
								  "reg_bank");
	if (IS_ERR(csi2rx->p_rst))
		return PTR_ERR(csi2rx->p_rst);

	csi2rx->dphy = devm_phy_optional_get(&pdev->dev, "dphy");
	if (IS_ERR(csi2rx->dphy)) {
		dev_err(&pdev->dev, "Couldn't get external D-PHY\n");
		return PTR_ERR(csi2rx->dphy);
	}

	ret = clk_prepare_enable(csi2rx->p_clk);
	if (ret) {
		dev_err(&pdev->dev, "Couldn't prepare and enable P clock\n");
		return ret;
	}

	dev_cfg = readl(csi2rx->base + CSI2RX_DEVICE_CFG_REG);
	clk_disable_unprepare(csi2rx->p_clk);

	csi2rx->max_lanes = dev_cfg & 7;
	if (csi2rx->max_lanes > CSI2RX_LANES_MAX) {
		dev_err(&pdev->dev, "Invalid number of lanes: %u\n",
			csi2rx->max_lanes);
		return -EINVAL;
	}

	csi2rx->max_streams = (dev_cfg >> 4) & 7;
	if (csi2rx->max_streams > CSI2RX_STREAMS_MAX) {
		dev_err(&pdev->dev, "Invalid number of streams: %u\n",
			csi2rx->max_streams);
		return -EINVAL;
	}

	csi2rx->has_internal_dphy = dev_cfg & BIT(3) ? true : false;

	/*
	 * FIXME: Once we'll have internal D-PHY support, the check
	 * will need to be removed.
	 */
	if (!csi2rx->dphy && csi2rx->has_internal_dphy) {
		dev_err(&pdev->dev, "Internal D-PHY not supported yet\n");
		return -EINVAL;
	}

	for (i = 0; i < csi2rx->max_streams; i++) {
		char name[16];

		snprintf(name, sizeof(name), "pixel_if%u_clk", i);
		csi2rx->pixel_clk[i] = devm_clk_get(&pdev->dev, name);
		if (IS_ERR(csi2rx->pixel_clk[i])) {
			dev_err(&pdev->dev, "Couldn't get clock %s\n", name);
			return PTR_ERR(csi2rx->pixel_clk[i]);
		}

		snprintf(name, sizeof(name), "pixel_if%u", i);
		csi2rx->pixel_rst[i] =
			devm_reset_control_get_optional_exclusive(&pdev->dev,
								  name);
		if (IS_ERR(csi2rx->pixel_rst[i]))
			return PTR_ERR(csi2rx->pixel_rst[i]);
	}

	return 0;
}

static int csi2rx_parse_dt(struct csi2rx_priv *csi2rx)
{
	struct v4l2_fwnode_endpoint v4l2_ep = { .bus_type = 0 };
	struct v4l2_async_connection *asd;
	struct fwnode_handle *fwh;
	struct device_node *ep;
	int ret;

	ep = of_graph_get_endpoint_by_regs(csi2rx->dev->of_node, 0, 0);
	if (!ep)
		return -EINVAL;

	fwh = of_fwnode_handle(ep);
	ret = v4l2_fwnode_endpoint_parse(fwh, &v4l2_ep);
	if (ret) {
		dev_err(csi2rx->dev, "Could not parse v4l2 endpoint\n");
		of_node_put(ep);
		return ret;
	}

	if (v4l2_ep.bus_type != V4L2_MBUS_CSI2_DPHY) {
		dev_err(csi2rx->dev, "Unsupported media bus type: 0x%x\n",
			v4l2_ep.bus_type);
		of_node_put(ep);
		return -EINVAL;
	}

	memcpy(csi2rx->lanes, v4l2_ep.bus.mipi_csi2.data_lanes,
	       sizeof(csi2rx->lanes));
	csi2rx->num_lanes = v4l2_ep.bus.mipi_csi2.num_data_lanes;
	if (csi2rx->num_lanes > csi2rx->max_lanes) {
		dev_err(csi2rx->dev, "Unsupported number of data-lanes: %d\n",
			csi2rx->num_lanes);
		of_node_put(ep);
		return -EINVAL;
	}

	v4l2_async_subdev_nf_init(&csi2rx->notifier, &csi2rx->subdev);

	asd = v4l2_async_nf_add_fwnode_remote(&csi2rx->notifier, fwh,
					      struct v4l2_async_connection);
	of_node_put(ep);
	if (IS_ERR(asd)) {
		v4l2_async_nf_cleanup(&csi2rx->notifier);
		return PTR_ERR(asd);
	}

	csi2rx->notifier.ops = &csi2rx_notifier_ops;

	ret = v4l2_async_nf_register(&csi2rx->notifier);
	if (ret)
		v4l2_async_nf_cleanup(&csi2rx->notifier);

	return ret;
}

static int csi2rx_probe(struct platform_device *pdev)
{
	struct csi2rx_priv *csi2rx;
	unsigned int i;
	int ret;

	csi2rx = kzalloc_obj(*csi2rx);
	if (!csi2rx)
		return -ENOMEM;
	platform_set_drvdata(pdev, csi2rx);
	csi2rx->dev = &pdev->dev;

	ret = csi2rx_get_resources(csi2rx, pdev);
	if (ret)
		goto err_free_priv;

	ret = csi2rx_parse_dt(csi2rx);
	if (ret)
		goto err_free_priv;

	csi2rx->subdev.owner = THIS_MODULE;
	csi2rx->subdev.dev = &pdev->dev;
	v4l2_subdev_init(&csi2rx->subdev, &csi2rx_subdev_ops);
	csi2rx->subdev.internal_ops = &csi2rx_internal_ops;
	v4l2_set_subdevdata(&csi2rx->subdev, &pdev->dev);
	snprintf(csi2rx->subdev.name, sizeof(csi2rx->subdev.name),
		 "%s.%s", KBUILD_MODNAME, dev_name(&pdev->dev));

	/* Create our media pads */
	csi2rx->subdev.entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;
	csi2rx->pads[CSI2RX_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	for (i = CSI2RX_PAD_SOURCE_STREAM0; i < CSI2RX_PAD_MAX; i++)
		csi2rx->pads[i].flags = MEDIA_PAD_FL_SOURCE;
	csi2rx->subdev.flags = V4L2_SUBDEV_FL_HAS_DEVNODE |
		V4L2_SUBDEV_FL_STREAMS;
	csi2rx->subdev.entity.ops = &csi2rx_media_ops;

	ret = media_entity_pads_init(&csi2rx->subdev.entity, CSI2RX_PAD_MAX,
				     csi2rx->pads);
	if (ret)
		goto err_cleanup;

	csi2rx->error_irq = platform_get_irq_byname_optional(pdev, "error_irq");

	if (csi2rx->error_irq < 0) {
		dev_dbg(csi2rx->dev, "Optional interrupt not defined, proceeding without it\n");
	} else {
		ret = devm_request_irq(csi2rx->dev, csi2rx->error_irq,
				       csi2rx_irq_handler, 0,
				       dev_name(&pdev->dev), csi2rx);
		if (ret) {
			dev_err(csi2rx->dev,
				"Unable to request interrupt: %d\n", ret);
			goto err_cleanup;
		}
	}

	ret = v4l2_subdev_init_finalize(&csi2rx->subdev);
	if (ret)
		goto err_cleanup;

	pm_runtime_enable(csi2rx->dev);
	ret = v4l2_async_register_subdev(&csi2rx->subdev);
	if (ret < 0)
		goto err_free_state;

	dev_info(&pdev->dev,
		 "Probed CSI2RX with %u/%u lanes, %u streams, %s D-PHY\n",
		 csi2rx->num_lanes, csi2rx->max_lanes, csi2rx->max_streams,
		 csi2rx->dphy ? "external" :
		 csi2rx->has_internal_dphy ? "internal" : "no");

	return 0;

err_free_state:
	v4l2_subdev_cleanup(&csi2rx->subdev);
	pm_runtime_disable(csi2rx->dev);
err_cleanup:
	v4l2_async_nf_unregister(&csi2rx->notifier);
	v4l2_async_nf_cleanup(&csi2rx->notifier);
	media_entity_cleanup(&csi2rx->subdev.entity);
err_free_priv:
	kfree(csi2rx);
	return ret;
}

static void csi2rx_remove(struct platform_device *pdev)
{
	struct csi2rx_priv *csi2rx = platform_get_drvdata(pdev);

	v4l2_async_nf_unregister(&csi2rx->notifier);
	v4l2_async_nf_cleanup(&csi2rx->notifier);
	v4l2_async_unregister_subdev(&csi2rx->subdev);
	v4l2_subdev_cleanup(&csi2rx->subdev);
	media_entity_cleanup(&csi2rx->subdev.entity);
	pm_runtime_disable(csi2rx->dev);
	kfree(csi2rx);
}

static int csi2rx_runtime_suspend(struct device *dev)
{
	struct csi2rx_priv *csi2rx = dev_get_drvdata(dev);

	reset_control_assert(csi2rx->sys_rst);
	clk_disable_unprepare(csi2rx->sys_clk);

	for (unsigned int i = 0; i < csi2rx->max_streams; i++) {
		reset_control_assert(csi2rx->pixel_rst[i]);
		clk_disable_unprepare(csi2rx->pixel_clk[i]);
	}

	reset_control_assert(csi2rx->p_rst);
	clk_disable_unprepare(csi2rx->p_clk);

	return 0;
}

static int csi2rx_runtime_resume(struct device *dev)
{
	struct csi2rx_priv *csi2rx = dev_get_drvdata(dev);
	unsigned int i;
	int ret;

	ret = clk_prepare_enable(csi2rx->p_clk);
	if (ret)
		return ret;

	reset_control_deassert(csi2rx->p_rst);

	for (i = 0; i < csi2rx->max_streams; i++) {
		ret = clk_prepare_enable(csi2rx->pixel_clk[i]);
		if (ret)
			goto err_disable_pixclk;

		reset_control_deassert(csi2rx->pixel_rst[i]);
	}

	ret = clk_prepare_enable(csi2rx->sys_clk);
	if (ret)
		goto err_disable_pixclk;

	reset_control_deassert(csi2rx->sys_rst);

	return 0;

err_disable_pixclk:
	while (i--) {
		reset_control_assert(csi2rx->pixel_rst[i]);
		clk_disable_unprepare(csi2rx->pixel_clk[i]);
	}

	reset_control_assert(csi2rx->p_rst);
	clk_disable_unprepare(csi2rx->p_clk);

	return ret;
}

static const struct dev_pm_ops csi2rx_pm_ops = {
	RUNTIME_PM_OPS(csi2rx_runtime_suspend, csi2rx_runtime_resume, NULL)
};

static const struct of_device_id csi2rx_of_table[] = {
	{ .compatible = "starfive,jh7110-csi2rx" },
	{ .compatible = "cdns,csi2rx" },
	{ },
};
MODULE_DEVICE_TABLE(of, csi2rx_of_table);

static struct platform_driver csi2rx_driver = {
	.probe	= csi2rx_probe,
	.remove = csi2rx_remove,

	.driver	= {
		.name		= "cdns-csi2rx",
		.of_match_table	= csi2rx_of_table,
		.pm		= &csi2rx_pm_ops,
	},
};
module_platform_driver(csi2rx_driver);
MODULE_AUTHOR("Maxime Ripard <maxime.ripard@bootlin.com>");
MODULE_DESCRIPTION("Cadence CSI2-RX controller");
MODULE_LICENSE("GPL");
