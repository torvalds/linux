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
#define XILINX_AES_AUTH_SIZE		16U
#define XILINX_AES_BLK_SIZE		1U
#define ZYNQMP_AES_MIN_INPUT_BLK_SIZE	4U
#define ZYNQMP_AES_WORD_LEN		4U

#define ZYNQMP_AES_GCM_TAG_MISMATCH_ERR	0x01
#define ZYNQMP_AES_WRONG_KEY_SRC_ERR	0x13
#define ZYNQMP_AES_PUF_NOT_PROGRAMMED	0xE300
#define XILINX_KEY_MAGIC		0x3EA0

enum xilinx_aead_op {
	XILINX_AES_DECRYPT = 0,
	XILINX_AES_ENCRYPT
};

enum zynqmp_aead_keysrc {
	ZYNQMP_AES_KUP_KEY = 0,
	ZYNQMP_AES_DEV_KEY,
	ZYNQMP_AES_PUF_KEY
};

struct xilinx_aead_dev {
	struct device *dev;
	struct crypto_engine *engine;
	struct xilinx_aead_alg *aead_algs;
};

struct xilinx_aead_alg {
	struct xilinx_aead_dev *aead_dev;
	struct aead_engine_alg aead;
	u8 dma_bit_mask;
};

struct xilinx_hwkey_info {
	u16 magic;
	u16 type;
} __packed;

struct zynqmp_aead_hw_req {
	u64 src;
	u64 iv;
	u64 key;
	u64 dst;
	u64 size;
	u64 op;
	u64 keysrc;
};

struct xilinx_aead_tfm_ctx {
	struct device *dev;
	dma_addr_t key_dma_addr;
	u8 *key;
	u32 keylen;
	u32 authsize;
	u8 keysrc;
	struct crypto_aead *fbk_cipher;
};

struct xilinx_aead_req_ctx {
	enum xilinx_aead_op op;
};

static struct xilinx_aead_dev *aead_dev;

