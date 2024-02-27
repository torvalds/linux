// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Rockchip ISP1 Driver - CSI-2 Receiver
 *
 * Copyright (C) 2019 Collabora, Ltd.
 * Copyright (C) 2022 Ideas on Board
 *
 * Based on Rockchip ISP1 driver by Rockchip Electronics Co., Ltd.
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/lockdep.h>
#include <linux/phy/phy.h>
#include <linux/phy/phy-mipi-dphy.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-fwnode.h>

#include "rkisp1-common.h"
#include "rkisp1-csi.h"

#define RKISP1_CSI_DEV_NAME	RKISP1_DRIVER_NAME "_csi"

#define RKISP1_CSI_DEF_FMT	MEDIA_BUS_FMT_SRGGB10_1X10

static inline struct rkisp1_csi *to_rkisp1_csi(struct v4l2_subdev *sd)
{
	return container_of(sd, struct rkisp1_csi, sd);
}

int rkisp1_csi_link_sensor(struct rkisp1_device *rkisp1, struct v4l2_subdev *sd,
			   struct rkisp1_sensor_async *s_asd,
			   unsigned int source_pad)
{
	struct rkisp1_csi *csi = &rkisp1->csi;
	int ret;

	s_asd->pixel_rate_ctrl = v4l2_ctrl_find(sd->ctrl_handler,
						V4L2_CID_PIXEL_RATE);
	if (!s_asd->pixel_rate_ctrl) {
		dev_err(rkisp1->dev, "No pixel rate control in subdev %s\n",
			sd->name);
		return -EINVAL;
	}

	/* Create the link from the sensor to the CSI receiver. */
	ret = media_create_pad_link(&sd->entity, source_pad,
				    &csi->sd.entity, RKISP1_CSI_PAD_SINK,
				    !s_asd->index ? MEDIA_LNK_FL_ENABLED : 0);
	if (ret) {
		dev_err(csi->rkisp1->dev, "failed to link src pad of %s\n",
			sd->name);
		return ret;
	}

	return 0;
}

static int rkisp1_csi_config(struct rkisp1_csi *csi,
			     const struct rkisp1_sensor_async *sensor,
			     const struct rkisp1_mbus_info *format)
{
	struct rkisp1_device *rkisp1 = csi->rkisp1;
	unsigned int lanes = sensor->lanes;
	u32 mipi_ctrl;

	if (lanes < 1 || lanes > 4)
		return -EINVAL;

	mipi_ctrl = RKISP1_CIF_MIPI_CTRL_NUM_LANES(lanes - 1) |
		    RKISP1_CIF_MIPI_CTRL_SHUTDOWNLANES(0xf) |
		    RKISP1_CIF_MIPI_CTRL_ERR_SOT_SYNC_HS_SKIP |
		    RKISP1_CIF_MIPI_CTRL_CLOCKLANE_ENA;

	rkisp1_write(rkisp1, RKISP1_CIF_MIPI_CTRL, mipi_ctrl);

	/* V12 could also use a newer csi2-host, but we don't want that yet */
	if (rkisp1->info->isp_ver == RKISP1_V12)
		rkisp1_write(rkisp1, RKISP1_CIF_ISP_CSI0_CTRL0, 0);

	/* Configure Data Type and Virtual Channel */
	rkisp1_write(rkisp1, RKISP1_CIF_MIPI_IMG_DATA_SEL,
		     RKISP1_CIF_MIPI_DATA_SEL_DT(format->mipi_dt) |
		     RKISP1_CIF_MIPI_DATA_SEL_VC(0));

	/* Clear MIPI interrupts */
	rkisp1_write(rkisp1, RKISP1_CIF_MIPI_ICR, ~0);

	/*
	 * Disable RKISP1_CIF_MIPI_ERR_DPHY interrupt here temporary for
	 * isp bus may be dead when switch isp.
	 */
	rkisp1_write(rkisp1, RKISP1_CIF_MIPI_IMSC,
		     RKISP1_CIF_MIPI_FRAME_END | RKISP1_CIF_MIPI_ERR_CSI |
		     RKISP1_CIF_MIPI_ERR_DPHY |
		     RKISP1_CIF_MIPI_SYNC_FIFO_OVFLW(0x03) |
		     RKISP1_CIF_MIPI_ADD_DATA_OVFLW);

	dev_dbg(rkisp1->dev, "\n  MIPI_CTRL 0x%08x\n"
		"  MIPI_IMG_DATA_SEL 0x%08x\n"
		"  MIPI_STATUS 0x%08x\n"
		"  MIPI_IMSC 0x%08x\n",
		rkisp1_read(rkisp1, RKISP1_CIF_MIPI_CTRL),
		rkisp1_read(rkisp1, RKISP1_CIF_MIPI_IMG_DATA_SEL),
		rkisp1_read(rkisp1, RKISP1_CIF_MIPI_STATUS),
		rkisp1_read(rkisp1, RKISP1_CIF_MIPI_IMSC));

	return 0;
}

