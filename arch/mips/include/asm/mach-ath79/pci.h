/*
 *  Atheros AR71XX/AR724X PCI support
 *
 *  Copyright (C) 2011 René Bolldorf <xsecute@googlemail.com>
 *  Copyright (C) 2008-2011 Gabor Juhos <juhosg@openwrt.org>
 *  Copyright (C) 2008 Imre Kaloz <kaloz@openwrt.org>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#ifndef __ASM_MACH_ATH79_PCI_H
#define __ASM_MACH_ATH79_PCI_H

#if defined(CONFIG_PCI) && defined(CONFIG_SOC_AR71XX)
int ar71xx_pcibios_init(void);
#else
static inline int ar71xx_pcibios_init(void) { return 0; }
#endif

#if defined(CONFIG_PCI_AR724X)
int ar724x_pcibios_init(int irq);
#else
static inline int ar724x_pcibios_init(int irq) { return 0; }
#endif

#endif /* __ASM_MACH_ATH79_PCI_H */
