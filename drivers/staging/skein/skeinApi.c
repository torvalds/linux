/*
Copyright (c) 2010 Werner Dittmann

Permission is hereby granted, free of charge, to any person
obtaining a copy of this software and associated documentation
files (the "Software"), to deal in the Software without
restriction, including without limitation the rights to use,
copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following
conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

*/

#include <linux/string.h>
#include <skeinApi.h>

int skein_ctx_prepare(struct skein_ctx *ctx, enum skein_size size)
{
	Skein_Assert(ctx && size, SKEIN_FAIL);

	memset(ctx , 0, sizeof(struct skein_ctx));
	ctx->skeinSize = size;

	return SKEIN_SUCCESS;
}

int skein_init(struct skein_ctx *ctx, size_t hashBitLen)
{
	int ret = SKEIN_FAIL;
	size_t Xlen = 0;
	u64 *X = NULL;
	u64 treeInfo = SKEIN_CFG_TREE_INFO_SEQUENTIAL;

	Skein_Assert(ctx, SKEIN_FAIL);
	/*
	 * The following two lines rely of the fact that the real Skein
	 * contexts are a union in out context and thus have tha maximum
	 * memory available.  The beauty of C :-) .
	 */
	X = ctx->m.s256.X;
	Xlen = ctx->skeinSize/8;
	/*
	 * If size is the same and hash bit length is zero then reuse
	 * the save chaining variables.
	 */
	switch (ctx->skeinSize) {
	case Skein256:
		ret = skein_256_init_ext(&ctx->m.s256, hashBitLen,
					 treeInfo, NULL, 0);
		break;
	case Skein512:
		ret = skein_512_init_ext(&ctx->m.s512, hashBitLen,
					 treeInfo, NULL, 0);
		break;
	case Skein1024:
		ret = skein_1024_init_ext(&ctx->m.s1024, hashBitLen,
					  treeInfo, NULL, 0);
		break;
	}

	if (ret == SKEIN_SUCCESS) {
		/*
		 * Save chaining variables for this combination of size and
		 * hashBitLen
		 */
		memcpy(ctx->XSave, X, Xlen);
	}
	return ret;
}

int skein_mac_init(struct skein_ctx *ctx, const u8 *key, size_t keyLen,
		   size_t hashBitLen)
{
	int ret = SKEIN_FAIL;
	u64 *X = NULL;
	size_t Xlen = 0;
	u64 treeInfo = SKEIN_CFG_TREE_INFO_SEQUENTIAL;

	Skein_Assert(ctx, SKEIN_FAIL);

	X = ctx->m.s256.X;
	Xlen = ctx->skeinSize/8;

	Skein_Assert(hashBitLen, SKEIN_BAD_HASHLEN);

	switch (ctx->skeinSize) {
	case Skein256:
		ret = skein_256_init_ext(&ctx->m.s256, hashBitLen,
					 treeInfo,
					 (const u8 *)key, keyLen);

		break;
	case Skein512:
		ret = skein_512_init_ext(&ctx->m.s512, hashBitLen,
					 treeInfo,
					 (const u8 *)key, keyLen);
		break;
	case Skein1024:
		ret = skein_1024_init_ext(&ctx->m.s1024, hashBitLen,
					  treeInfo,
					  (const u8 *)key, keyLen);

		break;
	}
	if (ret == SKEIN_SUCCESS) {
		/*
		 * Save chaining variables for this combination of key,
		 * keyLen, hashBitLen
		 */
		memcpy(ctx->XSave, X, Xlen);
	}
	return ret;
}

void skein_reset(struct skein_ctx *ctx)
{
	size_t Xlen = 0;
	u64 *X = NULL;

	/*
	 * The following two lines rely of the fact that the real Skein
	 * contexts are a union in out context and thus have tha maximum
	 * memory available.  The beautiy of C :-) .
	 */
	X = ctx->m.s256.X;
	Xlen = ctx->skeinSize/8;
	/* Restore the chaing variable, reset byte counter */
	memcpy(X, ctx->XSave, Xlen);

	/* Setup context to process the message */
	Skein_Start_New_Type(&ctx->m, MSG);
}

int skein_update(struct skein_ctx *ctx, const u8 *msg,
		 size_t msgByteCnt)
{
	int ret = SKEIN_FAIL;
	Skein_Assert(ctx, SKEIN_FAIL);

	switch (ctx->skeinSize) {
	case Skein256:
		ret = skein_256_update(&ctx->m.s256, (const u8 *)msg,
				       msgByteCnt);
		break;
	case Skein512:
		ret = skein_512_update(&ctx->m.s512, (const u8 *)msg,
				       msgByteCnt);
		break;
	case Skein1024:
		ret = skein_1024_update(&ctx->m.s1024, (const u8 *)msg,
					msgByteCnt);
		break;
	}
	return ret;

}

int skein_update_bits(struct skein_ctx *ctx, const u8 *msg,
		      size_t msgBitCnt)
{
	/*
	 * I've used the bit pad implementation from skein_test.c (see NIST CD)
	 * and modified it to use the convenience functions and added some
	 * pointer arithmetic.
	 */
	size_t length;
	u8 mask;
	u8 *up;

	/*
	 * only the final Update() call is allowed do partial bytes, else
	 * assert an error
	 */
	Skein_Assert((ctx->m.h.T[1] & SKEIN_T1_FLAG_BIT_PAD) == 0 ||
			msgBitCnt == 0, SKEIN_FAIL);

	/* if number of bits is a multiple of bytes - that's easy */
	if ((msgBitCnt & 0x7) == 0)
		return skein_update(ctx, msg, msgBitCnt >> 3);

	skein_update(ctx, msg, (msgBitCnt >> 3) + 1);

	/*
	 * The next line rely on the fact that the real Skein contexts
	 * are a union in our context. After the addition the pointer points to
	 * Skein's real partial block buffer.
	 * If this layout ever changes we have to adapt this as well.
	 */
	up = (u8 *)ctx->m.s256.X + ctx->skeinSize / 8;

	/* set tweak flag for the skein_final call */
	Skein_Set_Bit_Pad_Flag(ctx->m.h);

	/* now "pad" the final partial byte the way NIST likes */
	/* get the bCnt value (same location for all block sizes) */
	length = ctx->m.h.bCnt;
	/* internal sanity check: there IS a partial byte in the buffer! */
	Skein_assert(length != 0);
	/* partial byte bit mask */
	mask = (u8) (1u << (7 - (msgBitCnt & 7)));
	/* apply bit padding on final byte (in the buffer) */
	up[length-1]  = (u8)((up[length-1] & (0-mask))|mask);

	return SKEIN_SUCCESS;
}

int skein_final(struct skein_ctx *ctx, u8 *hash)
{
	int ret = SKEIN_FAIL;
	Skein_Assert(ctx, SKEIN_FAIL);

	switch (ctx->skeinSize) {
	case Skein256:
		ret = skein_256_final(&ctx->m.s256, (u8 *)hash);
		break;
	case Skein512:
		ret = skein_512_final(&ctx->m.s512, (u8 *)hash);
		break;
	case Skein1024:
		ret = skein_1024_final(&ctx->m.s1024, (u8 *)hash);
		break;
	}
	return ret;
}
