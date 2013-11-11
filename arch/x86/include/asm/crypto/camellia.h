#ifndef ASM_X86_CAMELLIA_H
#define ASM_X86_CAMELLIA_H

#include <linux/kernel.h>
#include <linux/crypto.h>

#define CAMELLIA_MIN_KEY_SIZE	16
#define CAMELLIA_MAX_KEY_SIZE	32
#define CAMELLIA_BLOCK_SIZE	16
#define CAMELLIA_TABLE_BYTE_LEN	272
#define CAMELLIA_PARALLEL_BLOCKS 2

struct camellia_ctx {
	u64 key_table[CAMELLIA_TABLE_BYTE_LEN / sizeof(u64)];
	u32 key_length;
};

struct camellia_lrw_ctx {
	struct lrw_table_ctx lrw_table;
	struct camellia_ctx camellia_ctx;
};

struct camellia_xts_ctx {
	struct camellia_ctx tweak_ctx;
	struct camellia_ctx crypt_ctx;
};

extern int __camellia_setkey(struct camellia_ctx *cctx,
			     const unsigned char *key,
			     unsigned int key_len, u32 *flags);

extern int lrw_camellia_setkey(struct crypto_tfm *tfm, const u8 *key,
			       unsigned int keylen);
extern void lrw_camellia_exit_tfm(struct crypto_tfm *tfm);

extern int xts_camellia_setkey(struct crypto_tfm *tfm, const u8 *key,
			       unsigned int keylen);

/* regular block cipher functions */
asmlinkage void __camellia_enc_blk(struct camellia_ctx *ctx, u8 *dst,
				   const u8 *src, bool xor);
asmlinkage void camellia_dec_blk(struct camellia_ctx *ctx, u8 *dst,
				 const u8 *src);

/* 2-way parallel cipher functions */
asmlinkage void __camellia_enc_blk_2way(struct camellia_ctx *ctx, u8 *dst,
					const u8 *src, bool xor);
asmlinkage void camellia_dec_blk_2way(struct camellia_ctx *ctx, u8 *dst,
				      const u8 *src);

/* 16-way parallel cipher functions (avx/aes-ni) */
asmlinkage void camellia_ecb_enc_16way(struct camellia_ctx *ctx, u8 *dst,
				       const u8 *src);
asmlinkage void camellia_ecb_dec_16way(struct camellia_ctx *ctx, u8 *dst,
				       const u8 *src);

asmlinkage void camellia_cbc_dec_16way(struct camellia_ctx *ctx, u8 *dst,
				       const u8 *src);
asmlinkage void camellia_ctr_16way(struct camellia_ctx *ctx, u8 *dst,
				   const u8 *src, le128 *iv);

asmlinkage void camellia_xts_enc_16way(struct camellia_ctx *ctx, u8 *dst,
				       const u8 *src, le128 *iv);
asmlinkage void camellia_xts_dec_16way(struct camellia_ctx *ctx, u8 *dst,
				       const u8 *src, le128 *iv);

static inline void camellia_enc_blk(struct camellia_ctx *ctx, u8 *dst,
				    const u8 *src)
{
	__camellia_enc_blk(ctx, dst, src, false);
}

static inline void camellia_enc_blk_xor(struct camellia_ctx *ctx, u8 *dst,
					const u8 *src)
{
	__camellia_enc_blk(ctx, dst, src, true);
}

static inline void camellia_enc_blk_2way(struct camellia_ctx *ctx, u8 *dst,
					 const u8 *src)
{
	__camellia_enc_blk_2way(ctx, dst, src, false);
}

static inline void camellia_enc_blk_xor_2way(struct camellia_ctx *ctx, u8 *dst,
					     const u8 *src)
{
	__camellia_enc_blk_2way(ctx, dst, src, true);
}

/* glue helpers */
extern void camellia_decrypt_cbc_2way(void *ctx, u128 *dst, const u128 *src);
extern void camellia_crypt_ctr(void *ctx, u128 *dst, const u128 *src,
			       le128 *iv);
extern void camellia_crypt_ctr_2way(void *ctx, u128 *dst, const u128 *src,
				    le128 *iv);

extern void camellia_xts_enc(void *ctx, u128 *dst, const u128 *src, le128 *iv);
extern void camellia_xts_dec(void *ctx, u128 *dst, const u128 *src, le128 *iv);

#endif /* ASM_X86_CAMELLIA_H */
