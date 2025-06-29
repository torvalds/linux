// SPDX-License-Identifier: GPL-2.0-only
/*
 * libipw -- common bits for IPW drivers
 *
 * Copyright(c) 2008 John W. Linville <linville@tuxdriver.com>
 *
 * Portions copied from old ieee80211 component, w/ original copyright
 * notices below:
 *
 * Host AP crypto routines
 *
 * Copyright (c) 2002-2003, Jouni Malinen <j@w1.fi>
 * Portions Copyright (C) 2004, Intel Corporation <jketreno@linux.intel.com>
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/ieee80211.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/string.h>
#include "libipw.h"

struct libipw_crypto_alg {
	struct list_head list;
	const struct libipw_crypto_ops *ops;
};

static LIST_HEAD(libipw_crypto_algs);
static DEFINE_SPINLOCK(libipw_crypto_lock);

static void libipw_crypt_deinit_entries(struct libipw_crypt_info *info,
					  int force);
static void libipw_crypt_quiescing(struct libipw_crypt_info *info);
static void libipw_crypt_deinit_handler(struct timer_list *t);

int libipw_crypt_info_init(struct libipw_crypt_info *info, char *name,
				spinlock_t *lock)
{
	memset(info, 0, sizeof(*info));

	info->name = name;
	info->lock = lock;

	INIT_LIST_HEAD(&info->crypt_deinit_list);
	timer_setup(&info->crypt_deinit_timer, libipw_crypt_deinit_handler,
		    0);

	return 0;
}
EXPORT_SYMBOL(libipw_crypt_info_init);

void libipw_crypt_info_free(struct libipw_crypt_info *info)
{
	int i;

        libipw_crypt_quiescing(info);
        timer_delete_sync(&info->crypt_deinit_timer);
        libipw_crypt_deinit_entries(info, 1);

        for (i = 0; i < NUM_WEP_KEYS; i++) {
                struct libipw_crypt_data *crypt = info->crypt[i];
                if (crypt) {
                        if (crypt->ops) {
                                crypt->ops->deinit(crypt->priv);
                                module_put(crypt->ops->owner);
                        }
                        kfree(crypt);
                        info->crypt[i] = NULL;
                }
        }
}
EXPORT_SYMBOL(libipw_crypt_info_free);

static void libipw_crypt_deinit_entries(struct libipw_crypt_info *info,
					int force)
{
	struct libipw_crypt_data *entry, *next;
	unsigned long flags;

	spin_lock_irqsave(info->lock, flags);
	list_for_each_entry_safe(entry, next, &info->crypt_deinit_list, list) {
		if (atomic_read(&entry->refcnt) != 0 && !force)
			continue;

		list_del(&entry->list);

		if (entry->ops) {
			entry->ops->deinit(entry->priv);
			module_put(entry->ops->owner);
		}
		kfree(entry);
	}
	spin_unlock_irqrestore(info->lock, flags);
}

/* After this, crypt_deinit_list won't accept new members */
static void libipw_crypt_quiescing(struct libipw_crypt_info *info)
{
	unsigned long flags;

	spin_lock_irqsave(info->lock, flags);
	info->crypt_quiesced = 1;
	spin_unlock_irqrestore(info->lock, flags);
}

static void libipw_crypt_deinit_handler(struct timer_list *t)
{
	struct libipw_crypt_info *info = timer_container_of(info, t,
							    crypt_deinit_timer);
	unsigned long flags;

	libipw_crypt_deinit_entries(info, 0);

	spin_lock_irqsave(info->lock, flags);
	if (!list_empty(&info->crypt_deinit_list) && !info->crypt_quiesced) {
		printk(KERN_DEBUG "%s: entries remaining in delayed crypt "
		       "deletion list\n", info->name);
		info->crypt_deinit_timer.expires = jiffies + HZ;
		add_timer(&info->crypt_deinit_timer);
	}
	spin_unlock_irqrestore(info->lock, flags);
}