static void rkisp1_csi_enable(struct rkisp1_csi *csi)
{
	struct rkisp1_device *rkisp1 = csi->rkisp1;
	u32 val;

	val = rkisp1_read(rkisp1, RKISP1_CIF_MIPI_CTRL);
	rkisp1_write(rkisp1, RKISP1_CIF_MIPI_CTRL,
		     val | RKISP1_CIF_MIPI_CTRL_OUTPUT_ENA);
}

static void rkisp1_csi_disable(struct rkisp1_csi *csi)
{
	struct rkisp1_device *rkisp1 = csi->rkisp1;
	u32 val;

	/* Mask MIPI interrupts. */
	rkisp1_write(rkisp1, RKISP1_CIF_MIPI_IMSC, 0);

	/* Flush posted writes */
	rkisp1_read(rkisp1, RKISP1_CIF_MIPI_IMSC);

	/*
	 * Wait until the IRQ handler has ended. The IRQ handler may get called
	 * even after this, but it will return immediately as the MIPI
	 * interrupts have been masked.
	 */
	synchronize_irq(rkisp1->irqs[RKISP1_IRQ_MIPI]);

	/* Clear MIPI interrupt status */
	rkisp1_write(rkisp1, RKISP1_CIF_MIPI_ICR, ~0);

	val = rkisp1_read(rkisp1, RKISP1_CIF_MIPI_CTRL);
	rkisp1_write(rkisp1, RKISP1_CIF_MIPI_CTRL,
		     val & (~RKISP1_CIF_MIPI_CTRL_OUTPUT_ENA));
}

static int rkisp1_csi_start(struct rkisp1_csi *csi,
			    const struct rkisp1_sensor_async *sensor,
			    const struct rkisp1_mbus_info *format)
{
	struct rkisp1_device *rkisp1 = csi->rkisp1;
	union phy_configure_opts opts;
	struct phy_configure_opts_mipi_dphy *cfg = &opts.mipi_dphy;
	s64 pixel_clock;
	int ret;

	ret = rkisp1_csi_config(csi, sensor, format);
	if (ret)
		return ret;

	pixel_clock = v4l2_ctrl_g_ctrl_int64(sensor->pixel_rate_ctrl);
	if (!pixel_clock) {
		dev_err(rkisp1->dev, "Invalid pixel rate value\n");
		return -EINVAL;
	}

	phy_mipi_dphy_get_default_config(pixel_clock, format->bus_width,
					 sensor->lanes, cfg);
	phy_set_mode(csi->dphy, PHY_MODE_MIPI_DPHY);
	phy_configure(csi->dphy, &opts);
	phy_power_on(csi->dphy);

	rkisp1_csi_enable(csi);

	/*
	 * CIF spec says to wait for sufficient time after enabling
	 * the MIPI interface and before starting the sensor output.
	 */
	usleep_range(1000, 1200);

	return 0;
}

static void rkisp1_csi_stop(struct rkisp1_csi *csi)
{
	rkisp1_csi_disable(csi);

	phy_power_off(csi->dphy);
}

irqreturn_t rkisp1_csi_isr(int irq, void *ctx)
{
	struct device *dev = ctx;
	struct rkisp1_device *rkisp1 = dev_get_drvdata(dev);
	u32 val, status;

	if (!rkisp1->irqs_enabled)
		return IRQ_NONE;

	status = rkisp1_read(rkisp1, RKISP1_CIF_MIPI_MIS);
	if (!status)
		return IRQ_NONE;

	rkisp1_write(rkisp1, RKISP1_CIF_MIPI_ICR, status);

	/*
	 * Disable DPHY errctrl interrupt, because this dphy
	 * erctrl signal is asserted until the next changes
	 * of line state. This time is may be too long and cpu
	 * is hold in this interrupt.
	 */
	if (status & RKISP1_CIF_MIPI_ERR_CTRL(0x0f)) {
		val = rkisp1_read(rkisp1, RKISP1_CIF_MIPI_IMSC);
		rkisp1_write(rkisp1, RKISP1_CIF_MIPI_IMSC,
			     val & ~RKISP1_CIF_MIPI_ERR_CTRL(0x0f));
		rkisp1->csi.is_dphy_errctrl_disabled = true;
	}

	/*
	 * Enable DPHY errctrl interrupt again, if mipi have receive
	 * the whole frame without any error.
	 */
	if (status == RKISP1_CIF_MIPI_FRAME_END) {
		/*
		 * Enable DPHY errctrl interrupt again, if mipi have receive
		 * the whole frame without any error.
		 */
		if (rkisp1->csi.is_dphy_errctrl_disabled) {
			val = rkisp1_read(rkisp1, RKISP1_CIF_MIPI_IMSC);
			val |= RKISP1_CIF_MIPI_ERR_CTRL(0x0f);
			rkisp1_write(rkisp1, RKISP1_CIF_MIPI_IMSC, val);
			rkisp1->csi.is_dphy_errctrl_disabled = false;
		}
	} else {
		rkisp1->debug.mipi_error++;
	}

	return IRQ_HANDLED;
}

