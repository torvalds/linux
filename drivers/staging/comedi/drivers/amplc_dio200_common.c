/*
    comedi/drivers/amplc_dio200_common.c

    Common support code for "amplc_dio200" and "amplc_dio200_pci".

    Copyright (C) 2005-2013 MEV Ltd. <http://www.mev.co.uk/>

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 1998,2000 David A. Schleef <ds@schleef.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
*/

#include <linux/module.h>
#include <linux/interrupt.h>

#include "../comedidev.h"

#include "amplc_dio200.h"
#include "comedi_fc.h"
#include "8253.h"
#include "8255.h"		/* only for register defines */

/* 200 series registers */
#define DIO200_IO_SIZE		0x20
#define DIO200_PCIE_IO_SIZE	0x4000
#define DIO200_XCLK_SCE		0x18	/* Group X clock selection register */
#define DIO200_YCLK_SCE		0x19	/* Group Y clock selection register */
#define DIO200_ZCLK_SCE		0x1a	/* Group Z clock selection register */
#define DIO200_XGAT_SCE		0x1b	/* Group X gate selection register */
#define DIO200_YGAT_SCE		0x1c	/* Group Y gate selection register */
#define DIO200_ZGAT_SCE		0x1d	/* Group Z gate selection register */
#define DIO200_INT_SCE		0x1e	/* Interrupt enable/status register */
/* Extra registers for new PCIe boards */
#define DIO200_ENHANCE		0x20	/* 1 to enable enhanced features */
#define DIO200_VERSION		0x24	/* Hardware version register */
#define DIO200_TS_CONFIG	0x600	/* Timestamp timer config register */
#define DIO200_TS_COUNT		0x602	/* Timestamp timer count register */

/*
 * Functions for constructing value for DIO_200_?CLK_SCE and
 * DIO_200_?GAT_SCE registers:
 *
 * 'which' is: 0 for CTR-X1, CTR-Y1, CTR-Z1; 1 for CTR-X2, CTR-Y2 or CTR-Z2.
 * 'chan' is the channel: 0, 1 or 2.
 * 'source' is the signal source: 0 to 7, or 0 to 31 for "enhanced" boards.
 */
static unsigned char clk_gat_sce(unsigned int which, unsigned int chan,
				 unsigned int source)
{
	return (which << 5) | (chan << 3) |
	       ((source & 030) << 3) | (source & 007);
}

static unsigned char clk_sce(unsigned int which, unsigned int chan,
			     unsigned int source)
{
	return clk_gat_sce(which, chan, source);
}

static unsigned char gat_sce(unsigned int which, unsigned int chan,
			     unsigned int source)
{
	return clk_gat_sce(which, chan, source);
}

/*
 * Periods of the internal clock sources in nanoseconds.
 */
static const unsigned int clock_period[32] = {
	[1] = 100,		/* 10 MHz */
	[2] = 1000,		/* 1 MHz */
	[3] = 10000,		/* 100 kHz */
	[4] = 100000,		/* 10 kHz */
	[5] = 1000000,		/* 1 kHz */
	[11] = 50,		/* 20 MHz (enhanced boards) */
	/* clock sources 12 and later reserved for enhanced boards */
};

/*
 * Timestamp timer configuration register (for new PCIe boards).
 */
#define TS_CONFIG_RESET		0x100	/* Reset counter to zero. */
#define TS_CONFIG_CLK_SRC_MASK	0x0FF	/* Clock source. */
#define TS_CONFIG_MAX_CLK_SRC	2	/* Maximum clock source value. */

/*
 * Periods of the timestamp timer clock sources in nanoseconds.
 */
static const unsigned int ts_clock_period[TS_CONFIG_MAX_CLK_SRC + 1] = {
	1,			/* 1 nanosecond (but with 20 ns granularity). */
	1000,			/* 1 microsecond. */
	1000000,		/* 1 millisecond. */
};

struct dio200_subdev_8254 {
	unsigned int ofs;		/* Counter base offset */
	unsigned int clk_sce_ofs;	/* CLK_SCE base address */
	unsigned int gat_sce_ofs;	/* GAT_SCE base address */
	int which;			/* Bit 5 of CLK_SCE or GAT_SCE */
	unsigned int clock_src[3];	/* Current clock sources */
	unsigned int gate_src[3];	/* Current gate sources */
	spinlock_t spinlock;
};

