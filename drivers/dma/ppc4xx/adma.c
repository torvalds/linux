// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2006-2009 DENX Software Engineering.
 *
 * Author: Yuri Tikhonov <yur@emcraft.com>
 *
 * Further porting to arch/powerpc by
 * 	Anatolij Gustschin <agust@denx.de>
 */

/*
 * This driver supports the asynchrounous DMA copy and RAID engines available
 * on the AMCC PPC440SPe Processors.
 * Based on the Intel Xscale(R) family of I/O Processors (IOP 32x, 33x, 134x)
 * ADMA driver written by D.Williams.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/async_tx.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <asm/dcr.h>
#include <asm/dcr-regs.h>
#include "adma.h"
#include "../dmaengine.h"

enum ppc_adma_init_code {
	PPC_ADMA_INIT_OK = 0,
	PPC_ADMA_INIT_MEMRES,
	PPC_ADMA_INIT_MEMREG,
	PPC_ADMA_INIT_ALLOC,
	PPC_ADMA_INIT_COHERENT,
	PPC_ADMA_INIT_CHANNEL,
	PPC_ADMA_INIT_IRQ1,
	PPC_ADMA_INIT_IRQ2,
	PPC_ADMA_INIT_REGISTER
};

static char *ppc_adma_errors[] = {
	[PPC_ADMA_INIT_OK] = "ok",
	[PPC_ADMA_INIT_MEMRES] = "failed to get memory resource",
	[PPC_ADMA_INIT_MEMREG] = "failed to request memory region",
	[PPC_ADMA_INIT_ALLOC] = "failed to allocate memory for adev "
				"structure",
	[PPC_ADMA_INIT_COHERENT] = "failed to allocate coherent memory for "
				   "hardware descriptors",
	[PPC_ADMA_INIT_CHANNEL] = "failed to allocate memory for channel",
	[PPC_ADMA_INIT_IRQ1] = "failed to request first irq",
	[PPC_ADMA_INIT_IRQ2] = "failed to request second irq",
	[PPC_ADMA_INIT_REGISTER] = "failed to register dma async device",
};

static enum ppc_adma_init_code
ppc440spe_adma_devices[PPC440SPE_ADMA_ENGINES_NUM];

struct ppc_dma_chan_ref {
	struct dma_chan *chan;
	struct list_head node;
};

/* The list of channels exported by ppc440spe ADMA */
static struct list_head
ppc440spe_adma_chan_list = LIST_HEAD_INIT(ppc440spe_adma_chan_list);

/* This flag is set when want to refetch the xor chain in the interrupt
 * handler
 */
static u32 do_xor_refetch;

/* Pointer to DMA0, DMA1 CP/CS FIFO */
static void *ppc440spe_dma_fifo_buf;

/* Pointers to last submitted to DMA0, DMA1 CDBs */
static struct ppc440spe_adma_desc_slot *chan_last_sub[3];
static struct ppc440spe_adma_desc_slot *chan_first_cdb[3];

/* Pointer to last linked and submitted xor CB */
static struct ppc440spe_adma_desc_slot *xor_last_linked;
static struct ppc440spe_adma_desc_slot *xor_last_submit;

/* This array is used in data-check operations for storing a pattern */
static char ppc440spe_qword[16];

static atomic_t ppc440spe_adma_err_irq_ref;
static dcr_host_t ppc440spe_mq_dcr_host;
static unsigned int ppc440spe_mq_dcr_len;

/* Since RXOR operations use the common register (MQ0_CF2H) for setting-up
 * the block size in transactions, then we do not allow to activate more than
 * only one RXOR transactions simultaneously. So use this var to store
 * the information about is RXOR currently active (PPC440SPE_RXOR_RUN bit is
 * set) or not (PPC440SPE_RXOR_RUN is clear).
 */
static unsigned long ppc440spe_rxor_state;

/* These are used in enable & check routines
 */
static u32 ppc440spe_r6_enabled;
static struct ppc440spe_adma_chan *ppc440spe_r6_tchan;
static struct completion ppc440spe_r6_test_comp;

static int ppc440spe_adma_dma2rxor_prep_src(
		struct ppc440spe_adma_desc_slot *desc,
		struct ppc440spe_rxor *cursor, int index,
		int src_cnt, u32 addr);
static void ppc440spe_adma_dma2rxor_set_src(
		struct ppc440spe_adma_desc_slot *desc,
		int index, dma_addr_t addr);
static void ppc440spe_adma_dma2rxor_set_mult(
		struct ppc440spe_adma_desc_slot *desc,
		int index, u8 mult);

#ifdef ADMA_LL_DEBUG
#define ADMA_LL_DBG(x) ({ if (1) x; 0; })
#else
#define ADMA_LL_DBG(x) ({ if (0) x; 0; })
#endif

static void print_cb(struct ppc440spe_adma_chan *chan, void *block)
{
	struct dma_cdb *cdb;
	struct xor_cb *cb;
	int i;

	switch (chan->device->id) {
	case 0:
	case 1:
		cdb = block;

		pr_debug("CDB at %p [%d]:\n"
			"\t attr 0x%02x opc 0x%02x cnt 0x%08x\n"
			"\t sg1u 0x%08x sg1l 0x%08x\n"
			"\t sg2u 0x%08x sg2l 0x%08x\n"
			"\t sg3u 0x%08x sg3l 0x%08x\n",
			cdb, chan->device->id,
			cdb->attr, cdb->opc, le32_to_cpu(cdb->cnt),
			le32_to_cpu(cdb->sg1u), le32_to_cpu(cdb->sg1l),
			le32_to_cpu(cdb->sg2u), le32_to_cpu(cdb->sg2l),
			le32_to_cpu(cdb->sg3u), le32_to_cpu(cdb->sg3l)
		);
		break;
	case 2:
		cb = block;

		pr_debug("CB at %p [%d]:\n"
			"\t cbc 0x%08x cbbc 0x%08x cbs 0x%08x\n"
			"\t cbtah 0x%08x cbtal 0x%08x\n"
			"\t cblah 0x%08x cblal 0x%08x\n",
			cb, chan->device->id,
			cb->cbc, cb->cbbc, cb->cbs,
			cb->cbtah, cb->cbtal,
			cb->cblah, cb->cblal);
		for (i = 0; i < 16; i++) {
			if (i && !cb->ops[i].h && !cb->ops[i].l)
				continue;
			pr_debug("\t ops[%2d]: h 0x%08x l 0x%08x\n",
				i, cb->ops[i].h, cb->ops[i].l);
		}
		break;
	}
}

static void print_cb_list(struct ppc440spe_adma_chan *chan,
			  struct ppc440spe_adma_desc_slot *iter)
{
	for (; iter; iter = iter->hw_next)
		print_cb(chan, iter->hw_desc);
}

static void prep_dma_xor_dbg(int id, dma_addr_t dst, dma_addr_t *src,
			     unsigned int src_cnt)
{
	int i;

	pr_debug("\n%s(%d):\nsrc: ", __func__, id);
	for (i = 0; i < src_cnt; i++)
		pr_debug("\t0x%016llx ", src[i]);
	pr_debug("dst:\n\t0x%016llx\n", dst);
}

static void prep_dma_pq_dbg(int id, dma_addr_t *dst, dma_addr_t *src,
			    unsigned int src_cnt)
{
	int i;

	pr_debug("\n%s(%d):\nsrc: ", __func__, id);
	for (i = 0; i < src_cnt; i++)
		pr_debug("\t0x%016llx ", src[i]);
	pr_debug("dst: ");
	for (i = 0; i < 2; i++)
		pr_debug("\t0x%016llx ", dst[i]);
}

static void prep_dma_pqzero_sum_dbg(int id, dma_addr_t *src,
				    unsigned int src_cnt,
				    const unsigned char *scf)
{
	int i;

	pr_debug("\n%s(%d):\nsrc(coef): ", __func__, id);
	if (scf) {
		for (i = 0; i < src_cnt; i++)
			pr_debug("\t0x%016llx(0x%02x) ", src[i], scf[i]);
	} else {
		for (i = 0; i < src_cnt; i++)
			pr_debug("\t0x%016llx(no) ", src[i]);
	}

	pr_debug("dst: ");
	for (i = 0; i < 2; i++)
		pr_debug("\t0x%016llx ", src[src_cnt + i]);
}

/******************************************************************************
 * Command (Descriptor) Blocks low-level routines
 ******************************************************************************/
/**
 * ppc440spe_desc_init_interrupt - initialize the descriptor for INTERRUPT
 * pseudo operation
 */
static void ppc440spe_desc_init_interrupt(struct ppc440spe_adma_desc_slot *desc,
					  struct ppc440spe_adma_chan *chan)
{
	struct xor_cb *p;

	switch (chan->device->id) {
	case PPC440SPE_XOR_ID:
		p = desc->hw_desc;
		memset(desc->hw_desc, 0, sizeof(struct xor_cb));
		/* NOP with Command Block Complete Enable */
		p->cbc = XOR_CBCR_CBCE_BIT;
		break;
	case PPC440SPE_DMA0_ID:
	case PPC440SPE_DMA1_ID:
		memset(desc->hw_desc, 0, sizeof(struct dma_cdb));
		/* NOP with interrupt */
		set_bit(PPC440SPE_DESC_INT, &desc->flags);
		break;
	default:
		printk(KERN_ERR "Unsupported id %d in %s\n", chan->device->id,
				__func__);
		break;
	}
}

/**
 * ppc440spe_desc_init_null_xor - initialize the descriptor for NULL XOR
 * pseudo operation
 */
static void ppc440spe_desc_init_null_xor(struct ppc440spe_adma_desc_slot *desc)
{
	memset(desc->hw_desc, 0, sizeof(struct xor_cb));
	desc->hw_next = NULL;
	desc->src_cnt = 0;
	desc->dst_cnt = 1;
}

/**
 * ppc440spe_desc_init_xor - initialize the descriptor for XOR operation
 */
static void ppc440spe_desc_init_xor(struct ppc440spe_adma_desc_slot *desc,
					 int src_cnt, unsigned long flags)
{
	struct xor_cb *hw_desc = desc->hw_desc;

	memset(desc->hw_desc, 0, sizeof(struct xor_cb));
	desc->hw_next = NULL;
	desc->src_cnt = src_cnt;
	desc->dst_cnt = 1;

	hw_desc->cbc = XOR_CBCR_TGT_BIT | src_cnt;
	if (flags & DMA_PREP_INTERRUPT)
		/* Enable interrupt on completion */
		hw_desc->cbc |= XOR_CBCR_CBCE_BIT;
}

/**
 * ppc440spe_desc_init_dma2pq - initialize the descriptor for PQ
 * operation in DMA2 controller
 */
static void ppc440spe_desc_init_dma2pq(struct ppc440spe_adma_desc_slot *desc,
		int dst_cnt, int src_cnt, unsigned long flags)
{
	struct xor_cb *hw_desc = desc->hw_desc;

	memset(desc->hw_desc, 0, sizeof(struct xor_cb));
	desc->hw_next = NULL;
	desc->src_cnt = src_cnt;
	desc->dst_cnt = dst_cnt;
	memset(desc->reverse_flags, 0, sizeof(desc->reverse_flags));
	desc->descs_per_op = 0;

	hw_desc->cbc = XOR_CBCR_TGT_BIT;
	if (flags & DMA_PREP_INTERRUPT)
		/* Enable interrupt on completion */
		hw_desc->cbc |= XOR_CBCR_CBCE_BIT;
}

#define DMA_CTRL_FLAGS_LAST	DMA_PREP_FENCE
#define DMA_PREP_ZERO_P		(DMA_CTRL_FLAGS_LAST << 1)
#define DMA_PREP_ZERO_Q		(DMA_PREP_ZERO_P << 1)

/**
 * ppc440spe_desc_init_dma01pq - initialize the descriptors for PQ operation
 * with DMA0/1
 */
static void ppc440spe_desc_init_dma01pq(struct ppc440spe_adma_desc_slot *desc,
				int dst_cnt, int src_cnt, unsigned long flags,
				unsigned long op)
{
	struct dma_cdb *hw_desc;
	struct ppc440spe_adma_desc_slot *iter;
	u8 dopc;

	/* Common initialization of a PQ descriptors chain */
	set_bits(op, &desc->flags);
	desc->src_cnt = src_cnt;
	desc->dst_cnt = dst_cnt;

	/* WXOR MULTICAST if both P and Q are being computed
	 * MV_SG1_SG2 if Q only
	 */
	dopc = (desc->dst_cnt == DMA_DEST_MAX_NUM) ?
		DMA_CDB_OPC_MULTICAST : DMA_CDB_OPC_MV_SG1_SG2;

	list_for_each_entry(iter, &desc->group_list, chain_node) {
		hw_desc = iter->hw_desc;
		memset(iter->hw_desc, 0, sizeof(struct dma_cdb));

		if (likely(!list_is_last(&iter->chain_node,
				&desc->group_list))) {
			/* set 'next' pointer */
			iter->hw_next = list_entry(iter->chain_node.next,
				struct ppc440spe_adma_desc_slot, chain_node);
			clear_bit(PPC440SPE_DESC_INT, &iter->flags);
		} else {
			/* this is the last descriptor.
			 * this slot will be pasted from ADMA level
			 * each time it wants to configure parameters
			 * of the transaction (src, dst, ...)
			 */
			iter->hw_next = NULL;
			if (flags & DMA_PREP_INTERRUPT)
				set_bit(PPC440SPE_DESC_INT, &iter->flags);
			else
				clear_bit(PPC440SPE_DESC_INT, &iter->flags);
		}
	}

	/* Set OPS depending on WXOR/RXOR type of operation */
	if (!test_bit(PPC440SPE_DESC_RXOR, &desc->flags)) {
		/* This is a WXOR only chain:
		 * - first descriptors are for zeroing destinations
		 *   if PPC440SPE_ZERO_P/Q set;
		 * - descriptors remained are for GF-XOR operations.
		 */
		iter = list_first_entry(&desc->group_list,
					struct ppc440spe_adma_desc_slot,
					chain_node);

		if (test_bit(PPC440SPE_ZERO_P, &desc->flags)) {
			hw_desc = iter->hw_desc;
			hw_desc->opc = DMA_CDB_OPC_MV_SG1_SG2;
			iter = list_first_entry(&iter->chain_node,
					struct ppc440spe_adma_desc_slot,
					chain_node);
		}

		if (test_bit(PPC440SPE_ZERO_Q, &desc->flags)) {
			hw_desc = iter->hw_desc;
			hw_desc->opc = DMA_CDB_OPC_MV_SG1_SG2;
			iter = list_first_entry(&iter->chain_node,
					struct ppc440spe_adma_desc_slot,
					chain_node);
		}

		list_for_each_entry_from(iter, &desc->group_list, chain_node) {
			hw_desc = iter->hw_desc;
			hw_desc->opc = dopc;
		}
	} else {
		/* This is either RXOR-only or mixed RXOR/WXOR */

		/* The first 1 or 2 slots in chain are always RXOR,
		 * if need to calculate P & Q, then there are two
		 * RXOR slots; if only P or only Q, then there is one
		 */
		iter = list_first_entry(&desc->group_list,
					struct ppc440spe_adma_desc_slot,
					chain_node);
		hw_desc = iter->hw_desc;
		hw_desc->opc = DMA_CDB_OPC_MV_SG1_SG2;

		if (desc->dst_cnt == DMA_DEST_MAX_NUM) {
			iter = list_first_entry(&iter->chain_node,
						struct ppc440spe_adma_desc_slot,
						chain_node);
			hw_desc = iter->hw_desc;
			hw_desc->opc = DMA_CDB_OPC_MV_SG1_SG2;
		}

		/* The remaining descs (if any) are WXORs */
		if (test_bit(PPC440SPE_DESC_WXOR, &desc->flags)) {
			iter = list_first_entry(&iter->chain_node,
						struct ppc440spe_adma_desc_slot,
						chain_node);
			list_for_each_entry_from(iter, &desc->group_list,
						chain_node) {
				hw_desc = iter->hw_desc;
				hw_desc->opc = dopc;
			}
		}
	}
}

/**
 * ppc440spe_desc_init_dma01pqzero_sum - initialize the descriptor
 * for PQ_ZERO_SUM operation
 */
static void ppc440spe_desc_init_dma01pqzero_sum(
				struct ppc440spe_adma_desc_slot *desc,
				int dst_cnt, int src_cnt)
{
	struct dma_cdb *hw_desc;
	struct ppc440spe_adma_desc_slot *iter;
	int i = 0;
	u8 dopc = (dst_cnt == 2) ? DMA_CDB_OPC_MULTICAST :
				   DMA_CDB_OPC_MV_SG1_SG2;
	/*
	 * Initialize starting from 2nd or 3rd descriptor dependent
	 * on dst_cnt. First one or two slots are for cloning P
	 * and/or Q to chan->pdest and/or chan->qdest as we have
	 * to preserve original P/Q.
	 */
	iter = list_first_entry(&desc->group_list,
				struct ppc440spe_adma_desc_slot, chain_node);
	iter = list_entry(iter->chain_node.next,
			  struct ppc440spe_adma_desc_slot, chain_node);

	if (dst_cnt > 1) {
		iter = list_entry(iter->chain_node.next,
				  struct ppc440spe_adma_desc_slot, chain_node);
	}
	/* initialize each source descriptor in chain */
	list_for_each_entry_from(iter, &desc->group_list, chain_node) {
		hw_desc = iter->hw_desc;
		memset(iter->hw_desc, 0, sizeof(struct dma_cdb));
		iter->src_cnt = 0;
		iter->dst_cnt = 0;

		/* This is a ZERO_SUM operation:
		 * - <src_cnt> descriptors starting from 2nd or 3rd
		 *   descriptor are for GF-XOR operations;
		 * - remaining <dst_cnt> descriptors are for checking the result
		 */
		if (i++ < src_cnt)
			/* MV_SG1_SG2 if only Q is being verified
			 * MULTICAST if both P and Q are being verified
			 */
			hw_desc->opc = dopc;
		else
			/* DMA_CDB_OPC_DCHECK128 operation */
			hw_desc->opc = DMA_CDB_OPC_DCHECK128;

		if (likely(!list_is_last(&iter->chain_node,
					 &desc->group_list))) {
			/* set 'next' pointer */
			iter->hw_next = list_entry(iter->chain_node.next,
						struct ppc440spe_adma_desc_slot,
						chain_node);
		} else {
			/* this is the last descriptor.
			 * this slot will be pasted from ADMA level
			 * each time it wants to configure parameters
			 * of the transaction (src, dst, ...)
			 */
			iter->hw_next = NULL;
			/* always enable interrupt generation since we get
			 * the status of pqzero from the handler
			 */
			set_bit(PPC440SPE_DESC_INT, &iter->flags);
		}
	}
	desc->src_cnt = src_cnt;
	desc->dst_cnt = dst_cnt;
}

/**
 * ppc440spe_desc_init_memcpy - initialize the descriptor for MEMCPY operation
 */
static void ppc440spe_desc_init_memcpy(struct ppc440spe_adma_desc_slot *desc,
					unsigned long flags)
{
	struct dma_cdb *hw_desc = desc->hw_desc;

	memset(desc->hw_desc, 0, sizeof(struct dma_cdb));
	desc->hw_next = NULL;
	desc->src_cnt = 1;
	desc->dst_cnt = 1;

	if (flags & DMA_PREP_INTERRUPT)
		set_bit(PPC440SPE_DESC_INT, &desc->flags);
	else
		clear_bit(PPC440SPE_DESC_INT, &desc->flags);

	hw_desc->opc = DMA_CDB_OPC_MV_SG1_SG2;
}

/**
 * ppc440spe_desc_set_src_addr - set source address into the descriptor
 */
static void ppc440spe_desc_set_src_addr(struct ppc440spe_adma_desc_slot *desc,
					struct ppc440spe_adma_chan *chan,
					int src_idx, dma_addr_t addrh,
					dma_addr_t addrl)
{
	struct dma_cdb *dma_hw_desc;
	struct xor_cb *xor_hw_desc;
	phys_addr_t addr64, tmplow, tmphi;

	switch (chan->device->id) {
	case PPC440SPE_DMA0_ID:
	case PPC440SPE_DMA1_ID:
		if (!addrh) {
			addr64 = addrl;
			tmphi = (addr64 >> 32);
			tmplow = (addr64 & 0xFFFFFFFF);
		} else {
			tmphi = addrh;
			tmplow = addrl;
		}
		dma_hw_desc = desc->hw_desc;
		dma_hw_desc->sg1l = cpu_to_le32((u32)tmplow);
		dma_hw_desc->sg1u |= cpu_to_le32((u32)tmphi);
		break;
	case PPC440SPE_XOR_ID:
		xor_hw_desc = desc->hw_desc;
		xor_hw_desc->ops[src_idx].l = addrl;
		xor_hw_desc->ops[src_idx].h |= addrh;
		break;
	}
}

/**
 * ppc440spe_desc_set_src_mult - set source address mult into the descriptor
 */
