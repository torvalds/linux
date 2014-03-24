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

#define SKEIN_ERR_CHECK 1
#include <skeinApi.h>
#include <string.h>
#include <stdio.h>

int skeinCtxPrepare(SkeinCtx_t* ctx, SkeinSize_t size)
{
    Skein_Assert(ctx && size, SKEIN_FAIL);

    memset(ctx ,0, sizeof(SkeinCtx_t));
    ctx->skeinSize = size;

    return SKEIN_SUCCESS;
}

int skeinInit(SkeinCtx_t* ctx, size_t hashBitLen)
{
    int ret = SKEIN_FAIL;
    size_t Xlen = 0;
    u64b_t*  X = NULL;
    uint64_t treeInfo = SKEIN_CFG_TREE_INFO_SEQUENTIAL;

    Skein_Assert(ctx, SKEIN_FAIL);
    /*
     * The following two lines rely of the fact that the real Skein contexts are
     * a union in out context and thus have tha maximum memory available.
     * The beauty of C :-) .
     */
    X = ctx->m.s256.X;
    Xlen = ctx->skeinSize/8;
    /*
     * If size is the same and hash bit length is zero then reuse
     * the save chaining variables.
     */
    switch (ctx->skeinSize) {
    case Skein256:
        ret = Skein_256_InitExt(&ctx->m.s256, hashBitLen,
                                treeInfo, NULL, 0);
        break;
    case Skein512:
        ret = Skein_512_InitExt(&ctx->m.s512, hashBitLen,
                                treeInfo, NULL, 0);
        break;
    case Skein1024:
        ret = Skein1024_InitExt(&ctx->m.s1024, hashBitLen,
                                treeInfo, NULL, 0);
        break;
    }

    if (ret == SKEIN_SUCCESS) {
        /* Save chaining variables for this combination of size and hashBitLen */
        memcpy(ctx->XSave, X, Xlen);
    }
    return ret;
}

int skeinMacInit(SkeinCtx_t* ctx, const uint8_t *key, size_t keyLen,
                 size_t hashBitLen)
{
    int ret = SKEIN_FAIL;
    u64b_t*  X = NULL;
    size_t Xlen = 0;
    uint64_t treeInfo = SKEIN_CFG_TREE_INFO_SEQUENTIAL;

    Skein_Assert(ctx, SKEIN_FAIL);

    X = ctx->m.s256.X;
    Xlen = ctx->skeinSize/8;

    Skein_Assert(hashBitLen, SKEIN_BAD_HASHLEN);

    switch (ctx->skeinSize) {
    case Skein256:
        ret = Skein_256_InitExt(&ctx->m.s256, hashBitLen,
                                treeInfo,
                                (const u08b_t*)key, keyLen);

        break;
    case Skein512:
        ret = Skein_512_InitExt(&ctx->m.s512, hashBitLen,
                                treeInfo,
                                (const u08b_t*)key, keyLen);
        break;
    case Skein1024:
        ret = Skein1024_InitExt(&ctx->m.s1024, hashBitLen,
                                treeInfo,
                                (const u08b_t*)key, keyLen);

        break;
    }
    if (ret == SKEIN_SUCCESS) {
        /* Save chaining variables for this combination of key, keyLen, hashBitLen */
        memcpy(ctx->XSave, X, Xlen);
    }
    return ret;
}

void skeinReset(SkeinCtx_t* ctx)
{
    size_t Xlen = 0;
    u64b_t*  X = NULL;

    /*
     * The following two lines rely of the fact that the real Skein contexts are
     * a union in out context and thus have tha maximum memory available.
     * The beautiy of C :-) .
     */
    X = ctx->m.s256.X;
    Xlen = ctx->skeinSize/8;
    /* Restore the chaing variable, reset byte counter */
    memcpy(X, ctx->XSave, Xlen);

    /* Setup context to process the message */
    Skein_Start_New_Type(&ctx->m, MSG);
}

int skeinUpdate(SkeinCtx_t *ctx, const uint8_t *msg,
                size_t msgByteCnt)
{
    int ret = SKEIN_FAIL;
    Skein_Assert(ctx, SKEIN_FAIL);

    switch (ctx->skeinSize) {
    case Skein256:
        ret = Skein_256_Update(&ctx->m.s256, (const u08b_t*)msg, msgByteCnt);
        break;
    case Skein512:
        ret = Skein_512_Update(&ctx->m.s512, (const u08b_t*)msg, msgByteCnt);
        break;
    case Skein1024:
        ret = Skein1024_Update(&ctx->m.s1024, (const u08b_t*)msg, msgByteCnt);
        break;
    }
    return ret;

}

int skeinUpdateBits(SkeinCtx_t *ctx, const uint8_t *msg,
                    size_t msgBitCnt)
{
    /*
     * I've used the bit pad implementation from skein_test.c (see NIST CD)
     * and modified it to use the convenience functions and added some pointer
     * arithmetic.
     */
    size_t length;
    uint8_t mask;
    uint8_t* up;

    /* only the final Update() call is allowed do partial bytes, else assert an error */
    Skein_Assert((ctx->m.h.T[1] & SKEIN_T1_FLAG_BIT_PAD) == 0 || msgBitCnt == 0, SKEIN_FAIL);

    /* if number of bits is a multiple of bytes - that's easy */
    if ((msgBitCnt & 0x7) == 0) {
        return skeinUpdate(ctx, msg, msgBitCnt >> 3);
    }
    skeinUpdate(ctx, msg, (msgBitCnt >> 3) + 1);

    /*
     * The next line rely on the fact that the real Skein contexts
     * are a union in our context. After the addition the pointer points to
     * Skein's real partial block buffer.
     * If this layout ever changes we have to adapt this as well.
     */
    up = (uint8_t*)ctx->m.s256.X + ctx->skeinSize / 8;

    Skein_Set_Bit_Pad_Flag(ctx->m.h);                       /* set tweak flag for the skeinFinal call */

    /* now "pad" the final partial byte the way NIST likes */
    length = ctx->m.h.bCnt;                                 /* get the bCnt value (same location for all block sizes) */
    Skein_assert(length != 0);                              /* internal sanity check: there IS a partial byte in the buffer! */
    mask = (uint8_t) (1u << (7 - (msgBitCnt & 7)));         /* partial byte bit mask */
    up[length-1]  = (uint8_t)((up[length-1] & (0-mask))|mask);   /* apply bit padding on final byte (in the buffer) */

    return SKEIN_SUCCESS;
}

int skeinFinal(SkeinCtx_t* ctx, uint8_t* hash)
{
    int ret = SKEIN_FAIL;
    Skein_Assert(ctx, SKEIN_FAIL);

    switch (ctx->skeinSize) {
    case Skein256:
        ret = Skein_256_Final(&ctx->m.s256, (u08b_t*)hash);
        break;
    case Skein512:
        ret = Skein_512_Final(&ctx->m.s512, (u08b_t*)hash);
        break;
    case Skein1024:
        ret = Skein1024_Final(&ctx->m.s1024, (u08b_t*)hash);
        break;
    }
    return ret;
}
