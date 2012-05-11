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
#include "comedi_pci.h"

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
  The board has 3 input modes and the gains of 1,2,4,...32 (, 64, 128)
*/
static const struct comedi_lrange rtd_ai_7520_range = { 18, {
							     /* +-5V input range gain steps */
							     BIP_RANGE(5.0),
							     BIP_RANGE(5.0 / 2),
							     BIP_RANGE(5.0 / 4),
							     BIP_RANGE(5.0 / 8),
							     BIP_RANGE(5.0 /
								       16),
							     BIP_RANGE(5.0 /
								       32),
							     /* +-10V input range gain steps */
							     BIP_RANGE(10.0),
							     BIP_RANGE(10.0 /
								       2),
							     BIP_RANGE(10.0 /
								       4),
							     BIP_RANGE(10.0 /
								       8),
							     BIP_RANGE(10.0 /
								       16),
							     BIP_RANGE(10.0 /
								       32),
							     /* +10V input range gain steps */
							     UNI_RANGE(10.0),
							     UNI_RANGE(10.0 /
								       2),
							     UNI_RANGE(10.0 /
								       4),
							     UNI_RANGE(10.0 /
								       8),
							     UNI_RANGE(10.0 /
								       16),
							     UNI_RANGE(10.0 /
								       32),

							     }
};

/* PCI4520 has two more gains (6 more entries) */
static const struct comedi_lrange rtd_ai_4520_range = { 24, {
							     /* +-5V input range gain steps */
							     BIP_RANGE(5.0),
							     BIP_RANGE(5.0 / 2),
							     BIP_RANGE(5.0 / 4),
							     BIP_RANGE(5.0 / 8),
							     BIP_RANGE(5.0 /
								       16),
							     BIP_RANGE(5.0 /
								       32),
							     BIP_RANGE(5.0 /
								       64),
							     BIP_RANGE(5.0 /
								       128),
							     /* +-10V input range gain steps */
							     BIP_RANGE(10.0),
							     BIP_RANGE(10.0 /
								       2),
							     BIP_RANGE(10.0 /
								       4),
							     BIP_RANGE(10.0 /
								       8),
							     BIP_RANGE(10.0 /
								       16),
							     BIP_RANGE(10.0 /
								       32),
							     BIP_RANGE(10.0 /
								       64),
							     BIP_RANGE(10.0 /
								       128),
							     /* +10V input range gain steps */
							     UNI_RANGE(10.0),
							     UNI_RANGE(10.0 /
								       2),
							     UNI_RANGE(10.0 /
								       4),
							     UNI_RANGE(10.0 /
								       8),
							     UNI_RANGE(10.0 /
								       16),
							     UNI_RANGE(10.0 /
								       32),
							     UNI_RANGE(10.0 /
								       64),
							     UNI_RANGE(10.0 /
								       128),
							     }
};

/* Table order matches range values */
static const struct comedi_lrange rtd_ao_range = { 4, {
						       RANGE(0, 5),
						       RANGE(0, 10),
						       RANGE(-5, 5),
						       RANGE(-10, 10),
						       }
};

/*
  Board descriptions
 */
struct rtdBoard {
	const char *name;	/* must be first */
	int device_id;
	int aiChans;
	int aiBits;
	int aiMaxGain;
	int range10Start;	/* start of +-10V range */
	int rangeUniStart;	/* start of +10V range */
};

static const struct rtdBoard rtd520Boards[] = {
	{
	 .name = "DM7520",
	 .device_id = 0x7520,
	 .aiChans = 16,
	 .aiBits = 12,
	 .aiMaxGain = 32,
	 .range10Start = 6,
	 .rangeUniStart = 12,
	 },
	{
	 .name = "PCI4520",
	 .device_id = 0x4520,
	 .aiChans = 16,
	 .aiBits = 12,
	 .aiMaxGain = 128,
	 .range10Start = 8,
	 .rangeUniStart = 16,
	 },
};

/*
 * Useful for shorthand access to the particular board structure
 */
#define thisboard ((const struct rtdBoard *)dev->board_ptr)

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

	/* PCI device info */
	struct pci_dev *pci_dev;
	int got_regions;	/* non-zero if PCI regions owned */

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
 * most drivers define the following macro to make it easy to
 * access the private structure.
 */
#define devpriv ((struct rtdPrivate *)dev->private)

/* Macros to access registers */

/* Reset board */
#define RtdResetBoard(dev) \
	writel(0, devpriv->las0+LAS0_BOARD_RESET)

/* Reset channel gain table read pointer */
#define RtdResetCGT(dev) \
	writel(0, devpriv->las0+LAS0_CGT_RESET)

/* Reset channel gain table read and write pointers */
#define RtdClearCGT(dev) \
	writel(0, devpriv->las0+LAS0_CGT_CLEAR)

/* Reset channel gain table read and write pointers */
#define RtdEnableCGT(dev, v) \
	writel((v > 0) ? 1 : 0, devpriv->las0+LAS0_CGT_ENABLE)

/* Write channel gain table entry */
#define RtdWriteCGTable(dev, v) \
	writel(v, devpriv->las0+LAS0_CGT_WRITE)

/* Write Channel Gain Latch */
#define RtdWriteCGLatch(dev, v) \
	writel(v, devpriv->las0+LAS0_CGL_WRITE)

/* Reset ADC FIFO */
#define RtdAdcClearFifo(dev) \
	writel(0, devpriv->las0+LAS0_ADC_FIFO_CLEAR)

/* Set ADC start conversion source select (write only) */
#define RtdAdcConversionSource(dev, v) \
	writel(v, devpriv->las0+LAS0_ADC_CONVERSION)

