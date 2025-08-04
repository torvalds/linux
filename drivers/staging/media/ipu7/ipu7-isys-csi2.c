// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 - 2025 Intel Corporation
 */

#include <linux/atomic.h>
#include <linux/bits.h>
#include <linux/bug.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/minmax.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-subdev.h>

#include "ipu7.h"
#include "ipu7-bus.h"
#include "ipu7-isys.h"
#include "ipu7-isys-csi2.h"
#include "ipu7-isys-csi2-regs.h"
#include "ipu7-isys-csi-phy.h"

static const u32 csi2_supported_codes[] = {
	MEDIA_BUS_FMT_Y10_1X10,
	MEDIA_BUS_FMT_RGB565_1X16,
	MEDIA_BUS_FMT_RGB888_1X24,
	MEDIA_BUS_FMT_UYVY8_1X16,
	MEDIA_BUS_FMT_YUYV8_1X16,
	MEDIA_BUS_FMT_YUYV10_1X20,
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
	0,
};

s64 ipu7_isys_csi2_get_link_freq(struct ipu7_isys_csi2 *csi2)
{
	struct media_pad *src_pad;

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
	struct ipu7_isys_subdev *asd = to_ipu7_isys_subdev(sd);
	struct ipu7_isys_csi2 *csi2 = to_ipu7_isys_csi2(asd);
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

static void csi2_irq_enable(struct ipu7_isys_csi2 *csi2)
{
	struct ipu7_device *isp = csi2->isys->adev->isp;
	unsigned int offset, mask;

	/* enable CSI2 legacy error irq */
	offset = IS_IO_CSI2_ERR_LEGACY_IRQ_CTL_BASE(csi2->port);
	mask = IPU7_CSI_RX_ERROR_IRQ_MASK;
	writel(mask, csi2->base + offset + IRQ_CTL_CLEAR);
	writel(mask, csi2->base + offset + IRQ_CTL_MASK);
	writel(mask, csi2->base + offset + IRQ_CTL_ENABLE);

	/* enable CSI2 legacy sync irq */
	offset = IS_IO_CSI2_SYNC_LEGACY_IRQ_CTL_BASE(csi2->port);
	mask = IPU7_CSI_RX_SYNC_IRQ_MASK;
	writel(mask, csi2->base + offset + IRQ_CTL_CLEAR);
	writel(mask, csi2->base + offset + IRQ_CTL_MASK);
	writel(mask, csi2->base + offset + IRQ_CTL_ENABLE);

	mask = IPU7P5_CSI_RX_SYNC_FE_IRQ_MASK;
	if (!is_ipu7(isp->hw_ver)) {
		writel(mask, csi2->base + offset + IRQ1_CTL_CLEAR);
		writel(mask, csi2->base + offset + IRQ1_CTL_MASK);
		writel(mask, csi2->base + offset + IRQ1_CTL_ENABLE);
	}
}

static void csi2_irq_disable(struct ipu7_isys_csi2 *csi2)
{
	struct ipu7_device *isp = csi2->isys->adev->isp;
	unsigned int offset, mask;

	/* disable CSI2 legacy error irq */
	offset = IS_IO_CSI2_ERR_LEGACY_IRQ_CTL_BASE(csi2->port);
	mask = IPU7_CSI_RX_ERROR_IRQ_MASK;
	writel(mask, csi2->base + offset + IRQ_CTL_CLEAR);
	writel(0, csi2->base + offset + IRQ_CTL_MASK);
	writel(0, csi2->base + offset + IRQ_CTL_ENABLE);

	/* disable CSI2 legacy sync irq */
	offset = IS_IO_CSI2_SYNC_LEGACY_IRQ_CTL_BASE(csi2->port);
	mask = IPU7_CSI_RX_SYNC_IRQ_MASK;
	writel(mask, csi2->base + offset + IRQ_CTL_CLEAR);
	writel(0, csi2->base + offset + IRQ_CTL_MASK);
	writel(0, csi2->base + offset + IRQ_CTL_ENABLE);

	if (!is_ipu7(isp->hw_ver)) {
		writel(mask, csi2->base + offset + IRQ1_CTL_CLEAR);
		writel(0, csi2->base + offset + IRQ1_CTL_MASK);
		writel(0, csi2->base + offset + IRQ1_CTL_ENABLE);
	}
}

static void ipu7_isys_csi2_disable_stream(struct ipu7_isys_csi2 *csi2)
{
	struct ipu7_isys *isys = csi2->isys;
	void __iomem *isys_base = isys->pdata->base;

	ipu7_isys_csi_phy_powerdown(csi2);

	writel(0x4, isys_base + IS_IO_GPREGS_BASE + CLK_DIV_FACTOR_APB_CLK);
	csi2_irq_disable(csi2);
}

static int ipu7_isys_csi2_enable_stream(struct ipu7_isys_csi2 *csi2)
{
	struct ipu7_isys *isys = csi2->isys;
	struct device *dev = &isys->adev->auxdev.dev;
	void __iomem *isys_base = isys->pdata->base;
	unsigned int port, nlanes, offset;
	int ret;

	port = csi2->port;
	nlanes = csi2->nlanes;

	offset = IS_IO_GPREGS_BASE;
	writel(0x2, isys_base + offset + CLK_DIV_FACTOR_APB_CLK);
	dev_dbg(dev, "port %u CLK_GATE = 0x%04x DIV_FACTOR_APB_CLK=0x%04x\n",
		port, readl(isys_base + offset + CSI_PORT_CLK_GATE),
		readl(isys_base + offset + CLK_DIV_FACTOR_APB_CLK));
	if (port == 0U && nlanes == 4U && !is_ipu7(isys->adev->isp->hw_ver)) {
		dev_dbg(dev, "CSI port %u in aggregation mode\n", port);
		writel(0x1, isys_base + offset + CSI_PORTAB_AGGREGATION);
	}

	/* input is coming from CSI receiver (sensor) */
	offset = IS_IO_CSI2_ADPL_PORT_BASE(port);
	writel(CSI_SENSOR_INPUT, isys_base + offset + CSI2_ADPL_INPUT_MODE);
	writel(1, isys_base + offset + CSI2_ADPL_CSI_RX_ERR_IRQ_CLEAR_EN);

	ret = ipu7_isys_csi_phy_powerup(csi2);
	if (ret) {
		dev_err(dev, "CSI-%d PHY power up failed %d\n", port, ret);
		return ret;
	}

	csi2_irq_enable(csi2);

	return 0;
}

static int ipu7_isys_csi2_set_sel(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  struct v4l2_subdev_selection *sel)
{
	struct ipu7_isys_subdev *asd = to_ipu7_isys_subdev(sd);
	struct device *dev = &asd->isys->adev->auxdev.dev;
	struct v4l2_mbus_framefmt *sink_ffmt;
	struct v4l2_mbus_framefmt *src_ffmt;
	struct v4l2_rect *crop;

	if (sel->pad == IPU7_CSI2_PAD_SINK || sel->target != V4L2_SEL_TGT_CROP)
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
	if (!ipu7_isys_is_bayer_format(sink_ffmt->code))
		sel->r.top &= ~1U;
	sel->r.height = clamp(sel->r.height & ~1U, IPU_ISYS_MIN_HEIGHT,
			      sink_ffmt->height - sel->r.top);
	*crop = sel->r;

	/* update source pad format */
	src_ffmt->width = sel->r.width;
	src_ffmt->height = sel->r.height;
	if (ipu7_isys_is_bayer_format(sink_ffmt->code))
		src_ffmt->code = ipu7_isys_convert_bayer_order(sink_ffmt->code,
							       sel->r.left,
							       sel->r.top);
	dev_dbg(dev, "set crop for %s sel: %d,%d,%d,%d code: 0x%x\n",
		sd->name, sel->r.left, sel->r.top, sel->r.width, sel->r.height,
		src_ffmt->code);

	return 0;
}

static int ipu7_isys_csi2_get_sel(struct v4l2_subdev *sd,
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

/*
 * Maximum stream ID is 63 for now, as we use u64 bitmask to represent a set
 * of streams.
 */
#define CSI2_SUBDEV_MAX_STREAM_ID 63

static int ipu7_isys_csi2_enable_streams(struct v4l2_subdev *sd,
					 struct v4l2_subdev_state *state,
					 u32 pad, u64 streams_mask)
{
	struct ipu7_isys_subdev *asd = to_ipu7_isys_subdev(sd);
	struct ipu7_isys_csi2 *csi2 = to_ipu7_isys_csi2(asd);
	struct v4l2_subdev *r_sd;
	struct media_pad *rp;
	u32 sink_pad, sink_stream;
	int ret, i;

	if (!csi2->stream_count) {
		dev_dbg(&csi2->isys->adev->auxdev.dev,
			"stream on CSI2-%u with %u lanes\n", csi2->port,
			csi2->nlanes);
		ret = ipu7_isys_csi2_enable_stream(csi2);
		if (ret)
			return ret;
	}

	for (i = 0; i <= CSI2_SUBDEV_MAX_STREAM_ID; i++) {
		if (streams_mask & BIT_ULL(i))
			break;
	}

	ret = v4l2_subdev_routing_find_opposite_end(&state->routing, pad, i,
						    &sink_pad, &sink_stream);
	if (ret)
		return ret;

	rp = media_pad_remote_pad_first(&sd->entity.pads[IPU7_CSI2_PAD_SINK]);
	r_sd = media_entity_to_v4l2_subdev(rp->entity);

	ret = v4l2_subdev_enable_streams(r_sd, rp->index,
					 BIT_ULL(sink_stream));
	if (!ret) {
		csi2->stream_count++;
		return 0;
	}

	if (!csi2->stream_count)
		ipu7_isys_csi2_disable_stream(csi2);

	return ret;
}

static int ipu7_isys_csi2_disable_streams(struct v4l2_subdev *sd,
					  struct v4l2_subdev_state *state,
					  u32 pad, u64 streams_mask)
{
	struct ipu7_isys_subdev *asd = to_ipu7_isys_subdev(sd);
	struct ipu7_isys_csi2 *csi2 = to_ipu7_isys_csi2(asd);
	struct v4l2_subdev *r_sd;
	struct media_pad *rp;
	u32 sink_pad, sink_stream;
	int ret, i;

	for (i = 0; i <= CSI2_SUBDEV_MAX_STREAM_ID; i++) {
		if (streams_mask & BIT_ULL(i))
			break;
	}

	ret = v4l2_subdev_routing_find_opposite_end(&state->routing, pad, i,
						    &sink_pad, &sink_stream);
	if (ret)
		return ret;

	rp = media_pad_remote_pad_first(&sd->entity.pads[IPU7_CSI2_PAD_SINK]);
	r_sd = media_entity_to_v4l2_subdev(rp->entity);

	v4l2_subdev_disable_streams(r_sd, rp->index, BIT_ULL(sink_stream));

	if (--csi2->stream_count)
		return 0;

	dev_dbg(&csi2->isys->adev->auxdev.dev,
		"stream off CSI2-%u with %u lanes\n", csi2->port, csi2->nlanes);

	ipu7_isys_csi2_disable_stream(csi2);

	return 0;
}

static const struct v4l2_subdev_pad_ops csi2_sd_pad_ops = {
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = ipu7_isys_subdev_set_fmt,
	.get_selection = ipu7_isys_csi2_get_sel,
	.set_selection = ipu7_isys_csi2_set_sel,
	.enum_mbus_code = ipu7_isys_subdev_enum_mbus_code,
	.enable_streams = ipu7_isys_csi2_enable_streams,
	.disable_streams = ipu7_isys_csi2_disable_streams,
	.set_routing = ipu7_isys_subdev_set_routing,
};

static const struct v4l2_subdev_ops csi2_sd_ops = {
	.core = &csi2_sd_core_ops,
	.pad = &csi2_sd_pad_ops,
};

static const struct media_entity_operations csi2_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
	.has_pad_interdep = v4l2_subdev_has_pad_interdep,
};

void ipu7_isys_csi2_cleanup(struct ipu7_isys_csi2 *csi2)
{
	if (!csi2->isys)
		return;

	v4l2_device_unregister_subdev(&csi2->asd.sd);
	v4l2_subdev_cleanup(&csi2->asd.sd);
	ipu7_isys_subdev_cleanup(&csi2->asd);
	csi2->isys = NULL;
}

int ipu7_isys_csi2_init(struct ipu7_isys_csi2 *csi2,
			struct ipu7_isys *isys,
			void __iomem *base, unsigned int index)
{
	struct device *dev = &isys->adev->auxdev.dev;
	int ret;

	csi2->isys = isys;
	csi2->base = base;
	csi2->port = index;

	if (!is_ipu7(isys->adev->isp->hw_ver))
		csi2->legacy_irq_mask = 0x7U << (index * 3U);
	else
		csi2->legacy_irq_mask = 0x3U << (index * 2U);

	dev_dbg(dev, "csi-%d legacy irq mask = 0x%x\n", index,
		csi2->legacy_irq_mask);

	csi2->asd.sd.entity.ops = &csi2_entity_ops;
	csi2->asd.isys = isys;

	ret = ipu7_isys_subdev_init(&csi2->asd, &csi2_sd_ops, 0,
				    IPU7_NR_OF_CSI2_SINK_PADS,
				    IPU7_NR_OF_CSI2_SRC_PADS);
	if (ret)
		return ret;

	csi2->asd.source = (int)index;
	csi2->asd.supported_codes = csi2_supported_codes;
	snprintf(csi2->asd.sd.name, sizeof(csi2->asd.sd.name),
		 IPU_ISYS_ENTITY_PREFIX " CSI2 %u", index);
	v4l2_set_subdevdata(&csi2->asd.sd, &csi2->asd);

	ret = v4l2_subdev_init_finalize(&csi2->asd.sd);
	if (ret) {
		dev_err(dev, "failed to init v4l2 subdev (%d)\n", ret);
		goto isys_subdev_cleanup;
	}

	ret = v4l2_device_register_subdev(&isys->v4l2_dev, &csi2->asd.sd);
	if (ret) {
		dev_err(dev, "failed to register v4l2 subdev (%d)\n", ret);
		goto v4l2_subdev_cleanup;
	}

	return 0;

v4l2_subdev_cleanup:
	v4l2_subdev_cleanup(&csi2->asd.sd);
isys_subdev_cleanup:
	ipu7_isys_subdev_cleanup(&csi2->asd);

	return ret;
}

void ipu7_isys_csi2_sof_event_by_stream(struct ipu7_isys_stream *stream)
{
	struct ipu7_isys_csi2 *csi2 = ipu7_isys_subdev_to_csi2(stream->asd);
	struct device *dev = &stream->isys->adev->auxdev.dev;
	struct video_device *vdev = csi2->asd.sd.devnode;
	struct v4l2_event ev = {
		.type = V4L2_EVENT_FRAME_SYNC,
	};

	ev.id = stream->vc;
	ev.u.frame_sync.frame_sequence = atomic_fetch_inc(&stream->sequence);
	v4l2_event_queue(vdev, &ev);

	dev_dbg(dev, "sof_event::csi2-%i sequence: %i, vc: %d\n",
		csi2->port, ev.u.frame_sync.frame_sequence, stream->vc);
}

void ipu7_isys_csi2_eof_event_by_stream(struct ipu7_isys_stream *stream)
{
	struct ipu7_isys_csi2 *csi2 = ipu7_isys_subdev_to_csi2(stream->asd);
	struct device *dev = &stream->isys->adev->auxdev.dev;
	u32 frame_sequence = atomic_read(&stream->sequence);

	dev_dbg(dev, "eof_event::csi2-%i sequence: %i\n",
		csi2->port, frame_sequence);
}

int ipu7_isys_csi2_get_remote_desc(u32 source_stream,
				   struct ipu7_isys_csi2 *csi2,
				   struct media_entity *source_entity,
				   struct v4l2_mbus_frame_desc_entry *entry,
				   int *nr_queues)
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

	pad = media_pad_remote_pad_first(&csi2->asd.pad[IPU7_CSI2_PAD_SINK]);
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

	if (desc_entry->bus.csi2.vc >= IPU7_NR_OF_CSI2_VC) {
		dev_err(dev, "invalid vc %d\n", desc_entry->bus.csi2.vc);
		return -EINVAL;
	}

	*entry = *desc_entry;

	for (i = 0; i < desc.num_entries; i++) {
		if (desc_entry->bus.csi2.vc == desc.entry[i].bus.csi2.vc)
			(*nr_queues)++;
	}

	return 0;
}
