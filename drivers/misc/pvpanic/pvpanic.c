// SPDX-License-Identifier: GPL-2.0+
/*
 *  Pvpanic Device Support
 *
 *  Copyright (C) 2013 Fujitsu.
 *  Copyright (C) 2018 ZTE.
 *  Copyright (C) 2021 Oracle.
 */

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/kexec.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#include <uapi/misc/pvpanic.h>

#include "pvpanic.h"

MODULE_AUTHOR("Mihai Carabas <mihai.carabas@oracle.com>");
MODULE_DESCRIPTION("pvpanic device driver ");
MODULE_LICENSE("GPL");

static void __iomem *base;
static unsigned int capability;
static unsigned int events;

static void
pvpanic_send_event(unsigned int event)
{
	if (event & capability & events)
		iowrite8(event, base);
}

static int
pvpanic_panic_notify(struct notifier_block *nb, unsigned long code,
		     void *unused)
{
	unsigned int event = PVPANIC_PANICKED;

	if (kexec_crash_loaded())
		event = PVPANIC_CRASH_LOADED;

	pvpanic_send_event(event);

	return NOTIFY_DONE;
}

static struct notifier_block pvpanic_panic_nb = {
	.notifier_call = pvpanic_panic_notify,
	.priority = 1, /* let this called before broken drm_fb_helper */
};

void pvpanic_probe(void __iomem *pbase, unsigned int dev_cap)
{
	base = pbase;
	capability = dev_cap;
	events = capability;

	if (capability)
		atomic_notifier_chain_register(&panic_notifier_list,
					       &pvpanic_panic_nb);
}
EXPORT_SYMBOL_GPL(pvpanic_probe);

void pvpanic_remove(void)
{
	if (capability)
		atomic_notifier_chain_unregister(&panic_notifier_list,
						 &pvpanic_panic_nb);
	base = NULL;
}
EXPORT_SYMBOL_GPL(pvpanic_remove);

void pvpanic_set_events(unsigned int dev_events)
{
	events = dev_events;
}
EXPORT_SYMBOL_GPL(pvpanic_set_events);
