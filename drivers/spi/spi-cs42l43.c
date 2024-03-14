// SPDX-License-Identifier: GPL-2.0
//
// CS42L43 SPI Controller Driver
//
// Copyright (C) 2022-2023 Cirrus Logic, Inc. and
//                         Cirrus Logic International Semiconductor Ltd.

#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/mfd/cs42l43.h>
#include <linux/mfd/cs42l43-regs.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>
#include <linux/units.h>

#define CS42L43_FIFO_SIZE		16
#define CS42L43_SPI_ROOT_HZ		(40 * HZ_PER_MHZ)
#define CS42L43_SPI_MAX_LENGTH		65532

enum cs42l43_spi_cmd {
	CS42L43_WRITE,
	CS42L43_READ
};

struct cs42l43_spi {
	struct device *dev;
	struct regmap *regmap;
	struct spi_controller *ctlr;
};

static const unsigned int cs42l43_clock_divs[] = {
	2, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30
};

static int cs42l43_spi_tx(struct regmap *regmap, const u8 *buf, unsigned int len)
{
	const u8 *end = buf + len;
	u32 val = 0;
	int ret;

	while (buf < end) {
		const u8 *block = min(buf + CS42L43_FIFO_SIZE, end);

		while (buf < block) {
			const u8 *word = min(buf + sizeof(u32), block);
			int pad = (buf + sizeof(u32)) - word;

			while (buf < word) {
				val >>= BITS_PER_BYTE;
				val |= FIELD_PREP(GENMASK(31, 24), *buf);

				buf++;
			}

			val >>= pad * BITS_PER_BYTE;

			regmap_write(regmap, CS42L43_TX_DATA, val);
		}

		regmap_write(regmap, CS42L43_TRAN_CONFIG8, CS42L43_SPI_TX_DONE_MASK);

		ret = regmap_read_poll_timeout(regmap, CS42L43_TRAN_STATUS1,
					       val, (val & CS42L43_SPI_TX_REQUEST_MASK),
					       1000, 5000);
		if (ret)
			return ret;
	}

	return 0;
}

static int cs42l43_spi_rx(struct regmap *regmap, u8 *buf, unsigned int len)
{
	u8 *end = buf + len;
	u32 val;
	int ret;

	while (buf < end) {
		u8 *block = min(buf + CS42L43_FIFO_SIZE, end);

		ret = regmap_read_poll_timeout(regmap, CS42L43_TRAN_STATUS1,
					       val, (val & CS42L43_SPI_RX_REQUEST_MASK),
					       1000, 5000);
		if (ret)
			return ret;

		while (buf < block) {
			u8 *word = min(buf + sizeof(u32), block);

			ret = regmap_read(regmap, CS42L43_RX_DATA, &val);
			if (ret)
				return ret;

			while (buf < word) {
				*buf = FIELD_GET(GENMASK(7, 0), val);

				val >>= BITS_PER_BYTE;
				buf++;
			}
		}

		regmap_write(regmap, CS42L43_TRAN_CONFIG8, CS42L43_SPI_RX_DONE_MASK);
	}

	return 0;
}

static int cs42l43_transfer_one(struct spi_controller *ctlr, struct spi_device *spi,
				struct spi_transfer *tfr)
{
	struct cs42l43_spi *priv = spi_controller_get_devdata(spi->controller);
	int i, ret = -EINVAL;

	for (i = 0; i < ARRAY_SIZE(cs42l43_clock_divs); i++) {
		if (CS42L43_SPI_ROOT_HZ / cs42l43_clock_divs[i] <= tfr->speed_hz)
			break;
	}

	if (i == ARRAY_SIZE(cs42l43_clock_divs))
		return -EINVAL;

	regmap_write(priv->regmap, CS42L43_SPI_CLK_CONFIG1, i);

	if (tfr->tx_buf) {
		regmap_write(priv->regmap, CS42L43_TRAN_CONFIG3, CS42L43_WRITE);
		regmap_write(priv->regmap, CS42L43_TRAN_CONFIG4, tfr->len - 1);
	} else if (tfr->rx_buf) {
		regmap_write(priv->regmap, CS42L43_TRAN_CONFIG3, CS42L43_READ);
		regmap_write(priv->regmap, CS42L43_TRAN_CONFIG5, tfr->len - 1);
	}

	regmap_write(priv->regmap, CS42L43_TRAN_CONFIG1, CS42L43_SPI_START_MASK);

	if (tfr->tx_buf)
		ret = cs42l43_spi_tx(priv->regmap, (const u8 *)tfr->tx_buf, tfr->len);
	else if (tfr->rx_buf)
		ret = cs42l43_spi_rx(priv->regmap, (u8 *)tfr->rx_buf, tfr->len);

	return ret;
}

static void cs42l43_set_cs(struct spi_device *spi, bool is_high)
{
	struct cs42l43_spi *priv = spi_controller_get_devdata(spi->controller);

	if (spi_get_chipselect(spi, 0) == 0)
		regmap_write(priv->regmap, CS42L43_SPI_CONFIG2, !is_high);
}

