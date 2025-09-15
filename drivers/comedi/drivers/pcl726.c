// SPDX-License-Identifier: GPL-2.0+
/*
 * pcl726.c
 * Comedi driver for 6/12-Channel D/A Output and DIO cards
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 1998 David A. Schleef <ds@schleef.org>
 */

/*
 * Driver: pcl726
 * Description: Advantech PCL-726 & compatibles
 * Author: David A. Schleef <ds@schleef.org>
 * Status: untested
 * Devices: [Advantech] PCL-726 (pcl726), PCL-727 (pcl727), PCL-728 (pcl728),
 *   [ADLink] ACL-6126 (acl6126), ACL-6128 (acl6128)
 *
 * Configuration Options:
 *   [0]  - IO Base
 *   [1]  - IRQ (ACL-6126 only)
 *   [2]  - D/A output range for channel 0
 *   [3]  - D/A output range for channel 1
 *
 * Boards with > 2 analog output channels:
 *   [4]  - D/A output range for channel 2
 *   [5]  - D/A output range for channel 3
 *   [6]  - D/A output range for channel 4
 *   [7]  - D/A output range for channel 5
 *
 * Boards with > 6 analog output channels:
 *   [8]  - D/A output range for channel 6
 *   [9]  - D/A output range for channel 7
 *   [10] - D/A output range for channel 8
 *   [11] - D/A output range for channel 9
 *   [12] - D/A output range for channel 10
 *   [13] - D/A output range for channel 11
 *
 * For PCL-726 the D/A output ranges are:
 *   0: 0-5V, 1: 0-10V, 2: +/-5V, 3: +/-10V, 4: 4-20mA, 5: unknown
 *
 * For PCL-727:
 *   0: 0-5V, 1: 0-10V, 2: +/-5V, 3: 4-20mA
 *
 * For PCL-728 and ACL-6128:
 *   0: 0-5V, 1: 0-10V, 2: +/-5V, 3: +/-10V, 4: 4-20mA, 5: 0-20mA
 *
 * For ACL-6126:
 *   0: 0-5V, 1: 0-10V, 2: +/-5V, 3: +/-10V, 4: 4-20mA
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/comedi/comedidev.h>

#define PCL726_AO_MSB_REG(x)	(0x00 + ((x) * 2))
#define PCL726_AO_LSB_REG(x)	(0x01 + ((x) * 2))
#define PCL726_DO_MSB_REG	0x0c
#define PCL726_DO_LSB_REG	0x0d
#define PCL726_DI_MSB_REG	0x0e
#define PCL726_DI_LSB_REG	0x0f

#define PCL727_DI_MSB_REG	0x00
#define PCL727_DI_LSB_REG	0x01
#define PCL727_DO_MSB_REG	0x18
#define PCL727_DO_LSB_REG	0x19

static const struct comedi_lrange *const rangelist_726[] = {
	&range_unipolar5,
	&range_unipolar10,
	&range_bipolar5,
	&range_bipolar10,
	&range_4_20mA,
	&range_unknown
};

static const struct comedi_lrange *const rangelist_727[] = {
	&range_unipolar5,
	&range_unipolar10,
	&range_bipolar5,
	&range_4_20mA
};

static const struct comedi_lrange *const rangelist_728[] = {
	&range_unipolar5,
	&range_unipolar10,
	&range_bipolar5,
	&range_bipolar10,
	&range_4_20mA,
	&range_0_20mA
};

struct pcl726_board {
	const char *name;
	unsigned long io_len;
	unsigned int irq_mask;
	const struct comedi_lrange *const *ao_ranges;
	int ao_num_ranges;
	int ao_nchan;
	unsigned int have_dio:1;
	unsigned int is_pcl727:1;
};

static const struct pcl726_board pcl726_boards[] = {
	{
		.name		= "pcl726",
		.io_len		= 0x10,
		.ao_ranges	= &rangelist_726[0],
		.ao_num_ranges	= ARRAY_SIZE(rangelist_726),
		.ao_nchan	= 6,
		.have_dio	= 1,
	}, {
		.name		= "pcl727",
		.io_len		= 0x20,
		.ao_ranges	= &rangelist_727[0],
		.ao_num_ranges	= ARRAY_SIZE(rangelist_727),
		.ao_nchan	= 12,
		.have_dio	= 1,
		.is_pcl727	= 1,
	}, {
		.name		= "pcl728",
		.io_len		= 0x08,
		.ao_num_ranges	= ARRAY_SIZE(rangelist_728),
		.ao_ranges	= &rangelist_728[0],
		.ao_nchan	= 2,
	}, {
		.name		= "acl6126",
		.io_len		= 0x10,
		.irq_mask	= 0x96e8,
		.ao_num_ranges	= ARRAY_SIZE(rangelist_726),
		.ao_ranges	= &rangelist_726[0],
		.ao_nchan	= 6,
		.have_dio	= 1,
	}, {
		.name		= "acl6128",
		.io_len		= 0x08,
		.ao_num_ranges	= ARRAY_SIZE(rangelist_728),
		.ao_ranges	= &rangelist_728[0],
		.ao_nchan	= 2,
	},
};

struct pcl726_private {
	const struct comedi_lrange *rangelist[12];
	unsigned int cmd_running:1;
};

static int pcl726_intr_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	data[1] = 0;
	return insn->n;
}

static int pcl726_intr_cmdtest(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_cmd *cmd)
{
	int err = 0;

	/* Step 1 : check if triggers are trivially valid */

	err |= comedi_check_trigger_src(&cmd->start_src, TRIG_NOW);
	err |= comedi_check_trigger_src(&cmd->scan_begin_src, TRIG_EXT);
	err |= comedi_check_trigger_src(&cmd->convert_src, TRIG_FOLLOW);
	err |= comedi_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= comedi_check_trigger_src(&cmd->stop_src, TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */
	/* Step 2b : and mutually compatible */

	/* Step 3: check if arguments are trivially valid */

	err |= comedi_check_trigger_arg_is(&cmd->start_arg, 0);
	err |= comedi_check_trigger_arg_is(&cmd->scan_begin_arg, 0);
	err |= comedi_check_trigger_arg_is(&cmd->convert_arg, 0);
	err |= comedi_check_trigger_arg_is(&cmd->scan_end_arg,
					   cmd->chanlist_len);
	err |= comedi_check_trigger_arg_is(&cmd->stop_arg, 0);

	if (err)
		return 3;

	/* Step 4: fix up any arguments */

	/* Step 5: check channel list if it exists */

	return 0;
}

