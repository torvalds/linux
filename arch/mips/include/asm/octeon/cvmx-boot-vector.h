/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003-2017 Cavium, Inc.
 */

#ifndef __CVMX_BOOT_VECTOR_H__
#define __CVMX_BOOT_VECTOR_H__

#include <asm/octeon/octeon.h>

/*
 * The boot vector table is made up of an array of 1024 elements of
 * struct cvmx_boot_vector_element.  There is one entry for each
 * possible MIPS CPUNum, indexed by the CPUNum.
 *
 * Once cvmx_boot_vector_get() returns a non-NULL value (indicating
 * success), NMI to a core will cause execution to transfer to the
 * target_ptr location for that core's entry in the vector table.
 *
 * The struct cvmx_boot_vector_element fields app0, app1, and app2 can
 * be used by the application that has set the target_ptr in any
 * application specific manner, they are not touched by the vectoring
 * code.
 *
 * The boot vector code clobbers the CP0_DESAVE register, and on
 * OCTEON II and later CPUs also clobbers CP0_KScratch2.  All GP
 * registers are preserved, except on pre-OCTEON II CPUs, where k1 is
 * clobbered.
 *
 */


/*
 * Applications install the boot bus code in cvmx-boot-vector.c, which
 * uses this magic:
 */
#define OCTEON_BOOT_MOVEABLE_MAGIC1 0xdb00110ad358eacdull

struct cvmx_boot_vector_element {
	/* kseg0 or xkphys address of target code. */
	uint64_t target_ptr;
	/* Three application specific arguments. */
	uint64_t app0;
	uint64_t app1;
	uint64_t app2;
};

struct cvmx_boot_vector_element *cvmx_boot_vector_get(void);

#endif /* __CVMX_BOOT_VECTOR_H__ */
