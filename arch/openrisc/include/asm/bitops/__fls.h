/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * OpenRISC Linux
 *
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 */

#ifndef __ASM_OPENRISC___FLS_H
#define __ASM_OPENRISC___FLS_H


#ifdef CONFIG_OPENRISC_HAVE_INST_FL1

static inline __attribute_const__ unsigned long __fls(unsigned long x)
{
	int ret;

	__asm__ ("l.fl1 %0,%1"
		 : "=r" (ret)
		 : "r" (x));

	return ret-1;
}

#else
#include <asm-generic/bitops/__fls.h>
#endif

#endif /* __ASM_OPENRISC___FLS_H */
