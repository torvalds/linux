// SPDX-License-Identifier: GPL-2.0

/*
 * The OCOTP driver for Sunplus	SP7021
 *
 * Copyright (C) 2019 Sunplus Technology Inc., All rights reserved.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/nvmem-provider.h>
#include <linux/platform_device.h>

/*
 * OTP memory
 * Each bank contains 4 words (32 bits).
 * Bank 0 starts at offset 0 from the base.
 */

#define OTP_WORDS_PER_BANK		4
#define OTP_WORD_SIZE			sizeof(u32)
#define OTP_BIT_ADDR_OF_BANK		(8 * OTP_WORD_SIZE * OTP_WORDS_PER_BANK)
#define QAC628_OTP_NUM_BANKS		8
#define QAC628_OTP_SIZE			(QAC628_OTP_NUM_BANKS * OTP_WORDS_PER_BANK * OTP_WORD_SIZE)
#define OTP_READ_TIMEOUT_US		200000

/* HB_GPIO */
#define ADDRESS_8_DATA			0x20

/* OTP_RX */
#define OTP_CONTROL_2			0x48
#define OTP_RD_PERIOD			GENMASK(15, 8)
#define OTP_RD_PERIOD_MASK		~GENMASK(15, 8)
#define CPU_CLOCK			FIELD_PREP(OTP_RD_PERIOD, 30)
#define SEL_BAK_KEY2			BIT(5)
#define SEL_BAK_KEY2_MASK		~BIT(5)
#define SW_TRIM_EN			BIT(4)
#define SW_TRIM_EN_MASK			~BIT(4)
#define SEL_BAK_KEY			BIT(3)
#define SEL_BAK_KEY_MASK		~BIT(3)
#define OTP_READ			BIT(2)
#define OTP_LOAD_SECURE_DATA		BIT(1)
#define OTP_LOAD_SECURE_DATA_MASK	~BIT(1)
#define OTP_DO_CRC			BIT(0)
#define OTP_DO_CRC_MASK			~BIT(0)
#define OTP_STATUS			0x4c
#define OTP_READ_DONE			BIT(4)
#define OTP_READ_DONE_MASK		~BIT(4)
#define OTP_LOAD_SECURE_DONE_MASK	~BIT(2)
#define OTP_READ_ADDRESS		0x50

enum base_type {
	HB_GPIO,
	OTPRX,
	BASEMAX,
};

struct sp_ocotp_priv {
	struct device *dev;
	void __iomem *base[BASEMAX];
	struct clk *clk;
};

struct sp_ocotp_data {
	int size;
};

static const struct sp_ocotp_data sp_otp_v0 = {
	.size = QAC628_OTP_SIZE,
};

static int sp_otp_read_real(struct sp_ocotp_priv *otp, int addr, char *value)
{
	unsigned int addr_data;
	unsigned int byte_shift;
	unsigned int status;
	int ret;

	addr_data = addr % (OTP_WORD_SIZE * OTP_WORDS_PER_BANK);
	addr_data = addr_data / OTP_WORD_SIZE;

	byte_shift = addr % (OTP_WORD_SIZE * OTP_WORDS_PER_BANK);
	byte_shift = byte_shift % OTP_WORD_SIZE;

	addr = addr / (OTP_WORD_SIZE * OTP_WORDS_PER_BANK);
	addr = addr * OTP_BIT_ADDR_OF_BANK;

	writel(readl(otp->base[OTPRX] + OTP_STATUS) & OTP_READ_DONE_MASK &
	       OTP_LOAD_SECURE_DONE_MASK, otp->base[OTPRX] + OTP_STATUS);
	writel(addr, otp->base[OTPRX] + OTP_READ_ADDRESS);
	writel(readl(otp->base[OTPRX] + OTP_CONTROL_2) | OTP_READ,
	       otp->base[OTPRX] + OTP_CONTROL_2);
	writel(readl(otp->base[OTPRX] + OTP_CONTROL_2) & SEL_BAK_KEY2_MASK & SW_TRIM_EN_MASK
	       & SEL_BAK_KEY_MASK & OTP_LOAD_SECURE_DATA_MASK & OTP_DO_CRC_MASK,
	       otp->base[OTPRX] + OTP_CONTROL_2);
	writel((readl(otp->base[OTPRX] + OTP_CONTROL_2) & OTP_RD_PERIOD_MASK) | CPU_CLOCK,
	       otp->base[OTPRX] + OTP_CONTROL_2);

	ret = readl_poll_timeout(otp->base[OTPRX] + OTP_STATUS, status,
				 status & OTP_READ_DONE, 10, OTP_READ_TIMEOUT_US);

	if (ret < 0)
		return ret;

	*value = (readl(otp->base[HB_GPIO] + ADDRESS_8_DATA + addr_data * OTP_WORD_SIZE)
		  >> (8 * byte_shift)) & 0xff;

	return ret;
}

