// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2015-2016 Mentor Graphics
 */

#include <linux/list.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/watchcat.h>

#include "watchcat_core.h"
#include "watchcat_pretimeout.h"

/* Default watchcat pretimeout governor */
static struct watchcat_governor *default_gov;

/* The spinlock protects default_gov, wdd->gov and pretimeout_list */
static DEFINE_SPINLOCK(pretimeout_lock);

/* List of watchcat devices, which can generate a pretimeout event */
static LIST_HEAD(pretimeout_list);

struct watchcat_pretimeout {
	struct watchcat_device		*wdd;
	struct list_head		entry;
};

/* The mutex protects governor list and serializes external interfaces */
static DEFINE_MUTEX(governor_lock);

/* List of the registered watchcat pretimeout governors */
static LIST_HEAD(governor_list);

struct governor_priv {
	struct watchcat_governor	*gov;
	struct list_head		entry;
};

static struct governor_priv *find_governor_by_name(const char *gov_name)
{
	struct governor_priv *priv;

	list_for_each_entry(priv, &governor_list, entry)
		if (sysfs_streq(gov_name, priv->gov->name))
			return priv;

	return NULL;
}

int watchcat_pretimeout_available_governors_get(char *buf)
{
	struct governor_priv *priv;
	int count = 0;

	mutex_lock(&governor_lock);

	list_for_each_entry(priv, &governor_list, entry)
		count += sysfs_emit_at(buf, count, "%s\n", priv->gov->name);

	mutex_unlock(&governor_lock);

	return count;
}

int watchcat_pretimeout_governor_get(struct watchcat_device *wdd, char *buf)
{
	int count = 0;

	spin_lock_irq(&pretimeout_lock);
	if (wdd->gov)
		count = sysfs_emit(buf, "%s\n", wdd->gov->name);
	spin_unlock_irq(&pretimeout_lock);

	return count;
}

int watchcat_pretimeout_governor_set(struct watchcat_device *wdd,
				     const char *buf)
{
	struct governor_priv *priv;

	mutex_lock(&governor_lock);

	priv = find_governor_by_name(buf);
	if (!priv) {
		mutex_unlock(&governor_lock);
		return -EINVAL;
	}

	spin_lock_irq(&pretimeout_lock);
	wdd->gov = priv->gov;
	spin_unlock_irq(&pretimeout_lock);

	mutex_unlock(&governor_lock);

	return 0;
}

void watchcat_notify_pretimeout(struct watchcat_device *wdd)
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
EXPORT_SYMBOL_GPL(watchcat_notify_pretimeout);

int watchcat_register_governor(struct watchcat_governor *gov)
{
	struct watchcat_pretimeout *p;
	struct governor_priv *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mutex_lock(&governor_lock);

	if (find_governor_by_name(gov->name)) {
		mutex_unlock(&governor_lock);
		kfree(priv);
		return -EBUSY;
	}

	priv->gov = gov;
	list_add(&priv->entry, &governor_list);

	if (!strncmp(gov->name, watchcat_PRETIMEOUT_DEFAULT_GOV,
		     watchcat_GOV_NAME_MAXLEN)) {
		spin_lock_irq(&pretimeout_lock);
		default_gov = gov;

		list_for_each_entry(p, &pretimeout_list, entry)
			if (!p->wdd->gov)
				p->wdd->gov = default_gov;
		spin_unlock_irq(&pretimeout_lock);
	}

	mutex_unlock(&governor_lock);

	return 0;
}
EXPORT_SYMBOL(watchcat_register_governor);

void watchcat_unregister_governor(struct watchcat_governor *gov)
{
	struct watchcat_pretimeout *p;
	struct governor_priv *priv, *t;

	mutex_lock(&governor_lock);

	list_for_each_entry_safe(priv, t, &governor_list, entry) {
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

	mutex_unlock(&governor_lock);
}
EXPORT_SYMBOL(watchcat_unregister_governor);

int watchcat_register_pretimeout(struct watchcat_device *wdd)
{
	struct watchcat_pretimeout *p;

	if (!watchcat_have_pretimeout(wdd))
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

void watchcat_unregister_pretimeout(struct watchcat_device *wdd)
{
	struct watchcat_pretimeout *p, *t;

	if (!watchcat_have_pretimeout(wdd))
		return;

	spin_lock_irq(&pretimeout_lock);
	wdd->gov = NULL;

	list_for_each_entry_safe(p, t, &pretimeout_list, entry) {
		if (p->wdd == wdd) {
			list_del(&p->entry);
			kfree(p);
			break;
		}
	}
	spin_unlock_irq(&pretimeout_lock);
}
