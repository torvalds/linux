/*

comedi/drivers/adl_pci9111.c

Hardware driver for PCI9111 ADLink cards:

PCI-9111HR

Copyright (C) 2002-2005 Emmanuel Pacaud <emmanuel.pacaud@univ-poitiers.fr>

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
Driver: adl_pci9111
Description: Adlink PCI-9111HR
Author: Emmanuel Pacaud <emmanuel.pacaud@univ-poitiers.fr>
Devices: [ADLink] PCI-9111HR (adl_pci9111)
Status: experimental

Supports:

	- ai_insn read
	- ao_insn read/write
	- di_insn read
	- do_insn read/write
	- ai_do_cmd mode with the following sources:

	- start_src		TRIG_NOW
	- scan_begin_src	TRIG_FOLLOW	TRIG_TIMER	TRIG_EXT
	- convert_src				TRIG_TIMER	TRIG_EXT
	- scan_end_src		TRIG_COUNT
	- stop_src		TRIG_COUNT	TRIG_NONE

The scanned channels must be consecutive and start from 0. They must
all have the same range and aref.

Configuration options: not applicable, uses PCI auto config
*/

/*
CHANGELOG:

2005/02/17 Extend AI streaming capabilities. Now, scan_begin_arg can be
a multiple of chanlist_len*convert_arg.
2002/02/19 Fixed the two's complement conversion in pci9111_(hr_)ai_get_data.
2002/02/18 Added external trigger support for analog input.

TODO:

	- Really test implemented functionality.
	- Add support for the PCI-9111DG with a probe routine to identify
	  the card type (perhaps with the help of the channel number readback
	  of the A/D Data register).
	- Add external multiplexer support.

*/

#include "../comedidev.h"

#include <linux/delay.h>
#include <linux/interrupt.h>

#include "8253.h"
#include "comedi_fc.h"

#define PCI9111_DRIVER_NAME	"adl_pci9111"
#define PCI9111_HR_DEVICE_ID	0x9111

#define PCI9111_FIFO_HALF_SIZE	512

#define PCI9111_AI_ACQUISITION_PERIOD_MIN_NS	10000

#define PCI9111_RANGE_SETTING_DELAY		10
#define PCI9111_AI_INSTANT_READ_UDELAY_US	2
#define PCI9111_AI_INSTANT_READ_TIMEOUT		100

#define PCI9111_8254_CLOCK_PERIOD_NS		500

/*
 * IO address map and bit defines
 */
#define PCI9111_AI_FIFO_REG		0x00
#define PCI9111_AO_REG			0x00
#define PCI9111_DIO_REG			0x02
#define PCI9111_EDIO_REG		0x04
#define PCI9111_AI_CHANNEL_REG		0x06
#define PCI9111_AI_RANGE_STAT_REG	0x08
#define PCI9111_AI_STAT_AD_BUSY		(1 << 7)
#define PCI9111_AI_STAT_FF_FF		(1 << 6)
#define PCI9111_AI_STAT_FF_HF		(1 << 5)
#define PCI9111_AI_STAT_FF_EF		(1 << 4)
#define PCI9111_AI_RANGE_MASK		(7 << 0)
#define PCI9111_AI_TRIG_CTRL_REG	0x0a
#define PCI9111_AI_TRIG_CTRL_TRGEVENT	(1 << 5)
#define PCI9111_AI_TRIG_CTRL_POTRG	(1 << 4)
#define PCI9111_AI_TRIG_CTRL_PTRG	(1 << 3)
#define PCI9111_AI_TRIG_CTRL_ETIS	(1 << 2)
#define PCI9111_AI_TRIG_CTRL_TPST	(1 << 1)
#define PCI9111_AI_TRIG_CTRL_ASCAN	(1 << 0)
#define PCI9111_INT_CTRL_REG		0x0c
#define PCI9111_INT_CTRL_ISC2		(1 << 3)
#define PCI9111_INT_CTRL_FFEN		(1 << 2)
#define PCI9111_INT_CTRL_ISC1		(1 << 1)
#define PCI9111_INT_CTRL_ISC0		(1 << 0)
#define PCI9111_SOFT_TRIG_REG		0x0e
#define PCI9111_8254_BASE_REG		0x40
#define PCI9111_INT_CLR_REG		0x48

