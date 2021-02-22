/*
 * Board setup routines for the Buffalo Linkstation / Kurobox Platform.
 *
 * Copyright (C) 2006 G. Liakhovetski (g.liakhovetski@gmx.de)
 *
 * Based on sandpoint.c by Mark A. Greer
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of
 * any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/initrd.h>
#include <linux/of_platform.h>

#include <asm/time.h>
#include <asm/prom.h>
#include <asm/mpic.h>
#include <asm/pci-bridge.h>

#include "mpc10x.h"

static const struct of_device_id of_bus_ids[] __initconst = {
	{ .type = "soc", },
	{ .compatible = "simple-bus", },
	{},
};

static int __init declare_of_platform_devices(void)
{
	of_platform_bus_probe(NULL, of_bus_ids, NULL);
	return 0;
}
machine_device_initcall(linkstation, declare_of_platform_devices);

static int __init linkstation_add_bridge(struct device_node *dev)
{
#ifdef CONFIG_PCI
	int len;
	struct pci_controller *hose;
	const int *bus_range;

	printk("Adding PCI host bridge %pOF\n", dev);

	bus_range = of_get_property(dev, "bus-range", &len);
	if (bus_range == NULL || len < 2 * sizeof(int))
		printk(KERN_WARNING "Can't get bus-range for %pOF, assume"
				" bus 0\n", dev);

	hose = pcibios_alloc_controller(dev);
	if (hose == NULL)
		return -ENOMEM;
	hose->first_busno = bus_range ? bus_range[0] : 0;
	hose->last_busno = bus_range ? bus_range[1] : 0xff;
	setup_indirect_pci(hose, 0xfec00000, 0xfee00000, 0);

	/* Interpret the "ranges" property */
	/* This also maps the I/O region and sets isa_io/mem_base */
	pci_process_bridge_OF_ranges(hose, dev, 1);
#endif
	return 0;
}

static void __init linkstation_setup_arch(void)
{
	printk(KERN_INFO "BUFFALO Network Attached Storage Series\n");
	printk(KERN_INFO "(C) 2002-2005 BUFFALO INC.\n");
}

static void __init linkstation_setup_pci(void)
{
	struct device_node *np;

	/* Lookup PCI host bridges */
	for_each_compatible_node(np, "pci", "mpc10x-pci")
		linkstation_add_bridge(np);
}

/*
 * Interrupt setup and service.  Interrupts on the linkstation come
 * from the four PCI slots plus onboard 8241 devices: I2C, DUART.
 */
static void __init linkstation_init_IRQ(void)
{
	struct mpic *mpic;

	mpic = mpic_alloc(NULL, 0, 0, 4, 0, " EPIC     ");
	BUG_ON(mpic == NULL);

	/* PCI IRQs */
	mpic_assign_isu(mpic, 0, mpic->paddr + 0x10200);

	/* I2C */
	mpic_assign_isu(mpic, 1, mpic->paddr + 0x11000);

	/* ttyS0, ttyS1 */
	mpic_assign_isu(mpic, 2, mpic->paddr + 0x11100);

	mpic_init(mpic);
}

extern void avr_uart_configure(void);
extern void avr_uart_send(const char);

static void __noreturn linkstation_restart(char *cmd)
{
	local_irq_disable();

	/* Reset system via AVR */
	avr_uart_configure();
	/* Send reboot command */
	avr_uart_send('C');

	for(;;)  /* Spin until reset happens */
		avr_uart_send('G');	/* "kick" */
}

static void __noreturn linkstation_power_off(void)
{
	local_irq_disable();

	/* Power down system via AVR */
	avr_uart_configure();
	/* send shutdown command */
	avr_uart_send('E');

	for(;;)  /* Spin until power-off happens */
		avr_uart_send('G');	/* "kick" */
	/* NOTREACHED */
}

static void __noreturn linkstation_halt(void)
{
	linkstation_power_off();
	/* NOTREACHED */
}

static void linkstation_show_cpuinfo(struct seq_file *m)
{
	seq_printf(m, "vendor\t\t: Buffalo Technology\n");
	seq_printf(m, "machine\t\t: Linkstation I/Kurobox(HG)\n");
}

static int __init linkstation_probe(void)
{
	if (!of_machine_is_compatible("linkstation"))
		return 0;

	pm_power_off = linkstation_power_off;

	return 1;
}

define_machine(linkstation){
	.name 			= "Buffalo Linkstation",
	.probe 			= linkstation_probe,
	.setup_arch 		= linkstation_setup_arch,
	.discover_phbs		= linkstation_setup_pci,
	.init_IRQ 		= linkstation_init_IRQ,
	.show_cpuinfo 		= linkstation_show_cpuinfo,
	.get_irq 		= mpic_get_irq,
	.restart 		= linkstation_restart,
	.halt	 		= linkstation_halt,
	.calibrate_decr 	= generic_calibrate_decr,
};
