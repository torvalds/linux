// SPDX-License-Identifier: GPL-2.0+
/*
 * SPI driver for Renesas Synchronization Management Unit (SMU) devices.
 *
 * Copyright (C) 2021 Integrated Device Technology, Inc., a Renesas Company.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mfd/core.h>
#include <linux/mfd/rsmu.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>

#include "rsmu.h"

#define	RSMU_CM_PAGE_ADDR		0x7C
#define	RSMU_SABRE_PAGE_ADDR		0x7F
#define	RSMU_PAGE_MASK			0xFFFFFF80
#define	RSMU_ADDR_MASK			0x7F

static int rsmu_read_device(struct rsmu_ddata *rsmu, u8 reg, u8 *buf, u16 bytes)
{
	struct spi_device *client = to_spi_device(rsmu->dev);
	struct spi_transfer xfer = {0};
	struct spi_message msg;
	u8 cmd[RSMU_MAX_READ_COUNT + 1] = {0};
	u8 rsp[RSMU_MAX_READ_COUNT + 1] = {0};
	int ret;

	if (bytes > RSMU_MAX_READ_COUNT)
		return -EINVAL;

	cmd[0] = reg | 0x80;
	xfer.rx_buf = rsp;
	xfer.len = bytes + 1;
	xfer.tx_buf = cmd;
	xfer.bits_per_word = client->bits_per_word;
	xfer.speed_hz = client->max_speed_hz;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

	/*
	 * 4-wire SPI is a shift register, so for every byte you send,
	 * you get one back at the same time. Example read from 0xC024,
	 * which has value of 0x2D
	 *
	 * MOSI:
	 *       7C 00 C0 #Set page register
	 *       A4 00    #MSB is set, so this is read command
	 * MISO:
	 *       XX 2D    #XX is a dummy byte from sending A4 and we
	 *                 need to throw it away
	 */
	ret = spi_sync(client, &msg);
	if (ret >= 0)
		memcpy(buf, &rsp[1], xfer.len-1);

	return ret;
}

static int rsmu_write_device(struct rsmu_ddata *rsmu, u8 reg, u8 *buf, u16 bytes)
{
	struct spi_device *client = to_spi_device(rsmu->dev);
	struct spi_transfer xfer = {0};
	struct spi_message msg;
	u8 cmd[RSMU_MAX_WRITE_COUNT + 1] = {0};

	if (bytes > RSMU_MAX_WRITE_COUNT)
		return -EINVAL;

	cmd[0] = reg;
	memcpy(&cmd[1], buf, bytes);

	xfer.len = bytes + 1;
	xfer.tx_buf = cmd;
	xfer.bits_per_word = client->bits_per_word;
	xfer.speed_hz = client->max_speed_hz;
	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

	return  spi_sync(client, &msg);
}

/*
 * 1-byte (1B) offset addressing:
 * 16-bit register address: the lower 7 bits of the register address come
 * from the offset addr byte and the upper 9 bits come from the page register.
 */
static int rsmu_write_page_register(struct rsmu_ddata *rsmu, u32 reg)
{
	u8 page_reg;
	u8 buf[4];
	u16 bytes;
	u32 page;
	int err;

	switch (rsmu->type) {
	case RSMU_CM:
		/* Do not modify page register for none-scsr registers */
		if (reg < RSMU_CM_SCSR_BASE)
			return 0;
		page_reg = RSMU_CM_PAGE_ADDR;
		page = reg & RSMU_PAGE_MASK;
		buf[0] = (u8)(page & 0xff);
		buf[1] = (u8)((page >> 8) & 0xff);
		buf[2] = (u8)((page >> 16) & 0xff);
		buf[3] = (u8)((page >> 24) & 0xff);
		bytes = 4;
		break;
	case RSMU_SABRE:
		/* Do not modify page register if reg is page register itself */
		if ((reg & RSMU_ADDR_MASK) == RSMU_ADDR_MASK)
			return 0;
		page_reg = RSMU_SABRE_PAGE_ADDR;
		page = reg & RSMU_PAGE_MASK;
		/* The three page bits are located in the single Page Register */
		buf[0] = (u8)((page >> 7) & 0x7);
		bytes = 1;
		break;
	default:
		dev_err(rsmu->dev, "Unsupported RSMU device type: %d\n", rsmu->type);
		return -ENODEV;
	}

	/* Simply return if we are on the same page */
	if (rsmu->page == page)
		return 0;

	err = rsmu_write_device(rsmu, page_reg, buf, bytes);
	if (err)
		dev_err(rsmu->dev, "Failed to set page offset 0x%x\n", page);
	else
		/* Remember the last page */
		rsmu->page = page;

	return err;
}

