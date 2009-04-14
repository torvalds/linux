/**
 * @file me4600_ai.c
 *
 * @brief ME-4000 analog input subdevice instance.
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
# define __KERNEL__
#endif

/*
 * Includes
 */
#include <linux/module.h>

#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/delay.h>

#include "medefines.h"
#include "meinternal.h"
#include "meerror.h"
#include "medebug.h"
#include "meids.h"

#include "me4600_reg.h"
#include "me4600_ai_reg.h"
#include "me4600_ai.h"

/*
 * Declarations (local)
 */

static void me4600_ai_destructor(struct me_subdevice *subdevice);
static int me4600_ai_io_reset_subdevice(me_subdevice_t *subdevice,
					struct file *filep, int flags);

static int me4600_ai_io_single_config(me_subdevice_t *subdevice,
				      struct file *filep,
				      int channel,
				      int single_config,
				      int ref,
				      int trig_chan,
				      int trig_type, int trig_edge, int flags);

static int me4600_ai_io_single_read(me_subdevice_t *subdevice,
				    struct file *filep,
				    int channel,
				    int *value, int time_out, int flags);

static int me4600_ai_io_stream_config(me_subdevice_t *subdevice,
				      struct file *filep,
				      meIOStreamConfig_t *config_list,
				      int count,
				      meIOStreamTrigger_t *trigger,
				      int fifo_irq_threshold, int flags);
static int me4600_ai_io_stream_read(me_subdevice_t *subdevice,
				    struct file *filep,
				    int read_mode,
				    int *values, int *count, int flags);
static int me4600_ai_io_stream_new_values(me_subdevice_t *subdevice,
					  struct file *filep,
					  int time_out, int *count, int flags);
static inline int me4600_ai_io_stream_read_get_value(me4600_ai_subdevice_t *
						     instance, int *values,
						     const int count,
						     const int flags);

static int me4600_ai_io_stream_start(me_subdevice_t *subdevice,
				     struct file *filep,
				     int start_mode, int time_out, int flags);
static int me4600_ai_io_stream_stop(me_subdevice_t *subdevice,
				    struct file *filep,
				    int stop_mode, int flags);
static int me4600_ai_io_stream_status(me_subdevice_t *subdevice,
				      struct file *filep,
				      int wait,
				      int *status, int *values, int flags);

static int me4600_ai_query_range_by_min_max(me_subdevice_t *subdevice,
					    int unit,
					    int *min,
					    int *max, int *maxdata, int *range);
static int me4600_ai_query_number_ranges(me_subdevice_t *subdevice,
					 int unit, int *count);
static int me4600_ai_query_range_info(me_subdevice_t *subdevice,
				      int range,
				      int *unit,
				      int *min, int *max, int *maxdata);
static int me4600_ai_query_timer(me_subdevice_t *subdevice,
				 int timer,
				 int *base_frequency,
				 long long *min_ticks, long long *max_ticks);
static int me4600_ai_query_number_channels(me_subdevice_t *subdevice,
					   int *number);
static int me4600_ai_query_subdevice_type(me_subdevice_t *subdevice,
					  int *type, int *subtype);
static int me4600_ai_query_subdevice_caps(me_subdevice_t *subdevice,
					  int *caps);
static int me4600_ai_query_subdevice_caps_args(struct me_subdevice *subdevice,
					       int cap, int *args, int count);

static irqreturn_t me4600_ai_isr(int irq, void *dev_id);

static int ai_mux_toggler(me4600_ai_subdevice_t *subdevice);

/** Immidiate stop.
* Reset all IRQ's sources. (block laches)
* Preserve FIFO
*/
static int ai_stop_immediately(me4600_ai_subdevice_t *instance);

/** Immidiate stop.
* Reset all IRQ's sources. (block laches)
* Reset data FIFO
*/
inline void ai_stop_isr(me4600_ai_subdevice_t *instance);

/** Interrupt logics.
* Read datas
* Reset latches
*/
void ai_limited_isr(me4600_ai_subdevice_t *instance, const uint32_t irq_status,
		    const uint32_t ctrl_status);
void ai_infinite_isr(me4600_ai_subdevice_t *instance,
		     const uint32_t irq_status, const uint32_t ctrl_status);

/** Last chunck of datas. We must reschedule sample counter.
* Leaving SC_RELOAD doesn't do any harm, but in some bad case can make extra interrupts.
* When threshold is wrongly set some IRQ are lost.(!!!)
*/
inline void ai_reschedule_SC(me4600_ai_subdevice_t *instance);

/** Read datas from FIFO and copy them to buffer */
static inline int ai_read_data(me4600_ai_subdevice_t *instance,
			       const int count);

/** Copy rest of data from fifo to circular buffer.*/
static inline int ai_read_data_pooling(me4600_ai_subdevice_t *instance);

/** Set ISM to next state for infinite data aqusation mode*/
inline void ai_infinite_ISM(me4600_ai_subdevice_t *instance);

/** Set ISM to next state for define amount of data aqusation mode*/
inline void ai_limited_ISM(me4600_ai_subdevice_t *instance,
			   uint32_t irq_status);

/** Set ISM to next stage for limited mode */
inline void ai_data_acquisition_logic(me4600_ai_subdevice_t *instance);

static void me4600_ai_work_control_task(struct work_struct *work);

/* Definitions
 */

me4600_ai_subdevice_t *me4600_ai_constructor(uint32_t reg_base,
					     unsigned int channels,
					     unsigned int ranges,
					     int isolated,
					     int sh,
					     int irq,
					     spinlock_t *ctrl_reg_lock,
					     struct workqueue_struct *me4600_wq)
{
	me4600_ai_subdevice_t *subdevice;
	int err;
	unsigned int i;

	PDEBUG("executed. idx=0\n");

	// Allocate memory for subdevice instance.
	subdevice = kmalloc(sizeof(me4600_ai_subdevice_t), GFP_KERNEL);

	if (!subdevice) {
		PERROR("Cannot get memory for subdevice instance.\n");
		return NULL;
	}

	memset(subdevice, 0, sizeof(me4600_ai_subdevice_t));

	// Initialize subdevice base class.
	err = me_subdevice_init(&subdevice->base);

	if (err) {
		PERROR("Cannot initialize subdevice base class instance.\n");
		kfree(subdevice);
		return NULL;
	}
	// Initialize spin locks.
	spin_lock_init(&subdevice->subdevice_lock);

	subdevice->ctrl_reg_lock = ctrl_reg_lock;

	// Initialize circular buffer.
	subdevice->circ_buf.mask = ME4600_AI_CIRC_BUF_COUNT - 1;

	subdevice->circ_buf.buf =
	    (void *)__get_free_pages(GFP_KERNEL, ME4600_AI_CIRC_BUF_SIZE_ORDER);
	PDEBUG("circ_buf = %p size=%ld\n", subdevice->circ_buf.buf,
	       ME4600_AI_CIRC_BUF_SIZE);

	if (!subdevice->circ_buf.buf) {
		PERROR("Cannot get circular buffer.\n");
		me_subdevice_deinit((me_subdevice_t *) subdevice);
		kfree(subdevice);
		return NULL;
	}

	memset(subdevice->circ_buf.buf, 0, ME4600_AI_CIRC_BUF_SIZE);
	subdevice->circ_buf.head = 0;
	subdevice->circ_buf.tail = 0;
	subdevice->status = ai_status_none;

	// Initialize wait queue.
	init_waitqueue_head(&subdevice->wait_queue);

	// Save the number of channels.
	subdevice->channels = channels;

	/* Initialize the single config entries to reset values */
	for (i = 0; i < channels; i++) {
		subdevice->single_config[i].status = ME_SINGLE_CHANNEL_NOT_CONFIGURED;	//not configured
	}

	// Save if isolated device.
	subdevice->isolated = isolated;

	// Save if sample and hold is available.
	subdevice->sh = sh;

	// Set stream config to not configured state.
	subdevice->fifo_irq_threshold = 0;
	subdevice->data_required = 0;
	subdevice->chan_list_len = 0;

	// Initialize registers addresses.
	subdevice->ctrl_reg = reg_base + ME4600_AI_CTRL_REG;
	subdevice->status_reg = reg_base + ME4600_AI_STATUS_REG;
	subdevice->channel_list_reg = reg_base + ME4600_AI_CHANNEL_LIST_REG;
	subdevice->data_reg = reg_base + ME4600_AI_DATA_REG;
	subdevice->chan_timer_reg = reg_base + ME4600_AI_CHAN_TIMER_REG;
	subdevice->chan_pre_timer_reg = reg_base + ME4600_AI_CHAN_PRE_TIMER_REG;
	subdevice->scan_timer_low_reg = reg_base + ME4600_AI_SCAN_TIMER_LOW_REG;
	subdevice->scan_timer_high_reg =
	    reg_base + ME4600_AI_SCAN_TIMER_HIGH_REG;
	subdevice->scan_pre_timer_low_reg =
	    reg_base + ME4600_AI_SCAN_PRE_TIMER_LOW_REG;
	subdevice->scan_pre_timer_high_reg =
	    reg_base + ME4600_AI_SCAN_PRE_TIMER_HIGH_REG;
	subdevice->start_reg = reg_base + ME4600_AI_START_REG;
	subdevice->irq_status_reg = reg_base + ME4600_IRQ_STATUS_REG;
	subdevice->sample_counter_reg = reg_base + ME4600_AI_SAMPLE_COUNTER_REG;
#ifdef MEDEBUG_DEBUG_REG
	subdevice->reg_base = reg_base;
#endif

	// Initialize ranges.
	subdevice->ranges_len = ranges;
	subdevice->ranges[0].min = -10E6;
	subdevice->ranges[0].max = 9999694;

	subdevice->ranges[1].min = 0;
	subdevice->ranges[1].max = 9999847;

	subdevice->ranges[2].min = -25E5;
	subdevice->ranges[2].max = 2499923;

	subdevice->ranges[3].min = 0;
	subdevice->ranges[3].max = 2499961;

	// We have to switch the mux in order to get it work correctly.
	ai_mux_toggler(subdevice);

	// Register interrupt service routine.
	subdevice->irq = irq;
	if (request_irq(subdevice->irq, me4600_ai_isr,
#ifdef IRQF_DISABLED
			IRQF_DISABLED | IRQF_SHARED,
#else
			SA_INTERRUPT | SA_SHIRQ,
#endif
			ME4600_NAME, subdevice)) {
		PERROR("Cannot register interrupt service routine.\n");
		me_subdevice_deinit((me_subdevice_t *) subdevice);
		free_pages((unsigned long)subdevice->circ_buf.buf,
			   ME4600_AI_CIRC_BUF_SIZE_ORDER);
		subdevice->circ_buf.buf = NULL;
		kfree(subdevice);
		return NULL;
	}
	PINFO("Registered irq=%d.\n", subdevice->irq);

	// Override base class methods.
	subdevice->base.me_subdevice_destructor = me4600_ai_destructor;
	subdevice->base.me_subdevice_io_reset_subdevice =
	    me4600_ai_io_reset_subdevice;
	subdevice->base.me_subdevice_io_single_config =
	    me4600_ai_io_single_config;
	subdevice->base.me_subdevice_io_single_read = me4600_ai_io_single_read;
	subdevice->base.me_subdevice_io_stream_config =
	    me4600_ai_io_stream_config;
	subdevice->base.me_subdevice_io_stream_new_values =
	    me4600_ai_io_stream_new_values;
	subdevice->base.me_subdevice_io_stream_read = me4600_ai_io_stream_read;
	subdevice->base.me_subdevice_io_stream_start =
	    me4600_ai_io_stream_start;
	subdevice->base.me_subdevice_io_stream_status =
	    me4600_ai_io_stream_status;
	subdevice->base.me_subdevice_io_stream_stop = me4600_ai_io_stream_stop;
	subdevice->base.me_subdevice_query_number_channels =
	    me4600_ai_query_number_channels;
	subdevice->base.me_subdevice_query_subdevice_type =
	    me4600_ai_query_subdevice_type;
	subdevice->base.me_subdevice_query_subdevice_caps =
	    me4600_ai_query_subdevice_caps;
	subdevice->base.me_subdevice_query_subdevice_caps_args =
	    me4600_ai_query_subdevice_caps_args;
	subdevice->base.me_subdevice_query_range_by_min_max =
	    me4600_ai_query_range_by_min_max;
	subdevice->base.me_subdevice_query_number_ranges =
	    me4600_ai_query_number_ranges;
	subdevice->base.me_subdevice_query_range_info =
	    me4600_ai_query_range_info;
	subdevice->base.me_subdevice_query_timer = me4600_ai_query_timer;

	// Prepare work queue.
	subdevice->me4600_workqueue = me4600_wq;

/* workqueue API changed in kernel 2.6.20 */
	INIT_DELAYED_WORK(&subdevice->ai_control_task,
			  me4600_ai_work_control_task);

	return subdevice;
}

static void me4600_ai_destructor(struct me_subdevice *subdevice)
{
	me4600_ai_subdevice_t *instance;

	instance = (me4600_ai_subdevice_t *) subdevice;

	PDEBUG("executed. idx=0\n");

	instance->ai_control_task_flag = 0;
	// Reset subdevice to asure clean exit.
	me4600_ai_io_reset_subdevice(subdevice, NULL,
				     ME_IO_RESET_SUBDEVICE_NO_FLAGS);

	// Remove any tasks from work queue. This is paranoic because it was done allready in reset().
	if (!cancel_delayed_work(&instance->ai_control_task)) {	//Wait 2 ticks to be sure that control task is removed from queue.
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(2);
	}

	free_irq(instance->irq, instance);
	free_pages((unsigned long)instance->circ_buf.buf,
		   ME4600_AI_CIRC_BUF_SIZE_ORDER);
	me_subdevice_deinit(&instance->base);
	kfree(instance);
}

static int me4600_ai_io_reset_subdevice(me_subdevice_t *subdevice,
					struct file *filep, int flags)
{
	me4600_ai_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	volatile uint32_t ctrl;
	unsigned long status;
	const int timeout = HZ / 10;	//100ms
	int i;

	PDEBUG("executed. idx=0\n");

	if (flags) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	instance = (me4600_ai_subdevice_t *) subdevice;

	ME_SUBDEVICE_ENTER;

	instance->ai_control_task_flag = 0;
	instance->status = ai_status_none;

	for (i = 0; i <= timeout; i++) {
		spin_lock_irqsave(instance->ctrl_reg_lock, status);
		ctrl = inl(instance->ctrl_reg);
		//Stop DMA
		ctrl &= ~ME4600_AI_CTRL_RPCI_FIFO;
		// Stop all actions. No conditions!
		ctrl &= ~ME4600_AI_CTRL_BIT_STOP;
		ctrl |= ME4600_AI_CTRL_BIT_IMMEDIATE_STOP;

		outl(ctrl, instance->ctrl_reg);
		PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->ctrl_reg - instance->reg_base, ctrl);
		spin_unlock_irqrestore(instance->ctrl_reg_lock, status);

