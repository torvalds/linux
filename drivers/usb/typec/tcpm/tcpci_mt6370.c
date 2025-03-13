// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Richtek Technology Corp.
 *
 * Author: ChiYuan Huang <cy_huang@richtek.com>
 */

#include <linux/bits.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeirq.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/usb/tcpci.h>
#include <linux/usb/tcpm.h>

#define MT6370_REG_SYSCTRL8	0x9B

#define MT6370_AUTOIDLE_MASK	BIT(3)

#define MT6370_VENDOR_ID	0x29CF
#define MT6370_TCPC_DID_A	0x2170

struct mt6370_priv {
	struct device *dev;
	struct regulator *vbus;
	struct tcpci *tcpci;
	struct tcpci_data tcpci_data;
};

static const struct reg_sequence mt6370_reg_init[] = {
	REG_SEQ(0xA0, 0x1, 1000),
	REG_SEQ(0x81, 0x38, 0),
	REG_SEQ(0x82, 0x82, 0),
	REG_SEQ(0xBA, 0xFC, 0),
	REG_SEQ(0xBB, 0x50, 0),
	REG_SEQ(0x9E, 0x8F, 0),
	REG_SEQ(0xA1, 0x5, 0),
	REG_SEQ(0xA2, 0x4, 0),
	REG_SEQ(0xA3, 0x4A, 0),
	REG_SEQ(0xA4, 0x01, 0),
	REG_SEQ(0x95, 0x01, 0),
	REG_SEQ(0x80, 0x71, 0),
	REG_SEQ(0x9B, 0x3A, 1000),
};

static int mt6370_tcpc_init(struct tcpci *tcpci, struct tcpci_data *data)
{
	u16 did;
	int ret;

	ret = regmap_register_patch(data->regmap, mt6370_reg_init,
				    ARRAY_SIZE(mt6370_reg_init));
	if (ret)
		return ret;

	ret = regmap_raw_read(data->regmap, TCPC_BCD_DEV, &did, sizeof(u16));
	if (ret)
		return ret;

	if (did == MT6370_TCPC_DID_A)
		return regmap_write(data->regmap, TCPC_FAULT_CTRL, 0x80);

	return 0;
}

static int mt6370_tcpc_set_vconn(struct tcpci *tcpci, struct tcpci_data *data,
				 bool enable)
{
	return regmap_update_bits(data->regmap, MT6370_REG_SYSCTRL8,
				  MT6370_AUTOIDLE_MASK,
				  enable ? 0 : MT6370_AUTOIDLE_MASK);
}

static int mt6370_tcpc_set_vbus(struct tcpci *tcpci, struct tcpci_data *data,
				bool source, bool sink)
{
	struct mt6370_priv *priv = container_of(data, struct mt6370_priv,
						tcpci_data);
	int ret;

	ret = regulator_is_enabled(priv->vbus);
	if (ret < 0)
		return ret;

	if (ret && !source)
		return regulator_disable(priv->vbus);

	if (!ret && source)
		return regulator_enable(priv->vbus);

	return 0;
}

static irqreturn_t mt6370_irq_handler(int irq, void *dev_id)
{
	struct mt6370_priv *priv = dev_id;

	return tcpci_irq(priv->tcpci);
}

static int mt6370_check_vendor_info(struct mt6370_priv *priv)
{
	struct regmap *regmap = priv->tcpci_data.regmap;
	u16 vid;
	int ret;

	ret = regmap_raw_read(regmap, TCPC_VENDOR_ID, &vid, sizeof(u16));
	if (ret)
		return ret;

	if (vid != MT6370_VENDOR_ID)
		return dev_err_probe(priv->dev, -ENODEV,
				     "Vendor ID not correct 0x%02x\n", vid);

	return 0;
}

static void mt6370_unregister_tcpci_port(void *tcpci)
{
	tcpci_unregister_port(tcpci);
}

static int mt6370_tcpc_probe(struct platform_device *pdev)
{
	struct mt6370_priv *priv;
	struct device *dev = &pdev->dev;
	int irq, ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;

	priv->tcpci_data.regmap = dev_get_regmap(dev->parent, NULL);
	if (!priv->tcpci_data.regmap)
		return dev_err_probe(dev, -ENODEV, "Failed to init regmap\n");

	ret = mt6370_check_vendor_info(priv);
	if (ret)
		return ret;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	/* Assign TCPCI feature and ops */
	priv->tcpci_data.auto_discharge_disconnect = 1;
	priv->tcpci_data.init = mt6370_tcpc_init;
	priv->tcpci_data.set_vconn = mt6370_tcpc_set_vconn;

	priv->vbus = devm_regulator_get_optional(dev, "vbus");
	if (!IS_ERR(priv->vbus))
		priv->tcpci_data.set_vbus = mt6370_tcpc_set_vbus;

	priv->tcpci = tcpci_register_port(dev, &priv->tcpci_data);
	if (IS_ERR(priv->tcpci))
		return dev_err_probe(dev, PTR_ERR(priv->tcpci),
				     "Failed to register tcpci port\n");

	ret = devm_add_action_or_reset(dev, mt6370_unregister_tcpci_port, priv->tcpci);
	if (ret)
		return ret;

	ret = devm_request_threaded_irq(dev, irq, NULL, mt6370_irq_handler,
					IRQF_ONESHOT, dev_name(dev), priv);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to allocate irq\n");

	device_init_wakeup(dev, true);
	dev_pm_set_wake_irq(dev, irq);

	return 0;
}

static void mt6370_tcpc_remove(struct platform_device *pdev)
{
	dev_pm_clear_wake_irq(&pdev->dev);
	device_init_wakeup(&pdev->dev, false);
}

static const struct of_device_id mt6370_tcpc_devid_table[] = {
	{ .compatible = "mediatek,mt6370-tcpc" },
	{}
};
MODULE_DEVICE_TABLE(of, mt6370_tcpc_devid_table);

static struct platform_driver mt6370_tcpc_driver = {
	.driver = {
		.name = "mt6370-tcpc",
		.of_match_table = mt6370_tcpc_devid_table,
	},
	.probe = mt6370_tcpc_probe,
	.remove = mt6370_tcpc_remove,
};
module_platform_driver(mt6370_tcpc_driver);

MODULE_AUTHOR("ChiYuan Huang <cy_huang@richtek.com>");
MODULE_DESCRIPTION("MT6370 USB Type-C Port Controller Interface Driver");
MODULE_LICENSE("GPL v2");