/* Set burst start source select (write only) */
#define RtdBurstStartSource(dev, v) \
	writel(v, devpriv->las0+LAS0_BURST_START)

/* Set Pacer start source select (write only) */
#define RtdPacerStartSource(dev, v) \
	writel(v, devpriv->las0+LAS0_PACER_START)

/* Set Pacer stop source select (write only) */
#define RtdPacerStopSource(dev, v) \
	writel(v, devpriv->las0+LAS0_PACER_STOP)

/* Set Pacer clock source select (write only) 0=external 1=internal */
#define RtdPacerClockSource(dev, v) \
	writel((v > 0) ? 1 : 0, devpriv->las0+LAS0_PACER_SELECT)

/* Set sample counter source select (write only) */
#define RtdAdcSampleCounterSource(dev, v) \
	writel(v, devpriv->las0+LAS0_ADC_SCNT_SRC)

/* Set Pacer trigger mode select (write only) 0=single cycle, 1=repeat */
#define RtdPacerTriggerMode(dev, v) \
	writel((v > 0) ? 1 : 0, devpriv->las0+LAS0_PACER_REPEAT)

/* Set About counter stop enable (write only) */
#define RtdAboutStopEnable(dev, v) \
	writel((v > 0) ? 1 : 0, devpriv->las0+LAS0_ACNT_STOP_ENABLE)

/* Set external trigger polarity (write only) 0=positive edge, 1=negative */
#define RtdTriggerPolarity(dev, v) \
	writel((v > 0) ? 1 : 0, devpriv->las0+LAS0_ETRG_POLARITY)

/* Start single ADC conversion */
#define RtdAdcStart(dev) \
	writew(0, devpriv->las0+LAS0_ADC)

/* Read one ADC data value (12bit (with sign extend) as 16bit) */
/* Note: matches what DMA would get.  Actual value >> 3 */
#define RtdAdcFifoGet(dev) \
	readw(devpriv->las1+LAS1_ADC_FIFO)

/* Read two ADC data values (DOESN'T WORK) */
#define RtdAdcFifoGet2(dev) \
	readl(devpriv->las1+LAS1_ADC_FIFO)

/* FIFO status */
#define RtdFifoStatus(dev) \
	readl(devpriv->las0+LAS0_ADC)

/* pacer start/stop read=start, write=stop*/
#define RtdPacerStart(dev) \
	readl(devpriv->las0+LAS0_PACER)
#define RtdPacerStop(dev) \
	writel(0, devpriv->las0+LAS0_PACER)

/* Interrupt status */
#define RtdInterruptStatus(dev) \
	readw(devpriv->las0+LAS0_IT)

/* Interrupt mask */
#define RtdInterruptMask(dev, v) \
	writew((devpriv->intMask = (v)), devpriv->las0+LAS0_IT)

/* Interrupt status clear (only bits set in mask) */
#define RtdInterruptClear(dev) \
	readw(devpriv->las0+LAS0_CLEAR)

/* Interrupt clear mask */
#define RtdInterruptClearMask(dev, v) \
	writew((devpriv->intClearMask = (v)), devpriv->las0+LAS0_CLEAR)

/* Interrupt overrun status */
#define RtdInterruptOverrunStatus(dev) \
	readl(devpriv->las0+LAS0_OVERRUN)

/* Interrupt overrun clear */
#define RtdInterruptOverrunClear(dev) \
	writel(0, devpriv->las0+LAS0_OVERRUN)

/* Pacer counter, 24bit */
#define RtdPacerCount(dev) \
	readl(devpriv->las0+LAS0_PCLK)
#define RtdPacerCounter(dev, v) \
	writel((v) & 0xffffff, devpriv->las0+LAS0_PCLK)

/* Burst counter, 10bit */
#define RtdBurstCount(dev) \
	readl(devpriv->las0+LAS0_BCLK)
#define RtdBurstCounter(dev, v) \
	writel((v) & 0x3ff, devpriv->las0+LAS0_BCLK)

/* Delay counter, 16bit */
#define RtdDelayCount(dev) \
	readl(devpriv->las0+LAS0_DCLK)
#define RtdDelayCounter(dev, v) \
	writel((v) & 0xffff, devpriv->las0+LAS0_DCLK)

/* About counter, 16bit */
#define RtdAboutCount(dev) \
	readl(devpriv->las0+LAS0_ACNT)
#define RtdAboutCounter(dev, v) \
	writel((v) & 0xffff, devpriv->las0+LAS0_ACNT)

/* ADC sample counter, 10bit */
#define RtdAdcSampleCount(dev) \
	readl(devpriv->las0+LAS0_ADC_SCNT)
#define RtdAdcSampleCounter(dev, v) \
	writel((v) & 0x3ff, devpriv->las0+LAS0_ADC_SCNT)

/* User Timer/Counter (8254) */
#define RtdUtcCounterGet(dev, n) \
	readb(devpriv->las0 \
		+ ((n <= 0) ? LAS0_UTC0 : ((1 == n) ? LAS0_UTC1 : LAS0_UTC2)))

#define RtdUtcCounterPut(dev, n, v) \
	writeb((v) & 0xff, devpriv->las0 \
		+ ((n <= 0) ? LAS0_UTC0 : ((1 == n) ? LAS0_UTC1 : LAS0_UTC2)))

/* Set UTC (8254) control byte  */
#define RtdUtcCtrlPut(dev, n, v) \
	writeb(devpriv->utcCtrl[(n) & 3] = (((n) & 3) << 6) | ((v) & 0x3f), \
		devpriv->las0 + LAS0_UTC_CTRL)

