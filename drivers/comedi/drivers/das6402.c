// SPDX-License-Identifier: GPL-2.0+
/*
 * das6402.c
 * Comedi driver for DAS6402 compatible boards
 * Copyright(c) 2014 H Hartley Sweeten <hsweeten@visionengravers.com>
 *
 * Rewrite of an experimental driver by:
 * Copyright (C) 1999 Oystein Svendsen <svendsen@pvv.org>
 */

/*
 * Driver: das6402
 * Description: Keithley Metrabyte DAS6402 (& compatibles)
 * Devices: [Keithley Metrabyte] DAS6402-12 (das6402-12),
 *   DAS6402-16 (das6402-16)
 * Author: H Hartley Sweeten <hsweeten@visionengravers.com>
 * Updated: Fri, 14 Mar 2014 10:18:43 -0700
 * Status: unknown
 *
 * Configuration Options:
 *   [0] - I/O base address
 *   [1] - IRQ (optional, needed for async command support)
 */

#include <linux/module.h>
#include <linux/interrupt.h>

#include "../comedidev.h"

#include "comedi_8254.h"

/*
 * Register I/O map
 */
#define DAS6402_AI_DATA_REG		0x00
#define DAS6402_AI_MUX_REG		0x02
#define DAS6402_AI_MUX_LO(x)		(((x) & 0x3f) << 0)
#define DAS6402_AI_MUX_HI(x)		(((x) & 0x3f) << 8)
#define DAS6402_DI_DO_REG		0x03
#define DAS6402_AO_DATA_REG(x)		(0x04 + ((x) * 2))
#define DAS6402_AO_LSB_REG(x)		(0x04 + ((x) * 2))
#define DAS6402_AO_MSB_REG(x)		(0x05 + ((x) * 2))
#define DAS6402_STATUS_REG		0x08
#define DAS6402_STATUS_FFNE		BIT(0)
#define DAS6402_STATUS_FHALF		BIT(1)
#define DAS6402_STATUS_FFULL		BIT(2)
#define DAS6402_STATUS_XINT		BIT(3)
#define DAS6402_STATUS_INT		BIT(4)
#define DAS6402_STATUS_XTRIG		BIT(5)
#define DAS6402_STATUS_INDGT		BIT(6)
#define DAS6402_STATUS_10MHZ		BIT(7)
#define DAS6402_STATUS_W_CLRINT		BIT(0)
#define DAS6402_STATUS_W_CLRXTR		BIT(1)
#define DAS6402_STATUS_W_CLRXIN		BIT(2)
#define DAS6402_STATUS_W_EXTEND		BIT(4)
#define DAS6402_STATUS_W_ARMED		BIT(5)
#define DAS6402_STATUS_W_POSTMODE	BIT(6)
#define DAS6402_STATUS_W_10MHZ		BIT(7)
#define DAS6402_CTRL_REG		0x09
#define DAS6402_CTRL_TRIG(x)		((x) << 0)
#define DAS6402_CTRL_SOFT_TRIG		DAS6402_CTRL_TRIG(0)
#define DAS6402_CTRL_EXT_FALL_TRIG	DAS6402_CTRL_TRIG(1)
#define DAS6402_CTRL_EXT_RISE_TRIG	DAS6402_CTRL_TRIG(2)
#define DAS6402_CTRL_PACER_TRIG		DAS6402_CTRL_TRIG(3)
#define DAS6402_CTRL_BURSTEN		BIT(2)
#define DAS6402_CTRL_XINTE		BIT(3)
#define DAS6402_CTRL_IRQ(x)		((x) << 4)
#define DAS6402_CTRL_INTE		BIT(7)
#define DAS6402_TRIG_REG		0x0a
#define DAS6402_TRIG_TGEN		BIT(0)
#define DAS6402_TRIG_TGSEL		BIT(1)
#define DAS6402_TRIG_TGPOL		BIT(2)
#define DAS6402_TRIG_PRETRIG		BIT(3)
#define DAS6402_AO_RANGE(_chan, _range)	((_range) << ((_chan) ? 6 : 4))
#define DAS6402_AO_RANGE_MASK(_chan)	(3 << ((_chan) ? 6 : 4))
#define DAS6402_MODE_REG		0x0b
#define DAS6402_MODE_RANGE(x)		((x) << 2)
#define DAS6402_MODE_POLLED		DAS6402_MODE_RANGE(0)
#define DAS6402_MODE_FIFONEPTY		DAS6402_MODE_RANGE(1)
#define DAS6402_MODE_FIFOHFULL		DAS6402_MODE_RANGE(2)
#define DAS6402_MODE_EOB		DAS6402_MODE_RANGE(3)
#define DAS6402_MODE_ENHANCED		BIT(4)
#define DAS6402_MODE_SE			BIT(5)
#define DAS6402_MODE_UNI		BIT(6)
#define DAS6402_MODE_DMA(x)		((x) << 7)
#define DAS6402_MODE_DMA1		DAS6402_MODE_DMA(0)
#define DAS6402_MODE_DMA3		DAS6402_MODE_DMA(1)
#define DAS6402_TIMER_BASE		0x0c

