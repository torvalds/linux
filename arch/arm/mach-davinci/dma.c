/*
 * EDMA3 support for DaVinci
 *
 * Copyright (C) 2006-2009 Texas Instruments.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/compiler.h>
#include <linux/io.h>

#include <mach/cputype.h>
#include <mach/memory.h>
#include <mach/hardware.h>
#include <mach/irqs.h>
#include <mach/edma.h>
#include <mach/mux.h>


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
#define SH_ER		0x00	/* 64 bits */
#define SH_ECR		0x08	/* 64 bits */
#define SH_ESR		0x10	/* 64 bits */
#define SH_CER		0x18	/* 64 bits */
#define SH_EER		0x20	/* 64 bits */
#define SH_EECR		0x28	/* 64 bits */
#define SH_EESR		0x30	/* 64 bits */
#define SH_SER		0x38	/* 64 bits */
#define SH_SECR		0x40	/* 64 bits */
#define SH_IER		0x50	/* 64 bits */
#define SH_IECR		0x58	/* 64 bits */
#define SH_IESR		0x60	/* 64 bits */
#define SH_IPR		0x68	/* 64 bits */
#define SH_ICR		0x70	/* 64 bits */
#define SH_IEVAL	0x78
#define SH_QER		0x80
#define SH_QEER		0x84
#define SH_QEECR	0x88
#define SH_QEESR	0x8c
#define SH_QSER		0x90
#define SH_QSECR	0x94
#define SH_SIZE		0x200

/* Offsets for EDMA CC global registers */
#define EDMA_REV	0x0000
#define EDMA_CCCFG	0x0004
#define EDMA_QCHMAP	0x0200	/* 8 registers */
#define EDMA_DMAQNUM	0x0240	/* 8 registers (4 on OMAP-L1xx) */
#define EDMA_QDMAQNUM	0x0260
#define EDMA_QUETCMAP	0x0280
#define EDMA_QUEPRI	0x0284
#define EDMA_EMR	0x0300	/* 64 bits */
#define EDMA_EMCR	0x0308	/* 64 bits */
#define EDMA_QEMR	0x0310
#define EDMA_QEMCR	0x0314
#define EDMA_CCERR	0x0318
#define EDMA_CCERRCLR	0x031c
#define EDMA_EEVAL	0x0320
#define EDMA_DRAE	0x0340	/* 4 x 64 bits*/
#define EDMA_QRAE	0x0380	/* 4 registers */
#define EDMA_QUEEVTENTRY	0x0400	/* 2 x 16 registers */
#define EDMA_QSTAT	0x0600	/* 2 registers */
#define EDMA_QWMTHRA	0x0620
#define EDMA_QWMTHRB	0x0624
#define EDMA_CCSTAT	0x0640

#define EDMA_M		0x1000	/* global channel registers */
#define EDMA_ECR	0x1008
#define EDMA_ECRH	0x100C
#define EDMA_SHADOW0	0x2000	/* 4 regions shadowing global channels */
#define EDMA_PARM	0x4000	/* 128 param entries */

#define DAVINCI_DMA_3PCC_BASE	0x01C00000

#define PARM_OFFSET(param_no)	(EDMA_PARM + ((param_no) << 5))

#define EDMA_MAX_DMACH           64
#define EDMA_MAX_PARAMENTRY     512
#define EDMA_MAX_EVQUE            2	/* FIXME too small */


/*****************************************************************************/

static void __iomem *edmacc_regs_base;

static inline unsigned int edma_read(int offset)
{
	return (unsigned int)__raw_readl(edmacc_regs_base + offset);
}

