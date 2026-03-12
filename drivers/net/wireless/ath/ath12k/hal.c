// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include <linux/dma-mapping.h>
#include "debug.h"
#include "hif.h"

static void ath12k_hal_ce_dst_setup(struct ath12k_base *ab,
				    struct hal_srng *srng, int ring_num)
{
	ab->hal.ops->ce_dst_setup(ab, srng, ring_num);
}

static void ath12k_hal_srng_src_hw_init(struct ath12k_base *ab,
					struct hal_srng *srng)
{
	ab->hal.ops->srng_src_hw_init(ab, srng);
}

static void ath12k_hal_srng_dst_hw_init(struct ath12k_base *ab,
					struct hal_srng *srng)
{
	ab->hal.ops->srng_dst_hw_init(ab, srng);
}

static void ath12k_hal_set_umac_srng_ptr_addr(struct ath12k_base *ab,
					      struct hal_srng *srng)
{
	ab->hal.ops->set_umac_srng_ptr_addr(ab, srng);
}

static int ath12k_hal_srng_get_ring_id(struct ath12k_hal *hal,
				       enum hal_ring_type type,
				       int ring_num, int mac_id)
{
	return hal->ops->srng_get_ring_id(hal, type, ring_num, mac_id);
}

int ath12k_hal_srng_update_shadow_config(struct ath12k_base *ab,
					 enum hal_ring_type ring_type,
					 int ring_num)
{
	return ab->hal.ops->srng_update_shadow_config(ab, ring_type,
							  ring_num);
}

u32 ath12k_hal_ce_get_desc_size(struct ath12k_hal *hal, enum hal_ce_desc type)
{
	return hal->ops->ce_get_desc_size(type);
}

void ath12k_hal_tx_set_dscp_tid_map(struct ath12k_base *ab, int id)
{
	ab->hal.ops->tx_set_dscp_tid_map(ab, id);
}

void ath12k_hal_tx_configure_bank_register(struct ath12k_base *ab,
					   u32 bank_config, u8 bank_id)
{
	ab->hal.ops->tx_configure_bank_register(ab, bank_config, bank_id);
}

void ath12k_hal_reoq_lut_addr_read_enable(struct ath12k_base *ab)
{
	ab->hal.ops->reoq_lut_addr_read_enable(ab);
}

void ath12k_hal_reoq_lut_set_max_peerid(struct ath12k_base *ab)
{
	ab->hal.ops->reoq_lut_set_max_peerid(ab);
}

void ath12k_hal_write_ml_reoq_lut_addr(struct ath12k_base *ab, dma_addr_t paddr)
{
	ab->hal.ops->write_ml_reoq_lut_addr(ab, paddr);
}

void ath12k_hal_write_reoq_lut_addr(struct ath12k_base *ab, dma_addr_t paddr)
{
	ab->hal.ops->write_reoq_lut_addr(ab, paddr);
}

void ath12k_hal_setup_link_idle_list(struct ath12k_base *ab,
				     struct hal_wbm_idle_scatter_list *sbuf,
				     u32 nsbufs, u32 tot_link_desc,
				     u32 end_offset)
{
	ab->hal.ops->setup_link_idle_list(ab, sbuf, nsbufs, tot_link_desc,
					      end_offset);
}

void ath12k_hal_reo_hw_setup(struct ath12k_base *ab, u32 ring_hash_map)
{
	ab->hal.ops->reo_hw_setup(ab, ring_hash_map);
}

void ath12k_hal_reo_init_cmd_ring(struct ath12k_base *ab, struct hal_srng *srng)
{
	ab->hal.ops->reo_init_cmd_ring(ab, srng);
}

void ath12k_hal_reo_shared_qaddr_cache_clear(struct ath12k_base *ab)
{
	ab->hal.ops->reo_shared_qaddr_cache_clear(ab);
}
EXPORT_SYMBOL(ath12k_hal_reo_shared_qaddr_cache_clear);

void ath12k_hal_rx_buf_addr_info_set(struct ath12k_hal *hal,
				     struct ath12k_buffer_addr *binfo,
				     dma_addr_t paddr, u32 cookie, u8 manager)
{
	hal->ops->rx_buf_addr_info_set(binfo, paddr, cookie, manager);
}

