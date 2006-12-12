/*
 * Copyright 2001 MontaVista Software Inc.
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
 *
 *  arch/mips/ddb5xxx/ddb5477/irq.c
 *     The irq setup and misc routines for DDB5476.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/types.h>
#include <linux/ptrace.h>

#include <asm/i8259.h>
#include <asm/system.h>
#include <asm/mipsregs.h>
#include <asm/debug.h>
#include <asm/addrspace.h>
#include <asm/bootinfo.h>

#include <asm/ddb5xxx/ddb5xxx.h>


/*
 * IRQ mapping
 *
 *  0-7: 8 CPU interrupts
 *	0 -	software interrupt 0
 *	1 - 	software interrupt 1
 *	2 - 	most Vrc5477 interrupts are routed to this pin
 *	3 - 	(optional) some other interrupts routed to this pin for debugg
 *	4 - 	not used
 *	5 - 	not used
 *	6 - 	not used
 *	7 - 	cpu timer (used by default)
 *
 *  8-39: 32 Vrc5477 interrupt sources
 *	(refer to the Vrc5477 manual)
 */

#define	PCI0			DDB_INTPPES0
#define	PCI1			DDB_INTPPES1

#define	ACTIVE_LOW		1
#define	ACTIVE_HIGH		0

#define	LEVEL_SENSE		2
#define	EDGE_TRIGGER		0

#define	INTA			0
#define	INTB			1
#define	INTC			2
#define	INTD			3
#define	INTE			4

static inline void
set_pci_int_attr(u32 pci, u32 intn, u32 active, u32 trigger)
{
	u32 reg_value;
	u32 reg_bitmask;

	reg_value = ddb_in32(pci);
	reg_bitmask = 0x3 << (intn * 2);

	reg_value &= ~reg_bitmask;
	reg_value |= (active | trigger) << (intn * 2);
	ddb_out32(pci, reg_value);
}

extern void vrc5477_irq_init(u32 base);
extern void mips_cpu_irq_init(u32 base);
static struct irqaction irq_cascade = { no_action, 0, CPU_MASK_NONE, "cascade", NULL, NULL };

void __init arch_init_irq(void)
{
	/* by default, we disable all interrupts and route all vrc5477
	 * interrupts to pin 0 (irq 2) */
	ddb_out32(DDB_INTCTRL0, 0);
	ddb_out32(DDB_INTCTRL1, 0);
	ddb_out32(DDB_INTCTRL2, 0);
	ddb_out32(DDB_INTCTRL3, 0);

	clear_c0_status(0xff00);
	set_c0_status(0x0400);

	/* setup PCI interrupt attributes */
	set_pci_int_attr(PCI0, INTA, ACTIVE_LOW, LEVEL_SENSE);
	set_pci_int_attr(PCI0, INTB, ACTIVE_LOW, LEVEL_SENSE);
	if (mips_machtype == MACH_NEC_ROCKHOPPERII)
		set_pci_int_attr(PCI0, INTC, ACTIVE_HIGH, LEVEL_SENSE);
	else
		set_pci_int_attr(PCI0, INTC, ACTIVE_LOW, LEVEL_SENSE);
	set_pci_int_attr(PCI0, INTD, ACTIVE_LOW, LEVEL_SENSE);
	set_pci_int_attr(PCI0, INTE, ACTIVE_LOW, LEVEL_SENSE);

	set_pci_int_attr(PCI1, INTA, ACTIVE_LOW, LEVEL_SENSE);
	set_pci_int_attr(PCI1, INTB, ACTIVE_LOW, LEVEL_SENSE);
	set_pci_int_attr(PCI1, INTC, ACTIVE_LOW, LEVEL_SENSE);
	set_pci_int_attr(PCI1, INTD, ACTIVE_LOW, LEVEL_SENSE);
	set_pci_int_attr(PCI1, INTE, ACTIVE_LOW, LEVEL_SENSE);

	/*
	 * for debugging purpose, we enable several error interrupts
	 * and route them to pin 1. (IP3)
	 */
	/* cpu parity check - 0 */
	ll_vrc5477_irq_route(0, 1); ll_vrc5477_irq_enable(0);
	/* cpu no-target decode - 1 */
	ll_vrc5477_irq_route(1, 1); ll_vrc5477_irq_enable(1);
	/* local bus read time-out - 7 */
	ll_vrc5477_irq_route(7, 1); ll_vrc5477_irq_enable(7);
	/* PCI SERR# - 14 */
	ll_vrc5477_irq_route(14, 1); ll_vrc5477_irq_enable(14);
	/* PCI internal error - 15 */
	ll_vrc5477_irq_route(15, 1); ll_vrc5477_irq_enable(15);
	/* IOPCI SERR# - 30 */
	ll_vrc5477_irq_route(30, 1); ll_vrc5477_irq_enable(30);
	/* IOPCI internal error - 31 */
	ll_vrc5477_irq_route(31, 1); ll_vrc5477_irq_enable(31);

	/* init all controllers */
	init_i8259_irqs();
	mips_cpu_irq_init(CPU_IRQ_BASE);
	vrc5477_irq_init(VRC5477_IRQ_BASE);


	/* setup cascade interrupts */
	setup_irq(VRC5477_IRQ_BASE + VRC5477_I8259_CASCADE, &irq_cascade);
	setup_irq(CPU_IRQ_BASE + CPU_VRC5477_CASCADE, &irq_cascade);
}