static inline void edma_write(int offset, int val)
{
	__raw_writel(val, edmacc_regs_base + offset);
}
static inline void edma_modify(int offset, unsigned and, unsigned or)
{
	unsigned val = edma_read(offset);
	val &= and;
	val |= or;
	edma_write(offset, val);
}
static inline void edma_and(int offset, unsigned and)
{
	unsigned val = edma_read(offset);
	val &= and;
	edma_write(offset, val);
}
static inline void edma_or(int offset, unsigned or)
{
	unsigned val = edma_read(offset);
	val |= or;
	edma_write(offset, val);
}
static inline unsigned int edma_read_array(int offset, int i)
{
	return edma_read(offset + (i << 2));
}
static inline void edma_write_array(int offset, int i, unsigned val)
{
	edma_write(offset + (i << 2), val);
}
static inline void edma_modify_array(int offset, int i,
		unsigned and, unsigned or)
{
	edma_modify(offset + (i << 2), and, or);
}
static inline void edma_or_array(int offset, int i, unsigned or)
{
	edma_or(offset + (i << 2), or);
}
static inline void edma_or_array2(int offset, int i, int j, unsigned or)
{
	edma_or(offset + ((i*2 + j) << 2), or);
}
static inline void edma_write_array2(int offset, int i, int j, unsigned val)
{
	edma_write(offset + ((i*2 + j) << 2), val);
}
static inline unsigned int edma_shadow0_read(int offset)
{
	return edma_read(EDMA_SHADOW0 + offset);
}
static inline unsigned int edma_shadow0_read_array(int offset, int i)
{
	return edma_read(EDMA_SHADOW0 + offset + (i << 2));
}
static inline void edma_shadow0_write(int offset, unsigned val)
{
	edma_write(EDMA_SHADOW0 + offset, val);
}
static inline void edma_shadow0_write_array(int offset, int i, unsigned val)
{
	edma_write(EDMA_SHADOW0 + offset + (i << 2), val);
}
static inline unsigned int edma_parm_read(int offset, int param_no)
{
	return edma_read(EDMA_PARM + offset + (param_no << 5));
}
static inline void edma_parm_write(int offset, int param_no, unsigned val)
{
	edma_write(EDMA_PARM + offset + (param_no << 5), val);
}
static inline void edma_parm_modify(int offset, int param_no,
		unsigned and, unsigned or)
{
	edma_modify(EDMA_PARM + offset + (param_no << 5), and, or);
}
static inline void edma_parm_and(int offset, int param_no, unsigned and)
{
	edma_and(EDMA_PARM + offset + (param_no << 5), and);
}
static inline void edma_parm_or(int offset, int param_no, unsigned or)
{
	edma_or(EDMA_PARM + offset + (param_no << 5), or);
}

/*****************************************************************************/

/* actual number of DMA channels and slots on this silicon */
static unsigned num_channels;
static unsigned num_slots;

static struct dma_interrupt_data {
	void (*callback)(unsigned channel, unsigned short ch_status,
			 void *data);
	void *data;
} intr_data[EDMA_MAX_DMACH];

/* The edma_inuse bit for each PaRAM slot is clear unless the
 * channel is in use ... by ARM or DSP, for QDMA, or whatever.
 */
static DECLARE_BITMAP(edma_inuse, EDMA_MAX_PARAMENTRY);

/* The edma_noevent bit for each channel is clear unless
 * it doesn't trigger DMA events on this platform.  It uses a
 * bit of SOC-specific initialization code.
 */
static DECLARE_BITMAP(edma_noevent, EDMA_MAX_DMACH);

/* dummy param set used to (re)initialize parameter RAM slots */
static const struct edmacc_param dummy_paramset = {
	.link_bcntrld = 0xffff,
	.ccnt = 1,
};

static const int __initconst
queue_tc_mapping[EDMA_MAX_EVQUE + 1][2] = {
/* {event queue no, TC no} */
	{0, 0},
	{1, 1},
	{-1, -1}
};

static const int __initconst
queue_priority_mapping[EDMA_MAX_EVQUE + 1][2] = {
	/* {event queue no, Priority} */
	{0, 3},
	{1, 7},
	{-1, -1}
};

/*****************************************************************************/

static void map_dmach_queue(unsigned ch_no, enum dma_event_q queue_no)
{
	int bit = (ch_no & 0x7) * 4;

	/* default to low priority queue */
	if (queue_no == EVENTQ_DEFAULT)
		queue_no = EVENTQ_1;

	queue_no &= 7;
	edma_modify_array(EDMA_DMAQNUM, (ch_no >> 3),
			~(0x7 << bit), queue_no << bit);
}

static void __init map_queue_tc(int queue_no, int tc_no)
{
	int bit = queue_no * 4;
	edma_modify(EDMA_QUETCMAP, ~(0x7 << bit), ((tc_no & 0x7) << bit));
}

static void __init assign_priority_to_queue(int queue_no, int priority)
{
	int bit = queue_no * 4;
	edma_modify(EDMA_QUEPRI, ~(0x7 << bit), ((priority & 0x7) << bit));
}

static inline void
setup_dma_interrupt(unsigned lch,
	void (*callback)(unsigned channel, u16 ch_status, void *data),
	void *data)
{
	if (!callback) {
		edma_shadow0_write_array(SH_IECR, lch >> 5,
				(1 << (lch & 0x1f)));
	}

	intr_data[lch].callback = callback;
	intr_data[lch].data = data;

	if (callback) {
		edma_shadow0_write_array(SH_ICR, lch >> 5,
				(1 << (lch & 0x1f)));
		edma_shadow0_write_array(SH_IESR, lch >> 5,
				(1 << (lch & 0x1f)));
	}
}

