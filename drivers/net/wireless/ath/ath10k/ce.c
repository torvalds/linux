/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2013 Qualcomm Atheros, Inc.
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

#include "hif.h"
#include "pci.h"
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
 * Typically, one side of the PCIe interconnect (Host or Target)
 * controls one ring and the other side controls the other ring.
 * The source side chooses when to initiate a transfer and it
 * chooses what to send (buffer address, length). The destination
 * side keeps a supply of "anonymous receive buffers" available and
 * it handles incoming data as it arrives (when the destination
 * recieves an interrupt).
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

static inline void ath10k_ce_dest_ring_write_index_set(struct ath10k *ar,
						       u32 ce_ctrl_addr,
						       unsigned int n)
{
	ath10k_pci_write32(ar, ce_ctrl_addr + DST_WR_INDEX_ADDRESS, n);
}

static inline u32 ath10k_ce_dest_ring_write_index_get(struct ath10k *ar,
						      u32 ce_ctrl_addr)
{
	return ath10k_pci_read32(ar, ce_ctrl_addr + DST_WR_INDEX_ADDRESS);
}

static inline void ath10k_ce_src_ring_write_index_set(struct ath10k *ar,
						      u32 ce_ctrl_addr,
						      unsigned int n)
{
	ath10k_pci_write32(ar, ce_ctrl_addr + SR_WR_INDEX_ADDRESS, n);
}

static inline u32 ath10k_ce_src_ring_write_index_get(struct ath10k *ar,
						     u32 ce_ctrl_addr)
{
	return ath10k_pci_read32(ar, ce_ctrl_addr + SR_WR_INDEX_ADDRESS);
}

static inline u32 ath10k_ce_src_ring_read_index_get(struct ath10k *ar,
						    u32 ce_ctrl_addr)
{
	return ath10k_pci_read32(ar, ce_ctrl_addr + CURRENT_SRRI_ADDRESS);
}

static inline void ath10k_ce_src_ring_base_addr_set(struct ath10k *ar,
						    u32 ce_ctrl_addr,
						    unsigned int addr)
{
	ath10k_pci_write32(ar, ce_ctrl_addr + SR_BA_ADDRESS, addr);
}

static inline void ath10k_ce_src_ring_size_set(struct ath10k *ar,
					       u32 ce_ctrl_addr,
					       unsigned int n)
{
	ath10k_pci_write32(ar, ce_ctrl_addr + SR_SIZE_ADDRESS, n);
}

static inline void ath10k_ce_src_ring_dmax_set(struct ath10k *ar,
					       u32 ce_ctrl_addr,
					       unsigned int n)
{
	u32 ctrl1_addr = ath10k_pci_read32((ar),
					   (ce_ctrl_addr) + CE_CTRL1_ADDRESS);

	ath10k_pci_write32(ar, ce_ctrl_addr + CE_CTRL1_ADDRESS,
			   (ctrl1_addr &  ~CE_CTRL1_DMAX_LENGTH_MASK) |
			   CE_CTRL1_DMAX_LENGTH_SET(n));
}

static inline void ath10k_ce_src_ring_byte_swap_set(struct ath10k *ar,
						    u32 ce_ctrl_addr,
						    unsigned int n)
{
	u32 ctrl1_addr = ath10k_pci_read32(ar, ce_ctrl_addr + CE_CTRL1_ADDRESS);

	ath10k_pci_write32(ar, ce_ctrl_addr + CE_CTRL1_ADDRESS,
			   (ctrl1_addr & ~CE_CTRL1_SRC_RING_BYTE_SWAP_EN_MASK) |
			   CE_CTRL1_SRC_RING_BYTE_SWAP_EN_SET(n));
}

static inline void ath10k_ce_dest_ring_byte_swap_set(struct ath10k *ar,
						     u32 ce_ctrl_addr,
						     unsigned int n)
{
	u32 ctrl1_addr = ath10k_pci_read32(ar, ce_ctrl_addr + CE_CTRL1_ADDRESS);

	ath10k_pci_write32(ar, ce_ctrl_addr + CE_CTRL1_ADDRESS,
			   (ctrl1_addr & ~CE_CTRL1_DST_RING_BYTE_SWAP_EN_MASK) |
			   CE_CTRL1_DST_RING_BYTE_SWAP_EN_SET(n));
}

static inline u32 ath10k_ce_dest_ring_read_index_get(struct ath10k *ar,
						     u32 ce_ctrl_addr)
{
	return ath10k_pci_read32(ar, ce_ctrl_addr + CURRENT_DRRI_ADDRESS);
}