static int zynqmp_aes_aead_cipher(struct aead_request *req)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct xilinx_aead_tfm_ctx *tfm_ctx = crypto_aead_ctx(aead);
	struct xilinx_aead_req_ctx *rq_ctx = aead_request_ctx(req);
	dma_addr_t dma_addr_data, dma_addr_hw_req;
	struct device *dev = tfm_ctx->dev;
	struct zynqmp_aead_hw_req *hwreq;
	unsigned int data_size;
	unsigned int status;
	int ret;
	size_t dma_size;
	void *dmabuf;
	char *kbuf;

	dma_size = req->cryptlen + XILINX_AES_AUTH_SIZE;
	kbuf = kmalloc(dma_size, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	dmabuf = kmalloc(sizeof(*hwreq) + GCM_AES_IV_SIZE, GFP_KERNEL);
	if (!dmabuf) {
		kfree(kbuf);
		return -ENOMEM;
	}
	hwreq = dmabuf;
	data_size = req->cryptlen;
	scatterwalk_map_and_copy(kbuf, req->src, 0, req->cryptlen, 0);
	memcpy(dmabuf + sizeof(struct zynqmp_aead_hw_req), req->iv, GCM_AES_IV_SIZE);
	dma_addr_data = dma_map_single(dev, kbuf, dma_size, DMA_BIDIRECTIONAL);
	if (unlikely(dma_mapping_error(dev, dma_addr_data))) {
		ret = -ENOMEM;
		goto freemem;
	}

	hwreq->src = dma_addr_data;
	hwreq->dst = dma_addr_data;
	hwreq->keysrc = tfm_ctx->keysrc;
	hwreq->op = rq_ctx->op;

	if (hwreq->op == XILINX_AES_ENCRYPT)
		hwreq->size = data_size;
	else
		hwreq->size = data_size - XILINX_AES_AUTH_SIZE;

	if (hwreq->keysrc == ZYNQMP_AES_KUP_KEY)
		hwreq->key = tfm_ctx->key_dma_addr;
	else
		hwreq->key = 0;

	dma_addr_hw_req = dma_map_single(dev, dmabuf, sizeof(struct zynqmp_aead_hw_req) +
					 GCM_AES_IV_SIZE,
					 DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(dev, dma_addr_hw_req))) {
		ret = -ENOMEM;
		dma_unmap_single(dev, dma_addr_data, dma_size, DMA_BIDIRECTIONAL);
		goto freemem;
	}
	hwreq->iv = dma_addr_hw_req + sizeof(struct zynqmp_aead_hw_req);
	dma_sync_single_for_device(dev, dma_addr_hw_req, sizeof(struct zynqmp_aead_hw_req) +
				   GCM_AES_IV_SIZE, DMA_TO_DEVICE);
	ret = zynqmp_pm_aes_engine(dma_addr_hw_req, &status);
	dma_unmap_single(dev, dma_addr_hw_req, sizeof(struct zynqmp_aead_hw_req) + GCM_AES_IV_SIZE,
			 DMA_TO_DEVICE);
	dma_unmap_single(dev, dma_addr_data, dma_size, DMA_BIDIRECTIONAL);
	if (ret) {
		dev_err(dev, "ERROR: AES PM API failed\n");
	} else if (status) {
		switch (status) {
		case ZYNQMP_AES_GCM_TAG_MISMATCH_ERR:
			ret = -EBADMSG;
			break;
		case ZYNQMP_AES_WRONG_KEY_SRC_ERR:
			ret = -EINVAL;
			dev_err(dev, "ERROR: Wrong KeySrc, enable secure mode\n");
			break;
		case ZYNQMP_AES_PUF_NOT_PROGRAMMED:
			ret = -EINVAL;
			dev_err(dev, "ERROR: PUF is not registered\n");
			break;
		default:
			ret = -EINVAL;
			break;
		}
	} else {
		if (hwreq->op == XILINX_AES_ENCRYPT)
			data_size = data_size + crypto_aead_authsize(aead);
		else
			data_size = data_size - XILINX_AES_AUTH_SIZE;

		sg_copy_from_buffer(req->dst, sg_nents(req->dst),
				    kbuf, data_size);
		ret = 0;
	}

freemem:
	memzero_explicit(kbuf, dma_size);
	kfree(kbuf);
	memzero_explicit(dmabuf, sizeof(struct zynqmp_aead_hw_req) + GCM_AES_IV_SIZE);
	kfree(dmabuf);

	return ret;
}

static int zynqmp_fallback_check(struct xilinx_aead_tfm_ctx *tfm_ctx,
				 struct aead_request *req)
{
	struct xilinx_aead_req_ctx *rq_ctx = aead_request_ctx(req);

	if (tfm_ctx->authsize != XILINX_AES_AUTH_SIZE && rq_ctx->op == XILINX_AES_DECRYPT)
		return 1;

	if (req->assoclen != 0 ||
	    req->cryptlen < ZYNQMP_AES_MIN_INPUT_BLK_SIZE)
		return 1;
	if (tfm_ctx->keylen == AES_KEYSIZE_128 ||
	    tfm_ctx->keylen == AES_KEYSIZE_192)
		return 1;

	if ((req->cryptlen % ZYNQMP_AES_WORD_LEN) != 0)
		return 1;

	if (rq_ctx->op == XILINX_AES_DECRYPT &&
	    req->cryptlen <= XILINX_AES_AUTH_SIZE)
		return 1;

	return 0;
}

static int xilinx_handle_aes_req(struct crypto_engine *engine, void *req)
{
	struct aead_request *areq =
				container_of(req, struct aead_request, base);
	int err;

	err = zynqmp_aes_aead_cipher(areq);
	local_bh_disable();
	crypto_finalize_aead_request(engine, areq, err);
	local_bh_enable();

	return 0;
}

