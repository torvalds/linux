// SPDX-License-Identifier: GPL-2.0+
/*
 * comedi/drivers/mite.c
 * Hardware driver for NI Mite PCI interface chip
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 1997-2002 David A. Schleef <ds@schleef.org>
 */

/*
 * The PCI-MIO E series driver was originally written by
 * Tomasz Motylewski <...>, and ported to comedi by ds.
 *
 * References for specifications:
 *
 *    321747b.pdf  Register Level Programmer Manual (obsolete)
 *    321747c.pdf  Register Level Programmer Manual (new)
 *    DAQ-STC reference manual
 *
 * Other possibly relevant info:
 *
 *    320517c.pdf  User manual (obsolete)
 *    320517f.pdf  User manual (new)
 *    320889a.pdf  delete
 *    320906c.pdf  maximum signal ratings
 *    321066a.pdf  about 16x
 *    321791a.pdf  discontinuation of at-mio-16e-10 rev. c
 *    321808a.pdf  about at-mio-16e-10 rev P
 *    321837a.pdf  discontinuation of at-mio-16de-10 rev d
 *    321838a.pdf  about at-mio-16de-10 rev N
 *
 * ISSUES:
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/log2.h>

#include "../comedi_pci.h"

#include "mite.h"

/*
 * Mite registers
 */
#define MITE_UNKNOWN_DMA_BURST_REG	0x28
#define UNKNOWN_DMA_BURST_ENABLE_BITS	0x600

#define MITE_PCI_CONFIG_OFFSET	0x300
#define MITE_CSIGR		0x460			/* chip signature */
#define CSIGR_TO_IOWINS(x)	(((x) >> 29) & 0x7)
#define CSIGR_TO_WINS(x)	(((x) >> 24) & 0x1f)
#define CSIGR_TO_WPDEP(x)	(((x) >> 20) & 0x7)
#define CSIGR_TO_DMAC(x)	(((x) >> 16) & 0xf)
#define CSIGR_TO_IMODE(x)	(((x) >> 12) & 0x3)	/* pci=0x3 */
#define CSIGR_TO_MMODE(x)	(((x) >> 8) & 0x3)	/* minimite=1 */
#define CSIGR_TO_TYPE(x)	(((x) >> 4) & 0xf)	/* mite=0, minimite=1 */
#define CSIGR_TO_VER(x)		(((x) >> 0) & 0xf)

