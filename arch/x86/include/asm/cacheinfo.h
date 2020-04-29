/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_CACHEINFO_H
#define _ASM_X86_CACHEINFO_H

void cacheinfo_amd_init_llc_id(struct cpuinfo_x86 *c, int cpu, u8 node_id);
void cacheinfo_hygon_init_llc_id(struct cpuinfo_x86 *c, int cpu, u8 node_id);

#endif /* _ASM_X86_CACHEINFO_H */
