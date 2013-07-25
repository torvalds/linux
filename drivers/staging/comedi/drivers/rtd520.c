/*
 * comedi/drivers/rtd520.c
 * Comedi driver for Real Time Devices (RTD) PCI4520/DM7520
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 2001 David A. Schleef <ds@schleef.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

/*
 * Driver: rtd520
 * Description: Real Time Devices PCI4520/DM7520
 * Devices: (Real Time Devices) DM7520HR-1 [DM7520]
 *	    (Real Time Devices) DM7520HR-8 [DM7520]
 *	    (Real Time Devices) PCI4520 [PCI4520]
 *	    (Real Time Devices) PCI4520-8 [PCI4520]
 * Author: Dan Christian
 * Status: Works. Only tested on DM7520-8. Not SMP safe.
 *
 * Configuration options: not applicable, uses PCI auto config
 */

/*
 * Created by Dan Christian, NASA Ames Research Center.
 *
 * The PCI4520 is a PCI card. The DM7520 is a PC/104-plus card.
 * Both have:
 *   8/16 12 bit ADC with FIFO and channel gain table
 *   8 bits high speed digital out (for external MUX) (or 8 in or 8 out)
 *   8 bits high speed digital in with FIFO and interrupt on change (or 8 IO)
 *   2 12 bit DACs with FIFOs
 *   2 bits output
 *   2 bits input
 *   bus mastering DMA
 *   timers: ADC sample, pacer, burst, about, delay, DA1, DA2
 *   sample counter
 *   3 user timer/counters (8254)
 *   external interrupt
 *
 * The DM7520 has slightly fewer features (fewer gain steps).
 *
 * These boards can support external multiplexors and multi-board
 * synchronization, but this driver doesn't support that.
 *
 * Board docs: http://www.rtdusa.com/PC104/DM/analog%20IO/dm7520.htm
 * Data sheet: http://www.rtdusa.com/pdf/dm7520.pdf
 * Example source: http://www.rtdusa.com/examples/dm/dm7520.zip
 * Call them and ask for the register level manual.
 * PCI chip: http://www.plxtech.com/products/io/pci9080
 *
 * Notes:
 * This board is memory mapped. There is some IO stuff, but it isn't needed.
 *
 * I use a pretty loose naming style within the driver (rtd_blah).
 * All externally visible names should be rtd520_blah.
 * I use camelCase for structures (and inside them).
 * I may also use upper CamelCase for function names (old habit).
 *
 * This board is somewhat related to the RTD PCI4400 board.
 *
 * I borrowed heavily from the ni_mio_common, ni_atmio16d, mite, and
 * das1800, since they have the best documented code. Driver cb_pcidas64.c
 * uses the same DMA controller.
 *
 * As far as I can tell, the About interrupt doesn't work if Sample is
 * also enabled. It turns out that About really isn't needed, since
 * we always count down samples read.
 *
 * There was some timer/counter code, but it didn't follow the right API.
 */

/*
 * driver status:
 *
 * Analog-In supports instruction and command mode.
 *
 * With DMA, you can sample at 1.15Mhz with 70% idle on a 400Mhz K6-2
 * (single channel, 64K read buffer). I get random system lockups when
 * using DMA with ALI-15xx based systems. I haven't been able to test
 * any other chipsets. The lockups happen soon after the start of an
 * acquistion, not in the middle of a long run.
 *
 * Without DMA, you can do 620Khz sampling with 20% idle on a 400Mhz K6-2
 * (with a 256K read buffer).
 *
 * Digital-IO and Analog-Out only support instruction mode.
 */

#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/interrupt.h>

#include "../comedidev.h"

#include "comedi_fc.h"
#include "plx9080.h"

/*
 * Local Address Space 0 Offsets
 */
