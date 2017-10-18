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
#include <linux/clk-provider.h>
#include <linux/of_address.h>

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
	cpu_reset();
	/* control never gets here */
}

void __init platform_setup(char **cmdline)
{
}

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
	ccount_freq = *(long *)XTFPGA_CLKFRQ_VADDR;
}

#endif

#ifdef CONFIG_OF

static void __init xtfpga_clk_setup(struct device_node *np)
{
	void __iomem *base = of_iomap(np, 0);
	struct clk *clk;
	u32 freq;

	if (!base) {
		pr_err("%s: invalid address\n", np->name);
		return;
	}

	freq = __raw_readl(base);
	iounmap(base);
	clk = clk_register_fixed_rate(NULL, np->name, NULL, 0, freq);

	if (IS_ERR(clk)) {
		pr_err("%s: clk registration failed\n", np->name);
		return;
	}

	if (of_clk_add_provider(np, of_clk_src_simple_get, clk)) {
		pr_err("%s: clk provider registration failed\n", np->name);
		return;
	}
}
CLK_OF_DECLARE(xtfpga_clk, "cdns,xtfpga-clock", xtfpga_clk_setup);

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
	struct device_node *eth = NULL;

	if ((eth = of_find_compatible_node(eth, NULL, "opencores,ethoc")))
		update_local_mac(eth);
	return 0;
}
arch_initcall(machine_setup);

#else

#include <linux/serial_8250.h>
#include <linux/if.h>
#include <net/ethoc.h>
#include <linux/usb/c67x00.h>

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
		.start = XTENSA_PIC_LINUX_IRQ(OETH_IRQ),
		.end   = XTENSA_PIC_LINUX_IRQ(OETH_IRQ),
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
	.big_endian = XCHAL_HAVE_BE,
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
 *  USB Host/Device -- Cypress CY7C67300
 */

static struct resource c67x00_res[] = {
	[0] = { /* register space */
		.start = C67X00_PADDR,
		.end   = C67X00_PADDR + C67X00_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = { /* IRQ number */
		.start = XTENSA_PIC_LINUX_IRQ(C67X00_IRQ),
		.end   = XTENSA_PIC_LINUX_IRQ(C67X00_IRQ),
		.flags = IORESOURCE_IRQ,
	},
};

static struct c67x00_platform_data c67x00_pdata = {
	.sie_config = C67X00_SIE1_HOST | C67X00_SIE2_UNUSED,
	.hpi_regstep = 4,
};

static struct platform_device c67x00_device = {
	.name = "c67x00",
	.id = -1,
	.num_resources = ARRAY_SIZE(c67x00_res),
	.resource = c67x00_res,
	.dev = {
		.platform_data = &c67x00_pdata,
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
		.irq		= XTENSA_PIC_LINUX_IRQ(DUART16552_INTNUM),
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST |
				  UPF_IOREMAP,
		.iotype		= XCHAL_HAVE_BE ? UPIO_MEM32BE : UPIO_MEM32,
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
	&c67x00_device,
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
