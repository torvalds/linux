// SPDX-License-Identifier: GPL-2.0
/*
 * Extcon charger detection driver for Intel Cherrytrail Whiskey Cove PMIC
 * Copyright (C) 2017 Hans de Goede <hdegoede@redhat.com>
 *
 * Based on various non upstream patches to support the CHT Whiskey Cove PMIC:
 * Copyright (C) 2013-2015 Intel Corporation. All rights reserved.
 */

#include <linux/extcon-provider.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/intel_soc_pmic.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/usb/role.h>

#include "extcon-intel.h"

#define CHT_WC_PHYCTRL			0x5e07

#define CHT_WC_CHGRCTRL0		0x5e16
#define CHT_WC_CHGRCTRL0_CHGRRESET	BIT(0)
#define CHT_WC_CHGRCTRL0_EMRGCHREN	BIT(1)
#define CHT_WC_CHGRCTRL0_EXTCHRDIS	BIT(2)
#define CHT_WC_CHGRCTRL0_SWCONTROL	BIT(3)
#define CHT_WC_CHGRCTRL0_TTLCK		BIT(4)
#define CHT_WC_CHGRCTRL0_CCSM_OFF	BIT(5)
#define CHT_WC_CHGRCTRL0_DBPOFF		BIT(6)
#define CHT_WC_CHGRCTRL0_CHR_WDT_NOKICK	BIT(7)

#define CHT_WC_CHGRCTRL1			0x5e17
#define CHT_WC_CHGRCTRL1_FUSB_INLMT_100		BIT(0)
#define CHT_WC_CHGRCTRL1_FUSB_INLMT_150		BIT(1)
#define CHT_WC_CHGRCTRL1_FUSB_INLMT_500		BIT(2)
#define CHT_WC_CHGRCTRL1_FUSB_INLMT_900		BIT(3)
#define CHT_WC_CHGRCTRL1_FUSB_INLMT_1500	BIT(4)
#define CHT_WC_CHGRCTRL1_FTEMP_EVENT		BIT(5)
#define CHT_WC_CHGRCTRL1_OTGMODE		BIT(6)
#define CHT_WC_CHGRCTRL1_DBPEN			BIT(7)

#define CHT_WC_USBSRC			0x5e29
#define CHT_WC_USBSRC_STS_MASK		GENMASK(1, 0)
#define CHT_WC_USBSRC_STS_SUCCESS	2
#define CHT_WC_USBSRC_STS_FAIL		3
#define CHT_WC_USBSRC_TYPE_SHIFT	2
#define CHT_WC_USBSRC_TYPE_MASK		GENMASK(5, 2)
#define CHT_WC_USBSRC_TYPE_NONE		0
#define CHT_WC_USBSRC_TYPE_SDP		1
#define CHT_WC_USBSRC_TYPE_DCP		2
#define CHT_WC_USBSRC_TYPE_CDP		3
#define CHT_WC_USBSRC_TYPE_ACA		4
#define CHT_WC_USBSRC_TYPE_SE1		5
#define CHT_WC_USBSRC_TYPE_MHL		6
#define CHT_WC_USBSRC_TYPE_FLOATING	7
#define CHT_WC_USBSRC_TYPE_OTHER	8
#define CHT_WC_USBSRC_TYPE_DCP_EXTPHY	9

#define CHT_WC_CHGDISCTRL		0x5e2f
#define CHT_WC_CHGDISCTRL_OUT		BIT(0)
/* 0 - open drain, 1 - regular push-pull output */
#define CHT_WC_CHGDISCTRL_DRV		BIT(4)
/* 0 - pin is controlled by SW, 1 - by HW */
#define CHT_WC_CHGDISCTRL_FN		BIT(6)

#define CHT_WC_PWRSRC_IRQ		0x6e03
#define CHT_WC_PWRSRC_IRQ_MASK		0x6e0f
#define CHT_WC_PWRSRC_STS		0x6e1e
#define CHT_WC_PWRSRC_VBUS		BIT(0)
#define CHT_WC_PWRSRC_DC		BIT(1)
#define CHT_WC_PWRSRC_BATT		BIT(2)
#define CHT_WC_PWRSRC_USBID_MASK	GENMASK(4, 3)
#define CHT_WC_PWRSRC_USBID_SHIFT	3
#define CHT_WC_PWRSRC_RID_ACA		0
#define CHT_WC_PWRSRC_RID_GND		1
#define CHT_WC_PWRSRC_RID_FLOAT		2

