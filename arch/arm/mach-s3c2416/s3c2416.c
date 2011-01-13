/* linux/arch/arm/mach-s3c2416/s3c2416.c
 *
 * Copyright (c) 2009 Yauhen Kharuzhy <jekhor@gmail.com>,
 *	as part of OpenInkpot project
 * Copyright (c) 2009 Promwad Innovation Company
 *	Yauhen Kharuzhy <yauhen.kharuzhy@promwad.com>
 *
 * Samsung S3C2416 Mobile CPU support
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

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/serial_core.h>
#include <linux/sysdev.h>
#include <linux/clk.h>
#include <linux/io.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/hardware.h>
#include <asm/proc-fns.h>
#include <asm/irq.h>

#include <mach/reset.h>
#include <mach/idle.h>
#include <mach/regs-s3c2443-clock.h>

#include <plat/gpio-core.h>
#include <plat/gpio-cfg.h>
#include <plat/gpio-cfg-helpers.h>
#include <plat/s3c2416.h>
#include <plat/devs.h>
#include <plat/cpu.h>

#include <plat/iic-core.h>
#include <plat/fb-core.h>
#include <plat/nand-core.h>

static struct map_desc s3c2416_iodesc[] __initdata = {
	IODESC_ENT(WATCHDOG),
	IODESC_ENT(CLKPWR),
	IODESC_ENT(TIMER),
};

struct sysdev_class s3c2416_sysclass = {
	.name = "s3c2416-core",
};

static struct sys_device s3c2416_sysdev = {
	.cls		= &s3c2416_sysclass,
};

static void s3c2416_hard_reset(void)
{
	__raw_writel(S3C2443_SWRST_RESET, S3C2443_SWRST);
}

int __init s3c2416_init(void)
{
	printk(KERN_INFO "S3C2416: Initializing architecture\n");

	s3c24xx_reset_hook = s3c2416_hard_reset;
	/* s3c24xx_idle = s3c2416_idle;	*/

	/* change WDT IRQ number */
	s3c_device_wdt.resource[1].start = IRQ_S3C2443_WDT;
	s3c_device_wdt.resource[1].end   = IRQ_S3C2443_WDT;

	/* the i2c devices are directly compatible with s3c2440 */
	s3c_i2c0_setname("s3c2440-i2c");
	s3c_i2c1_setname("s3c2440-i2c");

	s3c_fb_setname("s3c2443-fb");

	return sysdev_register(&s3c2416_sysdev);
}

void __init s3c2416_init_uarts(struct s3c2410_uartcfg *cfg, int no)
{
	s3c24xx_init_uartdevs("s3c2440-uart", s3c2410_uart_resources, cfg, no);

	s3c_nand_setname("s3c2412-nand");
}

/* s3c2416_map_io
 *
 * register the standard cpu IO areas, and any passed in from the
 * machine specific initialisation.
 */

void __init s3c2416_map_io(void)
{
	s3c24xx_gpiocfg_default.set_pull = s3c_gpio_setpull_updown;
	s3c24xx_gpiocfg_default.get_pull = s3c_gpio_getpull_updown;

	iotable_init(s3c2416_iodesc, ARRAY_SIZE(s3c2416_iodesc));
}

/* need to register class before we actually register the device, and
 * we also need to ensure that it has been initialised before any of the
 * drivers even try to use it (even if not on an s3c2416 based system)
 * as a driver which may support both 2443 and 2440 may try and use it.
*/

static int __init s3c2416_core_init(void)
{
	return sysdev_class_register(&s3c2416_sysclass);
}

core_initcall(s3c2416_core_init);
