// SPDX-License-Identifier: GPL-2.0
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/module.h>
#include <linux/of_platform.h>

static int am335x_child_probe(struct platform_device *pdev)
{
	int ret;

	pm_runtime_enable(&pdev->dev);

	ret = of_platform_populate(pdev->dev.of_node, NULL, NULL, &pdev->dev);
	if (ret)
		goto err;

	return 0;
err:
	pm_runtime_disable(&pdev->dev);
	return ret;
}

static const struct of_device_id am335x_child_of_match[] = {
	{ .compatible = "ti,am33xx-usb" },
	{  },
};
MODULE_DEVICE_TABLE(of, am335x_child_of_match);

static struct platform_driver am335x_child_driver = {
	.probe		= am335x_child_probe,
	.driver         = {
		.name   = "am335x-usb-childs",
		.of_match_table	= am335x_child_of_match,
	},
};

static int __init am335x_child_init(void)
{
	return platform_driver_register(&am335x_child_driver);
}
module_init(am335x_child_init);

MODULE_DESCRIPTION("AM33xx child devices");
MODULE_LICENSE("GPL v2");
