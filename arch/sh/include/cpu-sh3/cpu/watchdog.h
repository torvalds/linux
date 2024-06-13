/* SPDX-License-Identifier: GPL-2.0
 *
 * include/asm-sh/cpu-sh3/watchdog.h
 *
 * Copyright (C) 2002, 2003 Paul Mundt
 */
#ifndef __ASM_CPU_SH3_WATCHDOG_H
#define __ASM_CPU_SH3_WATCHDOG_H

/* Register definitions */
#define WTCNT		0xffffff84
#define WTCSR		0xffffff86

/* Bit definitions */
#define WTCSR_TME	0x80
#define WTCSR_WT	0x40
#define WTCSR_RSTS	0x20
#define WTCSR_WOVF	0x10
#define WTCSR_IOVF	0x08

#endif /* __ASM_CPU_SH3_WATCHDOG_H */

