/*
 * Copyright (C) 2007 Freescale Semiconductor, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * Prototypes for MPC512x shared code
 */

#ifndef __MPC512X_H__
#define __MPC512X_H__
extern unsigned long mpc512x_find_ips_freq(struct device_node *node);
extern void __init mpc512x_init_IRQ(void);
void __init mpc512x_declare_of_platform_devices(void);
#endif				/* __MPC512X_H__ */
