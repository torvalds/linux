// SPDX-License-Identifier: GPL-2.0-only
/*
 * Parallel video capture module (VIP) for the Tegra VI.
 *
 * This file implements the VIP-specific infrastructure.
 *
 * Copyright (C) 2023 SKIDATA GmbH
 * Author: Luca Ceresoli <luca.ceresoli@bootlin.com>
 */

#include <linux/device.h>
#include <linux/host1x.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include <media/v4l2-fwnode.h>

#include "vip.h"
#include "video.h"

static inline struct tegra_vip *host1x_client_to_vip(struct host1x_client *client)
{
	return container_of(client, struct tegra_vip, client);
}

static inline struct tegra_vip_channel *subdev_to_vip_channel(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct tegra_vip_channel, subdev);
}

static inline struct tegra_vip *vip_channel_to_vip(struct tegra_vip_channel *chan)
{
	return container_of(chan, struct tegra_vip, chan);
}

/* Find the previous subdev in the pipeline (i.e. the one connected to our sink pad) */
static struct v4l2_subdev *tegra_vip_channel_get_prev_subdev(struct tegra_vip_channel *chan)
{
	struct media_pad *remote_pad;

	remote_pad = media_pad_remote_pad_first(&chan->pads[TEGRA_VIP_PAD_SINK]);
	if (!remote_pad)
		return NULL;

	return media_entity_to_v4l2_subdev(remote_pad->entity);
}

static int tegra_vip_enable_stream(struct v4l2_subdev *subdev)
{
	struct tegra_vip_channel *vip_chan = subdev_to_vip_channel(subdev);
	struct tegra_vip *vip = vip_channel_to_vip(vip_chan);
	struct v4l2_subdev *prev_subdev = tegra_vip_channel_get_prev_subdev(vip_chan);
	int err;

	err = pm_runtime_resume_and_get(vip->dev);
	if (err)
		return dev_err_probe(vip->dev, err, "failed to get runtime PM\n");

	err = vip->soc->ops->vip_start_streaming(vip_chan);
	if (err < 0)
		goto err_start_streaming;

	err = v4l2_subdev_call(prev_subdev, video, s_stream, true);
	if (err < 0 && err != -ENOIOCTLCMD)
		goto err_prev_subdev_start_stream;

	return 0;

err_prev_subdev_start_stream:
err_start_streaming:
	pm_runtime_put(vip->dev);
	return err;
}

static int tegra_vip_disable_stream(struct v4l2_subdev *subdev)
{
	struct tegra_vip_channel *vip_chan = subdev_to_vip_channel(subdev);
	struct tegra_vip *vip = vip_channel_to_vip(vip_chan);
	struct v4l2_subdev *prev_subdev = tegra_vip_channel_get_prev_subdev(vip_chan);

	v4l2_subdev_call(prev_subdev, video, s_stream, false);

	pm_runtime_put(vip->dev);

	return 0;
}

static int tegra_vip_s_stream(struct v4l2_subdev *subdev, int enable)
{
	int err;

	if (enable)
		err = tegra_vip_enable_stream(subdev);
	else
		err = tegra_vip_disable_stream(subdev);

	return err;
}

static const struct v4l2_subdev_video_ops tegra_vip_video_ops = {
	.s_stream = tegra_vip_s_stream,
};

static const struct v4l2_subdev_ops tegra_vip_ops = {
	.video  = &tegra_vip_video_ops,
};

