/*
 * Copyright (C) STMicroelectronics SA 2017
 * Author: Fabien Dessenne <fabien.dessenne@st.com>
 * License terms:  GNU General Public License (GPL), version 2
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

#include <crypto/aes.h>
#include <crypto/des.h>
#include <crypto/engine.h>
#include <crypto/scatterwalk.h>

#define DRIVER_NAME             "stm32-cryp"

/* Bit [0] encrypt / decrypt */
#define FLG_ENCRYPT             BIT(0)
/* Bit [8..1] algo & operation mode */
#define FLG_AES                 BIT(1)
#define FLG_DES                 BIT(2)
#define FLG_TDES                BIT(3)
#define FLG_ECB                 BIT(4)
#define FLG_CBC                 BIT(5)
#define FLG_CTR                 BIT(6)
/* Mode mask = bits [15..0] */
#define FLG_MODE_MASK           GENMASK(15, 0)

/* Registers */
#define CRYP_CR                 0x00000000
#define CRYP_SR                 0x00000004
#define CRYP_DIN                0x00000008
#define CRYP_DOUT               0x0000000C
#define CRYP_DMACR              0x00000010
#define CRYP_IMSCR              0x00000014
#define CRYP_RISR               0x00000018
#define CRYP_MISR               0x0000001C
#define CRYP_K0LR               0x00000020
#define CRYP_K0RR               0x00000024
#define CRYP_K1LR               0x00000028
#define CRYP_K1RR               0x0000002C
#define CRYP_K2LR               0x00000030
#define CRYP_K2RR               0x00000034
#define CRYP_K3LR               0x00000038
#define CRYP_K3RR               0x0000003C
#define CRYP_IV0LR              0x00000040
#define CRYP_IV0RR              0x00000044
#define CRYP_IV1LR              0x00000048
#define CRYP_IV1RR              0x0000004C

/* Registers values */
#define CR_DEC_NOT_ENC          0x00000004
#define CR_TDES_ECB             0x00000000
#define CR_TDES_CBC             0x00000008
#define CR_DES_ECB              0x00000010
#define CR_DES_CBC              0x00000018
#define CR_AES_ECB              0x00000020
#define CR_AES_CBC              0x00000028
#define CR_AES_CTR              0x00000030
#define CR_AES_KP               0x00000038
#define CR_AES_UNKNOWN          0xFFFFFFFF
#define CR_ALGO_MASK            0x00080038
#define CR_DATA32               0x00000000
#define CR_DATA16               0x00000040
#define CR_DATA8                0x00000080
#define CR_DATA1                0x000000C0
#define CR_KEY128               0x00000000
#define CR_KEY192               0x00000100
#define CR_KEY256               0x00000200
#define CR_FFLUSH               0x00004000
#define CR_CRYPEN               0x00008000

#define SR_BUSY                 0x00000010
#define SR_OFNE                 0x00000004

#define IMSCR_IN                BIT(0)
#define IMSCR_OUT               BIT(1)

#define MISR_IN                 BIT(0)
#define MISR_OUT                BIT(1)

/* Misc */
#define AES_BLOCK_32            (AES_BLOCK_SIZE / sizeof(u32))
#define _walked_in              (cryp->in_walk.offset - cryp->in_sg->offset)
#define _walked_out             (cryp->out_walk.offset - cryp->out_sg->offset)

struct stm32_cryp_ctx {
	struct stm32_cryp       *cryp;
	int                     keylen;
	u32                     key[AES_KEYSIZE_256 / sizeof(u32)];
	unsigned long           flags;
};

struct stm32_cryp_reqctx {
	unsigned long mode;
};

struct stm32_cryp {
	struct list_head        list;
	struct device           *dev;
	void __iomem            *regs;
	struct clk              *clk;
	unsigned long           flags;
	u32                     irq_status;
	struct stm32_cryp_ctx   *ctx;

	struct crypto_engine    *engine;

	struct mutex            lock; /* protects req */
	struct ablkcipher_request *req;

	size_t                  hw_blocksize;

	size_t                  total_in;
	size_t                  total_in_save;
	size_t                  total_out;
	size_t                  total_out_save;

	struct scatterlist      *in_sg;
	struct scatterlist      *out_sg;
	struct scatterlist      *out_sg_save;

	struct scatterlist      in_sgl;
	struct scatterlist      out_sgl;
	bool                    sgs_copied;

	int                     in_sg_len;
	int                     out_sg_len;

	struct scatter_walk     in_walk;
	struct scatter_walk     out_walk;

	u32                     last_ctr[4];
};

struct stm32_cryp_list {
	struct list_head        dev_list;
	spinlock_t              lock; /* protect dev_list */
};

