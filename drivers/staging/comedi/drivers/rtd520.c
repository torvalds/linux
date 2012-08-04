/*
    comedi/drivers/rtd520.c
    Comedi driver for Real Time Devices (RTD) PCI4520/DM7520

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 2001 David A. Schleef <ds@schleef.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/
/*
Driver: rtd520
Description: Real Time Devices PCI4520/DM7520
Author: Dan Christian
Devices: [Real Time Devices] DM7520HR-1 (rtd520), DM7520HR-8,
  PCI4520, PCI4520-8
Status: Works.  Only tested on DM7520-8.  Not SMP safe.

Configuration options:
  [0] - PCI bus of device (optional)
	If bus / slot is not specified, the first available PCI
	device will be used.
  [1] - PCI slot of device (optional)
*/
/*
    Created by Dan Christian, NASA Ames Research Center.

    The PCI4520 is a PCI card.  The DM7520 is a PC/104-plus card.
    Both have:
    8/16 12 bit ADC with FIFO and channel gain table
    8 bits high speed digital out (for external MUX) (or 8 in or 8 out)
    8 bits high speed digital in with FIFO and interrupt on change (or 8 IO)
    2 12 bit DACs with FIFOs
    2 bits output
    2 bits input
    bus mastering DMA
    timers: ADC sample, pacer, burst, about, delay, DA1, DA2
    sample counter
    3 user timer/counters (8254)
    external interrupt

    The DM7520 has slightly fewer features (fewer gain steps).

    These boards can support external multiplexors and multi-board
    synchronization, but this driver doesn't support that.

    Board docs: http://www.rtdusa.com/PC104/DM/analog%20IO/dm7520.htm
    Data sheet: http://www.rtdusa.com/pdf/dm7520.pdf
    Example source: http://www.rtdusa.com/examples/dm/dm7520.zip
    Call them and ask for the register level manual.
    PCI chip: http://www.plxtech.com/products/io/pci9080

    Notes:
    This board is memory mapped.  There is some IO stuff, but it isn't needed.

    I use a pretty loose naming style within the driver (rtd_blah).
    All externally visible names should be rtd520_blah.
    I use camelCase for structures (and inside them).
    I may also use upper CamelCase for function names (old habit).

    This board is somewhat related to the RTD PCI4400 board.

    I borrowed heavily from the ni_mio_common, ni_atmio16d, mite, and
    das1800, since they have the best documented code.  Driver
    cb_pcidas64.c uses the same DMA controller.

    As far as I can tell, the About interrupt doesn't work if Sample is
    also enabled.  It turns out that About really isn't needed, since
    we always count down samples read.

    There was some timer/counter code, but it didn't follow the right API.

*/

/*
  driver status:

  Analog-In supports instruction and command mode.

  With DMA, you can sample at 1.15Mhz with 70% idle on a 400Mhz K6-2
  (single channel, 64K read buffer).  I get random system lockups when
  using DMA with ALI-15xx based systems.  I haven't been able to test
  any other chipsets.  The lockups happen soon after the start of an
  acquistion, not in the middle of a long run.

  Without DMA, you can do 620Khz sampling with 20% idle on a 400Mhz K6-2
  (with a 256K read buffer).

  Digital-IO and Analog-Out only support instruction mode.

*/

#include <linux/interrupt.h>
#include <linux/delay.h>

#include "../comedidev.h"

#define DRV_NAME "rtd520"

/*======================================================================
  Driver specific stuff (tunable)
======================================================================*/
/* Enable this to test the new DMA support. You may get hard lock ups */
/*#define USE_DMA*/

/* We really only need 2 buffers.  More than that means being much
   smarter about knowing which ones are full. */
#define DMA_CHAIN_COUNT 2	/* max DMA segments/buffers in a ring (min 2) */

/* Target period for periodic transfers.  This sets the user read latency. */
/* Note: There are certain rates where we give this up and transfer 1/2 FIFO */
/* If this is too low, efficiency is poor */
#define TRANS_TARGET_PERIOD 10000000	/* 10 ms (in nanoseconds) */

/* Set a practical limit on how long a list to support (affects memory use) */
/* The board support a channel list up to the FIFO length (1K or 8K) */
#define RTD_MAX_CHANLIST	128	/* max channel list that we allow */

/* tuning for ai/ao instruction done polling */
#ifdef FAST_SPIN
#define WAIT_QUIETLY		/* as nothing, spin on done bit */
#define RTD_ADC_TIMEOUT	66000	/* 2 msec at 33mhz bus rate */
#define RTD_DAC_TIMEOUT	66000
#define RTD_DMA_TIMEOUT	33000	/* 1 msec */
#else
/* by delaying, power and electrical noise are reduced somewhat */
#define WAIT_QUIETLY	udelay(1)
#define RTD_ADC_TIMEOUT	2000	/* in usec */
#define RTD_DAC_TIMEOUT	2000	/* in usec */
#define RTD_DMA_TIMEOUT	1000	/* in usec */
#endif

/*======================================================================
  Board specific stuff
======================================================================*/

/* registers  */
#define PCI_VENDOR_ID_RTD	0x1435
/*
  The board has three memory windows: las0, las1, and lcfg (the PCI chip)
  Las1 has the data and can be burst DMAed 32bits at a time.
*/
#define LCFG_PCIINDEX	0
/* PCI region 1 is a 256 byte IO space mapping.  Use??? */
#define LAS0_PCIINDEX	2	/* PCI memory resources */
#define LAS1_PCIINDEX	3
#define LCFG_PCISIZE	0x100
#define LAS0_PCISIZE	0x200
#define LAS1_PCISIZE	0x10

#define RTD_CLOCK_RATE	8000000	/* 8Mhz onboard clock */
#define RTD_CLOCK_BASE	125	/* clock period in ns */

/* Note: these speed are slower than the spec, but fit the counter resolution*/
#define RTD_MAX_SPEED	1625	/* when sampling, in nanoseconds */
/* max speed if we don't have to wait for settling */
#define RTD_MAX_SPEED_1	875	/* if single channel, in nanoseconds */

#define RTD_MIN_SPEED	2097151875	/* (24bit counter) in nanoseconds */
/* min speed when only 1 channel (no burst counter) */
#define RTD_MIN_SPEED_1	5000000	/* 200Hz, in nanoseconds */

#include "rtd520.h"
#include "plx9080.h"

/* Setup continuous ring of 1/2 FIFO transfers.  See RTD manual p91 */
#define DMA_MODE_BITS (\
		       PLX_LOCAL_BUS_16_WIDE_BITS \
		       | PLX_DMA_EN_READYIN_BIT \
		       | PLX_DMA_LOCAL_BURST_EN_BIT \
		       | PLX_EN_CHAIN_BIT \
		       | PLX_DMA_INTR_PCI_BIT \
		       | PLX_LOCAL_ADDR_CONST_BIT \
		       | PLX_DEMAND_MODE_BIT)

#define DMA_TRANSFER_BITS (\
/* descriptors in PCI memory*/  PLX_DESC_IN_PCI_BIT \
/* interrupt at end of block */ | PLX_INTR_TERM_COUNT \
/* from board to PCI */		| PLX_XFER_LOCAL_TO_PCI)

/*======================================================================
  Comedi specific stuff
======================================================================*/

/*
 * The board has 3 input modes and the gains of 1,2,4,...32 (, 64, 128)
 */
static const struct comedi_lrange rtd_ai_7520_range = {
	18, {
		/* +-5V input range gain steps */
		BIP_RANGE(5.0),
		BIP_RANGE(5.0 / 2),
		BIP_RANGE(5.0 / 4),
		BIP_RANGE(5.0 / 8),
		BIP_RANGE(5.0 / 16),
		BIP_RANGE(5.0 / 32),
		/* +-10V input range gain steps */
		BIP_RANGE(10.0),
		BIP_RANGE(10.0 / 2),
		BIP_RANGE(10.0 / 4),
		BIP_RANGE(10.0 / 8),
		BIP_RANGE(10.0 / 16),
		BIP_RANGE(10.0 / 32),
		/* +10V input range gain steps */
		UNI_RANGE(10.0),
		UNI_RANGE(10.0 / 2),
		UNI_RANGE(10.0 / 4),
		UNI_RANGE(10.0 / 8),
		UNI_RANGE(10.0 / 16),
		UNI_RANGE(10.0 / 32),
	}
};

/* PCI4520 has two more gains (6 more entries) */
static const struct comedi_lrange rtd_ai_4520_range = {
	24, {
		/* +-5V input range gain steps */
		BIP_RANGE(5.0),
		BIP_RANGE(5.0 / 2),
		BIP_RANGE(5.0 / 4),
		BIP_RANGE(5.0 / 8),
		BIP_RANGE(5.0 / 16),
		BIP_RANGE(5.0 / 32),
		BIP_RANGE(5.0 / 64),
		BIP_RANGE(5.0 / 128),
		/* +-10V input range gain steps */
		BIP_RANGE(10.0),
		BIP_RANGE(10.0 / 2),
		BIP_RANGE(10.0 / 4),
		BIP_RANGE(10.0 / 8),
		BIP_RANGE(10.0 / 16),
		BIP_RANGE(10.0 / 32),
		BIP_RANGE(10.0 / 64),
		BIP_RANGE(10.0 / 128),
		/* +10V input range gain steps */
		UNI_RANGE(10.0),
		UNI_RANGE(10.0 / 2),
		UNI_RANGE(10.0 / 4),
		UNI_RANGE(10.0 / 8),
		UNI_RANGE(10.0 / 16),
		UNI_RANGE(10.0 / 32),
		UNI_RANGE(10.0 / 64),
		UNI_RANGE(10.0 / 128),
	}
};

