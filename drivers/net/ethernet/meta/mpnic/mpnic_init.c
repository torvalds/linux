// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/cache.h>
#include <linux/if_ether.h>
#include <linux/iopoll.h>
#include <linux/log2.h>
#include <linux/sizes.h>

#include "mpnic.h"
#include "mpnic_txrx.h"

#define MPNIC_MEM_INIT_POLL_US		500
#define MPNIC_MEM_INIT_TO_US		5000

/* BDQ mem init:
 *  bit 1: fifo_wrptr_mem
 *  bit 0: fifo_rdptr_mem
 */
#define MPNIC_MEM_INIT_BDQ_VAL		0x3

/* RCM mem init:
 *  bit 3: cq_base_addr
 *  bit 2: cd_fifo_rptr_stats
 *  bit 1: cd_fifo_wptr_stats
 *  bit 0: cq_head_ptr_stats
 */
#define MPNIC_MEM_INIT_RCM_VAL		0xf

/* RDE mem init, bits 0-18. Bits 0-12 are the per-queue packet, error and
 * drop counters, bits 13-18 the two context memories of each of the HPQ,
 * PPQ and SPQ descriptor prefetchers.
 */
#define MPNIC_MEM_INIT_RDE_VAL		0x7ffff

/* RPC mem init, bit 0 covers the whole classifier. */
#define MPNIC_MEM_INIT_RPC_VAL		0x1

/* TCM mem init:
 *  bit 3: cq_head_ptr_stats
 *  bit 2: cd_fifo_wptr_stats
 *  bit 1: cd_fifo_rptr_stats
 *  bit 0: cq_base_addr
 */
#define MPNIC_MEM_INIT_TCM_VAL		0xf

/* TDE mem init:
 *  bit 1: stats mem
 *  bit 0: dma_head_ptr SRAM
 */
#define MPNIC_MEM_INIT_TDE_VAL		0x3

/* TDF mem init, bit 0 covers the descriptor fetch SRAM. */
#define MPNIC_MEM_INIT_TDF_VAL		0x1

/* RXB mem init, bit 0 covers the DMAC TCAM statistics RAM. */
#define MPNIC_MEM_INIT_RXB_VAL		0x1

/* TQS arbiter SRAM init done, one bit per DWRR level. */
#define MPNIC_TQS_ARB_INIT_VAL		0xf

/* On-chip SRAM allocated to each queue for descriptor fetch, in units of
 * descriptors. Valid values are 64, 128, 256, 512 and 1024.
 *
 * The partition sizes are chosen for the maximum number of queues the
 * device supports, so they do not have to be adjusted when the active
 * queue count changes:
 *
 *   BDQ: 1 MiB / (8 B/DESC) / (1024 HPQ + 1024 PPQ) =  64 DESC/QUEUE
 *   TWQ: 2 MiB / (8 B/DESC) / (1024 TXQ * 2 TWQ)    = 128 DESC/QUEUE
 */
#define MPNIC_BDQ_SRAM_DESCS		64u
#define MPNIC_TDF_SRAM_DESCS		128u

/* A total of 1 MiB worth of Tx credits is available, in units of 128 B.
 * The BMC to MAC and host to BMC paths each get a guaranteed share of them
 * whether or not anything is routed their way, everything else goes to
 * MAC TC0.
 */
#define MPNIC_TXB_BMC_PVT_CRDT_INIT_VAL		800
#define MPNIC_TXB_P0_MAC_PVT_CRDT_INIT_VAL	\
	(SZ_1M / 128 - 2 * MPNIC_TXB_BMC_PVT_CRDT_INIT_VAL)

/* The recommended lower bound for the TXB threshold is 80, based on a 10K
 * MTU. Round up by 25% to stay on the defensive side. The same reasoning
 * applies to the arbitration weight, which has to exceed the full packet
 * size.
 */
#define MPNIC_TXB_INIT_BMC_THRESH		100
#define MPNIC_TXB_INIT_P0_THRESH		100
#define MPNIC_TXB_INIT_P0_ARB_WEIGHTS		0x64

/* RXB host drop threshold in units of 128 B beats. Packets targeting a
 * queue are dropped when the available credits fall below it. 80 beats is
 * 10 KB, which is also the largest frame the device is configured for.
 */
#define MPNIC_RXB_INIT_HOST_DROP_THRESH		0x50