static struct stm32_cryp_list cryp_list = {
	.dev_list = LIST_HEAD_INIT(cryp_list.dev_list),
	.lock     = __SPIN_LOCK_UNLOCKED(cryp_list.lock),
};

static inline bool is_aes(struct stm32_cryp *cryp)
{
	return cryp->flags & FLG_AES;
}

static inline bool is_des(struct stm32_cryp *cryp)
{
	return cryp->flags & FLG_DES;
}

static inline bool is_tdes(struct stm32_cryp *cryp)
{
	return cryp->flags & FLG_TDES;
}

static inline bool is_ecb(struct stm32_cryp *cryp)
{
	return cryp->flags & FLG_ECB;
}

static inline bool is_cbc(struct stm32_cryp *cryp)
{
	return cryp->flags & FLG_CBC;
}

static inline bool is_ctr(struct stm32_cryp *cryp)
{
	return cryp->flags & FLG_CTR;
}

static inline bool is_encrypt(struct stm32_cryp *cryp)
{
	return cryp->flags & FLG_ENCRYPT;
}

static inline bool is_decrypt(struct stm32_cryp *cryp)
{
	return !is_encrypt(cryp);
}

static inline u32 stm32_cryp_read(struct stm32_cryp *cryp, u32 ofst)
{
	return readl_relaxed(cryp->regs + ofst);
}

static inline void stm32_cryp_write(struct stm32_cryp *cryp, u32 ofst, u32 val)
{
	writel_relaxed(val, cryp->regs + ofst);
}

static inline int stm32_cryp_wait_busy(struct stm32_cryp *cryp)
{
	u32 status;

	return readl_relaxed_poll_timeout(cryp->regs + CRYP_SR, status,
			!(status & SR_BUSY), 10, 100000);
}

static struct stm32_cryp *stm32_cryp_find_dev(struct stm32_cryp_ctx *ctx)
{
	struct stm32_cryp *tmp, *cryp = NULL;

	spin_lock_bh(&cryp_list.lock);
	if (!ctx->cryp) {
		list_for_each_entry(tmp, &cryp_list.dev_list, list) {
			cryp = tmp;
			break;
		}
		ctx->cryp = cryp;
	} else {
		cryp = ctx->cryp;
	}

	spin_unlock_bh(&cryp_list.lock);

	return cryp;
}

static int stm32_cryp_check_aligned(struct scatterlist *sg, size_t total,
				    size_t align)
{
	int len = 0;

	if (!total)
		return 0;

	if (!IS_ALIGNED(total, align))
		return -EINVAL;

	while (sg) {
		if (!IS_ALIGNED(sg->offset, sizeof(u32)))
			return -EINVAL;

		if (!IS_ALIGNED(sg->length, align))
			return -EINVAL;

		len += sg->length;
		sg = sg_next(sg);
	}

	if (len != total)
		return -EINVAL;

	return 0;
}

static int stm32_cryp_check_io_aligned(struct stm32_cryp *cryp)
{
	int ret;

	ret = stm32_cryp_check_aligned(cryp->in_sg, cryp->total_in,
				       cryp->hw_blocksize);
	if (ret)
		return ret;

	ret = stm32_cryp_check_aligned(cryp->out_sg, cryp->total_out,
				       cryp->hw_blocksize);

	return ret;
}

static void sg_copy_buf(void *buf, struct scatterlist *sg,
			unsigned int start, unsigned int nbytes, int out)
{
	struct scatter_walk walk;

	if (!nbytes)
		return;

	scatterwalk_start(&walk, sg);
	scatterwalk_advance(&walk, start);
	scatterwalk_copychunks(buf, &walk, nbytes, out);
	scatterwalk_done(&walk, out, 0);
}

static int stm32_cryp_copy_sgs(struct stm32_cryp *cryp)
{
	void *buf_in, *buf_out;
	int pages, total_in, total_out;

	if (!stm32_cryp_check_io_aligned(cryp)) {
		cryp->sgs_copied = 0;
		return 0;
	}

	total_in = ALIGN(cryp->total_in, cryp->hw_blocksize);
	pages = total_in ? get_order(total_in) : 1;
	buf_in = (void *)__get_free_pages(GFP_ATOMIC, pages);

	total_out = ALIGN(cryp->total_out, cryp->hw_blocksize);
	pages = total_out ? get_order(total_out) : 1;
	buf_out = (void *)__get_free_pages(GFP_ATOMIC, pages);

	if (!buf_in || !buf_out) {
		dev_err(cryp->dev, "Can't allocate pages when unaligned\n");
		cryp->sgs_copied = 0;
		return -EFAULT;
	}

	sg_copy_buf(buf_in, cryp->in_sg, 0, cryp->total_in, 0);

	sg_init_one(&cryp->in_sgl, buf_in, total_in);
	cryp->in_sg = &cryp->in_sgl;
	cryp->in_sg_len = 1;

	sg_init_one(&cryp->out_sgl, buf_out, total_out);
	cryp->out_sg_save = cryp->out_sg;
	cryp->out_sg = &cryp->out_sgl;
	cryp->out_sg_len = 1;

	cryp->sgs_copied = 1;

	return 0;
}

