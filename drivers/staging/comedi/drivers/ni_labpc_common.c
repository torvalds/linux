// SPDX-License-Identifier: GPL-2.0+
/*
 * comedi/drivers/ni_labpc_common.c
 *
 * Common support code for "ni_labpc", "ni_labpc_pci" and "ni_labpc_cs".
 *
 * Copyright (C) 2001-2003 Frank Mori Hess <fmhess@users.sourceforge.net>
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/slab.h>

#include "../comedidev.h"

#include "comedi_8254.h"
#include "8255.h"
#include "ni_labpc.h"
#include "ni_labpc_regs.h"
#include "ni_labpc_isadma.h"

enum scan_mode {
	MODE_SINGLE_CHAN,
	MODE_SINGLE_CHAN_INTERVAL,
	MODE_MULT_CHAN_UP,
	MODE_MULT_CHAN_DOWN,
};

static const struct comedi_lrange range_labpc_plus_ai = {
	16, {
		BIP_RANGE(5),
		BIP_RANGE(4),
		BIP_RANGE(2.5),
		BIP_RANGE(1),
		BIP_RANGE(0.5),
		BIP_RANGE(0.25),
		BIP_RANGE(0.1),
		BIP_RANGE(0.05),
		UNI_RANGE(10),
		UNI_RANGE(8),
		UNI_RANGE(5),
		UNI_RANGE(2),
		UNI_RANGE(1),
		UNI_RANGE(0.5),
		UNI_RANGE(0.2),
		UNI_RANGE(0.1)
	}
};

static const struct comedi_lrange range_labpc_1200_ai = {
	14, {
		BIP_RANGE(5),
		BIP_RANGE(2.5),
		BIP_RANGE(1),
		BIP_RANGE(0.5),
		BIP_RANGE(0.25),
		BIP_RANGE(0.1),
		BIP_RANGE(0.05),
		UNI_RANGE(10),
		UNI_RANGE(5),
		UNI_RANGE(2),
		UNI_RANGE(1),
		UNI_RANGE(0.5),
		UNI_RANGE(0.2),
		UNI_RANGE(0.1)
	}
};

static const struct comedi_lrange range_labpc_ao = {
	2, {
		BIP_RANGE(5),
		UNI_RANGE(10)
	}
};

/*
 * functions that do inb/outb and readb/writeb so we can use
 * function pointers to decide which to use
 */
static unsigned int labpc_inb(struct comedi_device *dev, unsigned long reg)
{
	return inb(dev->iobase + reg);
}

static void labpc_outb(struct comedi_device *dev,
		       unsigned int byte, unsigned long reg)
{
	outb(byte, dev->iobase + reg);
}

static unsigned int labpc_readb(struct comedi_device *dev, unsigned long reg)
{
	return readb(dev->mmio + reg);
}

static void labpc_writeb(struct comedi_device *dev,
			 unsigned int byte, unsigned long reg)
{
	writeb(byte, dev->mmio + reg);
}

static int labpc_cancel(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct labpc_private *devpriv = dev->private;
	unsigned long flags;

	spin_lock_irqsave(&dev->spinlock, flags);
	devpriv->cmd2 &= ~(CMD2_SWTRIG | CMD2_HWTRIG | CMD2_PRETRIG);
	devpriv->write_byte(dev, devpriv->cmd2, CMD2_REG);
	spin_unlock_irqrestore(&dev->spinlock, flags);

	devpriv->cmd3 = 0;
	devpriv->write_byte(dev, devpriv->cmd3, CMD3_REG);

	return 0;
}

static void labpc_ai_set_chan_and_gain(struct comedi_device *dev,
				       enum scan_mode mode,
				       unsigned int chan,
				       unsigned int range,
				       unsigned int aref)
{
	const struct labpc_boardinfo *board = dev->board_ptr;
	struct labpc_private *devpriv = dev->private;

	if (board->is_labpc1200) {
		/*
		 * The LabPC-1200 boards do not have a gain
		 * of '0x10'. Skip the range values that would
		 * result in this gain.
		 */
		range += (range > 0) + (range > 7);
	}

	/* munge channel bits for differential/scan disabled mode */
	if ((mode == MODE_SINGLE_CHAN || mode == MODE_SINGLE_CHAN_INTERVAL) &&
	    aref == AREF_DIFF)
		chan *= 2;
	devpriv->cmd1 = CMD1_MA(chan);
	devpriv->cmd1 |= CMD1_GAIN(range);

	devpriv->write_byte(dev, devpriv->cmd1, CMD1_REG);
}

static void labpc_setup_cmd6_reg(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 enum scan_mode mode,
				 enum transfer_type xfer,
				 unsigned int range,
				 unsigned int aref,
				 bool ena_intr)
{
	const struct labpc_boardinfo *board = dev->board_ptr;
	struct labpc_private *devpriv = dev->private;

	if (!board->is_labpc1200)
		return;

	/* reference inputs to ground or common? */
	if (aref != AREF_GROUND)
		devpriv->cmd6 |= CMD6_NRSE;
	else
		devpriv->cmd6 &= ~CMD6_NRSE;

	/* bipolar or unipolar range? */
	if (comedi_range_is_unipolar(s, range))
		devpriv->cmd6 |= CMD6_ADCUNI;
	else
		devpriv->cmd6 &= ~CMD6_ADCUNI;

	/*  interrupt on fifo half full? */
	if (xfer == fifo_half_full_transfer)
		devpriv->cmd6 |= CMD6_HFINTEN;
	else
		devpriv->cmd6 &= ~CMD6_HFINTEN;

	/* enable interrupt on counter a1 terminal count? */
	if (ena_intr)
		devpriv->cmd6 |= CMD6_DQINTEN;
	else
		devpriv->cmd6 &= ~CMD6_DQINTEN;

	/* are we scanning up or down through channels? */
	if (mode == MODE_MULT_CHAN_UP)
		devpriv->cmd6 |= CMD6_SCANUP;
	else
		devpriv->cmd6 &= ~CMD6_SCANUP;

	devpriv->write_byte(dev, devpriv->cmd6, CMD6_REG);
}

