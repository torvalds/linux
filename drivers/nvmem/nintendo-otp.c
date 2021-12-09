// SPDX-License-Identifier: GPL-2.0-only
/*
 * Nintendo Wii and Wii U OTP driver
 *
 * This is a driver exposing the OTP of a Nintendo Wii or Wii U console.
 *
 * This memory contains common and per-console keys, signatures and
 * related data required to access peripherals.
 *
 * Based on reversed documentation from https://wiiubrew.org/wiki/Hardware/OTP
 *
 * Copyright (C) 2021 Emmanuel Gil Peyrot <linkmauve@linkmauve.fr>
 */

#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/nvmem-provider.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#define HW_OTPCMD  0
#define HW_OTPDATA 4
#define OTP_READ   0x80000000
#define BANK_SIZE  128
#define WORD_SIZE  4

struct nintendo_otp_priv {
	void __iomem *regs;
};

struct nintendo_otp_devtype_data {
	const char *name;
	unsigned int num_banks;
};

static const struct nintendo_otp_devtype_data hollywood_otp_data = {
	.name = "wii-otp",
	.num_banks = 1,
};

static const struct nintendo_otp_devtype_data latte_otp_data = {
	.name = "wiiu-otp",
	.num_banks = 8,
};

static int nintendo_otp_reg_read(void *context,
				 unsigned int reg, void *_val, size_t bytes)
{
	struct nintendo_otp_priv *priv = context;
	u32 *val = _val;
	int words = bytes / WORD_SIZE;
	u32 bank, addr;

	while (words--) {
		bank = (reg / BANK_SIZE) << 8;
		addr = (reg / WORD_SIZE) % (BANK_SIZE / WORD_SIZE);
		iowrite32be(OTP_READ | bank | addr, priv->regs + HW_OTPCMD);
		*val++ = ioread32be(priv->regs + HW_OTPDATA);
		reg += WORD_SIZE;
	}

	return 0;
}

static const struct of_device_id nintendo_otp_of_table[] = {
	{ .compatible = "nintendo,hollywood-otp", .data = &hollywood_otp_data },
	{ .compatible = "nintendo,latte-otp", .data = &latte_otp_data },
	{/* sentinel */},
};
MODULE_DEVICE_TABLE(of, nintendo_otp_of_table);

static int nintendo_otp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct of_device_id *of_id =
		of_match_device(nintendo_otp_of_table, dev);
	struct resource *res;
	struct nvmem_device *nvmem;
	struct nintendo_otp_priv *priv;

	struct nvmem_config config = {
		.stride = WORD_SIZE,
		.word_size = WORD_SIZE,
		.reg_read = nintendo_otp_reg_read,
		.read_only = true,
		.root_only = true,
	};

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->regs))
		return PTR_ERR(priv->regs);

	if (of_id->data) {
		const struct nintendo_otp_devtype_data *data = of_id->data;
		config.name = data->name;
		config.size = data->num_banks * BANK_SIZE;
	}

	config.dev = dev;
	config.priv = priv;

	nvmem = devm_nvmem_register(dev, &config);

	return PTR_ERR_OR_ZERO(nvmem);
}

static struct platform_driver nintendo_otp_driver = {
	.probe = nintendo_otp_probe,
	.driver = {
		.name = "nintendo-otp",
		.of_match_table = nintendo_otp_of_table,
	},
};
module_platform_driver(nintendo_otp_driver);
MODULE_AUTHOR("Emmanuel Gil Peyrot <linkmauve@linkmauve.fr>");
MODULE_DESCRIPTION("Nintendo Wii and Wii U OTP driver");
MODULE_LICENSE("GPL v2");
