/*
 * TI EDMA DMA engine driver
 *
 * Copyright 2012 Texas Instruments
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/edma.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>

#include <linux/platform_data/edma.h>

#include "dmaengine.h"
#include "virt-dma.h"

/* Offsets matching "struct edmacc_param" */
#define PARM_OPT		0x00
#define PARM_SRC		0x04
#define PARM_A_B_CNT		0x08
#define PARM_DST		0x0c
#define PARM_SRC_DST_BIDX	0x10
#define PARM_LINK_BCNTRLD	0x14
#define PARM_SRC_DST_CIDX	0x18
#define PARM_CCNT		0x1c

#define PARM_SIZE		0x20

/* Offsets for EDMA CC global channel registers and their shadows */
#define SH_ER			0x00	/* 64 bits */
#define SH_ECR			0x08	/* 64 bits */
#define SH_ESR			0x10	/* 64 bits */
#define SH_CER			0x18	/* 64 bits */
#define SH_EER			0x20	/* 64 bits */
#define SH_EECR			0x28	/* 64 bits */
#define SH_EESR			0x30	/* 64 bits */
#define SH_SER			0x38	/* 64 bits */
#define SH_SECR			0x40	/* 64 bits */
#define SH_IER			0x50	/* 64 bits */
#define SH_IECR			0x58	/* 64 bits */
#define SH_IESR			0x60	/* 64 bits */
#define SH_IPR			0x68	/* 64 bits */
#define SH_ICR			0x70	/* 64 bits */
#define SH_IEVAL		0x78
#define SH_QER			0x80
#define SH_QEER			0x84
#define SH_QEECR		0x88
#define SH_QEESR		0x8c
#define SH_QSER			0x90
#define SH_QSECR		0x94
#define SH_SIZE			0x200

/* Offsets for EDMA CC global registers */
#define EDMA_REV		0x0000
#define EDMA_CCCFG		0x0004
#define EDMA_QCHMAP		0x0200	/* 8 registers */
#define EDMA_DMAQNUM		0x0240	/* 8 registers (4 on OMAP-L1xx) */
#define EDMA_QDMAQNUM		0x0260
#define EDMA_QUETCMAP		0x0280
#define EDMA_QUEPRI		0x0284
#define EDMA_EMR		0x0300	/* 64 bits */
#define EDMA_EMCR		0x0308	/* 64 bits */
#define EDMA_QEMR		0x0310
#define EDMA_QEMCR		0x0314
#define EDMA_CCERR		0x0318
#define EDMA_CCERRCLR		0x031c
#define EDMA_EEVAL		0x0320
#define EDMA_DRAE		0x0340	/* 4 x 64 bits*/
#define EDMA_QRAE		0x0380	/* 4 registers */
#define EDMA_QUEEVTENTRY	0x0400	/* 2 x 16 registers */
#define EDMA_QSTAT		0x0600	/* 2 registers */
#define EDMA_QWMTHRA		0x0620
#define EDMA_QWMTHRB		0x0624
#define EDMA_CCSTAT		0x0640

#define EDMA_M			0x1000	/* global channel registers */
#define EDMA_ECR		0x1008
#define EDMA_ECRH		0x100C
#define EDMA_SHADOW0		0x2000	/* 4 shadow regions */
#define EDMA_PARM		0x4000	/* PaRAM entries */

#define PARM_OFFSET(param_no)	(EDMA_PARM + ((param_no) << 5))

#define EDMA_DCHMAP		0x0100  /* 64 registers */

/* CCCFG register */
#define GET_NUM_DMACH(x)	(x & 0x7) /* bits 0-2 */
#define GET_NUM_PAENTRY(x)	((x & 0x7000) >> 12) /* bits 12-14 */
#define GET_NUM_EVQUE(x)	((x & 0x70000) >> 16) /* bits 16-18 */
#define GET_NUM_REGN(x)		((x & 0x300000) >> 20) /* bits 20-21 */
#define CHMAP_EXIST		BIT(24)

/*
 * Max of 20 segments per channel to conserve PaRAM slots
 * Also note that MAX_NR_SG should be atleast the no.of periods
 * that are required for ASoC, otherwise DMA prep calls will
 * fail. Today davinci-pcm is the only user of this driver and
 * requires atleast 17 slots, so we setup the default to 20.
 */
#define MAX_NR_SG		20
#define EDMA_MAX_SLOTS		MAX_NR_SG
#define EDMA_DESCRIPTORS	16

#define EDMA_CHANNEL_ANY		-1	/* for edma_alloc_channel() */
#define EDMA_SLOT_ANY			-1	/* for edma_alloc_slot() */
#define EDMA_CONT_PARAMS_ANY		 1001
#define EDMA_CONT_PARAMS_FIXED_EXACT	 1002
#define EDMA_CONT_PARAMS_FIXED_NOT_EXACT 1003

/* PaRAM slots are laid out like this */
struct edmacc_param {
	u32 opt;
	u32 src;
	u32 a_b_cnt;
	u32 dst;
	u32 src_dst_bidx;
	u32 link_bcntrld;
	u32 src_dst_cidx;
	u32 ccnt;
} __packed;

/* fields in edmacc_param.opt */
#define SAM		BIT(0)
#define DAM		BIT(1)
#define SYNCDIM		BIT(2)
#define STATIC		BIT(3)
#define EDMA_FWID	(0x07 << 8)
#define TCCMODE		BIT(11)
#define EDMA_TCC(t)	((t) << 12)
#define TCINTEN		BIT(20)
#define ITCINTEN	BIT(21)
#define TCCHEN		BIT(22)
#define ITCCHEN		BIT(23)

struct edma_pset {
	u32				len;
	dma_addr_t			addr;
	struct edmacc_param		param;
};

struct edma_desc {
	struct virt_dma_desc		vdesc;
	struct list_head		node;
	enum dma_transfer_direction	direction;
	int				cyclic;
	int				absync;
	int				pset_nr;
	struct edma_chan		*echan;
	int				processed;

	/*
	 * The following 4 elements are used for residue accounting.
	 *
	 * - processed_stat: the number of SG elements we have traversed
	 * so far to cover accounting. This is updated directly to processed
	 * during edma_callback and is always <= processed, because processed
	 * refers to the number of pending transfer (programmed to EDMA
	 * controller), where as processed_stat tracks number of transfers
	 * accounted for so far.
	 *
	 * - residue: The amount of bytes we have left to transfer for this desc
	 *
	 * - residue_stat: The residue in bytes of data we have covered
	 * so far for accounting. This is updated directly to residue
	 * during callbacks to keep it current.
	 *
	 * - sg_len: Tracks the length of the current intermediate transfer,
	 * this is required to update the residue during intermediate transfer
	 * completion callback.
	 */
	int				processed_stat;
	u32				sg_len;
	u32				residue;
	u32				residue_stat;

	struct edma_pset		pset[0];
};

struct edma_cc;

struct edma_chan {
	struct virt_dma_chan		vchan;
	struct list_head		node;
	struct edma_desc		*edesc;
	struct edma_cc			*ecc;
	int				ch_num;
	bool				alloced;
	int				slot[EDMA_MAX_SLOTS];
	int				missed;
	struct dma_slave_config		cfg;
};

struct edma_cc {
	struct device			*dev;
	struct edma_soc_info		*info;
	void __iomem			*base;
	int				id;

	/* eDMA3 resource information */
	unsigned			num_channels;
	unsigned			num_region;
	unsigned			num_slots;
	unsigned			num_tc;
	enum dma_event_q		default_queue;

	bool				unused_chan_list_done;
	/* The edma_inuse bit for each PaRAM slot is clear unless the
	 * channel is in use ... by ARM or DSP, for QDMA, or whatever.
	 */
	unsigned long *edma_inuse;

	/* The edma_unused bit for each channel is clear unless
	 * it is not being used on this platform. It uses a bit
	 * of SOC-specific initialization code.
	 */
	unsigned long *edma_unused;

	struct dma_device		dma_slave;
	struct edma_chan		*slave_chans;
	int				dummy_slot;
};

/* dummy param set used to (re)initialize parameter RAM slots */
static const struct edmacc_param dummy_paramset = {
	.link_bcntrld = 0xffff,
	.ccnt = 1,
};

static const struct of_device_id edma_of_ids[] = {
	{ .compatible = "ti,edma3", },
	{}
};

static inline unsigned int edma_read(struct edma_cc *ecc, int offset)
{
	return (unsigned int)__raw_readl(ecc->base + offset);
}

static inline void edma_write(struct edma_cc *ecc, int offset, int val)
{
	__raw_writel(val, ecc->base + offset);
}

static inline void edma_modify(struct edma_cc *ecc, int offset, unsigned and,
			       unsigned or)
{
	unsigned val = edma_read(ecc, offset);

	val &= and;
	val |= or;
	edma_write(ecc, offset, val);
}

static inline void edma_and(struct edma_cc *ecc, int offset, unsigned and)
{
	unsigned val = edma_read(ecc, offset);

	val &= and;
	edma_write(ecc, offset, val);
}

static inline void edma_or(struct edma_cc *ecc, int offset, unsigned or)
{
	unsigned val = edma_read(ecc, offset);

	val |= or;
	edma_write(ecc, offset, val);
}

static inline unsigned int edma_read_array(struct edma_cc *ecc, int offset,
					   int i)
{
	return edma_read(ecc, offset + (i << 2));
}

static inline void edma_write_array(struct edma_cc *ecc, int offset, int i,
				    unsigned val)
{
	edma_write(ecc, offset + (i << 2), val);
}

static inline void edma_modify_array(struct edma_cc *ecc, int offset, int i,
				     unsigned and, unsigned or)
{
	edma_modify(ecc, offset + (i << 2), and, or);
}

static inline void edma_or_array(struct edma_cc *ecc, int offset, int i,
				 unsigned or)
{
	edma_or(ecc, offset + (i << 2), or);
}

static inline void edma_or_array2(struct edma_cc *ecc, int offset, int i, int j,
				  unsigned or)
{
	edma_or(ecc, offset + ((i * 2 + j) << 2), or);
}

