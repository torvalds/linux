// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/usb/role.h>
#include <linux/usb/typec_mux.h>

#define TYPEC_MISC_STATUS		0xb
#define CC_ATTACHED			BIT(0)
#define CC_ORIENTATION			BIT(1)
#define SNK_SRC_MODE			BIT(6)
#define TYPEC_MODE_CFG			0x44
#define TYPEC_DISABLE_CMD		BIT(0)
#define EN_SNK_ONLY			BIT(1)
#define EN_SRC_ONLY			BIT(2)
#define TYPEC_VCONN_CONTROL		0x46
#define VCONN_EN_SRC			BIT(0)
#define VCONN_EN_VAL			BIT(1)
#define TYPEC_EXIT_STATE_CFG		0x50
#define SEL_SRC_UPPER_REF		BIT(2)
#define TYPEC_INTR_EN_CFG_1		0x5e
#define TYPEC_INTR_EN_CFG_1_MASK	GENMASK(7, 0)

struct qcom_pmic_typec {
	struct device		*dev;
	struct regmap		*regmap;
	u32			base;

	struct typec_port	*port;
	struct usb_role_switch *role_sw;

	struct regulator	*vbus_reg;
	bool			vbus_enabled;
};

static void qcom_pmic_typec_enable_vbus_regulator(struct qcom_pmic_typec
							*qcom_usb, bool enable)
{
	int ret;

	if (enable == qcom_usb->vbus_enabled)
		return;

	if (enable) {
		ret = regulator_enable(qcom_usb->vbus_reg);
		if (ret)
			return;
	} else {
		ret = regulator_disable(qcom_usb->vbus_reg);
		if (ret)
			return;
	}
	qcom_usb->vbus_enabled = enable;
}

static void qcom_pmic_typec_check_connection(struct qcom_pmic_typec *qcom_usb)
{
	enum typec_orientation orientation;
	enum usb_role role;
	unsigned int stat;
	bool enable_vbus;

	regmap_read(qcom_usb->regmap, qcom_usb->base + TYPEC_MISC_STATUS,
		    &stat);

	if (stat & CC_ATTACHED) {
		orientation = (stat & CC_ORIENTATION) ?
				TYPEC_ORIENTATION_REVERSE :
				TYPEC_ORIENTATION_NORMAL;
		typec_set_orientation(qcom_usb->port, orientation);

		role = (stat & SNK_SRC_MODE) ? USB_ROLE_HOST : USB_ROLE_DEVICE;
		if (role == USB_ROLE_HOST)
			enable_vbus = true;
		else
			enable_vbus = false;
	} else {
		role = USB_ROLE_NONE;
		enable_vbus = false;
	}

	qcom_pmic_typec_enable_vbus_regulator(qcom_usb, enable_vbus);
	usb_role_switch_set_role(qcom_usb->role_sw, role);
}

static irqreturn_t qcom_pmic_typec_interrupt(int irq, void *_qcom_usb)
{
	struct qcom_pmic_typec *qcom_usb = _qcom_usb;

	qcom_pmic_typec_check_connection(qcom_usb);
	return IRQ_HANDLED;
}

static void qcom_pmic_typec_typec_hw_init(struct qcom_pmic_typec *qcom_usb,
					  enum typec_port_type type)
{
	u8 mode = 0;

	regmap_update_bits(qcom_usb->regmap,
			   qcom_usb->base + TYPEC_INTR_EN_CFG_1,
			   TYPEC_INTR_EN_CFG_1_MASK, 0);

	if (type == TYPEC_PORT_SRC)
		mode = EN_SRC_ONLY;
	else if (type == TYPEC_PORT_SNK)
		mode = EN_SNK_ONLY;

	regmap_update_bits(qcom_usb->regmap, qcom_usb->base + TYPEC_MODE_CFG,
			   EN_SNK_ONLY | EN_SRC_ONLY, mode);

	regmap_update_bits(qcom_usb->regmap,
			   qcom_usb->base + TYPEC_VCONN_CONTROL,
			   VCONN_EN_SRC | VCONN_EN_VAL, VCONN_EN_SRC);
	regmap_update_bits(qcom_usb->regmap,
			   qcom_usb->base + TYPEC_EXIT_STATE_CFG,
			   SEL_SRC_UPPER_REF, SEL_SRC_UPPER_REF);
}

