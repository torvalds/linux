// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2023 Aspeed Technology Inc.
 */

#include <linux/platform_device.h>
#include "aspeed-rsss.h"

static u8 data_rev[SRAM_BLOCK_SIZE];
static u8 data[SRAM_BLOCK_SIZE];
static int dbg;

static void hexdump(char *name, unsigned char *buf, unsigned int len)
{
	if (!dbg)
		return;

#ifdef CONFIG_CRYPTO_DEV_ASPEED_DEBUG
	pr_info("%s:\n", name);
	print_hex_dump(KERN_CONT, "", DUMP_PREFIX_OFFSET,
		       16, 1, buf, len, false);
#endif
}

static inline struct akcipher_request *
	akcipher_request_cast(struct crypto_async_request *req)
{
	return container_of(req, struct akcipher_request, base);
}

static int aspeed_rsa_do_fallback(struct akcipher_request *req)
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

static bool aspeed_rsa_need_fallback(struct akcipher_request *req)
{
	struct crypto_akcipher *cipher = crypto_akcipher_reqtfm(req);
	struct aspeed_rsa_ctx *ctx = akcipher_tfm_ctx(cipher);

	return ctx->key.n_sz > ASPEED_RSA_MAX_KEY_LEN;
}

static int aspeed_rsa_handle_queue(struct aspeed_rsss_dev *rsss_dev,
				   struct akcipher_request *req)
{
	if (aspeed_rsa_need_fallback(req)) {
		RSSS_DBG(rsss_dev, "SW fallback\n");
		return aspeed_rsa_do_fallback(req);
	}

	return crypto_transfer_akcipher_request_to_engine(rsss_dev->crypt_engine_rsa, req);
}

static int aspeed_rsa_do_request(struct crypto_engine *engine, void *areq)
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

static int aspeed_rsa_complete(struct aspeed_rsss_dev *rsss_dev, int err)
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
static void aspeed_rsa_sg_copy_to_buffer(struct aspeed_rsss_dev *rsss_dev,
					 void __iomem *buf, struct scatterlist *src,
					 size_t nbytes)
{
	RSSS_DBG(rsss_dev, "src len:%zu\n", nbytes);

	memset(data_rev, 0, SRAM_BLOCK_SIZE);
	memset(data, 0, SRAM_BLOCK_SIZE);

	scatterwalk_map_and_copy(data, src, 0, nbytes, 0);

	hexdump("data", data, nbytes);
	for (int i = 0; i < nbytes; i++)
		data_rev[nbytes - i - 1] = data[i];

	/* align 8 bytes */
	memcpy_toio(buf, data_rev, (nbytes + 7) & ~(8 - 1));
}

/*
 * Copy Exp/Mod to SRAM buffer for engine used.
 *
 * Params:
 * - mode 0 : Exponential
 * - mode 1 : Modulus
 */
static int aspeed_rsa_ctx_copy(struct aspeed_rsss_dev *rsss_dev, void __iomem *dst,
			       const u8 *src, size_t nbytes,
			       enum aspeed_rsa_key_mode mode)
{
	RSSS_DBG(rsss_dev, "nbytes:%zu, mode:%d\n", nbytes, mode);

	if (nbytes > ASPEED_RSA_MAX_KEY_LEN)
		return -ENOMEM;

	memset(data, 0, SRAM_BLOCK_SIZE);

	/* Remove leading zeros */
	while (nbytes > 0 && src[0] == 0) {
		src++;
		nbytes--;
	}

	for (int i = 0; i < nbytes; i++)
		data[nbytes - i - 1] = src[i];

	/* align 8 bytes */
	memcpy_toio(dst, data, (nbytes + 7) & ~(8 - 1));

	return nbytes * 8;
}

static int aspeed_rsa_transfer(struct aspeed_rsss_dev *rsss_dev)
{
	struct aspeed_engine_rsa *rsa_engine = &rsss_dev->rsa_engine;
	struct akcipher_request *req = rsa_engine->req;
	struct scatterlist *out_sg = req->dst;
	size_t nbytes = req->dst_len;
	u8 data[SRAM_BLOCK_SIZE];
	u32 val;

	RSSS_DBG(rsss_dev, "nbytes:%zu\n", nbytes);

	/* Set SRAM access control - CPU */
	val = ast_rsss_read(rsss_dev, ASPEED_RSSS_CTRL);
	ast_rsss_write(rsss_dev, val | SRAM_AHB_MODE_CPU, ASPEED_RSSS_CTRL);

	for (int i = 0; i < nbytes; i++)
		data[nbytes - i - 1] = readb(rsa_engine->sram_data + i);

	scatterwalk_map_and_copy(data, out_sg, 0, nbytes, 1);

	return aspeed_rsa_complete(rsss_dev, 0);
}

