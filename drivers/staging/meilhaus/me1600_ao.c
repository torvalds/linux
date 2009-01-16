/**
 * @file me1600_ao.c
 *
 * @brief ME-1600 analog output subdevice instance.
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

#include <linux/module.h>

#include <linux/slab.h>
#include <linux/spinlock.h>
#include <asm/io.h>
#include <linux/types.h>
#include <linux/sched.h>

#include <linux/workqueue.h>

#include "medefines.h"
#include "meinternal.h"
#include "meerror.h"
#include "medebug.h"

#include "me1600_ao_reg.h"
#include "me1600_ao.h"

/* Defines
 */

static void me1600_ao_destructor(struct me_subdevice *subdevice);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
static void me1600_ao_work_control_task(void *subdevice);
#else
static void me1600_ao_work_control_task(struct work_struct *work);
#endif

static int me1600_ao_io_reset_subdevice(me_subdevice_t * subdevice,
					struct file *filep, int flags);
static int me1600_ao_io_single_config(me_subdevice_t * subdevice,
				      struct file *filep, int channel,
				      int single_config, int ref, int trig_chan,
				      int trig_type, int trig_edge, int flags);
static int me1600_ao_io_single_read(me_subdevice_t * subdevice,
				    struct file *filep, int channel, int *value,
				    int time_out, int flags);
static int me1600_ao_io_single_write(me_subdevice_t * subdevice,
				     struct file *filep, int channel, int value,
				     int time_out, int flags);
static int me1600_ao_query_number_channels(me_subdevice_t * subdevice,
					   int *number);
static int me1600_ao_query_subdevice_type(me_subdevice_t * subdevice, int *type,
					  int *subtype);
static int me1600_ao_query_subdevice_caps(me_subdevice_t * subdevice,
					  int *caps);
static int me1600_ao_query_range_by_min_max(me_subdevice_t * subdevice,
					    int unit, int *min, int *max,
					    int *maxdata, int *range);
static int me1600_ao_query_number_ranges(me_subdevice_t * subdevice, int unit,
					 int *count);
static int me1600_ao_query_range_info(me_subdevice_t * subdevice, int range,
				      int *unit, int *min, int *max,
				      int *maxdata);

/* Functions
 */

me1600_ao_subdevice_t *me1600_ao_constructor(uint32_t reg_base,
					     unsigned int ao_idx,
					     int curr,
					     spinlock_t * config_regs_lock,
					     spinlock_t * ao_shadows_lock,
					     me1600_ao_shadow_t *
					     ao_regs_shadows,
					     struct workqueue_struct *me1600_wq)
{
	me1600_ao_subdevice_t *subdevice;
	int err;

	PDEBUG("executed. idx=%d\n", ao_idx);

	// Allocate memory for subdevice instance.
	subdevice = kmalloc(sizeof(me1600_ao_subdevice_t), GFP_KERNEL);

	if (!subdevice) {
		PERROR
		    ("Cannot get memory for analog output subdevice instance.\n");
		return NULL;
	}

	memset(subdevice, 0, sizeof(me1600_ao_subdevice_t));

	// Initialize subdevice base class.
	err = me_subdevice_init(&subdevice->base);

	if (err) {
		PERROR("Cannot initialize subdevice base class instance.\n");
		kfree(subdevice);
		return NULL;
	}
	// Initialize spin locks.
	spin_lock_init(&subdevice->subdevice_lock);
	subdevice->config_regs_lock = config_regs_lock;
	subdevice->ao_shadows_lock = ao_shadows_lock;

	// Save the subdevice index.
	subdevice->ao_idx = ao_idx;

	// Initialize range lists.
	subdevice->u_ranges_count = 2;

	subdevice->u_ranges[0].min = 0;	//0V
	subdevice->u_ranges[0].max = 9997558;	//10V

	subdevice->u_ranges[1].min = -10E6;	//-10V
	subdevice->u_ranges[1].max = 9995117;	//10V

	if (curr) {		// This is version with current outputs.
		subdevice->i_ranges_count = 2;

		subdevice->i_ranges[0].min = 0;	//0mA
		subdevice->i_ranges[0].max = 19995117;	//20mA

		subdevice->i_ranges[1].min = 4E3;	//4mA
		subdevice->i_ranges[1].max = 19995118;	//20mA
	} else {		// This is version without current outputs.
		subdevice->i_ranges_count = 0;

		subdevice->i_ranges[0].min = 0;	//0mA
		subdevice->i_ranges[0].max = 0;	//0mA

		subdevice->i_ranges[1].min = 0;	//0mA
		subdevice->i_ranges[1].max = 0;	//0mA
	}