static const struct comedi_lrange pci9111_ai_range = {
	5,
	{
		BIP_RANGE(10),
		BIP_RANGE(5),
		BIP_RANGE(2.5),
		BIP_RANGE(1.25),
		BIP_RANGE(0.625)
	}
};

struct pci9111_private_data {
	unsigned long lcr_io_base;

	int stop_counter;
	int stop_is_none;

	unsigned int scan_delay;
	unsigned int chanlist_len;
	unsigned int chunk_counter;
	unsigned int chunk_num_samples;

	int ao_readback;

	unsigned int div1;
	unsigned int div2;

	short ai_bounce_buffer[2 * PCI9111_FIFO_HALF_SIZE];
};

#define PLX9050_REGISTER_INTERRUPT_CONTROL 0x4c

#define PLX9050_LINTI1_ENABLE		(1 << 0)
#define PLX9050_LINTI1_ACTIVE_HIGH	(1 << 1)
#define PLX9050_LINTI1_STATUS		(1 << 2)
#define PLX9050_LINTI2_ENABLE		(1 << 3)
#define PLX9050_LINTI2_ACTIVE_HIGH	(1 << 4)
#define PLX9050_LINTI2_STATUS		(1 << 5)
#define PLX9050_PCI_INTERRUPT_ENABLE	(1 << 6)
#define PLX9050_SOFTWARE_INTERRUPT	(1 << 7)

static void plx9050_interrupt_control(unsigned long io_base,
				      bool LINTi1_enable,
				      bool LINTi1_active_high,
				      bool LINTi2_enable,
				      bool LINTi2_active_high,
				      bool interrupt_enable)
{
	int flags = 0;

	if (LINTi1_enable)
		flags |= PLX9050_LINTI1_ENABLE;
	if (LINTi1_active_high)
		flags |= PLX9050_LINTI1_ACTIVE_HIGH;
	if (LINTi2_enable)
		flags |= PLX9050_LINTI2_ENABLE;
	if (LINTi2_active_high)
		flags |= PLX9050_LINTI2_ACTIVE_HIGH;

	if (interrupt_enable)
		flags |= PLX9050_PCI_INTERRUPT_ENABLE;

	outb(flags, io_base + PLX9050_REGISTER_INTERRUPT_CONTROL);
}

static void pci9111_timer_set(struct comedi_device *dev)
{
	struct pci9111_private_data *dev_private = dev->private;
	unsigned long timer_base = dev->iobase + PCI9111_8254_BASE_REG;

	i8254_set_mode(timer_base, 1, 0, I8254_MODE0 | I8254_BINARY);
	i8254_set_mode(timer_base, 1, 1, I8254_MODE2 | I8254_BINARY);
	i8254_set_mode(timer_base, 1, 2, I8254_MODE2 | I8254_BINARY);

	udelay(1);

	i8254_write(timer_base, 1, 2, dev_private->div2);
	i8254_write(timer_base, 1, 1, dev_private->div1);
}

enum pci9111_trigger_sources {
	software,
	timer_pacer,
	external
};

static void pci9111_trigger_source_set(struct comedi_device *dev,
				       enum pci9111_trigger_sources source)
{
	int flags;

	/* Read the current trigger mode control bits */
	flags = inb(dev->iobase + PCI9111_AI_TRIG_CTRL_REG);
	/* Mask off the EITS and TPST bits */
	flags &= 0x9;

	switch (source) {
	case software:
		break;

	case timer_pacer:
		flags |= PCI9111_AI_TRIG_CTRL_TPST;
		break;

	case external:
		flags |= PCI9111_AI_TRIG_CTRL_ETIS;
		break;
	}

	outb(flags, dev->iobase + PCI9111_AI_TRIG_CTRL_REG);
}

