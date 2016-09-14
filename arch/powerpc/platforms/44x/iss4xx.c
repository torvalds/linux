/*
 * PPC476 board specific routines
 *
 * Copyright 2010 Torez Smith, IBM Corporation.
 *
 * Based on earlier code:
 *    Matt Porter <mporter@kernel.crashing.org>
 *    Copyright 2002-2005 MontaVista Software Inc.
 *
 *    Eugene Surovegin <eugene.surovegin@zultys.com> or <ebs@ebshome.net>
 *    Copyright (c) 2003-2005 Zultys Technologies
 *
 *    Rewritten and ported to the merged powerpc tree:
 *    Copyright 2007 David Gibson <dwg@au1.ibm.com>, IBM Corporation.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/init.h>
#include <linux/of_platform.h>
#include <linux/rtc.h>

#include <asm/machdep.h>
#include <asm/prom.h>
#include <asm/udbg.h>
#include <asm/time.h>
#include <asm/uic.h>
#include <asm/ppc4xx.h>
#include <asm/mpic.h>
#include <asm/mmu.h>

static const struct of_device_id iss4xx_of_bus[] __initconst = {
	{ .compatible = "ibm,plb4", },
	{ .compatible = "ibm,plb6", },
	{ .compatible = "ibm,opb", },
	{ .compatible = "ibm,ebc", },
	{},
};

static int __init iss4xx_device_probe(void)
{
	of_platform_bus_probe(NULL, iss4xx_of_bus, NULL);
	of_instantiate_rtc();

	return 0;
}
machine_device_initcall(iss4xx, iss4xx_device_probe);

/* We can have either UICs or MPICs */
static void __init iss4xx_init_irq(void)
{
	struct device_node *np;

	/* Find top level interrupt controller */
	for_each_node_with_property(np, "interrupt-controller") {
		if (of_get_property(np, "interrupts", NULL) == NULL)
			break;
	}
	if (np == NULL)
		panic("Can't find top level interrupt controller");

	/* Check type and do appropriate initialization */
	if (of_device_is_compatible(np, "ibm,uic")) {
		uic_init_tree();
		ppc_md.get_irq = uic_get_irq;
#ifdef CONFIG_MPIC
	} else if (of_device_is_compatible(np, "chrp,open-pic")) {
		/* The MPIC driver will get everything it needs from the
		 * device-tree, just pass 0 to all arguments
		 */
		struct mpic *mpic = mpic_alloc(np, 0, MPIC_NO_RESET, 0, 0, " MPIC     ");
		BUG_ON(mpic == NULL);
		mpic_init(mpic);
		ppc_md.get_irq = mpic_get_irq;
#endif
	} else
		panic("Unrecognized top level interrupt controller");
}

#ifdef CONFIG_SMP
static void smp_iss4xx_setup_cpu(int cpu)
{
	mpic_setup_this_cpu();
}

static int smp_iss4xx_kick_cpu(int cpu)
{
	struct device_node *cpunode = of_get_cpu_node(cpu, NULL);
	const u64 *spin_table_addr_prop;
	u32 *spin_table;
	extern void start_secondary_47x(void);

	BUG_ON(cpunode == NULL);

	/* Assume spin table. We could test for the enable-method in
	 * the device-tree but currently there's little point as it's
	 * our only supported method
	 */
	spin_table_addr_prop = of_get_property(cpunode, "cpu-release-addr",
					       NULL);
	if (spin_table_addr_prop == NULL) {
		pr_err("CPU%d: Can't start, missing cpu-release-addr !\n", cpu);
		return -ENOENT;
	}

	/* Assume it's mapped as part of the linear mapping. This is a bit
	 * fishy but will work fine for now
	 */
	spin_table = (u32 *)__va(*spin_table_addr_prop);
	pr_debug("CPU%d: Spin table mapped at %p\n", cpu, spin_table);

	spin_table[3] = cpu;
	smp_wmb();
	spin_table[1] = __pa(start_secondary_47x);
	mb();

	return 0;
}

static struct smp_ops_t iss_smp_ops = {
	.probe		= smp_mpic_probe,
	.message_pass	= smp_mpic_message_pass,
	.setup_cpu	= smp_iss4xx_setup_cpu,
	.kick_cpu	= smp_iss4xx_kick_cpu,
	.give_timebase	= smp_generic_give_timebase,
	.take_timebase	= smp_generic_take_timebase,
};

static void __init iss4xx_smp_init(void)
{
	if (mmu_has_feature(MMU_FTR_TYPE_47x))
		smp_ops = &iss_smp_ops;
}

#else /* CONFIG_SMP */
static void __init iss4xx_smp_init(void) { }
#endif /* CONFIG_SMP */

static void __init iss4xx_setup_arch(void)
{
	iss4xx_smp_init();
}

/*
 * Called very early, MMU is off, device-tree isn't unflattened
 */
static int __init iss4xx_probe(void)
{
	if (!of_machine_is_compatible("ibm,iss-4xx"))
		return 0;

	return 1;
}

define_machine(iss4xx) {
	.name			= "ISS-4xx",
	.probe			= iss4xx_probe,
	.progress		= udbg_progress,
	.init_IRQ		= iss4xx_init_irq,
	.setup_arch		= iss4xx_setup_arch,
	.restart		= ppc4xx_reset_system,
	.calibrate_decr		= generic_calibrate_decr,
};
