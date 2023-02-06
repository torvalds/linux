// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/clk.h>
#include <linux/clk/tegra.h>
#include <linux/device.h>
#include <linux/host1x.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include <media/v4l2-fwnode.h>

#include "csi.h"
#include "video.h"

#define MHZ			1000000

static inline struct tegra_csi *
host1x_client_to_csi(struct host1x_client *client)
{
	return container_of(client, struct tegra_csi, client);
}

static inline struct tegra_csi_channel *to_csi_chan(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct tegra_csi_channel, subdev);
}

/*
 * CSI is a separate subdevice which has 6 source pads to generate
 * test pattern. CSI subdevice pad ops are used only for TPG and
 * allows below TPG formats.
 */
static const struct v4l2_mbus_framefmt tegra_csi_tpg_fmts[] = {
	{
		TEGRA_DEF_WIDTH,
		TEGRA_DEF_HEIGHT,
		MEDIA_BUS_FMT_SRGGB10_1X10,
		V4L2_FIELD_NONE,
		V4L2_COLORSPACE_SRGB
	},
	{
		TEGRA_DEF_WIDTH,
		TEGRA_DEF_HEIGHT,
		MEDIA_BUS_FMT_RGB888_1X32_PADHI,
		V4L2_FIELD_NONE,
		V4L2_COLORSPACE_SRGB
	},
};

static const struct v4l2_frmsize_discrete tegra_csi_tpg_sizes[] = {
	{ 1280, 720 },
	{ 1920, 1080 },
	{ 3840, 2160 },
};

/*
 * V4L2 Subdevice Pad Operations
 */
static int csi_enum_bus_code(struct v4l2_subdev *subdev,
			     struct v4l2_subdev_pad_config *cfg,
			     struct v4l2_subdev_mbus_code_enum *code)
{
	if (!IS_ENABLED(CONFIG_VIDEO_TEGRA_TPG))
		return -ENOIOCTLCMD;

	if (code->index >= ARRAY_SIZE(tegra_csi_tpg_fmts))
		return -EINVAL;

	code->code = tegra_csi_tpg_fmts[code->index].code;

	return 0;
}

static int csi_get_format(struct v4l2_subdev *subdev,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct tegra_csi_channel *csi_chan = to_csi_chan(subdev);

	if (!IS_ENABLED(CONFIG_VIDEO_TEGRA_TPG))
		return -ENOIOCTLCMD;

	fmt->format = csi_chan->format;

	return 0;
}

static int csi_get_frmrate_table_index(struct tegra_csi *csi, u32 code,
				       u32 width, u32 height)
{
	const struct tpg_framerate *frmrate;
	unsigned int i;

	frmrate = csi->soc->tpg_frmrate_table;
	for (i = 0; i < csi->soc->tpg_frmrate_table_size; i++) {
		if (frmrate[i].code == code &&
		    frmrate[i].frmsize.width == width &&
		    frmrate[i].frmsize.height == height) {
			return i;
		}
	}

	return -EINVAL;
}

static void csi_chan_update_blank_intervals(struct tegra_csi_channel *csi_chan,
					    u32 code, u32 width, u32 height)
{
	struct tegra_csi *csi = csi_chan->csi;
	const struct tpg_framerate *frmrate = csi->soc->tpg_frmrate_table;
	int index;

	index = csi_get_frmrate_table_index(csi_chan->csi, code,
					    width, height);
	if (index >= 0) {
		csi_chan->h_blank = frmrate[index].h_blank;
		csi_chan->v_blank = frmrate[index].v_blank;
		csi_chan->framerate = frmrate[index].framerate;
	}
}

static int csi_enum_framesizes(struct v4l2_subdev *subdev,
			       struct v4l2_subdev_pad_config *cfg,
			       struct v4l2_subdev_frame_size_enum *fse)
{
	unsigned int i;

	if (!IS_ENABLED(CONFIG_VIDEO_TEGRA_TPG))
		return -ENOIOCTLCMD;

