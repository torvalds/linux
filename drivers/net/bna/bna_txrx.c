/*
 * Linux network driver for Brocade Converged Network Adapter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (GPL) Version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
  */
/*
 * Copyright (c) 2005-2010 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 */
#include "bna.h"
#include "bfa_sm.h"
#include "bfi.h"

/**
 * IB
 */
#define bna_ib_find_free_ibidx(_mask, _pos)\
do {\
	(_pos) = 0;\
	while (((_pos) < (BFI_IBIDX_MAX_SEGSIZE)) &&\
		((1 << (_pos)) & (_mask)))\
		(_pos)++;\
} while (0)

#define bna_ib_count_ibidx(_mask, _count)\
do {\
	int pos = 0;\
	(_count) = 0;\
	while (pos < (BFI_IBIDX_MAX_SEGSIZE)) {\
		if ((1 << pos) & (_mask))\
			(_count) = pos + 1;\
		pos++;\
	} \
} while (0)

#define bna_ib_select_segpool(_count, _q_idx)\
do {\
	int i;\
	(_q_idx) = -1;\
	for (i = 0; i < BFI_IBIDX_TOTAL_POOLS; i++) {\
		if ((_count <= ibidx_pool[i].pool_entry_size)) {\
			(_q_idx) = i;\
			break;\
		} \
	} \
} while (0)

struct bna_ibidx_pool {
	int	pool_size;
	int	pool_entry_size;
};
init_ibidx_pool(ibidx_pool);

static struct bna_intr *
bna_intr_get(struct bna_ib_mod *ib_mod, enum bna_intr_type intr_type,
		int vector)
{
	struct bna_intr *intr;
	struct list_head *qe;

	list_for_each(qe, &ib_mod->intr_active_q) {
		intr = (struct bna_intr *)qe;

		if ((intr->intr_type == intr_type) &&
			(intr->vector == vector)) {
			intr->ref_count++;
			return intr;
		}
	}

	if (list_empty(&ib_mod->intr_free_q))
		return NULL;

	bfa_q_deq(&ib_mod->intr_free_q, &intr);
	bfa_q_qe_init(&intr->qe);

	intr->ref_count = 1;
	intr->intr_type = intr_type;
	intr->vector = vector;

	list_add_tail(&intr->qe, &ib_mod->intr_active_q);

	return intr;
}

static void
bna_intr_put(struct bna_ib_mod *ib_mod,
		struct bna_intr *intr)
{
	intr->ref_count--;

	if (intr->ref_count == 0) {
		intr->ib = NULL;
		list_del(&intr->qe);
		bfa_q_qe_init(&intr->qe);
		list_add_tail(&intr->qe, &ib_mod->intr_free_q);
	}
}

void
bna_ib_mod_init(struct bna_ib_mod *ib_mod, struct bna *bna,
		struct bna_res_info *res_info)
{
	int i;
	int j;
	int count;
	u8 offset;
	struct bna_doorbell_qset *qset;
	unsigned long off;

	ib_mod->bna = bna;

	ib_mod->ib = (struct bna_ib *)
		res_info[BNA_RES_MEM_T_IB_ARRAY].res_u.mem_info.mdl[0].kva;
	ib_mod->intr = (struct bna_intr *)
		res_info[BNA_RES_MEM_T_INTR_ARRAY].res_u.mem_info.mdl[0].kva;
	ib_mod->idx_seg = (struct bna_ibidx_seg *)
		res_info[BNA_RES_MEM_T_IDXSEG_ARRAY].res_u.mem_info.mdl[0].kva;

	INIT_LIST_HEAD(&ib_mod->ib_free_q);
	INIT_LIST_HEAD(&ib_mod->intr_free_q);
	INIT_LIST_HEAD(&ib_mod->intr_active_q);

	for (i = 0; i < BFI_IBIDX_TOTAL_POOLS; i++)
		INIT_LIST_HEAD(&ib_mod->ibidx_seg_pool[i]);

	for (i = 0; i < BFI_MAX_IB; i++) {
		ib_mod->ib[i].ib_id = i;

		ib_mod->ib[i].ib_seg_host_addr_kva =
		res_info[BNA_RES_MEM_T_IBIDX].res_u.mem_info.mdl[i].kva;
		ib_mod->ib[i].ib_seg_host_addr.lsb =
		res_info[BNA_RES_MEM_T_IBIDX].res_u.mem_info.mdl[i].dma.lsb;
		ib_mod->ib[i].ib_seg_host_addr.msb =
		res_info[BNA_RES_MEM_T_IBIDX].res_u.mem_info.mdl[i].dma.msb;

		qset = (struct bna_doorbell_qset *)0;
		off = (unsigned long)(&qset[i >> 1].ib0[(i & 0x1)
					* (0x20 >> 2)]);
		ib_mod->ib[i].door_bell.doorbell_addr = off +
			BNA_GET_DOORBELL_BASE_ADDR(bna->pcidev.pci_bar_kva);

		bfa_q_qe_init(&ib_mod->ib[i].qe);
		list_add_tail(&ib_mod->ib[i].qe, &ib_mod->ib_free_q);

		bfa_q_qe_init(&ib_mod->intr[i].qe);
		list_add_tail(&ib_mod->intr[i].qe, &ib_mod->intr_free_q);
	}

	count = 0;
	offset = 0;
	for (i = 0; i < BFI_IBIDX_TOTAL_POOLS; i++) {
		for (j = 0; j < ibidx_pool[i].pool_size; j++) {
			bfa_q_qe_init(&ib_mod->idx_seg[count]);
			ib_mod->idx_seg[count].ib_seg_size =
					ibidx_pool[i].pool_entry_size;
			ib_mod->idx_seg[count].ib_idx_tbl_offset = offset;
			list_add_tail(&ib_mod->idx_seg[count].qe,
				&ib_mod->ibidx_seg_pool[i]);
			count++;
			offset += ibidx_pool[i].pool_entry_size;
		}
	}
}

void
bna_ib_mod_uninit(struct bna_ib_mod *ib_mod)
{
	int i;
	int j;
	struct list_head *qe;

	i = 0;
	list_for_each(qe, &ib_mod->ib_free_q)
		i++;

	i = 0;
	list_for_each(qe, &ib_mod->intr_free_q)
		i++;

	for (i = 0; i < BFI_IBIDX_TOTAL_POOLS; i++) {
		j = 0;
		list_for_each(qe, &ib_mod->ibidx_seg_pool[i])
			j++;
	}

	ib_mod->bna = NULL;
}

static struct bna_ib *
bna_ib_get(struct bna_ib_mod *ib_mod,
		enum bna_intr_type intr_type,
		int vector)
{
	struct bna_ib *ib;
	struct bna_intr *intr;

	if (intr_type == BNA_INTR_T_INTX)
		vector = (1 << vector);

	intr = bna_intr_get(ib_mod, intr_type, vector);
	if (intr == NULL)
		return NULL;

	if (intr->ib) {
		if (intr->ib->ref_count == BFI_IBIDX_MAX_SEGSIZE) {
			bna_intr_put(ib_mod, intr);
			return NULL;
		}
		intr->ib->ref_count++;
		return intr->ib;
	}

	if (list_empty(&ib_mod->ib_free_q)) {
		bna_intr_put(ib_mod, intr);
		return NULL;
	}

	bfa_q_deq(&ib_mod->ib_free_q, &ib);
	bfa_q_qe_init(&ib->qe);

	ib->ref_count = 1;
	ib->start_count = 0;
	ib->idx_mask = 0;

	ib->intr = intr;
	ib->idx_seg = NULL;
	intr->ib = ib;

	ib->bna = ib_mod->bna;

	return ib;
}

static void
bna_ib_put(struct bna_ib_mod *ib_mod, struct bna_ib *ib)
{
	bna_intr_put(ib_mod, ib->intr);

	ib->ref_count--;

	if (ib->ref_count == 0) {
		ib->intr = NULL;
		ib->bna = NULL;
		list_add_tail(&ib->qe, &ib_mod->ib_free_q);
	}
}

/* Returns index offset - starting from 0 */
static int
bna_ib_reserve_idx(struct bna_ib *ib)
{
	struct bna_ib_mod *ib_mod = &ib->bna->ib_mod;
	struct bna_ibidx_seg *idx_seg;
	int idx;
	int num_idx;
	int q_idx;

	/* Find the first free index position */
	bna_ib_find_free_ibidx(ib->idx_mask, idx);
	if (idx == BFI_IBIDX_MAX_SEGSIZE)
		return -1;

	/*
	 * Calculate the total number of indexes held by this IB,
	 * including the index newly reserved above.
	 */
	bna_ib_count_ibidx((ib->idx_mask | (1 << idx)), num_idx);

	/* See if there is a free space in the index segment held by this IB */
	if (ib->idx_seg && (num_idx <= ib->idx_seg->ib_seg_size)) {
		ib->idx_mask |= (1 << idx);
		return idx;
	}

	if (ib->start_count)
		return -1;

	/* Allocate a new segment */
	bna_ib_select_segpool(num_idx, q_idx);
	while (1) {
		if (q_idx == BFI_IBIDX_TOTAL_POOLS)
			return -1;
		if (!list_empty(&ib_mod->ibidx_seg_pool[q_idx]))
			break;
		q_idx++;
	}
	bfa_q_deq(&ib_mod->ibidx_seg_pool[q_idx], &idx_seg);
	bfa_q_qe_init(&idx_seg->qe);

	/* Free the old segment */
	if (ib->idx_seg) {
		bna_ib_select_segpool(ib->idx_seg->ib_seg_size, q_idx);
		list_add_tail(&ib->idx_seg->qe, &ib_mod->ibidx_seg_pool[q_idx]);
	}

	ib->idx_seg = idx_seg;

	ib->idx_mask |= (1 << idx);

	return idx;
}

static void
bna_ib_release_idx(struct bna_ib *ib, int idx)
{
	struct bna_ib_mod *ib_mod = &ib->bna->ib_mod;
	struct bna_ibidx_seg *idx_seg;
	int num_idx;
	int cur_q_idx;
	int new_q_idx;

	ib->idx_mask &= ~(1 << idx);

	if (ib->start_count)
		return;

	bna_ib_count_ibidx(ib->idx_mask, num_idx);

	/*
	 * Free the segment, if there are no more indexes in the segment
	 * held by this IB
	 */
	if (!num_idx) {
		bna_ib_select_segpool(ib->idx_seg->ib_seg_size, cur_q_idx);
		list_add_tail(&ib->idx_seg->qe,
			&ib_mod->ibidx_seg_pool[cur_q_idx]);
		ib->idx_seg = NULL;
		return;
	}

	/* See if we can move to a smaller segment */
	bna_ib_select_segpool(num_idx, new_q_idx);
	bna_ib_select_segpool(ib->idx_seg->ib_seg_size, cur_q_idx);
	while (new_q_idx < cur_q_idx) {
		if (!list_empty(&ib_mod->ibidx_seg_pool[new_q_idx]))
			break;
		new_q_idx++;
	}
	if (new_q_idx < cur_q_idx) {
		/* Select the new smaller segment */
		bfa_q_deq(&ib_mod->ibidx_seg_pool[new_q_idx], &idx_seg);
		bfa_q_qe_init(&idx_seg->qe);
		/* Free the old segment */
		list_add_tail(&ib->idx_seg->qe,
			&ib_mod->ibidx_seg_pool[cur_q_idx]);
		ib->idx_seg = idx_seg;
	}
}

static int
bna_ib_config(struct bna_ib *ib, struct bna_ib_config *ib_config)
{
	if (ib->start_count)
		return -1;

	ib->ib_config.coalescing_timeo = ib_config->coalescing_timeo;
	ib->ib_config.interpkt_timeo = ib_config->interpkt_timeo;
	ib->ib_config.interpkt_count = ib_config->interpkt_count;
	ib->ib_config.ctrl_flags = ib_config->ctrl_flags;

	ib->ib_config.ctrl_flags |= BFI_IB_CF_MASTER_ENABLE;
	if (ib->intr->intr_type == BNA_INTR_T_MSIX)
		ib->ib_config.ctrl_flags |= BFI_IB_CF_MSIX_MODE;

	return 0;
}

static void
bna_ib_start(struct bna_ib *ib)
{
	struct bna_ib_blk_mem ib_cfg;
	struct bna_ib_blk_mem *ib_mem;
	u32 pg_num;
	u32 intx_mask;
	int i;
	void __iomem *base_addr;
	unsigned long off;

	ib->start_count++;

	if (ib->start_count > 1)
		return;

	ib_cfg.host_addr_lo = (u32)(ib->ib_seg_host_addr.lsb);
	ib_cfg.host_addr_hi = (u32)(ib->ib_seg_host_addr.msb);

	ib_cfg.clsc_n_ctrl_n_msix = (((u32)
				     ib->ib_config.coalescing_timeo << 16) |
				((u32)ib->ib_config.ctrl_flags << 8) |
				(ib->intr->vector));
	ib_cfg.ipkt_n_ent_n_idxof =
				((u32)
				 (ib->ib_config.interpkt_timeo & 0xf) << 16) |
				((u32)ib->idx_seg->ib_seg_size << 8) |
				(ib->idx_seg->ib_idx_tbl_offset);
	ib_cfg.ipkt_cnt_cfg_n_unacked = ((u32)
					 ib->ib_config.interpkt_count << 24);

	pg_num = BNA_GET_PAGE_NUM(HQM0_BLK_PG_NUM + ib->bna->port_num,
				HQM_IB_RAM_BASE_OFFSET);
	writel(pg_num, ib->bna->regs.page_addr);

	base_addr = BNA_GET_MEM_BASE_ADDR(ib->bna->pcidev.pci_bar_kva,
					HQM_IB_RAM_BASE_OFFSET);

	ib_mem = (struct bna_ib_blk_mem *)0;
	off = (unsigned long)&ib_mem[ib->ib_id].host_addr_lo;
	writel(htonl(ib_cfg.host_addr_lo), base_addr + off);

	off = (unsigned long)&ib_mem[ib->ib_id].host_addr_hi;
	writel(htonl(ib_cfg.host_addr_hi), base_addr + off);

	off = (unsigned long)&ib_mem[ib->ib_id].clsc_n_ctrl_n_msix;
	writel(ib_cfg.clsc_n_ctrl_n_msix, base_addr + off);

	off = (unsigned long)&ib_mem[ib->ib_id].ipkt_n_ent_n_idxof;
	writel(ib_cfg.ipkt_n_ent_n_idxof, base_addr + off);

	off = (unsigned long)&ib_mem[ib->ib_id].ipkt_cnt_cfg_n_unacked;
	writel(ib_cfg.ipkt_cnt_cfg_n_unacked, base_addr + off);

	ib->door_bell.doorbell_ack = BNA_DOORBELL_IB_INT_ACK(
				(u32)ib->ib_config.coalescing_timeo, 0);

	pg_num = BNA_GET_PAGE_NUM(HQM0_BLK_PG_NUM + ib->bna->port_num,
				HQM_INDX_TBL_RAM_BASE_OFFSET);
	writel(pg_num, ib->bna->regs.page_addr);

	base_addr = BNA_GET_MEM_BASE_ADDR(ib->bna->pcidev.pci_bar_kva,
					HQM_INDX_TBL_RAM_BASE_OFFSET);
	for (i = 0; i < ib->idx_seg->ib_seg_size; i++) {
		off = (unsigned long)
		((ib->idx_seg->ib_idx_tbl_offset + i) * BFI_IBIDX_SIZE);
		writel(0, base_addr + off);
	}

	if (ib->intr->intr_type == BNA_INTR_T_INTX) {
		bna_intx_disable(ib->bna, intx_mask);
		intx_mask &= ~(ib->intr->vector);
		bna_intx_enable(ib->bna, intx_mask);
	}
}

static void
bna_ib_stop(struct bna_ib *ib)
{
	u32 intx_mask;

	ib->start_count--;

	if (ib->start_count == 0) {
		writel(BNA_DOORBELL_IB_INT_DISABLE,
				ib->door_bell.doorbell_addr);
		if (ib->intr->intr_type == BNA_INTR_T_INTX) {
			bna_intx_disable(ib->bna, intx_mask);
			intx_mask |= (ib->intr->vector);
			bna_intx_enable(ib->bna, intx_mask);
		}
	}
}

static void
bna_ib_fail(struct bna_ib *ib)
{
	ib->start_count = 0;
}

/**
 * RXF
 */
static void rxf_enable(struct bna_rxf *rxf);
static void rxf_disable(struct bna_rxf *rxf);
static void __rxf_config_set(struct bna_rxf *rxf);
static void __rxf_rit_set(struct bna_rxf *rxf);
static void __bna_rxf_stat_clr(struct bna_rxf *rxf);
static int rxf_process_packet_filter(struct bna_rxf *rxf);
static int rxf_clear_packet_filter(struct bna_rxf *rxf);
static void rxf_reset_packet_filter(struct bna_rxf *rxf);
static void rxf_cb_enabled(void *arg, int status);
static void rxf_cb_disabled(void *arg, int status);
static void bna_rxf_cb_stats_cleared(void *arg, int status);
static void __rxf_enable(struct bna_rxf *rxf);
static void __rxf_disable(struct bna_rxf *rxf);

bfa_fsm_state_decl(bna_rxf, stopped, struct bna_rxf,
			enum bna_rxf_event);
bfa_fsm_state_decl(bna_rxf, start_wait, struct bna_rxf,
			enum bna_rxf_event);
bfa_fsm_state_decl(bna_rxf, cam_fltr_mod_wait, struct bna_rxf,
			enum bna_rxf_event);
bfa_fsm_state_decl(bna_rxf, started, struct bna_rxf,
			enum bna_rxf_event);
bfa_fsm_state_decl(bna_rxf, cam_fltr_clr_wait, struct bna_rxf,
			enum bna_rxf_event);
bfa_fsm_state_decl(bna_rxf, stop_wait, struct bna_rxf,
			enum bna_rxf_event);
bfa_fsm_state_decl(bna_rxf, pause_wait, struct bna_rxf,
			enum bna_rxf_event);
bfa_fsm_state_decl(bna_rxf, resume_wait, struct bna_rxf,
			enum bna_rxf_event);
bfa_fsm_state_decl(bna_rxf, stat_clr_wait, struct bna_rxf,
			enum bna_rxf_event);

static struct bfa_sm_table rxf_sm_table[] = {
	{BFA_SM(bna_rxf_sm_stopped), BNA_RXF_STOPPED},
	{BFA_SM(bna_rxf_sm_start_wait), BNA_RXF_START_WAIT},
	{BFA_SM(bna_rxf_sm_cam_fltr_mod_wait), BNA_RXF_CAM_FLTR_MOD_WAIT},
	{BFA_SM(bna_rxf_sm_started), BNA_RXF_STARTED},
	{BFA_SM(bna_rxf_sm_cam_fltr_clr_wait), BNA_RXF_CAM_FLTR_CLR_WAIT},
	{BFA_SM(bna_rxf_sm_stop_wait), BNA_RXF_STOP_WAIT},
	{BFA_SM(bna_rxf_sm_pause_wait), BNA_RXF_PAUSE_WAIT},
	{BFA_SM(bna_rxf_sm_resume_wait), BNA_RXF_RESUME_WAIT},
	{BFA_SM(bna_rxf_sm_stat_clr_wait), BNA_RXF_STAT_CLR_WAIT}
};

static void
bna_rxf_sm_stopped_entry(struct bna_rxf *rxf)
{
	call_rxf_stop_cbfn(rxf, BNA_CB_SUCCESS);
}

