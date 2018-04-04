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
#include <linux/list_sort.h>
#include "i915_drv.h"
#include "gvt.h"

struct mmio_diff_param {
	struct intel_vgpu *vgpu;
	int total;
	int diff;
	struct list_head diff_mmio_list;
};

struct diff_mmio {
	struct list_head node;
	u32 offset;
	u32 preg;
	u32 vreg;
};

/* Compare two diff_mmio items. */
static int mmio_offset_compare(void *priv,
	struct list_head *a, struct list_head *b)
{
	struct diff_mmio *ma;
	struct diff_mmio *mb;

	ma = container_of(a, struct diff_mmio, node);
	mb = container_of(b, struct diff_mmio, node);
	if (ma->offset < mb->offset)
		return -1;
	else if (ma->offset > mb->offset)
		return 1;
	return 0;
}

static inline int mmio_diff_handler(struct intel_gvt *gvt,
				    u32 offset, void *data)
{
	struct drm_i915_private *dev_priv = gvt->dev_priv;
	struct mmio_diff_param *param = data;
	struct diff_mmio *node;
	u32 preg, vreg;

	preg = I915_READ_NOTRACE(_MMIO(offset));
	vreg = vgpu_vreg(param->vgpu, offset);

	if (preg != vreg) {
		node = kmalloc(sizeof(*node), GFP_KERNEL);
		if (!node)
			return -ENOMEM;

		node->offset = offset;
		node->preg = preg;
		node->vreg = vreg;
		list_add(&node->node, &param->diff_mmio_list);
		param->diff++;
	}
	param->total++;
	return 0;
}

/* Show the all the different values of tracked mmio. */
static int vgpu_mmio_diff_show(struct seq_file *s, void *unused)
{
	struct intel_vgpu *vgpu = s->private;
	struct intel_gvt *gvt = vgpu->gvt;
	struct mmio_diff_param param = {
		.vgpu = vgpu,
		.total = 0,
		.diff = 0,
	};
	struct diff_mmio *node, *next;

	INIT_LIST_HEAD(&param.diff_mmio_list);

	mutex_lock(&gvt->lock);
	spin_lock_bh(&gvt->scheduler.mmio_context_lock);

	mmio_hw_access_pre(gvt->dev_priv);
	/* Recognize all the diff mmios to list. */
	intel_gvt_for_each_tracked_mmio(gvt, mmio_diff_handler, &param);
	mmio_hw_access_post(gvt->dev_priv);

	spin_unlock_bh(&gvt->scheduler.mmio_context_lock);
	mutex_unlock(&gvt->lock);

	/* In an ascending order by mmio offset. */
	list_sort(NULL, &param.diff_mmio_list, mmio_offset_compare);

	seq_printf(s, "%-8s %-8s %-8s %-8s\n", "Offset", "HW", "vGPU", "Diff");
	list_for_each_entry_safe(node, next, &param.diff_mmio_list, node) {
		u32 diff = node->preg ^ node->vreg;

		seq_printf(s, "%08x %08x %08x %*pbl\n",
			   node->offset, node->preg, node->vreg,
			   32, &diff);
		list_del(&node->node);
		kfree(node);
	}
	seq_printf(s, "Total: %d, Diff: %d\n", param.total, param.diff);
	return 0;
}

static int vgpu_mmio_diff_open(struct inode *inode, struct file *file)
{
	return single_open(file, vgpu_mmio_diff_show, inode->i_private);
}

static const struct file_operations vgpu_mmio_diff_fops = {
	.open		= vgpu_mmio_diff_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

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

	ent = debugfs_create_file("mmio_diff", 0444, vgpu->debugfs,
				  vgpu, &vgpu_mmio_diff_fops);
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
