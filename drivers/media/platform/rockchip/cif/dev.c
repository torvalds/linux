// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip CIF Driver
 *
 * Copyright (C) 2018 Rockchip Electronics Co., Ltd.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/reset.h>
#include <linux/pm_runtime.h>
#include <linux/pinctrl/consumer.h>
#include <linux/dma-iommu.h>
#include <media/v4l2-fwnode.h>

#include "dev.h"
#include "regs.h"

struct cif_match_data {
	const char * const *clks;
	int size;
};

int rkcif_debug;
module_param_named(debug, rkcif_debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

int using_pingpong;

/***************************** media controller *******************************/
static int rkcif_create_links(struct rkcif_device *dev)
{
	unsigned int s, pad;
	int ret;

	/* sensor links(or mipi-phy) */
	for (s = 0; s < dev->num_sensors; ++s) {
		struct rkcif_sensor_info *sensor = &dev->sensors[s];

		for (pad = 0; pad < sensor->sd->entity.num_pads; pad++)
			if (sensor->sd->entity.pads[pad].flags &
				MEDIA_PAD_FL_SOURCE)
				break;

		if (pad == sensor->sd->entity.num_pads) {
			dev_err(dev->dev, "failed to find src pad for %s\n",
				sensor->sd->name);
			return -ENXIO;
		}

		ret = media_entity_create_link(&sensor->sd->entity,
				pad, &dev->stream.vdev.entity, 0,
				s ? 0 : MEDIA_LNK_FL_ENABLED);
		if (ret) {
			dev_err(dev->dev, "failed to create link for %s\n",
				sensor->sd->name);
			return ret;
		}
	}

	return 0;
}

static int subdev_notifier_complete(struct v4l2_async_notifier *notifier)
{
	struct rkcif_device *dev;
	int ret;

	dev = container_of(notifier, struct rkcif_device, notifier);

	mutex_lock(&dev->media_dev.graph_mutex);

	ret = rkcif_create_links(dev);
	if (ret < 0)
		goto unlock;

	ret = v4l2_device_register_subdev_nodes(&dev->v4l2_dev);
	if (ret < 0)
		goto unlock;

	v4l2_info(&dev->v4l2_dev, "Async subdev notifier completed\n");

unlock:
	mutex_unlock(&dev->media_dev.graph_mutex);
	return ret;
}

struct rkcif_async_subdev {
	struct v4l2_async_subdev asd;
	struct v4l2_mbus_config mbus;
};

static int subdev_notifier_bound(struct v4l2_async_notifier *notifier,
				 struct v4l2_subdev *subdev,
				 struct v4l2_async_subdev *asd)
{
	struct rkcif_device *cif_dev = container_of(notifier,
					struct rkcif_device, notifier);
	struct rkcif_async_subdev *s_asd = container_of(asd,
					struct rkcif_async_subdev, asd);

	if (cif_dev->num_sensors == ARRAY_SIZE(cif_dev->sensors))
		return -EBUSY;

	cif_dev->sensors[cif_dev->num_sensors].mbus = s_asd->mbus;
	cif_dev->sensors[cif_dev->num_sensors].sd = subdev;
	++cif_dev->num_sensors;

	v4l2_dbg(1, rkcif_debug, subdev, "Async registered subdev\n");

	return 0;
}

static int rkcif_fwnode_parse(struct device *dev,
			       struct v4l2_fwnode_endpoint *vep,
			       struct v4l2_async_subdev *asd)
{
	struct rkcif_async_subdev *rk_asd =
			container_of(asd, struct rkcif_async_subdev, asd);
	struct v4l2_fwnode_bus_parallel *bus = &vep->bus.parallel;

	/*
	 * MIPI sensor is linked with a mipi dphy and its media bus config can
	 * not be get in here
	 */
	if (vep->bus_type != V4L2_MBUS_BT656 &&
		vep->bus_type != V4L2_MBUS_PARALLEL)
		return 0;

	rk_asd->mbus.flags = bus->flags;
	rk_asd->mbus.type = vep->bus_type;

	return 0;
}

static const struct v4l2_async_notifier_operations subdev_notifier_ops = {
	.bound = subdev_notifier_bound,
	.complete = subdev_notifier_complete,
};

static int cif_subdev_notifier(struct rkcif_device *cif_dev)
{
	struct v4l2_async_notifier *ntf = &cif_dev->notifier;
	struct device *dev = cif_dev->dev;
	int ret;

	ret = v4l2_async_notifier_parse_fwnode_endpoints(dev, ntf,
			sizeof(struct rkcif_async_subdev), rkcif_fwnode_parse);

	if (ret < 0)
		return ret;

	if (!ntf->num_subdevs)
		return -ENODEV;	/* no endpoint */

	ntf->ops = &subdev_notifier_ops;

	return v4l2_async_notifier_register(&cif_dev->v4l2_dev, ntf);
}

/***************************** platform deive *******************************/

static int rkcif_register_platform_subdevs(struct rkcif_device *cif_dev)
{
	int ret;

	ret = rkcif_register_stream_vdev(cif_dev);
	if (ret < 0)
		return ret;

	ret = cif_subdev_notifier(cif_dev);
	if (ret < 0) {
		v4l2_err(&cif_dev->v4l2_dev,
			 "Failed to register subdev notifier(%d)\n", ret);
		rkcif_unregister_stream_vdev(cif_dev);
	}

	return 0;
}

static const char * const rk3128_cif_clks[] = {
	"aclk_cif",
	"hclk_cif",
	"sclk_cif_out",
};

static const char * const rk3288_cif_clks[] = {
	"aclk_cif0",
	"hclk_cif0",
	"cif0_in",
	"cif0_out",
};

static const struct cif_match_data rk3128_cif_clk_data = {
	.clks = rk3128_cif_clks,
	.size = ARRAY_SIZE(rk3128_cif_clks),
};

static const struct cif_match_data rk3288_cif_clk_data = {
	.clks = rk3288_cif_clks,
	.size = ARRAY_SIZE(rk3288_cif_clks),
};

static const struct of_device_id rkcif_plat_of_match[] = {
	{
		.compatible = "rockchip,rk3128-cif",
		.data = &rk3128_cif_clk_data,
	},
	{
		.compatible = "rockchip,rk3288-cif",
		.data = &rk3288_cif_clk_data,
	},
	{},
};

static irqreturn_t rkcif_irq_handler(int irq, void *ctx)
{
	struct device *dev = ctx;
	struct rkcif_device *cif_dev = dev_get_drvdata(dev);

	if (using_pingpong)
		rkcif_irq_pingpong(cif_dev);
	else
		rkcif_irq_oneframe(cif_dev);

	return IRQ_HANDLED;
}

static void rkcif_disable_sys_clk(struct rkcif_device *cif_dev)
{
	int i;

	for (i = cif_dev->clk_size - 1; i >= 0; i--)
		clk_disable_unprepare(cif_dev->clks[i]);
}

static int rkcif_enable_sys_clk(struct rkcif_device *cif_dev)
{
	int i, ret = -EINVAL;

	for (i = 0; i < cif_dev->clk_size; i++) {
		ret = clk_prepare_enable(cif_dev->clks[i]);

		if (ret < 0)
			goto err;
	}

	return 0;

err:
	for (--i; i >= 0; --i)
		clk_disable_unprepare(cif_dev->clks[i]);

	return ret;
}

static int rkcif_iommu_init(struct rkcif_device *cif_dev)
{
	struct iommu_group *group;
	int ret;

	cif_dev->domain = iommu_domain_alloc(&platform_bus_type);
	if (!cif_dev->domain)
		return -ENOMEM;

	ret = iommu_get_dma_cookie(cif_dev->domain);
	if (ret)
		goto err_free_domain;

	group = iommu_group_get(cif_dev->dev);
	if (!group) {
		group = iommu_group_alloc();
		if (IS_ERR(group)) {
			ret = PTR_ERR(group);
			goto err_put_cookie;
		}
		ret = iommu_group_add_device(group, cif_dev->dev);
		iommu_group_put(group);
		if (ret)
			goto err_put_cookie;
	}
	iommu_group_put(group);

	ret = iommu_attach_device(cif_dev->domain, cif_dev->dev);
	if (ret)
		goto err_put_cookie;
	if (!common_iommu_setup_dma_ops(cif_dev->dev, 0x10000000, SZ_2G,
					cif_dev->domain->ops)) {
		ret = -ENODEV;
		goto err_detach;
	}

	return 0;

err_detach:
	iommu_detach_device(cif_dev->domain, cif_dev->dev);
err_put_cookie:
	iommu_put_dma_cookie(cif_dev->domain);
err_free_domain:
	iommu_domain_free(cif_dev->domain);

	dev_err(cif_dev->dev, "Failed to setup IOMMU, ret(%d)\n", ret);

	return ret;
}

static void rkcif_iommu_cleanup(struct rkcif_device *cif_dev)
{
	iommu_detach_device(cif_dev->domain, cif_dev->dev);
	iommu_put_dma_cookie(cif_dev->domain);
	iommu_domain_free(cif_dev->domain);
}

static inline bool is_iommu_enable(struct device *dev)
{
	struct device_node *iommu;

	iommu = of_parse_phandle(dev->of_node, "iommus", 0);
	if (!iommu) {
		dev_info(dev, "no iommu attached, using non-iommu buffers\n");
		return false;
	} else if (!of_device_is_available(iommu)) {
		dev_info(dev, "iommu is disabled, using non-iommu buffers\n");
		of_node_put(iommu);
		return false;
	}
	of_node_put(iommu);

	return true;
}

void rkcif_soft_reset(struct rkcif_device *cif_dev)
{
	if (cif_dev->iommu_en)
		rkcif_iommu_cleanup(cif_dev);

	reset_control_assert(cif_dev->cif_rst);
	udelay(5);
	reset_control_deassert(cif_dev->cif_rst);

	if (cif_dev->iommu_en)
		rkcif_iommu_init(cif_dev);
}

static int rkcif_plat_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct device_node *node = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct v4l2_device *v4l2_dev;
	struct rkcif_device *cif_dev;
	const struct cif_match_data *clk_data;
	struct resource *res;
	int i, ret, irq;

	match = of_match_node(rkcif_plat_of_match, node);
	cif_dev = devm_kzalloc(dev, sizeof(*cif_dev), GFP_KERNEL);
	if (!cif_dev)
		return -ENOMEM;

	dev_set_drvdata(dev, cif_dev);
	cif_dev->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	cif_dev->base_addr = devm_ioremap_resource(dev, res);
	if (IS_ERR(cif_dev->base_addr))
		return PTR_ERR(cif_dev->base_addr);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(dev, irq, rkcif_irq_handler, IRQF_SHARED,
			       dev_driver_string(dev), dev);
	if (ret < 0) {
		dev_err(dev, "request irq failed: %d\n", ret);
		return ret;
	}

	cif_dev->irq = irq;
	clk_data = match->data;
	for (i = 0; i < clk_data->size; i++) {
		struct clk *clk = devm_clk_get(dev, clk_data->clks[i]);

		if (IS_ERR(clk)) {
			dev_err(dev, "failed to get %s\n", clk_data->clks[i]);
			return PTR_ERR(clk);
		}
		cif_dev->clks[i] = clk;
	}
	cif_dev->clk_size = clk_data->size;

	cif_dev->cif_rst = devm_reset_control_get(dev, "rst_cif");
	if (IS_ERR(cif_dev->cif_rst)) {
		dev_err(dev, "failed to get core cif reset controller\n");
		return PTR_ERR(cif_dev->cif_rst);
	}

	rkcif_stream_init(cif_dev);

	strlcpy(cif_dev->media_dev.model, "rkcif",
		sizeof(cif_dev->media_dev.model));
	cif_dev->media_dev.dev = &pdev->dev;
	v4l2_dev = &cif_dev->v4l2_dev;
	v4l2_dev->mdev = &cif_dev->media_dev;
	strlcpy(v4l2_dev->name, "rkcif", sizeof(v4l2_dev->name));
	v4l2_ctrl_handler_init(&cif_dev->ctrl_handler, 8);
	v4l2_dev->ctrl_handler = &cif_dev->ctrl_handler;

	ret = v4l2_device_register(cif_dev->dev, &cif_dev->v4l2_dev);
	if (ret < 0)
		return ret;

	ret = media_device_register(&cif_dev->media_dev);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "Failed to register media device: %d\n",
			 ret);
		goto err_unreg_v4l2_dev;
	}

	/* create & register platefom subdev (from of_node) */
	ret = rkcif_register_platform_subdevs(cif_dev);
	if (ret < 0)
		goto err_unreg_media_dev;

	cif_dev->iommu_en = is_iommu_enable(dev);
	if (cif_dev->iommu_en)
		rkcif_iommu_init(cif_dev);
	pm_runtime_enable(&pdev->dev);

	return 0;

