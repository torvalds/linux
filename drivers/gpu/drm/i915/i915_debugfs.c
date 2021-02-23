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
#include "gt/intel_gt_buffer_pool.h"
#include "gt/intel_gt_clock_utils.h"
#include "gt/intel_gt.h"
#include "gt/intel_gt_pm.h"
#include "gt/intel_gt_requests.h"
#include "gt/intel_reset.h"
#include "gt/intel_rc6.h"
#include "gt/intel_rps.h"
#include "gt/intel_sseu_debugfs.h"

#include "i915_debugfs.h"
#include "i915_debugfs_params.h"
#include "i915_irq.h"
#include "i915_trace.h"
#include "intel_pm.h"
#include "intel_sideband.h"

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
	intel_gt_info_print(&i915->gt.info, &p);
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

void
i915_debugfs_describe_obj(struct seq_file *m, struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *dev_priv = to_i915(obj->base.dev);
	struct intel_engine_cs *engine;
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

		seq_printf(m, " (%sgtt offset: %08llx, size: %08llx, pages: %s",
			   i915_vma_is_ggtt(vma) ? "g" : "pp",
			   vma->node.start, vma->node.size,
			   stringify_page_sizes(vma->page_sizes.gtt, NULL, 0));
		if (i915_vma_is_ggtt(vma)) {
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
				seq_printf(m, ", rotated [(%ux%u, stride=%u, offset=%u), (%ux%u, stride=%u, offset=%u)]",
					   vma->ggtt_view.rotated.plane[0].width,
					   vma->ggtt_view.rotated.plane[0].height,
					   vma->ggtt_view.rotated.plane[0].stride,
					   vma->ggtt_view.rotated.plane[0].offset,
					   vma->ggtt_view.rotated.plane[1].width,
					   vma->ggtt_view.rotated.plane[1].height,
					   vma->ggtt_view.rotated.plane[1].stride,
					   vma->ggtt_view.rotated.plane[1].offset);
				break;

			case I915_GGTT_VIEW_REMAPPED:
				seq_printf(m, ", remapped [(%ux%u, stride=%u, offset=%u), (%ux%u, stride=%u, offset=%u)]",
					   vma->ggtt_view.remapped.plane[0].width,
					   vma->ggtt_view.remapped.plane[0].height,
					   vma->ggtt_view.remapped.plane[0].stride,
					   vma->ggtt_view.remapped.plane[0].offset,
					   vma->ggtt_view.remapped.plane[1].width,
					   vma->ggtt_view.remapped.plane[1].height,
					   vma->ggtt_view.remapped.plane[1].stride,
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
	if (obj->stolen)
		seq_printf(m, " (stolen: %08llx)", obj->stolen->start);
	if (i915_gem_object_is_framebuffer(obj))
		seq_printf(m, " (fb)");

	engine = i915_gem_object_last_write_engine(obj);
	if (engine)
		seq_printf(m, " (%s)", engine->name);
}

struct file_stats {
	struct i915_address_space *vm;
	unsigned long count;
	u64 total;
	u64 active, inactive;
	u64 closed;
};

static int per_file_stats(int id, void *ptr, void *data)
{
	struct drm_i915_gem_object *obj = ptr;
	struct file_stats *stats = data;
	struct i915_vma *vma;

	if (IS_ERR_OR_NULL(obj) || !kref_get_unless_zero(&obj->base.refcount))
		return 0;

	stats->count++;
	stats->total += obj->base.size;

	spin_lock(&obj->vma.lock);
	if (!stats->vm) {
		for_each_ggtt_vma(vma, obj) {
			if (!drm_mm_node_allocated(&vma->node))
				continue;

			if (i915_vma_is_active(vma))
				stats->active += vma->node.size;
			else
				stats->inactive += vma->node.size;

			if (i915_vma_is_closed(vma))
				stats->closed += vma->node.size;
		}
	} else {
		struct rb_node *p = obj->vma.tree.rb_node;

		while (p) {
			long cmp;

			vma = rb_entry(p, typeof(*vma), obj_node);
			cmp = i915_vma_compare(vma, stats->vm, NULL);
			if (cmp == 0) {
				if (drm_mm_node_allocated(&vma->node)) {
					if (i915_vma_is_active(vma))
						stats->active += vma->node.size;
					else
						stats->inactive += vma->node.size;

					if (i915_vma_is_closed(vma))
						stats->closed += vma->node.size;
				}
				break;
			}
			if (cmp < 0)
				p = p->rb_right;
			else
				p = p->rb_left;
		}
	}
	spin_unlock(&obj->vma.lock);

	i915_gem_object_put(obj);
	return 0;
}

#define print_file_stats(m, name, stats) do { \
	if (stats.count) \
		seq_printf(m, "%s: %lu objects, %llu bytes (%llu active, %llu inactive, %llu closed)\n", \
			   name, \
			   stats.count, \
			   stats.total, \
			   stats.active, \
			   stats.inactive, \
			   stats.closed); \
} while (0)

static void print_context_stats(struct seq_file *m,
				struct drm_i915_private *i915)
{
	struct file_stats kstats = {};
	struct i915_gem_context *ctx, *cn;

	spin_lock(&i915->gem.contexts.lock);
	list_for_each_entry_safe(ctx, cn, &i915->gem.contexts.list, link) {
		struct i915_gem_engines_iter it;
		struct intel_context *ce;

		if (!kref_get_unless_zero(&ctx->ref))
			continue;

		spin_unlock(&i915->gem.contexts.lock);

		for_each_gem_engine(ce,
				    i915_gem_context_lock_engines(ctx), it) {
			if (intel_context_pin_if_active(ce)) {
				rcu_read_lock();
				if (ce->state)
					per_file_stats(0,
						       ce->state->obj, &kstats);
				per_file_stats(0, ce->ring->vma->obj, &kstats);
				rcu_read_unlock();
				intel_context_unpin(ce);
			}
		}
		i915_gem_context_unlock_engines(ctx);

		mutex_lock(&ctx->mutex);
		if (!IS_ERR_OR_NULL(ctx->file_priv)) {
			struct file_stats stats = {
				.vm = rcu_access_pointer(ctx->vm),
			};
			struct drm_file *file = ctx->file_priv->file;
			struct task_struct *task;
			char name[80];

			rcu_read_lock();
			idr_for_each(&file->object_idr, per_file_stats, &stats);
			rcu_read_unlock();

			rcu_read_lock();
			task = pid_task(ctx->pid ?: file->pid, PIDTYPE_PID);
			snprintf(name, sizeof(name), "%s",
				 task ? task->comm : "<unknown>");
			rcu_read_unlock();

			print_file_stats(m, name, stats);
		}
		mutex_unlock(&ctx->mutex);

		spin_lock(&i915->gem.contexts.lock);
		list_safe_reset_next(ctx, cn, link);
		i915_gem_context_put(ctx);
	}
	spin_unlock(&i915->gem.contexts.lock);

	print_file_stats(m, "[k]contexts", kstats);
}

static int i915_gem_object_info(struct seq_file *m, void *data)
{
	struct drm_i915_private *i915 = node_to_i915(m->private);
	struct intel_memory_region *mr;
	enum intel_region_id id;

	seq_printf(m, "%u shrinkable [%u free] objects, %llu bytes\n",
		   i915->mm.shrink_count,
		   atomic_read(&i915->mm.free_count),
		   i915->mm.shrink_memory);
	for_each_memory_region(mr, i915, id)
		seq_printf(m, "%s: total:%pa, available:%pa bytes\n",
			   mr->name, &mr->total, &mr->avail);
	seq_putc(m, '\n');

	print_context_stats(m, i915);

	return 0;
}

static void gen8_display_interrupt_info(struct seq_file *m)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	enum pipe pipe;

	for_each_pipe(dev_priv, pipe) {
		enum intel_display_power_domain power_domain;
		intel_wakeref_t wakeref;

		power_domain = POWER_DOMAIN_PIPE(pipe);
		wakeref = intel_display_power_get_if_enabled(dev_priv,
							     power_domain);
		if (!wakeref) {
			seq_printf(m, "Pipe %c power disabled\n",
				   pipe_name(pipe));
			continue;
		}
		seq_printf(m, "Pipe %c IMR:\t%08x\n",
			   pipe_name(pipe),
			   I915_READ(GEN8_DE_PIPE_IMR(pipe)));
		seq_printf(m, "Pipe %c IIR:\t%08x\n",
			   pipe_name(pipe),
			   I915_READ(GEN8_DE_PIPE_IIR(pipe)));
		seq_printf(m, "Pipe %c IER:\t%08x\n",
			   pipe_name(pipe),
			   I915_READ(GEN8_DE_PIPE_IER(pipe)));

		intel_display_power_put(dev_priv, power_domain, wakeref);
	}

	seq_printf(m, "Display Engine port interrupt mask:\t%08x\n",
		   I915_READ(GEN8_DE_PORT_IMR));
	seq_printf(m, "Display Engine port interrupt identity:\t%08x\n",
		   I915_READ(GEN8_DE_PORT_IIR));
	seq_printf(m, "Display Engine port interrupt enable:\t%08x\n",
		   I915_READ(GEN8_DE_PORT_IER));

	seq_printf(m, "Display Engine misc interrupt mask:\t%08x\n",
		   I915_READ(GEN8_DE_MISC_IMR));
	seq_printf(m, "Display Engine misc interrupt identity:\t%08x\n",
		   I915_READ(GEN8_DE_MISC_IIR));
	seq_printf(m, "Display Engine misc interrupt enable:\t%08x\n",
		   I915_READ(GEN8_DE_MISC_IER));

	seq_printf(m, "PCU interrupt mask:\t%08x\n",
		   I915_READ(GEN8_PCU_IMR));
	seq_printf(m, "PCU interrupt identity:\t%08x\n",
		   I915_READ(GEN8_PCU_IIR));
	seq_printf(m, "PCU interrupt enable:\t%08x\n",
		   I915_READ(GEN8_PCU_IER));
}