#define MITE_CHAN(x)		(0x500 + 0x100 * (x))
#define MITE_CHOR(x)		(0x00 + MITE_CHAN(x))	/* channel operation */
#define CHOR_DMARESET		BIT(31)
#define CHOR_SET_SEND_TC	BIT(11)
#define CHOR_CLR_SEND_TC	BIT(10)
#define CHOR_SET_LPAUSE		BIT(9)
#define CHOR_CLR_LPAUSE		BIT(8)
#define CHOR_CLRDONE		BIT(7)
#define CHOR_CLRRB		BIT(6)
#define CHOR_CLRLC		BIT(5)
#define CHOR_FRESET		BIT(4)
#define CHOR_ABORT		BIT(3)	/* stop without emptying fifo */
#define CHOR_STOP		BIT(2)	/* stop after emptying fifo */
#define CHOR_CONT		BIT(1)
#define CHOR_START		BIT(0)
#define MITE_CHCR(x)		(0x04 + MITE_CHAN(x))	/* channel control */
#define CHCR_SET_DMA_IE		BIT(31)
#define CHCR_CLR_DMA_IE		BIT(30)
#define CHCR_SET_LINKP_IE	BIT(29)
#define CHCR_CLR_LINKP_IE	BIT(28)
#define CHCR_SET_SAR_IE		BIT(27)
#define CHCR_CLR_SAR_IE		BIT(26)
#define CHCR_SET_DONE_IE	BIT(25)
#define CHCR_CLR_DONE_IE	BIT(24)
#define CHCR_SET_MRDY_IE	BIT(23)
#define CHCR_CLR_MRDY_IE	BIT(22)
#define CHCR_SET_DRDY_IE	BIT(21)
#define CHCR_CLR_DRDY_IE	BIT(20)
#define CHCR_SET_LC_IE		BIT(19)
#define CHCR_CLR_LC_IE		BIT(18)
#define CHCR_SET_CONT_RB_IE	BIT(17)
#define CHCR_CLR_CONT_RB_IE	BIT(16)
#define CHCR_FIFO(x)		(((x) & 0x1) << 15)
#define CHCR_FIFODIS		CHCR_FIFO(1)
#define CHCR_FIFO_ON		CHCR_FIFO(0)
#define CHCR_BURST(x)		(((x) & 0x1) << 14)
#define CHCR_BURSTEN		CHCR_BURST(1)
#define CHCR_NO_BURSTEN		CHCR_BURST(0)
#define CHCR_BYTE_SWAP_DEVICE	BIT(6)
#define CHCR_BYTE_SWAP_MEMORY	BIT(4)
#define CHCR_DIR(x)		(((x) & 0x1) << 3)
#define CHCR_DEV_TO_MEM		CHCR_DIR(1)
#define CHCR_MEM_TO_DEV		CHCR_DIR(0)
#define CHCR_MODE(x)		(((x) & 0x7) << 0)
#define CHCR_NORMAL		CHCR_MODE(0)
#define CHCR_CONTINUE		CHCR_MODE(1)
#define CHCR_RINGBUFF		CHCR_MODE(2)
#define CHCR_LINKSHORT		CHCR_MODE(4)
#define CHCR_LINKLONG		CHCR_MODE(5)
#define MITE_TCR(x)		(0x08 + MITE_CHAN(x))	/* transfer count */
#define MITE_MCR(x)		(0x0c + MITE_CHAN(x))	/* memory config */
#define MITE_MAR(x)		(0x10 + MITE_CHAN(x))	/* memory address */
#define MITE_DCR(x)		(0x14 + MITE_CHAN(x))	/* device config */
#define DCR_NORMAL		BIT(29)
#define MITE_DAR(x)		(0x18 + MITE_CHAN(x))	/* device address */
#define MITE_LKCR(x)		(0x1c + MITE_CHAN(x))	/* link config */
#define MITE_LKAR(x)		(0x20 + MITE_CHAN(x))	/* link address */
#define MITE_LLKAR(x)		(0x24 + MITE_CHAN(x))	/* see tnt5002 manual */
#define MITE_BAR(x)		(0x28 + MITE_CHAN(x))	/* base address */
#define MITE_BCR(x)		(0x2c + MITE_CHAN(x))	/* base count */
#define MITE_SAR(x)		(0x30 + MITE_CHAN(x))	/* ? address */
#define MITE_WSCR(x)		(0x34 + MITE_CHAN(x))	/* ? */
#define MITE_WSER(x)		(0x38 + MITE_CHAN(x))	/* ? */
#define MITE_CHSR(x)		(0x3c + MITE_CHAN(x))	/* channel status */
#define CHSR_INT		BIT(31)
#define CHSR_LPAUSES		BIT(29)
#define CHSR_SARS		BIT(27)
#define CHSR_DONE		BIT(25)
#define CHSR_MRDY		BIT(23)
#define CHSR_DRDY		BIT(21)
#define CHSR_LINKC		BIT(19)
#define CHSR_CONTS_RB		BIT(17)
#define CHSR_ERROR		BIT(15)
#define CHSR_SABORT		BIT(14)
#define CHSR_HABORT		BIT(13)
#define CHSR_STOPS		BIT(12)
#define CHSR_OPERR(x)		(((x) & 0x3) << 10)
#define CHSR_OPERR_MASK		CHSR_OPERR(3)
#define CHSR_OPERR_NOERROR	CHSR_OPERR(0)
#define CHSR_OPERR_FIFOERROR	CHSR_OPERR(1)
#define CHSR_OPERR_LINKERROR	CHSR_OPERR(1)	/* ??? */
#define CHSR_XFERR		BIT(9)
#define CHSR_END		BIT(8)
#define CHSR_DRQ1		BIT(7)
#define CHSR_DRQ0		BIT(6)
#define CHSR_LERR(x)		(((x) & 0x3) << 4)
#define CHSR_LERR_MASK		CHSR_LERR(3)
#define CHSR_LBERR		CHSR_LERR(1)
#define CHSR_LRERR		CHSR_LERR(2)
#define CHSR_LOERR		CHSR_LERR(3)
#define CHSR_MERR(x)		(((x) & 0x3) << 2)
#define CHSR_MERR_MASK		CHSR_MERR(3)
#define CHSR_MBERR		CHSR_MERR(1)
#define CHSR_MRERR		CHSR_MERR(2)
#define CHSR_MOERR		CHSR_MERR(3)
#define CHSR_DERR(x)		(((x) & 0x3) << 0)
#define CHSR_DERR_MASK		CHSR_DERR(3)
#define CHSR_DBERR		CHSR_DERR(1)
#define CHSR_DRERR		CHSR_DERR(2)
#define CHSR_DOERR		CHSR_DERR(3)
#define MITE_FCR(x)		(0x40 + MITE_CHAN(x))	/* fifo count */

