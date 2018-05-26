/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Security related feature bit definitions.
 *
 * Copyright 2018, Michael Ellerman, IBM Corporation.
 */

#ifndef _ASM_POWERPC_SECURITY_FEATURES_H
#define _ASM_POWERPC_SECURITY_FEATURES_H


extern unsigned long powerpc_security_features;
extern bool rfi_flush;

static inline void security_ftr_set(unsigned long feature)
{
	powerpc_security_features |= feature;
}

static inline void security_ftr_clear(unsigned long feature)
{
	powerpc_security_features &= ~feature;
}

static inline bool security_ftr_enabled(unsigned long feature)
{
	return !!(powerpc_security_features & feature);
}


// Features indicating support for Spectre/Meltdown mitigations

// The L1-D cache can be flushed with ori r30,r30,0
#define SEC_FTR_L1D_FLUSH_ORI30		0x0000000000000001ull

// The L1-D cache can be flushed with mtspr 882,r0 (aka SPRN_TRIG2)
#define SEC_FTR_L1D_FLUSH_TRIG2		0x0000000000000002ull

// ori r31,r31,0 acts as a speculation barrier
#define SEC_FTR_SPEC_BAR_ORI31		0x0000000000000004ull

// Speculation past bctr is disabled
#define SEC_FTR_BCCTRL_SERIALISED	0x0000000000000008ull

// Entries in L1-D are private to a SMT thread
#define SEC_FTR_L1D_THREAD_PRIV		0x0000000000000010ull

// Indirect branch prediction cache disabled
#define SEC_FTR_COUNT_CACHE_DISABLED	0x0000000000000020ull


// Features indicating need for Spectre/Meltdown mitigations

// The L1-D cache should be flushed on MSR[HV] 1->0 transition (hypervisor to guest)
#define SEC_FTR_L1D_FLUSH_HV		0x0000000000000040ull

// The L1-D cache should be flushed on MSR[PR] 0->1 transition (kernel to userspace)
#define SEC_FTR_L1D_FLUSH_PR		0x0000000000000080ull

// A speculation barrier should be used for bounds checks (Spectre variant 1)
#define SEC_FTR_BNDS_CHK_SPEC_BAR	0x0000000000000100ull

// Firmware configuration indicates user favours security over performance
#define SEC_FTR_FAVOUR_SECURITY		0x0000000000000200ull

#endif /* _ASM_POWERPC_SECURITY_FEATURES_H */
