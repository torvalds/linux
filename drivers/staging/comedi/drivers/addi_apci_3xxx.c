/*
 * addi_apci_3xxx.c
 * Copyright (C) 2004,2005  ADDI-DATA GmbH for the source code of this module.
 * Project manager: S. Weber
 *
 *	ADDI-DATA GmbH
 *	Dieselstrasse 3
 *	D-77833 Ottersweier
 *	Tel: +19(0)7223/9493-0
 *	Fax: +49(0)7223/9493-92
 *	http://www.addi-data.com
 *	info@addi-data.com
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 */

#include <linux/module.h>
#include <linux/interrupt.h>

#include "../comedi_pci.h"

#define CONV_UNIT_NS		BIT(0)
#define CONV_UNIT_US		BIT(1)
#define CONV_UNIT_MS		BIT(2)

static const struct comedi_lrange apci3xxx_ai_range = {
	8, {
		BIP_RANGE(10),
		BIP_RANGE(5),
		BIP_RANGE(2),
		BIP_RANGE(1),
		UNI_RANGE(10),
		UNI_RANGE(5),
		UNI_RANGE(2),
		UNI_RANGE(1)
	}
};

static const struct comedi_lrange apci3xxx_ao_range = {
	2, {
		BIP_RANGE(10),
		UNI_RANGE(10)
	}
};

enum apci3xxx_boardid {
	BOARD_APCI3000_16,
	BOARD_APCI3000_8,
	BOARD_APCI3000_4,
	BOARD_APCI3006_16,
	BOARD_APCI3006_8,
	BOARD_APCI3006_4,
	BOARD_APCI3010_16,
	BOARD_APCI3010_8,
	BOARD_APCI3010_4,
	BOARD_APCI3016_16,
	BOARD_APCI3016_8,
	BOARD_APCI3016_4,
	BOARD_APCI3100_16_4,
	BOARD_APCI3100_8_4,
	BOARD_APCI3106_16_4,
	BOARD_APCI3106_8_4,
	BOARD_APCI3110_16_4,
	BOARD_APCI3110_8_4,
	BOARD_APCI3116_16_4,
	BOARD_APCI3116_8_4,
	BOARD_APCI3003,
	BOARD_APCI3002_16,
	BOARD_APCI3002_8,
	BOARD_APCI3002_4,
	BOARD_APCI3500,
};

struct apci3xxx_boardinfo {
	const char *name;
	int ai_subdev_flags;
	int ai_n_chan;
	unsigned int ai_maxdata;
	unsigned char ai_conv_units;
	unsigned int ai_min_acq_ns;
	unsigned int has_ao:1;
	unsigned int has_dig_in:1;
	unsigned int has_dig_out:1;
	unsigned int has_ttl_io:1;
};