static int pcl726_intr_cmd(struct comedi_device *dev,
			   struct comedi_subdevice *s)
{
	struct pcl726_private *devpriv = dev->private;

	devpriv->cmd_running = 1;

	return 0;
}

static int pcl726_intr_cancel(struct comedi_device *dev,
			      struct comedi_subdevice *s)
{
	struct pcl726_private *devpriv = dev->private;

	devpriv->cmd_running = 0;

	return 0;
}

static irqreturn_t pcl726_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct comedi_subdevice *s = dev->read_subdev;
	struct pcl726_private *devpriv = dev->private;

	if (devpriv->cmd_running) {
		unsigned short val = 0;

		pcl726_intr_cancel(dev, s);

		comedi_buf_write_samples(s, &val, 1);
		comedi_handle_events(dev, s);
	}

	return IRQ_HANDLED;
}

static int pcl726_ao_insn_write(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int range = CR_RANGE(insn->chanspec);
	int i;

	for (i = 0; i < insn->n; i++) {
		unsigned int val = data[i];

		s->readback[chan] = val;

		/* bipolar data to the DAC is two's complement */
		if (comedi_chan_range_is_bipolar(s, chan, range))
			val = comedi_offset_munge(s, val);

		/* order is important, MSB then LSB */
		outb((val >> 8) & 0xff, dev->iobase + PCL726_AO_MSB_REG(chan));
		outb(val & 0xff, dev->iobase + PCL726_AO_LSB_REG(chan));
	}

	return insn->n;
}

static int pcl726_di_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	const struct pcl726_board *board = dev->board_ptr;
	unsigned int val;

	if (board->is_pcl727) {
		val = inb(dev->iobase + PCL727_DI_LSB_REG);
		val |= (inb(dev->iobase + PCL727_DI_MSB_REG) << 8);
	} else {
		val = inb(dev->iobase + PCL726_DI_LSB_REG);
		val |= (inb(dev->iobase + PCL726_DI_MSB_REG) << 8);
	}

	data[1] = val;

	return insn->n;
}

