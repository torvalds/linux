/*
 * Copyright(c) 2011-2015 Intel Corporation. All rights reserved.
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

#include "intel_drv.h"
#include "i915_vgpu.h"

/**
 * DOC: Intel GVT-g guest support
 *
 * Intel GVT-g is a graphics virtualization technology which shares the
 * GPU among multiple virtual machines on a time-sharing basis. Each
 * virtual machine is presented a virtual GPU (vGPU), which has equivalent
 * features as the underlying physical GPU (pGPU), so i915 driver can run
 * seamlessly in a virtual machine. This file provides vGPU specific
 * optimizations when running in a virtual machine, to reduce the complexity
 * of vGPU emulation and to improve the overall performance.
 *
 * A primary function introduced here is so-called "address space ballooning"
 * technique. Intel GVT-g partitions global graphics memory among multiple VMs,
 * so each VM can directly access a portion of the memory without hypervisor's
 * intervention, e.g. filling textures or queuing commands. However with the
 * partitioning an unmodified i915 driver would assume a smaller graphics
 * memory starting from address ZERO, then requires vGPU emulation module to
 * translate the graphics address between 'guest view' and 'host view', for
 * all registers and command opcodes which contain a graphics memory address.
 * To reduce the complexity, Intel GVT-g introduces "address space ballooning",
 * by telling the exact partitioning knowledge to each guest i915 driver, which
 * then reserves and prevents non-allocated portions from allocation. Thus vGPU
 * emulation module only needs to scan and validate graphics addresses without
 * complexity of address translation.
 *
 */

/**
 * i915_check_vgpu - detect virtual GPU
 * @dev: drm device *
 *
 * This function is called at the initialization stage, to detect whether
 * running on a vGPU.
 */
void i915_check_vgpu(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	uint64_t magic;
	uint32_t version;

	BUILD_BUG_ON(sizeof(struct vgt_if) != VGT_PVINFO_SIZE);

	if (!IS_HASWELL(dev))
		return;

	magic = readq(dev_priv->regs + vgtif_reg(magic));
	if (magic != VGT_MAGIC)
		return;

	version = INTEL_VGT_IF_VERSION_ENCODE(
		readw(dev_priv->regs + vgtif_reg(version_major)),
		readw(dev_priv->regs + vgtif_reg(version_minor)));
	if (version != INTEL_VGT_IF_VERSION) {
		DRM_INFO("VGT interface version mismatch!\n");
		return;
	}

	dev_priv->vgpu.active = true;
	DRM_INFO("Virtual GPU for Intel GVT-g detected.\n");
}
