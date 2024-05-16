// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Rockchip ISP1 Driver - Base driver
 *
 * Copyright (C) 2019 Collabora, Ltd.
 *
 * Based on Rockchip ISP1 driver by Rockchip Electronics Co., Ltd.
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd.
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pm_runtime.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mc.h>

#include "rkisp1-common.h"
#include "rkisp1-csi.h"

/*
 * ISP Details
 * -----------
 *
 * ISP Comprises with:
 *	MIPI serial camera interface
 *	Image Signal Processing
 *	Many Image Enhancement Blocks
 *	Crop
 *	Resizer
 *	RBG display ready image
 *	Image Rotation
 *
 * ISP Block Diagram
 * -----------------
 *                                                             rkisp1-resizer.c          rkisp1-capture.c
 *                                                          |====================|  |=======================|
 *                                rkisp1-isp.c                              Main Picture Path
 *                        |==========================|      |===============================================|
 *                        +-----------+  +--+--+--+--+      +--------+  +--------+              +-----------+
 *                        |           |  |  |  |  |  |      |        |  |        |              |           |
 * +--------+    |\       |           |  |  |  |  |  |   -->|  Crop  |->|  RSZ   |------------->|           |
 * |  MIPI  |--->|  \     |           |  |  |  |  |  |   |  |        |  |        |              |           |
 * +--------+    |   |    |           |  |IE|IE|IE|IE|   |  +--------+  +--------+              |  Memory   |
 *               |MUX|--->|    ISP    |->|0 |1 |2 |3 |---+                                      | Interface |
 * +--------+    |   |    |           |  |  |  |  |  |   |  +--------+  +--------+  +--------+  |           |
 * |Parallel|--->|  /     |           |  |  |  |  |  |   |  |        |  |        |  |        |  |           |
 * +--------+    |/       |           |  |  |  |  |  |   -->|  Crop  |->|  RSZ   |->|  RGB   |->|           |
 *                        |           |  |  |  |  |  |      |        |  |        |  | Rotate |  |           |
 *                        +-----------+  +--+--+--+--+      +--------+  +--------+  +--------+  +-----------+
 *                                               ^
 * +--------+                                    |          |===============================================|
 * |  DMA   |------------------------------------+                          Self Picture Path
 * +--------+
 *
 *         rkisp1-stats.c        rkisp1-params.c
 *       |===============|      |===============|
 *       +---------------+      +---------------+
 *       |               |      |               |
 *       |      ISP      |      |      ISP      |
 *       |               |      |               |
 *       +---------------+      +---------------+
 *
 *
 * Media Topology
 * --------------
 *
 *          +----------+       +----------+
 *          | Sensor 1 |       | Sensor X |
 *          ------------  ...  ------------
 *          |    0     |       |    0     |
 *          +----------+       +----------+
 *               |                  |
 *                \----\       /----/
 *                     |       |
 *                     v       v
 *                  +-------------+
 *                  |      0      |
 *                  ---------------
 *                  |  CSI-2 RX   |
 *                  ---------------         +-----------+
 *                  |      1      |         |  params   |
 *                  +-------------+         | (output)  |
 *                         |               +-----------+
 *                         v                     |
 *                      +------+------+          |
 *                      |  0   |  1   |<---------+
 *                      |------+------|
 *                      |     ISP     |
 *                      |------+------|
 *        +-------------|  2   |  3   |----------+
 *        |             +------+------+          |
 *        |                |                     |
 *        v                v                     v
 *  +- ---------+    +-----------+         +-----------+
 *  |     0     |    |     0     |         |   stats   |
 *  -------------    -------------         | (capture) |
 *  |  Resizer  |    |  Resizer  |         +-----------+
 *  ------------|    ------------|
 *  |     1     |    |     1     |
 *  +-----------+    +-----------+
 *        |                |
 *        v                v
 *  +-----------+    +-----------+
 *  | selfpath  |    | mainpath  |
 *  | (capture) |    | (capture) |
 *  +-----------+    +-----------+
 */

struct rkisp1_isr_data {
	const char *name;
	irqreturn_t (*isr)(int irq, void *ctx);
	u32 line_mask;
};

/* ----------------------------------------------------------------------------
 * Sensor DT bindings
 */

