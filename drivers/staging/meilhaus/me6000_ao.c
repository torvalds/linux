/**
 * @file me6000_ao.c
 *
 * @brief ME-6000 analog output subdevice instance.
 * @note Copyright (C) 2007 Meilhaus Electronic GmbH (support@meilhaus.de)
 * @author Guenter Gebhardt
 * @author Krzysztof Gantzke	(k.gantzke@meilhaus.de)
 */

/*
 * Copyright (C) 2007 Meilhaus Electronic GmbH (support@meilhaus.de)
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __KERNEL__
#  define __KERNEL__
#endif

/* Includes
 */
#include <linux/version.h>
#include <linux/module.h>

#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/delay.h>

#include <linux/workqueue.h>

#include "medefines.h"
#include "meinternal.h"
#include "meerror.h"

#include "medebug.h"
#include "meids.h"
#include "me6000_reg.h"
#include "me6000_ao_reg.h"
#include "me6000_ao.h"

/* Defines
 */

static int me6000_ao_query_range_by_min_max(me_subdevice_t *subdevice,
					    int unit,
					    int *min,
					    int *max, int *maxdata, int *range);

static int me6000_ao_query_number_ranges(me_subdevice_t *subdevice,
					 int unit, int *count);

static int me6000_ao_query_range_info(me_subdevice_t *subdevice,
				      int range,
				      int *unit,
				      int *min, int *max, int *maxdata);

static int me6000_ao_query_timer(me_subdevice_t *subdevice,
				 int timer,
				 int *base_frequency,
				 long long *min_ticks, long long *max_ticks);

static int me6000_ao_query_number_channels(me_subdevice_t *subdevice,
					   int *number);

static int me6000_ao_query_subdevice_type(me_subdevice_t *subdevice,
					  int *type, int *subtype);

static int me6000_ao_query_subdevice_caps(me_subdevice_t *subdevice,
					  int *caps);

static int me6000_ao_query_subdevice_caps_args(struct me_subdevice *subdevice,
					       int cap, int *args, int count);

/** Remove subdevice. */
static void me6000_ao_destructor(struct me_subdevice *subdevice);

/** Reset subdevice. Stop all actions. Reset registry. Disable FIFO. Set output to 0V and status to 'none'. */
static int me6000_ao_io_reset_subdevice(me_subdevice_t *subdevice,
					struct file *filep, int flags);

/** Set output as single */
static int me6000_ao_io_single_config(me_subdevice_t *subdevice,
				      struct file *filep,
				      int channel,
				      int single_config,
				      int ref,
				      int trig_chan,
				      int trig_type, int trig_edge, int flags);

/** Pass to user actual value of output. */
static int me6000_ao_io_single_read(me_subdevice_t *subdevice,
				    struct file *filep,
				    int channel,
				    int *value, int time_out, int flags);

/** Write to output requed value. */
static int me6000_ao_io_single_write(me_subdevice_t *subdevice,
				     struct file *filep,
				     int channel,
				     int value, int time_out, int flags);

/** Set output as streamed device. */
static int me6000_ao_io_stream_config(me_subdevice_t *subdevice,
				      struct file *filep,
				      meIOStreamConfig_t *config_list,
				      int count,
				      meIOStreamTrigger_t *trigger,
				      int fifo_irq_threshold, int flags);

/** Wait for / Check empty space in buffer. */
static int me6000_ao_io_stream_new_values(me_subdevice_t *subdevice,
					  struct file *filep,
					  int time_out, int *count, int flags);

/** Start streaming. */
static int me6000_ao_io_stream_start(me_subdevice_t *subdevice,
				     struct file *filep,
				     int start_mode, int time_out, int flags);

/** Check actual state. / Wait for end. */
static int me6000_ao_io_stream_status(me_subdevice_t *subdevice,
				      struct file *filep,
				      int wait,
				      int *status, int *values, int flags);

/** Stop streaming. */
static int me6000_ao_io_stream_stop(me_subdevice_t *subdevice,
				    struct file *filep,
				    int stop_mode, int flags);

/** Write datas to buffor. */
static int me6000_ao_io_stream_write(me_subdevice_t *subdevice,
				     struct file *filep,
				     int write_mode,
				     int *values, int *count, int flags);

/** Interrupt handler. Copy from buffer to FIFO. */
static irqreturn_t me6000_ao_isr(int irq, void *dev_id);

/** Copy data from circular buffer to fifo (fast) in wraparound mode. */
inline int ao_write_data_wraparound(me6000_ao_subdevice_t *instance, int count,
				    int start_pos);

/** Copy data from circular buffer to fifo (fast).*/
inline int ao_write_data(me6000_ao_subdevice_t *instance, int count,
			 int start_pos);

/** Copy data from circular buffer to fifo (slow).*/
inline int ao_write_data_pooling(me6000_ao_subdevice_t *instance, int count,
				 int start_pos);

/** Copy data from user space to circular buffer. */
inline int ao_get_data_from_user(me6000_ao_subdevice_t *instance, int count,
				 int *user_values);

/** Stop presentation. Preserve FIFOs. */
inline int ao_stop_immediately(me6000_ao_subdevice_t *instance);

/** Function for checking timeout in non-blocking mode. */
static void me6000_ao_work_control_task(struct work_struct *work);

/* Functions
 */

static int me6000_ao_io_reset_subdevice(me_subdevice_t *subdevice,
					struct file *filep, int flags)
{
	me6000_ao_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	uint32_t tmp;
	uint32_t ctrl;

	instance = (me6000_ao_subdevice_t *) subdevice;

	PDEBUG("executed. idx=%d\n", instance->ao_idx);

	if (flags) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	ME_SUBDEVICE_ENTER;

	instance->status = ao_status_none;
	instance->ao_control_task_flag = 0;
	cancel_delayed_work(&instance->ao_control_task);
	instance->timeout.delay = 0;
	instance->timeout.start_time = jiffies;

	//Stop state machine.
	err = ao_stop_immediately(instance);

	//Remove from synchronous start.
	spin_lock(instance->preload_reg_lock);
	tmp = inl(instance->preload_reg);
	tmp &=
	    ~((ME6000_AO_SYNC_HOLD | ME6000_AO_SYNC_EXT_TRIG) << instance->
	      ao_idx);
	outl(tmp, instance->preload_reg);
	PDEBUG_REG("preload_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->preload_reg - instance->reg_base, tmp);
	*instance->preload_flags &=
	    ~((ME6000_AO_SYNC_HOLD | ME6000_AO_SYNC_EXT_TRIG) << instance->
	      ao_idx);

	//Reset triggering flag
	*instance->triggering_flags &= ~(0x1 << instance->ao_idx);
	spin_unlock(instance->preload_reg_lock);

	if (instance->fifo) {
		//Set single mode, dissable FIFO, dissable external trigger, block interrupt.
		ctrl = ME6000_AO_MODE_SINGLE;

		//Block ISM.
		ctrl |=
		    (ME6000_AO_CTRL_BIT_STOP |
		     ME6000_AO_CTRL_BIT_IMMEDIATE_STOP);

		outl(ctrl, instance->ctrl_reg);
		PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->ctrl_reg - instance->reg_base, ctrl);
		//Set speed
		outl(ME6000_AO_MIN_CHAN_TICKS - 1, instance->timer_reg);
		//Reset interrupt latch
		inl(instance->irq_reset_reg);
	}

	instance->hardware_stop_delay = HZ / 10;	//100ms

	//Set output to 0V
	outl(0x8000, instance->single_reg);
	PDEBUG_REG("single_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->single_reg - instance->reg_base, 0x8000);

	instance->circ_buf.head = 0;
	instance->circ_buf.tail = 0;
	instance->preloaded_count = 0;
	instance->data_count = 0;
	instance->single_value = 0x8000;
	instance->single_value_in_fifo = 0x8000;

	//Set status to signal that device is unconfigured.
	instance->status = ao_status_none;
	//Signal reset if user is on wait.
	wake_up_interruptible_all(&instance->wait_queue);

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me6000_ao_io_single_config(me_subdevice_t *subdevice,
				      struct file *filep,
				      int channel,
				      int single_config,
				      int ref,
				      int trig_chan,
				      int trig_type, int trig_edge, int flags)
{
	me6000_ao_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	uint32_t ctrl;
	uint32_t sync;
	unsigned long cpu_flags;

	instance = (me6000_ao_subdevice_t *) subdevice;

	PDEBUG("executed. ID=%d\n", instance->ao_idx);

	// Checking parameters
	if (flags) {
		PERROR
		    ("Invalid flag specified. Must be ME_IO_SINGLE_CONFIG_NO_FLAGS.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	if (instance->fifo) {	//Stream hardware (with or without fifo)
		if ((trig_edge == ME_TRIG_TYPE_SW)
		    && (trig_edge != ME_TRIG_EDGE_NONE)) {
			PERROR
			    ("Invalid trigger edge. Software trigger has not edge.\n");
			return ME_ERRNO_INVALID_TRIG_EDGE;
		}

		if (trig_type == ME_TRIG_TYPE_EXT_DIGITAL) {
			switch (trig_edge) {
			case ME_TRIG_EDGE_ANY:
			case ME_TRIG_EDGE_RISING:
			case ME_TRIG_EDGE_FALLING:
				break;

			default:
				PERROR("Invalid trigger edge.\n");
				return ME_ERRNO_INVALID_TRIG_EDGE;
			}
		}

		if ((trig_type != ME_TRIG_TYPE_SW)
		    && (trig_type != ME_TRIG_TYPE_EXT_DIGITAL)) {
			PERROR
			    ("Invalid trigger type. Trigger must be software or digital.\n");
			return ME_ERRNO_INVALID_TRIG_TYPE;
		}
	} else {		//Single
		if (trig_edge != ME_TRIG_EDGE_NONE) {
			PERROR
			    ("Invalid trigger edge. Single output trigger hasn't own edge.\n");
			return ME_ERRNO_INVALID_TRIG_EDGE;
		}

		if (trig_type != ME_TRIG_TYPE_SW) {
			PERROR
			    ("Invalid trigger type. Trigger must be software.\n");
			return ME_ERRNO_INVALID_TRIG_TYPE;
		}

	}

	if ((trig_chan != ME_TRIG_CHAN_DEFAULT)
	    && (trig_chan != ME_TRIG_CHAN_SYNCHRONOUS)) {
		PERROR("Invalid trigger channel specified.\n");
		return ME_ERRNO_INVALID_TRIG_CHAN;
	}
/*
	if ((trig_type == ME_TRIG_TYPE_EXT_DIGITAL) && (trig_chan != ME_TRIG_CHAN_SYNCHRONOUS))
	{
		PERROR("Invalid trigger channel specified. Must be synchronous when digital is choose.\n");
		return ME_ERRNO_INVALID_TRIG_CHAN;
	}
*/
	if (ref != ME_REF_AO_GROUND) {
		PERROR
		    ("Invalid reference. Analog outputs have to have got REF_AO_GROUND.\n");
		return ME_ERRNO_INVALID_REF;
	}

	if (single_config != 0) {
		PERROR
		    ("Invalid single config specified. Only one range for anlog outputs is available.\n");
		return ME_ERRNO_INVALID_SINGLE_CONFIG;
	}

	if (channel != 0) {
		PERROR
		    ("Invalid channel number specified. Analog output have only one channel.\n");
		return ME_ERRNO_INVALID_CHANNEL;
	}

	ME_SUBDEVICE_ENTER;

	//Subdevice running in stream mode!
	if ((instance->status >= ao_status_stream_run_wait)
	    && (instance->status < ao_status_stream_end)) {
		PERROR("Subdevice is busy.\n");
		ME_SUBDEVICE_EXIT;

		return ME_ERRNO_SUBDEVICE_BUSY;
	}
/// @note For single all calls (config and write) are erasing previous state!

	instance->status = ao_status_none;

	// Correct single mirrors
	instance->single_value_in_fifo = instance->single_value;

	//Stop device
	err = ao_stop_immediately(instance);
	if (err) {
		PERROR_CRITICAL("FSM IS BUSY!\n");
		ME_SUBDEVICE_EXIT;

		return ME_ERRNO_SUBDEVICE_BUSY;
	}

	if (instance->fifo) {	// Set control register.
		spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);
		// Set stop bit. Stop streaming mode (If running.).
		ctrl = inl(instance->ctrl_reg);
		//Reset all bits.
		ctrl =
		    ME6000_AO_CTRL_BIT_IMMEDIATE_STOP | ME6000_AO_CTRL_BIT_STOP;
		if (trig_type == ME_TRIG_TYPE_EXT_DIGITAL) {
			PINFO("External digital trigger.\n");

			if (trig_edge == ME_TRIG_EDGE_ANY) {
//                                      ctrl |= ME6000_AO_CTRL_BIT_EX_TRIG_EDGE | ME6000_AO_CTRL_BIT_EX_TRIG_EDGE_BOTH;
				instance->ctrl_trg =
				    ME6000_AO_CTRL_BIT_EX_TRIG_EDGE |
				    ME6000_AO_CTRL_BIT_EX_TRIG_EDGE_BOTH;
			} else if (trig_edge == ME_TRIG_EDGE_FALLING) {
//                                      ctrl |= ME6000_AO_CTRL_BIT_EX_TRIG_EDGE;
				instance->ctrl_trg =
				    ME6000_AO_CTRL_BIT_EX_TRIG_EDGE;
			} else if (trig_edge == ME_TRIG_EDGE_RISING) {
				instance->ctrl_trg = 0x0;
			}
		} else if (trig_type == ME_TRIG_TYPE_SW) {
			PDEBUG("SOFTWARE TRIGGER\n");
			instance->ctrl_trg = 0x0;
		}
		outl(ctrl, instance->ctrl_reg);
		PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->ctrl_reg - instance->reg_base, ctrl);
		spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);
	} else {
		PDEBUG("SOFTWARE TRIGGER\n");
	}

	// Set preload/synchronization register.
	spin_lock(instance->preload_reg_lock);

	if (trig_type == ME_TRIG_TYPE_SW) {
		*instance->preload_flags &=
		    ~(ME6000_AO_SYNC_EXT_TRIG << instance->ao_idx);
	} else			//if (trig_type == ME_TRIG_TYPE_EXT_DIGITAL)
	{
		*instance->preload_flags |=
		    ME6000_AO_SYNC_EXT_TRIG << instance->ao_idx;
	}

	if (trig_chan == ME_TRIG_CHAN_DEFAULT) {
		*instance->preload_flags &=
		    ~(ME6000_AO_SYNC_HOLD << instance->ao_idx);
	} else			//if (trig_chan == ME_TRIG_CHAN_SYNCHRONOUS)
	{
		*instance->preload_flags |=
		    ME6000_AO_SYNC_HOLD << instance->ao_idx;
	}