		if (!(inl(instance->status_reg) & ME4600_AI_STATUS_BIT_FSM))
			break;

		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(1);
	}

	if (i > timeout) {
		PERROR("FSM is still busy.\n");
		ME_SUBDEVICE_EXIT;
		return ME_ERRNO_INTERNAL;
	}

	spin_lock_irqsave(instance->ctrl_reg_lock, status);
	ctrl = inl(instance->ctrl_reg);
	// Clear all features. Dissable interrupts.
	ctrl &= ~(ME4600_AI_CTRL_BIT_STOP
		  | ME4600_AI_CTRL_BIT_LE_IRQ
		  | ME4600_AI_CTRL_BIT_HF_IRQ | ME4600_AI_CTRL_BIT_SC_IRQ);
	ctrl |= (ME4600_AI_CTRL_BIT_IMMEDIATE_STOP
		 | ME4600_AI_CTRL_BIT_LE_IRQ_RESET
		 | ME4600_AI_CTRL_BIT_HF_IRQ_RESET
		 | ME4600_AI_CTRL_BIT_SC_IRQ_RESET);

	outl(ctrl, instance->ctrl_reg);
	PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->ctrl_reg - instance->reg_base, ctrl);
	spin_unlock_irqrestore(instance->ctrl_reg_lock, status);

	outl(ME4600_AI_MIN_CHAN_TICKS - 1, instance->chan_timer_reg);
	PDEBUG_REG("chan_timer_reg outl(0x%lX+0x%lX)=0x%llx\n",
		   instance->reg_base,
		   instance->chan_timer_reg - instance->reg_base,
		   ME4600_AI_MIN_CHAN_TICKS);
	outl(ME4600_AI_MIN_ACQ_TICKS - 1, instance->chan_pre_timer_reg);
	PDEBUG_REG("chan_pre_timer_reg outl(0x%lX+0x%lX)=0x%llx\n",
		   instance->reg_base,
		   instance->chan_pre_timer_reg - instance->reg_base,
		   ME4600_AI_MIN_ACQ_TICKS);
	outl(0, instance->scan_timer_low_reg);
	PDEBUG_REG("scan_timer_low_reg outl(0x%lX+0x%lX)=0x%x\n",
		   instance->reg_base,
		   instance->scan_timer_low_reg - instance->reg_base, 0);
	outl(0, instance->scan_timer_high_reg);
	PDEBUG_REG("scan_timer_high_reg outl(0x%lX+0x%lX)=0x%x\n",
		   instance->reg_base,
		   instance->scan_timer_high_reg - instance->reg_base, 0);
	outl(0, instance->scan_pre_timer_low_reg);
	PDEBUG_REG("scan_pre_timer_low_reg outl(0x%lX+0x%lX)=0x%x\n",
		   instance->reg_base,
		   instance->scan_pre_timer_low_reg - instance->reg_base, 0);
	outl(0, instance->scan_pre_timer_high_reg);
	PDEBUG_REG("scan_pre_timer_high_reg outl(0x%lX+0x%lX)=0x%x\n",
		   instance->reg_base,
		   instance->scan_pre_timer_high_reg - instance->reg_base, 0);
	outl(0xEFFFFFFF, instance->sample_counter_reg);
	PDEBUG_REG("sample_counter_reg outl(0x%lX+0x%lX)=0x%x\n",
		   instance->reg_base,
		   instance->sample_counter_reg - instance->reg_base,
		   0xEFFFFFFF);

	instance->circ_buf.head = 0;
	instance->circ_buf.tail = 0;

	instance->fifo_irq_threshold = 0;
	instance->data_required = 0;
	instance->chan_list_len = 0;

	// Initialize the single config entries to reset values.
	for (i = 0; i < instance->channels; i++) {
		instance->single_config[i].status =
		    ME_SINGLE_CHANNEL_NOT_CONFIGURED;
	}
	instance->status = ai_status_none;

	//Signal reset if user is on wait.
	wake_up_interruptible_all(&instance->wait_queue);

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me4600_ai_io_single_config(me_subdevice_t *subdevice,
				      struct file *filep,
				      int channel,
				      int single_config,
				      int ref,
				      int trig_chan,
				      int trig_type, int trig_edge, int flags)
{
	me4600_ai_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	unsigned long cpu_flags;
	int i;

	instance = (me4600_ai_subdevice_t *) subdevice;

	PDEBUG("executed. idx=0\n");

	if (flags & ~ME_IO_SINGLE_CONFIG_CONTINUE) {
		PERROR("Invalid flag specified.\n");
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

	case ME_TRIG_TYPE_EXT_ANALOG:
		if (instance->channels <= 16)	//Only versions with 32 channels have analog trigger (4670 and 4680)
		{
			PERROR("Invalid trigger type specified.\n");
			return ME_ERRNO_INVALID_TRIG_TYPE;
		}

	case ME_TRIG_TYPE_EXT_DIGITAL:
		if ((trig_edge != ME_TRIG_EDGE_ANY)
		    && (trig_edge != ME_TRIG_EDGE_RISING)
		    && (trig_edge != ME_TRIG_EDGE_FALLING)) {
			PERROR("Invalid trigger edge specified.\n");
			return ME_ERRNO_INVALID_TRIG_EDGE;
		}
		break;

	default:
		PERROR("Invalid trigger type specified.\n");
		return ME_ERRNO_INVALID_TRIG_TYPE;
	}

	if (trig_chan != ME_TRIG_CHAN_DEFAULT) {
		PERROR("Invalid trigger channel specified.\n");
		return ME_ERRNO_INVALID_TRIG_CHAN;
	}

	if ((single_config < 0) || (single_config >= instance->ranges_len)) {
		PERROR("Invalid single config specified.\n");
		return ME_ERRNO_INVALID_SINGLE_CONFIG;
	}

	if ((ref != ME_REF_AI_GROUND) && (ref != ME_REF_AI_DIFFERENTIAL)) {
		PERROR("Invalid analog reference specified.\n");
		return ME_ERRNO_INVALID_REF;
	}

	if ((single_config % 2) && (ref != ME_REF_AI_GROUND)) {
		PERROR("Invalid analog reference specified.\n");
		return ME_ERRNO_INVALID_REF;
	}

	if ((ref == ME_REF_AI_DIFFERENTIAL)
	    && ((instance->channels == 16) || (channel >= 16))) {
		PERROR("Invalid analog reference specified.\n");
		return ME_ERRNO_INVALID_REF;
	}

	if (channel < 0) {
		PERROR("Invalid channel number specified.\n");
		return ME_ERRNO_INVALID_CHANNEL;
	}

	if (channel >= instance->channels) {
		PERROR("Invalid channel number specified.\n");
		return ME_ERRNO_INVALID_CHANNEL;
	}

	ME_SUBDEVICE_ENTER;

	spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);
	//Prepare data entry.
	// Common for all modes.
	instance->single_config[channel].entry =
	    channel | ME4600_AI_LIST_LAST_ENTRY;

	if (ref == ME_REF_AI_DIFFERENTIAL) {	// ME_REF_AI_DIFFERENTIAL
		instance->single_config[channel].entry |=
		    ME4600_AI_LIST_INPUT_DIFFERENTIAL;
	}