static void ppc440spe_desc_set_src_mult(struct ppc440spe_adma_desc_slot *desc,
			struct ppc440spe_adma_chan *chan, u32 mult_index,
			int sg_index, unsigned char mult_value)
{
	struct dma_cdb *dma_hw_desc;
	u32 *psgu;

	switch (chan->device->id) {
	case PPC440SPE_DMA0_ID:
	case PPC440SPE_DMA1_ID:
		dma_hw_desc = desc->hw_desc;

		switch (sg_index) {
		/* for RXOR operations set multiplier
		 * into source cued address
		 */
		case DMA_CDB_SG_SRC:
			psgu = &dma_hw_desc->sg1u;
			break;
		/* for WXOR operations set multiplier
		 * into destination cued address(es)
		 */
		case DMA_CDB_SG_DST1:
			psgu = &dma_hw_desc->sg2u;
			break;
		case DMA_CDB_SG_DST2:
			psgu = &dma_hw_desc->sg3u;
			break;
		default:
			BUG();
		}

		*psgu |= cpu_to_le32(mult_value << mult_index);
		break;
	case PPC440SPE_XOR_ID:
		break;
	default:
		BUG();
	}
}

/**
 * ppc440spe_desc_set_dest_addr - set destination address into the descriptor
 */
static void ppc440spe_desc_set_dest_addr(struct ppc440spe_adma_desc_slot *desc,
				struct ppc440spe_adma_chan *chan,
				dma_addr_t addrh, dma_addr_t addrl,
				u32 dst_idx)
{
	struct dma_cdb *dma_hw_desc;
	struct xor_cb *xor_hw_desc;
	phys_addr_t addr64, tmphi, tmplow;
	u32 *psgu, *psgl;

	switch (chan->device->id) {
	case PPC440SPE_DMA0_ID:
	case PPC440SPE_DMA1_ID:
		if (!addrh) {
			addr64 = addrl;
			tmphi = (addr64 >> 32);
			tmplow = (addr64 & 0xFFFFFFFF);
		} else {
			tmphi = addrh;
			tmplow = addrl;
		}
		dma_hw_desc = desc->hw_desc;

		psgu = dst_idx ? &dma_hw_desc->sg3u : &dma_hw_desc->sg2u;
		psgl = dst_idx ? &dma_hw_desc->sg3l : &dma_hw_desc->sg2l;

		*psgl = cpu_to_le32((u32)tmplow);
		*psgu |= cpu_to_le32((u32)tmphi);
		break;
	case PPC440SPE_XOR_ID:
		xor_hw_desc = desc->hw_desc;
		xor_hw_desc->cbtal = addrl;
		xor_hw_desc->cbtah |= addrh;
		break;
	}
}

/**
 * ppc440spe_desc_set_byte_count - set number of data bytes involved
 * into the operation
 */
static void ppc440spe_desc_set_byte_count(struct ppc440spe_adma_desc_slot *desc,
				struct ppc440spe_adma_chan *chan,
				u32 byte_count)
{
	struct dma_cdb *dma_hw_desc;
	struct xor_cb *xor_hw_desc;

	switch (chan->device->id) {
	case PPC440SPE_DMA0_ID:
	case PPC440SPE_DMA1_ID:
		dma_hw_desc = desc->hw_desc;
		dma_hw_desc->cnt = cpu_to_le32(byte_count);
		break;
	case PPC440SPE_XOR_ID:
		xor_hw_desc = desc->hw_desc;
		xor_hw_desc->cbbc = byte_count;
		break;
	}
}

/**
 * ppc440spe_desc_set_rxor_block_size - set RXOR block size
 */
static inline void ppc440spe_desc_set_rxor_block_size(u32 byte_count)
{
	/* assume that byte_count is aligned on the 512-boundary;
	 * thus write it directly to the register (bits 23:31 are
	 * reserved there).
	 */
	dcr_write(ppc440spe_mq_dcr_host, DCRN_MQ0_CF2H, byte_count);
}

/**
 * ppc440spe_desc_set_dcheck - set CHECK pattern
 */
static void ppc440spe_desc_set_dcheck(struct ppc440spe_adma_desc_slot *desc,
				struct ppc440spe_adma_chan *chan, u8 *qword)
{
	struct dma_cdb *dma_hw_desc;

	switch (chan->device->id) {
	case PPC440SPE_DMA0_ID:
	case PPC440SPE_DMA1_ID:
		dma_hw_desc = desc->hw_desc;
		iowrite32(qword[0], &dma_hw_desc->sg3l);
		iowrite32(qword[4], &dma_hw_desc->sg3u);
		iowrite32(qword[8], &dma_hw_desc->sg2l);
		iowrite32(qword[12], &dma_hw_desc->sg2u);
		break;
	default:
		BUG();
	}
}

/**
 * ppc440spe_xor_set_link - set link address in xor CB
 */
static void ppc440spe_xor_set_link(struct ppc440spe_adma_desc_slot *prev_desc,
				struct ppc440spe_adma_desc_slot *next_desc)
{
	struct xor_cb *xor_hw_desc = prev_desc->hw_desc;

	if (unlikely(!next_desc || !(next_desc->phys))) {
		printk(KERN_ERR "%s: next_desc=0x%p; next_desc->phys=0x%llx\n",
			__func__, next_desc,
			next_desc ? next_desc->phys : 0);
		BUG();
	}

	xor_hw_desc->cbs = 0;
	xor_hw_desc->cblal = next_desc->phys;
	xor_hw_desc->cblah = 0;
	xor_hw_desc->cbc |= XOR_CBCR_LNK_BIT;
}

/**
 * ppc440spe_desc_set_link - set the address of descriptor following this
 * descriptor in chain
 */
static void ppc440spe_desc_set_link(struct ppc440spe_adma_chan *chan,
				struct ppc440spe_adma_desc_slot *prev_desc,
				struct ppc440spe_adma_desc_slot *next_desc)
{
	unsigned long flags;
	struct ppc440spe_adma_desc_slot *tail = next_desc;

	if (unlikely(!prev_desc || !next_desc ||
		(prev_desc->hw_next && prev_desc->hw_next != next_desc))) {
		/* If previous next is overwritten something is wrong.
		 * though we may refetch from append to initiate list
		 * processing; in this case - it's ok.
		 */
		printk(KERN_ERR "%s: prev_desc=0x%p; next_desc=0x%p; "
			"prev->hw_next=0x%p\n", __func__, prev_desc,
			next_desc, prev_desc ? prev_desc->hw_next : 0);
		BUG();
	}

	local_irq_save(flags);

	/* do s/w chaining both for DMA and XOR descriptors */
	prev_desc->hw_next = next_desc;

	switch (chan->device->id) {
	case PPC440SPE_DMA0_ID:
	case PPC440SPE_DMA1_ID:
		break;
	case PPC440SPE_XOR_ID:
		/* bind descriptor to the chain */
		while (tail->hw_next)
			tail = tail->hw_next;
		xor_last_linked = tail;

		if (prev_desc == xor_last_submit)
			/* do not link to the last submitted CB */
			break;
		ppc440spe_xor_set_link(prev_desc, next_desc);
		break;
	}

	local_irq_restore(flags);
}

/**
 * ppc440spe_desc_get_link - get the address of the descriptor that
 * follows this one
 */
static inline u32 ppc440spe_desc_get_link(struct ppc440spe_adma_desc_slot *desc,
					struct ppc440spe_adma_chan *chan)
{
	if (!desc->hw_next)
		return 0;

	return desc->hw_next->phys;
}

/**
 * ppc440spe_desc_is_aligned - check alignment
 */
static inline int ppc440spe_desc_is_aligned(
	struct ppc440spe_adma_desc_slot *desc, int num_slots)
{
	return (desc->idx & (num_slots - 1)) ? 0 : 1;
}

/**
 * ppc440spe_chan_xor_slot_count - get the number of slots necessary for
 * XOR operation
 */
static int ppc440spe_chan_xor_slot_count(size_t len, int src_cnt,
			int *slots_per_op)
{
	int slot_cnt;

	/* each XOR descriptor provides up to 16 source operands */
	slot_cnt = *slots_per_op = (src_cnt + XOR_MAX_OPS - 1)/XOR_MAX_OPS;

	if (likely(len <= PPC440SPE_ADMA_XOR_MAX_BYTE_COUNT))
		return slot_cnt;

	printk(KERN_ERR "%s: len %d > max %d !!\n",
		__func__, len, PPC440SPE_ADMA_XOR_MAX_BYTE_COUNT);
	BUG();
	return slot_cnt;
}

/**
 * ppc440spe_dma2_pq_slot_count - get the number of slots necessary for
 * DMA2 PQ operation
 */
static int ppc440spe_dma2_pq_slot_count(dma_addr_t *srcs,
		int src_cnt, size_t len)
{
	signed long long order = 0;
	int state = 0;
	int addr_count = 0;
	int i;
	for (i = 1; i < src_cnt; i++) {
		dma_addr_t cur_addr = srcs[i];
		dma_addr_t old_addr = srcs[i-1];
		switch (state) {
		case 0:
			if (cur_addr == old_addr + len) {
				/* direct RXOR */
				order = 1;
				state = 1;
				if (i == src_cnt-1)
					addr_count++;
			} else if (old_addr == cur_addr + len) {
				/* reverse RXOR */
				order = -1;
				state = 1;
				if (i == src_cnt-1)
					addr_count++;
			} else {
				state = 3;
			}
			break;
		case 1:
			if (i == src_cnt-2 || (order == -1
				&& cur_addr != old_addr - len)) {
				order = 0;
				state = 0;
				addr_count++;
			} else if (cur_addr == old_addr + len*order) {
				state = 2;
				if (i == src_cnt-1)
					addr_count++;
			} else if (cur_addr == old_addr + 2*len) {
				state = 2;
				if (i == src_cnt-1)
					addr_count++;
			} else if (cur_addr == old_addr + 3*len) {
				state = 2;
				if (i == src_cnt-1)
					addr_count++;
			} else {
				order = 0;
				state = 0;
				addr_count++;
			}
			break;
		case 2:
			order = 0;
			state = 0;
			addr_count++;
				break;
		}
		if (state == 3)
			break;
	}
	if (src_cnt <= 1 || (state != 1 && state != 2)) {
		pr_err("%s: src_cnt=%d, state=%d, addr_count=%d, order=%lld\n",
			__func__, src_cnt, state, addr_count, order);
		for (i = 0; i < src_cnt; i++)
			pr_err("\t[%d] 0x%llx \n", i, srcs[i]);
		BUG();
	}

	return (addr_count + XOR_MAX_OPS - 1) / XOR_MAX_OPS;
}


/******************************************************************************
 * ADMA channel low-level routines
 ******************************************************************************/

static u32
ppc440spe_chan_get_current_descriptor(struct ppc440spe_adma_chan *chan);
static void ppc440spe_chan_append(struct ppc440spe_adma_chan *chan);

/**
 * ppc440spe_adma_device_clear_eot_status - interrupt ack to XOR or DMA engine
 */
static void ppc440spe_adma_device_clear_eot_status(
					struct ppc440spe_adma_chan *chan)
{
	struct dma_regs *dma_reg;
	struct xor_regs *xor_reg;
	u8 *p = chan->device->dma_desc_pool_virt;
	struct dma_cdb *cdb;
	u32 rv, i;

	switch (chan->device->id) {
	case PPC440SPE_DMA0_ID:
	case PPC440SPE_DMA1_ID:
		/* read FIFO to ack */
		dma_reg = chan->device->dma_reg;
		while ((rv = ioread32(&dma_reg->csfpl))) {
			i = rv & DMA_CDB_ADDR_MSK;
			cdb = (struct dma_cdb *)&p[i -
			    (u32)chan->device->dma_desc_pool];

			/* Clear opcode to ack. This is necessary for
			 * ZeroSum operations only
			 */
			cdb->opc = 0;

			if (test_bit(PPC440SPE_RXOR_RUN,
			    &ppc440spe_rxor_state)) {
				/* probably this is a completed RXOR op,
				 * get pointer to CDB using the fact that
				 * physical and virtual addresses of CDB
				 * in pools have the same offsets
				 */
				if (le32_to_cpu(cdb->sg1u) &
				    DMA_CUED_XOR_BASE) {
					/* this is a RXOR */
					clear_bit(PPC440SPE_RXOR_RUN,
						  &ppc440spe_rxor_state);
				}
			}

			if (rv & DMA_CDB_STATUS_MSK) {
				/* ZeroSum check failed
				 */
				struct ppc440spe_adma_desc_slot *iter;
				dma_addr_t phys = rv & ~DMA_CDB_MSK;

				/*
				 * Update the status of corresponding
				 * descriptor.
				 */
				list_for_each_entry(iter, &chan->chain,
				    chain_node) {
					if (iter->phys == phys)
						break;
				}
				/*
				 * if cannot find the corresponding
				 * slot it's a bug
				 */
				BUG_ON(&iter->chain_node == &chan->chain);

				if (iter->xor_check_result) {
					if (test_bit(PPC440SPE_DESC_PCHECK,
						     &iter->flags)) {
						*iter->xor_check_result |=
							SUM_CHECK_P_RESULT;
					} else
					if (test_bit(PPC440SPE_DESC_QCHECK,
						     &iter->flags)) {
						*iter->xor_check_result |=
							SUM_CHECK_Q_RESULT;
					} else
						BUG();
				}
			}
		}

		rv = ioread32(&dma_reg->dsts);
		if (rv) {
			pr_err("DMA%d err status: 0x%x\n",
			       chan->device->id, rv);
			/* write back to clear */
			iowrite32(rv, &dma_reg->dsts);
		}
		break;
	case PPC440SPE_XOR_ID:
		/* reset status bits to ack */
		xor_reg = chan->device->xor_reg;
		rv = ioread32be(&xor_reg->sr);
		iowrite32be(rv, &xor_reg->sr);

		if (rv & (XOR_IE_ICBIE_BIT|XOR_IE_ICIE_BIT|XOR_IE_RPTIE_BIT)) {
			if (rv & XOR_IE_RPTIE_BIT) {
				/* Read PLB Timeout Error.
				 * Try to resubmit the CB
				 */
				u32 val = ioread32be(&xor_reg->ccbalr);

				iowrite32be(val, &xor_reg->cblalr);

				val = ioread32be(&xor_reg->crsr);
				iowrite32be(val | XOR_CRSR_XAE_BIT,
					    &xor_reg->crsr);
			} else
				pr_err("XOR ERR 0x%x status\n", rv);
			break;
		}

		/*  if the XORcore is idle, but there are unprocessed CBs
		 * then refetch the s/w chain here
		 */
		if (!(ioread32be(&xor_reg->sr) & XOR_SR_XCP_BIT) &&
		    do_xor_refetch)
			ppc440spe_chan_append(chan);
		break;
	}
}

/**
 * ppc440spe_chan_is_busy - get the channel status
 */
static int ppc440spe_chan_is_busy(struct ppc440spe_adma_chan *chan)
{
	struct dma_regs *dma_reg;
	struct xor_regs *xor_reg;
	int busy = 0;

	switch (chan->device->id) {
	case PPC440SPE_DMA0_ID:
	case PPC440SPE_DMA1_ID:
		dma_reg = chan->device->dma_reg;
		/*  if command FIFO's head and tail pointers are equal and
		 * status tail is the same as command, then channel is free
		 */
		if (ioread16(&dma_reg->cpfhp) != ioread16(&dma_reg->cpftp) ||
		    ioread16(&dma_reg->cpftp) != ioread16(&dma_reg->csftp))
			busy = 1;
		break;
	case PPC440SPE_XOR_ID:
		/* use the special status bit for the XORcore
		 */
		xor_reg = chan->device->xor_reg;
		busy = (ioread32be(&xor_reg->sr) & XOR_SR_XCP_BIT) ? 1 : 0;
		break;
	}

	return busy;
}

/**
 * ppc440spe_chan_set_first_xor_descriptor -  init XORcore chain
 */
static void ppc440spe_chan_set_first_xor_descriptor(
				struct ppc440spe_adma_chan *chan,
				struct ppc440spe_adma_desc_slot *next_desc)
{
	struct xor_regs *xor_reg = chan->device->xor_reg;

	if (ioread32be(&xor_reg->sr) & XOR_SR_XCP_BIT)
		printk(KERN_INFO "%s: Warn: XORcore is running "
			"when try to set the first CDB!\n",
			__func__);

	xor_last_submit = xor_last_linked = next_desc;

	iowrite32be(XOR_CRSR_64BA_BIT, &xor_reg->crsr);

	iowrite32be(next_desc->phys, &xor_reg->cblalr);
	iowrite32be(0, &xor_reg->cblahr);
	iowrite32be(ioread32be(&xor_reg->cbcr) | XOR_CBCR_LNK_BIT,
		    &xor_reg->cbcr);

	chan->hw_chain_inited = 1;
}

/**
 * ppc440spe_dma_put_desc - put DMA0,1 descriptor to FIFO.
 * called with irqs disabled
 */
static void ppc440spe_dma_put_desc(struct ppc440spe_adma_chan *chan,
		struct ppc440spe_adma_desc_slot *desc)
{
	u32 pcdb;
	struct dma_regs *dma_reg = chan->device->dma_reg;

	pcdb = desc->phys;
	if (!test_bit(PPC440SPE_DESC_INT, &desc->flags))
		pcdb |= DMA_CDB_NO_INT;

	chan_last_sub[chan->device->id] = desc;

	ADMA_LL_DBG(print_cb(chan, desc->hw_desc));

	iowrite32(pcdb, &dma_reg->cpfpl);
}

/**
 * ppc440spe_chan_append - update the h/w chain in the channel
 */
static void ppc440spe_chan_append(struct ppc440spe_adma_chan *chan)
{
	struct xor_regs *xor_reg;
	struct ppc440spe_adma_desc_slot *iter;
	struct xor_cb *xcb;
	u32 cur_desc;
	unsigned long flags;

	local_irq_save(flags);

	switch (chan->device->id) {
	case PPC440SPE_DMA0_ID:
	case PPC440SPE_DMA1_ID:
		cur_desc = ppc440spe_chan_get_current_descriptor(chan);

		if (likely(cur_desc)) {
			iter = chan_last_sub[chan->device->id];
			BUG_ON(!iter);
		} else {
			/* first peer */
			iter = chan_first_cdb[chan->device->id];
			BUG_ON(!iter);
			ppc440spe_dma_put_desc(chan, iter);
			chan->hw_chain_inited = 1;
		}

		/* is there something new to append */
		if (!iter->hw_next)
			break;

		/* flush descriptors from the s/w queue to fifo */
		list_for_each_entry_continue(iter, &chan->chain, chain_node) {
			ppc440spe_dma_put_desc(chan, iter);
			if (!iter->hw_next)
				break;
		}
		break;
	case PPC440SPE_XOR_ID:
		/* update h/w links and refetch */
		if (!xor_last_submit->hw_next)
			break;

		xor_reg = chan->device->xor_reg;
		/* the last linked CDB has to generate an interrupt
		 * that we'd be able to append the next lists to h/w
		 * regardless of the XOR engine state at the moment of
		 * appending of these next lists
		 */
		xcb = xor_last_linked->hw_desc;
		xcb->cbc |= XOR_CBCR_CBCE_BIT;

		if (!(ioread32be(&xor_reg->sr) & XOR_SR_XCP_BIT)) {
			/* XORcore is idle. Refetch now */
			do_xor_refetch = 0;
			ppc440spe_xor_set_link(xor_last_submit,
				xor_last_submit->hw_next);

			ADMA_LL_DBG(print_cb_list(chan,
				xor_last_submit->hw_next));

			xor_last_submit = xor_last_linked;
			iowrite32be(ioread32be(&xor_reg->crsr) |
				    XOR_CRSR_RCBE_BIT | XOR_CRSR_64BA_BIT,
				    &xor_reg->crsr);
		} else {
			/* XORcore is running. Refetch later in the handler */
			do_xor_refetch = 1;
		}

		break;
	}

	local_irq_restore(flags);
}

/**
 * ppc440spe_chan_get_current_descriptor - get the currently executed descriptor
 */
static u32
ppc440spe_chan_get_current_descriptor(struct ppc440spe_adma_chan *chan)
{
	struct dma_regs *dma_reg;
	struct xor_regs *xor_reg;

	if (unlikely(!chan->hw_chain_inited))
		/* h/w descriptor chain is not initialized yet */
		return 0;

	switch (chan->device->id) {
	case PPC440SPE_DMA0_ID:
	case PPC440SPE_DMA1_ID:
		dma_reg = chan->device->dma_reg;
		return ioread32(&dma_reg->acpl) & (~DMA_CDB_MSK);
	case PPC440SPE_XOR_ID:
		xor_reg = chan->device->xor_reg;
		return ioread32be(&xor_reg->ccbalr);
	}
	return 0;
}

/**
 * ppc440spe_chan_run - enable the channel
 */
static void ppc440spe_chan_run(struct ppc440spe_adma_chan *chan)
{
	struct xor_regs *xor_reg;

	switch (chan->device->id) {
	case PPC440SPE_DMA0_ID:
	case PPC440SPE_DMA1_ID:
		/* DMAs are always enabled, do nothing */
		break;
	case PPC440SPE_XOR_ID:
		/* drain write buffer */
		xor_reg = chan->device->xor_reg;

		/* fetch descriptor pointed to in <link> */
		iowrite32be(XOR_CRSR_64BA_BIT | XOR_CRSR_XAE_BIT,
			    &xor_reg->crsr);
		break;
	}
}

/******************************************************************************
 * ADMA device level
 ******************************************************************************/

static void ppc440spe_chan_start_null_xor(struct ppc440spe_adma_chan *chan);
static int ppc440spe_adma_alloc_chan_resources(struct dma_chan *chan);

