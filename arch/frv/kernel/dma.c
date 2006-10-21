/* dma.c: DMA controller management on FR401 and the like
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <asm/dma.h>
#include <asm/gpio-regs.h>
#include <asm/irc-regs.h>
#include <asm/cpu-irqs.h>

struct frv_dma_channel {
	uint8_t			flags;
#define FRV_DMA_FLAGS_RESERVED	0x01
#define FRV_DMA_FLAGS_INUSE	0x02
#define FRV_DMA_FLAGS_PAUSED	0x04
	uint8_t			cap;		/* capabilities available */
	int			irq;		/* completion IRQ */
	uint32_t		dreqbit;
	uint32_t		dackbit;
	uint32_t		donebit;
	const unsigned long	ioaddr;		/* DMA controller regs addr */
	const char		*devname;
	dma_irq_handler_t	handler;
	void			*data;
};


#define __get_DMAC(IO,X)	({ *(volatile unsigned long *)((IO) + DMAC_##X##x); })

#define __set_DMAC(IO,X,V)					\
do {								\
	*(volatile unsigned long *)((IO) + DMAC_##X##x) = (V);	\
	mb();							\
} while(0)

#define ___set_DMAC(IO,X,V)					\
do {								\
	*(volatile unsigned long *)((IO) + DMAC_##X##x) = (V);	\
} while(0)


static struct frv_dma_channel frv_dma_channels[FRV_DMA_NCHANS] = {
	[0] = {
		.cap		= FRV_DMA_CAP_DREQ | FRV_DMA_CAP_DACK | FRV_DMA_CAP_DONE,
		.irq		= IRQ_CPU_DMA0,
		.dreqbit	= SIR_DREQ0_INPUT,
		.dackbit	= SOR_DACK0_OUTPUT,
		.donebit	= SOR_DONE0_OUTPUT,
		.ioaddr		= 0xfe000900,
	},
	[1] = {
		.cap		= FRV_DMA_CAP_DREQ | FRV_DMA_CAP_DACK | FRV_DMA_CAP_DONE,
		.irq		= IRQ_CPU_DMA1,
		.dreqbit	= SIR_DREQ1_INPUT,
		.dackbit	= SOR_DACK1_OUTPUT,
		.donebit	= SOR_DONE1_OUTPUT,
		.ioaddr		= 0xfe000980,
	},
	[2] = {
		.cap		= FRV_DMA_CAP_DREQ | FRV_DMA_CAP_DACK,
		.irq		= IRQ_CPU_DMA2,
		.dreqbit	= SIR_DREQ2_INPUT,
		.dackbit	= SOR_DACK2_OUTPUT,
		.ioaddr		= 0xfe000a00,
	},
	[3] = {
		.cap		= FRV_DMA_CAP_DREQ | FRV_DMA_CAP_DACK,
		.irq		= IRQ_CPU_DMA3,
		.dreqbit	= SIR_DREQ3_INPUT,
		.dackbit	= SOR_DACK3_OUTPUT,
		.ioaddr		= 0xfe000a80,
	},
	[4] = {
		.cap		= FRV_DMA_CAP_DREQ,
		.irq		= IRQ_CPU_DMA4,
		.dreqbit	= SIR_DREQ4_INPUT,
		.ioaddr		= 0xfe001000,
	},
	[5] = {
		.cap		= FRV_DMA_CAP_DREQ,
		.irq		= IRQ_CPU_DMA5,
		.dreqbit	= SIR_DREQ5_INPUT,
		.ioaddr		= 0xfe001080,
	},
	[6] = {
		.cap		= FRV_DMA_CAP_DREQ,
		.irq		= IRQ_CPU_DMA6,
		.dreqbit	= SIR_DREQ6_INPUT,
		.ioaddr		= 0xfe001100,
	},
	[7] = {
		.cap		= FRV_DMA_CAP_DREQ,
		.irq		= IRQ_CPU_DMA7,
		.dreqbit	= SIR_DREQ7_INPUT,
		.ioaddr		= 0xfe001180,
	},
};

static DEFINE_RWLOCK(frv_dma_channels_lock);

unsigned long frv_dma_inprogress;

#define frv_clear_dma_inprogress(channel) \
	atomic_clear_mask(1 << (channel), &frv_dma_inprogress);

#define frv_set_dma_inprogress(channel) \
	atomic_set_mask(1 << (channel), &frv_dma_inprogress);

/*****************************************************************************/
/*
 * DMA irq handler - determine channel involved, grab status and call real handler
 */
static irqreturn_t dma_irq_handler(int irq, void *_channel)
{
	struct frv_dma_channel *channel = _channel;

	frv_clear_dma_inprogress(channel - frv_dma_channels);
	return channel->handler(channel - frv_dma_channels,
				__get_DMAC(channel->ioaddr, CSTR),
				channel->data);

} /* end dma_irq_handler() */

/*****************************************************************************/
/*
 * Determine which DMA controllers are present on this CPU
 */
void __init frv_dma_init(void)
{
	unsigned long psr = __get_PSR();
	int num_dma, i;

	/* First, determine how many DMA channels are available */
	switch (PSR_IMPLE(psr)) {
	case PSR_IMPLE_FR405:
	case PSR_IMPLE_FR451:
	case PSR_IMPLE_FR501:
	case PSR_IMPLE_FR551:
		num_dma = FRV_DMA_8CHANS;
		break;

	case PSR_IMPLE_FR401:
	default:
		num_dma = FRV_DMA_4CHANS;
		break;
	}

	/* Now mark all of the non-existent channels as reserved */
	for(i = num_dma; i < FRV_DMA_NCHANS; i++)
		frv_dma_channels[i].flags = FRV_DMA_FLAGS_RESERVED;

} /* end frv_dma_init() */

/*****************************************************************************/
/*
 * allocate a DMA controller channel and the IRQ associated with it
 */
int frv_dma_open(const char *devname,
		 unsigned long dmamask,
		 int dmacap,
		 dma_irq_handler_t handler,
		 unsigned long irq_flags,
		 void *data)
{
	struct frv_dma_channel *channel;
	int dma, ret;
	uint32_t val;

	write_lock(&frv_dma_channels_lock);

	ret = -ENOSPC;

	for (dma = FRV_DMA_NCHANS - 1; dma >= 0; dma--) {
		channel = &frv_dma_channels[dma];

		if (!test_bit(dma, &dmamask))
			continue;

		if ((channel->cap & dmacap) != dmacap)
			continue;

		if (!frv_dma_channels[dma].flags)
			goto found;
	}

	goto out;

 found:
	ret = request_irq(channel->irq, dma_irq_handler, irq_flags, devname, channel);
	if (ret < 0)
		goto out;

	/* okay, we've allocated all the resources */
	channel = &frv_dma_channels[dma];

	channel->flags		|= FRV_DMA_FLAGS_INUSE;
	channel->devname	= devname;
	channel->handler	= handler;
	channel->data		= data;

	/* Now make sure we are set up for DMA and not GPIO */
	/* SIR bit must be set for DMA to work */
	__set_SIR(channel->dreqbit | __get_SIR());
	/* SOR bits depend on what the caller requests */
	val = __get_SOR();
	if(dmacap & FRV_DMA_CAP_DACK)
		val |= channel->dackbit;
	else
		val &= ~channel->dackbit;
	if(dmacap & FRV_DMA_CAP_DONE)
		val |= channel->donebit;
	else
		val &= ~channel->donebit;
	__set_SOR(val);

	ret = dma;
 out:
	write_unlock(&frv_dma_channels_lock);
	return ret;
} /* end frv_dma_open() */

EXPORT_SYMBOL(frv_dma_open);

/*****************************************************************************/
/*
 * close a DMA channel and its associated interrupt
 */
void frv_dma_close(int dma)
{
	struct frv_dma_channel *channel = &frv_dma_channels[dma];
	unsigned long flags;

	write_lock_irqsave(&frv_dma_channels_lock, flags);

	free_irq(channel->irq, channel);
	frv_dma_stop(dma);

	channel->flags &= ~FRV_DMA_FLAGS_INUSE;

	write_unlock_irqrestore(&frv_dma_channels_lock, flags);
} /* end frv_dma_close() */

EXPORT_SYMBOL(frv_dma_close);

/*****************************************************************************/
/*
 * set static configuration on a DMA channel
 */
void frv_dma_config(int dma, unsigned long ccfr, unsigned long cctr, unsigned long apr)
{
	unsigned long ioaddr = frv_dma_channels[dma].ioaddr;

	___set_DMAC(ioaddr, CCFR, ccfr);
	___set_DMAC(ioaddr, CCTR, cctr);
	___set_DMAC(ioaddr, APR,  apr);
	mb();

} /* end frv_dma_config() */

EXPORT_SYMBOL(frv_dma_config);

/*****************************************************************************/
/*
 * start a DMA channel
 */
void frv_dma_start(int dma,
		   unsigned long sba, unsigned long dba,
		   unsigned long pix, unsigned long six, unsigned long bcl)
{
	unsigned long ioaddr = frv_dma_channels[dma].ioaddr;

	___set_DMAC(ioaddr, SBA,  sba);
	___set_DMAC(ioaddr, DBA,  dba);
	___set_DMAC(ioaddr, PIX,  pix);
	___set_DMAC(ioaddr, SIX,  six);
	___set_DMAC(ioaddr, BCL,  bcl);
	___set_DMAC(ioaddr, CSTR, 0);
	mb();

	__set_DMAC(ioaddr, CCTR, __get_DMAC(ioaddr, CCTR) | DMAC_CCTRx_ACT);
	frv_set_dma_inprogress(dma);

} /* end frv_dma_start() */

EXPORT_SYMBOL(frv_dma_start);

/*****************************************************************************/
/*
 * restart a DMA channel that's been stopped in circular addressing mode by comparison-end
 */
void frv_dma_restart_circular(int dma, unsigned long six)
{
	unsigned long ioaddr = frv_dma_channels[dma].ioaddr;

	___set_DMAC(ioaddr, SIX,  six);
	___set_DMAC(ioaddr, CSTR, __get_DMAC(ioaddr, CSTR) & ~DMAC_CSTRx_CE);
	mb();

	__set_DMAC(ioaddr, CCTR, __get_DMAC(ioaddr, CCTR) | DMAC_CCTRx_ACT);
	frv_set_dma_inprogress(dma);

} /* end frv_dma_restart_circular() */

EXPORT_SYMBOL(frv_dma_restart_circular);

/*****************************************************************************/
/*
 * stop a DMA channel
 */
void frv_dma_stop(int dma)
{
	unsigned long ioaddr = frv_dma_channels[dma].ioaddr;
	uint32_t cctr;

	___set_DMAC(ioaddr, CSTR, 0);
	cctr = __get_DMAC(ioaddr, CCTR);
	cctr &= ~(DMAC_CCTRx_IE | DMAC_CCTRx_ACT);
	cctr |= DMAC_CCTRx_FC; 	/* fifo clear */
	__set_DMAC(ioaddr, CCTR, cctr);
	__set_DMAC(ioaddr, BCL,  0);
	frv_clear_dma_inprogress(dma);
} /* end frv_dma_stop() */

EXPORT_SYMBOL(frv_dma_stop);

/*****************************************************************************/
/*
 * test interrupt status of DMA channel
 */
int is_frv_dma_interrupting(int dma)
{
	unsigned long ioaddr = frv_dma_channels[dma].ioaddr;

	return __get_DMAC(ioaddr, CSTR) & (1 << 23);

} /* end is_frv_dma_interrupting() */

EXPORT_SYMBOL(is_frv_dma_interrupting);

/*****************************************************************************/
/*
 * dump data about a DMA channel
 */
void frv_dma_dump(int dma)
{
	unsigned long ioaddr = frv_dma_channels[dma].ioaddr;
	unsigned long cstr, pix, six, bcl;

	cstr = __get_DMAC(ioaddr, CSTR);
	pix  = __get_DMAC(ioaddr, PIX);
	six  = __get_DMAC(ioaddr, SIX);
	bcl  = __get_DMAC(ioaddr, BCL);

	printk("DMA[%d] cstr=%lx pix=%lx six=%lx bcl=%lx\n", dma, cstr, pix, six, bcl);

} /* end frv_dma_dump() */

EXPORT_SYMBOL(frv_dma_dump);

/*****************************************************************************/
/*
 * pause all DMA controllers
 * - called by clock mangling routines
 * - caller must be holding interrupts disabled
 */
void frv_dma_pause_all(void)
{
	struct frv_dma_channel *channel;
	unsigned long ioaddr;
	unsigned long cstr, cctr;
	int dma;

	write_lock(&frv_dma_channels_lock);

	for (dma = FRV_DMA_NCHANS - 1; dma >= 0; dma--) {
		channel = &frv_dma_channels[dma];

		if (!(channel->flags & FRV_DMA_FLAGS_INUSE))
			continue;

		ioaddr = channel->ioaddr;
		cctr = __get_DMAC(ioaddr, CCTR);
		if (cctr & DMAC_CCTRx_ACT) {
			cctr &= ~DMAC_CCTRx_ACT;
			__set_DMAC(ioaddr, CCTR, cctr);

			do {
				cstr = __get_DMAC(ioaddr, CSTR);
			} while (cstr & DMAC_CSTRx_BUSY);

			if (cstr & DMAC_CSTRx_FED)
				channel->flags |= FRV_DMA_FLAGS_PAUSED;
			frv_clear_dma_inprogress(dma);
		}
	}

} /* end frv_dma_pause_all() */

EXPORT_SYMBOL(frv_dma_pause_all);

/*****************************************************************************/
/*
 * resume paused DMA controllers
 * - called by clock mangling routines
 * - caller must be holding interrupts disabled
 */
void frv_dma_resume_all(void)
{
	struct frv_dma_channel *channel;
	unsigned long ioaddr;
	unsigned long cstr, cctr;
	int dma;

	for (dma = FRV_DMA_NCHANS - 1; dma >= 0; dma--) {
		channel = &frv_dma_channels[dma];

		if (!(channel->flags & FRV_DMA_FLAGS_PAUSED))
			continue;

		ioaddr = channel->ioaddr;
		cstr = __get_DMAC(ioaddr, CSTR);
		cstr &= ~(DMAC_CSTRx_FED | DMAC_CSTRx_INT);
		__set_DMAC(ioaddr, CSTR, cstr);

		cctr = __get_DMAC(ioaddr, CCTR);
		cctr |= DMAC_CCTRx_ACT;
		__set_DMAC(ioaddr, CCTR, cctr);

		channel->flags &= ~FRV_DMA_FLAGS_PAUSED;
		frv_set_dma_inprogress(dma);
	}

	write_unlock(&frv_dma_channels_lock);

} /* end frv_dma_resume_all() */

EXPORT_SYMBOL(frv_dma_resume_all);

/*****************************************************************************/
/*
 * dma status clear
 */
void frv_dma_status_clear(int dma)
{
	unsigned long ioaddr = frv_dma_channels[dma].ioaddr;
	uint32_t cctr;
	___set_DMAC(ioaddr, CSTR, 0);

	cctr = __get_DMAC(ioaddr, CCTR);
} /* end frv_dma_status_clear() */

EXPORT_SYMBOL(frv_dma_status_clear);