static void
bna_rxf_sm_stopped(struct bna_rxf *rxf, enum bna_rxf_event event)
{
	switch (event) {
	case RXF_E_START:
		bfa_fsm_set_state(rxf, bna_rxf_sm_start_wait);
		break;

	case RXF_E_STOP:
		bfa_fsm_set_state(rxf, bna_rxf_sm_stopped);
		break;

	case RXF_E_FAIL:
		/* No-op */
		break;

	case RXF_E_CAM_FLTR_MOD:
		call_rxf_cam_fltr_cbfn(rxf, BNA_CB_SUCCESS);
		break;

	case RXF_E_STARTED:
	case RXF_E_STOPPED:
	case RXF_E_CAM_FLTR_RESP:
		/**
		 * These events are received due to flushing of mbox
		 * when device fails
		 */
		/* No-op */
		break;

	case RXF_E_PAUSE:
		rxf->rxf_oper_state = BNA_RXF_OPER_STATE_PAUSED;
		call_rxf_pause_cbfn(rxf, BNA_CB_SUCCESS);
		break;

	case RXF_E_RESUME:
		rxf->rxf_oper_state = BNA_RXF_OPER_STATE_RUNNING;
		call_rxf_resume_cbfn(rxf, BNA_CB_SUCCESS);
		break;

	default:
		bfa_sm_fault(rxf->rx->bna, event);
	}
}

static void
bna_rxf_sm_start_wait_entry(struct bna_rxf *rxf)
{
	__rxf_config_set(rxf);
	__rxf_rit_set(rxf);
	rxf_enable(rxf);
}

static void
bna_rxf_sm_start_wait(struct bna_rxf *rxf, enum bna_rxf_event event)
{
	switch (event) {
	case RXF_E_STOP:
		/**
		 * STOP is originated from bnad. When this happens,
		 * it can not be waiting for filter update
		 */
		call_rxf_start_cbfn(rxf, BNA_CB_INTERRUPT);
		bfa_fsm_set_state(rxf, bna_rxf_sm_stop_wait);
		break;

	case RXF_E_FAIL:
		call_rxf_cam_fltr_cbfn(rxf, BNA_CB_SUCCESS);
		call_rxf_start_cbfn(rxf, BNA_CB_FAIL);
		bfa_fsm_set_state(rxf, bna_rxf_sm_stopped);
		break;

	case RXF_E_CAM_FLTR_MOD:
		/* No-op */
		break;

	case RXF_E_STARTED:
		/**
		 * Force rxf_process_filter() to go through initial
		 * config
		 */
		if ((rxf->ucast_active_mac != NULL) &&
			(rxf->ucast_pending_set == 0))
			rxf->ucast_pending_set = 1;

		if (rxf->rss_status == BNA_STATUS_T_ENABLED)
			rxf->rxf_flags |= BNA_RXF_FL_RSS_CONFIG_PENDING;

		rxf->rxf_flags |= BNA_RXF_FL_VLAN_CONFIG_PENDING;

		bfa_fsm_set_state(rxf, bna_rxf_sm_cam_fltr_mod_wait);
		break;

	case RXF_E_PAUSE:
	case RXF_E_RESUME:
		rxf->rxf_flags |= BNA_RXF_FL_OPERSTATE_CHANGED;
		break;

	default:
		bfa_sm_fault(rxf->rx->bna, event);
	}
}

static void
bna_rxf_sm_cam_fltr_mod_wait_entry(struct bna_rxf *rxf)
{
	if (!rxf_process_packet_filter(rxf)) {
		/* No more pending CAM entries to update */
		bfa_fsm_set_state(rxf, bna_rxf_sm_started);
	}
}

static void
bna_rxf_sm_cam_fltr_mod_wait(struct bna_rxf *rxf, enum bna_rxf_event event)
{
	switch (event) {
	case RXF_E_STOP:
		/**
		 * STOP is originated from bnad. When this happens,
		 * it can not be waiting for filter update
		 */
		call_rxf_start_cbfn(rxf, BNA_CB_INTERRUPT);
		bfa_fsm_set_state(rxf, bna_rxf_sm_cam_fltr_clr_wait);
		break;

	case RXF_E_FAIL:
		rxf_reset_packet_filter(rxf);
		call_rxf_cam_fltr_cbfn(rxf, BNA_CB_SUCCESS);
		call_rxf_start_cbfn(rxf, BNA_CB_FAIL);
		bfa_fsm_set_state(rxf, bna_rxf_sm_stopped);
		break;

	case RXF_E_CAM_FLTR_MOD:
		/* No-op */
		break;

	case RXF_E_CAM_FLTR_RESP:
		if (!rxf_process_packet_filter(rxf)) {
			/* No more pending CAM entries to update */
			call_rxf_cam_fltr_cbfn(rxf, BNA_CB_SUCCESS);
			bfa_fsm_set_state(rxf, bna_rxf_sm_started);
		}
		break;

	case RXF_E_PAUSE:
	case RXF_E_RESUME:
		rxf->rxf_flags |= BNA_RXF_FL_OPERSTATE_CHANGED;
		break;

	default:
		bfa_sm_fault(rxf->rx->bna, event);
	}
}

static void
bna_rxf_sm_started_entry(struct bna_rxf *rxf)
{
	call_rxf_start_cbfn(rxf, BNA_CB_SUCCESS);

	if (rxf->rxf_flags & BNA_RXF_FL_OPERSTATE_CHANGED) {
		if (rxf->rxf_oper_state == BNA_RXF_OPER_STATE_PAUSED)
			bfa_fsm_send_event(rxf, RXF_E_PAUSE);
		else
			bfa_fsm_send_event(rxf, RXF_E_RESUME);
	}

}

static void
bna_rxf_sm_started(struct bna_rxf *rxf, enum bna_rxf_event event)
{
	switch (event) {
	case RXF_E_STOP:
		bfa_fsm_set_state(rxf, bna_rxf_sm_cam_fltr_clr_wait);
		/* Hack to get FSM start clearing CAM entries */
		bfa_fsm_send_event(rxf, RXF_E_CAM_FLTR_RESP);
		break;

	case RXF_E_FAIL:
		rxf_reset_packet_filter(rxf);
		bfa_fsm_set_state(rxf, bna_rxf_sm_stopped);
		break;

	case RXF_E_CAM_FLTR_MOD:
		bfa_fsm_set_state(rxf, bna_rxf_sm_cam_fltr_mod_wait);
		break;

	case RXF_E_PAUSE:
		bfa_fsm_set_state(rxf, bna_rxf_sm_pause_wait);
		break;

	case RXF_E_RESUME:
		bfa_fsm_set_state(rxf, bna_rxf_sm_resume_wait);
		break;

	default:
		bfa_sm_fault(rxf->rx->bna, event);
	}
}

static void
bna_rxf_sm_cam_fltr_clr_wait_entry(struct bna_rxf *rxf)
{
	/**
	 *  Note: Do not add rxf_clear_packet_filter here.
	 * It will overstep mbox when this transition happens:
	 * 	cam_fltr_mod_wait -> cam_fltr_clr_wait on RXF_E_STOP event
	 */
}

static void
bna_rxf_sm_cam_fltr_clr_wait(struct bna_rxf *rxf, enum bna_rxf_event event)
{
	switch (event) {
	case RXF_E_FAIL:
		/**
		 * FSM was in the process of stopping, initiated by
		 * bnad. When this happens, no one can be waiting for
		 * start or filter update
		 */
		rxf_reset_packet_filter(rxf);
		bfa_fsm_set_state(rxf, bna_rxf_sm_stopped);
		break;

	case RXF_E_CAM_FLTR_RESP:
		if (!rxf_clear_packet_filter(rxf)) {
			/* No more pending CAM entries to clear */
			bfa_fsm_set_state(rxf, bna_rxf_sm_stop_wait);
			rxf_disable(rxf);
		}
		break;

	default:
		bfa_sm_fault(rxf->rx->bna, event);
	}
}

static void
bna_rxf_sm_stop_wait_entry(struct bna_rxf *rxf)
{
	/**
	 * NOTE: Do not add  rxf_disable here.
	 * It will overstep mbox when this transition happens:
	 * 	start_wait -> stop_wait on RXF_E_STOP event
	 */
}

static void
bna_rxf_sm_stop_wait(struct bna_rxf *rxf, enum bna_rxf_event event)
{
	switch (event) {
	case RXF_E_FAIL:
		/**
		 * FSM was in the process of stopping, initiated by
		 * bnad. When this happens, no one can be waiting for
		 * start or filter update
		 */
		bfa_fsm_set_state(rxf, bna_rxf_sm_stopped);
		break;

	case RXF_E_STARTED:
		/**
		 * This event is received due to abrupt transition from
		 * bna_rxf_sm_start_wait state on receiving
		 * RXF_E_STOP event
		 */
		rxf_disable(rxf);
		break;

	case RXF_E_STOPPED:
		/**
		 * FSM was in the process of stopping, initiated by
		 * bnad. When this happens, no one can be waiting for
		 * start or filter update
		 */
		bfa_fsm_set_state(rxf, bna_rxf_sm_stat_clr_wait);
		break;

	case RXF_E_PAUSE:
		rxf->rxf_oper_state = BNA_RXF_OPER_STATE_PAUSED;
		break;

	case RXF_E_RESUME:
		rxf->rxf_oper_state = BNA_RXF_OPER_STATE_RUNNING;
		break;

	default:
		bfa_sm_fault(rxf->rx->bna, event);
	}
}

static void
bna_rxf_sm_pause_wait_entry(struct bna_rxf *rxf)
{
	rxf->rxf_flags &=
		~(BNA_RXF_FL_OPERSTATE_CHANGED | BNA_RXF_FL_RXF_ENABLED);
	__rxf_disable(rxf);
}

static void
bna_rxf_sm_pause_wait(struct bna_rxf *rxf, enum bna_rxf_event event)
{
	switch (event) {
	case RXF_E_FAIL:
		/**
		 * FSM was in the process of disabling rxf, initiated by
		 * bnad.
		 */
		call_rxf_pause_cbfn(rxf, BNA_CB_FAIL);
		bfa_fsm_set_state(rxf, bna_rxf_sm_stopped);
		break;

	case RXF_E_STOPPED:
		rxf->rxf_oper_state = BNA_RXF_OPER_STATE_PAUSED;
		call_rxf_pause_cbfn(rxf, BNA_CB_SUCCESS);
		bfa_fsm_set_state(rxf, bna_rxf_sm_started);
		break;

	/*
	 * Since PAUSE/RESUME can only be sent by bnad, we don't expect
	 * any other event during these states
	 */
	default:
		bfa_sm_fault(rxf->rx->bna, event);
	}
}

static void
bna_rxf_sm_resume_wait_entry(struct bna_rxf *rxf)
{
	rxf->rxf_flags &= ~(BNA_RXF_FL_OPERSTATE_CHANGED);
	rxf->rxf_flags |= BNA_RXF_FL_RXF_ENABLED;
	__rxf_enable(rxf);
}

static void
bna_rxf_sm_resume_wait(struct bna_rxf *rxf, enum bna_rxf_event event)
{
	switch (event) {
	case RXF_E_FAIL:
		/**
		 * FSM was in the process of disabling rxf, initiated by
		 * bnad.
		 */
		call_rxf_resume_cbfn(rxf, BNA_CB_FAIL);
		bfa_fsm_set_state(rxf, bna_rxf_sm_stopped);
		break;

	case RXF_E_STARTED:
		rxf->rxf_oper_state = BNA_RXF_OPER_STATE_RUNNING;
		call_rxf_resume_cbfn(rxf, BNA_CB_SUCCESS);
		bfa_fsm_set_state(rxf, bna_rxf_sm_started);
		break;

	/*
	 * Since PAUSE/RESUME can only be sent by bnad, we don't expect
	 * any other event during these states
	 */
	default:
		bfa_sm_fault(rxf->rx->bna, event);
	}
}

static void
bna_rxf_sm_stat_clr_wait_entry(struct bna_rxf *rxf)
{
	__bna_rxf_stat_clr(rxf);
}

static void
bna_rxf_sm_stat_clr_wait(struct bna_rxf *rxf, enum bna_rxf_event event)
{
	switch (event) {
	case RXF_E_FAIL:
	case RXF_E_STAT_CLEARED:
		bfa_fsm_set_state(rxf, bna_rxf_sm_stopped);
		break;

	default:
		bfa_sm_fault(rxf->rx->bna, event);
	}
}

static void
__rxf_enable(struct bna_rxf *rxf)
{
	struct bfi_ll_rxf_multi_req ll_req;
	u32 bm[2] = {0, 0};

	if (rxf->rxf_id < 32)
		bm[0] = 1 << rxf->rxf_id;
	else
		bm[1] = 1 << (rxf->rxf_id - 32);

	bfi_h2i_set(ll_req.mh, BFI_MC_LL, BFI_LL_H2I_RX_REQ, 0);
	ll_req.rxf_id_mask[0] = htonl(bm[0]);
	ll_req.rxf_id_mask[1] = htonl(bm[1]);
	ll_req.enable = 1;

	bna_mbox_qe_fill(&rxf->mbox_qe, &ll_req, sizeof(ll_req),
			rxf_cb_enabled, rxf);

	bna_mbox_send(rxf->rx->bna, &rxf->mbox_qe);
}

static void
__rxf_disable(struct bna_rxf *rxf)
{
	struct bfi_ll_rxf_multi_req ll_req;
	u32 bm[2] = {0, 0};

	if (rxf->rxf_id < 32)
		bm[0] = 1 << rxf->rxf_id;
	else
		bm[1] = 1 << (rxf->rxf_id - 32);

	bfi_h2i_set(ll_req.mh, BFI_MC_LL, BFI_LL_H2I_RX_REQ, 0);
	ll_req.rxf_id_mask[0] = htonl(bm[0]);
	ll_req.rxf_id_mask[1] = htonl(bm[1]);
	ll_req.enable = 0;

	bna_mbox_qe_fill(&rxf->mbox_qe, &ll_req, sizeof(ll_req),
			rxf_cb_disabled, rxf);

	bna_mbox_send(rxf->rx->bna, &rxf->mbox_qe);
}

static void
__rxf_config_set(struct bna_rxf *rxf)
{
	u32 i;
	struct bna_rss_mem *rss_mem;
	struct bna_rx_fndb_ram *rx_fndb_ram;
	struct bna *bna = rxf->rx->bna;
	void __iomem *base_addr;
	unsigned long off;

	base_addr = BNA_GET_MEM_BASE_ADDR(bna->pcidev.pci_bar_kva,
			RSS_TABLE_BASE_OFFSET);

	rss_mem = (struct bna_rss_mem *)0;

	/* Configure RSS if required */
	if (rxf->ctrl_flags & BNA_RXF_CF_RSS_ENABLE) {
		/* configure RSS Table */
		writel(BNA_GET_PAGE_NUM(RAD0_MEM_BLK_BASE_PG_NUM +
			bna->port_num, RSS_TABLE_BASE_OFFSET),
					bna->regs.page_addr);

		/* temporarily disable RSS, while hash value is written */
		off = (unsigned long)&rss_mem[0].type_n_hash;
		writel(0, base_addr + off);

		for (i = 0; i < BFI_RSS_HASH_KEY_LEN; i++) {
			off = (unsigned long)
			&rss_mem[0].hash_key[(BFI_RSS_HASH_KEY_LEN - 1) - i];
			writel(htonl(rxf->rss_cfg.toeplitz_hash_key[i]),
			base_addr + off);
		}

		off = (unsigned long)&rss_mem[0].type_n_hash;
		writel(rxf->rss_cfg.hash_type | rxf->rss_cfg.hash_mask,
			base_addr + off);
	}

	/* Configure RxF */
	writel(BNA_GET_PAGE_NUM(
		LUT0_MEM_BLK_BASE_PG_NUM + (bna->port_num * 2),
		RX_FNDB_RAM_BASE_OFFSET),
		bna->regs.page_addr);

	base_addr = BNA_GET_MEM_BASE_ADDR(bna->pcidev.pci_bar_kva,
		RX_FNDB_RAM_BASE_OFFSET);

	rx_fndb_ram = (struct bna_rx_fndb_ram *)0;

	/* We always use RSS table 0 */
	off = (unsigned long)&rx_fndb_ram[rxf->rxf_id].rss_prop;
	writel(rxf->ctrl_flags & BNA_RXF_CF_RSS_ENABLE,
		base_addr + off);

	/* small large buffer enable/disable */
	off = (unsigned long)&rx_fndb_ram[rxf->rxf_id].size_routing_props;
	writel((rxf->ctrl_flags & BNA_RXF_CF_SM_LG_RXQ) | 0x80,
		base_addr + off);

	/* RIT offset,  HDS forced offset, multicast RxQ Id */
	off = (unsigned long)&rx_fndb_ram[rxf->rxf_id].rit_hds_mcastq;
	writel((rxf->rit_segment->rit_offset << 16) |
		(rxf->forced_offset << 8) |
		(rxf->hds_cfg.hdr_type & BNA_HDS_FORCED) | rxf->mcast_rxq_id,
		base_addr + off);

	/*
	 * default vlan tag, default function enable, strip vlan bytes,
	 * HDS type, header size
	 */

	off = (unsigned long)&rx_fndb_ram[rxf->rxf_id].control_flags;
	 writel(((u32)rxf->default_vlan_tag << 16) |
		(rxf->ctrl_flags &
			(BNA_RXF_CF_DEFAULT_VLAN |
			BNA_RXF_CF_DEFAULT_FUNCTION_ENABLE |
			BNA_RXF_CF_VLAN_STRIP)) |
		(rxf->hds_cfg.hdr_type & ~BNA_HDS_FORCED) |
		rxf->hds_cfg.header_size,
		base_addr + off);
}

void
__rxf_vlan_filter_set(struct bna_rxf *rxf, enum bna_status status)
{
	struct bna *bna = rxf->rx->bna;
	int i;

	writel(BNA_GET_PAGE_NUM(LUT0_MEM_BLK_BASE_PG_NUM +
			(bna->port_num * 2), VLAN_RAM_BASE_OFFSET),
			bna->regs.page_addr);

	if (status == BNA_STATUS_T_ENABLED) {
		/* enable VLAN filtering on this function */
		for (i = 0; i <= BFI_MAX_VLAN / 32; i++) {
			writel(rxf->vlan_filter_table[i],
					BNA_GET_VLAN_MEM_ENTRY_ADDR
					(bna->pcidev.pci_bar_kva, rxf->rxf_id,
						i * 32));
		}
	} else {
		/* disable VLAN filtering on this function */
		for (i = 0; i <= BFI_MAX_VLAN / 32; i++) {
			writel(0xffffffff,
					BNA_GET_VLAN_MEM_ENTRY_ADDR
					(bna->pcidev.pci_bar_kva, rxf->rxf_id,
						i * 32));
		}
	}
}

static void
__rxf_rit_set(struct bna_rxf *rxf)
{
	struct bna *bna = rxf->rx->bna;
	struct bna_rit_mem *rit_mem;
	int i;
	void __iomem *base_addr;
	unsigned long off;

	base_addr = BNA_GET_MEM_BASE_ADDR(bna->pcidev.pci_bar_kva,
			FUNCTION_TO_RXQ_TRANSLATE);

	rit_mem = (struct bna_rit_mem *)0;

	writel(BNA_GET_PAGE_NUM(RXA0_MEM_BLK_BASE_PG_NUM + bna->port_num,
		FUNCTION_TO_RXQ_TRANSLATE),
		bna->regs.page_addr);

	for (i = 0; i < rxf->rit_segment->rit_size; i++) {
		off = (unsigned long)&rit_mem[i + rxf->rit_segment->rit_offset];
		writel(rxf->rit_segment->rit[i].large_rxq_id << 6 |
			rxf->rit_segment->rit[i].small_rxq_id,
			base_addr + off);
	}
}

static void
__bna_rxf_stat_clr(struct bna_rxf *rxf)
{
	struct bfi_ll_stats_req ll_req;
	u32 bm[2] = {0, 0};

	if (rxf->rxf_id < 32)
		bm[0] = 1 << rxf->rxf_id;
	else
		bm[1] = 1 << (rxf->rxf_id - 32);

	bfi_h2i_set(ll_req.mh, BFI_MC_LL, BFI_LL_H2I_STATS_CLEAR_REQ, 0);
	ll_req.stats_mask = 0;
	ll_req.txf_id_mask[0] = 0;
	ll_req.txf_id_mask[1] =	0;

	ll_req.rxf_id_mask[0] = htonl(bm[0]);
	ll_req.rxf_id_mask[1] = htonl(bm[1]);

	bna_mbox_qe_fill(&rxf->mbox_qe, &ll_req, sizeof(ll_req),
			bna_rxf_cb_stats_cleared, rxf);
	bna_mbox_send(rxf->rx->bna, &rxf->mbox_qe);
}

