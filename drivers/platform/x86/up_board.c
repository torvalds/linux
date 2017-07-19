/*
 * UP Board header pin GPIO driver.
 *
 * Copyright (c) 2016, Emutex Ltd.  All rights reserved.
 *
 * Author: Dan O'Donovan <dan@emutex.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/module.h>
#include <linux/dmi.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/platform_data/pca953x.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/machine.h>
#include <linux/acpi.h>

#define UP_BOARD_SPIDEV_BUS_NUM 2
#define UP_BOARD_SPIDEV_MAX_CLK 25000000

struct up_board_info {
	struct pinctrl_map *pinmux_maps;
	unsigned num_pinmux_maps;
	int (*init_devices)(void);
};

static bool spidev0 = true;
module_param(spidev0, bool, S_IRUGO);
MODULE_PARM_DESC(spidev0, "register a spidev device on SPI bus 2-0");

static bool spidev1 = false;
module_param(spidev1, bool, S_IRUGO);
MODULE_PARM_DESC(spidev1, "register a spidev device on SPI bus 2-1");

/* On the UP board, if the ODEn bit is set on the pad configuration
 * it seems to impair some functions on the I/O header such as UART, SPI
 * and even I2C.  So we disable it for all header pins by default.
 */
static unsigned long oden_disable_conf[] = {
	PIN_CONF_PACKED(PIN_CONFIG_DRIVE_PUSH_PULL, 0),
};

#define UP_PIN_MAP_MUX_GROUP(d, p, f) \
	PIN_MAP_MUX_GROUP_DEFAULT(d, p, f "_grp", f)

#define UP_PIN_MAP_CONF_ODEN(d, p, f) \
	PIN_MAP_CONFIGS_GROUP_DEFAULT(d, p, f "_grp", oden_disable_conf)

/* Maps pin functions on UP Board I/O pin header to specific CHT SoC devices */
static struct pinctrl_map up_pinmux_maps[] __initdata = {
	UP_PIN_MAP_MUX_GROUP("8086228A:00", "up-pinctrl", "uart1"),
	UP_PIN_MAP_MUX_GROUP("808622C1:00", "up-pinctrl", "i2c0"),
	UP_PIN_MAP_MUX_GROUP("808622C1:01", "up-pinctrl", "i2c1"),
	UP_PIN_MAP_MUX_GROUP("808622A8:00", "up-pinctrl", "i2s0"),
	UP_PIN_MAP_MUX_GROUP("80862288:00", "up-pinctrl", "pwm0"),
	UP_PIN_MAP_MUX_GROUP("80862288:01", "up-pinctrl", "pwm1"),
	UP_PIN_MAP_MUX_GROUP("8086228E:01", "up-pinctrl", "spi2"),
	UP_PIN_MAP_MUX_GROUP("i2c-ADC081C:00", "up-pinctrl", "adc0"),

	UP_PIN_MAP_MUX_GROUP("8086228A:00", "INT33FF:00", "uart1"),
	UP_PIN_MAP_MUX_GROUP("808622C1:00", "INT33FF:00", "i2c0"),
	UP_PIN_MAP_MUX_GROUP("808622C1:01", "INT33FF:00", "i2c1"),
	UP_PIN_MAP_MUX_GROUP("808622C1:02", "INT33FF:00", "i2c2"),
	UP_PIN_MAP_MUX_GROUP("808622A8:00", "INT33FF:00", "lpe"),
	UP_PIN_MAP_MUX_GROUP("80862288:00", "INT33FF:03", "pwm0"),
	UP_PIN_MAP_MUX_GROUP("80862288:01", "INT33FF:03", "pwm1"),
	UP_PIN_MAP_MUX_GROUP("8086228E:01", "INT33FF:03", "spi2"),

	UP_PIN_MAP_CONF_ODEN("8086228A:00", "INT33FF:00", "uart1"),
	UP_PIN_MAP_CONF_ODEN("808622C1:00", "INT33FF:00", "i2c0"),
	UP_PIN_MAP_CONF_ODEN("808622C1:01", "INT33FF:00", "i2c1"),
	UP_PIN_MAP_CONF_ODEN("808622A8:00", "INT33FF:00", "lpe"),
	UP_PIN_MAP_CONF_ODEN("80862288:00", "INT33FF:03", "pwm0"),
	UP_PIN_MAP_CONF_ODEN("80862288:01", "INT33FF:03", "pwm1"),
	UP_PIN_MAP_CONF_ODEN("8086228E:01", "INT33FF:03", "spi2"),
};