struct dio200_subdev_8255 {
	unsigned int ofs;		/* DIO base offset */
};

struct dio200_subdev_intr {
	spinlock_t spinlock;
	unsigned int ofs;
	unsigned int valid_isns;
	unsigned int enabled_isns;
	unsigned int stopcount;
	bool active:1;
};

static unsigned char dio200_read8(struct comedi_device *dev,
				  unsigned int offset)
{
	const struct dio200_board *board = dev->board_ptr;

	if (board->is_pcie)
		offset <<= 3;

	if (dev->mmio)
		return readb(dev->mmio + offset);
	return inb(dev->iobase + offset);
}

static void dio200_write8(struct comedi_device *dev,
			  unsigned int offset, unsigned char val)
{
	const struct dio200_board *board = dev->board_ptr;

	if (board->is_pcie)
		offset <<= 3;

	if (dev->mmio)
		writeb(val, dev->mmio + offset);
	else
		outb(val, dev->iobase + offset);
}

static unsigned int dio200_read32(struct comedi_device *dev,
				  unsigned int offset)
{
	const struct dio200_board *board = dev->board_ptr;

	if (board->is_pcie)
		offset <<= 3;

	if (dev->mmio)
		return readl(dev->mmio + offset);
	return inl(dev->iobase + offset);
}

static void dio200_write32(struct comedi_device *dev,
			   unsigned int offset, unsigned int val)
{
	const struct dio200_board *board = dev->board_ptr;

	if (board->is_pcie)
		offset <<= 3;

	if (dev->mmio)
		writel(val, dev->mmio + offset);
	else
		outl(val, dev->iobase + offset);
}

static int dio200_subdev_intr_insn_bits(struct comedi_device *dev,
					struct comedi_subdevice *s,
					struct comedi_insn *insn,
					unsigned int *data)
{
	const struct dio200_board *board = dev->board_ptr;
	struct dio200_subdev_intr *subpriv = s->private;

	if (board->has_int_sce) {
		/* Just read the interrupt status register.  */
		data[1] = dio200_read8(dev, subpriv->ofs) & subpriv->valid_isns;
	} else {
		/* No interrupt status register. */
		data[0] = 0;
	}

	return insn->n;
}

static void dio200_stop_intr(struct comedi_device *dev,
			     struct comedi_subdevice *s)
{
	const struct dio200_board *board = dev->board_ptr;
	struct dio200_subdev_intr *subpriv = s->private;

	subpriv->active = false;
	subpriv->enabled_isns = 0;
	if (board->has_int_sce)
		dio200_write8(dev, subpriv->ofs, 0);
}

static void dio200_start_intr(struct comedi_device *dev,
			      struct comedi_subdevice *s)
{
	const struct dio200_board *board = dev->board_ptr;
	struct dio200_subdev_intr *subpriv = s->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned int n;
	unsigned isn_bits;

	/* Determine interrupt sources to enable. */
	isn_bits = 0;
	if (cmd->chanlist) {
		for (n = 0; n < cmd->chanlist_len; n++)
			isn_bits |= (1U << CR_CHAN(cmd->chanlist[n]));
	}
	isn_bits &= subpriv->valid_isns;
	/* Enable interrupt sources. */
	subpriv->enabled_isns = isn_bits;
	if (board->has_int_sce)
		dio200_write8(dev, subpriv->ofs, isn_bits);
}

static int dio200_inttrig_start_intr(struct comedi_device *dev,
				     struct comedi_subdevice *s,
				     unsigned int trig_num)
{
	struct dio200_subdev_intr *subpriv = s->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned long flags;

	if (trig_num != cmd->start_arg)
		return -EINVAL;

	spin_lock_irqsave(&subpriv->spinlock, flags);
	s->async->inttrig = NULL;
	if (subpriv->active)
		dio200_start_intr(dev, s);

	spin_unlock_irqrestore(&subpriv->spinlock, flags);

	return 1;
}

