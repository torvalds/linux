/*
  comedi/drivers/ni_tiocmd.c
  Command support for NI general purpose counters

  Copyright (C) 2006 Frank Mori Hess <fmhess@users.sourceforge.net>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
*/

/*
 * Module: ni_tiocmd
 * Description: National Instruments general purpose counters command support
 * Author: J.P. Mellor <jpmellor@rose-hulman.edu>,
 *         Herman.Bruyninckx@mech.kuleuven.ac.be,
 *         Wim.Meeussen@mech.kuleuven.ac.be,
 *         Klaas.Gadeyne@mech.kuleuven.ac.be,
 *         Frank Mori Hess <fmhess@users.sourceforge.net>
 * Updated: Fri, 11 Apr 2008 12:32:35 +0100
 * Status: works
 *
 * This module is not used directly by end-users.  Rather, it
 * is used by other drivers (for example ni_660x and ni_pcimio)
 * to provide command support for NI's general purpose counters.
 * It was originally split out of ni_tio.c to stop the 'ni_tio'
 * module depending on the 'mite' module.
 *
 * References:
 * DAQ 660x Register-Level Programmer Manual  (NI 370505A-01)
 * DAQ 6601/6602 User Manual (NI 322137B-01)
 * 340934b.pdf  DAQ-STC reference manual
 */

/*
TODO:
	Support use of both banks X and Y
*/

#include <linux/module.h>
#include "ni_tio_internal.h"
#include "mite.h"

static void ni_tio_configure_dma(struct ni_gpct *counter,
				 bool enable, bool read)
{
	struct ni_gpct_device *counter_dev = counter->counter_dev;
	unsigned cidx = counter->counter_index;
	unsigned mask;
	unsigned bits;

	mask = GI_READ_ACKS_IRQ | GI_WRITE_ACKS_IRQ;
	bits = 0;

	if (enable) {
		if (read)
			bits |= GI_READ_ACKS_IRQ;
		else
			bits |= GI_WRITE_ACKS_IRQ;
	}
	ni_tio_set_bits(counter, NITIO_INPUT_SEL_REG(cidx), mask, bits);

	switch (counter_dev->variant) {
	case ni_gpct_variant_e_series:
		break;
	case ni_gpct_variant_m_series:
	case ni_gpct_variant_660x:
		mask = GI_DMA_ENABLE | GI_DMA_INT_ENA | GI_DMA_WRITE;
		bits = 0;

		if (enable)
			bits |= GI_DMA_ENABLE | GI_DMA_INT_ENA;
		if (!read)
			bits |= GI_DMA_WRITE;
		ni_tio_set_bits(counter, NITIO_DMA_CFG_REG(cidx), mask, bits);
		break;
	}
}

static int ni_tio_input_inttrig(struct comedi_device *dev,
				struct comedi_subdevice *s,
				unsigned int trig_num)
{
	struct ni_gpct *counter = s->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned long flags;
	int ret = 0;

	if (trig_num != cmd->start_arg)
		return -EINVAL;

	spin_lock_irqsave(&counter->lock, flags);
	if (counter->mite_chan)
		mite_dma_arm(counter->mite_chan);
	else
		ret = -EIO;
	spin_unlock_irqrestore(&counter->lock, flags);
	if (ret < 0)
		return ret;
	ret = ni_tio_arm(counter, true, NI_GPCT_ARM_IMMEDIATE);
	s->async->inttrig = NULL;

	return ret;
}