static const struct apci3xxx_boardinfo apci3xxx_boardtypes[] = {
	[BOARD_APCI3000_16] = {
		.name			= "apci3000-16",
		.ai_subdev_flags	= SDF_COMMON | SDF_GROUND | SDF_DIFF,
		.ai_n_chan		= 16,
		.ai_maxdata		= 0x0fff,
		.ai_conv_units		= CONV_UNIT_MS | CONV_UNIT_US,
		.ai_min_acq_ns		= 10000,
		.has_ttl_io		= 1,
	},
	[BOARD_APCI3000_8] = {
		.name			= "apci3000-8",
		.ai_subdev_flags	= SDF_COMMON | SDF_GROUND | SDF_DIFF,
		.ai_n_chan		= 8,
		.ai_maxdata		= 0x0fff,
		.ai_conv_units		= CONV_UNIT_MS | CONV_UNIT_US,
		.ai_min_acq_ns		= 10000,
		.has_ttl_io		= 1,
	},
	[BOARD_APCI3000_4] = {
		.name			= "apci3000-4",
		.ai_subdev_flags	= SDF_COMMON | SDF_GROUND | SDF_DIFF,
		.ai_n_chan		= 4,
		.ai_maxdata		= 0x0fff,
		.ai_conv_units		= CONV_UNIT_MS | CONV_UNIT_US,
		.ai_min_acq_ns		= 10000,
		.has_ttl_io		= 1,
	},
	[BOARD_APCI3006_16] = {
		.name			= "apci3006-16",
		.ai_subdev_flags	= SDF_COMMON | SDF_GROUND | SDF_DIFF,
		.ai_n_chan		= 16,
		.ai_maxdata		= 0xffff,
		.ai_conv_units		= CONV_UNIT_MS | CONV_UNIT_US,
		.ai_min_acq_ns		= 10000,
		.has_ttl_io		= 1,
	},
	[BOARD_APCI3006_8] = {
		.name			= "apci3006-8",
		.ai_subdev_flags	= SDF_COMMON | SDF_GROUND | SDF_DIFF,
		.ai_n_chan		= 8,
		.ai_maxdata		= 0xffff,
		.ai_conv_units		= CONV_UNIT_MS | CONV_UNIT_US,
		.ai_min_acq_ns		= 10000,
		.has_ttl_io		= 1,
	},
	[BOARD_APCI3006_4] = {
		.name			= "apci3006-4",
		.ai_subdev_flags	= SDF_COMMON | SDF_GROUND | SDF_DIFF,
		.ai_n_chan		= 4,
		.ai_maxdata		= 0xffff,
		.ai_conv_units		= CONV_UNIT_MS | CONV_UNIT_US,
		.ai_min_acq_ns		= 10000,
		.has_ttl_io		= 1,
	},
	[BOARD_APCI3010_16] = {
		.name			= "apci3010-16",
		.ai_subdev_flags	= SDF_COMMON | SDF_GROUND | SDF_DIFF,
		.ai_n_chan		= 16,
		.ai_maxdata		= 0x0fff,
		.ai_conv_units		= CONV_UNIT_MS | CONV_UNIT_US,
		.ai_min_acq_ns		= 5000,
		.has_dig_in		= 1,
		.has_dig_out		= 1,
		.has_ttl_io		= 1,
	},
	[BOARD_APCI3010_8] = {
		.name			= "apci3010-8",
		.ai_subdev_flags	= SDF_COMMON | SDF_GROUND | SDF_DIFF,
		.ai_n_chan		= 8,
		.ai_maxdata		= 0x0fff,
		.ai_conv_units		= CONV_UNIT_MS | CONV_UNIT_US,
		.ai_min_acq_ns		= 5000,
		.has_dig_in		= 1,
		.has_dig_out		= 1,
		.has_ttl_io		= 1,
	},
	[BOARD_APCI3010_4] = {
		.name			= "apci3010-4",
		.ai_subdev_flags	= SDF_COMMON | SDF_GROUND | SDF_DIFF,
		.ai_n_chan		= 4,
		.ai_maxdata		= 0x0fff,
		.ai_conv_units		= CONV_UNIT_MS | CONV_UNIT_US,
		.ai_min_acq_ns		= 5000,
		.has_dig_in		= 1,
		.has_dig_out		= 1,
		.has_ttl_io		= 1,
	},
	[BOARD_APCI3016_16] = {
		.name			= "apci3016-16",
		.ai_subdev_flags	= SDF_COMMON | SDF_GROUND | SDF_DIFF,
		.ai_n_chan		= 16,
		.ai_maxdata		= 0xffff,
		.ai_conv_units		= CONV_UNIT_MS | CONV_UNIT_US,
		.ai_min_acq_ns		= 5000,
		.has_dig_in		= 1,
		.has_dig_out		= 1,
		.has_ttl_io		= 1,
	},
	[BOARD_APCI3016_8] = {
		.name			= "apci3016-8",
		.ai_subdev_flags	= SDF_COMMON | SDF_GROUND | SDF_DIFF,
		.ai_n_chan		= 8,
		.ai_maxdata		= 0xffff,
		.ai_conv_units		= CONV_UNIT_MS | CONV_UNIT_US,
		.ai_min_acq_ns		= 5000,
		.has_dig_in		= 1,
		.has_dig_out		= 1,
		.has_ttl_io		= 1,
	},
	[BOARD_APCI3016_4] = {
		.name			= "apci3016-4",
		.ai_subdev_flags	= SDF_COMMON | SDF_GROUND | SDF_DIFF,
		.ai_n_chan		= 4,
		.ai_maxdata		= 0xffff,
		.ai_conv_units		= CONV_UNIT_MS | CONV_UNIT_US,
		.ai_min_acq_ns		= 5000,
		.has_dig_in		= 1,
		.has_dig_out		= 1,
		.has_ttl_io		= 1,
	},
	[BOARD_APCI3100_16_4] = {
		.name			= "apci3100-16-4",
		.ai_subdev_flags	= SDF_COMMON | SDF_GROUND | SDF_DIFF,
		.ai_n_chan		= 16,
		.ai_maxdata		= 0x0fff,
		.ai_conv_units		= CONV_UNIT_MS | CONV_UNIT_US,
		.ai_min_acq_ns		= 10000,
		.has_ao			= 1,
		.has_ttl_io		= 1,
	},
	[BOARD_APCI3100_8_4] = {
		.name			= "apci3100-8-4",
		.ai_subdev_flags	= SDF_COMMON | SDF_GROUND | SDF_DIFF,
		.ai_n_chan		= 8,
		.ai_maxdata		= 0x0fff,
		.ai_conv_units		= CONV_UNIT_MS | CONV_UNIT_US,
		.ai_min_acq_ns		= 10000,
		.has_ao			= 1,
		.has_ttl_io		= 1,
	},
	[BOARD_APCI3106_16_4] = {
		.name			= "apci3106-16-4",
		.ai_subdev_flags	= SDF_COMMON | SDF_GROUND | SDF_DIFF,
		.ai_n_chan		= 16,
		.ai_maxdata		= 0xffff,
		.ai_conv_units		= CONV_UNIT_MS | CONV_UNIT_US,
		.ai_min_acq_ns		= 10000,
		.has_ao			= 1,
		.has_ttl_io		= 1,
	},
	[BOARD_APCI3106_8_4] = {
		.name			= "apci3106-8-4",
		.ai_subdev_flags	= SDF_COMMON | SDF_GROUND | SDF_DIFF,
		.ai_n_chan		= 8,
		.ai_maxdata		= 0xffff,
		.ai_conv_units		= CONV_UNIT_MS | CONV_UNIT_US,
		.ai_min_acq_ns		= 10000,
		.has_ao			= 1,
		.has_ttl_io		= 1,
	},
	[BOARD_APCI3110_16_4] = {
		.name			= "apci3110-16-4",
		.ai_subdev_flags	= SDF_COMMON | SDF_GROUND | SDF_DIFF,
		.ai_n_chan		= 16,
		.ai_maxdata		= 0x0fff,
		.ai_conv_units		= CONV_UNIT_MS | CONV_UNIT_US,
		.ai_min_acq_ns		= 5000,
		.has_ao			= 1,
		.has_dig_in		= 1,
		.has_dig_out		= 1,
		.has_ttl_io		= 1,
	},
	[BOARD_APCI3110_8_4] = {
		.name			= "apci3110-8-4",
		.ai_subdev_flags	= SDF_COMMON | SDF_GROUND | SDF_DIFF,
		.ai_n_chan		= 8,
		.ai_maxdata		= 0x0fff,
		.ai_conv_units		= CONV_UNIT_MS | CONV_UNIT_US,
		.ai_min_acq_ns		= 5000,
		.has_ao			= 1,
		.has_dig_in		= 1,
		.has_dig_out		= 1,
		.has_ttl_io		= 1,
	},
	[BOARD_APCI3116_16_4] = {
		.name			= "apci3116-16-4",
		.ai_subdev_flags	= SDF_COMMON | SDF_GROUND | SDF_DIFF,
		.ai_n_chan		= 16,
		.ai_maxdata		= 0xffff,
		.ai_conv_units		= CONV_UNIT_MS | CONV_UNIT_US,
		.ai_min_acq_ns		= 5000,
		.has_ao			= 1,
		.has_dig_in		= 1,
		.has_dig_out		= 1,
		.has_ttl_io		= 1,
	},
	[BOARD_APCI3116_8_4] = {
		.name			= "apci3116-8-4",
		.ai_subdev_flags	= SDF_COMMON | SDF_GROUND | SDF_DIFF,
		.ai_n_chan		= 8,
		.ai_maxdata		= 0xffff,
		.ai_conv_units		= CONV_UNIT_MS | CONV_UNIT_US,
		.ai_min_acq_ns		= 5000,
		.has_ao			= 1,
		.has_dig_in		= 1,
		.has_dig_out		= 1,
		.has_ttl_io		= 1,
	},
	[BOARD_APCI3003] = {
		.name			= "apci3003",
		.ai_subdev_flags	= SDF_DIFF,
		.ai_n_chan		= 4,
		.ai_maxdata		= 0xffff,
		.ai_conv_units		= CONV_UNIT_MS | CONV_UNIT_US |
					  CONV_UNIT_NS,
		.ai_min_acq_ns		= 2500,
		.has_dig_in		= 1,
		.has_dig_out		= 1,
	},
	[BOARD_APCI3002_16] = {
		.name			= "apci3002-16",
		.ai_subdev_flags	= SDF_DIFF,
		.ai_n_chan		= 16,
		.ai_maxdata		= 0xffff,
		.ai_conv_units		= CONV_UNIT_MS | CONV_UNIT_US,
		.ai_min_acq_ns		= 5000,
		.has_dig_in		= 1,
		.has_dig_out		= 1,
	},
	[BOARD_APCI3002_8] = {
		.name			= "apci3002-8",
		.ai_subdev_flags	= SDF_DIFF,
		.ai_n_chan		= 8,
		.ai_maxdata		= 0xffff,
		.ai_conv_units		= CONV_UNIT_MS | CONV_UNIT_US,
		.ai_min_acq_ns		= 5000,
		.has_dig_in		= 1,
		.has_dig_out		= 1,
	},
	[BOARD_APCI3002_4] = {
		.name			= "apci3002-4",
		.ai_subdev_flags	= SDF_DIFF,
		.ai_n_chan		= 4,
		.ai_maxdata		= 0xffff,
		.ai_conv_units		= CONV_UNIT_MS | CONV_UNIT_US,
		.ai_min_acq_ns		= 5000,
		.has_dig_in		= 1,
		.has_dig_out		= 1,
	},
	[BOARD_APCI3500] = {
		.name			= "apci3500",
		.has_ao			= 1,
		.has_ttl_io		= 1,
	},
};

