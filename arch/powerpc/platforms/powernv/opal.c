/*
 * PowerNV OPAL high level interfaces
 *
 * Copyright 2011 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#undef DEBUG

#include <linux/types.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <asm/opal.h>
#include <asm/firmware.h>

#include "powernv.h"

struct opal {
	u64 base;
	u64 entry;
} opal;

static struct device_node *opal_node;
static DEFINE_SPINLOCK(opal_write_lock);

int __init early_init_dt_scan_opal(unsigned long node,
				   const char *uname, int depth, void *data)
{
	const void *basep, *entryp;
	unsigned long basesz, entrysz;

	if (depth != 1 || strcmp(uname, "ibm,opal") != 0)
		return 0;

	basep  = of_get_flat_dt_prop(node, "opal-base-address", &basesz);
	entryp = of_get_flat_dt_prop(node, "opal-entry-address", &entrysz);

	if (!basep || !entryp)
		return 1;

	opal.base = of_read_number(basep, basesz/4);
	opal.entry = of_read_number(entryp, entrysz/4);

	pr_debug("OPAL Base  = 0x%llx (basep=%p basesz=%ld)\n",
		 opal.base, basep, basesz);
	pr_debug("OPAL Entry = 0x%llx (entryp=%p basesz=%ld)\n",
		 opal.entry, entryp, entrysz);

	powerpc_firmware_features |= FW_FEATURE_OPAL;
	if (of_flat_dt_is_compatible(node, "ibm,opal-v2")) {
		powerpc_firmware_features |= FW_FEATURE_OPALv2;
		printk("OPAL V2 detected !\n");
	} else {
		printk("OPAL V1 detected !\n");
	}

	return 1;
}

int opal_get_chars(uint32_t vtermno, char *buf, int count)
{
	s64 len, rc;
	u64 evt;

	if (!opal.entry)
		return -ENODEV;
	opal_poll_events(&evt);
	if ((evt & OPAL_EVENT_CONSOLE_INPUT) == 0)
		return 0;
	len = count;
	rc = opal_console_read(vtermno, &len, buf);
	if (rc == OPAL_SUCCESS)
		return len;
	return 0;
}

int opal_put_chars(uint32_t vtermno, const char *data, int total_len)
{
	int written = 0;
	s64 len, rc;
	unsigned long flags;
	u64 evt;

	if (!opal.entry)
		return -ENODEV;

	/* We want put_chars to be atomic to avoid mangling of hvsi
	 * packets. To do that, we first test for room and return
	 * -EAGAIN if there isn't enough.
	 *
	 * Unfortunately, opal_console_write_buffer_space() doesn't
	 * appear to work on opal v1, so we just assume there is
	 * enough room and be done with it
	 */
	spin_lock_irqsave(&opal_write_lock, flags);
	if (firmware_has_feature(FW_FEATURE_OPALv2)) {
		rc = opal_console_write_buffer_space(vtermno, &len);
		if (rc || len < total_len) {
			spin_unlock_irqrestore(&opal_write_lock, flags);
			/* Closed -> drop characters */
			if (rc)
				return total_len;
			opal_poll_events(&evt);
			return -EAGAIN;
		}
	}

	/* We still try to handle partial completions, though they
	 * should no longer happen.
	 */
	rc = OPAL_BUSY;
	while(total_len > 0 && (rc == OPAL_BUSY ||
				rc == OPAL_BUSY_EVENT || rc == OPAL_SUCCESS)) {
		len = total_len;
		rc = opal_console_write(vtermno, &len, data);
		if (rc == OPAL_SUCCESS) {
			total_len -= len;
			data += len;
			written += len;
		}
		/* This is a bit nasty but we need that for the console to
		 * flush when there aren't any interrupts. We will clean
		 * things a bit later to limit that to synchronous path
		 * such as the kernel console and xmon/udbg
		 */
		do
			opal_poll_events(&evt);
		while(rc == OPAL_SUCCESS && (evt & OPAL_EVENT_CONSOLE_OUTPUT));
	}
	spin_unlock_irqrestore(&opal_write_lock, flags);
	return written;
}

static int __init opal_init(void)
{
	struct device_node *np, *consoles;

	opal_node = of_find_node_by_path("/ibm,opal");
	if (!opal_node) {
		pr_warn("opal: Node not found\n");
		return -ENODEV;
	}
	if (firmware_has_feature(FW_FEATURE_OPALv2))
		consoles = of_find_node_by_path("/ibm,opal/consoles");
	else
		consoles = of_node_get(opal_node);

	/* Register serial ports */
	for_each_child_of_node(consoles, np) {
		if (strcmp(np->name, "serial"))
			continue;
		of_platform_device_create(np, NULL, NULL);
	}
	of_node_put(consoles);
	return 0;
}
subsys_initcall(opal_init);
