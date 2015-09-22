/*
 * comedi/drivers/amplc_pci224.c
 * Driver for Amplicon PCI224 and PCI234 AO boards.
 *
 * Copyright (C) 2005 MEV Ltd. <http://www.mev.co.uk/>
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 1998,2000 David A. Schleef <ds@schleef.org>
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
 */

/*
 * Driver: amplc_pci224
 * Description: Amplicon PCI224, PCI234
 * Author: Ian Abbott <abbotti@mev.co.uk>
 * Devices: [Amplicon] PCI224 (amplc_pci224), PCI234
 * Updated: Thu, 31 Jul 2014 11:08:03 +0000
 * Status: works, but see caveats
 *
 * Supports:
 *
 *   - ao_insn read/write
 *   - ao_do_cmd mode with the following sources:
 *
 *     - start_src         TRIG_INT        TRIG_EXT
 *     - scan_begin_src    TRIG_TIMER      TRIG_EXT
 *     - convert_src       TRIG_NOW
 *     - scan_end_src      TRIG_COUNT
 *     - stop_src          TRIG_COUNT      TRIG_EXT        TRIG_NONE
 *
 *     The channel list must contain at least one channel with no repeated
 *     channels.  The scan end count must equal the number of channels in
 *     the channel list.
 *
 *     There is only one external trigger source so only one of start_src,
 *     scan_begin_src or stop_src may use TRIG_EXT.
 *
 * Configuration options:
 *   none
 *
 * Manual configuration of PCI cards is not supported; they are configured
 * automatically.
 *
 * Output range selection - PCI224:
 *
 *   Output ranges on PCI224 are partly software-selectable and partly
 *   hardware-selectable according to jumper LK1.  All channels are set
 *   to the same range:
 *
 *   - LK1 position 1-2 (factory default) corresponds to the following
 *     comedi ranges:
 *
 *       0: [-10V,+10V]; 1: [-5V,+5V]; 2: [-2.5V,+2.5V], 3: [-1.25V,+1.25V],
 *       4: [0,+10V],    5: [0,+5V],   6: [0,+2.5V],     7: [0,+1.25V]
 *
 *   - LK1 position 2-3 corresponds to the following Comedi ranges, using
 *     an external voltage reference:
 *
 *       0: [-Vext,+Vext],
 *       1: [0,+Vext]
 *
 * Output range selection - PCI234:
 *
 *   Output ranges on PCI234 are hardware-selectable according to jumper
 *   LK1 which affects all channels, and jumpers LK2, LK3, LK4 and LK5
 *   which affect channels 0, 1, 2 and 3 individually.  LK1 chooses between
 *   an internal 5V reference and an external voltage reference (Vext).
 *   LK2/3/4/5 choose (per channel) to double the reference or not according
 *   to the following table:
 *
 *     LK1 position   LK2/3/4/5 pos  Comedi range
 *     -------------  -------------  --------------
 *     2-3 (factory)  1-2 (factory)  0: [-10V,+10V]
 *     2-3 (factory)  2-3            1: [-5V,+5V]
 *     1-2            1-2 (factory)  2: [-2*Vext,+2*Vext]
 *     1-2            2-3            3: [-Vext,+Vext]
 *
 * Caveats:
 *
 *   1) All channels on the PCI224 share the same range.  Any change to the
 *      range as a result of insn_write or a streaming command will affect
 *      the output voltages of all channels, including those not specified
 *      by the instruction or command.
 *
 *   2) For the analog output command,  the first scan may be triggered
 *      falsely at the start of acquisition.  This occurs when the DAC scan
 *      trigger source is switched from 'none' to 'timer' (scan_begin_src =
 *      TRIG_TIMER) or 'external' (scan_begin_src == TRIG_EXT) at the start
 *      of acquisition and the trigger source is at logic level 1 at the
 *      time of the switch.  This is very likely for TRIG_TIMER.  For
 *      TRIG_EXT, it depends on the state of the external line and whether
 *      the CR_INVERT flag has been set.  The remaining scans are triggered
 *      correctly.
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>

#include "../comedi_pci.h"

#include "comedi_8254.h"

/*
 * PCI224/234 i/o space 1 (PCIBAR2) registers.
 */
#define PCI224_Z2_BASE	0x14	/* 82C54 counter/timer */
#define PCI224_ZCLK_SCE	0x1A	/* Group Z Clock Configuration Register */
#define PCI224_ZGAT_SCE	0x1D	/* Group Z Gate Configuration Register */
#define PCI224_INT_SCE	0x1E	/* ISR Interrupt source mask register */
				/* /Interrupt status */

/*
 * PCI224/234 i/o space 2 (PCIBAR3) 16-bit registers.
 */
#define PCI224_DACDATA	0x00	/* (w-o) DAC FIFO data. */
#define PCI224_SOFTTRIG	0x00	/* (r-o) DAC software scan trigger. */
#define PCI224_DACCON	0x02	/* (r/w) DAC status/configuration. */
#define PCI224_FIFOSIZ	0x04	/* (w-o) FIFO size for wraparound mode. */
#define PCI224_DACCEN	0x06	/* (w-o) DAC channel enable register. */

/*
 * DACCON values.
 */
