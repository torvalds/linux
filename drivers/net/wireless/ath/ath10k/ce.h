/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2017 Qualcomm Atheros, Inc.
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

#ifndef _CE_H_
#define _CE_H_

#include "hif.h"

#define CE_HTT_H2T_MSG_SRC_NENTRIES 8192

/* Descriptor rings must be aligned to this boundary */
#define CE_DESC_RING_ALIGN	8
#define CE_SEND_FLAG_GATHER	0x00010000

/*
 * Copy Engine support: low-level Target-side Copy Engine API.
 * This is a hardware access layer used by code that understands
 * how to use copy engines.
 */

struct ath10k_ce_pipe;

#define CE_DESC_FLAGS_GATHER         (1 << 0)
#define CE_DESC_FLAGS_BYTE_SWAP      (1 << 1)
#define CE_WCN3990_DESC_FLAGS_GATHER BIT(31)

#define CE_DESC_FLAGS_GET_MASK		GENMASK(4, 0)
#define CE_DESC_37BIT_ADDR_MASK		GENMASK_ULL(37, 0)

/* Following desc flags are used in QCA99X0 */
#define CE_DESC_FLAGS_HOST_INT_DIS	(1 << 2)
#define CE_DESC_FLAGS_TGT_INT_DIS	(1 << 3)

#define CE_DESC_FLAGS_META_DATA_MASK ar->hw_values->ce_desc_meta_data_mask
#define CE_DESC_FLAGS_META_DATA_LSB  ar->hw_values->ce_desc_meta_data_lsb

#define CE_DDR_RRI_MASK			GENMASK(15, 0)
#define CE_DDR_DRRI_SHIFT		16

struct ce_desc {
	__le32 addr;
	__le16 nbytes;
	__le16 flags; /* %CE_DESC_FLAGS_ */
};

struct ce_desc_64 {
	__le64 addr;
	__le16 nbytes; /* length in register map */
	__le16 flags; /* fw_metadata_high */
	__le32 toeplitz_hash_result;
};

#define CE_DESC_SIZE sizeof(struct ce_desc)
#define CE_DESC_SIZE_64 sizeof(struct ce_desc_64)

struct ath10k_ce_ring {
	/* Number of entries in this ring; must be power of 2 */
	unsigned int nentries;
	unsigned int nentries_mask;

	/*
	 * For dest ring, this is the next index to be processed
	 * by software after it was/is received into.
	 *
	 * For src ring, this is the last descriptor that was sent
	 * and completion processed by software.
	 *
	 * Regardless of src or dest ring, this is an invariant
	 * (modulo ring size):
	 *     write index >= read index >= sw_index
	 */
	unsigned int sw_index;
	/* cached copy */
	unsigned int write_index;
	/*
	 * For src ring, this is the next index not yet processed by HW.
	 * This is a cached copy of the real HW index (read index), used
	 * for avoiding reading the HW index register more often than
	 * necessary.
	 * This extends the invariant:
	 *     write index >= read index >= hw_index >= sw_index
	 *
	 * For dest ring, this is currently unused.
	 */
	/* cached copy */
	unsigned int hw_index;

	/* Start of DMA-coherent area reserved for descriptors */
	/* Host address space */
	void *base_addr_owner_space_unaligned;
	/* CE address space */
	u32 base_addr_ce_space_unaligned;

	/*
	 * Actual start of descriptors.
	 * Aligned to descriptor-size boundary.
	 * Points into reserved DMA-coherent area, above.
	 */
	/* Host address space */
	void *base_addr_owner_space;

	/* CE address space */
	u32 base_addr_ce_space;

	char *shadow_base_unaligned;
	struct ce_desc *shadow_base;

	/* keep last */
	void *per_transfer_context[0];
};

struct ath10k_ce_pipe {
	struct ath10k *ar;
	unsigned int id;

	unsigned int attr_flags;

	u32 ctrl_addr;

	void (*send_cb)(struct ath10k_ce_pipe *);
	void (*recv_cb)(struct ath10k_ce_pipe *);

	unsigned int src_sz_max;
	struct ath10k_ce_ring *src_ring;
	struct ath10k_ce_ring *dest_ring;
	const struct ath10k_ce_ops *ops;
};

/* Copy Engine settable attributes */
struct ce_attr;

struct ath10k_bus_ops {
	u32 (*read32)(struct ath10k *ar, u32 offset);
	void (*write32)(struct ath10k *ar, u32 offset, u32 value);
	int (*get_num_banks)(struct ath10k *ar);
};

static inline struct ath10k_ce *ath10k_ce_priv(struct ath10k *ar)
{
	return (struct ath10k_ce *)ar->ce_priv;
}

struct ath10k_ce {
	/* protects CE info */
	spinlock_t ce_lock;
	const struct ath10k_bus_ops *bus_ops;
	struct ath10k_ce_pipe ce_states[CE_COUNT_MAX];
	u32 *vaddr_rri;
	dma_addr_t paddr_rri;
};