static int qcom_pmic_typec_probe(struct platform_device *pdev)
{
	struct qcom_pmic_typec *qcom_usb;
	struct device *dev = &pdev->dev;
	struct fwnode_handle *fwnode;
	struct typec_capability cap;
	const char *buf;
	int ret, irq, role;
	u32 reg;

	ret = device_property_read_u32(dev, "reg", &reg);
	if (ret < 0) {
		dev_err(dev, "missing base address\n");
		return ret;
	}

	qcom_usb = devm_kzalloc(dev, sizeof(*qcom_usb), GFP_KERNEL);
	if (!qcom_usb)
		return -ENOMEM;

	qcom_usb->dev = dev;
	qcom_usb->base = reg;

	qcom_usb->regmap = dev_get_regmap(dev->parent, NULL);
	if (!qcom_usb->regmap) {
		dev_err(dev, "Failed to get regmap\n");
		return -EINVAL;
	}

	qcom_usb->vbus_reg = devm_regulator_get(qcom_usb->dev, "usb_vbus");
	if (IS_ERR(qcom_usb->vbus_reg))
		return PTR_ERR(qcom_usb->vbus_reg);

	fwnode = device_get_named_child_node(dev, "connector");
	if (!fwnode)
		return -EINVAL;

	ret = fwnode_property_read_string(fwnode, "power-role", &buf);
	if (!ret) {
		role = typec_find_port_power_role(buf);
		if (role < 0)
			role = TYPEC_PORT_SNK;
	} else {
		role = TYPEC_PORT_SNK;
	}
	cap.type = role;

	ret = fwnode_property_read_string(fwnode, "data-role", &buf);
	if (!ret) {
		role = typec_find_port_data_role(buf);
		if (role < 0)
			role = TYPEC_PORT_UFP;
	} else {
		role = TYPEC_PORT_UFP;
	}
	cap.data = role;

	cap.prefer_role = TYPEC_NO_PREFERRED_ROLE;
	cap.fwnode = fwnode;
	qcom_usb->port = typec_register_port(dev, &cap);
	if (IS_ERR(qcom_usb->port)) {
		ret = PTR_ERR(qcom_usb->port);
		dev_err(dev, "Failed to register type c port %d\n", ret);
		goto err_put_node;
	}
	fwnode_handle_put(fwnode);

	qcom_usb->role_sw = fwnode_usb_role_switch_get(dev_fwnode(qcom_usb->dev));
	if (IS_ERR(qcom_usb->role_sw)) {
		if (PTR_ERR(qcom_usb->role_sw) != -EPROBE_DEFER)
			dev_err(dev, "failed to get role switch\n");
		ret = PTR_ERR(qcom_usb->role_sw);
		goto err_typec_port;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		goto err_usb_role_sw;

	ret = devm_request_threaded_irq(qcom_usb->dev, irq, NULL,
					qcom_pmic_typec_interrupt, IRQF_ONESHOT,
					"qcom-pmic-typec", qcom_usb);
	if (ret) {
		dev_err(&pdev->dev, "Could not request IRQ\n");
		goto err_usb_role_sw;
	}

	platform_set_drvdata(pdev, qcom_usb);
	qcom_pmic_typec_typec_hw_init(qcom_usb, cap.type);
	qcom_pmic_typec_check_connection(qcom_usb);

	return 0;

err_usb_role_sw:
	usb_role_switch_put(qcom_usb->role_sw);
err_typec_port:
	typec_unregister_port(qcom_usb->port);
err_put_node:
	fwnode_handle_put(fwnode);

	return ret;
}

static int qcom_pmic_typec_remove(struct platform_device *pdev)
{
	struct qcom_pmic_typec *qcom_usb = platform_get_drvdata(pdev);

	usb_role_switch_set_role(qcom_usb->role_sw, USB_ROLE_NONE);
	qcom_pmic_typec_enable_vbus_regulator(qcom_usb, 0);

	typec_unregister_port(qcom_usb->port);
	usb_role_switch_put(qcom_usb->role_sw);

	return 0;
}

static const struct of_device_id qcom_pmic_typec_table[] = {
	{ .compatible = "qcom,pm8150b-usb-typec" },
	{ }
};
MODULE_DEVICE_TABLE(of, qcom_pmic_typec_table);

static struct platform_driver qcom_pmic_typec = {
	.driver = {
		.name = "qcom,pmic-typec",
		.of_match_table = qcom_pmic_typec_table,
	},
	.probe = qcom_pmic_typec_probe,
	.remove = qcom_pmic_typec_remove,
};
module_platform_driver(qcom_pmic_typec);

MODULE_DESCRIPTION("QCOM PMIC USB type C driver");
MODULE_LICENSE("GPL v2");
