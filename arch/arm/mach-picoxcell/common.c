/*
 * Copyright (c) 2011 Picochip Ltd., Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All enquiries to support@picochip.com
 */
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>

#include <asm/mach/arch.h>
#include <asm/hardware/vic.h>

#include <mach/map.h>
#include <mach/picoxcell_soc.h>

#include "common.h"

static void __init picoxcell_init_machine(void)
{
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char *picoxcell_dt_match[] = {
	"picochip,pc3x2",
	"picochip,pc3x3",
	NULL
};

static const struct of_device_id vic_of_match[] __initconst = {
	{ .compatible = "arm,pl192-vic" },
	{ /* Sentinel */ }
};

static void __init picoxcell_init_irq(void)
{
	vic_init(IO_ADDRESS(PICOXCELL_VIC0_BASE), 0, ~0, 0);
	vic_init(IO_ADDRESS(PICOXCELL_VIC1_BASE), 32, ~0, 0);
	irq_domain_generate_simple(vic_of_match, PICOXCELL_VIC0_BASE, 0);
	irq_domain_generate_simple(vic_of_match, PICOXCELL_VIC1_BASE, 32);
}

DT_MACHINE_START(PICOXCELL, "Picochip picoXcell")
	.map_io		= picoxcell_map_io,
	.nr_irqs	= ARCH_NR_IRQS,
	.init_irq	= picoxcell_init_irq,
	.timer		= &picoxcell_timer,
	.init_machine	= picoxcell_init_machine,
	.dt_compat	= picoxcell_dt_match,
MACHINE_END