static inline void ath10k_ce_dest_ring_base_addr_set(struct ath10k *ar,
						     u32 ce_ctrl_addr,
						     u32 addr)
{
	ath10k_pci_write32(ar, ce_ctrl_addr + DR_BA_ADDRESS, addr);
}

static inline void ath10k_ce_dest_ring_size_set(struct ath10k *ar,
						u32 ce_ctrl_addr,
						unsigned int n)
{
	ath10k_pci_write32(ar, ce_ctrl_addr + DR_SIZE_ADDRESS, n);
}

static inline void ath10k_ce_src_ring_highmark_set(struct ath10k *ar,
						   u32 ce_ctrl_addr,
						   unsigned int n)
{
	u32 addr = ath10k_pci_read32(ar, ce_ctrl_addr + SRC_WATERMARK_ADDRESS);

	ath10k_pci_write32(ar, ce_ctrl_addr + SRC_WATERMARK_ADDRESS,
			   (addr & ~SRC_WATERMARK_HIGH_MASK) |
			   SRC_WATERMARK_HIGH_SET(n));
}

static inline void ath10k_ce_src_ring_lowmark_set(struct ath10k *ar,
						  u32 ce_ctrl_addr,
						  unsigned int n)
{
	u32 addr = ath10k_pci_read32(ar, ce_ctrl_addr + SRC_WATERMARK_ADDRESS);

	ath10k_pci_write32(ar, ce_ctrl_addr + SRC_WATERMARK_ADDRESS,
			   (addr & ~SRC_WATERMARK_LOW_MASK) |
			   SRC_WATERMARK_LOW_SET(n));
}

static inline void ath10k_ce_dest_ring_highmark_set(struct ath10k *ar,
						    u32 ce_ctrl_addr,
						    unsigned int n)
{
	u32 addr = ath10k_pci_read32(ar, ce_ctrl_addr + DST_WATERMARK_ADDRESS);

	ath10k_pci_write32(ar, ce_ctrl_addr + DST_WATERMARK_ADDRESS,
			   (addr & ~DST_WATERMARK_HIGH_MASK) |
			   DST_WATERMARK_HIGH_SET(n));
}

static inline void ath10k_ce_dest_ring_lowmark_set(struct ath10k *ar,
						   u32 ce_ctrl_addr,
						   unsigned int n)
{
	u32 addr = ath10k_pci_read32(ar, ce_ctrl_addr + DST_WATERMARK_ADDRESS);

	ath10k_pci_write32(ar, ce_ctrl_addr + DST_WATERMARK_ADDRESS,
			   (addr & ~DST_WATERMARK_LOW_MASK) |
			   DST_WATERMARK_LOW_SET(n));
}

static inline void ath10k_ce_copy_complete_inter_enable(struct ath10k *ar,
							u32 ce_ctrl_addr)
{
	u32 host_ie_addr = ath10k_pci_read32(ar,
					     ce_ctrl_addr + HOST_IE_ADDRESS);

	ath10k_pci_write32(ar, ce_ctrl_addr + HOST_IE_ADDRESS,
			   host_ie_addr | HOST_IE_COPY_COMPLETE_MASK);
}

static inline void ath10k_ce_copy_complete_intr_disable(struct ath10k *ar,
							u32 ce_ctrl_addr)
{
	u32 host_ie_addr = ath10k_pci_read32(ar,
					     ce_ctrl_addr + HOST_IE_ADDRESS);

	ath10k_pci_write32(ar, ce_ctrl_addr + HOST_IE_ADDRESS,
			   host_ie_addr & ~HOST_IE_COPY_COMPLETE_MASK);
}

static inline void ath10k_ce_watermark_intr_disable(struct ath10k *ar,
						    u32 ce_ctrl_addr)
{
	u32 host_ie_addr = ath10k_pci_read32(ar,
					     ce_ctrl_addr + HOST_IE_ADDRESS);

	ath10k_pci_write32(ar, ce_ctrl_addr + HOST_IE_ADDRESS,
			   host_ie_addr & ~CE_WATERMARK_MASK);
}

static inline void ath10k_ce_error_intr_enable(struct ath10k *ar,
					       u32 ce_ctrl_addr)
{
	u32 misc_ie_addr = ath10k_pci_read32(ar,
					     ce_ctrl_addr + MISC_IE_ADDRESS);

	ath10k_pci_write32(ar, ce_ctrl_addr + MISC_IE_ADDRESS,
			   misc_ie_addr | CE_ERROR_MASK);
}

static inline void ath10k_ce_engine_int_status_clear(struct ath10k *ar,
						     u32 ce_ctrl_addr,
						     unsigned int mask)
{
	ath10k_pci_write32(ar, ce_ctrl_addr + HOST_IS_ADDRESS, mask);
}