static void pci9111_pretrigger_set(struct comedi_device *dev, bool pretrigger)
{
	int flags;

	/* Read the current trigger mode control bits */
	flags = inb(dev->iobase + PCI9111_AI_TRIG_CTRL_REG);
	/* Mask off the PTRG bit */
	flags &= 0x7;

	if (pretrigger)
		flags |= PCI9111_AI_TRIG_CTRL_PTRG;

	outb(flags, dev->iobase + PCI9111_AI_TRIG_CTRL_REG);
}

static void pci9111_autoscan_set(struct comedi_device *dev, bool autoscan)
{
	int flags;

	/* Read the current trigger mode control bits */
	flags = inb(dev->iobase + PCI9111_AI_TRIG_CTRL_REG);
	/* Mask off the ASCAN bit */
	flags &= 0xe;

	if (autoscan)
		flags |= PCI9111_AI_TRIG_CTRL_ASCAN;

	outb(flags, dev->iobase + PCI9111_AI_TRIG_CTRL_REG);
}

enum pci9111_ISC0_sources {
	irq_on_eoc,
	irq_on_fifo_half_full
};

enum pci9111_ISC1_sources {
	irq_on_timer_tick,
	irq_on_external_trigger
};

static void pci9111_interrupt_source_set(struct comedi_device *dev,
					 enum pci9111_ISC0_sources irq_0_source,
					 enum pci9111_ISC1_sources irq_1_source)
{
	int flags;

	/* Read the current interrupt control bits */
	flags = inb(dev->iobase + PCI9111_AI_TRIG_CTRL_REG);
	/* Shift the bits so they are compatible with the write register */
	flags >>= 4;
	/* Mask off the ISCx bits */
	flags &= 0xc0;

	/* Now set the new ISCx bits */
	if (irq_0_source == irq_on_fifo_half_full)
		flags |= PCI9111_INT_CTRL_ISC0;

	if (irq_1_source == irq_on_external_trigger)
		flags |= PCI9111_INT_CTRL_ISC1;

	outb(flags, dev->iobase + PCI9111_INT_CTRL_REG);
}

static void pci9111_fifo_reset(struct comedi_device *dev)
{
	unsigned long int_ctrl_reg = dev->iobase + PCI9111_INT_CTRL_REG;

	/* To reset the FIFO, set FFEN sequence as 0 -> 1 -> 0 */
	outb(0, int_ctrl_reg);
	outb(PCI9111_INT_CTRL_FFEN, int_ctrl_reg);
	outb(0, int_ctrl_reg);
}

static int pci9111_ai_cancel(struct comedi_device *dev,
			     struct comedi_subdevice *s)
{
	struct pci9111_private_data *dev_private = dev->private;

	/*  Disable interrupts */
	plx9050_interrupt_control(dev_private->lcr_io_base, true, true, true,
				  true, false);

	pci9111_trigger_source_set(dev, software);

	pci9111_autoscan_set(dev, false);

	pci9111_fifo_reset(dev);

	return 0;
}