/* A total of 8 MiB of Rx buffer is available. The recommended per-TC pool
 * for a 800G configuration is 420 KB with all 8 TCs enabled. Only one TC
 * is in use, so give it 8 * 420 KB (in units of 128 B) and push the rest
 * to the common pool. Start drawing from the common pool as soon as the
 * TC0 credits fall below one max sized frame. The BMC keeps a small
 * reserve of its own whether or not the host talks to it.
 */
#define MPNIC_RXB_INIT_POOL_TC_CRDTS_P0		(0xd20 * 8)
#define MPNIC_RXB_INIT_BMC_CRDTS		0x20
#define MPNIC_RXB_INIT_COMMON_CRDT_MAX_THRSH	\
	(SZ_8M / 128 - MPNIC_RXB_INIT_POOL_TC_CRDTS_P0 - \
	 MPNIC_RXB_INIT_BMC_CRDTS)
#define MPNIC_RXB_INIT_COMMON_CRDT_THRSH	0x50

/* TXB port mode selects the number of active MAC ports for Tx buffer
 * credit distribution and arbitration. The hardware only supports single,
 * dual and quad port, encoded as b'001, b'010 and b'100.
 */
#define MPNIC_TXB_PORT_MODE_SINGLE	1

/* The MAC traffic classes start at index 8 in the TXB arrays, the BMC
 * sits above them.
 */
#define MPNIC_TXB_TC_IDX_MAC_0		8
#define MPNIC_TXB_TC_IDX_BMC		16

/* Largest frame the Tx queue scheduler will pass through. Anything above
 * it gets truncated.
 */
#define MPNIC_TQS_MTU_CTL0_MAX		0x2800

/* The unit of the DWRR quantum is 256 B. It has to be large enough for at
 * least 11 MTUs to be transmitted in one quantum; use 15 for headroom.
 */
#define MPNIC_TQS_DWRR_INIT_QUANTUM	(15 * MPNIC_TQS_MTU_CTL0_MAX / 256)

/* Lower bound on the credit available to a queue, group, set or port
 * before the scheduler stops issuing requests for it. 15 MTUs, matching
 * the DWRR quantum above.
 */
#define MPNIC_TQS_DWRR_CEV_MIN_SCHED_THRESH	(15 * MPNIC_TQS_MTU_CTL0_MAX)

/* The TQS view of the TXB partitioning between the MAC and the BMC, in
 * units of 1 KiB. It has to match the TXB private credits above.
 */
#define MPNIC_TQS_GLBL_TXB_CRDT_UNIT	SZ_1K
#define MPNIC_TQS_GLBL_TXB_CRDT_BMC	\
	(MPNIC_TXB_BMC_PVT_CRDT_INIT_VAL * 128 / MPNIC_TQS_GLBL_TXB_CRDT_UNIT)
#define MPNIC_TQS_GLBL_TXB_CRDT_MAC	\
	(MPNIC_TXB_P0_MAC_PVT_CRDT_INIT_VAL * 128 / \
	 MPNIC_TQS_GLBL_TXB_CRDT_UNIT)

struct mpnic_init_poll {
	u64 exp_val;
	u32 addr;
};

struct mpnic_poll_state {
	int poll_idx;
	u64 val;
};

static void mpnic_tdf_glbl_init(struct mpnic_dev *mpd)
{
	/* Default metadata descriptor, used for frames the driver did not
	 * prepend one to.
	 */
	mpnic_wr64(mpd, MPNIC_TWQ_DEF_PRI_TWD, MPNIC_TWD_FLAG_REQ_COMPLETION);
}

