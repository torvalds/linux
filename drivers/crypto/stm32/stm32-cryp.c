// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) STMicroelectronics SA 2017
 * Author: Fabien Dessenne <fabien.dessenne@st.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

#include <crypto/aes.h>
#include <crypto/internal/des.h>
#include <crypto/engine.h>
#include <crypto/scatterwalk.h>
#include <crypto/internal/aead.h>
#include <crypto/internal/skcipher.h>

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
#define FLG_GCM                 BIT(7)
#define FLG_CCM                 BIT(8)
/* Mode mask = bits [15..0] */
#define FLG_MODE_MASK           GENMASK(15, 0)
/* Bit [31..16] status  */
#define FLG_CCM_PADDED_WA       BIT(16)

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
#define CRYP_CSGCMCCM0R         0x00000050
#define CRYP_CSGCM0R            0x00000070

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
#define CR_AES_GCM              0x00080000
#define CR_AES_CCM              0x00080008
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
#define CR_PH_INIT              0x00000000
#define CR_PH_HEADER            0x00010000
#define CR_PH_PAYLOAD           0x00020000
#define CR_PH_FINAL             0x00030000
#define CR_PH_MASK              0x00030000
#define CR_NBPBL_SHIFT          20

#define SR_BUSY                 0x00000010
#define SR_OFNE                 0x00000004

#define IMSCR_IN                BIT(0)
#define IMSCR_OUT               BIT(1)

#define MISR_IN                 BIT(0)
#define MISR_OUT                BIT(1)

/* Misc */
#define AES_BLOCK_32            (AES_BLOCK_SIZE / sizeof(u32))
#define GCM_CTR_INIT            2
#define _walked_in              (cryp->in_walk.offset - cryp->in_sg->offset)
#define _walked_out             (cryp->out_walk.offset - cryp->out_sg->offset)
#define CRYP_AUTOSUSPEND_DELAY	50

struct stm32_cryp_caps {
	bool                    swap_final;
	bool                    padding_wa;
};

struct stm32_cryp_ctx {
	struct crypto_engine_ctx enginectx;
	struct stm32_cryp       *cryp;
	int                     keylen;
	__be32                  key[AES_KEYSIZE_256 / sizeof(u32)];
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
	const struct stm32_cryp_caps *caps;
	struct stm32_cryp_ctx   *ctx;

	struct crypto_engine    *engine;

	struct skcipher_request *req;
	struct aead_request     *areq;

