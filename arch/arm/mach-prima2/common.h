/*
 * This file contains common function prototypes to avoid externs in the c files.
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef __MACH_PRIMA2_COMMON_H__
#define __MACH_PRIMA2_COMMON_H__

#include <linux/init.h>
#include <asm/mach/time.h>

extern struct sys_timer sirfsoc_timer;

extern void __init sirfsoc_of_irq_init(void);
extern void __init sirfsoc_of_clk_init(void);
extern void sirfsoc_restart(char, const char *);

#ifndef CONFIG_DEBUG_LL
static inline void sirfsoc_map_lluart(void)  {}
#else
extern void __init sirfsoc_map_lluart(void);
#endif

#ifdef CONFIG_SUSPEND
extern int sirfsoc_pm_init(void);
#else
static inline int sirfsoc_pm_init(void) { return 0; }
#endif

#endif
