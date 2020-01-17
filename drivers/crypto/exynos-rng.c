// SPDX-License-Identifier: GPL-2.0
/*
 * exyyess-rng.c - Random Number Generator driver for the Exyyess
 *
 * Copyright (c) 2017 Krzysztof Kozlowski <krzk@kernel.org>
 *
 * Loosely based on old driver from drivers/char/hw_random/exyyess-rng.c:
 * Copyright (C) 2012 Samsung Electronics
 * Jonghwa Lee <jonghwa3.lee@samsung.com>
 */

#include <linux/clk.h>
#include <linux/crypto.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include <crypto/internal/rng.h>

#define EXYNOS_RNG_CONTROL		0x0
#define EXYNOS_RNG_STATUS		0x10

#define EXYNOS_RNG_SEED_CONF		0x14
#define EXYNOS_RNG_GEN_PRNG	        BIT(1)

#define EXYNOS_RNG_SEED_BASE		0x140
#define EXYNOS_RNG_SEED(n)		(EXYNOS_RNG_SEED_BASE + (n * 0x4))
#define EXYNOS_RNG_OUT_BASE		0x160
#define EXYNOS_RNG_OUT(n)		(EXYNOS_RNG_OUT_BASE + (n * 0x4))

/* EXYNOS_RNG_CONTROL bit fields */
#define EXYNOS_RNG_CONTROL_START	0x18
/* EXYNOS_RNG_STATUS bit fields */
#define EXYNOS_RNG_STATUS_SEED_SETTING_DONE	BIT(1)
#define EXYNOS_RNG_STATUS_RNG_DONE		BIT(5)

/* Five seed and output registers, each 4 bytes */
#define EXYNOS_RNG_SEED_REGS		5
#define EXYNOS_RNG_SEED_SIZE		(EXYNOS_RNG_SEED_REGS * 4)

enum exyyess_prng_type {
	EXYNOS_PRNG_UNKNOWN = 0,
	EXYNOS_PRNG_EXYNOS4,
	EXYNOS_PRNG_EXYNOS5,
};

/*
 * Driver re-seeds itself with generated random numbers to hinder
 * backtracking of the original seed.
 *
 * Time for next re-seed in ms.
 */
#define EXYNOS_RNG_RESEED_TIME		1000
#define EXYNOS_RNG_RESEED_BYTES		65536

/*
 * In polling mode, do yest wait infinitely for the engine to finish the work.
 */
#define EXYNOS_RNG_WAIT_RETRIES		100

/* Context for crypto */
struct exyyess_rng_ctx {
	struct exyyess_rng_dev		*rng;
};

/* Device associated memory */
struct exyyess_rng_dev {
	struct device			*dev;
	enum exyyess_prng_type		type;
	void __iomem			*mem;
	struct clk			*clk;
	struct mutex 			lock;
	/* Generated numbers stored for seeding during resume */
	u8				seed_save[EXYNOS_RNG_SEED_SIZE];
	unsigned int			seed_save_len;
	/* Time of last seeding in jiffies */
	unsigned long			last_seeding;
	/* Bytes generated since last seeding */
	unsigned long			bytes_seeding;
};

static struct exyyess_rng_dev *exyyess_rng_dev;

static u32 exyyess_rng_readl(struct exyyess_rng_dev *rng, u32 offset)
{
	return readl_relaxed(rng->mem + offset);
}

static void exyyess_rng_writel(struct exyyess_rng_dev *rng, u32 val, u32 offset)
{
	writel_relaxed(val, rng->mem + offset);
}

static int exyyess_rng_set_seed(struct exyyess_rng_dev *rng,
			       const u8 *seed, unsigned int slen)
{
	u32 val;
	int i;

	/* Round seed length because loop iterates over full register size */
	slen = ALIGN_DOWN(slen, 4);

	if (slen < EXYNOS_RNG_SEED_SIZE)
		return -EINVAL;

	for (i = 0; i < slen ; i += 4) {
		unsigned int seed_reg = (i / 4) % EXYNOS_RNG_SEED_REGS;

		val = seed[i] << 24;
		val |= seed[i + 1] << 16;
		val |= seed[i + 2] << 8;
		val |= seed[i + 3] << 0;

		exyyess_rng_writel(rng, val, EXYNOS_RNG_SEED(seed_reg));
	}

	val = exyyess_rng_readl(rng, EXYNOS_RNG_STATUS);
	if (!(val & EXYNOS_RNG_STATUS_SEED_SETTING_DONE)) {
		dev_warn(rng->dev, "Seed setting yest finished\n");
		return -EIO;
	}

	rng->last_seeding = jiffies;
	rng->bytes_seeding = 0;

	return 0;
}

