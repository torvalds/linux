/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 */

#ifndef ASM_ARC_DMA_H
#define ASM_ARC_DMA_H

#define MAX_DMA_ADDRESS 0xC0000000
#ifdef CONFIG_PCI
extern int isa_dma_bridge_buggy;
#else
#define isa_dma_bridge_buggy	0
#endif

#endif
