/*
 * ARC FPGA Platform support code
 *
 * Copyright (C) 2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/console.h>
#include <linux/of_platform.h>
#include <asm/setup.h>
#include <asm/clk.h>
#include <asm/mach_desc.h>
#include <plat/memmap.h>
#include <plat/smp.h>
#include <plat/irq.h>

/*-----------------------BVCI Latency Unit -----------------------------*/

#ifdef CONFIG_ARC_HAS_BVCI_LAT_UNIT

int lat_cycles = CONFIG_BVCI_LAT_CYCLES;

/* BVCI Bus Profiler: Latency Unit */
static void __init setup_bvci_lat_unit(void)
{
#define MAX_BVCI_UNITS 12

	unsigned int i;
	unsigned int *base = (unsigned int *)BVCI_LAT_UNIT_BASE;
	const unsigned long units_req = CONFIG_BVCI_LAT_UNITS;
	const unsigned int REG_UNIT = 21;
	const unsigned int REG_VAL = 22;

	/*
	 * There are multiple Latency Units corresponding to the many
	 * interfaces of the system bus arbiter (both CPU side as well as
	 * the peripheral side).
	 *
	 * Unit  0 - System Arb and Mem Controller - adds latency to all
	 *	    memory trasactions
	 * Unit  1 - I$ and System Bus
	 * Unit  2 - D$ and System Bus
	 * ..
	 * Unit 12 - IDE Disk controller and System Bus
	 *
	 * The programmers model requires writing to lat_unit reg first
	 * and then the latency value (cycles) to lat_value reg
	 */

	if (CONFIG_BVCI_LAT_UNITS == 0) {
		writel(0, base + REG_UNIT);
		writel(lat_cycles, base + REG_VAL);
		pr_info("BVCI Latency for all Memory Transactions %d cycles\n",
			lat_cycles);
	} else {
		for_each_set_bit(i, &units_req, MAX_BVCI_UNITS) {
			writel(i + 1, base + REG_UNIT); /* loop is 0 based */
			writel(lat_cycles, base + REG_VAL);
			pr_info("BVCI Latency for Unit[%d] = %d cycles\n",
				(i + 1), lat_cycles);
		}
	}
}
#else
static void __init setup_bvci_lat_unit(void)
{
}
#endif

/*----------------------- Platform Devices -----------------------------*/

#if IS_ENABLED(CONFIG_SERIAL_ARC)
static unsigned long arc_uart_info[] = {
	0,	/* uart->is_emulated (runtime @running_on_hw) */
	0,	/* uart->port.uartclk */
	0,	/* uart->baud */
	0
};

#if defined(CONFIG_SERIAL_ARC_CONSOLE)
/*
 * static platform data - but only for early serial
 * TBD: derive this from a special DT node
 */
static struct resource arc_uart0_res[] = {
	{
		.start = UART0_BASE,
		.end   = UART0_BASE + 0xFF,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = UART0_IRQ,
		.end   = UART0_IRQ,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device arc_uart0_dev = {
	.name = "arc-uart",
	.id = 0,
	.num_resources = ARRAY_SIZE(arc_uart0_res),
	.resource = arc_uart0_res,
	.dev = {
		.platform_data = &arc_uart_info,
	},
};

static struct platform_device *fpga_early_devs[] __initdata = {
	&arc_uart0_dev,
};
#endif	/* CONFIG_SERIAL_ARC_CONSOLE */

static void arc_fpga_serial_init(void)
{
	/* To let driver workaround ISS bug: baudh Reg can't be set to 0 */
	arc_uart_info[0] = !running_on_hw;

	arc_uart_info[1] = arc_get_core_freq();

	arc_uart_info[2] = CONFIG_ARC_SERIAL_BAUD;

#if defined(CONFIG_SERIAL_ARC_CONSOLE)
	early_platform_add_devices(fpga_early_devs,
				   ARRAY_SIZE(fpga_early_devs));

	/*
	 * ARC console driver registers itself as an early platform driver
	 * of class "earlyprintk".
	 * Install it here, followed by probe of devices.
	 * The installation here doesn't require earlyprintk in command line
	 * To do so however, replace the lines below with
	 *	parse_early_param();
	 *	early_platform_driver_probe("earlyprintk", 1, 1);
	 *						      ^^
	 */
	early_platform_driver_register_all("earlyprintk");
	early_platform_driver_probe("earlyprintk", 1, 0);

	/*
	 * This is to make sure that arc uart would be preferred console
	 * despite one/more of following:
	 *   -command line lacked "console=ttyARC0" or
	 *   -CONFIG_VT_CONSOLE was enabled (for no reason whatsoever)
	 * Note that this needs to be done after above early console is reg,
	 * otherwise the early console never gets a chance to run.
	 */
	add_preferred_console("ttyARC", 0, "115200");
#endif	/* CONFIG_SERIAL_ARC_CONSOLE */
}
#else	/* !IS_ENABLED(CONFIG_SERIAL_ARC) */
static void arc_fpga_serial_init(void)
{
}
#endif

static void __init plat_fpga_early_init(void)
{
	pr_info("[plat-arcfpga]: registering early dev resources\n");

	setup_bvci_lat_unit();

	arc_fpga_serial_init();

#ifdef CONFIG_SMP
	iss_model_init_early_smp();
#endif
}

static struct of_dev_auxdata plat_auxdata_lookup[] __initdata = {
#if IS_ENABLED(CONFIG_SERIAL_ARC)
	OF_DEV_AUXDATA("snps,arc-uart", UART0_BASE, "arc-uart", arc_uart_info),
#endif
	{}
};

static void __init plat_fpga_populate_dev(void)
{
	pr_info("[plat-arcfpga]: registering device resources\n");

	/*
	 * Traverses flattened DeviceTree - registering platform devices
	 * complete with their resources
	 */
	of_platform_populate(NULL, of_default_bus_match_table,
			     plat_auxdata_lookup, NULL);
}

/*----------------------- Machine Descriptions ------------------------------
 *
 * Machine description is simply a set of platform/board specific callbacks
 * This is not directly related to DeviceTree based dynamic device creation,
 * however as part of early device tree scan, we also select the right
 * callback set, by matching the DT compatible name.
 */

static const char *aa4_compat[] __initdata = {
	"snps,arc-angel4",
	NULL,
};

MACHINE_START(ANGEL4, "angel4")
	.dt_compat	= aa4_compat,
	.init_early	= plat_fpga_early_init,
	.init_machine	= plat_fpga_populate_dev,
	.init_irq	= plat_fpga_init_IRQ,
#ifdef CONFIG_SMP
	.init_smp	= iss_model_init_smp,
#endif
MACHINE_END

static const char *ml509_compat[] __initdata = {
	"snps,arc-ml509",
	NULL,
};

MACHINE_START(ML509, "ml509")
	.dt_compat	= ml509_compat,
	.init_early	= plat_fpga_early_init,
	.init_machine	= plat_fpga_populate_dev,
	.init_irq	= plat_fpga_init_IRQ,
#ifdef CONFIG_SMP
	.init_smp	= iss_model_init_smp,
#endif
MACHINE_END

static const char *nsimosci_compat[] __initdata = {
	"snps,nsimosci",
	NULL,
};

MACHINE_START(NSIMOSCI, "nsimosci")
	.dt_compat	= nsimosci_compat,
	.init_early	= NULL,
	.init_machine	= plat_fpga_populate_dev,
	.init_irq	= NULL,
MACHINE_END
