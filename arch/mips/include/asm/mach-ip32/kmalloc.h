/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_MACH_IP32_KMALLOC_H
#define __ASM_MACH_IP32_KMALLOC_H


#if defined(CONFIG_CPU_R5000) || defined(CONFIG_CPU_RM7000)
#define ARCH_DMA_MINALIGN	32
#else
#define ARCH_DMA_MINALIGN	128
#endif

#endif /* __ASM_MACH_IP32_KMALLOC_H */
