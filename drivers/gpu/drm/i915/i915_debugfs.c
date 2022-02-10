/*
 * Copyright © 2008 Intel Corporation
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *    Keith Packard <keithp@keithp.com>
 *
 */

#include <linux/sched/mm.h>
#include <linux/sort.h>

#include <drm/drm_debugfs.h>

#include "gem/i915_gem_context.h"
#include "gt/intel_gt.h"
#include "gt/intel_gt_buffer_pool.h"
#include "gt/intel_gt_clock_utils.h"
#include "gt/intel_gt_debugfs.h"
#include "gt/intel_gt_pm.h"
#include "gt/intel_gt_pm_debugfs.h"
#include "gt/intel_gt_regs.h"
#include "gt/intel_gt_requests.h"
#include "gt/intel_rc6.h"
#include "gt/intel_reset.h"
#include "gt/intel_rps.h"
#include "gt/intel_sseu_debugfs.h"

#include "i915_debugfs.h"
#include "i915_debugfs_params.h"
#include "i915_irq.h"
#include "i915_scheduler.h"
#include "intel_pm.h"

static inline struct drm_i915_private *node_to_i915(struct drm_info_node *node)
{
	return to_i915(node->minor->dev);
}

static int i915_capabilities(struct seq_file *m, void *data)
{
	struct drm_i915_private *i915 = node_to_i915(m->private);
	struct drm_printer p = drm_seq_file_printer(m);

	seq_printf(m, "pch: %d\n", INTEL_PCH_TYPE(i915));

	intel_device_info_print_static(INTEL_INFO(i915), &p);
	intel_device_info_print_runtime(RUNTIME_INFO(i915), &p);
	i915_print_iommu_status(i915, &p);
	intel_gt_info_print(&to_gt(i915)->info, &p);
	intel_driver_caps_print(&i915->caps, &p);

	kernel_param_lock(THIS_MODULE);
	i915_params_dump(&i915->params, &p);
	kernel_param_unlock(THIS_MODULE);

	return 0;
}

static char get_tiling_flag(struct drm_i915_gem_object *obj)
{
	switch (i915_gem_object_get_tiling(obj)) {
	default:
	case I915_TILING_NONE: return ' ';
	case I915_TILING_X: return 'X';
	case I915_TILING_Y: return 'Y';
	}
}

static char get_global_flag(struct drm_i915_gem_object *obj)
{
	return READ_ONCE(obj->userfault_count) ? 'g' : ' ';
}

static char get_pin_mapped_flag(struct drm_i915_gem_object *obj)
{
	return obj->mm.mapping ? 'M' : ' ';
}

static const char *
stringify_page_sizes(unsigned int page_sizes, char *buf, size_t len)
{
	size_t x = 0;

	switch (page_sizes) {
	case 0:
		return "";
	case I915_GTT_PAGE_SIZE_4K:
		return "4K";
	case I915_GTT_PAGE_SIZE_64K:
		return "64K";
	case I915_GTT_PAGE_SIZE_2M:
		return "2M";
	default:
		if (!buf)
			return "M";

		if (page_sizes & I915_GTT_PAGE_SIZE_2M)
			x += snprintf(buf + x, len - x, "2M, ");
		if (page_sizes & I915_GTT_PAGE_SIZE_64K)
			x += snprintf(buf + x, len - x, "64K, ");
		if (page_sizes & I915_GTT_PAGE_SIZE_4K)
			x += snprintf(buf + x, len - x, "4K, ");
		buf[x-2] = '\0';

		return buf;
	}
}

static const char *stringify_vma_type(const struct i915_vma *vma)
{
	if (i915_vma_is_ggtt(vma))
		return "ggtt";

	if (i915_vma_is_dpt(vma))
		return "dpt";

	return "ppgtt";
}

static const char *i915_cache_level_str(struct drm_i915_private *i915, int type)
{
	switch (type) {
	case I915_CACHE_NONE: return " uncached";
	case I915_CACHE_LLC: return HAS_LLC(i915) ? " LLC" : " snooped";
	case I915_CACHE_L3_LLC: return " L3+LLC";
	case I915_CACHE_WT: return " WT";
	default: return "";
	}
}