/* common bits for the memory/device/link config registers */
#define CR_RL(x)		(((x) & 0x7) << 21)
#define CR_REQS(x)		(((x) & 0x7) << 16)
#define CR_REQS_MASK		CR_REQS(7)
#define CR_ASEQ(x)		(((x) & 0x3) << 10)
#define CR_ASEQDONT		CR_ASEQ(0)
#define CR_ASEQUP		CR_ASEQ(1)
#define CR_ASEQDOWN		CR_ASEQ(2)
#define CR_ASEQ_MASK		CR_ASEQ(3)
#define CR_PSIZE(x)		(((x) & 0x3) << 8)
#define CR_PSIZE8		CR_PSIZE(1)
#define CR_PSIZE16		CR_PSIZE(2)
#define CR_PSIZE32		CR_PSIZE(3)
#define CR_PORT(x)		(((x) & 0x3) << 6)
#define CR_PORTCPU		CR_PORT(0)
#define CR_PORTIO		CR_PORT(1)
#define CR_PORTVXI		CR_PORT(2)
#define CR_PORTMXI		CR_PORT(3)
#define CR_AMDEVICE		BIT(0)

static unsigned int MITE_IODWBSR_1_WSIZE_bits(unsigned int size)
{
	return (ilog2(size) - 1) & 0x1f;
}

static unsigned int mite_retry_limit(unsigned int retry_limit)
{
	unsigned int value = 0;

	if (retry_limit)
		value = 1 + ilog2(retry_limit);
	if (value > 0x7)
		value = 0x7;
	return CR_RL(value);
}

static unsigned int mite_drq_reqs(unsigned int drq_line)
{
	/* This also works on m-series when using channels (drq_line) 4 or 5. */
	return CR_REQS((drq_line & 0x3) | 0x4);
}

static unsigned int mite_fifo_size(struct mite *mite, unsigned int channel)
{
	unsigned int fcr_bits = readl(mite->mmio + MITE_FCR(channel));
	unsigned int empty_count = (fcr_bits >> 16) & 0xff;
	unsigned int full_count = fcr_bits & 0xff;

	return empty_count + full_count;
}

static u32 mite_device_bytes_transferred(struct mite_channel *mite_chan)
{
	struct mite *mite = mite_chan->mite;

	return readl(mite->mmio + MITE_DAR(mite_chan->channel));
}

/**
 * mite_bytes_in_transit() - Returns the number of unread bytes in the fifo.
 * @mite_chan: MITE dma channel.
 */
u32 mite_bytes_in_transit(struct mite_channel *mite_chan)
{
	struct mite *mite = mite_chan->mite;

	return readl(mite->mmio + MITE_FCR(mite_chan->channel)) & 0xff;
}
EXPORT_SYMBOL_GPL(mite_bytes_in_transit);

/* returns lower bound for number of bytes transferred from device to memory */
static u32 mite_bytes_written_to_memory_lb(struct mite_channel *mite_chan)
{
	u32 device_byte_count;

	device_byte_count = mite_device_bytes_transferred(mite_chan);
	return device_byte_count - mite_bytes_in_transit(mite_chan);
}

/* returns upper bound for number of bytes transferred from device to memory */
static u32 mite_bytes_written_to_memory_ub(struct mite_channel *mite_chan)
{
	u32 in_transit_count;

	in_transit_count = mite_bytes_in_transit(mite_chan);
	return mite_device_bytes_transferred(mite_chan) - in_transit_count;
}

/* returns lower bound for number of bytes read from memory to device */
static u32 mite_bytes_read_from_memory_lb(struct mite_channel *mite_chan)
{
	u32 device_byte_count;

	device_byte_count = mite_device_bytes_transferred(mite_chan);
	return device_byte_count + mite_bytes_in_transit(mite_chan);
}

/* returns upper bound for number of bytes read from memory to device */
static u32 mite_bytes_read_from_memory_ub(struct mite_channel *mite_chan)
{
	u32 in_transit_count;

	in_transit_count = mite_bytes_in_transit(mite_chan);
	return mite_device_bytes_transferred(mite_chan) + in_transit_count;
}

static void mite_sync_input_dma(struct mite_channel *mite_chan,
				struct comedi_subdevice *s)
{
	struct comedi_async *async = s->async;
	int count;
	unsigned int nbytes, old_alloc_count;

	old_alloc_count = async->buf_write_alloc_count;
	/* write alloc as much as we can */
	comedi_buf_write_alloc(s, async->prealloc_bufsz);

	nbytes = mite_bytes_written_to_memory_lb(mite_chan);
	if ((int)(mite_bytes_written_to_memory_ub(mite_chan) -
		  old_alloc_count) > 0) {
		dev_warn(s->device->class_dev,
			 "mite: DMA overwrite of free area\n");
		async->events |= COMEDI_CB_OVERFLOW;
		return;
	}

