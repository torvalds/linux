/*
 * Intel IXP4xx NPE-C crypto driver
 *
 * Copyright (C) 2008 Christian Hohnstaedt <chohnstaedt@innominate.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 *
 */

#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/crypto.h>
#include <linux/kernel.h>
#include <linux/rtnetlink.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/gfp.h>
#include <linux/module.h>

#include <crypto/ctr.h>
#include <crypto/des.h>
#include <crypto/aes.h>
#include <crypto/sha.h>
#include <crypto/algapi.h>
#include <crypto/aead.h>
#include <crypto/authenc.h>
#include <crypto/scatterwalk.h>

#include <mach/npe.h>
#include <mach/qmgr.h>

#define MAX_KEYLEN 32

/* hash: cfgword + 2 * digestlen; crypt: keylen + cfgword */
#define NPE_CTX_LEN 80
#define AES_BLOCK128 16

#define NPE_OP_HASH_VERIFY   0x01
#define NPE_OP_CCM_ENABLE    0x04
#define NPE_OP_CRYPT_ENABLE  0x08
#define NPE_OP_HASH_ENABLE   0x10
#define NPE_OP_NOT_IN_PLACE  0x20
#define NPE_OP_HMAC_DISABLE  0x40
#define NPE_OP_CRYPT_ENCRYPT 0x80

#define NPE_OP_CCM_GEN_MIC   0xcc
#define NPE_OP_HASH_GEN_ICV  0x50
#define NPE_OP_ENC_GEN_KEY   0xc9

#define MOD_ECB     0x0000
#define MOD_CTR     0x1000
#define MOD_CBC_ENC 0x2000
#define MOD_CBC_DEC 0x3000
#define MOD_CCM_ENC 0x4000
#define MOD_CCM_DEC 0x5000

#define KEYLEN_128  4
#define KEYLEN_192  6
#define KEYLEN_256  8

#define CIPH_DECR   0x0000
#define CIPH_ENCR   0x0400

#define MOD_DES     0x0000
#define MOD_TDEA2   0x0100
#define MOD_3DES   0x0200
#define MOD_AES     0x0800
#define MOD_AES128  (0x0800 | KEYLEN_128)
#define MOD_AES192  (0x0900 | KEYLEN_192)
#define MOD_AES256  (0x0a00 | KEYLEN_256)

#define MAX_IVLEN   16
#define NPE_ID      2  /* NPE C */
#define NPE_QLEN    16
/* Space for registering when the first
 * NPE_QLEN crypt_ctl are busy */
#define NPE_QLEN_TOTAL 64

#define SEND_QID    29
#define RECV_QID    30

#define CTL_FLAG_UNUSED		0x0000
#define CTL_FLAG_USED		0x1000
#define CTL_FLAG_PERFORM_ABLK	0x0001
#define CTL_FLAG_GEN_ICV	0x0002
#define CTL_FLAG_GEN_REVAES	0x0004
#define CTL_FLAG_PERFORM_AEAD	0x0008
#define CTL_FLAG_MASK		0x000f

#define HMAC_IPAD_VALUE   0x36
#define HMAC_OPAD_VALUE   0x5C
#define HMAC_PAD_BLOCKLEN SHA1_BLOCK_SIZE

#define MD5_DIGEST_SIZE   16

struct buffer_desc {
	u32 phys_next;
#ifdef __ARMEB__
	u16 buf_len;
	u16 pkt_len;
#else
	u16 pkt_len;
	u16 buf_len;
#endif
	u32 phys_addr;
	u32 __reserved[4];
	struct buffer_desc *next;
	enum dma_data_direction dir;
};

struct crypt_ctl {
#ifdef __ARMEB__
	u8 mode;		/* NPE_OP_*  operation mode */
	u8 init_len;
	u16 reserved;
#else
	u16 reserved;
	u8 init_len;
	u8 mode;		/* NPE_OP_*  operation mode */
#endif
	u8 iv[MAX_IVLEN];	/* IV for CBC mode or CTR IV for CTR mode */
	u32 icv_rev_aes;	/* icv or rev aes */
	u32 src_buf;
	u32 dst_buf;
#ifdef __ARMEB__
	u16 auth_offs;		/* Authentication start offset */
	u16 auth_len;		/* Authentication data length */
	u16 crypt_offs;		/* Cryption start offset */
	u16 crypt_len;		/* Cryption data length */
#else
	u16 auth_len;		/* Authentication data length */
	u16 auth_offs;		/* Authentication start offset */
	u16 crypt_len;		/* Cryption data length */
	u16 crypt_offs;		/* Cryption start offset */
#endif
	u32 aadAddr;		/* Additional Auth Data Addr for CCM mode */
	u32 crypto_ctx;		/* NPE Crypto Param structure address */

	/* Used by Host: 4*4 bytes*/
	unsigned ctl_flags;
	union {
		struct ablkcipher_request *ablk_req;
		struct aead_request *aead_req;
		struct crypto_tfm *tfm;
	} data;
	struct buffer_desc *regist_buf;
	u8 *regist_ptr;
};

struct ablk_ctx {
	struct buffer_desc *src;
	struct buffer_desc *dst;
};

struct aead_ctx {
	struct buffer_desc *buffer;
	struct scatterlist ivlist;
	/* used when the hmac is not on one sg entry */
	u8 *hmac_virt;
	int encrypt;
};

struct ix_hash_algo {
	u32 cfgword;
	unsigned char *icv;
};

struct ix_sa_dir {
	unsigned char *npe_ctx;
	dma_addr_t npe_ctx_phys;
	int npe_ctx_idx;
	u8 npe_mode;
};

struct ixp_ctx {
	struct ix_sa_dir encrypt;
	struct ix_sa_dir decrypt;
	int authkey_len;
	u8 authkey[MAX_KEYLEN];
	int enckey_len;
	u8 enckey[MAX_KEYLEN];
	u8 salt[MAX_IVLEN];
	u8 nonce[CTR_RFC3686_NONCE_SIZE];
	unsigned salted;
	atomic_t configuring;
	struct completion completion;
};

struct ixp_alg {
	struct crypto_alg crypto;
	const struct ix_hash_algo *hash;
	u32 cfg_enc;
	u32 cfg_dec;

	int registered;
};

static const struct ix_hash_algo hash_alg_md5 = {
	.cfgword	= 0xAA010004,
	.icv		= "\x01\x23\x45\x67\x89\xAB\xCD\xEF"
			  "\xFE\xDC\xBA\x98\x76\x54\x32\x10",
};
static const struct ix_hash_algo hash_alg_sha1 = {
	.cfgword	= 0x00000005,
	.icv		= "\x67\x45\x23\x01\xEF\xCD\xAB\x89\x98\xBA"
			  "\xDC\xFE\x10\x32\x54\x76\xC3\xD2\xE1\xF0",
};

static struct npe *npe_c;
static struct dma_pool *buffer_pool = NULL;
static struct dma_pool *ctx_pool = NULL;

static struct crypt_ctl *crypt_virt = NULL;
static dma_addr_t crypt_phys;

