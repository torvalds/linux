/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2009 Chen Liqin <liqin.chen@sunplusct.com>
 * Copyright (C) 2016 Regents of the University of California
 */

#ifndef _ASM_RISCV_DELAY_H
#define _ASM_RISCV_DELAY_H

extern unsigned long riscv_timebase;

#define udelay udelay
extern void udelay(unsigned long usecs);

#define ndelay ndelay
extern void ndelay(unsigned long nsecs);

extern void __delay(unsigned long cycles);

#endif /* _ASM_RISCV_DELAY_H */
