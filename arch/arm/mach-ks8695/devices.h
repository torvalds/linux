/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/arm/mach-ks8695/include/mach/devices.h
 *
 * Copyright (C) 2006 Andrew Victor
 */

#ifndef __ASM_ARCH_DEVICES_H
#define __ASM_ARCH_DEVICES_H

#include <linux/pci.h>

 /* Ethernet */
extern void __init ks8695_add_device_wan(void);
extern void __init ks8695_add_device_lan(void);
extern void __init ks8695_add_device_hpna(void);

 /* PCI */
#define KS8695_MODE_PCI		0
#define KS8695_MODE_MINIPCI	1
#define KS8695_MODE_CARDBUS	2

struct ks8695_pci_cfg {
	short mode;
	int (*map_irq)(const struct pci_dev *, u8, u8);
};
extern __init void ks8695_init_pci(struct ks8695_pci_cfg *);

#endif
