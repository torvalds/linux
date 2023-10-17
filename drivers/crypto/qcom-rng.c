// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2017-18 Linaro Limited
//
// Based on msm-rng.c and downstream driver

#include <crypto/internal/rng.h>
#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/crypto.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

/* Device specific register offsets */
#define PRNG_DATA_OUT		0x0000
#define PRNG_STATUS		0x0004
#define PRNG_LFSR_CFG		0x0100
#define PRNG_CONFIG		0x0104

/* Device specific register masks and config values */
#define PRNG_LFSR_CFG_MASK	0x0000ffff
#define PRNG_LFSR_CFG_CLOCKS	0x0000dddd
#define PRNG_CONFIG_HW_ENABLE	BIT(1)
#define PRNG_STATUS_DATA_AVAIL	BIT(0)

#define WORD_SZ			4

struct qcom_rng {
	struct mutex lock;
	void __iomem *base;
	struct clk *clk;
	unsigned int skip_init;
};

struct qcom_rng_ctx {
	struct qcom_rng *rng;
};

static struct qcom_rng *qcom_rng_dev;

static int qcom_rng_read(struct qcom_rng *rng, u8 *data, unsigned int max)
{
	unsigned int currsize = 0;
	u32 val;
	int ret;

	/* read random data from hardware */
	do {
		ret = readl_poll_timeout(rng->base + PRNG_STATUS, val,
					 val & PRNG_STATUS_DATA_AVAIL,
					 200, 10000);
		if (ret)
			return ret;

		val = readl_relaxed(rng->base + PRNG_DATA_OUT);
		if (!val)
			return -EINVAL;

		if ((max - currsize) >= WORD_SZ) {
			memcpy(data, &val, WORD_SZ);
			data += WORD_SZ;
			currsize += WORD_SZ;
		} else {
			/* copy only remaining bytes */
			memcpy(data, &val, max - currsize);
			break;
		}
	} while (currsize < max);

	return 0;
}

static int qcom_rng_generate(struct crypto_rng *tfm,
			     const u8 *src, unsigned int slen,
			     u8 *dstn, unsigned int dlen)
{
	struct qcom_rng_ctx *ctx = crypto_rng_ctx(tfm);
	struct qcom_rng *rng = ctx->rng;
	int ret;

	ret = clk_prepare_enable(rng->clk);
	if (ret)
		return ret;

	mutex_lock(&rng->lock);

	ret = qcom_rng_read(rng, dstn, dlen);

	mutex_unlock(&rng->lock);
	clk_disable_unprepare(rng->clk);

	return ret;
}

static int qcom_rng_seed(struct crypto_rng *tfm, const u8 *seed,
			 unsigned int slen)
{
	return 0;
}

static int qcom_rng_enable(struct qcom_rng *rng)
{
	u32 val;
	int ret;

	ret = clk_prepare_enable(rng->clk);
	if (ret)
		return ret;

	/* Enable PRNG only if it is not already enabled */
	val = readl_relaxed(rng->base + PRNG_CONFIG);
	if (val & PRNG_CONFIG_HW_ENABLE)
		goto already_enabled;

	val = readl_relaxed(rng->base + PRNG_LFSR_CFG);
	val &= ~PRNG_LFSR_CFG_MASK;
	val |= PRNG_LFSR_CFG_CLOCKS;
	writel(val, rng->base + PRNG_LFSR_CFG);

	val = readl_relaxed(rng->base + PRNG_CONFIG);
	val |= PRNG_CONFIG_HW_ENABLE;
	writel(val, rng->base + PRNG_CONFIG);

already_enabled:
	clk_disable_unprepare(rng->clk);

	return 0;
}

static int qcom_rng_init(struct crypto_tfm *tfm)
{
	struct qcom_rng_ctx *ctx = crypto_tfm_ctx(tfm);

	ctx->rng = qcom_rng_dev;

	if (!ctx->rng->skip_init)
		return qcom_rng_enable(ctx->rng);

	return 0;
}

static struct rng_alg qcom_rng_alg = {
	.generate	= qcom_rng_generate,
	.seed		= qcom_rng_seed,
	.seedsize	= 0,
	.base		= {
		.cra_name		= "stdrng",
		.cra_driver_name	= "qcom-rng",
		.cra_flags		= CRYPTO_ALG_TYPE_RNG,
		.cra_priority		= 300,
		.cra_ctxsize		= sizeof(struct qcom_rng_ctx),
		.cra_module		= THIS_MODULE,
		.cra_init		= qcom_rng_init,
	}
};

static int qcom_rng_probe(struct platform_device *pdev)
{
	struct qcom_rng *rng;
	int ret;

	rng = devm_kzalloc(&pdev->dev, sizeof(*rng), GFP_KERNEL);
	if (!rng)
		return -ENOMEM;

	platform_set_drvdata(pdev, rng);
	mutex_init(&rng->lock);

	rng->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(rng->base))
		return PTR_ERR(rng->base);

	rng->clk = devm_clk_get_optional(&pdev->dev, "core");
	if (IS_ERR(rng->clk))
		return PTR_ERR(rng->clk);

	rng->skip_init = (unsigned long)device_get_match_data(&pdev->dev);

	qcom_rng_dev = rng;
	ret = crypto_register_rng(&qcom_rng_alg);
	if (ret) {
		dev_err(&pdev->dev, "Register crypto rng failed: %d\n", ret);
		qcom_rng_dev = NULL;
	}

	return ret;
}

static int qcom_rng_remove(struct platform_device *pdev)
{
	crypto_unregister_rng(&qcom_rng_alg);

	qcom_rng_dev = NULL;

	return 0;
}

static const struct acpi_device_id __maybe_unused qcom_rng_acpi_match[] = {
	{ .id = "QCOM8160", .driver_data = 1 },
	{}
};
MODULE_DEVICE_TABLE(acpi, qcom_rng_acpi_match);

static const struct of_device_id __maybe_unused qcom_rng_of_match[] = {
	{ .compatible = "qcom,prng", .data = (void *)0},
	{ .compatible = "qcom,prng-ee", .data = (void *)1},
	{}
};
MODULE_DEVICE_TABLE(of, qcom_rng_of_match);

static struct platform_driver qcom_rng_driver = {
	.probe = qcom_rng_probe,
	.remove =  qcom_rng_remove,
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = of_match_ptr(qcom_rng_of_match),
		.acpi_match_table = ACPI_PTR(qcom_rng_acpi_match),
	}
};
module_platform_driver(qcom_rng_driver);

MODULE_ALIAS("platform:" KBUILD_MODNAME);
MODULE_DESCRIPTION("Qualcomm random number generator driver");
MODULE_LICENSE("GPL v2");
