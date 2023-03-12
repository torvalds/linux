// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2020 Kévin L'hôpital <kevin.lhopital@bootlin.com>
 * Copyright 2020-2022 Bootlin
 * Author: Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <media/mipi-csi2.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>

#include "sun8i_a83t_dphy.h"
#include "sun8i_a83t_mipi_csi2.h"
#include "sun8i_a83t_mipi_csi2_reg.h"

/* Format */

static const struct sun8i_a83t_mipi_csi2_format
sun8i_a83t_mipi_csi2_formats[] = {
	{
		.mbus_code	= MEDIA_BUS_FMT_SBGGR8_1X8,
		.data_type	= MIPI_CSI2_DT_RAW8,
		.bpp		= 8,
	},
	{
		.mbus_code	= MEDIA_BUS_FMT_SGBRG8_1X8,
		.data_type	= MIPI_CSI2_DT_RAW8,
		.bpp		= 8,
	},
	{
		.mbus_code	= MEDIA_BUS_FMT_SGRBG8_1X8,
		.data_type	= MIPI_CSI2_DT_RAW8,
		.bpp		= 8,
	},
	{
		.mbus_code	= MEDIA_BUS_FMT_SRGGB8_1X8,
		.data_type	= MIPI_CSI2_DT_RAW8,
		.bpp		= 8,
	},
	{
		.mbus_code	= MEDIA_BUS_FMT_SBGGR10_1X10,
		.data_type	= MIPI_CSI2_DT_RAW10,
		.bpp		= 10,
	},
	{
		.mbus_code	= MEDIA_BUS_FMT_SGBRG10_1X10,
		.data_type	= MIPI_CSI2_DT_RAW10,
		.bpp		= 10,
	},
	{
		.mbus_code	= MEDIA_BUS_FMT_SGRBG10_1X10,
		.data_type	= MIPI_CSI2_DT_RAW10,
		.bpp		= 10,
	},
	{
		.mbus_code	= MEDIA_BUS_FMT_SRGGB10_1X10,
		.data_type	= MIPI_CSI2_DT_RAW10,
		.bpp		= 10,
	},
};

static const struct sun8i_a83t_mipi_csi2_format *
sun8i_a83t_mipi_csi2_format_find(u32 mbus_code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(sun8i_a83t_mipi_csi2_formats); i++)
		if (sun8i_a83t_mipi_csi2_formats[i].mbus_code == mbus_code)
			return &sun8i_a83t_mipi_csi2_formats[i];

	return NULL;
}

/* Controller */

static void
sun8i_a83t_mipi_csi2_init(struct sun8i_a83t_mipi_csi2_device *csi2_dev)
{
	struct regmap *regmap = csi2_dev->regmap;

	/*
	 * The Allwinner BSP sets various magic values on a bunch of registers.
	 * This is apparently a necessary initialization process that will cause
	 * the capture to fail with unsolicited interrupts hitting if skipped.
	 *
	 * Most of the registers are set to proper values later, except for the
	 * two reserved registers. They are said to hold a "hardware lock"
	 * value, without more information available.
	 */

	regmap_write(regmap, SUN8I_A83T_MIPI_CSI2_CTRL_REG, 0);
	regmap_write(regmap, SUN8I_A83T_MIPI_CSI2_CTRL_REG,
		     SUN8I_A83T_MIPI_CSI2_CTRL_INIT_VALUE);

	regmap_write(regmap, SUN8I_A83T_MIPI_CSI2_RX_PKT_NUM_REG, 0);
	regmap_write(regmap, SUN8I_A83T_MIPI_CSI2_RX_PKT_NUM_REG,
		     SUN8I_A83T_MIPI_CSI2_RX_PKT_NUM_INIT_VALUE);

	regmap_write(regmap, SUN8I_A83T_DPHY_CTRL_REG, 0);
	regmap_write(regmap, SUN8I_A83T_DPHY_CTRL_REG,
		     SUN8I_A83T_DPHY_CTRL_INIT_VALUE);

	regmap_write(regmap, SUN8I_A83T_MIPI_CSI2_RSVD1_REG, 0);
	regmap_write(regmap, SUN8I_A83T_MIPI_CSI2_RSVD1_REG,
		     SUN8I_A83T_MIPI_CSI2_RSVD1_HW_LOCK_VALUE);

	regmap_write(regmap, SUN8I_A83T_MIPI_CSI2_RSVD2_REG, 0);
	regmap_write(regmap, SUN8I_A83T_MIPI_CSI2_RSVD2_REG,
		     SUN8I_A83T_MIPI_CSI2_RSVD2_HW_LOCK_VALUE);

	regmap_write(regmap, SUN8I_A83T_MIPI_CSI2_CFG_REG, 0);
	regmap_write(regmap, SUN8I_A83T_MIPI_CSI2_CFG_REG,
		     SUN8I_A83T_MIPI_CSI2_CFG_INIT_VALUE);
}

