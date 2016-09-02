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
 *    Kevin Tian <kevin.tian@intel.com>
 *    Eddie Dong <eddie.dong@intel.com>
 *
 * Contributors:
 *    Niu Bing <bing.niu@intel.com>
 *    Zhi Wang <zhi.a.wang@intel.com>
 *
 */

#include <linux/types.h>
#include <xen/xen.h>

#include "i915_drv.h"

struct intel_gvt_host intel_gvt_host;

static const char * const supported_hypervisors[] = {
	[INTEL_GVT_HYPERVISOR_XEN] = "XEN",
	[INTEL_GVT_HYPERVISOR_KVM] = "KVM",
};

struct intel_gvt_io_emulation_ops intel_gvt_io_emulation_ops = {
	.emulate_cfg_read = intel_vgpu_emulate_cfg_read,
	.emulate_cfg_write = intel_vgpu_emulate_cfg_write,
	.emulate_mmio_read = intel_vgpu_emulate_mmio_read,
	.emulate_mmio_write = intel_vgpu_emulate_mmio_write,
};

/**
 * intel_gvt_init_host - Load MPT modules and detect if we're running in host
 * @gvt: intel gvt device
 *
 * This function is called at the driver loading stage. If failed to find a
 * loadable MPT module or detect currently we're running in a VM, then GVT-g
 * will be disabled
 *
 * Returns:
 * Zero on success, negative error code if failed.
 *
 */
int intel_gvt_init_host(void)
{
	if (intel_gvt_host.initialized)
		return 0;

	/* Xen DOM U */
	if (xen_domain() && !xen_initial_domain())
		return -ENODEV;

	/* Try to load MPT modules for hypervisors */
	if (xen_initial_domain()) {
		/* In Xen dom0 */
		intel_gvt_host.mpt = try_then_request_module(
				symbol_get(xengt_mpt), "xengt");
		intel_gvt_host.hypervisor_type = INTEL_GVT_HYPERVISOR_XEN;
	} else {
		/* not in Xen. Try KVMGT */
		intel_gvt_host.mpt = try_then_request_module(
				symbol_get(kvmgt_mpt), "kvm");
		intel_gvt_host.hypervisor_type = INTEL_GVT_HYPERVISOR_KVM;
	}

	/* Fail to load MPT modules - bail out */
	if (!intel_gvt_host.mpt)
		return -EINVAL;

	/* Try to detect if we're running in host instead of VM. */
	if (!intel_gvt_hypervisor_detect_host())
		return -ENODEV;

	gvt_dbg_core("Running with hypervisor %s in host mode\n",
			supported_hypervisors[intel_gvt_host.hypervisor_type]);

	intel_gvt_host.initialized = true;
	return 0;
}

static void init_device_info(struct intel_gvt *gvt)
{
	struct intel_gvt_device_info *info = &gvt->device_info;

	if (IS_BROADWELL(gvt->dev_priv) || IS_SKYLAKE(gvt->dev_priv)) {
		info->max_support_vgpus = 8;
		info->cfg_space_size = 256;
		info->mmio_size = 2 * 1024 * 1024;
		info->mmio_bar = 0;
		info->msi_cap_offset = IS_SKYLAKE(gvt->dev_priv) ? 0xac : 0x90;
		info->gtt_start_offset = 8 * 1024 * 1024;
		info->gtt_entry_size = 8;
		info->gtt_entry_size_shift = 3;
	}
}

/**
 * intel_gvt_clean_device - clean a GVT device
 * @gvt: intel gvt device
 *
 * This function is called at the driver unloading stage, to free the
 * resources owned by a GVT device.
 *
 */
void intel_gvt_clean_device(struct drm_i915_private *dev_priv)
{
	struct intel_gvt *gvt = &dev_priv->gvt;

	if (WARN_ON(!gvt->initialized))
		return;

	intel_gvt_clean_opregion(gvt);
	intel_gvt_clean_gtt(gvt);
	intel_gvt_clean_irq(gvt);
	intel_gvt_clean_mmio_info(gvt);
	intel_gvt_free_firmware(gvt);

	gvt->initialized = false;
}

/**
 * intel_gvt_init_device - initialize a GVT device
 * @dev_priv: drm i915 private data
 *
 * This function is called at the initialization stage, to initialize
 * necessary GVT components.
 *
 * Returns:
 * Zero on success, negative error code if failed.
 *
 */
int intel_gvt_init_device(struct drm_i915_private *dev_priv)
{
	struct intel_gvt *gvt = &dev_priv->gvt;
	int ret;

	/*
	 * Cannot initialize GVT device without intel_gvt_host gets
	 * initialized first.
	 */
	if (WARN_ON(!intel_gvt_host.initialized))
		return -EINVAL;

	if (WARN_ON(gvt->initialized))
		return -EEXIST;

	gvt_dbg_core("init gvt device\n");

	mutex_init(&gvt->lock);
	gvt->dev_priv = dev_priv;

	init_device_info(gvt);

	ret = intel_gvt_setup_mmio_info(gvt);
	if (ret)
		return ret;

	ret = intel_gvt_load_firmware(gvt);
	if (ret)
		goto out_clean_mmio_info;

	ret = intel_gvt_init_irq(gvt);
	if (ret)
		goto out_free_firmware;

	ret = intel_gvt_init_gtt(gvt);
	if (ret)
		goto out_clean_irq;

	ret = intel_gvt_init_opregion(gvt);
	if (ret)
		goto out_clean_gtt;

	gvt_dbg_core("gvt device creation is done\n");
	gvt->initialized = true;
	return 0;

out_clean_gtt:
	intel_gvt_clean_gtt(gvt);
out_clean_irq:
	intel_gvt_clean_irq(gvt);
out_free_firmware:
	intel_gvt_free_firmware(gvt);
out_clean_mmio_info:
	intel_gvt_clean_mmio_info(gvt);
	return ret;
}
