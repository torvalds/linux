/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/arm/mach-netx/generic.h
 *
 * Copyright (c) 2005 Sascha Hauer <s.hauer@pengutronix.de>, Pengutronix
 */

#include <linux/reboot.h>

extern void __init netx_map_io(void);
extern void __init netx_init_irq(void);
extern void netx_restart(enum reboot_mode, const char *);

extern void netx_timer_init(void);
