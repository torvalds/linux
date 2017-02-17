/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include "ia_css_types.h"
#include "assert_support.h"
#include "ia_css_queue.h" /* sp2host_dequeue_irq_event() */
#include "ia_css_eventq.h"
#include "ia_css_event.h"	/* ia_css_event_encode()
				ia_css_event_decode()
				*/
#include "platform_support.h" /* hrt_sleep() */

int ia_css_eventq_recv(
		ia_css_queue_t *eventq_handle,
		uint8_t *payload)
{
	uint32_t sp_event;
	int error;

	/* dequeue the IRQ event */
	error = ia_css_queue_dequeue(eventq_handle, &sp_event);

	/* check whether the IRQ event is available or not */
	if (!error)
		ia_css_event_decode(sp_event, payload);
	return error;
}

/**
 * @brief The Host sends the event to the SP.
 * Refer to "sh_css_sp.h" for details.
 */
int ia_css_eventq_send(
			ia_css_queue_t *eventq_handle,
			uint8_t evt_id,
			uint8_t evt_payload_0,
			uint8_t evt_payload_1,
			uint8_t evt_payload_2)
{
	uint8_t tmp[4];
	uint32_t sw_event;
	int error = ENOSYS;

	/*
	 * Encode the queue type, the thread ID and
	 * the queue ID into the event.
	 */
	tmp[0] = evt_id;
	tmp[1] = evt_payload_0;
	tmp[2] = evt_payload_1;
	tmp[3] = evt_payload_2;
	ia_css_event_encode(tmp, 4, &sw_event);

	/* queue the software event (busy-waiting) */
	for ( ; ; ) {
		error = ia_css_queue_enqueue(eventq_handle, sw_event);
		if (ENOBUFS != error) {
			/* We were able to successfully send the event
			   or had a real failure. return the status*/
			break;
		}
		/* Wait for the queue to be not full and try again*/
		hrt_sleep();
	}
	return error;
}