static unsigned int labpc_read_adc_fifo(struct comedi_device *dev)
{
	struct labpc_private *devpriv = dev->private;
	unsigned int lsb = devpriv->read_byte(dev, ADC_FIFO_REG);
	unsigned int msb = devpriv->read_byte(dev, ADC_FIFO_REG);

	return (msb << 8) | lsb;
}

static void labpc_clear_adc_fifo(struct comedi_device *dev)
{
	struct labpc_private *devpriv = dev->private;

	devpriv->write_byte(dev, 0x1, ADC_FIFO_CLEAR_REG);
	labpc_read_adc_fifo(dev);
}

static int labpc_ai_eoc(struct comedi_device *dev,
			struct comedi_subdevice *s,
			struct comedi_insn *insn,
			unsigned long context)
{
	struct labpc_private *devpriv = dev->private;

	devpriv->stat1 = devpriv->read_byte(dev, STAT1_REG);
	if (devpriv->stat1 & STAT1_DAVAIL)
		return 0;
	return -EBUSY;
}

static int labpc_ai_insn_read(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn,
			      unsigned int *data)
{
	struct labpc_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int range = CR_RANGE(insn->chanspec);
	unsigned int aref = CR_AREF(insn->chanspec);
	int ret;
	int i;

	/* disable timed conversions, interrupt generation and dma */
	labpc_cancel(dev, s);

	labpc_ai_set_chan_and_gain(dev, MODE_SINGLE_CHAN, chan, range, aref);

	labpc_setup_cmd6_reg(dev, s, MODE_SINGLE_CHAN, fifo_not_empty_transfer,
			     range, aref, false);

	/* setup cmd4 register */
	devpriv->cmd4 = 0;
	devpriv->cmd4 |= CMD4_ECLKRCV;
	/* single-ended/differential */
	if (aref == AREF_DIFF)
		devpriv->cmd4 |= CMD4_SEDIFF;
	devpriv->write_byte(dev, devpriv->cmd4, CMD4_REG);

	/* initialize pacer counter to prevent any problems */
	comedi_8254_set_mode(devpriv->counter, 0, I8254_MODE2 | I8254_BINARY);

	labpc_clear_adc_fifo(dev);

	for (i = 0; i < insn->n; i++) {
		/* trigger conversion */
		devpriv->write_byte(dev, 0x1, ADC_START_CONVERT_REG);

		ret = comedi_timeout(dev, s, insn, labpc_ai_eoc, 0);
		if (ret)
			return ret;

		data[i] = labpc_read_adc_fifo(dev);
	}

	return insn->n;
}

static bool labpc_use_continuous_mode(const struct comedi_cmd *cmd,
				      enum scan_mode mode)
{
	if (mode == MODE_SINGLE_CHAN || cmd->scan_begin_src == TRIG_FOLLOW)
		return true;

	return false;
}

static unsigned int labpc_ai_convert_period(const struct comedi_cmd *cmd,
					    enum scan_mode mode)
{
	if (cmd->convert_src != TRIG_TIMER)
		return 0;

	if (mode == MODE_SINGLE_CHAN && cmd->scan_begin_src == TRIG_TIMER)
		return cmd->scan_begin_arg;

	return cmd->convert_arg;
}

static void labpc_set_ai_convert_period(struct comedi_cmd *cmd,
					enum scan_mode mode, unsigned int ns)
{
	if (cmd->convert_src != TRIG_TIMER)
		return;

	if (mode == MODE_SINGLE_CHAN &&
	    cmd->scan_begin_src == TRIG_TIMER) {
		cmd->scan_begin_arg = ns;
		if (cmd->convert_arg > cmd->scan_begin_arg)
			cmd->convert_arg = cmd->scan_begin_arg;
	} else {
		cmd->convert_arg = ns;
	}
}

static unsigned int labpc_ai_scan_period(const struct comedi_cmd *cmd,
					 enum scan_mode mode)
{
	if (cmd->scan_begin_src != TRIG_TIMER)
		return 0;

	if (mode == MODE_SINGLE_CHAN && cmd->convert_src == TRIG_TIMER)
		return 0;

	return cmd->scan_begin_arg;
}

static void labpc_set_ai_scan_period(struct comedi_cmd *cmd,
				     enum scan_mode mode, unsigned int ns)
{
	if (cmd->scan_begin_src != TRIG_TIMER)
		return;

	if (mode == MODE_SINGLE_CHAN && cmd->convert_src == TRIG_TIMER)
		return;

	cmd->scan_begin_arg = ns;
}

/* figures out what counter values to use based on command */
static void labpc_adc_timing(struct comedi_device *dev, struct comedi_cmd *cmd,
			     enum scan_mode mode)
{
	struct comedi_8254 *pacer = dev->pacer;
	unsigned int convert_period = labpc_ai_convert_period(cmd, mode);
	unsigned int scan_period = labpc_ai_scan_period(cmd, mode);
	unsigned int base_period;