#ifdef RSSS_RSA_POLLING_MODE
static int aspeed_rsa_wait_complete(struct aspeed_rsss_dev *rsss_dev)
{
	struct aspeed_engine_rsa *rsa_engine = &rsss_dev->rsa_engine;
	u32 sts;
	int ret;

	ret = readl_poll_timeout(rsss_dev->regs + ASPEED_RSA_ENG_STS, sts,
				 ((sts & RSA_STS) == 0x0),
				 ASPEED_RSSS_POLLING_TIME,
				 ASPEED_RSSS_TIMEOUT * 10);
	if (ret) {
		dev_err(rsss_dev->dev, "RSA wrong engine status\n");
		return -EIO;
	}

	ret = readl_poll_timeout(rsss_dev->regs + ASPEED_RSSS_INT_STS, sts,
				 ((sts & RSA_INT_DONE) == RSA_INT_DONE),
				 ASPEED_RSSS_POLLING_TIME,
				 ASPEED_RSSS_TIMEOUT);
	if (ret) {
		dev_err(rsss_dev->dev, "RSA wrong interrupt status\n");
		return -EIO;
	}

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

	return 0;
}
#endif

static int aspeed_rsa_trigger(struct aspeed_rsss_dev *rsss_dev)
{
	struct aspeed_engine_rsa *rsa_engine = &rsss_dev->rsa_engine;
	struct akcipher_request *req = rsa_engine->req;
	struct crypto_akcipher *cipher;
	struct aspeed_rsa_ctx *ctx;
	int ne, nm;
	u32 val;

	RSSS_DBG(rsss_dev, "\n");

	cipher = crypto_akcipher_reqtfm(req);
	ctx = akcipher_tfm_ctx(cipher);

	if (!ctx->n || !ctx->n_sz) {
		dev_err(rsss_dev->dev, "%s: key n is not set\n", __func__);
		return -EINVAL;
	}

	/* Set SRAM access control - CPU */
	val = ast_rsss_read(rsss_dev, ASPEED_RSSS_CTRL);
	ast_rsss_write(rsss_dev, val | SRAM_AHB_MODE_CPU, ASPEED_RSSS_CTRL);

	memset_io(rsa_engine->sram_exp, 0, SRAM_BLOCK_SIZE);
	memset_io(rsa_engine->sram_mod, 0, SRAM_BLOCK_SIZE);
	memset_io(rsa_engine->sram_data, 0, SRAM_BLOCK_SIZE);

	/* Copy source data to SRAM buffer */
	aspeed_rsa_sg_copy_to_buffer(rsss_dev, rsa_engine->sram_data,
				     req->src, req->src_len);

	nm = aspeed_rsa_ctx_copy(rsss_dev, rsa_engine->sram_mod, ctx->n,
				 ctx->n_sz, ASPEED_RSA_MOD_MODE);

	/* Set dst len as modulus size */
	req->dst_len = nm / 8;

	if (ctx->enc) {
		if (!ctx->e || !ctx->e_sz) {
			dev_err(rsss_dev->dev, "%s: key e is not set\n",
				__func__);
			return -EINVAL;
		}
		/* Copy key e to SRAM buffer */
		ne = aspeed_rsa_ctx_copy(rsss_dev, rsa_engine->sram_exp,
					 ctx->e, ctx->e_sz,
					 ASPEED_RSA_EXP_MODE);
	} else {
		if (!ctx->d || !ctx->d_sz) {
			dev_err(rsss_dev->dev, "%s: key d is not set\n",
				__func__);
			return -EINVAL;
		}
		/* Copy key d to SRAM buffer */
		ne = aspeed_rsa_ctx_copy(rsss_dev, rsa_engine->sram_exp,
					 ctx->key.d, ctx->key.d_sz,
					 ASPEED_RSA_EXP_MODE);
	}

	hexdump("exp", rsa_engine->sram_exp, ctx->e_sz);
	hexdump("mod", rsa_engine->sram_mod, ctx->n_sz);
	hexdump("data", rsa_engine->sram_data, req->src_len);

	rsa_engine->resume = aspeed_rsa_transfer;

	ast_rsss_write(rsss_dev, (ne << 16) + nm,
		       ASPEED_RSA_KEY_INFO);

	/* Set SRAM access control - Engine */
	val = ast_rsss_read(rsss_dev, ASPEED_RSSS_CTRL);
	ast_rsss_write(rsss_dev, val & ~SRAM_AHB_MODE_CPU, ASPEED_RSSS_CTRL);

	/* Trigger RSA engines */
	ast_rsss_write(rsss_dev, RSA_TRIGGER, ASPEED_RSA_TRIGGER);

#ifdef RSSS_RSA_POLLING_MODE
	return aspeed_rsa_wait_complete(rsss_dev);
#else
	return 0;
#endif
}

