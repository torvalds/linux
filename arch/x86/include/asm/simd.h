/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_SIMD_H
#define _ASM_SIMD_H

#include <asm/fpu/api.h>
#include <linux/compiler_attributes.h>
#include <linux/types.h>

/*
 * may_use_simd - whether it is allowable at this time to issue SIMD
 *                instructions or access the SIMD register file
 */
static __must_check inline bool may_use_simd(void)
{
	return irq_fpu_usable();
}

#endif	/* _ASM_SIMD_H */
