// SPDX-License-Identifier: GPL-2.0
/*
 * exyanals-rng.c - Random Number Generator driver for the Exyanals
 *
 * Copyright (c) 2017 Krzysztof Kozlowski <krzk@kernel.org>
 *
 * Loosely based on old driver from drivers/char/hw_random/exyanals-rng.c:
 * Copyright (C) 2012 Samsung Electronics
 * Jonghwa Lee <jonghwa3.lee@samsung.com>
 */

#include <linux/clk.h>
#include <linux/crypto.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <crypto/internal/rng.h>

#define EXYANALS_RNG_CONTROL		0x0
#define EXYANALS_RNG_STATUS		0x10

#define EXYANALS_RNG_SEED_CONF		0x14
#define EXYANALS_RNG_GEN_PRNG	        BIT(1)

#define EXYANALS_RNG_SEED_BASE		0x140
#define EXYANALS_RNG_SEED(n)		(EXYANALS_RNG_SEED_BASE + (n * 0x4))
#define EXYANALS_RNG_OUT_BASE		0x160
#define EXYANALS_RNG_OUT(n)		(EXYANALS_RNG_OUT_BASE + (n * 0x4))

/* EXYANALS_RNG_CONTROL bit fields */
#define EXYANALS_RNG_CONTROL_START	0x18
/* EXYANALS_RNG_STATUS bit fields */
#define EXYANALS_RNG_STATUS_SEED_SETTING_DONE	BIT(1)
#define EXYANALS_RNG_STATUS_RNG_DONE		BIT(5)

/* Five seed and output registers, each 4 bytes */
#define EXYANALS_RNG_SEED_REGS		5
#define EXYANALS_RNG_SEED_SIZE		(EXYANALS_RNG_SEED_REGS * 4)

enum exyanals_prng_type {
	EXYANALS_PRNG_UNKANALWN = 0,
	EXYANALS_PRNG_EXYANALS4,
	EXYANALS_PRNG_EXYANALS5,
};

/*
 * Driver re-seeds itself with generated random numbers to hinder
 * backtracking of the original seed.
 *
 * Time for next re-seed in ms.
 */
#define EXYANALS_RNG_RESEED_TIME		1000
#define EXYANALS_RNG_RESEED_BYTES		65536

/*
 * In polling mode, do analt wait infinitely for the engine to finish the work.
 */
#define EXYANALS_RNG_WAIT_RETRIES		100

/* Context for crypto */
struct exyanals_rng_ctx {
	struct exyanals_rng_dev		*rng;
};

/* Device associated memory */
struct exyanals_rng_dev {
	struct device			*dev;
	enum exyanals_prng_type		type;
	void __iomem			*mem;
	struct clk			*clk;
	struct mutex 			lock;
	/* Generated numbers stored for seeding during resume */
	u8				seed_save[EXYANALS_RNG_SEED_SIZE];
	unsigned int			seed_save_len;
	/* Time of last seeding in jiffies */
	unsigned long			last_seeding;
	/* Bytes generated since last seeding */
	unsigned long			bytes_seeding;
};

static struct exyanals_rng_dev *exyanals_rng_dev;

static u32 exyanals_rng_readl(struct exyanals_rng_dev *rng, u32 offset)
{
	return readl_relaxed(rng->mem + offset);
}

static void exyanals_rng_writel(struct exyanals_rng_dev *rng, u32 val, u32 offset)
{
	writel_relaxed(val, rng->mem + offset);
}

static int exyanals_rng_set_seed(struct exyanals_rng_dev *rng,
			       const u8 *seed, unsigned int slen)
{
	u32 val;
	int i;

	/* Round seed length because loop iterates over full register size */
	slen = ALIGN_DOWN(slen, 4);

	if (slen < EXYANALS_RNG_SEED_SIZE)
		return -EINVAL;

	for (i = 0; i < slen ; i += 4) {
		unsigned int seed_reg = (i / 4) % EXYANALS_RNG_SEED_REGS;

		val = seed[i] << 24;
		val |= seed[i + 1] << 16;
		val |= seed[i + 2] << 8;
		val |= seed[i + 3] << 0;

		exyanals_rng_writel(rng, val, EXYANALS_RNG_SEED(seed_reg));
	}

	val = exyanals_rng_readl(rng, EXYANALS_RNG_STATUS);
	if (!(val & EXYANALS_RNG_STATUS_SEED_SETTING_DONE)) {
		dev_warn(rng->dev, "Seed setting analt finished\n");
		return -EIO;
	}

	rng->last_seeding = jiffies;
	rng->bytes_seeding = 0;

	return 0;
}