static dma_cookie_t
ppc440spe_adma_tx_submit(struct dma_async_tx_descriptor *tx);

static void ppc440spe_adma_set_dest(struct ppc440spe_adma_desc_slot *tx,
				    dma_addr_t addr, int index);
static void
ppc440spe_adma_memcpy_xor_set_src(struct ppc440spe_adma_desc_slot *tx,
				  dma_addr_t addr, int index);

static void
ppc440spe_adma_pq_set_dest(struct ppc440spe_adma_desc_slot *tx,
			   dma_addr_t *paddr, unsigned long flags);
static void
ppc440spe_adma_pq_set_src(struct ppc440spe_adma_desc_slot *tx,
			  dma_addr_t addr, int index);
static void
ppc440spe_adma_pq_set_src_mult(struct ppc440spe_adma_desc_slot *tx,
			       unsigned char mult, int index, int dst_pos);
static void
ppc440spe_adma_pqzero_sum_set_dest(struct ppc440spe_adma_desc_slot *tx,
				   dma_addr_t paddr, dma_addr_t qaddr);

static struct page *ppc440spe_rxor_srcs[32];

/**
 * ppc440spe_can_rxor - check if the operands may be processed with RXOR
 */
static int ppc440spe_can_rxor(struct page **srcs, int src_cnt, size_t len)
{
	int i, order = 0, state = 0;
	int idx = 0;

	if (unlikely(!(src_cnt > 1)))
		return 0;

	BUG_ON(src_cnt > ARRAY_SIZE(ppc440spe_rxor_srcs));

	/* Skip holes in the source list before checking */
	for (i = 0; i < src_cnt; i++) {
		if (!srcs[i])
			continue;
		ppc440spe_rxor_srcs[idx++] = srcs[i];
	}
	src_cnt = idx;

	for (i = 1; i < src_cnt; i++) {
		char *cur_addr = page_address(ppc440spe_rxor_srcs[i]);
		char *old_addr = page_address(ppc440spe_rxor_srcs[i - 1]);

		switch (state) {
		case 0:
			if (cur_addr == old_addr + len) {
				/* direct RXOR */
				order = 1;
				state = 1;
			} else if (old_addr == cur_addr + len) {
				/* reverse RXOR */
				order = -1;
				state = 1;
			} else
				goto out;
			break;
		case 1:
			if ((i == src_cnt - 2) ||
			    (order == -1 && cur_addr != old_addr - len)) {
				order = 0;
				state = 0;
			} else if ((cur_addr == old_addr + len * order) ||
				   (cur_addr == old_addr + 2 * len) ||
				   (cur_addr == old_addr + 3 * len)) {
				state = 2;
			} else {
				order = 0;
				state = 0;
			}
			break;
		case 2:
			order = 0;
			state = 0;
			break;
		}
	}

out:
	if (state == 1 || state == 2)
		return 1;

	return 0;
}

/**
 * ppc440spe_adma_device_estimate - estimate the efficiency of processing
 *	the operation given on this channel. It's assumed that 'chan' is
 *	capable to process 'cap' type of operation.
 * @chan: channel to use
 * @cap: type of transaction
 * @dst_lst: array of destination pointers
 * @dst_cnt: number of destination operands
 * @src_lst: array of source pointers
 * @src_cnt: number of source operands
 * @src_sz: size of each source operand
 */
static int ppc440spe_adma_estimate(struct dma_chan *chan,
	enum dma_transaction_type cap, struct page **dst_lst, int dst_cnt,
	struct page **src_lst, int src_cnt, size_t src_sz)
{
	int ef = 1;

	if (cap == DMA_PQ || cap == DMA_PQ_VAL) {
		/* If RAID-6 capabilities were not activated don't try
		 * to use them
		 */
		if (unlikely(!ppc440spe_r6_enabled))
			return -1;
	}
	/*  In the current implementation of ppc440spe ADMA driver it
	 * makes sense to pick out only pq case, because it may be
	 * processed:
	 * (1) either using Biskup method on DMA2;
	 * (2) or on DMA0/1.
	 *  Thus we give a favour to (1) if the sources are suitable;
	 * else let it be processed on one of the DMA0/1 engines.
	 *  In the sum_product case where destination is also the
	 * source process it on DMA0/1 only.
	 */
	if (cap == DMA_PQ && chan->chan_id == PPC440SPE_XOR_ID) {

		if (dst_cnt == 1 && src_cnt == 2 && dst_lst[0] == src_lst[1])
			ef = 0; /* sum_product case, process on DMA0/1 */
		else if (ppc440spe_can_rxor(src_lst, src_cnt, src_sz))
			ef = 3; /* override (DMA0/1 + idle) */
		else
			ef = 0; /* can't process on DMA2 if !rxor */
	}

	/* channel idleness increases the priority */
	if (likely(ef) &&
	    !ppc440spe_chan_is_busy(to_ppc440spe_adma_chan(chan)))
		ef++;

	return ef;
}

struct dma_chan *
ppc440spe_async_tx_find_best_channel(enum dma_transaction_type cap,
	struct page **dst_lst, int dst_cnt, struct page **src_lst,
	int src_cnt, size_t src_sz)
{
	struct dma_chan *best_chan = NULL;
	struct ppc_dma_chan_ref *ref;
	int best_rank = -1;

	if (unlikely(!src_sz))
		return NULL;
	if (src_sz > PAGE_SIZE) {
		/*
		 * should a user of the api ever pass > PAGE_SIZE requests
		 * we sort out cases where temporary page-sized buffers
		 * are used.
		 */
		switch (cap) {
		case DMA_PQ:
			if (src_cnt == 1 && dst_lst[1] == src_lst[0])
				return NULL;
			if (src_cnt == 2 && dst_lst[1] == src_lst[1])
				return NULL;
			break;
		case DMA_PQ_VAL:
		case DMA_XOR_VAL:
			return NULL;
		default:
			break;
		}
	}

	list_for_each_entry(ref, &ppc440spe_adma_chan_list, node) {
		if (dma_has_cap(cap, ref->chan->device->cap_mask)) {
			int rank;

			rank = ppc440spe_adma_estimate(ref->chan, cap, dst_lst,
					dst_cnt, src_lst, src_cnt, src_sz);
			if (rank > best_rank) {
				best_rank = rank;
				best_chan = ref->chan;
			}
		}
	}

	return best_chan;
}
EXPORT_SYMBOL_GPL(ppc440spe_async_tx_find_best_channel);

/**
 * ppc440spe_get_group_entry - get group entry with index idx
 * @tdesc: is the last allocated slot in the group.
 */
static struct ppc440spe_adma_desc_slot *
ppc440spe_get_group_entry(struct ppc440spe_adma_desc_slot *tdesc, u32 entry_idx)
{
	struct ppc440spe_adma_desc_slot *iter = tdesc->group_head;
	int i = 0;

	if (entry_idx < 0 || entry_idx >= (tdesc->src_cnt + tdesc->dst_cnt)) {
		printk("%s: entry_idx %d, src_cnt %d, dst_cnt %d\n",
			__func__, entry_idx, tdesc->src_cnt, tdesc->dst_cnt);
		BUG();
	}

	list_for_each_entry(iter, &tdesc->group_list, chain_node) {
		if (i++ == entry_idx)
			break;
	}
	return iter;
}

/**
 * ppc440spe_adma_free_slots - flags descriptor slots for reuse
 * @slot: Slot to free
 * Caller must hold &ppc440spe_chan->lock while calling this function
 */
static void ppc440spe_adma_free_slots(struct ppc440spe_adma_desc_slot *slot,
				      struct ppc440spe_adma_chan *chan)
{
	int stride = slot->slots_per_op;

	while (stride--) {
		slot->slots_per_op = 0;
		slot = list_entry(slot->slot_node.next,
				struct ppc440spe_adma_desc_slot,
				slot_node);
	}
}

/**
 * ppc440spe_adma_run_tx_complete_actions - call functions to be called
 * upon completion
 */
static dma_cookie_t ppc440spe_adma_run_tx_complete_actions(
		struct ppc440spe_adma_desc_slot *desc,
		struct ppc440spe_adma_chan *chan,
		dma_cookie_t cookie)
{
	BUG_ON(desc->async_tx.cookie < 0);
	if (desc->async_tx.cookie > 0) {
		cookie = desc->async_tx.cookie;
		desc->async_tx.cookie = 0;

		dma_descriptor_unmap(&desc->async_tx);
		/* call the callback (must not sleep or submit new
		 * operations to this channel)
		 */
		dmaengine_desc_get_callback_invoke(&desc->async_tx, NULL);
	}

	/* run dependent operations */
	dma_run_dependencies(&desc->async_tx);

	return cookie;
}

/**
 * ppc440spe_adma_clean_slot - clean up CDB slot (if ack is set)
 */
static int ppc440spe_adma_clean_slot(struct ppc440spe_adma_desc_slot *desc,
		struct ppc440spe_adma_chan *chan)
{
	/* the client is allowed to attach dependent operations
	 * until 'ack' is set
	 */
	if (!async_tx_test_ack(&desc->async_tx))
		return 0;

	/* leave the last descriptor in the chain
	 * so we can append to it
	 */
	if (list_is_last(&desc->chain_node, &chan->chain) ||
	    desc->phys == ppc440spe_chan_get_current_descriptor(chan))
		return 1;

	if (chan->device->id != PPC440SPE_XOR_ID) {
		/* our DMA interrupt handler clears opc field of
		 * each processed descriptor. For all types of
		 * operations except for ZeroSum we do not actually
		 * need ack from the interrupt handler. ZeroSum is a
		 * special case since the result of this operation
		 * is available from the handler only, so if we see
		 * such type of descriptor (which is unprocessed yet)
		 * then leave it in chain.
		 */
		struct dma_cdb *cdb = desc->hw_desc;
		if (cdb->opc == DMA_CDB_OPC_DCHECK128)
			return 1;
	}

	dev_dbg(chan->device->common.dev, "\tfree slot %llx: %d stride: %d\n",
		desc->phys, desc->idx, desc->slots_per_op);

	list_del(&desc->chain_node);
	ppc440spe_adma_free_slots(desc, chan);
	return 0;
}

/**
 * __ppc440spe_adma_slot_cleanup - this is the common clean-up routine
 *	which runs through the channel CDBs list until reach the descriptor
 *	currently processed. When routine determines that all CDBs of group
 *	are completed then corresponding callbacks (if any) are called and slots
 *	are freed.
 */
static void __ppc440spe_adma_slot_cleanup(struct ppc440spe_adma_chan *chan)
{
	struct ppc440spe_adma_desc_slot *iter, *_iter, *group_start = NULL;
	dma_cookie_t cookie = 0;
	u32 current_desc = ppc440spe_chan_get_current_descriptor(chan);
	int busy = ppc440spe_chan_is_busy(chan);
	int seen_current = 0, slot_cnt = 0, slots_per_op = 0;

	dev_dbg(chan->device->common.dev, "ppc440spe adma%d: %s\n",
		chan->device->id, __func__);

	if (!current_desc) {
		/*  There were no transactions yet, so
		 * nothing to clean
		 */
		return;
	}

	/* free completed slots from the chain starting with
	 * the oldest descriptor
	 */
	list_for_each_entry_safe(iter, _iter, &chan->chain,
					chain_node) {
		dev_dbg(chan->device->common.dev, "\tcookie: %d slot: %d "
		    "busy: %d this_desc: %#llx next_desc: %#x "
		    "cur: %#x ack: %d\n",
		    iter->async_tx.cookie, iter->idx, busy, iter->phys,
		    ppc440spe_desc_get_link(iter, chan), current_desc,
		    async_tx_test_ack(&iter->async_tx));
		prefetch(_iter);
		prefetch(&_iter->async_tx);

		/* do not advance past the current descriptor loaded into the
		 * hardware channel,subsequent descriptors are either in process
		 * or have not been submitted
		 */
		if (seen_current)
			break;

		/* stop the search if we reach the current descriptor and the
		 * channel is busy, or if it appears that the current descriptor
		 * needs to be re-read (i.e. has been appended to)
		 */
		if (iter->phys == current_desc) {
			BUG_ON(seen_current++);
			if (busy || ppc440spe_desc_get_link(iter, chan)) {
				/* not all descriptors of the group have
				 * been completed; exit.
				 */
				break;
			}
		}

		/* detect the start of a group transaction */
		if (!slot_cnt && !slots_per_op) {
			slot_cnt = iter->slot_cnt;
			slots_per_op = iter->slots_per_op;
			if (slot_cnt <= slots_per_op) {
				slot_cnt = 0;
				slots_per_op = 0;
			}
		}

		if (slot_cnt) {
			if (!group_start)
				group_start = iter;
			slot_cnt -= slots_per_op;
		}

		/* all the members of a group are complete */
		if (slots_per_op != 0 && slot_cnt == 0) {
			struct ppc440spe_adma_desc_slot *grp_iter, *_grp_iter;
			int end_of_chain = 0;

			/* clean up the group */
			slot_cnt = group_start->slot_cnt;
			grp_iter = group_start;
			list_for_each_entry_safe_from(grp_iter, _grp_iter,
				&chan->chain, chain_node) {

				cookie = ppc440spe_adma_run_tx_complete_actions(
					grp_iter, chan, cookie);

				slot_cnt -= slots_per_op;
				end_of_chain = ppc440spe_adma_clean_slot(
				    grp_iter, chan);
				if (end_of_chain && slot_cnt) {
					/* Should wait for ZeroSum completion */
					if (cookie > 0)
						chan->common.completed_cookie = cookie;
					return;
				}

				if (slot_cnt == 0 || end_of_chain)
					break;
			}

			/* the group should be complete at this point */
			BUG_ON(slot_cnt);

			slots_per_op = 0;
			group_start = NULL;
			if (end_of_chain)
				break;
			else
				continue;
		} else if (slots_per_op) /* wait for group completion */
			continue;

		cookie = ppc440spe_adma_run_tx_complete_actions(iter, chan,
		    cookie);

		if (ppc440spe_adma_clean_slot(iter, chan))
			break;
	}

	BUG_ON(!seen_current);

	if (cookie > 0) {
		chan->common.completed_cookie = cookie;
		pr_debug("\tcompleted cookie %d\n", cookie);
	}

}

/**
 * ppc440spe_adma_tasklet - clean up watch-dog initiator
 */
static void ppc440spe_adma_tasklet(struct tasklet_struct *t)
{
	struct ppc440spe_adma_chan *chan = from_tasklet(chan, t, irq_tasklet);

	spin_lock_nested(&chan->lock, SINGLE_DEPTH_NESTING);
	__ppc440spe_adma_slot_cleanup(chan);
	spin_unlock(&chan->lock);
}

/**
 * ppc440spe_adma_slot_cleanup - clean up scheduled initiator
 */
static void ppc440spe_adma_slot_cleanup(struct ppc440spe_adma_chan *chan)
{
	spin_lock_bh(&chan->lock);
	__ppc440spe_adma_slot_cleanup(chan);
	spin_unlock_bh(&chan->lock);
}

/**
 * ppc440spe_adma_alloc_slots - allocate free slots (if any)
 */
static struct ppc440spe_adma_desc_slot *ppc440spe_adma_alloc_slots(
		struct ppc440spe_adma_chan *chan, int num_slots,
		int slots_per_op)
{
	struct ppc440spe_adma_desc_slot *iter = NULL, *_iter;
	struct ppc440spe_adma_desc_slot *alloc_start = NULL;
	struct list_head chain = LIST_HEAD_INIT(chain);
	int slots_found, retry = 0;


	BUG_ON(!num_slots || !slots_per_op);
	/* start search from the last allocated descrtiptor
	 * if a contiguous allocation can not be found start searching
	 * from the beginning of the list
	 */
retry:
	slots_found = 0;
	if (retry == 0)
		iter = chan->last_used;
	else
		iter = list_entry(&chan->all_slots,
				  struct ppc440spe_adma_desc_slot,
				  slot_node);
	list_for_each_entry_safe_continue(iter, _iter, &chan->all_slots,
	    slot_node) {
		prefetch(_iter);
		prefetch(&_iter->async_tx);
		if (iter->slots_per_op) {
			slots_found = 0;
			continue;
		}

		/* start the allocation if the slot is correctly aligned */
		if (!slots_found++)
			alloc_start = iter;

		if (slots_found == num_slots) {
			struct ppc440spe_adma_desc_slot *alloc_tail = NULL;
			struct ppc440spe_adma_desc_slot *last_used = NULL;

			iter = alloc_start;
			while (num_slots) {
				int i;
				/* pre-ack all but the last descriptor */
				if (num_slots != slots_per_op)
					async_tx_ack(&iter->async_tx);

				list_add_tail(&iter->chain_node, &chain);
				alloc_tail = iter;
				iter->async_tx.cookie = 0;
				iter->hw_next = NULL;
				iter->flags = 0;
				iter->slot_cnt = num_slots;
				iter->xor_check_result = NULL;
				for (i = 0; i < slots_per_op; i++) {
					iter->slots_per_op = slots_per_op - i;
					last_used = iter;
					iter = list_entry(iter->slot_node.next,
						struct ppc440spe_adma_desc_slot,
						slot_node);
				}
				num_slots -= slots_per_op;
			}
			alloc_tail->group_head = alloc_start;
			alloc_tail->async_tx.cookie = -EBUSY;
			list_splice(&chain, &alloc_tail->group_list);
			chan->last_used = last_used;
			return alloc_tail;
		}
	}
	if (!retry++)
		goto retry;

	/* try to free some slots if the allocation fails */
	tasklet_schedule(&chan->irq_tasklet);
	return NULL;
}

/**
 * ppc440spe_adma_alloc_chan_resources -  allocate pools for CDB slots
 */
static int ppc440spe_adma_alloc_chan_resources(struct dma_chan *chan)
{
	struct ppc440spe_adma_chan *ppc440spe_chan;
	struct ppc440spe_adma_desc_slot *slot = NULL;
	char *hw_desc;
	int i, db_sz;
	int init;

	ppc440spe_chan = to_ppc440spe_adma_chan(chan);
	init = ppc440spe_chan->slots_allocated ? 0 : 1;
	chan->chan_id = ppc440spe_chan->device->id;

	/* Allocate descriptor slots */
	i = ppc440spe_chan->slots_allocated;
	if (ppc440spe_chan->device->id != PPC440SPE_XOR_ID)
		db_sz = sizeof(struct dma_cdb);
	else
		db_sz = sizeof(struct xor_cb);

	for (; i < (ppc440spe_chan->device->pool_size / db_sz); i++) {
		slot = kzalloc(sizeof(struct ppc440spe_adma_desc_slot),
			       GFP_KERNEL);
		if (!slot) {
			printk(KERN_INFO "SPE ADMA Channel only initialized"
				" %d descriptor slots", i--);
			break;
		}

		hw_desc = (char *) ppc440spe_chan->device->dma_desc_pool_virt;
		slot->hw_desc = (void *) &hw_desc[i * db_sz];
		dma_async_tx_descriptor_init(&slot->async_tx, chan);
		slot->async_tx.tx_submit = ppc440spe_adma_tx_submit;
		INIT_LIST_HEAD(&slot->chain_node);
		INIT_LIST_HEAD(&slot->slot_node);
		INIT_LIST_HEAD(&slot->group_list);
		slot->phys = ppc440spe_chan->device->dma_desc_pool + i * db_sz;
		slot->idx = i;

		spin_lock_bh(&ppc440spe_chan->lock);
		ppc440spe_chan->slots_allocated++;
		list_add_tail(&slot->slot_node, &ppc440spe_chan->all_slots);
		spin_unlock_bh(&ppc440spe_chan->lock);
	}

	if (i && !ppc440spe_chan->last_used) {
		ppc440spe_chan->last_used =
			list_entry(ppc440spe_chan->all_slots.next,
				struct ppc440spe_adma_desc_slot,
				slot_node);
	}

	dev_dbg(ppc440spe_chan->device->common.dev,
		"ppc440spe adma%d: allocated %d descriptor slots\n",
		ppc440spe_chan->device->id, i);

	/* initialize the channel and the chain with a null operation */
	if (init) {
		switch (ppc440spe_chan->device->id) {
		case PPC440SPE_DMA0_ID:
		case PPC440SPE_DMA1_ID:
			ppc440spe_chan->hw_chain_inited = 0;
			/* Use WXOR for self-testing */
			if (!ppc440spe_r6_tchan)
				ppc440spe_r6_tchan = ppc440spe_chan;
			break;
		case PPC440SPE_XOR_ID:
			ppc440spe_chan_start_null_xor(ppc440spe_chan);
			break;
		default:
			BUG();
		}
		ppc440spe_chan->needs_unmap = 1;
	}

	return (i > 0) ? i : -ENOMEM;
}

/**
 * ppc440spe_rxor_set_region_data -
 */
static void ppc440spe_rxor_set_region(struct ppc440spe_adma_desc_slot *desc,
	u8 xor_arg_no, u32 mask)
{
	struct xor_cb *xcb = desc->hw_desc;

	xcb->ops[xor_arg_no].h |= mask;
}

/**
 * ppc440spe_rxor_set_src -
 */