#define LAS0_USER_IO		0x0008	/* User I/O */
#define LAS0_ADC		0x0010	/* FIFO Status/Software A/D Start */
#define FS_DAC1_NOT_EMPTY	(1 << 0)	/* DAC1 FIFO not empty */
#define FS_DAC1_HEMPTY		(1 << 1)	/* DAC1 FIFO half empty */
#define FS_DAC1_NOT_FULL	(1 << 2)	/* DAC1 FIFO not full */
#define FS_DAC2_NOT_EMPTY	(1 << 4)	/* DAC2 FIFO not empty */
#define FS_DAC2_HEMPTY		(1 << 5)	/* DAC2 FIFO half empty */
#define FS_DAC2_NOT_FULL	(1 << 6)	/* DAC2 FIFO not full */
#define FS_ADC_NOT_EMPTY	(1 << 8)	/* ADC FIFO not empty */
#define FS_ADC_HEMPTY		(1 << 9)	/* ADC FIFO half empty */
#define FS_ADC_NOT_FULL		(1 << 10)	/* ADC FIFO not full */
#define FS_DIN_NOT_EMPTY	(1 << 12)	/* DIN FIFO not empty */
#define FS_DIN_HEMPTY		(1 << 13)	/* DIN FIFO half empty */
#define FS_DIN_NOT_FULL		(1 << 14)	/* DIN FIFO not full */
#define LAS0_DAC1		0x0014	/* Software D/A1 Update (w) */
#define LAS0_DAC2		0x0018	/* Software D/A2 Update (w) */
#define LAS0_DAC		0x0024	/* Software Simultaneous Update (w) */
#define LAS0_PACER		0x0028	/* Software Pacer Start/Stop */
#define LAS0_TIMER		0x002c	/* Timer Status/HDIN Software Trig. */
#define LAS0_IT			0x0030	/* Interrupt Status/Enable */
#define IRQM_ADC_FIFO_WRITE	(1 << 0)	/* ADC FIFO Write */
#define IRQM_CGT_RESET		(1 << 1)	/* Reset CGT */
#define IRQM_CGT_PAUSE		(1 << 3)	/* Pause CGT */
#define IRQM_ADC_ABOUT_CNT	(1 << 4)	/* About Counter out */
#define IRQM_ADC_DELAY_CNT	(1 << 5)	/* Delay Counter out */
#define IRQM_ADC_SAMPLE_CNT	(1 << 6)	/* ADC Sample Counter */
#define IRQM_DAC1_UCNT		(1 << 7)	/* DAC1 Update Counter */
#define IRQM_DAC2_UCNT		(1 << 8)	/* DAC2 Update Counter */
#define IRQM_UTC1		(1 << 9)	/* User TC1 out */
#define IRQM_UTC1_INV		(1 << 10)	/* User TC1 out, inverted */
#define IRQM_UTC2		(1 << 11)	/* User TC2 out */
#define IRQM_DIGITAL_IT		(1 << 12)	/* Digital Interrupt */
#define IRQM_EXTERNAL_IT	(1 << 13)	/* External Interrupt */
#define IRQM_ETRIG_RISING	(1 << 14)	/* Ext Trigger rising-edge */
#define IRQM_ETRIG_FALLING	(1 << 15)	/* Ext Trigger falling-edge */
#define LAS0_CLEAR		0x0034	/* Clear/Set Interrupt Clear Mask */
#define LAS0_OVERRUN		0x0038	/* Pending interrupts/Clear Overrun */
#define LAS0_PCLK		0x0040	/* Pacer Clock (24bit) */
#define LAS0_BCLK		0x0044	/* Burst Clock (10bit) */
#define LAS0_ADC_SCNT		0x0048	/* A/D Sample counter (10bit) */
#define LAS0_DAC1_UCNT		0x004c	/* D/A1 Update counter (10 bit) */
#define LAS0_DAC2_UCNT		0x0050	/* D/A2 Update counter (10 bit) */
#define LAS0_DCNT		0x0054	/* Delay counter (16 bit) */
#define LAS0_ACNT		0x0058	/* About counter (16 bit) */
#define LAS0_DAC_CLK		0x005c	/* DAC clock (16bit) */
#define LAS0_UTC0		0x0060	/* 8254 TC Counter 0 */
#define LAS0_UTC1		0x0064	/* 8254 TC Counter 1 */
#define LAS0_UTC2		0x0068	/* 8254 TC Counter 2 */
#define LAS0_UTC_CTRL		0x006c	/* 8254 TC Control */
#define LAS0_DIO0		0x0070	/* Digital I/O Port 0 */
#define LAS0_DIO1		0x0074	/* Digital I/O Port 1 */
#define LAS0_DIO0_CTRL		0x0078	/* Digital I/O Control */
#define LAS0_DIO_STATUS		0x007c	/* Digital I/O Status */
#define LAS0_BOARD_RESET	0x0100	/* Board reset */
#define LAS0_DMA0_SRC		0x0104	/* DMA 0 Sources select */
#define LAS0_DMA1_SRC		0x0108	/* DMA 1 Sources select */
#define LAS0_ADC_CONVERSION	0x010c	/* A/D Conversion Signal select */
#define LAS0_BURST_START	0x0110	/* Burst Clock Start Trigger select */
#define LAS0_PACER_START	0x0114	/* Pacer Clock Start Trigger select */
#define LAS0_PACER_STOP		0x0118	/* Pacer Clock Stop Trigger select */
#define LAS0_ACNT_STOP_ENABLE	0x011c	/* About Counter Stop Enable */
#define LAS0_PACER_REPEAT	0x0120	/* Pacer Start Trigger Mode select */
#define LAS0_DIN_START		0x0124	/* HiSpd DI Sampling Signal select */
#define LAS0_DIN_FIFO_CLEAR	0x0128	/* Digital Input FIFO Clear */
#define LAS0_ADC_FIFO_CLEAR	0x012c	/* A/D FIFO Clear */
#define LAS0_CGT_WRITE		0x0130	/* Channel Gain Table Write */
#define LAS0_CGL_WRITE		0x0134	/* Channel Gain Latch Write */
#define LAS0_CG_DATA		0x0138	/* Digital Table Write */
#define LAS0_CGT_ENABLE		0x013c	/* Channel Gain Table Enable */
#define LAS0_CG_ENABLE		0x0140	/* Digital Table Enable */
#define LAS0_CGT_PAUSE		0x0144	/* Table Pause Enable */
#define LAS0_CGT_RESET		0x0148	/* Reset Channel Gain Table */
#define LAS0_CGT_CLEAR		0x014c	/* Clear Channel Gain Table */
#define LAS0_DAC1_CTRL		0x0150	/* D/A1 output type/range */
#define LAS0_DAC1_SRC		0x0154	/* D/A1 update source */
#define LAS0_DAC1_CYCLE		0x0158	/* D/A1 cycle mode */
#define LAS0_DAC1_RESET		0x015c	/* D/A1 FIFO reset */
#define LAS0_DAC1_FIFO_CLEAR	0x0160	/* D/A1 FIFO clear */
#define LAS0_DAC2_CTRL		0x0164	/* D/A2 output type/range */
#define LAS0_DAC2_SRC		0x0168	/* D/A2 update source */
#define LAS0_DAC2_CYCLE		0x016c	/* D/A2 cycle mode */
#define LAS0_DAC2_RESET		0x0170	/* D/A2 FIFO reset */
#define LAS0_DAC2_FIFO_CLEAR	0x0174	/* D/A2 FIFO clear */
#define LAS0_ADC_SCNT_SRC	0x0178	/* A/D Sample Counter Source select */
#define LAS0_PACER_SELECT	0x0180	/* Pacer Clock select */
#define LAS0_SBUS0_SRC		0x0184	/* SyncBus 0 Source select */
#define LAS0_SBUS0_ENABLE	0x0188	/* SyncBus 0 enable */
#define LAS0_SBUS1_SRC		0x018c	/* SyncBus 1 Source select */
#define LAS0_SBUS1_ENABLE	0x0190	/* SyncBus 1 enable */
#define LAS0_SBUS2_SRC		0x0198	/* SyncBus 2 Source select */
#define LAS0_SBUS2_ENABLE	0x019c	/* SyncBus 2 enable */
#define LAS0_ETRG_POLARITY	0x01a4	/* Ext. Trigger polarity select */
#define LAS0_EINT_POLARITY	0x01a8	/* Ext. Interrupt polarity select */
#define LAS0_UTC0_CLOCK		0x01ac	/* UTC0 Clock select */
#define LAS0_UTC0_GATE		0x01b0	/* UTC0 Gate select */
#define LAS0_UTC1_CLOCK		0x01b4	/* UTC1 Clock select */
#define LAS0_UTC1_GATE		0x01b8	/* UTC1 Gate select */
#define LAS0_UTC2_CLOCK		0x01bc	/* UTC2 Clock select */
#define LAS0_UTC2_GATE		0x01c0	/* UTC2 Gate select */
#define LAS0_UOUT0_SELECT	0x01c4	/* User Output 0 source select */
#define LAS0_UOUT1_SELECT	0x01c8	/* User Output 1 source select */
#define LAS0_DMA0_RESET		0x01cc	/* DMA0 Request state machine reset */
#define LAS0_DMA1_RESET		0x01d0	/* DMA1 Request state machine reset */

