/*
 *  Atheros 724x PCI support
 *
 *  Copyright (C) 2011 René Bolldorf <xsecute@googlemail.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#ifndef __ASM_MACH_ATH79_PCI_H
#define __ASM_MACH_ATH79_PCI_H

#if defined(CONFIG_PCI) && defined(CONFIG_SOC_AR724X)
int ar724x_pcibios_init(int irq);
#else
static inline int ar724x_pcibios_init(int irq) { return 0; }
#endif

#endif /* __ASM_MACH_ATH79_PCI_H */