/* (r/w) Scan trigger. */
#define PCI224_DACCON_TRIG_MASK		(7 << 0)
#define PCI224_DACCON_TRIG_NONE		(0 << 0)	/* none */
#define PCI224_DACCON_TRIG_SW		(1 << 0)	/* software trig */
#define PCI224_DACCON_TRIG_EXTP		(2 << 0)	/* ext +ve edge */
#define PCI224_DACCON_TRIG_EXTN		(3 << 0)	/* ext -ve edge */
#define PCI224_DACCON_TRIG_Z2CT0	(4 << 0)	/* Z2 CT0 out */
#define PCI224_DACCON_TRIG_Z2CT1	(5 << 0)	/* Z2 CT1 out */
#define PCI224_DACCON_TRIG_Z2CT2	(6 << 0)	/* Z2 CT2 out */
/* (r/w) Polarity (PCI224 only, PCI234 always bipolar!). */
#define PCI224_DACCON_POLAR_MASK	(1 << 3)
#define PCI224_DACCON_POLAR_UNI		(0 << 3)	/* range [0,Vref] */
#define PCI224_DACCON_POLAR_BI		(1 << 3)	/* range [-Vref,Vref] */
/* (r/w) Internal Vref (PCI224 only, when LK1 in position 1-2). */
#define PCI224_DACCON_VREF_MASK		(3 << 4)
#define PCI224_DACCON_VREF_1_25		(0 << 4)	/* Vref = 1.25V */
#define PCI224_DACCON_VREF_2_5		(1 << 4)	/* Vref = 2.5V */
#define PCI224_DACCON_VREF_5		(2 << 4)	/* Vref = 5V */
#define PCI224_DACCON_VREF_10		(3 << 4)	/* Vref = 10V */
/* (r/w) Wraparound mode enable (to play back stored waveform). */
#define PCI224_DACCON_FIFOWRAP		(1 << 7)
/* (r/w) FIFO enable.  It MUST be set! */
#define PCI224_DACCON_FIFOENAB		(1 << 8)
/* (r/w) FIFO interrupt trigger level (most values are not very useful). */
#define PCI224_DACCON_FIFOINTR_MASK	(7 << 9)
#define PCI224_DACCON_FIFOINTR_EMPTY	(0 << 9)	/* when empty */
#define PCI224_DACCON_FIFOINTR_NEMPTY	(1 << 9)	/* when not empty */
#define PCI224_DACCON_FIFOINTR_NHALF	(2 << 9)	/* when not half full */
#define PCI224_DACCON_FIFOINTR_HALF	(3 << 9)	/* when half full */
#define PCI224_DACCON_FIFOINTR_NFULL	(4 << 9)	/* when not full */
#define PCI224_DACCON_FIFOINTR_FULL	(5 << 9)	/* when full */
/* (r-o) FIFO fill level. */
#define PCI224_DACCON_FIFOFL_MASK	(7 << 12)
#define PCI224_DACCON_FIFOFL_EMPTY	(1 << 12)	/* 0 */
#define PCI224_DACCON_FIFOFL_ONETOHALF	(0 << 12)	/* [1,2048] */
#define PCI224_DACCON_FIFOFL_HALFTOFULL	(4 << 12)	/* [2049,4095] */
#define PCI224_DACCON_FIFOFL_FULL	(6 << 12)	/* 4096 */
/* (r-o) DAC busy flag. */
#define PCI224_DACCON_BUSY		(1 << 15)
/* (w-o) FIFO reset. */
#define PCI224_DACCON_FIFORESET		(1 << 12)
/* (w-o) Global reset (not sure what it does). */
#define PCI224_DACCON_GLOBALRESET	(1 << 13)

/*
 * DAC FIFO size.
 */
#define PCI224_FIFO_SIZE	4096

/*
 * DAC FIFO guaranteed minimum room available, depending on reported fill level.
 * The maximum room available depends on the reported fill level and how much
 * has been written!
 */
#define PCI224_FIFO_ROOM_EMPTY		PCI224_FIFO_SIZE
#define PCI224_FIFO_ROOM_ONETOHALF	(PCI224_FIFO_SIZE / 2)
#define PCI224_FIFO_ROOM_HALFTOFULL	1
#define PCI224_FIFO_ROOM_FULL		0

/*
 * Counter/timer clock input configuration sources.
 */
#define CLK_CLK		0	/* reserved (channel-specific clock) */
#define CLK_10MHZ	1	/* internal 10 MHz clock */
#define CLK_1MHZ	2	/* internal 1 MHz clock */
#define CLK_100KHZ	3	/* internal 100 kHz clock */
#define CLK_10KHZ	4	/* internal 10 kHz clock */
#define CLK_1KHZ	5	/* internal 1 kHz clock */
#define CLK_OUTNM1	6	/* output of channel-1 modulo total */
#define CLK_EXT		7	/* external clock */
/* Macro to construct clock input configuration register value. */
#define CLK_CONFIG(chan, src)	((((chan) & 3) << 3) | ((src) & 7))

/*
 * Counter/timer gate input configuration sources.
 */
#define GAT_VCC		0	/* VCC (i.e. enabled) */
#define GAT_GND		1	/* GND (i.e. disabled) */
#define GAT_EXT		2	/* reserved (external gate input) */
#define GAT_NOUTNM2	3	/* inverted output of channel-2 modulo total */
/* Macro to construct gate input configuration register value. */
#define GAT_CONFIG(chan, src)	((((chan) & 3) << 3) | ((src) & 7))

/*
 * Summary of CLK_OUTNM1 and GAT_NOUTNM2 connections for PCI224 and PCI234:
 *
 *              Channel's       Channel's
 *              clock input     gate input
 * Channel      CLK_OUTNM1      GAT_NOUTNM2
 * -------      ----------      -----------
 * Z2-CT0       Z2-CT2-OUT      /Z2-CT1-OUT
 * Z2-CT1       Z2-CT0-OUT      /Z2-CT2-OUT
 * Z2-CT2       Z2-CT1-OUT      /Z2-CT0-OUT
 */

