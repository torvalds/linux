// SPDX-License-Identifier: GPL-2.0+
/*
 * dt3000.c
 * Data Translation DT3000 series driver
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 1999 David A. Schleef <ds@schleef.org>
 */

/*
 * Driver: dt3000
 * Description: Data Translation DT3000 series
 * Devices: [Data Translation] DT3001 (dt3000), DT3001-PGL, DT3002, DT3003,
 *   DT3003-PGL, DT3004, DT3005, DT3004-200
 * Author: ds
 * Updated: Mon, 14 Apr 2008 15:41:24 +0100
 * Status: works
 *
 * Configuration Options: not applicable, uses PCI auto config
 *
 * There is code to support AI commands, but it may not work.
 *
 * AO commands are not supported.
 */

/*
 * The DT3000 series is Data Translation's attempt to make a PCI
 * data acquisition board.  The design of this series is very nice,
 * since each board has an on-board DSP (Texas Instruments TMS320C52).
 * However, a few details are a little annoying.  The boards lack
 * bus-mastering DMA, which eliminates them from serious work.
 * They also are not capable of autocalibration, which is a common
 * feature in modern hardware.  The default firmware is pretty bad,
 * making it nearly impossible to write an RT compatible driver.
 * It would make an interesting project to write a decent firmware
 * for these boards.
 *
 * Data Translation originally wanted an NDA for the documentation
 * for the 3k series.  However, if you ask nicely, they might send
 * you the docs without one, also.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/interrupt.h>

#include "../comedi_pci.h"

/*
 * PCI BAR0 - dual-ported RAM location definitions (dev->mmio)
 */
#define DPR_DAC_BUFFER		(4 * 0x000)
#define DPR_ADC_BUFFER		(4 * 0x800)
#define DPR_COMMAND		(4 * 0xfd3)
#define DPR_SUBSYS		(4 * 0xfd3)
#define DPR_SUBSYS_AI		0
#define DPR_SUBSYS_AO		1
#define DPR_SUBSYS_DIN		2
#define DPR_SUBSYS_DOUT		3
#define DPR_SUBSYS_MEM		4
#define DPR_SUBSYS_CT		5
#define DPR_ENCODE		(4 * 0xfd4)
#define DPR_PARAMS(x)		(4 * (0xfd5 + (x)))
#define DPR_TICK_REG_LO		(4 * 0xff5)
#define DPR_TICK_REG_HI		(4 * 0xff6)
#define DPR_DA_BUF_FRONT	(4 * 0xff7)
#define DPR_DA_BUF_REAR		(4 * 0xff8)
#define DPR_AD_BUF_FRONT	(4 * 0xff9)
#define DPR_AD_BUF_REAR		(4 * 0xffa)
#define DPR_INT_MASK		(4 * 0xffb)
#define DPR_INTR_FLAG		(4 * 0xffc)
#define DPR_INTR_CMDONE		BIT(7)
#define DPR_INTR_CTDONE		BIT(6)
#define DPR_INTR_DAHWERR	BIT(5)
#define DPR_INTR_DASWERR	BIT(4)
#define DPR_INTR_DAEMPTY	BIT(3)
#define DPR_INTR_ADHWERR	BIT(2)
#define DPR_INTR_ADSWERR	BIT(1)
#define DPR_INTR_ADFULL		BIT(0)
#define DPR_RESPONSE_MBX	(4 * 0xffe)
#define DPR_CMD_MBX		(4 * 0xfff)
#define DPR_CMD_COMPLETION(x)	((x) << 8)
#define DPR_CMD_NOTPROCESSED	DPR_CMD_COMPLETION(0x00)
#define DPR_CMD_NOERROR		DPR_CMD_COMPLETION(0x55)
#define DPR_CMD_ERROR		DPR_CMD_COMPLETION(0xaa)
#define DPR_CMD_NOTSUPPORTED	DPR_CMD_COMPLETION(0xff)
#define DPR_CMD_COMPLETION_MASK	DPR_CMD_COMPLETION(0xff)
#define DPR_CMD(x)		((x) << 0)
#define DPR_CMD_GETBRDINFO	DPR_CMD(0)
#define DPR_CMD_CONFIG		DPR_CMD(1)
#define DPR_CMD_GETCONFIG	DPR_CMD(2)
#define DPR_CMD_START		DPR_CMD(3)
#define DPR_CMD_STOP		DPR_CMD(4)
#define DPR_CMD_READSINGLE	DPR_CMD(5)
#define DPR_CMD_WRITESINGLE	DPR_CMD(6)
#define DPR_CMD_CALCCLOCK	DPR_CMD(7)
#define DPR_CMD_READEVENTS	DPR_CMD(8)
#define DPR_CMD_WRITECTCTRL	DPR_CMD(16)
#define DPR_CMD_READCTCTRL	DPR_CMD(17)
#define DPR_CMD_WRITECT		DPR_CMD(18)
#define DPR_CMD_READCT		DPR_CMD(19)
#define DPR_CMD_WRITEDATA	DPR_CMD(32)
#define DPR_CMD_READDATA	DPR_CMD(33)
#define DPR_CMD_WRITEIO		DPR_CMD(34)
#define DPR_CMD_READIO		DPR_CMD(35)
#define DPR_CMD_WRITECODE	DPR_CMD(36)
#define DPR_CMD_READCODE	DPR_CMD(37)
#define DPR_CMD_EXECUTE		DPR_CMD(38)
#define DPR_CMD_HALT		DPR_CMD(48)
#define DPR_CMD_MASK		DPR_CMD(0xff)