static void ppc440spe_rxor_set_src(struct ppc440spe_adma_desc_slot *desc,
	u8 xor_arg_no, dma_addr_t addr)
{
	struct xor_cb *xcb = desc->hw_desc;

	xcb->ops[xor_arg_no].h |= DMA_CUED_XOR_BASE;
	xcb->ops[xor_arg_no].l = addr;
}

/**
 * ppc440spe_rxor_set_mult -
 */
static void ppc440spe_rxor_set_mult(struct ppc440spe_adma_desc_slot *desc,
	u8 xor_arg_no, u8 idx, u8 mult)
{
	struct xor_cb *xcb = desc->hw_desc;

	xcb->ops[xor_arg_no].h |= mult << (DMA_CUED_MULT1_OFF + idx * 8);
}

/**
 * ppc440spe_adma_check_threshold - append CDBs to h/w chain if threshold
 *	has been achieved
 */
static void ppc440spe_adma_check_threshold(struct ppc440spe_adma_chan *chan)
{
	dev_dbg(chan->device->common.dev, "ppc440spe adma%d: pending: %d\n",
		chan->device->id, chan->pending);

	if (chan->pending >= PPC440SPE_ADMA_THRESHOLD) {
		chan->pending = 0;
		ppc440spe_chan_append(chan);
	}
}

/**
 * ppc440spe_adma_tx_submit - submit new descriptor group to the channel
 *	(it's not necessary that descriptors will be submitted to the h/w
 *	chains too right now)
 */
static dma_cookie_t ppc440spe_adma_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct ppc440spe_adma_desc_slot *sw_desc;
	struct ppc440spe_adma_chan *chan = to_ppc440spe_adma_chan(tx->chan);
	struct ppc440spe_adma_desc_slot *group_start, *old_chain_tail;
	int slot_cnt;
	int slots_per_op;
	dma_cookie_t cookie;

	sw_desc = tx_to_ppc440spe_adma_slot(tx);

	group_start = sw_desc->group_head;
	slot_cnt = group_start->slot_cnt;
	slots_per_op = group_start->slots_per_op;

	spin_lock_bh(&chan->lock);
	cookie = dma_cookie_assign(tx);

	if (unlikely(list_empty(&chan->chain))) {
		/* first peer */
		list_splice_init(&sw_desc->group_list, &chan->chain);
		chan_first_cdb[chan->device->id] = group_start;
	} else {
		/* isn't first peer, bind CDBs to chain */
		old_chain_tail = list_entry(chan->chain.prev,
					struct ppc440spe_adma_desc_slot,
					chain_node);
		list_splice_init(&sw_desc->group_list,
		    &old_chain_tail->chain_node);
		/* fix up the hardware chain */
		ppc440spe_desc_set_link(chan, old_chain_tail, group_start);
	}

	/* increment the pending count by the number of operations */
	chan->pending += slot_cnt / slots_per_op;
	ppc440spe_adma_check_threshold(chan);
	spin_unlock_bh(&chan->lock);

	dev_dbg(chan->device->common.dev,
		"ppc440spe adma%d: %s cookie: %d slot: %d tx %p\n",
		chan->device->id, __func__,
		sw_desc->async_tx.cookie, sw_desc->idx, sw_desc);

	return cookie;
}

/**
 * ppc440spe_adma_prep_dma_interrupt - prepare CDB for a pseudo DMA operation
 */
static struct dma_async_tx_descriptor *ppc440spe_adma_prep_dma_interrupt(
		struct dma_chan *chan, unsigned long flags)
{
	struct ppc440spe_adma_chan *ppc440spe_chan;
	struct ppc440spe_adma_desc_slot *sw_desc, *group_start;
	int slot_cnt, slots_per_op;

	ppc440spe_chan = to_ppc440spe_adma_chan(chan);

	dev_dbg(ppc440spe_chan->device->common.dev,
		"ppc440spe adma%d: %s\n", ppc440spe_chan->device->id,
		__func__);

	spin_lock_bh(&ppc440spe_chan->lock);
	slot_cnt = slots_per_op = 1;
	sw_desc = ppc440spe_adma_alloc_slots(ppc440spe_chan, slot_cnt,
			slots_per_op);
	if (sw_desc) {
		group_start = sw_desc->group_head;
		ppc440spe_desc_init_interrupt(group_start, ppc440spe_chan);
		group_start->unmap_len = 0;
		sw_desc->async_tx.flags = flags;
	}
	spin_unlock_bh(&ppc440spe_chan->lock);

	return sw_desc ? &sw_desc->async_tx : NULL;
}

/**
 * ppc440spe_adma_prep_dma_memcpy - prepare CDB for a MEMCPY operation
 */
static struct dma_async_tx_descriptor *ppc440spe_adma_prep_dma_memcpy(
		struct dma_chan *chan, dma_addr_t dma_dest,
		dma_addr_t dma_src, size_t len, unsigned long flags)
{
	struct ppc440spe_adma_chan *ppc440spe_chan;
	struct ppc440spe_adma_desc_slot *sw_desc, *group_start;
	int slot_cnt, slots_per_op;

	ppc440spe_chan = to_ppc440spe_adma_chan(chan);

	if (unlikely(!len))
		return NULL;

	BUG_ON(len > PPC440SPE_ADMA_DMA_MAX_BYTE_COUNT);

	spin_lock_bh(&ppc440spe_chan->lock);

	dev_dbg(ppc440spe_chan->device->common.dev,
		"ppc440spe adma%d: %s len: %u int_en %d\n",
		ppc440spe_chan->device->id, __func__, len,
		flags & DMA_PREP_INTERRUPT ? 1 : 0);
	slot_cnt = slots_per_op = 1;
	sw_desc = ppc440spe_adma_alloc_slots(ppc440spe_chan, slot_cnt,
		slots_per_op);
	if (sw_desc) {
		group_start = sw_desc->group_head;
		ppc440spe_desc_init_memcpy(group_start, flags);
		ppc440spe_adma_set_dest(group_start, dma_dest, 0);
		ppc440spe_adma_memcpy_xor_set_src(group_start, dma_src, 0);
		ppc440spe_desc_set_byte_count(group_start, ppc440spe_chan, len);
		sw_desc->unmap_len = len;
		sw_desc->async_tx.flags = flags;
	}
	spin_unlock_bh(&ppc440spe_chan->lock);

	return sw_desc ? &sw_desc->async_tx : NULL;
}

/**
 * ppc440spe_adma_prep_dma_xor - prepare CDB for a XOR operation
 */
static struct dma_async_tx_descriptor *ppc440spe_adma_prep_dma_xor(
		struct dma_chan *chan, dma_addr_t dma_dest,
		dma_addr_t *dma_src, u32 src_cnt, size_t len,
		unsigned long flags)
{
	struct ppc440spe_adma_chan *ppc440spe_chan;
	struct ppc440spe_adma_desc_slot *sw_desc, *group_start;
	int slot_cnt, slots_per_op;

	ppc440spe_chan = to_ppc440spe_adma_chan(chan);

	ADMA_LL_DBG(prep_dma_xor_dbg(ppc440spe_chan->device->id,
				     dma_dest, dma_src, src_cnt));
	if (unlikely(!len))
		return NULL;
	BUG_ON(len > PPC440SPE_ADMA_XOR_MAX_BYTE_COUNT);

	dev_dbg(ppc440spe_chan->device->common.dev,
		"ppc440spe adma%d: %s src_cnt: %d len: %u int_en: %d\n",
		ppc440spe_chan->device->id, __func__, src_cnt, len,
		flags & DMA_PREP_INTERRUPT ? 1 : 0);

	spin_lock_bh(&ppc440spe_chan->lock);
	slot_cnt = ppc440spe_chan_xor_slot_count(len, src_cnt, &slots_per_op);
	sw_desc = ppc440spe_adma_alloc_slots(ppc440spe_chan, slot_cnt,
			slots_per_op);
	if (sw_desc) {
		group_start = sw_desc->group_head;
		ppc440spe_desc_init_xor(group_start, src_cnt, flags);
		ppc440spe_adma_set_dest(group_start, dma_dest, 0);
		while (src_cnt--)
			ppc440spe_adma_memcpy_xor_set_src(group_start,
				dma_src[src_cnt], src_cnt);
		ppc440spe_desc_set_byte_count(group_start, ppc440spe_chan, len);
		sw_desc->unmap_len = len;
		sw_desc->async_tx.flags = flags;
	}
	spin_unlock_bh(&ppc440spe_chan->lock);

	return sw_desc ? &sw_desc->async_tx : NULL;
}

static inline void
ppc440spe_desc_set_xor_src_cnt(struct ppc440spe_adma_desc_slot *desc,
				int src_cnt);
static void ppc440spe_init_rxor_cursor(struct ppc440spe_rxor *cursor);

/**
 * ppc440spe_adma_init_dma2rxor_slot -
 */
static void ppc440spe_adma_init_dma2rxor_slot(
		struct ppc440spe_adma_desc_slot *desc,
		dma_addr_t *src, int src_cnt)
{
	int i;

	/* initialize CDB */
	for (i = 0; i < src_cnt; i++) {
		ppc440spe_adma_dma2rxor_prep_src(desc, &desc->rxor_cursor, i,
						 desc->src_cnt, (u32)src[i]);
	}
}

/**
 * ppc440spe_dma01_prep_mult -
 * for Q operation where destination is also the source
 */
static struct ppc440spe_adma_desc_slot *ppc440spe_dma01_prep_mult(
		struct ppc440spe_adma_chan *ppc440spe_chan,
		dma_addr_t *dst, int dst_cnt, dma_addr_t *src, int src_cnt,
		const unsigned char *scf, size_t len, unsigned long flags)
{
	struct ppc440spe_adma_desc_slot *sw_desc = NULL;
	unsigned long op = 0;
	int slot_cnt;

	set_bit(PPC440SPE_DESC_WXOR, &op);
	slot_cnt = 2;

	spin_lock_bh(&ppc440spe_chan->lock);

	/* use WXOR, each descriptor occupies one slot */
	sw_desc = ppc440spe_adma_alloc_slots(ppc440spe_chan, slot_cnt, 1);
	if (sw_desc) {
		struct ppc440spe_adma_chan *chan;
		struct ppc440spe_adma_desc_slot *iter;
		struct dma_cdb *hw_desc;

		chan = to_ppc440spe_adma_chan(sw_desc->async_tx.chan);
		set_bits(op, &sw_desc->flags);
		sw_desc->src_cnt = src_cnt;
		sw_desc->dst_cnt = dst_cnt;
		/* First descriptor, zero data in the destination and copy it
		 * to q page using MULTICAST transfer.
		 */
		iter = list_first_entry(&sw_desc->group_list,
					struct ppc440spe_adma_desc_slot,
					chain_node);
		memset(iter->hw_desc, 0, sizeof(struct dma_cdb));
		/* set 'next' pointer */
		iter->hw_next = list_entry(iter->chain_node.next,
					   struct ppc440spe_adma_desc_slot,
					   chain_node);
		clear_bit(PPC440SPE_DESC_INT, &iter->flags);
		hw_desc = iter->hw_desc;
		hw_desc->opc = DMA_CDB_OPC_MULTICAST;

		ppc440spe_desc_set_dest_addr(iter, chan,
					     DMA_CUED_XOR_BASE, dst[0], 0);
		ppc440spe_desc_set_dest_addr(iter, chan, 0, dst[1], 1);
		ppc440spe_desc_set_src_addr(iter, chan, 0, DMA_CUED_XOR_HB,
					    src[0]);
		ppc440spe_desc_set_byte_count(iter, ppc440spe_chan, len);
		iter->unmap_len = len;

		/*
		 * Second descriptor, multiply data from the q page
		 * and store the result in real destination.
		 */
		iter = list_first_entry(&iter->chain_node,
					struct ppc440spe_adma_desc_slot,
					chain_node);
		memset(iter->hw_desc, 0, sizeof(struct dma_cdb));
		iter->hw_next = NULL;
		if (flags & DMA_PREP_INTERRUPT)
			set_bit(PPC440SPE_DESC_INT, &iter->flags);
		else
			clear_bit(PPC440SPE_DESC_INT, &iter->flags);

		hw_desc = iter->hw_desc;
		hw_desc->opc = DMA_CDB_OPC_MV_SG1_SG2;
		ppc440spe_desc_set_src_addr(iter, chan, 0,
					    DMA_CUED_XOR_HB, dst[1]);
		ppc440spe_desc_set_dest_addr(iter, chan,
					     DMA_CUED_XOR_BASE, dst[0], 0);

		ppc440spe_desc_set_src_mult(iter, chan, DMA_CUED_MULT1_OFF,
					    DMA_CDB_SG_DST1, scf[0]);
		ppc440spe_desc_set_byte_count(iter, ppc440spe_chan, len);
		iter->unmap_len = len;
		sw_desc->async_tx.flags = flags;
	}

	spin_unlock_bh(&ppc440spe_chan->lock);

	return sw_desc;
}

/**
 * ppc440spe_dma01_prep_sum_product -
 * Dx = A*(P+Pxy) + B*(Q+Qxy) operation where destination is also
 * the source.
 */
static struct ppc440spe_adma_desc_slot *ppc440spe_dma01_prep_sum_product(
		struct ppc440spe_adma_chan *ppc440spe_chan,
		dma_addr_t *dst, dma_addr_t *src, int src_cnt,
		const unsigned char *scf, size_t len, unsigned long flags)
{
	struct ppc440spe_adma_desc_slot *sw_desc = NULL;
	unsigned long op = 0;
	int slot_cnt;

	set_bit(PPC440SPE_DESC_WXOR, &op);
	slot_cnt = 3;

	spin_lock_bh(&ppc440spe_chan->lock);

	/* WXOR, each descriptor occupies one slot */
	sw_desc = ppc440spe_adma_alloc_slots(ppc440spe_chan, slot_cnt, 1);
	if (sw_desc) {
		struct ppc440spe_adma_chan *chan;
		struct ppc440spe_adma_desc_slot *iter;
		struct dma_cdb *hw_desc;

		chan = to_ppc440spe_adma_chan(sw_desc->async_tx.chan);
		set_bits(op, &sw_desc->flags);
		sw_desc->src_cnt = src_cnt;
		sw_desc->dst_cnt = 1;
		/* 1st descriptor, src[1] data to q page and zero destination */
		iter = list_first_entry(&sw_desc->group_list,
					struct ppc440spe_adma_desc_slot,
					chain_node);
		memset(iter->hw_desc, 0, sizeof(struct dma_cdb));
		iter->hw_next = list_entry(iter->chain_node.next,
					   struct ppc440spe_adma_desc_slot,
					   chain_node);
		clear_bit(PPC440SPE_DESC_INT, &iter->flags);
		hw_desc = iter->hw_desc;
		hw_desc->opc = DMA_CDB_OPC_MULTICAST;

		ppc440spe_desc_set_dest_addr(iter, chan, DMA_CUED_XOR_BASE,
					     *dst, 0);
		ppc440spe_desc_set_dest_addr(iter, chan, 0,
					     ppc440spe_chan->qdest, 1);
		ppc440spe_desc_set_src_addr(iter, chan, 0, DMA_CUED_XOR_HB,
					    src[1]);
		ppc440spe_desc_set_byte_count(iter, ppc440spe_chan, len);
		iter->unmap_len = len;

		/* 2nd descriptor, multiply src[1] data and store the
		 * result in destination */
		iter = list_first_entry(&iter->chain_node,
					struct ppc440spe_adma_desc_slot,
					chain_node);
		memset(iter->hw_desc, 0, sizeof(struct dma_cdb));
		/* set 'next' pointer */
		iter->hw_next = list_entry(iter->chain_node.next,
					   struct ppc440spe_adma_desc_slot,
					   chain_node);
		if (flags & DMA_PREP_INTERRUPT)
			set_bit(PPC440SPE_DESC_INT, &iter->flags);
		else
			clear_bit(PPC440SPE_DESC_INT, &iter->flags);

		hw_desc = iter->hw_desc;
		hw_desc->opc = DMA_CDB_OPC_MV_SG1_SG2;
		ppc440spe_desc_set_src_addr(iter, chan, 0, DMA_CUED_XOR_HB,
					    ppc440spe_chan->qdest);
		ppc440spe_desc_set_dest_addr(iter, chan, DMA_CUED_XOR_BASE,
					     *dst, 0);
		ppc440spe_desc_set_src_mult(iter, chan,	DMA_CUED_MULT1_OFF,
					    DMA_CDB_SG_DST1, scf[1]);
		ppc440spe_desc_set_byte_count(iter, ppc440spe_chan, len);
		iter->unmap_len = len;

		/*
		 * 3rd descriptor, multiply src[0] data and xor it
		 * with destination
		 */
		iter = list_first_entry(&iter->chain_node,
					struct ppc440spe_adma_desc_slot,
					chain_node);
		memset(iter->hw_desc, 0, sizeof(struct dma_cdb));
		iter->hw_next = NULL;
		if (flags & DMA_PREP_INTERRUPT)
			set_bit(PPC440SPE_DESC_INT, &iter->flags);
		else
			clear_bit(PPC440SPE_DESC_INT, &iter->flags);

		hw_desc = iter->hw_desc;
		hw_desc->opc = DMA_CDB_OPC_MV_SG1_SG2;
		ppc440spe_desc_set_src_addr(iter, chan, 0, DMA_CUED_XOR_HB,
					    src[0]);
		ppc440spe_desc_set_dest_addr(iter, chan, DMA_CUED_XOR_BASE,
					     *dst, 0);
		ppc440spe_desc_set_src_mult(iter, chan, DMA_CUED_MULT1_OFF,
					    DMA_CDB_SG_DST1, scf[0]);
		ppc440spe_desc_set_byte_count(iter, ppc440spe_chan, len);
		iter->unmap_len = len;
		sw_desc->async_tx.flags = flags;
	}

	spin_unlock_bh(&ppc440spe_chan->lock);

	return sw_desc;
}

static struct ppc440spe_adma_desc_slot *ppc440spe_dma01_prep_pq(
		struct ppc440spe_adma_chan *ppc440spe_chan,
		dma_addr_t *dst, int dst_cnt, dma_addr_t *src, int src_cnt,
		const unsigned char *scf, size_t len, unsigned long flags)
{
	int slot_cnt;
	struct ppc440spe_adma_desc_slot *sw_desc = NULL, *iter;
	unsigned long op = 0;
	unsigned char mult = 1;

	pr_debug("%s: dst_cnt %d, src_cnt %d, len %d\n",
		 __func__, dst_cnt, src_cnt, len);
	/*  select operations WXOR/RXOR depending on the
	 * source addresses of operators and the number
	 * of destinations (RXOR support only Q-parity calculations)
	 */
	set_bit(PPC440SPE_DESC_WXOR, &op);
	if (!test_and_set_bit(PPC440SPE_RXOR_RUN, &ppc440spe_rxor_state)) {
		/* no active RXOR;
		 * do RXOR if:
		 * - there are more than 1 source,
		 * - len is aligned on 512-byte boundary,
		 * - source addresses fit to one of 4 possible regions.
		 */
		if (src_cnt > 1 &&
		    !(len & MQ0_CF2H_RXOR_BS_MASK) &&
		    (src[0] + len) == src[1]) {
			/* may do RXOR R1 R2 */
			set_bit(PPC440SPE_DESC_RXOR, &op);
			if (src_cnt != 2) {
				/* may try to enhance region of RXOR */
				if ((src[1] + len) == src[2]) {
					/* do RXOR R1 R2 R3 */
					set_bit(PPC440SPE_DESC_RXOR123,
						&op);
				} else if ((src[1] + len * 2) == src[2]) {
					/* do RXOR R1 R2 R4 */
					set_bit(PPC440SPE_DESC_RXOR124, &op);
				} else if ((src[1] + len * 3) == src[2]) {
					/* do RXOR R1 R2 R5 */
					set_bit(PPC440SPE_DESC_RXOR125,
						&op);
				} else {
					/* do RXOR R1 R2 */
					set_bit(PPC440SPE_DESC_RXOR12,
						&op);
				}
			} else {
				/* do RXOR R1 R2 */
				set_bit(PPC440SPE_DESC_RXOR12, &op);
			}
		}

		if (!test_bit(PPC440SPE_DESC_RXOR, &op)) {
			/* can not do this operation with RXOR */
			clear_bit(PPC440SPE_RXOR_RUN,
				&ppc440spe_rxor_state);
		} else {
			/* can do; set block size right now */
			ppc440spe_desc_set_rxor_block_size(len);
		}
	}

	/* Number of necessary slots depends on operation type selected */
	if (!test_bit(PPC440SPE_DESC_RXOR, &op)) {
		/*  This is a WXOR only chain. Need descriptors for each
		 * source to GF-XOR them with WXOR, and need descriptors
		 * for each destination to zero them with WXOR
		 */
		slot_cnt = src_cnt;

		if (flags & DMA_PREP_ZERO_P) {
			slot_cnt++;
			set_bit(PPC440SPE_ZERO_P, &op);
		}
		if (flags & DMA_PREP_ZERO_Q) {
			slot_cnt++;
			set_bit(PPC440SPE_ZERO_Q, &op);
		}
	} else {
		/*  Need 1/2 descriptor for RXOR operation, and
		 * need (src_cnt - (2 or 3)) for WXOR of sources
		 * remained (if any)
		 */
		slot_cnt = dst_cnt;

		if (flags & DMA_PREP_ZERO_P)
			set_bit(PPC440SPE_ZERO_P, &op);
		if (flags & DMA_PREP_ZERO_Q)
			set_bit(PPC440SPE_ZERO_Q, &op);

		if (test_bit(PPC440SPE_DESC_RXOR12, &op))
			slot_cnt += src_cnt - 2;
		else
			slot_cnt += src_cnt - 3;

		/*  Thus we have either RXOR only chain or
		 * mixed RXOR/WXOR
		 */
		if (slot_cnt == dst_cnt)
			/* RXOR only chain */
			clear_bit(PPC440SPE_DESC_WXOR, &op);
	}

	spin_lock_bh(&ppc440spe_chan->lock);
	/* for both RXOR/WXOR each descriptor occupies one slot */
	sw_desc = ppc440spe_adma_alloc_slots(ppc440spe_chan, slot_cnt, 1);
	if (sw_desc) {
		ppc440spe_desc_init_dma01pq(sw_desc, dst_cnt, src_cnt,
				flags, op);

		/* setup dst/src/mult */
		pr_debug("%s: set dst descriptor 0, 1: 0x%016llx, 0x%016llx\n",
			 __func__, dst[0], dst[1]);
		ppc440spe_adma_pq_set_dest(sw_desc, dst, flags);
		while (src_cnt--) {
			ppc440spe_adma_pq_set_src(sw_desc, src[src_cnt],
						  src_cnt);

			/* NOTE: "Multi = 0 is equivalent to = 1" as it
			 * stated in 440SPSPe_RAID6_Addendum_UM_1_17.pdf
			 * doesn't work for RXOR with DMA0/1! Instead, multi=0
			 * leads to zeroing source data after RXOR.
			 * So, for P case set-up mult=1 explicitly.
			 */
			if (!(flags & DMA_PREP_PQ_DISABLE_Q))
				mult = scf[src_cnt];
			ppc440spe_adma_pq_set_src_mult(sw_desc,
				mult, src_cnt,  dst_cnt - 1);
		}

		/* Setup byte count foreach slot just allocated */
		sw_desc->async_tx.flags = flags;
		list_for_each_entry(iter, &sw_desc->group_list,
				chain_node) {
			ppc440spe_desc_set_byte_count(iter,
				ppc440spe_chan, len);
			iter->unmap_len = len;
		}
	}
	spin_unlock_bh(&ppc440spe_chan->lock);

	return sw_desc;
}

