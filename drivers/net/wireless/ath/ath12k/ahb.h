/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef ATH12K_AHB_H
#define ATH12K_AHB_H

#include <linux/clk.h>
#include "core.h"

#define ATH12K_AHB_RECOVERY_TIMEOUT (3 * HZ)

#define ATH12K_AHB_SMP2P_SMEM_MSG		GENMASK(15, 0)
#define ATH12K_AHB_SMP2P_SMEM_SEQ_NO		GENMASK(31, 16)
#define ATH12K_AHB_SMP2P_SMEM_VALUE_MASK	0xFFFFFFFF
#define ATH12K_PCI_CE_WAKE_IRQ			2
#define ATH12K_PCI_IRQ_CE0_OFFSET		3

enum ath12k_ahb_smp2p_msg_id {
	ATH12K_AHB_POWER_SAVE_ENTER = 1,
	ATH12K_AHB_POWER_SAVE_EXIT,
};

struct ath12k_base;

struct ath12k_ahb {
	struct rproc *tgt_rproc;
	struct clk *xo_clk;
};

static inline struct ath12k_ahb *ath12k_ab_to_ahb(struct ath12k_base *ab)
{
	return (struct ath12k_ahb *)ab->drv_priv;
}

#endif
