/*
 *	linux/arch/alpha/kernel/sys_miata.c
 *
 *	Copyright (C) 1995 David A Rusling
 *	Copyright (C) 1996 Jay A Estabrook
 *	Copyright (C) 1998, 1999, 2000 Richard Henderson
 *
 * Code supporting the MIATA (EV56+PYXIS).
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/reboot.h>

#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/dma.h>
#include <asm/irq.h>
#include <asm/mmu_context.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/core_cia.h>
#include <asm/tlbflush.h>

#include "proto.h"
#include "irq_impl.h"
#include "pci_impl.h"
#include "machvec_impl.h"


static void 
miata_srm_device_interrupt(unsigned long vector, struct pt_regs * regs)
{
	int irq;

	irq = (vector - 0x800) >> 4;

	/*
	 * I really hate to do this, but the MIATA SRM console ignores the
	 *  low 8 bits in the interrupt summary register, and reports the
	 *  vector 0x80 *lower* than I expected from the bit numbering in
	 *  the documentation.
	 * This was done because the low 8 summary bits really aren't used
	 *  for reporting any interrupts (the PCI-ISA bridge, bit 7, isn't
	 *  used for this purpose, as PIC interrupts are delivered as the
	 *  vectors 0x800-0x8f0).
	 * But I really don't want to change the fixup code for allocation
	 *  of IRQs, nor the alpha_irq_mask maintenance stuff, both of which
	 *  look nice and clean now.
	 * So, here's this grotty hack... :-(
	 */
	if (irq >= 16)
		irq = irq + 8;

	handle_irq(irq, regs);
}

static void __init
miata_init_irq(void)
{
	if (alpha_using_srm)
		alpha_mv.device_interrupt = miata_srm_device_interrupt;

#if 0
	/* These break on MiataGL so we'll try not to do it at all.  */
	*(vulp)PYXIS_INT_HILO = 0x000000B2UL; mb();	/* ISA/NMI HI */
	*(vulp)PYXIS_RT_COUNT = 0UL; mb();		/* clear count */
#endif

	init_i8259a_irqs();

	/* Not interested in the bogus interrupts (3,10), Fan Fault (0),
           NMI (1), or EIDE (9).

	   We also disable the risers (4,5), since we don't know how to
	   route the interrupts behind the bridge.  */
	init_pyxis_irqs(0x63b0000);

	common_init_isa_dma();
	setup_irq(16+2, &halt_switch_irqaction);	/* SRM only? */
	setup_irq(16+6, &timer_cascade_irqaction);
}


/*
 * PCI Fixup configuration.
 *
 * Summary @ PYXIS_INT_REQ:
 * Bit      Meaning
 * 0        Fan Fault
 * 1        NMI
 * 2        Halt/Reset switch
 * 3        none
 * 4        CID0 (Riser ID)
 * 5        CID1 (Riser ID)
 * 6        Interval timer
 * 7        PCI-ISA Bridge
 * 8        Ethernet
 * 9        EIDE (deprecated, ISA 14/15 used)
 *10        none
 *11        USB
 *12        Interrupt Line A from slot 4
 *13        Interrupt Line B from slot 4
 *14        Interrupt Line C from slot 4
 *15        Interrupt Line D from slot 4
 *16        Interrupt Line A from slot 5
 *17        Interrupt line B from slot 5
 *18        Interrupt Line C from slot 5
 *19        Interrupt Line D from slot 5
 *20        Interrupt Line A from slot 1
 *21        Interrupt Line B from slot 1
 *22        Interrupt Line C from slot 1
 *23        Interrupt Line D from slot 1
 *24        Interrupt Line A from slot 2
 *25        Interrupt Line B from slot 2
 *26        Interrupt Line C from slot 2
 *27        Interrupt Line D from slot 2
 *27        Interrupt Line A from slot 3
 *29        Interrupt Line B from slot 3
 *30        Interrupt Line C from slot 3
 *31        Interrupt Line D from slot 3
 *
 * The device to slot mapping looks like:
 *
 * Slot     Device
 *  3       DC21142 Ethernet
 *  4       EIDE CMD646
 *  5       none
 *  6       USB
 *  7       PCI-ISA bridge
 *  8       PCI-PCI Bridge      (SBU Riser)
 *  9       none
 * 10       none
 * 11       PCI on board slot 4 (SBU Riser)
 * 12       PCI on board slot 5 (SBU Riser)
 *
 *  These are behind the bridge, so I'm not sure what to do...
 *
 * 13       PCI on board slot 1 (SBU Riser)
 * 14       PCI on board slot 2 (SBU Riser)
 * 15       PCI on board slot 3 (SBU Riser)
 *   
 *
 * This two layered interrupt approach means that we allocate IRQ 16 and 
 * above for PCI interrupts.  The IRQ relates to which bit the interrupt
 * comes in on.  This makes interrupt processing much easier.
 */

