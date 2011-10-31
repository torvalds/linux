/*
 * linux/arch/arm/mach-sa1100/hackkit.c
 *
 * Copyright (C) 2002 Stefan Eletzhofer <stefan.eletzhofer@eletztrick.de>
 *
 * This file contains all HackKit tweaks. Based on original work from
 * Nicolas Pitre's assabet fixes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/cpufreq.h>
#include <linux/serial_core.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/setup.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/irq.h>

#include <asm/mach/arch.h>
#include <asm/mach/flash.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/mach/serial_sa1100.h>

#include "generic.h"

/**********************************************************************
 *  prototypes
 */

/* init funcs */
static void __init hackkit_map_io(void);

static u_int hackkit_get_mctrl(struct uart_port *port);
static void hackkit_set_mctrl(struct uart_port *port, u_int mctrl);
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
	.set_mctrl	= hackkit_set_mctrl,
	.get_mctrl	= hackkit_get_mctrl,
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

/*
 * Note! this can be called from IRQ context.
 * FIXME: No modem ctrl lines yet.
 */
static void hackkit_set_mctrl(struct uart_port *port, u_int mctrl)
{
#if 0
	if (port->mapbase == _Ser1UTCR0) {
		u_int set = 0, clear = 0;

		if (mctrl & TIOCM_RTS)
			set |= PT_CTRL2_RS1_RTS;
		else
			clear |= PT_CTRL2_RS1_RTS;

		if (mctrl & TIOCM_DTR)
			set |= PT_CTRL2_RS1_DTR;
		else
			clear |= PT_CTRL2_RS1_DTR;

		PTCTRL2_clear(clear);
		PTCTRL2_set(set);
	}
#endif
}

static u_int hackkit_get_mctrl(struct uart_port *port)
{
	u_int ret = 0;
#if 0
	u_int irqsr = PT_IRQSR;

	/* need 2 reads to read current value */
	irqsr = PT_IRQSR;

	/* TODO: check IRQ source register for modem/com
	 status lines and set them correctly. */
#endif

	ret = TIOCM_CD | TIOCM_CTS | TIOCM_DSR;

	return ret;
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

static struct resource hackkit_flash_resource = {
	.start		= SA1100_CS0_PHYS,
	.end		= SA1100_CS0_PHYS + SZ_32M,
	.flags		= IORESOURCE_MEM,
};

static void __init hackkit_init(void)
{
	sa11x0_register_mtd(&hackkit_flash_data, &hackkit_flash_resource, 1);
}

/**********************************************************************
 *  Exported Functions
 */

MACHINE_START(HACKKIT, "HackKit Cpu Board")
	.atag_offset	= 0x100,
	.map_io		= hackkit_map_io,
	.init_irq	= sa1100_init_irq,
	.timer		= &sa1100_timer,
	.init_machine	= hackkit_init,
MACHINE_END
