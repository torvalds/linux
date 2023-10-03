// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx ZynqMP AES Driver.
 * Copyright (c) 2020 Xilinx Inc.
 */

#include <crypto/aes.h>
#include <crypto/engine.h>
#include <crypto/gcm.h>
#include <crypto/internal/aead.h>
#include <crypto/scatterwalk.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/string.h>

#define ZYNQMP_DMA_BIT_MASK	32U

#define ZYNQMP_AES_KEY_SIZE		AES_KEYSIZE_256
#define ZYNQMP_AES_AUTH_SIZE		16U
#define ZYNQMP_KEY_SRC_SEL_KEY_LEN	1U
#define ZYNQMP_AES_BLK_SIZE		1U
#define ZYNQMP_AES_MIN_INPUT_BLK_SIZE	4U
#define ZYNQMP_AES_WORD_LEN		4U

#define ZYNQMP_AES_GCM_TAG_MISMATCH_ERR		0x01
#define ZYNQMP_AES_WRONG_KEY_SRC_ERR		0x13
#define ZYNQMP_AES_PUF_NOT_PROGRAMMED		0xE300

enum zynqmp_aead_op {
	ZYNQMP_AES_DECRYPT = 0,
	ZYNQMP_AES_ENCRYPT
};

enum zynqmp_aead_keysrc {
	ZYNQMP_AES_KUP_KEY = 0,
	ZYNQMP_AES_DEV_KEY,
	ZYNQMP_AES_PUF_KEY
};

struct zynqmp_aead_drv_ctx {
	union {
		struct aead_engine_alg aead;
	} alg;
	struct device *dev;
	struct crypto_engine *engine;
};

struct zynqmp_aead_hw_req {
	u64 src;
	u64 iv;
	u64 key;
	u64 dst;
	u64 size;
	u64 op;
	u64 keysrc;
};

struct zynqmp_aead_tfm_ctx {
	struct device *dev;
	u8 key[ZYNQMP_AES_KEY_SIZE];
	u8 *iv;
	u32 keylen;
	u32 authsize;
	enum zynqmp_aead_keysrc keysrc;
	struct crypto_aead *fbk_cipher;
};

struct zynqmp_aead_req_ctx {
	enum zynqmp_aead_op op;
};

static int zynqmp_aes_aead_cipher(struct aead_request *req)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct zynqmp_aead_tfm_ctx *tfm_ctx = crypto_aead_ctx(aead);
	struct zynqmp_aead_req_ctx *rq_ctx = aead_request_ctx(req);
	struct device *dev = tfm_ctx->dev;
	struct zynqmp_aead_hw_req *hwreq;
	dma_addr_t dma_addr_data, dma_addr_hw_req;
	unsigned int data_size;
	unsigned int status;
	int ret;
	size_t dma_size;
	char *kbuf;
	int err;

	if (tfm_ctx->keysrc == ZYNQMP_AES_KUP_KEY)
		dma_size = req->cryptlen + ZYNQMP_AES_KEY_SIZE
			   + GCM_AES_IV_SIZE;
	else
		dma_size = req->cryptlen + GCM_AES_IV_SIZE;

	kbuf = dma_alloc_coherent(dev, dma_size, &dma_addr_data, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	hwreq = dma_alloc_coherent(dev, sizeof(struct zynqmp_aead_hw_req),
				   &dma_addr_hw_req, GFP_KERNEL);
	if (!hwreq) {
		dma_free_coherent(dev, dma_size, kbuf, dma_addr_data);
		return -ENOMEM;
	}

	data_size = req->cryptlen;
	scatterwalk_map_and_copy(kbuf, req->src, 0, req->cryptlen, 0);
	memcpy(kbuf + data_size, req->iv, GCM_AES_IV_SIZE);

	hwreq->src = dma_addr_data;
	hwreq->dst = dma_addr_data;
	hwreq->iv = hwreq->src + data_size;
	hwreq->keysrc = tfm_ctx->keysrc;
	hwreq->op = rq_ctx->op;

	if (hwreq->op == ZYNQMP_AES_ENCRYPT)
		hwreq->size = data_size;
	else
		hwreq->size = data_size - ZYNQMP_AES_AUTH_SIZE;

	if (hwreq->keysrc == ZYNQMP_AES_KUP_KEY) {
		memcpy(kbuf + data_size + GCM_AES_IV_SIZE,
		       tfm_ctx->key, ZYNQMP_AES_KEY_SIZE);

		hwreq->key = hwreq->src + data_size + GCM_AES_IV_SIZE;
	} else {
		hwreq->key = 0;
	}

	ret = zynqmp_pm_aes_engine(dma_addr_hw_req, &status);

	if (ret) {
		dev_err(dev, "ERROR: AES PM API failed\n");
		err = ret;
	} else if (status) {
		switch (status) {
		case ZYNQMP_AES_GCM_TAG_MISMATCH_ERR:
			dev_err(dev, "ERROR: Gcm Tag mismatch\n");
			break;
		case ZYNQMP_AES_WRONG_KEY_SRC_ERR:
			dev_err(dev, "ERROR: Wrong KeySrc, enable secure mode\n");
			break;
		case ZYNQMP_AES_PUF_NOT_PROGRAMMED:
			dev_err(dev, "ERROR: PUF is not registered\n");
			break;
		default:
			dev_err(dev, "ERROR: Unknown error\n");
			break;
		}
		err = -status;
	} else {
		if (hwreq->op == ZYNQMP_AES_ENCRYPT)
			data_size = data_size + ZYNQMP_AES_AUTH_SIZE;
		else
			data_size = data_size - ZYNQMP_AES_AUTH_SIZE;

		sg_copy_from_buffer(req->dst, sg_nents(req->dst),
				    kbuf, data_size);
		err = 0;
	}

	if (kbuf) {
		memzero_explicit(kbuf, dma_size);
		dma_free_coherent(dev, dma_size, kbuf, dma_addr_data);
	}
	if (hwreq) {
		memzero_explicit(hwreq, sizeof(struct zynqmp_aead_hw_req));
		dma_free_coherent(dev, sizeof(struct zynqmp_aead_hw_req),
				  hwreq, dma_addr_hw_req);
	}
	return err;
}