/* Table order matches range values */
static const struct comedi_lrange rtd_ao_range = {
	4, {
		UNI_RANGE(5),
		UNI_RANGE(10),
		BIP_RANGE(5),
		BIP_RANGE(10),
	}
};

struct rtdBoard {
	const char *name;
	int device_id;
	int aiChans;
	int aiBits;
	int aiMaxGain;
	int range10Start;	/* start of +-10V range */
	int rangeUniStart;	/* start of +10V range */
};

static const struct rtdBoard rtd520Boards[] = {
	{
		.name		= "DM7520",
		.device_id	= 0x7520,
		.aiChans	= 16,
		.aiBits		= 12,
		.aiMaxGain	= 32,
		.range10Start	= 6,
		.rangeUniStart	= 12,
	}, {
		.name		= "PCI4520",
		.device_id	= 0x4520,
		.aiChans	= 16,
		.aiBits		= 12,
		.aiMaxGain	= 128,
		.range10Start	= 8,
		.rangeUniStart	= 16,
	},
};

/*
   This structure is for data unique to this hardware driver.
   This is also unique for each board in the system.
*/
struct rtdPrivate {
	/* memory mapped board structures */
	void __iomem *las0;
	void __iomem *las1;
	void __iomem *lcfg;

	unsigned long intCount;	/* interrupt count */
	long aiCount;		/* total transfer size (samples) */
	int transCount;		/* # to transfer data. 0->1/2FIFO */
	int flags;		/* flag event modes */

	/* channel list info */
	/* chanBipolar tracks whether a channel is bipolar (and needs +2048) */
	unsigned char chanBipolar[RTD_MAX_CHANLIST / 8];	/* bit array */

	/* read back data */
	unsigned int aoValue[2];	/* Used for AO read back */

	/* timer gate (when enabled) */
	u8 utcGate[4];		/* 1 extra allows simple range check */

	/* shadow registers affect other registers, but can't be read back */
	/* The macros below update these on writes */
	u16 intMask;		/* interrupt mask */
	u16 intClearMask;	/* interrupt clear mask */
	u8 utcCtrl[4];		/* crtl mode for 3 utc + read back */
	u8 dioStatus;		/* could be read back (dio0Ctrl) */
#ifdef USE_DMA
	/*
	 * Always DMA 1/2 FIFO.  Buffer (dmaBuff?) is (at least) twice that
	 * size.  After transferring, interrupt processes 1/2 FIFO and
	 * passes to comedi
	 */
	s16 dma0Offset;		/* current processing offset (0, 1/2) */
	uint16_t *dma0Buff[DMA_CHAIN_COUNT];	/* DMA buffers (for ADC) */
	dma_addr_t dma0BuffPhysAddr[DMA_CHAIN_COUNT];	/* physical addresses */
	struct plx_dma_desc *dma0Chain;	/* DMA descriptor ring for dmaBuff */
	dma_addr_t dma0ChainPhysAddr;	/* physical addresses */
	/* shadow registers */
	u8 dma0Control;
	u8 dma1Control;
#endif				/* USE_DMA */
	unsigned fifoLen;
};

/* bit defines for "flags" */
#define SEND_EOS	0x01	/* send End Of Scan events */
#define DMA0_ACTIVE	0x02	/* DMA0 is active */
#define DMA1_ACTIVE	0x04	/* DMA1 is active */

/* Macros for accessing channel list bit array */
#define CHAN_ARRAY_TEST(array, index) \
	(((array)[(index)/8] >> ((index) & 0x7)) & 0x1)
#define CHAN_ARRAY_SET(array, index) \
	(((array)[(index)/8] |= 1 << ((index) & 0x7)))
#define CHAN_ARRAY_CLEAR(array, index) \
	(((array)[(index)/8] &= ~(1 << ((index) & 0x7))))

/*
  Given a desired period and the clock period (both in ns),
  return the proper counter value (divider-1).
  Sets the original period to be the true value.
  Note: you have to check if the value is larger than the counter range!
*/
static int rtd_ns_to_timer_base(unsigned int *nanosec,	/* desired period (in ns) */
				int round_mode, int base)
{				/* clock period (in ns) */
	int divider;

	switch (round_mode) {
	case TRIG_ROUND_NEAREST:
	default:
		divider = (*nanosec + base / 2) / base;
		break;
	case TRIG_ROUND_DOWN:
		divider = (*nanosec) / base;
		break;
	case TRIG_ROUND_UP:
		divider = (*nanosec + base - 1) / base;
		break;
	}
	if (divider < 2)
		divider = 2;	/* min is divide by 2 */

	/* Note: we don't check for max, because different timers
	   have different ranges */

	*nanosec = base * divider;
	return divider - 1;	/* countdown is divisor+1 */
}

/*
  Given a desired period (in ns),
  return the proper counter value (divider-1) for the internal clock.
  Sets the original period to be the true value.
*/
static int rtd_ns_to_timer(unsigned int *ns, int round_mode)
{
	return rtd_ns_to_timer_base(ns, round_mode, RTD_CLOCK_BASE);
}

/*
  Convert a single comedi channel-gain entry to a RTD520 table entry
*/
static unsigned short rtdConvertChanGain(struct comedi_device *dev,
					 unsigned int comediChan, int chanIndex)
{				/* index in channel list */
	const struct rtdBoard *thisboard = comedi_board(dev);
	struct rtdPrivate *devpriv = dev->private;
	unsigned int chan, range, aref;
	unsigned short r = 0;

	chan = CR_CHAN(comediChan);
	range = CR_RANGE(comediChan);
	aref = CR_AREF(comediChan);

	r |= chan & 0xf;

	/* Note: we also setup the channel list bipolar flag array */
	if (range < thisboard->range10Start) {	/* first batch are +-5 */
		r |= 0x000;	/* +-5 range */
		r |= (range & 0x7) << 4;	/* gain */
		CHAN_ARRAY_SET(devpriv->chanBipolar, chanIndex);
	} else if (range < thisboard->rangeUniStart) {	/* second batch are +-10 */
		r |= 0x100;	/* +-10 range */
		/* gain */
		r |= ((range - thisboard->range10Start) & 0x7) << 4;
		CHAN_ARRAY_SET(devpriv->chanBipolar, chanIndex);
	} else {		/* last batch is +10 */
		r |= 0x200;	/* +10 range */
		/* gain */
		r |= ((range - thisboard->rangeUniStart) & 0x7) << 4;
		CHAN_ARRAY_CLEAR(devpriv->chanBipolar, chanIndex);
	}

	switch (aref) {
	case AREF_GROUND:	/* on-board ground */
		break;

	case AREF_COMMON:
		r |= 0x80;	/* ref external analog common */
		break;

	case AREF_DIFF:
		r |= 0x400;	/* differential inputs */
		break;

	case AREF_OTHER:	/* ??? */
		break;
	}
	/*printk ("chan=%d r=%d a=%d -> 0x%x\n",
	   chan, range, aref, r); */
	return r;
}

/*
  Setup the channel-gain table from a comedi list
*/
static void rtd_load_channelgain_list(struct comedi_device *dev,
				      unsigned int n_chan, unsigned int *list)
{
	struct rtdPrivate *devpriv = dev->private;

	if (n_chan > 1) {	/* setup channel gain table */
		int ii;

		writel(0, devpriv->las0 + LAS0_CGT_CLEAR);
		writel(1, devpriv->las0 + LAS0_CGT_ENABLE);
		for (ii = 0; ii < n_chan; ii++) {
			writel(rtdConvertChanGain(dev, list[ii], ii),
				devpriv->las0 + LAS0_CGT_WRITE);
		}
	} else {		/* just use the channel gain latch */
		writel(0, devpriv->las0 + LAS0_CGT_ENABLE);
		writel(rtdConvertChanGain(dev, list[0], 0),
			devpriv->las0 + LAS0_CGL_WRITE);
	}
}

/* determine fifo size by doing adc conversions until the fifo half
empty status flag clears */
static int rtd520_probe_fifo_depth(struct comedi_device *dev)
{
	struct rtdPrivate *devpriv = dev->private;
	unsigned int chanspec = CR_PACK(0, 0, AREF_GROUND);
	unsigned i;
	static const unsigned limit = 0x2000;
	unsigned fifo_size = 0;

	writel(0, devpriv->las0 + LAS0_ADC_FIFO_CLEAR);
	rtd_load_channelgain_list(dev, 1, &chanspec);
	/* ADC conversion trigger source: SOFTWARE */
	writel(0, devpriv->las0 + LAS0_ADC_CONVERSION);
	/* convert  samples */
	for (i = 0; i < limit; ++i) {
		unsigned fifo_status;
		/* trigger conversion */
		writew(0, devpriv->las0 + LAS0_ADC);
		udelay(1);
		fifo_status = readl(devpriv->las0 + LAS0_ADC);
		if ((fifo_status & FS_ADC_HEMPTY) == 0) {
			fifo_size = 2 * i;
			break;
		}
	}
	if (i == limit) {
		printk(KERN_INFO "\ncomedi: %s: failed to probe fifo size.\n",
		       DRV_NAME);
		return -EIO;
	}
	writel(0, devpriv->las0 + LAS0_ADC_FIFO_CLEAR);
	if (fifo_size != 0x400 && fifo_size != 0x2000) {
		printk
		    (KERN_INFO "\ncomedi: %s: unexpected fifo size of %i, expected 1024 or 8192.\n",
		     DRV_NAME, fifo_size);
		return -EIO;
	}
	return fifo_size;
}