static struct ppc440spe_adma_desc_slot *ppc440spe_dma2_prep_pq(
		struct ppc440spe_adma_chan *ppc440spe_chan,
		dma_addr_t *dst, int dst_cnt, dma_addr_t *src, int src_cnt,
		const unsigned char *scf, size_t len, unsigned long flags)
{
	int slot_cnt, descs_per_op;
	struct ppc440spe_adma_desc_slot *sw_desc = NULL, *iter;
	unsigned long op = 0;
	unsigned char mult = 1;

	BUG_ON(!dst_cnt);
	/*pr_debug("%s: dst_cnt %d, src_cnt %d, len %d\n",
		 __func__, dst_cnt, src_cnt, len);*/

	spin_lock_bh(&ppc440spe_chan->lock);
	descs_per_op = ppc440spe_dma2_pq_slot_count(src, src_cnt, len);
	if (descs_per_op < 0) {
		spin_unlock_bh(&ppc440spe_chan->lock);
		return NULL;
	}

	/* depending on number of sources we have 1 or 2 RXOR chains */
	slot_cnt = descs_per_op * dst_cnt;

	sw_desc = ppc440spe_adma_alloc_slots(ppc440spe_chan, slot_cnt, 1);
	if (sw_desc) {
		op = slot_cnt;
		sw_desc->async_tx.flags = flags;
		list_for_each_entry(iter, &sw_desc->group_list, chain_node) {
			ppc440spe_desc_init_dma2pq(iter, dst_cnt, src_cnt,
				--op ? 0 : flags);
			ppc440spe_desc_set_byte_count(iter, ppc440spe_chan,
				len);
			iter->unmap_len = len;

			ppc440spe_init_rxor_cursor(&(iter->rxor_cursor));
			iter->rxor_cursor.len = len;
			iter->descs_per_op = descs_per_op;
		}
		op = 0;
		list_for_each_entry(iter, &sw_desc->group_list, chain_node) {
			op++;
			if (op % descs_per_op == 0)
				ppc440spe_adma_init_dma2rxor_slot(iter, src,
								  src_cnt);
			if (likely(!list_is_last(&iter->chain_node,
						 &sw_desc->group_list))) {
				/* set 'next' pointer */
				iter->hw_next =
					list_entry(iter->chain_node.next,
						struct ppc440spe_adma_desc_slot,
						chain_node);
				ppc440spe_xor_set_link(iter, iter->hw_next);
			} else {
				/* this is the last descriptor. */
				iter->hw_next = NULL;
			}
		}

		/* fixup head descriptor */
		sw_desc->dst_cnt = dst_cnt;
		if (flags & DMA_PREP_ZERO_P)
			set_bit(PPC440SPE_ZERO_P, &sw_desc->flags);
		if (flags & DMA_PREP_ZERO_Q)
			set_bit(PPC440SPE_ZERO_Q, &sw_desc->flags);

		/* setup dst/src/mult */
		ppc440spe_adma_pq_set_dest(sw_desc, dst, flags);

		while (src_cnt--) {
			/* handle descriptors (if dst_cnt == 2) inside
			 * the ppc440spe_adma_pq_set_srcxxx() functions
			 */
			ppc440spe_adma_pq_set_src(sw_desc, src[src_cnt],
						  src_cnt);
			if (!(flags & DMA_PREP_PQ_DISABLE_Q))
				mult = scf[src_cnt];
			ppc440spe_adma_pq_set_src_mult(sw_desc,
					mult, src_cnt, dst_cnt - 1);
		}
	}
	spin_unlock_bh(&ppc440spe_chan->lock);
	ppc440spe_desc_set_rxor_block_size(len);
	return sw_desc;
}

/**
 * ppc440spe_adma_prep_dma_pq - prepare CDB (group) for a GF-XOR operation
 */
static struct dma_async_tx_descriptor *ppc440spe_adma_prep_dma_pq(
		struct dma_chan *chan, dma_addr_t *dst, dma_addr_t *src,
		unsigned int src_cnt, const unsigned char *scf,
		size_t len, unsigned long flags)
{
	struct ppc440spe_adma_chan *ppc440spe_chan;
	struct ppc440spe_adma_desc_slot *sw_desc = NULL;
	int dst_cnt = 0;

	ppc440spe_chan = to_ppc440spe_adma_chan(chan);

	ADMA_LL_DBG(prep_dma_pq_dbg(ppc440spe_chan->device->id,
				    dst, src, src_cnt));
	BUG_ON(!len);
	BUG_ON(len > PPC440SPE_ADMA_XOR_MAX_BYTE_COUNT);
	BUG_ON(!src_cnt);

	if (src_cnt == 1 && dst[1] == src[0]) {
		dma_addr_t dest[2];

		/* dst[1] is real destination (Q) */
		dest[0] = dst[1];
		/* this is the page to multicast source data to */
		dest[1] = ppc440spe_chan->qdest;
		sw_desc = ppc440spe_dma01_prep_mult(ppc440spe_chan,
				dest, 2, src, src_cnt, scf, len, flags);
		return sw_desc ? &sw_desc->async_tx : NULL;
	}

	if (src_cnt == 2 && dst[1] == src[1]) {
		sw_desc = ppc440spe_dma01_prep_sum_product(ppc440spe_chan,
					&dst[1], src, 2, scf, len, flags);
		return sw_desc ? &sw_desc->async_tx : NULL;
	}

	if (!(flags & DMA_PREP_PQ_DISABLE_P)) {
		BUG_ON(!dst[0]);
		dst_cnt++;
		flags |= DMA_PREP_ZERO_P;
	}

	if (!(flags & DMA_PREP_PQ_DISABLE_Q)) {
		BUG_ON(!dst[1]);
		dst_cnt++;
		flags |= DMA_PREP_ZERO_Q;
	}

	BUG_ON(!dst_cnt);

	dev_dbg(ppc440spe_chan->device->common.dev,
		"ppc440spe adma%d: %s src_cnt: %d len: %u int_en: %d\n",
		ppc440spe_chan->device->id, __func__, src_cnt, len,
		flags & DMA_PREP_INTERRUPT ? 1 : 0);

	switch (ppc440spe_chan->device->id) {
	case PPC440SPE_DMA0_ID:
	case PPC440SPE_DMA1_ID:
		sw_desc = ppc440spe_dma01_prep_pq(ppc440spe_chan,
				dst, dst_cnt, src, src_cnt, scf,
				len, flags);
		break;

	case PPC440SPE_XOR_ID:
		sw_desc = ppc440spe_dma2_prep_pq(ppc440spe_chan,
				dst, dst_cnt, src, src_cnt, scf,
				len, flags);
		break;
	}

	return sw_desc ? &sw_desc->async_tx : NULL;
}

/**
 * ppc440spe_adma_prep_dma_pqzero_sum - prepare CDB group for
 * a PQ_ZERO_SUM operation
 */
static struct dma_async_tx_descriptor *ppc440spe_adma_prep_dma_pqzero_sum(
		struct dma_chan *chan, dma_addr_t *pq, dma_addr_t *src,
		unsigned int src_cnt, const unsigned char *scf, size_t len,
		enum sum_check_flags *pqres, unsigned long flags)
{
	struct ppc440spe_adma_chan *ppc440spe_chan;
	struct ppc440spe_adma_desc_slot *sw_desc, *iter;
	dma_addr_t pdest, qdest;
	int slot_cnt, slots_per_op, idst, dst_cnt;

	ppc440spe_chan = to_ppc440spe_adma_chan(chan);

	if (flags & DMA_PREP_PQ_DISABLE_P)
		pdest = 0;
	else
		pdest = pq[0];

	if (flags & DMA_PREP_PQ_DISABLE_Q)
		qdest = 0;
	else
		qdest = pq[1];

	ADMA_LL_DBG(prep_dma_pqzero_sum_dbg(ppc440spe_chan->device->id,
					    src, src_cnt, scf));

	/* Always use WXOR for P/Q calculations (two destinations).
	 * Need 1 or 2 extra slots to verify results are zero.
	 */
	idst = dst_cnt = (pdest && qdest) ? 2 : 1;

	/* One additional slot per destination to clone P/Q
	 * before calculation (we have to preserve destinations).
	 */
	slot_cnt = src_cnt + dst_cnt * 2;
	slots_per_op = 1;

	spin_lock_bh(&ppc440spe_chan->lock);
	sw_desc = ppc440spe_adma_alloc_slots(ppc440spe_chan, slot_cnt,
					     slots_per_op);
	if (sw_desc) {
		ppc440spe_desc_init_dma01pqzero_sum(sw_desc, dst_cnt, src_cnt);

		/* Setup byte count for each slot just allocated */
		sw_desc->async_tx.flags = flags;
		list_for_each_entry(iter, &sw_desc->group_list, chain_node) {
			ppc440spe_desc_set_byte_count(iter, ppc440spe_chan,
						      len);
			iter->unmap_len = len;
		}

		if (pdest) {
			struct dma_cdb *hw_desc;
			struct ppc440spe_adma_chan *chan;

			iter = sw_desc->group_head;
			chan = to_ppc440spe_adma_chan(iter->async_tx.chan);
			memset(iter->hw_desc, 0, sizeof(struct dma_cdb));
			iter->hw_next = list_entry(iter->chain_node.next,
						struct ppc440spe_adma_desc_slot,
						chain_node);
			hw_desc = iter->hw_desc;
			hw_desc->opc = DMA_CDB_OPC_MV_SG1_SG2;
			iter->src_cnt = 0;
			iter->dst_cnt = 0;
			ppc440spe_desc_set_dest_addr(iter, chan, 0,
						     ppc440spe_chan->pdest, 0);
			ppc440spe_desc_set_src_addr(iter, chan, 0, 0, pdest);
			ppc440spe_desc_set_byte_count(iter, ppc440spe_chan,
						      len);
			iter->unmap_len = 0;
			/* override pdest to preserve original P */
			pdest = ppc440spe_chan->pdest;
		}
		if (qdest) {
			struct dma_cdb *hw_desc;
			struct ppc440spe_adma_chan *chan;

			iter = list_first_entry(&sw_desc->group_list,
						struct ppc440spe_adma_desc_slot,
						chain_node);
			chan = to_ppc440spe_adma_chan(iter->async_tx.chan);

			if (pdest) {
				iter = list_entry(iter->chain_node.next,
						struct ppc440spe_adma_desc_slot,
						chain_node);
			}

			memset(iter->hw_desc, 0, sizeof(struct dma_cdb));
			iter->hw_next = list_entry(iter->chain_node.next,
						struct ppc440spe_adma_desc_slot,
						chain_node);
			hw_desc = iter->hw_desc;
			hw_desc->opc = DMA_CDB_OPC_MV_SG1_SG2;
			iter->src_cnt = 0;
			iter->dst_cnt = 0;
			ppc440spe_desc_set_dest_addr(iter, chan, 0,
						     ppc440spe_chan->qdest, 0);
			ppc440spe_desc_set_src_addr(iter, chan, 0, 0, qdest);
			ppc440spe_desc_set_byte_count(iter, ppc440spe_chan,
						      len);
			iter->unmap_len = 0;
			/* override qdest to preserve original Q */
			qdest = ppc440spe_chan->qdest;
		}

		/* Setup destinations for P/Q ops */
		ppc440spe_adma_pqzero_sum_set_dest(sw_desc, pdest, qdest);

		/* Setup zero QWORDs into DCHECK CDBs */
		idst = dst_cnt;
		list_for_each_entry_reverse(iter, &sw_desc->group_list,
					    chain_node) {
			/*
			 * The last CDB corresponds to Q-parity check,
			 * the one before last CDB corresponds
			 * P-parity check
			 */
			if (idst == DMA_DEST_MAX_NUM) {
				if (idst == dst_cnt) {
					set_bit(PPC440SPE_DESC_QCHECK,
						&iter->flags);
				} else {
					set_bit(PPC440SPE_DESC_PCHECK,
						&iter->flags);
				}
			} else {
				if (qdest) {
					set_bit(PPC440SPE_DESC_QCHECK,
						&iter->flags);
				} else {
					set_bit(PPC440SPE_DESC_PCHECK,
						&iter->flags);
				}
			}
			iter->xor_check_result = pqres;

			/*
			 * set it to zero, if check fail then result will
			 * be updated
			 */
			*iter->xor_check_result = 0;
			ppc440spe_desc_set_dcheck(iter, ppc440spe_chan,
				ppc440spe_qword);

			if (!(--dst_cnt))
				break;
		}

		/* Setup sources and mults for P/Q ops */
		list_for_each_entry_continue_reverse(iter, &sw_desc->group_list,
						     chain_node) {
			struct ppc440spe_adma_chan *chan;
			u32 mult_dst;

			chan = to_ppc440spe_adma_chan(iter->async_tx.chan);
			ppc440spe_desc_set_src_addr(iter, chan, 0,
						    DMA_CUED_XOR_HB,
						    src[src_cnt - 1]);
			if (qdest) {
				mult_dst = (dst_cnt - 1) ? DMA_CDB_SG_DST2 :
							   DMA_CDB_SG_DST1;
				ppc440spe_desc_set_src_mult(iter, chan,
							    DMA_CUED_MULT1_OFF,
							    mult_dst,
							    scf[src_cnt - 1]);
			}
			if (!(--src_cnt))
				break;
		}
	}
	spin_unlock_bh(&ppc440spe_chan->lock);
	return sw_desc ? &sw_desc->async_tx : NULL;
}

/**
 * ppc440spe_adma_prep_dma_xor_zero_sum - prepare CDB group for
 * XOR ZERO_SUM operation
 */
static struct dma_async_tx_descriptor *ppc440spe_adma_prep_dma_xor_zero_sum(
		struct dma_chan *chan, dma_addr_t *src, unsigned int src_cnt,
		size_t len, enum sum_check_flags *result, unsigned long flags)
{
	struct dma_async_tx_descriptor *tx;
	dma_addr_t pq[2];

	/* validate P, disable Q */
	pq[0] = src[0];
	pq[1] = 0;
	flags |= DMA_PREP_PQ_DISABLE_Q;

	tx = ppc440spe_adma_prep_dma_pqzero_sum(chan, pq, &src[1],
						src_cnt - 1, 0, len,
						result, flags);
	return tx;
}

/**
 * ppc440spe_adma_set_dest - set destination address into descriptor
 */
static void ppc440spe_adma_set_dest(struct ppc440spe_adma_desc_slot *sw_desc,
		dma_addr_t addr, int index)
{
	struct ppc440spe_adma_chan *chan;

	BUG_ON(index >= sw_desc->dst_cnt);

	chan = to_ppc440spe_adma_chan(sw_desc->async_tx.chan);

	switch (chan->device->id) {
	case PPC440SPE_DMA0_ID:
	case PPC440SPE_DMA1_ID:
		/* to do: support transfers lengths >
		 * PPC440SPE_ADMA_DMA/XOR_MAX_BYTE_COUNT
		 */
		ppc440spe_desc_set_dest_addr(sw_desc->group_head,
			chan, 0, addr, index);
		break;
	case PPC440SPE_XOR_ID:
		sw_desc = ppc440spe_get_group_entry(sw_desc, index);
		ppc440spe_desc_set_dest_addr(sw_desc,
			chan, 0, addr, index);
		break;
	}
}

static void ppc440spe_adma_pq_zero_op(struct ppc440spe_adma_desc_slot *iter,
		struct ppc440spe_adma_chan *chan, dma_addr_t addr)
{
	/*  To clear destinations update the descriptor
	 * (P or Q depending on index) as follows:
	 * addr is destination (0 corresponds to SG2):
	 */
	ppc440spe_desc_set_dest_addr(iter, chan, DMA_CUED_XOR_BASE, addr, 0);

	/* ... and the addr is source: */
	ppc440spe_desc_set_src_addr(iter, chan, 0, DMA_CUED_XOR_HB, addr);

	/* addr is always SG2 then the mult is always DST1 */
	ppc440spe_desc_set_src_mult(iter, chan, DMA_CUED_MULT1_OFF,
				    DMA_CDB_SG_DST1, 1);
}

/**
 * ppc440spe_adma_pq_set_dest - set destination address into descriptor
 * for the PQXOR operation
 */
