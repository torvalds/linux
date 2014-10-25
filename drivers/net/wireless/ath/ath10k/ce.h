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

#ifndef _CE_H_
#define _CE_H_

#include "hif.h"

/* Maximum number of Copy Engine's supported */
#define CE_COUNT_MAX 8
#define CE_HTT_H2T_MSG_SRC_NENTRIES 4096

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
#define CE_DESC_FLAGS_META_DATA_MASK 0xFFFC
#define CE_DESC_FLAGS_META_DATA_LSB  3

struct ce_desc {
	__le32 addr;
	__le16 nbytes;
	__le16 flags; /* %CE_DESC_FLAGS_ */
};

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
	/*
	 * Start of shadow copy of descriptors, within regular memory.
	 * Aligned to descriptor-size boundary.
	 */
	void *shadow_base_unaligned;
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
};

/* Copy Engine settable attributes */
struct ce_attr;

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
		   u32 buffer,
		   unsigned int nbytes,
		   /* 14 bits */
		   unsigned int transfer_id,
		   unsigned int flags);

int ath10k_ce_send_nolock(struct ath10k_ce_pipe *ce_state,
			  void *per_transfer_context,
			  u32 buffer,
			  unsigned int nbytes,
			  unsigned int transfer_id,
			  unsigned int flags);

void __ath10k_ce_send_revert(struct ath10k_ce_pipe *pipe);

int ath10k_ce_num_free_src_entries(struct ath10k_ce_pipe *pipe);

/*==================Recv=======================*/

int __ath10k_ce_rx_num_free_bufs(struct ath10k_ce_pipe *pipe);
int __ath10k_ce_rx_post_buf(struct ath10k_ce_pipe *pipe, void *ctx, u32 paddr);
int ath10k_ce_rx_post_buf(struct ath10k_ce_pipe *pipe, void *ctx, u32 paddr);

/* recv flags */
/* Data is byte-swapped */
#define CE_RECV_FLAG_SWAPPED	1

/*
 * Supply data for the next completed unprocessed receive descriptor.
 * Pops buffer from Dest ring.
 */
int ath10k_ce_completed_recv_next(struct ath10k_ce_pipe *ce_state,
				  void **per_transfer_contextp,
				  u32 *bufferp,
				  unsigned int *nbytesp,
				  unsigned int *transfer_idp,
				  unsigned int *flagsp);
/*
 * Supply data for the next completed unprocessed send descriptor.
 * Pops 1 completed send buffer from Source ring.
 */
int ath10k_ce_completed_send_next(struct ath10k_ce_pipe *ce_state,
				  void **per_transfer_contextp,
				  u32 *bufferp,
				  unsigned int *nbytesp,
				  unsigned int *transfer_idp);

/*==================CE Engine Initialization=======================*/

int ath10k_ce_init_pipe(struct ath10k *ar, unsigned int ce_id,
			const struct ce_attr *attr,
			void (*send_cb)(struct ath10k_ce_pipe *),
			void (*recv_cb)(struct ath10k_ce_pipe *));
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
			       u32 *bufferp);

/*
 * Support clean shutdown by allowing the caller to cancel
 * pending sends.  Target DMA must be stopped before using
 * this API.
 */
int ath10k_ce_cancel_send_next(struct ath10k_ce_pipe *ce_state,
			       void **per_transfer_contextp,
			       u32 *bufferp,
			       unsigned int *nbytesp,
			       unsigned int *transfer_idp);

/*==================CE Interrupt Handlers====================*/
void ath10k_ce_per_engine_service_any(struct ath10k *ar);
void ath10k_ce_per_engine_service(struct ath10k *ar, unsigned int ce_id);
int ath10k_ce_disable_interrupts(struct ath10k *ar);
void ath10k_ce_enable_interrupts(struct ath10k *ar);

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
};

#define SR_BA_ADDRESS		0x0000
#define SR_SIZE_ADDRESS		0x0004
#define DR_BA_ADDRESS		0x0008
#define DR_SIZE_ADDRESS		0x000c
#define CE_CMD_ADDRESS		0x0018

#define CE_CTRL1_DST_RING_BYTE_SWAP_EN_MSB	17
#define CE_CTRL1_DST_RING_BYTE_SWAP_EN_LSB	17
#define CE_CTRL1_DST_RING_BYTE_SWAP_EN_MASK	0x00020000
#define CE_CTRL1_DST_RING_BYTE_SWAP_EN_SET(x) \
	(((0 | (x)) << CE_CTRL1_DST_RING_BYTE_SWAP_EN_LSB) & \
	CE_CTRL1_DST_RING_BYTE_SWAP_EN_MASK)

