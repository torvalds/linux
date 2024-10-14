// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation of the memory encryption/decryption API.
 *
 * Since the low-level details of the operation depend on the
 * Confidential Computing environment (e.g. pKVM, CCA, ...), this just
 * acts as a top-level dispatcher to whatever hooks may have been
 * registered.
 *
 * Author: Will Deacon <will@kernel.org>
 * Copyright (C) 2024 Google LLC
 *
 * "Hello, boils and ghouls!"
 */

#include <linux/bug.h>
#include <linux/compiler.h>
#include <linux/err.h>
#include <linux/mm.h>

#include <asm/mem_encrypt.h>

static const struct arm64_mem_crypt_ops *crypt_ops;

int arm64_mem_crypt_ops_register(const struct arm64_mem_crypt_ops *ops)
{
	if (WARN_ON(crypt_ops))
		return -EBUSY;

	crypt_ops = ops;
	return 0;
}

int set_memory_encrypted(unsigned long addr, int numpages)
{
	if (likely(!crypt_ops) || WARN_ON(!PAGE_ALIGNED(addr)))
		return 0;

	return crypt_ops->encrypt(addr, numpages);
}
EXPORT_SYMBOL_GPL(set_memory_encrypted);

int set_memory_decrypted(unsigned long addr, int numpages)
{
	if (likely(!crypt_ops) || WARN_ON(!PAGE_ALIGNED(addr)))
		return 0;

	return crypt_ops->decrypt(addr, numpages);
}
EXPORT_SYMBOL_GPL(set_memory_decrypted);