static void ppc440spe_adma_pq_set_dest(struct ppc440spe_adma_desc_slot *sw_desc,
		dma_addr_t *addrs, unsigned long flags)
{
	struct ppc440spe_adma_desc_slot *iter;
	struct ppc440spe_adma_chan *chan;
	dma_addr_t paddr, qaddr;
	dma_addr_t addr = 0, ppath, qpath;
	int index = 0, i;

	chan = to_ppc440spe_adma_chan(sw_desc->async_tx.chan);

	if (flags & DMA_PREP_PQ_DISABLE_P)
		paddr = 0;
	else
		paddr = addrs[0];

	if (flags & DMA_PREP_PQ_DISABLE_Q)
		qaddr = 0;
	else
		qaddr = addrs[1];

	if (!paddr || !qaddr)
		addr = paddr ? paddr : qaddr;

	switch (chan->device->id) {
	case PPC440SPE_DMA0_ID:
	case PPC440SPE_DMA1_ID:
		/* walk through the WXOR source list and set P/Q-destinations
		 * for each slot:
		 */
		if (!test_bit(PPC440SPE_DESC_RXOR, &sw_desc->flags)) {
			/* This is WXOR-only chain; may have 1/2 zero descs */
			if (test_bit(PPC440SPE_ZERO_P, &sw_desc->flags))
				index++;
			if (test_bit(PPC440SPE_ZERO_Q, &sw_desc->flags))
				index++;

			iter = ppc440spe_get_group_entry(sw_desc, index);
			if (addr) {
				/* one destination */
				list_for_each_entry_from(iter,
					&sw_desc->group_list, chain_node)
					ppc440spe_desc_set_dest_addr(iter, chan,
						DMA_CUED_XOR_BASE, addr, 0);
			} else {
				/* two destinations */
				list_for_each_entry_from(iter,
					&sw_desc->group_list, chain_node) {
					ppc440spe_desc_set_dest_addr(iter, chan,
						DMA_CUED_XOR_BASE, paddr, 0);
					ppc440spe_desc_set_dest_addr(iter, chan,
						DMA_CUED_XOR_BASE, qaddr, 1);
				}
			}

			if (index) {
				/*  To clear destinations update the descriptor
				 * (1st,2nd, or both depending on flags)
				 */
				index = 0;
				if (test_bit(PPC440SPE_ZERO_P,
						&sw_desc->flags)) {
					iter = ppc440spe_get_group_entry(
							sw_desc, index++);
					ppc440spe_adma_pq_zero_op(iter, chan,
							paddr);
				}

				if (test_bit(PPC440SPE_ZERO_Q,
						&sw_desc->flags)) {
					iter = ppc440spe_get_group_entry(
							sw_desc, index++);
					ppc440spe_adma_pq_zero_op(iter, chan,
							qaddr);
				}

				return;
			}
		} else {
			/* This is RXOR-only or RXOR/WXOR mixed chain */

			/* If we want to include destination into calculations,
			 * then make dest addresses cued with mult=1 (XOR).
			 */
			ppath = test_bit(PPC440SPE_ZERO_P, &sw_desc->flags) ?
					DMA_CUED_XOR_HB :
					DMA_CUED_XOR_BASE |
						(1 << DMA_CUED_MULT1_OFF);
			qpath = test_bit(PPC440SPE_ZERO_Q, &sw_desc->flags) ?
					DMA_CUED_XOR_HB :
					DMA_CUED_XOR_BASE |
						(1 << DMA_CUED_MULT1_OFF);

			/* Setup destination(s) in RXOR slot(s) */
			iter = ppc440spe_get_group_entry(sw_desc, index++);
			ppc440spe_desc_set_dest_addr(iter, chan,
						paddr ? ppath : qpath,
						paddr ? paddr : qaddr, 0);
			if (!addr) {
				/* two destinations */
				iter = ppc440spe_get_group_entry(sw_desc,
								 index++);
				ppc440spe_desc_set_dest_addr(iter, chan,
						qpath, qaddr, 0);
			}

			if (test_bit(PPC440SPE_DESC_WXOR, &sw_desc->flags)) {
				/* Setup destination(s) in remaining WXOR
				 * slots
				 */
				iter = ppc440spe_get_group_entry(sw_desc,
								 index);
				if (addr) {
					/* one destination */
					list_for_each_entry_from(iter,
					    &sw_desc->group_list,
					    chain_node)
						ppc440spe_desc_set_dest_addr(
							iter, chan,
							DMA_CUED_XOR_BASE,
							addr, 0);

				} else {
					/* two destinations */
					list_for_each_entry_from(iter,
					    &sw_desc->group_list,
					    chain_node) {
						ppc440spe_desc_set_dest_addr(
							iter, chan,
							DMA_CUED_XOR_BASE,
							paddr, 0);
						ppc440spe_desc_set_dest_addr(
							iter, chan,
							DMA_CUED_XOR_BASE,
							qaddr, 1);
					}
				}
			}

		}
		break;

	case PPC440SPE_XOR_ID:
		/* DMA2 descriptors have only 1 destination, so there are
		 * two chains - one for each dest.
		 * If we want to include destination into calculations,
		 * then make dest addresses cued with mult=1 (XOR).
		 */
		ppath = test_bit(PPC440SPE_ZERO_P, &sw_desc->flags) ?
				DMA_CUED_XOR_HB :
				DMA_CUED_XOR_BASE |
					(1 << DMA_CUED_MULT1_OFF);

		qpath = test_bit(PPC440SPE_ZERO_Q, &sw_desc->flags) ?
				DMA_CUED_XOR_HB :
				DMA_CUED_XOR_BASE |
					(1 << DMA_CUED_MULT1_OFF);

		iter = ppc440spe_get_group_entry(sw_desc, 0);
		for (i = 0; i < sw_desc->descs_per_op; i++) {
			ppc440spe_desc_set_dest_addr(iter, chan,
				paddr ? ppath : qpath,
				paddr ? paddr : qaddr, 0);
			iter = list_entry(iter->chain_node.next,
					  struct ppc440spe_adma_desc_slot,
					  chain_node);
		}

		if (!addr) {
			/* Two destinations; setup Q here */
			iter = ppc440spe_get_group_entry(sw_desc,
				sw_desc->descs_per_op);
			for (i = 0; i < sw_desc->descs_per_op; i++) {
				ppc440spe_desc_set_dest_addr(iter,
					chan, qpath, qaddr, 0);
				iter = list_entry(iter->chain_node.next,
						struct ppc440spe_adma_desc_slot,
						chain_node);
			}
		}

		break;
	}
}

/**
 * ppc440spe_adma_pq_zero_sum_set_dest - set destination address into descriptor
 * for the PQ_ZERO_SUM operation
 */
static void ppc440spe_adma_pqzero_sum_set_dest(
		struct ppc440spe_adma_desc_slot *sw_desc,
		dma_addr_t paddr, dma_addr_t qaddr)
{
	struct ppc440spe_adma_desc_slot *iter, *end;
	struct ppc440spe_adma_chan *chan;
	dma_addr_t addr = 0;
	int idx;

	chan = to_ppc440spe_adma_chan(sw_desc->async_tx.chan);

	/* walk through the WXOR source list and set P/Q-destinations
	 * for each slot
	 */
	idx = (paddr && qaddr) ? 2 : 1;
	/* set end */
	list_for_each_entry_reverse(end, &sw_desc->group_list,
				    chain_node) {
		if (!(--idx))
			break;
	}
	/* set start */
	idx = (paddr && qaddr) ? 2 : 1;
	iter = ppc440spe_get_group_entry(sw_desc, idx);

	if (paddr && qaddr) {
		/* two destinations */
		list_for_each_entry_from(iter, &sw_desc->group_list,
					 chain_node) {
			if (unlikely(iter == end))
				break;
			ppc440spe_desc_set_dest_addr(iter, chan,
						DMA_CUED_XOR_BASE, paddr, 0);
			ppc440spe_desc_set_dest_addr(iter, chan,
						DMA_CUED_XOR_BASE, qaddr, 1);
		}
	} else {
		/* one destination */
		addr = paddr ? paddr : qaddr;
		list_for_each_entry_from(iter, &sw_desc->group_list,
					 chain_node) {
			if (unlikely(iter == end))
				break;
			ppc440spe_desc_set_dest_addr(iter, chan,
						DMA_CUED_XOR_BASE, addr, 0);
		}
	}

	/*  The remaining descriptors are DATACHECK. These have no need in
	 * destination. Actually, these destinations are used there
	 * as sources for check operation. So, set addr as source.
	 */
	ppc440spe_desc_set_src_addr(end, chan, 0, 0, addr ? addr : paddr);

	if (!addr) {
		end = list_entry(end->chain_node.next,
				 struct ppc440spe_adma_desc_slot, chain_node);
		ppc440spe_desc_set_src_addr(end, chan, 0, 0, qaddr);
	}
}

/**
 * ppc440spe_desc_set_xor_src_cnt - set source count into descriptor
 */
static inline void ppc440spe_desc_set_xor_src_cnt(
			struct ppc440spe_adma_desc_slot *desc,
			int src_cnt)
{
	struct xor_cb *hw_desc = desc->hw_desc;

	hw_desc->cbc &= ~XOR_CDCR_OAC_MSK;
	hw_desc->cbc |= src_cnt;
}

/**
 * ppc440spe_adma_pq_set_src - set source address into descriptor
 */
static void ppc440spe_adma_pq_set_src(struct ppc440spe_adma_desc_slot *sw_desc,
		dma_addr_t addr, int index)
{
	struct ppc440spe_adma_chan *chan;
	dma_addr_t haddr = 0;
	struct ppc440spe_adma_desc_slot *iter = NULL;

	chan = to_ppc440spe_adma_chan(sw_desc->async_tx.chan);

	switch (chan->device->id) {
	case PPC440SPE_DMA0_ID:
	case PPC440SPE_DMA1_ID:
		/* DMA0,1 may do: WXOR, RXOR, RXOR+WXORs chain
		 */
		if (test_bit(PPC440SPE_DESC_RXOR, &sw_desc->flags)) {
			/* RXOR-only or RXOR/WXOR operation */
			int iskip = test_bit(PPC440SPE_DESC_RXOR12,
				&sw_desc->flags) ?  2 : 3;

			if (index == 0) {
				/* 1st slot (RXOR) */
				/* setup sources region (R1-2-3, R1-2-4,
				 * or R1-2-5)
				 */
				if (test_bit(PPC440SPE_DESC_RXOR12,
						&sw_desc->flags))
					haddr = DMA_RXOR12 <<
						DMA_CUED_REGION_OFF;
				else if (test_bit(PPC440SPE_DESC_RXOR123,
				    &sw_desc->flags))
					haddr = DMA_RXOR123 <<
						DMA_CUED_REGION_OFF;
				else if (test_bit(PPC440SPE_DESC_RXOR124,
				    &sw_desc->flags))
					haddr = DMA_RXOR124 <<
						DMA_CUED_REGION_OFF;
				else if (test_bit(PPC440SPE_DESC_RXOR125,
				    &sw_desc->flags))
					haddr = DMA_RXOR125 <<
						DMA_CUED_REGION_OFF;
				else
					BUG();
				haddr |= DMA_CUED_XOR_BASE;
				iter = ppc440spe_get_group_entry(sw_desc, 0);
			} else if (index < iskip) {
				/* 1st slot (RXOR)
				 * shall actually set source address only once
				 * instead of first <iskip>
				 */
				iter = NULL;
			} else {
				/* 2nd/3d and next slots (WXOR);
				 * skip first slot with RXOR
				 */
				haddr = DMA_CUED_XOR_HB;
				iter = ppc440spe_get_group_entry(sw_desc,
				    index - iskip + sw_desc->dst_cnt);
			}
		} else {
			int znum = 0;

			/* WXOR-only operation; skip first slots with
			 * zeroing destinations
			 */
			if (test_bit(PPC440SPE_ZERO_P, &sw_desc->flags))
				znum++;
			if (test_bit(PPC440SPE_ZERO_Q, &sw_desc->flags))
				znum++;

			haddr = DMA_CUED_XOR_HB;
			iter = ppc440spe_get_group_entry(sw_desc,
					index + znum);
		}

		if (likely(iter)) {
			ppc440spe_desc_set_src_addr(iter, chan, 0, haddr, addr);

			if (!index &&
			    test_bit(PPC440SPE_DESC_RXOR, &sw_desc->flags) &&
			    sw_desc->dst_cnt == 2) {
				/* if we have two destinations for RXOR, then
				 * setup source in the second descr too
				 */
				iter = ppc440spe_get_group_entry(sw_desc, 1);
				ppc440spe_desc_set_src_addr(iter, chan, 0,
					haddr, addr);
			}
		}
		break;

	case PPC440SPE_XOR_ID:
		/* DMA2 may do Biskup */
		iter = sw_desc->group_head;
		if (iter->dst_cnt == 2) {
			/* both P & Q calculations required; set P src here */
			ppc440spe_adma_dma2rxor_set_src(iter, index, addr);

			/* this is for Q */
			iter = ppc440spe_get_group_entry(sw_desc,
				sw_desc->descs_per_op);
		}
		ppc440spe_adma_dma2rxor_set_src(iter, index, addr);
		break;
	}
}

/**
 * ppc440spe_adma_memcpy_xor_set_src - set source address into descriptor
 */
static void ppc440spe_adma_memcpy_xor_set_src(
		struct ppc440spe_adma_desc_slot *sw_desc,
		dma_addr_t addr, int index)
{
	struct ppc440spe_adma_chan *chan;

	chan = to_ppc440spe_adma_chan(sw_desc->async_tx.chan);
	sw_desc = sw_desc->group_head;

	if (likely(sw_desc))
		ppc440spe_desc_set_src_addr(sw_desc, chan, index, 0, addr);
}

/**
 * ppc440spe_adma_dma2rxor_inc_addr  -
 */
static void ppc440spe_adma_dma2rxor_inc_addr(
		struct ppc440spe_adma_desc_slot *desc,
		struct ppc440spe_rxor *cursor, int index, int src_cnt)
{
	cursor->addr_count++;
	if (index == src_cnt - 1) {
		ppc440spe_desc_set_xor_src_cnt(desc, cursor->addr_count);
	} else if (cursor->addr_count == XOR_MAX_OPS) {
		ppc440spe_desc_set_xor_src_cnt(desc, cursor->addr_count);
		cursor->addr_count = 0;
		cursor->desc_count++;
	}
}

/**
 * ppc440spe_adma_dma2rxor_prep_src - setup RXOR types in DMA2 CDB
 */
static int ppc440spe_adma_dma2rxor_prep_src(
		struct ppc440spe_adma_desc_slot *hdesc,
		struct ppc440spe_rxor *cursor, int index,
		int src_cnt, u32 addr)
{
	u32 sign;
	struct ppc440spe_adma_desc_slot *desc = hdesc;
	int i;

	for (i = 0; i < cursor->desc_count; i++) {
		desc = list_entry(hdesc->chain_node.next,
				  struct ppc440spe_adma_desc_slot,
				  chain_node);
	}

	switch (cursor->state) {
	case 0:
		if (addr == cursor->addrl + cursor->len) {
			/* direct RXOR */
			cursor->state = 1;
			cursor->xor_count++;
			if (index == src_cnt-1) {
				ppc440spe_rxor_set_region(desc,
					cursor->addr_count,
					DMA_RXOR12 << DMA_CUED_REGION_OFF);
				ppc440spe_adma_dma2rxor_inc_addr(
					desc, cursor, index, src_cnt);
			}
		} else if (cursor->addrl == addr + cursor->len) {
			/* reverse RXOR */
			cursor->state = 1;
			cursor->xor_count++;
			set_bit(cursor->addr_count, &desc->reverse_flags[0]);
			if (index == src_cnt-1) {
				ppc440spe_rxor_set_region(desc,
					cursor->addr_count,
					DMA_RXOR12 << DMA_CUED_REGION_OFF);
				ppc440spe_adma_dma2rxor_inc_addr(
					desc, cursor, index, src_cnt);
			}
		} else {
			printk(KERN_ERR "Cannot build "
				"DMA2 RXOR command block.\n");
			BUG();
		}
		break;
	case 1:
		sign = test_bit(cursor->addr_count,
				desc->reverse_flags)
			? -1 : 1;
		if (index == src_cnt-2 || (sign == -1
			&& addr != cursor->addrl - 2*cursor->len)) {
			cursor->state = 0;
			cursor->xor_count = 1;
			cursor->addrl = addr;
			ppc440spe_rxor_set_region(desc,
				cursor->addr_count,
				DMA_RXOR12 << DMA_CUED_REGION_OFF);
			ppc440spe_adma_dma2rxor_inc_addr(
				desc, cursor, index, src_cnt);
		} else if (addr == cursor->addrl + 2*sign*cursor->len) {
			cursor->state = 2;
			cursor->xor_count = 0;
			ppc440spe_rxor_set_region(desc,
				cursor->addr_count,
				DMA_RXOR123 << DMA_CUED_REGION_OFF);
			if (index == src_cnt-1) {
				ppc440spe_adma_dma2rxor_inc_addr(
					desc, cursor, index, src_cnt);
			}
		} else if (addr == cursor->addrl + 3*cursor->len) {
			cursor->state = 2;
			cursor->xor_count = 0;
			ppc440spe_rxor_set_region(desc,
				cursor->addr_count,
				DMA_RXOR124 << DMA_CUED_REGION_OFF);
			if (index == src_cnt-1) {
				ppc440spe_adma_dma2rxor_inc_addr(
					desc, cursor, index, src_cnt);
			}
		} else if (addr == cursor->addrl + 4*cursor->len) {
			cursor->state = 2;
			cursor->xor_count = 0;
			ppc440spe_rxor_set_region(desc,
				cursor->addr_count,
				DMA_RXOR125 << DMA_CUED_REGION_OFF);
			if (index == src_cnt-1) {
				ppc440spe_adma_dma2rxor_inc_addr(
					desc, cursor, index, src_cnt);
			}
		} else {
			cursor->state = 0;
			cursor->xor_count = 1;
			cursor->addrl = addr;
			ppc440spe_rxor_set_region(desc,
				cursor->addr_count,
				DMA_RXOR12 << DMA_CUED_REGION_OFF);
			ppc440spe_adma_dma2rxor_inc_addr(
				desc, cursor, index, src_cnt);
		}
		break;
	case 2:
		cursor->state = 0;
		cursor->addrl = addr;
		cursor->xor_count++;
		if (index) {
			ppc440spe_adma_dma2rxor_inc_addr(
				desc, cursor, index, src_cnt);
		}
		break;
	}

	return 0;
}

/**
 * ppc440spe_adma_dma2rxor_set_src - set RXOR source address; it's assumed that
 *	ppc440spe_adma_dma2rxor_prep_src() has already done prior this call
 */
static void ppc440spe_adma_dma2rxor_set_src(
		struct ppc440spe_adma_desc_slot *desc,
		int index, dma_addr_t addr)
{
	struct xor_cb *xcb = desc->hw_desc;
	int k = 0, op = 0, lop = 0;

	/* get the RXOR operand which corresponds to index addr */
	while (op <= index) {
		lop = op;
		if (k == XOR_MAX_OPS) {
			k = 0;
			desc = list_entry(desc->chain_node.next,
				struct ppc440spe_adma_desc_slot, chain_node);
			xcb = desc->hw_desc;

		}
		if ((xcb->ops[k++].h & (DMA_RXOR12 << DMA_CUED_REGION_OFF)) ==
		    (DMA_RXOR12 << DMA_CUED_REGION_OFF))
			op += 2;
		else
			op += 3;
	}

	BUG_ON(k < 1);

	if (test_bit(k-1, desc->reverse_flags)) {
		/* reverse operand order; put last op in RXOR group */
		if (index == op - 1)
			ppc440spe_rxor_set_src(desc, k - 1, addr);
	} else {
		/* direct operand order; put first op in RXOR group */
		if (index == lop)
			ppc440spe_rxor_set_src(desc, k - 1, addr);
	}
}

/**
 * ppc440spe_adma_dma2rxor_set_mult - set RXOR multipliers; it's assumed that
 *	ppc440spe_adma_dma2rxor_prep_src() has already done prior this call
 */
static void ppc440spe_adma_dma2rxor_set_mult(
		struct ppc440spe_adma_desc_slot *desc,
		int index, u8 mult)
{
	struct xor_cb *xcb = desc->hw_desc;
	int k = 0, op = 0, lop = 0;

	/* get the RXOR operand which corresponds to index mult */
	while (op <= index) {
		lop = op;
		if (k == XOR_MAX_OPS) {
			k = 0;
			desc = list_entry(desc->chain_node.next,
					  struct ppc440spe_adma_desc_slot,
					  chain_node);
			xcb = desc->hw_desc;

		}
		if ((xcb->ops[k++].h & (DMA_RXOR12 << DMA_CUED_REGION_OFF)) ==
		    (DMA_RXOR12 << DMA_CUED_REGION_OFF))
			op += 2;
		else
			op += 3;
	}

	BUG_ON(k < 1);
	if (test_bit(k-1, desc->reverse_flags)) {
		/* reverse order */
		ppc440spe_rxor_set_mult(desc, k - 1, op - index - 1, mult);
	} else {
		/* direct order */
		ppc440spe_rxor_set_mult(desc, k - 1, index - lop, mult);
	}
}

/**
 * ppc440spe_init_rxor_cursor -
 */
static void ppc440spe_init_rxor_cursor(struct ppc440spe_rxor *cursor)
{
	memset(cursor, 0, sizeof(struct ppc440spe_rxor));
	cursor->state = 2;
}

/**
 * ppc440spe_adma_pq_set_src_mult - set multiplication coefficient into
 * descriptor for the PQXOR operation
 */
static void ppc440spe_adma_pq_set_src_mult(
		struct ppc440spe_adma_desc_slot *sw_desc,
		unsigned char mult, int index, int dst_pos)
{
	struct ppc440spe_adma_chan *chan;
	u32 mult_idx, mult_dst;
	struct ppc440spe_adma_desc_slot *iter = NULL, *iter1 = NULL;

	chan = to_ppc440spe_adma_chan(sw_desc->async_tx.chan);

	switch (chan->device->id) {
	case PPC440SPE_DMA0_ID:
	case PPC440SPE_DMA1_ID:
		if (test_bit(PPC440SPE_DESC_RXOR, &sw_desc->flags)) {
			int region = test_bit(PPC440SPE_DESC_RXOR12,
					&sw_desc->flags) ? 2 : 3;

			if (index < region) {
				/* RXOR multipliers */
				iter = ppc440spe_get_group_entry(sw_desc,
					sw_desc->dst_cnt - 1);
				if (sw_desc->dst_cnt == 2)
					iter1 = ppc440spe_get_group_entry(
							sw_desc, 0);

				mult_idx = DMA_CUED_MULT1_OFF + (index << 3);
				mult_dst = DMA_CDB_SG_SRC;
			} else {
				/* WXOR multiplier */
				iter = ppc440spe_get_group_entry(sw_desc,
							index - region +
							sw_desc->dst_cnt);
				mult_idx = DMA_CUED_MULT1_OFF;
				mult_dst = dst_pos ? DMA_CDB_SG_DST2 :
						     DMA_CDB_SG_DST1;
			}
		} else {
			int znum = 0;

			/* WXOR-only;
			 * skip first slots with destinations (if ZERO_DST has
			 * place)
			 */
			if (test_bit(PPC440SPE_ZERO_P, &sw_desc->flags))
				znum++;
			if (test_bit(PPC440SPE_ZERO_Q, &sw_desc->flags))
				znum++;

			iter = ppc440spe_get_group_entry(sw_desc, index + znum);
			mult_idx = DMA_CUED_MULT1_OFF;
			mult_dst = dst_pos ? DMA_CDB_SG_DST2 : DMA_CDB_SG_DST1;
		}

		if (likely(iter)) {
			ppc440spe_desc_set_src_mult(iter, chan,
				mult_idx, mult_dst, mult);

			if (unlikely(iter1)) {
				/* if we have two destinations for RXOR, then
				 * we've just set Q mult. Set-up P now.
				 */
				ppc440spe_desc_set_src_mult(iter1, chan,
					mult_idx, mult_dst, 1);
			}

		}
		break;

	case PPC440SPE_XOR_ID:
		iter = sw_desc->group_head;
		if (sw_desc->dst_cnt == 2) {
			/* both P & Q calculations required; set P mult here */
			ppc440spe_adma_dma2rxor_set_mult(iter, index, 1);

			/* and then set Q mult */
			iter = ppc440spe_get_group_entry(sw_desc,
			       sw_desc->descs_per_op);
		}
		ppc440spe_adma_dma2rxor_set_mult(iter, index, mult);
		break;
	}
}

