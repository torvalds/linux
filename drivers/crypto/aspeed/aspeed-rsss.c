// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2023 Aspeed Technology Inc.
 */

#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include "aspeed-rsss.h"

static inline struct akcipher_request *
	akcipher_request_cast(struct crypto_async_request *req)
{
	return container_of(req, struct akcipher_request, base);
}

static int aspeed_rsss_rsa_do_fallback(struct akcipher_request *req)
{
	struct crypto_akcipher *cipher = crypto_akcipher_reqtfm(req);
	struct aspeed_rsa_ctx *ctx = akcipher_tfm_ctx(cipher);
	int err;

	akcipher_request_set_tfm(req, ctx->fallback_tfm);

	if (ctx->enc)
		err = crypto_akcipher_encrypt(req);
	else
		err = crypto_akcipher_decrypt(req);

	akcipher_request_set_tfm(req, cipher);

	return err;
}

static bool aspeed_rsss_rsa_need_fallback(struct akcipher_request *req)
{
	struct crypto_akcipher *cipher = crypto_akcipher_reqtfm(req);
	struct aspeed_rsa_ctx *ctx = akcipher_tfm_ctx(cipher);

	return ctx->key.n_sz > ASPEED_RSA_MAX_KEY_LEN;
}

static int aspeed_rsss_rsa_handle_queue(struct aspeed_rsss_dev *rsss_dev,
					struct akcipher_request *req)
{
	if (aspeed_rsss_rsa_need_fallback(req)) {
		RSSS_DBG(rsss_dev, "SW fallback\n");
		return aspeed_rsss_rsa_do_fallback(req);
	}

	return crypto_transfer_akcipher_request_to_engine(rsss_dev->crypt_engine_rsa, req);
}

static int aspeed_rsss_rsa_do_request(struct crypto_engine *engine, void *areq)
{
	struct akcipher_request *req = akcipher_request_cast(areq);
	struct crypto_akcipher *cipher = crypto_akcipher_reqtfm(req);
	struct aspeed_rsa_ctx *ctx = akcipher_tfm_ctx(cipher);
	struct aspeed_rsss_dev *rsss_dev = ctx->rsss_dev;
	struct aspeed_engine_rsa *rsa_engine;

	rsa_engine = &rsss_dev->rsa_engine;
	rsa_engine->req = req;
	rsa_engine->flags |= CRYPTO_FLAGS_BUSY;

	return ctx->trigger(rsss_dev);
}

static int aspeed_rsss_rsa_complete(struct aspeed_rsss_dev *rsss_dev, int err)
{
	struct aspeed_engine_rsa *rsa_engine = &rsss_dev->rsa_engine;
	struct akcipher_request *req = rsa_engine->req;

	rsa_engine->flags &= ~CRYPTO_FLAGS_BUSY;

	crypto_finalize_akcipher_request(rsss_dev->crypt_engine_rsa, req, err);

	return err;
}

/*
 * Copy Data to SRAM buffer for engine used.
 */
static void aspeed_rsss_rsa_sg_copy_to_buffer(struct aspeed_rsss_dev *rsss_dev,
					      u8 *buf, struct scatterlist *src,
					      size_t nbytes)
{
	RSSS_DBG(rsss_dev, "\n");
	scatterwalk_map_and_copy(buf, src, 0, nbytes, 0);
}

/*
 * Copy Exp/Mod to SRAM buffer for engine used.
 *
 * Params:
 * - mode 0 : Exponential
 * - mode 1 : Modulus
 */
static int aspeed_rsss_rsa_ctx_copy(struct aspeed_rsss_dev *rsss_dev, void *buf,
				    const void *xbuf, size_t nbytes,
				    enum aspeed_rsa_key_mode mode)
{
	int nbits;

	RSSS_DBG(rsss_dev, "nbytes:%zu, mode:%d\n", nbytes, mode);

	if (nbytes > ASPEED_RSA_MAX_KEY_LEN)
		return -ENOMEM;

