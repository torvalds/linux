/*
 * SPI master driver using generic bitbanged GPIO
 *
 * Copyright (C) 2006,2008 David Brownell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>

#include <linux/spi/spi.h>
#include <linux/spi/spi_bitbang.h>
#include <linux/spi/spi_gpio.h>


/*
 * This bitbanging SPI master driver should help make systems usable
 * when a native hardware SPI engine is not available, perhaps because
 * its driver isn't yet working or because the I/O pins it requires
 * are used for other purposes.
 *
 * platform_device->driver_data ... points to spi_gpio
 *
 * spi->controller_state ... reserved for bitbang framework code
 * spi->controller_data ... holds chipselect GPIO
 *
 * spi->master->dev.driver_data ... points to spi_gpio->bitbang
 */

struct spi_gpio {
	struct spi_bitbang		bitbang;
	struct spi_gpio_platform_data	pdata;
	struct platform_device		*pdev;
	int				cs_gpios[0];
};

/*----------------------------------------------------------------------*/

/*
 * Because the overhead of going through four GPIO procedure calls
 * per transferred bit can make performance a problem, this code
 * is set up so that you can use it in either of two ways:
 *
 *   - The slow generic way:  set up platform_data to hold the GPIO
 *     numbers used for MISO/MOSI/SCK, and issue procedure calls for
 *     each of them.  This driver can handle several such busses.
 *
 *   - The quicker inlined way:  only helps with platform GPIO code
 *     that inlines operations for constant GPIOs.  This can give
 *     you tight (fast!) inner loops, but each such bus needs a
 *     new driver.  You'll define a new C file, with Makefile and
 *     Kconfig support; the C code can be a total of six lines:
 *
 *		#define DRIVER_NAME	"myboard_spi2"
 *		#define	SPI_MISO_GPIO	119
 *		#define	SPI_MOSI_GPIO	120
 *		#define	SPI_SCK_GPIO	121
 *		#define	SPI_N_CHIPSEL	4
 *		#include "spi-gpio.c"
 */

#ifndef DRIVER_NAME
#define DRIVER_NAME	"spi_gpio"

#define GENERIC_BITBANG	/* vs tight inlines */

/* all functions referencing these symbols must define pdata */
#define SPI_MISO_GPIO	((pdata)->miso)
#define SPI_MOSI_GPIO	((pdata)->mosi)
#define SPI_SCK_GPIO	((pdata)->sck)

#define SPI_N_CHIPSEL	((pdata)->num_chipselect)

#endif

/*----------------------------------------------------------------------*/

static inline struct spi_gpio * __pure
spi_to_spi_gpio(const struct spi_device *spi)
{
	const struct spi_bitbang	*bang;
	struct spi_gpio			*spi_gpio;

	bang = spi_master_get_devdata(spi->master);
	spi_gpio = container_of(bang, struct spi_gpio, bitbang);
	return spi_gpio;
}

static inline struct spi_gpio_platform_data * __pure
spi_to_pdata(const struct spi_device *spi)
{
	return &spi_to_spi_gpio(spi)->pdata;
}

/* this is #defined to avoid unused-variable warnings when inlining */
#define pdata		spi_to_pdata(spi)

static inline void setsck(const struct spi_device *spi, int is_on)
{
	gpio_set_value_cansleep(SPI_SCK_GPIO, is_on);
}

static inline void setmosi(const struct spi_device *spi, int is_on)
{
	gpio_set_value_cansleep(SPI_MOSI_GPIO, is_on);
}

static inline int getmiso(const struct spi_device *spi)
{
	return !!gpio_get_value_cansleep(SPI_MISO_GPIO);
}

#undef pdata

/*
 * NOTE:  this clocks "as fast as we can".  It "should" be a function of the
 * requested device clock.  Software overhead means we usually have trouble
 * reaching even one Mbit/sec (except when we can inline bitops), so for now
 * we'll just assume we never need additional per-bit slowdowns.
 */
#define spidelay(nsecs)	do {} while (0)

#include "spi-bitbang-txrx.h"

/*
 * These functions can leverage inline expansion of GPIO calls to shrink
 * costs for a txrx bit, often by factors of around ten (by instruction
 * count).  That is particularly visible for larger word sizes, but helps
 * even with default 8-bit words.
 *
 * REVISIT overheads calling these functions for each word also have
 * significant performance costs.  Having txrx_bufs() calls that inline
 * the txrx_word() logic would help performance, e.g. on larger blocks
 * used with flash storage or MMC/SD.  There should also be ways to make
 * GCC be less stupid about reloading registers inside the I/O loops,
 * even without inlined GPIO calls; __attribute__((hot)) on GCC 4.3?
 */