static inline void edma_write_array2(struct edma_cc *ecc, int offset, int i,
				     int j, unsigned val)
{
	edma_write(ecc, offset + ((i * 2 + j) << 2), val);
}

static inline unsigned int edma_shadow0_read(struct edma_cc *ecc, int offset)
{
	return edma_read(ecc, EDMA_SHADOW0 + offset);
}

static inline unsigned int edma_shadow0_read_array(struct edma_cc *ecc,
						   int offset, int i)
{
	return edma_read(ecc, EDMA_SHADOW0 + offset + (i << 2));
}

static inline void edma_shadow0_write(struct edma_cc *ecc, int offset,
				      unsigned val)
{
	edma_write(ecc, EDMA_SHADOW0 + offset, val);
}

static inline void edma_shadow0_write_array(struct edma_cc *ecc, int offset,
					    int i, unsigned val)
{
	edma_write(ecc, EDMA_SHADOW0 + offset + (i << 2), val);
}

static inline unsigned int edma_parm_read(struct edma_cc *ecc, int offset,
					  int param_no)
{
	return edma_read(ecc, EDMA_PARM + offset + (param_no << 5));
}

static inline void edma_parm_write(struct edma_cc *ecc, int offset,
				   int param_no, unsigned val)
{
	edma_write(ecc, EDMA_PARM + offset + (param_no << 5), val);
}

static inline void edma_parm_modify(struct edma_cc *ecc, int offset,
				    int param_no, unsigned and, unsigned or)
{
	edma_modify(ecc, EDMA_PARM + offset + (param_no << 5), and, or);
}

static inline void edma_parm_and(struct edma_cc *ecc, int offset, int param_no,
				 unsigned and)
{
	edma_and(ecc, EDMA_PARM + offset + (param_no << 5), and);
}

static inline void edma_parm_or(struct edma_cc *ecc, int offset, int param_no,
				unsigned or)
{
	edma_or(ecc, EDMA_PARM + offset + (param_no << 5), or);
}

static inline void set_bits(int offset, int len, unsigned long *p)
{
	for (; len > 0; len--)
		set_bit(offset + (len - 1), p);
}

static inline void clear_bits(int offset, int len, unsigned long *p)
{
	for (; len > 0; len--)
		clear_bit(offset + (len - 1), p);
}

static void edma_map_dmach_to_queue(struct edma_cc *ecc, unsigned ch_no,
				    enum dma_event_q queue_no)
{
	int bit = (ch_no & 0x7) * 4;

	/* default to low priority queue */
	if (queue_no == EVENTQ_DEFAULT)
		queue_no = ecc->default_queue;

	queue_no &= 7;
	edma_modify_array(ecc, EDMA_DMAQNUM, (ch_no >> 3), ~(0x7 << bit),
			  queue_no << bit);
}

static void edma_assign_priority_to_queue(struct edma_cc *ecc, int queue_no,
					  int priority)
{
	int bit = queue_no * 4;

	edma_modify(ecc, EDMA_QUEPRI, ~(0x7 << bit), ((priority & 0x7) << bit));
}

static void edma_direct_dmach_to_param_mapping(struct edma_cc *ecc)
{
	int i;

	for (i = 0; i < ecc->num_channels; i++)
		edma_write_array(ecc, EDMA_DCHMAP, i, (i << 5));
}

static int prepare_unused_channel_list(struct device *dev, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct edma_cc *ecc = data;
	int dma_req_min = EDMA_CTLR_CHAN(ecc->id, 0);
	int dma_req_max = dma_req_min + ecc->num_channels;
	int i, count;
	struct of_phandle_args  dma_spec;

	if (dev->of_node) {
		struct platform_device *dma_pdev;

		count = of_property_count_strings(dev->of_node, "dma-names");
		if (count < 0)
			return 0;
		for (i = 0; i < count; i++) {
			if (of_parse_phandle_with_args(dev->of_node, "dmas",
						       "#dma-cells", i,
						       &dma_spec))
				continue;

			if (!of_match_node(edma_of_ids, dma_spec.np)) {
				of_node_put(dma_spec.np);
				continue;
			}

			dma_pdev = of_find_device_by_node(dma_spec.np);
			if (&dma_pdev->dev != ecc->dev)
				continue;

			clear_bit(EDMA_CHAN_SLOT(dma_spec.args[0]),
				  ecc->edma_unused);
			of_node_put(dma_spec.np);
		}
		return 0;
	}

	/* For non-OF case */
	for (i = 0; i < pdev->num_resources; i++) {
		struct resource	*res = &pdev->resource[i];
		int dma_req;

		if (!(res->flags & IORESOURCE_DMA))
			continue;

		dma_req = (int)res->start;
		if (dma_req >= dma_req_min && dma_req < dma_req_max)
			clear_bit(EDMA_CHAN_SLOT(pdev->resource[i].start),
				  ecc->edma_unused);
	}

	return 0;
}

static void edma_setup_interrupt(struct edma_cc *ecc, unsigned lch, bool enable)
{
	lch = EDMA_CHAN_SLOT(lch);

	if (enable) {
		edma_shadow0_write_array(ecc, SH_ICR, lch >> 5,
					 BIT(lch & 0x1f));
		edma_shadow0_write_array(ecc, SH_IESR, lch >> 5,
					 BIT(lch & 0x1f));
	} else {
		edma_shadow0_write_array(ecc, SH_IECR, lch >> 5,
					 BIT(lch & 0x1f));
	}
}

/*
 * paRAM slot management functions
 */
static void edma_write_slot(struct edma_cc *ecc, unsigned slot,
			    const struct edmacc_param *param)
{
	slot = EDMA_CHAN_SLOT(slot);
	if (slot >= ecc->num_slots)
		return;
	memcpy_toio(ecc->base + PARM_OFFSET(slot), param, PARM_SIZE);
}

static void edma_read_slot(struct edma_cc *ecc, unsigned slot,
			   struct edmacc_param *param)
{
	slot = EDMA_CHAN_SLOT(slot);
	if (slot >= ecc->num_slots)
		return;
	memcpy_fromio(param, ecc->base + PARM_OFFSET(slot), PARM_SIZE);
}

/**
 * edma_alloc_slot - allocate DMA parameter RAM
 * @ecc: pointer to edma_cc struct
 * @slot: specific slot to allocate; negative for "any unused slot"
 *
 * This allocates a parameter RAM slot, initializing it to hold a
 * dummy transfer.  Slots allocated using this routine have not been
 * mapped to a hardware DMA channel, and will normally be used by
 * linking to them from a slot associated with a DMA channel.
 *
 * Normal use is to pass EDMA_SLOT_ANY as the @slot, but specific
 * slots may be allocated on behalf of DSP firmware.
 *
 * Returns the number of the slot, else negative errno.
 */
static int edma_alloc_slot(struct edma_cc *ecc, int slot)
{
	if (slot > 0)
		slot = EDMA_CHAN_SLOT(slot);
	if (slot < 0) {
		slot = ecc->num_channels;
		for (;;) {
			slot = find_next_zero_bit(ecc->edma_inuse,
						  ecc->num_slots,
						  slot);
			if (slot == ecc->num_slots)
				return -ENOMEM;
			if (!test_and_set_bit(slot, ecc->edma_inuse))
				break;
		}
	} else if (slot < ecc->num_channels || slot >= ecc->num_slots) {
		return -EINVAL;
	} else if (test_and_set_bit(slot, ecc->edma_inuse)) {
		return -EBUSY;
	}

	edma_write_slot(ecc, slot, &dummy_paramset);

	return EDMA_CTLR_CHAN(ecc->id, slot);
}

static void edma_free_slot(struct edma_cc *ecc, unsigned slot)
{
	slot = EDMA_CHAN_SLOT(slot);
	if (slot < ecc->num_channels || slot >= ecc->num_slots)
		return;

	edma_write_slot(ecc, slot, &dummy_paramset);
	clear_bit(slot, ecc->edma_inuse);
}

/**
 * edma_link - link one parameter RAM slot to another
 * @ecc: pointer to edma_cc struct
 * @from: parameter RAM slot originating the link
 * @to: parameter RAM slot which is the link target
 *
 * The originating slot should not be part of any active DMA transfer.
 */
static void edma_link(struct edma_cc *ecc, unsigned from, unsigned to)
{
	if (unlikely(EDMA_CTLR(from) != EDMA_CTLR(to)))
		dev_warn(ecc->dev, "Ignoring eDMA instance for linking\n");

	from = EDMA_CHAN_SLOT(from);
	to = EDMA_CHAN_SLOT(to);
	if (from >= ecc->num_slots || to >= ecc->num_slots)
		return;

	edma_parm_modify(ecc, PARM_LINK_BCNTRLD, from, 0xffff0000,
			 PARM_OFFSET(to));
}

/**
 * edma_get_position - returns the current transfer point
 * @ecc: pointer to edma_cc struct
 * @slot: parameter RAM slot being examined
 * @dst:  true selects the dest position, false the source
 *
 * Returns the position of the current active slot
 */
static dma_addr_t edma_get_position(struct edma_cc *ecc, unsigned slot,
				    bool dst)
{
	u32 offs;

	slot = EDMA_CHAN_SLOT(slot);
	offs = PARM_OFFSET(slot);
	offs += dst ? PARM_DST : PARM_SRC;

	return edma_read(ecc, offs);
}

/*-----------------------------------------------------------------------*/
/**
 * edma_start - start dma on a channel
 * @ecc: pointer to edma_cc struct
 * @channel: channel being activated
 *
 * Channels with event associations will be triggered by their hardware
 * events, and channels without such associations will be triggered by
 * software.  (At this writing there is no interface for using software
 * triggers except with channels that don't support hardware triggers.)
 *
 * Returns zero on success, else negative errno.
 */