u8 i8259_interrupt_ack(void)
{
	u8 irq;
	u32 reg;

	/* Set window 0 for interrupt acknowledge */
	reg = ddb_in32(DDB_PCIINIT10);

	ddb_set_pmr(DDB_PCIINIT10, DDB_PCICMD_IACK, 0, DDB_PCI_ACCESS_32);
	irq = *(volatile u8 *) KSEG1ADDR(DDB_PCI_IACK_BASE);
	ddb_out32(DDB_PCIINIT10, reg);

	/* i8259.c set the base vector to be 0x0 */
	return irq + I8259_IRQ_BASE;
}
/*
 * the first level int-handler will jump here if it is a vrc5477 irq
 */
#define	NUM_5477_IRQS	32
static void vrc5477_irq_dispatch(void)
{
	u32 intStatus;
	u32 bitmask;
	u32 i;

	db_assert(ddb_in32(DDB_INT2STAT) == 0);
	db_assert(ddb_in32(DDB_INT3STAT) == 0);
	db_assert(ddb_in32(DDB_INT4STAT) == 0);
	db_assert(ddb_in32(DDB_NMISTAT) == 0);

	if (ddb_in32(DDB_INT1STAT) != 0) {
#if defined(CONFIG_RUNTIME_DEBUG)
		vrc5477_show_int_regs();
#endif
		panic("error interrupt has happened.");
	}

	intStatus = ddb_in32(DDB_INT0STAT);

	if (mips_machtype == MACH_NEC_ROCKHOPPERII) {
		/* check for i8259 interrupts */
		if (intStatus & (1 << VRC5477_I8259_CASCADE)) {
			int i8259_irq = i8259_interrupt_ack();
			do_IRQ(I8259_IRQ_BASE + i8259_irq);
			return;
		}
	}

	for (i=0, bitmask=1; i<= NUM_5477_IRQS; bitmask <<=1, i++) {
		/* do we need to "and" with the int mask? */
		if (intStatus & bitmask) {
			do_IRQ(VRC5477_IRQ_BASE + i);
			return;
		}
	}
}

#define VR5477INTS (STATUSF_IP2|STATUSF_IP3|STATUSF_IP4|STATUSF_IP5|STATUSF_IP6)

asmlinkage void plat_irq_dispatch(void)
{
	unsigned int pending = read_c0_cause() & read_c0_status();

	if (pending & STATUSF_IP7)
		do_IRQ(CPU_IRQ_BASE + 7);
	else if (pending & VR5477INTS)
		vrc5477_irq_dispatch();
	else if (pending & STATUSF_IP0)
		do_IRQ(CPU_IRQ_BASE);
	else if (pending & STATUSF_IP1)
		do_IRQ(CPU_IRQ_BASE + 1);
	else
		spurious_interrupt();
}