/*
		// ME4600_AI_LIST_INPUT_SINGLE_ENDED = 0x0000
		// 'entry |= ME4600_AI_LIST_INPUT_SINGLE_ENDED' <== Do nothing. Removed.
		else
		{// ME_REF_AI_GROUND
			instance->single_config[channel].entry |= ME4600_AI_LIST_INPUT_SINGLE_ENDED;
		}
*/
	switch (single_config) {
	case 0:		//-10V..10V
/*
					// ME4600_AI_LIST_RANGE_BIPOLAR_10 = 0x0000
					// 'entry |= ME4600_AI_LIST_RANGE_BIPOLAR_10' <== Do nothing. Removed.
					instance->single_config[channel].entry |= ME4600_AI_LIST_RANGE_BIPOLAR_10;
*/ break;

	case 1:		//0V..10V
		instance->single_config[channel].entry |=
		    ME4600_AI_LIST_RANGE_UNIPOLAR_10;
		break;

	case 2:		//-2.5V..2.5V
		instance->single_config[channel].entry |=
		    ME4600_AI_LIST_RANGE_BIPOLAR_2_5;
		break;

	case 3:		//0V..2.5V
		instance->single_config[channel].entry |=
		    ME4600_AI_LIST_RANGE_UNIPOLAR_2_5;
		break;
	}

	// Prepare control register.
	// Common for all modes.
	instance->single_config[channel].ctrl =
	    ME4600_AI_CTRL_BIT_CHANNEL_FIFO | ME4600_AI_CTRL_BIT_DATA_FIFO;

	switch (trig_type) {
	case ME_TRIG_TYPE_SW:
		// Nothing to set.
		break;

	case ME_TRIG_TYPE_EXT_ANALOG:
		instance->single_config[channel].ctrl |=
		    ME4600_AI_CTRL_BIT_EX_TRIG_ANALOG;

	case ME_TRIG_TYPE_EXT_DIGITAL:
		instance->single_config[channel].ctrl |=
		    ME4600_AI_CTRL_BIT_EX_TRIG;
		break;
	}

	switch (trig_edge) {
	case ME_TRIG_EDGE_RISING:
		// Nothing to set.
		break;

	case ME_TRIG_EDGE_ANY:
		instance->single_config[channel].ctrl |=
		    ME4600_AI_CTRL_BIT_EX_TRIG_BOTH;

	case ME_TRIG_EDGE_FALLING:
		instance->single_config[channel].ctrl |=
		    ME4600_AI_CTRL_BIT_EX_TRIG_FALLING;
		break;
	}

	// Enable this channel
	instance->single_config[channel].status = ME_SINGLE_CHANNEL_CONFIGURED;

	// Copy this settings to other outputs.
	if (flags == ME_IO_SINGLE_CONFIG_CONTINUE) {
		for (i = channel + 1; i < instance->channels; i++) {
			instance->single_config[i].ctrl =
			    instance->single_config[channel].ctrl;
			instance->single_config[i].entry =
			    instance->single_config[channel].entry;
			instance->single_config[i].status =
			    ME_SINGLE_CHANNEL_CONFIGURED;
		}
	}

	instance->status = ai_status_single_configured;
	spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me4600_ai_io_single_read(me_subdevice_t *subdevice,
				    struct file *filep,
				    int channel,
				    int *value, int time_out, int flags)
{
	me4600_ai_subdevice_t *instance;
	volatile uint32_t tmp;
	volatile uint32_t val;
	unsigned long cpu_flags;
	int err = ME_ERRNO_SUCCESS;

	unsigned long j;
	unsigned long delay = 0;

	PDEBUG("executed. idx=0\n");

	instance = (me4600_ai_subdevice_t *) subdevice;

	if (flags) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	if (instance->status != ai_status_single_configured) {
		PERROR("Subdevice not configured to work in single mode!\n");
		return ME_ERRNO_PREVIOUS_CONFIG;
	}

	if ((channel > instance->channels) || (channel < 0)) {
		PERROR("Invalid channel specified.\n");
		return ME_ERRNO_INVALID_CHANNEL;
	}

	if (time_out < 0) {
		PERROR("Invalid timeout specified.\n");
		return ME_ERRNO_INVALID_TIMEOUT;
	}

	if (instance->single_config[channel].status !=
	    ME_SINGLE_CHANNEL_CONFIGURED) {
		PERROR("Channel is not configured to work in single mode!\n");
		return ME_ERRNO_PREVIOUS_CONFIG;
	}

	if (inl(instance->status_reg) & ME4600_AI_STATUS_BIT_FSM) {
		PERROR("Subdevice is busy.\n");
		return ME_ERRNO_SUBDEVICE_BUSY;
	}

	ME_SUBDEVICE_ENTER;

	// Cancel control task
	PDEBUG("Cancel control task.\n");
	instance->ai_control_task_flag = 0;
	cancel_delayed_work(&instance->ai_control_task);

	if (time_out) {
		delay = (time_out * HZ) / 1000;

		if (delay == 0)
			delay = 1;
	}

	spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);

	// Mark that StreamConfig is removed.
	instance->chan_list_len = 0;

	spin_lock_irqsave(instance->ctrl_reg_lock, cpu_flags);
	/// @note Imprtant: Preserve EXT IRQ settings.
	tmp = inl(instance->ctrl_reg);
	// Clear FIFOs and dissable interrupts
	tmp &=
	    ~(ME4600_AI_CTRL_BIT_CHANNEL_FIFO | ME4600_AI_CTRL_BIT_DATA_FIFO);

	tmp &=
	    ~(ME4600_AI_CTRL_BIT_SC_IRQ | ME4600_AI_CTRL_BIT_HF_IRQ |
	      ME4600_AI_CTRL_BIT_LE_IRQ);
	tmp |=
	    ME4600_AI_CTRL_BIT_SC_IRQ_RESET | ME4600_AI_CTRL_BIT_HF_IRQ_RESET |
	    ME4600_AI_CTRL_BIT_LE_IRQ_RESET;

	tmp |= ME4600_AI_CTRL_BIT_IMMEDIATE_STOP;
	outl(tmp, instance->ctrl_reg);
	PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->ctrl_reg - instance->reg_base, tmp);

	outl(0, instance->scan_pre_timer_low_reg);
	PDEBUG_REG("scan_pre_timer_low_reg outl(0x%lX+0x%lX)=0x%x\n",
		   instance->reg_base,
		   instance->scan_pre_timer_low_reg - instance->reg_base, 0);
	outl(0, instance->scan_pre_timer_high_reg);
	PDEBUG_REG("scan_pre_timer_high_reg outl(0x%lX+0x%lX)=0x%x\n",
		   instance->reg_base,
		   instance->scan_pre_timer_high_reg - instance->reg_base, 0);
	outl(0, instance->scan_timer_low_reg);
	PDEBUG_REG("scan_timer_low_reg outl(0x%lX+0x%lX)=0x%x\n",
		   instance->reg_base,
		   instance->scan_timer_low_reg - instance->reg_base, 0);
	outl(0, instance->scan_timer_high_reg);
	PDEBUG_REG("scan_timer_high_reg outl(0x%lX+0x%lX)=0x%x\n",
		   instance->reg_base,
		   instance->scan_timer_high_reg - instance->reg_base, 0);
	outl(65, instance->chan_timer_reg);
	PDEBUG_REG("chan_timer_reg outl(0x%lX+0x%lX)=0x%x\n",
		   instance->reg_base,
		   instance->chan_timer_reg - instance->reg_base, 65);
	outl(65, instance->chan_pre_timer_reg);
	PDEBUG_REG("chan_pre_timer_reg outl(0x%lX+0x%lX)=0x%x\n",
		   instance->reg_base,
		   instance->chan_pre_timer_reg - instance->reg_base, 65);

	//Reactive FIFOs. Enable work.
	tmp |= ME4600_AI_CTRL_BIT_CHANNEL_FIFO | ME4600_AI_CTRL_BIT_DATA_FIFO;
	outl(tmp, instance->ctrl_reg);
	PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->ctrl_reg - instance->reg_base, tmp);

	outl(instance->single_config[channel].entry,
	     instance->channel_list_reg);
	PDEBUG_REG("channel_list_reg outl(0x%lX+0x%lX)=0x%x\n",
		   instance->reg_base,
		   instance->channel_list_reg - instance->reg_base,
		   instance->single_config[channel].entry);

	// Preserve EXT IRQ settings.
	tmp &= (ME4600_AI_CTRL_BIT_EX_IRQ | ME4600_AI_CTRL_BIT_EX_IRQ_RESET);
	outl(instance->single_config[channel].ctrl | tmp, instance->ctrl_reg);
	PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->ctrl_reg - instance->reg_base,
		   instance->single_config[channel].ctrl | tmp);

	spin_unlock_irqrestore(instance->ctrl_reg_lock, cpu_flags);

	if (!(instance->single_config[channel].ctrl & ME4600_AI_CTRL_BIT_EX_TRIG)) {	// Software start
		inl(instance->start_reg);
		PDEBUG_REG("start_reg inl(0x%lX+0x%lX)\n", instance->reg_base,
			   instance->start_reg - instance->reg_base);

		delay = 2;
	}

	j = jiffies;

	while (!(inl(instance->status_reg) & ME4600_AI_STATUS_BIT_EF_DATA)) {
		if (delay && ((jiffies - j) >= delay)) {
			if (!(instance->single_config[channel].ctrl & ME4600_AI_CTRL_BIT_EX_TRIG)) {	// Software start.
				PERROR("Value not available after wait.\n");
				err = ME_ERRNO_INTERNAL;
			} else {	// External start.
				PERROR("Timeout reached.\n");
				err = ME_ERRNO_TIMEOUT;
			}
			break;
		}
		// Wait
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(1);

		if (signal_pending(current)) {
			PERROR
			    ("Wait on external trigger interrupted by signal.\n");
			err = ME_ERRNO_SIGNAL;
			break;
		}

		if (instance->status != ai_status_single_configured) {
			PERROR("Wait interrupted by reset.\n");
			err = ME_ERRNO_CANCELLED;
			break;
		}
	}

	// Read value.
	if (!err) {
		val = inl(instance->data_reg) ^ 0x8000;
		PDEBUG_REG("data_reg inl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->data_reg - instance->reg_base, val);
		*value = val & ME4600_AI_MAX_DATA;
	} else {
		*value = 0xFFFFFFFF;
	}

	// Restore settings.
	spin_lock_irqsave(instance->ctrl_reg_lock, cpu_flags);
	tmp = inl(instance->ctrl_reg);
	// Clear FIFOs and dissable interrupts.
	tmp &=
	    ~(ME4600_AI_CTRL_BIT_CHANNEL_FIFO | ME4600_AI_CTRL_BIT_DATA_FIFO);
	tmp |= ME4600_AI_CTRL_BIT_SC_IRQ | ME4600_AI_CTRL_BIT_HF_IRQ;
	tmp |=
	    ME4600_AI_CTRL_BIT_SC_IRQ_RESET | ME4600_AI_CTRL_BIT_HF_IRQ_RESET |
	    ME4600_AI_CTRL_BIT_LE_IRQ_RESET | ME4600_AI_CTRL_BIT_IMMEDIATE_STOP;
	outl(tmp, instance->ctrl_reg);
	PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->ctrl_reg - instance->reg_base, tmp);
	spin_unlock_irqrestore(instance->ctrl_reg_lock, cpu_flags);

	spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me4600_ai_io_stream_config(me_subdevice_t *subdevice,
				      struct file *filep,
				      meIOStreamConfig_t *config_list,
				      int count,
				      meIOStreamTrigger_t *trigger,
				      int fifo_irq_threshold, int flags)
{
	me4600_ai_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	int i;			// internal multipurpose variable
	unsigned long long data_required;

	volatile uint32_t entry;
	volatile uint32_t ctrl = ME4600_AI_CTRL_BIT_IMMEDIATE_STOP;
	volatile uint32_t tmp;	// use when current copy of register's value needed
	unsigned long cpu_flags;

	uint64_t acq_ticks;
	uint64_t scan_ticks;
	uint64_t conv_ticks;
	unsigned int acq_start_ticks_low = trigger->iAcqStartTicksLow;
	unsigned int acq_start_ticks_high = trigger->iAcqStartTicksHigh;
	unsigned int scan_start_ticks_low = trigger->iScanStartTicksLow;
	unsigned int scan_start_ticks_high = trigger->iScanStartTicksHigh;
	unsigned int conv_start_ticks_low = trigger->iConvStartTicksLow;
	unsigned int conv_start_ticks_high = trigger->iConvStartTicksHigh;

	PDEBUG("executed. idx=0\n");

	instance = (me4600_ai_subdevice_t *) subdevice;

	if (flags) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	ME_SUBDEVICE_ENTER
	    // Convert ticks to 64 bit long values
	    acq_ticks =
	    (uint64_t) acq_start_ticks_low +
	    ((uint64_t) acq_start_ticks_high << 32);
	scan_ticks =
	    (uint64_t) scan_start_ticks_low +
	    ((uint64_t) scan_start_ticks_high << 32);
	conv_ticks =
	    (uint64_t) conv_start_ticks_low +
	    ((uint64_t) conv_start_ticks_high << 32);

	// Check settings - begin
	switch (trigger->iAcqStartTrigType) {
	case ME_TRIG_TYPE_SW:
	case ME_TRIG_TYPE_EXT_DIGITAL:
	case ME_TRIG_TYPE_EXT_ANALOG:
		break;

	default:
		PERROR("Invalid acquisition start trigger type specified.\n");
		err = ME_ERRNO_INVALID_ACQ_START_TRIG_TYPE;
		goto ERROR;
		break;
	}

	if ((trigger->iAcqStartTrigType == ME_TRIG_TYPE_SW)
	    && (trigger->iAcqStartTrigEdge != ME_TRIG_EDGE_NONE)) {
		PERROR("Invalid acquisition start trigger edge specified.\n");
		err = ME_ERRNO_INVALID_ACQ_START_TRIG_EDGE;
		goto ERROR;
	}

	if (trigger->iAcqStartTrigType != ME_TRIG_TYPE_SW) {
		switch (trigger->iAcqStartTrigEdge) {
		case ME_TRIG_EDGE_RISING:
		case ME_TRIG_EDGE_FALLING:
		case ME_TRIG_EDGE_ANY:
			break;

		default:
			PERROR
			    ("Invalid acquisition start trigger edge specified.\n");
			err = ME_ERRNO_INVALID_ACQ_START_TRIG_EDGE;
			goto ERROR;
			break;
		}
	}

	if (trigger->iAcqStartTrigChan != ME_TRIG_CHAN_DEFAULT) {
		PERROR
		    ("Invalid acquisition start trigger channel specified.\n");
		err = ME_ERRNO_INVALID_ACQ_START_TRIG_CHAN;
		goto ERROR;
	}

	if ((acq_ticks < ME4600_AI_MIN_ACQ_TICKS)
	    || (acq_ticks > ME4600_AI_MAX_ACQ_TICKS)) {
		PERROR
		    ("Invalid acquisition start trigger argument specified.\n");
		err = ME_ERRNO_INVALID_ACQ_START_ARG;
		goto ERROR;
	}

	switch (trigger->iScanStartTrigType) {

	case ME_TRIG_TYPE_TIMER:
		if ((scan_ticks < ME4600_AI_MIN_SCAN_TICKS)
		    || (scan_ticks > ME4600_AI_MAX_SCAN_TICKS)
		    || (scan_ticks < count * conv_ticks)
		    ) {
			PERROR("Invalid scan start argument specified.\n");
			err = ME_ERRNO_INVALID_SCAN_START_ARG;
			goto ERROR;
		}
		break;

	case ME_TRIG_TYPE_EXT_DIGITAL:
		if (trigger->iAcqStartTrigType != ME_TRIG_TYPE_EXT_DIGITAL) {
			PERROR
			    ("Invalid scan start trigger type specified (Acq is HW digital)\n");
			err = ME_ERRNO_INVALID_SCAN_START_TRIG_TYPE;
			goto ERROR;
		}
		break;

	case ME_TRIG_TYPE_EXT_ANALOG:
		if (trigger->iAcqStartTrigType != ME_TRIG_TYPE_EXT_ANALOG) {
			PERROR
			    ("Invalid scan start trigger type specified (Acq is HW analog)\n");
			err = ME_ERRNO_INVALID_SCAN_START_TRIG_TYPE;
			goto ERROR;
		}
		break;

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
		if ((conv_ticks < ME4600_AI_MIN_CHAN_TICKS)
		    || (conv_ticks > ME4600_AI_MAX_CHAN_TICKS)) {
			PERROR
			    ("Invalid conv start trigger argument specified.\n");
			err = ME_ERRNO_INVALID_CONV_START_ARG;
			goto ERROR;
		}
		break;

	case ME_TRIG_TYPE_EXT_DIGITAL:
		if ((trigger->iScanStartTrigType != ME_TRIG_TYPE_FOLLOW)
		    || (trigger->iAcqStartTrigType !=
			ME_TRIG_TYPE_EXT_DIGITAL)) {
			PERROR("Invalid conv start trigger type specified.\n");
			err = ME_ERRNO_INVALID_CONV_START_TRIG_TYPE;
			goto ERROR;
		}
		break;

	case ME_TRIG_TYPE_EXT_ANALOG:
		if ((trigger->iScanStartTrigType != ME_TRIG_TYPE_FOLLOW)
		    || (trigger->iAcqStartTrigType !=
			ME_TRIG_TYPE_EXT_ANALOG)) {
			PERROR("Invalid conv start trigger type specified.\n");
			err = ME_ERRNO_INVALID_CONV_START_TRIG_TYPE;
			goto ERROR;
		}
		break;

	default:
		PERROR("Invalid conv start trigger type specified.\n");
		err = ME_ERRNO_INVALID_CONV_START_TRIG_TYPE;
		goto ERROR;

		break;
	}
/**
* Aceptable settings:
* iScanStopTrigType		:	iAcqStopTrigType
*
* ME_TRIG_TYPE_NONE		:	ME_TRIG_TYPE_NONE	-> infinite count with manual stop
* ME_TRIG_TYPE_NONE		:	ME_TRIG_TYPE_COUNT	-> stop after getting iScanStopCount list of values (iScanStopCount * count)
* ME_TRIG_TYPE_COUNT	:	ME_TRIG_TYPE_FOLLOW	-> stop after getting iAcqStopCount values (it can stops in midle of the list)
*/
	switch (trigger->iScanStopTrigType) {

	case ME_TRIG_TYPE_NONE:
		break;

	case ME_TRIG_TYPE_COUNT:
		if (trigger->iScanStopCount <= 0) {
			PERROR("Invalid scan stop argument specified.\n");
			err = ME_ERRNO_INVALID_SCAN_STOP_ARG;
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
		if (trigger->iScanStopTrigType != ME_TRIG_TYPE_NONE) {
			PERROR("Invalid acq stop trigger type specified.\n");
			err = ME_ERRNO_INVALID_ACQ_STOP_TRIG_TYPE;
			goto ERROR;
		}
		break;

	case ME_TRIG_TYPE_FOLLOW:
		if (trigger->iScanStopTrigType != ME_TRIG_TYPE_COUNT) {
			PERROR("Invalid acq stop trigger type specified.\n");
			err = ME_ERRNO_INVALID_ACQ_STOP_TRIG_TYPE;
			goto ERROR;
		}
		break;

	case ME_TRIG_TYPE_COUNT:
		if (trigger->iScanStopTrigType != ME_TRIG_TYPE_NONE) {
			PERROR("Invalid acq stop trigger type specified.\n");
			err = ME_ERRNO_INVALID_ACQ_STOP_TRIG_TYPE;
			goto ERROR;
		}

		if (trigger->iAcqStopCount <= 0) {
			PERROR
			    ("Invalid acquisition or scan stop argument specified.\n");
			err = ME_ERRNO_INVALID_ACQ_STOP_ARG;
			goto ERROR;
		}
		break;

	default:
		PERROR("Invalid acq stop trigger type specified.\n");
		err = ME_ERRNO_INVALID_ACQ_STOP_TRIG_TYPE;
		goto ERROR;
		break;
	}

	if ((count <= 0) || (count > ME4600_AI_LIST_COUNT)) {
		PERROR("Invalid channel list count specified.\n");
		err = ME_ERRNO_INVALID_CONFIG_LIST_COUNT;
		goto ERROR;
	}
///This is general limitation
//      if (fifo_irq_threshold < 0 || fifo_irq_threshold >= ME4600_AI_CIRC_BUF_COUNT)
///This is limitation from Windows. I use it for compatibility.
	if (fifo_irq_threshold < 0
	    || fifo_irq_threshold >= ME4600_AI_FIFO_COUNT) {
		PERROR("Invalid fifo irq threshold specified.\n");
		err = ME_ERRNO_INVALID_FIFO_IRQ_THRESHOLD;
		goto ERROR;
	}

	if ((config_list[0].iRef == ME_REF_AI_DIFFERENTIAL)
	    && (instance->channels == 16)) {
		PERROR
		    ("Differential reference is not available on this subdevice.\n");
		err = ME_ERRNO_INVALID_REF;
		goto ERROR;
	}

	if (flags & ME_IO_STREAM_CONFIG_SAMPLE_AND_HOLD) {
		if (!instance->sh) {
			PERROR
			    ("Sample and hold is not available for this board.\n");
			err = ME_ERRNO_INVALID_FLAGS;
			goto ERROR;
		}
		if (config_list[0].iRef == ME_REF_AI_DIFFERENTIAL) {
			PERROR
			    ("Sample and hold is not available in differential mode.\n");
			err = ME_ERRNO_INVALID_FLAGS;
			goto ERROR;
		}
	}

	for (i = 0; i < count; i++) {
		if ((config_list[i].iStreamConfig < 0)
		    || (config_list[i].iStreamConfig >= instance->ranges_len)) {
			PERROR("Invalid stream config specified.\n");
			err = ME_ERRNO_INVALID_STREAM_CONFIG;
			goto ERROR;
		}

		if ((config_list[i].iRef != ME_REF_AI_GROUND)
		    && (config_list[i].iRef != ME_REF_AI_DIFFERENTIAL)) {
			PERROR("Invalid references in the list. Ref=0x%x\n",
			       config_list[i].iRef);
			err = ME_ERRNO_INVALID_REF;
			goto ERROR;
		}

		if (config_list[i].iStreamConfig % 2) {	// StreamConfig: 1 or 3
			if (config_list[i].iRef == ME_REF_AI_DIFFERENTIAL) {
				PERROR
				    ("Only bipolar modes support differential measurement.\n");
				err = ME_ERRNO_INVALID_REF;
				goto ERROR;
			}
		}

		if (config_list[i].iRef != config_list[0].iRef) {
			PERROR
			    ("Not all references in the configuration list are equal. Ref[0]=0x%x Ref[%d]=0x%x\n",
			     config_list[0].iRef, i, config_list[i].iRef);
			err = ME_ERRNO_INVALID_REF;
			goto ERROR;
		}

		if ((config_list[i].iRef == ME_REF_AI_DIFFERENTIAL)
		    && (config_list[i].iChannel >= 16)) {
			PERROR("Channel not available in differential mode.\n");
			err = ME_ERRNO_INVALID_CHANNEL;
			goto ERROR;
		}

		if ((config_list[i].iChannel < 0)
		    || (config_list[i].iChannel >= instance->channels)) {
			PERROR("Invalid channel number specified.\n");
			err = ME_ERRNO_INVALID_CHANNEL;
			goto ERROR;
		}
	}

	// Check settings - end

	//Cancel control task
	PDEBUG("Cancel control task.\n");
	instance->ai_control_task_flag = 0;
	cancel_delayed_work(&instance->ai_control_task);

	// Work around from Keith Hartley - begin
	if (trigger->iScanStartTrigType == ME_TRIG_TYPE_TIMER) {
		if (count == 1) {
			// The hardware does not work properly with a non-zero scan time
			// if there is only ONE channel in the channel list. In this case
			// we must set the scan time to zero and use the channel time.

			conv_ticks = scan_ticks;
			trigger->iScanStartTrigType = ME_TRIG_TYPE_FOLLOW;
		} else if (scan_ticks == count * conv_ticks) {
			// Another hardware problem. If the number of scan ticks is
			// exactly equal to the number of channel ticks multiplied by
			// the number of channels then the sampling rate is reduced
			// by half.
			trigger->iScanStartTrigType = ME_TRIG_TYPE_FOLLOW;
		}
	}
	// Work around from Keith Hartley - end

	spin_lock_irqsave(&instance->subdevice_lock, cpu_flags);

	if (inl(instance->status_reg) & ME4600_AI_STATUS_BIT_FSM) {
		PERROR("Subdevice is busy.\n");
		spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);
		ME_SUBDEVICE_EXIT;
		return ME_ERRNO_SUBDEVICE_BUSY;
	}

	instance->status = ai_status_none;
	spin_lock_irqsave(instance->ctrl_reg_lock, cpu_flags);
	// Stop all actions. Block all interrupts. Clear (disable) FIFOs.
	ctrl =
	    ME4600_AI_CTRL_BIT_LE_IRQ_RESET | ME4600_AI_CTRL_BIT_HF_IRQ_RESET |
	    ME4600_AI_CTRL_BIT_SC_IRQ_RESET;

	tmp = inl(instance->ctrl_reg);
	// Preserve EXT IRQ and OFFSET settings. Clean other bits.
	tmp &=
	    (ME4600_AI_CTRL_BIT_EX_IRQ | ME4600_AI_CTRL_BIT_EX_IRQ_RESET |
	     ME4600_AI_CTRL_BIT_FULLSCALE | ME4600_AI_CTRL_BIT_OFFSET);

	// Send it to register.
	outl(tmp | ctrl, instance->ctrl_reg);
	PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->ctrl_reg - instance->reg_base, tmp | ctrl);

	// Enable channel fifo -> data fifo in stream_start().
	ctrl |= ME4600_AI_CTRL_BIT_CHANNEL_FIFO;
	outl(tmp | ctrl, instance->ctrl_reg);
	PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->ctrl_reg - instance->reg_base, tmp | ctrl);
	spin_unlock_irqrestore(instance->ctrl_reg_lock, cpu_flags);

	// Write the channel list
	for (i = 0; i < count; i++) {
		entry = config_list[i].iChannel;

		switch (config_list[i].iStreamConfig) {
		case 0:	//BIPOLAR 10V
/*
				// ME4600_AI_LIST_RANGE_BIPOLAR_10 = 0x0000
				// 'entry |= ME4600_AI_LIST_RANGE_BIPOLAR_10' <== Do nothing. Removed.
				entry |= ME4600_AI_LIST_RANGE_BIPOLAR_10;
*/
			break;
		case 1:	//UNIPOLAR 10V
			entry |= ME4600_AI_LIST_RANGE_UNIPOLAR_10;
			break;
		case 2:	//BIPOLAR 2.5V
			entry |= ME4600_AI_LIST_RANGE_BIPOLAR_2_5;
			break;
		case 3:	//UNIPOLAR 2.5V
			entry |= ME4600_AI_LIST_RANGE_UNIPOLAR_2_5;
			break;
		default:
			PERROR_CRITICAL("UNCHECK ERROR in config_list!\n");
			PERROR_CRITICAL
			    ("WRONG range\nPosition:%d Range:0x%04X\n", i,
			     config_list[i].iStreamConfig);
			goto VERIFY_ERROR;
			break;
		}

		switch (config_list[i].iRef) {
		case ME_REF_AI_GROUND:	//SINGLE ENDED
/*
				// ME4600_AI_LIST_INPUT_SINGLE_ENDED = 0x0000
				// 'entry |= ME4600_AI_LIST_INPUT_SINGLE_ENDED' ==> Do nothing. Removed.
				entry |= ME4600_AI_LIST_INPUT_SINGLE_ENDED;
*/ break;
		case ME_REF_AI_DIFFERENTIAL:	//DIFFERENTIAL
			entry |= ME4600_AI_LIST_INPUT_DIFFERENTIAL;
			break;
		default:
			PERROR_CRITICAL("UNCHECK ERROR in config_list!\n");
			PERROR_CRITICAL
			    ("WRONG reference\nPosition:%d Reference:0x%04X\n",
			     i, config_list[i].iRef);
			goto VERIFY_ERROR;
			break;
		}

		//Add last entry flag
		if (i == (count - 1)) {
			entry |= ME4600_AI_LIST_LAST_ENTRY;
		}

		outl(entry, instance->channel_list_reg);
		PDEBUG_REG("channel_list_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->channel_list_reg - instance->reg_base,
			   entry);
	}

	// Set triggering registers
	--acq_ticks;
	outl(acq_ticks, instance->chan_pre_timer_reg);
	PDEBUG_REG("chan_pre_timer_reg outl(0x%lX+0x%lX)=0x%llX\n",
		   instance->reg_base,
		   instance->chan_pre_timer_reg - instance->reg_base,
		   acq_ticks);
	outl(acq_ticks, instance->scan_pre_timer_low_reg);
	PDEBUG_REG("scan_pre_timer_low_reg outl(0x%lX+0x%lX)=0x%llX\n",
		   instance->reg_base,
		   instance->scan_pre_timer_low_reg - instance->reg_base,
		   acq_ticks & 0xFFFFFFFF);
	outl((acq_ticks >> 32), instance->scan_pre_timer_high_reg);
	PDEBUG_REG("scan_pre_timer_high_reg outl(0x%lX+0x%lX)=0x%llX\n",
		   instance->reg_base,
		   instance->scan_pre_timer_high_reg - instance->reg_base,
		   (acq_ticks >> 32) & 0xFFFFFFFF);

	// Set triggers
	switch (trigger->iAcqStartTrigType) {
		// Internal
	case ME_TRIG_TYPE_SW:
		// Nothing to set.
		break;

		// External
	case ME_TRIG_TYPE_EXT_ANALOG:
		ctrl |= ME4600_AI_CTRL_BIT_EX_TRIG_ANALOG;
	case ME_TRIG_TYPE_EXT_DIGITAL:
		ctrl |= ME4600_AI_CTRL_BIT_EX_TRIG;

		// External trigger needs edge's definition
		switch (trigger->iAcqStartTrigEdge) {
		case ME_TRIG_EDGE_RISING:
			// Nothing to set.
			break;

		case ME_TRIG_EDGE_FALLING:
			ctrl |= ME4600_AI_CTRL_BIT_EX_TRIG_FALLING;
			break;

		case ME_TRIG_EDGE_ANY:
			ctrl |=
			    ME4600_AI_CTRL_BIT_EX_TRIG_FALLING |
			    ME4600_AI_CTRL_BIT_EX_TRIG_BOTH;
			break;

		default:
			PERROR_CRITICAL
			    ("UNCHECK TRIGGER EDGE in triggers structure!\n");
			PERROR_CRITICAL
			    ("WRONG acquisition start trigger:0x%04X.\n",
			     trigger->iAcqStartTrigEdge);
			err = ME_ERRNO_INVALID_ACQ_START_TRIG_EDGE;
			goto VERIFY_ERROR;
			break;
		}
		break;

	default:
		PERROR_CRITICAL("UNCHECK TRIGGER in triggers structure!\n");
		PERROR_CRITICAL("WRONG acquisition start trigger:0x%04X.\n",
				trigger->iAcqStartTrigType);
		err = ME_ERRNO_INVALID_ACQ_START_TRIG_TYPE;
		goto VERIFY_ERROR;
		break;
	}

	switch (trigger->iScanStartTrigType) {
	case ME_TRIG_TYPE_TIMER:
		--scan_ticks;
		outl(scan_ticks, instance->scan_timer_low_reg);
		PDEBUG_REG("scan_timer_low_reg outl(0x%lX+0x%lX)=0x%llX\n",
			   instance->reg_base,
			   instance->scan_timer_low_reg - instance->reg_base,
			   scan_ticks & 0xFFFFFFFF);
		outl((scan_ticks >> 32), instance->scan_timer_high_reg);
		PDEBUG_REG("scan_timer_high_reg outl(0x%lX+0x%lX)=0x%llX\n",
			   instance->reg_base,
			   instance->scan_timer_high_reg - instance->reg_base,
			   (scan_ticks >> 32) & 0xFFFFFFFF);

		if (trigger->iAcqStartTrigType == ME_TRIG_TYPE_SW) {
			ctrl |= ME4600_AI_CTRL_BIT_MODE_0;
		} else {
			ctrl |= ME4600_AI_CTRL_BIT_MODE_1;
		}
		break;

	case ME_TRIG_TYPE_EXT_DIGITAL:
	case ME_TRIG_TYPE_EXT_ANALOG:
		outl(0, instance->scan_timer_low_reg);
		PDEBUG_REG("scan_timer_low_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->scan_timer_low_reg - instance->reg_base,
			   0);
		outl(0, instance->scan_timer_high_reg);
		PDEBUG_REG("scan_timer_high_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->scan_timer_high_reg - instance->reg_base,
			   0);
		ctrl |= ME4600_AI_CTRL_BIT_MODE_2;
		break;

	case ME_TRIG_TYPE_FOLLOW:
		outl(0, instance->scan_timer_low_reg);
		PDEBUG_REG("scan_timer_low_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->scan_timer_low_reg - instance->reg_base,
			   0);
		outl(0, instance->scan_timer_high_reg);
		PDEBUG_REG("scan_timer_high_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->scan_timer_high_reg - instance->reg_base,
			   0);

		if (trigger->iAcqStartTrigType == ME_TRIG_TYPE_SW) {
			ctrl |= ME4600_AI_CTRL_BIT_MODE_0;
		} else {
			ctrl |= ME4600_AI_CTRL_BIT_MODE_1;
		}
		break;

	default:
		PERROR_CRITICAL("UNCHECK TRIGGER in triggers structure!\n");
		PERROR_CRITICAL("WRONG scan start trigger:0x%04X.\n",
				trigger->iScanStartTrigType);
		err = ME_ERRNO_INVALID_SCAN_START_TRIG_TYPE;
		goto VERIFY_ERROR;
		break;
	}

	switch (trigger->iConvStartTrigType) {

	case ME_TRIG_TYPE_TIMER:
		--conv_ticks;
		outl(conv_ticks, instance->chan_timer_reg);
		PDEBUG_REG("chan_timer_reg outl(0x%lX+0x%lX)=0x%llX\n",
			   instance->reg_base,
			   instance->chan_timer_reg - instance->reg_base,
			   conv_ticks);
		break;

	case ME_TRIG_TYPE_EXT_DIGITAL:
	case ME_TRIG_TYPE_EXT_ANALOG:
		outl(0, instance->chan_timer_reg);
		PDEBUG_REG("chan_timer_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->chan_timer_reg - instance->reg_base, 0);
		ctrl |= ME4600_AI_CTRL_BIT_MODE_0 | ME4600_AI_CTRL_BIT_MODE_1;
		break;

	default:
		PERROR_CRITICAL("UNCHECK TRIGGER in triggers structure!\n");
		PERROR_CRITICAL("WRONG conv start trigger:0x%04X.\n",
				trigger->iConvStartTrigType);
		err = ME_ERRNO_INVALID_CONV_START_TRIG_TYPE;
		goto VERIFY_ERROR;

		break;
	}

	//Sample & Hold feature
	if (flags & ME_IO_STREAM_CONFIG_SAMPLE_AND_HOLD) {
		if (instance->sh) {
			ctrl |= ME4600_AI_CTRL_BIT_SAMPLE_HOLD;
		} else {
			PERROR_CRITICAL("UNCHECK S&H feature!\n");
			err = ME_ERRNO_INVALID_FLAGS;
			goto VERIFY_ERROR;
		}
	}
	//Enable IRQs sources but leave latches blocked.
	ctrl |= (ME4600_AI_CTRL_BIT_HF_IRQ | ME4600_AI_CTRL_BIT_SC_IRQ | ME4600_AI_CTRL_BIT_LE_IRQ);	//The last IRQ source (ME4600_AI_CTRL_BIT_LE_IRQ) is unused!

	//Everything is good. Finalize
	spin_lock_irqsave(instance->ctrl_reg_lock, cpu_flags);
	tmp = inl(instance->ctrl_reg);

	//Preserve EXT IRQ and OFFSET settings. Clean other bits.
	tmp &=
	    (ME4600_AI_CTRL_BIT_EX_IRQ | ME4600_AI_CTRL_BIT_EX_IRQ_RESET |
	     ME4600_AI_CTRL_BIT_FULLSCALE | ME4600_AI_CTRL_BIT_OFFSET);

	// write the control word
	outl(ctrl | tmp, instance->ctrl_reg);
	PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->ctrl_reg - instance->reg_base, ctrl | tmp);
	spin_unlock_irqrestore(instance->ctrl_reg_lock, cpu_flags);

	//Set the global parameters end exit.
	instance->chan_list_len = count;
	instance->fifo_irq_threshold = fifo_irq_threshold;

	if (trigger->iAcqStopTrigType == ME_TRIG_TYPE_COUNT) {
		data_required =
		    (unsigned long long)trigger->iAcqStopCount *
		    (unsigned long long)count;
		if (data_required > UINT_MAX)
			data_required = UINT_MAX;
		instance->data_required = (unsigned int)data_required;
	} else if (trigger->iScanStopTrigType == ME_TRIG_TYPE_COUNT)
		instance->data_required =
		    (unsigned long long)trigger->iScanStopCount;
	else
		instance->data_required = 0;

	// Mark subdevice as configured to work in stream mode.
	instance->status = ai_status_stream_configured;

	// Deinit single config. Set all entries to NOT_CONFIGURED.
	for (i = 0; i < instance->channels; i++) {
		instance->single_config[i].status =
		    ME_SINGLE_CHANNEL_NOT_CONFIGURED;
	}