/******************************************************************************
 *
 * DMA interrupt handler
 *
 *****************************************************************************/
static irqreturn_t dma_irq_handler(int irq, void *data)
{
	int i;
	unsigned int cnt = 0;

	dev_dbg(data, "dma_irq_handler\n");

	if ((edma_shadow0_read_array(SH_IPR, 0) == 0)
	    && (edma_shadow0_read_array(SH_IPR, 1) == 0))
		return IRQ_NONE;

	while (1) {
		int j;
		if (edma_shadow0_read_array(SH_IPR, 0))
			j = 0;
		else if (edma_shadow0_read_array(SH_IPR, 1))
			j = 1;
		else
			break;
		dev_dbg(data, "IPR%d %08x\n", j,
				edma_shadow0_read_array(SH_IPR, j));
		for (i = 0; i < 32; i++) {
			int k = (j << 5) + i;
			if (edma_shadow0_read_array(SH_IPR, j) & (1 << i)) {
				/* Clear the corresponding IPR bits */
				edma_shadow0_write_array(SH_ICR, j, (1 << i));
				if (intr_data[k].callback) {
					intr_data[k].callback(k, DMA_COMPLETE,
						intr_data[k].data);
				}
			}
		}
		cnt++;
		if (cnt > 10)
			break;
	}
	edma_shadow0_write(SH_IEVAL, 1);
	return IRQ_HANDLED;
}

/******************************************************************************
 *
 * DMA error interrupt handler
 *
 *****************************************************************************/
static irqreturn_t dma_ccerr_handler(int irq, void *data)
{
	int i;
	unsigned int cnt = 0;

	dev_dbg(data, "dma_ccerr_handler\n");

	if ((edma_read_array(EDMA_EMR, 0) == 0) &&
	    (edma_read_array(EDMA_EMR, 1) == 0) &&
	    (edma_read(EDMA_QEMR) == 0) && (edma_read(EDMA_CCERR) == 0))
		return IRQ_NONE;

	while (1) {
		int j = -1;
		if (edma_read_array(EDMA_EMR, 0))
			j = 0;
		else if (edma_read_array(EDMA_EMR, 1))
			j = 1;
		if (j >= 0) {
			dev_dbg(data, "EMR%d %08x\n", j,
					edma_read_array(EDMA_EMR, j));
			for (i = 0; i < 32; i++) {
				int k = (j << 5) + i;
				if (edma_read_array(EDMA_EMR, j) & (1 << i)) {
					/* Clear the corresponding EMR bits */
					edma_write_array(EDMA_EMCR, j, 1 << i);
					/* Clear any SER */
					edma_shadow0_write_array(SH_SECR, j,
							(1 << i));
					if (intr_data[k].callback) {
						intr_data[k].callback(k,
								DMA_CC_ERROR,
								intr_data
								[k].data);
					}
				}
			}
		} else if (edma_read(EDMA_QEMR)) {
			dev_dbg(data, "QEMR %02x\n",
				edma_read(EDMA_QEMR));
			for (i = 0; i < 8; i++) {
				if (edma_read(EDMA_QEMR) & (1 << i)) {
					/* Clear the corresponding IPR bits */
					edma_write(EDMA_QEMCR, 1 << i);
					edma_shadow0_write(SH_QSECR, (1 << i));

					/* NOTE:  not reported!! */
				}
			}
		} else if (edma_read(EDMA_CCERR)) {
			dev_dbg(data, "CCERR %08x\n",
				edma_read(EDMA_CCERR));
			/* FIXME:  CCERR.BIT(16) ignored!  much better
			 * to just write CCERRCLR with CCERR value...
			 */
			for (i = 0; i < 8; i++) {
				if (edma_read(EDMA_CCERR) & (1 << i)) {
					/* Clear the corresponding IPR bits */
					edma_write(EDMA_CCERRCLR, 1 << i);

					/* NOTE:  not reported!! */
				}
			}
		}
		if ((edma_read_array(EDMA_EMR, 0) == 0)
		    && (edma_read_array(EDMA_EMR, 1) == 0)
		    && (edma_read(EDMA_QEMR) == 0)
		    && (edma_read(EDMA_CCERR) == 0)) {
			break;
		}
		cnt++;
		if (cnt > 10)
			break;
	}
	edma_write(EDMA_EEVAL, 1);
	return IRQ_HANDLED;
}

