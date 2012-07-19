/*
    comedi/drivers/dt3000.c
    Data Translation DT3000 series driver

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 1999 David A. Schleef <ds@schleef.org>

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
Driver: dt3000
Description: Data Translation DT3000 series
Author: ds
Devices: [Data Translation] DT3001 (dt3000), DT3001-PGL, DT3002, DT3003,
  DT3003-PGL, DT3004, DT3005, DT3004-200
Updated: Mon, 14 Apr 2008 15:41:24 +0100
Status: works

Configuration Options:
  [0] - PCI bus of device (optional)
  [1] - PCI slot of device (optional)
  If bus/slot is not specified, the first supported
  PCI device found will be used.

There is code to support AI commands, but it may not work.

AO commands are not supported.
*/

/*
   The DT3000 series is Data Translation's attempt to make a PCI
   data acquisition board.  The design of this series is very nice,
   since each board has an on-board DSP (Texas Instruments TMS320C52).
   However, a few details are a little annoying.  The boards lack
   bus-mastering DMA, which eliminates them from serious work.
   They also are not capable of autocalibration, which is a common
   feature in modern hardware.  The default firmware is pretty bad,
   making it nearly impossible to write an RT compatible driver.
   It would make an interesting project to write a decent firmware
   for these boards.

   Data Translation originally wanted an NDA for the documentation
   for the 3k series.  However, if you ask nicely, they might send
   you the docs without one, also.
*/

#define DEBUG 1

#include <linux/interrupt.h>
#include "../comedidev.h"
#include <linux/delay.h>

#define PCI_VENDOR_ID_DT	0x1116

static const struct comedi_lrange range_dt3000_ai = { 4, {
							  RANGE(-10, 10),
							  RANGE(-5, 5),
							  RANGE(-2.5, 2.5),
							  RANGE(-1.25, 1.25)
							  }
};

static const struct comedi_lrange range_dt3000_ai_pgl = { 4, {
							      RANGE(-10, 10),
							      RANGE(-1, 1),
							      RANGE(-0.1, 0.1),
							      RANGE(-0.02, 0.02)
							      }
};

struct dt3k_boardtype {

	const char *name;
	unsigned int device_id;
	int adchan;
	int adbits;
	int ai_speed;
	const struct comedi_lrange *adrange;
	int dachan;
	int dabits;
};

static const struct dt3k_boardtype dt3k_boardtypes[] = {
	{.name = "dt3001",
	 .device_id = 0x22,
	 .adchan = 16,
	 .adbits = 12,
	 .adrange = &range_dt3000_ai,
	 .ai_speed = 3000,
	 .dachan = 2,
	 .dabits = 12,
	 },
	{.name = "dt3001-pgl",
	 .device_id = 0x27,
	 .adchan = 16,
	 .adbits = 12,
	 .adrange = &range_dt3000_ai_pgl,
	 .ai_speed = 3000,
	 .dachan = 2,
	 .dabits = 12,
	 },
	{.name = "dt3002",
	 .device_id = 0x23,
	 .adchan = 32,
	 .adbits = 12,
	 .adrange = &range_dt3000_ai,
	 .ai_speed = 3000,
	 .dachan = 0,
	 .dabits = 0,
	 },
	{.name = "dt3003",
	 .device_id = 0x24,
	 .adchan = 64,
	 .adbits = 12,
	 .adrange = &range_dt3000_ai,
	 .ai_speed = 3000,
	 .dachan = 2,
	 .dabits = 12,
	 },
	{.name = "dt3003-pgl",
	 .device_id = 0x28,
	 .adchan = 64,
	 .adbits = 12,
	 .adrange = &range_dt3000_ai_pgl,
	 .ai_speed = 3000,
	 .dachan = 2,
	 .dabits = 12,
	 },
	{.name = "dt3004",
	 .device_id = 0x25,
	 .adchan = 16,
	 .adbits = 16,
	 .adrange = &range_dt3000_ai,
	 .ai_speed = 10000,
	 .dachan = 2,
	 .dabits = 12,
	 },
	{.name = "dt3005",	/* a.k.a. 3004-200 */
	 .device_id = 0x26,
	 .adchan = 16,
	 .adbits = 16,
	 .adrange = &range_dt3000_ai,
	 .ai_speed = 5000,
	 .dachan = 2,
	 .dabits = 12,
	 },
};