static int pci9111_ai_do_cmd_test(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_cmd *cmd)
{
	struct pci9111_private_data *dev_private = dev->private;
	int tmp;
	int error = 0;
	int range, reference;
	int i;

	/* Step 1 : check if triggers are trivially valid */

	error |= cfc_check_trigger_src(&cmd->start_src, TRIG_NOW);
	error |= cfc_check_trigger_src(&cmd->scan_begin_src,
					TRIG_TIMER | TRIG_FOLLOW | TRIG_EXT);
	error |= cfc_check_trigger_src(&cmd->convert_src,
					TRIG_TIMER | TRIG_EXT);
	error |= cfc_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	error |= cfc_check_trigger_src(&cmd->stop_src,
					TRIG_COUNT | TRIG_NONE);

	if (error)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	error |= cfc_check_trigger_is_unique(cmd->scan_begin_src);
	error |= cfc_check_trigger_is_unique(cmd->convert_src);
	error |= cfc_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	if ((cmd->convert_src == TRIG_TIMER) &&
	    !((cmd->scan_begin_src == TRIG_TIMER) ||
	      (cmd->scan_begin_src == TRIG_FOLLOW)))
		error |= -EINVAL;
	if ((cmd->convert_src == TRIG_EXT) &&
	    !((cmd->scan_begin_src == TRIG_EXT) ||
	      (cmd->scan_begin_src == TRIG_FOLLOW)))
		error |= -EINVAL;

	if (error)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	error |= cfc_check_trigger_arg_is(&cmd->start_arg, 0);

	if (cmd->convert_src == TRIG_TIMER)
		error |= cfc_check_trigger_arg_min(&cmd->convert_arg,
					PCI9111_AI_ACQUISITION_PERIOD_MIN_NS);
	else	/* TRIG_EXT */
		error |= cfc_check_trigger_arg_is(&cmd->convert_arg, 0);

	if (cmd->scan_begin_src == TRIG_TIMER)
		error |= cfc_check_trigger_arg_min(&cmd->scan_begin_arg,
					PCI9111_AI_ACQUISITION_PERIOD_MIN_NS);
	else	/* TRIG_FOLLOW || TRIG_EXT */
		error |= cfc_check_trigger_arg_is(&cmd->scan_begin_arg, 0);

	error |= cfc_check_trigger_arg_is(&cmd->scan_end_arg,
					  cmd->chanlist_len);

	if (cmd->stop_src == TRIG_COUNT)
		error |= cfc_check_trigger_arg_min(&cmd->stop_arg, 1);
	else	/* TRIG_NONE */
		error |= cfc_check_trigger_arg_is(&cmd->stop_arg, 0);

	if (error)
		return 3;

	/*  Step 4 : fix up any arguments */

	if (cmd->convert_src == TRIG_TIMER) {
		tmp = cmd->convert_arg;
		i8253_cascade_ns_to_timer_2div(PCI9111_8254_CLOCK_PERIOD_NS,
					       &dev_private->div1,
					       &dev_private->div2,
					       &cmd->convert_arg,
					       cmd->flags & TRIG_ROUND_MASK);
		if (tmp != cmd->convert_arg)
			error++;
	}
	/*  There's only one timer on this card, so the scan_begin timer must */
	/*  be a multiple of chanlist_len*convert_arg */

	if (cmd->scan_begin_src == TRIG_TIMER) {

		unsigned int scan_begin_min;
		unsigned int scan_begin_arg;
		unsigned int scan_factor;

		scan_begin_min = cmd->chanlist_len * cmd->convert_arg;

		if (cmd->scan_begin_arg != scan_begin_min) {
			if (scan_begin_min < cmd->scan_begin_arg) {
				scan_factor =
				    cmd->scan_begin_arg / scan_begin_min;
				scan_begin_arg = scan_factor * scan_begin_min;
				if (cmd->scan_begin_arg != scan_begin_arg) {
					cmd->scan_begin_arg = scan_begin_arg;
					error++;
				}
			} else {
				cmd->scan_begin_arg = scan_begin_min;
				error++;
			}
		}
	}

	if (error)
		return 4;

	/*  Step 5 : check channel list */

	if (cmd->chanlist) {

		range = CR_RANGE(cmd->chanlist[0]);
		reference = CR_AREF(cmd->chanlist[0]);

		if (cmd->chanlist_len > 1) {
			for (i = 0; i < cmd->chanlist_len; i++) {
				if (CR_CHAN(cmd->chanlist[i]) != i) {
					comedi_error(dev,
						     "entries in chanlist must be consecutive "
						     "channels,counting upwards from 0\n");
					error++;
				}
				if (CR_RANGE(cmd->chanlist[i]) != range) {
					comedi_error(dev,
						     "entries in chanlist must all have the same gain\n");
					error++;
				}
				if (CR_AREF(cmd->chanlist[i]) != reference) {
					comedi_error(dev,
						     "entries in chanlist must all have the same reference\n");
					error++;
				}
			}
		}
	}

	if (error)
		return 5;

	return 0;

}