/*==================Send====================*/

/* ath10k_ce_send flags */
#define CE_SEND_FLAG_BYTE_SWAP 1

/*
 * Queue a source buffer to be sent to an anonymous destination buffer.
 *   ce         - which copy engine to use
 *   buffer          - address of buffer
 *   nbytes          - number of bytes to send
 *   transfer_id     - arbitrary ID; reflected to destination
 *   flags           - CE_SEND_FLAG_* values
 * Returns 0 on success; otherwise an error status.
 *
 * Note: If no flags are specified, use CE's default data swap mode.
 *
 * Implementation note: pushes 1 buffer to Source ring
 */
int ath10k_ce_send(struct ath10k_ce_pipe *ce_state,
		   void *per_transfer_send_context,
		   dma_addr_t buffer,
		   unsigned int nbytes,
		   /* 14 bits */
		   unsigned int transfer_id,
		   unsigned int flags);

int ath10k_ce_send_nolock(struct ath10k_ce_pipe *ce_state,
			  void *per_transfer_context,
			  dma_addr_t buffer,
			  unsigned int nbytes,
			  unsigned int transfer_id,
			  unsigned int flags);

void __ath10k_ce_send_revert(struct ath10k_ce_pipe *pipe);

int ath10k_ce_num_free_src_entries(struct ath10k_ce_pipe *pipe);

/*==================Recv=======================*/

int __ath10k_ce_rx_num_free_bufs(struct ath10k_ce_pipe *pipe);
int ath10k_ce_rx_post_buf(struct ath10k_ce_pipe *pipe, void *ctx,
			  dma_addr_t paddr);
void ath10k_ce_rx_update_write_idx(struct ath10k_ce_pipe *pipe, u32 nentries);

/* recv flags */
/* Data is byte-swapped */
#define CE_RECV_FLAG_SWAPPED	1

/*
 * Supply data for the next completed unprocessed receive descriptor.
 * Pops buffer from Dest ring.
 */
int ath10k_ce_completed_recv_next(struct ath10k_ce_pipe *ce_state,
				  void **per_transfer_contextp,
				  unsigned int *nbytesp);
/*
 * Supply data for the next completed unprocessed send descriptor.
 * Pops 1 completed send buffer from Source ring.
 */
int ath10k_ce_completed_send_next(struct ath10k_ce_pipe *ce_state,
				  void **per_transfer_contextp);

int ath10k_ce_completed_send_next_nolock(struct ath10k_ce_pipe *ce_state,
					 void **per_transfer_contextp);

/*==================CE Engine Initialization=======================*/

int ath10k_ce_init_pipe(struct ath10k *ar, unsigned int ce_id,
			const struct ce_attr *attr);
void ath10k_ce_deinit_pipe(struct ath10k *ar, unsigned int ce_id);
int ath10k_ce_alloc_pipe(struct ath10k *ar, int ce_id,
			 const struct ce_attr *attr);
void ath10k_ce_free_pipe(struct ath10k *ar, int ce_id);

/*==================CE Engine Shutdown=======================*/
/*
 * Support clean shutdown by allowing the caller to revoke
 * receive buffers.  Target DMA must be stopped before using
 * this API.
 */
int ath10k_ce_revoke_recv_next(struct ath10k_ce_pipe *ce_state,
			       void **per_transfer_contextp,
			       dma_addr_t *bufferp);

int ath10k_ce_completed_recv_next_nolock(struct ath10k_ce_pipe *ce_state,
					 void **per_transfer_contextp,
					 unsigned int *nbytesp);

/*
 * Support clean shutdown by allowing the caller to cancel
 * pending sends.  Target DMA must be stopped before using
 * this API.
 */
int ath10k_ce_cancel_send_next(struct ath10k_ce_pipe *ce_state,
			       void **per_transfer_contextp,
			       dma_addr_t *bufferp,
			       unsigned int *nbytesp,
			       unsigned int *transfer_idp);

/*==================CE Interrupt Handlers====================*/
void ath10k_ce_per_engine_service_any(struct ath10k *ar);
void ath10k_ce_per_engine_service(struct ath10k *ar, unsigned int ce_id);
int ath10k_ce_disable_interrupts(struct ath10k *ar);
void ath10k_ce_enable_interrupts(struct ath10k *ar);
void ath10k_ce_dump_registers(struct ath10k *ar,
			      struct ath10k_fw_crash_data *crash_data);
void ath10k_ce_alloc_rri(struct ath10k *ar);
void ath10k_ce_free_rri(struct ath10k *ar);

/* ce_attr.flags values */
/* Use NonSnooping PCIe accesses? */
#define CE_ATTR_NO_SNOOP		1

/* Byte swap data words */
#define CE_ATTR_BYTE_SWAP_DATA		2

/* Swizzle descriptors? */
#define CE_ATTR_SWIZZLE_DESCRIPTORS	4

/* no interrupt on copy completion */
#define CE_ATTR_DIS_INTR		8

