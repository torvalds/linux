// SPDX-License-Identifier: GPL-2.0-only
/*
 * EcoNet setup code
 *
 * Copyright (C) 2025 Caleb James DeLisle <cjd@cjdns.fr>
 */

#include <linux/init.h>
#include <linux/of_clk.h>
#include <linux/irqchip.h>

#include <asm/addrspace.h>
#include <asm/io.h>
#include <asm/bootinfo.h>
#include <asm/time.h>
#include <asm/prom.h>
#include <asm/smp-ops.h>
#include <asm/reboot.h>

#define CR_AHB_RSTCR		((void __iomem *)CKSEG1ADDR(0x1fb00040))
#define RESET			BIT(31)

#define UART_BASE		CKSEG1ADDR(0x1fbf0003)
#define UART_REG_SHIFT		2

static void hw_reset(char *command)
{
	iowrite32(RESET, CR_AHB_RSTCR);
}

/* 1. Bring up early printk. */
void __init prom_init(void)
{
	setup_8250_early_printk_port(UART_BASE, UART_REG_SHIFT, 0);
	_machine_restart = hw_reset;
}

/* 2. Parse the DT and find memory */
void __init plat_mem_setup(void)
{
	void *dtb;

	set_io_port_base(KSEG1);

	dtb = get_fdt();
	if (!dtb)
		panic("no dtb found");

	__dt_setup_arch(dtb);

	early_init_dt_scan_memory();
}

/* 3. Overload __weak device_tree_init(), add SMP_UP ops */
void __init device_tree_init(void)
{
	unflatten_and_copy_device_tree();

	register_up_smp_ops();
}

const char *get_system_type(void)
{
	return "EcoNet-EN75xx";
}

/* 4. Initialize the IRQ subsystem */
void __init arch_init_irq(void)
{
	irqchip_init();
}

/* 5. Timers */
void __init plat_time_init(void)
{
	of_clk_init(NULL);
	timer_probe();
}