/* ----------------------------------------------------------------------------
 * Subdev pad operations
 */

static int rkisp1_csi_enum_mbus_code(struct v4l2_subdev *sd,
				     struct v4l2_subdev_state *sd_state,
				     struct v4l2_subdev_mbus_code_enum *code)
{
	unsigned int i;
	int pos = 0;

	if (code->pad == RKISP1_CSI_PAD_SRC) {
		const struct v4l2_mbus_framefmt *sink_fmt;

		if (code->index)
			return -EINVAL;

		sink_fmt = v4l2_subdev_state_get_format(sd_state,
							RKISP1_CSI_PAD_SINK);
		code->code = sink_fmt->code;

		return 0;
	}

	for (i = 0; ; i++) {
		const struct rkisp1_mbus_info *fmt =
			rkisp1_mbus_info_get_by_index(i);

		if (!fmt)
			return -EINVAL;

		if (!(fmt->direction & RKISP1_ISP_SD_SINK))
			continue;

		if (code->index == pos) {
			code->code = fmt->mbus_code;
			return 0;
		}

		pos++;
	}

	return -EINVAL;
}

static int rkisp1_csi_init_state(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state)
{
	struct v4l2_mbus_framefmt *sink_fmt, *src_fmt;

	sink_fmt = v4l2_subdev_state_get_format(sd_state, RKISP1_CSI_PAD_SINK);
	src_fmt = v4l2_subdev_state_get_format(sd_state, RKISP1_CSI_PAD_SRC);

	sink_fmt->width = RKISP1_DEFAULT_WIDTH;
	sink_fmt->height = RKISP1_DEFAULT_HEIGHT;
	sink_fmt->field = V4L2_FIELD_NONE;
	sink_fmt->code = RKISP1_CSI_DEF_FMT;

	*src_fmt = *sink_fmt;

	return 0;
}

static int rkisp1_csi_set_fmt(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *sd_state,
			      struct v4l2_subdev_format *fmt)
{
	const struct rkisp1_mbus_info *mbus_info;
	struct v4l2_mbus_framefmt *sink_fmt, *src_fmt;

	/* The format on the source pad always matches the sink pad. */
	if (fmt->pad == RKISP1_CSI_PAD_SRC)
		return v4l2_subdev_get_fmt(sd, sd_state, fmt);

	sink_fmt = v4l2_subdev_state_get_format(sd_state, RKISP1_CSI_PAD_SINK);

	sink_fmt->code = fmt->format.code;

	mbus_info = rkisp1_mbus_info_get_by_code(sink_fmt->code);
	if (!mbus_info || !(mbus_info->direction & RKISP1_ISP_SD_SINK)) {
		sink_fmt->code = RKISP1_CSI_DEF_FMT;
		mbus_info = rkisp1_mbus_info_get_by_code(sink_fmt->code);
	}

	sink_fmt->width = clamp_t(u32, fmt->format.width,
				  RKISP1_ISP_MIN_WIDTH,
				  RKISP1_ISP_MAX_WIDTH);
	sink_fmt->height = clamp_t(u32, fmt->format.height,
				   RKISP1_ISP_MIN_HEIGHT,
				   RKISP1_ISP_MAX_HEIGHT);

	fmt->format = *sink_fmt;

	/* Propagate the format to the source pad. */
	src_fmt = v4l2_subdev_state_get_format(sd_state, RKISP1_CSI_PAD_SRC);
	*src_fmt = *sink_fmt;

	return 0;
}

/* ----------------------------------------------------------------------------
 * Subdev video operations
 */

