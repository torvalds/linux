// SPDX-License-Identifier: GPL-2.0
/*
 * Host AP crypto routines
 *
 * Copyright (c) 2002-2003, Jouni Malinen <jkmaline@cc.hut.fi>
 * Portions Copyright (C) 2004, Intel Corporation <jketreno@linux.intel.com>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/errno.h>

#include "ieee80211.h"

MODULE_AUTHOR("Jouni Malinen");
MODULE_DESCRIPTION("HostAP crypto");
MODULE_LICENSE("GPL");

struct ieee80211_crypto_alg {
	struct list_head list;
	struct ieee80211_crypto_ops *ops;
};


struct ieee80211_crypto {
	struct list_head algs;
	spinlock_t lock;
};

static struct ieee80211_crypto *hcrypt;

void ieee80211_crypt_deinit_entries(struct ieee80211_device *ieee,
					   int force)
{
	struct list_head *ptr, *n;
	struct ieee80211_crypt_data *entry;

	for (ptr = ieee->crypt_deinit_list.next, n = ptr->next;
	     ptr != &ieee->crypt_deinit_list; ptr = n, n = ptr->next) {
		entry = list_entry(ptr, struct ieee80211_crypt_data, list);

		if (atomic_read(&entry->refcnt) != 0 && !force)
			continue;

		list_del(ptr);

		if (entry->ops)
			entry->ops->deinit(entry->priv);
		kfree(entry);
	}
}

void ieee80211_crypt_deinit_handler(struct timer_list *t)
{
	struct ieee80211_device *ieee = from_timer(ieee, t, crypt_deinit_timer);
	unsigned long flags;

	spin_lock_irqsave(&ieee->lock, flags);
	ieee80211_crypt_deinit_entries(ieee, 0);
	if (!list_empty(&ieee->crypt_deinit_list)) {
		netdev_dbg(ieee->dev, "%s: entries remaining in delayed crypt deletion list\n",
				ieee->dev->name);
		ieee->crypt_deinit_timer.expires = jiffies + HZ;
		add_timer(&ieee->crypt_deinit_timer);
	}
	spin_unlock_irqrestore(&ieee->lock, flags);

}

void ieee80211_crypt_delayed_deinit(struct ieee80211_device *ieee,
				    struct ieee80211_crypt_data **crypt)
{
	struct ieee80211_crypt_data *tmp;
	unsigned long flags;

	if (!(*crypt))
		return;

	tmp = *crypt;
	*crypt = NULL;

	/* must not run ops->deinit() while there may be pending encrypt or
	 * decrypt operations. Use a list of delayed deinits to avoid needing
	 * locking.
	 */

	spin_lock_irqsave(&ieee->lock, flags);
	list_add(&tmp->list, &ieee->crypt_deinit_list);
	if (!timer_pending(&ieee->crypt_deinit_timer)) {
		ieee->crypt_deinit_timer.expires = jiffies + HZ;
		add_timer(&ieee->crypt_deinit_timer);
	}
	spin_unlock_irqrestore(&ieee->lock, flags);
}

int ieee80211_register_crypto_ops(struct ieee80211_crypto_ops *ops)
{
	unsigned long flags;
	struct ieee80211_crypto_alg *alg;

	if (!hcrypt)
		return -1;

	alg = kzalloc(sizeof(*alg), GFP_KERNEL);
	if (!alg)
		return -ENOMEM;

	alg->ops = ops;

	spin_lock_irqsave(&hcrypt->lock, flags);
	list_add(&alg->list, &hcrypt->algs);
	spin_unlock_irqrestore(&hcrypt->lock, flags);

	pr_debug("ieee80211_crypt: registered algorithm '%s'\n",
	       ops->name);

	return 0;
}

int ieee80211_unregister_crypto_ops(struct ieee80211_crypto_ops *ops)
{
	unsigned long flags;
	struct list_head *ptr;
	struct ieee80211_crypto_alg *del_alg = NULL;

	if (!hcrypt)
		return -1;

	spin_lock_irqsave(&hcrypt->lock, flags);
	for (ptr = hcrypt->algs.next; ptr != &hcrypt->algs; ptr = ptr->next) {
		struct ieee80211_crypto_alg *alg =
			(struct ieee80211_crypto_alg *)ptr;
		if (alg->ops == ops) {
			list_del(&alg->list);
			del_alg = alg;
			break;
		}
	}
	spin_unlock_irqrestore(&hcrypt->lock, flags);

	if (del_alg) {
		pr_debug("ieee80211_crypt: unregistered algorithm '%s'\n",
				ops->name);
		kfree(del_alg);
	}

	return del_alg ? 0 : -1;
}


struct ieee80211_crypto_ops *ieee80211_get_crypto_ops(const char *name)
{
	unsigned long flags;
	struct list_head *ptr;
	struct ieee80211_crypto_alg *found_alg = NULL;

	if (!hcrypt)
		return NULL;

	spin_lock_irqsave(&hcrypt->lock, flags);
	for (ptr = hcrypt->algs.next; ptr != &hcrypt->algs; ptr = ptr->next) {
		struct ieee80211_crypto_alg *alg =
			(struct ieee80211_crypto_alg *)ptr;
		if (strcmp(alg->ops->name, name) == 0) {
			found_alg = alg;
			break;
		}
	}
	spin_unlock_irqrestore(&hcrypt->lock, flags);

	if (found_alg)
		return found_alg->ops;
	return NULL;
}


static void *ieee80211_crypt_null_init(int keyidx) { return (void *)1; }
static void ieee80211_crypt_null_deinit(void *priv) {}

static struct ieee80211_crypto_ops ieee80211_crypt_null = {
	.name			= "NULL",
	.init			= ieee80211_crypt_null_init,
	.deinit			= ieee80211_crypt_null_deinit,
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

int __init ieee80211_crypto_init(void)
{
	int ret = -ENOMEM;

	hcrypt = kzalloc(sizeof(*hcrypt), GFP_KERNEL);
	if (!hcrypt)
		goto out;

	INIT_LIST_HEAD(&hcrypt->algs);
	spin_lock_init(&hcrypt->lock);

	ret = ieee80211_register_crypto_ops(&ieee80211_crypt_null);
	if (ret < 0) {
		kfree(hcrypt);
		hcrypt = NULL;
	}
out:
	return ret;
}

void __exit ieee80211_crypto_deinit(void)
{
	struct list_head *ptr, *n;

	if (!hcrypt)
		return;

	for (ptr = hcrypt->algs.next, n = ptr->next; ptr != &hcrypt->algs;
	     ptr = n, n = ptr->next) {
		struct ieee80211_crypto_alg *alg =
			(struct ieee80211_crypto_alg *)ptr;
		list_del(ptr);
		pr_debug("ieee80211_crypt: unregistered algorithm '%s' (deinit)\n",
				alg->ops->name);
		kfree(alg);
	}

	kfree(hcrypt);
}
