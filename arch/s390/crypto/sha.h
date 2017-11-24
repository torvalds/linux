/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Cryptographic API.
 *
 * s390 generic implementation of the SHA Secure Hash Algorithms.
 *
 * Copyright IBM Corp. 2007
 * Author(s): Jan Glauber (jang@de.ibm.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */
#ifndef _CRYPTO_ARCH_S390_SHA_H
#define _CRYPTO_ARCH_S390_SHA_H

#include <linux/crypto.h>
#include <crypto/sha.h>

/* must be big enough for the largest SHA variant */
#define SHA_MAX_STATE_SIZE	(SHA512_DIGEST_SIZE / 4)
#define SHA_MAX_BLOCK_SIZE      SHA512_BLOCK_SIZE

struct s390_sha_ctx {
	u64 count;              /* message length in bytes */
	u32 state[SHA_MAX_STATE_SIZE];
	u8 buf[2 * SHA_MAX_BLOCK_SIZE];
	int func;		/* KIMD function to use */
};

struct shash_desc;

int s390_sha_update(struct shash_desc *desc, const u8 *data, unsigned int len);
int s390_sha_final(struct shash_desc *desc, u8 *out);

#endif
