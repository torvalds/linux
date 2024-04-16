// SPDX-License-Identifier: GPL-2.0
/*
 * SH Pin Function Control Initialization
 *
 * Copyright (C) 2012  Renesas Solutions Corp.
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
