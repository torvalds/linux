/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/arm/include/asm/mach/time.h
 *
 * Copyright (C) 2004 MontaVista Software, Inc.
 */
#ifndef __ASM_ARM_MACH_TIME_H
#define __ASM_ARM_MACH_TIME_H

typedef void (*clock_access_fn)(struct timespec64 *);
extern int register_persistent_clock(clock_access_fn read_persistent);

#endif