static void stm32_cryp_hw_write_iv(struct stm32_cryp *cryp, u32 *iv)
{
	if (!iv)
		return;

	stm32_cryp_write(cryp, CRYP_IV0LR, cpu_to_be32(*iv++));
	stm32_cryp_write(cryp, CRYP_IV0RR, cpu_to_be32(*iv++));

	if (is_aes(cryp)) {
		stm32_cryp_write(cryp, CRYP_IV1LR, cpu_to_be32(*iv++));
		stm32_cryp_write(cryp, CRYP_IV1RR, cpu_to_be32(*iv++));
	}
}

static void stm32_cryp_hw_write_key(struct stm32_cryp *c)
{
	unsigned int i;
	int r_id;

	if (is_des(c)) {
		stm32_cryp_write(c, CRYP_K1LR, cpu_to_be32(c->ctx->key[0]));
		stm32_cryp_write(c, CRYP_K1RR, cpu_to_be32(c->ctx->key[1]));
	} else {
		r_id = CRYP_K3RR;
		for (i = c->ctx->keylen / sizeof(u32); i > 0; i--, r_id -= 4)
			stm32_cryp_write(c, r_id,
					 cpu_to_be32(c->ctx->key[i - 1]));
	}
}

static u32 stm32_cryp_get_hw_mode(struct stm32_cryp *cryp)
{
	if (is_aes(cryp) && is_ecb(cryp))
		return CR_AES_ECB;

	if (is_aes(cryp) && is_cbc(cryp))
		return CR_AES_CBC;

	if (is_aes(cryp) && is_ctr(cryp))
		return CR_AES_CTR;

	if (is_des(cryp) && is_ecb(cryp))
		return CR_DES_ECB;

	if (is_des(cryp) && is_cbc(cryp))
		return CR_DES_CBC;

	if (is_tdes(cryp) && is_ecb(cryp))
		return CR_TDES_ECB;

	if (is_tdes(cryp) && is_cbc(cryp))
		return CR_TDES_CBC;

	dev_err(cryp->dev, "Unknown mode\n");
	return CR_AES_UNKNOWN;
}

static int stm32_cryp_hw_init(struct stm32_cryp *cryp)
{
	int ret;
	u32 cfg, hw_mode;

	/* Disable interrupt */
	stm32_cryp_write(cryp, CRYP_IMSCR, 0);

	/* Set key */
	stm32_cryp_hw_write_key(cryp);

	/* Set configuration */
	cfg = CR_DATA8 | CR_FFLUSH;

	switch (cryp->ctx->keylen) {
	case AES_KEYSIZE_128:
		cfg |= CR_KEY128;
		break;

	case AES_KEYSIZE_192:
		cfg |= CR_KEY192;
		break;

	default:
	case AES_KEYSIZE_256:
		cfg |= CR_KEY256;
		break;
	}

	hw_mode = stm32_cryp_get_hw_mode(cryp);
	if (hw_mode == CR_AES_UNKNOWN)
		return -EINVAL;

	/* AES ECB/CBC decrypt: run key preparation first */
	if (is_decrypt(cryp) &&
	    ((hw_mode == CR_AES_ECB) || (hw_mode == CR_AES_CBC))) {
		stm32_cryp_write(cryp, CRYP_CR, cfg | CR_AES_KP | CR_CRYPEN);

		/* Wait for end of processing */
		ret = stm32_cryp_wait_busy(cryp);
		if (ret) {
			dev_err(cryp->dev, "Timeout (key preparation)\n");
			return ret;
		}
	}

	cfg |= hw_mode;

	if (is_decrypt(cryp))
		cfg |= CR_DEC_NOT_ENC;

	/* Apply config and flush (valid when CRYPEN = 0) */
	stm32_cryp_write(cryp, CRYP_CR, cfg);

	switch (hw_mode) {
	case CR_DES_CBC:
	case CR_TDES_CBC:
	case CR_AES_CBC:
	case CR_AES_CTR:
		stm32_cryp_hw_write_iv(cryp, (u32 *)cryp->req->info);
		break;

	default:
		break;
	}

	/* Enable now */
	cfg |= CR_CRYPEN;

	stm32_cryp_write(cryp, CRYP_CR, cfg);

	return 0;
}

