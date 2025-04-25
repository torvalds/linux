// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2017 Qualcomm Atheros, Inc.
 * Copyright (c) 2018 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "hif.h"
#include "ce.h"
#include "debug.h"

/*
 * Support for Copy Engine hardware, which is mainly used for
 * communication between Host and Target over a PCIe interconnect.
 */

/*
 * A single CopyEngine (CE) comprises two "rings":
 *   a source ring
 *   a destination ring
 *
 * Each ring consists of a number of descriptors which specify
 * an address, length, and meta-data.
 *
 * Typically, one side of the PCIe/AHB/SNOC interconnect (Host or Target)
 * controls one ring and the other side controls the other ring.
 * The source side chooses when to initiate a transfer and it
 * chooses what to send (buffer address, length). The destination
 * side keeps a supply of "anonymous receive buffers" available and
 * it handles incoming data as it arrives (when the destination
 * receives an interrupt).
 *
 * The sender may send a simple buffer (address/length) or it may
 * send a small list of buffers.  When a small list is sent, hardware
 * "gathers" these and they end up in a single destination buffer
 * with a single interrupt.
 *
 * There are several "contexts" managed by this layer -- more, it
 * may seem -- than should be needed. These are provided mainly for
 * maximum flexibility and especially to facilitate a simpler HIF
 * implementation. There are per-CopyEngine recv, send, and watermark
 * contexts. These are supplied by the caller when a recv, send,
 * or watermark handler is established and they are echoed back to
 * the caller when the respective callbacks are invoked. There is
 * also a per-transfer context supplied by the caller when a buffer
 * (or sendlist) is sent and when a buffer is enqueued for recv.
 * These per-transfer contexts are echoed back to the caller when
 * the buffer is sent/received.
 */

static inline u32 shadow_sr_wr_ind_addr(struct ath10k *ar,
					struct ath10k_ce_pipe *ce_state)
{
	u32 ce_id = ce_state->id;
	u32 addr = 0;

	switch (ce_id) {
	case 0:
		addr = 0x00032000;
		break;
	case 3:
		addr = 0x0003200C;
		break;
	case 4:
		addr = 0x00032010;
		break;
	case 5:
		addr = 0x00032014;
		break;
	case 7:
		addr = 0x0003201C;
		break;
	default:
		ath10k_warn(ar, "invalid CE id: %d", ce_id);
		break;
	}
	return addr;
}

static inline unsigned int
ath10k_set_ring_byte(unsigned int offset,
		     struct ath10k_hw_ce_regs_addr_map *addr_map)
{
	return ((offset << addr_map->lsb) & addr_map->mask);
}

static inline u32 ath10k_ce_read32(struct ath10k *ar, u32 offset)
{
	struct ath10k_ce *ce = ath10k_ce_priv(ar);

	return ce->bus_ops->read32(ar, offset);
}

static inline void ath10k_ce_write32(struct ath10k *ar, u32 offset, u32 value)
{
	struct ath10k_ce *ce = ath10k_ce_priv(ar);

	ce->bus_ops->write32(ar, offset, value);
}

static inline void ath10k_ce_dest_ring_write_index_set(struct ath10k *ar,
						       u32 ce_ctrl_addr,
						       unsigned int n)
{
	ath10k_ce_write32(ar, ce_ctrl_addr +
			  ar->hw_ce_regs->dst_wr_index_addr, n);
}

static inline u32 ath10k_ce_dest_ring_write_index_get(struct ath10k *ar,
						      u32 ce_ctrl_addr)
{
	return ath10k_ce_read32(ar, ce_ctrl_addr +
				ar->hw_ce_regs->dst_wr_index_addr);
}

static inline void ath10k_ce_src_ring_write_index_set(struct ath10k *ar,
						      u32 ce_ctrl_addr,
						      unsigned int n)
{
	ath10k_ce_write32(ar, ce_ctrl_addr +
			  ar->hw_ce_regs->sr_wr_index_addr, n);
}

static inline u32 ath10k_ce_src_ring_write_index_get(struct ath10k *ar,
						     u32 ce_ctrl_addr)
{
	return ath10k_ce_read32(ar, ce_ctrl_addr +
				ar->hw_ce_regs->sr_wr_index_addr);
}

static inline u32 ath10k_ce_src_ring_read_index_from_ddr(struct ath10k *ar,
							 u32 ce_id)
{
	struct ath10k_ce *ce = ath10k_ce_priv(ar);

	return ce->vaddr_rri[ce_id] & CE_DDR_RRI_MASK;
}

static inline u32 ath10k_ce_src_ring_read_index_get(struct ath10k *ar,
						    u32 ce_ctrl_addr)
{
	struct ath10k_ce *ce = ath10k_ce_priv(ar);
	u32 ce_id = COPY_ENGINE_ID(ce_ctrl_addr);
	struct ath10k_ce_pipe *ce_state = &ce->ce_states[ce_id];
	u32 index;

	if (ar->hw_params.rri_on_ddr &&
	    (ce_state->attr_flags & CE_ATTR_DIS_INTR))
		index = ath10k_ce_src_ring_read_index_from_ddr(ar, ce_id);
	else
		index = ath10k_ce_read32(ar, ce_ctrl_addr +
					 ar->hw_ce_regs->current_srri_addr);

	return index;
}

static inline void
ath10k_ce_shadow_src_ring_write_index_set(struct ath10k *ar,
					  struct ath10k_ce_pipe *ce_state,
					  unsigned int value)
{
	ath10k_ce_write32(ar, shadow_sr_wr_ind_addr(ar, ce_state), value);
}

static inline void ath10k_ce_src_ring_base_addr_set(struct ath10k *ar,
						    u32 ce_id,
						    u64 addr)
{
	struct ath10k_ce *ce = ath10k_ce_priv(ar);
	struct ath10k_ce_pipe *ce_state = &ce->ce_states[ce_id];
	u32 ce_ctrl_addr = ath10k_ce_base_address(ar, ce_id);
	u32 addr_lo = lower_32_bits(addr);

	ath10k_ce_write32(ar, ce_ctrl_addr +
			  ar->hw_ce_regs->sr_base_addr_lo, addr_lo);

	if (ce_state->ops->ce_set_src_ring_base_addr_hi) {
		ce_state->ops->ce_set_src_ring_base_addr_hi(ar, ce_ctrl_addr,
							    addr);
	}
}

static void ath10k_ce_set_src_ring_base_addr_hi(struct ath10k *ar,
						u32 ce_ctrl_addr,
						u64 addr)
{
	u32 addr_hi = upper_32_bits(addr) & CE_DESC_ADDR_HI_MASK;

	ath10k_ce_write32(ar, ce_ctrl_addr +
			  ar->hw_ce_regs->sr_base_addr_hi, addr_hi);
}

static inline void ath10k_ce_src_ring_size_set(struct ath10k *ar,
					       u32 ce_ctrl_addr,
					       unsigned int n)
{
	ath10k_ce_write32(ar, ce_ctrl_addr +
			  ar->hw_ce_regs->sr_size_addr, n);
}

static inline void ath10k_ce_src_ring_dmax_set(struct ath10k *ar,
					       u32 ce_ctrl_addr,
					       unsigned int n)
{
	struct ath10k_hw_ce_ctrl1 *ctrl_regs = ar->hw_ce_regs->ctrl1_regs;

	u32 ctrl1_addr = ath10k_ce_read32(ar, ce_ctrl_addr +
					  ctrl_regs->addr);

	ath10k_ce_write32(ar, ce_ctrl_addr + ctrl_regs->addr,
			  (ctrl1_addr &  ~(ctrl_regs->dmax->mask)) |
			  ath10k_set_ring_byte(n, ctrl_regs->dmax));
}

static inline void ath10k_ce_src_ring_byte_swap_set(struct ath10k *ar,
						    u32 ce_ctrl_addr,
						    unsigned int n)
{
	struct ath10k_hw_ce_ctrl1 *ctrl_regs = ar->hw_ce_regs->ctrl1_regs;

	u32 ctrl1_addr = ath10k_ce_read32(ar, ce_ctrl_addr +
					  ctrl_regs->addr);

	ath10k_ce_write32(ar, ce_ctrl_addr + ctrl_regs->addr,
			  (ctrl1_addr & ~(ctrl_regs->src_ring->mask)) |
			  ath10k_set_ring_byte(n, ctrl_regs->src_ring));
}

static inline void ath10k_ce_dest_ring_byte_swap_set(struct ath10k *ar,
						     u32 ce_ctrl_addr,
						     unsigned int n)
{
	struct ath10k_hw_ce_ctrl1 *ctrl_regs = ar->hw_ce_regs->ctrl1_regs;

	u32 ctrl1_addr = ath10k_ce_read32(ar, ce_ctrl_addr +
					  ctrl_regs->addr);

	ath10k_ce_write32(ar, ce_ctrl_addr + ctrl_regs->addr,
			  (ctrl1_addr & ~(ctrl_regs->dst_ring->mask)) |
			  ath10k_set_ring_byte(n, ctrl_regs->dst_ring));
}

static inline
	u32 ath10k_ce_dest_ring_read_index_from_ddr(struct ath10k *ar, u32 ce_id)
{
	struct ath10k_ce *ce = ath10k_ce_priv(ar);

	return (ce->vaddr_rri[ce_id] >> CE_DDR_DRRI_SHIFT) &
		CE_DDR_RRI_MASK;
}

