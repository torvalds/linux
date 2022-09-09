/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * OpenRISC Linux
 *
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 */

#ifndef __ASM_OPENRISC_FFS_H
#define __ASM_OPENRISC_FFS_H

#ifdef CONFIG_OPENRISC_HAVE_INST_FF1

static inline int ffs(int x)
{
	int ret;

	__asm__ ("l.ff1 %0,%1"
		 : "=r" (ret)
		 : "r" (x));

	return ret;
}

#else
#include <asm-generic/bitops/ffs.h>
#endif

#endif /* __ASM_OPENRISC_FFS_H */