VERIFY_ERROR:		// Error in code. Wrong setting check. This should never ever happend!
	spin_unlock_irqrestore(&instance->subdevice_lock, cpu_flags);
ERROR:			// Error in settings.
	ME_SUBDEVICE_EXIT;

	return err;
}

static int me4600_ai_io_stream_new_values(me_subdevice_t *subdevice,
					  struct file *filep,
					  int time_out, int *count, int flags)
{
	me4600_ai_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	unsigned long t;
	unsigned long j;
	int volatile head;

	PDEBUG("executed. idx=0\n");

	if (flags) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	if (time_out < 0) {
		PERROR("Invalid time_out specified.\n");
		return ME_ERRNO_INVALID_TIMEOUT;
	}

	if (time_out) {
		t = (time_out * HZ) / 1000;

		if (t == 0)
			t = 1;
	} else {		// Max time.
		t = LONG_MAX;
	}

	instance = (me4600_ai_subdevice_t *) subdevice;

	ME_SUBDEVICE_ENTER;

	j = jiffies;

	while (1) {
		// Only runing device can generate break.
		head = instance->circ_buf.head;
		wait_event_interruptible_timeout(instance->wait_queue,
						 ((head !=
						   instance->circ_buf.head)
						  ||
						  ((instance->status <=
						    ai_status_stream_run_wait)
						   && (instance->status >=
						       ai_status_stream_end_wait))),
						 t);

		if (head != instance->circ_buf.head) {	// New data in buffer.
			break;
		} else if (instance->status == ai_status_stream_end) {	// End of work.
			break;
		} else if (instance->status == ai_status_stream_fifo_error) {
			err = ME_ERRNO_FIFO_BUFFER_OVERFLOW;
			break;
		} else if (instance->status == ai_status_stream_buffer_error) {
			err = ME_ERRNO_RING_BUFFER_OVERFLOW;
			break;
		} else if (instance->status == ai_status_stream_error) {
			err = ME_ERRNO_INTERNAL;
			break;
		} else if ((jiffies - j) >= t) {
			PERROR("Wait on values timed out.\n");
			err = ME_ERRNO_TIMEOUT;
			break;
		} else if (signal_pending(current)) {
			PERROR("Wait on values interrupted from signal.\n");
			err = ME_ERRNO_SIGNAL;
			break;
		}
		// Correct timeout.
		t -= jiffies - j;
	}

	*count = me_circ_buf_values(&instance->circ_buf);

	ME_SUBDEVICE_EXIT;

	return err;
}

static inline int me4600_ai_io_stream_read_get_value(me4600_ai_subdevice_t *
						     instance, int *values,
						     const int count,
						     const int flags)
{
	int n;
	int i;
	uint32_t value;

	///Checking how many datas can be copied.
	n = me_circ_buf_values(&instance->circ_buf);
	if (n <= 0)
		return 0;

	if (n > count)
		n = count;

	if (flags & ME_IO_STREAM_READ_FRAMES) {
		if (n < instance->chan_list_len)	//Not enough data!
			return 0;
		n -= n % instance->chan_list_len;
	}

	for (i = 0; i < n; i++) {
		value = *(instance->circ_buf.buf + instance->circ_buf.tail);
		if (put_user(value, values + i)) {
			PERROR("Cannot copy new values to user.\n");
			return -ME_ERRNO_INTERNAL;
		}
		instance->circ_buf.tail++;
		instance->circ_buf.tail &= instance->circ_buf.mask;
	}
	return n;
}