static int i915_interrupt_info(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct intel_engine_cs *engine;
	intel_wakeref_t wakeref;
	int i, pipe;

	wakeref = intel_runtime_pm_get(&dev_priv->runtime_pm);

	if (IS_CHERRYVIEW(dev_priv)) {
		intel_wakeref_t pref;

		seq_printf(m, "Master Interrupt Control:\t%08x\n",
			   I915_READ(GEN8_MASTER_IRQ));

		seq_printf(m, "Display IER:\t%08x\n",
			   I915_READ(VLV_IER));
		seq_printf(m, "Display IIR:\t%08x\n",
			   I915_READ(VLV_IIR));
		seq_printf(m, "Display IIR_RW:\t%08x\n",
			   I915_READ(VLV_IIR_RW));
		seq_printf(m, "Display IMR:\t%08x\n",
			   I915_READ(VLV_IMR));
		for_each_pipe(dev_priv, pipe) {
			enum intel_display_power_domain power_domain;

			power_domain = POWER_DOMAIN_PIPE(pipe);
			pref = intel_display_power_get_if_enabled(dev_priv,
								  power_domain);
			if (!pref) {
				seq_printf(m, "Pipe %c power disabled\n",
					   pipe_name(pipe));
				continue;
			}

			seq_printf(m, "Pipe %c stat:\t%08x\n",
				   pipe_name(pipe),
				   I915_READ(PIPESTAT(pipe)));

			intel_display_power_put(dev_priv, power_domain, pref);
		}

		pref = intel_display_power_get(dev_priv, POWER_DOMAIN_INIT);
		seq_printf(m, "Port hotplug:\t%08x\n",
			   I915_READ(PORT_HOTPLUG_EN));
		seq_printf(m, "DPFLIPSTAT:\t%08x\n",
			   I915_READ(VLV_DPFLIPSTAT));
		seq_printf(m, "DPINVGTT:\t%08x\n",
			   I915_READ(DPINVGTT));
		intel_display_power_put(dev_priv, POWER_DOMAIN_INIT, pref);

		for (i = 0; i < 4; i++) {
			seq_printf(m, "GT Interrupt IMR %d:\t%08x\n",
				   i, I915_READ(GEN8_GT_IMR(i)));
			seq_printf(m, "GT Interrupt IIR %d:\t%08x\n",
				   i, I915_READ(GEN8_GT_IIR(i)));
			seq_printf(m, "GT Interrupt IER %d:\t%08x\n",
				   i, I915_READ(GEN8_GT_IER(i)));
		}

		seq_printf(m, "PCU interrupt mask:\t%08x\n",
			   I915_READ(GEN8_PCU_IMR));
		seq_printf(m, "PCU interrupt identity:\t%08x\n",
			   I915_READ(GEN8_PCU_IIR));
		seq_printf(m, "PCU interrupt enable:\t%08x\n",
			   I915_READ(GEN8_PCU_IER));
	} else if (INTEL_GEN(dev_priv) >= 11) {
		if (HAS_MASTER_UNIT_IRQ(dev_priv))
			seq_printf(m, "Master Unit Interrupt Control:  %08x\n",
				   I915_READ(DG1_MSTR_UNIT_INTR));

		seq_printf(m, "Master Interrupt Control:  %08x\n",
			   I915_READ(GEN11_GFX_MSTR_IRQ));

		seq_printf(m, "Render/Copy Intr Enable:   %08x\n",
			   I915_READ(GEN11_RENDER_COPY_INTR_ENABLE));
		seq_printf(m, "VCS/VECS Intr Enable:      %08x\n",
			   I915_READ(GEN11_VCS_VECS_INTR_ENABLE));
		seq_printf(m, "GUC/SG Intr Enable:\t   %08x\n",
			   I915_READ(GEN11_GUC_SG_INTR_ENABLE));
		seq_printf(m, "GPM/WGBOXPERF Intr Enable: %08x\n",
			   I915_READ(GEN11_GPM_WGBOXPERF_INTR_ENABLE));
		seq_printf(m, "Crypto Intr Enable:\t   %08x\n",
			   I915_READ(GEN11_CRYPTO_RSVD_INTR_ENABLE));
		seq_printf(m, "GUnit/CSME Intr Enable:\t   %08x\n",
			   I915_READ(GEN11_GUNIT_CSME_INTR_ENABLE));

		seq_printf(m, "Display Interrupt Control:\t%08x\n",
			   I915_READ(GEN11_DISPLAY_INT_CTL));

		gen8_display_interrupt_info(m);
	} else if (INTEL_GEN(dev_priv) >= 8) {
		seq_printf(m, "Master Interrupt Control:\t%08x\n",
			   I915_READ(GEN8_MASTER_IRQ));

		for (i = 0; i < 4; i++) {
			seq_printf(m, "GT Interrupt IMR %d:\t%08x\n",
				   i, I915_READ(GEN8_GT_IMR(i)));
			seq_printf(m, "GT Interrupt IIR %d:\t%08x\n",
				   i, I915_READ(GEN8_GT_IIR(i)));
			seq_printf(m, "GT Interrupt IER %d:\t%08x\n",
				   i, I915_READ(GEN8_GT_IER(i)));
		}

		gen8_display_interrupt_info(m);
	} else if (IS_VALLEYVIEW(dev_priv)) {
		intel_wakeref_t pref;

		seq_printf(m, "Display IER:\t%08x\n",
			   I915_READ(VLV_IER));
		seq_printf(m, "Display IIR:\t%08x\n",
			   I915_READ(VLV_IIR));
		seq_printf(m, "Display IIR_RW:\t%08x\n",
			   I915_READ(VLV_IIR_RW));
		seq_printf(m, "Display IMR:\t%08x\n",
			   I915_READ(VLV_IMR));
		for_each_pipe(dev_priv, pipe) {
			enum intel_display_power_domain power_domain;

			power_domain = POWER_DOMAIN_PIPE(pipe);
			pref = intel_display_power_get_if_enabled(dev_priv,
								  power_domain);
			if (!pref) {
				seq_printf(m, "Pipe %c power disabled\n",
					   pipe_name(pipe));
				continue;
			}

			seq_printf(m, "Pipe %c stat:\t%08x\n",
				   pipe_name(pipe),
				   I915_READ(PIPESTAT(pipe)));
			intel_display_power_put(dev_priv, power_domain, pref);
		}

		seq_printf(m, "Master IER:\t%08x\n",
			   I915_READ(VLV_MASTER_IER));

		seq_printf(m, "Render IER:\t%08x\n",
			   I915_READ(GTIER));
		seq_printf(m, "Render IIR:\t%08x\n",
			   I915_READ(GTIIR));
		seq_printf(m, "Render IMR:\t%08x\n",
			   I915_READ(GTIMR));

		seq_printf(m, "PM IER:\t\t%08x\n",
			   I915_READ(GEN6_PMIER));
		seq_printf(m, "PM IIR:\t\t%08x\n",
			   I915_READ(GEN6_PMIIR));
		seq_printf(m, "PM IMR:\t\t%08x\n",
			   I915_READ(GEN6_PMIMR));

		pref = intel_display_power_get(dev_priv, POWER_DOMAIN_INIT);
		seq_printf(m, "Port hotplug:\t%08x\n",
			   I915_READ(PORT_HOTPLUG_EN));
		seq_printf(m, "DPFLIPSTAT:\t%08x\n",
			   I915_READ(VLV_DPFLIPSTAT));
		seq_printf(m, "DPINVGTT:\t%08x\n",
			   I915_READ(DPINVGTT));
		intel_display_power_put(dev_priv, POWER_DOMAIN_INIT, pref);

	} else if (!HAS_PCH_SPLIT(dev_priv)) {
		seq_printf(m, "Interrupt enable:    %08x\n",
			   I915_READ(GEN2_IER));
		seq_printf(m, "Interrupt identity:  %08x\n",
			   I915_READ(GEN2_IIR));
		seq_printf(m, "Interrupt mask:      %08x\n",
			   I915_READ(GEN2_IMR));
		for_each_pipe(dev_priv, pipe)
			seq_printf(m, "Pipe %c stat:         %08x\n",
				   pipe_name(pipe),
				   I915_READ(PIPESTAT(pipe)));
	} else {
		seq_printf(m, "North Display Interrupt enable:		%08x\n",
			   I915_READ(DEIER));
		seq_printf(m, "North Display Interrupt identity:	%08x\n",
			   I915_READ(DEIIR));
		seq_printf(m, "North Display Interrupt mask:		%08x\n",
			   I915_READ(DEIMR));
		seq_printf(m, "South Display Interrupt enable:		%08x\n",
			   I915_READ(SDEIER));
		seq_printf(m, "South Display Interrupt identity:	%08x\n",
			   I915_READ(SDEIIR));
		seq_printf(m, "South Display Interrupt mask:		%08x\n",
			   I915_READ(SDEIMR));
		seq_printf(m, "Graphics Interrupt enable:		%08x\n",
			   I915_READ(GTIER));
		seq_printf(m, "Graphics Interrupt identity:		%08x\n",
			   I915_READ(GTIIR));
		seq_printf(m, "Graphics Interrupt mask:		%08x\n",
			   I915_READ(GTIMR));
	}

	if (INTEL_GEN(dev_priv) >= 11) {
		seq_printf(m, "RCS Intr Mask:\t %08x\n",
			   I915_READ(GEN11_RCS0_RSVD_INTR_MASK));
		seq_printf(m, "BCS Intr Mask:\t %08x\n",
			   I915_READ(GEN11_BCS_RSVD_INTR_MASK));
		seq_printf(m, "VCS0/VCS1 Intr Mask:\t %08x\n",
			   I915_READ(GEN11_VCS0_VCS1_INTR_MASK));
		seq_printf(m, "VCS2/VCS3 Intr Mask:\t %08x\n",
			   I915_READ(GEN11_VCS2_VCS3_INTR_MASK));
		seq_printf(m, "VECS0/VECS1 Intr Mask:\t %08x\n",
			   I915_READ(GEN11_VECS0_VECS1_INTR_MASK));
		seq_printf(m, "GUC/SG Intr Mask:\t %08x\n",
			   I915_READ(GEN11_GUC_SG_INTR_MASK));
		seq_printf(m, "GPM/WGBOXPERF Intr Mask: %08x\n",
			   I915_READ(GEN11_GPM_WGBOXPERF_INTR_MASK));
		seq_printf(m, "Crypto Intr Mask:\t %08x\n",
			   I915_READ(GEN11_CRYPTO_RSVD_INTR_MASK));
		seq_printf(m, "Gunit/CSME Intr Mask:\t %08x\n",
			   I915_READ(GEN11_GUNIT_CSME_INTR_MASK));

	} else if (INTEL_GEN(dev_priv) >= 6) {
		for_each_uabi_engine(engine, dev_priv) {
			seq_printf(m,
				   "Graphics Interrupt mask (%s):	%08x\n",
				   engine->name, ENGINE_READ(engine, RING_IMR));
		}
	}

	intel_runtime_pm_put(&dev_priv->runtime_pm, wakeref);

	return 0;
}

