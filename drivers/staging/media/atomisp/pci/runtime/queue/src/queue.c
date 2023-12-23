// SPDX-License-Identifier: GPL-2.0
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

#include "ia_css_queue.h"
#include <math_support.h>
#include <ia_css_circbuf.h>
#include <ia_css_circbuf_desc.h>
#include "queue_access.h"

/*****************************************************************************
 * Queue Public APIs
 *****************************************************************************/
int ia_css_queue_local_init(ia_css_queue_t *qhandle, ia_css_queue_local_t *desc)
{
	if (NULL == qhandle || NULL == desc
	    || NULL == desc->cb_elems || NULL == desc->cb_desc) {
		/* Invalid parameters, return error*/
		return -EINVAL;
	}

	/* Mark the queue as Local */
	qhandle->type = IA_CSS_QUEUE_TYPE_LOCAL;

	/* Create a local circular buffer queue*/
	ia_css_circbuf_create(&qhandle->desc.cb_local,
			      desc->cb_elems,
			      desc->cb_desc);

	return 0;
}

int ia_css_queue_remote_init(ia_css_queue_t *qhandle, ia_css_queue_remote_t *desc)
{
	if (NULL == qhandle || NULL == desc) {
		/* Invalid parameters, return error*/
		return -EINVAL;
	}

	/* Mark the queue as remote*/
	qhandle->type = IA_CSS_QUEUE_TYPE_REMOTE;

	/* Copy over the local queue descriptor*/
	qhandle->location = desc->location;
	qhandle->proc_id = desc->proc_id;
	qhandle->desc.remote.cb_desc_addr = desc->cb_desc_addr;
	qhandle->desc.remote.cb_elems_addr = desc->cb_elems_addr;

	/* If queue is remote, we let the local processor
	 * do its init, before using it. This is just to get us
	 * started, we can remove this restriction as we go ahead
	 */

	return 0;
}

int ia_css_queue_uninit(ia_css_queue_t *qhandle)
{
	if (!qhandle)
		return -EINVAL;

	/* Load the required queue object */
	if (qhandle->type == IA_CSS_QUEUE_TYPE_LOCAL) {
		/* Local queues are created. Destroy it*/
		ia_css_circbuf_destroy(&qhandle->desc.cb_local);
	}

	return 0;
}

int ia_css_queue_enqueue(ia_css_queue_t *qhandle, uint32_t item)
{
	int error;

	if (!qhandle)
		return -EINVAL;

	/* 1. Load the required queue object */
	if (qhandle->type == IA_CSS_QUEUE_TYPE_LOCAL) {
		/* Directly de-ref the object and
		 * operate on the queue
		 */
		if (ia_css_circbuf_is_full(&qhandle->desc.cb_local)) {
			/* Cannot push the element. Return*/
			return -ENOBUFS;
		}

		/* Push the element*/
		ia_css_circbuf_push(&qhandle->desc.cb_local, item);
	} else if (qhandle->type == IA_CSS_QUEUE_TYPE_REMOTE) {
		ia_css_circbuf_desc_t cb_desc;
		ia_css_circbuf_elem_t cb_elem;
		u32 ignore_desc_flags = QUEUE_IGNORE_STEP_FLAG;

		/* a. Load the queue cb_desc from remote */
		QUEUE_CB_DESC_INIT(&cb_desc);
		error = ia_css_queue_load(qhandle, &cb_desc, ignore_desc_flags);
		if (error != 0)
			return error;

		/* b. Operate on the queue */
		if (ia_css_circbuf_desc_is_full(&cb_desc))
			return -ENOBUFS;

		cb_elem.val = item;

		error = ia_css_queue_item_store(qhandle, cb_desc.end, &cb_elem);
		if (error != 0)
			return error;

		cb_desc.end = (cb_desc.end + 1) % cb_desc.size;

		/* c. Store the queue object */
		/* Set only fields requiring update with
		 * valid value. Avoids unnecessary calls
		 * to load/store functions
		 */
		ignore_desc_flags = QUEUE_IGNORE_SIZE_START_STEP_FLAGS;

		error = ia_css_queue_store(qhandle, &cb_desc, ignore_desc_flags);
		if (error != 0)
			return error;
	}

	return 0;
}

