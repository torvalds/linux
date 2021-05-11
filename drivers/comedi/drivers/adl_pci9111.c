// SPDX-License-Identifier: GPL-2.0+
/*
 * adl_pci9111.c
 * Hardware driver for PCI9111 ADLink cards: PCI-9111HR
 * Copyright (C) 2002-2005 Emmanuel Pacaud <emmanuel.pacaud@univ-poitiers.fr>
 */

/*
 * Driver: adl_pci9111
 * Description: Adlink PCI-9111HR
 * Devices: [ADLink] PCI-9111HR (adl_pci9111)
 * Author: Emmanuel Pacaud <emmanuel.pacaud@univ-poitiers.fr>
 * Status: experimental
 *
 * Configuration options: not applicable, uses PCI auto config
 *
 * Supports:
 * - ai_insn read
 * - ao_insn read/write
 * - di_insn read
 * - do_insn read/write
 * - ai_do_cmd mode with the following sources:
 *	- start_src		TRIG_NOW
 *	- scan_begin_src	TRIG_FOLLOW	TRIG_TIMER	TRIG_EXT
 *	- convert_src				TRIG_TIMER	TRIG_EXT
 *	- scan_end_src		TRIG_COUNT
 *	- stop_src		TRIG_COUNT	TRIG_NONE
 *
 * The scanned channels must be consecutive and start from 0. They must
 * all have the same range and aref.
 */

/*
 * TODO:
 * - Really test implemented functionality.
 * - Add support for the PCI-9111DG with a probe routine to identify
 *   the card type (perhaps with the help of the channel number readback
 *   of the A/D Data register).
 * - Add external multiplexer support.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/interrupt.h>

#include "../comedi_pci.h"

#include "plx9052.h"
#include "comedi_8254.h"

#define PCI9111_FIFO_HALF_SIZE	512

#define PCI9111_AI_ACQUISITION_PERIOD_MIN_NS	10000

#define PCI9111_RANGE_SETTING_DELAY		10
#define PCI9111_AI_INSTANT_READ_UDELAY_US	2

/*
 * IO address map and bit defines
 */
#define PCI9111_AI_FIFO_REG		0x00
#define PCI9111_AO_REG			0x00
#define PCI9111_DIO_REG			0x02
#define PCI9111_EDIO_REG		0x04
#define PCI9111_AI_CHANNEL_REG		0x06
#define PCI9111_AI_RANGE_STAT_REG	0x08
#define PCI9111_AI_STAT_AD_BUSY		BIT(7)
#define PCI9111_AI_STAT_FF_FF		BIT(6)
#define PCI9111_AI_STAT_FF_HF		BIT(5)
#define PCI9111_AI_STAT_FF_EF		BIT(4)
#define PCI9111_AI_RANGE(x)		(((x) & 0x7) << 0)
#define PCI9111_AI_RANGE_MASK		PCI9111_AI_RANGE(7)
#define PCI9111_AI_TRIG_CTRL_REG	0x0a
#define PCI9111_AI_TRIG_CTRL_TRGEVENT	BIT(5)
#define PCI9111_AI_TRIG_CTRL_POTRG	BIT(4)
#define PCI9111_AI_TRIG_CTRL_PTRG	BIT(3)
#define PCI9111_AI_TRIG_CTRL_ETIS	BIT(2)
#define PCI9111_AI_TRIG_CTRL_TPST	BIT(1)
#define PCI9111_AI_TRIG_CTRL_ASCAN	BIT(0)
#define PCI9111_INT_CTRL_REG		0x0c
#define PCI9111_INT_CTRL_ISC2		BIT(3)
#define PCI9111_INT_CTRL_FFEN		BIT(2)
#define PCI9111_INT_CTRL_ISC1		BIT(1)
#define PCI9111_INT_CTRL_ISC0		BIT(0)
#define PCI9111_SOFT_TRIG_REG		0x0e
#define PCI9111_8254_BASE_REG		0x40
#define PCI9111_INT_CLR_REG		0x48

/* PLX 9052 Local Interrupt 1 enabled and active */
#define PCI9111_LI1_ACTIVE	(PLX9052_INTCSR_LI1ENAB |	\
				 PLX9052_INTCSR_LI1STAT)

/* PLX 9052 Local Interrupt 2 enabled and active */
#define PCI9111_LI2_ACTIVE	(PLX9052_INTCSR_LI2ENAB |	\
				 PLX9052_INTCSR_LI2STAT)

static const struct comedi_lrange pci9111_ai_range = {
	5, {
		BIP_RANGE(10),
		BIP_RANGE(5),
		BIP_RANGE(2.5),
		BIP_RANGE(1.25),
		BIP_RANGE(0.625)
	}
};

