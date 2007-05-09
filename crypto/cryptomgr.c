/*
 * Create default crypto algorithm instances.
 *
 * Copyright (c) 2006 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <linux/crypto.h>
#include <linux/ctype.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/rtnetlink.h>
#include <linux/sched.h>
#include <linux/string.h>

#include "internal.h"

struct cryptomgr_param {
	struct rtattr *tb[CRYPTOA_MAX];

	struct {
		struct rtattr attr;
		struct crypto_attr_type data;
	} type;

	struct {
		struct rtattr attr;
		struct crypto_attr_alg data;
	} alg;

	struct {
		char name[CRYPTO_MAX_ALG_NAME];
	} larval;

	char template[CRYPTO_MAX_ALG_NAME];
};

static int cryptomgr_probe(void *data)
{
	struct cryptomgr_param *param = data;
	struct crypto_template *tmpl;
	struct crypto_instance *inst;
	int err;

	tmpl = crypto_lookup_template(param->template);
	if (!tmpl)
		goto err;

	do {
		inst = tmpl->alloc(param->tb);
		if (IS_ERR(inst))
			err = PTR_ERR(inst);
		else if ((err = crypto_register_instance(tmpl, inst)))
			tmpl->free(inst);
	} while (err == -EAGAIN && !signal_pending(current));

	crypto_tmpl_put(tmpl);

	if (err)
		goto err;

out:
	kfree(param);
	module_put_and_exit(0);

err:
	crypto_larval_error(param->larval.name, param->type.data.type,
			    param->type.data.mask);
	goto out;
}

static int cryptomgr_schedule_probe(struct crypto_larval *larval)
{
	struct task_struct *thread;
	struct cryptomgr_param *param;
	const char *name = larval->alg.cra_name;
	const char *p;
	unsigned int len;

	if (!try_module_get(THIS_MODULE))
		goto err;

	param = kzalloc(sizeof(*param), GFP_KERNEL);
	if (!param)
		goto err_put_module;

	for (p = name; isalnum(*p) || *p == '-' || *p == '_'; p++)
		;

	len = p - name;
	if (!len || *p != '(')
		goto err_free_param;

	memcpy(param->template, name, len);

	name = p + 1;
	len = 0;
	for (p = name; *p; p++) {
		for (; isalnum(*p) || *p == '-' || *p == '_' || *p == '('; p++)
			;

		if (*p != ')')
			goto err_free_param;

		len = p - name;
	}

	if (!len || name[len + 1])
		goto err_free_param;

	param->type.attr.rta_len = sizeof(param->type);
	param->type.attr.rta_type = CRYPTOA_TYPE;
	param->type.data.type = larval->alg.cra_flags;
	param->type.data.mask = larval->mask;
	param->tb[CRYPTOA_TYPE - 1] = &param->type.attr;

	param->alg.attr.rta_len = sizeof(param->alg);
	param->alg.attr.rta_type = CRYPTOA_ALG;
	memcpy(param->alg.data.name, name, len);
	param->tb[CRYPTOA_ALG - 1] = &param->alg.attr;

	memcpy(param->larval.name, larval->alg.cra_name, CRYPTO_MAX_ALG_NAME);

	thread = kthread_run(cryptomgr_probe, param, "cryptomgr");
	if (IS_ERR(thread))
		goto err_free_param;

	return NOTIFY_STOP;

err_free_param:
	kfree(param);
err_put_module:
	module_put(THIS_MODULE);
err:
	return NOTIFY_OK;
}

static int cryptomgr_notify(struct notifier_block *this, unsigned long msg,
			    void *data)
{
	switch (msg) {
	case CRYPTO_MSG_ALG_REQUEST:
		return cryptomgr_schedule_probe(data);
	}

	return NOTIFY_DONE;
}

static struct notifier_block cryptomgr_notifier = {
	.notifier_call = cryptomgr_notify,
};

static int __init cryptomgr_init(void)
{
	return crypto_register_notifier(&cryptomgr_notifier);
}

static void __exit cryptomgr_exit(void)
{
	int err = crypto_unregister_notifier(&cryptomgr_notifier);
	BUG_ON(err);
}

module_init(cryptomgr_init);
module_exit(cryptomgr_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Crypto Algorithm Manager");