static u32 spi_gpio_txrx_word_mode0(struct spi_device *spi,
		unsigned nsecs, u32 word, u8 bits)
{
	return bitbang_txrx_be_cpha0(spi, nsecs, 0, 0, word, bits);
}

static u32 spi_gpio_txrx_word_mode1(struct spi_device *spi,
		unsigned nsecs, u32 word, u8 bits)
{
	return bitbang_txrx_be_cpha1(spi, nsecs, 0, 0, word, bits);
}

static u32 spi_gpio_txrx_word_mode2(struct spi_device *spi,
		unsigned nsecs, u32 word, u8 bits)
{
	return bitbang_txrx_be_cpha0(spi, nsecs, 1, 0, word, bits);
}

static u32 spi_gpio_txrx_word_mode3(struct spi_device *spi,
		unsigned nsecs, u32 word, u8 bits)
{
	return bitbang_txrx_be_cpha1(spi, nsecs, 1, 0, word, bits);
}

/*
 * These functions do not call setmosi or getmiso if respective flag
 * (SPI_MASTER_NO_RX or SPI_MASTER_NO_TX) is set, so they are safe to
 * call when such pin is not present or defined in the controller.
 * A separate set of callbacks is defined to get highest possible
 * speed in the generic case (when both MISO and MOSI lines are
 * available), as optimiser will remove the checks when argument is
 * constant.
 */

static u32 spi_gpio_spec_txrx_word_mode0(struct spi_device *spi,
		unsigned nsecs, u32 word, u8 bits)
{
	unsigned flags = spi->master->flags;
	return bitbang_txrx_be_cpha0(spi, nsecs, 0, flags, word, bits);
}

static u32 spi_gpio_spec_txrx_word_mode1(struct spi_device *spi,
		unsigned nsecs, u32 word, u8 bits)
{
	unsigned flags = spi->master->flags;
	return bitbang_txrx_be_cpha1(spi, nsecs, 0, flags, word, bits);
}

static u32 spi_gpio_spec_txrx_word_mode2(struct spi_device *spi,
		unsigned nsecs, u32 word, u8 bits)
{
	unsigned flags = spi->master->flags;
	return bitbang_txrx_be_cpha0(spi, nsecs, 1, flags, word, bits);
}

static u32 spi_gpio_spec_txrx_word_mode3(struct spi_device *spi,
		unsigned nsecs, u32 word, u8 bits)
{
	unsigned flags = spi->master->flags;
	return bitbang_txrx_be_cpha1(spi, nsecs, 1, flags, word, bits);
}

/*----------------------------------------------------------------------*/

static void spi_gpio_chipselect(struct spi_device *spi, int is_active)
{
	struct spi_gpio *spi_gpio = spi_to_spi_gpio(spi);
	unsigned int cs = spi_gpio->cs_gpios[spi->chip_select];

	/* set initial clock polarity */
	if (is_active)
		setsck(spi, spi->mode & SPI_CPOL);

	if (cs != SPI_GPIO_NO_CHIPSELECT) {
		/* SPI is normally active-low */
		gpio_set_value_cansleep(cs, (spi->mode & SPI_CS_HIGH) ? is_active : !is_active);
	}
}

static int spi_gpio_setup(struct spi_device *spi)
{
	unsigned int		cs;
	int			status = 0;
	struct spi_gpio		*spi_gpio = spi_to_spi_gpio(spi);
	struct device_node	*np = spi->master->dev.of_node;

	if (np) {
		/*
		 * In DT environments, the CS GPIOs have already been
		 * initialized from the "cs-gpios" property of the node.
		 */
		cs = spi_gpio->cs_gpios[spi->chip_select];
	} else {
		/*
		 * ... otherwise, take it from spi->controller_data
		 */
		cs = (unsigned int)(uintptr_t) spi->controller_data;
	}

	if (!spi->controller_state) {
		if (cs != SPI_GPIO_NO_CHIPSELECT) {
			status = gpio_request(cs, dev_name(&spi->dev));
			if (status)
				return status;
			status = gpio_direction_output(cs,
					!(spi->mode & SPI_CS_HIGH));
		}
	}
	if (!status) {
		/* in case it was initialized from static board data */
		spi_gpio->cs_gpios[spi->chip_select] = cs;
		status = spi_bitbang_setup(spi);
	}

	if (status) {
		if (!spi->controller_state && cs != SPI_GPIO_NO_CHIPSELECT)
			gpio_free(cs);
	}
	return status;
}