static struct platform_device *up_pinctrl_dev;

static struct regulator_consumer_supply vref3v3_consumers[] = {
	REGULATOR_SUPPLY("vref", "i2c-ADC081C:00"),
};

static struct spi_board_info up_spidev_info __initdata = {
	.modalias	= "spidev",
	.bus_num	= UP_BOARD_SPIDEV_BUS_NUM,
	.max_speed_hz   = UP_BOARD_SPIDEV_MAX_CLK,
};

static int __init
up_board_init_devices(void)
{
#if IS_ENABLED(CONFIG_REGULATOR)
	struct platform_device *vreg;

	vreg = regulator_register_always_on(0, "fixed-3.3V",
					    vref3v3_consumers,
					    ARRAY_SIZE(vref3v3_consumers),
					    3300000);
	if (!vreg) {
		pr_err("Failed to register UP Board ADC vref regulator");
		return -ENODEV;
	}
#else
	pr_dbg("Voltage and Current Regulator Support NOT enabled!");
#endif

	return 0;
}

static struct up_board_info up_board_info __initdata = {
	.pinmux_maps = up_pinmux_maps,
	.num_pinmux_maps = ARRAY_SIZE(up_pinmux_maps),
	.init_devices = up_board_init_devices,
};

static const struct dmi_system_id up_board_id_table[] = {
	{
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "AAEON"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "UP-CHT01"),
			DMI_EXACT_MATCH(DMI_BOARD_VERSION, "V0.4"),
		},
		.driver_data = (void *)&up_board_info
	},
	{
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "AAEON"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "UP-CHT01"),
			DMI_EXACT_MATCH(DMI_BOARD_VERSION, "V0.5"),
		},
		.driver_data = (void *)&up_board_info
	},
	{ }
};

#define UPFPGA_PROTOCOL_V1_HRV 1

static int __init
up_board_init(void) {
	const struct dmi_system_id *system_id;
	struct up_board_info *board_info;
	acpi_status status;
	unsigned long long hrv;
	int ret;

	system_id = dmi_first_match(up_board_id_table);
	if (!system_id)
		return -ENXIO;

	status = acpi_evaluate_integer(NULL, "\\_SB.PCTL._HRV", NULL, &hrv);
	if (ACPI_SUCCESS(status) && (hrv != UPFPGA_PROTOCOL_V1_HRV)) {
		pr_debug("Unsupported PCTL revision: %llu", hrv);
		return -ENODEV;
	}

	board_info = system_id->driver_data;

	/* Register pin control mappings specific to board version */
	if (board_info->pinmux_maps) {
		ret = pinctrl_register_mappings(board_info->pinmux_maps,
						board_info->num_pinmux_maps);
		if (ret) {
			pr_err("Failed to register UP Board pinctrl mapping");
			return ret;
		}
	}

	/* Register devices specific to board version */
	if (board_info->init_devices) {
		ret = board_info->init_devices();
		if (ret) {
			pr_err("Failed to register UP Board devices");
			return ret;
		}
	}

	/* Register devices common to all board versions */
	if (spidev0) {
		up_spidev_info.chip_select = 0;
		ret = spi_register_board_info(&up_spidev_info, 1);
		if (ret) {
			pr_err("Failed to register UP Board spidev0 device");
			return -ENODEV;
		}
	}
	if (spidev1) {
		up_spidev_info.chip_select = 1;
		ret = spi_register_board_info(&up_spidev_info, 1);
		if (ret) {
			pr_err("Failed to register UP Board spidev1 device");
			return -ENODEV;
		}
	}

	/* Create a virtual device to manage the UP Board GPIO pin header */
	up_pinctrl_dev = platform_device_alloc("up-pinctrl", -1);
	if (!up_pinctrl_dev) {
		pr_err("Failed to allocate UP pinctrl platform device");
		return -ENOMEM;
	}

	ret = platform_device_add(up_pinctrl_dev);
	if (ret) {
		pr_err("Failed to allocate UP pinctrl platform device");
		platform_device_put(up_pinctrl_dev);
		return ret;
	}

	return 0;
}

static void __exit
up_board_exit(void)
{
	platform_device_unregister(up_pinctrl_dev);
}

/* Using arch_initcall to ensure that i2c devices are registered
 * before the I2C adapters are enumerated
 */
arch_initcall(up_board_init);
module_exit(up_board_exit);

MODULE_AUTHOR("Dan O'Donovan <dan@emutex.com>");
MODULE_DESCRIPTION("Platform driver for UP Board");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("dmi:*:svnAAEON*:rnUP-CHT01:*");
