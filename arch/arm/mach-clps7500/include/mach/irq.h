/*
 * arch/arm/mach-clps7500/include/mach/irq.h
 *
 * Copyright (C) 1996 Russell King
 * Copyright (C) 1999, 2001 Nexus Electronics Ltd.
 *
 * Changelog:
 *   10-10-1996	RMK	Brought up to date with arch-sa110eval
 *   22-08-1998	RMK	Restructured IRQ routines
 *   11-08-1999	PJB	Created ARM7500 version, derived from RiscPC code
 */

#include <asm/hardware/iomd.h>
#include <asm/io.h>

static inline int fixup_irq(unsigned int irq)
{
	if (irq == IRQ_ISA) {
		int isabits = *((volatile unsigned int *)0xe002b700);
		if (isabits == 0) {
			printk("Spurious ISA IRQ!\n");
			return irq;
		}
		irq = IRQ_ISA_BASE;
		while (!(isabits & 1)) {
			irq++;
			isabits >>= 1;
		}
	}

	return irq;
}
