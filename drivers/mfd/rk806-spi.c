// SPDX-License-Identifier: GPL-2.0
/*
 * SPI access driver for rockchip rk806
 *
 * Copyright (c) 2021 Rockchip Electronics Co., Ltd.
 *
 * Author: Xu Shengfei <xsf@rock-chips.com>
 */

#include <linux/mfd/rk806.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>

static const struct of_device_id rk806_spi_of_match_table[] = {
	{ .compatible = "rockchip,rk806", },
	{ }
};
MODULE_DEVICE_TABLE(of, rk806_spi_of_match_table);

static int rk806_spi_write(struct spi_device *spi,
			   char addr,
			   const char *data,
			   size_t data_len)
{
	char write_cmd = RK806_CMD_WRITE | (data_len - 1);
	char addrh = RK806_REG_H;
	struct spi_message m;
	int buffer, ret = 0;
	struct spi_transfer write_cmd_packet = {
		.tx_buf	= &buffer,
		.len	= sizeof(buffer),
	};

	buffer = write_cmd | (addr << 8) | (addrh << 16) | (*data << 24);

	spi_message_init(&m);
	spi_message_add_tail(&write_cmd_packet, &m);
	ret = spi_sync(spi, &m);
	return ret;
}

static int rk806_spi_bus_write(void *context, const void *data, size_t count)
{
	struct device *dev = context;
	struct spi_device *spi = to_spi_device(dev);
	char buf[2];

	if (count < 2) {
		dev_err(&spi->dev, "regmap write err!\n");
		return -EINVAL;
	}
	memcpy(buf, data, 2);

	return rk806_spi_write(spi, buf[0], &buf[1], (count - 1));
}

static int rk806_spi_bus_read(void *context,
			      const void *reg,
			      size_t reg_size,
			      void *val,
			      size_t val_size)
{
	struct device *dev = context;
	struct spi_device *spi = to_spi_device(dev);
	char addr;
	char txbuf[3] = { 0 };

	if (reg_size != sizeof(char) || val_size < 1)
		return -EINVAL;

	/* Copy address to read from into first element of SPI buffer. */
	memcpy(&addr, reg, sizeof(char));

	txbuf[0] = RK806_CMD_READ | (val_size - 1);
	txbuf[1] = addr;
	txbuf[2] = RK806_REG_H;

	return spi_write_then_read(spi, txbuf, 3, val, val_size);
}

static const struct regmap_bus rk806_regmap_bus_spi = {
	.write = rk806_spi_bus_write,
	.read = rk806_spi_bus_read,
	.reg_format_endian_default = REGMAP_ENDIAN_NATIVE,
	.val_format_endian_default = REGMAP_ENDIAN_NATIVE,
};

static int rk806_spi_probe(struct spi_device *spi)
{
	struct rk806 *rk806;

	rk806 = devm_kzalloc(&spi->dev, sizeof(*rk806), GFP_KERNEL);
	if (!rk806)
		return -ENOMEM;

	spi_set_drvdata(spi, rk806);
	rk806->dev = &spi->dev;
	rk806->irq = spi->irq;

	rk806->regmap = devm_regmap_init(&spi->dev,
					 &rk806_regmap_bus_spi,
					 &spi->dev,
					 &rk806_regmap_config_spi);
	if (IS_ERR(rk806->regmap)) {
		dev_err(rk806->dev, "Failed to initialize register map\n");
		return PTR_ERR(rk806->regmap);
	}

	return rk806_device_init(rk806);
}

static int rk806_spi_remove(struct spi_device *spi)
{
	struct rk806 *rk806 = spi_get_drvdata(spi);

	return rk806_device_exit(rk806);
}

static const struct spi_device_id rk806_spi_id_table[] = {
	{ "rk806", 0 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(spi, rk806_spi_id_table);

static struct spi_driver rk806_spi_driver = {
	.driver		= {
		.name	= "rk806",
		.owner = THIS_MODULE,
		.of_match_table = rk806_spi_of_match_table,
	},
	.probe		= rk806_spi_probe,
	.remove		= rk806_spi_remove,
	.id_table	= rk806_spi_id_table,
};
module_spi_driver(rk806_spi_driver);

MODULE_AUTHOR("Xu Shengfei <xsf@rock-chips.com>");
MODULE_DESCRIPTION("RK806 SPI Interface Driver");
MODULE_LICENSE("GPL v2");
