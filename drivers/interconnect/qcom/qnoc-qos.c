// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2023, Qualcomm Innovation Center, Inc. All rights reserved.
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
#define QOS_DISABLE_SHIFT		24

#ifndef CONFIG_INTERCONNECT_QCOM_RPMH
/* QNOC2 Macros*/
#define QOSGEN_QNOC2_PRIORITY(p, qp)    ((p)->offsets[qp] + \
					(p)->regs[QOSGEN_OFF_QNOC2_PRIORITY])
#define QOS_QNOC2_PRIO_MASK             0x3
#define QOS_QNOC2_PRIO_P1_SHFT          0x2
#define QOS_QNOC2_PRIO_P0_SHFT          0x0

#define QOSGEN_QNOC2_MODE(p, qp)        ((p)->offsets[qp] + \
					(p)->regs[QOSGEN_OFF_QNOC2_MODE])
#define QOS_QNOC2_MODE_MASK             0x3
#define QOS_QNOC2_MODE_SHFT             0x0

/* BIMC Macros*/
#define QOSGEN_M_BKE_HEALTH(p, qp, n)   ((p)->offsets[qp] + ((n) * 4) + \
					(p)->regs[QOSGEN_OFF_MPORT_BKE_HEALTH])
#define QOS_PRIOLVL_MASK                0x7
#define QOS_PRIOLVL_SHFT                0x0
#define QOS_AREQPRIO_MASK               0x7
#define QOS_AREQPRIO_SHFT               0x8

#define QOSGEN_M_BKE_EN(p, qp)  ((p)->offsets[qp] + \
				(p)->regs[QOSGEN_OFF_MPORT_BKE_EN])

#define QOS_BKE_EN_MASK         0x1
#define QOS_BKE_EN_SHFT         0x0

#define NUM_BKE_HEALTH_LEVELS           4
#endif

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
EXPORT_SYMBOL_GPL(icc_qnoc_qos_regs);

#ifndef CONFIG_INTERCONNECT_QCOM_RPMH
const u8 icc_qnoc2_qos_regs[][QOSGEN_OFF_MAX_REGS] = {
	[ICC_QNOC_QOSGEN_TYPE_RPMH] = {
		[QOSGEN_OFF_QNOC2_PRIORITY] = 0x8,
		[QOSGEN_OFF_QNOC2_MODE] = 0xC,
	},
};
EXPORT_SYMBOL_GPL(icc_qnoc2_qos_regs);

const u8 icc_bimc_qos_regs[][QOSGEN_OFF_MAX_REGS] = {
	[ICC_QNOC_QOSGEN_TYPE_RPMH] = {
	[QOSGEN_OFF_MPORT_BKE_EN] = 0x0,
	[QOSGEN_OFF_MPORT_BKE_HEALTH] = 0x40,
	},
};
EXPORT_SYMBOL_GPL(icc_bimc_qos_regs);
#endif

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
				   BIT(QOS_DISABLE_SHIFT),
				   qos->config->prio_fwd_disable << QOS_DISABLE_SHIFT);

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
EXPORT_SYMBOL_GPL(qcom_qnoc4_ops);

#ifndef CONFIG_INTERCONNECT_QCOM_RPMH
/**
 * qcom_icc_set_qnoc2_qos - initialize static QoS configurations
 * @node: qcom icc node to operate on
 */
static void qcom_icc_set_qnoc2_qos(struct qcom_icc_node *node)
{
	struct qcom_icc_qosbox *qos = node->qosbox;
	int port;

	if (!node->regmap)
		return;

	if (!qos)
		return;

	for (port = 0; port < qos->num_ports; port++) {
		regmap_update_bits(node->regmap, QOSGEN_QNOC2_PRIORITY(qos, port),
		QOS_QNOC2_PRIO_MASK << QOS_QNOC2_PRIO_P1_SHFT,
		qos->config->prio1 << QOS_QNOC2_PRIO_P1_SHFT);

		regmap_update_bits(node->regmap, QOSGEN_QNOC2_PRIORITY(qos, port),
				QOS_QNOC2_PRIO_MASK << QOS_QNOC2_PRIO_P0_SHFT,
				qos->config->prio0 << QOS_QNOC2_PRIO_P0_SHFT);

		regmap_update_bits(node->regmap, QOSGEN_QNOC2_MODE(qos, port),
		QOS_QNOC2_MODE_MASK << QOS_QNOC2_MODE_SHFT,
		qos->config->mode << QOS_QNOC2_MODE_SHFT);
	}
}

const struct qcom_icc_noc_ops qcom_qnoc2_ops = {
	.set_qos = qcom_icc_set_qnoc2_qos,
};
EXPORT_SYMBOL_GPL(qcom_qnoc2_ops);

/**
 * qcom_icc_set_bimc_qos - initialize static QoS configurations
 * @node: qcom icc node to operate on
 */
static void qcom_icc_set_bimc_qos(struct qcom_icc_node *node)
{
	struct qcom_icc_qosbox *qos = node->qosbox;
	int port, i;

	if (!node->regmap)
		return;

	if (!qos)
		return;

	for (port = 0; port < qos->num_ports; port++) {
		for (i = 0; i < NUM_BKE_HEALTH_LEVELS; i++) {
			regmap_update_bits(node->regmap,
			QOSGEN_M_BKE_HEALTH(qos, port, i),
			((QOS_PRIOLVL_MASK << QOS_PRIOLVL_SHFT) |
			 (QOS_AREQPRIO_MASK << QOS_AREQPRIO_SHFT)),
			((qos->config->prio << QOS_PRIOLVL_SHFT) |
			 (qos->config->prio << QOS_AREQPRIO_SHFT)));
		}

		regmap_update_bits(node->regmap,
		QOSGEN_M_BKE_EN(qos, port),
		QOS_BKE_EN_MASK << QOS_BKE_EN_SHFT,
		qos->config->bke_enable << QOS_BKE_EN_SHFT);
	}
}

const struct qcom_icc_noc_ops qcom_bimc_ops = {
	.set_qos = qcom_icc_set_bimc_qos,
};
EXPORT_SYMBOL_GPL(qcom_bimc_ops);
#endif

MODULE_LICENSE("GPL");