int ia_css_queue_dequeue(ia_css_queue_t *qhandle, uint32_t *item)
{
	int error;

	if (!qhandle || NULL == item)
		return -EINVAL;

	/* 1. Load the required queue object */
	if (qhandle->type == IA_CSS_QUEUE_TYPE_LOCAL) {
		/* Directly de-ref the object and
		 * operate on the queue
		 */
		if (ia_css_circbuf_is_empty(&qhandle->desc.cb_local)) {
			/* Nothing to pop. Return empty queue*/
			return -ENODATA;
		}

		*item = ia_css_circbuf_pop(&qhandle->desc.cb_local);
	} else if (qhandle->type == IA_CSS_QUEUE_TYPE_REMOTE) {
		/* a. Load the queue from remote */
		ia_css_circbuf_desc_t cb_desc;
		ia_css_circbuf_elem_t cb_elem;
		u32 ignore_desc_flags = QUEUE_IGNORE_STEP_FLAG;

		QUEUE_CB_DESC_INIT(&cb_desc);

		error = ia_css_queue_load(qhandle, &cb_desc, ignore_desc_flags);
		if (error != 0)
			return error;

		/* b. Operate on the queue */
		if (ia_css_circbuf_desc_is_empty(&cb_desc))
			return -ENODATA;

		error = ia_css_queue_item_load(qhandle, cb_desc.start, &cb_elem);
		if (error != 0)
			return error;

		*item = cb_elem.val;

		cb_desc.start = OP_std_modadd(cb_desc.start, 1, cb_desc.size);

		/* c. Store the queue object */
		/* Set only fields requiring update with
		 * valid value. Avoids unnecessary calls
		 * to load/store functions
		 */
		ignore_desc_flags = QUEUE_IGNORE_SIZE_END_STEP_FLAGS;
		error = ia_css_queue_store(qhandle, &cb_desc, ignore_desc_flags);
		if (error != 0)
			return error;
	}
	return 0;
}

int ia_css_queue_is_full(ia_css_queue_t *qhandle, bool *is_full)
{
	int error;

	if ((!qhandle) || (!is_full))
		return -EINVAL;

	/* 1. Load the required queue object */
	if (qhandle->type == IA_CSS_QUEUE_TYPE_LOCAL) {
		/* Directly de-ref the object and
		 * operate on the queue
		 */
		*is_full = ia_css_circbuf_is_full(&qhandle->desc.cb_local);
		return 0;
	} else if (qhandle->type == IA_CSS_QUEUE_TYPE_REMOTE) {
		/* a. Load the queue from remote */
		ia_css_circbuf_desc_t cb_desc;
		u32 ignore_desc_flags = QUEUE_IGNORE_STEP_FLAG;

		QUEUE_CB_DESC_INIT(&cb_desc);
		error = ia_css_queue_load(qhandle, &cb_desc, ignore_desc_flags);
		if (error != 0)
			return error;

		/* b. Operate on the queue */
		*is_full = ia_css_circbuf_desc_is_full(&cb_desc);
		return 0;
	}

	return -EINVAL;
}

int ia_css_queue_get_free_space(ia_css_queue_t *qhandle, uint32_t *size)
{
	int error;

	if ((!qhandle) || (!size))
		return -EINVAL;

	/* 1. Load the required queue object */
	if (qhandle->type == IA_CSS_QUEUE_TYPE_LOCAL) {
		/* Directly de-ref the object and
		 * operate on the queue
		 */
		*size = ia_css_circbuf_get_free_elems(&qhandle->desc.cb_local);
		return 0;
	} else if (qhandle->type == IA_CSS_QUEUE_TYPE_REMOTE) {
		/* a. Load the queue from remote */
		ia_css_circbuf_desc_t cb_desc;
		u32 ignore_desc_flags = QUEUE_IGNORE_STEP_FLAG;

		QUEUE_CB_DESC_INIT(&cb_desc);
		error = ia_css_queue_load(qhandle, &cb_desc, ignore_desc_flags);
		if (error != 0)
			return error;

		/* b. Operate on the queue */
		*size = ia_css_circbuf_desc_get_free_elems(&cb_desc);
		return 0;
	}

	return -EINVAL;
}

int ia_css_queue_get_used_space(ia_css_queue_t *qhandle, uint32_t *size)
{
	int error;

	if ((!qhandle) || (!size))
		return -EINVAL;

	/* 1. Load the required queue object */
	if (qhandle->type == IA_CSS_QUEUE_TYPE_LOCAL) {
		/* Directly de-ref the object and
		 * operate on the queue
		 */
		*size = ia_css_circbuf_get_num_elems(&qhandle->desc.cb_local);
		return 0;
	} else if (qhandle->type == IA_CSS_QUEUE_TYPE_REMOTE) {
		/* a. Load the queue from remote */
		ia_css_circbuf_desc_t cb_desc;
		u32 ignore_desc_flags = QUEUE_IGNORE_STEP_FLAG;

		QUEUE_CB_DESC_INIT(&cb_desc);
		error = ia_css_queue_load(qhandle, &cb_desc, ignore_desc_flags);
		if (error != 0)
			return error;

		/* b. Operate on the queue */
		*size = ia_css_circbuf_desc_get_num_elems(&cb_desc);
		return 0;
	}

	return -EINVAL;
}