static int __init
miata_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
        static char irq_tab[18][5] __initdata = {
		/*INT    INTA   INTB   INTC   INTD */
		{16+ 8, 16+ 8, 16+ 8, 16+ 8, 16+ 8},  /* IdSel 14,  DC21142 */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 15,  EIDE    */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 16,  none    */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 17,  none    */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 18,  PCI-ISA */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 19,  PCI-PCI */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 20,  none    */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 21,  none    */
		{16+12, 16+12, 16+13, 16+14, 16+15},  /* IdSel 22,  slot 4  */
		{16+16, 16+16, 16+17, 16+18, 16+19},  /* IdSel 23,  slot 5  */
		/* the next 7 are actually on PCI bus 1, across the bridge */
		{16+11, 16+11, 16+11, 16+11, 16+11},  /* IdSel 24,  QLISP/GL*/
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 25,  none    */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 26,  none    */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 27,  none    */
		{16+20, 16+20, 16+21, 16+22, 16+23},  /* IdSel 28,  slot 1  */
		{16+24, 16+24, 16+25, 16+26, 16+27},  /* IdSel 29,  slot 2  */
		{16+28, 16+28, 16+29, 16+30, 16+31},  /* IdSel 30,  slot 3  */
		/* This bridge is on the main bus of the later orig MIATA */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 31,  PCI-PCI */
        };
	const long min_idsel = 3, max_idsel = 20, irqs_per_slot = 5;
	
	/* the USB function of the 82c693 has it's interrupt connected to 
           the 2nd 8259 controller. So we have to check for it first. */

	if((slot == 7) && (PCI_FUNC(dev->devfn) == 3)) {
		u8 irq=0;

		if(pci_read_config_byte(pci_find_slot(dev->bus->number, dev->devfn & ~(7)), 0x40,&irq)!=PCIBIOS_SUCCESSFUL)
			return -1;
		else	
			return irq;
	}

	return COMMON_TABLE_LOOKUP;
}

static u8 __init
miata_swizzle(struct pci_dev *dev, u8 *pinp)
{
	int slot, pin = *pinp;

	if (dev->bus->number == 0) {
		slot = PCI_SLOT(dev->devfn);
	}		
	/* Check for the built-in bridge.  */
	else if ((PCI_SLOT(dev->bus->self->devfn) == 8) ||
		 (PCI_SLOT(dev->bus->self->devfn) == 20)) {
		slot = PCI_SLOT(dev->devfn) + 9;
	}
	else 
	{
		/* Must be a card-based bridge.  */
		do {
			if ((PCI_SLOT(dev->bus->self->devfn) == 8) ||
			    (PCI_SLOT(dev->bus->self->devfn) == 20)) {
				slot = PCI_SLOT(dev->devfn) + 9;
				break;
			}
			pin = bridge_swizzle(pin, PCI_SLOT(dev->devfn));

			/* Move up the chain of bridges.  */
			dev = dev->bus->self;
			/* Slot of the next bridge.  */
			slot = PCI_SLOT(dev->devfn);
		} while (dev->bus->self);
	}
	*pinp = pin;
	return slot;
}

static void __init
miata_init_pci(void)
{
	cia_init_pci();
	SMC669_Init(0); /* it might be a GL (fails harmlessly if not) */
	es1888_init();
}

static void
miata_kill_arch(int mode)
{
	cia_kill_arch(mode);

#ifndef ALPHA_RESTORE_SRM_SETUP
	switch(mode) {
	case LINUX_REBOOT_CMD_RESTART:
		/* Who said DEC engineers have no sense of humor? ;-)  */ 
		if (alpha_using_srm) {
			*(vuip) PYXIS_RESET = 0x0000dead; 
			mb(); 
		}
		break;
	case LINUX_REBOOT_CMD_HALT:
		break;
	case LINUX_REBOOT_CMD_POWER_OFF:
		break;
	}

	halt();
#endif
}


/*
 * The System Vector
 */

struct alpha_machine_vector miata_mv __initmv = {
	.vector_name		= "Miata",
	DO_EV5_MMU,
	DO_DEFAULT_RTC,
	DO_PYXIS_IO,
	.machine_check		= cia_machine_check,
	.max_isa_dma_address	= ALPHA_MAX_ISA_DMA_ADDRESS,
	.min_io_address		= DEFAULT_IO_BASE,
	.min_mem_address	= DEFAULT_MEM_BASE,
	.pci_dac_offset		= PYXIS_DAC_OFFSET,

	.nr_irqs		= 48,
	.device_interrupt	= pyxis_device_interrupt,

	.init_arch		= pyxis_init_arch,
	.init_irq		= miata_init_irq,
	.init_rtc		= common_init_rtc,
	.init_pci		= miata_init_pci,
	.kill_arch		= miata_kill_arch,
	.pci_map_irq		= miata_map_irq,
	.pci_swizzle		= miata_swizzle,
};
ALIAS_MV(miata)