	size_t                  authsize;
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
	u32                     gcm_ctr;
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

static inline bool is_gcm(struct stm32_cryp *cryp)
{
	return cryp->flags & FLG_GCM;
}

static inline bool is_ccm(struct stm32_cryp *cryp)
{
	return cryp->flags & FLG_CCM;
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

static inline int stm32_cryp_wait_enable(struct stm32_cryp *cryp)
{
	u32 status;

	return readl_relaxed_poll_timeout(cryp->regs + CRYP_CR, status,
			!(status & CR_CRYPEN), 10, 100000);
}

static inline int stm32_cryp_wait_output(struct stm32_cryp *cryp)
{
	u32 status;

	return readl_relaxed_poll_timeout(cryp->regs + CRYP_SR, status,
			status & SR_OFNE, 10, 100000);
}

static int stm32_cryp_read_auth_tag(struct stm32_cryp *cryp);

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

static void stm32_cryp_hw_write_iv(struct stm32_cryp *cryp, __be32 *iv)
{
	if (!iv)
		return;

	stm32_cryp_write(cryp, CRYP_IV0LR, be32_to_cpu(*iv++));
	stm32_cryp_write(cryp, CRYP_IV0RR, be32_to_cpu(*iv++));

	if (is_aes(cryp)) {
		stm32_cryp_write(cryp, CRYP_IV1LR, be32_to_cpu(*iv++));
		stm32_cryp_write(cryp, CRYP_IV1RR, be32_to_cpu(*iv++));
	}
}

static void stm32_cryp_get_iv(struct stm32_cryp *cryp)
{
	struct skcipher_request *req = cryp->req;
	__be32 *tmp = (void *)req->iv;

	if (!tmp)
		return;

	*tmp++ = cpu_to_be32(stm32_cryp_read(cryp, CRYP_IV0LR));
	*tmp++ = cpu_to_be32(stm32_cryp_read(cryp, CRYP_IV0RR));

	if (is_aes(cryp)) {
		*tmp++ = cpu_to_be32(stm32_cryp_read(cryp, CRYP_IV1LR));
		*tmp++ = cpu_to_be32(stm32_cryp_read(cryp, CRYP_IV1RR));
	}
}

static void stm32_cryp_hw_write_key(struct stm32_cryp *c)
{
	unsigned int i;
	int r_id;

	if (is_des(c)) {
		stm32_cryp_write(c, CRYP_K1LR, be32_to_cpu(c->ctx->key[0]));
		stm32_cryp_write(c, CRYP_K1RR, be32_to_cpu(c->ctx->key[1]));
	} else {
		r_id = CRYP_K3RR;
		for (i = c->ctx->keylen / sizeof(u32); i > 0; i--, r_id -= 4)
			stm32_cryp_write(c, r_id,
					 be32_to_cpu(c->ctx->key[i - 1]));
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

	if (is_aes(cryp) && is_gcm(cryp))
		return CR_AES_GCM;

	if (is_aes(cryp) && is_ccm(cryp))
		return CR_AES_CCM;

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

static unsigned int stm32_cryp_get_input_text_len(struct stm32_cryp *cryp)
{
	return is_encrypt(cryp) ? cryp->areq->cryptlen :
				  cryp->areq->cryptlen - cryp->authsize;
}

static int stm32_cryp_gcm_init(struct stm32_cryp *cryp, u32 cfg)
{
	int ret;
	__be32 iv[4];

	/* Phase 1 : init */
	memcpy(iv, cryp->areq->iv, 12);
	iv[3] = cpu_to_be32(GCM_CTR_INIT);
	cryp->gcm_ctr = GCM_CTR_INIT;
	stm32_cryp_hw_write_iv(cryp, iv);

	stm32_cryp_write(cryp, CRYP_CR, cfg | CR_PH_INIT | CR_CRYPEN);

	/* Wait for end of processing */
	ret = stm32_cryp_wait_enable(cryp);
	if (ret)
		dev_err(cryp->dev, "Timeout (gcm init)\n");

	return ret;
}

static int stm32_cryp_ccm_init(struct stm32_cryp *cryp, u32 cfg)
{
	int ret;
	u8 iv[AES_BLOCK_SIZE], b0[AES_BLOCK_SIZE];
	__be32 *bd;
	u32 *d;
	unsigned int i, textlen;

	/* Phase 1 : init. Firstly set the CTR value to 1 (not 0) */
	memcpy(iv, cryp->areq->iv, AES_BLOCK_SIZE);
	memset(iv + AES_BLOCK_SIZE - 1 - iv[0], 0, iv[0] + 1);
	iv[AES_BLOCK_SIZE - 1] = 1;
	stm32_cryp_hw_write_iv(cryp, (__be32 *)iv);

	/* Build B0 */
	memcpy(b0, iv, AES_BLOCK_SIZE);

	b0[0] |= (8 * ((cryp->authsize - 2) / 2));

	if (cryp->areq->assoclen)
		b0[0] |= 0x40;

	textlen = stm32_cryp_get_input_text_len(cryp);

	b0[AES_BLOCK_SIZE - 2] = textlen >> 8;
	b0[AES_BLOCK_SIZE - 1] = textlen & 0xFF;

	/* Enable HW */
	stm32_cryp_write(cryp, CRYP_CR, cfg | CR_PH_INIT | CR_CRYPEN);

	/* Write B0 */
	d = (u32 *)b0;
	bd = (__be32 *)b0;

	for (i = 0; i < AES_BLOCK_32; i++) {
		u32 xd = d[i];

		if (!cryp->caps->padding_wa)
			xd = be32_to_cpu(bd[i]);
		stm32_cryp_write(cryp, CRYP_DIN, xd);
	}

	/* Wait for end of processing */
	ret = stm32_cryp_wait_enable(cryp);
	if (ret)
		dev_err(cryp->dev, "Timeout (ccm init)\n");

	return ret;
}

static int stm32_cryp_hw_init(struct stm32_cryp *cryp)
{
	int ret;
	u32 cfg, hw_mode;

	pm_runtime_get_sync(cryp->dev);

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
	case CR_AES_GCM:
	case CR_AES_CCM:
		/* Phase 1 : init */
		if (hw_mode == CR_AES_CCM)
			ret = stm32_cryp_ccm_init(cryp, cfg);
		else
			ret = stm32_cryp_gcm_init(cryp, cfg);

		if (ret)
			return ret;

		/* Phase 2 : header (authenticated data) */
		if (cryp->areq->assoclen) {
			cfg |= CR_PH_HEADER;
		} else if (stm32_cryp_get_input_text_len(cryp)) {
			cfg |= CR_PH_PAYLOAD;
			stm32_cryp_write(cryp, CRYP_CR, cfg);
		} else {
			cfg |= CR_PH_INIT;
		}

		break;

	case CR_DES_CBC:
	case CR_TDES_CBC:
	case CR_AES_CBC:
	case CR_AES_CTR:
		stm32_cryp_hw_write_iv(cryp, (__be32 *)cryp->req->iv);
		break;

	default:
		break;
	}

	/* Enable now */
	cfg |= CR_CRYPEN;

	stm32_cryp_write(cryp, CRYP_CR, cfg);

	cryp->flags &= ~FLG_CCM_PADDED_WA;

	return 0;
}

static void stm32_cryp_finish_req(struct stm32_cryp *cryp, int err)
{
	if (!err && (is_gcm(cryp) || is_ccm(cryp)))
		/* Phase 4 : output tag */
		err = stm32_cryp_read_auth_tag(cryp);

	if (!err && (!(is_gcm(cryp) || is_ccm(cryp))))
		stm32_cryp_get_iv(cryp);

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

	pm_runtime_mark_last_busy(cryp->dev);
	pm_runtime_put_autosuspend(cryp->dev);

	if (is_gcm(cryp) || is_ccm(cryp))
		crypto_finalize_aead_request(cryp->engine, cryp->areq, err);
	else
		crypto_finalize_skcipher_request(cryp->engine, cryp->req,
						   err);

	memset(cryp->ctx->key, 0, cryp->ctx->keylen);
}

static int stm32_cryp_cpu_start(struct stm32_cryp *cryp)
{
	/* Enable interrupt and let the IRQ handler do everything */
	stm32_cryp_write(cryp, CRYP_IMSCR, IMSCR_IN | IMSCR_OUT);

	return 0;
}

static int stm32_cryp_cipher_one_req(struct crypto_engine *engine, void *areq);
static int stm32_cryp_prepare_cipher_req(struct crypto_engine *engine,
					 void *areq);

static int stm32_cryp_init_tfm(struct crypto_skcipher *tfm)
{
	struct stm32_cryp_ctx *ctx = crypto_skcipher_ctx(tfm);

	crypto_skcipher_set_reqsize(tfm, sizeof(struct stm32_cryp_reqctx));

	ctx->enginectx.op.do_one_request = stm32_cryp_cipher_one_req;
	ctx->enginectx.op.prepare_request = stm32_cryp_prepare_cipher_req;
	ctx->enginectx.op.unprepare_request = NULL;
	return 0;
}

static int stm32_cryp_aead_one_req(struct crypto_engine *engine, void *areq);
static int stm32_cryp_prepare_aead_req(struct crypto_engine *engine,
				       void *areq);

static int stm32_cryp_aes_aead_init(struct crypto_aead *tfm)
{
	struct stm32_cryp_ctx *ctx = crypto_aead_ctx(tfm);

	tfm->reqsize = sizeof(struct stm32_cryp_reqctx);

	ctx->enginectx.op.do_one_request = stm32_cryp_aead_one_req;
	ctx->enginectx.op.prepare_request = stm32_cryp_prepare_aead_req;
	ctx->enginectx.op.unprepare_request = NULL;

	return 0;
}

static int stm32_cryp_crypt(struct skcipher_request *req, unsigned long mode)
{
	struct stm32_cryp_ctx *ctx = crypto_skcipher_ctx(
			crypto_skcipher_reqtfm(req));
	struct stm32_cryp_reqctx *rctx = skcipher_request_ctx(req);
	struct stm32_cryp *cryp = stm32_cryp_find_dev(ctx);

	if (!cryp)
		return -ENODEV;

	rctx->mode = mode;

	return crypto_transfer_skcipher_request_to_engine(cryp->engine, req);
}

static int stm32_cryp_aead_crypt(struct aead_request *req, unsigned long mode)
{
	struct stm32_cryp_ctx *ctx = crypto_aead_ctx(crypto_aead_reqtfm(req));
	struct stm32_cryp_reqctx *rctx = aead_request_ctx(req);
	struct stm32_cryp *cryp = stm32_cryp_find_dev(ctx);

	if (!cryp)
		return -ENODEV;

	rctx->mode = mode;

	return crypto_transfer_aead_request_to_engine(cryp->engine, req);
}

static int stm32_cryp_setkey(struct crypto_skcipher *tfm, const u8 *key,
			     unsigned int keylen)
{
	struct stm32_cryp_ctx *ctx = crypto_skcipher_ctx(tfm);

	memcpy(ctx->key, key, keylen);
	ctx->keylen = keylen;

	return 0;
}

static int stm32_cryp_aes_setkey(struct crypto_skcipher *tfm, const u8 *key,
				 unsigned int keylen)
{
	if (keylen != AES_KEYSIZE_128 && keylen != AES_KEYSIZE_192 &&
	    keylen != AES_KEYSIZE_256)
		return -EINVAL;
	else
		return stm32_cryp_setkey(tfm, key, keylen);
}

static int stm32_cryp_des_setkey(struct crypto_skcipher *tfm, const u8 *key,
				 unsigned int keylen)
{
	return verify_skcipher_des_key(tfm, key) ?:
	       stm32_cryp_setkey(tfm, key, keylen);
}

static int stm32_cryp_tdes_setkey(struct crypto_skcipher *tfm, const u8 *key,
				  unsigned int keylen)
{
	return verify_skcipher_des3_key(tfm, key) ?:
	       stm32_cryp_setkey(tfm, key, keylen);
}

static int stm32_cryp_aes_aead_setkey(struct crypto_aead *tfm, const u8 *key,
				      unsigned int keylen)
{
	struct stm32_cryp_ctx *ctx = crypto_aead_ctx(tfm);

	if (keylen != AES_KEYSIZE_128 && keylen != AES_KEYSIZE_192 &&
	    keylen != AES_KEYSIZE_256)
		return -EINVAL;

	memcpy(ctx->key, key, keylen);
	ctx->keylen = keylen;

	return 0;
}

static int stm32_cryp_aes_gcm_setauthsize(struct crypto_aead *tfm,
					  unsigned int authsize)
{
	return authsize == AES_BLOCK_SIZE ? 0 : -EINVAL;
}

static int stm32_cryp_aes_ccm_setauthsize(struct crypto_aead *tfm,
					  unsigned int authsize)
{
	switch (authsize) {
	case 4:
	case 6:
	case 8:
	case 10:
	case 12:
	case 14:
	case 16:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int stm32_cryp_aes_ecb_encrypt(struct skcipher_request *req)
{
	return stm32_cryp_crypt(req, FLG_AES | FLG_ECB | FLG_ENCRYPT);
}

static int stm32_cryp_aes_ecb_decrypt(struct skcipher_request *req)
{
	return stm32_cryp_crypt(req, FLG_AES | FLG_ECB);
}

static int stm32_cryp_aes_cbc_encrypt(struct skcipher_request *req)
{
	return stm32_cryp_crypt(req, FLG_AES | FLG_CBC | FLG_ENCRYPT);
}

static int stm32_cryp_aes_cbc_decrypt(struct skcipher_request *req)
{
	return stm32_cryp_crypt(req, FLG_AES | FLG_CBC);
}

static int stm32_cryp_aes_ctr_encrypt(struct skcipher_request *req)
{
	return stm32_cryp_crypt(req, FLG_AES | FLG_CTR | FLG_ENCRYPT);
}

static int stm32_cryp_aes_ctr_decrypt(struct skcipher_request *req)
{
	return stm32_cryp_crypt(req, FLG_AES | FLG_CTR);
}

static int stm32_cryp_aes_gcm_encrypt(struct aead_request *req)
{
	return stm32_cryp_aead_crypt(req, FLG_AES | FLG_GCM | FLG_ENCRYPT);
}

static int stm32_cryp_aes_gcm_decrypt(struct aead_request *req)
{
	return stm32_cryp_aead_crypt(req, FLG_AES | FLG_GCM);
}

static int stm32_cryp_aes_ccm_encrypt(struct aead_request *req)
{
	return stm32_cryp_aead_crypt(req, FLG_AES | FLG_CCM | FLG_ENCRYPT);
}

static int stm32_cryp_aes_ccm_decrypt(struct aead_request *req)
{
	return stm32_cryp_aead_crypt(req, FLG_AES | FLG_CCM);
}

static int stm32_cryp_des_ecb_encrypt(struct skcipher_request *req)
{
	return stm32_cryp_crypt(req, FLG_DES | FLG_ECB | FLG_ENCRYPT);
}

static int stm32_cryp_des_ecb_decrypt(struct skcipher_request *req)
{
	return stm32_cryp_crypt(req, FLG_DES | FLG_ECB);
}

static int stm32_cryp_des_cbc_encrypt(struct skcipher_request *req)
{
	return stm32_cryp_crypt(req, FLG_DES | FLG_CBC | FLG_ENCRYPT);
}

static int stm32_cryp_des_cbc_decrypt(struct skcipher_request *req)
{
	return stm32_cryp_crypt(req, FLG_DES | FLG_CBC);
}

static int stm32_cryp_tdes_ecb_encrypt(struct skcipher_request *req)
{
	return stm32_cryp_crypt(req, FLG_TDES | FLG_ECB | FLG_ENCRYPT);
}

static int stm32_cryp_tdes_ecb_decrypt(struct skcipher_request *req)
{
	return stm32_cryp_crypt(req, FLG_TDES | FLG_ECB);
}

static int stm32_cryp_tdes_cbc_encrypt(struct skcipher_request *req)
{
	return stm32_cryp_crypt(req, FLG_TDES | FLG_CBC | FLG_ENCRYPT);
}

static int stm32_cryp_tdes_cbc_decrypt(struct skcipher_request *req)
{
	return stm32_cryp_crypt(req, FLG_TDES | FLG_CBC);
}

static int stm32_cryp_prepare_req(struct skcipher_request *req,
				  struct aead_request *areq)
{
	struct stm32_cryp_ctx *ctx;
	struct stm32_cryp *cryp;
	struct stm32_cryp_reqctx *rctx;
	int ret;

	if (!req && !areq)
		return -EINVAL;

	ctx = req ? crypto_skcipher_ctx(crypto_skcipher_reqtfm(req)) :
		    crypto_aead_ctx(crypto_aead_reqtfm(areq));

	cryp = ctx->cryp;

	if (!cryp)
		return -ENODEV;

	rctx = req ? skcipher_request_ctx(req) : aead_request_ctx(areq);
	rctx->mode &= FLG_MODE_MASK;

	ctx->cryp = cryp;

	cryp->flags = (cryp->flags & ~FLG_MODE_MASK) | rctx->mode;
	cryp->hw_blocksize = is_aes(cryp) ? AES_BLOCK_SIZE : DES_BLOCK_SIZE;
	cryp->ctx = ctx;

	if (req) {
		cryp->req = req;
		cryp->areq = NULL;
		cryp->total_in = req->cryptlen;
		cryp->total_out = cryp->total_in;
	} else {
		/*
		 * Length of input and output data:
		 * Encryption case:
		 *  INPUT  =   AssocData  ||   PlainText
		 *          <- assoclen ->  <- cryptlen ->
		 *          <------- total_in ----------->
		 *
		 *  OUTPUT =   AssocData  ||  CipherText  ||   AuthTag
		 *          <- assoclen ->  <- cryptlen ->  <- authsize ->
		 *          <---------------- total_out ----------------->
		 *
		 * Decryption case:
		 *  INPUT  =   AssocData  ||  CipherText  ||  AuthTag
		 *          <- assoclen ->  <--------- cryptlen --------->
		 *                                          <- authsize ->
		 *          <---------------- total_in ------------------>
		 *
		 *  OUTPUT =   AssocData  ||   PlainText
		 *          <- assoclen ->  <- crypten - authsize ->
		 *          <---------- total_out ----------------->
		 */
		cryp->areq = areq;
		cryp->req = NULL;
		cryp->authsize = crypto_aead_authsize(crypto_aead_reqtfm(areq));
		cryp->total_in = areq->assoclen + areq->cryptlen;
		if (is_encrypt(cryp))
			/* Append auth tag to output */
			cryp->total_out = cryp->total_in + cryp->authsize;
		else
			/* No auth tag in output */
			cryp->total_out = cryp->total_in - cryp->authsize;
	}

	cryp->total_in_save = cryp->total_in;
	cryp->total_out_save = cryp->total_out;

	cryp->in_sg = req ? req->src : areq->src;
	cryp->out_sg = req ? req->dst : areq->dst;
	cryp->out_sg_save = cryp->out_sg;

	cryp->in_sg_len = sg_nents_for_len(cryp->in_sg, cryp->total_in);
	if (cryp->in_sg_len < 0) {
		dev_err(cryp->dev, "Cannot get in_sg_len\n");
		ret = cryp->in_sg_len;
		return ret;
	}

	cryp->out_sg_len = sg_nents_for_len(cryp->out_sg, cryp->total_out);
	if (cryp->out_sg_len < 0) {
		dev_err(cryp->dev, "Cannot get out_sg_len\n");
		ret = cryp->out_sg_len;
		return ret;
	}

	ret = stm32_cryp_copy_sgs(cryp);
	if (ret)
		return ret;

	scatterwalk_start(&cryp->in_walk, cryp->in_sg);
	scatterwalk_start(&cryp->out_walk, cryp->out_sg);

	if (is_gcm(cryp) || is_ccm(cryp)) {
		/* In output, jump after assoc data */
		scatterwalk_advance(&cryp->out_walk, cryp->areq->assoclen);
		cryp->total_out -= cryp->areq->assoclen;
	}

	ret = stm32_cryp_hw_init(cryp);
	return ret;
}

static int stm32_cryp_prepare_cipher_req(struct crypto_engine *engine,
					 void *areq)
{
	struct skcipher_request *req = container_of(areq,
						      struct skcipher_request,
						      base);

	return stm32_cryp_prepare_req(req, NULL);
}

static int stm32_cryp_cipher_one_req(struct crypto_engine *engine, void *areq)
{
	struct skcipher_request *req = container_of(areq,
						      struct skcipher_request,
						      base);
	struct stm32_cryp_ctx *ctx = crypto_skcipher_ctx(
			crypto_skcipher_reqtfm(req));
	struct stm32_cryp *cryp = ctx->cryp;

	if (!cryp)
		return -ENODEV;

	return stm32_cryp_cpu_start(cryp);
}

static int stm32_cryp_prepare_aead_req(struct crypto_engine *engine, void *areq)
{
	struct aead_request *req = container_of(areq, struct aead_request,
						base);

	return stm32_cryp_prepare_req(NULL, req);
}

static int stm32_cryp_aead_one_req(struct crypto_engine *engine, void *areq)
{
	struct aead_request *req = container_of(areq, struct aead_request,
						base);
	struct stm32_cryp_ctx *ctx = crypto_aead_ctx(crypto_aead_reqtfm(req));
	struct stm32_cryp *cryp = ctx->cryp;

	if (!cryp)
		return -ENODEV;

	if (unlikely(!cryp->areq->assoclen &&
		     !stm32_cryp_get_input_text_len(cryp))) {
		/* No input data to process: get tag and finish */
		stm32_cryp_finish_req(cryp, 0);
		return 0;
	}

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

static int stm32_cryp_read_auth_tag(struct stm32_cryp *cryp)
{
	u32 cfg, size_bit, *dst, d32;
	u8 *d8;
	unsigned int i, j;
	int ret = 0;

	/* Update Config */
	cfg = stm32_cryp_read(cryp, CRYP_CR);

	cfg &= ~CR_PH_MASK;
	cfg |= CR_PH_FINAL;
	cfg &= ~CR_DEC_NOT_ENC;
	cfg |= CR_CRYPEN;

	stm32_cryp_write(cryp, CRYP_CR, cfg);

	if (is_gcm(cryp)) {
		/* GCM: write aad and payload size (in bits) */
		size_bit = cryp->areq->assoclen * 8;
		if (cryp->caps->swap_final)
			size_bit = (__force u32)cpu_to_be32(size_bit);

		stm32_cryp_write(cryp, CRYP_DIN, 0);
		stm32_cryp_write(cryp, CRYP_DIN, size_bit);

		size_bit = is_encrypt(cryp) ? cryp->areq->cryptlen :
				cryp->areq->cryptlen - AES_BLOCK_SIZE;
		size_bit *= 8;
		if (cryp->caps->swap_final)
			size_bit = (__force u32)cpu_to_be32(size_bit);

		stm32_cryp_write(cryp, CRYP_DIN, 0);
		stm32_cryp_write(cryp, CRYP_DIN, size_bit);
	} else {
		/* CCM: write CTR0 */
		u8 iv[AES_BLOCK_SIZE];
		u32 *iv32 = (u32 *)iv;
		__be32 *biv;

		biv = (void *)iv;

		memcpy(iv, cryp->areq->iv, AES_BLOCK_SIZE);
		memset(iv + AES_BLOCK_SIZE - 1 - iv[0], 0, iv[0] + 1);

		for (i = 0; i < AES_BLOCK_32; i++) {
			u32 xiv = iv32[i];

			if (!cryp->caps->padding_wa)
				xiv = be32_to_cpu(biv[i]);
			stm32_cryp_write(cryp, CRYP_DIN, xiv);
		}
	}

	/* Wait for output data */
	ret = stm32_cryp_wait_output(cryp);
	if (ret) {
		dev_err(cryp->dev, "Timeout (read tag)\n");
		return ret;
	}

	if (is_encrypt(cryp)) {
		/* Get and write tag */
		dst = sg_virt(cryp->out_sg) + _walked_out;

		for (i = 0; i < AES_BLOCK_32; i++) {
			if (cryp->total_out >= sizeof(u32)) {
				/* Read a full u32 */
				*dst = stm32_cryp_read(cryp, CRYP_DOUT);

				dst = stm32_cryp_next_out(cryp, dst,
							  sizeof(u32));
				cryp->total_out -= sizeof(u32);
			} else if (!cryp->total_out) {
				/* Empty fifo out (data from input padding) */
				stm32_cryp_read(cryp, CRYP_DOUT);
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
	} else {
		/* Get and check tag */
		u32 in_tag[AES_BLOCK_32], out_tag[AES_BLOCK_32];

		scatterwalk_map_and_copy(in_tag, cryp->in_sg,
					 cryp->total_in_save - cryp->authsize,
					 cryp->authsize, 0);

		for (i = 0; i < AES_BLOCK_32; i++)
			out_tag[i] = stm32_cryp_read(cryp, CRYP_DOUT);

		if (crypto_memneq(in_tag, out_tag, cryp->authsize))
			ret = -EBADMSG;
	}

	/* Disable cryp */
	cfg &= ~CR_CRYPEN;
	stm32_cryp_write(cryp, CRYP_CR, cfg);

	return ret;
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

		stm32_cryp_hw_write_iv(cryp, (__be32 *)cryp->last_ctr);

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
	size_t tag_size;

	/* Do no read tag now (if any) */
	if (is_encrypt(cryp) && (is_gcm(cryp) || is_ccm(cryp)))
		tag_size = cryp->authsize;
	else
		tag_size = 0;

	dst = sg_virt(cryp->out_sg) + _walked_out;

	for (i = 0; i < cryp->hw_blocksize / sizeof(u32); i++) {
		if (likely(cryp->total_out - tag_size >= sizeof(u32))) {
			/* Read a full u32 */
			*dst = stm32_cryp_read(cryp, CRYP_DOUT);

			dst = stm32_cryp_next_out(cryp, dst, sizeof(u32));
			cryp->total_out -= sizeof(u32);
		} else if (cryp->total_out == tag_size) {
			/* Empty fifo out (data from input padding) */
			d32 = stm32_cryp_read(cryp, CRYP_DOUT);
		} else {
			/* Read less than an u32 */
			d32 = stm32_cryp_read(cryp, CRYP_DOUT);
			d8 = (u8 *)&d32;

			for (j = 0; j < cryp->total_out - tag_size; j++) {
				*((u8 *)dst) = *(d8++);
				dst = stm32_cryp_next_out(cryp, dst, 1);
			}
			cryp->total_out = tag_size;
		}
	}

	return !(cryp->total_out - tag_size) || !cryp->total_in;
}

static void stm32_cryp_irq_write_block(struct stm32_cryp *cryp)
{
	unsigned int i, j;
	u32 *src;
	u8 d8[4];
	size_t tag_size;

	/* Do no write tag (if any) */
	if (is_decrypt(cryp) && (is_gcm(cryp) || is_ccm(cryp)))
		tag_size = cryp->authsize;
	else
		tag_size = 0;

	src = sg_virt(cryp->in_sg) + _walked_in;

	for (i = 0; i < cryp->hw_blocksize / sizeof(u32); i++) {
		if (likely(cryp->total_in - tag_size >= sizeof(u32))) {
			/* Write a full u32 */
			stm32_cryp_write(cryp, CRYP_DIN, *src);

			src = stm32_cryp_next_in(cryp, src, sizeof(u32));
			cryp->total_in -= sizeof(u32);
		} else if (cryp->total_in == tag_size) {
			/* Write padding data */
			stm32_cryp_write(cryp, CRYP_DIN, 0);
		} else {
			/* Write less than an u32 */
			memset(d8, 0, sizeof(u32));
			for (j = 0; j < cryp->total_in - tag_size; j++) {
				d8[j] = *((u8 *)src);
				src = stm32_cryp_next_in(cryp, src, 1);
			}

			stm32_cryp_write(cryp, CRYP_DIN, *(u32 *)d8);
			cryp->total_in = tag_size;
		}
	}
}

static void stm32_cryp_irq_write_gcm_padded_data(struct stm32_cryp *cryp)
{
	int err;
	u32 cfg, tmp[AES_BLOCK_32];
	size_t total_in_ori = cryp->total_in;
	struct scatterlist *out_sg_ori = cryp->out_sg;
	unsigned int i;

	/* 'Special workaround' procedure described in the datasheet */

	/* a) disable ip */
	stm32_cryp_write(cryp, CRYP_IMSCR, 0);
	cfg = stm32_cryp_read(cryp, CRYP_CR);
	cfg &= ~CR_CRYPEN;
	stm32_cryp_write(cryp, CRYP_CR, cfg);

	/* b) Update IV1R */
	stm32_cryp_write(cryp, CRYP_IV1RR, cryp->gcm_ctr - 2);

	/* c) change mode to CTR */
	cfg &= ~CR_ALGO_MASK;
	cfg |= CR_AES_CTR;
	stm32_cryp_write(cryp, CRYP_CR, cfg);

	/* a) enable IP */
	cfg |= CR_CRYPEN;
	stm32_cryp_write(cryp, CRYP_CR, cfg);

	/* b) pad and write the last block */
	stm32_cryp_irq_write_block(cryp);
	cryp->total_in = total_in_ori;
	err = stm32_cryp_wait_output(cryp);
	if (err) {
		dev_err(cryp->dev, "Timeout (write gcm header)\n");
		return stm32_cryp_finish_req(cryp, err);
	}

	/* c) get and store encrypted data */
	stm32_cryp_irq_read_data(cryp);
	scatterwalk_map_and_copy(tmp, out_sg_ori,
				 cryp->total_in_save - total_in_ori,
				 total_in_ori, 0);

	/* d) change mode back to AES GCM */
	cfg &= ~CR_ALGO_MASK;
	cfg |= CR_AES_GCM;
	stm32_cryp_write(cryp, CRYP_CR, cfg);

	/* e) change phase to Final */
	cfg &= ~CR_PH_MASK;
	cfg |= CR_PH_FINAL;
	stm32_cryp_write(cryp, CRYP_CR, cfg);

	/* f) write padded data */
	for (i = 0; i < AES_BLOCK_32; i++) {
		if (cryp->total_in)
			stm32_cryp_write(cryp, CRYP_DIN, tmp[i]);
		else
			stm32_cryp_write(cryp, CRYP_DIN, 0);

		cryp->total_in -= min_t(size_t, sizeof(u32), cryp->total_in);
	}

	/* g) Empty fifo out */
	err = stm32_cryp_wait_output(cryp);
	if (err) {
		dev_err(cryp->dev, "Timeout (write gcm header)\n");
		return stm32_cryp_finish_req(cryp, err);
	}

	for (i = 0; i < AES_BLOCK_32; i++)
		stm32_cryp_read(cryp, CRYP_DOUT);

	/* h) run the he normal Final phase */
	stm32_cryp_finish_req(cryp, 0);
}

static void stm32_cryp_irq_set_npblb(struct stm32_cryp *cryp)
{
	u32 cfg, payload_bytes;

	/* disable ip, set NPBLB and reneable ip */
	cfg = stm32_cryp_read(cryp, CRYP_CR);
	cfg &= ~CR_CRYPEN;
	stm32_cryp_write(cryp, CRYP_CR, cfg);

	payload_bytes = is_decrypt(cryp) ? cryp->total_in - cryp->authsize :
					   cryp->total_in;
	cfg |= (cryp->hw_blocksize - payload_bytes) << CR_NBPBL_SHIFT;
	cfg |= CR_CRYPEN;
	stm32_cryp_write(cryp, CRYP_CR, cfg);
}

static void stm32_cryp_irq_write_ccm_padded_data(struct stm32_cryp *cryp)
{
	int err = 0;
	u32 cfg, iv1tmp;
	u32 cstmp1[AES_BLOCK_32], cstmp2[AES_BLOCK_32], tmp[AES_BLOCK_32];
	size_t last_total_out, total_in_ori = cryp->total_in;
	struct scatterlist *out_sg_ori = cryp->out_sg;
	unsigned int i;

	/* 'Special workaround' procedure described in the datasheet */
	cryp->flags |= FLG_CCM_PADDED_WA;

	/* a) disable ip */
	stm32_cryp_write(cryp, CRYP_IMSCR, 0);

	cfg = stm32_cryp_read(cryp, CRYP_CR);
	cfg &= ~CR_CRYPEN;
	stm32_cryp_write(cryp, CRYP_CR, cfg);

	/* b) get IV1 from CRYP_CSGCMCCM7 */
	iv1tmp = stm32_cryp_read(cryp, CRYP_CSGCMCCM0R + 7 * 4);

	/* c) Load CRYP_CSGCMCCMxR */
	for (i = 0; i < ARRAY_SIZE(cstmp1); i++)
		cstmp1[i] = stm32_cryp_read(cryp, CRYP_CSGCMCCM0R + i * 4);

	/* d) Write IV1R */
	stm32_cryp_write(cryp, CRYP_IV1RR, iv1tmp);

	/* e) change mode to CTR */
	cfg &= ~CR_ALGO_MASK;
	cfg |= CR_AES_CTR;
	stm32_cryp_write(cryp, CRYP_CR, cfg);

	/* a) enable IP */
	cfg |= CR_CRYPEN;
	stm32_cryp_write(cryp, CRYP_CR, cfg);

	/* b) pad and write the last block */
	stm32_cryp_irq_write_block(cryp);
	cryp->total_in = total_in_ori;
	err = stm32_cryp_wait_output(cryp);
	if (err) {
		dev_err(cryp->dev, "Timeout (wite ccm padded data)\n");
		return stm32_cryp_finish_req(cryp, err);
	}

	/* c) get and store decrypted data */
	last_total_out = cryp->total_out;
	stm32_cryp_irq_read_data(cryp);

	memset(tmp, 0, sizeof(tmp));
	scatterwalk_map_and_copy(tmp, out_sg_ori,
				 cryp->total_out_save - last_total_out,
				 last_total_out, 0);

	/* d) Load again CRYP_CSGCMCCMxR */
	for (i = 0; i < ARRAY_SIZE(cstmp2); i++)
		cstmp2[i] = stm32_cryp_read(cryp, CRYP_CSGCMCCM0R + i * 4);

	/* e) change mode back to AES CCM */
	cfg &= ~CR_ALGO_MASK;
	cfg |= CR_AES_CCM;
	stm32_cryp_write(cryp, CRYP_CR, cfg);

	/* f) change phase to header */
	cfg &= ~CR_PH_MASK;
	cfg |= CR_PH_HEADER;
	stm32_cryp_write(cryp, CRYP_CR, cfg);

	/* g) XOR and write padded data */
	for (i = 0; i < ARRAY_SIZE(tmp); i++) {
		tmp[i] ^= cstmp1[i];
		tmp[i] ^= cstmp2[i];
		stm32_cryp_write(cryp, CRYP_DIN, tmp[i]);
	}

	/* h) wait for completion */
	err = stm32_cryp_wait_busy(cryp);
	if (err)
		dev_err(cryp->dev, "Timeout (wite ccm padded data)\n");

	/* i) run the he normal Final phase */
	stm32_cryp_finish_req(cryp, err);
}

static void stm32_cryp_irq_write_data(struct stm32_cryp *cryp)
{
	if (unlikely(!cryp->total_in)) {
		dev_warn(cryp->dev, "No more data to process\n");
		return;
	}

	if (unlikely(cryp->total_in < AES_BLOCK_SIZE &&
		     (stm32_cryp_get_hw_mode(cryp) == CR_AES_GCM) &&
		     is_encrypt(cryp))) {
		/* Padding for AES GCM encryption */
		if (cryp->caps->padding_wa)
			/* Special case 1 */
			return stm32_cryp_irq_write_gcm_padded_data(cryp);

		/* Setting padding bytes (NBBLB) */
		stm32_cryp_irq_set_npblb(cryp);
	}

	if (unlikely((cryp->total_in - cryp->authsize < AES_BLOCK_SIZE) &&
		     (stm32_cryp_get_hw_mode(cryp) == CR_AES_CCM) &&
		     is_decrypt(cryp))) {
		/* Padding for AES CCM decryption */
		if (cryp->caps->padding_wa)
			/* Special case 2 */
			return stm32_cryp_irq_write_ccm_padded_data(cryp);

		/* Setting padding bytes (NBBLB) */
		stm32_cryp_irq_set_npblb(cryp);
	}

	if (is_aes(cryp) && is_ctr(cryp))
		stm32_cryp_check_ctr_counter(cryp);

	stm32_cryp_irq_write_block(cryp);
}

static void stm32_cryp_irq_write_gcm_header(struct stm32_cryp *cryp)
{
	int err;
	unsigned int i, j;
	u32 cfg, *src;

	src = sg_virt(cryp->in_sg) + _walked_in;

	for (i = 0; i < AES_BLOCK_32; i++) {
		stm32_cryp_write(cryp, CRYP_DIN, *src);

		src = stm32_cryp_next_in(cryp, src, sizeof(u32));
		cryp->total_in -= min_t(size_t, sizeof(u32), cryp->total_in);

		/* Check if whole header written */
		if ((cryp->total_in_save - cryp->total_in) ==
				cryp->areq->assoclen) {
			/* Write padding if needed */
			for (j = i + 1; j < AES_BLOCK_32; j++)
				stm32_cryp_write(cryp, CRYP_DIN, 0);

			/* Wait for completion */
			err = stm32_cryp_wait_busy(cryp);
			if (err) {
				dev_err(cryp->dev, "Timeout (gcm header)\n");
				return stm32_cryp_finish_req(cryp, err);
			}

			if (stm32_cryp_get_input_text_len(cryp)) {
				/* Phase 3 : payload */
				cfg = stm32_cryp_read(cryp, CRYP_CR);
				cfg &= ~CR_CRYPEN;
				stm32_cryp_write(cryp, CRYP_CR, cfg);

				cfg &= ~CR_PH_MASK;
				cfg |= CR_PH_PAYLOAD;
				cfg |= CR_CRYPEN;
				stm32_cryp_write(cryp, CRYP_CR, cfg);
			} else {
				/* Phase 4 : tag */
				stm32_cryp_write(cryp, CRYP_IMSCR, 0);
				stm32_cryp_finish_req(cryp, 0);
			}

			break;
		}

		if (!cryp->total_in)
			break;
	}
}

static void stm32_cryp_irq_write_ccm_header(struct stm32_cryp *cryp)
{
	int err;
	unsigned int i = 0, j, k;
	u32 alen, cfg, *src;
	u8 d8[4];

	src = sg_virt(cryp->in_sg) + _walked_in;
	alen = cryp->areq->assoclen;

	if (!_walked_in) {
		if (cryp->areq->assoclen <= 65280) {
			/* Write first u32 of B1 */
			d8[0] = (alen >> 8) & 0xFF;
			d8[1] = alen & 0xFF;
			d8[2] = *((u8 *)src);
			src = stm32_cryp_next_in(cryp, src, 1);
			d8[3] = *((u8 *)src);
			src = stm32_cryp_next_in(cryp, src, 1);

			stm32_cryp_write(cryp, CRYP_DIN, *(u32 *)d8);
			i++;

			cryp->total_in -= min_t(size_t, 2, cryp->total_in);
		} else {
			/* Build the two first u32 of B1 */
			d8[0] = 0xFF;
			d8[1] = 0xFE;
			d8[2] = alen & 0xFF000000;
			d8[3] = alen & 0x00FF0000;

			stm32_cryp_write(cryp, CRYP_DIN, *(u32 *)d8);
			i++;

			d8[0] = alen & 0x0000FF00;
			d8[1] = alen & 0x000000FF;
			d8[2] = *((u8 *)src);
			src = stm32_cryp_next_in(cryp, src, 1);
			d8[3] = *((u8 *)src);
			src = stm32_cryp_next_in(cryp, src, 1);

			stm32_cryp_write(cryp, CRYP_DIN, *(u32 *)d8);
			i++;

			cryp->total_in -= min_t(size_t, 2, cryp->total_in);
		}
	}

	/* Write next u32 */
	for (; i < AES_BLOCK_32; i++) {
		/* Build an u32 */
		memset(d8, 0, sizeof(u32));
		for (k = 0; k < sizeof(u32); k++) {
			d8[k] = *((u8 *)src);
			src = stm32_cryp_next_in(cryp, src, 1);

			cryp->total_in -= min_t(size_t, 1, cryp->total_in);
			if ((cryp->total_in_save - cryp->total_in) == alen)
				break;
		}

		stm32_cryp_write(cryp, CRYP_DIN, *(u32 *)d8);

		if ((cryp->total_in_save - cryp->total_in) == alen) {
			/* Write padding if needed */
			for (j = i + 1; j < AES_BLOCK_32; j++)
				stm32_cryp_write(cryp, CRYP_DIN, 0);

			/* Wait for completion */
			err = stm32_cryp_wait_busy(cryp);
			if (err) {
				dev_err(cryp->dev, "Timeout (ccm header)\n");
				return stm32_cryp_finish_req(cryp, err);
			}

			if (stm32_cryp_get_input_text_len(cryp)) {
				/* Phase 3 : payload */
				cfg = stm32_cryp_read(cryp, CRYP_CR);
				cfg &= ~CR_CRYPEN;
				stm32_cryp_write(cryp, CRYP_CR, cfg);

				cfg &= ~CR_PH_MASK;
				cfg |= CR_PH_PAYLOAD;
				cfg |= CR_CRYPEN;
				stm32_cryp_write(cryp, CRYP_CR, cfg);
			} else {
				/* Phase 4 : tag */
				stm32_cryp_write(cryp, CRYP_IMSCR, 0);
				stm32_cryp_finish_req(cryp, 0);
			}

			break;
		}
	}
}

static irqreturn_t stm32_cryp_irq_thread(int irq, void *arg)
{
	struct stm32_cryp *cryp = arg;
	u32 ph;

	if (cryp->irq_status & MISR_OUT)
		/* Output FIFO IRQ: read data */
		if (unlikely(stm32_cryp_irq_read_data(cryp))) {
			/* All bytes processed, finish */
			stm32_cryp_write(cryp, CRYP_IMSCR, 0);
			stm32_cryp_finish_req(cryp, 0);
			return IRQ_HANDLED;
		}

	if (cryp->irq_status & MISR_IN) {
		if (is_gcm(cryp)) {
			ph = stm32_cryp_read(cryp, CRYP_CR) & CR_PH_MASK;
			if (unlikely(ph == CR_PH_HEADER))
				/* Write Header */
				stm32_cryp_irq_write_gcm_header(cryp);
			else
				/* Input FIFO IRQ: write data */
				stm32_cryp_irq_write_data(cryp);
			cryp->gcm_ctr++;
		} else if (is_ccm(cryp)) {
			ph = stm32_cryp_read(cryp, CRYP_CR) & CR_PH_MASK;
			if (unlikely(ph == CR_PH_HEADER))
				/* Write Header */
				stm32_cryp_irq_write_ccm_header(cryp);
			else
				/* Input FIFO IRQ: write data */
				stm32_cryp_irq_write_data(cryp);
		} else {
			/* Input FIFO IRQ: write data */
			stm32_cryp_irq_write_data(cryp);
		}
	}

	return IRQ_HANDLED;
}

static irqreturn_t stm32_cryp_irq(int irq, void *arg)
{
	struct stm32_cryp *cryp = arg;

	cryp->irq_status = stm32_cryp_read(cryp, CRYP_MISR);

	return IRQ_WAKE_THREAD;
}

static struct skcipher_alg crypto_algs[] = {
{
	.base.cra_name		= "ecb(aes)",
	.base.cra_driver_name	= "stm32-ecb-aes",
	.base.cra_priority	= 200,
	.base.cra_flags		= CRYPTO_ALG_ASYNC,
	.base.cra_blocksize	= AES_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct stm32_cryp_ctx),
	.base.cra_alignmask	= 0xf,
	.base.cra_module	= THIS_MODULE,

	.init			= stm32_cryp_init_tfm,
	.min_keysize		= AES_MIN_KEY_SIZE,
	.max_keysize		= AES_MAX_KEY_SIZE,
	.setkey			= stm32_cryp_aes_setkey,
	.encrypt		= stm32_cryp_aes_ecb_encrypt,
	.decrypt		= stm32_cryp_aes_ecb_decrypt,
},
{
	.base.cra_name		= "cbc(aes)",
	.base.cra_driver_name	= "stm32-cbc-aes",
	.base.cra_priority	= 200,
	.base.cra_flags		= CRYPTO_ALG_ASYNC,
	.base.cra_blocksize	= AES_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct stm32_cryp_ctx),
	.base.cra_alignmask	= 0xf,
	.base.cra_module	= THIS_MODULE,

	.init			= stm32_cryp_init_tfm,
	.min_keysize		= AES_MIN_KEY_SIZE,
	.max_keysize		= AES_MAX_KEY_SIZE,
	.ivsize			= AES_BLOCK_SIZE,
	.setkey			= stm32_cryp_aes_setkey,
	.encrypt		= stm32_cryp_aes_cbc_encrypt,
	.decrypt		= stm32_cryp_aes_cbc_decrypt,
},
{
	.base.cra_name		= "ctr(aes)",
	.base.cra_driver_name	= "stm32-ctr-aes",
	.base.cra_priority	= 200,
	.base.cra_flags		= CRYPTO_ALG_ASYNC,
	.base.cra_blocksize	= 1,
	.base.cra_ctxsize	= sizeof(struct stm32_cryp_ctx),
	.base.cra_alignmask	= 0xf,
	.base.cra_module	= THIS_MODULE,

	.init			= stm32_cryp_init_tfm,
	.min_keysize		= AES_MIN_KEY_SIZE,
	.max_keysize		= AES_MAX_KEY_SIZE,
	.ivsize			= AES_BLOCK_SIZE,
	.setkey			= stm32_cryp_aes_setkey,
	.encrypt		= stm32_cryp_aes_ctr_encrypt,
	.decrypt		= stm32_cryp_aes_ctr_decrypt,
},
{
	.base.cra_name		= "ecb(des)",
	.base.cra_driver_name	= "stm32-ecb-des",
	.base.cra_priority	= 200,
	.base.cra_flags		= CRYPTO_ALG_ASYNC,
	.base.cra_blocksize	= DES_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct stm32_cryp_ctx),
	.base.cra_alignmask	= 0xf,
	.base.cra_module	= THIS_MODULE,

	.init			= stm32_cryp_init_tfm,
	.min_keysize		= DES_BLOCK_SIZE,
	.max_keysize		= DES_BLOCK_SIZE,
	.setkey			= stm32_cryp_des_setkey,
	.encrypt		= stm32_cryp_des_ecb_encrypt,
	.decrypt		= stm32_cryp_des_ecb_decrypt,
},
{
	.base.cra_name		= "cbc(des)",
	.base.cra_driver_name	= "stm32-cbc-des",
	.base.cra_priority	= 200,
	.base.cra_flags		= CRYPTO_ALG_ASYNC,
	.base.cra_blocksize	= DES_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct stm32_cryp_ctx),
	.base.cra_alignmask	= 0xf,
	.base.cra_module	= THIS_MODULE,

	.init			= stm32_cryp_init_tfm,
	.min_keysize		= DES_BLOCK_SIZE,
	.max_keysize		= DES_BLOCK_SIZE,
	.ivsize			= DES_BLOCK_SIZE,
	.setkey			= stm32_cryp_des_setkey,
	.encrypt		= stm32_cryp_des_cbc_encrypt,
	.decrypt		= stm32_cryp_des_cbc_decrypt,
},
{
	.base.cra_name		= "ecb(des3_ede)",
	.base.cra_driver_name	= "stm32-ecb-des3",
	.base.cra_priority	= 200,
	.base.cra_flags		= CRYPTO_ALG_ASYNC,
	.base.cra_blocksize	= DES_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct stm32_cryp_ctx),
	.base.cra_alignmask	= 0xf,
	.base.cra_module	= THIS_MODULE,

	.init			= stm32_cryp_init_tfm,
	.min_keysize		= 3 * DES_BLOCK_SIZE,
	.max_keysize		= 3 * DES_BLOCK_SIZE,
	.setkey			= stm32_cryp_tdes_setkey,
	.encrypt		= stm32_cryp_tdes_ecb_encrypt,
	.decrypt		= stm32_cryp_tdes_ecb_decrypt,
},
{
	.base.cra_name		= "cbc(des3_ede)",
	.base.cra_driver_name	= "stm32-cbc-des3",
	.base.cra_priority	= 200,
	.base.cra_flags		= CRYPTO_ALG_ASYNC,
	.base.cra_blocksize	= DES_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct stm32_cryp_ctx),
	.base.cra_alignmask	= 0xf,
	.base.cra_module	= THIS_MODULE,

	.init			= stm32_cryp_init_tfm,
	.min_keysize		= 3 * DES_BLOCK_SIZE,
	.max_keysize		= 3 * DES_BLOCK_SIZE,
	.ivsize			= DES_BLOCK_SIZE,
	.setkey			= stm32_cryp_tdes_setkey,
	.encrypt		= stm32_cryp_tdes_cbc_encrypt,
	.decrypt		= stm32_cryp_tdes_cbc_decrypt,
},
};

static struct aead_alg aead_algs[] = {
{
	.setkey		= stm32_cryp_aes_aead_setkey,
	.setauthsize	= stm32_cryp_aes_gcm_setauthsize,
	.encrypt	= stm32_cryp_aes_gcm_encrypt,
	.decrypt	= stm32_cryp_aes_gcm_decrypt,
	.init		= stm32_cryp_aes_aead_init,
	.ivsize		= 12,
	.maxauthsize	= AES_BLOCK_SIZE,

	.base = {
		.cra_name		= "gcm(aes)",
		.cra_driver_name	= "stm32-gcm-aes",
		.cra_priority		= 200,
		.cra_flags		= CRYPTO_ALG_ASYNC,
		.cra_blocksize		= 1,
		.cra_ctxsize		= sizeof(struct stm32_cryp_ctx),
		.cra_alignmask		= 0xf,
		.cra_module		= THIS_MODULE,
	},
},
{
	.setkey		= stm32_cryp_aes_aead_setkey,
	.setauthsize	= stm32_cryp_aes_ccm_setauthsize,
	.encrypt	= stm32_cryp_aes_ccm_encrypt,
	.decrypt	= stm32_cryp_aes_ccm_decrypt,
	.init		= stm32_cryp_aes_aead_init,
	.ivsize		= AES_BLOCK_SIZE,
	.maxauthsize	= AES_BLOCK_SIZE,

	.base = {
		.cra_name		= "ccm(aes)",
		.cra_driver_name	= "stm32-ccm-aes",
		.cra_priority		= 200,
		.cra_flags		= CRYPTO_ALG_ASYNC,
		.cra_blocksize		= 1,
		.cra_ctxsize		= sizeof(struct stm32_cryp_ctx),
		.cra_alignmask		= 0xf,
		.cra_module		= THIS_MODULE,
	},
},
};

static const struct stm32_cryp_caps f7_data = {
	.swap_final = true,
	.padding_wa = true,
};

static const struct stm32_cryp_caps mp1_data = {
	.swap_final = false,
	.padding_wa = false,
};

static const struct of_device_id stm32_dt_ids[] = {
	{ .compatible = "st,stm32f756-cryp", .data = &f7_data},
	{ .compatible = "st,stm32mp1-cryp", .data = &mp1_data},
	{},
};
MODULE_DEVICE_TABLE(of, stm32_dt_ids);

static int stm32_cryp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct stm32_cryp *cryp;
	struct reset_control *rst;
	int irq, ret;

	cryp = devm_kzalloc(dev, sizeof(*cryp), GFP_KERNEL);
	if (!cryp)
		return -ENOMEM;

	cryp->caps = of_device_get_match_data(dev);
	if (!cryp->caps)
		return -ENODEV;

	cryp->dev = dev;

	cryp->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(cryp->regs))
		return PTR_ERR(cryp->regs);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

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

