/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef _ASM_DELAY_H
#define _ASM_DELAY_H

#include <linux/param.h>

extern void __delay(unsigned long cycles);
extern void __ndelay(unsigned long ns);
extern void __udelay(unsigned long us);

#define ndelay(ns) __ndelay(ns)
#define udelay(us) __udelay(us)

/* make sure "usecs *= ..." in udelay do not overflow. */
#if HZ >= 1000
#define MAX_UDELAY_MS	1
#elif HZ <= 200
#define MAX_UDELAY_MS	5
#else
#define MAX_UDELAY_MS	(1000 / HZ)
#endif

#endif /* _ASM_DELAY_H */
