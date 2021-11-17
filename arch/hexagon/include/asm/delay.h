/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#ifndef _ASM_DELAY_H
#define _ASM_DELAY_H

#include <asm/param.h>

extern void __delay(unsigned long cycles);
extern void __udelay(unsigned long usecs);

#define udelay(usecs) __udelay((usecs))

#endif /* _ASM_DELAY_H */