/* Set UTCn clock source (write only) */
#define RtdUtcClockSource(dev, n, v) \
	writew(v, devpriv->las0 \
		+ ((n <= 0) ? LAS0_UTC0_CLOCK : \
			((1 == n) ? LAS0_UTC1_CLOCK : LAS0_UTC2_CLOCK)))

/* Set UTCn gate source (write only) */
#define RtdUtcGateSource(dev, n, v) \
	writew(v, devpriv->las0 \
		+ ((n <= 0) ? LAS0_UTC0_GATE : \
			((1 == n) ? LAS0_UTC1_GATE : LAS0_UTC2_GATE)))

/* User output N source select (write only) */
#define RtdUsrOutSource(dev, n, v) \
	writel(v, devpriv->las0+((n <= 0) ? LAS0_UOUT0_SELECT : \
				LAS0_UOUT1_SELECT))

/* Digital IO */
#define RtdDio0Read(dev) \
	(readw(devpriv->las0+LAS0_DIO0) & 0xff)
#define RtdDio0Write(dev, v) \
	writew((v) & 0xff, devpriv->las0+LAS0_DIO0)

#define RtdDio1Read(dev) \
	(readw(devpriv->las0+LAS0_DIO1) & 0xff)
#define RtdDio1Write(dev, v) \
	writew((v) & 0xff, devpriv->las0+LAS0_DIO1)

#define RtdDioStatusRead(dev) \
	(readw(devpriv->las0+LAS0_DIO_STATUS) & 0xff)
#define RtdDioStatusWrite(dev, v) \
	writew((devpriv->dioStatus = (v)), devpriv->las0+LAS0_DIO_STATUS)

#define RtdDio0CtrlRead(dev) \
	(readw(devpriv->las0+LAS0_DIO0_CTRL) & 0xff)
#define RtdDio0CtrlWrite(dev, v) \
	writew((v) & 0xff, devpriv->las0+LAS0_DIO0_CTRL)

/* Digital to Analog converter */
/* Write one data value (sign + 12bit + marker bits) */
/* Note: matches what DMA would put.  Actual value << 3 */
#define RtdDacFifoPut(dev, n, v) \
	writew((v), devpriv->las1 + (((n) == 0) ? LAS1_DAC1_FIFO : \
				LAS1_DAC2_FIFO))

/* Start single DAC conversion */
#define RtdDacUpdate(dev, n) \
	writew(0, devpriv->las0 + (((n) == 0) ? LAS0_DAC1 : LAS0_DAC2))

/* Start single DAC conversion on both DACs */
#define RtdDacBothUpdate(dev) \
	writew(0, devpriv->las0+LAS0_DAC)

/* Set DAC output type and range */
#define RtdDacRange(dev, n, v) \
	writew((v) & 7, devpriv->las0 \
		+(((n) == 0) ? LAS0_DAC1_CTRL : LAS0_DAC2_CTRL))

/* Reset DAC FIFO */
#define RtdDacClearFifo(dev, n) \
	writel(0, devpriv->las0+(((n) == 0) ? LAS0_DAC1_RESET : \
				LAS0_DAC2_RESET))

/* Set source for DMA 0 (write only, shadow?) */
#define RtdDma0Source(dev, n) \
	writel((n) & 0xf, devpriv->las0+LAS0_DMA0_SRC)

/* Set source for DMA 1 (write only, shadow?) */
#define RtdDma1Source(dev, n) \
	writel((n) & 0xf, devpriv->las0+LAS0_DMA1_SRC)

/* Reset board state for DMA 0 */
#define RtdDma0Reset(dev) \
	writel(0, devpriv->las0+LAS0_DMA0_RESET)

/* Reset board state for DMA 1 */
#define RtdDma1Reset(dev) \
	writel(0, devpriv->las0+LAS0_DMA1_SRC)

/* PLX9080 interrupt mask and status */
#define RtdPlxInterruptRead(dev) \
	readl(devpriv->lcfg+LCFG_ITCSR)
#define RtdPlxInterruptWrite(dev, v) \
	writel(v, devpriv->lcfg+LCFG_ITCSR)

/* Set  mode for DMA 0 */
#define RtdDma0Mode(dev, m) \
	writel((m), devpriv->lcfg+LCFG_DMAMODE0)

/* Set PCI address for DMA 0 */
#define RtdDma0PciAddr(dev, a) \
	writel((a), devpriv->lcfg+LCFG_DMAPADR0)

/* Set local address for DMA 0 */
#define RtdDma0LocalAddr(dev, a) \
	writel((a), devpriv->lcfg+LCFG_DMALADR0)

/* Set byte count for DMA 0 */
#define RtdDma0Count(dev, c) \
	writel((c), devpriv->lcfg+LCFG_DMASIZ0)

/* Set next descriptor for DMA 0 */
#define RtdDma0Next(dev, a) \
	writel((a), devpriv->lcfg+LCFG_DMADPR0)

/* Set  mode for DMA 1 */
#define RtdDma1Mode(dev, m) \
	writel((m), devpriv->lcfg+LCFG_DMAMODE1)

/* Set PCI address for DMA 1 */
#define RtdDma1PciAddr(dev, a) \
	writel((a), devpriv->lcfg+LCFG_DMAADR1)

/* Set local address for DMA 1 */
#define RtdDma1LocalAddr(dev, a) \
	writel((a), devpriv->lcfg+LCFG_DMALADR1)

/* Set byte count for DMA 1 */
#define RtdDma1Count(dev, c) \
	writel((c), devpriv->lcfg+LCFG_DMASIZ1)

/* Set next descriptor for DMA 1 */
#define RtdDma1Next(dev, a) \
	writel((a), devpriv->lcfg+LCFG_DMADPR1)