static const struct comedi_lrange das6402_ai_ranges = {
	8, {
		BIP_RANGE(10),
		BIP_RANGE(5),
		BIP_RANGE(2.5),
		BIP_RANGE(1.25),
		UNI_RANGE(10),
		UNI_RANGE(5),
		UNI_RANGE(2.5),
		UNI_RANGE(1.25)
	}
};

/*
 * Analog output ranges are programmable on the DAS6402/12.
 * For the DAS6402/16 the range bits have no function, the
 * DAC ranges are selected by switches on the board.
 */
static const struct comedi_lrange das6402_ao_ranges = {
	4, {
		BIP_RANGE(5),
		BIP_RANGE(10),
		UNI_RANGE(5),
		UNI_RANGE(10)
	}
};

struct das6402_boardinfo {
	const char *name;
	unsigned int maxdata;
};

static struct das6402_boardinfo das6402_boards[] = {
	{
		.name		= "das6402-12",
		.maxdata	= 0x0fff,
	}, {
		.name		= "das6402-16",
		.maxdata	= 0xffff,
	},
};

struct das6402_private {
	unsigned int irq;
	unsigned int ao_range;
};

static void das6402_set_mode(struct comedi_device *dev,
			     unsigned int mode)
{
	outb(DAS6402_MODE_ENHANCED | mode, dev->iobase + DAS6402_MODE_REG);
}

static void das6402_set_extended(struct comedi_device *dev,
				 unsigned int val)
{
	outb(DAS6402_STATUS_W_EXTEND, dev->iobase + DAS6402_STATUS_REG);
	outb(DAS6402_STATUS_W_EXTEND | val, dev->iobase + DAS6402_STATUS_REG);
	outb(val, dev->iobase + DAS6402_STATUS_REG);
}

static void das6402_clear_all_interrupts(struct comedi_device *dev)
{
	outb(DAS6402_STATUS_W_CLRINT |
	     DAS6402_STATUS_W_CLRXTR |
	     DAS6402_STATUS_W_CLRXIN, dev->iobase + DAS6402_STATUS_REG);
}

static void das6402_ai_clear_eoc(struct comedi_device *dev)
{
	outb(DAS6402_STATUS_W_CLRINT, dev->iobase + DAS6402_STATUS_REG);
}

static unsigned int das6402_ai_read_sample(struct comedi_device *dev,
					   struct comedi_subdevice *s)
{
	unsigned int val;

	val = inw(dev->iobase + DAS6402_AI_DATA_REG);
	if (s->maxdata == 0x0fff)
		val >>= 4;
	return val;
}

static irqreturn_t das6402_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct comedi_subdevice *s = dev->read_subdev;
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;
	unsigned int status;

	status = inb(dev->iobase + DAS6402_STATUS_REG);
	if ((status & DAS6402_STATUS_INT) == 0)
		return IRQ_NONE;

	if (status & DAS6402_STATUS_FFULL) {
		async->events |= COMEDI_CB_OVERFLOW;
	} else if (status & DAS6402_STATUS_FFNE) {
		unsigned short val;

		val = das6402_ai_read_sample(dev, s);
		comedi_buf_write_samples(s, &val, 1);

		if (cmd->stop_src == TRIG_COUNT &&
		    async->scans_done >= cmd->stop_arg)
			async->events |= COMEDI_CB_EOA;
	}

	das6402_clear_all_interrupts(dev);

	comedi_handle_events(dev, s);

	return IRQ_HANDLED;
}

