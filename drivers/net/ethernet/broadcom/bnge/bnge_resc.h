/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2025 Broadcom */

#ifndef _BNGE_RESC_H_
#define _BNGE_RESC_H_

#include "bnge_netdev.h"
#include "bnge_rmem.h"

struct bnge_hw_resc {
	u16	min_rsscos_ctxs;
	u16	max_rsscos_ctxs;
	u16	resv_rsscos_ctxs;
	u16	min_cp_rings;
	u16	max_cp_rings;
	u16	resv_cp_rings;
	u16	min_tx_rings;
	u16	max_tx_rings;
	u16	resv_tx_rings;
	u16	max_tx_sch_inputs;
	u16	min_rx_rings;
	u16	max_rx_rings;
	u16	resv_rx_rings;
	u16	min_hw_ring_grps;
	u16	max_hw_ring_grps;
	u16	resv_hw_ring_grps;
	u16	min_l2_ctxs;
	u16	max_l2_ctxs;
	u16	min_vnics;
	u16	max_vnics;
	u16	resv_vnics;
	u16	min_stat_ctxs;
	u16	max_stat_ctxs;
	u16	resv_stat_ctxs;
	u16	max_nqs;
	u16	max_irqs;
	u16	resv_irqs;
	u32	max_encap_records;
	u32	max_decap_records;
	u32	max_tx_em_flows;
	u32	max_tx_wm_flows;
	u32	max_rx_em_flows;
	u32	max_rx_wm_flows;
};

struct bnge_hw_rings {
	u16 tx;
	u16 rx;
	u16 grp;
	u16 nq;
	u16 cmpl;
	u16 stat;
	u16 vnic;
	u16 rss_ctx;
};

/* "TXRX", 2 hypens, plus maximum integer */
#define BNGE_IRQ_NAME_EXTRA	17
struct bnge_irq {
	irq_handler_t	handler;
	unsigned int	vector;
	u8		requested:1;
	u8		have_cpumask:1;
	char		name[IFNAMSIZ + BNGE_IRQ_NAME_EXTRA];
	cpumask_var_t	cpu_mask;
};

int bnge_reserve_rings(struct bnge_dev *bd);
int bnge_fix_rings_count(u16 *rx, u16 *tx, u16 max, bool shared);
int bnge_alloc_irqs(struct bnge_dev *bd);
void bnge_free_irqs(struct bnge_dev *bd);
int bnge_net_init_dflt_config(struct bnge_dev *bd);
void bnge_net_uninit_dflt_config(struct bnge_dev *bd);
void bnge_aux_init_dflt_config(struct bnge_dev *bd);
u32 bnge_get_rxfh_indir_size(struct bnge_dev *bd);
int bnge_cal_nr_rss_ctxs(u16 rx_rings);

static inline u32
bnge_adjust_pow_two(u32 total_ent, u16 ent_per_blk)
{
	u32 blks = total_ent / ent_per_blk;

	if (blks == 0 || blks == 1)
		return ++blks;

	if (!is_power_of_2(blks))
		blks = roundup_pow_of_two(blks);

	return blks;
}

#define BNGE_MAX_ROCE_MSIX		64
#define BNGE_MIN_ROCE_CP_RINGS		2
#define BNGE_MIN_ROCE_STAT_CTXS		1

#endif /* _BNGE_RESC_H_ */
