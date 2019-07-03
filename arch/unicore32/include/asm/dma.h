/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/arch/unicore32/include/asm/dma.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 */

#ifndef __UNICORE_DMA_H__
#define __UNICORE_DMA_H__

#include <asm/memory.h>
#include <asm-generic/dma.h>

#ifdef CONFIG_PCI
extern int isa_dma_bridge_buggy;
#endif

#endif /* __UNICORE_DMA_H__ */
