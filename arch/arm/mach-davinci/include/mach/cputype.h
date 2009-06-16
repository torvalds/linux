/*
 * DaVinci CPU type detection
 *
 * Author: Kevin Hilman, Deep Root Systems, LLC
 *
 * Defines the cpu_is_*() macros for runtime detection of DaVinci
 * device type.  In addtion, if support for a given device is not
 * compiled in to the kernel, the macros return 0 so that
 * resulting code can be optimized out.
 *
 * 2009 (c) Deep Root Systems, LLC. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#ifndef _ASM_ARCH_CPU_H
#define _ASM_ARCH_CPU_H

extern unsigned int davinci_rev(void);

#define IS_DAVINCI_CPU(type, id)			\
static inline int is_davinci_dm ##type(void)	        \
{							\
	return (davinci_rev() == (id)) ? 1 : 0;	        \
}

IS_DAVINCI_CPU(644x, 0x6446)
IS_DAVINCI_CPU(646x, 0x6467)
IS_DAVINCI_CPU(355, 0x355)

#ifdef CONFIG_ARCH_DAVINCI_DM644x
#define cpu_is_davinci_dm644x() is_davinci_dm644x()
#else
#define cpu_is_davinci_dm644x() 0
#endif

#ifdef CONFIG_ARCH_DAVINCI_DM646x
#define cpu_is_davinci_dm646x() is_davinci_dm646x()
#else
#define cpu_is_davinci_dm646x() 0
#endif

#ifdef CONFIG_ARCH_DAVINCI_DM355
#define cpu_is_davinci_dm355() is_davinci_dm355()
#else
#define cpu_is_davinci_dm355() 0
#endif

#endif