	if (fse->index >= ARRAY_SIZE(tegra_csi_tpg_sizes))
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(tegra_csi_tpg_fmts); i++)
		if (fse->code == tegra_csi_tpg_fmts[i].code)
			break;

	if (i == ARRAY_SIZE(tegra_csi_tpg_fmts))
		return -EINVAL;

	fse->min_width = tegra_csi_tpg_sizes[fse->index].width;
	fse->max_width = tegra_csi_tpg_sizes[fse->index].width;
	fse->min_height = tegra_csi_tpg_sizes[fse->index].height;
	fse->max_height = tegra_csi_tpg_sizes[fse->index].height;

	return 0;
}

static int csi_enum_frameintervals(struct v4l2_subdev *subdev,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_interval_enum *fie)
{
	struct tegra_csi_channel *csi_chan = to_csi_chan(subdev);
	struct tegra_csi *csi = csi_chan->csi;
	const struct tpg_framerate *frmrate = csi->soc->tpg_frmrate_table;
	int index;

	if (!IS_ENABLED(CONFIG_VIDEO_TEGRA_TPG))
		return -ENOIOCTLCMD;

	/* one framerate per format and resolution */
	if (fie->index > 0)
		return -EINVAL;

	index = csi_get_frmrate_table_index(csi_chan->csi, fie->code,
					    fie->width, fie->height);
	if (index < 0)
		return -EINVAL;

	fie->interval.numerator = 1;
	fie->interval.denominator = frmrate[index].framerate;

	return 0;
}

static int csi_set_format(struct v4l2_subdev *subdev,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct tegra_csi_channel *csi_chan = to_csi_chan(subdev);
	struct v4l2_mbus_framefmt *format = &fmt->format;
	const struct v4l2_frmsize_discrete *sizes;
	unsigned int i;

	if (!IS_ENABLED(CONFIG_VIDEO_TEGRA_TPG))
		return -ENOIOCTLCMD;

	sizes = v4l2_find_nearest_size(tegra_csi_tpg_sizes,
				       ARRAY_SIZE(tegra_csi_tpg_sizes),
				       width, height,
				       format->width, format->width);
	format->width = sizes->width;
	format->height = sizes->height;

	for (i = 0; i < ARRAY_SIZE(tegra_csi_tpg_fmts); i++)
		if (format->code == tegra_csi_tpg_fmts[i].code)
			break;

	if (i == ARRAY_SIZE(tegra_csi_tpg_fmts))
		i = 0;

	format->code = tegra_csi_tpg_fmts[i].code;
	format->field = V4L2_FIELD_NONE;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		return 0;

	/* update blanking intervals from frame rate table and format */
	csi_chan_update_blank_intervals(csi_chan, format->code,
					format->width, format->height);
	csi_chan->format = *format;

	return 0;
}

/*
 * V4L2 Subdevice Video Operations
 */
static int tegra_csi_g_frame_interval(struct v4l2_subdev *subdev,
				      struct v4l2_subdev_frame_interval *vfi)
{
	struct tegra_csi_channel *csi_chan = to_csi_chan(subdev);

	if (!IS_ENABLED(CONFIG_VIDEO_TEGRA_TPG))
		return -ENOIOCTLCMD;

	vfi->interval.numerator = 1;
	vfi->interval.denominator = csi_chan->framerate;

	return 0;
}

static unsigned int csi_get_pixel_rate(struct tegra_csi_channel *csi_chan)
{
	struct tegra_vi_channel *chan;
	struct v4l2_subdev *src_subdev;
	struct v4l2_ctrl *ctrl;

	chan = v4l2_get_subdev_hostdata(&csi_chan->subdev);
	src_subdev = tegra_channel_get_remote_source_subdev(chan);
	ctrl = v4l2_ctrl_find(src_subdev->ctrl_handler, V4L2_CID_PIXEL_RATE);
	if (ctrl)
		return v4l2_ctrl_g_ctrl_int64(ctrl);

	return 0;
}