#define this_board ((const struct dt3k_boardtype *)dev->board_ptr)

#define DT3000_SIZE		(4*0x1000)

/* dual-ported RAM location definitions */

#define DPR_DAC_buffer		(4*0x000)
#define DPR_ADC_buffer		(4*0x800)
#define DPR_Command		(4*0xfd3)
#define DPR_SubSys		(4*0xfd3)
#define DPR_Encode		(4*0xfd4)
#define DPR_Params(a)		(4*(0xfd5+(a)))
#define DPR_Tick_Reg_Lo		(4*0xff5)
#define DPR_Tick_Reg_Hi		(4*0xff6)
#define DPR_DA_Buf_Front	(4*0xff7)
#define DPR_DA_Buf_Rear		(4*0xff8)
#define DPR_AD_Buf_Front	(4*0xff9)
#define DPR_AD_Buf_Rear		(4*0xffa)
#define DPR_Int_Mask		(4*0xffb)
#define DPR_Intr_Flag		(4*0xffc)
#define DPR_Response_Mbx	(4*0xffe)
#define DPR_Command_Mbx		(4*0xfff)

#define AI_FIFO_DEPTH	2003
#define AO_FIFO_DEPTH	2048

/* command list */

#define CMD_GETBRDINFO		0
#define CMD_CONFIG		1
#define CMD_GETCONFIG		2
#define CMD_START		3
#define CMD_STOP		4
#define CMD_READSINGLE		5
#define CMD_WRITESINGLE		6
#define CMD_CALCCLOCK		7
#define CMD_READEVENTS		8
#define CMD_WRITECTCTRL		16
#define CMD_READCTCTRL		17
#define CMD_WRITECT		18
#define CMD_READCT		19
#define CMD_WRITEDATA		32
#define CMD_READDATA		33
#define CMD_WRITEIO		34
#define CMD_READIO		35
#define CMD_WRITECODE		36
#define CMD_READCODE		37
#define CMD_EXECUTE		38
#define CMD_HALT		48

#define SUBS_AI		0
#define SUBS_AO		1
#define SUBS_DIN	2
#define SUBS_DOUT	3
#define SUBS_MEM	4
#define SUBS_CT		5

/* interrupt flags */
#define DT3000_CMDONE		0x80
#define DT3000_CTDONE		0x40
#define DT3000_DAHWERR		0x20
#define DT3000_DASWERR		0x10
#define DT3000_DAEMPTY		0x08
#define DT3000_ADHWERR		0x04
#define DT3000_ADSWERR		0x02
#define DT3000_ADFULL		0x01

#define DT3000_COMPLETION_MASK	0xff00
#define DT3000_COMMAND_MASK	0x00ff
#define DT3000_NOTPROCESSED	0x0000
#define DT3000_NOERROR		0x5500
#define DT3000_ERROR		0xaa00
#define DT3000_NOTSUPPORTED	0xff00

#define DT3000_EXTERNAL_CLOCK	1
#define DT3000_RISING_EDGE	2

#define TMODE_MASK		0x1c

#define DT3000_AD_TRIG_INTERNAL		(0<<2)
#define DT3000_AD_TRIG_EXTERNAL		(1<<2)
#define DT3000_AD_RETRIG_INTERNAL	(2<<2)
#define DT3000_AD_RETRIG_EXTERNAL	(3<<2)
#define DT3000_AD_EXTRETRIG		(4<<2)

#define DT3000_CHANNEL_MODE_SE		0
#define DT3000_CHANNEL_MODE_DI		1

struct dt3k_private {

	struct pci_dev *pci_dev;
	resource_size_t phys_addr;
	void __iomem *io_addr;
	unsigned int lock;
	unsigned int ao_readback[2];
	unsigned int ai_front;
	unsigned int ai_rear;
};

#define devpriv ((struct dt3k_private *)dev->private)

static void dt3k_ai_empty_fifo(struct comedi_device *dev,
			       struct comedi_subdevice *s);
static int dt3k_ns_to_timer(unsigned int timer_base, unsigned int *arg,
			    unsigned int round_mode);
static int dt3k_ai_cancel(struct comedi_device *dev,
			  struct comedi_subdevice *s);
#ifdef DEBUG
static void debug_intr_flags(unsigned int flags);
#endif

#define TIMEOUT 100

