/*
 * Copyright 2008-2011, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/of.h>
#include <linux/smp.h>

#include <asm/machdep.h>
#include <asm/system.h>
#include <asm/time.h>
#include <asm/udbg.h>

#include "ics.h"
#include "wsp.h"


static void psr2_spin(void)
{
	hard_irq_disable();
	for (;;) ;
}

static void psr2_restart(char *cmd)
{
	psr2_spin();
}

static int psr2_probe_devices(void)
{
	struct device_node *np;

	/* Our RTC is a ds1500. It seems to be programatically compatible
	 * with the ds1511 for which we have a driver so let's use that
	 */
	np = of_find_compatible_node(NULL, NULL, "dallas,ds1500");
	if (np != NULL) {
		struct resource res;
		if (of_address_to_resource(np, 0, &res) == 0)
			platform_device_register_simple("ds1511", 0, &res, 1);
	}
	return 0;
}
machine_arch_initcall(psr2_md, psr2_probe_devices);

static void __init psr2_setup_arch(void)
{
	/* init to some ~sane value until calibrate_delay() runs */
	loops_per_jiffy = 50000000;

	scom_init_wsp();

	/* Setup SMP callback */
#ifdef CONFIG_SMP
	a2_setup_smp();
#endif
#ifdef CONFIG_PCI
	wsp_setup_pci();
#endif

}

static int __init psr2_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	if (!of_flat_dt_is_compatible(root, "ibm,psr2"))
		return 0;

	return 1;
}

static void __init psr2_init_irq(void)
{
	wsp_init_irq();
	opb_pic_init();
}

define_machine(psr2_md) {
	.name			= "PSR2 A2",
	.probe			= psr2_probe,
	.setup_arch		= psr2_setup_arch,
	.restart		= psr2_restart,
	.power_off		= psr2_spin,
	.halt			= psr2_spin,
	.calibrate_decr		= generic_calibrate_decr,
	.init_IRQ		= psr2_init_irq,
	.progress		= udbg_progress,
	.power_save		= book3e_idle,
};