void tegra_csi_calc_settle_time(struct tegra_csi_channel *csi_chan,
				u8 *clk_settle_time,
				u8 *ths_settle_time)
{
	struct tegra_csi *csi = csi_chan->csi;
	unsigned int cil_clk_mhz;
	unsigned int pix_clk_mhz;
	int clk_idx = (csi_chan->csi_port_num >> 1) + 1;

	cil_clk_mhz = clk_get_rate(csi->clks[clk_idx].clk) / MHZ;
	pix_clk_mhz = csi_get_pixel_rate(csi_chan) / MHZ;

	/*
	 * CLK Settle time is the interval during which HS receiver should
	 * ignore any clock lane HS transitions, starting from the beginning
	 * of T-CLK-PREPARE.
	 * Per DPHY specification, T-CLK-SETTLE should be between 95ns ~ 300ns
	 *
	 * 95ns < (clk-settle-programmed + 7) * lp clk period < 300ns
	 * midpoint = 197.5 ns
	 */
	*clk_settle_time = ((95 + 300) * cil_clk_mhz - 14000) / 2000;

	/*
	 * THS Settle time is the interval during which HS receiver should
	 * ignore any data lane HS transitions, starting from the beginning
	 * of THS-PREPARE.
	 *
	 * Per DPHY specification, T-HS-SETTLE should be between 85ns + 6UI
	 * and 145ns+10UI.
	 * 85ns + 6UI < (Ths-settle-prog + 5) * lp_clk_period < 145ns + 10UI
	 * midpoint = 115ns + 8UI
	 */
	if (pix_clk_mhz)
		*ths_settle_time = (115 * cil_clk_mhz + 8000 * cil_clk_mhz
				   / (2 * pix_clk_mhz) - 5000) / 1000;
}

static int tegra_csi_enable_stream(struct v4l2_subdev *subdev)
{
	struct tegra_vi_channel *chan = v4l2_get_subdev_hostdata(subdev);
	struct tegra_csi_channel *csi_chan = to_csi_chan(subdev);
	struct tegra_csi *csi = csi_chan->csi;
	int ret, err;

	ret = pm_runtime_get_sync(csi->dev);
	if (ret < 0) {
		dev_err(csi->dev, "failed to get runtime PM: %d\n", ret);
		pm_runtime_put_noidle(csi->dev);
		return ret;
	}

	if (csi_chan->mipi) {
		ret = tegra_mipi_enable(csi_chan->mipi);
		if (ret < 0) {
			dev_err(csi->dev,
				"failed to enable MIPI pads: %d\n", ret);
			goto rpm_put;
		}

		/*
		 * CSI MIPI pads PULLUP, PULLDN and TERM impedances need to
		 * be calibrated after power on.
		 * So, trigger the calibration start here and results will
		 * be latched and applied to the pads when link is in LP11
		 * state during start of sensor streaming.
		 */
		ret = tegra_mipi_start_calibration(csi_chan->mipi);
		if (ret < 0) {
			dev_err(csi->dev,
				"failed to start MIPI calibration: %d\n", ret);
			goto disable_mipi;
		}
	}

	csi_chan->pg_mode = chan->pg_mode;
	ret = csi->ops->csi_start_streaming(csi_chan);
	if (ret < 0)
		goto finish_calibration;

	return 0;

finish_calibration:
	if (csi_chan->mipi)
		tegra_mipi_finish_calibration(csi_chan->mipi);
disable_mipi:
	if (csi_chan->mipi) {
		err = tegra_mipi_disable(csi_chan->mipi);
		if (err < 0)
			dev_err(csi->dev,
				"failed to disable MIPI pads: %d\n", err);
	}

rpm_put:
	pm_runtime_put(csi->dev);
	return ret;
}

static int tegra_csi_disable_stream(struct v4l2_subdev *subdev)
{
	struct tegra_csi_channel *csi_chan = to_csi_chan(subdev);
	struct tegra_csi *csi = csi_chan->csi;
	int err;

	csi->ops->csi_stop_streaming(csi_chan);

	if (csi_chan->mipi) {
		err = tegra_mipi_disable(csi_chan->mipi);
		if (err < 0)
			dev_err(csi->dev,
				"failed to disable MIPI pads: %d\n", err);
	}

	pm_runtime_put(csi->dev);

	return 0;
}

static int tegra_csi_s_stream(struct v4l2_subdev *subdev, int enable)
{
	int ret;

	if (enable)
		ret = tegra_csi_enable_stream(subdev);
	else
		ret = tegra_csi_disable_stream(subdev);

	return ret;
}

/*
 * V4L2 Subdevice Operations
 */
