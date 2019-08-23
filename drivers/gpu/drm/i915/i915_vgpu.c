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
 * i915_detect_vgpu - detect virtual GPU
 * @dev_priv: i915 device private
 *
 * This function is called at the initialization stage, to detect whether
 * running on a vGPU.
 */
void i915_detect_vgpu(struct drm_i915_private *dev_priv)
{
	struct pci_dev *pdev = dev_priv->drm.pdev;
	u64 magic;
	u16 version_major;
	void __iomem *shared_area;

	BUILD_BUG_ON(sizeof(struct vgt_if) != VGT_PVINFO_SIZE);

	/*
	 * This is called before we setup the main MMIO BAR mappings used via
	 * the uncore structure, so we need to access the BAR directly. Since
	 * we do not support VGT on older gens, return early so we don't have
	 * to consider differently numbered or sized MMIO bars
	 */
	if (INTEL_GEN(dev_priv) < 6)
		return;

	shared_area = pci_iomap_range(pdev, 0, VGT_PVINFO_PAGE, VGT_PVINFO_SIZE);
	if (!shared_area) {
		DRM_ERROR("failed to map MMIO bar to check for VGT\n");
		return;
	}

	magic = readq(shared_area + vgtif_offset(magic));
	if (magic != VGT_MAGIC)
		goto out;

	version_major = readw(shared_area + vgtif_offset(version_major));
	if (version_major < VGT_VERSION_MAJOR) {
		DRM_INFO("VGT interface version mismatch!\n");
		goto out;
	}

	dev_priv->vgpu.caps = readl(shared_area + vgtif_offset(vgt_caps));

	dev_priv->vgpu.active = true;
	mutex_init(&dev_priv->vgpu.lock);
	DRM_INFO("Virtual GPU for Intel GVT-g detected.\n");

out:
	pci_iounmap(pdev, shared_area);
}