/******************************************************************************
 *
 * Transfer controller error interrupt handlers
 *
 *****************************************************************************/

#define tc_errs_handled	false	/* disabled as long as they're NOPs */

static irqreturn_t dma_tc0err_handler(int irq, void *data)
{
	dev_dbg(data, "dma_tc0err_handler\n");
	return IRQ_HANDLED;
}

static irqreturn_t dma_tc1err_handler(int irq, void *data)
{
	dev_dbg(data, "dma_tc1err_handler\n");
	return IRQ_HANDLED;
}

/*-----------------------------------------------------------------------*/

/* Resource alloc/free:  dma channels, parameter RAM slots */

/**
 * edma_alloc_channel - allocate DMA channel and paired parameter RAM
 * @channel: specific channel to allocate; negative for "any unmapped channel"
 * @callback: optional; to be issued on DMA completion or errors
 * @data: passed to callback
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
int edma_alloc_channel(int channel,
		void (*callback)(unsigned channel, u16 ch_status, void *data),
		void *data,
		enum dma_event_q eventq_no)
{
	if (channel < 0) {
		channel = 0;
		for (;;) {
			channel = find_next_bit(edma_noevent,
					num_channels, channel);
			if (channel == num_channels)
				return -ENOMEM;
			if (!test_and_set_bit(channel, edma_inuse))
				break;
			channel++;
		}
	} else if (channel >= num_channels) {
		return -EINVAL;
	} else if (test_and_set_bit(channel, edma_inuse)) {
		return -EBUSY;
	}

	/* ensure access through shadow region 0 */
	edma_or_array2(EDMA_DRAE, 0, channel >> 5, 1 << (channel & 0x1f));

	/* ensure no events are pending */
	edma_stop(channel);
	memcpy_toio(edmacc_regs_base + PARM_OFFSET(channel),
			&dummy_paramset, PARM_SIZE);

	if (callback)
		setup_dma_interrupt(channel, callback, data);

	map_dmach_queue(channel, eventq_no);

	return channel;
}
EXPORT_SYMBOL(edma_alloc_channel);


/**
 * edma_free_channel - deallocate DMA channel
 * @channel: dma channel returned from edma_alloc_channel()
 *
 * This deallocates the DMA channel and associated parameter RAM slot
 * allocated by edma_alloc_channel().
 *
 * Callers are responsible for ensuring the channel is inactive, and
 * will not be reactivated by linking, chaining, or software calls to
 * edma_start().
 */
void edma_free_channel(unsigned channel)
{
	if (channel >= num_channels)
		return;

	setup_dma_interrupt(channel, NULL, NULL);
	/* REVISIT should probably take out of shadow region 0 */

	memcpy_toio(edmacc_regs_base + PARM_OFFSET(channel),
			&dummy_paramset, PARM_SIZE);
	clear_bit(channel, edma_inuse);
}
EXPORT_SYMBOL(edma_free_channel);

/**
 * edma_alloc_slot - allocate DMA parameter RAM
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
int edma_alloc_slot(int slot)
{
	if (slot < 0) {
		slot = num_channels;
		for (;;) {
			slot = find_next_zero_bit(edma_inuse,
					num_slots, slot);
			if (slot == num_slots)
				return -ENOMEM;
			if (!test_and_set_bit(slot, edma_inuse))
				break;
		}
	} else if (slot < num_channels || slot >= num_slots) {
		return -EINVAL;
	} else if (test_and_set_bit(slot, edma_inuse)) {
		return -EBUSY;
	}

	memcpy_toio(edmacc_regs_base + PARM_OFFSET(slot),
			&dummy_paramset, PARM_SIZE);

	return slot;
}
EXPORT_SYMBOL(edma_alloc_slot);

/**
 * edma_free_slot - deallocate DMA parameter RAM
 * @slot: parameter RAM slot returned from edma_alloc_slot()
 *
 * This deallocates the parameter RAM slot allocated by edma_alloc_slot().
 * Callers are responsible for ensuring the slot is inactive, and will
 * not be activated.
 */
void edma_free_slot(unsigned slot)
{
	if (slot < num_channels || slot >= num_slots)
		return;

	memcpy_toio(edmacc_regs_base + PARM_OFFSET(slot),
			&dummy_paramset, PARM_SIZE);
	clear_bit(slot, edma_inuse);
}
EXPORT_SYMBOL(edma_free_slot);

/*-----------------------------------------------------------------------*/

/* Parameter RAM operations (i) -- read/write partial slots */

