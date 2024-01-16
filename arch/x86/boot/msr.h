/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Helpers/definitions related to MSR access.
 */

#ifndef BOOT_MSR_H
#define BOOT_MSR_H

#include <asm/shared/msr.h>

/*
 * The kernel proper already defines rdmsr()/wrmsr(), but they are not for the
 * boot kernel since they rely on tracepoint/exception handling infrastructure
 * that's not available here.
 */
static inline void boot_rdmsr(unsigned int reg, struct msr *m)
{
	asm volatile("rdmsr" : "=a" (m->l), "=d" (m->h) : "c" (reg));
}

static inline void boot_wrmsr(unsigned int reg, const struct msr *m)
{
	asm volatile("wrmsr" : : "c" (reg), "a"(m->l), "d" (m->h) : "memory");
}

#endif /* BOOT_MSR_H */
