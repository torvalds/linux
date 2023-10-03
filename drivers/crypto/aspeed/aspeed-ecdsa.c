// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2023 Aspeed Technology Inc.
 */

#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/module.h>
#include <linux/asn1_decoder.h>
#include <linux/scatterlist.h>
#include <linux/iopoll.h>
#include <linux/interrupt.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <crypto/internal/akcipher.h>
#include <crypto/internal/ecc.h>
#include <crypto/akcipher.h>
#include <crypto/ecdh.h>
#include <crypto/sha2.h>

#include "aspeed-ecdsa.h"

#ifdef CONFIG_CRYPTO_DEV_ASPEED_DEBUG
static void hexdump(unsigned char *buf, unsigned int len)
{
	print_hex_dump(KERN_CONT, "", DUMP_PREFIX_OFFSET,
		       16, 1,
		       buf, len, false);
}
#else
static void hexdump(unsigned char *buf, unsigned int len)
{
	/* empty */
}
#endif

static void buff_reverse(u8 *dst, u8 *src, int len)
{
	for (int i = 0; i < len; i++)
		dst[len - i - 1] = src[i];
}

static bool aspeed_ecdsa_need_fallback(struct aspeed_ecc_ctx *ctx, int d_len)
{
	int curve_id = ctx->curve_id;

	switch (curve_id) {
	case ECC_CURVE_NIST_P256:
		if (d_len != SHA256_DIGEST_SIZE)
			return true;
		break;
	case ECC_CURVE_NIST_P384:
		if (d_len != SHA384_DIGEST_SIZE)
			return true;
		break;
	}

	return false;
}

static int aspeed_ecdsa_trigger(struct aspeed_ecdsa_dev *ecdsa_dev)
{
	u32 val, sts;
	int ret;

	ast_write(ecdsa_dev, 0x1, ASPEED_ECC_ECDSA_VERIFY);

	ast_write(ecdsa_dev, ECC_EN, ASPEED_ECC_CMD_REG);
	ast_write(ecdsa_dev, 0x0, ASPEED_ECC_CMD_REG);

	ret = readl_poll_timeout(ecdsa_dev->regs + ASPEED_ECC_STS_REG, sts,
				 ((sts & ECC_IDLE) == ECC_IDLE),
				 ASPEED_ECC_POLLING_TIME,
				 ASPEED_ECC_TIMEOUT * 10);
	if (ret) {
		dev_err(ecdsa_dev->dev, "ECC engine wrong status\n");
		return -EIO;
	}

	val = ast_read(ecdsa_dev, ASPEED_ECC_STS_REG) & ECC_VERIFY_PASS;
	if (val == ECC_VERIFY_PASS) {
		AST_DBG(ecdsa_dev, "Verify PASS !\n");
		return 0;
	}

	AST_DBG(ecdsa_dev, "Verify FAILED !\n");

	return -EKEYREJECTED;
}

static int _aspeed_ecdsa_verify(struct aspeed_ecc_ctx *ctx, const u64 *hash,
				const u64 *r, const u64 *s)
{
	int nbytes = ctx->curve->g.ndigits << ECC_DIGITS_TO_BYTES_SHIFT;
	const struct ecc_curve *curve = ctx->curve;
	void __iomem *base = ctx->ecdsa_dev->regs;
	unsigned int ndigits = curve->g.ndigits;
	u8 *data, *buf;

	/* 0 < r < n  and 0 < s < n */
	if (vli_is_zero(r, ndigits) || vli_cmp(r, curve->n, ndigits) >= 0 ||
	    vli_is_zero(s, ndigits) || vli_cmp(s, curve->n, ndigits) >= 0)
		return -EBADMSG;

	/* hash is given */
	AST_DBG(ctx->ecdsa_dev, "hash : %016llx %016llx ... %016llx\n",
		hash[ndigits - 1], hash[ndigits - 2], hash[0]);

	data = vmalloc(nbytes);
	if (!data)
		return -ENOMEM;

	AST_DBG(ctx->ecdsa_dev, "Dump r:\n");
	buf = (u8 *)r;
	hexdump(buf, nbytes);

	buff_reverse(data, (u8 *)r, nbytes);
	memcpy_toio(base + ASPEED_ECC_SIGN_R_REG, data, nbytes);

	AST_DBG(ctx->ecdsa_dev, "Dump s:\n");
	buf = (u8 *)s;
	hexdump(buf, nbytes);

	buff_reverse(data, (u8 *)s, nbytes);
	memcpy_toio(base + ASPEED_ECC_SIGN_S_REG, data, nbytes);

	AST_DBG(ctx->ecdsa_dev, "Dump m:\n");
	buf = (u8 *)hash;
	hexdump(buf, nbytes);

	buff_reverse(data, (u8 *)hash, nbytes);
	memcpy_toio(base + ASPEED_ECC_MESSAGE_REG, data, nbytes);

	vfree(data);

	return aspeed_ecdsa_trigger(ctx->ecdsa_dev);
}