static void dio200_read_scan_intr(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  unsigned int triggered)
{
	struct dio200_subdev_intr *subpriv = s->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned short val;
	unsigned int n, ch;

	val = 0;
	for (n = 0; n < cmd->chanlist_len; n++) {
		ch = CR_CHAN(cmd->chanlist[n]);
		if (triggered & (1U << ch))
			val |= (1U << n);
	}
	/* Write the scan to the buffer. */
	if (comedi_buf_put(s, val)) {
		s->async->events |= (COMEDI_CB_BLOCK | COMEDI_CB_EOS);
	} else {
		/* Error!  Stop acquisition.  */
		dio200_stop_intr(dev, s);
		s->async->events |= COMEDI_CB_ERROR | COMEDI_CB_OVERFLOW;
		dev_err(dev->class_dev, "buffer overflow\n");
	}

	/* Check for end of acquisition. */
	if (cmd->stop_src == TRIG_COUNT) {
		if (subpriv->stopcount > 0) {
			subpriv->stopcount--;
			if (subpriv->stopcount == 0) {
				s->async->events |= COMEDI_CB_EOA;
				dio200_stop_intr(dev, s);
			}
		}
	}
}

static int dio200_handle_read_intr(struct comedi_device *dev,
				   struct comedi_subdevice *s)
{
	const struct dio200_board *board = dev->board_ptr;
	struct dio200_subdev_intr *subpriv = s->private;
	unsigned triggered;
	unsigned intstat;
	unsigned cur_enabled;
	unsigned int oldevents;
	unsigned long flags;

	triggered = 0;

	spin_lock_irqsave(&subpriv->spinlock, flags);
	oldevents = s->async->events;
	if (board->has_int_sce) {
		/*
		 * Collect interrupt sources that have triggered and disable
		 * them temporarily.  Loop around until no extra interrupt
		 * sources have triggered, at which point, the valid part of
		 * the interrupt status register will read zero, clearing the
		 * cause of the interrupt.
		 *
		 * Mask off interrupt sources already seen to avoid infinite
		 * loop in case of misconfiguration.
		 */
		cur_enabled = subpriv->enabled_isns;
		while ((intstat = (dio200_read8(dev, subpriv->ofs) &
				   subpriv->valid_isns & ~triggered)) != 0) {
			triggered |= intstat;
			cur_enabled &= ~triggered;
			dio200_write8(dev, subpriv->ofs, cur_enabled);
		}
	} else {
		/*
		 * No interrupt status register.  Assume the single interrupt
		 * source has triggered.
		 */
		triggered = subpriv->enabled_isns;
	}

	if (triggered) {
		/*
		 * Some interrupt sources have triggered and have been
		 * temporarily disabled to clear the cause of the interrupt.
		 *
		 * Reenable them NOW to minimize the time they are disabled.
		 */
		cur_enabled = subpriv->enabled_isns;
		if (board->has_int_sce)
			dio200_write8(dev, subpriv->ofs, cur_enabled);

		if (subpriv->active) {
			/*
			 * The command is still active.
			 *
			 * Ignore interrupt sources that the command isn't
			 * interested in (just in case there's a race
			 * condition).
			 */
			if (triggered & subpriv->enabled_isns)
				/* Collect scan data. */
				dio200_read_scan_intr(dev, s, triggered);
		}
	}
	spin_unlock_irqrestore(&subpriv->spinlock, flags);

	if (oldevents != s->async->events)
		comedi_event(dev, s);

	return (triggered != 0);
}

static int dio200_subdev_intr_cancel(struct comedi_device *dev,
				     struct comedi_subdevice *s)
{
	struct dio200_subdev_intr *subpriv = s->private;
	unsigned long flags;

	spin_lock_irqsave(&subpriv->spinlock, flags);
	if (subpriv->active)
		dio200_stop_intr(dev, s);

	spin_unlock_irqrestore(&subpriv->spinlock, flags);

	return 0;
}

static int dio200_subdev_intr_cmdtest(struct comedi_device *dev,
				      struct comedi_subdevice *s,
				      struct comedi_cmd *cmd)
{
	int err = 0;

	/* Step 1 : check if triggers are trivially valid */