static int edma_start(struct edma_cc *ecc, unsigned channel)
{
	if (ecc->id != EDMA_CTLR(channel)) {
		dev_err(ecc->dev, "%s: ID mismatch for eDMA%d: %d\n", __func__,
			ecc->id, EDMA_CTLR(channel));
		return -EINVAL;
	}
	channel = EDMA_CHAN_SLOT(channel);

	if (channel < ecc->num_channels) {
		int j = channel >> 5;
		unsigned int mask = BIT(channel & 0x1f);

		/* EDMA channels without event association */
		if (test_bit(channel, ecc->edma_unused)) {
			dev_dbg(ecc->dev, "ESR%d %08x\n", j,
				edma_shadow0_read_array(ecc, SH_ESR, j));
			edma_shadow0_write_array(ecc, SH_ESR, j, mask);
			return 0;
		}

		/* EDMA channel with event association */
		dev_dbg(ecc->dev, "ER%d %08x\n", j,
			edma_shadow0_read_array(ecc, SH_ER, j));
		/* Clear any pending event or error */
		edma_write_array(ecc, EDMA_ECR, j, mask);
		edma_write_array(ecc, EDMA_EMCR, j, mask);
		/* Clear any SER */
		edma_shadow0_write_array(ecc, SH_SECR, j, mask);
		edma_shadow0_write_array(ecc, SH_EESR, j, mask);
		dev_dbg(ecc->dev, "EER%d %08x\n", j,
			edma_shadow0_read_array(ecc, SH_EER, j));
		return 0;
	}

	return -EINVAL;
}

/**
 * edma_stop - stops dma on the channel passed
 * @ecc: pointer to edma_cc struct
 * @channel: channel being deactivated
 *
 * Any active transfer is paused and all pending hardware events are cleared.
 * The current transfer may not be resumed, and the channel's Parameter RAM
 * should be reinitialized before being reused.
 */
static void edma_stop(struct edma_cc *ecc, unsigned channel)
{
	if (ecc->id != EDMA_CTLR(channel)) {
		dev_err(ecc->dev, "%s: ID mismatch for eDMA%d: %d\n", __func__,
			ecc->id, EDMA_CTLR(channel));
		return;
	}
	channel = EDMA_CHAN_SLOT(channel);

	if (channel < ecc->num_channels) {
		int j = channel >> 5;
		unsigned int mask = BIT(channel & 0x1f);

		edma_shadow0_write_array(ecc, SH_EECR, j, mask);
		edma_shadow0_write_array(ecc, SH_ECR, j, mask);
		edma_shadow0_write_array(ecc, SH_SECR, j, mask);
		edma_write_array(ecc, EDMA_EMCR, j, mask);

		/* clear possibly pending completion interrupt */
		edma_shadow0_write_array(ecc, SH_ICR, j, mask);

		dev_dbg(ecc->dev, "EER%d %08x\n", j,
			edma_shadow0_read_array(ecc, SH_EER, j));

		/* REVISIT:  consider guarding against inappropriate event
		 * chaining by overwriting with dummy_paramset.
		 */
	}
}

/*
 * Temporarily disable EDMA hardware events on the specified channel,
 * preventing them from triggering new transfers
 */
static void edma_pause(struct edma_cc *ecc, unsigned channel)
{
	if (ecc->id != EDMA_CTLR(channel)) {
		dev_err(ecc->dev, "%s: ID mismatch for eDMA%d: %d\n", __func__,
			ecc->id, EDMA_CTLR(channel));
		return;
	}
	channel = EDMA_CHAN_SLOT(channel);

	if (channel < ecc->num_channels) {
		unsigned int mask = BIT(channel & 0x1f);

		edma_shadow0_write_array(ecc, SH_EECR, channel >> 5, mask);
	}
}

/* Re-enable EDMA hardware events on the specified channel.  */
static void edma_resume(struct edma_cc *ecc, unsigned channel)
{
	if (ecc->id != EDMA_CTLR(channel)) {
		dev_err(ecc->dev, "%s: ID mismatch for eDMA%d: %d\n", __func__,
			ecc->id, EDMA_CTLR(channel));
		return;
	}
	channel = EDMA_CHAN_SLOT(channel);

	if (channel < ecc->num_channels) {
		unsigned int mask = BIT(channel & 0x1f);

		edma_shadow0_write_array(ecc, SH_EESR, channel >> 5, mask);
	}
}

static int edma_trigger_channel(struct edma_cc *ecc, unsigned channel)
{
	unsigned int mask;

	if (ecc->id != EDMA_CTLR(channel)) {
		dev_err(ecc->dev, "%s: ID mismatch for eDMA%d: %d\n", __func__,
			ecc->id, EDMA_CTLR(channel));
		return -EINVAL;
	}
	channel = EDMA_CHAN_SLOT(channel);
	mask = BIT(channel & 0x1f);

	edma_shadow0_write_array(ecc, SH_ESR, (channel >> 5), mask);

	dev_dbg(ecc->dev, "ESR%d %08x\n", (channel >> 5),
		edma_shadow0_read_array(ecc, SH_ESR, (channel >> 5)));
	return 0;
}

static void edma_clean_channel(struct edma_cc *ecc, unsigned channel)
{
	if (ecc->id != EDMA_CTLR(channel)) {
		dev_err(ecc->dev, "%s: ID mismatch for eDMA%d: %d\n", __func__,
			ecc->id, EDMA_CTLR(channel));
		return;
	}
	channel = EDMA_CHAN_SLOT(channel);

	if (channel < ecc->num_channels) {
		int j = (channel >> 5);
		unsigned int mask = BIT(channel & 0x1f);

		dev_dbg(ecc->dev, "EMR%d %08x\n", j,
			edma_read_array(ecc, EDMA_EMR, j));
		edma_shadow0_write_array(ecc, SH_ECR, j, mask);
		/* Clear the corresponding EMR bits */
		edma_write_array(ecc, EDMA_EMCR, j, mask);
		/* Clear any SER */
		edma_shadow0_write_array(ecc, SH_SECR, j, mask);
		edma_write(ecc, EDMA_CCERRCLR, BIT(16) | BIT(1) | BIT(0));
	}
}

/**
 * edma_alloc_channel - allocate DMA channel and paired parameter RAM
 * @ecc: pointer to edma_cc struct
 * @channel: specific channel to allocate; negative for "any unmapped channel"
 * @eventq_no: an EVENTQ_* constant, used to choose which Transfer
 *	Controller (TC) executes requests using this channel.  Use
 *	EVENTQ_DEFAULT unless you really need a high priority queue.
 *
 * This allocates a DMA channel and its associated parameter RAM slot.
 * The parameter RAM is initialized to hold a dummy transfer.
 *
 * Normal use is to pass a specific channel number as @channel, to make
 * use of hardware events mapped to that channel.  When the channel will
 * be used only for software triggering or event chaining, channels not
 * mapped to hardware events (or mapped to unused events) are preferable.
 *
 * DMA transfers start from a channel using edma_start(), or by
 * chaining.  When the transfer described in that channel's parameter RAM
 * slot completes, that slot's data may be reloaded through a link.
 *
 * DMA errors are only reported to the @callback associated with the
 * channel driving that transfer, but transfer completion callbacks can
 * be sent to another channel under control of the TCC field in
 * the option word of the transfer's parameter RAM set.  Drivers must not
 * use DMA transfer completion callbacks for channels they did not allocate.
 * (The same applies to TCC codes used in transfer chaining.)
 *
 * Returns the number of the channel, else negative errno.
 */
static int edma_alloc_channel(struct edma_cc *ecc, int channel,
			      enum dma_event_q eventq_no)
{
	unsigned done = 0;
	int ret = 0;

	if (!ecc->unused_chan_list_done) {
		/*
		 * Scan all the platform devices to find out the EDMA channels
		 * used and clear them in the unused list, making the rest
		 * available for ARM usage.
		 */
		ret = bus_for_each_dev(&platform_bus_type, NULL, ecc,
				       prepare_unused_channel_list);
		if (ret < 0)
			return ret;

		ecc->unused_chan_list_done = true;
	}

	if (channel >= 0) {
		if (ecc->id != EDMA_CTLR(channel)) {
			dev_err(ecc->dev, "%s: ID mismatch for eDMA%d: %d\n",
				__func__, ecc->id, EDMA_CTLR(channel));
			return -EINVAL;
		}
		channel = EDMA_CHAN_SLOT(channel);
	}

	if (channel < 0) {
		channel = 0;
		for (;;) {
			channel = find_next_bit(ecc->edma_unused,
						ecc->num_channels, channel);
			if (channel == ecc->num_channels)
				break;
			if (!test_and_set_bit(channel, ecc->edma_inuse)) {
				done = 1;
				break;
			}
			channel++;
		}
		if (!done)
			return -ENOMEM;
	} else if (channel >= ecc->num_channels) {
		return -EINVAL;
	} else if (test_and_set_bit(channel, ecc->edma_inuse)) {
		return -EBUSY;
	}

	/* ensure access through shadow region 0 */
	edma_or_array2(ecc, EDMA_DRAE, 0, channel >> 5, BIT(channel & 0x1f));

	/* ensure no events are pending */
	edma_stop(ecc, EDMA_CTLR_CHAN(ecc->id, channel));
	edma_write_slot(ecc, channel, &dummy_paramset);

	edma_setup_interrupt(ecc, EDMA_CTLR_CHAN(ecc->id, channel), true);

	edma_map_dmach_to_queue(ecc, channel, eventq_no);

	return EDMA_CTLR_CHAN(ecc->id, channel);
}

/**
 * edma_free_channel - deallocate DMA channel
 * @ecc: pointer to edma_cc struct
 * @channel: dma channel returned from edma_alloc_channel()
 *
 * This deallocates the DMA channel and associated parameter RAM slot
 * allocated by edma_alloc_channel().
 *
 * Callers are responsible for ensuring the channel is inactive, and
 * will not be reactivated by linking, chaining, or software calls to
 * edma_start().
 */
static void edma_free_channel(struct edma_cc *ecc, unsigned channel)
{
	if (ecc->id != EDMA_CTLR(channel)) {
		dev_err(ecc->dev, "%s: ID mismatch for eDMA%d: %d\n", __func__,
			ecc->id, EDMA_CTLR(channel));
		return;
	}
	channel = EDMA_CHAN_SLOT(channel);

	if (channel >= ecc->num_channels)
		return;

	edma_setup_interrupt(ecc, channel, false);
	/* REVISIT should probably take out of shadow region 0 */

	edma_write_slot(ecc, channel, &dummy_paramset);
	clear_bit(channel, ecc->edma_inuse);
}

