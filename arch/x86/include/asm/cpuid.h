/* SPDX-License-Identifier: GPL-2.0 */
/*
 * CPUID-related helpers/definitions
 *
 * Derived from arch/x86/kvm/cpuid.c
 */

#ifndef _ASM_X86_CPUID_H
#define _ASM_X86_CPUID_H

static __always_inline bool cpuid_function_is_indexed(u32 function)
{
	switch (function) {
	case 4:
	case 7:
	case 0xb:
	case 0xd:
	case 0xf:
	case 0x10:
	case 0x12:
	case 0x14:
	case 0x17:
	case 0x18:
	case 0x1d:
	case 0x1e:
	case 0x1f:
	case 0x8000001d:
		return true;
	}

	return false;
}

#endif /* _ASM_X86_CPUID_H */