	/*
	 * If both convert and scan triggers are TRIG_TIMER, then they
	 * both rely on counter b0. If only one TRIG_TIMER is used, we
	 * can use the generic cascaded timing functions.
	 */
	if (convert_period && scan_period) {
		/*
		 * pick the lowest divisor value we can (for maximum input
		 * clock speed on convert and scan counters)
		 */
		pacer->next_div1 = (scan_period - 1) /
				   (pacer->osc_base * I8254_MAX_COUNT) + 1;

		comedi_check_trigger_arg_min(&pacer->next_div1, 2);
		comedi_check_trigger_arg_max(&pacer->next_div1,
					     I8254_MAX_COUNT);

		base_period = pacer->osc_base * pacer->next_div1;

		/*  set a0 for conversion frequency and b1 for scan frequency */
		switch (cmd->flags & CMDF_ROUND_MASK) {
		default:
		case CMDF_ROUND_NEAREST:
			pacer->next_div = DIV_ROUND_CLOSEST(convert_period,
							    base_period);
			pacer->next_div2 = DIV_ROUND_CLOSEST(scan_period,
							     base_period);
			break;
		case CMDF_ROUND_UP:
			pacer->next_div = DIV_ROUND_UP(convert_period,
						       base_period);
			pacer->next_div2 = DIV_ROUND_UP(scan_period,
							base_period);
			break;
		case CMDF_ROUND_DOWN:
			pacer->next_div = convert_period / base_period;
			pacer->next_div2 = scan_period / base_period;
			break;
		}
		/*  make sure a0 and b1 values are acceptable */
		comedi_check_trigger_arg_min(&pacer->next_div, 2);
		comedi_check_trigger_arg_max(&pacer->next_div, I8254_MAX_COUNT);
		comedi_check_trigger_arg_min(&pacer->next_div2, 2);
		comedi_check_trigger_arg_max(&pacer->next_div2,
					     I8254_MAX_COUNT);

		/*  write corrected timings to command */
		labpc_set_ai_convert_period(cmd, mode,
					    base_period * pacer->next_div);
		labpc_set_ai_scan_period(cmd, mode,
					 base_period * pacer->next_div2);
	} else if (scan_period) {
		/*
		 * calculate cascaded counter values
		 * that give desired scan timing
		 * (pacer->next_div2 / pacer->next_div1)
		 */
		comedi_8254_cascade_ns_to_timer(pacer, &scan_period,
						cmd->flags);
		labpc_set_ai_scan_period(cmd, mode, scan_period);
	} else if (convert_period) {
		/*
		 * calculate cascaded counter values
		 * that give desired conversion timing
		 * (pacer->next_div / pacer->next_div1)
		 */
		comedi_8254_cascade_ns_to_timer(pacer, &convert_period,
						cmd->flags);
		/* transfer div2 value so correct timer gets updated */
		pacer->next_div = pacer->next_div2;
		labpc_set_ai_convert_period(cmd, mode, convert_period);
	}
}

static enum scan_mode labpc_ai_scan_mode(const struct comedi_cmd *cmd)
{
	unsigned int chan0;
	unsigned int chan1;

	if (cmd->chanlist_len == 1)
		return MODE_SINGLE_CHAN;

	/* chanlist may be NULL during cmdtest */
	if (!cmd->chanlist)
		return MODE_MULT_CHAN_UP;

	chan0 = CR_CHAN(cmd->chanlist[0]);
	chan1 = CR_CHAN(cmd->chanlist[1]);

	if (chan0 < chan1)
		return MODE_MULT_CHAN_UP;

	if (chan0 > chan1)
		return MODE_MULT_CHAN_DOWN;

	return MODE_SINGLE_CHAN_INTERVAL;
}