static int zynqmp_fallback_check(struct zynqmp_aead_tfm_ctx *tfm_ctx,
				 struct aead_request *req)
{
	int need_fallback = 0;
	struct zynqmp_aead_req_ctx *rq_ctx = aead_request_ctx(req);

	if (tfm_ctx->authsize != ZYNQMP_AES_AUTH_SIZE)
		need_fallback = 1;

	if (tfm_ctx->keysrc == ZYNQMP_AES_KUP_KEY &&
	    tfm_ctx->keylen != ZYNQMP_AES_KEY_SIZE) {
		need_fallback = 1;
	}
	if (req->assoclen != 0 ||
	    req->cryptlen < ZYNQMP_AES_MIN_INPUT_BLK_SIZE) {
		need_fallback = 1;
	}
	if ((req->cryptlen % ZYNQMP_AES_WORD_LEN) != 0)
		need_fallback = 1;

	if (rq_ctx->op == ZYNQMP_AES_DECRYPT &&
	    req->cryptlen <= ZYNQMP_AES_AUTH_SIZE) {
		need_fallback = 1;
	}
	return need_fallback;
}

static int zynqmp_handle_aes_req(struct crypto_engine *engine,
				 void *req)
{
	struct aead_request *areq =
				container_of(req, struct aead_request, base);
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct zynqmp_aead_tfm_ctx *tfm_ctx = crypto_aead_ctx(aead);
	struct zynqmp_aead_req_ctx *rq_ctx = aead_request_ctx(areq);
	struct aead_request *subreq = aead_request_ctx(req);
	int need_fallback;
	int err;

	need_fallback = zynqmp_fallback_check(tfm_ctx, areq);

	if (need_fallback) {
		aead_request_set_tfm(subreq, tfm_ctx->fbk_cipher);

		aead_request_set_callback(subreq, areq->base.flags,
					  NULL, NULL);
		aead_request_set_crypt(subreq, areq->src, areq->dst,
				       areq->cryptlen, areq->iv);
		aead_request_set_ad(subreq, areq->assoclen);
		if (rq_ctx->op == ZYNQMP_AES_ENCRYPT)
			err = crypto_aead_encrypt(subreq);
		else
			err = crypto_aead_decrypt(subreq);
	} else {
		err = zynqmp_aes_aead_cipher(areq);
	}

	crypto_finalize_aead_request(engine, areq, err);
	return 0;
}

