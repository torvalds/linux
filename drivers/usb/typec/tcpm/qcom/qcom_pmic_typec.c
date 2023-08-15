// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023, Linaro Ltd. All rights reserved.
 */

#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/usb/role.h>
#include <linux/usb/tcpm.h>
#include <linux/usb/typec_mux.h>
#include "qcom_pmic_typec_pdphy.h"
#include "qcom_pmic_typec_port.h"

struct pmic_typec_resources {
	struct pmic_typec_pdphy_resources	*pdphy_res;
	struct pmic_typec_port_resources	*port_res;
};

struct pmic_typec {
	struct device		*dev;
	struct tcpm_port	*tcpm_port;
	struct tcpc_dev		tcpc;
	struct pmic_typec_pdphy	*pmic_typec_pdphy;
	struct pmic_typec_port	*pmic_typec_port;
	bool			vbus_enabled;
	struct mutex		lock;		/* VBUS state serialization */
};

#define tcpc_to_tcpm(_tcpc_) container_of(_tcpc_, struct pmic_typec, tcpc)

static int qcom_pmic_typec_get_vbus(struct tcpc_dev *tcpc)
{
	struct pmic_typec *tcpm = tcpc_to_tcpm(tcpc);
	int ret;

	mutex_lock(&tcpm->lock);
	ret = tcpm->vbus_enabled || qcom_pmic_typec_port_get_vbus(tcpm->pmic_typec_port);
	mutex_unlock(&tcpm->lock);

	return ret;
}

static int qcom_pmic_typec_set_vbus(struct tcpc_dev *tcpc, bool on, bool sink)
{
	struct pmic_typec *tcpm = tcpc_to_tcpm(tcpc);
	int ret = 0;

	mutex_lock(&tcpm->lock);
	if (tcpm->vbus_enabled == on)
		goto done;

	ret = qcom_pmic_typec_port_set_vbus(tcpm->pmic_typec_port, on);
	if (ret)
		goto done;

	tcpm->vbus_enabled = on;
	tcpm_vbus_change(tcpm->tcpm_port);

done:
	dev_dbg(tcpm->dev, "set_vbus set: %d result %d\n", on, ret);
	mutex_unlock(&tcpm->lock);

	return ret;
}

static int qcom_pmic_typec_set_vconn(struct tcpc_dev *tcpc, bool on)
{
	struct pmic_typec *tcpm = tcpc_to_tcpm(tcpc);

	return qcom_pmic_typec_port_set_vconn(tcpm->pmic_typec_port, on);
}

static int qcom_pmic_typec_get_cc(struct tcpc_dev *tcpc,
				  enum typec_cc_status *cc1,
				  enum typec_cc_status *cc2)
{
	struct pmic_typec *tcpm = tcpc_to_tcpm(tcpc);

	return qcom_pmic_typec_port_get_cc(tcpm->pmic_typec_port, cc1, cc2);
}

static int qcom_pmic_typec_set_cc(struct tcpc_dev *tcpc,
				  enum typec_cc_status cc)
{
	struct pmic_typec *tcpm = tcpc_to_tcpm(tcpc);

	return qcom_pmic_typec_port_set_cc(tcpm->pmic_typec_port, cc);
}

static int qcom_pmic_typec_set_polarity(struct tcpc_dev *tcpc,
					enum typec_cc_polarity pol)
{
	/* Polarity is set separately by phy-qcom-qmp.c */
	return 0;
}

static int qcom_pmic_typec_start_toggling(struct tcpc_dev *tcpc,
					  enum typec_port_type port_type,
					  enum typec_cc_status cc)
{
	struct pmic_typec *tcpm = tcpc_to_tcpm(tcpc);

	return qcom_pmic_typec_port_start_toggling(tcpm->pmic_typec_port,
						   port_type, cc);
}

static int qcom_pmic_typec_set_roles(struct tcpc_dev *tcpc, bool attached,
				     enum typec_role power_role,
				     enum typec_data_role data_role)
{
	struct pmic_typec *tcpm = tcpc_to_tcpm(tcpc);

