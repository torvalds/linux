// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Algea Cao <algea.cao@rock-chips.com>
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/mfd/rk630.h>
#include <linux/spi/spi.h>

#define RK630_CMD_WRITE			0x00000011
#define RK630_CMD_WRITE_REG0		0x00010011
#define RK630_CMD_WRITE_REG1		0x00020011
#define RK630_CMD_WRITE_CTRL0		0x00030011
#define RK630_CMD_READ			0x00000077
#define RK630_CMD_READ_BEGIN		0x000000AA
#define RK630_CMD_QUERY			0x000000FF
#define RK630_CMD_QUERY_REG2		0x000001FF
#define RK630_CMD_QUICK_WRITE		0x00030011
#define RK630_OP_STATE_ID_MASK		(0xffff0000)
#define RK630_OP_STATE_ID		(0X16080000)
#define RK630_OP_STATE_MASK		(0x0000ffff)
#define RK630_OP_STATE_WRITE_ERROR	(0x01 << 0)
#define RK630_OP_STATE_WRITE_OVERFLOW	(0x01 << 1)
#define RK630_OP_STATE_WRITE_UNFINISHED	(0x01 << 2)
#define RK630_OP_STATE_READ_ERROR	(0x01 << 8)
#define RK630_OP_STATE_READ_UNDERFLOW	(0x01 << 9)
#define RK630_OP_STATE_PRE_READ_ERROR	(0x01 << 10)
#define RK630_MAX_OP_BYTES		(60000)

static int rk630_spi_ctrl_init(struct spi_device *spi)
{
	u32 write_cmd = RK630_CMD_WRITE_CTRL0;
	u32 buf = 0x00000008;
	struct spi_transfer write_cmd_packet = {
		.tx_buf = &write_cmd,
		.len	= 4,
	};
	struct spi_transfer data_packet = {
		.tx_buf = &buf,
		.len	= 4,
	};
	struct spi_message m;

	spi_message_init(&m);
	spi_message_add_tail(&write_cmd_packet, &m);
	spi_message_add_tail(&data_packet, &m);
	return spi_sync(spi, &m);
}

static int rk630_spi_write(struct spi_device *spi,
			   u32 addr, const u32 *data, size_t data_len)
{
	int ret = 0;
	u32 write_cmd = RK630_CMD_WRITE;

	struct spi_transfer write_cmd_packet = {
		.tx_buf = &write_cmd,
		.len    = sizeof(write_cmd),
	};
	struct spi_transfer addr_packet = {
		.tx_buf = &addr,
		.len    = sizeof(addr),
	};
	struct spi_transfer data_packet = {
		.tx_buf = data,
		.len    = data_len,
	};
	struct spi_message m;

	spi_message_init(&m);
	spi_message_add_tail(&write_cmd_packet, &m);
	spi_message_add_tail(&addr_packet, &m);
	spi_message_add_tail(&data_packet, &m);
	ret = spi_sync(spi, &m);

	return ret;
}

static int rk630_regmap_write(void *context, const void *data, size_t count)
{
	struct device *dev = context;
	struct spi_device *spi = to_spi_device(dev);
	u32 buf[count];

	if (count < 8) {
		dev_err(&spi->dev, "regmap write err!\n");
		return -EINVAL;
	}

	memcpy(buf, data, count);

	return rk630_spi_write(spi, buf[0], &buf[1], (count - 4));
}

static int rk630_spi_read(struct spi_device *spi,
		   u32 addr, u32 *data, size_t data_len)
{
	int ret;
	u32 read_cmd = RK630_CMD_READ | (1 << 16);
	u32 read_begin_cmd = RK630_CMD_READ_BEGIN;
	u32 dummy = 0;
	struct spi_transfer read_cmd_packet = {
		.tx_buf = &read_cmd,
		.len    = sizeof(read_cmd),
	};
	struct spi_transfer addr_packet = {
		.tx_buf = &addr,
		.len    = sizeof(addr),
	};
	struct spi_transfer read_dummy_packet = {
		.tx_buf = &dummy,
		.len    = sizeof(dummy),
	};
	struct spi_transfer read_begin_cmd_packet = {
		.tx_buf = &read_begin_cmd,
		.len    = sizeof(read_begin_cmd),
	};
	struct spi_transfer data_packet = {
		.rx_buf = data,
		.len    = data_len,
	};
	struct spi_message m;

	spi_message_init(&m);
	spi_message_add_tail(&read_cmd_packet, &m);
	spi_message_add_tail(&addr_packet, &m);
	spi_message_add_tail(&read_dummy_packet, &m);
	spi_message_add_tail(&read_begin_cmd_packet, &m);
	spi_message_add_tail(&data_packet, &m);
	ret = spi_sync(spi, &m);

	return ret;
}

