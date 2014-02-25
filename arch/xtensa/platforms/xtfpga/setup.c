/*
 *
 * arch/xtensa/platform/xtavnet/setup.c
 *
 * ...
 *
 * Authors:	Chris Zankel <chris@zankel.net>
 *		Joe Taylor <joe@tensilica.com>
 *
 * Copyright 2001 - 2006 Tensilica Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/reboot.h>
#include <linux/kdev_t.h>
#include <linux/types.h>
#include <linux/major.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/of.h>

#include <asm/timex.h>
#include <asm/processor.h>
#include <asm/platform.h>
#include <asm/bootparam.h>
#include <platform/lcd.h>
#include <platform/hardware.h>

void platform_halt(void)
{
	lcd_disp_at_pos(" HALT ", 0);
	local_irq_disable();
	while (1)
		cpu_relax();
}

void platform_power_off(void)
{
	lcd_disp_at_pos("POWEROFF", 0);
	local_irq_disable();
	while (1)
		cpu_relax();
}

void platform_restart(void)
{
	/* Flush and reset the mmu, simulate a processor reset, and
	 * jump to the reset vector. */


	__asm__ __volatile__ ("movi	a2, 15\n\t"
			      "wsr	a2, icountlevel\n\t"
			      "movi	a2, 0\n\t"
			      "wsr	a2, icount\n\t"
#if XCHAL_NUM_IBREAK > 0
			      "wsr	a2, ibreakenable\n\t"
#endif
			      "wsr	a2, lcount\n\t"
			      "movi	a2, 0x1f\n\t"
			      "wsr	a2, ps\n\t"
			      "isync\n\t"
			      "jx	%0\n\t"
			      :
			      : "a" (XCHAL_RESET_VECTOR_VADDR)
			      : "a2"
			      );

	/* control never gets here */
}

void __init platform_setup(char **cmdline)
{
}

#ifdef CONFIG_OF

static void __init update_clock_frequency(struct device_node *node)
{
	struct property *newfreq;
	u32 freq;

	if (!of_property_read_u32(node, "clock-frequency", &freq) && freq != 0)
		return;

	newfreq = kzalloc(sizeof(*newfreq) + sizeof(u32), GFP_KERNEL);
	if (!newfreq)
		return;
	newfreq->value = newfreq + 1;
	newfreq->length = sizeof(freq);
	newfreq->name = kstrdup("clock-frequency", GFP_KERNEL);
	if (!newfreq->name) {
		kfree(newfreq);
		return;
	}

	*(u32 *)newfreq->value = cpu_to_be32(*(u32 *)XTFPGA_CLKFRQ_VADDR);
	of_update_property(node, newfreq);
}

#define MAC_LEN 6
static void __init update_local_mac(struct device_node *node)
{
	struct property *newmac;
	const u8* macaddr;
	int prop_len;

	macaddr = of_get_property(node, "local-mac-address", &prop_len);
	if (macaddr == NULL || prop_len != MAC_LEN)
		return;

	newmac = kzalloc(sizeof(*newmac) + MAC_LEN, GFP_KERNEL);
	if (newmac == NULL)
		return;

	newmac->value = newmac + 1;
	newmac->length = MAC_LEN;
	newmac->name = kstrdup("local-mac-address", GFP_KERNEL);
	if (newmac->name == NULL) {
		kfree(newmac);
		return;
	}

	memcpy(newmac->value, macaddr, MAC_LEN);
	((u8*)newmac->value)[5] = (*(u32*)DIP_SWITCHES_VADDR) & 0x3f;
	of_update_property(node, newmac);
}

static int __init machine_setup(void)
{
	struct device_node *clock;
	struct device_node *eth = NULL;

	for_each_node_by_name(clock, "main-oscillator")
		update_clock_frequency(clock);

	if ((eth = of_find_compatible_node(eth, NULL, "opencores,ethoc")))
		update_local_mac(eth);
	return 0;
}
arch_initcall(machine_setup);

#endif

/* early initialization */

void __init platform_init(bp_tag_t *first)
{
}

/* Heartbeat. */

void platform_heartbeat(void)
{
}

#ifdef CONFIG_XTENSA_CALIBRATE_CCOUNT

