// SPDX-License-Identifier: GPL-2.0
/*
 * exynos-rng.c - Random Number Generator driver for the Exynos
 *
 * Copyright (c) 2017 Krzysztof Kozlowski <krzk@kernel.org>
 *
 * Loosely based on old driver from drivers/char/hw_random/exynos-rng.c:
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

enum exynos_prng_type {
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
 * In polling mode, do not wait infinitely for the engine to finish the work.
 */
#define EXYNOS_RNG_WAIT_RETRIES		100

/* Context for crypto */
struct exynos_rng_ctx {
	struct exynos_rng_dev		*rng;
};

/* Device associated memory */
struct exynos_rng_dev {
	struct device			*dev;
	enum exynos_prng_type		type;
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

static struct exynos_rng_dev *exynos_rng_dev;

static u32 exynos_rng_readl(struct exynos_rng_dev *rng, u32 offset)
{
	return readl_relaxed(rng->mem + offset);
}

static void exynos_rng_writel(struct exynos_rng_dev *rng, u32 val, u32 offset)
{
	writel_relaxed(val, rng->mem + offset);
}

static int exynos_rng_set_seed(struct exynos_rng_dev *rng,
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

		exynos_rng_writel(rng, val, EXYNOS_RNG_SEED(seed_reg));
	}