/* Move channel to a specific event queue */
static void edma_assign_channel_eventq(struct edma_cc *ecc, unsigned channel,
				       enum dma_event_q eventq_no)
{
	if (ecc->id != EDMA_CTLR(channel)) {
		dev_err(ecc->dev, "%s: ID mismatch for eDMA%d: %d\n", __func__,
			ecc->id, EDMA_CTLR(channel));
		return;
	}
	channel = EDMA_CHAN_SLOT(channel);

	if (channel >= ecc->num_channels)
		return;

	/* default to low priority queue */
	if (eventq_no == EVENTQ_DEFAULT)
		eventq_no = ecc->default_queue;
	if (eventq_no >= ecc->num_tc)
		return;

	edma_map_dmach_to_queue(ecc, channel, eventq_no);
}

static inline struct edma_cc *to_edma_cc(struct dma_device *d)
{
	return container_of(d, struct edma_cc, dma_slave);
}

static inline struct edma_chan *to_edma_chan(struct dma_chan *c)
{
	return container_of(c, struct edma_chan, vchan.chan);
}

static inline struct edma_desc *to_edma_desc(struct dma_async_tx_descriptor *tx)
{
	return container_of(tx, struct edma_desc, vdesc.tx);
}

static void edma_desc_free(struct virt_dma_desc *vdesc)
{
	kfree(container_of(vdesc, struct edma_desc, vdesc));
}

/* Dispatch a queued descriptor to the controller (caller holds lock) */
static void edma_execute(struct edma_chan *echan)
{
	struct edma_cc *ecc = echan->ecc;
	struct virt_dma_desc *vdesc;
	struct edma_desc *edesc;
	struct device *dev = echan->vchan.chan.device->dev;
	int i, j, left, nslots;

	if (!echan->edesc) {
		/* Setup is needed for the first transfer */
		vdesc = vchan_next_desc(&echan->vchan);
		if (!vdesc)
			return;
		list_del(&vdesc->node);
		echan->edesc = to_edma_desc(&vdesc->tx);
	}

	edesc = echan->edesc;

	/* Find out how many left */
	left = edesc->pset_nr - edesc->processed;
	nslots = min(MAX_NR_SG, left);
	edesc->sg_len = 0;

	/* Write descriptor PaRAM set(s) */
	for (i = 0; i < nslots; i++) {
		j = i + edesc->processed;
		edma_write_slot(ecc, echan->slot[i], &edesc->pset[j].param);
		edesc->sg_len += edesc->pset[j].len;
		dev_vdbg(dev,
			 "\n pset[%d]:\n"
			 "  chnum\t%d\n"
			 "  slot\t%d\n"
			 "  opt\t%08x\n"
			 "  src\t%08x\n"
			 "  dst\t%08x\n"
			 "  abcnt\t%08x\n"
			 "  ccnt\t%08x\n"
			 "  bidx\t%08x\n"
			 "  cidx\t%08x\n"
			 "  lkrld\t%08x\n",
			 j, echan->ch_num, echan->slot[i],
			 edesc->pset[j].param.opt,
			 edesc->pset[j].param.src,
			 edesc->pset[j].param.dst,
			 edesc->pset[j].param.a_b_cnt,
			 edesc->pset[j].param.ccnt,
			 edesc->pset[j].param.src_dst_bidx,
			 edesc->pset[j].param.src_dst_cidx,
			 edesc->pset[j].param.link_bcntrld);
		/* Link to the previous slot if not the last set */
		if (i != (nslots - 1))
			edma_link(ecc, echan->slot[i], echan->slot[i + 1]);
	}

	edesc->processed += nslots;

	/*
	 * If this is either the last set in a set of SG-list transactions
	 * then setup a link to the dummy slot, this results in all future
	 * events being absorbed and that's OK because we're done
	 */
	if (edesc->processed == edesc->pset_nr) {
		if (edesc->cyclic)
			edma_link(ecc, echan->slot[nslots - 1], echan->slot[1]);
		else
			edma_link(ecc, echan->slot[nslots - 1],
				  echan->ecc->dummy_slot);
	}

	if (echan->missed) {
		/*
		 * This happens due to setup times between intermediate
		 * transfers in long SG lists which have to be broken up into
		 * transfers of MAX_NR_SG
		 */
		dev_dbg(dev, "missed event on channel %d\n", echan->ch_num);
		edma_clean_channel(ecc, echan->ch_num);
		edma_stop(ecc, echan->ch_num);
		edma_start(ecc, echan->ch_num);
		edma_trigger_channel(ecc, echan->ch_num);
		echan->missed = 0;
	} else if (edesc->processed <= MAX_NR_SG) {
		dev_dbg(dev, "first transfer starting on channel %d\n",
			echan->ch_num);
		edma_start(ecc, echan->ch_num);
	} else {
		dev_dbg(dev, "chan: %d: completed %d elements, resuming\n",
			echan->ch_num, edesc->processed);
		edma_resume(ecc, echan->ch_num);
	}
}

static int edma_terminate_all(struct dma_chan *chan)
{
	struct edma_chan *echan = to_edma_chan(chan);
	unsigned long flags;
	LIST_HEAD(head);

	spin_lock_irqsave(&echan->vchan.lock, flags);

	/*
	 * Stop DMA activity: we assume the callback will not be called
	 * after edma_dma() returns (even if it does, it will see
	 * echan->edesc is NULL and exit.)
	 */
	if (echan->edesc) {
		edma_stop(echan->ecc, echan->ch_num);
		/* Move the cyclic channel back to default queue */
		if (echan->edesc->cyclic)
			edma_assign_channel_eventq(echan->ecc, echan->ch_num,
						   EVENTQ_DEFAULT);
		/*
		 * free the running request descriptor
		 * since it is not in any of the vdesc lists
		 */
		edma_desc_free(&echan->edesc->vdesc);
		echan->edesc = NULL;
	}

	vchan_get_all_descriptors(&echan->vchan, &head);
	spin_unlock_irqrestore(&echan->vchan.lock, flags);
	vchan_dma_desc_free_list(&echan->vchan, &head);

	return 0;
}

static int edma_slave_config(struct dma_chan *chan,
	struct dma_slave_config *cfg)
{
	struct edma_chan *echan = to_edma_chan(chan);

	if (cfg->src_addr_width == DMA_SLAVE_BUSWIDTH_8_BYTES ||
	    cfg->dst_addr_width == DMA_SLAVE_BUSWIDTH_8_BYTES)
		return -EINVAL;

	memcpy(&echan->cfg, cfg, sizeof(echan->cfg));

	return 0;
}

static int edma_dma_pause(struct dma_chan *chan)
{
	struct edma_chan *echan = to_edma_chan(chan);

	if (!echan->edesc)
		return -EINVAL;

	edma_pause(echan->ecc, echan->ch_num);
	return 0;
}

static int edma_dma_resume(struct dma_chan *chan)
{
	struct edma_chan *echan = to_edma_chan(chan);

	edma_resume(echan->ecc, echan->ch_num);
	return 0;
}

/*
 * A PaRAM set configuration abstraction used by other modes
 * @chan: Channel who's PaRAM set we're configuring
 * @pset: PaRAM set to initialize and setup.
 * @src_addr: Source address of the DMA
 * @dst_addr: Destination address of the DMA
 * @burst: In units of dev_width, how much to send
 * @dev_width: How much is the dev_width
 * @dma_length: Total length of the DMA transfer
 * @direction: Direction of the transfer
 */
static int edma_config_pset(struct dma_chan *chan, struct edma_pset *epset,
			    dma_addr_t src_addr, dma_addr_t dst_addr, u32 burst,
			    enum dma_slave_buswidth dev_width,
			    unsigned int dma_length,
			    enum dma_transfer_direction direction)
{
	struct edma_chan *echan = to_edma_chan(chan);
	struct device *dev = chan->device->dev;
	struct edmacc_param *param = &epset->param;
	int acnt, bcnt, ccnt, cidx;
	int src_bidx, dst_bidx, src_cidx, dst_cidx;
	int absync;

	acnt = dev_width;

	/* src/dst_maxburst == 0 is the same case as src/dst_maxburst == 1 */
	if (!burst)
		burst = 1;
	/*
	 * If the maxburst is equal to the fifo width, use
	 * A-synced transfers. This allows for large contiguous
	 * buffer transfers using only one PaRAM set.
	 */
	if (burst == 1) {
		/*
		 * For the A-sync case, bcnt and ccnt are the remainder
		 * and quotient respectively of the division of:
		 * (dma_length / acnt) by (SZ_64K -1). This is so
		 * that in case bcnt over flows, we have ccnt to use.
		 * Note: In A-sync tranfer only, bcntrld is used, but it
		 * only applies for sg_dma_len(sg) >= SZ_64K.
		 * In this case, the best way adopted is- bccnt for the
		 * first frame will be the remainder below. Then for
		 * every successive frame, bcnt will be SZ_64K-1. This
		 * is assured as bcntrld = 0xffff in end of function.
		 */
		absync = false;
		ccnt = dma_length / acnt / (SZ_64K - 1);
		bcnt = dma_length / acnt - ccnt * (SZ_64K - 1);
		/*
		 * If bcnt is non-zero, we have a remainder and hence an
		 * extra frame to transfer, so increment ccnt.
		 */
		if (bcnt)
			ccnt++;
		else
			bcnt = SZ_64K - 1;
		cidx = acnt;
	} else {
		/*
		 * If maxburst is greater than the fifo address_width,
		 * use AB-synced transfers where A count is the fifo
		 * address_width and B count is the maxburst. In this
		 * case, we are limited to transfers of C count frames
		 * of (address_width * maxburst) where C count is limited
		 * to SZ_64K-1. This places an upper bound on the length
		 * of an SG segment that can be handled.
		 */
		absync = true;
		bcnt = burst;
		ccnt = dma_length / (acnt * bcnt);
		if (ccnt > (SZ_64K - 1)) {
			dev_err(dev, "Exceeded max SG segment size\n");
			return -EINVAL;
		}
		cidx = acnt * bcnt;
	}