/*
 * Guts of ath10k_ce_send, used by both ath10k_ce_send and
 * ath10k_ce_sendlist_send.
 * The caller takes responsibility for any needed locking.
 */
static int ath10k_ce_send_nolock(struct ath10k_ce_pipe *ce_state,
				 void *per_transfer_context,
				 u32 buffer,
				 unsigned int nbytes,
				 unsigned int transfer_id,
				 unsigned int flags)
{
	struct ath10k *ar = ce_state->ar;
	struct ath10k_ce_ring *src_ring = ce_state->src_ring;
	struct ce_desc *desc, *sdesc;
	unsigned int nentries_mask = src_ring->nentries_mask;
	unsigned int sw_index = src_ring->sw_index;
	unsigned int write_index = src_ring->write_index;
	u32 ctrl_addr = ce_state->ctrl_addr;
	u32 desc_flags = 0;
	int ret = 0;

	if (nbytes > ce_state->src_sz_max)
		ath10k_warn("%s: send more we can (nbytes: %d, max: %d)\n",
			    __func__, nbytes, ce_state->src_sz_max);

	ret = ath10k_pci_wake(ar);
	if (ret)
		return ret;

	if (unlikely(CE_RING_DELTA(nentries_mask,
				   write_index, sw_index - 1) <= 0)) {
		ret = -ENOSR;
		goto exit;
	}

	desc = CE_SRC_RING_TO_DESC(src_ring->base_addr_owner_space,
				   write_index);
	sdesc = CE_SRC_RING_TO_DESC(src_ring->shadow_base, write_index);

	desc_flags |= SM(transfer_id, CE_DESC_FLAGS_META_DATA);

	if (flags & CE_SEND_FLAG_GATHER)
		desc_flags |= CE_DESC_FLAGS_GATHER;
	if (flags & CE_SEND_FLAG_BYTE_SWAP)
		desc_flags |= CE_DESC_FLAGS_BYTE_SWAP;

	sdesc->addr   = __cpu_to_le32(buffer);
	sdesc->nbytes = __cpu_to_le16(nbytes);
	sdesc->flags  = __cpu_to_le16(desc_flags);

	*desc = *sdesc;

	src_ring->per_transfer_context[write_index] = per_transfer_context;

	/* Update Source Ring Write Index */
	write_index = CE_RING_IDX_INCR(nentries_mask, write_index);

	/* WORKAROUND */
	if (!(flags & CE_SEND_FLAG_GATHER))
		ath10k_ce_src_ring_write_index_set(ar, ctrl_addr, write_index);

	src_ring->write_index = write_index;
exit:
	ath10k_pci_sleep(ar);
	return ret;
}

int ath10k_ce_send(struct ath10k_ce_pipe *ce_state,
		   void *per_transfer_context,
		   u32 buffer,
		   unsigned int nbytes,
		   unsigned int transfer_id,
		   unsigned int flags)
{
	struct ath10k *ar = ce_state->ar;
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	int ret;

	spin_lock_bh(&ar_pci->ce_lock);
	ret = ath10k_ce_send_nolock(ce_state, per_transfer_context,
				    buffer, nbytes, transfer_id, flags);
	spin_unlock_bh(&ar_pci->ce_lock);

	return ret;
}

int ath10k_ce_num_free_src_entries(struct ath10k_ce_pipe *pipe)
{
	struct ath10k *ar = pipe->ar;
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	int delta;

	spin_lock_bh(&ar_pci->ce_lock);
	delta = CE_RING_DELTA(pipe->src_ring->nentries_mask,
			      pipe->src_ring->write_index,
			      pipe->src_ring->sw_index - 1);
	spin_unlock_bh(&ar_pci->ce_lock);

	return delta;
}

int ath10k_ce_recv_buf_enqueue(struct ath10k_ce_pipe *ce_state,
			       void *per_recv_context,
			       u32 buffer)
{
	struct ath10k_ce_ring *dest_ring = ce_state->dest_ring;
	u32 ctrl_addr = ce_state->ctrl_addr;
	struct ath10k *ar = ce_state->ar;
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	unsigned int nentries_mask = dest_ring->nentries_mask;
	unsigned int write_index;
	unsigned int sw_index;
	int ret;

	spin_lock_bh(&ar_pci->ce_lock);
	write_index = dest_ring->write_index;
	sw_index = dest_ring->sw_index;

	ret = ath10k_pci_wake(ar);
	if (ret)
		goto out;

	if (CE_RING_DELTA(nentries_mask, write_index, sw_index - 1) > 0) {
		struct ce_desc *base = dest_ring->base_addr_owner_space;
		struct ce_desc *desc = CE_DEST_RING_TO_DESC(base, write_index);

		/* Update destination descriptor */
		desc->addr    = __cpu_to_le32(buffer);
		desc->nbytes = 0;

		dest_ring->per_transfer_context[write_index] =
							per_recv_context;

		/* Update Destination Ring Write Index */
		write_index = CE_RING_IDX_INCR(nentries_mask, write_index);
		ath10k_ce_dest_ring_write_index_set(ar, ctrl_addr, write_index);
		dest_ring->write_index = write_index;
		ret = 0;
	} else {
		ret = -EIO;
	}
	ath10k_pci_sleep(ar);

out:
	spin_unlock_bh(&ar_pci->ce_lock);

	return ret;
}

