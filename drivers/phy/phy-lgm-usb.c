// SPDX-License-Identifier: GPL-2.0
/*
 * Intel LGM USB PHY driver
 *
 * Copyright (C) 2020 Intel Corporation.
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/usb/phy.h>
#include <linux/workqueue.h>

#define CTRL1_OFFSET		0x14
#define SRAM_EXT_LD_DONE	BIT(25)
#define SRAM_INIT_DONE		BIT(26)

#define TCPC_OFFSET		0x1014
#define TCPC_MUX_CTL		GENMASK(1, 0)
#define MUX_NC			0
#define MUX_USB			1
#define MUX_DP			2
#define MUX_USBDP		3
#define TCPC_FLIPPED		BIT(2)
#define TCPC_LOW_POWER_EN	BIT(3)
#define TCPC_VALID		BIT(4)
#define TCPC_CONN		\
	(TCPC_VALID | FIELD_PREP(TCPC_MUX_CTL, MUX_USB))
#define TCPC_DISCONN		\
	(TCPC_VALID | FIELD_PREP(TCPC_MUX_CTL, MUX_NC) | TCPC_LOW_POWER_EN)

static const char *const PHY_RESETS[] = { "phy31", "phy", };
static const char *const CTL_RESETS[] = { "apb", "ctrl", };

struct tca_apb {
	struct reset_control *resets[ARRAY_SIZE(PHY_RESETS)];
	struct regulator *vbus;
	struct work_struct wk;
	struct usb_phy phy;

	bool regulator_enabled;
	bool phy_initialized;
	bool connected;
};

static int get_flipped(struct tca_apb *ta, bool *flipped)
{
	union extcon_property_value property;
	int ret;

	ret = extcon_get_property(ta->phy.edev, EXTCON_USB_HOST,
				  EXTCON_PROP_USB_TYPEC_POLARITY, &property);
	if (ret) {
		dev_err(ta->phy.dev, "no polarity property from extcon\n");
		return ret;
	}

	*flipped = property.intval;

	return 0;
}

static int phy_init(struct usb_phy *phy)
{
	struct tca_apb *ta = container_of(phy, struct tca_apb, phy);
	void __iomem *ctrl1 = phy->io_priv + CTRL1_OFFSET;
	int val, ret, i;

	if (ta->phy_initialized)
		return 0;

	for (i = 0; i < ARRAY_SIZE(PHY_RESETS); i++)
		reset_control_deassert(ta->resets[i]);

	ret = readl_poll_timeout(ctrl1, val, val & SRAM_INIT_DONE, 10, 10 * 1000);
	if (ret) {
		dev_err(ta->phy.dev, "SRAM init failed, 0x%x\n", val);
		return ret;
	}

	writel(readl(ctrl1) | SRAM_EXT_LD_DONE, ctrl1);

	ta->phy_initialized = true;
	if (!ta->phy.edev) {
		writel(TCPC_CONN, ta->phy.io_priv + TCPC_OFFSET);
		return phy->set_vbus(phy, true);
	}

	schedule_work(&ta->wk);

	return ret;
}

static void phy_shutdown(struct usb_phy *phy)
{
	struct tca_apb *ta = container_of(phy, struct tca_apb, phy);
	int i;

	if (!ta->phy_initialized)
		return;

	ta->phy_initialized = false;
	flush_work(&ta->wk);
	ta->phy.set_vbus(&ta->phy, false);

	ta->connected = false;
	writel(TCPC_DISCONN, ta->phy.io_priv + TCPC_OFFSET);

	for (i = 0; i < ARRAY_SIZE(PHY_RESETS); i++)
		reset_control_assert(ta->resets[i]);
}

static int phy_set_vbus(struct usb_phy *phy, int on)
{
	struct tca_apb *ta = container_of(phy, struct tca_apb, phy);
	int ret;

	if (!!on == ta->regulator_enabled)
		return 0;

	if (on)
		ret = regulator_enable(ta->vbus);
	else
		ret = regulator_disable(ta->vbus);

	if (!ret)
		ta->regulator_enabled = on;

	dev_dbg(ta->phy.dev, "set vbus: %d\n", on);
	return ret;
}

static void tca_work(struct work_struct *work)
{
	struct tca_apb *ta = container_of(work, struct tca_apb, wk);
	bool connected;
	bool flipped = false;
	u32 val;
	int ret;

	ret = get_flipped(ta, &flipped);
	if (ret)
		return;

	connected = extcon_get_state(ta->phy.edev, EXTCON_USB_HOST);
	if (connected == ta->connected)
		return;

	ta->connected = connected;
	if (connected) {
		val = TCPC_CONN;
		if (flipped)
			val |= TCPC_FLIPPED;
		dev_dbg(ta->phy.dev, "connected%s\n", flipped ? " flipped" : "");
	} else {
		val = TCPC_DISCONN;
		dev_dbg(ta->phy.dev, "disconnected\n");
	}

	writel(val, ta->phy.io_priv + TCPC_OFFSET);

	ret = ta->phy.set_vbus(&ta->phy, connected);
	if (ret)
		dev_err(ta->phy.dev, "failed to set VBUS\n");
}

static int id_notifier(struct notifier_block *nb, unsigned long event, void *ptr)
{
	struct tca_apb *ta = container_of(nb, struct tca_apb, phy.id_nb);

	if (ta->phy_initialized)
		schedule_work(&ta->wk);

	return NOTIFY_DONE;
}

static int vbus_notifier(struct notifier_block *nb, unsigned long evnt, void *ptr)
{
	return NOTIFY_DONE;
}

static int phy_probe(struct platform_device *pdev)
{
	struct reset_control *resets[ARRAY_SIZE(CTL_RESETS)];
	struct device *dev = &pdev->dev;
	struct usb_phy *phy;
	struct tca_apb *ta;
	int i;

	ta = devm_kzalloc(dev, sizeof(*ta), GFP_KERNEL);
	if (!ta)
		return -ENOMEM;

	platform_set_drvdata(pdev, ta);
	INIT_WORK(&ta->wk, tca_work);

	phy = &ta->phy;
	phy->dev = dev;
	phy->label = dev_name(dev);
	phy->type = USB_PHY_TYPE_USB3;
	phy->init = phy_init;
	phy->shutdown = phy_shutdown;
	phy->set_vbus = phy_set_vbus;
	phy->id_nb.notifier_call = id_notifier;
	phy->vbus_nb.notifier_call = vbus_notifier;

	phy->io_priv = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(phy->io_priv))
		return PTR_ERR(phy->io_priv);

	ta->vbus = devm_regulator_get(dev, "vbus");
	if (IS_ERR(ta->vbus))
		return PTR_ERR(ta->vbus);

	for (i = 0; i < ARRAY_SIZE(CTL_RESETS); i++) {
		resets[i] = devm_reset_control_get_exclusive(dev, CTL_RESETS[i]);
		if (IS_ERR(resets[i])) {
			dev_err(dev, "%s reset not found\n", CTL_RESETS[i]);
			return PTR_ERR(resets[i]);
		}
	}

	for (i = 0; i < ARRAY_SIZE(PHY_RESETS); i++) {
		ta->resets[i] = devm_reset_control_get_exclusive(dev, PHY_RESETS[i]);
		if (IS_ERR(ta->resets[i])) {
			dev_err(dev, "%s reset not found\n", PHY_RESETS[i]);
			return PTR_ERR(ta->resets[i]);
		}
	}

	for (i = 0; i < ARRAY_SIZE(CTL_RESETS); i++)
		reset_control_assert(resets[i]);

	for (i = 0; i < ARRAY_SIZE(PHY_RESETS); i++)
		reset_control_assert(ta->resets[i]);
	/*
	 * Out-of-band reset of the controller after PHY reset will cause
	 * controller malfunctioning, so we should use in-band controller
	 * reset only and leave the controller de-asserted here.
	 */
	for (i = 0; i < ARRAY_SIZE(CTL_RESETS); i++)
		reset_control_deassert(resets[i]);

	/* Need to wait at least 20us after de-assert the controller */
	usleep_range(20, 100);

	return usb_add_phy_dev(phy);
}

static void phy_remove(struct platform_device *pdev)
{
	struct tca_apb *ta = platform_get_drvdata(pdev);

	usb_remove_phy(&ta->phy);
}

static const struct of_device_id intel_usb_phy_dt_ids[] = {
	{ .compatible = "intel,lgm-usb-phy" },
	{ }
};
MODULE_DEVICE_TABLE(of, intel_usb_phy_dt_ids);

static struct platform_driver lgm_phy_driver = {
	.driver = {
		.name = "lgm-usb-phy",
		.of_match_table = intel_usb_phy_dt_ids,
	},
	.probe = phy_probe,
	.remove_new = phy_remove,
};

module_platform_driver(lgm_phy_driver);

MODULE_DESCRIPTION("Intel LGM USB PHY driver");
MODULE_AUTHOR("Li Yin <yin1.li@intel.com>");
MODULE_AUTHOR("Vadivel Murugan R <vadivel.muruganx.ramuthevar@linux.intel.com>");
MODULE_LICENSE("GPL v2");
