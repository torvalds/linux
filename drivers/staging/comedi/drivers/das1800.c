/*
    comedi/drivers/das1800.c
    Driver for Keitley das1700/das1800 series boards
    Copyright (C) 2000 Frank Mori Hess <fmhess@users.sourceforge.net>

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 2000 David A. Schleef <ds@schleef.org>

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

************************************************************************
*/
/*
Driver: das1800
Description: Keithley Metrabyte DAS1800 (& compatibles)
Author: Frank Mori Hess <fmhess@users.sourceforge.net>
Devices: [Keithley Metrabyte] DAS-1701ST (das-1701st),
  DAS-1701ST-DA (das-1701st-da), DAS-1701/AO (das-1701ao),
  DAS-1702ST (das-1702st), DAS-1702ST-DA (das-1702st-da),
  DAS-1702HR (das-1702hr), DAS-1702HR-DA (das-1702hr-da),
  DAS-1702/AO (das-1702ao), DAS-1801ST (das-1801st),
  DAS-1801ST-DA (das-1801st-da), DAS-1801HC (das-1801hc),
  DAS-1801AO (das-1801ao), DAS-1802ST (das-1802st),
  DAS-1802ST-DA (das-1802st-da), DAS-1802HR (das-1802hr),
  DAS-1802HR-DA (das-1802hr-da), DAS-1802HC (das-1802hc),
  DAS-1802AO (das-1802ao)
Status: works

The waveform analog output on the 'ao' cards is not supported.
If you need it, send me (Frank Hess) an email.

Configuration options:
  [0] - I/O port base address
  [1] - IRQ (optional, required for timed or externally triggered conversions)
  [2] - DMA0 (optional, requires irq)
  [3] - DMA1 (optional, requires irq and dma0)
*/
/*

This driver supports the following Keithley boards:

das-1701st
das-1701st-da
das-1701ao
das-1702st
das-1702st-da
das-1702hr
das-1702hr-da
das-1702ao
das-1801st
das-1801st-da
das-1801hc
das-1801ao
das-1802st
das-1802st-da
das-1802hr
das-1802hr-da
das-1802hc
das-1802ao

Options:
	[0] - base io address
	[1] - irq (optional, required for timed or externally triggered conversions)
	[2] - dma0 (optional, requires irq)
	[3] - dma1 (optional, requires irq and dma0)

irq can be omitted, although the cmd interface will not work without it.

analog input cmd triggers supported:
	start_src:      TRIG_NOW | TRIG_EXT
	scan_begin_src: TRIG_FOLLOW | TRIG_TIMER | TRIG_EXT
	scan_end_src:   TRIG_COUNT
	convert_src:    TRIG_TIMER | TRIG_EXT (TRIG_EXT requires scan_begin_src == TRIG_FOLLOW)
	stop_src:       TRIG_COUNT | TRIG_EXT | TRIG_NONE

scan_begin_src triggers TRIG_TIMER and TRIG_EXT use the card's
'burst mode' which limits the valid conversion time to 64 microseconds
(convert_arg <= 64000).  This limitation does not apply if scan_begin_src
is TRIG_FOLLOW.

NOTES:
Only the DAS-1801ST has been tested by me.
Unipolar and bipolar ranges cannot be mixed in the channel/gain list.

TODO:
	Make it automatically allocate irq and dma channels if they are not specified
	Add support for analog out on 'ao' cards
	read insn for analog out
*/

#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/io.h>
#include "../comedidev.h"

#include <linux/ioport.h>
#include <asm/dma.h>

#include "8253.h"
#include "comedi_fc.h"

/* misc. defines */
#define DAS1800_SIZE           16	/* uses 16 io addresses */
#define FIFO_SIZE              1024	/*  1024 sample fifo */
#define TIMER_BASE             200	/*  5 Mhz master clock */
#define UNIPOLAR               0x4	/*  bit that determines whether input range is uni/bipolar */
#define DMA_BUF_SIZE           0x1ff00	/*  size in bytes of dma buffers */

/* Registers for the das1800 */
#define DAS1800_FIFO            0x0
#define DAS1800_QRAM            0x0
#define DAS1800_DAC             0x0
#define DAS1800_SELECT          0x2
#define   ADC                     0x0
#define   QRAM                    0x1
#define   DAC(a)                  (0x2 + a)
#define DAS1800_DIGITAL         0x3
#define DAS1800_CONTROL_A       0x4
#define   FFEN                    0x1
#define   CGEN                    0x4
#define   CGSL                    0x8
#define   TGEN                    0x10
#define   TGSL                    0x20
#define   ATEN                    0x80
#define DAS1800_CONTROL_B       0x5
#define   DMA_CH5                 0x1
#define   DMA_CH6                 0x2
#define   DMA_CH7                 0x3
#define   DMA_CH5_CH6             0x5
#define   DMA_CH6_CH7             0x6
#define   DMA_CH7_CH5             0x7
#define   DMA_ENABLED             0x3	/* mask used to determine if dma is enabled */
#define   DMA_DUAL                0x4
#define   IRQ3                    0x8
#define   IRQ5                    0x10
#define   IRQ7                    0x18
#define   IRQ10                   0x28
#define   IRQ11                   0x30
#define   IRQ15                   0x38
#define   FIMD                    0x40
#define DAS1800_CONTROL_C       0X6
#define   IPCLK                   0x1
#define   XPCLK                   0x3
#define   BMDE                    0x4
#define   CMEN                    0x8
#define   UQEN                    0x10
#define   SD                      0x40
#define   UB                      0x80
#define DAS1800_STATUS          0x7
/* bits that prevent interrupt status bits (and CVEN) from being cleared on write */
#define   CLEAR_INTR_MASK         (CVEN_MASK | 0x1f)
#define   INT                     0x1
#define   DMATC                   0x2
#define   CT0TC                   0x8
#define   OVF                     0x10
#define   FHF                     0x20
#define   FNE                     0x40
#define   CVEN_MASK               0x40	/*  masks CVEN on write */
#define   CVEN                    0x80
#define DAS1800_BURST_LENGTH    0x8
#define DAS1800_BURST_RATE      0x9
#define DAS1800_QRAM_ADDRESS    0xa
#define DAS1800_COUNTER         0xc

#define IOBASE2                   0x400	/* offset of additional ioports used on 'ao' cards */

enum {
	das1701st, das1701st_da, das1702st, das1702st_da, das1702hr,
	das1702hr_da,
	das1701ao, das1702ao, das1801st, das1801st_da, das1802st, das1802st_da,
	das1802hr, das1802hr_da, das1801hc, das1802hc, das1801ao, das1802ao
};

/* analog input ranges */
static const struct comedi_lrange range_ai_das1801 = {
	8,
	{
	 RANGE(-5, 5),
	 RANGE(-1, 1),
	 RANGE(-0.1, 0.1),
	 RANGE(-0.02, 0.02),
	 RANGE(0, 5),
	 RANGE(0, 1),
	 RANGE(0, 0.1),
	 RANGE(0, 0.02),
	 }
};

static const struct comedi_lrange range_ai_das1802 = {
	8,
	{
	 RANGE(-10, 10),
	 RANGE(-5, 5),
	 RANGE(-2.5, 2.5),
	 RANGE(-1.25, 1.25),
	 RANGE(0, 10),
	 RANGE(0, 5),
	 RANGE(0, 2.5),
	 RANGE(0, 1.25),
	 }
};