static int rkisp1_subdev_notifier_bound(struct v4l2_async_notifier *notifier,
					struct v4l2_subdev *sd,
					struct v4l2_async_connection *asc)
{
	struct rkisp1_device *rkisp1 =
		container_of(notifier, struct rkisp1_device, notifier);
	struct rkisp1_sensor_async *s_asd =
		container_of(asc, struct rkisp1_sensor_async, asd);
	int source_pad;
	int ret;

	s_asd->sd = sd;

	source_pad = media_entity_get_fwnode_pad(&sd->entity, s_asd->source_ep,
						 MEDIA_PAD_FL_SOURCE);
	if (source_pad < 0) {
		dev_err(rkisp1->dev, "failed to find source pad for %s\n",
			sd->name);
		return source_pad;
	}

	if (s_asd->port == 0)
		return rkisp1_csi_link_sensor(rkisp1, sd, s_asd, source_pad);

	ret = media_create_pad_link(&sd->entity, source_pad,
				    &rkisp1->isp.sd.entity,
				    RKISP1_ISP_PAD_SINK_VIDEO,
				    !s_asd->index ? MEDIA_LNK_FL_ENABLED : 0);
	if (ret) {
		dev_err(rkisp1->dev, "failed to link source pad of %s\n",
			sd->name);
		return ret;
	}

	return 0;
}

static int rkisp1_subdev_notifier_complete(struct v4l2_async_notifier *notifier)
{
	struct rkisp1_device *rkisp1 =
		container_of(notifier, struct rkisp1_device, notifier);

	return v4l2_device_register_subdev_nodes(&rkisp1->v4l2_dev);
}

static void rkisp1_subdev_notifier_destroy(struct v4l2_async_connection *asc)
{
	struct rkisp1_sensor_async *rk_asd =
		container_of(asc, struct rkisp1_sensor_async, asd);

	fwnode_handle_put(rk_asd->source_ep);
}

static const struct v4l2_async_notifier_operations rkisp1_subdev_notifier_ops = {
	.bound = rkisp1_subdev_notifier_bound,
	.complete = rkisp1_subdev_notifier_complete,
	.destroy = rkisp1_subdev_notifier_destroy,
};

static int rkisp1_subdev_notifier_register(struct rkisp1_device *rkisp1)
{
	struct v4l2_async_notifier *ntf = &rkisp1->notifier;
	struct fwnode_handle *fwnode = dev_fwnode(rkisp1->dev);
	struct fwnode_handle *ep;
	unsigned int index = 0;
	int ret = 0;

	v4l2_async_nf_init(ntf, &rkisp1->v4l2_dev);

	ntf->ops = &rkisp1_subdev_notifier_ops;

	fwnode_graph_for_each_endpoint(fwnode, ep) {
		struct fwnode_handle *port;
		struct v4l2_fwnode_endpoint vep = { };
		struct rkisp1_sensor_async *rk_asd;
		struct fwnode_handle *source;
		u32 reg = 0;

		/* Select the bus type based on the port. */
		port = fwnode_get_parent(ep);
		fwnode_property_read_u32(port, "reg", &reg);
		fwnode_handle_put(port);

		switch (reg) {
		case 0:
			/* MIPI CSI-2 port */
			if (!rkisp1_has_feature(rkisp1, MIPI_CSI2)) {
				dev_err(rkisp1->dev,
					"internal CSI must be available for port 0\n");
				ret = -EINVAL;
				break;
			}

			vep.bus_type = V4L2_MBUS_CSI2_DPHY;
			break;

		case 1:
			/*
			 * Parallel port. The bus-type property in DT is
			 * mandatory for port 1, it will be used to determine if
			 * it's PARALLEL or BT656.
			 */
			vep.bus_type = V4L2_MBUS_UNKNOWN;
			break;
		}

		/* Parse the endpoint and validate the bus type. */
		ret = v4l2_fwnode_endpoint_parse(ep, &vep);
		if (ret) {
			dev_err(rkisp1->dev, "failed to parse endpoint %pfw\n",
				ep);
			break;
		}

		if (vep.base.port == 1) {
			if (vep.bus_type != V4L2_MBUS_PARALLEL &&
			    vep.bus_type != V4L2_MBUS_BT656) {
				dev_err(rkisp1->dev,
					"port 1 must be parallel or BT656\n");
				ret = -EINVAL;
				break;
			}
		}

		/* Add the async subdev to the notifier. */
		source = fwnode_graph_get_remote_endpoint(ep);
		if (!source) {
			dev_err(rkisp1->dev,
				"endpoint %pfw has no remote endpoint\n",
				ep);
			ret = -ENODEV;
			break;
		}

		rk_asd = v4l2_async_nf_add_fwnode(ntf, source,
						  struct rkisp1_sensor_async);
		if (IS_ERR(rk_asd)) {
			fwnode_handle_put(source);
			ret = PTR_ERR(rk_asd);
			break;
		}

		rk_asd->index = index++;
		rk_asd->source_ep = source;
		rk_asd->mbus_type = vep.bus_type;
		rk_asd->port = vep.base.port;

		if (vep.bus_type == V4L2_MBUS_CSI2_DPHY) {
			rk_asd->mbus_flags = vep.bus.mipi_csi2.flags;
			rk_asd->lanes = vep.bus.mipi_csi2.num_data_lanes;
		} else {
			rk_asd->mbus_flags = vep.bus.parallel.flags;
		}

		dev_dbg(rkisp1->dev, "registered ep id %d, bus type %u, %u lanes\n",
			vep.base.id, rk_asd->mbus_type, rk_asd->lanes);
	}

	if (ret) {
		fwnode_handle_put(ep);
		v4l2_async_nf_cleanup(ntf);
		return ret;
	}

	if (!index)
		dev_dbg(rkisp1->dev, "no remote subdevice found\n");

	ret = v4l2_async_nf_register(ntf);
	if (ret) {
		v4l2_async_nf_cleanup(ntf);
		return ret;
	}

	return 0;
}