static int i915_gem_fence_regs_info(struct seq_file *m, void *data)
{
	struct drm_i915_private *i915 = node_to_i915(m->private);
	unsigned int i;

	seq_printf(m, "Total fences = %d\n", i915->ggtt.num_fences);

	rcu_read_lock();
	for (i = 0; i < i915->ggtt.num_fences; i++) {
		struct i915_fence_reg *reg = &i915->ggtt.fence_regs[i];
		struct i915_vma *vma = reg->vma;

		seq_printf(m, "Fence %d, pin count = %d, object = ",
			   i, atomic_read(&reg->pin_count));
		if (!vma)
			seq_puts(m, "unused");
		else
			i915_debugfs_describe_obj(m, vma->obj);
		seq_putc(m, '\n');
	}
	rcu_read_unlock();

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
		gpu = i915_gpu_coredump(&i915->gt, ALL_ENGINES);
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
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct intel_uncore *uncore = &dev_priv->uncore;
	struct intel_rps *rps = &dev_priv->gt.rps;
	intel_wakeref_t wakeref;

	wakeref = intel_runtime_pm_get(&dev_priv->runtime_pm);

	if (IS_GEN(dev_priv, 5)) {
		u16 rgvswctl = intel_uncore_read16(uncore, MEMSWCTL);
		u16 rgvstat = intel_uncore_read16(uncore, MEMSTAT_ILK);

		seq_printf(m, "Requested P-state: %d\n", (rgvswctl >> 8) & 0xf);
		seq_printf(m, "Requested VID: %d\n", rgvswctl & 0x3f);
		seq_printf(m, "Current VID: %d\n", (rgvstat & MEMSTAT_VID_MASK) >>
			   MEMSTAT_VID_SHIFT);
		seq_printf(m, "Current P-state: %d\n",
			   (rgvstat & MEMSTAT_PSTATE_MASK) >> MEMSTAT_PSTATE_SHIFT);
	} else if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
		u32 rpmodectl, freq_sts;

		rpmodectl = I915_READ(GEN6_RP_CONTROL);
		seq_printf(m, "Video Turbo Mode: %s\n",
			   yesno(rpmodectl & GEN6_RP_MEDIA_TURBO));
		seq_printf(m, "HW control enabled: %s\n",
			   yesno(rpmodectl & GEN6_RP_ENABLE));
		seq_printf(m, "SW control enabled: %s\n",
			   yesno((rpmodectl & GEN6_RP_MEDIA_MODE_MASK) ==
				  GEN6_RP_MEDIA_SW_MODE));

		vlv_punit_get(dev_priv);
		freq_sts = vlv_punit_read(dev_priv, PUNIT_REG_GPU_FREQ_STS);
		vlv_punit_put(dev_priv);

		seq_printf(m, "PUNIT_REG_GPU_FREQ_STS: 0x%08x\n", freq_sts);
		seq_printf(m, "DDR freq: %d MHz\n", dev_priv->mem_freq);

		seq_printf(m, "actual GPU freq: %d MHz\n",
			   intel_gpu_freq(rps, (freq_sts >> 8) & 0xff));

		seq_printf(m, "current GPU freq: %d MHz\n",
			   intel_gpu_freq(rps, rps->cur_freq));

		seq_printf(m, "max GPU freq: %d MHz\n",
			   intel_gpu_freq(rps, rps->max_freq));

		seq_printf(m, "min GPU freq: %d MHz\n",
			   intel_gpu_freq(rps, rps->min_freq));

		seq_printf(m, "idle GPU freq: %d MHz\n",
			   intel_gpu_freq(rps, rps->idle_freq));

		seq_printf(m,
			   "efficient (RPe) frequency: %d MHz\n",
			   intel_gpu_freq(rps, rps->efficient_freq));
	} else if (INTEL_GEN(dev_priv) >= 6) {
		u32 rp_state_limits;
		u32 gt_perf_status;
		u32 rp_state_cap;
		u32 rpmodectl, rpinclimit, rpdeclimit;
		u32 rpstat, cagf, reqf;
		u32 rpupei, rpcurup, rpprevup;
		u32 rpdownei, rpcurdown, rpprevdown;
		u32 pm_ier, pm_imr, pm_isr, pm_iir, pm_mask;
		int max_freq;

		rp_state_limits = I915_READ(GEN6_RP_STATE_LIMITS);
		if (IS_GEN9_LP(dev_priv)) {
			rp_state_cap = I915_READ(BXT_RP_STATE_CAP);
			gt_perf_status = I915_READ(BXT_GT_PERF_STATUS);
		} else {
			rp_state_cap = I915_READ(GEN6_RP_STATE_CAP);
			gt_perf_status = I915_READ(GEN6_GT_PERF_STATUS);
		}

		/* RPSTAT1 is in the GT power well */
		intel_uncore_forcewake_get(&dev_priv->uncore, FORCEWAKE_ALL);

		reqf = I915_READ(GEN6_RPNSWREQ);
		if (INTEL_GEN(dev_priv) >= 9)
			reqf >>= 23;
		else {
			reqf &= ~GEN6_TURBO_DISABLE;
			if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv))
				reqf >>= 24;
			else
				reqf >>= 25;
		}
		reqf = intel_gpu_freq(rps, reqf);

		rpmodectl = I915_READ(GEN6_RP_CONTROL);
		rpinclimit = I915_READ(GEN6_RP_UP_THRESHOLD);
		rpdeclimit = I915_READ(GEN6_RP_DOWN_THRESHOLD);

		rpstat = I915_READ(GEN6_RPSTAT1);
		rpupei = I915_READ(GEN6_RP_CUR_UP_EI) & GEN6_CURICONT_MASK;
		rpcurup = I915_READ(GEN6_RP_CUR_UP) & GEN6_CURBSYTAVG_MASK;
		rpprevup = I915_READ(GEN6_RP_PREV_UP) & GEN6_CURBSYTAVG_MASK;
		rpdownei = I915_READ(GEN6_RP_CUR_DOWN_EI) & GEN6_CURIAVG_MASK;
		rpcurdown = I915_READ(GEN6_RP_CUR_DOWN) & GEN6_CURBSYTAVG_MASK;
		rpprevdown = I915_READ(GEN6_RP_PREV_DOWN) & GEN6_CURBSYTAVG_MASK;
		cagf = intel_rps_read_actual_frequency(rps);

		intel_uncore_forcewake_put(&dev_priv->uncore, FORCEWAKE_ALL);

		if (INTEL_GEN(dev_priv) >= 11) {
			pm_ier = I915_READ(GEN11_GPM_WGBOXPERF_INTR_ENABLE);
			pm_imr = I915_READ(GEN11_GPM_WGBOXPERF_INTR_MASK);
			/*
			 * The equivalent to the PM ISR & IIR cannot be read
			 * without affecting the current state of the system
			 */
			pm_isr = 0;
			pm_iir = 0;
		} else if (INTEL_GEN(dev_priv) >= 8) {
			pm_ier = I915_READ(GEN8_GT_IER(2));
			pm_imr = I915_READ(GEN8_GT_IMR(2));
			pm_isr = I915_READ(GEN8_GT_ISR(2));
			pm_iir = I915_READ(GEN8_GT_IIR(2));
		} else {
			pm_ier = I915_READ(GEN6_PMIER);
			pm_imr = I915_READ(GEN6_PMIMR);
			pm_isr = I915_READ(GEN6_PMISR);
			pm_iir = I915_READ(GEN6_PMIIR);
		}
		pm_mask = I915_READ(GEN6_PMINTRMSK);

		seq_printf(m, "Video Turbo Mode: %s\n",
			   yesno(rpmodectl & GEN6_RP_MEDIA_TURBO));
		seq_printf(m, "HW control enabled: %s\n",
			   yesno(rpmodectl & GEN6_RP_ENABLE));
		seq_printf(m, "SW control enabled: %s\n",
			   yesno((rpmodectl & GEN6_RP_MEDIA_MODE_MASK) ==
				  GEN6_RP_MEDIA_SW_MODE));

		seq_printf(m, "PM IER=0x%08x IMR=0x%08x, MASK=0x%08x\n",
			   pm_ier, pm_imr, pm_mask);
		if (INTEL_GEN(dev_priv) <= 10)
			seq_printf(m, "PM ISR=0x%08x IIR=0x%08x\n",
				   pm_isr, pm_iir);
		seq_printf(m, "pm_intrmsk_mbz: 0x%08x\n",
			   rps->pm_intrmsk_mbz);
		seq_printf(m, "GT_PERF_STATUS: 0x%08x\n", gt_perf_status);
		seq_printf(m, "Render p-state ratio: %d\n",
			   (gt_perf_status & (INTEL_GEN(dev_priv) >= 9 ? 0x1ff00 : 0xff00)) >> 8);
		seq_printf(m, "Render p-state VID: %d\n",
			   gt_perf_status & 0xff);
		seq_printf(m, "Render p-state limit: %d\n",
			   rp_state_limits & 0xff);
		seq_printf(m, "RPSTAT1: 0x%08x\n", rpstat);
		seq_printf(m, "RPMODECTL: 0x%08x\n", rpmodectl);
		seq_printf(m, "RPINCLIMIT: 0x%08x\n", rpinclimit);
		seq_printf(m, "RPDECLIMIT: 0x%08x\n", rpdeclimit);
		seq_printf(m, "RPNSWREQ: %dMHz\n", reqf);
		seq_printf(m, "CAGF: %dMHz\n", cagf);
		seq_printf(m, "RP CUR UP EI: %d (%dns)\n",
			   rpupei,
			   intel_gt_pm_interval_to_ns(&dev_priv->gt, rpupei));
		seq_printf(m, "RP CUR UP: %d (%dun)\n",
			   rpcurup,
			   intel_gt_pm_interval_to_ns(&dev_priv->gt, rpcurup));
		seq_printf(m, "RP PREV UP: %d (%dns)\n",
			   rpprevup,
			   intel_gt_pm_interval_to_ns(&dev_priv->gt, rpprevup));
		seq_printf(m, "Up threshold: %d%%\n",
			   rps->power.up_threshold);

		seq_printf(m, "RP CUR DOWN EI: %d (%dns)\n",
			   rpdownei,
			   intel_gt_pm_interval_to_ns(&dev_priv->gt,
						      rpdownei));
		seq_printf(m, "RP CUR DOWN: %d (%dns)\n",
			   rpcurdown,
			   intel_gt_pm_interval_to_ns(&dev_priv->gt,
						      rpcurdown));
		seq_printf(m, "RP PREV DOWN: %d (%dns)\n",
			   rpprevdown,
			   intel_gt_pm_interval_to_ns(&dev_priv->gt,
						      rpprevdown));
		seq_printf(m, "Down threshold: %d%%\n",
			   rps->power.down_threshold);

		max_freq = (IS_GEN9_LP(dev_priv) ? rp_state_cap >> 0 :
			    rp_state_cap >> 16) & 0xff;
		max_freq *= (IS_GEN9_BC(dev_priv) ||
			     INTEL_GEN(dev_priv) >= 10 ? GEN9_FREQ_SCALER : 1);
		seq_printf(m, "Lowest (RPN) frequency: %dMHz\n",
			   intel_gpu_freq(rps, max_freq));

		max_freq = (rp_state_cap & 0xff00) >> 8;
		max_freq *= (IS_GEN9_BC(dev_priv) ||
			     INTEL_GEN(dev_priv) >= 10 ? GEN9_FREQ_SCALER : 1);
		seq_printf(m, "Nominal (RP1) frequency: %dMHz\n",
			   intel_gpu_freq(rps, max_freq));

		max_freq = (IS_GEN9_LP(dev_priv) ? rp_state_cap >> 16 :
			    rp_state_cap >> 0) & 0xff;
		max_freq *= (IS_GEN9_BC(dev_priv) ||
			     INTEL_GEN(dev_priv) >= 10 ? GEN9_FREQ_SCALER : 1);
		seq_printf(m, "Max non-overclocked (RP0) frequency: %dMHz\n",
			   intel_gpu_freq(rps, max_freq));
		seq_printf(m, "Max overclocked frequency: %dMHz\n",
			   intel_gpu_freq(rps, rps->max_freq));

		seq_printf(m, "Current freq: %d MHz\n",
			   intel_gpu_freq(rps, rps->cur_freq));
		seq_printf(m, "Actual freq: %d MHz\n", cagf);
		seq_printf(m, "Idle freq: %d MHz\n",
			   intel_gpu_freq(rps, rps->idle_freq));
		seq_printf(m, "Min freq: %d MHz\n",
			   intel_gpu_freq(rps, rps->min_freq));
		seq_printf(m, "Boost freq: %d MHz\n",
			   intel_gpu_freq(rps, rps->boost_freq));
		seq_printf(m, "Max freq: %d MHz\n",
			   intel_gpu_freq(rps, rps->max_freq));
		seq_printf(m,
			   "efficient (RPe) frequency: %d MHz\n",
			   intel_gpu_freq(rps, rps->efficient_freq));
	} else {
		seq_puts(m, "no P-state info available\n");
	}

	seq_printf(m, "Current CD clock frequency: %d kHz\n", dev_priv->cdclk.hw.cdclk);
	seq_printf(m, "Max CD clock frequency: %d kHz\n", dev_priv->max_cdclk_freq);
	seq_printf(m, "Max pixel clock frequency: %d kHz\n", dev_priv->max_dotclk_freq);

	intel_runtime_pm_put(&dev_priv->runtime_pm, wakeref);
	return 0;
}

