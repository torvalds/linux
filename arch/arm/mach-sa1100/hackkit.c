// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/arch/arm/mach-sa1100/hackkit.c
 *
 * Copyright (C) 2002 Stefan Eletzhofer <stefan.eletzhofer@eletztrick.de>
 *
 * This file contains all HackKit tweaks. Based on original work from
 * Nicolas Pitre's assabet fixes
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/cpufreq.h>
#include <linux/platform_data/sa11x0-serial.h>
#include <linux/serial_core.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/tty.h>
#include <linux/gpio.h>
#include <linux/leds.h>
#include <linux/platform_device.h>

#include <asm/mach-types.h>
#include <asm/setup.h>
#include <asm/page.h>
#include <asm/pgtable.h>

#include <asm/mach/arch.h>
#include <asm/mach/flash.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/hardware.h>
#include <mach/irqs.h>

#include "generic.h"

/**********************************************************************
 *  prototypes
 */

/* init funcs */
static void __init hackkit_map_io(void);

static void hackkit_uart_pm(struct uart_port *port, u_int state, u_int oldstate);

/**********************************************************************
 *  global data
 */

/**********************************************************************
 *  static data
 */

static struct map_desc hackkit_io_desc[] __initdata = {
	{	/* Flash bank 0 */
		.virtual	=  0xe8000000,
		.pfn		= __phys_to_pfn(0x00000000),
		.length		= 0x01000000,
		.type		= MT_DEVICE
	},
};

static struct sa1100_port_fns hackkit_port_fns __initdata = {
	.pm		= hackkit_uart_pm,
};

/**********************************************************************
 *  Static functions
 */

static void __init hackkit_map_io(void)
{
	sa1100_map_io();
	iotable_init(hackkit_io_desc, ARRAY_SIZE(hackkit_io_desc));

	sa1100_register_uart_fns(&hackkit_port_fns);
	sa1100_register_uart(0, 1);	/* com port */
	sa1100_register_uart(1, 2);
	sa1100_register_uart(2, 3);	/* radio module */

	Ser1SDCR0 |= SDCR0_SUS;
}

/**
 *	hackkit_uart_pm - powermgmt callback function for system 3 UART
 *	@port: uart port structure
 *	@state: pm state
 *	@oldstate: old pm state
 *
 */
static void hackkit_uart_pm(struct uart_port *port, u_int state, u_int oldstate)
{
	/* TODO: switch on/off uart in powersave mode */
}

static struct mtd_partition hackkit_partitions[] = {
	{
		.name		= "BLOB",
		.size		= 0x00040000,
		.offset		= 0x00000000,
		.mask_flags	= MTD_WRITEABLE,  /* force read-only */
	}, {
		.name		= "config",
		.size		= 0x00040000,
		.offset		= MTDPART_OFS_APPEND,
	}, {
		.name		= "kernel",
		.size		= 0x00100000,
		.offset		= MTDPART_OFS_APPEND,
	}, {
		.name		= "initrd",
		.size		= 0x00180000,
		.offset		= MTDPART_OFS_APPEND,
	}, {
		.name		= "rootfs",
		.size		= 0x700000,
		.offset		= MTDPART_OFS_APPEND,
	}, {
		.name		= "data",
		.size		= MTDPART_SIZ_FULL,
		.offset		= MTDPART_OFS_APPEND,
	}
};

static struct flash_platform_data hackkit_flash_data = {
	.map_name	= "cfi_probe",
	.parts		= hackkit_partitions,
	.nr_parts	= ARRAY_SIZE(hackkit_partitions),
};

static struct resource hackkit_flash_resource =
	DEFINE_RES_MEM(SA1100_CS0_PHYS, SZ_32M);

/* LEDs */
struct gpio_led hackkit_gpio_leds[] = {
	{
		.name			= "hackkit:red",
		.default_trigger	= "cpu0",
		.gpio			= 22,
	},
	{
		.name			= "hackkit:green",
		.default_trigger	= "heartbeat",
		.gpio			= 23,
	},
};

static struct gpio_led_platform_data hackkit_gpio_led_info = {
	.leds		= hackkit_gpio_leds,
	.num_leds	= ARRAY_SIZE(hackkit_gpio_leds),
};

static struct platform_device hackkit_leds = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data	= &hackkit_gpio_led_info,
	}
};

static void __init hackkit_init(void)
{
	sa11x0_register_mtd(&hackkit_flash_data, &hackkit_flash_resource, 1);
	platform_device_register(&hackkit_leds);
}

/**********************************************************************
 *  Exported Functions
 */

MACHINE_START(HACKKIT, "HackKit Cpu Board")
	.atag_offset	= 0x100,
	.map_io		= hackkit_map_io,
	.nr_irqs	= SA1100_NR_IRQS,
	.init_irq	= sa1100_init_irq,
	.init_time	= sa1100_timer_init,
	.init_machine	= hackkit_init,
	.init_late	= sa11x0_init_late,
	.restart	= sa11x0_restart,
MACHINE_END
