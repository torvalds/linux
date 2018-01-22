/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_H8300_PCI_H
#define _ASM_H8300_PCI_H

/*
 * asm-h8300/pci.h - H8/300 specific PCI declarations.
 *
 * Yoshinori Sato <ysato@users.sourceforge.jp>
 */

#define pcibios_assign_all_busses()	0

static inline void pcibios_penalize_isa_irq(int irq, int active)
{
	/* We don't do dynamic PCI IRQ allocation */
}

#define PCI_DMA_BUS_IS_PHYS	(1)

#endif /* _ASM_H8300_PCI_H */
