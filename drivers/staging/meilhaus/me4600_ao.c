/**
 * @file me4600_ao.c
 *
 * @brief ME-4000 analog output subdevice instance.
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

///Common part. (For normal and Bosch builds.)

/* Includes
 */

#include <linux/module.h>

#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/types.h>
#include <linux/version.h>
#include <linux/interrupt.h>
#include <linux/delay.h>

#include "medefines.h"
#include "meinternal.h"
#include "meerror.h"

#include "medebug.h"
#include "meids.h"
#include "me4600_reg.h"
#include "me4600_ao_reg.h"
#include "me4600_ao.h"

/* Defines
 */

static int me4600_ao_query_range_by_min_max(me_subdevice_t *subdevice,
					    int unit,
					    int *min,
					    int *max, int *maxdata, int *range);

static int me4600_ao_query_number_ranges(me_subdevice_t *subdevice,
					 int unit, int *count);

static int me4600_ao_query_range_info(me_subdevice_t *subdevice,
				      int range,
				      int *unit,
				      int *min, int *max, int *maxdata);

static int me4600_ao_query_timer(me_subdevice_t *subdevice,
				 int timer,
				 int *base_frequency,
				 long long *min_ticks, long long *max_ticks);

static int me4600_ao_query_number_channels(me_subdevice_t *subdevice,
					   int *number);

static int me4600_ao_query_subdevice_type(me_subdevice_t *subdevice,
					  int *type, int *subtype);

static int me4600_ao_query_subdevice_caps(me_subdevice_t *subdevice,
					  int *caps);

static int me4600_ao_query_subdevice_caps_args(struct me_subdevice *subdevice,
					       int cap, int *args, int count);

#ifndef BOSCH
/// @note NORMAL BUILD
/// @author Krzysztof Gantzke   (k.gantzke@meilhaus.de)
/* Includes
 */

# include <linux/workqueue.h>

/* Defines
 */

/** Remove subdevice.
*/
static void me4600_ao_destructor(struct me_subdevice *subdevice);

/** Reset subdevice. Stop all actions. Reset registry. Disable FIFO. Set output to 0V and status to 'none'.
*/
static int me4600_ao_io_reset_subdevice(me_subdevice_t *subdevice,
					struct file *filep, int flags);

/** Set output as single
*/
static int me4600_ao_io_single_config(me_subdevice_t *subdevice,
				      struct file *filep,
				      int channel,
				      int single_config,
				      int ref,
				      int trig_chan,
				      int trig_type, int trig_edge, int flags);

/** Pass to user actual value of output.
*/
static int me4600_ao_io_single_read(me_subdevice_t *subdevice,
				    struct file *filep,
				    int channel,
				    int *value, int time_out, int flags);

/** Write to output requed value.
*/
static int me4600_ao_io_single_write(me_subdevice_t *subdevice,
				     struct file *filep,
				     int channel,
				     int value, int time_out, int flags);

/** Set output as streamed device.
*/
static int me4600_ao_io_stream_config(me_subdevice_t *subdevice,
				      struct file *filep,
				      meIOStreamConfig_t *config_list,
				      int count,
				      meIOStreamTrigger_t *trigger,
				      int fifo_irq_threshold, int flags);

/** Wait for / Check empty space in buffer.
*/
static int me4600_ao_io_stream_new_values(me_subdevice_t *subdevice,
					  struct file *filep,
					  int time_out, int *count, int flags);

/** Start streaming.
*/
static int me4600_ao_io_stream_start(me_subdevice_t *subdevice,
				     struct file *filep,
				     int start_mode, int time_out, int flags);

/** Check actual state. / Wait for end.
*/
static int me4600_ao_io_stream_status(me_subdevice_t *subdevice,
				      struct file *filep,
				      int wait,
				      int *status, int *values, int flags);

/** Stop streaming.
*/
static int me4600_ao_io_stream_stop(me_subdevice_t *subdevice,
				    struct file *filep,
				    int stop_mode, int flags);

/** Write datas to buffor.
*/
static int me4600_ao_io_stream_write(me_subdevice_t *subdevice,
				     struct file *filep,
				     int write_mode,
				     int *values, int *count, int flags);

/** Interrupt handler. Copy from buffer to FIFO.
*/
static irqreturn_t me4600_ao_isr(int irq, void *dev_id);
/** Copy data from circular buffer to fifo (fast) in wraparound mode.
*/
inline int ao_write_data_wraparound(me4600_ao_subdevice_t *instance, int count,
				    int start_pos);

/** Copy data from circular buffer to fifo (fast).
*/
inline int ao_write_data(me4600_ao_subdevice_t *instance, int count,
			 int start_pos);

/** Copy data from circular buffer to fifo (slow).
*/
inline int ao_write_data_pooling(me4600_ao_subdevice_t *instance, int count,
				 int start_pos);

/** Copy data from user space to circular buffer.
*/
inline int ao_get_data_from_user(me4600_ao_subdevice_t *instance, int count,
				 int *user_values);

/** Stop presentation. Preserve FIFOs.
*/
inline int ao_stop_immediately(me4600_ao_subdevice_t *instance);

/** Task for asynchronical state verifying.
*/
static void me4600_ao_work_control_task(struct work_struct *work);
/* Functions
 */

static int me4600_ao_io_reset_subdevice(me_subdevice_t *subdevice,
					struct file *filep, int flags)
{
	me4600_ao_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	uint32_t tmp;

