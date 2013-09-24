#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/usb/otg.h>
#include <linux/usb/usb_phy_gen_xceiv.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include "am35x-phy-control.h"
#include "phy-generic.h"

struct am335x_phy {
	struct usb_phy_gen_xceiv usb_phy_gen;
	struct phy_control *phy_ctrl;
	int id;
};

static int am335x_init(struct usb_phy *phy)
{
	struct am335x_phy *am_phy = dev_get_drvdata(phy->dev);

	phy_ctrl_power(am_phy->phy_ctrl, am_phy->id, true);
	return 0;
}

static void am335x_shutdown(struct usb_phy *phy)
{
	struct am335x_phy *am_phy = dev_get_drvdata(phy->dev);

	phy_ctrl_power(am_phy->phy_ctrl, am_phy->id, false);
}

static int am335x_phy_probe(struct platform_device *pdev)
{
	struct am335x_phy *am_phy;
	struct device *dev = &pdev->dev;
	int ret;

	am_phy = devm_kzalloc(dev, sizeof(*am_phy), GFP_KERNEL);
	if (!am_phy)
		return -ENOMEM;

	am_phy->phy_ctrl = am335x_get_phy_control(dev);
	if (!am_phy->phy_ctrl)
		return -EPROBE_DEFER;
	am_phy->id = of_alias_get_id(pdev->dev.of_node, "phy");
	if (am_phy->id < 0) {
		dev_err(&pdev->dev, "Missing PHY id: %d\n", am_phy->id);
		return am_phy->id;
	}

	ret = usb_phy_gen_create_phy(dev, &am_phy->usb_phy_gen,
			USB_PHY_TYPE_USB2, 0, false);
	if (ret)
		return ret;

	ret = usb_add_phy_dev(&am_phy->usb_phy_gen.phy);
	if (ret)
		return ret;
	am_phy->usb_phy_gen.phy.init = am335x_init;
	am_phy->usb_phy_gen.phy.shutdown = am335x_shutdown;

	platform_set_drvdata(pdev, am_phy);

	return 0;

	return ret;
}

static int am335x_phy_remove(struct platform_device *pdev)
{
	struct am335x_phy *am_phy = platform_get_drvdata(pdev);

	usb_remove_phy(&am_phy->usb_phy_gen.phy);
	return 0;
}

#ifdef CONFIG_PM_RUNTIME

static int am335x_phy_runtime_suspend(struct device *dev)
{
	struct platform_device	*pdev = to_platform_device(dev);
	struct am335x_phy *am_phy = platform_get_drvdata(pdev);

	if (device_may_wakeup(dev))
		phy_ctrl_wkup(am_phy->phy_ctrl, am_phy->id, true);
	phy_ctrl_power(am_phy->phy_ctrl, am_phy->id, false);
	return 0;
}

static int am335x_phy_runtime_resume(struct device *dev)
{
	struct platform_device	*pdev = to_platform_device(dev);
	struct am335x_phy	*am_phy = platform_get_drvdata(pdev);

	phy_ctrl_power(am_phy->phy_ctrl, am_phy->id, true);
	if (device_may_wakeup(dev))
		phy_ctrl_wkup(am_phy->phy_ctrl, am_phy->id, false);
	return 0;
}

static const struct dev_pm_ops am335x_pm_ops = {
	SET_RUNTIME_PM_OPS(am335x_phy_runtime_suspend,
			am335x_phy_runtime_resume, NULL)
};

#define DEV_PM_OPS	(&am335x_pm_ops)
#else
#define DEV_PM_OPS	NULL
#endif

static const struct of_device_id am335x_phy_ids[] = {
	{ .compatible = "ti,am335x-usb-phy" },
	{ }
};
MODULE_DEVICE_TABLE(of, am335x_phy_ids);

static struct platform_driver am335x_phy_driver = {
	.probe          = am335x_phy_probe,
	.remove         = am335x_phy_remove,
	.driver         = {
		.name   = "am335x-phy-driver",
		.owner  = THIS_MODULE,
		.pm = DEV_PM_OPS,
		.of_match_table = am335x_phy_ids,
	},
};

module_platform_driver(am335x_phy_driver);
MODULE_LICENSE("GPL v2");
