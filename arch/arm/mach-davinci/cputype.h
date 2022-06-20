/*
 * DaVinci CPU type detection
 *
 * Author: Kevin Hilman, Deep Root Systems, LLC
 *
 * Defines the cpu_is_*() macros for runtime detection of DaVinci
 * device type.  In addition, if support for a given device is not
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

#include "common.h"

struct davinci_id {
	u8	variant;	/* JTAG ID bits 31:28 */
	u16	part_no;	/* JTAG ID bits 27:12 */
	u16	manufacturer;	/* JTAG ID bits 11:1 */
	u32	cpu_id;
	char	*name;
};

/* Can use lower 16 bits of cpu id  for a variant when required */
#define	DAVINCI_CPU_ID_DM6446		0x64460000
#define	DAVINCI_CPU_ID_DM6467		0x64670000
#define	DAVINCI_CPU_ID_DM355		0x03550000
#define	DAVINCI_CPU_ID_DM365		0x03650000
#define	DAVINCI_CPU_ID_DA830		0x08300000
#define	DAVINCI_CPU_ID_DA850		0x08500000

#define IS_DAVINCI_CPU(type, id)					\
static inline int is_davinci_ ##type(void)				\
{									\
	return (davinci_soc_info.cpu_id == (id));			\
}

IS_DAVINCI_CPU(dm644x, DAVINCI_CPU_ID_DM6446)
IS_DAVINCI_CPU(dm646x, DAVINCI_CPU_ID_DM6467)
IS_DAVINCI_CPU(dm355, DAVINCI_CPU_ID_DM355)
IS_DAVINCI_CPU(dm365, DAVINCI_CPU_ID_DM365)
IS_DAVINCI_CPU(da830, DAVINCI_CPU_ID_DA830)
IS_DAVINCI_CPU(da850, DAVINCI_CPU_ID_DA850)

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

#ifdef CONFIG_ARCH_DAVINCI_DM365
#define cpu_is_davinci_dm365() is_davinci_dm365()
#else
#define cpu_is_davinci_dm365() 0
#endif

#ifdef CONFIG_ARCH_DAVINCI_DA830
#define cpu_is_davinci_da830() is_davinci_da830()
#else
#define cpu_is_davinci_da830() 0
#endif

#ifdef CONFIG_ARCH_DAVINCI_DA850
#define cpu_is_davinci_da850() is_davinci_da850()
#else
#define cpu_is_davinci_da850() 0
#endif

#endif