void ath12k_hal_rx_buf_addr_info_get(struct ath12k_hal *hal,
				     struct ath12k_buffer_addr *binfo,
				     dma_addr_t *paddr, u32 *msdu_cookies,
				     u8 *rbm)
{
	hal->ops->rx_buf_addr_info_get(binfo, paddr, msdu_cookies, rbm);
}

void ath12k_hal_rx_msdu_list_get(struct ath12k_hal *hal, struct ath12k *ar,
				 void *link_desc,
				 void *msdu_list,
				 u16 *num_msdus)
{
	hal->ops->rx_msdu_list_get(ar, link_desc, msdu_list, num_msdus);
}

void ath12k_hal_rx_reo_ent_buf_paddr_get(struct ath12k_hal *hal, void *rx_desc,
					 dma_addr_t *paddr,
					 u32 *sw_cookie,
					 struct ath12k_buffer_addr **pp_buf_addr,
					 u8 *rbm, u32 *msdu_cnt)
{
	hal->ops->rx_reo_ent_buf_paddr_get(rx_desc, paddr, sw_cookie,
					   pp_buf_addr, rbm, msdu_cnt);
}

void ath12k_hal_cc_config(struct ath12k_base *ab)
{
	ab->hal.ops->cc_config(ab);
}

enum hal_rx_buf_return_buf_manager
ath12k_hal_get_idle_link_rbm(struct ath12k_hal *hal, u8 device_id)
{
	return hal->ops->get_idle_link_rbm(hal, device_id);
}

static int ath12k_hal_alloc_cont_rdp(struct ath12k_hal *hal)
{
	size_t size;

	size = sizeof(u32) * HAL_SRNG_RING_ID_MAX;
	hal->rdp.vaddr = dma_alloc_coherent(hal->dev, size, &hal->rdp.paddr,
					    GFP_KERNEL);
	if (!hal->rdp.vaddr)
		return -ENOMEM;

	return 0;
}

static void ath12k_hal_free_cont_rdp(struct ath12k_hal *hal)
{
	size_t size;

	if (!hal->rdp.vaddr)
		return;

	size = sizeof(u32) * HAL_SRNG_RING_ID_MAX;
	dma_free_coherent(hal->dev, size,
			  hal->rdp.vaddr, hal->rdp.paddr);
	hal->rdp.vaddr = NULL;
}

static int ath12k_hal_alloc_cont_wrp(struct ath12k_hal *hal)
{
	size_t size;

	size = sizeof(u32) * (HAL_SRNG_NUM_PMAC_RINGS + HAL_SRNG_NUM_DMAC_RINGS);
	hal->wrp.vaddr = dma_alloc_coherent(hal->dev, size, &hal->wrp.paddr,
					    GFP_KERNEL);
	if (!hal->wrp.vaddr)
		return -ENOMEM;

	return 0;
}

static void ath12k_hal_free_cont_wrp(struct ath12k_hal *hal)
{
	size_t size;

	if (!hal->wrp.vaddr)
		return;

	size = sizeof(u32) * (HAL_SRNG_NUM_PMAC_RINGS + HAL_SRNG_NUM_DMAC_RINGS);
	dma_free_coherent(hal->dev, size,
			  hal->wrp.vaddr, hal->wrp.paddr);
	hal->wrp.vaddr = NULL;
}

static void ath12k_hal_srng_hw_init(struct ath12k_base *ab,
				    struct hal_srng *srng)
{
	if (srng->ring_dir == HAL_SRNG_DIR_SRC)
		ath12k_hal_srng_src_hw_init(ab, srng);
	else
		ath12k_hal_srng_dst_hw_init(ab, srng);
}

int ath12k_hal_srng_get_entrysize(struct ath12k_base *ab, u32 ring_type)
{
	struct hal_srng_config *srng_config;

	if (WARN_ON(ring_type >= HAL_MAX_RING_TYPES))
		return -EINVAL;

	srng_config = &ab->hal.srng_config[ring_type];

	return (srng_config->entry_size << 2);
}
EXPORT_SYMBOL(ath12k_hal_srng_get_entrysize);