/*
 * Interrupt enable/status bits
 */
#define PCI224_INTR_EXT		0x01	/* rising edge on external input */
#define PCI224_INTR_DAC		0x04	/* DAC (FIFO) interrupt */
#define PCI224_INTR_Z2CT1	0x20	/* rising edge on Z2-CT1 output */

#define PCI224_INTR_EDGE_BITS	(PCI224_INTR_EXT | PCI224_INTR_Z2CT1)
#define PCI224_INTR_LEVEL_BITS	PCI224_INTR_DACFIFO

/*
 * Handy macros.
 */

/* Combine old and new bits. */
#define COMBINE(old, new, mask)	(((old) & ~(mask)) | ((new) & (mask)))

/* Current CPU.  XXX should this be hard_smp_processor_id()? */
#define THISCPU		smp_processor_id()

/* State bits for use with atomic bit operations. */
#define AO_CMD_STARTED	0

/*
 * Range tables.
 */

/*
 * The ranges for PCI224.
 *
 * These are partly hardware-selectable by jumper LK1 and partly
 * software-selectable.
 *
 * All channels share the same hardware range.
 */
static const struct comedi_lrange range_pci224 = {
	10, {
		/* jumper LK1 in position 1-2 (factory default) */
		BIP_RANGE(10),
		BIP_RANGE(5),
		BIP_RANGE(2.5),
		BIP_RANGE(1.25),
		UNI_RANGE(10),
		UNI_RANGE(5),
		UNI_RANGE(2.5),
		UNI_RANGE(1.25),
		/* jumper LK1 in position 2-3 */
		RANGE_ext(-1, 1),	/* bipolar [-Vext,+Vext] */
		RANGE_ext(0, 1),	/* unipolar [0,+Vext] */
	}
};

static const unsigned short hwrange_pci224[10] = {
	/* jumper LK1 in position 1-2 (factory default) */
	PCI224_DACCON_POLAR_BI | PCI224_DACCON_VREF_10,
	PCI224_DACCON_POLAR_BI | PCI224_DACCON_VREF_5,
	PCI224_DACCON_POLAR_BI | PCI224_DACCON_VREF_2_5,
	PCI224_DACCON_POLAR_BI | PCI224_DACCON_VREF_1_25,
	PCI224_DACCON_POLAR_UNI | PCI224_DACCON_VREF_10,
	PCI224_DACCON_POLAR_UNI | PCI224_DACCON_VREF_5,
	PCI224_DACCON_POLAR_UNI | PCI224_DACCON_VREF_2_5,
	PCI224_DACCON_POLAR_UNI | PCI224_DACCON_VREF_1_25,
	/* jumper LK1 in position 2-3 */
	PCI224_DACCON_POLAR_BI,
	PCI224_DACCON_POLAR_UNI,
};

/* Used to check all channels set to the same range on PCI224. */
static const unsigned char range_check_pci224[10] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
};

/*
 * The ranges for PCI234.
 *
 * These are all hardware-selectable by jumper LK1 affecting all channels,
 * and jumpers LK2, LK3, LK4 and LK5 affecting channels 0, 1, 2 and 3
 * individually.
 */
static const struct comedi_lrange range_pci234 = {
	4, {
		/* LK1: 1-2 (fact def), LK2/3/4/5: 2-3 (fac def) */
		BIP_RANGE(10),
		/* LK1: 1-2 (fact def), LK2/3/4/5: 1-2 */
		BIP_RANGE(5),
		/* LK1: 2-3, LK2/3/4/5: 2-3 (fac def) */
		RANGE_ext(-2, 2),	/* bipolar [-2*Vext,+2*Vext] */
		/* LK1: 2-3, LK2/3/4/5: 1-2 */
		RANGE_ext(-1, 1),	/* bipolar [-Vext,+Vext] */
	}
};

/* N.B. PCI234 ignores the polarity bit, but software uses it. */
static const unsigned short hwrange_pci234[4] = {
	PCI224_DACCON_POLAR_BI,
	PCI224_DACCON_POLAR_BI,
	PCI224_DACCON_POLAR_BI,
	PCI224_DACCON_POLAR_BI,
};

/* Used to check all channels use same LK1 setting on PCI234. */
static const unsigned char range_check_pci234[4] = {
	0, 0, 1, 1,
};

/*
 * Board descriptions.
 */

enum pci224_model { pci224_model, pci234_model };

struct pci224_board {
	const char *name;
	unsigned int ao_chans;
	unsigned int ao_bits;
	const struct comedi_lrange *ao_range;
	const unsigned short *ao_hwrange;
	const unsigned char *ao_range_check;
};

static const struct pci224_board pci224_boards[] = {
	[pci224_model] = {
		.name		= "pci224",
		.ao_chans	= 16,
		.ao_bits	= 12,
		.ao_range	= &range_pci224,
		.ao_hwrange	= &hwrange_pci224[0],
		.ao_range_check	= &range_check_pci224[0],
	},
	[pci234_model] = {
		.name		= "pci234",
		.ao_chans	= 4,
		.ao_bits	= 16,
		.ao_range	= &range_pci234,
		.ao_hwrange	= &hwrange_pci234[0],
		.ao_range_check	= &range_check_pci234[0],
	},
};