struct apci3xxx_private {
	unsigned int ai_timer;
	unsigned char ai_time_base;
};

static irqreturn_t apci3xxx_irq_handler(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct comedi_subdevice *s = dev->read_subdev;
	unsigned int status;
	unsigned int val;

	/* Test if interrupt occur */
	status = readl(dev->mmio + 16);
	if ((status & 0x2) == 0x2) {
		/* Reset the interrupt */
		writel(status, dev->mmio + 16);

		val = readl(dev->mmio + 28);
		comedi_buf_write_samples(s, &val, 1);

		s->async->events |= COMEDI_CB_EOA;
		comedi_handle_events(dev, s);

		return IRQ_HANDLED;
	}
	return IRQ_NONE;
}

static int apci3xxx_ai_started(struct comedi_device *dev)
{
	if ((readl(dev->mmio + 8) & 0x80000) == 0x80000)
		return 1;

	return 0;
}

static int apci3xxx_ai_setup(struct comedi_device *dev, unsigned int chanspec)
{
	unsigned int chan = CR_CHAN(chanspec);
	unsigned int range = CR_RANGE(chanspec);
	unsigned int aref = CR_AREF(chanspec);
	unsigned int delay_mode;
	unsigned int val;

	if (apci3xxx_ai_started(dev))
		return -EBUSY;

	/* Clear the FIFO */
	writel(0x10000, dev->mmio + 12);

	/* Get and save the delay mode */
	delay_mode = readl(dev->mmio + 4);
	delay_mode &= 0xfffffef0;

	/* Channel configuration selection */
	writel(delay_mode, dev->mmio + 4);

	/* Make the configuration */
	val = (range & 3) | ((range >> 2) << 6) |
	      ((aref == AREF_DIFF) << 7);
	writel(val, dev->mmio + 0);

	/* Channel selection */
	writel(delay_mode | 0x100, dev->mmio + 4);
	writel(chan, dev->mmio + 0);

	/* Restore delay mode */
	writel(delay_mode, dev->mmio + 4);

	/* Set the number of sequence to 1 */
	writel(1, dev->mmio + 48);

	return 0;
}