/*
 * Guts of ath10k_ce_completed_recv_next.
 * The caller takes responsibility for any necessary locking.
 */
static int ath10k_ce_completed_recv_next_nolock(struct ath10k_ce_pipe *ce_state,
						void **per_transfer_contextp,
						u32 *bufferp,
						unsigned int *nbytesp,
						unsigned int *transfer_idp,
						unsigned int *flagsp)
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
	*bufferp = __le32_to_cpu(sdesc.addr);
	*nbytesp = nbytes;
	*transfer_idp = MS(__le16_to_cpu(sdesc.flags), CE_DESC_FLAGS_META_DATA);

	if (__le16_to_cpu(sdesc.flags) & CE_DESC_FLAGS_BYTE_SWAP)
		*flagsp = CE_RECV_FLAG_SWAPPED;
	else
		*flagsp = 0;

	if (per_transfer_contextp)
		*per_transfer_contextp =
			dest_ring->per_transfer_context[sw_index];

	/* sanity */
	dest_ring->per_transfer_context[sw_index] = NULL;

	/* Update sw_index */
	sw_index = CE_RING_IDX_INCR(nentries_mask, sw_index);
	dest_ring->sw_index = sw_index;

	return 0;
}

int ath10k_ce_completed_recv_next(struct ath10k_ce_pipe *ce_state,
				  void **per_transfer_contextp,
				  u32 *bufferp,
				  unsigned int *nbytesp,
				  unsigned int *transfer_idp,
				  unsigned int *flagsp)
{
	struct ath10k *ar = ce_state->ar;
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	int ret;

	spin_lock_bh(&ar_pci->ce_lock);
	ret = ath10k_ce_completed_recv_next_nolock(ce_state,
						   per_transfer_contextp,
						   bufferp, nbytesp,
						   transfer_idp, flagsp);
	spin_unlock_bh(&ar_pci->ce_lock);

	return ret;
}

int ath10k_ce_revoke_recv_next(struct ath10k_ce_pipe *ce_state,
			       void **per_transfer_contextp,
			       u32 *bufferp)
{
	struct ath10k_ce_ring *dest_ring;
	unsigned int nentries_mask;
	unsigned int sw_index;
	unsigned int write_index;
	int ret;
	struct ath10k *ar;
	struct ath10k_pci *ar_pci;

	dest_ring = ce_state->dest_ring;

	if (!dest_ring)
		return -EIO;

	ar = ce_state->ar;
	ar_pci = ath10k_pci_priv(ar);

	spin_lock_bh(&ar_pci->ce_lock);

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

		/* Update sw_index */
		sw_index = CE_RING_IDX_INCR(nentries_mask, sw_index);
		dest_ring->sw_index = sw_index;
		ret = 0;
	} else {
		ret = -EIO;
	}

	spin_unlock_bh(&ar_pci->ce_lock);

	return ret;
}

/*
 * Guts of ath10k_ce_completed_send_next.
 * The caller takes responsibility for any necessary locking.
 */
