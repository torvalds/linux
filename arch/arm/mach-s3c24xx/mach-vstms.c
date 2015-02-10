/* linux/arch/arm/mach-s3c2412/mach-vstms.c
 *
 * (C) 2006 Thomas Gleixner <tglx@linutronix.de>
 *
 * Derived from mach-smdk2413.c - (C) 2006 Simtec Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/serial_core.h>
#include <linux/serial_s3c.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/mtd/partitions.h>
#include <linux/memblock.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/hardware.h>
#include <asm/setup.h>
#include <asm/irq.h>
#include <asm/mach-types.h>

#include <mach/regs-gpio.h>
#include <mach/regs-lcd.h>

#include <mach/fb.h>

#include <linux/platform_data/i2c-s3c2410.h>
#include <linux/platform_data/mtd-nand-s3c2410.h>

#include <plat/devs.h>
#include <plat/cpu.h>
#include <plat/samsung-time.h>

#include "common.h"

static struct map_desc vstms_iodesc[] __initdata = {
};

static struct s3c2410_uartcfg vstms_uartcfgs[] __initdata = {
	[0] = {
		.hwport	     = 0,
		.flags	     = 0,
		.ucon	     = 0x3c5,
		.ulcon	     = 0x03,
		.ufcon	     = 0x51,
	},
	[1] = {
		.hwport	     = 1,
		.flags	     = 0,
		.ucon	     = 0x3c5,
		.ulcon	     = 0x03,
		.ufcon	     = 0x51,
	},
	[2] = {
		.hwport	     = 2,
		.flags	     = 0,
		.ucon	     = 0x3c5,
		.ulcon	     = 0x03,
		.ufcon	     = 0x51,
	}
};

static struct mtd_partition __initdata vstms_nand_part[] = {
	[0] = {
		.name	= "Boot Agent",
		.size	= 0x7C000,
		.offset	= 0,
	},
	[1] = {
		.name	= "UBoot Config",
		.offset = 0x7C000,
		.size	= 0x4000,
	},
	[2] = {
		.name	= "Kernel",
		.offset = 0x80000,
		.size	= 0x200000,
	},
	[3] = {
		.name	= "RFS",
		.offset	= 0x280000,
		.size	= 0x3d80000,
	},
};

static struct s3c2410_nand_set __initdata vstms_nand_sets[] = {
	[0] = {
		.name		= "NAND",
		.nr_chips	= 1,
		.nr_partitions	= ARRAY_SIZE(vstms_nand_part),
		.partitions	= vstms_nand_part,
	},
};

/* choose a set of timings which should suit most 512Mbit
 * chips and beyond.
*/

static struct s3c2410_platform_nand __initdata vstms_nand_info = {
	.tacls		= 20,
	.twrph0		= 60,
	.twrph1		= 20,
	.nr_sets	= ARRAY_SIZE(vstms_nand_sets),
	.sets		= vstms_nand_sets,
};

static struct platform_device *vstms_devices[] __initdata = {
	&s3c_device_ohci,
	&s3c_device_wdt,
	&s3c_device_i2c0,
	&s3c_device_iis,
	&s3c_device_rtc,
	&s3c_device_nand,
	&s3c2412_device_dma,
};

static void __init vstms_fixup(struct tag *tags, char **cmdline)
{
	if (tags != phys_to_virt(S3C2410_SDRAM_PA + 0x100)) {
		memblock_add(0x30000000, SZ_64M);
	}
}

static void __init vstms_map_io(void)
{
	s3c24xx_init_io(vstms_iodesc, ARRAY_SIZE(vstms_iodesc));
	s3c24xx_init_uarts(vstms_uartcfgs, ARRAY_SIZE(vstms_uartcfgs));
	samsung_set_timer_source(SAMSUNG_PWM3, SAMSUNG_PWM4);
}

static void __init vstms_init_time(void)
{
	s3c2412_init_clocks(12000000);
	samsung_timer_init();
}

static void __init vstms_init(void)
{
	s3c_i2c0_set_platdata(NULL);
	s3c_nand_set_platdata(&vstms_nand_info);

	platform_add_devices(vstms_devices, ARRAY_SIZE(vstms_devices));
}

MACHINE_START(VSTMS, "VSTMS")
	.atag_offset	= 0x100,

	.fixup		= vstms_fixup,
	.init_irq	= s3c2412_init_irq,
	.init_machine	= vstms_init,
	.map_io		= vstms_map_io,
	.init_time	= vstms_init_time,
MACHINE_END
