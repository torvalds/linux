/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_IA64_DMA_H
#define _ASM_IA64_DMA_H

/*
 * Copyright (C) 1998-2002 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */


#include <asm/io.h>		/* need byte IO */

extern unsigned long MAX_DMA_ADDRESS;

extern int isa_dma_bridge_buggy;

#define free_dma(x)

#endif /* _ASM_IA64_DMA_H */
