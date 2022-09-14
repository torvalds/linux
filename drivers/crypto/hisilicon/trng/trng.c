// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 HiSilicon Limited. */

#include <linux/acpi.h>
#include <linux/crypto.h>
#include <linux/err.h>
#include <linux/hw_random.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/random.h>
#include <crypto/internal/rng.h>

#define HISI_TRNG_REG		0x00F0
#define HISI_TRNG_BYTES		4
#define HISI_TRNG_QUALITY	512
#define HISI_TRNG_VERSION	0x01B8
#define HISI_TRNG_VER_V1	GENMASK(31, 0)
#define SLEEP_US		10
#define TIMEOUT_US		10000
#define SW_DRBG_NUM_SHIFT	2
#define SW_DRBG_KEY_BASE	0x082C
#define SW_DRBG_SEED(n)         (SW_DRBG_KEY_BASE - ((n) << SW_DRBG_NUM_SHIFT))
#define SW_DRBG_SEED_REGS_NUM	12
#define SW_DRBG_SEED_SIZE	48
#define SW_DRBG_BLOCKS		0x0830
#define SW_DRBG_INIT		0x0834
#define SW_DRBG_GEN		0x083c
#define SW_DRBG_STATUS		0x0840
#define SW_DRBG_BLOCKS_NUM	4095
#define SW_DRBG_DATA_BASE	0x0850
#define SW_DRBG_DATA_NUM	4
#define SW_DRBG_DATA(n)		(SW_DRBG_DATA_BASE - ((n) << SW_DRBG_NUM_SHIFT))
#define SW_DRBG_BYTES		16
#define SW_DRBG_ENABLE_SHIFT	12
#define SEED_SHIFT_24		24
#define SEED_SHIFT_16		16
#define SEED_SHIFT_8		8

struct hisi_trng_list {
	struct mutex lock;
	struct list_head list;
	bool is_init;
};

struct hisi_trng {
	void __iomem *base;
	struct hisi_trng_list *trng_list;
	struct list_head list;
	struct hwrng rng;
	u32 ver;
	bool is_used;
	struct mutex mutex;
};

struct hisi_trng_ctx {
	struct hisi_trng *trng;
};

static atomic_t trng_active_devs;
static struct hisi_trng_list trng_devices;

static void hisi_trng_set_seed(struct hisi_trng *trng, const u8 *seed)
{
	u32 val, seed_reg, i;

	for (i = 0; i < SW_DRBG_SEED_SIZE;
	     i += SW_DRBG_SEED_SIZE / SW_DRBG_SEED_REGS_NUM) {
		val = seed[i] << SEED_SHIFT_24;
		val |= seed[i + 1UL] << SEED_SHIFT_16;
		val |= seed[i + 2UL] << SEED_SHIFT_8;
		val |= seed[i + 3UL];

		seed_reg = (i >> SW_DRBG_NUM_SHIFT) % SW_DRBG_SEED_REGS_NUM;
		writel(val, trng->base + SW_DRBG_SEED(seed_reg));
	}
}

static int hisi_trng_seed(struct crypto_rng *tfm, const u8 *seed,
			  unsigned int slen)
{
	struct hisi_trng_ctx *ctx = crypto_rng_ctx(tfm);
	struct hisi_trng *trng = ctx->trng;
	u32 val = 0;
	int ret = 0;

	if (slen < SW_DRBG_SEED_SIZE) {
		pr_err("slen(%u) is not matched with trng(%d)\n", slen,
			SW_DRBG_SEED_SIZE);
		return -EINVAL;
	}

	writel(0x0, trng->base + SW_DRBG_BLOCKS);
	hisi_trng_set_seed(trng, seed);

	writel(SW_DRBG_BLOCKS_NUM | (0x1 << SW_DRBG_ENABLE_SHIFT),
	       trng->base + SW_DRBG_BLOCKS);
	writel(0x1, trng->base + SW_DRBG_INIT);

	ret = readl_relaxed_poll_timeout(trng->base + SW_DRBG_STATUS,
					val, val & BIT(0), SLEEP_US, TIMEOUT_US);
	if (ret)
		pr_err("fail to init trng(%d)\n", ret);

	return ret;
}

static int hisi_trng_generate(struct crypto_rng *tfm, const u8 *src,
			      unsigned int slen, u8 *dstn, unsigned int dlen)
{
	struct hisi_trng_ctx *ctx = crypto_rng_ctx(tfm);
	struct hisi_trng *trng = ctx->trng;
	u32 data[SW_DRBG_DATA_NUM];
	u32 currsize = 0;
	u32 val = 0;
	int ret;
	u32 i;

	if (dlen > SW_DRBG_BLOCKS_NUM * SW_DRBG_BYTES || dlen == 0) {
		pr_err("dlen(%d) exceeds limit(%d)!\n", dlen,
			SW_DRBG_BLOCKS_NUM * SW_DRBG_BYTES);
		return -EINVAL;
	}

	do {
		ret = readl_relaxed_poll_timeout(trng->base + SW_DRBG_STATUS,
		     val, val & BIT(1), SLEEP_US, TIMEOUT_US);
		if (ret) {
			pr_err("fail to generate random number(%d)!\n", ret);
			break;
		}

		for (i = 0; i < SW_DRBG_DATA_NUM; i++)
			data[i] = readl(trng->base + SW_DRBG_DATA(i));

		if (dlen - currsize >= SW_DRBG_BYTES) {
			memcpy(dstn + currsize, data, SW_DRBG_BYTES);
			currsize += SW_DRBG_BYTES;
		} else {
			memcpy(dstn + currsize, data, dlen - currsize);
			currsize = dlen;
		}

		writel(0x1, trng->base + SW_DRBG_GEN);
	} while (currsize < dlen);

	return ret;
}