/* ----------------------------------------------------------------------------
 * Power
 */

static int __maybe_unused rkisp1_runtime_suspend(struct device *dev)
{
	struct rkisp1_device *rkisp1 = dev_get_drvdata(dev);

	rkisp1->irqs_enabled = false;
	/* Make sure the IRQ handler will see the above */
	mb();

	/*
	 * Wait until any running IRQ handler has returned. The IRQ handler
	 * may get called even after this (as it's a shared interrupt line)
	 * but the 'irqs_enabled' flag will make the handler return immediately.
	 */
	for (unsigned int il = 0; il < ARRAY_SIZE(rkisp1->irqs); ++il) {
		if (rkisp1->irqs[il] == -1)
			continue;

		/* Skip if the irq line is the same as previous */
		if (il == 0 || rkisp1->irqs[il - 1] != rkisp1->irqs[il])
			synchronize_irq(rkisp1->irqs[il]);
	}

	clk_bulk_disable_unprepare(rkisp1->clk_size, rkisp1->clks);
	return pinctrl_pm_select_sleep_state(dev);
}

static int __maybe_unused rkisp1_runtime_resume(struct device *dev)
{
	struct rkisp1_device *rkisp1 = dev_get_drvdata(dev);
	int ret;

	ret = pinctrl_pm_select_default_state(dev);
	if (ret)
		return ret;
	ret = clk_bulk_prepare_enable(rkisp1->clk_size, rkisp1->clks);
	if (ret)
		return ret;

	rkisp1->irqs_enabled = true;
	/* Make sure the IRQ handler will see the above */
	mb();

	return 0;
}

static const struct dev_pm_ops rkisp1_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(rkisp1_runtime_suspend, rkisp1_runtime_resume, NULL)
};

/* ----------------------------------------------------------------------------
 * Core
 */

static int rkisp1_create_links(struct rkisp1_device *rkisp1)
{
	unsigned int dev_count = rkisp1_path_count(rkisp1);
	unsigned int i;
	int ret;

	if (rkisp1_has_feature(rkisp1, MIPI_CSI2)) {
		/* Link the CSI receiver to the ISP. */
		ret = media_create_pad_link(&rkisp1->csi.sd.entity,
					    RKISP1_CSI_PAD_SRC,
					    &rkisp1->isp.sd.entity,
					    RKISP1_ISP_PAD_SINK_VIDEO,
					    MEDIA_LNK_FL_ENABLED);
		if (ret)
			return ret;
	}

	/* create ISP->RSZ->CAP links */
	for (i = 0; i < dev_count; i++) {
		struct media_entity *resizer =
			&rkisp1->resizer_devs[i].sd.entity;
		struct media_entity *capture =
			&rkisp1->capture_devs[i].vnode.vdev.entity;

		ret = media_create_pad_link(&rkisp1->isp.sd.entity,
					    RKISP1_ISP_PAD_SOURCE_VIDEO,
					    resizer, RKISP1_RSZ_PAD_SINK,
					    MEDIA_LNK_FL_ENABLED);
		if (ret)
			return ret;

		ret = media_create_pad_link(resizer, RKISP1_RSZ_PAD_SRC,
					    capture, 0,
					    MEDIA_LNK_FL_ENABLED |
					    MEDIA_LNK_FL_IMMUTABLE);
		if (ret)
			return ret;
	}

	/* params links */
	ret = media_create_pad_link(&rkisp1->params.vnode.vdev.entity, 0,
				    &rkisp1->isp.sd.entity,
				    RKISP1_ISP_PAD_SINK_PARAMS,
				    MEDIA_LNK_FL_ENABLED |
				    MEDIA_LNK_FL_IMMUTABLE);
	if (ret)
		return ret;

	/* 3A stats links */
	return media_create_pad_link(&rkisp1->isp.sd.entity,
				     RKISP1_ISP_PAD_SOURCE_STATS,
				     &rkisp1->stats.vnode.vdev.entity, 0,
				     MEDIA_LNK_FL_ENABLED |
				     MEDIA_LNK_FL_IMMUTABLE);
}

