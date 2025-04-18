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

#include <crypto/sha3.h>
#include <linux/types.h>

/* must be big enough for the largest SHA variant */
#define CPACF_MAX_PARMBLOCK_SIZE	SHA3_STATE_SIZE
#define SHA_MAX_BLOCK_SIZE		SHA3_224_BLOCK_SIZE
#define S390_SHA_CTX_SIZE		offsetof(struct s390_sha_ctx, buf)

struct s390_sha_ctx {
	u64 count;		/* message length in bytes */
	u32 state[CPACF_MAX_PARMBLOCK_SIZE / sizeof(u32)];
	int func;		/* KIMD function to use */
	bool first_message_part;
	u8 buf[SHA_MAX_BLOCK_SIZE];
};

struct shash_desc;

int s390_sha_update(struct shash_desc *desc, const u8 *data, unsigned int len);
int s390_sha_update_blocks(struct shash_desc *desc, const u8 *data,
			   unsigned int len);
int s390_sha_final(struct shash_desc *desc, u8 *out);
int s390_sha_finup(struct shash_desc *desc, const u8 *src, unsigned int len,
		   u8 *out);

#endif