static void
sun8i_a83t_mipi_csi2_enable(struct sun8i_a83t_mipi_csi2_device *csi2_dev)
{
	struct regmap *regmap = csi2_dev->regmap;

	regmap_update_bits(regmap, SUN8I_A83T_MIPI_CSI2_CFG_REG,
			   SUN8I_A83T_MIPI_CSI2_CFG_SYNC_EN,
			   SUN8I_A83T_MIPI_CSI2_CFG_SYNC_EN);
}

static void
sun8i_a83t_mipi_csi2_disable(struct sun8i_a83t_mipi_csi2_device *csi2_dev)
{
	struct regmap *regmap = csi2_dev->regmap;

	regmap_update_bits(regmap, SUN8I_A83T_MIPI_CSI2_CFG_REG,
			   SUN8I_A83T_MIPI_CSI2_CFG_SYNC_EN, 0);

	regmap_write(regmap, SUN8I_A83T_MIPI_CSI2_CTRL_REG, 0);
}

static void
sun8i_a83t_mipi_csi2_configure(struct sun8i_a83t_mipi_csi2_device *csi2_dev)
{
	struct regmap *regmap = csi2_dev->regmap;
	unsigned int lanes_count =
		csi2_dev->bridge.endpoint.bus.mipi_csi2.num_data_lanes;
	struct v4l2_mbus_framefmt *mbus_format = &csi2_dev->bridge.mbus_format;
	const struct sun8i_a83t_mipi_csi2_format *format;
	struct device *dev = csi2_dev->dev;
	u32 version = 0;

	format = sun8i_a83t_mipi_csi2_format_find(mbus_format->code);
	if (WARN_ON(!format))
		return;

	regmap_write(regmap, SUN8I_A83T_MIPI_CSI2_CTRL_REG,
		     SUN8I_A83T_MIPI_CSI2_CTRL_RESET_N);

	regmap_read(regmap, SUN8I_A83T_MIPI_CSI2_VERSION_REG, &version);

	dev_dbg(dev, "A83T MIPI CSI-2 version: %04x\n", version);

	regmap_write(regmap, SUN8I_A83T_MIPI_CSI2_CFG_REG,
		     SUN8I_A83T_MIPI_CSI2_CFG_UNPKT_EN |
		     SUN8I_A83T_MIPI_CSI2_CFG_SYNC_DLY_CYCLE(8) |
		     SUN8I_A83T_MIPI_CSI2_CFG_N_CHANNEL(1) |
		     SUN8I_A83T_MIPI_CSI2_CFG_N_LANE(lanes_count));

	/*
	 * Only a single virtual channel (index 0) is currently supported.
	 * While the registers do mention multiple physical channels being
	 * available (which can be configured to match a specific virtual
	 * channel or data type), it's unclear whether channels > 0 are actually
	 * connected and available and the reference source code only makes use
	 * of channel 0.
	 *
	 * Using extra channels would also require matching channels to be
	 * available on the CSI (and ISP) side, which is also unsure although
	 * some CSI implementations are said to support multiple channels for
	 * BT656 time-sharing.
	 *
	 * We still configure virtual channel numbers to ensure that virtual
	 * channel 0 only goes to channel 0.
	 */

	regmap_write(regmap, SUN8I_A83T_MIPI_CSI2_VCDT0_REG,
		     SUN8I_A83T_MIPI_CSI2_VCDT0_CH_VC(3, 3) |
		     SUN8I_A83T_MIPI_CSI2_VCDT0_CH_VC(2, 2) |
		     SUN8I_A83T_MIPI_CSI2_VCDT0_CH_VC(1, 1) |
		     SUN8I_A83T_MIPI_CSI2_VCDT0_CH_VC(0, 0) |
		     SUN8I_A83T_MIPI_CSI2_VCDT0_CH_DT(0, format->data_type));
}