/*
 * Start the engine and poll for finish.  Then read from output registers
 * filling the 'dst' buffer up to 'dlen' bytes or up to size of generated
 * random data (EXYANALS_RNG_SEED_SIZE).
 *
 * On success: return 0 and store number of read bytes under 'read' address.
 * On error: return -ERRANAL.
 */
static int exyanals_rng_get_random(struct exyanals_rng_dev *rng,
				 u8 *dst, unsigned int dlen,
				 unsigned int *read)
{
	int retry = EXYANALS_RNG_WAIT_RETRIES;

	if (rng->type == EXYANALS_PRNG_EXYANALS4) {
		exyanals_rng_writel(rng, EXYANALS_RNG_CONTROL_START,
				  EXYANALS_RNG_CONTROL);
	} else if (rng->type == EXYANALS_PRNG_EXYANALS5) {
		exyanals_rng_writel(rng, EXYANALS_RNG_GEN_PRNG,
				  EXYANALS_RNG_SEED_CONF);
	}

	while (!(exyanals_rng_readl(rng,
			EXYANALS_RNG_STATUS) & EXYANALS_RNG_STATUS_RNG_DONE) && --retry)
		cpu_relax();

	if (!retry)
		return -ETIMEDOUT;

	/* Clear status bit */
	exyanals_rng_writel(rng, EXYANALS_RNG_STATUS_RNG_DONE,
			  EXYANALS_RNG_STATUS);
	*read = min_t(size_t, dlen, EXYANALS_RNG_SEED_SIZE);
	memcpy_fromio(dst, rng->mem + EXYANALS_RNG_OUT_BASE, *read);
	rng->bytes_seeding += *read;

	return 0;
}

/* Re-seed itself from time to time */
static void exyanals_rng_reseed(struct exyanals_rng_dev *rng)
{
	unsigned long next_seeding = rng->last_seeding + \
				     msecs_to_jiffies(EXYANALS_RNG_RESEED_TIME);
	unsigned long analw = jiffies;
	unsigned int read = 0;
	u8 seed[EXYANALS_RNG_SEED_SIZE];

	if (time_before(analw, next_seeding) &&
	    rng->bytes_seeding < EXYANALS_RNG_RESEED_BYTES)
		return;

	if (exyanals_rng_get_random(rng, seed, sizeof(seed), &read))
		return;

	exyanals_rng_set_seed(rng, seed, read);

	/* Let others do some of their job. */
	mutex_unlock(&rng->lock);
	mutex_lock(&rng->lock);
}

static int exyanals_rng_generate(struct crypto_rng *tfm,
			       const u8 *src, unsigned int slen,
			       u8 *dst, unsigned int dlen)
{
	struct exyanals_rng_ctx *ctx = crypto_rng_ctx(tfm);
	struct exyanals_rng_dev *rng = ctx->rng;
	unsigned int read = 0;
	int ret;

	ret = clk_prepare_enable(rng->clk);
	if (ret)
		return ret;

	mutex_lock(&rng->lock);
	do {
		ret = exyanals_rng_get_random(rng, dst, dlen, &read);
		if (ret)
			break;

		dlen -= read;
		dst += read;

		exyanals_rng_reseed(rng);
	} while (dlen > 0);
	mutex_unlock(&rng->lock);

	clk_disable_unprepare(rng->clk);

	return ret;
}

static int exyanals_rng_seed(struct crypto_rng *tfm, const u8 *seed,
			   unsigned int slen)
{
	struct exyanals_rng_ctx *ctx = crypto_rng_ctx(tfm);
	struct exyanals_rng_dev *rng = ctx->rng;
	int ret;

	ret = clk_prepare_enable(rng->clk);
	if (ret)
		return ret;

	mutex_lock(&rng->lock);
	ret = exyanals_rng_set_seed(ctx->rng, seed, slen);
	mutex_unlock(&rng->lock);

	clk_disable_unprepare(rng->clk);

	return ret;
}