/*
  "instructions" read/write data in "one-shot" or "software-triggered"
  mode (simplest case).
  This doesn't use interrupts.

  Note, we don't do any settling delays.  Use a instruction list to
  select, delay, then read.
 */
static int rtd_ai_rinsn(struct comedi_device *dev,
			struct comedi_subdevice *s, struct comedi_insn *insn,
			unsigned int *data)
{
	struct rtdPrivate *devpriv = dev->private;
	int n, ii;
	int stat;

	/* clear any old fifo data */
	writel(0, devpriv->las0 + LAS0_ADC_FIFO_CLEAR);

	/* write channel to multiplexer and clear channel gain table */
	rtd_load_channelgain_list(dev, 1, &insn->chanspec);

	/* ADC conversion trigger source: SOFTWARE */
	writel(0, devpriv->las0 + LAS0_ADC_CONVERSION);

	/* convert n samples */
	for (n = 0; n < insn->n; n++) {
		s16 d;
		/* trigger conversion */
		writew(0, devpriv->las0 + LAS0_ADC);

		for (ii = 0; ii < RTD_ADC_TIMEOUT; ++ii) {
			stat = readl(devpriv->las0 + LAS0_ADC);
			if (stat & FS_ADC_NOT_EMPTY)	/* 1 -> not empty */
				break;
			WAIT_QUIETLY;
		}
		if (ii >= RTD_ADC_TIMEOUT) {
			DPRINTK
			    ("rtd520: Error: ADC never finished! FifoStatus=0x%x\n",
			     stat ^ 0x6666);
			return -ETIMEDOUT;
		}

		/* read data */
		d = readw(devpriv->las1 + LAS1_ADC_FIFO);
		/*printk ("rtd520: Got 0x%x after %d usec\n", d, ii+1); */
		d = d >> 3;	/* low 3 bits are marker lines */
		if (CHAN_ARRAY_TEST(devpriv->chanBipolar, 0))
			/* convert to comedi unsigned data */
			data[n] = d + 2048;
		else
			data[n] = d;
	}

	/* return the number of samples read/written */
	return n;
}

/*
  Get what we know is there.... Fast!
  This uses 1/2 the bus cycles of read_dregs (below).

  The manual claims that we can do a lword read, but it doesn't work here.
*/
static int ai_read_n(struct comedi_device *dev, struct comedi_subdevice *s,
		     int count)
{
	struct rtdPrivate *devpriv = dev->private;
	int ii;

	for (ii = 0; ii < count; ii++) {
		short sample;
		s16 d;

		if (0 == devpriv->aiCount) {	/* done */
			d = readw(devpriv->las1 + LAS1_ADC_FIFO);
			continue;
		}
#if 0
		if (!(readl(devpriv->las0 + LAS0_ADC) & FS_ADC_NOT_EMPTY)) {
			DPRINTK("comedi: READ OOPS on %d of %d\n", ii + 1,
				count);
			break;
		}
#endif
		d = readw(devpriv->las1 + LAS1_ADC_FIFO);

		d = d >> 3;	/* low 3 bits are marker lines */
		if (CHAN_ARRAY_TEST(devpriv->chanBipolar, s->async->cur_chan)) {
			/* convert to comedi unsigned data */
			sample = d + 2048;
		} else
			sample = d;

		if (!comedi_buf_put(s->async, sample))
			return -1;

		if (devpriv->aiCount > 0)	/* < 0, means read forever */
			devpriv->aiCount--;
	}
	return 0;
}

/*
  unknown amout of data is waiting in fifo.
*/
static int ai_read_dregs(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct rtdPrivate *devpriv = dev->private;

	while (readl(devpriv->las0 + LAS0_ADC) & FS_ADC_NOT_EMPTY) {
		short sample;
		s16 d = readw(devpriv->las1 + LAS1_ADC_FIFO);

		if (0 == devpriv->aiCount) {	/* done */
			continue;	/* read rest */
		}

		d = d >> 3;	/* low 3 bits are marker lines */
		if (CHAN_ARRAY_TEST(devpriv->chanBipolar, s->async->cur_chan)) {
			/* convert to comedi unsigned data */
			sample = d + 2048;
		} else
			sample = d;

		if (!comedi_buf_put(s->async, sample))
			return -1;

		if (devpriv->aiCount > 0)	/* < 0, means read forever */
			devpriv->aiCount--;
	}
	return 0;
}

#ifdef USE_DMA
/*
  Terminate a DMA transfer and wait for everything to quiet down
*/
void abort_dma(struct comedi_device *dev, unsigned int channel)
{				/* DMA channel 0, 1 */
	struct rtdPrivate *devpriv = dev->private;
	unsigned long dma_cs_addr;	/* the control/status register */
	uint8_t status;
	unsigned int ii;
	/* unsigned long flags; */

	dma_cs_addr = (unsigned long)devpriv->lcfg
	    + ((channel == 0) ? LCFG_DMACSR0 : LCFG_DMACSR1);

	/*  spinlock for plx dma control/status reg */
	/* spin_lock_irqsave( &dev->spinlock, flags ); */

	/*  abort dma transfer if necessary */
	status = readb(dma_cs_addr);
	if ((status & PLX_DMA_EN_BIT) == 0) {	/* not enabled (Error?) */
		DPRINTK("rtd520: AbortDma on non-active channel %d (0x%x)\n",
			channel, status);
		goto abortDmaExit;
	}

	/* wait to make sure done bit is zero (needed?) */
	for (ii = 0; (status & PLX_DMA_DONE_BIT) && ii < RTD_DMA_TIMEOUT; ii++) {
		WAIT_QUIETLY;
		status = readb(dma_cs_addr);
	}
	if (status & PLX_DMA_DONE_BIT) {
		printk("rtd520: Timeout waiting for dma %i done clear\n",
		       channel);
		goto abortDmaExit;
	}

	/* disable channel (required) */
	writeb(0, dma_cs_addr);
	udelay(1);		/* needed?? */
	/* set abort bit for channel */
	writeb(PLX_DMA_ABORT_BIT, dma_cs_addr);

	/*  wait for dma done bit to be set */
	status = readb(dma_cs_addr);
	for (ii = 0;
	     (status & PLX_DMA_DONE_BIT) == 0 && ii < RTD_DMA_TIMEOUT; ii++) {
		status = readb(dma_cs_addr);
		WAIT_QUIETLY;
	}
	if ((status & PLX_DMA_DONE_BIT) == 0) {
		printk("rtd520: Timeout waiting for dma %i done set\n",
		       channel);
	}

abortDmaExit:
	/* spin_unlock_irqrestore( &dev->spinlock, flags ); */
}