/* V4L2 Subdev */

static int sun8i_a83t_mipi_csi2_s_stream(struct v4l2_subdev *subdev, int on)
{
	struct sun8i_a83t_mipi_csi2_device *csi2_dev =
		v4l2_get_subdevdata(subdev);
	struct v4l2_subdev *source_subdev = csi2_dev->bridge.source_subdev;
	union phy_configure_opts dphy_opts = { 0 };
	struct phy_configure_opts_mipi_dphy *dphy_cfg = &dphy_opts.mipi_dphy;
	struct v4l2_mbus_framefmt *mbus_format = &csi2_dev->bridge.mbus_format;
	const struct sun8i_a83t_mipi_csi2_format *format;
	struct phy *dphy = csi2_dev->dphy;
	struct device *dev = csi2_dev->dev;
	struct v4l2_ctrl *ctrl;
	unsigned int lanes_count =
		csi2_dev->bridge.endpoint.bus.mipi_csi2.num_data_lanes;
	unsigned long pixel_rate;
	int ret;

	if (!source_subdev)
		return -ENODEV;

	if (!on) {
		v4l2_subdev_call(source_subdev, video, s_stream, 0);
		ret = 0;
		goto disable;
	}

	/* Runtime PM */

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return ret;

	/* Sensor pixel rate */

	ctrl = v4l2_ctrl_find(source_subdev->ctrl_handler, V4L2_CID_PIXEL_RATE);
	if (!ctrl) {
		dev_err(dev, "missing sensor pixel rate\n");
		ret = -ENODEV;
		goto error_pm;
	}

	pixel_rate = (unsigned long)v4l2_ctrl_g_ctrl_int64(ctrl);
	if (!pixel_rate) {
		dev_err(dev, "missing (zero) sensor pixel rate\n");
		ret = -ENODEV;
		goto error_pm;
	}

	/* D-PHY */

	if (!lanes_count) {
		dev_err(dev, "missing (zero) MIPI CSI-2 lanes count\n");
		ret = -ENODEV;
		goto error_pm;
	}

	format = sun8i_a83t_mipi_csi2_format_find(mbus_format->code);
	if (WARN_ON(!format)) {
		ret = -ENODEV;
		goto error_pm;
	}

	phy_mipi_dphy_get_default_config(pixel_rate, format->bpp, lanes_count,
					 dphy_cfg);

	/*
	 * Note that our hardware is using DDR, which is not taken in account by
	 * phy_mipi_dphy_get_default_config when calculating hs_clk_rate from
	 * the pixel rate, lanes count and bpp.
	 *
	 * The resulting clock rate is basically the symbol rate over the whole
	 * link. The actual clock rate is calculated with division by two since
	 * DDR samples both on rising and falling edges.
	 */

	dev_dbg(dev, "A83T MIPI CSI-2 config:\n");
	dev_dbg(dev, "%ld pixels/s, %u bits/pixel, %u lanes, %lu Hz clock\n",
		pixel_rate, format->bpp, lanes_count,
		dphy_cfg->hs_clk_rate / 2);

	ret = phy_reset(dphy);
	if (ret) {
		dev_err(dev, "failed to reset MIPI D-PHY\n");
		goto error_pm;
	}

	ret = phy_configure(dphy, &dphy_opts);
	if (ret) {
		dev_err(dev, "failed to configure MIPI D-PHY\n");
		goto error_pm;
	}

	/* Controller */

	sun8i_a83t_mipi_csi2_configure(csi2_dev);
	sun8i_a83t_mipi_csi2_enable(csi2_dev);

	/* D-PHY */

	ret = phy_power_on(dphy);
	if (ret) {
		dev_err(dev, "failed to power on MIPI D-PHY\n");
		goto error_pm;
	}

	/* Source */

	ret = v4l2_subdev_call(source_subdev, video, s_stream, 1);
	if (ret && ret != -ENOIOCTLCMD)
		goto disable;

	return 0;

disable:
	phy_power_off(dphy);
	sun8i_a83t_mipi_csi2_disable(csi2_dev);

error_pm:
	pm_runtime_put(dev);

	return ret;
}