static void rkisp1_entities_unregister(struct rkisp1_device *rkisp1)
{
	if (rkisp1_has_feature(rkisp1, MIPI_CSI2))
		rkisp1_csi_unregister(rkisp1);
	rkisp1_params_unregister(rkisp1);
	rkisp1_stats_unregister(rkisp1);
	rkisp1_capture_devs_unregister(rkisp1);
	rkisp1_resizer_devs_unregister(rkisp1);
	rkisp1_isp_unregister(rkisp1);
}

static int rkisp1_entities_register(struct rkisp1_device *rkisp1)
{
	int ret;

	ret = rkisp1_isp_register(rkisp1);
	if (ret)
		goto error;

	ret = rkisp1_resizer_devs_register(rkisp1);
	if (ret)
		goto error;

	ret = rkisp1_capture_devs_register(rkisp1);
	if (ret)
		goto error;

	ret = rkisp1_stats_register(rkisp1);
	if (ret)
		goto error;

	ret = rkisp1_params_register(rkisp1);
	if (ret)
		goto error;

	if (rkisp1_has_feature(rkisp1, MIPI_CSI2)) {
		ret = rkisp1_csi_register(rkisp1);
		if (ret)
			goto error;
	}

	ret = rkisp1_create_links(rkisp1);
	if (ret)
		goto error;

	return 0;

error:
	rkisp1_entities_unregister(rkisp1);
	return ret;
}

static irqreturn_t rkisp1_isr(int irq, void *ctx)
{
	irqreturn_t ret = IRQ_NONE;

	/*
	 * Call rkisp1_capture_isr() first to handle the frame that
	 * potentially completed using the current frame_sequence number before
	 * it is potentially incremented by rkisp1_isp_isr() in the vertical
	 * sync.
	 */

	if (rkisp1_capture_isr(irq, ctx) == IRQ_HANDLED)
		ret = IRQ_HANDLED;

	if (rkisp1_isp_isr(irq, ctx) == IRQ_HANDLED)
		ret = IRQ_HANDLED;

	if (rkisp1_csi_isr(irq, ctx) == IRQ_HANDLED)
		ret = IRQ_HANDLED;

	return ret;
}

static const char * const px30_isp_clks[] = {
	"isp",
	"aclk",
	"hclk",
	"pclk",
};

static const struct rkisp1_isr_data px30_isp_isrs[] = {
	{ "isp", rkisp1_isp_isr, BIT(RKISP1_IRQ_ISP) },
	{ "mi", rkisp1_capture_isr, BIT(RKISP1_IRQ_MI) },
	{ "mipi", rkisp1_csi_isr, BIT(RKISP1_IRQ_MIPI) },
};

static const struct rkisp1_info px30_isp_info = {
	.clks = px30_isp_clks,
	.clk_size = ARRAY_SIZE(px30_isp_clks),
	.isrs = px30_isp_isrs,
	.isr_size = ARRAY_SIZE(px30_isp_isrs),
	.isp_ver = RKISP1_V12,
	.features = RKISP1_FEATURE_MIPI_CSI2
		  | RKISP1_FEATURE_SELF_PATH
		  | RKISP1_FEATURE_DUAL_CROP,
};

static const char * const rk3399_isp_clks[] = {
	"isp",
	"aclk",
	"hclk",
};

