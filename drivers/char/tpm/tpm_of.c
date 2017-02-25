/*
 * Copyright 2012 IBM Corporation
 *
 * Author: Ashley Lai <ashleydlai@gmail.com>
 *         Nayna Jain <nayna@linux.vnet.ibm.com>
 *
 * Maintained by: <tpmdd-devel@lists.sourceforge.net>
 *
 * Read the event log created by the firmware on PPC64
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */

#include <linux/slab.h>
#include <linux/of.h>

#include "tpm.h"
#include "tpm_eventlog.h"

int tpm_read_log_of(struct tpm_chip *chip)
{
	struct device_node *np;
	const u32 *sizep;
	const u64 *basep;
	struct tpm_bios_log *log;

	log = &chip->log;
	if (chip->dev.parent && chip->dev.parent->of_node)
		np = chip->dev.parent->of_node;
	else
		return -ENODEV;

	sizep = of_get_property(np, "linux,sml-size", NULL);
	basep = of_get_property(np, "linux,sml-base", NULL);
	if (sizep == NULL && basep == NULL)
		return -ENODEV;
	if (sizep == NULL || basep == NULL)
		return -EIO;

	if (*sizep == 0) {
		dev_warn(&chip->dev, "%s: Event log area empty\n", __func__);
		return -EIO;
	}

	log->bios_event_log = kmalloc(*sizep, GFP_KERNEL);
	if (!log->bios_event_log)
		return -ENOMEM;

	log->bios_event_log_end = log->bios_event_log + *sizep;

	memcpy(log->bios_event_log, __va(*basep), *sizep);

	return 0;
}