static int dt3k_send_cmd(struct comedi_device *dev, unsigned int cmd)
{
	int i;
	unsigned int status = 0;

	writew(cmd, devpriv->io_addr + DPR_Command_Mbx);

	for (i = 0; i < TIMEOUT; i++) {
		status = readw(devpriv->io_addr + DPR_Command_Mbx);
		if ((status & DT3000_COMPLETION_MASK) != DT3000_NOTPROCESSED)
			break;
		udelay(1);
	}
	if ((status & DT3000_COMPLETION_MASK) == DT3000_NOERROR)
		return 0;

	dev_dbg(dev->class_dev, "dt3k_send_cmd() timeout/error status=0x%04x\n",
		status);

	return -ETIME;
}

static unsigned int dt3k_readsingle(struct comedi_device *dev,
				    unsigned int subsys, unsigned int chan,
				    unsigned int gain)
{
	writew(subsys, devpriv->io_addr + DPR_SubSys);

	writew(chan, devpriv->io_addr + DPR_Params(0));
	writew(gain, devpriv->io_addr + DPR_Params(1));

	dt3k_send_cmd(dev, CMD_READSINGLE);

	return readw(devpriv->io_addr + DPR_Params(2));
}

static void dt3k_writesingle(struct comedi_device *dev, unsigned int subsys,
			     unsigned int chan, unsigned int data)
{
	writew(subsys, devpriv->io_addr + DPR_SubSys);

	writew(chan, devpriv->io_addr + DPR_Params(0));
	writew(0, devpriv->io_addr + DPR_Params(1));
	writew(data, devpriv->io_addr + DPR_Params(2));

	dt3k_send_cmd(dev, CMD_WRITESINGLE);
}

static int debug_n_ints;

/* FIXME! Assumes shared interrupt is for this card. */
/* What's this debug_n_ints stuff? Obviously needs some work... */
static irqreturn_t dt3k_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct comedi_subdevice *s;
	unsigned int status;

	if (!dev->attached)
		return IRQ_NONE;

	s = dev->subdevices + 0;
	status = readw(devpriv->io_addr + DPR_Intr_Flag);
#ifdef DEBUG
	debug_intr_flags(status);
#endif

	if (status & DT3000_ADFULL) {
		dt3k_ai_empty_fifo(dev, s);
		s->async->events |= COMEDI_CB_BLOCK;
	}

	if (status & (DT3000_ADSWERR | DT3000_ADHWERR))
		s->async->events |= COMEDI_CB_ERROR | COMEDI_CB_EOA;

	debug_n_ints++;
	if (debug_n_ints >= 10) {
		dt3k_ai_cancel(dev, s);
		s->async->events |= COMEDI_CB_EOA;
	}

	comedi_event(dev, s);
	return IRQ_HANDLED;
}

#ifdef DEBUG
static char *intr_flags[] = {
	"AdFull", "AdSwError", "AdHwError", "DaEmpty",
	"DaSwError", "DaHwError", "CtDone", "CmDone",
};

static void debug_intr_flags(unsigned int flags)
{
	int i;
	printk(KERN_DEBUG "dt3k: intr_flags:");
	for (i = 0; i < 8; i++) {
		if (flags & (1 << i))
			printk(KERN_CONT " %s", intr_flags[i]);
	}
	printk(KERN_CONT "\n");
}
#endif

static void dt3k_ai_empty_fifo(struct comedi_device *dev,
			       struct comedi_subdevice *s)
{
	int front;
	int rear;
	int count;
	int i;
	short data;

	front = readw(devpriv->io_addr + DPR_AD_Buf_Front);
	count = front - devpriv->ai_front;
	if (count < 0)
		count += AI_FIFO_DEPTH;

	dev_dbg(dev->class_dev, "reading %d samples\n", count);

	rear = devpriv->ai_rear;

	for (i = 0; i < count; i++) {
		data = readw(devpriv->io_addr + DPR_ADC_buffer + rear);
		comedi_buf_put(s->async, data);
		rear++;
		if (rear >= AI_FIFO_DEPTH)
			rear = 0;
	}

	devpriv->ai_rear = rear;
	writew(rear, devpriv->io_addr + DPR_AD_Buf_Rear);
}