static int support_aes = 1;

static void dev_release(struct device *dev)
{
	return;
}

#define DRIVER_NAME "ixp4xx_crypto"
static struct platform_device pseudo_dev = {
	.name = DRIVER_NAME,
	.id   = 0,
	.num_resources = 0,
	.dev  = {
		.coherent_dma_mask = DMA_BIT_MASK(32),
		.release = dev_release,
	}
};

static struct device *dev = &pseudo_dev.dev;

static inline dma_addr_t crypt_virt2phys(struct crypt_ctl *virt)
{
	return crypt_phys + (virt - crypt_virt) * sizeof(struct crypt_ctl);
}

static inline struct crypt_ctl *crypt_phys2virt(dma_addr_t phys)
{
	return crypt_virt + (phys - crypt_phys) / sizeof(struct crypt_ctl);
}

static inline u32 cipher_cfg_enc(struct crypto_tfm *tfm)
{
	return container_of(tfm->__crt_alg, struct ixp_alg,crypto)->cfg_enc;
}

static inline u32 cipher_cfg_dec(struct crypto_tfm *tfm)
{
	return container_of(tfm->__crt_alg, struct ixp_alg,crypto)->cfg_dec;
}

static inline const struct ix_hash_algo *ix_hash(struct crypto_tfm *tfm)
{
	return container_of(tfm->__crt_alg, struct ixp_alg, crypto)->hash;
}

static int setup_crypt_desc(void)
{
	BUILD_BUG_ON(sizeof(struct crypt_ctl) != 64);
	crypt_virt = dma_alloc_coherent(dev,
			NPE_QLEN * sizeof(struct crypt_ctl),
			&crypt_phys, GFP_ATOMIC);
	if (!crypt_virt)
		return -ENOMEM;
	memset(crypt_virt, 0, NPE_QLEN * sizeof(struct crypt_ctl));
	return 0;
}

static spinlock_t desc_lock;
static struct crypt_ctl *get_crypt_desc(void)
{
	int i;
	static int idx = 0;
	unsigned long flags;

	spin_lock_irqsave(&desc_lock, flags);

	if (unlikely(!crypt_virt))
		setup_crypt_desc();
	if (unlikely(!crypt_virt)) {
		spin_unlock_irqrestore(&desc_lock, flags);
		return NULL;
	}
	i = idx;
	if (crypt_virt[i].ctl_flags == CTL_FLAG_UNUSED) {
		if (++idx >= NPE_QLEN)
			idx = 0;
		crypt_virt[i].ctl_flags = CTL_FLAG_USED;
		spin_unlock_irqrestore(&desc_lock, flags);
		return crypt_virt +i;
	} else {
		spin_unlock_irqrestore(&desc_lock, flags);
		return NULL;
	}
}

static spinlock_t emerg_lock;
static struct crypt_ctl *get_crypt_desc_emerg(void)
{
	int i;
	static int idx = NPE_QLEN;
	struct crypt_ctl *desc;
	unsigned long flags;

	desc = get_crypt_desc();
	if (desc)
		return desc;
	if (unlikely(!crypt_virt))
		return NULL;

	spin_lock_irqsave(&emerg_lock, flags);
	i = idx;
	if (crypt_virt[i].ctl_flags == CTL_FLAG_UNUSED) {
		if (++idx >= NPE_QLEN_TOTAL)
			idx = NPE_QLEN;
		crypt_virt[i].ctl_flags = CTL_FLAG_USED;
		spin_unlock_irqrestore(&emerg_lock, flags);
		return crypt_virt +i;
	} else {
		spin_unlock_irqrestore(&emerg_lock, flags);
		return NULL;
	}
}

static void free_buf_chain(struct device *dev, struct buffer_desc *buf,u32 phys)
{
	while (buf) {
		struct buffer_desc *buf1;
		u32 phys1;

		buf1 = buf->next;
		phys1 = buf->phys_next;
		dma_unmap_single(dev, buf->phys_next, buf->buf_len, buf->dir);
		dma_pool_free(buffer_pool, buf, phys);
		buf = buf1;
		phys = phys1;
	}
}

static struct tasklet_struct crypto_done_tasklet;

static void finish_scattered_hmac(struct crypt_ctl *crypt)
{
	struct aead_request *req = crypt->data.aead_req;
	struct aead_ctx *req_ctx = aead_request_ctx(req);
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	int authsize = crypto_aead_authsize(tfm);
	int decryptlen = req->cryptlen - authsize;

	if (req_ctx->encrypt) {
		scatterwalk_map_and_copy(req_ctx->hmac_virt,
			req->src, decryptlen, authsize, 1);
	}
	dma_pool_free(buffer_pool, req_ctx->hmac_virt, crypt->icv_rev_aes);
}

static void one_packet(dma_addr_t phys)
{
	struct crypt_ctl *crypt;
	struct ixp_ctx *ctx;
	int failed;

	failed = phys & 0x1 ? -EBADMSG : 0;
	phys &= ~0x3;
	crypt = crypt_phys2virt(phys);

	switch (crypt->ctl_flags & CTL_FLAG_MASK) {
	case CTL_FLAG_PERFORM_AEAD: {
		struct aead_request *req = crypt->data.aead_req;
		struct aead_ctx *req_ctx = aead_request_ctx(req);

		free_buf_chain(dev, req_ctx->buffer, crypt->src_buf);
		if (req_ctx->hmac_virt) {
			finish_scattered_hmac(crypt);
		}
		req->base.complete(&req->base, failed);
		break;
	}
	case CTL_FLAG_PERFORM_ABLK: {
		struct ablkcipher_request *req = crypt->data.ablk_req;
		struct ablk_ctx *req_ctx = ablkcipher_request_ctx(req);

		if (req_ctx->dst) {
			free_buf_chain(dev, req_ctx->dst, crypt->dst_buf);
		}
		free_buf_chain(dev, req_ctx->src, crypt->src_buf);
		req->base.complete(&req->base, failed);
		break;
	}
	case CTL_FLAG_GEN_ICV:
		ctx = crypto_tfm_ctx(crypt->data.tfm);
		dma_pool_free(ctx_pool, crypt->regist_ptr,
				crypt->regist_buf->phys_addr);
		dma_pool_free(buffer_pool, crypt->regist_buf, crypt->src_buf);
		if (atomic_dec_and_test(&ctx->configuring))
			complete(&ctx->completion);
		break;
	case CTL_FLAG_GEN_REVAES:
		ctx = crypto_tfm_ctx(crypt->data.tfm);
		*(u32*)ctx->decrypt.npe_ctx &= cpu_to_be32(~CIPH_ENCR);
		if (atomic_dec_and_test(&ctx->configuring))
			complete(&ctx->completion);
		break;
	default:
		BUG();
	}
	crypt->ctl_flags = CTL_FLAG_UNUSED;
}

static void irqhandler(void *_unused)
{
	tasklet_schedule(&crypto_done_tasklet);
}

