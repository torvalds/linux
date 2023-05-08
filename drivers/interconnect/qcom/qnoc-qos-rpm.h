/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __DRIVERS_INTERCONNECT_QCOM_QNOC_QOS_RPM_H__
#define __DRIVERS_INTERCONNECT_QCOM_QNOC_QOS_RPM_H__

#define QOSGEN_OFF_MAX_REGS 6
#define ICC_QNOC_QOS_MAX_TYPE 1

enum {
	ICC_QNOC_QOSGEN_TYPE_RPMH,
};

enum {
	QOSGEN_OFF_MAINCTL_LO,
	QOSGEN_OFF_LIMITBW_LO,
	QOSGEN_OFF_SHAPING_LO,
	QOSGEN_OFF_SHAPING_HI,
	QOSGEN_OFF_REGUL0CTL_LO,
	QOSGEN_OFF_REGUL0BW_LO,
};

enum {
	QOSGEN_OFF_MPORT_BKE_HEALTH,
	QOSGEN_OFF_MPORT_BKE_EN,
};

extern const u8 icc_qnoc_qos_regs[ICC_QNOC_QOS_MAX_TYPE][QOSGEN_OFF_MAX_REGS];
extern const u8 icc_bimc_qos_regs[ICC_QNOC_QOS_MAX_TYPE][QOSGEN_OFF_MAX_REGS];

struct qcom_icc_noc_ops {
	void		(*set_qos)(struct qcom_icc_node *node);
};

struct qos_config {
	u32 prio;
	u32 urg_fwd;
	bool prio_fwd_disable;
	u32 bke_enable;
};

struct qcom_icc_qosbox {
	u32 num_ports;
	const u8 *regs;
	bool initialized;
	struct qos_config *config;
	u32 offsets[];
};

extern const struct qcom_icc_noc_ops qcom_qnoc4_ops;
extern const struct qcom_icc_noc_ops qcom_bimc_ops;
#endif
