/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2012 ARM Ltd.
 */

#ifndef __ASM_PMUV3_H
#define __ASM_PMUV3_H

#include <asm/cp15.h>
#include <asm/cputype.h>

#define PMCCNTR			__ACCESS_CP15_64(0, c9)

#define PMCR			__ACCESS_CP15(c9,  0, c12, 0)
#define PMCNTENSET		__ACCESS_CP15(c9,  0, c12, 1)
#define PMCNTENCLR		__ACCESS_CP15(c9,  0, c12, 2)
#define PMOVSR			__ACCESS_CP15(c9,  0, c12, 3)
#define PMSELR			__ACCESS_CP15(c9,  0, c12, 5)
#define PMCEID0			__ACCESS_CP15(c9,  0, c12, 6)
#define PMCEID1			__ACCESS_CP15(c9,  0, c12, 7)
#define PMXEVTYPER		__ACCESS_CP15(c9,  0, c13, 1)
#define PMXEVCNTR		__ACCESS_CP15(c9,  0, c13, 2)
#define PMUSERENR		__ACCESS_CP15(c9,  0, c14, 0)
#define PMINTENSET		__ACCESS_CP15(c9,  0, c14, 1)
#define PMINTENCLR		__ACCESS_CP15(c9,  0, c14, 2)
#define PMMIR			__ACCESS_CP15(c9,  0, c14, 6)
#define PMCCFILTR		__ACCESS_CP15(c14, 0, c15, 7)

#define PMEVCNTR0		__ACCESS_CP15(c14, 0, c8, 0)
#define PMEVCNTR1		__ACCESS_CP15(c14, 0, c8, 1)
#define PMEVCNTR2		__ACCESS_CP15(c14, 0, c8, 2)
#define PMEVCNTR3		__ACCESS_CP15(c14, 0, c8, 3)
#define PMEVCNTR4		__ACCESS_CP15(c14, 0, c8, 4)
#define PMEVCNTR5		__ACCESS_CP15(c14, 0, c8, 5)
#define PMEVCNTR6		__ACCESS_CP15(c14, 0, c8, 6)
#define PMEVCNTR7		__ACCESS_CP15(c14, 0, c8, 7)
#define PMEVCNTR8		__ACCESS_CP15(c14, 0, c9, 0)
#define PMEVCNTR9		__ACCESS_CP15(c14, 0, c9, 1)
#define PMEVCNTR10		__ACCESS_CP15(c14, 0, c9, 2)
#define PMEVCNTR11		__ACCESS_CP15(c14, 0, c9, 3)
#define PMEVCNTR12		__ACCESS_CP15(c14, 0, c9, 4)
#define PMEVCNTR13		__ACCESS_CP15(c14, 0, c9, 5)
#define PMEVCNTR14		__ACCESS_CP15(c14, 0, c9, 6)
#define PMEVCNTR15		__ACCESS_CP15(c14, 0, c9, 7)
#define PMEVCNTR16		__ACCESS_CP15(c14, 0, c10, 0)
#define PMEVCNTR17		__ACCESS_CP15(c14, 0, c10, 1)
#define PMEVCNTR18		__ACCESS_CP15(c14, 0, c10, 2)
#define PMEVCNTR19		__ACCESS_CP15(c14, 0, c10, 3)
#define PMEVCNTR20		__ACCESS_CP15(c14, 0, c10, 4)
#define PMEVCNTR21		__ACCESS_CP15(c14, 0, c10, 5)
#define PMEVCNTR22		__ACCESS_CP15(c14, 0, c10, 6)
#define PMEVCNTR23		__ACCESS_CP15(c14, 0, c10, 7)
#define PMEVCNTR24		__ACCESS_CP15(c14, 0, c11, 0)
#define PMEVCNTR25		__ACCESS_CP15(c14, 0, c11, 1)
#define PMEVCNTR26		__ACCESS_CP15(c14, 0, c11, 2)
#define PMEVCNTR27		__ACCESS_CP15(c14, 0, c11, 3)
#define PMEVCNTR28		__ACCESS_CP15(c14, 0, c11, 4)
#define PMEVCNTR29		__ACCESS_CP15(c14, 0, c11, 5)
#define PMEVCNTR30		__ACCESS_CP15(c14, 0, c11, 6)