struct pci224_private {
	unsigned long iobase1;
	unsigned long state;
	spinlock_t ao_spinlock;	/* spinlock for AO command handling */
	unsigned short *ao_scan_vals;
	unsigned char *ao_scan_order;
	int intr_cpuid;
	short intr_running;
	unsigned short daccon;
	unsigned short ao_enab;	/* max 16 channels so 'short' will do */
	unsigned char intsce;
};

/*
 * Called from the 'insn_write' function to perform a single write.
 */
static void
pci224_ao_set_data(struct comedi_device *dev, int chan, int range,
		   unsigned int data)
{
	const struct pci224_board *board = dev->board_ptr;
	struct pci224_private *devpriv = dev->private;
	unsigned short mangled;

	/* Enable the channel. */
	outw(1 << chan, dev->iobase + PCI224_DACCEN);
	/* Set range and reset FIFO. */
	devpriv->daccon = COMBINE(devpriv->daccon, board->ao_hwrange[range],
				  PCI224_DACCON_POLAR_MASK |
				  PCI224_DACCON_VREF_MASK);
	outw(devpriv->daccon | PCI224_DACCON_FIFORESET,
	     dev->iobase + PCI224_DACCON);
	/*
	 * Mangle the data.  The hardware expects:
	 * - bipolar: 16-bit 2's complement
	 * - unipolar: 16-bit unsigned
	 */
	mangled = (unsigned short)data << (16 - board->ao_bits);
	if ((devpriv->daccon & PCI224_DACCON_POLAR_MASK) ==
	    PCI224_DACCON_POLAR_BI) {
		mangled ^= 0x8000;
	}
	/* Write mangled data to the FIFO. */
	outw(mangled, dev->iobase + PCI224_DACDATA);
	/* Trigger the conversion. */
	inw(dev->iobase + PCI224_SOFTTRIG);
}

static int pci224_ao_insn_write(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int range = CR_RANGE(insn->chanspec);
	unsigned int val = s->readback[chan];
	int i;

	for (i = 0; i < insn->n; i++) {
		val = data[i];
		pci224_ao_set_data(dev, chan, range, val);
	}
	s->readback[chan] = val;

	return insn->n;
}

/*
 * Kills a command running on the AO subdevice.
 */
static void pci224_ao_stop(struct comedi_device *dev,
			   struct comedi_subdevice *s)
{
	struct pci224_private *devpriv = dev->private;
	unsigned long flags;

	if (!test_and_clear_bit(AO_CMD_STARTED, &devpriv->state))
		return;

	spin_lock_irqsave(&devpriv->ao_spinlock, flags);
	/* Kill the interrupts. */
	devpriv->intsce = 0;
	outb(0, devpriv->iobase1 + PCI224_INT_SCE);
	/*
	 * Interrupt routine may or may not be running.  We may or may not
	 * have been called from the interrupt routine (directly or
	 * indirectly via a comedi_events() callback routine).  It's highly
	 * unlikely that we've been called from some other interrupt routine
	 * but who knows what strange things coders get up to!
	 *
	 * If the interrupt routine is currently running, wait for it to
	 * finish, unless we appear to have been called via the interrupt
	 * routine.
	 */
	while (devpriv->intr_running && devpriv->intr_cpuid != THISCPU) {
		spin_unlock_irqrestore(&devpriv->ao_spinlock, flags);
		spin_lock_irqsave(&devpriv->ao_spinlock, flags);
	}
	spin_unlock_irqrestore(&devpriv->ao_spinlock, flags);
	/* Reconfigure DAC for insn_write usage. */
	outw(0, dev->iobase + PCI224_DACCEN);	/* Disable channels. */
	devpriv->daccon =
	     COMBINE(devpriv->daccon,
		     PCI224_DACCON_TRIG_SW | PCI224_DACCON_FIFOINTR_EMPTY,
		     PCI224_DACCON_TRIG_MASK | PCI224_DACCON_FIFOINTR_MASK);
	outw(devpriv->daccon | PCI224_DACCON_FIFORESET,
	     dev->iobase + PCI224_DACCON);
}

/*
 * Handles start of acquisition for the AO subdevice.
 */
static void pci224_ao_start(struct comedi_device *dev,
			    struct comedi_subdevice *s)
{
	struct pci224_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned long flags;

	set_bit(AO_CMD_STARTED, &devpriv->state);

	/* Enable interrupts. */
	spin_lock_irqsave(&devpriv->ao_spinlock, flags);
	if (cmd->stop_src == TRIG_EXT)
		devpriv->intsce = PCI224_INTR_EXT | PCI224_INTR_DAC;
	else
		devpriv->intsce = PCI224_INTR_DAC;

	outb(devpriv->intsce, devpriv->iobase1 + PCI224_INT_SCE);
	spin_unlock_irqrestore(&devpriv->ao_spinlock, flags);
}

/*
 * Handles interrupts from the DAC FIFO.
 */
static void pci224_ao_handle_fifo(struct comedi_device *dev,
				  struct comedi_subdevice *s)
{
	struct pci224_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned int num_scans = comedi_nscans_left(s, 0);
	unsigned int room;
	unsigned short dacstat;
	unsigned int i, n;