static void das6402_ai_set_mode(struct comedi_device *dev,
				struct comedi_subdevice *s,
				unsigned int chanspec,
				unsigned int mode)
{
	unsigned int range = CR_RANGE(chanspec);
	unsigned int aref = CR_AREF(chanspec);

	mode |= DAS6402_MODE_RANGE(range);
	if (aref == AREF_GROUND)
		mode |= DAS6402_MODE_SE;
	if (comedi_range_is_unipolar(s, range))
		mode |= DAS6402_MODE_UNI;

	das6402_set_mode(dev, mode);
}

static int das6402_ai_cmd(struct comedi_device *dev,
			  struct comedi_subdevice *s)
{
	struct das6402_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned int chan_lo = CR_CHAN(cmd->chanlist[0]);
	unsigned int chan_hi = CR_CHAN(cmd->chanlist[cmd->chanlist_len - 1]);

	das6402_ai_set_mode(dev, s, cmd->chanlist[0], DAS6402_MODE_FIFONEPTY);

	/* load the mux for chanlist conversion */
	outw(DAS6402_AI_MUX_HI(chan_hi) | DAS6402_AI_MUX_LO(chan_lo),
	     dev->iobase + DAS6402_AI_MUX_REG);

	comedi_8254_update_divisors(dev->pacer);
	comedi_8254_pacer_enable(dev->pacer, 1, 2, true);

	/* enable interrupt and pacer trigger */
	outb(DAS6402_CTRL_INTE |
	     DAS6402_CTRL_IRQ(devpriv->irq) |
	     DAS6402_CTRL_PACER_TRIG, dev->iobase + DAS6402_CTRL_REG);

	return 0;
}

static int das6402_ai_check_chanlist(struct comedi_device *dev,
				     struct comedi_subdevice *s,
				     struct comedi_cmd *cmd)
{
	unsigned int chan0 = CR_CHAN(cmd->chanlist[0]);
	unsigned int range0 = CR_RANGE(cmd->chanlist[0]);
	unsigned int aref0 = CR_AREF(cmd->chanlist[0]);
	int i;

	for (i = 1; i < cmd->chanlist_len; i++) {
		unsigned int chan = CR_CHAN(cmd->chanlist[i]);
		unsigned int range = CR_RANGE(cmd->chanlist[i]);
		unsigned int aref = CR_AREF(cmd->chanlist[i]);

		if (chan != chan0 + i) {
			dev_dbg(dev->class_dev,
				"chanlist must be consecutive\n");
			return -EINVAL;
		}

		if (range != range0) {
			dev_dbg(dev->class_dev,
				"chanlist must have the same range\n");
			return -EINVAL;
		}

		if (aref != aref0) {
			dev_dbg(dev->class_dev,
				"chanlist must have the same reference\n");
			return -EINVAL;
		}

		if (aref0 == AREF_DIFF && chan > (s->n_chan / 2)) {
			dev_dbg(dev->class_dev,
				"chanlist differential channel too large\n");
			return -EINVAL;
		}
	}
	return 0;
}

static int das6402_ai_cmdtest(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_cmd *cmd)
{
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
	err |= comedi_check_trigger_arg_min(&cmd->convert_arg, 10000);
	err |= comedi_check_trigger_arg_min(&cmd->chanlist_len, 1);
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
	comedi_8254_cascade_ns_to_timer(dev->pacer, &arg, cmd->flags);
	err |= comedi_check_trigger_arg_is(&cmd->convert_arg, arg);

	if (err)
		return 4;

	/* Step 5: check channel list if it exists */
	if (cmd->chanlist && cmd->chanlist_len > 0)
		err |= das6402_ai_check_chanlist(dev, s, cmd);

	if (err)
		return 5;

	return 0;
}

static int das6402_ai_cancel(struct comedi_device *dev,
			     struct comedi_subdevice *s)
{
	outb(DAS6402_CTRL_SOFT_TRIG, dev->iobase + DAS6402_CTRL_REG);

	return 0;
}

static void das6402_ai_soft_trig(struct comedi_device *dev)
{
	outw(0, dev->iobase + DAS6402_AI_DATA_REG);
}

static int das6402_ai_eoc(struct comedi_device *dev,
			  struct comedi_subdevice *s,
			  struct comedi_insn *insn,
			  unsigned long context)
{
	unsigned int status;

	status = inb(dev->iobase + DAS6402_STATUS_REG);
	if (status & DAS6402_STATUS_FFNE)
		return 0;
	return -EBUSY;
}

