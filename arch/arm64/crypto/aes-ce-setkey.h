/* SPDX-License-Identifier: GPL-2.0 */

int ce_aes_setkey(struct crypto_tfm *tfm, const u8 *in_key,
		  unsigned int key_len);
int ce_aes_expandkey(struct crypto_aes_ctx *ctx, const u8 *in_key,
		     unsigned int key_len);