static int i915_ring_freq_table(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct intel_rps *rps = &dev_priv->gt.rps;
	unsigned int max_gpu_freq, min_gpu_freq;
	intel_wakeref_t wakeref;
	int gpu_freq, ia_freq;

	if (!HAS_LLC(dev_priv))
		return -ENODEV;

	min_gpu_freq = rps->min_freq;
	max_gpu_freq = rps->max_freq;
	if (IS_GEN9_BC(dev_priv) || INTEL_GEN(dev_priv) >= 10) {
		/* Convert GT frequency to 50 HZ units */
		min_gpu_freq /= GEN9_FREQ_SCALER;
		max_gpu_freq /= GEN9_FREQ_SCALER;
	}

	seq_puts(m, "GPU freq (MHz)\tEffective CPU freq (MHz)\tEffective Ring freq (MHz)\n");

	wakeref = intel_runtime_pm_get(&dev_priv->runtime_pm);
	for (gpu_freq = min_gpu_freq; gpu_freq <= max_gpu_freq; gpu_freq++) {
		ia_freq = gpu_freq;
		sandybridge_pcode_read(dev_priv,
				       GEN6_PCODE_READ_MIN_FREQ_TABLE,
				       &ia_freq, NULL);
		seq_printf(m, "%d\t\t%d\t\t\t\t%d\n",
			   intel_gpu_freq(rps,
					  (gpu_freq *
					   (IS_GEN9_BC(dev_priv) ||
					    INTEL_GEN(dev_priv) >= 10 ?
					    GEN9_FREQ_SCALER : 1))),
			   ((ia_freq >> 0) & 0xff) * 100,
			   ((ia_freq >> 8) & 0xff) * 100);
	}
	intel_runtime_pm_put(&dev_priv->runtime_pm, wakeref);

	return 0;
}