static void spi_gpio_cleanup(struct spi_device *spi)
{
	struct spi_gpio *spi_gpio = spi_to_spi_gpio(spi);
	unsigned int cs = spi_gpio->cs_gpios[spi->chip_select];

	if (cs != SPI_GPIO_NO_CHIPSELECT)
		gpio_free(cs);
	spi_bitbang_cleanup(spi);
}

static int spi_gpio_alloc(unsigned pin, const char *label, bool is_in)
{
	int value;

	value = gpio_request(pin, label);
	if (value == 0) {
		if (is_in)
			value = gpio_direction_input(pin);
		else
			value = gpio_direction_output(pin, 0);
	}
	return value;
}

static int spi_gpio_request(struct spi_gpio_platform_data *pdata,
			    const char *label, u16 *res_flags)
{
	int value;

	/* NOTE:  SPI_*_GPIO symbols may reference "pdata" */

	if (SPI_MOSI_GPIO != SPI_GPIO_NO_MOSI) {
		value = spi_gpio_alloc(SPI_MOSI_GPIO, label, false);
		if (value)
			goto done;
	} else {
		/* HW configuration without MOSI pin */
		*res_flags |= SPI_MASTER_NO_TX;
	}

	if (SPI_MISO_GPIO != SPI_GPIO_NO_MISO) {
		value = spi_gpio_alloc(SPI_MISO_GPIO, label, true);
		if (value)
			goto free_mosi;
	} else {
		/* HW configuration without MISO pin */
		*res_flags |= SPI_MASTER_NO_RX;
	}

	value = spi_gpio_alloc(SPI_SCK_GPIO, label, false);
	if (value)
		goto free_miso;

	goto done;

free_miso:
	if (SPI_MISO_GPIO != SPI_GPIO_NO_MISO)
		gpio_free(SPI_MISO_GPIO);
free_mosi:
	if (SPI_MOSI_GPIO != SPI_GPIO_NO_MOSI)
		gpio_free(SPI_MOSI_GPIO);
done:
	return value;
}

#ifdef CONFIG_OF
static const struct of_device_id spi_gpio_dt_ids[] = {
	{ .compatible = "spi-gpio" },
	{}
};
MODULE_DEVICE_TABLE(of, spi_gpio_dt_ids);

static int spi_gpio_probe_dt(struct platform_device *pdev)
{
	int ret;
	u32 tmp;
	struct spi_gpio_platform_data	*pdata;
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *of_id =
			of_match_device(spi_gpio_dt_ids, &pdev->dev);

	if (!of_id)
		return 0;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	ret = of_get_named_gpio(np, "gpio-sck", 0);
	if (ret < 0) {
		dev_err(&pdev->dev, "gpio-sck property not found\n");
		goto error_free;
	}
	pdata->sck = ret;

	ret = of_get_named_gpio(np, "gpio-miso", 0);
	if (ret < 0) {
		dev_info(&pdev->dev, "gpio-miso property not found, switching to no-rx mode\n");
		pdata->miso = SPI_GPIO_NO_MISO;
	} else
		pdata->miso = ret;

	ret = of_get_named_gpio(np, "gpio-mosi", 0);
	if (ret < 0) {
		dev_info(&pdev->dev, "gpio-mosi property not found, switching to no-tx mode\n");
		pdata->mosi = SPI_GPIO_NO_MOSI;
	} else
		pdata->mosi = ret;

	ret = of_property_read_u32(np, "num-chipselects", &tmp);
	if (ret < 0) {
		dev_err(&pdev->dev, "num-chipselects property not found\n");
		goto error_free;
	}

	pdata->num_chipselect = tmp;
	pdev->dev.platform_data = pdata;

	return 1;

error_free:
	devm_kfree(&pdev->dev, pdata);
	return ret;
}
#else
static inline int spi_gpio_probe_dt(struct platform_device *pdev)
{
	return 0;
}
#endif