static void crypto_done_action(unsigned long arg)
{
	int i;

	for(i=0; i<4; i++) {
		dma_addr_t phys = qmgr_get_entry(RECV_QID);
		if (!phys)
			return;
		one_packet(phys);
	}
	tasklet_schedule(&crypto_done_tasklet);
}

static int init_ixp_crypto(void)
{
	int ret = -ENODEV;
	u32 msg[2] = { 0, 0 };

	if (! ( ~(*IXP4XX_EXP_CFG2) & (IXP4XX_FEATURE_HASH |
				IXP4XX_FEATURE_AES | IXP4XX_FEATURE_DES))) {
		printk(KERN_ERR "ixp_crypto: No HW crypto available\n");
		return ret;
	}
	npe_c = npe_request(NPE_ID);
	if (!npe_c)
		return ret;

	if (!npe_running(npe_c)) {
		ret = npe_load_firmware(npe_c, npe_name(npe_c), dev);
		if (ret) {
			return ret;
		}
		if (npe_recv_message(npe_c, msg, "STATUS_MSG"))
			goto npe_error;
	} else {
		if (npe_send_message(npe_c, msg, "STATUS_MSG"))
			goto npe_error;

		if (npe_recv_message(npe_c, msg, "STATUS_MSG"))
			goto npe_error;
	}

	switch ((msg[1]>>16) & 0xff) {
	case 3:
		printk(KERN_WARNING "Firmware of %s lacks AES support\n",
				npe_name(npe_c));
		support_aes = 0;
		break;
	case 4:
	case 5:
		support_aes = 1;
		break;
	default:
		printk(KERN_ERR "Firmware of %s lacks crypto support\n",
			npe_name(npe_c));
		return -ENODEV;
	}
	/* buffer_pool will also be used to sometimes store the hmac,
	 * so assure it is large enough
	 */
	BUILD_BUG_ON(SHA1_DIGEST_SIZE > sizeof(struct buffer_desc));
	buffer_pool = dma_pool_create("buffer", dev,
			sizeof(struct buffer_desc), 32, 0);
	ret = -ENOMEM;
	if (!buffer_pool) {
		goto err;
	}
	ctx_pool = dma_pool_create("context", dev,
			NPE_CTX_LEN, 16, 0);
	if (!ctx_pool) {
		goto err;
	}
	ret = qmgr_request_queue(SEND_QID, NPE_QLEN_TOTAL, 0, 0,
				 "ixp_crypto:out", NULL);
	if (ret)
		goto err;
	ret = qmgr_request_queue(RECV_QID, NPE_QLEN, 0, 0,
				 "ixp_crypto:in", NULL);
	if (ret) {
		qmgr_release_queue(SEND_QID);
		goto err;
	}
	qmgr_set_irq(RECV_QID, QUEUE_IRQ_SRC_NOT_EMPTY, irqhandler, NULL);
	tasklet_init(&crypto_done_tasklet, crypto_done_action, 0);

	qmgr_enable_irq(RECV_QID);
	return 0;

npe_error:
	printk(KERN_ERR "%s not responding\n", npe_name(npe_c));
	ret = -EIO;
err:
	if (ctx_pool)
		dma_pool_destroy(ctx_pool);
	if (buffer_pool)
		dma_pool_destroy(buffer_pool);
	npe_release(npe_c);
	return ret;
}

static void release_ixp_crypto(void)
{
	qmgr_disable_irq(RECV_QID);
	tasklet_kill(&crypto_done_tasklet);

	qmgr_release_queue(SEND_QID);
	qmgr_release_queue(RECV_QID);

	dma_pool_destroy(ctx_pool);
	dma_pool_destroy(buffer_pool);

	npe_release(npe_c);

	if (crypt_virt) {
		dma_free_coherent(dev,
			NPE_QLEN_TOTAL * sizeof( struct crypt_ctl),
			crypt_virt, crypt_phys);
	}
	return;
}

static void reset_sa_dir(struct ix_sa_dir *dir)
{
	memset(dir->npe_ctx, 0, NPE_CTX_LEN);
	dir->npe_ctx_idx = 0;
	dir->npe_mode = 0;
}

static int init_sa_dir(struct ix_sa_dir *dir)
{
	dir->npe_ctx = dma_pool_alloc(ctx_pool, GFP_KERNEL, &dir->npe_ctx_phys);
	if (!dir->npe_ctx) {
		return -ENOMEM;
	}
	reset_sa_dir(dir);
	return 0;
}

static void free_sa_dir(struct ix_sa_dir *dir)
{
	memset(dir->npe_ctx, 0, NPE_CTX_LEN);
	dma_pool_free(ctx_pool, dir->npe_ctx, dir->npe_ctx_phys);
}

static int init_tfm(struct crypto_tfm *tfm)
{
	struct ixp_ctx *ctx = crypto_tfm_ctx(tfm);
	int ret;

	atomic_set(&ctx->configuring, 0);
	ret = init_sa_dir(&ctx->encrypt);
	if (ret)
		return ret;
	ret = init_sa_dir(&ctx->decrypt);
	if (ret) {
		free_sa_dir(&ctx->encrypt);
	}
	return ret;
}

static int init_tfm_ablk(struct crypto_tfm *tfm)
{
	tfm->crt_ablkcipher.reqsize = sizeof(struct ablk_ctx);
	return init_tfm(tfm);
}

static int init_tfm_aead(struct crypto_tfm *tfm)
{
	tfm->crt_aead.reqsize = sizeof(struct aead_ctx);
	return init_tfm(tfm);
}

static void exit_tfm(struct crypto_tfm *tfm)
{
	struct ixp_ctx *ctx = crypto_tfm_ctx(tfm);
	free_sa_dir(&ctx->encrypt);
	free_sa_dir(&ctx->decrypt);
}

static int register_chain_var(struct crypto_tfm *tfm, u8 xpad, u32 target,
		int init_len, u32 ctx_addr, const u8 *key, int key_len)
{
	struct ixp_ctx *ctx = crypto_tfm_ctx(tfm);
	struct crypt_ctl *crypt;
	struct buffer_desc *buf;
	int i;
	u8 *pad;
	u32 pad_phys, buf_phys;

	BUILD_BUG_ON(NPE_CTX_LEN < HMAC_PAD_BLOCKLEN);
	pad = dma_pool_alloc(ctx_pool, GFP_KERNEL, &pad_phys);
	if (!pad)
		return -ENOMEM;
	buf = dma_pool_alloc(buffer_pool, GFP_KERNEL, &buf_phys);
	if (!buf) {
		dma_pool_free(ctx_pool, pad, pad_phys);
		return -ENOMEM;
	}
	crypt = get_crypt_desc_emerg();
	if (!crypt) {
		dma_pool_free(ctx_pool, pad, pad_phys);
		dma_pool_free(buffer_pool, buf, buf_phys);
		return -EAGAIN;
	}

	memcpy(pad, key, key_len);
	memset(pad + key_len, 0, HMAC_PAD_BLOCKLEN - key_len);
	for (i = 0; i < HMAC_PAD_BLOCKLEN; i++) {
		pad[i] ^= xpad;
	}

	crypt->data.tfm = tfm;
	crypt->regist_ptr = pad;
	crypt->regist_buf = buf;

	crypt->auth_offs = 0;
	crypt->auth_len = HMAC_PAD_BLOCKLEN;
	crypt->crypto_ctx = ctx_addr;
	crypt->src_buf = buf_phys;
	crypt->icv_rev_aes = target;
	crypt->mode = NPE_OP_HASH_GEN_ICV;
	crypt->init_len = init_len;
	crypt->ctl_flags |= CTL_FLAG_GEN_ICV;

	buf->next = 0;
	buf->buf_len = HMAC_PAD_BLOCKLEN;
	buf->pkt_len = 0;
	buf->phys_addr = pad_phys;

	atomic_inc(&ctx->configuring);
	qmgr_put_entry(SEND_QID, crypt_virt2phys(crypt));
	BUG_ON(qmgr_stat_overflow(SEND_QID));
	return 0;
}