static int ath10k_ce_completed_send_next_nolock(struct ath10k_ce_pipe *ce_state,
						void **per_transfer_contextp,
						u32 *bufferp,
						unsigned int *nbytesp,
						unsigned int *transfer_idp)
{
	struct ath10k_ce_ring *src_ring = ce_state->src_ring;
	u32 ctrl_addr = ce_state->ctrl_addr;
	struct ath10k *ar = ce_state->ar;
	unsigned int nentries_mask = src_ring->nentries_mask;
	unsigned int sw_index = src_ring->sw_index;
	struct ce_desc *sdesc, *sbase;
	unsigned int read_index;
	int ret;

	if (src_ring->hw_index == sw_index) {
		/*
		 * The SW completion index has caught up with the cached
		 * version of the HW completion index.
		 * Update the cached HW completion index to see whether
		 * the SW has really caught up to the HW, or if the cached
		 * value of the HW index has become stale.
		 */

		ret = ath10k_pci_wake(ar);
		if (ret)
			return ret;

		src_ring->hw_index =
			ath10k_ce_src_ring_read_index_get(ar, ctrl_addr);
		src_ring->hw_index &= nentries_mask;

		ath10k_pci_sleep(ar);
	}

	read_index = src_ring->hw_index;

	if ((read_index == sw_index) || (read_index == 0xffffffff))
		return -EIO;

	sbase = src_ring->shadow_base;
	sdesc = CE_SRC_RING_TO_DESC(sbase, sw_index);

	/* Return data from completed source descriptor */
	*bufferp = __le32_to_cpu(sdesc->addr);
	*nbytesp = __le16_to_cpu(sdesc->nbytes);
	*transfer_idp = MS(__le16_to_cpu(sdesc->flags),
			   CE_DESC_FLAGS_META_DATA);

	if (per_transfer_contextp)
		*per_transfer_contextp =
			src_ring->per_transfer_context[sw_index];

	/* sanity */
	src_ring->per_transfer_context[sw_index] = NULL;

	/* Update sw_index */
	sw_index = CE_RING_IDX_INCR(nentries_mask, sw_index);
	src_ring->sw_index = sw_index;

	return 0;
}

/* NB: Modeled after ath10k_ce_completed_send_next */
int ath10k_ce_cancel_send_next(struct ath10k_ce_pipe *ce_state,
			       void **per_transfer_contextp,
			       u32 *bufferp,
			       unsigned int *nbytesp,
			       unsigned int *transfer_idp)
{
	struct ath10k_ce_ring *src_ring;
	unsigned int nentries_mask;
	unsigned int sw_index;
	unsigned int write_index;
	int ret;
	struct ath10k *ar;
	struct ath10k_pci *ar_pci;

	src_ring = ce_state->src_ring;

	if (!src_ring)
		return -EIO;

	ar = ce_state->ar;
	ar_pci = ath10k_pci_priv(ar);

	spin_lock_bh(&ar_pci->ce_lock);

	nentries_mask = src_ring->nentries_mask;
	sw_index = src_ring->sw_index;
	write_index = src_ring->write_index;

	if (write_index != sw_index) {
		struct ce_desc *base = src_ring->base_addr_owner_space;
		struct ce_desc *desc = CE_SRC_RING_TO_DESC(base, sw_index);

		/* Return data from completed source descriptor */
		*bufferp = __le32_to_cpu(desc->addr);
		*nbytesp = __le16_to_cpu(desc->nbytes);
		*transfer_idp = MS(__le16_to_cpu(desc->flags),
						CE_DESC_FLAGS_META_DATA);

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

	spin_unlock_bh(&ar_pci->ce_lock);

	return ret;
}

int ath10k_ce_completed_send_next(struct ath10k_ce_pipe *ce_state,
				  void **per_transfer_contextp,
				  u32 *bufferp,
				  unsigned int *nbytesp,
				  unsigned int *transfer_idp)
{
	struct ath10k *ar = ce_state->ar;
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	int ret;

	spin_lock_bh(&ar_pci->ce_lock);
	ret = ath10k_ce_completed_send_next_nolock(ce_state,
						   per_transfer_contextp,
						   bufferp, nbytesp,
						   transfer_idp);
	spin_unlock_bh(&ar_pci->ce_lock);

	return ret;
}

/*
 * Guts of interrupt handler for per-engine interrupts on a particular CE.
 *
 * Invokes registered callbacks for recv_complete,
 * send_complete, and watermarks.
 */
void ath10k_ce_per_engine_service(struct ath10k *ar, unsigned int ce_id)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	struct ath10k_ce_pipe *ce_state = &ar_pci->ce_states[ce_id];
	u32 ctrl_addr = ce_state->ctrl_addr;
	int ret;

	ret = ath10k_pci_wake(ar);
	if (ret)
		return;

	spin_lock_bh(&ar_pci->ce_lock);

	/* Clear the copy-complete interrupts that will be handled here. */
	ath10k_ce_engine_int_status_clear(ar, ctrl_addr,
					  HOST_IS_COPY_COMPLETE_MASK);

	spin_unlock_bh(&ar_pci->ce_lock);

	if (ce_state->recv_cb)
		ce_state->recv_cb(ce_state);

	if (ce_state->send_cb)
		ce_state->send_cb(ce_state);

	spin_lock_bh(&ar_pci->ce_lock);

	/*
	 * Misc CE interrupts are not being handled, but still need
	 * to be cleared.
	 */
	ath10k_ce_engine_int_status_clear(ar, ctrl_addr, CE_WATERMARK_MASK);

	spin_unlock_bh(&ar_pci->ce_lock);
	ath10k_pci_sleep(ar);
}

/*
 * Handler for per-engine interrupts on ALL active CEs.
 * This is used in cases where the system is sharing a
 * single interrput for all CEs
 */