static const struct rkisp1_isr_data rk3399_isp_isrs[] = {
	{ NULL, rkisp1_isr, BIT(RKISP1_IRQ_ISP) | BIT(RKISP1_IRQ_MI) | BIT(RKISP1_IRQ_MIPI) },
};

static const struct rkisp1_info rk3399_isp_info = {
	.clks = rk3399_isp_clks,
	.clk_size = ARRAY_SIZE(rk3399_isp_clks),
	.isrs = rk3399_isp_isrs,
	.isr_size = ARRAY_SIZE(rk3399_isp_isrs),
	.isp_ver = RKISP1_V10,
	.features = RKISP1_FEATURE_MIPI_CSI2
		  | RKISP1_FEATURE_SELF_PATH
		  | RKISP1_FEATURE_DUAL_CROP,
};

static const char * const imx8mp_isp_clks[] = {
	"isp",
	"hclk",
	"aclk",
};

static const struct rkisp1_isr_data imx8mp_isp_isrs[] = {
	{ NULL, rkisp1_isr, BIT(RKISP1_IRQ_ISP) | BIT(RKISP1_IRQ_MI) },
};

static const struct rkisp1_info imx8mp_isp_info = {
	.clks = imx8mp_isp_clks,
	.clk_size = ARRAY_SIZE(imx8mp_isp_clks),
	.isrs = imx8mp_isp_isrs,
	.isr_size = ARRAY_SIZE(imx8mp_isp_isrs),
	.isp_ver = RKISP1_V_IMX8MP,
	.features = RKISP1_FEATURE_MAIN_STRIDE
		  | RKISP1_FEATURE_DMA_34BIT,
};

static const struct of_device_id rkisp1_of_match[] = {
	{
		.compatible = "rockchip,px30-cif-isp",
		.data = &px30_isp_info,
	},
	{
		.compatible = "rockchip,rk3399-cif-isp",
		.data = &rk3399_isp_info,
	},
	{
		.compatible = "fsl,imx8mp-isp",
		.data = &imx8mp_isp_info,
	},
	{},
};
MODULE_DEVICE_TABLE(of, rkisp1_of_match);

