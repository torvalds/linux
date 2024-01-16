/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _CRYPTO_SHA256_GLUE_H
#define _CRYPTO_SHA256_GLUE_H

#include <linux/crypto.h>

extern struct shash_alg sha256_neon_algs[2];

int crypto_sha256_arm_update(struct shash_desc *desc, const u8 *data,
			     unsigned int len);

int crypto_sha256_arm_finup(struct shash_desc *desc, const u8 *data,
			    unsigned int len, u8 *hash);

#endif /* _CRYPTO_SHA256_GLUE_H */