struct pci9111_private_data {
	unsigned long lcr_io_base;

	unsigned int scan_delay;
	unsigned int chunk_counter;
	unsigned int chunk_num_samples;

	unsigned short ai_bounce_buffer[2 * PCI9111_FIFO_HALF_SIZE];
};

static void plx9050_interrupt_control(unsigned long io_base,
				      bool int1_enable,
				      bool int1_active_high,
				      bool int2_enable,
				      bool int2_active_high,
				      bool interrupt_enable)
{
	int flags = 0;

	if (int1_enable)
		flags |= PLX9052_INTCSR_LI1ENAB;
	if (int1_active_high)
		flags |= PLX9052_INTCSR_LI1POL;
	if (int2_enable)
		flags |= PLX9052_INTCSR_LI2ENAB;
	if (int2_active_high)
		flags |= PLX9052_INTCSR_LI2POL;

	if (interrupt_enable)
		flags |= PLX9052_INTCSR_PCIENAB;

	outb(flags, io_base + PLX9052_INTCSR);
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

	/* disable A/D triggers (software trigger mode) and auto scan off */
	outb(0, dev->iobase + PCI9111_AI_TRIG_CTRL_REG);

	pci9111_fifo_reset(dev);

	return 0;
}

static int pci9111_ai_check_chanlist(struct comedi_device *dev,
				     struct comedi_subdevice *s,
				     struct comedi_cmd *cmd)
{
	unsigned int range0 = CR_RANGE(cmd->chanlist[0]);
	unsigned int aref0 = CR_AREF(cmd->chanlist[0]);
	int i;