static int labpc_ai_check_chanlist(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_cmd *cmd)
{
	enum scan_mode mode = labpc_ai_scan_mode(cmd);
	unsigned int chan0 = CR_CHAN(cmd->chanlist[0]);
	unsigned int range0 = CR_RANGE(cmd->chanlist[0]);
	unsigned int aref0 = CR_AREF(cmd->chanlist[0]);
	int i;

	for (i = 0; i < cmd->chanlist_len; i++) {
		unsigned int chan = CR_CHAN(cmd->chanlist[i]);
		unsigned int range = CR_RANGE(cmd->chanlist[i]);
		unsigned int aref = CR_AREF(cmd->chanlist[i]);

		switch (mode) {
		case MODE_SINGLE_CHAN:
			break;
		case MODE_SINGLE_CHAN_INTERVAL:
			if (chan != chan0) {
				dev_dbg(dev->class_dev,
					"channel scanning order specified in chanlist is not supported by hardware\n");
				return -EINVAL;
			}
			break;
		case MODE_MULT_CHAN_UP:
			if (chan != i) {
				dev_dbg(dev->class_dev,
					"channel scanning order specified in chanlist is not supported by hardware\n");
				return -EINVAL;
			}
			break;
		case MODE_MULT_CHAN_DOWN:
			if (chan != (cmd->chanlist_len - i - 1)) {
				dev_dbg(dev->class_dev,
					"channel scanning order specified in chanlist is not supported by hardware\n");
				return -EINVAL;
			}
			break;
		}

		if (range != range0) {
			dev_dbg(dev->class_dev,
				"entries in chanlist must all have the same range\n");
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

static int labpc_ai_cmdtest(struct comedi_device *dev,
			    struct comedi_subdevice *s, struct comedi_cmd *cmd)
{
	const struct labpc_boardinfo *board = dev->board_ptr;
	int err = 0;
	int tmp, tmp2;
	unsigned int stop_mask;
	enum scan_mode mode;

	/* Step 1 : check if triggers are trivially valid */

	err |= comedi_check_trigger_src(&cmd->start_src, TRIG_NOW | TRIG_EXT);
	err |= comedi_check_trigger_src(&cmd->scan_begin_src,
					TRIG_TIMER | TRIG_FOLLOW | TRIG_EXT);
	err |= comedi_check_trigger_src(&cmd->convert_src,
					TRIG_TIMER | TRIG_EXT);
	err |= comedi_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);

	stop_mask = TRIG_COUNT | TRIG_NONE;
	if (board->is_labpc1200)
		stop_mask |= TRIG_EXT;
	err |= comedi_check_trigger_src(&cmd->stop_src, stop_mask);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= comedi_check_trigger_is_unique(cmd->start_src);
	err |= comedi_check_trigger_is_unique(cmd->scan_begin_src);
	err |= comedi_check_trigger_is_unique(cmd->convert_src);
	err |= comedi_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	/* can't have external stop and start triggers at once */
	if (cmd->start_src == TRIG_EXT && cmd->stop_src == TRIG_EXT)
		err++;

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	switch (cmd->start_src) {
	case TRIG_NOW:
		err |= comedi_check_trigger_arg_is(&cmd->start_arg, 0);
		break;
	case TRIG_EXT:
		/* start_arg value is ignored */
		break;
	}

	if (!cmd->chanlist_len)
		err |= -EINVAL;
	err |= comedi_check_trigger_arg_is(&cmd->scan_end_arg,
					   cmd->chanlist_len);

	if (cmd->convert_src == TRIG_TIMER) {
		err |= comedi_check_trigger_arg_min(&cmd->convert_arg,
						    board->ai_speed);
	}

	/* make sure scan timing is not too fast */
	if (cmd->scan_begin_src == TRIG_TIMER) {
		if (cmd->convert_src == TRIG_TIMER) {
			err |= comedi_check_trigger_arg_min(&cmd->
							    scan_begin_arg,
							    cmd->convert_arg *
							    cmd->chanlist_len);
		}
		err |= comedi_check_trigger_arg_min(&cmd->scan_begin_arg,
						    board->ai_speed *
						    cmd->chanlist_len);
	}

	switch (cmd->stop_src) {
	case TRIG_COUNT:
		err |= comedi_check_trigger_arg_min(&cmd->stop_arg, 1);
		break;
	case TRIG_NONE:
		err |= comedi_check_trigger_arg_is(&cmd->stop_arg, 0);
		break;
		/*
		 * TRIG_EXT doesn't care since it doesn't
		 * trigger off a numbered channel
		 */
	default:
		break;
	}

	if (err)
		return 3;

	/* step 4: fix up any arguments */

	tmp = cmd->convert_arg;
	tmp2 = cmd->scan_begin_arg;
	mode = labpc_ai_scan_mode(cmd);
	labpc_adc_timing(dev, cmd, mode);
	if (tmp != cmd->convert_arg || tmp2 != cmd->scan_begin_arg)
		err++;

	if (err)
		return 4;

	/* Step 5: check channel list if it exists */
	if (cmd->chanlist && cmd->chanlist_len > 0)
		err |= labpc_ai_check_chanlist(dev, s, cmd);

	if (err)
		return 5;

	return 0;
}

static int labpc_ai_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	const struct labpc_boardinfo *board = dev->board_ptr;
	struct labpc_private *devpriv = dev->private;
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;
	enum scan_mode mode = labpc_ai_scan_mode(cmd);
	unsigned int chanspec = (mode == MODE_MULT_CHAN_UP) ?
				cmd->chanlist[cmd->chanlist_len - 1] :
				cmd->chanlist[0];
	unsigned int chan = CR_CHAN(chanspec);
	unsigned int range = CR_RANGE(chanspec);
	unsigned int aref = CR_AREF(chanspec);
	enum transfer_type xfer;
	unsigned long flags;

	/* make sure board is disabled before setting up acquisition */
	labpc_cancel(dev, s);

	/*  initialize software conversion count */
	if (cmd->stop_src == TRIG_COUNT)
		devpriv->count = cmd->stop_arg * cmd->chanlist_len;

	/*  setup hardware conversion counter */
	if (cmd->stop_src == TRIG_EXT) {
		/*
		 * load counter a1 with count of 3
		 * (pc+ manual says this is minimum allowed) using mode 0
		 */
		comedi_8254_load(devpriv->counter, 1,
				 3, I8254_MODE0 | I8254_BINARY);
	} else	{
		/* just put counter a1 in mode 0 to set its output low */
		comedi_8254_set_mode(devpriv->counter, 1,
				     I8254_MODE0 | I8254_BINARY);
	}

	/* figure out what method we will use to transfer data */
	if (devpriv->dma &&
	    (cmd->flags & (CMDF_WAKE_EOS | CMDF_PRIORITY)) == 0) {
		/*
		 * dma unsafe at RT priority,
		 * and too much setup time for CMDF_WAKE_EOS
		 */
		xfer = isa_dma_transfer;
	} else if (board->is_labpc1200 &&
		   (cmd->flags & CMDF_WAKE_EOS) == 0 &&
		   (cmd->stop_src != TRIG_COUNT || devpriv->count > 256)) {
		/*
		 * pc-plus has no fifo-half full interrupt
		 * wake-end-of-scan should interrupt on fifo not empty
		 * make sure we are taking more than just a few points
		 */
		xfer = fifo_half_full_transfer;
	} else {
		xfer = fifo_not_empty_transfer;
	}
	devpriv->current_transfer = xfer;

	labpc_ai_set_chan_and_gain(dev, mode, chan, range, aref);

	labpc_setup_cmd6_reg(dev, s, mode, xfer, range, aref,
			     (cmd->stop_src == TRIG_EXT));

	/* manual says to set scan enable bit on second pass */
	if (mode == MODE_MULT_CHAN_UP || mode == MODE_MULT_CHAN_DOWN) {
		devpriv->cmd1 |= CMD1_SCANEN;
		/*
		 * Need a brief delay before enabling scan, or scan
		 * list will get screwed when you switch between
		 * scan up to scan down mode - dunno why.
		 */
		udelay(1);
		devpriv->write_byte(dev, devpriv->cmd1, CMD1_REG);
	}

	devpriv->write_byte(dev, cmd->chanlist_len, INTERVAL_COUNT_REG);
	/*  load count */
	devpriv->write_byte(dev, 0x1, INTERVAL_STROBE_REG);

	if (cmd->convert_src == TRIG_TIMER ||
	    cmd->scan_begin_src == TRIG_TIMER) {
		struct comedi_8254 *pacer = dev->pacer;
		struct comedi_8254 *counter = devpriv->counter;

		comedi_8254_update_divisors(pacer);

		/* set up pacing */
		comedi_8254_load(pacer, 0, pacer->divisor1,
				 I8254_MODE3 | I8254_BINARY);

		/* set up conversion pacing */
		comedi_8254_set_mode(counter, 0, I8254_MODE2 | I8254_BINARY);
		if (labpc_ai_convert_period(cmd, mode))
			comedi_8254_write(counter, 0, pacer->divisor);

		/* set up scan pacing */
		if (labpc_ai_scan_period(cmd, mode))
			comedi_8254_load(pacer, 1, pacer->divisor2,
					 I8254_MODE2 | I8254_BINARY);
	}

	labpc_clear_adc_fifo(dev);

	if (xfer == isa_dma_transfer)
		labpc_setup_dma(dev, s);

	/*  enable error interrupts */
	devpriv->cmd3 |= CMD3_ERRINTEN;
	/*  enable fifo not empty interrupt? */
	if (xfer == fifo_not_empty_transfer)
		devpriv->cmd3 |= CMD3_FIFOINTEN;
	devpriv->write_byte(dev, devpriv->cmd3, CMD3_REG);

	/*  setup any external triggering/pacing (cmd4 register) */
	devpriv->cmd4 = 0;
	if (cmd->convert_src != TRIG_EXT)
		devpriv->cmd4 |= CMD4_ECLKRCV;
	/*
	 * XXX should discard first scan when using interval scanning
	 * since manual says it is not synced with scan clock.
	 */
	if (!labpc_use_continuous_mode(cmd, mode)) {
		devpriv->cmd4 |= CMD4_INTSCAN;
		if (cmd->scan_begin_src == TRIG_EXT)
			devpriv->cmd4 |= CMD4_EOIRCV;
	}
	/*  single-ended/differential */
	if (aref == AREF_DIFF)
		devpriv->cmd4 |= CMD4_SEDIFF;
	devpriv->write_byte(dev, devpriv->cmd4, CMD4_REG);

	/*  startup acquisition */

	spin_lock_irqsave(&dev->spinlock, flags);

	/* use 2 cascaded counters for pacing */
	devpriv->cmd2 |= CMD2_TBSEL;

	devpriv->cmd2 &= ~(CMD2_SWTRIG | CMD2_HWTRIG | CMD2_PRETRIG);
	if (cmd->start_src == TRIG_EXT)
		devpriv->cmd2 |= CMD2_HWTRIG;
	else
		devpriv->cmd2 |= CMD2_SWTRIG;
	if (cmd->stop_src == TRIG_EXT)
		devpriv->cmd2 |= (CMD2_HWTRIG | CMD2_PRETRIG);

	devpriv->write_byte(dev, devpriv->cmd2, CMD2_REG);

	spin_unlock_irqrestore(&dev->spinlock, flags);

	return 0;
}

/* read all available samples from ai fifo */
static int labpc_drain_fifo(struct comedi_device *dev)
{
	struct labpc_private *devpriv = dev->private;
	struct comedi_async *async = dev->read_subdev->async;
	struct comedi_cmd *cmd = &async->cmd;
	unsigned short data;
	const int timeout = 10000;
	unsigned int i;

	devpriv->stat1 = devpriv->read_byte(dev, STAT1_REG);

	for (i = 0; (devpriv->stat1 & STAT1_DAVAIL) && i < timeout;
	     i++) {
		/*  quit if we have all the data we want */
		if (cmd->stop_src == TRIG_COUNT) {
			if (devpriv->count == 0)
				break;
			devpriv->count--;
		}
		data = labpc_read_adc_fifo(dev);
		comedi_buf_write_samples(dev->read_subdev, &data, 1);
		devpriv->stat1 = devpriv->read_byte(dev, STAT1_REG);
	}
	if (i == timeout) {
		dev_err(dev->class_dev, "ai timeout, fifo never empties\n");
		async->events |= COMEDI_CB_ERROR;
		return -1;
	}

	return 0;
}

/*
 * Makes sure all data acquired by board is transferred to comedi (used
 * when acquisition is terminated by stop_src == TRIG_EXT).
 */
static void labpc_drain_dregs(struct comedi_device *dev)
{
	struct labpc_private *devpriv = dev->private;

	if (devpriv->current_transfer == isa_dma_transfer)
		labpc_drain_dma(dev);

	labpc_drain_fifo(dev);
}

/* interrupt service routine */
static irqreturn_t labpc_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	const struct labpc_boardinfo *board = dev->board_ptr;
	struct labpc_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	struct comedi_async *async;
	struct comedi_cmd *cmd;

	if (!dev->attached) {
		dev_err(dev->class_dev, "premature interrupt\n");
		return IRQ_HANDLED;
	}

	async = s->async;
	cmd = &async->cmd;

	/* read board status */
	devpriv->stat1 = devpriv->read_byte(dev, STAT1_REG);
	if (board->is_labpc1200)
		devpriv->stat2 = devpriv->read_byte(dev, STAT2_REG);

	if ((devpriv->stat1 & (STAT1_GATA0 | STAT1_CNTINT | STAT1_OVERFLOW |
			       STAT1_OVERRUN | STAT1_DAVAIL)) == 0 &&
	    (devpriv->stat2 & STAT2_OUTA1) == 0 &&
	    (devpriv->stat2 & STAT2_FIFONHF)) {
		return IRQ_NONE;
	}

	if (devpriv->stat1 & STAT1_OVERRUN) {
		/* clear error interrupt */
		devpriv->write_byte(dev, 0x1, ADC_FIFO_CLEAR_REG);
		async->events |= COMEDI_CB_ERROR;
		comedi_handle_events(dev, s);
		dev_err(dev->class_dev, "overrun\n");
		return IRQ_HANDLED;
	}

	if (devpriv->current_transfer == isa_dma_transfer)
		labpc_handle_dma_status(dev);
	else
		labpc_drain_fifo(dev);

	if (devpriv->stat1 & STAT1_CNTINT) {
		dev_err(dev->class_dev, "handled timer interrupt?\n");
		/*  clear it */
		devpriv->write_byte(dev, 0x1, TIMER_CLEAR_REG);
	}

	if (devpriv->stat1 & STAT1_OVERFLOW) {
		/*  clear error interrupt */
		devpriv->write_byte(dev, 0x1, ADC_FIFO_CLEAR_REG);
		async->events |= COMEDI_CB_ERROR;
		comedi_handle_events(dev, s);
		dev_err(dev->class_dev, "overflow\n");
		return IRQ_HANDLED;
	}
	/*  handle external stop trigger */
	if (cmd->stop_src == TRIG_EXT) {
		if (devpriv->stat2 & STAT2_OUTA1) {
			labpc_drain_dregs(dev);
			async->events |= COMEDI_CB_EOA;
		}
	}

	/* TRIG_COUNT end of acquisition */
	if (cmd->stop_src == TRIG_COUNT) {
		if (devpriv->count == 0)
			async->events |= COMEDI_CB_EOA;
	}

	comedi_handle_events(dev, s);
	return IRQ_HANDLED;
}