	count = nbytes - async->buf_write_count;
	/*
	 * it's possible count will be negative due to conservative value
	 * returned by mite_bytes_written_to_memory_lb
	 */
	if (count > 0) {
		comedi_buf_write_free(s, count);
		comedi_inc_scan_progress(s, count);
		async->events |= COMEDI_CB_BLOCK;
	}
}

static void mite_sync_output_dma(struct mite_channel *mite_chan,
				 struct comedi_subdevice *s)
{
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;
	u32 stop_count = cmd->stop_arg * comedi_bytes_per_scan(s);
	unsigned int old_alloc_count = async->buf_read_alloc_count;
	u32 nbytes_ub, nbytes_lb;
	int count;
	bool finite_regen = (cmd->stop_src == TRIG_NONE && stop_count != 0);

	/* read alloc as much as we can */
	comedi_buf_read_alloc(s, async->prealloc_bufsz);
	nbytes_lb = mite_bytes_read_from_memory_lb(mite_chan);
	if (cmd->stop_src == TRIG_COUNT && (int)(nbytes_lb - stop_count) > 0)
		nbytes_lb = stop_count;
	nbytes_ub = mite_bytes_read_from_memory_ub(mite_chan);
	if (cmd->stop_src == TRIG_COUNT && (int)(nbytes_ub - stop_count) > 0)
		nbytes_ub = stop_count;

	if ((!finite_regen || stop_count > old_alloc_count) &&
	    ((int)(nbytes_ub - old_alloc_count) > 0)) {
		dev_warn(s->device->class_dev, "mite: DMA underrun\n");
		async->events |= COMEDI_CB_OVERFLOW;
		return;
	}

	if (finite_regen) {
		/*
		 * This is a special case where we continuously output a finite
		 * buffer.  In this case, we do not free any of the memory,
		 * hence we expect that old_alloc_count will reach a maximum of
		 * stop_count bytes.
		 */
		return;
	}

	count = nbytes_lb - async->buf_read_count;
	if (count > 0) {
		comedi_buf_read_free(s, count);
		async->events |= COMEDI_CB_BLOCK;
	}
}

/**
 * mite_sync_dma() - Sync the MITE dma with the COMEDI async buffer.
 * @mite_chan: MITE dma channel.
 * @s: COMEDI subdevice.
 */
void mite_sync_dma(struct mite_channel *mite_chan, struct comedi_subdevice *s)
{
	if (mite_chan->dir == COMEDI_INPUT)
		mite_sync_input_dma(mite_chan, s);
	else
		mite_sync_output_dma(mite_chan, s);
}
EXPORT_SYMBOL_GPL(mite_sync_dma);

static unsigned int mite_get_status(struct mite_channel *mite_chan)
{
	struct mite *mite = mite_chan->mite;
	unsigned int status;
	unsigned long flags;

	spin_lock_irqsave(&mite->lock, flags);
	status = readl(mite->mmio + MITE_CHSR(mite_chan->channel));
	if (status & CHSR_DONE) {
		mite_chan->done = 1;
		writel(CHOR_CLRDONE,
		       mite->mmio + MITE_CHOR(mite_chan->channel));
	}
	spin_unlock_irqrestore(&mite->lock, flags);
	return status;
}

/**
 * mite_ack_linkc() - Check and ack the LINKC interrupt,
 * @mite_chan: MITE dma channel.
 * @s: COMEDI subdevice.
 * @sync: flag to force a mite_sync_dma().
 *
 * This will also ack the DONE interrupt if active.
 */
void mite_ack_linkc(struct mite_channel *mite_chan,
		    struct comedi_subdevice *s,
		    bool sync)
{
	struct mite *mite = mite_chan->mite;
	unsigned int status;

	status = mite_get_status(mite_chan);
	if (status & CHSR_LINKC) {
		writel(CHOR_CLRLC, mite->mmio + MITE_CHOR(mite_chan->channel));
		sync = true;
	}
	if (sync)
		mite_sync_dma(mite_chan, s);

	if (status & CHSR_XFERR) {
		dev_err(s->device->class_dev,
			"mite: transfer error %08x\n", status);
		s->async->events |= COMEDI_CB_ERROR;
	}
}
EXPORT_SYMBOL_GPL(mite_ack_linkc);

/**
 * mite_done() - Check is a MITE dma transfer is complete.
 * @mite_chan: MITE dma channel.
 *
 * This will also ack the DONE interrupt if active.
 */
int mite_done(struct mite_channel *mite_chan)
{
	struct mite *mite = mite_chan->mite;
	unsigned long flags;
	int done;

	mite_get_status(mite_chan);
	spin_lock_irqsave(&mite->lock, flags);
	done = mite_chan->done;
	spin_unlock_irqrestore(&mite->lock, flags);
	return done;
}
EXPORT_SYMBOL_GPL(mite_done);

