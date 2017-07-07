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
#include <hrt/hive_isp_css_mm_hrt.h>

const hrt_vaddress mmgr_NULL = (hrt_vaddress)0;
const hrt_vaddress mmgr_EXCEPTION = (hrt_vaddress)-1;

hrt_vaddress
mmgr_malloc(const size_t size)
{
	return mmgr_alloc_attr(size, 0);
}

hrt_vaddress mmgr_alloc_attr(const size_t size, const uint16_t attrs)
{
	uint16_t masked_attrs = attrs & MMGR_ATTRIBUTE_MASK;
	WARN_ON(attrs & MMGR_ATTRIBUTE_CONTIGUOUS);

	if (masked_attrs & MMGR_ATTRIBUTE_CLEARED) {
		if (masked_attrs & MMGR_ATTRIBUTE_CACHED)
			return (ia_css_ptr) hrt_isp_css_mm_calloc_cached(size);
		else
			return (ia_css_ptr) hrt_isp_css_mm_calloc(size);
	} else {
		if (masked_attrs & MMGR_ATTRIBUTE_CACHED)
			return (ia_css_ptr) hrt_isp_css_mm_alloc_cached(size);
		else
			return (ia_css_ptr) hrt_isp_css_mm_alloc(size);
	}
}

hrt_vaddress
mmgr_calloc(const size_t N, const size_t size)
{
	return mmgr_alloc_attr(size * N, MMGR_ATTRIBUTE_CLEARED);
}

void mmgr_clear(hrt_vaddress vaddr, const size_t size)
{
	if (vaddr)
		hmm_set(vaddr, 0, size);
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

hrt_vaddress
mmgr_mmap(const void *ptr, const size_t size,
	  uint16_t attribute, void *context)
{
	struct hrt_userbuffer_attr *userbuffer_attr = context;
	return hrt_isp_css_mm_alloc_user_ptr(
			size, (void *)ptr, userbuffer_attr->pgnr,
			userbuffer_attr->type,
			attribute & HRT_BUF_FLAG_CACHED);
}
