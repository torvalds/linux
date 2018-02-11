/* rc-ir-raw.c - handle IR pulse/space events
 *
 * Copyright (C) 2010 by Mauro Carvalho Chehab
 *
 * This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#include <linux/export.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/kmod.h>
#include <linux/sched.h>
#include "rc-core-priv.h"

/* Used to keep track of IR raw clients, protected by ir_raw_handler_lock */
static LIST_HEAD(ir_raw_client_list);

/* Used to handle IR raw handler extensions */
static DEFINE_MUTEX(ir_raw_handler_lock);
static LIST_HEAD(ir_raw_handler_list);
static atomic64_t available_protocols = ATOMIC64_INIT(0);

static int ir_raw_event_thread(void *data)
{
	struct ir_raw_event ev;
	struct ir_raw_handler *handler;
	struct ir_raw_event_ctrl *raw = (struct ir_raw_event_ctrl *)data;

	while (1) {
		mutex_lock(&ir_raw_handler_lock);
		while (kfifo_out(&raw->kfifo, &ev, 1)) {
			list_for_each_entry(handler, &ir_raw_handler_list, list)
				if (raw->dev->enabled_protocols &
				    handler->protocols || !handler->protocols)
					handler->decode(raw->dev, ev);
			raw->prev_ev = ev;
		}
		mutex_unlock(&ir_raw_handler_lock);

		set_current_state(TASK_INTERRUPTIBLE);

		if (kthread_should_stop()) {
			__set_current_state(TASK_RUNNING);
			break;
		} else if (!kfifo_is_empty(&raw->kfifo))
			set_current_state(TASK_RUNNING);

		schedule();
	}

	return 0;
}

/**
 * ir_raw_event_store() - pass a pulse/space duration to the raw ir decoders
 * @dev:	the struct rc_dev device descriptor
 * @ev:		the struct ir_raw_event descriptor of the pulse/space
 *
 * This routine (which may be called from an interrupt context) stores a
 * pulse/space duration for the raw ir decoding state machines. Pulses are
 * signalled as positive values and spaces as negative values. A zero value
 * will reset the decoding state machines.
 */
