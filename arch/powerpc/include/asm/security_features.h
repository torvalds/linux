/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Security related feature bit definitions.
 *
 * Copyright 2018, Michael Ellerman, IBM Corporation.
 */

#ifndef _ASM_POWERPC_SECURITY_FEATURES_H
#define _ASM_POWERPC_SECURITY_FEATURES_H


extern u64 powerpc_security_features;
extern bool rfi_flush;

/* These are bit flags */
enum stf_barrier_type {
	STF_BARRIER_NONE	= 0x1,
	STF_BARRIER_FALLBACK	= 0x2,
	STF_BARRIER_EIEIO	= 0x4,
	STF_BARRIER_SYNC_ORI	= 0x8,
};

void setup_stf_barrier(void);
void do_stf_barrier_fixups(enum stf_barrier_type types);
void setup_count_cache_flush(void);

static inline void security_ftr_set(u64 feature)
{
	powerpc_security_features |= feature;
}

static inline void security_ftr_clear(u64 feature)
{
	powerpc_security_features &= ~feature;
}

static inline bool security_ftr_enabled(u64 feature)
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

// bcctr 2,0,0 triggers a hardware assisted count cache flush
#define SEC_FTR_BCCTR_FLUSH_ASSIST	0x0000000000000800ull

// bcctr 2,0,0 triggers a hardware assisted link stack flush
#define SEC_FTR_BCCTR_LINK_FLUSH_ASSIST	0x0000000000002000ull

// Features indicating need for Spectre/Meltdown mitigations

// The L1-D cache should be flushed on MSR[HV] 1->0 transition (hypervisor to guest)
#define SEC_FTR_L1D_FLUSH_HV		0x0000000000000040ull

// The L1-D cache should be flushed on MSR[PR] 0->1 transition (kernel to userspace)
#define SEC_FTR_L1D_FLUSH_PR		0x0000000000000080ull

// A speculation barrier should be used for bounds checks (Spectre variant 1)
#define SEC_FTR_BNDS_CHK_SPEC_BAR	0x0000000000000100ull

// Firmware configuration indicates user favours security over performance
#define SEC_FTR_FAVOUR_SECURITY		0x0000000000000200ull

// Software required to flush count cache on context switch
#define SEC_FTR_FLUSH_COUNT_CACHE	0x0000000000000400ull

// Software required to flush link stack on context switch
#define SEC_FTR_FLUSH_LINK_STACK	0x0000000000001000ull

// The L1-D cache should be flushed when entering the kernel
#define SEC_FTR_L1D_FLUSH_ENTRY		0x0000000000004000ull

// The L1-D cache should be flushed after user accesses from the kernel
#define SEC_FTR_L1D_FLUSH_UACCESS	0x0000000000008000ull

// The STF flush should be executed on privilege state switch
#define SEC_FTR_STF_BARRIER		0x0000000000010000ull

// Features enabled by default
#define SEC_FTR_DEFAULT \
	(SEC_FTR_L1D_FLUSH_HV | \
	 SEC_FTR_L1D_FLUSH_PR | \
	 SEC_FTR_BNDS_CHK_SPEC_BAR | \
	 SEC_FTR_L1D_FLUSH_ENTRY | \
	 SEC_FTR_L1D_FLUSH_UACCESS | \
	 SEC_FTR_STF_BARRIER | \
	 SEC_FTR_FAVOUR_SECURITY)

#endif /* _ASM_POWERPC_SECURITY_FEATURES_H */
