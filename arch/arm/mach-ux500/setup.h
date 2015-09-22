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

#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <linux/init.h>
#include <linux/mfd/abx500/ab8500.h>

void ux500_restart(enum reboot_mode mode, const char *cmd);

void __init ux500_setup_id(void);

extern void __init ux500_init_irq(void);

extern struct device *ux500_soc_device_init(const char *soc_id);

extern void ux500_timer_init(void);

extern void ux500_cpu_die(unsigned int cpu);

#endif /*  __ASM_ARCH_SETUP_H */