	err |= cfc_check_trigger_src(&cmd->start_src, TRIG_NOW | TRIG_INT);
	err |= cfc_check_trigger_src(&cmd->scan_begin_src, TRIG_EXT);
	err |= cfc_check_trigger_src(&cmd->convert_src, TRIG_NOW);
	err |= cfc_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= cfc_check_trigger_src(&cmd->stop_src, TRIG_COUNT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= cfc_check_trigger_is_unique(cmd->start_src);
	err |= cfc_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	err |= cfc_check_trigger_arg_is(&cmd->start_arg, 0);
	err |= cfc_check_trigger_arg_is(&cmd->scan_begin_arg, 0);
	err |= cfc_check_trigger_arg_is(&cmd->convert_arg, 0);
	err |= cfc_check_trigger_arg_is(&cmd->scan_end_arg, cmd->chanlist_len);

	if (cmd->stop_src == TRIG_COUNT)
		err |= cfc_check_trigger_arg_min(&cmd->stop_arg, 1);
	else	/* TRIG_NONE */
		err |= cfc_check_trigger_arg_is(&cmd->stop_arg, 0);

	if (err)
		return 3;

	/* step 4: fix up any arguments */

	/* if (err) return 4; */

	return 0;
}

static int dio200_subdev_intr_cmd(struct comedi_device *dev,
				  struct comedi_subdevice *s)
{
	struct comedi_cmd *cmd = &s->async->cmd;
	struct dio200_subdev_intr *subpriv = s->private;
	unsigned long flags;

	spin_lock_irqsave(&subpriv->spinlock, flags);

	subpriv->active = true;
	subpriv->stopcount = cmd->stop_arg;

	if (cmd->start_src == TRIG_INT)
		s->async->inttrig = dio200_inttrig_start_intr;
	else	/* TRIG_NOW */
		dio200_start_intr(dev, s);

	spin_unlock_irqrestore(&subpriv->spinlock, flags);

	return 0;
}

static int dio200_subdev_intr_init(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   unsigned int offset,
				   unsigned valid_isns)
{
	const struct dio200_board *board = dev->board_ptr;
	struct dio200_subdev_intr *subpriv;

	subpriv = comedi_alloc_spriv(s, sizeof(*subpriv));
	if (!subpriv)
		return -ENOMEM;

	subpriv->ofs = offset;
	subpriv->valid_isns = valid_isns;
	spin_lock_init(&subpriv->spinlock);

	if (board->has_int_sce)
		/* Disable interrupt sources. */
		dio200_write8(dev, subpriv->ofs, 0);

	s->type = COMEDI_SUBD_DI;
	s->subdev_flags = SDF_READABLE | SDF_CMD_READ;
	if (board->has_int_sce) {
		s->n_chan = DIO200_MAX_ISNS;
		s->len_chanlist = DIO200_MAX_ISNS;
	} else {
		/* No interrupt source register.  Support single channel. */
		s->n_chan = 1;
		s->len_chanlist = 1;
	}
	s->range_table = &range_digital;
	s->maxdata = 1;
	s->insn_bits = dio200_subdev_intr_insn_bits;
	s->do_cmdtest = dio200_subdev_intr_cmdtest;
	s->do_cmd = dio200_subdev_intr_cmd;
	s->cancel = dio200_subdev_intr_cancel;

	return 0;
}

static irqreturn_t dio200_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct comedi_subdevice *s = dev->read_subdev;
	int handled;

	if (!dev->attached)
		return IRQ_NONE;

	handled = dio200_handle_read_intr(dev, s);

	return IRQ_RETVAL(handled);
}

static unsigned int dio200_subdev_8254_read_chan(struct comedi_device *dev,
						 struct comedi_subdevice *s,
						 unsigned int chan)
{
	struct dio200_subdev_8254 *subpriv = s->private;
	unsigned int val;

	/* latch counter */
	val = chan << 6;
	dio200_write8(dev, subpriv->ofs + i8254_control_reg, val);
	/* read lsb, msb */
	val = dio200_read8(dev, subpriv->ofs + chan);
	val += dio200_read8(dev, subpriv->ofs + chan) << 8;
	return val;
}