static int apci3xxx_ai_eoc(struct comedi_device *dev,
			   struct comedi_subdevice *s,
			   struct comedi_insn *insn,
			   unsigned long context)
{
	unsigned int status;

	status = readl(dev->mmio + 20);
	if (status & 0x1)
		return 0;
	return -EBUSY;
}

static int apci3xxx_ai_insn_read(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	int ret;
	int i;

	ret = apci3xxx_ai_setup(dev, insn->chanspec);
	if (ret)
		return ret;

	for (i = 0; i < insn->n; i++) {
		/* Start the conversion */
		writel(0x80000, dev->mmio + 8);

		/* Wait the EOS */
		ret = comedi_timeout(dev, s, insn, apci3xxx_ai_eoc, 0);
		if (ret)
			return ret;

		/* Read the analog value */
		data[i] = readl(dev->mmio + 28);
	}

	return insn->n;
}

static int apci3xxx_ai_ns_to_timer(struct comedi_device *dev,
				   unsigned int *ns, unsigned int flags)
{
	const struct apci3xxx_boardinfo *board = dev->board_ptr;
	struct apci3xxx_private *devpriv = dev->private;
	unsigned int base;
	unsigned int timer;
	int time_base;

	/* time_base: 0 = ns, 1 = us, 2 = ms */
	for (time_base = 0; time_base < 3; time_base++) {
		/* skip unsupported time bases */
		if (!(board->ai_conv_units & (1 << time_base)))
			continue;

		switch (time_base) {
		case 0:
			base = 1;
			break;
		case 1:
			base = 1000;
			break;
		case 2:
			base = 1000000;
			break;
		}

		switch (flags & CMDF_ROUND_MASK) {
		case CMDF_ROUND_NEAREST:
		default:
			timer = DIV_ROUND_CLOSEST(*ns, base);
			break;
		case CMDF_ROUND_DOWN:
			timer = *ns / base;
			break;
		case CMDF_ROUND_UP:
			timer = (*ns + base - 1) / base;
			break;
		}

		if (timer < 0x10000) {
			devpriv->ai_time_base = time_base;
			devpriv->ai_timer = timer;
			*ns = timer * time_base;
			return 0;
		}
	}
	return -EINVAL;
}