	return qcom_pmic_typec_pdphy_set_roles(tcpm->pmic_typec_pdphy,
					       data_role, power_role);
}

static int qcom_pmic_typec_set_pd_rx(struct tcpc_dev *tcpc, bool on)
{
	struct pmic_typec *tcpm = tcpc_to_tcpm(tcpc);

	return qcom_pmic_typec_pdphy_set_pd_rx(tcpm->pmic_typec_pdphy, on);
}

static int qcom_pmic_typec_pd_transmit(struct tcpc_dev *tcpc,
				       enum tcpm_transmit_type type,
				       const struct pd_message *msg,
				       unsigned int negotiated_rev)
{
	struct pmic_typec *tcpm = tcpc_to_tcpm(tcpc);

	return qcom_pmic_typec_pdphy_pd_transmit(tcpm->pmic_typec_pdphy, type,
						 msg, negotiated_rev);
}

static int qcom_pmic_typec_init(struct tcpc_dev *tcpc)
{
	return 0;
}

static int qcom_pmic_typec_probe(struct platform_device *pdev)
{
	struct pmic_typec *tcpm;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	const struct pmic_typec_resources *res;
	struct regmap *regmap;
	u32 base[2];
	int ret;

	res = of_device_get_match_data(dev);
	if (!res)
		return -ENODEV;

	tcpm = devm_kzalloc(dev, sizeof(*tcpm), GFP_KERNEL);
	if (!tcpm)
		return -ENOMEM;

	tcpm->dev = dev;
	tcpm->tcpc.init = qcom_pmic_typec_init;
	tcpm->tcpc.get_vbus = qcom_pmic_typec_get_vbus;
	tcpm->tcpc.set_vbus = qcom_pmic_typec_set_vbus;
	tcpm->tcpc.set_cc = qcom_pmic_typec_set_cc;
	tcpm->tcpc.get_cc = qcom_pmic_typec_get_cc;
	tcpm->tcpc.set_polarity = qcom_pmic_typec_set_polarity;
	tcpm->tcpc.set_vconn = qcom_pmic_typec_set_vconn;
	tcpm->tcpc.start_toggling = qcom_pmic_typec_start_toggling;
	tcpm->tcpc.set_pd_rx = qcom_pmic_typec_set_pd_rx;
	tcpm->tcpc.set_roles = qcom_pmic_typec_set_roles;
	tcpm->tcpc.pd_transmit = qcom_pmic_typec_pd_transmit;

	regmap = dev_get_regmap(dev->parent, NULL);
	if (!regmap) {
		dev_err(dev, "Failed to get regmap\n");
		return -ENODEV;
	}

	ret = of_property_read_u32_array(np, "reg", base, 2);
	if (ret)
		return ret;

	tcpm->pmic_typec_port = qcom_pmic_typec_port_alloc(dev);
	if (IS_ERR(tcpm->pmic_typec_port))
		return PTR_ERR(tcpm->pmic_typec_port);

	tcpm->pmic_typec_pdphy = qcom_pmic_typec_pdphy_alloc(dev);
	if (IS_ERR(tcpm->pmic_typec_pdphy))
		return PTR_ERR(tcpm->pmic_typec_pdphy);

	ret = qcom_pmic_typec_port_probe(pdev, tcpm->pmic_typec_port,
					 res->port_res, regmap, base[0]);
	if (ret)
		return ret;

	ret = qcom_pmic_typec_pdphy_probe(pdev, tcpm->pmic_typec_pdphy,
					  res->pdphy_res, regmap, base[1]);
	if (ret)
		return ret;

	mutex_init(&tcpm->lock);
	platform_set_drvdata(pdev, tcpm);

	tcpm->tcpc.fwnode = device_get_named_child_node(tcpm->dev, "connector");
	if (!tcpm->tcpc.fwnode)
		return -EINVAL;

	tcpm->tcpm_port = tcpm_register_port(tcpm->dev, &tcpm->tcpc);
	if (IS_ERR(tcpm->tcpm_port)) {
		ret = PTR_ERR(tcpm->tcpm_port);
		goto fwnode_remove;
	}

	ret = qcom_pmic_typec_port_start(tcpm->pmic_typec_port,
					 tcpm->tcpm_port);
	if (ret)
		goto fwnode_remove;

	ret = qcom_pmic_typec_pdphy_start(tcpm->pmic_typec_pdphy,
					  tcpm->tcpm_port);
	if (ret)
		goto fwnode_remove;

	return 0;

fwnode_remove:
	fwnode_remove_software_node(tcpm->tcpc.fwnode);

	return ret;
}