/*
  Process what is in the DMA transfer buffer and pass to comedi
  Note: this is not re-entrant
*/
static int ai_process_dma(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct rtdPrivate *devpriv = dev->private;
	int ii, n;
	s16 *dp;

	if (devpriv->aiCount == 0)	/* transfer already complete */
		return 0;

	dp = devpriv->dma0Buff[devpriv->dma0Offset];
	for (ii = 0; ii < devpriv->fifoLen / 2;) {	/* convert samples */
		short sample;

		if (CHAN_ARRAY_TEST(devpriv->chanBipolar, s->async->cur_chan)) {
			sample = (*dp >> 3) + 2048;	/* convert to comedi unsigned data */
		else
			sample = *dp >> 3;	/* low 3 bits are marker lines */

		*dp++ = sample;	/* put processed value back */

		if (++s->async->cur_chan >= s->async->cmd.chanlist_len)
			s->async->cur_chan = 0;

		++ii;		/* number ready to transfer */
		if (devpriv->aiCount > 0) {	/* < 0, means read forever */
			if (--devpriv->aiCount == 0) {	/* done */
				/*DPRINTK ("rtd520: Final %d samples\n", ii); */
				break;
			}
		}
	}

	/* now pass the whole array to the comedi buffer */
	dp = devpriv->dma0Buff[devpriv->dma0Offset];
	n = comedi_buf_write_alloc(s->async, ii * sizeof(s16));
	if (n < (ii * sizeof(s16))) {	/* any residual is an error */
		DPRINTK("rtd520:ai_process_dma buffer overflow %d samples!\n",
			ii - (n / sizeof(s16)));
		s->async->events |= COMEDI_CB_ERROR;
		return -1;
	}
	comedi_buf_memcpy_to(s->async, 0, dp, n);
	comedi_buf_write_free(s->async, n);

	/*
	 * always at least 1 scan -- 1/2 FIFO is larger than our max scan list
	 */
	s->async->events |= COMEDI_CB_BLOCK | COMEDI_CB_EOS;

	if (++devpriv->dma0Offset >= DMA_CHAIN_COUNT) {	/* next buffer */
		devpriv->dma0Offset = 0;
	}
	return 0;
}
#endif /* USE_DMA */

/*
  Handle all rtd520 interrupts.
  Runs atomically and is never re-entered.
  This is a "slow handler";  other interrupts may be active.
  The data conversion may someday happen in a "bottom half".
*/
static irqreturn_t rtd_interrupt(int irq,	/* interrupt number (ignored) */
				 void *d)
{				/* our data *//* cpu context (ignored) */
	struct comedi_device *dev = d;
	struct comedi_subdevice *s = dev->subdevices + 0;	/* analog in subdevice */
	struct rtdPrivate *devpriv = dev->private;
	u32 overrun;
	u16 status;
	u16 fifoStatus;

	if (!dev->attached)
		return IRQ_NONE;

	devpriv->intCount++;	/* DEBUG statistics */

	fifoStatus = readl(devpriv->las0 + LAS0_ADC);
	/* check for FIFO full, this automatically halts the ADC! */
	if (!(fifoStatus & FS_ADC_NOT_FULL)) {	/* 0 -> full */
		DPRINTK("rtd520: FIFO full! fifo_status=0x%x\n", (fifoStatus ^ 0x6666) & 0x7777);	/* should be all 0s */
		goto abortTransfer;
	}
#ifdef USE_DMA
	if (devpriv->flags & DMA0_ACTIVE) {	/* Check DMA */
		u32 istatus = readl(devpriv->lcfg + LCFG_ITCSR);

		if (istatus & ICS_DMA0_A) {
			if (ai_process_dma(dev, s) < 0) {
				DPRINTK
				    ("rtd520: comedi read buffer overflow (DMA) with %ld to go!\n",
				     devpriv->aiCount);
				devpriv->dma0Control &= ~PLX_DMA_START_BIT;
				devpriv->dma0Control |= PLX_CLEAR_DMA_INTR_BIT;
				writeb(devpriv->dma0Control,
					devpriv->lcfg + LCFG_DMACSR0);
				goto abortTransfer;
			}

			/*DPRINTK ("rtd520: DMA transfer: %ld to go, istatus %x\n",
			   devpriv->aiCount, istatus); */
			devpriv->dma0Control &= ~PLX_DMA_START_BIT;
			devpriv->dma0Control |= PLX_CLEAR_DMA_INTR_BIT;
			writeb(devpriv->dma0Control,
				devpriv->lcfg + LCFG_DMACSR0);
			if (0 == devpriv->aiCount) {	/* counted down */
				DPRINTK("rtd520: Samples Done (DMA).\n");
				goto transferDone;
			}
			comedi_event(dev, s);
		} else {
			/*DPRINTK ("rtd520: No DMA ready: istatus %x\n", istatus); */
		}
	}
	/* Fall through and check for other interrupt sources */
#endif /* USE_DMA */

	status = readw(devpriv->las0 + LAS0_IT);
	/* if interrupt was not caused by our board, or handled above */
	if (0 == status)
		return IRQ_HANDLED;

	if (status & IRQM_ADC_ABOUT_CNT) {	/* sample count -> read FIFO */
		/* since the priority interrupt controller may have queued a sample
		   counter interrupt, even though we have already finished,
		   we must handle the possibility that there is no data here */
		if (!(fifoStatus & FS_ADC_HEMPTY)) {	/* 0 -> 1/2 full */
			/*DPRINTK("rtd520: Sample int, reading 1/2FIFO.  fifo_status 0x%x\n",
			   (fifoStatus ^ 0x6666) & 0x7777); */
			if (ai_read_n(dev, s, devpriv->fifoLen / 2) < 0) {
				DPRINTK
				    ("rtd520: comedi read buffer overflow (1/2FIFO) with %ld to go!\n",
				     devpriv->aiCount);
				goto abortTransfer;
			}
			if (0 == devpriv->aiCount) {	/* counted down */
				DPRINTK("rtd520: Samples Done (1/2). fifo_status was 0x%x\n", (fifoStatus ^ 0x6666) & 0x7777);	/* should be all 0s */
				goto transferDone;
			}
			comedi_event(dev, s);
		} else if (devpriv->transCount > 0) {	/* read often */
			/*DPRINTK("rtd520: Sample int, reading %d  fifo_status 0x%x\n",
			   devpriv->transCount, (fifoStatus ^ 0x6666) & 0x7777); */
			if (fifoStatus & FS_ADC_NOT_EMPTY) {	/* 1 -> not empty */
				if (ai_read_n(dev, s, devpriv->transCount) < 0) {
					DPRINTK
					    ("rtd520: comedi read buffer overflow (N) with %ld to go!\n",
					     devpriv->aiCount);
					goto abortTransfer;
				}
				if (0 == devpriv->aiCount) {	/* counted down */
					DPRINTK
					    ("rtd520: Samples Done (N). fifo_status was 0x%x\n",
					     (fifoStatus ^ 0x6666) & 0x7777);
					goto transferDone;
				}
				comedi_event(dev, s);
			}
		} else {	/* wait for 1/2 FIFO (old) */
			DPRINTK
			    ("rtd520: Sample int.  Wait for 1/2. fifo_status 0x%x\n",
			     (fifoStatus ^ 0x6666) & 0x7777);
		}
	} else {
		DPRINTK("rtd520: unknown interrupt source!\n");
	}

	overrun = readl(devpriv->las0 + LAS0_OVERRUN) & 0xffff;
	if (overrun) {
		DPRINTK
		    ("rtd520: Interrupt overrun with %ld to go! over_status=0x%x\n",
		     devpriv->aiCount, overrun);
		goto abortTransfer;
	}

	/* clear the interrupt */
	devpriv->intClearMask = status;
	writew(devpriv->intClearMask, devpriv->las0 + LAS0_CLEAR);
	readw(devpriv->las0 + LAS0_CLEAR);
	return IRQ_HANDLED;

abortTransfer:
	writel(0, devpriv->las0 + LAS0_ADC_FIFO_CLEAR);
	s->async->events |= COMEDI_CB_ERROR;
	devpriv->aiCount = 0;	/* stop and don't transfer any more */
	/* fall into transferDone */

transferDone:
	/* pacer stop source: SOFTWARE */
	writel(0, devpriv->las0 + LAS0_PACER_STOP);
	writel(0, devpriv->las0 + LAS0_PACER);	/* stop pacer */
	writel(0, devpriv->las0 + LAS0_ADC_CONVERSION);
	devpriv->intMask = 0;
	writew(devpriv->intMask, devpriv->las0 + LAS0_IT);
#ifdef USE_DMA
	if (devpriv->flags & DMA0_ACTIVE) {
		writel(readl(devpriv->lcfg + LCFG_ITCSR) & ~ICS_DMA0_E,
			devpriv->lcfg + LCFG_ITCSR);
		abort_dma(dev, 0);
		devpriv->flags &= ~DMA0_ACTIVE;
		/* if Using DMA, then we should have read everything by now */
		if (devpriv->aiCount > 0) {
			DPRINTK("rtd520: Lost DMA data! %ld remain\n",
				devpriv->aiCount);
		}
	}
#endif /* USE_DMA */

	if (devpriv->aiCount > 0) {	/* there shouldn't be anything left */
		fifoStatus = readl(devpriv->las0 + LAS0_ADC);
		DPRINTK("rtd520: Finishing up. %ld remain, fifoStat=%x\n", devpriv->aiCount, (fifoStatus ^ 0x6666) & 0x7777);	/* should read all 0s */
		ai_read_dregs(dev, s);	/* read anything left in FIFO */
	}

	s->async->events |= COMEDI_CB_EOA;	/* signal end to comedi */
	comedi_event(dev, s);

	/* clear the interrupt */
	status = readw(devpriv->las0 + LAS0_IT);
	devpriv->intClearMask = status;
	writew(devpriv->intClearMask, devpriv->las0 + LAS0_CLEAR);
	readw(devpriv->las0 + LAS0_CLEAR);

	fifoStatus = readl(devpriv->las0 + LAS0_ADC);
	overrun = readl(devpriv->las0 + LAS0_OVERRUN) & 0xffff;
	DPRINTK
	    ("rtd520: Acquisition complete. %ld ints, intStat=%x, overStat=%x\n",
	     devpriv->intCount, status, overrun);

	return IRQ_HANDLED;
}

#if 0
/*
  return the number of samples available
*/
static int rtd_ai_poll(struct comedi_device *dev, struct comedi_subdevice *s)
{
	/* TODO: This needs to mask interrupts, read_dregs, and then re-enable */
	/* Not sure what to do if DMA is active */
	return s->async->buf_write_count - s->async->buf_read_count;
}
#endif

/*
  cmdtest tests a particular command to see if it is valid.
  Using the cmdtest ioctl, a user can create a valid cmd
  and then have it executed by the cmd ioctl (asyncronously).

  cmdtest returns 1,2,3,4 or 0, depending on which tests
  the command passes.
*/

static int rtd_ai_cmdtest(struct comedi_device *dev,
			  struct comedi_subdevice *s, struct comedi_cmd *cmd)
{
	int err = 0;
	int tmp;

	/* step 1: make sure trigger sources are trivially valid */

	tmp = cmd->start_src;
	cmd->start_src &= TRIG_NOW;
	if (!cmd->start_src || tmp != cmd->start_src)
		err++;

	tmp = cmd->scan_begin_src;
	cmd->scan_begin_src &= TRIG_TIMER | TRIG_EXT;
	if (!cmd->scan_begin_src || tmp != cmd->scan_begin_src)
		err++;


	tmp = cmd->convert_src;
	cmd->convert_src &= TRIG_TIMER | TRIG_EXT;
	if (!cmd->convert_src || tmp != cmd->convert_src)
		err++;


	tmp = cmd->scan_end_src;
	cmd->scan_end_src &= TRIG_COUNT;
	if (!cmd->scan_end_src || tmp != cmd->scan_end_src)
		err++;


	tmp = cmd->stop_src;
	cmd->stop_src &= TRIG_COUNT | TRIG_NONE;
	if (!cmd->stop_src || tmp != cmd->stop_src)
		err++;


	if (err)
		return 1;

	/* step 2: make sure trigger sources are unique
	   and mutually compatible */
	/* note that mutual compatibility is not an issue here */
	if (cmd->scan_begin_src != TRIG_TIMER &&
	    cmd->scan_begin_src != TRIG_EXT) {
		err++;
	}
	if (cmd->convert_src != TRIG_TIMER && cmd->convert_src != TRIG_EXT)
		err++;

	if (cmd->stop_src != TRIG_COUNT && cmd->stop_src != TRIG_NONE)
		err++;

	if (err)
		return 2;

	/* step 3: make sure arguments are trivially compatible */

	if (cmd->start_arg != 0) {
		cmd->start_arg = 0;
		err++;
	}

	if (cmd->scan_begin_src == TRIG_TIMER) {
		/* Note: these are time periods, not actual rates */
		if (1 == cmd->chanlist_len) {	/* no scanning */
			if (cmd->scan_begin_arg < RTD_MAX_SPEED_1) {
				cmd->scan_begin_arg = RTD_MAX_SPEED_1;
				rtd_ns_to_timer(&cmd->scan_begin_arg,
						TRIG_ROUND_UP);
				err++;
			}
			if (cmd->scan_begin_arg > RTD_MIN_SPEED_1) {
				cmd->scan_begin_arg = RTD_MIN_SPEED_1;
				rtd_ns_to_timer(&cmd->scan_begin_arg,
						TRIG_ROUND_DOWN);
				err++;
			}
		} else {
			if (cmd->scan_begin_arg < RTD_MAX_SPEED) {
				cmd->scan_begin_arg = RTD_MAX_SPEED;
				rtd_ns_to_timer(&cmd->scan_begin_arg,
						TRIG_ROUND_UP);
				err++;
			}
			if (cmd->scan_begin_arg > RTD_MIN_SPEED) {
				cmd->scan_begin_arg = RTD_MIN_SPEED;
				rtd_ns_to_timer(&cmd->scan_begin_arg,
						TRIG_ROUND_DOWN);
				err++;
			}
		}
	} else {
		/* external trigger */
		/* should be level/edge, hi/lo specification here */
		/* should specify multiple external triggers */
		if (cmd->scan_begin_arg > 9) {
			cmd->scan_begin_arg = 9;
			err++;
		}
	}
	if (cmd->convert_src == TRIG_TIMER) {
		if (1 == cmd->chanlist_len) {	/* no scanning */
			if (cmd->convert_arg < RTD_MAX_SPEED_1) {
				cmd->convert_arg = RTD_MAX_SPEED_1;
				rtd_ns_to_timer(&cmd->convert_arg,
						TRIG_ROUND_UP);
				err++;
			}
			if (cmd->convert_arg > RTD_MIN_SPEED_1) {
				cmd->convert_arg = RTD_MIN_SPEED_1;
				rtd_ns_to_timer(&cmd->convert_arg,
						TRIG_ROUND_DOWN);
				err++;
			}
		} else {
			if (cmd->convert_arg < RTD_MAX_SPEED) {
				cmd->convert_arg = RTD_MAX_SPEED;
				rtd_ns_to_timer(&cmd->convert_arg,
						TRIG_ROUND_UP);
				err++;
			}
			if (cmd->convert_arg > RTD_MIN_SPEED) {
				cmd->convert_arg = RTD_MIN_SPEED;
				rtd_ns_to_timer(&cmd->convert_arg,
						TRIG_ROUND_DOWN);
				err++;
			}
		}
	} else {
		/* external trigger */
		/* see above */
		if (cmd->convert_arg > 9) {
			cmd->convert_arg = 9;
			err++;
		}
	}

#if 0
	if (cmd->scan_end_arg != cmd->chanlist_len) {
		cmd->scan_end_arg = cmd->chanlist_len;
		err++;
	}
#endif
	if (cmd->stop_src == TRIG_COUNT) {
		/* TODO check for rounding error due to counter wrap */

	} else {
		/* TRIG_NONE */
		if (cmd->stop_arg != 0) {
			cmd->stop_arg = 0;
			err++;
		}
	}

	if (err)
		return 3;


	/* step 4: fix up any arguments */

	if (cmd->chanlist_len > RTD_MAX_CHANLIST) {
		cmd->chanlist_len = RTD_MAX_CHANLIST;
		err++;
	}
	if (cmd->scan_begin_src == TRIG_TIMER) {
		tmp = cmd->scan_begin_arg;
		rtd_ns_to_timer(&cmd->scan_begin_arg,
				cmd->flags & TRIG_ROUND_MASK);
		if (tmp != cmd->scan_begin_arg)
			err++;

	}
	if (cmd->convert_src == TRIG_TIMER) {
		tmp = cmd->convert_arg;
		rtd_ns_to_timer(&cmd->convert_arg,
				cmd->flags & TRIG_ROUND_MASK);
		if (tmp != cmd->convert_arg)
			err++;

		if (cmd->scan_begin_src == TRIG_TIMER
		    && (cmd->scan_begin_arg
			< (cmd->convert_arg * cmd->scan_end_arg))) {
			cmd->scan_begin_arg =
			    cmd->convert_arg * cmd->scan_end_arg;
			err++;
		}
	}

	if (err)
		return 4;

	return 0;
}

/*
  Execute a analog in command with many possible triggering options.
  The data get stored in the async structure of the subdevice.
  This is usually done by an interrupt handler.
  Userland gets to the data using read calls.
*/
static int rtd_ai_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct rtdPrivate *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	int timer;

	/* stop anything currently running */
	/* pacer stop source: SOFTWARE */
	writel(0, devpriv->las0 + LAS0_PACER_STOP);
	writel(0, devpriv->las0 + LAS0_PACER);	/* stop pacer */
	writel(0, devpriv->las0 + LAS0_ADC_CONVERSION);
	devpriv->intMask = 0;
	writew(devpriv->intMask, devpriv->las0 + LAS0_IT);
#ifdef USE_DMA
	if (devpriv->flags & DMA0_ACTIVE) {	/* cancel anything running */
		writel(readl(devpriv->lcfg + LCFG_ITCSR) & ~ICS_DMA0_E,
			devpriv->lcfg + LCFG_ITCSR);
		abort_dma(dev, 0);
		devpriv->flags &= ~DMA0_ACTIVE;
		if (readl(devpriv->lcfg + LCFG_ITCSR) & ICS_DMA0_A) {
			devpriv->dma0Control = PLX_CLEAR_DMA_INTR_BIT;
			writeb(devpriv->dma0Control,
				devpriv->lcfg + LCFG_DMACSR0);
		}
	}
	writel(0, devpriv->las0 + LAS0_DMA0_RESET);
#endif /* USE_DMA */
	writel(0, devpriv->las0 + LAS0_ADC_FIFO_CLEAR);
	writel(0, devpriv->las0 + LAS0_OVERRUN);
	devpriv->intCount = 0;

	if (!dev->irq) {	/* we need interrupts for this */
		DPRINTK("rtd520: ERROR! No interrupt available!\n");
		return -ENXIO;
	}

	/* start configuration */
	/* load channel list and reset CGT */
	rtd_load_channelgain_list(dev, cmd->chanlist_len, cmd->chanlist);

	/* setup the common case and override if needed */
	if (cmd->chanlist_len > 1) {
		/*DPRINTK ("rtd520: Multi channel setup\n"); */
		/* pacer start source: SOFTWARE */
		writel(0, devpriv->las0 + LAS0_PACER_START);
		/* burst trigger source: PACER */
		writel(1, devpriv->las0 + LAS0_BURST_START);
		/* ADC conversion trigger source: BURST */
		writel(2, devpriv->las0 + LAS0_ADC_CONVERSION);
	} else {		/* single channel */
		/*DPRINTK ("rtd520: single channel setup\n"); */
		/* pacer start source: SOFTWARE */
		writel(0, devpriv->las0 + LAS0_PACER_START);
		/* ADC conversion trigger source: PACER */
		writel(1, devpriv->las0 + LAS0_ADC_CONVERSION);
	}
	writel((devpriv->fifoLen / 2 - 1) & 0xffff, devpriv->las0 + LAS0_ACNT);

	if (TRIG_TIMER == cmd->scan_begin_src) {
		/* scan_begin_arg is in nanoseconds */
		/* find out how many samples to wait before transferring */
		if (cmd->flags & TRIG_WAKE_EOS) {
			/* this may generate un-sustainable interrupt rates */
			/* the application is responsible for doing the right thing */
			devpriv->transCount = cmd->chanlist_len;
			devpriv->flags |= SEND_EOS;
		} else {
			/* arrange to transfer data periodically */
			devpriv->transCount
			    =
			    (TRANS_TARGET_PERIOD * cmd->chanlist_len) /
			    cmd->scan_begin_arg;
			if (devpriv->transCount < cmd->chanlist_len) {
				/* transfer after each scan (and avoid 0) */
				devpriv->transCount = cmd->chanlist_len;
			} else {	/* make a multiple of scan length */
				devpriv->transCount =
				    (devpriv->transCount +
				     cmd->chanlist_len - 1)
				    / cmd->chanlist_len;
				devpriv->transCount *= cmd->chanlist_len;
			}
			devpriv->flags |= SEND_EOS;
		}
		if (devpriv->transCount >= (devpriv->fifoLen / 2)) {
			/* out of counter range, use 1/2 fifo instead */
			devpriv->transCount = 0;
			devpriv->flags &= ~SEND_EOS;
		} else {
			/* interrupt for each transfer */
			writel((devpriv->transCount - 1) & 0xffff,
				devpriv->las0 + LAS0_ACNT);
		}

		DPRINTK
		    ("rtd520: scanLen=%d transferCount=%d fifoLen=%d\n  scanTime(ns)=%d flags=0x%x\n",
		     cmd->chanlist_len, devpriv->transCount, devpriv->fifoLen,
		     cmd->scan_begin_arg, devpriv->flags);
	} else {		/* unknown timing, just use 1/2 FIFO */
		devpriv->transCount = 0;
		devpriv->flags &= ~SEND_EOS;
	}
	/* pacer clock source: INTERNAL 8MHz */
	writel(1, devpriv->las0 + LAS0_PACER_SELECT);
	/* just interrupt, don't stop */
	writel(1, devpriv->las0 + LAS0_ACNT_STOP_ENABLE);

	/* BUG??? these look like enumerated values, but they are bit fields */

	/* First, setup when to stop */
	switch (cmd->stop_src) {
	case TRIG_COUNT:	/* stop after N scans */
		devpriv->aiCount = cmd->stop_arg * cmd->chanlist_len;
		if ((devpriv->transCount > 0)
		    && (devpriv->transCount > devpriv->aiCount)) {
			devpriv->transCount = devpriv->aiCount;
		}
		break;

	case TRIG_NONE:	/* stop when cancel is called */
		devpriv->aiCount = -1;	/* read forever */
		break;

	default:
		DPRINTK("rtd520: Warning! ignoring stop_src mode %d\n",
			cmd->stop_src);
	}

	/* Scan timing */
	switch (cmd->scan_begin_src) {
	case TRIG_TIMER:	/* periodic scanning */
		timer = rtd_ns_to_timer(&cmd->scan_begin_arg,
					TRIG_ROUND_NEAREST);
		/* set PACER clock */
		/*DPRINTK ("rtd520: loading %d into pacer\n", timer); */
		writel(timer & 0xffffff, devpriv->las0 + LAS0_PCLK);

		break;

	case TRIG_EXT:
		/* pacer start source: EXTERNAL */
		writel(1, devpriv->las0 + LAS0_PACER_START);
		break;

	default:
		DPRINTK("rtd520: Warning! ignoring scan_begin_src mode %d\n",
			cmd->scan_begin_src);
	}

	/* Sample timing within a scan */
	switch (cmd->convert_src) {
	case TRIG_TIMER:	/* periodic */
		if (cmd->chanlist_len > 1) {	/* only needed for multi-channel */
			timer = rtd_ns_to_timer(&cmd->convert_arg,
						TRIG_ROUND_NEAREST);
			/* setup BURST clock */
			/*DPRINTK ("rtd520: loading %d into burst\n", timer); */
			writel(timer & 0x3ff, devpriv->las0 + LAS0_BCLK);
		}

		break;

	case TRIG_EXT:		/* external */
		/* burst trigger source: EXTERNAL */
		writel(2, devpriv->las0 + LAS0_BURST_START);
		break;

	default:
		DPRINTK("rtd520: Warning! ignoring convert_src mode %d\n",
			cmd->convert_src);
	}
	/* end configuration */

	/* This doesn't seem to work.  There is no way to clear an interrupt
	   that the priority controller has queued! */
	devpriv->intClearMask = ~0;
	writew(devpriv->intClearMask, devpriv->las0 + LAS0_CLEAR);
	readw(devpriv->las0 + LAS0_CLEAR);

	/* TODO: allow multiple interrupt sources */
	if (devpriv->transCount > 0) {	/* transfer every N samples */
		devpriv->intMask = IRQM_ADC_ABOUT_CNT;
		writew(devpriv->intMask, devpriv->las0 + LAS0_IT);
		DPRINTK("rtd520: Transferring every %d\n", devpriv->transCount);
	} else {		/* 1/2 FIFO transfers */
#ifdef USE_DMA
		devpriv->flags |= DMA0_ACTIVE;

		/* point to first transfer in ring */
		devpriv->dma0Offset = 0;
		writel(DMA_MODE_BITS, devpriv->lcfg + LCFG_DMAMODE0);
		/* point to first block */
		writel(devpriv->dma0Chain[DMA_CHAIN_COUNT - 1].next,
			devpriv->lcfg + LCFG_DMADPR0);
		writel(DMAS_ADFIFO_HALF_FULL, devpriv->las0 + LAS0_DMA0_SRC);
		writel(readl(devpriv->lcfg + LCFG_ITCSR) | ICS_DMA0_E,
			devpriv->lcfg + LCFG_ITCSR);
		/* Must be 2 steps.  See PLX app note about "Starting a DMA transfer" */
		devpriv->dma0Control = PLX_DMA_EN_BIT;
		writeb(devpriv->dma0Control,
			devpriv->lcfg + LCFG_DMACSR0);
		devpriv->dma0Control |= PLX_DMA_START_BIT;
		writeb(devpriv->dma0Control,
			devpriv->lcfg + LCFG_DMACSR0);
		DPRINTK("rtd520: Using DMA0 transfers. plxInt %x RtdInt %x\n",
			readl(devpriv->lcfg + LCFG_ITCSR), devpriv->intMask);
#else /* USE_DMA */
		devpriv->intMask = IRQM_ADC_ABOUT_CNT;
		writew(devpriv->intMask, devpriv->las0 + LAS0_IT);
		DPRINTK("rtd520: Transferring every 1/2 FIFO\n");
#endif /* USE_DMA */
	}

	/* BUG: start_src is ASSUMED to be TRIG_NOW */
	/* BUG? it seems like things are running before the "start" */
	readl(devpriv->las0 + LAS0_PACER);	/* start pacer */
	return 0;
}

