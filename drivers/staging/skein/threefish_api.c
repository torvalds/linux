// SPDX-License-Identifier: GPL-2.0
#include <linux/string.h>
#include "threefish_api.h"

void threefish_set_key(struct threefish_key *key_ctx,
		       enum threefish_size state_size,
		       u64 *key_data, u64 *tweak)
{
	int key_words = state_size / 64;
	int i;
	u64 parity = KEY_SCHEDULE_CONST;

	key_ctx->tweak[0] = tweak[0];
	key_ctx->tweak[1] = tweak[1];
	key_ctx->tweak[2] = tweak[0] ^ tweak[1];

	for (i = 0; i < key_words; i++) {
		key_ctx->key[i] = key_data[i];
		parity ^= key_data[i];
	}
	key_ctx->key[i] = parity;
	key_ctx->state_size = state_size;
}

void threefish_encrypt_block_bytes(struct threefish_key *key_ctx, u8 *in,
				   u8 *out)
{
	u64 plain[SKEIN_MAX_STATE_WORDS];        /* max number of words*/
	u64 cipher[SKEIN_MAX_STATE_WORDS];

	skein_get64_lsb_first(plain, in, key_ctx->state_size / 64);
	threefish_encrypt_block_words(key_ctx, plain, cipher);
	skein_put64_lsb_first(out, cipher, key_ctx->state_size / 8);
}

void threefish_encrypt_block_words(struct threefish_key *key_ctx, u64 *in,
				   u64 *out)
{
	switch (key_ctx->state_size) {
	case THREEFISH_256:
		threefish_encrypt_256(key_ctx, in, out);
		break;
	case THREEFISH_512:
		threefish_encrypt_512(key_ctx, in, out);
		break;
	case THREEFISH_1024:
		threefish_encrypt_1024(key_ctx, in, out);
		break;
	}
}

void threefish_decrypt_block_bytes(struct threefish_key *key_ctx, u8 *in,
				   u8 *out)
{
	u64 plain[SKEIN_MAX_STATE_WORDS];        /* max number of words*/
	u64 cipher[SKEIN_MAX_STATE_WORDS];

	skein_get64_lsb_first(cipher, in, key_ctx->state_size / 64);
	threefish_decrypt_block_words(key_ctx, cipher, plain);
	skein_put64_lsb_first(out, plain, key_ctx->state_size / 8);
}

void threefish_decrypt_block_words(struct threefish_key *key_ctx, u64 *in,
				   u64 *out)
{
	switch (key_ctx->state_size) {
	case THREEFISH_256:
		threefish_decrypt_256(key_ctx, in, out);
		break;
	case THREEFISH_512:
		threefish_decrypt_512(key_ctx, in, out);
		break;
	case THREEFISH_1024:
		threefish_decrypt_1024(key_ctx, in, out);
		break;
	}
}

