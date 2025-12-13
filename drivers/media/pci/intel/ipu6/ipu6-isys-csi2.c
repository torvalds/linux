// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013--2024 Intel Corporation
 */

#include <linux/atomic.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/minmax.h>
#include <linux/sprintf.h>

#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-subdev.h>

#include "ipu6-bus.h"
#include "ipu6-isys.h"
#include "ipu6-isys-csi2.h"
#include "ipu6-isys-subdev.h"
#include "ipu6-platform-isys-csi2-reg.h"

static const u32 csi2_supported_codes[] = {
	MEDIA_BUS_FMT_RGB565_1X16,
	MEDIA_BUS_FMT_RGB888_1X24,
	MEDIA_BUS_FMT_UYVY8_1X16,
	MEDIA_BUS_FMT_YUYV8_1X16,
	MEDIA_BUS_FMT_SBGGR10_1X10,
	MEDIA_BUS_FMT_SGBRG10_1X10,
	MEDIA_BUS_FMT_SGRBG10_1X10,
	MEDIA_BUS_FMT_SRGGB10_1X10,
	MEDIA_BUS_FMT_SBGGR12_1X12,
	MEDIA_BUS_FMT_SGBRG12_1X12,
	MEDIA_BUS_FMT_SGRBG12_1X12,
	MEDIA_BUS_FMT_SRGGB12_1X12,
	MEDIA_BUS_FMT_SBGGR8_1X8,
	MEDIA_BUS_FMT_SGBRG8_1X8,
	MEDIA_BUS_FMT_SGRBG8_1X8,
	MEDIA_BUS_FMT_SRGGB8_1X8,
	MEDIA_BUS_FMT_META_8,
	MEDIA_BUS_FMT_META_10,
	MEDIA_BUS_FMT_META_12,
	MEDIA_BUS_FMT_META_16,
	MEDIA_BUS_FMT_META_24,
	0
};

/*
 * Strings corresponding to CSI-2 receiver errors are here.
 * Corresponding macros are defined in the header file.
 */
static const struct ipu6_csi2_error dphy_rx_errors[] = {
	{ "Single packet header error corrected", true },
	{ "Multiple packet header errors detected", true },
	{ "Payload checksum (CRC) error", true },
	{ "Transfer FIFO overflow", false },
	{ "Reserved short packet data type detected", true },
	{ "Reserved long packet data type detected", true },
	{ "Incomplete long packet detected", false },
	{ "Frame sync error", false },
	{ "Line sync error", false },
	{ "DPHY recoverable synchronization error", true },
	{ "DPHY fatal error", false },
	{ "DPHY elastic FIFO overflow", false },
	{ "Inter-frame short packet discarded", true },
	{ "Inter-frame long packet discarded", true },
	{ "MIPI pktgen overflow", false },
	{ "MIPI pktgen data loss", false },
	{ "FIFO overflow", false },
	{ "Lane deskew", false },
	{ "SOT sync error", false },
	{ "HSIDLE detected", false }
};

s64 ipu6_isys_csi2_get_link_freq(struct ipu6_isys_csi2 *csi2)
{
	struct media_pad *src_pad;

	if (!csi2)
		return -EINVAL;

	src_pad = media_entity_remote_source_pad_unique(&csi2->asd.sd.entity);
	if (IS_ERR(src_pad)) {
		dev_err(&csi2->isys->adev->auxdev.dev,
			"can't get source pad of %s (%ld)\n",
			csi2->asd.sd.name, PTR_ERR(src_pad));
		return PTR_ERR(src_pad);
	}

	return v4l2_get_link_freq(src_pad, 0, 0);
}

static int csi2_subscribe_event(struct v4l2_subdev *sd, struct v4l2_fh *fh,
				struct v4l2_event_subscription *sub)
{
	struct ipu6_isys_subdev *asd = to_ipu6_isys_subdev(sd);
	struct ipu6_isys_csi2 *csi2 = to_ipu6_isys_csi2(asd);
	struct device *dev = &csi2->isys->adev->auxdev.dev;

	dev_dbg(dev, "csi2 subscribe event(type %u id %u)\n",
		sub->type, sub->id);

	switch (sub->type) {
	case V4L2_EVENT_FRAME_SYNC:
		return v4l2_event_subscribe(fh, sub, 10, NULL);
	case V4L2_EVENT_CTRL:
		return v4l2_ctrl_subdev_subscribe_event(sd, fh, sub);
	default:
		return -EINVAL;
	}
}