static int zynqmp_aes_aead_setkey(struct crypto_aead *aead, const u8 *key,
				  unsigned int keylen)
{
	struct crypto_tfm *tfm = crypto_aead_tfm(aead);
	struct zynqmp_aead_tfm_ctx *tfm_ctx =
			(struct zynqmp_aead_tfm_ctx *)crypto_tfm_ctx(tfm);
	unsigned char keysrc;

	if (keylen == ZYNQMP_KEY_SRC_SEL_KEY_LEN) {
		keysrc = *key;
		if (keysrc == ZYNQMP_AES_KUP_KEY ||
		    keysrc == ZYNQMP_AES_DEV_KEY ||
		    keysrc == ZYNQMP_AES_PUF_KEY) {
			tfm_ctx->keysrc = (enum zynqmp_aead_keysrc)keysrc;
		} else {
			tfm_ctx->keylen = keylen;
		}
	} else {
		tfm_ctx->keylen = keylen;
		if (keylen == ZYNQMP_AES_KEY_SIZE) {
			tfm_ctx->keysrc = ZYNQMP_AES_KUP_KEY;
			memcpy(tfm_ctx->key, key, keylen);
		}
	}

	tfm_ctx->fbk_cipher->base.crt_flags &= ~CRYPTO_TFM_REQ_MASK;
	tfm_ctx->fbk_cipher->base.crt_flags |= (aead->base.crt_flags &
					CRYPTO_TFM_REQ_MASK);

	return crypto_aead_setkey(tfm_ctx->fbk_cipher, key, keylen);
}

static int zynqmp_aes_aead_setauthsize(struct crypto_aead *aead,
				       unsigned int authsize)
{
	struct crypto_tfm *tfm = crypto_aead_tfm(aead);
	struct zynqmp_aead_tfm_ctx *tfm_ctx =
			(struct zynqmp_aead_tfm_ctx *)crypto_tfm_ctx(tfm);

	tfm_ctx->authsize = authsize;
	return crypto_aead_setauthsize(tfm_ctx->fbk_cipher, authsize);
}

static int zynqmp_aes_aead_encrypt(struct aead_request *req)
{
	struct zynqmp_aead_drv_ctx *drv_ctx;
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct aead_alg *alg = crypto_aead_alg(aead);
	struct zynqmp_aead_req_ctx *rq_ctx = aead_request_ctx(req);

	rq_ctx->op = ZYNQMP_AES_ENCRYPT;
	drv_ctx = container_of(alg, struct zynqmp_aead_drv_ctx, alg.aead.base);

	return crypto_transfer_aead_request_to_engine(drv_ctx->engine, req);
}

static int zynqmp_aes_aead_decrypt(struct aead_request *req)
{
	struct zynqmp_aead_drv_ctx *drv_ctx;
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct aead_alg *alg = crypto_aead_alg(aead);
	struct zynqmp_aead_req_ctx *rq_ctx = aead_request_ctx(req);

	rq_ctx->op = ZYNQMP_AES_DECRYPT;
	drv_ctx = container_of(alg, struct zynqmp_aead_drv_ctx, alg.aead.base);

	return crypto_transfer_aead_request_to_engine(drv_ctx->engine, req);
}

static int zynqmp_aes_aead_init(struct crypto_aead *aead)
{
	struct crypto_tfm *tfm = crypto_aead_tfm(aead);
	struct zynqmp_aead_tfm_ctx *tfm_ctx =
		(struct zynqmp_aead_tfm_ctx *)crypto_tfm_ctx(tfm);
	struct zynqmp_aead_drv_ctx *drv_ctx;
	struct aead_alg *alg = crypto_aead_alg(aead);

	drv_ctx = container_of(alg, struct zynqmp_aead_drv_ctx, alg.aead.base);
	tfm_ctx->dev = drv_ctx->dev;

	tfm_ctx->fbk_cipher = crypto_alloc_aead(drv_ctx->alg.aead.base.base.cra_name,
						0,
						CRYPTO_ALG_NEED_FALLBACK);

	if (IS_ERR(tfm_ctx->fbk_cipher)) {
		pr_err("%s() Error: failed to allocate fallback for %s\n",
		       __func__, drv_ctx->alg.aead.base.base.cra_name);
		return PTR_ERR(tfm_ctx->fbk_cipher);
	}

	crypto_aead_set_reqsize(aead,
				max(sizeof(struct zynqmp_aead_req_ctx),
				    sizeof(struct aead_request) +
				    crypto_aead_reqsize(tfm_ctx->fbk_cipher)));
	return 0;
}

