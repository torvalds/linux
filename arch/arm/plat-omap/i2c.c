/*
 * linux/arch/arm/plat-omap/i2c.c
 *
 * Helper module for board specific I2C bus registration
 *
 * Copyright (C) 2007 Nokia Corporation.
 *
 * Contact: Jarkko Nikula <jarkko.nikula@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <mach/irqs.h>
#include <mach/mux.h>

#define OMAP_I2C_SIZE		0x3f
#define OMAP1_I2C_BASE		0xfffb3800
#define OMAP2_I2C_BASE1		0x48070000
#define OMAP2_I2C_BASE2		0x48072000
#define OMAP2_I2C_BASE3		0x48060000

static const char name[] = "i2c_omap";

#define I2C_RESOURCE_BUILDER(base, irq)			\
	{						\
		.start	= (base),			\
		.end	= (base) + OMAP_I2C_SIZE,	\
		.flags	= IORESOURCE_MEM,		\
	},						\
	{						\
		.start	= (irq),			\
		.flags	= IORESOURCE_IRQ,		\
	},

static struct resource i2c_resources[][2] = {
	{ I2C_RESOURCE_BUILDER(0, 0) },
#if	defined(CONFIG_ARCH_OMAP24XX) || defined(CONFIG_ARCH_OMAP34XX)
	{ I2C_RESOURCE_BUILDER(OMAP2_I2C_BASE2, INT_24XX_I2C2_IRQ) },
#endif
#if	defined(CONFIG_ARCH_OMAP34XX)
	{ I2C_RESOURCE_BUILDER(OMAP2_I2C_BASE3, INT_34XX_I2C3_IRQ) },
#endif
};

#define I2C_DEV_BUILDER(bus_id, res, data)		\
	{						\
		.id	= (bus_id),			\
		.name	= name,				\
		.num_resources	= ARRAY_SIZE(res),	\
		.resource	= (res),		\
		.dev		= {			\
			.platform_data	= (data),	\
		},					\
	}

static u32 i2c_rate[ARRAY_SIZE(i2c_resources)];
static struct platform_device omap_i2c_devices[] = {
	I2C_DEV_BUILDER(1, i2c_resources[0], &i2c_rate[0]),
#if	defined(CONFIG_ARCH_OMAP24XX) || defined(CONFIG_ARCH_OMAP34XX)
	I2C_DEV_BUILDER(2, i2c_resources[1], &i2c_rate[1]),
#endif
#if	defined(CONFIG_ARCH_OMAP34XX)
	I2C_DEV_BUILDER(3, i2c_resources[2], &i2c_rate[2]),
#endif
};

#if defined(CONFIG_ARCH_OMAP24XX)
static const int omap24xx_pins[][2] = {
	{ M19_24XX_I2C1_SCL, L15_24XX_I2C1_SDA },
	{ J15_24XX_I2C2_SCL, H19_24XX_I2C2_SDA },
};
#else
static const int omap24xx_pins[][2] = {};
#endif
#if defined(CONFIG_ARCH_OMAP34XX)
static const int omap34xx_pins[][2] = {
	{ K21_34XX_I2C1_SCL, J21_34XX_I2C1_SDA},
	{ AF15_34XX_I2C2_SCL, AE15_34XX_I2C2_SDA},
	{ AF14_34XX_I2C3_SCL, AG14_34XX_I2C3_SDA},
};
#else
static const int omap34xx_pins[][2] = {};
#endif

#define OMAP_I2C_CMDLINE_SETUP	(BIT(31))

static void __init omap_i2c_mux_pins(int bus)
{
	int scl, sda;

	if (cpu_class_is_omap1()) {
		scl = I2C_SCL;
		sda = I2C_SDA;
	} else if (cpu_is_omap24xx()) {
		scl = omap24xx_pins[bus][0];
		sda = omap24xx_pins[bus][1];
	} else if (cpu_is_omap34xx()) {
		scl = omap34xx_pins[bus][0];
		sda = omap34xx_pins[bus][1];
	} else {
		return;
	}

	omap_cfg_reg(sda);
	omap_cfg_reg(scl);
}

static int __init omap_i2c_nr_ports(void)
{
	int ports = 0;

	if (cpu_class_is_omap1())
		ports = 1;
	else if (cpu_is_omap24xx())
		ports = 2;
	else if (cpu_is_omap34xx())
		ports = 3;

	return ports;
}

static int __init omap_i2c_add_bus(int bus_id)
{
	struct platform_device *pdev;
	struct resource *res;
	resource_size_t base, irq;

	pdev = &omap_i2c_devices[bus_id - 1];
	if (bus_id == 1) {
		res = pdev->resource;
		if (cpu_class_is_omap1()) {
			base = OMAP1_I2C_BASE;
			irq = INT_I2C;
		} else {
			base = OMAP2_I2C_BASE1;
			irq = INT_24XX_I2C1_IRQ;
		}
		res[0].start = base;
		res[0].end = base + OMAP_I2C_SIZE;
		res[1].start = irq;
	}

	omap_i2c_mux_pins(bus_id - 1);
	return platform_device_register(pdev);
}

/**
 * omap_i2c_bus_setup - Process command line options for the I2C bus speed
 * @str: String of options
 *
 * This function allow to override the default I2C bus speed for given I2C
 * bus with a command line option.
 *
 * Format: i2c_bus=bus_id,clkrate (in kHz)
 *
 * Returns 1 on success, 0 otherwise.
 */
static int __init omap_i2c_bus_setup(char *str)
{
	int ports;
	int ints[3];

	ports = omap_i2c_nr_ports();
	get_options(str, 3, ints);
	if (ints[0] < 2 || ints[1] < 1 || ints[1] > ports)
		return 0;
	i2c_rate[ints[1] - 1] = ints[2];
	i2c_rate[ints[1] - 1] |= OMAP_I2C_CMDLINE_SETUP;

	return 1;
}
__setup("i2c_bus=", omap_i2c_bus_setup);

/*
 * Register busses defined in command line but that are not registered with
 * omap_register_i2c_bus from board initialization code.
 */
static int __init omap_register_i2c_bus_cmdline(void)
{
	int i, err = 0;

	for (i = 0; i < ARRAY_SIZE(i2c_rate); i++)
		if (i2c_rate[i] & OMAP_I2C_CMDLINE_SETUP) {
			i2c_rate[i] &= ~OMAP_I2C_CMDLINE_SETUP;
			err = omap_i2c_add_bus(i + 1);
			if (err)
				goto out;
		}

out:
	return err;
}
subsys_initcall(omap_register_i2c_bus_cmdline);

/**
 * omap_register_i2c_bus - register I2C bus with device descriptors
 * @bus_id: bus id counting from number 1
 * @clkrate: clock rate of the bus in kHz
 * @info: pointer into I2C device descriptor table or NULL
 * @len: number of descriptors in the table
 *
 * Returns 0 on success or an error code.
 */
int __init omap_register_i2c_bus(int bus_id, u32 clkrate,
			  struct i2c_board_info const *info,
			  unsigned len)
{
	int err;

	BUG_ON(bus_id < 1 || bus_id > omap_i2c_nr_ports());

	if (info) {
		err = i2c_register_board_info(bus_id, info, len);
		if (err)
			return err;
	}

	if (!i2c_rate[bus_id - 1])
		i2c_rate[bus_id - 1] = clkrate;
	i2c_rate[bus_id - 1] &= ~OMAP_I2C_CMDLINE_SETUP;

	return omap_i2c_add_bus(bus_id);
}
