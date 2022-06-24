/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2021 StarFive, Inc <huan.feng@starfivetech.com>
 */
#ifndef __JH7110_STR_H__
#define __JH7110_STR_H__

#include <crypto/internal/akcipher.h>
#include <crypto/internal/rsa.h>
#include <crypto/engine.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/delay.h>

#include "jh7110-regs.h"

#define JH7110_MSG_BUFFER_SIZE			(16 * 1024)
#define MAX_KEY_SIZE				SHA512_BLOCK_SIZE

#define JH7110_AES_IV_LEN			AES_BLOCK_SIZE
#define JH7110_AES_CTR_LEN			AES_BLOCK_SIZE


struct jh7110_rsa_key {
	u8					*n;
	u8					*e;
	u8					*d;
	u8					*p;
	u8					*q;
	u8					*dp;
	u8					*dq;
	u8					*qinv;
	u8					*rinv;
	u8					*rinv_p;
	u8					*rinv_q;
	u8					*mp;
	u8					*rsqr;
	u8					*rsqr_p;
	u8					*rsqr_q;
	u8					*pmp;
	u8					*qmp;
	int					e_bitlen;
	int					d_bitlen;
	int					bitlen;
	size_t					key_sz;
	bool					crt_mode;
};

struct jh7110_sec_ctx {
	struct crypto_engine_ctx		enginectx;

	struct jh7110_sec_request_ctx		*rctx;
	struct jh7110_sec_dev			*sdev;

	unsigned int				sha_mode;

	u8					key[MAX_KEY_SIZE];
	int					keylen;
	int					sec_init;
	struct scatterlist			sg[2];
	struct jh7110_rsa_key			rsa_key;
	size_t					sha_len_total;
	u8					*buffer;
};

struct jh7110_sec_dev {
	struct list_head			list;
	struct device				*dev;

	struct clk				*sec_hclk;
	struct clk				*sec_ahb;
	struct reset_control			*rst_hresetn;

	struct jh7110_pl08x_device		*pl080;

	void __iomem				*io_base;
	void __iomem				*dma_base;
	phys_addr_t				io_phys_base;
	void					*sha_data;
	void					*aes_data;
	void					*des_data;
	void					*pka_data;
	unsigned int				secirq;
	unsigned int				irq;

	size_t					data_buf_len;
	int					pages_count;
	u32					use_dma;
	u32					dma_maxburst;
	struct dma_chan				*sec_xm_m;
	struct dma_chan				*sec_xm_p;
	struct dma_slave_config			cfg_in;
	struct dma_slave_config			cfg_out;
	struct completion			sec_comp_m;
	struct completion			sec_comp_p;
	struct scatterlist			in_sg;
	struct scatterlist			out_sg;
	unsigned long				in_sg_len;
	unsigned long				out_sg_len;


	struct mutex				doing;
	struct mutex				pl080_doing;
	struct mutex				lock; /* protects req / areq */
	struct mutex				sha_lock;
	struct mutex				des_lock;
	struct mutex				aes_lock;
	struct mutex				rsa_lock;

#define JH7110_SHA_HMAC_DONE			BIT(1)
#define JH7110_SHA_SHA_DONE			BIT(2)
#define JH7110_AES_DONE				BIT(3)
#define JH7110_DES_DONE				BIT(4)
#define JH7110_PKA_DONE				BIT(5)
	u32					done_flags;
#define JH7110_SHA_TYPE				0x1
#define JH7110_AES_TYPE				0x2
#define JH7110_DES_TYPE				0x3
#define JH7110_PKA_TYPE				0x4
	u32					cry_type;

	struct crypto_engine			*engine;

	union jh7110_alg_cr			alg_cr;
	union jh7110_ie_mask			ie_mask;
	union jh7110_ie_flag			ie_flag;
};

struct jh7110_sec_request_ctx {
	struct jh7110_sec_ctx			*ctx;
	struct jh7110_sec_dev			*sdev;

	union {
		struct ahash_request		*hreq;
		struct skcipher_request		*sreq;
		struct aead_request		*areq;
	} req;

#define JH7110_AHASH_REQ			0
#define JH7110_ABLK_REQ				1
#define JH7110_AEAD_REQ				2
	unsigned int				req_type;

	union {
		union jh7110_crypto_cacr	pka_csr;
		union jh7110_des_daecsr		des_csr;
		union jh7110_aes_csr		aes_csr;
		union jh7110_sha_shacsr		sha_csr;
	} csr;