void __init platform_calibrate_ccount(void)
{
	long clk_freq = 0;
#ifdef CONFIG_OF
	struct device_node *cpu =
		of_find_compatible_node(NULL, NULL, "cdns,xtensa-cpu");
	if (cpu) {
		u32 freq;
		update_clock_frequency(cpu);
		if (!of_property_read_u32(cpu, "clock-frequency", &freq))
			clk_freq = freq;
	}
#endif
	if (!clk_freq)
		clk_freq = *(long *)XTFPGA_CLKFRQ_VADDR;

	ccount_freq = clk_freq;
}

#endif

#ifndef CONFIG_OF

#include <linux/serial_8250.h>
#include <linux/if.h>
#include <net/ethoc.h>

/*----------------------------------------------------------------------------
 *  Ethernet -- OpenCores Ethernet MAC (ethoc driver)
 */

static struct resource ethoc_res[] = {
	[0] = { /* register space */
		.start = OETH_REGS_PADDR,
		.end   = OETH_REGS_PADDR + OETH_REGS_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = { /* buffer space */
		.start = OETH_SRAMBUFF_PADDR,
		.end   = OETH_SRAMBUFF_PADDR + OETH_SRAMBUFF_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	[2] = { /* IRQ number */
		.start = OETH_IRQ,
		.end   = OETH_IRQ,
		.flags = IORESOURCE_IRQ,
	},
};

static struct ethoc_platform_data ethoc_pdata = {
	/*
	 * The MAC address for these boards is 00:50:c2:13:6f:xx.
	 * The last byte (here as zero) is read from the DIP switches on the
	 * board.
	 */
	.hwaddr = { 0x00, 0x50, 0xc2, 0x13, 0x6f, 0 },
	.phy_id = -1,
};

static struct platform_device ethoc_device = {
	.name = "ethoc",
	.id = -1,
	.num_resources = ARRAY_SIZE(ethoc_res),
	.resource = ethoc_res,
	.dev = {
		.platform_data = &ethoc_pdata,
	},
};

/*----------------------------------------------------------------------------
 *  UART
 */

static struct resource serial_resource = {
	.start	= DUART16552_PADDR,
	.end	= DUART16552_PADDR + 0x1f,
	.flags	= IORESOURCE_MEM,
};

static struct plat_serial8250_port serial_platform_data[] = {
	[0] = {
		.mapbase	= DUART16552_PADDR,
		.irq		= DUART16552_INTNUM,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST |
				  UPF_IOREMAP,
		.iotype		= UPIO_MEM32,
		.regshift	= 2,
		.uartclk	= 0,    /* set in xtavnet_init() */
	},
	{ },
};

static struct platform_device xtavnet_uart = {
	.name		= "serial8250",
	.id		= PLAT8250_DEV_PLATFORM,
	.dev		= {
		.platform_data	= serial_platform_data,
	},
	.num_resources	= 1,
	.resource	= &serial_resource,
};

/* platform devices */
static struct platform_device *platform_devices[] __initdata = {
	&ethoc_device,
	&xtavnet_uart,
};


static int __init xtavnet_init(void)
{
	/* Ethernet MAC address.  */
	ethoc_pdata.hwaddr[5] = *(u32 *)DIP_SWITCHES_VADDR;

	/* Clock rate varies among FPGA bitstreams; board specific FPGA register
	 * reports the actual clock rate.
	 */
	serial_platform_data[0].uartclk = *(long *)XTFPGA_CLKFRQ_VADDR;


	/* register platform devices */
	platform_add_devices(platform_devices, ARRAY_SIZE(platform_devices));

	/* ETHOC driver is a bit quiet; at least display Ethernet MAC, so user
	 * knows whether they set it correctly on the DIP switches.
	 */
	pr_info("XTFPGA: Ethernet MAC %pM\n", ethoc_pdata.hwaddr);
	ethoc_pdata.eth_clkfreq = *(long *)XTFPGA_CLKFRQ_VADDR;

	return 0;
}

/*
 * Register to be done during do_initcalls().
 */
arch_initcall(xtavnet_init);

#endif /* CONFIG_OF */