static int rsmu_reg_read(void *context, unsigned int reg, unsigned int *val)
{
	struct rsmu_ddata *rsmu = spi_get_drvdata((struct spi_device *)context);
	u8 addr = (u8)(reg & RSMU_ADDR_MASK);
	int err;

	err = rsmu_write_page_register(rsmu, reg);
	if (err)
		return err;

	err = rsmu_read_device(rsmu, addr, (u8 *)val, 1);
	if (err)
		dev_err(rsmu->dev, "Failed to read offset address 0x%x\n", addr);

	return err;
}

static int rsmu_reg_write(void *context, unsigned int reg, unsigned int val)
{
	struct rsmu_ddata *rsmu = spi_get_drvdata((struct spi_device *)context);
	u8 addr = (u8)(reg & RSMU_ADDR_MASK);
	u8 data = (u8)val;
	int err;

	err = rsmu_write_page_register(rsmu, reg);
	if (err)
		return err;

	err = rsmu_write_device(rsmu, addr, &data, 1);
	if (err)
		dev_err(rsmu->dev,
			"Failed to write offset address 0x%x\n", addr);

	return err;
}

static const struct regmap_config rsmu_cm_regmap_config = {
	.reg_bits = 32,
	.val_bits = 8,
	.max_register = 0x20120000,
	.reg_read = rsmu_reg_read,
	.reg_write = rsmu_reg_write,
	.cache_type = REGCACHE_NONE,
};

static const struct regmap_config rsmu_sabre_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = 0x400,
	.reg_read = rsmu_reg_read,
	.reg_write = rsmu_reg_write,
	.cache_type = REGCACHE_NONE,
};

static int rsmu_spi_probe(struct spi_device *client)
{
	const struct spi_device_id *id = spi_get_device_id(client);
	const struct regmap_config *cfg;
	struct rsmu_ddata *rsmu;
	int ret;

	rsmu = devm_kzalloc(&client->dev, sizeof(*rsmu), GFP_KERNEL);
	if (!rsmu)
		return -ENOMEM;

	spi_set_drvdata(client, rsmu);

	rsmu->dev = &client->dev;
	rsmu->type = (enum rsmu_type)id->driver_data;

	/* Initialize regmap */
	switch (rsmu->type) {
	case RSMU_CM:
		cfg = &rsmu_cm_regmap_config;
		break;
	case RSMU_SABRE:
		cfg = &rsmu_sabre_regmap_config;
		break;
	default:
		dev_err(rsmu->dev, "Unsupported RSMU device type: %d\n", rsmu->type);
		return -ENODEV;
	}

	rsmu->regmap = devm_regmap_init(&client->dev, NULL, client, cfg);
	if (IS_ERR(rsmu->regmap)) {
		ret = PTR_ERR(rsmu->regmap);
		dev_err(rsmu->dev, "Failed to allocate register map: %d\n", ret);
		return ret;
	}

	return rsmu_core_init(rsmu);
}

static void rsmu_spi_remove(struct spi_device *client)
{
	struct rsmu_ddata *rsmu = spi_get_drvdata(client);

	rsmu_core_exit(rsmu);
}

static const struct spi_device_id rsmu_spi_id[] = {
	{ "8a34000",  RSMU_CM },
	{ "8a34001",  RSMU_CM },
	{ "82p33810", RSMU_SABRE },
	{ "82p33811", RSMU_SABRE },
	{}
};
MODULE_DEVICE_TABLE(spi, rsmu_spi_id);

static const struct of_device_id rsmu_spi_of_match[] = {
	{ .compatible = "idt,8a34000",  .data = (void *)RSMU_CM },
	{ .compatible = "idt,8a34001",  .data = (void *)RSMU_CM },
	{ .compatible = "idt,82p33810", .data = (void *)RSMU_SABRE },
	{ .compatible = "idt,82p33811", .data = (void *)RSMU_SABRE },
	{}
};
MODULE_DEVICE_TABLE(of, rsmu_spi_of_match);

static struct spi_driver rsmu_spi_driver = {
	.driver = {
		.name = "rsmu-spi",
		.of_match_table = of_match_ptr(rsmu_spi_of_match),
	},
	.probe = rsmu_spi_probe,
	.remove	= rsmu_spi_remove,
	.id_table = rsmu_spi_id,
};

static int __init rsmu_spi_init(void)
{
	return spi_register_driver(&rsmu_spi_driver);
}
subsys_initcall(rsmu_spi_init);

static void __exit rsmu_spi_exit(void)
{
	spi_unregister_driver(&rsmu_spi_driver);
}
module_exit(rsmu_spi_exit);

MODULE_DESCRIPTION("Renesas SMU SPI driver");
MODULE_LICENSE("GPL");