#define DPR_PARAM5_AD_TRIG(x)		(((x) & 0x7) << 2)
#define DPR_PARAM5_AD_TRIG_INT		DPR_PARAM5_AD_TRIG(0)
#define DPR_PARAM5_AD_TRIG_EXT		DPR_PARAM5_AD_TRIG(1)
#define DPR_PARAM5_AD_TRIG_INT_RETRIG	DPR_PARAM5_AD_TRIG(2)
#define DPR_PARAM5_AD_TRIG_EXT_RETRIG	DPR_PARAM5_AD_TRIG(3)
#define DPR_PARAM5_AD_TRIG_INT_RETRIG2	DPR_PARAM5_AD_TRIG(4)

#define DPR_PARAM6_AD_DIFF		BIT(0)

#define DPR_AI_FIFO_DEPTH		2003
#define DPR_AO_FIFO_DEPTH		2048

#define DPR_EXTERNAL_CLOCK		1
#define DPR_RISING_EDGE			2

#define DPR_TMODE_MASK			0x1c

#define DPR_CMD_TIMEOUT			100

static const struct comedi_lrange range_dt3000_ai = {
	4, {
		BIP_RANGE(10),
		BIP_RANGE(5),
		BIP_RANGE(2.5),
		BIP_RANGE(1.25)
	}
};

static const struct comedi_lrange range_dt3000_ai_pgl = {
	4, {
		BIP_RANGE(10),
		BIP_RANGE(1),
		BIP_RANGE(0.1),
		BIP_RANGE(0.02)
	}
};

enum dt3k_boardid {
	BOARD_DT3001,
	BOARD_DT3001_PGL,
	BOARD_DT3002,
	BOARD_DT3003,
	BOARD_DT3003_PGL,
	BOARD_DT3004,
	BOARD_DT3005,
};

struct dt3k_boardtype {
	const char *name;
	int adchan;
	int ai_speed;
	const struct comedi_lrange *adrange;
	unsigned int ai_is_16bit:1;
	unsigned int has_ao:1;
};

static const struct dt3k_boardtype dt3k_boardtypes[] = {
	[BOARD_DT3001] = {
		.name		= "dt3001",
		.adchan		= 16,
		.adrange	= &range_dt3000_ai,
		.ai_speed	= 3000,
		.has_ao		= 1,
	},
	[BOARD_DT3001_PGL] = {
		.name		= "dt3001-pgl",
		.adchan		= 16,
		.adrange	= &range_dt3000_ai_pgl,
		.ai_speed	= 3000,
		.has_ao		= 1,
	},
	[BOARD_DT3002] = {
		.name		= "dt3002",
		.adchan		= 32,
		.adrange	= &range_dt3000_ai,
		.ai_speed	= 3000,
	},
	[BOARD_DT3003] = {
		.name		= "dt3003",
		.adchan		= 64,
		.adrange	= &range_dt3000_ai,
		.ai_speed	= 3000,
		.has_ao		= 1,
	},
	[BOARD_DT3003_PGL] = {
		.name		= "dt3003-pgl",
		.adchan		= 64,
		.adrange	= &range_dt3000_ai_pgl,
		.ai_speed	= 3000,
		.has_ao		= 1,
	},
	[BOARD_DT3004] = {
		.name		= "dt3004",
		.adchan		= 16,
		.adrange	= &range_dt3000_ai,
		.ai_speed	= 10000,
		.ai_is_16bit	= 1,
		.has_ao		= 1,
	},
	[BOARD_DT3005] = {
		.name		= "dt3005",	/* a.k.a. 3004-200 */
		.adchan		= 16,
		.adrange	= &range_dt3000_ai,
		.ai_speed	= 5000,
		.ai_is_16bit	= 1,
		.has_ao		= 1,
	},
};

