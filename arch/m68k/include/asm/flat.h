/* SPDX-License-Identifier: GPL-2.0 */
/*
 * flat.h -- uClinux flat-format executables
 */

#ifndef __M68KNOMMU_FLAT_H__
#define __M68KNOMMU_FLAT_H__

#include <asm-generic/flat.h>

#define FLAT_PLAT_INIT(regs) \
	do { \
		if (current->mm) \
			(regs)->d5 = current->mm->start_data; \
	} while (0)

#endif /* __M68KNOMMU_FLAT_H__ */
