#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/usb/otg.h>
#include "phy-am335x-control.h"

struct am335x_control_usb {
	struct device *dev;
	void __iomem *phy_reg;
	void __iomem *wkup;
	spinlock_t lock;
	struct phy_control phy_ctrl;
};

#define AM335X_USB0_CTRL		0x0
#define AM335X_USB1_CTRL		0x8
#define AM335x_USB_WKUP			0x0

#define USBPHY_CM_PWRDN		(1 << 0)
#define USBPHY_OTG_PWRDN	(1 << 1)
#define USBPHY_OTGVDET_EN	(1 << 19)
#define USBPHY_OTGSESSEND_EN	(1 << 20)

#define AM335X_PHY0_WK_EN	(1 << 0)
#define AM335X_PHY1_WK_EN	(1 << 8)

static void am335x_phy_wkup(struct  phy_control *phy_ctrl, u32 id, bool on)
{
	struct am335x_control_usb *usb_ctrl;
	u32 val;
	u32 reg;

	usb_ctrl = container_of(phy_ctrl, struct am335x_control_usb, phy_ctrl);

	switch (id) {
	case 0:
		reg = AM335X_PHY0_WK_EN;
		break;
	case 1:
		reg = AM335X_PHY1_WK_EN;
		break;
	default:
		WARN_ON(1);
		return;
	}

	spin_lock(&usb_ctrl->lock);
	val = readl(usb_ctrl->wkup);

	if (on)
		val |= reg;
	else
		val &= ~reg;

	writel(val, usb_ctrl->wkup);
	spin_unlock(&usb_ctrl->lock);
}

static void am335x_phy_power(struct phy_control *phy_ctrl, u32 id,
				enum usb_dr_mode dr_mode, bool on)
{
	struct am335x_control_usb *usb_ctrl;
	u32 val;
	u32 reg;

	usb_ctrl = container_of(phy_ctrl, struct am335x_control_usb, phy_ctrl);

	switch (id) {
	case 0:
		reg = AM335X_USB0_CTRL;
		break;
	case 1:
		reg = AM335X_USB1_CTRL;
		break;
	default:
		WARN_ON(1);
		return;
	}

	val = readl(usb_ctrl->phy_reg + reg);
	if (on) {
		if (dr_mode == USB_DR_MODE_HOST) {
			val &= ~(USBPHY_CM_PWRDN | USBPHY_OTG_PWRDN |
					USBPHY_OTGVDET_EN);
			val |= USBPHY_OTGSESSEND_EN;
		} else {
			val &= ~(USBPHY_CM_PWRDN | USBPHY_OTG_PWRDN);
			val |= USBPHY_OTGVDET_EN | USBPHY_OTGSESSEND_EN;
		}
	} else {
		val |= USBPHY_CM_PWRDN | USBPHY_OTG_PWRDN;
	}

	writel(val, usb_ctrl->phy_reg + reg);

	/*
	 * Give the PHY ~1ms to complete the power up operation.
	 * Tests have shown unstable behaviour if other USB PHY related
	 * registers are written too shortly after such a transition.
	 */
	if (on)
		mdelay(1);
}

static const struct phy_control ctrl_am335x = {
	.phy_power = am335x_phy_power,
	.phy_wkup = am335x_phy_wkup,
};

static const struct of_device_id omap_control_usb_id_table[] = {
	{ .compatible = "ti,am335x-usb-ctrl-module", .data = &ctrl_am335x },
	{}
};
MODULE_DEVICE_TABLE(of, omap_control_usb_id_table);

static struct platform_driver am335x_control_driver;
static int match(struct device *dev, void *data)
{
	struct device_node *node = (struct device_node *)data;
	return dev->of_node == node &&
		dev->driver == &am335x_control_driver.driver;
}

struct phy_control *am335x_get_phy_control(struct device *dev)
{
	struct device_node *node;
	struct am335x_control_usb *ctrl_usb;

	node = of_parse_phandle(dev->of_node, "ti,ctrl_mod", 0);
	if (!node)
		return NULL;

	dev = bus_find_device(&platform_bus_type, NULL, node, match);
	if (!dev)
		return NULL;

	ctrl_usb = dev_get_drvdata(dev);
	if (!ctrl_usb)
		return NULL;
	return &ctrl_usb->phy_ctrl;
}
EXPORT_SYMBOL_GPL(am335x_get_phy_control);

static int am335x_control_usb_probe(struct platform_device *pdev)
{
	struct resource	*res;
	struct am335x_control_usb *ctrl_usb;
	const struct of_device_id *of_id;
	const struct phy_control *phy_ctrl;

	of_id = of_match_node(omap_control_usb_id_table, pdev->dev.of_node);
	if (!of_id)
		return -EINVAL;

	phy_ctrl = of_id->data;

	ctrl_usb = devm_kzalloc(&pdev->dev, sizeof(*ctrl_usb), GFP_KERNEL);
	if (!ctrl_usb)
		return -ENOMEM;

	ctrl_usb->dev = &pdev->dev;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "phy_ctrl");
	ctrl_usb->phy_reg = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(ctrl_usb->phy_reg))
		return PTR_ERR(ctrl_usb->phy_reg);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "wakeup");
	ctrl_usb->wkup = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(ctrl_usb->wkup))
		return PTR_ERR(ctrl_usb->wkup);

	spin_lock_init(&ctrl_usb->lock);
	ctrl_usb->phy_ctrl = *phy_ctrl;

	dev_set_drvdata(ctrl_usb->dev, ctrl_usb);
	return 0;
}

static struct platform_driver am335x_control_driver = {
	.probe		= am335x_control_usb_probe,
	.driver		= {
		.name	= "am335x-control-usb",
		.of_match_table = omap_control_usb_id_table,
	},
};

module_platform_driver(am335x_control_driver);
MODULE_LICENSE("GPL v2");