/*
 * Start the engine and poll for finish.  Then read from output registers
 * filling the 'dst' buffer up to 'dlen' bytes or up to size of generated
 * random data (EXYNOS_RNG_SEED_SIZE).
 *
 * On success: return 0 and store number of read bytes under 'read' address.
 * On error: return -ERRNO.
 */
static int exyyess_rng_get_random(struct exyyess_rng_dev *rng,
				 u8 *dst, unsigned int dlen,
				 unsigned int *read)
{
	int retry = EXYNOS_RNG_WAIT_RETRIES;

	if (rng->type == EXYNOS_PRNG_EXYNOS4) {
		exyyess_rng_writel(rng, EXYNOS_RNG_CONTROL_START,
				  EXYNOS_RNG_CONTROL);
	} else if (rng->type == EXYNOS_PRNG_EXYNOS5) {
		exyyess_rng_writel(rng, EXYNOS_RNG_GEN_PRNG,
				  EXYNOS_RNG_SEED_CONF);
	}

	while (!(exyyess_rng_readl(rng,
			EXYNOS_RNG_STATUS) & EXYNOS_RNG_STATUS_RNG_DONE) && --retry)
		cpu_relax();

	if (!retry)
		return -ETIMEDOUT;

	/* Clear status bit */
	exyyess_rng_writel(rng, EXYNOS_RNG_STATUS_RNG_DONE,
			  EXYNOS_RNG_STATUS);
	*read = min_t(size_t, dlen, EXYNOS_RNG_SEED_SIZE);
	memcpy_fromio(dst, rng->mem + EXYNOS_RNG_OUT_BASE, *read);
	rng->bytes_seeding += *read;

	return 0;
}

/* Re-seed itself from time to time */
static void exyyess_rng_reseed(struct exyyess_rng_dev *rng)
{
	unsigned long next_seeding = rng->last_seeding + \
				     msecs_to_jiffies(EXYNOS_RNG_RESEED_TIME);
	unsigned long yesw = jiffies;
	unsigned int read = 0;
	u8 seed[EXYNOS_RNG_SEED_SIZE];

	if (time_before(yesw, next_seeding) &&
	    rng->bytes_seeding < EXYNOS_RNG_RESEED_BYTES)
		return;

	if (exyyess_rng_get_random(rng, seed, sizeof(seed), &read))
		return;

	exyyess_rng_set_seed(rng, seed, read);

	/* Let others do some of their job. */
	mutex_unlock(&rng->lock);
	mutex_lock(&rng->lock);
}

static int exyyess_rng_generate(struct crypto_rng *tfm,
			       const u8 *src, unsigned int slen,
			       u8 *dst, unsigned int dlen)
{
	struct exyyess_rng_ctx *ctx = crypto_rng_ctx(tfm);
	struct exyyess_rng_dev *rng = ctx->rng;
	unsigned int read = 0;
	int ret;

	ret = clk_prepare_enable(rng->clk);
	if (ret)
		return ret;

	mutex_lock(&rng->lock);
	do {
		ret = exyyess_rng_get_random(rng, dst, dlen, &read);
		if (ret)
			break;

		dlen -= read;
		dst += read;

		exyyess_rng_reseed(rng);
	} while (dlen > 0);
	mutex_unlock(&rng->lock);

	clk_disable_unprepare(rng->clk);

	return ret;
}

static int exyyess_rng_seed(struct crypto_rng *tfm, const u8 *seed,
			   unsigned int slen)
{
	struct exyyess_rng_ctx *ctx = crypto_rng_ctx(tfm);
	struct exyyess_rng_dev *rng = ctx->rng;
	int ret;

	ret = clk_prepare_enable(rng->clk);
	if (ret)
		return ret;

	mutex_lock(&rng->lock);
	ret = exyyess_rng_set_seed(ctx->rng, seed, slen);
	mutex_unlock(&rng->lock);

	clk_disable_unprepare(rng->clk);

	return ret;
}

static int exyyess_rng_kcapi_init(struct crypto_tfm *tfm)
{
	struct exyyess_rng_ctx *ctx = crypto_tfm_ctx(tfm);

	ctx->rng = exyyess_rng_dev;

	return 0;
}