/**
 * ppc440spe_adma_free_chan_resources - free the resources allocated
 */
static void ppc440spe_adma_free_chan_resources(struct dma_chan *chan)
{
	struct ppc440spe_adma_chan *ppc440spe_chan;
	struct ppc440spe_adma_desc_slot *iter, *_iter;
	int in_use_descs = 0;

	ppc440spe_chan = to_ppc440spe_adma_chan(chan);
	ppc440spe_adma_slot_cleanup(ppc440spe_chan);

	spin_lock_bh(&ppc440spe_chan->lock);
	list_for_each_entry_safe(iter, _iter, &ppc440spe_chan->chain,
					chain_node) {
		in_use_descs++;
		list_del(&iter->chain_node);
	}
	list_for_each_entry_safe_reverse(iter, _iter,
			&ppc440spe_chan->all_slots, slot_node) {
		list_del(&iter->slot_node);
		kfree(iter);
		ppc440spe_chan->slots_allocated--;
	}
	ppc440spe_chan->last_used = NULL;

	dev_dbg(ppc440spe_chan->device->common.dev,
		"ppc440spe adma%d %s slots_allocated %d\n",
		ppc440spe_chan->device->id,
		__func__, ppc440spe_chan->slots_allocated);
	spin_unlock_bh(&ppc440spe_chan->lock);

	/* one is ok since we left it on there on purpose */
	if (in_use_descs > 1)
		printk(KERN_ERR "SPE: Freeing %d in use descriptors!\n",
			in_use_descs - 1);
}

/**
 * ppc440spe_adma_tx_status - poll the status of an ADMA transaction
 * @chan: ADMA channel handle
 * @cookie: ADMA transaction identifier
 * @txstate: a holder for the current state of the channel
 */
static enum dma_status ppc440spe_adma_tx_status(struct dma_chan *chan,
			dma_cookie_t cookie, struct dma_tx_state *txstate)
{
	struct ppc440spe_adma_chan *ppc440spe_chan;
	enum dma_status ret;

	ppc440spe_chan = to_ppc440spe_adma_chan(chan);
	ret = dma_cookie_status(chan, cookie, txstate);
	if (ret == DMA_COMPLETE)
		return ret;

	ppc440spe_adma_slot_cleanup(ppc440spe_chan);

	return dma_cookie_status(chan, cookie, txstate);
}

/**
 * ppc440spe_adma_eot_handler - end of transfer interrupt handler
 */
static irqreturn_t ppc440spe_adma_eot_handler(int irq, void *data)
{
	struct ppc440spe_adma_chan *chan = data;

	dev_dbg(chan->device->common.dev,
		"ppc440spe adma%d: %s\n", chan->device->id, __func__);

	tasklet_schedule(&chan->irq_tasklet);
	ppc440spe_adma_device_clear_eot_status(chan);

	return IRQ_HANDLED;
}

/**
 * ppc440spe_adma_err_handler - DMA error interrupt handler;
 *	do the same things as a eot handler
 */
static irqreturn_t ppc440spe_adma_err_handler(int irq, void *data)
{
	struct ppc440spe_adma_chan *chan = data;

	dev_dbg(chan->device->common.dev,
		"ppc440spe adma%d: %s\n", chan->device->id, __func__);

	tasklet_schedule(&chan->irq_tasklet);
	ppc440spe_adma_device_clear_eot_status(chan);

	return IRQ_HANDLED;
}

/**
 * ppc440spe_test_callback - called when test operation has been done
 */
static void ppc440spe_test_callback(void *unused)
{
	complete(&ppc440spe_r6_test_comp);
}

/**
 * ppc440spe_adma_issue_pending - flush all pending descriptors to h/w
 */
static void ppc440spe_adma_issue_pending(struct dma_chan *chan)
{
	struct ppc440spe_adma_chan *ppc440spe_chan;

	ppc440spe_chan = to_ppc440spe_adma_chan(chan);
	dev_dbg(ppc440spe_chan->device->common.dev,
		"ppc440spe adma%d: %s %d \n", ppc440spe_chan->device->id,
		__func__, ppc440spe_chan->pending);

	if (ppc440spe_chan->pending) {
		ppc440spe_chan->pending = 0;
		ppc440spe_chan_append(ppc440spe_chan);
	}
}

/**
 * ppc440spe_chan_start_null_xor - initiate the first XOR operation (DMA engines
 *	use FIFOs (as opposite to chains used in XOR) so this is a XOR
 *	specific operation)
 */
static void ppc440spe_chan_start_null_xor(struct ppc440spe_adma_chan *chan)
{
	struct ppc440spe_adma_desc_slot *sw_desc, *group_start;
	dma_cookie_t cookie;
	int slot_cnt, slots_per_op;

	dev_dbg(chan->device->common.dev,
		"ppc440spe adma%d: %s\n", chan->device->id, __func__);

	spin_lock_bh(&chan->lock);
	slot_cnt = ppc440spe_chan_xor_slot_count(0, 2, &slots_per_op);
	sw_desc = ppc440spe_adma_alloc_slots(chan, slot_cnt, slots_per_op);
	if (sw_desc) {
		group_start = sw_desc->group_head;
		list_splice_init(&sw_desc->group_list, &chan->chain);
		async_tx_ack(&sw_desc->async_tx);
		ppc440spe_desc_init_null_xor(group_start);

		cookie = dma_cookie_assign(&sw_desc->async_tx);

		/* initialize the completed cookie to be less than
		 * the most recently used cookie
		 */
		chan->common.completed_cookie = cookie - 1;

		/* channel should not be busy */
		BUG_ON(ppc440spe_chan_is_busy(chan));

		/* set the descriptor address */
		ppc440spe_chan_set_first_xor_descriptor(chan, sw_desc);

		/* run the descriptor */
		ppc440spe_chan_run(chan);
	} else
		printk(KERN_ERR "ppc440spe adma%d"
			" failed to allocate null descriptor\n",
			chan->device->id);
	spin_unlock_bh(&chan->lock);
}

/**
 * ppc440spe_test_raid6 - test are RAID-6 capabilities enabled successfully.
 *	For this we just perform one WXOR operation with the same source
 *	and destination addresses, the GF-multiplier is 1; so if RAID-6
 *	capabilities are enabled then we'll get src/dst filled with zero.
 */
static int ppc440spe_test_raid6(struct ppc440spe_adma_chan *chan)
{
	struct ppc440spe_adma_desc_slot *sw_desc, *iter;
	struct page *pg;
	char *a;
	dma_addr_t dma_addr, addrs[2];
	unsigned long op = 0;
	int rval = 0;

	set_bit(PPC440SPE_DESC_WXOR, &op);

	pg = alloc_page(GFP_KERNEL);
	if (!pg)
		return -ENOMEM;

	spin_lock_bh(&chan->lock);
	sw_desc = ppc440spe_adma_alloc_slots(chan, 1, 1);
	if (sw_desc) {
		/* 1 src, 1 dsr, int_ena, WXOR */
		ppc440spe_desc_init_dma01pq(sw_desc, 1, 1, 1, op);
		list_for_each_entry(iter, &sw_desc->group_list, chain_node) {
			ppc440spe_desc_set_byte_count(iter, chan, PAGE_SIZE);
			iter->unmap_len = PAGE_SIZE;
		}
	} else {
		rval = -EFAULT;
		spin_unlock_bh(&chan->lock);
		goto exit;
	}
	spin_unlock_bh(&chan->lock);

	/* Fill the test page with ones */
	memset(page_address(pg), 0xFF, PAGE_SIZE);
	dma_addr = dma_map_page(chan->device->dev, pg, 0,
				PAGE_SIZE, DMA_BIDIRECTIONAL);

	/* Setup addresses */
	ppc440spe_adma_pq_set_src(sw_desc, dma_addr, 0);
	ppc440spe_adma_pq_set_src_mult(sw_desc, 1, 0, 0);
	addrs[0] = dma_addr;
	addrs[1] = 0;
	ppc440spe_adma_pq_set_dest(sw_desc, addrs, DMA_PREP_PQ_DISABLE_Q);

	async_tx_ack(&sw_desc->async_tx);
	sw_desc->async_tx.callback = ppc440spe_test_callback;
	sw_desc->async_tx.callback_param = NULL;

	init_completion(&ppc440spe_r6_test_comp);

	ppc440spe_adma_tx_submit(&sw_desc->async_tx);
	ppc440spe_adma_issue_pending(&chan->common);

	wait_for_completion(&ppc440spe_r6_test_comp);

	/* Now check if the test page is zeroed */
	a = page_address(pg);
	if ((*(u32 *)a) == 0 && memcmp(a, a+4, PAGE_SIZE-4) == 0) {
		/* page is zero - RAID-6 enabled */
		rval = 0;
	} else {
		/* RAID-6 was not enabled */
		rval = -EINVAL;
	}
exit:
	__free_page(pg);
	return rval;
}

static void ppc440spe_adma_init_capabilities(struct ppc440spe_adma_device *adev)
{
	switch (adev->id) {
	case PPC440SPE_DMA0_ID:
	case PPC440SPE_DMA1_ID:
		dma_cap_set(DMA_MEMCPY, adev->common.cap_mask);
		dma_cap_set(DMA_INTERRUPT, adev->common.cap_mask);
		dma_cap_set(DMA_PQ, adev->common.cap_mask);
		dma_cap_set(DMA_PQ_VAL, adev->common.cap_mask);
		dma_cap_set(DMA_XOR_VAL, adev->common.cap_mask);
		break;
	case PPC440SPE_XOR_ID:
		dma_cap_set(DMA_XOR, adev->common.cap_mask);
		dma_cap_set(DMA_PQ, adev->common.cap_mask);
		dma_cap_set(DMA_INTERRUPT, adev->common.cap_mask);
		adev->common.cap_mask = adev->common.cap_mask;
		break;
	}

	/* Set base routines */
	adev->common.device_alloc_chan_resources =
				ppc440spe_adma_alloc_chan_resources;
	adev->common.device_free_chan_resources =
				ppc440spe_adma_free_chan_resources;
	adev->common.device_tx_status = ppc440spe_adma_tx_status;
	adev->common.device_issue_pending = ppc440spe_adma_issue_pending;

	/* Set prep routines based on capability */
	if (dma_has_cap(DMA_MEMCPY, adev->common.cap_mask)) {
		adev->common.device_prep_dma_memcpy =
			ppc440spe_adma_prep_dma_memcpy;
	}
	if (dma_has_cap(DMA_XOR, adev->common.cap_mask)) {
		adev->common.max_xor = XOR_MAX_OPS;
		adev->common.device_prep_dma_xor =
			ppc440spe_adma_prep_dma_xor;
	}
	if (dma_has_cap(DMA_PQ, adev->common.cap_mask)) {
		switch (adev->id) {
		case PPC440SPE_DMA0_ID:
			dma_set_maxpq(&adev->common,
				DMA0_FIFO_SIZE / sizeof(struct dma_cdb), 0);
			break;
		case PPC440SPE_DMA1_ID:
			dma_set_maxpq(&adev->common,
				DMA1_FIFO_SIZE / sizeof(struct dma_cdb), 0);
			break;
		case PPC440SPE_XOR_ID:
			adev->common.max_pq = XOR_MAX_OPS * 3;
			break;
		}
		adev->common.device_prep_dma_pq =
			ppc440spe_adma_prep_dma_pq;
	}
	if (dma_has_cap(DMA_PQ_VAL, adev->common.cap_mask)) {
		switch (adev->id) {
		case PPC440SPE_DMA0_ID:
			adev->common.max_pq = DMA0_FIFO_SIZE /
						sizeof(struct dma_cdb);
			break;
		case PPC440SPE_DMA1_ID:
			adev->common.max_pq = DMA1_FIFO_SIZE /
						sizeof(struct dma_cdb);
			break;
		}
		adev->common.device_prep_dma_pq_val =
			ppc440spe_adma_prep_dma_pqzero_sum;
	}
	if (dma_has_cap(DMA_XOR_VAL, adev->common.cap_mask)) {
		switch (adev->id) {
		case PPC440SPE_DMA0_ID:
			adev->common.max_xor = DMA0_FIFO_SIZE /
						sizeof(struct dma_cdb);
			break;
		case PPC440SPE_DMA1_ID:
			adev->common.max_xor = DMA1_FIFO_SIZE /
						sizeof(struct dma_cdb);
			break;
		}
		adev->common.device_prep_dma_xor_val =
			ppc440spe_adma_prep_dma_xor_zero_sum;
	}
	if (dma_has_cap(DMA_INTERRUPT, adev->common.cap_mask)) {
		adev->common.device_prep_dma_interrupt =
			ppc440spe_adma_prep_dma_interrupt;
	}
	pr_info("%s: AMCC(R) PPC440SP(E) ADMA Engine: "
	  "( %s%s%s%s%s%s)\n",
	  dev_name(adev->dev),
	  dma_has_cap(DMA_PQ, adev->common.cap_mask) ? "pq " : "",
	  dma_has_cap(DMA_PQ_VAL, adev->common.cap_mask) ? "pq_val " : "",
	  dma_has_cap(DMA_XOR, adev->common.cap_mask) ? "xor " : "",
	  dma_has_cap(DMA_XOR_VAL, adev->common.cap_mask) ? "xor_val " : "",
	  dma_has_cap(DMA_MEMCPY, adev->common.cap_mask) ? "memcpy " : "",
	  dma_has_cap(DMA_INTERRUPT, adev->common.cap_mask) ? "intr " : "");
}

static int ppc440spe_adma_setup_irqs(struct ppc440spe_adma_device *adev,
				     struct ppc440spe_adma_chan *chan,
				     int *initcode)
{
	struct platform_device *ofdev;
	struct device_node *np;
	int ret;

	ofdev = container_of(adev->dev, struct platform_device, dev);
	np = ofdev->dev.of_node;
	if (adev->id != PPC440SPE_XOR_ID) {
		adev->err_irq = irq_of_parse_and_map(np, 1);
		if (!adev->err_irq) {
			dev_warn(adev->dev, "no err irq resource?\n");
			*initcode = PPC_ADMA_INIT_IRQ2;
			adev->err_irq = -ENXIO;
		} else
			atomic_inc(&ppc440spe_adma_err_irq_ref);
	} else {
		adev->err_irq = -ENXIO;
	}

	adev->irq = irq_of_parse_and_map(np, 0);
	if (!adev->irq) {
		dev_err(adev->dev, "no irq resource\n");
		*initcode = PPC_ADMA_INIT_IRQ1;
		ret = -ENXIO;
		goto err_irq_map;
	}
	dev_dbg(adev->dev, "irq %d, err irq %d\n",
		adev->irq, adev->err_irq);

	ret = request_irq(adev->irq, ppc440spe_adma_eot_handler,
			  0, dev_driver_string(adev->dev), chan);
	if (ret) {
		dev_err(adev->dev, "can't request irq %d\n",
			adev->irq);
		*initcode = PPC_ADMA_INIT_IRQ1;
		ret = -EIO;
		goto err_req1;
	}

	/* only DMA engines have a separate error IRQ
	 * so it's Ok if err_irq < 0 in XOR engine case.
	 */
	if (adev->err_irq > 0) {
		/* both DMA engines share common error IRQ */
		ret = request_irq(adev->err_irq,
				  ppc440spe_adma_err_handler,
				  IRQF_SHARED,
				  dev_driver_string(adev->dev),
				  chan);
		if (ret) {
			dev_err(adev->dev, "can't request irq %d\n",
				adev->err_irq);
			*initcode = PPC_ADMA_INIT_IRQ2;
			ret = -EIO;
			goto err_req2;
		}
	}

	if (adev->id == PPC440SPE_XOR_ID) {
		/* enable XOR engine interrupts */
		iowrite32be(XOR_IE_CBCIE_BIT | XOR_IE_ICBIE_BIT |
			    XOR_IE_ICIE_BIT | XOR_IE_RPTIE_BIT,
			    &adev->xor_reg->ier);
	} else {
		u32 mask, enable;

		np = of_find_compatible_node(NULL, NULL, "ibm,i2o-440spe");
		if (!np) {
			pr_err("%s: can't find I2O device tree node\n",
				__func__);
			ret = -ENODEV;
			goto err_req2;
		}
		adev->i2o_reg = of_iomap(np, 0);
		if (!adev->i2o_reg) {
			pr_err("%s: failed to map I2O registers\n", __func__);
			of_node_put(np);
			ret = -EINVAL;
			goto err_req2;
		}
		of_node_put(np);
		/* Unmask 'CS FIFO Attention' interrupts and
		 * enable generating interrupts on errors
		 */
		enable = (adev->id == PPC440SPE_DMA0_ID) ?
			 ~(I2O_IOPIM_P0SNE | I2O_IOPIM_P0EM) :
			 ~(I2O_IOPIM_P1SNE | I2O_IOPIM_P1EM);
		mask = ioread32(&adev->i2o_reg->iopim) & enable;
		iowrite32(mask, &adev->i2o_reg->iopim);
	}
	return 0;

err_req2:
	free_irq(adev->irq, chan);
err_req1:
	irq_dispose_mapping(adev->irq);
err_irq_map:
	if (adev->err_irq > 0) {
		if (atomic_dec_and_test(&ppc440spe_adma_err_irq_ref))
			irq_dispose_mapping(adev->err_irq);
	}
	return ret;
}

static void ppc440spe_adma_release_irqs(struct ppc440spe_adma_device *adev,
					struct ppc440spe_adma_chan *chan)
{
	u32 mask, disable;

	if (adev->id == PPC440SPE_XOR_ID) {
		/* disable XOR engine interrupts */
		mask = ioread32be(&adev->xor_reg->ier);
		mask &= ~(XOR_IE_CBCIE_BIT | XOR_IE_ICBIE_BIT |
			  XOR_IE_ICIE_BIT | XOR_IE_RPTIE_BIT);
		iowrite32be(mask, &adev->xor_reg->ier);
	} else {
		/* disable DMAx engine interrupts */
		disable = (adev->id == PPC440SPE_DMA0_ID) ?
			  (I2O_IOPIM_P0SNE | I2O_IOPIM_P0EM) :
			  (I2O_IOPIM_P1SNE | I2O_IOPIM_P1EM);
		mask = ioread32(&adev->i2o_reg->iopim) | disable;
		iowrite32(mask, &adev->i2o_reg->iopim);
	}
	free_irq(adev->irq, chan);
	irq_dispose_mapping(adev->irq);
	if (adev->err_irq > 0) {
		free_irq(adev->err_irq, chan);
		if (atomic_dec_and_test(&ppc440spe_adma_err_irq_ref)) {
			irq_dispose_mapping(adev->err_irq);
			iounmap(adev->i2o_reg);
		}
	}
}

/**
 * ppc440spe_adma_probe - probe the asynch device
 */