static int apci3xxx_ai_cmdtest(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_cmd *cmd)
{
	const struct apci3xxx_boardinfo *board = dev->board_ptr;
	int err = 0;
	unsigned int arg;

	/* Step 1 : check if triggers are trivially valid */

	err |= comedi_check_trigger_src(&cmd->start_src, TRIG_NOW);
	err |= comedi_check_trigger_src(&cmd->scan_begin_src, TRIG_FOLLOW);
	err |= comedi_check_trigger_src(&cmd->convert_src, TRIG_TIMER);
	err |= comedi_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= comedi_check_trigger_src(&cmd->stop_src, TRIG_COUNT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= comedi_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	err |= comedi_check_trigger_arg_is(&cmd->start_arg, 0);
	err |= comedi_check_trigger_arg_is(&cmd->scan_begin_arg, 0);
	err |= comedi_check_trigger_arg_min(&cmd->convert_arg,
					    board->ai_min_acq_ns);
	err |= comedi_check_trigger_arg_is(&cmd->scan_end_arg,
					   cmd->chanlist_len);

	if (cmd->stop_src == TRIG_COUNT)
		err |= comedi_check_trigger_arg_min(&cmd->stop_arg, 1);
	else	/* TRIG_NONE */
		err |= comedi_check_trigger_arg_is(&cmd->stop_arg, 0);

	if (err)
		return 3;

	/* step 4: fix up any arguments */

	arg = cmd->convert_arg;
	err |= apci3xxx_ai_ns_to_timer(dev, &arg, cmd->flags);
	err |= comedi_check_trigger_arg_is(&cmd->convert_arg, arg);

	if (err)
		return 4;

	return 0;
}

static int apci3xxx_ai_cmd(struct comedi_device *dev,
			   struct comedi_subdevice *s)
{
	struct apci3xxx_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	int ret;

	ret = apci3xxx_ai_setup(dev, cmd->chanlist[0]);
	if (ret)
		return ret;

	/* Set the convert timing unit */
	writel(devpriv->ai_time_base, dev->mmio + 36);

	/* Set the convert timing */
	writel(devpriv->ai_timer, dev->mmio + 32);

	/* Start the conversion */
	writel(0x180000, dev->mmio + 8);

	return 0;
}

static int apci3xxx_ai_cancel(struct comedi_device *dev,
			      struct comedi_subdevice *s)
{
	return 0;
}

static int apci3xxx_ao_eoc(struct comedi_device *dev,
			   struct comedi_subdevice *s,
			   struct comedi_insn *insn,
			   unsigned long context)
{
	unsigned int status;

	status = readl(dev->mmio + 96);
	if (status & 0x100)
		return 0;
	return -EBUSY;
}

static int apci3xxx_ao_insn_write(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn,
				  unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int range = CR_RANGE(insn->chanspec);
	int ret;
	int i;

	for (i = 0; i < insn->n; i++) {
		unsigned int val = data[i];

		/* Set the range selection */
		writel(range, dev->mmio + 96);

		/* Write the analog value to the selected channel */
		writel((val << 8) | chan, dev->mmio + 100);

		/* Wait the end of transfer */
		ret = comedi_timeout(dev, s, insn, apci3xxx_ao_eoc, 0);
		if (ret)
			return ret;

		s->readback[chan] = val;
	}

	return insn->n;
}

static int apci3xxx_di_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	data[1] = inl(dev->iobase + 32) & 0xf;

	return insn->n;
}