/**
 * edma_set_src - set initial DMA source address in parameter RAM slot
 * @slot: parameter RAM slot being configured
 * @src_port: physical address of source (memory, controller FIFO, etc)
 * @addressMode: INCR, except in very rare cases
 * @fifoWidth: ignored unless @addressMode is FIFO, else specifies the
 *	width to use when addressing the fifo (e.g. W8BIT, W32BIT)
 *
 * Note that the source address is modified during the DMA transfer
 * according to edma_set_src_index().
 */
void edma_set_src(unsigned slot, dma_addr_t src_port,
				enum address_mode mode, enum fifo_width width)
{
	if (slot < num_slots) {
		unsigned int i = edma_parm_read(PARM_OPT, slot);

		if (mode) {
			/* set SAM and program FWID */
			i = (i & ~(EDMA_FWID)) | (SAM | ((width & 0x7) << 8));
		} else {
			/* clear SAM */
			i &= ~SAM;
		}
		edma_parm_write(PARM_OPT, slot, i);

		/* set the source port address
		   in source register of param structure */
		edma_parm_write(PARM_SRC, slot, src_port);
	}
}
EXPORT_SYMBOL(edma_set_src);

/**
 * edma_set_dest - set initial DMA destination address in parameter RAM slot
 * @slot: parameter RAM slot being configured
 * @dest_port: physical address of destination (memory, controller FIFO, etc)
 * @addressMode: INCR, except in very rare cases
 * @fifoWidth: ignored unless @addressMode is FIFO, else specifies the
 *	width to use when addressing the fifo (e.g. W8BIT, W32BIT)
 *
 * Note that the destination address is modified during the DMA transfer
 * according to edma_set_dest_index().
 */
void edma_set_dest(unsigned slot, dma_addr_t dest_port,
				 enum address_mode mode, enum fifo_width width)
{
	if (slot < num_slots) {
		unsigned int i = edma_parm_read(PARM_OPT, slot);

		if (mode) {
			/* set DAM and program FWID */
			i = (i & ~(EDMA_FWID)) | (DAM | ((width & 0x7) << 8));
		} else {
			/* clear DAM */
			i &= ~DAM;
		}
		edma_parm_write(PARM_OPT, slot, i);
		/* set the destination port address
		   in dest register of param structure */
		edma_parm_write(PARM_DST, slot, dest_port);
	}
}
EXPORT_SYMBOL(edma_set_dest);

/**
 * edma_get_position - returns the current transfer points
 * @slot: parameter RAM slot being examined
 * @src: pointer to source port position
 * @dst: pointer to destination port position
 *
 * Returns current source and destination addresses for a particular
 * parameter RAM slot.  Its channel should not be active when this is called.
 */
void edma_get_position(unsigned slot, dma_addr_t *src, dma_addr_t *dst)
{
	struct edmacc_param temp;

	edma_read_slot(slot, &temp);
	if (src != NULL)
		*src = temp.src;
	if (dst != NULL)
		*dst = temp.dst;
}
EXPORT_SYMBOL(edma_get_position);

/**
 * edma_set_src_index - configure DMA source address indexing
 * @slot: parameter RAM slot being configured
 * @src_bidx: byte offset between source arrays in a frame
 * @src_cidx: byte offset between source frames in a block
 *
 * Offsets are specified to support either contiguous or discontiguous
 * memory transfers, or repeated access to a hardware register, as needed.
 * When accessing hardware registers, both offsets are normally zero.
 */
void edma_set_src_index(unsigned slot, s16 src_bidx, s16 src_cidx)
{
	if (slot < num_slots) {
		edma_parm_modify(PARM_SRC_DST_BIDX, slot,
				0xffff0000, src_bidx);
		edma_parm_modify(PARM_SRC_DST_CIDX, slot,
				0xffff0000, src_cidx);
	}
}
EXPORT_SYMBOL(edma_set_src_index);

/**
 * edma_set_dest_index - configure DMA destination address indexing
 * @slot: parameter RAM slot being configured
 * @dest_bidx: byte offset between destination arrays in a frame
 * @dest_cidx: byte offset between destination frames in a block
 *
 * Offsets are specified to support either contiguous or discontiguous
 * memory transfers, or repeated access to a hardware register, as needed.
 * When accessing hardware registers, both offsets are normally zero.
 */
void edma_set_dest_index(unsigned slot, s16 dest_bidx, s16 dest_cidx)
{
	if (slot < num_slots) {
		edma_parm_modify(PARM_SRC_DST_BIDX, slot,
				0x0000ffff, dest_bidx << 16);
		edma_parm_modify(PARM_SRC_DST_CIDX, slot,
				0x0000ffff, dest_cidx << 16);
	}
}
EXPORT_SYMBOL(edma_set_dest_index);