struct das1800_board {
	const char *name;
	int ai_speed;		/* max conversion period in nanoseconds */
	int resolution;		/* bits of ai resolution */
	int qram_len;		/* length of card's channel / gain queue */
	int common;		/* supports AREF_COMMON flag */
	int do_n_chan;		/* number of digital output channels */
	int ao_ability;		/* 0 == no analog out, 1 == basic analog out, 2 == waveform analog out */
	int ao_n_chan;		/* number of analog out channels */
	const struct comedi_lrange *range_ai;	/* available input ranges */
};

/* Warning: the maximum conversion speeds listed below are
 * not always achievable depending on board setup (see
 * user manual.)
 */
static const struct das1800_board das1800_boards[] = {
	{
	 .name = "das-1701st",
	 .ai_speed = 6250,
	 .resolution = 12,
	 .qram_len = 256,
	 .common = 1,
	 .do_n_chan = 4,
	 .ao_ability = 0,
	 .ao_n_chan = 0,
	 .range_ai = &range_ai_das1801,
	 },
	{
	 .name = "das-1701st-da",
	 .ai_speed = 6250,
	 .resolution = 12,
	 .qram_len = 256,
	 .common = 1,
	 .do_n_chan = 4,
	 .ao_ability = 1,
	 .ao_n_chan = 4,
	 .range_ai = &range_ai_das1801,
	 },
	{
	 .name = "das-1702st",
	 .ai_speed = 6250,
	 .resolution = 12,
	 .qram_len = 256,
	 .common = 1,
	 .do_n_chan = 4,
	 .ao_ability = 0,
	 .ao_n_chan = 0,
	 .range_ai = &range_ai_das1802,
	 },
	{
	 .name = "das-1702st-da",
	 .ai_speed = 6250,
	 .resolution = 12,
	 .qram_len = 256,
	 .common = 1,
	 .do_n_chan = 4,
	 .ao_ability = 1,
	 .ao_n_chan = 4,
	 .range_ai = &range_ai_das1802,
	 },
	{
	 .name = "das-1702hr",
	 .ai_speed = 20000,
	 .resolution = 16,
	 .qram_len = 256,
	 .common = 1,
	 .do_n_chan = 4,
	 .ao_ability = 0,
	 .ao_n_chan = 0,
	 .range_ai = &range_ai_das1802,
	 },
	{
	 .name = "das-1702hr-da",
	 .ai_speed = 20000,
	 .resolution = 16,
	 .qram_len = 256,
	 .common = 1,
	 .do_n_chan = 4,
	 .ao_ability = 1,
	 .ao_n_chan = 2,
	 .range_ai = &range_ai_das1802,
	 },
	{
	 .name = "das-1701ao",
	 .ai_speed = 6250,
	 .resolution = 12,
	 .qram_len = 256,
	 .common = 1,
	 .do_n_chan = 4,
	 .ao_ability = 2,
	 .ao_n_chan = 2,
	 .range_ai = &range_ai_das1801,
	 },
	{
	 .name = "das-1702ao",
	 .ai_speed = 6250,
	 .resolution = 12,
	 .qram_len = 256,
	 .common = 1,
	 .do_n_chan = 4,
	 .ao_ability = 2,
	 .ao_n_chan = 2,
	 .range_ai = &range_ai_das1802,
	 },
	{
	 .name = "das-1801st",
	 .ai_speed = 3000,
	 .resolution = 12,
	 .qram_len = 256,
	 .common = 1,
	 .do_n_chan = 4,
	 .ao_ability = 0,
	 .ao_n_chan = 0,
	 .range_ai = &range_ai_das1801,
	 },
	{
	 .name = "das-1801st-da",
	 .ai_speed = 3000,
	 .resolution = 12,
	 .qram_len = 256,
	 .common = 1,
	 .do_n_chan = 4,
	 .ao_ability = 0,
	 .ao_n_chan = 4,
	 .range_ai = &range_ai_das1801,
	 },
	{
	 .name = "das-1802st",
	 .ai_speed = 3000,
	 .resolution = 12,
	 .qram_len = 256,
	 .common = 1,
	 .do_n_chan = 4,
	 .ao_ability = 0,
	 .ao_n_chan = 0,
	 .range_ai = &range_ai_das1802,
	 },
	{
	 .name = "das-1802st-da",
	 .ai_speed = 3000,
	 .resolution = 12,
	 .qram_len = 256,
	 .common = 1,
	 .do_n_chan = 4,
	 .ao_ability = 1,
	 .ao_n_chan = 4,
	 .range_ai = &range_ai_das1802,
	 },
	{
	 .name = "das-1802hr",
	 .ai_speed = 10000,
	 .resolution = 16,
	 .qram_len = 256,
	 .common = 1,
	 .do_n_chan = 4,
	 .ao_ability = 0,
	 .ao_n_chan = 0,
	 .range_ai = &range_ai_das1802,
	 },
	{
	 .name = "das-1802hr-da",
	 .ai_speed = 10000,
	 .resolution = 16,
	 .qram_len = 256,
	 .common = 1,
	 .do_n_chan = 4,
	 .ao_ability = 1,
	 .ao_n_chan = 2,
	 .range_ai = &range_ai_das1802,
	 },
	{
	 .name = "das-1801hc",
	 .ai_speed = 3000,
	 .resolution = 12,
	 .qram_len = 64,
	 .common = 0,
	 .do_n_chan = 8,
	 .ao_ability = 1,
	 .ao_n_chan = 2,
	 .range_ai = &range_ai_das1801,
	 },
	{
	 .name = "das-1802hc",
	 .ai_speed = 3000,
	 .resolution = 12,
	 .qram_len = 64,
	 .common = 0,
	 .do_n_chan = 8,
	 .ao_ability = 1,
	 .ao_n_chan = 2,
	 .range_ai = &range_ai_das1802,
	 },
	{
	 .name = "das-1801ao",
	 .ai_speed = 3000,
	 .resolution = 12,
	 .qram_len = 256,
	 .common = 1,
	 .do_n_chan = 4,
	 .ao_ability = 2,
	 .ao_n_chan = 2,
	 .range_ai = &range_ai_das1801,
	 },
	{
	 .name = "das-1802ao",
	 .ai_speed = 3000,
	 .resolution = 12,
	 .qram_len = 256,
	 .common = 1,
	 .do_n_chan = 4,
	 .ao_ability = 2,
	 .ao_n_chan = 2,
	 .range_ai = &range_ai_das1802,
	 },
};

/*
 * Useful for shorthand access to the particular board structure
 */
#define thisboard ((const struct das1800_board *)dev->board_ptr)

struct das1800_private {
	volatile unsigned int count;	/* number of data points left to be taken */
	unsigned int divisor1;	/* value to load into board's counter 1 for timed conversions */
	unsigned int divisor2;	/* value to load into board's counter 2 for timed conversions */
	int do_bits;		/* digital output bits */
	int irq_dma_bits;	/* bits for control register b */
	/* dma bits for control register b, stored so that dma can be
	 * turned on and off */
	int dma_bits;
	unsigned int dma0;	/* dma channels used */
	unsigned int dma1;
	volatile unsigned int dma_current;	/* dma channel currently in use */
	uint16_t *ai_buf0;	/* pointers to dma buffers */
	uint16_t *ai_buf1;
	uint16_t *dma_current_buf;	/* pointer to dma buffer currently being used */
	unsigned int dma_transfer_size;	/* size of transfer currently used, in bytes */
	unsigned long iobase2;	/* secondary io address used for analog out on 'ao' boards */
	short ao_update_bits;	/* remembers the last write to the 'update' dac */
};

