/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_LKL_DMA_H
#define _ASM_LKL_DMA_H

#include <asm-generic/dma.h>

#ifdef CONFIG_PCI
extern int isa_dma_bridge_buggy;
#else
#define isa_dma_bridge_buggy (0)
#endif

#endif /* _ASM_LKL_DMA_H */
