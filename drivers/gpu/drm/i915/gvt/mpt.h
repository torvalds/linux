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
 * intel_gvt_hypervisor_detect_host - check if GVT-g is running within
 * hypervisor host/privilged domain
 *
 * Returns:
 * Zero on success, -ENODEV if current kernel is running inside a VM
 */
static inline int intel_gvt_hypervisor_detect_host(void)
{
	return intel_gvt_host.mpt->detect_host();
}

/**
 * intel_gvt_hypervisor_host_init - init GVT-g host side
 *
 * Returns:
 * Zero on success, negative error code if failed
 */
static inline int intel_gvt_hypervisor_host_init(struct device *dev,
			void *gvt, const void *ops)
{
	/* optional to provide */
	if (!intel_gvt_host.mpt->host_init)
		return 0;

	return intel_gvt_host.mpt->host_init(dev, gvt, ops);
}

/**
 * intel_gvt_hypervisor_host_exit - exit GVT-g host side
 */
static inline void intel_gvt_hypervisor_host_exit(struct device *dev,
			void *gvt)
{
	/* optional to provide */
	if (!intel_gvt_host.mpt->host_exit)
		return;

	intel_gvt_host.mpt->host_exit(dev, gvt);
}

/**
 * intel_gvt_hypervisor_attach_vgpu - call hypervisor to initialize vGPU
 * related stuffs inside hypervisor.
 *
 * Returns:
 * Zero on success, negative error code if failed.
 */
static inline int intel_gvt_hypervisor_attach_vgpu(struct intel_vgpu *vgpu)
{
	/* optional to provide */
	if (!intel_gvt_host.mpt->attach_vgpu)
		return 0;

	return intel_gvt_host.mpt->attach_vgpu(vgpu, &vgpu->handle);
}

/**
 * intel_gvt_hypervisor_detach_vgpu - call hypervisor to release vGPU
 * related stuffs inside hypervisor.
 *
 * Returns:
 * Zero on success, negative error code if failed.
 */
static inline void intel_gvt_hypervisor_detach_vgpu(struct intel_vgpu *vgpu)
{
	/* optional to provide */
	if (!intel_gvt_host.mpt->detach_vgpu)
		return;

	intel_gvt_host.mpt->detach_vgpu(vgpu->handle);
}

#define MSI_CAP_CONTROL(offset) (offset + 2)
#define MSI_CAP_ADDRESS(offset) (offset + 4)
#define MSI_CAP_DATA(offset) (offset + 8)
#define MSI_CAP_EN 0x1

/**
 * intel_gvt_hypervisor_inject_msi - inject a MSI interrupt into vGPU
 *
 * Returns:
 * Zero on success, negative error code if failed.
 */
static inline int intel_gvt_hypervisor_inject_msi(struct intel_vgpu *vgpu)
{
	unsigned long offset = vgpu->gvt->device_info.msi_cap_offset;
	u16 control, data;
	u32 addr;
	int ret;

	control = *(u16 *)(vgpu_cfg_space(vgpu) + MSI_CAP_CONTROL(offset));
	addr = *(u32 *)(vgpu_cfg_space(vgpu) + MSI_CAP_ADDRESS(offset));
	data = *(u16 *)(vgpu_cfg_space(vgpu) + MSI_CAP_DATA(offset));

	/* Do not generate MSI if MSIEN is disable */
	if (!(control & MSI_CAP_EN))
		return 0;

	if (WARN(control & GENMASK(15, 1), "only support one MSI format\n"))
		return -EINVAL;

	gvt_dbg_irq("vgpu%d: inject msi address %x data%x\n", vgpu->id, addr,
		    data);

	ret = intel_gvt_host.mpt->inject_msi(vgpu->handle, addr, data);
	if (ret)
		return ret;
	return 0;
}

/**
 * intel_gvt_hypervisor_set_wp_page - translate a host VA into MFN
 * @p: host kernel virtual address
 *
 * Returns:
 * MFN on success, INTEL_GVT_INVALID_ADDR if failed.
 */
static inline unsigned long intel_gvt_hypervisor_virt_to_mfn(void *p)
{
	return intel_gvt_host.mpt->from_virt_to_mfn(p);
}