struct dt3k_private {
	unsigned int lock;
	unsigned int ai_front;
	unsigned int ai_rear;
};

static void dt3k_send_cmd(struct comedi_device *dev, unsigned int cmd)
{
	int i;
	unsigned int status = 0;

	writew(cmd, dev->mmio + DPR_CMD_MBX);

	for (i = 0; i < DPR_CMD_TIMEOUT; i++) {
		status = readw(dev->mmio + DPR_CMD_MBX);
		status &= DPR_CMD_COMPLETION_MASK;
		if (status != DPR_CMD_NOTPROCESSED)
			break;
		udelay(1);
	}

	if (status != DPR_CMD_NOERROR)
		dev_dbg(dev->class_dev, "%s: timeout/error status=0x%04x\n",
			__func__, status);
}

static unsigned int dt3k_readsingle(struct comedi_device *dev,
				    unsigned int subsys, unsigned int chan,
				    unsigned int gain)
{
	writew(subsys, dev->mmio + DPR_SUBSYS);

	writew(chan, dev->mmio + DPR_PARAMS(0));
	writew(gain, dev->mmio + DPR_PARAMS(1));

	dt3k_send_cmd(dev, DPR_CMD_READSINGLE);

	return readw(dev->mmio + DPR_PARAMS(2));
}

static void dt3k_writesingle(struct comedi_device *dev, unsigned int subsys,
			     unsigned int chan, unsigned int data)
{
	writew(subsys, dev->mmio + DPR_SUBSYS);

	writew(chan, dev->mmio + DPR_PARAMS(0));
	writew(0, dev->mmio + DPR_PARAMS(1));
	writew(data, dev->mmio + DPR_PARAMS(2));

	dt3k_send_cmd(dev, DPR_CMD_WRITESINGLE);
}

static void dt3k_ai_empty_fifo(struct comedi_device *dev,
			       struct comedi_subdevice *s)
{
	struct dt3k_private *devpriv = dev->private;
	int front;
	int rear;
	int count;
	int i;
	unsigned short data;

	front = readw(dev->mmio + DPR_AD_BUF_FRONT);
	count = front - devpriv->ai_front;
	if (count < 0)
		count += DPR_AI_FIFO_DEPTH;

	rear = devpriv->ai_rear;

	for (i = 0; i < count; i++) {
		data = readw(dev->mmio + DPR_ADC_BUFFER + rear);
		comedi_buf_write_samples(s, &data, 1);
		rear++;
		if (rear >= DPR_AI_FIFO_DEPTH)
			rear = 0;
	}

	devpriv->ai_rear = rear;
	writew(rear, dev->mmio + DPR_AD_BUF_REAR);
}

static int dt3k_ai_cancel(struct comedi_device *dev,
			  struct comedi_subdevice *s)
{
	writew(DPR_SUBSYS_AI, dev->mmio + DPR_SUBSYS);
	dt3k_send_cmd(dev, DPR_CMD_STOP);

	writew(0, dev->mmio + DPR_INT_MASK);

	return 0;
}

static int debug_n_ints;

/* FIXME! Assumes shared interrupt is for this card. */
/* What's this debug_n_ints stuff? Obviously needs some work... */
static irqreturn_t dt3k_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct comedi_subdevice *s = dev->read_subdev;
	unsigned int status;

	if (!dev->attached)
		return IRQ_NONE;

	status = readw(dev->mmio + DPR_INTR_FLAG);

	if (status & DPR_INTR_ADFULL)
		dt3k_ai_empty_fifo(dev, s);

	if (status & (DPR_INTR_ADSWERR | DPR_INTR_ADHWERR))
		s->async->events |= COMEDI_CB_ERROR;

	debug_n_ints++;
	if (debug_n_ints >= 10)
		s->async->events |= COMEDI_CB_EOA;

	comedi_handle_events(dev, s);
	return IRQ_HANDLED;
}