static int dt3k_ai_cmdtest(struct comedi_device *dev,
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
	cmd->scan_begin_src &= TRIG_TIMER;
	if (!cmd->scan_begin_src || tmp != cmd->scan_begin_src)
		err++;

	tmp = cmd->convert_src;
	cmd->convert_src &= TRIG_TIMER;
	if (!cmd->convert_src || tmp != cmd->convert_src)
		err++;

	tmp = cmd->scan_end_src;
	cmd->scan_end_src &= TRIG_COUNT;
	if (!cmd->scan_end_src || tmp != cmd->scan_end_src)
		err++;

	tmp = cmd->stop_src;
	cmd->stop_src &= TRIG_COUNT;
	if (!cmd->stop_src || tmp != cmd->stop_src)
		err++;

	if (err)
		return 1;

	/* step 2: make sure trigger sources are unique and mutually compatible */

	if (err)
		return 2;

	/* step 3: make sure arguments are trivially compatible */

	if (cmd->start_arg != 0) {
		cmd->start_arg = 0;
		err++;
	}

	if (cmd->scan_begin_src == TRIG_TIMER) {
		if (cmd->scan_begin_arg < this_board->ai_speed) {
			cmd->scan_begin_arg = this_board->ai_speed;
			err++;
		}
		if (cmd->scan_begin_arg > 100 * 16 * 65535) {
			cmd->scan_begin_arg = 100 * 16 * 65535;
			err++;
		}
	} else {
		/* not supported */
	}
	if (cmd->convert_src == TRIG_TIMER) {
		if (cmd->convert_arg < this_board->ai_speed) {
			cmd->convert_arg = this_board->ai_speed;
			err++;
		}
		if (cmd->convert_arg > 50 * 16 * 65535) {
			cmd->convert_arg = 50 * 16 * 65535;
			err++;
		}
	} else {
		/* not supported */
	}

	if (cmd->scan_end_arg != cmd->chanlist_len) {
		cmd->scan_end_arg = cmd->chanlist_len;
		err++;
	}
	if (cmd->stop_src == TRIG_COUNT) {
		if (cmd->stop_arg > 0x00ffffff) {
			cmd->stop_arg = 0x00ffffff;
			err++;
		}
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

	if (cmd->scan_begin_src == TRIG_TIMER) {
		tmp = cmd->scan_begin_arg;
		dt3k_ns_to_timer(100, &cmd->scan_begin_arg,
				 cmd->flags & TRIG_ROUND_MASK);
		if (tmp != cmd->scan_begin_arg)
			err++;
	} else {
		/* not supported */
	}
	if (cmd->convert_src == TRIG_TIMER) {
		tmp = cmd->convert_arg;
		dt3k_ns_to_timer(50, &cmd->convert_arg,
				 cmd->flags & TRIG_ROUND_MASK);
		if (tmp != cmd->convert_arg)
			err++;
		if (cmd->scan_begin_src == TRIG_TIMER &&
		    cmd->scan_begin_arg <
		    cmd->convert_arg * cmd->scan_end_arg) {
			cmd->scan_begin_arg =
			    cmd->convert_arg * cmd->scan_end_arg;
			err++;
		}
	} else {
		/* not supported */
	}

	if (err)
		return 4;

	return 0;
}

static int dt3k_ns_to_timer(unsigned int timer_base, unsigned int *nanosec,
			    unsigned int round_mode)
{
	int divider, base, prescale;

	/* This function needs improvment */
	/* Don't know if divider==0 works. */

	for (prescale = 0; prescale < 16; prescale++) {
		base = timer_base * (prescale + 1);
		switch (round_mode) {
		case TRIG_ROUND_NEAREST:
		default:
			divider = (*nanosec + base / 2) / base;
			break;
		case TRIG_ROUND_DOWN:
			divider = (*nanosec) / base;
			break;
		case TRIG_ROUND_UP:
			divider = (*nanosec) / base;
			break;
		}
		if (divider < 65536) {
			*nanosec = divider * base;
			return (prescale << 16) | (divider);
		}
	}

	prescale = 15;
	base = timer_base * (1 << prescale);
	divider = 65535;
	*nanosec = divider * base;
	return (prescale << 16) | (divider);
}

static int dt3k_ai_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct comedi_cmd *cmd = &s->async->cmd;
	int i;
	unsigned int chan, range, aref;
	unsigned int divider;
	unsigned int tscandiv;
	int ret;
	unsigned int mode;

	dev_dbg(dev->class_dev, "dt3k_ai_cmd:\n");
	for (i = 0; i < cmd->chanlist_len; i++) {
		chan = CR_CHAN(cmd->chanlist[i]);
		range = CR_RANGE(cmd->chanlist[i]);

		writew((range << 6) | chan,
		       devpriv->io_addr + DPR_ADC_buffer + i);
	}
	aref = CR_AREF(cmd->chanlist[0]);

	writew(cmd->scan_end_arg, devpriv->io_addr + DPR_Params(0));
	dev_dbg(dev->class_dev, "param[0]=0x%04x\n", cmd->scan_end_arg);

	if (cmd->convert_src == TRIG_TIMER) {
		divider = dt3k_ns_to_timer(50, &cmd->convert_arg,
					   cmd->flags & TRIG_ROUND_MASK);
		writew((divider >> 16), devpriv->io_addr + DPR_Params(1));
		dev_dbg(dev->class_dev, "param[1]=0x%04x\n", divider >> 16);
		writew((divider & 0xffff), devpriv->io_addr + DPR_Params(2));
		dev_dbg(dev->class_dev, "param[2]=0x%04x\n", divider & 0xffff);
	} else {
		/* not supported */
	}

	if (cmd->scan_begin_src == TRIG_TIMER) {
		tscandiv = dt3k_ns_to_timer(100, &cmd->scan_begin_arg,
					    cmd->flags & TRIG_ROUND_MASK);
		writew((tscandiv >> 16), devpriv->io_addr + DPR_Params(3));
		dev_dbg(dev->class_dev, "param[3]=0x%04x\n", tscandiv >> 16);
		writew((tscandiv & 0xffff), devpriv->io_addr + DPR_Params(4));
		dev_dbg(dev->class_dev, "param[4]=0x%04x\n", tscandiv & 0xffff);
	} else {
		/* not supported */
	}

	mode = DT3000_AD_RETRIG_INTERNAL | 0 | 0;
	writew(mode, devpriv->io_addr + DPR_Params(5));
	dev_dbg(dev->class_dev, "param[5]=0x%04x\n", mode);
	writew(aref == AREF_DIFF, devpriv->io_addr + DPR_Params(6));
	dev_dbg(dev->class_dev, "param[6]=0x%04x\n", aref == AREF_DIFF);

	writew(AI_FIFO_DEPTH / 2, devpriv->io_addr + DPR_Params(7));
	dev_dbg(dev->class_dev, "param[7]=0x%04x\n", AI_FIFO_DEPTH / 2);

	writew(SUBS_AI, devpriv->io_addr + DPR_SubSys);
	ret = dt3k_send_cmd(dev, CMD_CONFIG);

	writew(DT3000_ADFULL | DT3000_ADSWERR | DT3000_ADHWERR,
	       devpriv->io_addr + DPR_Int_Mask);

	debug_n_ints = 0;

	writew(SUBS_AI, devpriv->io_addr + DPR_SubSys);
	ret = dt3k_send_cmd(dev, CMD_START);

	return 0;
}

