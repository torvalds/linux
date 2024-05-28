// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2024, Linaro Ltd. All rights reserved.
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
#include <linux/usb/pd.h>
#include <linux/usb/tcpm.h>
#include "qcom_pmic_typec.h"
#include "qcom_pmic_typec_pdphy.h"

static int qcom_pmic_typec_pdphy_stub_pd_transmit(struct tcpc_dev *tcpc,
						  enum tcpm_transmit_type type,
						  const struct pd_message *msg,
						  unsigned int negotiated_rev)
{
	struct pmic_typec *tcpm = tcpc_to_tcpm(tcpc);
	struct device *dev = tcpm->dev;

	dev_dbg(dev, "pdphy_transmit: type=%d\n", type);

	tcpm_pd_transmit_complete(tcpm->tcpm_port,
				  TCPC_TX_SUCCESS);

	return 0;
}

static int qcom_pmic_typec_pdphy_stub_set_pd_rx(struct tcpc_dev *tcpc, bool on)
{
	struct pmic_typec *tcpm = tcpc_to_tcpm(tcpc);
	struct device *dev = tcpm->dev;

	dev_dbg(dev, "set_pd_rx: %s\n", on ? "on" : "off");

	return 0;
}

static int qcom_pmic_typec_pdphy_stub_set_roles(struct tcpc_dev *tcpc, bool attached,
						enum typec_role power_role,
						enum typec_data_role data_role)
{
	struct pmic_typec *tcpm = tcpc_to_tcpm(tcpc);
	struct device *dev = tcpm->dev;

	dev_dbg(dev, "pdphy_set_roles: data_role_host=%d power_role_src=%d\n",
		data_role, power_role);

	return 0;
}

static int qcom_pmic_typec_pdphy_stub_start(struct pmic_typec *tcpm,
					    struct tcpm_port *tcpm_port)
{
	return 0;
}

static void qcom_pmic_typec_pdphy_stub_stop(struct pmic_typec *tcpm)
{
}

int qcom_pmic_typec_pdphy_stub_probe(struct platform_device *pdev,
				     struct pmic_typec *tcpm)
{
	tcpm->tcpc.set_pd_rx = qcom_pmic_typec_pdphy_stub_set_pd_rx;
	tcpm->tcpc.set_roles = qcom_pmic_typec_pdphy_stub_set_roles;
	tcpm->tcpc.pd_transmit = qcom_pmic_typec_pdphy_stub_pd_transmit;

	tcpm->pdphy_start = qcom_pmic_typec_pdphy_stub_start;
	tcpm->pdphy_stop = qcom_pmic_typec_pdphy_stub_stop;

	return 0;
}