	/* Determine how much room is in the FIFO (in samples). */
	dacstat = inw(dev->iobase + PCI224_DACCON);
	switch (dacstat & PCI224_DACCON_FIFOFL_MASK) {
	case PCI224_DACCON_FIFOFL_EMPTY:
		room = PCI224_FIFO_ROOM_EMPTY;
		if (cmd->stop_src == TRIG_COUNT &&
		    s->async->scans_done >= cmd->stop_arg) {
			/* FIFO empty at end of counted acquisition. */
			s->async->events |= COMEDI_CB_EOA;
			comedi_handle_events(dev, s);
			return;
		}
		break;
	case PCI224_DACCON_FIFOFL_ONETOHALF:
		room = PCI224_FIFO_ROOM_ONETOHALF;
		break;
	case PCI224_DACCON_FIFOFL_HALFTOFULL:
		room = PCI224_FIFO_ROOM_HALFTOFULL;
		break;
	default:
		room = PCI224_FIFO_ROOM_FULL;
		break;
	}
	if (room >= PCI224_FIFO_ROOM_ONETOHALF) {
		/* FIFO is less than half-full. */
		if (num_scans == 0) {
			/* Nothing left to put in the FIFO. */
			dev_err(dev->class_dev, "AO buffer underrun\n");
			s->async->events |= COMEDI_CB_OVERFLOW;
		}
	}
	/* Determine how many new scans can be put in the FIFO. */
	room /= cmd->chanlist_len;

	/* Determine how many scans to process. */
	if (num_scans > room)
		num_scans = room;

	/* Process scans. */
	for (n = 0; n < num_scans; n++) {
		comedi_buf_read_samples(s, &devpriv->ao_scan_vals[0],
					cmd->chanlist_len);
		for (i = 0; i < cmd->chanlist_len; i++) {
			outw(devpriv->ao_scan_vals[devpriv->ao_scan_order[i]],
			     dev->iobase + PCI224_DACDATA);
		}
	}
	if (cmd->stop_src == TRIG_COUNT &&
	    s->async->scans_done >= cmd->stop_arg) {
		/*
		 * Change FIFO interrupt trigger level to wait
		 * until FIFO is empty.
		 */
		devpriv->daccon = COMBINE(devpriv->daccon,
					  PCI224_DACCON_FIFOINTR_EMPTY,
					  PCI224_DACCON_FIFOINTR_MASK);
		outw(devpriv->daccon, dev->iobase + PCI224_DACCON);
	}
	if ((devpriv->daccon & PCI224_DACCON_TRIG_MASK) ==
	    PCI224_DACCON_TRIG_NONE) {
		unsigned short trig;

		/*
		 * This is the initial DAC FIFO interrupt at the
		 * start of the acquisition.  The DAC's scan trigger
		 * has been set to 'none' up until now.
		 *
		 * Now that data has been written to the FIFO, the
		 * DAC's scan trigger source can be set to the
		 * correct value.
		 *
		 * BUG: The first scan will be triggered immediately
		 * if the scan trigger source is at logic level 1.
		 */
		if (cmd->scan_begin_src == TRIG_TIMER) {
			trig = PCI224_DACCON_TRIG_Z2CT0;
		} else {
			/* cmd->scan_begin_src == TRIG_EXT */
			if (cmd->scan_begin_arg & CR_INVERT)
				trig = PCI224_DACCON_TRIG_EXTN;
			else
				trig = PCI224_DACCON_TRIG_EXTP;
		}
		devpriv->daccon =
		    COMBINE(devpriv->daccon, trig, PCI224_DACCON_TRIG_MASK);
		outw(devpriv->daccon, dev->iobase + PCI224_DACCON);
	}

	comedi_handle_events(dev, s);
}

static int pci224_ao_inttrig_start(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   unsigned int trig_num)
{
	struct comedi_cmd *cmd = &s->async->cmd;

	if (trig_num != cmd->start_arg)
		return -EINVAL;

	s->async->inttrig = NULL;
	pci224_ao_start(dev, s);

	return 1;
}

static int pci224_ao_check_chanlist(struct comedi_device *dev,
				    struct comedi_subdevice *s,
				    struct comedi_cmd *cmd)
{
	const struct pci224_board *board = dev->board_ptr;
	unsigned int range_check_0;
	unsigned int chan_mask = 0;
	int i;

	range_check_0 = board->ao_range_check[CR_RANGE(cmd->chanlist[0])];
	for (i = 0; i < cmd->chanlist_len; i++) {
		unsigned int chan = CR_CHAN(cmd->chanlist[i]);

		if (chan_mask & (1 << chan)) {
			dev_dbg(dev->class_dev,
				"%s: entries in chanlist must contain no duplicate channels\n",
				__func__);
			return -EINVAL;
		}
		chan_mask |= 1 << chan;

		if (board->ao_range_check[CR_RANGE(cmd->chanlist[i])] !=
		    range_check_0) {
			dev_dbg(dev->class_dev,
				"%s: entries in chanlist have incompatible ranges\n",
				__func__);
			return -EINVAL;
		}
	}

	return 0;
}

#define MAX_SCAN_PERIOD		0xFFFFFFFFU
#define MIN_SCAN_PERIOD		2500
#define CONVERT_PERIOD		625

/*
 * 'do_cmdtest' function for AO subdevice.
 */
static int
pci224_ao_cmdtest(struct comedi_device *dev, struct comedi_subdevice *s,
		  struct comedi_cmd *cmd)
{
	int err = 0;
	unsigned int arg;

	/* Step 1 : check if triggers are trivially valid */

