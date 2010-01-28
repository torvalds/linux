/*
 * Copyright (C) 2009 Integration Software and Electronic Engineering.
 *
 * Modified from mach-omap2/board-generic.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>

#include <linux/regulator/machine.h>
#include <linux/i2c/twl.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <plat/board.h>
#include <plat/common.h>
#include <plat/gpmc.h>
#include <plat/usb.h>

#include "mux.h"
#include "mmc-twl4030.h"

#define IGEP2_SMSC911X_CS       5
#define IGEP2_SMSC911X_GPIO     176
#define IGEP2_GPIO_USBH_NRESET  24
#define IGEP2_GPIO_LED0_RED 	26
#define IGEP2_GPIO_LED0_GREEN 	27
#define IGEP2_GPIO_LED1_RED   	28

#if defined(CONFIG_SMSC911X) || defined(CONFIG_SMSC911X_MODULE)

#include <linux/smsc911x.h>

static struct smsc911x_platform_config igep2_smsc911x_config = {
	.irq_polarity	= SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type	= SMSC911X_IRQ_TYPE_OPEN_DRAIN,
	.flags		= SMSC911X_USE_32BIT | SMSC911X_SAVE_MAC_ADDRESS  ,
	.phy_interface	= PHY_INTERFACE_MODE_MII,
};

static struct resource igep2_smsc911x_resources[] = {
	{
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= OMAP_GPIO_IRQ(IGEP2_SMSC911X_GPIO),
		.end	= OMAP_GPIO_IRQ(IGEP2_SMSC911X_GPIO),
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_LOWLEVEL,
	},
};

static struct platform_device igep2_smsc911x_device = {
	.name		= "smsc911x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(igep2_smsc911x_resources),
	.resource	= igep2_smsc911x_resources,
	.dev		= {
		.platform_data = &igep2_smsc911x_config,
	},
};

static inline void __init igep2_init_smsc911x(void)
{
	unsigned long cs_mem_base;

	if (gpmc_cs_request(IGEP2_SMSC911X_CS, SZ_16M, &cs_mem_base) < 0) {
		pr_err("IGEP v2: Failed request for GPMC mem for smsc911x\n");
		gpmc_cs_free(IGEP2_SMSC911X_CS);
		return;
	}

	igep2_smsc911x_resources[0].start = cs_mem_base + 0x0;
	igep2_smsc911x_resources[0].end   = cs_mem_base + 0xff;

	if ((gpio_request(IGEP2_SMSC911X_GPIO, "SMSC911X IRQ") == 0) &&
	    (gpio_direction_input(IGEP2_SMSC911X_GPIO) == 0)) {
		gpio_export(IGEP2_SMSC911X_GPIO, 0);
	} else {
		pr_err("IGEP v2: Could not obtain gpio for for SMSC911X IRQ\n");
		return;
	}

	platform_device_register(&igep2_smsc911x_device);
}

#else
static inline void __init igep2_init_smsc911x(void) { }
#endif

static struct omap_board_config_kernel igep2_config[] __initdata = {
};

static struct regulator_consumer_supply igep2_vmmc1_supply = {
	.supply		= "vmmc",
};

/* VMMC1 for OMAP VDD_MMC1 (i/o) and MMC1 card */
static struct regulator_init_data igep2_vmmc1 = {
	.constraints = {
		.min_uV			= 1850000,
		.max_uV			= 3150000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_VOLTAGE
					| REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies  = 1,
	.consumer_supplies      = &igep2_vmmc1_supply,
};

static struct twl4030_hsmmc_info mmc[] = {
	{
		.mmc		= 1,
		.wires		= 4,
		.gpio_cd	= -EINVAL,
		.gpio_wp	= -EINVAL,
	},
	{
		.mmc		= 2,
		.wires		= 4,
		.gpio_cd	= -EINVAL,
		.gpio_wp	= -EINVAL,
	},
	{}      /* Terminator */
};

static int igep2_twl_gpio_setup(struct device *dev,
		unsigned gpio, unsigned ngpio)
{
	/* gpio + 0 is "mmc0_cd" (input/IRQ) */
	mmc[0].gpio_cd = gpio + 0;
	twl4030_mmc_init(mmc);

	/* link regulators to MMC adapters ... we "know" the
	 * regulators will be set up only *after* we return.
	*/
	igep2_vmmc1_supply.dev = mmc[0].dev;

	return 0;
};

static struct twl4030_gpio_platform_data igep2_gpio_data = {
	.gpio_base	= OMAP_MAX_GPIO_LINES,
	.irq_base	= TWL4030_GPIO_IRQ_BASE,
	.irq_end	= TWL4030_GPIO_IRQ_END,
	.use_leds	= false,
	.setup		= igep2_twl_gpio_setup,
};

static struct twl4030_usb_data igep2_usb_data = {
	.usb_mode	= T2_USB_MODE_ULPI,
};

static void __init igep2_init_irq(void)
{
	omap_board_config = igep2_config;
	omap_board_config_size = ARRAY_SIZE(igep2_config);
	omap2_init_common_hw(NULL, NULL);
	omap_init_irq();
	omap_gpio_init();
}

static struct twl4030_platform_data igep2_twldata = {
	.irq_base	= TWL4030_IRQ_BASE,
	.irq_end	= TWL4030_IRQ_END,

