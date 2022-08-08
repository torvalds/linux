/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_SPARSEMEM_H
#define __ASM_SPARSEMEM_H

#ifdef CONFIG_SPARSEMEM
#define MAX_PHYSMEM_BITS	CONFIG_ARM64_PA_BITS

/*
 * Section size must be at least 512MB for 64K base
 * page size config. Otherwise it will be less than
 * (MAX_ORDER - 1) and the build process will fail.
 */
#ifdef CONFIG_ARM64_64K_PAGES
#define SECTION_SIZE_BITS 29

#else

/*
 * Section size must be at least 128MB for 4K base
 * page size config. Otherwise PMD based huge page
 * entries could not be created for vmemmap mappings.
 * 16K follows 4K for simplicity.
 */
#define SECTION_SIZE_BITS 27
#endif /* CONFIG_ARM64_64K_PAGES */

#endif /* CONFIG_SPARSEMEM*/

#endif
