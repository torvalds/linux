#ifndef ASM_ARM_CRYPTO_SHA1_H
#define ASM_ARM_CRYPTO_SHA1_H

#include <linux/crypto.h>
#include <crypto/sha.h>

extern int sha1_update_arm(struct shash_desc *desc, const u8 *data,
			   unsigned int len);

#endif