/* Attributes of an instance of a Copy Engine */
struct ce_attr {
	/* CE_ATTR_* values */
	unsigned int flags;

	/* #entries in source ring - Must be a power of 2 */
	unsigned int src_nentries;

	/*
	 * Max source send size for this CE.
	 * This is also the minimum size of a destination buffer.
	 */
	unsigned int src_sz_max;

	/* #entries in destination ring - Must be a power of 2 */
	unsigned int dest_nentries;

	void (*send_cb)(struct ath10k_ce_pipe *);
	void (*recv_cb)(struct ath10k_ce_pipe *);
};

struct ath10k_ce_ops {
	struct ath10k_ce_ring *(*ce_alloc_src_ring)(struct ath10k *ar,
						    u32 ce_id,
						    const struct ce_attr *attr);
	struct ath10k_ce_ring *(*ce_alloc_dst_ring)(struct ath10k *ar,
						    u32 ce_id,
						    const struct ce_attr *attr);
	int (*ce_rx_post_buf)(struct ath10k_ce_pipe *pipe, void *ctx,
			      dma_addr_t paddr);
	int (*ce_completed_recv_next_nolock)(struct ath10k_ce_pipe *ce_state,
					     void **per_transfer_contextp,
					     u32 *nbytesp);
	int (*ce_revoke_recv_next)(struct ath10k_ce_pipe *ce_state,
				   void **per_transfer_contextp,
				   dma_addr_t *nbytesp);
	void (*ce_extract_desc_data)(struct ath10k *ar,
				     struct ath10k_ce_ring *src_ring,
				     u32 sw_index, dma_addr_t *bufferp,
				     u32 *nbytesp, u32 *transfer_idp);
	void (*ce_free_pipe)(struct ath10k *ar, int ce_id);
	int (*ce_send_nolock)(struct ath10k_ce_pipe *pipe,
			      void *per_transfer_context,
			      dma_addr_t buffer, u32 nbytes,
			      u32 transfer_id, u32 flags);
};

static inline u32 ath10k_ce_base_address(struct ath10k *ar, unsigned int ce_id)
{
	return CE0_BASE_ADDRESS + (CE1_BASE_ADDRESS - CE0_BASE_ADDRESS) * ce_id;
}

#define COPY_ENGINE_ID(COPY_ENGINE_BASE_ADDRESS) (((COPY_ENGINE_BASE_ADDRESS) \
		- CE0_BASE_ADDRESS) / (CE1_BASE_ADDRESS - CE0_BASE_ADDRESS))

#define CE_SRC_RING_TO_DESC(baddr, idx) \
	(&(((struct ce_desc *)baddr)[idx]))

#define CE_DEST_RING_TO_DESC(baddr, idx) \
	(&(((struct ce_desc *)baddr)[idx]))

#define CE_SRC_RING_TO_DESC_64(baddr, idx) \
	(&(((struct ce_desc_64 *)baddr)[idx]))

#define CE_DEST_RING_TO_DESC_64(baddr, idx) \
	(&(((struct ce_desc_64 *)baddr)[idx]))

/* Ring arithmetic (modulus number of entries in ring, which is a pwr of 2). */
#define CE_RING_DELTA(nentries_mask, fromidx, toidx) \
	(((int)(toidx) - (int)(fromidx)) & (nentries_mask))

#define CE_RING_IDX_INCR(nentries_mask, idx) (((idx) + 1) & (nentries_mask))
#define CE_RING_IDX_ADD(nentries_mask, idx, num) \
		(((idx) + (num)) & (nentries_mask))

#define CE_WRAPPER_INTERRUPT_SUMMARY_HOST_MSI_LSB \
				ar->regs->ce_wrap_intr_sum_host_msi_lsb
#define CE_WRAPPER_INTERRUPT_SUMMARY_HOST_MSI_MASK \
				ar->regs->ce_wrap_intr_sum_host_msi_mask
#define CE_WRAPPER_INTERRUPT_SUMMARY_HOST_MSI_GET(x) \
	(((x) & CE_WRAPPER_INTERRUPT_SUMMARY_HOST_MSI_MASK) >> \
		CE_WRAPPER_INTERRUPT_SUMMARY_HOST_MSI_LSB)
#define CE_WRAPPER_INTERRUPT_SUMMARY_ADDRESS			0x0000
#define CE_INTERRUPT_SUMMARY		(GENMASK(CE_COUNT_MAX - 1, 0))

static inline u32 ath10k_ce_interrupt_summary(struct ath10k *ar)
{
	struct ath10k_ce *ce = ath10k_ce_priv(ar);

	if (!ar->hw_params.per_ce_irq)
		return CE_WRAPPER_INTERRUPT_SUMMARY_HOST_MSI_GET(
			ce->bus_ops->read32((ar), CE_WRAPPER_BASE_ADDRESS +
			CE_WRAPPER_INTERRUPT_SUMMARY_ADDRESS));
	else
		return CE_INTERRUPT_SUMMARY;
}

#endif /* _CE_H_ */
