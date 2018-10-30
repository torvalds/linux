/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SH_CPU_SH5_MMU_CONTEXT_H
#define __ASM_SH_CPU_SH5_MMU_CONTEXT_H

/* Common defines */
#define TLB_STEP	0x00000010
#define TLB_PTEH	0x00000000
#define TLB_PTEL	0x00000008

/* PTEH defines */
#define PTEH_ASID_SHIFT	2
#define PTEH_VALID	0x0000000000000001
#define PTEH_SHARED	0x0000000000000002
#define PTEH_MATCH_ASID	0x00000000000003ff

#ifndef __ASSEMBLY__
/* This has to be a common function because the next location to fill
 * information is shared. */
extern void __do_tlb_refill(unsigned long address, unsigned long long is_text_not_data, pte_t *pte);
#endif /* __ASSEMBLY__ */

#endif /* __ASM_SH_CPU_SH5_MMU_CONTEXT_H */
