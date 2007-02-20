/*
 * Cryptographic API.
 *
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 * Copyright (c) 2005 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.
 *
 */
#ifndef _CRYPTO_INTERNAL_H
#define _CRYPTO_INTERNAL_H

#include <crypto/algapi.h>
#include <linux/completion.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/notifier.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <asm/kmap_types.h>

/* Crypto notification events. */
enum {
	CRYPTO_MSG_ALG_REQUEST,
	CRYPTO_MSG_ALG_REGISTER,
	CRYPTO_MSG_ALG_UNREGISTER,
	CRYPTO_MSG_TMPL_REGISTER,
	CRYPTO_MSG_TMPL_UNREGISTER,
};

struct crypto_instance;
struct crypto_template;

struct crypto_larval {
	struct crypto_alg alg;
	struct crypto_alg *adult;
	struct completion completion;
	u32 mask;
};

extern struct list_head crypto_alg_list;
extern struct rw_semaphore crypto_alg_sem;
extern struct blocking_notifier_head crypto_chain;

extern enum km_type crypto_km_types[];

static inline enum km_type crypto_kmap_type(int out)
{
	return crypto_km_types[(in_softirq() ? 2 : 0) + out];
}

static inline void *crypto_kmap(struct page *page, int out)
{
	return kmap_atomic(page, crypto_kmap_type(out));
}

static inline void crypto_kunmap(void *vaddr, int out)
{
	kunmap_atomic(vaddr, crypto_kmap_type(out));
}

static inline void crypto_yield(u32 flags)
{
	if (flags & CRYPTO_TFM_REQ_MAY_SLEEP)
		cond_resched();
}

#ifdef CONFIG_PROC_FS
void __init crypto_init_proc(void);
void __exit crypto_exit_proc(void);
#else
static inline void crypto_init_proc(void)
{ }
static inline void crypto_exit_proc(void)
{ }
#endif

static inline unsigned int crypto_digest_ctxsize(struct crypto_alg *alg)
{
	unsigned int len = alg->cra_ctxsize;

	if (alg->cra_alignmask) {
		len = ALIGN(len, (unsigned long)alg->cra_alignmask + 1);
		len += alg->cra_digest.dia_digestsize;
	}

	return len;
}

static inline unsigned int crypto_cipher_ctxsize(struct crypto_alg *alg)
{
	return alg->cra_ctxsize;
}

static inline unsigned int crypto_compress_ctxsize(struct crypto_alg *alg)
{
	return alg->cra_ctxsize;
}

struct crypto_alg *crypto_mod_get(struct crypto_alg *alg);
struct crypto_alg *__crypto_alg_lookup(const char *name, u32 type, u32 mask);
struct crypto_alg *crypto_alg_mod_lookup(const char *name, u32 type, u32 mask);

int crypto_init_digest_ops(struct crypto_tfm *tfm);
int crypto_init_cipher_ops(struct crypto_tfm *tfm);
int crypto_init_compress_ops(struct crypto_tfm *tfm);

void crypto_exit_digest_ops(struct crypto_tfm *tfm);
void crypto_exit_cipher_ops(struct crypto_tfm *tfm);
void crypto_exit_compress_ops(struct crypto_tfm *tfm);

void crypto_larval_error(const char *name, u32 type, u32 mask);

void crypto_shoot_alg(struct crypto_alg *alg);
struct crypto_tfm *__crypto_alloc_tfm(struct crypto_alg *alg, u32 type,
				      u32 mask);

int crypto_register_instance(struct crypto_template *tmpl,
			     struct crypto_instance *inst);

int crypto_register_notifier(struct notifier_block *nb);
int crypto_unregister_notifier(struct notifier_block *nb);

static inline void crypto_alg_put(struct crypto_alg *alg)
{
	if (atomic_dec_and_test(&alg->cra_refcnt) && alg->cra_destroy)
		alg->cra_destroy(alg);
}

static inline int crypto_tmpl_get(struct crypto_template *tmpl)
{
	return try_module_get(tmpl->module);
}

static inline void crypto_tmpl_put(struct crypto_template *tmpl)
{
	module_put(tmpl->module);
}

static inline int crypto_is_larval(struct crypto_alg *alg)
{
	return alg->cra_flags & CRYPTO_ALG_LARVAL;
}

static inline int crypto_is_dead(struct crypto_alg *alg)
{
	return alg->cra_flags & CRYPTO_ALG_DEAD;
}

static inline int crypto_is_moribund(struct crypto_alg *alg)
{
	return alg->cra_flags & (CRYPTO_ALG_DEAD | CRYPTO_ALG_DYING);
}

static inline int crypto_notify(unsigned long val, void *v)
{
	return blocking_notifier_call_chain(&crypto_chain, val, v);
}

#endif	/* _CRYPTO_INTERNAL_H */