int ia_css_queue_peek(ia_css_queue_t *qhandle, u32 offset, uint32_t *element)
{
	u32 num_elems;
	int error;

	if ((!qhandle) || (!element))
		return -EINVAL;

	/* 1. Load the required queue object */
	if (qhandle->type == IA_CSS_QUEUE_TYPE_LOCAL) {
		/* Directly de-ref the object and
		 * operate on the queue
		 */
		/* Check if offset is valid */
		num_elems = ia_css_circbuf_get_num_elems(&qhandle->desc.cb_local);
		if (offset > num_elems)
			return -EINVAL;

		*element = ia_css_circbuf_peek_from_start(&qhandle->desc.cb_local, (int)offset);
		return 0;
	} else if (qhandle->type == IA_CSS_QUEUE_TYPE_REMOTE) {
		/* a. Load the queue from remote */
		ia_css_circbuf_desc_t cb_desc;
		ia_css_circbuf_elem_t cb_elem;
		u32 ignore_desc_flags = QUEUE_IGNORE_STEP_FLAG;

		QUEUE_CB_DESC_INIT(&cb_desc);

		error =  ia_css_queue_load(qhandle, &cb_desc, ignore_desc_flags);
		if (error != 0)
			return error;

		/* Check if offset is valid */
		num_elems = ia_css_circbuf_desc_get_num_elems(&cb_desc);
		if (offset > num_elems)
			return -EINVAL;

		offset = OP_std_modadd(cb_desc.start, offset, cb_desc.size);
		error = ia_css_queue_item_load(qhandle, (uint8_t)offset, &cb_elem);
		if (error != 0)
			return error;

		*element = cb_elem.val;
		return 0;
	}

	return -EINVAL;
}

int ia_css_queue_is_empty(ia_css_queue_t *qhandle, bool *is_empty)
{
	int error;

	if ((!qhandle) || (!is_empty))
		return -EINVAL;

	/* 1. Load the required queue object */
	if (qhandle->type == IA_CSS_QUEUE_TYPE_LOCAL) {
		/* Directly de-ref the object and
		 * operate on the queue
		 */
		*is_empty = ia_css_circbuf_is_empty(&qhandle->desc.cb_local);
		return 0;
	} else if (qhandle->type == IA_CSS_QUEUE_TYPE_REMOTE) {
		/* a. Load the queue from remote */
		ia_css_circbuf_desc_t cb_desc;
		u32 ignore_desc_flags = QUEUE_IGNORE_STEP_FLAG;

		QUEUE_CB_DESC_INIT(&cb_desc);
		error = ia_css_queue_load(qhandle, &cb_desc, ignore_desc_flags);
		if (error != 0)
			return error;

		/* b. Operate on the queue */
		*is_empty = ia_css_circbuf_desc_is_empty(&cb_desc);
		return 0;
	}

	return -EINVAL;
}

int ia_css_queue_get_size(ia_css_queue_t *qhandle, uint32_t *size)
{
	int error;

	if ((!qhandle) || (!size))
		return -EINVAL;

	/* 1. Load the required queue object */
	if (qhandle->type == IA_CSS_QUEUE_TYPE_LOCAL) {
		/* Directly de-ref the object and
		 * operate on the queue
		 */
		/* Return maximum usable capacity */
		*size = ia_css_circbuf_get_size(&qhandle->desc.cb_local);
	} else if (qhandle->type == IA_CSS_QUEUE_TYPE_REMOTE) {
		/* a. Load the queue from remote */
		ia_css_circbuf_desc_t cb_desc;
		u32 ignore_desc_flags = QUEUE_IGNORE_START_END_STEP_FLAGS;

		QUEUE_CB_DESC_INIT(&cb_desc);

		error = ia_css_queue_load(qhandle, &cb_desc, ignore_desc_flags);
		if (error != 0)
			return error;

		/* Return maximum usable capacity */
		*size = cb_desc.size;
	}

	return 0;
}
