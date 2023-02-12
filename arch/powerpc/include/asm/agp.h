/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_AGP_H
#define _ASM_POWERPC_AGP_H
#ifdef __KERNEL__

#include <asm/io.h>

#define map_page_into_agp(page) do {} while (0)
#define unmap_page_from_agp(page) do {} while (0)
#define flush_agp_cache() mb()

#endif /* __KERNEL__ */
#endif	/* _ASM_POWERPC_AGP_H */