/**
 * edma_set_transfer_params - configure DMA transfer parameters
 * @slot: parameter RAM slot being configured
 * @acnt: how many bytes per array (at least one)
 * @bcnt: how many arrays per frame (at least one)
 * @ccnt: how many frames per block (at least one)
 * @bcnt_rld: used only for A-Synchronized transfers; this specifies
 *	the value to reload into bcnt when it decrements to zero
 * @sync_mode: ASYNC or ABSYNC
 *
 * See the EDMA3 documentation to understand how to configure and link
 * transfers using the fields in PaRAM slots.  If you are not doing it
 * all at once with edma_write_slot(), you will use this routine
 * plus two calls each for source and destination, setting the initial
 * address and saying how to index that address.
 *
 * An example of an A-Synchronized transfer is a serial link using a
 * single word shift register.  In that case, @acnt would be equal to
 * that word size; the serial controller issues a DMA synchronization
 * event to transfer each word, and memory access by the DMA transfer
 * controller will be word-at-a-time.
 *
 * An example of an AB-Synchronized transfer is a device using a FIFO.
 * In that case, @acnt equals the FIFO width and @bcnt equals its depth.
 * The controller with the FIFO issues DMA synchronization events when
 * the FIFO threshold is reached, and the DMA transfer controller will
 * transfer one frame to (or from) the FIFO.  It will probably use
 * efficient burst modes to access memory.
 */
void edma_set_transfer_params(unsigned slot,
		u16 acnt, u16 bcnt, u16 ccnt,
		u16 bcnt_rld, enum sync_dimension sync_mode)
{
	if (slot < num_slots) {
		edma_parm_modify(PARM_LINK_BCNTRLD, slot,
				0x0000ffff, bcnt_rld << 16);
		if (sync_mode == ASYNC)
			edma_parm_and(PARM_OPT, slot, ~SYNCDIM);
		else
			edma_parm_or(PARM_OPT, slot, SYNCDIM);
		/* Set the acount, bcount, ccount registers */
		edma_parm_write(PARM_A_B_CNT, slot, (bcnt << 16) | acnt);
		edma_parm_write(PARM_CCNT, slot, ccnt);
	}
}
EXPORT_SYMBOL(edma_set_transfer_params);

/**
 * edma_link - link one parameter RAM slot to another
 * @from: parameter RAM slot originating the link
 * @to: parameter RAM slot which is the link target
 *
 * The originating slot should not be part of any active DMA transfer.
 */
void edma_link(unsigned from, unsigned to)
{
	if (from >= num_slots)
		return;
	if (to >= num_slots)
		return;
	edma_parm_modify(PARM_LINK_BCNTRLD, from, 0xffff0000, PARM_OFFSET(to));
}
EXPORT_SYMBOL(edma_link);

/**
 * edma_unlink - cut link from one parameter RAM slot
 * @from: parameter RAM slot originating the link
 *
 * The originating slot should not be part of any active DMA transfer.
 * Its link is set to 0xffff.
 */
void edma_unlink(unsigned from)
{
	if (from >= num_slots)
		return;
	edma_parm_or(PARM_LINK_BCNTRLD, from, 0xffff);
}
EXPORT_SYMBOL(edma_unlink);

/*-----------------------------------------------------------------------*/

/* Parameter RAM operations (ii) -- read/write whole parameter sets */

/**
 * edma_write_slot - write parameter RAM data for slot
 * @slot: number of parameter RAM slot being modified
 * @param: data to be written into parameter RAM slot
 *
 * Use this to assign all parameters of a transfer at once.  This
 * allows more efficient setup of transfers than issuing multiple
 * calls to set up those parameters in small pieces, and provides
 * complete control over all transfer options.
 */
void edma_write_slot(unsigned slot, const struct edmacc_param *param)
{
	if (slot >= num_slots)
		return;
	memcpy_toio(edmacc_regs_base + PARM_OFFSET(slot), param, PARM_SIZE);
}
EXPORT_SYMBOL(edma_write_slot);

/**
 * edma_read_slot - read parameter RAM data from slot
 * @slot: number of parameter RAM slot being copied
 * @param: where to store copy of parameter RAM data
 *
 * Use this to read data from a parameter RAM slot, perhaps to
 * save them as a template for later reuse.
 */
void edma_read_slot(unsigned slot, struct edmacc_param *param)
{
	if (slot >= num_slots)
		return;
	memcpy_fromio(param, edmacc_regs_base + PARM_OFFSET(slot), PARM_SIZE);
}
EXPORT_SYMBOL(edma_read_slot);