	// Initialize registers.
	subdevice->uni_bi_reg = reg_base + ME1600_UNI_BI_REG;
	subdevice->i_range_reg = reg_base + ME1600_020_420_REG;
	subdevice->sim_output_reg = reg_base + ME1600_SIM_OUTPUT_REG;
	subdevice->current_on_reg = reg_base + ME1600_CURRENT_ON_REG;
#ifdef MEDEBUG_DEBUG_REG
	subdevice->reg_base = reg_base;
#endif

	// Initialize shadow structure.
	subdevice->ao_regs_shadows = ao_regs_shadows;

	// Override base class methods.
	subdevice->base.me_subdevice_destructor = me1600_ao_destructor;
	subdevice->base.me_subdevice_io_reset_subdevice =
	    me1600_ao_io_reset_subdevice;
	subdevice->base.me_subdevice_io_single_config =
	    me1600_ao_io_single_config;
	subdevice->base.me_subdevice_io_single_read = me1600_ao_io_single_read;
	subdevice->base.me_subdevice_io_single_write =
	    me1600_ao_io_single_write;
	subdevice->base.me_subdevice_query_number_channels =
	    me1600_ao_query_number_channels;
	subdevice->base.me_subdevice_query_subdevice_type =
	    me1600_ao_query_subdevice_type;
	subdevice->base.me_subdevice_query_subdevice_caps =
	    me1600_ao_query_subdevice_caps;
	subdevice->base.me_subdevice_query_range_by_min_max =
	    me1600_ao_query_range_by_min_max;
	subdevice->base.me_subdevice_query_number_ranges =
	    me1600_ao_query_number_ranges;
	subdevice->base.me_subdevice_query_range_info =
	    me1600_ao_query_range_info;

	// Initialize wait queue.
	init_waitqueue_head(&subdevice->wait_queue);

	// Prepare work queue.
	subdevice->me1600_workqueue = me1600_wq;

/* workqueue API changed in kernel 2.6.20 */
#if ( LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20) )
	INIT_WORK(&subdevice->ao_control_task, me1600_ao_work_control_task,
		  (void *)subdevice);
#else
	INIT_DELAYED_WORK(&subdevice->ao_control_task,
			  me1600_ao_work_control_task);
#endif
	return subdevice;
}

static void me1600_ao_destructor(struct me_subdevice *subdevice)
{
	me1600_ao_subdevice_t *instance;

	instance = (me1600_ao_subdevice_t *) subdevice;

	PDEBUG("executed. idx=%d\n", instance->ao_idx);

	instance->ao_control_task_flag = 0;

	// Reset subdevice to asure clean exit.
	me1600_ao_io_reset_subdevice(subdevice, NULL,
				     ME_IO_RESET_SUBDEVICE_NO_FLAGS);

	// Remove any tasks from work queue. This is paranoic because it was done allready in reset().
	if (!cancel_delayed_work(&instance->ao_control_task)) {	//Wait 2 ticks to be sure that control task is removed from queue.
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(2);
	}
}

static int me1600_ao_io_reset_subdevice(me_subdevice_t * subdevice,
					struct file *filep, int flags)
{
	me1600_ao_subdevice_t *instance;
	uint16_t tmp;

	instance = (me1600_ao_subdevice_t *) subdevice;

	PDEBUG("executed. idx=%d\n", instance->ao_idx);