static const struct v4l2_subdev_core_ops csi2_sd_core_ops = {
	.subscribe_event = csi2_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

/*
 * The input system CSI2+ receiver has several
 * parameters affecting the receiver timings. These depend
 * on the MIPI bus frequency F in Hz (sensor transmitter rate)
 * as follows:
 *	register value = (A/1e9 + B * UI) / COUNT_ACC
 * where
 *	UI = 1 / (2 * F) in seconds
 *	COUNT_ACC = counter accuracy in seconds
 *	COUNT_ACC = 0.125 ns = 1 / 8 ns, ACCINV = 8.
 *
 * A and B are coefficients from the table below,
 * depending whether the register minimum or maximum value is
 * calculated.
 *				       Minimum     Maximum
 * Clock lane			       A     B     A     B
 * reg_rx_csi_dly_cnt_termen_clane     0     0    38     0
 * reg_rx_csi_dly_cnt_settle_clane    95    -8   300   -16
 * Data lanes
 * reg_rx_csi_dly_cnt_termen_dlane0    0     0    35     4
 * reg_rx_csi_dly_cnt_settle_dlane0   85    -2   145    -6
 * reg_rx_csi_dly_cnt_termen_dlane1    0     0    35     4
 * reg_rx_csi_dly_cnt_settle_dlane1   85    -2   145    -6
 * reg_rx_csi_dly_cnt_termen_dlane2    0     0    35     4
 * reg_rx_csi_dly_cnt_settle_dlane2   85    -2   145    -6
 * reg_rx_csi_dly_cnt_termen_dlane3    0     0    35     4
 * reg_rx_csi_dly_cnt_settle_dlane3   85    -2   145    -6
 *
 * We use the minimum values of both A and B.
 */

#define DIV_SHIFT	8
#define CSI2_ACCINV	8

static u32 calc_timing(s32 a, s32 b, s64 link_freq, s32 accinv)
{
	return accinv * a + (accinv * b * (500000000 >> DIV_SHIFT)
			     / (s32)(link_freq >> DIV_SHIFT));
}

static int
ipu6_isys_csi2_calc_timing(struct ipu6_isys_csi2 *csi2,
			   struct ipu6_isys_csi2_timing *timing, s32 accinv)
{
	struct device *dev = &csi2->isys->adev->auxdev.dev;
	s64 link_freq;

	link_freq = ipu6_isys_csi2_get_link_freq(csi2);
	if (link_freq < 0)
		return link_freq;

	timing->ctermen = calc_timing(CSI2_CSI_RX_DLY_CNT_TERMEN_CLANE_A,
				      CSI2_CSI_RX_DLY_CNT_TERMEN_CLANE_B,
				      link_freq, accinv);
	timing->csettle = calc_timing(CSI2_CSI_RX_DLY_CNT_SETTLE_CLANE_A,
				      CSI2_CSI_RX_DLY_CNT_SETTLE_CLANE_B,
				      link_freq, accinv);
	timing->dtermen = calc_timing(CSI2_CSI_RX_DLY_CNT_TERMEN_DLANE_A,
				      CSI2_CSI_RX_DLY_CNT_TERMEN_DLANE_B,
				      link_freq, accinv);
	timing->dsettle = calc_timing(CSI2_CSI_RX_DLY_CNT_SETTLE_DLANE_A,
				      CSI2_CSI_RX_DLY_CNT_SETTLE_DLANE_B,
				      link_freq, accinv);

	dev_dbg(dev, "ctermen %u csettle %u dtermen %u dsettle %u\n",
		timing->ctermen, timing->csettle,
		timing->dtermen, timing->dsettle);

	return 0;
}

void ipu6_isys_register_errors(struct ipu6_isys_csi2 *csi2)
{
	u32 irq = readl(csi2->base + CSI_PORT_REG_BASE_IRQ_CSI +
			CSI_PORT_REG_BASE_IRQ_STATUS_OFFSET);
	struct ipu6_isys *isys = csi2->isys;
	u32 mask;

	mask = isys->pdata->ipdata->csi2.irq_mask;
	writel(irq & mask, csi2->base + CSI_PORT_REG_BASE_IRQ_CSI +
	       CSI_PORT_REG_BASE_IRQ_CLEAR_OFFSET);
	csi2->receiver_errors |= irq & mask;
}

void ipu6_isys_csi2_error(struct ipu6_isys_csi2 *csi2)
{
	struct device *dev = &csi2->isys->adev->auxdev.dev;
	const struct ipu6_csi2_error *errors;
	u32 status;
	u32 i;

	/* register errors once more in case of interrupts are disabled */
	ipu6_isys_register_errors(csi2);
	status = csi2->receiver_errors;
	csi2->receiver_errors = 0;
	errors = dphy_rx_errors;

	for (i = 0; i < CSI_RX_NUM_ERRORS_IN_IRQ; i++) {
		if (status & BIT(i))
			dev_err_ratelimited(dev, "csi2-%i error: %s\n",
					    csi2->port, errors[i].error_string);
	}
}

static int ipu6_isys_csi2_set_stream(struct v4l2_subdev *sd,
				     const struct ipu6_isys_csi2_timing *timing,
				     unsigned int nlanes, int enable)
{
	struct ipu6_isys_subdev *asd = to_ipu6_isys_subdev(sd);
	struct ipu6_isys_csi2 *csi2 = to_ipu6_isys_csi2(asd);
	struct ipu6_isys *isys = csi2->isys;
	struct device *dev = &isys->adev->auxdev.dev;
	struct ipu6_isys_csi2_config cfg;
	unsigned int nports;
	int ret = 0;
	u32 mask = 0;
	u32 i;

	dev_dbg(dev, "stream %s CSI2-%u with %u lanes\n", enable ? "on" : "off",
		csi2->port, nlanes);

	cfg.port = csi2->port;
	cfg.nlanes = nlanes;

	mask = isys->pdata->ipdata->csi2.irq_mask;
	nports = isys->pdata->ipdata->csi2.nports;

	if (!enable) {
		writel(0, csi2->base + CSI_REG_CSI_FE_ENABLE);
		writel(0, csi2->base + CSI_REG_PPI2CSI_ENABLE);

		writel(0,
		       csi2->base + CSI_PORT_REG_BASE_IRQ_CSI +
		       CSI_PORT_REG_BASE_IRQ_ENABLE_OFFSET);
		writel(mask,
		       csi2->base + CSI_PORT_REG_BASE_IRQ_CSI +
		       CSI_PORT_REG_BASE_IRQ_CLEAR_OFFSET);
		writel(0,
		       csi2->base + CSI_PORT_REG_BASE_IRQ_CSI_SYNC +
		       CSI_PORT_REG_BASE_IRQ_ENABLE_OFFSET);
		writel(0xffffffff,
		       csi2->base + CSI_PORT_REG_BASE_IRQ_CSI_SYNC +
		       CSI_PORT_REG_BASE_IRQ_CLEAR_OFFSET);

		isys->phy_set_power(isys, &cfg, timing, false);

		writel(0, isys->pdata->base + CSI_REG_HUB_FW_ACCESS_PORT
		       (isys->pdata->ipdata->csi2.fw_access_port_ofs,
			csi2->port));
		writel(0, isys->pdata->base +
		       CSI_REG_HUB_DRV_ACCESS_PORT(csi2->port));

		return ret;
	}

	/* reset port reset */
	writel(0x1, csi2->base + CSI_REG_PORT_GPREG_SRST);
	usleep_range(100, 200);
	writel(0x0, csi2->base + CSI_REG_PORT_GPREG_SRST);

	/* enable port clock */
	for (i = 0; i < nports; i++) {
		writel(1, isys->pdata->base + CSI_REG_HUB_DRV_ACCESS_PORT(i));
		writel(1, isys->pdata->base + CSI_REG_HUB_FW_ACCESS_PORT
		       (isys->pdata->ipdata->csi2.fw_access_port_ofs, i));
	}

	/* enable all error related irq */
	writel(mask,
	       csi2->base + CSI_PORT_REG_BASE_IRQ_CSI +
	       CSI_PORT_REG_BASE_IRQ_STATUS_OFFSET);
	writel(mask,
	       csi2->base + CSI_PORT_REG_BASE_IRQ_CSI +
	       CSI_PORT_REG_BASE_IRQ_MASK_OFFSET);
	writel(mask,
	       csi2->base + CSI_PORT_REG_BASE_IRQ_CSI +
	       CSI_PORT_REG_BASE_IRQ_CLEAR_OFFSET);
	writel(mask,
	       csi2->base + CSI_PORT_REG_BASE_IRQ_CSI +
	       CSI_PORT_REG_BASE_IRQ_LEVEL_NOT_PULSE_OFFSET);
	writel(mask,
	       csi2->base + CSI_PORT_REG_BASE_IRQ_CSI +
	       CSI_PORT_REG_BASE_IRQ_ENABLE_OFFSET);

	/*
	 * Using event from firmware instead of irq to handle CSI2 sync event
	 * which can reduce system wakeups. If CSI2 sync irq enabled, we need
	 * disable the firmware CSI2 sync event to avoid duplicate handling.
	 */
	writel(0xffffffff, csi2->base + CSI_PORT_REG_BASE_IRQ_CSI_SYNC +
	       CSI_PORT_REG_BASE_IRQ_STATUS_OFFSET);
	writel(0, csi2->base + CSI_PORT_REG_BASE_IRQ_CSI_SYNC +
	       CSI_PORT_REG_BASE_IRQ_MASK_OFFSET);
	writel(0xffffffff, csi2->base + CSI_PORT_REG_BASE_IRQ_CSI_SYNC +
	       CSI_PORT_REG_BASE_IRQ_CLEAR_OFFSET);
	writel(0, csi2->base + CSI_PORT_REG_BASE_IRQ_CSI_SYNC +
	       CSI_PORT_REG_BASE_IRQ_LEVEL_NOT_PULSE_OFFSET);
	writel(0xffffffff, csi2->base + CSI_PORT_REG_BASE_IRQ_CSI_SYNC +
	       CSI_PORT_REG_BASE_IRQ_ENABLE_OFFSET);

	/* configure to enable FE and PPI2CSI */
	writel(0, csi2->base + CSI_REG_CSI_FE_MODE);
	writel(CSI_SENSOR_INPUT, csi2->base + CSI_REG_CSI_FE_MUX_CTRL);
	writel(CSI_CNTR_SENSOR_LINE_ID | CSI_CNTR_SENSOR_FRAME_ID,
	       csi2->base + CSI_REG_CSI_FE_SYNC_CNTR_SEL);
	writel(FIELD_PREP(PPI_INTF_CONFIG_NOF_ENABLED_DLANES_MASK, nlanes - 1),
	       csi2->base + CSI_REG_PPI2CSI_CONFIG_PPI_INTF);

	writel(1, csi2->base + CSI_REG_PPI2CSI_ENABLE);
	writel(1, csi2->base + CSI_REG_CSI_FE_ENABLE);

	ret = isys->phy_set_power(isys, &cfg, timing, true);
	if (ret)
		dev_err(dev, "csi-%d phy power up failed %d\n", csi2->port,
			ret);

	return ret;
}

static int ipu6_isys_csi2_enable_streams(struct v4l2_subdev *sd,
					 struct v4l2_subdev_state *state,
					 u32 pad, u64 streams_mask)
{
	struct ipu6_isys_subdev *asd = to_ipu6_isys_subdev(sd);
	struct ipu6_isys_csi2 *csi2 = to_ipu6_isys_csi2(asd);
	struct ipu6_isys_csi2_timing timing = { };
	struct v4l2_subdev *remote_sd;
	struct media_pad *remote_pad;
	u64 sink_streams;
	int ret;

	remote_pad = media_pad_remote_pad_first(&sd->entity.pads[CSI2_PAD_SINK]);
	remote_sd = media_entity_to_v4l2_subdev(remote_pad->entity);

	sink_streams =
		v4l2_subdev_state_xlate_streams(state, pad, CSI2_PAD_SINK,
						&streams_mask);

	ret = ipu6_isys_csi2_calc_timing(csi2, &timing, CSI2_ACCINV);
	if (ret)
		return ret;

	ret = ipu6_isys_csi2_set_stream(sd, &timing, csi2->nlanes, true);
	if (ret)
		return ret;

	ret = v4l2_subdev_enable_streams(remote_sd, remote_pad->index,
					 sink_streams);
	if (ret) {
		ipu6_isys_csi2_set_stream(sd, NULL, 0, false);
		return ret;
	}

	return 0;
}

static int ipu6_isys_csi2_disable_streams(struct v4l2_subdev *sd,
					  struct v4l2_subdev_state *state,
					  u32 pad, u64 streams_mask)
{
	struct v4l2_subdev *remote_sd;
	struct media_pad *remote_pad;
	u64 sink_streams;

	sink_streams =
		v4l2_subdev_state_xlate_streams(state, pad, CSI2_PAD_SINK,
						&streams_mask);

	remote_pad = media_pad_remote_pad_first(&sd->entity.pads[CSI2_PAD_SINK]);
	remote_sd = media_entity_to_v4l2_subdev(remote_pad->entity);

	ipu6_isys_csi2_set_stream(sd, NULL, 0, false);

	v4l2_subdev_disable_streams(remote_sd, remote_pad->index, sink_streams);

	return 0;
}

static int ipu6_isys_csi2_set_sel(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  struct v4l2_subdev_selection *sel)
{
	struct ipu6_isys_subdev *asd = to_ipu6_isys_subdev(sd);
	struct device *dev = &asd->isys->adev->auxdev.dev;
	struct v4l2_mbus_framefmt *sink_ffmt;
	struct v4l2_mbus_framefmt *src_ffmt;
	struct v4l2_rect *crop;

	if (sel->pad == CSI2_PAD_SINK || sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	sink_ffmt = v4l2_subdev_state_get_opposite_stream_format(state,
								 sel->pad,
								 sel->stream);
	if (!sink_ffmt)
		return -EINVAL;

	src_ffmt = v4l2_subdev_state_get_format(state, sel->pad, sel->stream);
	if (!src_ffmt)
		return -EINVAL;

	crop = v4l2_subdev_state_get_crop(state, sel->pad, sel->stream);
	if (!crop)
		return -EINVAL;

	/* Only vertical cropping is supported */
	sel->r.left = 0;
	sel->r.width = sink_ffmt->width;
	/* Non-bayer formats can't be single line cropped */
	if (!ipu6_isys_is_bayer_format(sink_ffmt->code))
		sel->r.top &= ~1;
	sel->r.height = clamp(sel->r.height & ~1, IPU6_ISYS_MIN_HEIGHT,
			      sink_ffmt->height - sel->r.top);
	*crop = sel->r;

	/* update source pad format */
	src_ffmt->width = sel->r.width;
	src_ffmt->height = sel->r.height;
	if (ipu6_isys_is_bayer_format(sink_ffmt->code))
		src_ffmt->code = ipu6_isys_convert_bayer_order(sink_ffmt->code,
							       sel->r.left,
							       sel->r.top);
	dev_dbg(dev, "set crop for %s sel: %d,%d,%d,%d code: 0x%x\n",
		sd->name, sel->r.left, sel->r.top, sel->r.width, sel->r.height,
		src_ffmt->code);

	return 0;
}

static int ipu6_isys_csi2_get_sel(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  struct v4l2_subdev_selection *sel)
{
	struct v4l2_mbus_framefmt *sink_ffmt;
	struct v4l2_rect *crop;
	int ret = 0;

	if (sd->entity.pads[sel->pad].flags & MEDIA_PAD_FL_SINK)
		return -EINVAL;

	sink_ffmt = v4l2_subdev_state_get_opposite_stream_format(state,
								 sel->pad,
								 sel->stream);
	if (!sink_ffmt)
		return -EINVAL;

	crop = v4l2_subdev_state_get_crop(state, sel->pad, sel->stream);
	if (!crop)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = sink_ffmt->width;
		sel->r.height = sink_ffmt->height;
		break;
	case V4L2_SEL_TGT_CROP:
		sel->r = *crop;
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static const struct v4l2_subdev_pad_ops csi2_sd_pad_ops = {
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = ipu6_isys_subdev_set_fmt,
	.get_selection = ipu6_isys_csi2_get_sel,
	.set_selection = ipu6_isys_csi2_set_sel,
	.enum_mbus_code = ipu6_isys_subdev_enum_mbus_code,
	.set_routing = ipu6_isys_subdev_set_routing,
	.enable_streams = ipu6_isys_csi2_enable_streams,
	.disable_streams = ipu6_isys_csi2_disable_streams,
};

static const struct v4l2_subdev_ops csi2_sd_ops = {
	.core = &csi2_sd_core_ops,
	.pad = &csi2_sd_pad_ops,
};

static const struct media_entity_operations csi2_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
	.has_pad_interdep = v4l2_subdev_has_pad_interdep,
};

void ipu6_isys_csi2_cleanup(struct ipu6_isys_csi2 *csi2)
{
	if (!csi2->isys)
		return;

	v4l2_device_unregister_subdev(&csi2->asd.sd);
	v4l2_subdev_cleanup(&csi2->asd.sd);
	ipu6_isys_subdev_cleanup(&csi2->asd);
	csi2->isys = NULL;
}

int ipu6_isys_csi2_init(struct ipu6_isys_csi2 *csi2,
			struct ipu6_isys *isys,
			void __iomem *base, unsigned int index)
{
	struct device *dev = &isys->adev->auxdev.dev;
	int ret;

	csi2->isys = isys;
	csi2->base = base;
	csi2->port = index;

	csi2->asd.sd.entity.ops = &csi2_entity_ops;
	csi2->asd.isys = isys;
	ret = ipu6_isys_subdev_init(&csi2->asd, &csi2_sd_ops, 0,
				    NR_OF_CSI2_SINK_PADS, NR_OF_CSI2_SRC_PADS);
	if (ret)
		goto fail;

	csi2->asd.source = IPU6_FW_ISYS_STREAM_SRC_CSI2_PORT0 + index;
	csi2->asd.supported_codes = csi2_supported_codes;
	snprintf(csi2->asd.sd.name, sizeof(csi2->asd.sd.name),
		 IPU6_ISYS_ENTITY_PREFIX " CSI2 %u", index);
	v4l2_set_subdevdata(&csi2->asd.sd, &csi2->asd);
	ret = v4l2_subdev_init_finalize(&csi2->asd.sd);
	if (ret) {
		dev_err(dev, "failed to init v4l2 subdev\n");
		goto fail;
	}

	ret = v4l2_device_register_subdev(&isys->v4l2_dev, &csi2->asd.sd);
	if (ret) {
		dev_err(dev, "failed to register v4l2 subdev\n");
		goto fail;
	}

	return 0;

fail:
	ipu6_isys_csi2_cleanup(csi2);

	return ret;
}

void ipu6_isys_csi2_sof_event_by_stream(struct ipu6_isys_stream *stream)
{
	struct video_device *vdev = stream->asd->sd.devnode;
	struct device *dev = &stream->isys->adev->auxdev.dev;
	struct ipu6_isys_csi2 *csi2 = ipu6_isys_subdev_to_csi2(stream->asd);
	struct v4l2_event ev = {
		.type = V4L2_EVENT_FRAME_SYNC,
	};

	ev.u.frame_sync.frame_sequence = atomic_fetch_inc(&stream->sequence);
	v4l2_event_queue(vdev, &ev);

	dev_dbg(dev, "sof_event::csi2-%i sequence: %i, vc: %d\n",
		csi2->port, ev.u.frame_sync.frame_sequence, stream->vc);
}

void ipu6_isys_csi2_eof_event_by_stream(struct ipu6_isys_stream *stream)
{
	struct device *dev = &stream->isys->adev->auxdev.dev;
	struct ipu6_isys_csi2 *csi2 = ipu6_isys_subdev_to_csi2(stream->asd);
	u32 frame_sequence = atomic_read(&stream->sequence);

	dev_dbg(dev, "eof_event::csi2-%i sequence: %i\n",
		csi2->port, frame_sequence);
}

int ipu6_isys_csi2_get_remote_desc(u32 source_stream,
				   struct ipu6_isys_csi2 *csi2,
				   struct media_entity *source_entity,
				   struct v4l2_mbus_frame_desc_entry *entry)
{
	struct v4l2_mbus_frame_desc_entry *desc_entry = NULL;
	struct device *dev = &csi2->isys->adev->auxdev.dev;
	struct v4l2_mbus_frame_desc desc;
	struct v4l2_subdev *source;
	struct media_pad *pad;
	unsigned int i;
	int ret;

	source = media_entity_to_v4l2_subdev(source_entity);
	if (!source)
		return -EPIPE;

	pad = media_pad_remote_pad_first(&csi2->asd.pad[CSI2_PAD_SINK]);
	if (!pad)
		return -EPIPE;

	ret = v4l2_subdev_call(source, pad, get_frame_desc, pad->index, &desc);
	if (ret)
		return ret;

	if (desc.type != V4L2_MBUS_FRAME_DESC_TYPE_CSI2) {
		dev_err(dev, "Unsupported frame descriptor type\n");
		return -EINVAL;
	}

	for (i = 0; i < desc.num_entries; i++) {
		if (source_stream == desc.entry[i].stream) {
			desc_entry = &desc.entry[i];
			break;
		}
	}

	if (!desc_entry) {
		dev_err(dev, "Failed to find stream %u from remote subdev\n",
			source_stream);
		return -EINVAL;
	}

	if (desc_entry->bus.csi2.vc >= NR_OF_CSI2_VC) {
		dev_err(dev, "invalid vc %d\n", desc_entry->bus.csi2.vc);
		return -EINVAL;
	}

	*entry = *desc_entry;

	return 0;
}
