/*
 * Copyright 2008-2011, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/smp.h>
#include <linux/delay.h>
#include <linux/time.h>

#include <asm/scom.h>

#include "wsp.h"
#include "ics.h"

#define WSP_SOC_COMPATIBLE	"ibm,wsp-soc"
#define PBIC_COMPATIBLE		"ibm,wsp-pbic"
#define COPRO_COMPATIBLE	"ibm,wsp-coprocessor"

static int __init wsp_probe_buses(void)
{
	static __initdata struct of_device_id bus_ids[] = {
		/*
		 * every node in between needs to be here or you won't
		 * find it
		 */
		{ .compatible = WSP_SOC_COMPATIBLE, },
		{ .compatible = PBIC_COMPATIBLE, },
		{ .compatible = COPRO_COMPATIBLE, },
		{},
	};
	of_platform_bus_probe(NULL, bus_ids, NULL);

	return 0;
}

void __init wsp_setup_arch(void)
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

void __init wsp_setup_irq(void)
{
	wsp_init_irq();
	opb_pic_init();
}


int __init wsp_probe_devices(void)
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

	wsp_probe_buses();

	return 0;
}

void wsp_halt(void)
{
	u64 val;
	scom_map_t m;
	struct device_node *dn;
	struct device_node *mine;
	struct device_node *me;

	me = of_get_cpu_node(smp_processor_id(), NULL);
	mine = scom_find_parent(me);

	/* This will halt all the A2s but not power off the chip */
	for_each_node_with_property(dn, "scom-controller") {
		if (dn == mine)
			continue;
		m = scom_map(dn, 0, 1);

		/* read-modify-write it so the HW probe does not get
		 * confused */
		val = scom_read(m, 0);
		val |= 1;
		scom_write(m, 0, val);
		scom_unmap(m);
	}
	m = scom_map(mine, 0, 1);
	val = scom_read(m, 0);
	val |= 1;
	scom_write(m, 0, val);
	/* should never return */
	scom_unmap(m);
}