int ath12k_hal_srng_get_max_entries(struct ath12k_base *ab, u32 ring_type)
{
	struct hal_srng_config *srng_config;

	if (WARN_ON(ring_type >= HAL_MAX_RING_TYPES))
		return -EINVAL;

	srng_config = &ab->hal.srng_config[ring_type];

	return (srng_config->max_size / srng_config->entry_size);
}

void ath12k_hal_srng_get_params(struct ath12k_base *ab, struct hal_srng *srng,
				struct hal_srng_params *params)
{
	params->ring_base_paddr = srng->ring_base_paddr;
	params->ring_base_vaddr = srng->ring_base_vaddr;
	params->num_entries = srng->num_entries;
	params->intr_timer_thres_us = srng->intr_timer_thres_us;
	params->intr_batch_cntr_thres_entries =
		srng->intr_batch_cntr_thres_entries;
	params->low_threshold = srng->u.src_ring.low_threshold;
	params->msi_addr = srng->msi_addr;
	params->msi2_addr = srng->msi2_addr;
	params->msi_data = srng->msi_data;
	params->msi2_data = srng->msi2_data;
	params->flags = srng->flags;
}
EXPORT_SYMBOL(ath12k_hal_srng_get_params);

dma_addr_t ath12k_hal_srng_get_hp_addr(struct ath12k_base *ab,
				       struct hal_srng *srng)
{
	if (!(srng->flags & HAL_SRNG_FLAGS_LMAC_RING))
		return 0;

	if (srng->ring_dir == HAL_SRNG_DIR_SRC)
		return ab->hal.wrp.paddr +
		       ((unsigned long)srng->u.src_ring.hp_addr -
			(unsigned long)ab->hal.wrp.vaddr);
	else
		return ab->hal.rdp.paddr +
		       ((unsigned long)srng->u.dst_ring.hp_addr -
			 (unsigned long)ab->hal.rdp.vaddr);
}

dma_addr_t ath12k_hal_srng_get_tp_addr(struct ath12k_base *ab,
				       struct hal_srng *srng)
{
	if (!(srng->flags & HAL_SRNG_FLAGS_LMAC_RING))
		return 0;

	if (srng->ring_dir == HAL_SRNG_DIR_SRC)
		return ab->hal.rdp.paddr +
		       ((unsigned long)srng->u.src_ring.tp_addr -
			(unsigned long)ab->hal.rdp.vaddr);
	else
		return ab->hal.wrp.paddr +
		       ((unsigned long)srng->u.dst_ring.tp_addr -
			(unsigned long)ab->hal.wrp.vaddr);
}

void ath12k_hal_ce_src_set_desc(struct ath12k_hal *hal,
				struct hal_ce_srng_src_desc *desc,
				dma_addr_t paddr, u32 len, u32 id,
				u8 byte_swap_data)
{
	hal->ops->ce_src_set_desc(desc, paddr, len, id, byte_swap_data);
}

void ath12k_hal_ce_dst_set_desc(struct ath12k_hal *hal,
				struct hal_ce_srng_dest_desc *desc,
				dma_addr_t paddr)
{
	hal->ops->ce_dst_set_desc(desc, paddr);
}

u32 ath12k_hal_ce_dst_status_get_length(struct ath12k_hal *hal,
					struct hal_ce_srng_dst_status_desc *desc)
{
	return hal->ops->ce_dst_status_get_length(desc);
}

void ath12k_hal_set_link_desc_addr(struct ath12k_hal *hal,
				   struct hal_wbm_link_desc *desc, u32 cookie,
				   dma_addr_t paddr, int rbm)
{
	hal->ops->set_link_desc_addr(desc, cookie, paddr, rbm);
}

void *ath12k_hal_srng_dst_peek(struct ath12k_base *ab, struct hal_srng *srng)
{
	lockdep_assert_held(&srng->lock);

	if (srng->u.dst_ring.tp != srng->u.dst_ring.cached_hp)
		return (srng->ring_base_vaddr + srng->u.dst_ring.tp);

	return NULL;
}
EXPORT_SYMBOL(ath12k_hal_srng_dst_peek);