static int zynqmp_aes_aead_setkey(struct crypto_aead *aead, const u8 *key,
				  unsigned int keylen)
{
	struct crypto_tfm *tfm = crypto_aead_tfm(aead);
	struct xilinx_aead_tfm_ctx *tfm_ctx = crypto_tfm_ctx(tfm);
	int err;

	if (keylen == AES_KEYSIZE_256) {
		memcpy(tfm_ctx->key, key, keylen);
		dma_sync_single_for_device(tfm_ctx->dev, tfm_ctx->key_dma_addr,
					   AES_KEYSIZE_256,
					   DMA_TO_DEVICE);
	}

	tfm_ctx->fbk_cipher->base.crt_flags &= ~CRYPTO_TFM_REQ_MASK;
	tfm_ctx->fbk_cipher->base.crt_flags |= (aead->base.crt_flags &
						CRYPTO_TFM_REQ_MASK);

	err = crypto_aead_setkey(tfm_ctx->fbk_cipher, key, keylen);
	if (err)
		goto err;
	tfm_ctx->keylen = keylen;
	tfm_ctx->keysrc = ZYNQMP_AES_KUP_KEY;
err:
	return err;
}

static int zynqmp_paes_aead_setkey(struct crypto_aead *aead, const u8 *key,
				   unsigned int keylen)
{
	struct crypto_tfm *tfm = crypto_aead_tfm(aead);
	struct xilinx_aead_tfm_ctx *tfm_ctx = crypto_tfm_ctx(tfm);
	struct xilinx_hwkey_info hwkey;
	unsigned char keysrc;
	int err = -EINVAL;

	if (keylen != sizeof(struct xilinx_hwkey_info))
		return -EINVAL;
	memcpy(&hwkey, key, sizeof(struct xilinx_hwkey_info));
	if (hwkey.magic != XILINX_KEY_MAGIC)
		return -EINVAL;
	keysrc = hwkey.type;
	if (keysrc == ZYNQMP_AES_DEV_KEY ||
	    keysrc == ZYNQMP_AES_PUF_KEY) {
		tfm_ctx->keysrc = keysrc;
		tfm_ctx->keylen = sizeof(struct xilinx_hwkey_info);
		err = 0;
	}

	return err;
}

static int xilinx_aes_aead_setauthsize(struct crypto_aead *aead,
				       unsigned int authsize)
{
	struct crypto_tfm *tfm = crypto_aead_tfm(aead);
	struct xilinx_aead_tfm_ctx *tfm_ctx = crypto_tfm_ctx(tfm);

	tfm_ctx->authsize = authsize;
	return tfm_ctx->fbk_cipher ? crypto_aead_setauthsize(tfm_ctx->fbk_cipher, authsize) : 0;
}

static int xilinx_aes_fallback_crypt(struct aead_request *req, bool encrypt)
{
	struct aead_request *subreq = aead_request_ctx(req);
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct xilinx_aead_tfm_ctx *tfm_ctx = crypto_aead_ctx(aead);

	aead_request_set_tfm(subreq, tfm_ctx->fbk_cipher);
	aead_request_set_callback(subreq, req->base.flags, NULL, NULL);
	aead_request_set_crypt(subreq, req->src, req->dst,
			       req->cryptlen, req->iv);
	aead_request_set_ad(subreq, req->assoclen);

	return encrypt ? crypto_aead_encrypt(subreq) : crypto_aead_decrypt(subreq);
}

static int zynqmp_aes_aead_encrypt(struct aead_request *req)
{
	struct xilinx_aead_req_ctx *rq_ctx = aead_request_ctx(req);
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct xilinx_aead_tfm_ctx *tfm_ctx = crypto_aead_ctx(aead);
	struct aead_alg *alg = crypto_aead_alg(aead);
	struct xilinx_aead_alg *drv_ctx;
	int err;

	drv_ctx = container_of(alg, struct xilinx_aead_alg, aead.base);
	if (tfm_ctx->keysrc == ZYNQMP_AES_KUP_KEY &&
	    tfm_ctx->keylen == sizeof(struct xilinx_hwkey_info))
		return -EINVAL;

	rq_ctx->op = XILINX_AES_ENCRYPT;
	err = zynqmp_fallback_check(tfm_ctx, req);
	if (err && tfm_ctx->keysrc != ZYNQMP_AES_KUP_KEY)
		return -EOPNOTSUPP;

	if (err)
		return xilinx_aes_fallback_crypt(req, true);

	return crypto_transfer_aead_request_to_engine(drv_ctx->aead_dev->engine, req);
}