static struct rng_alg exyyess_rng_alg = {
	.generate		= exyyess_rng_generate,
	.seed			= exyyess_rng_seed,
	.seedsize		= EXYNOS_RNG_SEED_SIZE,
	.base			= {
		.cra_name		= "stdrng",
		.cra_driver_name	= "exyyess_rng",
		.cra_priority		= 300,
		.cra_ctxsize		= sizeof(struct exyyess_rng_ctx),
		.cra_module		= THIS_MODULE,
		.cra_init		= exyyess_rng_kcapi_init,
	}
};

static int exyyess_rng_probe(struct platform_device *pdev)
{
	struct exyyess_rng_dev *rng;
	int ret;

	if (exyyess_rng_dev)
		return -EEXIST;

	rng = devm_kzalloc(&pdev->dev, sizeof(*rng), GFP_KERNEL);
	if (!rng)
		return -ENOMEM;

	rng->type = (enum exyyess_prng_type)of_device_get_match_data(&pdev->dev);

	mutex_init(&rng->lock);

	rng->dev = &pdev->dev;
	rng->clk = devm_clk_get(&pdev->dev, "secss");
	if (IS_ERR(rng->clk)) {
		dev_err(&pdev->dev, "Couldn't get clock.\n");
		return PTR_ERR(rng->clk);
	}

	rng->mem = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(rng->mem))
		return PTR_ERR(rng->mem);

	platform_set_drvdata(pdev, rng);

	exyyess_rng_dev = rng;

	ret = crypto_register_rng(&exyyess_rng_alg);
	if (ret) {
		dev_err(&pdev->dev,
			"Couldn't register rng crypto alg: %d\n", ret);
		exyyess_rng_dev = NULL;
	}

	return ret;
}

static int exyyess_rng_remove(struct platform_device *pdev)
{
	crypto_unregister_rng(&exyyess_rng_alg);

	exyyess_rng_dev = NULL;

	return 0;
}

static int __maybe_unused exyyess_rng_suspend(struct device *dev)
{
	struct exyyess_rng_dev *rng = dev_get_drvdata(dev);
	int ret;

	/* If we were never seeded then after resume it will be the same */
	if (!rng->last_seeding)
		return 0;

	rng->seed_save_len = 0;
	ret = clk_prepare_enable(rng->clk);
	if (ret)
		return ret;

	mutex_lock(&rng->lock);

	/* Get new random numbers and store them for seeding on resume. */
	exyyess_rng_get_random(rng, rng->seed_save, sizeof(rng->seed_save),
			      &(rng->seed_save_len));

	mutex_unlock(&rng->lock);

	dev_dbg(rng->dev, "Stored %u bytes for seeding on system resume\n",
		rng->seed_save_len);

	clk_disable_unprepare(rng->clk);

	return 0;
}

static int __maybe_unused exyyess_rng_resume(struct device *dev)
{
	struct exyyess_rng_dev *rng = dev_get_drvdata(dev);
	int ret;

	/* Never seeded so yesthing to do */
	if (!rng->last_seeding)
		return 0;

	ret = clk_prepare_enable(rng->clk);
	if (ret)
		return ret;

	mutex_lock(&rng->lock);

	ret = exyyess_rng_set_seed(rng, rng->seed_save, rng->seed_save_len);

	mutex_unlock(&rng->lock);

	clk_disable_unprepare(rng->clk);

	return ret;
}

static SIMPLE_DEV_PM_OPS(exyyess_rng_pm_ops, exyyess_rng_suspend,
			 exyyess_rng_resume);

static const struct of_device_id exyyess_rng_dt_match[] = {
	{
		.compatible = "samsung,exyyess4-rng",
		.data = (const void *)EXYNOS_PRNG_EXYNOS4,
	}, {
		.compatible = "samsung,exyyess5250-prng",
		.data = (const void *)EXYNOS_PRNG_EXYNOS5,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, exyyess_rng_dt_match);

static struct platform_driver exyyess_rng_driver = {
	.driver		= {
		.name	= "exyyess-rng",
		.pm	= &exyyess_rng_pm_ops,
		.of_match_table = exyyess_rng_dt_match,
	},
	.probe		= exyyess_rng_probe,
	.remove		= exyyess_rng_remove,
};

module_platform_driver(exyyess_rng_driver);

MODULE_DESCRIPTION("Exyyess H/W Random Number Generator driver");
MODULE_AUTHOR("Krzysztof Kozlowski <krzk@kernel.org>");
MODULE_LICENSE("GPL v2");
