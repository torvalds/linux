/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2008 by Ralf Baechle (ralf@linux-mips.org)
 */
#ifndef __ASM_R4K_TIMER_H
#define __ASM_R4K_TIMER_H

#include <linux/compiler.h>

#ifdef CONFIG_SYNC_R4K

extern void synchronise_count_slave(int cpu);

#else

static inline void synchronise_count_slave(int cpu)
{
}

#endif

#endif /* __ASM_R4K_TIMER_H */