static int cs42l43_prepare_message(struct spi_controller *ctlr, struct spi_message *msg)
{
	struct cs42l43_spi *priv = spi_controller_get_devdata(ctlr);
	struct spi_device *spi = msg->spi;
	unsigned int spi_config1 = 0;

	/* select another internal CS, which doesn't exist, so CS 0 is not used */
	if (spi_get_csgpiod(spi, 0))
		spi_config1 |= 1 << CS42L43_SPI_SS_SEL_SHIFT;
	if (spi->mode & SPI_CPOL)
		spi_config1 |= CS42L43_SPI_CPOL_MASK;
	if (spi->mode & SPI_CPHA)
		spi_config1 |= CS42L43_SPI_CPHA_MASK;
	if (spi->mode & SPI_3WIRE)
		spi_config1 |= CS42L43_SPI_THREE_WIRE_MASK;

	regmap_write(priv->regmap, CS42L43_SPI_CONFIG1, spi_config1);

	return 0;
}

static int cs42l43_prepare_transfer_hardware(struct spi_controller *ctlr)
{
	struct cs42l43_spi *priv = spi_controller_get_devdata(ctlr);
	int ret;

	ret = regmap_write(priv->regmap, CS42L43_BLOCK_EN2, CS42L43_SPI_MSTR_EN_MASK);
	if (ret)
		dev_err(priv->dev, "Failed to enable SPI controller: %d\n", ret);

	return ret;
}

static int cs42l43_unprepare_transfer_hardware(struct spi_controller *ctlr)
{
	struct cs42l43_spi *priv = spi_controller_get_devdata(ctlr);
	int ret;

	ret = regmap_write(priv->regmap, CS42L43_BLOCK_EN2, 0);
	if (ret)
		dev_err(priv->dev, "Failed to disable SPI controller: %d\n", ret);

	return ret;
}

static size_t cs42l43_spi_max_length(struct spi_device *spi)
{
	return CS42L43_SPI_MAX_LENGTH;
}

static int cs42l43_spi_probe(struct platform_device *pdev)
{
	struct cs42l43 *cs42l43 = dev_get_drvdata(pdev->dev.parent);
	struct cs42l43_spi *priv;
	struct fwnode_handle *fwnode = dev_fwnode(cs42l43->dev);
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->ctlr = devm_spi_alloc_master(&pdev->dev, sizeof(*priv->ctlr));
	if (!priv->ctlr)
		return -ENOMEM;

	spi_controller_set_devdata(priv->ctlr, priv);

	priv->dev = &pdev->dev;
	priv->regmap = cs42l43->regmap;

	priv->ctlr->prepare_message = cs42l43_prepare_message;
	priv->ctlr->prepare_transfer_hardware = cs42l43_prepare_transfer_hardware;
	priv->ctlr->unprepare_transfer_hardware = cs42l43_unprepare_transfer_hardware;
	priv->ctlr->transfer_one = cs42l43_transfer_one;
	priv->ctlr->set_cs = cs42l43_set_cs;
	priv->ctlr->max_transfer_size = cs42l43_spi_max_length;

	if (is_of_node(fwnode))
		fwnode = fwnode_get_named_child_node(fwnode, "spi");

	device_set_node(&priv->ctlr->dev, fwnode);

	priv->ctlr->mode_bits = SPI_3WIRE | SPI_MODE_X_MASK;
	priv->ctlr->flags = SPI_CONTROLLER_HALF_DUPLEX;
	priv->ctlr->bits_per_word_mask = SPI_BPW_MASK(8) | SPI_BPW_MASK(16) |
					 SPI_BPW_MASK(32);
	priv->ctlr->min_speed_hz = CS42L43_SPI_ROOT_HZ /
				   cs42l43_clock_divs[ARRAY_SIZE(cs42l43_clock_divs) - 1];
	priv->ctlr->max_speed_hz = CS42L43_SPI_ROOT_HZ / cs42l43_clock_divs[0];
	priv->ctlr->use_gpio_descriptors = true;
	priv->ctlr->auto_runtime_pm = true;

	ret = devm_pm_runtime_enable(priv->dev);
	if (ret)
		return ret;

	pm_runtime_idle(priv->dev);

	regmap_write(priv->regmap, CS42L43_TRAN_CONFIG6, CS42L43_FIFO_SIZE - 1);
	regmap_write(priv->regmap, CS42L43_TRAN_CONFIG7, CS42L43_FIFO_SIZE - 1);

	// Disable Watchdog timer and enable stall
	regmap_write(priv->regmap, CS42L43_SPI_CONFIG3, 0);
	regmap_write(priv->regmap, CS42L43_SPI_CONFIG4, CS42L43_SPI_STALL_ENA_MASK);

	ret = devm_spi_register_controller(priv->dev, priv->ctlr);
	if (ret) {
		dev_err(priv->dev, "Failed to register SPI controller: %d\n", ret);
	}

	return ret;
}

static const struct platform_device_id cs42l43_spi_id_table[] = {
	{ "cs42l43-spi", },
	{}
};
MODULE_DEVICE_TABLE(platform, cs42l43_spi_id_table);

static struct platform_driver cs42l43_spi_driver = {
	.driver = {
		.name	= "cs42l43-spi",
	},
	.probe		= cs42l43_spi_probe,
	.id_table	= cs42l43_spi_id_table,
};
module_platform_driver(cs42l43_spi_driver);

MODULE_DESCRIPTION("CS42L43 SPI Driver");
MODULE_AUTHOR("Lucas Tanure <tanureal@opensource.cirrus.com>");
MODULE_AUTHOR("Maciej Strozek <mstrozek@opensource.cirrus.com>");
MODULE_LICENSE("GPL");