static int dt3k_ai_cancel(struct comedi_device *dev, struct comedi_subdevice *s)
{
	int ret;

	writew(SUBS_AI, devpriv->io_addr + DPR_SubSys);
	ret = dt3k_send_cmd(dev, CMD_STOP);

	writew(0, devpriv->io_addr + DPR_Int_Mask);

	return 0;
}

static int dt3k_ai_insn(struct comedi_device *dev, struct comedi_subdevice *s,
			struct comedi_insn *insn, unsigned int *data)
{
	int i;
	unsigned int chan, gain, aref;

	chan = CR_CHAN(insn->chanspec);
	gain = CR_RANGE(insn->chanspec);
	/* XXX docs don't explain how to select aref */
	aref = CR_AREF(insn->chanspec);

	for (i = 0; i < insn->n; i++)
		data[i] = dt3k_readsingle(dev, SUBS_AI, chan, gain);

	return i;
}

static int dt3k_ao_insn(struct comedi_device *dev, struct comedi_subdevice *s,
			struct comedi_insn *insn, unsigned int *data)
{
	int i;
	unsigned int chan;

	chan = CR_CHAN(insn->chanspec);
	for (i = 0; i < insn->n; i++) {
		dt3k_writesingle(dev, SUBS_AO, chan, data[i]);
		devpriv->ao_readback[chan] = data[i];
	}

