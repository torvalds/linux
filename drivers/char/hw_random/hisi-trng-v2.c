// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 HiSilicon Limited. */

#include <linux/acpi.h>
#include <linux/err.h>
#include <linux/hw_random.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/random.h>

#define HISI_TRNG_REG		0x00F0
#define HISI_TRNG_BYTES		4
#define HISI_TRNG_QUALITY	512
#define SLEEP_US		10
#define TIMEOUT_US		10000

struct hisi_trng {
	void __iomem *base;
	struct hwrng rng;
};

static int hisi_trng_read(struct hwrng *rng, void *buf, size_t max, bool wait)
{
	struct hisi_trng *trng;
	int currsize = 0;
	u32 val = 0;
	u32 ret;

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

static int hisi_trng_probe(struct platform_device *pdev)
{
	struct hisi_trng *trng;
	int ret;

	trng = devm_kzalloc(&pdev->dev, sizeof(*trng), GFP_KERNEL);
	if (!trng)
		return -ENOMEM;

	trng->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(trng->base))
		return PTR_ERR(trng->base);

	trng->rng.name = pdev->name;
	trng->rng.read = hisi_trng_read;
	trng->rng.quality = HISI_TRNG_QUALITY;

	ret = devm_hwrng_register(&pdev->dev, &trng->rng);
	if (ret)
		dev_err(&pdev->dev, "failed to register hwrng!\n");

	return ret;
}

static const struct acpi_device_id hisi_trng_acpi_match[] = {
	{ "HISI02B3", 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, hisi_trng_acpi_match);

static struct platform_driver hisi_trng_driver = {
	.probe		= hisi_trng_probe,
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