	instance = (me4600_ao_subdevice_t *) subdevice;

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
	    ~((ME4600_AO_SYNC_HOLD | ME4600_AO_SYNC_EXT_TRIG) << instance->
	      ao_idx);
	outl(tmp, instance->preload_reg);
	PDEBUG_REG("preload_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->preload_reg - instance->reg_base, tmp);
	*instance->preload_flags &=
	    ~((ME4600_AO_SYNC_HOLD | ME4600_AO_SYNC_EXT_TRIG) << instance->
	      ao_idx);
	spin_unlock(instance->preload_reg_lock);

	//Set single mode, dissable FIFO, dissable external trigger, set output to analog, block interrupt.
	outl(ME4600_AO_MODE_SINGLE | ME4600_AO_CTRL_BIT_STOP |
	     ME4600_AO_CTRL_BIT_IMMEDIATE_STOP | ME4600_AO_CTRL_BIT_RESET_IRQ,
	     instance->ctrl_reg);
	PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->ctrl_reg - instance->reg_base,
		   ME4600_AO_MODE_SINGLE | ME4600_AO_CTRL_BIT_STOP |
		   ME4600_AO_CTRL_BIT_IMMEDIATE_STOP |
		   ME4600_AO_CTRL_BIT_RESET_IRQ);

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

static int me4600_ao_io_single_config(me_subdevice_t *subdevice,
				      struct file *filep,
				      int channel,
				      int single_config,
				      int ref,
				      int trig_chan,
				      int trig_type, int trig_edge, int flags)
{
	me4600_ao_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	uint32_t ctrl;
	uint32_t sync;
	unsigned long cpu_flags;

	instance = (me4600_ao_subdevice_t *) subdevice;

	PDEBUG("executed. idx=%d\n", instance->ao_idx);

	// Checking parameters
	if (flags) {
		PERROR
		    ("Invalid flag specified. Must be ME_IO_SINGLE_CONFIG_NO_FLAGS.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	switch (trig_type) {
	case ME_TRIG_TYPE_SW:
		if (trig_edge != ME_TRIG_EDGE_NONE) {
			PERROR
			    ("Invalid trigger edge. Software trigger has not edge.\n");
			return ME_ERRNO_INVALID_TRIG_EDGE;
		}
		break;

	case ME_TRIG_TYPE_EXT_DIGITAL:
		switch (trig_edge) {
		case ME_TRIG_EDGE_ANY:
		case ME_TRIG_EDGE_RISING:
		case ME_TRIG_EDGE_FALLING:
			break;

		default:
			PERROR("Invalid trigger edge.\n");
			return ME_ERRNO_INVALID_TRIG_EDGE;
		}
		break;

	default:
		PERROR
		    ("Invalid trigger type. Trigger must be software or digital.\n");
		return ME_ERRNO_INVALID_TRIG_TYPE;
	}

	if ((trig_chan != ME_TRIG_CHAN_DEFAULT)
	    && (trig_chan != ME_TRIG_CHAN_SYNCHRONOUS)) {
		PERROR("Invalid trigger channel specified.\n");
		return ME_ERRNO_INVALID_TRIG_CHAN;
	}

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
	// Set control register.
	spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);
	// Set stop bit. Stop streaming mode.
	ctrl = inl(instance->ctrl_reg);
	//Reset all bits.
	ctrl = ME4600_AO_CTRL_BIT_IMMEDIATE_STOP | ME4600_AO_CTRL_BIT_STOP;

	if (trig_type == ME_TRIG_TYPE_EXT_DIGITAL) {
		PINFO("External digital trigger.\n");

		if (trig_edge == ME_TRIG_EDGE_ANY) {
//                              ctrl |= ME4600_AO_CTRL_BIT_EX_TRIG_EDGE | ME4600_AO_CTRL_BIT_EX_TRIG_EDGE_BOTH;
			instance->ctrl_trg =
			    ME4600_AO_CTRL_BIT_EX_TRIG_EDGE |
			    ME4600_AO_CTRL_BIT_EX_TRIG_EDGE_BOTH;
		} else if (trig_edge == ME_TRIG_EDGE_FALLING) {
//                              ctrl |= ME4600_AO_CTRL_BIT_EX_TRIG_EDGE;
			instance->ctrl_trg = ME4600_AO_CTRL_BIT_EX_TRIG_EDGE;
		} else if (trig_edge == ME_TRIG_EDGE_RISING) {
			instance->ctrl_trg = 0x0;
		}
	} else if (trig_type == ME_TRIG_TYPE_SW) {
		PDEBUG("Software trigger\n");
		instance->ctrl_trg = 0x0;
	}

	outl(ctrl, instance->ctrl_reg);
	PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->ctrl_reg - instance->reg_base, ctrl);
	spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);

	// Set preload/synchronization register.
	spin_lock(instance->preload_reg_lock);
	if (trig_type == ME_TRIG_TYPE_SW) {
		*instance->preload_flags &=
		    ~(ME4600_AO_SYNC_EXT_TRIG << instance->ao_idx);
	} else			//if (trig_type == ME_TRIG_TYPE_EXT_DIGITAL)
	{
		*instance->preload_flags |=
		    ME4600_AO_SYNC_EXT_TRIG << instance->ao_idx;
	}

	if (trig_chan == ME_TRIG_CHAN_DEFAULT) {
		*instance->preload_flags &=
		    ~(ME4600_AO_SYNC_HOLD << instance->ao_idx);
	} else			//if (trig_chan == ME_TRIG_CHAN_SYNCHRONOUS)
	{
		*instance->preload_flags |=
		    ME4600_AO_SYNC_HOLD << instance->ao_idx;
	}

	//Reset hardware register
	sync = inl(instance->preload_reg);
	PDEBUG_REG("preload_reg inl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->preload_reg - instance->reg_base, sync);
	sync &= ~(ME4600_AO_SYNC_EXT_TRIG << instance->ao_idx);
	sync |= ME4600_AO_SYNC_HOLD << instance->ao_idx;

	//Output configured in default (safe) mode.
	outl(sync, instance->preload_reg);
	PDEBUG_REG("preload_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->preload_reg - instance->reg_base, sync);
	spin_unlock(instance->preload_reg_lock);

	instance->status = ao_status_single_configured;

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me4600_ao_io_single_read(me_subdevice_t *subdevice,
				    struct file *filep,
				    int channel,
				    int *value, int time_out, int flags)
{
	me4600_ao_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;

	unsigned long j;
	unsigned long delay = 0;

	instance = (me4600_ao_subdevice_t *) subdevice;

	PDEBUG("executed. idx=%d\n", instance->ao_idx);

	if (flags & ~ME_IO_SINGLE_NONBLOCKING) {
		PERROR("Invalid flag specified. %d\n", flags);
		return ME_ERRNO_INVALID_FLAGS;
	}

	if (time_out < 0) {
		PERROR("Invalid timeout specified.\n");
		return ME_ERRNO_INVALID_TIMEOUT;
	}

	if (channel != 0) {
		PERROR("Invalid channel number specified.\n");
		return ME_ERRNO_INVALID_CHANNEL;
	}

	if ((instance->status >= ao_status_stream_configured)
	    && (instance->status <= ao_status_stream_end)) {
		PERROR("Subdevice not configured to work in single mode!\n");
		return ME_ERRNO_PREVIOUS_CONFIG;
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
						 (delay) ? delay +
						 1 : LONG_MAX);

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

static int me4600_ao_io_single_write(me_subdevice_t *subdevice,
				     struct file *filep,
				     int channel,
				     int value, int time_out, int flags)
{
	me4600_ao_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	unsigned long cpu_flags;
	unsigned long j;
	unsigned long delay = 0x0;

	//Registry handling variables.
	uint32_t sync_mask;
	uint32_t mode;
	uint32_t tmp;
	uint32_t ctrl;
	uint32_t status;

	instance = (me4600_ao_subdevice_t *) subdevice;

	PDEBUG("executed. idx=%d\n", instance->ao_idx);

	if (flags &
	    ~(ME_IO_SINGLE_TYPE_TRIG_SYNCHRONOUS |
	      ME_IO_SINGLE_TYPE_WRITE_NONBLOCKING)) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	if (time_out < 0) {
		PERROR("Invalid timeout specified.\n");
		return ME_ERRNO_INVALID_TIMEOUT;
	}

	if (value & ~ME4600_AO_MAX_DATA) {
		PERROR("Invalid value provided.\n");
		return ME_ERRNO_VALUE_OUT_OF_RANGE;
	}

	if (channel != 0) {
		PERROR("Invalid channel number specified.\n");
		return ME_ERRNO_INVALID_CHANNEL;
	}

	if ((instance->status == ao_status_none)
	    || (instance->status > ao_status_single_end)) {
		PERROR("Subdevice not configured to work in single mode!\n");
		return ME_ERRNO_PREVIOUS_CONFIG;
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

	ctrl = inl(instance->ctrl_reg);

	if (!instance->fifo) {	//No FIFO
		//Set the single mode.
		ctrl &= ~ME4600_AO_CTRL_MODE_MASK;

		//Write value
		PDEBUG("Write value\n");
		outl(value, instance->single_reg);
		PDEBUG_REG("single_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->single_reg - instance->reg_base, value);
	} else {		// mix-mode
		//Set speed
		outl(ME4600_AO_MIN_CHAN_TICKS - 1, instance->timer_reg);
		PDEBUG_REG("timer_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->timer_reg - instance->reg_base,
			   (int)ME4600_AO_MIN_CHAN_TICKS);
		instance->hardware_stop_delay = HZ / 10;	//100ms

		status = inl(instance->status_reg);

		//Set the continous mode.
		ctrl &= ~ME4600_AO_CTRL_MODE_MASK;
		ctrl |= ME4600_AO_MODE_CONTINUOUS;

		//Prepare FIFO
		if (!(ctrl & ME4600_AO_CTRL_BIT_ENABLE_FIFO)) {	//FIFO wasn't enabeled. Do it.
			PINFO("Enableing FIFO.\n");
			ctrl &= ~ME4600_AO_CTRL_BIT_ENABLE_IRQ;
			ctrl |=
			    ME4600_AO_CTRL_BIT_ENABLE_FIFO |
			    ME4600_AO_CTRL_BIT_RESET_IRQ;
		} else {	//Check if FIFO is empty
			if (status & ME4600_AO_STATUS_BIT_EF) {	//FIFO not empty
				PINFO("Reseting FIFO.\n");
				ctrl &=
				    ~(ME4600_AO_CTRL_BIT_ENABLE_FIFO |
				      ME4600_AO_CTRL_BIT_ENABLE_IRQ);
				ctrl |= ME4600_AO_CTRL_BIT_RESET_IRQ;
				outl(ctrl, instance->ctrl_reg);
				PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
					   instance->reg_base,
					   instance->ctrl_reg -
					   instance->reg_base, ctrl);

				ctrl |=
				    ME4600_AO_CTRL_BIT_ENABLE_FIFO |
				    ME4600_AO_CTRL_BIT_RESET_IRQ;
			} else {	//FIFO empty, only interrupt needs to be disabled!
				ctrl &= ~ME4600_AO_CTRL_BIT_ENABLE_IRQ;
				ctrl |= ME4600_AO_CTRL_BIT_RESET_IRQ;
			}
		}

		outl(ctrl, instance->ctrl_reg);
		PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->ctrl_reg - instance->reg_base, ctrl);

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
	}

	mode = *instance->preload_flags >> instance->ao_idx;
	mode &= (ME4600_AO_SYNC_HOLD | ME4600_AO_SYNC_EXT_TRIG);

	PINFO("Triggering mode: 0x%x\n", mode);

	spin_lock(instance->preload_reg_lock);
	sync_mask = inl(instance->preload_reg);
	PDEBUG_REG("preload_reg inl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->preload_reg - instance->reg_base, sync_mask);
	switch (mode) {
	case 0:		//Individual software
		ctrl &= ~ME4600_AO_CTRL_BIT_ENABLE_EX_TRIG;

		if (!instance->fifo) {	// No FIFO - In this case resetting 'ME4600_AO_SYNC_HOLD' will trigger output.
			if ((sync_mask & ((ME4600_AO_SYNC_HOLD | ME4600_AO_SYNC_EXT_TRIG) << instance->ao_idx)) != ME4600_AO_SYNC_HOLD) {	//Now we can set correct mode. This is exception. It is set to synchronous and triggered later.
				sync_mask &=
				    ~(ME4600_AO_SYNC_EXT_TRIG << instance->
				      ao_idx);
				sync_mask |=
				    ME4600_AO_SYNC_HOLD << instance->ao_idx;

				outl(sync_mask, instance->preload_reg);
				PDEBUG_REG
				    ("preload_reg outl(0x%lX+0x%lX)=0x%x\n",
				     instance->reg_base,
				     instance->preload_reg - instance->reg_base,
				     sync_mask);
			}
		} else {	// FIFO
			if ((sync_mask & ((ME4600_AO_SYNC_HOLD | ME4600_AO_SYNC_EXT_TRIG) << instance->ao_idx)) != 0x0) {	//Now we can set correct mode.
				sync_mask &=
				    ~((ME4600_AO_SYNC_EXT_TRIG |
				       ME4600_AO_SYNC_HOLD) << instance->
				      ao_idx);

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

	case ME4600_AO_SYNC_EXT_TRIG:	//Individual hardware
		ctrl |= ME4600_AO_CTRL_BIT_ENABLE_EX_TRIG;

		if (!instance->fifo) {	// No FIFO - In this case resetting 'ME4600_AO_SYNC_HOLD' will trigger output.
			if ((sync_mask & ((ME4600_AO_SYNC_HOLD | ME4600_AO_SYNC_EXT_TRIG) << instance->ao_idx)) != ME4600_AO_SYNC_HOLD) {	//Now we can set correct mode
				sync_mask &=
				    ~(ME4600_AO_SYNC_EXT_TRIG << instance->
				      ao_idx);
				sync_mask |=
				    ME4600_AO_SYNC_HOLD << instance->ao_idx;

				outl(sync_mask, instance->preload_reg);
				PDEBUG_REG
				    ("preload_reg outl(0x%lX+0x%lX)=0x%x\n",
				     instance->reg_base,
				     instance->preload_reg - instance->reg_base,
				     sync_mask);
			}
		} else {	// FIFO
			if ((sync_mask & ((ME4600_AO_SYNC_HOLD | ME4600_AO_SYNC_EXT_TRIG) << instance->ao_idx)) != 0x0) {	//Now we can set correct mode.
				sync_mask &=
				    ~((ME4600_AO_SYNC_EXT_TRIG |
				       ME4600_AO_SYNC_HOLD) << instance->
				      ao_idx);

				outl(sync_mask, instance->preload_reg);
				PDEBUG_REG
				    ("preload_reg outl(0x%lX+0x%lX)=0x%x\n",
				     instance->reg_base,
				     instance->preload_reg - instance->reg_base,
				     sync_mask);
			}
		}
		break;

	case ME4600_AO_SYNC_HOLD:	//Synchronous software
		ctrl &= ~ME4600_AO_CTRL_BIT_ENABLE_EX_TRIG;

//                                      if((sync_mask & ((ME4600_AO_SYNC_HOLD | ME4600_AO_SYNC_EXT_TRIG) << instance->ao_idx)) != ME4600_AO_SYNC_HOLD)
		if ((sync_mask & ((ME4600_AO_SYNC_HOLD | ME4600_AO_SYNC_EXT_TRIG) << instance->ao_idx)) != (ME4600_AO_SYNC_HOLD | ME4600_AO_SYNC_EXT_TRIG)) {	//Now we can set correct mode
			sync_mask |=
			    ME4600_AO_SYNC_EXT_TRIG << instance->ao_idx;
//                                              sync_mask &= ~(ME4600_AO_SYNC_EXT_TRIG << instance->ao_idx);
			sync_mask |= ME4600_AO_SYNC_HOLD << instance->ao_idx;

			outl(sync_mask, instance->preload_reg);
			PDEBUG_REG("preload_reg outl(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->preload_reg - instance->reg_base,
				   sync_mask);
		}
		break;

	case (ME4600_AO_SYNC_HOLD | ME4600_AO_SYNC_EXT_TRIG):	//Synchronous hardware
		ctrl |= ME4600_AO_CTRL_BIT_ENABLE_EX_TRIG;
		if ((sync_mask & ((ME4600_AO_SYNC_HOLD | ME4600_AO_SYNC_EXT_TRIG) << instance->ao_idx)) != (ME4600_AO_SYNC_HOLD | ME4600_AO_SYNC_EXT_TRIG)) {	//Now we can set correct mode
			sync_mask |=
			    (ME4600_AO_SYNC_HOLD | ME4600_AO_SYNC_EXT_TRIG) <<
			    instance->ao_idx;

			outl(sync_mask, instance->preload_reg);
			PDEBUG_REG("preload_reg outl(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->preload_reg - instance->reg_base,
				   sync_mask);
		}
		break;
	}
//              spin_unlock(instance->preload_reg_lock);        // Moved down.

	//Activate ISM (remove 'stop' bits)
	ctrl &=
	    ~(ME4600_AO_CTRL_BIT_EX_TRIG_EDGE |
	      ME4600_AO_CTRL_BIT_EX_TRIG_EDGE_BOTH);
	ctrl |= instance->ctrl_trg;
	ctrl &= ~(ME4600_AO_CTRL_BIT_STOP | ME4600_AO_CTRL_BIT_IMMEDIATE_STOP);
	outl(ctrl, instance->ctrl_reg);
	PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->ctrl_reg - instance->reg_base, ctrl);
	spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);

/// @note When flag 'ME_IO_SINGLE_TYPE_TRIG_SYNCHRONOUS' is set than output is triggered. ALWAYS!

	if (!instance->fifo) {	//No FIFO
		if (flags & ME_IO_SINGLE_TYPE_TRIG_SYNCHRONOUS) {	//Fired all software synchronous outputs.
			tmp = ~(*instance->preload_flags | 0xFFFF0000);
			PINFO
			    ("Fired all software synchronous outputs. mask:0x%08x\n",
			     tmp);
			tmp |= sync_mask & 0xFFFF0000;
			// Add this channel to list
			tmp &= ~(ME4600_AO_SYNC_HOLD << instance->ao_idx);

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
		} else if (!mode) {	// Add this channel to list
			outl(sync_mask &
			     ~(ME4600_AO_SYNC_HOLD << instance->ao_idx),
			     instance->preload_reg);
			PDEBUG_REG("preload_reg outl(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->preload_reg - instance->reg_base,
				   sync_mask & ~(ME4600_AO_SYNC_HOLD <<
						 instance->ao_idx));

			//Fire
			PINFO("Software trigger.\n");

			//Restore save settings
			outl(sync_mask, instance->preload_reg);
			PDEBUG_REG("preload_reg outl(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->preload_reg - instance->reg_base,
				   sync_mask);
		}

	} else {		// mix-mode - begin
		if (flags & ME_IO_SINGLE_TYPE_TRIG_SYNCHRONOUS) {	//Trigger outputs
			//Add channel to start list
			outl(sync_mask |
			     (ME4600_AO_SYNC_HOLD << instance->ao_idx),
			     instance->preload_reg);
			PDEBUG_REG("preload_reg outl(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->preload_reg - instance->reg_base,
				   sync_mask | (ME4600_AO_SYNC_HOLD <<
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
/*			//Remove channel from start list //<== Unnecessary. Removed.
			outl(sync_mask & ~(ME4600_AO_SYNC_HOLD << instance->ao_idx), instance->preload_reg);
			PDEBUG_REG("preload_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base, instance->preload_reg - instance->reg_base, tmp);
*/
			//Fire
			PINFO("Software trigger.\n");
			outl(0x8000, instance->single_reg);
			PDEBUG_REG("single_reg outl(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->single_reg - instance->reg_base,
				   0x8000);

/*			//Restore save settings //<== Unnecessary. Removed.
			outl(sync_mask, instance->preload_reg);
			PDEBUG_REG("preload_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base, instance->preload_reg - instance->reg_base, sync_mask);
*/
		}
	}
	spin_unlock(instance->preload_reg_lock);

	j = jiffies;
	instance->status = ao_status_single_run_wait;

	instance->timeout.delay = delay;
	instance->timeout.start_time = j;
	instance->ao_control_task_flag = 1;
	queue_delayed_work(instance->me4600_workqueue,
			   &instance->ao_control_task, 1);

	if (!(flags & ME_IO_SINGLE_TYPE_WRITE_NONBLOCKING)) {

		//Only runing process will interrupt this call. Events are signaled when status change. Extra timeout add for safe reason.
		wait_event_interruptible_timeout(instance->wait_queue,
						 (instance->status !=
						  ao_status_single_run_wait),
						 (delay) ? delay +
						 1 : LONG_MAX);

		if (((!delay) || ((jiffies - j) <= delay))
		    && (instance->status != ao_status_single_end)) {
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
			} else {
				if ((jiffies - j) > delay) {
					PERROR
					    ("Timeout reached. Not handled by control task!\n");
				} else {
					PERROR
					    ("Timeout reached. Signal come but status is strange: %d\n",
					     instance->status);
				}

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

static int me4600_ao_io_stream_config(me_subdevice_t *subdevice,
				      struct file *filep,
				      meIOStreamConfig_t *config_list,
				      int count,
				      meIOStreamTrigger_t *trigger,
				      int fifo_irq_threshold, int flags)
{
	me4600_ao_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	uint32_t ctrl;
	unsigned long cpu_flags;
	uint64_t conv_ticks;
	unsigned int conv_start_ticks_low = trigger->iConvStartTicksLow;
	unsigned int conv_start_ticks_high = trigger->iConvStartTicksHigh;

	instance = (me4600_ao_subdevice_t *) subdevice;

	PDEBUG("executed. idx=%d\n", instance->ao_idx);

	if (!instance->fifo) {
		PERROR("Not a streaming ao.\n");
		return ME_ERRNO_NOT_SUPPORTED;
	}

	conv_ticks =
	    (uint64_t) conv_start_ticks_low +
	    ((uint64_t) conv_start_ticks_high << 32);

	if (flags &
	    ~(ME_IO_STREAM_CONFIG_HARDWARE_ONLY | ME_IO_STREAM_CONFIG_WRAPAROUND
	      | ME_IO_STREAM_CONFIG_BIT_PATTERN)) {
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

	switch (trigger->iAcqStartTrigType) {
	case ME_TRIG_TYPE_SW:
		if (trigger->iAcqStartTrigEdge != ME_TRIG_EDGE_NONE) {
			PERROR
			    ("Invalid acquisition start trigger edge specified.\n");
			return ME_ERRNO_INVALID_ACQ_START_TRIG_EDGE;
		}
		break;

	case ME_TRIG_TYPE_EXT_DIGITAL:
		switch (trigger->iAcqStartTrigEdge) {
		case ME_TRIG_EDGE_ANY:
		case ME_TRIG_EDGE_RISING:
		case ME_TRIG_EDGE_FALLING:
			break;

		default:
			PERROR
			    ("Invalid acquisition start trigger edge specified.\n");
			return ME_ERRNO_INVALID_ACQ_START_TRIG_EDGE;
		}
		break;

	default:
		PERROR("Invalid acquisition start trigger type specified.\n");
		return ME_ERRNO_INVALID_ACQ_START_TRIG_TYPE;
	}

	if (trigger->iScanStartTrigType != ME_TRIG_TYPE_FOLLOW) {
		PERROR("Invalid scan start trigger type specified.\n");
		return ME_ERRNO_INVALID_SCAN_START_TRIG_TYPE;
	}

	if (trigger->iConvStartTrigType != ME_TRIG_TYPE_TIMER) {
		PERROR("Invalid conv start trigger type specified.\n");
		return ME_ERRNO_INVALID_CONV_START_TRIG_TYPE;
	}

	if ((conv_ticks < ME4600_AO_MIN_CHAN_TICKS)
	    || (conv_ticks > ME4600_AO_MAX_CHAN_TICKS)) {
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

	if ((flags & ME_IO_STREAM_CONFIG_BIT_PATTERN) && !instance->bitpattern) {
		PERROR("This subdevice not support output redirection.\n");
		ME_SUBDEVICE_EXIT;
		return ME_ERRNO_INVALID_FLAGS;
	}
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
	ctrl =
	    ME4600_AO_CTRL_BIT_IMMEDIATE_STOP | ME4600_AO_CTRL_BIT_STOP |
	    ME4600_AO_CTRL_BIT_RESET_IRQ;
	outl(ctrl, instance->ctrl_reg);
	PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->ctrl_reg - instance->reg_base, ctrl);

	//This is paranoic, but to be sure.
	instance->preloaded_count = 0;
	instance->data_count = 0;
	instance->circ_buf.head = 0;
	instance->circ_buf.tail = 0;

	/* Set mode. */
	if (flags & ME_IO_STREAM_CONFIG_WRAPAROUND) {	//Wraparound
		if (flags & ME_IO_STREAM_CONFIG_HARDWARE_ONLY) {	//Hardware wraparound
			PINFO("Hardware wraparound.\n");
			ctrl |= ME4600_AO_MODE_WRAPAROUND;
			instance->mode = ME4600_AO_HW_WRAP_MODE;
		} else {	//Software wraparound
			PINFO("Software wraparound.\n");
			ctrl |= ME4600_AO_MODE_CONTINUOUS;
			instance->mode = ME4600_AO_SW_WRAP_MODE;
		}
	} else {		//Continous
		PINFO("Continous.\n");
		ctrl |= ME4600_AO_MODE_CONTINUOUS;
		instance->mode = ME4600_AO_CONTINOUS;
	}

	//Set the trigger edge.
	if (trigger->iAcqStartTrigType == ME_TRIG_TYPE_EXT_DIGITAL) {	//Set the trigger type and edge for external trigger.
		PINFO("External digital trigger.\n");
		instance->start_mode = ME4600_AO_EXT_TRIG;
/*
			ctrl |= ME4600_AO_CTRL_BIT_ENABLE_EX_TRIG;
*/
		switch (trigger->iAcqStartTrigEdge) {
		case ME_TRIG_EDGE_RISING:
			PINFO("Set the trigger edge: rising.\n");
			instance->ctrl_trg = 0x0;
			break;

		case ME_TRIG_EDGE_FALLING:
			PINFO("Set the trigger edge: falling.\n");
//                                      ctrl |= ME4600_AO_CTRL_BIT_EX_TRIG_EDGE;
			instance->ctrl_trg = ME4600_AO_CTRL_BIT_EX_TRIG_EDGE;
			break;

		case ME_TRIG_EDGE_ANY:
			PINFO("Set the trigger edge: both edges.\n");
//                                      ctrl |= ME4600_AO_CTRL_BIT_EX_TRIG_EDGE | ME4600_AO_CTRL_BIT_EX_TRIG_EDGE_BOTH;
			instance->ctrl_trg =
			    ME4600_AO_CTRL_BIT_EX_TRIG_EDGE |
			    ME4600_AO_CTRL_BIT_EX_TRIG_EDGE_BOTH;
			break;
		}
	} else {
		PINFO("Internal software trigger.\n");
		instance->start_mode = 0;
	}

	//Set the stop mode and value.
	if (trigger->iAcqStopTrigType == ME_TRIG_TYPE_COUNT) {	//Amount of data
		instance->stop_mode = ME4600_AO_ACQ_STOP_MODE;
		instance->stop_count = trigger->iAcqStopCount;
	} else if (trigger->iScanStopTrigType == ME_TRIG_TYPE_COUNT) {	//Amount of 'scans'
		instance->stop_mode = ME4600_AO_SCAN_STOP_MODE;
		instance->stop_count = trigger->iScanStopCount;
	} else {		//Infinite
		instance->stop_mode = ME4600_AO_INF_STOP_MODE;
		instance->stop_count = 0;
	}

	PINFO("Stop count: %d.\n", instance->stop_count);

	if (trigger->iAcqStartTrigChan == ME_TRIG_CHAN_SYNCHRONOUS) {	//Synchronous start
		instance->start_mode |= ME4600_AO_SYNC_HOLD;
		if (trigger->iAcqStartTrigType == ME_TRIG_TYPE_EXT_DIGITAL) {	//Externaly triggered
			PINFO("Synchronous start. Externaly trigger active.\n");
			instance->start_mode |= ME4600_AO_SYNC_EXT_TRIG;
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
	instance->hardware_stop_delay = (int)(conv_ticks * HZ) / ME4600_AO_BASE_FREQUENCY;	//<== MUST be with cast!

	//Conect outputs to analog or digital port.
	if (flags & ME_IO_STREAM_CONFIG_BIT_PATTERN) {
		ctrl |= ME4600_AO_CTRL_BIT_ENABLE_DO;
	}
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

static int me4600_ao_io_stream_new_values(me_subdevice_t *subdevice,
					  struct file *filep,
					  int time_out, int *count, int flags)
{
	me4600_ao_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	long t = 0;
	long j;

	instance = (me4600_ao_subdevice_t *) subdevice;

	PDEBUG("executed. idx=%d\n", instance->ao_idx);

	if (!instance->fifo) {
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
						       ME4600_AO_STATUS_BIT_FSM)),
						 t);

		if (!(inl(instance->status_reg) & ME4600_AO_STATUS_BIT_FSM)) {
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

static int me4600_ao_io_stream_start(me_subdevice_t *subdevice,
				     struct file *filep,
				     int start_mode, int time_out, int flags)
{
	me4600_ao_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	unsigned long cpu_flags = 0;
	uint32_t status;
	uint32_t ctrl;
	uint32_t synch;
	int count = 0;
	int circ_buffer_count;

	unsigned long ref;
	unsigned long delay = 0;

	instance = (me4600_ao_subdevice_t *) subdevice;

	PDEBUG("executed. idx=%d\n", instance->ao_idx);

	if (!instance->fifo) {
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

	if (instance->mode == ME4600_AO_CONTINOUS) {	//Continous
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
	instance->single_value = ME4600_AO_MAX_DATA + 1;
	instance->single_value_in_fifo = ME4600_AO_MAX_DATA + 1;

	//Setting stop points
	if (instance->stop_mode == ME4600_AO_SCAN_STOP_MODE) {
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
	if (!(ctrl & ME4600_AO_CTRL_BIT_ENABLE_FIFO)) {	//FIFO wasn't enabeled. Do it. <= This should be done by user call with ME_WRITE_MODE_PRELOAD
		PINFO("Enableing FIFO.\n");
		ctrl |=
		    ME4600_AO_CTRL_BIT_ENABLE_FIFO |
		    ME4600_AO_CTRL_BIT_RESET_IRQ;

		instance->preloaded_count = 0;
		instance->data_count = 0;
	} else {		//Block IRQ
		ctrl |= ME4600_AO_CTRL_BIT_RESET_IRQ;
	}
	outl(ctrl, instance->ctrl_reg);
	PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->ctrl_reg - instance->reg_base,
		   ctrl | ME4600_AO_CTRL_BIT_RESET_IRQ);

	//Fill FIFO <= Generaly this should be done by user pre-load call but this is second place to do it.
	status = inl(instance->status_reg);
	if (!(status & ME4600_AO_STATUS_BIT_EF)) {	//FIFO empty
		if (instance->stop_data_count == 0) {
			count = ME4600_AO_FIFO_COUNT;
		} else {
			count =
			    (ME4600_AO_FIFO_COUNT <
			     instance->
			     stop_data_count) ? ME4600_AO_FIFO_COUNT :
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
	    ~((ME4600_AO_SYNC_HOLD | ME4600_AO_SYNC_EXT_TRIG) << instance->
	      ao_idx);
	synch |=
	    (instance->start_mode & ~ME4600_AO_EXT_TRIG) << instance->ao_idx;
	outl(synch, instance->preload_reg);
	PDEBUG_REG("preload_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->preload_reg - instance->reg_base, synch);
	spin_unlock(instance->preload_reg_lock);

	//Default count is '0'
	if (instance->mode == ME4600_AO_CONTINOUS) {	//Continous
		instance->preloaded_count = 0;
		instance->circ_buf.tail += count;
		instance->circ_buf.tail &= instance->circ_buf.mask;
	} else {		//Wraparound
		instance->preloaded_count += count;
		instance->data_count += count;

		//Special case: Infinite wraparound with less than FIFO datas always should runs in hardware mode.
		if ((instance->stop_mode == ME4600_AO_INF_STOP_MODE)
		    && (circ_buffer_count <= ME4600_AO_FIFO_COUNT)) {	//Change to hardware wraparound
			PDEBUG
			    ("Changeing mode from software wraparound to hardware wraparound.\n");
			//Copy all data
			count =
			    ao_write_data(instance, circ_buffer_count,
					  instance->preloaded_count);
			ctrl &= ~ME4600_AO_CTRL_MODE_MASK;
			ctrl |= ME4600_AO_MODE_WRAPAROUND;
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
	ctrl &= ~(ME4600_AO_CTRL_BIT_STOP | ME4600_AO_CTRL_BIT_IMMEDIATE_STOP);
	if (instance->start_mode == ME4600_AO_EXT_TRIG) {	// External trigger.
		PINFO("External trigger.\n");
		ctrl |= ME4600_AO_CTRL_BIT_ENABLE_EX_TRIG;
	}
	if (!(status & ME4600_AO_STATUS_BIT_HF)) {	//More than half!
		if ((ctrl & ME4600_AO_CTRL_MODE_MASK) == ME4600_AO_MODE_CONTINUOUS) {	//Enable IRQ only when hardware_continous is set and FIFO is more than half
			ctrl &= ~ME4600_AO_CTRL_BIT_RESET_IRQ;
			ctrl |= ME4600_AO_CTRL_BIT_ENABLE_IRQ;
		}
	}
	outl(ctrl, instance->ctrl_reg);
	PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->ctrl_reg - instance->reg_base, ctrl);
	spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);

	//Trigger output
	if (flags & ME_IO_SINGLE_TYPE_TRIG_SYNCHRONOUS) {	//Trigger outputs
		spin_lock(instance->preload_reg_lock);
		synch = inl(instance->preload_reg);
		//Add channel to start list
		outl(synch | (ME4600_AO_SYNC_HOLD << instance->ao_idx),
		     instance->preload_reg);
		PDEBUG_REG("preload_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->preload_reg - instance->reg_base,
			   synch | (ME4600_AO_SYNC_HOLD << instance->ao_idx));

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
		//Remove channel from start list.	// <== Unnecessary. Removed.
		spin_lock(instance->preload_reg_lock);
			synch = inl(instance->preload_reg);
			outl(synch & ~(ME4600_AO_SYNC_HOLD << instance->ao_idx), instance->preload_reg);
			PDEBUG_REG("preload_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base, instance->preload_reg - instance->reg_base, synch & ~(ME4600_AO_SYNC_HOLD << instance->ao_idx));
*/
		//Fire
		PINFO("Software trigger.\n");
		outl(0x8000, instance->single_reg);
		PDEBUG_REG("single_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->single_reg - instance->reg_base, 0x8000);

/*
			//Restore save settings.	// <== Unnecessary. Removed.
			outl(synch, instance->preload_reg);
			PDEBUG_REG("preload_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base, instance->preload_reg - instance->reg_base, synch);
		spin_unlock(instance->preload_reg_lock);
*/
	}
	// Set control task's timeout
	ref = jiffies;
	instance->timeout.delay = delay;
	instance->timeout.start_time = ref;

	if (status & ME4600_AO_STATUS_BIT_HF) {	//Less than half but not empty!
		PINFO("Less than half.\n");
		if (instance->stop_data_count != 0) {
			count = ME4600_AO_FIFO_COUNT / 2;
		} else {
			count =
			    ((ME4600_AO_FIFO_COUNT / 2) <
			     instance->stop_data_count) ? ME4600_AO_FIFO_COUNT /
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

		if (instance->mode == ME4600_AO_CONTINOUS) {	//Continous
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
		if (!(status & ME4600_AO_STATUS_BIT_HF)) {	//More than half!
			spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);
			ctrl = inl(instance->ctrl_reg);
			ctrl &= ~ME4600_AO_CTRL_BIT_RESET_IRQ;
			ctrl |= ME4600_AO_CTRL_BIT_ENABLE_IRQ;
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
	if ((instance->stop_mode != ME4600_AO_INF_STOP_MODE)
	    && (instance->mode == ME4600_AO_SW_WRAP_MODE)
	    && (circ_buffer_count <= (ME4600_AO_FIFO_COUNT / 2))) {	//Put more data to FIFO
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

			if (!((status = inl(instance->status_reg)) & ME4600_AO_STATUS_BIT_HF)) {	//FIFO is more than half. Enable IRQ and end copy.
				spin_lock_irqsave(&instance->subdevice_lock,
						  cpu_flags);
				ctrl = inl(instance->ctrl_reg);
				ctrl &= ~ME4600_AO_CTRL_BIT_RESET_IRQ;
				ctrl |= ME4600_AO_CTRL_BIT_ENABLE_IRQ;
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
	// Schedule control task.
	instance->ao_control_task_flag = 1;
	queue_delayed_work(instance->me4600_workqueue,
			   &instance->ao_control_task, 1);

	if (start_mode == ME_START_MODE_BLOCKING) {	//Wait for start.
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
		} else if ((delay) && ((jiffies - ref) >= delay)) {
			if (instance->status != ao_status_stream_run) {
				if (instance->status == ao_status_stream_end) {
					PDEBUG("Timeout reached.\n");
				} else {
					if ((jiffies - ref) > delay) {
						PERROR
						    ("Timeout reached. Not handled by control task!\n");
					} else {
						PERROR
						    ("Timeout reached. Signal come but status is strange: %d\n",
						     instance->status);
					}
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

static int me4600_ao_io_stream_status(me_subdevice_t *subdevice,
				      struct file *filep,
				      int wait,
				      int *status, int *values, int flags)
{
	me4600_ao_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;

	instance = (me4600_ao_subdevice_t *) subdevice;

	PDEBUG("executed. idx=%d\n", instance->ao_idx);

	if (!instance->fifo) {
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
		    (inl(instance->status_reg) & ME4600_AO_STATUS_BIT_FSM) ?
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

static int me4600_ao_io_stream_stop(me_subdevice_t *subdevice,
				    struct file *filep,
				    int stop_mode, int flags)
{				// Stop work and empty buffer and FIFO
	int err = ME_ERRNO_SUCCESS;
	me4600_ao_subdevice_t *instance;
	unsigned long cpu_flags;
	volatile uint32_t ctrl;

	instance = (me4600_ao_subdevice_t *) subdevice;

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

	if (!instance->fifo) {
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
		ctrl = inl(instance->ctrl_reg) & ME4600_AO_CTRL_MODE_MASK;
		if (ctrl == ME4600_AO_MODE_WRAPAROUND) {	//Hardware wraparound => Hardware stop.
			spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);
			ctrl = inl(instance->ctrl_reg);
			ctrl |=
			    ME4600_AO_CTRL_BIT_STOP |
			    ME4600_AO_CTRL_BIT_RESET_IRQ;
			ctrl &= ~ME4600_AO_CTRL_BIT_ENABLE_IRQ;
			outl(ctrl, instance->ctrl_reg);
			PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->ctrl_reg - instance->reg_base,
				   ctrl);
			spin_unlock_irqrestore(&instance->subdevice_lock,
					       cpu_flags);
		}
		//Only runing process will interrupt this call. Events are signaled when status change.
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
	ctrl |=
	    ME4600_AO_CTRL_BIT_STOP | ME4600_AO_CTRL_BIT_IMMEDIATE_STOP |
	    ME4600_AO_CTRL_BIT_RESET_IRQ;
	ctrl &= ~ME4600_AO_CTRL_BIT_ENABLE_IRQ;
	if (!flags) {		//Reset FIFO
		ctrl &= ~ME4600_AO_CTRL_BIT_ENABLE_FIFO;
	}
	outl(ctrl, instance->ctrl_reg);
	PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->ctrl_reg - instance->reg_base, ctrl);
	spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);

	if (!flags) {		//Reset software buffer
		instance->circ_buf.head = 0;
		instance->circ_buf.tail = 0;
		instance->preloaded_count = 0;
		instance->data_count = 0;
	}

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me4600_ao_io_stream_write(me_subdevice_t *subdevice,
				     struct file *filep,
				     int write_mode,
				     int *values, int *count, int flags)
{
	int err = ME_ERRNO_SUCCESS;
	me4600_ao_subdevice_t *instance;
	unsigned long cpu_flags = 0;
	uint32_t reg_copy;

	int copied_from_user = 0;
	int left_to_copy_from_user = *count;

	int copied_values;

	instance = (me4600_ao_subdevice_t *) subdevice;

	PDEBUG("executed. idx=%d\n", instance->ao_idx);

	//Checking arguments
	if (!instance->fifo) {
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
/// @note If no 'pre-load' is used. stream_start() will move data to FIFO.
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
		/// @note Some other thread must empty buffer by starting engine.
		break;

	default:
		PERROR("Invalid write mode specified.\n");
		return ME_ERRNO_INVALID_WRITE_MODE;
	}

	if (instance->mode & ME4600_AO_WRAP_MODE) {	//Wraparound mode. Device must be stopped.
		if ((instance->status != ao_status_stream_configured)
		    && (instance->status != ao_status_stream_end)) {
			PERROR
			    ("Subdevice mustn't be runing when 'pre-load' mode is used.\n");
			return ME_ERRNO_INVALID_WRITE_MODE;
		}
	}

	if ((instance->mode == ME4600_AO_HW_WRAP_MODE) && (write_mode != ME_WRITE_MODE_PRELOAD)) {	// hardware wrap_around mode.
		//This is transparent for user.
		PDEBUG("Changing write_mode to ME_WRITE_MODE_PRELOAD.\n");
		write_mode = ME_WRITE_MODE_PRELOAD;
	}

	ME_SUBDEVICE_ENTER;

	if (write_mode == ME_WRITE_MODE_PRELOAD) {	//Init enviroment - preload
		spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);
		reg_copy = inl(instance->ctrl_reg);
		//Check FIFO
		if (!(reg_copy & ME4600_AO_CTRL_BIT_ENABLE_FIFO)) {	//FIFO not active. Enable it.
			reg_copy |= ME4600_AO_CTRL_BIT_ENABLE_FIFO;
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
		if ((instance->status == ao_status_stream_run) && !(reg_copy & ME4600_AO_STATUS_BIT_FSM)) {	//BROKEN PIPE! The state machine is stoped but logical status show that should be working.
			PERROR("Broken pipe in write.\n");
			err = ME_ERRNO_SUBDEVICE_NOT_RUNNING;
			break;
		}

		if ((instance->status == ao_status_stream_run) && (instance->mode == ME4600_AO_CONTINOUS) && (reg_copy & ME4600_AO_STATUS_BIT_HF)) {	//Continous mode runing and data are below half!

			// Block interrupts.
			spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);
			reg_copy = inl(instance->ctrl_reg);
			//reg_copy &= ~ME4600_AO_CTRL_BIT_ENABLE_IRQ;
			reg_copy |= ME4600_AO_CTRL_BIT_RESET_IRQ;
			outl(reg_copy, instance->ctrl_reg);
			PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->ctrl_reg - instance->reg_base,
				   reg_copy);
			spin_unlock_irqrestore(&instance->subdevice_lock,
					       cpu_flags);

			//Fast copy
			copied_values =
			    ao_write_data(instance, ME4600_AO_FIFO_COUNT / 2,
					  0);
			if (copied_values > 0) {
				instance->circ_buf.tail += copied_values;
				instance->circ_buf.tail &=
				    instance->circ_buf.mask;
				continue;
			}
			// Activate interrupts.
			spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);
			reg_copy = inl(instance->ctrl_reg);
			//reg_copy |= ME4600_AO_CTRL_BIT_ENABLE_IRQ;
			reg_copy &= ~ME4600_AO_CTRL_BIT_RESET_IRQ;
			outl(reg_copy, instance->ctrl_reg);
			PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->ctrl_reg - instance->reg_base,
				   reg_copy);
			spin_unlock_irqrestore(&instance->subdevice_lock,
					       cpu_flags);

			if (copied_values == 0) {	//This was checked and never should happend!
				PERROR_CRITICAL("COPING FINISH WITH 0!\n");
			}

			if (copied_values < 0) {	//This was checked and never should happend!
				PERROR_CRITICAL
				    ("COPING FINISH WITH AN ERROR!\n");
				instance->status = ao_status_stream_fifo_error;
				err = ME_ERRNO_FIFO_BUFFER_OVERFLOW;
				break;
			}
		}

		if (!left_to_copy_from_user) {	//All datas were copied.
			break;
		} else {	//Not all datas were copied.
			if (instance->mode & ME4600_AO_WRAP_MODE) {	//Error too much datas! Wraparound is limited in size!
				PERROR
				    ("Too much data for wraparound mode!  Exceeded size of %ld.\n",
				     ME4600_AO_CIRC_BUF_COUNT - 1);
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
		    ao_write_data_pooling(instance, ME4600_AO_FIFO_COUNT,
					  instance->preloaded_count);
		instance->preloaded_count += copied_values;
		instance->data_count += copied_values;

		if ((instance->mode == ME4600_AO_HW_WRAP_MODE)
		    && (me_circ_buf_values(&instance->circ_buf) >
			ME4600_AO_FIFO_COUNT)) {
			PERROR
			    ("Too much data for hardware wraparound mode! Exceeded size of %d.\n",
			     ME4600_AO_FIFO_COUNT);
			err = ME_ERRNO_FIFO_BUFFER_OVERFLOW;
		}
	}

	*count = *count - left_to_copy_from_user;
	ME_SUBDEVICE_EXIT;

	return err;
}
static irqreturn_t me4600_ao_isr(int irq, void *dev_id)
{
	me4600_ao_subdevice_t *instance = dev_id;
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
	if (!(irq_status & (ME4600_IRQ_STATUS_BIT_AO_HF << instance->ao_idx))) {
		PINFO("%ld Shared interrupt. %s(): ID=%d: status_reg=0x%04X\n",
		      jiffies, __func__, instance->ao_idx, irq_status);
		return IRQ_NONE;
	}

	if (!instance->circ_buf.buf) {
		instance->status = ao_status_stream_error;
		PERROR_CRITICAL("CIRCULAR BUFFER NOT EXISTS!\n");
		//Block interrupts. Stop machine.
		ctrl = inl(instance->ctrl_reg);
		ctrl &= ~ME4600_AO_CTRL_BIT_ENABLE_IRQ;
		ctrl |=
		    ME4600_AO_CTRL_BIT_RESET_IRQ |
		    ME4600_AO_CTRL_BIT_IMMEDIATE_STOP | ME4600_AO_CTRL_BIT_STOP;
		outl(ctrl, instance->ctrl_reg);
		PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->ctrl_reg - instance->reg_base, ctrl);

		//Inform user
		wake_up_interruptible_all(&instance->wait_queue);
		return IRQ_HANDLED;
	}

	status = inl(instance->status_reg);
	if (!(status & ME4600_AO_STATUS_BIT_FSM)) {	//Too late. Not working! END? BROKEN PIPE?
		PDEBUG("Interrupt come but ISM is not working!\n");
		//Block interrupts. Stop machine.
		ctrl = inl(instance->ctrl_reg);
		ctrl &= ~ME4600_AO_CTRL_BIT_ENABLE_IRQ;
		ctrl |=
		    ME4600_AO_CTRL_BIT_RESET_IRQ | ME4600_AO_CTRL_BIT_STOP |
		    ME4600_AO_CTRL_BIT_IMMEDIATE_STOP;
		outl(ctrl, instance->ctrl_reg);
		PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->ctrl_reg - instance->reg_base, ctrl);

		return IRQ_HANDLED;
	}
	//General procedure. Process more datas.

#ifdef MEDEBUG_DEBUG
	if (!me_circ_buf_values(&instance->circ_buf)) {	//Buffer is empty!
		PDEBUG("Circular buffer empty!\n");
	}
#endif

	//Check FIFO
	if (status & ME4600_AO_STATUS_BIT_HF) {	//OK less than half

		//Block interrupts
		ctrl = inl(instance->ctrl_reg);
		ctrl &= ~ME4600_AO_CTRL_BIT_ENABLE_IRQ;
		ctrl |= ME4600_AO_CTRL_BIT_RESET_IRQ;
		outl(ctrl, instance->ctrl_reg);
		PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->ctrl_reg - instance->reg_base, ctrl);

		do {
			//Calculate how many should be copied.
			count =
			    (instance->stop_data_count) ? instance->
			    stop_data_count -
			    instance->data_count : ME4600_AO_FIFO_COUNT / 2;
			if (ME4600_AO_FIFO_COUNT / 2 < count) {
				count = ME4600_AO_FIFO_COUNT / 2;
			}
			//Copy data
			if (instance->mode == ME4600_AO_CONTINOUS) {	//Continous
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
			} else if ((instance->mode == ME4600_AO_SW_WRAP_MODE) && ((ctrl & ME4600_AO_CTRL_MODE_MASK) == ME4600_AO_MODE_CONTINUOUS)) {	//Wraparound (software)
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
			inl(instance->status_reg)) & ME4600_AO_STATUS_BIT_HF);

		//Unblock interrupts
		ctrl = inl(instance->ctrl_reg);
		if (count >= 0) {	//Copy was successful.
			if (instance->stop_data_count && (instance->stop_data_count <= instance->data_count)) {	//Finishing work. No more interrupts.
				PDEBUG("Finishing work. Interrupt disabled.\n");
				instance->status = ao_status_stream_end_wait;
			} else if (count > 0) {	//Normal work. Enable interrupt.
				PDEBUG("Normal work. Enable interrupt.\n");
				ctrl &= ~ME4600_AO_CTRL_BIT_RESET_IRQ;
				ctrl |= ME4600_AO_CTRL_BIT_ENABLE_IRQ;
			} else {	//Normal work but there are no more data in buffer. Interrupt active but blocked. stream_write() will unblock it.
				PDEBUG
				    ("No data in software buffer. Interrupt blocked.\n");
				ctrl |= ME4600_AO_CTRL_BIT_ENABLE_IRQ;
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
		//Reset pending interrupt
		ctrl = inl(instance->ctrl_reg);
		ctrl |= ME4600_AO_CTRL_BIT_RESET_IRQ;
		outl(ctrl, instance->ctrl_reg);
		PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->ctrl_reg - instance->reg_base, ctrl);
		ctrl &= ~ME4600_AO_CTRL_BIT_RESET_IRQ;
		outl(ctrl, instance->ctrl_reg);
		PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->ctrl_reg - instance->reg_base, ctrl);
	}

	PINFO("ISR: Buffer count: %d.(T:%d H:%d)\n",
	      me_circ_buf_values(&instance->circ_buf), instance->circ_buf.tail,
	      instance->circ_buf.head);
	PINFO("ISR: Stop count: %d.\n", instance->stop_count);
	PINFO("ISR: Stop data count: %d.\n", instance->stop_data_count);
	PINFO("ISR: Data count: %d.\n", instance->data_count);

	//Inform user
	wake_up_interruptible_all(&instance->wait_queue);

	return IRQ_HANDLED;
}

static void me4600_ao_destructor(struct me_subdevice *subdevice)
{
	me4600_ao_subdevice_t *instance;

	instance = (me4600_ao_subdevice_t *) subdevice;

	PDEBUG("executed. idx=%d\n", instance->ao_idx);

	instance->ao_control_task_flag = 0;

	// Reset subdevice to asure clean exit.
	me4600_ao_io_reset_subdevice(subdevice, NULL,
				     ME_IO_RESET_SUBDEVICE_NO_FLAGS);

	// Remove any tasks from work queue. This is paranoic because it was done allready in reset().
	if (!cancel_delayed_work(&instance->ao_control_task)) {	//Wait 2 ticks to be sure that control task is removed from queue.
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(2);
	}

	if (instance->fifo) {
		if (instance->irq) {
			free_irq(instance->irq, instance);
			instance->irq = 0;
		}

		if (instance->circ_buf.buf) {
			free_pages((unsigned long)instance->circ_buf.buf,
				   ME4600_AO_CIRC_BUF_SIZE_ORDER);
		}
		instance->circ_buf.buf = NULL;
	}

	me_subdevice_deinit(&instance->base);
	kfree(instance);
}

me4600_ao_subdevice_t *me4600_ao_constructor(uint32_t reg_base,
					     spinlock_t *preload_reg_lock,
					     uint32_t *preload_flags,
					     int ao_idx,
					     int fifo,
					     int irq,
					     struct workqueue_struct *me4600_wq)
{
	me4600_ao_subdevice_t *subdevice;
	int err;

	PDEBUG("executed. idx=%d\n", ao_idx);

	// Allocate memory for subdevice instance.
	subdevice = kmalloc(sizeof(me4600_ao_subdevice_t), GFP_KERNEL);

	if (!subdevice) {
		PERROR("Cannot get memory for subdevice instance.\n");
		return NULL;
	}

	memset(subdevice, 0, sizeof(me4600_ao_subdevice_t));

	// Initialize subdevice base class.
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

	// Store analog output index.
	subdevice->ao_idx = ao_idx;

	// Store if analog output has fifo.
	subdevice->fifo = (ao_idx < fifo) ? 1 : 0;

	if (subdevice->fifo) {	// Allocate and initialize circular buffer.
		subdevice->circ_buf.mask = ME4600_AO_CIRC_BUF_COUNT - 1;

		subdevice->circ_buf.buf =
		    (void *)__get_free_pages(GFP_KERNEL,
					     ME4600_AO_CIRC_BUF_SIZE_ORDER);
		PDEBUG("circ_buf = %p size=%ld\n", subdevice->circ_buf.buf,
		       ME4600_AO_CIRC_BUF_SIZE);

		if (!subdevice->circ_buf.buf) {
			PERROR
			    ("Cannot initialize subdevice base class instance.\n");
			kfree(subdevice);
			return NULL;
		}

		memset(subdevice->circ_buf.buf, 0, ME4600_AO_CIRC_BUF_SIZE);
	} else {		// No FIFO.
		subdevice->circ_buf.mask = 0;
		subdevice->circ_buf.buf = NULL;
	}

	subdevice->circ_buf.head = 0;
	subdevice->circ_buf.tail = 0;

	subdevice->status = ao_status_none;
	subdevice->ao_control_task_flag = 0;
	subdevice->timeout.delay = 0;
	subdevice->timeout.start_time = jiffies;

	// Initialize wait queue.
	init_waitqueue_head(&subdevice->wait_queue);

	// Initialize single value to 0V.
	subdevice->single_value = 0x8000;
	subdevice->single_value_in_fifo = 0x8000;

	// Register interrupt service routine.
	if (subdevice->fifo) {
		subdevice->irq = irq;
		if (request_irq(subdevice->irq, me4600_ao_isr,
				IRQF_DISABLED | IRQF_SHARED,
				ME4600_NAME, subdevice)) {
			PERROR("Cannot get interrupt line.\n");
			PDEBUG("free circ_buf = %p size=%d",
			       subdevice->circ_buf.buf,
			       PAGE_SHIFT << ME4600_AO_CIRC_BUF_SIZE_ORDER);
			free_pages((unsigned long)subdevice->circ_buf.buf,
				   ME4600_AO_CIRC_BUF_SIZE_ORDER);
			me_subdevice_deinit((me_subdevice_t *) subdevice);
			kfree(subdevice);
			return NULL;
		}
		PINFO("Registered irq=%d.\n", subdevice->irq);
	} else {
		subdevice->irq = 0;
	}

	// Initialize registers.
	subdevice->irq_status_reg = reg_base + ME4600_IRQ_STATUS_REG;
	subdevice->preload_reg = reg_base + ME4600_AO_SYNC_REG;
	if (ao_idx == 0) {
		subdevice->ctrl_reg = reg_base + ME4600_AO_00_CTRL_REG;
		subdevice->status_reg = reg_base + ME4600_AO_00_STATUS_REG;
		subdevice->fifo_reg = reg_base + ME4600_AO_00_FIFO_REG;
		subdevice->single_reg = reg_base + ME4600_AO_00_SINGLE_REG;
		subdevice->timer_reg = reg_base + ME4600_AO_00_TIMER_REG;
		subdevice->reg_base = reg_base;
		subdevice->bitpattern = 0;
	} else if (ao_idx == 1) {
		subdevice->ctrl_reg = reg_base + ME4600_AO_01_CTRL_REG;
		subdevice->status_reg = reg_base + ME4600_AO_01_STATUS_REG;
		subdevice->fifo_reg = reg_base + ME4600_AO_01_FIFO_REG;
		subdevice->single_reg = reg_base + ME4600_AO_01_SINGLE_REG;
		subdevice->timer_reg = reg_base + ME4600_AO_01_TIMER_REG;
		subdevice->reg_base = reg_base;
		subdevice->bitpattern = 0;
	} else if (ao_idx == 2) {
		subdevice->ctrl_reg = reg_base + ME4600_AO_02_CTRL_REG;
		subdevice->status_reg = reg_base + ME4600_AO_02_STATUS_REG;
		subdevice->fifo_reg = reg_base + ME4600_AO_02_FIFO_REG;
		subdevice->single_reg = reg_base + ME4600_AO_02_SINGLE_REG;
		subdevice->timer_reg = reg_base + ME4600_AO_02_TIMER_REG;
		subdevice->reg_base = reg_base;
		subdevice->bitpattern = 0;
	} else if (ao_idx == 3) {
		subdevice->ctrl_reg = reg_base + ME4600_AO_03_CTRL_REG;
		subdevice->status_reg = reg_base + ME4600_AO_03_STATUS_REG;
		subdevice->fifo_reg = reg_base + ME4600_AO_03_FIFO_REG;
		subdevice->single_reg = reg_base + ME4600_AO_03_SINGLE_REG;
		subdevice->timer_reg = reg_base + ME4600_AO_03_TIMER_REG;
		subdevice->reg_base = reg_base;
		subdevice->bitpattern = 1;
	} else {
		PERROR_CRITICAL("WRONG SUBDEVICE idx=%d!", ao_idx);
		me_subdevice_deinit((me_subdevice_t *) subdevice);
		if (subdevice->fifo) {
			free_pages((unsigned long)subdevice->circ_buf.buf,
				   ME4600_AO_CIRC_BUF_SIZE_ORDER);
		}
		subdevice->circ_buf.buf = NULL;
		kfree(subdevice);
		return NULL;
	}

	// Override base class methods.
	subdevice->base.me_subdevice_destructor = me4600_ao_destructor;
	subdevice->base.me_subdevice_io_reset_subdevice =
	    me4600_ao_io_reset_subdevice;
	subdevice->base.me_subdevice_io_single_config =
	    me4600_ao_io_single_config;
	subdevice->base.me_subdevice_io_single_read = me4600_ao_io_single_read;
	subdevice->base.me_subdevice_io_single_write =
	    me4600_ao_io_single_write;
	subdevice->base.me_subdevice_io_stream_config =
	    me4600_ao_io_stream_config;
	subdevice->base.me_subdevice_io_stream_new_values =
	    me4600_ao_io_stream_new_values;
	subdevice->base.me_subdevice_io_stream_write =
	    me4600_ao_io_stream_write;
	subdevice->base.me_subdevice_io_stream_start =
	    me4600_ao_io_stream_start;
	subdevice->base.me_subdevice_io_stream_status =
	    me4600_ao_io_stream_status;
	subdevice->base.me_subdevice_io_stream_stop = me4600_ao_io_stream_stop;
	subdevice->base.me_subdevice_query_number_channels =
	    me4600_ao_query_number_channels;
	subdevice->base.me_subdevice_query_subdevice_type =
	    me4600_ao_query_subdevice_type;
	subdevice->base.me_subdevice_query_subdevice_caps =
	    me4600_ao_query_subdevice_caps;
	subdevice->base.me_subdevice_query_subdevice_caps_args =
	    me4600_ao_query_subdevice_caps_args;
	subdevice->base.me_subdevice_query_range_by_min_max =
	    me4600_ao_query_range_by_min_max;
	subdevice->base.me_subdevice_query_number_ranges =
	    me4600_ao_query_number_ranges;
	subdevice->base.me_subdevice_query_range_info =
	    me4600_ao_query_range_info;
	subdevice->base.me_subdevice_query_timer = me4600_ao_query_timer;

	// Prepare work queue
	subdevice->me4600_workqueue = me4600_wq;

/* workqueue API changed in kernel 2.6.20 */
	INIT_DELAYED_WORK(&subdevice->ao_control_task,
			  me4600_ao_work_control_task);

	if (subdevice->fifo) {	// Set speed for single operations.
		outl(ME4600_AO_MIN_CHAN_TICKS - 1, subdevice->timer_reg);
		subdevice->hardware_stop_delay = HZ / 10;	//100ms
	}

	return subdevice;
}

/** @brief Stop presentation. Preserve FIFOs.
*
* @param instance The subdevice instance (pointer).
*/
inline int ao_stop_immediately(me4600_ao_subdevice_t *instance)
{
	unsigned long cpu_flags;
	uint32_t ctrl;
	int timeout;
	int i;

	timeout =
	    (instance->hardware_stop_delay >
	     (HZ / 10)) ? instance->hardware_stop_delay : HZ / 10;
	for (i = 0; i <= timeout; i++) {
		spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);
		// Stop all actions. No conditions! Block interrupts. Leave FIFO untouched!
		ctrl = inl(instance->ctrl_reg);
		ctrl |=
		    ME4600_AO_CTRL_BIT_STOP | ME4600_AO_CTRL_BIT_IMMEDIATE_STOP
		    | ME4600_AO_CTRL_BIT_RESET_IRQ;
		ctrl &=
		    ~(ME4600_AO_CTRL_BIT_ENABLE_IRQ |
		      ME4600_AO_CTRL_BIT_ENABLE_EX_TRIG);
		outl(ctrl, instance->ctrl_reg);
		PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->ctrl_reg - instance->reg_base, ctrl);
		spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);

		if (!(inl(instance->status_reg) & ME4600_AO_STATUS_BIT_FSM)) {	// Exit.
			break;
		}
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
inline int ao_write_data_wraparound(me4600_ao_subdevice_t *instance, int count,
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
	if (!(status & ME4600_AO_STATUS_BIT_FF)) {	//FIFO is full before all datas were copied!
		PERROR("FIFO was full before all datas were copied! idx=%d\n",
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

	PINFO("WRAPAROUND LOADED %d values. idx=%d\n", local_count,
	      instance->ao_idx);
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
inline int ao_write_data(me4600_ao_subdevice_t *instance, int count,
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
	if (!(status & ME4600_AO_STATUS_BIT_FF)) {	//FIFO is full before all datas were copied!
		PERROR("FIFO was full before all datas were copied! idx=%d\n",
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

	PINFO("FAST LOADED %d values. idx=%d\n", local_count, instance->ao_idx);
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
inline int ao_write_data_pooling(me4600_ao_subdevice_t *instance, int count,
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
		PERROR("SLOW LOADED: Wrong count! idx=%d\n", instance->ao_idx);
		return 0;
	}

	max_count = me_circ_buf_values(&instance->circ_buf) - start_pos;
	if (max_count <= 0) {	//No data to copy!
		PERROR("SLOW LOADED: No data to copy! idx=%d\n",
		       instance->ao_idx);
		return 0;
	}

	if (max_count < count) {
		local_count = max_count;
	}

	for (i = 0; i < local_count; i++) {
		status = inl(instance->status_reg);
		if (!(status & ME4600_AO_STATUS_BIT_FF)) {	//FIFO is full!
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

	PINFO("SLOW LOADED %d values. idx=%d\n", local_count, instance->ao_idx);
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
inline int ao_get_data_from_user(me4600_ao_subdevice_t *instance, int count,
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
			    ("BUFFER LOADED: get_user(0x%p) return an error: %d. idx=%d\n",
			     user_values + i, err, instance->ao_idx);
			return -ME_ERRNO_INTERNAL;
		}
		/// @note The analog output in me4600 series has size of 16 bits.
		*(instance->circ_buf.buf + instance->circ_buf.head) =
		    (uint16_t) value;
		instance->circ_buf.head++;
		instance->circ_buf.head &= instance->circ_buf.mask;
	}

	PINFO("BUFFER LOADED %d values. idx=%d\n", copied, instance->ao_idx);
	return copied;
}

/** @brief Checking actual hardware and logical state.
* @param instance The subdevice instance (pointer).
*/
static void me4600_ao_work_control_task(struct work_struct *work)
{
	me4600_ao_subdevice_t *instance;
	unsigned long cpu_flags = 0;
	uint32_t status;
	uint32_t ctrl;
	uint32_t synch;
	int reschedule = 0;
	int signaling = 0;

	instance =
	    container_of((void *)work, me4600_ao_subdevice_t, ao_control_task);
	PINFO("<%s: %ld> executed. idx=%d\n", __func__, jiffies,
	      instance->ao_idx);

	status = inl(instance->status_reg);
	PDEBUG_REG("status_reg inl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->status_reg - instance->reg_base, status);

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

	case ao_status_stream_end:
		if (!instance->fifo) {
			PERROR_CRITICAL
			    ("Streaming on single device! This feature is not implemented in this version!\n");
			instance->status = ao_status_stream_error;
			// Signal the end.
			signaling = 1;
			break;
		}
	case ao_status_single_end:
		if (status & ME4600_AO_STATUS_BIT_FSM) {	// State machine is working but the status is set to end. Force stop.

			// Wait for stop.
			reschedule = 1;
		}

		spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);
		// Stop all actions. No conditions! Block interrupts and trigger. Leave FIFO untouched!
		ctrl = inl(instance->ctrl_reg);
		ctrl |=
		    ME4600_AO_CTRL_BIT_IMMEDIATE_STOP | ME4600_AO_CTRL_BIT_STOP
		    | ME4600_AO_CTRL_BIT_RESET_IRQ;
		ctrl &=
		    ~(ME4600_AO_CTRL_BIT_ENABLE_IRQ |
		      ME4600_AO_CTRL_BIT_ENABLE_EX_TRIG);
		ctrl &=
		    ~(ME4600_AO_CTRL_BIT_EX_TRIG_EDGE |
		      ME4600_AO_CTRL_BIT_EX_TRIG_EDGE_BOTH);
		outl(ctrl, instance->ctrl_reg);
		PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->ctrl_reg - instance->reg_base, ctrl);
		spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);
		break;

		// Single modes
	case ao_status_single_run_wait:
	case ao_status_single_run:
	case ao_status_single_end_wait:

		if (!(status & ME4600_AO_STATUS_BIT_FSM)) {	// State machine is not working.
			if (((instance->fifo)
			     && (!(status & ME4600_AO_STATUS_BIT_EF)))
			    || (!(instance->fifo))) {	// Single is in end state.
				PDEBUG("Single call has been complited.\n");

				// Set correct value for single_read();
				instance->single_value =
				    instance->single_value_in_fifo;

				// Set status as 'ao_status_single_end'
				instance->status = ao_status_single_end;

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
			spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);
			ctrl = inl(instance->ctrl_reg);
			ctrl |=
			    ME4600_AO_CTRL_BIT_STOP |
			    ME4600_AO_CTRL_BIT_IMMEDIATE_STOP |
			    ME4600_AO_CTRL_BIT_RESET_IRQ;
			ctrl &=
			    ~(ME4600_AO_CTRL_BIT_ENABLE_IRQ |
			      ME4600_AO_CTRL_BIT_ENABLE_EX_TRIG);
			/// Fix for timeout error.
			ctrl &=
			    ~(ME4600_AO_CTRL_BIT_EX_TRIG_EDGE |
			      ME4600_AO_CTRL_BIT_EX_TRIG_EDGE_BOTH);
			if (instance->fifo) {	//Disabling FIFO
				ctrl &= ~ME4600_AO_CTRL_BIT_ENABLE_FIFO;
			}
			outl(ctrl, instance->ctrl_reg);
			PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->ctrl_reg - instance->reg_base,
				   ctrl);
			spin_unlock_irqrestore(&instance->subdevice_lock,
					       cpu_flags);

			spin_lock(instance->preload_reg_lock);
			//Remove from synchronous start. Block triggering from this output.
			synch = inl(instance->preload_reg);
			synch &=
			    ~((ME4600_AO_SYNC_HOLD | ME4600_AO_SYNC_EXT_TRIG) <<
			      instance->ao_idx);
			if (!(instance->fifo)) {	// No FIFO - set to single safe mode
				synch |=
				    ME4600_AO_SYNC_HOLD << instance->ao_idx;
			}
			outl(synch, instance->preload_reg);
			PDEBUG_REG("preload_reg outl(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->preload_reg - instance->reg_base,
				   synch);
			spin_unlock(instance->preload_reg_lock);

			if (!(instance->fifo)) {	// No FIFO
				// Restore old settings.
				PDEBUG("Write old value back to register.\n");
				outl(instance->single_value,
				     instance->single_reg);
				PDEBUG_REG
				    ("single_reg outl(0x%lX+0x%lX)=0x%x\n",
				     instance->reg_base,
				     instance->single_reg - instance->reg_base,
				     instance->single_value);
			}
			// Set correct value for single_read();
			instance->single_value_in_fifo = instance->single_value;

			instance->status = ao_status_single_end;

			// Signal the end.
			signaling = 1;
		}
		// Wait for stop.
		reschedule = 1;
		break;

		// Stream modes
	case ao_status_stream_run_wait:
		if (!instance->fifo) {
			PERROR_CRITICAL
			    ("Streaming on single device! This feature is not implemented in this version!\n");
			instance->status = ao_status_stream_error;
			// Signal the end.
			signaling = 1;
			break;
		}

		if (status & ME4600_AO_STATUS_BIT_FSM) {	// State machine is working. Waiting for start finish.
			instance->status = ao_status_stream_run;

			// Signal end of this step
			signaling = 1;
		} else {	// State machine is not working.
			if (!(status & ME4600_AO_STATUS_BIT_EF)) {	// FIFO is empty. Procedure has started and finish already!
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
			    ME4600_AO_CTRL_BIT_STOP |
			    ME4600_AO_CTRL_BIT_IMMEDIATE_STOP |
			    ME4600_AO_CTRL_BIT_RESET_IRQ;
			ctrl &=
			    ~(ME4600_AO_CTRL_BIT_ENABLE_IRQ |
			      ME4600_AO_CTRL_BIT_ENABLE_EX_TRIG);
			outl(ctrl, instance->ctrl_reg);
			PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->ctrl_reg - instance->reg_base,
				   ctrl);
			spin_unlock_irqrestore(&instance->subdevice_lock,
					       cpu_flags);
			spin_lock(instance->preload_reg_lock);
			//Remove from synchronous start. Block triggering from this output.
			synch = inl(instance->preload_reg);
			synch &=
			    ~((ME4600_AO_SYNC_HOLD | ME4600_AO_SYNC_EXT_TRIG) <<
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
		if (!instance->fifo) {
			PERROR_CRITICAL
			    ("Streaming on single device! This feature is not implemented in this version!\n");
			instance->status = ao_status_stream_error;
			// Signal the end.
			signaling = 1;
			break;
		}

		if (!(status & ME4600_AO_STATUS_BIT_FSM)) {	// State machine is not working. This is an error.
			// BROKEN PIPE!
			if (!(status & ME4600_AO_STATUS_BIT_EF)) {	// FIFO is empty.
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
		if (!instance->fifo) {
			PERROR_CRITICAL
			    ("Streaming on single device! This feature is not implemented in this version!\n");
			instance->status = ao_status_stream_error;
			// Signal the end.
			signaling = 1;
			break;
		}

		if (!(status & ME4600_AO_STATUS_BIT_FSM)) {	// State machine is not working. Waiting for stop finish.
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
		queue_delayed_work(instance->me4600_workqueue,
				   &instance->ao_control_task, 1);
	} else {
		PINFO("<%s> Ending control task.\n", __func__);
	}

}
#else
/// @note SPECIAL BUILD FOR BOSCH
/// @author Guenter Gebhardt
static int me4600_ao_io_reset_subdevice(me_subdevice_t *subdevice,
					struct file *filep, int flags)
{
	me4600_ao_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	uint32_t tmp;
	unsigned long status;

	PDEBUG("executed.\n");

	instance = (me4600_ao_subdevice_t *) subdevice;

	ME_SUBDEVICE_ENTER spin_lock_irqsave(&instance->subdevice_lock, status);
	spin_lock(instance->preload_reg_lock);
	tmp = inl(instance->preload_reg);
	tmp &= ~(0x10001 << instance->ao_idx);
	outl(tmp, instance->preload_reg);
	*instance->preload_flags &= ~(0x1 << instance->ao_idx);
	spin_unlock(instance->preload_reg_lock);

	tmp = inl(instance->ctrl_reg);
	tmp |= ME4600_AO_CTRL_BIT_STOP | ME4600_AO_CTRL_BIT_IMMEDIATE_STOP;
	outl(tmp, instance->ctrl_reg);

	while (inl(instance->status_reg) & ME4600_AO_STATUS_BIT_FSM) ;

	outl(ME4600_AO_CTRL_BIT_STOP | ME4600_AO_CTRL_BIT_IMMEDIATE_STOP,
	     instance->ctrl_reg);

	outl(0x8000, instance->single_reg);

	instance->single_value = 0x8000;
	instance->circ_buf.head = 0;
	instance->circ_buf.tail = 0;

	spin_unlock_irqrestore(&instance->subdevice_lock, status);

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me4600_ao_io_single_config(me_subdevice_t *subdevice,
				      struct file *filep,
				      int channel,
				      int single_config,
				      int ref,
				      int trig_chan,
				      int trig_type, int trig_edge, int flags)
{
	me4600_ao_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	uint32_t tmp;
	unsigned long cpu_flags;

	PDEBUG("executed.\n");

	instance = (me4600_ao_subdevice_t *) subdevice;

	ME_SUBDEVICE_ENTER
	    spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);

	if (inl(instance->status_reg) & ME4600_AO_STATUS_BIT_FSM) {
		PERROR("Subdevice is busy.\n");
		err = ME_ERRNO_SUBDEVICE_BUSY;
		goto ERROR;
	}

	if (channel == 0) {
		if (single_config == 0) {
			if (ref == ME_REF_AO_GROUND) {
				if (trig_chan == ME_TRIG_CHAN_DEFAULT) {
					if (trig_type == ME_TRIG_TYPE_SW) {
						tmp = inl(instance->ctrl_reg);
						tmp |=
						    ME4600_AO_CTRL_BIT_IMMEDIATE_STOP;
						outl(tmp, instance->ctrl_reg);
						tmp =
						    ME4600_AO_CTRL_BIT_IMMEDIATE_STOP;
						outl(tmp, instance->ctrl_reg);

						spin_lock(instance->
							  preload_reg_lock);
						tmp =
						    inl(instance->preload_reg);
						tmp &=
						    ~(0x10001 << instance->
						      ao_idx);
						outl(tmp,
						     instance->preload_reg);
						*instance->preload_flags &=
						    ~(0x1 << instance->ao_idx);
						spin_unlock(instance->
							    preload_reg_lock);
					} else if (trig_type ==
						   ME_TRIG_TYPE_EXT_DIGITAL) {
						if (trig_edge ==
						    ME_TRIG_EDGE_RISING) {
							tmp =
							    inl(instance->
								ctrl_reg);
							tmp |=
							    ME4600_AO_CTRL_BIT_IMMEDIATE_STOP;
							outl(tmp,
							     instance->
							     ctrl_reg);
							tmp =
							    ME4600_AO_CTRL_BIT_IMMEDIATE_STOP
							    |
							    ME4600_AO_CTRL_BIT_ENABLE_EX_TRIG;
							outl(tmp,
							     instance->
							     ctrl_reg);
						} else if (trig_edge ==
							   ME_TRIG_EDGE_FALLING)
						{
							tmp =
							    inl(instance->
								ctrl_reg);
							tmp |=
							    ME4600_AO_CTRL_BIT_IMMEDIATE_STOP;
							outl(tmp,
							     instance->
							     ctrl_reg);
							tmp =
							    ME4600_AO_CTRL_BIT_IMMEDIATE_STOP
							    |
							    ME4600_AO_CTRL_BIT_ENABLE_EX_TRIG
							    |
							    ME4600_AO_CTRL_BIT_EX_TRIG_EDGE;
							outl(tmp,
							     instance->
							     ctrl_reg);
						} else if (trig_edge ==
							   ME_TRIG_EDGE_ANY) {
							tmp =
							    inl(instance->
								ctrl_reg);
							tmp |=
							    ME4600_AO_CTRL_BIT_IMMEDIATE_STOP;
							outl(tmp,
							     instance->
							     ctrl_reg);
							tmp =
							    ME4600_AO_CTRL_BIT_IMMEDIATE_STOP
							    |
							    ME4600_AO_CTRL_BIT_ENABLE_EX_TRIG
							    |
							    ME4600_AO_CTRL_BIT_EX_TRIG_EDGE
							    |
							    ME4600_AO_CTRL_BIT_EX_TRIG_EDGE_BOTH;
							outl(tmp,
							     instance->
							     ctrl_reg);
						} else {
							PERROR
							    ("Invalid trigger edge.\n");
							err =
							    ME_ERRNO_INVALID_TRIG_EDGE;
							goto ERROR;
						}

						spin_lock(instance->
							  preload_reg_lock);

						tmp =
						    inl(instance->preload_reg);
						tmp &=
						    ~(0x10001 << instance->
						      ao_idx);
						tmp |= 0x1 << instance->ao_idx;
						outl(tmp,
						     instance->preload_reg);
						*instance->preload_flags &=
						    ~(0x1 << instance->ao_idx);
						spin_unlock(instance->
							    preload_reg_lock);
					} else {
						PERROR
						    ("Invalid trigger type.\n");
						err =
						    ME_ERRNO_INVALID_TRIG_TYPE;
						goto ERROR;
					}
				} else if (trig_chan ==
					   ME_TRIG_CHAN_SYNCHRONOUS) {
					if (trig_type == ME_TRIG_TYPE_SW) {
						tmp = inl(instance->ctrl_reg);
						tmp |=
						    ME4600_AO_CTRL_BIT_IMMEDIATE_STOP;
						outl(tmp, instance->ctrl_reg);
						tmp =
						    ME4600_AO_CTRL_BIT_IMMEDIATE_STOP;
						outl(tmp, instance->ctrl_reg);

						spin_lock(instance->
							  preload_reg_lock);
						tmp =
						    inl(instance->preload_reg);
						tmp &=
						    ~(0x10001 << instance->
						      ao_idx);
						tmp |= 0x1 << instance->ao_idx;
						outl(tmp,
						     instance->preload_reg);
						*instance->preload_flags |=
						    0x1 << instance->ao_idx;
						spin_unlock(instance->
							    preload_reg_lock);
					} else if (trig_type ==
						   ME_TRIG_TYPE_EXT_DIGITAL) {
						if (trig_edge ==
						    ME_TRIG_EDGE_RISING) {
							tmp =
							    inl(instance->
								ctrl_reg);
							tmp |=
							    ME4600_AO_CTRL_BIT_IMMEDIATE_STOP;
							outl(tmp,
							     instance->
							     ctrl_reg);
							tmp =
							    ME4600_AO_CTRL_BIT_IMMEDIATE_STOP;
							outl(tmp,
							     instance->
							     ctrl_reg);
						} else if (trig_edge ==
							   ME_TRIG_EDGE_FALLING)
						{
							tmp =
							    inl(instance->
								ctrl_reg);
							tmp |=
							    ME4600_AO_CTRL_BIT_IMMEDIATE_STOP;
							outl(tmp,
							     instance->
							     ctrl_reg);
							tmp =
							    ME4600_AO_CTRL_BIT_IMMEDIATE_STOP
							    |
							    ME4600_AO_CTRL_BIT_EX_TRIG_EDGE;
							outl(tmp,
							     instance->
							     ctrl_reg);
						} else if (trig_edge ==
							   ME_TRIG_EDGE_ANY) {
							tmp =
							    inl(instance->
								ctrl_reg);
							tmp |=
							    ME4600_AO_CTRL_BIT_IMMEDIATE_STOP;
							outl(tmp,
							     instance->
							     ctrl_reg);
							tmp =
							    ME4600_AO_CTRL_BIT_IMMEDIATE_STOP
							    |
							    ME4600_AO_CTRL_BIT_EX_TRIG_EDGE
							    |
							    ME4600_AO_CTRL_BIT_EX_TRIG_EDGE_BOTH;
							outl(tmp,
							     instance->
							     ctrl_reg);
						} else {
							PERROR
							    ("Invalid trigger edge.\n");
							err =
							    ME_ERRNO_INVALID_TRIG_EDGE;
							goto ERROR;
						}

						spin_lock(instance->
							  preload_reg_lock);

						tmp =
						    inl(instance->preload_reg);
						tmp |=
						    0x10001 << instance->ao_idx;
						outl(tmp,
						     instance->preload_reg);
						*instance->preload_flags &=
						    ~(0x1 << instance->ao_idx);
						spin_unlock(instance->
							    preload_reg_lock);
					} else {
						PERROR
						    ("Invalid trigger type.\n");
						err =
						    ME_ERRNO_INVALID_TRIG_TYPE;
						goto ERROR;
					}
				} else {
					PERROR
					    ("Invalid trigger channel specified.\n");
					err = ME_ERRNO_INVALID_REF;
					goto ERROR;
				}
			} else {
				PERROR("Invalid analog reference specified.\n");
				err = ME_ERRNO_INVALID_REF;
				goto ERROR;
			}
		} else {
			PERROR("Invalid single config specified.\n");
			err = ME_ERRNO_INVALID_SINGLE_CONFIG;
			goto ERROR;
		}
	} else {
		PERROR("Invalid channel number specified.\n");
		err = ME_ERRNO_INVALID_CHANNEL;
		goto ERROR;
	}

ERROR:

	spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me4600_ao_io_single_read(me_subdevice_t *subdevice,
				    struct file *filep,
				    int channel,
				    int *value, int time_out, int flags)
{
	me4600_ao_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	unsigned long tmp;
	unsigned long cpu_flags;

	PDEBUG("executed.\n");

	instance = (me4600_ao_subdevice_t *) subdevice;

	if (channel != 0) {
		PERROR("Invalid channel number specified.\n");
		return ME_ERRNO_INVALID_CHANNEL;
	}

	ME_SUBDEVICE_ENTER
	    spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);
	tmp = inl(instance->ctrl_reg);

	if (tmp & 0x3) {
		PERROR("Not in single mode.\n");
		err = ME_ERRNO_PREVIOUS_CONFIG;
	} else {
		*value = instance->single_value;
	}

	spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me4600_ao_io_single_write(me_subdevice_t *subdevice,
				     struct file *filep,
				     int channel,
				     int value, int time_out, int flags)
{
	me4600_ao_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	unsigned long mask = 0;
	unsigned long tmp;
	unsigned long cpu_flags;
	int i;
	wait_queue_head_t queue;
	unsigned long j;
	unsigned long delay = 0;

	PDEBUG("executed.\n");

	init_waitqueue_head(&queue);

	instance = (me4600_ao_subdevice_t *) subdevice;

	if (channel != 0) {
		PERROR("Invalid channel number specified.\n");
		return ME_ERRNO_INVALID_CHANNEL;
	}

	if (time_out < 0) {
		PERROR("Invalid timeout specified.\n");
		return ME_ERRNO_INVALID_TIMEOUT;
	}

	if (time_out) {
		delay = (time_out * HZ) / 1000;

		if (delay == 0)
			delay = 1;
	}

	ME_SUBDEVICE_ENTER
	    spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);

	tmp = inl(instance->ctrl_reg);

	if (tmp & 0x3) {
		spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);
		PERROR("Not in single mode.\n");
		err = ME_ERRNO_PREVIOUS_CONFIG;
		goto ERROR;
	}

	if (tmp & ME4600_AO_CTRL_BIT_ENABLE_EX_TRIG) {
		outl(value, instance->single_reg);
		instance->single_value = value;
		spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);

		if (!(flags & ME_IO_SINGLE_TYPE_WRITE_NONBLOCKING)) {
			j = jiffies;

			while (inl(instance->status_reg) &
			       ME4600_AO_STATUS_BIT_FSM) {
				interruptible_sleep_on_timeout(&queue, 1);

				if (signal_pending(current)) {
					PERROR
					    ("Wait on external trigger interrupted by signal.\n");
					err = ME_ERRNO_SIGNAL;
					goto ERROR;
				}

				if (delay && ((jiffies - j) > delay)) {
					PERROR("Timeout reached.\n");
					err = ME_ERRNO_TIMEOUT;
					goto ERROR;
				}
			}
		}
	} else if ((inl(instance->preload_reg) & (0x10001 << instance->ao_idx))
		   == (0x10001 << instance->ao_idx)) {
		if (flags & ME_IO_SINGLE_TYPE_TRIG_SYNCHRONOUS) {
			tmp |= ME4600_AO_CTRL_BIT_ENABLE_EX_TRIG;
			outl(tmp, instance->ctrl_reg);
			outl(value, instance->single_reg);
			instance->single_value = value;
			spin_unlock_irqrestore(&instance->subdevice_lock,
					       cpu_flags);

			if (!(flags & ME_IO_SINGLE_TYPE_WRITE_NONBLOCKING)) {
				j = jiffies;

				while (inl(instance->status_reg) &
				       ME4600_AO_STATUS_BIT_FSM) {
					interruptible_sleep_on_timeout(&queue,
								       1);

					if (signal_pending(current)) {
						PERROR
						    ("Wait on external trigger interrupted by signal.\n");
						err = ME_ERRNO_SIGNAL;
						goto ERROR;
					}

					if (delay && ((jiffies - j) > delay)) {
						PERROR("Timeout reached.\n");
						err = ME_ERRNO_TIMEOUT;
						goto ERROR;
					}
				}
			}
		} else {
			outl(value, instance->single_reg);
			instance->single_value = value;
			spin_unlock_irqrestore(&instance->subdevice_lock,
					       cpu_flags);
		}
	} else if ((inl(instance->preload_reg) & (0x10001 << instance->ao_idx))
		   == (0x1 << instance->ao_idx)) {
		outl(value, instance->single_reg);
		instance->single_value = value;

		PDEBUG("Synchronous SW, flags = 0x%X.\n", flags);

		if (flags & ME_IO_SINGLE_TYPE_TRIG_SYNCHRONOUS) {
			PDEBUG("Trigger synchronous SW.\n");
			spin_lock(instance->preload_reg_lock);
			tmp = inl(instance->preload_reg);

			for (i = 0; i < ME4600_AO_MAX_SUBDEVICES; i++) {
				if ((*instance->preload_flags & (0x1 << i))) {
					if ((tmp & (0x10001 << i)) ==
					    (0x1 << i)) {
						mask |= 0x1 << i;
					}
				}
			}

			tmp &= ~(mask);

			outl(tmp, instance->preload_reg);
			spin_unlock(instance->preload_reg_lock);
		}

		spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);
	} else {
		outl(value, instance->single_reg);
		instance->single_value = value;
		spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);
	}

ERROR:

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me4600_ao_io_stream_config(me_subdevice_t *subdevice,
				      struct file *filep,
				      meIOStreamConfig_t *config_list,
				      int count,
				      meIOStreamTrigger_t *trigger,
				      int fifo_irq_threshold, int flags)
{
	me4600_ao_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	unsigned long ctrl;
	unsigned long tmp;
	unsigned long cpu_flags;
	uint64_t conv_ticks;
	unsigned int conv_start_ticks_low = trigger->iConvStartTicksLow;
	unsigned int conv_start_ticks_high = trigger->iConvStartTicksHigh;

	PDEBUG("executed.\n");

	instance = (me4600_ao_subdevice_t *) subdevice;

	conv_ticks =
	    (uint64_t) conv_start_ticks_low +
	    ((uint64_t) conv_start_ticks_high << 32);

	if (!instance->fifo) {
		PERROR("Not a streaming ao.\n");
		return ME_ERRNO_NOT_SUPPORTED;
	}

	ME_SUBDEVICE_ENTER
	    spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);

	if ((inl(instance->status_reg)) & ME4600_AO_STATUS_BIT_FSM) {
		PERROR("Subdevice is busy.\n");
		err = ME_ERRNO_SUBDEVICE_BUSY;
		goto ERROR;
	}

	ctrl = inl(instance->ctrl_reg);
	ctrl |= ME4600_AO_CTRL_BIT_IMMEDIATE_STOP;
	outl(ctrl, instance->ctrl_reg);
	ctrl = ME4600_AO_CTRL_BIT_IMMEDIATE_STOP;
	outl(ctrl, instance->ctrl_reg);

	if (count != 1) {
		PERROR("Invalid stream configuration list count specified.\n");
		err = ME_ERRNO_INVALID_CONFIG_LIST_COUNT;
		goto ERROR;
	}

	if (config_list[0].iChannel != 0) {
		PERROR("Invalid channel number specified.\n");
		err = ME_ERRNO_INVALID_CHANNEL;
		goto ERROR;
	}

	if (config_list[0].iStreamConfig != 0) {
		PERROR("Invalid stream config specified.\n");
		err = ME_ERRNO_INVALID_STREAM_CONFIG;
		goto ERROR;
	}

	if (config_list[0].iRef != ME_REF_AO_GROUND) {
		PERROR("Invalid analog reference.\n");
		err = ME_ERRNO_INVALID_REF;
		goto ERROR;
	}

	if ((trigger->iAcqStartTicksLow != 0)
	    || (trigger->iAcqStartTicksHigh != 0)) {
		PERROR
		    ("Invalid acquisition start trigger argument specified.\n");
		err = ME_ERRNO_INVALID_ACQ_START_ARG;
		goto ERROR;
	}

	switch (trigger->iAcqStartTrigType) {

	case ME_TRIG_TYPE_SW:
		break;

	case ME_TRIG_TYPE_EXT_DIGITAL:
		ctrl |= ME4600_AO_CTRL_BIT_ENABLE_EX_TRIG;

		switch (trigger->iAcqStartTrigEdge) {

		case ME_TRIG_EDGE_RISING:
			break;

		case ME_TRIG_EDGE_FALLING:
			ctrl |= ME4600_AO_CTRL_BIT_EX_TRIG_EDGE;

			break;

		case ME_TRIG_EDGE_ANY:
			ctrl |=
			    ME4600_AO_CTRL_BIT_EX_TRIG_EDGE |
			    ME4600_AO_CTRL_BIT_EX_TRIG_EDGE_BOTH;

			break;

		default:
			PERROR
			    ("Invalid acquisition start trigger edge specified.\n");

			err = ME_ERRNO_INVALID_ACQ_START_TRIG_EDGE;

			goto ERROR;

			break;
		}

		break;

	default:
		PERROR("Invalid acquisition start trigger type specified.\n");

		err = ME_ERRNO_INVALID_ACQ_START_TRIG_TYPE;

		goto ERROR;

		break;
	}

	switch (trigger->iScanStartTrigType) {

	case ME_TRIG_TYPE_FOLLOW:
		break;

	default:
		PERROR("Invalid scan start trigger type specified.\n");

		err = ME_ERRNO_INVALID_SCAN_START_TRIG_TYPE;

		goto ERROR;

		break;
	}

	switch (trigger->iConvStartTrigType) {

	case ME_TRIG_TYPE_TIMER:
		if ((conv_ticks < ME4600_AO_MIN_CHAN_TICKS)
		    || (conv_ticks > ME4600_AO_MAX_CHAN_TICKS)) {
			PERROR
			    ("Invalid conv start trigger argument specified.\n");
			err = ME_ERRNO_INVALID_CONV_START_ARG;
			goto ERROR;
		}

		break;

	default:
		PERROR("Invalid conv start trigger type specified.\n");

		err = ME_ERRNO_INVALID_CONV_START_TRIG_TYPE;

		goto ERROR;

		break;
	}

	/* Preset to hardware wraparound mode */
	instance->flags &= ~(ME4600_AO_FLAGS_SW_WRAP_MODE_MASK);

	switch (trigger->iScanStopTrigType) {

	case ME_TRIG_TYPE_NONE:
		if (flags & ME_IO_STREAM_CONFIG_WRAPAROUND) {
			/* Set flags to indicate usage of software mode. */
			instance->flags |= ME4600_AO_FLAGS_SW_WRAP_MODE_INF;
			instance->wrap_count = 0;
			instance->wrap_remaining = 0;
		}

		break;

	case ME_TRIG_TYPE_COUNT:
		if (flags & ME_IO_STREAM_CONFIG_WRAPAROUND) {
			if (trigger->iScanStopCount <= 0) {
				PERROR("Invalid scan stop count specified.\n");
				err = ME_ERRNO_INVALID_SCAN_STOP_ARG;
				goto ERROR;
			}

			/* Set flags to indicate usage of software mode. */
			instance->flags |= ME4600_AO_FLAGS_SW_WRAP_MODE_FIN;
			instance->wrap_count = trigger->iScanStopCount;
			instance->wrap_remaining = trigger->iScanStopCount;
		} else {
			PERROR("Invalid scan stop trigger type specified.\n");
			err = ME_ERRNO_INVALID_ACQ_STOP_TRIG_TYPE;
			goto ERROR;
		}

		break;

	default:
		PERROR("Invalid scan stop trigger type specified.\n");

		err = ME_ERRNO_INVALID_SCAN_STOP_TRIG_TYPE;

		goto ERROR;

		break;
	}

	switch (trigger->iAcqStopTrigType) {

	case ME_TRIG_TYPE_NONE:
		break;

	case ME_TRIG_TYPE_COUNT:
		if (trigger->iScanStopTrigType != ME_TRIG_TYPE_NONE) {
			PERROR("Invalid acq stop trigger type specified.\n");
			err = ME_ERRNO_INVALID_ACQ_STOP_TRIG_TYPE;
			goto ERROR;
		}

		if (flags & ME_IO_STREAM_CONFIG_WRAPAROUND) {
			if (trigger->iAcqStopCount <= 0) {
				PERROR("Invalid acq stop count specified.\n");
				err = ME_ERRNO_INVALID_ACQ_STOP_ARG;
				goto ERROR;
			}

			/* Set flags to indicate usage of software mode. */
			instance->flags |= ME4600_AO_FLAGS_SW_WRAP_MODE_FIN;
			instance->wrap_count = trigger->iAcqStopCount;
			instance->wrap_remaining = trigger->iAcqStopCount;
		} else {
			PERROR("Invalid acp stop trigger type specified.\n");
			err = ME_ERRNO_INVALID_ACQ_STOP_TRIG_TYPE;
			goto ERROR;
		}

		break;

	default:
		PERROR("Invalid acq stop trigger type specified.\n");
		err = ME_ERRNO_INVALID_ACQ_STOP_TRIG_TYPE;
		goto ERROR;
		break;
	}

	switch (trigger->iAcqStartTrigChan) {

	case ME_TRIG_CHAN_DEFAULT:
		spin_lock(instance->preload_reg_lock);
		tmp = inl(instance->preload_reg);
		tmp &= ~(0x10001 << instance->ao_idx);
		outl(tmp, instance->preload_reg);
		spin_unlock(instance->preload_reg_lock);

		break;

	case ME_TRIG_CHAN_SYNCHRONOUS:
		if (trigger->iAcqStartTrigType == ME_TRIG_TYPE_SW) {
			spin_lock(instance->preload_reg_lock);
			tmp = inl(instance->preload_reg);
			tmp &= ~(0x10001 << instance->ao_idx);
			outl(tmp, instance->preload_reg);
			tmp |= 0x1 << instance->ao_idx;
			outl(tmp, instance->preload_reg);
			spin_unlock(instance->preload_reg_lock);
		} else {
			ctrl &= ~(ME4600_AO_CTRL_BIT_ENABLE_EX_TRIG);
			spin_lock(instance->preload_reg_lock);
			tmp = inl(instance->preload_reg);
			tmp &= ~(0x10001 << instance->ao_idx);
			outl(tmp, instance->preload_reg);
			tmp |= 0x10000 << instance->ao_idx;
			outl(tmp, instance->preload_reg);
			spin_unlock(instance->preload_reg_lock);
		}

		break;

	default:
		PERROR("Invalid acq start trigger channel specified.\n");
		err = ME_ERRNO_INVALID_ACQ_START_TRIG_CHAN;
		goto ERROR;

		break;
	}

	outl(conv_ticks - 2, instance->timer_reg);

	if (flags & ME_IO_STREAM_CONFIG_BIT_PATTERN) {
		if (instance->ao_idx == 3) {
			ctrl |= ME4600_AO_CTRL_BIT_ENABLE_DO;
		} else {
			err = ME_ERRNO_INVALID_FLAGS;
			goto ERROR;
		}
	} else {
		if (instance->ao_idx == 3) {
			ctrl &= ~ME4600_AO_CTRL_BIT_ENABLE_DO;
		}
	}

	/* Set hardware mode. */
	if (flags & ME_IO_STREAM_CONFIG_WRAPAROUND) {
		ctrl |= ME4600_AO_CTRL_BIT_MODE_0;
	} else {
		ctrl |= ME4600_AO_CTRL_BIT_MODE_1;
	}

	PDEBUG("Preload word = 0x%X.\n", inl(instance->preload_reg));

	PDEBUG("Ctrl word = 0x%lX.\n", ctrl);
	outl(ctrl, instance->ctrl_reg);	// Write the control word

ERROR:

	spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me4600_ao_io_stream_new_values(me_subdevice_t *subdevice,
					  struct file *filep,
					  int time_out, int *count, int flags)
{
	me4600_ao_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	long t = 0;
	long j;

	PDEBUG("executed.\n");

	instance = (me4600_ao_subdevice_t *) subdevice;

	if (!instance->fifo) {
		PERROR("Not a streaming ao.\n");
		return ME_ERRNO_NOT_SUPPORTED;
	}

	if (time_out < 0) {
		PERROR("Invalid time_out specified.\n");
		return ME_ERRNO_INVALID_TIMEOUT;
	}

	if (time_out) {
		t = (time_out * HZ) / 1000;

		if (t == 0)
			t = 1;
	}

	*count = 0;

	ME_SUBDEVICE_ENTER;

	if (t) {
		j = jiffies;
		wait_event_interruptible_timeout(instance->wait_queue,
						 ((me_circ_buf_space
						   (&instance->circ_buf))
						  || !(inl(instance->status_reg)
						       &
						       ME4600_AO_STATUS_BIT_FSM)),
						 t);

		if (!(inl(instance->status_reg) & ME4600_AO_STATUS_BIT_FSM)) {
			PERROR("AO subdevice is not running.\n");
			err = ME_ERRNO_SUBDEVICE_NOT_RUNNING;
		} else if (signal_pending(current)) {
			PERROR("Wait on values interrupted from signal.\n");
			err = ME_ERRNO_SIGNAL;
		} else if ((jiffies - j) >= t) {
			PERROR("Wait on values timed out.\n");
			err = ME_ERRNO_TIMEOUT;
		} else {
			*count = me_circ_buf_space(&instance->circ_buf);
		}
	} else {
		wait_event_interruptible(instance->wait_queue,
					 ((me_circ_buf_space
					   (&instance->circ_buf))
					  || !(inl(instance->status_reg) &
					       ME4600_AO_STATUS_BIT_FSM)));

		if (!(inl(instance->status_reg) & ME4600_AO_STATUS_BIT_FSM)) {
			PERROR("AO subdevice is not running.\n");
			err = ME_ERRNO_SUBDEVICE_NOT_RUNNING;
		} else if (signal_pending(current)) {
			PERROR("Wait on values interrupted from signal.\n");
			err = ME_ERRNO_SIGNAL;
		} else {
			*count = me_circ_buf_space(&instance->circ_buf);
		}
	}

	ME_SUBDEVICE_EXIT;

	return err;
}

static void stop_immediately(me4600_ao_subdevice_t *instance)
{
	unsigned long cpu_flags;
	uint32_t tmp;

	spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);
	tmp = inl(instance->ctrl_reg);
	tmp |= ME4600_AO_CTRL_BIT_STOP | ME4600_AO_CTRL_BIT_IMMEDIATE_STOP;
	outl(tmp, instance->ctrl_reg);

	while (inl(instance->status_reg) & ME4600_AO_STATUS_BIT_FSM) ;

	spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);
}

static int me4600_ao_io_stream_start(me_subdevice_t *subdevice,
				     struct file *filep,
				     int start_mode, int time_out, int flags)
{
	me4600_ao_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	unsigned long cpu_flags = 0;
	unsigned long ref;
	unsigned long tmp;
	unsigned long delay = 0;
	wait_queue_head_t queue;

	PDEBUG("executed.\n");

	instance = (me4600_ao_subdevice_t *) subdevice;

	init_waitqueue_head(&queue);

	if (time_out < 0) {
		PERROR("Invalid timeout specified.\n");
		return ME_ERRNO_INVALID_TIMEOUT;
	}

	if (time_out) {
		delay = (time_out * HZ) / 1000;

		if (delay == 0)
			delay = 1;
	}

	if (!instance->fifo) {
		PERROR("Not a streaming ao.\n");
		return ME_ERRNO_NOT_SUPPORTED;
	}

	ME_SUBDEVICE_ENTER
	    spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);

	tmp = inl(instance->ctrl_reg);

	switch (tmp & (ME4600_AO_CTRL_MASK_MODE)) {

	case 0:		// Single mode
		spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);
		PERROR("Subdevice is configured in single mode.\n");
		err = ME_ERRNO_PREVIOUS_CONFIG;
		goto ERROR;

	case 1:		// Wraparound mode
		if (tmp & ME4600_AO_CTRL_BIT_ENABLE_EX_TRIG) {	// Normal wraparound with external trigger

			if ((inl(instance->status_reg) &
			     ME4600_AO_STATUS_BIT_FSM)) {
				spin_unlock_irqrestore(&instance->
						       subdevice_lock,
						       cpu_flags);
				PERROR("Conversion is already running.\n");
				err = ME_ERRNO_SUBDEVICE_BUSY;
				goto ERROR;
			}

			tmp &=
			    ~(ME4600_AO_CTRL_BIT_ENABLE_IRQ |
			      ME4600_AO_CTRL_BIT_STOP |
			      ME4600_AO_CTRL_BIT_IMMEDIATE_STOP);

			outl(tmp, instance->ctrl_reg);
			spin_unlock_irqrestore(&instance->subdevice_lock,
					       cpu_flags);

			if (start_mode == ME_START_MODE_BLOCKING) {
				init_waitqueue_head(&queue);

				if (delay) {
					ref = jiffies;

					while (!
					       (inl(instance->status_reg) &
						ME4600_AO_STATUS_BIT_FSM)) {
						interruptible_sleep_on_timeout
						    (&queue, 1);

						if (signal_pending(current)) {
							PERROR
							    ("Wait on start of state machine interrupted.\n");
							stop_immediately
							    (instance);
							err = ME_ERRNO_SIGNAL;
							goto ERROR;
						}

						if (((jiffies - ref) >= delay)) {
							PERROR
							    ("Timeout reached.\n");
							stop_immediately
							    (instance);
							err = ME_ERRNO_TIMEOUT;
							goto ERROR;
						}
					}
				} else {
					while (!
					       (inl(instance->status_reg) &
						ME4600_AO_STATUS_BIT_FSM)) {
						interruptible_sleep_on_timeout
						    (&queue, 1);

						if (signal_pending(current)) {
							PERROR
							    ("Wait on start of state machine interrupted.\n");
							stop_immediately
							    (instance);
							err = ME_ERRNO_SIGNAL;
							goto ERROR;
						}
					}
				}
			} else if (start_mode == ME_START_MODE_NONBLOCKING) {
			} else {
				PERROR("Invalid start mode specified.\n");
				err = ME_ERRNO_INVALID_START_MODE;
				goto ERROR;
			}
		} else if ((inl(instance->preload_reg) & (0x10001 << instance->ao_idx)) == (0x10000 << instance->ao_idx)) {	// Synchronous with external trigger

			if ((inl(instance->status_reg) &
			     ME4600_AO_STATUS_BIT_FSM)) {
				spin_unlock_irqrestore(&instance->
						       subdevice_lock,
						       cpu_flags);
				PERROR("Conversion is already running.\n");
				err = ME_ERRNO_SUBDEVICE_BUSY;
				goto ERROR;
			}

			if (flags & ME_IO_STREAM_START_TYPE_TRIG_SYNCHRONOUS) {
				tmp |= ME4600_AO_CTRL_BIT_ENABLE_EX_TRIG;
				tmp &=
				    ~(ME4600_AO_CTRL_BIT_ENABLE_IRQ |
				      ME4600_AO_CTRL_BIT_STOP |
				      ME4600_AO_CTRL_BIT_IMMEDIATE_STOP);
				outl(tmp, instance->ctrl_reg);
				spin_unlock_irqrestore(&instance->
						       subdevice_lock,
						       cpu_flags);

				if (start_mode == ME_START_MODE_BLOCKING) {
					init_waitqueue_head(&queue);

					if (delay) {
						ref = jiffies;

						while (!
						       (inl
							(instance->
							 status_reg) &
							ME4600_AO_STATUS_BIT_FSM))
						{
							interruptible_sleep_on_timeout
							    (&queue, 1);

							if (signal_pending
							    (current)) {
								PERROR
								    ("Wait on start of state machine interrupted.\n");
								stop_immediately
								    (instance);
								err =
								    ME_ERRNO_SIGNAL;
								goto ERROR;
							}

							if (((jiffies - ref) >=
							     delay)) {
								PERROR
								    ("Timeout reached.\n");
								stop_immediately
								    (instance);
								err =
								    ME_ERRNO_TIMEOUT;
								goto ERROR;
							}
						}
					} else {
						while (!
						       (inl
							(instance->
							 status_reg) &
							ME4600_AO_STATUS_BIT_FSM))
						{
							interruptible_sleep_on_timeout
							    (&queue, 1);

							if (signal_pending
							    (current)) {
								PERROR
								    ("Wait on start of state machine interrupted.\n");
								stop_immediately
								    (instance);
								err =
								    ME_ERRNO_SIGNAL;
								goto ERROR;
							}
						}
					}
				} else if (start_mode ==
					   ME_START_MODE_NONBLOCKING) {
				} else {
					PERROR
					    ("Invalid start mode specified.\n");
					err = ME_ERRNO_INVALID_START_MODE;
					goto ERROR;
				}
			} else {
				tmp &=
				    ~(ME4600_AO_CTRL_BIT_ENABLE_IRQ |
				      ME4600_AO_CTRL_BIT_STOP |
				      ME4600_AO_CTRL_BIT_IMMEDIATE_STOP);
				outl(tmp, instance->ctrl_reg);
				spin_unlock_irqrestore(&instance->
						       subdevice_lock,
						       cpu_flags);
			}
		} else if ((inl(instance->preload_reg) & (0x10001 << instance->ao_idx)) == (0x1 << instance->ao_idx)) {	// Synchronous wraparound with sw trigger

			if ((inl(instance->status_reg) &
			     ME4600_AO_STATUS_BIT_FSM)) {
				spin_unlock_irqrestore(&instance->
						       subdevice_lock,
						       cpu_flags);
				PERROR("Conversion is already running.\n");
				err = ME_ERRNO_SUBDEVICE_BUSY;
				goto ERROR;
			}

			tmp &=
			    ~(ME4600_AO_CTRL_BIT_ENABLE_IRQ |
			      ME4600_AO_CTRL_BIT_STOP |
			      ME4600_AO_CTRL_BIT_IMMEDIATE_STOP);

			outl(tmp, instance->ctrl_reg);

			if (flags & ME_IO_STREAM_START_TYPE_TRIG_SYNCHRONOUS) {
				outl(0x8000, instance->single_reg);
				instance->single_value = 0x8000;
			}

			spin_unlock_irqrestore(&instance->subdevice_lock,
					       cpu_flags);
		} else {	// Software start

			if ((inl(instance->status_reg) &
			     ME4600_AO_STATUS_BIT_FSM)) {
				spin_unlock_irqrestore(&instance->
						       subdevice_lock,
						       cpu_flags);
				PERROR("Conversion is already running.\n");
				err = ME_ERRNO_SUBDEVICE_BUSY;
				goto ERROR;
			}

			tmp &=
			    ~(ME4600_AO_CTRL_BIT_ENABLE_IRQ |
			      ME4600_AO_CTRL_BIT_STOP |
			      ME4600_AO_CTRL_BIT_IMMEDIATE_STOP);

			outl(tmp, instance->ctrl_reg);

			outl(0x8000, instance->single_reg);
			instance->single_value = 0x8000;

			spin_unlock_irqrestore(&instance->subdevice_lock,
					       cpu_flags);
		}

		break;

	case 2:		// Continuous mode
		if (tmp & ME4600_AO_CTRL_BIT_ENABLE_EX_TRIG) {	// Externally triggered

			if ((inl(instance->status_reg) &
			     ME4600_AO_STATUS_BIT_FSM)) {
				spin_unlock_irqrestore(&instance->
						       subdevice_lock,
						       cpu_flags);
				PERROR("Conversion is already running.\n");
				err = ME_ERRNO_SUBDEVICE_BUSY;
				goto ERROR;
			}

			tmp &=
			    ~(ME4600_AO_CTRL_BIT_STOP |
			      ME4600_AO_CTRL_BIT_IMMEDIATE_STOP);
			tmp |= ME4600_AO_CTRL_BIT_ENABLE_IRQ;
			outl(tmp, instance->ctrl_reg);
			instance->wrap_remaining = instance->wrap_count;
			instance->circ_buf.tail = 0;
			spin_unlock_irqrestore(&instance->subdevice_lock,
					       cpu_flags);

			if (start_mode == ME_START_MODE_BLOCKING) {
				init_waitqueue_head(&queue);

				if (delay) {
					ref = jiffies;

					while (!
					       (inl(instance->status_reg) &
						ME4600_AO_STATUS_BIT_FSM)) {
						interruptible_sleep_on_timeout
						    (&queue, 1);

						if (signal_pending(current)) {
							PERROR
							    ("Wait on start of state machine interrupted.\n");
							stop_immediately
							    (instance);
							err = ME_ERRNO_SIGNAL;
							goto ERROR;
						}

						if (((jiffies - ref) >= delay)) {
							PERROR
							    ("Timeout reached.\n");
							stop_immediately
							    (instance);
							err = ME_ERRNO_TIMEOUT;
							goto ERROR;
						}
					}
				} else {
					while (!
					       (inl(instance->status_reg) &
						ME4600_AO_STATUS_BIT_FSM)) {
						interruptible_sleep_on_timeout
						    (&queue, 1);

						if (signal_pending(current)) {
							PERROR
							    ("Wait on start of state machine interrupted.\n");
							stop_immediately
							    (instance);
							err = ME_ERRNO_SIGNAL;
							goto ERROR;
						}
					}
				}
			} else if (start_mode == ME_START_MODE_NONBLOCKING) {
				/* Do nothing */
			} else {
				PERROR("Invalid start mode specified.\n");
				stop_immediately(instance);
				err = ME_ERRNO_INVALID_START_MODE;
				goto ERROR;
			}
		} else if ((inl(instance->preload_reg) & (0x10001 << instance->ao_idx)) == (0x10000 << instance->ao_idx)) {	// Synchronous with external trigger

			if ((inl(instance->status_reg) &
			     ME4600_AO_STATUS_BIT_FSM)) {
				spin_unlock_irqrestore(&instance->
						       subdevice_lock,
						       cpu_flags);
				PERROR("Conversion is already running.\n");
				err = ME_ERRNO_SUBDEVICE_BUSY;
				goto ERROR;
			}

			if (flags & ME_IO_STREAM_START_TYPE_TRIG_SYNCHRONOUS) {
				tmp |=
				    ME4600_AO_CTRL_BIT_ENABLE_EX_TRIG |
				    ME4600_AO_CTRL_BIT_ENABLE_IRQ;
				tmp &=
				    ~(ME4600_AO_CTRL_BIT_STOP |
				      ME4600_AO_CTRL_BIT_IMMEDIATE_STOP);
				outl(tmp, instance->ctrl_reg);
				instance->wrap_remaining = instance->wrap_count;
				instance->circ_buf.tail = 0;

				spin_unlock_irqrestore(&instance->
						       subdevice_lock,
						       cpu_flags);

				if (start_mode == ME_START_MODE_BLOCKING) {
					init_waitqueue_head(&queue);

					if (delay) {
						ref = jiffies;

						while (!
						       (inl
							(instance->
							 status_reg) &
							ME4600_AO_STATUS_BIT_FSM))
						{
							interruptible_sleep_on_timeout
							    (&queue, 1);

							if (signal_pending
							    (current)) {
								PERROR
								    ("Wait on start of state machine interrupted.\n");
								stop_immediately
								    (instance);
								err =
								    ME_ERRNO_SIGNAL;
								goto ERROR;
							}

							if (((jiffies - ref) >=
							     delay)) {
								PERROR
								    ("Timeout reached.\n");
								stop_immediately
								    (instance);
								err =
								    ME_ERRNO_TIMEOUT;
								goto ERROR;
							}
						}
					} else {
						while (!
						       (inl
							(instance->
							 status_reg) &
							ME4600_AO_STATUS_BIT_FSM))
						{
							interruptible_sleep_on_timeout
							    (&queue, 1);

							if (signal_pending
							    (current)) {
								PERROR
								    ("Wait on start of state machine interrupted.\n");
								stop_immediately
								    (instance);
								err =
								    ME_ERRNO_SIGNAL;
								goto ERROR;
							}
						}
					}
				} else if (start_mode ==
					   ME_START_MODE_NONBLOCKING) {
				} else {
					PERROR
					    ("Invalid start mode specified.\n");
					stop_immediately(instance);
					err = ME_ERRNO_INVALID_START_MODE;
					goto ERROR;
				}
			} else {
				tmp |= ME4600_AO_CTRL_BIT_ENABLE_IRQ;
				tmp &=
				    ~(ME4600_AO_CTRL_BIT_STOP |
				      ME4600_AO_CTRL_BIT_IMMEDIATE_STOP);
				outl(tmp, instance->ctrl_reg);
				instance->wrap_remaining = instance->wrap_count;
				instance->circ_buf.tail = 0;
				spin_unlock_irqrestore(&instance->
						       subdevice_lock,
						       cpu_flags);
			}
		} else if ((inl(instance->preload_reg) & (0x10001 << instance->ao_idx)) == (0x1 << instance->ao_idx)) {	// Synchronous wraparound with sw trigger

			if ((inl(instance->status_reg) &
			     ME4600_AO_STATUS_BIT_FSM)) {
				spin_unlock_irqrestore(&instance->
						       subdevice_lock,
						       cpu_flags);
				PERROR("Conversion is already running.\n");
				err = ME_ERRNO_SUBDEVICE_BUSY;
				goto ERROR;
			}

			tmp &=
			    ~(ME4600_AO_CTRL_BIT_STOP |
			      ME4600_AO_CTRL_BIT_IMMEDIATE_STOP);
			tmp |= ME4600_AO_CTRL_BIT_ENABLE_IRQ;
			instance->wrap_remaining = instance->wrap_count;
			instance->circ_buf.tail = 0;
			PDEBUG("CTRL Reg = 0x%X.\n", inl(instance->ctrl_reg));
			outl(tmp, instance->ctrl_reg);

			if (flags & ME_IO_STREAM_START_TYPE_TRIG_SYNCHRONOUS) {
				outl(0x8000, instance->single_reg);
				instance->single_value = 0x8000;
			}

			spin_unlock_irqrestore(&instance->subdevice_lock,
					       cpu_flags);
		} else {	// Software start

			if ((inl(instance->status_reg) &
			     ME4600_AO_STATUS_BIT_FSM)) {
				spin_unlock_irqrestore(&instance->
						       subdevice_lock,
						       cpu_flags);
				PERROR("Conversion is already running.\n");
				err = ME_ERRNO_SUBDEVICE_BUSY;
				goto ERROR;
			}

			tmp &=
			    ~(ME4600_AO_CTRL_BIT_STOP |
			      ME4600_AO_CTRL_BIT_IMMEDIATE_STOP);

			tmp |= ME4600_AO_CTRL_BIT_ENABLE_IRQ;
			outl(tmp, instance->ctrl_reg);
			outl(0x8000, instance->single_reg);
			instance->single_value = 0x8000;
			instance->wrap_remaining = instance->wrap_count;
			instance->circ_buf.tail = 0;
			spin_unlock_irqrestore(&instance->subdevice_lock,
					       cpu_flags);
		}

		break;

	default:
		spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);
		PERROR("Invalid mode configured.\n");
		err = ME_ERRNO_INTERNAL;
		goto ERROR;
	}

ERROR:

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me4600_ao_io_stream_status(me_subdevice_t *subdevice,
				      struct file *filep,
				      int wait,
				      int *status, int *values, int flags)
{
	me4600_ao_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	wait_queue_head_t queue;

	PDEBUG("executed.\n");

	instance = (me4600_ao_subdevice_t *) subdevice;

	init_waitqueue_head(&queue);

	if (!instance->fifo) {
		PERROR("Not a streaming ao.\n");
		return ME_ERRNO_NOT_SUPPORTED;
	}

	ME_SUBDEVICE_ENTER;

	if (wait == ME_WAIT_NONE) {
		*status =
		    (inl(instance->status_reg) & ME4600_AO_STATUS_BIT_FSM) ?
		    ME_STATUS_BUSY : ME_STATUS_IDLE;
		*values = me_circ_buf_space(&instance->circ_buf);
	} else if (wait == ME_WAIT_IDLE) {
		while (inl(instance->status_reg) & ME4600_AO_STATUS_BIT_FSM) {
			interruptible_sleep_on_timeout(&queue, 1);

			if (instance->flags & ME4600_AO_FLAGS_BROKEN_PIPE) {
				PERROR("Output stream was interrupted.\n");
				*status = ME_STATUS_ERROR;
				err = ME_ERRNO_SUCCESS;
				goto ERROR;
			}

			if (signal_pending(current)) {
				PERROR
				    ("Wait on state machine interrupted by signal.\n");
				*status = ME_STATUS_INVALID;
				err = ME_ERRNO_SIGNAL;
				goto ERROR;
			}
		}

		*status = ME_STATUS_IDLE;

		*values = me_circ_buf_space(&instance->circ_buf);
	} else {
		PERROR("Invalid wait argument specified.\n");
		*status = ME_STATUS_INVALID;
		err = ME_ERRNO_INVALID_WAIT;
		goto ERROR;
	}

ERROR:

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me4600_ao_io_stream_stop(me_subdevice_t *subdevice,
				    struct file *filep,
				    int stop_mode, int flags)
{
	int err = ME_ERRNO_SUCCESS;
	me4600_ao_subdevice_t *instance;
	unsigned long cpu_flags;
	unsigned long tmp;

	PDEBUG("executed.\n");

	instance = (me4600_ao_subdevice_t *) subdevice;

	if (!instance->fifo) {
		PERROR("Not a streaming ao.\n");
		return ME_ERRNO_NOT_SUPPORTED;
	}

	ME_SUBDEVICE_ENTER;

	if (stop_mode == ME_STOP_MODE_IMMEDIATE) {
		spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);
		tmp = inl(instance->ctrl_reg);
		tmp |=
		    ME4600_AO_CTRL_BIT_STOP | ME4600_AO_CTRL_BIT_IMMEDIATE_STOP;
		outl(tmp, instance->ctrl_reg);

		while (inl(instance->status_reg) & ME4600_AO_STATUS_BIT_FSM) ;

		spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);
	} else if (stop_mode == ME_STOP_MODE_LAST_VALUE) {
		spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);
		tmp = inl(instance->ctrl_reg);
		tmp |= ME4600_AO_CTRL_BIT_STOP;
		outl(tmp, instance->ctrl_reg);
		spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);
	} else {
		PERROR("Invalid stop mode specified.\n");
		err = ME_ERRNO_INVALID_STOP_MODE;
		goto ERROR;
	}

ERROR:

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me4600_ao_io_stream_write(me_subdevice_t *subdevice,
				     struct file *filep,
				     int write_mode,
				     int *values, int *count, int flags)
{
	int err = ME_ERRNO_SUCCESS;
	me4600_ao_subdevice_t *instance;
	unsigned long tmp;
	int i;
	int value;
	int cnt = *count;
	int c;
	int k;
	int ret = 0;
	unsigned long cpu_flags = 0;

	PDEBUG("executed.\n");

	instance = (me4600_ao_subdevice_t *) subdevice;

	if (!instance->fifo) {
		PERROR("Not a streaming ao.\n");
		return ME_ERRNO_NOT_SUPPORTED;
	}

	ME_SUBDEVICE_ENTER;

	if (*count <= 0) {
		PERROR("Invalid count of values specified.\n");
		err = ME_ERRNO_INVALID_VALUE_COUNT;
		goto ERROR;
	}

	spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);

	tmp = inl(instance->ctrl_reg);

	switch (tmp & 0x3) {

	case 1:		// Wraparound mode
		if (instance->bosch_fw) {	// Bosch firmware
			spin_unlock_irqrestore(&instance->subdevice_lock,
					       cpu_flags);

			if (cnt != 7) {
				PERROR
				    ("Invalid count of values specified. 7 expected.\n");
				err = ME_ERRNO_INVALID_VALUE_COUNT;
				goto ERROR;
			}

			for (i = 0; i < 7; i++) {
				if (get_user(value, values)) {
					PERROR
					    ("Can't copy value from user space.\n");
					err = ME_ERRNO_INTERNAL;
					goto ERROR;
				}

				if (i == 0) {
					/* Maximum voltage */
					value <<= 16;
					value |=
					    inl(instance->reg_base +
						0xD4) & 0xFFFF;
					outl(value, instance->reg_base + 0xD4);
				} else if (i == 1) {
					/* Minimum voltage */
					value &= 0xFFFF;
					value |=
					    inl(instance->reg_base +
						0xD4) & 0xFFFF0000;
					outl(value, instance->reg_base + 0xD4);
				} else if (i == 2) {
					/* Delta up */
					value <<= 16;
					value |=
					    inl(instance->reg_base +
						0xD8) & 0xFFFF;
					outl(value, instance->reg_base + 0xD8);
				} else if (i == 3) {
					/* Delta down */
					value &= 0xFFFF;
					value |=
					    inl(instance->reg_base +
						0xD8) & 0xFFFF0000;
					outl(value, instance->reg_base + 0xD8);
				} else if (i == 4) {
					/* Start value */
					outl(value, instance->reg_base + 0xDC);
				} else if (i == 5) {
					/* Invert */
					if (value) {
						value = inl(instance->ctrl_reg);
						value |= 0x100;
						outl(value, instance->ctrl_reg);
					} else {
						value = inl(instance->ctrl_reg);
						value &= ~0x100;
						outl(value, instance->ctrl_reg);
					}
				} else if (i == 6) {
					/* Timer for positive ramp */
					outl(value, instance->reg_base + 0xE0);
				}

				values++;
			}
		} else {	// Normal firmware
			PDEBUG("Write for wraparound mode.\n");

			if (inl(instance->status_reg) &
			    ME4600_AO_STATUS_BIT_FSM) {
				spin_unlock_irqrestore(&instance->
						       subdevice_lock,
						       cpu_flags);
				PERROR
				    ("There is already a conversion running.\n");
				err = ME_ERRNO_SUBDEVICE_BUSY;
				goto ERROR;
			}

			tmp |= ME4600_AO_CTRL_BIT_IMMEDIATE_STOP;
			tmp &= ~ME4600_AO_CTRL_BIT_ENABLE_FIFO;
			outl(tmp, instance->ctrl_reg);
			tmp |= ME4600_AO_CTRL_BIT_ENABLE_FIFO;

			if ((*count > ME4600_AO_FIFO_COUNT) ||
			    ((instance->
			      flags & ME4600_AO_FLAGS_SW_WRAP_MODE_MASK) ==
			     ME4600_AO_FLAGS_SW_WRAP_MODE_FIN)) {
				tmp &=
				    ~(ME4600_AO_CTRL_BIT_MODE_0 |
				      ME4600_AO_CTRL_BIT_MODE_1);
				tmp |= ME4600_AO_CTRL_BIT_MODE_1;
			}

			outl(tmp, instance->ctrl_reg);
			spin_unlock_irqrestore(&instance->subdevice_lock,
					       cpu_flags);

			if ((*count <= ME4600_AO_FIFO_COUNT) &&
			    ((instance->
			      flags & ME4600_AO_FLAGS_SW_WRAP_MODE_MASK) ==
			     ME4600_AO_FLAGS_SW_WRAP_MODE_INF)) {
				for (i = 0; i < *count; i++) {
					if (get_user(value, values + i)) {
						PERROR
						    ("Cannot copy value from user space.\n");
						err = ME_ERRNO_INTERNAL;
						goto ERROR;
					}

					if (instance->ao_idx & 0x1)
						value <<= 16;

					outl(value, instance->fifo_reg);
				}
			} else if ((*count <= ME4600_AO_CIRC_BUF_COUNT) &&
				   ((instance->
				     flags & ME4600_AO_FLAGS_SW_WRAP_MODE_MASK)
				    == ME4600_AO_FLAGS_SW_WRAP_MODE_INF)) {
				for (i = 0; i < *count; i++) {
					if (get_user(value, values + i)) {
						PERROR
						    ("Cannot copy value from user space.\n");
						err = ME_ERRNO_INTERNAL;
						goto ERROR;
					}

					instance->circ_buf.buf[i] = value;	/* Used to hold the values. */
				}

				instance->circ_buf.tail = 0;	/* Used as the current read position. */
				instance->circ_buf.head = *count;	/* Used as the buffer size. */

				/* Preload the FIFO. */

				for (i = 0; i < ME4600_AO_FIFO_COUNT;
				     i++, instance->circ_buf.tail++) {
					if (instance->circ_buf.tail >=
					    instance->circ_buf.head)
						instance->circ_buf.tail = 0;

					if (instance->ao_idx & 0x1)
						outl(instance->circ_buf.
						     buf[instance->circ_buf.
							 tail] << 16,
						     instance->fifo_reg);
					else
						outl(instance->circ_buf.
						     buf[instance->circ_buf.
							 tail],
						     instance->fifo_reg);
				}
			} else if ((*count <= ME4600_AO_CIRC_BUF_COUNT) &&
				   ((instance->
				     flags & ME4600_AO_FLAGS_SW_WRAP_MODE_MASK)
				    == ME4600_AO_FLAGS_SW_WRAP_MODE_FIN)) {
				unsigned int preload_count;

				for (i = 0; i < *count; i++) {
					if (get_user(value, values + i)) {
						PERROR
						    ("Cannot copy value from user space.\n");
						err = ME_ERRNO_INTERNAL;
						goto ERROR;
					}

					instance->circ_buf.buf[i] = value;	/* Used to hold the values. */
				}

				instance->circ_buf.tail = 0;	/* Used as the current read position. */
				instance->circ_buf.head = *count;	/* Used as the buffer size. */

				/* Try to preload the whole FIFO. */
				preload_count = ME4600_AO_FIFO_COUNT;

				if (preload_count > instance->wrap_count)
					preload_count = instance->wrap_count;

				/* Preload the FIFO. */
				for (i = 0; i < preload_count;
				     i++, instance->circ_buf.tail++) {
					if (instance->circ_buf.tail >=
					    instance->circ_buf.head)
						instance->circ_buf.tail = 0;

					if (instance->ao_idx & 0x1)
						outl(instance->circ_buf.
						     buf[instance->circ_buf.
							 tail] << 16,
						     instance->fifo_reg);
					else
						outl(instance->circ_buf.
						     buf[instance->circ_buf.
							 tail],
						     instance->fifo_reg);
				}

				instance->wrap_remaining =
				    instance->wrap_count - preload_count;
			} else {
				PERROR("To many values written.\n");
				err = ME_ERRNO_INVALID_VALUE_COUNT;
				goto ERROR;
			}
		}

		break;

	case 2:		// Continuous mode
		/* Check if in SW wrapround mode */
		if (instance->flags & ME4600_AO_FLAGS_SW_WRAP_MODE_MASK) {
			spin_unlock_irqrestore(&instance->subdevice_lock,
					       cpu_flags);
			PERROR("Subdevice is configured SW wrapround mode.\n");
			err = ME_ERRNO_PREVIOUS_CONFIG;
			goto ERROR;
		}

		switch (write_mode) {

		case ME_WRITE_MODE_BLOCKING:
			spin_unlock_irqrestore(&instance->subdevice_lock,
					       cpu_flags);

			PDEBUG("Write for blocking continuous mode.\n");

			while (cnt > 0) {
				wait_event_interruptible(instance->wait_queue,
							 (c =
							  me_circ_buf_space_to_end
							  (&instance->
							   circ_buf)));

				if (instance->
				    flags & ME4600_AO_FLAGS_BROKEN_PIPE) {
					PERROR
					    ("Broken pipe in blocking write.\n");
					err = ME_ERRNO_SUBDEVICE_NOT_RUNNING;
					goto ERROR;
				} else if (signal_pending(current)) {
					PERROR
					    ("Wait for free buffer interrupted from signal.\n");
					err = ME_ERRNO_SIGNAL;
					goto ERROR;
				}

				PDEBUG("Space to end = %d.\n", c);

				/* Only able to write size of free buffer or size of count */

				if (cnt < c)
					c = cnt;
				k = sizeof(int) * c;
				k -= copy_from_user(instance->circ_buf.buf +
						    instance->circ_buf.head,
						    values, k);
				c = k / sizeof(int);

				PDEBUG("Copy %d values from user space.\n", c);

				if (!c) {
					PERROR
					    ("Cannot copy values from user space.\n");
					err = ME_ERRNO_INTERNAL;
					goto ERROR;
				}

				instance->circ_buf.head =
				    (instance->circ_buf.head +
				     c) & (instance->circ_buf.mask);

				values += c;
				cnt -= c;
				ret += c;

				/* Values are now available so enable interrupts */
				spin_lock_irqsave(&instance->subdevice_lock,
						  cpu_flags);

				if (me_circ_buf_space(&instance->circ_buf)) {
					tmp = inl(instance->ctrl_reg);
					tmp |= ME4600_AO_CTRL_BIT_ENABLE_IRQ;
					outl(tmp, instance->ctrl_reg);
				}

				spin_unlock_irqrestore(&instance->
						       subdevice_lock,
						       cpu_flags);
			}

			*count = ret;

			break;

		case ME_WRITE_MODE_NONBLOCKING:
			spin_unlock_irqrestore(&instance->subdevice_lock,
					       cpu_flags);

			PDEBUG("Write for non blocking continuous mode.\n");

			while (cnt > 0) {
				if (instance->
				    flags & ME4600_AO_FLAGS_BROKEN_PIPE) {
					PERROR
					    ("ME4600:Broken pipe in nonblocking write.\n");
					err = ME_ERRNO_SUBDEVICE_NOT_RUNNING;
					goto ERROR;
				}

				c = me_circ_buf_space_to_end(&instance->
							     circ_buf);

				if (!c) {
					PDEBUG
					    ("Returning from nonblocking write.\n");
					break;
				}

				PDEBUG("Space to end = %d.\n", c);

				/* Only able to write size of free buffer or size of count */

				if (cnt < c)
					c = cnt;
				k = sizeof(int) * c;
				k -= copy_from_user(instance->circ_buf.buf +
						    instance->circ_buf.head,
						    values, k);
				c = k / sizeof(int);

				PDEBUG("Copy %d values from user space.\n", c);

				if (!c) {
					PERROR
					    ("Cannot copy values from user space.\n");
					err = ME_ERRNO_INTERNAL;
					goto ERROR;
				}

				instance->circ_buf.head =
				    (instance->circ_buf.head +
				     c) & (instance->circ_buf.mask);

				values += c;
				cnt -= c;
				ret += c;

				/* Values are now available so enable interrupts */
				spin_lock_irqsave(&instance->subdevice_lock,
						  cpu_flags);

				if (me_circ_buf_space(&instance->circ_buf)) {
					tmp = inl(instance->ctrl_reg);
					tmp |= ME4600_AO_CTRL_BIT_ENABLE_IRQ;
					outl(tmp, instance->ctrl_reg);
				}

				spin_unlock_irqrestore(&instance->
						       subdevice_lock,
						       cpu_flags);
			}

			*count = ret;

			break;

		case ME_WRITE_MODE_PRELOAD:
			PDEBUG("Write for preload continuous mode.\n");

			if ((inl(instance->status_reg) &
			     ME4600_AO_STATUS_BIT_FSM)) {
				spin_unlock_irqrestore(&instance->
						       subdevice_lock,
						       cpu_flags);
				PERROR
				    ("Can't Preload DAC FIFO while conversion is running.\n");
				err = ME_ERRNO_SUBDEVICE_BUSY;
				goto ERROR;
			}

			tmp = inl(instance->ctrl_reg);

			tmp |=
			    ME4600_AO_CTRL_BIT_STOP |
			    ME4600_AO_CTRL_BIT_IMMEDIATE_STOP;
			outl(tmp, instance->ctrl_reg);
			tmp &=
			    ~(ME4600_AO_CTRL_BIT_ENABLE_FIFO |
			      ME4600_AO_CTRL_BIT_ENABLE_IRQ);
			outl(tmp, instance->ctrl_reg);
			tmp |= ME4600_AO_CTRL_BIT_ENABLE_FIFO;
			outl(tmp, instance->ctrl_reg);

			instance->circ_buf.head = 0;
			instance->circ_buf.tail = 0;
			instance->flags &= ~ME4600_AO_FLAGS_BROKEN_PIPE;

			spin_unlock_irqrestore(&instance->subdevice_lock,
					       cpu_flags);

			c = ME4600_AO_FIFO_COUNT;

			if (cnt < c)
				c = cnt;

			for (i = 0; i < c; i++) {
				if (get_user(value, values)) {
					PERROR
					    ("Can't copy value from user space.\n");
					err = ME_ERRNO_INTERNAL;
					goto ERROR;
				}

				if (instance->ao_idx & 0x1)
					value <<= 16;

				outl(value, instance->fifo_reg);

				values++;
			}

			cnt -= c;

			ret += c;

			PDEBUG("Wrote %d values to fifo.\n", c);

			while (1) {
				c = me_circ_buf_space_to_end(&instance->
							     circ_buf);

				if (c == 0)
					break;

				if (cnt < c)
					c = cnt;

				if (c <= 0)
					break;

				k = sizeof(int) * c;

				k -= copy_from_user(instance->circ_buf.buf +
						    instance->circ_buf.head,
						    values, k);

				c = k / sizeof(int);

				PDEBUG("Wrote %d values to circular buffer.\n",
				       c);

				if (!c) {
					PERROR
					    ("Can't copy values from user space.\n");
					err = ME_ERRNO_INTERNAL;
					goto ERROR;
				}

				instance->circ_buf.head =
				    (instance->circ_buf.head +
				     c) & (instance->circ_buf.mask);

				values += c;
				cnt -= c;
				ret += c;
			}

			*count = ret;

			break;

		default:
			spin_unlock_irqrestore(&instance->subdevice_lock,
					       cpu_flags);

			PERROR("Invalid write mode specified.\n");

			err = ME_ERRNO_INVALID_WRITE_MODE;

			goto ERROR;
		}

		break;

	default:		// Single mode of invalid
		spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);
		PERROR("Subdevice is configured in single mode.\n");
		err = ME_ERRNO_PREVIOUS_CONFIG;
		goto ERROR;
	}

ERROR:

	ME_SUBDEVICE_EXIT;

	return err;
}

static irqreturn_t me4600_ao_isr(int irq, void *dev_id)
{
	unsigned long tmp;
	int value;
	me4600_ao_subdevice_t *instance = dev_id;
	int i;
	int c = 0;
	int c1 = 0;

	if (irq != instance->irq) {
		PDEBUG("Incorrect interrupt num: %d.\n", irq);
		return IRQ_NONE;
	}

	if (!((0x1 << (instance->ao_idx + 3)) & inl(instance->irq_status_reg))) {
		return IRQ_NONE;
	}

	PDEBUG("executed.\n");

	tmp = inl(instance->status_reg);

	if (!(tmp & ME4600_AO_STATUS_BIT_EF) &&
	    (tmp & ME4600_AO_STATUS_BIT_HF) &&
	    (tmp & ME4600_AO_STATUS_BIT_HF)) {
		c = ME4600_AO_FIFO_COUNT;
		PDEBUG("Fifo empty.\n");
	} else if ((tmp & ME4600_AO_STATUS_BIT_EF) &&
		   (tmp & ME4600_AO_STATUS_BIT_HF) &&
		   (tmp & ME4600_AO_STATUS_BIT_HF)) {
		c = ME4600_AO_FIFO_COUNT / 2;
		PDEBUG("Fifo under half full.\n");
	} else {
		c = 0;
		PDEBUG("Fifo full.\n");
	}

	PDEBUG("Try to write 0x%04X values.\n", c);

	if ((instance->flags & ME4600_AO_FLAGS_SW_WRAP_MODE_MASK) ==
	    ME4600_AO_FLAGS_SW_WRAP_MODE_INF) {
		while (c) {
			c1 = c;

			if (c1 > (instance->circ_buf.head - instance->circ_buf.tail))	/* Only up to the end of the buffer */
				c1 = (instance->circ_buf.head -
				      instance->circ_buf.tail);

			/* Write the values to the FIFO */
			for (i = 0; i < c1; i++, instance->circ_buf.tail++, c--) {
				if (instance->ao_idx & 0x1)
					outl(instance->circ_buf.
					     buf[instance->circ_buf.tail] << 16,
					     instance->fifo_reg);
				else
					outl(instance->circ_buf.
					     buf[instance->circ_buf.tail],
					     instance->fifo_reg);
			}

			if (instance->circ_buf.tail >= instance->circ_buf.head)	/* Start from beginning */
				instance->circ_buf.tail = 0;
		}

		spin_lock(&instance->subdevice_lock);

		tmp = inl(instance->ctrl_reg);
		tmp |= ME4600_AO_CTRL_BIT_RESET_IRQ;
		outl(tmp, instance->ctrl_reg);
		tmp &= ~ME4600_AO_CTRL_BIT_RESET_IRQ;
		outl(tmp, instance->ctrl_reg);

		if (!(inl(instance->status_reg) & ME4600_AO_STATUS_BIT_FSM)) {
			PERROR("Broken pipe.\n");
			instance->flags |= ME4600_AO_FLAGS_BROKEN_PIPE;
			tmp &= ~ME4600_AO_CTRL_BIT_ENABLE_IRQ;
			outl(tmp, instance->ctrl_reg);
		}

		spin_unlock(&instance->subdevice_lock);
	} else if ((instance->flags & ME4600_AO_FLAGS_SW_WRAP_MODE_MASK) ==
		   ME4600_AO_FLAGS_SW_WRAP_MODE_FIN) {
		while (c && instance->wrap_remaining) {
			c1 = c;

			if (c1 > (instance->circ_buf.head - instance->circ_buf.tail))	/* Only up to the end of the buffer */
				c1 = (instance->circ_buf.head -
				      instance->circ_buf.tail);

			if (c1 > instance->wrap_remaining)	/* Only up to count of user defined number of values */
				c1 = instance->wrap_remaining;

			/* Write the values to the FIFO */
			for (i = 0; i < c1;
			     i++, instance->circ_buf.tail++, c--,
			     instance->wrap_remaining--) {
				if (instance->ao_idx & 0x1)
					outl(instance->circ_buf.
					     buf[instance->circ_buf.tail] << 16,
					     instance->fifo_reg);
				else
					outl(instance->circ_buf.
					     buf[instance->circ_buf.tail],
					     instance->fifo_reg);
			}

			if (instance->circ_buf.tail >= instance->circ_buf.head)	/* Start from beginning */
				instance->circ_buf.tail = 0;
		}

		spin_lock(&instance->subdevice_lock);

		tmp = inl(instance->ctrl_reg);

		if (!instance->wrap_remaining) {
			PDEBUG("Finite SW wraparound done.\n");
			tmp &= ~ME4600_AO_CTRL_BIT_ENABLE_IRQ;
		}

		tmp |= ME4600_AO_CTRL_BIT_RESET_IRQ;

		outl(tmp, instance->ctrl_reg);
		tmp &= ~ME4600_AO_CTRL_BIT_RESET_IRQ;
		outl(tmp, instance->ctrl_reg);

		if (!(inl(instance->status_reg) & ME4600_AO_STATUS_BIT_FSM)) {
			PERROR("Broken pipe.\n");
			instance->flags |= ME4600_AO_FLAGS_BROKEN_PIPE;
			tmp &= ~ME4600_AO_CTRL_BIT_ENABLE_IRQ;
			outl(tmp, instance->ctrl_reg);
		}

		spin_unlock(&instance->subdevice_lock);

	} else {		/* Regular continuous mode */

		while (1) {
			c1 = me_circ_buf_values_to_end(&instance->circ_buf);
			PDEBUG("Values to end = %d.\n", c1);

			if (c1 > c)
				c1 = c;

			if (c1 <= 0) {
				PDEBUG("Work done or buffer empty.\n");
				break;
			}

			if (instance->ao_idx & 0x1) {
				for (i = 0; i < c1; i++) {
					value =
					    *(instance->circ_buf.buf +
					      instance->circ_buf.tail +
					      i) << 16;
					outl(value, instance->fifo_reg);
				}
			} else
				outsl(instance->fifo_reg,
				      instance->circ_buf.buf +
				      instance->circ_buf.tail, c1);

			instance->circ_buf.tail =
			    (instance->circ_buf.tail +
			     c1) & (instance->circ_buf.mask);

			PDEBUG("%d values wrote to port 0x%04X.\n", c1,
			       instance->fifo_reg);

			c -= c1;
		}

		spin_lock(&instance->subdevice_lock);

		tmp = inl(instance->ctrl_reg);

		if (!me_circ_buf_values(&instance->circ_buf)) {
			PDEBUG
			    ("Disable Interrupt because no values left in buffer.\n");
			tmp &= ~ME4600_AO_CTRL_BIT_ENABLE_IRQ;
		}

		tmp |= ME4600_AO_CTRL_BIT_RESET_IRQ;

		outl(tmp, instance->ctrl_reg);
		tmp &= ~ME4600_AO_CTRL_BIT_RESET_IRQ;
		outl(tmp, instance->ctrl_reg);

		if (!(inl(instance->status_reg) & ME4600_AO_STATUS_BIT_FSM)) {
			PDEBUG("Broken pipe in me4600_ao_isr.\n");
			instance->flags |= ME4600_AO_FLAGS_BROKEN_PIPE;
			tmp &= ~ME4600_AO_CTRL_BIT_ENABLE_IRQ;
			outl(tmp, instance->ctrl_reg);
		}

		spin_unlock(&instance->subdevice_lock);

		wake_up_interruptible(&instance->wait_queue);
	}

	return IRQ_HANDLED;
}

static void me4600_ao_destructor(struct me_subdevice *subdevice)
{
	me4600_ao_subdevice_t *instance;

	PDEBUG("executed.\n");

	instance = (me4600_ao_subdevice_t *) subdevice;

	free_irq(instance->irq, instance);
	kfree(instance->circ_buf.buf);
	me_subdevice_deinit(&instance->base);
	kfree(instance);
}

me4600_ao_subdevice_t *me4600_ao_constructor(uint32_t reg_base,
					     spinlock_t *preload_reg_lock,
					     uint32_t *preload_flags,
					     int ao_idx, int fifo, int irq)
{
	me4600_ao_subdevice_t *subdevice;
	int err;

	PDEBUG("executed.\n");

	/* Allocate memory for subdevice instance */
	subdevice = kmalloc(sizeof(me4600_ao_subdevice_t), GFP_KERNEL);

	if (!subdevice) {
		PERROR("Cannot get memory for subdevice instance.\n");
		return NULL;
	}

	memset(subdevice, 0, sizeof(me4600_ao_subdevice_t));

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

	/* Allocate and initialize circular buffer */
	subdevice->circ_buf.mask = ME4600_AO_CIRC_BUF_COUNT - 1;
	subdevice->circ_buf.buf = kmalloc(ME4600_AO_CIRC_BUF_SIZE, GFP_KERNEL);

	if (!subdevice->circ_buf.buf) {
		PERROR("Cannot initialize subdevice base class instance.\n");
		me_subdevice_deinit((me_subdevice_t *) subdevice);
		kfree(subdevice);
		return NULL;
	}

	memset(subdevice->circ_buf.buf, 0, ME4600_AO_CIRC_BUF_SIZE);

	subdevice->circ_buf.head = 0;
	subdevice->circ_buf.tail = 0;

	/* Initialize wait queue */
	init_waitqueue_head(&subdevice->wait_queue);

	/* Initialize single value to 0V */
	subdevice->single_value = 0x8000;

	/* Store analog output index */
	subdevice->ao_idx = ao_idx;

	/* Store if analog output has fifo */
	subdevice->fifo = fifo;

	/* Initialize registers */

	if (ao_idx == 0) {
		subdevice->ctrl_reg = reg_base + ME4600_AO_00_CTRL_REG;
		subdevice->status_reg = reg_base + ME4600_AO_00_STATUS_REG;
		subdevice->fifo_reg = reg_base + ME4600_AO_00_FIFO_REG;
		subdevice->single_reg = reg_base + ME4600_AO_00_SINGLE_REG;
		subdevice->timer_reg = reg_base + ME4600_AO_00_TIMER_REG;
		subdevice->reg_base = reg_base;

		if (inl(subdevice->reg_base + ME4600_AO_BOSCH_REG) == 0x20000) {
			PINFO("Bosch firmware in use for channel 0.\n");
			subdevice->bosch_fw = 1;
		} else {
			subdevice->bosch_fw = 0;
		}
	} else if (ao_idx == 1) {
		subdevice->ctrl_reg = reg_base + ME4600_AO_01_CTRL_REG;
		subdevice->status_reg = reg_base + ME4600_AO_01_STATUS_REG;
		subdevice->fifo_reg = reg_base + ME4600_AO_01_FIFO_REG;
		subdevice->single_reg = reg_base + ME4600_AO_01_SINGLE_REG;
		subdevice->timer_reg = reg_base + ME4600_AO_01_TIMER_REG;
		subdevice->reg_base = reg_base;
		subdevice->bosch_fw = 0;
	} else if (ao_idx == 2) {
		subdevice->ctrl_reg = reg_base + ME4600_AO_02_CTRL_REG;
		subdevice->status_reg = reg_base + ME4600_AO_02_STATUS_REG;
		subdevice->fifo_reg = reg_base + ME4600_AO_02_FIFO_REG;
		subdevice->single_reg = reg_base + ME4600_AO_02_SINGLE_REG;
		subdevice->timer_reg = reg_base + ME4600_AO_02_TIMER_REG;
		subdevice->reg_base = reg_base;
		subdevice->bosch_fw = 0;
	} else {
		subdevice->ctrl_reg = reg_base + ME4600_AO_03_CTRL_REG;
		subdevice->status_reg = reg_base + ME4600_AO_03_STATUS_REG;
		subdevice->fifo_reg = reg_base + ME4600_AO_03_FIFO_REG;
		subdevice->single_reg = reg_base + ME4600_AO_03_SINGLE_REG;
		subdevice->timer_reg = reg_base + ME4600_AO_03_TIMER_REG;
		subdevice->reg_base = reg_base;
		subdevice->bosch_fw = 0;
	}

	subdevice->irq_status_reg = reg_base + ME4600_IRQ_STATUS_REG;
	subdevice->preload_reg = reg_base + ME4600_AO_LOADSETREG_XX;

	/* Register interrupt service routine */
	subdevice->irq = irq;

	if (request_irq
	    (subdevice->irq, me4600_ao_isr, IRQF_DISABLED | IRQF_SHARED,
	     ME4600_NAME, subdevice)) {
		PERROR("Cannot get interrupt line.\n");
		me_subdevice_deinit((me_subdevice_t *) subdevice);
		kfree(subdevice->circ_buf.buf);
		kfree(subdevice);
		return NULL;
	}

	/* Override base class methods. */
	subdevice->base.me_subdevice_destructor = me4600_ao_destructor;
	subdevice->base.me_subdevice_io_reset_subdevice =
	    me4600_ao_io_reset_subdevice;
	subdevice->base.me_subdevice_io_single_config =
	    me4600_ao_io_single_config;
	subdevice->base.me_subdevice_io_single_read = me4600_ao_io_single_read;
	subdevice->base.me_subdevice_io_single_write =
	    me4600_ao_io_single_write;
	subdevice->base.me_subdevice_io_stream_config =
	    me4600_ao_io_stream_config;
	subdevice->base.me_subdevice_io_stream_new_values =
	    me4600_ao_io_stream_new_values;
	subdevice->base.me_subdevice_io_stream_write =
	    me4600_ao_io_stream_write;
	subdevice->base.me_subdevice_io_stream_start =
	    me4600_ao_io_stream_start;
	subdevice->base.me_subdevice_io_stream_status =
	    me4600_ao_io_stream_status;
	subdevice->base.me_subdevice_io_stream_stop = me4600_ao_io_stream_stop;
	subdevice->base.me_subdevice_query_number_channels =
	    me4600_ao_query_number_channels;
	subdevice->base.me_subdevice_query_subdevice_type =
	    me4600_ao_query_subdevice_type;
	subdevice->base.me_subdevice_query_subdevice_caps =
	    me4600_ao_query_subdevice_caps;
	subdevice->base.me_subdevice_query_subdevice_caps_args =
	    me4600_ao_query_subdevice_caps_args;
	subdevice->base.me_subdevice_query_range_by_min_max =
	    me4600_ao_query_range_by_min_max;
	subdevice->base.me_subdevice_query_number_ranges =
	    me4600_ao_query_number_ranges;
	subdevice->base.me_subdevice_query_range_info =
	    me4600_ao_query_range_info;
	subdevice->base.me_subdevice_query_timer = me4600_ao_query_timer;

	return subdevice;
}

#endif // BOSCH

/* Common functions
*/

static int me4600_ao_query_range_by_min_max(me_subdevice_t *subdevice,
					    int unit,
					    int *min,
					    int *max, int *maxdata, int *range)
{
	me4600_ao_subdevice_t *instance;

	instance = (me4600_ao_subdevice_t *) subdevice;

	PDEBUG("executed. idx=%d\n", instance->ao_idx);

	if ((*max - *min) < 0) {
		PERROR("Invalid minimum and maximum values specified.\n");
		return ME_ERRNO_INVALID_MIN_MAX;
	}

	if ((unit == ME_UNIT_VOLT) || (unit == ME_UNIT_ANY)) {
		if ((*max <= (ME4600_AO_MAX_RANGE + 1000))
		    && (*min >= ME4600_AO_MIN_RANGE)) {
			*min = ME4600_AO_MIN_RANGE;
			*max = ME4600_AO_MAX_RANGE;
			*maxdata = ME4600_AO_MAX_DATA;
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

static int me4600_ao_query_number_ranges(me_subdevice_t *subdevice,
					 int unit, int *count)
{
	me4600_ao_subdevice_t *instance;

	instance = (me4600_ao_subdevice_t *) subdevice;

	PDEBUG("executed. idx=%d\n", instance->ao_idx);

	if ((unit == ME_UNIT_VOLT) || (unit == ME_UNIT_ANY)) {
		*count = 1;
	} else {
		*count = 0;
	}

	return ME_ERRNO_SUCCESS;
}

static int me4600_ao_query_range_info(me_subdevice_t *subdevice,
				      int range,
				      int *unit,
				      int *min, int *max, int *maxdata)
{
	me4600_ao_subdevice_t *instance;

	instance = (me4600_ao_subdevice_t *) subdevice;

	PDEBUG("executed. idx=%d\n", instance->ao_idx);

	if (range == 0) {
		*unit = ME_UNIT_VOLT;
		*min = ME4600_AO_MIN_RANGE;
		*max = ME4600_AO_MAX_RANGE;
		*maxdata = ME4600_AO_MAX_DATA;
	} else {
		PERROR("Invalid range number specified.\n");
		return ME_ERRNO_INVALID_RANGE;
	}

	return ME_ERRNO_SUCCESS;
}

static int me4600_ao_query_timer(me_subdevice_t *subdevice,
				 int timer,
				 int *base_frequency,
				 long long *min_ticks, long long *max_ticks)
{
	me4600_ao_subdevice_t *instance;

	instance = (me4600_ao_subdevice_t *) subdevice;

	PDEBUG("executed. idx=%d\n", instance->ao_idx);

	if ((timer != ME_TIMER_ACQ_START) && (timer != ME_TIMER_CONV_START)) {
		PERROR("Invalid timer specified.\n");
		return ME_ERRNO_INVALID_TIMER;
	}

	if (instance->fifo) {	//Streaming device.
		*base_frequency = ME4600_AO_BASE_FREQUENCY;
		if (timer == ME_TIMER_ACQ_START) {
			*min_ticks = ME4600_AO_MIN_ACQ_TICKS;
			*max_ticks = ME4600_AO_MAX_ACQ_TICKS;
		} else if (timer == ME_TIMER_CONV_START) {
			*min_ticks = ME4600_AO_MIN_CHAN_TICKS;
			*max_ticks = ME4600_AO_MAX_CHAN_TICKS;
		}
	} else {		//Not streaming device!
		*base_frequency = 0;
		*min_ticks = 0;
		*max_ticks = 0;
	}

	return ME_ERRNO_SUCCESS;
}

static int me4600_ao_query_number_channels(me_subdevice_t *subdevice,
					   int *number)
{
	me4600_ao_subdevice_t *instance;
	instance = (me4600_ao_subdevice_t *) subdevice;

	PDEBUG("executed. idx=%d\n", instance->ao_idx);

	*number = 1;

	return ME_ERRNO_SUCCESS;
}

static int me4600_ao_query_subdevice_type(me_subdevice_t *subdevice,
					  int *type, int *subtype)
{
	me4600_ao_subdevice_t *instance;

	instance = (me4600_ao_subdevice_t *) subdevice;

	PDEBUG("executed. idx=%d\n", instance->ao_idx);

	*type = ME_TYPE_AO;
	*subtype = (instance->fifo) ? ME_SUBTYPE_STREAMING : ME_SUBTYPE_SINGLE;

	return ME_ERRNO_SUCCESS;
}

static int me4600_ao_query_subdevice_caps(me_subdevice_t *subdevice, int *caps)
{
	me4600_ao_subdevice_t *instance;
	instance = (me4600_ao_subdevice_t *) subdevice;

	PDEBUG("executed. idx=%d\n", instance->ao_idx);

	*caps =
	    ME_CAPS_AO_TRIG_SYNCHRONOUS | ((instance->fifo) ? ME_CAPS_AO_FIFO :
					   ME_CAPS_NONE);

	return ME_ERRNO_SUCCESS;
}

static int me4600_ao_query_subdevice_caps_args(struct me_subdevice *subdevice,
					       int cap, int *args, int count)
{
	me4600_ao_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;

	instance = (me4600_ao_subdevice_t *) subdevice;

	PDEBUG("executed. idx=%d\n", instance->ao_idx);

	if (count != 1) {
		PERROR("Invalid capability argument count.\n");
		return ME_ERRNO_INVALID_CAP_ARG_COUNT;
	}

	switch (cap) {
	case ME_CAP_AI_FIFO_SIZE:
		args[0] = (instance->fifo) ? ME4600_AO_FIFO_COUNT : 0;
		break;

	case ME_CAP_AI_BUFFER_SIZE:
		args[0] =
		    (instance->circ_buf.buf) ? ME4600_AO_CIRC_BUF_COUNT : 0;
		break;

	default:
		PERROR("Invalid capability.\n");
		err = ME_ERRNO_INVALID_CAP;
		args[0] = 0;
	}

	return err;
}