static void stm32_cryp_finish_req(struct stm32_cryp *cryp)
{
	int err = 0;

	if (cryp->sgs_copied) {
		void *buf_in, *buf_out;
		int pages, len;

		buf_in = sg_virt(&cryp->in_sgl);
		buf_out = sg_virt(&cryp->out_sgl);

		sg_copy_buf(buf_out, cryp->out_sg_save, 0,
			    cryp->total_out_save, 1);

		len = ALIGN(cryp->total_in_save, cryp->hw_blocksize);
		pages = len ? get_order(len) : 1;
		free_pages((unsigned long)buf_in, pages);

		len = ALIGN(cryp->total_out_save, cryp->hw_blocksize);
		pages = len ? get_order(len) : 1;
		free_pages((unsigned long)buf_out, pages);
	}

	crypto_finalize_cipher_request(cryp->engine, cryp->req, err);
	cryp->req = NULL;

	memset(cryp->ctx->key, 0, cryp->ctx->keylen);

	mutex_unlock(&cryp->lock);
}

static int stm32_cryp_cpu_start(struct stm32_cryp *cryp)
{
	/* Enable interrupt and let the IRQ handler do everything */
	stm32_cryp_write(cryp, CRYP_IMSCR, IMSCR_IN | IMSCR_OUT);

	return 0;
}

static int stm32_cryp_cra_init(struct crypto_tfm *tfm)
{
	tfm->crt_ablkcipher.reqsize = sizeof(struct stm32_cryp_reqctx);

	return 0;
}

static int stm32_cryp_crypt(struct ablkcipher_request *req, unsigned long mode)
{
	struct stm32_cryp_ctx *ctx = crypto_ablkcipher_ctx(
			crypto_ablkcipher_reqtfm(req));
	struct stm32_cryp_reqctx *rctx = ablkcipher_request_ctx(req);
	struct stm32_cryp *cryp = stm32_cryp_find_dev(ctx);

	if (!cryp)
		return -ENODEV;

	rctx->mode = mode;

	return crypto_transfer_cipher_request_to_engine(cryp->engine, req);
}

static int stm32_cryp_setkey(struct crypto_ablkcipher *tfm, const u8 *key,
			     unsigned int keylen)
{
	struct stm32_cryp_ctx *ctx = crypto_ablkcipher_ctx(tfm);

	memcpy(ctx->key, key, keylen);
	ctx->keylen = keylen;

	return 0;
}

static int stm32_cryp_aes_setkey(struct crypto_ablkcipher *tfm, const u8 *key,
				 unsigned int keylen)
{
	if (keylen != AES_KEYSIZE_128 && keylen != AES_KEYSIZE_192 &&
	    keylen != AES_KEYSIZE_256)
		return -EINVAL;
	else
		return stm32_cryp_setkey(tfm, key, keylen);
}

static int stm32_cryp_des_setkey(struct crypto_ablkcipher *tfm, const u8 *key,
				 unsigned int keylen)
{
	if (keylen != DES_KEY_SIZE)
		return -EINVAL;
	else
		return stm32_cryp_setkey(tfm, key, keylen);
}

static int stm32_cryp_tdes_setkey(struct crypto_ablkcipher *tfm, const u8 *key,
				  unsigned int keylen)
{
	if (keylen != (3 * DES_KEY_SIZE))
		return -EINVAL;
	else
		return stm32_cryp_setkey(tfm, key, keylen);
}

static int stm32_cryp_aes_ecb_encrypt(struct ablkcipher_request *req)
{
	return stm32_cryp_crypt(req, FLG_AES | FLG_ECB | FLG_ENCRYPT);
}

static int stm32_cryp_aes_ecb_decrypt(struct ablkcipher_request *req)
{
	return stm32_cryp_crypt(req, FLG_AES | FLG_ECB);
}

static int stm32_cryp_aes_cbc_encrypt(struct ablkcipher_request *req)
{
	return stm32_cryp_crypt(req, FLG_AES | FLG_CBC | FLG_ENCRYPT);
}

static int stm32_cryp_aes_cbc_decrypt(struct ablkcipher_request *req)
{
	return stm32_cryp_crypt(req, FLG_AES | FLG_CBC);
}

static int stm32_cryp_aes_ctr_encrypt(struct ablkcipher_request *req)
{
	return stm32_cryp_crypt(req, FLG_AES | FLG_CTR | FLG_ENCRYPT);
}

static int stm32_cryp_aes_ctr_decrypt(struct ablkcipher_request *req)
{
	return stm32_cryp_crypt(req, FLG_AES | FLG_CTR);
}

static int stm32_cryp_des_ecb_encrypt(struct ablkcipher_request *req)
{
	return stm32_cryp_crypt(req, FLG_DES | FLG_ECB | FLG_ENCRYPT);
}

static int stm32_cryp_des_ecb_decrypt(struct ablkcipher_request *req)
{
	return stm32_cryp_crypt(req, FLG_DES | FLG_ECB);
}

