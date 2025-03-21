/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef ATH12K_AHB_H
#define ATH12K_AHB_H

#include <linux/clk.h>
#include <linux/remoteproc/qcom_rproc.h>
#include "core.h"

#define ATH12K_AHB_RECOVERY_TIMEOUT (3 * HZ)

#define ATH12K_AHB_SMP2P_SMEM_MSG		GENMASK(15, 0)
#define ATH12K_AHB_SMP2P_SMEM_SEQ_NO		GENMASK(31, 16)
#define ATH12K_AHB_SMP2P_SMEM_VALUE_MASK	0xFFFFFFFF
#define ATH12K_PCI_CE_WAKE_IRQ			2
#define ATH12K_PCI_IRQ_CE0_OFFSET		3
#define ATH12K_ROOTPD_READY_TIMEOUT		(5 * HZ)
#define ATH12K_RPROC_AFTER_POWERUP		QCOM_SSR_AFTER_POWERUP
#define ATH12K_AHB_FW_PREFIX			"q6_fw"
#define ATH12K_AHB_FW_SUFFIX			".mdt"
#define ATH12K_AHB_FW2				"iu_fw.mdt"
#define ATH12K_AHB_UPD_SWID			0x12
#define ATH12K_USERPD_SPAWN_TIMEOUT		(5 * HZ)
#define ATH12K_USERPD_READY_TIMEOUT		(10 * HZ)
#define ATH12K_USERPD_STOP_TIMEOUT		(5 * HZ)
#define ATH12K_USERPD_ID_MASK			GENMASK(9, 8)
#define ATH12K_USERPD_FW_NAME_LEN		35

enum ath12k_ahb_smp2p_msg_id {
	ATH12K_AHB_POWER_SAVE_ENTER = 1,
	ATH12K_AHB_POWER_SAVE_EXIT,
};

enum ath12k_ahb_userpd_irq {
	ATH12K_USERPD_SPAWN_IRQ,
	ATH12K_USERPD_READY_IRQ,
	ATH12K_USERPD_STOP_ACK_IRQ,
	ATH12K_USERPD_MAX_IRQ,
};

struct ath12k_base;

struct ath12k_ahb {
	struct ath12k_base *ab;
	struct rproc *tgt_rproc;
	struct clk *xo_clk;
	struct completion rootpd_ready;
	struct notifier_block root_pd_nb;
	void *root_pd_notifier;
	struct qcom_smem_state *spawn_state;
	struct qcom_smem_state *stop_state;
	struct completion userpd_spawned;
	struct completion userpd_ready;
	struct completion userpd_stopped;
	u32 userpd_id;
	u32 spawn_bit;
	u32 stop_bit;
	int userpd_irq_num[ATH12K_USERPD_MAX_IRQ];
};

static inline struct ath12k_ahb *ath12k_ab_to_ahb(struct ath12k_base *ab)
{
	return (struct ath12k_ahb *)ab->drv_priv;
}

#endif