static int ni_tio_input_cmd(struct comedi_subdevice *s)
{
	struct ni_gpct *counter = s->private;
	struct ni_gpct_device *counter_dev = counter->counter_dev;
	unsigned cidx = counter->counter_index;
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;
	int ret = 0;

	/* write alloc the entire buffer */
	comedi_buf_write_alloc(s, async->prealloc_bufsz);
	counter->mite_chan->dir = COMEDI_INPUT;
	switch (counter_dev->variant) {
	case ni_gpct_variant_m_series:
	case ni_gpct_variant_660x:
		mite_prep_dma(counter->mite_chan, 32, 32);
		break;
	case ni_gpct_variant_e_series:
		mite_prep_dma(counter->mite_chan, 16, 32);
		break;
	default:
		BUG();
		break;
	}
	ni_tio_set_bits(counter, NITIO_CMD_REG(cidx), GI_SAVE_TRACE, 0);
	ni_tio_configure_dma(counter, true, true);

	if (cmd->start_src == TRIG_INT) {
		async->inttrig = &ni_tio_input_inttrig;
	} else {	/* TRIG_NOW || TRIG_EXT || TRIG_OTHER */
		async->inttrig = NULL;
		mite_dma_arm(counter->mite_chan);

		if (cmd->start_src == TRIG_NOW)
			ret = ni_tio_arm(counter, true, NI_GPCT_ARM_IMMEDIATE);
		else if (cmd->start_src == TRIG_EXT)
			ret = ni_tio_arm(counter, true, cmd->start_arg);
	}
	return ret;
}

static int ni_tio_output_cmd(struct comedi_subdevice *s)
{
	struct ni_gpct *counter = s->private;

	dev_err(counter->counter_dev->dev->class_dev,
		"output commands not yet implemented.\n");
	return -ENOTSUPP;
}

static int ni_tio_cmd_setup(struct comedi_subdevice *s)
{
	struct comedi_cmd *cmd = &s->async->cmd;
	struct ni_gpct *counter = s->private;
	unsigned cidx = counter->counter_index;
	int set_gate_source = 0;
	unsigned gate_source;
	int retval = 0;

	if (cmd->scan_begin_src == TRIG_EXT) {
		set_gate_source = 1;
		gate_source = cmd->scan_begin_arg;
	} else if (cmd->convert_src == TRIG_EXT) {
		set_gate_source = 1;
		gate_source = cmd->convert_arg;
	}
	if (set_gate_source)
		retval = ni_tio_set_gate_src(counter, 0, gate_source);
	if (cmd->flags & CMDF_WAKE_EOS) {
		ni_tio_set_bits(counter, NITIO_INT_ENA_REG(cidx),
				GI_GATE_INTERRUPT_ENABLE(cidx),
				GI_GATE_INTERRUPT_ENABLE(cidx));
	}
	return retval;
}

int ni_tio_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct ni_gpct *counter = s->private;
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;
	int retval = 0;
	unsigned long flags;

	spin_lock_irqsave(&counter->lock, flags);
	if (!counter->mite_chan) {
		dev_err(counter->counter_dev->dev->class_dev,
			"commands only supported with DMA.  ");
		dev_err(counter->counter_dev->dev->class_dev,
			"Interrupt-driven commands not yet implemented.\n");
		retval = -EIO;
	} else {
		retval = ni_tio_cmd_setup(s);
		if (retval == 0) {
			if (cmd->flags & CMDF_WRITE)
				retval = ni_tio_output_cmd(s);
			else
				retval = ni_tio_input_cmd(s);
		}
	}
	spin_unlock_irqrestore(&counter->lock, flags);
	return retval;
}
EXPORT_SYMBOL_GPL(ni_tio_cmd);

int ni_tio_cmdtest(struct comedi_device *dev,
		   struct comedi_subdevice *s,
		   struct comedi_cmd *cmd)
{
	struct ni_gpct *counter = s->private;
	int err = 0;
	unsigned int sources;

	/* Step 1 : check if triggers are trivially valid */

	sources = TRIG_NOW | TRIG_INT | TRIG_OTHER;
	if (ni_tio_counting_mode_registers_present(counter->counter_dev))
		sources |= TRIG_EXT;
	err |= comedi_check_trigger_src(&cmd->start_src, sources);