	err |= comedi_check_trigger_src(&cmd->start_src, TRIG_INT | TRIG_EXT);
	err |= comedi_check_trigger_src(&cmd->scan_begin_src,
					TRIG_EXT | TRIG_TIMER);
	err |= comedi_check_trigger_src(&cmd->convert_src, TRIG_NOW);
	err |= comedi_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= comedi_check_trigger_src(&cmd->stop_src,
					TRIG_COUNT | TRIG_EXT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= comedi_check_trigger_is_unique(cmd->start_src);
	err |= comedi_check_trigger_is_unique(cmd->scan_begin_src);
	err |= comedi_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	/*
	 * There's only one external trigger signal (which makes these
	 * tests easier).  Only one thing can use it.
	 */
	arg = 0;
	if (cmd->start_src & TRIG_EXT)
		arg++;
	if (cmd->scan_begin_src & TRIG_EXT)
		arg++;
	if (cmd->stop_src & TRIG_EXT)
		arg++;
	if (arg > 1)
		err |= -EINVAL;

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	switch (cmd->start_src) {
	case TRIG_INT:
		err |= comedi_check_trigger_arg_is(&cmd->start_arg, 0);
		break;
	case TRIG_EXT:
		/* Force to external trigger 0. */
		if (cmd->start_arg & ~CR_FLAGS_MASK) {
			cmd->start_arg =
			    COMBINE(cmd->start_arg, 0, ~CR_FLAGS_MASK);
			err |= -EINVAL;
		}
		/* The only flag allowed is CR_EDGE, which is ignored. */
		if (cmd->start_arg & CR_FLAGS_MASK & ~CR_EDGE) {
			cmd->start_arg = COMBINE(cmd->start_arg, 0,
						 CR_FLAGS_MASK & ~CR_EDGE);
			err |= -EINVAL;
		}
		break;
	}

	switch (cmd->scan_begin_src) {
	case TRIG_TIMER:
		err |= comedi_check_trigger_arg_max(&cmd->scan_begin_arg,
						    MAX_SCAN_PERIOD);

		arg = cmd->chanlist_len * CONVERT_PERIOD;
		if (arg < MIN_SCAN_PERIOD)
			arg = MIN_SCAN_PERIOD;
		err |= comedi_check_trigger_arg_min(&cmd->scan_begin_arg, arg);
		break;
	case TRIG_EXT:
		/* Force to external trigger 0. */
		if (cmd->scan_begin_arg & ~CR_FLAGS_MASK) {
			cmd->scan_begin_arg =
			    COMBINE(cmd->scan_begin_arg, 0, ~CR_FLAGS_MASK);
			err |= -EINVAL;
		}
		/* Only allow flags CR_EDGE and CR_INVERT.  Ignore CR_EDGE. */
		if (cmd->scan_begin_arg & CR_FLAGS_MASK &
		    ~(CR_EDGE | CR_INVERT)) {
			cmd->scan_begin_arg =
			    COMBINE(cmd->scan_begin_arg, 0,
				    CR_FLAGS_MASK & ~(CR_EDGE | CR_INVERT));
			err |= -EINVAL;
		}
		break;
	}

	err |= comedi_check_trigger_arg_is(&cmd->convert_arg, 0);
	err |= comedi_check_trigger_arg_is(&cmd->scan_end_arg,
					   cmd->chanlist_len);

	switch (cmd->stop_src) {
	case TRIG_COUNT:
		err |= comedi_check_trigger_arg_min(&cmd->stop_arg, 1);
		break;
	case TRIG_EXT:
		/* Force to external trigger 0. */
		if (cmd->stop_arg & ~CR_FLAGS_MASK) {
			cmd->stop_arg =
			    COMBINE(cmd->stop_arg, 0, ~CR_FLAGS_MASK);
			err |= -EINVAL;
		}
		/* The only flag allowed is CR_EDGE, which is ignored. */
		if (cmd->stop_arg & CR_FLAGS_MASK & ~CR_EDGE) {
			cmd->stop_arg =
			    COMBINE(cmd->stop_arg, 0, CR_FLAGS_MASK & ~CR_EDGE);
		}
		break;
	case TRIG_NONE:
		err |= comedi_check_trigger_arg_is(&cmd->stop_arg, 0);
		break;
	}

	if (err)
		return 3;

	/* Step 4: fix up any arguments. */

	if (cmd->scan_begin_src == TRIG_TIMER) {
		arg = cmd->scan_begin_arg;
		/* Use two timers. */
		comedi_8254_cascade_ns_to_timer(dev->pacer, &arg, cmd->flags);
		err |= comedi_check_trigger_arg_is(&cmd->scan_begin_arg, arg);
	}

	if (err)
		return 4;

	/* Step 5: check channel list if it exists */
	if (cmd->chanlist && cmd->chanlist_len > 0)
		err |= pci224_ao_check_chanlist(dev, s, cmd);

	if (err)
		return 5;

	return 0;
}

static void pci224_ao_start_pacer(struct comedi_device *dev,
				  struct comedi_subdevice *s)
{
	struct pci224_private *devpriv = dev->private;

	/*
	 * The output of timer Z2-0 will be used as the scan trigger
	 * source.
	 */
	/* Make sure Z2-0 is gated on.  */
	outb(GAT_CONFIG(0, GAT_VCC), devpriv->iobase1 + PCI224_ZGAT_SCE);
	/* Cascading with Z2-2. */
	/* Make sure Z2-2 is gated on.  */
	outb(GAT_CONFIG(2, GAT_VCC), devpriv->iobase1 + PCI224_ZGAT_SCE);
	/* Z2-2 needs 10 MHz clock. */
	outb(CLK_CONFIG(2, CLK_10MHZ), devpriv->iobase1 + PCI224_ZCLK_SCE);
	/* Z2-0 is clocked from Z2-2's output. */
	outb(CLK_CONFIG(0, CLK_OUTNM1), devpriv->iobase1 + PCI224_ZCLK_SCE);

