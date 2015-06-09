/* Copyright (C) 2010 - 2013 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

/* Linux GCC Version (32-bit and 64-bit) */
static inline unsigned long
__unisys_vmcall_gnuc(unsigned long tuple, unsigned long reg_ebx,
		     unsigned long reg_ecx)
{
	unsigned long result = 0;
	unsigned int cpuid_eax, cpuid_ebx, cpuid_ecx, cpuid_edx;

	cpuid(0x00000001, &cpuid_eax, &cpuid_ebx, &cpuid_ecx, &cpuid_edx);
	if (!(cpuid_ecx & 0x80000000))
		return -1;

	__asm__ __volatile__(".byte 0x00f, 0x001, 0x0c1" : "=a"(result) :
		"a"(tuple), "b"(reg_ebx), "c"(reg_ecx));
	return result;
}

static inline unsigned long
__unisys_extended_vmcall_gnuc(unsigned long long tuple,
			      unsigned long long reg_ebx,
			      unsigned long long reg_ecx,
			      unsigned long long reg_edx)
{
	unsigned long result = 0;
	unsigned int cpuid_eax, cpuid_ebx, cpuid_ecx, cpuid_edx;

	cpuid(0x00000001, &cpuid_eax, &cpuid_ebx, &cpuid_ecx, &cpuid_edx);
	if (!(cpuid_ecx & 0x80000000))
		return -1;

	__asm__ __volatile__(".byte 0x00f, 0x001, 0x0c1" : "=a"(result) :
		"a"(tuple), "b"(reg_ebx), "c"(reg_ecx), "d"(reg_edx));
	return result;
}