void ath10k_ce_per_engine_service_any(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	int ce_id, ret;
	u32 intr_summary;

	ret = ath10k_pci_wake(ar);
	if (ret)
		return;

	intr_summary = CE_INTERRUPT_SUMMARY(ar);

	for (ce_id = 0; intr_summary && (ce_id < ar_pci->ce_count); ce_id++) {
		if (intr_summary & (1 << ce_id))
			intr_summary &= ~(1 << ce_id);
		else
			/* no intr pending on this CE */
			continue;

		ath10k_ce_per_engine_service(ar, ce_id);
	}

	ath10k_pci_sleep(ar);
}

/*
 * Adjust interrupts for the copy complete handler.
 * If it's needed for either send or recv, then unmask
 * this interrupt; otherwise, mask it.
 *
 * Called with ce_lock held.
 */
static void ath10k_ce_per_engine_handler_adjust(struct ath10k_ce_pipe *ce_state,
						int disable_copy_compl_intr)
{
	u32 ctrl_addr = ce_state->ctrl_addr;
	struct ath10k *ar = ce_state->ar;
	int ret;

	ret = ath10k_pci_wake(ar);
	if (ret)
		return;

	if ((!disable_copy_compl_intr) &&
	    (ce_state->send_cb || ce_state->recv_cb))
		ath10k_ce_copy_complete_inter_enable(ar, ctrl_addr);
	else
		ath10k_ce_copy_complete_intr_disable(ar, ctrl_addr);

	ath10k_ce_watermark_intr_disable(ar, ctrl_addr);

	ath10k_pci_sleep(ar);
}

void ath10k_ce_disable_interrupts(struct ath10k *ar)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	int ce_id, ret;

	ret = ath10k_pci_wake(ar);
	if (ret)
		return;

	for (ce_id = 0; ce_id < ar_pci->ce_count; ce_id++) {
		struct ath10k_ce_pipe *ce_state = &ar_pci->ce_states[ce_id];
		u32 ctrl_addr = ce_state->ctrl_addr;

		ath10k_ce_copy_complete_intr_disable(ar, ctrl_addr);
	}
	ath10k_pci_sleep(ar);
}

void ath10k_ce_send_cb_register(struct ath10k_ce_pipe *ce_state,
				void (*send_cb)(struct ath10k_ce_pipe *),
				int disable_interrupts)
{
	struct ath10k *ar = ce_state->ar;
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);

	spin_lock_bh(&ar_pci->ce_lock);
	ce_state->send_cb = send_cb;
	ath10k_ce_per_engine_handler_adjust(ce_state, disable_interrupts);
	spin_unlock_bh(&ar_pci->ce_lock);
}

void ath10k_ce_recv_cb_register(struct ath10k_ce_pipe *ce_state,
				void (*recv_cb)(struct ath10k_ce_pipe *))
{
	struct ath10k *ar = ce_state->ar;
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);

	spin_lock_bh(&ar_pci->ce_lock);
	ce_state->recv_cb = recv_cb;
	ath10k_ce_per_engine_handler_adjust(ce_state, 0);
	spin_unlock_bh(&ar_pci->ce_lock);
}