static void qcom_pmic_typec_remove(struct platform_device *pdev)
{
	struct pmic_typec *tcpm = platform_get_drvdata(pdev);

	qcom_pmic_typec_pdphy_stop(tcpm->pmic_typec_pdphy);
	qcom_pmic_typec_port_stop(tcpm->pmic_typec_port);
	tcpm_unregister_port(tcpm->tcpm_port);
	fwnode_remove_software_node(tcpm->tcpc.fwnode);
}

static struct pmic_typec_pdphy_resources pm8150b_pdphy_res = {
	.irq_params = {
		{
			.virq = PMIC_PDPHY_SIG_TX_IRQ,
			.irq_name = "sig-tx",
		},
		{
			.virq = PMIC_PDPHY_SIG_RX_IRQ,
			.irq_name = "sig-rx",
		},
		{
			.virq = PMIC_PDPHY_MSG_TX_IRQ,
			.irq_name = "msg-tx",
		},
		{
			.virq = PMIC_PDPHY_MSG_RX_IRQ,
			.irq_name = "msg-rx",
		},
		{
			.virq = PMIC_PDPHY_MSG_TX_FAIL_IRQ,
			.irq_name = "msg-tx-failed",
		},
		{
			.virq = PMIC_PDPHY_MSG_TX_DISCARD_IRQ,
			.irq_name = "msg-tx-discarded",
		},
		{
			.virq = PMIC_PDPHY_MSG_RX_DISCARD_IRQ,
			.irq_name = "msg-rx-discarded",
		},
	},
	.nr_irqs = 7,
};

static struct pmic_typec_port_resources pm8150b_port_res = {
	.irq_params = {
		{
			.irq_name = "vpd-detect",
			.virq = PMIC_TYPEC_VPD_IRQ,
		},

		{
			.irq_name = "cc-state-change",
			.virq = PMIC_TYPEC_CC_STATE_IRQ,
		},
		{
			.irq_name = "vconn-oc",
			.virq = PMIC_TYPEC_VCONN_OC_IRQ,
		},

		{
			.irq_name = "vbus-change",
			.virq = PMIC_TYPEC_VBUS_IRQ,
		},

		{
			.irq_name = "attach-detach",
			.virq = PMIC_TYPEC_ATTACH_DETACH_IRQ,
		},
		{
			.irq_name = "legacy-cable-detect",
			.virq = PMIC_TYPEC_LEGACY_CABLE_IRQ,
		},

		{
			.irq_name = "try-snk-src-detect",
			.virq = PMIC_TYPEC_TRY_SNK_SRC_IRQ,
		},
	},
	.nr_irqs = 7,
};

static struct pmic_typec_resources pm8150b_typec_res = {
	.pdphy_res = &pm8150b_pdphy_res,
	.port_res = &pm8150b_port_res,
};

static const struct of_device_id qcom_pmic_typec_table[] = {
	{ .compatible = "qcom,pm8150b-typec", .data = &pm8150b_typec_res },
	{ }
};
MODULE_DEVICE_TABLE(of, qcom_pmic_typec_table);

static struct platform_driver qcom_pmic_typec_driver = {
	.driver = {
		.name = "qcom,pmic-typec",
		.of_match_table = qcom_pmic_typec_table,
	},
	.probe = qcom_pmic_typec_probe,
	.remove_new = qcom_pmic_typec_remove,
};

module_platform_driver(qcom_pmic_typec_driver);

MODULE_DESCRIPTION("QCOM PMIC USB Type-C Port Manager Driver");
MODULE_LICENSE("GPL");
