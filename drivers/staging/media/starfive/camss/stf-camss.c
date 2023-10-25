// SPDX-License-Identifier: GPL-2.0
/*
 * stf_camss.c
 *
 * Starfive Camera Subsystem driver
 *
 * Copyright (C) 2021-2023 StarFive Technology Co., Ltd.
 *
 * Author: Jack Zhu <jack.zhu@starfivetech.com>
 * Author: Changhuang Liang <changhuang.liang@starfivetech.com>
 *
 */
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/videodev2.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mc.h>

#include "stf-camss.h"

static const char * const stfcamss_clocks[] = {
	"wrapper_clk_c",
	"ispcore_2x",
	"isp_axi",
};

static const char * const stfcamss_resets[] = {
	"wrapper_p",
	"wrapper_c",
	"axiwr",
	"isp_top_n",
	"isp_top_axi",
};

static const struct stf_isr_data stf_isrs[] = {
	{"wr_irq", stf_wr_irq_handler},
	{"isp_irq", stf_isp_irq_handler},
	{"line_irq", stf_line_irq_handler},
};

static int stfcamss_get_mem_res(struct stfcamss *stfcamss)
{
	struct platform_device *pdev = to_platform_device(stfcamss->dev);

	stfcamss->syscon_base =
		devm_platform_ioremap_resource_byname(pdev, "syscon");
	if (IS_ERR(stfcamss->syscon_base))
		return PTR_ERR(stfcamss->syscon_base);

	stfcamss->isp_base = devm_platform_ioremap_resource_byname(pdev, "isp");
	if (IS_ERR(stfcamss->isp_base))
		return PTR_ERR(stfcamss->isp_base);

	return 0;
}

/*
 * stfcamss_of_parse_endpoint_node - Parse port endpoint node
 * @dev: Device
 * @node: Device node to be parsed
 * @csd: Parsed data from port endpoint node
 *
 * Return 0 on success or a negative error code on failure
 */
static int stfcamss_of_parse_endpoint_node(struct stfcamss *stfcamss,
					   struct device_node *node,
					   struct stfcamss_async_subdev *csd)
{
	struct v4l2_fwnode_endpoint vep = { { 0 } };
	int ret;

	ret = v4l2_fwnode_endpoint_parse(of_fwnode_handle(node), &vep);
	if (ret) {
		dev_err(stfcamss->dev, "endpoint not defined at %pOF\n", node);
		return ret;
	}

	csd->port = vep.base.port;

	return 0;
}

/*
 * stfcamss_of_parse_ports - Parse ports node
 * @stfcamss: STFCAMSS device
 *
 * Return number of "port" nodes found in "ports" node
 */
static int stfcamss_of_parse_ports(struct stfcamss *stfcamss)
{
	struct device_node *node = NULL;
	int ret, num_subdevs = 0;

	for_each_endpoint_of_node(stfcamss->dev->of_node, node) {
		struct stfcamss_async_subdev *csd;

		if (!of_device_is_available(node))
			continue;

		csd = v4l2_async_nf_add_fwnode_remote(&stfcamss->notifier,
						      of_fwnode_handle(node),
						      struct stfcamss_async_subdev);
		if (IS_ERR(csd)) {
			ret = PTR_ERR(csd);
			dev_err(stfcamss->dev, "failed to add async notifier\n");
			goto err_cleanup;
		}

		ret = stfcamss_of_parse_endpoint_node(stfcamss, node, csd);
		if (ret)
			goto err_cleanup;

		num_subdevs++;
	}

	return num_subdevs;

err_cleanup:
	of_node_put(node);
	return ret;
}

static int stfcamss_subdev_notifier_complete(struct v4l2_async_notifier *ntf)
{
	struct stfcamss *stfcamss =
		container_of(ntf, struct stfcamss, notifier);

	return v4l2_device_register_subdev_nodes(&stfcamss->v4l2_dev);
}

static const struct v4l2_async_notifier_operations
stfcamss_subdev_notifier_ops = {
	.complete = stfcamss_subdev_notifier_complete,
};

static void stfcamss_mc_init(struct platform_device *pdev,
			     struct stfcamss *stfcamss)
{
	stfcamss->media_dev.dev = stfcamss->dev;
	strscpy(stfcamss->media_dev.model, "Starfive Camera Subsystem",
		sizeof(stfcamss->media_dev.model));
	media_device_init(&stfcamss->media_dev);

	stfcamss->v4l2_dev.mdev = &stfcamss->media_dev;
}

/*
 * stfcamss_probe - Probe STFCAMSS platform device
 * @pdev: Pointer to STFCAMSS platform device
 *
 * Return 0 on success or a negative error code on failure
 */
