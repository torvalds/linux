/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_TIMEX_H
#define __ASM_TIMEX_H

#include <asm/arch_timer.h>

/*
 * Use the current timer as a cycle counter since this is what we use for
 * the delay loop.
 */
#define get_cycles()	arch_timer_read_counter()

#include <asm-generic/timex.h>

#endif