static const struct v4l2_subdev_video_ops tegra_csi_video_ops = {
	.s_stream = tegra_csi_s_stream,
	.g_frame_interval = tegra_csi_g_frame_interval,
	.s_frame_interval = tegra_csi_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops tegra_csi_pad_ops = {
	.enum_mbus_code		= csi_enum_bus_code,
	.enum_frame_size	= csi_enum_framesizes,
	.enum_frame_interval	= csi_enum_frameintervals,
	.get_fmt		= csi_get_format,
	.set_fmt		= csi_set_format,
};

static const struct v4l2_subdev_ops tegra_csi_ops = {
	.video  = &tegra_csi_video_ops,
	.pad    = &tegra_csi_pad_ops,
};

static int tegra_csi_channel_alloc(struct tegra_csi *csi,
				   struct device_node *node,
				   unsigned int port_num, unsigned int lanes,
				   unsigned int num_pads)
{
	struct tegra_csi_channel *chan;
	int ret = 0;

	chan = kzalloc(sizeof(*chan), GFP_KERNEL);
	if (!chan)
		return -ENOMEM;

	list_add_tail(&chan->list, &csi->csi_chans);
	chan->csi = csi;
	chan->csi_port_num = port_num;
	chan->numlanes = lanes;
	chan->of_node = of_node_get(node);
	chan->numpads = num_pads;
	if (num_pads & 0x2) {
		chan->pads[0].flags = MEDIA_PAD_FL_SINK;
		chan->pads[1].flags = MEDIA_PAD_FL_SOURCE;
	} else {
		chan->pads[0].flags = MEDIA_PAD_FL_SOURCE;
	}

	if (IS_ENABLED(CONFIG_VIDEO_TEGRA_TPG))
		return 0;

	chan->mipi = tegra_mipi_request(csi->dev, node);
	if (IS_ERR(chan->mipi)) {
		ret = PTR_ERR(chan->mipi);
		chan->mipi = NULL;
		dev_err(csi->dev, "failed to get mipi device: %d\n", ret);
	}

	return ret;
}

static int tegra_csi_tpg_channels_alloc(struct tegra_csi *csi)
{
	struct device_node *node = csi->dev->of_node;
	unsigned int port_num;
	unsigned int tpg_channels = csi->soc->csi_max_channels;
	int ret;

	/* allocate CSI channel for each CSI x2 ports */
	for (port_num = 0; port_num < tpg_channels; port_num++) {
		ret = tegra_csi_channel_alloc(csi, node, port_num, 2, 1);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int tegra_csi_channels_alloc(struct tegra_csi *csi)
{
	struct device_node *node = csi->dev->of_node;
	struct v4l2_fwnode_endpoint v4l2_ep = {
		.bus_type = V4L2_MBUS_CSI2_DPHY
	};
	struct fwnode_handle *fwh;
	struct device_node *channel;
	struct device_node *ep;
	unsigned int lanes, portno, num_pads;
	int ret;

	for_each_child_of_node(node, channel) {
		if (!of_node_name_eq(channel, "channel"))
			continue;

		ret = of_property_read_u32(channel, "reg", &portno);
		if (ret < 0)
			continue;

		if (portno >= csi->soc->csi_max_channels) {
			dev_err(csi->dev, "invalid port num %d for %pOF\n",
				portno, channel);
			ret = -EINVAL;
			goto err_node_put;
		}

		ep = of_graph_get_endpoint_by_regs(channel, 0, 0);
		if (!ep)
			continue;

		fwh = of_fwnode_handle(ep);
		ret = v4l2_fwnode_endpoint_parse(fwh, &v4l2_ep);
		of_node_put(ep);
		if (ret) {
			dev_err(csi->dev,
				"failed to parse v4l2 endpoint for %pOF: %d\n",
				channel, ret);
			goto err_node_put;
		}

		lanes = v4l2_ep.bus.mipi_csi2.num_data_lanes;
		if (!lanes || ((lanes & (lanes - 1)) != 0)) {
			dev_err(csi->dev, "invalid data-lanes %d for %pOF\n",
				lanes, channel);
			ret = -EINVAL;
			goto err_node_put;
		}

		num_pads = of_graph_get_endpoint_count(channel);
		if (num_pads == TEGRA_CSI_PADS_NUM) {
			ret = tegra_csi_channel_alloc(csi, channel, portno,
						      lanes, num_pads);
			if (ret < 0)
				goto err_node_put;
		}
	}

	return 0;

err_node_put:
	of_node_put(channel);
	return ret;
}

static int tegra_csi_channel_init(struct tegra_csi_channel *chan)
{
	struct tegra_csi *csi = chan->csi;
	struct v4l2_subdev *subdev;
	int ret;

	/* initialize the default format */
	chan->format.code = MEDIA_BUS_FMT_SRGGB10_1X10;
	chan->format.field = V4L2_FIELD_NONE;
	chan->format.colorspace = V4L2_COLORSPACE_SRGB;
	chan->format.width = TEGRA_DEF_WIDTH;
	chan->format.height = TEGRA_DEF_HEIGHT;
	csi_chan_update_blank_intervals(chan, chan->format.code,
					chan->format.width,
					chan->format.height);
	/* initialize V4L2 subdevice and media entity */
	subdev = &chan->subdev;
	v4l2_subdev_init(subdev, &tegra_csi_ops);
	subdev->dev = csi->dev;
	if (IS_ENABLED(CONFIG_VIDEO_TEGRA_TPG))
		snprintf(subdev->name, V4L2_SUBDEV_NAME_SIZE, "%s-%d", "tpg",
			 chan->csi_port_num);
	else
		snprintf(subdev->name, V4L2_SUBDEV_NAME_SIZE, "%s",
			 kbasename(chan->of_node->full_name));

	v4l2_set_subdevdata(subdev, chan);
	subdev->fwnode = of_fwnode_handle(chan->of_node);
	subdev->entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;

	/* initialize media entity pads */
	ret = media_entity_pads_init(&subdev->entity, chan->numpads,
				     chan->pads);
	if (ret < 0) {
		dev_err(csi->dev,
			"failed to initialize media entity: %d\n", ret);
		subdev->dev = NULL;
		return ret;
	}

	if (!IS_ENABLED(CONFIG_VIDEO_TEGRA_TPG)) {
		ret = v4l2_async_register_subdev(subdev);
		if (ret < 0) {
			dev_err(csi->dev,
				"failed to register subdev: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

void tegra_csi_error_recover(struct v4l2_subdev *sd)
{
	struct tegra_csi_channel *csi_chan = to_csi_chan(sd);
	struct tegra_csi *csi = csi_chan->csi;

	/* stop streaming during error recovery */
	csi->ops->csi_stop_streaming(csi_chan);
	csi->ops->csi_err_recover(csi_chan);
	csi->ops->csi_start_streaming(csi_chan);
}

static int tegra_csi_channels_init(struct tegra_csi *csi)
{
	struct tegra_csi_channel *chan;
	int ret;

	list_for_each_entry(chan, &csi->csi_chans, list) {
		ret = tegra_csi_channel_init(chan);
		if (ret) {
			dev_err(csi->dev,
				"failed to initialize channel-%d: %d\n",
				chan->csi_port_num, ret);
			return ret;
		}
	}

	return 0;
}

static void tegra_csi_channels_cleanup(struct tegra_csi *csi)
{
	struct v4l2_subdev *subdev;
	struct tegra_csi_channel *chan, *tmp;

	list_for_each_entry_safe(chan, tmp, &csi->csi_chans, list) {
		if (chan->mipi)
			tegra_mipi_free(chan->mipi);

		subdev = &chan->subdev;
		if (subdev->dev) {
			if (!IS_ENABLED(CONFIG_VIDEO_TEGRA_TPG))
				v4l2_async_unregister_subdev(subdev);
			media_entity_cleanup(&subdev->entity);
		}

		of_node_put(chan->of_node);
		list_del(&chan->list);
		kfree(chan);
	}
}

static int __maybe_unused csi_runtime_suspend(struct device *dev)
{
	struct tegra_csi *csi = dev_get_drvdata(dev);

	clk_bulk_disable_unprepare(csi->soc->num_clks, csi->clks);

	return 0;
}

static int __maybe_unused csi_runtime_resume(struct device *dev)
{
	struct tegra_csi *csi = dev_get_drvdata(dev);
	int ret;

	ret = clk_bulk_prepare_enable(csi->soc->num_clks, csi->clks);
	if (ret < 0) {
		dev_err(csi->dev, "failed to enable clocks: %d\n", ret);
		return ret;
	}

	return 0;
}

static int tegra_csi_init(struct host1x_client *client)
{
	struct tegra_csi *csi = host1x_client_to_csi(client);
	struct tegra_video_device *vid = dev_get_drvdata(client->host);
	int ret;

	INIT_LIST_HEAD(&csi->csi_chans);

	if (IS_ENABLED(CONFIG_VIDEO_TEGRA_TPG))
		ret = tegra_csi_tpg_channels_alloc(csi);
	else
		ret = tegra_csi_channels_alloc(csi);
	if (ret < 0) {
		dev_err(csi->dev,
			"failed to allocate channels: %d\n", ret);
		goto cleanup;
	}

	ret = tegra_csi_channels_init(csi);
	if (ret < 0)
		goto cleanup;

	vid->csi = csi;

	return 0;

cleanup:
	tegra_csi_channels_cleanup(csi);
	return ret;
}

static int tegra_csi_exit(struct host1x_client *client)
{
	struct tegra_csi *csi = host1x_client_to_csi(client);

	tegra_csi_channels_cleanup(csi);

	return 0;
}

static const struct host1x_client_ops csi_client_ops = {
	.init = tegra_csi_init,
	.exit = tegra_csi_exit,
};

static int tegra_csi_probe(struct platform_device *pdev)
{
	struct tegra_csi *csi;
	unsigned int i;
	int ret;

	csi = devm_kzalloc(&pdev->dev, sizeof(*csi), GFP_KERNEL);
	if (!csi)
		return -ENOMEM;

	csi->iomem = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(csi->iomem))
		return PTR_ERR(csi->iomem);

	csi->soc = of_device_get_match_data(&pdev->dev);

	csi->clks = devm_kcalloc(&pdev->dev, csi->soc->num_clks,
				 sizeof(*csi->clks), GFP_KERNEL);
	if (!csi->clks)
		return -ENOMEM;

	for (i = 0; i < csi->soc->num_clks; i++)
		csi->clks[i].id = csi->soc->clk_names[i];

	ret = devm_clk_bulk_get(&pdev->dev, csi->soc->num_clks, csi->clks);
	if (ret) {
		dev_err(&pdev->dev, "failed to get the clocks: %d\n", ret);
		return ret;
	}

	if (!pdev->dev.pm_domain) {
		ret = -ENOENT;
		dev_warn(&pdev->dev, "PM domain is not attached: %d\n", ret);
		return ret;
	}

	csi->dev = &pdev->dev;
	csi->ops = csi->soc->ops;
	platform_set_drvdata(pdev, csi);
	pm_runtime_enable(&pdev->dev);

	/* initialize host1x interface */
	INIT_LIST_HEAD(&csi->client.list);
	csi->client.ops = &csi_client_ops;
	csi->client.dev = &pdev->dev;

	ret = host1x_client_register(&csi->client);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"failed to register host1x client: %d\n", ret);
		goto rpm_disable;
	}

	return 0;

rpm_disable:
	pm_runtime_disable(&pdev->dev);
	return ret;
}

static int tegra_csi_remove(struct platform_device *pdev)
{
	struct tegra_csi *csi = platform_get_drvdata(pdev);
	int err;

	err = host1x_client_unregister(&csi->client);
	if (err < 0) {
		dev_err(&pdev->dev,
			"failed to unregister host1x client: %d\n", err);
		return err;
	}

	pm_runtime_disable(&pdev->dev);

	return 0;
}

static const struct of_device_id tegra_csi_of_id_table[] = {
#if defined(CONFIG_ARCH_TEGRA_210_SOC)
	{ .compatible = "nvidia,tegra210-csi", .data = &tegra210_csi_soc },
#endif
	{ }
};
MODULE_DEVICE_TABLE(of, tegra_csi_of_id_table);

static const struct dev_pm_ops tegra_csi_pm_ops = {
	SET_RUNTIME_PM_OPS(csi_runtime_suspend, csi_runtime_resume, NULL)
};

struct platform_driver tegra_csi_driver = {
	.driver = {
		.name		= "tegra-csi",
		.of_match_table	= tegra_csi_of_id_table,
		.pm		= &tegra_csi_pm_ops,
	},
	.probe			= tegra_csi_probe,
	.remove			= tegra_csi_remove,
};
