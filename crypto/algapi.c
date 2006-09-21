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
#include <linux/list.h>
#include <linux/module.h>
#include <linux/string.h>

#include "internal.h"

static LIST_HEAD(crypto_template_list);

void crypto_larval_error(const char *name, u32 type, u32 mask)
{
	struct crypto_alg *alg;

	down_read(&crypto_alg_sem);
	alg = __crypto_alg_lookup(name, type, mask);
	up_read(&crypto_alg_sem);

	if (alg) {
		if (crypto_is_larval(alg)) {
			struct crypto_larval *larval = (void *)alg;
			complete(&larval->completion);
		}
		crypto_mod_put(alg);
	}
}
EXPORT_SYMBOL_GPL(crypto_larval_error);

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

static int crypto_check_alg(struct crypto_alg *alg)
{
	if (alg->cra_alignmask & (alg->cra_alignmask + 1))
		return -EINVAL;

	if (alg->cra_alignmask & alg->cra_blocksize)
		return -EINVAL;

	if (alg->cra_blocksize > PAGE_SIZE / 8)
		return -EINVAL;

	if (alg->cra_priority < 0)
		return -EINVAL;

	return crypto_set_driver_name(alg);
}

static int __crypto_register_alg(struct crypto_alg *alg)
{
	struct crypto_alg *q;
	int ret = -EEXIST;

	atomic_set(&alg->cra_refcnt, 1);
	list_for_each_entry(q, &crypto_alg_list, cra_list) {
		if (q == alg)
			goto out;
		if (crypto_is_larval(q) &&
		    (!strcmp(alg->cra_name, q->cra_name) ||
		     !strcmp(alg->cra_driver_name, q->cra_name))) {
			struct crypto_larval *larval = (void *)q;

			if ((q->cra_flags ^ alg->cra_flags) & larval->mask)
				continue;
			if (!crypto_mod_get(alg))
				continue;
			larval->adult = alg;
			complete(&larval->completion);
		}
	}
	
	list_add(&alg->cra_list, &crypto_alg_list);

	crypto_notify(CRYPTO_MSG_ALG_REGISTER, alg);
	ret = 0;

out:	
	return ret;
}

int crypto_register_alg(struct crypto_alg *alg)
{
	int err;

	err = crypto_check_alg(alg);
	if (err)
		return err;

	down_write(&crypto_alg_sem);
	err = __crypto_register_alg(alg);
	up_write(&crypto_alg_sem);

	return err;
}
EXPORT_SYMBOL_GPL(crypto_register_alg);

int crypto_unregister_alg(struct crypto_alg *alg)
{
	int ret = -ENOENT;
	
	down_write(&crypto_alg_sem);
	if (likely(!list_empty(&alg->cra_list))) {
		list_del_init(&alg->cra_list);
		ret = 0;
	}
	crypto_notify(CRYPTO_MSG_ALG_UNREGISTER, alg);
	up_write(&crypto_alg_sem);

	if (ret)
		return ret;

	BUG_ON(atomic_read(&alg->cra_refcnt) != 1);
	if (alg->cra_destroy)
		alg->cra_destroy(alg);

	return 0;
}
EXPORT_SYMBOL_GPL(crypto_unregister_alg);

int crypto_register_template(struct crypto_template *tmpl)
{
	struct crypto_template *q;
	int err = -EEXIST;

	down_write(&crypto_alg_sem);

	list_for_each_entry(q, &crypto_template_list, list) {
		if (q == tmpl)
			goto out;
	}

	list_add(&tmpl->list, &crypto_template_list);
	crypto_notify(CRYPTO_MSG_TMPL_REGISTER, tmpl);
	err = 0;
out:
	up_write(&crypto_alg_sem);
	return err;
}
EXPORT_SYMBOL_GPL(crypto_register_template);

void crypto_unregister_template(struct crypto_template *tmpl)
{
	struct crypto_instance *inst;
	struct hlist_node *p, *n;
	struct hlist_head *list;

	down_write(&crypto_alg_sem);

	BUG_ON(list_empty(&tmpl->list));
	list_del_init(&tmpl->list);

	list = &tmpl->instances;
	hlist_for_each_entry(inst, p, list, list) {
		BUG_ON(list_empty(&inst->alg.cra_list));
		list_del_init(&inst->alg.cra_list);
		crypto_notify(CRYPTO_MSG_ALG_UNREGISTER, &inst->alg);
	}

	crypto_notify(CRYPTO_MSG_TMPL_UNREGISTER, tmpl);

	up_write(&crypto_alg_sem);

	hlist_for_each_entry_safe(inst, p, n, list, list) {
		BUG_ON(atomic_read(&inst->alg.cra_refcnt) != 1);
		tmpl->free(inst);
	}
}
EXPORT_SYMBOL_GPL(crypto_unregister_template);

static struct crypto_template *__crypto_lookup_template(const char *name)
{
	struct crypto_template *q, *tmpl = NULL;

	down_read(&crypto_alg_sem);
	list_for_each_entry(q, &crypto_template_list, list) {
		if (strcmp(q->name, name))
			continue;
		if (unlikely(!crypto_tmpl_get(q)))
			continue;

		tmpl = q;
		break;
	}
	up_read(&crypto_alg_sem);

	return tmpl;
}

struct crypto_template *crypto_lookup_template(const char *name)
{
	return try_then_request_module(__crypto_lookup_template(name), name);
}
EXPORT_SYMBOL_GPL(crypto_lookup_template);

int crypto_register_instance(struct crypto_template *tmpl,
			     struct crypto_instance *inst)
{
	int err = -EINVAL;

	if (inst->alg.cra_destroy)
		goto err;

	err = crypto_check_alg(&inst->alg);
	if (err)
		goto err;

	inst->alg.cra_module = tmpl->module;

	down_write(&crypto_alg_sem);

	err = __crypto_register_alg(&inst->alg);
	if (err)
		goto unlock;

	hlist_add_head(&inst->list, &tmpl->instances);
	inst->tmpl = tmpl;

unlock:
	up_write(&crypto_alg_sem);

err:
	return err;
}
EXPORT_SYMBOL_GPL(crypto_register_instance);

int crypto_register_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&crypto_chain, nb);
}
EXPORT_SYMBOL_GPL(crypto_register_notifier);

int crypto_unregister_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&crypto_chain, nb);
}
EXPORT_SYMBOL_GPL(crypto_unregister_notifier);

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
