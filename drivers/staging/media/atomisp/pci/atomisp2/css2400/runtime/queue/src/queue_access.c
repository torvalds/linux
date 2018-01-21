#ifndef ISP2401
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
#else
/*
Support for Intel Camera Imaging ISP subsystem.
Copyright (c) 2010 - 2015, Intel Corporation.

This program is free software; you can redistribute it and/or modify it
under the terms and conditions of the GNU General Public License,
version 2, as published by the Free Software Foundation.

This program is distributed in the hope it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
more details.
*/
#endif

#include "type_support.h"
#include "queue_access.h"
#include "ia_css_circbuf.h"
#include "sp.h"
#include "memory_access.h"
#include "assert_support.h"

int ia_css_queue_load(
		struct ia_css_queue *rdesc,
		ia_css_circbuf_desc_t *cb_desc,
		uint32_t ignore_desc_flags)
{
	if (rdesc == NULL || cb_desc == NULL)
		return EINVAL;

	if (rdesc->location == IA_CSS_QUEUE_LOC_SP) {
		assert(ignore_desc_flags <= QUEUE_IGNORE_DESC_FLAGS_MAX);

		if (0 == (ignore_desc_flags & QUEUE_IGNORE_SIZE_FLAG)) {
			cb_desc->size = sp_dmem_load_uint8(rdesc->proc_id,
				rdesc->desc.remote.cb_desc_addr
				+ offsetof(ia_css_circbuf_desc_t, size));

			if (0 == cb_desc->size) {
				/* Adding back the workaround which was removed
				   while refactoring queues. When reading size
				   through sp_dmem_load_*, sometimes we get back
				   the value as zero. This causes division by 0
				   exception as the size is used in a modular
				   division operation. */
				return EDOM;
			}
		}

		if (0 == (ignore_desc_flags & QUEUE_IGNORE_START_FLAG))
			cb_desc->start = sp_dmem_load_uint8(rdesc->proc_id,
				rdesc->desc.remote.cb_desc_addr
				+ offsetof(ia_css_circbuf_desc_t, start));

		if (0 == (ignore_desc_flags & QUEUE_IGNORE_END_FLAG))
			cb_desc->end = sp_dmem_load_uint8(rdesc->proc_id,
				rdesc->desc.remote.cb_desc_addr
				+ offsetof(ia_css_circbuf_desc_t, end));

		if (0 == (ignore_desc_flags & QUEUE_IGNORE_STEP_FLAG))
			cb_desc->step = sp_dmem_load_uint8(rdesc->proc_id,
				rdesc->desc.remote.cb_desc_addr
				+ offsetof(ia_css_circbuf_desc_t, step));

	} else if (rdesc->location == IA_CSS_QUEUE_LOC_HOST) {
		/* doing DMA transfer of entire structure */
		mmgr_load(rdesc->desc.remote.cb_desc_addr,
			(void *)cb_desc,
			sizeof(ia_css_circbuf_desc_t));
	} else if (rdesc->location == IA_CSS_QUEUE_LOC_ISP) {
		/* Not supported yet */
		return ENOTSUP;
	}

	return 0;
}

int ia_css_queue_store(
		struct ia_css_queue *rdesc,
		ia_css_circbuf_desc_t *cb_desc,
		uint32_t ignore_desc_flags)
{
	if (rdesc == NULL || cb_desc == NULL)
		return EINVAL;

	if (rdesc->location == IA_CSS_QUEUE_LOC_SP) {
		assert(ignore_desc_flags <= QUEUE_IGNORE_DESC_FLAGS_MAX);

		if (0 == (ignore_desc_flags & QUEUE_IGNORE_SIZE_FLAG))
			sp_dmem_store_uint8(rdesc->proc_id,
				rdesc->desc.remote.cb_desc_addr
				+ offsetof(ia_css_circbuf_desc_t, size),
				cb_desc->size);

		if (0 == (ignore_desc_flags & QUEUE_IGNORE_START_FLAG))
			sp_dmem_store_uint8(rdesc->proc_id,
				rdesc->desc.remote.cb_desc_addr
				+ offsetof(ia_css_circbuf_desc_t, start),
				cb_desc->start);

		if (0 == (ignore_desc_flags & QUEUE_IGNORE_END_FLAG))
			sp_dmem_store_uint8(rdesc->proc_id,
				rdesc->desc.remote.cb_desc_addr
				+ offsetof(ia_css_circbuf_desc_t, end),
				cb_desc->end);

		if (0 == (ignore_desc_flags & QUEUE_IGNORE_STEP_FLAG))
			sp_dmem_store_uint8(rdesc->proc_id,
				rdesc->desc.remote.cb_desc_addr
				+ offsetof(ia_css_circbuf_desc_t, step),
				cb_desc->step);
	} else if (rdesc->location == IA_CSS_QUEUE_LOC_HOST) {
		/* doing DMA transfer of entire structure */
		mmgr_store(rdesc->desc.remote.cb_desc_addr,
			(void *)cb_desc,
			sizeof(ia_css_circbuf_desc_t));
	} else if (rdesc->location == IA_CSS_QUEUE_LOC_ISP) {
		/* Not supported yet */
		return ENOTSUP;
	}

	return 0;
}

int ia_css_queue_item_load(
		struct ia_css_queue *rdesc,
		uint8_t position,
		ia_css_circbuf_elem_t *item)
{
	if (rdesc == NULL || item == NULL)
		return EINVAL;

	if (rdesc->location == IA_CSS_QUEUE_LOC_SP) {
		sp_dmem_load(rdesc->proc_id,
			rdesc->desc.remote.cb_elems_addr
			+ position * sizeof(ia_css_circbuf_elem_t),
			item,
			sizeof(ia_css_circbuf_elem_t));
	} else if (rdesc->location == IA_CSS_QUEUE_LOC_HOST) {
		mmgr_load(rdesc->desc.remote.cb_elems_addr
			+ position * sizeof(ia_css_circbuf_elem_t),
			(void *)item,
			sizeof(ia_css_circbuf_elem_t));
	} else if (rdesc->location == IA_CSS_QUEUE_LOC_ISP) {
		/* Not supported yet */
		return ENOTSUP;
	}

	return 0;
}

int ia_css_queue_item_store(
		struct ia_css_queue *rdesc,
		uint8_t position,
		ia_css_circbuf_elem_t *item)
{
	if (rdesc == NULL || item == NULL)
		return EINVAL;

	if (rdesc->location == IA_CSS_QUEUE_LOC_SP) {
		sp_dmem_store(rdesc->proc_id,
			rdesc->desc.remote.cb_elems_addr
			+ position * sizeof(ia_css_circbuf_elem_t),
			item,
			sizeof(ia_css_circbuf_elem_t));
	} else if (rdesc->location == IA_CSS_QUEUE_LOC_HOST) {
		mmgr_store(rdesc->desc.remote.cb_elems_addr
			+ position * sizeof(ia_css_circbuf_elem_t),
			(void *)item,
			sizeof(ia_css_circbuf_elem_t));
	} else if (rdesc->location == IA_CSS_QUEUE_LOC_ISP) {
		/* Not supported yet */
		return ENOTSUP;
	}

	return 0;
}
