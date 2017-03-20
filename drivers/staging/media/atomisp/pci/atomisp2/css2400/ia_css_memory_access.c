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

#include "ia_css_memory_access.h"
#include <type_support.h>
#include <system_types.h>
#include <assert_support.h>
#include <memory_access.h>
#include <ia_css_env.h>

const hrt_vaddress mmgr_NULL = (hrt_vaddress)0;
const hrt_vaddress mmgr_EXCEPTION = (hrt_vaddress)-1;

static struct ia_css_css_mem_env my_env;

void
ia_css_memory_access_init(const struct ia_css_css_mem_env *env)
{
	assert(env != NULL);

	my_env = *env;
}

hrt_vaddress
mmgr_malloc(const size_t size)
{
	return mmgr_alloc_attr(size, 0);
}

hrt_vaddress mmgr_alloc_attr(const size_t size, const uint16_t attrs)
{
	uint32_t my_attrs = 0;
	uint16_t masked_attrs = attrs & MMGR_ATTRIBUTE_MASK;
	hrt_vaddress ptr;

	if (masked_attrs & MMGR_ATTRIBUTE_CACHED)
		my_attrs |= IA_CSS_MEM_ATTR_CACHED;
	if (masked_attrs & MMGR_ATTRIBUTE_CLEARED)
		my_attrs |= IA_CSS_MEM_ATTR_ZEROED;
	if (masked_attrs & MMGR_ATTRIBUTE_CONTIGUOUS)
		my_attrs |= IA_CSS_MEM_ATTR_CONTIGUOUS;
	if (masked_attrs & MMGR_ATTRIBUTE_PAGEALIGN)
		my_attrs |= IA_CSS_MEM_ATTR_PAGEALIGN;

	ptr = my_env.alloc(size, my_attrs);
	return ptr;
}

hrt_vaddress
mmgr_calloc(const size_t N, const size_t size)
{
	return mmgr_alloc_attr(size * N, MMGR_ATTRIBUTE_CLEARED);
}

void
mmgr_free(hrt_vaddress vaddr)
{
	my_env.free(vaddr);
}

void
mmgr_clear(hrt_vaddress vaddr, const size_t size)
{
	my_env.set(vaddr, 0, size);
}

void
mmgr_load(const hrt_vaddress vaddr, void *data, const size_t size)
{
	my_env.load(vaddr, data, size);
}

void
mmgr_store(const hrt_vaddress vaddr, const void *data, const size_t size)
{
	my_env.store(vaddr, data, size);
}

hrt_vaddress
mmgr_mmap(const void *ptr, const size_t size,
	  uint16_t attribute, void *context)
{
	return my_env.mmap(ptr, size, attribute, context);
}
