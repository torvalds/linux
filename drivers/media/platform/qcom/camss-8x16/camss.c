/*
 * camss.c
 *
 * Qualcomm MSM Camera Subsystem - Core
 *
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 * Copyright (C) 2015-2017 Linaro Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/clk.h>
#include <linux/media-bus-format.h>
#include <linux/media.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/slab.h>
#include <linux/videodev2.h>

#include <media/media-device.h>
#include <media/v4l2-async.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mc.h>
#include <media/v4l2-fwnode.h>

#include "camss.h"

#define CAMSS_CLOCK_MARGIN_NUMERATOR 105
#define CAMSS_CLOCK_MARGIN_DENOMINATOR 100

static const struct resources csiphy_res[] = {
	/* CSIPHY0 */
	{
		.regulator = { NULL },
		.clock = { "camss_top_ahb", "ispif_ahb",
			   "camss_ahb", "csiphy0_timer" },
		.clock_rate = { { 0 },
				{ 0 },
				{ 0 },
				{ 100000000, 200000000 } },
		.reg = { "csiphy0", "csiphy0_clk_mux" },
		.interrupt = { "csiphy0" }
	},

	/* CSIPHY1 */
	{
		.regulator = { NULL },
		.clock = { "camss_top_ahb", "ispif_ahb",
			   "camss_ahb", "csiphy1_timer" },
		.clock_rate = { { 0 },
				{ 0 },
				{ 0 },
				{ 100000000, 200000000 } },
		.reg = { "csiphy1", "csiphy1_clk_mux" },
		.interrupt = { "csiphy1" }
	}
};

static const struct resources csid_res[] = {
	/* CSID0 */
	{
		.regulator = { "vdda" },
		.clock = { "camss_top_ahb", "ispif_ahb",
			   "csi0_ahb", "camss_ahb",
			   "csi0", "csi0_phy", "csi0_pix", "csi0_rdi" },
		.clock_rate = { { 0 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 100000000, 200000000 },
				{ 0 },
				{ 0 },
				{ 0 } },
		.reg = { "csid0" },
		.interrupt = { "csid0" }
	},

	/* CSID1 */
	{
		.regulator = { "vdda" },
		.clock = { "camss_top_ahb", "ispif_ahb",
			   "csi1_ahb", "camss_ahb",
			   "csi1", "csi1_phy", "csi1_pix", "csi1_rdi" },
		.clock_rate = { { 0 },
				{ 0 },
				{ 0 },
				{ 0 },
				{ 100000000, 200000000 },
				{ 0 },
				{ 0 },
				{ 0 } },
		.reg = { "csid1" },
		.interrupt = { "csid1" }
	},
};

static const struct resources_ispif ispif_res = {
	/* ISPIF */
	.clock = { "camss_top_ahb", "camss_ahb", "ispif_ahb",
		   "csi0", "csi0_pix", "csi0_rdi",
		   "csi1", "csi1_pix", "csi1_rdi" },
	.clock_for_reset = { "camss_vfe_vfe", "camss_csi_vfe" },
	.reg = { "ispif", "csi_clk_mux" },
	.interrupt = "ispif"

};

static const struct resources vfe_res = {
	/* VFE0 */
	.regulator = { NULL },
	.clock = { "camss_top_ahb", "camss_vfe_vfe", "camss_csi_vfe",
		   "iface", "bus", "camss_ahb" },
	.clock_rate = { { 0 },
			{ 50000000, 80000000, 100000000, 160000000,
			  177780000, 200000000, 266670000, 320000000,
			  400000000, 465000000 },
			{ 0 },
			{ 0 },
			{ 0 },
			{ 0 },
			{ 0 },
			{ 0 },
			{ 0 } },
	.reg = { "vfe0" },
	.interrupt = { "vfe0" }
};

/*
 * camss_add_clock_margin - Add margin to clock frequency rate
 * @rate: Clock frequency rate
 *
 * When making calculations with physical clock frequency values
 * some safety margin must be added. Add it.
 */
inline void camss_add_clock_margin(u64 *rate)
{
	*rate *= CAMSS_CLOCK_MARGIN_NUMERATOR;
	*rate = div_u64(*rate, CAMSS_CLOCK_MARGIN_DENOMINATOR);
}

/*
 * camss_enable_clocks - Enable multiple clocks
 * @nclocks: Number of clocks in clock array
 * @clock: Clock array
 * @dev: Device
 *
 * Return 0 on success or a negative error code otherwise
 */