static void
rxf_enable(struct bna_rxf *rxf)
{
	if (rxf->rxf_oper_state == BNA_RXF_OPER_STATE_PAUSED)
		bfa_fsm_send_event(rxf, RXF_E_STARTED);
	else {
		rxf->rxf_flags |= BNA_RXF_FL_RXF_ENABLED;
		__rxf_enable(rxf);
	}
}

static void
rxf_cb_enabled(void *arg, int status)
{
	struct bna_rxf *rxf = (struct bna_rxf *)arg;

	bfa_q_qe_init(&rxf->mbox_qe.qe);
	bfa_fsm_send_event(rxf, RXF_E_STARTED);
}

static void
rxf_disable(struct bna_rxf *rxf)
{
	if (rxf->rxf_oper_state == BNA_RXF_OPER_STATE_PAUSED)
		bfa_fsm_send_event(rxf, RXF_E_STOPPED);
	else
		rxf->rxf_flags &= ~BNA_RXF_FL_RXF_ENABLED;
		__rxf_disable(rxf);
}

static void
rxf_cb_disabled(void *arg, int status)
{
	struct bna_rxf *rxf = (struct bna_rxf *)arg;

	bfa_q_qe_init(&rxf->mbox_qe.qe);
	bfa_fsm_send_event(rxf, RXF_E_STOPPED);
}

void
rxf_cb_cam_fltr_mbox_cmd(void *arg, int status)
{
	struct bna_rxf *rxf = (struct bna_rxf *)arg;

	bfa_q_qe_init(&rxf->mbox_qe.qe);

	bfa_fsm_send_event(rxf, RXF_E_CAM_FLTR_RESP);
}

static void
bna_rxf_cb_stats_cleared(void *arg, int status)
{
	struct bna_rxf *rxf = (struct bna_rxf *)arg;

	bfa_q_qe_init(&rxf->mbox_qe.qe);
	bfa_fsm_send_event(rxf, RXF_E_STAT_CLEARED);
}

void
rxf_cam_mbox_cmd(struct bna_rxf *rxf, u8 cmd,
		const struct bna_mac *mac_addr)
{
	struct bfi_ll_mac_addr_req req;

	bfi_h2i_set(req.mh, BFI_MC_LL, cmd, 0);

	req.rxf_id = rxf->rxf_id;
	memcpy(&req.mac_addr, (void *)&mac_addr->addr, ETH_ALEN);

	bna_mbox_qe_fill(&rxf->mbox_qe, &req, sizeof(req),
				rxf_cb_cam_fltr_mbox_cmd, rxf);

	bna_mbox_send(rxf->rx->bna, &rxf->mbox_qe);
}

static int
rxf_process_packet_filter_mcast(struct bna_rxf *rxf)
{
	struct bna_mac *mac = NULL;
	struct list_head *qe;

	/* Add multicast entries */
	if (!list_empty(&rxf->mcast_pending_add_q)) {
		bfa_q_deq(&rxf->mcast_pending_add_q, &qe);
		bfa_q_qe_init(qe);
		mac = (struct bna_mac *)qe;
		rxf_cam_mbox_cmd(rxf, BFI_LL_H2I_MAC_MCAST_ADD_REQ, mac);
		list_add_tail(&mac->qe, &rxf->mcast_active_q);
		return 1;
	}

	/* Delete multicast entries previousely added */
	if (!list_empty(&rxf->mcast_pending_del_q)) {
		bfa_q_deq(&rxf->mcast_pending_del_q, &qe);
		bfa_q_qe_init(qe);
		mac = (struct bna_mac *)qe;
		rxf_cam_mbox_cmd(rxf, BFI_LL_H2I_MAC_MCAST_DEL_REQ, mac);
		bna_mcam_mod_mac_put(&rxf->rx->bna->mcam_mod, mac);
		return 1;
	}

	return 0;
}

static int
rxf_process_packet_filter_vlan(struct bna_rxf *rxf)
{
	/* Apply the VLAN filter */
	if (rxf->rxf_flags & BNA_RXF_FL_VLAN_CONFIG_PENDING) {
		rxf->rxf_flags &= ~BNA_RXF_FL_VLAN_CONFIG_PENDING;
		if (!(rxf->rxmode_active & BNA_RXMODE_PROMISC))
			__rxf_vlan_filter_set(rxf, rxf->vlan_filter_status);
	}

	/* Apply RSS configuration */
	if (rxf->rxf_flags & BNA_RXF_FL_RSS_CONFIG_PENDING) {
		rxf->rxf_flags &= ~BNA_RXF_FL_RSS_CONFIG_PENDING;
		if (rxf->rss_status == BNA_STATUS_T_DISABLED) {
			/* RSS is being disabled */
			rxf->ctrl_flags &= ~BNA_RXF_CF_RSS_ENABLE;
			__rxf_rit_set(rxf);
			__rxf_config_set(rxf);
		} else {
			/* RSS is being enabled or reconfigured */
			rxf->ctrl_flags |= BNA_RXF_CF_RSS_ENABLE;
			__rxf_rit_set(rxf);
			__rxf_config_set(rxf);
		}
	}

	return 0;
}

/**
 * Processes pending ucast, mcast entry addition/deletion and issues mailbox
 * command. Also processes pending filter configuration - promiscuous mode,
 * default mode, allmutli mode and issues mailbox command or directly applies
 * to h/w
 */
static int
rxf_process_packet_filter(struct bna_rxf *rxf)
{
	/* Set the default MAC first */
	if (rxf->ucast_pending_set > 0) {
		rxf_cam_mbox_cmd(rxf, BFI_LL_H2I_MAC_UCAST_SET_REQ,
				rxf->ucast_active_mac);
		rxf->ucast_pending_set--;
		return 1;
	}

	if (rxf_process_packet_filter_ucast(rxf))
		return 1;

	if (rxf_process_packet_filter_mcast(rxf))
		return 1;

	if (rxf_process_packet_filter_promisc(rxf))
		return 1;

	if (rxf_process_packet_filter_allmulti(rxf))
		return 1;

	if (rxf_process_packet_filter_vlan(rxf))
		return 1;

	return 0;
}

static int
rxf_clear_packet_filter_mcast(struct bna_rxf *rxf)
{
	struct bna_mac *mac = NULL;
	struct list_head *qe;

	/* 3. delete pending mcast entries */
	if (!list_empty(&rxf->mcast_pending_del_q)) {
		bfa_q_deq(&rxf->mcast_pending_del_q, &qe);
		bfa_q_qe_init(qe);
		mac = (struct bna_mac *)qe;
		rxf_cam_mbox_cmd(rxf, BFI_LL_H2I_MAC_MCAST_DEL_REQ, mac);
		bna_mcam_mod_mac_put(&rxf->rx->bna->mcam_mod, mac);
		return 1;
	}

	/* 4. clear active mcast entries; move them to pending_add_q */
	if (!list_empty(&rxf->mcast_active_q)) {
		bfa_q_deq(&rxf->mcast_active_q, &qe);
		bfa_q_qe_init(qe);
		mac = (struct bna_mac *)qe;
		rxf_cam_mbox_cmd(rxf, BFI_LL_H2I_MAC_MCAST_DEL_REQ, mac);
		list_add_tail(&mac->qe, &rxf->mcast_pending_add_q);
		return 1;
	}

	return 0;
}

/**
 * In the rxf stop path, processes pending ucast/mcast delete queue and issues
 * the mailbox command. Moves the active ucast/mcast entries to pending add q,
 * so that they are added to CAM again in the rxf start path. Moves the current
 * filter settings - promiscuous, default, allmutli - to pending filter
 * configuration
 */
static int
rxf_clear_packet_filter(struct bna_rxf *rxf)
{
	if (rxf_clear_packet_filter_ucast(rxf))
		return 1;

	if (rxf_clear_packet_filter_mcast(rxf))
		return 1;

	/* 5. clear active default MAC in the CAM */
	if (rxf->ucast_pending_set > 0)
		rxf->ucast_pending_set = 0;

	if (rxf_clear_packet_filter_promisc(rxf))
		return 1;

	if (rxf_clear_packet_filter_allmulti(rxf))
		return 1;

	return 0;
}

static void
rxf_reset_packet_filter_mcast(struct bna_rxf *rxf)
{
	struct list_head *qe;
	struct bna_mac *mac;

	/* 3. Move active mcast entries to pending_add_q */
	while (!list_empty(&rxf->mcast_active_q)) {
		bfa_q_deq(&rxf->mcast_active_q, &qe);
		bfa_q_qe_init(qe);
		list_add_tail(qe, &rxf->mcast_pending_add_q);
	}

	/* 4. Throw away delete pending mcast entries */
	while (!list_empty(&rxf->mcast_pending_del_q)) {
		bfa_q_deq(&rxf->mcast_pending_del_q, &qe);
		bfa_q_qe_init(qe);
		mac = (struct bna_mac *)qe;
		bna_mcam_mod_mac_put(&rxf->rx->bna->mcam_mod, mac);
	}
}

/**
 * In the rxf fail path, throws away the ucast/mcast entries pending for
 * deletion, moves all active ucast/mcast entries to pending queue so that
 * they are added back to CAM in the rxf start path. Also moves the current
 * filter configuration to pending filter configuration.
 */
static void
rxf_reset_packet_filter(struct bna_rxf *rxf)
{
	rxf_reset_packet_filter_ucast(rxf);

	rxf_reset_packet_filter_mcast(rxf);

	/* 5. Turn off ucast set flag */
	rxf->ucast_pending_set = 0;

	rxf_reset_packet_filter_promisc(rxf);

	rxf_reset_packet_filter_allmulti(rxf);
}

static void
bna_rxf_init(struct bna_rxf *rxf,
		struct bna_rx *rx,
		struct bna_rx_config *q_config)
{
	struct list_head *qe;
	struct bna_rxp *rxp;

	/* rxf_id is initialized during rx_mod init */
	rxf->rx = rx;

	INIT_LIST_HEAD(&rxf->ucast_pending_add_q);
	INIT_LIST_HEAD(&rxf->ucast_pending_del_q);
	rxf->ucast_pending_set = 0;
	INIT_LIST_HEAD(&rxf->ucast_active_q);
	rxf->ucast_active_mac = NULL;

	INIT_LIST_HEAD(&rxf->mcast_pending_add_q);
	INIT_LIST_HEAD(&rxf->mcast_pending_del_q);
	INIT_LIST_HEAD(&rxf->mcast_active_q);

	bfa_q_qe_init(&rxf->mbox_qe.qe);

	if (q_config->vlan_strip_status == BNA_STATUS_T_ENABLED)
		rxf->ctrl_flags |= BNA_RXF_CF_VLAN_STRIP;

	rxf->rxf_oper_state = (q_config->paused) ?
		BNA_RXF_OPER_STATE_PAUSED : BNA_RXF_OPER_STATE_RUNNING;

	bna_rxf_adv_init(rxf, rx, q_config);

	rxf->rit_segment = bna_rit_mod_seg_get(&rxf->rx->bna->rit_mod,
					q_config->num_paths);

	list_for_each(qe, &rx->rxp_q) {
		rxp = (struct bna_rxp *)qe;
		if (q_config->rxp_type == BNA_RXP_SINGLE)
			rxf->mcast_rxq_id = rxp->rxq.single.only->rxq_id;
		else
			rxf->mcast_rxq_id = rxp->rxq.slr.large->rxq_id;
		break;
	}

	rxf->vlan_filter_status = BNA_STATUS_T_DISABLED;
	memset(rxf->vlan_filter_table, 0,
			(sizeof(u32) * ((BFI_MAX_VLAN + 1) / 32)));

	/* Set up VLAN 0 for pure priority tagged packets */
	rxf->vlan_filter_table[0] |= 1;

	bfa_fsm_set_state(rxf, bna_rxf_sm_stopped);
}

static void
bna_rxf_uninit(struct bna_rxf *rxf)
{
	struct bna *bna = rxf->rx->bna;
	struct bna_mac *mac;

	bna_rit_mod_seg_put(&rxf->rx->bna->rit_mod, rxf->rit_segment);
	rxf->rit_segment = NULL;

	rxf->ucast_pending_set = 0;

	while (!list_empty(&rxf->ucast_pending_add_q)) {
		bfa_q_deq(&rxf->ucast_pending_add_q, &mac);
		bfa_q_qe_init(&mac->qe);
		bna_ucam_mod_mac_put(&rxf->rx->bna->ucam_mod, mac);
	}

	if (rxf->ucast_active_mac) {
		bfa_q_qe_init(&rxf->ucast_active_mac->qe);
		bna_ucam_mod_mac_put(&rxf->rx->bna->ucam_mod,
			rxf->ucast_active_mac);
		rxf->ucast_active_mac = NULL;
	}

	while (!list_empty(&rxf->mcast_pending_add_q)) {
		bfa_q_deq(&rxf->mcast_pending_add_q, &mac);
		bfa_q_qe_init(&mac->qe);
		bna_mcam_mod_mac_put(&rxf->rx->bna->mcam_mod, mac);
	}

	/* Turn off pending promisc mode */
	if (is_promisc_enable(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask)) {
		/* system promisc state should be pending */
		BUG_ON(!(bna->rxf_promisc_id == rxf->rxf_id));
		promisc_inactive(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask);
		 bna->rxf_promisc_id = BFI_MAX_RXF;
	}
	/* Promisc mode should not be active */
	BUG_ON(rxf->rxmode_active & BNA_RXMODE_PROMISC);

	/* Turn off pending all-multi mode */
	if (is_allmulti_enable(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask)) {
		allmulti_inactive(rxf->rxmode_pending,
				rxf->rxmode_pending_bitmask);
	}
	/* Allmulti mode should not be active */
	BUG_ON(rxf->rxmode_active & BNA_RXMODE_ALLMULTI);

	rxf->rx = NULL;
}

static void
bna_rx_cb_rxf_started(struct bna_rx *rx, enum bna_cb_status status)
{
	bfa_fsm_send_event(rx, RX_E_RXF_STARTED);
	if (rx->rxf.rxf_id < 32)
		rx->bna->rx_mod.rxf_bmap[0] |= ((u32)1 << rx->rxf.rxf_id);
	else
		rx->bna->rx_mod.rxf_bmap[1] |= ((u32)
				1 << (rx->rxf.rxf_id - 32));
}

static void
bna_rxf_start(struct bna_rxf *rxf)
{
	rxf->start_cbfn = bna_rx_cb_rxf_started;
	rxf->start_cbarg = rxf->rx;
	rxf->rxf_flags &= ~BNA_RXF_FL_FAILED;
	bfa_fsm_send_event(rxf, RXF_E_START);
}

static void
bna_rx_cb_rxf_stopped(struct bna_rx *rx, enum bna_cb_status status)
{
	bfa_fsm_send_event(rx, RX_E_RXF_STOPPED);
	if (rx->rxf.rxf_id < 32)
		rx->bna->rx_mod.rxf_bmap[0] &= ~(u32)1 << rx->rxf.rxf_id;
	else
		rx->bna->rx_mod.rxf_bmap[1] &= ~(u32)
				1 << (rx->rxf.rxf_id - 32);
}

static void
bna_rxf_stop(struct bna_rxf *rxf)
{
	rxf->stop_cbfn = bna_rx_cb_rxf_stopped;
	rxf->stop_cbarg = rxf->rx;
	bfa_fsm_send_event(rxf, RXF_E_STOP);
}

static void
bna_rxf_fail(struct bna_rxf *rxf)
{
	rxf->rxf_flags |= BNA_RXF_FL_FAILED;
	bfa_fsm_send_event(rxf, RXF_E_FAIL);
}

int
bna_rxf_state_get(struct bna_rxf *rxf)
{
	return bfa_sm_to_state(rxf_sm_table, rxf->fsm);
}

enum bna_cb_status
bna_rx_ucast_set(struct bna_rx *rx, u8 *ucmac,
		 void (*cbfn)(struct bnad *, struct bna_rx *,
			      enum bna_cb_status))
{
	struct bna_rxf *rxf = &rx->rxf;

	if (rxf->ucast_active_mac == NULL) {
		rxf->ucast_active_mac =
				bna_ucam_mod_mac_get(&rxf->rx->bna->ucam_mod);
		if (rxf->ucast_active_mac == NULL)
			return BNA_CB_UCAST_CAM_FULL;
		bfa_q_qe_init(&rxf->ucast_active_mac->qe);
	}

	memcpy(rxf->ucast_active_mac->addr, ucmac, ETH_ALEN);
	rxf->ucast_pending_set++;
	rxf->cam_fltr_cbfn = cbfn;
	rxf->cam_fltr_cbarg = rx->bna->bnad;

	bfa_fsm_send_event(rxf, RXF_E_CAM_FLTR_MOD);

	return BNA_CB_SUCCESS;
}

enum bna_cb_status
bna_rx_mcast_add(struct bna_rx *rx, u8 *addr,
		 void (*cbfn)(struct bnad *, struct bna_rx *,
			      enum bna_cb_status))
{
	struct bna_rxf *rxf = &rx->rxf;
	struct list_head	*qe;
	struct bna_mac *mac;

	/* Check if already added */
	list_for_each(qe, &rxf->mcast_active_q) {
		mac = (struct bna_mac *)qe;
		if (BNA_MAC_IS_EQUAL(mac->addr, addr)) {
			if (cbfn)
				(*cbfn)(rx->bna->bnad, rx, BNA_CB_SUCCESS);
			return BNA_CB_SUCCESS;
		}
	}

	/* Check if pending addition */
	list_for_each(qe, &rxf->mcast_pending_add_q) {
		mac = (struct bna_mac *)qe;
		if (BNA_MAC_IS_EQUAL(mac->addr, addr)) {
			if (cbfn)
				(*cbfn)(rx->bna->bnad, rx, BNA_CB_SUCCESS);
			return BNA_CB_SUCCESS;
		}
	}

	mac = bna_mcam_mod_mac_get(&rxf->rx->bna->mcam_mod);
	if (mac == NULL)
		return BNA_CB_MCAST_LIST_FULL;
	bfa_q_qe_init(&mac->qe);
	memcpy(mac->addr, addr, ETH_ALEN);
	list_add_tail(&mac->qe, &rxf->mcast_pending_add_q);

	rxf->cam_fltr_cbfn = cbfn;
	rxf->cam_fltr_cbarg = rx->bna->bnad;

	bfa_fsm_send_event(rxf, RXF_E_CAM_FLTR_MOD);

	return BNA_CB_SUCCESS;
}

enum bna_cb_status
bna_rx_mcast_listset(struct bna_rx *rx, int count, u8 *mclist,
		     void (*cbfn)(struct bnad *, struct bna_rx *,
				  enum bna_cb_status))
{
	struct bna_rxf *rxf = &rx->rxf;
	struct list_head list_head;
	struct list_head *qe;
	u8 *mcaddr;
	struct bna_mac *mac;
	struct bna_mac *mac1;
	int skip;
	int delete;
	int need_hw_config = 0;
	int i;

	/* Allocate nodes */
	INIT_LIST_HEAD(&list_head);
	for (i = 0, mcaddr = mclist; i < count; i++) {
		mac = bna_mcam_mod_mac_get(&rxf->rx->bna->mcam_mod);
		if (mac == NULL)
			goto err_return;
		bfa_q_qe_init(&mac->qe);
		memcpy(mac->addr, mcaddr, ETH_ALEN);
		list_add_tail(&mac->qe, &list_head);

		mcaddr += ETH_ALEN;
	}

