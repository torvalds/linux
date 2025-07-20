/* SPDX-License-Identifier: GPL-2.0-only */
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
 * 2009 (c) Deep Root Systems, LLC.
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
#define	DAVINCI_CPU_ID_DA850		0x08500000

#endif
