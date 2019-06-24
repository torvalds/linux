/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * arch/arm/mach-ks8695/generic.h
 *
 * Copyright (C) 2006 Ben Dooks <ben@simtec.co.uk>
 * Copyright (C) 2006 Simtec Electronics
*/

extern __init void ks8695_map_io(void);
extern __init void ks8695_init_irq(void);
extern void ks8695_restart(enum reboot_mode, const char *);
extern void ks8695_timer_init(void);