static int ath10k_ce_init_src_ring(struct ath10k *ar,
				   unsigned int ce_id,
				   struct ath10k_ce_pipe *ce_state,
				   const struct ce_attr *attr)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	struct ath10k_ce_ring *src_ring;
	unsigned int nentries = attr->src_nentries;
	unsigned int ce_nbytes;
	u32 ctrl_addr = ath10k_ce_base_address(ce_id);
	dma_addr_t base_addr;
	char *ptr;

	nentries = roundup_pow_of_two(nentries);

	if (ce_state->src_ring) {
		WARN_ON(ce_state->src_ring->nentries != nentries);
		return 0;
	}

	ce_nbytes = sizeof(struct ath10k_ce_ring) + (nentries * sizeof(void *));
	ptr = kzalloc(ce_nbytes, GFP_KERNEL);
	if (ptr == NULL)
		return -ENOMEM;

	ce_state->src_ring = (struct ath10k_ce_ring *)ptr;
	src_ring = ce_state->src_ring;

	ptr += sizeof(struct ath10k_ce_ring);
	src_ring->nentries = nentries;
	src_ring->nentries_mask = nentries - 1;

	src_ring->sw_index = ath10k_ce_src_ring_read_index_get(ar, ctrl_addr);
	src_ring->sw_index &= src_ring->nentries_mask;
	src_ring->hw_index = src_ring->sw_index;

	src_ring->write_index =
		ath10k_ce_src_ring_write_index_get(ar, ctrl_addr);
	src_ring->write_index &= src_ring->nentries_mask;

	src_ring->per_transfer_context = (void **)ptr;

	/*
	 * Legacy platforms that do not support cache
	 * coherent DMA are unsupported
	 */
	src_ring->base_addr_owner_space_unaligned =
		pci_alloc_consistent(ar_pci->pdev,
				     (nentries * sizeof(struct ce_desc) +
				      CE_DESC_RING_ALIGN),
				     &base_addr);
	if (!src_ring->base_addr_owner_space_unaligned) {
		kfree(ce_state->src_ring);
		ce_state->src_ring = NULL;
		return -ENOMEM;
	}

	src_ring->base_addr_ce_space_unaligned = base_addr;

	src_ring->base_addr_owner_space = PTR_ALIGN(
			src_ring->base_addr_owner_space_unaligned,
			CE_DESC_RING_ALIGN);
	src_ring->base_addr_ce_space = ALIGN(
			src_ring->base_addr_ce_space_unaligned,
			CE_DESC_RING_ALIGN);

	/*
	 * Also allocate a shadow src ring in regular
	 * mem to use for faster access.
	 */
	src_ring->shadow_base_unaligned =
		kmalloc((nentries * sizeof(struct ce_desc) +
			 CE_DESC_RING_ALIGN), GFP_KERNEL);
	if (!src_ring->shadow_base_unaligned) {
		pci_free_consistent(ar_pci->pdev,
				    (nentries * sizeof(struct ce_desc) +
				     CE_DESC_RING_ALIGN),
				    src_ring->base_addr_owner_space,
				    src_ring->base_addr_ce_space);
		kfree(ce_state->src_ring);
		ce_state->src_ring = NULL;
		return -ENOMEM;
	}

	src_ring->shadow_base = PTR_ALIGN(
			src_ring->shadow_base_unaligned,
			CE_DESC_RING_ALIGN);

	ath10k_ce_src_ring_base_addr_set(ar, ctrl_addr,
					 src_ring->base_addr_ce_space);
	ath10k_ce_src_ring_size_set(ar, ctrl_addr, nentries);
	ath10k_ce_src_ring_dmax_set(ar, ctrl_addr, attr->src_sz_max);
	ath10k_ce_src_ring_byte_swap_set(ar, ctrl_addr, 0);
	ath10k_ce_src_ring_lowmark_set(ar, ctrl_addr, 0);
	ath10k_ce_src_ring_highmark_set(ar, ctrl_addr, nentries);

	ath10k_dbg(ATH10K_DBG_BOOT,
		   "boot ce src ring id %d entries %d base_addr %p\n",
		   ce_id, nentries, src_ring->base_addr_owner_space);

	return 0;
}

static int ath10k_ce_init_dest_ring(struct ath10k *ar,
				    unsigned int ce_id,
				    struct ath10k_ce_pipe *ce_state,
				    const struct ce_attr *attr)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	struct ath10k_ce_ring *dest_ring;
	unsigned int nentries = attr->dest_nentries;
	unsigned int ce_nbytes;
	u32 ctrl_addr = ath10k_ce_base_address(ce_id);
	dma_addr_t base_addr;
	char *ptr;

	nentries = roundup_pow_of_two(nentries);

	if (ce_state->dest_ring) {
		WARN_ON(ce_state->dest_ring->nentries != nentries);
		return 0;
	}

	ce_nbytes = sizeof(struct ath10k_ce_ring) + (nentries * sizeof(void *));
	ptr = kzalloc(ce_nbytes, GFP_KERNEL);
	if (ptr == NULL)
		return -ENOMEM;

	ce_state->dest_ring = (struct ath10k_ce_ring *)ptr;
	dest_ring = ce_state->dest_ring;

	ptr += sizeof(struct ath10k_ce_ring);
	dest_ring->nentries = nentries;
	dest_ring->nentries_mask = nentries - 1;

	dest_ring->sw_index = ath10k_ce_dest_ring_read_index_get(ar, ctrl_addr);
	dest_ring->sw_index &= dest_ring->nentries_mask;
	dest_ring->write_index =
		ath10k_ce_dest_ring_write_index_get(ar, ctrl_addr);
	dest_ring->write_index &= dest_ring->nentries_mask;

	dest_ring->per_transfer_context = (void **)ptr;

	/*
	 * Legacy platforms that do not support cache
	 * coherent DMA are unsupported
	 */
	dest_ring->base_addr_owner_space_unaligned =
		pci_alloc_consistent(ar_pci->pdev,
				     (nentries * sizeof(struct ce_desc) +
				      CE_DESC_RING_ALIGN),
				     &base_addr);
	if (!dest_ring->base_addr_owner_space_unaligned) {
		kfree(ce_state->dest_ring);
		ce_state->dest_ring = NULL;
		return -ENOMEM;
	}

	dest_ring->base_addr_ce_space_unaligned = base_addr;

	/*
	 * Correctly initialize memory to 0 to prevent garbage
	 * data crashing system when download firmware
	 */
	memset(dest_ring->base_addr_owner_space_unaligned, 0,
	       nentries * sizeof(struct ce_desc) + CE_DESC_RING_ALIGN);

	dest_ring->base_addr_owner_space = PTR_ALIGN(
			dest_ring->base_addr_owner_space_unaligned,
			CE_DESC_RING_ALIGN);
	dest_ring->base_addr_ce_space = ALIGN(
			dest_ring->base_addr_ce_space_unaligned,
			CE_DESC_RING_ALIGN);

	ath10k_ce_dest_ring_base_addr_set(ar, ctrl_addr,
					  dest_ring->base_addr_ce_space);
	ath10k_ce_dest_ring_size_set(ar, ctrl_addr, nentries);
	ath10k_ce_dest_ring_byte_swap_set(ar, ctrl_addr, 0);
	ath10k_ce_dest_ring_lowmark_set(ar, ctrl_addr, 0);
	ath10k_ce_dest_ring_highmark_set(ar, ctrl_addr, nentries);

	ath10k_dbg(ATH10K_DBG_BOOT,
		   "boot ce dest ring id %d entries %d base_addr %p\n",
		   ce_id, nentries, dest_ring->base_addr_owner_space);

	return 0;
}

