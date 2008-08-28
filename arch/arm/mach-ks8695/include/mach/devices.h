/*
 * arch/arm/mach-ks8695/include/mach/devices.h
 *
 * Copyright (C) 2006 Andrew Victor
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_DEVICES_H
#define __ASM_ARCH_DEVICES_H

#include <linux/pci.h>

 /* Ethernet */
extern void __init ks8695_add_device_wan(void);
extern void __init ks8695_add_device_lan(void);
extern void __init ks8695_add_device_hpna(void);

 /* LEDs */
extern short ks8695_leds_cpu;
extern short ks8695_leds_timer;
extern void __init ks8695_init_leds(u8 cpu_led, u8 timer_led);

 /* PCI */
#define KS8695_MODE_PCI		0
#define KS8695_MODE_MINIPCI	1
#define KS8695_MODE_CARDBUS	2

struct ks8695_pci_cfg {
	short mode;
	int (*map_irq)(struct pci_dev *, u8, u8);
};
extern __init void ks8695_init_pci(struct ks8695_pci_cfg *);

#endif