#define CHT_WC_VBUS_GPIO_CTLO		0x6e2d
#define CHT_WC_VBUS_GPIO_CTLO_OUTPUT	BIT(0)
#define CHT_WC_VBUS_GPIO_CTLO_DRV_OD	BIT(4)
#define CHT_WC_VBUS_GPIO_CTLO_DIR_OUT	BIT(5)

enum cht_wc_mux_select {
	MUX_SEL_PMIC = 0,
	MUX_SEL_SOC,
};

static const unsigned int cht_wc_extcon_cables[] = {
	EXTCON_USB,
	EXTCON_USB_HOST,
	EXTCON_CHG_USB_SDP,
	EXTCON_CHG_USB_CDP,
	EXTCON_CHG_USB_DCP,
	EXTCON_CHG_USB_ACA,
	EXTCON_NONE,
};

struct cht_wc_extcon_data {
	struct device *dev;
	struct regmap *regmap;
	struct extcon_dev *edev;
	struct usb_role_switch *role_sw;
	struct regulator *vbus_boost;
	unsigned int previous_cable;
	bool usb_host;
	bool vbus_boost_enabled;
};

static int cht_wc_extcon_get_id(struct cht_wc_extcon_data *ext, int pwrsrc_sts)
{
	switch ((pwrsrc_sts & CHT_WC_PWRSRC_USBID_MASK) >> CHT_WC_PWRSRC_USBID_SHIFT) {
	case CHT_WC_PWRSRC_RID_GND:
		return INTEL_USB_ID_GND;
	case CHT_WC_PWRSRC_RID_FLOAT:
		return INTEL_USB_ID_FLOAT;
	case CHT_WC_PWRSRC_RID_ACA:
	default:
		/*
		 * Once we have IIO support for the GPADC we should read
		 * the USBID GPADC channel here and determine ACA role
		 * based on that.
		 */
		return INTEL_USB_ID_FLOAT;
	}
}

static int cht_wc_extcon_get_charger(struct cht_wc_extcon_data *ext,
				     bool ignore_errors)
{
	int ret, usbsrc, status;
	unsigned long timeout;

	/* Charger detection can take upto 600ms, wait 800ms max. */
	timeout = jiffies + msecs_to_jiffies(800);
	do {
		ret = regmap_read(ext->regmap, CHT_WC_USBSRC, &usbsrc);
		if (ret) {
			dev_err(ext->dev, "Error reading usbsrc: %d\n", ret);
			return ret;
		}

		status = usbsrc & CHT_WC_USBSRC_STS_MASK;
		if (status == CHT_WC_USBSRC_STS_SUCCESS ||
		    status == CHT_WC_USBSRC_STS_FAIL)
			break;

		msleep(50); /* Wait a bit before retrying */
	} while (time_before(jiffies, timeout));

	if (status != CHT_WC_USBSRC_STS_SUCCESS) {
		if (!ignore_errors) {
			if (status == CHT_WC_USBSRC_STS_FAIL)
				dev_warn(ext->dev, "Could not detect charger type\n");
			else
				dev_warn(ext->dev, "Timeout detecting charger type\n");
		}

		/* Safe fallback */
		usbsrc = CHT_WC_USBSRC_TYPE_SDP << CHT_WC_USBSRC_TYPE_SHIFT;
	}

	usbsrc = (usbsrc & CHT_WC_USBSRC_TYPE_MASK) >> CHT_WC_USBSRC_TYPE_SHIFT;
	switch (usbsrc) {
	default:
		dev_warn(ext->dev,
			"Unhandled charger type %d, defaulting to SDP\n",
			 ret);
		return EXTCON_CHG_USB_SDP;
	case CHT_WC_USBSRC_TYPE_SDP:
	case CHT_WC_USBSRC_TYPE_FLOATING:
	case CHT_WC_USBSRC_TYPE_OTHER:
		return EXTCON_CHG_USB_SDP;
	case CHT_WC_USBSRC_TYPE_CDP:
		return EXTCON_CHG_USB_CDP;
	case CHT_WC_USBSRC_TYPE_DCP:
	case CHT_WC_USBSRC_TYPE_DCP_EXTPHY:
	case CHT_WC_USBSRC_TYPE_MHL: /* MHL2+ delivers upto 2A, treat as DCP */
		return EXTCON_CHG_USB_DCP;
	case CHT_WC_USBSRC_TYPE_ACA:
		return EXTCON_CHG_USB_ACA;
	}
}

static void cht_wc_extcon_set_phymux(struct cht_wc_extcon_data *ext, u8 state)
{
	int ret;

	ret = regmap_write(ext->regmap, CHT_WC_PHYCTRL, state);
	if (ret)
		dev_err(ext->dev, "Error writing phyctrl: %d\n", ret);
}

