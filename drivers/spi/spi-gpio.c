// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SPI master driver using generic bitbanged GPIO
 *
 * Copyright (C) 2006,2008 David Brownell
 * Copyright (C) 2017 Linus Walleij
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/of_device.h>

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
 *
 * spi->master->dev.driver_data ... points to spi_gpio->bitbang
 */

struct spi_gpio {
	struct spi_bitbang		bitbang;
	struct gpio_desc		*sck;
	struct gpio_desc		*miso;
	struct gpio_desc		*mosi;
	struct gpio_desc		**cs_gpios;
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

#endif

/*----------------------------------------------------------------------*/

static inline struct spi_gpio *__pure
spi_to_spi_gpio(const struct spi_device *spi)
{
	const struct spi_bitbang	*bang;
	struct spi_gpio			*spi_gpio;

	bang = spi_master_get_devdata(spi->master);
	spi_gpio = container_of(bang, struct spi_gpio, bitbang);
	return spi_gpio;
}

/* These helpers are in turn called by the bitbang inlines */
static inline void setsck(const struct spi_device *spi, int is_on)
{
	struct spi_gpio *spi_gpio = spi_to_spi_gpio(spi);

	gpiod_set_value_cansleep(spi_gpio->sck, is_on);
}

static inline void setmosi(const struct spi_device *spi, int is_on)
{
	struct spi_gpio *spi_gpio = spi_to_spi_gpio(spi);

	gpiod_set_value_cansleep(spi_gpio->mosi, is_on);
}

static inline int getmiso(const struct spi_device *spi)
{
	struct spi_gpio *spi_gpio = spi_to_spi_gpio(spi);

	if (spi->mode & SPI_3WIRE)
		return !!gpiod_get_value_cansleep(spi_gpio->mosi);
	else
		return !!gpiod_get_value_cansleep(spi_gpio->miso);
}

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
		unsigned nsecs, u32 word, u8 bits, unsigned flags)
{
	if (unlikely(spi->mode & SPI_LSB_FIRST))
		return bitbang_txrx_le_cpha0(spi, nsecs, 0, flags, word, bits);
	else
		return bitbang_txrx_be_cpha0(spi, nsecs, 0, flags, word, bits);
}

static u32 spi_gpio_txrx_word_mode1(struct spi_device *spi,
		unsigned nsecs, u32 word, u8 bits, unsigned flags)
{
	if (unlikely(spi->mode & SPI_LSB_FIRST))
		return bitbang_txrx_le_cpha1(spi, nsecs, 0, flags, word, bits);
	else
		return bitbang_txrx_be_cpha1(spi, nsecs, 0, flags, word, bits);
}

static u32 spi_gpio_txrx_word_mode2(struct spi_device *spi,
		unsigned nsecs, u32 word, u8 bits, unsigned flags)
{
	if (unlikely(spi->mode & SPI_LSB_FIRST))
		return bitbang_txrx_le_cpha0(spi, nsecs, 1, flags, word, bits);
	else
		return bitbang_txrx_be_cpha0(spi, nsecs, 1, flags, word, bits);
}

static u32 spi_gpio_txrx_word_mode3(struct spi_device *spi,
		unsigned nsecs, u32 word, u8 bits, unsigned flags)
{
	if (unlikely(spi->mode & SPI_LSB_FIRST))
		return bitbang_txrx_le_cpha1(spi, nsecs, 1, flags, word, bits);
	else
		return bitbang_txrx_be_cpha1(spi, nsecs, 1, flags, word, bits);
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
		unsigned nsecs, u32 word, u8 bits, unsigned flags)
{
	flags = spi->master->flags;
	if (unlikely(spi->mode & SPI_LSB_FIRST))
		return bitbang_txrx_le_cpha0(spi, nsecs, 0, flags, word, bits);
	else
		return bitbang_txrx_be_cpha0(spi, nsecs, 0, flags, word, bits);
}

static u32 spi_gpio_spec_txrx_word_mode1(struct spi_device *spi,
		unsigned nsecs, u32 word, u8 bits, unsigned flags)
{
	flags = spi->master->flags;
	if (unlikely(spi->mode & SPI_LSB_FIRST))
		return bitbang_txrx_le_cpha1(spi, nsecs, 0, flags, word, bits);
	else
		return bitbang_txrx_be_cpha1(spi, nsecs, 0, flags, word, bits);
}

static u32 spi_gpio_spec_txrx_word_mode2(struct spi_device *spi,
		unsigned nsecs, u32 word, u8 bits, unsigned flags)
{
	flags = spi->master->flags;
	if (unlikely(spi->mode & SPI_LSB_FIRST))
		return bitbang_txrx_le_cpha0(spi, nsecs, 1, flags, word, bits);
	else
		return bitbang_txrx_be_cpha0(spi, nsecs, 1, flags, word, bits);
}