	/* Schedule for addition */
	while (!list_empty(&list_head)) {
		bfa_q_deq(&list_head, &qe);
		mac = (struct bna_mac *)qe;
		bfa_q_qe_init(&mac->qe);

		skip = 0;

		/* Skip if already added */
		list_for_each(qe, &rxf->mcast_active_q) {
			mac1 = (struct bna_mac *)qe;
			if (BNA_MAC_IS_EQUAL(mac1->addr, mac->addr)) {
				bna_mcam_mod_mac_put(&rxf->rx->bna->mcam_mod,
							mac);
				skip = 1;
				break;
			}
		}

		if (skip)
			continue;

		/* Skip if pending addition */
		list_for_each(qe, &rxf->mcast_pending_add_q) {
			mac1 = (struct bna_mac *)qe;
			if (BNA_MAC_IS_EQUAL(mac1->addr, mac->addr)) {
				bna_mcam_mod_mac_put(&rxf->rx->bna->mcam_mod,
							mac);
				skip = 1;
				break;
			}
		}

		if (skip)
			continue;

		need_hw_config = 1;
		list_add_tail(&mac->qe, &rxf->mcast_pending_add_q);
	}

	/**
	 * Delete the entries that are in the pending_add_q but not
	 * in the new list
	 */
	while (!list_empty(&rxf->mcast_pending_add_q)) {
		bfa_q_deq(&rxf->mcast_pending_add_q, &qe);
		mac = (struct bna_mac *)qe;
		bfa_q_qe_init(&mac->qe);
		for (i = 0, mcaddr = mclist, delete = 1; i < count; i++) {
			if (BNA_MAC_IS_EQUAL(mcaddr, mac->addr)) {
				delete = 0;
				break;
			}
			mcaddr += ETH_ALEN;
		}
		if (delete)
			bna_mcam_mod_mac_put(&rxf->rx->bna->mcam_mod, mac);
		else
			list_add_tail(&mac->qe, &list_head);
	}
	while (!list_empty(&list_head)) {
		bfa_q_deq(&list_head, &qe);
		mac = (struct bna_mac *)qe;
		bfa_q_qe_init(&mac->qe);
		list_add_tail(&mac->qe, &rxf->mcast_pending_add_q);
	}

	/**
	 * Schedule entries for deletion that are in the active_q but not
	 * in the new list
	 */
	while (!list_empty(&rxf->mcast_active_q)) {
		bfa_q_deq(&rxf->mcast_active_q, &qe);
		mac = (struct bna_mac *)qe;
		bfa_q_qe_init(&mac->qe);
		for (i = 0, mcaddr = mclist, delete = 1; i < count; i++) {
			if (BNA_MAC_IS_EQUAL(mcaddr, mac->addr)) {
				delete = 0;
				break;
			}
			mcaddr += ETH_ALEN;
		}
		if (delete) {
			list_add_tail(&mac->qe, &rxf->mcast_pending_del_q);
			need_hw_config = 1;
		} else {
			list_add_tail(&mac->qe, &list_head);
		}
	}
	while (!list_empty(&list_head)) {
		bfa_q_deq(&list_head, &qe);
		mac = (struct bna_mac *)qe;
		bfa_q_qe_init(&mac->qe);
		list_add_tail(&mac->qe, &rxf->mcast_active_q);
	}

	if (need_hw_config) {
		rxf->cam_fltr_cbfn = cbfn;
		rxf->cam_fltr_cbarg = rx->bna->bnad;
		bfa_fsm_send_event(rxf, RXF_E_CAM_FLTR_MOD);
	} else if (cbfn)
		(*cbfn)(rx->bna->bnad, rx, BNA_CB_SUCCESS);

	return BNA_CB_SUCCESS;

err_return:
	while (!list_empty(&list_head)) {
		bfa_q_deq(&list_head, &qe);
		mac = (struct bna_mac *)qe;
		bfa_q_qe_init(&mac->qe);
		bna_mcam_mod_mac_put(&rxf->rx->bna->mcam_mod, mac);
	}

	return BNA_CB_MCAST_LIST_FULL;
}

void
bna_rx_vlan_add(struct bna_rx *rx, int vlan_id)
{
	struct bna_rxf *rxf = &rx->rxf;
	int index = (vlan_id >> 5);
	int bit = (1 << (vlan_id & 0x1F));

	rxf->vlan_filter_table[index] |= bit;
	if (rxf->vlan_filter_status == BNA_STATUS_T_ENABLED) {
		rxf->rxf_flags |= BNA_RXF_FL_VLAN_CONFIG_PENDING;
		bfa_fsm_send_event(rxf, RXF_E_CAM_FLTR_MOD);
	}
}

void
bna_rx_vlan_del(struct bna_rx *rx, int vlan_id)
{
	struct bna_rxf *rxf = &rx->rxf;
	int index = (vlan_id >> 5);
	int bit = (1 << (vlan_id & 0x1F));

	rxf->vlan_filter_table[index] &= ~bit;
	if (rxf->vlan_filter_status == BNA_STATUS_T_ENABLED) {
		rxf->rxf_flags |= BNA_RXF_FL_VLAN_CONFIG_PENDING;
		bfa_fsm_send_event(rxf, RXF_E_CAM_FLTR_MOD);
	}
}

/**
 * RX
 */
#define	RXQ_RCB_INIT(q, rxp, qdepth, bna, _id, unmapq_mem)	do {	\
	struct bna_doorbell_qset *_qset;				\
	unsigned long off;						\
	(q)->rcb->producer_index = (q)->rcb->consumer_index = 0;	\
	(q)->rcb->q_depth = (qdepth);					\
	(q)->rcb->unmap_q = unmapq_mem;					\
	(q)->rcb->rxq = (q);						\
	(q)->rcb->cq = &(rxp)->cq;					\
	(q)->rcb->bnad = (bna)->bnad;					\
	_qset = (struct bna_doorbell_qset *)0;			\
	off = (unsigned long)&_qset[(q)->rxq_id].rxq[0];		\
	(q)->rcb->q_dbell = off +					\
		BNA_GET_DOORBELL_BASE_ADDR((bna)->pcidev.pci_bar_kva);	\
	(q)->rcb->id = _id;						\
} while (0)

#define	BNA_GET_RXQS(qcfg)	(((qcfg)->rxp_type == BNA_RXP_SINGLE) ?	\
	(qcfg)->num_paths : ((qcfg)->num_paths * 2))

#define	SIZE_TO_PAGES(size)	(((size) >> PAGE_SHIFT) + ((((size) &\
	(PAGE_SIZE - 1)) + (PAGE_SIZE - 1)) >> PAGE_SHIFT))

#define	call_rx_stop_callback(rx, status)				\
	if ((rx)->stop_cbfn) {						\
		(*(rx)->stop_cbfn)((rx)->stop_cbarg, rx, (status));	\
		(rx)->stop_cbfn = NULL;					\
		(rx)->stop_cbarg = NULL;				\
	}

/*
 * Since rx_enable is synchronous callback, there is no start_cbfn required.
 * Instead, we'll call bnad_rx_post(rxp) so that bnad can post the buffers
 * for each rxpath.
 */

#define	call_rx_disable_cbfn(rx, status)				\
		if ((rx)->disable_cbfn)	{				\
			(*(rx)->disable_cbfn)((rx)->disable_cbarg,	\
					status);			\
			(rx)->disable_cbfn = NULL;			\
			(rx)->disable_cbarg = NULL;			\
		}							\

#define	rxqs_reqd(type, num_rxqs)					\
	(((type) == BNA_RXP_SINGLE) ? (num_rxqs) : ((num_rxqs) * 2))

#define rx_ib_fail(rx)						\
do {								\
	struct bna_rxp *rxp;					\
	struct list_head *qe;						\
	list_for_each(qe, &(rx)->rxp_q) {				\
		rxp = (struct bna_rxp *)qe;			\
		bna_ib_fail(rxp->cq.ib);			\
	}							\
} while (0)

static void __bna_multi_rxq_stop(struct bna_rxp *, u32 *);
static void __bna_rxq_start(struct bna_rxq *rxq);
static void __bna_cq_start(struct bna_cq *cq);
static void bna_rit_create(struct bna_rx *rx);
static void bna_rx_cb_multi_rxq_stopped(void *arg, int status);
static void bna_rx_cb_rxq_stopped_all(void *arg);

bfa_fsm_state_decl(bna_rx, stopped,
	struct bna_rx, enum bna_rx_event);
bfa_fsm_state_decl(bna_rx, rxf_start_wait,
	struct bna_rx, enum bna_rx_event);
bfa_fsm_state_decl(bna_rx, started,
	struct bna_rx, enum bna_rx_event);
bfa_fsm_state_decl(bna_rx, rxf_stop_wait,
	struct bna_rx, enum bna_rx_event);
bfa_fsm_state_decl(bna_rx, rxq_stop_wait,
	struct bna_rx, enum bna_rx_event);

static const struct bfa_sm_table rx_sm_table[] = {
	{BFA_SM(bna_rx_sm_stopped), BNA_RX_STOPPED},
	{BFA_SM(bna_rx_sm_rxf_start_wait), BNA_RX_RXF_START_WAIT},
	{BFA_SM(bna_rx_sm_started), BNA_RX_STARTED},
	{BFA_SM(bna_rx_sm_rxf_stop_wait), BNA_RX_RXF_STOP_WAIT},
	{BFA_SM(bna_rx_sm_rxq_stop_wait), BNA_RX_RXQ_STOP_WAIT},
};

static void bna_rx_sm_stopped_entry(struct bna_rx *rx)
{
	struct bna_rxp *rxp;
	struct list_head *qe_rxp;

	list_for_each(qe_rxp, &rx->rxp_q) {
		rxp = (struct bna_rxp *)qe_rxp;
		rx->rx_cleanup_cbfn(rx->bna->bnad, rxp->cq.ccb);
	}

	call_rx_stop_callback(rx, BNA_CB_SUCCESS);
}

static void bna_rx_sm_stopped(struct bna_rx *rx,
				enum bna_rx_event event)
{
	switch (event) {
	case RX_E_START:
		bfa_fsm_set_state(rx, bna_rx_sm_rxf_start_wait);
		break;
	case RX_E_STOP:
		call_rx_stop_callback(rx, BNA_CB_SUCCESS);
		break;
	case RX_E_FAIL:
		/* no-op */
		break;
	default:
		bfa_sm_fault(rx->bna, event);
		break;
	}

}

static void bna_rx_sm_rxf_start_wait_entry(struct bna_rx *rx)
{
	struct bna_rxp *rxp;
	struct list_head *qe_rxp;
	struct bna_rxq *q0 = NULL, *q1 = NULL;

	/* Setup the RIT */
	bna_rit_create(rx);

	list_for_each(qe_rxp, &rx->rxp_q) {
		rxp = (struct bna_rxp *)qe_rxp;
		bna_ib_start(rxp->cq.ib);
		GET_RXQS(rxp, q0, q1);
		q0->buffer_size = bna_port_mtu_get(&rx->bna->port);
		__bna_rxq_start(q0);
		rx->rx_post_cbfn(rx->bna->bnad, q0->rcb);
		if (q1)  {
			__bna_rxq_start(q1);
			rx->rx_post_cbfn(rx->bna->bnad, q1->rcb);
		}
		__bna_cq_start(&rxp->cq);
	}

	bna_rxf_start(&rx->rxf);
}

static void bna_rx_sm_rxf_start_wait(struct bna_rx *rx,
				enum bna_rx_event event)
{
	switch (event) {
	case RX_E_STOP:
		bfa_fsm_set_state(rx, bna_rx_sm_rxf_stop_wait);
		break;
	case RX_E_FAIL:
		bfa_fsm_set_state(rx, bna_rx_sm_stopped);
		rx_ib_fail(rx);
		bna_rxf_fail(&rx->rxf);
		break;
	case RX_E_RXF_STARTED:
		bfa_fsm_set_state(rx, bna_rx_sm_started);
		break;
	default:
		bfa_sm_fault(rx->bna, event);
		break;
	}
}

void
bna_rx_sm_started_entry(struct bna_rx *rx)
{
	struct bna_rxp *rxp;
	struct list_head *qe_rxp;

	/* Start IB */
	list_for_each(qe_rxp, &rx->rxp_q) {
		rxp = (struct bna_rxp *)qe_rxp;
		bna_ib_ack(&rxp->cq.ib->door_bell, 0);
	}

	bna_llport_rx_started(&rx->bna->port.llport);
}

void
bna_rx_sm_started(struct bna_rx *rx, enum bna_rx_event event)
{
	switch (event) {
	case RX_E_FAIL:
		bna_llport_rx_stopped(&rx->bna->port.llport);
		bfa_fsm_set_state(rx, bna_rx_sm_stopped);
		rx_ib_fail(rx);
		bna_rxf_fail(&rx->rxf);
		break;
	case RX_E_STOP:
		bna_llport_rx_stopped(&rx->bna->port.llport);
		bfa_fsm_set_state(rx, bna_rx_sm_rxf_stop_wait);
		break;
	default:
		bfa_sm_fault(rx->bna, event);
		break;
	}
}

void
bna_rx_sm_rxf_stop_wait_entry(struct bna_rx *rx)
{
	bna_rxf_stop(&rx->rxf);
}

void
bna_rx_sm_rxf_stop_wait(struct bna_rx *rx, enum bna_rx_event event)
{
	switch (event) {
	case RX_E_RXF_STOPPED:
		bfa_fsm_set_state(rx, bna_rx_sm_rxq_stop_wait);
		break;
	case RX_E_RXF_STARTED:
		/**
		 * RxF was in the process of starting up when
		 * RXF_E_STOP was issued. Ignore this event
		 */
		break;
	case RX_E_FAIL:
		bfa_fsm_set_state(rx, bna_rx_sm_stopped);
		rx_ib_fail(rx);
		bna_rxf_fail(&rx->rxf);
		break;
	default:
		bfa_sm_fault(rx->bna, event);
		break;
	}

}

void
bna_rx_sm_rxq_stop_wait_entry(struct bna_rx *rx)
{
	struct bna_rxp *rxp = NULL;
	struct bna_rxq *q0 = NULL;
	struct bna_rxq *q1 = NULL;
	struct list_head	*qe;
	u32 rxq_mask[2] = {0, 0};

	/* Only one call to multi-rxq-stop for all RXPs in this RX */
	bfa_wc_up(&rx->rxq_stop_wc);
	list_for_each(qe, &rx->rxp_q) {
		rxp = (struct bna_rxp *)qe;
		GET_RXQS(rxp, q0, q1);
		if (q0->rxq_id < 32)
			rxq_mask[0] |= ((u32)1 << q0->rxq_id);
		else
			rxq_mask[1] |= ((u32)1 << (q0->rxq_id - 32));
		if (q1) {
			if (q1->rxq_id < 32)
				rxq_mask[0] |= ((u32)1 << q1->rxq_id);
			else
				rxq_mask[1] |= ((u32)
						1 << (q1->rxq_id - 32));
		}
	}

	__bna_multi_rxq_stop(rxp, rxq_mask);
}

void
bna_rx_sm_rxq_stop_wait(struct bna_rx *rx, enum bna_rx_event event)
{
	struct bna_rxp *rxp = NULL;
	struct list_head	*qe;

	switch (event) {
	case RX_E_RXQ_STOPPED:
		list_for_each(qe, &rx->rxp_q) {
			rxp = (struct bna_rxp *)qe;
			bna_ib_stop(rxp->cq.ib);
		}
		/* Fall through */
	case RX_E_FAIL:
		bfa_fsm_set_state(rx, bna_rx_sm_stopped);
		break;
	default:
		bfa_sm_fault(rx->bna, event);
		break;
	}
}

void
__bna_multi_rxq_stop(struct bna_rxp *rxp, u32 * rxq_id_mask)
{
	struct bfi_ll_q_stop_req ll_req;

	bfi_h2i_set(ll_req.mh, BFI_MC_LL, BFI_LL_H2I_RXQ_STOP_REQ, 0);
	ll_req.q_id_mask[0] = htonl(rxq_id_mask[0]);
	ll_req.q_id_mask[1] = htonl(rxq_id_mask[1]);
	bna_mbox_qe_fill(&rxp->mbox_qe, &ll_req, sizeof(ll_req),
		bna_rx_cb_multi_rxq_stopped, rxp);
	bna_mbox_send(rxp->rx->bna, &rxp->mbox_qe);
}

void
__bna_rxq_start(struct bna_rxq *rxq)
{
	struct bna_rxtx_q_mem *q_mem;
	struct bna_rxq_mem rxq_cfg, *rxq_mem;
	struct bna_dma_addr cur_q_addr;
	/* struct bna_doorbell_qset *qset; */
	struct bna_qpt *qpt;
	u32 pg_num;
	struct bna *bna = rxq->rx->bna;
	void __iomem *base_addr;
	unsigned long off;

	qpt = &rxq->qpt;
	cur_q_addr = *((struct bna_dma_addr *)(qpt->kv_qpt_ptr));

	rxq_cfg.pg_tbl_addr_lo = qpt->hw_qpt_ptr.lsb;
	rxq_cfg.pg_tbl_addr_hi = qpt->hw_qpt_ptr.msb;
	rxq_cfg.cur_q_entry_lo = cur_q_addr.lsb;
	rxq_cfg.cur_q_entry_hi = cur_q_addr.msb;

	rxq_cfg.pg_cnt_n_prd_ptr = ((u32)qpt->page_count << 16) | 0x0;
	rxq_cfg.entry_n_pg_size = ((u32)(BFI_RXQ_WI_SIZE >> 2) << 16) |
		(qpt->page_size >> 2);
	rxq_cfg.sg_n_cq_n_cns_ptr =
		((u32)(rxq->rxp->cq.cq_id & 0xff) << 16) | 0x0;
	rxq_cfg.buf_sz_n_q_state = ((u32)rxq->buffer_size << 16) |
		BNA_Q_IDLE_STATE;
	rxq_cfg.next_qid = 0x0 | (0x3 << 8);

	/* Write the page number register */
	pg_num = BNA_GET_PAGE_NUM(HQM0_BLK_PG_NUM + bna->port_num,
			HQM_RXTX_Q_RAM_BASE_OFFSET);
	writel(pg_num, bna->regs.page_addr);

	/* Write to h/w */
	base_addr = BNA_GET_MEM_BASE_ADDR(bna->pcidev.pci_bar_kva,
					HQM_RXTX_Q_RAM_BASE_OFFSET);

	q_mem = (struct bna_rxtx_q_mem *)0;
	rxq_mem = &q_mem[rxq->rxq_id].rxq;

	off = (unsigned long)&rxq_mem->pg_tbl_addr_lo;
	writel(htonl(rxq_cfg.pg_tbl_addr_lo), base_addr + off);

	off = (unsigned long)&rxq_mem->pg_tbl_addr_hi;
	writel(htonl(rxq_cfg.pg_tbl_addr_hi), base_addr + off);

	off = (unsigned long)&rxq_mem->cur_q_entry_lo;
	writel(htonl(rxq_cfg.cur_q_entry_lo), base_addr + off);

	off = (unsigned long)&rxq_mem->cur_q_entry_hi;
	writel(htonl(rxq_cfg.cur_q_entry_hi), base_addr + off);

	off = (unsigned long)&rxq_mem->pg_cnt_n_prd_ptr;
	writel(rxq_cfg.pg_cnt_n_prd_ptr, base_addr + off);

	off = (unsigned long)&rxq_mem->entry_n_pg_size;
	writel(rxq_cfg.entry_n_pg_size, base_addr + off);

	off = (unsigned long)&rxq_mem->sg_n_cq_n_cns_ptr;
	writel(rxq_cfg.sg_n_cq_n_cns_ptr, base_addr + off);

	off = (unsigned long)&rxq_mem->buf_sz_n_q_state;
	writel(rxq_cfg.buf_sz_n_q_state, base_addr + off);

	off = (unsigned long)&rxq_mem->next_qid;
	writel(rxq_cfg.next_qid, base_addr + off);

	rxq->rcb->producer_index = 0;
	rxq->rcb->consumer_index = 0;
}