/* analog out range for boards with basic analog out */
static const struct comedi_lrange range_ao_1 = {
	1,
	{
	 RANGE(-10, 10),
	 }
};

/* analog out range for 'ao' boards */
/*
static const struct comedi_lrange range_ao_2 = {
	2,
	{
		RANGE(-10, 10),
		RANGE(-5, 5),
	}
};
*/

static inline uint16_t munge_bipolar_sample(const struct comedi_device *dev,
					    uint16_t sample)
{
	sample += 1 << (thisboard->resolution - 1);
	return sample;
}

static void munge_data(struct comedi_device *dev, uint16_t * array,
		       unsigned int num_elements)
{
	unsigned int i;
	int unipolar;

	/* see if card is using a unipolar or bipolar range so we can munge data correctly */
	unipolar = inb(dev->iobase + DAS1800_CONTROL_C) & UB;

	/* convert to unsigned type if we are in a bipolar mode */
	if (!unipolar) {
		for (i = 0; i < num_elements; i++)
			array[i] = munge_bipolar_sample(dev, array[i]);
	}
}

static void das1800_handle_fifo_half_full(struct comedi_device *dev,
					  struct comedi_subdevice *s)
{
	struct das1800_private *devpriv = dev->private;
	int numPoints = 0;	/* number of points to read */
	struct comedi_cmd *cmd = &s->async->cmd;

	numPoints = FIFO_SIZE / 2;
	/* if we only need some of the points */
	if (cmd->stop_src == TRIG_COUNT && devpriv->count < numPoints)
		numPoints = devpriv->count;
	insw(dev->iobase + DAS1800_FIFO, devpriv->ai_buf0, numPoints);
	munge_data(dev, devpriv->ai_buf0, numPoints);
	cfc_write_array_to_buffer(s, devpriv->ai_buf0,
				  numPoints * sizeof(devpriv->ai_buf0[0]));
	if (cmd->stop_src == TRIG_COUNT)
		devpriv->count -= numPoints;
	return;
}

static void das1800_handle_fifo_not_empty(struct comedi_device *dev,
					  struct comedi_subdevice *s)
{
	struct das1800_private *devpriv = dev->private;
	short dpnt;
	int unipolar;
	struct comedi_cmd *cmd = &s->async->cmd;

	unipolar = inb(dev->iobase + DAS1800_CONTROL_C) & UB;

	while (inb(dev->iobase + DAS1800_STATUS) & FNE) {
		if (cmd->stop_src == TRIG_COUNT && devpriv->count == 0)
			break;
		dpnt = inw(dev->iobase + DAS1800_FIFO);
		/* convert to unsigned type if we are in a bipolar mode */
		if (!unipolar)
			;
		dpnt = munge_bipolar_sample(dev, dpnt);
		cfc_write_to_buffer(s, dpnt);
		if (cmd->stop_src == TRIG_COUNT)
			devpriv->count--;
	}

	return;
}

/* Utility function used by das1800_flush_dma() and das1800_handle_dma().
 * Assumes dma lock is held */
static void das1800_flush_dma_channel(struct comedi_device *dev,
				      struct comedi_subdevice *s,
				      unsigned int channel, uint16_t *buffer)
{
	struct das1800_private *devpriv = dev->private;
	unsigned int num_bytes, num_samples;
	struct comedi_cmd *cmd = &s->async->cmd;

	disable_dma(channel);

	/* clear flip-flop to make sure 2-byte registers
	 * get set correctly */
	clear_dma_ff(channel);

	/*  figure out how many points to read */
	num_bytes = devpriv->dma_transfer_size - get_dma_residue(channel);
	num_samples = num_bytes / sizeof(short);

	/* if we only need some of the points */
	if (cmd->stop_src == TRIG_COUNT && devpriv->count < num_samples)
		num_samples = devpriv->count;

	munge_data(dev, buffer, num_samples);
	cfc_write_array_to_buffer(s, buffer, num_bytes);
	if (s->async->cmd.stop_src == TRIG_COUNT)
		devpriv->count -= num_samples;

	return;
}

/* flushes remaining data from board when external trigger has stopped acquisition
 * and we are using dma transfers */
static void das1800_flush_dma(struct comedi_device *dev,
			      struct comedi_subdevice *s)
{
	struct das1800_private *devpriv = dev->private;
	unsigned long flags;
	const int dual_dma = devpriv->irq_dma_bits & DMA_DUAL;

	flags = claim_dma_lock();
	das1800_flush_dma_channel(dev, s, devpriv->dma_current,
				  devpriv->dma_current_buf);

	if (dual_dma) {
		/*  switch to other channel and flush it */
		if (devpriv->dma_current == devpriv->dma0) {
			devpriv->dma_current = devpriv->dma1;
			devpriv->dma_current_buf = devpriv->ai_buf1;
		} else {
			devpriv->dma_current = devpriv->dma0;
			devpriv->dma_current_buf = devpriv->ai_buf0;
		}
		das1800_flush_dma_channel(dev, s, devpriv->dma_current,
					  devpriv->dma_current_buf);
	}

	release_dma_lock(flags);

	/*  get any remaining samples in fifo */
	das1800_handle_fifo_not_empty(dev, s);

	return;
}

static void das1800_handle_dma(struct comedi_device *dev,
			       struct comedi_subdevice *s, unsigned int status)
{
	struct das1800_private *devpriv = dev->private;
	unsigned long flags;
	const int dual_dma = devpriv->irq_dma_bits & DMA_DUAL;

	flags = claim_dma_lock();
	das1800_flush_dma_channel(dev, s, devpriv->dma_current,
				  devpriv->dma_current_buf);
	/*  re-enable  dma channel */
	set_dma_addr(devpriv->dma_current,
		     virt_to_bus(devpriv->dma_current_buf));
	set_dma_count(devpriv->dma_current, devpriv->dma_transfer_size);
	enable_dma(devpriv->dma_current);
	release_dma_lock(flags);

	if (status & DMATC) {
		/*  clear DMATC interrupt bit */
		outb(CLEAR_INTR_MASK & ~DMATC, dev->iobase + DAS1800_STATUS);
		/*  switch dma channels for next time, if appropriate */
		if (dual_dma) {
			/*  read data from the other channel next time */
			if (devpriv->dma_current == devpriv->dma0) {
				devpriv->dma_current = devpriv->dma1;
				devpriv->dma_current_buf = devpriv->ai_buf1;
			} else {
				devpriv->dma_current = devpriv->dma0;
				devpriv->dma_current_buf = devpriv->ai_buf0;
			}
		}
	}

	return;
}

static int das1800_cancel(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct das1800_private *devpriv = dev->private;

	outb(0x0, dev->iobase + DAS1800_STATUS);	/* disable conversions */
	outb(0x0, dev->iobase + DAS1800_CONTROL_B);	/* disable interrupts and dma */
	outb(0x0, dev->iobase + DAS1800_CONTROL_A);	/* disable and clear fifo and stop triggering */
	if (devpriv->dma0)
		disable_dma(devpriv->dma0);
	if (devpriv->dma1)
		disable_dma(devpriv->dma1);
	return 0;
}