static int ppc440spe_adma_probe(struct platform_device *ofdev)
{
	struct device_node *np = ofdev->dev.of_node;
	struct resource res;
	struct ppc440spe_adma_device *adev;
	struct ppc440spe_adma_chan *chan;
	struct ppc_dma_chan_ref *ref, *_ref;
	int ret = 0, initcode = PPC_ADMA_INIT_OK;
	const u32 *idx;
	int len;
	void *regs;
	u32 id, pool_size;

	if (of_device_is_compatible(np, "amcc,xor-accelerator")) {
		id = PPC440SPE_XOR_ID;
		/* As far as the XOR engine is concerned, it does not
		 * use FIFOs but uses linked list. So there is no dependency
		 * between pool size to allocate and the engine configuration.
		 */
		pool_size = PAGE_SIZE << 1;
	} else {
		/* it is DMA0 or DMA1 */
		idx = of_get_property(np, "cell-index", &len);
		if (!idx || (len != sizeof(u32))) {
			dev_err(&ofdev->dev, "Device node %pOF has missing "
				"or invalid cell-index property\n",
				np);
			return -EINVAL;
		}
		id = *idx;
		/* DMA0,1 engines use FIFO to maintain CDBs, so we
		 * should allocate the pool accordingly to size of this
		 * FIFO. Thus, the pool size depends on the FIFO depth:
		 * how much CDBs pointers the FIFO may contain then so
		 * much CDBs we should provide in the pool.
		 * That is
		 *   CDB size = 32B;
		 *   CDBs number = (DMA0_FIFO_SIZE >> 3);
		 *   Pool size = CDBs number * CDB size =
		 *      = (DMA0_FIFO_SIZE >> 3) << 5 = DMA0_FIFO_SIZE << 2.
		 */
		pool_size = (id == PPC440SPE_DMA0_ID) ?
			    DMA0_FIFO_SIZE : DMA1_FIFO_SIZE;
		pool_size <<= 2;
	}

	if (of_address_to_resource(np, 0, &res)) {
		dev_err(&ofdev->dev, "failed to get memory resource\n");
		initcode = PPC_ADMA_INIT_MEMRES;
		ret = -ENODEV;
		goto out;
	}

	if (!request_mem_region(res.start, resource_size(&res),
				dev_driver_string(&ofdev->dev))) {
		dev_err(&ofdev->dev, "failed to request memory region %pR\n",
			&res);
		initcode = PPC_ADMA_INIT_MEMREG;
		ret = -EBUSY;
		goto out;
	}

	/* create a device */
	adev = kzalloc(sizeof(*adev), GFP_KERNEL);
	if (!adev) {
		initcode = PPC_ADMA_INIT_ALLOC;
		ret = -ENOMEM;
		goto err_adev_alloc;
	}

	adev->id = id;
	adev->pool_size = pool_size;
	/* allocate coherent memory for hardware descriptors */
	adev->dma_desc_pool_virt = dma_alloc_coherent(&ofdev->dev,
					adev->pool_size, &adev->dma_desc_pool,
					GFP_KERNEL);
	if (adev->dma_desc_pool_virt == NULL) {
		dev_err(&ofdev->dev, "failed to allocate %d bytes of coherent "
			"memory for hardware descriptors\n",
			adev->pool_size);
		initcode = PPC_ADMA_INIT_COHERENT;
		ret = -ENOMEM;
		goto err_dma_alloc;
	}
	dev_dbg(&ofdev->dev, "allocated descriptor pool virt 0x%p phys 0x%llx\n",
		adev->dma_desc_pool_virt, (u64)adev->dma_desc_pool);

	regs = ioremap(res.start, resource_size(&res));
	if (!regs) {
		dev_err(&ofdev->dev, "failed to ioremap regs!\n");
		ret = -ENOMEM;
		goto err_regs_alloc;
	}

	if (adev->id == PPC440SPE_XOR_ID) {
		adev->xor_reg = regs;
		/* Reset XOR */
		iowrite32be(XOR_CRSR_XASR_BIT, &adev->xor_reg->crsr);
		iowrite32be(XOR_CRSR_64BA_BIT, &adev->xor_reg->crrr);
	} else {
		size_t fifo_size = (adev->id == PPC440SPE_DMA0_ID) ?
				   DMA0_FIFO_SIZE : DMA1_FIFO_SIZE;
		adev->dma_reg = regs;
		/* DMAx_FIFO_SIZE is defined in bytes,
		 * <fsiz> - is defined in number of CDB pointers (8byte).
		 * DMA FIFO Length = CSlength + CPlength, where
		 * CSlength = CPlength = (fsiz + 1) * 8.
		 */
		iowrite32(DMA_FIFO_ENABLE | ((fifo_size >> 3) - 2),
			  &adev->dma_reg->fsiz);
		/* Configure DMA engine */
		iowrite32(DMA_CFG_DXEPR_HP | DMA_CFG_DFMPP_HP | DMA_CFG_FALGN,
			  &adev->dma_reg->cfg);
		/* Clear Status */
		iowrite32(~0, &adev->dma_reg->dsts);
	}

	adev->dev = &ofdev->dev;
	adev->common.dev = &ofdev->dev;
	INIT_LIST_HEAD(&adev->common.channels);
	platform_set_drvdata(ofdev, adev);

	/* create a channel */
	chan = kzalloc(sizeof(*chan), GFP_KERNEL);
	if (!chan) {
		initcode = PPC_ADMA_INIT_CHANNEL;
		ret = -ENOMEM;
		goto err_chan_alloc;
	}

	spin_lock_init(&chan->lock);
	INIT_LIST_HEAD(&chan->chain);
	INIT_LIST_HEAD(&chan->all_slots);
	chan->device = adev;
	chan->common.device = &adev->common;
	dma_cookie_init(&chan->common);
	list_add_tail(&chan->common.device_node, &adev->common.channels);
	tasklet_setup(&chan->irq_tasklet, ppc440spe_adma_tasklet);

	/* allocate and map helper pages for async validation or
	 * async_mult/async_sum_product operations on DMA0/1.
	 */
	if (adev->id != PPC440SPE_XOR_ID) {
		chan->pdest_page = alloc_page(GFP_KERNEL);
		chan->qdest_page = alloc_page(GFP_KERNEL);
		if (!chan->pdest_page ||
		    !chan->qdest_page) {
			if (chan->pdest_page)
				__free_page(chan->pdest_page);
			if (chan->qdest_page)
				__free_page(chan->qdest_page);
			ret = -ENOMEM;
			goto err_page_alloc;
		}
		chan->pdest = dma_map_page(&ofdev->dev, chan->pdest_page, 0,
					   PAGE_SIZE, DMA_BIDIRECTIONAL);
		chan->qdest = dma_map_page(&ofdev->dev, chan->qdest_page, 0,
					   PAGE_SIZE, DMA_BIDIRECTIONAL);
	}

	ref = kmalloc(sizeof(*ref), GFP_KERNEL);
	if (ref) {
		ref->chan = &chan->common;
		INIT_LIST_HEAD(&ref->node);
		list_add_tail(&ref->node, &ppc440spe_adma_chan_list);
	} else {
		dev_err(&ofdev->dev, "failed to allocate channel reference!\n");
		ret = -ENOMEM;
		goto err_ref_alloc;
	}

	ret = ppc440spe_adma_setup_irqs(adev, chan, &initcode);
	if (ret)
		goto err_irq;

	ppc440spe_adma_init_capabilities(adev);

	ret = dma_async_device_register(&adev->common);
	if (ret) {
		initcode = PPC_ADMA_INIT_REGISTER;
		dev_err(&ofdev->dev, "failed to register dma device\n");
		goto err_dev_reg;
	}

	goto out;

err_dev_reg:
	ppc440spe_adma_release_irqs(adev, chan);
err_irq:
	list_for_each_entry_safe(ref, _ref, &ppc440spe_adma_chan_list, node) {
		if (chan == to_ppc440spe_adma_chan(ref->chan)) {
			list_del(&ref->node);
			kfree(ref);
		}
	}
err_ref_alloc:
	if (adev->id != PPC440SPE_XOR_ID) {
		dma_unmap_page(&ofdev->dev, chan->pdest,
			       PAGE_SIZE, DMA_BIDIRECTIONAL);
		dma_unmap_page(&ofdev->dev, chan->qdest,
			       PAGE_SIZE, DMA_BIDIRECTIONAL);
		__free_page(chan->pdest_page);
		__free_page(chan->qdest_page);
	}
err_page_alloc:
	kfree(chan);
err_chan_alloc:
	if (adev->id == PPC440SPE_XOR_ID)
		iounmap(adev->xor_reg);
	else
		iounmap(adev->dma_reg);
err_regs_alloc:
	dma_free_coherent(adev->dev, adev->pool_size,
			  adev->dma_desc_pool_virt,
			  adev->dma_desc_pool);
err_dma_alloc:
	kfree(adev);
err_adev_alloc:
	release_mem_region(res.start, resource_size(&res));
out:
	if (id < PPC440SPE_ADMA_ENGINES_NUM)
		ppc440spe_adma_devices[id] = initcode;

	return ret;
}

/**
 * ppc440spe_adma_remove - remove the asynch device
 */
static int ppc440spe_adma_remove(struct platform_device *ofdev)
{
	struct ppc440spe_adma_device *adev = platform_get_drvdata(ofdev);
	struct device_node *np = ofdev->dev.of_node;
	struct resource res;
	struct dma_chan *chan, *_chan;
	struct ppc_dma_chan_ref *ref, *_ref;
	struct ppc440spe_adma_chan *ppc440spe_chan;

	if (adev->id < PPC440SPE_ADMA_ENGINES_NUM)
		ppc440spe_adma_devices[adev->id] = -1;

	dma_async_device_unregister(&adev->common);

	list_for_each_entry_safe(chan, _chan, &adev->common.channels,
				 device_node) {
		ppc440spe_chan = to_ppc440spe_adma_chan(chan);
		ppc440spe_adma_release_irqs(adev, ppc440spe_chan);
		tasklet_kill(&ppc440spe_chan->irq_tasklet);
		if (adev->id != PPC440SPE_XOR_ID) {
			dma_unmap_page(&ofdev->dev, ppc440spe_chan->pdest,
					PAGE_SIZE, DMA_BIDIRECTIONAL);
			dma_unmap_page(&ofdev->dev, ppc440spe_chan->qdest,
					PAGE_SIZE, DMA_BIDIRECTIONAL);
			__free_page(ppc440spe_chan->pdest_page);
			__free_page(ppc440spe_chan->qdest_page);
		}
		list_for_each_entry_safe(ref, _ref, &ppc440spe_adma_chan_list,
					 node) {
			if (ppc440spe_chan ==
			    to_ppc440spe_adma_chan(ref->chan)) {
				list_del(&ref->node);
				kfree(ref);
			}
		}
		list_del(&chan->device_node);
		kfree(ppc440spe_chan);
	}

	dma_free_coherent(adev->dev, adev->pool_size,
			  adev->dma_desc_pool_virt, adev->dma_desc_pool);
	if (adev->id == PPC440SPE_XOR_ID)
		iounmap(adev->xor_reg);
	else
		iounmap(adev->dma_reg);
	of_address_to_resource(np, 0, &res);
	release_mem_region(res.start, resource_size(&res));
	kfree(adev);
	return 0;
}

/*
 * /sys driver interface to enable h/w RAID-6 capabilities
 * Files created in e.g. /sys/devices/plb.0/400100100.dma0/driver/
 * directory are "devices", "enable" and "poly".
 * "devices" shows available engines.
 * "enable" is used to enable RAID-6 capabilities or to check
 * whether these has been activated.
 * "poly" allows setting/checking used polynomial (for PPC440SPe only).
 */

static ssize_t devices_show(struct device_driver *dev, char *buf)
{
	ssize_t size = 0;
	int i;

	for (i = 0; i < PPC440SPE_ADMA_ENGINES_NUM; i++) {
		if (ppc440spe_adma_devices[i] == -1)
			continue;
		size += scnprintf(buf + size, PAGE_SIZE - size,
				 "PPC440SP(E)-ADMA.%d: %s\n", i,
				 ppc_adma_errors[ppc440spe_adma_devices[i]]);
	}
	return size;
}
static DRIVER_ATTR_RO(devices);

static ssize_t enable_show(struct device_driver *dev, char *buf)
{
	return snprintf(buf, PAGE_SIZE,
			"PPC440SP(e) RAID-6 capabilities are %sABLED.\n",
			ppc440spe_r6_enabled ? "EN" : "DIS");
}

static ssize_t enable_store(struct device_driver *dev, const char *buf,
			    size_t count)
{
	unsigned long val;
	int err;

	if (!count || count > 11)
		return -EINVAL;

	if (!ppc440spe_r6_tchan)
		return -EFAULT;

	/* Write a key */
	err = kstrtoul(buf, 16, &val);
	if (err)
		return err;

	dcr_write(ppc440spe_mq_dcr_host, DCRN_MQ0_XORBA, val);
	isync();

	/* Verify whether it really works now */
	if (ppc440spe_test_raid6(ppc440spe_r6_tchan) == 0) {
		pr_info("PPC440SP(e) RAID-6 has been activated "
			"successfully\n");
		ppc440spe_r6_enabled = 1;
	} else {
		pr_info("PPC440SP(e) RAID-6 hasn't been activated!"
			" Error key ?\n");
		ppc440spe_r6_enabled = 0;
	}
	return count;
}
static DRIVER_ATTR_RW(enable);

static ssize_t poly_show(struct device_driver *dev, char *buf)
{
	ssize_t size = 0;
	u32 reg;

#ifdef CONFIG_440SP
	/* 440SP has fixed polynomial */
	reg = 0x4d;
#else
	reg = dcr_read(ppc440spe_mq_dcr_host, DCRN_MQ0_CFBHL);
	reg >>= MQ0_CFBHL_POLY;
	reg &= 0xFF;
#endif

	size = snprintf(buf, PAGE_SIZE, "PPC440SP(e) RAID-6 driver "
			"uses 0x1%02x polynomial.\n", reg);
	return size;
}

static ssize_t poly_store(struct device_driver *dev, const char *buf,
			  size_t count)
{
	unsigned long reg, val;
	int err;
#ifdef CONFIG_440SP
	/* 440SP uses default 0x14D polynomial only */
	return -EINVAL;
#endif

	if (!count || count > 6)
		return -EINVAL;

	/* e.g., 0x14D or 0x11D */
	err = kstrtoul(buf, 16, &val);
	if (err)
		return err;

	if (val & ~0x1FF)
		return -EINVAL;

	val &= 0xFF;
	reg = dcr_read(ppc440spe_mq_dcr_host, DCRN_MQ0_CFBHL);
	reg &= ~(0xFF << MQ0_CFBHL_POLY);
	reg |= val << MQ0_CFBHL_POLY;
	dcr_write(ppc440spe_mq_dcr_host, DCRN_MQ0_CFBHL, reg);

	return count;
}
static DRIVER_ATTR_RW(poly);

/*
 * Common initialisation for RAID engines; allocate memory for
 * DMAx FIFOs, perform configuration common for all DMA engines.
 * Further DMA engine specific configuration is done at probe time.
 */
static int ppc440spe_configure_raid_devices(void)
{
	struct device_node *np;
	struct resource i2o_res;
	struct i2o_regs __iomem *i2o_reg;
	dcr_host_t i2o_dcr_host;
	unsigned int dcr_base, dcr_len;
	int i, ret;

	np = of_find_compatible_node(NULL, NULL, "ibm,i2o-440spe");
	if (!np) {
		pr_err("%s: can't find I2O device tree node\n",
			__func__);
		return -ENODEV;
	}

	if (of_address_to_resource(np, 0, &i2o_res)) {
		of_node_put(np);
		return -EINVAL;
	}

	i2o_reg = of_iomap(np, 0);
	if (!i2o_reg) {
		pr_err("%s: failed to map I2O registers\n", __func__);
		of_node_put(np);
		return -EINVAL;
	}

	/* Get I2O DCRs base */
	dcr_base = dcr_resource_start(np, 0);
	dcr_len = dcr_resource_len(np, 0);
	if (!dcr_base && !dcr_len) {
		pr_err("%pOF: can't get DCR registers base/len!\n", np);
		of_node_put(np);
		iounmap(i2o_reg);
		return -ENODEV;
	}

	i2o_dcr_host = dcr_map(np, dcr_base, dcr_len);
	if (!DCR_MAP_OK(i2o_dcr_host)) {
		pr_err("%pOF: failed to map DCRs!\n", np);
		of_node_put(np);
		iounmap(i2o_reg);
		return -ENODEV;
	}
	of_node_put(np);

	/* Provide memory regions for DMA's FIFOs: I2O, DMA0 and DMA1 share
	 * the base address of FIFO memory space.
	 * Actually we need twice more physical memory than programmed in the
	 * <fsiz> register (because there are two FIFOs for each DMA: CP and CS)
	 */
	ppc440spe_dma_fifo_buf = kmalloc((DMA0_FIFO_SIZE + DMA1_FIFO_SIZE) << 1,
					 GFP_KERNEL);
	if (!ppc440spe_dma_fifo_buf) {
		pr_err("%s: DMA FIFO buffer allocation failed.\n", __func__);
		iounmap(i2o_reg);
		dcr_unmap(i2o_dcr_host, dcr_len);
		return -ENOMEM;
	}

	/*
	 * Configure h/w
	 */
	/* Reset I2O/DMA */
	mtdcri(SDR0, DCRN_SDR0_SRST, DCRN_SDR0_SRST_I2ODMA);
	mtdcri(SDR0, DCRN_SDR0_SRST, 0);

	/* Setup the base address of mmaped registers */
	dcr_write(i2o_dcr_host, DCRN_I2O0_IBAH, (u32)(i2o_res.start >> 32));
	dcr_write(i2o_dcr_host, DCRN_I2O0_IBAL, (u32)(i2o_res.start) |
						I2O_REG_ENABLE);
	dcr_unmap(i2o_dcr_host, dcr_len);

	/* Setup FIFO memory space base address */
	iowrite32(0, &i2o_reg->ifbah);
	iowrite32(((u32)__pa(ppc440spe_dma_fifo_buf)), &i2o_reg->ifbal);

	/* set zero FIFO size for I2O, so the whole
	 * ppc440spe_dma_fifo_buf is used by DMAs.
	 * DMAx_FIFOs will be configured while probe.
	 */
	iowrite32(0, &i2o_reg->ifsiz);
	iounmap(i2o_reg);

	/* To prepare WXOR/RXOR functionality we need access to
	 * Memory Queue Module DCRs (finally it will be enabled
	 * via /sys interface of the ppc440spe ADMA driver).
	 */
	np = of_find_compatible_node(NULL, NULL, "ibm,mq-440spe");
	if (!np) {
		pr_err("%s: can't find MQ device tree node\n",
			__func__);
		ret = -ENODEV;
		goto out_free;
	}

	/* Get MQ DCRs base */
	dcr_base = dcr_resource_start(np, 0);
	dcr_len = dcr_resource_len(np, 0);
	if (!dcr_base && !dcr_len) {
		pr_err("%pOF: can't get DCR registers base/len!\n", np);
		ret = -ENODEV;
		goto out_mq;
	}

	ppc440spe_mq_dcr_host = dcr_map(np, dcr_base, dcr_len);
	if (!DCR_MAP_OK(ppc440spe_mq_dcr_host)) {
		pr_err("%pOF: failed to map DCRs!\n", np);
		ret = -ENODEV;
		goto out_mq;
	}
	of_node_put(np);
	ppc440spe_mq_dcr_len = dcr_len;

	/* Set HB alias */
	dcr_write(ppc440spe_mq_dcr_host, DCRN_MQ0_BAUH, DMA_CUED_XOR_HB);

	/* Set:
	 * - LL transaction passing limit to 1;
	 * - Memory controller cycle limit to 1;
	 * - Galois Polynomial to 0x14d (default)
	 */
	dcr_write(ppc440spe_mq_dcr_host, DCRN_MQ0_CFBHL,
		  (1 << MQ0_CFBHL_TPLM) | (1 << MQ0_CFBHL_HBCL) |
		  (PPC440SPE_DEFAULT_POLY << MQ0_CFBHL_POLY));

	atomic_set(&ppc440spe_adma_err_irq_ref, 0);
	for (i = 0; i < PPC440SPE_ADMA_ENGINES_NUM; i++)
		ppc440spe_adma_devices[i] = -1;

	return 0;

out_mq:
	of_node_put(np);
out_free:
	kfree(ppc440spe_dma_fifo_buf);
	return ret;
}

static const struct of_device_id ppc440spe_adma_of_match[] = {
	{ .compatible	= "ibm,dma-440spe", },
	{ .compatible	= "amcc,xor-accelerator", },
	{},
};
MODULE_DEVICE_TABLE(of, ppc440spe_adma_of_match);

static struct platform_driver ppc440spe_adma_driver = {
	.probe = ppc440spe_adma_probe,
	.remove = ppc440spe_adma_remove,
	.driver = {
		.name = "PPC440SP(E)-ADMA",
		.of_match_table = ppc440spe_adma_of_match,
	},
};

static __init int ppc440spe_adma_init(void)
{
	int ret;

	ret = ppc440spe_configure_raid_devices();
	if (ret)
		return ret;

	ret = platform_driver_register(&ppc440spe_adma_driver);
	if (ret) {
		pr_err("%s: failed to register platform driver\n",
			__func__);
		goto out_reg;
	}

	/* Initialization status */
	ret = driver_create_file(&ppc440spe_adma_driver.driver,
				 &driver_attr_devices);
	if (ret)
		goto out_dev;

	/* RAID-6 h/w enable entry */
	ret = driver_create_file(&ppc440spe_adma_driver.driver,
				 &driver_attr_enable);
	if (ret)
		goto out_en;

	/* GF polynomial to use */
	ret = driver_create_file(&ppc440spe_adma_driver.driver,
				 &driver_attr_poly);
	if (!ret)
		return ret;

	driver_remove_file(&ppc440spe_adma_driver.driver,
			   &driver_attr_enable);
out_en:
	driver_remove_file(&ppc440spe_adma_driver.driver,
			   &driver_attr_devices);
out_dev:
	/* User will not be able to enable h/w RAID-6 */
	pr_err("%s: failed to create RAID-6 driver interface\n",
		__func__);
	platform_driver_unregister(&ppc440spe_adma_driver);
out_reg:
	dcr_unmap(ppc440spe_mq_dcr_host, ppc440spe_mq_dcr_len);
	kfree(ppc440spe_dma_fifo_buf);
	return ret;
}

static void __exit ppc440spe_adma_exit(void)
{
	driver_remove_file(&ppc440spe_adma_driver.driver,
			   &driver_attr_poly);
	driver_remove_file(&ppc440spe_adma_driver.driver,
			   &driver_attr_enable);
	driver_remove_file(&ppc440spe_adma_driver.driver,
			   &driver_attr_devices);
	platform_driver_unregister(&ppc440spe_adma_driver);
	dcr_unmap(ppc440spe_mq_dcr_host, ppc440spe_mq_dcr_len);
	kfree(ppc440spe_dma_fifo_buf);
}

arch_initcall(ppc440spe_adma_init);
module_exit(ppc440spe_adma_exit);

MODULE_AUTHOR("Yuri Tikhonov <yur@emcraft.com>");
MODULE_DESCRIPTION("PPC440SPE ADMA Engine Driver");
MODULE_LICENSE("GPL");