void
__bna_cq_start(struct bna_cq *cq)
{
	struct bna_cq_mem cq_cfg, *cq_mem;
	const struct bna_qpt *qpt;
	struct bna_dma_addr cur_q_addr;
	u32 pg_num;
	struct bna *bna = cq->rx->bna;
	void __iomem *base_addr;
	unsigned long off;

	qpt = &cq->qpt;
	cur_q_addr = *((struct bna_dma_addr *)(qpt->kv_qpt_ptr));

	/*
	 * Fill out structure, to be subsequently written
	 * to hardware
	 */
	cq_cfg.pg_tbl_addr_lo = qpt->hw_qpt_ptr.lsb;
	cq_cfg.pg_tbl_addr_hi = qpt->hw_qpt_ptr.msb;
	cq_cfg.cur_q_entry_lo = cur_q_addr.lsb;
	cq_cfg.cur_q_entry_hi = cur_q_addr.msb;

	cq_cfg.pg_cnt_n_prd_ptr = (qpt->page_count << 16) | 0x0;
	cq_cfg.entry_n_pg_size =
		((u32)(BFI_CQ_WI_SIZE >> 2) << 16) | (qpt->page_size >> 2);
	cq_cfg.int_blk_n_cns_ptr = ((((u32)cq->ib_seg_offset) << 24) |
			((u32)(cq->ib->ib_id & 0xff)  << 16) | 0x0);
	cq_cfg.q_state = BNA_Q_IDLE_STATE;

	/* Write the page number register */
	pg_num = BNA_GET_PAGE_NUM(HQM0_BLK_PG_NUM + bna->port_num,
				  HQM_CQ_RAM_BASE_OFFSET);

	writel(pg_num, bna->regs.page_addr);

	/* H/W write */
	base_addr = BNA_GET_MEM_BASE_ADDR(bna->pcidev.pci_bar_kva,
					HQM_CQ_RAM_BASE_OFFSET);

	cq_mem = (struct bna_cq_mem *)0;

	off = (unsigned long)&cq_mem[cq->cq_id].pg_tbl_addr_lo;
	writel(htonl(cq_cfg.pg_tbl_addr_lo), base_addr + off);

	off = (unsigned long)&cq_mem[cq->cq_id].pg_tbl_addr_hi;
	writel(htonl(cq_cfg.pg_tbl_addr_hi), base_addr + off);

	off = (unsigned long)&cq_mem[cq->cq_id].cur_q_entry_lo;
	writel(htonl(cq_cfg.cur_q_entry_lo), base_addr + off);

	off = (unsigned long)&cq_mem[cq->cq_id].cur_q_entry_hi;
	writel(htonl(cq_cfg.cur_q_entry_hi), base_addr + off);

	off = (unsigned long)&cq_mem[cq->cq_id].pg_cnt_n_prd_ptr;
	writel(cq_cfg.pg_cnt_n_prd_ptr, base_addr + off);

	off = (unsigned long)&cq_mem[cq->cq_id].entry_n_pg_size;
	writel(cq_cfg.entry_n_pg_size, base_addr + off);

	off = (unsigned long)&cq_mem[cq->cq_id].int_blk_n_cns_ptr;
	writel(cq_cfg.int_blk_n_cns_ptr, base_addr + off);

	off = (unsigned long)&cq_mem[cq->cq_id].q_state;
	writel(cq_cfg.q_state, base_addr + off);

	cq->ccb->producer_index = 0;
	*(cq->ccb->hw_producer_index) = 0;
}

void
bna_rit_create(struct bna_rx *rx)
{
	struct list_head	*qe_rxp;
	struct bna_rxp *rxp;
	struct bna_rxq *q0 = NULL;
	struct bna_rxq *q1 = NULL;
	int offset;

	offset = 0;
	list_for_each(qe_rxp, &rx->rxp_q) {
		rxp = (struct bna_rxp *)qe_rxp;
		GET_RXQS(rxp, q0, q1);
		rx->rxf.rit_segment->rit[offset].large_rxq_id = q0->rxq_id;
		rx->rxf.rit_segment->rit[offset].small_rxq_id =
						(q1 ? q1->rxq_id : 0);
		offset++;
	}
}

static int
_rx_can_satisfy(struct bna_rx_mod *rx_mod,
		struct bna_rx_config *rx_cfg)
{
	if ((rx_mod->rx_free_count == 0) ||
		(rx_mod->rxp_free_count == 0) ||
		(rx_mod->rxq_free_count == 0))
		return 0;

	if (rx_cfg->rxp_type == BNA_RXP_SINGLE) {
		if ((rx_mod->rxp_free_count < rx_cfg->num_paths) ||
			(rx_mod->rxq_free_count < rx_cfg->num_paths))
				return 0;
	} else {
		if ((rx_mod->rxp_free_count < rx_cfg->num_paths) ||
			(rx_mod->rxq_free_count < (2 * rx_cfg->num_paths)))
			return 0;
	}

	if (!bna_rit_mod_can_satisfy(&rx_mod->bna->rit_mod, rx_cfg->num_paths))
		return 0;

	return 1;
}

static struct bna_rxq *
_get_free_rxq(struct bna_rx_mod *rx_mod)
{
	struct bna_rxq *rxq = NULL;
	struct list_head	*qe = NULL;

	bfa_q_deq(&rx_mod->rxq_free_q, &qe);
	if (qe) {
		rx_mod->rxq_free_count--;
		rxq = (struct bna_rxq *)qe;
	}
	return rxq;
}

static void
_put_free_rxq(struct bna_rx_mod *rx_mod, struct bna_rxq *rxq)
{
	bfa_q_qe_init(&rxq->qe);
	list_add_tail(&rxq->qe, &rx_mod->rxq_free_q);
	rx_mod->rxq_free_count++;
}

static struct bna_rxp *
_get_free_rxp(struct bna_rx_mod *rx_mod)
{
	struct list_head	*qe = NULL;
	struct bna_rxp *rxp = NULL;

	bfa_q_deq(&rx_mod->rxp_free_q, &qe);
	if (qe) {
		rx_mod->rxp_free_count--;

		rxp = (struct bna_rxp *)qe;
	}

	return rxp;
}

static void
_put_free_rxp(struct bna_rx_mod *rx_mod, struct bna_rxp *rxp)
{
	bfa_q_qe_init(&rxp->qe);
	list_add_tail(&rxp->qe, &rx_mod->rxp_free_q);
	rx_mod->rxp_free_count++;
}

static struct bna_rx *
_get_free_rx(struct bna_rx_mod *rx_mod)
{
	struct list_head	*qe = NULL;
	struct bna_rx *rx = NULL;

	bfa_q_deq(&rx_mod->rx_free_q, &qe);
	if (qe) {
		rx_mod->rx_free_count--;

		rx = (struct bna_rx *)qe;
		bfa_q_qe_init(qe);
		list_add_tail(&rx->qe, &rx_mod->rx_active_q);
	}

	return rx;
}

static void
_put_free_rx(struct bna_rx_mod *rx_mod, struct bna_rx *rx)
{
	bfa_q_qe_init(&rx->qe);
	list_add_tail(&rx->qe, &rx_mod->rx_free_q);
	rx_mod->rx_free_count++;
}

static void
_rx_init(struct bna_rx *rx, struct bna *bna)
{
	rx->bna = bna;
	rx->rx_flags = 0;

	INIT_LIST_HEAD(&rx->rxp_q);

	rx->rxq_stop_wc.wc_resume = bna_rx_cb_rxq_stopped_all;
	rx->rxq_stop_wc.wc_cbarg = rx;
	rx->rxq_stop_wc.wc_count = 0;

	rx->stop_cbfn = NULL;
	rx->stop_cbarg = NULL;
}

static void
_rxp_add_rxqs(struct bna_rxp *rxp,
		struct bna_rxq *q0,
		struct bna_rxq *q1)
{
	switch (rxp->type) {
	case BNA_RXP_SINGLE:
		rxp->rxq.single.only = q0;
		rxp->rxq.single.reserved = NULL;
		break;
	case BNA_RXP_SLR:
		rxp->rxq.slr.large = q0;
		rxp->rxq.slr.small = q1;
		break;
	case BNA_RXP_HDS:
		rxp->rxq.hds.data = q0;
		rxp->rxq.hds.hdr = q1;
		break;
	default:
		break;
	}
}

static void
_rxq_qpt_init(struct bna_rxq *rxq,
		struct bna_rxp *rxp,
		u32 page_count,
		u32 page_size,
		struct bna_mem_descr *qpt_mem,
		struct bna_mem_descr *swqpt_mem,
		struct bna_mem_descr *page_mem)
{
	int	i;

	rxq->qpt.hw_qpt_ptr.lsb = qpt_mem->dma.lsb;
	rxq->qpt.hw_qpt_ptr.msb = qpt_mem->dma.msb;
	rxq->qpt.kv_qpt_ptr = qpt_mem->kva;
	rxq->qpt.page_count = page_count;
	rxq->qpt.page_size = page_size;

	rxq->rcb->sw_qpt = (void **) swqpt_mem->kva;

	for (i = 0; i < rxq->qpt.page_count; i++) {
		rxq->rcb->sw_qpt[i] = page_mem[i].kva;
		((struct bna_dma_addr *)rxq->qpt.kv_qpt_ptr)[i].lsb =
			page_mem[i].dma.lsb;
		((struct bna_dma_addr *)rxq->qpt.kv_qpt_ptr)[i].msb =
			page_mem[i].dma.msb;

	}
}

static void
_rxp_cqpt_setup(struct bna_rxp *rxp,
		u32 page_count,
		u32 page_size,
		struct bna_mem_descr *qpt_mem,
		struct bna_mem_descr *swqpt_mem,
		struct bna_mem_descr *page_mem)
{
	int	i;

	rxp->cq.qpt.hw_qpt_ptr.lsb = qpt_mem->dma.lsb;
	rxp->cq.qpt.hw_qpt_ptr.msb = qpt_mem->dma.msb;
	rxp->cq.qpt.kv_qpt_ptr = qpt_mem->kva;
	rxp->cq.qpt.page_count = page_count;
	rxp->cq.qpt.page_size = page_size;

	rxp->cq.ccb->sw_qpt = (void **) swqpt_mem->kva;

	for (i = 0; i < rxp->cq.qpt.page_count; i++) {
		rxp->cq.ccb->sw_qpt[i] = page_mem[i].kva;

		((struct bna_dma_addr *)rxp->cq.qpt.kv_qpt_ptr)[i].lsb =
			page_mem[i].dma.lsb;
		((struct bna_dma_addr *)rxp->cq.qpt.kv_qpt_ptr)[i].msb =
			page_mem[i].dma.msb;

	}
}

static void
_rx_add_rxp(struct bna_rx *rx, struct bna_rxp *rxp)
{
	list_add_tail(&rxp->qe, &rx->rxp_q);
}

static void
_init_rxmod_queues(struct bna_rx_mod *rx_mod)
{
	INIT_LIST_HEAD(&rx_mod->rx_free_q);
	INIT_LIST_HEAD(&rx_mod->rxq_free_q);
	INIT_LIST_HEAD(&rx_mod->rxp_free_q);
	INIT_LIST_HEAD(&rx_mod->rx_active_q);

	rx_mod->rx_free_count = 0;
	rx_mod->rxq_free_count = 0;
	rx_mod->rxp_free_count = 0;
}

static void
_rx_ctor(struct bna_rx *rx, int id)
{
	bfa_q_qe_init(&rx->qe);
	INIT_LIST_HEAD(&rx->rxp_q);
	rx->bna = NULL;

	rx->rxf.rxf_id = id;

	/* FIXME: mbox_qe ctor()?? */
	bfa_q_qe_init(&rx->mbox_qe.qe);

	rx->stop_cbfn = NULL;
	rx->stop_cbarg = NULL;
}

void
bna_rx_cb_multi_rxq_stopped(void *arg, int status)
{
	struct bna_rxp *rxp = (struct bna_rxp *)arg;

	bfa_wc_down(&rxp->rx->rxq_stop_wc);
}

void
bna_rx_cb_rxq_stopped_all(void *arg)
{
	struct bna_rx *rx = (struct bna_rx *)arg;

	bfa_fsm_send_event(rx, RX_E_RXQ_STOPPED);
}

static void
bna_rx_mod_cb_rx_stopped(void *arg, struct bna_rx *rx,
			 enum bna_cb_status status)
{
	struct bna_rx_mod *rx_mod = (struct bna_rx_mod *)arg;

	bfa_wc_down(&rx_mod->rx_stop_wc);
}

static void
bna_rx_mod_cb_rx_stopped_all(void *arg)
{
	struct bna_rx_mod *rx_mod = (struct bna_rx_mod *)arg;

	if (rx_mod->stop_cbfn)
		rx_mod->stop_cbfn(&rx_mod->bna->port, BNA_CB_SUCCESS);
	rx_mod->stop_cbfn = NULL;
}

static void
bna_rx_start(struct bna_rx *rx)
{
	rx->rx_flags |= BNA_RX_F_PORT_ENABLED;
	if (rx->rx_flags & BNA_RX_F_ENABLE)
		bfa_fsm_send_event(rx, RX_E_START);
}

static void
bna_rx_stop(struct bna_rx *rx)
{
	rx->rx_flags &= ~BNA_RX_F_PORT_ENABLED;
	if (rx->fsm == (bfa_fsm_t) bna_rx_sm_stopped)
		bna_rx_mod_cb_rx_stopped(&rx->bna->rx_mod, rx, BNA_CB_SUCCESS);
	else {
		rx->stop_cbfn = bna_rx_mod_cb_rx_stopped;
		rx->stop_cbarg = &rx->bna->rx_mod;
		bfa_fsm_send_event(rx, RX_E_STOP);
	}
}

static void
bna_rx_fail(struct bna_rx *rx)
{
	/* Indicate port is not enabled, and failed */
	rx->rx_flags &= ~BNA_RX_F_PORT_ENABLED;
	rx->rx_flags |= BNA_RX_F_PORT_FAILED;
	bfa_fsm_send_event(rx, RX_E_FAIL);
}

void
bna_rx_mod_start(struct bna_rx_mod *rx_mod, enum bna_rx_type type)
{
	struct bna_rx *rx;
	struct list_head *qe;

	rx_mod->flags |= BNA_RX_MOD_F_PORT_STARTED;
	if (type == BNA_RX_T_LOOPBACK)
		rx_mod->flags |= BNA_RX_MOD_F_PORT_LOOPBACK;

	list_for_each(qe, &rx_mod->rx_active_q) {
		rx = (struct bna_rx *)qe;
		if (rx->type == type)
			bna_rx_start(rx);
	}
}

void
bna_rx_mod_stop(struct bna_rx_mod *rx_mod, enum bna_rx_type type)
{
	struct bna_rx *rx;
	struct list_head *qe;

	rx_mod->flags &= ~BNA_RX_MOD_F_PORT_STARTED;
	rx_mod->flags &= ~BNA_RX_MOD_F_PORT_LOOPBACK;

	rx_mod->stop_cbfn = bna_port_cb_rx_stopped;

	/**
	 * Before calling bna_rx_stop(), increment rx_stop_wc as many times
	 * as we are going to call bna_rx_stop
	 */
	list_for_each(qe, &rx_mod->rx_active_q) {
		rx = (struct bna_rx *)qe;
		if (rx->type == type)
			bfa_wc_up(&rx_mod->rx_stop_wc);
	}

	if (rx_mod->rx_stop_wc.wc_count == 0) {
		rx_mod->stop_cbfn(&rx_mod->bna->port, BNA_CB_SUCCESS);
		rx_mod->stop_cbfn = NULL;
		return;
	}

	list_for_each(qe, &rx_mod->rx_active_q) {
		rx = (struct bna_rx *)qe;
		if (rx->type == type)
			bna_rx_stop(rx);
	}
}

void
bna_rx_mod_fail(struct bna_rx_mod *rx_mod)
{
	struct bna_rx *rx;
	struct list_head *qe;

	rx_mod->flags &= ~BNA_RX_MOD_F_PORT_STARTED;
	rx_mod->flags &= ~BNA_RX_MOD_F_PORT_LOOPBACK;

	list_for_each(qe, &rx_mod->rx_active_q) {
		rx = (struct bna_rx *)qe;
		bna_rx_fail(rx);
	}
}

void bna_rx_mod_init(struct bna_rx_mod *rx_mod, struct bna *bna,
			struct bna_res_info *res_info)
{
	int	index;
	struct bna_rx *rx_ptr;
	struct bna_rxp *rxp_ptr;
	struct bna_rxq *rxq_ptr;

	rx_mod->bna = bna;
	rx_mod->flags = 0;

	rx_mod->rx = (struct bna_rx *)
		res_info[BNA_RES_MEM_T_RX_ARRAY].res_u.mem_info.mdl[0].kva;
	rx_mod->rxp = (struct bna_rxp *)
		res_info[BNA_RES_MEM_T_RXP_ARRAY].res_u.mem_info.mdl[0].kva;
	rx_mod->rxq = (struct bna_rxq *)
		res_info[BNA_RES_MEM_T_RXQ_ARRAY].res_u.mem_info.mdl[0].kva;

	/* Initialize the queues */
	_init_rxmod_queues(rx_mod);

	/* Build RX queues */
	for (index = 0; index < BFI_MAX_RXQ; index++) {
		rx_ptr = &rx_mod->rx[index];
		_rx_ctor(rx_ptr, index);
		list_add_tail(&rx_ptr->qe, &rx_mod->rx_free_q);
		rx_mod->rx_free_count++;
	}

	/* build RX-path queue */
	for (index = 0; index < BFI_MAX_RXQ; index++) {
		rxp_ptr = &rx_mod->rxp[index];
		rxp_ptr->cq.cq_id = index;
		bfa_q_qe_init(&rxp_ptr->qe);
		list_add_tail(&rxp_ptr->qe, &rx_mod->rxp_free_q);
		rx_mod->rxp_free_count++;
	}

	/* build RXQ queue */
	for (index = 0; index < BFI_MAX_RXQ; index++) {
		rxq_ptr = &rx_mod->rxq[index];
		rxq_ptr->rxq_id = index;

		bfa_q_qe_init(&rxq_ptr->qe);
		list_add_tail(&rxq_ptr->qe, &rx_mod->rxq_free_q);
		rx_mod->rxq_free_count++;
	}

	rx_mod->rx_stop_wc.wc_resume = bna_rx_mod_cb_rx_stopped_all;
	rx_mod->rx_stop_wc.wc_cbarg = rx_mod;
	rx_mod->rx_stop_wc.wc_count = 0;
}

void
bna_rx_mod_uninit(struct bna_rx_mod *rx_mod)
{
	struct list_head		*qe;
	int i;

	i = 0;
	list_for_each(qe, &rx_mod->rx_free_q)
		i++;

	i = 0;
	list_for_each(qe, &rx_mod->rxp_free_q)
		i++;

	i = 0;
	list_for_each(qe, &rx_mod->rxq_free_q)
		i++;

	rx_mod->bna = NULL;
}

int
bna_rx_state_get(struct bna_rx *rx)
{
	return bfa_sm_to_state(rx_sm_table, rx->fsm);
}