static void zynqmp_aes_aead_exit(struct crypto_aead *aead)
{
	struct crypto_tfm *tfm = crypto_aead_tfm(aead);
	struct zynqmp_aead_tfm_ctx *tfm_ctx =
			(struct zynqmp_aead_tfm_ctx *)crypto_tfm_ctx(tfm);

	if (tfm_ctx->fbk_cipher) {
		crypto_free_aead(tfm_ctx->fbk_cipher);
		tfm_ctx->fbk_cipher = NULL;
	}
	memzero_explicit(tfm_ctx, sizeof(struct zynqmp_aead_tfm_ctx));
}

static struct zynqmp_aead_drv_ctx aes_drv_ctx = {
	.alg.aead.base = {
		.setkey		= zynqmp_aes_aead_setkey,
		.setauthsize	= zynqmp_aes_aead_setauthsize,
		.encrypt	= zynqmp_aes_aead_encrypt,
		.decrypt	= zynqmp_aes_aead_decrypt,
		.init		= zynqmp_aes_aead_init,
		.exit		= zynqmp_aes_aead_exit,
		.ivsize		= GCM_AES_IV_SIZE,
		.maxauthsize	= ZYNQMP_AES_AUTH_SIZE,
		.base = {
		.cra_name		= "gcm(aes)",
		.cra_driver_name	= "xilinx-zynqmp-aes-gcm",
		.cra_priority		= 200,
		.cra_flags		= CRYPTO_ALG_TYPE_AEAD |
					  CRYPTO_ALG_ASYNC |
					  CRYPTO_ALG_ALLOCATES_MEMORY |
					  CRYPTO_ALG_KERN_DRIVER_ONLY |
					  CRYPTO_ALG_NEED_FALLBACK,
		.cra_blocksize		= ZYNQMP_AES_BLK_SIZE,
		.cra_ctxsize		= sizeof(struct zynqmp_aead_tfm_ctx),
		.cra_module		= THIS_MODULE,
		}
	},
	.alg.aead.op = {
		.do_one_request = zynqmp_handle_aes_req,
	},
};

static int zynqmp_aes_aead_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int err;

	/* ZynqMP AES driver supports only one instance */
	if (!aes_drv_ctx.dev)
		aes_drv_ctx.dev = dev;
	else
		return -ENODEV;

	err = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(ZYNQMP_DMA_BIT_MASK));
	if (err < 0) {
		dev_err(dev, "No usable DMA configuration\n");
		return err;
	}

	aes_drv_ctx.engine = crypto_engine_alloc_init(dev, 1);
	if (!aes_drv_ctx.engine) {
		dev_err(dev, "Cannot alloc AES engine\n");
		err = -ENOMEM;
		goto err_engine;
	}

	err = crypto_engine_start(aes_drv_ctx.engine);
	if (err) {
		dev_err(dev, "Cannot start AES engine\n");
		goto err_engine;
	}

	err = crypto_engine_register_aead(&aes_drv_ctx.alg.aead);
	if (err < 0) {
		dev_err(dev, "Failed to register AEAD alg.\n");
		goto err_aead;
	}
	return 0;

err_aead:
	crypto_engine_unregister_aead(&aes_drv_ctx.alg.aead);

err_engine:
	if (aes_drv_ctx.engine)
		crypto_engine_exit(aes_drv_ctx.engine);

	return err;
}

static int zynqmp_aes_aead_remove(struct platform_device *pdev)
{
	crypto_engine_exit(aes_drv_ctx.engine);
	crypto_engine_unregister_aead(&aes_drv_ctx.alg.aead);

	return 0;
}

static const struct of_device_id zynqmp_aes_dt_ids[] = {
	{ .compatible = "xlnx,zynqmp-aes" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, zynqmp_aes_dt_ids);

static struct platform_driver zynqmp_aes_driver = {
	.probe	= zynqmp_aes_aead_probe,
	.remove = zynqmp_aes_aead_remove,
	.driver = {
		.name		= "zynqmp-aes",
		.of_match_table = zynqmp_aes_dt_ids,
	},
};

module_platform_driver(zynqmp_aes_driver);
MODULE_LICENSE("GPL");
