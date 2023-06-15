/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2012 ARM Ltd.
 */

#ifndef __ASM_PMUV3_H
#define __ASM_PMUV3_H

#include <linux/kvm_host.h>

#include <asm/cpufeature.h>
#include <asm/sysreg.h>

#define RETURN_READ_PMEVCNTRN(n) \
	return read_sysreg(pmevcntr##n##_el0)
static unsigned long read_pmevcntrn(int n)
{
	PMEVN_SWITCH(n, RETURN_READ_PMEVCNTRN);
	return 0;
}

#define WRITE_PMEVCNTRN(n) \
	write_sysreg(val, pmevcntr##n##_el0)
static void write_pmevcntrn(int n, unsigned long val)
{
	PMEVN_SWITCH(n, WRITE_PMEVCNTRN);
}

#define WRITE_PMEVTYPERN(n) \
	write_sysreg(val, pmevtyper##n##_el0)
static void write_pmevtypern(int n, unsigned long val)
{
	PMEVN_SWITCH(n, WRITE_PMEVTYPERN);
}

static inline unsigned long read_pmmir(void)
{
	return read_cpuid(PMMIR_EL1);
}

static inline u32 read_pmuver(void)
{
	u64 dfr0 = read_sysreg(id_aa64dfr0_el1);

	return cpuid_feature_extract_unsigned_field(dfr0,
			ID_AA64DFR0_EL1_PMUVer_SHIFT);
}

static inline void write_pmcr(u32 val)
{
	write_sysreg(val, pmcr_el0);
}

static inline u32 read_pmcr(void)
{
	return read_sysreg(pmcr_el0);
}

static inline void write_pmselr(u32 val)
{
	write_sysreg(val, pmselr_el0);
}

static inline void write_pmccntr(u64 val)
{
	write_sysreg(val, pmccntr_el0);
}

static inline u64 read_pmccntr(void)
{
	return read_sysreg(pmccntr_el0);
}

static inline void write_pmxevcntr(u32 val)
{
	write_sysreg(val, pmxevcntr_el0);
}

static inline u32 read_pmxevcntr(void)
{
	return read_sysreg(pmxevcntr_el0);
}

static inline void write_pmxevtyper(u32 val)
{
	write_sysreg(val, pmxevtyper_el0);
}

static inline void write_pmcntenset(u32 val)
{
	write_sysreg(val, pmcntenset_el0);
}

static inline void write_pmcntenclr(u32 val)
{
	write_sysreg(val, pmcntenclr_el0);
}

static inline void write_pmintenset(u32 val)
{
	write_sysreg(val, pmintenset_el1);
}

static inline void write_pmintenclr(u32 val)
{
	write_sysreg(val, pmintenclr_el1);
}

static inline void write_pmccfiltr(u32 val)
{
	write_sysreg(val, pmccfiltr_el0);
}

static inline void write_pmovsclr(u32 val)
{
	write_sysreg(val, pmovsclr_el0);
}

static inline u32 read_pmovsclr(void)
{
	return read_sysreg(pmovsclr_el0);
}

static inline void write_pmuserenr(u32 val)
{
	write_sysreg(val, pmuserenr_el0);
}

static inline u32 read_pmceid0(void)
{
	return read_sysreg(pmceid0_el0);
}

static inline u32 read_pmceid1(void)
{
	return read_sysreg(pmceid1_el0);
}

static inline bool pmuv3_implemented(int pmuver)
{
	return !(pmuver == ID_AA64DFR0_EL1_PMUVer_IMP_DEF ||
		 pmuver == ID_AA64DFR0_EL1_PMUVer_NI);
}

static inline bool is_pmuv3p4(int pmuver)
{
	return pmuver >= ID_AA64DFR0_EL1_PMUVer_V3P4;
}

static inline bool is_pmuv3p5(int pmuver)
{
	return pmuver >= ID_AA64DFR0_EL1_PMUVer_V3P5;
}

#endif
