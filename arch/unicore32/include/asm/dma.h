/*
 * linux/arch/unicore32/include/asm/dma.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __UNICORE_DMA_H__
#define __UNICORE_DMA_H__

#include <asm/memory.h>
#include <asm-generic/dma.h>

#ifdef CONFIG_PCI
extern int isa_dma_bridge_buggy;
#endif

#endif /* __UNICORE_DMA_H__ */