	//Reset hardware register
	sync = inl(instance->preload_reg);
	PDEBUG_REG("preload_reg inl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->preload_reg - instance->reg_base, sync);
	sync &= ~(ME6000_AO_SYNC_EXT_TRIG << instance->ao_idx);
	sync |= ME6000_AO_SYNC_HOLD << instance->ao_idx;

	//Output configured in default mode (safe one)
	outl(sync, instance->preload_reg);
	PDEBUG_REG("preload_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->preload_reg - instance->reg_base, sync);
	spin_unlock(instance->preload_reg_lock);

	instance->status = ao_status_single_configured;

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me6000_ao_io_single_read(me_subdevice_t *subdevice,
				    struct file *filep,
				    int channel,
				    int *value, int time_out, int flags)
{
	me6000_ao_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;

	unsigned long j;
	unsigned long delay = 0;

	instance = (me6000_ao_subdevice_t *) subdevice;

	PDEBUG("executed. idx=%d\n", instance->ao_idx);

	if (flags & ~ME_IO_SINGLE_NONBLOCKING) {
		PERROR("Invalid flag specified. %d\n", flags);
		return ME_ERRNO_INVALID_FLAGS;
	}

	if ((instance->status >= ao_status_stream_configured)
	    && (instance->status <= ao_status_stream_end)) {
		PERROR("Subdevice not configured to work in single mode!\n");
		return ME_ERRNO_PREVIOUS_CONFIG;
	}

	if (channel != 0) {
		PERROR("Invalid channel number specified.\n");
		return ME_ERRNO_INVALID_CHANNEL;
	}

	if (time_out < 0) {
		PERROR("Invalid timeout specified.\n");
		return ME_ERRNO_INVALID_TIMEOUT;
	}

	ME_SUBDEVICE_ENTER;
	if ((!flags) && (instance->status == ao_status_single_run_wait)) {	//Blocking mode. Wait for trigger.
		if (time_out) {
			delay = (time_out * HZ) / 1000;
			if (delay == 0)
				delay = 1;
		}

		j = jiffies;

		//Only runing process will interrupt this call. Events are signaled when status change. This procedure has own timeout.
		wait_event_interruptible_timeout(instance->wait_queue,
						 (instance->status !=
						  ao_status_single_run_wait),
						 (delay) ? delay : LONG_MAX);

		if (instance->status == ao_status_none) {
			PDEBUG("Single canceled.\n");
			err = ME_ERRNO_CANCELLED;
		}

		if (signal_pending(current)) {
			PERROR("Wait on start of state machine interrupted.\n");
			instance->status = ao_status_none;
			ao_stop_immediately(instance);
			err = ME_ERRNO_SIGNAL;
		}

		if ((delay) && ((jiffies - j) >= delay)) {
			PDEBUG("Timeout reached.\n");
			err = ME_ERRNO_TIMEOUT;
		}

		*value =
		    (!err) ? instance->single_value_in_fifo : instance->
		    single_value;
	} else {		//Non-blocking mode
		//Read value
		*value = instance->single_value;
	}

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me6000_ao_io_single_write(me_subdevice_t *subdevice,
				     struct file *filep,
				     int channel,
				     int value, int time_out, int flags)
{
	me6000_ao_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	unsigned long cpu_flags;
	unsigned long j;
	unsigned long delay = 0;

	uint32_t sync_mask;
	uint32_t mode;

	uint32_t tmp;

/// Workaround for mix-mode - begin
	uint32_t ctrl = 0x0;
	uint32_t status;
/// Workaround for mix-mode - end

	instance = (me6000_ao_subdevice_t *) subdevice;

	PDEBUG("executed. idx=%d\n", instance->ao_idx);

	if (flags &
	    ~(ME_IO_SINGLE_TYPE_TRIG_SYNCHRONOUS |
	      ME_IO_SINGLE_TYPE_WRITE_NONBLOCKING)) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	if ((instance->status == ao_status_none)
	    || (instance->status > ao_status_single_end)) {
		PERROR("Subdevice not configured to work in single mode!\n");
		return ME_ERRNO_PREVIOUS_CONFIG;
	}

	if (channel != 0) {
		PERROR("Invalid channel number specified.\n");
		return ME_ERRNO_INVALID_CHANNEL;
	}

	if (value & ~ME6000_AO_MAX_DATA) {
		PERROR("Invalid value provided.\n");
		return ME_ERRNO_VALUE_OUT_OF_RANGE;
	}

	if (time_out < 0) {
		PERROR("Invalid timeout specified.\n");
		return ME_ERRNO_INVALID_TIMEOUT;
	}

	ME_SUBDEVICE_ENTER;

/// @note For single all calls (config and write) are erasing previous state!

	//Cancel control task
	PDEBUG("Cancel control task. idx=%d\n", instance->ao_idx);
	instance->ao_control_task_flag = 0;
	cancel_delayed_work(&instance->ao_control_task);

	// Correct single mirrors
	instance->single_value_in_fifo = instance->single_value;

	//Stop device
	err = ao_stop_immediately(instance);
	if (err) {
		PERROR_CRITICAL("FSM IS BUSY!\n");
		ME_SUBDEVICE_EXIT;

		return ME_ERRNO_SUBDEVICE_BUSY;
	}

	if (time_out) {
		delay = (time_out * HZ) / 1000;

		if (delay == 0)
			delay = 1;
	}

	spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);

	instance->single_value_in_fifo = value;

	if (instance->fifo) {
		ctrl = inl(instance->ctrl_reg);
	}

	if (instance->fifo & ME6000_AO_HAS_FIFO) {	/// Workaround for mix-mode - begin
		//Set speed
		outl(ME6000_AO_MIN_CHAN_TICKS - 1, instance->timer_reg);
		PDEBUG_REG("timer_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->timer_reg - instance->reg_base,
			   (int)ME6000_AO_MIN_CHAN_TICKS);
		instance->hardware_stop_delay = HZ / 10;	//100ms

		status = inl(instance->status_reg);

		//Set the continous mode.
		ctrl &= ~ME6000_AO_CTRL_MODE_MASK;
		ctrl |= ME6000_AO_MODE_CONTINUOUS;

		//Prepare FIFO
		if (!(ctrl & ME6000_AO_CTRL_BIT_ENABLE_FIFO)) {	//FIFO wasn't enabeled. Do it.
			PINFO("Enableing FIFO.\n");
			ctrl &= ~ME6000_AO_CTRL_BIT_ENABLE_IRQ;
			ctrl |= ME6000_AO_CTRL_BIT_ENABLE_FIFO;
		} else {	//Check if FIFO is empty
			if (status & ME6000_AO_STATUS_BIT_EF) {	//FIFO not empty
				PINFO("Reseting FIFO.\n");
				ctrl &=
				    ~(ME6000_AO_CTRL_BIT_ENABLE_FIFO |
				      ME6000_AO_CTRL_BIT_ENABLE_IRQ);
				outl(ctrl, instance->ctrl_reg);
				PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
					   instance->reg_base,
					   instance->ctrl_reg -
					   instance->reg_base, ctrl);

				ctrl |= ME6000_AO_CTRL_BIT_ENABLE_FIFO;
			} else {	//FIFO empty, only interrupt needs to be disabled!
				ctrl &= ~ME6000_AO_CTRL_BIT_ENABLE_IRQ;
			}
		}

		outl(ctrl, instance->ctrl_reg);
		PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->ctrl_reg - instance->reg_base, ctrl);

		//Reset interrupt latch
		inl(instance->irq_reset_reg);

		//Write output - 1 value to FIFO
		if (instance->ao_idx & 0x1) {
			outl(value <<= 16, instance->fifo_reg);
			PDEBUG_REG("fifo_reg outl(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->fifo_reg - instance->reg_base,
				   value <<= 16);
		} else {
			outl(value, instance->fifo_reg);
			PDEBUG_REG("fifo_reg outl(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->fifo_reg - instance->reg_base,
				   value);
		}
		/// Workaround for mix-mode - end
	} else {		//No FIFO - always in single mode
		//Write value
		PDEBUG("Write value\n");
		outl(value, instance->single_reg);
		PDEBUG_REG("single_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->single_reg - instance->reg_base, value);
	}

	mode = *instance->preload_flags >> instance->ao_idx;
	mode &= (ME6000_AO_SYNC_HOLD | ME6000_AO_SYNC_EXT_TRIG);

	PINFO("Triggering mode: 0x%08x\n", mode);

	spin_lock(instance->preload_reg_lock);
	sync_mask = inl(instance->preload_reg);
	PDEBUG_REG("preload_reg inl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->preload_reg - instance->reg_base, sync_mask);
	switch (mode) {
	case 0:		//0x00000000: Individual software
		ctrl &= ~ME6000_AO_CTRL_BIT_ENABLE_EX_TRIG;

		if (instance->fifo & ME6000_AO_HAS_FIFO) {	// FIFO - Continous mode
			ctrl &= ~ME6000_AO_CTRL_BIT_ENABLE_EX_TRIG;
			if ((sync_mask & ((ME6000_AO_SYNC_HOLD | ME6000_AO_SYNC_EXT_TRIG) << instance->ao_idx)) != 0x0) {	//Now we can set correct mode.
				sync_mask &=
				    ~((ME6000_AO_SYNC_EXT_TRIG |
				       ME6000_AO_SYNC_HOLD) << instance->
				      ao_idx);

				outl(sync_mask, instance->preload_reg);
				PDEBUG_REG
				    ("preload_reg outl(0x%lX+0x%lX)=0x%x\n",
				     instance->reg_base,
				     instance->preload_reg - instance->reg_base,
				     sync_mask);
			}
		} else {	// No FIFO - Single mode: In this case resetting 'ME6000_AO_SYNC_HOLD' will trigger output.
			if ((sync_mask & ((ME6000_AO_SYNC_HOLD | ME6000_AO_SYNC_EXT_TRIG) << instance->ao_idx)) != ME6000_AO_SYNC_HOLD) {	//Now we can set correct mode. This is exception. It is set to synchronous and triggered later.
				sync_mask &=
				    ~(ME6000_AO_SYNC_EXT_TRIG << instance->
				      ao_idx);
				sync_mask |=
				    ME6000_AO_SYNC_HOLD << instance->ao_idx;

				outl(sync_mask, instance->preload_reg);
				PDEBUG_REG
				    ("preload_reg outl(0x%lX+0x%lX)=0x%x\n",
				     instance->reg_base,
				     instance->preload_reg - instance->reg_base,
				     sync_mask);
			}
		}
		instance->single_value = value;
		break;

	case ME6000_AO_SYNC_EXT_TRIG:	//0x00010000: Individual hardware
		PDEBUG("DIGITAL TRIGGER\n");
		ctrl |= ME6000_AO_CTRL_BIT_ENABLE_EX_TRIG;

		if (instance->fifo & ME6000_AO_HAS_FIFO) {	// FIFO - Continous mode
			if ((sync_mask & ((ME6000_AO_SYNC_HOLD | ME6000_AO_SYNC_EXT_TRIG) << instance->ao_idx)) != 0x0) {	//Now we can set correct mode.
				sync_mask &=
				    ~((ME6000_AO_SYNC_EXT_TRIG |
				       ME6000_AO_SYNC_HOLD) << instance->
				      ao_idx);

				outl(sync_mask, instance->preload_reg);
				PDEBUG_REG
				    ("preload_reg outl(0x%lX+0x%lX)=0x%x\n",
				     instance->reg_base,
				     instance->preload_reg - instance->reg_base,
				     sync_mask);
			}
		} else {	// No FIFO - Single mode
			if ((sync_mask &
			     ((ME6000_AO_SYNC_HOLD | ME6000_AO_SYNC_EXT_TRIG) <<
			      instance->ao_idx)) != ME6000_AO_SYNC_HOLD) {
				//Now we can set correct mode
				sync_mask &=
				    ~(ME6000_AO_SYNC_EXT_TRIG << instance->
				      ao_idx);
				sync_mask |=
				    ME6000_AO_SYNC_HOLD << instance->ao_idx;

				outl(sync_mask, instance->preload_reg);
				PDEBUG_REG
				    ("preload_reg outl(0x%lX+0x%lX)=0x%x\n",
				     instance->reg_base,
				     instance->preload_reg - instance->reg_base,
				     sync_mask);
			}
		}
		break;

	case ME6000_AO_SYNC_HOLD:	//0x00000001: Synchronous software
		ctrl &= ~ME6000_AO_CTRL_BIT_ENABLE_EX_TRIG;

		if ((sync_mask &
		     ((ME6000_AO_SYNC_HOLD | ME6000_AO_SYNC_EXT_TRIG) <<
		      instance->ao_idx)) !=
		    (ME6000_AO_SYNC_HOLD | ME6000_AO_SYNC_EXT_TRIG)) {
			//Now we can set correct mode
			sync_mask |=
			    ME6000_AO_SYNC_EXT_TRIG << instance->ao_idx;
			sync_mask |= ME6000_AO_SYNC_HOLD << instance->ao_idx;
			outl(sync_mask, instance->preload_reg);
			PDEBUG_REG("preload_reg outl(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->preload_reg - instance->reg_base,
				   sync_mask);
		}
		//Set triggering flag
		*instance->triggering_flags |= 0x1 << instance->ao_idx;
		break;

	case (ME6000_AO_SYNC_HOLD | ME6000_AO_SYNC_EXT_TRIG):	//0x00010001: Synchronous hardware
		PDEBUG("DIGITAL TRIGGER\n");
		ctrl |= ME6000_AO_CTRL_BIT_ENABLE_EX_TRIG;

		if ((sync_mask &
		     ((ME6000_AO_SYNC_HOLD | ME6000_AO_SYNC_EXT_TRIG) <<
		      instance->ao_idx)) !=
		    (ME6000_AO_SYNC_HOLD | ME6000_AO_SYNC_EXT_TRIG)) {
			//Now we can set correct mode
			sync_mask |=
			    (ME6000_AO_SYNC_HOLD | ME6000_AO_SYNC_EXT_TRIG) <<
			    instance->ao_idx;
			outl(sync_mask, instance->preload_reg);
			PDEBUG_REG("preload_reg outl(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->preload_reg - instance->reg_base,
				   sync_mask);
		}
		//Set triggering flag
		*instance->triggering_flags |= 0x1 << instance->ao_idx;
		break;
	}
//              spin_unlock(instance->preload_reg_lock);        // Moved down.

	if (instance->fifo) {	//Activate ISM (remove 'stop' bits)
		ctrl &=
		    ~(ME6000_AO_CTRL_BIT_EX_TRIG_EDGE |
		      ME6000_AO_CTRL_BIT_EX_TRIG_EDGE_BOTH);
		ctrl |= instance->ctrl_trg;
		ctrl &=
		    ~(ME6000_AO_CTRL_BIT_STOP |
		      ME6000_AO_CTRL_BIT_IMMEDIATE_STOP);

		outl(ctrl, instance->ctrl_reg);
		PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->ctrl_reg - instance->reg_base, ctrl);
	}
	spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);

/// @note When flag 'ME_IO_SINGLE_TYPE_TRIG_SYNCHRONOUS' is set than output is triggered. ALWAYS!

	PINFO("<%s> start mode= 0x%08x %s\n", __func__, mode,
	      (flags & ME_IO_SINGLE_TYPE_TRIG_SYNCHRONOUS) ? "SYNCHRONOUS" :
	      "");
	if (instance->fifo & ME6000_AO_HAS_FIFO) {	// FIFO - Continous mode
		if (flags & ME_IO_SINGLE_TYPE_TRIG_SYNCHRONOUS) {	//Trigger outputs
			//Add channel to start list
			outl(sync_mask |
			     (ME6000_AO_SYNC_HOLD << instance->ao_idx),
			     instance->preload_reg);
			PDEBUG_REG("preload_reg outl(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->preload_reg - instance->reg_base,
				   sync_mask | (ME6000_AO_SYNC_HOLD <<
						instance->ao_idx));

			//Fire
			PINFO
			    ("Fired all software synchronous outputs by software trigger.\n");
			outl(0x8000, instance->single_reg);
			PDEBUG_REG("single_reg outl(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->single_reg - instance->reg_base,
				   0x8000);

			//Restore save settings
			outl(sync_mask, instance->preload_reg);
			PDEBUG_REG("preload_reg outl(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->preload_reg - instance->reg_base,
				   sync_mask);

		} else if (!mode) {	//Trigger outputs
/*			//Remove channel from start list
			outl(sync_mask & ~(ME6000_AO_SYNC_HOLD << instance->ao_idx), instance->preload_reg);
			PDEBUG_REG("preload_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base, instance->preload_reg - instance->reg_base, sync_mask & ~(ME6000_AO_SYNC_HOLD << instance->ao_idx));
*/
			//Fire
			PINFO("Software trigger.\n");
			outl(0x8000, instance->single_reg);
			PDEBUG_REG("single_reg outl(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->single_reg - instance->reg_base,
				   0x8000);

/*			//Restore save settings
			outl(sync_mask, instance->preload_reg);
			PDEBUG_REG("preload_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base, instance->preload_reg - instance->reg_base, sync_mask);
*/
		}
/// @note This is mix-mode case. For now I do not have possibility to trigger first 4 channels (continous mode) and other (single) ones at once.
/// @note Because triggering is not working it can not be add to synchronous list. First 4 channels don't need this information, anyway.
		*instance->triggering_flags &= 0xFFFFFFF0;
	} else {		// No FIFO - Single mode
		if (flags & ME_IO_SINGLE_TYPE_TRIG_SYNCHRONOUS) {	//Fired all software synchronous outputs.
			tmp = ~(*instance->preload_flags | 0xFFFF0000);
			PINFO
			    ("Fired all software synchronous outputs. mask:0x%08x\n",
			     tmp);
			tmp |= sync_mask & 0xFFFF0000;
			// Add this channel to list
			tmp &= ~(ME6000_AO_SYNC_HOLD << instance->ao_idx);

			//Fire
			PINFO("Software trigger.\n");
			outl(tmp, instance->preload_reg);
			PDEBUG_REG("preload_reg outl(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->preload_reg - instance->reg_base,
				   tmp);

			//Restore save settings
			outl(sync_mask, instance->preload_reg);
			PDEBUG_REG("preload_reg outl(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->preload_reg - instance->reg_base,
				   sync_mask);

			//Set all as triggered.
			*instance->triggering_flags = 0x0;
		} else if (!mode) {	// Add this channel to list
			outl(sync_mask &
			     ~(ME6000_AO_SYNC_HOLD << instance->ao_idx),
			     instance->preload_reg);
			PDEBUG_REG("preload_reg outl(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->preload_reg - instance->reg_base,
				   sync_mask & ~(ME6000_AO_SYNC_HOLD <<
						 instance->ao_idx));

			//Fire
			PINFO("Software trigger.\n");

			//Restore save settings
			outl(sync_mask, instance->preload_reg);
			PDEBUG_REG("preload_reg outl(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->preload_reg - instance->reg_base,
				   sync_mask);

			//Set all as triggered.
			*instance->triggering_flags = 0x0;
		}

	}
	spin_unlock(instance->preload_reg_lock);

	instance->status = ao_status_single_run_wait;

	instance->timeout.delay = delay;
	instance->timeout.start_time = jiffies;
	instance->ao_control_task_flag = 1;
	queue_delayed_work(instance->me6000_workqueue,
			   &instance->ao_control_task, 1);

	if (!(flags & ME_IO_SINGLE_TYPE_WRITE_NONBLOCKING)) {
		j = jiffies;

		//Only runing process will interrupt this call. Events are signaled when status change. Extra timeout add for safe reason.
		wait_event_interruptible_timeout(instance->wait_queue,
						 (instance->status !=
						  ao_status_single_run_wait),
						 (delay) ? delay +
						 1 : LONG_MAX);

		if (instance->status != ao_status_single_end) {
			PDEBUG("Single canceled.\n");
			err = ME_ERRNO_CANCELLED;
		}

		if (signal_pending(current)) {
			PERROR("Wait on start of state machine interrupted.\n");
			instance->ao_control_task_flag = 0;
			cancel_delayed_work(&instance->ao_control_task);
			ao_stop_immediately(instance);
			instance->status = ao_status_none;
			err = ME_ERRNO_SIGNAL;
		}

		if ((delay) && ((jiffies - j) >= delay)) {
			if (instance->status == ao_status_single_end) {
				PDEBUG("Timeout reached.\n");
			} else if ((jiffies - j) > delay) {
				PERROR
				    ("Timeout reached. Not handled by control task!\n");
				ao_stop_immediately(instance);
			} else {
				PERROR
				    ("Timeout reached. Signal come but status is strange: %d\n",
				     instance->status);
				ao_stop_immediately(instance);
			}

			instance->ao_control_task_flag = 0;
			cancel_delayed_work(&instance->ao_control_task);
			instance->status = ao_status_single_end;
			err = ME_ERRNO_TIMEOUT;
		}
	}

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me6000_ao_io_stream_config(me_subdevice_t *subdevice,
				      struct file *filep,
				      meIOStreamConfig_t *config_list,
				      int count,
				      meIOStreamTrigger_t *trigger,
				      int fifo_irq_threshold, int flags)
{
	me6000_ao_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	uint32_t ctrl;
	unsigned long cpu_flags;
	uint64_t conv_ticks;
	unsigned int conv_start_ticks_low = trigger->iConvStartTicksLow;
	unsigned int conv_start_ticks_high = trigger->iConvStartTicksHigh;

	instance = (me6000_ao_subdevice_t *) subdevice;

	PDEBUG("executed. idx=%d\n", instance->ao_idx);

	if (!(instance->fifo & ME6000_AO_HAS_FIFO)) {
		PERROR("Not a streaming ao.\n");
		return ME_ERRNO_NOT_SUPPORTED;
	}

	conv_ticks =
	    (uint64_t) conv_start_ticks_low +
	    ((uint64_t) conv_start_ticks_high << 32);

	if (flags &
	    ~(ME_IO_STREAM_CONFIG_HARDWARE_ONLY |
	      ME_IO_STREAM_CONFIG_WRAPAROUND)) {
		PERROR("Invalid flags.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	if (flags & ME_IO_STREAM_CONFIG_HARDWARE_ONLY) {
		if (!(flags & ME_IO_STREAM_CONFIG_WRAPAROUND)) {
			PERROR
			    ("Hardware ME_IO_STREAM_CONFIG_HARDWARE_ONLY has to be with ME_IO_STREAM_CONFIG_WRAPAROUND.\n");
			return ME_ERRNO_INVALID_FLAGS;
		}

		if ((trigger->iAcqStopTrigType != ME_TRIG_TYPE_NONE)
		    || (trigger->iScanStopTrigType != ME_TRIG_TYPE_NONE)) {
			PERROR
			    ("Hardware wraparound mode must be in infinite mode.\n");
			return ME_ERRNO_INVALID_FLAGS;
		}
	}

	if (count != 1) {
		PERROR("Only 1 entry in config list acceptable.\n");
		return ME_ERRNO_INVALID_CONFIG_LIST_COUNT;
	}

	if (config_list[0].iChannel != 0) {
		PERROR("Invalid channel number specified.\n");
		return ME_ERRNO_INVALID_CHANNEL;
	}

	if (config_list[0].iStreamConfig != 0) {
		PERROR("Only one range available.\n");
		return ME_ERRNO_INVALID_STREAM_CONFIG;
	}

	if (config_list[0].iRef != ME_REF_AO_GROUND) {
		PERROR("Output is referenced to ground.\n");
		return ME_ERRNO_INVALID_REF;
	}

	if ((trigger->iAcqStartTicksLow != 0)
	    || (trigger->iAcqStartTicksHigh != 0)) {
		PERROR
		    ("Invalid acquisition start trigger argument specified.\n");
		return ME_ERRNO_INVALID_ACQ_START_ARG;
	}

	if (config_list[0].iFlags) {
		PERROR("Invalid config list flag.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	if ((trigger->iAcqStartTrigType != ME_TRIG_TYPE_SW)
	    && (trigger->iAcqStartTrigType != ME_TRIG_TYPE_EXT_DIGITAL)) {
		PERROR("Invalid acquisition start trigger type specified.\n");
		return ME_ERRNO_INVALID_ACQ_START_TRIG_TYPE;
	}

	if (trigger->iAcqStartTrigType == ME_TRIG_TYPE_EXT_DIGITAL) {
		switch (trigger->iAcqStartTrigEdge) {
		case ME_TRIG_EDGE_RISING:
		case ME_TRIG_EDGE_FALLING:
		case ME_TRIG_EDGE_ANY:
			break;

		default:
			PERROR
			    ("Invalid acquisition start trigger edge specified.\n");
			return ME_ERRNO_INVALID_ACQ_START_TRIG_EDGE;
		}
	}

	if ((trigger->iAcqStartTrigType == ME_TRIG_TYPE_SW)
	    && (trigger->iAcqStartTrigEdge != ME_TRIG_TYPE_NONE)) {
		PERROR("Invalid acquisition start trigger edge specified.\n");
		return ME_ERRNO_INVALID_ACQ_START_TRIG_EDGE;
	}

	if (trigger->iScanStartTrigType != ME_TRIG_TYPE_FOLLOW) {
		PERROR("Invalid scan start trigger type specified.\n");
		return ME_ERRNO_INVALID_SCAN_START_TRIG_TYPE;
	}

	if (trigger->iConvStartTrigType != ME_TRIG_TYPE_TIMER) {
		PERROR("Invalid conv start trigger type specified.\n");
		return ME_ERRNO_INVALID_CONV_START_TRIG_TYPE;
	}

	if ((conv_ticks < ME6000_AO_MIN_CHAN_TICKS)
	    || (conv_ticks > ME6000_AO_MAX_CHAN_TICKS)) {
		PERROR("Invalid conv start trigger argument specified.\n");
		return ME_ERRNO_INVALID_CONV_START_ARG;
	}

	if (trigger->iAcqStartTicksLow || trigger->iAcqStartTicksHigh) {
		PERROR("Invalid acq start trigger argument specified.\n");
		return ME_ERRNO_INVALID_ACQ_START_ARG;
	}

	if (trigger->iScanStartTicksLow || trigger->iScanStartTicksHigh) {
		PERROR("Invalid scan start trigger argument specified.\n");
		return ME_ERRNO_INVALID_SCAN_START_ARG;
	}

	switch (trigger->iScanStopTrigType) {
	case ME_TRIG_TYPE_NONE:
		if (trigger->iScanStopCount != 0) {
			PERROR("Invalid scan stop count specified.\n");
			return ME_ERRNO_INVALID_SCAN_STOP_ARG;
		}
		break;

	case ME_TRIG_TYPE_COUNT:
		if (flags & ME_IO_STREAM_CONFIG_WRAPAROUND) {
			if (trigger->iScanStopCount <= 0) {
				PERROR("Invalid scan stop count specified.\n");
				return ME_ERRNO_INVALID_SCAN_STOP_ARG;
			}
		} else {
			PERROR("The continous mode has not 'scan' contects.\n");
			return ME_ERRNO_INVALID_ACQ_STOP_TRIG_TYPE;
		}
		break;

	default:
		PERROR("Invalid scan stop trigger type specified.\n");
		return ME_ERRNO_INVALID_SCAN_STOP_TRIG_TYPE;
	}

	switch (trigger->iAcqStopTrigType) {
	case ME_TRIG_TYPE_NONE:
		if (trigger->iAcqStopCount != 0) {
			PERROR("Invalid acq stop count specified.\n");
			return ME_ERRNO_INVALID_ACQ_STOP_ARG;
		}
		break;

	case ME_TRIG_TYPE_COUNT:
		if (trigger->iScanStopTrigType != ME_TRIG_TYPE_NONE) {
			PERROR("Invalid acq stop trigger type specified.\n");
			return ME_ERRNO_INVALID_ACQ_STOP_TRIG_TYPE;
		}

		if (flags & ME_IO_STREAM_CONFIG_WRAPAROUND) {
			if (trigger->iAcqStopCount <= 0) {
				PERROR
				    ("The continous mode has not 'scan' contects.\n");
				return ME_ERRNO_INVALID_ACQ_STOP_ARG;
			}
		}
//                      else
//                      {
//                              PERROR("Invalid acq stop trigger type specified.\n");
//                              return ME_ERRNO_INVALID_ACQ_STOP_TRIG_TYPE;
//                      }

		break;

	default:
		PERROR("Invalid acq stop trigger type specified.\n");
		return ME_ERRNO_INVALID_ACQ_STOP_TRIG_TYPE;
	}

	switch (trigger->iAcqStartTrigChan) {
	case ME_TRIG_CHAN_DEFAULT:
	case ME_TRIG_CHAN_SYNCHRONOUS:
		break;

	default:
		PERROR("Invalid acq start trigger channel specified.\n");
		return ME_ERRNO_INVALID_ACQ_START_TRIG_CHAN;
	}

	ME_SUBDEVICE_ENTER;

	//Stop device

	//Cancel control task
	PDEBUG("Cancel control task. idx=%d\n", instance->ao_idx);
	instance->ao_control_task_flag = 0;
	cancel_delayed_work(&instance->ao_control_task);

	//Check if state machine is stopped.
	err = ao_stop_immediately(instance);
	if (err) {
		PERROR_CRITICAL("FSM IS BUSY!\n");
		ME_SUBDEVICE_EXIT;

		return ME_ERRNO_SUBDEVICE_BUSY;
	}

	spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);
	//Reset control register. Block all actions. Disable IRQ. Disable FIFO.
	ctrl = ME6000_AO_CTRL_BIT_IMMEDIATE_STOP | ME6000_AO_CTRL_BIT_STOP;
	outl(ctrl, instance->ctrl_reg);
	PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->ctrl_reg - instance->reg_base, ctrl);

	//Reset interrupt latch
	inl(instance->irq_reset_reg);

	//This is paranoic, but to be sure.
	instance->preloaded_count = 0;
	instance->data_count = 0;
	instance->circ_buf.head = 0;
	instance->circ_buf.tail = 0;

	/* Set mode. */
	if (flags & ME_IO_STREAM_CONFIG_WRAPAROUND) {	//Wraparound
		if (flags & ME_IO_STREAM_CONFIG_HARDWARE_ONLY) {	//Hardware wraparound
			PINFO("Hardware wraparound.\n");
			ctrl |= ME6000_AO_MODE_WRAPAROUND;
			instance->mode = ME6000_AO_HW_WRAP_MODE;
		} else {	//Software wraparound
			PINFO("Software wraparound.\n");
			ctrl |= ME6000_AO_MODE_CONTINUOUS;
			instance->mode = ME6000_AO_SW_WRAP_MODE;
		}
	} else {		//Continous
		PINFO("Continous.\n");
		ctrl |= ME6000_AO_MODE_CONTINUOUS;
		instance->mode = ME6000_AO_CONTINOUS;
	}

	//Set the trigger edge.
	if (trigger->iAcqStartTrigType == ME_TRIG_TYPE_EXT_DIGITAL) {	//Set the trigger type and edge for external trigger.
		PINFO("External digital trigger.\n");
		instance->start_mode = ME6000_AO_EXT_TRIG;

		switch (trigger->iAcqStartTrigEdge) {
		case ME_TRIG_EDGE_RISING:
			PINFO("Set the trigger edge: rising.\n");
			instance->ctrl_trg = 0x0;
			break;

		case ME_TRIG_EDGE_FALLING:
			PINFO("Set the trigger edge: falling.\n");
//                                      ctrl |= ME6000_AO_CTRL_BIT_EX_TRIG_EDGE;
			instance->ctrl_trg = ME6000_AO_CTRL_BIT_EX_TRIG_EDGE;
			break;

		case ME_TRIG_EDGE_ANY:
			PINFO("Set the trigger edge: both edges.\n");
//                                      ctrl |= ME6000_AO_CTRL_BIT_EX_TRIG_EDGE | ME6000_AO_CTRL_BIT_EX_TRIG_EDGE_BOTH;
			instance->ctrl_trg =
			    ME6000_AO_CTRL_BIT_EX_TRIG_EDGE |
			    ME6000_AO_CTRL_BIT_EX_TRIG_EDGE_BOTH;
			break;
		}
	} else {
		PINFO("Internal software trigger.\n");
		instance->start_mode = 0;
	}

	//Set the stop mode and value.
	if (trigger->iAcqStopTrigType == ME_TRIG_TYPE_COUNT) {	//Amount of data
		instance->stop_mode = ME6000_AO_ACQ_STOP_MODE;
		instance->stop_count = trigger->iAcqStopCount;
	} else if (trigger->iScanStopTrigType == ME_TRIG_TYPE_COUNT) {	//Amount of 'scans'
		instance->stop_mode = ME6000_AO_SCAN_STOP_MODE;
		instance->stop_count = trigger->iScanStopCount;
	} else {		//Infinite
		instance->stop_mode = ME6000_AO_INF_STOP_MODE;
		instance->stop_count = 0;
	}

	PINFO("Stop count: %d.\n", instance->stop_count);

	if (trigger->iAcqStartTrigChan == ME_TRIG_CHAN_SYNCHRONOUS) {	//Synchronous start
		instance->start_mode |= ME6000_AO_SYNC_HOLD;
		if (trigger->iAcqStartTrigType == ME_TRIG_TYPE_EXT_DIGITAL) {	//Externaly triggered
			PINFO("Synchronous start. Externaly trigger active.\n");
			instance->start_mode |= ME6000_AO_SYNC_EXT_TRIG;
		}
#ifdef MEDEBUG_INFO
		else {
			PINFO
			    ("Synchronous start. Externaly trigger dissabled.\n");
		}
#endif

	}
	//Set speed
	outl(conv_ticks - 2, instance->timer_reg);
	PDEBUG_REG("timer_reg outl(0x%lX+0x%lX)=0x%llx\n", instance->reg_base,
		   instance->timer_reg - instance->reg_base, conv_ticks - 2);
	instance->hardware_stop_delay = (int)(conv_ticks * HZ) / ME6000_AO_BASE_FREQUENCY;	//<== MUST be with cast!

	// Write the control word
	outl(ctrl, instance->ctrl_reg);
	PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->ctrl_reg - instance->reg_base, ctrl);

	//Set status.
	instance->status = ao_status_stream_configured;
	spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me6000_ao_io_stream_new_values(me_subdevice_t *subdevice,
					  struct file *filep,
					  int time_out, int *count, int flags)
{
	me6000_ao_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	long t = 0;
	long j;

	instance = (me6000_ao_subdevice_t *) subdevice;

	PDEBUG("executed. idx=%d\n", instance->ao_idx);

	if (!(instance->fifo & ME6000_AO_HAS_FIFO)) {
		PERROR("Not a streaming ao.\n");
		return ME_ERRNO_NOT_SUPPORTED;
	}

	if (flags) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	if (!instance->circ_buf.buf) {
		PERROR("Circular buffer not exists.\n");
		return ME_ERRNO_INTERNAL;
	}

	if (time_out < 0) {
		PERROR("Invalid time_out specified.\n");
		return ME_ERRNO_INVALID_TIMEOUT;
	}

	ME_SUBDEVICE_ENTER;

	if (me_circ_buf_space(&instance->circ_buf)) {	//The buffer is NOT full.
		*count = me_circ_buf_space(&instance->circ_buf);
	} else {		//The buffer is full.
		if (time_out) {
			t = (time_out * HZ) / 1000;

			if (t == 0)
				t = 1;
		} else {	//Max time.
			t = LONG_MAX;
		}

		*count = 0;

		j = jiffies;

		//Only runing process will interrupt this call. Interrupts are when FIFO HF is signaled.
		wait_event_interruptible_timeout(instance->wait_queue,
						 ((me_circ_buf_space
						   (&instance->circ_buf))
						  || !(inl(instance->status_reg)
						       &
						       ME6000_AO_STATUS_BIT_FSM)),
						 t);

		if (!(inl(instance->status_reg) & ME6000_AO_STATUS_BIT_FSM)) {
			PERROR("AO subdevice is not running.\n");
			err = ME_ERRNO_SUBDEVICE_NOT_RUNNING;
		} else if (signal_pending(current)) {
			PERROR("Wait on values interrupted from signal.\n");
			instance->status = ao_status_none;
			ao_stop_immediately(instance);
			err = ME_ERRNO_SIGNAL;
		} else if ((jiffies - j) >= t) {
			PERROR("Wait on values timed out.\n");
			err = ME_ERRNO_TIMEOUT;
		} else {	//Uff... all is good. Inform user about empty space.
			*count = me_circ_buf_space(&instance->circ_buf);
		}
	}

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me6000_ao_io_stream_start(me_subdevice_t *subdevice,
				     struct file *filep,
				     int start_mode, int time_out, int flags)
{
	me6000_ao_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	unsigned long cpu_flags = 0;
	uint32_t status;
	uint32_t ctrl;
	uint32_t synch;
	int count = 0;
	int circ_buffer_count;

	unsigned long ref;
	unsigned long delay = 0;

	instance = (me6000_ao_subdevice_t *) subdevice;

	PDEBUG("executed. idx=%d\n", instance->ao_idx);

	if (!(instance->fifo & ME6000_AO_HAS_FIFO)) {
		PERROR("Not a streaming ao.\n");
		return ME_ERRNO_NOT_SUPPORTED;
	}

	if (flags & ~ME_IO_STREAM_START_TYPE_TRIG_SYNCHRONOUS) {
		PERROR("Invalid flags.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	if (time_out < 0) {
		PERROR("Invalid timeout specified.\n");
		return ME_ERRNO_INVALID_TIMEOUT;
	}

	if ((start_mode != ME_START_MODE_BLOCKING)
	    && (start_mode != ME_START_MODE_NONBLOCKING)) {
		PERROR("Invalid start mode specified.\n");
		return ME_ERRNO_INVALID_START_MODE;
	}

	if (time_out) {
		delay = (time_out * HZ) / 1000;
		if (delay == 0)
			delay = 1;
	}

	switch (instance->status) {	//Checking actual mode.
	case ao_status_stream_configured:
	case ao_status_stream_end:
		//Correct modes!
		break;

		//The device is in wrong mode.
	case ao_status_none:
	case ao_status_single_configured:
	case ao_status_single_run_wait:
	case ao_status_single_run:
	case ao_status_single_end_wait:
		PERROR
		    ("Subdevice must be preinitialize correctly for streaming.\n");
		return ME_ERRNO_PREVIOUS_CONFIG;

	case ao_status_stream_fifo_error:
	case ao_status_stream_buffer_error:
	case ao_status_stream_error:
		PDEBUG("Before restart broke stream 'STOP' must be caled.\n");
		return ME_STATUS_ERROR;

	case ao_status_stream_run_wait:
	case ao_status_stream_run:
	case ao_status_stream_end_wait:
		PDEBUG("Stream is already working.\n");
		return ME_ERRNO_SUBDEVICE_BUSY;

	default:
		instance->status = ao_status_stream_error;
		PERROR_CRITICAL("Status is in wrong state!\n");
		return ME_ERRNO_INTERNAL;

	}

	ME_SUBDEVICE_ENTER;

	if (instance->mode == ME6000_AO_CONTINOUS) {	//Continous
		instance->circ_buf.tail += instance->preloaded_count;
		instance->circ_buf.tail &= instance->circ_buf.mask;
	}
	circ_buffer_count = me_circ_buf_values(&instance->circ_buf);

	if (!circ_buffer_count && !instance->preloaded_count) {	//No values in buffer
		ME_SUBDEVICE_EXIT;
		PERROR("No values in buffer!\n");
		return ME_ERRNO_LACK_OF_RESOURCES;
	}

	//Cancel control task
	PDEBUG("Cancel control task. idx=%d\n", instance->ao_idx);
	instance->ao_control_task_flag = 0;
	cancel_delayed_work(&instance->ao_control_task);

	//Stop device
	err = ao_stop_immediately(instance);
	if (err) {
		PERROR_CRITICAL("FSM IS BUSY!\n");
		ME_SUBDEVICE_EXIT;

		return ME_ERRNO_SUBDEVICE_BUSY;
	}
	//Set values for single_read()
	instance->single_value = ME6000_AO_MAX_DATA + 1;
	instance->single_value_in_fifo = ME6000_AO_MAX_DATA + 1;

	//Setting stop points
	if (instance->stop_mode == ME6000_AO_SCAN_STOP_MODE) {
		instance->stop_data_count =
		    instance->stop_count * circ_buffer_count;
	} else {
		instance->stop_data_count = instance->stop_count;
	}

	if ((instance->stop_data_count != 0)
	    && (instance->stop_data_count < circ_buffer_count)) {
		PERROR("More data in buffer than previously set limit!\n");
	}

	spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);
	ctrl = inl(instance->ctrl_reg);
	//Check FIFO
	if (!(ctrl & ME6000_AO_CTRL_BIT_ENABLE_FIFO)) {	//FIFO wasn't enabeled. Do it. <= This should be done by user call with ME_WRITE_MODE_PRELOAD
		PINFO("Enableing FIFO.\n");
		ctrl |= ME6000_AO_CTRL_BIT_ENABLE_FIFO;
		ctrl &= ~ME6000_AO_CTRL_BIT_ENABLE_IRQ;

		instance->preloaded_count = 0;
		instance->data_count = 0;
	} else {		//Block IRQ
		ctrl &= ~ME6000_AO_CTRL_BIT_ENABLE_IRQ;
	}
	outl(ctrl, instance->ctrl_reg);
	PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->ctrl_reg - instance->reg_base, ctrl);

	//Reset interrupt latch
	inl(instance->irq_reset_reg);

	//Fill FIFO <= Generaly this should be done by user pre-load call but this is second place to do it.
	status = inl(instance->status_reg);
	if (!(status & ME6000_AO_STATUS_BIT_EF)) {	//FIFO empty
		if (instance->stop_data_count != 0) {
			count = ME6000_AO_FIFO_COUNT;
		} else {
			count =
			    (ME6000_AO_FIFO_COUNT <
			     instance->
			     stop_data_count) ? ME6000_AO_FIFO_COUNT :
			    instance->stop_data_count;
		}

		//Copy data
		count =
		    ao_write_data(instance, count, instance->preloaded_count);

		if (count < 0) {	//This should never happend!
			PERROR_CRITICAL("COPY FINISH WITH ERROR!\n");
			spin_unlock_irqrestore(&instance->subdevice_lock,
					       cpu_flags);
			ME_SUBDEVICE_EXIT;
			return ME_ERRNO_INTERNAL;
		}
	}
	//Set pre-load features.
	spin_lock(instance->preload_reg_lock);
	synch = inl(instance->preload_reg);
	synch &=
	    ~((ME6000_AO_SYNC_HOLD | ME6000_AO_SYNC_EXT_TRIG) << instance->
	      ao_idx);
	synch |=
	    (instance->start_mode & ~ME6000_AO_EXT_TRIG) << instance->ao_idx;
	outl(synch, instance->preload_reg);
	PDEBUG_REG("preload_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->preload_reg - instance->reg_base, synch);
	spin_unlock(instance->preload_reg_lock);

	//Default count is '0'
	if (instance->mode == ME6000_AO_CONTINOUS) {	//Continous
		instance->preloaded_count = 0;
		instance->circ_buf.tail += count;
		instance->circ_buf.tail &= instance->circ_buf.mask;
	} else {		//Wraparound
		instance->preloaded_count += count;
		instance->data_count += count;

		//Special case: Infinite wraparound with less than FIFO datas always should runs in hardware mode.
		if ((instance->stop_mode == ME6000_AO_INF_STOP_MODE)
		    && (circ_buffer_count <= ME6000_AO_FIFO_COUNT)) {	//Change to hardware wraparound
			PDEBUG
			    ("Changeing mode from software wraparound to hardware wraparound.\n");
			//Copy all data
			count =
			    ao_write_data(instance, circ_buffer_count,
					  instance->preloaded_count);
			ctrl &= ~ME6000_AO_CTRL_MODE_MASK;
			ctrl |= ME6000_AO_MODE_WRAPAROUND;
		}

		if (instance->preloaded_count == me_circ_buf_values(&instance->circ_buf)) {	//Reset position indicator.
			instance->preloaded_count = 0;
		} else if (instance->preloaded_count > me_circ_buf_values(&instance->circ_buf)) {	//This should never happend!
			PERROR_CRITICAL
			    ("PRELOADED MORE VALUES THAN ARE IN BUFFER!\n");
			spin_unlock_irqrestore(&instance->subdevice_lock,
					       cpu_flags);
			ME_SUBDEVICE_EXIT;
			return ME_ERRNO_INTERNAL;
		}
	}

	//Set status to 'wait for start'
	instance->status = ao_status_stream_run_wait;

	status = inl(instance->status_reg);
	//Start state machine and interrupts
	PINFO("<%s:%d> Start state machine.\n", __func__, __LINE__);
	ctrl &= ~(ME6000_AO_CTRL_BIT_STOP | ME6000_AO_CTRL_BIT_IMMEDIATE_STOP);
	if (instance->start_mode == ME6000_AO_EXT_TRIG) {
		PDEBUG("DIGITAL TRIGGER\n");
		ctrl |= ME6000_AO_CTRL_BIT_ENABLE_EX_TRIG;
	}
	if (!(status & ME6000_AO_STATUS_BIT_HF)) {	//More than half!
		if ((ctrl & ME6000_AO_CTRL_MODE_MASK) == ME6000_AO_MODE_CONTINUOUS) {	//Enable IRQ only when hardware_continous is set and FIFO is more than half
			PINFO("<%s:%d> Start interrupts.\n", __func__,
			      __LINE__);
			ctrl |= ME6000_AO_CTRL_BIT_ENABLE_IRQ;
		}
	}
	outl(ctrl, instance->ctrl_reg);
	PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->ctrl_reg - instance->reg_base, ctrl);
	spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);

	//Trigger output
	PINFO("<%s> start mode= 0x%x %s\n", __func__, instance->start_mode,
	      (flags & ME_IO_SINGLE_TYPE_TRIG_SYNCHRONOUS) ? "SYNCHRONOUS" :
	      "");
	if (flags & ME_IO_SINGLE_TYPE_TRIG_SYNCHRONOUS) {	//Trigger outputs
		spin_lock(instance->preload_reg_lock);
		synch = inl(instance->preload_reg);
		//Add channel to start list
		outl(synch | (ME6000_AO_SYNC_HOLD << instance->ao_idx),
		     instance->preload_reg);
		PDEBUG_REG("preload_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->preload_reg - instance->reg_base,
			   synch | (ME6000_AO_SYNC_HOLD << instance->ao_idx));

		//Fire
		PINFO
		    ("Fired all software synchronous outputs by software trigger.\n");
		outl(0x8000, instance->single_reg);
		PDEBUG_REG("single_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->single_reg - instance->reg_base, 0x8000);

		//Restore save settings
		outl(synch, instance->preload_reg);
		PDEBUG_REG("preload_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->preload_reg - instance->reg_base, synch);
		spin_unlock(instance->preload_reg_lock);
	} else if (!instance->start_mode) {	//Trigger outputs
/*
		spin_lock(instance->preload_reg_lock);
			synch = inl(instance->preload_reg);
			//Remove channel from start list
			outl(synch & ~(ME6000_AO_SYNC_HOLD << instance->ao_idx), instance->preload_reg);
			PDEBUG_REG("preload_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base, instance->preload_reg - instance->reg_base, synch & ~(ME6000_AO_SYNC_HOLD << instance->ao_idx));
*/
		//Fire
		PINFO("Software trigger.\n");
		outl(0x8000, instance->single_reg);
		PDEBUG_REG("single_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->single_reg - instance->reg_base, 0x8000);

/*
			//Restore save settings
			outl(synch, instance->preload_reg);
			PDEBUG_REG("preload_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base, instance->preload_reg - instance->reg_base, synch);
		spin_unlock(instance->preload_reg_lock);
*/
	}
	// Set control task's timeout
	instance->timeout.delay = delay;
	instance->timeout.start_time = jiffies;

	if (status & ME6000_AO_STATUS_BIT_HF) {	//Less than half but not empty!
		PINFO("Less than half.\n");
		if (instance->stop_data_count == 0) {
			count = ME6000_AO_FIFO_COUNT / 2;
		} else {
			count =
			    ((ME6000_AO_FIFO_COUNT / 2) <
			     instance->stop_data_count) ? ME6000_AO_FIFO_COUNT /
			    2 : instance->stop_data_count;
		}

		//Copy data
		count =
		    ao_write_data(instance, count, instance->preloaded_count);

		if (count < 0) {	//This should never happend!
			PERROR_CRITICAL("COPY FINISH WITH ERROR!\n");
			ME_SUBDEVICE_EXIT;
			return ME_ERRNO_INTERNAL;
		}

		if (instance->mode == ME6000_AO_CONTINOUS) {	//Continous
			instance->circ_buf.tail += count;
			instance->circ_buf.tail &= instance->circ_buf.mask;
		} else {	//Wraparound
			instance->data_count += count;
			instance->preloaded_count += count;

			if (instance->preloaded_count == me_circ_buf_values(&instance->circ_buf)) {	//Reset position indicator.
				instance->preloaded_count = 0;
			} else if (instance->preloaded_count > me_circ_buf_values(&instance->circ_buf)) {	//This should never happend!
				PERROR_CRITICAL
				    ("PRELOADED MORE VALUES THAN ARE IN BUFFER!\n");
				ME_SUBDEVICE_EXIT;
				return ME_ERRNO_INTERNAL;
			}
		}

		status = inl(instance->status_reg);
		if (!(status & ME6000_AO_STATUS_BIT_HF)) {	//More than half!
			spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);
			PINFO("<%s:%d> Start interrupts.\n", __func__,
			      __LINE__);
			ctrl = inl(instance->ctrl_reg);
			ctrl |= ME6000_AO_CTRL_BIT_ENABLE_IRQ;
			outl(ctrl, instance->ctrl_reg);
			PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->ctrl_reg - instance->reg_base,
				   ctrl);
			spin_unlock_irqrestore(&instance->subdevice_lock,
					       cpu_flags);
		}
	}
	//Special case: Limited wraparound with less than HALF FIFO datas need work around to generate first interrupt.
	if ((instance->stop_mode != ME6000_AO_INF_STOP_MODE)
	    && (instance->mode == ME6000_AO_SW_WRAP_MODE)
	    && (circ_buffer_count <= (ME6000_AO_FIFO_COUNT / 2))) {	//Put more data to FIFO
		PINFO("Limited wraparound with less than HALF FIFO datas.\n");
		if (instance->preloaded_count) {	//This should never happend!
			PERROR_CRITICAL
			    ("ERROR WHEN LOADING VALUES FOR WRAPAROUND!\n");
			ME_SUBDEVICE_EXIT;
			return ME_ERRNO_INTERNAL;
		}

		while (instance->stop_data_count > instance->data_count) {	//Maximum data not set jet.
			//Copy to buffer
			if (circ_buffer_count != ao_write_data(instance, circ_buffer_count, 0)) {	//This should never happend!
				PERROR_CRITICAL
				    ("ERROR WHEN LOADING VALUES FOR WRAPAROUND!\n");
				ME_SUBDEVICE_EXIT;
				return ME_ERRNO_INTERNAL;
			}
			instance->data_count += circ_buffer_count;

			if (!((status = inl(instance->status_reg)) & ME6000_AO_STATUS_BIT_HF)) {	//FIFO is more than half. Enable IRQ and end copy.
				//Reset interrupt latch
				inl(instance->irq_reset_reg);

				spin_lock_irqsave(&instance->subdevice_lock,
						  cpu_flags);
				PINFO("<%s:%d> Start interrupts.\n",
				      __func__, __LINE__);
				ctrl = inl(instance->ctrl_reg);
				ctrl |= ME6000_AO_CTRL_BIT_ENABLE_IRQ;
				outl(ctrl, instance->ctrl_reg);
				PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
					   instance->reg_base,
					   instance->ctrl_reg -
					   instance->reg_base, ctrl);
				spin_unlock_irqrestore(&instance->
						       subdevice_lock,
						       cpu_flags);
				break;
			}
		}
	}
	// Schedule control task
	instance->ao_control_task_flag = 1;
	queue_delayed_work(instance->me6000_workqueue,
			   &instance->ao_control_task, 1);

	if (start_mode == ME_START_MODE_BLOCKING) {	//Wait for start.
		ref = jiffies;
		//Only runing process will interrupt this call. Events are signaled when status change. Extra timeout add for safe reason.
		wait_event_interruptible_timeout(instance->wait_queue,
						 (instance->status !=
						  ao_status_stream_run_wait),
						 (delay) ? delay +
						 1 : LONG_MAX);

		if ((instance->status != ao_status_stream_run)
		    && (instance->status != ao_status_stream_end)) {
			PDEBUG("Starting stream canceled. %d\n",
			       instance->status);
			err = ME_ERRNO_CANCELLED;
		}

		if (signal_pending(current)) {
			PERROR("Wait on start of state machine interrupted.\n");
			instance->status = ao_status_none;
			ao_stop_immediately(instance);
			err = ME_ERRNO_SIGNAL;
		}

		if ((delay) && ((jiffies - ref) >= delay)) {
			if (instance->status != ao_status_stream_run) {
				if (instance->status == ao_status_stream_end) {
					PDEBUG("Timeout reached.\n");
				} else if ((jiffies - ref) > delay) {
					PERROR
					    ("Timeout reached. Not handled by control task!\n");
					ao_stop_immediately(instance);
				} else {
					PERROR
					    ("Timeout reached. Signal come but status is strange: %d\n",
					     instance->status);
					ao_stop_immediately(instance);
				}

				instance->ao_control_task_flag = 0;
				cancel_delayed_work(&instance->ao_control_task);
				instance->status = ao_status_stream_end;
				err = ME_ERRNO_TIMEOUT;
			}
		}
	}

	ME_SUBDEVICE_EXIT;
	return err;
}

static int me6000_ao_io_stream_status(me_subdevice_t *subdevice,
				      struct file *filep,
				      int wait,
				      int *status, int *values, int flags)
{
	me6000_ao_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;

	instance = (me6000_ao_subdevice_t *) subdevice;

	PDEBUG("executed. idx=%d\n", instance->ao_idx);

	if (!(instance->fifo & ME6000_AO_HAS_FIFO)) {
		PERROR("Not a streaming ao.\n");
		return ME_ERRNO_NOT_SUPPORTED;
	}

	if (flags) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	if ((wait != ME_WAIT_NONE) && (wait != ME_WAIT_IDLE)) {
		PERROR("Invalid wait argument specified.\n");
		*status = ME_STATUS_INVALID;
		return ME_ERRNO_INVALID_WAIT;
	}

	ME_SUBDEVICE_ENTER;

	switch (instance->status) {
	case ao_status_single_configured:
	case ao_status_single_end:
	case ao_status_stream_configured:
	case ao_status_stream_end:
	case ao_status_stream_fifo_error:
	case ao_status_stream_buffer_error:
	case ao_status_stream_error:
		*status = ME_STATUS_IDLE;
		break;

	case ao_status_single_run_wait:
	case ao_status_single_run:
	case ao_status_single_end_wait:
	case ao_status_stream_run_wait:
	case ao_status_stream_run:
	case ao_status_stream_end_wait:
		*status = ME_STATUS_BUSY;
		break;

	case ao_status_none:
	default:
		*status =
		    (inl(instance->status_reg) & ME6000_AO_STATUS_BIT_FSM) ?
		    ME_STATUS_BUSY : ME_STATUS_IDLE;
		break;
	}

	if ((wait == ME_WAIT_IDLE) && (*status == ME_STATUS_BUSY)) {
		//Only runing process will interrupt this call. Events are signaled when status change. Extra timeout add for safe reason.
		wait_event_interruptible_timeout(instance->wait_queue,
						 ((instance->status !=
						   ao_status_single_run_wait)
						  && (instance->status !=
						      ao_status_single_run)
						  && (instance->status !=
						      ao_status_single_end_wait)
						  && (instance->status !=
						      ao_status_stream_run_wait)
						  && (instance->status !=
						      ao_status_stream_run)
						  && (instance->status !=
						      ao_status_stream_end_wait)),
						 LONG_MAX);

		if (instance->status != ao_status_stream_end) {
			PDEBUG("Wait for IDLE canceled. %d\n",
			       instance->status);
			err = ME_ERRNO_CANCELLED;
		}

		if (signal_pending(current)) {
			PERROR("Wait for IDLE interrupted.\n");
			instance->status = ao_status_none;
			ao_stop_immediately(instance);
			err = ME_ERRNO_SIGNAL;
		}

		*status = ME_STATUS_IDLE;
	}

	*values = me_circ_buf_space(&instance->circ_buf);

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me6000_ao_io_stream_stop(me_subdevice_t *subdevice,
				    struct file *filep,
				    int stop_mode, int flags)
{				/// @note Stop work and empty buffer and FIFO
	int err = ME_ERRNO_SUCCESS;
	me6000_ao_subdevice_t *instance;
	unsigned long cpu_flags;
	volatile uint32_t ctrl;

	instance = (me6000_ao_subdevice_t *) subdevice;

	PDEBUG("executed. idx=%d\n", instance->ao_idx);

	if (flags & ~ME_IO_STREAM_STOP_PRESERVE_BUFFERS) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	if ((stop_mode != ME_STOP_MODE_IMMEDIATE)
	    && (stop_mode != ME_STOP_MODE_LAST_VALUE)) {
		PERROR("Invalid stop mode specified.\n");
		return ME_ERRNO_INVALID_STOP_MODE;
	}

	if (!(instance->fifo & ME6000_AO_HAS_FIFO)) {
		PERROR("Not a streaming ao.\n");
		return ME_ERRNO_NOT_SUPPORTED;
	}

	if (instance->status < ao_status_stream_configured) {
		//There is nothing to stop!
		PERROR("Subdevice not in streaming mode. %d\n",
		       instance->status);
		return ME_ERRNO_PREVIOUS_CONFIG;
	}

	ME_SUBDEVICE_ENTER;

	//Mark as stopping. => Software stop.
	instance->status = ao_status_stream_end_wait;

	if (stop_mode == ME_STOP_MODE_IMMEDIATE) {	//Stopped now!
		err = ao_stop_immediately(instance);
	} else if (stop_mode == ME_STOP_MODE_LAST_VALUE) {
		ctrl = inl(instance->ctrl_reg) & ME6000_AO_CTRL_MODE_MASK;
		if (ctrl == ME6000_AO_MODE_WRAPAROUND) {	//Hardware wraparound => Hardware stop.
			spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);
			ctrl = inl(instance->ctrl_reg);
			ctrl |= ME6000_AO_CTRL_BIT_STOP;
			ctrl &= ~ME6000_AO_CTRL_BIT_ENABLE_IRQ;
			outl(ctrl, instance->ctrl_reg);
			PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->ctrl_reg - instance->reg_base,
				   ctrl);
			spin_unlock_irqrestore(&instance->subdevice_lock,
					       cpu_flags);

			//Reset interrupt latch
			inl(instance->irq_reset_reg);
		}
		//Only runing process will interrupt this call. Events are signaled when status change. Extra timeout add for safe reason.
		wait_event_interruptible_timeout(instance->wait_queue,
						 (instance->status !=
						  ao_status_stream_end_wait),
						 LONG_MAX);

		if (instance->status != ao_status_stream_end) {
			PDEBUG("Stopping stream canceled.\n");
			err = ME_ERRNO_CANCELLED;
		}

		if (signal_pending(current)) {
			PERROR("Stopping stream interrupted.\n");
			instance->status = ao_status_none;
			ao_stop_immediately(instance);
			err = ME_ERRNO_SIGNAL;
		}
	}

	spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);
	ctrl = inl(instance->ctrl_reg);
	ctrl |= ME6000_AO_CTRL_BIT_STOP | ME6000_AO_CTRL_BIT_IMMEDIATE_STOP;
	ctrl &= ~ME6000_AO_CTRL_BIT_ENABLE_IRQ;
	if (!flags) {		//Reset FIFO
		ctrl &= ~ME6000_AO_CTRL_BIT_ENABLE_FIFO;
	}
	outl(ctrl, instance->ctrl_reg);
	PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->ctrl_reg - instance->reg_base, ctrl);
	spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);

	//Reset interrupt latch
	inl(instance->irq_reset_reg);

	if (!flags) {		//Reset software buffer
		instance->circ_buf.head = 0;
		instance->circ_buf.tail = 0;
		instance->preloaded_count = 0;
		instance->data_count = 0;
	}

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me6000_ao_io_stream_write(me_subdevice_t *subdevice,
				     struct file *filep,
				     int write_mode,
				     int *values, int *count, int flags)
{
	int err = ME_ERRNO_SUCCESS;
	me6000_ao_subdevice_t *instance;
	unsigned long cpu_flags = 0;
	uint32_t reg_copy;

	int copied_from_user = 0;
	int left_to_copy_from_user = *count;

	int copied_values;

	instance = (me6000_ao_subdevice_t *) subdevice;

	PDEBUG("executed. idx=%d\n", instance->ao_idx);

	//Checking arguments
	if (!(instance->fifo & ME6000_AO_HAS_FIFO)) {
		PERROR("Not a streaming ao.\n");
		return ME_ERRNO_NOT_SUPPORTED;
	}

	if (flags) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	if (*count <= 0) {
		PERROR("Invalid count of values specified.\n");
		return ME_ERRNO_INVALID_VALUE_COUNT;
	}

	if (values == NULL) {
		PERROR("Invalid address of values specified.\n");
		return ME_ERRNO_INVALID_POINTER;
	}

	if ((instance->status == ao_status_none) || (instance->status == ao_status_single_configured)) {	//The device is in single mode.
		PERROR
		    ("Subdevice must be preinitialize correctly for streaming.\n");
		return ME_ERRNO_PREVIOUS_CONFIG;
	}

	switch (write_mode) {
	case ME_WRITE_MODE_PRELOAD:

		//Device must be stopped.
		if ((instance->status != ao_status_stream_configured)
		    && (instance->status != ao_status_stream_end)) {
			PERROR
			    ("Subdevice mustn't be runing when 'pre-load' mode is used.\n");
			return ME_ERRNO_PREVIOUS_CONFIG;
		}
		break;
	case ME_WRITE_MODE_NONBLOCKING:
	case ME_WRITE_MODE_BLOCKING:
		/// @note In blocking mode: When device is not runing and there is not enought space call will blocked up!
		/// @note Some other thread must empty buffer by strating engine.
		break;

	default:
		PERROR("Invalid write mode specified.\n");
		return ME_ERRNO_INVALID_WRITE_MODE;
	}

	if (instance->mode & ME6000_AO_WRAP_MODE) {	//Wraparound mode. Device must be stopped.
		if ((instance->status != ao_status_stream_configured)
		    && (instance->status != ao_status_stream_end)) {
			PERROR
			    ("Subdevice mustn't be runing when 'pre-load' mode is used.\n");
			return ME_ERRNO_INVALID_WRITE_MODE;
		}
	}

	if ((instance->mode == ME6000_AO_HW_WRAP_MODE)
	    && (write_mode != ME_WRITE_MODE_PRELOAD)) {
/*
		PERROR("Only 'pre-load' write is acceptable in hardware wraparound mode.\n");
		return ME_ERRNO_PREVIOUS_CONFIG;
*/
		//This is transparent for user.
		PDEBUG("Changing write_mode to ME_WRITE_MODE_PRELOAD.\n");
		write_mode = ME_WRITE_MODE_PRELOAD;
	}

	ME_SUBDEVICE_ENTER;

	if (write_mode == ME_WRITE_MODE_PRELOAD) {	//Init enviroment - preload
		spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);
		reg_copy = inl(instance->ctrl_reg);
		//Check FIFO
		if (!(reg_copy & ME6000_AO_CTRL_BIT_ENABLE_FIFO)) {	//FIFO not active. Enable it.
			reg_copy |= ME6000_AO_CTRL_BIT_ENABLE_FIFO;
			outl(reg_copy, instance->ctrl_reg);
			PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->ctrl_reg - instance->reg_base,
				   reg_copy);
			instance->preloaded_count = 0;
		}
		spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);
	}

	while (1) {
		//Copy to buffer. This step is common for all modes.
		copied_from_user =
		    ao_get_data_from_user(instance, left_to_copy_from_user,
					  values + (*count -
						    left_to_copy_from_user));
		left_to_copy_from_user -= copied_from_user;

		reg_copy = inl(instance->status_reg);
		if ((instance->status == ao_status_stream_run) && !(reg_copy & ME6000_AO_STATUS_BIT_FSM)) {	//BROKEN PIPE! The state machine is stoped but logical status show that should be working.
			PERROR("Broken pipe in write.\n");
			err = ME_ERRNO_SUBDEVICE_NOT_RUNNING;
			break;
		}

		if ((instance->status == ao_status_stream_run) && (instance->mode == ME6000_AO_CONTINOUS) && (reg_copy & ME6000_AO_STATUS_BIT_HF)) {	//Continous mode runing and data are below half!

			// Block interrupts.
			spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);
			reg_copy = inl(instance->ctrl_reg);
			reg_copy &= ~ME6000_AO_CTRL_BIT_ENABLE_IRQ;
			outl(reg_copy, instance->ctrl_reg);
			PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->ctrl_reg - instance->reg_base,
				   reg_copy);
			spin_unlock_irqrestore(&instance->subdevice_lock,
					       cpu_flags);

			//Fast copy
			copied_values =
			    ao_write_data(instance, ME6000_AO_FIFO_COUNT / 2,
					  0);
			if (copied_values > 0) {
				instance->circ_buf.tail += copied_values;
				instance->circ_buf.tail &=
				    instance->circ_buf.mask;
				continue;
			}
			//Reset interrupt latch
			inl(instance->irq_reset_reg);

			// Activate interrupts.
			spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);
			reg_copy = inl(instance->ctrl_reg);
			reg_copy |= ME6000_AO_CTRL_BIT_ENABLE_IRQ;
			outl(reg_copy, instance->ctrl_reg);
			PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->ctrl_reg - instance->reg_base,
				   reg_copy);
			spin_unlock_irqrestore(&instance->subdevice_lock,
					       cpu_flags);

			if (copied_values == 0) {	//This was checked and never should happend!
				PERROR_CRITICAL("COPY FINISH WITH 0!\n");
			}

			if (copied_values < 0) {	//This was checked and never should happend!
				PERROR_CRITICAL("COPY FINISH WITH ERROR!\n");
				instance->status = ao_status_stream_fifo_error;
				err = ME_ERRNO_FIFO_BUFFER_OVERFLOW;
				break;
			}
		}

		if (!left_to_copy_from_user) {	//All datas were copied.
			break;
		} else {	//Not all datas were copied.
			if (instance->mode & ME6000_AO_WRAP_MODE) {	//Error too much datas! Wraparound is limited in size!
				PERROR
				    ("Too much data for wraparound mode!  Exceeded size of %ld.\n",
				     ME6000_AO_CIRC_BUF_COUNT - 1);
				err = ME_ERRNO_RING_BUFFER_OVERFLOW;
				break;
			}

			if (write_mode != ME_WRITE_MODE_BLOCKING) {	//Non blocking calls
				break;
			}

			wait_event_interruptible(instance->wait_queue,
						 me_circ_buf_space(&instance->
								   circ_buf));

			if (signal_pending(current)) {
				PERROR("Writing interrupted by signal.\n");
				instance->status = ao_status_none;
				ao_stop_immediately(instance);
				err = ME_ERRNO_SIGNAL;
				break;
			}

			if (instance->status == ao_status_none) {	//Reset
				PERROR("Writing interrupted by reset.\n");
				err = ME_ERRNO_CANCELLED;
				break;
			}
		}
	}

	if (write_mode == ME_WRITE_MODE_PRELOAD) {	//Copy data to FIFO - preload
		copied_values =
		    ao_write_data_pooling(instance, ME6000_AO_FIFO_COUNT,
					  instance->preloaded_count);
		instance->preloaded_count += copied_values;
		instance->data_count += copied_values;

		if ((instance->mode == ME6000_AO_HW_WRAP_MODE)
		    && (me_circ_buf_values(&instance->circ_buf) >
			ME6000_AO_FIFO_COUNT)) {
			PERROR
			    ("Too much data for hardware wraparound mode! Exceeded size of %d.\n",
			     ME6000_AO_FIFO_COUNT);
			err = ME_ERRNO_FIFO_BUFFER_OVERFLOW;
		}
	}

	*count = *count - left_to_copy_from_user;
	ME_SUBDEVICE_EXIT;

	return err;
}