	val = exynos_rng_readl(rng, EXYNOS_RNG_STATUS);
	if (!(val & EXYNOS_RNG_STATUS_SEED_SETTING_DONE)) {
		dev_warn(rng->dev, "Seed setting not finished\n");
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
static int exynos_rng_get_random(struct exynos_rng_dev *rng,
				 u8 *dst, unsigned int dlen,
				 unsigned int *read)
{
	int retry = EXYNOS_RNG_WAIT_RETRIES;

	if (rng->type == EXYNOS_PRNG_EXYNOS4) {
		exynos_rng_writel(rng, EXYNOS_RNG_CONTROL_START,
				  EXYNOS_RNG_CONTROL);
	} else if (rng->type == EXYNOS_PRNG_EXYNOS5) {
		exynos_rng_writel(rng, EXYNOS_RNG_GEN_PRNG,
				  EXYNOS_RNG_SEED_CONF);
	}

	while (!(exynos_rng_readl(rng,
			EXYNOS_RNG_STATUS) & EXYNOS_RNG_STATUS_RNG_DONE) && --retry)
		cpu_relax();

	if (!retry)
		return -ETIMEDOUT;

	/* Clear status bit */
	exynos_rng_writel(rng, EXYNOS_RNG_STATUS_RNG_DONE,
			  EXYNOS_RNG_STATUS);
	*read = min_t(size_t, dlen, EXYNOS_RNG_SEED_SIZE);
	memcpy_fromio(dst, rng->mem + EXYNOS_RNG_OUT_BASE, *read);
	rng->bytes_seeding += *read;

	return 0;
}

/* Re-seed itself from time to time */
static void exynos_rng_reseed(struct exynos_rng_dev *rng)
{
	unsigned long next_seeding = rng->last_seeding + \
				     msecs_to_jiffies(EXYNOS_RNG_RESEED_TIME);
	unsigned long now = jiffies;
	unsigned int read = 0;
	u8 seed[EXYNOS_RNG_SEED_SIZE];

	if (time_before(now, next_seeding) &&
	    rng->bytes_seeding < EXYNOS_RNG_RESEED_BYTES)
		return;

	if (exynos_rng_get_random(rng, seed, sizeof(seed), &read))
		return;

	exynos_rng_set_seed(rng, seed, read);

	/* Let others do some of their job. */
	mutex_unlock(&rng->lock);
	mutex_lock(&rng->lock);
}

static int exynos_rng_generate(struct crypto_rng *tfm,
			       const u8 *src, unsigned int slen,
			       u8 *dst, unsigned int dlen)
{
	struct exynos_rng_ctx *ctx = crypto_rng_ctx(tfm);
	struct exynos_rng_dev *rng = ctx->rng;
	unsigned int read = 0;
	int ret;

	ret = clk_prepare_enable(rng->clk);
	if (ret)
		return ret;

	mutex_lock(&rng->lock);
	do {
		ret = exynos_rng_get_random(rng, dst, dlen, &read);
		if (ret)
			break;

		dlen -= read;
		dst += read;

		exynos_rng_reseed(rng);
	} while (dlen > 0);
	mutex_unlock(&rng->lock);

	clk_disable_unprepare(rng->clk);

	return ret;
}

static int exynos_rng_seed(struct crypto_rng *tfm, const u8 *seed,
			   unsigned int slen)
{
	struct exynos_rng_ctx *ctx = crypto_rng_ctx(tfm);
	struct exynos_rng_dev *rng = ctx->rng;
	int ret;

	ret = clk_prepare_enable(rng->clk);
	if (ret)
		return ret;

	mutex_lock(&rng->lock);
	ret = exynos_rng_set_seed(ctx->rng, seed, slen);
	mutex_unlock(&rng->lock);

	clk_disable_unprepare(rng->clk);

	return ret;
}

static int exynos_rng_kcapi_init(struct crypto_tfm *tfm)
{
	struct exynos_rng_ctx *ctx = crypto_tfm_ctx(tfm);

	ctx->rng = exynos_rng_dev;

	return 0;
}

static struct rng_alg exynos_rng_alg = {
	.generate		= exynos_rng_generate,
	.seed			= exynos_rng_seed,
	.seedsize		= EXYNOS_RNG_SEED_SIZE,
	.base			= {
		.cra_name		= "stdrng",
		.cra_driver_name	= "exynos_rng",
		.cra_priority		= 300,
		.cra_ctxsize		= sizeof(struct exynos_rng_ctx),
		.cra_module		= THIS_MODULE,
		.cra_init		= exynos_rng_kcapi_init,
	}
};

static int exynos_rng_probe(struct platform_device *pdev)
{
	struct exynos_rng_dev *rng;
	struct resource *res;
	int ret;

	if (exynos_rng_dev)
		return -EEXIST;

	rng = devm_kzalloc(&pdev->dev, sizeof(*rng), GFP_KERNEL);
	if (!rng)
		return -ENOMEM;

	rng->type = (enum exynos_prng_type)of_device_get_match_data(&pdev->dev);

	mutex_init(&rng->lock);

	rng->dev = &pdev->dev;
	rng->clk = devm_clk_get(&pdev->dev, "secss");
	if (IS_ERR(rng->clk)) {
		dev_err(&pdev->dev, "Couldn't get clock.\n");
		return PTR_ERR(rng->clk);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	rng->mem = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(rng->mem))
		return PTR_ERR(rng->mem);

	platform_set_drvdata(pdev, rng);

	exynos_rng_dev = rng;

	ret = crypto_register_rng(&exynos_rng_alg);
	if (ret) {
		dev_err(&pdev->dev,
			"Couldn't register rng crypto alg: %d\n", ret);
		exynos_rng_dev = NULL;
	}

	return ret;
}

static int exynos_rng_remove(struct platform_device *pdev)
{
	crypto_unregister_rng(&exynos_rng_alg);

	exynos_rng_dev = NULL;

	return 0;
}

static int __maybe_unused exynos_rng_suspend(struct device *dev)
{
	struct exynos_rng_dev *rng = dev_get_drvdata(dev);
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
	exynos_rng_get_random(rng, rng->seed_save, sizeof(rng->seed_save),
			      &(rng->seed_save_len));

	mutex_unlock(&rng->lock);

	dev_dbg(rng->dev, "Stored %u bytes for seeding on system resume\n",
		rng->seed_save_len);

	clk_disable_unprepare(rng->clk);

	return 0;
}

static int __maybe_unused exynos_rng_resume(struct device *dev)
{
	struct exynos_rng_dev *rng = dev_get_drvdata(dev);
	int ret;

	/* Never seeded so nothing to do */
	if (!rng->last_seeding)
		return 0;

	ret = clk_prepare_enable(rng->clk);
	if (ret)
		return ret;

	mutex_lock(&rng->lock);

	ret = exynos_rng_set_seed(rng, rng->seed_save, rng->seed_save_len);

	mutex_unlock(&rng->lock);

	clk_disable_unprepare(rng->clk);

	return ret;
}

static SIMPLE_DEV_PM_OPS(exynos_rng_pm_ops, exynos_rng_suspend,
			 exynos_rng_resume);

static const struct of_device_id exynos_rng_dt_match[] = {
	{
		.compatible = "samsung,exynos4-rng",
		.data = (const void *)EXYNOS_PRNG_EXYNOS4,
	}, {
		.compatible = "samsung,exynos5250-prng",
		.data = (const void *)EXYNOS_PRNG_EXYNOS5,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, exynos_rng_dt_match);

static struct platform_driver exynos_rng_driver = {
	.driver		= {
		.name	= "exynos-rng",
		.pm	= &exynos_rng_pm_ops,
		.of_match_table = exynos_rng_dt_match,
	},
	.probe		= exynos_rng_probe,
	.remove		= exynos_rng_remove,
};

module_platform_driver(exynos_rng_driver);

MODULE_DESCRIPTION("Exynos H/W Random Number Generator driver");
MODULE_AUTHOR("Krzysztof Kozlowski <krzk@kernel.org>");
MODULE_LICENSE("GPL v2");
