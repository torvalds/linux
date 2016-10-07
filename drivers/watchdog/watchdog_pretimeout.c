/*
 * Copyright (C) 2015-2016 Mentor Graphics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/list.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/watchdog.h>

#include "watchdog_pretimeout.h"

/* Default watchdog pretimeout governor */
static struct watchdog_governor *default_gov;

/* The spinlock protects default_gov, wdd->gov and pretimeout_list */
static DEFINE_SPINLOCK(pretimeout_lock);

/* List of watchdog devices, which can generate a pretimeout event */
static LIST_HEAD(pretimeout_list);

struct watchdog_pretimeout {
	struct watchdog_device		*wdd;
	struct list_head		entry;
};

int watchdog_pretimeout_governor_get(struct watchdog_device *wdd, char *buf)
{
	int count = 0;

	spin_lock_irq(&pretimeout_lock);
	if (wdd->gov)
		count = sprintf(buf, "%s\n", wdd->gov->name);
	spin_unlock_irq(&pretimeout_lock);

	return count;
}

void watchdog_notify_pretimeout(struct watchdog_device *wdd)
{
	unsigned long flags;

	spin_lock_irqsave(&pretimeout_lock, flags);
	if (!wdd->gov) {
		spin_unlock_irqrestore(&pretimeout_lock, flags);
		return;
	}

	wdd->gov->pretimeout(wdd);
	spin_unlock_irqrestore(&pretimeout_lock, flags);
}
EXPORT_SYMBOL_GPL(watchdog_notify_pretimeout);

int watchdog_register_governor(struct watchdog_governor *gov)
{
	struct watchdog_pretimeout *p;

	if (!strncmp(gov->name, WATCHDOG_PRETIMEOUT_DEFAULT_GOV,
		     WATCHDOG_GOV_NAME_MAXLEN)) {
		spin_lock_irq(&pretimeout_lock);
		default_gov = gov;

		list_for_each_entry(p, &pretimeout_list, entry)
			if (!p->wdd->gov)
				p->wdd->gov = default_gov;
		spin_unlock_irq(&pretimeout_lock);
	}

	return 0;
}
EXPORT_SYMBOL(watchdog_register_governor);

void watchdog_unregister_governor(struct watchdog_governor *gov)
{
	struct watchdog_pretimeout *p;

	spin_lock_irq(&pretimeout_lock);
	list_for_each_entry(p, &pretimeout_list, entry)
		if (p->wdd->gov == gov)
			p->wdd->gov = default_gov;
	spin_unlock_irq(&pretimeout_lock);
}
EXPORT_SYMBOL(watchdog_unregister_governor);

int watchdog_register_pretimeout(struct watchdog_device *wdd)
{
	struct watchdog_pretimeout *p;

	if (!(wdd->info->options & WDIOF_PRETIMEOUT))
		return 0;

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	spin_lock_irq(&pretimeout_lock);
	list_add(&p->entry, &pretimeout_list);
	p->wdd = wdd;
	wdd->gov = default_gov;
	spin_unlock_irq(&pretimeout_lock);

	return 0;
}

void watchdog_unregister_pretimeout(struct watchdog_device *wdd)
{
	struct watchdog_pretimeout *p, *t;

	if (!(wdd->info->options & WDIOF_PRETIMEOUT))
		return;

	spin_lock_irq(&pretimeout_lock);
	wdd->gov = NULL;

	list_for_each_entry_safe(p, t, &pretimeout_list, entry) {
		if (p->wdd == wdd) {
			list_del(&p->entry);
			break;
		}
	}
	spin_unlock_irq(&pretimeout_lock);

	kfree(p);
}
