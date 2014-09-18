/*
 * Copyright 2012 IBM Corporation
 *
 * Author: Ashley Lai <adlai@us.ibm.com>
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

int read_log(struct tpm_bios_log *log)
{
	struct device_node *np;
	const u32 *sizep;
	const __be64 *basep;

	if (log->bios_event_log != NULL) {
		pr_err("%s: ERROR - Eventlog already initialized\n", __func__);
		return -EFAULT;
	}

	np = of_find_node_by_name(NULL, "ibm,vtpm");
	if (!np) {
		pr_err("%s: ERROR - IBMVTPM not supported\n", __func__);
		return -ENODEV;
	}

	sizep = of_get_property(np, "linux,sml-size", NULL);
	if (sizep == NULL) {
		pr_err("%s: ERROR - SML size not found\n", __func__);
		goto cleanup_eio;
	}
	if (*sizep == 0) {
		pr_err("%s: ERROR - event log area empty\n", __func__);
		goto cleanup_eio;
	}

	basep = of_get_property(np, "linux,sml-base", NULL);
	if (basep == NULL) {
		pr_err(KERN_ERR "%s: ERROR - SML not found\n", __func__);
		goto cleanup_eio;
	}

	of_node_put(np);
	log->bios_event_log = kmalloc(*sizep, GFP_KERNEL);
	if (!log->bios_event_log) {
		pr_err("%s: ERROR - Not enough memory for BIOS measurements\n",
		       __func__);
		return -ENOMEM;
	}

	log->bios_event_log_end = log->bios_event_log + *sizep;

	memcpy(log->bios_event_log, __va(be64_to_cpup(basep)), *sizep);

	return 0;

cleanup_eio:
	of_node_put(np);
	return -EIO;
}