static inline u32 ath10k_ce_dest_ring_read_index_get(struct ath10k *ar,
						     u32 ce_ctrl_addr)
{
	struct ath10k_ce *ce = ath10k_ce_priv(ar);
	u32 ce_id = COPY_ENGINE_ID(ce_ctrl_addr);
	struct ath10k_ce_pipe *ce_state = &ce->ce_states[ce_id];
	u32 index;

	if (ar->hw_params.rri_on_ddr &&
	    (ce_state->attr_flags & CE_ATTR_DIS_INTR))
		index = ath10k_ce_dest_ring_read_index_from_ddr(ar, ce_id);
	else
		index = ath10k_ce_read32(ar, ce_ctrl_addr +
					 ar->hw_ce_regs->current_drri_addr);

	return index;
}

static inline void ath10k_ce_dest_ring_base_addr_set(struct ath10k *ar,
						     u32 ce_id,
						     u64 addr)
{
	struct ath10k_ce *ce = ath10k_ce_priv(ar);
	struct ath10k_ce_pipe *ce_state = &ce->ce_states[ce_id];
	u32 ce_ctrl_addr = ath10k_ce_base_address(ar, ce_id);
	u32 addr_lo = lower_32_bits(addr);

	ath10k_ce_write32(ar, ce_ctrl_addr +
			  ar->hw_ce_regs->dr_base_addr_lo, addr_lo);

	if (ce_state->ops->ce_set_dest_ring_base_addr_hi) {
		ce_state->ops->ce_set_dest_ring_base_addr_hi(ar, ce_ctrl_addr,
							     addr);
	}
}

static void ath10k_ce_set_dest_ring_base_addr_hi(struct ath10k *ar,
						 u32 ce_ctrl_addr,
						 u64 addr)
{
	u32 addr_hi = upper_32_bits(addr) & CE_DESC_ADDR_HI_MASK;
	u32 reg_value;

	reg_value = ath10k_ce_read32(ar, ce_ctrl_addr +
				     ar->hw_ce_regs->dr_base_addr_hi);
	reg_value &= ~CE_DESC_ADDR_HI_MASK;
	reg_value |= addr_hi;
	ath10k_ce_write32(ar, ce_ctrl_addr +
			  ar->hw_ce_regs->dr_base_addr_hi, reg_value);
}

static inline void ath10k_ce_dest_ring_size_set(struct ath10k *ar,
						u32 ce_ctrl_addr,
						unsigned int n)
{
	ath10k_ce_write32(ar, ce_ctrl_addr +
			  ar->hw_ce_regs->dr_size_addr, n);
}

static inline void ath10k_ce_src_ring_highmark_set(struct ath10k *ar,
						   u32 ce_ctrl_addr,
						   unsigned int n)
{
	struct ath10k_hw_ce_dst_src_wm_regs *srcr_wm = ar->hw_ce_regs->wm_srcr;
	u32 addr = ath10k_ce_read32(ar, ce_ctrl_addr + srcr_wm->addr);

	ath10k_ce_write32(ar, ce_ctrl_addr + srcr_wm->addr,
			  (addr & ~(srcr_wm->wm_high->mask)) |
			  (ath10k_set_ring_byte(n, srcr_wm->wm_high)));
}

static inline void ath10k_ce_src_ring_lowmark_set(struct ath10k *ar,
						  u32 ce_ctrl_addr,
						  unsigned int n)
{
	struct ath10k_hw_ce_dst_src_wm_regs *srcr_wm = ar->hw_ce_regs->wm_srcr;
	u32 addr = ath10k_ce_read32(ar, ce_ctrl_addr + srcr_wm->addr);

	ath10k_ce_write32(ar, ce_ctrl_addr + srcr_wm->addr,
			  (addr & ~(srcr_wm->wm_low->mask)) |
			  (ath10k_set_ring_byte(n, srcr_wm->wm_low)));
}

static inline void ath10k_ce_dest_ring_highmark_set(struct ath10k *ar,
						    u32 ce_ctrl_addr,
						    unsigned int n)
{
	struct ath10k_hw_ce_dst_src_wm_regs *dstr_wm = ar->hw_ce_regs->wm_dstr;
	u32 addr = ath10k_ce_read32(ar, ce_ctrl_addr + dstr_wm->addr);

	ath10k_ce_write32(ar, ce_ctrl_addr + dstr_wm->addr,
			  (addr & ~(dstr_wm->wm_high->mask)) |
			  (ath10k_set_ring_byte(n, dstr_wm->wm_high)));
}

static inline void ath10k_ce_dest_ring_lowmark_set(struct ath10k *ar,
						   u32 ce_ctrl_addr,
						   unsigned int n)
{
	struct ath10k_hw_ce_dst_src_wm_regs *dstr_wm = ar->hw_ce_regs->wm_dstr;
	u32 addr = ath10k_ce_read32(ar, ce_ctrl_addr + dstr_wm->addr);

	ath10k_ce_write32(ar, ce_ctrl_addr + dstr_wm->addr,
			  (addr & ~(dstr_wm->wm_low->mask)) |
			  (ath10k_set_ring_byte(n, dstr_wm->wm_low)));
}

static inline void ath10k_ce_copy_complete_inter_enable(struct ath10k *ar,
							u32 ce_ctrl_addr)
{
	struct ath10k_hw_ce_host_ie *host_ie = ar->hw_ce_regs->host_ie;

	u32 host_ie_addr = ath10k_ce_read32(ar, ce_ctrl_addr +
					    ar->hw_ce_regs->host_ie_addr);

	ath10k_ce_write32(ar, ce_ctrl_addr + ar->hw_ce_regs->host_ie_addr,
			  host_ie_addr | host_ie->copy_complete->mask);
}

static inline void ath10k_ce_copy_complete_intr_disable(struct ath10k *ar,
							u32 ce_ctrl_addr)
{
	struct ath10k_hw_ce_host_ie *host_ie = ar->hw_ce_regs->host_ie;

	u32 host_ie_addr = ath10k_ce_read32(ar, ce_ctrl_addr +
					    ar->hw_ce_regs->host_ie_addr);

	ath10k_ce_write32(ar, ce_ctrl_addr + ar->hw_ce_regs->host_ie_addr,
			  host_ie_addr & ~(host_ie->copy_complete->mask));
}

static inline void ath10k_ce_watermark_intr_disable(struct ath10k *ar,
						    u32 ce_ctrl_addr)
{
	struct ath10k_hw_ce_host_wm_regs *wm_regs = ar->hw_ce_regs->wm_regs;

	u32 host_ie_addr = ath10k_ce_read32(ar, ce_ctrl_addr +
					    ar->hw_ce_regs->host_ie_addr);

	ath10k_ce_write32(ar, ce_ctrl_addr + ar->hw_ce_regs->host_ie_addr,
			  host_ie_addr & ~(wm_regs->wm_mask));
}

static inline void ath10k_ce_error_intr_disable(struct ath10k *ar,
						u32 ce_ctrl_addr)
{
	struct ath10k_hw_ce_misc_regs *misc_regs = ar->hw_ce_regs->misc_regs;

	u32 misc_ie_addr = ath10k_ce_read32(ar,
			ce_ctrl_addr + ar->hw_ce_regs->misc_ie_addr);

	ath10k_ce_write32(ar,
			  ce_ctrl_addr + ar->hw_ce_regs->misc_ie_addr,
			  misc_ie_addr & ~(misc_regs->err_mask));
}

static inline void ath10k_ce_engine_int_status_clear(struct ath10k *ar,
						     u32 ce_ctrl_addr,
						     unsigned int mask)
{
	struct ath10k_hw_ce_host_wm_regs *wm_regs = ar->hw_ce_regs->wm_regs;

	ath10k_ce_write32(ar, ce_ctrl_addr + wm_regs->addr, mask);
}

/*
 * Guts of ath10k_ce_send.
 * The caller takes responsibility for any needed locking.
 */
static int _ath10k_ce_send_nolock(struct ath10k_ce_pipe *ce_state,
				  void *per_transfer_context,
				  dma_addr_t buffer,
				  unsigned int nbytes,
				  unsigned int transfer_id,
				  unsigned int flags)
{
	struct ath10k *ar = ce_state->ar;
	struct ath10k_ce_ring *src_ring = ce_state->src_ring;
	struct ce_desc *desc, sdesc;
	unsigned int nentries_mask = src_ring->nentries_mask;
	unsigned int sw_index = src_ring->sw_index;
	unsigned int write_index = src_ring->write_index;
	u32 ctrl_addr = ce_state->ctrl_addr;
	u32 desc_flags = 0;
	int ret = 0;

	if (nbytes > ce_state->src_sz_max)
		ath10k_warn(ar, "%s: send more we can (nbytes: %d, max: %d)\n",
			    __func__, nbytes, ce_state->src_sz_max);

	if (unlikely(CE_RING_DELTA(nentries_mask,
				   write_index, sw_index - 1) <= 0)) {
		ret = -ENOSR;
		goto exit;
	}

	desc = CE_SRC_RING_TO_DESC(src_ring->base_addr_owner_space,
				   write_index);

	desc_flags |= SM(transfer_id, CE_DESC_FLAGS_META_DATA);

	if (flags & CE_SEND_FLAG_GATHER)
		desc_flags |= CE_DESC_FLAGS_GATHER;
	if (flags & CE_SEND_FLAG_BYTE_SWAP)
		desc_flags |= CE_DESC_FLAGS_BYTE_SWAP;

	sdesc.addr   = __cpu_to_le32(buffer);
	sdesc.nbytes = __cpu_to_le16(nbytes);
	sdesc.flags  = __cpu_to_le16(desc_flags);

	*desc = sdesc;

	src_ring->per_transfer_context[write_index] = per_transfer_context;

	/* Update Source Ring Write Index */
	write_index = CE_RING_IDX_INCR(nentries_mask, write_index);

	/* WORKAROUND */
	if (!(flags & CE_SEND_FLAG_GATHER))
		ath10k_ce_src_ring_write_index_set(ar, ctrl_addr, write_index);

	src_ring->write_index = write_index;
exit:
	return ret;
}