/**
 * intel_gvt_hypervisor_set_wp_page - set a guest page to write-protected
 * @vgpu: a vGPU
 * @p: intel_vgpu_guest_page
 *
 * Returns:
 * Zero on success, negative error code if failed.
 */
static inline int intel_gvt_hypervisor_set_wp_page(struct intel_vgpu *vgpu,
		struct intel_vgpu_guest_page *p)
{
	int ret;

	if (p->writeprotection)
		return 0;

	ret = intel_gvt_host.mpt->set_wp_page(vgpu->handle, p->gfn);
	if (ret)
		return ret;
	p->writeprotection = true;
	atomic_inc(&vgpu->gtt.n_write_protected_guest_page);
	return 0;
}

/**
 * intel_gvt_hypervisor_unset_wp_page - remove the write-protection of a
 * guest page
 * @vgpu: a vGPU
 * @p: intel_vgpu_guest_page
 *
 * Returns:
 * Zero on success, negative error code if failed.
 */
static inline int intel_gvt_hypervisor_unset_wp_page(struct intel_vgpu *vgpu,
		struct intel_vgpu_guest_page *p)
{
	int ret;

	if (!p->writeprotection)
		return 0;

	ret = intel_gvt_host.mpt->unset_wp_page(vgpu->handle, p->gfn);
	if (ret)
		return ret;
	p->writeprotection = false;
	atomic_dec(&vgpu->gtt.n_write_protected_guest_page);
	return 0;
}

/**
 * intel_gvt_hypervisor_read_gpa - copy data from GPA to host data buffer
 * @vgpu: a vGPU
 * @gpa: guest physical address
 * @buf: host data buffer
 * @len: data length
 *
 * Returns:
 * Zero on success, negative error code if failed.
 */
static inline int intel_gvt_hypervisor_read_gpa(struct intel_vgpu *vgpu,
		unsigned long gpa, void *buf, unsigned long len)
{
	return intel_gvt_host.mpt->read_gpa(vgpu->handle, gpa, buf, len);
}

/**
 * intel_gvt_hypervisor_write_gpa - copy data from host data buffer to GPA
 * @vgpu: a vGPU
 * @gpa: guest physical address
 * @buf: host data buffer
 * @len: data length
 *
 * Returns:
 * Zero on success, negative error code if failed.
 */
static inline int intel_gvt_hypervisor_write_gpa(struct intel_vgpu *vgpu,
		unsigned long gpa, void *buf, unsigned long len)
{
	return intel_gvt_host.mpt->write_gpa(vgpu->handle, gpa, buf, len);
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
	return intel_gvt_host.mpt->gfn_to_mfn(vgpu->handle, gfn);
}

/**
 * intel_gvt_hypervisor_map_gfn_to_mfn - map a GFN region to MFN
 * @vgpu: a vGPU
 * @gfn: guest PFN
 * @mfn: host PFN
 * @nr: amount of PFNs
 * @map: map or unmap
 *
 * Returns:
 * Zero on success, negative error code if failed.
 */
static inline int intel_gvt_hypervisor_map_gfn_to_mfn(
		struct intel_vgpu *vgpu, unsigned long gfn,
		unsigned long mfn, unsigned int nr,
		bool map)
{
	/* a MPT implementation could have MMIO mapped elsewhere */
	if (!intel_gvt_host.mpt->map_gfn_to_mfn)
		return 0;

	return intel_gvt_host.mpt->map_gfn_to_mfn(vgpu->handle, gfn, mfn, nr,
						  map);
}

/**
 * intel_gvt_hypervisor_set_trap_area - Trap a guest PA region
 * @vgpu: a vGPU
 * @start: the beginning of the guest physical address region
 * @end: the end of the guest physical address region
 * @map: map or unmap
 *
 * Returns:
 * Zero on success, negative error code if failed.
 */
static inline int intel_gvt_hypervisor_set_trap_area(
		struct intel_vgpu *vgpu, u64 start, u64 end, bool map)
{
	/* a MPT implementation could have MMIO trapped elsewhere */
	if (!intel_gvt_host.mpt->set_trap_area)
		return 0;

	return intel_gvt_host.mpt->set_trap_area(vgpu->handle, start, end, map);
}

#endif /* _GVT_MPT_H_ */
