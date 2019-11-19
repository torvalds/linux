/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Cryptographic API.
 *
 * s390 generic implementation of the SHA Secure Hash Algorithms.
 *
 * Copyright IBM Corp. 2007
 * Author(s): Jan Glauber (jang@de.ibm.com)
 */
#ifndef _CRYPTO_ARCH_S390_SHA_H
#define _CRYPTO_ARCH_S390_SHA_H

#include <linux/crypto.h>
#include <crypto/sha.h>
#include <crypto/sha3.h>

/* must be big enough for the largest SHA variant */
#define SHA3_STATE_SIZE			200
#define CPACF_MAX_PARMBLOCK_SIZE	SHA3_STATE_SIZE
#define SHA_MAX_BLOCK_SIZE		SHA3_224_BLOCK_SIZE

struct s390_sha_ctx {
	u64 count;		/* message length in bytes */
	u32 state[CPACF_MAX_PARMBLOCK_SIZE / sizeof(u32)];
	u8 buf[SHA_MAX_BLOCK_SIZE];
	int func;		/* KIMD function to use */
};

struct shash_desc;

int s390_sha_update(struct shash_desc *desc, const u8 *data, unsigned int len);
int s390_sha_final(struct shash_desc *desc, u8 *out);

#endif