static int pcl726_do_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	const struct pcl726_board *board = dev->board_ptr;
	unsigned long io = dev->iobase;
	unsigned int mask;

	mask = comedi_dio_update_state(s, data);
	if (mask) {
		if (board->is_pcl727) {
			if (mask & 0x00ff)
				outb(s->state & 0xff, io + PCL727_DO_LSB_REG);
			if (mask & 0xff00)
				outb((s->state >> 8), io + PCL727_DO_MSB_REG);
		} else {
			if (mask & 0x00ff)
				outb(s->state & 0xff, io + PCL726_DO_LSB_REG);
			if (mask & 0xff00)
				outb((s->state >> 8), io + PCL726_DO_MSB_REG);
		}
	}

	data[1] = s->state;

	return insn->n;
}

static int pcl726_attach(struct comedi_device *dev,
			 struct comedi_devconfig *it)
{
	const struct pcl726_board *board = dev->board_ptr;
	struct pcl726_private *devpriv;
	struct comedi_subdevice *s;
	int subdev;
	int ret;
	int i;

	ret = comedi_request_region(dev, it->options[0], board->io_len);
	if (ret)
		return ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	/*
	 * Hook up the external trigger source interrupt only if the
	 * user config option is valid and the board supports interrupts.
	 */
	if (it->options[1] > 0 && it->options[1] < 16 &&
	    (board->irq_mask & (1U << it->options[1]))) {
		ret = request_irq(it->options[1], pcl726_interrupt, 0,
				  dev->board_name, dev);
		if (ret == 0) {
			/* External trigger source is from Pin-17 of CN3 */
			dev->irq = it->options[1];
		}
	}

	/* setup the per-channel analog output range_table_list */
	for (i = 0; i < 12; i++) {
		unsigned int opt = it->options[2 + i];

		if (opt < board->ao_num_ranges && i < board->ao_nchan)
			devpriv->rangelist[i] = board->ao_ranges[opt];
		else
			devpriv->rangelist[i] = &range_unknown;
	}

	subdev = board->have_dio ? 3 : 1;
	if (dev->irq)
		subdev++;
	ret = comedi_alloc_subdevices(dev, subdev);
	if (ret)
		return ret;

	subdev = 0;

	/* Analog Output subdevice */
	s = &dev->subdevices[subdev++];
	s->type		= COMEDI_SUBD_AO;
	s->subdev_flags	= SDF_WRITABLE | SDF_GROUND;
	s->n_chan	= board->ao_nchan;
	s->maxdata	= 0x0fff;
	s->range_table_list = devpriv->rangelist;
	s->insn_write	= pcl726_ao_insn_write;

	ret = comedi_alloc_subdev_readback(s);
	if (ret)
		return ret;

	if (board->have_dio) {
		/* Digital Input subdevice */
		s = &dev->subdevices[subdev++];
		s->type		= COMEDI_SUBD_DI;
		s->subdev_flags	= SDF_READABLE;
		s->n_chan	= 16;
		s->maxdata	= 1;
		s->insn_bits	= pcl726_di_insn_bits;
		s->range_table	= &range_digital;

		/* Digital Output subdevice */
		s = &dev->subdevices[subdev++];
		s->type		= COMEDI_SUBD_DO;
		s->subdev_flags	= SDF_WRITABLE;
		s->n_chan	= 16;
		s->maxdata	= 1;
		s->insn_bits	= pcl726_do_insn_bits;
		s->range_table	= &range_digital;
	}

	if (dev->irq) {
		/* Digital Input subdevice - Interrupt support */
		s = &dev->subdevices[subdev++];
		dev->read_subdev = s;
		s->type		= COMEDI_SUBD_DI;
		s->subdev_flags	= SDF_READABLE | SDF_CMD_READ;
		s->n_chan	= 1;
		s->maxdata	= 1;
		s->range_table	= &range_digital;
		s->insn_bits	= pcl726_intr_insn_bits;
		s->len_chanlist	= 1;
		s->do_cmdtest	= pcl726_intr_cmdtest;
		s->do_cmd	= pcl726_intr_cmd;
		s->cancel	= pcl726_intr_cancel;
	}

	return 0;
}

static struct comedi_driver pcl726_driver = {
	.driver_name	= "pcl726",
	.module		= THIS_MODULE,
	.attach		= pcl726_attach,
	.detach		= comedi_legacy_detach,
	.board_name	= &pcl726_boards[0].name,
	.num_names	= ARRAY_SIZE(pcl726_boards),
	.offset		= sizeof(struct pcl726_board),
};
module_comedi_driver(pcl726_driver);

MODULE_AUTHOR("Comedi https://www.comedi.org");
MODULE_DESCRIPTION("Comedi driver for Advantech PCL-726 & compatibles");
MODULE_LICENSE("GPL");
