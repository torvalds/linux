/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#ifndef _ASM_TILE_DELAY_H
#define _ASM_TILE_DELAY_H

/* Undefined functions to get compile-time errors. */
extern void __bad_udelay(void);
extern void __bad_ndelay(void);

extern void __udelay(unsigned long usecs);
extern void __ndelay(unsigned long nsecs);
extern void __delay(unsigned long loops);

#define udelay(n) (__builtin_constant_p(n) ? \
	((n) > 20000 ? __bad_udelay() : __ndelay((n) * 1000)) : \
	__udelay(n))

#define ndelay(n) (__builtin_constant_p(n) ? \
	((n) > 20000 ? __bad_ndelay() : __ndelay(n)) : \
	__ndelay(n))

#endif /* _ASM_TILE_DELAY_H */