	for (i = 1; i < cmd->chanlist_len; i++) {
		unsigned int chan = CR_CHAN(cmd->chanlist[i]);
		unsigned int range = CR_RANGE(cmd->chanlist[i]);
		unsigned int aref = CR_AREF(cmd->chanlist[i]);

		if (chan != i) {
			dev_dbg(dev->class_dev,
				"entries in chanlist must be consecutive channels,counting upwards from 0\n");
			return -EINVAL;
		}

		if (range != range0) {
			dev_dbg(dev->class_dev,
				"entries in chanlist must all have the same gain\n");
			return -EINVAL;
		}

		if (aref != aref0) {
			dev_dbg(dev->class_dev,
				"entries in chanlist must all have the same reference\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int pci9111_ai_do_cmd_test(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_cmd *cmd)
{
	int err = 0;
	unsigned int arg;

	/* Step 1 : check if triggers are trivially valid */

	err |= comedi_check_trigger_src(&cmd->start_src, TRIG_NOW);
	err |= comedi_check_trigger_src(&cmd->scan_begin_src,
					TRIG_TIMER | TRIG_FOLLOW | TRIG_EXT);
	err |= comedi_check_trigger_src(&cmd->convert_src,
					TRIG_TIMER | TRIG_EXT);
	err |= comedi_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= comedi_check_trigger_src(&cmd->stop_src,
					TRIG_COUNT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= comedi_check_trigger_is_unique(cmd->scan_begin_src);
	err |= comedi_check_trigger_is_unique(cmd->convert_src);
	err |= comedi_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	if (cmd->scan_begin_src != TRIG_FOLLOW) {
		if (cmd->scan_begin_src != cmd->convert_src)
			err |= -EINVAL;
	}

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	err |= comedi_check_trigger_arg_is(&cmd->start_arg, 0);

	if (cmd->convert_src == TRIG_TIMER) {
		err |= comedi_check_trigger_arg_min(&cmd->convert_arg,
					PCI9111_AI_ACQUISITION_PERIOD_MIN_NS);
	} else {	/* TRIG_EXT */
		err |= comedi_check_trigger_arg_is(&cmd->convert_arg, 0);
	}

	if (cmd->scan_begin_src == TRIG_TIMER) {
		err |= comedi_check_trigger_arg_min(&cmd->scan_begin_arg,
					PCI9111_AI_ACQUISITION_PERIOD_MIN_NS);
	} else {	/* TRIG_FOLLOW || TRIG_EXT */
		err |= comedi_check_trigger_arg_is(&cmd->scan_begin_arg, 0);
	}

	err |= comedi_check_trigger_arg_is(&cmd->scan_end_arg,
					   cmd->chanlist_len);

	if (cmd->stop_src == TRIG_COUNT)
		err |= comedi_check_trigger_arg_min(&cmd->stop_arg, 1);
	else	/* TRIG_NONE */
		err |= comedi_check_trigger_arg_is(&cmd->stop_arg, 0);

	if (err)
		return 3;

	/* Step 4: fix up any arguments */

	if (cmd->convert_src == TRIG_TIMER) {
		arg = cmd->convert_arg;
		comedi_8254_cascade_ns_to_timer(dev->pacer, &arg, cmd->flags);
		err |= comedi_check_trigger_arg_is(&cmd->convert_arg, arg);
	}

	/*
	 * There's only one timer on this card, so the scan_begin timer
	 * must be a multiple of chanlist_len*convert_arg
	 */
	if (cmd->scan_begin_src == TRIG_TIMER) {
		arg = cmd->chanlist_len * cmd->convert_arg;

		if (arg < cmd->scan_begin_arg)
			arg *= (cmd->scan_begin_arg / arg);

		err |= comedi_check_trigger_arg_is(&cmd->scan_begin_arg, arg);
	}

	if (err)
		return 4;

	/* Step 5: check channel list if it exists */
	if (cmd->chanlist && cmd->chanlist_len > 0)
		err |= pci9111_ai_check_chanlist(dev, s, cmd);

	if (err)
		return 5;

	return 0;
}

static int pci9111_ai_do_cmd(struct comedi_device *dev,
			     struct comedi_subdevice *s)
{
	struct pci9111_private_data *dev_private = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned int last_chan = CR_CHAN(cmd->chanlist[cmd->chanlist_len - 1]);
	unsigned int range0 = CR_RANGE(cmd->chanlist[0]);
	unsigned int trig = 0;

	/*  Set channel scan limit */
	/*  PCI9111 allows only scanning from channel 0 to channel n */
	/*  TODO: handle the case of an external multiplexer */

	if (cmd->chanlist_len > 1)
		trig |= PCI9111_AI_TRIG_CTRL_ASCAN;

	outb(last_chan, dev->iobase + PCI9111_AI_CHANNEL_REG);

	/*  Set gain - all channels use the same range */
	outb(PCI9111_AI_RANGE(range0), dev->iobase + PCI9111_AI_RANGE_STAT_REG);

	/*  Set timer pacer */
	dev_private->scan_delay = 0;
	if (cmd->convert_src == TRIG_TIMER) {
		trig |= PCI9111_AI_TRIG_CTRL_TPST;
		comedi_8254_update_divisors(dev->pacer);
		comedi_8254_pacer_enable(dev->pacer, 1, 2, true);
		pci9111_fifo_reset(dev);
		pci9111_interrupt_source_set(dev, irq_on_fifo_half_full,
					     irq_on_timer_tick);
		plx9050_interrupt_control(dev_private->lcr_io_base, true, true,
					  false, true, true);

		if (cmd->scan_begin_src == TRIG_TIMER) {
			dev_private->scan_delay = (cmd->scan_begin_arg /
				(cmd->convert_arg * cmd->chanlist_len)) - 1;
		}
	} else {	/* TRIG_EXT */
		trig |= PCI9111_AI_TRIG_CTRL_ETIS;
		pci9111_fifo_reset(dev);
		pci9111_interrupt_source_set(dev, irq_on_fifo_half_full,
					     irq_on_timer_tick);
		plx9050_interrupt_control(dev_private->lcr_io_base, true, true,
					  false, true, true);
	}
	outb(trig, dev->iobase + PCI9111_AI_TRIG_CTRL_REG);

	dev_private->chunk_counter = 0;
	dev_private->chunk_num_samples = cmd->chanlist_len *
					 (1 + dev_private->scan_delay);

	return 0;
}

static void pci9111_ai_munge(struct comedi_device *dev,
			     struct comedi_subdevice *s, void *data,
			     unsigned int num_bytes,
			     unsigned int start_chan_index)
{
	unsigned short *array = data;
	unsigned int maxdata = s->maxdata;
	unsigned int invert = (maxdata + 1) >> 1;
	unsigned int shift = (maxdata == 0xffff) ? 0 : 4;
	unsigned int num_samples = comedi_bytes_to_samples(s, num_bytes);
	unsigned int i;

	for (i = 0; i < num_samples; i++)
		array[i] = ((array[i] >> shift) & maxdata) ^ invert;
}

static void pci9111_handle_fifo_half_full(struct comedi_device *dev,
					  struct comedi_subdevice *s)
{
	struct pci9111_private_data *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned short *buf = devpriv->ai_bounce_buffer;
	unsigned int samples;

	samples = comedi_nsamples_left(s, PCI9111_FIFO_HALF_SIZE);
	insw(dev->iobase + PCI9111_AI_FIFO_REG, buf, samples);

	if (devpriv->scan_delay < 1) {
		comedi_buf_write_samples(s, buf, samples);
	} else {
		unsigned int pos = 0;
		unsigned int to_read;

		while (pos < samples) {
			if (devpriv->chunk_counter < cmd->chanlist_len) {
				to_read = cmd->chanlist_len -
					  devpriv->chunk_counter;

				if (to_read > samples - pos)
					to_read = samples - pos;

				comedi_buf_write_samples(s, buf + pos, to_read);
			} else {
				to_read = devpriv->chunk_num_samples -
					  devpriv->chunk_counter;

				if (to_read > samples - pos)
					to_read = samples - pos;
			}

			pos += to_read;
			devpriv->chunk_counter += to_read;

			if (devpriv->chunk_counter >=
			    devpriv->chunk_num_samples)
				devpriv->chunk_counter = 0;
		}
	}
}

static irqreturn_t pci9111_interrupt(int irq, void *p_device)
{
	struct comedi_device *dev = p_device;
	struct pci9111_private_data *dev_private = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	struct comedi_async *async;
	struct comedi_cmd *cmd;
	unsigned int status;
	unsigned long irq_flags;
	unsigned char intcsr;

	if (!dev->attached) {
		/*  Ignore interrupt before device fully attached. */
		/*  Might not even have allocated subdevices yet! */
		return IRQ_NONE;
	}

	async = s->async;
	cmd = &async->cmd;

	spin_lock_irqsave(&dev->spinlock, irq_flags);

	/*  Check if we are source of interrupt */
	intcsr = inb(dev_private->lcr_io_base + PLX9052_INTCSR);
	if (!(((intcsr & PLX9052_INTCSR_PCIENAB) != 0) &&
	      (((intcsr & PCI9111_LI1_ACTIVE) == PCI9111_LI1_ACTIVE) ||
	       ((intcsr & PCI9111_LI2_ACTIVE) == PCI9111_LI2_ACTIVE)))) {
		/*  Not the source of the interrupt. */
		/*  (N.B. not using PLX9052_INTCSR_SOFTINT) */
		spin_unlock_irqrestore(&dev->spinlock, irq_flags);
		return IRQ_NONE;
	}

	if ((intcsr & PCI9111_LI1_ACTIVE) == PCI9111_LI1_ACTIVE) {
		/*  Interrupt comes from fifo_half-full signal */

		status = inb(dev->iobase + PCI9111_AI_RANGE_STAT_REG);

		/* '0' means FIFO is full, data may have been lost */
		if (!(status & PCI9111_AI_STAT_FF_FF)) {
			spin_unlock_irqrestore(&dev->spinlock, irq_flags);
			dev_dbg(dev->class_dev, "fifo overflow\n");
			outb(0, dev->iobase + PCI9111_INT_CLR_REG);
			async->events |= COMEDI_CB_ERROR;
			comedi_handle_events(dev, s);

			return IRQ_HANDLED;
		}

		/* '0' means FIFO is half-full */
		if (!(status & PCI9111_AI_STAT_FF_HF))
			pci9111_handle_fifo_half_full(dev, s);
	}

	if (cmd->stop_src == TRIG_COUNT && async->scans_done >= cmd->stop_arg)
		async->events |= COMEDI_CB_EOA;

	outb(0, dev->iobase + PCI9111_INT_CLR_REG);

	spin_unlock_irqrestore(&dev->spinlock, irq_flags);

	comedi_handle_events(dev, s);

	return IRQ_HANDLED;
}

static int pci9111_ai_eoc(struct comedi_device *dev,
			  struct comedi_subdevice *s,
			  struct comedi_insn *insn,
			  unsigned long context)
{
	unsigned int status;

	status = inb(dev->iobase + PCI9111_AI_RANGE_STAT_REG);
	if (status & PCI9111_AI_STAT_FF_EF)
		return 0;
	return -EBUSY;
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
	int ret;
	int i;

	outb(chan, dev->iobase + PCI9111_AI_CHANNEL_REG);

	status = inb(dev->iobase + PCI9111_AI_RANGE_STAT_REG);
	if ((status & PCI9111_AI_RANGE_MASK) != range) {
		outb(PCI9111_AI_RANGE(range),
		     dev->iobase + PCI9111_AI_RANGE_STAT_REG);
	}

	pci9111_fifo_reset(dev);

	for (i = 0; i < insn->n; i++) {
		/* Generate a software trigger */
		outb(0, dev->iobase + PCI9111_SOFT_TRIG_REG);

		ret = comedi_timeout(dev, s, insn, pci9111_ai_eoc, 0);
		if (ret) {
			pci9111_fifo_reset(dev);
			return ret;
		}

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
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int val = s->readback[chan];
	int i;

	for (i = 0; i < insn->n; i++) {
		val = data[i];
		outw(val, dev->iobase + PCI9111_AO_REG);
	}
	s->readback[chan] = val;

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
	if (comedi_dio_update_state(s, data))
		outw(s->state, dev->iobase + PCI9111_DIO_REG);

	data[1] = s->state;

	return insn->n;
}

static int pci9111_reset(struct comedi_device *dev)
{
	struct pci9111_private_data *dev_private = dev->private;

	/*  Set trigger source to software */
	plx9050_interrupt_control(dev_private->lcr_io_base, true, true, true,
				  true, false);

	/* disable A/D triggers (software trigger mode) and auto scan off */
	outb(0, dev->iobase + PCI9111_AI_TRIG_CTRL_REG);

	return 0;
}

static int pci9111_auto_attach(struct comedi_device *dev,
			       unsigned long context_unused)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct pci9111_private_data *dev_private;
	struct comedi_subdevice *s;
	int ret;

	dev_private = comedi_alloc_devpriv(dev, sizeof(*dev_private));
	if (!dev_private)
		return -ENOMEM;

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;
	dev_private->lcr_io_base = pci_resource_start(pcidev, 1);
	dev->iobase = pci_resource_start(pcidev, 2);

	pci9111_reset(dev);

	if (pcidev->irq) {
		ret = request_irq(pcidev->irq, pci9111_interrupt,
				  IRQF_SHARED, dev->board_name, dev);
		if (ret == 0)
			dev->irq = pcidev->irq;
	}

	dev->pacer = comedi_8254_init(dev->iobase + PCI9111_8254_BASE_REG,
				      I8254_OSC_BASE_2MHZ, I8254_IO16, 0);
	if (!dev->pacer)
		return -ENOMEM;

	ret = comedi_alloc_subdevices(dev, 4);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_AI;
	s->subdev_flags	= SDF_READABLE | SDF_COMMON;
	s->n_chan	= 16;
	s->maxdata	= 0xffff;
	s->range_table	= &pci9111_ai_range;
	s->insn_read	= pci9111_ai_insn_read;
	if (dev->irq) {
		dev->read_subdev = s;
		s->subdev_flags	|= SDF_CMD_READ;
		s->len_chanlist	= s->n_chan;
		s->do_cmdtest	= pci9111_ai_do_cmd_test;
		s->do_cmd	= pci9111_ai_do_cmd;
		s->cancel	= pci9111_ai_cancel;
		s->munge	= pci9111_ai_munge;
	}

	s = &dev->subdevices[1];
	s->type		= COMEDI_SUBD_AO;
	s->subdev_flags	= SDF_WRITABLE | SDF_COMMON;
	s->n_chan	= 1;
	s->maxdata	= 0x0fff;
	s->len_chanlist	= 1;
	s->range_table	= &range_bipolar10;
	s->insn_write	= pci9111_ao_insn_write;

	ret = comedi_alloc_subdev_readback(s);
	if (ret)
		return ret;

	s = &dev->subdevices[2];
	s->type		= COMEDI_SUBD_DI;
	s->subdev_flags	= SDF_READABLE;
	s->n_chan	= 16;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= pci9111_di_insn_bits;

	s = &dev->subdevices[3];
	s->type		= COMEDI_SUBD_DO;
	s->subdev_flags	= SDF_WRITABLE;
	s->n_chan	= 16;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= pci9111_do_insn_bits;

	return 0;
}

static void pci9111_detach(struct comedi_device *dev)
{
	if (dev->iobase)
		pci9111_reset(dev);
	comedi_pci_detach(dev);
}

static struct comedi_driver adl_pci9111_driver = {
	.driver_name	= "adl_pci9111",
	.module		= THIS_MODULE,
	.auto_attach	= pci9111_auto_attach,
	.detach		= pci9111_detach,
};

static int pci9111_pci_probe(struct pci_dev *dev,
			     const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &adl_pci9111_driver,
				      id->driver_data);
}

static const struct pci_device_id pci9111_pci_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADLINK, 0x9111) },
	/* { PCI_DEVICE(PCI_VENDOR_ID_ADLINK, PCI9111_HG_DEVICE_ID) }, */
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, pci9111_pci_table);

static struct pci_driver adl_pci9111_pci_driver = {
	.name		= "adl_pci9111",
	.id_table	= pci9111_pci_table,
	.probe		= pci9111_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(adl_pci9111_driver, adl_pci9111_pci_driver);

MODULE_AUTHOR("Comedi https://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