	nbits = nbytes * 8;

	memcpy(buf, xbuf, nbytes);

	return nbits;
}

static int aspeed_rsss_rsa_transfer(struct aspeed_rsss_dev *rsss_dev)
{
	struct aspeed_engine_rsa *rsa_engine = &rsss_dev->rsa_engine;
	struct akcipher_request *req = rsa_engine->req;
	struct scatterlist *out_sg = req->dst;
	u32 val;

	RSSS_DBG(rsss_dev, "\n");

	/* Set SRAM access control - CPU */
	val = ast_rsss_read(rsss_dev, ASPEED_RSSS_CTRL);
	ast_rsss_write(rsss_dev, val | SRAM_AHB_MODE_CPU, ASPEED_RSSS_CTRL);

	scatterwalk_map_and_copy(rsa_engine->sram_data, out_sg, 0, req->dst_len, 1);

	memzero_explicit(rsa_engine->sram_data, SRAM_BLOCK_SIZE);

	return aspeed_rsss_rsa_complete(rsss_dev, 0);
}

static int aspeed_rsss_rsa_trigger(struct aspeed_rsss_dev *rsss_dev)
{
	struct aspeed_engine_rsa *rsa_engine = &rsss_dev->rsa_engine;
	struct akcipher_request *req = rsa_engine->req;
	struct crypto_akcipher *cipher;
	struct aspeed_rsa_ctx *ctx;
	int ne, nm;
	u32 val;

	cipher = crypto_akcipher_reqtfm(req);
	ctx = akcipher_tfm_ctx(cipher);

	if (!ctx->n || !ctx->n_sz) {
		dev_err(rsss_dev->dev, "%s: key n is not set\n", __func__);
		return -EINVAL;
	}

	memzero_explicit(rsa_engine->sram_data, SRAM_BLOCK_SIZE);

	/* Set SRAM access control - CPU */
	val = ast_rsss_read(rsss_dev, ASPEED_RSSS_CTRL);
	ast_rsss_write(rsss_dev, val | SRAM_AHB_MODE_CPU, ASPEED_RSSS_CTRL);

	/* Copy source data to SRAM buffer */
	aspeed_rsss_rsa_sg_copy_to_buffer(rsss_dev, rsa_engine->sram_data,
					  req->src, req->src_len);

	nm = aspeed_rsss_rsa_ctx_copy(rsss_dev, rsa_engine->sram_mod, ctx->n,
				      ctx->n_sz, ASPEED_RSA_MOD_MODE);
	if (ctx->enc) {
		if (!ctx->e || !ctx->e_sz) {
			dev_err(rsss_dev->dev, "%s: key e is not set\n",
				__func__);
			return -EINVAL;
		}
		/* Copy key e to SRAM buffer */
		ne = aspeed_rsss_rsa_ctx_copy(rsss_dev, rsa_engine->sram_exp,
					      ctx->e, ctx->e_sz,
					      ASPEED_RSA_EXP_MODE);
	} else {
		if (!ctx->d || !ctx->d_sz) {
			dev_err(rsss_dev->dev, "%s: key d is not set\n",
				__func__);
			return -EINVAL;
		}
		/* Copy key d to SRAM buffer */
		ne = aspeed_rsss_rsa_ctx_copy(rsss_dev, rsa_engine->sram_exp,
					      ctx->key.d, ctx->key.d_sz,
					      ASPEED_RSA_EXP_MODE);
	}

	rsa_engine->resume = aspeed_rsss_rsa_transfer;

	ast_rsss_write(rsss_dev, (ne << 16) + nm,
		       ASPEED_RSA_KEY_INFO);

	/* Set SRAM access control - Engine */
	val = ast_rsss_read(rsss_dev, ASPEED_RSSS_CTRL);
	ast_rsss_write(rsss_dev, val & ~SRAM_AHB_MODE_CPU, ASPEED_RSSS_CTRL);

	/* Trigger RSA engines */
	ast_rsss_write(rsss_dev, RSA_TRIGGER, ASPEED_RSA_TRIGGER);

	return 0;
}

