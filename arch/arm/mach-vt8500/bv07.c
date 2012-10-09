/*
 *  arch/arm/mach-vt8500/bv07.c
 *
 *  Copyright (C) 2010 Alexey Charkov <alchark@gmail.com>
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

#include <linux/io.h>
#include <linux/pm.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <mach/restart.h>

#include "devices.h"

static void __iomem *pmc_hiber;

static struct platform_device *devices[] __initdata = {
	&vt8500_device_uart0,
	&vt8500_device_lcdc,
	&vt8500_device_ehci,
	&vt8500_device_ge_rops,
	&vt8500_device_pwm,
	&vt8500_device_pwmbl,
	&vt8500_device_rtc,
};

static void vt8500_power_off(void)
{
	local_irq_disable();
	writew(5, pmc_hiber);
	asm("mcr%? p15, 0, %0, c7, c0, 4" : : "r" (0));
}

void __init bv07_init(void)
{
#ifdef CONFIG_FB_VT8500
	void __iomem *gpio_mux_reg = ioremap(wmt_gpio_base + 0x200, 4);
	if (gpio_mux_reg) {
		writel(readl(gpio_mux_reg) | 1, gpio_mux_reg);
		iounmap(gpio_mux_reg);
	} else {
		printk(KERN_ERR "Could not remap the GPIO mux register, display may not work properly!\n");
	}
#endif
	pmc_hiber = ioremap(wmt_pmc_base + 0x12, 2);
	if (pmc_hiber)
		pm_power_off = &vt8500_power_off;
	else
		printk(KERN_ERR "PMC Hibernation register could not be remapped, not enabling power off!\n");

	wmt_setup_restart();
	vt8500_set_resources();
	platform_add_devices(devices, ARRAY_SIZE(devices));
	vt8500_gpio_init();
}

MACHINE_START(BV07, "Benign BV07 Mini Netbook")
	.atag_offset	= 0x100,
	.restart	= wmt_restart,
	.reserve	= vt8500_reserve_mem,
	.map_io		= vt8500_map_io,
	.init_irq	= vt8500_init_irq,
	.timer		= &vt8500_timer,
	.init_machine	= bv07_init,
MACHINE_END
