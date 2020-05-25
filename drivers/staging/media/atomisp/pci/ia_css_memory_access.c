/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015-2017, Intel Corporation.
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

#include <type_support.h>
#include <system_types.h>
#include <assert_support.h>
#include <memory_access.h>
#include <ia_css_env.h>

#include "atomisp_internal.h"

hrt_vaddress mmgr_alloc_attr(const size_t size, const uint16_t attrs)
{
	ia_css_ptr data;

	WARN_ON(attrs & MMGR_ATTRIBUTE_CONTIGUOUS);

	data = hmm_alloc(size, HMM_BO_PRIVATE, 0, NULL,
			 attrs & MMGR_ATTRIBUTE_CACHED);

	if (!data)
		return 0;

	if (attrs & MMGR_ATTRIBUTE_CLEARED)
		hmm_set(data, 0, size);

	return (ia_css_ptr)data;
}

void mmgr_load(const hrt_vaddress vaddr, void *data, const size_t size)
{
	if (vaddr && data)
		hmm_load(vaddr, data, size);
}

void
mmgr_store(const hrt_vaddress vaddr, const void *data, const size_t size)
{
	if (vaddr && data)
		hmm_store(vaddr, data, size);
}