void *ath12k_hal_srng_dst_get_next_entry(struct ath12k_base *ab,
					 struct hal_srng *srng)
{
	void *desc;

	lockdep_assert_held(&srng->lock);

	if (srng->u.dst_ring.tp == srng->u.dst_ring.cached_hp)
		return NULL;

	desc = srng->ring_base_vaddr + srng->u.dst_ring.tp;

	srng->u.dst_ring.tp = (srng->u.dst_ring.tp + srng->entry_size) %
			      srng->ring_size;

	return desc;
}
EXPORT_SYMBOL(ath12k_hal_srng_dst_get_next_entry);

int ath12k_hal_srng_dst_num_free(struct ath12k_base *ab, struct hal_srng *srng,
				 bool sync_hw_ptr)
{
	u32 tp, hp;

	lockdep_assert_held(&srng->lock);

	tp = srng->u.dst_ring.tp;

	if (sync_hw_ptr) {
		hp = *srng->u.dst_ring.hp_addr;
		srng->u.dst_ring.cached_hp = hp;
	} else {
		hp = srng->u.dst_ring.cached_hp;
	}

	if (hp >= tp)
		return (hp - tp) / srng->entry_size;
	else
		return (srng->ring_size - tp + hp) / srng->entry_size;
}
EXPORT_SYMBOL(ath12k_hal_srng_dst_num_free);

/* Returns number of available entries in src ring */
int ath12k_hal_srng_src_num_free(struct ath12k_base *ab, struct hal_srng *srng,
				 bool sync_hw_ptr)
{
	u32 tp, hp;

	lockdep_assert_held(&srng->lock);

	hp = srng->u.src_ring.hp;

	if (sync_hw_ptr) {
		tp = *srng->u.src_ring.tp_addr;
		srng->u.src_ring.cached_tp = tp;
	} else {
		tp = srng->u.src_ring.cached_tp;
	}

	if (tp > hp)
		return ((tp - hp) / srng->entry_size) - 1;
	else
		return ((srng->ring_size - hp + tp) / srng->entry_size) - 1;
}

void *ath12k_hal_srng_src_next_peek(struct ath12k_base *ab,
				    struct hal_srng *srng)
{
	void *desc;
	u32 next_hp;

	lockdep_assert_held(&srng->lock);

	next_hp = (srng->u.src_ring.hp + srng->entry_size) % srng->ring_size;

	if (next_hp == srng->u.src_ring.cached_tp)
		return NULL;

	desc = srng->ring_base_vaddr + next_hp;

	return desc;
}
EXPORT_SYMBOL(ath12k_hal_srng_src_next_peek);

void *ath12k_hal_srng_src_get_next_entry(struct ath12k_base *ab,
					 struct hal_srng *srng)
{
	void *desc;
	u32 next_hp;

	lockdep_assert_held(&srng->lock);

	/* TODO: Using % is expensive, but we have to do this since size of some
	 * SRNG rings is not power of 2 (due to descriptor sizes). Need to see
	 * if separate function is defined for rings having power of 2 ring size
	 * (TCL2SW, REO2SW, SW2RXDMA and CE rings) so that we can avoid the
	 * overhead of % by using mask (with &).
	 */
	next_hp = (srng->u.src_ring.hp + srng->entry_size) % srng->ring_size;

	if (next_hp == srng->u.src_ring.cached_tp)
		return NULL;

	desc = srng->ring_base_vaddr + srng->u.src_ring.hp;
	srng->u.src_ring.hp = next_hp;

	/* TODO: Reap functionality is not used by all rings. If particular
	 * ring does not use reap functionality, we need not update reap_hp
	 * with next_hp pointer. Need to make sure a separate function is used
	 * before doing any optimization by removing below code updating
	 * reap_hp.
	 */
	srng->u.src_ring.reap_hp = next_hp;

	return desc;
}
EXPORT_SYMBOL(ath12k_hal_srng_src_get_next_entry);