static int das6402_ai_insn_read(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int aref = CR_AREF(insn->chanspec);
	int ret;
	int i;

	if (aref == AREF_DIFF && chan > (s->n_chan / 2))
		return -EINVAL;

	/* enable software conversion trigger */
	outb(DAS6402_CTRL_SOFT_TRIG, dev->iobase + DAS6402_CTRL_REG);

	das6402_ai_set_mode(dev, s, insn->chanspec, DAS6402_MODE_POLLED);

	/* load the mux for single channel conversion */
	outw(DAS6402_AI_MUX_HI(chan) | DAS6402_AI_MUX_LO(chan),
	     dev->iobase + DAS6402_AI_MUX_REG);

	for (i = 0; i < insn->n; i++) {
		das6402_ai_clear_eoc(dev);
		das6402_ai_soft_trig(dev);

		ret = comedi_timeout(dev, s, insn, das6402_ai_eoc, 0);
		if (ret)
			break;

		data[i] = das6402_ai_read_sample(dev, s);
	}

	das6402_ai_clear_eoc(dev);

	return insn->n;
}

static int das6402_ao_insn_write(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	struct das6402_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int range = CR_RANGE(insn->chanspec);
	unsigned int val;
	int i;

	/* set the range for this channel */
	val = devpriv->ao_range;
	val &= ~DAS6402_AO_RANGE_MASK(chan);
	val |= DAS6402_AO_RANGE(chan, range);
	if (val != devpriv->ao_range) {
		devpriv->ao_range = val;
		outb(val, dev->iobase + DAS6402_TRIG_REG);
	}

	/*
	 * The DAS6402/16 has a jumper to select either individual
	 * update (UPDATE) or simultaneous updating (XFER) of both
	 * DAC's. In UPDATE mode, when the MSB is written, that DAC
	 * is updated. In XFER mode, after both DAC's are loaded,
	 * a read cycle of any DAC register will update both DAC's
	 * simultaneously.
	 *
	 * If you have XFER mode enabled a (*insn_read) will need
	 * to be performed in order to update the DAC's with the
	 * last value written.
	 */
	for (i = 0; i < insn->n; i++) {
		val = data[i];

		s->readback[chan] = val;

		if (s->maxdata == 0x0fff) {
			/*
			 * DAS6402/12 has the two 8-bit DAC registers, left
			 * justified (the 4 LSB bits are don't care). Data
			 * can be written as one word.
			 */
			val <<= 4;
			outw(val, dev->iobase + DAS6402_AO_DATA_REG(chan));
		} else {
			/*
			 * DAS6402/16 uses both 8-bit DAC registers and needs
			 * to be written LSB then MSB.
			 */
			outb(val & 0xff,
			     dev->iobase + DAS6402_AO_LSB_REG(chan));
			outb((val >> 8) & 0xff,
			     dev->iobase + DAS6402_AO_LSB_REG(chan));
		}
	}

	return insn->n;
}

static int das6402_ao_insn_read(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);

	/*
	 * If XFER mode is enabled, reading any DAC register
	 * will update both DAC's simultaneously.
	 */
	inw(dev->iobase + DAS6402_AO_LSB_REG(chan));

	return comedi_readback_insn_read(dev, s, insn, data);
}

static int das6402_di_insn_bits(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	data[1] = inb(dev->iobase + DAS6402_DI_DO_REG);

	return insn->n;
}

static int das6402_do_insn_bits(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	if (comedi_dio_update_state(s, data))
		outb(s->state, dev->iobase + DAS6402_DI_DO_REG);

	data[1] = s->state;

	return insn->n;
}

static void das6402_reset(struct comedi_device *dev)
{
	struct das6402_private *devpriv = dev->private;

	/* enable "Enhanced" mode */
	outb(DAS6402_MODE_ENHANCED, dev->iobase + DAS6402_MODE_REG);

	/* enable 10MHz pacer clock */
	das6402_set_extended(dev, DAS6402_STATUS_W_10MHZ);

	/* enable software conversion trigger */
	outb(DAS6402_CTRL_SOFT_TRIG, dev->iobase + DAS6402_CTRL_REG);

	/* default ADC to single-ended unipolar 10V inputs */
	das6402_set_mode(dev, DAS6402_MODE_RANGE(0) |
			      DAS6402_MODE_POLLED |
			      DAS6402_MODE_SE |
			      DAS6402_MODE_UNI);

	/* default mux for single channel conversion (channel 0) */
	outw(DAS6402_AI_MUX_HI(0) | DAS6402_AI_MUX_LO(0),
	     dev->iobase + DAS6402_AI_MUX_REG);

	/* set both DAC's for unipolar 5V output range */
	devpriv->ao_range = DAS6402_AO_RANGE(0, 2) | DAS6402_AO_RANGE(1, 2);
	outb(devpriv->ao_range, dev->iobase + DAS6402_TRIG_REG);

	/* set both DAC's to 0V */
	outw(0, dev->iobase + DAS6402_AO_DATA_REG(0));
	outw(0, dev->iobase + DAS6402_AO_DATA_REG(0));
	inw(dev->iobase + DAS6402_AO_LSB_REG(0));

	/* set all digital outputs low */
	outb(0, dev->iobase + DAS6402_DI_DO_REG);

	das6402_clear_all_interrupts(dev);
}

