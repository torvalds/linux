/*
 * Corenet based SoC DS Setup
 *
 * Copyright 2009 Freescale Semiconductor Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef CORENET_DS_H
#define CORENET_DS_H

extern void __init corenet_ds_pic_init(void);
extern void __init corenet_ds_setup_arch(void);
extern int __init corenet_ds_publish_devices(void);

#endif