static int me4600_ai_io_stream_read(me_subdevice_t *subdevice,
				    struct file *filep,
				    int read_mode,
				    int *values, int *count, int flags)
{
	me4600_ai_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	int ret;

	int c = *count;
	int min = c;

	PDEBUG("executed. idx=0\n");

	if (flags & ~ME_IO_STREAM_READ_FRAMES) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	if (!values || !count) {
		PERROR("Request has invalid pointer.\n");
		return ME_ERRNO_INVALID_POINTER;
	}

	if (c < 0) {
		PERROR("Request has invalid value's counter.\n");
		return ME_ERRNO_INVALID_VALUE_COUNT;
	}

	if ((read_mode != ME_READ_MODE_BLOCKING)
	    && (read_mode != ME_READ_MODE_NONBLOCKING)) {
		PERROR("Invalid read mode specified.\n");
		return ME_ERRNO_INVALID_READ_MODE;
	}

	if (c == 0) {		//You get what you want! Nothing more or less.
		return ME_ERRNO_SUCCESS;
	}

	instance = (me4600_ai_subdevice_t *) subdevice;
	ME_SUBDEVICE_ENTER;

	//Check if subdevice is configured.
	if (instance->chan_list_len <= 0) {
		PERROR("Subdevice wasn't configured.\n");
		ME_SUBDEVICE_EXIT;
		return ME_ERRNO_PREVIOUS_CONFIG;
	}

	if (flags & ME_IO_STREAM_READ_FRAMES) {
		if (c < instance->chan_list_len) {	//Not enough data requested.
			PERROR
			    ("When using FRAME_READ mode minimal size is defined by channel list.\n");
			ME_SUBDEVICE_EXIT;
			return ME_ERRNO_INVALID_VALUE_COUNT;
		}
	}

	if (c > (ME4600_AI_CIRC_BUF_COUNT - instance->chan_list_len)) {	// To return acceptable amount of data when user pass too big value.
		min = ME4600_AI_CIRC_BUF_COUNT - instance->chan_list_len;
	}

	if (flags & ME_IO_STREAM_READ_FRAMES) {
		//Wait for whole list.
		if (read_mode == ME_READ_MODE_BLOCKING) {
			min = c - (c % instance->chan_list_len);
		}

		if (read_mode == ME_READ_MODE_NONBLOCKING) {
			min = instance->chan_list_len;
		}
	}

	if ((inl(instance->status_reg) & ME4600_AI_STATUS_BIT_FSM)) {	//Working
		//If blocking mode -> wait for data.
		if ((me_circ_buf_values(&instance->circ_buf) < min)
		    && (read_mode == ME_READ_MODE_BLOCKING)) {
			wait_event_interruptible(instance->wait_queue,
						 ((me_circ_buf_values
						   (&instance->circ_buf) >= min)
						  || !(inl(instance->status_reg)
						       &
						       ME4600_AI_STATUS_BIT_FSM)));

			if (signal_pending(current)) {
				PERROR
				    ("Wait on values interrupted from signal.\n");
				err = ME_ERRNO_SIGNAL;
			}
		}
	}

	ret = me4600_ai_io_stream_read_get_value(instance, values, c, flags);
	if (ret < 0) {
		err = -ret;
		*count = 0;
	} else if (ret == 0) {
		*count = 0;
		if (instance->status == ai_status_stream_fifo_error) {
			err = ME_ERRNO_FIFO_BUFFER_OVERFLOW;
			instance->status = ai_status_stream_end;
		} else if (instance->status == ai_status_stream_buffer_error) {
			err = ME_ERRNO_RING_BUFFER_OVERFLOW;
			instance->status = ai_status_stream_end;
		} else if (instance->status == ai_status_stream_end) {
			err = ME_ERRNO_SUBDEVICE_NOT_RUNNING;
		} else if (instance->status == ai_status_stream_error) {
			err = ME_ERRNO_INTERNAL;
		} else if (instance->status == ai_status_none) {
			PDEBUG("Stream canceled.\n");
			err = ME_ERRNO_INTERNAL;
		}
	} else {
		*count = ret;
	}

	ME_SUBDEVICE_EXIT;

	return err;
}

/** @brief Stop aqusation. Preserve FIFOs.
*
* @param instance The subdevice instance (pointer).
*/

