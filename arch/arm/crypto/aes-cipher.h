/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef ARM_CRYPTO_AES_CIPHER_H
#define ARM_CRYPTO_AES_CIPHER_H

#include <linux/linkage.h>
#include <linux/types.h>

asmlinkage void __aes_arm_encrypt(const u32 rk[], int rounds,
				  const u8 *in, u8 *out);
asmlinkage void __aes_arm_decrypt(const u32 rk[], int rounds,
				  const u8 *in, u8 *out);

#endif /* ARM_CRYPTO_AES_CIPHER_H */