int camss_enable_clocks(int nclocks, struct camss_clock *clock,
			struct device *dev)
{
	int ret;
	int i;

	for (i = 0; i < nclocks; i++) {
		ret = clk_prepare_enable(clock[i].clk);
		if (ret) {
			dev_err(dev, "clock enable failed: %d\n", ret);
			goto error;
		}
	}

	return 0;

error:
	for (i--; i >= 0; i--)
		clk_disable_unprepare(clock[i].clk);

	return ret;
}

/*
 * camss_disable_clocks - Disable multiple clocks
 * @nclocks: Number of clocks in clock array
 * @clock: Clock array
 */
void camss_disable_clocks(int nclocks, struct camss_clock *clock)
{
	int i;

	for (i = nclocks - 1; i >= 0; i--)
		clk_disable_unprepare(clock[i].clk);
}

/*
 * camss_find_sensor - Find a linked media entity which represents a sensor
 * @entity: Media entity to start searching from
 *
 * Return a pointer to sensor media entity or NULL if not found
 */
static struct media_entity *camss_find_sensor(struct media_entity *entity)
{
	struct media_pad *pad;

	while (1) {
		pad = &entity->pads[0];
		if (!(pad->flags & MEDIA_PAD_FL_SINK))
			return NULL;

		pad = media_entity_remote_pad(pad);
		if (!pad || !is_media_entity_v4l2_subdev(pad->entity))
			return NULL;

		entity = pad->entity;

		if (entity->function == MEDIA_ENT_F_CAM_SENSOR)
			return entity;
	}
}

/*
 * camss_get_pixel_clock - Get pixel clock rate from sensor
 * @entity: Media entity in the current pipeline
 * @pixel_clock: Received pixel clock value
 *
 * Return 0 on success or a negative error code otherwise
 */
int camss_get_pixel_clock(struct media_entity *entity, u32 *pixel_clock)
{
	struct media_entity *sensor;
	struct v4l2_subdev *subdev;
	struct v4l2_ctrl *ctrl;

	sensor = camss_find_sensor(entity);
	if (!sensor)
		return -ENODEV;

	subdev = media_entity_to_v4l2_subdev(sensor);

	ctrl = v4l2_ctrl_find(subdev->ctrl_handler, V4L2_CID_PIXEL_RATE);

	if (!ctrl)
		return -EINVAL;

	*pixel_clock = v4l2_ctrl_g_ctrl_int64(ctrl);

	return 0;
}

/*
 * camss_of_parse_endpoint_node - Parse port endpoint node
 * @dev: Device
 * @node: Device node to be parsed
 * @csd: Parsed data from port endpoint node
 *
 * Return 0 on success or a negative error code on failure
 */
static int camss_of_parse_endpoint_node(struct device *dev,
					struct device_node *node,
					struct camss_async_subdev *csd)
{
	struct csiphy_lanes_cfg *lncfg = &csd->interface.csi2.lane_cfg;
	struct v4l2_fwnode_bus_mipi_csi2 *mipi_csi2;
	struct v4l2_fwnode_endpoint vep = { { 0 } };
	unsigned int i;

	v4l2_fwnode_endpoint_parse(of_fwnode_handle(node), &vep);

	csd->interface.csiphy_id = vep.base.port;

	mipi_csi2 = &vep.bus.mipi_csi2;
	lncfg->clk.pos = mipi_csi2->clock_lane;
	lncfg->clk.pol = mipi_csi2->lane_polarities[0];
	lncfg->num_data = mipi_csi2->num_data_lanes;

	lncfg->data = devm_kzalloc(dev, lncfg->num_data * sizeof(*lncfg->data),
				   GFP_KERNEL);
	if (!lncfg->data)
		return -ENOMEM;

	for (i = 0; i < lncfg->num_data; i++) {
		lncfg->data[i].pos = mipi_csi2->data_lanes[i];
		lncfg->data[i].pol = mipi_csi2->lane_polarities[i + 1];
	}

	return 0;
}

/*
 * camss_of_parse_ports - Parse ports node
 * @dev: Device
 * @notifier: v4l2_device notifier data
 *
 * Return number of "port" nodes found in "ports" node
 */
static int camss_of_parse_ports(struct device *dev,
				struct v4l2_async_notifier *notifier)
{
	struct device_node *node = NULL;
	struct device_node *remote = NULL;
	unsigned int size, i;
	int ret;

	while ((node = of_graph_get_next_endpoint(dev->of_node, node)))
		if (of_device_is_available(node))
			notifier->num_subdevs++;

