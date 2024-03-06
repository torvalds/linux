// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef _AEGIS_NEON_H
#define _AEGIS_NEON_H

void crypto_aegis128_init_neon(void *state, const void *key, const void *iv);
void crypto_aegis128_update_neon(void *state, const void *msg);
void crypto_aegis128_encrypt_chunk_neon(void *state, void *dst, const void *src,
					unsigned int size);
void crypto_aegis128_decrypt_chunk_neon(void *state, void *dst, const void *src,
					unsigned int size);
int crypto_aegis128_final_neon(void *state, void *tag_xor,
			       unsigned int assoclen,
			       unsigned int cryptlen,
			       unsigned int authsize);

#endif