static int rkisp1_csi_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct rkisp1_csi *csi = to_rkisp1_csi(sd);
	struct rkisp1_device *rkisp1 = csi->rkisp1;
	const struct v4l2_mbus_framefmt *sink_fmt;
	const struct rkisp1_mbus_info *format;
	struct rkisp1_sensor_async *source_asd;
	struct v4l2_async_connection *asc;
	struct v4l2_subdev_state *sd_state;
	struct media_pad *source_pad;
	struct v4l2_subdev *source;
	int ret;

	if (!enable) {
		v4l2_subdev_call(csi->source, video, s_stream, false);

		rkisp1_csi_stop(csi);

		return 0;
	}

	source_pad = media_entity_remote_source_pad_unique(&sd->entity);
	if (IS_ERR(source_pad)) {
		dev_dbg(rkisp1->dev, "Failed to get source for CSI: %ld\n",
			PTR_ERR(source_pad));
		return -EPIPE;
	}

	source = media_entity_to_v4l2_subdev(source_pad->entity);
	if (!source) {
		/* This should really not happen, so is not worth a message. */
		return -EPIPE;
	}

	asc = v4l2_async_connection_unique(source);
	if (!asc)
		return -EPIPE;

	source_asd = container_of(asc, struct rkisp1_sensor_async, asd);
	if (source_asd->mbus_type != V4L2_MBUS_CSI2_DPHY)
		return -EINVAL;

	sd_state = v4l2_subdev_lock_and_get_active_state(sd);
	sink_fmt = v4l2_subdev_state_get_format(sd_state, RKISP1_CSI_PAD_SINK);
	format = rkisp1_mbus_info_get_by_code(sink_fmt->code);
	v4l2_subdev_unlock_state(sd_state);

	ret = rkisp1_csi_start(csi, source_asd, format);
	if (ret)
		return ret;

	ret = v4l2_subdev_call(source, video, s_stream, true);
	if (ret) {
		rkisp1_csi_stop(csi);
		return ret;
	}

	csi->source = source;

	return 0;
}

/* ----------------------------------------------------------------------------
 * Registration
 */

static const struct media_entity_operations rkisp1_csi_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static const struct v4l2_subdev_video_ops rkisp1_csi_video_ops = {
	.s_stream = rkisp1_csi_s_stream,
};

static const struct v4l2_subdev_pad_ops rkisp1_csi_pad_ops = {
	.enum_mbus_code = rkisp1_csi_enum_mbus_code,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = rkisp1_csi_set_fmt,
};

static const struct v4l2_subdev_ops rkisp1_csi_ops = {
	.video = &rkisp1_csi_video_ops,
	.pad = &rkisp1_csi_pad_ops,
};

static const struct v4l2_subdev_internal_ops rkisp1_csi_internal_ops = {
	.init_state = rkisp1_csi_init_state,
};

int rkisp1_csi_register(struct rkisp1_device *rkisp1)
{
	struct rkisp1_csi *csi = &rkisp1->csi;
	struct media_pad *pads;
	struct v4l2_subdev *sd;
	int ret;

	csi->rkisp1 = rkisp1;

	sd = &csi->sd;
	v4l2_subdev_init(sd, &rkisp1_csi_ops);
	sd->internal_ops = &rkisp1_csi_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	sd->entity.ops = &rkisp1_csi_media_ops;
	sd->entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;
	sd->owner = THIS_MODULE;
	strscpy(sd->name, RKISP1_CSI_DEV_NAME, sizeof(sd->name));

	pads = csi->pads;
	pads[RKISP1_CSI_PAD_SINK].flags = MEDIA_PAD_FL_SINK |
					  MEDIA_PAD_FL_MUST_CONNECT;
	pads[RKISP1_CSI_PAD_SRC].flags = MEDIA_PAD_FL_SOURCE |
					 MEDIA_PAD_FL_MUST_CONNECT;

	ret = media_entity_pads_init(&sd->entity, RKISP1_CSI_PAD_NUM, pads);
	if (ret)
		goto err_entity_cleanup;

	ret = v4l2_subdev_init_finalize(sd);
	if (ret)
		goto err_entity_cleanup;

	ret = v4l2_device_register_subdev(&csi->rkisp1->v4l2_dev, sd);
	if (ret) {
		dev_err(sd->dev, "Failed to register csi receiver subdev\n");
		goto err_subdev_cleanup;
	}

	return 0;

err_subdev_cleanup:
	v4l2_subdev_cleanup(sd);
err_entity_cleanup:
	media_entity_cleanup(&sd->entity);
	csi->rkisp1 = NULL;
	return ret;
}

void rkisp1_csi_unregister(struct rkisp1_device *rkisp1)
{
	struct rkisp1_csi *csi = &rkisp1->csi;

	if (!csi->rkisp1)
		return;

	v4l2_device_unregister_subdev(&csi->sd);
	v4l2_subdev_cleanup(&csi->sd);
	media_entity_cleanup(&csi->sd.entity);
}

int rkisp1_csi_init(struct rkisp1_device *rkisp1)
{
	struct rkisp1_csi *csi = &rkisp1->csi;

	csi->rkisp1 = rkisp1;

	csi->dphy = devm_phy_get(rkisp1->dev, "dphy");
	if (IS_ERR(csi->dphy))
		return dev_err_probe(rkisp1->dev, PTR_ERR(csi->dphy),
				     "Couldn't get the MIPI D-PHY\n");

	phy_init(csi->dphy);

	return 0;
}

void rkisp1_csi_cleanup(struct rkisp1_device *rkisp1)
{
	struct rkisp1_csi *csi = &rkisp1->csi;

	phy_exit(csi->dphy);
}