/* Set control for DMA 0 (write only, shadow?) */
#define RtdDma0Control(dev, n) \
	writeb(devpriv->dma0Control = (n), devpriv->lcfg+LCFG_DMACSR0)

/* Get status for DMA 0 */
#define RtdDma0Status(dev) \
	readb(devpriv->lcfg+LCFG_DMACSR0)

/* Set control for DMA 1 (write only, shadow?) */
#define RtdDma1Control(dev, n) \
	writeb(devpriv->dma1Control = (n), devpriv->lcfg+LCFG_DMACSR1)

/* Get status for DMA 1 */
#define RtdDma1Status(dev) \
	readb(devpriv->lcfg+LCFG_DMACSR1)

static int rtd_ai_rinsn(struct comedi_device *dev, struct comedi_subdevice *s,
			struct comedi_insn *insn, unsigned int *data);
static int rtd_ao_winsn(struct comedi_device *dev, struct comedi_subdevice *s,
			struct comedi_insn *insn, unsigned int *data);
static int rtd_ao_rinsn(struct comedi_device *dev, struct comedi_subdevice *s,
			struct comedi_insn *insn, unsigned int *data);
static int rtd_dio_insn_bits(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     struct comedi_insn *insn, unsigned int *data);
static int rtd_dio_insn_config(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data);
static int rtd_ai_cmdtest(struct comedi_device *dev, struct comedi_subdevice *s,
			  struct comedi_cmd *cmd);
static int rtd_ai_cmd(struct comedi_device *dev, struct comedi_subdevice *s);
static int rtd_ai_cancel(struct comedi_device *dev, struct comedi_subdevice *s);
/*
 * static int rtd_ai_poll(struct comedi_device *dev,
 *			  struct comedi_subdevice *s);
 */
static int rtd_ns_to_timer(unsigned int *ns, int roundMode);
static irqreturn_t rtd_interrupt(int irq, void *d);
static int rtd520_probe_fifo_depth(struct comedi_device *dev);

/*
 * Attach is called by the Comedi core to configure the driver
 * for a particular board.  If you specified a board_name array
 * in the driver structure, dev->board_ptr contains that
 * address.
 */