/*
 * Local Address Space 1 Offsets
 */
#define LAS1_ADC_FIFO		0x0000	/* A/D FIFO (16bit) */
#define LAS1_HDIO_FIFO		0x0004	/* HiSpd DI FIFO (16bit) */
#define LAS1_DAC1_FIFO		0x0008	/* D/A1 FIFO (16bit) */
#define LAS1_DAC2_FIFO		0x000c	/* D/A2 FIFO (16bit) */

/*======================================================================
  Driver specific stuff (tunable)
======================================================================*/

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

#define RTD_CLOCK_RATE	8000000	/* 8Mhz onboard clock */
#define RTD_CLOCK_BASE	125	/* clock period in ns */

/* Note: these speed are slower than the spec, but fit the counter resolution*/
#define RTD_MAX_SPEED	1625	/* when sampling, in nanoseconds */
/* max speed if we don't have to wait for settling */
#define RTD_MAX_SPEED_1	875	/* if single channel, in nanoseconds */

#define RTD_MIN_SPEED	2097151875	/* (24bit counter) in nanoseconds */
/* min speed when only 1 channel (no burst counter) */
#define RTD_MIN_SPEED_1	5000000	/* 200Hz, in nanoseconds */

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

enum rtd_boardid {
	BOARD_DM7520,
	BOARD_PCI4520,
};

struct rtd_boardinfo {
	const char *name;
	int range_bip10;	/* start of +-10V range */
	int range_uni10;	/* start of +10V range */
	const struct comedi_lrange *ai_range;
};

static const struct rtd_boardinfo rtd520Boards[] = {
	[BOARD_DM7520] = {
		.name		= "DM7520",
		.range_bip10	= 6,
		.range_uni10	= 12,
		.ai_range	= &rtd_ai_7520_range,
	},
	[BOARD_PCI4520] = {
		.name		= "PCI4520",
		.range_bip10	= 8,
		.range_uni10	= 16,
		.ai_range	= &rtd_ai_4520_range,
	},
};

struct rtd_private {
	/* memory mapped board structures */
	void __iomem *las0;
	void __iomem *las1;
	void __iomem *lcfg;

	long ai_count;		/* total transfer size (samples) */
	int xfer_count;		/* # to transfer data. 0->1/2FIFO */
	int flags;		/* flag event modes */

	unsigned char chan_is_bipolar[RTD_MAX_CHANLIST / 8];	/* bit array */

	unsigned int ao_readback[2];