static void dio200_subdev_8254_write_chan(struct comedi_device *dev,
					  struct comedi_subdevice *s,
					  unsigned int chan,
					  unsigned int count)
{
	struct dio200_subdev_8254 *subpriv = s->private;

	/* write lsb, msb */
	dio200_write8(dev, subpriv->ofs + chan, count & 0xff);
	dio200_write8(dev, subpriv->ofs + chan, (count >> 8) & 0xff);
}

static void dio200_subdev_8254_set_mode(struct comedi_device *dev,
					struct comedi_subdevice *s,
					unsigned int chan,
					unsigned int mode)
{
	struct dio200_subdev_8254 *subpriv = s->private;
	unsigned int byte;

	byte = chan << 6;
	byte |= 0x30;		/* access order: lsb, msb */
	byte |= (mode & 0xf);	/* counter mode and BCD|binary */
	dio200_write8(dev, subpriv->ofs + i8254_control_reg, byte);
}

static unsigned int dio200_subdev_8254_status(struct comedi_device *dev,
					      struct comedi_subdevice *s,
					      unsigned int chan)
{
	struct dio200_subdev_8254 *subpriv = s->private;

	/* latch status */
	dio200_write8(dev, subpriv->ofs + i8254_control_reg,
		      0xe0 | (2 << chan));
	/* read status */
	return dio200_read8(dev, subpriv->ofs + chan);
}

static int dio200_subdev_8254_read(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn,
				   unsigned int *data)
{
	struct dio200_subdev_8254 *subpriv = s->private;
	int chan = CR_CHAN(insn->chanspec);
	unsigned int n;
	unsigned long flags;

	for (n = 0; n < insn->n; n++) {
		spin_lock_irqsave(&subpriv->spinlock, flags);
		data[n] = dio200_subdev_8254_read_chan(dev, s, chan);
		spin_unlock_irqrestore(&subpriv->spinlock, flags);
	}
	return insn->n;
}

static int dio200_subdev_8254_write(struct comedi_device *dev,
				    struct comedi_subdevice *s,
				    struct comedi_insn *insn,
				    unsigned int *data)
{
	struct dio200_subdev_8254 *subpriv = s->private;
	int chan = CR_CHAN(insn->chanspec);
	unsigned int n;
	unsigned long flags;

	for (n = 0; n < insn->n; n++) {
		spin_lock_irqsave(&subpriv->spinlock, flags);
		dio200_subdev_8254_write_chan(dev, s, chan, data[n]);
		spin_unlock_irqrestore(&subpriv->spinlock, flags);
	}
	return insn->n;
}

static int dio200_subdev_8254_set_gate_src(struct comedi_device *dev,
					   struct comedi_subdevice *s,
					   unsigned int counter_number,
					   unsigned int gate_src)
{
	const struct dio200_board *board = dev->board_ptr;
	struct dio200_subdev_8254 *subpriv = s->private;
	unsigned char byte;

	if (!board->has_clk_gat_sce)
		return -1;
	if (counter_number > 2)
		return -1;
	if (gate_src > (board->is_pcie ? 31 : 7))
		return -1;

	subpriv->gate_src[counter_number] = gate_src;
	byte = gat_sce(subpriv->which, counter_number, gate_src);
	dio200_write8(dev, subpriv->gat_sce_ofs, byte);

	return 0;
}

static int dio200_subdev_8254_get_gate_src(struct comedi_device *dev,
					   struct comedi_subdevice *s,
					   unsigned int counter_number)
{
	const struct dio200_board *board = dev->board_ptr;
	struct dio200_subdev_8254 *subpriv = s->private;

	if (!board->has_clk_gat_sce)
		return -1;
	if (counter_number > 2)
		return -1;

	return subpriv->gate_src[counter_number];
}

static int dio200_subdev_8254_set_clock_src(struct comedi_device *dev,
					    struct comedi_subdevice *s,
					    unsigned int counter_number,
					    unsigned int clock_src)
{
	const struct dio200_board *board = dev->board_ptr;
	struct dio200_subdev_8254 *subpriv = s->private;
	unsigned char byte;

	if (!board->has_clk_gat_sce)
		return -1;
	if (counter_number > 2)
		return -1;
	if (clock_src > (board->is_pcie ? 31 : 7))
		return -1;

	subpriv->clock_src[counter_number] = clock_src;
	byte = clk_sce(subpriv->which, counter_number, clock_src);
	dio200_write8(dev, subpriv->clk_sce_ofs, byte);

	return 0;
}

