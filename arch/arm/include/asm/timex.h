/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  arch/arm/include/asm/timex.h
 *
 *  Copyright (C) 1997,1998 Russell King
 *
 *  Architecture Specific TIME specifications
 */
#ifndef _ASMARM_TIMEX_H
#define _ASMARM_TIMEX_H

typedef unsigned long cycles_t;
// Temporary workaround until timex.h is cleaned up
bool delay_read_timer(unsigned long *t);

#define get_cycles()	({ cycles_t c; delay_read_timer(&c) ? c : 0; })
#define random_get_entropy() (((unsigned long)get_cycles()) ?: random_get_entropy_fallback())

#endif