static void mpnic_txb_init(struct mpnic_dev *mpd)
{
	int i;

	mpnic_wr64(mpd, MPNIC_TXB_BMC, MPNIC_TXB_BMC_PVT_CRDT_INIT_VAL);

	/* Zero the private credits of every traffic class, then hand the
	 * unreserved ones to MAC TC0.
	 */
	for (i = 0; i < MPNIC_TXB_P0_CNT; i++)
		mpnic_wr64(mpd, MPNIC_TXB_P0(i), 0);
	mpnic_wr64(mpd, MPNIC_TXB_P0(MPNIC_TXB_TC_IDX_MAC_0),
		   MPNIC_TXB_P0_MAC_PVT_CRDT_INIT_VAL);
	mpnic_wr64(mpd, MPNIC_TXB_P0(MPNIC_TXB_TC_IDX_BMC),
		   MPNIC_TXB_BMC_PVT_CRDT_INIT_VAL);

	mpnic_wr64(mpd, MPNIC_TXB_P0_THRESH(MPNIC_TXB_TC_IDX_BMC),
		   MPNIC_TXB_INIT_BMC_THRESH);

	mpnic_wr64(mpd, MPNIC_TXB_P0_THRESH(MPNIC_TXB_TC_IDX_MAC_0),
		   MPNIC_TXB_INIT_P0_THRESH);
	mpnic_wr64(mpd, MPNIC_TXB_P0_ARB_WEIGHTS(MPNIC_TXB_TC_IDX_MAC_0),
		   MPNIC_TXB_INIT_P0_ARB_WEIGHTS);
	mpnic_wr64(mpd, MPNIC_TXB_PORT_CONFIG,
		   FIELD_PREP(MPNIC_TXB_PORT_CONFIG_PORT_MODE,
			      MPNIC_TXB_PORT_MODE_SINGLE));
}

static void mpnic_rxb_init(struct mpnic_dev *mpd)
{
	/* Accept all packets that miss dmac tcam, until l2 filtering is
	 * implemented.
	 */
	mpnic_wr64(mpd, MPNIC_RXB_PORT_CLASS_CFG(0),
		   FIELD_PREP(MPNIC_RXB_PORT_CLASS_CFG_DEFAULT_L2_ACTION,
			      MPNIC_L2_ACTION_PASS));
	mpnic_wr64(mpd, MPNIC_RXB_PORT_CFG(0),
		   FIELD_PREP(MPNIC_RXB_PORT_CFG_FCS_STRIP_MODE,
			      MPNIC_FCS_MODE_STRIP));

	mpnic_wr64(mpd, MPNIC_RXB_HOST_DROP_THRESH(0),
		   MPNIC_RXB_INIT_HOST_DROP_THRESH);

	mpnic_wr64(mpd, MPNIC_RXB_TC_CRDTS(0),
		   MPNIC_RXB_INIT_POOL_TC_CRDTS_P0);
	mpnic_wr64(mpd, MPNIC_RXB_COMMON_CRDT_CTRL_TC(0),
		   FIELD_PREP(MPNIC_RXB_COMMON_CRDT_CTRL_TC_THRESH,
			      MPNIC_RXB_INIT_COMMON_CRDT_THRSH) |
		   FIELD_PREP(MPNIC_RXB_COMMON_CRDT_CTRL_TC_MAX_CRDTS,
			      MPNIC_RXB_INIT_COMMON_CRDT_MAX_THRSH) |
		   MPNIC_RXB_COMMON_CRDT_CTRL_TC_TC_EN);

	/* Only pool 0 is used, it gets all of the common credits */
	mpnic_wr64(mpd, MPNIC_RXB_POOL_COMMON_CRDTS(0),
		   MPNIC_RXB_INIT_COMMON_CRDT_MAX_THRSH);

	mpnic_wr64(mpd, MPNIC_RXB_BMC_CRDTS(0), MPNIC_RXB_INIT_BMC_CRDTS);

	mpnic_wr64(mpd, MPNIC_RXB_MEM_INIT_REQ, MPNIC_MEM_INIT_RXB_VAL);
}

static u64 mpnic_desc_cfg(unsigned int sram_descs, unsigned int q_idx)
{
	/* The hardware encodes the partition size as 64 * 2^n descriptors,
	 * and its start address in units of 64 descriptors.
	 */
	return FIELD_PREP(MPNIC_DESC_CFG_NUM_DESCS, __ffs(sram_descs) - 6) |
	       FIELD_PREP(MPNIC_DESC_CFG_START_ADDR,
			  q_idx * (sram_descs >> 6));
}

static void mpnic_desc_sram_init(struct mpnic_dev *mpd)
{
	int i;

	for (i = 0; i < MPNIC_MAX_TXQS * 2; i++)
		mpnic_wr64(mpd, MPNIC_TDF_DESC_CFG(i),
			   mpnic_desc_cfg(MPNIC_TDF_SRAM_DESCS, i));

	for (i = 0; i < MPNIC_MAX_RXQS; i++) {
		mpnic_wr64(mpd, MPNIC_HPQ_DESC_CFG(i),
			   mpnic_desc_cfg(MPNIC_BDQ_SRAM_DESCS, i));
		mpnic_wr64(mpd, MPNIC_PPQ_DESC_CFG(i),
			   mpnic_desc_cfg(MPNIC_BDQ_SRAM_DESCS,
					  MPNIC_MAX_RXQS + i));
	}
}

