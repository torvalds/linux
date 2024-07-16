// SPDX-License-Identifier: GPL-2.0
//
// (C) 2006 Thomas Gleixner <tglx@linutronix.de>
//
// Derived from mach-smdk2413.c - (C) 2006 Simtec Electronics

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
#include <linux/mtd/rawnand.h>
#include <linux/mtd/nand-ecc-sw-hamming.h>
#include <linux/mtd/partitions.h>
#include <linux/memblock.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/setup.h>
#include <asm/irq.h>
#include <asm/mach-types.h>

#include "regs-gpio.h"
#include "gpio-samsung.h"
#include "gpio-cfg.h"

#include <linux/platform_data/fb-s3c2410.h>

#include <linux/platform_data/i2c-s3c2410.h>
#include <linux/platform_data/mtd-nand-s3c2410.h>

#include "devs.h"
#include "cpu.h"

#include "s3c24xx.h"

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
	.engine_type	= NAND_ECC_ENGINE_TYPE_SOFT,
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
	s3c24xx_set_timer_source(S3C24XX_PWM3, S3C24XX_PWM4);
}

static void __init vstms_init_time(void)
{
	s3c2412_init_clocks(12000000);
	s3c24xx_timer_init();
}

static void __init vstms_init(void)
{
	s3c_i2c0_set_platdata(NULL);
	s3c_nand_set_platdata(&vstms_nand_info);
	/* Configure the I2S pins (GPE0...GPE4) in correct mode */
	s3c_gpio_cfgall_range(S3C2410_GPE(0), 5, S3C_GPIO_SFN(2),
			      S3C_GPIO_PULL_NONE);
	platform_add_devices(vstms_devices, ARRAY_SIZE(vstms_devices));
}

MACHINE_START(VSTMS, "VSTMS")
	.atag_offset	= 0x100,
	.nr_irqs	= NR_IRQS_S3C2412,

	.fixup		= vstms_fixup,
	.init_irq	= s3c2412_init_irq,
	.init_machine	= vstms_init,
	.map_io		= vstms_map_io,
	.init_time	= vstms_init_time,
MACHINE_END