/* the guts of the interrupt handler, that is shared with das1800_ai_poll */
static void das1800_ai_handler(struct comedi_device *dev)
{
	struct das1800_private *devpriv = dev->private;
	struct comedi_subdevice *s = &dev->subdevices[0];
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;
	unsigned int status = inb(dev->iobase + DAS1800_STATUS);

	async->events = 0;
	/*  select adc for base address + 0 */
	outb(ADC, dev->iobase + DAS1800_SELECT);
	/*  dma buffer full */
	if (devpriv->irq_dma_bits & DMA_ENABLED) {
		/*  look for data from dma transfer even if dma terminal count hasn't happened yet */
		das1800_handle_dma(dev, s, status);
	} else if (status & FHF) {	/*  if fifo half full */
		das1800_handle_fifo_half_full(dev, s);
	} else if (status & FNE) {	/*  if fifo not empty */
		das1800_handle_fifo_not_empty(dev, s);
	}

	async->events |= COMEDI_CB_BLOCK;
	/* if the card's fifo has overflowed */
	if (status & OVF) {
		/*  clear OVF interrupt bit */
		outb(CLEAR_INTR_MASK & ~OVF, dev->iobase + DAS1800_STATUS);
		comedi_error(dev, "DAS1800 FIFO overflow");
		das1800_cancel(dev, s);
		async->events |= COMEDI_CB_ERROR | COMEDI_CB_EOA;
		comedi_event(dev, s);
		return;
	}
	/*  stop taking data if appropriate */
	/* stop_src TRIG_EXT */
	if (status & CT0TC) {
		/*  clear CT0TC interrupt bit */
		outb(CLEAR_INTR_MASK & ~CT0TC, dev->iobase + DAS1800_STATUS);
		/*  make sure we get all remaining data from board before quitting */
		if (devpriv->irq_dma_bits & DMA_ENABLED)
			das1800_flush_dma(dev, s);
		else
			das1800_handle_fifo_not_empty(dev, s);
		das1800_cancel(dev, s);	/* disable hardware conversions */
		async->events |= COMEDI_CB_EOA;
	} else if (cmd->stop_src == TRIG_COUNT && devpriv->count == 0) {	/*  stop_src TRIG_COUNT */
		das1800_cancel(dev, s);	/* disable hardware conversions */
		async->events |= COMEDI_CB_EOA;
	}

	comedi_event(dev, s);

	return;
}

static int das1800_ai_poll(struct comedi_device *dev,
			   struct comedi_subdevice *s)
{
	unsigned long flags;

	/*  prevent race with interrupt handler */
	spin_lock_irqsave(&dev->spinlock, flags);
	das1800_ai_handler(dev);
	spin_unlock_irqrestore(&dev->spinlock, flags);

	return s->async->buf_write_count - s->async->buf_read_count;
}

static irqreturn_t das1800_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	unsigned int status;

	if (dev->attached == 0) {
		comedi_error(dev, "premature interrupt");
		return IRQ_HANDLED;
	}

	/* Prevent race with das1800_ai_poll() on multi processor systems.
	 * Also protects indirect addressing in das1800_ai_handler */
	spin_lock(&dev->spinlock);
	status = inb(dev->iobase + DAS1800_STATUS);

	/* if interrupt was not caused by das-1800 */
	if (!(status & INT)) {
		spin_unlock(&dev->spinlock);
		return IRQ_NONE;
	}
	/* clear the interrupt status bit INT */
	outb(CLEAR_INTR_MASK & ~INT, dev->iobase + DAS1800_STATUS);
	/*  handle interrupt */
	das1800_ai_handler(dev);

	spin_unlock(&dev->spinlock);
	return IRQ_HANDLED;
}

/* converts requested conversion timing to timing compatible with
 * hardware, used only when card is in 'burst mode'
 */
static unsigned int burst_convert_arg(unsigned int convert_arg, int round_mode)
{
	unsigned int micro_sec;

	/*  in burst mode, the maximum conversion time is 64 microseconds */
	if (convert_arg > 64000)
		convert_arg = 64000;

	/*  the conversion time must be an integral number of microseconds */
	switch (round_mode) {
	case TRIG_ROUND_NEAREST:
	default:
		micro_sec = (convert_arg + 500) / 1000;
		break;
	case TRIG_ROUND_DOWN:
		micro_sec = convert_arg / 1000;
		break;
	case TRIG_ROUND_UP:
		micro_sec = (convert_arg - 1) / 1000 + 1;
		break;
	}

	/*  return number of nanoseconds */
	return micro_sec * 1000;
}