/*-----------------------------------------------------------------------*/

/* Various EDMA channel control operations */

/**
 * edma_pause - pause dma on a channel
 * @channel: on which edma_start() has been called
 *
 * This temporarily disables EDMA hardware events on the specified channel,
 * preventing them from triggering new transfers on its behalf
 */
void edma_pause(unsigned channel)
{
	if (channel < num_channels) {
		unsigned int mask = (1 << (channel & 0x1f));

		edma_shadow0_write_array(SH_EECR, channel >> 5, mask);
	}
}
EXPORT_SYMBOL(edma_pause);

/**
 * edma_resume - resumes dma on a paused channel
 * @channel: on which edma_pause() has been called
 *
 * This re-enables EDMA hardware events on the specified channel.
 */
void edma_resume(unsigned channel)
{
	if (channel < num_channels) {
		unsigned int mask = (1 << (channel & 0x1f));

		edma_shadow0_write_array(SH_EESR, channel >> 5, mask);
	}
}
EXPORT_SYMBOL(edma_resume);

/**
 * edma_start - start dma on a channel
 * @channel: channel being activated
 *
 * Channels with event associations will be triggered by their hardware
 * events, and channels without such associations will be triggered by
 * software.  (At this writing there is no interface for using software
 * triggers except with channels that don't support hardware triggers.)
 *
 * Returns zero on success, else negative errno.
 */
int edma_start(unsigned channel)
{
	if (channel < num_channels) {
		int j = channel >> 5;
		unsigned int mask = (1 << (channel & 0x1f));

		/* EDMA channels without event association */
		if (test_bit(channel, edma_noevent)) {
			pr_debug("EDMA: ESR%d %08x\n", j,
				edma_shadow0_read_array(SH_ESR, j));
			edma_shadow0_write_array(SH_ESR, j, mask);
			return 0;
		}

		/* EDMA channel with event association */
		pr_debug("EDMA: ER%d %08x\n", j,
			edma_shadow0_read_array(SH_ER, j));
		/* Clear any pending error */
		edma_write_array(EDMA_EMCR, j, mask);
		/* Clear any SER */
		edma_shadow0_write_array(SH_SECR, j, mask);
		edma_shadow0_write_array(SH_EESR, j, mask);
		pr_debug("EDMA: EER%d %08x\n", j,
			edma_shadow0_read_array(SH_EER, j));
		return 0;
	}

	return -EINVAL;
}
EXPORT_SYMBOL(edma_start);

/**
 * edma_stop - stops dma on the channel passed
 * @channel: channel being deactivated
 *
 * When @lch is a channel, any active transfer is paused and
 * all pending hardware events are cleared.  The current transfer
 * may not be resumed, and the channel's Parameter RAM should be
 * reinitialized before being reused.
 */
void edma_stop(unsigned channel)
{
	if (channel < num_channels) {
		int j = channel >> 5;
		unsigned int mask = (1 << (channel & 0x1f));

		edma_shadow0_write_array(SH_EECR, j, mask);
		edma_shadow0_write_array(SH_ECR, j, mask);
		edma_shadow0_write_array(SH_SECR, j, mask);
		edma_write_array(EDMA_EMCR, j, mask);

		pr_debug("EDMA: EER%d %08x\n", j,
				edma_shadow0_read_array(SH_EER, j));

		/* REVISIT:  consider guarding against inappropriate event
		 * chaining by overwriting with dummy_paramset.
		 */
	}
}
EXPORT_SYMBOL(edma_stop);

/******************************************************************************
 *
 * It cleans ParamEntry qand bring back EDMA to initial state if media has
 * been removed before EDMA has finished.It is usedful for removable media.
 * Arguments:
 *      ch_no     - channel no
 *
 * Return: zero on success, or corresponding error no on failure
 *
 * FIXME this should not be needed ... edma_stop() should suffice.
 *
 *****************************************************************************/

void edma_clean_channel(unsigned channel)
{
	if (channel < num_channels) {
		int j = (channel >> 5);
		unsigned int mask = 1 << (channel & 0x1f);

		pr_debug("EDMA: EMR%d %08x\n", j,
				edma_read_array(EDMA_EMR, j));
		edma_shadow0_write_array(SH_ECR, j, mask);
		/* Clear the corresponding EMR bits */
		edma_write_array(EDMA_EMCR, j, mask);
		/* Clear any SER */
		edma_shadow0_write_array(SH_SECR, j, mask);
		edma_write(EDMA_CCERRCLR, (1 << 16) | 0x3);
	}
}
EXPORT_SYMBOL(edma_clean_channel);