static int ai_stop_immediately(me4600_ai_subdevice_t *instance)
{
	unsigned long cpu_flags = 0;
	volatile uint32_t ctrl;
	const int timeout = HZ / 10;	//100ms
	int i;

	for (i = 0; i <= timeout; i++) {
		spin_lock_irqsave(instance->ctrl_reg_lock, cpu_flags);
		ctrl = inl(instance->ctrl_reg);
		ctrl &= ~ME4600_AI_CTRL_BIT_STOP;
		ctrl |=
		    (ME4600_AI_CTRL_BIT_IMMEDIATE_STOP |
		     ME4600_AI_CTRL_BIT_HF_IRQ_RESET |
		     ME4600_AI_CTRL_BIT_SC_IRQ_RESET);
		outl(ctrl, instance->ctrl_reg);
		PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->ctrl_reg - instance->reg_base, ctrl);
		spin_unlock_irqrestore(instance->ctrl_reg_lock, cpu_flags);

		if (!(inl(instance->status_reg) & ME4600_AI_STATUS_BIT_FSM)) {	// Exit.
			break;
		}

		PINFO("Wait for stop: %d\n", i + 1);
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

static int me4600_ai_io_stream_start(me_subdevice_t *subdevice,
				     struct file *filep,
				     int start_mode, int time_out, int flags)
{
	me4600_ai_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	unsigned long cpu_flags = 0;
	unsigned long ref;
	unsigned long delay = 0;

	volatile uint32_t tmp;

	PDEBUG("executed. idx=0\n");

	instance = (me4600_ai_subdevice_t *) subdevice;

	if (flags) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	if ((start_mode != ME_START_MODE_BLOCKING)
	    && (start_mode != ME_START_MODE_NONBLOCKING)) {
		PERROR("Invalid start mode specified.\n");
		return ME_ERRNO_INVALID_START_MODE;
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
	    spin_lock_irqsave(instance->ctrl_reg_lock, cpu_flags);

	tmp = inl(instance->ctrl_reg);

	if ((tmp & ME4600_AI_STATUS_BIT_FSM)) {
		PERROR("Conversion is already running.\n");
		spin_unlock_irqrestore(instance->ctrl_reg_lock, cpu_flags);
		err = ME_ERRNO_SUBDEVICE_BUSY;
		goto ERROR;
	}

	if (instance->chan_list_len == 0) {	//Not configured!
		PERROR("Subdevice is not configured to work in stream mode!\n");
		spin_unlock_irqrestore(instance->ctrl_reg_lock, cpu_flags);
		err = ME_ERRNO_PREVIOUS_CONFIG;
		goto ERROR;
	}

	if (!(tmp & (ME4600_AI_CTRL_BIT_MODE_0 | ME4600_AI_CTRL_BIT_MODE_1 | ME4600_AI_CTRL_BIT_MODE_2))) {	//Mode 0 = single work => no stream config
		PERROR("Subdevice is configured to work in single mode.\n");
		spin_unlock_irqrestore(instance->ctrl_reg_lock, cpu_flags);
		err = ME_ERRNO_PREVIOUS_CONFIG;
		goto ERROR;
	}
	//Reset stop bits.
	tmp |= ME4600_AI_CTRL_BIT_IMMEDIATE_STOP | ME4600_AI_CTRL_BIT_STOP;
	outl(tmp, instance->ctrl_reg);
	PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->ctrl_reg - instance->reg_base, tmp);

	//Start datas' FIFO.
	tmp |= ME4600_AI_CTRL_BIT_DATA_FIFO;
	//Free stop bits.
	tmp &= ~(ME4600_AI_CTRL_BIT_IMMEDIATE_STOP | ME4600_AI_CTRL_BIT_STOP);
	outl(tmp, instance->ctrl_reg);
	PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->ctrl_reg - instance->reg_base, tmp);
	spin_unlock_irqrestore(instance->ctrl_reg_lock, cpu_flags);

	//Cancel control task
	PDEBUG("Cancel control task.\n");
	instance->ai_control_task_flag = 0;
	cancel_delayed_work(&instance->ai_control_task);

	//Set the starting values.
	instance->ISM.global_read = 0;
	instance->ISM.read = 0;
	//Clear circular buffer
	instance->circ_buf.head = 0;
	instance->circ_buf.tail = 0;

	//Set everything.
	ai_data_acquisition_logic(instance);

	//Set status to 'wait for start'
	instance->status = ai_status_stream_run_wait;

	// Set control task's timeout
	instance->timeout.delay = delay;
	instance->timeout.start_time = jiffies;

	//Lets go! Start work
	inl(instance->start_reg);
	PDEBUG_REG("start_reg inl(0x%lX+0x%lX)\n", instance->reg_base,
		   instance->start_reg - instance->reg_base);

	// Schedule control task
	instance->ai_control_task_flag = 1;
	queue_delayed_work(instance->me4600_workqueue,
			   &instance->ai_control_task, 1);

	PDEVELOP("Delay:%ld\n", delay);

	if (start_mode == ME_START_MODE_BLOCKING) {	//Wait for start.
		ref = jiffies;
		//Only runing process will interrupt this call. Events are signaled when status change. Extra timeout add for safe reason.
		wait_event_interruptible_timeout(instance->wait_queue,
						 (instance->status !=
						  ai_status_stream_run_wait),
						 (delay) ? delay +
						 1 : LONG_MAX);

		if ((instance->status != ai_status_stream_run)
		    && (instance->status != ai_status_stream_end)) {
			PDEBUG("Starting stream canceled. %d\n",
			       instance->status);
			err = ME_ERRNO_CANCELLED;
		}

		if (signal_pending(current)) {
			PERROR("Wait on start of state machine interrupted.\n");
			instance->status = ai_status_none;
			ai_stop_isr(instance);
			err = ME_ERRNO_SIGNAL;
		} else if ((delay) && ((jiffies - ref) > delay)) {
			if (instance->status != ai_status_stream_run) {
				if (instance->status == ai_status_stream_end) {
					PDEBUG("Timeout reached.\n");
				} else if ((jiffies - ref) > delay + 1) {
					PERROR
					    ("Timeout reached. Not handled by control task!\n");
					ai_stop_isr(instance);
					instance->status =
					    ai_status_stream_error;
				} else {
					PERROR
					    ("Timeout reached. Signal come but status is strange: %d\n",
					     instance->status);
					ai_stop_isr(instance);
					instance->status =
					    ai_status_stream_error;
				}

				instance->ai_control_task_flag = 0;
				cancel_delayed_work(&instance->ai_control_task);
				err = ME_ERRNO_TIMEOUT;
			}
		}
	}
#ifdef MEDEBUG_INFO
	tmp = inl(instance->ctrl_reg);
	PDEBUG_REG("ctrl_reg inl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->ctrl_reg - instance->reg_base, tmp);

	PINFO("STATUS_BIT_FSM=%s.\n",
	      (tmp & ME4600_AI_STATUS_BIT_FSM) ? "on" : "off");
	PINFO("CTRL_BIT_HF_IRQ=%s.\n",
	      (tmp & ME4600_AI_CTRL_BIT_HF_IRQ) ? "enable" : "disable");
	PINFO("CTRL_BIT_HF_IRQ_RESET=%s.\n",
	      (tmp & ME4600_AI_CTRL_BIT_HF_IRQ_RESET) ? "reset" : "work");
	PINFO("CTRL_BIT_SC_IRQ=%s.\n",
	      (tmp & ME4600_AI_CTRL_BIT_SC_IRQ) ? "enable" : "disable");
	PINFO("CTRL_BIT_SC_RELOAD=%s.\n",
	      (tmp & ME4600_AI_CTRL_BIT_SC_RELOAD) ? "on" : "off");
	PINFO("CTRL_BIT_SC_IRQ_RESET=%s.\n",
	      (tmp & ME4600_AI_CTRL_BIT_SC_IRQ_RESET) ? "reset" : "work");
#endif

ERROR:
	ME_SUBDEVICE_EXIT;

	return err;
}

static int me4600_ai_io_stream_status(me_subdevice_t *subdevice,
				      struct file *filep,
				      int wait,
				      int *status, int *values, int flags)
{
	me4600_ai_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;

	PDEBUG("executed. idx=0\n");

	instance = (me4600_ai_subdevice_t *) subdevice;

	if (flags) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	ME_SUBDEVICE_ENTER;

	switch (instance->status) {
	case ai_status_single_configured:
	case ai_status_stream_configured:
	case ai_status_stream_end:
	case ai_status_stream_fifo_error:
	case ai_status_stream_buffer_error:
	case ai_status_stream_error:
		*status = ME_STATUS_IDLE;
		break;

	case ai_status_stream_run_wait:
	case ai_status_stream_run:
	case ai_status_stream_end_wait:
		*status = ME_STATUS_BUSY;
		break;

	case ai_status_none:
	default:
		*status =
		    (inl(instance->status_reg) & ME4600_AI_STATUS_BIT_FSM) ?
		    ME_STATUS_BUSY : ME_STATUS_IDLE;
		break;
	}

	if ((wait == ME_WAIT_IDLE) && (*status == ME_STATUS_BUSY)) {
		// Only runing process will interrupt this call. Events are signaled when status change. Extra timeout add for safe reason.
		wait_event_interruptible_timeout(instance->wait_queue,
						 ((instance->status !=
						   ai_status_stream_run_wait)
						  && (instance->status !=
						      ai_status_stream_run)
						  && (instance->status !=
						      ai_status_stream_end_wait)),
						 LONG_MAX);

		if (instance->status != ai_status_stream_end) {
			PDEBUG("Wait for IDLE canceled. %d\n",
			       instance->status);
			err = ME_ERRNO_CANCELLED;
		}

		if (signal_pending(current)) {
			PERROR("Wait for IDLE interrupted.\n");
			instance->status = ai_status_none;
			ai_stop_isr(instance);
			err = ME_ERRNO_SIGNAL;
		}

		*status = ME_STATUS_IDLE;
	}

	*values = me_circ_buf_values(&instance->circ_buf);
	PDEBUG("me_circ_buf_values(&instance->circ_buf)=%d.\n", *values);

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me4600_ai_io_stream_stop(me_subdevice_t *subdevice,
				    struct file *filep,
				    int stop_mode, int flags)
{
/**
 @note Stop is implemented only in blocking mode.
 @note Function return when state machine is stoped.
*/
	me4600_ai_subdevice_t *instance;
	unsigned long cpu_flags;
	uint32_t ctrl;
	int ret;

	PDEBUG("executed. idx=0\n");

	if (flags) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	if ((stop_mode != ME_STOP_MODE_IMMEDIATE)
	    && (stop_mode != ME_STOP_MODE_LAST_VALUE)) {
		PERROR("Invalid stop mode specified.\n");
		return ME_ERRNO_INVALID_STOP_MODE;
	}

	instance = (me4600_ai_subdevice_t *) subdevice;

	ME_SUBDEVICE_ENTER;

	// Mark as stopping. => Software stop.
	instance->status = ai_status_stream_end_wait;

	if (stop_mode == ME_STOP_MODE_IMMEDIATE) {
		ret = ai_stop_immediately(instance);

		if (ret) {
			PERROR("FSM is still busy.\n");
			ME_SUBDEVICE_EXIT;
			return ME_ERRNO_SUBDEVICE_BUSY;
		}
		instance->ai_control_task_flag = 0;

	} else if (stop_mode == ME_STOP_MODE_LAST_VALUE) {
		// Set stop bit in registry.
		spin_lock_irqsave(instance->ctrl_reg_lock, cpu_flags);
		ctrl = inl(instance->ctrl_reg);
		ctrl |= ME4600_AI_CTRL_BIT_STOP;
		outl(ctrl, instance->ctrl_reg);
		PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->ctrl_reg - instance->reg_base, ctrl);
		spin_unlock_irqrestore(instance->ctrl_reg_lock, cpu_flags);

		// Only runing process will interrupt this call. Events are signaled when status change.
		wait_event_interruptible_timeout(instance->wait_queue,
						 (instance->status !=
						  ai_status_stream_end_wait),
						 LONG_MAX);

		if (instance->status != ai_status_stream_end) {
			PDEBUG("Stopping stream canceled.\n");
			ret = ME_ERRNO_CANCELLED;
		}

		if (signal_pending(current)) {
			PERROR("Stopping stream interrupted.\n");
			instance->status = ai_status_none;
			ret = ME_ERRNO_SIGNAL;
		}
		// End of work.
		ai_stop_immediately(instance);

	}

	ret = ai_read_data_pooling(instance);
	if (ret > 0) {		// Everything fine. More datas put to software buffer.
		instance->status = ai_status_stream_end;
		ret = ME_ERRNO_SUCCESS;
		// Signal that we put last data to software buffer.
		wake_up_interruptible_all(&instance->wait_queue);
	} else if (ret == 0) {	// Everything fine. No more datas in FIFO.
		instance->status = ai_status_stream_end;
		ret = ME_ERRNO_SUCCESS;
	} else if (ret == -ME_ERRNO_RING_BUFFER_OVERFLOW) {	// Stop is unsuccessful, buffer is overflow.
		instance->status = ai_status_stream_buffer_error;
		ret = ME_ERRNO_SUCCESS;
	} else {		// Stop is unsuccessful
		instance->status = ai_status_stream_end;
		ret = -ret;
	}

	ME_SUBDEVICE_EXIT;

	return ret;
}

static int me4600_ai_query_range_by_min_max(me_subdevice_t *subdevice,
					    int unit,
					    int *min,
					    int *max, int *maxdata, int *range)
{
	me4600_ai_subdevice_t *instance;
	int i;
	int r = -1;
	int diff = 21E6;

	PDEBUG("executed. idx=0\n");

	instance = (me4600_ai_subdevice_t *) subdevice;

	if ((*max - *min) < 0) {
		PERROR("Invalid minimum and maximum values specified.\n");
		return ME_ERRNO_INVALID_MIN_MAX;
	}

	if ((unit == ME_UNIT_VOLT) || (unit == ME_UNIT_ANY)) {
		for (i = 0; i < instance->ranges_len; i++) {
			if ((instance->ranges[i].min <= *min)
			    && ((instance->ranges[i].max + 1000) >= *max)) {
				if ((instance->ranges[i].max -
				     instance->ranges[i].min) - (*max - *min) <
				    diff) {
					r = i;
					diff =
					    (instance->ranges[i].max -
					     instance->ranges[i].min) - (*max -
									 *min);
				}
			}
		}

		if (r < 0) {
			PERROR("No matching range found.\n");
			return ME_ERRNO_NO_RANGE;
		} else {
			*min = instance->ranges[r].min;
			*max = instance->ranges[r].max;
			*maxdata = ME4600_AI_MAX_DATA;
			*range = r;
		}
	} else {
		PERROR("Invalid physical unit specified.\n");
		return ME_ERRNO_INVALID_UNIT;
	}

	return ME_ERRNO_SUCCESS;
}

static int me4600_ai_query_number_ranges(me_subdevice_t *subdevice,
					 int unit, int *count)
{
	me4600_ai_subdevice_t *instance;

	PDEBUG("executed. idx=0\n");

	instance = (me4600_ai_subdevice_t *) subdevice;

	if ((unit == ME_UNIT_VOLT) || (unit == ME_UNIT_ANY)) {
		*count = instance->ranges_len;
	} else {
		*count = 0;
	}

	return ME_ERRNO_SUCCESS;
}

static int me4600_ai_query_range_info(me_subdevice_t *subdevice,
				      int range,
				      int *unit,
				      int *min, int *max, int *maxdata)
{
	me4600_ai_subdevice_t *instance;

	PDEBUG("executed. idx=0\n");

	instance = (me4600_ai_subdevice_t *) subdevice;

	if ((range < instance->ranges_len) && (range >= 0)) {
		*unit = ME_UNIT_VOLT;
		*min = instance->ranges[range].min;
		*max = instance->ranges[range].max;
		*maxdata = ME4600_AI_MAX_DATA;
	} else {
		PERROR("Invalid range number specified.\n");
		return ME_ERRNO_INVALID_RANGE;
	}

	return ME_ERRNO_SUCCESS;
}

static int me4600_ai_query_timer(me_subdevice_t *subdevice,
				 int timer,
				 int *base_frequency,
				 long long *min_ticks, long long *max_ticks)
{
	me4600_ai_subdevice_t *instance;

	PDEBUG("executed. idx=0\n");

	instance = (me4600_ai_subdevice_t *) subdevice;

	switch (timer) {

	case ME_TIMER_ACQ_START:
		*base_frequency = ME4600_AI_BASE_FREQUENCY;
		*min_ticks = ME4600_AI_MIN_ACQ_TICKS;
		*max_ticks = ME4600_AI_MAX_ACQ_TICKS;
		break;

	case ME_TIMER_SCAN_START:
		*base_frequency = ME4600_AI_BASE_FREQUENCY;
		*min_ticks = ME4600_AI_MIN_SCAN_TICKS;
		*max_ticks = ME4600_AI_MAX_SCAN_TICKS;
		break;

	case ME_TIMER_CONV_START:
		*base_frequency = ME4600_AI_BASE_FREQUENCY;
		*min_ticks = ME4600_AI_MIN_CHAN_TICKS;
		*max_ticks = ME4600_AI_MAX_CHAN_TICKS;
		break;

	default:
		PERROR("Invalid timer specified.(0x%04x)\n", timer);

		return ME_ERRNO_INVALID_TIMER;
	}

	return ME_ERRNO_SUCCESS;
}

static int me4600_ai_query_number_channels(me_subdevice_t *subdevice,
					   int *number)
{
	me4600_ai_subdevice_t *instance;

	PDEBUG("executed. idx=0\n");

	instance = (me4600_ai_subdevice_t *) subdevice;
	*number = instance->channels;

	return ME_ERRNO_SUCCESS;
}

static int me4600_ai_query_subdevice_type(me_subdevice_t *subdevice,
					  int *type, int *subtype)
{
	PDEBUG("executed. idx=0\n");

	*type = ME_TYPE_AI;
	*subtype = ME_SUBTYPE_STREAMING;

	return ME_ERRNO_SUCCESS;
}

static int me4600_ai_query_subdevice_caps(me_subdevice_t *subdevice, int *caps)
{
	PDEBUG("executed. idx=0\n");

	*caps =
	    ME_CAPS_AI_TRIG_SYNCHRONOUS | ME_CAPS_AI_FIFO |
	    ME_CAPS_AI_FIFO_THRESHOLD;

	return ME_ERRNO_SUCCESS;
}

static int me4600_ai_query_subdevice_caps_args(struct me_subdevice *subdevice,
					       int cap, int *args, int count)
{
	me4600_ai_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;

	instance = (me4600_ai_subdevice_t *) subdevice;

	PDEBUG("executed. idx=0\n");

	if (count != 1) {
		PERROR("Invalid capability argument count.\n");
		return ME_ERRNO_INVALID_CAP_ARG_COUNT;
	}

	switch (cap) {
	case ME_CAP_AI_FIFO_SIZE:
		args[0] = ME4600_AI_FIFO_COUNT;
		break;

	case ME_CAP_AI_BUFFER_SIZE:
		args[0] =
		    (instance->circ_buf.buf) ? ME4600_AI_CIRC_BUF_COUNT : 0;
		break;

	default:
		PERROR("Invalid capability.\n");
		err = ME_ERRNO_INVALID_CAP;
		args[0] = 0;
	}

	return err;
}

void ai_limited_isr(me4600_ai_subdevice_t *instance, const uint32_t irq_status,
		    const uint32_t ctrl_status)
{
	int to_read;

	if (!instance->fifo_irq_threshold) {	//No threshold provided. SC ends work. HF need reseting.
		if (irq_status & ME4600_IRQ_STATUS_BIT_SC) {
			if (ai_read_data(instance, instance->ISM.next) != instance->ISM.next) {	//ERROR!
				PERROR
				    ("Limited amounts aqusition with TH=0: Circular buffer full!\n");
				instance->status =
				    ai_status_stream_buffer_error;
			} else {
				instance->status = ai_status_stream_end;
			}
			//End of work.
			ai_stop_isr(instance);
		} else if (irq_status & ME4600_IRQ_STATUS_BIT_AI_HF) {
			instance->ISM.global_read += ME4600_AI_FIFO_HALF;

			if (ai_read_data(instance, ME4600_AI_FIFO_HALF) != ME4600_AI_FIFO_HALF) {	//ERROR!
				PERROR
				    ("Limited amounts aqusition with TH = 0: Circular buffer full!\n");
				//End of work.
				ai_stop_isr(instance);
				instance->status =
				    ai_status_stream_buffer_error;
			} else {
				//Continue.
				ai_limited_ISM(instance, irq_status);
			}
		}
		//Signal user.
		wake_up_interruptible_all(&instance->wait_queue);
	} else			//if(instance->fifo_irq_threshold)
	{
		if (irq_status & ME4600_IRQ_STATUS_BIT_SC) {
			instance->ISM.read = 0;
			if ((instance->fifo_irq_threshold < ME4600_AI_FIFO_HALF)
			    && (!(ctrl_status & ME4600_AI_STATUS_BIT_HF_DATA)))
			{
				to_read =
				    ME4600_AI_FIFO_HALF -
				    (ME4600_AI_FIFO_HALF %
				     instance->fifo_irq_threshold);
				PDEBUG
				    ("Limited amounts aqusition with TH != 0: Not fast enough data aqusition! correction=%d\n",
				     to_read);
			} else {
				to_read = instance->ISM.next;
			}
			instance->ISM.global_read += to_read;

			ai_reschedule_SC(instance);

			if (ai_read_data(instance, to_read) != to_read) {	//ERROR!
				PERROR
				    ("Limited amounts aqusition with TH != 0: Circular buffer full!\n");
				//End of work.
				ai_stop_isr(instance);
				instance->status =
				    ai_status_stream_buffer_error;
			} else {
				//Continue.
				ai_limited_ISM(instance, irq_status);
			}

			//Signal user.
			wake_up_interruptible_all(&instance->wait_queue);
		} else if (irq_status & ME4600_IRQ_STATUS_BIT_AI_HF) {
			instance->ISM.read += ME4600_AI_FIFO_HALF;
			instance->ISM.global_read += ME4600_AI_FIFO_HALF;

			if (ai_read_data(instance, ME4600_AI_FIFO_HALF) != ME4600_AI_FIFO_HALF) {	//ERROR!
				PERROR
				    ("Limited amounts aqusition with TH != 0: Circular buffer full!\n");
				ai_stop_isr(instance);

				instance->status =
				    ai_status_stream_buffer_error;
				//Signal user.
				wake_up_interruptible_all(&instance->
							  wait_queue);
			} else {
				//Countinue.
				ai_limited_ISM(instance, irq_status);
			}
		}

		if (instance->ISM.global_read >= instance->data_required) {	//End of work. Next paranoid pice of code: '>=' instead od '==' only to be sure.
			ai_stop_isr(instance);
			if (instance->status < ai_status_stream_end) {
				instance->status = ai_status_stream_end;
			}
#ifdef MEDEBUG_ERROR
			if (instance->ISM.global_read > instance->data_required) {	//This is security check case. This should never ever happend!
				PERROR
				    ("Limited amounts aqusition: Read more data than necessary! data_required=%d < read=%d\n",
				     instance->data_required,
				     instance->ISM.global_read);
				//Signal error (warning??).
				instance->status = ai_status_stream_error;
			}
#endif
		}
	}
}

void ai_infinite_isr(me4600_ai_subdevice_t *instance,
		     const uint32_t irq_status, const uint32_t ctrl_status)
{
	int to_read;

	if (irq_status & ME4600_IRQ_STATUS_BIT_SC) {	//next chunck of data -> read fifo
		//Set new state in ISM.
		if ((instance->fifo_irq_threshold < ME4600_AI_FIFO_HALF) && (!(ctrl_status & ME4600_AI_STATUS_BIT_HF_DATA))) {	//There is more data than we ecpected. Propably we aren't fast enough. Read as many as possible.
			if (instance->fifo_irq_threshold) {
				to_read =
				    ME4600_AI_FIFO_HALF -
				    (ME4600_AI_FIFO_HALF %
				     instance->fifo_irq_threshold);
				if (to_read > instance->fifo_irq_threshold) {
					PDEBUG
					    ("Infinite aqusition: Not fast enough data aqusition! TH != 0: correction=%d\n",
					     to_read);
				}
			} else {	//No threshold specified.
				to_read = ME4600_AI_FIFO_HALF;
			}
		} else {
			to_read = instance->ISM.next;
		}

		instance->ISM.read += to_read;

		//Get data
		if (ai_read_data(instance, to_read) != to_read) {	//ERROR!
			PERROR("Infinite aqusition: Circular buffer full!\n");
			ai_stop_isr(instance);
			instance->status = ai_status_stream_buffer_error;
		} else {
			ai_infinite_ISM(instance);
			instance->ISM.global_read += instance->ISM.read;
			instance->ISM.read = 0;
		}

		//Signal data to user
		wake_up_interruptible_all(&instance->wait_queue);
	} else if (irq_status & ME4600_IRQ_STATUS_BIT_AI_HF) {	//fifo is half full -> read fifo       Large blocks only!
		instance->ISM.read += ME4600_AI_FIFO_HALF;

		if (ai_read_data(instance, ME4600_AI_FIFO_HALF) != ME4600_AI_FIFO_HALF) {	//ERROR!
			PERROR("Infinite aqusition: Circular buffer full!\n");
			ai_stop_isr(instance);
			instance->status = ai_status_stream_buffer_error;

			//Signal it.
			wake_up_interruptible_all(&instance->wait_queue);
		} else {
			ai_infinite_ISM(instance);
		}
	}
}

static irqreturn_t me4600_ai_isr(int irq, void *dev_id)
{				/// @note This is time critical function!
	uint32_t irq_status;
	uint32_t ctrl_status;
	me4600_ai_subdevice_t *instance = dev_id;
	//int to_read;

	PDEBUG("executed. idx=0\n");

	if (irq != instance->irq) {
		PERROR("Incorrect interrupt num: %d.\n", irq);
		return IRQ_NONE;
	}

	irq_status = inl(instance->irq_status_reg);
	if (!
	    (irq_status &
	     (ME4600_IRQ_STATUS_BIT_AI_HF | ME4600_IRQ_STATUS_BIT_SC))) {
#ifdef MEDEBUG_INFO
		if ((irq_status & (ME4600_IRQ_STATUS_BIT_AI_HF | ME4600_IRQ_STATUS_BIT_SC | ME4600_IRQ_STATUS_BIT_LE)) == ME4600_IRQ_STATUS_BIT_LE) {	//This is security check case. LE is unused. This should never ever happend.
			PINFO
			    ("%ld Shared interrupt. %s(): irq_status_reg=LE_IRQ\n",
			     jiffies, __func__);
		} else {
			PINFO
			    ("%ld Shared interrupt. %s(): irq_status_reg=0x%04X\n",
			     jiffies, __func__, irq_status);
		}
#endif
		return IRQ_NONE;
	}

	if (!instance->circ_buf.buf) {	//Security check.
		PERROR_CRITICAL("CIRCULAR BUFFER NOT EXISTS!\n");
		ai_stop_isr(instance);
		return IRQ_HANDLED;
	}
	//Get the status register.
	ctrl_status = inl(instance->status_reg);

#ifdef MEDEBUG_INFO
	if (irq_status & ME4600_IRQ_STATUS_BIT_AI_HF)
		PINFO("HF interrupt active\n");
	if (irq_status & ME4600_IRQ_STATUS_BIT_SC)
		PINFO("SC interrupt active\n");
	if (irq_status & ME4600_IRQ_STATUS_BIT_LE)
		PINFO("LE interrupt active\n");
#endif

	//This is safety check!
	if ((irq_status & ME4600_IRQ_STATUS_BIT_AI_HF)
	    && (ctrl_status & ME4600_AI_STATUS_BIT_HF_DATA)) {
		PDEBUG("HF interrupt active but FIFO under half\n");
		//Reset HF interrupt latch.
		spin_lock(instance->ctrl_reg_lock);
		outl(ctrl_status | ME4600_AI_CTRL_BIT_HF_IRQ_RESET,
		     instance->ctrl_reg);
		PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->ctrl_reg - instance->reg_base,
			   ctrl_status);
		outl(ctrl_status, instance->ctrl_reg);
		PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->ctrl_reg - instance->reg_base,
			   ctrl_status);
		spin_unlock(instance->ctrl_reg_lock);
		return IRQ_HANDLED;
	}
#ifdef MEDEBUG_INFO
	PINFO("STATUS_BIT_FSM=%s.\n",
	      (ctrl_status & ME4600_AI_STATUS_BIT_FSM) ? "on" : "off");

	PINFO("STATUS_BIT_EF_CHANNEL=%s.\n",
	      (ctrl_status & ME4600_AI_STATUS_BIT_EF_CHANNEL) ? "not empty" :
	      "empty");
	PINFO("STATUS_BIT_HF_CHANNEL=%s.\n",
	      (ctrl_status & ME4600_AI_STATUS_BIT_HF_CHANNEL) ? " < HF" :
	      " > HF");
	PINFO("STATUS_BIT_FF_CHANNEL=%s.\n",
	      (ctrl_status & ME4600_AI_STATUS_BIT_FF_CHANNEL) ? "not full" :
	      "full");

	PINFO("STATUS_BIT_EF_DATA=%s.\n",
	      (ctrl_status & ME4600_AI_STATUS_BIT_EF_DATA) ? "not empty" :
	      "empty");
	PINFO("STATUS_BIT_HF_DATA=%s.\n",
	      (ctrl_status & ME4600_AI_STATUS_BIT_HF_DATA) ? " < HF" : " > HF");
	PINFO("STATUS_BIT_FF_DATA=%s.\n",
	      (ctrl_status & ME4600_AI_STATUS_BIT_FF_DATA) ? "not full" :
	      "full");

	PINFO("CTRL_BIT_HF_IRQ=%s.\n",
	      (ctrl_status & ME4600_AI_CTRL_BIT_HF_IRQ) ? "enable" : "disable");
	PINFO("CTRL_BIT_HF_IRQ_RESET=%s.\n",
	      (ctrl_status & ME4600_AI_CTRL_BIT_HF_IRQ_RESET) ? "reset" :
	      "work");
	PINFO("CTRL_BIT_SC_IRQ=%s.\n",
	      (ctrl_status & ME4600_AI_CTRL_BIT_SC_IRQ) ? "enable" : "disable");
	PINFO("CTRL_BIT_SC_RELOAD=%s.\n",
	      (ctrl_status & ME4600_AI_CTRL_BIT_SC_RELOAD) ? "on" : "off");
	PINFO("CTRL_BIT_SC_IRQ_RESET=%s.\n",
	      (ctrl_status & ME4600_AI_CTRL_BIT_SC_IRQ_RESET) ? "reset" :
	      "work");