static int pci9111_ai_do_cmd(struct comedi_device *dev,
			     struct comedi_subdevice *s)
{
	struct pci9111_private_data *dev_private = dev->private;
	struct comedi_cmd *async_cmd = &s->async->cmd;

	if (!dev->irq) {
		comedi_error(dev,
			     "no irq assigned for PCI9111, cannot do hardware conversion");
		return -1;
	}
	/*  Set channel scan limit */
	/*  PCI9111 allows only scanning from channel 0 to channel n */
	/*  TODO: handle the case of an external multiplexer */

	if (async_cmd->chanlist_len > 1) {
		outb(async_cmd->chanlist_len - 1,
			dev->iobase + PCI9111_AI_CHANNEL_REG);
		pci9111_autoscan_set(dev, true);
	} else {
		outb(CR_CHAN(async_cmd->chanlist[0]),
			dev->iobase + PCI9111_AI_CHANNEL_REG);
		pci9111_autoscan_set(dev, false);
	}

	/*  Set gain */
	/*  This is the same gain on every channel */

	outb(CR_RANGE(async_cmd->chanlist[0]) & PCI9111_AI_RANGE_MASK,
		dev->iobase + PCI9111_AI_RANGE_STAT_REG);

	/* Set counter */

	switch (async_cmd->stop_src) {
	case TRIG_COUNT:
		dev_private->stop_counter =
		    async_cmd->stop_arg * async_cmd->chanlist_len;
		dev_private->stop_is_none = 0;
		break;

	case TRIG_NONE:
		dev_private->stop_counter = 0;
		dev_private->stop_is_none = 1;
		break;

	default:
		comedi_error(dev, "Invalid stop trigger");
		return -1;
	}

	/*  Set timer pacer */

	dev_private->scan_delay = 0;
	switch (async_cmd->convert_src) {
	case TRIG_TIMER:
		pci9111_trigger_source_set(dev, software);
		pci9111_timer_set(dev);
		pci9111_fifo_reset(dev);
		pci9111_interrupt_source_set(dev, irq_on_fifo_half_full,
					     irq_on_timer_tick);
		pci9111_trigger_source_set(dev, timer_pacer);
		plx9050_interrupt_control(dev_private->lcr_io_base, true, true,
					  false, true, true);

		if (async_cmd->scan_begin_src == TRIG_TIMER) {
			dev_private->scan_delay =
				(async_cmd->scan_begin_arg /
				 (async_cmd->convert_arg *
				  async_cmd->chanlist_len)) - 1;
		}

		break;

	case TRIG_EXT:

		pci9111_trigger_source_set(dev, external);
		pci9111_fifo_reset(dev);
		pci9111_interrupt_source_set(dev, irq_on_fifo_half_full,
					     irq_on_timer_tick);
		plx9050_interrupt_control(dev_private->lcr_io_base, true, true,
					  false, true, true);

		break;

	default:
		comedi_error(dev, "Invalid convert trigger");
		return -1;
	}

	dev_private->stop_counter *= (1 + dev_private->scan_delay);
	dev_private->chanlist_len = async_cmd->chanlist_len;
	dev_private->chunk_counter = 0;
	dev_private->chunk_num_samples =
	    dev_private->chanlist_len * (1 + dev_private->scan_delay);

	return 0;
}

static void pci9111_ai_munge(struct comedi_device *dev,
			     struct comedi_subdevice *s, void *data,
			     unsigned int num_bytes,
			     unsigned int start_chan_index)
{
	short *array = data;
	unsigned int maxdata = s->maxdata;
	unsigned int invert = (maxdata + 1) >> 1;
	unsigned int shift = (maxdata == 0xffff) ? 0 : 4;
	unsigned int num_samples = num_bytes / sizeof(short);
	unsigned int i;

	for (i = 0; i < num_samples; i++)
		array[i] = ((array[i] >> shift) & maxdata) ^ invert;
}

