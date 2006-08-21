/*
 * Cryptographic API for algorithms (i.e., low-level API).
 *
 * Copyright (c) 2006 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>

#include "internal.h"

static inline int crypto_set_driver_name(struct crypto_alg *alg)
{
	static const char suffix[] = "-generic";
	char *driver_name = alg->cra_driver_name;
	int len;

	if (*driver_name)
		return 0;

	len = strlcpy(driver_name, alg->cra_name, CRYPTO_MAX_ALG_NAME);
	if (len + sizeof(suffix) > CRYPTO_MAX_ALG_NAME)
		return -ENAMETOOLONG;

	memcpy(driver_name + len, suffix, sizeof(suffix));
	return 0;
}

int crypto_register_alg(struct crypto_alg *alg)
{
	int ret;
	struct crypto_alg *q;

	if (alg->cra_alignmask & (alg->cra_alignmask + 1))
		return -EINVAL;

	if (alg->cra_alignmask & alg->cra_blocksize)
		return -EINVAL;

	if (alg->cra_blocksize > PAGE_SIZE / 8)
		return -EINVAL;

	if (alg->cra_priority < 0)
		return -EINVAL;
	
	ret = crypto_set_driver_name(alg);
	if (unlikely(ret))
		return ret;

	down_write(&crypto_alg_sem);
	
	list_for_each_entry(q, &crypto_alg_list, cra_list) {
		if (q == alg) {
			ret = -EEXIST;
			goto out;
		}
	}
	
	list_add(&alg->cra_list, &crypto_alg_list);
	atomic_set(&alg->cra_refcnt, 1);
out:	
	up_write(&crypto_alg_sem);
	return ret;
}
EXPORT_SYMBOL_GPL(crypto_register_alg);

int crypto_unregister_alg(struct crypto_alg *alg)
{
	int ret = -ENOENT;
	struct crypto_alg *q;
	
	down_write(&crypto_alg_sem);
	list_for_each_entry(q, &crypto_alg_list, cra_list) {
		if (alg == q) {
			list_del(&alg->cra_list);
			ret = 0;
			goto out;
		}
	}
out:	
	up_write(&crypto_alg_sem);

	if (ret)
		return ret;

	BUG_ON(atomic_read(&alg->cra_refcnt) != 1);
	if (alg->cra_destroy)
		alg->cra_destroy(alg);

	return 0;
}
EXPORT_SYMBOL_GPL(crypto_unregister_alg);

static int __init crypto_algapi_init(void)
{
	crypto_init_proc();
	return 0;
}

static void __exit crypto_algapi_exit(void)
{
	crypto_exit_proc();
}

module_init(crypto_algapi_init);
module_exit(crypto_algapi_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Cryptographic algorithms API");