/*
  Stop a running data acquisition.
*/
static int rtd_ai_cancel(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct rtdPrivate *devpriv = dev->private;
	u32 overrun;
	u16 status;

	/* pacer stop source: SOFTWARE */
	writel(0, devpriv->las0 + LAS0_PACER_STOP);
	writel(0, devpriv->las0 + LAS0_PACER);	/* stop pacer */
	writel(0, devpriv->las0 + LAS0_ADC_CONVERSION);
	devpriv->intMask = 0;
	writew(devpriv->intMask, devpriv->las0 + LAS0_IT);
	devpriv->aiCount = 0;	/* stop and don't transfer any more */
#ifdef USE_DMA
	if (devpriv->flags & DMA0_ACTIVE) {
		writel(readl(devpriv->lcfg + LCFG_ITCSR) & ~ICS_DMA0_E,
			devpriv->lcfg + LCFG_ITCSR);
		abort_dma(dev, 0);
		devpriv->flags &= ~DMA0_ACTIVE;
	}
#endif /* USE_DMA */
	status = readw(devpriv->las0 + LAS0_IT);
	overrun = readl(devpriv->las0 + LAS0_OVERRUN) & 0xffff;
	DPRINTK
	    ("rtd520: Acquisition canceled. %ld ints, intStat=%x, overStat=%x\n",
	     devpriv->intCount, status, overrun);
	return 0;
}