static irqreturn_t me6000_ao_isr(int irq, void *dev_id)
{
	me6000_ao_subdevice_t *instance = dev_id;
	uint32_t irq_status;
	uint32_t ctrl;
	uint32_t status;
	int count = 0;

	PDEBUG("executed. idx=%d\n", instance->ao_idx);

	if (irq != instance->irq) {
		PERROR("Incorrect interrupt num: %d.\n", irq);
		return IRQ_NONE;
	}

	irq_status = inl(instance->irq_status_reg);
	if (!(irq_status & (ME6000_IRQ_STATUS_BIT_AO_HF << instance->ao_idx))) {
		PINFO("%ld Shared interrupt. %s(): ID=%d: status_reg=0x%04X\n",
		      jiffies, __func__, instance->ao_idx, irq_status);
		return IRQ_NONE;
	}

	if (!instance->circ_buf.buf) {
		instance->status = ao_status_stream_error;
		PERROR_CRITICAL("CIRCULAR BUFFER NOT EXISTS!\n");
		//Block interrupts. Stop machine.
		ctrl = inl(instance->ctrl_reg);
		ctrl &= ~ME6000_AO_CTRL_BIT_ENABLE_IRQ;
		ctrl |=
		    ME6000_AO_CTRL_BIT_IMMEDIATE_STOP | ME6000_AO_CTRL_BIT_STOP;
		outl(ctrl, instance->ctrl_reg);
		PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->ctrl_reg - instance->reg_base, ctrl);

		//Inform user
		wake_up_interruptible_all(&instance->wait_queue);
		return IRQ_HANDLED;
	}

	status = inl(instance->status_reg);
	if (!(status & ME6000_AO_STATUS_BIT_FSM)) {	//Too late. Not working! END? BROKEN PIPE?
		/// @note Error checking was moved to separate task.
		PDEBUG("Interrupt come but ISM is not working!\n");
		//Block interrupts. Stop machine.
		ctrl = inl(instance->ctrl_reg);
		ctrl &= ~ME6000_AO_CTRL_BIT_ENABLE_IRQ;
		ctrl |=
		    ME6000_AO_CTRL_BIT_STOP | ME6000_AO_CTRL_BIT_IMMEDIATE_STOP;
		outl(ctrl, instance->ctrl_reg);
		PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->ctrl_reg - instance->reg_base, ctrl);

		//Reset interrupt latch
		inl(instance->irq_reset_reg);

		/// @note User notification was also moved to separate task.
		return IRQ_HANDLED;
	}
	//General procedure. Process more datas.

