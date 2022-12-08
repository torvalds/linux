/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef ATH11K_AHB_H
#define ATH11K_AHB_H

#include "core.h"

#define ATH11K_AHB_RECOVERY_TIMEOUT (3 * HZ)

#define ATH11K_AHB_SMP2P_SMEM_MSG		GENMASK(15, 0)
#define ATH11K_AHB_SMP2P_SMEM_SEQ_NO		GENMASK(31, 16)
#define ATH11K_AHB_SMP2P_SMEM_VALUE_MASK	0xFFFFFFFF

enum ath11k_ahb_smp2p_msg_id {
	ATH11K_AHB_POWER_SAVE_ENTER = 1,
	ATH11K_AHB_POWER_SAVE_EXIT,
};

struct ath11k_base;

struct ath11k_ahb {
	struct rproc *tgt_rproc;
	struct {
		struct device *dev;
		struct iommu_domain *iommu_domain;
		dma_addr_t msa_paddr;
		u32 msa_size;
		dma_addr_t ce_paddr;
		u32 ce_size;
		bool use_tz;
	} fw;
	struct {
		unsigned short seq_no;
		unsigned int smem_bit;
		struct qcom_smem_state *smem_state;
	} smp2p_info;
};

static inline struct ath11k_ahb *ath11k_ahb_priv(struct ath11k_base *ab)
{
	return (struct ath11k_ahb *)ab->drv_priv;
}
#endif