	return i;
}

static int dt3k_ao_insn_read(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     struct comedi_insn *insn, unsigned int *data)
{
	int i;
	unsigned int chan;

	chan = CR_CHAN(insn->chanspec);
	for (i = 0; i < insn->n; i++)
		data[i] = devpriv->ao_readback[chan];

	return i;
}

static void dt3k_dio_config(struct comedi_device *dev, int bits)
{
	/* XXX */
	writew(SUBS_DOUT, devpriv->io_addr + DPR_SubSys);

	writew(bits, devpriv->io_addr + DPR_Params(0));
#if 0
	/* don't know */
	writew(0, devpriv->io_addr + DPR_Params(1));
	writew(0, devpriv->io_addr + DPR_Params(2));
#endif

	dt3k_send_cmd(dev, CMD_CONFIG);
}

static int dt3k_dio_insn_config(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data)
{
	int mask;

	mask = (CR_CHAN(insn->chanspec) < 4) ? 0x0f : 0xf0;

	switch (data[0]) {
	case INSN_CONFIG_DIO_OUTPUT:
		s->io_bits |= mask;
		break;
	case INSN_CONFIG_DIO_INPUT:
		s->io_bits &= ~mask;
		break;
	case INSN_CONFIG_DIO_QUERY:
		data[1] =
		    (s->
		     io_bits & (1 << CR_CHAN(insn->chanspec))) ? COMEDI_OUTPUT :
		    COMEDI_INPUT;
		return insn->n;
		break;
	default:
		return -EINVAL;
		break;
	}
	mask = (s->io_bits & 0x01) | ((s->io_bits & 0x10) >> 3);
	dt3k_dio_config(dev, mask);

	return insn->n;
}

static int dt3k_dio_insn_bits(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn, unsigned int *data)
{
	if (data[0]) {
		s->state &= ~data[0];
		s->state |= data[1] & data[0];
		dt3k_writesingle(dev, SUBS_DOUT, 0, s->state);
	}
	data[1] = dt3k_readsingle(dev, SUBS_DIN, 0, 0);

	return insn->n;
}

static int dt3k_mem_insn_read(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn, unsigned int *data)
{
	unsigned int addr = CR_CHAN(insn->chanspec);
	int i;

	for (i = 0; i < insn->n; i++) {
		writew(SUBS_MEM, devpriv->io_addr + DPR_SubSys);
		writew(addr, devpriv->io_addr + DPR_Params(0));
		writew(1, devpriv->io_addr + DPR_Params(1));

		dt3k_send_cmd(dev, CMD_READCODE);

		data[i] = readw(devpriv->io_addr + DPR_Params(2));
	}

	return i;
}

static struct pci_dev *dt3000_find_pci_dev(struct comedi_device *dev,
					   struct comedi_devconfig *it)
{
	struct pci_dev *pcidev = NULL;
	int bus = it->options[0];
	int slot = it->options[1];
	int i;

	for_each_pci_dev(pcidev) {
		if (bus || slot) {
			if (bus != pcidev->bus->number ||
			    slot != PCI_SLOT(pcidev->devfn))
				continue;
		}
		if (pcidev->vendor != PCI_VENDOR_ID_DT)
			continue;
		for (i = 0; i < ARRAY_SIZE(dt3k_boardtypes); i++) {
			if (dt3k_boardtypes[i].device_id != pcidev->device)
				continue;
			dev->board_ptr = dt3k_boardtypes + i;
			return pcidev;
		}
	}
	dev_err(dev->class_dev,
		"No supported board found! (req. bus %d, slot %d)\n",
		bus, slot);
	return NULL;
}