static void mite_dma_reset(struct mite_channel *mite_chan)
{
	writel(CHOR_DMARESET | CHOR_FRESET,
	       mite_chan->mite->mmio + MITE_CHOR(mite_chan->channel));
}

/**
 * mite_dma_arm() - Start a MITE dma transfer.
 * @mite_chan: MITE dma channel.
 */
void mite_dma_arm(struct mite_channel *mite_chan)
{
	struct mite *mite = mite_chan->mite;
	unsigned long flags;

	/*
	 * memory barrier is intended to insure any twiddling with the buffer
	 * is done before writing to the mite to arm dma transfer
	 */
	smp_mb();
	spin_lock_irqsave(&mite->lock, flags);
	mite_chan->done = 0;
	/* arm */
	writel(CHOR_START, mite->mmio + MITE_CHOR(mite_chan->channel));
	spin_unlock_irqrestore(&mite->lock, flags);
}
EXPORT_SYMBOL_GPL(mite_dma_arm);

/**
 * mite_dma_disarm() - Stop a MITE dma transfer.
 * @mite_chan: MITE dma channel.
 */
void mite_dma_disarm(struct mite_channel *mite_chan)
{
	struct mite *mite = mite_chan->mite;

	/* disarm */
	writel(CHOR_ABORT, mite->mmio + MITE_CHOR(mite_chan->channel));
}
EXPORT_SYMBOL_GPL(mite_dma_disarm);

/**
 * mite_prep_dma() - Prepare a MITE dma channel for transfers.
 * @mite_chan: MITE dma channel.
 * @num_device_bits: device transfer size (8, 16, or 32-bits).
 * @num_memory_bits: memory transfer size (8, 16, or 32-bits).
 */
void mite_prep_dma(struct mite_channel *mite_chan,
		   unsigned int num_device_bits, unsigned int num_memory_bits)
{
	struct mite *mite = mite_chan->mite;
	unsigned int chcr, mcr, dcr, lkcr;

	mite_dma_reset(mite_chan);

	/* short link chaining mode */
	chcr = CHCR_SET_DMA_IE | CHCR_LINKSHORT | CHCR_SET_DONE_IE |
	    CHCR_BURSTEN;
	/*
	 * Link Complete Interrupt: interrupt every time a link
	 * in MITE_RING is completed. This can generate a lot of
	 * extra interrupts, but right now we update the values
	 * of buf_int_ptr and buf_int_count at each interrupt. A
	 * better method is to poll the MITE before each user
	 * "read()" to calculate the number of bytes available.
	 */
	chcr |= CHCR_SET_LC_IE;
	if (num_memory_bits == 32 && num_device_bits == 16) {
		/*
		 * Doing a combined 32 and 16 bit byteswap gets the 16 bit
		 * samples into the fifo in the right order. Tested doing 32 bit
		 * memory to 16 bit device transfers to the analog out of a
		 * pxi-6281, which has mite version = 1, type = 4. This also
		 * works for dma reads from the counters on e-series boards.
		 */
		chcr |= CHCR_BYTE_SWAP_DEVICE | CHCR_BYTE_SWAP_MEMORY;
	}
	if (mite_chan->dir == COMEDI_INPUT)
		chcr |= CHCR_DEV_TO_MEM;

	writel(chcr, mite->mmio + MITE_CHCR(mite_chan->channel));

	/* to/from memory */
	mcr = mite_retry_limit(64) | CR_ASEQUP;
	switch (num_memory_bits) {
	case 8:
		mcr |= CR_PSIZE8;
		break;
	case 16:
		mcr |= CR_PSIZE16;
		break;
	case 32:
		mcr |= CR_PSIZE32;
		break;
	default:
		pr_warn("bug! invalid mem bit width for dma transfer\n");
		break;
	}
	writel(mcr, mite->mmio + MITE_MCR(mite_chan->channel));

	/* from/to device */
	dcr = mite_retry_limit(64) | CR_ASEQUP;
	dcr |= CR_PORTIO | CR_AMDEVICE | mite_drq_reqs(mite_chan->channel);
	switch (num_device_bits) {
	case 8:
		dcr |= CR_PSIZE8;
		break;
	case 16:
		dcr |= CR_PSIZE16;
		break;
	case 32:
		dcr |= CR_PSIZE32;
		break;
	default:
		pr_warn("bug! invalid dev bit width for dma transfer\n");
		break;
	}
	writel(dcr, mite->mmio + MITE_DCR(mite_chan->channel));

	/* reset the DAR */
	writel(0, mite->mmio + MITE_DAR(mite_chan->channel));

	/* the link is 32bits */
	lkcr = mite_retry_limit(64) | CR_ASEQUP | CR_PSIZE32;
	writel(lkcr, mite->mmio + MITE_LKCR(mite_chan->channel));

	/* starting address for link chaining */
	writel(mite_chan->ring->dma_addr,
	       mite->mmio + MITE_LKAR(mite_chan->channel));
}
EXPORT_SYMBOL_GPL(mite_prep_dma);