static int setup_auth(struct crypto_tfm *tfm, int encrypt, unsigned authsize,
		const u8 *key, int key_len, unsigned digest_len)
{
	u32 itarget, otarget, npe_ctx_addr;
	unsigned char *cinfo;
	int init_len, ret = 0;
	u32 cfgword;
	struct ix_sa_dir *dir;
	struct ixp_ctx *ctx = crypto_tfm_ctx(tfm);
	const struct ix_hash_algo *algo;

	dir = encrypt ? &ctx->encrypt : &ctx->decrypt;
	cinfo = dir->npe_ctx + dir->npe_ctx_idx;
	algo = ix_hash(tfm);

	/* write cfg word to cryptinfo */
	cfgword = algo->cfgword | ( authsize << 6); /* (authsize/4) << 8 */
#ifndef __ARMEB__
	cfgword ^= 0xAA000000; /* change the "byte swap" flags */
#endif
	*(u32*)cinfo = cpu_to_be32(cfgword);
	cinfo += sizeof(cfgword);

	/* write ICV to cryptinfo */
	memcpy(cinfo, algo->icv, digest_len);
	cinfo += digest_len;

	itarget = dir->npe_ctx_phys + dir->npe_ctx_idx
				+ sizeof(algo->cfgword);
	otarget = itarget + digest_len;
	init_len = cinfo - (dir->npe_ctx + dir->npe_ctx_idx);
	npe_ctx_addr = dir->npe_ctx_phys + dir->npe_ctx_idx;

	dir->npe_ctx_idx += init_len;
	dir->npe_mode |= NPE_OP_HASH_ENABLE;

	if (!encrypt)
		dir->npe_mode |= NPE_OP_HASH_VERIFY;

	ret = register_chain_var(tfm, HMAC_OPAD_VALUE, otarget,
			init_len, npe_ctx_addr, key, key_len);
	if (ret)
		return ret;
	return register_chain_var(tfm, HMAC_IPAD_VALUE, itarget,
			init_len, npe_ctx_addr, key, key_len);
}

static int gen_rev_aes_key(struct crypto_tfm *tfm)
{
	struct crypt_ctl *crypt;
	struct ixp_ctx *ctx = crypto_tfm_ctx(tfm);
	struct ix_sa_dir *dir = &ctx->decrypt;

	crypt = get_crypt_desc_emerg();
	if (!crypt) {
		return -EAGAIN;
	}
	*(u32*)dir->npe_ctx |= cpu_to_be32(CIPH_ENCR);

	crypt->data.tfm = tfm;
	crypt->crypt_offs = 0;
	crypt->crypt_len = AES_BLOCK128;
	crypt->src_buf = 0;
	crypt->crypto_ctx = dir->npe_ctx_phys;
	crypt->icv_rev_aes = dir->npe_ctx_phys + sizeof(u32);
	crypt->mode = NPE_OP_ENC_GEN_KEY;
	crypt->init_len = dir->npe_ctx_idx;
	crypt->ctl_flags |= CTL_FLAG_GEN_REVAES;

	atomic_inc(&ctx->configuring);
	qmgr_put_entry(SEND_QID, crypt_virt2phys(crypt));
	BUG_ON(qmgr_stat_overflow(SEND_QID));
	return 0;
}

static int setup_cipher(struct crypto_tfm *tfm, int encrypt,
		const u8 *key, int key_len)
{
	u8 *cinfo;
	u32 cipher_cfg;
	u32 keylen_cfg = 0;
	struct ix_sa_dir *dir;
	struct ixp_ctx *ctx = crypto_tfm_ctx(tfm);
	u32 *flags = &tfm->crt_flags;

	dir = encrypt ? &ctx->encrypt : &ctx->decrypt;
	cinfo = dir->npe_ctx;

	if (encrypt) {
		cipher_cfg = cipher_cfg_enc(tfm);
		dir->npe_mode |= NPE_OP_CRYPT_ENCRYPT;
	} else {
		cipher_cfg = cipher_cfg_dec(tfm);
	}
	if (cipher_cfg & MOD_AES) {
		switch (key_len) {
			case 16: keylen_cfg = MOD_AES128 | KEYLEN_128; break;
			case 24: keylen_cfg = MOD_AES192 | KEYLEN_192; break;
			case 32: keylen_cfg = MOD_AES256 | KEYLEN_256; break;
			default:
				*flags |= CRYPTO_TFM_RES_BAD_KEY_LEN;
				return -EINVAL;
		}
		cipher_cfg |= keylen_cfg;
	} else if (cipher_cfg & MOD_3DES) {
		const u32 *K = (const u32 *)key;
		if (unlikely(!((K[0] ^ K[2]) | (K[1] ^ K[3])) ||
			     !((K[2] ^ K[4]) | (K[3] ^ K[5]))))
		{
			*flags |= CRYPTO_TFM_RES_BAD_KEY_SCHED;
			return -EINVAL;
		}
	} else {
		u32 tmp[DES_EXPKEY_WORDS];
		if (des_ekey(tmp, key) == 0) {
			*flags |= CRYPTO_TFM_RES_WEAK_KEY;
		}
	}
	/* write cfg word to cryptinfo */
	*(u32*)cinfo = cpu_to_be32(cipher_cfg);
	cinfo += sizeof(cipher_cfg);

	/* write cipher key to cryptinfo */
	memcpy(cinfo, key, key_len);
	/* NPE wants keylen set to DES3_EDE_KEY_SIZE even for single DES */
	if (key_len < DES3_EDE_KEY_SIZE && !(cipher_cfg & MOD_AES)) {
		memset(cinfo + key_len, 0, DES3_EDE_KEY_SIZE -key_len);
		key_len = DES3_EDE_KEY_SIZE;
	}
	dir->npe_ctx_idx = sizeof(cipher_cfg) + key_len;
	dir->npe_mode |= NPE_OP_CRYPT_ENABLE;
	if ((cipher_cfg & MOD_AES) && !encrypt) {
		return gen_rev_aes_key(tfm);
	}
	return 0;
}

