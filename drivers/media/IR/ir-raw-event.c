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

/* Define the max number of bit transitions per IR keycode */
#define MAX_IR_EVENT_SIZE	256

int ir_raw_event_register(struct input_dev *input_dev)
{
	struct ir_input_dev *ir = input_get_drvdata(input_dev);
	int rc, size;

	ir->raw = kzalloc(sizeof(*ir->raw), GFP_KERNEL);

	size = sizeof(struct ir_raw_event) * MAX_IR_EVENT_SIZE * 2;
	size = roundup_pow_of_two(size);

	rc = kfifo_alloc(&ir->raw->kfifo, size, GFP_KERNEL);

	return rc;
}
EXPORT_SYMBOL_GPL(ir_raw_event_register);

void ir_raw_event_unregister(struct input_dev *input_dev)
{
	struct ir_input_dev *ir = input_get_drvdata(input_dev);

	if (!ir->raw)
		return;

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

	rc = ir_nec_decode(input_dev, evs, len);

	kfree(evs);

	return rc;
}
EXPORT_SYMBOL_GPL(ir_raw_event_handle);
