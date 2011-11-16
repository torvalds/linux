/*
 * Host AP crypto routines
 *
 * Copyright (c) 2002-2003, Jouni Malinen <jkmaline@cc.hut.fi>
 * Portions Copyright (C) 2004, Intel Corporation <jketreno@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation. See README and COPYING for
 * more details.
 *
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/errno.h>

#include "rtllib.h"

struct rtllib_crypto_alg {
	struct list_head list;
	struct rtllib_crypto_ops *ops;
};


struct rtllib_crypto {
	struct list_head algs;
	spinlock_t lock;
};

static struct rtllib_crypto *hcrypt;

void rtllib_crypt_deinit_entries(struct rtllib_device *ieee,
					   int force)
{
	struct list_head *ptr, *n;
	struct rtllib_crypt_data *entry;

	for (ptr = ieee->crypt_deinit_list.next, n = ptr->next;
	     ptr != &ieee->crypt_deinit_list; ptr = n, n = ptr->next) {
		entry = list_entry(ptr, struct rtllib_crypt_data, list);

		if (atomic_read(&entry->refcnt) != 0 && !force)
			continue;

		list_del(ptr);

		if (entry->ops)
			entry->ops->deinit(entry->priv);
		kfree(entry);
	}
}

void rtllib_crypt_deinit_handler(unsigned long data)
{
	struct rtllib_device *ieee = (struct rtllib_device *)data;
	unsigned long flags;

	spin_lock_irqsave(&ieee->lock, flags);
	rtllib_crypt_deinit_entries(ieee, 0);
	if (!list_empty(&ieee->crypt_deinit_list)) {
		printk(KERN_DEBUG "%s: entries remaining in delayed crypt "
		       "deletion list\n", ieee->dev->name);
		ieee->crypt_deinit_timer.expires = jiffies + HZ;
		add_timer(&ieee->crypt_deinit_timer);
	}
	spin_unlock_irqrestore(&ieee->lock, flags);

}

void rtllib_crypt_delayed_deinit(struct rtllib_device *ieee,
				    struct rtllib_crypt_data **crypt)
{
	struct rtllib_crypt_data *tmp;
	unsigned long flags;

	if (*crypt == NULL)
		return;

	tmp = *crypt;
	*crypt = NULL;

	/* must not run ops->deinit() while there may be pending encrypt or
	 * decrypt operations. Use a list of delayed deinits to avoid needing
	 * locking. */

	spin_lock_irqsave(&ieee->lock, flags);
	list_add(&tmp->list, &ieee->crypt_deinit_list);
	if (!timer_pending(&ieee->crypt_deinit_timer)) {
		ieee->crypt_deinit_timer.expires = jiffies + HZ;
		add_timer(&ieee->crypt_deinit_timer);
	}
	spin_unlock_irqrestore(&ieee->lock, flags);
}

int rtllib_register_crypto_ops(struct rtllib_crypto_ops *ops)
{
	unsigned long flags;
	struct rtllib_crypto_alg *alg;

	if (hcrypt == NULL)
		return -1;

	alg = kmalloc(sizeof(*alg), GFP_KERNEL);
	if (alg == NULL)
		return -ENOMEM;

	memset(alg, 0, sizeof(*alg));
	alg->ops = ops;

	spin_lock_irqsave(&hcrypt->lock, flags);
	list_add(&alg->list, &hcrypt->algs);
	spin_unlock_irqrestore(&hcrypt->lock, flags);

	printk(KERN_DEBUG "rtllib_crypt: registered algorithm '%s'\n",
	       ops->name);

	return 0;
}

int rtllib_unregister_crypto_ops(struct rtllib_crypto_ops *ops)
{
	unsigned long flags;
	struct list_head *ptr;
	struct rtllib_crypto_alg *del_alg = NULL;

	if (hcrypt == NULL)
		return -1;

	spin_lock_irqsave(&hcrypt->lock, flags);
	for (ptr = hcrypt->algs.next; ptr != &hcrypt->algs; ptr = ptr->next) {
		struct rtllib_crypto_alg *alg =
			(struct rtllib_crypto_alg *) ptr;
		if (alg->ops == ops) {
			list_del(&alg->list);
			del_alg = alg;
			break;
		}
	}
	spin_unlock_irqrestore(&hcrypt->lock, flags);

	if (del_alg) {
		printk(KERN_DEBUG "rtllib_crypt: unregistered algorithm "
		       "'%s'\n", ops->name);
		kfree(del_alg);
	}

	return del_alg ? 0 : -1;
}


struct rtllib_crypto_ops *rtllib_get_crypto_ops(const char *name)
{
	unsigned long flags;
	struct list_head *ptr;
	struct rtllib_crypto_alg *found_alg = NULL;

	if (hcrypt == NULL)
		return NULL;

	spin_lock_irqsave(&hcrypt->lock, flags);
	for (ptr = hcrypt->algs.next; ptr != &hcrypt->algs; ptr = ptr->next) {
		struct rtllib_crypto_alg *alg =
			(struct rtllib_crypto_alg *) ptr;
		if (strcmp(alg->ops->name, name) == 0) {
			found_alg = alg;
			break;
		}
	}
	spin_unlock_irqrestore(&hcrypt->lock, flags);

	if (found_alg)
		return found_alg->ops;
	else
		return NULL;
}


static void * rtllib_crypt_null_init(int keyidx) { return (void *) 1; }
static void rtllib_crypt_null_deinit(void *priv) {}

static struct rtllib_crypto_ops rtllib_crypt_null = {
	.name			= "NULL",
	.init			= rtllib_crypt_null_init,
	.deinit			= rtllib_crypt_null_deinit,
	.encrypt_mpdu		= NULL,
	.decrypt_mpdu		= NULL,
	.encrypt_msdu		= NULL,
	.decrypt_msdu		= NULL,
	.set_key		= NULL,
	.get_key		= NULL,
	.extra_prefix_len	= 0,
	.extra_postfix_len	= 0,
	.owner			= THIS_MODULE,
};


int __init rtllib_crypto_init(void)
{
	int ret = -ENOMEM;

	hcrypt = kmalloc(sizeof(*hcrypt), GFP_KERNEL);
	if (!hcrypt)
		goto out;

	memset(hcrypt, 0, sizeof(*hcrypt));
	INIT_LIST_HEAD(&hcrypt->algs);
	spin_lock_init(&hcrypt->lock);

	ret = rtllib_register_crypto_ops(&rtllib_crypt_null);
	if (ret < 0) {
		kfree(hcrypt);
		hcrypt = NULL;
	}
out:
	return ret;
}


void __exit rtllib_crypto_deinit(void)
{
	struct list_head *ptr, *n;

	if (hcrypt == NULL)
		return;

	for (ptr = hcrypt->algs.next, n = ptr->next; ptr != &hcrypt->algs;
	     ptr = n, n = ptr->next) {
		struct rtllib_crypto_alg *alg =
			(struct rtllib_crypto_alg *) ptr;
		list_del(ptr);
		printk(KERN_DEBUG "rtllib_crypt: unregistered algorithm "
		       "'%s' (deinit)\n", alg->ops->name);
		kfree(alg);
	}

	kfree(hcrypt);
}
