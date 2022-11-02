/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_CACHEINFO_H
#define _ASM_X86_CACHEINFO_H

/* Kernel controls MTRR and/or PAT MSRs. */
extern unsigned int memory_caching_control;
#define CACHE_MTRR 0x01
#define CACHE_PAT  0x02

void cacheinfo_amd_init_llc_id(struct cpuinfo_x86 *c, int cpu);
void cacheinfo_hygon_init_llc_id(struct cpuinfo_x86 *c, int cpu);

void cache_disable(void);
void cache_enable(void);

#endif /* _ASM_X86_CACHEINFO_H */