static irqreturn_t pci9111_interrupt(int irq, void *p_device)
{
	struct comedi_device *dev = p_device;
	struct pci9111_private_data *dev_private = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	struct comedi_async *async;
	unsigned int status;
	unsigned long irq_flags;
	unsigned char intcsr;

	if (!dev->attached) {
		/*  Ignore interrupt before device fully attached. */
		/*  Might not even have allocated subdevices yet! */
		return IRQ_NONE;
	}

	async = s->async;

	spin_lock_irqsave(&dev->spinlock, irq_flags);

	/*  Check if we are source of interrupt */
	intcsr = inb(dev_private->lcr_io_base +
		     PLX9050_REGISTER_INTERRUPT_CONTROL);
	if (!(((intcsr & PLX9050_PCI_INTERRUPT_ENABLE) != 0)
	      && (((intcsr & (PLX9050_LINTI1_ENABLE | PLX9050_LINTI1_STATUS))
		   == (PLX9050_LINTI1_ENABLE | PLX9050_LINTI1_STATUS))
		  || ((intcsr & (PLX9050_LINTI2_ENABLE | PLX9050_LINTI2_STATUS))
		      == (PLX9050_LINTI2_ENABLE | PLX9050_LINTI2_STATUS))))) {
		/*  Not the source of the interrupt. */
		/*  (N.B. not using PLX9050_SOFTWARE_INTERRUPT) */
		spin_unlock_irqrestore(&dev->spinlock, irq_flags);
		return IRQ_NONE;
	}

	if ((intcsr & (PLX9050_LINTI1_ENABLE | PLX9050_LINTI1_STATUS)) ==
	    (PLX9050_LINTI1_ENABLE | PLX9050_LINTI1_STATUS)) {
		/*  Interrupt comes from fifo_half-full signal */

		status = inb(dev->iobase + PCI9111_AI_RANGE_STAT_REG);

		/* '0' means FIFO is full, data may have been lost */
		if (!(status & PCI9111_AI_STAT_FF_FF)) {
			spin_unlock_irqrestore(&dev->spinlock, irq_flags);
			comedi_error(dev, PCI9111_DRIVER_NAME " fifo overflow");
			outb(0, dev->iobase + PCI9111_INT_CLR_REG);
			pci9111_ai_cancel(dev, s);
			async->events |= COMEDI_CB_ERROR | COMEDI_CB_EOA;
			comedi_event(dev, s);

			return IRQ_HANDLED;
		}

		/* '0' means FIFO is half-full */
		if (!(status & PCI9111_AI_STAT_FF_HF)) {
			unsigned int num_samples;
			unsigned int bytes_written = 0;

			num_samples =
			    PCI9111_FIFO_HALF_SIZE >
			    dev_private->stop_counter
			    && !dev_private->
			    stop_is_none ? dev_private->stop_counter :
			    PCI9111_FIFO_HALF_SIZE;
			insw(dev->iobase + PCI9111_AI_FIFO_REG,
			     dev_private->ai_bounce_buffer, num_samples);

			if (dev_private->scan_delay < 1) {
				bytes_written =
				    cfc_write_array_to_buffer(s,
							      dev_private->
							      ai_bounce_buffer,
							      num_samples *
							      sizeof(short));
			} else {
				int position = 0;
				int to_read;

				while (position < num_samples) {
					if (dev_private->chunk_counter <
					    dev_private->chanlist_len) {
						to_read =
						    dev_private->chanlist_len -
						    dev_private->chunk_counter;

						if (to_read >
						    num_samples - position)
							to_read =
							    num_samples -
							    position;

						bytes_written +=
						    cfc_write_array_to_buffer
						    (s,
						     dev_private->ai_bounce_buffer
						     + position,
						     to_read * sizeof(short));
					} else {
						to_read =
						    dev_private->chunk_num_samples
						    -
						    dev_private->chunk_counter;
						if (to_read >
						    num_samples - position)
							to_read =
							    num_samples -
							    position;

						bytes_written +=
						    sizeof(short) * to_read;
					}

					position += to_read;
					dev_private->chunk_counter += to_read;

					if (dev_private->chunk_counter >=
					    dev_private->chunk_num_samples)
						dev_private->chunk_counter = 0;
				}
			}

			dev_private->stop_counter -=
			    bytes_written / sizeof(short);
		}
	}

	if ((dev_private->stop_counter == 0) && (!dev_private->stop_is_none)) {
		async->events |= COMEDI_CB_EOA;
		pci9111_ai_cancel(dev, s);
	}

	outb(0, dev->iobase + PCI9111_INT_CLR_REG);

	spin_unlock_irqrestore(&dev->spinlock, irq_flags);

	comedi_event(dev, s);

	return IRQ_HANDLED;
}