static struct buffer_desc *chainup_buffers(struct device *dev,
		struct scatterlist *sg,	unsigned nbytes,
		struct buffer_desc *buf, gfp_t flags,
		enum dma_data_direction dir)
{
	for (;nbytes > 0; sg = scatterwalk_sg_next(sg)) {
		unsigned len = min(nbytes, sg->length);
		struct buffer_desc *next_buf;
		u32 next_buf_phys;
		void *ptr;

		nbytes -= len;
		ptr = page_address(sg_page(sg)) + sg->offset;
		next_buf = dma_pool_alloc(buffer_pool, flags, &next_buf_phys);
		if (!next_buf) {
			buf = NULL;
			break;
		}
		sg_dma_address(sg) = dma_map_single(dev, ptr, len, dir);
		buf->next = next_buf;
		buf->phys_next = next_buf_phys;
		buf = next_buf;

		buf->phys_addr = sg_dma_address(sg);
		buf->buf_len = len;
		buf->dir = dir;
	}
	buf->next = NULL;
	buf->phys_next = 0;
	return buf;
}

static int ablk_setkey(struct crypto_ablkcipher *tfm, const u8 *key,
			unsigned int key_len)
{
	struct ixp_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	u32 *flags = &tfm->base.crt_flags;
	int ret;

	init_completion(&ctx->completion);
	atomic_inc(&ctx->configuring);

	reset_sa_dir(&ctx->encrypt);
	reset_sa_dir(&ctx->decrypt);

	ctx->encrypt.npe_mode = NPE_OP_HMAC_DISABLE;
	ctx->decrypt.npe_mode = NPE_OP_HMAC_DISABLE;

	ret = setup_cipher(&tfm->base, 0, key, key_len);
	if (ret)
		goto out;
	ret = setup_cipher(&tfm->base, 1, key, key_len);
	if (ret)
		goto out;

	if (*flags & CRYPTO_TFM_RES_WEAK_KEY) {
		if (*flags & CRYPTO_TFM_REQ_WEAK_KEY) {
			ret = -EINVAL;
		} else {
			*flags &= ~CRYPTO_TFM_RES_WEAK_KEY;
		}
	}
out:
	if (!atomic_dec_and_test(&ctx->configuring))
		wait_for_completion(&ctx->completion);
	return ret;
}

static int ablk_rfc3686_setkey(struct crypto_ablkcipher *tfm, const u8 *key,
		unsigned int key_len)
{
	struct ixp_ctx *ctx = crypto_ablkcipher_ctx(tfm);

	/* the nonce is stored in bytes at end of key */
	if (key_len < CTR_RFC3686_NONCE_SIZE)
		return -EINVAL;

	memcpy(ctx->nonce, key + (key_len - CTR_RFC3686_NONCE_SIZE),
			CTR_RFC3686_NONCE_SIZE);

	key_len -= CTR_RFC3686_NONCE_SIZE;
	return ablk_setkey(tfm, key, key_len);
}

static int ablk_perform(struct ablkcipher_request *req, int encrypt)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct ixp_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	unsigned ivsize = crypto_ablkcipher_ivsize(tfm);
	struct ix_sa_dir *dir;
	struct crypt_ctl *crypt;
	unsigned int nbytes = req->nbytes;
	enum dma_data_direction src_direction = DMA_BIDIRECTIONAL;
	struct ablk_ctx *req_ctx = ablkcipher_request_ctx(req);
	struct buffer_desc src_hook;
	gfp_t flags = req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP ?
				GFP_KERNEL : GFP_ATOMIC;

	if (qmgr_stat_full(SEND_QID))
		return -EAGAIN;
	if (atomic_read(&ctx->configuring))
		return -EAGAIN;

	dir = encrypt ? &ctx->encrypt : &ctx->decrypt;

	crypt = get_crypt_desc();
	if (!crypt)
		return -ENOMEM;

	crypt->data.ablk_req = req;
	crypt->crypto_ctx = dir->npe_ctx_phys;
	crypt->mode = dir->npe_mode;
	crypt->init_len = dir->npe_ctx_idx;

	crypt->crypt_offs = 0;
	crypt->crypt_len = nbytes;

	BUG_ON(ivsize && !req->info);
	memcpy(crypt->iv, req->info, ivsize);
	if (req->src != req->dst) {
		struct buffer_desc dst_hook;
		crypt->mode |= NPE_OP_NOT_IN_PLACE;
		/* This was never tested by Intel
		 * for more than one dst buffer, I think. */
		BUG_ON(req->dst->length < nbytes);
		req_ctx->dst = NULL;
		if (!chainup_buffers(dev, req->dst, nbytes, &dst_hook,
					flags, DMA_FROM_DEVICE))
			goto free_buf_dest;
		src_direction = DMA_TO_DEVICE;
		req_ctx->dst = dst_hook.next;
		crypt->dst_buf = dst_hook.phys_next;
	} else {
		req_ctx->dst = NULL;
	}
	req_ctx->src = NULL;
	if (!chainup_buffers(dev, req->src, nbytes, &src_hook,
				flags, src_direction))
		goto free_buf_src;

	req_ctx->src = src_hook.next;
	crypt->src_buf = src_hook.phys_next;
	crypt->ctl_flags |= CTL_FLAG_PERFORM_ABLK;
	qmgr_put_entry(SEND_QID, crypt_virt2phys(crypt));
	BUG_ON(qmgr_stat_overflow(SEND_QID));
	return -EINPROGRESS;

free_buf_src:
	free_buf_chain(dev, req_ctx->src, crypt->src_buf);
free_buf_dest:
	if (req->src != req->dst) {
		free_buf_chain(dev, req_ctx->dst, crypt->dst_buf);
	}
	crypt->ctl_flags = CTL_FLAG_UNUSED;
	return -ENOMEM;
}

static int ablk_encrypt(struct ablkcipher_request *req)
{
	return ablk_perform(req, 1);
}

static int ablk_decrypt(struct ablkcipher_request *req)
{
	return ablk_perform(req, 0);
}

static int ablk_rfc3686_crypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct ixp_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	u8 iv[CTR_RFC3686_BLOCK_SIZE];
	u8 *info = req->info;
	int ret;

	/* set up counter block */
        memcpy(iv, ctx->nonce, CTR_RFC3686_NONCE_SIZE);
	memcpy(iv + CTR_RFC3686_NONCE_SIZE, info, CTR_RFC3686_IV_SIZE);

	/* initialize counter portion of counter block */
	*(__be32 *)(iv + CTR_RFC3686_NONCE_SIZE + CTR_RFC3686_IV_SIZE) =
		cpu_to_be32(1);

	req->info = iv;
	ret = ablk_perform(req, 1);
	req->info = info;
	return ret;
}

static int hmac_inconsistent(struct scatterlist *sg, unsigned start,
		unsigned int nbytes)
{
	int offset = 0;

	if (!nbytes)
		return 0;

	for (;;) {
		if (start < offset + sg->length)
			break;

		offset += sg->length;
		sg = scatterwalk_sg_next(sg);
	}
	return (start + nbytes > offset + sg->length);
}