	epset->len = dma_length;

	if (direction == DMA_MEM_TO_DEV) {
		src_bidx = acnt;
		src_cidx = cidx;
		dst_bidx = 0;
		dst_cidx = 0;
		epset->addr = src_addr;
	} else if (direction == DMA_DEV_TO_MEM)  {
		src_bidx = 0;
		src_cidx = 0;
		dst_bidx = acnt;
		dst_cidx = cidx;
		epset->addr = dst_addr;
	} else if (direction == DMA_MEM_TO_MEM)  {
		src_bidx = acnt;
		src_cidx = cidx;
		dst_bidx = acnt;
		dst_cidx = cidx;
	} else {
		dev_err(dev, "%s: direction not implemented yet\n", __func__);
		return -EINVAL;
	}

	param->opt = EDMA_TCC(EDMA_CHAN_SLOT(echan->ch_num));
	/* Configure A or AB synchronized transfers */
	if (absync)
		param->opt |= SYNCDIM;

	param->src = src_addr;
	param->dst = dst_addr;

	param->src_dst_bidx = (dst_bidx << 16) | src_bidx;
	param->src_dst_cidx = (dst_cidx << 16) | src_cidx;

	param->a_b_cnt = bcnt << 16 | acnt;
	param->ccnt = ccnt;
	/*
	 * Only time when (bcntrld) auto reload is required is for
	 * A-sync case, and in this case, a requirement of reload value
	 * of SZ_64K-1 only is assured. 'link' is initially set to NULL
	 * and then later will be populated by edma_execute.
	 */
	param->link_bcntrld = 0xffffffff;
	return absync;
}

static struct dma_async_tx_descriptor *edma_prep_slave_sg(
	struct dma_chan *chan, struct scatterlist *sgl,
	unsigned int sg_len, enum dma_transfer_direction direction,
	unsigned long tx_flags, void *context)
{
	struct edma_chan *echan = to_edma_chan(chan);
	struct device *dev = chan->device->dev;
	struct edma_desc *edesc;
	dma_addr_t src_addr = 0, dst_addr = 0;
	enum dma_slave_buswidth dev_width;
	u32 burst;
	struct scatterlist *sg;
	int i, nslots, ret;

	if (unlikely(!echan || !sgl || !sg_len))
		return NULL;

	if (direction == DMA_DEV_TO_MEM) {
		src_addr = echan->cfg.src_addr;
		dev_width = echan->cfg.src_addr_width;
		burst = echan->cfg.src_maxburst;
	} else if (direction == DMA_MEM_TO_DEV) {
		dst_addr = echan->cfg.dst_addr;
		dev_width = echan->cfg.dst_addr_width;
		burst = echan->cfg.dst_maxburst;
	} else {
		dev_err(dev, "%s: bad direction: %d\n", __func__, direction);
		return NULL;
	}

	if (dev_width == DMA_SLAVE_BUSWIDTH_UNDEFINED) {
		dev_err(dev, "%s: Undefined slave buswidth\n", __func__);
		return NULL;
	}

	edesc = kzalloc(sizeof(*edesc) + sg_len * sizeof(edesc->pset[0]),
			GFP_ATOMIC);
	if (!edesc) {
		dev_err(dev, "%s: Failed to allocate a descriptor\n", __func__);
		return NULL;
	}

	edesc->pset_nr = sg_len;
	edesc->residue = 0;
	edesc->direction = direction;
	edesc->echan = echan;

	/* Allocate a PaRAM slot, if needed */
	nslots = min_t(unsigned, MAX_NR_SG, sg_len);

	for (i = 0; i < nslots; i++) {
		if (echan->slot[i] < 0) {
			echan->slot[i] =
				edma_alloc_slot(echan->ecc, EDMA_SLOT_ANY);
			if (echan->slot[i] < 0) {
				kfree(edesc);
				dev_err(dev, "%s: Failed to allocate slot\n",
					__func__);
				return NULL;
			}
		}
	}

	/* Configure PaRAM sets for each SG */
	for_each_sg(sgl, sg, sg_len, i) {
		/* Get address for each SG */
		if (direction == DMA_DEV_TO_MEM)
			dst_addr = sg_dma_address(sg);
		else
			src_addr = sg_dma_address(sg);

		ret = edma_config_pset(chan, &edesc->pset[i], src_addr,
				       dst_addr, burst, dev_width,
				       sg_dma_len(sg), direction);
		if (ret < 0) {
			kfree(edesc);
			return NULL;
		}

		edesc->absync = ret;
		edesc->residue += sg_dma_len(sg);

		/* If this is the last in a current SG set of transactions,
		   enable interrupts so that next set is processed */
		if (!((i+1) % MAX_NR_SG))
			edesc->pset[i].param.opt |= TCINTEN;

		/* If this is the last set, enable completion interrupt flag */
		if (i == sg_len - 1)
			edesc->pset[i].param.opt |= TCINTEN;
	}
	edesc->residue_stat = edesc->residue;

	return vchan_tx_prep(&echan->vchan, &edesc->vdesc, tx_flags);
}

static struct dma_async_tx_descriptor *edma_prep_dma_memcpy(
	struct dma_chan *chan, dma_addr_t dest, dma_addr_t src,
	size_t len, unsigned long tx_flags)
{
	int ret;
	struct edma_desc *edesc;
	struct device *dev = chan->device->dev;
	struct edma_chan *echan = to_edma_chan(chan);

	if (unlikely(!echan || !len))
		return NULL;

	edesc = kzalloc(sizeof(*edesc) + sizeof(edesc->pset[0]), GFP_ATOMIC);
	if (!edesc) {
		dev_dbg(dev, "Failed to allocate a descriptor\n");
		return NULL;
	}

	edesc->pset_nr = 1;

	ret = edma_config_pset(chan, &edesc->pset[0], src, dest, 1,
			       DMA_SLAVE_BUSWIDTH_4_BYTES, len, DMA_MEM_TO_MEM);
	if (ret < 0)
		return NULL;

	edesc->absync = ret;

	/*
	 * Enable intermediate transfer chaining to re-trigger channel
	 * on completion of every TR, and enable transfer-completion
	 * interrupt on completion of the whole transfer.
	 */
	edesc->pset[0].param.opt |= ITCCHEN;
	edesc->pset[0].param.opt |= TCINTEN;

	return vchan_tx_prep(&echan->vchan, &edesc->vdesc, tx_flags);
}

static struct dma_async_tx_descriptor *edma_prep_dma_cyclic(
	struct dma_chan *chan, dma_addr_t buf_addr, size_t buf_len,
	size_t period_len, enum dma_transfer_direction direction,
	unsigned long tx_flags)
{
	struct edma_chan *echan = to_edma_chan(chan);
	struct device *dev = chan->device->dev;
	struct edma_desc *edesc;
	dma_addr_t src_addr, dst_addr;
	enum dma_slave_buswidth dev_width;
	u32 burst;
	int i, ret, nslots;

	if (unlikely(!echan || !buf_len || !period_len))
		return NULL;

	if (direction == DMA_DEV_TO_MEM) {
		src_addr = echan->cfg.src_addr;
		dst_addr = buf_addr;
		dev_width = echan->cfg.src_addr_width;
		burst = echan->cfg.src_maxburst;
	} else if (direction == DMA_MEM_TO_DEV) {
		src_addr = buf_addr;
		dst_addr = echan->cfg.dst_addr;
		dev_width = echan->cfg.dst_addr_width;
		burst = echan->cfg.dst_maxburst;
	} else {
		dev_err(dev, "%s: bad direction: %d\n", __func__, direction);
		return NULL;
	}

	if (dev_width == DMA_SLAVE_BUSWIDTH_UNDEFINED) {
		dev_err(dev, "%s: Undefined slave buswidth\n", __func__);
		return NULL;
	}

	if (unlikely(buf_len % period_len)) {
		dev_err(dev, "Period should be multiple of Buffer length\n");
		return NULL;
	}

	nslots = (buf_len / period_len) + 1;

	/*
	 * Cyclic DMA users such as audio cannot tolerate delays introduced
	 * by cases where the number of periods is more than the maximum
	 * number of SGs the EDMA driver can handle at a time. For DMA types
	 * such as Slave SGs, such delays are tolerable and synchronized,
	 * but the synchronization is difficult to achieve with Cyclic and
	 * cannot be guaranteed, so we error out early.
	 */
	if (nslots > MAX_NR_SG)
		return NULL;

	edesc = kzalloc(sizeof(*edesc) + nslots * sizeof(edesc->pset[0]),
			GFP_ATOMIC);
	if (!edesc) {
		dev_err(dev, "%s: Failed to allocate a descriptor\n", __func__);
		return NULL;
	}

	edesc->cyclic = 1;
	edesc->pset_nr = nslots;
	edesc->residue = edesc->residue_stat = buf_len;
	edesc->direction = direction;
	edesc->echan = echan;

	dev_dbg(dev, "%s: channel=%d nslots=%d period_len=%zu buf_len=%zu\n",
		__func__, echan->ch_num, nslots, period_len, buf_len);

	for (i = 0; i < nslots; i++) {
		/* Allocate a PaRAM slot, if needed */
		if (echan->slot[i] < 0) {
			echan->slot[i] =
				edma_alloc_slot(echan->ecc, EDMA_SLOT_ANY);
			if (echan->slot[i] < 0) {
				kfree(edesc);
				dev_err(dev, "%s: Failed to allocate slot\n",
					__func__);
				return NULL;
			}
		}

		if (i == nslots - 1) {
			memcpy(&edesc->pset[i], &edesc->pset[0],
			       sizeof(edesc->pset[0]));
			break;
		}

		ret = edma_config_pset(chan, &edesc->pset[i], src_addr,
				       dst_addr, burst, dev_width, period_len,
				       direction);
		if (ret < 0) {
			kfree(edesc);
			return NULL;
		}

		if (direction == DMA_DEV_TO_MEM)
			dst_addr += period_len;
		else
			src_addr += period_len;

		dev_vdbg(dev, "%s: Configure period %d of buf:\n", __func__, i);
		dev_vdbg(dev,
			"\n pset[%d]:\n"
			"  chnum\t%d\n"
			"  slot\t%d\n"
			"  opt\t%08x\n"
			"  src\t%08x\n"
			"  dst\t%08x\n"
			"  abcnt\t%08x\n"
			"  ccnt\t%08x\n"
			"  bidx\t%08x\n"
			"  cidx\t%08x\n"
			"  lkrld\t%08x\n",
			i, echan->ch_num, echan->slot[i],
			edesc->pset[i].param.opt,
			edesc->pset[i].param.src,
			edesc->pset[i].param.dst,
			edesc->pset[i].param.a_b_cnt,
			edesc->pset[i].param.ccnt,
			edesc->pset[i].param.src_dst_bidx,
			edesc->pset[i].param.src_dst_cidx,
			edesc->pset[i].param.link_bcntrld);

		edesc->absync = ret;

		/*
		 * Enable period interrupt only if it is requested
		 */
		if (tx_flags & DMA_PREP_INTERRUPT)
			edesc->pset[i].param.opt |= TCINTEN;
	}

	/* Place the cyclic channel to highest priority queue */
	edma_assign_channel_eventq(echan->ecc, echan->ch_num, EVENTQ_0);

	return vchan_tx_prep(&echan->vchan, &edesc->vdesc, tx_flags);
}

