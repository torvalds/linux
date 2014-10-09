/*
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ASM_CPUTYPE_H
#define __ASM_CPUTYPE_H

#define ID_MIDR_EL1		"midr_el1"
#define ID_MPIDR_EL1		"mpidr_el1"
#define ID_CTR_EL0		"ctr_el0"

#define ID_AA64PFR0_EL1		"id_aa64pfr0_el1"
#define ID_AA64DFR0_EL1		"id_aa64dfr0_el1"
#define ID_AA64AFR0_EL1		"id_aa64afr0_el1"
#define ID_AA64ISAR0_EL1	"id_aa64isar0_el1"
#define ID_AA64MMFR0_EL1	"id_aa64mmfr0_el1"

#define INVALID_HWID		ULONG_MAX

#define MPIDR_HWID_BITMASK	0xff00ffffff

#define read_cpuid(reg) ({						\
	u64 __val;							\
	asm("mrs	%0, " reg : "=r" (__val));			\
	__val;								\
})

#define ARM_CPU_IMP_ARM		0x41
#define ARM_CPU_IMP_APM		0x50

#define ARM_CPU_PART_AEM_V8	0xD0F0
#define ARM_CPU_PART_FOUNDATION	0xD000
#define ARM_CPU_PART_CORTEX_A53	0xD030
#define ARM_CPU_PART_CORTEX_A57	0xD070

#define APM_CPU_PART_POTENZA	0x0000

#ifndef __ASSEMBLY__

/*
 * The CPU ID never changes at run time, so we might as well tell the
 * compiler that it's constant.  Use this function to read the CPU ID
 * rather than directly reading processor_id or read_cpuid() directly.
 */
static inline u32 __attribute_const__ read_cpuid_id(void)
{
	return read_cpuid(ID_MIDR_EL1);
}

static inline u64 __attribute_const__ read_cpuid_mpidr(void)
{
	return read_cpuid(ID_MPIDR_EL1);
}

static inline unsigned int __attribute_const__ read_cpuid_implementor(void)
{
	return (read_cpuid_id() & 0xFF000000) >> 24;
}

static inline unsigned int __attribute_const__ read_cpuid_part_number(void)
{
	return (read_cpuid_id() & 0xFFF0);
}

static inline u32 __attribute_const__ read_cpuid_cachetype(void)
{
	return read_cpuid(ID_CTR_EL0);
}

#endif /* __ASSEMBLY__ */

#endif
