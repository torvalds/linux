/*
 * container.c  - ACPI Generic Container Driver
 *
 * Copyright (C) 2004 Anil S Keshavamurthy (anil.s.keshavamurthy@intel.com)
 * Copyright (C) 2004 Keiichiro Tokunaga (tokunaga.keiich@jp.fujitsu.com)
 * Copyright (C) 2004 Motoyuki Ito (motoyuki@soft.fujitsu.com)
 * Copyright (C) 2004 FUJITSU LIMITED
 * Copyright (C) 2004, 2013 Intel Corp.
 * Author: Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
#include <linux/acpi.h>

#include "internal.h"

#define PREFIX "ACPI: "

#define _COMPONENT			ACPI_CONTAINER_COMPONENT
ACPI_MODULE_NAME("container");

static const struct acpi_device_id container_device_ids[] = {
	{"ACPI0004", 0},
	{"PNP0A05", 0},
	{"PNP0A06", 0},
	{"", 0},
};

static int container_device_attach(struct acpi_device *device,
				   const struct acpi_device_id *not_used)
{
	/* This is necessary for container hotplug to work. */
	return 1;
}

static struct acpi_scan_handler container_device_handler = {
	.ids = container_device_ids,
	.attach = container_device_attach,
	.hotplug = {
		.enabled = true,
		.mode = AHM_CONTAINER,
	},
};

void __init acpi_container_init(void)
{
	acpi_scan_add_handler(&container_device_handler);
}