static int _ath10k_ce_send_nolock_64(struct ath10k_ce_pipe *ce_state,
				     void *per_transfer_context,
				     dma_addr_t buffer,
				     unsigned int nbytes,
				     unsigned int transfer_id,
				     unsigned int flags)
{
	struct ath10k *ar = ce_state->ar;
	struct ath10k_ce_ring *src_ring = ce_state->src_ring;
	struct ce_desc_64 *desc, sdesc;
	unsigned int nentries_mask = src_ring->nentries_mask;
	unsigned int sw_index;
	unsigned int write_index = src_ring->write_index;
	u32 ctrl_addr = ce_state->ctrl_addr;
	__le32 *addr;
	u32 desc_flags = 0;
	int ret = 0;

	if (test_bit(ATH10K_FLAG_CRASH_FLUSH, &ar->dev_flags))
		return -ESHUTDOWN;

	if (nbytes > ce_state->src_sz_max)
		ath10k_warn(ar, "%s: send more we can (nbytes: %d, max: %d)\n",
			    __func__, nbytes, ce_state->src_sz_max);

	if (ar->hw_params.rri_on_ddr)
		sw_index = ath10k_ce_src_ring_read_index_from_ddr(ar, ce_state->id);
	else
		sw_index = src_ring->sw_index;

	if (unlikely(CE_RING_DELTA(nentries_mask,
				   write_index, sw_index - 1) <= 0)) {
		ret = -ENOSR;
		goto exit;
	}

	desc = CE_SRC_RING_TO_DESC_64(src_ring->base_addr_owner_space,
				      write_index);

	desc_flags |= SM(transfer_id, CE_DESC_FLAGS_META_DATA);

	if (flags & CE_SEND_FLAG_GATHER)
		desc_flags |= CE_DESC_FLAGS_GATHER;

	if (flags & CE_SEND_FLAG_BYTE_SWAP)
		desc_flags |= CE_DESC_FLAGS_BYTE_SWAP;

	addr = (__le32 *)&sdesc.addr;

	flags |= upper_32_bits(buffer) & CE_DESC_ADDR_HI_MASK;
	addr[0] = __cpu_to_le32(buffer);
	addr[1] = __cpu_to_le32(flags);
	if (flags & CE_SEND_FLAG_GATHER)
		addr[1] |= __cpu_to_le32(CE_WCN3990_DESC_FLAGS_GATHER);
	else
		addr[1] &= ~(__cpu_to_le32(CE_WCN3990_DESC_FLAGS_GATHER));

	sdesc.nbytes = __cpu_to_le16(nbytes);
	sdesc.flags  = __cpu_to_le16(desc_flags);

	*desc = sdesc;

	src_ring->per_transfer_context[write_index] = per_transfer_context;

	/* Update Source Ring Write Index */
	write_index = CE_RING_IDX_INCR(nentries_mask, write_index);

	if (!(flags & CE_SEND_FLAG_GATHER)) {
		if (ar->hw_params.shadow_reg_support)
			ath10k_ce_shadow_src_ring_write_index_set(ar, ce_state,
								  write_index);
		else
			ath10k_ce_src_ring_write_index_set(ar, ctrl_addr,
							   write_index);
	}

	src_ring->write_index = write_index;
exit:
	return ret;
}

int ath10k_ce_send_nolock(struct ath10k_ce_pipe *ce_state,
			  void *per_transfer_context,
			  dma_addr_t buffer,
			  unsigned int nbytes,
			  unsigned int transfer_id,
			  unsigned int flags)
{
	return ce_state->ops->ce_send_nolock(ce_state, per_transfer_context,
				    buffer, nbytes, transfer_id, flags);
}
EXPORT_SYMBOL(ath10k_ce_send_nolock);

void __ath10k_ce_send_revert(struct ath10k_ce_pipe *pipe)
{
	struct ath10k *ar = pipe->ar;
	struct ath10k_ce *ce = ath10k_ce_priv(ar);
	struct ath10k_ce_ring *src_ring = pipe->src_ring;
	u32 ctrl_addr = pipe->ctrl_addr;

	lockdep_assert_held(&ce->ce_lock);

	/*
	 * This function must be called only if there is an incomplete
	 * scatter-gather transfer (before index register is updated)
	 * that needs to be cleaned up.
	 */
	if (WARN_ON_ONCE(src_ring->write_index == src_ring->sw_index))
		return;

	if (WARN_ON_ONCE(src_ring->write_index ==
			 ath10k_ce_src_ring_write_index_get(ar, ctrl_addr)))
		return;

	src_ring->write_index--;
	src_ring->write_index &= src_ring->nentries_mask;

	src_ring->per_transfer_context[src_ring->write_index] = NULL;
}
EXPORT_SYMBOL(__ath10k_ce_send_revert);

int ath10k_ce_send(struct ath10k_ce_pipe *ce_state,
		   void *per_transfer_context,
		   dma_addr_t buffer,
		   unsigned int nbytes,
		   unsigned int transfer_id,
		   unsigned int flags)
{
	struct ath10k *ar = ce_state->ar;
	struct ath10k_ce *ce = ath10k_ce_priv(ar);
	int ret;

	spin_lock_bh(&ce->ce_lock);
	ret = ath10k_ce_send_nolock(ce_state, per_transfer_context,
				    buffer, nbytes, transfer_id, flags);
	spin_unlock_bh(&ce->ce_lock);

	return ret;
}
EXPORT_SYMBOL(ath10k_ce_send);

int ath10k_ce_num_free_src_entries(struct ath10k_ce_pipe *pipe)
{
	struct ath10k *ar = pipe->ar;
	struct ath10k_ce *ce = ath10k_ce_priv(ar);
	int delta;

	spin_lock_bh(&ce->ce_lock);
	delta = CE_RING_DELTA(pipe->src_ring->nentries_mask,
			      pipe->src_ring->write_index,
			      pipe->src_ring->sw_index - 1);
	spin_unlock_bh(&ce->ce_lock);

	return delta;
}
EXPORT_SYMBOL(ath10k_ce_num_free_src_entries);

int __ath10k_ce_rx_num_free_bufs(struct ath10k_ce_pipe *pipe)
{
	struct ath10k *ar = pipe->ar;
	struct ath10k_ce *ce = ath10k_ce_priv(ar);
	struct ath10k_ce_ring *dest_ring = pipe->dest_ring;
	unsigned int nentries_mask = dest_ring->nentries_mask;
	unsigned int write_index = dest_ring->write_index;
	unsigned int sw_index = dest_ring->sw_index;

	lockdep_assert_held(&ce->ce_lock);

	return CE_RING_DELTA(nentries_mask, write_index, sw_index - 1);
}
EXPORT_SYMBOL(__ath10k_ce_rx_num_free_bufs);

static int __ath10k_ce_rx_post_buf(struct ath10k_ce_pipe *pipe, void *ctx,
				   dma_addr_t paddr)
{
	struct ath10k *ar = pipe->ar;
	struct ath10k_ce *ce = ath10k_ce_priv(ar);
	struct ath10k_ce_ring *dest_ring = pipe->dest_ring;
	unsigned int nentries_mask = dest_ring->nentries_mask;
	unsigned int write_index = dest_ring->write_index;
	unsigned int sw_index = dest_ring->sw_index;
	struct ce_desc *base = dest_ring->base_addr_owner_space;
	struct ce_desc *desc = CE_DEST_RING_TO_DESC(base, write_index);
	u32 ctrl_addr = pipe->ctrl_addr;

	lockdep_assert_held(&ce->ce_lock);

	if ((pipe->id != 5) &&
	    CE_RING_DELTA(nentries_mask, write_index, sw_index - 1) == 0)
		return -ENOSPC;

	desc->addr = __cpu_to_le32(paddr);
	desc->nbytes = 0;

	dest_ring->per_transfer_context[write_index] = ctx;
	write_index = CE_RING_IDX_INCR(nentries_mask, write_index);
	ath10k_ce_dest_ring_write_index_set(ar, ctrl_addr, write_index);
	dest_ring->write_index = write_index;

	return 0;
}

static int __ath10k_ce_rx_post_buf_64(struct ath10k_ce_pipe *pipe,
				      void *ctx,
				      dma_addr_t paddr)
{
	struct ath10k *ar = pipe->ar;
	struct ath10k_ce *ce = ath10k_ce_priv(ar);
	struct ath10k_ce_ring *dest_ring = pipe->dest_ring;
	unsigned int nentries_mask = dest_ring->nentries_mask;
	unsigned int write_index = dest_ring->write_index;
	unsigned int sw_index = dest_ring->sw_index;
	struct ce_desc_64 *base = dest_ring->base_addr_owner_space;
	struct ce_desc_64 *desc =
			CE_DEST_RING_TO_DESC_64(base, write_index);
	u32 ctrl_addr = pipe->ctrl_addr;

	lockdep_assert_held(&ce->ce_lock);

	if (CE_RING_DELTA(nentries_mask, write_index, sw_index - 1) == 0)
		return -ENOSPC;

	desc->addr = __cpu_to_le64(paddr);
	desc->addr &= __cpu_to_le64(CE_DESC_ADDR_MASK);

	desc->nbytes = 0;

	dest_ring->per_transfer_context[write_index] = ctx;
	write_index = CE_RING_IDX_INCR(nentries_mask, write_index);
	ath10k_ce_dest_ring_write_index_set(ar, ctrl_addr, write_index);
	dest_ring->write_index = write_index;

	return 0;
}