#define CE_CTRL1_SRC_RING_BYTE_SWAP_EN_MSB	16
#define CE_CTRL1_SRC_RING_BYTE_SWAP_EN_LSB	16
#define CE_CTRL1_SRC_RING_BYTE_SWAP_EN_MASK	0x00010000
#define CE_CTRL1_SRC_RING_BYTE_SWAP_EN_GET(x) \
	(((x) & CE_CTRL1_SRC_RING_BYTE_SWAP_EN_MASK) >> \
	 CE_CTRL1_SRC_RING_BYTE_SWAP_EN_LSB)
#define CE_CTRL1_SRC_RING_BYTE_SWAP_EN_SET(x) \
	(((0 | (x)) << CE_CTRL1_SRC_RING_BYTE_SWAP_EN_LSB) & \
	 CE_CTRL1_SRC_RING_BYTE_SWAP_EN_MASK)

#define CE_CTRL1_DMAX_LENGTH_MSB		15
#define CE_CTRL1_DMAX_LENGTH_LSB		0
#define CE_CTRL1_DMAX_LENGTH_MASK		0x0000ffff
#define CE_CTRL1_DMAX_LENGTH_GET(x) \
	(((x) & CE_CTRL1_DMAX_LENGTH_MASK) >> CE_CTRL1_DMAX_LENGTH_LSB)
#define CE_CTRL1_DMAX_LENGTH_SET(x) \
	(((0 | (x)) << CE_CTRL1_DMAX_LENGTH_LSB) & CE_CTRL1_DMAX_LENGTH_MASK)

#define CE_CTRL1_ADDRESS			0x0010
#define CE_CTRL1_HW_MASK			0x0007ffff
#define CE_CTRL1_SW_MASK			0x0007ffff
#define CE_CTRL1_HW_WRITE_MASK			0x00000000
#define CE_CTRL1_SW_WRITE_MASK			0x0007ffff
#define CE_CTRL1_RSTMASK			0xffffffff
#define CE_CTRL1_RESET				0x00000080

#define CE_CMD_HALT_STATUS_MSB			3
#define CE_CMD_HALT_STATUS_LSB			3
#define CE_CMD_HALT_STATUS_MASK			0x00000008
#define CE_CMD_HALT_STATUS_GET(x) \
	(((x) & CE_CMD_HALT_STATUS_MASK) >> CE_CMD_HALT_STATUS_LSB)
#define CE_CMD_HALT_STATUS_SET(x) \
	(((0 | (x)) << CE_CMD_HALT_STATUS_LSB) & CE_CMD_HALT_STATUS_MASK)
#define CE_CMD_HALT_STATUS_RESET		0
#define CE_CMD_HALT_MSB				0
#define CE_CMD_HALT_MASK			0x00000001

#define HOST_IE_COPY_COMPLETE_MSB		0
#define HOST_IE_COPY_COMPLETE_LSB		0
#define HOST_IE_COPY_COMPLETE_MASK		0x00000001
#define HOST_IE_COPY_COMPLETE_GET(x) \
	(((x) & HOST_IE_COPY_COMPLETE_MASK) >> HOST_IE_COPY_COMPLETE_LSB)
#define HOST_IE_COPY_COMPLETE_SET(x) \
	(((0 | (x)) << HOST_IE_COPY_COMPLETE_LSB) & HOST_IE_COPY_COMPLETE_MASK)
#define HOST_IE_COPY_COMPLETE_RESET		0
#define HOST_IE_ADDRESS				0x002c

#define HOST_IS_DST_RING_LOW_WATERMARK_MASK	0x00000010
#define HOST_IS_DST_RING_HIGH_WATERMARK_MASK	0x00000008
#define HOST_IS_SRC_RING_LOW_WATERMARK_MASK	0x00000004
#define HOST_IS_SRC_RING_HIGH_WATERMARK_MASK	0x00000002
#define HOST_IS_COPY_COMPLETE_MASK		0x00000001
#define HOST_IS_ADDRESS				0x0030

#define MISC_IE_ADDRESS				0x0034

#define MISC_IS_AXI_ERR_MASK			0x00000400

#define MISC_IS_DST_ADDR_ERR_MASK		0x00000200
#define MISC_IS_SRC_LEN_ERR_MASK		0x00000100
#define MISC_IS_DST_MAX_LEN_VIO_MASK		0x00000080
#define MISC_IS_DST_RING_OVERFLOW_MASK		0x00000040
#define MISC_IS_SRC_RING_OVERFLOW_MASK		0x00000020

#define MISC_IS_ADDRESS				0x0038

#define SR_WR_INDEX_ADDRESS			0x003c

#define DST_WR_INDEX_ADDRESS			0x0040

#define CURRENT_SRRI_ADDRESS			0x0044

#define CURRENT_DRRI_ADDRESS			0x0048

#define SRC_WATERMARK_LOW_MSB			31
#define SRC_WATERMARK_LOW_LSB			16
#define SRC_WATERMARK_LOW_MASK			0xffff0000
#define SRC_WATERMARK_LOW_GET(x) \
	(((x) & SRC_WATERMARK_LOW_MASK) >> SRC_WATERMARK_LOW_LSB)