static void cht_wc_extcon_set_5v_boost(struct cht_wc_extcon_data *ext,
				       bool enable)
{
	int ret, val;

	/*
	 * The 5V boost converter is enabled through a gpio on the PMIC, since
	 * there currently is no gpio driver we access the gpio reg directly.
	 */
	val = CHT_WC_VBUS_GPIO_CTLO_DRV_OD | CHT_WC_VBUS_GPIO_CTLO_DIR_OUT;
	if (enable)
		val |= CHT_WC_VBUS_GPIO_CTLO_OUTPUT;

	ret = regmap_write(ext->regmap, CHT_WC_VBUS_GPIO_CTLO, val);
	if (ret)
		dev_err(ext->dev, "Error writing Vbus GPIO CTLO: %d\n", ret);
}

static void cht_wc_extcon_set_otgmode(struct cht_wc_extcon_data *ext,
				      bool enable)
{
	unsigned int val = enable ? CHT_WC_CHGRCTRL1_OTGMODE : 0;
	int ret;

	ret = regmap_update_bits(ext->regmap, CHT_WC_CHGRCTRL1,
				 CHT_WC_CHGRCTRL1_OTGMODE, val);
	if (ret)
		dev_err(ext->dev, "Error updating CHGRCTRL1 reg: %d\n", ret);

	if (ext->vbus_boost && ext->vbus_boost_enabled != enable) {
		if (enable)
			ret = regulator_enable(ext->vbus_boost);
		else
			ret = regulator_disable(ext->vbus_boost);

		if (ret)
			dev_err(ext->dev, "Error updating Vbus boost regulator: %d\n", ret);
		else
			ext->vbus_boost_enabled = enable;
	}
}

static void cht_wc_extcon_enable_charging(struct cht_wc_extcon_data *ext,
					  bool enable)
{
	unsigned int val = enable ? 0 : CHT_WC_CHGDISCTRL_OUT;
	int ret;

	ret = regmap_update_bits(ext->regmap, CHT_WC_CHGDISCTRL,
				 CHT_WC_CHGDISCTRL_OUT, val);
	if (ret)
		dev_err(ext->dev, "Error updating CHGDISCTRL reg: %d\n", ret);
}

/* Small helper to sync EXTCON_CHG_USB_SDP and EXTCON_USB state */
static void cht_wc_extcon_set_state(struct cht_wc_extcon_data *ext,
				    unsigned int cable, bool state)
{
	extcon_set_state_sync(ext->edev, cable, state);
	if (cable == EXTCON_CHG_USB_SDP)
		extcon_set_state_sync(ext->edev, EXTCON_USB, state);
}

static void cht_wc_extcon_pwrsrc_event(struct cht_wc_extcon_data *ext)
{
	int ret, pwrsrc_sts, id;
	unsigned int cable = EXTCON_NONE;
	/* Ignore errors in host mode, as the 5v boost converter is on then */
	bool ignore_get_charger_errors = ext->usb_host;
	enum usb_role role;

	ret = regmap_read(ext->regmap, CHT_WC_PWRSRC_STS, &pwrsrc_sts);
	if (ret) {
		dev_err(ext->dev, "Error reading pwrsrc status: %d\n", ret);
		return;
	}

	id = cht_wc_extcon_get_id(ext, pwrsrc_sts);
	if (id == INTEL_USB_ID_GND) {
		cht_wc_extcon_enable_charging(ext, false);
		cht_wc_extcon_set_otgmode(ext, true);

		/* The 5v boost causes a false VBUS / SDP detect, skip */
		goto charger_det_done;
	}

	cht_wc_extcon_set_otgmode(ext, false);
	cht_wc_extcon_enable_charging(ext, true);

	/* Plugged into a host/charger or not connected? */
	if (!(pwrsrc_sts & CHT_WC_PWRSRC_VBUS)) {
		/* Route D+ and D- to PMIC for future charger detection */
		cht_wc_extcon_set_phymux(ext, MUX_SEL_PMIC);
		goto set_state;
	}

	ret = cht_wc_extcon_get_charger(ext, ignore_get_charger_errors);
	if (ret >= 0)
		cable = ret;

charger_det_done:
	/* Route D+ and D- to SoC for the host or gadget controller */
	cht_wc_extcon_set_phymux(ext, MUX_SEL_SOC);

set_state:
	if (cable != ext->previous_cable) {
		cht_wc_extcon_set_state(ext, cable, true);
		cht_wc_extcon_set_state(ext, ext->previous_cable, false);
		ext->previous_cable = cable;
	}

	ext->usb_host = ((id == INTEL_USB_ID_GND) || (id == INTEL_USB_RID_A));
	extcon_set_state_sync(ext->edev, EXTCON_USB_HOST, ext->usb_host);

	if (ext->usb_host)
		role = USB_ROLE_HOST;
	else if (pwrsrc_sts & CHT_WC_PWRSRC_VBUS)
		role = USB_ROLE_DEVICE;
	else
		role = USB_ROLE_NONE;

	/* Note: this is a no-op when ext->role_sw is NULL */
	ret = usb_role_switch_set_role(ext->role_sw, role);
	if (ret)
		dev_err(ext->dev, "Error setting USB-role: %d\n", ret);
}

