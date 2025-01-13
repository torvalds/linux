// SPDX-License-Identifier: GPL-2.0-only
/*
 * EEPROMs access control driver for display configuration EEPROMs
 * on DigsyMTC board.
 *
 * (C) 2011 DENX Software Engineering, Anatolij Gustschin <agust@denx.de>
 *
 * FIXME: this driver is used on a device-tree probed platform: it
 * should be defined as a bit-banged SPI device and probed from the device
 * tree and not like this with static grabbing of a few numbered GPIO
 * lines at random.
 *
 * Add proper SPI and EEPROM in arch/powerpc/boot/dts/digsy_mtc.dts
 * and delete this driver.
 */

#include <linux/gpio.h>
#include <linux/gpio/machine.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_gpio.h>
#include <linux/eeprom_93xx46.h>

#define GPIO_EEPROM_CLK		216
#define GPIO_EEPROM_CS		210
#define GPIO_EEPROM_DI		217
#define GPIO_EEPROM_DO		249
#define GPIO_EEPROM_OE		255
#define EE_SPI_BUS_NUM	1

static void digsy_mtc_op_prepare(void *p)
{
	/* enable */
	gpio_set_value(GPIO_EEPROM_OE, 0);
}

static void digsy_mtc_op_finish(void *p)
{
	/* disable */
	gpio_set_value(GPIO_EEPROM_OE, 1);
}

struct eeprom_93xx46_platform_data digsy_mtc_eeprom_data = {
	.flags		= EE_ADDR8 | EE_SIZE1K,
	.prepare	= digsy_mtc_op_prepare,
	.finish		= digsy_mtc_op_finish,
};

static struct spi_gpio_platform_data eeprom_spi_gpio_data = {
	.num_chipselect	= 1,
};

static struct platform_device digsy_mtc_eeprom = {
	.name	= "spi_gpio",
	.id	= EE_SPI_BUS_NUM,
	.dev	= {
		.platform_data	= &eeprom_spi_gpio_data,
	},
};

static struct gpiod_lookup_table eeprom_spi_gpiod_table = {
	.dev_id         = "spi_gpio",
	.table          = {
		GPIO_LOOKUP("gpio@b00", GPIO_EEPROM_CLK,
			    "sck", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("gpio@b00", GPIO_EEPROM_DI,
			    "mosi", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("gpio@b00", GPIO_EEPROM_DO,
			    "miso", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("gpio@b00", GPIO_EEPROM_CS,
			    "cs", GPIO_ACTIVE_HIGH),
		{ },
	},
};

static struct spi_board_info digsy_mtc_eeprom_info[] __initdata = {
	{
		.modalias		= "93xx46",
		.max_speed_hz		= 1000000,
		.bus_num		= EE_SPI_BUS_NUM,
		.chip_select		= 0,
		.mode			= SPI_MODE_0,
		.platform_data		= &digsy_mtc_eeprom_data,
	},
};

static int __init digsy_mtc_eeprom_devices_init(void)
{
	int ret;

	ret = gpio_request_one(GPIO_EEPROM_OE, GPIOF_OUT_INIT_HIGH,
				"93xx46 EEPROMs OE");
	if (ret) {
		pr_err("can't request gpio %d\n", GPIO_EEPROM_OE);
		return ret;
	}
	gpiod_add_lookup_table(&eeprom_spi_gpiod_table);
	spi_register_board_info(digsy_mtc_eeprom_info,
				ARRAY_SIZE(digsy_mtc_eeprom_info));
	return platform_device_register(&digsy_mtc_eeprom);
}
device_initcall(digsy_mtc_eeprom_devices_init);
