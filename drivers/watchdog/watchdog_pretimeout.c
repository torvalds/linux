// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2015-2016 Mentor Graphics
 */

#include <linux/list.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/watchdog.h>

#include "watchdog_pretimeout.h"

/* Default watchdog pretimeout goveryesr */
static struct watchdog_goveryesr *default_gov;

/* The spinlock protects default_gov, wdd->gov and pretimeout_list */
static DEFINE_SPINLOCK(pretimeout_lock);

/* List of watchdog devices, which can generate a pretimeout event */
static LIST_HEAD(pretimeout_list);

struct watchdog_pretimeout {
	struct watchdog_device		*wdd;
	struct list_head		entry;
};

/* The mutex protects goveryesr list and serializes external interfaces */
static DEFINE_MUTEX(goveryesr_lock);

/* List of the registered watchdog pretimeout goveryesrs */
static LIST_HEAD(goveryesr_list);

struct goveryesr_priv {
	struct watchdog_goveryesr	*gov;
	struct list_head		entry;
};

static struct goveryesr_priv *find_goveryesr_by_name(const char *gov_name)
{
	struct goveryesr_priv *priv;

	list_for_each_entry(priv, &goveryesr_list, entry)
		if (sysfs_streq(gov_name, priv->gov->name))
			return priv;

	return NULL;
}

int watchdog_pretimeout_available_goveryesrs_get(char *buf)
{
	struct goveryesr_priv *priv;
	int count = 0;

	mutex_lock(&goveryesr_lock);

	list_for_each_entry(priv, &goveryesr_list, entry)
		count += sprintf(buf + count, "%s\n", priv->gov->name);

	mutex_unlock(&goveryesr_lock);

	return count;
}

int watchdog_pretimeout_goveryesr_get(struct watchdog_device *wdd, char *buf)
{
	int count = 0;

	spin_lock_irq(&pretimeout_lock);
	if (wdd->gov)
		count = sprintf(buf, "%s\n", wdd->gov->name);
	spin_unlock_irq(&pretimeout_lock);

	return count;
}

int watchdog_pretimeout_goveryesr_set(struct watchdog_device *wdd,
				     const char *buf)
{
	struct goveryesr_priv *priv;

	mutex_lock(&goveryesr_lock);

	priv = find_goveryesr_by_name(buf);
	if (!priv) {
		mutex_unlock(&goveryesr_lock);
		return -EINVAL;
	}

	spin_lock_irq(&pretimeout_lock);
	wdd->gov = priv->gov;
	spin_unlock_irq(&pretimeout_lock);

	mutex_unlock(&goveryesr_lock);

	return 0;
}

void watchdog_yestify_pretimeout(struct watchdog_device *wdd)
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
EXPORT_SYMBOL_GPL(watchdog_yestify_pretimeout);

int watchdog_register_goveryesr(struct watchdog_goveryesr *gov)
{
	struct watchdog_pretimeout *p;
	struct goveryesr_priv *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mutex_lock(&goveryesr_lock);

	if (find_goveryesr_by_name(gov->name)) {
		mutex_unlock(&goveryesr_lock);
		kfree(priv);
		return -EBUSY;
	}

	priv->gov = gov;
	list_add(&priv->entry, &goveryesr_list);

	if (!strncmp(gov->name, WATCHDOG_PRETIMEOUT_DEFAULT_GOV,
		     WATCHDOG_GOV_NAME_MAXLEN)) {
		spin_lock_irq(&pretimeout_lock);
		default_gov = gov;

		list_for_each_entry(p, &pretimeout_list, entry)
			if (!p->wdd->gov)
				p->wdd->gov = default_gov;
		spin_unlock_irq(&pretimeout_lock);
	}

	mutex_unlock(&goveryesr_lock);

	return 0;
}
EXPORT_SYMBOL(watchdog_register_goveryesr);

void watchdog_unregister_goveryesr(struct watchdog_goveryesr *gov)
{
	struct watchdog_pretimeout *p;
	struct goveryesr_priv *priv, *t;

	mutex_lock(&goveryesr_lock);

	list_for_each_entry_safe(priv, t, &goveryesr_list, entry) {
		if (priv->gov == gov) {
			list_del(&priv->entry);
			kfree(priv);
			break;
		}
	}

	spin_lock_irq(&pretimeout_lock);
	list_for_each_entry(p, &pretimeout_list, entry)
		if (p->wdd->gov == gov)
			p->wdd->gov = default_gov;
	spin_unlock_irq(&pretimeout_lock);

	mutex_unlock(&goveryesr_lock);
}
EXPORT_SYMBOL(watchdog_unregister_goveryesr);

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