/* test analog input cmd */
static int das1800_ai_do_cmdtest(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_cmd *cmd)
{
	struct das1800_private *devpriv = dev->private;
	int err = 0;
	unsigned int tmp_arg;
	int i;
	int unipolar;

	/* Step 1 : check if triggers are trivially valid */

	err |= cfc_check_trigger_src(&cmd->start_src, TRIG_NOW | TRIG_EXT);
	err |= cfc_check_trigger_src(&cmd->scan_begin_src,
					TRIG_FOLLOW | TRIG_TIMER | TRIG_EXT);
	err |= cfc_check_trigger_src(&cmd->convert_src, TRIG_TIMER | TRIG_EXT);
	err |= cfc_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= cfc_check_trigger_src(&cmd->stop_src,
					TRIG_COUNT | TRIG_EXT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= cfc_check_trigger_is_unique(cmd->start_src);
	err |= cfc_check_trigger_is_unique(cmd->scan_begin_src);
	err |= cfc_check_trigger_is_unique(cmd->convert_src);
	err |= cfc_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	if (cmd->scan_begin_src != TRIG_FOLLOW &&
	    cmd->convert_src != TRIG_TIMER)
		err |= -EINVAL;

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	err |= cfc_check_trigger_arg_is(&cmd->start_arg, 0);

	if (cmd->convert_src == TRIG_TIMER)
		err |= cfc_check_trigger_arg_min(&cmd->convert_arg,
						 thisboard->ai_speed);

	err |= cfc_check_trigger_arg_min(&cmd->chanlist_len, 1);
	err |= cfc_check_trigger_arg_is(&cmd->scan_end_arg, cmd->chanlist_len);

	switch (cmd->stop_src) {
	case TRIG_COUNT:
		err |= cfc_check_trigger_arg_min(&cmd->stop_arg, 1);
		break;
	case TRIG_NONE:
		err |= cfc_check_trigger_arg_is(&cmd->stop_arg, 0);
		break;
	default:
		break;
	}

	if (err)
		return 3;

	/* step 4: fix up any arguments */

	if (cmd->convert_src == TRIG_TIMER) {
		/*  if we are not in burst mode */
		if (cmd->scan_begin_src == TRIG_FOLLOW) {
			tmp_arg = cmd->convert_arg;
			/* calculate counter values that give desired timing */
			i8253_cascade_ns_to_timer_2div(TIMER_BASE,
						       &(devpriv->divisor1),
						       &(devpriv->divisor2),
						       &(cmd->convert_arg),
						       cmd->
						       flags & TRIG_ROUND_MASK);
			if (tmp_arg != cmd->convert_arg)
				err++;
		}
		/*  if we are in burst mode */
		else {
			/*  check that convert_arg is compatible */
			tmp_arg = cmd->convert_arg;
			cmd->convert_arg =
			    burst_convert_arg(cmd->convert_arg,
					      cmd->flags & TRIG_ROUND_MASK);
			if (tmp_arg != cmd->convert_arg)
				err++;

			if (cmd->scan_begin_src == TRIG_TIMER) {
				/*  if scans are timed faster than conversion rate allows */
				if (cmd->convert_arg * cmd->chanlist_len >
				    cmd->scan_begin_arg) {
					cmd->scan_begin_arg =
					    cmd->convert_arg *
					    cmd->chanlist_len;
					err++;
				}
				tmp_arg = cmd->scan_begin_arg;
				/* calculate counter values that give desired timing */
				i8253_cascade_ns_to_timer_2div(TIMER_BASE,
							       &(devpriv->
								 divisor1),
							       &(devpriv->
								 divisor2),
							       &(cmd->
								 scan_begin_arg),
							       cmd->
							       flags &
							       TRIG_ROUND_MASK);
				if (tmp_arg != cmd->scan_begin_arg)
					err++;
			}
		}
	}

	if (err)
		return 4;

	/*  make sure user is not trying to mix unipolar and bipolar ranges */
	if (cmd->chanlist) {
		unipolar = CR_RANGE(cmd->chanlist[0]) & UNIPOLAR;
		for (i = 1; i < cmd->chanlist_len; i++) {
			if (unipolar != (CR_RANGE(cmd->chanlist[i]) & UNIPOLAR)) {
				comedi_error(dev,
					     "unipolar and bipolar ranges cannot be mixed in the chanlist");
				err++;
				break;
			}
		}
	}

	if (err)
		return 5;

	return 0;
}

/* returns appropriate bits for control register a, depending on command */
static int control_a_bits(const struct comedi_cmd *cmd)
{
	int control_a;

	control_a = FFEN;	/* enable fifo */
	if (cmd->stop_src == TRIG_EXT)
		control_a |= ATEN;
	switch (cmd->start_src) {
	case TRIG_EXT:
		control_a |= TGEN | CGSL;
		break;
	case TRIG_NOW:
		control_a |= CGEN;
		break;
	default:
		break;
	}

	return control_a;
}

/* returns appropriate bits for control register c, depending on command */
static int control_c_bits(const struct comedi_cmd *cmd)
{
	int control_c;
	int aref;

	/* set clock source to internal or external, select analog reference,
	 * select unipolar / bipolar
	 */
	aref = CR_AREF(cmd->chanlist[0]);
	control_c = UQEN;	/* enable upper qram addresses */
	if (aref != AREF_DIFF)
		control_c |= SD;
	if (aref == AREF_COMMON)
		control_c |= CMEN;
	/* if a unipolar range was selected */
	if (CR_RANGE(cmd->chanlist[0]) & UNIPOLAR)
		control_c |= UB;
	switch (cmd->scan_begin_src) {
	case TRIG_FOLLOW:	/*  not in burst mode */
		switch (cmd->convert_src) {
		case TRIG_TIMER:
			/* trig on cascaded counters */
			control_c |= IPCLK;
			break;
		case TRIG_EXT:
			/* trig on falling edge of external trigger */
			control_c |= XPCLK;
			break;
		default:
			break;
		}
		break;
	case TRIG_TIMER:
		/*  burst mode with internal pacer clock */
		control_c |= BMDE | IPCLK;
		break;
	case TRIG_EXT:
		/*  burst mode with external trigger */
		control_c |= BMDE | XPCLK;
		break;
	default:
		break;
	}

	return control_c;
}

/* loads counters with divisor1, divisor2 from private structure */
static int das1800_set_frequency(struct comedi_device *dev)
{
	struct das1800_private *devpriv = dev->private;
	int err = 0;

	/*  counter 1, mode 2 */
	if (i8254_load(dev->iobase + DAS1800_COUNTER, 0, 1, devpriv->divisor1,
		       2))
		err++;
	/*  counter 2, mode 2 */
	if (i8254_load(dev->iobase + DAS1800_COUNTER, 0, 2, devpriv->divisor2,
		       2))
		err++;
	if (err)
		return -1;

	return 0;
}

/* sets up counters */
static int setup_counters(struct comedi_device *dev,
			  const struct comedi_cmd *cmd)
{
	struct das1800_private *devpriv = dev->private;
	unsigned int period;

	/*  setup cascaded counters for conversion/scan frequency */
	switch (cmd->scan_begin_src) {
	case TRIG_FOLLOW:	/*  not in burst mode */
		if (cmd->convert_src == TRIG_TIMER) {
			/* set conversion frequency */
			period = cmd->convert_arg;
			i8253_cascade_ns_to_timer_2div(TIMER_BASE,
						       &devpriv->divisor1,
						       &devpriv->divisor2,
						       &period,
						       cmd->flags &
							TRIG_ROUND_MASK);
			if (das1800_set_frequency(dev) < 0)
				return -1;
		}
		break;
	case TRIG_TIMER:	/*  in burst mode */
		/* set scan frequency */
		period = cmd->scan_begin_arg;
		i8253_cascade_ns_to_timer_2div(TIMER_BASE, &devpriv->divisor1,
					       &devpriv->divisor2, &period,
					       cmd->flags & TRIG_ROUND_MASK);
		if (das1800_set_frequency(dev) < 0)
			return -1;
		break;
	default:
		break;
	}

	/*  setup counter 0 for 'about triggering' */
	if (cmd->stop_src == TRIG_EXT) {
		/*  load counter 0 in mode 0 */
		i8254_load(dev->iobase + DAS1800_COUNTER, 0, 0, 1, 0);
	}

	return 0;
}

/* utility function that suggests a dma transfer size based on the conversion period 'ns' */
static unsigned int suggest_transfer_size(const struct comedi_cmd *cmd)
{
	unsigned int size = DMA_BUF_SIZE;
	static const int sample_size = 2;	/*  size in bytes of one sample from board */
	unsigned int fill_time = 300000000;	/*  target time in nanoseconds for filling dma buffer */
	unsigned int max_size;	/*  maximum size we will allow for a transfer */

	/*  make dma buffer fill in 0.3 seconds for timed modes */
	switch (cmd->scan_begin_src) {
	case TRIG_FOLLOW:	/*  not in burst mode */
		if (cmd->convert_src == TRIG_TIMER)
			size = (fill_time / cmd->convert_arg) * sample_size;
		break;
	case TRIG_TIMER:
		size = (fill_time / (cmd->scan_begin_arg * cmd->chanlist_len)) *
		    sample_size;
		break;
	default:
		size = DMA_BUF_SIZE;
		break;
	}

	/*  set a minimum and maximum size allowed */
	max_size = DMA_BUF_SIZE;
	/*  if we are taking limited number of conversions, limit transfer size to that */
	if (cmd->stop_src == TRIG_COUNT &&
	    cmd->stop_arg * cmd->chanlist_len * sample_size < max_size)
		max_size = cmd->stop_arg * cmd->chanlist_len * sample_size;

	if (size > max_size)
		size = max_size;
	if (size < sample_size)
		size = sample_size;

	return size;
}

/* sets up dma */
static void setup_dma(struct comedi_device *dev, const struct comedi_cmd *cmd)
{
	struct das1800_private *devpriv = dev->private;
	unsigned long lock_flags;
	const int dual_dma = devpriv->irq_dma_bits & DMA_DUAL;

	if ((devpriv->irq_dma_bits & DMA_ENABLED) == 0)
		return;

	/* determine a reasonable dma transfer size */
	devpriv->dma_transfer_size = suggest_transfer_size(cmd);
	lock_flags = claim_dma_lock();
	disable_dma(devpriv->dma0);
	/* clear flip-flop to make sure 2-byte registers for
	 * count and address get set correctly */
	clear_dma_ff(devpriv->dma0);
	set_dma_addr(devpriv->dma0, virt_to_bus(devpriv->ai_buf0));
	/*  set appropriate size of transfer */
	set_dma_count(devpriv->dma0, devpriv->dma_transfer_size);
	devpriv->dma_current = devpriv->dma0;
	devpriv->dma_current_buf = devpriv->ai_buf0;
	enable_dma(devpriv->dma0);
	/*  set up dual dma if appropriate */
	if (dual_dma) {
		disable_dma(devpriv->dma1);
		/* clear flip-flop to make sure 2-byte registers for
		 * count and address get set correctly */
		clear_dma_ff(devpriv->dma1);
		set_dma_addr(devpriv->dma1, virt_to_bus(devpriv->ai_buf1));
		/*  set appropriate size of transfer */
		set_dma_count(devpriv->dma1, devpriv->dma_transfer_size);
		enable_dma(devpriv->dma1);
	}
	release_dma_lock(lock_flags);

	return;
}

/* programs channel/gain list into card */
static void program_chanlist(struct comedi_device *dev,
			     const struct comedi_cmd *cmd)
{
	int i, n, chan_range;
	unsigned long irq_flags;
	const int range_mask = 0x3;	/* masks unipolar/bipolar bit off range */
	const int range_bitshift = 8;

	n = cmd->chanlist_len;
	/*  spinlock protects indirect addressing */
	spin_lock_irqsave(&dev->spinlock, irq_flags);
	outb(QRAM, dev->iobase + DAS1800_SELECT);	/* select QRAM for baseAddress + 0x0 */
	outb(n - 1, dev->iobase + DAS1800_QRAM_ADDRESS);	/*set QRAM address start */
	/* make channel / gain list */
	for (i = 0; i < n; i++) {
		chan_range =
		    CR_CHAN(cmd->chanlist[i]) |
		    ((CR_RANGE(cmd->chanlist[i]) & range_mask) <<
		     range_bitshift);
		outw(chan_range, dev->iobase + DAS1800_QRAM);
	}
	outb(n - 1, dev->iobase + DAS1800_QRAM_ADDRESS);	/*finish write to QRAM */
	spin_unlock_irqrestore(&dev->spinlock, irq_flags);

	return;
}

/* analog input do_cmd */
static int das1800_ai_do_cmd(struct comedi_device *dev,
			     struct comedi_subdevice *s)
{
	struct das1800_private *devpriv = dev->private;
	int ret;
	int control_a, control_c;
	struct comedi_async *async = s->async;
	const struct comedi_cmd *cmd = &async->cmd;

	if (!dev->irq) {
		comedi_error(dev,
			     "no irq assigned for das-1800, cannot do hardware conversions");
		return -1;
	}

	/* disable dma on TRIG_WAKE_EOS, or TRIG_RT
	 * (because dma in handler is unsafe at hard real-time priority) */
	if (cmd->flags & (TRIG_WAKE_EOS | TRIG_RT))
		devpriv->irq_dma_bits &= ~DMA_ENABLED;
	else
		devpriv->irq_dma_bits |= devpriv->dma_bits;
	/*  interrupt on end of conversion for TRIG_WAKE_EOS */
	if (cmd->flags & TRIG_WAKE_EOS) {
		/*  interrupt fifo not empty */
		devpriv->irq_dma_bits &= ~FIMD;
	} else {
		/*  interrupt fifo half full */
		devpriv->irq_dma_bits |= FIMD;
	}
	/*  determine how many conversions we need */
	if (cmd->stop_src == TRIG_COUNT)
		devpriv->count = cmd->stop_arg * cmd->chanlist_len;

	das1800_cancel(dev, s);

	/*  determine proper bits for control registers */
	control_a = control_a_bits(cmd);
	control_c = control_c_bits(cmd);

	/* setup card and start */
	program_chanlist(dev, cmd);
	ret = setup_counters(dev, cmd);
	if (ret < 0) {
		comedi_error(dev, "Error setting up counters");
		return ret;
	}
	setup_dma(dev, cmd);
	outb(control_c, dev->iobase + DAS1800_CONTROL_C);
	/*  set conversion rate and length for burst mode */
	if (control_c & BMDE) {
		/*  program conversion period with number of microseconds minus 1 */
		outb(cmd->convert_arg / 1000 - 1,
		     dev->iobase + DAS1800_BURST_RATE);
		outb(cmd->chanlist_len - 1, dev->iobase + DAS1800_BURST_LENGTH);
	}
	outb(devpriv->irq_dma_bits, dev->iobase + DAS1800_CONTROL_B);	/*  enable irq/dma */
	outb(control_a, dev->iobase + DAS1800_CONTROL_A);	/* enable fifo and triggering */
	outb(CVEN, dev->iobase + DAS1800_STATUS);	/* enable conversions */

	return 0;
}

/* read analog input */
static int das1800_ai_rinsn(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn, unsigned int *data)
{
	int i, n;
	int chan, range, aref, chan_range;
	int timeout = 1000;
	short dpnt;
	int conv_flags = 0;
	unsigned long irq_flags;

	/* set up analog reference and unipolar / bipolar mode */
	aref = CR_AREF(insn->chanspec);
	conv_flags |= UQEN;
	if (aref != AREF_DIFF)
		conv_flags |= SD;
	if (aref == AREF_COMMON)
		conv_flags |= CMEN;
	/* if a unipolar range was selected */
	if (CR_RANGE(insn->chanspec) & UNIPOLAR)
		conv_flags |= UB;

	outb(conv_flags, dev->iobase + DAS1800_CONTROL_C);	/* software conversion enabled */
	outb(CVEN, dev->iobase + DAS1800_STATUS);	/* enable conversions */
	outb(0x0, dev->iobase + DAS1800_CONTROL_A);	/* reset fifo */
	outb(FFEN, dev->iobase + DAS1800_CONTROL_A);

	chan = CR_CHAN(insn->chanspec);
	/* mask of unipolar/bipolar bit from range */
	range = CR_RANGE(insn->chanspec) & 0x3;
	chan_range = chan | (range << 8);
	spin_lock_irqsave(&dev->spinlock, irq_flags);
	outb(QRAM, dev->iobase + DAS1800_SELECT);	/* select QRAM for baseAddress + 0x0 */
	outb(0x0, dev->iobase + DAS1800_QRAM_ADDRESS);	/* set QRAM address start */
	outw(chan_range, dev->iobase + DAS1800_QRAM);
	outb(0x0, dev->iobase + DAS1800_QRAM_ADDRESS);	/*finish write to QRAM */
	outb(ADC, dev->iobase + DAS1800_SELECT);	/* select ADC for baseAddress + 0x0 */

	for (n = 0; n < insn->n; n++) {
		/* trigger conversion */
		outb(0, dev->iobase + DAS1800_FIFO);
		for (i = 0; i < timeout; i++) {
			if (inb(dev->iobase + DAS1800_STATUS) & FNE)
				break;
		}
		if (i == timeout) {
			comedi_error(dev, "timeout");
			n = -ETIME;
			goto exit;
		}
		dpnt = inw(dev->iobase + DAS1800_FIFO);
		/* shift data to offset binary for bipolar ranges */
		if ((conv_flags & UB) == 0)
			dpnt += 1 << (thisboard->resolution - 1);
		data[n] = dpnt;
	}
exit:
	spin_unlock_irqrestore(&dev->spinlock, irq_flags);

	return n;
}

/* writes to an analog output channel */
static int das1800_ao_winsn(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn, unsigned int *data)
{
	struct das1800_private *devpriv = dev->private;
	int chan = CR_CHAN(insn->chanspec);
/* int range = CR_RANGE(insn->chanspec); */
	int update_chan = thisboard->ao_n_chan - 1;
	short output;
	unsigned long irq_flags;

	/*   card expects two's complement data */
	output = data[0] - (1 << (thisboard->resolution - 1));
	/*  if the write is to the 'update' channel, we need to remember its value */
	if (chan == update_chan)
		devpriv->ao_update_bits = output;
	/*  write to channel */
	spin_lock_irqsave(&dev->spinlock, irq_flags);
	outb(DAC(chan), dev->iobase + DAS1800_SELECT);	/* select dac channel for baseAddress + 0x0 */
	outw(output, dev->iobase + DAS1800_DAC);
	/*  now we need to write to 'update' channel to update all dac channels */
	if (chan != update_chan) {
		outb(DAC(update_chan), dev->iobase + DAS1800_SELECT);	/* select 'update' channel for baseAddress + 0x0 */
		outw(devpriv->ao_update_bits, dev->iobase + DAS1800_DAC);
	}
	spin_unlock_irqrestore(&dev->spinlock, irq_flags);

	return 1;
}

/* reads from digital input channels */
static int das1800_di_rbits(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn, unsigned int *data)
{

	data[1] = inb(dev->iobase + DAS1800_DIGITAL) & 0xf;
	data[0] = 0;

	return insn->n;
}

/* writes to digital output channels */
static int das1800_do_wbits(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn, unsigned int *data)
{
	struct das1800_private *devpriv = dev->private;
	unsigned int wbits;

	/*  only set bits that have been masked */
	data[0] &= (1 << s->n_chan) - 1;
	wbits = devpriv->do_bits;
	wbits &= ~data[0];
	wbits |= data[0] & data[1];
	devpriv->do_bits = wbits;

	outb(devpriv->do_bits, dev->iobase + DAS1800_DIGITAL);

	data[1] = devpriv->do_bits;

	return insn->n;
}

static int das1800_init_dma(struct comedi_device *dev, unsigned int dma0,
			    unsigned int dma1)
{
	struct das1800_private *devpriv = dev->private;
	unsigned long flags;

	/*  need an irq to do dma */
	if (dev->irq && dma0) {
		/* encode dma0 and dma1 into 2 digit hexadecimal for switch */
		switch ((dma0 & 0x7) | (dma1 << 4)) {
		case 0x5:	/*  dma0 == 5 */
			devpriv->dma_bits |= DMA_CH5;
			break;
		case 0x6:	/*  dma0 == 6 */
			devpriv->dma_bits |= DMA_CH6;
			break;
		case 0x7:	/*  dma0 == 7 */
			devpriv->dma_bits |= DMA_CH7;
			break;
		case 0x65:	/*  dma0 == 5, dma1 == 6 */
			devpriv->dma_bits |= DMA_CH5_CH6;
			break;
		case 0x76:	/*  dma0 == 6, dma1 == 7 */
			devpriv->dma_bits |= DMA_CH6_CH7;
			break;
		case 0x57:	/*  dma0 == 7, dma1 == 5 */
			devpriv->dma_bits |= DMA_CH7_CH5;
			break;
		default:
			dev_err(dev->class_dev,
				"only supports dma channels 5 through 7\n");
			dev_err(dev->class_dev,
				"Dual dma only allows the following combinations:\n");
			dev_err(dev->class_dev,
				"dma 5,6 / 6,7 / or 7,5\n");
			return -EINVAL;
			break;
		}
		if (request_dma(dma0, dev->driver->driver_name)) {
			dev_err(dev->class_dev,
				"failed to allocate dma channel %i\n", dma0);
			return -EINVAL;
		}
		devpriv->dma0 = dma0;
		devpriv->dma_current = dma0;
		if (dma1) {
			if (request_dma(dma1, dev->driver->driver_name)) {
				dev_err(dev->class_dev,
					"failed to allocate dma channel %i\n",
					dma1);
				return -EINVAL;
			}
			devpriv->dma1 = dma1;
		}
		devpriv->ai_buf0 = kmalloc(DMA_BUF_SIZE, GFP_KERNEL | GFP_DMA);
		if (devpriv->ai_buf0 == NULL)
			return -ENOMEM;
		devpriv->dma_current_buf = devpriv->ai_buf0;
		if (dma1) {
			devpriv->ai_buf1 =
			    kmalloc(DMA_BUF_SIZE, GFP_KERNEL | GFP_DMA);
			if (devpriv->ai_buf1 == NULL)
				return -ENOMEM;
		}
		flags = claim_dma_lock();
		disable_dma(devpriv->dma0);
		set_dma_mode(devpriv->dma0, DMA_MODE_READ);
		if (dma1) {
			disable_dma(devpriv->dma1);
			set_dma_mode(devpriv->dma1, DMA_MODE_READ);
		}
		release_dma_lock(flags);
	}
	return 0;
}

static int das1800_probe(struct comedi_device *dev)
{
	int id;
	int board;

	id = (inb(dev->iobase + DAS1800_DIGITAL) >> 4) & 0xf;	/* get id bits */
	board = ((struct das1800_board *)dev->board_ptr) - das1800_boards;

	switch (id) {
	case 0x3:
		if (board == das1801st_da || board == das1802st_da ||
		    board == das1701st_da || board == das1702st_da) {
			dev_dbg(dev->class_dev, "Board model: %s\n",
				das1800_boards[board].name);
			return board;
		}
		printk
		    (" Board model (probed, not recommended): das-1800st-da series\n");
		return das1801st;
		break;
	case 0x4:
		if (board == das1802hr_da || board == das1702hr_da) {
			dev_dbg(dev->class_dev, "Board model: %s\n",
				das1800_boards[board].name);
			return board;
		}
		printk
		    (" Board model (probed, not recommended): das-1802hr-da\n");
		return das1802hr;
		break;
	case 0x5:
		if (board == das1801ao || board == das1802ao ||
		    board == das1701ao || board == das1702ao) {
			dev_dbg(dev->class_dev, "Board model: %s\n",
				das1800_boards[board].name);
			return board;
		}
		printk
		    (" Board model (probed, not recommended): das-1800ao series\n");
		return das1801ao;
		break;
	case 0x6:
		if (board == das1802hr || board == das1702hr) {
			dev_dbg(dev->class_dev, "Board model: %s\n",
				das1800_boards[board].name);
			return board;
		}
		printk
		    (" Board model (probed, not recommended): das-1802hr\n");
		return das1802hr;
		break;
	case 0x7:
		if (board == das1801st || board == das1802st ||
		    board == das1701st || board == das1702st) {
			dev_dbg(dev->class_dev, "Board model: %s\n",
				das1800_boards[board].name);
			return board;
		}
		printk
		    (" Board model (probed, not recommended): das-1800st series\n");
		return das1801st;
		break;
	case 0x8:
		if (board == das1801hc || board == das1802hc) {
			dev_dbg(dev->class_dev, "Board model: %s\n",
				das1800_boards[board].name);
			return board;
		}
		printk
		    (" Board model (probed, not recommended): das-1800hc series\n");
		return das1801hc;
		break;
	default:
		printk
		    (" Board model: probe returned 0x%x (unknown, please report)\n",
		     id);
		return board;
		break;
	}
	return -1;
}

static int das1800_attach(struct comedi_device *dev,
			  struct comedi_devconfig *it)
{
	struct das1800_private *devpriv;
	struct comedi_subdevice *s;
	unsigned long iobase = it->options[0];
	unsigned int irq = it->options[1];
	unsigned int dma0 = it->options[2];
	unsigned int dma1 = it->options[3];
	unsigned long iobase2;
	int board;
	int retval;

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

	printk(KERN_DEBUG "comedi%d: %s: io 0x%lx", dev->minor,
	       dev->driver->driver_name, iobase);
	if (irq) {
		printk(KERN_CONT ", irq %u", irq);
		if (dma0) {
			printk(KERN_CONT ", dma %u", dma0);
			if (dma1)
				printk(KERN_CONT " and %u", dma1);
		}
	}
	printk(KERN_CONT "\n");

	if (iobase == 0) {
		dev_err(dev->class_dev, "io base address required\n");
		return -EINVAL;
	}

	/* check if io addresses are available */
	if (!request_region(iobase, DAS1800_SIZE, dev->driver->driver_name)) {
		printk
		    (" I/O port conflict: failed to allocate ports 0x%lx to 0x%lx\n",
		     iobase, iobase + DAS1800_SIZE - 1);
		return -EIO;
	}
	dev->iobase = iobase;

	board = das1800_probe(dev);
	if (board < 0) {
		dev_err(dev->class_dev, "unable to determine board type\n");
		return -ENODEV;
	}

	dev->board_ptr = das1800_boards + board;
	dev->board_name = thisboard->name;

	/*  if it is an 'ao' board with fancy analog out then we need extra io ports */
	if (thisboard->ao_ability == 2) {
		iobase2 = iobase + IOBASE2;
		if (!request_region(iobase2, DAS1800_SIZE,
				    dev->driver->driver_name)) {
			printk
			    (" I/O port conflict: failed to allocate ports 0x%lx to 0x%lx\n",
			     iobase2, iobase2 + DAS1800_SIZE - 1);
			return -EIO;
		}
		devpriv->iobase2 = iobase2;
	}

	/* grab our IRQ */
	if (irq) {
		if (request_irq(irq, das1800_interrupt, 0,
				dev->driver->driver_name, dev)) {
			dev_dbg(dev->class_dev, "unable to allocate irq %u\n",
				irq);
			return -EINVAL;
		}
	}
	dev->irq = irq;

	/*  set bits that tell card which irq to use */
	switch (irq) {
	case 0:
		break;
	case 3:
		devpriv->irq_dma_bits |= 0x8;
		break;
	case 5:
		devpriv->irq_dma_bits |= 0x10;
		break;
	case 7:
		devpriv->irq_dma_bits |= 0x18;
		break;
	case 10:
		devpriv->irq_dma_bits |= 0x28;
		break;
	case 11:
		devpriv->irq_dma_bits |= 0x30;
		break;
	case 15:
		devpriv->irq_dma_bits |= 0x38;
		break;
	default:
		dev_err(dev->class_dev, "irq out of range\n");
		return -EINVAL;
		break;
	}

	retval = das1800_init_dma(dev, dma0, dma1);
	if (retval < 0)
		return retval;

	if (devpriv->ai_buf0 == NULL) {
		devpriv->ai_buf0 =
		    kmalloc(FIFO_SIZE * sizeof(uint16_t), GFP_KERNEL);
		if (devpriv->ai_buf0 == NULL)
			return -ENOMEM;
	}

	retval = comedi_alloc_subdevices(dev, 4);
	if (retval)
		return retval;

	/* analog input subdevice */
	s = &dev->subdevices[0];
	dev->read_subdev = s;
	s->type = COMEDI_SUBD_AI;
	s->subdev_flags = SDF_READABLE | SDF_DIFF | SDF_GROUND | SDF_CMD_READ;
	if (thisboard->common)
		s->subdev_flags |= SDF_COMMON;
	s->n_chan = thisboard->qram_len;
	s->len_chanlist = thisboard->qram_len;
	s->maxdata = (1 << thisboard->resolution) - 1;
	s->range_table = thisboard->range_ai;
	s->do_cmd = das1800_ai_do_cmd;
	s->do_cmdtest = das1800_ai_do_cmdtest;
	s->insn_read = das1800_ai_rinsn;
	s->poll = das1800_ai_poll;
	s->cancel = das1800_cancel;

	/* analog out */
	s = &dev->subdevices[1];
	if (thisboard->ao_ability == 1) {
		s->type = COMEDI_SUBD_AO;
		s->subdev_flags = SDF_WRITABLE;
		s->n_chan = thisboard->ao_n_chan;
		s->maxdata = (1 << thisboard->resolution) - 1;
		s->range_table = &range_ao_1;
		s->insn_write = das1800_ao_winsn;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	/* di */
	s = &dev->subdevices[2];
	s->type = COMEDI_SUBD_DI;
	s->subdev_flags = SDF_READABLE;
	s->n_chan = 4;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->insn_bits = das1800_di_rbits;

	/* do */
	s = &dev->subdevices[3];
	s->type = COMEDI_SUBD_DO;
	s->subdev_flags = SDF_WRITABLE | SDF_READABLE;
	s->n_chan = thisboard->do_n_chan;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->insn_bits = das1800_do_wbits;

	das1800_cancel(dev, dev->read_subdev);

	/*  initialize digital out channels */
	outb(devpriv->do_bits, dev->iobase + DAS1800_DIGITAL);

	/*  initialize analog out channels */
	if (thisboard->ao_ability == 1) {
		/*  select 'update' dac channel for baseAddress + 0x0 */
		outb(DAC(thisboard->ao_n_chan - 1),
		     dev->iobase + DAS1800_SELECT);
		outw(devpriv->ao_update_bits, dev->iobase + DAS1800_DAC);
	}

	return 0;
};

static void das1800_detach(struct comedi_device *dev)
{
	struct das1800_private *devpriv = dev->private;

	if (dev->iobase)
		release_region(dev->iobase, DAS1800_SIZE);
	if (dev->irq)
		free_irq(dev->irq, dev);
	if (devpriv) {
		if (devpriv->iobase2)
			release_region(devpriv->iobase2, DAS1800_SIZE);
		if (devpriv->dma0)
			free_dma(devpriv->dma0);
		if (devpriv->dma1)
			free_dma(devpriv->dma1);
		kfree(devpriv->ai_buf0);
		kfree(devpriv->ai_buf1);
	}
};

static struct comedi_driver das1800_driver = {
	.driver_name	= "das1800",
	.module		= THIS_MODULE,
	.attach		= das1800_attach,
	.detach		= das1800_detach,
	.num_names	= ARRAY_SIZE(das1800_boards),
	.board_name	= &das1800_boards[0].name,
	.offset		= sizeof(struct das1800_board),
};
module_comedi_driver(das1800_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