static void mpnic_rxglb_init(struct mpnic_dev *mpd)
{
	/* Descriptor prefetch reads are only issued once 32 descriptors
	 * worth of FIFO space is available, and no more than 64 descriptors
	 * are fetched for one queue at a time so that a single queue cannot
	 * monopolize the bus. Both have to be multiples of 16 to keep the
	 * reads 128 B aligned.
	 */
	mpnic_wr64(mpd, MPNIC_BDQ_GLBL_CTL0,
		   FIELD_PREP(MPNIC_BDQ_GLBL_CTL0_PREFETCH_SPACE_THRESH, 32) |
		   FIELD_PREP(MPNIC_BDQ_GLBL_CTL0_MAX_REQ_SIZE, 64));

	/* Minimum number of descriptors which has to be available before
	 * the descriptor engine considers a queue usable, globally and in
	 * the per-queue prefetch FIFO.
	 */
	mpnic_wr64(mpd, MPNIC_RDE_CTL,
		   FIELD_PREP(MPNIC_RDE_CTL_HPQ_DROP_THRESHOLD,
			      MPNIC_RX_HPQ_DROP_THRS - 1) |
		   FIELD_PREP(MPNIC_RDE_CTL_PPQ_DROP_THRESHOLD,
			      MPNIC_RX_PPQ_DROP_THRS - 1) |
		   FIELD_PREP(MPNIC_RDE_CTL_HPQ_LOCAL_DROP_THRESHOLD,
			      MPNIC_RX_HPQ_DROP_THRS - 1) |
		   FIELD_PREP(MPNIC_RDE_CTL_PPQ_LOCAL_DROP_THRESHOLD,
			      MPNIC_RX_PPQ_DROP_THRS - 1));

	/* Receive side coalescing is not supported yet */
	mpnic_wr64(mpd, MPNIC_RSC_GLOBAL_CONF,
		   MPNIC_RSC_GLOBAL_CONF_RSC_DISABLE);

	mpnic_wr64(mpd, MPNIC_BDQ_MEM_INIT_REQ, MPNIC_MEM_INIT_BDQ_VAL);
	mpnic_wr64(mpd, MPNIC_RCM_MEM_INIT_REQ, MPNIC_MEM_INIT_RCM_VAL);
	mpnic_wr64(mpd, MPNIC_RDE_MEM_INIT_REQ, MPNIC_MEM_INIT_RDE_VAL);
	mpnic_wr64(mpd, MPNIC_RPC_MEM_INIT_REQ, MPNIC_MEM_INIT_RPC_VAL);
}

static void mpnic_txglb_init(struct mpnic_dev *mpd)
{
	/* Nothing is redirected to the BMC until the Tx offload TCAM gets
	 * programmed with its addresses.
	 */
	mpnic_wr64(mpd, MPNIC_TOF_TCAM_DEST_REMAP, 0);

	mpnic_wr64(mpd, MPNIC_TCM_MEM_INIT_REQ, MPNIC_MEM_INIT_TCM_VAL);
	mpnic_wr64(mpd, MPNIC_TDE_MEM_INIT_REQ, MPNIC_MEM_INIT_TDE_VAL);
	mpnic_wr64(mpd, MPNIC_TDF_MEM_INIT_REQ, MPNIC_MEM_INIT_TDF_VAL);
}

/* Fill the DWRR arbiter memories. Setting the INIT bit makes the hardware
 * write the given credit and quantum into every entry at the queue, group,
 * set and port level, so nothing has to be programmed per queue.
 */