/*
 * Verify an ECDSA signature.
 */
static int aspeed_ecdsa_verify(struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct aspeed_ecc_ctx *ctx = akcipher_tfm_ctx(tfm);
	size_t keylen = ctx->curve->g.ndigits * sizeof(u64);
	struct ecdsa_signature_ctx sig_ctx = {
		.curve = ctx->curve,
	};
	u8 rawhash[ECC_MAX_BYTES];
	u64 hash[ECC_MAX_DIGITS];
	unsigned char *buffer;
	ssize_t diff;
	int ret;

	if (unlikely(!ctx->pub_key_set))
		return -EINVAL;

	if (aspeed_ecdsa_need_fallback(ctx, req->dst_len)) {
		AST_DBG(ctx->ecdsa_dev, "SW fallback\n");

		akcipher_request_set_tfm(req, ctx->fallback_tfm);
		ret = crypto_akcipher_verify(req);
		akcipher_request_set_tfm(req, tfm);

		AST_DBG(ctx->ecdsa_dev, "SW verify...ret:0x%x\n", ret);

		return ret;
	}

	buffer = kmalloc(req->src_len + req->dst_len, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	/* Input src: signature + digest */
	sg_pcopy_to_buffer(req->src, sg_nents_for_len(req->src, req->src_len + req->dst_len),
			   buffer, req->src_len + req->dst_len, 0);

	ret = asn1_ber_decoder(&ecdsasignature_decoder, &sig_ctx,
			       buffer, req->src_len);
	if (ret < 0)
		goto error;

	/* if the hash is shorter then we will add leading zeros to fit to ndigits */
	diff = keylen - req->dst_len;
	if (diff >= 0) {
		if (diff)
			memset(rawhash, 0, diff);
		memcpy(&rawhash[diff], buffer + req->src_len, req->dst_len);
	} else if (diff < 0) {
		/* given hash is longer, we take the left-most bytes */
		memcpy(&rawhash, buffer + req->src_len, keylen);
	}

	ecc_swap_digits((u64 *)rawhash, hash, ctx->curve->g.ndigits);

	ret = _aspeed_ecdsa_verify(ctx, hash, sig_ctx.r, sig_ctx.s);

error:
	kfree(buffer);

	return ret;
}

static int aspeed_ecdsa_ecc_ctx_init(struct aspeed_ecc_ctx *ctx, unsigned int curve_id)
{
	void __iomem *base = ctx->ecdsa_dev->regs;
	u8 *data, *buf;
	u32 ctrl;
	int nbytes;

	ctx->curve_id = curve_id;
	ctx->curve = ecc_get_curve(curve_id);
	if (!ctx->curve)
		return -EINVAL;

	nbytes = ctx->curve->g.ndigits << ECC_DIGITS_TO_BYTES_SHIFT;

	switch (curve_id) {
	case ECC_CURVE_NIST_P256:
		AST_DBG(ctx->ecdsa_dev, "curve ECC_CURVE_NIST_P256\n");
		ctrl = ECDSA_256_EN;
		break;
	case ECC_CURVE_NIST_P384:
		AST_DBG(ctx->ecdsa_dev, "curve ECC_CURVE_NIST_P384\n");
		ctrl = ECDSA_384_EN;
		break;
	}

	ast_write(ctx->ecdsa_dev, ECC_EN | ctrl, ASPEED_ECC_CTRL_REG);

	/* Initial Curve: ecc point/p/a/n */
	data = vmalloc(nbytes);
	if (!data)
		return -ENOMEM;

	AST_DBG(ctx->ecdsa_dev, "Dump Gx:\n");
	buf = (u8 *)ctx->curve->g.x;
	hexdump(buf, nbytes);

	buff_reverse(data, (u8 *)ctx->curve->g.x, nbytes);
	memcpy_toio(base + ASPEED_ECC_PAR_GX_REG, data, nbytes);

	AST_DBG(ctx->ecdsa_dev, "Dump Gy:\n");
	buf = (u8 *)ctx->curve->g.y;
	hexdump(buf, nbytes);

	buff_reverse(data, (u8 *)ctx->curve->g.y, nbytes);
	memcpy_toio(base + ASPEED_ECC_PAR_GY_REG, data, nbytes);

	AST_DBG(ctx->ecdsa_dev, "Dump P:\n");
	buf = (u8 *)ctx->curve->p;
	hexdump(buf, nbytes);

	buff_reverse(data, (u8 *)ctx->curve->p, nbytes);
	memcpy_toio(base + ASPEED_ECC_PAR_P_REG, data, nbytes);

	AST_DBG(ctx->ecdsa_dev, "Dump A:\n");
	buf = (u8 *)ctx->curve->a;
	hexdump(buf, nbytes);

	buff_reverse(data, (u8 *)ctx->curve->a, nbytes);
	memcpy_toio(base + ASPEED_ECC_PAR_A_REG, data, nbytes);

	AST_DBG(ctx->ecdsa_dev, "Dump N:\n");
	buf = (u8 *)ctx->curve->n;
	hexdump(buf, nbytes);

	buff_reverse(data, (u8 *)ctx->curve->n, nbytes);
	memcpy_toio(base + ASPEED_ECC_PAR_N_REG, data, nbytes);

	vfree(data);
	return 0;
}

static void aspeed_ecdsa_ecc_ctx_deinit(struct aspeed_ecc_ctx *ctx)
{
	ctx->pub_key_set = false;
}

static void aspeed_ecdsa_ecc_ctx_reset(struct aspeed_ecc_ctx *ctx)
{
	ctx->pub_key = ECC_POINT_INIT(ctx->x, ctx->y,
				      ctx->curve->g.ndigits);
}

/*
 * Set the public key given the raw uncompressed key data from an X509
 * certificate. The key data contain the concatenated X and Y coordinates of
 * the public key.
 */
static int aspeed_ecdsa_set_pub_key(struct crypto_akcipher *tfm, const void *key,
				    unsigned int keylen)
{
	struct aspeed_ecc_ctx *ctx = akcipher_tfm_ctx(tfm);
	int nbytes = ctx->curve->g.ndigits << ECC_DIGITS_TO_BYTES_SHIFT;
	void __iomem *base = ctx->ecdsa_dev->regs;
	const unsigned char *d = key;
	const u64 *digits = (const u64 *)&d[1];
	unsigned int ndigits;
	u8 *data, *buf;
	int ret;

	ret = crypto_akcipher_set_pub_key(ctx->fallback_tfm, key, keylen);
	if (ret)
		return ret;

	aspeed_ecdsa_ecc_ctx_reset(ctx);

	if (keylen < 1 || (((keylen - 1) >> 1) % sizeof(u64)) != 0)
		return -EINVAL;
	/* we only accept uncompressed format indicated by '4' */
	if (d[0] != 4)
		return -EINVAL;

	keylen--;
	ndigits = (keylen >> 1) / sizeof(u64);
	if (ndigits != ctx->curve->g.ndigits)
		return -EINVAL;

	ecc_swap_digits(digits, ctx->pub_key.x, ndigits);
	ecc_swap_digits(&digits[ndigits], ctx->pub_key.y, ndigits);
	ret = ecc_is_pubkey_valid_full(ctx->curve, &ctx->pub_key);

	/* Set public key: Qx/Qy */
	data = vmalloc(nbytes);
	if (!data)
		return -ENOMEM;

	AST_DBG(ctx->ecdsa_dev, "Dump Qx:\n");
	buf = (u8 *)ctx->pub_key.x;
	hexdump(buf, nbytes);

	buff_reverse(data, (u8 *)ctx->pub_key.x, nbytes);
	memcpy_toio(base + ASPEED_ECC_PAR_QX_REG, data, nbytes);

	AST_DBG(ctx->ecdsa_dev, "Dump Qy:\n");
	buf = (u8 *)ctx->pub_key.y;
	hexdump(buf, nbytes);
	buff_reverse(data, (u8 *)ctx->pub_key.y, nbytes);
	memcpy_toio(base + ASPEED_ECC_PAR_QY_REG, data, nbytes);

	ctx->pub_key_set = ret == 0;

	vfree(data);

	return ret;
}

static void aspeed_ecdsa_exit_tfm(struct crypto_akcipher *tfm)
{
	struct aspeed_ecc_ctx *ctx = akcipher_tfm_ctx(tfm);

	aspeed_ecdsa_ecc_ctx_deinit(ctx);

	crypto_free_akcipher(ctx->fallback_tfm);
}

static unsigned int aspeed_ecdsa_max_size(struct crypto_akcipher *tfm)
{
	struct aspeed_ecc_ctx *ctx = akcipher_tfm_ctx(tfm);

	return ctx->pub_key.ndigits << ECC_DIGITS_TO_BYTES_SHIFT;
}

static int aspeed_ecdsa_nist_p384_init_tfm(struct crypto_akcipher *tfm)
{
	struct aspeed_ecc_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct akcipher_alg *alg = crypto_akcipher_alg(tfm);
	const char *name = crypto_tfm_alg_name(&tfm->base);
	struct aspeed_ecdsa_alg *ecdsa_alg;

	ecdsa_alg = container_of(alg, struct aspeed_ecdsa_alg, akcipher);

	ctx->ecdsa_dev = ecdsa_alg->ecdsa_dev;

	AST_DBG(ctx->ecdsa_dev, "\n");
	ctx->fallback_tfm = crypto_alloc_akcipher(name, 0, CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(ctx->fallback_tfm)) {
		dev_err(ctx->ecdsa_dev->dev, "ERROR: Cannot allocate fallback for %s %ld\n",
			name, PTR_ERR(ctx->fallback_tfm));
		return PTR_ERR(ctx->fallback_tfm);
	}

	return aspeed_ecdsa_ecc_ctx_init(ctx, ECC_CURVE_NIST_P384);
}

static int aspeed_ecdsa_nist_p256_init_tfm(struct crypto_akcipher *tfm)
{
	struct aspeed_ecc_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct akcipher_alg *alg = crypto_akcipher_alg(tfm);
	const char *name = crypto_tfm_alg_name(&tfm->base);
	struct aspeed_ecdsa_alg *ecdsa_alg;

	ecdsa_alg = container_of(alg, struct aspeed_ecdsa_alg, akcipher);

	ctx->ecdsa_dev = ecdsa_alg->ecdsa_dev;

	AST_DBG(ctx->ecdsa_dev, "\n");
	ctx->fallback_tfm = crypto_alloc_akcipher(name, 0, CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(ctx->fallback_tfm)) {
		dev_err(ctx->ecdsa_dev->dev, "ERROR: Cannot allocate fallback for %s %ld\n",
			name, PTR_ERR(ctx->fallback_tfm));
		return PTR_ERR(ctx->fallback_tfm);
	}

	return aspeed_ecdsa_ecc_ctx_init(ctx, ECC_CURVE_NIST_P256);
}

static struct aspeed_ecdsa_alg aspeed_ecdsa_nist_p256 = {
	.akcipher = {
		.verify = aspeed_ecdsa_verify,
		.set_pub_key = aspeed_ecdsa_set_pub_key,
		.max_size = aspeed_ecdsa_max_size,
		.init = aspeed_ecdsa_nist_p256_init_tfm,
		.exit = aspeed_ecdsa_exit_tfm,
		.base = {
			.cra_name = "ecdsa-nist-p256",
			.cra_driver_name = "aspeed-ecdsa-nist-p256",
			.cra_priority = 300,
			.cra_module = THIS_MODULE,
			.cra_ctxsize = sizeof(struct aspeed_ecc_ctx),
			.cra_flags = CRYPTO_ALG_TYPE_AKCIPHER |
				     CRYPTO_ALG_NEED_FALLBACK,
		},
	},
};

static struct aspeed_ecdsa_alg aspeed_ecdsa_nist_p384 = {
	.akcipher = {
		.verify = aspeed_ecdsa_verify,
		.set_pub_key = aspeed_ecdsa_set_pub_key,
		.max_size = aspeed_ecdsa_max_size,
		.init = aspeed_ecdsa_nist_p384_init_tfm,
		.exit = aspeed_ecdsa_exit_tfm,
		.base = {
			.cra_name = "ecdsa-nist-p384",
			.cra_driver_name = "aspeed-ecdsa-nist-p384",
			.cra_priority = 300,
			.cra_module = THIS_MODULE,
			.cra_ctxsize = sizeof(struct aspeed_ecc_ctx),
			.cra_flags = CRYPTO_ALG_TYPE_AKCIPHER |
				     CRYPTO_ALG_NEED_FALLBACK,
		},
	},
};

static int aspeed_ecdsa_register(struct aspeed_ecdsa_dev *ecdsa_dev)
{
	int rc;

	aspeed_ecdsa_nist_p256.ecdsa_dev = ecdsa_dev;
	rc = crypto_register_akcipher(&aspeed_ecdsa_nist_p256.akcipher);
	if (rc)
		goto nist_p256_error;

	aspeed_ecdsa_nist_p384.ecdsa_dev = ecdsa_dev;
	rc = crypto_register_akcipher(&aspeed_ecdsa_nist_p384.akcipher);
	if (rc)
		goto nist_p384_error;

	return 0;

nist_p384_error:
	crypto_unregister_akcipher(&aspeed_ecdsa_nist_p256.akcipher);

nist_p256_error:
	return rc;
}

static void aspeed_ecdsa_unregister(struct aspeed_ecdsa_dev *ecdsa_dev)
{
	crypto_unregister_akcipher(&aspeed_ecdsa_nist_p256.akcipher);
	crypto_unregister_akcipher(&aspeed_ecdsa_nist_p384.akcipher);
}

#ifdef ASPEED_ECDSA_IRQ_MODE
/* ecdsa interrupt service routine. */
static irqreturn_t aspeed_ecdsa_irq(int irq, void *dev)
{
	struct aspeed_ecdsa_dev *ecdsa_dev = (struct aspeed_ecdsa_dev *)dev;
	u32 sts;

	sts = ast_read(ecdsa_dev, ASPEED_ECC_INT_STS);
	ast_write(ecdsa_dev, sts, ASPEED_ECC_INT_STS);

	AST_DBG(ecdsa_dev, "irq sts:0x%x\n", sts);

	return IRQ_HANDLED;
}
#endif
static const struct of_device_id aspeed_ecdsa_of_matches[] = {
	{ .compatible = "aspeed,ast2700-ecdsa", },
	{},
};

static int aspeed_ecdsa_probe(struct platform_device *pdev)
{
	struct aspeed_ecdsa_dev *ecdsa_dev;
	struct device *dev = &pdev->dev;
	int rc;

	ecdsa_dev = devm_kzalloc(dev, sizeof(struct aspeed_ecdsa_dev),
				 GFP_KERNEL);
	if (!ecdsa_dev)
		return -ENOMEM;

	ecdsa_dev->dev = dev;

	platform_set_drvdata(pdev, ecdsa_dev);

	ecdsa_dev->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ecdsa_dev->regs))
		return PTR_ERR(ecdsa_dev->regs);

