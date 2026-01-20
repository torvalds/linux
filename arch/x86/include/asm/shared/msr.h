/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_SHARED_MSR_H
#define _ASM_X86_SHARED_MSR_H

struct msr {
	union {
		struct {
			u32 l;
			u32 h;
		};
		u64 q;
	};
};

/*
 * The kernel proper already defines rdmsr()/wrmsr(), but they are not for the
 * boot kernel since they rely on tracepoint/exception handling infrastructure
 * that's not available here.
 */
static inline void raw_rdmsr(unsigned int reg, struct msr *m)
{
	asm volatile("rdmsr" : "=a" (m->l), "=d" (m->h) : "c" (reg));
}

static inline void raw_wrmsr(unsigned int reg, const struct msr *m)
{
	asm volatile("wrmsr" : : "c" (reg), "a"(m->l), "d" (m->h) : "memory");
}

#endif /* _ASM_X86_SHARED_MSR_H */