static int zynqmp_aes_aead_decrypt(struct aead_request *req)
{
	struct xilinx_aead_req_ctx *rq_ctx = aead_request_ctx(req);
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct xilinx_aead_tfm_ctx *tfm_ctx = crypto_aead_ctx(aead);
	struct aead_alg *alg = crypto_aead_alg(aead);
	struct xilinx_aead_alg *drv_ctx;
	int err;

	rq_ctx->op = XILINX_AES_DECRYPT;
	drv_ctx = container_of(alg, struct xilinx_aead_alg, aead.base);
	if (tfm_ctx->keysrc == ZYNQMP_AES_KUP_KEY &&
	    tfm_ctx->keylen == sizeof(struct xilinx_hwkey_info))
		return -EINVAL;
	err = zynqmp_fallback_check(tfm_ctx, req);
	if (err && tfm_ctx->keysrc != ZYNQMP_AES_KUP_KEY)
		return -EOPNOTSUPP;
	if (err)
		return xilinx_aes_fallback_crypt(req, false);

	return crypto_transfer_aead_request_to_engine(drv_ctx->aead_dev->engine, req);
}

static int xilinx_paes_aead_init(struct crypto_aead *aead)
{
	struct crypto_tfm *tfm = crypto_aead_tfm(aead);
	struct xilinx_aead_tfm_ctx *tfm_ctx = crypto_tfm_ctx(tfm);
	struct xilinx_aead_alg *drv_alg;
	struct aead_alg *alg = crypto_aead_alg(aead);

	drv_alg = container_of(alg, struct xilinx_aead_alg, aead.base);
	tfm_ctx->dev = drv_alg->aead_dev->dev;
	tfm_ctx->keylen = 0;
	tfm_ctx->key = NULL;
	tfm_ctx->fbk_cipher = NULL;
	crypto_aead_set_reqsize(aead, sizeof(struct xilinx_aead_req_ctx));

	return 0;
}

static int xilinx_aes_aead_init(struct crypto_aead *aead)
{
	struct crypto_tfm *tfm = crypto_aead_tfm(aead);
	struct xilinx_aead_tfm_ctx *tfm_ctx = crypto_tfm_ctx(tfm);
	struct xilinx_aead_alg *drv_ctx;
	struct aead_alg *alg = crypto_aead_alg(aead);

	drv_ctx = container_of(alg, struct xilinx_aead_alg, aead.base);
	tfm_ctx->dev = drv_ctx->aead_dev->dev;
	tfm_ctx->keylen = 0;

	tfm_ctx->fbk_cipher = crypto_alloc_aead(drv_ctx->aead.base.base.cra_name,
						0,
						CRYPTO_ALG_NEED_FALLBACK);

	if (IS_ERR(tfm_ctx->fbk_cipher)) {
		pr_err("%s() Error: failed to allocate fallback for %s\n",
		       __func__, drv_ctx->aead.base.base.cra_name);
		return PTR_ERR(tfm_ctx->fbk_cipher);
	}
	tfm_ctx->key = kmalloc(AES_KEYSIZE_256, GFP_KERNEL);
	if (!tfm_ctx->key) {
		crypto_free_aead(tfm_ctx->fbk_cipher);
		return -ENOMEM;
	}
	tfm_ctx->key_dma_addr = dma_map_single(tfm_ctx->dev, tfm_ctx->key,
					       AES_KEYSIZE_256,
					       DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(tfm_ctx->dev, tfm_ctx->key_dma_addr))) {
		kfree(tfm_ctx->key);
		crypto_free_aead(tfm_ctx->fbk_cipher);
		tfm_ctx->fbk_cipher = NULL;
		return -ENOMEM;
	}
	crypto_aead_set_reqsize(aead,
				max(sizeof(struct xilinx_aead_req_ctx),
				    sizeof(struct aead_request) +
				    crypto_aead_reqsize(tfm_ctx->fbk_cipher)));
	return 0;
}

