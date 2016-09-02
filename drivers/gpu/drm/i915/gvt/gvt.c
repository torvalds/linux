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
 */

#include <linux/types.h>
#include <xen/xen.h>

#include "i915_drv.h"

struct intel_gvt_host intel_gvt_host;

static const char * const supported_hypervisors[] = {
	[INTEL_GVT_HYPERVISOR_XEN] = "XEN",
	[INTEL_GVT_HYPERVISOR_KVM] = "KVM",
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
	if (IS_BROADWELL(gvt->dev_priv) || IS_SKYLAKE(gvt->dev_priv))
		gvt->device_info.max_support_vgpus = 8;
	/* This function will grow large in GVT device model patches. */
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

	/* Other de-initialization of GVT components will be introduced. */

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
	/*
	 * Other initialization of GVT components will be introduce here.
	 */
	gvt_dbg_core("gvt device creation is done\n");
	gvt->initialized = true;
	return 0;
}