	size = sizeof(*notifier->subdevs) * notifier->num_subdevs;
	notifier->subdevs = devm_kzalloc(dev, size, GFP_KERNEL);
	if (!notifier->subdevs) {
		dev_err(dev, "Failed to allocate memory\n");
		return -ENOMEM;
	}

	i = 0;
	while ((node = of_graph_get_next_endpoint(dev->of_node, node))) {
		struct camss_async_subdev *csd;

		if (!of_device_is_available(node))
			continue;

		csd = devm_kzalloc(dev, sizeof(*csd), GFP_KERNEL);
		if (!csd) {
			of_node_put(node);
			dev_err(dev, "Failed to allocate memory\n");
			return -ENOMEM;
		}

		notifier->subdevs[i++] = &csd->asd;

		ret = camss_of_parse_endpoint_node(dev, node, csd);
		if (ret < 0) {
			of_node_put(node);
			return ret;
		}

		remote = of_graph_get_remote_port_parent(node);
		of_node_put(node);

		if (!remote) {
			dev_err(dev, "Cannot get remote parent\n");
			return -EINVAL;
		}

		csd->asd.match_type = V4L2_ASYNC_MATCH_FWNODE;
		csd->asd.match.fwnode.fwnode = of_fwnode_handle(remote);
	}

	return notifier->num_subdevs;
}

/*
 * camss_init_subdevices - Initialize subdev structures and resources
 * @camss: CAMSS device
 *
 * Return 0 on success or a negative error code on failure
 */
static int camss_init_subdevices(struct camss *camss)
{
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(camss->csiphy); i++) {
		ret = msm_csiphy_subdev_init(&camss->csiphy[i],
					     &csiphy_res[i], i);
		if (ret < 0) {
			dev_err(camss->dev,
				"Failed to init csiphy%d sub-device: %d\n",
				i, ret);
			return ret;
		}
	}

	for (i = 0; i < ARRAY_SIZE(camss->csid); i++) {
		ret = msm_csid_subdev_init(&camss->csid[i],
					   &csid_res[i], i);
		if (ret < 0) {
			dev_err(camss->dev,
				"Failed to init csid%d sub-device: %d\n",
				i, ret);
			return ret;
		}
	}

	ret = msm_ispif_subdev_init(&camss->ispif, &ispif_res);
	if (ret < 0) {
		dev_err(camss->dev, "Failed to init ispif sub-device: %d\n",
			ret);
		return ret;
	}

	ret = msm_vfe_subdev_init(&camss->vfe, &vfe_res);
	if (ret < 0) {
		dev_err(camss->dev, "Fail to init vfe sub-device: %d\n", ret);
		return ret;
	}

	return 0;
}

/*
 * camss_register_entities - Register subdev nodes and create links
 * @camss: CAMSS device
 *
 * Return 0 on success or a negative error code on failure
 */