static int aspeed_rsss_rsa_enc(struct akcipher_request *req)
{
	struct crypto_akcipher *cipher = crypto_akcipher_reqtfm(req);
	struct aspeed_rsa_ctx *ctx = akcipher_tfm_ctx(cipher);
	struct aspeed_rsss_dev *rsss_dev = ctx->rsss_dev;

	ctx->trigger = aspeed_rsss_rsa_trigger;
	ctx->enc = 1;

	return aspeed_rsss_rsa_handle_queue(rsss_dev, req);
}

static int aspeed_rsss_rsa_dec(struct akcipher_request *req)
{
	struct crypto_akcipher *cipher = crypto_akcipher_reqtfm(req);
	struct aspeed_rsa_ctx *ctx = akcipher_tfm_ctx(cipher);
	struct aspeed_rsss_dev *rsss_dev = ctx->rsss_dev;

	ctx->trigger = aspeed_rsss_rsa_trigger;
	ctx->enc = 0;

	return aspeed_rsss_rsa_handle_queue(rsss_dev, req);
}

static u8 *aspeed_rsa_key_copy(u8 *src, size_t len)
{
	return kmemdup(src, len, GFP_KERNEL);
}

static int aspeed_rsa_set_n(struct aspeed_rsa_ctx *ctx, u8 *value,
			    size_t len)
{
	ctx->n_sz = len;
	ctx->n = aspeed_rsa_key_copy(value, len);
	if (!ctx->n)
		return -ENOMEM;

	return 0;
}

static int aspeed_rsa_set_e(struct aspeed_rsa_ctx *ctx, u8 *value,
			    size_t len)
{
	ctx->e_sz = len;
	ctx->e = aspeed_rsa_key_copy(value, len);
	if (!ctx->e)
		return -ENOMEM;

	return 0;
}

static int aspeed_rsa_set_d(struct aspeed_rsa_ctx *ctx, u8 *value,
			    size_t len)
{
	ctx->d_sz = len;
	ctx->d = aspeed_rsa_key_copy(value, len);
	if (!ctx->d)
		return -ENOMEM;

	return 0;
}

static void aspeed_rsss_rsa_key_free(struct aspeed_rsa_ctx *ctx)
{
	kfree_sensitive(ctx->n);
	kfree_sensitive(ctx->e);
	kfree_sensitive(ctx->d);
	ctx->n_sz = 0;
	ctx->e_sz = 0;
	ctx->d_sz = 0;
}

static int aspeed_rsss_rsa_setkey(struct crypto_akcipher *tfm, const void *key,
				  unsigned int keylen, int priv)
{
	struct aspeed_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct aspeed_rsss_dev *rsss_dev = ctx->rsss_dev;
	int ret;

	if (priv)
		ret = rsa_parse_priv_key(&ctx->key, key, keylen);
	else
		ret = rsa_parse_pub_key(&ctx->key, key, keylen);

	if (ret) {
		dev_err(rsss_dev->dev, "rsss parse key failed, ret:0x%x\n",
			ret);
		return ret;
	}

	/* Aspeed engine supports up to 4096 bits,
	 * Use software fallback instead.
	 */
	if (ctx->key.n_sz > ASPEED_RSA_MAX_KEY_LEN)
		return 0;

	ret = aspeed_rsa_set_n(ctx, (u8 *)ctx->key.n, ctx->key.n_sz);
	if (ret)
		goto err;

	ret = aspeed_rsa_set_e(ctx, (u8 *)ctx->key.e, ctx->key.e_sz);
	if (ret)
		goto err;

	if (priv) {
		ret = aspeed_rsa_set_d(ctx, (u8 *)ctx->key.d, ctx->key.d_sz);
		if (ret)
			goto err;
	}

	return 0;

err:
	dev_err(rsss_dev->dev, "rsss set key failed\n");
	aspeed_rsss_rsa_key_free(ctx);

	return ret;
}