static void mpnic_tqs_sram_init(struct mpnic_dev *mpd)
{
	mpnic_wr64(mpd, MPNIC_TQS_GROUP_INIT_CTL,
		   FIELD_PREP(MPNIC_TQS_GROUP_INIT_CTL_QUANTUM,
			      MPNIC_TQS_DWRR_INIT_QUANTUM) |
		   MPNIC_TQS_GROUP_INIT_CTL_INIT);
	mpnic_wr64(mpd, MPNIC_TQS_SET_INIT_CTL,
		   FIELD_PREP(MPNIC_TQS_SET_INIT_CTL_QUANTUM,
			      MPNIC_TQS_DWRR_INIT_QUANTUM) |
		   MPNIC_TQS_SET_INIT_CTL_INIT);
	mpnic_wr64(mpd, MPNIC_TQS_PORT_INIT_CTL,
		   FIELD_PREP(MPNIC_TQS_PORT_INIT_CTL_QUANTUM,
			      MPNIC_TQS_DWRR_INIT_QUANTUM) |
		   MPNIC_TQS_PORT_INIT_CTL_INIT);
	mpnic_wr64(mpd, MPNIC_TQS_SRAM_INIT_CTL,
		   FIELD_PREP(MPNIC_TQS_SRAM_INIT_CTL_QUANTUM,
			      MPNIC_TQS_DWRR_INIT_QUANTUM) |
		   MPNIC_TQS_SRAM_INIT_CTL_INIT);
}

static void mpnic_tqs_init(struct mpnic_dev *mpd)
{
	u64 val;

	/* Initialize to the largest frame we support, the scheduler
	 * truncates anything above it. The BMC gets the same limit.
	 */
	mpnic_wr64(mpd, MPNIC_TQS_MTU_CTL0, MPNIC_TQS_MTU_CTL0_MAX);
	mpnic_wr64(mpd, MPNIC_TQS_MTU_CTL1, MPNIC_TQS_MTU_CTL0_MAX);

	mpnic_wr64(mpd, MPNIC_TQS_GLBL_CTL0,
		   MPNIC_TQS_GLBL_CTL0_TWD_ERROR_CHECK_EN);

	mpnic_tqs_sram_init(mpd);

	/* Only port 0 is used. A single traffic class is in use as well, so
	 * TC0 gets all of the Tx buffer credits not reserved for the BMC.
	 */
	mpnic_wr64(mpd, MPNIC_TQS_GLBL_P0_0,
		   FIELD_PREP(MPNIC_TQS_GLBL_P0_0_TXB_MAX_CRDTS_0,
			      MPNIC_TQS_GLBL_TXB_CRDT_MAC));
	mpnic_wr64(mpd, MPNIC_TQS_GLBL_P0_1, 0);
	mpnic_wr64(mpd, MPNIC_TQS_GLBL_BMC,
		   FIELD_PREP(MPNIC_TQS_GLBL_BMC_TXB_MAX_CRDTS,
			      MPNIC_TQS_GLBL_TXB_CRDT_BMC));

	mpnic_wr64(mpd, MPNIC_TQS_PORT_CTL(0), 0);

	/* Map all sets to port 0 */
	mpnic_wr64(mpd, MPNIC_TQS_SET_P0_MAP0(0), ~0ULL);
	mpnic_wr64(mpd, MPNIC_TQS_SET_P0_MAP1(0), ~0ULL);

	mpnic_wr64(mpd, MPNIC_TQS_CEV_MIN_SCHED_THRESH_0,
		   FIELD_PREP(MPNIC_TQS_CEV_MIN_SCHED_THRESH_0_QUEUE,
			      MPNIC_TQS_DWRR_CEV_MIN_SCHED_THRESH) |
		   FIELD_PREP(MPNIC_TQS_CEV_MIN_SCHED_THRESH_0_GROUP,
			      MPNIC_TQS_DWRR_CEV_MIN_SCHED_THRESH));
	mpnic_wr64(mpd, MPNIC_TQS_CEV_MIN_SCHED_THRESH_1,
		   FIELD_PREP(MPNIC_TQS_CEV_MIN_SCHED_THRESH_1_SET,
			      MPNIC_TQS_DWRR_CEV_MIN_SCHED_THRESH) |
		   FIELD_PREP(MPNIC_TQS_CEV_MIN_SCHED_THRESH_1_PORT,
			      MPNIC_TQS_DWRR_CEV_MIN_SCHED_THRESH));

	/* The rate limiters are left uninitialized, so shaping has to stay
	 * off or nothing would ever get scheduled.
	 */
	mpnic_wr64(mpd, MPNIC_TQS_GLBL_SHAPING, MPNIC_TQS_GLBL_SHAPING_DISABLE);

	/* Enable fairness protection (phantom eligibility). Read modify
	 * write so that the reset default slowdown cycle is preserved.
	 */
	val = mpnic_rd64(mpd, MPNIC_TQS_SLOWDOWN_CTL);
	val |= MPNIC_TQS_SLOWDOWN_CTL_ENABLE;
	mpnic_wr64(mpd, MPNIC_TQS_SLOWDOWN_CTL, val);

	/* Use immediate credit decrement at every DWRR level so that the
	 * credit reflects a grant in the same cycle.
	 */
	val = mpnic_rd64(mpd, MPNIC_TQS_ARB_CTL);
	val |= MPNIC_TQS_ARB_CTL_SET_CRDT_BUCKET_EN |
	       MPNIC_TQS_ARB_CTL_SET_IMM_DECR_EN |
	       MPNIC_TQS_ARB_CTL_GROUP_CRDT_BUCKET_EN |
	       MPNIC_TQS_ARB_CTL_GROUP_IMM_DECR_EN |
	       MPNIC_TQS_ARB_CTL_QUEUE_CRDT_BUCKET_EN |
	       MPNIC_TQS_ARB_CTL_QUEUE_IMM_DECR_EN;
	mpnic_wr64(mpd, MPNIC_TQS_ARB_CTL, val);
}