static struct ath10k_ce_pipe *ath10k_ce_init_state(struct ath10k *ar,
					     unsigned int ce_id,
					     const struct ce_attr *attr)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	struct ath10k_ce_pipe *ce_state = &ar_pci->ce_states[ce_id];
	u32 ctrl_addr = ath10k_ce_base_address(ce_id);

	spin_lock_bh(&ar_pci->ce_lock);

	ce_state->ar = ar;
	ce_state->id = ce_id;
	ce_state->ctrl_addr = ctrl_addr;
	ce_state->attr_flags = attr->flags;
	ce_state->src_sz_max = attr->src_sz_max;

	spin_unlock_bh(&ar_pci->ce_lock);

	return ce_state;
}

/*
 * Initialize a Copy Engine based on caller-supplied attributes.
 * This may be called once to initialize both source and destination
 * rings or it may be called twice for separate source and destination
 * initialization. It may be that only one side or the other is
 * initialized by software/firmware.
 */
struct ath10k_ce_pipe *ath10k_ce_init(struct ath10k *ar,
				unsigned int ce_id,
				const struct ce_attr *attr)
{
	struct ath10k_ce_pipe *ce_state;
	u32 ctrl_addr = ath10k_ce_base_address(ce_id);
	int ret;

	ret = ath10k_pci_wake(ar);
	if (ret)
		return NULL;

	ce_state = ath10k_ce_init_state(ar, ce_id, attr);
	if (!ce_state) {
		ath10k_err("Failed to initialize CE state for ID: %d\n", ce_id);
		return NULL;
	}

	if (attr->src_nentries) {
		ret = ath10k_ce_init_src_ring(ar, ce_id, ce_state, attr);
		if (ret) {
			ath10k_err("Failed to initialize CE src ring for ID: %d (%d)\n",
				   ce_id, ret);
			ath10k_ce_deinit(ce_state);
			return NULL;
		}
	}

	if (attr->dest_nentries) {
		ret = ath10k_ce_init_dest_ring(ar, ce_id, ce_state, attr);
		if (ret) {
			ath10k_err("Failed to initialize CE dest ring for ID: %d (%d)\n",
				   ce_id, ret);
			ath10k_ce_deinit(ce_state);
			return NULL;
		}
	}

	/* Enable CE error interrupts */
	ath10k_ce_error_intr_enable(ar, ctrl_addr);

	ath10k_pci_sleep(ar);

	return ce_state;
}

void ath10k_ce_deinit(struct ath10k_ce_pipe *ce_state)
{
	struct ath10k *ar = ce_state->ar;
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);

	if (ce_state->src_ring) {
		kfree(ce_state->src_ring->shadow_base_unaligned);
		pci_free_consistent(ar_pci->pdev,
				    (ce_state->src_ring->nentries *
				     sizeof(struct ce_desc) +
				     CE_DESC_RING_ALIGN),
				    ce_state->src_ring->base_addr_owner_space,
				    ce_state->src_ring->base_addr_ce_space);
		kfree(ce_state->src_ring);
	}

	if (ce_state->dest_ring) {
		pci_free_consistent(ar_pci->pdev,
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