static int rk630_regmap_read(void *context,
			     const void *reg, size_t reg_size,
			     void *val, size_t val_size)
{
	struct device *dev = context;
	struct spi_device *spi = to_spi_device(dev);
	u32 rx_buf[2] = { 0 };
	int ret;

	if (reg_size != sizeof(u32) || val_size != sizeof(u32))
		return -EINVAL;

	/* Copy address to read from into first element of SPI buffer. */
	memcpy(rx_buf, reg, sizeof(u32));
	ret = rk630_spi_read(spi, rx_buf[0], &rx_buf[1], val_size);
	if (ret < 0) {
		dev_err(&spi->dev, "rk630 spi read err\n");
		return ret;
	}

	memcpy(val, &rx_buf[1], val_size);
	return 0;
}

static struct regmap_bus rk630_regmap = {
	.write = rk630_regmap_write,
	.read = rk630_regmap_read,
	.reg_format_endian_default = REGMAP_ENDIAN_NATIVE,
	.val_format_endian_default = REGMAP_ENDIAN_NATIVE,
};

static int
rk630_spi_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct rk630 *rk630;
	int ret;

	spi->bits_per_word = 8;
	ret = spi_setup(spi);
	if (ret < 0)
		return ret;

	rk630 = devm_kzalloc(dev, sizeof(*rk630), GFP_KERNEL);
	if (!rk630)
		return -ENOMEM;

	rk630->dev = dev;
	spi_set_drvdata(spi, rk630);

	rk630->grf = devm_regmap_init(&spi->dev, &rk630_regmap,
				      &spi->dev, &rk630_grf_regmap_config);
	if (IS_ERR(rk630->grf)) {
		ret = PTR_ERR(rk630->grf);
		dev_err(dev, "failed to allocate grf register map: %d\n", ret);
		return ret;
	}

	rk630->cru = devm_regmap_init(&spi->dev, &rk630_regmap,
				      &spi->dev, &rk630_cru_regmap_config);
	if (IS_ERR(rk630->cru)) {
		ret = PTR_ERR(rk630->cru);
		dev_err(dev, "failed to allocate cru register map: %d\n", ret);
		return ret;
	}

	rk630->tve = devm_regmap_init(&spi->dev, &rk630_regmap,
				      &spi->dev, &rk630_tve_regmap_config);
	if (IS_ERR(rk630->tve)) {
		ret = PTR_ERR(rk630->tve);
		dev_err(rk630->dev, "Failed to initialize tve regmap: %d\n",
			ret);
		return ret;
	}

	ret = rk630_core_probe(rk630);
	if (ret)
		return ret;

	rk630_spi_ctrl_init(spi);

	return 0;
}

static const struct of_device_id rk630_spi_of_match[] = {
	{ .compatible = "rockchip,rk630", },
	{}
};
MODULE_DEVICE_TABLE(of, rk630_spi_of_match);

static const struct spi_device_id rk630_spi_id[] = {
	{ "rk630", 0 },
	{}
};
MODULE_DEVICE_TABLE(spi, rk630_spi_id);

static struct spi_driver rk630_spi_driver = {
	.driver = {
		.name = "rk630",
		.of_match_table = of_match_ptr(rk630_spi_of_match),
	},
	.probe = rk630_spi_probe,
	.id_table = rk630_spi_id,
};
module_spi_driver(rk630_spi_driver);

MODULE_AUTHOR("Algea Cao <Algea.cao@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip rk630 MFD SPI driver");
MODULE_LICENSE("GPL v2");
