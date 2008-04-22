/* VISWS traps */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>

#include <asm/io.h>
#include <asm/arch_hooks.h>
#include <asm/apic.h>
#include "cobalt.h"
#include "lithium.h"


#define A01234 (LI_INTA_0 | LI_INTA_1 | LI_INTA_2 | LI_INTA_3 | LI_INTA_4)
#define BCD (LI_INTB | LI_INTC | LI_INTD)
#define ALLDEVS (A01234 | BCD)

static __init void lithium_init(void)
{
	set_fixmap(FIX_LI_PCIA, LI_PCI_A_PHYS);
	set_fixmap(FIX_LI_PCIB, LI_PCI_B_PHYS);

	if ((li_pcia_read16(PCI_VENDOR_ID) != PCI_VENDOR_ID_SGI) ||
	    (li_pcia_read16(PCI_DEVICE_ID) != PCI_DEVICE_ID_SGI_LITHIUM)) {
		printk(KERN_EMERG "Lithium hostbridge %c not found\n", 'A');
		panic("This machine is not SGI Visual Workstation 320/540");
	}

	if ((li_pcib_read16(PCI_VENDOR_ID) != PCI_VENDOR_ID_SGI) ||
	    (li_pcib_read16(PCI_DEVICE_ID) != PCI_DEVICE_ID_SGI_LITHIUM)) {
		printk(KERN_EMERG "Lithium hostbridge %c not found\n", 'B');
		panic("This machine is not SGI Visual Workstation 320/540");
	}

	li_pcia_write16(LI_PCI_INTEN, ALLDEVS);
	li_pcib_write16(LI_PCI_INTEN, ALLDEVS);
}

static __init void cobalt_init(void)
{
	/*
	 * On normal SMP PC this is used only with SMP, but we have to
	 * use it and set it up here to start the Cobalt clock
	 */
	set_fixmap(FIX_APIC_BASE, APIC_DEFAULT_PHYS_BASE);
	setup_local_APIC();
	printk(KERN_INFO "Local APIC Version %#x, ID %#x\n",
		(unsigned int)apic_read(APIC_LVR),
		(unsigned int)apic_read(APIC_ID));

	set_fixmap(FIX_CO_CPU, CO_CPU_PHYS);
	set_fixmap(FIX_CO_APIC, CO_APIC_PHYS);
	printk(KERN_INFO "Cobalt Revision %#lx, APIC ID %#lx\n",
		co_cpu_read(CO_CPU_REV), co_apic_read(CO_APIC_ID));

	/* Enable Cobalt APIC being careful to NOT change the ID! */
	co_apic_write(CO_APIC_ID, co_apic_read(CO_APIC_ID) | CO_APIC_ENABLE);

	printk(KERN_INFO "Cobalt APIC enabled: ID reg %#lx\n",
		co_apic_read(CO_APIC_ID));
}

void __init trap_init_hook(void)
{
	lithium_init();
	cobalt_init();
}