static irqreturn_t cht_wc_extcon_isr(int irq, void *data)
{
	struct cht_wc_extcon_data *ext = data;
	int ret, irqs;

	ret = regmap_read(ext->regmap, CHT_WC_PWRSRC_IRQ, &irqs);
	if (ret) {
		dev_err(ext->dev, "Error reading irqs: %d\n", ret);
		return IRQ_NONE;
	}

	cht_wc_extcon_pwrsrc_event(ext);

	ret = regmap_write(ext->regmap, CHT_WC_PWRSRC_IRQ, irqs);
	if (ret) {
		dev_err(ext->dev, "Error writing irqs: %d\n", ret);
		return IRQ_NONE;
	}

	return IRQ_HANDLED;
}

static int cht_wc_extcon_sw_control(struct cht_wc_extcon_data *ext, bool enable)
{
	int ret, mask, val;

	val = enable ? 0 : CHT_WC_CHGDISCTRL_FN;
	ret = regmap_update_bits(ext->regmap, CHT_WC_CHGDISCTRL,
				 CHT_WC_CHGDISCTRL_FN, val);
	if (ret)
		dev_err(ext->dev,
			"Error setting sw control for CHGDIS pin: %d\n",
			ret);

	mask = CHT_WC_CHGRCTRL0_SWCONTROL | CHT_WC_CHGRCTRL0_CCSM_OFF;
	val = enable ? mask : 0;
	ret = regmap_update_bits(ext->regmap, CHT_WC_CHGRCTRL0, mask, val);
	if (ret)
		dev_err(ext->dev, "Error setting sw control: %d\n", ret);

	return ret;
}

static int cht_wc_extcon_find_role_sw(struct cht_wc_extcon_data *ext)
{
	const struct software_node *swnode;
	struct fwnode_handle *fwnode;

	swnode = software_node_find_by_name(NULL, "intel-xhci-usb-sw");
	if (!swnode)
		return -EPROBE_DEFER;

	fwnode = software_node_fwnode(swnode);
	ext->role_sw = usb_role_switch_find_by_fwnode(fwnode);
	fwnode_handle_put(fwnode);

	return ext->role_sw ? 0 : -EPROBE_DEFER;
}

static void cht_wc_extcon_put_role_sw(void *data)
{
	struct cht_wc_extcon_data *ext = data;

	usb_role_switch_put(ext->role_sw);
}

/* Some boards require controlling the role-sw and Vbus based on the id-pin */
static int cht_wc_extcon_get_role_sw_and_regulator(struct cht_wc_extcon_data *ext)
{
	int ret;

	ret = cht_wc_extcon_find_role_sw(ext);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(ext->dev, cht_wc_extcon_put_role_sw, ext);
	if (ret)
		return ret;

	/*
	 * On x86/ACPI platforms the regulator <-> consumer link is provided
	 * by platform_data passed to the regulator driver. This means that
	 * this info is not available before the regulator driver has bound.
	 * Use devm_regulator_get_optional() to avoid getting a dummy
	 * regulator and wait for the regulator to show up if necessary.
	 */
	ext->vbus_boost = devm_regulator_get_optional(ext->dev, "vbus");
	if (IS_ERR(ext->vbus_boost)) {
		ret = PTR_ERR(ext->vbus_boost);
		if (ret == -ENODEV)
			ret = -EPROBE_DEFER;

		return dev_err_probe(ext->dev, ret, "getting Vbus regulator");
	}

	return 0;
}