static int rkisp1_probe(struct platform_device *pdev)
{
	const struct rkisp1_info *info;
	struct device *dev = &pdev->dev;
	struct rkisp1_device *rkisp1;
	struct v4l2_device *v4l2_dev;
	unsigned int i;
	u64 dma_mask;
	int ret, irq;
	u32 cif_id;

	rkisp1 = devm_kzalloc(dev, sizeof(*rkisp1), GFP_KERNEL);
	if (!rkisp1)
		return -ENOMEM;

	info = of_device_get_match_data(dev);
	rkisp1->info = info;

	dev_set_drvdata(dev, rkisp1);
	rkisp1->dev = dev;

	dma_mask = rkisp1_has_feature(rkisp1, DMA_34BIT) ? DMA_BIT_MASK(34) :
							   DMA_BIT_MASK(32);

	ret = dma_set_mask_and_coherent(dev, dma_mask);
	if (ret)
		return ret;

	mutex_init(&rkisp1->stream_lock);

	rkisp1->base_addr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(rkisp1->base_addr))
		return PTR_ERR(rkisp1->base_addr);

	for (unsigned int il = 0; il < ARRAY_SIZE(rkisp1->irqs); ++il)
		rkisp1->irqs[il] = -1;

	for (i = 0; i < info->isr_size; i++) {
		irq = info->isrs[i].name
		    ? platform_get_irq_byname(pdev, info->isrs[i].name)
		    : platform_get_irq(pdev, i);
		if (irq < 0)
			return irq;

		for (unsigned int il = 0; il < ARRAY_SIZE(rkisp1->irqs); ++il) {
			if (info->isrs[i].line_mask & BIT(il))
				rkisp1->irqs[il] = irq;
		}

		ret = devm_request_irq(dev, irq, info->isrs[i].isr, IRQF_SHARED,
				       dev_driver_string(dev), dev);
		if (ret) {
			dev_err(dev, "request irq failed: %d\n", ret);
			return ret;
		}
	}

	for (i = 0; i < info->clk_size; i++)
		rkisp1->clks[i].id = info->clks[i];
	ret = devm_clk_bulk_get(dev, info->clk_size, rkisp1->clks);
	if (ret)
		return ret;
	rkisp1->clk_size = info->clk_size;

	if (info->isp_ver == RKISP1_V_IMX8MP) {
		unsigned int id;

		rkisp1->gasket = syscon_regmap_lookup_by_phandle_args(dev->of_node,
								      "fsl,blk-ctrl",
								      1, &id);
		if (IS_ERR(rkisp1->gasket)) {
			ret = PTR_ERR(rkisp1->gasket);
			dev_err(dev, "failed to get gasket: %d\n", ret);
			return ret;
		}

		rkisp1->gasket_id = id;
	}

	pm_runtime_enable(&pdev->dev);

	ret = pm_runtime_resume_and_get(&pdev->dev);
	if (ret)
		goto err_pm_runtime_disable;

	cif_id = rkisp1_read(rkisp1, RKISP1_CIF_VI_ID);
	dev_dbg(rkisp1->dev, "CIF_ID 0x%08x\n", cif_id);

	pm_runtime_put(&pdev->dev);

	rkisp1->media_dev.hw_revision = info->isp_ver;
	strscpy(rkisp1->media_dev.model, RKISP1_DRIVER_NAME,
		sizeof(rkisp1->media_dev.model));
	rkisp1->media_dev.dev = &pdev->dev;
	strscpy(rkisp1->media_dev.bus_info, RKISP1_BUS_INFO,
		sizeof(rkisp1->media_dev.bus_info));
	media_device_init(&rkisp1->media_dev);

	v4l2_dev = &rkisp1->v4l2_dev;
	v4l2_dev->mdev = &rkisp1->media_dev;
	strscpy(v4l2_dev->name, RKISP1_DRIVER_NAME, sizeof(v4l2_dev->name));

	ret = v4l2_device_register(rkisp1->dev, &rkisp1->v4l2_dev);
	if (ret)
		goto err_media_dev_cleanup;

	ret = media_device_register(&rkisp1->media_dev);
	if (ret) {
		dev_err(dev, "Failed to register media device: %d\n", ret);
		goto err_unreg_v4l2_dev;
	}

	if (rkisp1->info->features & RKISP1_FEATURE_MIPI_CSI2) {
		ret = rkisp1_csi_init(rkisp1);
		if (ret)
			goto err_unreg_media_dev;
	}

	ret = rkisp1_entities_register(rkisp1);
	if (ret)
		goto err_cleanup_csi;

	ret = rkisp1_subdev_notifier_register(rkisp1);
	if (ret)
		goto err_unreg_entities;

	rkisp1_debug_init(rkisp1);

	return 0;

err_unreg_entities:
	rkisp1_entities_unregister(rkisp1);
err_cleanup_csi:
	if (rkisp1_has_feature(rkisp1, MIPI_CSI2))
		rkisp1_csi_cleanup(rkisp1);
err_unreg_media_dev:
	media_device_unregister(&rkisp1->media_dev);
err_unreg_v4l2_dev:
	v4l2_device_unregister(&rkisp1->v4l2_dev);
err_media_dev_cleanup:
	media_device_cleanup(&rkisp1->media_dev);
err_pm_runtime_disable:
	pm_runtime_disable(&pdev->dev);
	return ret;
}

static void rkisp1_remove(struct platform_device *pdev)
{
	struct rkisp1_device *rkisp1 = platform_get_drvdata(pdev);

	v4l2_async_nf_unregister(&rkisp1->notifier);
	v4l2_async_nf_cleanup(&rkisp1->notifier);

	rkisp1_entities_unregister(rkisp1);
	if (rkisp1_has_feature(rkisp1, MIPI_CSI2))
		rkisp1_csi_cleanup(rkisp1);
	rkisp1_debug_cleanup(rkisp1);

	media_device_unregister(&rkisp1->media_dev);
	v4l2_device_unregister(&rkisp1->v4l2_dev);

	media_device_cleanup(&rkisp1->media_dev);

	pm_runtime_disable(&pdev->dev);
}

static struct platform_driver rkisp1_drv = {
	.driver = {
		.name = RKISP1_DRIVER_NAME,
		.of_match_table = of_match_ptr(rkisp1_of_match),
		.pm = &rkisp1_pm_ops,
	},
	.probe = rkisp1_probe,
	.remove_new = rkisp1_remove,
};

module_platform_driver(rkisp1_drv);
MODULE_DESCRIPTION("Rockchip ISP1 platform driver");
MODULE_LICENSE("Dual MIT/GPL");
