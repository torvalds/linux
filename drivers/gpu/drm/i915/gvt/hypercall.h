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

#ifndef _GVT_HYPERCALL_H_
#define _GVT_HYPERCALL_H_

#include <linux/types.h>

struct device;
struct intel_vgpu;

/*
 * Specific GVT-g MPT modules function collections. Currently GVT-g supports
 * both Xen and KVM by providing dedicated hypervisor-related MPT modules.
 */
struct intel_gvt_mpt {
	int (*host_init)(struct device *dev, void *gvt);
	void (*host_exit)(struct device *dev, void *gvt);
	void (*detach_vgpu)(struct intel_vgpu *vgpu);
	int (*inject_msi)(struct intel_vgpu *vgpu, u32 addr, u16 data);
	int (*enable_page_track)(struct intel_vgpu *vgpu, u64 gfn);
	int (*disable_page_track)(struct intel_vgpu *vgpu, u64 gfn);
	unsigned long (*gfn_to_mfn)(struct intel_vgpu *vgpu, unsigned long gfn);

	int (*dma_map_guest_page)(struct intel_vgpu *vgpu, unsigned long gfn,
				  unsigned long size, dma_addr_t *dma_addr);
	void (*dma_unmap_guest_page)(struct intel_vgpu *vgpu,
				dma_addr_t dma_addr);

	int (*dma_pin_guest_page)(struct intel_vgpu *vgpu, dma_addr_t dma_addr);

	int (*set_opregion)(struct intel_vgpu *vgpu);
	int (*set_edid)(struct intel_vgpu *vgpu, int port_num);
	bool (*is_valid_gfn)(struct intel_vgpu *vgpu, unsigned long gfn);
};

#endif /* _GVT_HYPERCALL_H_ */