void ath10k_ce_rx_update_write_idx(struct ath10k_ce_pipe *pipe, u32 nentries)
{
	struct ath10k *ar = pipe->ar;
	struct ath10k_ce_ring *dest_ring = pipe->dest_ring;
	unsigned int nentries_mask = dest_ring->nentries_mask;
	unsigned int write_index = dest_ring->write_index;
	u32 ctrl_addr = pipe->ctrl_addr;
	u32 cur_write_idx = ath10k_ce_dest_ring_write_index_get(ar, ctrl_addr);

	/* Prevent CE ring stuck issue that will occur when ring is full.
	 * Make sure that write index is 1 less than read index.
	 */
	if (((cur_write_idx + nentries) & nentries_mask) == dest_ring->sw_index)
		nentries -= 1;

	write_index = CE_RING_IDX_ADD(nentries_mask, write_index, nentries);
	ath10k_ce_dest_ring_write_index_set(ar, ctrl_addr, write_index);
	dest_ring->write_index = write_index;
}
EXPORT_SYMBOL(ath10k_ce_rx_update_write_idx);

int ath10k_ce_rx_post_buf(struct ath10k_ce_pipe *pipe, void *ctx,
			  dma_addr_t paddr)
{
	struct ath10k *ar = pipe->ar;
	struct ath10k_ce *ce = ath10k_ce_priv(ar);
	int ret;

	spin_lock_bh(&ce->ce_lock);
	ret = pipe->ops->ce_rx_post_buf(pipe, ctx, paddr);
	spin_unlock_bh(&ce->ce_lock);

	return ret;
}
EXPORT_SYMBOL(ath10k_ce_rx_post_buf);

/*
 * Guts of ath10k_ce_completed_recv_next.
 * The caller takes responsibility for any necessary locking.
 */
static int
	 _ath10k_ce_completed_recv_next_nolock(struct ath10k_ce_pipe *ce_state,
					       void **per_transfer_contextp,
					       unsigned int *nbytesp)
{
	struct ath10k_ce_ring *dest_ring = ce_state->dest_ring;
	unsigned int nentries_mask = dest_ring->nentries_mask;
	unsigned int sw_index = dest_ring->sw_index;

	struct ce_desc *base = dest_ring->base_addr_owner_space;
	struct ce_desc *desc = CE_DEST_RING_TO_DESC(base, sw_index);
	struct ce_desc sdesc;
	u16 nbytes;

	/* Copy in one go for performance reasons */
	sdesc = *desc;

	nbytes = __le16_to_cpu(sdesc.nbytes);
	if (nbytes == 0) {
		/*
		 * This closes a relatively unusual race where the Host
		 * sees the updated DRRI before the update to the
		 * corresponding descriptor has completed. We treat this
		 * as a descriptor that is not yet done.
		 */
		return -EIO;
	}

	desc->nbytes = 0;

	/* Return data from completed destination descriptor */
	*nbytesp = nbytes;

	if (per_transfer_contextp)
		*per_transfer_contextp =
			dest_ring->per_transfer_context[sw_index];

	/* Copy engine 5 (HTT Rx) will reuse the same transfer context.
	 * So update transfer context all CEs except CE5.
	 */
	if (ce_state->id != 5)
		dest_ring->per_transfer_context[sw_index] = NULL;

	/* Update sw_index */
	sw_index = CE_RING_IDX_INCR(nentries_mask, sw_index);
	dest_ring->sw_index = sw_index;

	return 0;
}

static int
_ath10k_ce_completed_recv_next_nolock_64(struct ath10k_ce_pipe *ce_state,
					 void **per_transfer_contextp,
					 unsigned int *nbytesp)
{
	struct ath10k_ce_ring *dest_ring = ce_state->dest_ring;
	unsigned int nentries_mask = dest_ring->nentries_mask;
	unsigned int sw_index = dest_ring->sw_index;
	struct ce_desc_64 *base = dest_ring->base_addr_owner_space;
	struct ce_desc_64 *desc =
		CE_DEST_RING_TO_DESC_64(base, sw_index);
	struct ce_desc_64 sdesc;
	u16 nbytes;

	/* Copy in one go for performance reasons */
	sdesc = *desc;

	nbytes = __le16_to_cpu(sdesc.nbytes);
	if (nbytes == 0) {
		/* This closes a relatively unusual race where the Host
		 * sees the updated DRRI before the update to the
		 * corresponding descriptor has completed. We treat this
		 * as a descriptor that is not yet done.
		 */
		return -EIO;
	}

	desc->nbytes = 0;

	/* Return data from completed destination descriptor */
	*nbytesp = nbytes;

	if (per_transfer_contextp)
		*per_transfer_contextp =
			dest_ring->per_transfer_context[sw_index];

	/* Copy engine 5 (HTT Rx) will reuse the same transfer context.
	 * So update transfer context all CEs except CE5.
	 */
	if (ce_state->id != 5)
		dest_ring->per_transfer_context[sw_index] = NULL;

	/* Update sw_index */
	sw_index = CE_RING_IDX_INCR(nentries_mask, sw_index);
	dest_ring->sw_index = sw_index;

	return 0;
}

int ath10k_ce_completed_recv_next_nolock(struct ath10k_ce_pipe *ce_state,
					 void **per_transfer_ctx,
					 unsigned int *nbytesp)
{
	return ce_state->ops->ce_completed_recv_next_nolock(ce_state,
							    per_transfer_ctx,
							    nbytesp);
}
EXPORT_SYMBOL(ath10k_ce_completed_recv_next_nolock);

int ath10k_ce_completed_recv_next(struct ath10k_ce_pipe *ce_state,
				  void **per_transfer_contextp,
				  unsigned int *nbytesp)
{
	struct ath10k *ar = ce_state->ar;
	struct ath10k_ce *ce = ath10k_ce_priv(ar);
	int ret;

	spin_lock_bh(&ce->ce_lock);
	ret = ce_state->ops->ce_completed_recv_next_nolock(ce_state,
						   per_transfer_contextp,
						   nbytesp);

	spin_unlock_bh(&ce->ce_lock);

	return ret;
}
EXPORT_SYMBOL(ath10k_ce_completed_recv_next);

static int _ath10k_ce_revoke_recv_next(struct ath10k_ce_pipe *ce_state,
				       void **per_transfer_contextp,
				       dma_addr_t *bufferp)
{
	struct ath10k_ce_ring *dest_ring;
	unsigned int nentries_mask;
	unsigned int sw_index;
	unsigned int write_index;
	int ret;
	struct ath10k *ar;
	struct ath10k_ce *ce;

	dest_ring = ce_state->dest_ring;

	if (!dest_ring)
		return -EIO;

	ar = ce_state->ar;
	ce = ath10k_ce_priv(ar);

	spin_lock_bh(&ce->ce_lock);

	nentries_mask = dest_ring->nentries_mask;
	sw_index = dest_ring->sw_index;
	write_index = dest_ring->write_index;
	if (write_index != sw_index) {
		struct ce_desc *base = dest_ring->base_addr_owner_space;
		struct ce_desc *desc = CE_DEST_RING_TO_DESC(base, sw_index);

		/* Return data from completed destination descriptor */
		*bufferp = __le32_to_cpu(desc->addr);

		if (per_transfer_contextp)
			*per_transfer_contextp =
				dest_ring->per_transfer_context[sw_index];

		/* sanity */
		dest_ring->per_transfer_context[sw_index] = NULL;
		desc->nbytes = 0;

		/* Update sw_index */
		sw_index = CE_RING_IDX_INCR(nentries_mask, sw_index);
		dest_ring->sw_index = sw_index;
		ret = 0;
	} else {
		ret = -EIO;
	}

	spin_unlock_bh(&ce->ce_lock);

	return ret;
}

static int _ath10k_ce_revoke_recv_next_64(struct ath10k_ce_pipe *ce_state,
					  void **per_transfer_contextp,
					  dma_addr_t *bufferp)
{
	struct ath10k_ce_ring *dest_ring;
	unsigned int nentries_mask;
	unsigned int sw_index;
	unsigned int write_index;
	int ret;
	struct ath10k *ar;
	struct ath10k_ce *ce;

	dest_ring = ce_state->dest_ring;

	if (!dest_ring)
		return -EIO;

	ar = ce_state->ar;
	ce = ath10k_ce_priv(ar);

	spin_lock_bh(&ce->ce_lock);

	nentries_mask = dest_ring->nentries_mask;
	sw_index = dest_ring->sw_index;
	write_index = dest_ring->write_index;
	if (write_index != sw_index) {
		struct ce_desc_64 *base = dest_ring->base_addr_owner_space;
		struct ce_desc_64 *desc =
			CE_DEST_RING_TO_DESC_64(base, sw_index);

		/* Return data from completed destination descriptor */
		*bufferp = __le64_to_cpu(desc->addr);

		if (per_transfer_contextp)
			*per_transfer_contextp =
				dest_ring->per_transfer_context[sw_index];

		/* sanity */
		dest_ring->per_transfer_context[sw_index] = NULL;
		desc->nbytes = 0;

		/* Update sw_index */
		sw_index = CE_RING_IDX_INCR(nentries_mask, sw_index);
		dest_ring->sw_index = sw_index;
		ret = 0;
	} else {
		ret = -EIO;
	}

	spin_unlock_bh(&ce->ce_lock);

	return ret;
}