void
bna_rx_res_req(struct bna_rx_config *q_cfg, struct bna_res_info *res_info)
{
	u32 cq_size, hq_size, dq_size;
	u32 cpage_count, hpage_count, dpage_count;
	struct bna_mem_info *mem_info;
	u32 cq_depth;
	u32 hq_depth;
	u32 dq_depth;

	dq_depth = q_cfg->q_depth;
	hq_depth = ((q_cfg->rxp_type == BNA_RXP_SINGLE) ? 0 : q_cfg->q_depth);
	cq_depth = dq_depth + hq_depth;

	BNA_TO_POWER_OF_2_HIGH(cq_depth);
	cq_size = cq_depth * BFI_CQ_WI_SIZE;
	cq_size = ALIGN(cq_size, PAGE_SIZE);
	cpage_count = SIZE_TO_PAGES(cq_size);

	BNA_TO_POWER_OF_2_HIGH(dq_depth);
	dq_size = dq_depth * BFI_RXQ_WI_SIZE;
	dq_size = ALIGN(dq_size, PAGE_SIZE);
	dpage_count = SIZE_TO_PAGES(dq_size);

	if (BNA_RXP_SINGLE != q_cfg->rxp_type) {
		BNA_TO_POWER_OF_2_HIGH(hq_depth);
		hq_size = hq_depth * BFI_RXQ_WI_SIZE;
		hq_size = ALIGN(hq_size, PAGE_SIZE);
		hpage_count = SIZE_TO_PAGES(hq_size);
	} else {
		hpage_count = 0;
	}

	/* CCB structures */
	res_info[BNA_RX_RES_MEM_T_CCB].res_type = BNA_RES_T_MEM;
	mem_info = &res_info[BNA_RX_RES_MEM_T_CCB].res_u.mem_info;
	mem_info->mem_type = BNA_MEM_T_KVA;
	mem_info->len = sizeof(struct bna_ccb);
	mem_info->num = q_cfg->num_paths;

	/* RCB structures */
	res_info[BNA_RX_RES_MEM_T_RCB].res_type = BNA_RES_T_MEM;
	mem_info = &res_info[BNA_RX_RES_MEM_T_RCB].res_u.mem_info;
	mem_info->mem_type = BNA_MEM_T_KVA;
	mem_info->len = sizeof(struct bna_rcb);
	mem_info->num = BNA_GET_RXQS(q_cfg);

	/* Completion QPT */
	res_info[BNA_RX_RES_MEM_T_CQPT].res_type = BNA_RES_T_MEM;
	mem_info = &res_info[BNA_RX_RES_MEM_T_CQPT].res_u.mem_info;
	mem_info->mem_type = BNA_MEM_T_DMA;
	mem_info->len = cpage_count * sizeof(struct bna_dma_addr);
	mem_info->num = q_cfg->num_paths;

	/* Completion s/w QPT */
	res_info[BNA_RX_RES_MEM_T_CSWQPT].res_type = BNA_RES_T_MEM;
	mem_info = &res_info[BNA_RX_RES_MEM_T_CSWQPT].res_u.mem_info;
	mem_info->mem_type = BNA_MEM_T_KVA;
	mem_info->len = cpage_count * sizeof(void *);
	mem_info->num = q_cfg->num_paths;

	/* Completion QPT pages */
	res_info[BNA_RX_RES_MEM_T_CQPT_PAGE].res_type = BNA_RES_T_MEM;
	mem_info = &res_info[BNA_RX_RES_MEM_T_CQPT_PAGE].res_u.mem_info;
	mem_info->mem_type = BNA_MEM_T_DMA;
	mem_info->len = PAGE_SIZE;
	mem_info->num = cpage_count * q_cfg->num_paths;

	/* Data QPTs */
	res_info[BNA_RX_RES_MEM_T_DQPT].res_type = BNA_RES_T_MEM;
	mem_info = &res_info[BNA_RX_RES_MEM_T_DQPT].res_u.mem_info;
	mem_info->mem_type = BNA_MEM_T_DMA;
	mem_info->len = dpage_count * sizeof(struct bna_dma_addr);
	mem_info->num = q_cfg->num_paths;

	/* Data s/w QPTs */
	res_info[BNA_RX_RES_MEM_T_DSWQPT].res_type = BNA_RES_T_MEM;
	mem_info = &res_info[BNA_RX_RES_MEM_T_DSWQPT].res_u.mem_info;
	mem_info->mem_type = BNA_MEM_T_KVA;
	mem_info->len = dpage_count * sizeof(void *);
	mem_info->num = q_cfg->num_paths;

	/* Data QPT pages */
	res_info[BNA_RX_RES_MEM_T_DPAGE].res_type = BNA_RES_T_MEM;
	mem_info = &res_info[BNA_RX_RES_MEM_T_DPAGE].res_u.mem_info;
	mem_info->mem_type = BNA_MEM_T_DMA;
	mem_info->len = PAGE_SIZE;
	mem_info->num = dpage_count * q_cfg->num_paths;

	/* Hdr QPTs */
	res_info[BNA_RX_RES_MEM_T_HQPT].res_type = BNA_RES_T_MEM;
	mem_info = &res_info[BNA_RX_RES_MEM_T_HQPT].res_u.mem_info;
	mem_info->mem_type = BNA_MEM_T_DMA;
	mem_info->len = hpage_count * sizeof(struct bna_dma_addr);
	mem_info->num = (hpage_count ? q_cfg->num_paths : 0);

	/* Hdr s/w QPTs */
	res_info[BNA_RX_RES_MEM_T_HSWQPT].res_type = BNA_RES_T_MEM;
	mem_info = &res_info[BNA_RX_RES_MEM_T_HSWQPT].res_u.mem_info;
	mem_info->mem_type = BNA_MEM_T_KVA;
	mem_info->len = hpage_count * sizeof(void *);
	mem_info->num = (hpage_count ? q_cfg->num_paths : 0);

	/* Hdr QPT pages */
	res_info[BNA_RX_RES_MEM_T_HPAGE].res_type = BNA_RES_T_MEM;
	mem_info = &res_info[BNA_RX_RES_MEM_T_HPAGE].res_u.mem_info;
	mem_info->mem_type = BNA_MEM_T_DMA;
	mem_info->len = (hpage_count ? PAGE_SIZE : 0);
	mem_info->num = (hpage_count ? (hpage_count * q_cfg->num_paths) : 0);

	/* RX Interrupts */
	res_info[BNA_RX_RES_T_INTR].res_type = BNA_RES_T_INTR;
	res_info[BNA_RX_RES_T_INTR].res_u.intr_info.intr_type = BNA_INTR_T_MSIX;
	res_info[BNA_RX_RES_T_INTR].res_u.intr_info.num = q_cfg->num_paths;
}

struct bna_rx *
bna_rx_create(struct bna *bna, struct bnad *bnad,
		struct bna_rx_config *rx_cfg,
		struct bna_rx_event_cbfn *rx_cbfn,
		struct bna_res_info *res_info,
		void *priv)
{
	struct bna_rx_mod *rx_mod = &bna->rx_mod;
	struct bna_rx *rx;
	struct bna_rxp *rxp;
	struct bna_rxq *q0;
	struct bna_rxq *q1;
	struct bna_intr_info *intr_info;
	u32 page_count;
	struct bna_mem_descr *ccb_mem;
	struct bna_mem_descr *rcb_mem;
	struct bna_mem_descr *unmapq_mem;
	struct bna_mem_descr *cqpt_mem;
	struct bna_mem_descr *cswqpt_mem;
	struct bna_mem_descr *cpage_mem;
	struct bna_mem_descr *hqpt_mem;	/* Header/Small Q qpt */
	struct bna_mem_descr *dqpt_mem;	/* Data/Large Q qpt */
	struct bna_mem_descr *hsqpt_mem;	/* s/w qpt for hdr */
	struct bna_mem_descr *dsqpt_mem;	/* s/w qpt for data */
	struct bna_mem_descr *hpage_mem;	/* hdr page mem */
	struct bna_mem_descr *dpage_mem;	/* data page mem */
	int i, cpage_idx = 0, dpage_idx = 0, hpage_idx = 0;
	int dpage_count, hpage_count, rcb_idx;
	struct bna_ib_config ibcfg;
	/* Fail if we don't have enough RXPs, RXQs */
	if (!_rx_can_satisfy(rx_mod, rx_cfg))
		return NULL;

	/* Initialize resource pointers */
	intr_info = &res_info[BNA_RX_RES_T_INTR].res_u.intr_info;
	ccb_mem = &res_info[BNA_RX_RES_MEM_T_CCB].res_u.mem_info.mdl[0];
	rcb_mem = &res_info[BNA_RX_RES_MEM_T_RCB].res_u.mem_info.mdl[0];
	unmapq_mem = &res_info[BNA_RX_RES_MEM_T_UNMAPQ].res_u.mem_info.mdl[0];
	cqpt_mem = &res_info[BNA_RX_RES_MEM_T_CQPT].res_u.mem_info.mdl[0];
	cswqpt_mem = &res_info[BNA_RX_RES_MEM_T_CSWQPT].res_u.mem_info.mdl[0];
	cpage_mem = &res_info[BNA_RX_RES_MEM_T_CQPT_PAGE].res_u.mem_info.mdl[0];
	hqpt_mem = &res_info[BNA_RX_RES_MEM_T_HQPT].res_u.mem_info.mdl[0];
	dqpt_mem = &res_info[BNA_RX_RES_MEM_T_DQPT].res_u.mem_info.mdl[0];
	hsqpt_mem = &res_info[BNA_RX_RES_MEM_T_HSWQPT].res_u.mem_info.mdl[0];
	dsqpt_mem = &res_info[BNA_RX_RES_MEM_T_DSWQPT].res_u.mem_info.mdl[0];
	hpage_mem = &res_info[BNA_RX_RES_MEM_T_HPAGE].res_u.mem_info.mdl[0];
	dpage_mem = &res_info[BNA_RX_RES_MEM_T_DPAGE].res_u.mem_info.mdl[0];

	/* Compute q depth & page count */
	page_count = res_info[BNA_RX_RES_MEM_T_CQPT_PAGE].res_u.mem_info.num /
			rx_cfg->num_paths;

	dpage_count = res_info[BNA_RX_RES_MEM_T_DPAGE].res_u.mem_info.num /
			rx_cfg->num_paths;

	hpage_count = res_info[BNA_RX_RES_MEM_T_HPAGE].res_u.mem_info.num /
			rx_cfg->num_paths;
	/* Get RX pointer */
	rx = _get_free_rx(rx_mod);
	_rx_init(rx, bna);
	rx->priv = priv;
	rx->type = rx_cfg->rx_type;

	rx->rcb_setup_cbfn = rx_cbfn->rcb_setup_cbfn;
	rx->rcb_destroy_cbfn = rx_cbfn->rcb_destroy_cbfn;
	rx->ccb_setup_cbfn = rx_cbfn->ccb_setup_cbfn;
	rx->ccb_destroy_cbfn = rx_cbfn->ccb_destroy_cbfn;
	/* Following callbacks are mandatory */
	rx->rx_cleanup_cbfn = rx_cbfn->rx_cleanup_cbfn;
	rx->rx_post_cbfn = rx_cbfn->rx_post_cbfn;

	if (rx->bna->rx_mod.flags & BNA_RX_MOD_F_PORT_STARTED) {
		switch (rx->type) {
		case BNA_RX_T_REGULAR:
			if (!(rx->bna->rx_mod.flags &
				BNA_RX_MOD_F_PORT_LOOPBACK))
				rx->rx_flags |= BNA_RX_F_PORT_ENABLED;
			break;
		case BNA_RX_T_LOOPBACK:
			if (rx->bna->rx_mod.flags & BNA_RX_MOD_F_PORT_LOOPBACK)
				rx->rx_flags |= BNA_RX_F_PORT_ENABLED;
			break;
		}
	}

	for (i = 0, rcb_idx = 0; i < rx_cfg->num_paths; i++) {
		rxp = _get_free_rxp(rx_mod);
		rxp->type = rx_cfg->rxp_type;
		rxp->rx = rx;
		rxp->cq.rx = rx;

		/* Get required RXQs, and queue them to rx-path */
		q0 = _get_free_rxq(rx_mod);
		if (BNA_RXP_SINGLE == rx_cfg->rxp_type)
			q1 = NULL;
		else
			q1 = _get_free_rxq(rx_mod);

		/* Initialize IB */
		if (1 == intr_info->num) {
			rxp->cq.ib = bna_ib_get(&bna->ib_mod,
					intr_info->intr_type,
					intr_info->idl[0].vector);
			rxp->vector = intr_info->idl[0].vector;
		} else {
			rxp->cq.ib = bna_ib_get(&bna->ib_mod,
					intr_info->intr_type,
					intr_info->idl[i].vector);

			/* Map the MSI-x vector used for this RXP */
			rxp->vector = intr_info->idl[i].vector;
		}

		rxp->cq.ib_seg_offset = bna_ib_reserve_idx(rxp->cq.ib);

		ibcfg.coalescing_timeo = BFI_RX_COALESCING_TIMEO;
		ibcfg.interpkt_count = BFI_RX_INTERPKT_COUNT;
		ibcfg.interpkt_timeo = BFI_RX_INTERPKT_TIMEO;
		ibcfg.ctrl_flags = BFI_IB_CF_INT_ENABLE;

		bna_ib_config(rxp->cq.ib, &ibcfg);

		/* Link rxqs to rxp */
		_rxp_add_rxqs(rxp, q0, q1);

		/* Link rxp to rx */
		_rx_add_rxp(rx, rxp);

		q0->rx = rx;
		q0->rxp = rxp;

		/* Initialize RCB for the large / data q */
		q0->rcb = (struct bna_rcb *) rcb_mem[rcb_idx].kva;
		RXQ_RCB_INIT(q0, rxp, rx_cfg->q_depth, bna, 0,
			(void *)unmapq_mem[rcb_idx].kva);
		rcb_idx++;
		(q0)->rx_packets = (q0)->rx_bytes = 0;
		(q0)->rx_packets_with_error = (q0)->rxbuf_alloc_failed = 0;

		/* Initialize RXQs */
		_rxq_qpt_init(q0, rxp, dpage_count, PAGE_SIZE,
			&dqpt_mem[i], &dsqpt_mem[i], &dpage_mem[dpage_idx]);
		q0->rcb->page_idx = dpage_idx;
		q0->rcb->page_count = dpage_count;
		dpage_idx += dpage_count;

		/* Call bnad to complete rcb setup */
		if (rx->rcb_setup_cbfn)
			rx->rcb_setup_cbfn(bnad, q0->rcb);

		if (q1) {
			q1->rx = rx;
			q1->rxp = rxp;

			q1->rcb = (struct bna_rcb *) rcb_mem[rcb_idx].kva;
			RXQ_RCB_INIT(q1, rxp, rx_cfg->q_depth, bna, 1,
				(void *)unmapq_mem[rcb_idx].kva);
			rcb_idx++;
			(q1)->buffer_size = (rx_cfg)->small_buff_size;
			(q1)->rx_packets = (q1)->rx_bytes = 0;
			(q1)->rx_packets_with_error =
				(q1)->rxbuf_alloc_failed = 0;

			_rxq_qpt_init(q1, rxp, hpage_count, PAGE_SIZE,
				&hqpt_mem[i], &hsqpt_mem[i],
				&hpage_mem[hpage_idx]);
			q1->rcb->page_idx = hpage_idx;
			q1->rcb->page_count = hpage_count;
			hpage_idx += hpage_count;

			/* Call bnad to complete rcb setup */
			if (rx->rcb_setup_cbfn)
				rx->rcb_setup_cbfn(bnad, q1->rcb);
		}
		/* Setup RXP::CQ */
		rxp->cq.ccb = (struct bna_ccb *) ccb_mem[i].kva;
		_rxp_cqpt_setup(rxp, page_count, PAGE_SIZE,
			&cqpt_mem[i], &cswqpt_mem[i], &cpage_mem[cpage_idx]);
		rxp->cq.ccb->page_idx = cpage_idx;
		rxp->cq.ccb->page_count = page_count;
		cpage_idx += page_count;

		rxp->cq.ccb->pkt_rate.small_pkt_cnt = 0;
		rxp->cq.ccb->pkt_rate.large_pkt_cnt = 0;

		rxp->cq.ccb->producer_index = 0;
		rxp->cq.ccb->q_depth =	rx_cfg->q_depth +
					((rx_cfg->rxp_type == BNA_RXP_SINGLE) ?
					0 : rx_cfg->q_depth);
		rxp->cq.ccb->i_dbell = &rxp->cq.ib->door_bell;
		rxp->cq.ccb->rcb[0] = q0->rcb;
		if (q1)
			rxp->cq.ccb->rcb[1] = q1->rcb;
		rxp->cq.ccb->cq = &rxp->cq;
		rxp->cq.ccb->bnad = bna->bnad;
		rxp->cq.ccb->hw_producer_index =
			((volatile u32 *)rxp->cq.ib->ib_seg_host_addr_kva +
				      (rxp->cq.ib_seg_offset * BFI_IBIDX_SIZE));
		*(rxp->cq.ccb->hw_producer_index) = 0;
		rxp->cq.ccb->intr_type = intr_info->intr_type;
		rxp->cq.ccb->intr_vector = (intr_info->num == 1) ?
						intr_info->idl[0].vector :
						intr_info->idl[i].vector;
		rxp->cq.ccb->rx_coalescing_timeo =
					rxp->cq.ib->ib_config.coalescing_timeo;
		rxp->cq.ccb->id = i;

		/* Call bnad to complete CCB setup */
		if (rx->ccb_setup_cbfn)
			rx->ccb_setup_cbfn(bnad, rxp->cq.ccb);

	} /* for each rx-path */

	bna_rxf_init(&rx->rxf, rx, rx_cfg);

	bfa_fsm_set_state(rx, bna_rx_sm_stopped);

	return rx;
}

void
bna_rx_destroy(struct bna_rx *rx)
{
	struct bna_rx_mod *rx_mod = &rx->bna->rx_mod;
	struct bna_ib_mod *ib_mod = &rx->bna->ib_mod;
	struct bna_rxq *q0 = NULL;
	struct bna_rxq *q1 = NULL;
	struct bna_rxp *rxp;
	struct list_head *qe;

	bna_rxf_uninit(&rx->rxf);

	while (!list_empty(&rx->rxp_q)) {
		bfa_q_deq(&rx->rxp_q, &rxp);
		GET_RXQS(rxp, q0, q1);
		/* Callback to bnad for destroying RCB */
		if (rx->rcb_destroy_cbfn)
			rx->rcb_destroy_cbfn(rx->bna->bnad, q0->rcb);
		q0->rcb = NULL;
		q0->rxp = NULL;
		q0->rx = NULL;
		_put_free_rxq(rx_mod, q0);
		if (q1) {
			/* Callback to bnad for destroying RCB */
			if (rx->rcb_destroy_cbfn)
				rx->rcb_destroy_cbfn(rx->bna->bnad, q1->rcb);
			q1->rcb = NULL;
			q1->rxp = NULL;
			q1->rx = NULL;
			_put_free_rxq(rx_mod, q1);
		}
		rxp->rxq.slr.large = NULL;
		rxp->rxq.slr.small = NULL;
		if (rxp->cq.ib) {
			if (rxp->cq.ib_seg_offset != 0xff)
				bna_ib_release_idx(rxp->cq.ib,
						rxp->cq.ib_seg_offset);
			bna_ib_put(ib_mod, rxp->cq.ib);
			rxp->cq.ib = NULL;
		}
		/* Callback to bnad for destroying CCB */
		if (rx->ccb_destroy_cbfn)
			rx->ccb_destroy_cbfn(rx->bna->bnad, rxp->cq.ccb);
		rxp->cq.ccb = NULL;
		rxp->rx = NULL;
		_put_free_rxp(rx_mod, rxp);
	}

	list_for_each(qe, &rx_mod->rx_active_q) {
		if (qe == &rx->qe) {
			list_del(&rx->qe);
			bfa_q_qe_init(&rx->qe);
			break;
		}
	}

	rx->bna = NULL;
	rx->priv = NULL;
	_put_free_rx(rx_mod, rx);
}

void
bna_rx_enable(struct bna_rx *rx)
{
	if (rx->fsm != (bfa_sm_t)bna_rx_sm_stopped)
		return;

	rx->rx_flags |= BNA_RX_F_ENABLE;
	if (rx->rx_flags & BNA_RX_F_PORT_ENABLED)
		bfa_fsm_send_event(rx, RX_E_START);
}

