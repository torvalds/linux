/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright Altera Corporation (C) 2014. All rights reserved.
 */

#ifndef _ASM_NIOS2_TIMEX_H
#define _ASM_NIOS2_TIMEX_H

typedef unsigned long cycles_t;

extern cycles_t get_cycles(void);
#define get_cycles get_cycles

#define random_get_entropy() (((unsigned long)get_cycles()) ?: random_get_entropy_fallback())

#endif