	struct scatterlist			*sg;
	struct scatterlist			*in_sg;
	struct scatterlist			*out_sg;
	struct scatterlist			in_sgl;
	struct scatterlist			out_sgl;

	unsigned long				sg_len;
	unsigned long				in_sg_len;
	unsigned long				out_sg_len;

	unsigned long				flags;
	unsigned long				op;
	unsigned long				stmode;
	unsigned long long			jiffies_hw;
	unsigned long long			jiffies_cp;

	size_t					bufcnt;
	size_t					buflen;
	size_t					total;
	size_t					offset;
	size_t					data_offset;
	size_t					authsize;
	size_t					total_in;
	size_t					total_out;
	size_t					assoclen;
	size_t					ctr_over_count;

	u32					msg_end[4];
	u32					dec_end[4];
	u32					last_ctr[4];
	u32					aes_nonce[4];
	u32					aes_iv[4];
	u8					sha_digest_mid[SHA512_DIGEST_SIZE]__aligned(sizeof(u32));
	unsigned int				sha_digest_len;
};

struct jh7110_sec_dma {
	struct dma_slave_config			cfg;
	union  jh7110_alg_cr			alg_cr;
	struct dma_chan				*chan;
	struct completion			*dma_comp;
	struct scatterlist			*sg;
	struct jh7110_sec_ctx			*ctx;
	void					*data;
	size_t					total;
};

static inline u64 jh7110_sec_readq(struct jh7110_sec_dev *sdev, u32 offset)
{
#ifdef CONFIG_64BIT
	return __raw_readq(sdev->io_base + offset);
#else
	return ((u64)__raw_readl(sdev->io_base + offset) << 32) | (u64)__raw_readl(sdev->io_base + offset + 4);
#endif
}

static inline u32 jh7110_sec_read(struct jh7110_sec_dev *sdev, u32 offset)
{
	return __raw_readl(sdev->io_base + offset);
}

static inline u16 jh7110_sec_readw(struct jh7110_sec_dev *sdev, u32 offset)
{
	return __raw_readw(sdev->io_base + offset);
}

static inline u8 jh7110_sec_readb(struct jh7110_sec_dev *sdev, u32 offset)
{
	return __raw_readb(sdev->io_base + offset);
}

static inline void jh7110_sec_writeq(struct jh7110_sec_dev *sdev,
					u32 offset, u64 value)
{
#ifdef CONFIG_64BIT
	__raw_writeq(value, sdev->io_base + offset);
#else
	__raw_writel((value >> 32), sdev->io_base + offset);
	__raw_writel(value & 0xffffffff, sdev->io_base + offset + 4);
#endif
}

static inline void jh7110_sec_write(struct jh7110_sec_dev *sdev,
					u32 offset, u32 value)
{
	__raw_writel(value, sdev->io_base + offset);
}

static inline void jh7110_sec_writew(struct jh7110_sec_dev *sdev,
					u32 offset, u16 value)
{
	__raw_writew(value, sdev->io_base + offset);
}

static inline void jh7110_sec_writeb(struct jh7110_sec_dev *sdev,
					u32 offset, u8 value)
{
	__raw_writeb(value, sdev->io_base + offset);
}

extern struct jh7110_sec_dev *jh7110_sec_find_dev(struct jh7110_sec_ctx *ctx);

extern int jh7110_hash_register_algs(void);
extern void jh7110_hash_unregister_algs(void);

extern int jh7110_aes_register_algs(void);
extern void jh7110_aes_unregister_algs(void);

extern int jh7110_des_register_algs(void);
extern void jh7110_des_unregister_algs(void);

extern int jh7110_pka_register_algs(void);
extern void jh7110_pka_unregister_algs(void);

extern int jh7110_dma_sg_to_device(struct jh7110_sec_dma *sdma);
extern int jh7110_dma_mem_to_device(struct jh7110_sec_dma *sdma);
extern int jh7110_dma_sg_from_device(struct jh7110_sec_dma *sdma);
extern int jh7110_dma_mem_from_device(struct jh7110_sec_dma *sdma);
extern int jh7110_mem_to_mem_test(struct jh7110_sec_ctx *ctx);

extern int jh7110_dmac_init(struct jh7110_sec_dev *sdev, int irq);
extern int jh7110_dmac_secdata_out(struct jh7110_sec_dev *sdev, u8 chan, u32 src, u32 dst, u32 size);
extern int jh7110_dmac_secdata_in(struct jh7110_sec_dev *sdev, u8 chan, u32 src, u32 dst, u32 size);
extern int jh7110_dmac_wait_done(struct jh7110_sec_dev *sdev, u8 chan);

#endif