static int stm32_cryp_des_cbc_encrypt(struct ablkcipher_request *req)
{
	return stm32_cryp_crypt(req, FLG_DES | FLG_CBC | FLG_ENCRYPT);
}

static int stm32_cryp_des_cbc_decrypt(struct ablkcipher_request *req)
{
	return stm32_cryp_crypt(req, FLG_DES | FLG_CBC);
}

static int stm32_cryp_tdes_ecb_encrypt(struct ablkcipher_request *req)
{
	return stm32_cryp_crypt(req, FLG_TDES | FLG_ECB | FLG_ENCRYPT);
}

static int stm32_cryp_tdes_ecb_decrypt(struct ablkcipher_request *req)
{
	return stm32_cryp_crypt(req, FLG_TDES | FLG_ECB);
}

static int stm32_cryp_tdes_cbc_encrypt(struct ablkcipher_request *req)
{
	return stm32_cryp_crypt(req, FLG_TDES | FLG_CBC | FLG_ENCRYPT);
}

static int stm32_cryp_tdes_cbc_decrypt(struct ablkcipher_request *req)
{
	return stm32_cryp_crypt(req, FLG_TDES | FLG_CBC);
}

static int stm32_cryp_prepare_req(struct crypto_engine *engine,
				  struct ablkcipher_request *req)
{
	struct stm32_cryp_ctx *ctx;
	struct stm32_cryp *cryp;
	struct stm32_cryp_reqctx *rctx;
	int ret;

	if (!req)
		return -EINVAL;

	ctx = crypto_ablkcipher_ctx(crypto_ablkcipher_reqtfm(req));

	cryp = ctx->cryp;

	if (!cryp)
		return -ENODEV;

	mutex_lock(&cryp->lock);

	rctx = ablkcipher_request_ctx(req);
	rctx->mode &= FLG_MODE_MASK;

	ctx->cryp = cryp;

	cryp->flags = (cryp->flags & ~FLG_MODE_MASK) | rctx->mode;
	cryp->hw_blocksize = is_aes(cryp) ? AES_BLOCK_SIZE : DES_BLOCK_SIZE;
	cryp->ctx = ctx;

	cryp->req = req;
	cryp->total_in = req->nbytes;
	cryp->total_out = cryp->total_in;

	cryp->total_in_save = cryp->total_in;
	cryp->total_out_save = cryp->total_out;

	cryp->in_sg = req->src;
	cryp->out_sg = req->dst;
	cryp->out_sg_save = cryp->out_sg;

	cryp->in_sg_len = sg_nents_for_len(cryp->in_sg, cryp->total_in);
	if (cryp->in_sg_len < 0) {
		dev_err(cryp->dev, "Cannot get in_sg_len\n");
		ret = cryp->in_sg_len;
		goto out;
	}

	cryp->out_sg_len = sg_nents_for_len(cryp->out_sg, cryp->total_out);
	if (cryp->out_sg_len < 0) {
		dev_err(cryp->dev, "Cannot get out_sg_len\n");
		ret = cryp->out_sg_len;
		goto out;
	}

	ret = stm32_cryp_copy_sgs(cryp);
	if (ret)
		goto out;

	scatterwalk_start(&cryp->in_walk, cryp->in_sg);
	scatterwalk_start(&cryp->out_walk, cryp->out_sg);

	ret = stm32_cryp_hw_init(cryp);
out:
	if (ret)
		mutex_unlock(&cryp->lock);

	return ret;
}

static int stm32_cryp_prepare_cipher_req(struct crypto_engine *engine,
					 struct ablkcipher_request *req)
{
	return stm32_cryp_prepare_req(engine, req);
}

static int stm32_cryp_cipher_one_req(struct crypto_engine *engine,
				     struct ablkcipher_request *req)
{
	struct stm32_cryp_ctx *ctx = crypto_ablkcipher_ctx(
			crypto_ablkcipher_reqtfm(req));
	struct stm32_cryp *cryp = ctx->cryp;

	if (!cryp)
		return -ENODEV;

	return stm32_cryp_cpu_start(cryp);
}

static u32 *stm32_cryp_next_out(struct stm32_cryp *cryp, u32 *dst,
				unsigned int n)
{
	scatterwalk_advance(&cryp->out_walk, n);

	if (unlikely(cryp->out_sg->length == _walked_out)) {
		cryp->out_sg = sg_next(cryp->out_sg);
		if (cryp->out_sg) {
			scatterwalk_start(&cryp->out_walk, cryp->out_sg);
			return (sg_virt(cryp->out_sg) + _walked_out);
		}
	}

	return (u32 *)((u8 *)dst + n);
}

