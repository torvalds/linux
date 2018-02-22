/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_NOHASH_64_SLICE_H
#define _ASM_POWERPC_NOHASH_64_SLICE_H

#ifdef CONFIG_PPC_64K_PAGES
#define get_slice_psize(mm, addr)	MMU_PAGE_64K
#else /* CONFIG_PPC_64K_PAGES */
#define get_slice_psize(mm, addr)	MMU_PAGE_4K
#endif /* !CONFIG_PPC_64K_PAGES */
#define slice_set_user_psize(mm, psize)	do { BUG(); } while (0)

#endif /* _ASM_POWERPC_NOHASH_64_SLICE_H */
