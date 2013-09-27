#include <linux/init.h>
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

static int of_remove_populated_child(struct device *dev, void *d)
{
	struct platform_device *pdev = to_platform_device(dev);

	of_device_unregister(pdev);
	return 0;
}

static int am335x_child_remove(struct platform_device *pdev)
{
	device_for_each_child(&pdev->dev, NULL, of_remove_populated_child);
	pm_runtime_disable(&pdev->dev);
	return 0;
}

static const struct of_device_id am335x_child_of_match[] = {
	{ .compatible = "ti,am33xx-usb" },
	{  },
};
MODULE_DEVICE_TABLE(of, am335x_child_of_match);

static struct platform_driver am335x_child_driver = {
	.probe		= am335x_child_probe,
	.remove         = am335x_child_remove,
	.driver         = {
		.name   = "am335x-usb-childs",
		.of_match_table	= of_match_ptr(am335x_child_of_match),
	},
};

module_platform_driver(am335x_child_driver);
MODULE_DESCRIPTION("AM33xx child devices");
MODULE_LICENSE("GPL v2");