static void xilinx_paes_aead_exit(struct crypto_aead *aead)
{
	struct crypto_tfm *tfm = crypto_aead_tfm(aead);
	struct xilinx_aead_tfm_ctx *tfm_ctx = crypto_tfm_ctx(tfm);

	memzero_explicit(tfm_ctx, sizeof(struct xilinx_aead_tfm_ctx));
}

static void xilinx_aes_aead_exit(struct crypto_aead *aead)
{
	struct crypto_tfm *tfm = crypto_aead_tfm(aead);
	struct xilinx_aead_tfm_ctx *tfm_ctx = crypto_tfm_ctx(tfm);

	dma_unmap_single(tfm_ctx->dev, tfm_ctx->key_dma_addr, AES_KEYSIZE_256, DMA_TO_DEVICE);
	kfree(tfm_ctx->key);
	if (tfm_ctx->fbk_cipher) {
		crypto_free_aead(tfm_ctx->fbk_cipher);
		tfm_ctx->fbk_cipher = NULL;
	}
	memzero_explicit(tfm_ctx, sizeof(struct xilinx_aead_tfm_ctx));
}

static struct xilinx_aead_alg zynqmp_aes_algs[] = {
	{
		.aead.base = {
			.setkey		= zynqmp_aes_aead_setkey,
			.setauthsize	= xilinx_aes_aead_setauthsize,
			.encrypt	= zynqmp_aes_aead_encrypt,
			.decrypt	= zynqmp_aes_aead_decrypt,
			.init		= xilinx_aes_aead_init,
			.exit		= xilinx_aes_aead_exit,
			.ivsize		= GCM_AES_IV_SIZE,
			.maxauthsize	= XILINX_AES_AUTH_SIZE,
			.base = {
				.cra_name		= "gcm(aes)",
				.cra_driver_name	= "xilinx-zynqmp-aes-gcm",
				.cra_priority		= 200,
			.cra_flags		= CRYPTO_ALG_TYPE_AEAD |
				CRYPTO_ALG_ASYNC |
				CRYPTO_ALG_ALLOCATES_MEMORY |
				CRYPTO_ALG_KERN_DRIVER_ONLY |
				CRYPTO_ALG_NEED_FALLBACK,
			.cra_blocksize		= XILINX_AES_BLK_SIZE,
			.cra_ctxsize		= sizeof(struct xilinx_aead_tfm_ctx),
			.cra_module		= THIS_MODULE,
			}
		},
		.aead.op = {
			.do_one_request = xilinx_handle_aes_req,
		},
		.dma_bit_mask = ZYNQMP_DMA_BIT_MASK,
	},
	{
		.aead.base = {
			.setkey		= zynqmp_paes_aead_setkey,
			.setauthsize	= xilinx_aes_aead_setauthsize,
			.encrypt	= zynqmp_aes_aead_encrypt,
			.decrypt	= zynqmp_aes_aead_decrypt,
			.init		= xilinx_paes_aead_init,
			.exit		= xilinx_paes_aead_exit,
			.ivsize		= GCM_AES_IV_SIZE,
			.maxauthsize	= XILINX_AES_AUTH_SIZE,
			.base = {
				.cra_name		= "gcm(paes)",
				.cra_driver_name	= "xilinx-zynqmp-paes-gcm",
				.cra_priority		= 200,
			.cra_flags		= CRYPTO_ALG_TYPE_AEAD |
				CRYPTO_ALG_ASYNC |
				CRYPTO_ALG_ALLOCATES_MEMORY |
				CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_blocksize		= XILINX_AES_BLK_SIZE,
			.cra_ctxsize		= sizeof(struct xilinx_aead_tfm_ctx),
			.cra_module		= THIS_MODULE,
			}
		},
		.aead.op = {
			.do_one_request = xilinx_handle_aes_req,
		},
		.dma_bit_mask = ZYNQMP_DMA_BIT_MASK,
	},
	{ /* sentinel */ }
};