static const struct v4l2_subdev_video_ops
sun8i_a83t_mipi_csi2_video_ops = {
	.s_stream	= sun8i_a83t_mipi_csi2_s_stream,
};

static void
sun8i_a83t_mipi_csi2_mbus_format_prepare(struct v4l2_mbus_framefmt *mbus_format)
{
	if (!sun8i_a83t_mipi_csi2_format_find(mbus_format->code))
		mbus_format->code = sun8i_a83t_mipi_csi2_formats[0].mbus_code;

	mbus_format->field = V4L2_FIELD_NONE;
	mbus_format->colorspace = V4L2_COLORSPACE_RAW;
	mbus_format->quantization = V4L2_QUANTIZATION_DEFAULT;
	mbus_format->xfer_func = V4L2_XFER_FUNC_DEFAULT;
}

static int sun8i_a83t_mipi_csi2_init_cfg(struct v4l2_subdev *subdev,
					 struct v4l2_subdev_state *state)
{
	struct sun8i_a83t_mipi_csi2_device *csi2_dev =
		v4l2_get_subdevdata(subdev);
	unsigned int pad = SUN8I_A83T_MIPI_CSI2_PAD_SINK;
	struct v4l2_mbus_framefmt *mbus_format =
		v4l2_subdev_get_try_format(subdev, state, pad);
	struct mutex *lock = &csi2_dev->bridge.lock;

	mutex_lock(lock);

	mbus_format->code = sun8i_a83t_mipi_csi2_formats[0].mbus_code;
	mbus_format->width = 640;
	mbus_format->height = 480;

	sun8i_a83t_mipi_csi2_mbus_format_prepare(mbus_format);

	mutex_unlock(lock);

	return 0;
}

static int
sun8i_a83t_mipi_csi2_enum_mbus_code(struct v4l2_subdev *subdev,
				    struct v4l2_subdev_state *state,
				    struct v4l2_subdev_mbus_code_enum *code_enum)
{
	if (code_enum->index >= ARRAY_SIZE(sun8i_a83t_mipi_csi2_formats))
		return -EINVAL;

	code_enum->code =
		sun8i_a83t_mipi_csi2_formats[code_enum->index].mbus_code;

	return 0;
}

static int sun8i_a83t_mipi_csi2_get_fmt(struct v4l2_subdev *subdev,
					struct v4l2_subdev_state *state,
					struct v4l2_subdev_format *format)
{
	struct sun8i_a83t_mipi_csi2_device *csi2_dev =
		v4l2_get_subdevdata(subdev);
	struct v4l2_mbus_framefmt *mbus_format = &format->format;
	struct mutex *lock = &csi2_dev->bridge.lock;

	mutex_lock(lock);

	if (format->which == V4L2_SUBDEV_FORMAT_TRY)
		*mbus_format = *v4l2_subdev_get_try_format(subdev, state,
							   format->pad);
	else
		*mbus_format = csi2_dev->bridge.mbus_format;

	mutex_unlock(lock);

	return 0;
}