int ir_raw_event_store(struct rc_dev *dev, struct ir_raw_event *ev)
{
	if (!dev->raw)
		return -EINVAL;

	IR_dprintk(2, "sample: (%05dus %s)\n",
		   TO_US(ev->duration), TO_STR(ev->pulse));

	if (!kfifo_put(&dev->raw->kfifo, *ev)) {
		dev_err(&dev->dev, "IR event FIFO is full!\n");
		return -ENOSPC;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(ir_raw_event_store);

/**
 * ir_raw_event_store_edge() - notify raw ir decoders of the start of a pulse/space
 * @dev:	the struct rc_dev device descriptor
 * @pulse:	true for pulse, false for space
 *
 * This routine (which may be called from an interrupt context) is used to
 * store the beginning of an ir pulse or space (or the start/end of ir
 * reception) for the raw ir decoding state machines. This is used by
 * hardware which does not provide durations directly but only interrupts
 * (or similar events) on state change.
 */
int ir_raw_event_store_edge(struct rc_dev *dev, bool pulse)
{
	ktime_t			now;
	DEFINE_IR_RAW_EVENT(ev);
	int			rc = 0;

	if (!dev->raw)
		return -EINVAL;

	now = ktime_get();
	ev.duration = ktime_to_ns(ktime_sub(now, dev->raw->last_event));
	ev.pulse = !pulse;

	rc = ir_raw_event_store(dev, &ev);

	dev->raw->last_event = now;

	/* timer could be set to timeout (125ms by default) */
	if (!timer_pending(&dev->raw->edge_handle) ||
	    time_after(dev->raw->edge_handle.expires,
		       jiffies + msecs_to_jiffies(15))) {
		mod_timer(&dev->raw->edge_handle,
			  jiffies + msecs_to_jiffies(15));
	}

	return rc;
}
EXPORT_SYMBOL_GPL(ir_raw_event_store_edge);

/**
 * ir_raw_event_store_with_filter() - pass next pulse/space to decoders with some processing
 * @dev:	the struct rc_dev device descriptor
 * @ev:		the event that has occurred
 *
 * This routine (which may be called from an interrupt context) works
 * in similar manner to ir_raw_event_store_edge.
 * This routine is intended for devices with limited internal buffer
 * It automerges samples of same type, and handles timeouts. Returns non-zero
 * if the event was added, and zero if the event was ignored due to idle
 * processing.
 */
int ir_raw_event_store_with_filter(struct rc_dev *dev, struct ir_raw_event *ev)
{
	if (!dev->raw)
		return -EINVAL;

	/* Ignore spaces in idle mode */
	if (dev->idle && !ev->pulse)
		return 0;
	else if (dev->idle)
		ir_raw_event_set_idle(dev, false);

	if (!dev->raw->this_ev.duration)
		dev->raw->this_ev = *ev;
	else if (ev->pulse == dev->raw->this_ev.pulse)
		dev->raw->this_ev.duration += ev->duration;
	else {
		ir_raw_event_store(dev, &dev->raw->this_ev);
		dev->raw->this_ev = *ev;
	}

	/* Enter idle mode if nessesary */
	if (!ev->pulse && dev->timeout &&
	    dev->raw->this_ev.duration >= dev->timeout)
		ir_raw_event_set_idle(dev, true);

	return 1;
}
EXPORT_SYMBOL_GPL(ir_raw_event_store_with_filter);

/**
 * ir_raw_event_set_idle() - provide hint to rc-core when the device is idle or not
 * @dev:	the struct rc_dev device descriptor
 * @idle:	whether the device is idle or not
 */
void ir_raw_event_set_idle(struct rc_dev *dev, bool idle)
{
	if (!dev->raw)
		return;

	IR_dprintk(2, "%s idle mode\n", idle ? "enter" : "leave");

	if (idle) {
		dev->raw->this_ev.timeout = true;
		ir_raw_event_store(dev, &dev->raw->this_ev);
		init_ir_raw_event(&dev->raw->this_ev);
	}

	if (dev->s_idle)
		dev->s_idle(dev, idle);

	dev->idle = idle;
}
EXPORT_SYMBOL_GPL(ir_raw_event_set_idle);

/**
 * ir_raw_event_handle() - schedules the decoding of stored ir data
 * @dev:	the struct rc_dev device descriptor
 *
 * This routine will tell rc-core to start decoding stored ir data.
 */
void ir_raw_event_handle(struct rc_dev *dev)
{
	if (!dev->raw || !dev->raw->thread)
		return;

	wake_up_process(dev->raw->thread);
}
EXPORT_SYMBOL_GPL(ir_raw_event_handle);

/* used internally by the sysfs interface */
u64
ir_raw_get_allowed_protocols(void)
{
	return atomic64_read(&available_protocols);
}

static int change_protocol(struct rc_dev *dev, u64 *rc_proto)
{
	/* the caller will update dev->enabled_protocols */
	return 0;
}

static void ir_raw_disable_protocols(struct rc_dev *dev, u64 protocols)
{
	mutex_lock(&dev->lock);
	dev->enabled_protocols &= ~protocols;
	mutex_unlock(&dev->lock);
}

/**
 * ir_raw_gen_manchester() - Encode data with Manchester (bi-phase) modulation.
 * @ev:		Pointer to pointer to next free event. *@ev is incremented for
 *		each raw event filled.
 * @max:	Maximum number of raw events to fill.
 * @timings:	Manchester modulation timings.
 * @n:		Number of bits of data.
 * @data:	Data bits to encode.
 *
 * Encodes the @n least significant bits of @data using Manchester (bi-phase)
 * modulation with the timing characteristics described by @timings, writing up
 * to @max raw IR events using the *@ev pointer.
 *
 * Returns:	0 on success.
 *		-ENOBUFS if there isn't enough space in the array to fit the
 *		full encoded data. In this case all @max events will have been
 *		written.
 */
int ir_raw_gen_manchester(struct ir_raw_event **ev, unsigned int max,
			  const struct ir_raw_timings_manchester *timings,
			  unsigned int n, u64 data)
{
	bool need_pulse;
	u64 i;
	int ret = -ENOBUFS;

	i = BIT_ULL(n - 1);

	if (timings->leader) {
		if (!max--)
			return ret;
		if (timings->pulse_space_start) {
			init_ir_raw_event_duration((*ev)++, 1, timings->leader);

			if (!max--)
				return ret;
			init_ir_raw_event_duration((*ev), 0, timings->leader);
		} else {
			init_ir_raw_event_duration((*ev), 1, timings->leader);
		}
		i >>= 1;
	} else {
		/* continue existing signal */
		--(*ev);
	}
	/* from here on *ev will point to the last event rather than the next */

	while (n && i > 0) {
		need_pulse = !(data & i);
		if (timings->invert)
			need_pulse = !need_pulse;
		if (need_pulse == !!(*ev)->pulse) {
			(*ev)->duration += timings->clock;
		} else {
			if (!max--)
				goto nobufs;
			init_ir_raw_event_duration(++(*ev), need_pulse,
						   timings->clock);
		}

		if (!max--)
			goto nobufs;
		init_ir_raw_event_duration(++(*ev), !need_pulse,
					   timings->clock);
		i >>= 1;
	}

	if (timings->trailer_space) {
		if (!(*ev)->pulse)
			(*ev)->duration += timings->trailer_space;
		else if (!max--)
			goto nobufs;
		else
			init_ir_raw_event_duration(++(*ev), 0,
						   timings->trailer_space);
	}

	ret = 0;
nobufs:
	/* point to the next event rather than last event before returning */
	++(*ev);
	return ret;
}
EXPORT_SYMBOL(ir_raw_gen_manchester);

/**
 * ir_raw_gen_pd() - Encode data to raw events with pulse-distance modulation.
 * @ev:		Pointer to pointer to next free event. *@ev is incremented for
 *		each raw event filled.
 * @max:	Maximum number of raw events to fill.
 * @timings:	Pulse distance modulation timings.
 * @n:		Number of bits of data.
 * @data:	Data bits to encode.
 *
 * Encodes the @n least significant bits of @data using pulse-distance
 * modulation with the timing characteristics described by @timings, writing up
 * to @max raw IR events using the *@ev pointer.
 *
 * Returns:	0 on success.
 *		-ENOBUFS if there isn't enough space in the array to fit the
 *		full encoded data. In this case all @max events will have been
 *		written.
 */
int ir_raw_gen_pd(struct ir_raw_event **ev, unsigned int max,
		  const struct ir_raw_timings_pd *timings,
		  unsigned int n, u64 data)
{
	int i;
	int ret;
	unsigned int space;

	if (timings->header_pulse) {
		ret = ir_raw_gen_pulse_space(ev, &max, timings->header_pulse,
					     timings->header_space);
		if (ret)
			return ret;
	}

	if (timings->msb_first) {
		for (i = n - 1; i >= 0; --i) {
			space = timings->bit_space[(data >> i) & 1];
			ret = ir_raw_gen_pulse_space(ev, &max,
						     timings->bit_pulse,
						     space);
			if (ret)
				return ret;
		}
	} else {
		for (i = 0; i < n; ++i, data >>= 1) {
			space = timings->bit_space[data & 1];
			ret = ir_raw_gen_pulse_space(ev, &max,
						     timings->bit_pulse,
						     space);
			if (ret)
				return ret;
		}
	}

	ret = ir_raw_gen_pulse_space(ev, &max, timings->trailer_pulse,
				     timings->trailer_space);
	return ret;
}
EXPORT_SYMBOL(ir_raw_gen_pd);

/**
 * ir_raw_gen_pl() - Encode data to raw events with pulse-length modulation.
 * @ev:		Pointer to pointer to next free event. *@ev is incremented for
 *		each raw event filled.
 * @max:	Maximum number of raw events to fill.
 * @timings:	Pulse distance modulation timings.
 * @n:		Number of bits of data.
 * @data:	Data bits to encode.
 *
 * Encodes the @n least significant bits of @data using space-distance
 * modulation with the timing characteristics described by @timings, writing up
 * to @max raw IR events using the *@ev pointer.
 *
 * Returns:	0 on success.
 *		-ENOBUFS if there isn't enough space in the array to fit the
 *		full encoded data. In this case all @max events will have been
 *		written.
 */
int ir_raw_gen_pl(struct ir_raw_event **ev, unsigned int max,
		  const struct ir_raw_timings_pl *timings,
		  unsigned int n, u64 data)
{
	int i;
	int ret = -ENOBUFS;
	unsigned int pulse;

	if (!max--)
		return ret;

	init_ir_raw_event_duration((*ev)++, 1, timings->header_pulse);

	if (timings->msb_first) {
		for (i = n - 1; i >= 0; --i) {
			if (!max--)
				return ret;
			init_ir_raw_event_duration((*ev)++, 0,
						   timings->bit_space);
			if (!max--)
				return ret;
			pulse = timings->bit_pulse[(data >> i) & 1];
			init_ir_raw_event_duration((*ev)++, 1, pulse);
		}
	} else {
		for (i = 0; i < n; ++i, data >>= 1) {
			if (!max--)
				return ret;
			init_ir_raw_event_duration((*ev)++, 0,
						   timings->bit_space);
			if (!max--)
				return ret;
			pulse = timings->bit_pulse[data & 1];
			init_ir_raw_event_duration((*ev)++, 1, pulse);
		}
	}

	if (!max--)
		return ret;

	init_ir_raw_event_duration((*ev)++, 0, timings->trailer_space);

	return 0;
}
EXPORT_SYMBOL(ir_raw_gen_pl);

/**
 * ir_raw_encode_scancode() - Encode a scancode as raw events
 *
 * @protocol:		protocol
 * @scancode:		scancode filter describing a single scancode
 * @events:		array of raw events to write into
 * @max:		max number of raw events
 *
 * Attempts to encode the scancode as raw events.
 *
 * Returns:	The number of events written.
 *		-ENOBUFS if there isn't enough space in the array to fit the
 *		encoding. In this case all @max events will have been written.
 *		-EINVAL if the scancode is ambiguous or invalid, or if no
 *		compatible encoder was found.
 */
int ir_raw_encode_scancode(enum rc_proto protocol, u32 scancode,
			   struct ir_raw_event *events, unsigned int max)
{
	struct ir_raw_handler *handler;
	int ret = -EINVAL;
	u64 mask = 1ULL << protocol;

	mutex_lock(&ir_raw_handler_lock);
	list_for_each_entry(handler, &ir_raw_handler_list, list) {
		if (handler->protocols & mask && handler->encode) {
			ret = handler->encode(protocol, scancode, events, max);
			if (ret >= 0 || ret == -ENOBUFS)
				break;
		}
	}
	mutex_unlock(&ir_raw_handler_lock);

	return ret;
}
EXPORT_SYMBOL(ir_raw_encode_scancode);

static void edge_handle(struct timer_list *t)
{
	struct ir_raw_event_ctrl *raw = from_timer(raw, t, edge_handle);
	struct rc_dev *dev = raw->dev;
	ktime_t interval = ktime_sub(ktime_get(), dev->raw->last_event);

	if (ktime_to_ns(interval) >= dev->timeout) {
		DEFINE_IR_RAW_EVENT(ev);

		ev.timeout = true;
		ev.duration = ktime_to_ns(interval);

		ir_raw_event_store(dev, &ev);
	} else {
		mod_timer(&dev->raw->edge_handle,
			  jiffies + nsecs_to_jiffies(dev->timeout -
						     ktime_to_ns(interval)));
	}

	ir_raw_event_handle(dev);
}

/*
 * Used to (un)register raw event clients
 */
int ir_raw_event_prepare(struct rc_dev *dev)
{
	static bool raw_init; /* 'false' default value, raw decoders loaded? */

	if (!dev)
		return -EINVAL;

	if (!raw_init) {
		request_module("ir-lirc-codec");
		raw_init = true;
	}

	dev->raw = kzalloc(sizeof(*dev->raw), GFP_KERNEL);
	if (!dev->raw)
		return -ENOMEM;

	dev->raw->dev = dev;
	dev->change_protocol = change_protocol;
	timer_setup(&dev->raw->edge_handle, edge_handle, 0);
	INIT_KFIFO(dev->raw->kfifo);

	return 0;
}

int ir_raw_event_register(struct rc_dev *dev)
{
	struct ir_raw_handler *handler;
	struct task_struct *thread;

	/*
	 * raw transmitters do not need any event registration
	 * because the event is coming from userspace
	 */
	if (dev->driver_type != RC_DRIVER_IR_RAW_TX) {
		thread = kthread_run(ir_raw_event_thread, dev->raw, "rc%u",
				     dev->minor);

		if (IS_ERR(thread))
			return PTR_ERR(thread);

		dev->raw->thread = thread;
	}

	mutex_lock(&ir_raw_handler_lock);
	list_add_tail(&dev->raw->list, &ir_raw_client_list);
	list_for_each_entry(handler, &ir_raw_handler_list, list)
		if (handler->raw_register)
			handler->raw_register(dev);
	mutex_unlock(&ir_raw_handler_lock);

	return 0;
}

void ir_raw_event_free(struct rc_dev *dev)
{
	if (!dev)
		return;

	kfree(dev->raw);
	dev->raw = NULL;
}

void ir_raw_event_unregister(struct rc_dev *dev)
{
	struct ir_raw_handler *handler;

	if (!dev || !dev->raw)
		return;

	kthread_stop(dev->raw->thread);
	del_timer_sync(&dev->raw->edge_handle);

	mutex_lock(&ir_raw_handler_lock);
	list_del(&dev->raw->list);
	list_for_each_entry(handler, &ir_raw_handler_list, list)
		if (handler->raw_unregister)
			handler->raw_unregister(dev);
	mutex_unlock(&ir_raw_handler_lock);

	ir_raw_event_free(dev);
}

/*
 * Extension interface - used to register the IR decoders
 */

int ir_raw_handler_register(struct ir_raw_handler *ir_raw_handler)
{
	struct ir_raw_event_ctrl *raw;

	mutex_lock(&ir_raw_handler_lock);
	list_add_tail(&ir_raw_handler->list, &ir_raw_handler_list);
	if (ir_raw_handler->raw_register)
		list_for_each_entry(raw, &ir_raw_client_list, list)
			ir_raw_handler->raw_register(raw->dev);
	atomic64_or(ir_raw_handler->protocols, &available_protocols);
	mutex_unlock(&ir_raw_handler_lock);

	return 0;
}
EXPORT_SYMBOL(ir_raw_handler_register);

void ir_raw_handler_unregister(struct ir_raw_handler *ir_raw_handler)
{
	struct ir_raw_event_ctrl *raw;
	u64 protocols = ir_raw_handler->protocols;

	mutex_lock(&ir_raw_handler_lock);
	list_del(&ir_raw_handler->list);
	list_for_each_entry(raw, &ir_raw_client_list, list) {
		ir_raw_disable_protocols(raw->dev, protocols);
		if (ir_raw_handler->raw_unregister)
			ir_raw_handler->raw_unregister(raw->dev);
	}
	atomic64_andnot(protocols, &available_protocols);
	mutex_unlock(&ir_raw_handler_lock);
}
EXPORT_SYMBOL(ir_raw_handler_unregister);