void
i915_debugfs_describe_obj(struct seq_file *m, struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *dev_priv = to_i915(obj->base.dev);
	struct i915_vma *vma;
	int pin_count = 0;

	seq_printf(m, "%pK: %c%c%c %8zdKiB %02x %02x %s%s%s",
		   &obj->base,
		   get_tiling_flag(obj),
		   get_global_flag(obj),
		   get_pin_mapped_flag(obj),
		   obj->base.size / 1024,
		   obj->read_domains,
		   obj->write_domain,
		   i915_cache_level_str(dev_priv, obj->cache_level),
		   obj->mm.dirty ? " dirty" : "",
		   obj->mm.madv == I915_MADV_DONTNEED ? " purgeable" : "");
	if (obj->base.name)
		seq_printf(m, " (name: %d)", obj->base.name);

	spin_lock(&obj->vma.lock);
	list_for_each_entry(vma, &obj->vma.list, obj_link) {
		if (!drm_mm_node_allocated(&vma->node))
			continue;

		spin_unlock(&obj->vma.lock);

		if (i915_vma_is_pinned(vma))
			pin_count++;

		seq_printf(m, " (%s offset: %08llx, size: %08llx, pages: %s",
			   stringify_vma_type(vma),
			   vma->node.start, vma->node.size,
			   stringify_page_sizes(vma->page_sizes.gtt, NULL, 0));
		if (i915_vma_is_ggtt(vma) || i915_vma_is_dpt(vma)) {
			switch (vma->ggtt_view.type) {
			case I915_GGTT_VIEW_NORMAL:
				seq_puts(m, ", normal");
				break;

			case I915_GGTT_VIEW_PARTIAL:
				seq_printf(m, ", partial [%08llx+%x]",
					   vma->ggtt_view.partial.offset << PAGE_SHIFT,
					   vma->ggtt_view.partial.size << PAGE_SHIFT);
				break;

			case I915_GGTT_VIEW_ROTATED:
				seq_printf(m, ", rotated [(%ux%u, src_stride=%u, dst_stride=%u, offset=%u), (%ux%u, src_stride=%u, dst_stride=%u, offset=%u)]",
					   vma->ggtt_view.rotated.plane[0].width,
					   vma->ggtt_view.rotated.plane[0].height,
					   vma->ggtt_view.rotated.plane[0].src_stride,
					   vma->ggtt_view.rotated.plane[0].dst_stride,
					   vma->ggtt_view.rotated.plane[0].offset,
					   vma->ggtt_view.rotated.plane[1].width,
					   vma->ggtt_view.rotated.plane[1].height,
					   vma->ggtt_view.rotated.plane[1].src_stride,
					   vma->ggtt_view.rotated.plane[1].dst_stride,
					   vma->ggtt_view.rotated.plane[1].offset);
				break;

			case I915_GGTT_VIEW_REMAPPED:
				seq_printf(m, ", remapped [(%ux%u, src_stride=%u, dst_stride=%u, offset=%u), (%ux%u, src_stride=%u, dst_stride=%u, offset=%u)]",
					   vma->ggtt_view.remapped.plane[0].width,
					   vma->ggtt_view.remapped.plane[0].height,
					   vma->ggtt_view.remapped.plane[0].src_stride,
					   vma->ggtt_view.remapped.plane[0].dst_stride,
					   vma->ggtt_view.remapped.plane[0].offset,
					   vma->ggtt_view.remapped.plane[1].width,
					   vma->ggtt_view.remapped.plane[1].height,
					   vma->ggtt_view.remapped.plane[1].src_stride,
					   vma->ggtt_view.remapped.plane[1].dst_stride,
					   vma->ggtt_view.remapped.plane[1].offset);
				break;

			default:
				MISSING_CASE(vma->ggtt_view.type);
				break;
			}
		}
		if (vma->fence)
			seq_printf(m, " , fence: %d", vma->fence->id);
		seq_puts(m, ")");

		spin_lock(&obj->vma.lock);
	}
	spin_unlock(&obj->vma.lock);

	seq_printf(m, " (pinned x %d)", pin_count);
	if (i915_gem_object_is_stolen(obj))
		seq_printf(m, " (stolen: %08llx)", obj->stolen->start);
	if (i915_gem_object_is_framebuffer(obj))
		seq_printf(m, " (fb)");
}