	/* platform_data for children goes here */
	.usb		= &igep2_usb_data,
	.gpio		= &igep2_gpio_data,
	.vmmc1          = &igep2_vmmc1,

};

static struct i2c_board_info __initdata igep2_i2c_boardinfo[] = {
	{
		I2C_BOARD_INFO("twl4030", 0x48),
		.flags		= I2C_CLIENT_WAKE,
		.irq		= INT_34XX_SYS_NIRQ,
		.platform_data	= &igep2_twldata,
	},
};

static int __init igep2_i2c_init(void)
{
	omap_register_i2c_bus(1, 2600, igep2_i2c_boardinfo,
			ARRAY_SIZE(igep2_i2c_boardinfo));
	/* Bus 3 is attached to the DVI port where devices like the pico DLP
	 * projector don't work reliably with 400kHz */
	omap_register_i2c_bus(3, 100, NULL, 0);
	return 0;
}

#ifdef CONFIG_OMAP_MUX
static struct omap_board_mux board_mux[] __initdata = {
	{ .reg_offset = OMAP_MUX_TERMINATOR },
};
#else
#define board_mux	NULL
#endif

static void __init igep2_init(void)
{
	omap3_mux_init(board_mux, OMAP_PACKAGE_CBB);
	igep2_i2c_init();
	omap_serial_init();
	usb_musb_init();

	igep2_init_smsc911x();

	/* GPIO userspace leds */
	if ((gpio_request(IGEP2_GPIO_LED0_RED, "GPIO_LED0_RED") == 0) &&
	    (gpio_direction_output(IGEP2_GPIO_LED0_RED, 1) == 0)) {
		gpio_export(IGEP2_GPIO_LED0_RED, 0);
		gpio_set_value(IGEP2_GPIO_LED0_RED, 0);
	} else
		pr_warning("IGEP v2: Could not obtain gpio GPIO_LED0_RED\n");

	if ((gpio_request(IGEP2_GPIO_LED0_GREEN, "GPIO_LED0_GREEN") == 0) &&
	    (gpio_direction_output(IGEP2_GPIO_LED0_GREEN, 1) == 0)) {
		gpio_export(IGEP2_GPIO_LED0_GREEN, 0);
		gpio_set_value(IGEP2_GPIO_LED0_GREEN, 0);
	} else
		pr_warning("IGEP v2: Could not obtain gpio GPIO_LED0_GREEN\n");

	if ((gpio_request(IGEP2_GPIO_LED1_RED, "GPIO_LED1_RED") == 0) &&
	    (gpio_direction_output(IGEP2_GPIO_LED1_RED, 1) == 0)) {
		gpio_export(IGEP2_GPIO_LED1_RED, 0);
		gpio_set_value(IGEP2_GPIO_LED1_RED, 0);
	} else
		pr_warning("IGEP v2: Could not obtain gpio GPIO_LED1_RED\n");
}

static void __init igep2_map_io(void)
{
	omap2_set_globals_343x();
	omap2_map_common_io();
}

MACHINE_START(IGEP0020, "IGEP v2 board")
	.phys_io	= 0x48000000,
	.io_pg_offst	= ((0xfa000000) >> 18) & 0xfffc,
	.boot_params	= 0x80000100,
	.map_io		= igep2_map_io,
	.init_irq	= igep2_init_irq,
	.init_machine	= igep2_init,
	.timer		= &omap_timer,
MACHINE_END
