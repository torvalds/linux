// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2024 Christian Marangi */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/hw_random.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/platform_device.h>

#define TRNG_IP_RDY			0x800
#define   CNT_TRANS			GENMASK(15, 8)
#define   SAMPLE_RDY			BIT(0)
#define TRNG_NS_SEK_AND_DAT_EN		0x804
#define	  RNG_EN			BIT(31) /* referenced as ring_en */
#define	  RAW_DATA_EN			BIT(16)
#define TRNG_HEALTH_TEST_SW_RST		0x808
#define   SW_RST			BIT(0) /* Active High */
#define TRNG_INTR_EN			0x818
#define   INTR_MASK			BIT(16)
#define   CONTINUOUS_HEALTH_INITR_EN	BIT(2)
#define   SW_STARTUP_INITR_EN		BIT(1)
#define   RST_STARTUP_INITR_EN		BIT(0)
/* Notice that Health Test are done only out of Reset and with RNG_EN */
#define TRNG_HEALTH_TEST_STATUS		0x824
#define   CONTINUOUS_HEALTH_AP_TEST_FAIL BIT(23)
#define   CONTINUOUS_HEALTH_RC_TEST_FAIL BIT(22)
#define   SW_STARTUP_TEST_DONE		BIT(21)
#define   SW_STARTUP_AP_TEST_FAIL	BIT(20)
#define   SW_STARTUP_RC_TEST_FAIL	BIT(19)
#define   RST_STARTUP_TEST_DONE		BIT(18)
#define   RST_STARTUP_AP_TEST_FAIL	BIT(17)
#define   RST_STARTUP_RC_TEST_FAIL	BIT(16)
#define   RAW_DATA_VALID		BIT(7)

#define TRNG_RAW_DATA_OUT		0x828

#define TRNG_CNT_TRANS_VALID		0x80
#define BUSY_LOOP_SLEEP			10
#define BUSY_LOOP_TIMEOUT		(BUSY_LOOP_SLEEP * 10000)

struct airoha_trng {
	void __iomem *base;
	struct hwrng rng;
	struct device *dev;

	struct completion rng_op_done;
};

static int airoha_trng_irq_mask(struct airoha_trng *trng)
{
	u32 val;

	val = readl(trng->base + TRNG_INTR_EN);
	val |= INTR_MASK;
	writel(val, trng->base + TRNG_INTR_EN);

	return 0;
}

static int airoha_trng_irq_unmask(struct airoha_trng *trng)
{
	u32 val;

	val = readl(trng->base + TRNG_INTR_EN);
	val &= ~INTR_MASK;
	writel(val, trng->base + TRNG_INTR_EN);

	return 0;
}

static int airoha_trng_init(struct hwrng *rng)
{
	struct airoha_trng *trng = container_of(rng, struct airoha_trng, rng);
	int ret;
	u32 val;

	val = readl(trng->base + TRNG_NS_SEK_AND_DAT_EN);
	val |= RNG_EN;
	writel(val, trng->base + TRNG_NS_SEK_AND_DAT_EN);

	/* Set out of SW Reset */
	airoha_trng_irq_unmask(trng);
	writel(0, trng->base + TRNG_HEALTH_TEST_SW_RST);

	ret = wait_for_completion_timeout(&trng->rng_op_done, BUSY_LOOP_TIMEOUT);
	if (ret <= 0) {
		dev_err(trng->dev, "Timeout waiting for Health Check\n");
		airoha_trng_irq_mask(trng);
		return -ENODEV;
	}

	/* Check if Health Test Failed */
	val = readl(trng->base + TRNG_HEALTH_TEST_STATUS);
	if (val & (RST_STARTUP_AP_TEST_FAIL | RST_STARTUP_RC_TEST_FAIL)) {
		dev_err(trng->dev, "Health Check fail: %s test fail\n",
			val & RST_STARTUP_AP_TEST_FAIL ? "AP" : "RC");
		return -ENODEV;
	}

	/* Check if IP is ready */
	ret = readl_poll_timeout(trng->base + TRNG_IP_RDY, val,
				 val & SAMPLE_RDY, 10, 1000);
	if (ret < 0) {
		dev_err(trng->dev, "Timeout waiting for IP ready");
		return -ENODEV;
	}

	/* CNT_TRANS must be 0x80 for IP to be considered ready */
	ret = readl_poll_timeout(trng->base + TRNG_IP_RDY, val,
				 FIELD_GET(CNT_TRANS, val) == TRNG_CNT_TRANS_VALID,
				 10, 1000);
	if (ret < 0) {
		dev_err(trng->dev, "Timeout waiting for IP ready");
		return -ENODEV;
	}

	return 0;
}