static int aspeed_rsss_rsa_set_pub_key(struct crypto_akcipher *tfm,
				       const void *key,
				       unsigned int keylen)
{
	struct aspeed_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	int ret;

	ret = crypto_akcipher_set_pub_key(ctx->fallback_tfm, key, keylen);
	if (ret)
		return ret;

	return aspeed_rsss_rsa_setkey(tfm, key, keylen, 0);
}

static int aspeed_rsss_rsa_set_priv_key(struct crypto_akcipher *tfm,
					const void *key,
					unsigned int keylen)
{
	struct aspeed_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	int ret;

	ret = crypto_akcipher_set_priv_key(ctx->fallback_tfm, key, keylen);
	if (ret)
		return ret;

	return aspeed_rsss_rsa_setkey(tfm, key, keylen, 1);
}

static unsigned int aspeed_rsss_rsa_max_size(struct crypto_akcipher *tfm)
{
	struct aspeed_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);

	if (ctx->key.n_sz > ASPEED_RSA_MAX_KEY_LEN)
		return crypto_akcipher_maxsize(ctx->fallback_tfm);

	return ctx->n_sz;
}

static int aspeed_rsss_rsa_init_tfm(struct crypto_akcipher *tfm)
{
	struct aspeed_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct akcipher_alg *alg = crypto_akcipher_alg(tfm);
	const char *name = crypto_tfm_alg_name(&tfm->base);
	struct aspeed_rsss_rsa_alg *rsa_alg;

	rsa_alg = container_of(alg, struct aspeed_rsss_rsa_alg, akcipher);

	ctx->rsss_dev = rsa_alg->rsss_dev;

	ctx->fallback_tfm = crypto_alloc_akcipher(name, 0, CRYPTO_ALG_ASYNC |
						  CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(ctx->fallback_tfm)) {
		dev_err(ctx->rsss_dev->dev, "ERROR: Cannot allocate fallback for %s %ld\n",
			name, PTR_ERR(ctx->fallback_tfm));
		return PTR_ERR(ctx->fallback_tfm);
	}

	ctx->enginectx.op.do_one_request = aspeed_rsss_rsa_do_request;
	ctx->enginectx.op.prepare_request = NULL;
	ctx->enginectx.op.unprepare_request = NULL;

	return 0;
}

static void aspeed_rsss_rsa_exit_tfm(struct crypto_akcipher *tfm)
{
	struct aspeed_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);

	crypto_free_akcipher(ctx->fallback_tfm);
}

static struct aspeed_rsss_rsa_alg aspeed_rsa_akcipher_algs[] = {
	{
		.akcipher = {
			.encrypt = aspeed_rsss_rsa_enc,
			.decrypt = aspeed_rsss_rsa_dec,
			.sign = aspeed_rsss_rsa_dec,
			.verify = aspeed_rsss_rsa_enc,
			.set_pub_key = aspeed_rsss_rsa_set_pub_key,
			.set_priv_key = aspeed_rsss_rsa_set_priv_key,
			.max_size = aspeed_rsss_rsa_max_size,
			.init = aspeed_rsss_rsa_init_tfm,
			.exit = aspeed_rsss_rsa_exit_tfm,
			.base = {
				.cra_name = "rsss-rsa",
				.cra_driver_name = "aspeed-rsss-rsa",
				.cra_priority = 300,
				.cra_flags = CRYPTO_ALG_TYPE_AKCIPHER |
					     CRYPTO_ALG_ASYNC |
					     CRYPTO_ALG_KERN_DRIVER_ONLY |
					     CRYPTO_ALG_NEED_FALLBACK,
				.cra_module = THIS_MODULE,
				.cra_ctxsize = sizeof(struct aspeed_rsa_ctx),
			},
		},
	},
};

