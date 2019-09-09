// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * BRIEF MODULE DESCRIPTION
 *	MyCable XXS1500 board support
 *
 * Copyright 2003, 2008 MontaVista Software Inc.
 * Author: MontaVista Software, Inc. <source@mvista.com>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <asm/bootinfo.h>
#include <asm/reboot.h>
#include <asm/setup.h>
#include <asm/mach-au1x00/au1000.h>
#include <prom.h>

const char *get_system_type(void)
{
	return "XXS1500";
}

void __init prom_init(void)
{
	unsigned char *memsize_str;
	unsigned long memsize;

	prom_argc = fw_arg0;
	prom_argv = (char **)fw_arg1;
	prom_envp = (char **)fw_arg2;

	prom_init_cmdline();

	memsize_str = prom_getenv("memsize");
	if (!memsize_str || kstrtoul(memsize_str, 0, &memsize))
		memsize = 0x04000000;

	add_memory_region(0, memsize, BOOT_MEM_RAM);
}

void prom_putchar(char c)
{
	alchemy_uart_putchar(AU1000_UART0_PHYS_ADDR, c);
}

static void xxs1500_reset(char *c)
{
	/* Jump to the reset vector */
	__asm__ __volatile__("jr\t%0" : : "r"(0xbfc00000));
}

static void xxs1500_power_off(void)
{
	while (1)
		asm volatile (
		"	.set	mips32					\n"
		"	wait						\n"
		"	.set	mips0					\n");
}

void __init board_setup(void)
{
	u32 pin_func;

	pm_power_off = xxs1500_power_off;
	_machine_halt = xxs1500_power_off;
	_machine_restart = xxs1500_reset;

	alchemy_gpio1_input_enable();
	alchemy_gpio2_enable();

	/* Set multiple use pins (UART3/GPIO) to UART (it's used as UART too) */
	pin_func  = alchemy_rdsys(AU1000_SYS_PINFUNC) & ~SYS_PF_UR3;
	pin_func |= SYS_PF_UR3;
	alchemy_wrsys(pin_func, AU1000_SYS_PINFUNC);

	/* Enable UART */
	alchemy_uart_enable(AU1000_UART3_PHYS_ADDR);
	/* Enable DTR (MCR bit 0) = USB power up */
	__raw_writel(1, (void __iomem *)KSEG1ADDR(AU1000_UART3_PHYS_ADDR + 0x18));
	wmb();
}

/******************************************************************************/

static struct resource xxs1500_pcmcia_res[] = {
	{
		.name	= "pcmcia-io",
		.flags	= IORESOURCE_MEM,
		.start	= AU1000_PCMCIA_IO_PHYS_ADDR,
		.end	= AU1000_PCMCIA_IO_PHYS_ADDR + 0x000400000 - 1,
	},
	{
		.name	= "pcmcia-attr",
		.flags	= IORESOURCE_MEM,
		.start	= AU1000_PCMCIA_ATTR_PHYS_ADDR,
		.end	= AU1000_PCMCIA_ATTR_PHYS_ADDR + 0x000400000 - 1,
	},
	{
		.name	= "pcmcia-mem",
		.flags	= IORESOURCE_MEM,
		.start	= AU1000_PCMCIA_MEM_PHYS_ADDR,
		.end	= AU1000_PCMCIA_MEM_PHYS_ADDR + 0x000400000 - 1,
	},
};

static struct platform_device xxs1500_pcmcia_dev = {
	.name		= "xxs1500_pcmcia",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(xxs1500_pcmcia_res),
	.resource	= xxs1500_pcmcia_res,
};

static struct platform_device *xxs1500_devs[] __initdata = {
	&xxs1500_pcmcia_dev,
};

static int __init xxs1500_dev_init(void)
{
	irq_set_irq_type(AU1500_GPIO204_INT, IRQ_TYPE_LEVEL_HIGH);
	irq_set_irq_type(AU1500_GPIO201_INT, IRQ_TYPE_LEVEL_LOW);
	irq_set_irq_type(AU1500_GPIO202_INT, IRQ_TYPE_LEVEL_LOW);
	irq_set_irq_type(AU1500_GPIO203_INT, IRQ_TYPE_LEVEL_LOW);
	irq_set_irq_type(AU1500_GPIO205_INT, IRQ_TYPE_LEVEL_LOW);
	irq_set_irq_type(AU1500_GPIO207_INT, IRQ_TYPE_LEVEL_LOW);

	irq_set_irq_type(AU1500_GPIO0_INT, IRQ_TYPE_LEVEL_LOW);
	irq_set_irq_type(AU1500_GPIO1_INT, IRQ_TYPE_LEVEL_LOW);
	irq_set_irq_type(AU1500_GPIO2_INT, IRQ_TYPE_LEVEL_LOW);
	irq_set_irq_type(AU1500_GPIO3_INT, IRQ_TYPE_LEVEL_LOW);
	irq_set_irq_type(AU1500_GPIO4_INT, IRQ_TYPE_LEVEL_LOW); /* CF irq */
	irq_set_irq_type(AU1500_GPIO5_INT, IRQ_TYPE_LEVEL_LOW);

	return platform_add_devices(xxs1500_devs,
				    ARRAY_SIZE(xxs1500_devs));
}
device_initcall(xxs1500_dev_init);