static int stfcamss_probe(struct platform_device *pdev)
{
	struct stfcamss *stfcamss;
	struct device *dev = &pdev->dev;
	int ret, num_subdevs;
	unsigned int i;

	stfcamss = devm_kzalloc(dev, sizeof(*stfcamss), GFP_KERNEL);
	if (!stfcamss)
		return -ENOMEM;

	stfcamss->dev = dev;

	for (i = 0; i < ARRAY_SIZE(stf_isrs); ++i) {
		int irq;

		irq = platform_get_irq(pdev, i);
		if (irq < 0)
			return irq;

		ret = devm_request_irq(stfcamss->dev, irq, stf_isrs[i].isr, 0,
				       stf_isrs[i].name, stfcamss);
		if (ret) {
			dev_err(dev, "request irq failed: %d\n", ret);
			return ret;
		}
	}

	stfcamss->nclks = ARRAY_SIZE(stfcamss->sys_clk);
	for (i = 0; i < stfcamss->nclks; ++i)
		stfcamss->sys_clk[i].id = stfcamss_clocks[i];
	ret = devm_clk_bulk_get(dev, stfcamss->nclks, stfcamss->sys_clk);
	if (ret) {
		dev_err(dev, "Failed to get clk controls\n");
		return ret;
	}

	stfcamss->nrsts = ARRAY_SIZE(stfcamss->sys_rst);
	for (i = 0; i < stfcamss->nrsts; ++i)
		stfcamss->sys_rst[i].id = stfcamss_resets[i];
	ret = devm_reset_control_bulk_get_shared(dev, stfcamss->nrsts,
						 stfcamss->sys_rst);
	if (ret) {
		dev_err(dev, "Failed to get reset controls\n");
		return ret;
	}

	ret = stfcamss_get_mem_res(stfcamss);
	if (ret) {
		dev_err(dev, "Could not map registers\n");
		return ret;
	}

	platform_set_drvdata(pdev, stfcamss);

	v4l2_async_nf_init(&stfcamss->notifier, &stfcamss->v4l2_dev);

	num_subdevs = stfcamss_of_parse_ports(stfcamss);
	if (num_subdevs < 0) {
		ret = -ENODEV;
		dev_err(dev, "Failed to get sub devices: %d\n", ret);
		goto err_cleanup_notifier;
	}

	stfcamss_mc_init(pdev, stfcamss);

	ret = v4l2_device_register(stfcamss->dev, &stfcamss->v4l2_dev);
	if (ret < 0) {
		dev_err(dev, "Failed to register V4L2 device: %d\n", ret);
		goto err_cleanup_media_device;
	}

	ret = media_device_register(&stfcamss->media_dev);
	if (ret) {
		dev_err(dev, "Failed to register media device: %d\n", ret);
		goto err_unregister_device;
	}

	pm_runtime_enable(dev);

	stfcamss->notifier.ops = &stfcamss_subdev_notifier_ops;
	ret = v4l2_async_nf_register(&stfcamss->notifier);
	if (ret) {
		dev_err(dev, "Failed to register async subdev nodes: %d\n",
			ret);
		pm_runtime_disable(dev);
		goto err_unregister_media_dev;
	}

	return 0;

err_unregister_media_dev:
	media_device_unregister(&stfcamss->media_dev);
err_unregister_device:
	v4l2_device_unregister(&stfcamss->v4l2_dev);
err_cleanup_media_device:
	media_device_cleanup(&stfcamss->media_dev);
err_cleanup_notifier:
	v4l2_async_nf_cleanup(&stfcamss->notifier);
	return ret;
}

/*
 * stfcamss_remove - Remove STFCAMSS platform device
 * @pdev: Pointer to STFCAMSS platform device
 *
 * Always returns 0.
 */
static int stfcamss_remove(struct platform_device *pdev)
{
	struct stfcamss *stfcamss = platform_get_drvdata(pdev);

	v4l2_device_unregister(&stfcamss->v4l2_dev);
	media_device_cleanup(&stfcamss->media_dev);
	v4l2_async_nf_cleanup(&stfcamss->notifier);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static const struct of_device_id stfcamss_of_match[] = {
	{ .compatible = "starfive,jh7110-camss" },
	{ /* sentinel */ },
};

MODULE_DEVICE_TABLE(of, stfcamss_of_match);

static int __maybe_unused stfcamss_runtime_suspend(struct device *dev)
{
	struct stfcamss *stfcamss = dev_get_drvdata(dev);
	int ret;

	ret = reset_control_bulk_assert(stfcamss->nrsts, stfcamss->sys_rst);
	if (ret) {
		dev_err(dev, "reset assert failed\n");
		return ret;
	}

	clk_bulk_disable_unprepare(stfcamss->nclks, stfcamss->sys_clk);

	return 0;
}

static int __maybe_unused stfcamss_runtime_resume(struct device *dev)
{
	struct stfcamss *stfcamss = dev_get_drvdata(dev);
	int ret;

	ret = clk_bulk_prepare_enable(stfcamss->nclks, stfcamss->sys_clk);
	if (ret) {
		dev_err(dev, "clock prepare enable failed\n");
		return ret;
	}

	ret = reset_control_bulk_deassert(stfcamss->nrsts, stfcamss->sys_rst);
	if (ret < 0) {
		dev_err(dev, "cannot deassert resets\n");
		clk_bulk_disable_unprepare(stfcamss->nclks, stfcamss->sys_clk);
		return ret;
	}

	return 0;
}

static const struct dev_pm_ops stfcamss_pm_ops = {
	SET_RUNTIME_PM_OPS(stfcamss_runtime_suspend,
			   stfcamss_runtime_resume,
			   NULL)
};

static struct platform_driver stfcamss_driver = {
	.probe = stfcamss_probe,
	.remove = stfcamss_remove,
	.driver = {
		.name = "starfive-camss",
		.pm = &stfcamss_pm_ops,
		.of_match_table = stfcamss_of_match,
	},
};

module_platform_driver(stfcamss_driver);

MODULE_AUTHOR("Jack Zhu <jack.zhu@starfivetech.com>");
MODULE_AUTHOR("Changhuang Liang <changhuang.liang@starfivetech.com>");
MODULE_DESCRIPTION("StarFive Camera Subsystem driver");
MODULE_LICENSE("GPL");