#ifdef ASPEED_ECDSA_IRQ_MODE
	/* Get irq number and register it */
	ecdsa_dev->irq = platform_get_irq(pdev, 0);
	if (ecdsa_dev->irq < 0)
		return -ENXIO;

	rc = devm_request_irq(dev, ecdsa_dev->irq, aspeed_ecdsa_irq, 0,
			      dev_name(dev), ecdsa_dev);
	if (rc) {
		dev_err(dev, "Failed to request irq.\n");
		return rc;
	}

	/* Enable interrupt */
	ast_write(ecdsa_dev, 0x1, ASPEED_ECC_INT_EN);
#endif

	rc = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));
	if (rc) {
		dev_warn(&pdev->dev, "No suitable DMA available\n");
		return rc;
	}

	ecdsa_dev->clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(ecdsa_dev->clk)) {
		dev_err(dev, "Failed to get ecdsa clk\n");
		return PTR_ERR(ecdsa_dev->clk);
	}

	ecdsa_dev->rst = devm_reset_control_get_by_index(dev, 0);
	if (IS_ERR(ecdsa_dev->rst)) {
		dev_err(dev, "Failed to get ecdsa reset\n");
		return PTR_ERR(ecdsa_dev->rst);
	}

	rc = reset_control_deassert(ecdsa_dev->rst);
	if (rc) {
		dev_err(dev, "Deassert ecdsa reset failed\n");
		return rc;
	}

	rc = aspeed_ecdsa_register(ecdsa_dev);
	if (rc) {
		dev_err(dev, "ECDSA algo register failed\n");
		return rc;
	}

	dev_info(dev, "Aspeed ECDSA Hardware Accelerator successfully registered\n");

	return 0;
}

static int aspeed_ecdsa_remove(struct platform_device *pdev)
{
	struct aspeed_ecdsa_dev *ecdsa_dev = platform_get_drvdata(pdev);

	aspeed_ecdsa_unregister(ecdsa_dev);

	return 0;
}

MODULE_DEVICE_TABLE(of, aspeed_ecdsa_of_matches);

static struct platform_driver aspeed_ecdsa_driver = {
	.probe		= aspeed_ecdsa_probe,
	.remove		= aspeed_ecdsa_remove,
	.driver		= {
		.name   = KBUILD_MODNAME,
		.of_match_table = aspeed_ecdsa_of_matches,
	},
};

module_platform_driver(aspeed_ecdsa_driver);
MODULE_AUTHOR("Neal Liu <neal_liu@aspeedtech.com>");
MODULE_DESCRIPTION("ASPEED ECDSA algorithm driver acceleration");
MODULE_LICENSE("GPL");
