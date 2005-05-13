/*
 * Host AP crypto routines
 *
 * Copyright (c) 2002-2003, Jouni Malinen <jkmaline@cc.hut.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation. See README and COPYING for
 * more details.
 */

struct hostap_crypto_alg {
	struct list_head list;
	struct hostap_crypto_ops *ops;
};


struct hostap_crypto {
	struct list_head algs;
	spinlock_t lock;
};

static struct hostap_crypto *hcrypt;


int hostap_register_crypto_ops(struct hostap_crypto_ops *ops)
{
	unsigned long flags;
	struct hostap_crypto_alg *alg;

	if (hcrypt == NULL)
		return -1;

	alg = (struct hostap_crypto_alg *) kmalloc(sizeof(*alg), GFP_KERNEL);
	if (alg == NULL)
		return -ENOMEM;

	memset(alg, 0, sizeof(*alg));
	alg->ops = ops;

	spin_lock_irqsave(&hcrypt->lock, flags);
	list_add(&alg->list, &hcrypt->algs);
	spin_unlock_irqrestore(&hcrypt->lock, flags);

	printk(KERN_DEBUG "hostap_crypt: registered algorithm '%s'\n",
	       ops->name);

	return 0;
}


int hostap_unregister_crypto_ops(struct hostap_crypto_ops *ops)
{
	unsigned long flags;
	struct list_head *ptr;
	struct hostap_crypto_alg *del_alg = NULL;

	if (hcrypt == NULL)
		return -1;

	spin_lock_irqsave(&hcrypt->lock, flags);
	for (ptr = hcrypt->algs.next; ptr != &hcrypt->algs; ptr = ptr->next) {
		struct hostap_crypto_alg *alg =
			(struct hostap_crypto_alg *) ptr;
		if (alg->ops == ops) {
			list_del(&alg->list);
			del_alg = alg;
			break;
		}
	}
	spin_unlock_irqrestore(&hcrypt->lock, flags);

	if (del_alg) {
		printk(KERN_DEBUG "hostap_crypt: unregistered algorithm "
		       "'%s'\n", ops->name);
		kfree(del_alg);
	}

	return del_alg ? 0 : -1;
}


struct hostap_crypto_ops * hostap_get_crypto_ops(const char *name)
{
	unsigned long flags;
	struct list_head *ptr;
	struct hostap_crypto_alg *found_alg = NULL;

	if (hcrypt == NULL)
		return NULL;

	spin_lock_irqsave(&hcrypt->lock, flags);
	for (ptr = hcrypt->algs.next; ptr != &hcrypt->algs; ptr = ptr->next) {
		struct hostap_crypto_alg *alg =
			(struct hostap_crypto_alg *) ptr;
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


static void * hostap_crypt_null_init(int keyidx) { return (void *) 1; }
static void hostap_crypt_null_deinit(void *priv) {}

static struct hostap_crypto_ops hostap_crypt_null = {
	.name			= "NULL",
	.init			= hostap_crypt_null_init,
	.deinit			= hostap_crypt_null_deinit,
	.encrypt_mpdu		= NULL,
	.decrypt_mpdu		= NULL,
	.encrypt_msdu		= NULL,
	.decrypt_msdu		= NULL,
	.set_key		= NULL,
	.get_key		= NULL,
	.extra_prefix_len	= 0,
	.extra_postfix_len	= 0
};


static int __init hostap_crypto_init(void)
{
	hcrypt = (struct hostap_crypto *) kmalloc(sizeof(*hcrypt), GFP_KERNEL);
	if (hcrypt == NULL)
		return -ENOMEM;

	memset(hcrypt, 0, sizeof(*hcrypt));
	INIT_LIST_HEAD(&hcrypt->algs);
	spin_lock_init(&hcrypt->lock);

	(void) hostap_register_crypto_ops(&hostap_crypt_null);

	return 0;
}


static void __exit hostap_crypto_deinit(void)
{
	struct list_head *ptr, *n;

	if (hcrypt == NULL)
		return;

	for (ptr = hcrypt->algs.next, n = ptr->next; ptr != &hcrypt->algs;
	     ptr = n, n = ptr->next) {
		struct hostap_crypto_alg *alg =
			(struct hostap_crypto_alg *) ptr;
		list_del(ptr);
		printk(KERN_DEBUG "hostap_crypt: unregistered algorithm "
		       "'%s' (deinit)\n", alg->ops->name);
		kfree(alg);
	}

	kfree(hcrypt);
}


EXPORT_SYMBOL(hostap_register_crypto_ops);
EXPORT_SYMBOL(hostap_unregister_crypto_ops);
EXPORT_SYMBOL(hostap_get_crypto_ops);
