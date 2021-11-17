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

#include <linux/slab.h>
#include <ia_css_host_data.h>
#include <sh_css_internal.h>

struct ia_css_host_data *ia_css_host_data_allocate(size_t size)
{
	struct ia_css_host_data *me;

	me =  kmalloc(sizeof(struct ia_css_host_data), GFP_KERNEL);
	if (!me)
		return NULL;
	me->size = (uint32_t)size;
	me->address = kvmalloc(size, GFP_KERNEL);
	if (!me->address) {
		kfree(me);
		return NULL;
	}
	return me;
}

void ia_css_host_data_free(struct ia_css_host_data *me)
{
	if (me) {
		kvfree(me->address);
		me->address = NULL;
		kfree(me);
	}
}