/**
 * mite_request_channel_in_range() - Request a MITE dma channel.
 * @mite: MITE device.
 * @ring: MITE dma ring.
 * @min_channel: minimum channel index to use.
 * @max_channel: maximum channel index to use.
 */
struct mite_channel *mite_request_channel_in_range(struct mite *mite,
						   struct mite_ring *ring,
						   unsigned int min_channel,
						   unsigned int max_channel)
{
	struct mite_channel *mite_chan = NULL;
	unsigned long flags;
	int i;

	/*
	 * spin lock so mite_release_channel can be called safely
	 * from interrupts
	 */
	spin_lock_irqsave(&mite->lock, flags);
	for (i = min_channel; i <= max_channel; ++i) {
		mite_chan = &mite->channels[i];
		if (!mite_chan->ring) {
			mite_chan->ring = ring;
			break;
		}
		mite_chan = NULL;
	}
	spin_unlock_irqrestore(&mite->lock, flags);
	return mite_chan;
}
EXPORT_SYMBOL_GPL(mite_request_channel_in_range);

/**
 * mite_request_channel() - Request a MITE dma channel.
 * @mite: MITE device.
 * @ring: MITE dma ring.
 */
struct mite_channel *mite_request_channel(struct mite *mite,
					  struct mite_ring *ring)
{
	return mite_request_channel_in_range(mite, ring, 0,
					     mite->num_channels - 1);
}
EXPORT_SYMBOL_GPL(mite_request_channel);

/**
 * mite_release_channel() - Release a MITE dma channel.
 * @mite_chan: MITE dma channel.
 */
void mite_release_channel(struct mite_channel *mite_chan)
{
	struct mite *mite = mite_chan->mite;
	unsigned long flags;

	/* spin lock to prevent races with mite_request_channel */
	spin_lock_irqsave(&mite->lock, flags);
	if (mite_chan->ring) {
		mite_dma_disarm(mite_chan);
		mite_dma_reset(mite_chan);
		/*
		 * disable all channel's interrupts (do it after disarm/reset so
		 * MITE_CHCR reg isn't changed while dma is still active!)
		 */
		writel(CHCR_CLR_DMA_IE | CHCR_CLR_LINKP_IE |
		       CHCR_CLR_SAR_IE | CHCR_CLR_DONE_IE |
		       CHCR_CLR_MRDY_IE | CHCR_CLR_DRDY_IE |
		       CHCR_CLR_LC_IE | CHCR_CLR_CONT_RB_IE,
		       mite->mmio + MITE_CHCR(mite_chan->channel));
		mite_chan->ring = NULL;
	}
	spin_unlock_irqrestore(&mite->lock, flags);
}
EXPORT_SYMBOL_GPL(mite_release_channel);

/**
 * mite_init_ring_descriptors() - Initialize a MITE dma ring descriptors.
 * @ring: MITE dma ring.
 * @s: COMEDI subdevice.
 * @nbytes: the size of the dma ring (in bytes).
 *
 * Initializes the ring buffer descriptors to provide correct DMA transfer
 * links to the exact amount of memory required. When the ring buffer is
 * allocated by mite_buf_change(), the default is to initialize the ring
 * to refer to the entire DMA data buffer. A command may call this function
 * later to re-initialize and shorten the amount of memory that will be
 * transferred.
 */
int mite_init_ring_descriptors(struct mite_ring *ring,
			       struct comedi_subdevice *s,
			       unsigned int nbytes)
{
	struct comedi_async *async = s->async;
	struct mite_dma_desc *desc = NULL;
	unsigned int n_full_links = nbytes >> PAGE_SHIFT;
	unsigned int remainder = nbytes % PAGE_SIZE;
	int i;

	dev_dbg(s->device->class_dev,
		"mite: init ring buffer to %u bytes\n", nbytes);

	if ((n_full_links + (remainder > 0 ? 1 : 0)) > ring->n_links) {
		dev_err(s->device->class_dev,
			"mite: ring buffer too small for requested init\n");
		return -ENOMEM;
	}

	/* We set the descriptors for all full links. */
	for (i = 0; i < n_full_links; ++i) {
		desc = &ring->descs[i];
		desc->count = cpu_to_le32(PAGE_SIZE);
		desc->addr = cpu_to_le32(async->buf_map->page_list[i].dma_addr);
		desc->next = cpu_to_le32(ring->dma_addr +
					 (i + 1) * sizeof(*desc));
	}

	/* the last link is either a remainder or was a full link. */
	if (remainder > 0) {
		desc = &ring->descs[i];
		/* set the lesser count for the remainder link */
		desc->count = cpu_to_le32(remainder);
		desc->addr = cpu_to_le32(async->buf_map->page_list[i].dma_addr);
	}

	/* Assign the last link->next to point back to the head of the list. */
	desc->next = cpu_to_le32(ring->dma_addr);

	/*
	 * barrier is meant to insure that all the writes to the dma descriptors
	 * have completed before the dma controller is commanded to read them
	 */
	smp_wmb();
	return 0;
}
EXPORT_SYMBOL_GPL(mite_init_ring_descriptors);