static int rtd_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{				/* board name and options flags */
	struct comedi_subdevice *s;
	struct pci_dev *pcidev;
	int ret;
	resource_size_t physLas0;	/* configuration */
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

	/*
	 * Probe the device to determine what device in the series it is.
	 */
	for (pcidev = pci_get_device(PCI_VENDOR_ID_RTD, PCI_ANY_ID, NULL);
	     pcidev != NULL;
	     pcidev = pci_get_device(PCI_VENDOR_ID_RTD, PCI_ANY_ID, pcidev)) {
		int i;

		if (it->options[0] || it->options[1]) {
			if (pcidev->bus->number != it->options[0]
			    || PCI_SLOT(pcidev->devfn) != it->options[1]) {
				continue;
			}
		}
		for (i = 0; i < ARRAY_SIZE(rtd520Boards); ++i) {
			if (pcidev->device == rtd520Boards[i].device_id) {
				dev->board_ptr = &rtd520Boards[i];
				break;
			}
		}
		if (dev->board_ptr)
			break;	/* found one */
	}
	if (!pcidev) {
		if (it->options[0] && it->options[1]) {
			printk(KERN_INFO "No RTD card at bus=%d slot=%d.\n",
			       it->options[0], it->options[1]);
		} else {
			printk(KERN_INFO "No RTD card found.\n");
		}
		return -EIO;
	}
	devpriv->pci_dev = pcidev;
	dev->board_name = thisboard->name;

	ret = comedi_pci_enable(pcidev, DRV_NAME);
	if (ret < 0) {
		printk(KERN_INFO "Failed to enable PCI device and request regions.\n");
		return ret;
	}
	devpriv->got_regions = 1;

	/*
	 * Initialize base addresses
	 */
	/* Get the physical address from PCI config */
	physLas0 = pci_resource_start(devpriv->pci_dev, LAS0_PCIINDEX);
	physLas1 = pci_resource_start(devpriv->pci_dev, LAS1_PCIINDEX);
	physLcfg = pci_resource_start(devpriv->pci_dev, LCFG_PCIINDEX);
	/* Now have the kernel map this into memory */
	/* ASSUME page aligned */
	devpriv->las0 = ioremap_nocache(physLas0, LAS0_PCISIZE);
	devpriv->las1 = ioremap_nocache(physLas1, LAS1_PCISIZE);
	devpriv->lcfg = ioremap_nocache(physLcfg, LCFG_PCISIZE);

	if (!devpriv->las0 || !devpriv->las1 || !devpriv->lcfg)
		return -ENOMEM;


	DPRINTK("%s: LAS0=%llx, LAS1=%llx, CFG=%llx.\n", dev->board_name,
		(unsigned long long)physLas0, (unsigned long long)physLas1,
		(unsigned long long)physLcfg);
	{			/* The RTD driver does this */
		unsigned char pci_latency;
		u16 revision;
		/*uint32_t epld_version; */

		pci_read_config_word(devpriv->pci_dev, PCI_REVISION_ID,
				     &revision);
		DPRINTK("%s: PCI revision %d.\n", dev->board_name, revision);

		pci_read_config_byte(devpriv->pci_dev,
				     PCI_LATENCY_TIMER, &pci_latency);
		if (pci_latency < 32) {
			printk(KERN_INFO "%s: PCI latency changed from %d to %d\n",
			       dev->board_name, pci_latency, 32);
			pci_write_config_byte(devpriv->pci_dev,
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

	/*
	 * Allocate the subdevice structures.  alloc_subdevice() is a
	 * convenient macro defined in comedidev.h.
	 */
	if (alloc_subdevices(dev, 4) < 0)
		return -ENOMEM;


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
	RtdResetBoard(dev);
	udelay(100);		/* needed? */
	RtdPlxInterruptWrite(dev, 0);
	RtdInterruptMask(dev, 0);	/* and sets shadow */
	RtdInterruptClearMask(dev, ~0);	/* and sets shadow */
	RtdInterruptClear(dev);	/* clears bits set by mask */
	RtdInterruptOverrunClear(dev);
	RtdClearCGT(dev);
	RtdAdcClearFifo(dev);
	RtdDacClearFifo(dev, 0);
	RtdDacClearFifo(dev, 1);
	/* clear digital IO fifo */
	RtdDioStatusWrite(dev, 0);	/* safe state, set shadow */
	RtdUtcCtrlPut(dev, 0, 0x30);	/* safe state, set shadow */
	RtdUtcCtrlPut(dev, 1, 0x30);	/* safe state, set shadow */
	RtdUtcCtrlPut(dev, 2, 0x30);	/* safe state, set shadow */
	RtdUtcCtrlPut(dev, 3, 0);	/* safe state, set shadow */
	/* TODO: set user out source ??? */

	/* check if our interrupt is available and get it */
	ret = request_irq(devpriv->pci_dev->irq, rtd_interrupt,
			  IRQF_SHARED, DRV_NAME, dev);

	if (ret < 0) {
		printk("Could not get interrupt! (%u)\n",
		       devpriv->pci_dev->irq);
		return ret;
	}
	dev->irq = devpriv->pci_dev->irq;
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
			    pci_alloc_consistent(devpriv->pci_dev,
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
		    pci_alloc_consistent(devpriv->pci_dev,
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

		RtdDma0Mode(dev, DMA_MODE_BITS);
		/* set DMA trigger source */
		RtdDma0Source(dev, DMAS_ADFIFO_HALF_FULL);
	} else {
		printk(KERN_INFO "( no IRQ->no DMA )");
	}
#endif /* USE_DMA */

	if (dev->irq) {		/* enable plx9080 interrupts */
		RtdPlxInterruptWrite(dev, ICS_PIE | ICS_PLIE);
	}

	printk("\ncomedi%d: rtd520 driver attached.\n", dev->minor);

	return 1;

#if 0
	/* hit an error, clean up memory and return ret */
/* rtd_attach_die_error: */
#ifdef USE_DMA
	for (index = 0; index < DMA_CHAIN_COUNT; index++) {
		if (NULL != devpriv->dma0Buff[index]) {	/* free buffer memory */
			pci_free_consistent(devpriv->pci_dev,
					    sizeof(u16) * devpriv->fifoLen / 2,
					    devpriv->dma0Buff[index],
					    devpriv->dma0BuffPhysAddr[index]);
			devpriv->dma0Buff[index] = NULL;
		}
	}
	if (NULL != devpriv->dma0Chain) {
		pci_free_consistent(devpriv->pci_dev,
				    sizeof(struct plx_dma_desc)
				    * DMA_CHAIN_COUNT,
				    devpriv->dma0Chain,
				    devpriv->dma0ChainPhysAddr);
		devpriv->dma0Chain = NULL;
	}
#endif /* USE_DMA */
	/* subdevices and priv are freed by the core */
	if (dev->irq) {
		/* disable interrupt controller */
		RtdPlxInterruptWrite(dev, RtdPlxInterruptRead(dev)
				     & ~(ICS_PLIE | ICS_DMA0_E | ICS_DMA1_E));
		free_irq(dev->irq, dev);
	}

	/* release all regions that were allocated */
	if (devpriv->las0)
		iounmap(devpriv->las0);

	if (devpriv->las1)
		iounmap(devpriv->las1);

	if (devpriv->lcfg)
		iounmap(devpriv->lcfg);

	if (devpriv->pci_dev)
		pci_dev_put(devpriv->pci_dev);

	return ret;
#endif
}

/*
 * _detach is called to deconfigure a device.  It should deallocate
 * resources.
 * This function is also called when _attach() fails, so it should be
 * careful not to release resources that were not necessarily
 * allocated by _attach().  dev->private and dev->subdevices are
 * deallocated automatically by the core.
 */
static int rtd_detach(struct comedi_device *dev)
{
#ifdef USE_DMA
	int index;
#endif

	DPRINTK("comedi%d: rtd520: removing (%ld ints)\n",
		dev->minor, (devpriv ? devpriv->intCount : 0L));
	if (devpriv && devpriv->lcfg) {
		DPRINTK
		    ("(int status 0x%x, overrun status 0x%x, fifo status 0x%x)...\n",
		     0xffff & RtdInterruptStatus(dev),
		     0xffff & RtdInterruptOverrunStatus(dev),
		     (0xffff & RtdFifoStatus(dev)) ^ 0x6666);
	}

	if (devpriv) {
		/* Shut down any board ops by resetting it */
#ifdef USE_DMA
		if (devpriv->lcfg) {
			RtdDma0Control(dev, 0);	/* disable DMA */
			RtdDma1Control(dev, 0);	/* disable DMA */
			RtdPlxInterruptWrite(dev, ICS_PIE | ICS_PLIE);
		}
#endif /* USE_DMA */
		if (devpriv->las0) {
			RtdResetBoard(dev);
			RtdInterruptMask(dev, 0);
			RtdInterruptClearMask(dev, ~0);
			RtdInterruptClear(dev);	/* clears bits set by mask */
		}
#ifdef USE_DMA
		/* release DMA */
		for (index = 0; index < DMA_CHAIN_COUNT; index++) {
			if (NULL != devpriv->dma0Buff[index]) {
				pci_free_consistent(devpriv->pci_dev,
						    sizeof(u16) *
						    devpriv->fifoLen / 2,
						    devpriv->dma0Buff[index],
						    devpriv->
						    dma0BuffPhysAddr[index]);
				devpriv->dma0Buff[index] = NULL;
			}
		}
		if (NULL != devpriv->dma0Chain) {
			pci_free_consistent(devpriv->pci_dev,
					    sizeof(struct plx_dma_desc) *
					    DMA_CHAIN_COUNT, devpriv->dma0Chain,
					    devpriv->dma0ChainPhysAddr);
			devpriv->dma0Chain = NULL;
		}
#endif /* USE_DMA */

		/* release IRQ */
		if (dev->irq) {
			/* disable interrupt controller */
			RtdPlxInterruptWrite(dev, RtdPlxInterruptRead(dev)
					     & ~(ICS_PLIE | ICS_DMA0_E |
						 ICS_DMA1_E));
			free_irq(dev->irq, dev);
		}

		/* release all regions that were allocated */
		if (devpriv->las0)
			iounmap(devpriv->las0);

		if (devpriv->las1)
			iounmap(devpriv->las1);

		if (devpriv->lcfg)
			iounmap(devpriv->lcfg);

		if (devpriv->pci_dev) {
			if (devpriv->got_regions)
				comedi_pci_disable(devpriv->pci_dev);

			pci_dev_put(devpriv->pci_dev);
		}
	}

	printk(KERN_INFO "comedi%d: rtd520: removed.\n", dev->minor);

	return 0;
}

/*
  Convert a single comedi channel-gain entry to a RTD520 table entry
*/
static unsigned short rtdConvertChanGain(struct comedi_device *dev,
					 unsigned int comediChan, int chanIndex)
{				/* index in channel list */
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
	if (n_chan > 1) {	/* setup channel gain table */
		int ii;
		RtdClearCGT(dev);
		RtdEnableCGT(dev, 1);	/* enable table */
		for (ii = 0; ii < n_chan; ii++) {
			RtdWriteCGTable(dev, rtdConvertChanGain(dev, list[ii],
								ii));
		}
	} else {		/* just use the channel gain latch */
		RtdEnableCGT(dev, 0);	/* disable table, enable latch */
		RtdWriteCGLatch(dev, rtdConvertChanGain(dev, list[0], 0));
	}
}

/* determine fifo size by doing adc conversions until the fifo half
empty status flag clears */
static int rtd520_probe_fifo_depth(struct comedi_device *dev)
{
	unsigned int chanspec = CR_PACK(0, 0, AREF_GROUND);
	unsigned i;
	static const unsigned limit = 0x2000;
	unsigned fifo_size = 0;

	RtdAdcClearFifo(dev);
	rtd_load_channelgain_list(dev, 1, &chanspec);
	RtdAdcConversionSource(dev, 0);	/* software */
	/* convert  samples */
	for (i = 0; i < limit; ++i) {
		unsigned fifo_status;
		/* trigger conversion */
		RtdAdcStart(dev);
		udelay(1);
		fifo_status = RtdFifoStatus(dev);
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
	RtdAdcClearFifo(dev);
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
	int n, ii;
	int stat;

	/* clear any old fifo data */
	RtdAdcClearFifo(dev);

	/* write channel to multiplexer and clear channel gain table */
	rtd_load_channelgain_list(dev, 1, &insn->chanspec);

	/* set conversion source */
	RtdAdcConversionSource(dev, 0);	/* software */

	/* convert n samples */
	for (n = 0; n < insn->n; n++) {
		s16 d;
		/* trigger conversion */
		RtdAdcStart(dev);

		for (ii = 0; ii < RTD_ADC_TIMEOUT; ++ii) {
			stat = RtdFifoStatus(dev);
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
		d = RtdAdcFifoGet(dev);	/* get 2s comp value */
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
	int ii;

	for (ii = 0; ii < count; ii++) {
		short sample;
		s16 d;

		if (0 == devpriv->aiCount) {	/* done */
			d = RtdAdcFifoGet(dev);	/* Read N and discard */
			continue;
		}
#if 0
		if (0 == (RtdFifoStatus(dev) & FS_ADC_NOT_EMPTY)) {	/* DEBUG */
			DPRINTK("comedi: READ OOPS on %d of %d\n", ii + 1,
				count);
			break;
		}
#endif
		d = RtdAdcFifoGet(dev);	/* get 2s comp value */

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
	while (RtdFifoStatus(dev) & FS_ADC_NOT_EMPTY) {	/* 1 -> not empty */
		short sample;
		s16 d = RtdAdcFifoGet(dev);	/* get 2s comp value */

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
	struct comedi_device *dev = d;	/* must be called "dev" for devpriv */
	u16 status;
	u16 fifoStatus;
	struct comedi_subdevice *s = dev->subdevices + 0;	/* analog in subdevice */

	if (!dev->attached)
		return IRQ_NONE;

	devpriv->intCount++;	/* DEBUG statistics */

	fifoStatus = RtdFifoStatus(dev);
	/* check for FIFO full, this automatically halts the ADC! */
	if (!(fifoStatus & FS_ADC_NOT_FULL)) {	/* 0 -> full */
		DPRINTK("rtd520: FIFO full! fifo_status=0x%x\n", (fifoStatus ^ 0x6666) & 0x7777);	/* should be all 0s */
		goto abortTransfer;
	}
#ifdef USE_DMA
	if (devpriv->flags & DMA0_ACTIVE) {	/* Check DMA */
		u32 istatus = RtdPlxInterruptRead(dev);

		if (istatus & ICS_DMA0_A) {
			if (ai_process_dma(dev, s) < 0) {
				DPRINTK
				    ("rtd520: comedi read buffer overflow (DMA) with %ld to go!\n",
				     devpriv->aiCount);
				RtdDma0Control(dev,
					       (devpriv->dma0Control &
						~PLX_DMA_START_BIT)
					       | PLX_CLEAR_DMA_INTR_BIT);
				goto abortTransfer;
			}

			/*DPRINTK ("rtd520: DMA transfer: %ld to go, istatus %x\n",
			   devpriv->aiCount, istatus); */
			RtdDma0Control(dev,
				       (devpriv->
					dma0Control & ~PLX_DMA_START_BIT)
				       | PLX_CLEAR_DMA_INTR_BIT);
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

	status = RtdInterruptStatus(dev);
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

	if (0xffff & RtdInterruptOverrunStatus(dev)) {	/* interrupt overrun */
		DPRINTK
		    ("rtd520: Interrupt overrun with %ld to go! over_status=0x%x\n",
		     devpriv->aiCount, 0xffff & RtdInterruptOverrunStatus(dev));
		goto abortTransfer;
	}

	/* clear the interrupt */
	RtdInterruptClearMask(dev, status);
	RtdInterruptClear(dev);
	return IRQ_HANDLED;

abortTransfer:
	RtdAdcClearFifo(dev);	/* clears full flag */
	s->async->events |= COMEDI_CB_ERROR;
	devpriv->aiCount = 0;	/* stop and don't transfer any more */
	/* fall into transferDone */

transferDone:
	RtdPacerStopSource(dev, 0);	/* stop on SOFTWARE stop */
	RtdPacerStop(dev);	/* Stop PACER */
	RtdAdcConversionSource(dev, 0);	/* software trigger only */
	RtdInterruptMask(dev, 0);	/* mask out SAMPLE */
#ifdef USE_DMA
	if (devpriv->flags & DMA0_ACTIVE) {
		RtdPlxInterruptWrite(dev,	/* disable any more interrupts */
				     RtdPlxInterruptRead(dev) & ~ICS_DMA0_E);
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
		fifoStatus = RtdFifoStatus(dev);
		DPRINTK("rtd520: Finishing up. %ld remain, fifoStat=%x\n", devpriv->aiCount, (fifoStatus ^ 0x6666) & 0x7777);	/* should read all 0s */
		ai_read_dregs(dev, s);	/* read anything left in FIFO */
	}

	s->async->events |= COMEDI_CB_EOA;	/* signal end to comedi */
	comedi_event(dev, s);

	/* clear the interrupt */
	status = RtdInterruptStatus(dev);
	RtdInterruptClearMask(dev, status);
	RtdInterruptClear(dev);

	fifoStatus = RtdFifoStatus(dev);	/* DEBUG */
	DPRINTK
	    ("rtd520: Acquisition complete. %ld ints, intStat=%x, overStat=%x\n",
	     devpriv->intCount, status,
	     0xffff & RtdInterruptOverrunStatus(dev));

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
	struct comedi_cmd *cmd = &s->async->cmd;
	int timer;

	/* stop anything currently running */
	RtdPacerStopSource(dev, 0);	/* stop on SOFTWARE stop */
	RtdPacerStop(dev);	/* make sure PACER is stopped */
	RtdAdcConversionSource(dev, 0);	/* software trigger only */
	RtdInterruptMask(dev, 0);
#ifdef USE_DMA
	if (devpriv->flags & DMA0_ACTIVE) {	/* cancel anything running */
		RtdPlxInterruptWrite(dev,	/* disable any more interrupts */
				     RtdPlxInterruptRead(dev) & ~ICS_DMA0_E);
		abort_dma(dev, 0);
		devpriv->flags &= ~DMA0_ACTIVE;
		if (RtdPlxInterruptRead(dev) & ICS_DMA0_A) {	/*clear pending int */
			RtdDma0Control(dev, PLX_CLEAR_DMA_INTR_BIT);
		}
	}
	RtdDma0Reset(dev);	/* reset onboard state */
#endif /* USE_DMA */
	RtdAdcClearFifo(dev);	/* clear any old data */
	RtdInterruptOverrunClear(dev);
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
		RtdPacerStartSource(dev, 0);	/* software triggers pacer */
		RtdBurstStartSource(dev, 1);	/* PACER triggers burst */
		RtdAdcConversionSource(dev, 2);	/* BURST triggers ADC */
	} else {		/* single channel */
		/*DPRINTK ("rtd520: single channel setup\n"); */
		RtdPacerStartSource(dev, 0);	/* software triggers pacer */
		RtdAdcConversionSource(dev, 1);	/* PACER triggers ADC */
	}
	RtdAboutCounter(dev, devpriv->fifoLen / 2 - 1);	/* 1/2 FIFO */

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
			RtdAboutCounter(dev, devpriv->transCount - 1);
		}

		DPRINTK
		    ("rtd520: scanLen=%d transferCount=%d fifoLen=%d\n  scanTime(ns)=%d flags=0x%x\n",
		     cmd->chanlist_len, devpriv->transCount, devpriv->fifoLen,
		     cmd->scan_begin_arg, devpriv->flags);
	} else {		/* unknown timing, just use 1/2 FIFO */
		devpriv->transCount = 0;
		devpriv->flags &= ~SEND_EOS;
	}
	RtdPacerClockSource(dev, 1);	/* use INTERNAL 8Mhz clock source */
	RtdAboutStopEnable(dev, 1);	/* just interrupt, dont stop */

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
		RtdPacerCounter(dev, timer);

		break;

	case TRIG_EXT:
		RtdPacerStartSource(dev, 1);	/* EXTERNALy trigger pacer */
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
			RtdBurstCounter(dev, timer);
		}

		break;

	case TRIG_EXT:		/* external */
		RtdBurstStartSource(dev, 2);	/* EXTERNALy trigger burst */
		break;

	default:
		DPRINTK("rtd520: Warning! ignoring convert_src mode %d\n",
			cmd->convert_src);
	}
	/* end configuration */

	/* This doesn't seem to work.  There is no way to clear an interrupt
	   that the priority controller has queued! */
	RtdInterruptClearMask(dev, ~0);	/* clear any existing flags */
	RtdInterruptClear(dev);

	/* TODO: allow multiple interrupt sources */
	if (devpriv->transCount > 0) {	/* transfer every N samples */
		RtdInterruptMask(dev, IRQM_ADC_ABOUT_CNT);
		DPRINTK("rtd520: Transferring every %d\n", devpriv->transCount);
	} else {		/* 1/2 FIFO transfers */
#ifdef USE_DMA
		devpriv->flags |= DMA0_ACTIVE;

		/* point to first transfer in ring */
		devpriv->dma0Offset = 0;
		RtdDma0Mode(dev, DMA_MODE_BITS);
		RtdDma0Next(dev,	/* point to first block */
			    devpriv->dma0Chain[DMA_CHAIN_COUNT - 1].next);
		RtdDma0Source(dev, DMAS_ADFIFO_HALF_FULL);	/* set DMA trigger source */

		RtdPlxInterruptWrite(dev,	/* enable interrupt */
				     RtdPlxInterruptRead(dev) | ICS_DMA0_E);
		/* Must be 2 steps.  See PLX app note about "Starting a DMA transfer" */
		RtdDma0Control(dev, PLX_DMA_EN_BIT);	/* enable DMA (clear INTR?) */
		RtdDma0Control(dev, PLX_DMA_EN_BIT | PLX_DMA_START_BIT);	/*start DMA */
		DPRINTK("rtd520: Using DMA0 transfers. plxInt %x RtdInt %x\n",
			RtdPlxInterruptRead(dev), devpriv->intMask);
#else /* USE_DMA */
		RtdInterruptMask(dev, IRQM_ADC_ABOUT_CNT);
		DPRINTK("rtd520: Transferring every 1/2 FIFO\n");
#endif /* USE_DMA */
	}

	/* BUG: start_src is ASSUMED to be TRIG_NOW */
	/* BUG? it seems like things are running before the "start" */
	RtdPacerStart(dev);	/* Start PACER */
	return 0;
}

/*
  Stop a running data acquisition.
*/
static int rtd_ai_cancel(struct comedi_device *dev, struct comedi_subdevice *s)
{
	u16 status;

	RtdPacerStopSource(dev, 0);	/* stop on SOFTWARE stop */
	RtdPacerStop(dev);	/* Stop PACER */
	RtdAdcConversionSource(dev, 0);	/* software trigger only */
	RtdInterruptMask(dev, 0);
	devpriv->aiCount = 0;	/* stop and don't transfer any more */
#ifdef USE_DMA
	if (devpriv->flags & DMA0_ACTIVE) {
		RtdPlxInterruptWrite(dev,	/* disable any more interrupts */
				     RtdPlxInterruptRead(dev) & ~ICS_DMA0_E);
		abort_dma(dev, 0);
		devpriv->flags &= ~DMA0_ACTIVE;
	}
#endif /* USE_DMA */
	status = RtdInterruptStatus(dev);
	DPRINTK
	    ("rtd520: Acquisition canceled. %ld ints, intStat=%x, overStat=%x\n",
	     devpriv->intCount, status,
	     0xffff & RtdInterruptOverrunStatus(dev));
	return 0;
}

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
  Output one (or more) analog values to a single port as fast as possible.
*/
static int rtd_ao_winsn(struct comedi_device *dev,
			struct comedi_subdevice *s, struct comedi_insn *insn,
			unsigned int *data)
{
	int i;
	int chan = CR_CHAN(insn->chanspec);
	int range = CR_RANGE(insn->chanspec);

	/* Configure the output range (table index matches the range values) */
	RtdDacRange(dev, chan, range);

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
		RtdDacFifoPut(dev, chan, val);	/* put the value in */
		RtdDacUpdate(dev, chan);	/* trigger the conversion */

		devpriv->aoValue[chan] = data[i];	/* save for read back */

		for (ii = 0; ii < RTD_DAC_TIMEOUT; ++ii) {
			stat = RtdFifoStatus(dev);
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
	if (insn->n != 2)
		return -EINVAL;

	/* The insn data is a mask in data[0] and the new data
	 * in data[1], each channel cooresponding to a bit. */
	if (data[0]) {
		s->state &= ~data[0];
		s->state |= data[0] & data[1];

		/* Write out the new digital output lines */
		RtdDio0Write(dev, s->state);
	}
	/* on return, data[1] contains the value of the digital
	 * input lines. */
	data[1] = RtdDio0Read(dev);

	/*DPRINTK("rtd520:port_0 wrote: 0x%x read: 0x%x\n", s->state, data[1]); */

	return 2;
}

/*
  Configure one bit on a IO port as Input or Output (hence the name :-).
*/
static int rtd_dio_insn_config(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{
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
	RtdDioStatusWrite(dev, 0x01);	/* make Dio0Ctrl point to direction */
	RtdDio0CtrlWrite(dev, s->io_bits);	/* set direction 1 means Out */
	RtdDioStatusWrite(dev, 0);	/* make Dio0Ctrl clear interrupts */

	/* port1 can only be all input or all output */

	/* there are also 2 user input lines and 2 user output lines */

	return 1;
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