static int pci9111_ai_insn_read(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int range = CR_RANGE(insn->chanspec);
	unsigned int maxdata = s->maxdata;
	unsigned int invert = (maxdata + 1) >> 1;
	unsigned int shift = (maxdata == 0xffff) ? 0 : 4;
	unsigned int status;
	int timeout;
	int i;

	outb(chan, dev->iobase + PCI9111_AI_CHANNEL_REG);

	status = inb(dev->iobase + PCI9111_AI_RANGE_STAT_REG);
	if ((status & PCI9111_AI_RANGE_MASK) != range) {
		outb(range & PCI9111_AI_RANGE_MASK,
			dev->iobase + PCI9111_AI_RANGE_STAT_REG);
	}

	pci9111_fifo_reset(dev);

	for (i = 0; i < insn->n; i++) {
		/* Generate a software trigger */
		outb(0, dev->iobase + PCI9111_SOFT_TRIG_REG);

		timeout = PCI9111_AI_INSTANT_READ_TIMEOUT;

		while (timeout--) {
			status = inb(dev->iobase + PCI9111_AI_RANGE_STAT_REG);
			/* '1' means FIFO is not empty */
			if (status & PCI9111_AI_STAT_FF_EF)
				goto conversion_done;
		}

		comedi_error(dev, "A/D read timeout");
		data[i] = 0;
		pci9111_fifo_reset(dev);
		return -ETIME;

conversion_done:

		data[i] = inw(dev->iobase + PCI9111_AI_FIFO_REG);
		data[i] = ((data[i] >> shift) & maxdata) ^ invert;
	}

	return i;
}

static int pci9111_ao_insn_write(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	struct pci9111_private_data *dev_private = dev->private;
	unsigned int val = 0;
	int i;

	for (i = 0; i < insn->n; i++) {
		val = data[i];
		outw(val, dev->iobase + PCI9111_AO_REG);
	}
	dev_private->ao_readback = val;

	return insn->n;
}

static int pci9111_ao_insn_read(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	struct pci9111_private_data *dev_private = dev->private;
	int i;

	for (i = 0; i < insn->n; i++)
		data[i] = dev_private->ao_readback;

	return insn->n;
}

static int pci9111_di_insn_bits(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	data[1] = inw(dev->iobase + PCI9111_DIO_REG);

	return insn->n;
}

static int pci9111_do_insn_bits(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	unsigned int mask = data[0];
	unsigned int bits = data[1];

	if (mask) {
		s->state &= ~mask;
		s->state |= (bits & mask);

		outw(s->state, dev->iobase + PCI9111_DIO_REG);
	}

	data[1] = s->state;

	return insn->n;
}

static int pci9111_reset(struct comedi_device *dev)
{
	struct pci9111_private_data *dev_private = dev->private;

	/*  Set trigger source to software */
	plx9050_interrupt_control(dev_private->lcr_io_base, true, true, true,
				  true, false);

	pci9111_trigger_source_set(dev, software);
	pci9111_pretrigger_set(dev, false);
	pci9111_autoscan_set(dev, false);

	/* Reset 8254 chip */
	dev_private->div1 = 0;
	dev_private->div2 = 0;
	pci9111_timer_set(dev);

	return 0;
}