#ifdef MEDEBUG_DEBUG
	if (!me_circ_buf_values(&instance->circ_buf)) {	//Buffer is empty!
		PDEBUG("Circular buffer empty!\n");
	}
#endif

	//Check FIFO
	if (status & ME6000_AO_STATUS_BIT_HF) {	//OK less than half

		//Block interrupts
		ctrl = inl(instance->ctrl_reg);
		ctrl &= ~ME6000_AO_CTRL_BIT_ENABLE_IRQ;
		outl(ctrl, instance->ctrl_reg);
		PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->ctrl_reg - instance->reg_base, ctrl);

		do {
			//Calculate how many should be copied.
			count =
			    (instance->stop_data_count) ? instance->
			    stop_data_count -
			    instance->data_count : ME6000_AO_FIFO_COUNT / 2;
			if (ME6000_AO_FIFO_COUNT / 2 < count) {
				count = ME6000_AO_FIFO_COUNT / 2;
			}
			//Copy data
			if (instance->mode == ME6000_AO_CONTINOUS) {	//Continous
				count = ao_write_data(instance, count, 0);
				if (count > 0) {
					instance->circ_buf.tail += count;
					instance->circ_buf.tail &=
					    instance->circ_buf.mask;
					instance->data_count += count;

					if ((instance->status == ao_status_stream_end_wait) && !me_circ_buf_values(&instance->circ_buf)) {	//Stoping. Whole buffer was copied.
						break;
					}
				}
			} else if ((instance->mode == ME6000_AO_SW_WRAP_MODE) && ((ctrl & ME6000_AO_CTRL_MODE_MASK) == ME6000_AO_MODE_CONTINUOUS)) {	//Wraparound (software)
				if (instance->status == ao_status_stream_end_wait) {	//We stoping => Copy to the end of the buffer.
					count =
					    ao_write_data(instance, count, 0);
				} else {	//Copy in wraparound mode.
					count =
					    ao_write_data_wraparound(instance,
								     count,
								     instance->
								     preloaded_count);
				}

				if (count > 0) {
					instance->data_count += count;
					instance->preloaded_count += count;
					instance->preloaded_count %=
					    me_circ_buf_values(&instance->
							       circ_buf);

					if ((instance->status == ao_status_stream_end_wait) && !instance->preloaded_count) {	//Stoping. Whole buffer was copied.
						break;
					}
				}
			}

			if ((count <= 0) || (instance->stop_data_count && (instance->stop_data_count <= instance->data_count))) {	//End of work.
				break;
			}
		}		//Repeat if still is under half fifo
		while ((status =
			inl(instance->status_reg)) & ME6000_AO_STATUS_BIT_HF);

		//Unblock interrupts
		ctrl = inl(instance->ctrl_reg);
		if (count >= 0) {	//Copy was successful.
			if (instance->stop_data_count && (instance->stop_data_count <= instance->data_count)) {	//Finishing work. No more interrupts.
				PDEBUG("Finishing work. Interrupt disabled.\n");
				instance->status = ao_status_stream_end_wait;
			} else if (count > 0) {	//Normal work. Enable interrupt.
				PDEBUG("Normal work. Enable interrupt.\n");
				ctrl |= ME6000_AO_CTRL_BIT_ENABLE_IRQ;
			} else {	//Normal work but there are no more data in buffer. Interrupt blocked. stream_write() will unblock it.
				PDEBUG
				    ("No data in software buffer. Interrupt blocked.\n");
			}
		} else {	//Error during copy.
			instance->status = ao_status_stream_fifo_error;
		}

		outl(ctrl, instance->ctrl_reg);
		PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->ctrl_reg - instance->reg_base, ctrl);
	} else {		//?? more than half
		PDEBUG
		    ("Interrupt come but FIFO more than half full! Reset interrupt.\n");
	}

	PINFO("ISR: Buffer count: %d.(T:%d H:%d)\n",
	      me_circ_buf_values(&instance->circ_buf), instance->circ_buf.tail,
	      instance->circ_buf.head);
	PINFO("ISR: Stop count: %d.\n", instance->stop_count);
	PINFO("ISR: Stop data count: %d.\n", instance->stop_data_count);
	PINFO("ISR: Data count: %d.\n", instance->data_count);

	//Reset interrupt latch
	inl(instance->irq_reset_reg);

	//Inform user
	wake_up_interruptible_all(&instance->wait_queue);

	return IRQ_HANDLED;
}