static u32 spi_gpio_spec_txrx_word_mode3(struct spi_device *spi,
		unsigned nsecs, u32 word, u8 bits, unsigned flags)
{
	flags = spi->master->flags;
	if (unlikely(spi->mode & SPI_LSB_FIRST))
		return bitbang_txrx_le_cpha1(spi, nsecs, 1, flags, word, bits);
	else
		return bitbang_txrx_be_cpha1(spi, nsecs, 1, flags, word, bits);
}

/*----------------------------------------------------------------------*/

static void spi_gpio_chipselect(struct spi_device *spi, int is_active)
{
	struct spi_gpio *spi_gpio = spi_to_spi_gpio(spi);

	/* set initial clock line level */
	if (is_active)
		gpiod_set_value_cansleep(spi_gpio->sck, spi->mode & SPI_CPOL);

	/* Drive chip select line, if we have one */
	if (spi_gpio->cs_gpios) {
		struct gpio_desc *cs = spi_gpio->cs_gpios[spi_get_chipselect(spi, 0)];

		/* SPI chip selects are normally active-low */
		gpiod_set_value_cansleep(cs, (spi->mode & SPI_CS_HIGH) ? is_active : !is_active);
	}
}

static int spi_gpio_setup(struct spi_device *spi)
{
	struct gpio_desc	*cs;
	int			status = 0;
	struct spi_gpio		*spi_gpio = spi_to_spi_gpio(spi);

	/*
	 * The CS GPIOs have already been
	 * initialized from the descriptor lookup.
	 */
	if (spi_gpio->cs_gpios) {
		cs = spi_gpio->cs_gpios[spi_get_chipselect(spi, 0)];
		if (!spi->controller_state && cs)
			status = gpiod_direction_output(cs,
						  !(spi->mode & SPI_CS_HIGH));
	}

	if (!status)
		status = spi_bitbang_setup(spi);

	return status;
}

static int spi_gpio_set_direction(struct spi_device *spi, bool output)
{
	struct spi_gpio *spi_gpio = spi_to_spi_gpio(spi);
	int ret;

	if (output)
		return gpiod_direction_output(spi_gpio->mosi, 1);

	/*
	 * Only change MOSI to an input if using 3WIRE mode.
	 * Otherwise, MOSI could be left floating if there is
	 * no pull resistor connected to the I/O pin, or could
	 * be left logic high if there is a pull-up. Transmitting
	 * logic high when only clocking MISO data in can put some
	 * SPI devices in to a bad state.
	 */
	if (spi->mode & SPI_3WIRE) {
		ret = gpiod_direction_input(spi_gpio->mosi);
		if (ret)
			return ret;
	}
	/*
	 * Send a turnaround high impedance cycle when switching
	 * from output to input. Theoretically there should be
	 * a clock delay here, but as has been noted above, the
	 * nsec delay function for bit-banged GPIO is simply
	 * {} because bit-banging just doesn't get fast enough
	 * anyway.
	 */
	if (spi->mode & SPI_3WIRE_HIZ) {
		gpiod_set_value_cansleep(spi_gpio->sck,
					 !(spi->mode & SPI_CPOL));
		gpiod_set_value_cansleep(spi_gpio->sck,
					 !!(spi->mode & SPI_CPOL));
	}
	return 0;
}

static void spi_gpio_cleanup(struct spi_device *spi)
{
	spi_bitbang_cleanup(spi);
}

/*
 * It can be convenient to use this driver with pins that have alternate
 * functions associated with a "native" SPI controller if a driver for that
 * controller is not available, or is missing important functionality.
 *
 * On platforms which can do so, configure MISO with a weak pullup unless
 * there's an external pullup on that signal.  That saves power by avoiding
 * floating signals.  (A weak pulldown would save power too, but many
 * drivers expect to see all-ones data as the no slave "response".)
 */
static int spi_gpio_request(struct device *dev, struct spi_gpio *spi_gpio)
{
	spi_gpio->mosi = devm_gpiod_get_optional(dev, "mosi", GPIOD_OUT_LOW);
	if (IS_ERR(spi_gpio->mosi))
		return PTR_ERR(spi_gpio->mosi);

	spi_gpio->miso = devm_gpiod_get_optional(dev, "miso", GPIOD_IN);
	if (IS_ERR(spi_gpio->miso))
		return PTR_ERR(spi_gpio->miso);

	spi_gpio->sck = devm_gpiod_get(dev, "sck", GPIOD_OUT_LOW);
	return PTR_ERR_OR_ZERO(spi_gpio->sck);
}

#ifdef CONFIG_OF
static const struct of_device_id spi_gpio_dt_ids[] = {
	{ .compatible = "spi-gpio" },
	{}
};
MODULE_DEVICE_TABLE(of, spi_gpio_dt_ids);

static int spi_gpio_probe_dt(struct platform_device *pdev,
			     struct spi_master *master)
{
	master->dev.of_node = pdev->dev.of_node;
	master->use_gpio_descriptors = true;