static void airoha_trng_cleanup(struct hwrng *rng)
{
	struct airoha_trng *trng = container_of(rng, struct airoha_trng, rng);
	u32 val;

	val = readl(trng->base + TRNG_NS_SEK_AND_DAT_EN);
	val &= ~RNG_EN;
	writel(val, trng->base + TRNG_NS_SEK_AND_DAT_EN);

	/* Put it in SW Reset */
	writel(SW_RST, trng->base + TRNG_HEALTH_TEST_SW_RST);
}

static int airoha_trng_read(struct hwrng *rng, void *buf, size_t max, bool wait)
{
	struct airoha_trng *trng = container_of(rng, struct airoha_trng, rng);
	u32 *data = buf;
	u32 status;
	int ret;

	ret = readl_poll_timeout(trng->base + TRNG_HEALTH_TEST_STATUS, status,
				 status & RAW_DATA_VALID, 10, 1000);
	if (ret < 0) {
		dev_err(trng->dev, "Timeout waiting for TRNG RAW Data valid\n");
		return ret;
	}

	*data = readl(trng->base + TRNG_RAW_DATA_OUT);

	return 4;
}

static irqreturn_t airoha_trng_irq(int irq, void *priv)
{
	struct airoha_trng *trng = (struct airoha_trng *)priv;

	airoha_trng_irq_mask(trng);
	/* Just complete the task, we will read the value later */
	complete(&trng->rng_op_done);

	return IRQ_HANDLED;
}

static int airoha_trng_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct airoha_trng *trng;
	int irq, ret;
	u32 val;

	trng = devm_kzalloc(dev, sizeof(*trng), GFP_KERNEL);
	if (!trng)
		return -ENOMEM;

	trng->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(trng->base))
		return PTR_ERR(trng->base);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	airoha_trng_irq_mask(trng);
	ret = devm_request_irq(&pdev->dev, irq, airoha_trng_irq, 0,
			       pdev->name, (void *)trng);
	if (ret) {
		dev_err(dev, "Can't get interrupt working.\n");
		return ret;
	}

	init_completion(&trng->rng_op_done);

	/* Enable interrupt for SW reset Health Check */
	val = readl(trng->base + TRNG_INTR_EN);
	val |= RST_STARTUP_INITR_EN;
	writel(val, trng->base + TRNG_INTR_EN);

	/* Set output to raw data */
	val = readl(trng->base + TRNG_NS_SEK_AND_DAT_EN);
	val |= RAW_DATA_EN;
	writel(val, trng->base + TRNG_NS_SEK_AND_DAT_EN);

	/* Put it in SW Reset */
	writel(SW_RST, trng->base + TRNG_HEALTH_TEST_SW_RST);

	trng->dev = dev;
	trng->rng.name = pdev->name;
	trng->rng.init = airoha_trng_init;
	trng->rng.cleanup = airoha_trng_cleanup;
	trng->rng.read = airoha_trng_read;

	ret = devm_hwrng_register(dev, &trng->rng);
	if (ret) {
		dev_err(dev, "failed to register rng device: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct of_device_id airoha_trng_of_match[] = {
	{ .compatible = "airoha,en7581-trng", },
	{},
};
MODULE_DEVICE_TABLE(of, airoha_trng_of_match);

static struct platform_driver airoha_trng_driver = {
	.driver = {
		.name = "airoha-trng",
		.of_match_table	= airoha_trng_of_match,
	},
	.probe = airoha_trng_probe,
};

module_platform_driver(airoha_trng_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Christian Marangi <ansuelsmth@gmail.com>");
MODULE_DESCRIPTION("Airoha True Random Number Generator driver");