static void me6000_ao_destructor(struct me_subdevice *subdevice)
{
	me6000_ao_subdevice_t *instance;

	instance = (me6000_ao_subdevice_t *) subdevice;

	PDEBUG("executed. idx=%d\n", instance->ao_idx);

	instance->ao_control_task_flag = 0;

	// Reset subdevice to asure clean exit.
	me6000_ao_io_reset_subdevice(subdevice, NULL,
				     ME_IO_RESET_SUBDEVICE_NO_FLAGS);

	// Remove any tasks from work queue. This is paranoic because it was done allready in reset().
	if (!cancel_delayed_work(&instance->ao_control_task)) {	//Wait 2 ticks to be sure that control task is removed from queue.
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(2);
	}

	if (instance->fifo & ME6000_AO_HAS_FIFO) {
		if (instance->irq) {
			free_irq(instance->irq, instance);
			instance->irq = 0;
		}

		if (instance->circ_buf.buf) {
			PDEBUG("free circ_buf = %p size=%d",
			       instance->circ_buf.buf,
			       PAGE_SHIFT << ME6000_AO_CIRC_BUF_SIZE_ORDER);
			free_pages((unsigned long)instance->circ_buf.buf,
				   ME6000_AO_CIRC_BUF_SIZE_ORDER);
		}
		instance->circ_buf.buf = NULL;
	}

	me_subdevice_deinit(&instance->base);
	kfree(instance);
}

me6000_ao_subdevice_t *me6000_ao_constructor(uint32_t reg_base,
					     spinlock_t *preload_reg_lock,
					     uint32_t *preload_flags,
					     uint32_t *triggering_flags,
					     int ao_idx,
					     int fifo,
					     int irq,
					     int high_range,
					     struct workqueue_struct *me6000_wq)
{
	me6000_ao_subdevice_t *subdevice;
	int err;

	PDEBUG("executed ID=%d.\n", ao_idx);

	/* Allocate memory for subdevice instance */
	subdevice = kmalloc(sizeof(me6000_ao_subdevice_t), GFP_KERNEL);

	if (!subdevice) {
		PERROR("Cannot get memory for subdevice instance.\n");
		return NULL;
	}

	memset(subdevice, 0, sizeof(me6000_ao_subdevice_t));

	/* Initialize subdevice base class */
	err = me_subdevice_init(&subdevice->base);

	if (err) {
		PERROR("Cannot initialize subdevice base class instance.\n");
		kfree(subdevice);
		return NULL;
	}
	// Initialize spin locks.
	spin_lock_init(&subdevice->subdevice_lock);

	subdevice->preload_reg_lock = preload_reg_lock;
	subdevice->preload_flags = preload_flags;
	subdevice->triggering_flags = triggering_flags;

	/* Store analog output index */
	subdevice->ao_idx = ao_idx;

	/* Store if analog output has fifo */
	subdevice->fifo = fifo;

	if (subdevice->fifo & ME6000_AO_HAS_FIFO) {
		/* Allocate and initialize circular buffer */
		subdevice->circ_buf.mask = ME6000_AO_CIRC_BUF_COUNT - 1;
		subdevice->circ_buf.buf =
		    (void *)__get_free_pages(GFP_KERNEL,
					     ME6000_AO_CIRC_BUF_SIZE_ORDER);
		PDEBUG("circ_buf = %p size=%ld\n", subdevice->circ_buf.buf,
		       ME6000_AO_CIRC_BUF_SIZE);

		if (!subdevice->circ_buf.buf) {
			PERROR
			    ("Cannot initialize subdevice base class instance.\n");
			kfree(subdevice);
			return NULL;
		}

		memset(subdevice->circ_buf.buf, 0, ME6000_AO_CIRC_BUF_SIZE);
	} else {
		subdevice->circ_buf.mask = 0;
		subdevice->circ_buf.buf = NULL;
	}
	subdevice->circ_buf.head = 0;
	subdevice->circ_buf.tail = 0;

	subdevice->status = ao_status_none;
	subdevice->ao_control_task_flag = 0;
	subdevice->timeout.delay = 0;
	subdevice->timeout.start_time = jiffies;

	/* Initialize wait queue */
	init_waitqueue_head(&subdevice->wait_queue);

	/* Initialize single value to 0V */
	subdevice->single_value = 0x8000;
	subdevice->single_value_in_fifo = 0x8000;

	/* Initialize range boarders */
	if (high_range) {
		subdevice->min = ME6000_AO_MIN_RANGE_HIGH;
		subdevice->max = ME6000_AO_MAX_RANGE_HIGH;
	} else {
		subdevice->min = ME6000_AO_MIN_RANGE;
		subdevice->max = ME6000_AO_MAX_RANGE;
	}

	/* Register interrupt service routine */

	if (subdevice->fifo & ME6000_AO_HAS_FIFO) {
		subdevice->irq = irq;
		if (request_irq(subdevice->irq, me6000_ao_isr,
				IRQF_DISABLED | IRQF_SHARED,
				ME6000_NAME, subdevice)) {
			PERROR("Cannot get interrupt line.\n");
			PDEBUG("free circ_buf = %p size=%d",
			       subdevice->circ_buf.buf,
			       PAGE_SHIFT << ME6000_AO_CIRC_BUF_SIZE_ORDER);
			free_pages((unsigned long)subdevice->circ_buf.buf,
				   ME6000_AO_CIRC_BUF_SIZE_ORDER);
			subdevice->circ_buf.buf = NULL;
			kfree(subdevice);
			return NULL;
		}
		PINFO("Registered irq=%d.\n", subdevice->irq);
	} else {
		subdevice->irq = 0;
	}

	/* Initialize registers */
	// Only streamed subdevices support interrupts. For the rest this register has no meaning.
	subdevice->irq_status_reg = reg_base + ME6000_AO_IRQ_STATUS_REG;
	subdevice->preload_reg = reg_base + ME6000_AO_PRELOAD_REG;

	if (ao_idx == 0) {
		subdevice->ctrl_reg = reg_base + ME6000_AO_00_CTRL_REG;
		subdevice->status_reg = reg_base + ME6000_AO_00_STATUS_REG;
		subdevice->fifo_reg = reg_base + ME6000_AO_00_FIFO_REG;
		subdevice->timer_reg = reg_base + ME6000_AO_00_TIMER_REG;
		subdevice->irq_reset_reg =
		    reg_base + ME6000_AO_00_IRQ_RESET_REG;
		subdevice->single_reg = reg_base + ME6000_AO_00_SINGLE_REG;
	} else if (ao_idx == 1) {
		subdevice->ctrl_reg = reg_base + ME6000_AO_01_CTRL_REG;
		subdevice->status_reg = reg_base + ME6000_AO_01_STATUS_REG;
		subdevice->fifo_reg = reg_base + ME6000_AO_01_FIFO_REG;
		subdevice->timer_reg = reg_base + ME6000_AO_01_TIMER_REG;
		subdevice->irq_reset_reg =
		    reg_base + ME6000_AO_01_IRQ_RESET_REG;
		subdevice->single_reg = reg_base + ME6000_AO_01_SINGLE_REG;
	} else if (ao_idx == 2) {
		subdevice->ctrl_reg = reg_base + ME6000_AO_02_CTRL_REG;
		subdevice->status_reg = reg_base + ME6000_AO_02_STATUS_REG;
		subdevice->fifo_reg = reg_base + ME6000_AO_02_FIFO_REG;
		subdevice->timer_reg = reg_base + ME6000_AO_02_TIMER_REG;
		subdevice->irq_reset_reg =
		    reg_base + ME6000_AO_02_IRQ_RESET_REG;
		subdevice->single_reg = reg_base + ME6000_AO_02_SINGLE_REG;
	} else if (ao_idx == 3) {
		subdevice->ctrl_reg = reg_base + ME6000_AO_03_CTRL_REG;
		subdevice->status_reg = reg_base + ME6000_AO_03_STATUS_REG;
		subdevice->fifo_reg = reg_base + ME6000_AO_03_FIFO_REG;
		subdevice->timer_reg = reg_base + ME6000_AO_03_TIMER_REG;
		subdevice->irq_reset_reg =
		    reg_base + ME6000_AO_03_IRQ_RESET_REG;
		subdevice->single_reg = reg_base + ME6000_AO_03_SINGLE_REG;
	} else {
		subdevice->ctrl_reg = reg_base + ME6000_AO_DUMY;
		subdevice->fifo_reg = reg_base + ME6000_AO_DUMY;
		subdevice->timer_reg = reg_base + ME6000_AO_DUMY;
		subdevice->irq_reset_reg = reg_base + ME6000_AO_DUMY;
		subdevice->single_reg = reg_base + ME6000_AO_DUMY;

		subdevice->status_reg = reg_base + ME6000_AO_SINGLE_STATUS_REG;
		if (ao_idx == 4) {
			subdevice->single_reg =
			    reg_base + ME6000_AO_04_SINGLE_REG;
		} else if (ao_idx == 5) {
			subdevice->single_reg =
			    reg_base + ME6000_AO_05_SINGLE_REG;
		} else if (ao_idx == 6) {
			subdevice->single_reg =
			    reg_base + ME6000_AO_06_SINGLE_REG;
		} else if (ao_idx == 7) {
			subdevice->single_reg =
			    reg_base + ME6000_AO_07_SINGLE_REG;
		} else if (ao_idx == 8) {
			subdevice->single_reg =
			    reg_base + ME6000_AO_08_SINGLE_REG;
		} else if (ao_idx == 9) {
			subdevice->single_reg =
			    reg_base + ME6000_AO_09_SINGLE_REG;
		} else if (ao_idx == 10) {
			subdevice->single_reg =
			    reg_base + ME6000_AO_10_SINGLE_REG;
		} else if (ao_idx == 11) {
			subdevice->single_reg =
			    reg_base + ME6000_AO_11_SINGLE_REG;
		} else if (ao_idx == 12) {
			subdevice->single_reg =
			    reg_base + ME6000_AO_12_SINGLE_REG;
		} else if (ao_idx == 13) {
			subdevice->single_reg =
			    reg_base + ME6000_AO_13_SINGLE_REG;
		} else if (ao_idx == 14) {
			subdevice->single_reg =
			    reg_base + ME6000_AO_14_SINGLE_REG;
		} else if (ao_idx == 15) {
			subdevice->single_reg =
			    reg_base + ME6000_AO_15_SINGLE_REG;
		} else {
			PERROR_CRITICAL("WRONG SUBDEVICE ID=%d!", ao_idx);
			me_subdevice_deinit((me_subdevice_t *) subdevice);
			if (subdevice->fifo) {
				free_pages((unsigned long)subdevice->circ_buf.
					   buf, ME6000_AO_CIRC_BUF_SIZE_ORDER);
			}
			subdevice->circ_buf.buf = NULL;
			kfree(subdevice);
			return NULL;
		}
	}
#ifdef MEDEBUG_DEBUG_REG
	subdevice->reg_base = reg_base;
#endif

