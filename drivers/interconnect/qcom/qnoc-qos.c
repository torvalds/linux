// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 *
 */

#include <linux/interconnect.h>
#include <linux/interconnect-provider.h>
#include <linux/module.h>

#include "icc-rpmh.h"
#include "qnoc-qos.h"

#define QOSGEN_MAINCTL_LO(p, qp)	((p)->offsets[qp] + \
					(p)->regs[QOSGEN_OFF_MAINCTL_LO])
#define QOS_SLV_URG_MSG_EN_SHFT		3
# define QOS_DFLT_PRIO_MASK		0x7
# define QOS_DFLT_PRIO_SHFT		4

const u8 icc_qnoc_qos_regs[][QOSGEN_OFF_MAX_REGS] = {
	[ICC_QNOC_QOSGEN_TYPE_RPMH] = {
		[QOSGEN_OFF_MAINCTL_LO] = 0x8,
		[QOSGEN_OFF_LIMITBW_LO] = 0x18,
		[QOSGEN_OFF_SHAPING_LO] = 0x20,
		[QOSGEN_OFF_SHAPING_HI] = 0x24,
		[QOSGEN_OFF_REGUL0CTL_LO] = 0x40,
		[QOSGEN_OFF_REGUL0BW_LO] = 0x48,
	},
};
EXPORT_SYMBOL(icc_qnoc_qos_regs);

/**
 * qcom_icc_set_qos - initialize static QoS configurations
 * @node: qcom icc node to operate on
 */
static void qcom_icc_set_qos(struct qcom_icc_node *node)
{
	struct qcom_icc_qosbox *qos = node->qosbox;
	int port;

	if (!node->regmap)
		return;

	if (!qos)
		return;

	for (port = 0; port < qos->num_ports; port++) {
		regmap_update_bits(node->regmap, QOSGEN_MAINCTL_LO(qos, port),
				   QOS_DFLT_PRIO_MASK << QOS_DFLT_PRIO_SHFT,
				   qos->config->prio << QOS_DFLT_PRIO_SHFT);

		regmap_update_bits(node->regmap, QOSGEN_MAINCTL_LO(qos, port),
				   BIT(QOS_SLV_URG_MSG_EN_SHFT),
				   qos->config->urg_fwd << QOS_SLV_URG_MSG_EN_SHFT);
	}
}

const struct qcom_icc_noc_ops qcom_qnoc4_ops = {
	.set_qos = qcom_icc_set_qos,
};
EXPORT_SYMBOL(qcom_qnoc4_ops);

MODULE_LICENSE("GPL v2");