	pm_runtime_set_autosuspend_delay(dev, CRYP_AUTOSUSPEND_DELAY);
	pm_runtime_use_autosuspend(dev);

	pm_runtime_get_noresume(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

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

	ret = crypto_engine_start(cryp->engine);
	if (ret) {
		dev_err(dev, "Could not start crypto engine\n");
		goto err_engine2;
	}

	ret = crypto_register_skciphers(crypto_algs, ARRAY_SIZE(crypto_algs));
	if (ret) {
		dev_err(dev, "Could not register algs\n");
		goto err_algs;
	}

	ret = crypto_register_aeads(aead_algs, ARRAY_SIZE(aead_algs));
	if (ret)
		goto err_aead_algs;

	dev_info(dev, "Initialized\n");

	pm_runtime_put_sync(dev);

	return 0;

err_aead_algs:
	crypto_unregister_skciphers(crypto_algs, ARRAY_SIZE(crypto_algs));
err_algs:
err_engine2:
	crypto_engine_exit(cryp->engine);
err_engine1:
	spin_lock(&cryp_list.lock);
	list_del(&cryp->list);
	spin_unlock(&cryp_list.lock);

	pm_runtime_disable(dev);
	pm_runtime_put_noidle(dev);
	pm_runtime_disable(dev);
	pm_runtime_put_noidle(dev);

	clk_disable_unprepare(cryp->clk);

	return ret;
}

static int stm32_cryp_remove(struct platform_device *pdev)
{
	struct stm32_cryp *cryp = platform_get_drvdata(pdev);
	int ret;

	if (!cryp)
		return -ENODEV;

	ret = pm_runtime_get_sync(cryp->dev);
	if (ret < 0)
		return ret;

	crypto_unregister_aeads(aead_algs, ARRAY_SIZE(aead_algs));
	crypto_unregister_skciphers(crypto_algs, ARRAY_SIZE(crypto_algs));

	crypto_engine_exit(cryp->engine);

	spin_lock(&cryp_list.lock);
	list_del(&cryp->list);
	spin_unlock(&cryp_list.lock);

	pm_runtime_disable(cryp->dev);
	pm_runtime_put_noidle(cryp->dev);

	clk_disable_unprepare(cryp->clk);

	return 0;
}

#ifdef CONFIG_PM
static int stm32_cryp_runtime_suspend(struct device *dev)
{
	struct stm32_cryp *cryp = dev_get_drvdata(dev);

	clk_disable_unprepare(cryp->clk);

	return 0;
}

static int stm32_cryp_runtime_resume(struct device *dev)
{
	struct stm32_cryp *cryp = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(cryp->clk);
	if (ret) {
		dev_err(cryp->dev, "Failed to prepare_enable clock\n");
		return ret;
	}

	return 0;
}
#endif

static const struct dev_pm_ops stm32_cryp_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(stm32_cryp_runtime_suspend,
			   stm32_cryp_runtime_resume, NULL)
};

static struct platform_driver stm32_cryp_driver = {
	.probe  = stm32_cryp_probe,
	.remove = stm32_cryp_remove,
	.driver = {
		.name           = DRIVER_NAME,
		.pm		= &stm32_cryp_pm_ops,
		.of_match_table = stm32_dt_ids,
	},
};

module_platform_driver(stm32_cryp_driver);

MODULE_AUTHOR("Fabien Dessenne <fabien.dessenne@st.com>");
MODULE_DESCRIPTION("STMicrolectronics STM32 CRYP hardware driver");
MODULE_LICENSE("GPL");