static u32 *stm32_cryp_next_in(struct stm32_cryp *cryp, u32 *src,
			       unsigned int n)
{
	scatterwalk_advance(&cryp->in_walk, n);

	if (unlikely(cryp->in_sg->length == _walked_in)) {
		cryp->in_sg = sg_next(cryp->in_sg);
		if (cryp->in_sg) {
			scatterwalk_start(&cryp->in_walk, cryp->in_sg);
			return (sg_virt(cryp->in_sg) + _walked_in);
		}
	}

	return (u32 *)((u8 *)src + n);
}

static void stm32_cryp_check_ctr_counter(struct stm32_cryp *cryp)
{
	u32 cr;

	if (unlikely(cryp->last_ctr[3] == 0xFFFFFFFF)) {
		cryp->last_ctr[3] = 0;
		cryp->last_ctr[2]++;
		if (!cryp->last_ctr[2]) {
			cryp->last_ctr[1]++;
			if (!cryp->last_ctr[1])
				cryp->last_ctr[0]++;
		}

		cr = stm32_cryp_read(cryp, CRYP_CR);
		stm32_cryp_write(cryp, CRYP_CR, cr & ~CR_CRYPEN);

		stm32_cryp_hw_write_iv(cryp, (u32 *)cryp->last_ctr);

		stm32_cryp_write(cryp, CRYP_CR, cr);
	}

	cryp->last_ctr[0] = stm32_cryp_read(cryp, CRYP_IV0LR);
	cryp->last_ctr[1] = stm32_cryp_read(cryp, CRYP_IV0RR);
	cryp->last_ctr[2] = stm32_cryp_read(cryp, CRYP_IV1LR);
	cryp->last_ctr[3] = stm32_cryp_read(cryp, CRYP_IV1RR);
}

static bool stm32_cryp_irq_read_data(struct stm32_cryp *cryp)
{
	unsigned int i, j;
	u32 d32, *dst;
	u8 *d8;

	dst = sg_virt(cryp->out_sg) + _walked_out;

	for (i = 0; i < cryp->hw_blocksize / sizeof(u32); i++) {
		if (likely(cryp->total_out >= sizeof(u32))) {
			/* Read a full u32 */
			*dst = stm32_cryp_read(cryp, CRYP_DOUT);

			dst = stm32_cryp_next_out(cryp, dst, sizeof(u32));
			cryp->total_out -= sizeof(u32);
		} else if (!cryp->total_out) {
			/* Empty fifo out (data from input padding) */
			d32 = stm32_cryp_read(cryp, CRYP_DOUT);
		} else {
			/* Read less than an u32 */
			d32 = stm32_cryp_read(cryp, CRYP_DOUT);
			d8 = (u8 *)&d32;

			for (j = 0; j < cryp->total_out; j++) {
				*((u8 *)dst) = *(d8++);
				dst = stm32_cryp_next_out(cryp, dst, 1);
			}
			cryp->total_out = 0;
		}
	}

	return !cryp->total_out || !cryp->total_in;
}

static void stm32_cryp_irq_write_block(struct stm32_cryp *cryp)
{
	unsigned int i, j;
	u32 *src;
	u8 d8[4];

	src = sg_virt(cryp->in_sg) + _walked_in;

	for (i = 0; i < cryp->hw_blocksize / sizeof(u32); i++) {
		if (likely(cryp->total_in >= sizeof(u32))) {
			/* Write a full u32 */
			stm32_cryp_write(cryp, CRYP_DIN, *src);

			src = stm32_cryp_next_in(cryp, src, sizeof(u32));
			cryp->total_in -= sizeof(u32);
		} else if (!cryp->total_in) {
			/* Write padding data */
			stm32_cryp_write(cryp, CRYP_DIN, 0);
		} else {
			/* Write less than an u32 */
			memset(d8, 0, sizeof(u32));
			for (j = 0; j < cryp->total_in; j++) {
				d8[j] = *((u8 *)src);
				src = stm32_cryp_next_in(cryp, src, 1);
			}

			stm32_cryp_write(cryp, CRYP_DIN, *(u32 *)d8);
			cryp->total_in = 0;
		}
	}
}

static void stm32_cryp_irq_write_data(struct stm32_cryp *cryp)
{
	if (unlikely(!cryp->total_in)) {
		dev_warn(cryp->dev, "No more data to process\n");
		return;
	}

	if (is_aes(cryp) && is_ctr(cryp))
		stm32_cryp_check_ctr_counter(cryp);

	stm32_cryp_irq_write_block(cryp);
}

static irqreturn_t stm32_cryp_irq_thread(int irq, void *arg)
{
	struct stm32_cryp *cryp = arg;

	if (cryp->irq_status & MISR_OUT)
		/* Output FIFO IRQ: read data */
		if (unlikely(stm32_cryp_irq_read_data(cryp))) {
			/* All bytes processed, finish */
			stm32_cryp_write(cryp, CRYP_IMSCR, 0);
			stm32_cryp_finish_req(cryp);
			return IRQ_HANDLED;
		}

	if (cryp->irq_status & MISR_IN) {
		/* Input FIFO IRQ: write data */
		stm32_cryp_irq_write_data(cryp);
	}

	return IRQ_HANDLED;
}