static int dt3k_ns_to_timer(unsigned int timer_base, unsigned int *nanosec,
			    unsigned int flags)
{
	unsigned int divider, base, prescale;

	/* This function needs improvement */
	/* Don't know if divider==0 works. */

	for (prescale = 0; prescale < 16; prescale++) {
		base = timer_base * (prescale + 1);
		switch (flags & CMDF_ROUND_MASK) {
		case CMDF_ROUND_NEAREST:
		default:
			divider = DIV_ROUND_CLOSEST(*nanosec, base);
			break;
		case CMDF_ROUND_DOWN:
			divider = (*nanosec) / base;
			break;
		case CMDF_ROUND_UP:
			divider = DIV_ROUND_UP(*nanosec, base);
			break;
		}
		if (divider < 65536) {
			*nanosec = divider * base;
			return (prescale << 16) | (divider);
		}
	}

	prescale = 15;
	base = timer_base * (prescale + 1);
	divider = 65535;
	*nanosec = divider * base;
	return (prescale << 16) | (divider);
}

static int dt3k_ai_cmdtest(struct comedi_device *dev,
			   struct comedi_subdevice *s, struct comedi_cmd *cmd)
{
	const struct dt3k_boardtype *board = dev->board_ptr;
	int err = 0;
	unsigned int arg;

	/* Step 1 : check if triggers are trivially valid */

	err |= comedi_check_trigger_src(&cmd->start_src, TRIG_NOW);
	err |= comedi_check_trigger_src(&cmd->scan_begin_src, TRIG_TIMER);
	err |= comedi_check_trigger_src(&cmd->convert_src, TRIG_TIMER);
	err |= comedi_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= comedi_check_trigger_src(&cmd->stop_src, TRIG_COUNT);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */
	/* Step 2b : and mutually compatible */

	/* Step 3: check if arguments are trivially valid */

	err |= comedi_check_trigger_arg_is(&cmd->start_arg, 0);

	if (cmd->scan_begin_src == TRIG_TIMER) {
		err |= comedi_check_trigger_arg_min(&cmd->scan_begin_arg,
						    board->ai_speed);
		err |= comedi_check_trigger_arg_max(&cmd->scan_begin_arg,
						    100 * 16 * 65535);
	}

	if (cmd->convert_src == TRIG_TIMER) {
		err |= comedi_check_trigger_arg_min(&cmd->convert_arg,
						    board->ai_speed);
		err |= comedi_check_trigger_arg_max(&cmd->convert_arg,
						    50 * 16 * 65535);
	}

	err |= comedi_check_trigger_arg_is(&cmd->scan_end_arg,
					   cmd->chanlist_len);

	if (cmd->stop_src == TRIG_COUNT)
		err |= comedi_check_trigger_arg_max(&cmd->stop_arg, 0x00ffffff);
	else	/* TRIG_NONE */
		err |= comedi_check_trigger_arg_is(&cmd->stop_arg, 0);

	if (err)
		return 3;

	/* step 4: fix up any arguments */

	if (cmd->scan_begin_src == TRIG_TIMER) {
		arg = cmd->scan_begin_arg;
		dt3k_ns_to_timer(100, &arg, cmd->flags);
		err |= comedi_check_trigger_arg_is(&cmd->scan_begin_arg, arg);
	}

	if (cmd->convert_src == TRIG_TIMER) {
		arg = cmd->convert_arg;
		dt3k_ns_to_timer(50, &arg, cmd->flags);
		err |= comedi_check_trigger_arg_is(&cmd->convert_arg, arg);

		if (cmd->scan_begin_src == TRIG_TIMER) {
			arg = cmd->convert_arg * cmd->scan_end_arg;
			err |= comedi_check_trigger_arg_min(&cmd->
							    scan_begin_arg,
							    arg);
		}
	}

	if (err)
		return 4;

	return 0;
}