static int sp_ocotp_read(void *priv, unsigned int offset, void *value, size_t bytes)
{
	struct sp_ocotp_priv *otp = priv;
	unsigned int addr;
	char *buf = value;
	char val[4];
	int ret;

	ret = clk_enable(otp->clk);
	if (ret)
		return ret;

	*buf = 0;
	for (addr = offset; addr < (offset + bytes); addr++) {
		ret = sp_otp_read_real(otp, addr, val);
		if (ret < 0) {
			dev_err(otp->dev, "OTP read fail:%d at %d", ret, addr);
			goto disable_clk;
		}

		*buf++ = *val;
	}

disable_clk:
	clk_disable(otp->clk);

	return ret;
}

static struct nvmem_config sp_ocotp_nvmem_config = {
	.name = "sp-ocotp",
	.add_legacy_fixed_of_cells = true,
	.read_only = true,
	.word_size = 1,
	.size = QAC628_OTP_SIZE,
	.stride = 1,
	.reg_read = sp_ocotp_read,
	.owner = THIS_MODULE,
};

static int sp_ocotp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct nvmem_device *nvmem;
	struct sp_ocotp_priv *otp;
	int ret;

	otp = devm_kzalloc(dev, sizeof(*otp), GFP_KERNEL);
	if (!otp)
		return -ENOMEM;

	otp->dev = dev;

	otp->base[HB_GPIO] = devm_platform_ioremap_resource_byname(pdev, "hb_gpio");
	if (IS_ERR(otp->base[HB_GPIO]))
		return PTR_ERR(otp->base[HB_GPIO]);

	otp->base[OTPRX] = devm_platform_ioremap_resource_byname(pdev, "otprx");
	if (IS_ERR(otp->base[OTPRX]))
		return PTR_ERR(otp->base[OTPRX]);

	otp->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(otp->clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(otp->clk),
						"devm_clk_get fail\n");

	ret = clk_prepare(otp->clk);
	if (ret < 0) {
		dev_err(dev, "failed to prepare clk: %d\n", ret);
		return ret;
	}

	sp_ocotp_nvmem_config.priv = otp;
	sp_ocotp_nvmem_config.dev = dev;

	nvmem = devm_nvmem_register(dev, &sp_ocotp_nvmem_config);
	if (IS_ERR(nvmem)) {
		ret = dev_err_probe(&pdev->dev, PTR_ERR(nvmem),
						"register nvmem device fail\n");
		goto err;
	}

	platform_set_drvdata(pdev, nvmem);

	dev_dbg(dev, "banks:%d x wpb:%d x wsize:%d = %d",
		(int)QAC628_OTP_NUM_BANKS, (int)OTP_WORDS_PER_BANK,
		(int)OTP_WORD_SIZE, (int)QAC628_OTP_SIZE);

	return 0;
err:
	clk_unprepare(otp->clk);
	return ret;
}

static const struct of_device_id sp_ocotp_dt_ids[] = {
	{ .compatible = "sunplus,sp7021-ocotp", .data = &sp_otp_v0 },
	{ }
};
MODULE_DEVICE_TABLE(of, sp_ocotp_dt_ids);

static struct platform_driver sp_otp_driver = {
	.probe     = sp_ocotp_probe,
	.driver    = {
		.name           = "sunplus,sp7021-ocotp",
		.of_match_table = sp_ocotp_dt_ids,
	}
};
module_platform_driver(sp_otp_driver);

MODULE_AUTHOR("Vincent Shih <vincent.sunplus@gmail.com>");
MODULE_DESCRIPTION("Sunplus On-Chip OTP driver");
MODULE_LICENSE("GPL");

