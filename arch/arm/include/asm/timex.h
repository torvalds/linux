/*
 *  arch/arm/include/asm/timex.h
 *
 *  Copyright (C) 1997,1998 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Architecture Specific TIME specifications
 */
#ifndef _ASMARM_TIMEX_H
#define _ASMARM_TIMEX_H

#include <asm/arch_timer.h>
#ifdef CONFIG_ARCH_MULTIPLATFORM
#define CLOCK_TICK_RATE 1000000
#else
#include <mach/timex.h>
#endif

typedef unsigned long cycles_t;

#ifdef ARCH_HAS_READ_CURRENT_TIMER
#define get_cycles()	({ cycles_t c; read_current_timer(&c) ? 0 : c; })
#else
#define get_cycles()	(0)
#endif

#endif
