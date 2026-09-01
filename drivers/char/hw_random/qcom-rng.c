// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2017-18 Linaro Limited
//
// Based on msm-rng.c and downstream driver

#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/hw_random.h>
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

#define QCOM_TRNG_QUALITY	1024

struct qcom_rng {
	void __iomem *base;
	struct clk *clk;
	struct hwrng hwrng;
};

struct qcom_rng_match_data {
	bool hwrng_support;
};

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

		if ((max - currsize) >= WORD_SZ) {
			memcpy(data, &val, WORD_SZ);
			data += WORD_SZ;
			currsize += WORD_SZ;
		} else {
			/* copy only remaining bytes */
			memcpy(data, &val, max - currsize);
			currsize = max;
		}
	} while (currsize < max);

	return currsize;
}

static int qcom_hwrng_init(struct hwrng *hwrng)
{
	struct qcom_rng *qrng = container_of(hwrng, struct qcom_rng, hwrng);

	return clk_prepare_enable(qrng->clk);
}

static int qcom_hwrng_read(struct hwrng *hwrng, void *data, size_t max, bool wait)
{
	struct qcom_rng *qrng = container_of(hwrng, struct qcom_rng, hwrng);

	return qcom_rng_read(qrng, data, max);
}

static void qcom_hwrng_cleanup(struct hwrng *hwrng)
{
	struct qcom_rng *qrng = container_of(hwrng, struct qcom_rng, hwrng);

	clk_disable_unprepare(qrng->clk);
}

static int qcom_rng_probe(struct platform_device *pdev)
{
	const struct qcom_rng_match_data *match_data;
	struct qcom_rng *rng;
	int ret;

	match_data = device_get_match_data(&pdev->dev);
	if (match_data == NULL || !match_data->hwrng_support) {
		dev_info(&pdev->dev, "TRNG support not detected\n");
		/*
		 * In this case the driver does nothing except the dev_info(),
		 * but bind the device anyway to avoid effects on GCC state.
		 */
		return 0;
	}

	rng = devm_kzalloc(&pdev->dev, sizeof(*rng), GFP_KERNEL);
	if (!rng)
		return -ENOMEM;

	rng->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(rng->base))
		return PTR_ERR(rng->base);

	rng->clk = devm_clk_get_optional(&pdev->dev, "core");
	if (IS_ERR(rng->clk))
		return PTR_ERR(rng->clk);

	rng->hwrng.name = "qcom_hwrng";
	rng->hwrng.init = qcom_hwrng_init;
	rng->hwrng.read = qcom_hwrng_read;
	rng->hwrng.cleanup = qcom_hwrng_cleanup;
	rng->hwrng.quality = QCOM_TRNG_QUALITY;
	ret = devm_hwrng_register(&pdev->dev, &rng->hwrng);
	if (ret)
		dev_err(&pdev->dev, "Register hwrng failed: %d\n", ret);
	return ret;
}

static struct qcom_rng_match_data qcom_prng_match_data = {
	.hwrng_support = false,
};

static struct qcom_rng_match_data qcom_prng_ee_match_data = {
	.hwrng_support = false,
};

static struct qcom_rng_match_data qcom_trng_match_data = {
	.hwrng_support = true,
};

static const struct acpi_device_id __maybe_unused qcom_rng_acpi_match[] = {
	{ .id = "QCOM8160", .driver_data = (kernel_ulong_t)&qcom_prng_ee_match_data },
	{}
};
MODULE_DEVICE_TABLE(acpi, qcom_rng_acpi_match);

static const struct of_device_id __maybe_unused qcom_rng_of_match[] = {
	{ .compatible = "qcom,prng", .data = &qcom_prng_match_data },
	{ .compatible = "qcom,prng-ee", .data = &qcom_prng_ee_match_data },
	{ .compatible = "qcom,trng", .data = &qcom_trng_match_data },
	{}
};
MODULE_DEVICE_TABLE(of, qcom_rng_of_match);

static struct platform_driver qcom_rng_driver = {
	.probe = qcom_rng_probe,
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = qcom_rng_of_match,
		.acpi_match_table = ACPI_PTR(qcom_rng_acpi_match),
	}
};
module_platform_driver(qcom_rng_driver);

MODULE_ALIAS("platform:" KBUILD_MODNAME);
MODULE_DESCRIPTION("Qualcomm random number generator driver");
MODULE_LICENSE("GPL v2");