err_unreg_media_dev:
	media_device_unregister(&cif_dev->media_dev);
err_unreg_v4l2_dev:
	v4l2_device_unregister(&cif_dev->v4l2_dev);
	return ret;
}

static int rkcif_plat_remove(struct platform_device *pdev)
{
	struct rkcif_device *cif_dev = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);
	if (cif_dev->iommu_en)
		rkcif_iommu_cleanup(cif_dev);

	media_device_unregister(&cif_dev->media_dev);
	v4l2_device_unregister(&cif_dev->v4l2_dev);
	rkcif_unregister_stream_vdev(cif_dev);

	return 0;
}

static int __maybe_unused rkcif_runtime_suspend(struct device *dev)
{
	struct rkcif_device *cif_dev = dev_get_drvdata(dev);

	rkcif_disable_sys_clk(cif_dev);

	return pinctrl_pm_select_sleep_state(dev);
}

static int __maybe_unused rkcif_runtime_resume(struct device *dev)
{
	struct rkcif_device *cif_dev = dev_get_drvdata(dev);
	int ret;

	ret = pinctrl_pm_select_default_state(dev);
	if (ret < 0)
		return ret;
	rkcif_enable_sys_clk(cif_dev);

	return 0;
}

static const struct dev_pm_ops rkcif_plat_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(rkcif_runtime_suspend, rkcif_runtime_resume, NULL)
};

static struct platform_driver rkcif_plat_drv = {
	.driver = {
		   .name = CIF_DRIVER_NAME,
		   .of_match_table = of_match_ptr(rkcif_plat_of_match),
		   .pm = &rkcif_plat_pm_ops,
	},
	.probe = rkcif_plat_probe,
	.remove = rkcif_plat_remove,
};

module_platform_driver(rkcif_plat_drv);
MODULE_AUTHOR("Rockchip Camera/ISP team");
MODULE_DESCRIPTION("Rockchip CIF platform driver");
MODULE_LICENSE("GPL v2");
