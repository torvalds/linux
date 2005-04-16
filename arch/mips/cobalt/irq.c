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

#include <asm/i8259.h>
#include <asm/irq_cpu.h>
#include <asm/gt64120.h>
#include <asm/ptrace.h>

#include <asm/cobalt/cobalt.h>

extern void cobalt_handle_int(void);

/*
 * We have two types of interrupts that we handle, ones that come in through
 * the CPU interrupt lines, and ones that come in on the via chip. The CPU
 * mappings are:
 *
 *    16,  - Software interrupt 0 (unused)	IE_SW0
 *    17   - Software interrupt 1 (unused)	IE_SW0
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

asmlinkage void cobalt_irq(struct pt_regs *regs)
{
	unsigned int pending = read_c0_status() & read_c0_cause();

	if (pending & CAUSEF_IP2) {			/* int 18 */
		unsigned long irq_src = GALILEO_INL(GT_INTRCAUSE_OFS);

		/* Check for timer irq ... */
		if (irq_src & GALILEO_T0EXP) {
			/* Clear the int line */
			GALILEO_OUTL(0, GT_INTRCAUSE_OFS);
			do_IRQ(COBALT_TIMER_IRQ, regs);
		}
		return;
	}

	if (pending & CAUSEF_IP6) {			/* int 22 */
		int irq = i8259_irq();

		if (irq >= 0)
			do_IRQ(irq, regs);
		return;
	}

	if (pending & CAUSEF_IP3) {			/* int 19 */
		do_IRQ(COBALT_ETH0_IRQ, regs);
		return;
	}

	if (pending & CAUSEF_IP4) {			/* int 20 */
		do_IRQ(COBALT_ETH1_IRQ, regs);
		return;
	}

	if (pending & CAUSEF_IP5) {			/* int 21 */
		do_IRQ(COBALT_SERIAL_IRQ, regs);
		return;
	}

	if (pending & CAUSEF_IP7) {			/* int 23 */
		do_IRQ(COBALT_QUBE_SLOT_IRQ, regs);
		return;
	}
}

void __init arch_init_irq(void)
{
	set_except_vector(0, cobalt_handle_int);

	init_i8259_irqs();				/*  0 ... 15 */
	mips_cpu_irq_init(16);				/* 16 ... 23 */

	/*
	 * Mask all cpu interrupts
	 *  (except IE4, we already masked those at VIA level)
	 */
	change_c0_status(ST0_IM, IE_IRQ4);
}