static int sun8i_a83t_mipi_csi2_set_fmt(struct v4l2_subdev *subdev,
					struct v4l2_subdev_state *state,
					struct v4l2_subdev_format *format)
{
	struct sun8i_a83t_mipi_csi2_device *csi2_dev =
		v4l2_get_subdevdata(subdev);
	struct v4l2_mbus_framefmt *mbus_format = &format->format;
	struct mutex *lock = &csi2_dev->bridge.lock;

	mutex_lock(lock);

	sun8i_a83t_mipi_csi2_mbus_format_prepare(mbus_format);

	if (format->which == V4L2_SUBDEV_FORMAT_TRY)
		*v4l2_subdev_get_try_format(subdev, state, format->pad) =
			*mbus_format;
	else
		csi2_dev->bridge.mbus_format = *mbus_format;

	mutex_unlock(lock);

	return 0;
}

static const struct v4l2_subdev_pad_ops sun8i_a83t_mipi_csi2_pad_ops = {
	.init_cfg	= sun8i_a83t_mipi_csi2_init_cfg,
	.enum_mbus_code	= sun8i_a83t_mipi_csi2_enum_mbus_code,
	.get_fmt	= sun8i_a83t_mipi_csi2_get_fmt,
	.set_fmt	= sun8i_a83t_mipi_csi2_set_fmt,
};

static const struct v4l2_subdev_ops sun8i_a83t_mipi_csi2_subdev_ops = {
	.video	= &sun8i_a83t_mipi_csi2_video_ops,
	.pad	= &sun8i_a83t_mipi_csi2_pad_ops,
};

/* Media Entity */

static const struct media_entity_operations sun8i_a83t_mipi_csi2_entity_ops = {
	.link_validate	= v4l2_subdev_link_validate,
};

/* V4L2 Async */

static int
sun8i_a83t_mipi_csi2_notifier_bound(struct v4l2_async_notifier *notifier,
				    struct v4l2_subdev *remote_subdev,
				    struct v4l2_async_subdev *async_subdev)
{
	struct v4l2_subdev *subdev = notifier->sd;
	struct sun8i_a83t_mipi_csi2_device *csi2_dev =
		container_of(notifier, struct sun8i_a83t_mipi_csi2_device,
			     bridge.notifier);
	struct media_entity *sink_entity = &subdev->entity;
	struct media_entity *source_entity = &remote_subdev->entity;
	struct device *dev = csi2_dev->dev;
	int sink_pad_index = 0;
	int source_pad_index;
	int ret;

	ret = media_entity_get_fwnode_pad(source_entity, remote_subdev->fwnode,
					  MEDIA_PAD_FL_SOURCE);
	if (ret < 0) {
		dev_err(dev, "missing source pad in external entity %s\n",
			source_entity->name);
		return -EINVAL;
	}

	source_pad_index = ret;

	dev_dbg(dev, "creating %s:%u -> %s:%u link\n", source_entity->name,
		source_pad_index, sink_entity->name, sink_pad_index);

	ret = media_create_pad_link(source_entity, source_pad_index,
				    sink_entity, sink_pad_index,
				    MEDIA_LNK_FL_ENABLED |
				    MEDIA_LNK_FL_IMMUTABLE);
	if (ret) {
		dev_err(dev, "failed to create %s:%u -> %s:%u link\n",
			source_entity->name, source_pad_index,
			sink_entity->name, sink_pad_index);
		return ret;
	}

	csi2_dev->bridge.source_subdev = remote_subdev;

	return 0;
}

static const struct v4l2_async_notifier_operations
sun8i_a83t_mipi_csi2_notifier_ops = {
	.bound	= sun8i_a83t_mipi_csi2_notifier_bound,
};

/* Bridge */