	/* Override base class methods. */
	subdevice->base.me_subdevice_destructor = me6000_ao_destructor;
	subdevice->base.me_subdevice_io_reset_subdevice =
	    me6000_ao_io_reset_subdevice;
	subdevice->base.me_subdevice_io_single_config =
	    me6000_ao_io_single_config;
	subdevice->base.me_subdevice_io_single_read = me6000_ao_io_single_read;
	subdevice->base.me_subdevice_io_single_write =
	    me6000_ao_io_single_write;
	subdevice->base.me_subdevice_io_stream_config =
	    me6000_ao_io_stream_config;
	subdevice->base.me_subdevice_io_stream_new_values =
	    me6000_ao_io_stream_new_values;
	subdevice->base.me_subdevice_io_stream_write =
	    me6000_ao_io_stream_write;
	subdevice->base.me_subdevice_io_stream_start =
	    me6000_ao_io_stream_start;
	subdevice->base.me_subdevice_io_stream_status =
	    me6000_ao_io_stream_status;
	subdevice->base.me_subdevice_io_stream_stop = me6000_ao_io_stream_stop;
	subdevice->base.me_subdevice_query_number_channels =
	    me6000_ao_query_number_channels;
	subdevice->base.me_subdevice_query_subdevice_type =
	    me6000_ao_query_subdevice_type;
	subdevice->base.me_subdevice_query_subdevice_caps =
	    me6000_ao_query_subdevice_caps;
	subdevice->base.me_subdevice_query_subdevice_caps_args =
	    me6000_ao_query_subdevice_caps_args;
	subdevice->base.me_subdevice_query_range_by_min_max =
	    me6000_ao_query_range_by_min_max;
	subdevice->base.me_subdevice_query_number_ranges =
	    me6000_ao_query_number_ranges;
	subdevice->base.me_subdevice_query_range_info =
	    me6000_ao_query_range_info;
	subdevice->base.me_subdevice_query_timer = me6000_ao_query_timer;

	//prepare work queue and work function
	subdevice->me6000_workqueue = me6000_wq;

/* workqueue API changed in kernel 2.6.20 */
	INIT_DELAYED_WORK(&subdevice->ao_control_task,
			  me6000_ao_work_control_task);

	if (subdevice->fifo) {	//Set speed
		outl(ME6000_AO_MIN_CHAN_TICKS - 1, subdevice->timer_reg);
		subdevice->hardware_stop_delay = HZ / 10;	//100ms
	}

	return subdevice;
}

/** @brief Stop presentation. Preserve FIFOs.
*
* @param instance The subdevice instance (pointer).
*/
inline int ao_stop_immediately(me6000_ao_subdevice_t *instance)
{
	unsigned long cpu_flags;
	uint32_t ctrl;
	int timeout;
	int i;
	uint32_t single_mask;

	if (instance->ao_idx < ME6000_AO_SINGLE_STATUS_OFFSET)
		single_mask = 0x0000;
	else
		single_mask = 0x0001 << (instance->ao_idx -
				ME6000_AO_SINGLE_STATUS_OFFSET);

	timeout =
	    (instance->hardware_stop_delay >
	     (HZ / 10)) ? instance->hardware_stop_delay : HZ / 10;
	for (i = 0; i <= timeout; i++) {
		if (instance->fifo) {
			spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);
			// Stop all actions. No conditions! Block interrupts. Leave FIFO untouched!
			ctrl = inl(instance->ctrl_reg);
			ctrl |=
			    ME6000_AO_CTRL_BIT_STOP |
			    ME6000_AO_CTRL_BIT_IMMEDIATE_STOP;
			ctrl &=
			    ~(ME6000_AO_CTRL_BIT_ENABLE_IRQ |
			      ME6000_AO_CTRL_BIT_ENABLE_EX_TRIG);
			outl(ctrl, instance->ctrl_reg);
			PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->ctrl_reg - instance->reg_base,
				   ctrl);
			spin_unlock_irqrestore(&instance->subdevice_lock,
					       cpu_flags);

			if (!(inl(instance->status_reg) & ME6000_AO_STATUS_BIT_FSM)) {	// Exit.
				break;
			}
		} else {
			if (!(inl(instance->status_reg) & single_mask)) {	// Exit.
				break;
			}
		}

		PINFO("<%s> Wait for stop: %d\n", __func__, i);

		//Still working!
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(1);
	}

	if (i > timeout) {
		PERROR_CRITICAL("FSM IS BUSY!\n");
		return ME_ERRNO_INTERNAL;
	}
	return ME_ERRNO_SUCCESS;
}

/** @brief Copy data from circular buffer to fifo (fast) in wraparound.
* @note This is time critical function. Checking is done at begining and end only.
* @note The is not reasonable way to check how many walues was in FIFO at begining. The count must be managed externaly.
*
* @param instance The subdevice instance (pointer).
* @param count Maximum number of copied data.
* @param start_pos Position of the firs value in buffer.
*
* @return On success: Number of copied data.
* @return On error/success: 0.	No datas were copied => no data in buffer.
* @return On error: -ME_ERRNO_FIFO_BUFFER_OVERFLOW.
*/
inline int ao_write_data_wraparound(me6000_ao_subdevice_t *instance, int count,
				    int start_pos)
{				/// @note This is time critical function!
	uint32_t status;
	uint32_t value;
	int pos =
	    (instance->circ_buf.tail + start_pos) & instance->circ_buf.mask;
	int local_count = count;
	int i = 1;

	if (count <= 0) {	//Wrong count!
		return 0;
	}

	while (i < local_count) {
		//Get value from buffer
		value = *(instance->circ_buf.buf + pos);
		//Prepare it
		if (instance->ao_idx & 0x1) {
			value <<= 16;
		}
		//Put value to FIFO
		outl(value, instance->fifo_reg);
		//PDEBUG_REG("idx=%d fifo_reg outl(0x%lX+0x%lX)=0x%x\n", instance->ao_idx, instance->reg_base, instance->fifo_reg - instance->reg_base, value);

		pos++;
		pos &= instance->circ_buf.mask;
		if (pos == instance->circ_buf.head) {
			pos = instance->circ_buf.tail;
		}
		i++;
	}

	status = inl(instance->status_reg);
	if (!(status & ME6000_AO_STATUS_BIT_FF)) {	//FIFO is full before all datas were copied!
		PERROR("idx=%d FIFO is full before all datas were copied!\n",
		       instance->ao_idx);
		return -ME_ERRNO_FIFO_BUFFER_OVERFLOW;
	} else {		//Add last value
		value = *(instance->circ_buf.buf + pos);
		if (instance->ao_idx & 0x1) {
			value <<= 16;
		}
		//Put value to FIFO
		outl(value, instance->fifo_reg);
		//PDEBUG_REG("idx=%d fifo_reg outl(0x%lX+0x%lX)=0x%x\n", instance->ao_idx, instance->reg_base, instance->fifo_reg - instance->reg_base, value);
	}

	PINFO("idx=%d WRAPAROUND LOADED %d values\n", instance->ao_idx,
	      local_count);
	return local_count;
}

/** @brief Copy data from software buffer to fifo (fast).
* @note This is time critical function. Checking is done at begining and end only.
* @note The is not reasonable way to check how many walues was in FIFO at begining. The count must be managed externaly.
*
* @param instance The subdevice instance (pointer).
* @param count Maximum number of copied data.
* @param start_pos Position of the firs value in buffer.
*
* @return On success: Number of copied data.
* @return On error/success: 0.	No datas were copied => no data in buffer.
* @return On error: -ME_ERRNO_FIFO_BUFFER_OVERFLOW.
*/
inline int ao_write_data(me6000_ao_subdevice_t *instance, int count,
			 int start_pos)
{				/// @note This is time critical function!
	uint32_t status;
	uint32_t value;
	int pos =
	    (instance->circ_buf.tail + start_pos) & instance->circ_buf.mask;
	int local_count = count;
	int max_count;
	int i = 1;

	if (count <= 0) {	//Wrong count!
		return 0;
	}

	max_count = me_circ_buf_values(&instance->circ_buf) - start_pos;
	if (max_count <= 0) {	//No data to copy!
		return 0;
	}

	if (max_count < count) {
		local_count = max_count;
	}

	while (i < local_count) {
		//Get value from buffer
		value = *(instance->circ_buf.buf + pos);
		//Prepare it
		if (instance->ao_idx & 0x1) {
			value <<= 16;
		}
		//Put value to FIFO
		outl(value, instance->fifo_reg);
		//PDEBUG_REG("idx=%d fifo_reg outl(0x%lX+0x%lX)=0x%x\n", instance->ao_idx, instance->reg_base, instance->fifo_reg - instance->reg_base, value);

		pos++;
		pos &= instance->circ_buf.mask;
		i++;
	}

	status = inl(instance->status_reg);
	if (!(status & ME6000_AO_STATUS_BIT_FF)) {	//FIFO is full before all datas were copied!
		PERROR("idx=%d FIFO is full before all datas were copied!\n",
		       instance->ao_idx);
		return -ME_ERRNO_FIFO_BUFFER_OVERFLOW;
	} else {		//Add last value
		value = *(instance->circ_buf.buf + pos);
		if (instance->ao_idx & 0x1) {
			value <<= 16;
		}
		//Put value to FIFO
		outl(value, instance->fifo_reg);
		//PDEBUG_REG("idx=%d fifo_reg outl(0x%lX+0x%lX)=0x%x\n", instance->ao_idx, instance->reg_base, instance->fifo_reg - instance->reg_base, value);
	}

	PINFO("idx=%d FAST LOADED %d values\n", instance->ao_idx, local_count);
	return local_count;
}

/** @brief Copy data from software buffer to fifo (slow).
* @note This is slow function that copy all data from buffer to FIFO with full control.
*
* @param instance The subdevice instance (pointer).
* @param count Maximum number of copied data.
* @param start_pos Position of the firs value in buffer.
*
* @return On success: Number of copied values.
* @return On error/success: 0.	FIFO was full at begining.
* @return On error: -ME_ERRNO_RING_BUFFER_UNDEFFLOW.
*/
inline int ao_write_data_pooling(me6000_ao_subdevice_t *instance, int count,
				 int start_pos)
{				/// @note This is slow function!
	uint32_t status;
	uint32_t value;
	int pos =
	    (instance->circ_buf.tail + start_pos) & instance->circ_buf.mask;
	int local_count = count;
	int i;
	int max_count;

	if (count <= 0) {	//Wrong count!
		PERROR("idx=%d SLOW LOADED: Wrong count!\n", instance->ao_idx);
		return 0;
	}

	max_count = me_circ_buf_values(&instance->circ_buf) - start_pos;
	if (max_count <= 0) {	//No data to copy!
		PERROR("idx=%d SLOW LOADED: No data to copy!\n",
		       instance->ao_idx);
		return 0;
	}

	if (max_count < count) {
		local_count = max_count;
	}

	for (i = 0; i < local_count; i++) {
		status = inl(instance->status_reg);
		if (!(status & ME6000_AO_STATUS_BIT_FF)) {	//FIFO is full!
			return i;
		}
		//Get value from buffer
		value = *(instance->circ_buf.buf + pos);
		//Prepare it
		if (instance->ao_idx & 0x1) {
			value <<= 16;
		}
		//Put value to FIFO
		outl(value, instance->fifo_reg);
		//PDEBUG_REG("idx=%d fifo_reg outl(0x%lX+0x%lX)=0x%x\n", instance->ao_idx, instance->reg_base, instance->fifo_reg - instance->reg_base, value);

		pos++;
		pos &= instance->circ_buf.mask;
	}

	PINFO("idx=%d SLOW LOADED %d values\n", instance->ao_idx, local_count);
	return local_count;
}

/** @brief Copy data from user space to circular buffer.
* @param instance The subdevice instance (pointer).
* @param count Number of datas in user space.
* @param user_values Buffer's pointer.
*
* @return On success: Number of copied values.
* @return On error: -ME_ERRNO_INTERNAL.
*/
inline int ao_get_data_from_user(me6000_ao_subdevice_t *instance, int count,
				 int *user_values)
{
	int i, err;
	int empty_space;
	int copied;
	int value;

	empty_space = me_circ_buf_space(&instance->circ_buf);
	//We have only this space free.
	copied = (count < empty_space) ? count : empty_space;
	for (i = 0; i < copied; i++) {	//Copy from user to buffer
		if ((err = get_user(value, (int *)(user_values + i)))) {
			PERROR
			    ("idx=%d BUFFER LOADED: get_user(0x%p) return an error: %d\n",
			     instance->ao_idx, user_values + i, err);
			return -ME_ERRNO_INTERNAL;
		}
		/// @note The analog output in me6000 series has size of 16 bits.
		*(instance->circ_buf.buf + instance->circ_buf.head) =
		    (uint16_t) value;
		instance->circ_buf.head++;
		instance->circ_buf.head &= instance->circ_buf.mask;
	}

	PINFO("idx=%d BUFFER LOADED %d values\n", instance->ao_idx, copied);
	return copied;
}