static int apci3xxx_do_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	s->state = inl(dev->iobase + 48) & 0xf;

	if (comedi_dio_update_state(s, data))
		outl(s->state, dev->iobase + 48);

	data[1] = s->state;

	return insn->n;
}

static int apci3xxx_dio_insn_config(struct comedi_device *dev,
				    struct comedi_subdevice *s,
				    struct comedi_insn *insn,
				    unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int mask = 0;
	int ret;

	/*
	 * Port 0 (channels 0-7) are always inputs
	 * Port 1 (channels 8-15) are always outputs
	 * Port 2 (channels 16-23) are programmable i/o
	 */
	if (data[0] != INSN_CONFIG_DIO_QUERY) {
		/* ignore all other instructions for ports 0 and 1 */
		if (chan < 16)
			return -EINVAL;

		/* changing any channel in port 2 changes the entire port */
		mask = 0xff0000;
	}

	ret = comedi_dio_insn_config(dev, s, insn, data, mask);
	if (ret)
		return ret;

	/* update port 2 configuration */
	outl((s->io_bits >> 24) & 0xff, dev->iobase + 224);

	return insn->n;
}

static int apci3xxx_dio_insn_bits(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn,
				  unsigned int *data)
{
	unsigned int mask;
	unsigned int val;

	mask = comedi_dio_update_state(s, data);
	if (mask) {
		if (mask & 0xff)
			outl(s->state & 0xff, dev->iobase + 80);
		if (mask & 0xff0000)
			outl((s->state >> 16) & 0xff, dev->iobase + 112);
	}

	val = inl(dev->iobase + 80);
	val |= (inl(dev->iobase + 64) << 8);
	if (s->io_bits & 0xff0000)
		val |= (inl(dev->iobase + 112) << 16);
	else
		val |= (inl(dev->iobase + 96) << 16);

	data[1] = val;

	return insn->n;
}

static int apci3xxx_reset(struct comedi_device *dev)
{
	unsigned int val;
	int i;

	/* Disable the interrupt */
	disable_irq(dev->irq);

	/* Clear the start command */
	writel(0, dev->mmio + 8);

	/* Reset the interrupt flags */
	val = readl(dev->mmio + 16);
	writel(val, dev->mmio + 16);

	/* clear the EOS */
	readl(dev->mmio + 20);

	/* Clear the FIFO */
	for (i = 0; i < 16; i++)
		val = readl(dev->mmio + 28);

	/* Enable the interrupt */
	enable_irq(dev->irq);

	return 0;
}