void *ath12k_hal_srng_src_peek(struct ath12k_base *ab, struct hal_srng *srng)
{
	lockdep_assert_held(&srng->lock);

	if (((srng->u.src_ring.hp + srng->entry_size) % srng->ring_size) ==
	    srng->u.src_ring.cached_tp)
		return NULL;

	return srng->ring_base_vaddr + srng->u.src_ring.hp;
}
EXPORT_SYMBOL(ath12k_hal_srng_src_peek);

void *ath12k_hal_srng_src_reap_next(struct ath12k_base *ab,
				    struct hal_srng *srng)
{
	void *desc;
	u32 next_reap_hp;

	lockdep_assert_held(&srng->lock);

	next_reap_hp = (srng->u.src_ring.reap_hp + srng->entry_size) %
		       srng->ring_size;

	if (next_reap_hp == srng->u.src_ring.cached_tp)
		return NULL;

	desc = srng->ring_base_vaddr + next_reap_hp;
	srng->u.src_ring.reap_hp = next_reap_hp;

	return desc;
}

void *ath12k_hal_srng_src_get_next_reaped(struct ath12k_base *ab,
					  struct hal_srng *srng)
{
	void *desc;

	lockdep_assert_held(&srng->lock);

	if (srng->u.src_ring.hp == srng->u.src_ring.reap_hp)
		return NULL;

	desc = srng->ring_base_vaddr + srng->u.src_ring.hp;
	srng->u.src_ring.hp = (srng->u.src_ring.hp + srng->entry_size) %
			      srng->ring_size;

	return desc;
}

void ath12k_hal_srng_access_begin(struct ath12k_base *ab, struct hal_srng *srng)
{
	u32 hp;

	lockdep_assert_held(&srng->lock);

	if (srng->ring_dir == HAL_SRNG_DIR_SRC) {
		srng->u.src_ring.cached_tp =
			*(volatile u32 *)srng->u.src_ring.tp_addr;
	} else {
		hp = READ_ONCE(*srng->u.dst_ring.hp_addr);

		if (hp != srng->u.dst_ring.cached_hp) {
			srng->u.dst_ring.cached_hp = hp;
			/* Make sure descriptor is read after the head
			 * pointer.
			 */
			dma_rmb();
		}
	}
}
EXPORT_SYMBOL(ath12k_hal_srng_access_begin);

/* Update cached ring head/tail pointers to HW. ath12k_hal_srng_access_begin()
 * should have been called before this.
 */
void ath12k_hal_srng_access_end(struct ath12k_base *ab, struct hal_srng *srng)
{
	lockdep_assert_held(&srng->lock);

	if (srng->flags & HAL_SRNG_FLAGS_LMAC_RING) {
		/* For LMAC rings, ring pointer updates are done through FW and
		 * hence written to a shared memory location that is read by FW
		 */
		if (srng->ring_dir == HAL_SRNG_DIR_SRC) {
			srng->u.src_ring.last_tp =
				*(volatile u32 *)srng->u.src_ring.tp_addr;
			/* Make sure descriptor is written before updating the
			 * head pointer.
			 */
			dma_wmb();
			WRITE_ONCE(*srng->u.src_ring.hp_addr, srng->u.src_ring.hp);
		} else {
			srng->u.dst_ring.last_hp = *srng->u.dst_ring.hp_addr;
			/* Make sure descriptor is read before updating the
			 * tail pointer.
			 */
			dma_mb();
			WRITE_ONCE(*srng->u.dst_ring.tp_addr, srng->u.dst_ring.tp);
		}
	} else {
		if (srng->ring_dir == HAL_SRNG_DIR_SRC) {
			srng->u.src_ring.last_tp =
				*(volatile u32 *)srng->u.src_ring.tp_addr;
			/* Assume implementation use an MMIO write accessor
			 * which has the required wmb() so that the descriptor
			 * is written before the updating the head pointer.
			 */
			ath12k_hif_write32(ab,
					   (unsigned long)srng->u.src_ring.hp_addr -
					   (unsigned long)ab->mem,
					   srng->u.src_ring.hp);
		} else {
			srng->u.dst_ring.last_hp = *srng->u.dst_ring.hp_addr;
			/* Make sure descriptor is read before updating the
			 * tail pointer.
			 */
			mb();
			ath12k_hif_write32(ab,
					   (unsigned long)srng->u.dst_ring.tp_addr -
					   (unsigned long)ab->mem,
					   srng->u.dst_ring.tp);
		}
	}

	srng->timestamp = jiffies;
}
EXPORT_SYMBOL(ath12k_hal_srng_access_end);

