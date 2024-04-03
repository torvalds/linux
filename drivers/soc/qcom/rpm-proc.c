// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2021-2023, Stephan Gerhold <stephan@gerhold.net> */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/rpmsg/qcom_smd.h>

static int rpm_proc_probe(struct platform_device *pdev)
{
	struct qcom_smd_edge *edge = NULL;
	struct device *dev = &pdev->dev;
	struct device_node *edge_node;
	int ret;

	edge_node = of_get_child_by_name(dev->of_node, "smd-edge");
	if (edge_node) {
		edge = qcom_smd_register_edge(dev, edge_node);
		of_node_put(edge_node);
		if (IS_ERR(edge))
			return dev_err_probe(dev, PTR_ERR(edge),
					     "Failed to register smd-edge\n");
	}

	ret = devm_of_platform_populate(dev);
	if (ret) {
		dev_err(dev, "Failed to populate child devices: %d\n", ret);
		goto err;
	}

	platform_set_drvdata(pdev, edge);
	return 0;
err:
	if (edge)
		qcom_smd_unregister_edge(edge);
	return ret;
}

static void rpm_proc_remove(struct platform_device *pdev)
{
	struct qcom_smd_edge *edge = platform_get_drvdata(pdev);

	if (edge)
		qcom_smd_unregister_edge(edge);
}

static const struct of_device_id rpm_proc_of_match[] = {
	{ .compatible = "qcom,rpm-proc", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rpm_proc_of_match);

static struct platform_driver rpm_proc_driver = {
	.probe = rpm_proc_probe,
	.remove_new = rpm_proc_remove,
	.driver = {
		.name = "qcom-rpm-proc",
		.of_match_table = rpm_proc_of_match,
	},
};

static int __init rpm_proc_init(void)
{
	return platform_driver_register(&rpm_proc_driver);
}
arch_initcall(rpm_proc_init);

static void __exit rpm_proc_exit(void)
{
	platform_driver_unregister(&rpm_proc_driver);
}
module_exit(rpm_proc_exit);

MODULE_DESCRIPTION("Qualcomm RPM processor/subsystem driver");
MODULE_AUTHOR("Stephan Gerhold <stephan@gerhold.net>");
MODULE_LICENSE("GPL");
