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

#include "i915_drv.h"
#include "i915_vgpu.h"
#include "intel_gvt.h"
#include "gvt/gvt.h"

/**
 * DOC: Intel GVT-g host support
 *
 * Intel GVT-g is a graphics virtualization technology which shares the
 * GPU among multiple virtual machines on a time-sharing basis. Each
 * virtual machine is presented a virtual GPU (vGPU), which has equivalent
 * features as the underlying physical GPU (pGPU), so i915 driver can run
 * seamlessly in a virtual machine.
 *
 * To virtualize GPU resources GVT-g driver depends on hypervisor technology
 * e.g KVM/VFIO/mdev, Xen, etc. to provide resource access trapping capability
 * and be virtualized within GVT-g device module. More architectural design
 * doc is available on https://01.org/group/2230/documentation-list.
 */

static bool is_supported_device(struct drm_i915_private *dev_priv)
{
	if (IS_BROADWELL(dev_priv))
		return true;
	if (IS_SKYLAKE(dev_priv))
		return true;
	if (IS_KABYLAKE(dev_priv))
		return true;
	if (IS_BROXTON(dev_priv))
		return true;
	if (IS_COFFEELAKE(dev_priv))
		return true;
	if (IS_COMETLAKE(dev_priv))
		return true;

	return false;
}

/**
 * intel_gvt_sanitize_options - sanitize GVT related options
 * @dev_priv: drm i915 private data
 *
 * This function is called at the i915 options sanitize stage.
 */
void intel_gvt_sanitize_options(struct drm_i915_private *dev_priv)
{
	if (!dev_priv->params.enable_gvt)
		return;

	if (intel_vgpu_active(dev_priv)) {
		drm_info(&dev_priv->drm, "GVT-g is disabled for guest\n");
		goto bail;
	}

	if (!is_supported_device(dev_priv)) {
		drm_info(&dev_priv->drm,
			 "Unsupported device. GVT-g is disabled\n");
		goto bail;
	}

	return;
bail:
	dev_priv->params.enable_gvt = 0;
}

static void free_initial_hw_state(struct drm_i915_private *dev_priv)
{
	struct i915_virtual_gpu *vgpu = &dev_priv->vgpu;

	vfree(vgpu->initial_mmio);
	vgpu->initial_mmio = NULL;

	kfree(vgpu->initial_cfg_space);
	vgpu->initial_cfg_space = NULL;
}

static void save_mmio(struct intel_gvt_mmio_table_iter *iter, u32 offset,
		      u32 size)
{
	struct drm_i915_private *dev_priv = iter->i915;
	u32 *mmio, i;

	for (i = offset; i < offset + size; i += 4) {
		mmio = iter->data + i;
		*mmio = intel_uncore_read_notrace(to_gt(dev_priv)->uncore,
						  _MMIO(i));
	}
}

static int handle_mmio(struct intel_gvt_mmio_table_iter *iter,
		       u32 offset, u32 size)
{
	if (WARN_ON(!IS_ALIGNED(offset, 4)))
		return -EINVAL;

	save_mmio(iter, offset, size);
	return 0;
}

static int save_initial_hw_state(struct drm_i915_private *dev_priv)
{
	struct pci_dev *pdev = to_pci_dev(dev_priv->drm.dev);
	struct i915_virtual_gpu *vgpu = &dev_priv->vgpu;
	struct intel_gvt_mmio_table_iter iter;
	void *mem;
	int i, ret;

	mem = kzalloc(PCI_CFG_SPACE_EXP_SIZE, GFP_KERNEL);
	if (!mem)
		return -ENOMEM;

	vgpu->initial_cfg_space = mem;

	for (i = 0; i < PCI_CFG_SPACE_EXP_SIZE; i += 4)
		pci_read_config_dword(pdev, i, mem + i);

	mem = vzalloc(2 * SZ_1M);
	if (!mem) {
		ret = -ENOMEM;
		goto err_mmio;
	}

	vgpu->initial_mmio = mem;

	iter.i915 = dev_priv;
	iter.data = vgpu->initial_mmio;
	iter.handle_mmio_cb = handle_mmio;

	ret = intel_gvt_iterate_mmio_table(&iter);
	if (ret)
		goto err_iterate;

	return 0;

err_iterate:
	vfree(vgpu->initial_mmio);
	vgpu->initial_mmio = NULL;
err_mmio:
	kfree(vgpu->initial_cfg_space);
	vgpu->initial_cfg_space = NULL;

	return ret;
}

/**
 * intel_gvt_init - initialize GVT components
 * @dev_priv: drm i915 private data
 *
 * This function is called at the initialization stage to create a GVT device.
 *
 * Returns:
 * Zero on success, negative error code if failed.
 *
 */
int intel_gvt_init(struct drm_i915_private *dev_priv)
{
	int ret;

	if (i915_inject_probe_failure(dev_priv))
		return -ENODEV;

	if (!dev_priv->params.enable_gvt) {
		drm_dbg(&dev_priv->drm,
			"GVT-g is disabled by kernel params\n");
		return 0;
	}

	if (intel_uc_wants_guc_submission(&to_gt(dev_priv)->uc)) {
		drm_err(&dev_priv->drm,
			"i915 GVT-g loading failed due to Graphics virtualization is not yet supported with GuC submission\n");
		return -EIO;
	}

	ret = save_initial_hw_state(dev_priv);
	if (ret) {
		drm_dbg(&dev_priv->drm, "Fail to save initial HW state\n");
		goto err_save_hw_state;
	}

	ret = intel_gvt_init_device(dev_priv);
	if (ret) {
		drm_dbg(&dev_priv->drm, "Fail to init GVT device\n");
		goto err_init_device;
	}

	return 0;

err_init_device:
	free_initial_hw_state(dev_priv);
err_save_hw_state:
	dev_priv->params.enable_gvt = 0;
	return 0;
}

static inline bool intel_gvt_active(struct drm_i915_private *dev_priv)
{
	return dev_priv->gvt;
}

/**
 * intel_gvt_driver_remove - cleanup GVT components when i915 driver is
 *			     unbinding
 * @dev_priv: drm i915 private *
 *
 * This function is called at the i915 driver unloading stage, to shutdown
 * GVT components and release the related resources.
 */
void intel_gvt_driver_remove(struct drm_i915_private *dev_priv)
{
	if (!intel_gvt_active(dev_priv))
		return;

	intel_gvt_clean_device(dev_priv);
	free_initial_hw_state(dev_priv);
}

/**
 * intel_gvt_resume - GVT resume routine wapper
 *
 * @dev_priv: drm i915 private *
 *
 * This function is called at the i915 driver resume stage to restore required
 * HW status for GVT so that vGPU can continue running after resumed.
 */
void intel_gvt_resume(struct drm_i915_private *dev_priv)
{
	if (intel_gvt_active(dev_priv))
		intel_gvt_pm_resume(dev_priv->gvt);
}
