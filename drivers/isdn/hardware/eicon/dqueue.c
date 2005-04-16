/* $Id: dqueue.c,v 1.5 2003/04/12 21:40:49 schindler Exp $
 *
 * Driver for Eicon DIVA Server ISDN cards.
 * User Mode IDI Interface
 *
 * Copyright 2000-2003 by Armin Schindler (mac@melware.de)
 * Copyright 2000-2003 Cytronics & Melware (info@melware.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

#include "platform.h"
#include "dqueue.h"

int
diva_data_q_init(diva_um_idi_data_queue_t * q,
		 int max_length, int max_segments)
{
	int i;

	q->max_length = max_length;
	q->segments = max_segments;

	for (i = 0; i < q->segments; i++) {
		q->data[i] = NULL;
		q->length[i] = 0;
	}
	q->read = q->write = q->count = q->segment_pending = 0;

	for (i = 0; i < q->segments; i++) {
		if (!(q->data[i] = diva_os_malloc(0, q->max_length))) {
			diva_data_q_finit(q);
			return (-1);
		}
	}

	return (0);
}

int diva_data_q_finit(diva_um_idi_data_queue_t * q)
{
	int i;

	for (i = 0; i < q->segments; i++) {
		if (q->data[i]) {
			diva_os_free(0, q->data[i]);
		}
		q->data[i] = NULL;
		q->length[i] = 0;
	}
	q->read = q->write = q->count = q->segment_pending = 0;

	return (0);
}

int diva_data_q_get_max_length(const diva_um_idi_data_queue_t * q)
{
	return (q->max_length);
}

void *diva_data_q_get_segment4write(diva_um_idi_data_queue_t * q)
{
	if ((!q->segment_pending) && (q->count < q->segments)) {
		q->segment_pending = 1;
		return (q->data[q->write]);
	}

	return NULL;
}

void
diva_data_q_ack_segment4write(diva_um_idi_data_queue_t * q, int length)
{
	if (q->segment_pending) {
		q->length[q->write] = length;
		q->count++;
		q->write++;
		if (q->write >= q->segments) {
			q->write = 0;
		}
		q->segment_pending = 0;
	}
}

const void *diva_data_q_get_segment4read(const diva_um_idi_data_queue_t *
					 q)
{
	if (q->count) {
		return (q->data[q->read]);
	}
	return NULL;
}

int diva_data_q_get_segment_length(const diva_um_idi_data_queue_t * q)
{
	return (q->length[q->read]);
}

void diva_data_q_ack_segment4read(diva_um_idi_data_queue_t * q)
{
	if (q->count) {
		q->length[q->read] = 0;
		q->count--;
		q->read++;
		if (q->read >= q->segments) {
			q->read = 0;
		}
	}
}
