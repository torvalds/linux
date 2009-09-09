/*
 * Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_MXC_MEMORY_H__
#define __ASM_ARCH_MXC_MEMORY_H__

#if defined CONFIG_ARCH_MX1
#define PHYS_OFFSET		UL(0x08000000)
#elif defined CONFIG_ARCH_MX2
#ifdef CONFIG_MACH_MX21
#define PHYS_OFFSET		UL(0xC0000000)
#endif
#ifdef CONFIG_MACH_MX27
#define PHYS_OFFSET		UL(0xA0000000)
#endif
#elif defined CONFIG_ARCH_MX3
#define PHYS_OFFSET		UL(0x80000000)
#endif

#if defined(CONFIG_MX1_VIDEO)
/*
 * Increase size of DMA-consistent memory region.
 * This is required for i.MX camera driver to capture at least four VGA frames.
 */
#define CONSISTENT_DMA_SIZE SZ_4M
#endif /* CONFIG_MX1_VIDEO */

#if defined(CONFIG_MX3_VIDEO)
/*
 * Increase size of DMA-consistent memory region.
 * This is required for mx3 camera driver to capture at least two QXGA frames.
 */
#define CONSISTENT_DMA_SIZE SZ_8M
#endif /* CONFIG_MX3_VIDEO */

#endif /* __ASM_ARCH_MXC_MEMORY_H__ */