#endif

	//Look for overflow error.
	if (!(ctrl_status & ME4600_AI_STATUS_BIT_FF_DATA)) {
		//FIFO is full. Read datas and reset all settings.
		PERROR("FIFO overflow.\n");
		ai_read_data(instance, ME4600_AI_FIFO_COUNT);
		ai_stop_isr(instance);

		instance->status = ai_status_stream_fifo_error;
		//Signal it.
		wake_up_interruptible_all(&instance->wait_queue);

		return IRQ_HANDLED;
	}

	if (!instance->data_required) {	//This is infinite aqusition.
#ifdef MEDEBUG_ERROR
		if ((irq_status &
		     (ME4600_IRQ_STATUS_BIT_AI_HF | ME4600_IRQ_STATUS_BIT_SC))
		    ==
		    (ME4600_IRQ_STATUS_BIT_AI_HF | ME4600_IRQ_STATUS_BIT_SC)) {
			///In infinite mode only one interrupt source should be reported!
			PERROR
			    ("Error in ISM! Infinite aqusition: HF and SC interrupts active! threshold=%d next=%d ctrl=0x%04X irq_status_reg=0x%04X",
			     instance->fifo_irq_threshold, instance->ISM.next,
			     ctrl_status, irq_status);
		}
#endif

		ai_infinite_isr(instance, irq_status, ctrl_status);

#ifdef MEDEBUG_INFO
		ctrl_status = inl(instance->ctrl_reg);
#endif
	} else {

		ai_limited_isr(instance, irq_status, ctrl_status);
		ctrl_status = inl(instance->status_reg);
		if (!(ctrl_status & (ME4600_AI_STATUS_BIT_HF_DATA | ME4600_AI_CTRL_BIT_HF_IRQ_RESET))) {	//HF active, but we have more than half already => HF will never come
			PDEBUG
			    ("MISSED HF. data_required=%d ISM.read=%d ISM.global=%d ISM.next=%d\n",
			     instance->data_required, instance->ISM.read,
			     instance->ISM.global_read, instance->ISM.next);
			ai_limited_isr(instance, ME4600_IRQ_STATUS_BIT_AI_HF,
				       ctrl_status);
		}
	}

#ifdef MEDEBUG_INFO
	PINFO("STATUS_BIT_FSM=%s.\n",
	      (ctrl_status & ME4600_AI_STATUS_BIT_FSM) ? "on" : "off");

	PINFO("STATUS_BIT_EF_CHANNEL=%s.\n",
	      (ctrl_status & ME4600_AI_STATUS_BIT_EF_CHANNEL) ? "not empty" :
	      "empty");
	PINFO("STATUS_BIT_HF_CHANNEL=%s.\n",
	      (ctrl_status & ME4600_AI_STATUS_BIT_HF_CHANNEL) ? " < HF" :
	      " > HF");
	PINFO("STATUS_BIT_FF_CHANNEL=%s.\n",
	      (ctrl_status & ME4600_AI_STATUS_BIT_FF_CHANNEL) ? "not full" :
	      "full");

	PINFO("STATUS_BIT_EF_DATA=%s.\n",
	      (ctrl_status & ME4600_AI_STATUS_BIT_EF_DATA) ? "not empty" :
	      "empty");
	PINFO("STATUS_BIT_HF_DATA=%s.\n",
	      (ctrl_status & ME4600_AI_STATUS_BIT_HF_DATA) ? " < HF" : " > HF");
	PINFO("STATUS_BIT_FF_DATA=%s.\n",
	      (ctrl_status & ME4600_AI_STATUS_BIT_FF_DATA) ? "not full" :
	      "full");

	PINFO("CTRL_BIT_HF_IRQ_RESET=%s.\n",
	      (ctrl_status & ME4600_AI_CTRL_BIT_HF_IRQ_RESET) ? "reset" :
	      "work");
	PINFO("CTRL_BIT_SC_IRQ=%s.\n",
	      (ctrl_status & ME4600_AI_CTRL_BIT_SC_IRQ) ? "enable" : "disable");
	PINFO("CTRL_BIT_SC_RELOAD=%s.\n",
	      (ctrl_status & ME4600_AI_CTRL_BIT_SC_RELOAD) ? "on" : "off");
	PINFO("CTRL_BIT_SC_IRQ_RESET=%s.\n",
	      (ctrl_status & ME4600_AI_CTRL_BIT_SC_IRQ_RESET) ? "reset" :
	      "work");
	PINFO("%ld END\n", jiffies);
#endif

	return IRQ_HANDLED;
}

/** @brief Stop aqusation of data. Reset interrupts' laches. Clear data's FIFO.
*
* @param instance The subdevice instance (pointer).
*/
inline void ai_stop_isr(me4600_ai_subdevice_t *instance)
{				/// @note This is soft time critical function!
	register uint32_t tmp;

	spin_lock(instance->ctrl_reg_lock);
	//Stop all. Reset interrupt laches. Reset data FIFO.
	tmp = inl(instance->ctrl_reg);
	tmp |=
	    (ME4600_AI_CTRL_BIT_IMMEDIATE_STOP | ME4600_AI_CTRL_BIT_HF_IRQ_RESET
	     | ME4600_AI_CTRL_BIT_LE_IRQ_RESET |
	     ME4600_AI_CTRL_BIT_SC_IRQ_RESET);
	tmp &= ~ME4600_AI_CTRL_BIT_DATA_FIFO;
	outl(tmp, instance->ctrl_reg);
	PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->ctrl_reg - instance->reg_base, tmp);
	spin_unlock(instance->ctrl_reg_lock);
}

/** @brief Copy data from fifo to circular buffer.
*
* @param instance The subdevice instance (pointer).
* @param count The number of requested data.
*
* @return On success: Number of copied values.
* @return On error: -ME_ERRNO_RING_BUFFER_OVERFLOW.
*/
static inline int ai_read_data(me4600_ai_subdevice_t *instance,
			       const int count)
{				/// @note This is time critical function!
	int c = count;
	int empty_space;
	int copied = 0;
	int i, j;

	empty_space = me_circ_buf_space_to_end(&instance->circ_buf);
	if (empty_space <= 0) {
		PDEBUG("Circular buffer full.\n");
		return -ME_ERRNO_RING_BUFFER_OVERFLOW;
	}

	if (empty_space < c) {	//Copy first part. Max to end of buffer.
		PDEBUG
		    ("Try to copy %d values from FIFO to circular buffer (pass 1).\n",
		     empty_space);
		for (i = 0; i < empty_space; i++) {
			*(instance->circ_buf.buf + instance->circ_buf.head) =
			    (inw(instance->data_reg) ^ 0x8000);
			instance->circ_buf.head++;
		}
		instance->circ_buf.head &= instance->circ_buf.mask;
		c -= empty_space;
		copied = empty_space;

		empty_space = me_circ_buf_space_to_end(&instance->circ_buf);
	}

	if (empty_space > 0) {
		j = (empty_space < c) ? empty_space : c;
		PDEBUG
		    ("Try to copy %d values from FIFO to circular buffer (pass 2).\n",
		     c);
		for (i = 0; i < j; i++) {
			*(instance->circ_buf.buf + instance->circ_buf.head) =
			    (inw(instance->data_reg) ^ 0x8000);
			instance->circ_buf.head++;
		}
		instance->circ_buf.head &= instance->circ_buf.mask;
		copied += j;
	}
	return copied;
}

inline void ai_infinite_ISM(me4600_ai_subdevice_t *instance)
{				/// @note This is time critical function!
	register volatile uint32_t ctrl_set, ctrl_reset, tmp;

	if (instance->fifo_irq_threshold < ME4600_AI_FIFO_MAX_SC) {	// Only sample counter with reloadnig is working. Reset it.
		PINFO
		    ("Only sample counter with reloadnig is working. Reset it.\n");
		ctrl_set = ME4600_AI_CTRL_BIT_SC_IRQ_RESET;
		ctrl_reset = ~ME4600_AI_CTRL_BIT_SC_IRQ_RESET;
	} else if (instance->fifo_irq_threshold == instance->ISM.read) {	//This is SC interrupt for large block. The whole section is done. Reset SC_IRQ an HF_IRQ and start everything again from beginning.
		PINFO
		    ("This is SC interrupt for large block. The whole section is done. Reset SC_IRQ an HF_IRQ and start everything again from beginning.\n");
		ctrl_set =
		    ME4600_AI_CTRL_BIT_SC_IRQ_RESET |
		    ME4600_AI_CTRL_BIT_HF_IRQ_RESET;
		ctrl_reset =
		    ~(ME4600_AI_CTRL_BIT_SC_IRQ_RESET |
		      ME4600_AI_CTRL_BIT_HF_IRQ_RESET);
	} else if (instance->fifo_irq_threshold >= (ME4600_AI_FIFO_MAX_SC + instance->ISM.read)) {	//This is HF interrupt for large block.The next interrupt should be from HF, also. Reset HF.
		PINFO
		    ("This is HF interrupt for large block.The next interrupt should be from HF, also. Reset HF.\n");
		ctrl_set = ME4600_AI_CTRL_BIT_HF_IRQ_RESET;
		ctrl_reset = ~ME4600_AI_CTRL_BIT_HF_IRQ_RESET;
	} else {		//This is HF interrupt for large block.The next interrupt should be from SC. Don't reset HF!
		PINFO
		    ("This is HF interrupt for large block.The next interrupt should be from SC. Don't reset HF!\n");
		ctrl_set = ME4600_AI_CTRL_BIT_HF_IRQ_RESET;
		ctrl_reset = 0xFFFFFFFF;
	}

	//Reset interrupt latch.
	spin_lock(instance->ctrl_reg_lock);
	tmp = inl(instance->ctrl_reg);
	PINFO("ctrl=0x%x ctrl_set=0x%x ctrl_reset=0x%x\n", tmp, ctrl_set,
	      ctrl_reset);
	tmp |= ctrl_set;
	outl(tmp, instance->ctrl_reg);
	PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->ctrl_reg - instance->reg_base, tmp);
	if (ctrl_reset != 0xFFFFFFFF) {
		outl(tmp & ctrl_reset, instance->ctrl_reg);
		PDEBUG_REG("ctrl_reset outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->ctrl_reg - instance->reg_base,
			   tmp & ctrl_reset);
	}
	spin_unlock(instance->ctrl_reg_lock);

}

inline void ai_limited_ISM(me4600_ai_subdevice_t *instance,
			   uint32_t irq_status)
{				/// @note This is time critical function!
	register volatile uint32_t ctrl_set, ctrl_reset = 0xFFFFFFFF, tmp;

	if (!instance->fifo_irq_threshold) {	//No threshold provided. SC ends work.
		PINFO("No threshold provided. SC ends work.\n");
		ctrl_set = ME4600_AI_CTRL_BIT_HF_IRQ_RESET;
		if (instance->data_required > (ME4600_AI_FIFO_COUNT - 1 + instance->ISM.global_read)) {	//HF need reseting.
			ctrl_reset &= ~ME4600_AI_CTRL_BIT_HF_IRQ_RESET;
		}
	} else			//if(instance->fifo_irq_threshold)
	{
		if (irq_status & ME4600_IRQ_STATUS_BIT_AI_HF) {
			PINFO("Threshold provided. Clear HF latch.\n");
			ctrl_set = ME4600_AI_CTRL_BIT_HF_IRQ_RESET;

			if (instance->fifo_irq_threshold >= (ME4600_AI_FIFO_MAX_SC + instance->ISM.read)) {	//This is not the last one. HF need reseting.
				PINFO
				    ("The next interrupt is HF. HF need be activating.\n");
				ctrl_reset = ~ME4600_AI_CTRL_BIT_HF_IRQ_RESET;
			}
		}

		if (irq_status & ME4600_IRQ_STATUS_BIT_SC) {
			PINFO("Threshold provided. Restart SC.\n");
			ctrl_set = ME4600_AI_CTRL_BIT_SC_IRQ_RESET;
			ctrl_reset &= ~ME4600_AI_CTRL_BIT_SC_IRQ_RESET;

			if (instance->fifo_irq_threshold >= ME4600_AI_FIFO_MAX_SC) {	//This is not the last one. HF need to be activating.
				PINFO
				    ("The next interrupt is HF. HF need to be activating.\n");
				ctrl_reset &= ~ME4600_AI_CTRL_BIT_HF_IRQ_RESET;
			}
		}
	}

	//Reset interrupt latch.
	spin_lock(instance->ctrl_reg_lock);
	tmp = inl(instance->ctrl_reg);
	tmp |= ctrl_set;
	outl(tmp, instance->ctrl_reg);
	PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->ctrl_reg - instance->reg_base, tmp);

	if (ctrl_reset != 0xFFFFFFFF) {
		outl(tmp & ctrl_reset, instance->ctrl_reg);
		PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->ctrl_reg - instance->reg_base,
			   tmp & ctrl_reset);
	}
	spin_unlock(instance->ctrl_reg_lock);

}

/** @brief Last chunck of datas. We must reschedule sample counter.
*	@note Last chunck.
*	Leaving SC_RELOAD doesn't do any harm, but in some bad case can make extra interrupts.
*	@warning When threshold is wrongly set some IRQ are lost.(!!!)
*/
inline void ai_reschedule_SC(me4600_ai_subdevice_t *instance)
{
	register uint32_t rest;

	if (instance->data_required <= instance->ISM.global_read)
		return;

	rest = instance->data_required - instance->ISM.global_read;
	if (rest < instance->fifo_irq_threshold) {	//End of work soon ....
		PDEBUG("Rescheduling SC from %d to %d.\n",
		       instance->fifo_irq_threshold, rest);
		/// @note Write new value to SC <==  DANGER! This is not safe solution! We can miss some inputs.
		outl(rest, instance->sample_counter_reg);
		PDEBUG_REG("sample_counter_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->sample_counter_reg - instance->reg_base,
			   rest);
		instance->fifo_irq_threshold = rest;

		if (rest < ME4600_AI_FIFO_MAX_SC) {
			instance->ISM.next = rest;
		} else {
			instance->ISM.next = rest % ME4600_AI_FIFO_HALF;
			if (instance->ISM.next + ME4600_AI_FIFO_HALF <
			    ME4600_AI_FIFO_MAX_SC) {
				instance->ISM.next += ME4600_AI_FIFO_HALF;
			}
		}
	}
}