#define SRC_WATERMARK_LOW_SET(x) \
	(((0 | (x)) << SRC_WATERMARK_LOW_LSB) & SRC_WATERMARK_LOW_MASK)
#define SRC_WATERMARK_LOW_RESET			0
#define SRC_WATERMARK_HIGH_MSB			15
#define SRC_WATERMARK_HIGH_LSB			0
#define SRC_WATERMARK_HIGH_MASK			0x0000ffff
#define SRC_WATERMARK_HIGH_GET(x) \
	(((x) & SRC_WATERMARK_HIGH_MASK) >> SRC_WATERMARK_HIGH_LSB)
#define SRC_WATERMARK_HIGH_SET(x) \
	(((0 | (x)) << SRC_WATERMARK_HIGH_LSB) & SRC_WATERMARK_HIGH_MASK)
#define SRC_WATERMARK_HIGH_RESET		0
#define SRC_WATERMARK_ADDRESS			0x004c

#define DST_WATERMARK_LOW_LSB			16
#define DST_WATERMARK_LOW_MASK			0xffff0000
#define DST_WATERMARK_LOW_SET(x) \
	(((0 | (x)) << DST_WATERMARK_LOW_LSB) & DST_WATERMARK_LOW_MASK)
#define DST_WATERMARK_LOW_RESET			0
#define DST_WATERMARK_HIGH_MSB			15
#define DST_WATERMARK_HIGH_LSB			0
#define DST_WATERMARK_HIGH_MASK			0x0000ffff
#define DST_WATERMARK_HIGH_GET(x) \
	(((x) & DST_WATERMARK_HIGH_MASK) >> DST_WATERMARK_HIGH_LSB)
#define DST_WATERMARK_HIGH_SET(x) \
	(((0 | (x)) << DST_WATERMARK_HIGH_LSB) & DST_WATERMARK_HIGH_MASK)
#define DST_WATERMARK_HIGH_RESET		0
#define DST_WATERMARK_ADDRESS			0x0050

static inline u32 ath10k_ce_base_address(unsigned int ce_id)
{
	return CE0_BASE_ADDRESS + (CE1_BASE_ADDRESS - CE0_BASE_ADDRESS) * ce_id;
}

#define CE_WATERMARK_MASK (HOST_IS_SRC_RING_LOW_WATERMARK_MASK  | \
			   HOST_IS_SRC_RING_HIGH_WATERMARK_MASK | \
			   HOST_IS_DST_RING_LOW_WATERMARK_MASK  | \
			   HOST_IS_DST_RING_HIGH_WATERMARK_MASK)

#define CE_ERROR_MASK	(MISC_IS_AXI_ERR_MASK           | \
			 MISC_IS_DST_ADDR_ERR_MASK      | \
			 MISC_IS_SRC_LEN_ERR_MASK       | \
			 MISC_IS_DST_MAX_LEN_VIO_MASK   | \
			 MISC_IS_DST_RING_OVERFLOW_MASK | \
			 MISC_IS_SRC_RING_OVERFLOW_MASK)

#define CE_SRC_RING_TO_DESC(baddr, idx) \
	(&(((struct ce_desc *)baddr)[idx]))

#define CE_DEST_RING_TO_DESC(baddr, idx) \
	(&(((struct ce_desc *)baddr)[idx]))

/* Ring arithmetic (modulus number of entries in ring, which is a pwr of 2). */
#define CE_RING_DELTA(nentries_mask, fromidx, toidx) \
	(((int)(toidx)-(int)(fromidx)) & (nentries_mask))

#define CE_RING_IDX_INCR(nentries_mask, idx) (((idx) + 1) & (nentries_mask))

#define CE_WRAPPER_INTERRUPT_SUMMARY_HOST_MSI_LSB		8
#define CE_WRAPPER_INTERRUPT_SUMMARY_HOST_MSI_MASK		0x0000ff00
#define CE_WRAPPER_INTERRUPT_SUMMARY_HOST_MSI_GET(x) \
	(((x) & CE_WRAPPER_INTERRUPT_SUMMARY_HOST_MSI_MASK) >> \
		CE_WRAPPER_INTERRUPT_SUMMARY_HOST_MSI_LSB)
#define CE_WRAPPER_INTERRUPT_SUMMARY_ADDRESS			0x0000

#define CE_INTERRUPT_SUMMARY(ar) \
	CE_WRAPPER_INTERRUPT_SUMMARY_HOST_MSI_GET( \
		ath10k_pci_read32((ar), CE_WRAPPER_BASE_ADDRESS + \
		CE_WRAPPER_INTERRUPT_SUMMARY_ADDRESS))

#endif /* _CE_H_ */
