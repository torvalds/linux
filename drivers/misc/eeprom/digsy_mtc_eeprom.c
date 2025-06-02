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

#include <linux/gpio/machine.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_gpio.h>

#define GPIO_EEPROM_CLK		216
#define GPIO_EEPROM_CS		210
#define GPIO_EEPROM_DI		217
#define GPIO_EEPROM_DO		249
#define GPIO_EEPROM_OE		255
#define EE_SPI_BUS_NUM	1

static const struct property_entry digsy_mtc_spi_properties[] = {
	PROPERTY_ENTRY_U32("data-size", 8),
	{ }
};

static const struct software_node digsy_mtc_spi_node = {
	.properties = digsy_mtc_spi_properties,
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
	.dev_id         = "spi_gpio.1",
	.table          = {
		GPIO_LOOKUP("gpio@b00", GPIO_EEPROM_CLK,
			    "sck", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("gpio@b00", GPIO_EEPROM_DI,
			    "mosi", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("gpio@b00", GPIO_EEPROM_DO,
			    "miso", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("gpio@b00", GPIO_EEPROM_CS,
			    "cs", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("gpio@b00", GPIO_EEPROM_OE,
			    "select", GPIO_ACTIVE_LOW),
		{ },
	},
};

static struct spi_board_info digsy_mtc_eeprom_info[] __initdata = {
	{
		.modalias		= "eeprom-93xx46",
		.max_speed_hz		= 1000000,
		.bus_num		= EE_SPI_BUS_NUM,
		.chip_select		= 0,
		.mode			= SPI_MODE_0,
	},
};

static int __init digsy_mtc_eeprom_devices_init(void)
{
	int ret;

	gpiod_add_lookup_table(&eeprom_spi_gpiod_table);
	spi_register_board_info(digsy_mtc_eeprom_info,
				ARRAY_SIZE(digsy_mtc_eeprom_info));

	ret = device_add_software_node(&digsy_mtc_eeprom.dev, &digsy_mtc_spi_node);
	if (ret)
		return ret;

	ret = platform_device_register(&digsy_mtc_eeprom);
	if (ret)
		device_remove_software_node(&digsy_mtc_eeprom.dev);

	return ret;
}
device_initcall(digsy_mtc_eeprom_devices_init);
