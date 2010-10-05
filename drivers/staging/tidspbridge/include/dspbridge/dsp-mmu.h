/*
 * dsp-mmu.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * DSP iommu.
 *
 * Copyright (C) 2005-2010 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef _DSP_MMU_
#define _DSP_MMU_

#include <plat/iommu.h>
#include <plat/iovmm.h>

/**
 * dsp_mmu_init() - initialize dsp_mmu module and returns a handle
 *
 * This function initialize dsp mmu module and returns a struct iommu
 * handle to use it for dsp maps.
 *
 */
struct iommu *dsp_mmu_init(void);

/**
 * dsp_mmu_exit() - destroy dsp mmu module
 * @mmu:	Pointer to iommu handle.
 *
 * This function destroys dsp mmu module.
 *
 */
void dsp_mmu_exit(struct iommu *mmu);

/**
 * user_to_dsp_map() - maps user to dsp virtual address
 * @mmu:	Pointer to iommu handle.
 * @uva:		Virtual user space address.
 * @da		DSP address
 * @size		Buffer size to map.
 * @usr_pgs	struct page array pointer where the user pages will be stored
 *
 * This function maps a user space buffer into DSP virtual address.
 *
 */
u32 user_to_dsp_map(struct iommu *mmu, u32 uva, u32 da, u32 size,
						struct page **usr_pgs);

/**
 * user_to_dsp_unmap() - unmaps DSP virtual buffer.
 * @mmu:	Pointer to iommu handle.
 * @da		DSP address
 *
 * This function unmaps a user space buffer into DSP virtual address.
 *
 */
int user_to_dsp_unmap(struct iommu *mmu, u32 da);

#endif