/*
 * edma_clear_event - clear an outstanding event on the DMA channel
 * Arguments:
 *	channel - channel number
 */
void edma_clear_event(unsigned channel)
{
	if (channel >= num_channels)
		return;
	if (channel < 32)
		edma_write(EDMA_ECR, 1 << channel);
	else
		edma_write(EDMA_ECRH, 1 << (channel - 32));
}
EXPORT_SYMBOL(edma_clear_event);

/*-----------------------------------------------------------------------*/

static int __init edma_probe(struct platform_device *pdev)
{
	struct edma_soc_info	*info = pdev->dev.platform_data;
	int			i;
	int			status;
	const s8		*noevent;
	int			irq = 0, err_irq = 0;
	struct resource		*r;
	resource_size_t		len;

	if (!info)
		return -ENODEV;

	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, "edma_cc");
	if (!r)
		return -ENODEV;

	len = r->end - r->start + 1;

	r = request_mem_region(r->start, len, r->name);
	if (!r)
		return -EBUSY;

	edmacc_regs_base = ioremap(r->start, len);
	if (!edmacc_regs_base) {
		status = -EBUSY;
		goto fail1;
	}

	num_channels = min_t(unsigned, info->n_channel, EDMA_MAX_DMACH);
	num_slots = min_t(unsigned, info->n_slot, EDMA_MAX_PARAMENTRY);

	dev_dbg(&pdev->dev, "DMA REG BASE ADDR=%p\n", edmacc_regs_base);

	for (i = 0; i < num_slots; i++)
		memcpy_toio(edmacc_regs_base + PARM_OFFSET(i),
				&dummy_paramset, PARM_SIZE);

	noevent = info->noevent;
	if (noevent) {
		while (*noevent != -1)
			set_bit(*noevent++, edma_noevent);
	}

	irq = platform_get_irq(pdev, 0);
	status = request_irq(irq, dma_irq_handler, 0, "edma", &pdev->dev);
	if (status < 0) {
		dev_dbg(&pdev->dev, "request_irq %d failed --> %d\n",
			irq, status);
		goto fail;
	}

	err_irq = platform_get_irq(pdev, 1);
	status = request_irq(err_irq, dma_ccerr_handler, 0,
				"edma_error", &pdev->dev);
	if (status < 0) {
		dev_dbg(&pdev->dev, "request_irq %d failed --> %d\n",
			err_irq, status);
		goto fail;
	}

	if (tc_errs_handled) {
		status = request_irq(IRQ_TCERRINT0, dma_tc0err_handler, 0,
					"edma_tc0", &pdev->dev);
		if (status < 0) {
			dev_dbg(&pdev->dev, "request_irq %d failed --> %d\n",
				IRQ_TCERRINT0, status);
			return status;
		}
		status = request_irq(IRQ_TCERRINT, dma_tc1err_handler, 0,
					"edma_tc1", &pdev->dev);
		if (status < 0) {
			dev_dbg(&pdev->dev, "request_irq %d --> %d\n",
				IRQ_TCERRINT, status);
			return status;
		}
	}

	/* Everything lives on transfer controller 1 until otherwise specified.
	 * This way, long transfers on the low priority queue
	 * started by the codec engine will not cause audio defects.
	 */
	for (i = 0; i < num_channels; i++)
		map_dmach_queue(i, EVENTQ_1);

	/* Event queue to TC mapping */
	for (i = 0; queue_tc_mapping[i][0] != -1; i++)
		map_queue_tc(queue_tc_mapping[i][0], queue_tc_mapping[i][1]);

	/* Event queue priority mapping */
	for (i = 0; queue_priority_mapping[i][0] != -1; i++)
		assign_priority_to_queue(queue_priority_mapping[i][0],
					 queue_priority_mapping[i][1]);

	for (i = 0; i < info->n_region; i++) {
		edma_write_array2(EDMA_DRAE, i, 0, 0x0);
		edma_write_array2(EDMA_DRAE, i, 1, 0x0);
		edma_write_array(EDMA_QRAE, i, 0x0);
	}

	return 0;

fail:
	if (err_irq)
		free_irq(err_irq, NULL);
	if (irq)
		free_irq(irq, NULL);
	iounmap(edmacc_regs_base);
fail1:
	release_mem_region(r->start, len);
	return status;
}


static struct platform_driver edma_driver = {
	.driver.name	= "edma",
};

static int __init edma_init(void)
{
	return platform_driver_probe(&edma_driver, edma_probe);
}
arch_initcall(edma_init);