static int hisi_trng_init(struct crypto_tfm *tfm)
{
	struct hisi_trng_ctx *ctx = crypto_tfm_ctx(tfm);
	struct hisi_trng *trng;
	int ret = -EBUSY;

	mutex_lock(&trng_devices.lock);
	list_for_each_entry(trng, &trng_devices.list, list) {
		if (!trng->is_used) {
			trng->is_used = true;
			ctx->trng = trng;
			ret = 0;
			break;
		}
	}
	mutex_unlock(&trng_devices.lock);

	return ret;
}

static void hisi_trng_exit(struct crypto_tfm *tfm)
{
	struct hisi_trng_ctx *ctx = crypto_tfm_ctx(tfm);

	mutex_lock(&trng_devices.lock);
	ctx->trng->is_used = false;
	mutex_unlock(&trng_devices.lock);
}

static int hisi_trng_read(struct hwrng *rng, void *buf, size_t max, bool wait)
{
	struct hisi_trng *trng;
	int currsize = 0;
	u32 val = 0;
	int ret;

	trng = container_of(rng, struct hisi_trng, rng);

	do {
		ret = readl_poll_timeout(trng->base + HISI_TRNG_REG, val,
					 val, SLEEP_US, TIMEOUT_US);
		if (ret)
			return currsize;

		if (max - currsize >= HISI_TRNG_BYTES) {
			memcpy(buf + currsize, &val, HISI_TRNG_BYTES);
			currsize += HISI_TRNG_BYTES;
			if (currsize == max)
				return currsize;
			continue;
		}

		/* copy remaining bytes */
		memcpy(buf + currsize, &val, max - currsize);
		currsize = max;
	} while (currsize < max);

	return currsize;
}

static struct rng_alg hisi_trng_alg = {
	.generate = hisi_trng_generate,
	.seed =	hisi_trng_seed,
	.seedsize = SW_DRBG_SEED_SIZE,
	.base = {
		.cra_name = "stdrng",
		.cra_driver_name = "hisi_stdrng",
		.cra_priority = 300,
		.cra_ctxsize = sizeof(struct hisi_trng_ctx),
		.cra_module = THIS_MODULE,
		.cra_init = hisi_trng_init,
		.cra_exit = hisi_trng_exit,
	},
};

static void hisi_trng_add_to_list(struct hisi_trng *trng)
{
	mutex_lock(&trng_devices.lock);
	list_add_tail(&trng->list, &trng_devices.list);
	mutex_unlock(&trng_devices.lock);
}

static int hisi_trng_del_from_list(struct hisi_trng *trng)
{
	int ret = -EBUSY;

	mutex_lock(&trng_devices.lock);
	if (!trng->is_used) {
		list_del(&trng->list);
		ret = 0;
	}
	mutex_unlock(&trng_devices.lock);

	return ret;
}

static int hisi_trng_probe(struct platform_device *pdev)
{
	struct hisi_trng *trng;
	int ret;

	trng = devm_kzalloc(&pdev->dev, sizeof(*trng), GFP_KERNEL);
	if (!trng)
		return -ENOMEM;

	platform_set_drvdata(pdev, trng);

	trng->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(trng->base))
		return PTR_ERR(trng->base);

	trng->is_used = false;
	trng->ver = readl(trng->base + HISI_TRNG_VERSION);
	if (!trng_devices.is_init) {
		INIT_LIST_HEAD(&trng_devices.list);
		mutex_init(&trng_devices.lock);
		trng_devices.is_init = true;
	}

	hisi_trng_add_to_list(trng);
	if (trng->ver != HISI_TRNG_VER_V1 &&
	    atomic_inc_return(&trng_active_devs) == 1) {
		ret = crypto_register_rng(&hisi_trng_alg);
		if (ret) {
			dev_err(&pdev->dev,
				"failed to register crypto(%d)\n", ret);
			atomic_dec_return(&trng_active_devs);
			goto err_remove_from_list;
		}
	}

	trng->rng.name = pdev->name;
	trng->rng.read = hisi_trng_read;
	trng->rng.quality = HISI_TRNG_QUALITY;
	ret = devm_hwrng_register(&pdev->dev, &trng->rng);
	if (ret) {
		dev_err(&pdev->dev, "failed to register hwrng: %d!\n", ret);
		goto err_crypto_unregister;
	}

	return ret;

err_crypto_unregister:
	if (trng->ver != HISI_TRNG_VER_V1 &&
	    atomic_dec_return(&trng_active_devs) == 0)
		crypto_unregister_rng(&hisi_trng_alg);

err_remove_from_list:
	hisi_trng_del_from_list(trng);
	return ret;
}

static int hisi_trng_remove(struct platform_device *pdev)
{
	struct hisi_trng *trng = platform_get_drvdata(pdev);

	/* Wait until the task is finished */
	while (hisi_trng_del_from_list(trng))
		;

	if (trng->ver != HISI_TRNG_VER_V1 &&
	    atomic_dec_return(&trng_active_devs) == 0)
		crypto_unregister_rng(&hisi_trng_alg);

	return 0;
}

static const struct acpi_device_id hisi_trng_acpi_match[] = {
	{ "HISI02B3", 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, hisi_trng_acpi_match);

static struct platform_driver hisi_trng_driver = {
	.probe		= hisi_trng_probe,
	.remove         = hisi_trng_remove,
	.driver		= {
		.name	= "hisi-trng-v2",
		.acpi_match_table = ACPI_PTR(hisi_trng_acpi_match),
	},
};

module_platform_driver(hisi_trng_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Weili Qian <qianweili@huawei.com>");
MODULE_AUTHOR("Zaibo Xu <xuzaibo@huawei.com>");
MODULE_DESCRIPTION("HiSilicon true random number generator V2 driver");