static int apci3xxx_auto_attach(struct comedi_device *dev,
				unsigned long context)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct apci3xxx_boardinfo *board = NULL;
	struct apci3xxx_private *devpriv;
	struct comedi_subdevice *s;
	int n_subdevices;
	int subdev;
	int ret;

	if (context < ARRAY_SIZE(apci3xxx_boardtypes))
		board = &apci3xxx_boardtypes[context];
	if (!board)
		return -ENODEV;
	dev->board_ptr = board;
	dev->board_name = board->name;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;

	dev->iobase = pci_resource_start(pcidev, 2);
	dev->mmio = pci_ioremap_bar(pcidev, 3);

	if (pcidev->irq > 0) {
		ret = request_irq(pcidev->irq, apci3xxx_irq_handler,
				  IRQF_SHARED, dev->board_name, dev);
		if (ret == 0)
			dev->irq = pcidev->irq;
	}

	n_subdevices = (board->ai_n_chan ? 0 : 1) + board->has_ao +
		       board->has_dig_in + board->has_dig_out +
		       board->has_ttl_io;
	ret = comedi_alloc_subdevices(dev, n_subdevices);
	if (ret)
		return ret;

	subdev = 0;

	/* Analog Input subdevice */
	if (board->ai_n_chan) {
		s = &dev->subdevices[subdev];
		s->type		= COMEDI_SUBD_AI;
		s->subdev_flags	= SDF_READABLE | board->ai_subdev_flags;
		s->n_chan	= board->ai_n_chan;
		s->maxdata	= board->ai_maxdata;
		s->range_table	= &apci3xxx_ai_range;
		s->insn_read	= apci3xxx_ai_insn_read;
		if (dev->irq) {
			/*
			 * FIXME: The hardware supports multiple scan modes
			 * but the original addi-data driver only supported
			 * reading a single channel with interrupts. Need a
			 * proper datasheet to fix this.
			 *
			 * The following scan modes are supported by the
			 * hardware:
			 *   1) Single software scan
			 *   2) Single hardware triggered scan
			 *   3) Continuous software scan
			 *   4) Continuous software scan with timer delay
			 *   5) Continuous hardware triggered scan
			 *   6) Continuous hardware triggered scan with timer
			 *      delay
			 *
			 * For now, limit the chanlist to a single channel.
			 */
			dev->read_subdev = s;
			s->subdev_flags	|= SDF_CMD_READ;
			s->len_chanlist	= 1;
			s->do_cmdtest	= apci3xxx_ai_cmdtest;
			s->do_cmd	= apci3xxx_ai_cmd;
			s->cancel	= apci3xxx_ai_cancel;
		}

		subdev++;
	}

	/* Analog Output subdevice */
	if (board->has_ao) {
		s = &dev->subdevices[subdev];
		s->type		= COMEDI_SUBD_AO;
		s->subdev_flags	= SDF_WRITABLE | SDF_GROUND | SDF_COMMON;
		s->n_chan	= 4;
		s->maxdata	= 0x0fff;
		s->range_table	= &apci3xxx_ao_range;
		s->insn_write	= apci3xxx_ao_insn_write;

		ret = comedi_alloc_subdev_readback(s);
		if (ret)
			return ret;

		subdev++;
	}

	/* Digital Input subdevice */
	if (board->has_dig_in) {
		s = &dev->subdevices[subdev];
		s->type		= COMEDI_SUBD_DI;
		s->subdev_flags	= SDF_READABLE;
		s->n_chan	= 4;
		s->maxdata	= 1;
		s->range_table	= &range_digital;
		s->insn_bits	= apci3xxx_di_insn_bits;

		subdev++;
	}

	/* Digital Output subdevice */
	if (board->has_dig_out) {
		s = &dev->subdevices[subdev];
		s->type		= COMEDI_SUBD_DO;
		s->subdev_flags	= SDF_WRITABLE;
		s->n_chan	= 4;
		s->maxdata	= 1;
		s->range_table	= &range_digital;
		s->insn_bits	= apci3xxx_do_insn_bits;

		subdev++;
	}

	/* TTL Digital I/O subdevice */
	if (board->has_ttl_io) {
		s = &dev->subdevices[subdev];
		s->type		= COMEDI_SUBD_DIO;
		s->subdev_flags	= SDF_READABLE | SDF_WRITABLE;
		s->n_chan	= 24;
		s->maxdata	= 1;
		s->io_bits	= 0xff;	/* channels 0-7 are always outputs */
		s->range_table	= &range_digital;
		s->insn_config	= apci3xxx_dio_insn_config;
		s->insn_bits	= apci3xxx_dio_insn_bits;

		subdev++;
	}

	apci3xxx_reset(dev);
	return 0;
}