static int dt3k_ai_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct comedi_cmd *cmd = &s->async->cmd;
	int i;
	unsigned int chan, range, aref;
	unsigned int divider;
	unsigned int tscandiv;

	for (i = 0; i < cmd->chanlist_len; i++) {
		chan = CR_CHAN(cmd->chanlist[i]);
		range = CR_RANGE(cmd->chanlist[i]);

		writew((range << 6) | chan, dev->mmio + DPR_ADC_BUFFER + i);
	}
	aref = CR_AREF(cmd->chanlist[0]);

	writew(cmd->scan_end_arg, dev->mmio + DPR_PARAMS(0));

	if (cmd->convert_src == TRIG_TIMER) {
		divider = dt3k_ns_to_timer(50, &cmd->convert_arg, cmd->flags);
		writew((divider >> 16), dev->mmio + DPR_PARAMS(1));
		writew((divider & 0xffff), dev->mmio + DPR_PARAMS(2));
	}

	if (cmd->scan_begin_src == TRIG_TIMER) {
		tscandiv = dt3k_ns_to_timer(100, &cmd->scan_begin_arg,
					    cmd->flags);
		writew((tscandiv >> 16), dev->mmio + DPR_PARAMS(3));
		writew((tscandiv & 0xffff), dev->mmio + DPR_PARAMS(4));
	}

	writew(DPR_PARAM5_AD_TRIG_INT_RETRIG, dev->mmio + DPR_PARAMS(5));
	writew((aref == AREF_DIFF) ? DPR_PARAM6_AD_DIFF : 0,
	       dev->mmio + DPR_PARAMS(6));

	writew(DPR_AI_FIFO_DEPTH / 2, dev->mmio + DPR_PARAMS(7));

	writew(DPR_SUBSYS_AI, dev->mmio + DPR_SUBSYS);
	dt3k_send_cmd(dev, DPR_CMD_CONFIG);

	writew(DPR_INTR_ADFULL | DPR_INTR_ADSWERR | DPR_INTR_ADHWERR,
	       dev->mmio + DPR_INT_MASK);

	debug_n_ints = 0;

	writew(DPR_SUBSYS_AI, dev->mmio + DPR_SUBSYS);
	dt3k_send_cmd(dev, DPR_CMD_START);

	return 0;
}

static int dt3k_ai_insn_read(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     struct comedi_insn *insn,
			     unsigned int *data)
{
	int i;
	unsigned int chan, gain, aref;

	chan = CR_CHAN(insn->chanspec);
	gain = CR_RANGE(insn->chanspec);
	/* XXX docs don't explain how to select aref */
	aref = CR_AREF(insn->chanspec);

	for (i = 0; i < insn->n; i++)
		data[i] = dt3k_readsingle(dev, DPR_SUBSYS_AI, chan, gain);

	return i;
}

static int dt3k_ao_insn_write(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn,
			      unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int val = s->readback[chan];
	int i;

	for (i = 0; i < insn->n; i++) {
		val = data[i];
		dt3k_writesingle(dev, DPR_SUBSYS_AO, chan, val);
	}
	s->readback[chan] = val;

	return insn->n;
}

static void dt3k_dio_config(struct comedi_device *dev, int bits)
{
	/* XXX */
	writew(DPR_SUBSYS_DOUT, dev->mmio + DPR_SUBSYS);

	writew(bits, dev->mmio + DPR_PARAMS(0));

	/* XXX write 0 to DPR_PARAMS(1) and DPR_PARAMS(2) ? */

	dt3k_send_cmd(dev, DPR_CMD_CONFIG);
}

static int dt3k_dio_insn_config(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int mask;
	int ret;

	if (chan < 4)
		mask = 0x0f;
	else
		mask = 0xf0;

	ret = comedi_dio_insn_config(dev, s, insn, data, mask);
	if (ret)
		return ret;

	dt3k_dio_config(dev, (s->io_bits & 0x01) | ((s->io_bits & 0x10) >> 3));

	return insn->n;
}

static int dt3k_dio_insn_bits(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn,
			      unsigned int *data)
{
	if (comedi_dio_update_state(s, data))
		dt3k_writesingle(dev, DPR_SUBSYS_DOUT, 0, s->state);

	data[1] = dt3k_readsingle(dev, DPR_SUBSYS_DIN, 0, 0);

	return insn->n;
}

static int dt3k_mem_insn_read(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn,
			      unsigned int *data)
{
	unsigned int addr = CR_CHAN(insn->chanspec);
	int i;

	for (i = 0; i < insn->n; i++) {
		writew(DPR_SUBSYS_MEM, dev->mmio + DPR_SUBSYS);
		writew(addr, dev->mmio + DPR_PARAMS(0));
		writew(1, dev->mmio + DPR_PARAMS(1));

		dt3k_send_cmd(dev, DPR_CMD_READCODE);

		data[i] = readw(dev->mmio + DPR_PARAMS(2));
	}

	return i;
}