static int spi_gpio_probe(struct platform_device *pdev)
{
	int				status;
	struct spi_master		*master;
	struct spi_gpio			*spi_gpio;
	struct spi_gpio_platform_data	*pdata;
	u16 master_flags = 0;
	bool use_of = 0;

	status = spi_gpio_probe_dt(pdev);
	if (status < 0)
		return status;
	if (status > 0)
		use_of = 1;

	pdata = dev_get_platdata(&pdev->dev);
#ifdef GENERIC_BITBANG
	if (!pdata || !pdata->num_chipselect)
		return -ENODEV;
#endif

	status = spi_gpio_request(pdata, dev_name(&pdev->dev), &master_flags);
	if (status < 0)
		return status;

	master = spi_alloc_master(&pdev->dev, sizeof(*spi_gpio) +
					(sizeof(int) * SPI_N_CHIPSEL));
	if (!master) {
		status = -ENOMEM;
		goto gpio_free;
	}
	spi_gpio = spi_master_get_devdata(master);
	platform_set_drvdata(pdev, spi_gpio);

	spi_gpio->pdev = pdev;
	if (pdata)
		spi_gpio->pdata = *pdata;

	master->bits_per_word_mask = SPI_BPW_RANGE_MASK(1, 32);
	master->flags = master_flags;
	master->bus_num = pdev->id;
	master->num_chipselect = SPI_N_CHIPSEL;
	master->setup = spi_gpio_setup;
	master->cleanup = spi_gpio_cleanup;
#ifdef CONFIG_OF
	master->dev.of_node = pdev->dev.of_node;

	if (use_of) {
		int i;
		struct device_node *np = pdev->dev.of_node;

		/*
		 * In DT environments, take the CS GPIO from the "cs-gpios"
		 * property of the node.
		 */

		for (i = 0; i < SPI_N_CHIPSEL; i++)
			spi_gpio->cs_gpios[i] =
				of_get_named_gpio(np, "cs-gpios", i);
	}
#endif

	spi_gpio->bitbang.master = master;
	spi_gpio->bitbang.chipselect = spi_gpio_chipselect;

	if ((master_flags & (SPI_MASTER_NO_TX | SPI_MASTER_NO_RX)) == 0) {
		spi_gpio->bitbang.txrx_word[SPI_MODE_0] = spi_gpio_txrx_word_mode0;
		spi_gpio->bitbang.txrx_word[SPI_MODE_1] = spi_gpio_txrx_word_mode1;
		spi_gpio->bitbang.txrx_word[SPI_MODE_2] = spi_gpio_txrx_word_mode2;
		spi_gpio->bitbang.txrx_word[SPI_MODE_3] = spi_gpio_txrx_word_mode3;
	} else {
		spi_gpio->bitbang.txrx_word[SPI_MODE_0] = spi_gpio_spec_txrx_word_mode0;
		spi_gpio->bitbang.txrx_word[SPI_MODE_1] = spi_gpio_spec_txrx_word_mode1;
		spi_gpio->bitbang.txrx_word[SPI_MODE_2] = spi_gpio_spec_txrx_word_mode2;
		spi_gpio->bitbang.txrx_word[SPI_MODE_3] = spi_gpio_spec_txrx_word_mode3;
	}
	spi_gpio->bitbang.setup_transfer = spi_bitbang_setup_transfer;
	spi_gpio->bitbang.flags = SPI_CS_HIGH;

	status = spi_bitbang_start(&spi_gpio->bitbang);
	if (status < 0) {
gpio_free:
		if (SPI_MISO_GPIO != SPI_GPIO_NO_MISO)
			gpio_free(SPI_MISO_GPIO);
		if (SPI_MOSI_GPIO != SPI_GPIO_NO_MOSI)
			gpio_free(SPI_MOSI_GPIO);
		gpio_free(SPI_SCK_GPIO);
		spi_master_put(master);
	}

	return status;
}

static int spi_gpio_remove(struct platform_device *pdev)
{
	struct spi_gpio			*spi_gpio;
	struct spi_gpio_platform_data	*pdata;

	spi_gpio = platform_get_drvdata(pdev);
	pdata = dev_get_platdata(&pdev->dev);

	/* stop() unregisters child devices too */
	spi_bitbang_stop(&spi_gpio->bitbang);

	if (SPI_MISO_GPIO != SPI_GPIO_NO_MISO)
		gpio_free(SPI_MISO_GPIO);
	if (SPI_MOSI_GPIO != SPI_GPIO_NO_MOSI)
		gpio_free(SPI_MOSI_GPIO);
	gpio_free(SPI_SCK_GPIO);
	spi_master_put(spi_gpio->bitbang.master);

	return 0;
}

MODULE_ALIAS("platform:" DRIVER_NAME);

static struct platform_driver spi_gpio_driver = {
	.driver = {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(spi_gpio_dt_ids),
	},
	.probe		= spi_gpio_probe,
	.remove		= spi_gpio_remove,
};
module_platform_driver(spi_gpio_driver);

MODULE_DESCRIPTION("SPI master driver using generic bitbanged GPIO ");
MODULE_AUTHOR("David Brownell");
MODULE_LICENSE("GPL");
