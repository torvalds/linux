
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

/* Remote mouse and keyboard event handling functions */

#include "ibmasm.h"
#include "remote.h"

int ibmasm_init_remote_queue(struct service_processor *sp)
{
	struct remote_queue *q = &sp->remote_queue;

	disable_mouse_interrupts(sp);

	q->open = 0;
	q->size = 0;

	q->start = kmalloc(DRIVER_REMOTE_QUEUE_SIZE * sizeof(struct remote_event), GFP_KERNEL);
        if (q->start == 0)
                return -ENOMEM;

	q->end = q->start + DRIVER_REMOTE_QUEUE_SIZE;
	q->reader = q->start;
	q->writer = q->start;
	q->size = DRIVER_REMOTE_QUEUE_SIZE;
	init_waitqueue_head(&q->wait);

	return 0;
}

void ibmasm_free_remote_queue(struct service_processor *sp)
{
	kfree(sp->remote_queue.start);
}

void ibmasm_advance_reader(struct remote_queue *q, unsigned int n)
{
	q->reader += n;
	if (q->reader >= q->end)
		q->reader -= q->size;
}

size_t ibmasm_events_available(struct remote_queue *q)
{
	ssize_t diff = q->writer - q->reader;
 
	return (diff >= 0) ? diff : q->end - q->reader;	
}
	

static int space_free(struct remote_queue *q)
{
	if (q->reader == q->writer)
		return q->size - 1;

	return ( (q->reader + q->size - q->writer) % q->size ) - 1;
}

static void set_mouse_event(struct remote_input *input, struct mouse_event *mouse)
{
	static char last_buttons = 0;

	mouse->x = input->data.mouse.x;
	mouse->y = input->data.mouse.y;

	if (input->mouse_buttons == REMOTE_MOUSE_DOUBLE_CLICK) {
		mouse->buttons = REMOTE_MOUSE_DOUBLE_CLICK;
		last_buttons = 0;
		return;
	}
	mouse->transitions = last_buttons ^ input->mouse_buttons;
	mouse->buttons = input->mouse_buttons;

	last_buttons = input->mouse_buttons;
}

static void set_keyboard_event(struct remote_input *input, struct keyboard_event *keyboard)
{
	keyboard->key_code = input->data.keyboard.key_code;
	keyboard->key_down = input->data.keyboard.key_down;
}

static int add_to_driver_queue(struct remote_queue *q, struct remote_input *input)
{
	struct remote_event *event = q->writer;

	if (space_free(q) < 1) {
		return 1;
	}

	switch(input->type) {
	case (INPUT_TYPE_MOUSE):
		event->type = INPUT_TYPE_MOUSE;
		set_mouse_event(input, &event->data.mouse);
		break;
	case (INPUT_TYPE_KEYBOARD):
		event->type = INPUT_TYPE_KEYBOARD;
		set_keyboard_event(input, &event->data.keyboard);
		break;
	default:
		return 0;
	}
	event->type = input->type;

	q->writer++;
	if (q->writer == q->end)
		q->writer = q->start;

	return 0;
}
	

void ibmasm_handle_mouse_interrupt(struct service_processor *sp)
{
	unsigned long reader;
	unsigned long writer;
	struct remote_input input;

	reader = get_queue_reader(sp);
	writer = get_queue_writer(sp);

	while (reader != writer) {
		memcpy(&input, (void *)get_queue_entry(sp, reader), sizeof(struct remote_input));

		if (add_to_driver_queue(&sp->remote_queue, &input))
			break;

		reader = advance_queue_reader(sp, reader);
	}
	wake_up_interruptible(&sp->remote_queue.wait);
}