/*
  Output one (or more) analog values to a single port as fast as possible.
*/
static int rtd_ao_winsn(struct comedi_device *dev,
			struct comedi_subdevice *s, struct comedi_insn *insn,
			unsigned int *data)
{
	struct rtdPrivate *devpriv = dev->private;
	int i;
	int chan = CR_CHAN(insn->chanspec);
	int range = CR_RANGE(insn->chanspec);

	/* Configure the output range (table index matches the range values) */
	writew(range & 7, devpriv->las0 +
		((chan == 0) ? LAS0_DAC1_CTRL : LAS0_DAC2_CTRL));

	/* Writing a list of values to an AO channel is probably not
	 * very useful, but that's how the interface is defined. */
	for (i = 0; i < insn->n; ++i) {
		int val = data[i] << 3;
		int stat = 0;	/* initialize to avoid bogus warning */
		int ii;

		/* VERIFY: comedi range and offset conversions */

		if ((range > 1)	/* bipolar */
		    && (data[i] < 2048)) {
			/* offset and sign extend */
			val = (((int)data[i]) - 2048) << 3;
		} else {	/* unipolor */
			val = data[i] << 3;
		}

		DPRINTK
		    ("comedi: rtd520 DAC chan=%d range=%d writing %d as 0x%x\n",
		     chan, range, data[i], val);

		/* a typical programming sequence */
		writew(val, devpriv->las1 +
			((chan == 0) ? LAS1_DAC1_FIFO : LAS1_DAC2_FIFO));
		writew(0, devpriv->las0 + ((chan == 0) ? LAS0_DAC1 : LAS0_DAC2));

		devpriv->aoValue[chan] = data[i];	/* save for read back */

		for (ii = 0; ii < RTD_DAC_TIMEOUT; ++ii) {
			stat = readl(devpriv->las0 + LAS0_ADC);
			/* 1 -> not empty */
			if (stat & ((0 == chan) ? FS_DAC1_NOT_EMPTY :
				    FS_DAC2_NOT_EMPTY))
				break;
			WAIT_QUIETLY;
		}
		if (ii >= RTD_DAC_TIMEOUT) {
			DPRINTK
			    ("rtd520: Error: DAC never finished! FifoStatus=0x%x\n",
			     stat ^ 0x6666);
			return -ETIMEDOUT;
		}
	}

	/* return the number of samples read/written */
	return i;
}

/* AO subdevices should have a read insn as well as a write insn.
 * Usually this means copying a value stored in devpriv. */
static int rtd_ao_rinsn(struct comedi_device *dev,
			struct comedi_subdevice *s, struct comedi_insn *insn,
			unsigned int *data)
{
	struct rtdPrivate *devpriv = dev->private;
	int i;
	int chan = CR_CHAN(insn->chanspec);

	for (i = 0; i < insn->n; i++)
		data[i] = devpriv->aoValue[chan];


	return i;
}