	unsigned fifosz;
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
static int rtd_ns_to_timer_base(unsigned int *nanosec,
				int round_mode, int base)
{
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
static unsigned short rtd_convert_chan_gain(struct comedi_device *dev,
					    unsigned int chanspec, int index)
{
	const struct rtd_boardinfo *board = comedi_board(dev);
	struct rtd_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(chanspec);
	unsigned int range = CR_RANGE(chanspec);
	unsigned int aref = CR_AREF(chanspec);
	unsigned short r = 0;

	r |= chan & 0xf;

	/* Note: we also setup the channel list bipolar flag array */
	if (range < board->range_bip10) {
		/* +-5 range */
		r |= 0x000;
		r |= (range & 0x7) << 4;
		CHAN_ARRAY_SET(devpriv->chan_is_bipolar, index);
	} else if (range < board->range_uni10) {
		/* +-10 range */
		r |= 0x100;
		r |= ((range - board->range_bip10) & 0x7) << 4;
		CHAN_ARRAY_SET(devpriv->chan_is_bipolar, index);
	} else {
		/* +10 range */
		r |= 0x200;
		r |= ((range - board->range_uni10) & 0x7) << 4;
		CHAN_ARRAY_CLEAR(devpriv->chan_is_bipolar, index);
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
	struct rtd_private *devpriv = dev->private;

	if (n_chan > 1) {	/* setup channel gain table */
		int ii;

		writel(0, devpriv->las0 + LAS0_CGT_CLEAR);
		writel(1, devpriv->las0 + LAS0_CGT_ENABLE);
		for (ii = 0; ii < n_chan; ii++) {
			writel(rtd_convert_chan_gain(dev, list[ii], ii),
				devpriv->las0 + LAS0_CGT_WRITE);
		}
	} else {		/* just use the channel gain latch */
		writel(0, devpriv->las0 + LAS0_CGT_ENABLE);
		writel(rtd_convert_chan_gain(dev, list[0], 0),
			devpriv->las0 + LAS0_CGL_WRITE);
	}
}

/* determine fifo size by doing adc conversions until the fifo half
empty status flag clears */
static int rtd520_probe_fifo_depth(struct comedi_device *dev)
{
	struct rtd_private *devpriv = dev->private;
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
		dev_info(dev->class_dev, "failed to probe fifo size.\n");
		return -EIO;
	}
	writel(0, devpriv->las0 + LAS0_ADC_FIFO_CLEAR);
	if (fifo_size != 0x400 && fifo_size != 0x2000) {
		dev_info(dev->class_dev,
			 "unexpected fifo size of %i, expected 1024 or 8192.\n",
			 fifo_size);
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
	struct rtd_private *devpriv = dev->private;
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
		if (ii >= RTD_ADC_TIMEOUT)
			return -ETIMEDOUT;

		/* read data */
		d = readw(devpriv->las1 + LAS1_ADC_FIFO);
		/*printk ("rtd520: Got 0x%x after %d usec\n", d, ii+1); */
		d = d >> 3;	/* low 3 bits are marker lines */
		if (CHAN_ARRAY_TEST(devpriv->chan_is_bipolar, 0))
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
	struct rtd_private *devpriv = dev->private;
	int ii;

	for (ii = 0; ii < count; ii++) {
		short sample;
		s16 d;

		if (0 == devpriv->ai_count) {	/* done */
			d = readw(devpriv->las1 + LAS1_ADC_FIFO);
			continue;
		}

		d = readw(devpriv->las1 + LAS1_ADC_FIFO);
		d = d >> 3;	/* low 3 bits are marker lines */
		if (CHAN_ARRAY_TEST(devpriv->chan_is_bipolar,
				    s->async->cur_chan)) {
			/* convert to comedi unsigned data */
			sample = d + 2048;
		} else
			sample = d;

		if (!comedi_buf_put(s->async, sample))
			return -1;

		if (devpriv->ai_count > 0)	/* < 0, means read forever */
			devpriv->ai_count--;
	}
	return 0;
}

/*
  unknown amout of data is waiting in fifo.
*/
static int ai_read_dregs(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct rtd_private *devpriv = dev->private;

	while (readl(devpriv->las0 + LAS0_ADC) & FS_ADC_NOT_EMPTY) {
		short sample;
		s16 d = readw(devpriv->las1 + LAS1_ADC_FIFO);

		if (0 == devpriv->ai_count) {	/* done */
			continue;	/* read rest */
		}

		d = d >> 3;	/* low 3 bits are marker lines */
		if (CHAN_ARRAY_TEST(devpriv->chan_is_bipolar,
				    s->async->cur_chan)) {
			/* convert to comedi unsigned data */
			sample = d + 2048;
		} else
			sample = d;

		if (!comedi_buf_put(s->async, sample))
			return -1;

		if (devpriv->ai_count > 0)	/* < 0, means read forever */
			devpriv->ai_count--;
	}
	return 0;
}

/*
  Handle all rtd520 interrupts.
  Runs atomically and is never re-entered.
  This is a "slow handler";  other interrupts may be active.
  The data conversion may someday happen in a "bottom half".
*/
static irqreturn_t rtd_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct comedi_subdevice *s = &dev->subdevices[0];
	struct rtd_private *devpriv = dev->private;
	u32 overrun;
	u16 status;
	u16 fifo_status;

	if (!dev->attached)
		return IRQ_NONE;

	fifo_status = readl(devpriv->las0 + LAS0_ADC);
	/* check for FIFO full, this automatically halts the ADC! */
	if (!(fifo_status & FS_ADC_NOT_FULL))	/* 0 -> full */
		goto xfer_abort;

	status = readw(devpriv->las0 + LAS0_IT);
	/* if interrupt was not caused by our board, or handled above */
	if (0 == status)
		return IRQ_HANDLED;

	if (status & IRQM_ADC_ABOUT_CNT) {	/* sample count -> read FIFO */
		/*
		 * since the priority interrupt controller may have queued
		 * a sample counter interrupt, even though we have already
		 * finished, we must handle the possibility that there is
		 * no data here
		 */
		if (!(fifo_status & FS_ADC_HEMPTY)) {
			/* FIFO half full */
			if (ai_read_n(dev, s, devpriv->fifosz / 2) < 0)
				goto xfer_abort;

			if (0 == devpriv->ai_count)
				goto xfer_done;

			comedi_event(dev, s);
		} else if (devpriv->xfer_count > 0) {
			if (fifo_status & FS_ADC_NOT_EMPTY) {
				/* FIFO not empty */
				if (ai_read_n(dev, s, devpriv->xfer_count) < 0)
					goto xfer_abort;

				if (0 == devpriv->ai_count)
					goto xfer_done;

				comedi_event(dev, s);
			}
		}
	}

	overrun = readl(devpriv->las0 + LAS0_OVERRUN) & 0xffff;
	if (overrun)
		goto xfer_abort;

	/* clear the interrupt */
	writew(status, devpriv->las0 + LAS0_CLEAR);
	readw(devpriv->las0 + LAS0_CLEAR);
	return IRQ_HANDLED;

xfer_abort:
	writel(0, devpriv->las0 + LAS0_ADC_FIFO_CLEAR);
	s->async->events |= COMEDI_CB_ERROR;
	devpriv->ai_count = 0;	/* stop and don't transfer any more */
	/* fall into xfer_done */

xfer_done:
	/* pacer stop source: SOFTWARE */
	writel(0, devpriv->las0 + LAS0_PACER_STOP);
	writel(0, devpriv->las0 + LAS0_PACER);	/* stop pacer */
	writel(0, devpriv->las0 + LAS0_ADC_CONVERSION);
	writew(0, devpriv->las0 + LAS0_IT);

	if (devpriv->ai_count > 0) {	/* there shouldn't be anything left */
		fifo_status = readl(devpriv->las0 + LAS0_ADC);
		ai_read_dregs(dev, s);	/* read anything left in FIFO */
	}

	s->async->events |= COMEDI_CB_EOA;	/* signal end to comedi */
	comedi_event(dev, s);

	/* clear the interrupt */
	status = readw(devpriv->las0 + LAS0_IT);
	writew(status, devpriv->las0 + LAS0_CLEAR);
	readw(devpriv->las0 + LAS0_CLEAR);

	fifo_status = readl(devpriv->las0 + LAS0_ADC);
	overrun = readl(devpriv->las0 + LAS0_OVERRUN) & 0xffff;

	return IRQ_HANDLED;
}

/*
  cmdtest tests a particular command to see if it is valid.
  Using the cmdtest ioctl, a user can create a valid cmd
  and then have it executed by the cmd ioctl (asynchronously).

  cmdtest returns 1,2,3,4 or 0, depending on which tests
  the command passes.
*/

static int rtd_ai_cmdtest(struct comedi_device *dev,
			  struct comedi_subdevice *s, struct comedi_cmd *cmd)
{
	int err = 0;
	int tmp;

	/* Step 1 : check if triggers are trivially valid */

	err |= cfc_check_trigger_src(&cmd->start_src, TRIG_NOW);
	err |= cfc_check_trigger_src(&cmd->scan_begin_src,
					TRIG_TIMER | TRIG_EXT);
	err |= cfc_check_trigger_src(&cmd->convert_src, TRIG_TIMER | TRIG_EXT);
	err |= cfc_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= cfc_check_trigger_src(&cmd->stop_src, TRIG_COUNT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= cfc_check_trigger_is_unique(cmd->scan_begin_src);
	err |= cfc_check_trigger_is_unique(cmd->convert_src);
	err |= cfc_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	err |= cfc_check_trigger_arg_is(&cmd->start_arg, 0);

	if (cmd->scan_begin_src == TRIG_TIMER) {
		/* Note: these are time periods, not actual rates */
		if (1 == cmd->chanlist_len) {	/* no scanning */
			if (cfc_check_trigger_arg_min(&cmd->scan_begin_arg,
						      RTD_MAX_SPEED_1)) {
				rtd_ns_to_timer(&cmd->scan_begin_arg,
						TRIG_ROUND_UP);
				err |= -EINVAL;
			}
			if (cfc_check_trigger_arg_max(&cmd->scan_begin_arg,
						      RTD_MIN_SPEED_1)) {
				rtd_ns_to_timer(&cmd->scan_begin_arg,
						TRIG_ROUND_DOWN);
				err |= -EINVAL;
			}
		} else {
			if (cfc_check_trigger_arg_min(&cmd->scan_begin_arg,
						      RTD_MAX_SPEED)) {
				rtd_ns_to_timer(&cmd->scan_begin_arg,
						TRIG_ROUND_UP);
				err |= -EINVAL;
			}
			if (cfc_check_trigger_arg_max(&cmd->scan_begin_arg,
						      RTD_MIN_SPEED)) {
				rtd_ns_to_timer(&cmd->scan_begin_arg,
						TRIG_ROUND_DOWN);
				err |= -EINVAL;
			}
		}
	} else {
		/* external trigger */
		/* should be level/edge, hi/lo specification here */
		/* should specify multiple external triggers */
		err |= cfc_check_trigger_arg_max(&cmd->scan_begin_arg, 9);
	}

	if (cmd->convert_src == TRIG_TIMER) {
		if (1 == cmd->chanlist_len) {	/* no scanning */
			if (cfc_check_trigger_arg_min(&cmd->convert_arg,
						      RTD_MAX_SPEED_1)) {
				rtd_ns_to_timer(&cmd->convert_arg,
						TRIG_ROUND_UP);
				err |= -EINVAL;
			}
			if (cfc_check_trigger_arg_max(&cmd->convert_arg,
						      RTD_MIN_SPEED_1)) {
				rtd_ns_to_timer(&cmd->convert_arg,
						TRIG_ROUND_DOWN);
				err |= -EINVAL;
			}
		} else {
			if (cfc_check_trigger_arg_min(&cmd->convert_arg,
						      RTD_MAX_SPEED)) {
				rtd_ns_to_timer(&cmd->convert_arg,
						TRIG_ROUND_UP);
				err |= -EINVAL;
			}
			if (cfc_check_trigger_arg_max(&cmd->convert_arg,
						      RTD_MIN_SPEED)) {
				rtd_ns_to_timer(&cmd->convert_arg,
						TRIG_ROUND_DOWN);
				err |= -EINVAL;
			}
		}
	} else {
		/* external trigger */
		/* see above */
		err |= cfc_check_trigger_arg_max(&cmd->convert_arg, 9);
	}

	if (cmd->stop_src == TRIG_COUNT) {
		/* TODO check for rounding error due to counter wrap */
	} else {
		/* TRIG_NONE */
		err |= cfc_check_trigger_arg_is(&cmd->stop_arg, 0);
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
	struct rtd_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	int timer;

	/* stop anything currently running */
	/* pacer stop source: SOFTWARE */
	writel(0, devpriv->las0 + LAS0_PACER_STOP);
	writel(0, devpriv->las0 + LAS0_PACER);	/* stop pacer */
	writel(0, devpriv->las0 + LAS0_ADC_CONVERSION);
	writew(0, devpriv->las0 + LAS0_IT);
	writel(0, devpriv->las0 + LAS0_ADC_FIFO_CLEAR);
	writel(0, devpriv->las0 + LAS0_OVERRUN);

	/* start configuration */
	/* load channel list and reset CGT */
	rtd_load_channelgain_list(dev, cmd->chanlist_len, cmd->chanlist);

	/* setup the common case and override if needed */
	if (cmd->chanlist_len > 1) {
		/* pacer start source: SOFTWARE */
		writel(0, devpriv->las0 + LAS0_PACER_START);
		/* burst trigger source: PACER */
		writel(1, devpriv->las0 + LAS0_BURST_START);
		/* ADC conversion trigger source: BURST */
		writel(2, devpriv->las0 + LAS0_ADC_CONVERSION);
	} else {		/* single channel */
		/* pacer start source: SOFTWARE */
		writel(0, devpriv->las0 + LAS0_PACER_START);
		/* ADC conversion trigger source: PACER */
		writel(1, devpriv->las0 + LAS0_ADC_CONVERSION);
	}
	writel((devpriv->fifosz / 2 - 1) & 0xffff, devpriv->las0 + LAS0_ACNT);

	if (TRIG_TIMER == cmd->scan_begin_src) {
		/* scan_begin_arg is in nanoseconds */
		/* find out how many samples to wait before transferring */
		if (cmd->flags & TRIG_WAKE_EOS) {
			/*
			 * this may generate un-sustainable interrupt rates
			 * the application is responsible for doing the
			 * right thing
			 */
			devpriv->xfer_count = cmd->chanlist_len;
			devpriv->flags |= SEND_EOS;
		} else {
			/* arrange to transfer data periodically */
			devpriv->xfer_count =
			    (TRANS_TARGET_PERIOD * cmd->chanlist_len) /
			    cmd->scan_begin_arg;
			if (devpriv->xfer_count < cmd->chanlist_len) {
				/* transfer after each scan (and avoid 0) */
				devpriv->xfer_count = cmd->chanlist_len;
			} else {	/* make a multiple of scan length */
				devpriv->xfer_count =
				    (devpriv->xfer_count +
				     cmd->chanlist_len - 1)
				    / cmd->chanlist_len;
				devpriv->xfer_count *= cmd->chanlist_len;
			}
			devpriv->flags |= SEND_EOS;
		}
		if (devpriv->xfer_count >= (devpriv->fifosz / 2)) {
			/* out of counter range, use 1/2 fifo instead */
			devpriv->xfer_count = 0;
			devpriv->flags &= ~SEND_EOS;
		} else {
			/* interrupt for each transfer */
			writel((devpriv->xfer_count - 1) & 0xffff,
				devpriv->las0 + LAS0_ACNT);
		}
	} else {		/* unknown timing, just use 1/2 FIFO */
		devpriv->xfer_count = 0;
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
		devpriv->ai_count = cmd->stop_arg * cmd->chanlist_len;
		if ((devpriv->xfer_count > 0)
		    && (devpriv->xfer_count > devpriv->ai_count)) {
			devpriv->xfer_count = devpriv->ai_count;
		}
		break;

	case TRIG_NONE:	/* stop when cancel is called */
		devpriv->ai_count = -1;	/* read forever */
		break;
	}

	/* Scan timing */
	switch (cmd->scan_begin_src) {
	case TRIG_TIMER:	/* periodic scanning */
		timer = rtd_ns_to_timer(&cmd->scan_begin_arg,
					TRIG_ROUND_NEAREST);
		/* set PACER clock */
		writel(timer & 0xffffff, devpriv->las0 + LAS0_PCLK);

		break;

	case TRIG_EXT:
		/* pacer start source: EXTERNAL */
		writel(1, devpriv->las0 + LAS0_PACER_START);
		break;
	}

	/* Sample timing within a scan */
	switch (cmd->convert_src) {
	case TRIG_TIMER:	/* periodic */
		if (cmd->chanlist_len > 1) {
			/* only needed for multi-channel */
			timer = rtd_ns_to_timer(&cmd->convert_arg,
						TRIG_ROUND_NEAREST);
			/* setup BURST clock */
			writel(timer & 0x3ff, devpriv->las0 + LAS0_BCLK);
		}

		break;

	case TRIG_EXT:		/* external */
		/* burst trigger source: EXTERNAL */
		writel(2, devpriv->las0 + LAS0_BURST_START);
		break;
	}
	/* end configuration */

	/* This doesn't seem to work.  There is no way to clear an interrupt
	   that the priority controller has queued! */
	writew(~0, devpriv->las0 + LAS0_CLEAR);
	readw(devpriv->las0 + LAS0_CLEAR);

	/* TODO: allow multiple interrupt sources */
	if (devpriv->xfer_count > 0) {	/* transfer every N samples */
		writew(IRQM_ADC_ABOUT_CNT, devpriv->las0 + LAS0_IT);
	} else {		/* 1/2 FIFO transfers */
		writew(IRQM_ADC_ABOUT_CNT, devpriv->las0 + LAS0_IT);
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
	struct rtd_private *devpriv = dev->private;
	u32 overrun;
	u16 status;

	/* pacer stop source: SOFTWARE */
	writel(0, devpriv->las0 + LAS0_PACER_STOP);
	writel(0, devpriv->las0 + LAS0_PACER);	/* stop pacer */
	writel(0, devpriv->las0 + LAS0_ADC_CONVERSION);
	writew(0, devpriv->las0 + LAS0_IT);
	devpriv->ai_count = 0;	/* stop and don't transfer any more */
	status = readw(devpriv->las0 + LAS0_IT);
	overrun = readl(devpriv->las0 + LAS0_OVERRUN) & 0xffff;
	return 0;
}

/*
  Output one (or more) analog values to a single port as fast as possible.
*/
static int rtd_ao_winsn(struct comedi_device *dev,
			struct comedi_subdevice *s, struct comedi_insn *insn,
			unsigned int *data)
{
	struct rtd_private *devpriv = dev->private;
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

		/* a typical programming sequence */
		writew(val, devpriv->las1 +
			((chan == 0) ? LAS1_DAC1_FIFO : LAS1_DAC2_FIFO));
		writew(0, devpriv->las0 +
			((chan == 0) ? LAS0_DAC1 : LAS0_DAC2));

		devpriv->ao_readback[chan] = data[i];

		for (ii = 0; ii < RTD_DAC_TIMEOUT; ++ii) {
			stat = readl(devpriv->las0 + LAS0_ADC);
			/* 1 -> not empty */
			if (stat & ((0 == chan) ? FS_DAC1_NOT_EMPTY :
				    FS_DAC2_NOT_EMPTY))
				break;
			WAIT_QUIETLY;
		}
		if (ii >= RTD_DAC_TIMEOUT)
			return -ETIMEDOUT;
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
	struct rtd_private *devpriv = dev->private;
	int i;
	int chan = CR_CHAN(insn->chanspec);

	for (i = 0; i < insn->n; i++)
		data[i] = devpriv->ao_readback[chan];


	return i;
}

static int rtd_dio_insn_bits(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     struct comedi_insn *insn,
			     unsigned int *data)
{
	struct rtd_private *devpriv = dev->private;
	unsigned int mask = data[0];
	unsigned int bits = data[1];

	if (mask) {
		s->state &= ~mask;
		s->state |= (bits & mask);

		writew(s->state & 0xff, devpriv->las0 + LAS0_DIO0);
	}

	data[1] = readw(devpriv->las0 + LAS0_DIO0) & 0xff;

	return insn->n;
}

static int rtd_dio_insn_config(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	struct rtd_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int mask = 1 << chan;

	switch (data[0]) {
	case INSN_CONFIG_DIO_OUTPUT:
		s->io_bits |= mask;
		break;
	case INSN_CONFIG_DIO_INPUT:
		s->io_bits &= ~mask;
		break;
	case INSN_CONFIG_DIO_QUERY:
		data[1] = (s->io_bits & mask) ? COMEDI_OUTPUT : COMEDI_INPUT;
		return insn->n;
		break;
	default:
		return -EINVAL;
	}

	/* TODO support digital match interrupts and strobes */

	/* set direction */
	writew(0x01, devpriv->las0 + LAS0_DIO_STATUS);
	writew(s->io_bits & 0xff, devpriv->las0 + LAS0_DIO0_CTRL);

	/* clear interrupts */
	writew(0x00, devpriv->las0 + LAS0_DIO_STATUS);

	/* port1 can only be all input or all output */

	/* there are also 2 user input lines and 2 user output lines */

	return insn->n;
}

static void rtd_reset(struct comedi_device *dev)
{
	struct rtd_private *devpriv = dev->private;

	writel(0, devpriv->las0 + LAS0_BOARD_RESET);
	udelay(100);		/* needed? */
	writel(0, devpriv->lcfg + PLX_INTRCS_REG);
	writew(0, devpriv->las0 + LAS0_IT);
	writew(~0, devpriv->las0 + LAS0_CLEAR);
	readw(devpriv->las0 + LAS0_CLEAR);
}

/*
 * initialize board, per RTD spec
 * also, initialize shadow registers
 */
static void rtd_init_board(struct comedi_device *dev)
{
	struct rtd_private *devpriv = dev->private;

	rtd_reset(dev);

	writel(0, devpriv->las0 + LAS0_OVERRUN);
	writel(0, devpriv->las0 + LAS0_CGT_CLEAR);
	writel(0, devpriv->las0 + LAS0_ADC_FIFO_CLEAR);
	writel(0, devpriv->las0 + LAS0_DAC1_RESET);
	writel(0, devpriv->las0 + LAS0_DAC2_RESET);
	/* clear digital IO fifo */
	writew(0, devpriv->las0 + LAS0_DIO_STATUS);
	writeb((0 << 6) | 0x30, devpriv->las0 + LAS0_UTC_CTRL);
	writeb((1 << 6) | 0x30, devpriv->las0 + LAS0_UTC_CTRL);
	writeb((2 << 6) | 0x30, devpriv->las0 + LAS0_UTC_CTRL);
	writeb((3 << 6) | 0x00, devpriv->las0 + LAS0_UTC_CTRL);
	/* TODO: set user out source ??? */
}

/* The RTD driver does this */
static void rtd_pci_latency_quirk(struct comedi_device *dev,
				  struct pci_dev *pcidev)
{
	unsigned char pci_latency;

	pci_read_config_byte(pcidev, PCI_LATENCY_TIMER, &pci_latency);
	if (pci_latency < 32) {
		dev_info(dev->class_dev,
			"PCI latency changed from %d to %d\n",
			pci_latency, 32);
		pci_write_config_byte(pcidev, PCI_LATENCY_TIMER, 32);
	}
}

static int rtd_auto_attach(struct comedi_device *dev,
			   unsigned long context)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct rtd_boardinfo *board = NULL;
	struct rtd_private *devpriv;
	struct comedi_subdevice *s;
	int ret;

	if (context < ARRAY_SIZE(rtd520Boards))
		board = &rtd520Boards[context];
	if (!board)
		return -ENODEV;
	dev->board_ptr = board;
	dev->board_name = board->name;

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;

	devpriv->las0 = pci_ioremap_bar(pcidev, 2);
	devpriv->las1 = pci_ioremap_bar(pcidev, 3);
	devpriv->lcfg = pci_ioremap_bar(pcidev, 0);
	if (!devpriv->las0 || !devpriv->las1 || !devpriv->lcfg)
		return -ENOMEM;

	rtd_pci_latency_quirk(dev, pcidev);

	if (pcidev->irq) {
		ret = request_irq(pcidev->irq, rtd_interrupt, IRQF_SHARED,
				  dev->board_name, dev);
		if (ret == 0)
			dev->irq = pcidev->irq;
	}

	ret = comedi_alloc_subdevices(dev, 4);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	/* analog input subdevice */
	s->type		= COMEDI_SUBD_AI;
	s->subdev_flags	= SDF_READABLE | SDF_GROUND | SDF_COMMON | SDF_DIFF;
	s->n_chan	= 16;
	s->maxdata	= 0x0fff;
	s->range_table	= board->ai_range;
	s->len_chanlist	= RTD_MAX_CHANLIST;
	s->insn_read	= rtd_ai_rinsn;
	if (dev->irq) {
		dev->read_subdev = s;
		s->subdev_flags	|= SDF_CMD_READ;
		s->do_cmd	= rtd_ai_cmd;
		s->do_cmdtest	= rtd_ai_cmdtest;
		s->cancel	= rtd_ai_cancel;
	}

	s = &dev->subdevices[1];
	/* analog output subdevice */
	s->type		= COMEDI_SUBD_AO;
	s->subdev_flags	= SDF_WRITABLE;
	s->n_chan	= 2;
	s->maxdata	= 0x0fff;
	s->range_table	= &rtd_ao_range;
	s->insn_write	= rtd_ao_winsn;
	s->insn_read	= rtd_ao_rinsn;

	s = &dev->subdevices[2];
	/* digital i/o subdevice */
	s->type		= COMEDI_SUBD_DIO;
	s->subdev_flags	= SDF_READABLE | SDF_WRITABLE;
	/* we only support port 0 right now.  Ignoring port 1 and user IO */
	s->n_chan	= 8;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= rtd_dio_insn_bits;
	s->insn_config	= rtd_dio_insn_config;

	/* timer/counter subdevices (not currently supported) */
	s = &dev->subdevices[3];
	s->type		= COMEDI_SUBD_COUNTER;
	s->subdev_flags	= SDF_READABLE | SDF_WRITABLE;
	s->n_chan	= 3;
	s->maxdata	= 0xffff;

	rtd_init_board(dev);

	ret = rtd520_probe_fifo_depth(dev);
	if (ret < 0)
		return ret;
	devpriv->fifosz = ret;

	if (dev->irq)
		writel(ICS_PIE | ICS_PLIE, devpriv->lcfg + PLX_INTRCS_REG);

	dev_info(dev->class_dev, "%s attached\n", dev->board_name);

	return 0;
}

static void rtd_detach(struct comedi_device *dev)
{
	struct rtd_private *devpriv = dev->private;

	if (devpriv) {
		/* Shut down any board ops by resetting it */
		if (devpriv->las0 && devpriv->lcfg)
			rtd_reset(dev);
		if (dev->irq) {
			writel(readl(devpriv->lcfg + PLX_INTRCS_REG) &
				~(ICS_PLIE | ICS_DMA0_E | ICS_DMA1_E),
				devpriv->lcfg + PLX_INTRCS_REG);
			free_irq(dev->irq, dev);
		}
		if (devpriv->las0)
			iounmap(devpriv->las0);
		if (devpriv->las1)
			iounmap(devpriv->las1);
		if (devpriv->lcfg)
			iounmap(devpriv->lcfg);
	}
	comedi_pci_disable(dev);
}

static struct comedi_driver rtd520_driver = {
	.driver_name	= "rtd520",
	.module		= THIS_MODULE,
	.auto_attach	= rtd_auto_attach,
	.detach		= rtd_detach,
};

static int rtd520_pci_probe(struct pci_dev *dev,
			    const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &rtd520_driver, id->driver_data);
}

static DEFINE_PCI_DEVICE_TABLE(rtd520_pci_table) = {
	{ PCI_VDEVICE(RTD, 0x7520), BOARD_DM7520 },
	{ PCI_VDEVICE(RTD, 0x4520), BOARD_PCI4520 },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, rtd520_pci_table);

static struct pci_driver rtd520_pci_driver = {
	.name		= "rtd520",
	.id_table	= rtd520_pci_table,
	.probe		= rtd520_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(rtd520_driver, rtd520_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
