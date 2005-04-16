/*
 *  arch/mips/ddb5476/nile4.c --
 *  	low-level PIC code for NEC Vrc-5476 (Nile 4)
 *
 *  Copyright (C) 2000 Geert Uytterhoeven <geert@sonycom.com>
 *                     Sony Software Development Center Europe (SDCE), Brussels
 *
 *  Copyright 2001 MontaVista Software Inc.
 *  Author: jsun@mvista.com or jsun@junsun.net
 *
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/types.h>

#include <asm/addrspace.h>

#include <asm/ddb5xxx/ddb5xxx.h>


/*
 *  Interrupt Programming
 */
void nile4_map_irq(int nile4_irq, int cpu_irq)
{
	u32 offset, t;

	offset = DDB_INTCTRL;
	if (nile4_irq >= 8) {
		offset += 4;
		nile4_irq -= 8;
	}
	t = ddb_in32(offset);
	t &= ~(7 << (nile4_irq * 4));
	t |= cpu_irq << (nile4_irq * 4);
	ddb_out32(offset, t);
}

void nile4_map_irq_all(int cpu_irq)
{
	u32 all, t;

	all = cpu_irq;
	all |= all << 4;
	all |= all << 8;
	all |= all << 16;
	t = ddb_in32(DDB_INTCTRL);
	t &= 0x88888888;
	t |= all;
	ddb_out32(DDB_INTCTRL, t);
	t = ddb_in32(DDB_INTCTRL + 4);
	t &= 0x88888888;
	t |= all;
	ddb_out32(DDB_INTCTRL + 4, t);
}

void nile4_enable_irq(int nile4_irq)
{
	u32 offset, t;

	offset = DDB_INTCTRL;
	if (nile4_irq >= 8) {
		offset += 4;
		nile4_irq -= 8;
	}
	t = ddb_in32(offset);
	t |= 8 << (nile4_irq * 4);
	ddb_out32(offset, t);
}

void nile4_disable_irq(int nile4_irq)
{
	u32 offset, t;

	offset = DDB_INTCTRL;
	if (nile4_irq >= 8) {
		offset += 4;
		nile4_irq -= 8;
	}
	t = ddb_in32(offset);
	t &= ~(8 << (nile4_irq * 4));
	ddb_out32(offset, t);
}

void nile4_disable_irq_all(void)
{
	ddb_out32(DDB_INTCTRL, 0);
	ddb_out32(DDB_INTCTRL + 4, 0);
}

u16 nile4_get_irq_stat(int cpu_irq)
{
	return ddb_in16(DDB_INTSTAT0 + cpu_irq * 2);
}

void nile4_enable_irq_output(int cpu_irq)
{
	u32 t;

	t = ddb_in32(DDB_INTSTAT1 + 4);
	t |= 1 << (16 + cpu_irq);
	ddb_out32(DDB_INTSTAT1, t);
}

void nile4_disable_irq_output(int cpu_irq)
{
	u32 t;

	t = ddb_in32(DDB_INTSTAT1 + 4);
	t &= ~(1 << (16 + cpu_irq));
	ddb_out32(DDB_INTSTAT1, t);
}

void nile4_set_pci_irq_polarity(int pci_irq, int high)
{
	u32 t;

	t = ddb_in32(DDB_INTPPES);
	if (high)
		t &= ~(1 << (pci_irq * 2));
	else
		t |= 1 << (pci_irq * 2);
	ddb_out32(DDB_INTPPES, t);
}

void nile4_set_pci_irq_level_or_edge(int pci_irq, int level)
{
	u32 t;

	t = ddb_in32(DDB_INTPPES);
	if (level)
		t |= 2 << (pci_irq * 2);
	else
		t &= ~(2 << (pci_irq * 2));
	ddb_out32(DDB_INTPPES, t);
}

void nile4_clear_irq(int nile4_irq)
{
	ddb_out32(DDB_INTCLR, 1 << nile4_irq);
}

void nile4_clear_irq_mask(u32 mask)
{
	ddb_out32(DDB_INTCLR, mask);
}

u8 nile4_i8259_iack(void)
{
	u8 irq;
	u32 reg;

	/* Set window 0 for interrupt acknowledge */
	reg = ddb_in32(DDB_PCIINIT0);

	ddb_set_pmr(DDB_PCIINIT0, DDB_PCICMD_IACK, 0, DDB_PCI_ACCESS_32);
	irq = *(volatile u8 *) KSEG1ADDR(DDB_PCI_IACK_BASE);
	/* restore window 0 for PCI I/O space */
	// ddb_set_pmr(DDB_PCIINIT0, DDB_PCICMD_IO, 0, DDB_PCI_ACCESS_32);
	ddb_out32(DDB_PCIINIT0, reg);

	/* i8269.c set the base vector to be 0x0 */
	return irq + I8259_IRQ_BASE;
}

#if defined(CONFIG_RUNTIME_DEBUG)
void nile4_dump_irq_status(void)
{
	printk(KERN_DEBUG "
	       CPUSTAT = %p:%p\n", (void *) ddb_in32(DDB_CPUSTAT + 4),
	       (void *) ddb_in32(DDB_CPUSTAT));
	printk(KERN_DEBUG "
	       INTCTRL = %p:%p\n", (void *) ddb_in32(DDB_INTCTRL + 4),
	       (void *) ddb_in32(DDB_INTCTRL));
	printk(KERN_DEBUG
	       "INTSTAT0 = %p:%p\n",
	       (void *) ddb_in32(DDB_INTSTAT0 + 4),
	       (void *) ddb_in32(DDB_INTSTAT0));
	printk(KERN_DEBUG
	       "INTSTAT1 = %p:%p\n",
	       (void *) ddb_in32(DDB_INTSTAT1 + 4),
	       (void *) ddb_in32(DDB_INTSTAT1));
	printk(KERN_DEBUG
	       "INTCLR = %p:%p\n", (void *) ddb_in32(DDB_INTCLR + 4),
	       (void *) ddb_in32(DDB_INTCLR));
	printk(KERN_DEBUG
	       "INTPPES = %p:%p\n", (void *) ddb_in32(DDB_INTPPES + 4),
	       (void *) ddb_in32(DDB_INTPPES));
}
#endif