	comedi_8254_pacer_enable(dev->pacer, 2, 0, false);
}

static int pci224_ao_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	const struct pci224_board *board = dev->board_ptr;
	struct pci224_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	int range;
	unsigned int i, j;
	unsigned int ch;
	unsigned int rank;
	unsigned long flags;

	/* Cannot handle null/empty chanlist. */
	if (!cmd->chanlist || cmd->chanlist_len == 0)
		return -EINVAL;

	/* Determine which channels are enabled and their load order.  */
	devpriv->ao_enab = 0;

	for (i = 0; i < cmd->chanlist_len; i++) {
		ch = CR_CHAN(cmd->chanlist[i]);
		devpriv->ao_enab |= 1U << ch;
		rank = 0;
		for (j = 0; j < cmd->chanlist_len; j++) {
			if (CR_CHAN(cmd->chanlist[j]) < ch)
				rank++;
		}
		devpriv->ao_scan_order[rank] = i;
	}

	/* Set enabled channels. */
	outw(devpriv->ao_enab, dev->iobase + PCI224_DACCEN);

	/* Determine range and polarity.  All channels the same.  */
	range = CR_RANGE(cmd->chanlist[0]);

	/*
	 * Set DAC range and polarity.
	 * Set DAC scan trigger source to 'none'.
	 * Set DAC FIFO interrupt trigger level to 'not half full'.
	 * Reset DAC FIFO.
	 *
	 * N.B. DAC FIFO interrupts are currently disabled.
	 */
	devpriv->daccon =
	    COMBINE(devpriv->daccon,
		    board->ao_hwrange[range] | PCI224_DACCON_TRIG_NONE |
		    PCI224_DACCON_FIFOINTR_NHALF,
		    PCI224_DACCON_POLAR_MASK | PCI224_DACCON_VREF_MASK |
		    PCI224_DACCON_TRIG_MASK | PCI224_DACCON_FIFOINTR_MASK);
	outw(devpriv->daccon | PCI224_DACCON_FIFORESET,
	     dev->iobase + PCI224_DACCON);

	if (cmd->scan_begin_src == TRIG_TIMER) {
		comedi_8254_update_divisors(dev->pacer);
		pci224_ao_start_pacer(dev, s);
	}

	spin_lock_irqsave(&devpriv->ao_spinlock, flags);
	if (cmd->start_src == TRIG_INT) {
		s->async->inttrig = pci224_ao_inttrig_start;
	} else {	/* TRIG_EXT */
		/* Enable external interrupt trigger to start acquisition. */
		devpriv->intsce |= PCI224_INTR_EXT;
		outb(devpriv->intsce, devpriv->iobase1 + PCI224_INT_SCE);
	}
	spin_unlock_irqrestore(&devpriv->ao_spinlock, flags);

	return 0;
}

/*
 * 'cancel' function for AO subdevice.
 */
static int pci224_ao_cancel(struct comedi_device *dev,
			    struct comedi_subdevice *s)
{
	pci224_ao_stop(dev, s);
	return 0;
}

/*
 * 'munge' data for AO command.
 */
static void
pci224_ao_munge(struct comedi_device *dev, struct comedi_subdevice *s,
		void *data, unsigned int num_bytes, unsigned int chan_index)
{
	const struct pci224_board *board = dev->board_ptr;
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned short *array = data;
	unsigned int length = num_bytes / sizeof(*array);
	unsigned int offset;
	unsigned int shift;
	unsigned int i;

	/* The hardware expects 16-bit numbers. */
	shift = 16 - board->ao_bits;
	/* Channels will be all bipolar or all unipolar. */
	if ((board->ao_hwrange[CR_RANGE(cmd->chanlist[0])] &
	     PCI224_DACCON_POLAR_MASK) == PCI224_DACCON_POLAR_UNI) {
		/* Unipolar */
		offset = 0;
	} else {
		/* Bipolar */
		offset = 32768;
	}
	/* Munge the data. */
	for (i = 0; i < length; i++)
		array[i] = (array[i] << shift) - offset;
}

/*
 * Interrupt handler.
 */
static irqreturn_t pci224_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct pci224_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->write_subdev;
	struct comedi_cmd *cmd;
	unsigned char intstat, valid_intstat;
	unsigned char curenab;
	int retval = 0;
	unsigned long flags;

	intstat = inb(devpriv->iobase1 + PCI224_INT_SCE) & 0x3F;
	if (intstat) {
		retval = 1;
		spin_lock_irqsave(&devpriv->ao_spinlock, flags);
		valid_intstat = devpriv->intsce & intstat;
		/* Temporarily disable interrupt sources. */
		curenab = devpriv->intsce & ~intstat;
		outb(curenab, devpriv->iobase1 + PCI224_INT_SCE);
		devpriv->intr_running = 1;
		devpriv->intr_cpuid = THISCPU;
		spin_unlock_irqrestore(&devpriv->ao_spinlock, flags);
		if (valid_intstat) {
			cmd = &s->async->cmd;
			if (valid_intstat & PCI224_INTR_EXT) {
				devpriv->intsce &= ~PCI224_INTR_EXT;
				if (cmd->start_src == TRIG_EXT)
					pci224_ao_start(dev, s);
				else if (cmd->stop_src == TRIG_EXT)
					pci224_ao_stop(dev, s);
			}
			if (valid_intstat & PCI224_INTR_DAC)
				pci224_ao_handle_fifo(dev, s);
		}
		/* Reenable interrupt sources. */
		spin_lock_irqsave(&devpriv->ao_spinlock, flags);
		if (curenab != devpriv->intsce) {
			outb(devpriv->intsce,
			     devpriv->iobase1 + PCI224_INT_SCE);
		}
		devpriv->intr_running = 0;
		spin_unlock_irqrestore(&devpriv->ao_spinlock, flags);
	}
	return IRQ_RETVAL(retval);
}