static void apci3xxx_detach(struct comedi_device *dev)
{
	if (dev->iobase)
		apci3xxx_reset(dev);
	comedi_pci_detach(dev);
}

static struct comedi_driver apci3xxx_driver = {
	.driver_name	= "addi_apci_3xxx",
	.module		= THIS_MODULE,
	.auto_attach	= apci3xxx_auto_attach,
	.detach		= apci3xxx_detach,
};

static int apci3xxx_pci_probe(struct pci_dev *dev,
			      const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &apci3xxx_driver, id->driver_data);
}

static const struct pci_device_id apci3xxx_pci_table[] = {
	{ PCI_VDEVICE(ADDIDATA, 0x3010), BOARD_APCI3000_16 },
	{ PCI_VDEVICE(ADDIDATA, 0x300f), BOARD_APCI3000_8 },
	{ PCI_VDEVICE(ADDIDATA, 0x300e), BOARD_APCI3000_4 },
	{ PCI_VDEVICE(ADDIDATA, 0x3013), BOARD_APCI3006_16 },
	{ PCI_VDEVICE(ADDIDATA, 0x3014), BOARD_APCI3006_8 },
	{ PCI_VDEVICE(ADDIDATA, 0x3015), BOARD_APCI3006_4 },
	{ PCI_VDEVICE(ADDIDATA, 0x3016), BOARD_APCI3010_16 },
	{ PCI_VDEVICE(ADDIDATA, 0x3017), BOARD_APCI3010_8 },
	{ PCI_VDEVICE(ADDIDATA, 0x3018), BOARD_APCI3010_4 },
	{ PCI_VDEVICE(ADDIDATA, 0x3019), BOARD_APCI3016_16 },
	{ PCI_VDEVICE(ADDIDATA, 0x301a), BOARD_APCI3016_8 },
	{ PCI_VDEVICE(ADDIDATA, 0x301b), BOARD_APCI3016_4 },
	{ PCI_VDEVICE(ADDIDATA, 0x301c), BOARD_APCI3100_16_4 },
	{ PCI_VDEVICE(ADDIDATA, 0x301d), BOARD_APCI3100_8_4 },
	{ PCI_VDEVICE(ADDIDATA, 0x301e), BOARD_APCI3106_16_4 },
	{ PCI_VDEVICE(ADDIDATA, 0x301f), BOARD_APCI3106_8_4 },
	{ PCI_VDEVICE(ADDIDATA, 0x3020), BOARD_APCI3110_16_4 },
	{ PCI_VDEVICE(ADDIDATA, 0x3021), BOARD_APCI3110_8_4 },
	{ PCI_VDEVICE(ADDIDATA, 0x3022), BOARD_APCI3116_16_4 },
	{ PCI_VDEVICE(ADDIDATA, 0x3023), BOARD_APCI3116_8_4 },
	{ PCI_VDEVICE(ADDIDATA, 0x300B), BOARD_APCI3003 },
	{ PCI_VDEVICE(ADDIDATA, 0x3002), BOARD_APCI3002_16 },
	{ PCI_VDEVICE(ADDIDATA, 0x3003), BOARD_APCI3002_8 },
	{ PCI_VDEVICE(ADDIDATA, 0x3004), BOARD_APCI3002_4 },
	{ PCI_VDEVICE(ADDIDATA, 0x3024), BOARD_APCI3500 },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, apci3xxx_pci_table);

static struct pci_driver apci3xxx_pci_driver = {
	.name		= "addi_apci_3xxx",
	.id_table	= apci3xxx_pci_table,
	.probe		= apci3xxx_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(apci3xxx_driver, apci3xxx_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
