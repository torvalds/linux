/*
 * Copyright (C) 2009 ST-Ericsson.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * These symbols are needed for board-specific files to call their
 * own cpu-specific files
 */
#ifndef __ASM_ARCH_SETUP_H
#define __ASM_ARCH_SETUP_H

#include <asm/mach/time.h>
#include <linux/init.h>

extern void u8500_map_io(void);
extern void u8500_init_devices(void);
extern void u8500_init_irq(void);
/* We re-use nomadik_timer for this platform */
extern void nmdk_timer_init(void);

#endif /*  __ASM_ARCH_SETUP_H */
