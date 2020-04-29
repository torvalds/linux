// SPDX-License-Identifier: GPL-2.0
/* DSA driver for:
 * Vitesse VSC7385 SparX-G5 5+1-port Integrated Gigabit Ethernet Switch
 * Vitesse VSC7388 SparX-G8 8-port Integrated Gigabit Ethernet Switch
 * Vitesse VSC7395 SparX-G5e 5+1-port Integrated Gigabit Ethernet Switch
 * Vitesse VSC7398 SparX-G8e 8-port Integrated Gigabit Ethernet Switch
 *
 * This driver takes control of the switch chip over SPI and
 * configures it to route packages around when connected to a CPU port.
 *
 * Copyright (C) 2018 Linus Wallej <linus.walleij@linaro.org>
 * Includes portions of code from the firmware uploader by:
 * Copyright (C) 2009 Gabor Juhos <juhosg@openwrt.org>
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/spi/spi.h>

#include "vitesse-vsc73xx.h"

#define VSC73XX_CMD_SPI_MODE_READ		0
#define VSC73XX_CMD_SPI_MODE_WRITE		1
#define VSC73XX_CMD_SPI_MODE_SHIFT		4
#define VSC73XX_CMD_SPI_BLOCK_SHIFT		5
#define VSC73XX_CMD_SPI_BLOCK_MASK		0x7
#define VSC73XX_CMD_SPI_SUBBLOCK_MASK		0xf

/**
 * struct vsc73xx_spi - VSC73xx SPI state container
 */
struct vsc73xx_spi {
	struct spi_device	*spi;
	struct mutex		lock; /* Protects SPI traffic */
	struct vsc73xx		vsc;
};

static const struct vsc73xx_ops vsc73xx_spi_ops;

static u8 vsc73xx_make_addr(u8 mode, u8 block, u8 subblock)
{
	u8 ret;

	ret =
	    (block & VSC73XX_CMD_SPI_BLOCK_MASK) << VSC73XX_CMD_SPI_BLOCK_SHIFT;
	ret |= (mode & 1) << VSC73XX_CMD_SPI_MODE_SHIFT;
	ret |= subblock & VSC73XX_CMD_SPI_SUBBLOCK_MASK;

	return ret;
}

static int vsc73xx_spi_read(struct vsc73xx *vsc, u8 block, u8 subblock, u8 reg,
			    u32 *val)
{
	struct vsc73xx_spi *vsc_spi = vsc->priv;
	struct spi_transfer t[2];
	struct spi_message m;
	u8 cmd[4];
	u8 buf[4];
	int ret;

	if (!vsc73xx_is_addr_valid(block, subblock))
		return -EINVAL;

	spi_message_init(&m);

	memset(&t, 0, sizeof(t));

	t[0].tx_buf = cmd;
	t[0].len = sizeof(cmd);
	spi_message_add_tail(&t[0], &m);

	t[1].rx_buf = buf;
	t[1].len = sizeof(buf);
	spi_message_add_tail(&t[1], &m);

	cmd[0] = vsc73xx_make_addr(VSC73XX_CMD_SPI_MODE_READ, block, subblock);
	cmd[1] = reg;
	cmd[2] = 0;
	cmd[3] = 0;

	mutex_lock(&vsc_spi->lock);
	ret = spi_sync(vsc_spi->spi, &m);
	mutex_unlock(&vsc_spi->lock);

	if (ret)
		return ret;

	*val = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];

	return 0;
}

static int vsc73xx_spi_write(struct vsc73xx *vsc, u8 block, u8 subblock, u8 reg,
			     u32 val)
{
	struct vsc73xx_spi *vsc_spi = vsc->priv;
	struct spi_transfer t[2];
	struct spi_message m;
	u8 cmd[2];
	u8 buf[4];
	int ret;

	if (!vsc73xx_is_addr_valid(block, subblock))
		return -EINVAL;

	spi_message_init(&m);

	memset(&t, 0, sizeof(t));

	t[0].tx_buf = cmd;
	t[0].len = sizeof(cmd);
	spi_message_add_tail(&t[0], &m);

	t[1].tx_buf = buf;
	t[1].len = sizeof(buf);
	spi_message_add_tail(&t[1], &m);

	cmd[0] = vsc73xx_make_addr(VSC73XX_CMD_SPI_MODE_WRITE, block, subblock);
	cmd[1] = reg;

	buf[0] = (val >> 24) & 0xff;
	buf[1] = (val >> 16) & 0xff;
	buf[2] = (val >> 8) & 0xff;
	buf[3] = val & 0xff;

	mutex_lock(&vsc_spi->lock);
	ret = spi_sync(vsc_spi->spi, &m);
	mutex_unlock(&vsc_spi->lock);

	return ret;
}

static int vsc73xx_spi_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct vsc73xx_spi *vsc_spi;
	int ret;

	vsc_spi = devm_kzalloc(dev, sizeof(*vsc_spi), GFP_KERNEL);
	if (!vsc_spi)
		return -ENOMEM;

	spi_set_drvdata(spi, vsc_spi);
	vsc_spi->spi = spi_dev_get(spi);
	vsc_spi->vsc.dev = dev;
	vsc_spi->vsc.priv = vsc_spi;
	vsc_spi->vsc.ops = &vsc73xx_spi_ops;
	mutex_init(&vsc_spi->lock);

	spi->mode = SPI_MODE_0;
	spi->bits_per_word = 8;
	ret = spi_setup(spi);
	if (ret < 0) {
		dev_err(dev, "spi setup failed.\n");
		return ret;
	}

	return vsc73xx_probe(&vsc_spi->vsc);
}

static int vsc73xx_spi_remove(struct spi_device *spi)
{
	struct vsc73xx_spi *vsc_spi = spi_get_drvdata(spi);

	return vsc73xx_remove(&vsc_spi->vsc);
}

static const struct vsc73xx_ops vsc73xx_spi_ops = {
	.read = vsc73xx_spi_read,
	.write = vsc73xx_spi_write,
};

static const struct of_device_id vsc73xx_of_match[] = {
	{
		.compatible = "vitesse,vsc7385",
	},
	{
		.compatible = "vitesse,vsc7388",
	},
	{
		.compatible = "vitesse,vsc7395",
	},
	{
		.compatible = "vitesse,vsc7398",
	},
	{ },
};
MODULE_DEVICE_TABLE(of, vsc73xx_of_match);

static struct spi_driver vsc73xx_spi_driver = {
	.probe = vsc73xx_spi_probe,
	.remove = vsc73xx_spi_remove,
	.driver = {
		.name = "vsc73xx-spi",
		.of_match_table = vsc73xx_of_match,
	},
};
module_spi_driver(vsc73xx_spi_driver);

MODULE_AUTHOR("Linus Walleij <linus.walleij@linaro.org>");
MODULE_DESCRIPTION("Vitesse VSC7385/7388/7395/7398 SPI driver");
MODULE_LICENSE("GPL v2");
