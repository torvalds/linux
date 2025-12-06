/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_CRASH_RESERVE_H
#define _ASM_POWERPC_CRASH_RESERVE_H

/* crash kernel regions are Page size agliged */
#define CRASH_ALIGN             PAGE_SIZE

#ifdef CONFIG_ARCH_HAS_GENERIC_CRASHKERNEL_RESERVATION
static inline bool arch_add_crash_res_to_iomem(void)
{
	return false;
}
#define arch_add_crash_res_to_iomem arch_add_crash_res_to_iomem
#endif

#endif /* _ASM_POWERPC_CRASH_RESERVE_H */
