/*
 * Cryptographic API.
 *
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */
#ifndef _CRYPTO_INTERNAL_H
#define _CRYPTO_INTERNAL_H


//#include <linux/crypto.h>
#include "rtl_crypto.h"
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/init.h>
#include <asm/hardirq.h>
#include <asm/softirq.h>
#include <asm/kmap_types.h>


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

static inline void crypto_yield(struct crypto_tfm *tfm)
{
	if (!in_softirq())
		cond_resched();
}

static inline void *crypto_tfm_ctx(struct crypto_tfm *tfm)
{
	return (void *)&tfm[1];
}

struct crypto_alg *crypto_alg_lookup(const char *name);

#ifdef CONFIG_KMOD
void crypto_alg_autoload(const char *name);
struct crypto_alg *crypto_alg_mod_lookup(const char *name);
#else
static inline struct crypto_alg *crypto_alg_mod_lookup(const char *name)
{
	return crypto_alg_lookup(name);
}
#endif

#ifdef CONFIG_CRYPTO_HMAC
int crypto_alloc_hmac_block(struct crypto_tfm *tfm);
void crypto_free_hmac_block(struct crypto_tfm *tfm);
#else
static inline int crypto_alloc_hmac_block(struct crypto_tfm *tfm)
{
	return 0;
}

static inline void crypto_free_hmac_block(struct crypto_tfm *tfm)
{ }
#endif

#ifdef CONFIG_PROC_FS
void __init crypto_init_proc(void);
#else
static inline void crypto_init_proc(void)
{ }
#endif

int crypto_init_digest_flags(struct crypto_tfm *tfm, u32 flags);
int crypto_init_cipher_flags(struct crypto_tfm *tfm, u32 flags);
int crypto_init_compress_flags(struct crypto_tfm *tfm, u32 flags);

int crypto_init_digest_ops(struct crypto_tfm *tfm);
int crypto_init_cipher_ops(struct crypto_tfm *tfm);
int crypto_init_compress_ops(struct crypto_tfm *tfm);

void crypto_exit_digest_ops(struct crypto_tfm *tfm);
void crypto_exit_cipher_ops(struct crypto_tfm *tfm);
void crypto_exit_compress_ops(struct crypto_tfm *tfm);

#endif	/* _CRYPTO_INTERNAL_H */

