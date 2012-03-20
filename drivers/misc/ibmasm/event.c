
/*
 * IBM ASM Service Processor Device Driver
 *
 * This program is free software; you can redistribute it and/or modify
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) IBM Corporation, 2004
 *
 * Author: Max Asböck <amax@us.ibm.com>
 *
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include "ibmasm.h"
#include "lowlevel.h"

/*
 * ASM service processor event handling routines.
 *
 * Events are signalled to the device drivers through interrupts.
 * They have the format of dot commands, with the type field set to
 * sp_event.
 * The driver does not interpret the events, it simply stores them in a
 * circular buffer.
 */

static void wake_up_event_readers(struct service_processor *sp)
{
	struct event_reader *reader;

	list_for_each_entry(reader, &sp->event_buffer->readers, node)
                wake_up_interruptible(&reader->wait);
}

/**
 * receive_event
 * Called by the interrupt handler when a dot command of type sp_event is
 * received.
 * Store the event in the circular event buffer, wake up any sleeping
 * event readers.
 * There is no reader marker in the buffer, therefore readers are
 * responsible for keeping up with the writer, or they will lose events.
 */
void ibmasm_receive_event(struct service_processor *sp, void *data, unsigned int data_size)
{
	struct event_buffer *buffer = sp->event_buffer;
	struct ibmasm_event *event;
	unsigned long flags;

	data_size = min(data_size, IBMASM_EVENT_MAX_SIZE);

	spin_lock_irqsave(&sp->lock, flags);
	/* copy the event into the next slot in the circular buffer */
	event = &buffer->events[buffer->next_index];
	memcpy_fromio(event->data, data, data_size);
	event->data_size = data_size;
	event->serial_number = buffer->next_serial_number;

	/* advance indices in the buffer */
	buffer->next_index = (buffer->next_index + 1) % IBMASM_NUM_EVENTS;
	buffer->next_serial_number++;
	spin_unlock_irqrestore(&sp->lock, flags);

	wake_up_event_readers(sp);
}

static inline int event_available(struct event_buffer *b, struct event_reader *r)
{
	return (r->next_serial_number < b->next_serial_number);
}

/**
 * get_next_event
 * Called by event readers (initiated from user space through the file
 * system).
 * Sleeps until a new event is available.
 */
int ibmasm_get_next_event(struct service_processor *sp, struct event_reader *reader)
{
	struct event_buffer *buffer = sp->event_buffer;
	struct ibmasm_event *event;
	unsigned int index;
	unsigned long flags;

	reader->cancelled = 0;

	if (wait_event_interruptible(reader->wait,
			event_available(buffer, reader) || reader->cancelled))
		return -ERESTARTSYS;

	if (!event_available(buffer, reader))
		return 0;

	spin_lock_irqsave(&sp->lock, flags);

	index = buffer->next_index;
	event = &buffer->events[index];
	while (event->serial_number < reader->next_serial_number) {
		index = (index + 1) % IBMASM_NUM_EVENTS;
		event = &buffer->events[index];
	}
	memcpy(reader->data, event->data, event->data_size);
	reader->data_size = event->data_size;
	reader->next_serial_number = event->serial_number + 1;

	spin_unlock_irqrestore(&sp->lock, flags);

	return event->data_size;
}

void ibmasm_cancel_next_event(struct event_reader *reader)
{
        reader->cancelled = 1;
        wake_up_interruptible(&reader->wait);
}

void ibmasm_event_reader_register(struct service_processor *sp, struct event_reader *reader)
{
	unsigned long flags;

	reader->next_serial_number = sp->event_buffer->next_serial_number;
	init_waitqueue_head(&reader->wait);
	spin_lock_irqsave(&sp->lock, flags);
	list_add(&reader->node, &sp->event_buffer->readers);
	spin_unlock_irqrestore(&sp->lock, flags);
}

void ibmasm_event_reader_unregister(struct service_processor *sp, struct event_reader *reader)
{
	unsigned long flags;

	spin_lock_irqsave(&sp->lock, flags);
	list_del(&reader->node);
	spin_unlock_irqrestore(&sp->lock, flags);
}

int ibmasm_event_buffer_init(struct service_processor *sp)
{
	struct event_buffer *buffer;
	struct ibmasm_event *event;
	int i;

	buffer = kmalloc(sizeof(struct event_buffer), GFP_KERNEL);
	if (!buffer)
		return 1;

	buffer->next_index = 0;
	buffer->next_serial_number = 1;

	event = buffer->events;
	for (i=0; i<IBMASM_NUM_EVENTS; i++, event++)
		event->serial_number = 0;

	INIT_LIST_HEAD(&buffer->readers);

	sp->event_buffer = buffer;

	return 0;
}

void ibmasm_event_buffer_exit(struct service_processor *sp)
{
	kfree(sp->event_buffer);
}
