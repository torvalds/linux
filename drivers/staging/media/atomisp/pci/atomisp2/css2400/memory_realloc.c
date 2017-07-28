/**
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
#include "memory_realloc.h"
#include "ia_css_debug.h"
#include "ia_css_refcount.h"
#include "memory_access.h"

static bool realloc_isp_css_mm_buf(
	hrt_vaddress *curr_buf,
	size_t *curr_size,
	size_t needed_size,
	bool force,
	enum ia_css_err *err,
	uint16_t mmgr_attribute);


bool reallocate_buffer(
	hrt_vaddress *curr_buf,
	size_t *curr_size,
	size_t needed_size,
	bool force,
	enum ia_css_err *err)
{
	bool ret;
	uint16_t	mmgr_attribute = MMGR_ATTRIBUTE_DEFAULT;

	IA_CSS_ENTER_PRIVATE("void");

	ret = realloc_isp_css_mm_buf(curr_buf,
		curr_size, needed_size, force, err, mmgr_attribute);

	IA_CSS_LEAVE_PRIVATE("ret=%d", ret);
	return ret;
}

static bool realloc_isp_css_mm_buf(
	hrt_vaddress *curr_buf,
	size_t *curr_size,
	size_t needed_size,
	bool force,
	enum ia_css_err *err,
	uint16_t mmgr_attribute)
{
	int32_t id;

	*err = IA_CSS_SUCCESS;
	/* Possible optimization: add a function sh_css_isp_css_mm_realloc()
	 * and implement on top of hmm. */

	IA_CSS_ENTER_PRIVATE("void");

	if (ia_css_refcount_is_single(*curr_buf) && !force && *curr_size >= needed_size) {
		IA_CSS_LEAVE_PRIVATE("false");
		return false;
	}

	id = IA_CSS_REFCOUNT_PARAM_BUFFER;
	ia_css_refcount_decrement(id, *curr_buf);
	*curr_buf = ia_css_refcount_increment(id, mmgr_alloc_attr(needed_size,
							mmgr_attribute));

	if (!*curr_buf) {
		*err = IA_CSS_ERR_CANNOT_ALLOCATE_MEMORY;
		*curr_size = 0;
	} else {
		*curr_size = needed_size;
	}
	IA_CSS_LEAVE_PRIVATE("true");
	return true;
}