static void labpc_ao_write(struct comedi_device *dev,
			   struct comedi_subdevice *s,
			   unsigned int chan, unsigned int val)
{
	struct labpc_private *devpriv = dev->private;

	devpriv->write_byte(dev, val & 0xff, DAC_LSB_REG(chan));
	devpriv->write_byte(dev, (val >> 8) & 0xff, DAC_MSB_REG(chan));

	s->readback[chan] = val;
}

static int labpc_ao_insn_write(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	const struct labpc_boardinfo *board = dev->board_ptr;
	struct labpc_private *devpriv = dev->private;
	unsigned int channel;
	unsigned int range;
	unsigned int i;
	unsigned long flags;

	channel = CR_CHAN(insn->chanspec);

	/*
	 * Turn off pacing of analog output channel.
	 * NOTE: hardware bug in daqcard-1200 means pacing cannot
	 * be independently enabled/disabled for its the two channels.
	 */
	spin_lock_irqsave(&dev->spinlock, flags);
	devpriv->cmd2 &= ~CMD2_LDAC(channel);
	devpriv->write_byte(dev, devpriv->cmd2, CMD2_REG);
	spin_unlock_irqrestore(&dev->spinlock, flags);

	/* set range */
	if (board->is_labpc1200) {
		range = CR_RANGE(insn->chanspec);
		if (comedi_range_is_unipolar(s, range))
			devpriv->cmd6 |= CMD6_DACUNI(channel);
		else
			devpriv->cmd6 &= ~CMD6_DACUNI(channel);
		/*  write to register */
		devpriv->write_byte(dev, devpriv->cmd6, CMD6_REG);
	}
	/* send data */
	for (i = 0; i < insn->n; i++)
		labpc_ao_write(dev, s, channel, data[i]);

	return insn->n;
}

