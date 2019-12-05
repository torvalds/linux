/* SPDX-License-Identifier: ISC */
/*
 * Copyright (c) 2018 The Linux Foundation. All rights reserved.
 */

#ifndef _SNOC_H_
#define _SNOC_H_

#include "hw.h"
#include "ce.h"
#include "qmi.h"

struct ath10k_snoc_drv_priv {
	enum ath10k_hw_rev hw_rev;
	u64 dma_mask;
	u32 msa_size;
};

struct snoc_state {
	u32 pipe_cfg_addr;
	u32 svc_to_pipe_map;
};

struct ath10k_snoc_pipe {
	struct ath10k_ce_pipe *ce_hdl;
	u8 pipe_num;
	struct ath10k *hif_ce_state;
	size_t buf_sz;
	/* protect ce info */
	spinlock_t pipe_lock;
	struct ath10k_snoc *ar_snoc;
};

struct ath10k_snoc_target_info {
	u32 target_version;
	u32 target_type;
	u32 target_revision;
	u32 soc_version;
};

struct ath10k_snoc_ce_irq {
	u32 irq_line;
};

enum ath10k_snoc_flags {
	ATH10K_SNOC_FLAG_REGISTERED,
	ATH10K_SNOC_FLAG_UNREGISTERING,
	ATH10K_SNOC_FLAG_RECOVERY,
	ATH10K_SNOC_FLAG_8BIT_HOST_CAP_QUIRK,
};

struct clk_bulk_data;
struct regulator_bulk_data;

struct ath10k_snoc {
	struct platform_device *dev;
	struct ath10k *ar;
	void __iomem *mem;
	dma_addr_t mem_pa;
	struct ath10k_snoc_target_info target_info;
	size_t mem_len;
	struct ath10k_snoc_pipe pipe_info[CE_COUNT_MAX];
	struct ath10k_snoc_ce_irq ce_irqs[CE_COUNT_MAX];
	struct ath10k_ce ce;
	struct timer_list rx_post_retry;
	struct regulator_bulk_data *vregs;
	size_t num_vregs;
	struct clk_bulk_data *clks;
	size_t num_clks;
	struct ath10k_qmi *qmi;
	unsigned long flags;
	bool xo_cal_supported;
	u32 xo_cal_data;
};

static inline struct ath10k_snoc *ath10k_snoc_priv(struct ath10k *ar)
{
	return (struct ath10k_snoc *)ar->drv_priv;
}

int ath10k_snoc_fw_indication(struct ath10k *ar, u64 type);
void ath10k_snoc_fw_crashed_dump(struct ath10k *ar);

#endif /* _SNOC_H_ */
