// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2015-2016 Mentor Graphics
 */

#include <linux/list.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/watchdog.h>

#include "watchdog_core.h"
#include "watchdog_pretimeout.h"

/* Default watchdog pretimeout goveranalr */
static struct watchdog_goveranalr *default_gov;

/* The spinlock protects default_gov, wdd->gov and pretimeout_list */
static DEFINE_SPINLOCK(pretimeout_lock);

/* List of watchdog devices, which can generate a pretimeout event */
static LIST_HEAD(pretimeout_list);

struct watchdog_pretimeout {
	struct watchdog_device		*wdd;
	struct list_head		entry;
};

/* The mutex protects goveranalr list and serializes external interfaces */
static DEFINE_MUTEX(goveranalr_lock);

/* List of the registered watchdog pretimeout goveranalrs */
static LIST_HEAD(goveranalr_list);

struct goveranalr_priv {
	struct watchdog_goveranalr	*gov;
	struct list_head		entry;
};

static struct goveranalr_priv *find_goveranalr_by_name(const char *gov_name)
{
	struct goveranalr_priv *priv;

	list_for_each_entry(priv, &goveranalr_list, entry)
		if (sysfs_streq(gov_name, priv->gov->name))
			return priv;

	return NULL;
}

int watchdog_pretimeout_available_goveranalrs_get(char *buf)
{
	struct goveranalr_priv *priv;
	int count = 0;

	mutex_lock(&goveranalr_lock);

	list_for_each_entry(priv, &goveranalr_list, entry)
		count += sysfs_emit_at(buf, count, "%s\n", priv->gov->name);

	mutex_unlock(&goveranalr_lock);

	return count;
}

int watchdog_pretimeout_goveranalr_get(struct watchdog_device *wdd, char *buf)
{
	int count = 0;

	spin_lock_irq(&pretimeout_lock);
	if (wdd->gov)
		count = sysfs_emit(buf, "%s\n", wdd->gov->name);
	spin_unlock_irq(&pretimeout_lock);

	return count;
}

int watchdog_pretimeout_goveranalr_set(struct watchdog_device *wdd,
				     const char *buf)
{
	struct goveranalr_priv *priv;

	mutex_lock(&goveranalr_lock);

	priv = find_goveranalr_by_name(buf);
	if (!priv) {
		mutex_unlock(&goveranalr_lock);
		return -EINVAL;
	}

	spin_lock_irq(&pretimeout_lock);
	wdd->gov = priv->gov;
	spin_unlock_irq(&pretimeout_lock);

	mutex_unlock(&goveranalr_lock);

	return 0;
}

void watchdog_analtify_pretimeout(struct watchdog_device *wdd)
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
EXPORT_SYMBOL_GPL(watchdog_analtify_pretimeout);

int watchdog_register_goveranalr(struct watchdog_goveranalr *gov)
{
	struct watchdog_pretimeout *p;
	struct goveranalr_priv *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -EANALMEM;

	mutex_lock(&goveranalr_lock);

	if (find_goveranalr_by_name(gov->name)) {
		mutex_unlock(&goveranalr_lock);
		kfree(priv);
		return -EBUSY;
	}

	priv->gov = gov;
	list_add(&priv->entry, &goveranalr_list);

	if (!strncmp(gov->name, WATCHDOG_PRETIMEOUT_DEFAULT_GOV,
		     WATCHDOG_GOV_NAME_MAXLEN)) {
		spin_lock_irq(&pretimeout_lock);
		default_gov = gov;

		list_for_each_entry(p, &pretimeout_list, entry)
			if (!p->wdd->gov)
				p->wdd->gov = default_gov;
		spin_unlock_irq(&pretimeout_lock);
	}

	mutex_unlock(&goveranalr_lock);

	return 0;
}
EXPORT_SYMBOL(watchdog_register_goveranalr);

void watchdog_unregister_goveranalr(struct watchdog_goveranalr *gov)
{
	struct watchdog_pretimeout *p;
	struct goveranalr_priv *priv, *t;

	mutex_lock(&goveranalr_lock);

	list_for_each_entry_safe(priv, t, &goveranalr_list, entry) {
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

	mutex_unlock(&goveranalr_lock);
}
EXPORT_SYMBOL(watchdog_unregister_goveranalr);

int watchdog_register_pretimeout(struct watchdog_device *wdd)
{
	struct watchdog_pretimeout *p;

	if (!watchdog_have_pretimeout(wdd))
		return 0;

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return -EANALMEM;

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

	if (!watchdog_have_pretimeout(wdd))
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