static int das6402_attach(struct comedi_device *dev,
			  struct comedi_devconfig *it)
{
	const struct das6402_boardinfo *board = dev->board_ptr;
	struct das6402_private *devpriv;
	struct comedi_subdevice *s;
	int ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	ret = comedi_request_region(dev, it->options[0], 0x10);
	if (ret)
		return ret;

	das6402_reset(dev);

	/* IRQs 2,3,5,6,7, 10,11,15 are valid for "enhanced" mode */
	if ((1 << it->options[1]) & 0x8cec) {
		ret = request_irq(it->options[1], das6402_interrupt, 0,
				  dev->board_name, dev);
		if (ret == 0) {
			dev->irq = it->options[1];

			switch (dev->irq) {
			case 10:
				devpriv->irq = 4;
				break;
			case 11:
				devpriv->irq = 1;
				break;
			case 15:
				devpriv->irq = 6;
				break;
			default:
				devpriv->irq = dev->irq;
				break;
			}
		}
	}

	dev->pacer = comedi_8254_init(dev->iobase + DAS6402_TIMER_BASE,
				      I8254_OSC_BASE_10MHZ, I8254_IO8, 0);
	if (!dev->pacer)
		return -ENOMEM;

	ret = comedi_alloc_subdevices(dev, 4);
	if (ret)
		return ret;

	/* Analog Input subdevice */
	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_AI;
	s->subdev_flags	= SDF_READABLE | SDF_GROUND | SDF_DIFF;
	s->n_chan	= 64;
	s->maxdata	= board->maxdata;
	s->range_table	= &das6402_ai_ranges;
	s->insn_read	= das6402_ai_insn_read;
	if (dev->irq) {
		dev->read_subdev = s;
		s->subdev_flags	|= SDF_CMD_READ;
		s->len_chanlist	= s->n_chan;
		s->do_cmdtest	= das6402_ai_cmdtest;
		s->do_cmd	= das6402_ai_cmd;
		s->cancel	= das6402_ai_cancel;
	}

	/* Analog Output subdevice */
	s = &dev->subdevices[1];
	s->type		= COMEDI_SUBD_AO;
	s->subdev_flags	= SDF_WRITABLE;
	s->n_chan	= 2;
	s->maxdata	= board->maxdata;
	s->range_table	= &das6402_ao_ranges;
	s->insn_write	= das6402_ao_insn_write;
	s->insn_read	= das6402_ao_insn_read;

	ret = comedi_alloc_subdev_readback(s);
	if (ret)
		return ret;

	/* Digital Input subdevice */
	s = &dev->subdevices[2];
	s->type		= COMEDI_SUBD_DI;
	s->subdev_flags	= SDF_READABLE;
	s->n_chan	= 8;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= das6402_di_insn_bits;

	/* Digital Input subdevice */
	s = &dev->subdevices[3];
	s->type		= COMEDI_SUBD_DO;
	s->subdev_flags	= SDF_WRITABLE;
	s->n_chan	= 8;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= das6402_do_insn_bits;

	return 0;
}

static struct comedi_driver das6402_driver = {
	.driver_name	= "das6402",
	.module		= THIS_MODULE,
	.attach		= das6402_attach,
	.detach		= comedi_legacy_detach,
	.board_name	= &das6402_boards[0].name,
	.num_names	= ARRAY_SIZE(das6402_boards),
	.offset		= sizeof(struct das6402_boardinfo),
};
module_comedi_driver(das6402_driver)

MODULE_AUTHOR("H Hartley Sweeten <hsweeten@visionengravers.com>");
MODULE_DESCRIPTION("Comedi driver for DAS6402 compatible boards");
MODULE_LICENSE("GPL");