static int aead_perform(struct aead_request *req, int encrypt,
		int cryptoffset, int eff_cryptlen, u8 *iv)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct ixp_ctx *ctx = crypto_aead_ctx(tfm);
	unsigned ivsize = crypto_aead_ivsize(tfm);
	unsigned authsize = crypto_aead_authsize(tfm);
	struct ix_sa_dir *dir;
	struct crypt_ctl *crypt;
	unsigned int cryptlen;
	struct buffer_desc *buf, src_hook;
	struct aead_ctx *req_ctx = aead_request_ctx(req);
	gfp_t flags = req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP ?
				GFP_KERNEL : GFP_ATOMIC;

	if (qmgr_stat_full(SEND_QID))
		return -EAGAIN;
	if (atomic_read(&ctx->configuring))
		return -EAGAIN;

	if (encrypt) {
		dir = &ctx->encrypt;
		cryptlen = req->cryptlen;
	} else {
		dir = &ctx->decrypt;
		/* req->cryptlen includes the authsize when decrypting */
		cryptlen = req->cryptlen -authsize;
		eff_cryptlen -= authsize;
	}
	crypt = get_crypt_desc();
	if (!crypt)
		return -ENOMEM;

	crypt->data.aead_req = req;
	crypt->crypto_ctx = dir->npe_ctx_phys;
	crypt->mode = dir->npe_mode;
	crypt->init_len = dir->npe_ctx_idx;

	crypt->crypt_offs = cryptoffset;
	crypt->crypt_len = eff_cryptlen;

	crypt->auth_offs = 0;
	crypt->auth_len = req->assoclen + ivsize + cryptlen;
	BUG_ON(ivsize && !req->iv);
	memcpy(crypt->iv, req->iv, ivsize);

	if (req->src != req->dst) {
		BUG(); /* -ENOTSUP because of my laziness */
	}

	/* ASSOC data */
	buf = chainup_buffers(dev, req->assoc, req->assoclen, &src_hook,
		flags, DMA_TO_DEVICE);
	req_ctx->buffer = src_hook.next;
	crypt->src_buf = src_hook.phys_next;
	if (!buf)
		goto out;
	/* IV */
	sg_init_table(&req_ctx->ivlist, 1);
	sg_set_buf(&req_ctx->ivlist, iv, ivsize);
	buf = chainup_buffers(dev, &req_ctx->ivlist, ivsize, buf, flags,
			DMA_BIDIRECTIONAL);
	if (!buf)
		goto free_chain;
	if (unlikely(hmac_inconsistent(req->src, cryptlen, authsize))) {
		/* The 12 hmac bytes are scattered,
		 * we need to copy them into a safe buffer */
		req_ctx->hmac_virt = dma_pool_alloc(buffer_pool, flags,
				&crypt->icv_rev_aes);
		if (unlikely(!req_ctx->hmac_virt))
			goto free_chain;
		if (!encrypt) {
			scatterwalk_map_and_copy(req_ctx->hmac_virt,
				req->src, cryptlen, authsize, 0);
		}
		req_ctx->encrypt = encrypt;
	} else {
		req_ctx->hmac_virt = NULL;
	}
	/* Crypt */
	buf = chainup_buffers(dev, req->src, cryptlen + authsize, buf, flags,
			DMA_BIDIRECTIONAL);
	if (!buf)
		goto free_hmac_virt;
	if (!req_ctx->hmac_virt) {
		crypt->icv_rev_aes = buf->phys_addr + buf->buf_len - authsize;
	}

	crypt->ctl_flags |= CTL_FLAG_PERFORM_AEAD;
	qmgr_put_entry(SEND_QID, crypt_virt2phys(crypt));
	BUG_ON(qmgr_stat_overflow(SEND_QID));
	return -EINPROGRESS;
free_hmac_virt:
	if (req_ctx->hmac_virt) {
		dma_pool_free(buffer_pool, req_ctx->hmac_virt,
				crypt->icv_rev_aes);
	}
free_chain:
	free_buf_chain(dev, req_ctx->buffer, crypt->src_buf);
out:
	crypt->ctl_flags = CTL_FLAG_UNUSED;
	return -ENOMEM;
}

static int aead_setup(struct crypto_aead *tfm, unsigned int authsize)
{
	struct ixp_ctx *ctx = crypto_aead_ctx(tfm);
	u32 *flags = &tfm->base.crt_flags;
	unsigned digest_len = crypto_aead_alg(tfm)->maxauthsize;
	int ret;

	if (!ctx->enckey_len && !ctx->authkey_len)
		return 0;
	init_completion(&ctx->completion);
	atomic_inc(&ctx->configuring);

	reset_sa_dir(&ctx->encrypt);
	reset_sa_dir(&ctx->decrypt);

	ret = setup_cipher(&tfm->base, 0, ctx->enckey, ctx->enckey_len);
	if (ret)
		goto out;
	ret = setup_cipher(&tfm->base, 1, ctx->enckey, ctx->enckey_len);
	if (ret)
		goto out;
	ret = setup_auth(&tfm->base, 0, authsize, ctx->authkey,
			ctx->authkey_len, digest_len);
	if (ret)
		goto out;
	ret = setup_auth(&tfm->base, 1, authsize,  ctx->authkey,
			ctx->authkey_len, digest_len);
	if (ret)
		goto out;

	if (*flags & CRYPTO_TFM_RES_WEAK_KEY) {
		if (*flags & CRYPTO_TFM_REQ_WEAK_KEY) {
			ret = -EINVAL;
			goto out;
		} else {
			*flags &= ~CRYPTO_TFM_RES_WEAK_KEY;
		}
	}
out:
	if (!atomic_dec_and_test(&ctx->configuring))
		wait_for_completion(&ctx->completion);
	return ret;
}

static int aead_setauthsize(struct crypto_aead *tfm, unsigned int authsize)
{
	int max = crypto_aead_alg(tfm)->maxauthsize >> 2;

	if ((authsize>>2) < 1 || (authsize>>2) > max || (authsize & 3))
		return -EINVAL;
	return aead_setup(tfm, authsize);
}

