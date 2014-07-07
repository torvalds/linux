/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2013,2017-2018 The Linux Foundation. All rights reserved.
 */

#ifndef _ASM_DMA_CONTIGUOUS_H
#define _ASM_DMA_CONTIGUOUS_H

#ifdef __KERNEL__
#ifdef CONFIG_DMA_CMA

#include <linux/types.h>

void dma_contiguous_early_fixup(phys_addr_t base, unsigned long size);

#endif
#endif

#endif