static int camss_register_entities(struct camss *camss)
{
	int i, j;
	int ret;

	for (i = 0; i < ARRAY_SIZE(camss->csiphy); i++) {
		ret = msm_csiphy_register_entity(&camss->csiphy[i],
						 &camss->v4l2_dev);
		if (ret < 0) {
			dev_err(camss->dev,
				"Failed to register csiphy%d entity: %d\n",
				i, ret);
			goto err_reg_csiphy;
		}
	}

	for (i = 0; i < ARRAY_SIZE(camss->csid); i++) {
		ret = msm_csid_register_entity(&camss->csid[i],
					       &camss->v4l2_dev);
		if (ret < 0) {
			dev_err(camss->dev,
				"Failed to register csid%d entity: %d\n",
				i, ret);
			goto err_reg_csid;
		}
	}

	ret = msm_ispif_register_entities(&camss->ispif, &camss->v4l2_dev);
	if (ret < 0) {
		dev_err(camss->dev, "Failed to register ispif entities: %d\n",
			ret);
		goto err_reg_ispif;
	}

	ret = msm_vfe_register_entities(&camss->vfe, &camss->v4l2_dev);
	if (ret < 0) {
		dev_err(camss->dev, "Failed to register vfe entities: %d\n",
			ret);
		goto err_reg_vfe;
	}

	for (i = 0; i < ARRAY_SIZE(camss->csiphy); i++) {
		for (j = 0; j < ARRAY_SIZE(camss->csid); j++) {
			ret = media_create_pad_link(
				&camss->csiphy[i].subdev.entity,
				MSM_CSIPHY_PAD_SRC,
				&camss->csid[j].subdev.entity,
				MSM_CSID_PAD_SINK,
				0);
			if (ret < 0) {
				dev_err(camss->dev,
					"Failed to link %s->%s entities: %d\n",
					camss->csiphy[i].subdev.entity.name,
					camss->csid[j].subdev.entity.name,
					ret);
				goto err_link;
			}
		}
	}

	for (i = 0; i < ARRAY_SIZE(camss->csid); i++) {
		for (j = 0; j < ARRAY_SIZE(camss->ispif.line); j++) {
			ret = media_create_pad_link(
				&camss->csid[i].subdev.entity,
				MSM_CSID_PAD_SRC,
				&camss->ispif.line[j].subdev.entity,
				MSM_ISPIF_PAD_SINK,
				0);
			if (ret < 0) {
				dev_err(camss->dev,
					"Failed to link %s->%s entities: %d\n",
					camss->csid[i].subdev.entity.name,
					camss->ispif.line[j].subdev.entity.name,
					ret);
				goto err_link;
			}
		}
	}

	for (i = 0; i < ARRAY_SIZE(camss->ispif.line); i++) {
		for (j = 0; j < ARRAY_SIZE(camss->vfe.line); j++) {
			ret = media_create_pad_link(
				&camss->ispif.line[i].subdev.entity,
				MSM_ISPIF_PAD_SRC,
				&camss->vfe.line[j].subdev.entity,
				MSM_VFE_PAD_SINK,
				0);
			if (ret < 0) {
				dev_err(camss->dev,
					"Failed to link %s->%s entities: %d\n",
					camss->ispif.line[i].subdev.entity.name,
					camss->vfe.line[j].subdev.entity.name,
					ret);
				goto err_link;
			}
		}
	}

	return 0;

err_link:
	msm_vfe_unregister_entities(&camss->vfe);
err_reg_vfe:
	msm_ispif_unregister_entities(&camss->ispif);
err_reg_ispif:

	i = ARRAY_SIZE(camss->csid);
err_reg_csid:
	for (i--; i >= 0; i--)
		msm_csid_unregister_entity(&camss->csid[i]);

	i = ARRAY_SIZE(camss->csiphy);
err_reg_csiphy:
	for (i--; i >= 0; i--)
		msm_csiphy_unregister_entity(&camss->csiphy[i]);

	return ret;
}

/*
 * camss_unregister_entities - Unregister subdev nodes
 * @camss: CAMSS device
 *
 * Return 0 on success or a negative error code on failure
 */
static void camss_unregister_entities(struct camss *camss)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(camss->csiphy); i++)
		msm_csiphy_unregister_entity(&camss->csiphy[i]);

	for (i = 0; i < ARRAY_SIZE(camss->csid); i++)
		msm_csid_unregister_entity(&camss->csid[i]);

	msm_ispif_unregister_entities(&camss->ispif);
	msm_vfe_unregister_entities(&camss->vfe);
}

static int camss_subdev_notifier_bound(struct v4l2_async_notifier *async,
				       struct v4l2_subdev *subdev,
				       struct v4l2_async_subdev *asd)
{
	struct camss *camss = container_of(async, struct camss, notifier);
	struct camss_async_subdev *csd =
		container_of(asd, struct camss_async_subdev, asd);
	u8 id = csd->interface.csiphy_id;
	struct csiphy_device *csiphy = &camss->csiphy[id];

	csiphy->cfg.csi2 = &csd->interface.csi2;
	subdev->host_priv = csiphy;

	return 0;
}

static int camss_subdev_notifier_complete(struct v4l2_async_notifier *async)
{
	struct camss *camss = container_of(async, struct camss, notifier);
	struct v4l2_device *v4l2_dev = &camss->v4l2_dev;
	struct v4l2_subdev *sd;
	int ret;

	list_for_each_entry(sd, &v4l2_dev->subdevs, list) {
		if (sd->host_priv) {
			struct media_entity *sensor = &sd->entity;
			struct csiphy_device *csiphy =
					(struct csiphy_device *) sd->host_priv;
			struct media_entity *input = &csiphy->subdev.entity;
			unsigned int i;

			for (i = 0; i < sensor->num_pads; i++) {
				if (sensor->pads[i].flags & MEDIA_PAD_FL_SOURCE)
					break;
			}
			if (i == sensor->num_pads) {
				dev_err(camss->dev,
					"No source pad in external entity\n");
				return -EINVAL;
			}

			ret = media_create_pad_link(sensor, i,
				input, MSM_CSIPHY_PAD_SINK,
				MEDIA_LNK_FL_IMMUTABLE | MEDIA_LNK_FL_ENABLED);
			if (ret < 0) {
				dev_err(camss->dev,
					"Failed to link %s->%s entities: %d\n",
					sensor->name, input->name, ret);
				return ret;
			}
		}
	}

	ret = v4l2_device_register_subdev_nodes(&camss->v4l2_dev);
	if (ret < 0)
		return ret;

	return media_device_register(&camss->media_dev);
}