	if (flags) {
		PERROR("Invalid flag specified.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	ME_SUBDEVICE_ENTER;

	//Cancel control task
	PDEBUG("Cancel control task. idx=%d\n", instance->ao_idx);
	instance->ao_control_task_flag = 0;
	cancel_delayed_work(&instance->ao_control_task);
	(instance->ao_regs_shadows)->trigger &= ~(0x1 << instance->ao_idx);	//Cancell waiting for trigger.

	// Reset all settings.
	spin_lock(&instance->subdevice_lock);
	spin_lock(instance->ao_shadows_lock);
	(instance->ao_regs_shadows)->shadow[instance->ao_idx] = 0;
	(instance->ao_regs_shadows)->mirror[instance->ao_idx] = 0;
	(instance->ao_regs_shadows)->trigger &= ~(0x1 << instance->ao_idx);	//Not waiting for triggering.
	(instance->ao_regs_shadows)->synchronous &= ~(0x1 << instance->ao_idx);	//Individual triggering.

	// Set output to default (safe) state.
	spin_lock(instance->config_regs_lock);
	tmp = inw(instance->uni_bi_reg);	// unipolar
	tmp |= (0x1 << instance->ao_idx);
	outw(tmp, instance->uni_bi_reg);
	PDEBUG_REG("uni_bi_reg outw(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->uni_bi_reg - instance->reg_base, tmp);

	tmp = inw(instance->current_on_reg);	// Volts only!
	tmp &= ~(0x1 << instance->ao_idx);
	tmp &= 0x00FF;
	outw(tmp, instance->current_on_reg);
	PDEBUG_REG("current_on_reg outl(0x%lX+0x%lX)=0x%x\n",
		   instance->reg_base,
		   instance->current_on_reg - instance->reg_base, tmp);

	tmp = inw(instance->i_range_reg);	// 0..20mA <= If exists.
	tmp &= ~(0x1 << instance->ao_idx);
	outw(tmp, instance->i_range_reg);
	PDEBUG_REG("i_range_reg outl(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   instance->i_range_reg - instance->reg_base, tmp);

	outw(0, (instance->ao_regs_shadows)->registry[instance->ao_idx]);
	PDEBUG_REG("channel_reg outw(0x%lX+0x%lX)=0x%x\n", instance->reg_base,
		   (instance->ao_regs_shadows)->registry[instance->ao_idx] -
		   instance->reg_base, 0);

	// Trigger output.
	outw(0x0000, instance->sim_output_reg);
	PDEBUG_REG("sim_output_reg outl(0x%lX+0x%lX)=0x%x\n",
		   instance->reg_base,
		   instance->sim_output_reg - instance->reg_base, 0x0000);
	outw(0xFFFF, instance->sim_output_reg);
	PDEBUG_REG("sim_output_reg outl(0x%lX+0x%lX)=0x%x\n",
		   instance->reg_base,
		   instance->sim_output_reg - instance->reg_base, 0xFFFF);
	spin_unlock(instance->config_regs_lock);
	spin_unlock(instance->ao_shadows_lock);

	// Set status to 'none'
	instance->status = ao_status_none;
	spin_unlock(&instance->subdevice_lock);

	//Signal reset if user is on wait.
	wake_up_interruptible_all(&instance->wait_queue);

	ME_SUBDEVICE_EXIT;

	return ME_ERRNO_SUCCESS;
}

static int me1600_ao_io_single_config(me_subdevice_t * subdevice,
				      struct file *filep,
				      int channel,
				      int single_config,
				      int ref,
				      int trig_chan,
				      int trig_type, int trig_edge, int flags)
{
	me1600_ao_subdevice_t *instance;
	uint16_t tmp;

	instance = (me1600_ao_subdevice_t *) subdevice;

	PDEBUG("executed. idx=%d\n", instance->ao_idx);

	// Checking parameters.
	if (flags) {
		PERROR
		    ("Invalid flag specified. Must be ME_IO_SINGLE_CONFIG_NO_FLAGS.\n");
		return ME_ERRNO_INVALID_FLAGS;
	}

	if (trig_edge != ME_TRIG_EDGE_NONE) {
		PERROR
		    ("Invalid trigger edge. Software trigger has not edge. Must be ME_TRIG_EDGE_NONE\n");
		return ME_ERRNO_INVALID_TRIG_EDGE;
	}

	if (trig_type != ME_TRIG_TYPE_SW) {
		PERROR("Invalid trigger edge. Must be ME_TRIG_TYPE_SW.\n");
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

	if (((single_config + 1) >
	     (instance->u_ranges_count + instance->i_ranges_count))
	    || (single_config < 0)) {
		PERROR("Invalid range specified.\n");
		return ME_ERRNO_INVALID_SINGLE_CONFIG;
	}

	if (channel) {
		PERROR("Invalid channel specified.\n");
		return ME_ERRNO_INVALID_CHANNEL;
	}
	// Checking parameters - done. All is fine. Do config.

	ME_SUBDEVICE_ENTER;

	//Cancel control task
	PDEBUG("Cancel control task. idx=%d\n", instance->ao_idx);
	instance->ao_control_task_flag = 0;
	cancel_delayed_work(&instance->ao_control_task);

	spin_lock(&instance->subdevice_lock);
	spin_lock(instance->ao_shadows_lock);
	(instance->ao_regs_shadows)->trigger &= ~(0x1 << instance->ao_idx);	//Cancell waiting for trigger.
	(instance->ao_regs_shadows)->shadow[instance->ao_idx] = 0;
	(instance->ao_regs_shadows)->mirror[instance->ao_idx] = 0;

	spin_lock(instance->config_regs_lock);
	switch (single_config) {
	case 0:		// 0V 10V
		tmp = inw(instance->current_on_reg);	// Volts
		tmp &= ~(0x1 << instance->ao_idx);
		outw(tmp, instance->current_on_reg);
		PDEBUG_REG("current_on_reg outw(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->current_on_reg - instance->reg_base, tmp);

		// 0V
		outw(0,
		     (instance->ao_regs_shadows)->registry[instance->ao_idx]);
		PDEBUG_REG("channel_reg outw(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   (instance->ao_regs_shadows)->registry[instance->
								 ao_idx] -
			   instance->reg_base, 0);

		tmp = inw(instance->uni_bi_reg);	// unipolar
		tmp |= (0x1 << instance->ao_idx);
		outw(tmp, instance->uni_bi_reg);
		PDEBUG_REG("uni_bi_reg outw(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->uni_bi_reg - instance->reg_base, tmp);

		tmp = inw(instance->i_range_reg);	// 0..20mA <= If exists.
		tmp &= ~(0x1 << instance->ao_idx);
		outw(tmp, instance->i_range_reg);
		PDEBUG_REG("i_range_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->i_range_reg - instance->reg_base, tmp);
		break;

	case 1:		// -10V 10V
		tmp = inw(instance->current_on_reg);	// Volts
		tmp &= ~(0x1 << instance->ao_idx);
		outw(tmp, instance->current_on_reg);
		PDEBUG_REG("current_on_reg outw(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->current_on_reg - instance->reg_base, tmp);

		// 0V
		outw(0x0800,
		     (instance->ao_regs_shadows)->registry[instance->ao_idx]);
		PDEBUG_REG("channel_reg outw(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   (instance->ao_regs_shadows)->registry[instance->
								 ao_idx] -
			   instance->reg_base, 0x0800);

		tmp = inw(instance->uni_bi_reg);	// bipolar
		tmp &= ~(0x1 << instance->ao_idx);
		outw(tmp, instance->uni_bi_reg);
		PDEBUG_REG("uni_bi_reg outw(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->uni_bi_reg - instance->reg_base, tmp);

		tmp = inw(instance->i_range_reg);	// 0..20mA <= If exists.
		tmp &= ~(0x1 << instance->ao_idx);
		outw(tmp, instance->i_range_reg);
		PDEBUG_REG("i_range_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->i_range_reg - instance->reg_base, tmp);
		break;

	case 2:		// 0mA 20mA
		tmp = inw(instance->current_on_reg);	// mAmpers
		tmp |= (0x1 << instance->ao_idx);
		outw(tmp, instance->current_on_reg);
		PDEBUG_REG("current_on_reg outw(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->current_on_reg - instance->reg_base, tmp);

		tmp = inw(instance->i_range_reg);	// 0..20mA
		tmp &= ~(0x1 << instance->ao_idx);
		outw(tmp, instance->i_range_reg);
		PDEBUG_REG("i_range_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->i_range_reg - instance->reg_base, tmp);

		// 0mA
		outw(0,
		     (instance->ao_regs_shadows)->registry[instance->ao_idx]);
		PDEBUG_REG("channel_reg outw(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   (instance->ao_regs_shadows)->registry[instance->
								 ao_idx] -
			   instance->reg_base, 0);

		tmp = inw(instance->uni_bi_reg);	// unipolar
		tmp |= (0x1 << instance->ao_idx);
		outw(tmp, instance->uni_bi_reg);
		PDEBUG_REG("uni_bi_reg outw(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->uni_bi_reg - instance->reg_base, tmp);
		break;

	case 3:		// 4mA 20mA
		tmp = inw(instance->current_on_reg);	// mAmpers
		tmp |= (0x1 << instance->ao_idx);
		outw(tmp, instance->current_on_reg);
		PDEBUG_REG("current_on_reg outw(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->current_on_reg - instance->reg_base, tmp);

		tmp = inw(instance->i_range_reg);	// 4..20mA
		tmp |= (0x1 << instance->ao_idx);
		outw(tmp, instance->i_range_reg);
		PDEBUG_REG("i_range_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->i_range_reg - instance->reg_base, tmp);

		// 4mA
		outw(0,
		     (instance->ao_regs_shadows)->registry[instance->ao_idx]);
		PDEBUG_REG("channel_reg outw(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   (instance->ao_regs_shadows)->registry[instance->
								 ao_idx] -
			   instance->reg_base, 0);

		tmp = inw(instance->uni_bi_reg);	// unipolar
		tmp |= (0x1 << instance->ao_idx);
		outw(tmp, instance->uni_bi_reg);
		PDEBUG_REG("uni_bi_reg outw(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->uni_bi_reg - instance->reg_base, tmp);
		break;
	}

	// Trigger output.
	outw(0x0000, instance->sim_output_reg);
	PDEBUG_REG("sim_output_reg outl(0x%lX+0x%lX)=0x%x\n",
		   instance->reg_base,
		   instance->sim_output_reg - instance->reg_base, 0x0000);
	outw(0xFFFF, instance->sim_output_reg);
	PDEBUG_REG("sim_output_reg outl(0x%lX+0x%lX)=0x%x\n",
		   instance->reg_base,
		   instance->sim_output_reg - instance->reg_base, 0xFFFF);

	if (trig_chan == ME_TRIG_CHAN_DEFAULT) {	// Individual triggering.
		(instance->ao_regs_shadows)->synchronous &=
		    ~(0x1 << instance->ao_idx);
		PDEBUG("Individual triggering.\n");
	} else if (trig_chan == ME_TRIG_CHAN_SYNCHRONOUS) {	// Synchronous triggering.
		(instance->ao_regs_shadows)->synchronous |=
		    (0x1 << instance->ao_idx);
		PDEBUG("Synchronous triggering.\n");
	}
	spin_unlock(instance->config_regs_lock);
	spin_unlock(instance->ao_shadows_lock);

	instance->status = ao_status_single_configured;
	spin_unlock(&instance->subdevice_lock);

	ME_SUBDEVICE_EXIT;

	return ME_ERRNO_SUCCESS;
}

static int me1600_ao_io_single_read(me_subdevice_t * subdevice,
				    struct file *filep,
				    int channel,
				    int *value, int time_out, int flags)
{
	me1600_ao_subdevice_t *instance;
	unsigned long delay = 0;
	unsigned long j = 0;
	int err = ME_ERRNO_SUCCESS;

	instance = (me1600_ao_subdevice_t *) subdevice;

	PDEBUG("executed. idx=%d\n", instance->ao_idx);

	if (flags & ~ME_IO_SINGLE_NONBLOCKING) {
		PERROR("Invalid flag specified. %d\n", flags);
		return ME_ERRNO_INVALID_FLAGS;
	}

	if (time_out < 0) {
		PERROR("Invalid timeout specified.\n");
		return ME_ERRNO_INVALID_TIMEOUT;
	}

	if (channel) {
		PERROR("Invalid channel specified.\n");
		return ME_ERRNO_INVALID_CHANNEL;
	}

	if ((!flags) && ((instance->ao_regs_shadows)->trigger & instance->ao_idx)) {	//Blocking mode. Wait for software trigger.
		if (time_out) {
			delay = (time_out * HZ) / 1000;
			if (delay == 0)
				delay = 1;
		}

		j = jiffies;

		//Only runing process will interrupt this call. Events are signaled when status change. This procedure has own timeout.
		wait_event_interruptible_timeout(instance->wait_queue,
						 (!((instance->
						     ao_regs_shadows)->
						    trigger & instance->
						    ao_idx)),
						 (delay) ? delay : LONG_MAX);

		if (instance == ao_status_none) {	// Reset was called.
			PDEBUG("Single canceled.\n");
			err = ME_ERRNO_CANCELLED;
		}

		if (signal_pending(current)) {
			PERROR("Wait on start of state machine interrupted.\n");
			err = ME_ERRNO_SIGNAL;
		}

		if ((delay) && ((jiffies - j) >= delay)) {
			PDEBUG("Timeout reached.\n");
			err = ME_ERRNO_TIMEOUT;
		}
	}

	*value = (instance->ao_regs_shadows)->mirror[instance->ao_idx];

	return err;
}

static int me1600_ao_io_single_write(me_subdevice_t * subdevice,
				     struct file *filep,
				     int channel,
				     int value, int time_out, int flags)
{
	me1600_ao_subdevice_t *instance;
	int err = ME_ERRNO_SUCCESS;
	unsigned long delay = 0;
	int i;
	unsigned long j = 0;

	instance = (me1600_ao_subdevice_t *) subdevice;

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

	if (value & ~ME1600_AO_MAX_DATA) {
		PERROR("Invalid value provided.\n");
		return ME_ERRNO_VALUE_OUT_OF_RANGE;
	}

	if (channel) {
		PERROR("Invalid channel specified.\n");
		return ME_ERRNO_INVALID_CHANNEL;
	}

	ME_SUBDEVICE_ENTER;

	//Cancel control task
	PDEBUG("Cancel control task. idx=%d\n", instance->ao_idx);
	instance->ao_control_task_flag = 0;
	cancel_delayed_work(&instance->ao_control_task);
	(instance->ao_regs_shadows)->trigger &= ~(0x1 << instance->ao_idx);	//Cancell waiting for trigger.

	if (time_out) {
		delay = (time_out * HZ) / 1000;

		if (delay == 0)
			delay = 1;
	}
	//Write value.
	spin_lock(instance->ao_shadows_lock);
	(instance->ao_regs_shadows)->shadow[instance->ao_idx] =
	    (uint16_t) value;

	if (flags & ME_IO_SINGLE_TYPE_TRIG_SYNCHRONOUS) {	// Trigger all outputs from synchronous list.
		for (i = 0; i < (instance->ao_regs_shadows)->count; i++) {
			if (((instance->ao_regs_shadows)->synchronous & (0x1 << i)) || (i == instance->ao_idx)) {	// Set all from synchronous list to correct state.
				PDEBUG
				    ("Synchronous triggering: output %d. idx=%d\n",
				     i, instance->ao_idx);
				(instance->ao_regs_shadows)->mirror[i] =
				    (instance->ao_regs_shadows)->shadow[i];

				outw((instance->ao_regs_shadows)->shadow[i],
				     (instance->ao_regs_shadows)->registry[i]);
				PDEBUG_REG
				    ("channel_reg outw(0x%lX+0x%lX)=0x%x\n",
				     instance->reg_base,
				     (instance->ao_regs_shadows)->registry[i] -
				     instance->reg_base,
				     (instance->ao_regs_shadows)->shadow[i]);

				(instance->ao_regs_shadows)->trigger &=
				    ~(0x1 << i);
			}
		}

		// Trigger output.
		outw(0x0000, instance->sim_output_reg);
		PDEBUG_REG("sim_output_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->sim_output_reg - instance->reg_base, 0);
		outw(0xFFFF, instance->sim_output_reg);
		PDEBUG_REG("sim_output_reg outl(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   instance->sim_output_reg - instance->reg_base,
			   0xFFFF);
		instance->status = ao_status_single_end;
	} else {		// Individual mode.
		if ((instance->ao_regs_shadows)->synchronous & (0x1 << instance->ao_idx)) {	// Put on synchronous start list. Set output as waiting for trigger.
			PDEBUG("Add to synchronous list. idx=%d\n",
			       instance->ao_idx);
			(instance->ao_regs_shadows)->trigger |=
			    (0x1 << instance->ao_idx);
			instance->status = ao_status_single_run;
			PDEBUG("Synchronous list: 0x%x.\n",
			       (instance->ao_regs_shadows)->synchronous);
		} else {	// Fired this one.
			PDEBUG("Triggering. idx=%d\n", instance->ao_idx);
			(instance->ao_regs_shadows)->mirror[instance->ao_idx] =
			    (instance->ao_regs_shadows)->shadow[instance->
								ao_idx];

			outw((instance->ao_regs_shadows)->
			     shadow[instance->ao_idx],
			     (instance->ao_regs_shadows)->registry[instance->
								   ao_idx]);
			PDEBUG_REG("channel_reg outw(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   (instance->ao_regs_shadows)->
				   registry[instance->ao_idx] -
				   instance->reg_base,
				   (instance->ao_regs_shadows)->
				   shadow[instance->ao_idx]);

			// Set output as triggered.
			(instance->ao_regs_shadows)->trigger &=
			    ~(0x1 << instance->ao_idx);

			// Trigger output.
			outw(0x0000, instance->sim_output_reg);
			PDEBUG_REG("sim_output_reg outl(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->sim_output_reg -
				   instance->reg_base, 0);
			outw(0xFFFF, instance->sim_output_reg);
			PDEBUG_REG("sim_output_reg outl(0x%lX+0x%lX)=0x%x\n",
				   instance->reg_base,
				   instance->sim_output_reg -
				   instance->reg_base, 0xFFFF);
			instance->status = ao_status_single_end;
		}
	}
	spin_unlock(instance->ao_shadows_lock);

	//Init control task
	instance->timeout.delay = delay;
	instance->timeout.start_time = jiffies;
	instance->ao_control_task_flag = 1;
	queue_delayed_work(instance->me1600_workqueue,
			   &instance->ao_control_task, 1);

	if ((!flags & ME_IO_SINGLE_TYPE_WRITE_NONBLOCKING) && ((instance->ao_regs_shadows)->trigger & instance->ao_idx)) {	//Blocking mode. Wait for software trigger.
		if (time_out) {
			delay = (time_out * HZ) / 1000;
			if (delay == 0)
				delay = 1;
		}

		j = jiffies;

		//Only runing process will interrupt this call. Events are signaled when status change. This procedure has own timeout.
		wait_event_interruptible_timeout(instance->wait_queue,
						 (!((instance->
						     ao_regs_shadows)->
						    trigger & instance->
						    ao_idx)),
						 (delay) ? delay : LONG_MAX);

		if (instance == ao_status_none) {
			PDEBUG("Single canceled.\n");
			err = ME_ERRNO_CANCELLED;
		}
		if (signal_pending(current)) {
			PERROR("Wait on start of state machine interrupted.\n");
			err = ME_ERRNO_SIGNAL;
		}

		if ((delay) && ((jiffies - j) >= delay)) {
			PDEBUG("Timeout reached.\n");
			err = ME_ERRNO_TIMEOUT;
		}
	}

	ME_SUBDEVICE_EXIT;

	return err;
}

static int me1600_ao_query_number_channels(me_subdevice_t * subdevice,
					   int *number)
{
	me1600_ao_subdevice_t *instance;
	instance = (me1600_ao_subdevice_t *) subdevice;

	PDEBUG("executed. idx=%d\n", instance->ao_idx);

	*number = 1;		//Every subdevice has only 1 channel.
	return ME_ERRNO_SUCCESS;
}

static int me1600_ao_query_subdevice_type(me_subdevice_t * subdevice, int *type,
					  int *subtype)
{
	me1600_ao_subdevice_t *instance;
	instance = (me1600_ao_subdevice_t *) subdevice;

	PDEBUG("executed. idx=%d\n", instance->ao_idx);

	*type = ME_TYPE_AO;
	*subtype = ME_SUBTYPE_SINGLE;
	return ME_ERRNO_SUCCESS;
}

static int me1600_ao_query_subdevice_caps(me_subdevice_t * subdevice, int *caps)
{
	PDEBUG("executed.\n");
	*caps = ME_CAPS_AO_TRIG_SYNCHRONOUS;
	return ME_ERRNO_SUCCESS;
}

static int me1600_ao_query_range_by_min_max(me_subdevice_t * subdevice,
					    int unit,
					    int *min,
					    int *max, int *maxdata, int *range)
{
	me1600_ao_subdevice_t *instance;
	int i;
	int r = -1;
	int diff = 21E6;

	instance = (me1600_ao_subdevice_t *) subdevice;

	PDEBUG("executed. idx=%d\n", instance->ao_idx);

	if ((*max - *min) < 0) {
		PERROR("Invalid minimum and maximum values specified.\n");
		return ME_ERRNO_INVALID_MIN_MAX;
	}
	// Maximum ranges are slightly less then 10V or 20mA. For convenient we accepted this value as valid one.
	if (unit == ME_UNIT_VOLT) {
		for (i = 0; i < instance->u_ranges_count; i++) {
			if ((instance->u_ranges[i].min <= *min)
			    && ((instance->u_ranges[i].max + 5000) >= *max)) {
				if ((instance->u_ranges[i].max -
				     instance->u_ranges[i].min) - (*max -
								   *min) <
				    diff) {
					r = i;
					diff =
					    (instance->u_ranges[i].max -
					     instance->u_ranges[i].min) -
					    (*max - *min);
				}
			}
		}

		if (r < 0) {
			PERROR("No matching range found.\n");
			return ME_ERRNO_NO_RANGE;
		} else {
			*min = instance->u_ranges[r].min;
			*max = instance->u_ranges[r].max;
			*range = r;
		}
	} else if (unit == ME_UNIT_AMPERE) {
		for (i = 0; i < instance->i_ranges_count; i++) {
			if ((instance->i_ranges[i].min <= *min)
			    && (instance->i_ranges[i].max + 5000 >= *max)) {
				if ((instance->i_ranges[i].max -
				     instance->i_ranges[i].min) - (*max -
								   *min) <
				    diff) {
					r = i;
					diff =
					    (instance->i_ranges[i].max -
					     instance->i_ranges[i].min) -
					    (*max - *min);
				}
			}
		}

		if (r < 0) {
			PERROR("No matching range found.\n");
			return ME_ERRNO_NO_RANGE;
		} else {
			*min = instance->i_ranges[r].min;
			*max = instance->i_ranges[r].max;
			*range = r + instance->u_ranges_count;
		}
	} else {
		PERROR("Invalid physical unit specified.\n");
		return ME_ERRNO_INVALID_UNIT;
	}
	*maxdata = ME1600_AO_MAX_DATA;

	return ME_ERRNO_SUCCESS;
}

static int me1600_ao_query_number_ranges(me_subdevice_t * subdevice,
					 int unit, int *count)
{
	me1600_ao_subdevice_t *instance;

	PDEBUG("executed.\n");

	instance = (me1600_ao_subdevice_t *) subdevice;
	switch (unit) {
	case ME_UNIT_VOLT:
		*count = instance->u_ranges_count;
		break;
	case ME_UNIT_AMPERE:
		*count = instance->i_ranges_count;
		break;
	case ME_UNIT_ANY:
		*count = instance->u_ranges_count + instance->i_ranges_count;
		break;
	default:
		*count = 0;
	}

	return ME_ERRNO_SUCCESS;
}

static int me1600_ao_query_range_info(me_subdevice_t * subdevice,
				      int range,
				      int *unit,
				      int *min, int *max, int *maxdata)
{
	me1600_ao_subdevice_t *instance;

	PDEBUG("executed.\n");

	instance = (me1600_ao_subdevice_t *) subdevice;

	if (((range + 1) >
	     (instance->u_ranges_count + instance->i_ranges_count))
	    || (range < 0)) {
		PERROR("Invalid range number specified.\n");
		return ME_ERRNO_INVALID_RANGE;
	}

	if (range < instance->u_ranges_count) {
		*unit = ME_UNIT_VOLT;
		*min = instance->u_ranges[range].min;
		*max = instance->u_ranges[range].max;
	} else if (range < instance->u_ranges_count + instance->i_ranges_count) {
		*unit = ME_UNIT_AMPERE;
		*min = instance->i_ranges[range - instance->u_ranges_count].min;
		*max = instance->i_ranges[range - instance->u_ranges_count].max;
	}
	*maxdata = ME1600_AO_MAX_DATA;

	return ME_ERRNO_SUCCESS;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
static void me1600_ao_work_control_task(void *subdevice)
#else
static void me1600_ao_work_control_task(struct work_struct *work)
#endif
{
	me1600_ao_subdevice_t *instance;
	int reschedule = 1;
	int signaling = 0;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
	instance = (me1600_ao_subdevice_t *) subdevice;
#else
	instance =
	    container_of((void *)work, me1600_ao_subdevice_t, ao_control_task);
#endif

	PINFO("<%s: %ld> executed. idx=%d\n", __func__, jiffies,
	      instance->ao_idx);

	if (!((instance->ao_regs_shadows)->trigger & instance->ao_idx)) {	// Output was triggerd.
		// Signal the end.
		signaling = 1;
		reschedule = 0;
		if (instance->status == ao_status_single_run) {
			instance->status = ao_status_single_end;
		}

	} else if ((instance->timeout.delay) && ((jiffies - instance->timeout.start_time) >= instance->timeout.delay)) {	// Timeout
		PDEBUG("Timeout reached.\n");
		spin_lock(instance->ao_shadows_lock);
		// Restore old settings.
		PDEBUG("Write old value back to register.\n");
		(instance->ao_regs_shadows)->shadow[instance->ao_idx] =
		    (instance->ao_regs_shadows)->mirror[instance->ao_idx];

		outw((instance->ao_regs_shadows)->mirror[instance->ao_idx],
		     (instance->ao_regs_shadows)->registry[instance->ao_idx]);
		PDEBUG_REG("channel_reg outw(0x%lX+0x%lX)=0x%x\n",
			   instance->reg_base,
			   (instance->ao_regs_shadows)->registry[instance->
								 ao_idx] -
			   instance->reg_base,
			   (instance->ao_regs_shadows)->mirror[instance->
							       ao_idx]);

		//Remove from synchronous strt list.
		(instance->ao_regs_shadows)->trigger &=
		    ~(0x1 << instance->ao_idx);
		if (instance->status == ao_status_none) {
			instance->status = ao_status_single_end;
		}
		spin_unlock(instance->ao_shadows_lock);

		// Signal the end.
		signaling = 1;
		reschedule = 0;
	}

	if (signaling) {	//Signal it.
		wake_up_interruptible_all(&instance->wait_queue);
	}

	if (instance->ao_control_task_flag && reschedule) {	// Reschedule task
		queue_delayed_work(instance->me1600_workqueue,
				   &instance->ao_control_task, 1);
	} else {
		PINFO("<%s> Ending control task.\n", __func__);
	}

}