static void edma_completion_handler(struct edma_chan *echan)
{
	struct edma_cc *ecc = echan->ecc;
	struct device *dev = echan->vchan.chan.device->dev;
	struct edma_desc *edesc = echan->edesc;

	if (!edesc)
		return;

	spin_lock(&echan->vchan.lock);
	if (edesc->cyclic) {
		vchan_cyclic_callback(&edesc->vdesc);
		spin_unlock(&echan->vchan.lock);
		return;
	} else if (edesc->processed == edesc->pset_nr) {
		edesc->residue = 0;
		edma_stop(ecc, echan->ch_num);
		vchan_cookie_complete(&edesc->vdesc);
		echan->edesc = NULL;

		dev_dbg(dev, "Transfer completed on channel %d\n",
			echan->ch_num);
	} else {
		dev_dbg(dev, "Sub transfer completed on channel %d\n",
			echan->ch_num);

		edma_pause(ecc, echan->ch_num);

		/* Update statistics for tx_status */
		edesc->residue -= edesc->sg_len;
		edesc->residue_stat = edesc->residue;
		edesc->processed_stat = edesc->processed;
	}
	edma_execute(echan);

	spin_unlock(&echan->vchan.lock);
}

/* eDMA interrupt handler */
static irqreturn_t dma_irq_handler(int irq, void *data)
{
	struct edma_cc *ecc = data;
	int ctlr;
	u32 sh_ier;
	u32 sh_ipr;
	u32 bank;

	ctlr = ecc->id;
	if (ctlr < 0)
		return IRQ_NONE;

	dev_vdbg(ecc->dev, "dma_irq_handler\n");

	sh_ipr = edma_shadow0_read_array(ecc, SH_IPR, 0);
	if (!sh_ipr) {
		sh_ipr = edma_shadow0_read_array(ecc, SH_IPR, 1);
		if (!sh_ipr)
			return IRQ_NONE;
		sh_ier = edma_shadow0_read_array(ecc, SH_IER, 1);
		bank = 1;
	} else {
		sh_ier = edma_shadow0_read_array(ecc, SH_IER, 0);
		bank = 0;
	}

	do {
		u32 slot;
		u32 channel;

		slot = __ffs(sh_ipr);
		sh_ipr &= ~(BIT(slot));

		if (sh_ier & BIT(slot)) {
			channel = (bank << 5) | slot;
			/* Clear the corresponding IPR bits */
			edma_shadow0_write_array(ecc, SH_ICR, bank, BIT(slot));
			edma_completion_handler(&ecc->slave_chans[channel]);
		}
	} while (sh_ipr);

	edma_shadow0_write(ecc, SH_IEVAL, 1);
	return IRQ_HANDLED;
}

static void edma_error_handler(struct edma_chan *echan)
{
	struct edma_cc *ecc = echan->ecc;
	struct device *dev = echan->vchan.chan.device->dev;
	struct edmacc_param p;

	if (!echan->edesc)
		return;

	spin_lock(&echan->vchan.lock);

	edma_read_slot(ecc, echan->slot[0], &p);
	/*
	 * Issue later based on missed flag which will be sure
	 * to happen as:
	 * (1) we finished transmitting an intermediate slot and
	 *     edma_execute is coming up.
	 * (2) or we finished current transfer and issue will
	 *     call edma_execute.
	 *
	 * Important note: issuing can be dangerous here and
	 * lead to some nasty recursion when we are in a NULL
	 * slot. So we avoid doing so and set the missed flag.
	 */
	if (p.a_b_cnt == 0 && p.ccnt == 0) {
		dev_dbg(dev, "Error on null slot, setting miss\n");
		echan->missed = 1;
	} else {
		/*
		 * The slot is already programmed but the event got
		 * missed, so its safe to issue it here.
		 */
		dev_dbg(dev, "Missed event, TRIGGERING\n");
		edma_clean_channel(ecc, echan->ch_num);
		edma_stop(ecc, echan->ch_num);
		edma_start(ecc, echan->ch_num);
		edma_trigger_channel(ecc, echan->ch_num);
	}
	spin_unlock(&echan->vchan.lock);
}

/* eDMA error interrupt handler */
static irqreturn_t dma_ccerr_handler(int irq, void *data)
{
	struct edma_cc *ecc = data;
	int i;
	int ctlr;
	unsigned int cnt = 0;

	ctlr = ecc->id;
	if (ctlr < 0)
		return IRQ_NONE;

	dev_vdbg(ecc->dev, "dma_ccerr_handler\n");

	if ((edma_read_array(ecc, EDMA_EMR, 0) == 0) &&
	    (edma_read_array(ecc, EDMA_EMR, 1) == 0) &&
	    (edma_read(ecc, EDMA_QEMR) == 0) &&
	    (edma_read(ecc, EDMA_CCERR) == 0))
		return IRQ_NONE;

	while (1) {
		int j = -1;

		if (edma_read_array(ecc, EDMA_EMR, 0))
			j = 0;
		else if (edma_read_array(ecc, EDMA_EMR, 1))
			j = 1;
		if (j >= 0) {
			dev_dbg(ecc->dev, "EMR%d %08x\n", j,
				edma_read_array(ecc, EDMA_EMR, j));
			for (i = 0; i < 32; i++) {
				int k = (j << 5) + i;

				if (edma_read_array(ecc, EDMA_EMR, j) &
							BIT(i)) {
					/* Clear the corresponding EMR bits */
					edma_write_array(ecc, EDMA_EMCR, j,
							 BIT(i));
					/* Clear any SER */
					edma_shadow0_write_array(ecc, SH_SECR,
								 j, BIT(i));
					edma_error_handler(&ecc->slave_chans[k]);
				}
			}
		} else if (edma_read(ecc, EDMA_QEMR)) {
			dev_dbg(ecc->dev, "QEMR %02x\n",
				edma_read(ecc, EDMA_QEMR));
			for (i = 0; i < 8; i++) {
				if (edma_read(ecc, EDMA_QEMR) & BIT(i)) {
					/* Clear the corresponding IPR bits */
					edma_write(ecc, EDMA_QEMCR, BIT(i));
					edma_shadow0_write(ecc, SH_QSECR,
							   BIT(i));

					/* NOTE:  not reported!! */
				}
			}
		} else if (edma_read(ecc, EDMA_CCERR)) {
			dev_dbg(ecc->dev, "CCERR %08x\n",
				edma_read(ecc, EDMA_CCERR));
			/* FIXME:  CCERR.BIT(16) ignored!  much better
			 * to just write CCERRCLR with CCERR value...
			 */
			for (i = 0; i < 8; i++) {
				if (edma_read(ecc, EDMA_CCERR) & BIT(i)) {
					/* Clear the corresponding IPR bits */
					edma_write(ecc, EDMA_CCERRCLR, BIT(i));

					/* NOTE:  not reported!! */
				}
			}
		}
		if ((edma_read_array(ecc, EDMA_EMR, 0) == 0) &&
		    (edma_read_array(ecc, EDMA_EMR, 1) == 0) &&
		    (edma_read(ecc, EDMA_QEMR) == 0) &&
		    (edma_read(ecc, EDMA_CCERR) == 0))
			break;
		cnt++;
		if (cnt > 10)
			break;
	}
	edma_write(ecc, EDMA_EEVAL, 1);
	return IRQ_HANDLED;
}

/* Alloc channel resources */
static int edma_alloc_chan_resources(struct dma_chan *chan)
{
	struct edma_chan *echan = to_edma_chan(chan);
	struct device *dev = chan->device->dev;
	int ret;
	int a_ch_num;
	LIST_HEAD(descs);

	a_ch_num = edma_alloc_channel(echan->ecc, echan->ch_num, EVENTQ_DEFAULT);

	if (a_ch_num < 0) {
		ret = -ENODEV;
		goto err_no_chan;
	}

	if (a_ch_num != echan->ch_num) {
		dev_err(dev, "failed to allocate requested channel %u:%u\n",
			EDMA_CTLR(echan->ch_num),
			EDMA_CHAN_SLOT(echan->ch_num));
		ret = -ENODEV;
		goto err_wrong_chan;
	}

	echan->alloced = true;
	echan->slot[0] = echan->ch_num;

	dev_dbg(dev, "allocated channel %d for %u:%u\n", echan->ch_num,
		EDMA_CTLR(echan->ch_num), EDMA_CHAN_SLOT(echan->ch_num));

	return 0;

err_wrong_chan:
	edma_free_channel(echan->ecc, a_ch_num);
err_no_chan:
	return ret;
}

