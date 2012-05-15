/* linux/arch/arm/plat-s3c24xx/cpu.c
 *
 * Copyright (c) 2004-2005 Simtec Electronics
 *	http://www.simtec.co.uk/products/SWLINUX/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C24XX CPU Support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/


#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/serial_core.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <mach/regs-clock.h>
#include <asm/irq.h>
#include <asm/cacheflush.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/regs-gpio.h>
#include <plat/regs-serial.h>

#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/clock.h>
#include <plat/s3c2410.h>
#include <plat/s3c2412.h>
#include <plat/s3c2416.h>
#include <plat/s3c244x.h>
#include <plat/s3c2443.h>

/* table of supported CPUs */

static const char name_s3c2410[]  = "S3C2410";
static const char name_s3c2412[]  = "S3C2412";
static const char name_s3c2416[]  = "S3C2416/S3C2450";
static const char name_s3c2440[]  = "S3C2440";
static const char name_s3c2442[]  = "S3C2442";
static const char name_s3c2442b[]  = "S3C2442B";
static const char name_s3c2443[]  = "S3C2443";
static const char name_s3c2410a[] = "S3C2410A";
static const char name_s3c2440a[] = "S3C2440A";

static struct cpu_table cpu_ids[] __initdata = {
	{
		.idcode		= 0x32410000,
		.idmask		= 0xffffffff,
		.map_io		= s3c2410_map_io,
		.init_clocks	= s3c2410_init_clocks,
		.init_uarts	= s3c2410_init_uarts,
		.init		= s3c2410_init,
		.name		= name_s3c2410
	},
	{
		.idcode		= 0x32410002,
		.idmask		= 0xffffffff,
		.map_io		= s3c2410_map_io,
		.init_clocks	= s3c2410_init_clocks,
		.init_uarts	= s3c2410_init_uarts,
		.init		= s3c2410a_init,
		.name		= name_s3c2410a
	},
	{
		.idcode		= 0x32440000,
		.idmask		= 0xffffffff,
		.map_io		= s3c2440_map_io,
		.init_clocks	= s3c244x_init_clocks,
		.init_uarts	= s3c244x_init_uarts,
		.init		= s3c2440_init,
		.name		= name_s3c2440
	},
	{
		.idcode		= 0x32440001,
		.idmask		= 0xffffffff,
		.map_io		= s3c2440_map_io,
		.init_clocks	= s3c244x_init_clocks,
		.init_uarts	= s3c244x_init_uarts,
		.init		= s3c2440_init,
		.name		= name_s3c2440a
	},
	{
		.idcode		= 0x32440aaa,
		.idmask		= 0xffffffff,
		.map_io		= s3c2442_map_io,
		.init_clocks	= s3c244x_init_clocks,
		.init_uarts	= s3c244x_init_uarts,
		.init		= s3c2442_init,
		.name		= name_s3c2442
	},
	{
		.idcode		= 0x32440aab,
		.idmask		= 0xffffffff,
		.map_io		= s3c2442_map_io,
		.init_clocks	= s3c244x_init_clocks,
		.init_uarts	= s3c244x_init_uarts,
		.init		= s3c2442_init,
		.name		= name_s3c2442b
	},
	{
		.idcode		= 0x32412001,
		.idmask		= 0xffffffff,
		.map_io		= s3c2412_map_io,
		.init_clocks	= s3c2412_init_clocks,
		.init_uarts	= s3c2412_init_uarts,
		.init		= s3c2412_init,
		.name		= name_s3c2412,
	},
	{			/* a newer version of the s3c2412 */
		.idcode		= 0x32412003,
		.idmask		= 0xffffffff,
		.map_io		= s3c2412_map_io,
		.init_clocks	= s3c2412_init_clocks,
		.init_uarts	= s3c2412_init_uarts,
		.init		= s3c2412_init,
		.name		= name_s3c2412,
	},
	{			/* a strange version of the s3c2416 */
		.idcode		= 0x32450003,
		.idmask		= 0xffffffff,
		.map_io		= s3c2416_map_io,
		.init_clocks	= s3c2416_init_clocks,
		.init_uarts	= s3c2416_init_uarts,
		.init		= s3c2416_init,
		.name		= name_s3c2416,
	},
	{
		.idcode		= 0x32443001,
		.idmask		= 0xffffffff,
		.map_io		= s3c2443_map_io,
		.init_clocks	= s3c2443_init_clocks,
		.init_uarts	= s3c2443_init_uarts,
		.init		= s3c2443_init,
		.name		= name_s3c2443,
	},
};

/* minimal IO mapping */

static struct map_desc s3c_iodesc[] __initdata = {
	IODESC_ENT(GPIO),
	IODESC_ENT(IRQ),
	IODESC_ENT(MEMCTRL),
	IODESC_ENT(UART)
};

/* read cpu identificaiton code */

static unsigned long s3c24xx_read_idcode_v5(void)
{
#if defined(CONFIG_CPU_S3C2416)
	/* s3c2416 is v5, with S3C24XX_GSTATUS1 instead of S3C2412_GSTATUS1 */

	u32 gs = __raw_readl(S3C24XX_GSTATUS1);

	/* test for s3c2416 or similar device */
	if ((gs >> 16) == 0x3245)
		return gs;
#endif

#if defined(CONFIG_CPU_S3C2412) || defined(CONFIG_CPU_S3C2413)
	return __raw_readl(S3C2412_GSTATUS1);
#else
	return 1UL;	/* don't look like an 2400 */
#endif
}

static unsigned long s3c24xx_read_idcode_v4(void)
{
	return __raw_readl(S3C2410_GSTATUS1);
}

static void s3c24xx_default_idle(void)
{
	unsigned long tmp;
	int i;

	/* idle the system by using the idle mode which will wait for an
	 * interrupt to happen before restarting the system.
	 */

	/* Warning: going into idle state upsets jtag scanning */

	__raw_writel(__raw_readl(S3C2410_CLKCON) | S3C2410_CLKCON_IDLE,
		     S3C2410_CLKCON);

	/* the samsung port seems to do a loop and then unset idle.. */
	for (i = 0; i < 50; i++)
		tmp += __raw_readl(S3C2410_CLKCON); /* ensure loop not optimised out */

	/* this bit is not cleared on re-start... */

	__raw_writel(__raw_readl(S3C2410_CLKCON) & ~S3C2410_CLKCON_IDLE,
		     S3C2410_CLKCON);
}

void __init s3c24xx_init_io(struct map_desc *mach_desc, int size)
{
	arm_pm_idle = s3c24xx_default_idle;

	/* initialise the io descriptors we need for initialisation */
	iotable_init(mach_desc, size);
	iotable_init(s3c_iodesc, ARRAY_SIZE(s3c_iodesc));

	if (cpu_architecture() >= CPU_ARCH_ARMv5) {
		samsung_cpu_id = s3c24xx_read_idcode_v5();
	} else {
		samsung_cpu_id = s3c24xx_read_idcode_v4();
	}
	s3c24xx_init_cpu();

	s3c_init_cpu(samsung_cpu_id, cpu_ids, ARRAY_SIZE(cpu_ids));
}