static int
pci224_auto_attach(struct comedi_device *dev, unsigned long context_model)
{
	struct pci_dev *pci_dev = comedi_to_pci_dev(dev);
	const struct pci224_board *board = NULL;
	struct pci224_private *devpriv;
	struct comedi_subdevice *s;
	unsigned int irq;
	int ret;

	if (context_model < ARRAY_SIZE(pci224_boards))
		board = &pci224_boards[context_model];
	if (!board || !board->name) {
		dev_err(dev->class_dev,
			"amplc_pci224: BUG! cannot determine board type!\n");
		return -EINVAL;
	}
	dev->board_ptr = board;
	dev->board_name = board->name;

	dev_info(dev->class_dev, "amplc_pci224: attach pci %s - %s\n",
		 pci_name(pci_dev), dev->board_name);

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;

	spin_lock_init(&devpriv->ao_spinlock);

	devpriv->iobase1 = pci_resource_start(pci_dev, 2);
	dev->iobase = pci_resource_start(pci_dev, 3);
	irq = pci_dev->irq;

	/* Allocate buffer to hold values for AO channel scan. */
	devpriv->ao_scan_vals = kmalloc(sizeof(devpriv->ao_scan_vals[0]) *
					board->ao_chans, GFP_KERNEL);
	if (!devpriv->ao_scan_vals)
		return -ENOMEM;

	/* Allocate buffer to hold AO channel scan order. */
	devpriv->ao_scan_order = kmalloc(sizeof(devpriv->ao_scan_order[0]) *
					 board->ao_chans, GFP_KERNEL);
	if (!devpriv->ao_scan_order)
		return -ENOMEM;

	/* Disable interrupt sources. */
	devpriv->intsce = 0;
	outb(0, devpriv->iobase1 + PCI224_INT_SCE);

	/* Initialize the DAC hardware. */
	outw(PCI224_DACCON_GLOBALRESET, dev->iobase + PCI224_DACCON);
	outw(0, dev->iobase + PCI224_DACCEN);
	outw(0, dev->iobase + PCI224_FIFOSIZ);
	devpriv->daccon = PCI224_DACCON_TRIG_SW | PCI224_DACCON_POLAR_BI |
			  PCI224_DACCON_FIFOENAB | PCI224_DACCON_FIFOINTR_EMPTY;
	outw(devpriv->daccon | PCI224_DACCON_FIFORESET,
	     dev->iobase + PCI224_DACCON);

	dev->pacer = comedi_8254_init(devpriv->iobase1 + PCI224_Z2_BASE,
				      I8254_OSC_BASE_10MHZ, I8254_IO8, 0);
	if (!dev->pacer)
		return -ENOMEM;

	ret = comedi_alloc_subdevices(dev, 1);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	/* Analog output subdevice. */
	s->type = COMEDI_SUBD_AO;
	s->subdev_flags = SDF_WRITABLE | SDF_GROUND | SDF_CMD_WRITE;
	s->n_chan = board->ao_chans;
	s->maxdata = (1 << board->ao_bits) - 1;
	s->range_table = board->ao_range;
	s->insn_write = pci224_ao_insn_write;
	s->len_chanlist = s->n_chan;
	dev->write_subdev = s;
	s->do_cmd = pci224_ao_cmd;
	s->do_cmdtest = pci224_ao_cmdtest;
	s->cancel = pci224_ao_cancel;
	s->munge = pci224_ao_munge;

	ret = comedi_alloc_subdev_readback(s);
	if (ret)
		return ret;

	if (irq) {
		ret = request_irq(irq, pci224_interrupt, IRQF_SHARED,
				  dev->board_name, dev);
		if (ret < 0) {
			dev_err(dev->class_dev,
				"error! unable to allocate irq %u\n", irq);
			return ret;
		}
		dev->irq = irq;
	}

	return 0;
}

static void pci224_detach(struct comedi_device *dev)
{
	struct pci224_private *devpriv = dev->private;

	comedi_pci_detach(dev);
	if (devpriv) {
		kfree(devpriv->ao_scan_vals);
		kfree(devpriv->ao_scan_order);
	}
}

static struct comedi_driver amplc_pci224_driver = {
	.driver_name	= "amplc_pci224",
	.module		= THIS_MODULE,
	.detach		= pci224_detach,
	.auto_attach	= pci224_auto_attach,
	.board_name	= &pci224_boards[0].name,
	.offset		= sizeof(struct pci224_board),
	.num_names	= ARRAY_SIZE(pci224_boards),
};

static int amplc_pci224_pci_probe(struct pci_dev *dev,
				  const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &amplc_pci224_driver,
				      id->driver_data);
}

static const struct pci_device_id amplc_pci224_pci_table[] = {
	{ PCI_VDEVICE(AMPLICON, 0x0007), pci224_model },
	{ PCI_VDEVICE(AMPLICON, 0x0008), pci234_model },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, amplc_pci224_pci_table);

static struct pci_driver amplc_pci224_pci_driver = {
	.name		= "amplc_pci224",
	.id_table	= amplc_pci224_pci_table,
	.probe		= amplc_pci224_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(amplc_pci224_driver, amplc_pci224_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi driver for Amplicon PCI224 and PCI234 AO boards");
MODULE_LICENSE("GPL");
