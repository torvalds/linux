/*
 * SH Pin Function Control Initialization
 *
 * Copyright (C) 2012  Renesas Solutions Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/init.h>
#include <linux/platform_device.h>

#include <cpu/pfc.h>

static struct platform_device sh_pfc_device = {
	.id		= -1,
};

int __init sh_pfc_register(const char *name,
			   struct resource *resource, u32 num_resources)
{
	sh_pfc_device.name = name;
	sh_pfc_device.num_resources = num_resources;
	sh_pfc_device.resource = resource;

	return platform_device_register(&sh_pfc_device);
}
