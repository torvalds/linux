/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_SPARSEMEM_H
#define __ASM_SPARSEMEM_H

#ifdef CONFIG_SPARSEMEM
#define MAX_PHYSMEM_BITS	CONFIG_ARM64_PA_BITS

#if defined(CONFIG_ARM64_4K_PAGES) || defined(CONFIG_ARM64_16K_PAGES)
#define SECTION_SIZE_BITS 27
#else
#define SECTION_SIZE_BITS 29
#endif /* CONFIG_ARM64_4K_PAGES || CONFIG_ARM64_16K_PAGES */

#endif /* CONFIG_SPARSEMEM*/

#endif