/** Start the ISM. All must be reseted before enter to this function. */
inline void ai_data_acquisition_logic(me4600_ai_subdevice_t *instance)
{
	register uint32_t tmp;

	if (!instance->data_required) {	//This is infinite aqusition.
		if (!instance->fifo_irq_threshold) {	//No threshold provided. Set SC to 0.5*FIFO. Clear the SC's latch.
			//Set the sample counter
			outl(ME4600_AI_FIFO_HALF, instance->sample_counter_reg);
			PDEBUG_REG
			    ("sample_counter_reg outl(0x%lX+0x%lX)=0x%x\n",
			     instance->reg_base,
			     instance->sample_counter_reg - instance->reg_base,
			     ME4600_AI_FIFO_HALF);
		} else {	//Threshold provided. Set SC to treshold. Clear the SC's latch.
			//Set the sample counter
			outl(instance->fifo_irq_threshold,
			     instance->sample_counter_reg);
			PDEBUG_REG
			    ("sample_counter_reg outl(0x%lX+0x%lX)=0x%x\n",
			     instance->reg_base,
			     instance->sample_counter_reg - instance->reg_base,
			     instance->fifo_irq_threshold);
		}

		if (instance->fifo_irq_threshold < ME4600_AI_FIFO_MAX_SC) {	//Enable only sample counter's interrupt. Set reload bit. Clear the SC's latch.
			spin_lock(instance->ctrl_reg_lock);
			tmp = inl(instance->ctrl_reg);
			tmp |= ME4600_AI_CTRL_BIT_SC_RELOAD;
			tmp &= ~ME4600_AI_CTRL_BIT_SC_IRQ_RESET;
			outl(tmp, instance->ctrl_reg);
			PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->ctrl_reg - instance->reg_base,
				   tmp);
			spin_unlock(instance->ctrl_reg_lock);
			if (!instance->fifo_irq_threshold) {	//No threshold provided. Set ISM.next to 0.5*FIFO.
				instance->ISM.next = ME4600_AI_FIFO_HALF;
			} else {	//Threshold provided. Set ISM.next to treshold.
				instance->ISM.next =
				    instance->fifo_irq_threshold;
			}
		} else {	//Enable sample counter's and HF's interrupts.
			spin_lock(instance->ctrl_reg_lock);
			tmp = inl(instance->ctrl_reg);
			tmp |= ME4600_AI_CTRL_BIT_SC_RELOAD;
			tmp &=
			    ~(ME4600_AI_CTRL_BIT_SC_IRQ_RESET |
			      ME4600_AI_CTRL_BIT_HF_IRQ_RESET);
			outl(tmp, instance->ctrl_reg);
			PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->ctrl_reg - instance->reg_base,
				   tmp);
			spin_unlock(instance->ctrl_reg_lock);

			instance->ISM.next =
			    instance->fifo_irq_threshold % ME4600_AI_FIFO_HALF;
			if (instance->ISM.next + ME4600_AI_FIFO_HALF <
			    ME4600_AI_FIFO_MAX_SC) {
				instance->ISM.next += ME4600_AI_FIFO_HALF;
			}
		}
	} else {		//This aqusition is limited to set number of data.
		if (instance->fifo_irq_threshold >= instance->data_required) {	//Stupid situation.
			instance->fifo_irq_threshold = 0;
			PDEBUG
			    ("Stupid situation: data_required(%d) < threshold(%d).\n",
			     instance->fifo_irq_threshold,
			     instance->data_required);
		}

		if (!instance->fifo_irq_threshold) {	//No threshold provided. Easy case: HF=read and SC=end.
			//Set the sample counter to data_required.
			outl(instance->data_required,
			     instance->sample_counter_reg);
			PDEBUG_REG
			    ("sample_counter_reg outl(0x%lX+0x%lX)=0x%x\n",
			     instance->reg_base,
			     instance->sample_counter_reg - instance->reg_base,
			     instance->data_required);

			//Reset the latches of sample counter and HF (if SC>FIFO).
			//No SC reload!
			spin_lock(instance->ctrl_reg_lock);
			tmp = inl(instance->ctrl_reg);
			tmp &=
			    ~(ME4600_AI_CTRL_BIT_SC_IRQ_RESET |
			      ME4600_AI_CTRL_BIT_SC_RELOAD);
			if (instance->data_required >
			    (ME4600_AI_FIFO_COUNT - 1)) {
				tmp &= ~ME4600_AI_CTRL_BIT_HF_IRQ_RESET;
				instance->ISM.next =
				    instance->data_required %
				    ME4600_AI_FIFO_HALF;
				instance->ISM.next += ME4600_AI_FIFO_HALF;

			} else {
				instance->ISM.next = instance->data_required;
			}
			outl(tmp, instance->ctrl_reg);
			PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->ctrl_reg - instance->reg_base,
				   tmp);
			spin_unlock(instance->ctrl_reg_lock);

		} else {	//The most general case. We have concret numbe of required data and threshold. SC=TH
			//Set the sample counter to threshold.
			outl(instance->fifo_irq_threshold,
			     instance->sample_counter_reg);
			PDEBUG_REG
			    ("sample_counter_reg outl(0x%lX+0x%lX)=0x%x\n",
			     instance->reg_base,
			     instance->sample_counter_reg - instance->reg_base,
			     instance->fifo_irq_threshold);

			spin_lock(instance->ctrl_reg_lock);
			tmp = inl(instance->ctrl_reg);
			//In this moment we are sure that SC will come more than once.
			tmp |= ME4600_AI_CTRL_BIT_SC_RELOAD;

			if (instance->fifo_irq_threshold < ME4600_AI_FIFO_MAX_SC) {	//The threshold is so small that we do need HF.
				tmp &= ~ME4600_AI_CTRL_BIT_SC_IRQ_RESET;
				instance->ISM.next =
				    instance->fifo_irq_threshold;
			} else {	//The threshold is large. The HF must be use.
				tmp &=
				    ~(ME4600_AI_CTRL_BIT_SC_IRQ_RESET |
				      ME4600_AI_CTRL_BIT_HF_IRQ_RESET);
				instance->ISM.next =
				    instance->fifo_irq_threshold %
				    ME4600_AI_FIFO_HALF;
				if (instance->ISM.next + ME4600_AI_FIFO_HALF <
				    ME4600_AI_FIFO_MAX_SC) {
					instance->ISM.next +=
					    ME4600_AI_FIFO_HALF;
				}
			}
			outl(tmp, instance->ctrl_reg);
			PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->ctrl_reg - instance->reg_base,
				   tmp);
			spin_unlock(instance->ctrl_reg_lock);
		}
	}
}

static int ai_mux_toggler(me4600_ai_subdevice_t *instance)
{
	uint32_t tmp;

	PDEBUG("executed. idx=0\n");

	outl(0, instance->scan_pre_timer_low_reg);
	PDEBUG_REG("scan_pre_timer_low_reg outl(0x%lX+0x%lX)=0x%x\n",
		   instance->reg_base,
		   instance->scan_pre_timer_low_reg - instance->reg_base, 0);
	outl(0, instance->scan_pre_timer_high_reg);
	PDEBUG_REG("scan_pre_timer_high_reg outl(0x%lX+0x%lX)=0x%x\n",
		   instance->reg_base,
		   instance->scan_pre_timer_high_reg - instance->reg_base, 0);
	outl(0, instance->scan_timer_low_reg);
	PDEBUG_REG("scan_timer_low_reg outl(0x%lX+0x%lX)=0x%x\n",
		   instance->reg_base,
		   instance->scan_timer_low_reg - instance->reg_base, 0);
	outl(0, instance->scan_timer_high_reg);
	PDEBUG_REG("scan_timer_high_reg outl(0x%lX+0x%lX)=0x%x\n",
		   instance->reg_base,
		   instance->scan_timer_high_reg - instance->reg_base, 0);
	outl(65, instance->chan_timer_reg);
	PDEBUG_REG("chan_timer_reg outl(0x%lX+0x%lX)=0x%x\n",
		   instance->reg_base,
		   instance->chan_timer_reg - instance->reg_base, 65);
	outl(65, instance->chan_pre_timer_reg);
	PDEBUG_REG("chan_pre_timer_reg outl(0x%lX+0x%lX)=0x%x\n",
		   instance->reg_base,
		   instance->chan_pre_timer_reg - instance->reg_base, 65);

	// Turn on internal reference.
	tmp = inl(instance->ctrl_reg);
	tmp |= ME4600_AI_CTRL_BIT_FULLSCALE;
	outl(tmp, instance->ctrl_reg);
	PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->ctrl_reg - instance->reg_base, tmp);

	// Clear data and channel fifo.
	tmp &=
	    ~(ME4600_AI_CTRL_BIT_CHANNEL_FIFO | ME4600_AI_CTRL_BIT_DATA_FIFO);
	outl(tmp, instance->ctrl_reg);
	PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->ctrl_reg - instance->reg_base, tmp);
	tmp |= ME4600_AI_CTRL_BIT_CHANNEL_FIFO | ME4600_AI_CTRL_BIT_DATA_FIFO;
	outl(tmp, instance->ctrl_reg);
	PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->ctrl_reg - instance->reg_base, tmp);

	// Write channel entry.
	outl(ME4600_AI_LIST_INPUT_DIFFERENTIAL |
	     ME4600_AI_LIST_RANGE_UNIPOLAR_2_5 | 31,
	     instance->channel_list_reg);
	PDEBUG_REG("channel_list_reg outl(0x%lX+0x%lX)=0x%x\n",
		   instance->reg_base,
		   instance->channel_list_reg - instance->reg_base,
		   ME4600_AI_LIST_INPUT_DIFFERENTIAL |
		   ME4600_AI_LIST_RANGE_UNIPOLAR_2_5 | 31);

	// Start conversion.
	inl(instance->start_reg);
	PDEBUG_REG("start_reg inl(0x%lX+0x%lX)\n", instance->reg_base,
		   instance->start_reg - instance->reg_base);
	udelay(10);

	// Clear data and channel fifo.
	tmp &=
	    ~(ME4600_AI_CTRL_BIT_CHANNEL_FIFO | ME4600_AI_CTRL_BIT_DATA_FIFO);
	outl(tmp, instance->ctrl_reg);
	PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->ctrl_reg - instance->reg_base, tmp);
	tmp |= ME4600_AI_CTRL_BIT_CHANNEL_FIFO | ME4600_AI_CTRL_BIT_DATA_FIFO;
	outl(tmp, instance->ctrl_reg);
	PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->ctrl_reg - instance->reg_base, tmp);

	// Write channel entry.
	// ME4600_AI_LIST_INPUT_SINGLE_ENDED | ME4600_AI_LIST_RANGE_BIPOLAR_10 <= 0x0000
	outl(ME4600_AI_LIST_INPUT_SINGLE_ENDED |
	     ME4600_AI_LIST_RANGE_BIPOLAR_10, instance->channel_list_reg);
	PDEBUG_REG("channel_list_reg outl(0x%lX+0x%lX)=0x%x\n",
		   instance->reg_base,
		   instance->channel_list_reg - instance->reg_base,
		   ME4600_AI_LIST_INPUT_SINGLE_ENDED |
		   ME4600_AI_LIST_RANGE_BIPOLAR_10);

	// Start conversion.
	inl(instance->start_reg);
	PDEBUG_REG("start_reg inl(0x%lX+0x%lX)\n", instance->reg_base,
		   instance->start_reg - instance->reg_base);
	udelay(10);

	// Clear control register.
	tmp &= (ME4600_AI_CTRL_BIT_EX_IRQ | ME4600_AI_CTRL_BIT_EX_IRQ_RESET);
	outl(tmp, instance->ctrl_reg);
	PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->ctrl_reg - instance->reg_base, tmp);

	return ME_ERRNO_SUCCESS;
}

/** @brief Copy rest of data from fifo to circular buffer.
* @note Helper for STOP command. After FSM is stopped.
* @note This is slow function that copy all remainig data from FIFO to buffer.
*
* @param instance The subdevice instance (pointer).
*
* @return On success: Number of copied values.
* @return On error: Negative error code -ME_ERRNO_RING_BUFFER_OVERFLOW.
*/
static inline int ai_read_data_pooling(me4600_ai_subdevice_t *instance)
{				/// @note This is time critical function!
	int empty_space;
	int copied = 0;
	int status = ME_ERRNO_SUCCESS;

	PDEBUG("Space left in circular buffer = %d.\n",
	       me_circ_buf_space(&instance->circ_buf));

	while ((empty_space = me_circ_buf_space(&instance->circ_buf))) {
		if (!(status = inl(instance->status_reg) & ME4600_AI_STATUS_BIT_EF_DATA)) {	//No more data. status = ME_ERRNO_SUCCESS = 0
			break;
		}
		*(instance->circ_buf.buf + instance->circ_buf.head) =
		    (inw(instance->data_reg) ^ 0x8000);
		instance->circ_buf.head++;
		instance->circ_buf.head &= instance->circ_buf.mask;
	}

#ifdef MEDEBUG_ERROR
	if (!status)
		PDEBUG
		    ("Copied all remaining datas (%d) from FIFO to circular buffer.\n",
		     copied);
	else {
		PDEBUG("No more empty space in buffer.\n");
		PDEBUG("Copied %d datas from FIFO to circular buffer.\n",
		       copied);
		PDEBUG("FIFO still not empty.\n");
	}
#endif
	return (!status) ? copied : -ME_ERRNO_RING_BUFFER_OVERFLOW;
}

static void me4600_ai_work_control_task(struct work_struct *work)
{
	me4600_ai_subdevice_t *instance;
	uint32_t status;
	uint32_t ctrl;
	unsigned long cpu_flags = 0;
	int reschedule = 0;
	int signaling = 0;

	instance =
	    container_of((void *)work, me4600_ai_subdevice_t, ai_control_task);
	PINFO("<%s: %ld> executed.\n", __func__, jiffies);

	status = inl(instance->status_reg);
	PDEBUG_REG("status_reg inl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->status_reg - instance->reg_base, status);

	switch (instance->status) {	// Checking actual mode.
		// Not configured for work.
	case ai_status_none:
		break;

		//This are stable modes. No need to do anything. (?)
	case ai_status_single_configured:
	case ai_status_stream_configured:
	case ai_status_stream_fifo_error:
	case ai_status_stream_buffer_error:
	case ai_status_stream_error:
		PERROR("Shouldn't be running!.\n");
		break;

		// Stream modes
	case ai_status_stream_run_wait:
		if (status & ME4600_AI_STATUS_BIT_FSM) {	// ISM started..
			instance->status = ai_status_stream_run;
			// Signal the end of wait for start.
			signaling = 1;
			// Wait now for stop.
			reschedule = 1;
			break;

			// Check timeout.
			if ((instance->timeout.delay) && ((jiffies - instance->timeout.start_time) >= instance->timeout.delay)) {	// Timeout
				PDEBUG("Timeout reached.\n");
				// Stop all actions. No conditions! Block interrupts. Reset FIFO => Too late!
				ai_stop_isr(instance);

				instance->status = ai_status_stream_end;

				// Signal the end.
				signaling = 1;
			}
		}
		break;

	case ai_status_stream_run:
		// Wait for stop ISM.
		reschedule = 1;
		break;

	case ai_status_stream_end_wait:
		if (!(status & ME4600_AI_STATUS_BIT_FSM)) {	// ISM stoped. Overwrite ISR.
			instance->status = ai_status_stream_end;
			// Signal the end of wait for stop.
			signaling = 1;
		} else {
			// Wait for stop ISM.
			reschedule = 1;
		}
		break;

	case ai_status_stream_end:
		//End work.
		if (status & ME4600_AI_STATUS_BIT_FSM) {	// Still working? Stop it!
			PERROR
			    ("Status is 'ai_status_stream_end' but hardware is still working!\n");
			spin_lock_irqsave(instance->ctrl_reg_lock, cpu_flags);
			ctrl = inl(instance->ctrl_reg);
			ctrl |=
			    (ME4600_AI_CTRL_BIT_IMMEDIATE_STOP |
			     ME4600_AI_CTRL_BIT_HF_IRQ_RESET |
			     ME4600_AI_CTRL_BIT_SC_IRQ_RESET);
			outl(ctrl, instance->ctrl_reg);
			PDEBUG_REG("ctrl_reg outl(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->ctrl_reg - instance->reg_base,
				   ctrl);
			spin_unlock_irqrestore(instance->ctrl_reg_lock,
					       cpu_flags);
		}
		break;

	default:
		PERROR_CRITICAL("Status is in wrong state (%d)!\n",
				instance->status);
		instance->status = ai_status_stream_error;
		// Signal the end.
		signaling = 1;
		break;

	}

	if (signaling) {	//Signal it.
		wake_up_interruptible_all(&instance->wait_queue);
	}

	if (instance->ai_control_task_flag && reschedule) {	// Reschedule task
		queue_delayed_work(instance->me4600_workqueue,
				   &instance->ai_control_task, 1);
	} else {
		PINFO("<%s> Ending control task.\n", __func__);
	}

}
