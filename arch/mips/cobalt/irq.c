/*
 * IRQ vector handles
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1996, 1997, 2003 by Ralf Baechle
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/pci.h>

#include <asm/i8259.h>
#include <asm/irq_cpu.h>
#include <asm/gt64120.h>

#include <cobalt.h>

/*
 * We have two types of interrupts that we handle, ones that come in through
 * the CPU interrupt lines, and ones that come in on the via chip. The CPU
 * mappings are:
 *
 *    16   - Software interrupt 0 (unused)	IE_SW0
 *    17   - Software interrupt 1 (unused)	IE_SW1
 *    18   - Galileo chip (timer)		IE_IRQ0
 *    19   - Tulip 0 + NCR SCSI			IE_IRQ1
 *    20   - Tulip 1				IE_IRQ2
 *    21   - 16550 UART				IE_IRQ3
 *    22   - VIA southbridge PIC		IE_IRQ4
 *    23   - unused				IE_IRQ5
 *
 * The VIA chip is a master/slave 8259 setup and has the following interrupts:
 *
 *     8  - RTC
 *     9  - PCI
 *    14  - IDE0
 *    15  - IDE1
 */

static inline void galileo_irq(void)
{
	unsigned int mask, pending, devfn;

	mask = GT_READ(GT_INTRMASK_OFS);
	pending = GT_READ(GT_INTRCAUSE_OFS) & mask;

	if (pending & GT_INTR_T0EXP_MSK) {
		GT_WRITE(GT_INTRCAUSE_OFS, ~GT_INTR_T0EXP_MSK);
		do_IRQ(COBALT_GALILEO_IRQ);
	} else if (pending & GT_INTR_RETRYCTR0_MSK) {
		devfn = GT_READ(GT_PCI0_CFGADDR_OFS) >> 8;
		GT_WRITE(GT_INTRCAUSE_OFS, ~GT_INTR_RETRYCTR0_MSK);
		printk(KERN_WARNING
		       "Galileo: PCI retry count exceeded (%02x.%u)\n",
		       PCI_SLOT(devfn), PCI_FUNC(devfn));
	} else {
		GT_WRITE(GT_INTRMASK_OFS, mask & ~pending);
		printk(KERN_WARNING
		       "Galileo: masking unexpected interrupt %08x\n", pending);
	}
}

static inline void via_pic_irq(void)
{
	int irq;

	irq = i8259_irq();
	if (irq >= 0)
		do_IRQ(irq);
}

asmlinkage void plat_irq_dispatch(void)
{
	unsigned pending = read_c0_status() & read_c0_cause();

	if (pending & CAUSEF_IP2)		/* COBALT_GALILEO_IRQ (18) */
		galileo_irq();
	else if (pending & CAUSEF_IP6)		/* COBALT_VIA_IRQ (22) */
		via_pic_irq();
	else if (pending & CAUSEF_IP3)		/* COBALT_ETH0_IRQ (19) */
		do_IRQ(COBALT_CPU_IRQ + 3);
	else if (pending & CAUSEF_IP4)		/* COBALT_ETH1_IRQ (20) */
		do_IRQ(COBALT_CPU_IRQ + 4);
	else if (pending & CAUSEF_IP5)		/* COBALT_SERIAL_IRQ (21) */
		do_IRQ(COBALT_CPU_IRQ + 5);
	else if (pending & CAUSEF_IP7)		/* IRQ 23 */
		do_IRQ(COBALT_CPU_IRQ + 7);
}

static struct irqaction irq_via = {
	no_action, 0, { { 0, } }, "cascade", NULL, NULL
};

void __init arch_init_irq(void)
{
	/*
	 * Mask all Galileo interrupts. The Galileo
	 * handler is set in cobalt_timer_setup()
	 */
	GT_WRITE(GT_INTRMASK_OFS, 0);

	init_i8259_irqs();				/*  0 ... 15 */
	mips_cpu_irq_init();		/* 16 ... 23 */

	/*
	 * Mask all cpu interrupts
	 *  (except IE4, we already masked those at VIA level)
	 */
	change_c0_status(ST0_IM, IE_IRQ4);

	setup_irq(COBALT_VIA_IRQ, &irq_via);
}