int ath10k_ce_revoke_recv_next(struct ath10k_ce_pipe *ce_state,
			       void **per_transfer_contextp,
			       dma_addr_t *bufferp)
{
	return ce_state->ops->ce_revoke_recv_next(ce_state,
						  per_transfer_contextp,
						  bufferp);
}
EXPORT_SYMBOL(ath10k_ce_revoke_recv_next);

/*
 * Guts of ath10k_ce_completed_send_next.
 * The caller takes responsibility for any necessary locking.
 */
static int _ath10k_ce_completed_send_next_nolock(struct ath10k_ce_pipe *ce_state,
						 void **per_transfer_contextp)
{
	struct ath10k_ce_ring *src_ring = ce_state->src_ring;
	u32 ctrl_addr = ce_state->ctrl_addr;
	struct ath10k *ar = ce_state->ar;
	unsigned int nentries_mask = src_ring->nentries_mask;
	unsigned int sw_index = src_ring->sw_index;
	unsigned int read_index;
	struct ce_desc *desc;

	if (src_ring->hw_index == sw_index) {
		/*
		 * The SW completion index has caught up with the cached
		 * version of the HW completion index.
		 * Update the cached HW completion index to see whether
		 * the SW has really caught up to the HW, or if the cached
		 * value of the HW index has become stale.
		 */

		read_index = ath10k_ce_src_ring_read_index_get(ar, ctrl_addr);
		if (read_index == 0xffffffff)
			return -ENODEV;

		read_index &= nentries_mask;
		src_ring->hw_index = read_index;
	}

	if (ar->hw_params.rri_on_ddr)
		read_index = ath10k_ce_src_ring_read_index_get(ar, ctrl_addr);
	else
		read_index = src_ring->hw_index;

	if (read_index == sw_index)
		return -EIO;

	if (per_transfer_contextp)
		*per_transfer_contextp =
			src_ring->per_transfer_context[sw_index];

	/* sanity */
	src_ring->per_transfer_context[sw_index] = NULL;
	desc = CE_SRC_RING_TO_DESC(src_ring->base_addr_owner_space,
				   sw_index);
	desc->nbytes = 0;

	/* Update sw_index */
	sw_index = CE_RING_IDX_INCR(nentries_mask, sw_index);
	src_ring->sw_index = sw_index;

	return 0;
}

static int _ath10k_ce_completed_send_next_nolock_64(struct ath10k_ce_pipe *ce_state,
						    void **per_transfer_contextp)
{
	struct ath10k_ce_ring *src_ring = ce_state->src_ring;
	u32 ctrl_addr = ce_state->ctrl_addr;
	struct ath10k *ar = ce_state->ar;
	unsigned int nentries_mask = src_ring->nentries_mask;
	unsigned int sw_index = src_ring->sw_index;
	unsigned int read_index;
	struct ce_desc_64 *desc;

	if (src_ring->hw_index == sw_index) {
		/*
		 * The SW completion index has caught up with the cached
		 * version of the HW completion index.
		 * Update the cached HW completion index to see whether
		 * the SW has really caught up to the HW, or if the cached
		 * value of the HW index has become stale.
		 */

		read_index = ath10k_ce_src_ring_read_index_get(ar, ctrl_addr);
		if (read_index == 0xffffffff)
			return -ENODEV;

		read_index &= nentries_mask;
		src_ring->hw_index = read_index;
	}

	if (ar->hw_params.rri_on_ddr)
		read_index = ath10k_ce_src_ring_read_index_get(ar, ctrl_addr);
	else
		read_index = src_ring->hw_index;

	if (read_index == sw_index)
		return -EIO;

	if (per_transfer_contextp)
		*per_transfer_contextp =
			src_ring->per_transfer_context[sw_index];

	/* sanity */
	src_ring->per_transfer_context[sw_index] = NULL;
	desc = CE_SRC_RING_TO_DESC_64(src_ring->base_addr_owner_space,
				      sw_index);
	desc->nbytes = 0;

	/* Update sw_index */
	sw_index = CE_RING_IDX_INCR(nentries_mask, sw_index);
	src_ring->sw_index = sw_index;

	return 0;
}

int ath10k_ce_completed_send_next_nolock(struct ath10k_ce_pipe *ce_state,
					 void **per_transfer_contextp)
{
	return ce_state->ops->ce_completed_send_next_nolock(ce_state,
							    per_transfer_contextp);
}
EXPORT_SYMBOL(ath10k_ce_completed_send_next_nolock);

static void ath10k_ce_extract_desc_data(struct ath10k *ar,
					struct ath10k_ce_ring *src_ring,
					u32 sw_index,
					dma_addr_t *bufferp,
					u32 *nbytesp,
					u32 *transfer_idp)
{
		struct ce_desc *base = src_ring->base_addr_owner_space;
		struct ce_desc *desc = CE_SRC_RING_TO_DESC(base, sw_index);

		/* Return data from completed source descriptor */
		*bufferp = __le32_to_cpu(desc->addr);
		*nbytesp = __le16_to_cpu(desc->nbytes);
		*transfer_idp = MS(__le16_to_cpu(desc->flags),
				   CE_DESC_FLAGS_META_DATA);
}

static void ath10k_ce_extract_desc_data_64(struct ath10k *ar,
					   struct ath10k_ce_ring *src_ring,
					   u32 sw_index,
					   dma_addr_t *bufferp,
					   u32 *nbytesp,
					   u32 *transfer_idp)
{
		struct ce_desc_64 *base = src_ring->base_addr_owner_space;
		struct ce_desc_64 *desc =
			CE_SRC_RING_TO_DESC_64(base, sw_index);

		/* Return data from completed source descriptor */
		*bufferp = __le64_to_cpu(desc->addr);
		*nbytesp = __le16_to_cpu(desc->nbytes);
		*transfer_idp = MS(__le16_to_cpu(desc->flags),
				   CE_DESC_FLAGS_META_DATA);
}

/* NB: Modeled after ath10k_ce_completed_send_next */
int ath10k_ce_cancel_send_next(struct ath10k_ce_pipe *ce_state,
			       void **per_transfer_contextp,
			       dma_addr_t *bufferp,
			       unsigned int *nbytesp,
			       unsigned int *transfer_idp)
{
	struct ath10k_ce_ring *src_ring;
	unsigned int nentries_mask;
	unsigned int sw_index;
	unsigned int write_index;
	int ret;
	struct ath10k *ar;
	struct ath10k_ce *ce;

	src_ring = ce_state->src_ring;

	if (!src_ring)
		return -EIO;

	ar = ce_state->ar;
	ce = ath10k_ce_priv(ar);

	spin_lock_bh(&ce->ce_lock);

	nentries_mask = src_ring->nentries_mask;
	sw_index = src_ring->sw_index;
	write_index = src_ring->write_index;

	if (write_index != sw_index) {
		ce_state->ops->ce_extract_desc_data(ar, src_ring, sw_index,
						    bufferp, nbytesp,
						    transfer_idp);

		if (per_transfer_contextp)
			*per_transfer_contextp =
				src_ring->per_transfer_context[sw_index];

		/* sanity */
		src_ring->per_transfer_context[sw_index] = NULL;

		/* Update sw_index */
		sw_index = CE_RING_IDX_INCR(nentries_mask, sw_index);
		src_ring->sw_index = sw_index;
		ret = 0;
	} else {
		ret = -EIO;
	}

	spin_unlock_bh(&ce->ce_lock);

	return ret;
}
EXPORT_SYMBOL(ath10k_ce_cancel_send_next);

int ath10k_ce_completed_send_next(struct ath10k_ce_pipe *ce_state,
				  void **per_transfer_contextp)
{
	struct ath10k *ar = ce_state->ar;
	struct ath10k_ce *ce = ath10k_ce_priv(ar);
	int ret;

	spin_lock_bh(&ce->ce_lock);
	ret = ath10k_ce_completed_send_next_nolock(ce_state,
						   per_transfer_contextp);
	spin_unlock_bh(&ce->ce_lock);

	return ret;
}
EXPORT_SYMBOL(ath10k_ce_completed_send_next);

/*
 * Guts of interrupt handler for per-engine interrupts on a particular CE.
 *
 * Invokes registered callbacks for recv_complete,
 * send_complete, and watermarks.
 */
void ath10k_ce_per_engine_service(struct ath10k *ar, unsigned int ce_id)
{
	struct ath10k_ce *ce = ath10k_ce_priv(ar);
	struct ath10k_ce_pipe *ce_state = &ce->ce_states[ce_id];
	struct ath10k_hw_ce_host_wm_regs *wm_regs = ar->hw_ce_regs->wm_regs;
	u32 ctrl_addr = ce_state->ctrl_addr;

	/*
	 * Clear before handling
	 *
	 * Misc CE interrupts are not being handled, but still need
	 * to be cleared.
	 *
	 * NOTE: When the last copy engine interrupt is cleared the
	 * hardware will go to sleep.  Once this happens any access to
	 * the CE registers can cause a hardware fault.
	 */
	ath10k_ce_engine_int_status_clear(ar, ctrl_addr,
					  wm_regs->cc_mask | wm_regs->wm_mask);

	if (ce_state->recv_cb)
		ce_state->recv_cb(ce_state);

	if (ce_state->send_cb)
		ce_state->send_cb(ce_state);
}
EXPORT_SYMBOL(ath10k_ce_per_engine_service);