void
bna_rx_disable(struct bna_rx *rx, enum bna_cleanup_type type,
		void (*cbfn)(void *, struct bna_rx *,
				enum bna_cb_status))
{
	if (type == BNA_SOFT_CLEANUP) {
		/* h/w should not be accessed. Treat we're stopped */
		(*cbfn)(rx->bna->bnad, rx, BNA_CB_SUCCESS);
	} else {
		rx->stop_cbfn = cbfn;
		rx->stop_cbarg = rx->bna->bnad;

		rx->rx_flags &= ~BNA_RX_F_ENABLE;

		bfa_fsm_send_event(rx, RX_E_STOP);
	}
}

/**
 * TX
 */
#define call_tx_stop_cbfn(tx, status)\
do {\
	if ((tx)->stop_cbfn)\
		(tx)->stop_cbfn((tx)->stop_cbarg, (tx), status);\
	(tx)->stop_cbfn = NULL;\
	(tx)->stop_cbarg = NULL;\
} while (0)

#define call_tx_prio_change_cbfn(tx, status)\
do {\
	if ((tx)->prio_change_cbfn)\
		(tx)->prio_change_cbfn((tx)->bna->bnad, (tx), status);\
	(tx)->prio_change_cbfn = NULL;\
} while (0)

static void bna_tx_mod_cb_tx_stopped(void *tx_mod, struct bna_tx *tx,
					enum bna_cb_status status);
static void bna_tx_cb_txq_stopped(void *arg, int status);
static void bna_tx_cb_stats_cleared(void *arg, int status);
static void __bna_tx_stop(struct bna_tx *tx);
static void __bna_tx_start(struct bna_tx *tx);
static void __bna_txf_stat_clr(struct bna_tx *tx);

enum bna_tx_event {
	TX_E_START			= 1,
	TX_E_STOP			= 2,
	TX_E_FAIL			= 3,
	TX_E_TXQ_STOPPED		= 4,
	TX_E_PRIO_CHANGE		= 5,
	TX_E_STAT_CLEARED		= 6,
};

enum bna_tx_state {
	BNA_TX_STOPPED			= 1,
	BNA_TX_STARTED			= 2,
	BNA_TX_TXQ_STOP_WAIT		= 3,
	BNA_TX_PRIO_STOP_WAIT		= 4,
	BNA_TX_STAT_CLR_WAIT		= 5,
};

bfa_fsm_state_decl(bna_tx, stopped, struct bna_tx,
			enum bna_tx_event);
bfa_fsm_state_decl(bna_tx, started, struct bna_tx,
			enum bna_tx_event);
bfa_fsm_state_decl(bna_tx, txq_stop_wait, struct bna_tx,
			enum bna_tx_event);
bfa_fsm_state_decl(bna_tx, prio_stop_wait, struct bna_tx,
			enum bna_tx_event);
bfa_fsm_state_decl(bna_tx, stat_clr_wait, struct bna_tx,
			enum bna_tx_event);

static struct bfa_sm_table tx_sm_table[] = {
	{BFA_SM(bna_tx_sm_stopped), BNA_TX_STOPPED},
	{BFA_SM(bna_tx_sm_started), BNA_TX_STARTED},
	{BFA_SM(bna_tx_sm_txq_stop_wait), BNA_TX_TXQ_STOP_WAIT},
	{BFA_SM(bna_tx_sm_prio_stop_wait), BNA_TX_PRIO_STOP_WAIT},
	{BFA_SM(bna_tx_sm_stat_clr_wait), BNA_TX_STAT_CLR_WAIT},
};

static void
bna_tx_sm_stopped_entry(struct bna_tx *tx)
{
	struct bna_txq *txq;
	struct list_head		 *qe;

	list_for_each(qe, &tx->txq_q) {
		txq = (struct bna_txq *)qe;
		(tx->tx_cleanup_cbfn)(tx->bna->bnad, txq->tcb);
	}

	call_tx_stop_cbfn(tx, BNA_CB_SUCCESS);
}

static void
bna_tx_sm_stopped(struct bna_tx *tx, enum bna_tx_event event)
{
	switch (event) {
	case TX_E_START:
		bfa_fsm_set_state(tx, bna_tx_sm_started);
		break;

	case TX_E_STOP:
		bfa_fsm_set_state(tx, bna_tx_sm_stopped);
		break;

	case TX_E_FAIL:
		/* No-op */
		break;

	case TX_E_PRIO_CHANGE:
		call_tx_prio_change_cbfn(tx, BNA_CB_SUCCESS);
		break;

	case TX_E_TXQ_STOPPED:
		/**
		 * This event is received due to flushing of mbox when
		 * device fails
		 */
		/* No-op */
		break;

	default:
		bfa_sm_fault(tx->bna, event);
	}
}

static void
bna_tx_sm_started_entry(struct bna_tx *tx)
{
	struct bna_txq *txq;
	struct list_head		 *qe;

	__bna_tx_start(tx);

	/* Start IB */
	list_for_each(qe, &tx->txq_q) {
		txq = (struct bna_txq *)qe;
		bna_ib_ack(&txq->ib->door_bell, 0);
	}
}

static void
bna_tx_sm_started(struct bna_tx *tx, enum bna_tx_event event)
{
	struct bna_txq *txq;
	struct list_head		 *qe;

	switch (event) {
	case TX_E_STOP:
		bfa_fsm_set_state(tx, bna_tx_sm_txq_stop_wait);
		__bna_tx_stop(tx);
		break;

	case TX_E_FAIL:
		list_for_each(qe, &tx->txq_q) {
			txq = (struct bna_txq *)qe;
			bna_ib_fail(txq->ib);
			(tx->tx_stall_cbfn)(tx->bna->bnad, txq->tcb);
		}
		bfa_fsm_set_state(tx, bna_tx_sm_stopped);
		break;

	case TX_E_PRIO_CHANGE:
		bfa_fsm_set_state(tx, bna_tx_sm_prio_stop_wait);
		break;

	default:
		bfa_sm_fault(tx->bna, event);
	}
}

static void
bna_tx_sm_txq_stop_wait_entry(struct bna_tx *tx)
{
}

static void
bna_tx_sm_txq_stop_wait(struct bna_tx *tx, enum bna_tx_event event)
{
	struct bna_txq *txq;
	struct list_head		 *qe;

	switch (event) {
	case TX_E_FAIL:
		bfa_fsm_set_state(tx, bna_tx_sm_stopped);
		break;

	case TX_E_TXQ_STOPPED:
		list_for_each(qe, &tx->txq_q) {
			txq = (struct bna_txq *)qe;
			bna_ib_stop(txq->ib);
		}
		bfa_fsm_set_state(tx, bna_tx_sm_stat_clr_wait);
		break;

	case TX_E_PRIO_CHANGE:
		/* No-op */
		break;

	default:
		bfa_sm_fault(tx->bna, event);
	}
}

static void
bna_tx_sm_prio_stop_wait_entry(struct bna_tx *tx)
{
	__bna_tx_stop(tx);
}

static void
bna_tx_sm_prio_stop_wait(struct bna_tx *tx, enum bna_tx_event event)
{
	struct bna_txq *txq;
	struct list_head		 *qe;

	switch (event) {
	case TX_E_STOP:
		bfa_fsm_set_state(tx, bna_tx_sm_txq_stop_wait);
		break;

	case TX_E_FAIL:
		call_tx_prio_change_cbfn(tx, BNA_CB_FAIL);
		bfa_fsm_set_state(tx, bna_tx_sm_stopped);
		break;

	case TX_E_TXQ_STOPPED:
		list_for_each(qe, &tx->txq_q) {
			txq = (struct bna_txq *)qe;
			bna_ib_stop(txq->ib);
			(tx->tx_cleanup_cbfn)(tx->bna->bnad, txq->tcb);
		}
		call_tx_prio_change_cbfn(tx, BNA_CB_SUCCESS);
		bfa_fsm_set_state(tx, bna_tx_sm_started);
		break;

	case TX_E_PRIO_CHANGE:
		/* No-op */
		break;

	default:
		bfa_sm_fault(tx->bna, event);
	}
}

static void
bna_tx_sm_stat_clr_wait_entry(struct bna_tx *tx)
{
	__bna_txf_stat_clr(tx);
}

static void
bna_tx_sm_stat_clr_wait(struct bna_tx *tx, enum bna_tx_event event)
{
	switch (event) {
	case TX_E_FAIL:
	case TX_E_STAT_CLEARED:
		bfa_fsm_set_state(tx, bna_tx_sm_stopped);
		break;

	default:
		bfa_sm_fault(tx->bna, event);
	}
}

static void
__bna_txq_start(struct bna_tx *tx, struct bna_txq *txq)
{
	struct bna_rxtx_q_mem *q_mem;
	struct bna_txq_mem txq_cfg;
	struct bna_txq_mem *txq_mem;
	struct bna_dma_addr cur_q_addr;
	u32 pg_num;
	void __iomem *base_addr;
	unsigned long off;

	/* Fill out structure, to be subsequently written to hardware */
	txq_cfg.pg_tbl_addr_lo = txq->qpt.hw_qpt_ptr.lsb;
	txq_cfg.pg_tbl_addr_hi = txq->qpt.hw_qpt_ptr.msb;
	cur_q_addr = *((struct bna_dma_addr *)(txq->qpt.kv_qpt_ptr));
	txq_cfg.cur_q_entry_lo = cur_q_addr.lsb;
	txq_cfg.cur_q_entry_hi = cur_q_addr.msb;

	txq_cfg.pg_cnt_n_prd_ptr = (txq->qpt.page_count << 16) | 0x0;

	txq_cfg.entry_n_pg_size = ((u32)(BFI_TXQ_WI_SIZE >> 2) << 16) |
			(txq->qpt.page_size >> 2);
	txq_cfg.int_blk_n_cns_ptr = ((((u32)txq->ib_seg_offset) << 24) |
			((u32)(txq->ib->ib_id & 0xff) << 16) | 0x0);

	txq_cfg.cns_ptr2_n_q_state = BNA_Q_IDLE_STATE;
	txq_cfg.nxt_qid_n_fid_n_pri = (((tx->txf.txf_id & 0x3f) << 3) |
			(txq->priority & 0x7));
	txq_cfg.wvc_n_cquota_n_rquota =
			((((u32)BFI_TX_MAX_WRR_QUOTA & 0xfff) << 12) |
			(BFI_TX_MAX_WRR_QUOTA & 0xfff));

	/* Setup the page and write to H/W */

	pg_num = BNA_GET_PAGE_NUM(HQM0_BLK_PG_NUM + tx->bna->port_num,
			HQM_RXTX_Q_RAM_BASE_OFFSET);
	writel(pg_num, tx->bna->regs.page_addr);

	base_addr = BNA_GET_MEM_BASE_ADDR(tx->bna->pcidev.pci_bar_kva,
					HQM_RXTX_Q_RAM_BASE_OFFSET);
	q_mem = (struct bna_rxtx_q_mem *)0;
	txq_mem = &q_mem[txq->txq_id].txq;

	/*
	 * The following 4 lines, is a hack b'cos the H/W needs to read
	 * these DMA addresses as little endian
	 */

	off = (unsigned long)&txq_mem->pg_tbl_addr_lo;
	writel(htonl(txq_cfg.pg_tbl_addr_lo), base_addr + off);

	off = (unsigned long)&txq_mem->pg_tbl_addr_hi;
	writel(htonl(txq_cfg.pg_tbl_addr_hi), base_addr + off);

	off = (unsigned long)&txq_mem->cur_q_entry_lo;
	writel(htonl(txq_cfg.cur_q_entry_lo), base_addr + off);

	off = (unsigned long)&txq_mem->cur_q_entry_hi;
	writel(htonl(txq_cfg.cur_q_entry_hi), base_addr + off);

	off = (unsigned long)&txq_mem->pg_cnt_n_prd_ptr;
	writel(txq_cfg.pg_cnt_n_prd_ptr, base_addr + off);

	off = (unsigned long)&txq_mem->entry_n_pg_size;
	writel(txq_cfg.entry_n_pg_size, base_addr + off);

	off = (unsigned long)&txq_mem->int_blk_n_cns_ptr;
	writel(txq_cfg.int_blk_n_cns_ptr, base_addr + off);

	off = (unsigned long)&txq_mem->cns_ptr2_n_q_state;
	writel(txq_cfg.cns_ptr2_n_q_state, base_addr + off);

	off = (unsigned long)&txq_mem->nxt_qid_n_fid_n_pri;
	writel(txq_cfg.nxt_qid_n_fid_n_pri, base_addr + off);

	off = (unsigned long)&txq_mem->wvc_n_cquota_n_rquota;
	writel(txq_cfg.wvc_n_cquota_n_rquota, base_addr + off);

	txq->tcb->producer_index = 0;
	txq->tcb->consumer_index = 0;
	*(txq->tcb->hw_consumer_index) = 0;

}

static void
__bna_txq_stop(struct bna_tx *tx, struct bna_txq *txq)
{
	struct bfi_ll_q_stop_req ll_req;
	u32 bit_mask[2] = {0, 0};
	if (txq->txq_id < 32)
		bit_mask[0] = (u32)1 << txq->txq_id;
	else
		bit_mask[1] = (u32)1 << (txq->txq_id - 32);

	memset(&ll_req, 0, sizeof(ll_req));
	ll_req.mh.msg_class = BFI_MC_LL;
	ll_req.mh.msg_id = BFI_LL_H2I_TXQ_STOP_REQ;
	ll_req.mh.mtag.h2i.lpu_id = 0;
	ll_req.q_id_mask[0] = htonl(bit_mask[0]);
	ll_req.q_id_mask[1] = htonl(bit_mask[1]);

	bna_mbox_qe_fill(&tx->mbox_qe, &ll_req, sizeof(ll_req),
			bna_tx_cb_txq_stopped, tx);

	bna_mbox_send(tx->bna, &tx->mbox_qe);
}

static void
__bna_txf_start(struct bna_tx *tx)
{
	struct bna_tx_fndb_ram *tx_fndb;
	struct bna_txf *txf = &tx->txf;
	void __iomem *base_addr;
	unsigned long off;

	writel(BNA_GET_PAGE_NUM(LUT0_MEM_BLK_BASE_PG_NUM +
			(tx->bna->port_num * 2), TX_FNDB_RAM_BASE_OFFSET),
			tx->bna->regs.page_addr);

	base_addr = BNA_GET_MEM_BASE_ADDR(tx->bna->pcidev.pci_bar_kva,
					TX_FNDB_RAM_BASE_OFFSET);

	tx_fndb = (struct bna_tx_fndb_ram *)0;
	off = (unsigned long)&tx_fndb[txf->txf_id].vlan_n_ctrl_flags;

	writel(((u32)txf->vlan << 16) | txf->ctrl_flags,
			base_addr + off);

	if (tx->txf.txf_id < 32)
		tx->bna->tx_mod.txf_bmap[0] |= ((u32)1 << tx->txf.txf_id);
	else
		tx->bna->tx_mod.txf_bmap[1] |= ((u32)
						 1 << (tx->txf.txf_id - 32));
}

static void
__bna_txf_stop(struct bna_tx *tx)
{
	struct bna_tx_fndb_ram *tx_fndb;
	u32 page_num;
	u32 ctl_flags;
	struct bna_txf *txf = &tx->txf;
	void __iomem *base_addr;
	unsigned long off;

	/* retrieve the running txf_flags & turn off enable bit */
	page_num = BNA_GET_PAGE_NUM(LUT0_MEM_BLK_BASE_PG_NUM +
			(tx->bna->port_num * 2), TX_FNDB_RAM_BASE_OFFSET);
	writel(page_num, tx->bna->regs.page_addr);

	base_addr = BNA_GET_MEM_BASE_ADDR(tx->bna->pcidev.pci_bar_kva,
					TX_FNDB_RAM_BASE_OFFSET);
	tx_fndb = (struct bna_tx_fndb_ram *)0;
	off = (unsigned long)&tx_fndb[txf->txf_id].vlan_n_ctrl_flags;

	ctl_flags = readl(base_addr + off);
	ctl_flags &= ~BFI_TXF_CF_ENABLE;

	writel(ctl_flags, base_addr + off);

	if (tx->txf.txf_id < 32)
		tx->bna->tx_mod.txf_bmap[0] &= ~((u32)1 << tx->txf.txf_id);
	else
		tx->bna->tx_mod.txf_bmap[0] &= ~((u32)
						 1 << (tx->txf.txf_id - 32));
}

static void
__bna_txf_stat_clr(struct bna_tx *tx)
{
	struct bfi_ll_stats_req ll_req;
	u32 txf_bmap[2] = {0, 0};
	if (tx->txf.txf_id < 32)
		txf_bmap[0] = ((u32)1 << tx->txf.txf_id);
	else
		txf_bmap[1] = ((u32)1 << (tx->txf.txf_id - 32));
	bfi_h2i_set(ll_req.mh, BFI_MC_LL, BFI_LL_H2I_STATS_CLEAR_REQ, 0);
	ll_req.stats_mask = 0;
	ll_req.rxf_id_mask[0] = 0;
	ll_req.rxf_id_mask[1] =	0;
	ll_req.txf_id_mask[0] =	htonl(txf_bmap[0]);
	ll_req.txf_id_mask[1] =	htonl(txf_bmap[1]);

	bna_mbox_qe_fill(&tx->mbox_qe, &ll_req, sizeof(ll_req),
			bna_tx_cb_stats_cleared, tx);
	bna_mbox_send(tx->bna, &tx->mbox_qe);
}

static void
__bna_tx_start(struct bna_tx *tx)
{
	struct bna_txq *txq;
	struct list_head		 *qe;

	list_for_each(qe, &tx->txq_q) {
		txq = (struct bna_txq *)qe;
		bna_ib_start(txq->ib);
		__bna_txq_start(tx, txq);
	}

	__bna_txf_start(tx);

	list_for_each(qe, &tx->txq_q) {
		txq = (struct bna_txq *)qe;
		txq->tcb->priority = txq->priority;
		(tx->tx_resume_cbfn)(tx->bna->bnad, txq->tcb);
	}
}

static void
__bna_tx_stop(struct bna_tx *tx)
{
	struct bna_txq *txq;
	struct list_head		 *qe;

	list_for_each(qe, &tx->txq_q) {
		txq = (struct bna_txq *)qe;
		(tx->tx_stall_cbfn)(tx->bna->bnad, txq->tcb);
	}

	__bna_txf_stop(tx);

	list_for_each(qe, &tx->txq_q) {
		txq = (struct bna_txq *)qe;
		bfa_wc_up(&tx->txq_stop_wc);
	}

	list_for_each(qe, &tx->txq_q) {
		txq = (struct bna_txq *)qe;
		__bna_txq_stop(tx, txq);
	}
}

static void
bna_txq_qpt_setup(struct bna_txq *txq, int page_count, int page_size,
		struct bna_mem_descr *qpt_mem,
		struct bna_mem_descr *swqpt_mem,
		struct bna_mem_descr *page_mem)
{
	int i;

	txq->qpt.hw_qpt_ptr.lsb = qpt_mem->dma.lsb;
	txq->qpt.hw_qpt_ptr.msb = qpt_mem->dma.msb;
	txq->qpt.kv_qpt_ptr = qpt_mem->kva;
	txq->qpt.page_count = page_count;
	txq->qpt.page_size = page_size;

	txq->tcb->sw_qpt = (void **) swqpt_mem->kva;

	for (i = 0; i < page_count; i++) {
		txq->tcb->sw_qpt[i] = page_mem[i].kva;

		((struct bna_dma_addr *)txq->qpt.kv_qpt_ptr)[i].lsb =
			page_mem[i].dma.lsb;
		((struct bna_dma_addr *)txq->qpt.kv_qpt_ptr)[i].msb =
			page_mem[i].dma.msb;

	}
}

