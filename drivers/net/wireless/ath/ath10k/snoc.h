/*
 * Copyright (c) 2018 The Linux Foundation. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _SNOC_H_
#define _SNOC_H_

#include "hw.h"
#include "ce.h"
#include "pci.h"

struct ath10k_snoc_drv_priv {
	enum ath10k_hw_rev hw_rev;
	u64 dma_mask;
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

struct ath10k_wcn3990_vreg_info {
	struct regulator *reg;
	const char *name;
	u32 min_v;
	u32 max_v;
	u32 load_ua;
	unsigned long settle_delay;
	bool required;
};

struct ath10k_wcn3990_clk_info {
	struct clk *handle;
	const char *name;
	u32 freq;
	bool required;
};

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
	struct ath10k_wcn3990_vreg_info *vreg;
	struct ath10k_wcn3990_clk_info *clk;
};

static inline struct ath10k_snoc *ath10k_snoc_priv(struct ath10k *ar)
{
	return (struct ath10k_snoc *)ar->drv_priv;
}

void ath10k_snoc_write32(struct ath10k *ar, u32 offset, u32 value);
u32 ath10k_snoc_read32(struct ath10k *ar, u32 offset);

#endif /* _SNOC_H_ */