static int dt3000_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct pci_dev *pcidev;
	struct comedi_subdevice *s;
	int ret = 0;

	dev_dbg(dev->class_dev, "dt3000:\n");

	ret = alloc_private(dev, sizeof(struct dt3k_private));
	if (ret < 0)
		return ret;

	pcidev = dt3000_find_pci_dev(dev, it);
	if (!pcidev)
		return -EIO;
	devpriv->pci_dev = pcidev;

	ret = comedi_pci_enable(pcidev, "dt3000");
	if (ret < 0)
		return ret;

	devpriv->phys_addr = pci_resource_start(pcidev, 0);
	devpriv->io_addr = ioremap(devpriv->phys_addr, DT3000_SIZE);
	if (!devpriv->io_addr)
		return -ENOMEM;

	dev->board_name = this_board->name;

	if (request_irq(pcidev->irq, dt3k_interrupt, IRQF_SHARED,
			"dt3000", dev)) {
		dev_err(dev->class_dev, "unable to allocate IRQ %u\n",
			pcidev->irq);
		return -EINVAL;
	}
	dev->irq = pcidev->irq;

	ret = comedi_alloc_subdevices(dev, 4);
	if (ret)
		return ret;

	s = dev->subdevices;
	dev->read_subdev = s;

	/* ai subdevice */
	s->type = COMEDI_SUBD_AI;
	s->subdev_flags = SDF_READABLE | SDF_GROUND | SDF_DIFF | SDF_CMD_READ;
	s->n_chan = this_board->adchan;
	s->insn_read = dt3k_ai_insn;
	s->maxdata = (1 << this_board->adbits) - 1;
	s->len_chanlist = 512;
	s->range_table = &range_dt3000_ai;	/* XXX */
	s->do_cmd = dt3k_ai_cmd;
	s->do_cmdtest = dt3k_ai_cmdtest;
	s->cancel = dt3k_ai_cancel;

	s++;
	/* ao subsystem */
	s->type = COMEDI_SUBD_AO;
	s->subdev_flags = SDF_WRITABLE;
	s->n_chan = 2;
	s->insn_read = dt3k_ao_insn_read;
	s->insn_write = dt3k_ao_insn;
	s->maxdata = (1 << this_board->dabits) - 1;
	s->len_chanlist = 1;
	s->range_table = &range_bipolar10;

	s++;
	/* dio subsystem */
	s->type = COMEDI_SUBD_DIO;
	s->subdev_flags = SDF_READABLE | SDF_WRITABLE;
	s->n_chan = 8;
	s->insn_config = dt3k_dio_insn_config;
	s->insn_bits = dt3k_dio_insn_bits;
	s->maxdata = 1;
	s->len_chanlist = 8;
	s->range_table = &range_digital;

	s++;
	/* mem subsystem */
	s->type = COMEDI_SUBD_MEMORY;
	s->subdev_flags = SDF_READABLE;
	s->n_chan = 0x1000;
	s->insn_read = dt3k_mem_insn_read;
	s->maxdata = 0xff;
	s->len_chanlist = 1;
	s->range_table = &range_unknown;

#if 0
	s++;
	/* proc subsystem */
	s->type = COMEDI_SUBD_PROC;
#endif

	return 0;
}

static void dt3000_detach(struct comedi_device *dev)
{
	if (dev->irq)
		free_irq(dev->irq, dev);
	if (devpriv) {
		if (devpriv->pci_dev) {
			if (devpriv->phys_addr)
				comedi_pci_disable(devpriv->pci_dev);
			pci_dev_put(devpriv->pci_dev);
		}
		if (devpriv->io_addr)
			iounmap(devpriv->io_addr);
	}
}

static struct comedi_driver dt3000_driver = {
	.driver_name	= "dt3000",
	.module		= THIS_MODULE,
	.attach		= dt3000_attach,
	.detach		= dt3000_detach,
};

static int __devinit dt3000_pci_probe(struct pci_dev *dev,
				      const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, &dt3000_driver);
}

static void __devexit dt3000_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static DEFINE_PCI_DEVICE_TABLE(dt3000_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_DT, 0x0022) },
	{ PCI_DEVICE(PCI_VENDOR_ID_DT, 0x0027) },
	{ PCI_DEVICE(PCI_VENDOR_ID_DT, 0x0023) },
	{ PCI_DEVICE(PCI_VENDOR_ID_DT, 0x0024) },
	{ PCI_DEVICE(PCI_VENDOR_ID_DT, 0x0028) },
	{ PCI_DEVICE(PCI_VENDOR_ID_DT, 0x0025) },
	{ PCI_DEVICE(PCI_VENDOR_ID_DT, 0x0026) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, dt3000_pci_table);

static struct pci_driver dt3000_pci_driver = {
	.name		= "dt3000",
	.id_table	= dt3000_pci_table,
	.probe		= dt3000_pci_probe,
	.remove		= __devexit_p(dt3000_pci_remove),
};
module_comedi_pci_driver(dt3000_driver, dt3000_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