static void mite_free_dma_descs(struct mite_ring *ring)
{
	struct mite_dma_desc *descs = ring->descs;

	if (descs) {
		dma_free_coherent(ring->hw_dev,
				  ring->n_links * sizeof(*descs),
				  descs, ring->dma_addr);
		ring->descs = NULL;
		ring->dma_addr = 0;
		ring->n_links = 0;
	}
}

/**
 * mite_buf_change() - COMEDI subdevice (*buf_change) for a MITE dma ring.
 * @ring: MITE dma ring.
 * @s: COMEDI subdevice.
 */
int mite_buf_change(struct mite_ring *ring, struct comedi_subdevice *s)
{
	struct comedi_async *async = s->async;
	struct mite_dma_desc *descs;
	unsigned int n_links;

	mite_free_dma_descs(ring);

	if (async->prealloc_bufsz == 0)
		return 0;

	n_links = async->prealloc_bufsz >> PAGE_SHIFT;

	descs = dma_alloc_coherent(ring->hw_dev,
				   n_links * sizeof(*descs),
				   &ring->dma_addr, GFP_KERNEL);
	if (!descs) {
		dev_err(s->device->class_dev,
			"mite: ring buffer allocation failed\n");
		return -ENOMEM;
	}
	ring->descs = descs;
	ring->n_links = n_links;

	return mite_init_ring_descriptors(ring, s, n_links << PAGE_SHIFT);
}
EXPORT_SYMBOL_GPL(mite_buf_change);

/**
 * mite_alloc_ring() - Allocate a MITE dma ring.
 * @mite: MITE device.
 */
struct mite_ring *mite_alloc_ring(struct mite *mite)
{
	struct mite_ring *ring;

	ring = kmalloc(sizeof(*ring), GFP_KERNEL);
	if (!ring)
		return NULL;
	ring->hw_dev = get_device(&mite->pcidev->dev);
	if (!ring->hw_dev) {
		kfree(ring);
		return NULL;
	}
	ring->n_links = 0;
	ring->descs = NULL;
	ring->dma_addr = 0;
	return ring;
}
EXPORT_SYMBOL_GPL(mite_alloc_ring);

/**
 * mite_free_ring() - Free a MITE dma ring and its descriptors.
 * @ring: MITE dma ring.
 */
void mite_free_ring(struct mite_ring *ring)
{
	if (ring) {
		mite_free_dma_descs(ring);
		put_device(ring->hw_dev);
		kfree(ring);
	}
}
EXPORT_SYMBOL_GPL(mite_free_ring);

