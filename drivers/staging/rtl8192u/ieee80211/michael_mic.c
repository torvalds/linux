/*
 * Cryptographic API
 *
 * Michael MIC (IEEE 802.11i/TKIP) keyed digest
 *
 * Copyright (c) 2004 Jouni Malinen <jkmaline@cc.hut.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>
//#include <linux/crypto.h>
#include "rtl_crypto.h"


struct michael_mic_ctx {
	u8 pending[4];
	size_t pending_len;

	u32 l, r;
};


static inline u32 rotl(u32 val, int bits)
{
	return (val << bits) | (val >> (32 - bits));
}


static inline u32 rotr(u32 val, int bits)
{
	return (val >> bits) | (val << (32 - bits));
}


static inline u32 xswap(u32 val)
{
	return ((val & 0x00ff00ff) << 8) | ((val & 0xff00ff00) >> 8);
}


#define michael_block(l, r)	\
do {				\
	r ^= rotl(l, 17);	\
	l += r;			\
	r ^= xswap(l);		\
	l += r;			\
	r ^= rotl(l, 3);	\
	l += r;			\
	r ^= rotr(l, 2);	\
	l += r;			\
} while (0)


static inline u32 get_le32(const u8 *p)
{
	return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}


static inline void put_le32(u8 *p, u32 v)
{
	p[0] = v;
	p[1] = v >> 8;
	p[2] = v >> 16;
	p[3] = v >> 24;
}


static void michael_init(void *ctx)
{
	struct michael_mic_ctx *mctx = ctx;
	mctx->pending_len = 0;
}


static void michael_update(void *ctx, const u8 *data, unsigned int len)
{
	struct michael_mic_ctx *mctx = ctx;

	if (mctx->pending_len) {
		int flen = 4 - mctx->pending_len;
		if (flen > len)
			flen = len;
		memcpy(&mctx->pending[mctx->pending_len], data, flen);
		mctx->pending_len += flen;
		data += flen;
		len -= flen;

		if (mctx->pending_len < 4)
			return;

		mctx->l ^= get_le32(mctx->pending);
		michael_block(mctx->l, mctx->r);
		mctx->pending_len = 0;
	}

	while (len >= 4) {
		mctx->l ^= get_le32(data);
		michael_block(mctx->l, mctx->r);
		data += 4;
		len -= 4;
	}

	if (len > 0) {
		mctx->pending_len = len;
		memcpy(mctx->pending, data, len);
	}
}


static void michael_final(void *ctx, u8 *out)
{
	struct michael_mic_ctx *mctx = ctx;
	u8 *data = mctx->pending;

	/* Last block and padding (0x5a, 4..7 x 0) */
	switch (mctx->pending_len) {
	case 0:
		mctx->l ^= 0x5a;
		break;
	case 1:
		mctx->l ^= data[0] | 0x5a00;
		break;
	case 2:
		mctx->l ^= data[0] | (data[1] << 8) | 0x5a0000;
		break;
	case 3:
		mctx->l ^= data[0] | (data[1] << 8) | (data[2] << 16) |
			0x5a000000;
		break;
	}
	michael_block(mctx->l, mctx->r);
	/* l ^= 0; */
	michael_block(mctx->l, mctx->r);

	put_le32(out, mctx->l);
	put_le32(out + 4, mctx->r);
}


static int michael_setkey(void *ctx, const u8 *key, unsigned int keylen,
			  u32 *flags)
{
	struct michael_mic_ctx *mctx = ctx;
	if (keylen != 8) {
		if (flags)
			*flags = CRYPTO_TFM_RES_BAD_KEY_LEN;
		return -EINVAL;
	}
	mctx->l = get_le32(key);
	mctx->r = get_le32(key + 4);
	return 0;
}


static struct crypto_alg michael_mic_alg = {
	.cra_name	= "michael_mic",
	.cra_flags	= CRYPTO_ALG_TYPE_DIGEST,
	.cra_blocksize	= 8,
	.cra_ctxsize	= sizeof(struct michael_mic_ctx),
	.cra_module	= THIS_MODULE,
	.cra_list	= LIST_HEAD_INIT(michael_mic_alg.cra_list),
	.cra_u		= { .digest = {
	.dia_digestsize	= 8,
	.dia_init	= michael_init,
	.dia_update	= michael_update,
	.dia_final	= michael_final,
	.dia_setkey	= michael_setkey } }
};


static int __init michael_mic_init(void)
{
	return crypto_register_alg(&michael_mic_alg);
}


static void __exit michael_mic_exit(void)
{
	crypto_unregister_alg(&michael_mic_alg);
}


module_init(michael_mic_init);
module_exit(michael_mic_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Michael MIC");
MODULE_AUTHOR("Jouni Malinen <jkmaline@cc.hut.fi>");
