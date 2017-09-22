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

#include "ia_css_device_access.h"
#include <type_support.h>   /* for uint*, size_t */
#include <system_types.h>   /* for hrt_address */
#include <ia_css_env.h>     /* for ia_css_hw_access_env */
#include <assert_support.h> /* for assert */

static struct ia_css_hw_access_env my_env;

void
ia_css_device_access_init(const struct ia_css_hw_access_env *env)
{
	assert(env != NULL);

	my_env = *env;
}

uint8_t
ia_css_device_load_uint8(const hrt_address addr)
{
	return my_env.load_8(addr);
}

uint16_t
ia_css_device_load_uint16(const hrt_address addr)
{
	return my_env.load_16(addr);
}

uint32_t
ia_css_device_load_uint32(const hrt_address addr)
{
	return my_env.load_32(addr);
}

uint64_t
ia_css_device_load_uint64(const hrt_address addr)
{
	assert(0);

	(void)addr;
	return 0;
}

void
ia_css_device_store_uint8(const hrt_address addr, const uint8_t data)
{
	my_env.store_8(addr, data);
}

void
ia_css_device_store_uint16(const hrt_address addr, const uint16_t data)
{
	my_env.store_16(addr, data);
}

void
ia_css_device_store_uint32(const hrt_address addr, const uint32_t data)
{
	my_env.store_32(addr, data);
}

void
ia_css_device_store_uint64(const hrt_address addr, const uint64_t data)
{
	assert(0);

	(void)addr;
	(void)data;
}

void
ia_css_device_load(const hrt_address addr, void *data, const size_t size)
{
	my_env.load(addr, data, (uint32_t)size);
}

void
ia_css_device_store(const hrt_address addr, const void *data, const size_t size)
{
	my_env.store(addr, data, (uint32_t)size);
}
