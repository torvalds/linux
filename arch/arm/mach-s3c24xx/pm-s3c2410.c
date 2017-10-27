/* linux/arch/arm/mach-s3c2410/pm.c
 *
 * Copyright (c) 2006 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2410 (and compatible) Power Manager (Suspend-To-RAM) support
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
#include <linux/suspend.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/device.h>
#include <linux/syscore_ops.h>
#include <linux/gpio.h>
#include <linux/io.h>

#include <asm/mach-types.h>

#include <mach/hardware.h>
#include <mach/regs-gpio.h>
#include <mach/gpio-samsung.h>

#include <plat/gpio-cfg.h>
#include <plat/cpu.h>
#include <plat/pm.h>

#include "h1940.h"

static void s3c2410_pm_prepare(void)
{
	/* ensure at least GSTATUS3 has the resume address */

	__raw_writel(__pa_symbol(s3c_cpu_resume), S3C2410_GSTATUS3);

	S3C_PMDBG("GSTATUS3 0x%08x\n", __raw_readl(S3C2410_GSTATUS3));
	S3C_PMDBG("GSTATUS4 0x%08x\n", __raw_readl(S3C2410_GSTATUS4));

	if (machine_is_h1940()) {
		void *base = phys_to_virt(H1940_SUSPEND_CHECK);
		unsigned long ptr;
		unsigned long calc = 0;

		/* generate check for the bootloader to check on resume */

		for (ptr = 0; ptr < 0x40000; ptr += 0x400)
			calc += __raw_readl(base+ptr);

		__raw_writel(calc, phys_to_virt(H1940_SUSPEND_CHECKSUM));
	}

	/* RX3715 and RX1950 use similar to H1940 code and the
	 * same offsets for resume and checksum pointers */

	if (machine_is_rx3715() || machine_is_rx1950()) {
		void *base = phys_to_virt(H1940_SUSPEND_CHECK);
		unsigned long ptr;
		unsigned long calc = 0;

		/* generate check for the bootloader to check on resume */

		for (ptr = 0; ptr < 0x40000; ptr += 0x4)
			calc += __raw_readl(base+ptr);

		__raw_writel(calc, phys_to_virt(H1940_SUSPEND_CHECKSUM));
	}

	if (machine_is_aml_m5900()) {
		gpio_request_one(S3C2410_GPF(2), GPIOF_OUT_INIT_HIGH, NULL);
		gpio_free(S3C2410_GPF(2));
	}

	if (machine_is_rx1950()) {
		/* According to S3C2442 user's manual, page 7-17,
		 * when the system is operating in NAND boot mode,
		 * the hardware pin configuration - EINT[23:21] –
		 * must be set as input for starting up after
		 * wakeup from sleep mode
		 */
		s3c_gpio_cfgpin(S3C2410_GPG(13), S3C2410_GPIO_INPUT);
		s3c_gpio_cfgpin(S3C2410_GPG(14), S3C2410_GPIO_INPUT);
		s3c_gpio_cfgpin(S3C2410_GPG(15), S3C2410_GPIO_INPUT);
	}
}

static void s3c2410_pm_resume(void)
{
	unsigned long tmp;

	/* unset the return-from-sleep flag, to ensure reset */

	tmp = __raw_readl(S3C2410_GSTATUS2);
	tmp &= S3C2410_GSTATUS2_OFFRESET;
	__raw_writel(tmp, S3C2410_GSTATUS2);

	if (machine_is_aml_m5900()) {
		gpio_request_one(S3C2410_GPF(2), GPIOF_OUT_INIT_LOW, NULL);
		gpio_free(S3C2410_GPF(2));
	}
}

struct syscore_ops s3c2410_pm_syscore_ops = {
	.resume		= s3c2410_pm_resume,
};

static int s3c2410_pm_add(struct device *dev, struct subsys_interface *sif)
{
	pm_cpu_prep = s3c2410_pm_prepare;
	pm_cpu_sleep = s3c2410_cpu_suspend;

	return 0;
}

#if defined(CONFIG_CPU_S3C2410)
static struct subsys_interface s3c2410_pm_interface = {
	.name		= "s3c2410_pm",
	.subsys		= &s3c2410_subsys,
	.add_dev	= s3c2410_pm_add,
};

/* register ourselves */

static int __init s3c2410_pm_drvinit(void)
{
	return subsys_interface_register(&s3c2410_pm_interface);
}

arch_initcall(s3c2410_pm_drvinit);

static struct subsys_interface s3c2410a_pm_interface = {
	.name		= "s3c2410a_pm",
	.subsys		= &s3c2410a_subsys,
	.add_dev	= s3c2410_pm_add,
};

static int __init s3c2410a_pm_drvinit(void)
{
	return subsys_interface_register(&s3c2410a_pm_interface);
}

arch_initcall(s3c2410a_pm_drvinit);
#endif

#if defined(CONFIG_CPU_S3C2440)
static struct subsys_interface s3c2440_pm_interface = {
	.name		= "s3c2440_pm",
	.subsys		= &s3c2440_subsys,
	.add_dev	= s3c2410_pm_add,
};

static int __init s3c2440_pm_drvinit(void)
{
	return subsys_interface_register(&s3c2440_pm_interface);
}

arch_initcall(s3c2440_pm_drvinit);
#endif

#if defined(CONFIG_CPU_S3C2442)
static struct subsys_interface s3c2442_pm_interface = {
	.name		= "s3c2442_pm",
	.subsys		= &s3c2442_subsys,
	.add_dev	= s3c2410_pm_add,
};

static int __init s3c2442_pm_drvinit(void)
{
	return subsys_interface_register(&s3c2442_pm_interface);
}

arch_initcall(s3c2442_pm_drvinit);
#endif