static int cht_wc_extcon_probe(struct platform_device *pdev)
{
	struct intel_soc_pmic *pmic = dev_get_drvdata(pdev->dev.parent);
	struct cht_wc_extcon_data *ext;
	unsigned long mask = ~(CHT_WC_PWRSRC_VBUS | CHT_WC_PWRSRC_USBID_MASK);
	int pwrsrc_sts, id;
	int irq, ret;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ext = devm_kzalloc(&pdev->dev, sizeof(*ext), GFP_KERNEL);
	if (!ext)
		return -ENOMEM;

	ext->dev = &pdev->dev;
	ext->regmap = pmic->regmap;
	ext->previous_cable = EXTCON_NONE;

	/* Initialize extcon device */
	ext->edev = devm_extcon_dev_allocate(ext->dev, cht_wc_extcon_cables);
	if (IS_ERR(ext->edev))
		return PTR_ERR(ext->edev);

	switch (pmic->cht_wc_model) {
	case INTEL_CHT_WC_GPD_WIN_POCKET:
		/*
		 * When a host-cable is detected the BIOS enables an external 5v boost
		 * converter to power connected devices there are 2 problems with this:
		 * 1) This gets seen by the external battery charger as a valid Vbus
		 *    supply and it then tries to feed Vsys from this creating a
		 *    feedback loop which causes aprox. 300 mA extra battery drain
		 *    (and unless we drive the external-charger-disable pin high it
		 *    also tries to charge the battery causing even more feedback).
		 * 2) This gets seen by the pwrsrc block as a SDP USB Vbus supply
		 * Since the external battery charger has its own 5v boost converter
		 * which does not have these issues, we simply turn the separate
		 * external 5v boost converter off and leave it off entirely.
		 */
		cht_wc_extcon_set_5v_boost(ext, false);
		break;
	case INTEL_CHT_WC_LENOVO_YOGABOOK1:
	case INTEL_CHT_WC_XIAOMI_MIPAD2:
		ret = cht_wc_extcon_get_role_sw_and_regulator(ext);
		if (ret)
			return ret;
		break;
	default:
		break;
	}

	/* Enable sw control */
	ret = cht_wc_extcon_sw_control(ext, true);
	if (ret)
		goto disable_sw_control;

	/* Disable charging by external battery charger */
	cht_wc_extcon_enable_charging(ext, false);

	/* Register extcon device */
	ret = devm_extcon_dev_register(ext->dev, ext->edev);
	if (ret) {
		dev_err(ext->dev, "Error registering extcon device: %d\n", ret);
		goto disable_sw_control;
	}

	ret = regmap_read(ext->regmap, CHT_WC_PWRSRC_STS, &pwrsrc_sts);
	if (ret) {
		dev_err(ext->dev, "Error reading pwrsrc status: %d\n", ret);
		goto disable_sw_control;
	}

	/*
	 * If no USB host or device connected, route D+ and D- to PMIC for
	 * initial charger detection
	 */
	id = cht_wc_extcon_get_id(ext, pwrsrc_sts);
	if (id != INTEL_USB_ID_GND)
		cht_wc_extcon_set_phymux(ext, MUX_SEL_PMIC);

	/* Get initial state */
	cht_wc_extcon_pwrsrc_event(ext);

	ret = devm_request_threaded_irq(ext->dev, irq, NULL, cht_wc_extcon_isr,
					IRQF_ONESHOT, pdev->name, ext);
	if (ret) {
		dev_err(ext->dev, "Error requesting interrupt: %d\n", ret);
		goto disable_sw_control;
	}

	/* Unmask irqs */
	ret = regmap_write(ext->regmap, CHT_WC_PWRSRC_IRQ_MASK, mask);
	if (ret) {
		dev_err(ext->dev, "Error writing irq-mask: %d\n", ret);
		goto disable_sw_control;
	}

	platform_set_drvdata(pdev, ext);

	return 0;

disable_sw_control:
	cht_wc_extcon_sw_control(ext, false);
	return ret;
}

static int cht_wc_extcon_remove(struct platform_device *pdev)
{
	struct cht_wc_extcon_data *ext = platform_get_drvdata(pdev);

	cht_wc_extcon_sw_control(ext, false);

	return 0;
}

static const struct platform_device_id cht_wc_extcon_table[] = {
	{ .name = "cht_wcove_pwrsrc" },
	{},
};
MODULE_DEVICE_TABLE(platform, cht_wc_extcon_table);

static struct platform_driver cht_wc_extcon_driver = {
	.probe = cht_wc_extcon_probe,
	.remove = cht_wc_extcon_remove,
	.id_table = cht_wc_extcon_table,
	.driver = {
		.name = "cht_wcove_pwrsrc",
	},
};
module_platform_driver(cht_wc_extcon_driver);

MODULE_DESCRIPTION("Intel Cherrytrail Whiskey Cove PMIC extcon driver");
MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_LICENSE("GPL v2");