/* lowlevel write to eeprom/dac */
static void labpc_serial_out(struct comedi_device *dev, unsigned int value,
			     unsigned int value_width)
{
	struct labpc_private *devpriv = dev->private;
	int i;

	for (i = 1; i <= value_width; i++) {
		/*  clear serial clock */
		devpriv->cmd5 &= ~CMD5_SCLK;
		/*  send bits most significant bit first */
		if (value & (1 << (value_width - i)))
			devpriv->cmd5 |= CMD5_SDATA;
		else
			devpriv->cmd5 &= ~CMD5_SDATA;
		udelay(1);
		devpriv->write_byte(dev, devpriv->cmd5, CMD5_REG);
		/*  set clock to load bit */
		devpriv->cmd5 |= CMD5_SCLK;
		udelay(1);
		devpriv->write_byte(dev, devpriv->cmd5, CMD5_REG);
	}
}

/* lowlevel read from eeprom */
static unsigned int labpc_serial_in(struct comedi_device *dev)
{
	struct labpc_private *devpriv = dev->private;
	unsigned int value = 0;
	int i;
	const int value_width = 8;	/*  number of bits wide values are */

	for (i = 1; i <= value_width; i++) {
		/*  set serial clock */
		devpriv->cmd5 |= CMD5_SCLK;
		udelay(1);
		devpriv->write_byte(dev, devpriv->cmd5, CMD5_REG);
		/*  clear clock bit */
		devpriv->cmd5 &= ~CMD5_SCLK;
		udelay(1);
		devpriv->write_byte(dev, devpriv->cmd5, CMD5_REG);
		/*  read bits most significant bit first */
		udelay(1);
		devpriv->stat2 = devpriv->read_byte(dev, STAT2_REG);
		if (devpriv->stat2 & STAT2_PROMOUT)
			value |= 1 << (value_width - i);
	}

	return value;
}

static unsigned int labpc_eeprom_read(struct comedi_device *dev,
				      unsigned int address)
{
	struct labpc_private *devpriv = dev->private;
	unsigned int value;
	/*  bits to tell eeprom to expect a read */
	const int read_instruction = 0x3;
	/*  8 bit write lengths to eeprom */
	const int write_length = 8;

	/*  enable read/write to eeprom */
	devpriv->cmd5 &= ~CMD5_EEPROMCS;
	udelay(1);
	devpriv->write_byte(dev, devpriv->cmd5, CMD5_REG);
	devpriv->cmd5 |= (CMD5_EEPROMCS | CMD5_WRTPRT);
	udelay(1);
	devpriv->write_byte(dev, devpriv->cmd5, CMD5_REG);

	/*  send read instruction */
	labpc_serial_out(dev, read_instruction, write_length);
	/*  send 8 bit address to read from */
	labpc_serial_out(dev, address, write_length);
	/*  read result */
	value = labpc_serial_in(dev);

	/*  disable read/write to eeprom */
	devpriv->cmd5 &= ~(CMD5_EEPROMCS | CMD5_WRTPRT);
	udelay(1);
	devpriv->write_byte(dev, devpriv->cmd5, CMD5_REG);

	return value;
}