static int aead_setkey(struct crypto_aead *tfm, const u8 *key,
			unsigned int keylen)
{
	struct ixp_ctx *ctx = crypto_aead_ctx(tfm);
	struct rtattr *rta = (struct rtattr *)key;
	struct crypto_authenc_key_param *param;

	if (!RTA_OK(rta, keylen))
		goto badkey;
	if (rta->rta_type != CRYPTO_AUTHENC_KEYA_PARAM)
		goto badkey;
	if (RTA_PAYLOAD(rta) < sizeof(*param))
		goto badkey;

	param = RTA_DATA(rta);
	ctx->enckey_len = be32_to_cpu(param->enckeylen);

	key += RTA_ALIGN(rta->rta_len);
	keylen -= RTA_ALIGN(rta->rta_len);

	if (keylen < ctx->enckey_len)
		goto badkey;

	ctx->authkey_len = keylen - ctx->enckey_len;
	memcpy(ctx->enckey, key + ctx->authkey_len, ctx->enckey_len);
	memcpy(ctx->authkey, key, ctx->authkey_len);

	return aead_setup(tfm, crypto_aead_authsize(tfm));
badkey:
	ctx->enckey_len = 0;
	crypto_aead_set_flags(tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
	return -EINVAL;
}

static int aead_encrypt(struct aead_request *req)
{
	unsigned ivsize = crypto_aead_ivsize(crypto_aead_reqtfm(req));
	return aead_perform(req, 1, req->assoclen + ivsize,
			req->cryptlen, req->iv);
}

static int aead_decrypt(struct aead_request *req)
{
	unsigned ivsize = crypto_aead_ivsize(crypto_aead_reqtfm(req));
	return aead_perform(req, 0, req->assoclen + ivsize,
			req->cryptlen, req->iv);
}

static int aead_givencrypt(struct aead_givcrypt_request *req)
{
	struct crypto_aead *tfm = aead_givcrypt_reqtfm(req);
	struct ixp_ctx *ctx = crypto_aead_ctx(tfm);
	unsigned len, ivsize = crypto_aead_ivsize(tfm);
	__be64 seq;

	/* copied from eseqiv.c */
	if (!ctx->salted) {
		get_random_bytes(ctx->salt, ivsize);
		ctx->salted = 1;
	}
	memcpy(req->areq.iv, ctx->salt, ivsize);
	len = ivsize;
	if (ivsize > sizeof(u64)) {
		memset(req->giv, 0, ivsize - sizeof(u64));
		len = sizeof(u64);
	}
	seq = cpu_to_be64(req->seq);
	memcpy(req->giv + ivsize - len, &seq, len);
	return aead_perform(&req->areq, 1, req->areq.assoclen,
			req->areq.cryptlen +ivsize, req->giv);
}

static struct ixp_alg ixp4xx_algos[] = {
{
	.crypto	= {
		.cra_name	= "cbc(des)",
		.cra_blocksize	= DES_BLOCK_SIZE,
		.cra_u		= { .ablkcipher = {
			.min_keysize	= DES_KEY_SIZE,
			.max_keysize	= DES_KEY_SIZE,
			.ivsize		= DES_BLOCK_SIZE,
			.geniv		= "eseqiv",
			}
		}
	},
	.cfg_enc = CIPH_ENCR | MOD_DES | MOD_CBC_ENC | KEYLEN_192,
	.cfg_dec = CIPH_DECR | MOD_DES | MOD_CBC_DEC | KEYLEN_192,

}, {
	.crypto	= {
		.cra_name	= "ecb(des)",
		.cra_blocksize	= DES_BLOCK_SIZE,
		.cra_u		= { .ablkcipher = {
			.min_keysize	= DES_KEY_SIZE,
			.max_keysize	= DES_KEY_SIZE,
			}
		}
	},
	.cfg_enc = CIPH_ENCR | MOD_DES | MOD_ECB | KEYLEN_192,
	.cfg_dec = CIPH_DECR | MOD_DES | MOD_ECB | KEYLEN_192,
}, {
	.crypto	= {
		.cra_name	= "cbc(des3_ede)",
		.cra_blocksize	= DES3_EDE_BLOCK_SIZE,
		.cra_u		= { .ablkcipher = {
			.min_keysize	= DES3_EDE_KEY_SIZE,
			.max_keysize	= DES3_EDE_KEY_SIZE,
			.ivsize		= DES3_EDE_BLOCK_SIZE,
			.geniv		= "eseqiv",
			}
		}
	},
	.cfg_enc = CIPH_ENCR | MOD_3DES | MOD_CBC_ENC | KEYLEN_192,
	.cfg_dec = CIPH_DECR | MOD_3DES | MOD_CBC_DEC | KEYLEN_192,
}, {
	.crypto	= {
		.cra_name	= "ecb(des3_ede)",
		.cra_blocksize	= DES3_EDE_BLOCK_SIZE,
		.cra_u		= { .ablkcipher = {
			.min_keysize	= DES3_EDE_KEY_SIZE,
			.max_keysize	= DES3_EDE_KEY_SIZE,
			}
		}
	},
	.cfg_enc = CIPH_ENCR | MOD_3DES | MOD_ECB | KEYLEN_192,
	.cfg_dec = CIPH_DECR | MOD_3DES | MOD_ECB | KEYLEN_192,
}, {
	.crypto	= {
		.cra_name	= "cbc(aes)",
		.cra_blocksize	= AES_BLOCK_SIZE,
		.cra_u		= { .ablkcipher = {
			.min_keysize	= AES_MIN_KEY_SIZE,
			.max_keysize	= AES_MAX_KEY_SIZE,
			.ivsize		= AES_BLOCK_SIZE,
			.geniv		= "eseqiv",
			}
		}
	},
	.cfg_enc = CIPH_ENCR | MOD_AES | MOD_CBC_ENC,
	.cfg_dec = CIPH_DECR | MOD_AES | MOD_CBC_DEC,
}, {
	.crypto	= {
		.cra_name	= "ecb(aes)",
		.cra_blocksize	= AES_BLOCK_SIZE,
		.cra_u		= { .ablkcipher = {
			.min_keysize	= AES_MIN_KEY_SIZE,
			.max_keysize	= AES_MAX_KEY_SIZE,
			}
		}
	},
	.cfg_enc = CIPH_ENCR | MOD_AES | MOD_ECB,
	.cfg_dec = CIPH_DECR | MOD_AES | MOD_ECB,
}, {
	.crypto	= {
		.cra_name	= "ctr(aes)",
		.cra_blocksize	= AES_BLOCK_SIZE,
		.cra_u		= { .ablkcipher = {
			.min_keysize	= AES_MIN_KEY_SIZE,
			.max_keysize	= AES_MAX_KEY_SIZE,
			.ivsize		= AES_BLOCK_SIZE,
			.geniv		= "eseqiv",
			}
		}
	},
	.cfg_enc = CIPH_ENCR | MOD_AES | MOD_CTR,
	.cfg_dec = CIPH_ENCR | MOD_AES | MOD_CTR,
}, {
	.crypto	= {
		.cra_name	= "rfc3686(ctr(aes))",
		.cra_blocksize	= AES_BLOCK_SIZE,
		.cra_u		= { .ablkcipher = {
			.min_keysize	= AES_MIN_KEY_SIZE,
			.max_keysize	= AES_MAX_KEY_SIZE,
			.ivsize		= AES_BLOCK_SIZE,
			.geniv		= "eseqiv",
			.setkey		= ablk_rfc3686_setkey,
			.encrypt	= ablk_rfc3686_crypt,
			.decrypt	= ablk_rfc3686_crypt }
		}
	},
	.cfg_enc = CIPH_ENCR | MOD_AES | MOD_CTR,
	.cfg_dec = CIPH_ENCR | MOD_AES | MOD_CTR,
}, {
	.crypto	= {
		.cra_name	= "authenc(hmac(md5),cbc(des))",
		.cra_blocksize	= DES_BLOCK_SIZE,
		.cra_u		= { .aead = {
			.ivsize		= DES_BLOCK_SIZE,
			.maxauthsize	= MD5_DIGEST_SIZE,
			}
		}
	},
	.hash = &hash_alg_md5,
	.cfg_enc = CIPH_ENCR | MOD_DES | MOD_CBC_ENC | KEYLEN_192,
	.cfg_dec = CIPH_DECR | MOD_DES | MOD_CBC_DEC | KEYLEN_192,
}, {
	.crypto	= {
		.cra_name	= "authenc(hmac(md5),cbc(des3_ede))",
		.cra_blocksize	= DES3_EDE_BLOCK_SIZE,
		.cra_u		= { .aead = {
			.ivsize		= DES3_EDE_BLOCK_SIZE,
			.maxauthsize	= MD5_DIGEST_SIZE,
			}
		}
	},
	.hash = &hash_alg_md5,
	.cfg_enc = CIPH_ENCR | MOD_3DES | MOD_CBC_ENC | KEYLEN_192,
	.cfg_dec = CIPH_DECR | MOD_3DES | MOD_CBC_DEC | KEYLEN_192,
}, {
	.crypto	= {
		.cra_name	= "authenc(hmac(sha1),cbc(des))",
		.cra_blocksize	= DES_BLOCK_SIZE,
		.cra_u		= { .aead = {
			.ivsize		= DES_BLOCK_SIZE,
			.maxauthsize	= SHA1_DIGEST_SIZE,
			}
		}
	},
	.hash = &hash_alg_sha1,
	.cfg_enc = CIPH_ENCR | MOD_DES | MOD_CBC_ENC | KEYLEN_192,
	.cfg_dec = CIPH_DECR | MOD_DES | MOD_CBC_DEC | KEYLEN_192,
}, {
	.crypto	= {
		.cra_name	= "authenc(hmac(sha1),cbc(des3_ede))",
		.cra_blocksize	= DES3_EDE_BLOCK_SIZE,
		.cra_u		= { .aead = {
			.ivsize		= DES3_EDE_BLOCK_SIZE,
			.maxauthsize	= SHA1_DIGEST_SIZE,
			}
		}
	},
	.hash = &hash_alg_sha1,
	.cfg_enc = CIPH_ENCR | MOD_3DES | MOD_CBC_ENC | KEYLEN_192,
	.cfg_dec = CIPH_DECR | MOD_3DES | MOD_CBC_DEC | KEYLEN_192,
}, {
	.crypto	= {
		.cra_name	= "authenc(hmac(md5),cbc(aes))",
		.cra_blocksize	= AES_BLOCK_SIZE,
		.cra_u		= { .aead = {
			.ivsize		= AES_BLOCK_SIZE,
			.maxauthsize	= MD5_DIGEST_SIZE,
			}
		}
	},
	.hash = &hash_alg_md5,
	.cfg_enc = CIPH_ENCR | MOD_AES | MOD_CBC_ENC,
	.cfg_dec = CIPH_DECR | MOD_AES | MOD_CBC_DEC,
}, {
	.crypto	= {
		.cra_name	= "authenc(hmac(sha1),cbc(aes))",
		.cra_blocksize	= AES_BLOCK_SIZE,
		.cra_u		= { .aead = {
			.ivsize		= AES_BLOCK_SIZE,
			.maxauthsize	= SHA1_DIGEST_SIZE,
			}
		}
	},
	.hash = &hash_alg_sha1,
	.cfg_enc = CIPH_ENCR | MOD_AES | MOD_CBC_ENC,
	.cfg_dec = CIPH_DECR | MOD_AES | MOD_CBC_DEC,
} };

#define IXP_POSTFIX "-ixp4xx"
static int __init ixp_module_init(void)
{
	int num = ARRAY_SIZE(ixp4xx_algos);
	int i,err ;

	if (platform_device_register(&pseudo_dev))
		return -ENODEV;

	spin_lock_init(&desc_lock);
	spin_lock_init(&emerg_lock);

	err = init_ixp_crypto();
	if (err) {
		platform_device_unregister(&pseudo_dev);
		return err;
	}
	for (i=0; i< num; i++) {
		struct crypto_alg *cra = &ixp4xx_algos[i].crypto;

		if (snprintf(cra->cra_driver_name, CRYPTO_MAX_ALG_NAME,
			"%s"IXP_POSTFIX, cra->cra_name) >=
			CRYPTO_MAX_ALG_NAME)
		{
			continue;
		}
		if (!support_aes && (ixp4xx_algos[i].cfg_enc & MOD_AES)) {
			continue;
		}
		if (!ixp4xx_algos[i].hash) {
			/* block ciphers */
			cra->cra_type = &crypto_ablkcipher_type;
			cra->cra_flags = CRYPTO_ALG_TYPE_ABLKCIPHER |
					 CRYPTO_ALG_KERN_DRIVER_ONLY |
					 CRYPTO_ALG_ASYNC;
			if (!cra->cra_ablkcipher.setkey)
				cra->cra_ablkcipher.setkey = ablk_setkey;
			if (!cra->cra_ablkcipher.encrypt)
				cra->cra_ablkcipher.encrypt = ablk_encrypt;
			if (!cra->cra_ablkcipher.decrypt)
				cra->cra_ablkcipher.decrypt = ablk_decrypt;
			cra->cra_init = init_tfm_ablk;
		} else {
			/* authenc */
			cra->cra_type = &crypto_aead_type;
			cra->cra_flags = CRYPTO_ALG_TYPE_AEAD |
					 CRYPTO_ALG_KERN_DRIVER_ONLY |
					 CRYPTO_ALG_ASYNC;
			cra->cra_aead.setkey = aead_setkey;
			cra->cra_aead.setauthsize = aead_setauthsize;
			cra->cra_aead.encrypt = aead_encrypt;
			cra->cra_aead.decrypt = aead_decrypt;
			cra->cra_aead.givencrypt = aead_givencrypt;
			cra->cra_init = init_tfm_aead;
		}
		cra->cra_ctxsize = sizeof(struct ixp_ctx);
		cra->cra_module = THIS_MODULE;
		cra->cra_alignmask = 3;
		cra->cra_priority = 300;
		cra->cra_exit = exit_tfm;
		if (crypto_register_alg(cra))
			printk(KERN_ERR "Failed to register '%s'\n",
				cra->cra_name);
		else
			ixp4xx_algos[i].registered = 1;
	}
	return 0;
}

static void __exit ixp_module_exit(void)
{
	int num = ARRAY_SIZE(ixp4xx_algos);
	int i;

	for (i=0; i< num; i++) {
		if (ixp4xx_algos[i].registered)
			crypto_unregister_alg(&ixp4xx_algos[i].crypto);
	}
	release_ixp_crypto();
	platform_device_unregister(&pseudo_dev);
}

module_init(ixp_module_init);
module_exit(ixp_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Christian Hohnstaedt <chohnstaedt@innominate.com>");
MODULE_DESCRIPTION("IXP4xx hardware crypto");