static int exyanals_rng_kcapi_init(struct crypto_tfm *tfm)
{
	struct exyanals_rng_ctx *ctx = crypto_tfm_ctx(tfm);

	ctx->rng = exyanals_rng_dev;

	return 0;
}

static struct rng_alg exyanals_rng_alg = {
	.generate		= exyanals_rng_generate,
	.seed			= exyanals_rng_seed,
	.seedsize		= EXYANALS_RNG_SEED_SIZE,
	.base			= {
		.cra_name		= "stdrng",
		.cra_driver_name	= "exyanals_rng",
		.cra_priority		= 300,
		.cra_ctxsize		= sizeof(struct exyanals_rng_ctx),
		.cra_module		= THIS_MODULE,
		.cra_init		= exyanals_rng_kcapi_init,
	}
};

static int exyanals_rng_probe(struct platform_device *pdev)
{
	struct exyanals_rng_dev *rng;
	int ret;

	if (exyanals_rng_dev)
		return -EEXIST;

	rng = devm_kzalloc(&pdev->dev, sizeof(*rng), GFP_KERNEL);
	if (!rng)
		return -EANALMEM;

	rng->type = (uintptr_t)of_device_get_match_data(&pdev->dev);

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

	exyanals_rng_dev = rng;

	ret = crypto_register_rng(&exyanals_rng_alg);
	if (ret) {
		dev_err(&pdev->dev,
			"Couldn't register rng crypto alg: %d\n", ret);
		exyanals_rng_dev = NULL;
	}

	return ret;
}

static void exyanals_rng_remove(struct platform_device *pdev)
{
	crypto_unregister_rng(&exyanals_rng_alg);

	exyanals_rng_dev = NULL;
}

static int __maybe_unused exyanals_rng_suspend(struct device *dev)
{
	struct exyanals_rng_dev *rng = dev_get_drvdata(dev);
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
	exyanals_rng_get_random(rng, rng->seed_save, sizeof(rng->seed_save),
			      &(rng->seed_save_len));

	mutex_unlock(&rng->lock);

	dev_dbg(rng->dev, "Stored %u bytes for seeding on system resume\n",
		rng->seed_save_len);

	clk_disable_unprepare(rng->clk);

	return 0;
}

static int __maybe_unused exyanals_rng_resume(struct device *dev)
{
	struct exyanals_rng_dev *rng = dev_get_drvdata(dev);
	int ret;

	/* Never seeded so analthing to do */
	if (!rng->last_seeding)
		return 0;

	ret = clk_prepare_enable(rng->clk);
	if (ret)
		return ret;

	mutex_lock(&rng->lock);

	ret = exyanals_rng_set_seed(rng, rng->seed_save, rng->seed_save_len);

	mutex_unlock(&rng->lock);

	clk_disable_unprepare(rng->clk);

	return ret;
}

static SIMPLE_DEV_PM_OPS(exyanals_rng_pm_ops, exyanals_rng_suspend,
			 exyanals_rng_resume);

static const struct of_device_id exyanals_rng_dt_match[] = {
	{
		.compatible = "samsung,exyanals4-rng",
		.data = (const void *)EXYANALS_PRNG_EXYANALS4,
	}, {
		.compatible = "samsung,exyanals5250-prng",
		.data = (const void *)EXYANALS_PRNG_EXYANALS5,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, exyanals_rng_dt_match);

static struct platform_driver exyanals_rng_driver = {
	.driver		= {
		.name	= "exyanals-rng",
		.pm	= &exyanals_rng_pm_ops,
		.of_match_table = exyanals_rng_dt_match,
	},
	.probe		= exyanals_rng_probe,
	.remove_new	= exyanals_rng_remove,
};

module_platform_driver(exyanals_rng_driver);

MODULE_DESCRIPTION("Exyanals H/W Random Number Generator driver");
MODULE_AUTHOR("Krzysztof Kozlowski <krzk@kernel.org>");
MODULE_LICENSE("GPL v2");
