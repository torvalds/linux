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

#define INVALID_HWID		ULONG_MAX

#define MPIDR_UP_BITMASK	(0x1 << 30)
#define MPIDR_MT_BITMASK	(0x1 << 24)
#define MPIDR_HWID_BITMASK	0xff00ffffff

#define MPIDR_LEVEL_BITS_SHIFT	3
#define MPIDR_LEVEL_BITS	(1 << MPIDR_LEVEL_BITS_SHIFT)
#define MPIDR_LEVEL_MASK	((1 << MPIDR_LEVEL_BITS) - 1)

#define MPIDR_LEVEL_SHIFT(level) \
	(((1 << level) >> 1) << MPIDR_LEVEL_BITS_SHIFT)

#define MPIDR_AFFINITY_LEVEL(mpidr, level) \
	((mpidr >> MPIDR_LEVEL_SHIFT(level)) & MPIDR_LEVEL_MASK)

#define read_cpuid(reg) ({						\
	u64 __val;							\
	asm("mrs	%0, " #reg : "=r" (__val));			\
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
	return read_cpuid(MIDR_EL1);
}

static inline u64 __attribute_const__ read_cpuid_mpidr(void)
{
	return read_cpuid(MPIDR_EL1);
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
	return read_cpuid(CTR_EL0);
}

#endif /* __ASSEMBLY__ */

#endif
