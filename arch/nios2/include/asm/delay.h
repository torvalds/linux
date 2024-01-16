/*
 * Copyright (C) 2014 Altera Corporation
 * Copyright (C) 2004 Microtronix Datacom Ltd
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_NIOS2_DELAY_H
#define _ASM_NIOS2_DELAY_H

#include <asm-generic/delay.h>

/* Undefined functions to get compile-time errors */
extern void __bad_udelay(void);
extern void __bad_ndelay(void);

extern unsigned long loops_per_jiffy;

#endif /* _ASM_NIOS2_DELAY_H */