#define PMEVTYPER0		__ACCESS_CP15(c14, 0, c12, 0)
#define PMEVTYPER1		__ACCESS_CP15(c14, 0, c12, 1)
#define PMEVTYPER2		__ACCESS_CP15(c14, 0, c12, 2)
#define PMEVTYPER3		__ACCESS_CP15(c14, 0, c12, 3)
#define PMEVTYPER4		__ACCESS_CP15(c14, 0, c12, 4)
#define PMEVTYPER5		__ACCESS_CP15(c14, 0, c12, 5)
#define PMEVTYPER6		__ACCESS_CP15(c14, 0, c12, 6)
#define PMEVTYPER7		__ACCESS_CP15(c14, 0, c12, 7)
#define PMEVTYPER8		__ACCESS_CP15(c14, 0, c13, 0)
#define PMEVTYPER9		__ACCESS_CP15(c14, 0, c13, 1)
#define PMEVTYPER10		__ACCESS_CP15(c14, 0, c13, 2)
#define PMEVTYPER11		__ACCESS_CP15(c14, 0, c13, 3)
#define PMEVTYPER12		__ACCESS_CP15(c14, 0, c13, 4)
#define PMEVTYPER13		__ACCESS_CP15(c14, 0, c13, 5)
#define PMEVTYPER14		__ACCESS_CP15(c14, 0, c13, 6)
#define PMEVTYPER15		__ACCESS_CP15(c14, 0, c13, 7)
#define PMEVTYPER16		__ACCESS_CP15(c14, 0, c14, 0)
#define PMEVTYPER17		__ACCESS_CP15(c14, 0, c14, 1)
#define PMEVTYPER18		__ACCESS_CP15(c14, 0, c14, 2)
#define PMEVTYPER19		__ACCESS_CP15(c14, 0, c14, 3)
#define PMEVTYPER20		__ACCESS_CP15(c14, 0, c14, 4)
#define PMEVTYPER21		__ACCESS_CP15(c14, 0, c14, 5)
#define PMEVTYPER22		__ACCESS_CP15(c14, 0, c14, 6)
#define PMEVTYPER23		__ACCESS_CP15(c14, 0, c14, 7)
#define PMEVTYPER24		__ACCESS_CP15(c14, 0, c15, 0)
#define PMEVTYPER25		__ACCESS_CP15(c14, 0, c15, 1)
#define PMEVTYPER26		__ACCESS_CP15(c14, 0, c15, 2)
#define PMEVTYPER27		__ACCESS_CP15(c14, 0, c15, 3)
#define PMEVTYPER28		__ACCESS_CP15(c14, 0, c15, 4)
#define PMEVTYPER29		__ACCESS_CP15(c14, 0, c15, 5)
#define PMEVTYPER30		__ACCESS_CP15(c14, 0, c15, 6)

#define RETURN_READ_PMEVCNTRN(n) \
	return read_sysreg(PMEVCNTR##n)
static unsigned long read_pmevcntrn(int n)
{
	PMEVN_SWITCH(n, RETURN_READ_PMEVCNTRN);
	return 0;
}

#define WRITE_PMEVCNTRN(n) \
	write_sysreg(val, PMEVCNTR##n)
static void write_pmevcntrn(int n, unsigned long val)
{
	PMEVN_SWITCH(n, WRITE_PMEVCNTRN);
}

#define WRITE_PMEVTYPERN(n) \
	write_sysreg(val, PMEVTYPER##n)
static void write_pmevtypern(int n, unsigned long val)
{
	PMEVN_SWITCH(n, WRITE_PMEVTYPERN);
}

static inline unsigned long read_pmmir(void)
{
	return read_sysreg(PMMIR);
}

static inline u32 read_pmuver(void)
{
	/* PMUVers is not a signed field */
	u32 dfr0 = read_cpuid_ext(CPUID_EXT_DFR0);

	return (dfr0 >> 24) & 0xf;
}

static inline void write_pmcr(u32 val)
{
	write_sysreg(val, PMCR);
}

static inline u32 read_pmcr(void)
{
	return read_sysreg(PMCR);
}

static inline void write_pmselr(u32 val)
{
	write_sysreg(val, PMSELR);
}

static inline void write_pmccntr(u64 val)
{
	write_sysreg(val, PMCCNTR);
}

static inline u64 read_pmccntr(void)
{
	return read_sysreg(PMCCNTR);
}

static inline void write_pmxevcntr(u32 val)
{
	write_sysreg(val, PMXEVCNTR);
}

static inline u32 read_pmxevcntr(void)
{
	return read_sysreg(PMXEVCNTR);
}

static inline void write_pmxevtyper(u32 val)
{
	write_sysreg(val, PMXEVTYPER);
}

static inline void write_pmcntenset(u32 val)
{
	write_sysreg(val, PMCNTENSET);
}

static inline void write_pmcntenclr(u32 val)
{
	write_sysreg(val, PMCNTENCLR);
}

static inline void write_pmintenset(u32 val)
{
	write_sysreg(val, PMINTENSET);
}

static inline void write_pmintenclr(u32 val)
{
	write_sysreg(val, PMINTENCLR);
}

static inline void write_pmccfiltr(u32 val)
{
	write_sysreg(val, PMCCFILTR);
}

static inline void write_pmovsclr(u32 val)
{
	write_sysreg(val, PMOVSR);
}

static inline u32 read_pmovsclr(void)
{
	return read_sysreg(PMOVSR);
}

static inline void write_pmuserenr(u32 val)
{
	write_sysreg(val, PMUSERENR);
}

static inline u32 read_pmceid0(void)
{
	return read_sysreg(PMCEID0);
}

static inline u32 read_pmceid1(void)
{
	return read_sysreg(PMCEID1);
}

static inline void kvm_set_pmu_events(u32 set, struct perf_event_attr *attr) {}
static inline void kvm_clr_pmu_events(u32 clr) {}
static inline bool kvm_pmu_counter_deferred(struct perf_event_attr *attr)
{
	return false;
}

/* PMU Version in DFR Register */
#define ARMV8_PMU_DFR_VER_NI        0
#define ARMV8_PMU_DFR_VER_V3P4      0x5
#define ARMV8_PMU_DFR_VER_V3P5      0x6
#define ARMV8_PMU_DFR_VER_IMP_DEF   0xF

static inline bool pmuv3_implemented(int pmuver)
{
	return !(pmuver == ARMV8_PMU_DFR_VER_IMP_DEF ||
		 pmuver == ARMV8_PMU_DFR_VER_NI);
}

static inline bool is_pmuv3p4(int pmuver)
{
	return pmuver >= ARMV8_PMU_DFR_VER_V3P4;
}

static inline bool is_pmuv3p5(int pmuver)
{
	return pmuver >= ARMV8_PMU_DFR_VER_V3P5;
}

#endif
