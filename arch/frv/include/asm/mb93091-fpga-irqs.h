/* mb93091-fpga-irqs.h: MB93091 CPU board FPGA IRQs
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_MB93091_FPGA_IRQS_H
#define _ASM_MB93091_FPGA_IRQS_H

#include <asm/irq.h>

#ifndef __ASSEMBLY__

/* IRQ IDs presented to drivers */
enum {
	IRQ_FPGA__UNUSED			= IRQ_BASE_FPGA,
	IRQ_FPGA_SYSINT_BUS_EXPANSION_1,
	IRQ_FPGA_SL_BUS_EXPANSION_2,
	IRQ_FPGA_PCI_INTD,
	IRQ_FPGA_PCI_INTC,
	IRQ_FPGA_PCI_INTB,
	IRQ_FPGA_PCI_INTA,
	IRQ_FPGA_SL_BUS_EXPANSION_7,
	IRQ_FPGA_SYSINT_BUS_EXPANSION_8,
	IRQ_FPGA_SL_BUS_EXPANSION_9,
	IRQ_FPGA_MB86943_PCI_INTA,
	IRQ_FPGA_MB86943_SLBUS_SIDE,
	IRQ_FPGA_RTL8029_INTA,
	IRQ_FPGA_SYSINT_BUS_EXPANSION_13,
	IRQ_FPGA_SL_BUS_EXPANSION_14,
	IRQ_FPGA_NMI,
};


#endif /* !__ASSEMBLY__ */

#endif /* _ASM_MB93091_FPGA_IRQS_H */