static void aspeed_rsss_rsa_register(struct aspeed_rsss_dev *rsss_dev)
{
	int i, rc;

	for (i = 0; i < ARRAY_SIZE(aspeed_rsa_akcipher_algs); i++) {
		aspeed_rsa_akcipher_algs[i].rsss_dev = rsss_dev;
		rc = crypto_register_akcipher(&aspeed_rsa_akcipher_algs[i].akcipher);
		if (rc) {
			RSSS_DBG(rsss_dev, "Failed to register %s\n",
				 aspeed_rsa_akcipher_algs[i].akcipher.base.cra_name);
		}
	}
}

static void aspeed_rsss_rsa_unregister(struct aspeed_rsss_dev *rsss_dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(aspeed_rsa_akcipher_algs); i++)
		crypto_unregister_akcipher(&aspeed_rsa_akcipher_algs[i].akcipher);
}

/* RSSS interrupt service routine. */
static irqreturn_t aspeed_rsss_irq(int irq, void *dev)
{
	struct aspeed_rsss_dev *rsss_dev = (struct aspeed_rsss_dev *)dev;
	struct aspeed_engine_sha3 *sha3_engine = &rsss_dev->sha3_engine;
	struct aspeed_engine_rsa *rsa_engine = &rsss_dev->rsa_engine;
	u32 sts;

	sts = ast_rsss_read(rsss_dev, ASPEED_RSSS_INT_STS);
	ast_rsss_write(rsss_dev, sts, ASPEED_RSSS_INT_STS);

	RSSS_DBG(rsss_dev, "irq sts:0x%x\n", sts);

	if (sts & RSA_INT_DONE) {
		/* Stop RSA engine */
		ast_rsss_write(rsss_dev, 0, ASPEED_RSA_TRIGGER);

		if (rsa_engine->flags & CRYPTO_FLAGS_BUSY)
			tasklet_schedule(&rsa_engine->done_task);
		else
			dev_err(rsss_dev->dev, "RSA no active requests.\n");
	}

	if (sts & SHA3_INT_DONE) {
		if (sha3_engine->flags & CRYPTO_FLAGS_BUSY)
			tasklet_schedule(&sha3_engine->done_task);
		else
			dev_err(rsss_dev->dev, "SHA3 no active requests.\n");
	}

	return IRQ_HANDLED;
}

static void aspeed_rsss_rsa_done_task(unsigned long data)
{
	struct aspeed_rsss_dev *rsss_dev = (struct aspeed_rsss_dev *)data;
	struct aspeed_engine_rsa *rsa_engine = &rsss_dev->rsa_engine;

	(void)rsa_engine->resume(rsss_dev);
}

static void aspeed_rsss_sha3_done_task(unsigned long data)
{
	struct aspeed_rsss_dev *rsss_dev = (struct aspeed_rsss_dev *)data;
	struct aspeed_engine_sha3 *sha3_engine = &rsss_dev->sha3_engine;

	(void)sha3_engine->resume(rsss_dev);
}

static const struct of_device_id aspeed_rsss_of_matches[] = {
	{ .compatible = "aspeed,ast2700-rsss", },
	{},
};