	return 0;
}
#else
static inline int spi_gpio_probe_dt(struct platform_device *pdev,
				    struct spi_master *master)
{
	return 0;
}
#endif

static int spi_gpio_probe_pdata(struct platform_device *pdev,
				struct spi_master *master)
{
	struct device *dev = &pdev->dev;
	struct spi_gpio_platform_data *pdata = dev_get_platdata(dev);
	struct spi_gpio *spi_gpio = spi_master_get_devdata(master);
	int i;

#ifdef GENERIC_BITBANG
	if (!pdata || !pdata->num_chipselect)
		return -ENODEV;
#endif
	/*
	 * The master needs to think there is a chipselect even if not
	 * connected
	 */
	master->num_chipselect = pdata->num_chipselect ?: 1;

	spi_gpio->cs_gpios = devm_kcalloc(dev, master->num_chipselect,
					  sizeof(*spi_gpio->cs_gpios),
					  GFP_KERNEL);
	if (!spi_gpio->cs_gpios)
		return -ENOMEM;

	for (i = 0; i < master->num_chipselect; i++) {
		spi_gpio->cs_gpios[i] = devm_gpiod_get_index(dev, "cs", i,
							     GPIOD_OUT_HIGH);
		if (IS_ERR(spi_gpio->cs_gpios[i]))
			return PTR_ERR(spi_gpio->cs_gpios[i]);
	}

	return 0;
}

static int spi_gpio_probe(struct platform_device *pdev)
{
	int				status;
	struct spi_master		*master;
	struct spi_gpio			*spi_gpio;
	struct device			*dev = &pdev->dev;
	struct spi_bitbang		*bb;

	master = devm_spi_alloc_master(dev, sizeof(*spi_gpio));
	if (!master)
		return -ENOMEM;

	if (pdev->dev.of_node)
		status = spi_gpio_probe_dt(pdev, master);
	else
		status = spi_gpio_probe_pdata(pdev, master);

	if (status)
		return status;

	spi_gpio = spi_master_get_devdata(master);

	status = spi_gpio_request(dev, spi_gpio);
	if (status)
		return status;

	master->bits_per_word_mask = SPI_BPW_RANGE_MASK(1, 32);
	master->mode_bits = SPI_3WIRE | SPI_3WIRE_HIZ | SPI_CPHA | SPI_CPOL |
			    SPI_CS_HIGH | SPI_LSB_FIRST;
	if (!spi_gpio->mosi) {
		/* HW configuration without MOSI pin
		 *
		 * No setting SPI_MASTER_NO_RX here - if there is only
		 * a MOSI pin connected the host can still do RX by
		 * changing the direction of the line.
		 */
		master->flags = SPI_MASTER_NO_TX;
	}

	master->bus_num = pdev->id;
	master->setup = spi_gpio_setup;
	master->cleanup = spi_gpio_cleanup;

	bb = &spi_gpio->bitbang;
	bb->master = master;
	/*
	 * There is some additional business, apart from driving the CS GPIO
	 * line, that we need to do on selection. This makes the local
	 * callback for chipselect always get called.
	 */
	master->flags |= SPI_MASTER_GPIO_SS;
	bb->chipselect = spi_gpio_chipselect;
	bb->set_line_direction = spi_gpio_set_direction;

	if (master->flags & SPI_MASTER_NO_TX) {
		bb->txrx_word[SPI_MODE_0] = spi_gpio_spec_txrx_word_mode0;
		bb->txrx_word[SPI_MODE_1] = spi_gpio_spec_txrx_word_mode1;
		bb->txrx_word[SPI_MODE_2] = spi_gpio_spec_txrx_word_mode2;
		bb->txrx_word[SPI_MODE_3] = spi_gpio_spec_txrx_word_mode3;
	} else {
		bb->txrx_word[SPI_MODE_0] = spi_gpio_txrx_word_mode0;
		bb->txrx_word[SPI_MODE_1] = spi_gpio_txrx_word_mode1;
		bb->txrx_word[SPI_MODE_2] = spi_gpio_txrx_word_mode2;
		bb->txrx_word[SPI_MODE_3] = spi_gpio_txrx_word_mode3;
	}
	bb->setup_transfer = spi_bitbang_setup_transfer;

	status = spi_bitbang_init(&spi_gpio->bitbang);
	if (status)
		return status;

	return devm_spi_register_master(&pdev->dev, master);
}

MODULE_ALIAS("platform:" DRIVER_NAME);

static struct platform_driver spi_gpio_driver = {
	.driver = {
		.name	= DRIVER_NAME,
		.of_match_table = of_match_ptr(spi_gpio_dt_ids),
	},
	.probe		= spi_gpio_probe,
};
module_platform_driver(spi_gpio_driver);

MODULE_DESCRIPTION("SPI master driver using generic bitbanged GPIO ");
MODULE_AUTHOR("David Brownell");
MODULE_LICENSE("GPL");
