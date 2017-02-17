/*
 * NXP LPC18xx/43xx OTP memory NVMEM driver
 *
 * Copyright (c) 2016 Joachim Eastwood <manabian@gmail.com>
 *
 * Based on the imx ocotp driver,
 * Copyright (c) 2015 Pengutronix, Philipp Zabel <p.zabel@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * TODO: add support for writing OTP register via API in boot ROM.
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

/*
 * LPC18xx OTP memory contains 4 banks with 4 32-bit words. Bank 0 starts
 * at offset 0 from the base.
 *
 * Bank 0 contains the part ID for Flashless devices and is reseverd for
 * devices with Flash.
 * Bank 1/2 is generale purpose or AES key storage for secure devices.
 * Bank 3 contains control data, USB ID and generale purpose words.
 */
#define LPC18XX_OTP_NUM_BANKS		4
#define LPC18XX_OTP_WORDS_PER_BANK	4
#define LPC18XX_OTP_WORD_SIZE		sizeof(u32)
#define LPC18XX_OTP_SIZE		(LPC18XX_OTP_NUM_BANKS * \
					 LPC18XX_OTP_WORDS_PER_BANK * \
					 LPC18XX_OTP_WORD_SIZE)

struct lpc18xx_otp {
	void __iomem *base;
};

static int lpc18xx_otp_read(void *context, unsigned int offset,
			    void *val, size_t bytes)
{
	struct lpc18xx_otp *otp = context;
	unsigned int count = bytes >> 2;
	u32 index = offset >> 2;
	u32 *buf = val;
	int i;

	if (count > (LPC18XX_OTP_SIZE - index))
		count = LPC18XX_OTP_SIZE - index;

	for (i = index; i < (index + count); i++)
		*buf++ = readl(otp->base + i * LPC18XX_OTP_WORD_SIZE);

	return 0;
}

static struct nvmem_config lpc18xx_otp_nvmem_config = {
	.name = "lpc18xx-otp",
	.read_only = true,
	.word_size = LPC18XX_OTP_WORD_SIZE,
	.stride = LPC18XX_OTP_WORD_SIZE,
	.owner = THIS_MODULE,
	.reg_read = lpc18xx_otp_read,
};

static int lpc18xx_otp_probe(struct platform_device *pdev)
{
	struct nvmem_device *nvmem;
	struct lpc18xx_otp *otp;
	struct resource *res;

	otp = devm_kzalloc(&pdev->dev, sizeof(*otp), GFP_KERNEL);
	if (!otp)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	otp->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(otp->base))
		return PTR_ERR(otp->base);

	lpc18xx_otp_nvmem_config.size = LPC18XX_OTP_SIZE;
	lpc18xx_otp_nvmem_config.dev = &pdev->dev;
	lpc18xx_otp_nvmem_config.priv = otp;

	nvmem = nvmem_register(&lpc18xx_otp_nvmem_config);
	if (IS_ERR(nvmem))
		return PTR_ERR(nvmem);

	platform_set_drvdata(pdev, nvmem);

	return 0;
}

static int lpc18xx_otp_remove(struct platform_device *pdev)
{
	struct nvmem_device *nvmem = platform_get_drvdata(pdev);

	return nvmem_unregister(nvmem);
}

static const struct of_device_id lpc18xx_otp_dt_ids[] = {
	{ .compatible = "nxp,lpc1850-otp" },
	{ },
};
MODULE_DEVICE_TABLE(of, lpc18xx_otp_dt_ids);

static struct platform_driver lpc18xx_otp_driver = {
	.probe	= lpc18xx_otp_probe,
	.remove	= lpc18xx_otp_remove,
	.driver = {
		.name	= "lpc18xx_otp",
		.of_match_table = lpc18xx_otp_dt_ids,
	},
};
module_platform_driver(lpc18xx_otp_driver);

MODULE_AUTHOR("Joachim Eastwoood <manabian@gmail.com>");
MODULE_DESCRIPTION("NXP LPC18xx OTP NVMEM driver");
MODULE_LICENSE("GPL v2");