/* The MPS and CLS fields sit at the same bit positions in every block, so
 * one set of masks covers both the RNI and the TNI registers.
 */
static void mpnic_mps_init(struct mpnic_dev *mpd, u32 reg, unsigned int mps,
			   unsigned int cls)
{
	u64 val = mpnic_rd64(mpd, reg);

	val &= ~(MPNIC_RNI_RDE_CTL_MPS | MPNIC_RNI_RDE_CTL_CLS);
	val |= FIELD_PREP(MPNIC_RNI_RDE_CTL_MPS, mps) |
	       FIELD_PREP(MPNIC_RNI_RDE_CTL_CLS, cls);

	mpnic_wr64(mpd, reg, val);
}

/* Likewise for the MRRS and CLS fields, which have their own common
 * layout.
 */
static void mpnic_mrrs_init(struct mpnic_dev *mpd, u32 reg, unsigned int mrrs,
			    unsigned int cls)
{
	u64 val = mpnic_rd64(mpd, reg);

	val &= ~(MPNIC_TNI_GLBL_TDF_CTL_MRRS | MPNIC_TNI_GLBL_TDF_CTL_CLS);
	val |= FIELD_PREP(MPNIC_TNI_GLBL_TDF_CTL_MRRS, mrrs) |
	       FIELD_PREP(MPNIC_TNI_GLBL_TDF_CTL_CLS, cls);

	mpnic_wr64(mpd, reg, val);
}

/**
 * mpnic_axi_init - Configure AXI bus parameters from host PCIe capabilities
 * @mpd: Device to configure
 *
 * Programs the max read request size, max payload size and cache line size
 * of the DMA engines. The hardware encodes all three as a power of 2 index,
 * MRRS and MPS relative to 128 B and CLS relative to 64 B.
 *
 * MAX_OT and MAX_OB are left at their hardware defaults.
 */
static void mpnic_axi_init(struct mpnic_dev *mpd)
{
	int mps, cls, mrrs;

	mps = clamp(ilog2(mpd->mps) - 7, 0, 3);
	cls = clamp(ilog2(L1_CACHE_BYTES) - 6, 0, 3);

	mpnic_mps_init(mpd, MPNIC_RNI_RDE_CTL, mps, cls);
	mpnic_mps_init(mpd, MPNIC_RNI_RCM_CTL, mps, cls);
	mpnic_mps_init(mpd, MPNIC_TNI_GLBL_TCM_CTL, mps, cls);

	mrrs = clamp(ilog2(mpd->readrq) - 7, 0, 3);
	mpnic_mrrs_init(mpd, MPNIC_RNI_RBP_CTL, mrrs, cls);
	mpnic_mrrs_init(mpd, MPNIC_TNI_GLBL_TDF_CTL, mrrs, cls);

	/* TDE supports a wider range of MRRS encodings. */
	mrrs = clamp(ilog2(mpd->readrq) - 7, 0, 5);
	mpnic_mrrs_init(mpd, MPNIC_TNI_GLBL_TDE_CTL, mrrs, cls);
}

