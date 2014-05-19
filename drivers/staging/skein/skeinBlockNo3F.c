
#include <linux/string.h>
#include <skein.h>
#include <threefishApi.h>


/*****************************  Skein_256 ******************************/
void skein_256_process_block(struct skein_256_ctx *ctx, const u8 *blk_ptr,
			     size_t blk_cnt, size_t byte_cnt_add)
{
	struct threefish_key key;
	u64 tweak[2];
	int i;
	u64 w[SKEIN_256_STATE_WORDS]; /* local copy of input block */
	u64 words[3];

	skein_assert(blk_cnt != 0); /* never call with blk_cnt == 0! */
	tweak[0] = ctx->h.T[0];
	tweak[1] = ctx->h.T[1];

	do  {
		u64 carry = byte_cnt_add;

		words[0] = tweak[0] & 0xffffffffL;
		words[1] = ((tweak[0] >> 32) & 0xffffffffL);
		words[2] = (tweak[1] & 0xffffffffL);

		for (i = 0; i < 3; i++) {
			carry += words[i];
			words[i] = carry;
			carry >>= 32;
		}
		tweak[0] = words[0] & 0xffffffffL;
		tweak[0] |= (words[1] & 0xffffffffL) << 32;
		tweak[1] |= words[2] & 0xffffffffL;

		threefish_set_key(&key, THREEFISH_256, ctx->X, tweak);

		/* get input block in little-endian format */
		skein_get64_lsb_first(w, blk_ptr, SKEIN_256_STATE_WORDS);

		threefish_encrypt_block_words(&key, w, ctx->X);

		blk_ptr += SKEIN_256_BLOCK_BYTES;

		/* do the final "feedforward" xor, update ctx chaining vars */
		ctx->X[0] = ctx->X[0] ^ w[0];
		ctx->X[1] = ctx->X[1] ^ w[1];
		ctx->X[2] = ctx->X[2] ^ w[2];
		ctx->X[3] = ctx->X[3] ^ w[3];

		tweak[1] &= ~SKEIN_T1_FLAG_FIRST;
	} while (--blk_cnt);

	ctx->h.T[0] = tweak[0];
	ctx->h.T[1] = tweak[1];
}

void skein_512_process_block(struct skein_512_ctx *ctx, const u8 *blk_ptr,
			     size_t blk_cnt, size_t byte_cnt_add)
{
	struct threefish_key key;
	u64 tweak[2];
	int i;
	u64 words[3];
	u64 w[SKEIN_512_STATE_WORDS]; /* local copy of input block */

	skein_assert(blk_cnt != 0); /* never call with blk_cnt == 0! */
	tweak[0] = ctx->h.T[0];
	tweak[1] = ctx->h.T[1];

	do  {
		u64 carry = byte_cnt_add;

		words[0] = tweak[0] & 0xffffffffL;
		words[1] = ((tweak[0] >> 32) & 0xffffffffL);
		words[2] = (tweak[1] & 0xffffffffL);

		for (i = 0; i < 3; i++) {
			carry += words[i];
			words[i] = carry;
			carry >>= 32;
		}
		tweak[0] = words[0] & 0xffffffffL;
		tweak[0] |= (words[1] & 0xffffffffL) << 32;
		tweak[1] |= words[2] & 0xffffffffL;

		threefish_set_key(&key, THREEFISH_512, ctx->X, tweak);

		/* get input block in little-endian format */
		skein_get64_lsb_first(w, blk_ptr, SKEIN_512_STATE_WORDS);

		threefish_encrypt_block_words(&key, w, ctx->X);

		blk_ptr += SKEIN_512_BLOCK_BYTES;

		/* do the final "feedforward" xor, update ctx chaining vars */
		ctx->X[0] = ctx->X[0] ^ w[0];
		ctx->X[1] = ctx->X[1] ^ w[1];
		ctx->X[2] = ctx->X[2] ^ w[2];
		ctx->X[3] = ctx->X[3] ^ w[3];
		ctx->X[4] = ctx->X[4] ^ w[4];
		ctx->X[5] = ctx->X[5] ^ w[5];
		ctx->X[6] = ctx->X[6] ^ w[6];
		ctx->X[7] = ctx->X[7] ^ w[7];

		tweak[1] &= ~SKEIN_T1_FLAG_FIRST;
	} while (--blk_cnt);

	ctx->h.T[0] = tweak[0];
	ctx->h.T[1] = tweak[1];
}

void skein_1024_process_block(struct skein_1024_ctx *ctx, const u8 *blk_ptr,
			      size_t blk_cnt, size_t byte_cnt_add)
{
	struct threefish_key key;
	u64 tweak[2];
	int i;
	u64 words[3];
	u64 w[SKEIN_1024_STATE_WORDS]; /* local copy of input block */

	skein_assert(blk_cnt != 0); /* never call with blk_cnt == 0! */
	tweak[0] = ctx->h.T[0];
	tweak[1] = ctx->h.T[1];

	do  {
		u64 carry = byte_cnt_add;

		words[0] = tweak[0] & 0xffffffffL;
		words[1] = ((tweak[0] >> 32) & 0xffffffffL);
		words[2] = (tweak[1] & 0xffffffffL);

		for (i = 0; i < 3; i++) {
			carry += words[i];
			words[i] = carry;
			carry >>= 32;
		}
		tweak[0] = words[0] & 0xffffffffL;
		tweak[0] |= (words[1] & 0xffffffffL) << 32;
		tweak[1] |= words[2] & 0xffffffffL;

		threefish_set_key(&key, THREEFISH_1024, ctx->X, tweak);

		/* get input block in little-endian format */
		skein_get64_lsb_first(w, blk_ptr, SKEIN_1024_STATE_WORDS);

		threefish_encrypt_block_words(&key, w, ctx->X);

		blk_ptr += SKEIN_1024_BLOCK_BYTES;

		/* do the final "feedforward" xor, update ctx chaining vars */
		ctx->X[0]  = ctx->X[0]  ^ w[0];
		ctx->X[1]  = ctx->X[1]  ^ w[1];
		ctx->X[2]  = ctx->X[2]  ^ w[2];
		ctx->X[3]  = ctx->X[3]  ^ w[3];
		ctx->X[4]  = ctx->X[4]  ^ w[4];
		ctx->X[5]  = ctx->X[5]  ^ w[5];
		ctx->X[6]  = ctx->X[6]  ^ w[6];
		ctx->X[7]  = ctx->X[7]  ^ w[7];
		ctx->X[8]  = ctx->X[8]  ^ w[8];
		ctx->X[9]  = ctx->X[9]  ^ w[9];
		ctx->X[10] = ctx->X[10] ^ w[10];
		ctx->X[11] = ctx->X[11] ^ w[11];
		ctx->X[12] = ctx->X[12] ^ w[12];
		ctx->X[13] = ctx->X[13] ^ w[13];
		ctx->X[14] = ctx->X[14] ^ w[14];
		ctx->X[15] = ctx->X[15] ^ w[15];

		tweak[1] &= ~SKEIN_T1_FLAG_FIRST;
	} while (--blk_cnt);

	ctx->h.T[0] = tweak[0];
	ctx->h.T[1] = tweak[1];
}