static int dio200_subdev_8254_get_clock_src(struct comedi_device *dev,
					    struct comedi_subdevice *s,
					    unsigned int counter_number,
					    unsigned int *period_ns)
{
	const struct dio200_board *board = dev->board_ptr;
	struct dio200_subdev_8254 *subpriv = s->private;
	unsigned clock_src;

	if (!board->has_clk_gat_sce)
		return -1;
	if (counter_number > 2)
		return -1;

	clock_src = subpriv->clock_src[counter_number];
	*period_ns = clock_period[clock_src];
	return clock_src;
}

static int dio200_subdev_8254_config(struct comedi_device *dev,
				     struct comedi_subdevice *s,
				     struct comedi_insn *insn,
				     unsigned int *data)
{
	struct dio200_subdev_8254 *subpriv = s->private;
	int ret = 0;
	int chan = CR_CHAN(insn->chanspec);
	unsigned long flags;

	spin_lock_irqsave(&subpriv->spinlock, flags);
	switch (data[0]) {
	case INSN_CONFIG_SET_COUNTER_MODE:
		if (data[1] > (I8254_MODE5 | I8254_BCD))
			ret = -EINVAL;
		else
			dio200_subdev_8254_set_mode(dev, s, chan, data[1]);
		break;
	case INSN_CONFIG_8254_READ_STATUS:
		data[1] = dio200_subdev_8254_status(dev, s, chan);
		break;
	case INSN_CONFIG_SET_GATE_SRC:
		ret = dio200_subdev_8254_set_gate_src(dev, s, chan, data[2]);
		if (ret < 0)
			ret = -EINVAL;
		break;
	case INSN_CONFIG_GET_GATE_SRC:
		ret = dio200_subdev_8254_get_gate_src(dev, s, chan);
		if (ret < 0) {
			ret = -EINVAL;
			break;
		}
		data[2] = ret;
		break;
	case INSN_CONFIG_SET_CLOCK_SRC:
		ret = dio200_subdev_8254_set_clock_src(dev, s, chan, data[1]);
		if (ret < 0)
			ret = -EINVAL;
		break;
	case INSN_CONFIG_GET_CLOCK_SRC:
		ret = dio200_subdev_8254_get_clock_src(dev, s, chan, &data[2]);
		if (ret < 0) {
			ret = -EINVAL;
			break;
		}
		data[1] = ret;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	spin_unlock_irqrestore(&subpriv->spinlock, flags);
	return ret < 0 ? ret : insn->n;
}

static int dio200_subdev_8254_init(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   unsigned int offset)
{
	const struct dio200_board *board = dev->board_ptr;
	struct dio200_subdev_8254 *subpriv;
	unsigned int chan;

	subpriv = comedi_alloc_spriv(s, sizeof(*subpriv));
	if (!subpriv)
		return -ENOMEM;

	s->type = COMEDI_SUBD_COUNTER;
	s->subdev_flags = SDF_WRITABLE | SDF_READABLE;
	s->n_chan = 3;
	s->maxdata = 0xFFFF;
	s->insn_read = dio200_subdev_8254_read;
	s->insn_write = dio200_subdev_8254_write;
	s->insn_config = dio200_subdev_8254_config;

	spin_lock_init(&subpriv->spinlock);
	subpriv->ofs = offset;
	if (board->has_clk_gat_sce) {
		/* Derive CLK_SCE and GAT_SCE register offsets from
		 * 8254 offset. */
		subpriv->clk_sce_ofs = DIO200_XCLK_SCE + (offset >> 3);
		subpriv->gat_sce_ofs = DIO200_XGAT_SCE + (offset >> 3);
		subpriv->which = (offset >> 2) & 1;
	}

	/* Initialize channels. */
	for (chan = 0; chan < 3; chan++) {
		dio200_subdev_8254_set_mode(dev, s, chan,
					    I8254_MODE0 | I8254_BINARY);
		if (board->has_clk_gat_sce) {
			/* Gate source 0 is VCC (logic 1). */
			dio200_subdev_8254_set_gate_src(dev, s, chan, 0);
			/* Clock source 0 is the dedicated clock input. */
			dio200_subdev_8254_set_clock_src(dev, s, chan, 0);
		}
	}

	return 0;
}

static void dio200_subdev_8255_set_dir(struct comedi_device *dev,
				       struct comedi_subdevice *s)
{
	struct dio200_subdev_8255 *subpriv = s->private;
	int config;

	config = I8255_CTRL_CW;
	/* 1 in io_bits indicates output, 1 in config indicates input */
	if (!(s->io_bits & 0x0000ff))
		config |= I8255_CTRL_A_IO;
	if (!(s->io_bits & 0x00ff00))
		config |= I8255_CTRL_B_IO;
	if (!(s->io_bits & 0x0f0000))
		config |= I8255_CTRL_C_LO_IO;
	if (!(s->io_bits & 0xf00000))
		config |= I8255_CTRL_C_HI_IO;
	dio200_write8(dev, subpriv->ofs + I8255_CTRL_REG, config);
}

static int dio200_subdev_8255_bits(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn,
				   unsigned int *data)
{
	struct dio200_subdev_8255 *subpriv = s->private;
	unsigned int mask;
	unsigned int val;

	mask = comedi_dio_update_state(s, data);
	if (mask) {
		if (mask & 0xff)
			dio200_write8(dev, subpriv->ofs + I8255_DATA_A_REG,
				      s->state & 0xff);
		if (mask & 0xff00)
			dio200_write8(dev, subpriv->ofs + I8255_DATA_B_REG,
				      (s->state >> 8) & 0xff);
		if (mask & 0xff0000)
			dio200_write8(dev, subpriv->ofs + I8255_DATA_C_REG,
				      (s->state >> 16) & 0xff);
	}

	val = dio200_read8(dev, subpriv->ofs + I8255_DATA_A_REG);
	val |= dio200_read8(dev, subpriv->ofs + I8255_DATA_B_REG) << 8;
	val |= dio200_read8(dev, subpriv->ofs + I8255_DATA_C_REG) << 16;

	data[1] = val;

	return insn->n;
}

static int dio200_subdev_8255_config(struct comedi_device *dev,
				     struct comedi_subdevice *s,
				     struct comedi_insn *insn,
				     unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int mask;
	int ret;

	if (chan < 8)
		mask = 0x0000ff;
	else if (chan < 16)
		mask = 0x00ff00;
	else if (chan < 20)
		mask = 0x0f0000;
	else
		mask = 0xf00000;

	ret = comedi_dio_insn_config(dev, s, insn, data, mask);
	if (ret)
		return ret;

	dio200_subdev_8255_set_dir(dev, s);

	return insn->n;
}

static int dio200_subdev_8255_init(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   unsigned int offset)
{
	struct dio200_subdev_8255 *subpriv;

	subpriv = comedi_alloc_spriv(s, sizeof(*subpriv));
	if (!subpriv)
		return -ENOMEM;

	subpriv->ofs = offset;

	s->type = COMEDI_SUBD_DIO;
	s->subdev_flags = SDF_READABLE | SDF_WRITABLE;
	s->n_chan = 24;
	s->range_table = &range_digital;
	s->maxdata = 1;
	s->insn_bits = dio200_subdev_8255_bits;
	s->insn_config = dio200_subdev_8255_config;
	dio200_subdev_8255_set_dir(dev, s);
	return 0;
}

static int dio200_subdev_timer_read(struct comedi_device *dev,
				    struct comedi_subdevice *s,
				    struct comedi_insn *insn,
				    unsigned int *data)
{
	unsigned int n;

	for (n = 0; n < insn->n; n++)
		data[n] = dio200_read32(dev, DIO200_TS_COUNT);
	return n;
}

static void dio200_subdev_timer_reset(struct comedi_device *dev,
				      struct comedi_subdevice *s)
{
	unsigned int clock;

	clock = dio200_read32(dev, DIO200_TS_CONFIG) & TS_CONFIG_CLK_SRC_MASK;
	dio200_write32(dev, DIO200_TS_CONFIG, clock | TS_CONFIG_RESET);
	dio200_write32(dev, DIO200_TS_CONFIG, clock);
}

static void dio200_subdev_timer_get_clock_src(struct comedi_device *dev,
					      struct comedi_subdevice *s,
					      unsigned int *src,
					      unsigned int *period)
{
	unsigned int clk;

	clk = dio200_read32(dev, DIO200_TS_CONFIG) & TS_CONFIG_CLK_SRC_MASK;
	*src = clk;
	*period = (clk < ARRAY_SIZE(ts_clock_period)) ?
		  ts_clock_period[clk] : 0;
}

static int dio200_subdev_timer_set_clock_src(struct comedi_device *dev,
					     struct comedi_subdevice *s,
					     unsigned int src)
{
	if (src > TS_CONFIG_MAX_CLK_SRC)
		return -EINVAL;
	dio200_write32(dev, DIO200_TS_CONFIG, src);
	return 0;
}

static int dio200_subdev_timer_config(struct comedi_device *dev,
				      struct comedi_subdevice *s,
				      struct comedi_insn *insn,
				      unsigned int *data)
{
	int ret = 0;

	switch (data[0]) {
	case INSN_CONFIG_RESET:
		dio200_subdev_timer_reset(dev, s);
		break;
	case INSN_CONFIG_SET_CLOCK_SRC:
		ret = dio200_subdev_timer_set_clock_src(dev, s, data[1]);
		if (ret < 0)
			ret = -EINVAL;
		break;
	case INSN_CONFIG_GET_CLOCK_SRC:
		dio200_subdev_timer_get_clock_src(dev, s, &data[1], &data[2]);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret < 0 ? ret : insn->n;
}

void amplc_dio200_set_enhance(struct comedi_device *dev, unsigned char val)
{
	dio200_write8(dev, DIO200_ENHANCE, val);
}
EXPORT_SYMBOL_GPL(amplc_dio200_set_enhance);

int amplc_dio200_common_attach(struct comedi_device *dev, unsigned int irq,
			       unsigned long req_irq_flags)
{
	const struct dio200_board *board = dev->board_ptr;
	struct comedi_subdevice *s;
	unsigned int n;
	int ret;

	ret = comedi_alloc_subdevices(dev, board->n_subdevs);
	if (ret)
		return ret;

	for (n = 0; n < dev->n_subdevices; n++) {
		s = &dev->subdevices[n];
		switch (board->sdtype[n]) {
		case sd_8254:
			/* counter subdevice (8254) */
			ret = dio200_subdev_8254_init(dev, s,
						      board->sdinfo[n]);
			if (ret < 0)
				return ret;
			break;
		case sd_8255:
			/* digital i/o subdevice (8255) */
			ret = dio200_subdev_8255_init(dev, s,
						      board->sdinfo[n]);
			if (ret < 0)
				return ret;
			break;
		case sd_intr:
			/* 'INTERRUPT' subdevice */
			if (irq && !dev->read_subdev) {
				ret = dio200_subdev_intr_init(dev, s,
							      DIO200_INT_SCE,
							      board->sdinfo[n]);
				if (ret < 0)
					return ret;
				dev->read_subdev = s;
			} else {
				s->type = COMEDI_SUBD_UNUSED;
			}
			break;
		case sd_timer:
			s->type		= COMEDI_SUBD_TIMER;
			s->subdev_flags	= SDF_READABLE | SDF_LSAMPL;
			s->n_chan	= 1;
			s->maxdata	= 0xffffffff;
			s->insn_read	= dio200_subdev_timer_read;
			s->insn_config	= dio200_subdev_timer_config;
			break;
		default:
			s->type = COMEDI_SUBD_UNUSED;
			break;
		}
	}

	if (irq && dev->read_subdev) {
		if (request_irq(irq, dio200_interrupt, req_irq_flags,
				dev->board_name, dev) >= 0) {
			dev->irq = irq;
		} else {
			dev_warn(dev->class_dev,
				 "warning! irq %u unavailable!\n", irq);
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(amplc_dio200_common_attach);

static int __init amplc_dio200_common_init(void)
{
	return 0;
}
module_init(amplc_dio200_common_init);

static void __exit amplc_dio200_common_exit(void)
{
}
module_exit(amplc_dio200_common_exit);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi helper for amplc_dio200 and amplc_dio200_pci");
MODULE_LICENSE("GPL");