static int i915_gem_object_info(struct seq_file *m, void *data)
{
	struct drm_i915_private *i915 = node_to_i915(m->private);
	struct drm_printer p = drm_seq_file_printer(m);
	struct intel_memory_region *mr;
	enum intel_region_id id;

	seq_printf(m, "%u shrinkable [%u free] objects, %llu bytes\n",
		   i915->mm.shrink_count,
		   atomic_read(&i915->mm.free_count),
		   i915->mm.shrink_memory);
	for_each_memory_region(mr, i915, id)
		intel_memory_region_debug(mr, &p);

	return 0;
}

#if IS_ENABLED(CONFIG_DRM_I915_CAPTURE_ERROR)
static ssize_t gpu_state_read(struct file *file, char __user *ubuf,
			      size_t count, loff_t *pos)
{
	struct i915_gpu_coredump *error;
	ssize_t ret;
	void *buf;

	error = file->private_data;
	if (!error)
		return 0;

	/* Bounce buffer required because of kernfs __user API convenience. */
	buf = kmalloc(count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = i915_gpu_coredump_copy_to_buffer(error, buf, *pos, count);
	if (ret <= 0)
		goto out;

	if (!copy_to_user(ubuf, buf, ret))
		*pos += ret;
	else
		ret = -EFAULT;

out:
	kfree(buf);
	return ret;
}

static int gpu_state_release(struct inode *inode, struct file *file)
{
	i915_gpu_coredump_put(file->private_data);
	return 0;
}

static int i915_gpu_info_open(struct inode *inode, struct file *file)
{
	struct drm_i915_private *i915 = inode->i_private;
	struct i915_gpu_coredump *gpu;
	intel_wakeref_t wakeref;

	gpu = NULL;
	with_intel_runtime_pm(&i915->runtime_pm, wakeref)
		gpu = i915_gpu_coredump(to_gt(i915), ALL_ENGINES);
	if (IS_ERR(gpu))
		return PTR_ERR(gpu);

	file->private_data = gpu;
	return 0;
}

static const struct file_operations i915_gpu_info_fops = {
	.owner = THIS_MODULE,
	.open = i915_gpu_info_open,
	.read = gpu_state_read,
	.llseek = default_llseek,
	.release = gpu_state_release,
};

static ssize_t
i915_error_state_write(struct file *filp,
		       const char __user *ubuf,
		       size_t cnt,
		       loff_t *ppos)
{
	struct i915_gpu_coredump *error = filp->private_data;

	if (!error)
		return 0;

	drm_dbg(&error->i915->drm, "Resetting error state\n");
	i915_reset_error_state(error->i915);

	return cnt;
}

static int i915_error_state_open(struct inode *inode, struct file *file)
{
	struct i915_gpu_coredump *error;

	error = i915_first_error_state(inode->i_private);
	if (IS_ERR(error))
		return PTR_ERR(error);

	file->private_data  = error;
	return 0;
}

static const struct file_operations i915_error_state_fops = {
	.owner = THIS_MODULE,
	.open = i915_error_state_open,
	.read = gpu_state_read,
	.write = i915_error_state_write,
	.llseek = default_llseek,
	.release = gpu_state_release,
};
#endif

static int i915_frequency_info(struct seq_file *m, void *unused)
{
	struct drm_i915_private *i915 = node_to_i915(m->private);
	struct intel_gt *gt = to_gt(i915);
	struct drm_printer p = drm_seq_file_printer(m);

	intel_gt_pm_frequency_dump(gt, &p);

	return 0;
}

static const char *swizzle_string(unsigned swizzle)
{
	switch (swizzle) {
	case I915_BIT_6_SWIZZLE_NONE:
		return "none";
	case I915_BIT_6_SWIZZLE_9:
		return "bit9";
	case I915_BIT_6_SWIZZLE_9_10:
		return "bit9/bit10";
	case I915_BIT_6_SWIZZLE_9_11:
		return "bit9/bit11";
	case I915_BIT_6_SWIZZLE_9_10_11:
		return "bit9/bit10/bit11";
	case I915_BIT_6_SWIZZLE_9_17:
		return "bit9/bit17";
	case I915_BIT_6_SWIZZLE_9_10_17:
		return "bit9/bit10/bit17";
	case I915_BIT_6_SWIZZLE_UNKNOWN:
		return "unknown";
	}

	return "bug";
}

static int i915_swizzle_info(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct intel_uncore *uncore = &dev_priv->uncore;
	intel_wakeref_t wakeref;

	seq_printf(m, "bit6 swizzle for X-tiling = %s\n",
		   swizzle_string(dev_priv->ggtt.bit_6_swizzle_x));
	seq_printf(m, "bit6 swizzle for Y-tiling = %s\n",
		   swizzle_string(dev_priv->ggtt.bit_6_swizzle_y));

	if (dev_priv->quirks & QUIRK_PIN_SWIZZLED_PAGES)
		seq_puts(m, "L-shaped memory detected\n");

	/* On BDW+, swizzling is not used. See detect_bit_6_swizzle() */
	if (GRAPHICS_VER(dev_priv) >= 8 || IS_VALLEYVIEW(dev_priv))
		return 0;

	wakeref = intel_runtime_pm_get(&dev_priv->runtime_pm);

	if (IS_GRAPHICS_VER(dev_priv, 3, 4)) {
		seq_printf(m, "DDC = 0x%08x\n",
			   intel_uncore_read(uncore, DCC));
		seq_printf(m, "DDC2 = 0x%08x\n",
			   intel_uncore_read(uncore, DCC2));
		seq_printf(m, "C0DRB3 = 0x%04x\n",
			   intel_uncore_read16(uncore, C0DRB3_BW));
		seq_printf(m, "C1DRB3 = 0x%04x\n",
			   intel_uncore_read16(uncore, C1DRB3_BW));
	} else if (GRAPHICS_VER(dev_priv) >= 6) {
		seq_printf(m, "MAD_DIMM_C0 = 0x%08x\n",
			   intel_uncore_read(uncore, MAD_DIMM_C0));
		seq_printf(m, "MAD_DIMM_C1 = 0x%08x\n",
			   intel_uncore_read(uncore, MAD_DIMM_C1));
		seq_printf(m, "MAD_DIMM_C2 = 0x%08x\n",
			   intel_uncore_read(uncore, MAD_DIMM_C2));
		seq_printf(m, "TILECTL = 0x%08x\n",
			   intel_uncore_read(uncore, TILECTL));
		if (GRAPHICS_VER(dev_priv) >= 8)
			seq_printf(m, "GAMTARBMODE = 0x%08x\n",
				   intel_uncore_read(uncore, GAMTARBMODE));
		else
			seq_printf(m, "ARB_MODE = 0x%08x\n",
				   intel_uncore_read(uncore, ARB_MODE));
		seq_printf(m, "DISP_ARB_CTL = 0x%08x\n",
			   intel_uncore_read(uncore, DISP_ARB_CTL));
	}

	intel_runtime_pm_put(&dev_priv->runtime_pm, wakeref);

	return 0;
}

static int i915_rps_boost_info(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct intel_rps *rps = &to_gt(dev_priv)->rps;

	seq_printf(m, "RPS enabled? %s\n", yesno(intel_rps_is_enabled(rps)));
	seq_printf(m, "RPS active? %s\n", yesno(intel_rps_is_active(rps)));
	seq_printf(m, "GPU busy? %s\n", yesno(to_gt(dev_priv)->awake));
	seq_printf(m, "Boosts outstanding? %d\n",
		   atomic_read(&rps->num_waiters));
	seq_printf(m, "Interactive? %d\n", READ_ONCE(rps->power.interactive));
	seq_printf(m, "Frequency requested %d, actual %d\n",
		   intel_gpu_freq(rps, rps->cur_freq),
		   intel_rps_read_actual_frequency(rps));
	seq_printf(m, "  min hard:%d, soft:%d; max soft:%d, hard:%d\n",
		   intel_gpu_freq(rps, rps->min_freq),
		   intel_gpu_freq(rps, rps->min_freq_softlimit),
		   intel_gpu_freq(rps, rps->max_freq_softlimit),
		   intel_gpu_freq(rps, rps->max_freq));
	seq_printf(m, "  idle:%d, efficient:%d, boost:%d\n",
		   intel_gpu_freq(rps, rps->idle_freq),
		   intel_gpu_freq(rps, rps->efficient_freq),
		   intel_gpu_freq(rps, rps->boost_freq));

	seq_printf(m, "Wait boosts: %d\n", READ_ONCE(rps->boosts));

	return 0;
}

static int i915_runtime_pm_status(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct pci_dev *pdev = to_pci_dev(dev_priv->drm.dev);

	if (!HAS_RUNTIME_PM(dev_priv))
		seq_puts(m, "Runtime power management not supported\n");

	seq_printf(m, "Runtime power status: %s\n",
		   enableddisabled(!dev_priv->power_domains.init_wakeref));

	seq_printf(m, "GPU idle: %s\n", yesno(!to_gt(dev_priv)->awake));
	seq_printf(m, "IRQs disabled: %s\n",
		   yesno(!intel_irqs_enabled(dev_priv)));
#ifdef CONFIG_PM
	seq_printf(m, "Usage count: %d\n",
		   atomic_read(&dev_priv->drm.dev->power.usage_count));
#else
	seq_printf(m, "Device Power Management (CONFIG_PM) disabled\n");
#endif
	seq_printf(m, "PCI device power state: %s [%d]\n",
		   pci_power_name(pdev->current_state),
		   pdev->current_state);

	if (IS_ENABLED(CONFIG_DRM_I915_DEBUG_RUNTIME_PM)) {
		struct drm_printer p = drm_seq_file_printer(m);

		print_intel_runtime_pm_wakeref(&dev_priv->runtime_pm, &p);
	}

	return 0;
}

static int i915_engine_info(struct seq_file *m, void *unused)
{
	struct drm_i915_private *i915 = node_to_i915(m->private);
	struct intel_engine_cs *engine;
	intel_wakeref_t wakeref;
	struct drm_printer p;

	wakeref = intel_runtime_pm_get(&i915->runtime_pm);

	seq_printf(m, "GT awake? %s [%d], %llums\n",
		   yesno(to_gt(i915)->awake),
		   atomic_read(&to_gt(i915)->wakeref.count),
		   ktime_to_ms(intel_gt_get_awake_time(to_gt(i915))));
	seq_printf(m, "CS timestamp frequency: %u Hz, %d ns\n",
		   to_gt(i915)->clock_frequency,
		   to_gt(i915)->clock_period_ns);

	p = drm_seq_file_printer(m);
	for_each_uabi_engine(engine, i915)
		intel_engine_dump(engine, &p, "%s\n", engine->name);

	intel_gt_show_timelines(to_gt(i915), &p, i915_request_show_with_schedule);

	intel_runtime_pm_put(&i915->runtime_pm, wakeref);

	return 0;
}

static int i915_wa_registers(struct seq_file *m, void *unused)
{
	struct drm_i915_private *i915 = node_to_i915(m->private);
	struct intel_engine_cs *engine;

	for_each_uabi_engine(engine, i915) {
		const struct i915_wa_list *wal = &engine->ctx_wa_list;
		const struct i915_wa *wa;
		unsigned int count;

		count = wal->count;
		if (!count)
			continue;

		seq_printf(m, "%s: Workarounds applied: %u\n",
			   engine->name, count);

		for (wa = wal->list; count--; wa++)
			seq_printf(m, "0x%X: 0x%08X, mask: 0x%08X\n",
				   i915_mmio_reg_offset(wa->reg),
				   wa->set, wa->clr);

		seq_printf(m, "\n");
	}

	return 0;
}

static int i915_wedged_get(void *data, u64 *val)
{
	struct drm_i915_private *i915 = data;

	return intel_gt_debugfs_reset_show(to_gt(i915), val);
}

static int i915_wedged_set(void *data, u64 val)
{
	struct drm_i915_private *i915 = data;

	return intel_gt_debugfs_reset_store(to_gt(i915), val);
}

DEFINE_SIMPLE_ATTRIBUTE(i915_wedged_fops,
			i915_wedged_get, i915_wedged_set,
			"%llu\n");

static int
i915_perf_noa_delay_set(void *data, u64 val)
{
	struct drm_i915_private *i915 = data;

	/*
	 * This would lead to infinite waits as we're doing timestamp
	 * difference on the CS with only 32bits.
	 */
	if (intel_gt_ns_to_clock_interval(to_gt(i915), val) > U32_MAX)
		return -EINVAL;

	atomic64_set(&i915->perf.noa_programming_delay, val);
	return 0;
}

static int
i915_perf_noa_delay_get(void *data, u64 *val)
{
	struct drm_i915_private *i915 = data;

	*val = atomic64_read(&i915->perf.noa_programming_delay);
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(i915_perf_noa_delay_fops,
			i915_perf_noa_delay_get,
			i915_perf_noa_delay_set,
			"%llu\n");

#define DROP_UNBOUND	BIT(0)
#define DROP_BOUND	BIT(1)
#define DROP_RETIRE	BIT(2)
#define DROP_ACTIVE	BIT(3)
#define DROP_FREED	BIT(4)
#define DROP_SHRINK_ALL	BIT(5)
#define DROP_IDLE	BIT(6)
#define DROP_RESET_ACTIVE	BIT(7)
#define DROP_RESET_SEQNO	BIT(8)
#define DROP_RCU	BIT(9)
#define DROP_ALL (DROP_UNBOUND	| \
		  DROP_BOUND	| \
		  DROP_RETIRE	| \
		  DROP_ACTIVE	| \
		  DROP_FREED	| \
		  DROP_SHRINK_ALL |\
		  DROP_IDLE	| \
		  DROP_RESET_ACTIVE | \
		  DROP_RESET_SEQNO | \
		  DROP_RCU)
static int
i915_drop_caches_get(void *data, u64 *val)
{
	*val = DROP_ALL;

	return 0;
}
static int
gt_drop_caches(struct intel_gt *gt, u64 val)
{
	int ret;

	if (val & DROP_RESET_ACTIVE &&
	    wait_for(intel_engines_are_idle(gt), I915_IDLE_ENGINES_TIMEOUT))
		intel_gt_set_wedged(gt);

	if (val & DROP_RETIRE)
		intel_gt_retire_requests(gt);

	if (val & (DROP_IDLE | DROP_ACTIVE)) {
		ret = intel_gt_wait_for_idle(gt, MAX_SCHEDULE_TIMEOUT);
		if (ret)
			return ret;
	}

	if (val & DROP_IDLE) {
		ret = intel_gt_pm_wait_for_idle(gt);
		if (ret)
			return ret;
	}

	if (val & DROP_RESET_ACTIVE && intel_gt_terminally_wedged(gt))
		intel_gt_handle_error(gt, ALL_ENGINES, 0, NULL);

	if (val & DROP_FREED)
		intel_gt_flush_buffer_pool(gt);

	return 0;
}

static int
i915_drop_caches_set(void *data, u64 val)
{
	struct drm_i915_private *i915 = data;
	unsigned int flags;
	int ret;

	DRM_DEBUG("Dropping caches: 0x%08llx [0x%08llx]\n",
		  val, val & DROP_ALL);

	ret = gt_drop_caches(to_gt(i915), val);
	if (ret)
		return ret;

	fs_reclaim_acquire(GFP_KERNEL);
	flags = memalloc_noreclaim_save();
	if (val & DROP_BOUND)
		i915_gem_shrink(NULL, i915, LONG_MAX, NULL, I915_SHRINK_BOUND);

	if (val & DROP_UNBOUND)
		i915_gem_shrink(NULL, i915, LONG_MAX, NULL, I915_SHRINK_UNBOUND);

	if (val & DROP_SHRINK_ALL)
		i915_gem_shrink_all(i915);
	memalloc_noreclaim_restore(flags);
	fs_reclaim_release(GFP_KERNEL);

	if (val & DROP_RCU)
		rcu_barrier();

	if (val & DROP_FREED)
		i915_gem_drain_freed_objects(i915);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(i915_drop_caches_fops,
			i915_drop_caches_get, i915_drop_caches_set,
			"0x%08llx\n");

static int i915_sseu_status(struct seq_file *m, void *unused)
{
	struct drm_i915_private *i915 = node_to_i915(m->private);
	struct intel_gt *gt = to_gt(i915);

	return intel_sseu_status(m, gt);
}

static int i915_forcewake_open(struct inode *inode, struct file *file)
{
	struct drm_i915_private *i915 = inode->i_private;

	return intel_gt_pm_debugfs_forcewake_user_open(to_gt(i915));
}

static int i915_forcewake_release(struct inode *inode, struct file *file)
{
	struct drm_i915_private *i915 = inode->i_private;

	return intel_gt_pm_debugfs_forcewake_user_release(to_gt(i915));
}

static const struct file_operations i915_forcewake_fops = {
	.owner = THIS_MODULE,
	.open = i915_forcewake_open,
	.release = i915_forcewake_release,
};

static const struct drm_info_list i915_debugfs_list[] = {
	{"i915_capabilities", i915_capabilities, 0},
	{"i915_gem_objects", i915_gem_object_info, 0},
	{"i915_frequency_info", i915_frequency_info, 0},
	{"i915_swizzle_info", i915_swizzle_info, 0},
	{"i915_runtime_pm_status", i915_runtime_pm_status, 0},
	{"i915_engine_info", i915_engine_info, 0},
	{"i915_wa_registers", i915_wa_registers, 0},
	{"i915_sseu_status", i915_sseu_status, 0},
	{"i915_rps_boost_info", i915_rps_boost_info, 0},
};
#define I915_DEBUGFS_ENTRIES ARRAY_SIZE(i915_debugfs_list)

static const struct i915_debugfs_files {
	const char *name;
	const struct file_operations *fops;
} i915_debugfs_files[] = {
	{"i915_perf_noa_delay", &i915_perf_noa_delay_fops},
	{"i915_wedged", &i915_wedged_fops},
	{"i915_gem_drop_caches", &i915_drop_caches_fops},
#if IS_ENABLED(CONFIG_DRM_I915_CAPTURE_ERROR)
	{"i915_error_state", &i915_error_state_fops},
	{"i915_gpu_info", &i915_gpu_info_fops},
#endif
};

void i915_debugfs_register(struct drm_i915_private *dev_priv)
{
	struct drm_minor *minor = dev_priv->drm.primary;
	int i;

	i915_debugfs_params(dev_priv);

	debugfs_create_file("i915_forcewake_user", S_IRUSR, minor->debugfs_root,
			    to_i915(minor->dev), &i915_forcewake_fops);
	for (i = 0; i < ARRAY_SIZE(i915_debugfs_files); i++) {
		debugfs_create_file(i915_debugfs_files[i].name,
				    S_IRUGO | S_IWUSR,
				    minor->debugfs_root,
				    to_i915(minor->dev),
				    i915_debugfs_files[i].fops);
	}

	drm_debugfs_create_files(i915_debugfs_list,
				 I915_DEBUGFS_ENTRIES,
				 minor->debugfs_root, minor);
}
