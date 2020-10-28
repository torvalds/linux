// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2020 Xiphera Ltd. */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/hw_random.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/delay.h>

#define CONTROL_REG			0x00000000
#define STATUS_REG			0x00000004
#define RAND_REG			0x00000000

#define HOST_TO_TRNG_RESET		0x00000001
#define HOST_TO_TRNG_RELEASE_RESET	0x00000002
#define HOST_TO_TRNG_ENABLE		0x80000000
#define HOST_TO_TRNG_ZEROIZE		0x80000004
#define HOST_TO_TRNG_ACK_ZEROIZE	0x80000008
#define HOST_TO_TRNG_READ		0x8000000F

/* trng statuses */
#define TRNG_ACK_RESET			0x000000AC
#define TRNG_SUCCESSFUL_STARTUP		0x00000057
#define TRNG_FAILED_STARTUP		0x000000FA
#define TRNG_NEW_RAND_AVAILABLE		0x000000ED

struct xiphera_trng {
	void __iomem *mem;
	struct hwrng rng;
};

static int xiphera_trng_read(struct hwrng *rng, void *buf, size_t max, bool wait)
{
	struct xiphera_trng *trng = container_of(rng, struct xiphera_trng, rng);
	int ret = 0;

	while (max >= sizeof(u32)) {
		/* check for data */
		if (readl(trng->mem + STATUS_REG) == TRNG_NEW_RAND_AVAILABLE) {
			*(u32 *)buf = readl(trng->mem + RAND_REG);
			/*
			 * Inform the trng of the read
			 * and re-enable it to produce a new random number
			 */
			writel(HOST_TO_TRNG_READ, trng->mem + CONTROL_REG);
			writel(HOST_TO_TRNG_ENABLE, trng->mem + CONTROL_REG);
			ret += sizeof(u32);
			buf += sizeof(u32);
			max -= sizeof(u32);
		} else {
			break;
		}
	}
	return ret;
}

static int xiphera_trng_probe(struct platform_device *pdev)
{
	int ret;
	struct xiphera_trng *trng;
	struct device *dev = &pdev->dev;
	struct resource *res;

	trng = devm_kzalloc(dev, sizeof(*trng), GFP_KERNEL);
	if (!trng)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	trng->mem = devm_ioremap_resource(dev, res);
	if (IS_ERR(trng->mem))
		return PTR_ERR(trng->mem);

	/*
	 * the trng needs to be reset first which might not happen in time,
	 * hence we incorporate a small delay to ensure proper behaviour
	 */
	writel(HOST_TO_TRNG_RESET, trng->mem + CONTROL_REG);
	usleep_range(100, 200);

	if (readl(trng->mem + STATUS_REG) != TRNG_ACK_RESET) {
		/*
		 * there is a small chance the trng is just not ready yet,
		 * so we try one more time. If the second time fails, we give up
		 */
		usleep_range(100, 200);
		if (readl(trng->mem + STATUS_REG) != TRNG_ACK_RESET) {
			dev_err(dev, "failed to reset the trng ip\n");
			return -ENODEV;
		}
	}

	/*
	 * once again, to ensure proper behaviour we sleep
	 * for a while after zeroizing the trng
	 */
	writel(HOST_TO_TRNG_RELEASE_RESET, trng->mem + CONTROL_REG);
	writel(HOST_TO_TRNG_ENABLE, trng->mem + CONTROL_REG);
	writel(HOST_TO_TRNG_ZEROIZE, trng->mem + CONTROL_REG);
	msleep(20);

	if (readl(trng->mem + STATUS_REG) != TRNG_SUCCESSFUL_STARTUP) {
		/* diagnose the reason for the failure */
		if (readl(trng->mem + STATUS_REG) == TRNG_FAILED_STARTUP) {
			dev_err(dev, "trng ip startup-tests failed\n");
			return -ENODEV;
		}
		dev_err(dev, "startup-tests yielded no response\n");
		return -ENODEV;
	}

	writel(HOST_TO_TRNG_ACK_ZEROIZE, trng->mem + CONTROL_REG);

	trng->rng.name = pdev->name;
	trng->rng.read = xiphera_trng_read;
	trng->rng.quality = 900;

	ret = devm_hwrng_register(dev, &trng->rng);
	if (ret) {
		dev_err(dev, "failed to register rng device: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, trng);

	return 0;
}

static const struct of_device_id xiphera_trng_of_match[] = {
	{ .compatible = "xiphera,xip8001b-trng", },
	{},
};
MODULE_DEVICE_TABLE(of, xiphera_trng_of_match);

static struct platform_driver xiphera_trng_driver = {
	.driver = {
		.name = "xiphera-trng",
		.of_match_table	= xiphera_trng_of_match,
	},
	.probe = xiphera_trng_probe,
};

module_platform_driver(xiphera_trng_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Atte Tommiska");
MODULE_DESCRIPTION("Xiphera FPGA-based true random number generator driver");