static struct xlnx_feature aes_feature_map[] = {
	{
		.family = PM_ZYNQMP_FAMILY_CODE,
		.feature_id = PM_SECURE_AES,
		.data = zynqmp_aes_algs,
	},
	{ /* sentinel */ }
};

static int xilinx_aes_aead_probe(struct platform_device *pdev)
{
	struct xilinx_aead_alg *aead_algs;
	struct device *dev = &pdev->dev;
	int err;
	int i;

	/* Verify the hardware is present */
	aead_algs = xlnx_get_crypto_dev_data(aes_feature_map);
	if (IS_ERR(aead_algs)) {
		dev_err(dev, "AES is not supported on the platform\n");
		return PTR_ERR(aead_algs);
	}

	/* ZynqMP AES driver supports only one instance */
	if (aead_dev)
		return -ENODEV;

	aead_dev = devm_kzalloc(dev, sizeof(*aead_dev), GFP_KERNEL);
	if (!aead_dev)
		return -ENOMEM;
	aead_dev->dev = dev;
	aead_dev->aead_algs = aead_algs;
	platform_set_drvdata(pdev, aead_dev);
	err = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(aead_algs[0].dma_bit_mask));
	if (err < 0) {
		dev_err(dev, "No usable DMA configuration\n");
		return err;
	}

	aead_dev->engine = crypto_engine_alloc_init(dev, 1);
	if (!aead_dev->engine) {
		dev_err(dev, "Cannot alloc AES engine\n");
		err = -ENOMEM;
		goto err_engine;
	}

	err = crypto_engine_start(aead_dev->engine);
	if (err) {
		dev_err(dev, "Cannot start AES engine\n");
		goto err_engine;
	}

	for (i = 0; aead_dev->aead_algs[i].dma_bit_mask; i++) {
		aead_dev->aead_algs[i].aead_dev = aead_dev;
		err = crypto_engine_register_aead(&aead_dev->aead_algs[i].aead);
		if (err < 0) {
			dev_err(dev, "Failed to register AEAD alg %d.\n", i);
			goto err_alg_register;
		}
	}
	return 0;

	return 0;

err_alg_register:
	while (i > 0)
		crypto_engine_unregister_aead(&aead_dev->aead_algs[--i].aead);
err_engine:
	crypto_engine_exit(aead_dev->engine);

	return err;
}

static void xilinx_aes_aead_remove(struct platform_device *pdev)
{
	aead_dev = platform_get_drvdata(pdev);
	crypto_engine_exit(aead_dev->engine);
	for (int i = 0; aead_dev->aead_algs[i].dma_bit_mask; i++)
		crypto_engine_unregister_aead(&aead_dev->aead_algs[i].aead);

	 aead_dev = NULL;
}

static struct platform_driver xilinx_aes_driver = {
	.probe	= xilinx_aes_aead_probe,
	.remove = xilinx_aes_aead_remove,
	.driver = {
		.name		= "zynqmp-aes",
	},
};

static struct platform_device *platform_dev;

static int __init aes_driver_init(void)
{
	int ret;

	ret = platform_driver_register(&xilinx_aes_driver);
	if (ret)
		return ret;

	platform_dev = platform_device_register_simple(xilinx_aes_driver.driver.name,
						       0, NULL, 0);
	if (IS_ERR(platform_dev)) {
		ret = PTR_ERR(platform_dev);
		platform_driver_unregister(&xilinx_aes_driver);
	}

	return ret;
}

static void __exit aes_driver_exit(void)
{
	platform_device_unregister(platform_dev);
	platform_driver_unregister(&xilinx_aes_driver);
}

module_init(aes_driver_init);
module_exit(aes_driver_exit);
MODULE_DESCRIPTION("zynqmp aes-gcm hardware acceleration support.");
MODULE_LICENSE("GPL");