static irqreturn_t stm32_cryp_irq(int irq, void *arg)
{
	struct stm32_cryp *cryp = arg;

	cryp->irq_status = stm32_cryp_read(cryp, CRYP_MISR);

	return IRQ_WAKE_THREAD;
}

static struct crypto_alg crypto_algs[] = {
{
	.cra_name		= "ecb(aes)",
	.cra_driver_name	= "stm32-ecb-aes",
	.cra_priority		= 200,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER |
				  CRYPTO_ALG_ASYNC,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct stm32_cryp_ctx),
	.cra_alignmask		= 0xf,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= stm32_cryp_cra_init,
	.cra_ablkcipher = {
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.setkey		= stm32_cryp_aes_setkey,
		.encrypt	= stm32_cryp_aes_ecb_encrypt,
		.decrypt	= stm32_cryp_aes_ecb_decrypt,
	}
},
{
	.cra_name		= "cbc(aes)",
	.cra_driver_name	= "stm32-cbc-aes",
	.cra_priority		= 200,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER |
				  CRYPTO_ALG_ASYNC,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct stm32_cryp_ctx),
	.cra_alignmask		= 0xf,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= stm32_cryp_cra_init,
	.cra_ablkcipher = {
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.ivsize		= AES_BLOCK_SIZE,
		.setkey		= stm32_cryp_aes_setkey,
		.encrypt	= stm32_cryp_aes_cbc_encrypt,
		.decrypt	= stm32_cryp_aes_cbc_decrypt,
	}
},
{
	.cra_name		= "ctr(aes)",
	.cra_driver_name	= "stm32-ctr-aes",
	.cra_priority		= 200,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER |
				  CRYPTO_ALG_ASYNC,
	.cra_blocksize		= 1,
	.cra_ctxsize		= sizeof(struct stm32_cryp_ctx),
	.cra_alignmask		= 0xf,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= stm32_cryp_cra_init,
	.cra_ablkcipher = {
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.ivsize		= AES_BLOCK_SIZE,
		.setkey		= stm32_cryp_aes_setkey,
		.encrypt	= stm32_cryp_aes_ctr_encrypt,
		.decrypt	= stm32_cryp_aes_ctr_decrypt,
	}
},
{
	.cra_name		= "ecb(des)",
	.cra_driver_name	= "stm32-ecb-des",
	.cra_priority		= 200,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER |
				  CRYPTO_ALG_ASYNC,
	.cra_blocksize		= DES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct stm32_cryp_ctx),
	.cra_alignmask		= 0xf,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= stm32_cryp_cra_init,
	.cra_ablkcipher = {
		.min_keysize	= DES_BLOCK_SIZE,
		.max_keysize	= DES_BLOCK_SIZE,
		.setkey		= stm32_cryp_des_setkey,
		.encrypt	= stm32_cryp_des_ecb_encrypt,
		.decrypt	= stm32_cryp_des_ecb_decrypt,
	}
},
{
	.cra_name		= "cbc(des)",
	.cra_driver_name	= "stm32-cbc-des",
	.cra_priority		= 200,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER |
				  CRYPTO_ALG_ASYNC,
	.cra_blocksize		= DES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct stm32_cryp_ctx),
	.cra_alignmask		= 0xf,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= stm32_cryp_cra_init,
	.cra_ablkcipher = {
		.min_keysize	= DES_BLOCK_SIZE,
		.max_keysize	= DES_BLOCK_SIZE,
		.ivsize		= DES_BLOCK_SIZE,
		.setkey		= stm32_cryp_des_setkey,
		.encrypt	= stm32_cryp_des_cbc_encrypt,
		.decrypt	= stm32_cryp_des_cbc_decrypt,
	}
},
{
	.cra_name		= "ecb(des3_ede)",
	.cra_driver_name	= "stm32-ecb-des3",
	.cra_priority		= 200,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER |
				  CRYPTO_ALG_ASYNC,
	.cra_blocksize		= DES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct stm32_cryp_ctx),
	.cra_alignmask		= 0xf,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= stm32_cryp_cra_init,
	.cra_ablkcipher = {
		.min_keysize	= 3 * DES_BLOCK_SIZE,
		.max_keysize	= 3 * DES_BLOCK_SIZE,
		.setkey		= stm32_cryp_tdes_setkey,
		.encrypt	= stm32_cryp_tdes_ecb_encrypt,
		.decrypt	= stm32_cryp_tdes_ecb_decrypt,
	}
},
{
	.cra_name		= "cbc(des3_ede)",
	.cra_driver_name	= "stm32-cbc-des3",
	.cra_priority		= 200,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER |
				  CRYPTO_ALG_ASYNC,
	.cra_blocksize		= DES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct stm32_cryp_ctx),
	.cra_alignmask		= 0xf,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= stm32_cryp_cra_init,
	.cra_ablkcipher = {
		.min_keysize	= 3 * DES_BLOCK_SIZE,
		.max_keysize	= 3 * DES_BLOCK_SIZE,
		.ivsize		= DES_BLOCK_SIZE,
		.setkey		= stm32_cryp_tdes_setkey,
		.encrypt	= stm32_cryp_tdes_cbc_encrypt,
		.decrypt	= stm32_cryp_tdes_cbc_decrypt,
	}
},
};

