/*
 *  arch/mips/ddb5476/irq.c -- NEC DDB Vrc-5476 interrupt routines
 *
 *  Copyright (C) 2000 Geert Uytterhoeven <geert@sonycom.com>
 *                     Sony Software Development Center Europe (SDCE), Brussels
 *
 * Re-write the whole thing to use new irq.c file.
 * Copyright (C) 2001 MontaVista Software Inc.
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
 *
 */
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/interrupt.h>

#include <asm/i8259.h>
#include <asm/io.h>
#include <asm/ptrace.h>

#include <asm/ddb5xxx/ddb5xxx.h>

#define M1543_PNP_CONFIG	0x03f0	/* PnP Config Port */
#define M1543_PNP_INDEX		0x03f0	/* PnP Index Port */
#define M1543_PNP_DATA		0x03f1	/* PnP Data Port */

#define M1543_PNP_ALT_CONFIG	0x0370	/* Alternative PnP Config Port */
#define M1543_PNP_ALT_INDEX	0x0370	/* Alternative PnP Index Port */
#define M1543_PNP_ALT_DATA	0x0371	/* Alternative PnP Data Port */

#define M1543_INT1_MASTER_CTRL	0x0020	/* INT_1 (master) Control Register */
#define M1543_INT1_MASTER_MASK	0x0021	/* INT_1 (master) Mask Register */

#define M1543_INT1_SLAVE_CTRL	0x00a0	/* INT_1 (slave) Control Register */
#define M1543_INT1_SLAVE_MASK	0x00a1	/* INT_1 (slave) Mask Register */

#define M1543_INT1_MASTER_ELCR	0x04d0	/* INT_1 (master) Edge/Level Control */
#define M1543_INT1_SLAVE_ELCR	0x04d1	/* INT_1 (slave) Edge/Level Control */

static void m1543_irq_setup(void)
{
	/*
	 *  The ALI M1543 has 13 interrupt inputs, IRQ1..IRQ13.  Not all
	 *  the possible IO sources in the M1543 are in use by us.  We will
	 *  use the following mapping:
	 *
	 *      IRQ1  - keyboard (default set by M1543)
	 *      IRQ3  - reserved for UART B (default set by M1543) (note that
	 *              the schematics for the DDB Vrc-5476 board seem to
	 *              indicate that IRQ3 is connected to the DS1386
	 *              watchdog timer interrupt output so we might have
	 *              a conflict)
	 *      IRQ4  - reserved for UART A (default set by M1543)
	 *      IRQ5  - parallel (default set by M1543)
	 *      IRQ8  - DS1386 time of day (RTC) interrupt
	 *      IRQ9  - USB (hardwired in ddb_setup)
	 *      IRQ10 - PMU (hardwired in ddb_setup)
	 *      IRQ12 - mouse
	 *      IRQ14,15 - IDE controller (need to be confirmed, jsun)
	 */

	/*
	 *  Assing mouse interrupt to IRQ12
	 */

	/* Enter configuration mode */
	outb(0x51, M1543_PNP_CONFIG);
	outb(0x23, M1543_PNP_CONFIG);

	/* Select logical device 7 (Keyboard) */
	outb(0x07, M1543_PNP_INDEX);
	outb(0x07, M1543_PNP_DATA);

	/* Select IRQ12 */
	outb(0x72, M1543_PNP_INDEX);
	outb(0x0c, M1543_PNP_DATA);

	/* Leave configration mode */
	outb(0xbb, M1543_PNP_CONFIG);
}

static void nile4_irq_setup(void)
{
	int i;

	/* Map all interrupts to CPU int #0 (IP2) */
	nile4_map_irq_all(0);

	/* PCI INTA#-E# must be level triggered */
	nile4_set_pci_irq_level_or_edge(0, 1);
	nile4_set_pci_irq_level_or_edge(1, 1);
	nile4_set_pci_irq_level_or_edge(2, 1);
	nile4_set_pci_irq_level_or_edge(3, 1);

	/* PCI INTA#, B#, D# must be active low, INTC# must be active high */
	nile4_set_pci_irq_polarity(0, 0);
	nile4_set_pci_irq_polarity(1, 0);
	nile4_set_pci_irq_polarity(2, 1);
	nile4_set_pci_irq_polarity(3, 0);

	for (i = 0; i < 16; i++)
		nile4_clear_irq(i);

	/* Enable CPU int #0 */
	nile4_enable_irq_output(0);

	/* memory resource acquire in ddb_setup */
}

static struct irqaction irq_cascade = { no_action, 0, CPU_MASK_NONE, "cascade", NULL, NULL };
static struct irqaction irq_error = { no_action, 0, CPU_MASK_NONE, "error", NULL, NULL };

extern int setup_irq(unsigned int irq, struct irqaction *irqaction);
extern void mips_cpu_irq_init(u32 irq_base);
extern void vrc5476_irq_init(u32 irq_base);

extern void vrc5476_irq_dispatch(struct pt_regs *regs);

asmlinkage void plat_irq_dispatch(struct pt_regs *regs)
{
	unsigned int pending = read_c0_cause() & read_c0_status();

	if (pending & STATUSF_IP7)
		do_IRQ(CPU_IRQ_BASE + 7, regs);
	else if (pending & STATUSF_IP2)
		vrc5476_irq_dispatch(regs);
	else if (pending & STATUSF_IP3)
		do_IRQ(CPU_IRQ_BASE + 3, regs);
	else if (pending & STATUSF_IP4)
		do_IRQ(CPU_IRQ_BASE + 4, regs);
	else if (pending & STATUSF_IP5)
		do_IRQ(CPU_IRQ_BASE + 5, regs);
	else if (pending & STATUSF_IP6)
		do_IRQ(CPU_IRQ_BASE + 6, regs);
	else if (pending & STATUSF_IP0)
		do_IRQ(CPU_IRQ_BASE, regs);
	else if (pending & STATUSF_IP1)
		do_IRQ(CPU_IRQ_BASE + 1, regs);

	vrc5476_irq_dispatch(regs);
}

void __init arch_init_irq(void)
{
	/* hardware initialization */
	nile4_irq_setup();
	m1543_irq_setup();

	/* controller setup */
	init_i8259_irqs();
	vrc5476_irq_init(VRC5476_IRQ_BASE);
	mips_cpu_irq_init(CPU_IRQ_BASE);

	/* setup cascade interrupts */
	setup_irq(VRC5476_IRQ_BASE + VRC5476_I8259_CASCADE, &irq_cascade);
	setup_irq(CPU_IRQ_BASE + CPU_VRC5476_CASCADE, &irq_cascade);

	/* setup error interrupts for debugging */
	setup_irq(VRC5476_IRQ_BASE + VRC5476_IRQ_CPCE, &irq_error);
	setup_irq(VRC5476_IRQ_BASE + VRC5476_IRQ_CNTD, &irq_error);
	setup_irq(VRC5476_IRQ_BASE + VRC5476_IRQ_MCE, &irq_error);
	setup_irq(VRC5476_IRQ_BASE + VRC5476_IRQ_LBRT, &irq_error);
	setup_irq(VRC5476_IRQ_BASE + VRC5476_IRQ_PCIS, &irq_error);
	setup_irq(VRC5476_IRQ_BASE + VRC5476_IRQ_PCI, &irq_error);
}