/* Free channel resources */
static void edma_free_chan_resources(struct dma_chan *chan)
{
	struct edma_chan *echan = to_edma_chan(chan);
	int i;

	/* Terminate transfers */
	edma_stop(echan->ecc, echan->ch_num);

	vchan_free_chan_resources(&echan->vchan);

	/* Free EDMA PaRAM slots */
	for (i = 1; i < EDMA_MAX_SLOTS; i++) {
		if (echan->slot[i] >= 0) {
			edma_free_slot(echan->ecc, echan->slot[i]);
			echan->slot[i] = -1;
		}
	}

	/* Free EDMA channel */
	if (echan->alloced) {
		edma_free_channel(echan->ecc, echan->ch_num);
		echan->alloced = false;
	}

	dev_dbg(chan->device->dev, "freeing channel for %u\n", echan->ch_num);
}

/* Send pending descriptor to hardware */
static void edma_issue_pending(struct dma_chan *chan)
{
	struct edma_chan *echan = to_edma_chan(chan);
	unsigned long flags;

	spin_lock_irqsave(&echan->vchan.lock, flags);
	if (vchan_issue_pending(&echan->vchan) && !echan->edesc)
		edma_execute(echan);
	spin_unlock_irqrestore(&echan->vchan.lock, flags);
}

static u32 edma_residue(struct edma_desc *edesc)
{
	bool dst = edesc->direction == DMA_DEV_TO_MEM;
	struct edma_pset *pset = edesc->pset;
	dma_addr_t done, pos;
	int i;

	/*
	 * We always read the dst/src position from the first RamPar
	 * pset. That's the one which is active now.
	 */
	pos = edma_get_position(edesc->echan->ecc, edesc->echan->slot[0], dst);

	/*
	 * Cyclic is simple. Just subtract pset[0].addr from pos.
	 *
	 * We never update edesc->residue in the cyclic case, so we
	 * can tell the remaining room to the end of the circular
	 * buffer.
	 */
	if (edesc->cyclic) {
		done = pos - pset->addr;
		edesc->residue_stat = edesc->residue - done;
		return edesc->residue_stat;
	}

	/*
	 * For SG operation we catch up with the last processed
	 * status.
	 */
	pset += edesc->processed_stat;

	for (i = edesc->processed_stat; i < edesc->processed; i++, pset++) {
		/*
		 * If we are inside this pset address range, we know
		 * this is the active one. Get the current delta and
		 * stop walking the psets.
		 */
		if (pos >= pset->addr && pos < pset->addr + pset->len)
			return edesc->residue_stat - (pos - pset->addr);

		/* Otherwise mark it done and update residue_stat. */
		edesc->processed_stat++;
		edesc->residue_stat -= pset->len;
	}
	return edesc->residue_stat;
}

/* Check request completion status */
static enum dma_status edma_tx_status(struct dma_chan *chan,
				      dma_cookie_t cookie,
				      struct dma_tx_state *txstate)
{
	struct edma_chan *echan = to_edma_chan(chan);
	struct virt_dma_desc *vdesc;
	enum dma_status ret;
	unsigned long flags;

	ret = dma_cookie_status(chan, cookie, txstate);
	if (ret == DMA_COMPLETE || !txstate)
		return ret;

	spin_lock_irqsave(&echan->vchan.lock, flags);
	if (echan->edesc && echan->edesc->vdesc.tx.cookie == cookie)
		txstate->residue = edma_residue(echan->edesc);
	else if ((vdesc = vchan_find_desc(&echan->vchan, cookie)))
		txstate->residue = to_edma_desc(&vdesc->tx)->residue;
	spin_unlock_irqrestore(&echan->vchan.lock, flags);

	return ret;
}

static void __init edma_chan_init(struct edma_cc *ecc, struct dma_device *dma,
				  struct edma_chan *echans)
{
	int i, j;

	for (i = 0; i < ecc->num_channels; i++) {
		struct edma_chan *echan = &echans[i];
		echan->ch_num = EDMA_CTLR_CHAN(ecc->id, i);
		echan->ecc = ecc;
		echan->vchan.desc_free = edma_desc_free;

		vchan_init(&echan->vchan, dma);

		INIT_LIST_HEAD(&echan->node);
		for (j = 0; j < EDMA_MAX_SLOTS; j++)
			echan->slot[j] = -1;
	}
}

#define EDMA_DMA_BUSWIDTHS	(BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) | \
				 BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) | \
				 BIT(DMA_SLAVE_BUSWIDTH_3_BYTES) | \
				 BIT(DMA_SLAVE_BUSWIDTH_4_BYTES))

static void edma_dma_init(struct edma_cc *ecc, struct dma_device *dma,
			  struct device *dev)
{
	dma->device_prep_slave_sg = edma_prep_slave_sg;
	dma->device_prep_dma_cyclic = edma_prep_dma_cyclic;
	dma->device_prep_dma_memcpy = edma_prep_dma_memcpy;
	dma->device_alloc_chan_resources = edma_alloc_chan_resources;
	dma->device_free_chan_resources = edma_free_chan_resources;
	dma->device_issue_pending = edma_issue_pending;
	dma->device_tx_status = edma_tx_status;
	dma->device_config = edma_slave_config;
	dma->device_pause = edma_dma_pause;
	dma->device_resume = edma_dma_resume;
	dma->device_terminate_all = edma_terminate_all;

	dma->src_addr_widths = EDMA_DMA_BUSWIDTHS;
	dma->dst_addr_widths = EDMA_DMA_BUSWIDTHS;
	dma->directions = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV);
	dma->residue_granularity = DMA_RESIDUE_GRANULARITY_BURST;

	dma->dev = dev;

	/*
	 * code using dma memcpy must make sure alignment of
	 * length is at dma->copy_align boundary.
	 */
	dma->copy_align = DMAENGINE_ALIGN_4_BYTES;

	INIT_LIST_HEAD(&dma->channels);
}

static int edma_setup_from_hw(struct device *dev, struct edma_soc_info *pdata,
			      struct edma_cc *ecc)
{
	int i;
	u32 value, cccfg;
	s8 (*queue_priority_map)[2];

	/* Decode the eDMA3 configuration from CCCFG register */
	cccfg = edma_read(ecc, EDMA_CCCFG);

	value = GET_NUM_REGN(cccfg);
	ecc->num_region = BIT(value);

	value = GET_NUM_DMACH(cccfg);
	ecc->num_channels = BIT(value + 1);

	value = GET_NUM_PAENTRY(cccfg);
	ecc->num_slots = BIT(value + 4);

	value = GET_NUM_EVQUE(cccfg);
	ecc->num_tc = value + 1;

	dev_dbg(dev, "eDMA3 CC HW configuration (cccfg: 0x%08x):\n", cccfg);
	dev_dbg(dev, "num_region: %u\n", ecc->num_region);
	dev_dbg(dev, "num_channels: %u\n", ecc->num_channels);
	dev_dbg(dev, "num_slots: %u\n", ecc->num_slots);
	dev_dbg(dev, "num_tc: %u\n", ecc->num_tc);

	/* Nothing need to be done if queue priority is provided */
	if (pdata->queue_priority_mapping)
		return 0;

	/*
	 * Configure TC/queue priority as follows:
	 * Q0 - priority 0
	 * Q1 - priority 1
	 * Q2 - priority 2
	 * ...
	 * The meaning of priority numbers: 0 highest priority, 7 lowest
	 * priority. So Q0 is the highest priority queue and the last queue has
	 * the lowest priority.
	 */
	queue_priority_map = devm_kcalloc(dev, ecc->num_tc + 1, sizeof(s8),
					  GFP_KERNEL);
	if (!queue_priority_map)
		return -ENOMEM;

	for (i = 0; i < ecc->num_tc; i++) {
		queue_priority_map[i][0] = i;
		queue_priority_map[i][1] = i;
	}
	queue_priority_map[i][0] = -1;
	queue_priority_map[i][1] = -1;

	pdata->queue_priority_mapping = queue_priority_map;
	/* Default queue has the lowest priority */
	pdata->default_queue = i - 1;

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static int edma_xbar_event_map(struct device *dev, struct edma_soc_info *pdata,
			       size_t sz)
{
	const char pname[] = "ti,edma-xbar-event-map";
	struct resource res;
	void __iomem *xbar;
	s16 (*xbar_chans)[2];
	size_t nelm = sz / sizeof(s16);
	u32 shift, offset, mux;
	int ret, i;

	xbar_chans = devm_kcalloc(dev, nelm + 2, sizeof(s16), GFP_KERNEL);
	if (!xbar_chans)
		return -ENOMEM;

	ret = of_address_to_resource(dev->of_node, 1, &res);
	if (ret)
		return -ENOMEM;

	xbar = devm_ioremap(dev, res.start, resource_size(&res));
	if (!xbar)
		return -ENOMEM;

	ret = of_property_read_u16_array(dev->of_node, pname, (u16 *)xbar_chans,
					 nelm);
	if (ret)
		return -EIO;

	/* Invalidate last entry for the other user of this mess */
	nelm >>= 1;
	xbar_chans[nelm][0] = -1;
	xbar_chans[nelm][1] = -1;

	for (i = 0; i < nelm; i++) {
		shift = (xbar_chans[i][1] & 0x03) << 3;
		offset = xbar_chans[i][1] & 0xfffffffc;
		mux = readl(xbar + offset);
		mux &= ~(0xff << shift);
		mux |= xbar_chans[i][0] << shift;
		writel(mux, (xbar + offset));
	}

	pdata->xbar_chans = (const s16 (*)[2]) xbar_chans;
	return 0;
}

static int edma_of_parse_dt(struct device *dev, struct edma_soc_info *pdata)
{
	int ret = 0;
	struct property *prop;
	size_t sz;
	struct edma_rsv_info *rsv_info;

	rsv_info = devm_kzalloc(dev, sizeof(struct edma_rsv_info), GFP_KERNEL);
	if (!rsv_info)
		return -ENOMEM;
	pdata->rsv = rsv_info;

	prop = of_find_property(dev->of_node, "ti,edma-xbar-event-map", &sz);
	if (prop)
		ret = edma_xbar_event_map(dev, pdata, sz);

	return ret;
}

static struct edma_soc_info *edma_setup_info_from_dt(struct device *dev)
{
	struct edma_soc_info *info;
	int ret;

	info = devm_kzalloc(dev, sizeof(struct edma_soc_info), GFP_KERNEL);
	if (!info)
		return ERR_PTR(-ENOMEM);