static int mite_setup(struct comedi_device *dev, struct mite *mite,
		      bool use_win1)
{
	resource_size_t daq_phys_addr;
	unsigned long length;
	int i;
	u32 csigr_bits;
	unsigned int unknown_dma_burst_bits;
	unsigned int wpdep;

	pci_set_master(mite->pcidev);

	mite->mmio = pci_ioremap_bar(mite->pcidev, 0);
	if (!mite->mmio)
		return -ENOMEM;

	dev->mmio = pci_ioremap_bar(mite->pcidev, 1);
	if (!dev->mmio)
		return -ENOMEM;
	daq_phys_addr = pci_resource_start(mite->pcidev, 1);
	length = pci_resource_len(mite->pcidev, 1);

	if (use_win1) {
		writel(0, mite->mmio + MITE_IODWBSR);
		dev_dbg(dev->class_dev,
			"mite: using I/O Window Base Size register 1\n");
		writel(daq_phys_addr | WENAB |
		       MITE_IODWBSR_1_WSIZE_bits(length),
		       mite->mmio + MITE_IODWBSR_1);
		writel(0, mite->mmio + MITE_IODWCR_1);
	} else {
		writel(daq_phys_addr | WENAB, mite->mmio + MITE_IODWBSR);
	}
	/*
	 * Make sure dma bursts work. I got this from running a bus analyzer
	 * on a pxi-6281 and a pxi-6713. 6713 powered up with register value
	 * of 0x61f and bursts worked. 6281 powered up with register value of
	 * 0x1f and bursts didn't work. The NI windows driver reads the
	 * register, then does a bitwise-or of 0x600 with it and writes it back.
	 *
	 * The bits 0x90180700 in MITE_UNKNOWN_DMA_BURST_REG can be
	 * written and read back.  The bits 0x1f always read as 1.
	 * The rest always read as zero.
	 */
	unknown_dma_burst_bits = readl(mite->mmio + MITE_UNKNOWN_DMA_BURST_REG);
	unknown_dma_burst_bits |= UNKNOWN_DMA_BURST_ENABLE_BITS;
	writel(unknown_dma_burst_bits, mite->mmio + MITE_UNKNOWN_DMA_BURST_REG);

	csigr_bits = readl(mite->mmio + MITE_CSIGR);
	mite->num_channels = CSIGR_TO_DMAC(csigr_bits);
	if (mite->num_channels > MAX_MITE_DMA_CHANNELS) {
		dev_warn(dev->class_dev,
			 "mite: bug? chip claims to have %i dma channels. Setting to %i.\n",
			 mite->num_channels, MAX_MITE_DMA_CHANNELS);
		mite->num_channels = MAX_MITE_DMA_CHANNELS;
	}

	/* get the wpdep bits and convert it to the write port fifo depth */
	wpdep = CSIGR_TO_WPDEP(csigr_bits);
	if (wpdep)
		wpdep = BIT(wpdep);

	dev_dbg(dev->class_dev,
		"mite: version = %i, type = %i, mite mode = %i, interface mode = %i\n",
		CSIGR_TO_VER(csigr_bits), CSIGR_TO_TYPE(csigr_bits),
		CSIGR_TO_MMODE(csigr_bits), CSIGR_TO_IMODE(csigr_bits));
	dev_dbg(dev->class_dev,
		"mite: num channels = %i, write post fifo depth = %i, wins = %i, iowins = %i\n",
		CSIGR_TO_DMAC(csigr_bits), wpdep,
		CSIGR_TO_WINS(csigr_bits), CSIGR_TO_IOWINS(csigr_bits));

	for (i = 0; i < mite->num_channels; i++) {
		writel(CHOR_DMARESET, mite->mmio + MITE_CHOR(i));
		/* disable interrupts */
		writel(CHCR_CLR_DMA_IE | CHCR_CLR_LINKP_IE | CHCR_CLR_SAR_IE |
		       CHCR_CLR_DONE_IE | CHCR_CLR_MRDY_IE | CHCR_CLR_DRDY_IE |
		       CHCR_CLR_LC_IE | CHCR_CLR_CONT_RB_IE,
		       mite->mmio + MITE_CHCR(i));
	}
	mite->fifo_size = mite_fifo_size(mite, 0);
	dev_dbg(dev->class_dev, "mite: fifo size is %i.\n", mite->fifo_size);
	return 0;
}

/**
 * mite_attach() - Allocate and initialize a MITE device for a comedi driver.
 * @dev: COMEDI device.
 * @use_win1: flag to use I/O Window 1 instead of I/O Window 0.
 *
 * Called by a COMEDI drivers (*auto_attach).
 *
 * Returns a pointer to the MITE device on success, or NULL if the MITE cannot
 * be allocated or remapped.
 */
struct mite *mite_attach(struct comedi_device *dev, bool use_win1)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct mite *mite;
	unsigned int i;
	int ret;

	mite = kzalloc(sizeof(*mite), GFP_KERNEL);
	if (!mite)
		return NULL;

	spin_lock_init(&mite->lock);
	mite->pcidev = pcidev;
	for (i = 0; i < MAX_MITE_DMA_CHANNELS; ++i) {
		mite->channels[i].mite = mite;
		mite->channels[i].channel = i;
		mite->channels[i].done = 1;
	}

	ret = mite_setup(dev, mite, use_win1);
	if (ret) {
		if (mite->mmio)
			iounmap(mite->mmio);
		kfree(mite);
		return NULL;
	}

	return mite;
}
EXPORT_SYMBOL_GPL(mite_attach);

/**
 * mite_detach() - Unmap and free a MITE device for a comedi driver.
 * @mite: MITE device.
 *
 * Called by a COMEDI drivers (*detach).
 */
void mite_detach(struct mite *mite)
{
	if (!mite)
		return;

	if (mite->mmio)
		iounmap(mite->mmio);

	kfree(mite);
}
EXPORT_SYMBOL_GPL(mite_detach);

static int __init mite_module_init(void)
{
	return 0;
}
module_init(mite_module_init);

static void __exit mite_module_exit(void)
{
}
module_exit(mite_module_exit);

MODULE_AUTHOR("Comedi https://www.comedi.org");
MODULE_DESCRIPTION("Comedi helper for NI Mite PCI interface chip");
MODULE_LICENSE("GPL");
