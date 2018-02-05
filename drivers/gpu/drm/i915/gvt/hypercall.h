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

/*
 * Specific GVT-g MPT modules function collections. Currently GVT-g supports
 * both Xen and KVM by providing dedicated hypervisor-related MPT modules.
 */
struct intel_gvt_mpt {
	int (*host_init)(struct device *dev, void *gvt, const void *ops);
	void (*host_exit)(struct device *dev, void *gvt);
	int (*attach_vgpu)(void *vgpu, unsigned long *handle);
	void (*detach_vgpu)(unsigned long handle);
	int (*inject_msi)(unsigned long handle, u32 addr, u16 data);
	unsigned long (*from_virt_to_mfn)(void *p);
	int (*set_wp_page)(unsigned long handle, u64 gfn);
	int (*unset_wp_page)(unsigned long handle, u64 gfn);
	int (*read_gpa)(unsigned long handle, unsigned long gpa, void *buf,
			unsigned long len);
	int (*write_gpa)(unsigned long handle, unsigned long gpa, void *buf,
			 unsigned long len);
	unsigned long (*gfn_to_mfn)(unsigned long handle, unsigned long gfn);
	int (*map_gfn_to_mfn)(unsigned long handle, unsigned long gfn,
			      unsigned long mfn, unsigned int nr, bool map);
	int (*set_trap_area)(unsigned long handle, u64 start, u64 end,
			     bool map);
	int (*set_opregion)(void *vgpu);
	int (*get_vfio_device)(void *vgpu);
	void (*put_vfio_device)(void *vgpu);
};

extern struct intel_gvt_mpt xengt_mpt;
extern struct intel_gvt_mpt kvmgt_mpt;

#endif /* _GVT_HYPERCALL_H_ */