bool intel_vgpu_has_full_ppgtt(struct drm_i915_private *dev_priv)
{
	return dev_priv->vgpu.caps & VGT_CAPS_FULL_PPGTT;
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

static void vgt_deballoon_space(struct i915_ggtt *ggtt,
				struct drm_mm_node *node)
{
	if (!drm_mm_node_allocated(node))
		return;

	DRM_DEBUG_DRIVER("deballoon space: range [0x%llx - 0x%llx] %llu KiB.\n",
			 node->start,
			 node->start + node->size,
			 node->size / 1024);

	ggtt->vm.reserved -= node->size;
	drm_mm_remove_node(node);
}

/**
 * intel_vgt_deballoon - deballoon reserved graphics address trunks
 * @ggtt: the global GGTT from which we reserved earlier
 *
 * This function is called to deallocate the ballooned-out graphic memory, when
 * driver is unloaded or when ballooning fails.
 */
void intel_vgt_deballoon(struct i915_ggtt *ggtt)
{
	int i;

	if (!intel_vgpu_active(ggtt->vm.i915))
		return;

	DRM_DEBUG("VGT deballoon.\n");

	for (i = 0; i < 4; i++)
		vgt_deballoon_space(ggtt, &bl_info.space[i]);
}

static int vgt_balloon_space(struct i915_ggtt *ggtt,
			     struct drm_mm_node *node,
			     unsigned long start, unsigned long end)
{
	unsigned long size = end - start;
	int ret;

	if (start >= end)
		return -EINVAL;

	DRM_INFO("balloon space: range [ 0x%lx - 0x%lx ] %lu KiB.\n",
		 start, end, size / 1024);
	ret = i915_gem_gtt_reserve(&ggtt->vm, node,
				   size, start, I915_COLOR_UNEVICTABLE,
				   0);
	if (!ret)
		ggtt->vm.reserved += size;

	return ret;
}

/**
 * intel_vgt_balloon - balloon out reserved graphics address trunks
 * @ggtt: the global GGTT from which to reserve
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
 * vGPUs in different VMs. ::
 *
 *                         vGPU1 view         Host view
 *              0 ------> +-----------+     +-----------+
 *                ^       |###########|     |   vGPU3   |
 *                |       |###########|     +-----------+
 *                |       |###########|     |   vGPU2   |
 *                |       +-----------+     +-----------+
 *         mappable GM    | available | ==> |   vGPU1   |
 *                |       +-----------+     +-----------+
 *                |       |###########|     |           |
 *                v       |###########|     |   Host    |
 *                +=======+===========+     +===========+
 *                ^       |###########|     |   vGPU3   |
 *                |       |###########|     +-----------+
 *                |       |###########|     |   vGPU2   |
 *                |       +-----------+     +-----------+
 *       unmappable GM    | available | ==> |   vGPU1   |
 *                |       +-----------+     +-----------+
 *                |       |###########|     |           |
 *                |       |###########|     |   Host    |
 *                v       |###########|     |           |
 *  total GM size ------> +-----------+     +-----------+
 *
 * Returns:
 * zero on success, non-zero if configuration invalid or ballooning failed
 */
int intel_vgt_balloon(struct i915_ggtt *ggtt)
{
	struct intel_uncore *uncore = &ggtt->vm.i915->uncore;
	unsigned long ggtt_end = ggtt->vm.total;

	unsigned long mappable_base, mappable_size, mappable_end;
	unsigned long unmappable_base, unmappable_size, unmappable_end;
	int ret;

	if (!intel_vgpu_active(ggtt->vm.i915))
		return 0;

	mappable_base =
	  intel_uncore_read(uncore, vgtif_reg(avail_rs.mappable_gmadr.base));
	mappable_size =
	  intel_uncore_read(uncore, vgtif_reg(avail_rs.mappable_gmadr.size));
	unmappable_base =
	  intel_uncore_read(uncore, vgtif_reg(avail_rs.nonmappable_gmadr.base));
	unmappable_size =
	  intel_uncore_read(uncore, vgtif_reg(avail_rs.nonmappable_gmadr.size));

	mappable_end = mappable_base + mappable_size;
	unmappable_end = unmappable_base + unmappable_size;

	DRM_INFO("VGT ballooning configuration:\n");
	DRM_INFO("Mappable graphic memory: base 0x%lx size %ldKiB\n",
		 mappable_base, mappable_size / 1024);
	DRM_INFO("Unmappable graphic memory: base 0x%lx size %ldKiB\n",
		 unmappable_base, unmappable_size / 1024);

	if (mappable_end > ggtt->mappable_end ||
	    unmappable_base < ggtt->mappable_end ||
	    unmappable_end > ggtt_end) {
		DRM_ERROR("Invalid ballooning configuration!\n");
		return -EINVAL;
	}

	/* Unmappable graphic memory ballooning */
	if (unmappable_base > ggtt->mappable_end) {
		ret = vgt_balloon_space(ggtt, &bl_info.space[2],
					ggtt->mappable_end, unmappable_base);

		if (ret)
			goto err;
	}

	if (unmappable_end < ggtt_end) {
		ret = vgt_balloon_space(ggtt, &bl_info.space[3],
					unmappable_end, ggtt_end);
		if (ret)
			goto err_upon_mappable;
	}

	/* Mappable graphic memory ballooning */
	if (mappable_base) {
		ret = vgt_balloon_space(ggtt, &bl_info.space[0],
					0, mappable_base);

		if (ret)
			goto err_upon_unmappable;
	}

	if (mappable_end < ggtt->mappable_end) {
		ret = vgt_balloon_space(ggtt, &bl_info.space[1],
					mappable_end, ggtt->mappable_end);

		if (ret)
			goto err_below_mappable;
	}

	DRM_INFO("VGT balloon successfully\n");
	return 0;

err_below_mappable:
	vgt_deballoon_space(ggtt, &bl_info.space[0]);
err_upon_unmappable:
	vgt_deballoon_space(ggtt, &bl_info.space[3]);
err_upon_mappable:
	vgt_deballoon_space(ggtt, &bl_info.space[2]);
err:
	DRM_ERROR("VGT balloon fail\n");
	return ret;
}
