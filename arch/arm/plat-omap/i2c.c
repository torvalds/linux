/*
 * linux/arch/arm/plat-omap/i2c.c
 *
 * Helper module for board specific I2C bus registration
 *
 * Copyright (C) 2007 Nokia Corporation.
 *
 * Contact: Jarkko Nikula <jhnikula@gmail.com>
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
#include <linux/i2c-omap.h>

#include <mach/irqs.h>
#include <plat/mux.h>
#include <plat/i2c.h>
#include <plat/omap-pm.h>

#define OMAP_I2C_SIZE		0x3f
#define OMAP1_I2C_BASE		0xfffb3800
#define OMAP2_I2C_BASE1		0x48070000
#define OMAP2_I2C_BASE2		0x48072000
#define OMAP2_I2C_BASE3		0x48060000
#define OMAP4_I2C_BASE4		0x48350000

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
#if	defined(CONFIG_ARCH_OMAP2PLUS)
	{ I2C_RESOURCE_BUILDER(OMAP2_I2C_BASE2, 0) },
#endif
#if	defined(CONFIG_ARCH_OMAP3) || defined(CONFIG_ARCH_OMAP4)
	{ I2C_RESOURCE_BUILDER(OMAP2_I2C_BASE3, 0) },
#endif
#if	defined(CONFIG_ARCH_OMAP4)
	{ I2C_RESOURCE_BUILDER(OMAP4_I2C_BASE4, 0) },
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

static struct omap_i2c_bus_platform_data i2c_pdata[ARRAY_SIZE(i2c_resources)];
static struct platform_device omap_i2c_devices[] = {
	I2C_DEV_BUILDER(1, i2c_resources[0], &i2c_pdata[0]),
#if	defined(CONFIG_ARCH_OMAP2PLUS)
	I2C_DEV_BUILDER(2, i2c_resources[1], &i2c_pdata[1]),
#endif
#if	defined(CONFIG_ARCH_OMAP3) || defined(CONFIG_ARCH_OMAP4)
	I2C_DEV_BUILDER(3, i2c_resources[2], &i2c_pdata[2]),
#endif
#if	defined(CONFIG_ARCH_OMAP4)
	I2C_DEV_BUILDER(4, i2c_resources[3], &i2c_pdata[3]),
#endif
};

#define OMAP_I2C_CMDLINE_SETUP	(BIT(31))

static int __init omap_i2c_nr_ports(void)
{
	int ports = 0;

	if (cpu_class_is_omap1())
		ports = 1;
	else if (cpu_is_omap24xx())
		ports = 2;
	else if (cpu_is_omap34xx())
		ports = 3;
	else if (cpu_is_omap44xx())
		ports = 4;

	return ports;
}

/* Shared between omap2 and 3 */
static resource_size_t omap2_i2c_irq[3] __initdata = {
	INT_24XX_I2C1_IRQ,
	INT_24XX_I2C2_IRQ,
	INT_34XX_I2C3_IRQ,
};

static resource_size_t omap4_i2c_irq[4] __initdata = {
	OMAP44XX_IRQ_I2C1,
	OMAP44XX_IRQ_I2C2,
	OMAP44XX_IRQ_I2C3,
	OMAP44XX_IRQ_I2C4,
};

static inline int omap1_i2c_add_bus(struct platform_device *pdev, int bus_id)
{
	struct omap_i2c_bus_platform_data *pd;
	struct resource *res;

	pd = pdev->dev.platform_data;
	res = pdev->resource;
	res[0].start = OMAP1_I2C_BASE;
	res[0].end = res[0].start + OMAP_I2C_SIZE;
	res[1].start = INT_I2C;
	omap1_i2c_mux_pins(bus_id);

	return platform_device_register(pdev);
}

/*
 * XXX This function is a temporary compatibility wrapper - only
 * needed until the I2C driver can be converted to call
 * omap_pm_set_max_dev_wakeup_lat() and handle a return code.
 */
static void omap_pm_set_max_mpu_wakeup_lat_compat(struct device *dev, long t)
{
	omap_pm_set_max_mpu_wakeup_lat(dev, t);
}

static inline int omap2_i2c_add_bus(struct platform_device *pdev, int bus_id)
{
	struct resource *res;
	resource_size_t *irq;

	res = pdev->resource;

	if (!cpu_is_omap44xx())
		irq = omap2_i2c_irq;
	else
		irq = omap4_i2c_irq;

	if (bus_id == 1) {
		res[0].start = OMAP2_I2C_BASE1;
		res[0].end = res[0].start + OMAP_I2C_SIZE;
	}

	res[1].start = irq[bus_id - 1];
	omap2_i2c_mux_pins(bus_id);

	/*
	 * When waiting for completion of a i2c transfer, we need to
	 * set a wake up latency constraint for the MPU. This is to
	 * ensure quick enough wakeup from idle, when transfer
	 * completes.
	 */
	if (cpu_is_omap34xx()) {
		struct omap_i2c_bus_platform_data *pd;

		pd = pdev->dev.platform_data;
		pd->set_mpu_wkup_lat = omap_pm_set_max_mpu_wakeup_lat_compat;
	}

	return platform_device_register(pdev);
}

static int __init omap_i2c_add_bus(int bus_id)
{
	struct platform_device *pdev;

	pdev = &omap_i2c_devices[bus_id - 1];

	if (cpu_class_is_omap1())
		return omap1_i2c_add_bus(pdev, bus_id);
	else
		return omap2_i2c_add_bus(pdev, bus_id);
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
	i2c_pdata[ints[1] - 1].clkrate = ints[2];
	i2c_pdata[ints[1] - 1].clkrate |= OMAP_I2C_CMDLINE_SETUP;

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

	for (i = 0; i < ARRAY_SIZE(i2c_pdata); i++)
		if (i2c_pdata[i].clkrate & OMAP_I2C_CMDLINE_SETUP) {
			i2c_pdata[i].clkrate &= ~OMAP_I2C_CMDLINE_SETUP;
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

	if (!i2c_pdata[bus_id - 1].clkrate)
		i2c_pdata[bus_id - 1].clkrate = clkrate;

	i2c_pdata[bus_id - 1].clkrate &= ~OMAP_I2C_CMDLINE_SETUP;

	return omap_i2c_add_bus(bus_id);
}