static unsigned int labpc_eeprom_read_status(struct comedi_device *dev)
{
	struct labpc_private *devpriv = dev->private;
	unsigned int value;
	const int read_status_instruction = 0x5;
	const int write_length = 8;	/*  8 bit write lengths to eeprom */

	/*  enable read/write to eeprom */
	devpriv->cmd5 &= ~CMD5_EEPROMCS;
	udelay(1);
	devpriv->write_byte(dev, devpriv->cmd5, CMD5_REG);
	devpriv->cmd5 |= (CMD5_EEPROMCS | CMD5_WRTPRT);
	udelay(1);
	devpriv->write_byte(dev, devpriv->cmd5, CMD5_REG);

	/*  send read status instruction */
	labpc_serial_out(dev, read_status_instruction, write_length);
	/*  read result */
	value = labpc_serial_in(dev);

	/*  disable read/write to eeprom */
	devpriv->cmd5 &= ~(CMD5_EEPROMCS | CMD5_WRTPRT);
	udelay(1);
	devpriv->write_byte(dev, devpriv->cmd5, CMD5_REG);

	return value;
}

static void labpc_eeprom_write(struct comedi_device *dev,
			       unsigned int address, unsigned int value)
{
	struct labpc_private *devpriv = dev->private;
	const int write_enable_instruction = 0x6;
	const int write_instruction = 0x2;
	const int write_length = 8;	/*  8 bit write lengths to eeprom */

	/*  enable read/write to eeprom */
	devpriv->cmd5 &= ~CMD5_EEPROMCS;
	udelay(1);
	devpriv->write_byte(dev, devpriv->cmd5, CMD5_REG);
	devpriv->cmd5 |= (CMD5_EEPROMCS | CMD5_WRTPRT);
	udelay(1);
	devpriv->write_byte(dev, devpriv->cmd5, CMD5_REG);

	/*  send write_enable instruction */
	labpc_serial_out(dev, write_enable_instruction, write_length);
	devpriv->cmd5 &= ~CMD5_EEPROMCS;
	udelay(1);
	devpriv->write_byte(dev, devpriv->cmd5, CMD5_REG);

	/*  send write instruction */
	devpriv->cmd5 |= CMD5_EEPROMCS;
	udelay(1);
	devpriv->write_byte(dev, devpriv->cmd5, CMD5_REG);
	labpc_serial_out(dev, write_instruction, write_length);
	/*  send 8 bit address to write to */
	labpc_serial_out(dev, address, write_length);
	/*  write value */
	labpc_serial_out(dev, value, write_length);
	devpriv->cmd5 &= ~CMD5_EEPROMCS;
	udelay(1);
	devpriv->write_byte(dev, devpriv->cmd5, CMD5_REG);

	/*  disable read/write to eeprom */
	devpriv->cmd5 &= ~(CMD5_EEPROMCS | CMD5_WRTPRT);
	udelay(1);
	devpriv->write_byte(dev, devpriv->cmd5, CMD5_REG);
}

/* writes to 8 bit calibration dacs */
static void write_caldac(struct comedi_device *dev, unsigned int channel,
			 unsigned int value)
{
	struct labpc_private *devpriv = dev->private;

	/*  clear caldac load bit and make sure we don't write to eeprom */
	devpriv->cmd5 &= ~(CMD5_CALDACLD | CMD5_EEPROMCS | CMD5_WRTPRT);
	udelay(1);
	devpriv->write_byte(dev, devpriv->cmd5, CMD5_REG);

	/*  write 4 bit channel */
	labpc_serial_out(dev, channel, 4);
	/*  write 8 bit caldac value */
	labpc_serial_out(dev, value, 8);

	/*  set and clear caldac bit to load caldac value */
	devpriv->cmd5 |= CMD5_CALDACLD;
	udelay(1);
	devpriv->write_byte(dev, devpriv->cmd5, CMD5_REG);
	devpriv->cmd5 &= ~CMD5_CALDACLD;
	udelay(1);
	devpriv->write_byte(dev, devpriv->cmd5, CMD5_REG);
}

static int labpc_calib_insn_write(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn,
				  unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);

	/*
	 * Only write the last data value to the caldac. Preceding
	 * data would be overwritten anyway.
	 */
	if (insn->n > 0) {
		unsigned int val = data[insn->n - 1];

		if (s->readback[chan] != val) {
			write_caldac(dev, chan, val);
			s->readback[chan] = val;
		}
	}

	return insn->n;
}

static int labpc_eeprom_ready(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn,
			      unsigned long context)
{
	unsigned int status;

	/* make sure there isn't already a write in progress */
	status = labpc_eeprom_read_status(dev);
	if ((status & 0x1) == 0)
		return 0;
	return -EBUSY;
}

static int labpc_eeprom_insn_write(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn,
				   unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	int ret;

	/* only allow writes to user area of eeprom */
	if (chan < 16 || chan > 127)
		return -EINVAL;

	/*
	 * Only write the last data value to the eeprom. Preceding
	 * data would be overwritten anyway.
	 */
	if (insn->n > 0) {
		unsigned int val = data[insn->n - 1];

		ret = comedi_timeout(dev, s, insn, labpc_eeprom_ready, 0);
		if (ret)
			return ret;

		labpc_eeprom_write(dev, chan, val);
		s->readback[chan] = val;
	}

	return insn->n;
}