/*
 * Handler for per-engine interrupts on ALL active CEs.
 * This is used in cases where the system is sharing a
 * single interrupt for all CEs
 */

void ath10k_ce_per_engine_service_any(struct ath10k *ar)
{
	int ce_id;
	u32 intr_summary;

	intr_summary = ath10k_ce_interrupt_summary(ar);

	for (ce_id = 0; intr_summary && (ce_id < CE_COUNT); ce_id++) {
		if (intr_summary & (1 << ce_id))
			intr_summary &= ~(1 << ce_id);
		else
			/* no intr pending on this CE */
			continue;

		ath10k_ce_per_engine_service(ar, ce_id);
	}
}
EXPORT_SYMBOL(ath10k_ce_per_engine_service_any);

/*
 * Adjust interrupts for the copy complete handler.
 * If it's needed for either send or recv, then unmask
 * this interrupt; otherwise, mask it.
 *
 * Called with ce_lock held.
 */
static void ath10k_ce_per_engine_handler_adjust(struct ath10k_ce_pipe *ce_state)
{
	u32 ctrl_addr = ce_state->ctrl_addr;
	struct ath10k *ar = ce_state->ar;
	bool disable_copy_compl_intr = ce_state->attr_flags & CE_ATTR_DIS_INTR;

	if ((!disable_copy_compl_intr) &&
	    (ce_state->send_cb || ce_state->recv_cb))
		ath10k_ce_copy_complete_inter_enable(ar, ctrl_addr);
	else
		ath10k_ce_copy_complete_intr_disable(ar, ctrl_addr);

	ath10k_ce_watermark_intr_disable(ar, ctrl_addr);
}

void ath10k_ce_disable_interrupt(struct ath10k *ar, int ce_id)
{
	struct ath10k_ce *ce = ath10k_ce_priv(ar);
	struct ath10k_ce_pipe *ce_state;
	u32 ctrl_addr;

	ce_state  = &ce->ce_states[ce_id];
	if (ce_state->attr_flags & CE_ATTR_POLL)
		return;

	ctrl_addr = ath10k_ce_base_address(ar, ce_id);

	ath10k_ce_copy_complete_intr_disable(ar, ctrl_addr);
	ath10k_ce_error_intr_disable(ar, ctrl_addr);
	ath10k_ce_watermark_intr_disable(ar, ctrl_addr);
}
EXPORT_SYMBOL(ath10k_ce_disable_interrupt);

void ath10k_ce_disable_interrupts(struct ath10k *ar)
{
	int ce_id;

	for (ce_id = 0; ce_id < CE_COUNT; ce_id++)
		ath10k_ce_disable_interrupt(ar, ce_id);
}
EXPORT_SYMBOL(ath10k_ce_disable_interrupts);

void ath10k_ce_enable_interrupt(struct ath10k *ar, int ce_id)
{
	struct ath10k_ce *ce = ath10k_ce_priv(ar);
	struct ath10k_ce_pipe *ce_state;

	ce_state  = &ce->ce_states[ce_id];
	if (ce_state->attr_flags & CE_ATTR_POLL)
		return;

	ath10k_ce_per_engine_handler_adjust(ce_state);
}
EXPORT_SYMBOL(ath10k_ce_enable_interrupt);

void ath10k_ce_enable_interrupts(struct ath10k *ar)
{
	int ce_id;

	/* Enable interrupts for copy engine that
	 * are not using polling mode.
	 */
	for (ce_id = 0; ce_id < CE_COUNT; ce_id++)
		ath10k_ce_enable_interrupt(ar, ce_id);
}
EXPORT_SYMBOL(ath10k_ce_enable_interrupts);

static int ath10k_ce_init_src_ring(struct ath10k *ar,
				   unsigned int ce_id,
				   const struct ce_attr *attr)
{
	struct ath10k_ce *ce = ath10k_ce_priv(ar);
	struct ath10k_ce_pipe *ce_state = &ce->ce_states[ce_id];
	struct ath10k_ce_ring *src_ring = ce_state->src_ring;
	u32 nentries, ctrl_addr = ath10k_ce_base_address(ar, ce_id);

	nentries = roundup_pow_of_two(attr->src_nentries);

	if (ar->hw_params.target_64bit)
		memset(src_ring->base_addr_owner_space, 0,
		       nentries * sizeof(struct ce_desc_64));
	else
		memset(src_ring->base_addr_owner_space, 0,
		       nentries * sizeof(struct ce_desc));

	src_ring->sw_index = ath10k_ce_src_ring_read_index_get(ar, ctrl_addr);
	src_ring->sw_index &= src_ring->nentries_mask;
	src_ring->hw_index = src_ring->sw_index;

	src_ring->write_index =
		ath10k_ce_src_ring_write_index_get(ar, ctrl_addr);
	src_ring->write_index &= src_ring->nentries_mask;

	ath10k_ce_src_ring_base_addr_set(ar, ce_id,
					 src_ring->base_addr_ce_space);
	ath10k_ce_src_ring_size_set(ar, ctrl_addr, nentries);
	ath10k_ce_src_ring_dmax_set(ar, ctrl_addr, attr->src_sz_max);
	ath10k_ce_src_ring_byte_swap_set(ar, ctrl_addr, 0);
	ath10k_ce_src_ring_lowmark_set(ar, ctrl_addr, 0);
	ath10k_ce_src_ring_highmark_set(ar, ctrl_addr, nentries);

	ath10k_dbg(ar, ATH10K_DBG_BOOT,
		   "boot init ce src ring id %d entries %d base_addr %p\n",
		   ce_id, nentries, src_ring->base_addr_owner_space);

	return 0;
}

static int ath10k_ce_init_dest_ring(struct ath10k *ar,
				    unsigned int ce_id,
				    const struct ce_attr *attr)
{
	struct ath10k_ce *ce = ath10k_ce_priv(ar);
	struct ath10k_ce_pipe *ce_state = &ce->ce_states[ce_id];
	struct ath10k_ce_ring *dest_ring = ce_state->dest_ring;
	u32 nentries, ctrl_addr = ath10k_ce_base_address(ar, ce_id);

	nentries = roundup_pow_of_two(attr->dest_nentries);

	if (ar->hw_params.target_64bit)
		memset(dest_ring->base_addr_owner_space, 0,
		       nentries * sizeof(struct ce_desc_64));
	else
		memset(dest_ring->base_addr_owner_space, 0,
		       nentries * sizeof(struct ce_desc));

	dest_ring->sw_index = ath10k_ce_dest_ring_read_index_get(ar, ctrl_addr);
	dest_ring->sw_index &= dest_ring->nentries_mask;
	dest_ring->write_index =
		ath10k_ce_dest_ring_write_index_get(ar, ctrl_addr);
	dest_ring->write_index &= dest_ring->nentries_mask;

	ath10k_ce_dest_ring_base_addr_set(ar, ce_id,
					  dest_ring->base_addr_ce_space);
	ath10k_ce_dest_ring_size_set(ar, ctrl_addr, nentries);
	ath10k_ce_dest_ring_byte_swap_set(ar, ctrl_addr, 0);
	ath10k_ce_dest_ring_lowmark_set(ar, ctrl_addr, 0);
	ath10k_ce_dest_ring_highmark_set(ar, ctrl_addr, nentries);

	ath10k_dbg(ar, ATH10K_DBG_BOOT,
		   "boot ce dest ring id %d entries %d base_addr %p\n",
		   ce_id, nentries, dest_ring->base_addr_owner_space);

	return 0;
}

static int ath10k_ce_alloc_shadow_base(struct ath10k *ar,
				       struct ath10k_ce_ring *src_ring,
				       u32 nentries)
{
	src_ring->shadow_base_unaligned = kcalloc(nentries,
						  sizeof(struct ce_desc_64),
						  GFP_KERNEL);
	if (!src_ring->shadow_base_unaligned)
		return -ENOMEM;

	src_ring->shadow_base = (struct ce_desc_64 *)
			PTR_ALIGN(src_ring->shadow_base_unaligned,
				  CE_DESC_RING_ALIGN);
	return 0;
}

static struct ath10k_ce_ring *
ath10k_ce_alloc_src_ring(struct ath10k *ar, unsigned int ce_id,
			 const struct ce_attr *attr)
{
	struct ath10k_ce_ring *src_ring;
	u32 nentries = attr->src_nentries;
	dma_addr_t base_addr;
	int ret;

	nentries = roundup_pow_of_two(nentries);

	src_ring = kzalloc(struct_size(src_ring, per_transfer_context,
				       nentries), GFP_KERNEL);
	if (src_ring == NULL)
		return ERR_PTR(-ENOMEM);

	src_ring->nentries = nentries;
	src_ring->nentries_mask = nentries - 1;

	/*
	 * Legacy platforms that do not support cache
	 * coherent DMA are unsupported
	 */
	src_ring->base_addr_owner_space_unaligned =
		dma_alloc_coherent(ar->dev,
				   (nentries * sizeof(struct ce_desc) +
				    CE_DESC_RING_ALIGN),
				   &base_addr, GFP_KERNEL);
	if (!src_ring->base_addr_owner_space_unaligned) {
		kfree(src_ring);
		return ERR_PTR(-ENOMEM);
	}

	src_ring->base_addr_ce_space_unaligned = base_addr;

	src_ring->base_addr_owner_space =
			PTR_ALIGN(src_ring->base_addr_owner_space_unaligned,
				  CE_DESC_RING_ALIGN);
	src_ring->base_addr_ce_space =
			ALIGN(src_ring->base_addr_ce_space_unaligned,
			      CE_DESC_RING_ALIGN);

	if (ar->hw_params.shadow_reg_support) {
		ret = ath10k_ce_alloc_shadow_base(ar, src_ring, nentries);
		if (ret) {
			dma_free_coherent(ar->dev,
					  (nentries * sizeof(struct ce_desc) +
					   CE_DESC_RING_ALIGN),
					  src_ring->base_addr_owner_space_unaligned,
					  base_addr);
			kfree(src_ring);
			return ERR_PTR(ret);
		}
	}

	return src_ring;
}