static int tegra_vip_channel_of_parse(struct tegra_vip *vip)
{
	struct device *dev = vip->dev;
	struct device_node *np = dev->of_node;
	struct v4l2_fwnode_endpoint v4l2_ep = {
		.bus_type = V4L2_MBUS_PARALLEL
	};
	struct fwnode_handle *fwh;
	struct device_node *ep;
	unsigned int num_pads;
	int err;

	dev_dbg(dev, "Parsing %pOF", np);

	ep = of_graph_get_endpoint_by_regs(np, 0, 0);
	if (!ep) {
		err = -EINVAL;
		dev_err_probe(dev, err, "%pOF: error getting endpoint node\n", np);
		goto err_node_put;
	}

	fwh = of_fwnode_handle(ep);
	err = v4l2_fwnode_endpoint_parse(fwh, &v4l2_ep);
	of_node_put(ep);
	if (err) {
		dev_err_probe(dev, err, "%pOF: failed to parse v4l2 endpoint\n", np);
		goto err_node_put;
	}

	num_pads = of_graph_get_endpoint_count(np);
	if (num_pads != TEGRA_VIP_PADS_NUM) {
		err = -EINVAL;
		dev_err_probe(dev, err, "%pOF: need 2 pads, got %d\n", np, num_pads);
		goto err_node_put;
	}

	vip->chan.of_node = of_node_get(np);
	vip->chan.pads[TEGRA_VIP_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	vip->chan.pads[TEGRA_VIP_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;

	return 0;

err_node_put:
	of_node_put(np);
	return err;
}

static int tegra_vip_channel_init(struct tegra_vip *vip)
{
	struct v4l2_subdev *subdev;
	int err;

	subdev = &vip->chan.subdev;
	v4l2_subdev_init(subdev, &tegra_vip_ops);
	subdev->dev = vip->dev;
	snprintf(subdev->name, sizeof(subdev->name), "%s",
		 kbasename(vip->chan.of_node->full_name));

	v4l2_set_subdevdata(subdev, &vip->chan);
	subdev->fwnode = of_fwnode_handle(vip->chan.of_node);
	subdev->entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;

	err = media_entity_pads_init(&subdev->entity, TEGRA_VIP_PADS_NUM, vip->chan.pads);
	if (err)
		return dev_err_probe(vip->dev, err, "failed to initialize media entity\n");

	err = v4l2_async_register_subdev(subdev);
	if (err) {
		dev_err_probe(vip->dev, err, "failed to register subdev\n");
		goto err_register_subdev;
	}

	return 0;

err_register_subdev:
	media_entity_cleanup(&subdev->entity);
	return err;
}

static int tegra_vip_init(struct host1x_client *client)
{
	struct tegra_vip *vip = host1x_client_to_vip(client);
	int err;

	err = tegra_vip_channel_of_parse(vip);
	if (err)
		return err;

	err = tegra_vip_channel_init(vip);
	if (err)
		goto err_init;

	return 0;

err_init:
	of_node_put(vip->chan.of_node);
	return err;
}

static int tegra_vip_exit(struct host1x_client *client)
{
	struct tegra_vip *vip = host1x_client_to_vip(client);
	struct v4l2_subdev *subdev = &vip->chan.subdev;

	v4l2_async_unregister_subdev(subdev);
	media_entity_cleanup(&subdev->entity);
	of_node_put(vip->chan.of_node);

	return 0;
}

static const struct host1x_client_ops vip_client_ops = {
	.init = tegra_vip_init,
	.exit = tegra_vip_exit,
};

static int tegra_vip_probe(struct platform_device *pdev)
{
	struct tegra_vip *vip;
	int err;

	dev_dbg(&pdev->dev, "Probing VIP \"%s\" from %pOF\n", pdev->name, pdev->dev.of_node);

	vip = devm_kzalloc(&pdev->dev, sizeof(*vip), GFP_KERNEL);
	if (!vip)
		return -ENOMEM;

	vip->soc = of_device_get_match_data(&pdev->dev);

	vip->dev = &pdev->dev;
	platform_set_drvdata(pdev, vip);

	/* initialize host1x interface */
	INIT_LIST_HEAD(&vip->client.list);
	vip->client.ops = &vip_client_ops;
	vip->client.dev = &pdev->dev;

	err = host1x_client_register(&vip->client);
	if (err)
		return dev_err_probe(&pdev->dev, err, "failed to register host1x client\n");

	pm_runtime_enable(&pdev->dev);

	return 0;
}

static int tegra_vip_remove(struct platform_device *pdev)
{
	struct tegra_vip *vip = platform_get_drvdata(pdev);

	host1x_client_unregister(&vip->client);

	pm_runtime_disable(&pdev->dev);

	return 0;
}

#if defined(CONFIG_ARCH_TEGRA_2x_SOC)
extern const struct tegra_vip_soc tegra20_vip_soc;
#endif

static const struct of_device_id tegra_vip_of_id_table[] = {
#if defined(CONFIG_ARCH_TEGRA_2x_SOC)
	{ .compatible = "nvidia,tegra20-vip", .data = &tegra20_vip_soc },
#endif
	{ }
};
MODULE_DEVICE_TABLE(of, tegra_vip_of_id_table);

struct platform_driver tegra_vip_driver = {
	.driver = {
		.name		= "tegra-vip",
		.of_match_table	= tegra_vip_of_id_table,
	},
	.probe			= tegra_vip_probe,
	.remove			= tegra_vip_remove,
};