void libipw_crypt_delayed_deinit(struct libipw_crypt_info *info,
				 struct libipw_crypt_data **crypt)
{
	struct libipw_crypt_data *tmp;
	unsigned long flags;

	if (*crypt == NULL)
		return;

	tmp = *crypt;
	*crypt = NULL;

	/* must not run ops->deinit() while there may be pending encrypt or
	 * decrypt operations. Use a list of delayed deinits to avoid needing
	 * locking. */

	spin_lock_irqsave(info->lock, flags);
	if (!info->crypt_quiesced) {
		list_add(&tmp->list, &info->crypt_deinit_list);
		if (!timer_pending(&info->crypt_deinit_timer)) {
			info->crypt_deinit_timer.expires = jiffies + HZ;
			add_timer(&info->crypt_deinit_timer);
		}
	}
	spin_unlock_irqrestore(info->lock, flags);
}
EXPORT_SYMBOL(libipw_crypt_delayed_deinit);

int libipw_register_crypto_ops(const struct libipw_crypto_ops *ops)
{
	unsigned long flags;
	struct libipw_crypto_alg *alg;

	alg = kzalloc(sizeof(*alg), GFP_KERNEL);
	if (alg == NULL)
		return -ENOMEM;

	alg->ops = ops;

	spin_lock_irqsave(&libipw_crypto_lock, flags);
	list_add(&alg->list, &libipw_crypto_algs);
	spin_unlock_irqrestore(&libipw_crypto_lock, flags);

	printk(KERN_DEBUG "libipw_crypt: registered algorithm '%s'\n",
	       ops->name);

	return 0;
}
EXPORT_SYMBOL(libipw_register_crypto_ops);

int libipw_unregister_crypto_ops(const struct libipw_crypto_ops *ops)
{
	struct libipw_crypto_alg *alg;
	unsigned long flags;

	spin_lock_irqsave(&libipw_crypto_lock, flags);
	list_for_each_entry(alg, &libipw_crypto_algs, list) {
		if (alg->ops == ops)
			goto found;
	}
	spin_unlock_irqrestore(&libipw_crypto_lock, flags);
	return -EINVAL;

      found:
	printk(KERN_DEBUG "libipw_crypt: unregistered algorithm '%s'\n",
	       ops->name);
	list_del(&alg->list);
	spin_unlock_irqrestore(&libipw_crypto_lock, flags);
	kfree(alg);
	return 0;
}
EXPORT_SYMBOL(libipw_unregister_crypto_ops);

const struct libipw_crypto_ops *libipw_get_crypto_ops(const char *name)
{
	struct libipw_crypto_alg *alg;
	unsigned long flags;

	spin_lock_irqsave(&libipw_crypto_lock, flags);
	list_for_each_entry(alg, &libipw_crypto_algs, list) {
		if (strcmp(alg->ops->name, name) == 0)
			goto found;
	}
	spin_unlock_irqrestore(&libipw_crypto_lock, flags);
	return NULL;

      found:
	spin_unlock_irqrestore(&libipw_crypto_lock, flags);
	return alg->ops;
}
EXPORT_SYMBOL(libipw_get_crypto_ops);

static void *libipw_crypt_null_init(int keyidx)
{
	return (void *)1;
}

static void libipw_crypt_null_deinit(void *priv)
{
}

static const struct libipw_crypto_ops libipw_crypt_null = {
	.name = "NULL",
	.init = libipw_crypt_null_init,
	.deinit = libipw_crypt_null_deinit,
	.owner = THIS_MODULE,
};

int __init libipw_crypto_init(void)
{
	return libipw_register_crypto_ops(&libipw_crypt_null);
}

void libipw_crypto_exit(void)
{
	libipw_unregister_crypto_ops(&libipw_crypt_null);
	BUG_ON(!list_empty(&libipw_crypto_algs));
}
