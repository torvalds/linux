/*
 * Board setup routines for the storcenter
 *
 * Copyright 2007 (C) Oyvind Repvik (nail@nslu2-linux.org)
 * Copyright 2007 Andy Wilcox, Jon Loeliger
 *
 * Based on linkstation.c by G. Liakhovetski
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of
 * any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/initrd.h>
#include <linux/mtd/physmap.h>
#include <linux/of_platform.h>

#include <asm/system.h>
#include <asm/time.h>
#include <asm/prom.h>
#include <asm/mpic.h>
#include <asm/pci-bridge.h>

#include "mpc10x.h"


#ifdef CONFIG_MTD_PHYSMAP
static struct mtd_partition storcenter_physmap_partitions[] = {
	{
		.name   = "kernel",
		.offset = 0x000000,
		.size   = 0x170000,
	},
	{
		.name   = "rootfs",
		.offset = 0x170000,
		.size   = 0x590000,
	},
	{
		.name   = "uboot",
		.offset = 0x700000,
		.size   = 0x040000,
	},
	{
		.name   = "config",
		.offset = 0x740000,
		.size   = 0x0c0000,
	},
};
#endif


static __initdata struct of_device_id storcenter_of_bus[] = {
	{ .name = "soc", },
	{},
};

static int __init storcenter_device_probe(void)
{
	of_platform_bus_probe(NULL, storcenter_of_bus, NULL);
	return 0;
}
machine_device_initcall(storcenter, storcenter_device_probe);


static int __init storcenter_add_bridge(struct device_node *dev)
{
#ifdef CONFIG_PCI
	int len;
	struct pci_controller *hose;
	const int *bus_range;

	printk("Adding PCI host bridge %s\n", dev->full_name);

	hose = pcibios_alloc_controller(dev);
	if (hose == NULL)
		return -ENOMEM;

	bus_range = of_get_property(dev, "bus-range", &len);
	hose->first_busno = bus_range ? bus_range[0] : 0;
	hose->last_busno = bus_range ? bus_range[1] : 0xff;

	setup_indirect_pci(hose, MPC10X_MAPB_CNFG_ADDR, MPC10X_MAPB_CNFG_DATA, 0);

	/* Interpret the "ranges" property */
	/* This also maps the I/O region and sets isa_io/mem_base */
	pci_process_bridge_OF_ranges(hose, dev, 1);
#endif

	return 0;
}

static void __init storcenter_setup_arch(void)
{
	struct device_node *np;

#ifdef CONFIG_MTD_PHYSMAP
	physmap_set_partitions(storcenter_physmap_partitions,
			       ARRAY_SIZE(storcenter_physmap_partitions));
#endif

	/* Lookup PCI host bridges */
	for_each_compatible_node(np, "pci", "mpc10x-pci")
		storcenter_add_bridge(np);

	printk(KERN_INFO "IOMEGA StorCenter\n");
}

/*
 * Interrupt setup and service.  Interrrupts on the turbostation come
 * from the four PCI slots plus onboard 8241 devices: I2C, DUART.
 */
static void __init storcenter_init_IRQ(void)
{
	struct mpic *mpic;
	struct device_node *dnp;
	const void *prop;
	int size;
	phys_addr_t paddr;

	dnp = of_find_node_by_type(NULL, "open-pic");
	if (dnp == NULL)
		return;

	prop = of_get_property(dnp, "reg", &size);
	if (prop == NULL) {
		of_node_put(dnp);
		return;
	}

	paddr = (phys_addr_t)of_translate_address(dnp, prop);
	mpic = mpic_alloc(dnp, paddr, MPIC_PRIMARY | MPIC_WANTS_RESET,
			4, 32, " EPIC     ");

	of_node_put(dnp);

	BUG_ON(mpic == NULL);

	/* PCI IRQs */
	/*
	 * 2.6.12 patch:
	 *         openpic_set_sources(0, 5, OpenPIC_Addr + 0x10200);
	 *         openpic_set_sources(5, 2, OpenPIC_Addr + 0x11120);
	 *         first_irq, num_irqs, __iomem first_ISR
	 *         o_ss: i, src: 0, fdf50200
	 *         o_ss: i, src: 1, fdf50220
	 *         o_ss: i, src: 2, fdf50240
	 *         o_ss: i, src: 3, fdf50260
	 *         o_ss: i, src: 4, fdf50280
	 *         o_ss: i, src: 5, fdf51120
	 *         o_ss: i, src: 6, fdf51140
	 */
	mpic_assign_isu(mpic, 0, paddr + 0x10200);
	mpic_assign_isu(mpic, 1, paddr + 0x10220);
	mpic_assign_isu(mpic, 2, paddr + 0x10240);
	mpic_assign_isu(mpic, 3, paddr + 0x10260);
	mpic_assign_isu(mpic, 4, paddr + 0x10280);
	mpic_assign_isu(mpic, 5, paddr + 0x11120);
	mpic_assign_isu(mpic, 6, paddr + 0x11140);

	mpic_init(mpic);
}

static void storcenter_restart(char *cmd)
{
	local_irq_disable();

	/* Set exception prefix high - to the firmware */
	_nmask_and_or_msr(0, MSR_IP);

	/* Wait for reset to happen */
	for (;;) ;
}

static int __init storcenter_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	return of_flat_dt_is_compatible(root, "storcenter");
}

define_machine(storcenter){
	.name 			= "IOMEGA StorCenter",
	.probe 			= storcenter_probe,
	.setup_arch 		= storcenter_setup_arch,
	.init_IRQ 		= storcenter_init_IRQ,
	.get_irq 		= mpic_get_irq,
	.restart 		= storcenter_restart,
	.calibrate_decr 	= generic_calibrate_decr,
};