static struct ath10k_ce_ring *
ath10k_ce_alloc_src_ring_64(struct ath10k *ar, unsigned int ce_id,
			    const struct ce_attr *attr)
{
	struct ath10k_ce_ring *src_ring;
	u32 nentries = attr->src_nentries;
	dma_addr_t base_addr;
	int ret;

	nentries = roundup_pow_of_two(nentries);

	src_ring = kzalloc(struct_size(src_ring, per_transfer_context,
				       nentries), GFP_KERNEL);
	if (!src_ring)
		return ERR_PTR(-ENOMEM);

	src_ring->nentries = nentries;
	src_ring->nentries_mask = nentries - 1;

	/* Legacy platforms that do not support cache
	 * coherent DMA are unsupported
	 */
	src_ring->base_addr_owner_space_unaligned =
		dma_alloc_coherent(ar->dev,
				   (nentries * sizeof(struct ce_desc_64) +
				    CE_DESC_RING_ALIGN),
				   &base_addr, GFP_KERNEL);
	if (!src_ring->base_addr_owner_space_unaligned) {
		kfree(src_ring);
		return ERR_PTR(-ENOMEM);
	}

	src_ring->base_addr_ce_space_unaligned = base_addr;

	src_ring->base_addr_owner_space =
			PTR_ALIGN(src_ring->base_addr_owner_space_unaligned,
				  CE_DESC_RING_ALIGN);
	src_ring->base_addr_ce_space =
			ALIGN(src_ring->base_addr_ce_space_unaligned,
			      CE_DESC_RING_ALIGN);

	if (ar->hw_params.shadow_reg_support) {
		ret = ath10k_ce_alloc_shadow_base(ar, src_ring, nentries);
		if (ret) {
			dma_free_coherent(ar->dev,
					  (nentries * sizeof(struct ce_desc_64) +
					   CE_DESC_RING_ALIGN),
					  src_ring->base_addr_owner_space_unaligned,
					  base_addr);
			kfree(src_ring);
			return ERR_PTR(ret);
		}
	}

	return src_ring;
}

static struct ath10k_ce_ring *
ath10k_ce_alloc_dest_ring(struct ath10k *ar, unsigned int ce_id,
			  const struct ce_attr *attr)
{
	struct ath10k_ce_ring *dest_ring;
	u32 nentries;
	dma_addr_t base_addr;

	nentries = roundup_pow_of_two(attr->dest_nentries);

	dest_ring = kzalloc(struct_size(dest_ring, per_transfer_context,
					nentries), GFP_KERNEL);
	if (dest_ring == NULL)
		return ERR_PTR(-ENOMEM);

	dest_ring->nentries = nentries;
	dest_ring->nentries_mask = nentries - 1;

	/*
	 * Legacy platforms that do not support cache
	 * coherent DMA are unsupported
	 */
	dest_ring->base_addr_owner_space_unaligned =
		dma_alloc_coherent(ar->dev,
				   (nentries * sizeof(struct ce_desc) +
				    CE_DESC_RING_ALIGN),
				   &base_addr, GFP_KERNEL);
	if (!dest_ring->base_addr_owner_space_unaligned) {
		kfree(dest_ring);
		return ERR_PTR(-ENOMEM);
	}

	dest_ring->base_addr_ce_space_unaligned = base_addr;

	dest_ring->base_addr_owner_space =
			PTR_ALIGN(dest_ring->base_addr_owner_space_unaligned,
				  CE_DESC_RING_ALIGN);
	dest_ring->base_addr_ce_space =
				ALIGN(dest_ring->base_addr_ce_space_unaligned,
				      CE_DESC_RING_ALIGN);

	return dest_ring;
}

static struct ath10k_ce_ring *
ath10k_ce_alloc_dest_ring_64(struct ath10k *ar, unsigned int ce_id,
			     const struct ce_attr *attr)
{
	struct ath10k_ce_ring *dest_ring;
	u32 nentries;
	dma_addr_t base_addr;

	nentries = roundup_pow_of_two(attr->dest_nentries);

	dest_ring = kzalloc(struct_size(dest_ring, per_transfer_context,
					nentries), GFP_KERNEL);
	if (!dest_ring)
		return ERR_PTR(-ENOMEM);

	dest_ring->nentries = nentries;
	dest_ring->nentries_mask = nentries - 1;

	/* Legacy platforms that do not support cache
	 * coherent DMA are unsupported
	 */
	dest_ring->base_addr_owner_space_unaligned =
		dma_alloc_coherent(ar->dev,
				   (nentries * sizeof(struct ce_desc_64) +
				    CE_DESC_RING_ALIGN),
				   &base_addr, GFP_KERNEL);
	if (!dest_ring->base_addr_owner_space_unaligned) {
		kfree(dest_ring);
		return ERR_PTR(-ENOMEM);
	}

	dest_ring->base_addr_ce_space_unaligned = base_addr;

	/* Correctly initialize memory to 0 to prevent garbage
	 * data crashing system when download firmware
	 */
	dest_ring->base_addr_owner_space =
			PTR_ALIGN(dest_ring->base_addr_owner_space_unaligned,
				  CE_DESC_RING_ALIGN);
	dest_ring->base_addr_ce_space =
			ALIGN(dest_ring->base_addr_ce_space_unaligned,
			      CE_DESC_RING_ALIGN);

	return dest_ring;
}

/*
 * Initialize a Copy Engine based on caller-supplied attributes.
 * This may be called once to initialize both source and destination
 * rings or it may be called twice for separate source and destination
 * initialization. It may be that only one side or the other is
 * initialized by software/firmware.
 */
