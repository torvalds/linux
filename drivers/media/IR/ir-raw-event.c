/* ir-raw-event.c - handle IR Pulse/Space event
 *
 * Copyright (C) 2010 by Mauro Carvalho Chehab <mchehab@redhat.com>
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

#include <media/ir-core.h>
#include <linux/workqueue.h>

/* Define the max number of bit transitions per IR keycode */
#define MAX_IR_EVENT_SIZE	256

/* Used to handle IR raw handler extensions */
static LIST_HEAD(ir_raw_handler_list);
static DEFINE_MUTEX(ir_raw_handler_lock);

/* Used to load the decoders */
static struct work_struct wq_load;

static void ir_keyup_timer(unsigned long data)
{
	struct input_dev *input_dev = (struct input_dev *)data;

	ir_keyup(input_dev);
}

int ir_raw_event_register(struct input_dev *input_dev)
{
	struct ir_input_dev *ir = input_get_drvdata(input_dev);
	int rc, size;

	ir->raw = kzalloc(sizeof(*ir->raw), GFP_KERNEL);

	size = sizeof(struct ir_raw_event) * MAX_IR_EVENT_SIZE * 2;
	size = roundup_pow_of_two(size);

	init_timer(&ir->raw->timer_keyup);
	ir->raw->timer_keyup.function = ir_keyup_timer;
	ir->raw->timer_keyup.data = (unsigned long)input_dev;
	set_bit(EV_REP, input_dev->evbit);

	rc = kfifo_alloc(&ir->raw->kfifo, size, GFP_KERNEL);

	return rc;
}
EXPORT_SYMBOL_GPL(ir_raw_event_register);

void ir_raw_event_unregister(struct input_dev *input_dev)
{
	struct ir_input_dev *ir = input_get_drvdata(input_dev);

	if (!ir->raw)
		return;

	del_timer_sync(&ir->raw->timer_keyup);

	kfifo_free(&ir->raw->kfifo);
	kfree(ir->raw);
	ir->raw = NULL;
}
EXPORT_SYMBOL_GPL(ir_raw_event_unregister);

int ir_raw_event_store(struct input_dev *input_dev, enum raw_event_type type)
{
	struct ir_input_dev	*ir = input_get_drvdata(input_dev);
	struct timespec		ts;
	struct ir_raw_event	event;
	int			rc;

	if (!ir->raw)
		return -EINVAL;

	event.type = type;
	event.delta.tv_sec = 0;
	event.delta.tv_nsec = 0;

	ktime_get_ts(&ts);

	if (timespec_equal(&ir->raw->last_event, &event.delta))
		event.type |= IR_START_EVENT;
	else
		event.delta = timespec_sub(ts, ir->raw->last_event);

	memcpy(&ir->raw->last_event, &ts, sizeof(ts));

	if (event.delta.tv_sec) {
		event.type |= IR_START_EVENT;
		event.delta.tv_sec = 0;
		event.delta.tv_nsec = 0;
	}

	kfifo_in(&ir->raw->kfifo, &event, sizeof(event));

	return rc;
}
EXPORT_SYMBOL_GPL(ir_raw_event_store);

int ir_raw_event_handle(struct input_dev *input_dev)
{
	struct ir_input_dev		*ir = input_get_drvdata(input_dev);
	int				rc;
	struct ir_raw_event		*evs;
	int 				len, i;
	struct ir_raw_handler		*ir_raw_handler;

	/*
	 * Store the events into a temporary buffer. This allows calling more than
	 * one decoder to deal with the received data
	 */
	len = kfifo_len(&ir->raw->kfifo) / sizeof(*evs);
	if (!len)
		return 0;
	evs = kmalloc(len * sizeof(*evs), GFP_ATOMIC);

	for (i = 0; i < len; i++) {
		rc = kfifo_out(&ir->raw->kfifo, &evs[i], sizeof(*evs));
		if (rc != sizeof(*evs)) {
			IR_dprintk(1, "overflow error: received %d instead of %zd\n",
				   rc, sizeof(*evs));
			return -EINVAL;
		}
		IR_dprintk(2, "event type %d, time before event: %07luus\n",
			evs[i].type, (evs[i].delta.tv_nsec + 500) / 1000);
	}

	/*
	 * Call all ir decoders. This allows decoding the same event with
	 * more than one protocol handler.
	 * FIXME: better handle the returned code: does it make sense to use
	 * other decoders, if the first one already handled the IR?
	 */
	list_for_each_entry(ir_raw_handler, &ir_raw_handler_list, list) {
		rc = ir_raw_handler->decode(input_dev, evs, len);
	}

	kfree(evs);

	return rc;
}
EXPORT_SYMBOL_GPL(ir_raw_event_handle);

/*
 * Extension interface - used to register the IR decoders
 */

int ir_raw_handler_register(struct ir_raw_handler *ir_raw_handler)
{
	mutex_lock(&ir_raw_handler_lock);
	list_add_tail(&ir_raw_handler->list, &ir_raw_handler_list);
	mutex_unlock(&ir_raw_handler_lock);
	return 0;
}
EXPORT_SYMBOL(ir_raw_handler_register);

void ir_raw_handler_unregister(struct ir_raw_handler *ir_raw_handler)
{
	mutex_lock(&ir_raw_handler_lock);
	list_del(&ir_raw_handler->list);
	mutex_unlock(&ir_raw_handler_lock);
}
EXPORT_SYMBOL(ir_raw_handler_unregister);

static void init_decoders(struct work_struct *work)
{
	/* Load the decoder modules */

	load_nec_decode();

	/* If needed, we may later add some init code. In this case,
	   it is needed to change the CONFIG_MODULE test at ir-core.h
	 */
}

void ir_raw_init(void)
{
	INIT_WORK(&wq_load, init_decoders);
	schedule_work(&wq_load);
}