static const struct of_device_id stm32_dt_ids[] = {
	{ .compatible = "st,stm32f756-cryp", },
	{},
};
MODULE_DEVICE_TABLE(of, stm32_dt_ids);

static int stm32_cryp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct stm32_cryp *cryp;
	struct resource *res;
	struct reset_control *rst;
	int irq, ret;

	cryp = devm_kzalloc(dev, sizeof(*cryp), GFP_KERNEL);
	if (!cryp)
		return -ENOMEM;

	cryp->dev = dev;

	mutex_init(&cryp->lock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	cryp->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(cryp->regs)) {
		dev_err(dev, "Cannot map CRYP IO\n");
		return PTR_ERR(cryp->regs);
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "Cannot get IRQ resource\n");
		return irq;
	}

	ret = devm_request_threaded_irq(dev, irq, stm32_cryp_irq,
					stm32_cryp_irq_thread, IRQF_ONESHOT,
					dev_name(dev), cryp);
	if (ret) {
		dev_err(dev, "Cannot grab IRQ\n");
		return ret;
	}

	cryp->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(cryp->clk)) {
		dev_err(dev, "Could not get clock\n");
		return PTR_ERR(cryp->clk);
	}

	ret = clk_prepare_enable(cryp->clk);
	if (ret) {
		dev_err(cryp->dev, "Failed to enable clock\n");
		return ret;
	}

	rst = devm_reset_control_get(dev, NULL);
	if (!IS_ERR(rst)) {
		reset_control_assert(rst);
		udelay(2);
		reset_control_deassert(rst);
	}

	platform_set_drvdata(pdev, cryp);

	spin_lock(&cryp_list.lock);
	list_add(&cryp->list, &cryp_list.dev_list);
	spin_unlock(&cryp_list.lock);

	/* Initialize crypto engine */
	cryp->engine = crypto_engine_alloc_init(dev, 1);
	if (!cryp->engine) {
		dev_err(dev, "Could not init crypto engine\n");
		ret = -ENOMEM;
		goto err_engine1;
	}

	cryp->engine->prepare_cipher_request = stm32_cryp_prepare_cipher_req;
	cryp->engine->cipher_one_request = stm32_cryp_cipher_one_req;

	ret = crypto_engine_start(cryp->engine);
	if (ret) {
		dev_err(dev, "Could not start crypto engine\n");
		goto err_engine2;
	}

	ret = crypto_register_algs(crypto_algs, ARRAY_SIZE(crypto_algs));
	if (ret) {
		dev_err(dev, "Could not register algs\n");
		goto err_algs;
	}

	dev_info(dev, "Initialized\n");

	return 0;

err_algs:
err_engine2:
	crypto_engine_exit(cryp->engine);
err_engine1:
	spin_lock(&cryp_list.lock);
	list_del(&cryp->list);
	spin_unlock(&cryp_list.lock);

	clk_disable_unprepare(cryp->clk);

	return ret;
}

static int stm32_cryp_remove(struct platform_device *pdev)
{
	struct stm32_cryp *cryp = platform_get_drvdata(pdev);

	if (!cryp)
		return -ENODEV;

	crypto_unregister_algs(crypto_algs, ARRAY_SIZE(crypto_algs));

	crypto_engine_exit(cryp->engine);

	spin_lock(&cryp_list.lock);
	list_del(&cryp->list);
	spin_unlock(&cryp_list.lock);

	clk_disable_unprepare(cryp->clk);

	return 0;
}

static struct platform_driver stm32_cryp_driver = {
	.probe  = stm32_cryp_probe,
	.remove = stm32_cryp_remove,
	.driver = {
		.name           = DRIVER_NAME,
		.of_match_table = stm32_dt_ids,
	},
};

module_platform_driver(stm32_cryp_driver);

MODULE_AUTHOR("Fabien Dessenne <fabien.dessenne@st.com>");
MODULE_DESCRIPTION("STMicrolectronics STM32 CRYP hardware driver");
MODULE_LICENSE("GPL");