static const struct media_device_ops camss_media_ops = {
	.link_notify = v4l2_pipeline_link_notify,
};

/*
 * camss_probe - Probe CAMSS platform device
 * @pdev: Pointer to CAMSS platform device
 *
 * Return 0 on success or a negative error code on failure
 */
static int camss_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct camss *camss;
	int ret;

	camss = kzalloc(sizeof(*camss), GFP_KERNEL);
	if (!camss)
		return -ENOMEM;

	atomic_set(&camss->ref_count, 0);
	camss->dev = dev;
	platform_set_drvdata(pdev, camss);

	ret = camss_of_parse_ports(dev, &camss->notifier);
	if (ret < 0)
		return ret;

	ret = camss_init_subdevices(camss);
	if (ret < 0)
		return ret;

	ret = dma_set_mask_and_coherent(dev, 0xffffffff);
	if (ret)
		return ret;

	camss->media_dev.dev = camss->dev;
	strlcpy(camss->media_dev.model, "Qualcomm Camera Subsystem",
		sizeof(camss->media_dev.model));
	camss->media_dev.ops = &camss_media_ops;
	media_device_init(&camss->media_dev);

	camss->v4l2_dev.mdev = &camss->media_dev;
	ret = v4l2_device_register(camss->dev, &camss->v4l2_dev);
	if (ret < 0) {
		dev_err(dev, "Failed to register V4L2 device: %d\n", ret);
		return ret;
	}

	ret = camss_register_entities(camss);
	if (ret < 0)
		goto err_register_entities;

	if (camss->notifier.num_subdevs) {
		camss->notifier.bound = camss_subdev_notifier_bound;
		camss->notifier.complete = camss_subdev_notifier_complete;

		ret = v4l2_async_notifier_register(&camss->v4l2_dev,
						   &camss->notifier);
		if (ret) {
			dev_err(dev,
				"Failed to register async subdev nodes: %d\n",
				ret);
			goto err_register_subdevs;
		}
	} else {
		ret = v4l2_device_register_subdev_nodes(&camss->v4l2_dev);
		if (ret < 0) {
			dev_err(dev, "Failed to register subdev nodes: %d\n",
				ret);
			goto err_register_subdevs;
		}

		ret = media_device_register(&camss->media_dev);
		if (ret < 0) {
			dev_err(dev, "Failed to register media device: %d\n",
				ret);
			goto err_register_subdevs;
		}
	}

	return 0;

err_register_subdevs:
	camss_unregister_entities(camss);
err_register_entities:
	v4l2_device_unregister(&camss->v4l2_dev);

	return ret;
}

void camss_delete(struct camss *camss)
{
	v4l2_device_unregister(&camss->v4l2_dev);
	media_device_unregister(&camss->media_dev);
	media_device_cleanup(&camss->media_dev);

	kfree(camss);
}

/*
 * camss_remove - Remove CAMSS platform device
 * @pdev: Pointer to CAMSS platform device
 *
 * Always returns 0.
 */
static int camss_remove(struct platform_device *pdev)
{
	struct camss *camss = platform_get_drvdata(pdev);

	msm_vfe_stop_streaming(&camss->vfe);

	v4l2_async_notifier_unregister(&camss->notifier);
	camss_unregister_entities(camss);

	if (atomic_read(&camss->ref_count) == 0)
		camss_delete(camss);

	return 0;
}

static const struct of_device_id camss_dt_match[] = {
	{ .compatible = "qcom,msm8916-camss" },
	{ }
};

MODULE_DEVICE_TABLE(of, camss_dt_match);

static struct platform_driver qcom_camss_driver = {
	.probe = camss_probe,
	.remove = camss_remove,
	.driver = {
		.name = "qcom-camss",
		.of_match_table = camss_dt_match,
	},
};

module_platform_driver(qcom_camss_driver);

MODULE_ALIAS("platform:qcom-camss");
MODULE_DESCRIPTION("Qualcomm Camera Subsystem driver");
MODULE_AUTHOR("Todor Tomov <todor.tomov@linaro.org>");
MODULE_LICENSE("GPL v2");
