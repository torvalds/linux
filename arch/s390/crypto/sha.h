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

#include <crypto/hash.h>
#include <crypto/sha2.h>
#include <crypto/sha3.h>
#include <linux/build_bug.h>
#include <linux/types.h>

/* must be big enough for the largest SHA variant */
#define CPACF_MAX_PARMBLOCK_SIZE	SHA3_STATE_SIZE
#define SHA_MAX_BLOCK_SIZE		SHA3_224_BLOCK_SIZE

struct s390_sha_ctx {
	u64 count;		/* message length in bytes */
	union {
		u32 state[CPACF_MAX_PARMBLOCK_SIZE / sizeof(u32)];
		struct {
			u64 state[SHA512_DIGEST_SIZE / sizeof(u64)];
			u64 count_hi;
		} sha512;
		struct {
			__le64 state[SHA3_STATE_SIZE / sizeof(u64)];
		} sha3;
	};
	int func;		/* KIMD function to use */
	bool first_message_part;
};

struct shash_desc;

int s390_sha_update_blocks(struct shash_desc *desc, const u8 *data,
			   unsigned int len);
int s390_sha_finup(struct shash_desc *desc, const u8 *src, unsigned int len,
		   u8 *out);

static inline void __check_s390_sha_ctx_size(void)
{
	BUILD_BUG_ON(S390_SHA_CTX_SIZE != sizeof(struct s390_sha_ctx));
}

#endif
