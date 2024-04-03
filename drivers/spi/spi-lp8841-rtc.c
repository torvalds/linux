// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SPI host driver for ICP DAS LP-8841 RTC
 *
 * Copyright (C) 2016 Sergei Ianovich
 *
 * based on
 *
 * Dallas DS1302 RTC Support
 * Copyright (C) 2002 David McCullough
 * Copyright (C) 2003 - 2007 Paul Mundt
 */
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/spi/spi.h>

#define DRIVER_NAME	"spi_lp8841_rtc"

#define SPI_LP8841_RTC_CE	0x01
#define SPI_LP8841_RTC_CLK	0x02
#define SPI_LP8841_RTC_nWE	0x04
#define SPI_LP8841_RTC_MOSI	0x08
#define SPI_LP8841_RTC_MISO	0x01

/*
 * REVISIT If there is support for SPI_3WIRE and SPI_LSB_FIRST in SPI
 * GPIO driver, this SPI driver can be replaced by a simple GPIO driver
 * providing 3 GPIO pins.
 */

struct spi_lp8841_rtc {
	void		*iomem;
	unsigned long	state;
};

static inline void
setsck(struct spi_lp8841_rtc *data, int is_on)
{
	if (is_on)
		data->state |= SPI_LP8841_RTC_CLK;
	else
		data->state &= ~SPI_LP8841_RTC_CLK;
	writeb(data->state, data->iomem);
}

static inline void
setmosi(struct spi_lp8841_rtc *data, int is_on)
{
	if (is_on)
		data->state |= SPI_LP8841_RTC_MOSI;
	else
		data->state &= ~SPI_LP8841_RTC_MOSI;
	writeb(data->state, data->iomem);
}

static inline int
getmiso(struct spi_lp8841_rtc *data)
{
	return ioread8(data->iomem) & SPI_LP8841_RTC_MISO;
}

static inline u32
bitbang_txrx_be_cpha0_lsb(struct spi_lp8841_rtc *data,
		unsigned usecs, unsigned cpol, unsigned flags,
		u32 word, u8 bits)
{
	/* if (cpol == 0) this is SPI_MODE_0; else this is SPI_MODE_2 */

	u32 shift = 32 - bits;
	/* clock starts at inactive polarity */
	for (; likely(bits); bits--) {

		/* setup LSB (to target) on leading edge */
		if ((flags & SPI_CONTROLLER_NO_TX) == 0)
			setmosi(data, (word & 1));

		usleep_range(usecs, usecs + 1);	/* T(setup) */

		/* sample LSB (from target) on trailing edge */
		word >>= 1;
		if ((flags & SPI_CONTROLLER_NO_RX) == 0)
			word |= (getmiso(data) << 31);

		setsck(data, !cpol);
		usleep_range(usecs, usecs + 1);

		setsck(data, cpol);
	}

	word >>= shift;
	return word;
}

static int
spi_lp8841_rtc_transfer_one(struct spi_controller *host,
			    struct spi_device *spi,
			    struct spi_transfer *t)
{
	struct spi_lp8841_rtc	*data = spi_controller_get_devdata(host);
	unsigned		count = t->len;
	const u8		*tx = t->tx_buf;
	u8			*rx = t->rx_buf;
	u8			word = 0;
	int			ret = 0;

	if (tx) {
		data->state &= ~SPI_LP8841_RTC_nWE;
		writeb(data->state, data->iomem);
		while (likely(count > 0)) {
			word = *tx++;
			bitbang_txrx_be_cpha0_lsb(data, 1, 0,
					SPI_CONTROLLER_NO_RX, word, 8);
			count--;
		}
	} else if (rx) {
		data->state |= SPI_LP8841_RTC_nWE;
		writeb(data->state, data->iomem);
		while (likely(count > 0)) {
			word = bitbang_txrx_be_cpha0_lsb(data, 1, 0,
					SPI_CONTROLLER_NO_TX, word, 8);
			*rx++ = word;
			count--;
		}
	} else {
		ret = -EINVAL;
	}

	spi_finalize_current_transfer(host);

	return ret;
}

static void
spi_lp8841_rtc_set_cs(struct spi_device *spi, bool enable)
{
	struct spi_lp8841_rtc *data = spi_controller_get_devdata(spi->controller);

	data->state = 0;
	writeb(data->state, data->iomem);
	if (enable) {
		usleep_range(4, 5);
		data->state |= SPI_LP8841_RTC_CE;
		writeb(data->state, data->iomem);
		usleep_range(4, 5);
	}
}

static int
spi_lp8841_rtc_setup(struct spi_device *spi)
{
	if ((spi->mode & SPI_CS_HIGH) == 0) {
		dev_err(&spi->dev, "unsupported active low chip select\n");
		return -EINVAL;
	}

	if ((spi->mode & SPI_LSB_FIRST) == 0) {
		dev_err(&spi->dev, "unsupported MSB first mode\n");
		return -EINVAL;
	}

	if ((spi->mode & SPI_3WIRE) == 0) {
		dev_err(&spi->dev, "unsupported wiring. 3 wires required\n");
		return -EINVAL;
	}

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id spi_lp8841_rtc_dt_ids[] = {
	{ .compatible = "icpdas,lp8841-spi-rtc" },
	{ }
};

MODULE_DEVICE_TABLE(of, spi_lp8841_rtc_dt_ids);
#endif

static int
spi_lp8841_rtc_probe(struct platform_device *pdev)
{
	int				ret;
	struct spi_controller		*host;
	struct spi_lp8841_rtc		*data;

	host = spi_alloc_host(&pdev->dev, sizeof(*data));
	if (!host)
		return -ENOMEM;
	platform_set_drvdata(pdev, host);

	host->flags = SPI_CONTROLLER_HALF_DUPLEX;
	host->mode_bits = SPI_CS_HIGH | SPI_3WIRE | SPI_LSB_FIRST;

	host->bus_num = pdev->id;
	host->num_chipselect = 1;
	host->setup = spi_lp8841_rtc_setup;
	host->set_cs = spi_lp8841_rtc_set_cs;
	host->transfer_one = spi_lp8841_rtc_transfer_one;
	host->bits_per_word_mask = SPI_BPW_MASK(8);
#ifdef CONFIG_OF
	host->dev.of_node = pdev->dev.of_node;
#endif

	data = spi_controller_get_devdata(host);

	data->iomem = devm_platform_ioremap_resource(pdev, 0);
	ret = PTR_ERR_OR_ZERO(data->iomem);
	if (ret) {
		dev_err(&pdev->dev, "failed to get IO address\n");
		goto err_put_host;
	}

	/* register with the SPI framework */
	ret = devm_spi_register_controller(&pdev->dev, host);
	if (ret) {
		dev_err(&pdev->dev, "cannot register spi host\n");
		goto err_put_host;
	}

	return ret;


err_put_host:
	spi_controller_put(host);

	return ret;
}

MODULE_ALIAS("platform:" DRIVER_NAME);

static struct platform_driver spi_lp8841_rtc_driver = {
	.driver = {
		.name	= DRIVER_NAME,
		.of_match_table = of_match_ptr(spi_lp8841_rtc_dt_ids),
	},
	.probe		= spi_lp8841_rtc_probe,
};
module_platform_driver(spi_lp8841_rtc_driver);

MODULE_DESCRIPTION("SPI host driver for ICP DAS LP-8841 RTC");
MODULE_AUTHOR("Sergei Ianovich");
MODULE_LICENSE("GPL");
