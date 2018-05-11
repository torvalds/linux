/*
 * Copyright (C) 2008-2009 ST-Ericsson SA
 *
 * Author: Srinidhi KASAGAR <srinidhi.kasagar@stericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 */
#include <linux/types.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/amba/bus.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqchip/arm-gic.h>
#include <linux/mfd/dbx500-prcmu.h>
#include <linux/platform_data/arm-ux500-pm.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/regulator/machine.h>

#include <asm/outercache.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/mach/map.h>
#include <asm/mach/arch.h>

#include "db8500-regs.h"
#include "pm_domains.h"

static int __init ux500_l2x0_unlock(void)
{
	int i;
	struct device_node *np;
	void __iomem *l2x0_base;

	np = of_find_compatible_node(NULL, NULL, "arm,pl310-cache");
	l2x0_base = of_iomap(np, 0);
	of_node_put(np);
	if (!l2x0_base)
		return -ENODEV;

	/*
	 * Unlock Data and Instruction Lock if locked. Ux500 U-Boot versions
	 * apparently locks both caches before jumping to the kernel. The
	 * l2x0 core will not touch the unlock registers if the l2x0 is
	 * already enabled, so we do it right here instead. The PL310 has
	 * 8 sets of registers, one per possible CPU.
	 */
	for (i = 0; i < 8; i++) {
		writel_relaxed(0x0, l2x0_base + L2X0_LOCKDOWN_WAY_D_BASE +
			       i * L2X0_LOCKDOWN_STRIDE);
		writel_relaxed(0x0, l2x0_base + L2X0_LOCKDOWN_WAY_I_BASE +
			       i * L2X0_LOCKDOWN_STRIDE);
	}
	iounmap(l2x0_base);
	return 0;
}

static void ux500_l2c310_write_sec(unsigned long val, unsigned reg)
{
	/*
	 * We can't write to secure registers as we are in non-secure
	 * mode, until we have some SMI service available.
	 */
}

/*
 * FIXME: Should we set up the GPIO domain here?
 *
 * The problem is that we cannot put the interrupt resources into the platform
 * device until the irqdomain has been added. Right now, we set the GIC interrupt
 * domain from init_irq(), then load the gpio driver from
 * core_initcall(nmk_gpio_init) and add the platform devices from
 * arch_initcall(customize_machine).
 *
 * This feels fragile because it depends on the gpio device getting probed
 * _before_ any device uses the gpio interrupts.
*/
static void __init ux500_init_irq(void)
{
	struct device_node *np;
	struct resource r;

	irqchip_init();
	np = of_find_compatible_node(NULL, NULL, "stericsson,db8500-prcmu");
	of_address_to_resource(np, 0, &r);
	of_node_put(np);
	if (!r.start) {
		pr_err("could not find PRCMU base resource\n");
		return;
	}
	prcmu_early_init(r.start, r.end-r.start);
	ux500_pm_init(r.start, r.end-r.start);

	/* Unlock before init */
	ux500_l2x0_unlock();
	outer_cache.write_sec = ux500_l2c310_write_sec;
}

static void ux500_restart(enum reboot_mode mode, const char *cmd)
{
	local_irq_disable();
	local_fiq_disable();

	prcmu_system_reset(0);
}

static struct of_dev_auxdata u8540_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("stericsson,db8500-prcmu", 0x80157000, "db8500-prcmu", NULL),
	{},
};

static const struct of_device_id u8500_local_bus_nodes[] = {
	/* only create devices below soc node */
	{ .compatible = "stericsson,db8500", },
	{ .compatible = "stericsson,db8500-prcmu", },
	{ .compatible = "simple-bus"},
	{ },
};

static void __init u8500_init_machine(void)
{
	/* Initialize ux500 power domains */
	ux500_pm_domains_init();

	/* automatically probe child nodes of dbx5x0 devices */
	if (of_machine_is_compatible("st-ericsson,u8540"))
		of_platform_populate(NULL, u8500_local_bus_nodes,
				     u8540_auxdata_lookup, NULL);
	else
		of_platform_populate(NULL, u8500_local_bus_nodes,
				     NULL, NULL);
}

static const char * stericsson_dt_platform_compat[] = {
	"st-ericsson,u8500",
	"st-ericsson,u8540",
	"st-ericsson,u9500",
	"st-ericsson,u9540",
	NULL,
};

DT_MACHINE_START(U8500_DT, "ST-Ericsson Ux5x0 platform (Device Tree Support)")
	.l2c_aux_val    = 0,
	.l2c_aux_mask	= ~0,
	.init_irq	= ux500_init_irq,
	.init_machine	= u8500_init_machine,
	.dt_compat      = stericsson_dt_platform_compat,
	.restart        = ux500_restart,
MACHINE_END