int ath12k_hal_srng_setup(struct ath12k_base *ab, enum hal_ring_type type,
			  int ring_num, int mac_id,
			  struct hal_srng_params *params)
{
	struct ath12k_hal *hal = &ab->hal;
	struct hal_srng_config *srng_config = &ab->hal.srng_config[type];
	struct hal_srng *srng;
	int ring_id;
	u32 idx;
	int i;

	ring_id = ath12k_hal_srng_get_ring_id(hal, type, ring_num, mac_id);
	if (ring_id < 0)
		return ring_id;

	srng = &hal->srng_list[ring_id];

	srng->ring_id = ring_id;
	srng->ring_dir = srng_config->ring_dir;
	srng->ring_base_paddr = params->ring_base_paddr;
	srng->ring_base_vaddr = params->ring_base_vaddr;
	srng->entry_size = srng_config->entry_size;
	srng->num_entries = params->num_entries;
	srng->ring_size = srng->entry_size * srng->num_entries;
	srng->intr_batch_cntr_thres_entries =
				params->intr_batch_cntr_thres_entries;
	srng->intr_timer_thres_us = params->intr_timer_thres_us;
	srng->flags = params->flags;
	srng->msi_addr = params->msi_addr;
	srng->msi2_addr = params->msi2_addr;
	srng->msi_data = params->msi_data;
	srng->msi2_data = params->msi2_data;
	srng->initialized = 1;
	spin_lock_init(&srng->lock);
	lockdep_set_class(&srng->lock, &srng->lock_key);

	for (i = 0; i < HAL_SRNG_NUM_REG_GRP; i++) {
		srng->hwreg_base[i] = srng_config->reg_start[i] +
				      (ring_num * srng_config->reg_size[i]);
	}

	memset(srng->ring_base_vaddr, 0,
	       (srng->entry_size * srng->num_entries) << 2);

	if (srng->ring_dir == HAL_SRNG_DIR_SRC) {
		srng->u.src_ring.hp = 0;
		srng->u.src_ring.cached_tp = 0;
		srng->u.src_ring.reap_hp = srng->ring_size - srng->entry_size;
		srng->u.src_ring.tp_addr = (void *)(hal->rdp.vaddr + ring_id);
		srng->u.src_ring.low_threshold = params->low_threshold *
						 srng->entry_size;
		if (srng_config->mac_type == ATH12K_HAL_SRNG_UMAC) {
			ath12k_hal_set_umac_srng_ptr_addr(ab, srng);
		} else {
			idx = ring_id - HAL_SRNG_RING_ID_DMAC_CMN_ID_START;
			srng->u.src_ring.hp_addr = (void *)(hal->wrp.vaddr +
						   idx);
			srng->flags |= HAL_SRNG_FLAGS_LMAC_RING;
		}
	} else {
		/* During initialization loop count in all the descriptors
		 * will be set to zero, and HW will set it to 1 on completing
		 * descriptor update in first loop, and increments it by 1 on
		 * subsequent loops (loop count wraps around after reaching
		 * 0xffff). The 'loop_cnt' in SW ring state is the expected
		 * loop count in descriptors updated by HW (to be processed
		 * by SW).
		 */
		srng->u.dst_ring.loop_cnt = 1;
		srng->u.dst_ring.tp = 0;
		srng->u.dst_ring.cached_hp = 0;
		srng->u.dst_ring.hp_addr = (void *)(hal->rdp.vaddr + ring_id);
		if (srng_config->mac_type == ATH12K_HAL_SRNG_UMAC) {
			ath12k_hal_set_umac_srng_ptr_addr(ab, srng);
		} else {
			/* For PMAC & DMAC rings, tail pointer updates will be done
			 * through FW by writing to a shared memory location
			 */
			idx = ring_id - HAL_SRNG_RING_ID_DMAC_CMN_ID_START;
			srng->u.dst_ring.tp_addr = (void *)(hal->wrp.vaddr +
						   idx);
			srng->flags |= HAL_SRNG_FLAGS_LMAC_RING;
		}
	}