static void me6000_ao_work_control_task(struct work_struct *work)
{
	me6000_ao_subdevice_t *instance;
	unsigned long cpu_flags = 0;
	uint32_t status;
	uint32_t ctrl;
	uint32_t synch;
	int reschedule = 0;
	int signaling = 0;
	uint32_t single_mask;

	instance =
	    container_of((void *)work, me6000_ao_subdevice_t, ao_control_task);
	PINFO("<%s: %ld> executed. idx=%d\n", __func__, jiffies,
	      instance->ao_idx);

	status = inl(instance->status_reg);
	PDEBUG_REG("status_reg inl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->status_reg - instance->reg_base, status);

/// @note AO_STATUS_BIT_FSM doesn't work as should be for pure single channels (idx>=4)
//      single_mask = (instance->ao_idx-ME6000_AO_SINGLE_STATUS_OFFSET < 0) ? 0x0000 : (0x0001 << (instance->ao_idx-ME6000_AO_SINGLE_STATUS_OFFSET));
	single_mask = *instance->triggering_flags & (0x1 << instance->ao_idx);

	switch (instance->status) {	// Checking actual mode.

		// Not configured for work.
	case ao_status_none:
		break;

		//This are stable modes. No need to do anything. (?)
	case ao_status_single_configured:
	case ao_status_stream_configured:
	case ao_status_stream_fifo_error:
	case ao_status_stream_buffer_error:
	case ao_status_stream_error:
		PERROR("Shouldn't be running!.\n");
		break;

		// Single modes
	case ao_status_single_run_wait:
	case ao_status_single_run:
	case ao_status_single_end_wait:
		if (instance->fifo) {	// Extra registers.
			if (!(status & ME6000_AO_STATUS_BIT_FSM)) {	// State machine is not working.
				if (((instance->fifo & ME6000_AO_HAS_FIFO)
				     && (!(status & ME6000_AO_STATUS_BIT_EF)))
				    || (!(instance->fifo & ME6000_AO_HAS_FIFO))) {	// Single is in end state.
					PDEBUG
					    ("Single call has been complited.\n");

					// Set correct value for single_read();
					instance->single_value =
					    instance->single_value_in_fifo;

					// Set status as 'ao_status_single_end'
					instance->status = ao_status_single_end;

					spin_lock(instance->preload_reg_lock);
					if ((single_mask) && (*instance->preload_flags & (ME6000_AO_SYNC_HOLD << instance->ao_idx))) {	// This is one of synchronous start channels. Set all as triggered.
						*instance->triggering_flags =
						    0x00000000;
					} else {
						//Set this channel as triggered (none active).
						*instance->triggering_flags &=
						    ~(0x1 << instance->ao_idx);
					}
					spin_unlock(instance->preload_reg_lock);

					// Signal the end.
					signaling = 1;
					// Wait for stop ISM.
					reschedule = 1;

					break;
				}
			}
			// Check timeout.
			if ((instance->timeout.delay) && ((jiffies - instance->timeout.start_time) >= instance->timeout.delay)) {	// Timeout
				PDEBUG("Timeout reached.\n");
				// Stop all actions. No conditions! Block interrupts and trigger. Leave FIFO untouched!
				spin_lock_irqsave(&instance->subdevice_lock,
						  cpu_flags);
				ctrl = inl(instance->ctrl_reg);
				ctrl |=
				    ME6000_AO_CTRL_BIT_STOP |
				    ME6000_AO_CTRL_BIT_IMMEDIATE_STOP;
				ctrl &=
				    ~(ME6000_AO_CTRL_BIT_ENABLE_IRQ |
				      ME6000_AO_CTRL_BIT_ENABLE_EX_TRIG);
				ctrl &=
				    ~(ME6000_AO_CTRL_BIT_EX_TRIG_EDGE |
				      ME6000_AO_CTRL_BIT_EX_TRIG_EDGE_BOTH);
				//Disabling FIFO
				ctrl &= ~ME6000_AO_CTRL_BIT_ENABLE_FIFO;

				outl(ctrl, instance->ctrl_reg);
				PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
					   instance->reg_base,
					   instance->ctrl_reg -
					   instance->reg_base, ctrl);
				spin_unlock_irqrestore(&instance->
						       subdevice_lock,
						       cpu_flags);

				//Reset interrupt latch
				inl(instance->irq_reset_reg);

				spin_lock(instance->preload_reg_lock);
				//Remove from synchronous start. Block triggering from this output.
				synch = inl(instance->preload_reg);
				synch &=
				    ~((ME6000_AO_SYNC_HOLD |
				       ME6000_AO_SYNC_EXT_TRIG) << instance->
				      ao_idx);
				if (!(instance->fifo & ME6000_AO_HAS_FIFO)) {	// No FIFO - set to single safe mode
					synch |=
					    ME6000_AO_SYNC_HOLD << instance->
					    ao_idx;
				}
				outl(synch, instance->preload_reg);
				PDEBUG_REG
				    ("preload_reg outl(0x%lX+0x%lX)=0x%x\n",
				     instance->reg_base,
				     instance->preload_reg - instance->reg_base,
				     synch);
				//Set this channel as triggered (none active).
				*instance->triggering_flags &=
				    ~(0x1 << instance->ao_idx);
				spin_unlock(instance->preload_reg_lock);

				// Set correct value for single_read();
				instance->single_value_in_fifo =
				    instance->single_value;

				instance->status = ao_status_single_end;

				// Signal the end.
				signaling = 1;
			}
		} else {	// No extra registers.
/*
				if (!(status & single_mask))
				{// State machine is not working.
					PDEBUG("Single call has been complited.\n");

					// Set correct value for single_read();
					instance->single_value = instance->single_value_in_fifo;

					// Set status as 'ao_status_single_end'
					instance->status = ao_status_single_end;

					// Signal the end.
					signaling = 1;
					// Wait for stop ISM.
					reschedule = 1;

					break;
				}
*/
			if (!single_mask) {	// Was triggered.
				PDEBUG("Single call has been complited.\n");

				// Set correct value for single_read();
				instance->single_value =
				    instance->single_value_in_fifo;

				// Set status as 'ao_status_single_end'
				instance->status = ao_status_single_end;

				// Signal the end.
				signaling = 1;

				break;
			}
			// Check timeout.
			if ((instance->timeout.delay) && ((jiffies - instance->timeout.start_time) >= instance->timeout.delay)) {	// Timeout
				PDEBUG("Timeout reached.\n");

				spin_lock(instance->preload_reg_lock);
				//Remove from synchronous start. Block triggering from this output.
				synch = inl(instance->preload_reg);
				synch &=
				    ~(ME6000_AO_SYNC_EXT_TRIG << instance->
				      ao_idx);
				synch |=
				    ME6000_AO_SYNC_HOLD << instance->ao_idx;

				outl(synch, instance->preload_reg);
				PDEBUG_REG
				    ("preload_reg outl(0x%lX+0x%lX)=0x%x\n",
				     instance->reg_base,
				     instance->preload_reg - instance->reg_base,
				     synch);
				//Set this channel as triggered (none active).
				*instance->triggering_flags &=
				    ~(0x1 << instance->ao_idx);
				spin_unlock(instance->preload_reg_lock);

				// Restore old settings.
				PDEBUG("Write old value back to register.\n");
				outl(instance->single_value,
				     instance->single_reg);
				PDEBUG_REG
				    ("single_reg outl(0x%lX+0x%lX)=0x%x\n",
				     instance->reg_base,
				     instance->single_reg - instance->reg_base,
				     instance->single_value);

				// Set correct value for single_read();
				instance->single_value_in_fifo =
				    instance->single_value;

				instance->status = ao_status_single_end;

				// Signal the end.
				signaling = 1;
			}
		}

		// Wait for stop.
		reschedule = 1;
		break;

	case ao_status_stream_end:
		if (!(instance->fifo & ME6000_AO_HAS_FIFO)) {	// No FIFO
			PERROR_CRITICAL
			    ("Streaming on single device! This feature is not implemented in this version!\n");
			instance->status = ao_status_stream_error;
			// Signal the end.
			signaling = 1;
			break;
		}
	case ao_status_single_end:
		if (instance->fifo) {	// Extra registers.
			if (status & ME6000_AO_STATUS_BIT_FSM) {	// State machine is working but the status is set to end. Force stop.

				// Wait for stop.
				reschedule = 1;
			}

			spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);
			// Stop all actions. No conditions! Block interrupts and trigger. Leave FIFO untouched!
			ctrl = inl(instance->ctrl_reg);
			ctrl |=
			    ME6000_AO_CTRL_BIT_IMMEDIATE_STOP |
			    ME6000_AO_CTRL_BIT_STOP;
			ctrl &=
			    ~(ME6000_AO_CTRL_BIT_ENABLE_IRQ |
			      ME6000_AO_CTRL_BIT_ENABLE_EX_TRIG);
			outl(ctrl, instance->ctrl_reg);
			PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->ctrl_reg - instance->reg_base,
				   ctrl);
			spin_unlock_irqrestore(&instance->subdevice_lock,
					       cpu_flags);

			//Reset interrupt latch
			inl(instance->irq_reset_reg);
		} else {	// No extra registers.
/*
				if (status & single_mask)
				{// State machine is working but the status is set to end. Force stop.

					// Wait for stop.
					reschedule = 1;
				}
*/
		}
		break;

		// Stream modes
	case ao_status_stream_run_wait:
		if (!(instance->fifo & ME6000_AO_HAS_FIFO)) {	// No FIFO
			PERROR_CRITICAL
			    ("Streaming on single device! This feature is not implemented in this version!\n");
			instance->status = ao_status_stream_error;
			// Signal the end.
			signaling = 1;
			break;
		}

		if (status & ME6000_AO_STATUS_BIT_FSM) {	// State machine is working. Waiting for start finish.
			instance->status = ao_status_stream_run;

			// Signal end of this step
			signaling = 1;
		} else {	// State machine is not working.
			if (!(status & ME6000_AO_STATUS_BIT_EF)) {	// FIFO is empty. Procedure has started and finish already!
				instance->status = ao_status_stream_end;

				// Signal the end.
				signaling = 1;
				// Wait for stop.
				reschedule = 1;
				break;
			}
		}

		// Check timeout.
		if ((instance->timeout.delay) && ((jiffies - instance->timeout.start_time) >= instance->timeout.delay)) {	// Timeout
			PDEBUG("Timeout reached.\n");
			// Stop all actions. No conditions! Block interrupts. Leave FIFO untouched!
			spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);
			ctrl = inl(instance->ctrl_reg);
			ctrl |=
			    ME6000_AO_CTRL_BIT_STOP |
			    ME6000_AO_CTRL_BIT_IMMEDIATE_STOP;
			ctrl &=
			    ~(ME6000_AO_CTRL_BIT_ENABLE_IRQ |
			      ME6000_AO_CTRL_BIT_ENABLE_EX_TRIG);
			outl(ctrl, instance->ctrl_reg);
			PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->ctrl_reg - instance->reg_base,
				   ctrl);
			spin_unlock_irqrestore(&instance->subdevice_lock,
					       cpu_flags);

			//Reset interrupt latch
			inl(instance->irq_reset_reg);

			spin_lock(instance->preload_reg_lock);
			//Remove from synchronous start. Block triggering from this output.
			synch = inl(instance->preload_reg);
			synch &=
			    ~((ME6000_AO_SYNC_HOLD | ME6000_AO_SYNC_EXT_TRIG) <<
			      instance->ao_idx);
			outl(synch, instance->preload_reg);
			PDEBUG_REG("preload_reg outl(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->preload_reg - instance->reg_base,
				   synch);
			spin_unlock(instance->preload_reg_lock);

			instance->status = ao_status_stream_end;

			// Signal the end.
			signaling = 1;
		}
		// Wait for stop.
		reschedule = 1;
		break;

	case ao_status_stream_run:
		if (!(instance->fifo & ME6000_AO_HAS_FIFO)) {	// No FIFO
			PERROR_CRITICAL
			    ("Streaming on single device! This feature is not implemented in this version!\n");
			instance->status = ao_status_stream_error;
			// Signal the end.
			signaling = 1;
			break;
		}

		if (!(status & ME6000_AO_STATUS_BIT_FSM)) {	// State machine is not working. This is an error.
			// BROKEN PIPE!
			if (!(status & ME6000_AO_STATUS_BIT_EF)) {	// FIFO is empty.
				if (me_circ_buf_values(&instance->circ_buf)) {	// Software buffer is not empty.
					if (instance->stop_data_count && (instance->stop_data_count <= instance->data_count)) {	//Finishing work. Requed data shown.
						PDEBUG
						    ("ISM stoped. No data in FIFO. Buffer is not empty.\n");
						instance->status =
						    ao_status_stream_end;
					} else {
						PERROR
						    ("Output stream has been broken. ISM stoped. No data in FIFO. Buffer is not empty.\n");
						instance->status =
						    ao_status_stream_buffer_error;
					}
				} else {	// Software buffer is empty.
					PDEBUG
					    ("ISM stoped. No data in FIFO. Buffer is empty.\n");
					instance->status = ao_status_stream_end;
				}
			} else {	// There are still datas in FIFO.
				if (me_circ_buf_values(&instance->circ_buf)) {	// Software buffer is not empty.
					PERROR
					    ("Output stream has been broken. ISM stoped but some data in FIFO and buffer.\n");
				} else {	// Software buffer is empty.
					PERROR
					    ("Output stream has been broken. ISM stoped but some data in FIFO. Buffer is empty.\n");
				}
				instance->status = ao_status_stream_fifo_error;

			}

			// Signal the failure.
			signaling = 1;
			break;
		}
		// Wait for stop.
		reschedule = 1;
		break;

	case ao_status_stream_end_wait:
		if (!(instance->fifo & ME6000_AO_HAS_FIFO)) {	// No FIFO
			PERROR_CRITICAL
			    ("Streaming on single device! This feature is not implemented in this version!\n");
			instance->status = ao_status_stream_error;
			// Signal the end.
			signaling = 1;
			break;
		}

		if (!(status & ME6000_AO_STATUS_BIT_FSM)) {	// State machine is not working. Waiting for stop finish.
			instance->status = ao_status_stream_end;
			signaling = 1;
		}
		// State machine is working.
		reschedule = 1;
		break;

	default:
		PERROR_CRITICAL("Status is in wrong state (%d)!\n",
				instance->status);
		instance->status = ao_status_stream_error;
		// Signal the end.
		signaling = 1;
		break;

	}

	if (signaling) {	//Signal it.
		wake_up_interruptible_all(&instance->wait_queue);
	}

	if (instance->ao_control_task_flag && reschedule) {	// Reschedule task
		queue_delayed_work(instance->me6000_workqueue,
				   &instance->ao_control_task, 1);
	} else {
		PINFO("<%s> Ending control task.\n", __func__);
	}

}

static int me6000_ao_query_range_by_min_max(me_subdevice_t *subdevice,
					    int unit,
					    int *min,
					    int *max, int *maxdata, int *range)
{
	me6000_ao_subdevice_t *instance;

	instance = (me6000_ao_subdevice_t *) subdevice;

	PDEBUG("executed. idx=%d\n", instance->ao_idx);

	if ((*max - *min) < 0) {
		PERROR("Invalid minimum and maximum values specified.\n");
		return ME_ERRNO_INVALID_MIN_MAX;
	}

	if ((unit == ME_UNIT_VOLT) || (unit == ME_UNIT_ANY)) {
		if ((*max <= (instance->max + 1000)) && (*min >= instance->min)) {
			*min = instance->min;
			*max = instance->max;
			*maxdata = ME6000_AO_MAX_DATA;
			*range = 0;
		} else {
			PERROR("No matching range available.\n");
			return ME_ERRNO_NO_RANGE;
		}
	} else {
		PERROR("Invalid physical unit specified.\n");
		return ME_ERRNO_INVALID_UNIT;
	}

	return ME_ERRNO_SUCCESS;
}

static int me6000_ao_query_number_ranges(me_subdevice_t *subdevice,
					 int unit, int *count)
{
	me6000_ao_subdevice_t *instance;

	instance = (me6000_ao_subdevice_t *) subdevice;

	PDEBUG("executed. idx=%d\n", instance->ao_idx);

	if ((unit == ME_UNIT_VOLT) || (unit == ME_UNIT_ANY)) {
		*count = 1;
	} else {
		*count = 0;
	}

	return ME_ERRNO_SUCCESS;
}

static int me6000_ao_query_range_info(me_subdevice_t *subdevice,
				      int range,
				      int *unit,
				      int *min, int *max, int *maxdata)
{
	me6000_ao_subdevice_t *instance;

	instance = (me6000_ao_subdevice_t *) subdevice;

	PDEBUG("executed. idx=%d\n", instance->ao_idx);

	if (range == 0) {
		*unit = ME_UNIT_VOLT;
		*min = instance->min;
		*max = instance->max;
		*maxdata = ME6000_AO_MAX_DATA;
	} else {
		PERROR("Invalid range number specified.\n");
		return ME_ERRNO_INVALID_RANGE;
	}

	return ME_ERRNO_SUCCESS;
}

static int me6000_ao_query_timer(me_subdevice_t *subdevice,
				 int timer,
				 int *base_frequency,
				 long long *min_ticks, long long *max_ticks)
{
	me6000_ao_subdevice_t *instance;

	instance = (me6000_ao_subdevice_t *) subdevice;

	PDEBUG("executed. idx=%d\n", instance->ao_idx);

	if (instance->fifo) {	//Streaming device.
		*base_frequency = ME6000_AO_BASE_FREQUENCY;
		if (timer == ME_TIMER_ACQ_START) {
			*min_ticks = ME6000_AO_MIN_ACQ_TICKS;
			*max_ticks = ME6000_AO_MAX_ACQ_TICKS;
		} else if (timer == ME_TIMER_CONV_START) {
			*min_ticks = ME6000_AO_MIN_CHAN_TICKS;
			*max_ticks = ME6000_AO_MAX_CHAN_TICKS;
		}
	} else {		//Not streaming device!
		*base_frequency = 0;
		*min_ticks = 0;
		*max_ticks = 0;
	}

	return ME_ERRNO_SUCCESS;
}

static int me6000_ao_query_number_channels(me_subdevice_t *subdevice,
					   int *number)
{
	me6000_ao_subdevice_t *instance;
	instance = (me6000_ao_subdevice_t *) subdevice;

	PDEBUG("executed. idx=%d\n", instance->ao_idx);

	*number = 1;
	return ME_ERRNO_SUCCESS;
}

static int me6000_ao_query_subdevice_type(me_subdevice_t *subdevice,
					  int *type, int *subtype)
{
	me6000_ao_subdevice_t *instance;

	instance = (me6000_ao_subdevice_t *) subdevice;

	PDEBUG("executed. idx=%d\n", instance->ao_idx);

	*type = ME_TYPE_AO;
	*subtype =
	    (instance->
	     fifo & ME6000_AO_HAS_FIFO) ? ME_SUBTYPE_STREAMING :
	    ME_SUBTYPE_SINGLE;

	return ME_ERRNO_SUCCESS;
}

static int me6000_ao_query_subdevice_caps(me_subdevice_t *subdevice, int *caps)
{
	me6000_ao_subdevice_t *instance;
	instance = (me6000_ao_subdevice_t *) subdevice;

	PDEBUG("executed. idx=%d\n", instance->ao_idx);

	*caps =
	    ME_CAPS_AO_TRIG_SYNCHRONOUS | ((instance->fifo) ? ME_CAPS_AO_FIFO :
					   ME_CAPS_NONE);

	return ME_ERRNO_SUCCESS;
}

static int me6000_ao_query_subdevice_caps_args(struct me_subdevice *subdevice,
					       int cap, int *args, int count)
{
	me6000_ao_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;

	instance = (me6000_ao_subdevice_t *) subdevice;

	PDEBUG("executed. idx=%d\n", instance->ao_idx);

	if (count != 1) {
		PERROR("Invalid capability argument count.\n");
		return ME_ERRNO_INVALID_CAP_ARG_COUNT;
	}

	switch (cap) {
	case ME_CAP_AI_FIFO_SIZE:
		args[0] = (instance->fifo) ? ME6000_AO_FIFO_COUNT : 0;
		break;

	case ME_CAP_AI_BUFFER_SIZE:
		args[0] =
		    (instance->circ_buf.buf) ? ME6000_AO_CIRC_BUF_COUNT : 0;
		break;

	default:
		PERROR("Invalid capability.\n");
		err = ME_ERRNO_INVALID_CAP;
		args[0] = 0;
	}

	return err;
}