static void describe_ctx_ring(struct seq_file *m, struct intel_ring *ring)
{
	seq_printf(m, " (ringbuffer, space: %d, head: %u, tail: %u, emit: %u)",
		   ring->space, ring->head, ring->tail, ring->emit);
}

static int i915_context_status(struct seq_file *m, void *unused)
{
	struct drm_i915_private *i915 = node_to_i915(m->private);
	struct i915_gem_context *ctx, *cn;

	spin_lock(&i915->gem.contexts.lock);
	list_for_each_entry_safe(ctx, cn, &i915->gem.contexts.list, link) {
		struct i915_gem_engines_iter it;
		struct intel_context *ce;

		if (!kref_get_unless_zero(&ctx->ref))
			continue;

		spin_unlock(&i915->gem.contexts.lock);

		seq_puts(m, "HW context ");
		if (ctx->pid) {
			struct task_struct *task;

			task = get_pid_task(ctx->pid, PIDTYPE_PID);
			if (task) {
				seq_printf(m, "(%s [%d]) ",
					   task->comm, task->pid);
				put_task_struct(task);
			}
		} else if (IS_ERR(ctx->file_priv)) {
			seq_puts(m, "(deleted) ");
		} else {
			seq_puts(m, "(kernel) ");
		}

		seq_putc(m, ctx->remap_slice ? 'R' : 'r');
		seq_putc(m, '\n');

		for_each_gem_engine(ce,
				    i915_gem_context_lock_engines(ctx), it) {
			if (intel_context_pin_if_active(ce)) {
				seq_printf(m, "%s: ", ce->engine->name);
				if (ce->state)
					i915_debugfs_describe_obj(m, ce->state->obj);
				describe_ctx_ring(m, ce->ring);
				seq_putc(m, '\n');
				intel_context_unpin(ce);
			}
		}
		i915_gem_context_unlock_engines(ctx);

		seq_putc(m, '\n');

		spin_lock(&i915->gem.contexts.lock);
		list_safe_reset_next(ctx, cn, link);
		i915_gem_context_put(ctx);
	}
	spin_unlock(&i915->gem.contexts.lock);

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
	if (INTEL_GEN(dev_priv) >= 8 || IS_VALLEYVIEW(dev_priv))
		return 0;

	wakeref = intel_runtime_pm_get(&dev_priv->runtime_pm);

	if (IS_GEN_RANGE(dev_priv, 3, 4)) {
		seq_printf(m, "DDC = 0x%08x\n",
			   intel_uncore_read(uncore, DCC));
		seq_printf(m, "DDC2 = 0x%08x\n",
			   intel_uncore_read(uncore, DCC2));
		seq_printf(m, "C0DRB3 = 0x%04x\n",
			   intel_uncore_read16(uncore, C0DRB3));
		seq_printf(m, "C1DRB3 = 0x%04x\n",
			   intel_uncore_read16(uncore, C1DRB3));
	} else if (INTEL_GEN(dev_priv) >= 6) {
		seq_printf(m, "MAD_DIMM_C0 = 0x%08x\n",
			   intel_uncore_read(uncore, MAD_DIMM_C0));
		seq_printf(m, "MAD_DIMM_C1 = 0x%08x\n",
			   intel_uncore_read(uncore, MAD_DIMM_C1));
		seq_printf(m, "MAD_DIMM_C2 = 0x%08x\n",
			   intel_uncore_read(uncore, MAD_DIMM_C2));
		seq_printf(m, "TILECTL = 0x%08x\n",
			   intel_uncore_read(uncore, TILECTL));
		if (INTEL_GEN(dev_priv) >= 8)
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

static const char *rps_power_to_str(unsigned int power)
{
	static const char * const strings[] = {
		[LOW_POWER] = "low power",
		[BETWEEN] = "mixed",
		[HIGH_POWER] = "high power",
	};

	if (power >= ARRAY_SIZE(strings) || !strings[power])
		return "unknown";

	return strings[power];
}

static int i915_rps_boost_info(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct intel_rps *rps = &dev_priv->gt.rps;

	seq_printf(m, "RPS enabled? %s\n", yesno(intel_rps_is_enabled(rps)));
	seq_printf(m, "RPS active? %s\n", yesno(intel_rps_is_active(rps)));
	seq_printf(m, "GPU busy? %s\n", yesno(dev_priv->gt.awake));
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

	seq_printf(m, "Wait boosts: %d\n", atomic_read(&rps->boosts));

	if (INTEL_GEN(dev_priv) >= 6 && intel_rps_is_active(rps)) {
		u32 rpup, rpupei;
		u32 rpdown, rpdownei;

		intel_uncore_forcewake_get(&dev_priv->uncore, FORCEWAKE_ALL);
		rpup = I915_READ_FW(GEN6_RP_CUR_UP) & GEN6_RP_EI_MASK;
		rpupei = I915_READ_FW(GEN6_RP_CUR_UP_EI) & GEN6_RP_EI_MASK;
		rpdown = I915_READ_FW(GEN6_RP_CUR_DOWN) & GEN6_RP_EI_MASK;
		rpdownei = I915_READ_FW(GEN6_RP_CUR_DOWN_EI) & GEN6_RP_EI_MASK;
		intel_uncore_forcewake_put(&dev_priv->uncore, FORCEWAKE_ALL);

		seq_printf(m, "\nRPS Autotuning (current \"%s\" window):\n",
			   rps_power_to_str(rps->power.mode));
		seq_printf(m, "  Avg. up: %d%% [above threshold? %d%%]\n",
			   rpup && rpupei ? 100 * rpup / rpupei : 0,
			   rps->power.up_threshold);
		seq_printf(m, "  Avg. down: %d%% [below threshold? %d%%]\n",
			   rpdown && rpdownei ? 100 * rpdown / rpdownei : 0,
			   rps->power.down_threshold);
	} else {
		seq_puts(m, "\nRPS Autotuning inactive\n");
	}

	return 0;
}

static int i915_llc(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	const bool edram = INTEL_GEN(dev_priv) > 8;

	seq_printf(m, "LLC: %s\n", yesno(HAS_LLC(dev_priv)));
	seq_printf(m, "%s: %uMB\n", edram ? "eDRAM" : "eLLC",
		   dev_priv->edram_size_mb);

	return 0;
}

static int i915_runtime_pm_status(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct pci_dev *pdev = dev_priv->drm.pdev;

	if (!HAS_RUNTIME_PM(dev_priv))
		seq_puts(m, "Runtime power management not supported\n");

	seq_printf(m, "Runtime power status: %s\n",
		   enableddisabled(!dev_priv->power_domains.wakeref));

	seq_printf(m, "GPU idle: %s\n", yesno(!dev_priv->gt.awake));
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
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct intel_engine_cs *engine;
	intel_wakeref_t wakeref;
	struct drm_printer p;

	wakeref = intel_runtime_pm_get(&dev_priv->runtime_pm);

	seq_printf(m, "GT awake? %s [%d]\n",
		   yesno(dev_priv->gt.awake),
		   atomic_read(&dev_priv->gt.wakeref.count));
	seq_printf(m, "CS timestamp frequency: %u Hz\n",
		   RUNTIME_INFO(dev_priv)->cs_timestamp_frequency_hz);

	p = drm_seq_file_printer(m);
	for_each_uabi_engine(engine, dev_priv)
		intel_engine_dump(engine, &p, "%s\n", engine->name);

	intel_runtime_pm_put(&dev_priv->runtime_pm, wakeref);

	return 0;
}

static int i915_shrinker_info(struct seq_file *m, void *unused)
{
	struct drm_i915_private *i915 = node_to_i915(m->private);

	seq_printf(m, "seeks = %d\n", i915->mm.shrinker.seeks);
	seq_printf(m, "batch = %lu\n", i915->mm.shrinker.batch);

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

static int
i915_wedged_get(void *data, u64 *val)
{
	struct drm_i915_private *i915 = data;
	int ret = intel_gt_terminally_wedged(&i915->gt);

	switch (ret) {
	case -EIO:
		*val = 1;
		return 0;
	case 0:
		*val = 0;
		return 0;
	default:
		return ret;
	}
}

static int
i915_wedged_set(void *data, u64 val)
{
	struct drm_i915_private *i915 = data;

	/* Flush any previous reset before applying for a new one */
	wait_event(i915->gt.reset.queue,
		   !test_bit(I915_RESET_BACKOFF, &i915->gt.reset.flags));

	intel_gt_handle_error(&i915->gt, val, I915_ERROR_CAPTURE,
			      "Manually set wedged engine mask = %llx", val);
	return 0;
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
	if (i915_cs_timestamp_ns_to_ticks(i915, val) > U32_MAX)
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
	int ret;

	DRM_DEBUG("Dropping caches: 0x%08llx [0x%08llx]\n",
		  val, val & DROP_ALL);

	ret = gt_drop_caches(&i915->gt, val);
	if (ret)
		return ret;

	fs_reclaim_acquire(GFP_KERNEL);
	if (val & DROP_BOUND)
		i915_gem_shrink(i915, LONG_MAX, NULL, I915_SHRINK_BOUND);

	if (val & DROP_UNBOUND)
		i915_gem_shrink(i915, LONG_MAX, NULL, I915_SHRINK_UNBOUND);

	if (val & DROP_SHRINK_ALL)
		i915_gem_shrink_all(i915);
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

static int
i915_cache_sharing_get(void *data, u64 *val)
{
	struct drm_i915_private *dev_priv = data;
	intel_wakeref_t wakeref;
	u32 snpcr = 0;

	if (!(IS_GEN_RANGE(dev_priv, 6, 7)))
		return -ENODEV;

	with_intel_runtime_pm(&dev_priv->runtime_pm, wakeref)
		snpcr = I915_READ(GEN6_MBCUNIT_SNPCR);

	*val = (snpcr & GEN6_MBC_SNPCR_MASK) >> GEN6_MBC_SNPCR_SHIFT;

	return 0;
}

static int
i915_cache_sharing_set(void *data, u64 val)
{
	struct drm_i915_private *dev_priv = data;
	intel_wakeref_t wakeref;

	if (!(IS_GEN_RANGE(dev_priv, 6, 7)))
		return -ENODEV;

	if (val > 3)
		return -EINVAL;

	drm_dbg(&dev_priv->drm,
		"Manually setting uncore sharing to %llu\n", val);
	with_intel_runtime_pm(&dev_priv->runtime_pm, wakeref) {
		u32 snpcr;

		/* Update the cache sharing policy here as well */
		snpcr = I915_READ(GEN6_MBCUNIT_SNPCR);
		snpcr &= ~GEN6_MBC_SNPCR_MASK;
		snpcr |= val << GEN6_MBC_SNPCR_SHIFT;
		I915_WRITE(GEN6_MBCUNIT_SNPCR, snpcr);
	}

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(i915_cache_sharing_fops,
			i915_cache_sharing_get, i915_cache_sharing_set,
			"%llu\n");

static int i915_sseu_status(struct seq_file *m, void *unused)
{
	struct drm_i915_private *i915 = node_to_i915(m->private);
	struct intel_gt *gt = &i915->gt;

	return intel_sseu_status(m, gt);
}

static int i915_forcewake_open(struct inode *inode, struct file *file)
{
	struct drm_i915_private *i915 = inode->i_private;
	struct intel_gt *gt = &i915->gt;

	atomic_inc(&gt->user_wakeref);
	intel_gt_pm_get(gt);
	if (INTEL_GEN(i915) >= 6)
		intel_uncore_forcewake_user_get(gt->uncore);

	return 0;
}

static int i915_forcewake_release(struct inode *inode, struct file *file)
{
	struct drm_i915_private *i915 = inode->i_private;
	struct intel_gt *gt = &i915->gt;

	if (INTEL_GEN(i915) >= 6)
		intel_uncore_forcewake_user_put(&i915->uncore);
	intel_gt_pm_put(gt);
	atomic_dec(&gt->user_wakeref);

	return 0;
}

static const struct file_operations i915_forcewake_fops = {
	.owner = THIS_MODULE,
	.open = i915_forcewake_open,
	.release = i915_forcewake_release,
};

static const struct drm_info_list i915_debugfs_list[] = {
	{"i915_capabilities", i915_capabilities, 0},
	{"i915_gem_objects", i915_gem_object_info, 0},
	{"i915_gem_fence_regs", i915_gem_fence_regs_info, 0},
	{"i915_gem_interrupt", i915_interrupt_info, 0},
	{"i915_frequency_info", i915_frequency_info, 0},
	{"i915_ring_freq_table", i915_ring_freq_table, 0},
	{"i915_context_status", i915_context_status, 0},
	{"i915_swizzle_info", i915_swizzle_info, 0},
	{"i915_llc", i915_llc, 0},
	{"i915_runtime_pm_status", i915_runtime_pm_status, 0},
	{"i915_engine_info", i915_engine_info, 0},
	{"i915_shrinker_info", i915_shrinker_info, 0},
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
	{"i915_cache_sharing", &i915_cache_sharing_fops},
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