/*
   Write a masked set of bits and the read back the port.
   We track what the bits should be (i.e. we don't read the port first).

   DIO devices are slightly special.  Although it is possible to
 * implement the insn_read/insn_write interface, it is much more
 * useful to applications if you implement the insn_bits interface.
 * This allows packed reading/writing of the DIO channels.  The
 * comedi core can convert between insn_bits and insn_read/write
 */
static int rtd_dio_insn_bits(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     struct comedi_insn *insn, unsigned int *data)
{
	struct rtdPrivate *devpriv = dev->private;

	/* The insn data is a mask in data[0] and the new data
	 * in data[1], each channel cooresponding to a bit. */
	if (data[0]) {
		s->state &= ~data[0];
		s->state |= data[0] & data[1];

		/* Write out the new digital output lines */
		writew(s->state & 0xff, devpriv->las0 + LAS0_DIO0);
	}
	/* on return, data[1] contains the value of the digital
	 * input lines. */
	data[1] = readw(devpriv->las0 + LAS0_DIO0) & 0xff;

	/*DPRINTK("rtd520:port_0 wrote: 0x%x read: 0x%x\n", s->state, data[1]); */

	return insn->n;
}

/*
  Configure one bit on a IO port as Input or Output (hence the name :-).
*/
static int rtd_dio_insn_config(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{
	struct rtdPrivate *devpriv = dev->private;
	int chan = CR_CHAN(insn->chanspec);

	/* The input or output configuration of each digital line is
	 * configured by a special insn_config instruction.  chanspec
	 * contains the channel to be changed, and data[0] contains the
	 * value COMEDI_INPUT or COMEDI_OUTPUT. */
	switch (data[0]) {
	case INSN_CONFIG_DIO_OUTPUT:
		s->io_bits |= 1 << chan;	/* 1 means Out */
		break;
	case INSN_CONFIG_DIO_INPUT:
		s->io_bits &= ~(1 << chan);
		break;
	case INSN_CONFIG_DIO_QUERY:
		data[1] =
		    (s->io_bits & (1 << chan)) ? COMEDI_OUTPUT : COMEDI_INPUT;
		return insn->n;
		break;
	default:
		return -EINVAL;
	}

	DPRINTK("rtd520: port_0_direction=0x%x (1 means out)\n", s->io_bits);
	/* TODO support digital match interrupts and strobes */
	devpriv->dioStatus = 0x01;	/* set direction */
	writew(devpriv->dioStatus, devpriv->las0 + LAS0_DIO_STATUS);
	writew(s->io_bits & 0xff, devpriv->las0 + LAS0_DIO0_CTRL);
	devpriv->dioStatus = 0x00;	/* clear interrupts */
	writew(devpriv->dioStatus, devpriv->las0 + LAS0_DIO_STATUS);

	/* port1 can only be all input or all output */

	/* there are also 2 user input lines and 2 user output lines */

	return 1;
}

static struct pci_dev *rtd_find_pci(struct comedi_device *dev,
				    struct comedi_devconfig *it)
{
	const struct rtdBoard *thisboard;
	struct pci_dev *pcidev = NULL;
	int bus = it->options[0];
	int slot = it->options[1];
	int i;

	for_each_pci_dev(pcidev) {
		if (pcidev->vendor != PCI_VENDOR_ID_RTD)
			continue;
		if (bus || slot) {
			if (pcidev->bus->number != bus ||
			    PCI_SLOT(pcidev->devfn) != slot)
				continue;
		}
		for (i = 0; i < ARRAY_SIZE(rtd520Boards); i++) {
			thisboard = &rtd520Boards[i];
			if (pcidev->device == thisboard->device_id) {
				dev->board_ptr = thisboard;
				return pcidev;
			}
		}
	}
	dev_warn(dev->class_dev,
		"no supported board found! (req. bus/slot: %d/%d)\n",
		bus, slot);
	return NULL;
}

static int rtd_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{				/* board name and options flags */
	const struct rtdBoard *thisboard;
	struct rtdPrivate *devpriv;
	struct pci_dev *pcidev;
	struct comedi_subdevice *s;
	int ret;
	resource_size_t physLas1;	/* data area */
	resource_size_t physLcfg;	/* PLX9080 */
#ifdef USE_DMA
	int index;
#endif

	printk(KERN_INFO "comedi%d: rtd520 attaching.\n", dev->minor);

#if defined(CONFIG_COMEDI_DEBUG) && defined(USE_DMA)
	/* You can set this a load time: modprobe comedi comedi_debug=1 */
	if (0 == comedi_debug)	/* force DMA debug printks */
		comedi_debug = 1;
#endif

	/*
	 * Allocate the private structure area.  alloc_private() is a
	 * convenient macro defined in comedidev.h.
	 */
	if (alloc_private(dev, sizeof(struct rtdPrivate)) < 0)
		return -ENOMEM;
	devpriv = dev->private;

	pcidev = rtd_find_pci(dev, it);
	if (!pcidev)
		return -EIO;
	comedi_set_hw_dev(dev, &pcidev->dev);
	thisboard = comedi_board(dev);

	dev->board_name = thisboard->name;

	ret = comedi_pci_enable(pcidev, DRV_NAME);
	if (ret < 0) {
		printk(KERN_INFO "Failed to enable PCI device and request regions.\n");
		return ret;
	}

	/*
	 * Initialize base addresses
	 */
	/* Get the physical address from PCI config */
	dev->iobase = pci_resource_start(pcidev, LAS0_PCIINDEX);
	physLas1 = pci_resource_start(pcidev, LAS1_PCIINDEX);
	physLcfg = pci_resource_start(pcidev, LCFG_PCIINDEX);
	/* Now have the kernel map this into memory */
	/* ASSUME page aligned */
	devpriv->las0 = ioremap_nocache(dev->iobase, LAS0_PCISIZE);
	devpriv->las1 = ioremap_nocache(physLas1, LAS1_PCISIZE);
	devpriv->lcfg = ioremap_nocache(physLcfg, LCFG_PCISIZE);

	if (!devpriv->las0 || !devpriv->las1 || !devpriv->lcfg)
		return -ENOMEM;

	{			/* The RTD driver does this */
		unsigned char pci_latency;
		u16 revision;
		/*uint32_t epld_version; */

		pci_read_config_word(pcidev, PCI_REVISION_ID,
				     &revision);
		DPRINTK("%s: PCI revision %d.\n", dev->board_name, revision);

		pci_read_config_byte(pcidev,
				     PCI_LATENCY_TIMER, &pci_latency);
		if (pci_latency < 32) {
			printk(KERN_INFO "%s: PCI latency changed from %d to %d\n",
			       dev->board_name, pci_latency, 32);
			pci_write_config_byte(pcidev,
					      PCI_LATENCY_TIMER, 32);
		} else {
			DPRINTK("rtd520: PCI latency = %d\n", pci_latency);
		}

		/*
		 * Undocumented EPLD version (doesn't match RTD driver results)
		 */
		/*DPRINTK ("rtd520: Reading epld from %p\n",
		   devpriv->las0+0);
		   epld_version = readl (devpriv->las0+0);
		   if ((epld_version & 0xF0) >> 4 == 0x0F) {
		   DPRINTK("rtd520: pre-v8 EPLD. (%x)\n", epld_version);
		   } else {
		   DPRINTK("rtd520: EPLD version %x.\n", epld_version >> 4);
		   } */
	}

	/* Show board configuration */
	printk(KERN_INFO "%s:", dev->board_name);

	ret = comedi_alloc_subdevices(dev, 4);
	if (ret)
		return ret;

	s = dev->subdevices + 0;
	dev->read_subdev = s;
	/* analog input subdevice */
	s->type = COMEDI_SUBD_AI;
	s->subdev_flags =
	    SDF_READABLE | SDF_GROUND | SDF_COMMON | SDF_DIFF | SDF_CMD_READ;
	s->n_chan = thisboard->aiChans;
	s->maxdata = (1 << thisboard->aiBits) - 1;
	if (thisboard->aiMaxGain <= 32)
		s->range_table = &rtd_ai_7520_range;
	else
		s->range_table = &rtd_ai_4520_range;

	s->len_chanlist = RTD_MAX_CHANLIST;	/* devpriv->fifoLen */
	s->insn_read = rtd_ai_rinsn;
	s->do_cmd = rtd_ai_cmd;
	s->do_cmdtest = rtd_ai_cmdtest;
	s->cancel = rtd_ai_cancel;
	/* s->poll = rtd_ai_poll; *//* not ready yet */

	s = dev->subdevices + 1;
	/* analog output subdevice */
	s->type = COMEDI_SUBD_AO;
	s->subdev_flags = SDF_WRITABLE;
	s->n_chan = 2;
	s->maxdata = (1 << thisboard->aiBits) - 1;
	s->range_table = &rtd_ao_range;
	s->insn_write = rtd_ao_winsn;
	s->insn_read = rtd_ao_rinsn;

	s = dev->subdevices + 2;
	/* digital i/o subdevice */
	s->type = COMEDI_SUBD_DIO;
	s->subdev_flags = SDF_READABLE | SDF_WRITABLE;
	/* we only support port 0 right now.  Ignoring port 1 and user IO */
	s->n_chan = 8;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->insn_bits = rtd_dio_insn_bits;
	s->insn_config = rtd_dio_insn_config;

	/* timer/counter subdevices (not currently supported) */
	s = dev->subdevices + 3;
	s->type = COMEDI_SUBD_COUNTER;
	s->subdev_flags = SDF_READABLE | SDF_WRITABLE;
	s->n_chan = 3;
	s->maxdata = 0xffff;

	/* initialize board, per RTD spec */
	/* also, initialize shadow registers */
	writel(0, devpriv->las0 + LAS0_BOARD_RESET);
	udelay(100);		/* needed? */
	writel(0, devpriv->lcfg + LCFG_ITCSR);
	devpriv->intMask = 0;
	writew(devpriv->intMask, devpriv->las0 + LAS0_IT);
	devpriv->intClearMask = ~0;
	writew(devpriv->intClearMask, devpriv->las0 + LAS0_CLEAR);
	readw(devpriv->las0 + LAS0_CLEAR);
	writel(0, devpriv->las0 + LAS0_OVERRUN);
	writel(0, devpriv->las0 + LAS0_CGT_CLEAR);
	writel(0, devpriv->las0 + LAS0_ADC_FIFO_CLEAR);
	writel(0, devpriv->las0 + LAS0_DAC1_RESET);
	writel(0, devpriv->las0 + LAS0_DAC2_RESET);
	/* clear digital IO fifo */
	devpriv->dioStatus = 0;
	writew(devpriv->dioStatus, devpriv->las0 + LAS0_DIO_STATUS);
	devpriv->utcCtrl[0] = (0 << 6) | 0x30;
	devpriv->utcCtrl[1] = (1 << 6) | 0x30;
	devpriv->utcCtrl[2] = (2 << 6) | 0x30;
	devpriv->utcCtrl[3] = (3 << 6) | 0x00;
	writeb(devpriv->utcCtrl[0], devpriv->las0 + LAS0_UTC_CTRL);
	writeb(devpriv->utcCtrl[1], devpriv->las0 + LAS0_UTC_CTRL);
	writeb(devpriv->utcCtrl[2], devpriv->las0 + LAS0_UTC_CTRL);
	writeb(devpriv->utcCtrl[3], devpriv->las0 + LAS0_UTC_CTRL);
	/* TODO: set user out source ??? */

	/* check if our interrupt is available and get it */
	ret = request_irq(pcidev->irq, rtd_interrupt,
			  IRQF_SHARED, DRV_NAME, dev);

	if (ret < 0) {
		printk("Could not get interrupt! (%u)\n",
		       pcidev->irq);
		return ret;
	}
	dev->irq = pcidev->irq;
	printk(KERN_INFO "( irq=%u )", dev->irq);

	ret = rtd520_probe_fifo_depth(dev);
	if (ret < 0)
		return ret;

	devpriv->fifoLen = ret;
	printk("( fifoLen=%d )", devpriv->fifoLen);

#ifdef USE_DMA
	if (dev->irq > 0) {
		printk("( DMA buff=%d )\n", DMA_CHAIN_COUNT);
		/*
		 * The PLX9080 has 2 DMA controllers, but there could be
		 * 4 sources: ADC, digital, DAC1, and DAC2.  Since only the
		 * ADC supports cmd mode right now, this isn't an issue (yet)
		 */
		devpriv->dma0Offset = 0;

		for (index = 0; index < DMA_CHAIN_COUNT; index++) {
			devpriv->dma0Buff[index] =
			    pci_alloc_consistent(pcidev,
						 sizeof(u16) *
						 devpriv->fifoLen / 2,
						 &devpriv->
						 dma0BuffPhysAddr[index]);
			if (devpriv->dma0Buff[index] == NULL) {
				ret = -ENOMEM;
				goto rtd_attach_die_error;
			}
			/*DPRINTK ("buff[%d] @ %p virtual, %x PCI\n",
			   index,
			   devpriv->dma0Buff[index],
			   devpriv->dma0BuffPhysAddr[index]); */
		}

		/*
		 * setup DMA descriptor ring (use cpu_to_le32 for byte
		 * ordering?)
		 */
		devpriv->dma0Chain =
		    pci_alloc_consistent(pcidev,
					 sizeof(struct plx_dma_desc) *
					 DMA_CHAIN_COUNT,
					 &devpriv->dma0ChainPhysAddr);
		for (index = 0; index < DMA_CHAIN_COUNT; index++) {
			devpriv->dma0Chain[index].pci_start_addr =
			    devpriv->dma0BuffPhysAddr[index];
			devpriv->dma0Chain[index].local_start_addr =
			    DMALADDR_ADC;
			devpriv->dma0Chain[index].transfer_size =
			    sizeof(u16) * devpriv->fifoLen / 2;
			devpriv->dma0Chain[index].next =
			    (devpriv->dma0ChainPhysAddr + ((index +
							    1) %
							   (DMA_CHAIN_COUNT))
			     * sizeof(devpriv->dma0Chain[0]))
			    | DMA_TRANSFER_BITS;
			/*DPRINTK ("ring[%d] @%lx PCI: %x, local: %x, N: 0x%x, next: %x\n",
			   index,
			   ((long)devpriv->dma0ChainPhysAddr
			   + (index * sizeof(devpriv->dma0Chain[0]))),
			   devpriv->dma0Chain[index].pci_start_addr,
			   devpriv->dma0Chain[index].local_start_addr,
			   devpriv->dma0Chain[index].transfer_size,
			   devpriv->dma0Chain[index].next); */
		}

		if (devpriv->dma0Chain == NULL) {
			ret = -ENOMEM;
			goto rtd_attach_die_error;
		}

		writel(DMA_MODE_BITS, devpriv->lcfg + LCFG_DMAMODE0);
		/* set DMA trigger source */
		writel(DMAS_ADFIFO_HALF_FULL, devpriv->las0 + LAS0_DMA0_SRC);
	} else {
		printk(KERN_INFO "( no IRQ->no DMA )");
	}
#endif /* USE_DMA */

	if (dev->irq)
		writel(ICS_PIE | ICS_PLIE, devpriv->lcfg + LCFG_ITCSR);

	printk("\ncomedi%d: rtd520 driver attached.\n", dev->minor);

	return 1;
}

static void rtd_detach(struct comedi_device *dev)
{
	struct rtdPrivate *devpriv = dev->private;
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
#ifdef USE_DMA
	int index;
#endif

	if (devpriv) {
		/* Shut down any board ops by resetting it */
#ifdef USE_DMA
		if (devpriv->lcfg) {
			devpriv->dma0Control = 0;
			devpriv->dma1Control = 0;
			writeb(devpriv->dma0Control,
				devpriv->lcfg + LCFG_DMACSR0);
			writeb(devpriv->dma1Control,
				devpriv->lcfg + LCFG_DMACSR1);
			writel(ICS_PIE | ICS_PLIE, devpriv->lcfg + LCFG_ITCSR);
		}
#endif /* USE_DMA */
		if (devpriv->las0) {
			writel(0, devpriv->las0 + LAS0_BOARD_RESET);
			devpriv->intMask = 0;
			writew(devpriv->intMask, devpriv->las0 + LAS0_IT);
			devpriv->intClearMask = ~0;
			writew(devpriv->intClearMask,
				devpriv->las0 + LAS0_CLEAR);
			readw(devpriv->las0 + LAS0_CLEAR);
		}
#ifdef USE_DMA
		/* release DMA */
		for (index = 0; index < DMA_CHAIN_COUNT; index++) {
			if (NULL != devpriv->dma0Buff[index]) {
				pci_free_consistent(pcidev,
						    sizeof(u16) *
						    devpriv->fifoLen / 2,
						    devpriv->dma0Buff[index],
						    devpriv->
						    dma0BuffPhysAddr[index]);
				devpriv->dma0Buff[index] = NULL;
			}
		}
		if (NULL != devpriv->dma0Chain) {
			pci_free_consistent(pcidev,
					    sizeof(struct plx_dma_desc) *
					    DMA_CHAIN_COUNT, devpriv->dma0Chain,
					    devpriv->dma0ChainPhysAddr);
			devpriv->dma0Chain = NULL;
		}
#endif /* USE_DMA */
		if (dev->irq) {
			writel(readl(devpriv->lcfg + LCFG_ITCSR) &
				~(ICS_PLIE | ICS_DMA0_E | ICS_DMA1_E),
				devpriv->lcfg + LCFG_ITCSR);
			free_irq(dev->irq, dev);
		}
		if (devpriv->las0)
			iounmap(devpriv->las0);
		if (devpriv->las1)
			iounmap(devpriv->las1);
		if (devpriv->lcfg)
			iounmap(devpriv->lcfg);
	}
	if (pcidev) {
		if (dev->iobase)
			comedi_pci_disable(pcidev);
		pci_dev_put(pcidev);
	}
}

static struct comedi_driver rtd520_driver = {
	.driver_name	= "rtd520",
	.module		= THIS_MODULE,
	.attach		= rtd_attach,
	.detach		= rtd_detach,
};

static int __devinit rtd520_pci_probe(struct pci_dev *dev,
				      const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, &rtd520_driver);
}

static void __devexit rtd520_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static DEFINE_PCI_DEVICE_TABLE(rtd520_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_RTD, 0x7520) },
	{ PCI_DEVICE(PCI_VENDOR_ID_RTD, 0x4520) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, rtd520_pci_table);

static struct pci_driver rtd520_pci_driver = {
	.name		= "rtd520",
	.id_table	= rtd520_pci_table,
	.probe		= rtd520_pci_probe,
	.remove		= __devexit_p(rtd520_pci_remove),
};
module_comedi_pci_driver(rtd520_driver, rtd520_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
