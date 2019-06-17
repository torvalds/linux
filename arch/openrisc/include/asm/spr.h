/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * OpenRISC Linux
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * OpenRISC implementation:
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 */

#ifndef __ASM_OPENRISC_SPR_H
#define __ASM_OPENRISC_SPR_H

#define mtspr(_spr, _val) __asm__ __volatile__ (		\
	"l.mtspr r0,%1,%0"					\
	: : "K" (_spr), "r" (_val))
#define mtspr_off(_spr, _off, _val) __asm__ __volatile__ (	\
	"l.mtspr %0,%1,%2"					\
	: : "r" (_off), "r" (_val), "K" (_spr))

static inline unsigned long mfspr(unsigned long add)
{
	unsigned long ret;
	__asm__ __volatile__ ("l.mfspr %0,r0,%1" : "=r" (ret) : "K" (add));
	return ret;
}

static inline unsigned long mfspr_off(unsigned long add, unsigned long offset)
{
	unsigned long ret;
	__asm__ __volatile__ ("l.mfspr %0,%1,%2" : "=r" (ret)
						 : "r" (offset), "K" (add));
	return ret;
}

#endif
