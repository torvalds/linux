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
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/rtnetlink.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/workqueue.h>

#include "internal.h"

struct cryptomgr_param {
	struct work_struct work;

	struct {
		struct rtattr attr;
		struct crypto_attr_alg data;
	} alg;

	struct {
		u32 type;
		u32 mask;
		char name[CRYPTO_MAX_ALG_NAME];
	} larval;

	char template[CRYPTO_MAX_ALG_NAME];
};

static void cryptomgr_probe(struct work_struct *work)
{
	struct cryptomgr_param *param =
		container_of(work, struct cryptomgr_param, work);
	struct crypto_template *tmpl;
	struct crypto_instance *inst;
	int err;

	tmpl = crypto_lookup_template(param->template);
	if (!tmpl)
		goto err;

	do {
		inst = tmpl->alloc(&param->alg, sizeof(param->alg));
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
	return;

err:
	crypto_larval_error(param->larval.name, param->larval.type,
			    param->larval.mask);
	goto out;
}

static int cryptomgr_schedule_probe(struct crypto_larval *larval)
{
	struct cryptomgr_param *param;
	const char *name = larval->alg.cra_name;
	const char *p;
	unsigned int len;

	param = kmalloc(sizeof(*param), GFP_KERNEL);
	if (!param)
		goto err;

	for (p = name; isalnum(*p) || *p == '-' || *p == '_'; p++)
		;

	len = p - name;
	if (!len || *p != '(')
		goto err_free_param;

	memcpy(param->template, name, len);
	param->template[len] = 0;

	name = p + 1;
	for (p = name; isalnum(*p) || *p == '-' || *p == '_'; p++)
		;

	len = p - name;
	if (!len || *p != ')' || p[1])
		goto err_free_param;

	param->alg.attr.rta_len = sizeof(param->alg);
	param->alg.attr.rta_type = CRYPTOA_ALG;
	memcpy(param->alg.data.name, name, len);
	param->alg.data.name[len] = 0;

	memcpy(param->larval.name, larval->alg.cra_name, CRYPTO_MAX_ALG_NAME);
	param->larval.type = larval->alg.cra_flags;
	param->larval.mask = larval->mask;

	INIT_WORK(&param->work, cryptomgr_probe);
	schedule_work(&param->work);

	return NOTIFY_STOP;

err_free_param:
	kfree(param);
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