static void
bna_tx_free(struct bna_tx *tx)
{
	struct bna_tx_mod *tx_mod = &tx->bna->tx_mod;
	struct bna_txq *txq;
	struct bna_ib_mod *ib_mod = &tx->bna->ib_mod;
	struct list_head *qe;

	while (!list_empty(&tx->txq_q)) {
		bfa_q_deq(&tx->txq_q, &txq);
		bfa_q_qe_init(&txq->qe);
		if (txq->ib) {
			if (txq->ib_seg_offset != -1)
				bna_ib_release_idx(txq->ib,
						txq->ib_seg_offset);
			bna_ib_put(ib_mod, txq->ib);
			txq->ib = NULL;
		}
		txq->tcb = NULL;
		txq->tx = NULL;
		list_add_tail(&txq->qe, &tx_mod->txq_free_q);
	}

	list_for_each(qe, &tx_mod->tx_active_q) {
		if (qe == &tx->qe) {
			list_del(&tx->qe);
			bfa_q_qe_init(&tx->qe);
			break;
		}
	}

	tx->bna = NULL;
	tx->priv = NULL;
	list_add_tail(&tx->qe, &tx_mod->tx_free_q);
}

static void
bna_tx_cb_txq_stopped(void *arg, int status)
{
	struct bna_tx *tx = (struct bna_tx *)arg;

	bfa_q_qe_init(&tx->mbox_qe.qe);
	bfa_wc_down(&tx->txq_stop_wc);
}

static void
bna_tx_cb_txq_stopped_all(void *arg)
{
	struct bna_tx *tx = (struct bna_tx *)arg;

	bfa_fsm_send_event(tx, TX_E_TXQ_STOPPED);
}

static void
bna_tx_cb_stats_cleared(void *arg, int status)
{
	struct bna_tx *tx = (struct bna_tx *)arg;

	bfa_q_qe_init(&tx->mbox_qe.qe);

	bfa_fsm_send_event(tx, TX_E_STAT_CLEARED);
}

static void
bna_tx_start(struct bna_tx *tx)
{
	tx->flags |= BNA_TX_F_PORT_STARTED;
	if (tx->flags & BNA_TX_F_ENABLED)
		bfa_fsm_send_event(tx, TX_E_START);
}

static void
bna_tx_stop(struct bna_tx *tx)
{
	tx->stop_cbfn = bna_tx_mod_cb_tx_stopped;
	tx->stop_cbarg = &tx->bna->tx_mod;

	tx->flags &= ~BNA_TX_F_PORT_STARTED;
	bfa_fsm_send_event(tx, TX_E_STOP);
}

static void
bna_tx_fail(struct bna_tx *tx)
{
	tx->flags &= ~BNA_TX_F_PORT_STARTED;
	bfa_fsm_send_event(tx, TX_E_FAIL);
}

static void
bna_tx_prio_changed(struct bna_tx *tx, int prio)
{
	struct bna_txq *txq;
	struct list_head		 *qe;

	list_for_each(qe, &tx->txq_q) {
		txq = (struct bna_txq *)qe;
		txq->priority = prio;
	}

	bfa_fsm_send_event(tx, TX_E_PRIO_CHANGE);
}

static void
bna_tx_cee_link_status(struct bna_tx *tx, int cee_link)
{
	if (cee_link)
		tx->flags |= BNA_TX_F_PRIO_LOCK;
	else
		tx->flags &= ~BNA_TX_F_PRIO_LOCK;
}

static void
bna_tx_mod_cb_tx_stopped(void *arg, struct bna_tx *tx,
			enum bna_cb_status status)
{
	struct bna_tx_mod *tx_mod = (struct bna_tx_mod *)arg;

	bfa_wc_down(&tx_mod->tx_stop_wc);
}

static void
bna_tx_mod_cb_tx_stopped_all(void *arg)
{
	struct bna_tx_mod *tx_mod = (struct bna_tx_mod *)arg;

	if (tx_mod->stop_cbfn)
		tx_mod->stop_cbfn(&tx_mod->bna->port, BNA_CB_SUCCESS);
	tx_mod->stop_cbfn = NULL;
}

void
bna_tx_res_req(int num_txq, int txq_depth, struct bna_res_info *res_info)
{
	u32 q_size;
	u32 page_count;
	struct bna_mem_info *mem_info;

	res_info[BNA_TX_RES_MEM_T_TCB].res_type = BNA_RES_T_MEM;
	mem_info = &res_info[BNA_TX_RES_MEM_T_TCB].res_u.mem_info;
	mem_info->mem_type = BNA_MEM_T_KVA;
	mem_info->len = sizeof(struct bna_tcb);
	mem_info->num = num_txq;

	q_size = txq_depth * BFI_TXQ_WI_SIZE;
	q_size = ALIGN(q_size, PAGE_SIZE);
	page_count = q_size >> PAGE_SHIFT;

	res_info[BNA_TX_RES_MEM_T_QPT].res_type = BNA_RES_T_MEM;
	mem_info = &res_info[BNA_TX_RES_MEM_T_QPT].res_u.mem_info;
	mem_info->mem_type = BNA_MEM_T_DMA;
	mem_info->len = page_count * sizeof(struct bna_dma_addr);
	mem_info->num = num_txq;

	res_info[BNA_TX_RES_MEM_T_SWQPT].res_type = BNA_RES_T_MEM;
	mem_info = &res_info[BNA_TX_RES_MEM_T_SWQPT].res_u.mem_info;
	mem_info->mem_type = BNA_MEM_T_KVA;
	mem_info->len = page_count * sizeof(void *);
	mem_info->num = num_txq;

	res_info[BNA_TX_RES_MEM_T_PAGE].res_type = BNA_RES_T_MEM;
	mem_info = &res_info[BNA_TX_RES_MEM_T_PAGE].res_u.mem_info;
	mem_info->mem_type = BNA_MEM_T_DMA;
	mem_info->len = PAGE_SIZE;
	mem_info->num = num_txq * page_count;

	res_info[BNA_TX_RES_INTR_T_TXCMPL].res_type = BNA_RES_T_INTR;
	res_info[BNA_TX_RES_INTR_T_TXCMPL].res_u.intr_info.intr_type =
			BNA_INTR_T_MSIX;
	res_info[BNA_TX_RES_INTR_T_TXCMPL].res_u.intr_info.num = num_txq;
}

struct bna_tx *
bna_tx_create(struct bna *bna, struct bnad *bnad,
		struct bna_tx_config *tx_cfg,
		struct bna_tx_event_cbfn *tx_cbfn,
		struct bna_res_info *res_info, void *priv)
{
	struct bna_intr_info *intr_info;
	struct bna_tx_mod *tx_mod = &bna->tx_mod;
	struct bna_tx *tx;
	struct bna_txq *txq;
	struct list_head *qe;
	struct bna_ib_mod *ib_mod = &bna->ib_mod;
	struct bna_doorbell_qset *qset;
	struct bna_ib_config ib_config;
	int page_count;
	int page_size;
	int page_idx;
	int i;
	unsigned long off;

	intr_info = &res_info[BNA_TX_RES_INTR_T_TXCMPL].res_u.intr_info;
	page_count = (res_info[BNA_TX_RES_MEM_T_PAGE].res_u.mem_info.num) /
			tx_cfg->num_txq;
	page_size = res_info[BNA_TX_RES_MEM_T_PAGE].res_u.mem_info.len;

	/**
	 * Get resources
	 */

	if ((intr_info->num != 1) && (intr_info->num != tx_cfg->num_txq))
		return NULL;

	/* Tx */

	if (list_empty(&tx_mod->tx_free_q))
		return NULL;
	bfa_q_deq(&tx_mod->tx_free_q, &tx);
	bfa_q_qe_init(&tx->qe);

	/* TxQs */

	INIT_LIST_HEAD(&tx->txq_q);
	for (i = 0; i < tx_cfg->num_txq; i++) {
		if (list_empty(&tx_mod->txq_free_q))
			goto err_return;

		bfa_q_deq(&tx_mod->txq_free_q, &txq);
		bfa_q_qe_init(&txq->qe);
		list_add_tail(&txq->qe, &tx->txq_q);
		txq->ib = NULL;
		txq->ib_seg_offset = -1;
		txq->tx = tx;
	}

	/* IBs */
	i = 0;
	list_for_each(qe, &tx->txq_q) {
		txq = (struct bna_txq *)qe;

		if (intr_info->num == 1)
			txq->ib = bna_ib_get(ib_mod, intr_info->intr_type,
						intr_info->idl[0].vector);
		else
			txq->ib = bna_ib_get(ib_mod, intr_info->intr_type,
						intr_info->idl[i].vector);

		if (txq->ib == NULL)
			goto err_return;

		txq->ib_seg_offset = bna_ib_reserve_idx(txq->ib);
		if (txq->ib_seg_offset == -1)
			goto err_return;

		i++;
	}

	/*
	 * Initialize
	 */

	/* Tx */

	tx->tcb_setup_cbfn = tx_cbfn->tcb_setup_cbfn;
	tx->tcb_destroy_cbfn = tx_cbfn->tcb_destroy_cbfn;
	/* Following callbacks are mandatory */
	tx->tx_stall_cbfn = tx_cbfn->tx_stall_cbfn;
	tx->tx_resume_cbfn = tx_cbfn->tx_resume_cbfn;
	tx->tx_cleanup_cbfn = tx_cbfn->tx_cleanup_cbfn;

	list_add_tail(&tx->qe, &tx_mod->tx_active_q);
	tx->bna = bna;
	tx->priv = priv;
	tx->txq_stop_wc.wc_resume = bna_tx_cb_txq_stopped_all;
	tx->txq_stop_wc.wc_cbarg = tx;
	tx->txq_stop_wc.wc_count = 0;

	tx->type = tx_cfg->tx_type;

	tx->flags = 0;
	if (tx->bna->tx_mod.flags & BNA_TX_MOD_F_PORT_STARTED) {
		switch (tx->type) {
		case BNA_TX_T_REGULAR:
			if (!(tx->bna->tx_mod.flags &
				BNA_TX_MOD_F_PORT_LOOPBACK))
				tx->flags |= BNA_TX_F_PORT_STARTED;
			break;
		case BNA_TX_T_LOOPBACK:
			if (tx->bna->tx_mod.flags & BNA_TX_MOD_F_PORT_LOOPBACK)
				tx->flags |= BNA_TX_F_PORT_STARTED;
			break;
		}
	}
	if (tx->bna->tx_mod.cee_link)
		tx->flags |= BNA_TX_F_PRIO_LOCK;

	/* TxQ */

	i = 0;
	page_idx = 0;
	list_for_each(qe, &tx->txq_q) {
		txq = (struct bna_txq *)qe;
		txq->priority = tx_mod->priority;
		txq->tcb = (struct bna_tcb *)
		  res_info[BNA_TX_RES_MEM_T_TCB].res_u.mem_info.mdl[i].kva;
		txq->tx_packets = 0;
		txq->tx_bytes = 0;

		/* IB */

		ib_config.coalescing_timeo = BFI_TX_COALESCING_TIMEO;
		ib_config.interpkt_timeo = 0; /* Not used */
		ib_config.interpkt_count = BFI_TX_INTERPKT_COUNT;
		ib_config.ctrl_flags = (BFI_IB_CF_INTER_PKT_DMA |
					BFI_IB_CF_INT_ENABLE |
					BFI_IB_CF_COALESCING_MODE);
		bna_ib_config(txq->ib, &ib_config);

		/* TCB */

		txq->tcb->producer_index = 0;
		txq->tcb->consumer_index = 0;
		txq->tcb->hw_consumer_index = (volatile u32 *)
			((volatile u8 *)txq->ib->ib_seg_host_addr_kva +
			 (txq->ib_seg_offset * BFI_IBIDX_SIZE));
		*(txq->tcb->hw_consumer_index) = 0;
		txq->tcb->q_depth = tx_cfg->txq_depth;
		txq->tcb->unmap_q = (void *)
		res_info[BNA_TX_RES_MEM_T_UNMAPQ].res_u.mem_info.mdl[i].kva;
		qset = (struct bna_doorbell_qset *)0;
		off = (unsigned long)&qset[txq->txq_id].txq[0];
		txq->tcb->q_dbell = off +
			BNA_GET_DOORBELL_BASE_ADDR(bna->pcidev.pci_bar_kva);
		txq->tcb->i_dbell = &txq->ib->door_bell;
		txq->tcb->intr_type = intr_info->intr_type;
		txq->tcb->intr_vector = (intr_info->num == 1) ?
					intr_info->idl[0].vector :
					intr_info->idl[i].vector;
		txq->tcb->txq = txq;
		txq->tcb->bnad = bnad;
		txq->tcb->id = i;

		/* QPT, SWQPT, Pages */
		bna_txq_qpt_setup(txq, page_count, page_size,
			&res_info[BNA_TX_RES_MEM_T_QPT].res_u.mem_info.mdl[i],
			&res_info[BNA_TX_RES_MEM_T_SWQPT].res_u.mem_info.mdl[i],
			&res_info[BNA_TX_RES_MEM_T_PAGE].
				  res_u.mem_info.mdl[page_idx]);
		txq->tcb->page_idx = page_idx;
		txq->tcb->page_count = page_count;
		page_idx += page_count;

		/* Callback to bnad for setting up TCB */
		if (tx->tcb_setup_cbfn)
			(tx->tcb_setup_cbfn)(bna->bnad, txq->tcb);

		i++;
	}

	/* TxF */

	tx->txf.ctrl_flags = BFI_TXF_CF_ENABLE | BFI_TXF_CF_VLAN_WI_BASED;
	tx->txf.vlan = 0;

	/* Mbox element */
	bfa_q_qe_init(&tx->mbox_qe.qe);

	bfa_fsm_set_state(tx, bna_tx_sm_stopped);

	return tx;

err_return:
	bna_tx_free(tx);
	return NULL;
}

void
bna_tx_destroy(struct bna_tx *tx)
{
	/* Callback to bnad for destroying TCB */
	if (tx->tcb_destroy_cbfn) {
		struct bna_txq *txq;
		struct list_head *qe;

		list_for_each(qe, &tx->txq_q) {
			txq = (struct bna_txq *)qe;
			(tx->tcb_destroy_cbfn)(tx->bna->bnad, txq->tcb);
		}
	}

	bna_tx_free(tx);
}

void
bna_tx_enable(struct bna_tx *tx)
{
	if (tx->fsm != (bfa_sm_t)bna_tx_sm_stopped)
		return;

	tx->flags |= BNA_TX_F_ENABLED;

	if (tx->flags & BNA_TX_F_PORT_STARTED)
		bfa_fsm_send_event(tx, TX_E_START);
}

void
bna_tx_disable(struct bna_tx *tx, enum bna_cleanup_type type,
		void (*cbfn)(void *, struct bna_tx *, enum bna_cb_status))
{
	if (type == BNA_SOFT_CLEANUP) {
		(*cbfn)(tx->bna->bnad, tx, BNA_CB_SUCCESS);
		return;
	}

	tx->stop_cbfn = cbfn;
	tx->stop_cbarg = tx->bna->bnad;

	tx->flags &= ~BNA_TX_F_ENABLED;

	bfa_fsm_send_event(tx, TX_E_STOP);
}

int
bna_tx_state_get(struct bna_tx *tx)
{
	return bfa_sm_to_state(tx_sm_table, tx->fsm);
}

void
bna_tx_mod_init(struct bna_tx_mod *tx_mod, struct bna *bna,
		struct bna_res_info *res_info)
{
	int i;

	tx_mod->bna = bna;
	tx_mod->flags = 0;

	tx_mod->tx = (struct bna_tx *)
		res_info[BNA_RES_MEM_T_TX_ARRAY].res_u.mem_info.mdl[0].kva;
	tx_mod->txq = (struct bna_txq *)
		res_info[BNA_RES_MEM_T_TXQ_ARRAY].res_u.mem_info.mdl[0].kva;

	INIT_LIST_HEAD(&tx_mod->tx_free_q);
	INIT_LIST_HEAD(&tx_mod->tx_active_q);

	INIT_LIST_HEAD(&tx_mod->txq_free_q);

	for (i = 0; i < BFI_MAX_TXQ; i++) {
		tx_mod->tx[i].txf.txf_id = i;
		bfa_q_qe_init(&tx_mod->tx[i].qe);
		list_add_tail(&tx_mod->tx[i].qe, &tx_mod->tx_free_q);

		tx_mod->txq[i].txq_id = i;
		bfa_q_qe_init(&tx_mod->txq[i].qe);
		list_add_tail(&tx_mod->txq[i].qe, &tx_mod->txq_free_q);
	}

	tx_mod->tx_stop_wc.wc_resume = bna_tx_mod_cb_tx_stopped_all;
	tx_mod->tx_stop_wc.wc_cbarg = tx_mod;
	tx_mod->tx_stop_wc.wc_count = 0;
}

void
bna_tx_mod_uninit(struct bna_tx_mod *tx_mod)
{
	struct list_head		*qe;
	int i;

	i = 0;
	list_for_each(qe, &tx_mod->tx_free_q)
		i++;

	i = 0;
	list_for_each(qe, &tx_mod->txq_free_q)
		i++;

	tx_mod->bna = NULL;
}

void
bna_tx_mod_start(struct bna_tx_mod *tx_mod, enum bna_tx_type type)
{
	struct bna_tx *tx;
	struct list_head		*qe;

	tx_mod->flags |= BNA_TX_MOD_F_PORT_STARTED;
	if (type == BNA_TX_T_LOOPBACK)
		tx_mod->flags |= BNA_TX_MOD_F_PORT_LOOPBACK;

	list_for_each(qe, &tx_mod->tx_active_q) {
		tx = (struct bna_tx *)qe;
		if (tx->type == type)
			bna_tx_start(tx);
	}
}

void
bna_tx_mod_stop(struct bna_tx_mod *tx_mod, enum bna_tx_type type)
{
	struct bna_tx *tx;
	struct list_head		*qe;

	tx_mod->flags &= ~BNA_TX_MOD_F_PORT_STARTED;
	tx_mod->flags &= ~BNA_TX_MOD_F_PORT_LOOPBACK;

	tx_mod->stop_cbfn = bna_port_cb_tx_stopped;

	/**
	 * Before calling bna_tx_stop(), increment tx_stop_wc as many times
	 * as we are going to call bna_tx_stop
	 */
	list_for_each(qe, &tx_mod->tx_active_q) {
		tx = (struct bna_tx *)qe;
		if (tx->type == type)
			bfa_wc_up(&tx_mod->tx_stop_wc);
	}

	if (tx_mod->tx_stop_wc.wc_count == 0) {
		tx_mod->stop_cbfn(&tx_mod->bna->port, BNA_CB_SUCCESS);
		tx_mod->stop_cbfn = NULL;
		return;
	}

	list_for_each(qe, &tx_mod->tx_active_q) {
		tx = (struct bna_tx *)qe;
		if (tx->type == type)
			bna_tx_stop(tx);
	}
}

void
bna_tx_mod_fail(struct bna_tx_mod *tx_mod)
{
	struct bna_tx *tx;
	struct list_head		*qe;

	tx_mod->flags &= ~BNA_TX_MOD_F_PORT_STARTED;
	tx_mod->flags &= ~BNA_TX_MOD_F_PORT_LOOPBACK;

	list_for_each(qe, &tx_mod->tx_active_q) {
		tx = (struct bna_tx *)qe;
		bna_tx_fail(tx);
	}
}

void
bna_tx_mod_prio_changed(struct bna_tx_mod *tx_mod, int prio)
{
	struct bna_tx *tx;
	struct list_head		*qe;

	if (prio != tx_mod->priority) {
		tx_mod->priority = prio;

		list_for_each(qe, &tx_mod->tx_active_q) {
			tx = (struct bna_tx *)qe;
			bna_tx_prio_changed(tx, prio);
		}
	}
}

void
bna_tx_mod_cee_link_status(struct bna_tx_mod *tx_mod, int cee_link)
{
	struct bna_tx *tx;
	struct list_head		*qe;

	tx_mod->cee_link = cee_link;

	list_for_each(qe, &tx_mod->tx_active_q) {
		tx = (struct bna_tx *)qe;
		bna_tx_cee_link_status(tx, cee_link);
	}
}