	ret = edma_of_parse_dt(dev, info);
	if (ret)
		return ERR_PTR(ret);

	return info;
}
#else
static struct edma_soc_info *edma_setup_info_from_dt(struct device *dev)
{
	return ERR_PTR(-EINVAL);
}
#endif

static int edma_probe(struct platform_device *pdev)
{
	struct edma_soc_info	*info = pdev->dev.platform_data;
	s8			(*queue_priority_mapping)[2];
	int			i, off, ln;
	const s16		(*rsv_chans)[2];
	const s16		(*rsv_slots)[2];
	const s16		(*xbar_chans)[2];
	int			irq;
	char			*irq_name;
	struct resource		*mem;
	struct device_node	*node = pdev->dev.of_node;
	struct device		*dev = &pdev->dev;
	struct edma_cc		*ecc;
	int ret;

	if (node) {
		info = edma_setup_info_from_dt(dev);
		if (IS_ERR(info)) {
			dev_err(dev, "failed to get DT data\n");
			return PTR_ERR(info);
		}
	}

	if (!info)
		return -ENODEV;

	pm_runtime_enable(dev);
	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		dev_err(dev, "pm_runtime_get_sync() failed\n");
		return ret;
	}

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	ecc = devm_kzalloc(dev, sizeof(*ecc), GFP_KERNEL);
	if (!ecc) {
		dev_err(dev, "Can't allocate controller\n");
		return -ENOMEM;
	}

	ecc->dev = dev;
	ecc->id = pdev->id;
	/* When booting with DT the pdev->id is -1 */
	if (ecc->id < 0)
		ecc->id = 0;

	mem = platform_get_resource_byname(pdev, IORESOURCE_MEM, "edma3_cc");
	if (!mem) {
		dev_dbg(dev, "mem resource not found, using index 0\n");
		mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (!mem) {
			dev_err(dev, "no mem resource?\n");
			return -ENODEV;
		}
	}
	ecc->base = devm_ioremap_resource(dev, mem);
	if (IS_ERR(ecc->base))
		return PTR_ERR(ecc->base);

	platform_set_drvdata(pdev, ecc);

	/* Get eDMA3 configuration from IP */
	ret = edma_setup_from_hw(dev, info, ecc);
	if (ret)
		return ret;

	/* Allocate memory based on the information we got from the IP */
	ecc->slave_chans = devm_kcalloc(dev, ecc->num_channels,
					sizeof(*ecc->slave_chans), GFP_KERNEL);
	if (!ecc->slave_chans)
		return -ENOMEM;

	ecc->edma_unused = devm_kcalloc(dev, BITS_TO_LONGS(ecc->num_channels),
					sizeof(unsigned long), GFP_KERNEL);
	if (!ecc->edma_unused)
		return -ENOMEM;

	ecc->edma_inuse = devm_kcalloc(dev, BITS_TO_LONGS(ecc->num_slots),
				       sizeof(unsigned long), GFP_KERNEL);
	if (!ecc->edma_inuse)
		return -ENOMEM;

	ecc->default_queue = info->default_queue;

	for (i = 0; i < ecc->num_slots; i++)
		edma_write_slot(ecc, i, &dummy_paramset);

	/* Mark all channels as unused */
	memset(ecc->edma_unused, 0xff, sizeof(ecc->edma_unused));

	if (info->rsv) {
		/* Clear the reserved channels in unused list */
		rsv_chans = info->rsv->rsv_chans;
		if (rsv_chans) {
			for (i = 0; rsv_chans[i][0] != -1; i++) {
				off = rsv_chans[i][0];
				ln = rsv_chans[i][1];
				clear_bits(off, ln, ecc->edma_unused);
			}
		}

		/* Set the reserved slots in inuse list */
		rsv_slots = info->rsv->rsv_slots;
		if (rsv_slots) {
			for (i = 0; rsv_slots[i][0] != -1; i++) {
				off = rsv_slots[i][0];
				ln = rsv_slots[i][1];
				set_bits(off, ln, ecc->edma_inuse);
			}
		}
	}

	/* Clear the xbar mapped channels in unused list */
	xbar_chans = info->xbar_chans;
	if (xbar_chans) {
		for (i = 0; xbar_chans[i][1] != -1; i++) {
			off = xbar_chans[i][1];
			clear_bits(off, 1, ecc->edma_unused);
		}
	}

	irq = platform_get_irq_byname(pdev, "edma3_ccint");
	if (irq < 0 && node)
		irq = irq_of_parse_and_map(node, 0);

	if (irq >= 0) {
		irq_name = devm_kasprintf(dev, GFP_KERNEL, "%s_ccint",
					  dev_name(dev));
		ret = devm_request_irq(dev, irq, dma_irq_handler, 0, irq_name,
				       ecc);
		if (ret) {
			dev_err(dev, "CCINT (%d) failed --> %d\n", irq, ret);
			return ret;
		}
	}

	irq = platform_get_irq_byname(pdev, "edma3_ccerrint");
	if (irq < 0 && node)
		irq = irq_of_parse_and_map(node, 2);

	if (irq >= 0) {
		irq_name = devm_kasprintf(dev, GFP_KERNEL, "%s_ccerrint",
					  dev_name(dev));
		ret = devm_request_irq(dev, irq, dma_ccerr_handler, 0, irq_name,
				       ecc);
		if (ret) {
			dev_err(dev, "CCERRINT (%d) failed --> %d\n", irq, ret);
			return ret;
		}
	}

	for (i = 0; i < ecc->num_channels; i++)
		edma_map_dmach_to_queue(ecc, i, info->default_queue);

	queue_priority_mapping = info->queue_priority_mapping;

	/* Event queue priority mapping */
	for (i = 0; queue_priority_mapping[i][0] != -1; i++)
		edma_assign_priority_to_queue(ecc, queue_priority_mapping[i][0],
					      queue_priority_mapping[i][1]);

	/* Map the channel to param entry if channel mapping logic exist */
	if (edma_read(ecc, EDMA_CCCFG) & CHMAP_EXIST)
		edma_direct_dmach_to_param_mapping(ecc);

	for (i = 0; i < ecc->num_region; i++) {
		edma_write_array2(ecc, EDMA_DRAE, i, 0, 0x0);
		edma_write_array2(ecc, EDMA_DRAE, i, 1, 0x0);
		edma_write_array(ecc, EDMA_QRAE, i, 0x0);
	}
	ecc->info = info;

	ecc->dummy_slot = edma_alloc_slot(ecc, EDMA_SLOT_ANY);
	if (ecc->dummy_slot < 0) {
		dev_err(dev, "Can't allocate PaRAM dummy slot\n");
		return ecc->dummy_slot;
	}

	dma_cap_zero(ecc->dma_slave.cap_mask);
	dma_cap_set(DMA_SLAVE, ecc->dma_slave.cap_mask);
	dma_cap_set(DMA_CYCLIC, ecc->dma_slave.cap_mask);
	dma_cap_set(DMA_MEMCPY, ecc->dma_slave.cap_mask);

	edma_dma_init(ecc, &ecc->dma_slave, dev);

	edma_chan_init(ecc, &ecc->dma_slave, ecc->slave_chans);

	ret = dma_async_device_register(&ecc->dma_slave);
	if (ret)
		goto err_reg1;

	if (node)
		of_dma_controller_register(node, of_dma_xlate_by_chan_id,
					   &ecc->dma_slave);

	dev_info(dev, "TI EDMA DMA engine driver\n");

	return 0;

err_reg1:
	edma_free_slot(ecc, ecc->dummy_slot);
	return ret;
}

static int edma_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct edma_cc *ecc = dev_get_drvdata(dev);

	if (dev->of_node)
		of_dma_controller_free(dev->of_node);
	dma_async_device_unregister(&ecc->dma_slave);
	edma_free_slot(ecc, ecc->dummy_slot);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int edma_pm_resume(struct device *dev)
{
	struct edma_cc *ecc = dev_get_drvdata(dev);
	int i;
	s8 (*queue_priority_mapping)[2];

	queue_priority_mapping = ecc->info->queue_priority_mapping;

	/* Event queue priority mapping */
	for (i = 0; queue_priority_mapping[i][0] != -1; i++)
		edma_assign_priority_to_queue(ecc, queue_priority_mapping[i][0],
					      queue_priority_mapping[i][1]);

	/* Map the channel to param entry if channel mapping logic */
	if (edma_read(ecc, EDMA_CCCFG) & CHMAP_EXIST)
		edma_direct_dmach_to_param_mapping(ecc);

	for (i = 0; i < ecc->num_channels; i++) {
		if (test_bit(i, ecc->edma_inuse)) {
			/* ensure access through shadow region 0 */
			edma_or_array2(ecc, EDMA_DRAE, 0, i >> 5,
				       BIT(i & 0x1f));

			edma_setup_interrupt(ecc, EDMA_CTLR_CHAN(ecc->id, i),
					     true);
		}
	}

	return 0;
}
#endif

static const struct dev_pm_ops edma_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(NULL, edma_pm_resume)
};

static struct platform_driver edma_driver = {
	.probe		= edma_probe,
	.remove		= edma_remove,
	.driver = {
		.name	= "edma",
		.pm	= &edma_pm_ops,
		.of_match_table = edma_of_ids,
	},
};

bool edma_filter_fn(struct dma_chan *chan, void *param)
{
	if (chan->device->dev->driver == &edma_driver.driver) {
		struct edma_chan *echan = to_edma_chan(chan);
		unsigned ch_req = *(unsigned *)param;
		return ch_req == echan->ch_num;
	}
	return false;
}
EXPORT_SYMBOL(edma_filter_fn);

static int edma_init(void)
{
	return platform_driver_register(&edma_driver);
}
subsys_initcall(edma_init);

static void __exit edma_exit(void)
{
	platform_driver_unregister(&edma_driver);
}
module_exit(edma_exit);

MODULE_AUTHOR("Matt Porter <matt.porter@linaro.org>");
MODULE_DESCRIPTION("TI EDMA DMA engine driver");
MODULE_LICENSE("GPL v2");