static int dt3000_auto_attach(struct comedi_device *dev,
			      unsigned long context)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct dt3k_boardtype *board = NULL;
	struct dt3k_private *devpriv;
	struct comedi_subdevice *s;
	int ret = 0;

	if (context < ARRAY_SIZE(dt3k_boardtypes))
		board = &dt3k_boardtypes[context];
	if (!board)
		return -ENODEV;
	dev->board_ptr = board;
	dev->board_name = board->name;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	ret = comedi_pci_enable(dev);
	if (ret < 0)
		return ret;

	dev->mmio = pci_ioremap_bar(pcidev, 0);
	if (!dev->mmio)
		return -ENOMEM;

	if (pcidev->irq) {
		ret = request_irq(pcidev->irq, dt3k_interrupt, IRQF_SHARED,
				  dev->board_name, dev);
		if (ret == 0)
			dev->irq = pcidev->irq;
	}

	ret = comedi_alloc_subdevices(dev, 4);
	if (ret)
		return ret;

	/* Analog Input subdevice */
	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_AI;
	s->subdev_flags	= SDF_READABLE | SDF_GROUND | SDF_DIFF;
	s->n_chan	= board->adchan;
	s->maxdata	= board->ai_is_16bit ? 0xffff : 0x0fff;
	s->range_table	= &range_dt3000_ai;	/* XXX */
	s->insn_read	= dt3k_ai_insn_read;
	if (dev->irq) {
		dev->read_subdev = s;
		s->subdev_flags	|= SDF_CMD_READ;
		s->len_chanlist	= 512;
		s->do_cmd	= dt3k_ai_cmd;
		s->do_cmdtest	= dt3k_ai_cmdtest;
		s->cancel	= dt3k_ai_cancel;
	}

	/* Analog Output subdevice */
	s = &dev->subdevices[1];
	if (board->has_ao) {
		s->type		= COMEDI_SUBD_AO;
		s->subdev_flags	= SDF_WRITABLE;
		s->n_chan	= 2;
		s->maxdata	= 0x0fff;
		s->range_table	= &range_bipolar10;
		s->insn_write	= dt3k_ao_insn_write;

		ret = comedi_alloc_subdev_readback(s);
		if (ret)
			return ret;

	} else {
		s->type		= COMEDI_SUBD_UNUSED;
	}

	/* Digital I/O subdevice */
	s = &dev->subdevices[2];
	s->type		= COMEDI_SUBD_DIO;
	s->subdev_flags	= SDF_READABLE | SDF_WRITABLE;
	s->n_chan	= 8;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_config	= dt3k_dio_insn_config;
	s->insn_bits	= dt3k_dio_insn_bits;

	/* Memory subdevice */
	s = &dev->subdevices[3];
	s->type		= COMEDI_SUBD_MEMORY;
	s->subdev_flags	= SDF_READABLE;
	s->n_chan	= 0x1000;
	s->maxdata	= 0xff;
	s->range_table	= &range_unknown;
	s->insn_read	= dt3k_mem_insn_read;

	return 0;
}

static struct comedi_driver dt3000_driver = {
	.driver_name	= "dt3000",
	.module		= THIS_MODULE,
	.auto_attach	= dt3000_auto_attach,
	.detach		= comedi_pci_detach,
};

static int dt3000_pci_probe(struct pci_dev *dev,
			    const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &dt3000_driver, id->driver_data);
}

static const struct pci_device_id dt3000_pci_table[] = {
	{ PCI_VDEVICE(DT, 0x0022), BOARD_DT3001 },
	{ PCI_VDEVICE(DT, 0x0023), BOARD_DT3002 },
	{ PCI_VDEVICE(DT, 0x0024), BOARD_DT3003 },
	{ PCI_VDEVICE(DT, 0x0025), BOARD_DT3004 },
	{ PCI_VDEVICE(DT, 0x0026), BOARD_DT3005 },
	{ PCI_VDEVICE(DT, 0x0027), BOARD_DT3001_PGL },
	{ PCI_VDEVICE(DT, 0x0028), BOARD_DT3003_PGL },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, dt3000_pci_table);

static struct pci_driver dt3000_pci_driver = {
	.name		= "dt3000",
	.id_table	= dt3000_pci_table,
	.probe		= dt3000_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(dt3000_driver, dt3000_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi driver for Data Translation DT3000 series boards");
MODULE_LICENSE("GPL");
