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

	magic = __raw_i915_read64(dev_priv, vgtif_reg(magic));
	if (magic != VGT_MAGIC)
		return;

	version = INTEL_VGT_IF_VERSION_ENCODE(
		__raw_i915_read16(dev_priv, vgtif_reg(version_major)),
		__raw_i915_read16(dev_priv, vgtif_reg(version_minor)));
	if (version != INTEL_VGT_IF_VERSION) {
		DRM_INFO("VGT interface version mismatch!\n");
		return;
	}

	dev_priv->vgpu.active = true;
	DRM_INFO("Virtual GPU for Intel GVT-g detected.\n");
}

struct _balloon_info_ {
	/*
	 * There are up to 2 regions per mappable/unmappable graphic
	 * memory that might be ballooned. Here, index 0/1 is for mappable
	 * graphic memory, 2/3 for unmappable graphic memory.
	 */
	struct drm_mm_node space[4];
};

static struct _balloon_info_ bl_info;

/**
 * intel_vgt_deballoon - deballoon reserved graphics address trunks
 *
 * This function is called to deallocate the ballooned-out graphic memory, when
 * driver is unloaded or when ballooning fails.
 */
void intel_vgt_deballoon(void)
{
	int i;

	DRM_DEBUG("VGT deballoon.\n");

	for (i = 0; i < 4; i++) {
		if (bl_info.space[i].allocated)
			drm_mm_remove_node(&bl_info.space[i]);
	}

	memset(&bl_info, 0, sizeof(bl_info));
}

static int vgt_balloon_space(struct drm_mm *mm,
			     struct drm_mm_node *node,
			     unsigned long start, unsigned long end)
{
	unsigned long size = end - start;

	if (start == end)
		return -EINVAL;

	DRM_INFO("balloon space: range [ 0x%lx - 0x%lx ] %lu KiB.\n",
		 start, end, size / 1024);

	node->start = start;
	node->size = size;

	return drm_mm_reserve_node(mm, node);
}

/**
 * intel_vgt_balloon - balloon out reserved graphics address trunks
 * @dev: drm device
 *
 * This function is called at the initialization stage, to balloon out the
 * graphic address space allocated to other vGPUs, by marking these spaces as
 * reserved. The ballooning related knowledge(starting address and size of
 * the mappable/unmappable graphic memory) is described in the vgt_if structure
 * in a reserved mmio range.
 *
 * To give an example, the drawing below depicts one typical scenario after
 * ballooning. Here the vGPU1 has 2 pieces of graphic address spaces ballooned
 * out each for the mappable and the non-mappable part. From the vGPU1 point of
 * view, the total size is the same as the physical one, with the start address
 * of its graphic space being zero. Yet there are some portions ballooned out(
 * the shadow part, which are marked as reserved by drm allocator). From the
 * host point of view, the graphic address space is partitioned by multiple
 * vGPUs in different VMs.
 *
 *                        vGPU1 view         Host view
 *             0 ------> +-----------+     +-----------+
 *               ^       |///////////|     |   vGPU3   |
 *               |       |///////////|     +-----------+
 *               |       |///////////|     |   vGPU2   |
 *               |       +-----------+     +-----------+
 *        mappable GM    | available | ==> |   vGPU1   |
 *               |       +-----------+     +-----------+
 *               |       |///////////|     |           |
 *               v       |///////////|     |   Host    |
 *               +=======+===========+     +===========+
 *               ^       |///////////|     |   vGPU3   |
 *               |       |///////////|     +-----------+
 *               |       |///////////|     |   vGPU2   |
 *               |       +-----------+     +-----------+
 *      unmappable GM    | available | ==> |   vGPU1   |
 *               |       +-----------+     +-----------+
 *               |       |///////////|     |           |
 *               |       |///////////|     |   Host    |
 *               v       |///////////|     |           |
 * total GM size ------> +-----------+     +-----------+
 *
 * Returns:
 * zero on success, non-zero if configuration invalid or ballooning failed
 */
int intel_vgt_balloon(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct i915_address_space *ggtt_vm = &dev_priv->gtt.base;
	unsigned long ggtt_vm_end = ggtt_vm->start + ggtt_vm->total;

	unsigned long mappable_base, mappable_size, mappable_end;
	unsigned long unmappable_base, unmappable_size, unmappable_end;
	int ret;

	mappable_base = I915_READ(vgtif_reg(avail_rs.mappable_gmadr.base));
	mappable_size = I915_READ(vgtif_reg(avail_rs.mappable_gmadr.size));
	unmappable_base = I915_READ(vgtif_reg(avail_rs.nonmappable_gmadr.base));
	unmappable_size = I915_READ(vgtif_reg(avail_rs.nonmappable_gmadr.size));

	mappable_end = mappable_base + mappable_size;
	unmappable_end = unmappable_base + unmappable_size;

	DRM_INFO("VGT ballooning configuration:\n");
	DRM_INFO("Mappable graphic memory: base 0x%lx size %ldKiB\n",
		 mappable_base, mappable_size / 1024);
	DRM_INFO("Unmappable graphic memory: base 0x%lx size %ldKiB\n",
		 unmappable_base, unmappable_size / 1024);

	if (mappable_base < ggtt_vm->start ||
	    mappable_end > dev_priv->gtt.mappable_end ||
	    unmappable_base < dev_priv->gtt.mappable_end ||
	    unmappable_end > ggtt_vm_end) {
		DRM_ERROR("Invalid ballooning configuration!\n");
		return -EINVAL;
	}

	/* Unmappable graphic memory ballooning */
	if (unmappable_base > dev_priv->gtt.mappable_end) {
		ret = vgt_balloon_space(&ggtt_vm->mm,
					&bl_info.space[2],
					dev_priv->gtt.mappable_end,
					unmappable_base);

		if (ret)
			goto err;
	}

	/*
	 * No need to partition out the last physical page,
	 * because it is reserved to the guard page.
	 */
	if (unmappable_end < ggtt_vm_end - PAGE_SIZE) {
		ret = vgt_balloon_space(&ggtt_vm->mm,
					&bl_info.space[3],
					unmappable_end,
					ggtt_vm_end - PAGE_SIZE);
		if (ret)
			goto err;
	}

	/* Mappable graphic memory ballooning */
	if (mappable_base > ggtt_vm->start) {
		ret = vgt_balloon_space(&ggtt_vm->mm,
					&bl_info.space[0],
					ggtt_vm->start, mappable_base);

		if (ret)
			goto err;
	}

	if (mappable_end < dev_priv->gtt.mappable_end) {
		ret = vgt_balloon_space(&ggtt_vm->mm,
					&bl_info.space[1],
					mappable_end,
					dev_priv->gtt.mappable_end);

		if (ret)
			goto err;
	}

	DRM_INFO("VGT balloon successfully\n");
	return 0;

err:
	DRM_ERROR("VGT balloon fail\n");
	intel_vgt_deballoon();
	return ret;
}