int labpc_common_attach(struct comedi_device *dev,
			unsigned int irq, unsigned long isr_flags)
{
	const struct labpc_boardinfo *board = dev->board_ptr;
	struct labpc_private *devpriv;
	struct comedi_subdevice *s;
	int ret;
	int i;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	if (dev->mmio) {
		devpriv->read_byte = labpc_readb;
		devpriv->write_byte = labpc_writeb;
	} else {
		devpriv->read_byte = labpc_inb;
		devpriv->write_byte = labpc_outb;
	}

	/* initialize board's command registers */
	devpriv->write_byte(dev, devpriv->cmd1, CMD1_REG);
	devpriv->write_byte(dev, devpriv->cmd2, CMD2_REG);
	devpriv->write_byte(dev, devpriv->cmd3, CMD3_REG);
	devpriv->write_byte(dev, devpriv->cmd4, CMD4_REG);
	if (board->is_labpc1200) {
		devpriv->write_byte(dev, devpriv->cmd5, CMD5_REG);
		devpriv->write_byte(dev, devpriv->cmd6, CMD6_REG);
	}

	if (irq) {
		ret = request_irq(irq, labpc_interrupt, isr_flags,
				  dev->board_name, dev);
		if (ret == 0)
			dev->irq = irq;
	}

	if (dev->mmio) {
		dev->pacer = comedi_8254_mm_init(dev->mmio + COUNTER_B_BASE_REG,
						 I8254_OSC_BASE_2MHZ,
						 I8254_IO8, 0);
		devpriv->counter = comedi_8254_mm_init(dev->mmio +
						       COUNTER_A_BASE_REG,
						       I8254_OSC_BASE_2MHZ,
						       I8254_IO8, 0);
	} else {
		dev->pacer = comedi_8254_init(dev->iobase + COUNTER_B_BASE_REG,
					      I8254_OSC_BASE_2MHZ,
					      I8254_IO8, 0);
		devpriv->counter = comedi_8254_init(dev->iobase +
						    COUNTER_A_BASE_REG,
						    I8254_OSC_BASE_2MHZ,
						    I8254_IO8, 0);
	}
	if (!dev->pacer || !devpriv->counter)
		return -ENOMEM;

	ret = comedi_alloc_subdevices(dev, 5);
	if (ret)
		return ret;

	/* analog input subdevice */
	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_AI;
	s->subdev_flags	= SDF_READABLE | SDF_GROUND | SDF_COMMON | SDF_DIFF;
	s->n_chan	= 8;
	s->len_chanlist	= 8;
	s->maxdata	= 0x0fff;
	s->range_table	= board->is_labpc1200 ?
			  &range_labpc_1200_ai : &range_labpc_plus_ai;
	s->insn_read	= labpc_ai_insn_read;
	if (dev->irq) {
		dev->read_subdev = s;
		s->subdev_flags	|= SDF_CMD_READ;
		s->do_cmd	= labpc_ai_cmd;
		s->do_cmdtest	= labpc_ai_cmdtest;
		s->cancel	= labpc_cancel;
	}

	/* analog output */
	s = &dev->subdevices[1];
	if (board->has_ao) {
		s->type		= COMEDI_SUBD_AO;
		s->subdev_flags	= SDF_READABLE | SDF_WRITABLE | SDF_GROUND;
		s->n_chan	= 2;
		s->maxdata	= 0x0fff;
		s->range_table	= &range_labpc_ao;
		s->insn_write	= labpc_ao_insn_write;

		ret = comedi_alloc_subdev_readback(s);
		if (ret)
			return ret;

		/* initialize analog outputs to a known value */
		for (i = 0; i < s->n_chan; i++)
			labpc_ao_write(dev, s, i, s->maxdata / 2);
	} else {
		s->type		= COMEDI_SUBD_UNUSED;
	}

	/* 8255 dio */
	s = &dev->subdevices[2];
	if (dev->mmio)
		ret = subdev_8255_mm_init(dev, s, NULL, DIO_BASE_REG);
	else
		ret = subdev_8255_init(dev, s, NULL, DIO_BASE_REG);
	if (ret)
		return ret;

	/*  calibration subdevices for boards that have one */
	s = &dev->subdevices[3];
	if (board->is_labpc1200) {
		s->type		= COMEDI_SUBD_CALIB;
		s->subdev_flags	= SDF_READABLE | SDF_WRITABLE | SDF_INTERNAL;
		s->n_chan	= 16;
		s->maxdata	= 0xff;
		s->insn_write	= labpc_calib_insn_write;

		ret = comedi_alloc_subdev_readback(s);
		if (ret)
			return ret;

		for (i = 0; i < s->n_chan; i++) {
			write_caldac(dev, i, s->maxdata / 2);
			s->readback[i] = s->maxdata / 2;
		}
	} else {
		s->type		= COMEDI_SUBD_UNUSED;
	}

	/* EEPROM (256 bytes) */
	s = &dev->subdevices[4];
	if (board->is_labpc1200) {
		s->type		= COMEDI_SUBD_MEMORY;
		s->subdev_flags	= SDF_READABLE | SDF_WRITABLE | SDF_INTERNAL;
		s->n_chan	= 256;
		s->maxdata	= 0xff;
		s->insn_write	= labpc_eeprom_insn_write;

		ret = comedi_alloc_subdev_readback(s);
		if (ret)
			return ret;

		for (i = 0; i < s->n_chan; i++)
			s->readback[i] = labpc_eeprom_read(dev, i);
	} else {
		s->type		= COMEDI_SUBD_UNUSED;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(labpc_common_attach);

void labpc_common_detach(struct comedi_device *dev)
{
	struct labpc_private *devpriv = dev->private;

	if (devpriv)
		kfree(devpriv->counter);
}
EXPORT_SYMBOL_GPL(labpc_common_detach);

static int __init labpc_common_init(void)
{
	return 0;
}
module_init(labpc_common_init);

static void __exit labpc_common_exit(void)
{
}
module_exit(labpc_common_exit);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi helper for ni_labpc, ni_labpc_pci, ni_labpc_cs");
MODULE_LICENSE("GPL");