int ath10k_ce_init_pipe(struct ath10k *ar, unsigned int ce_id,
			const struct ce_attr *attr)
{
	int ret;

	if (attr->src_nentries) {
		ret = ath10k_ce_init_src_ring(ar, ce_id, attr);
		if (ret) {
			ath10k_err(ar, "Failed to initialize CE src ring for ID: %d (%d)\n",
				   ce_id, ret);
			return ret;
		}
	}

	if (attr->dest_nentries) {
		ret = ath10k_ce_init_dest_ring(ar, ce_id, attr);
		if (ret) {
			ath10k_err(ar, "Failed to initialize CE dest ring for ID: %d (%d)\n",
				   ce_id, ret);
			return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL(ath10k_ce_init_pipe);

static void ath10k_ce_deinit_src_ring(struct ath10k *ar, unsigned int ce_id)
{
	u32 ctrl_addr = ath10k_ce_base_address(ar, ce_id);

	ath10k_ce_src_ring_base_addr_set(ar, ce_id, 0);
	ath10k_ce_src_ring_size_set(ar, ctrl_addr, 0);
	ath10k_ce_src_ring_dmax_set(ar, ctrl_addr, 0);
	ath10k_ce_src_ring_highmark_set(ar, ctrl_addr, 0);
}

static void ath10k_ce_deinit_dest_ring(struct ath10k *ar, unsigned int ce_id)
{
	u32 ctrl_addr = ath10k_ce_base_address(ar, ce_id);

	ath10k_ce_dest_ring_base_addr_set(ar, ce_id, 0);
	ath10k_ce_dest_ring_size_set(ar, ctrl_addr, 0);
	ath10k_ce_dest_ring_highmark_set(ar, ctrl_addr, 0);
}

void ath10k_ce_deinit_pipe(struct ath10k *ar, unsigned int ce_id)
{
	ath10k_ce_deinit_src_ring(ar, ce_id);
	ath10k_ce_deinit_dest_ring(ar, ce_id);
}
EXPORT_SYMBOL(ath10k_ce_deinit_pipe);

static void _ath10k_ce_free_pipe(struct ath10k *ar, int ce_id)
{
	struct ath10k_ce *ce = ath10k_ce_priv(ar);
	struct ath10k_ce_pipe *ce_state = &ce->ce_states[ce_id];

	if (ce_state->src_ring) {
		if (ar->hw_params.shadow_reg_support)
			kfree(ce_state->src_ring->shadow_base_unaligned);
		dma_free_coherent(ar->dev,
				  (ce_state->src_ring->nentries *
				   sizeof(struct ce_desc) +
				   CE_DESC_RING_ALIGN),
				  ce_state->src_ring->base_addr_owner_space,
				  ce_state->src_ring->base_addr_ce_space);
		kfree(ce_state->src_ring);
	}

	if (ce_state->dest_ring) {
		dma_free_coherent(ar->dev,
				  (ce_state->dest_ring->nentries *
				   sizeof(struct ce_desc) +
				   CE_DESC_RING_ALIGN),
				  ce_state->dest_ring->base_addr_owner_space,
				  ce_state->dest_ring->base_addr_ce_space);
		kfree(ce_state->dest_ring);
	}

	ce_state->src_ring = NULL;
	ce_state->dest_ring = NULL;
}

static void _ath10k_ce_free_pipe_64(struct ath10k *ar, int ce_id)
{
	struct ath10k_ce *ce = ath10k_ce_priv(ar);
	struct ath10k_ce_pipe *ce_state = &ce->ce_states[ce_id];

	if (ce_state->src_ring) {
		if (ar->hw_params.shadow_reg_support)
			kfree(ce_state->src_ring->shadow_base_unaligned);
		dma_free_coherent(ar->dev,
				  (ce_state->src_ring->nentries *
				   sizeof(struct ce_desc_64) +
				   CE_DESC_RING_ALIGN),
				  ce_state->src_ring->base_addr_owner_space,
				  ce_state->src_ring->base_addr_ce_space);
		kfree(ce_state->src_ring);
	}

	if (ce_state->dest_ring) {
		dma_free_coherent(ar->dev,
				  (ce_state->dest_ring->nentries *
				   sizeof(struct ce_desc_64) +
				   CE_DESC_RING_ALIGN),
				  ce_state->dest_ring->base_addr_owner_space,
				  ce_state->dest_ring->base_addr_ce_space);
		kfree(ce_state->dest_ring);
	}

	ce_state->src_ring = NULL;
	ce_state->dest_ring = NULL;
}

void ath10k_ce_free_pipe(struct ath10k *ar, int ce_id)
{
	struct ath10k_ce *ce = ath10k_ce_priv(ar);
	struct ath10k_ce_pipe *ce_state = &ce->ce_states[ce_id];

	ce_state->ops->ce_free_pipe(ar, ce_id);
}
EXPORT_SYMBOL(ath10k_ce_free_pipe);

void ath10k_ce_dump_registers(struct ath10k *ar,
			      struct ath10k_fw_crash_data *crash_data)
{
	struct ath10k_ce *ce = ath10k_ce_priv(ar);
	struct ath10k_ce_crash_data ce_data;
	u32 addr, id;

	lockdep_assert_held(&ar->dump_mutex);

	ath10k_err(ar, "Copy Engine register dump:\n");

	spin_lock_bh(&ce->ce_lock);
	for (id = 0; id < CE_COUNT; id++) {
		addr = ath10k_ce_base_address(ar, id);
		ce_data.base_addr = cpu_to_le32(addr);

		ce_data.src_wr_idx =
			cpu_to_le32(ath10k_ce_src_ring_write_index_get(ar, addr));
		ce_data.src_r_idx =
			cpu_to_le32(ath10k_ce_src_ring_read_index_get(ar, addr));
		ce_data.dst_wr_idx =
			cpu_to_le32(ath10k_ce_dest_ring_write_index_get(ar, addr));
		ce_data.dst_r_idx =
			cpu_to_le32(ath10k_ce_dest_ring_read_index_get(ar, addr));

		if (crash_data)
			crash_data->ce_crash_data[id] = ce_data;

		ath10k_err(ar, "[%02d]: 0x%08x %3u %3u %3u %3u", id,
			   le32_to_cpu(ce_data.base_addr),
			   le32_to_cpu(ce_data.src_wr_idx),
			   le32_to_cpu(ce_data.src_r_idx),
			   le32_to_cpu(ce_data.dst_wr_idx),
			   le32_to_cpu(ce_data.dst_r_idx));
	}

	spin_unlock_bh(&ce->ce_lock);
}
EXPORT_SYMBOL(ath10k_ce_dump_registers);

static const struct ath10k_ce_ops ce_ops = {
	.ce_alloc_src_ring = ath10k_ce_alloc_src_ring,
	.ce_alloc_dst_ring = ath10k_ce_alloc_dest_ring,
	.ce_rx_post_buf = __ath10k_ce_rx_post_buf,
	.ce_completed_recv_next_nolock = _ath10k_ce_completed_recv_next_nolock,
	.ce_revoke_recv_next = _ath10k_ce_revoke_recv_next,
	.ce_extract_desc_data = ath10k_ce_extract_desc_data,
	.ce_free_pipe = _ath10k_ce_free_pipe,
	.ce_send_nolock = _ath10k_ce_send_nolock,
	.ce_set_src_ring_base_addr_hi = NULL,
	.ce_set_dest_ring_base_addr_hi = NULL,
	.ce_completed_send_next_nolock = _ath10k_ce_completed_send_next_nolock,
};

static const struct ath10k_ce_ops ce_64_ops = {
	.ce_alloc_src_ring = ath10k_ce_alloc_src_ring_64,
	.ce_alloc_dst_ring = ath10k_ce_alloc_dest_ring_64,
	.ce_rx_post_buf = __ath10k_ce_rx_post_buf_64,
	.ce_completed_recv_next_nolock =
				_ath10k_ce_completed_recv_next_nolock_64,
	.ce_revoke_recv_next = _ath10k_ce_revoke_recv_next_64,
	.ce_extract_desc_data = ath10k_ce_extract_desc_data_64,
	.ce_free_pipe = _ath10k_ce_free_pipe_64,
	.ce_send_nolock = _ath10k_ce_send_nolock_64,
	.ce_set_src_ring_base_addr_hi = ath10k_ce_set_src_ring_base_addr_hi,
	.ce_set_dest_ring_base_addr_hi = ath10k_ce_set_dest_ring_base_addr_hi,
	.ce_completed_send_next_nolock = _ath10k_ce_completed_send_next_nolock_64,
};

static void ath10k_ce_set_ops(struct ath10k *ar,
			      struct ath10k_ce_pipe *ce_state)
{
	switch (ar->hw_rev) {
	case ATH10K_HW_WCN3990:
		ce_state->ops = &ce_64_ops;
		break;
	default:
		ce_state->ops = &ce_ops;
		break;
	}
}

int ath10k_ce_alloc_pipe(struct ath10k *ar, int ce_id,
			 const struct ce_attr *attr)
{
	struct ath10k_ce *ce = ath10k_ce_priv(ar);
	struct ath10k_ce_pipe *ce_state = &ce->ce_states[ce_id];
	int ret;

	ath10k_ce_set_ops(ar, ce_state);
	/* Make sure there's enough CE ringbuffer entries for HTT TX to avoid
	 * additional TX locking checks.
	 *
	 * For the lack of a better place do the check here.
	 */
	BUILD_BUG_ON(2 * TARGET_NUM_MSDU_DESC >
		     (CE_HTT_H2T_MSG_SRC_NENTRIES - 1));
	BUILD_BUG_ON(2 * TARGET_10_4_NUM_MSDU_DESC_PFC >
		     (CE_HTT_H2T_MSG_SRC_NENTRIES - 1));
	BUILD_BUG_ON(2 * TARGET_TLV_NUM_MSDU_DESC >
		     (CE_HTT_H2T_MSG_SRC_NENTRIES - 1));

	ce_state->ar = ar;
	ce_state->id = ce_id;
	ce_state->ctrl_addr = ath10k_ce_base_address(ar, ce_id);
	ce_state->attr_flags = attr->flags;
	ce_state->src_sz_max = attr->src_sz_max;

	if (attr->src_nentries)
		ce_state->send_cb = attr->send_cb;

	if (attr->dest_nentries)
		ce_state->recv_cb = attr->recv_cb;

	if (attr->src_nentries) {
		ce_state->src_ring =
			ce_state->ops->ce_alloc_src_ring(ar, ce_id, attr);
		if (IS_ERR(ce_state->src_ring)) {
			ret = PTR_ERR(ce_state->src_ring);
			ath10k_err(ar, "failed to alloc CE src ring %d: %d\n",
				   ce_id, ret);
			ce_state->src_ring = NULL;
			return ret;
		}
	}

	if (attr->dest_nentries) {
		ce_state->dest_ring = ce_state->ops->ce_alloc_dst_ring(ar,
									ce_id,
									attr);
		if (IS_ERR(ce_state->dest_ring)) {
			ret = PTR_ERR(ce_state->dest_ring);
			ath10k_err(ar, "failed to alloc CE dest ring %d: %d\n",
				   ce_id, ret);
			ce_state->dest_ring = NULL;
			return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL(ath10k_ce_alloc_pipe);

void ath10k_ce_alloc_rri(struct ath10k *ar)
{
	int i;
	u32 value;
	u32 ctrl1_regs;
	u32 ce_base_addr;
	struct ath10k_ce *ce = ath10k_ce_priv(ar);

	ce->vaddr_rri = dma_alloc_coherent(ar->dev,
					   (CE_COUNT * sizeof(u32)),
					   &ce->paddr_rri, GFP_KERNEL);

	if (!ce->vaddr_rri)
		return;

	ath10k_ce_write32(ar, ar->hw_ce_regs->ce_rri_low,
			  lower_32_bits(ce->paddr_rri));
	ath10k_ce_write32(ar, ar->hw_ce_regs->ce_rri_high,
			  (upper_32_bits(ce->paddr_rri) &
			  CE_DESC_ADDR_HI_MASK));

	for (i = 0; i < CE_COUNT; i++) {
		ctrl1_regs = ar->hw_ce_regs->ctrl1_regs->addr;
		ce_base_addr = ath10k_ce_base_address(ar, i);
		value = ath10k_ce_read32(ar, ce_base_addr + ctrl1_regs);
		value |= ar->hw_ce_regs->upd->mask;
		ath10k_ce_write32(ar, ce_base_addr + ctrl1_regs, value);
	}
}
EXPORT_SYMBOL(ath10k_ce_alloc_rri);

void ath10k_ce_free_rri(struct ath10k *ar)
{
	struct ath10k_ce *ce = ath10k_ce_priv(ar);

	dma_free_coherent(ar->dev, (CE_COUNT * sizeof(u32)),
			  ce->vaddr_rri,
			  ce->paddr_rri);
}
EXPORT_SYMBOL(ath10k_ce_free_rri);
