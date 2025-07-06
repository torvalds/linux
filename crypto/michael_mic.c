// SPDX-License-Identifier: GPL-2.0-only
/*
 * Cryptographic API
 *
 * Michael MIC (IEEE 802.11i/TKIP) keyed digest
 *
 * Copyright (c) 2004 Jouni Malinen <j@w1.fi>
 */
#include <crypto/internal/hash.h>
#include <linux/unaligned.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/types.h>


struct michael_mic_ctx {
	u32 l, r;
};

struct michael_mic_desc_ctx {
	__le32 pending;
	size_t pending_len;

	u32 l, r;
};

static inline u32 xswap(u32 val)
{
	return ((val & 0x00ff00ff) << 8) | ((val & 0xff00ff00) >> 8);
}


#define michael_block(l, r)	\
do {				\
	r ^= rol32(l, 17);	\
	l += r;			\
	r ^= xswap(l);		\
	l += r;			\
	r ^= rol32(l, 3);	\
	l += r;			\
	r ^= ror32(l, 2);	\
	l += r;			\
} while (0)


static int michael_init(struct shash_desc *desc)
{
	struct michael_mic_desc_ctx *mctx = shash_desc_ctx(desc);
	struct michael_mic_ctx *ctx = crypto_shash_ctx(desc->tfm);
	mctx->pending_len = 0;
	mctx->l = ctx->l;
	mctx->r = ctx->r;

	return 0;
}


static int michael_update(struct shash_desc *desc, const u8 *data,
			   unsigned int len)
{
	struct michael_mic_desc_ctx *mctx = shash_desc_ctx(desc);

	if (mctx->pending_len) {
		int flen = 4 - mctx->pending_len;
		if (flen > len)
			flen = len;
		memcpy((u8 *)&mctx->pending + mctx->pending_len, data, flen);
		mctx->pending_len += flen;
		data += flen;
		len -= flen;

		if (mctx->pending_len < 4)
			return 0;

		mctx->l ^= le32_to_cpu(mctx->pending);
		michael_block(mctx->l, mctx->r);
		mctx->pending_len = 0;
	}

	while (len >= 4) {
		mctx->l ^= get_unaligned_le32(data);
		michael_block(mctx->l, mctx->r);
		data += 4;
		len -= 4;
	}

	if (len > 0) {
		mctx->pending_len = len;
		memcpy(&mctx->pending, data, len);
	}

	return 0;
}


static int michael_final(struct shash_desc *desc, u8 *out)
{
	struct michael_mic_desc_ctx *mctx = shash_desc_ctx(desc);
	u8 *data = (u8 *)&mctx->pending;

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

	put_unaligned_le32(mctx->l, out);
	put_unaligned_le32(mctx->r, out + 4);

	return 0;
}


static int michael_setkey(struct crypto_shash *tfm, const u8 *key,
			  unsigned int keylen)
{
	struct michael_mic_ctx *mctx = crypto_shash_ctx(tfm);

	if (keylen != 8)
		return -EINVAL;

	mctx->l = get_unaligned_le32(key);
	mctx->r = get_unaligned_le32(key + 4);
	return 0;
}

static struct shash_alg alg = {
	.digestsize		=	8,
	.setkey			=	michael_setkey,
	.init			=	michael_init,
	.update			=	michael_update,
	.final			=	michael_final,
	.descsize		=	sizeof(struct michael_mic_desc_ctx),
	.base			=	{
		.cra_name		=	"michael_mic",
		.cra_driver_name	=	"michael_mic-generic",
		.cra_blocksize		=	8,
		.cra_ctxsize		=	sizeof(struct michael_mic_ctx),
		.cra_module		=	THIS_MODULE,
	}
};

static int __init michael_mic_init(void)
{
	return crypto_register_shash(&alg);
}


static void __exit michael_mic_exit(void)
{
	crypto_unregister_shash(&alg);
}


module_init(michael_mic_init);
module_exit(michael_mic_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Michael MIC");
MODULE_AUTHOR("Jouni Malinen <j@w1.fi>");
MODULE_ALIAS_CRYPTO("michael_mic");
