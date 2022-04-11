/*
 * Copyright(c) 2011-2016 Intel Corporation. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Eddie Dong <eddie.dong@intel.com>
 *    Dexuan Cui
 *    Jike Song <jike.song@intel.com>
 *
 * Contributors:
 *    Zhi Wang <zhi.a.wang@intel.com>
 *
 */

#ifndef _GVT_MPT_H_
#define _GVT_MPT_H_

#include "gvt.h"

/**
 * DOC: Hypervisor Service APIs for GVT-g Core Logic
 *
 * This is the glue layer between specific hypervisor MPT modules and GVT-g core
 * logic. Each kind of hypervisor MPT module provides a collection of function
 * callbacks and will be attached to GVT host when the driver is loading.
 * GVT-g core logic will call these APIs to request specific services from
 * hypervisor.
 */

/**
 * intel_gvt_hypervisor_host_init - init GVT-g host side
 *
 * Returns:
 * Zero on success, negative error code if failed
 */
static inline int intel_gvt_hypervisor_host_init(struct device *dev, void *gvt)
{
	if (!intel_gvt_host.mpt->host_init)
		return -ENODEV;

	return intel_gvt_host.mpt->host_init(dev, gvt);
}

/**
 * intel_gvt_hypervisor_host_exit - exit GVT-g host side
 */
static inline void intel_gvt_hypervisor_host_exit(struct device *dev, void *gvt)
{
	/* optional to provide */
	if (!intel_gvt_host.mpt->host_exit)
		return;

	intel_gvt_host.mpt->host_exit(dev, gvt);
}

/**
 * intel_gvt_hypervisor_enable_page_track - track a guest page
 * @vgpu: a vGPU
 * @gfn: the gfn of guest
 *
 * Returns:
 * Zero on success, negative error code if failed.
 */
static inline int intel_gvt_hypervisor_enable_page_track(
		struct intel_vgpu *vgpu, unsigned long gfn)
{
	return intel_gvt_host.mpt->enable_page_track(vgpu, gfn);
}

/**
 * intel_gvt_hypervisor_disable_page_track - untrack a guest page
 * @vgpu: a vGPU
 * @gfn: the gfn of guest
 *
 * Returns:
 * Zero on success, negative error code if failed.
 */
static inline int intel_gvt_hypervisor_disable_page_track(
		struct intel_vgpu *vgpu, unsigned long gfn)
{
	return intel_gvt_host.mpt->disable_page_track(vgpu, gfn);
}

/**
 * intel_gvt_hypervisor_gfn_to_mfn - translate a GFN to MFN
 * @vgpu: a vGPU
 * @gpfn: guest pfn
 *
 * Returns:
 * MFN on success, INTEL_GVT_INVALID_ADDR if failed.
 */
static inline unsigned long intel_gvt_hypervisor_gfn_to_mfn(
		struct intel_vgpu *vgpu, unsigned long gfn)
{
	return intel_gvt_host.mpt->gfn_to_mfn(vgpu, gfn);
}

/**
 * intel_gvt_hypervisor_dma_map_guest_page - setup dma map for guest page
 * @vgpu: a vGPU
 * @gfn: guest pfn
 * @size: page size
 * @dma_addr: retrieve allocated dma addr
 *
 * Returns:
 * 0 on success, negative error code if failed.
 */
static inline int intel_gvt_hypervisor_dma_map_guest_page(
		struct intel_vgpu *vgpu, unsigned long gfn, unsigned long size,
		dma_addr_t *dma_addr)
{
	return intel_gvt_host.mpt->dma_map_guest_page(vgpu, gfn, size,
						      dma_addr);
}

/**
 * intel_gvt_hypervisor_dma_unmap_guest_page - cancel dma map for guest page
 * @vgpu: a vGPU
 * @dma_addr: the mapped dma addr
 */
static inline void intel_gvt_hypervisor_dma_unmap_guest_page(
		struct intel_vgpu *vgpu, dma_addr_t dma_addr)
{
	intel_gvt_host.mpt->dma_unmap_guest_page(vgpu, dma_addr);
}

/**
 * intel_gvt_hypervisor_dma_pin_guest_page - pin guest dma buf
 * @vgpu: a vGPU
 * @dma_addr: guest dma addr
 *
 * Returns:
 * 0 on success, negative error code if failed.
 */
static inline int
intel_gvt_hypervisor_dma_pin_guest_page(struct intel_vgpu *vgpu,
					dma_addr_t dma_addr)
{
	return intel_gvt_host.mpt->dma_pin_guest_page(vgpu, dma_addr);
}

/**
 * intel_gvt_hypervisor_is_valid_gfn - check if a visible gfn
 * @vgpu: a vGPU
 * @gfn: guest PFN
 *
 * Returns:
 * true on valid gfn, false on not.
 */
static inline bool intel_gvt_hypervisor_is_valid_gfn(
		struct intel_vgpu *vgpu, unsigned long gfn)
{
	if (!intel_gvt_host.mpt->is_valid_gfn)
		return true;

	return intel_gvt_host.mpt->is_valid_gfn(vgpu, gfn);
}

#endif /* _GVT_MPT_H_ */