	err |= comedi_check_trigger_src(&cmd->scan_begin_src,
					TRIG_FOLLOW | TRIG_EXT | TRIG_OTHER);
	err |= comedi_check_trigger_src(&cmd->convert_src,
					TRIG_NOW | TRIG_EXT | TRIG_OTHER);
	err |= comedi_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= comedi_check_trigger_src(&cmd->stop_src, TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= comedi_check_trigger_is_unique(cmd->start_src);
	err |= comedi_check_trigger_is_unique(cmd->scan_begin_src);
	err |= comedi_check_trigger_is_unique(cmd->convert_src);

	/* Step 2b : and mutually compatible */

	if (cmd->convert_src != TRIG_NOW && cmd->scan_begin_src != TRIG_FOLLOW)
		err |= -EINVAL;

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	switch (cmd->start_src) {
	case TRIG_NOW:
	case TRIG_INT:
	case TRIG_OTHER:
		err |= comedi_check_trigger_arg_is(&cmd->start_arg, 0);
		break;
	case TRIG_EXT:
		/* start_arg is the start_trigger passed to ni_tio_arm() */
		break;
	}

	if (cmd->scan_begin_src != TRIG_EXT)
		err |= comedi_check_trigger_arg_is(&cmd->scan_begin_arg, 0);

	if (cmd->convert_src != TRIG_EXT)
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
EXPORT_SYMBOL_GPL(ni_tio_cmdtest);

int ni_tio_cancel(struct ni_gpct *counter)
{
	unsigned cidx = counter->counter_index;
	unsigned long flags;

	ni_tio_arm(counter, false, 0);
	spin_lock_irqsave(&counter->lock, flags);
	if (counter->mite_chan)
		mite_dma_disarm(counter->mite_chan);
	spin_unlock_irqrestore(&counter->lock, flags);
	ni_tio_configure_dma(counter, false, false);

	ni_tio_set_bits(counter, NITIO_INT_ENA_REG(cidx),
			GI_GATE_INTERRUPT_ENABLE(cidx), 0x0);
	return 0;
}
EXPORT_SYMBOL_GPL(ni_tio_cancel);

	/* During buffered input counter operation for e-series, the gate
	   interrupt is acked automatically by the dma controller, due to the
	   Gi_Read/Write_Acknowledges_IRQ bits in the input select register.  */
static int should_ack_gate(struct ni_gpct *counter)
{
	unsigned long flags;
	int retval = 0;

	switch (counter->counter_dev->variant) {
	case ni_gpct_variant_m_series:
	/*  not sure if 660x really supports gate
	    interrupts (the bits are not listed
	    in register-level manual) */
	case ni_gpct_variant_660x:
		return 1;
	case ni_gpct_variant_e_series:
		spin_lock_irqsave(&counter->lock, flags);
		{
			if (!counter->mite_chan ||
			    counter->mite_chan->dir != COMEDI_INPUT ||
			    (mite_done(counter->mite_chan))) {
				retval = 1;
			}
		}
		spin_unlock_irqrestore(&counter->lock, flags);
		break;
	}
	return retval;
}

static void ni_tio_acknowledge_and_confirm(struct ni_gpct *counter,
					   int *gate_error,
					   int *tc_error,
					   int *perm_stale_data,
					   int *stale_data)
{
	unsigned cidx = counter->counter_index;
	const unsigned short gxx_status = ni_tio_read(counter,
						NITIO_SHARED_STATUS_REG(cidx));
	const unsigned short gi_status = ni_tio_read(counter,
						NITIO_STATUS_REG(cidx));
	unsigned ack = 0;

	if (gate_error)
		*gate_error = 0;
	if (tc_error)
		*tc_error = 0;
	if (perm_stale_data)
		*perm_stale_data = 0;
	if (stale_data)
		*stale_data = 0;

	if (gxx_status & GI_GATE_ERROR(cidx)) {
		ack |= GI_GATE_ERROR_CONFIRM(cidx);
		if (gate_error) {
			/*660x don't support automatic acknowledgment
			  of gate interrupt via dma read/write
			   and report bogus gate errors */
			if (counter->counter_dev->variant !=
			    ni_gpct_variant_660x)
				*gate_error = 1;
		}
	}
	if (gxx_status & GI_TC_ERROR(cidx)) {
		ack |= GI_TC_ERROR_CONFIRM(cidx);
		if (tc_error)
			*tc_error = 1;
	}
	if (gi_status & GI_TC)
		ack |= GI_TC_INTERRUPT_ACK;
	if (gi_status & GI_GATE_INTERRUPT) {
		if (should_ack_gate(counter))
			ack |= GI_GATE_INTERRUPT_ACK;
	}
	if (ack)
		ni_tio_write(counter, ack, NITIO_INT_ACK_REG(cidx));
	if (ni_tio_get_soft_copy(counter, NITIO_MODE_REG(cidx)) &
	    GI_LOADING_ON_GATE) {
		if (gxx_status & GI_STALE_DATA(cidx)) {
			if (stale_data)
				*stale_data = 1;
		}
		if (ni_tio_read(counter, NITIO_STATUS2_REG(cidx)) &
		    GI_PERMANENT_STALE(cidx)) {
			dev_info(counter->counter_dev->dev->class_dev,
				 "%s: Gi_Permanent_Stale_Data detected.\n",
				 __func__);
			if (perm_stale_data)
				*perm_stale_data = 1;
		}
	}
}

void ni_tio_acknowledge(struct ni_gpct *counter)
{
	ni_tio_acknowledge_and_confirm(counter, NULL, NULL, NULL, NULL);
}
EXPORT_SYMBOL_GPL(ni_tio_acknowledge);

void ni_tio_handle_interrupt(struct ni_gpct *counter,
			     struct comedi_subdevice *s)
{
	unsigned cidx = counter->counter_index;
	unsigned gpct_mite_status;
	unsigned long flags;
	int gate_error;
	int tc_error;
	int perm_stale_data;

	ni_tio_acknowledge_and_confirm(counter, &gate_error, &tc_error,
				       &perm_stale_data, NULL);
	if (gate_error) {
		dev_notice(counter->counter_dev->dev->class_dev,
			   "%s: Gi_Gate_Error detected.\n", __func__);
		s->async->events |= COMEDI_CB_OVERFLOW;
	}
	if (perm_stale_data)
		s->async->events |= COMEDI_CB_ERROR;
	switch (counter->counter_dev->variant) {
	case ni_gpct_variant_m_series:
	case ni_gpct_variant_660x:
		if (ni_tio_read(counter, NITIO_DMA_STATUS_REG(cidx)) &
		    GI_DRQ_ERROR) {
			dev_notice(counter->counter_dev->dev->class_dev,
				   "%s: Gi_DRQ_Error detected.\n", __func__);
			s->async->events |= COMEDI_CB_OVERFLOW;
		}
		break;
	case ni_gpct_variant_e_series:
		break;
	}
	spin_lock_irqsave(&counter->lock, flags);
	if (!counter->mite_chan) {
		spin_unlock_irqrestore(&counter->lock, flags);
		return;
	}
	gpct_mite_status = mite_get_status(counter->mite_chan);
	if (gpct_mite_status & CHSR_LINKC)
		writel(CHOR_CLRLC,
		       counter->mite_chan->mite->mite_io_addr +
		       MITE_CHOR(counter->mite_chan->channel));
	mite_sync_input_dma(counter->mite_chan, s);
	spin_unlock_irqrestore(&counter->lock, flags);
}
EXPORT_SYMBOL_GPL(ni_tio_handle_interrupt);

void ni_tio_set_mite_channel(struct ni_gpct *counter,
			     struct mite_channel *mite_chan)
{
	unsigned long flags;

	spin_lock_irqsave(&counter->lock, flags);
	counter->mite_chan = mite_chan;
	spin_unlock_irqrestore(&counter->lock, flags);
}
EXPORT_SYMBOL_GPL(ni_tio_set_mite_channel);

static int __init ni_tiocmd_init_module(void)
{
	return 0;
}
module_init(ni_tiocmd_init_module);

static void __exit ni_tiocmd_cleanup_module(void)
{
}
module_exit(ni_tiocmd_cleanup_module);

MODULE_AUTHOR("Comedi <comedi@comedi.org>");
MODULE_DESCRIPTION("Comedi command support for NI general-purpose counters");
MODULE_LICENSE("GPL");