	if (srng_config->mac_type != ATH12K_HAL_SRNG_UMAC)
		return ring_id;

	ath12k_hal_srng_hw_init(ab, srng);

	if (type == HAL_CE_DST) {
		srng->u.dst_ring.max_buffer_length = params->max_buffer_len;
		ath12k_hal_ce_dst_setup(ab, srng, ring_num);
	}

	return ring_id;
}

void ath12k_hal_srng_shadow_config(struct ath12k_base *ab)
{
	struct ath12k_hal *hal = &ab->hal;
	int ring_type, ring_num;

	/* update all the non-CE srngs. */
	for (ring_type = 0; ring_type < HAL_MAX_RING_TYPES; ring_type++) {
		struct hal_srng_config *srng_config = &hal->srng_config[ring_type];

		if (ring_type == HAL_CE_SRC ||
		    ring_type == HAL_CE_DST ||
			ring_type == HAL_CE_DST_STATUS)
			continue;

		if (srng_config->mac_type == ATH12K_HAL_SRNG_DMAC ||
		    srng_config->mac_type == ATH12K_HAL_SRNG_PMAC)
			continue;

		for (ring_num = 0; ring_num < srng_config->max_rings; ring_num++)
			ath12k_hal_srng_update_shadow_config(ab, ring_type, ring_num);
	}
}

void ath12k_hal_srng_get_shadow_config(struct ath12k_base *ab,
				       u32 **cfg, u32 *len)
{
	struct ath12k_hal *hal = &ab->hal;

	*len = hal->num_shadow_reg_configured;
	*cfg = hal->shadow_reg_addr;
}

void ath12k_hal_srng_shadow_update_hp_tp(struct ath12k_base *ab,
					 struct hal_srng *srng)
{
	lockdep_assert_held(&srng->lock);

	/* check whether the ring is empty. Update the shadow
	 * HP only when then ring isn't' empty.
	 */
	if (srng->ring_dir == HAL_SRNG_DIR_SRC &&
	    *srng->u.src_ring.tp_addr != srng->u.src_ring.hp)
		ath12k_hal_srng_access_end(ab, srng);
}

static void ath12k_hal_register_srng_lock_keys(struct ath12k_hal *hal)
{
	u32 ring_id;

	for (ring_id = 0; ring_id < HAL_SRNG_RING_ID_MAX; ring_id++)
		lockdep_register_key(&hal->srng_list[ring_id].lock_key);
}

static void ath12k_hal_unregister_srng_lock_keys(struct ath12k_hal *hal)
{
	u32 ring_id;

	for (ring_id = 0; ring_id < HAL_SRNG_RING_ID_MAX; ring_id++)
		lockdep_unregister_key(&hal->srng_list[ring_id].lock_key);
}

int ath12k_hal_srng_init(struct ath12k_base *ab)
{
	struct ath12k_hal *hal = &ab->hal;
	int ret;

	ret = hal->ops->create_srng_config(hal);
	if (ret)
		goto err_hal;

	hal->dev = ab->dev;

	ret = ath12k_hal_alloc_cont_rdp(hal);
	if (ret)
		goto err_hal;

	ret = ath12k_hal_alloc_cont_wrp(hal);
	if (ret)
		goto err_free_cont_rdp;

	ath12k_hal_register_srng_lock_keys(hal);

	return 0;

err_free_cont_rdp:
	ath12k_hal_free_cont_rdp(hal);

err_hal:
	return ret;
}

void ath12k_hal_srng_deinit(struct ath12k_base *ab)
{
	struct ath12k_hal *hal = &ab->hal;

	ath12k_hal_unregister_srng_lock_keys(hal);
	ath12k_hal_free_cont_rdp(hal);
	ath12k_hal_free_cont_wrp(hal);
	kfree(hal->srng_config);
	hal->srng_config = NULL;
}

