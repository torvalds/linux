/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_CACHEINFO_H
#define _ASM_X86_CACHEINFO_H

/* Kernel controls MTRR and/or PAT MSRs. */
extern unsigned int memory_caching_control;
#define CACHE_MTRR 0x01
#define CACHE_PAT  0x02

void cache_disable(void);
void cache_enable(void);
void set_cache_aps_delayed_init(bool val);
bool get_cache_aps_delayed_init(void);
void cache_bp_init(void);
void cache_bp_restore(void);
void cache_aps_init(void);

#endif /* _ASM_X86_CACHEINFO_H */