static int aspeed_rsa_enc(struct akcipher_request *req)
{
	struct crypto_akcipher *cipher = crypto_akcipher_reqtfm(req);
	struct aspeed_rsa_ctx *ctx = akcipher_tfm_ctx(cipher);
	struct aspeed_rsss_dev *rsss_dev = ctx->rsss_dev;

	ctx->trigger = aspeed_rsa_trigger;
	ctx->enc = 1;

	return aspeed_rsa_handle_queue(rsss_dev, req);
}

static int aspeed_rsa_dec(struct akcipher_request *req)
{
	struct crypto_akcipher *cipher = crypto_akcipher_reqtfm(req);
	struct aspeed_rsa_ctx *ctx = akcipher_tfm_ctx(cipher);
	struct aspeed_rsss_dev *rsss_dev = ctx->rsss_dev;

	ctx->trigger = aspeed_rsa_trigger;
	ctx->enc = 0;

	return aspeed_rsa_handle_queue(rsss_dev, req);
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

static void aspeed_rsa_key_free(struct aspeed_rsa_ctx *ctx)
{
	kfree_sensitive(ctx->n);
	kfree_sensitive(ctx->e);
	kfree_sensitive(ctx->d);
	ctx->n_sz = 0;
	ctx->e_sz = 0;
	ctx->d_sz = 0;
}

static int aspeed_rsa_setkey(struct crypto_akcipher *tfm, const void *key,
			     unsigned int keylen, int priv)
{
	struct aspeed_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct aspeed_rsss_dev *rsss_dev = ctx->rsss_dev;
	int ret;

	RSSS_DBG(rsss_dev, "\n");

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

	hexdump("n", (u8 *)ctx->key.n, ctx->key.n_sz);
	ret = aspeed_rsa_set_n(ctx, (u8 *)ctx->key.n, ctx->key.n_sz);
	if (ret)
		goto err;

	hexdump("e", (u8 *)ctx->key.e, ctx->key.e_sz);
	ret = aspeed_rsa_set_e(ctx, (u8 *)ctx->key.e, ctx->key.e_sz);
	if (ret)
		goto err;

	if (priv) {
		hexdump("d", (u8 *)ctx->key.d, ctx->key.d_sz);
		ret = aspeed_rsa_set_d(ctx, (u8 *)ctx->key.d, ctx->key.d_sz);
		if (ret)
			goto err;
	}

	return 0;

err:
	dev_err(rsss_dev->dev, "rsss set key failed\n");
	aspeed_rsa_key_free(ctx);

	return ret;
}

static int aspeed_rsa_set_pub_key(struct crypto_akcipher *tfm,
				  const void *key,
				  unsigned int keylen)
{
	struct aspeed_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	int ret;

	ret = crypto_akcipher_set_pub_key(ctx->fallback_tfm, key, keylen);
	if (ret)
		return ret;

	return aspeed_rsa_setkey(tfm, key, keylen, 0);
}

static int aspeed_rsa_set_priv_key(struct crypto_akcipher *tfm,
				   const void *key,
				   unsigned int keylen)
{
	struct aspeed_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	int ret;

	ret = crypto_akcipher_set_priv_key(ctx->fallback_tfm, key, keylen);
	if (ret)
		return ret;

	return aspeed_rsa_setkey(tfm, key, keylen, 1);
}

static unsigned int aspeed_rsa_max_size(struct crypto_akcipher *tfm)
{
	struct aspeed_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);

	if (ctx->key.n_sz > ASPEED_RSA_MAX_KEY_LEN)
		return crypto_akcipher_maxsize(ctx->fallback_tfm);

	return ctx->n_sz;
}