void ath12k_hal_dump_srng_stats(struct ath12k_base *ab)
{
	struct hal_srng *srng;
	struct ath12k_ext_irq_grp *irq_grp;
	struct ath12k_ce_pipe *ce_pipe;
	int i;

	ath12k_err(ab, "Last interrupt received for each CE:\n");
	for (i = 0; i < ab->hw_params->ce_count; i++) {
		ce_pipe = &ab->ce.ce_pipe[i];

		if (ath12k_ce_get_attr_flags(ab, i) & CE_ATTR_DIS_INTR)
			continue;

		ath12k_err(ab, "CE_id %d pipe_num %d %ums before\n",
			   i, ce_pipe->pipe_num,
			   jiffies_to_msecs(jiffies - ce_pipe->timestamp));
	}

	ath12k_err(ab, "\nLast interrupt received for each group:\n");
	for (i = 0; i < ATH12K_EXT_IRQ_GRP_NUM_MAX; i++) {
		irq_grp = &ab->ext_irq_grp[i];
		ath12k_err(ab, "group_id %d %ums before\n",
			   irq_grp->grp_id,
			   jiffies_to_msecs(jiffies - irq_grp->timestamp));
	}

	for (i = 0; i < HAL_SRNG_RING_ID_MAX; i++) {
		srng = &ab->hal.srng_list[i];

		if (!srng->initialized)
			continue;

		if (srng->ring_dir == HAL_SRNG_DIR_SRC)
			ath12k_err(ab,
				   "src srng id %u hp %u, reap_hp %u, cur tp %u, cached tp %u last tp %u napi processed before %ums\n",
				   srng->ring_id, srng->u.src_ring.hp,
				   srng->u.src_ring.reap_hp,
				   *srng->u.src_ring.tp_addr, srng->u.src_ring.cached_tp,
				   srng->u.src_ring.last_tp,
				   jiffies_to_msecs(jiffies - srng->timestamp));
		else if (srng->ring_dir == HAL_SRNG_DIR_DST)
			ath12k_err(ab,
				   "dst srng id %u tp %u, cur hp %u, cached hp %u last hp %u napi processed before %ums\n",
				   srng->ring_id, srng->u.dst_ring.tp,
				   *srng->u.dst_ring.hp_addr,
				   srng->u.dst_ring.cached_hp,
				   srng->u.dst_ring.last_hp,
				   jiffies_to_msecs(jiffies - srng->timestamp));
	}
}

void *ath12k_hal_encode_tlv64_hdr(void *tlv, u64 tag, u64 len)
{
	struct hal_tlv_64_hdr *tlv64 = tlv;

	tlv64->tl = le64_encode_bits(tag, HAL_TLV_HDR_TAG) |
		    le64_encode_bits(len, HAL_TLV_HDR_LEN);

	return tlv64->value;
}
EXPORT_SYMBOL(ath12k_hal_encode_tlv64_hdr);

void *ath12k_hal_encode_tlv32_hdr(void *tlv, u64 tag, u64 len)
{
	struct hal_tlv_hdr *tlv32 = tlv;

	tlv32->tl = le32_encode_bits(tag, HAL_TLV_HDR_TAG) |
		    le32_encode_bits(len, HAL_TLV_HDR_LEN);

	return tlv32->value;
}
EXPORT_SYMBOL(ath12k_hal_encode_tlv32_hdr);

u16 ath12k_hal_decode_tlv64_hdr(void *tlv, void **desc)
{
	struct hal_tlv_64_hdr *tlv64 = tlv;
	u16 tag;

	tag = le64_get_bits(tlv64->tl, HAL_SRNG_TLV_HDR_TAG);
	*desc = tlv64->value;

	return tag;
}
EXPORT_SYMBOL(ath12k_hal_decode_tlv64_hdr);

u16 ath12k_hal_decode_tlv32_hdr(void *tlv, void **desc)
{
	struct hal_tlv_hdr *tlv32 = tlv;
	u16 tag;

	tag = le32_get_bits(tlv32->tl, HAL_SRNG_TLV_HDR_TAG);
	*desc = tlv32->value;

	return tag;
}
EXPORT_SYMBOL(ath12k_hal_decode_tlv32_hdr);