static int aspeed_rsss_probe(struct platform_device *pdev)
{
	struct aspeed_engine_sha3 *sha3_engine;
	struct aspeed_engine_rsa *rsa_engine;
	struct aspeed_rsss_dev *rsss_dev;
	struct device *dev = &pdev->dev;
	int rc;

	rsss_dev = devm_kzalloc(dev, sizeof(struct aspeed_rsss_dev),
				GFP_KERNEL);
	if (!rsss_dev)
		return -ENOMEM;

	rsss_dev->dev = dev;
	rsa_engine = &rsss_dev->rsa_engine;
	sha3_engine = &rsss_dev->sha3_engine;

	platform_set_drvdata(pdev, rsss_dev);

	rsss_dev->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(rsss_dev->regs))
		return PTR_ERR(rsss_dev->regs);

	/* Get irq number and register it */
	rsss_dev->irq = platform_get_irq(pdev, 0);
	if (rsss_dev->irq < 0)
		return -ENXIO;

	rc = devm_request_irq(dev, rsss_dev->irq, aspeed_rsss_irq, 0,
			      dev_name(dev), rsss_dev);
	if (rc) {
		dev_err(dev, "Failed to request irq.\n");
		return rc;
	}

	rsss_dev->clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(rsss_dev->clk)) {
		dev_err(dev, "Failed to get rsss clk\n");
		return PTR_ERR(rsss_dev->clk);
	}

	/* Initialize crypto hardware engine structure for RSA */
	rsss_dev->crypt_engine_rsa = crypto_engine_alloc_init(dev, true);
	if (!rsss_dev->crypt_engine_rsa) {
		rc = -ENOMEM;
		goto clk_exit;
	}

	rc = crypto_engine_start(rsss_dev->crypt_engine_rsa);
	if (rc)
		goto err_engine_rsa_start;

	tasklet_init(&rsa_engine->done_task, aspeed_rsss_rsa_done_task,
		     (unsigned long)rsss_dev);

	/* Initialize crypto hardware engine structure for SHA3 */
	rsss_dev->crypt_engine_sha3 = crypto_engine_alloc_init(dev, true);
	if (!rsss_dev->crypt_engine_sha3) {
		rc = -ENOMEM;
		goto err_engine_rsa_start;
	}

	rc = crypto_engine_start(rsss_dev->crypt_engine_sha3);
	if (rc)
		goto err_engine_sha3_start;

	tasklet_init(&sha3_engine->done_task, aspeed_rsss_sha3_done_task,
		     (unsigned long)rsss_dev);

	aspeed_rsss_rsa_register(rsss_dev);

	rsa_engine->sram_exp = rsss_dev->regs + SRAM_OFFSET_EXP;
	rsa_engine->sram_mod = rsss_dev->regs + SRAM_OFFSET_MOD;
	rsa_engine->sram_data = rsss_dev->regs + SRAM_OFFSET_DATA;

	/* Set SRAM for RSA operation */
	ast_rsss_write(rsss_dev, RSA_OPERATION, ASPEED_RSSS_CTRL);

	/* Enable RSA interrupt */
	ast_rsss_write(rsss_dev, RSA_INT_EN, ASPEED_RSSS_INT_EN);

	dev_info(dev, "Aspeed RSSS Hardware Accelerator successfully registered\n");

	return 0;

err_engine_sha3_start:
	crypto_engine_exit(rsss_dev->crypt_engine_sha3);
err_engine_rsa_start:
	crypto_engine_exit(rsss_dev->crypt_engine_rsa);
clk_exit:
	clk_disable_unprepare(rsss_dev->clk);

	return rc;
}

static int aspeed_rsss_remove(struct platform_device *pdev)
{
	struct aspeed_rsss_dev *rsss_dev = platform_get_drvdata(pdev);
	struct aspeed_engine_sha3 *sha3_engine = &rsss_dev->sha3_engine;
	struct aspeed_engine_rsa *rsa_engine = &rsss_dev->rsa_engine;

	aspeed_rsss_rsa_unregister(rsss_dev);

	crypto_engine_exit(rsss_dev->crypt_engine_rsa);
	crypto_engine_exit(rsss_dev->crypt_engine_rsa);

	tasklet_kill(&rsa_engine->done_task);
	tasklet_kill(&sha3_engine->done_task);

	clk_disable_unprepare(rsss_dev->clk);

	return 0;
}

MODULE_DEVICE_TABLE(of, aspeed_rsss_of_matches);

static struct platform_driver aspeed_rsss_driver = {
	.probe		= aspeed_rsss_probe,
	.remove		= aspeed_rsss_remove,
	.driver		= {
		.name   = KBUILD_MODNAME,
		.of_match_table = aspeed_rsss_of_matches,
	},
};

module_platform_driver(aspeed_rsss_driver);

MODULE_AUTHOR("Neal Liu <neal_liu@aspeedtech.com>");
MODULE_DESCRIPTION("ASPEED RSSS driver for multiple hardware cryptographic engine");
MODULE_LICENSE("GPL");