static int pci9111_auto_attach(struct comedi_device *dev,
					 unsigned long context_unused)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct pci9111_private_data *dev_private;
	struct comedi_subdevice *s;
	int ret;

	dev->board_name = dev->driver->driver_name;

	dev_private = kzalloc(sizeof(*dev_private), GFP_KERNEL);
	if (!dev_private)
		return -ENOMEM;
	dev->private = dev_private;

	ret = comedi_pci_enable(pcidev, dev->board_name);
	if (ret)
		return ret;
	dev_private->lcr_io_base = pci_resource_start(pcidev, 1);
	dev->iobase = pci_resource_start(pcidev, 2);

	pci9111_reset(dev);

	if (pcidev->irq > 0) {
		ret = request_irq(dev->irq, pci9111_interrupt,
				  IRQF_SHARED, dev->board_name, dev);
		if (ret)
			return ret;
		dev->irq = pcidev->irq;
	}

	ret = comedi_alloc_subdevices(dev, 4);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	dev->read_subdev = s;
	s->type		= COMEDI_SUBD_AI;
	s->subdev_flags	= SDF_READABLE | SDF_COMMON | SDF_CMD_READ;
	s->n_chan	= 16;
	s->maxdata	= 0xffff;
	s->len_chanlist	= 16;
	s->range_table	= &pci9111_ai_range;
	s->cancel	= pci9111_ai_cancel;
	s->insn_read	= pci9111_ai_insn_read;
	s->do_cmdtest	= pci9111_ai_do_cmd_test;
	s->do_cmd	= pci9111_ai_do_cmd;
	s->munge	= pci9111_ai_munge;

	s = &dev->subdevices[1];
	s->type		= COMEDI_SUBD_AO;
	s->subdev_flags	= SDF_WRITABLE | SDF_COMMON;
	s->n_chan	= 1;
	s->maxdata	= 0x0fff;
	s->len_chanlist	= 1;
	s->range_table	= &range_bipolar10;
	s->insn_write	= pci9111_ao_insn_write;
	s->insn_read	= pci9111_ao_insn_read;

	s = &dev->subdevices[2];
	s->type		= COMEDI_SUBD_DI;
	s->subdev_flags	= SDF_READABLE;
	s->n_chan	= 16;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= pci9111_di_insn_bits;

	s = &dev->subdevices[3];
	s->type		= COMEDI_SUBD_DO;
	s->subdev_flags	= SDF_READABLE | SDF_WRITABLE;
	s->n_chan	= 16;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= pci9111_do_insn_bits;

	dev_info(dev->class_dev, "%s attached\n", dev->board_name);

	return 0;
}

static void pci9111_detach(struct comedi_device *dev)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);

	if (dev->iobase)
		pci9111_reset(dev);
	if (dev->irq != 0)
		free_irq(dev->irq, dev);
	if (pcidev) {
		if (dev->iobase)
			comedi_pci_disable(pcidev);
	}
}

static struct comedi_driver adl_pci9111_driver = {
	.driver_name	= "adl_pci9111",
	.module		= THIS_MODULE,
	.auto_attach	= pci9111_auto_attach,
	.detach		= pci9111_detach,
};

static int pci9111_pci_probe(struct pci_dev *dev,
				       const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, &adl_pci9111_driver);
}

static void __devexit pci9111_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static DEFINE_PCI_DEVICE_TABLE(pci9111_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADLINK, PCI9111_HR_DEVICE_ID) },
	/* { PCI_DEVICE(PCI_VENDOR_ID_ADLINK, PCI9111_HG_DEVICE_ID) }, */
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, pci9111_pci_table);

static struct pci_driver adl_pci9111_pci_driver = {
	.name		= "adl_pci9111",
	.id_table	= pci9111_pci_table,
	.probe		= pci9111_pci_probe,
	.remove		= pci9111_pci_remove,
};
module_comedi_pci_driver(adl_pci9111_driver, adl_pci9111_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