static int
sun8i_a83t_mipi_csi2_bridge_source_setup(struct sun8i_a83t_mipi_csi2_device *csi2_dev)
{
	struct v4l2_async_notifier *notifier = &csi2_dev->bridge.notifier;
	struct v4l2_fwnode_endpoint *endpoint = &csi2_dev->bridge.endpoint;
	struct v4l2_async_subdev *subdev_async;
	struct fwnode_handle *handle;
	struct device *dev = csi2_dev->dev;
	int ret;

	handle = fwnode_graph_get_endpoint_by_id(dev_fwnode(dev), 0, 0,
						 FWNODE_GRAPH_ENDPOINT_NEXT);
	if (!handle)
		return -ENODEV;

	endpoint->bus_type = V4L2_MBUS_CSI2_DPHY;

	ret = v4l2_fwnode_endpoint_parse(handle, endpoint);
	if (ret)
		goto complete;

	subdev_async =
		v4l2_async_nf_add_fwnode_remote(notifier, handle,
						struct v4l2_async_subdev);
	if (IS_ERR(subdev_async))
		ret = PTR_ERR(subdev_async);

complete:
	fwnode_handle_put(handle);

	return ret;
}

static int
sun8i_a83t_mipi_csi2_bridge_setup(struct sun8i_a83t_mipi_csi2_device *csi2_dev)
{
	struct sun8i_a83t_mipi_csi2_bridge *bridge = &csi2_dev->bridge;
	struct v4l2_subdev *subdev = &bridge->subdev;
	struct v4l2_async_notifier *notifier = &bridge->notifier;
	struct media_pad *pads = bridge->pads;
	struct device *dev = csi2_dev->dev;
	bool notifier_registered = false;
	int ret;

	mutex_init(&bridge->lock);

	/* V4L2 Subdev */

	v4l2_subdev_init(subdev, &sun8i_a83t_mipi_csi2_subdev_ops);
	strscpy(subdev->name, SUN8I_A83T_MIPI_CSI2_NAME, sizeof(subdev->name));
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	subdev->owner = THIS_MODULE;
	subdev->dev = dev;

	v4l2_set_subdevdata(subdev, csi2_dev);

	/* Media Entity */

	subdev->entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;
	subdev->entity.ops = &sun8i_a83t_mipi_csi2_entity_ops;

	/* Media Pads */

	pads[SUN8I_A83T_MIPI_CSI2_PAD_SINK].flags = MEDIA_PAD_FL_SINK |
						    MEDIA_PAD_FL_MUST_CONNECT;
	pads[SUN8I_A83T_MIPI_CSI2_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE |
						      MEDIA_PAD_FL_MUST_CONNECT;

	ret = media_entity_pads_init(&subdev->entity,
				     SUN8I_A83T_MIPI_CSI2_PAD_COUNT, pads);
	if (ret)
		return ret;

	/* V4L2 Async */

	v4l2_async_nf_init(notifier);
	notifier->ops = &sun8i_a83t_mipi_csi2_notifier_ops;

	ret = sun8i_a83t_mipi_csi2_bridge_source_setup(csi2_dev);
	if (ret && ret != -ENODEV)
		goto error_v4l2_notifier_cleanup;

	/* Only register the notifier when a sensor is connected. */
	if (ret != -ENODEV) {
		ret = v4l2_async_subdev_nf_register(subdev, notifier);
		if (ret < 0)
			goto error_v4l2_notifier_cleanup;

		notifier_registered = true;
	}

	/* V4L2 Subdev */

	ret = v4l2_async_register_subdev(subdev);
	if (ret < 0)
		goto error_v4l2_notifier_unregister;

	return 0;

error_v4l2_notifier_unregister:
	if (notifier_registered)
		v4l2_async_nf_unregister(notifier);

error_v4l2_notifier_cleanup:
	v4l2_async_nf_cleanup(notifier);

	media_entity_cleanup(&subdev->entity);

	return ret;
}

static void
sun8i_a83t_mipi_csi2_bridge_cleanup(struct sun8i_a83t_mipi_csi2_device *csi2_dev)
{
	struct v4l2_subdev *subdev = &csi2_dev->bridge.subdev;
	struct v4l2_async_notifier *notifier = &csi2_dev->bridge.notifier;

	v4l2_async_unregister_subdev(subdev);
	v4l2_async_nf_unregister(notifier);
	v4l2_async_nf_cleanup(notifier);
	media_entity_cleanup(&subdev->entity);
}

/* Platform */

static int sun8i_a83t_mipi_csi2_suspend(struct device *dev)
{
	struct sun8i_a83t_mipi_csi2_device *csi2_dev = dev_get_drvdata(dev);

	clk_disable_unprepare(csi2_dev->clock_misc);
	clk_disable_unprepare(csi2_dev->clock_mipi);
	clk_disable_unprepare(csi2_dev->clock_mod);
	reset_control_assert(csi2_dev->reset);

	return 0;
}

static int sun8i_a83t_mipi_csi2_resume(struct device *dev)
{
	struct sun8i_a83t_mipi_csi2_device *csi2_dev = dev_get_drvdata(dev);
	int ret;

	ret = reset_control_deassert(csi2_dev->reset);
	if (ret) {
		dev_err(dev, "failed to deassert reset\n");
		return ret;
	}

	ret = clk_prepare_enable(csi2_dev->clock_mod);
	if (ret) {
		dev_err(dev, "failed to enable module clock\n");
		goto error_reset;
	}

	ret = clk_prepare_enable(csi2_dev->clock_mipi);
	if (ret) {
		dev_err(dev, "failed to enable MIPI clock\n");
		goto error_clock_mod;
	}

	ret = clk_prepare_enable(csi2_dev->clock_misc);
	if (ret) {
		dev_err(dev, "failed to enable CSI misc clock\n");
		goto error_clock_mipi;
	}

	sun8i_a83t_mipi_csi2_init(csi2_dev);

	return 0;

error_clock_mipi:
	clk_disable_unprepare(csi2_dev->clock_mipi);

error_clock_mod:
	clk_disable_unprepare(csi2_dev->clock_mod);

error_reset:
	reset_control_assert(csi2_dev->reset);

	return ret;
}

static const struct dev_pm_ops sun8i_a83t_mipi_csi2_pm_ops = {
	.runtime_suspend	= sun8i_a83t_mipi_csi2_suspend,
	.runtime_resume		= sun8i_a83t_mipi_csi2_resume,
};

static const struct regmap_config sun8i_a83t_mipi_csi2_regmap_config = {
	.reg_bits       = 32,
	.reg_stride     = 4,
	.val_bits       = 32,
	.max_register	= 0x120,
};

static int
sun8i_a83t_mipi_csi2_resources_setup(struct sun8i_a83t_mipi_csi2_device *csi2_dev,
				     struct platform_device *platform_dev)
{
	struct device *dev = csi2_dev->dev;
	void __iomem *io_base;
	int ret;

	/* Registers */

	io_base = devm_platform_ioremap_resource(platform_dev, 0);
	if (IS_ERR(io_base))
		return PTR_ERR(io_base);

	csi2_dev->regmap =
		devm_regmap_init_mmio_clk(dev, "bus", io_base,
					  &sun8i_a83t_mipi_csi2_regmap_config);
	if (IS_ERR(csi2_dev->regmap)) {
		dev_err(dev, "failed to init register map\n");
		return PTR_ERR(csi2_dev->regmap);
	}

	/* Clocks */

	csi2_dev->clock_mod = devm_clk_get(dev, "mod");
	if (IS_ERR(csi2_dev->clock_mod)) {
		dev_err(dev, "failed to acquire mod clock\n");
		return PTR_ERR(csi2_dev->clock_mod);
	}

	ret = clk_set_rate_exclusive(csi2_dev->clock_mod, 297000000);
	if (ret) {
		dev_err(dev, "failed to set mod clock rate\n");
		return ret;
	}

	csi2_dev->clock_mipi = devm_clk_get(dev, "mipi");
	if (IS_ERR(csi2_dev->clock_mipi)) {
		dev_err(dev, "failed to acquire mipi clock\n");
		ret = PTR_ERR(csi2_dev->clock_mipi);
		goto error_clock_rate_exclusive;
	}

	csi2_dev->clock_misc = devm_clk_get(dev, "misc");
	if (IS_ERR(csi2_dev->clock_misc)) {
		dev_err(dev, "failed to acquire misc clock\n");
		ret = PTR_ERR(csi2_dev->clock_misc);
		goto error_clock_rate_exclusive;
	}

	/* Reset */

	csi2_dev->reset = devm_reset_control_get_shared(dev, NULL);
	if (IS_ERR(csi2_dev->reset)) {
		dev_err(dev, "failed to get reset controller\n");
		ret = PTR_ERR(csi2_dev->reset);
		goto error_clock_rate_exclusive;
	}

	/* D-PHY */

	ret = sun8i_a83t_dphy_register(csi2_dev);
	if (ret) {
		dev_err(dev, "failed to initialize MIPI D-PHY\n");
		goto error_clock_rate_exclusive;
	}

	/* Runtime PM */

	pm_runtime_enable(dev);

	return 0;

error_clock_rate_exclusive:
	clk_rate_exclusive_put(csi2_dev->clock_mod);

	return ret;
}

static void
sun8i_a83t_mipi_csi2_resources_cleanup(struct sun8i_a83t_mipi_csi2_device *csi2_dev)
{
	pm_runtime_disable(csi2_dev->dev);
	phy_exit(csi2_dev->dphy);
	clk_rate_exclusive_put(csi2_dev->clock_mod);
}

static int sun8i_a83t_mipi_csi2_probe(struct platform_device *platform_dev)
{
	struct sun8i_a83t_mipi_csi2_device *csi2_dev;
	struct device *dev = &platform_dev->dev;
	int ret;

	csi2_dev = devm_kzalloc(dev, sizeof(*csi2_dev), GFP_KERNEL);
	if (!csi2_dev)
		return -ENOMEM;

	csi2_dev->dev = dev;
	platform_set_drvdata(platform_dev, csi2_dev);

	ret = sun8i_a83t_mipi_csi2_resources_setup(csi2_dev, platform_dev);
	if (ret)
		return ret;

	ret = sun8i_a83t_mipi_csi2_bridge_setup(csi2_dev);
	if (ret)
		goto error_resources;

	return 0;

error_resources:
	sun8i_a83t_mipi_csi2_resources_cleanup(csi2_dev);

	return ret;
}

static void sun8i_a83t_mipi_csi2_remove(struct platform_device *platform_dev)
{
	struct sun8i_a83t_mipi_csi2_device *csi2_dev =
		platform_get_drvdata(platform_dev);

	sun8i_a83t_mipi_csi2_bridge_cleanup(csi2_dev);
	sun8i_a83t_mipi_csi2_resources_cleanup(csi2_dev);
}

static const struct of_device_id sun8i_a83t_mipi_csi2_of_match[] = {
	{ .compatible	= "allwinner,sun8i-a83t-mipi-csi2" },
	{},
};
MODULE_DEVICE_TABLE(of, sun8i_a83t_mipi_csi2_of_match);

static struct platform_driver sun8i_a83t_mipi_csi2_platform_driver = {
	.probe	= sun8i_a83t_mipi_csi2_probe,
	.remove_new = sun8i_a83t_mipi_csi2_remove,
	.driver	= {
		.name		= SUN8I_A83T_MIPI_CSI2_NAME,
		.of_match_table	= sun8i_a83t_mipi_csi2_of_match,
		.pm		= &sun8i_a83t_mipi_csi2_pm_ops,
	},
};
module_platform_driver(sun8i_a83t_mipi_csi2_platform_driver);

MODULE_DESCRIPTION("Allwinner A83T MIPI CSI-2 and D-PHY Controller Driver");
MODULE_AUTHOR("Paul Kocialkowski <paul.kocialkowski@bootlin.com>");
MODULE_LICENSE("GPL");