/**
 * mpnic_ro_init - Set relaxed ordering on the outbound TLP attributes
 * @mpd: Device to configure
 *
 * Completions must stay ordered so that they are not observed before the
 * payload DMA they describe has landed, so RCM and TCM are left alone.
 */
static void mpnic_ro_init(struct mpnic_dev *mpd)
{
	u64 attr = mpd->relaxed_ord ? MPNIC_OB_ATTR_RO : 0;

	mpnic_wr64(mpd, MPNIC_OB_ATTR_TDE_H, attr);
	mpnic_wr64(mpd, MPNIC_OB_ATTR_TDE_P, attr);
	mpnic_wr64(mpd, MPNIC_OB_ATTR_TDF, attr);
	mpnic_wr64(mpd, MPNIC_OB_ATTR_RBP_HPQ, attr);
	mpnic_wr64(mpd, MPNIC_OB_ATTR_RBP_PPQ, attr);
	mpnic_wr64(mpd, MPNIC_OB_ATTR_RDE_H, attr);
	mpnic_wr64(mpd, MPNIC_OB_ATTR_RDE_P, attr);
}

static bool mpnic_init_status_ready(struct mpnic_dev *mpd,
				    const struct mpnic_init_poll *polls,
				    struct mpnic_poll_state *state)
{
	u64 val;
	int i;

	for (i = state->poll_idx; polls[i].addr; i++) {
		val = mpnic_rd64(mpd, polls[i].addr);

		if ((val & polls[i].exp_val) != polls[i].exp_val) {
			state->poll_idx = i;
			state->val = val;
			return false;
		}
	}

	return true;
}

/**
 * mpnic_mem_init_poll - Wait for the memory initializations to complete
 * @mpd: Device to poll
 *
 * The blocks initialize their memories in parallel, so walk the status
 * registers in order and only go back to sleep on the first one which is
 * not done yet.
 *
 * Return: 0 on success, -ETIMEDOUT if not everything completed in time
 */
static int mpnic_mem_init_poll(struct mpnic_dev *mpd)
{
	static const struct mpnic_init_poll polls[] = {
		{ MPNIC_TQS_ARB_INIT_VAL, MPNIC_TQS_SRAM_STS },
		{ MPNIC_MEM_INIT_BDQ_VAL, MPNIC_BDQ_MEM_INIT_DONE },
		{ MPNIC_MEM_INIT_RCM_VAL, MPNIC_RCM_MEM_INIT_DONE },
		{ MPNIC_MEM_INIT_RDE_VAL, MPNIC_RDE_MEM_INIT_DONE },
		{ MPNIC_MEM_INIT_RPC_VAL, MPNIC_RPC_MEM_INIT_DONE },
		{ MPNIC_MEM_INIT_TCM_VAL, MPNIC_TCM_MEM_INIT_DONE },
		{ MPNIC_MEM_INIT_TDE_VAL, MPNIC_TDE_MEM_INIT_DONE },
		{ MPNIC_MEM_INIT_TDF_VAL, MPNIC_TDF_MEM_INIT_DONE },
		{ MPNIC_MEM_INIT_RXB_VAL, MPNIC_RXB_MEM_INIT_DONE },
		{ 0 },
	};
	struct mpnic_poll_state state = {};
	bool done;
	int err;

	err = read_poll_timeout(mpnic_init_status_ready, done, done,
				MPNIC_MEM_INIT_POLL_US, MPNIC_MEM_INIT_TO_US,
				false, mpd, polls, &state);
	if (err)
		dev_err(mpd->dev, "Poll timeout for reg 0x%x: 0x%llx\n",
			polls[state.poll_idx].addr, state.val);

	return err;
}

int mpnic_dev_init(struct mpnic_dev *mpd)
{
	int err;

	mpnic_tdf_glbl_init(mpd);
	mpnic_txb_init(mpd);
	mpnic_rxb_init(mpd);
	mpnic_desc_sram_init(mpd);
	mpnic_axi_init(mpd);
	mpnic_ro_init(mpd);
	mpnic_rxglb_init(mpd);
	mpnic_txglb_init(mpd);
	mpnic_tqs_init(mpd);

	err = mpnic_mem_init_poll(mpd);
	if (err) {
		dev_err(mpd->dev, "Device initialization failed: %d\n", err);
		return err;
	}

	if (!mpnic_present(mpd))
		return -EIO;

	return 0;
}
