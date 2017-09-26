/*
 * Copyright(c) 2011-2017 Intel Corporation. All rights reserved.
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
#include <linux/debugfs.h>
#include "i915_drv.h"
#include "gvt.h"


/**
 * intel_gvt_debugfs_add_vgpu - register debugfs entries for a vGPU
 * @vgpu: a vGPU
 *
 * Returns:
 * Zero on success, negative error code if failed.
 */
int intel_gvt_debugfs_add_vgpu(struct intel_vgpu *vgpu)
{
	struct dentry *ent;
	char name[10] = "";

	sprintf(name, "vgpu%d", vgpu->id);
	vgpu->debugfs = debugfs_create_dir(name, vgpu->gvt->debugfs_root);
	if (!vgpu->debugfs)
		return -ENOMEM;

	ent = debugfs_create_bool("active", 0444, vgpu->debugfs,
				  &vgpu->active);
	if (!ent)
		return -ENOMEM;

	return 0;
}

/**
 * intel_gvt_debugfs_remove_vgpu - remove debugfs entries of a vGPU
 * @vgpu: a vGPU
 */
void intel_gvt_debugfs_remove_vgpu(struct intel_vgpu *vgpu)
{
	debugfs_remove_recursive(vgpu->debugfs);
	vgpu->debugfs = NULL;
}

/**
 * intel_gvt_debugfs_init - register gvt debugfs root entry
 * @gvt: GVT device
 *
 * Returns:
 * zero on success, negative if failed.
 */
int intel_gvt_debugfs_init(struct intel_gvt *gvt)
{
	struct drm_minor *minor = gvt->dev_priv->drm.primary;
	struct dentry *ent;

	gvt->debugfs_root = debugfs_create_dir("gvt", minor->debugfs_root);
	if (!gvt->debugfs_root) {
		gvt_err("Cannot create debugfs dir\n");
		return -ENOMEM;
	}

	ent = debugfs_create_ulong("num_tracked_mmio", 0444, gvt->debugfs_root,
				   &gvt->mmio.num_tracked_mmio);
	if (!ent)
		return -ENOMEM;

	return 0;
}

/**
 * intel_gvt_debugfs_clean - remove debugfs entries
 * @gvt: GVT device
 */
void intel_gvt_debugfs_clean(struct intel_gvt *gvt)
{
	debugfs_remove_recursive(gvt->debugfs_root);
	gvt->debugfs_root = NULL;
}