static int aspeed_rsa_init_tfm(struct crypto_akcipher *tfm)
{
	struct aspeed_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct akcipher_alg *alg = crypto_akcipher_alg(tfm);
	const char *name = crypto_tfm_alg_name(&tfm->base);
	struct aspeed_rsss_alg *rsa_alg;

	rsa_alg = container_of(alg, struct aspeed_rsss_alg, alg.akcipher.base);

	ctx->rsss_dev = rsa_alg->rsss_dev;

	ctx->fallback_tfm = crypto_alloc_akcipher(name, 0, CRYPTO_ALG_ASYNC |
						  CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(ctx->fallback_tfm)) {
		dev_err(ctx->rsss_dev->dev, "ERROR: Cannot allocate fallback for %s %ld\n",
			name, PTR_ERR(ctx->fallback_tfm));
		return PTR_ERR(ctx->fallback_tfm);
	}

	return 0;
}

static void aspeed_rsa_exit_tfm(struct crypto_akcipher *tfm)
{
	struct aspeed_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);

	crypto_free_akcipher(ctx->fallback_tfm);
}

struct aspeed_rsss_alg aspeed_rsss_algs_rsa = {
	.type = ASPEED_ALGO_TYPE_AKCIPHER,
	.alg.akcipher.base = {
		.encrypt = aspeed_rsa_enc,
		.decrypt = aspeed_rsa_dec,
		.sign = aspeed_rsa_dec,
		.verify = aspeed_rsa_enc,
		.set_pub_key = aspeed_rsa_set_pub_key,
		.set_priv_key = aspeed_rsa_set_priv_key,
		.max_size = aspeed_rsa_max_size,
		.init = aspeed_rsa_init_tfm,
		.exit = aspeed_rsa_exit_tfm,
		.base = {
			.cra_name = "rsa",
			.cra_driver_name = "aspeed-rsa",
			.cra_priority = 300,
			.cra_flags = CRYPTO_ALG_TYPE_AKCIPHER |
				CRYPTO_ALG_ASYNC |
				CRYPTO_ALG_KERN_DRIVER_ONLY |
				CRYPTO_ALG_NEED_FALLBACK,
			.cra_module = THIS_MODULE,
			.cra_ctxsize = sizeof(struct aspeed_rsa_ctx),
		},
	},
	.alg.akcipher.op = {
		.do_one_request = aspeed_rsa_do_request,
	},
};

static void aspeed_rsa_done_task(unsigned long data)
{
	struct aspeed_rsss_dev *rsss_dev = (struct aspeed_rsss_dev *)data;
	struct aspeed_engine_rsa *rsa_engine = &rsss_dev->rsa_engine;

	(void)rsa_engine->resume(rsss_dev);
}

void aspeed_rsss_rsa_exit(struct aspeed_rsss_dev *rsss_dev)
{
	struct aspeed_engine_rsa *rsa_engine = &rsss_dev->rsa_engine;

	crypto_engine_exit(rsss_dev->crypt_engine_rsa);
	tasklet_kill(&rsa_engine->done_task);
}

int aspeed_rsss_rsa_init(struct aspeed_rsss_dev *rsss_dev)
{
	struct aspeed_engine_rsa *rsa_engine;
	u32 val;
	int rc;

	rc = reset_control_deassert(rsss_dev->reset_rsa);
	if (rc) {
		dev_err(rsss_dev->dev, "Deassert RSA reset failed\n");
		goto end;
	}

	rsa_engine = &rsss_dev->rsa_engine;

	/* Initialize crypto hardware engine structure for RSA */
	rsss_dev->crypt_engine_rsa = crypto_engine_alloc_init(rsss_dev->dev, true);
	if (!rsss_dev->crypt_engine_rsa) {
		rc = -ENOMEM;
		goto end;
	}

	rc = crypto_engine_start(rsss_dev->crypt_engine_rsa);
	if (rc)
		goto err_engine_rsa_start;

	tasklet_init(&rsa_engine->done_task, aspeed_rsa_done_task,
		     (unsigned long)rsss_dev);

	rsa_engine->sram_exp = rsss_dev->regs + SRAM_OFFSET_EXP;
	rsa_engine->sram_mod = rsss_dev->regs + SRAM_OFFSET_MOD;
	rsa_engine->sram_data = rsss_dev->regs + SRAM_OFFSET_DATA;

	/* Set SRAM for RSA operation */
	ast_rsss_write(rsss_dev, RSA_OPERATION, ASPEED_RSSS_CTRL);

	/* Enable RSA interrupt */
	val = ast_rsss_read(rsss_dev, ASPEED_RSSS_INT_EN);
	ast_rsss_write(rsss_dev, val | RSA_INT_EN, ASPEED_RSSS_INT_EN);

	dev_info(rsss_dev->dev, "Aspeed RSSS RSA initialized\n");

	return 0;

err_engine_rsa_start:
	crypto_engine_exit(rsss_dev->crypt_engine_rsa);
end:
	return rc;
}
