/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Counter facility support definitions for the Linux perf
 *
 * Copyright IBM Corp. 2019
 * Author(s): Hendrik Brueckner <brueckner@linux.ibm.com>
 */
#ifndef _ASM_S390_CPU_MCF_H
#define _ASM_S390_CPU_MCF_H

#include <linux/perf_event.h>
#include <asm/cpu_mf.h>

enum cpumf_ctr_set {
	CPUMF_CTR_SET_BASIC   = 0,    /* Basic Counter Set */
	CPUMF_CTR_SET_USER    = 1,    /* Problem-State Counter Set */
	CPUMF_CTR_SET_CRYPTO  = 2,    /* Crypto-Activity Counter Set */
	CPUMF_CTR_SET_EXT     = 3,    /* Extended Counter Set */
	CPUMF_CTR_SET_MT_DIAG = 4,    /* MT-diagnostic Counter Set */

	/* Maximum number of counter sets */
	CPUMF_CTR_SET_MAX,
};

#define CPUMF_LCCTL_ENABLE_SHIFT    16
#define CPUMF_LCCTL_ACTCTL_SHIFT     0
static const u64 cpumf_ctr_ctl[CPUMF_CTR_SET_MAX] = {
	[CPUMF_CTR_SET_BASIC]	= 0x02,
	[CPUMF_CTR_SET_USER]	= 0x04,
	[CPUMF_CTR_SET_CRYPTO]	= 0x08,
	[CPUMF_CTR_SET_EXT]	= 0x01,
	[CPUMF_CTR_SET_MT_DIAG] = 0x20,
};

static inline void ctr_set_enable(u64 *state, int ctr_set)
{
	*state |= cpumf_ctr_ctl[ctr_set] << CPUMF_LCCTL_ENABLE_SHIFT;
}
static inline void ctr_set_disable(u64 *state, int ctr_set)
{
	*state &= ~(cpumf_ctr_ctl[ctr_set] << CPUMF_LCCTL_ENABLE_SHIFT);
}
static inline void ctr_set_start(u64 *state, int ctr_set)
{
	*state |= cpumf_ctr_ctl[ctr_set] << CPUMF_LCCTL_ACTCTL_SHIFT;
}
static inline void ctr_set_stop(u64 *state, int ctr_set)
{
	*state &= ~(cpumf_ctr_ctl[ctr_set] << CPUMF_LCCTL_ACTCTL_SHIFT);
}

static inline void ctr_set_multiple_enable(u64 *state, u64 ctrsets)
{
	*state |= ctrsets << CPUMF_LCCTL_ENABLE_SHIFT;
}

static inline void ctr_set_multiple_disable(u64 *state, u64 ctrsets)
{
	*state &= ~(ctrsets << CPUMF_LCCTL_ENABLE_SHIFT);
}

static inline void ctr_set_multiple_start(u64 *state, u64 ctrsets)
{
	*state |= ctrsets << CPUMF_LCCTL_ACTCTL_SHIFT;
}

static inline void ctr_set_multiple_stop(u64 *state, u64 ctrsets)
{
	*state &= ~(ctrsets << CPUMF_LCCTL_ACTCTL_SHIFT);
}

static inline int ctr_stcctm(enum cpumf_ctr_set set, u64 range, u64 *dest)
{
	switch (set) {
	case CPUMF_CTR_SET_BASIC:
		return stcctm(BASIC, range, dest);
	case CPUMF_CTR_SET_USER:
		return stcctm(PROBLEM_STATE, range, dest);
	case CPUMF_CTR_SET_CRYPTO:
		return stcctm(CRYPTO_ACTIVITY, range, dest);
	case CPUMF_CTR_SET_EXT:
		return stcctm(EXTENDED, range, dest);
	case CPUMF_CTR_SET_MT_DIAG:
		return stcctm(MT_DIAG_CLEARING, range, dest);
	case CPUMF_CTR_SET_MAX:
		return 3;
	}
	return 3;
}

struct cpu_cf_events {
	struct cpumf_ctr_info	info;
	atomic_t		ctr_set[CPUMF_CTR_SET_MAX];
	atomic64_t		alert;
	u64			state, tx_state;
	unsigned int		flags;
	unsigned int		txn_flags;
};
DECLARE_PER_CPU(struct cpu_cf_events, cpu_cf_events);

bool kernel_cpumcf_avail(void);
int __kernel_cpumcf_begin(void);
unsigned long kernel_cpumcf_alert(int clear);
void __kernel_cpumcf_end(void);

static inline int kernel_cpumcf_begin(void)
{
	if (!cpum_cf_avail())
		return -ENODEV;

	preempt_disable();
	return __kernel_cpumcf_begin();
}
static inline void kernel_cpumcf_end(void)
{
	__kernel_cpumcf_end();
	preempt_enable();
}

/* Return true if store counter set multiple instruction is available */
static inline int stccm_avail(void)
{
	return test_facility(142);
}

size_t cpum_cf_ctrset_size(enum cpumf_ctr_set ctrset,
			   struct cpumf_ctr_info *info);
#endif /* _ASM_S390_CPU_MCF_H */
