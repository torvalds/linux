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

#include <linux/debugfs.h>
#include <linux/sort.h>
#include <linux/sched/mm.h>
#include "intel_drv.h"
#include "intel_guc_submission.h"

static inline struct drm_i915_private *node_to_i915(struct drm_info_node *node)
{
	return to_i915(node->minor->dev);
}

static int i915_capabilities(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	const struct intel_device_info *info = INTEL_INFO(dev_priv);
	struct drm_printer p = drm_seq_file_printer(m);

	seq_printf(m, "gen: %d\n", INTEL_GEN(dev_priv));
	seq_printf(m, "platform: %s\n", intel_platform_name(info->platform));
	seq_printf(m, "pch: %d\n", INTEL_PCH_TYPE(dev_priv));

	intel_device_info_dump_flags(info, &p);
	intel_device_info_dump_runtime(info, &p);
	intel_driver_caps_print(&dev_priv->caps, &p);

	kernel_param_lock(THIS_MODULE);
	i915_params_dump(&i915_modparams, &p);
	kernel_param_unlock(THIS_MODULE);

	return 0;
}

static char get_active_flag(struct drm_i915_gem_object *obj)
{
	return i915_gem_object_is_active(obj) ? '*' : ' ';
}

static char get_pin_flag(struct drm_i915_gem_object *obj)
{
	return obj->pin_global ? 'p' : ' ';
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
	return obj->userfault_count ? 'g' : ' ';
}

static char get_pin_mapped_flag(struct drm_i915_gem_object *obj)
{
	return obj->mm.mapping ? 'M' : ' ';
}

static u64 i915_gem_obj_total_ggtt_size(struct drm_i915_gem_object *obj)
{
	u64 size = 0;
	struct i915_vma *vma;

	for_each_ggtt_vma(vma, obj) {
		if (drm_mm_node_allocated(&vma->node))
			size += vma->node.size;
	}

	return size;
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

static void
describe_obj(struct seq_file *m, struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *dev_priv = to_i915(obj->base.dev);
	struct intel_engine_cs *engine;
	struct i915_vma *vma;
	unsigned int frontbuffer_bits;
	int pin_count = 0;

	lockdep_assert_held(&obj->base.dev->struct_mutex);

	seq_printf(m, "%pK: %c%c%c%c%c %8zdKiB %02x %02x %s%s%s",
		   &obj->base,
		   get_active_flag(obj),
		   get_pin_flag(obj),
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
	list_for_each_entry(vma, &obj->vma_list, obj_link) {
		if (i915_vma_is_pinned(vma))
			pin_count++;
	}
	seq_printf(m, " (pinned x %d)", pin_count);
	if (obj->pin_global)
		seq_printf(m, " (global)");
	list_for_each_entry(vma, &obj->vma_list, obj_link) {
		if (!drm_mm_node_allocated(&vma->node))
			continue;

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

			default:
				MISSING_CASE(vma->ggtt_view.type);
				break;
			}
		}
		if (vma->fence)
			seq_printf(m, " , fence: %d%s",
				   vma->fence->id,
				   i915_gem_active_isset(&vma->last_fence) ? "*" : "");
		seq_puts(m, ")");
	}
	if (obj->stolen)
		seq_printf(m, " (stolen: %08llx)", obj->stolen->start);

	engine = i915_gem_object_last_write_engine(obj);
	if (engine)
		seq_printf(m, " (%s)", engine->name);

	frontbuffer_bits = atomic_read(&obj->frontbuffer_bits);
	if (frontbuffer_bits)
		seq_printf(m, " (frontbuffer: 0x%03x)", frontbuffer_bits);
}

static int obj_rank_by_stolen(const void *A, const void *B)
{
	const struct drm_i915_gem_object *a =
		*(const struct drm_i915_gem_object **)A;
	const struct drm_i915_gem_object *b =
		*(const struct drm_i915_gem_object **)B;

	if (a->stolen->start < b->stolen->start)
		return -1;
	if (a->stolen->start > b->stolen->start)
		return 1;
	return 0;
}

static int i915_gem_stolen_list_info(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct drm_device *dev = &dev_priv->drm;
	struct drm_i915_gem_object **objects;
	struct drm_i915_gem_object *obj;
	u64 total_obj_size, total_gtt_size;
	unsigned long total, count, n;
	int ret;

	total = READ_ONCE(dev_priv->mm.object_count);
	objects = kvmalloc_array(total, sizeof(*objects), GFP_KERNEL);
	if (!objects)
		return -ENOMEM;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		goto out;

	total_obj_size = total_gtt_size = count = 0;

	spin_lock(&dev_priv->mm.obj_lock);
	list_for_each_entry(obj, &dev_priv->mm.bound_list, mm.link) {
		if (count == total)
			break;

		if (obj->stolen == NULL)
			continue;

		objects[count++] = obj;
		total_obj_size += obj->base.size;
		total_gtt_size += i915_gem_obj_total_ggtt_size(obj);

	}
	list_for_each_entry(obj, &dev_priv->mm.unbound_list, mm.link) {
		if (count == total)
			break;

		if (obj->stolen == NULL)
			continue;

		objects[count++] = obj;
		total_obj_size += obj->base.size;
	}
	spin_unlock(&dev_priv->mm.obj_lock);

	sort(objects, count, sizeof(*objects), obj_rank_by_stolen, NULL);

	seq_puts(m, "Stolen:\n");
	for (n = 0; n < count; n++) {
		seq_puts(m, "   ");
		describe_obj(m, objects[n]);
		seq_putc(m, '\n');
	}
	seq_printf(m, "Total %lu objects, %llu bytes, %llu GTT size\n",
		   count, total_obj_size, total_gtt_size);

	mutex_unlock(&dev->struct_mutex);
out:
	kvfree(objects);
	return ret;
}

struct file_stats {
	struct drm_i915_file_private *file_priv;
	unsigned long count;
	u64 total, unbound;
	u64 global, shared;
	u64 active, inactive;
};

static int per_file_stats(int id, void *ptr, void *data)
{
	struct drm_i915_gem_object *obj = ptr;
	struct file_stats *stats = data;
	struct i915_vma *vma;

	lockdep_assert_held(&obj->base.dev->struct_mutex);

	stats->count++;
	stats->total += obj->base.size;
	if (!obj->bind_count)
		stats->unbound += obj->base.size;
	if (obj->base.name || obj->base.dma_buf)
		stats->shared += obj->base.size;

	list_for_each_entry(vma, &obj->vma_list, obj_link) {
		if (!drm_mm_node_allocated(&vma->node))
			continue;

		if (i915_vma_is_ggtt(vma)) {
			stats->global += vma->node.size;
		} else {
			struct i915_hw_ppgtt *ppgtt = i915_vm_to_ppgtt(vma->vm);

			if (ppgtt->base.file != stats->file_priv)
				continue;
		}

		if (i915_vma_is_active(vma))
			stats->active += vma->node.size;
		else
			stats->inactive += vma->node.size;
	}

	return 0;
}

#define print_file_stats(m, name, stats) do { \
	if (stats.count) \
		seq_printf(m, "%s: %lu objects, %llu bytes (%llu active, %llu inactive, %llu global, %llu shared, %llu unbound)\n", \
			   name, \
			   stats.count, \
			   stats.total, \
			   stats.active, \
			   stats.inactive, \
			   stats.global, \
			   stats.shared, \
			   stats.unbound); \
} while (0)

static void print_batch_pool_stats(struct seq_file *m,
				   struct drm_i915_private *dev_priv)
{
	struct drm_i915_gem_object *obj;
	struct file_stats stats;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int j;

	memset(&stats, 0, sizeof(stats));

	for_each_engine(engine, dev_priv, id) {
		for (j = 0; j < ARRAY_SIZE(engine->batch_pool.cache_list); j++) {
			list_for_each_entry(obj,
					    &engine->batch_pool.cache_list[j],
					    batch_pool_link)
				per_file_stats(0, obj, &stats);
		}
	}

	print_file_stats(m, "[k]batch pool", stats);
}

static int per_file_ctx_stats(int idx, void *ptr, void *data)
{
	struct i915_gem_context *ctx = ptr;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	for_each_engine(engine, ctx->i915, id) {
		struct intel_context *ce = to_intel_context(ctx, engine);

		if (ce->state)
			per_file_stats(0, ce->state->obj, data);
		if (ce->ring)
			per_file_stats(0, ce->ring->vma->obj, data);
	}

	return 0;
}

static void print_context_stats(struct seq_file *m,
				struct drm_i915_private *dev_priv)
{
	struct drm_device *dev = &dev_priv->drm;
	struct file_stats stats;
	struct drm_file *file;

	memset(&stats, 0, sizeof(stats));

	mutex_lock(&dev->struct_mutex);
	if (dev_priv->kernel_context)
		per_file_ctx_stats(0, dev_priv->kernel_context, &stats);

	list_for_each_entry(file, &dev->filelist, lhead) {
		struct drm_i915_file_private *fpriv = file->driver_priv;
		idr_for_each(&fpriv->context_idr, per_file_ctx_stats, &stats);
	}
	mutex_unlock(&dev->struct_mutex);

	print_file_stats(m, "[k]contexts", stats);
}

static int i915_gem_object_info(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct drm_device *dev = &dev_priv->drm;
	struct i915_ggtt *ggtt = &dev_priv->ggtt;
	u32 count, mapped_count, purgeable_count, dpy_count, huge_count;
	u64 size, mapped_size, purgeable_size, dpy_size, huge_size;
	struct drm_i915_gem_object *obj;
	unsigned int page_sizes = 0;
	struct drm_file *file;
	char buf[80];
	int ret;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	seq_printf(m, "%u objects, %llu bytes\n",
		   dev_priv->mm.object_count,
		   dev_priv->mm.object_memory);

	size = count = 0;
	mapped_size = mapped_count = 0;
	purgeable_size = purgeable_count = 0;
	huge_size = huge_count = 0;

	spin_lock(&dev_priv->mm.obj_lock);
	list_for_each_entry(obj, &dev_priv->mm.unbound_list, mm.link) {
		size += obj->base.size;
		++count;

		if (obj->mm.madv == I915_MADV_DONTNEED) {
			purgeable_size += obj->base.size;
			++purgeable_count;
		}

		if (obj->mm.mapping) {
			mapped_count++;
			mapped_size += obj->base.size;
		}

		if (obj->mm.page_sizes.sg > I915_GTT_PAGE_SIZE) {
			huge_count++;
			huge_size += obj->base.size;
			page_sizes |= obj->mm.page_sizes.sg;
		}
	}
	seq_printf(m, "%u unbound objects, %llu bytes\n", count, size);

	size = count = dpy_size = dpy_count = 0;
	list_for_each_entry(obj, &dev_priv->mm.bound_list, mm.link) {
		size += obj->base.size;
		++count;

		if (obj->pin_global) {
			dpy_size += obj->base.size;
			++dpy_count;
		}

		if (obj->mm.madv == I915_MADV_DONTNEED) {
			purgeable_size += obj->base.size;
			++purgeable_count;
		}

		if (obj->mm.mapping) {
			mapped_count++;
			mapped_size += obj->base.size;
		}

		if (obj->mm.page_sizes.sg > I915_GTT_PAGE_SIZE) {
			huge_count++;
			huge_size += obj->base.size;
			page_sizes |= obj->mm.page_sizes.sg;
		}
	}
	spin_unlock(&dev_priv->mm.obj_lock);

	seq_printf(m, "%u bound objects, %llu bytes\n",
		   count, size);
	seq_printf(m, "%u purgeable objects, %llu bytes\n",
		   purgeable_count, purgeable_size);
	seq_printf(m, "%u mapped objects, %llu bytes\n",
		   mapped_count, mapped_size);
	seq_printf(m, "%u huge-paged objects (%s) %llu bytes\n",
		   huge_count,
		   stringify_page_sizes(page_sizes, buf, sizeof(buf)),
		   huge_size);
	seq_printf(m, "%u display objects (globally pinned), %llu bytes\n",
		   dpy_count, dpy_size);

	seq_printf(m, "%llu [%pa] gtt total\n",
		   ggtt->base.total, &ggtt->mappable_end);
	seq_printf(m, "Supported page sizes: %s\n",
		   stringify_page_sizes(INTEL_INFO(dev_priv)->page_sizes,
					buf, sizeof(buf)));

	seq_putc(m, '\n');
	print_batch_pool_stats(m, dev_priv);
	mutex_unlock(&dev->struct_mutex);

	mutex_lock(&dev->filelist_mutex);
	print_context_stats(m, dev_priv);
	list_for_each_entry_reverse(file, &dev->filelist, lhead) {
		struct file_stats stats;
		struct drm_i915_file_private *file_priv = file->driver_priv;
		struct i915_request *request;
		struct task_struct *task;

		mutex_lock(&dev->struct_mutex);

		memset(&stats, 0, sizeof(stats));
		stats.file_priv = file->driver_priv;
		spin_lock(&file->table_lock);
		idr_for_each(&file->object_idr, per_file_stats, &stats);
		spin_unlock(&file->table_lock);
		/*
		 * Although we have a valid reference on file->pid, that does
		 * not guarantee that the task_struct who called get_pid() is
		 * still alive (e.g. get_pid(current) => fork() => exit()).
		 * Therefore, we need to protect this ->comm access using RCU.
		 */
		request = list_first_entry_or_null(&file_priv->mm.request_list,
						   struct i915_request,
						   client_link);
		rcu_read_lock();
		task = pid_task(request && request->ctx->pid ?
				request->ctx->pid : file->pid,
				PIDTYPE_PID);
		print_file_stats(m, task ? task->comm : "<unknown>", stats);
		rcu_read_unlock();

		mutex_unlock(&dev->struct_mutex);
	}
	mutex_unlock(&dev->filelist_mutex);

	return 0;
}

static int i915_gem_gtt_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = m->private;
	struct drm_i915_private *dev_priv = node_to_i915(node);
	struct drm_device *dev = &dev_priv->drm;
	struct drm_i915_gem_object **objects;
	struct drm_i915_gem_object *obj;
	u64 total_obj_size, total_gtt_size;
	unsigned long nobject, n;
	int count, ret;

	nobject = READ_ONCE(dev_priv->mm.object_count);
	objects = kvmalloc_array(nobject, sizeof(*objects), GFP_KERNEL);
	if (!objects)
		return -ENOMEM;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	count = 0;
	spin_lock(&dev_priv->mm.obj_lock);
	list_for_each_entry(obj, &dev_priv->mm.bound_list, mm.link) {
		objects[count++] = obj;
		if (count == nobject)
			break;
	}
	spin_unlock(&dev_priv->mm.obj_lock);

	total_obj_size = total_gtt_size = 0;
	for (n = 0;  n < count; n++) {
		obj = objects[n];

		seq_puts(m, "   ");
		describe_obj(m, obj);
		seq_putc(m, '\n');
		total_obj_size += obj->base.size;
		total_gtt_size += i915_gem_obj_total_ggtt_size(obj);
	}

	mutex_unlock(&dev->struct_mutex);

	seq_printf(m, "Total %d objects, %llu bytes, %llu GTT size\n",
		   count, total_obj_size, total_gtt_size);
	kvfree(objects);

	return 0;
}

static int i915_gem_batch_pool_info(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct drm_device *dev = &dev_priv->drm;
	struct drm_i915_gem_object *obj;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int total = 0;
	int ret, j;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	for_each_engine(engine, dev_priv, id) {
		for (j = 0; j < ARRAY_SIZE(engine->batch_pool.cache_list); j++) {
			int count;

			count = 0;
			list_for_each_entry(obj,
					    &engine->batch_pool.cache_list[j],
					    batch_pool_link)
				count++;
			seq_printf(m, "%s cache[%d]: %d objects\n",
				   engine->name, j, count);

			list_for_each_entry(obj,
					    &engine->batch_pool.cache_list[j],
					    batch_pool_link) {
				seq_puts(m, "   ");
				describe_obj(m, obj);
				seq_putc(m, '\n');
			}

			total += count;
		}
	}

	seq_printf(m, "total: %d\n", total);

	mutex_unlock(&dev->struct_mutex);

	return 0;
}

static void gen8_display_interrupt_info(struct seq_file *m)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	int pipe;

	for_each_pipe(dev_priv, pipe) {
		enum intel_display_power_domain power_domain;

		power_domain = POWER_DOMAIN_PIPE(pipe);
		if (!intel_display_power_get_if_enabled(dev_priv,
							power_domain)) {
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

		intel_display_power_put(dev_priv, power_domain);
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
	enum intel_engine_id id;
	int i, pipe;

	intel_runtime_pm_get(dev_priv);

	if (IS_CHERRYVIEW(dev_priv)) {
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
			if (!intel_display_power_get_if_enabled(dev_priv,
								power_domain)) {
				seq_printf(m, "Pipe %c power disabled\n",
					   pipe_name(pipe));
				continue;
			}

			seq_printf(m, "Pipe %c stat:\t%08x\n",
				   pipe_name(pipe),
				   I915_READ(PIPESTAT(pipe)));

			intel_display_power_put(dev_priv, power_domain);
		}

		intel_display_power_get(dev_priv, POWER_DOMAIN_INIT);
		seq_printf(m, "Port hotplug:\t%08x\n",
			   I915_READ(PORT_HOTPLUG_EN));
		seq_printf(m, "DPFLIPSTAT:\t%08x\n",
			   I915_READ(VLV_DPFLIPSTAT));
		seq_printf(m, "DPINVGTT:\t%08x\n",
			   I915_READ(DPINVGTT));
		intel_display_power_put(dev_priv, POWER_DOMAIN_INIT);

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
			if (!intel_display_power_get_if_enabled(dev_priv,
								power_domain)) {
				seq_printf(m, "Pipe %c power disabled\n",
					   pipe_name(pipe));
				continue;
			}

			seq_printf(m, "Pipe %c stat:\t%08x\n",
				   pipe_name(pipe),
				   I915_READ(PIPESTAT(pipe)));
			intel_display_power_put(dev_priv, power_domain);
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

		seq_printf(m, "Port hotplug:\t%08x\n",
			   I915_READ(PORT_HOTPLUG_EN));
		seq_printf(m, "DPFLIPSTAT:\t%08x\n",
			   I915_READ(VLV_DPFLIPSTAT));
		seq_printf(m, "DPINVGTT:\t%08x\n",
			   I915_READ(DPINVGTT));

	} else if (!HAS_PCH_SPLIT(dev_priv)) {
		seq_printf(m, "Interrupt enable:    %08x\n",
			   I915_READ(IER));
		seq_printf(m, "Interrupt identity:  %08x\n",
			   I915_READ(IIR));
		seq_printf(m, "Interrupt mask:      %08x\n",
			   I915_READ(IMR));
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
		for_each_engine(engine, dev_priv, id) {
			seq_printf(m,
				   "Graphics Interrupt mask (%s):	%08x\n",
				   engine->name, I915_READ_IMR(engine));
		}
	}

	intel_runtime_pm_put(dev_priv);

	return 0;
}

static int i915_gem_fence_regs_info(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct drm_device *dev = &dev_priv->drm;
	int i, ret;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	seq_printf(m, "Total fences = %d\n", dev_priv->num_fence_regs);
	for (i = 0; i < dev_priv->num_fence_regs; i++) {
		struct i915_vma *vma = dev_priv->fence_regs[i].vma;

		seq_printf(m, "Fence %d, pin count = %d, object = ",
			   i, dev_priv->fence_regs[i].pin_count);
		if (!vma)
			seq_puts(m, "unused");
		else
			describe_obj(m, vma->obj);
		seq_putc(m, '\n');
	}

	mutex_unlock(&dev->struct_mutex);
	return 0;
}

#if IS_ENABLED(CONFIG_DRM_I915_CAPTURE_ERROR)
static ssize_t gpu_state_read(struct file *file, char __user *ubuf,
			      size_t count, loff_t *pos)
{
	struct i915_gpu_state *error = file->private_data;
	struct drm_i915_error_state_buf str;
	ssize_t ret;
	loff_t tmp;

	if (!error)
		return 0;

	ret = i915_error_state_buf_init(&str, error->i915, count, *pos);
	if (ret)
		return ret;

	ret = i915_error_state_to_str(&str, error);
	if (ret)
		goto out;

	tmp = 0;
	ret = simple_read_from_buffer(ubuf, count, &tmp, str.buf, str.bytes);
	if (ret < 0)
		goto out;

	*pos = str.start + ret;
out:
	i915_error_state_buf_release(&str);
	return ret;
}

static int gpu_state_release(struct inode *inode, struct file *file)
{
	i915_gpu_state_put(file->private_data);
	return 0;
}

static int i915_gpu_info_open(struct inode *inode, struct file *file)
{
	struct drm_i915_private *i915 = inode->i_private;
	struct i915_gpu_state *gpu;

	intel_runtime_pm_get(i915);
	gpu = i915_capture_gpu_state(i915);
	intel_runtime_pm_put(i915);
	if (!gpu)
		return -ENOMEM;

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
	struct i915_gpu_state *error = filp->private_data;

	if (!error)
		return 0;

	DRM_DEBUG_DRIVER("Resetting error state\n");
	i915_reset_error_state(error->i915);

	return cnt;
}

static int i915_error_state_open(struct inode *inode, struct file *file)
{
	file->private_data = i915_first_error_state(inode->i_private);
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

static int
i915_next_seqno_set(void *data, u64 val)
{
	struct drm_i915_private *dev_priv = data;
	struct drm_device *dev = &dev_priv->drm;
	int ret;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	intel_runtime_pm_get(dev_priv);
	ret = i915_gem_set_global_seqno(dev, val);
	intel_runtime_pm_put(dev_priv);

	mutex_unlock(&dev->struct_mutex);

	return ret;
}

DEFINE_SIMPLE_ATTRIBUTE(i915_next_seqno_fops,
			NULL, i915_next_seqno_set,
			"0x%llx\n");

static int i915_frequency_info(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct intel_rps *rps = &dev_priv->gt_pm.rps;
	int ret = 0;

	intel_runtime_pm_get(dev_priv);

	if (IS_GEN5(dev_priv)) {
		u16 rgvswctl = I915_READ16(MEMSWCTL);
		u16 rgvstat = I915_READ16(MEMSTAT_ILK);

		seq_printf(m, "Requested P-state: %d\n", (rgvswctl >> 8) & 0xf);
		seq_printf(m, "Requested VID: %d\n", rgvswctl & 0x3f);
		seq_printf(m, "Current VID: %d\n", (rgvstat & MEMSTAT_VID_MASK) >>
			   MEMSTAT_VID_SHIFT);
		seq_printf(m, "Current P-state: %d\n",
			   (rgvstat & MEMSTAT_PSTATE_MASK) >> MEMSTAT_PSTATE_SHIFT);
	} else if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
		u32 rpmodectl, freq_sts;

		mutex_lock(&dev_priv->pcu_lock);

		rpmodectl = I915_READ(GEN6_RP_CONTROL);
		seq_printf(m, "Video Turbo Mode: %s\n",
			   yesno(rpmodectl & GEN6_RP_MEDIA_TURBO));
		seq_printf(m, "HW control enabled: %s\n",
			   yesno(rpmodectl & GEN6_RP_ENABLE));
		seq_printf(m, "SW control enabled: %s\n",
			   yesno((rpmodectl & GEN6_RP_MEDIA_MODE_MASK) ==
				  GEN6_RP_MEDIA_SW_MODE));

		freq_sts = vlv_punit_read(dev_priv, PUNIT_REG_GPU_FREQ_STS);
		seq_printf(m, "PUNIT_REG_GPU_FREQ_STS: 0x%08x\n", freq_sts);
		seq_printf(m, "DDR freq: %d MHz\n", dev_priv->mem_freq);

		seq_printf(m, "actual GPU freq: %d MHz\n",
			   intel_gpu_freq(dev_priv, (freq_sts >> 8) & 0xff));

		seq_printf(m, "current GPU freq: %d MHz\n",
			   intel_gpu_freq(dev_priv, rps->cur_freq));

		seq_printf(m, "max GPU freq: %d MHz\n",
			   intel_gpu_freq(dev_priv, rps->max_freq));

		seq_printf(m, "min GPU freq: %d MHz\n",
			   intel_gpu_freq(dev_priv, rps->min_freq));

		seq_printf(m, "idle GPU freq: %d MHz\n",
			   intel_gpu_freq(dev_priv, rps->idle_freq));

		seq_printf(m,
			   "efficient (RPe) frequency: %d MHz\n",
			   intel_gpu_freq(dev_priv, rps->efficient_freq));
		mutex_unlock(&dev_priv->pcu_lock);
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
		intel_uncore_forcewake_get(dev_priv, FORCEWAKE_ALL);

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
		reqf = intel_gpu_freq(dev_priv, reqf);

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
		cagf = intel_gpu_freq(dev_priv,
				      intel_get_cagf(dev_priv, rpstat));

		intel_uncore_forcewake_put(dev_priv, FORCEWAKE_ALL);

		if (IS_GEN6(dev_priv) || IS_GEN7(dev_priv)) {
			pm_ier = I915_READ(GEN6_PMIER);
			pm_imr = I915_READ(GEN6_PMIMR);
			pm_isr = I915_READ(GEN6_PMISR);
			pm_iir = I915_READ(GEN6_PMIIR);
			pm_mask = I915_READ(GEN6_PMINTRMSK);
		} else {
			pm_ier = I915_READ(GEN8_GT_IER(2));
			pm_imr = I915_READ(GEN8_GT_IMR(2));
			pm_isr = I915_READ(GEN8_GT_ISR(2));
			pm_iir = I915_READ(GEN8_GT_IIR(2));
			pm_mask = I915_READ(GEN6_PMINTRMSK);
		}
		seq_printf(m, "Video Turbo Mode: %s\n",
			   yesno(rpmodectl & GEN6_RP_MEDIA_TURBO));
		seq_printf(m, "HW control enabled: %s\n",
			   yesno(rpmodectl & GEN6_RP_ENABLE));
		seq_printf(m, "SW control enabled: %s\n",
			   yesno((rpmodectl & GEN6_RP_MEDIA_MODE_MASK) ==
				  GEN6_RP_MEDIA_SW_MODE));
		seq_printf(m, "PM IER=0x%08x IMR=0x%08x ISR=0x%08x IIR=0x%08x, MASK=0x%08x\n",
			   pm_ier, pm_imr, pm_isr, pm_iir, pm_mask);
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
		seq_printf(m, "RP CUR UP EI: %d (%dus)\n",
			   rpupei, GT_PM_INTERVAL_TO_US(dev_priv, rpupei));
		seq_printf(m, "RP CUR UP: %d (%dus)\n",
			   rpcurup, GT_PM_INTERVAL_TO_US(dev_priv, rpcurup));
		seq_printf(m, "RP PREV UP: %d (%dus)\n",
			   rpprevup, GT_PM_INTERVAL_TO_US(dev_priv, rpprevup));
		seq_printf(m, "Up threshold: %d%%\n", rps->up_threshold);

		seq_printf(m, "RP CUR DOWN EI: %d (%dus)\n",
			   rpdownei, GT_PM_INTERVAL_TO_US(dev_priv, rpdownei));
		seq_printf(m, "RP CUR DOWN: %d (%dus)\n",
			   rpcurdown, GT_PM_INTERVAL_TO_US(dev_priv, rpcurdown));
		seq_printf(m, "RP PREV DOWN: %d (%dus)\n",
			   rpprevdown, GT_PM_INTERVAL_TO_US(dev_priv, rpprevdown));
		seq_printf(m, "Down threshold: %d%%\n", rps->down_threshold);

		max_freq = (IS_GEN9_LP(dev_priv) ? rp_state_cap >> 0 :
			    rp_state_cap >> 16) & 0xff;
		max_freq *= (IS_GEN9_BC(dev_priv) ||
			     INTEL_GEN(dev_priv) >= 10 ? GEN9_FREQ_SCALER : 1);
		seq_printf(m, "Lowest (RPN) frequency: %dMHz\n",
			   intel_gpu_freq(dev_priv, max_freq));

		max_freq = (rp_state_cap & 0xff00) >> 8;
		max_freq *= (IS_GEN9_BC(dev_priv) ||
			     INTEL_GEN(dev_priv) >= 10 ? GEN9_FREQ_SCALER : 1);
		seq_printf(m, "Nominal (RP1) frequency: %dMHz\n",
			   intel_gpu_freq(dev_priv, max_freq));

		max_freq = (IS_GEN9_LP(dev_priv) ? rp_state_cap >> 16 :
			    rp_state_cap >> 0) & 0xff;
		max_freq *= (IS_GEN9_BC(dev_priv) ||
			     INTEL_GEN(dev_priv) >= 10 ? GEN9_FREQ_SCALER : 1);
		seq_printf(m, "Max non-overclocked (RP0) frequency: %dMHz\n",
			   intel_gpu_freq(dev_priv, max_freq));
		seq_printf(m, "Max overclocked frequency: %dMHz\n",
			   intel_gpu_freq(dev_priv, rps->max_freq));

		seq_printf(m, "Current freq: %d MHz\n",
			   intel_gpu_freq(dev_priv, rps->cur_freq));
		seq_printf(m, "Actual freq: %d MHz\n", cagf);
		seq_printf(m, "Idle freq: %d MHz\n",
			   intel_gpu_freq(dev_priv, rps->idle_freq));
		seq_printf(m, "Min freq: %d MHz\n",
			   intel_gpu_freq(dev_priv, rps->min_freq));
		seq_printf(m, "Boost freq: %d MHz\n",
			   intel_gpu_freq(dev_priv, rps->boost_freq));
		seq_printf(m, "Max freq: %d MHz\n",
			   intel_gpu_freq(dev_priv, rps->max_freq));
		seq_printf(m,
			   "efficient (RPe) frequency: %d MHz\n",
			   intel_gpu_freq(dev_priv, rps->efficient_freq));
	} else {
		seq_puts(m, "no P-state info available\n");
	}

	seq_printf(m, "Current CD clock frequency: %d kHz\n", dev_priv->cdclk.hw.cdclk);
	seq_printf(m, "Max CD clock frequency: %d kHz\n", dev_priv->max_cdclk_freq);
	seq_printf(m, "Max pixel clock frequency: %d kHz\n", dev_priv->max_dotclk_freq);

	intel_runtime_pm_put(dev_priv);
	return ret;
}

static void i915_instdone_info(struct drm_i915_private *dev_priv,
			       struct seq_file *m,
			       struct intel_instdone *instdone)
{
	int slice;
	int subslice;

	seq_printf(m, "\t\tINSTDONE: 0x%08x\n",
		   instdone->instdone);

	if (INTEL_GEN(dev_priv) <= 3)
		return;

	seq_printf(m, "\t\tSC_INSTDONE: 0x%08x\n",
		   instdone->slice_common);

	if (INTEL_GEN(dev_priv) <= 6)
		return;

	for_each_instdone_slice_subslice(dev_priv, slice, subslice)
		seq_printf(m, "\t\tSAMPLER_INSTDONE[%d][%d]: 0x%08x\n",
			   slice, subslice, instdone->sampler[slice][subslice]);

	for_each_instdone_slice_subslice(dev_priv, slice, subslice)
		seq_printf(m, "\t\tROW_INSTDONE[%d][%d]: 0x%08x\n",
			   slice, subslice, instdone->row[slice][subslice]);
}

static int i915_hangcheck_info(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct intel_engine_cs *engine;
	u64 acthd[I915_NUM_ENGINES];
	u32 seqno[I915_NUM_ENGINES];
	struct intel_instdone instdone;
	enum intel_engine_id id;

	if (test_bit(I915_WEDGED, &dev_priv->gpu_error.flags))
		seq_puts(m, "Wedged\n");
	if (test_bit(I915_RESET_BACKOFF, &dev_priv->gpu_error.flags))
		seq_puts(m, "Reset in progress: struct_mutex backoff\n");
	if (test_bit(I915_RESET_HANDOFF, &dev_priv->gpu_error.flags))
		seq_puts(m, "Reset in progress: reset handoff to waiter\n");
	if (waitqueue_active(&dev_priv->gpu_error.wait_queue))
		seq_puts(m, "Waiter holding struct mutex\n");
	if (waitqueue_active(&dev_priv->gpu_error.reset_queue))
		seq_puts(m, "struct_mutex blocked for reset\n");

	if (!i915_modparams.enable_hangcheck) {
		seq_puts(m, "Hangcheck disabled\n");
		return 0;
	}

	intel_runtime_pm_get(dev_priv);

	for_each_engine(engine, dev_priv, id) {
		acthd[id] = intel_engine_get_active_head(engine);
		seqno[id] = intel_engine_get_seqno(engine);
	}

	intel_engine_get_instdone(dev_priv->engine[RCS], &instdone);

	intel_runtime_pm_put(dev_priv);

	if (timer_pending(&dev_priv->gpu_error.hangcheck_work.timer))
		seq_printf(m, "Hangcheck active, timer fires in %dms\n",
			   jiffies_to_msecs(dev_priv->gpu_error.hangcheck_work.timer.expires -
					    jiffies));
	else if (delayed_work_pending(&dev_priv->gpu_error.hangcheck_work))
		seq_puts(m, "Hangcheck active, work pending\n");
	else
		seq_puts(m, "Hangcheck inactive\n");

	seq_printf(m, "GT active? %s\n", yesno(dev_priv->gt.awake));

	for_each_engine(engine, dev_priv, id) {
		struct intel_breadcrumbs *b = &engine->breadcrumbs;
		struct rb_node *rb;

		seq_printf(m, "%s:\n", engine->name);
		seq_printf(m, "\tseqno = %x [current %x, last %x]\n",
			   engine->hangcheck.seqno, seqno[id],
			   intel_engine_last_submit(engine));
		seq_printf(m, "\twaiters? %s, fake irq active? %s, stalled? %s\n",
			   yesno(intel_engine_has_waiter(engine)),
			   yesno(test_bit(engine->id,
					  &dev_priv->gpu_error.missed_irq_rings)),
			   yesno(engine->hangcheck.stalled));

		spin_lock_irq(&b->rb_lock);
		for (rb = rb_first(&b->waiters); rb; rb = rb_next(rb)) {
			struct intel_wait *w = rb_entry(rb, typeof(*w), node);

			seq_printf(m, "\t%s [%d] waiting for %x\n",
				   w->tsk->comm, w->tsk->pid, w->seqno);
		}
		spin_unlock_irq(&b->rb_lock);

		seq_printf(m, "\tACTHD = 0x%08llx [current 0x%08llx]\n",
			   (long long)engine->hangcheck.acthd,
			   (long long)acthd[id]);
		seq_printf(m, "\taction = %s(%d) %d ms ago\n",
			   hangcheck_action_to_str(engine->hangcheck.action),
			   engine->hangcheck.action,
			   jiffies_to_msecs(jiffies -
					    engine->hangcheck.action_timestamp));

		if (engine->id == RCS) {
			seq_puts(m, "\tinstdone read =\n");

			i915_instdone_info(dev_priv, m, &instdone);

			seq_puts(m, "\tinstdone accu =\n");

			i915_instdone_info(dev_priv, m,
					   &engine->hangcheck.instdone);
		}
	}

	return 0;
}

static int i915_reset_info(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct i915_gpu_error *error = &dev_priv->gpu_error;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	seq_printf(m, "full gpu reset = %u\n", i915_reset_count(error));

	for_each_engine(engine, dev_priv, id) {
		seq_printf(m, "%s = %u\n", engine->name,
			   i915_reset_engine_count(error, engine));
	}

	return 0;
}

static int ironlake_drpc_info(struct seq_file *m)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	u32 rgvmodectl, rstdbyctl;
	u16 crstandvid;

	rgvmodectl = I915_READ(MEMMODECTL);
	rstdbyctl = I915_READ(RSTDBYCTL);
	crstandvid = I915_READ16(CRSTANDVID);

	seq_printf(m, "HD boost: %s\n", yesno(rgvmodectl & MEMMODE_BOOST_EN));
	seq_printf(m, "Boost freq: %d\n",
		   (rgvmodectl & MEMMODE_BOOST_FREQ_MASK) >>
		   MEMMODE_BOOST_FREQ_SHIFT);
	seq_printf(m, "HW control enabled: %s\n",
		   yesno(rgvmodectl & MEMMODE_HWIDLE_EN));
	seq_printf(m, "SW control enabled: %s\n",
		   yesno(rgvmodectl & MEMMODE_SWMODE_EN));
	seq_printf(m, "Gated voltage change: %s\n",
		   yesno(rgvmodectl & MEMMODE_RCLK_GATE));
	seq_printf(m, "Starting frequency: P%d\n",
		   (rgvmodectl & MEMMODE_FSTART_MASK) >> MEMMODE_FSTART_SHIFT);
	seq_printf(m, "Max P-state: P%d\n",
		   (rgvmodectl & MEMMODE_FMAX_MASK) >> MEMMODE_FMAX_SHIFT);
	seq_printf(m, "Min P-state: P%d\n", (rgvmodectl & MEMMODE_FMIN_MASK));
	seq_printf(m, "RS1 VID: %d\n", (crstandvid & 0x3f));
	seq_printf(m, "RS2 VID: %d\n", ((crstandvid >> 8) & 0x3f));
	seq_printf(m, "Render standby enabled: %s\n",
		   yesno(!(rstdbyctl & RCX_SW_EXIT)));
	seq_puts(m, "Current RS state: ");
	switch (rstdbyctl & RSX_STATUS_MASK) {
	case RSX_STATUS_ON:
		seq_puts(m, "on\n");
		break;
	case RSX_STATUS_RC1:
		seq_puts(m, "RC1\n");
		break;
	case RSX_STATUS_RC1E:
		seq_puts(m, "RC1E\n");
		break;
	case RSX_STATUS_RS1:
		seq_puts(m, "RS1\n");
		break;
	case RSX_STATUS_RS2:
		seq_puts(m, "RS2 (RC6)\n");
		break;
	case RSX_STATUS_RS3:
		seq_puts(m, "RC3 (RC6+)\n");
		break;
	default:
		seq_puts(m, "unknown\n");
		break;
	}

	return 0;
}

static int i915_forcewake_domains(struct seq_file *m, void *data)
{
	struct drm_i915_private *i915 = node_to_i915(m->private);
	struct intel_uncore_forcewake_domain *fw_domain;
	unsigned int tmp;

	seq_printf(m, "user.bypass_count = %u\n",
		   i915->uncore.user_forcewake.count);

	for_each_fw_domain(fw_domain, i915, tmp)
		seq_printf(m, "%s.wake_count = %u\n",
			   intel_uncore_forcewake_domain_to_str(fw_domain->id),
			   READ_ONCE(fw_domain->wake_count));

	return 0;
}

static void print_rc6_res(struct seq_file *m,
			  const char *title,
			  const i915_reg_t reg)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);

	seq_printf(m, "%s %u (%llu us)\n",
		   title, I915_READ(reg),
		   intel_rc6_residency_us(dev_priv, reg));
}

static int vlv_drpc_info(struct seq_file *m)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	u32 rcctl1, pw_status;

	pw_status = I915_READ(VLV_GTLC_PW_STATUS);
	rcctl1 = I915_READ(GEN6_RC_CONTROL);

	seq_printf(m, "RC6 Enabled: %s\n",
		   yesno(rcctl1 & (GEN7_RC_CTL_TO_MODE |
					GEN6_RC_CTL_EI_MODE(1))));
	seq_printf(m, "Render Power Well: %s\n",
		   (pw_status & VLV_GTLC_PW_RENDER_STATUS_MASK) ? "Up" : "Down");
	seq_printf(m, "Media Power Well: %s\n",
		   (pw_status & VLV_GTLC_PW_MEDIA_STATUS_MASK) ? "Up" : "Down");

	print_rc6_res(m, "Render RC6 residency since boot:", VLV_GT_RENDER_RC6);
	print_rc6_res(m, "Media RC6 residency since boot:", VLV_GT_MEDIA_RC6);

	return i915_forcewake_domains(m, NULL);
}

static int gen6_drpc_info(struct seq_file *m)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	u32 gt_core_status, rcctl1, rc6vids = 0;
	u32 gen9_powergate_enable = 0, gen9_powergate_status = 0;

	gt_core_status = I915_READ_FW(GEN6_GT_CORE_STATUS);
	trace_i915_reg_rw(false, GEN6_GT_CORE_STATUS, gt_core_status, 4, true);

	rcctl1 = I915_READ(GEN6_RC_CONTROL);
	if (INTEL_GEN(dev_priv) >= 9) {
		gen9_powergate_enable = I915_READ(GEN9_PG_ENABLE);
		gen9_powergate_status = I915_READ(GEN9_PWRGT_DOMAIN_STATUS);
	}

	if (INTEL_GEN(dev_priv) <= 7) {
		mutex_lock(&dev_priv->pcu_lock);
		sandybridge_pcode_read(dev_priv, GEN6_PCODE_READ_RC6VIDS,
				       &rc6vids);
		mutex_unlock(&dev_priv->pcu_lock);
	}

	seq_printf(m, "RC1e Enabled: %s\n",
		   yesno(rcctl1 & GEN6_RC_CTL_RC1e_ENABLE));
	seq_printf(m, "RC6 Enabled: %s\n",
		   yesno(rcctl1 & GEN6_RC_CTL_RC6_ENABLE));
	if (INTEL_GEN(dev_priv) >= 9) {
		seq_printf(m, "Render Well Gating Enabled: %s\n",
			yesno(gen9_powergate_enable & GEN9_RENDER_PG_ENABLE));
		seq_printf(m, "Media Well Gating Enabled: %s\n",
			yesno(gen9_powergate_enable & GEN9_MEDIA_PG_ENABLE));
	}
	seq_printf(m, "Deep RC6 Enabled: %s\n",
		   yesno(rcctl1 & GEN6_RC_CTL_RC6p_ENABLE));
	seq_printf(m, "Deepest RC6 Enabled: %s\n",
		   yesno(rcctl1 & GEN6_RC_CTL_RC6pp_ENABLE));
	seq_puts(m, "Current RC state: ");
	switch (gt_core_status & GEN6_RCn_MASK) {
	case GEN6_RC0:
		if (gt_core_status & GEN6_CORE_CPD_STATE_MASK)
			seq_puts(m, "Core Power Down\n");
		else
			seq_puts(m, "on\n");
		break;
	case GEN6_RC3:
		seq_puts(m, "RC3\n");
		break;
	case GEN6_RC6:
		seq_puts(m, "RC6\n");
		break;
	case GEN6_RC7:
		seq_puts(m, "RC7\n");
		break;
	default:
		seq_puts(m, "Unknown\n");
		break;
	}

	seq_printf(m, "Core Power Down: %s\n",
		   yesno(gt_core_status & GEN6_CORE_CPD_STATE_MASK));
	if (INTEL_GEN(dev_priv) >= 9) {
		seq_printf(m, "Render Power Well: %s\n",
			(gen9_powergate_status &
			 GEN9_PWRGT_RENDER_STATUS_MASK) ? "Up" : "Down");
		seq_printf(m, "Media Power Well: %s\n",
			(gen9_powergate_status &
			 GEN9_PWRGT_MEDIA_STATUS_MASK) ? "Up" : "Down");
	}

	/* Not exactly sure what this is */
	print_rc6_res(m, "RC6 \"Locked to RPn\" residency since boot:",
		      GEN6_GT_GFX_RC6_LOCKED);
	print_rc6_res(m, "RC6 residency since boot:", GEN6_GT_GFX_RC6);
	print_rc6_res(m, "RC6+ residency since boot:", GEN6_GT_GFX_RC6p);
	print_rc6_res(m, "RC6++ residency since boot:", GEN6_GT_GFX_RC6pp);

	if (INTEL_GEN(dev_priv) <= 7) {
		seq_printf(m, "RC6   voltage: %dmV\n",
			   GEN6_DECODE_RC6_VID(((rc6vids >> 0) & 0xff)));
		seq_printf(m, "RC6+  voltage: %dmV\n",
			   GEN6_DECODE_RC6_VID(((rc6vids >> 8) & 0xff)));
		seq_printf(m, "RC6++ voltage: %dmV\n",
			   GEN6_DECODE_RC6_VID(((rc6vids >> 16) & 0xff)));
	}

	return i915_forcewake_domains(m, NULL);
}

static int i915_drpc_info(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	int err;

	intel_runtime_pm_get(dev_priv);

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		err = vlv_drpc_info(m);
	else if (INTEL_GEN(dev_priv) >= 6)
		err = gen6_drpc_info(m);
	else
		err = ironlake_drpc_info(m);

	intel_runtime_pm_put(dev_priv);

	return err;
}

static int i915_frontbuffer_tracking(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);

	seq_printf(m, "FB tracking busy bits: 0x%08x\n",
		   dev_priv->fb_tracking.busy_bits);

	seq_printf(m, "FB tracking flip bits: 0x%08x\n",
		   dev_priv->fb_tracking.flip_bits);

	return 0;
}

static int i915_fbc_status(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct intel_fbc *fbc = &dev_priv->fbc;

	if (!HAS_FBC(dev_priv))
		return -ENODEV;

	intel_runtime_pm_get(dev_priv);
	mutex_lock(&fbc->lock);

	if (intel_fbc_is_active(dev_priv))
		seq_puts(m, "FBC enabled\n");
	else
		seq_printf(m, "FBC disabled: %s\n", fbc->no_fbc_reason);

	if (fbc->work.scheduled)
		seq_printf(m, "FBC worker scheduled on vblank %llu, now %llu\n",
			   fbc->work.scheduled_vblank,
			   drm_crtc_vblank_count(&fbc->crtc->base));

	if (intel_fbc_is_active(dev_priv)) {
		u32 mask;

		if (INTEL_GEN(dev_priv) >= 8)
			mask = I915_READ(IVB_FBC_STATUS2) & BDW_FBC_COMP_SEG_MASK;
		else if (INTEL_GEN(dev_priv) >= 7)
			mask = I915_READ(IVB_FBC_STATUS2) & IVB_FBC_COMP_SEG_MASK;
		else if (INTEL_GEN(dev_priv) >= 5)
			mask = I915_READ(ILK_DPFC_STATUS) & ILK_DPFC_COMP_SEG_MASK;
		else if (IS_G4X(dev_priv))
			mask = I915_READ(DPFC_STATUS) & DPFC_COMP_SEG_MASK;
		else
			mask = I915_READ(FBC_STATUS) & (FBC_STAT_COMPRESSING |
							FBC_STAT_COMPRESSED);

		seq_printf(m, "Compressing: %s\n", yesno(mask));
	}

	mutex_unlock(&fbc->lock);
	intel_runtime_pm_put(dev_priv);

	return 0;
}

static int i915_fbc_false_color_get(void *data, u64 *val)
{
	struct drm_i915_private *dev_priv = data;

	if (INTEL_GEN(dev_priv) < 7 || !HAS_FBC(dev_priv))
		return -ENODEV;

	*val = dev_priv->fbc.false_color;

	return 0;
}

static int i915_fbc_false_color_set(void *data, u64 val)
{
	struct drm_i915_private *dev_priv = data;
	u32 reg;

	if (INTEL_GEN(dev_priv) < 7 || !HAS_FBC(dev_priv))
		return -ENODEV;

	mutex_lock(&dev_priv->fbc.lock);

	reg = I915_READ(ILK_DPFC_CONTROL);
	dev_priv->fbc.false_color = val;

	I915_WRITE(ILK_DPFC_CONTROL, val ?
		   (reg | FBC_CTL_FALSE_COLOR) :
		   (reg & ~FBC_CTL_FALSE_COLOR));

	mutex_unlock(&dev_priv->fbc.lock);
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(i915_fbc_false_color_fops,
			i915_fbc_false_color_get, i915_fbc_false_color_set,
			"%llu\n");

static int i915_ips_status(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);

	if (!HAS_IPS(dev_priv))
		return -ENODEV;

	intel_runtime_pm_get(dev_priv);

	seq_printf(m, "Enabled by kernel parameter: %s\n",
		   yesno(i915_modparams.enable_ips));

	if (INTEL_GEN(dev_priv) >= 8) {
		seq_puts(m, "Currently: unknown\n");
	} else {
		if (I915_READ(IPS_CTL) & IPS_ENABLE)
			seq_puts(m, "Currently: enabled\n");
		else
			seq_puts(m, "Currently: disabled\n");
	}

	intel_runtime_pm_put(dev_priv);

	return 0;
}

static int i915_sr_status(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	bool sr_enabled = false;

	intel_runtime_pm_get(dev_priv);
	intel_display_power_get(dev_priv, POWER_DOMAIN_INIT);

	if (INTEL_GEN(dev_priv) >= 9)
		/* no global SR status; inspect per-plane WM */;
	else if (HAS_PCH_SPLIT(dev_priv))
		sr_enabled = I915_READ(WM1_LP_ILK) & WM1_LP_SR_EN;
	else if (IS_I965GM(dev_priv) || IS_G4X(dev_priv) ||
		 IS_I945G(dev_priv) || IS_I945GM(dev_priv))
		sr_enabled = I915_READ(FW_BLC_SELF) & FW_BLC_SELF_EN;
	else if (IS_I915GM(dev_priv))
		sr_enabled = I915_READ(INSTPM) & INSTPM_SELF_EN;
	else if (IS_PINEVIEW(dev_priv))
		sr_enabled = I915_READ(DSPFW3) & PINEVIEW_SELF_REFRESH_EN;
	else if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		sr_enabled = I915_READ(FW_BLC_SELF_VLV) & FW_CSPWRDWNEN;

	intel_display_power_put(dev_priv, POWER_DOMAIN_INIT);
	intel_runtime_pm_put(dev_priv);

	seq_printf(m, "self-refresh: %s\n", enableddisabled(sr_enabled));

	return 0;
}

static int i915_emon_status(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct drm_device *dev = &dev_priv->drm;
	unsigned long temp, chipset, gfx;
	int ret;

	if (!IS_GEN5(dev_priv))
		return -ENODEV;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	temp = i915_mch_val(dev_priv);
	chipset = i915_chipset_val(dev_priv);
	gfx = i915_gfx_val(dev_priv);
	mutex_unlock(&dev->struct_mutex);

	seq_printf(m, "GMCH temp: %ld\n", temp);
	seq_printf(m, "Chipset power: %ld\n", chipset);
	seq_printf(m, "GFX power: %ld\n", gfx);
	seq_printf(m, "Total power: %ld\n", chipset + gfx);

	return 0;
}

static int i915_ring_freq_table(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct intel_rps *rps = &dev_priv->gt_pm.rps;
	unsigned int max_gpu_freq, min_gpu_freq;
	int gpu_freq, ia_freq;
	int ret;

	if (!HAS_LLC(dev_priv))
		return -ENODEV;

	intel_runtime_pm_get(dev_priv);

	ret = mutex_lock_interruptible(&dev_priv->pcu_lock);
	if (ret)
		goto out;

	min_gpu_freq = rps->min_freq;
	max_gpu_freq = rps->max_freq;
	if (IS_GEN9_BC(dev_priv) || INTEL_GEN(dev_priv) >= 10) {
		/* Convert GT frequency to 50 HZ units */
		min_gpu_freq /= GEN9_FREQ_SCALER;
		max_gpu_freq /= GEN9_FREQ_SCALER;
	}

	seq_puts(m, "GPU freq (MHz)\tEffective CPU freq (MHz)\tEffective Ring freq (MHz)\n");

	for (gpu_freq = min_gpu_freq; gpu_freq <= max_gpu_freq; gpu_freq++) {
		ia_freq = gpu_freq;
		sandybridge_pcode_read(dev_priv,
				       GEN6_PCODE_READ_MIN_FREQ_TABLE,
				       &ia_freq);
		seq_printf(m, "%d\t\t%d\t\t\t\t%d\n",
			   intel_gpu_freq(dev_priv, (gpu_freq *
						     (IS_GEN9_BC(dev_priv) ||
						      INTEL_GEN(dev_priv) >= 10 ?
						      GEN9_FREQ_SCALER : 1))),
			   ((ia_freq >> 0) & 0xff) * 100,
			   ((ia_freq >> 8) & 0xff) * 100);
	}

	mutex_unlock(&dev_priv->pcu_lock);

out:
	intel_runtime_pm_put(dev_priv);
	return ret;
}

static int i915_opregion(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct drm_device *dev = &dev_priv->drm;
	struct intel_opregion *opregion = &dev_priv->opregion;
	int ret;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		goto out;

	if (opregion->header)
		seq_write(m, opregion->header, OPREGION_SIZE);

	mutex_unlock(&dev->struct_mutex);

out:
	return 0;
}

static int i915_vbt(struct seq_file *m, void *unused)
{
	struct intel_opregion *opregion = &node_to_i915(m->private)->opregion;

	if (opregion->vbt)
		seq_write(m, opregion->vbt, opregion->vbt_size);

	return 0;
}

static int i915_gem_framebuffer_info(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct drm_device *dev = &dev_priv->drm;
	struct intel_framebuffer *fbdev_fb = NULL;
	struct drm_framebuffer *drm_fb;
	int ret;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

#ifdef CONFIG_DRM_FBDEV_EMULATION
	if (dev_priv->fbdev && dev_priv->fbdev->helper.fb) {
		fbdev_fb = to_intel_framebuffer(dev_priv->fbdev->helper.fb);

		seq_printf(m, "fbcon size: %d x %d, depth %d, %d bpp, modifier 0x%llx, refcount %d, obj ",
			   fbdev_fb->base.width,
			   fbdev_fb->base.height,
			   fbdev_fb->base.format->depth,
			   fbdev_fb->base.format->cpp[0] * 8,
			   fbdev_fb->base.modifier,
			   drm_framebuffer_read_refcount(&fbdev_fb->base));
		describe_obj(m, fbdev_fb->obj);
		seq_putc(m, '\n');
	}
#endif

	mutex_lock(&dev->mode_config.fb_lock);
	drm_for_each_fb(drm_fb, dev) {
		struct intel_framebuffer *fb = to_intel_framebuffer(drm_fb);
		if (fb == fbdev_fb)
			continue;

		seq_printf(m, "user size: %d x %d, depth %d, %d bpp, modifier 0x%llx, refcount %d, obj ",
			   fb->base.width,
			   fb->base.height,
			   fb->base.format->depth,
			   fb->base.format->cpp[0] * 8,
			   fb->base.modifier,
			   drm_framebuffer_read_refcount(&fb->base));
		describe_obj(m, fb->obj);
		seq_putc(m, '\n');
	}
	mutex_unlock(&dev->mode_config.fb_lock);
	mutex_unlock(&dev->struct_mutex);

	return 0;
}

static void describe_ctx_ring(struct seq_file *m, struct intel_ring *ring)
{
	seq_printf(m, " (ringbuffer, space: %d, head: %u, tail: %u, emit: %u)",
		   ring->space, ring->head, ring->tail, ring->emit);
}

static int i915_context_status(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct drm_device *dev = &dev_priv->drm;
	struct intel_engine_cs *engine;
	struct i915_gem_context *ctx;
	enum intel_engine_id id;
	int ret;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	list_for_each_entry(ctx, &dev_priv->contexts.list, link) {
		seq_printf(m, "HW context %u ", ctx->hw_id);
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

		for_each_engine(engine, dev_priv, id) {
			struct intel_context *ce =
				to_intel_context(ctx, engine);

			seq_printf(m, "%s: ", engine->name);
			if (ce->state)
				describe_obj(m, ce->state->obj);
			if (ce->ring)
				describe_ctx_ring(m, ce->ring);
			seq_putc(m, '\n');
		}

		seq_putc(m, '\n');
	}

	mutex_unlock(&dev->struct_mutex);

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

	intel_runtime_pm_get(dev_priv);

	seq_printf(m, "bit6 swizzle for X-tiling = %s\n",
		   swizzle_string(dev_priv->mm.bit_6_swizzle_x));
	seq_printf(m, "bit6 swizzle for Y-tiling = %s\n",
		   swizzle_string(dev_priv->mm.bit_6_swizzle_y));

	if (IS_GEN3(dev_priv) || IS_GEN4(dev_priv)) {
		seq_printf(m, "DDC = 0x%08x\n",
			   I915_READ(DCC));
		seq_printf(m, "DDC2 = 0x%08x\n",
			   I915_READ(DCC2));
		seq_printf(m, "C0DRB3 = 0x%04x\n",
			   I915_READ16(C0DRB3));
		seq_printf(m, "C1DRB3 = 0x%04x\n",
			   I915_READ16(C1DRB3));
	} else if (INTEL_GEN(dev_priv) >= 6) {
		seq_printf(m, "MAD_DIMM_C0 = 0x%08x\n",
			   I915_READ(MAD_DIMM_C0));
		seq_printf(m, "MAD_DIMM_C1 = 0x%08x\n",
			   I915_READ(MAD_DIMM_C1));
		seq_printf(m, "MAD_DIMM_C2 = 0x%08x\n",
			   I915_READ(MAD_DIMM_C2));
		seq_printf(m, "TILECTL = 0x%08x\n",
			   I915_READ(TILECTL));
		if (INTEL_GEN(dev_priv) >= 8)
			seq_printf(m, "GAMTARBMODE = 0x%08x\n",
				   I915_READ(GAMTARBMODE));
		else
			seq_printf(m, "ARB_MODE = 0x%08x\n",
				   I915_READ(ARB_MODE));
		seq_printf(m, "DISP_ARB_CTL = 0x%08x\n",
			   I915_READ(DISP_ARB_CTL));
	}

	if (dev_priv->quirks & QUIRK_PIN_SWIZZLED_PAGES)
		seq_puts(m, "L-shaped memory detected\n");

	intel_runtime_pm_put(dev_priv);

	return 0;
}

static int per_file_ctx(int id, void *ptr, void *data)
{
	struct i915_gem_context *ctx = ptr;
	struct seq_file *m = data;
	struct i915_hw_ppgtt *ppgtt = ctx->ppgtt;

	if (!ppgtt) {
		seq_printf(m, "  no ppgtt for context %d\n",
			   ctx->user_handle);
		return 0;
	}

	if (i915_gem_context_is_default(ctx))
		seq_puts(m, "  default context:\n");
	else
		seq_printf(m, "  context %d:\n", ctx->user_handle);
	ppgtt->debug_dump(ppgtt, m);

	return 0;
}

static void gen8_ppgtt_info(struct seq_file *m,
			    struct drm_i915_private *dev_priv)
{
	struct i915_hw_ppgtt *ppgtt = dev_priv->mm.aliasing_ppgtt;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int i;

	if (!ppgtt)
		return;

	for_each_engine(engine, dev_priv, id) {
		seq_printf(m, "%s\n", engine->name);
		for (i = 0; i < 4; i++) {
			u64 pdp = I915_READ(GEN8_RING_PDP_UDW(engine, i));
			pdp <<= 32;
			pdp |= I915_READ(GEN8_RING_PDP_LDW(engine, i));
			seq_printf(m, "\tPDP%d 0x%016llx\n", i, pdp);
		}
	}
}

static void gen6_ppgtt_info(struct seq_file *m,
			    struct drm_i915_private *dev_priv)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	if (IS_GEN6(dev_priv))
		seq_printf(m, "GFX_MODE: 0x%08x\n", I915_READ(GFX_MODE));

	for_each_engine(engine, dev_priv, id) {
		seq_printf(m, "%s\n", engine->name);
		if (IS_GEN7(dev_priv))
			seq_printf(m, "GFX_MODE: 0x%08x\n",
				   I915_READ(RING_MODE_GEN7(engine)));
		seq_printf(m, "PP_DIR_BASE: 0x%08x\n",
			   I915_READ(RING_PP_DIR_BASE(engine)));
		seq_printf(m, "PP_DIR_BASE_READ: 0x%08x\n",
			   I915_READ(RING_PP_DIR_BASE_READ(engine)));
		seq_printf(m, "PP_DIR_DCLV: 0x%08x\n",
			   I915_READ(RING_PP_DIR_DCLV(engine)));
	}
	if (dev_priv->mm.aliasing_ppgtt) {
		struct i915_hw_ppgtt *ppgtt = dev_priv->mm.aliasing_ppgtt;

		seq_puts(m, "aliasing PPGTT:\n");
		seq_printf(m, "pd gtt offset: 0x%08x\n", ppgtt->pd.base.ggtt_offset);

		ppgtt->debug_dump(ppgtt, m);
	}

	seq_printf(m, "ECOCHK: 0x%08x\n", I915_READ(GAM_ECOCHK));
}

static int i915_ppgtt_info(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct drm_device *dev = &dev_priv->drm;
	struct drm_file *file;
	int ret;

	mutex_lock(&dev->filelist_mutex);
	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		goto out_unlock;

	intel_runtime_pm_get(dev_priv);

	if (INTEL_GEN(dev_priv) >= 8)
		gen8_ppgtt_info(m, dev_priv);
	else if (INTEL_GEN(dev_priv) >= 6)
		gen6_ppgtt_info(m, dev_priv);

	list_for_each_entry_reverse(file, &dev->filelist, lhead) {
		struct drm_i915_file_private *file_priv = file->driver_priv;
		struct task_struct *task;

		task = get_pid_task(file->pid, PIDTYPE_PID);
		if (!task) {
			ret = -ESRCH;
			goto out_rpm;
		}
		seq_printf(m, "\nproc: %s\n", task->comm);
		put_task_struct(task);
		idr_for_each(&file_priv->context_idr, per_file_ctx,
			     (void *)(unsigned long)m);
	}

out_rpm:
	intel_runtime_pm_put(dev_priv);
	mutex_unlock(&dev->struct_mutex);
out_unlock:
	mutex_unlock(&dev->filelist_mutex);
	return ret;
}

static int count_irq_waiters(struct drm_i915_private *i915)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int count = 0;

	for_each_engine(engine, i915, id)
		count += intel_engine_has_waiter(engine);

	return count;
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
	struct drm_device *dev = &dev_priv->drm;
	struct intel_rps *rps = &dev_priv->gt_pm.rps;
	struct drm_file *file;

	seq_printf(m, "RPS enabled? %d\n", rps->enabled);
	seq_printf(m, "GPU busy? %s [%d requests]\n",
		   yesno(dev_priv->gt.awake), dev_priv->gt.active_requests);
	seq_printf(m, "CPU waiting? %d\n", count_irq_waiters(dev_priv));
	seq_printf(m, "Boosts outstanding? %d\n",
		   atomic_read(&rps->num_waiters));
	seq_printf(m, "Frequency requested %d\n",
		   intel_gpu_freq(dev_priv, rps->cur_freq));
	seq_printf(m, "  min hard:%d, soft:%d; max soft:%d, hard:%d\n",
		   intel_gpu_freq(dev_priv, rps->min_freq),
		   intel_gpu_freq(dev_priv, rps->min_freq_softlimit),
		   intel_gpu_freq(dev_priv, rps->max_freq_softlimit),
		   intel_gpu_freq(dev_priv, rps->max_freq));
	seq_printf(m, "  idle:%d, efficient:%d, boost:%d\n",
		   intel_gpu_freq(dev_priv, rps->idle_freq),
		   intel_gpu_freq(dev_priv, rps->efficient_freq),
		   intel_gpu_freq(dev_priv, rps->boost_freq));

	mutex_lock(&dev->filelist_mutex);
	list_for_each_entry_reverse(file, &dev->filelist, lhead) {
		struct drm_i915_file_private *file_priv = file->driver_priv;
		struct task_struct *task;

		rcu_read_lock();
		task = pid_task(file->pid, PIDTYPE_PID);
		seq_printf(m, "%s [%d]: %d boosts\n",
			   task ? task->comm : "<unknown>",
			   task ? task->pid : -1,
			   atomic_read(&file_priv->rps_client.boosts));
		rcu_read_unlock();
	}
	seq_printf(m, "Kernel (anonymous) boosts: %d\n",
		   atomic_read(&rps->boosts));
	mutex_unlock(&dev->filelist_mutex);

	if (INTEL_GEN(dev_priv) >= 6 &&
	    rps->enabled &&
	    dev_priv->gt.active_requests) {
		u32 rpup, rpupei;
		u32 rpdown, rpdownei;

		intel_uncore_forcewake_get(dev_priv, FORCEWAKE_ALL);
		rpup = I915_READ_FW(GEN6_RP_CUR_UP) & GEN6_RP_EI_MASK;
		rpupei = I915_READ_FW(GEN6_RP_CUR_UP_EI) & GEN6_RP_EI_MASK;
		rpdown = I915_READ_FW(GEN6_RP_CUR_DOWN) & GEN6_RP_EI_MASK;
		rpdownei = I915_READ_FW(GEN6_RP_CUR_DOWN_EI) & GEN6_RP_EI_MASK;
		intel_uncore_forcewake_put(dev_priv, FORCEWAKE_ALL);

		seq_printf(m, "\nRPS Autotuning (current \"%s\" window):\n",
			   rps_power_to_str(rps->power));
		seq_printf(m, "  Avg. up: %d%% [above threshold? %d%%]\n",
			   rpup && rpupei ? 100 * rpup / rpupei : 0,
			   rps->up_threshold);
		seq_printf(m, "  Avg. down: %d%% [below threshold? %d%%]\n",
			   rpdown && rpdownei ? 100 * rpdown / rpdownei : 0,
			   rps->down_threshold);
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
	seq_printf(m, "%s: %lluMB\n", edram ? "eDRAM" : "eLLC",
		   intel_uncore_edram_size(dev_priv)/1024/1024);

	return 0;
}

static int i915_huc_load_status_info(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct drm_printer p;

	if (!HAS_HUC(dev_priv))
		return -ENODEV;

	p = drm_seq_file_printer(m);
	intel_uc_fw_dump(&dev_priv->huc.fw, &p);

	intel_runtime_pm_get(dev_priv);
	seq_printf(m, "\nHuC status 0x%08x:\n", I915_READ(HUC_STATUS2));
	intel_runtime_pm_put(dev_priv);

	return 0;
}

static int i915_guc_load_status_info(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct drm_printer p;
	u32 tmp, i;

	if (!HAS_GUC(dev_priv))
		return -ENODEV;

	p = drm_seq_file_printer(m);
	intel_uc_fw_dump(&dev_priv->guc.fw, &p);

	intel_runtime_pm_get(dev_priv);

	tmp = I915_READ(GUC_STATUS);

	seq_printf(m, "\nGuC status 0x%08x:\n", tmp);
	seq_printf(m, "\tBootrom status = 0x%x\n",
		(tmp & GS_BOOTROM_MASK) >> GS_BOOTROM_SHIFT);
	seq_printf(m, "\tuKernel status = 0x%x\n",
		(tmp & GS_UKERNEL_MASK) >> GS_UKERNEL_SHIFT);
	seq_printf(m, "\tMIA Core status = 0x%x\n",
		(tmp & GS_MIA_MASK) >> GS_MIA_SHIFT);
	seq_puts(m, "\nScratch registers:\n");
	for (i = 0; i < 16; i++)
		seq_printf(m, "\t%2d: \t0x%x\n", i, I915_READ(SOFT_SCRATCH(i)));

	intel_runtime_pm_put(dev_priv);

	return 0;
}

static const char *
stringify_guc_log_type(enum guc_log_buffer_type type)
{
	switch (type) {
	case GUC_ISR_LOG_BUFFER:
		return "ISR";
	case GUC_DPC_LOG_BUFFER:
		return "DPC";
	case GUC_CRASH_DUMP_LOG_BUFFER:
		return "CRASH";
	default:
		MISSING_CASE(type);
	}

	return "";
}

static void i915_guc_log_info(struct seq_file *m,
			      struct drm_i915_private *dev_priv)
{
	struct intel_guc_log *log = &dev_priv->guc.log;
	enum guc_log_buffer_type type;

	if (!intel_guc_log_relay_enabled(log)) {
		seq_puts(m, "GuC log relay disabled\n");
		return;
	}

	seq_puts(m, "GuC logging stats:\n");

	seq_printf(m, "\tRelay full count: %u\n",
		   log->relay.full_count);

	for (type = GUC_ISR_LOG_BUFFER; type < GUC_MAX_LOG_BUFFER; type++) {
		seq_printf(m, "\t%s:\tflush count %10u, overflow count %10u\n",
			   stringify_guc_log_type(type),
			   log->stats[type].flush,
			   log->stats[type].sampled_overflow);
	}
}

static void i915_guc_client_info(struct seq_file *m,
				 struct drm_i915_private *dev_priv,
				 struct intel_guc_client *client)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	uint64_t tot = 0;

	seq_printf(m, "\tPriority %d, GuC stage index: %u, PD offset 0x%x\n",
		client->priority, client->stage_id, client->proc_desc_offset);
	seq_printf(m, "\tDoorbell id %d, offset: 0x%lx\n",
		client->doorbell_id, client->doorbell_offset);

	for_each_engine(engine, dev_priv, id) {
		u64 submissions = client->submissions[id];
		tot += submissions;
		seq_printf(m, "\tSubmissions: %llu %s\n",
				submissions, engine->name);
	}
	seq_printf(m, "\tTotal: %llu\n", tot);
}

static int i915_guc_info(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	const struct intel_guc *guc = &dev_priv->guc;

	if (!USES_GUC(dev_priv))
		return -ENODEV;

	i915_guc_log_info(m, dev_priv);

	if (!USES_GUC_SUBMISSION(dev_priv))
		return 0;

	GEM_BUG_ON(!guc->execbuf_client);

	seq_printf(m, "\nDoorbell map:\n");
	seq_printf(m, "\t%*pb\n", GUC_NUM_DOORBELLS, guc->doorbell_bitmap);
	seq_printf(m, "Doorbell next cacheline: 0x%x\n", guc->db_cacheline);

	seq_printf(m, "\nGuC execbuf client @ %p:\n", guc->execbuf_client);
	i915_guc_client_info(m, dev_priv, guc->execbuf_client);
	if (guc->preempt_client) {
		seq_printf(m, "\nGuC preempt client @ %p:\n",
			   guc->preempt_client);
		i915_guc_client_info(m, dev_priv, guc->preempt_client);
	}

	/* Add more as required ... */

	return 0;
}

static int i915_guc_stage_pool(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	const struct intel_guc *guc = &dev_priv->guc;
	struct guc_stage_desc *desc = guc->stage_desc_pool_vaddr;
	struct intel_guc_client *client = guc->execbuf_client;
	unsigned int tmp;
	int index;

	if (!USES_GUC_SUBMISSION(dev_priv))
		return -ENODEV;

	for (index = 0; index < GUC_MAX_STAGE_DESCRIPTORS; index++, desc++) {
		struct intel_engine_cs *engine;

		if (!(desc->attribute & GUC_STAGE_DESC_ATTR_ACTIVE))
			continue;

		seq_printf(m, "GuC stage descriptor %u:\n", index);
		seq_printf(m, "\tIndex: %u\n", desc->stage_id);
		seq_printf(m, "\tAttribute: 0x%x\n", desc->attribute);
		seq_printf(m, "\tPriority: %d\n", desc->priority);
		seq_printf(m, "\tDoorbell id: %d\n", desc->db_id);
		seq_printf(m, "\tEngines used: 0x%x\n",
			   desc->engines_used);
		seq_printf(m, "\tDoorbell trigger phy: 0x%llx, cpu: 0x%llx, uK: 0x%x\n",
			   desc->db_trigger_phy,
			   desc->db_trigger_cpu,
			   desc->db_trigger_uk);
		seq_printf(m, "\tProcess descriptor: 0x%x\n",
			   desc->process_desc);
		seq_printf(m, "\tWorkqueue address: 0x%x, size: 0x%x\n",
			   desc->wq_addr, desc->wq_size);
		seq_putc(m, '\n');

		for_each_engine_masked(engine, dev_priv, client->engines, tmp) {
			u32 guc_engine_id = engine->guc_id;
			struct guc_execlist_context *lrc =
						&desc->lrc[guc_engine_id];

			seq_printf(m, "\t%s LRC:\n", engine->name);
			seq_printf(m, "\t\tContext desc: 0x%x\n",
				   lrc->context_desc);
			seq_printf(m, "\t\tContext id: 0x%x\n", lrc->context_id);
			seq_printf(m, "\t\tLRCA: 0x%x\n", lrc->ring_lrca);
			seq_printf(m, "\t\tRing begin: 0x%x\n", lrc->ring_begin);
			seq_printf(m, "\t\tRing end: 0x%x\n", lrc->ring_end);
			seq_putc(m, '\n');
		}
	}

	return 0;
}

static int i915_guc_log_dump(struct seq_file *m, void *data)
{
	struct drm_info_node *node = m->private;
	struct drm_i915_private *dev_priv = node_to_i915(node);
	bool dump_load_err = !!node->info_ent->data;
	struct drm_i915_gem_object *obj = NULL;
	u32 *log;
	int i = 0;

	if (!HAS_GUC(dev_priv))
		return -ENODEV;

	if (dump_load_err)
		obj = dev_priv->guc.load_err_log;
	else if (dev_priv->guc.log.vma)
		obj = dev_priv->guc.log.vma->obj;

	if (!obj)
		return 0;

	log = i915_gem_object_pin_map(obj, I915_MAP_WC);
	if (IS_ERR(log)) {
		DRM_DEBUG("Failed to pin object\n");
		seq_puts(m, "(log data unaccessible)\n");
		return PTR_ERR(log);
	}

	for (i = 0; i < obj->base.size / sizeof(u32); i += 4)
		seq_printf(m, "0x%08x 0x%08x 0x%08x 0x%08x\n",
			   *(log + i), *(log + i + 1),
			   *(log + i + 2), *(log + i + 3));

	seq_putc(m, '\n');

	i915_gem_object_unpin_map(obj);

	return 0;
}

static int i915_guc_log_level_get(void *data, u64 *val)
{
	struct drm_i915_private *dev_priv = data;

	if (!USES_GUC(dev_priv))
		return -ENODEV;

	*val = intel_guc_log_level_get(&dev_priv->guc.log);

	return 0;
}

static int i915_guc_log_level_set(void *data, u64 val)
{
	struct drm_i915_private *dev_priv = data;

	if (!USES_GUC(dev_priv))
		return -ENODEV;

	return intel_guc_log_level_set(&dev_priv->guc.log, val);
}

DEFINE_SIMPLE_ATTRIBUTE(i915_guc_log_level_fops,
			i915_guc_log_level_get, i915_guc_log_level_set,
			"%lld\n");

static int i915_guc_log_relay_open(struct inode *inode, struct file *file)
{
	struct drm_i915_private *dev_priv = inode->i_private;

	if (!USES_GUC(dev_priv))
		return -ENODEV;

	file->private_data = &dev_priv->guc.log;

	return intel_guc_log_relay_open(&dev_priv->guc.log);
}

static ssize_t
i915_guc_log_relay_write(struct file *filp,
			 const char __user *ubuf,
			 size_t cnt,
			 loff_t *ppos)
{
	struct intel_guc_log *log = filp->private_data;

	intel_guc_log_relay_flush(log);

	return cnt;
}

static int i915_guc_log_relay_release(struct inode *inode, struct file *file)
{
	struct drm_i915_private *dev_priv = inode->i_private;

	intel_guc_log_relay_close(&dev_priv->guc.log);

	return 0;
}

static const struct file_operations i915_guc_log_relay_fops = {
	.owner = THIS_MODULE,
	.open = i915_guc_log_relay_open,
	.write = i915_guc_log_relay_write,
	.release = i915_guc_log_relay_release,
};

static const char *psr2_live_status(u32 val)
{
	static const char * const live_status[] = {
		"IDLE",
		"CAPTURE",
		"CAPTURE_FS",
		"SLEEP",
		"BUFON_FW",
		"ML_UP",
		"SU_STANDBY",
		"FAST_SLEEP",
		"DEEP_SLEEP",
		"BUF_ON",
		"TG_ON"
	};

	val = (val & EDP_PSR2_STATUS_STATE_MASK) >> EDP_PSR2_STATUS_STATE_SHIFT;
	if (val < ARRAY_SIZE(live_status))
		return live_status[val];

	return "unknown";
}

static const char *psr_sink_status(u8 val)
{
	static const char * const sink_status[] = {
		"inactive",
		"transition to active, capture and display",
		"active, display from RFB",
		"active, capture and display on sink device timings",
		"transition to inactive, capture and display, timing re-sync",
		"reserved",
		"reserved",
		"sink internal error"
	};

	val &= DP_PSR_SINK_STATE_MASK;
	if (val < ARRAY_SIZE(sink_status))
		return sink_status[val];

	return "unknown";
}

static int i915_edp_psr_status(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	u32 psrperf = 0;
	u32 stat[3];
	enum pipe pipe;
	bool enabled = false;
	bool sink_support;

	if (!HAS_PSR(dev_priv))
		return -ENODEV;

	sink_support = dev_priv->psr.sink_support;
	seq_printf(m, "Sink_Support: %s\n", yesno(sink_support));
	if (!sink_support)
		return 0;

	intel_runtime_pm_get(dev_priv);

	mutex_lock(&dev_priv->psr.lock);
	seq_printf(m, "Enabled: %s\n", yesno((bool)dev_priv->psr.enabled));
	seq_printf(m, "Busy frontbuffer bits: 0x%03x\n",
		   dev_priv->psr.busy_frontbuffer_bits);
	seq_printf(m, "Re-enable work scheduled: %s\n",
		   yesno(work_busy(&dev_priv->psr.work.work)));

	if (HAS_DDI(dev_priv)) {
		if (dev_priv->psr.psr2_enabled)
			enabled = I915_READ(EDP_PSR2_CTL) & EDP_PSR2_ENABLE;
		else
			enabled = I915_READ(EDP_PSR_CTL) & EDP_PSR_ENABLE;
	} else {
		for_each_pipe(dev_priv, pipe) {
			enum transcoder cpu_transcoder =
				intel_pipe_to_cpu_transcoder(dev_priv, pipe);
			enum intel_display_power_domain power_domain;

			power_domain = POWER_DOMAIN_TRANSCODER(cpu_transcoder);
			if (!intel_display_power_get_if_enabled(dev_priv,
								power_domain))
				continue;

			stat[pipe] = I915_READ(VLV_PSRSTAT(pipe)) &
				VLV_EDP_PSR_CURR_STATE_MASK;
			if ((stat[pipe] == VLV_EDP_PSR_ACTIVE_NORFB_UP) ||
			    (stat[pipe] == VLV_EDP_PSR_ACTIVE_SF_UPDATE))
				enabled = true;

			intel_display_power_put(dev_priv, power_domain);
		}
	}

	seq_printf(m, "Main link in standby mode: %s\n",
		   yesno(dev_priv->psr.link_standby));

	seq_printf(m, "HW Enabled & Active bit: %s", yesno(enabled));

	if (!HAS_DDI(dev_priv))
		for_each_pipe(dev_priv, pipe) {
			if ((stat[pipe] == VLV_EDP_PSR_ACTIVE_NORFB_UP) ||
			    (stat[pipe] == VLV_EDP_PSR_ACTIVE_SF_UPDATE))
				seq_printf(m, " pipe %c", pipe_name(pipe));
		}
	seq_puts(m, "\n");

	/*
	 * VLV/CHV PSR has no kind of performance counter
	 * SKL+ Perf counter is reset to 0 everytime DC state is entered
	 */
	if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv)) {
		psrperf = I915_READ(EDP_PSR_PERF_CNT) &
			EDP_PSR_PERF_CNT_MASK;

		seq_printf(m, "Performance_Counter: %u\n", psrperf);
	}
	if (dev_priv->psr.psr2_enabled) {
		u32 psr2 = I915_READ(EDP_PSR2_STATUS);

		seq_printf(m, "EDP_PSR2_STATUS: %x [%s]\n",
			   psr2, psr2_live_status(psr2));
	}

	if (dev_priv->psr.enabled) {
		struct drm_dp_aux *aux = &dev_priv->psr.enabled->aux;
		u8 val;

		if (drm_dp_dpcd_readb(aux, DP_PSR_STATUS, &val) == 1)
			seq_printf(m, "Sink PSR status: 0x%x [%s]\n", val,
				   psr_sink_status(val));
	}
	mutex_unlock(&dev_priv->psr.lock);

	if (READ_ONCE(dev_priv->psr.debug)) {
		seq_printf(m, "Last attempted entry at: %lld\n",
			   dev_priv->psr.last_entry_attempt);
		seq_printf(m, "Last exit at: %lld\n",
			   dev_priv->psr.last_exit);
	}

	intel_runtime_pm_put(dev_priv);
	return 0;
}

static int
i915_edp_psr_debug_set(void *data, u64 val)
{
	struct drm_i915_private *dev_priv = data;

	if (!CAN_PSR(dev_priv))
		return -ENODEV;

	DRM_DEBUG_KMS("PSR debug %s\n", enableddisabled(val));

	intel_runtime_pm_get(dev_priv);
	intel_psr_irq_control(dev_priv, !!val);
	intel_runtime_pm_put(dev_priv);

	return 0;
}

static int
i915_edp_psr_debug_get(void *data, u64 *val)
{
	struct drm_i915_private *dev_priv = data;

	if (!CAN_PSR(dev_priv))
		return -ENODEV;

	*val = READ_ONCE(dev_priv->psr.debug);
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(i915_edp_psr_debug_fops,
			i915_edp_psr_debug_get, i915_edp_psr_debug_set,
			"%llu\n");

static int i915_sink_crc(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct drm_device *dev = &dev_priv->drm;
	struct intel_connector *connector;
	struct drm_connector_list_iter conn_iter;
	struct intel_dp *intel_dp = NULL;
	struct drm_modeset_acquire_ctx ctx;
	int ret;
	u8 crc[6];

	drm_modeset_acquire_init(&ctx, DRM_MODESET_ACQUIRE_INTERRUPTIBLE);

	drm_connector_list_iter_begin(dev, &conn_iter);

	for_each_intel_connector_iter(connector, &conn_iter) {
		struct drm_crtc *crtc;
		struct drm_connector_state *state;
		struct intel_crtc_state *crtc_state;

		if (connector->base.connector_type != DRM_MODE_CONNECTOR_eDP)
			continue;

retry:
		ret = drm_modeset_lock(&dev->mode_config.connection_mutex, &ctx);
		if (ret)
			goto err;

		state = connector->base.state;
		if (!state->best_encoder)
			continue;

		crtc = state->crtc;
		ret = drm_modeset_lock(&crtc->mutex, &ctx);
		if (ret)
			goto err;

		crtc_state = to_intel_crtc_state(crtc->state);
		if (!crtc_state->base.active)
			continue;

		/*
		 * We need to wait for all crtc updates to complete, to make
		 * sure any pending modesets and plane updates are completed.
		 */
		if (crtc_state->base.commit) {
			ret = wait_for_completion_interruptible(&crtc_state->base.commit->hw_done);

			if (ret)
				goto err;
		}

		intel_dp = enc_to_intel_dp(state->best_encoder);

		ret = intel_dp_sink_crc(intel_dp, crtc_state, crc);
		if (ret)
			goto err;

		seq_printf(m, "%02x%02x%02x%02x%02x%02x\n",
			   crc[0], crc[1], crc[2],
			   crc[3], crc[4], crc[5]);
		goto out;

err:
		if (ret == -EDEADLK) {
			ret = drm_modeset_backoff(&ctx);
			if (!ret)
				goto retry;
		}
		goto out;
	}
	ret = -ENODEV;
out:
	drm_connector_list_iter_end(&conn_iter);
	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);

	return ret;
}

static int i915_energy_uJ(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	unsigned long long power;
	u32 units;

	if (INTEL_GEN(dev_priv) < 6)
		return -ENODEV;

	intel_runtime_pm_get(dev_priv);

	if (rdmsrl_safe(MSR_RAPL_POWER_UNIT, &power)) {
		intel_runtime_pm_put(dev_priv);
		return -ENODEV;
	}

	units = (power & 0x1f00) >> 8;
	power = I915_READ(MCH_SECP_NRG_STTS);
	power = (1000000 * power) >> units; /* convert to uJ */

	intel_runtime_pm_put(dev_priv);

	seq_printf(m, "%llu", power);

	return 0;
}

static int i915_runtime_pm_status(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct pci_dev *pdev = dev_priv->drm.pdev;

	if (!HAS_RUNTIME_PM(dev_priv))
		seq_puts(m, "Runtime power management not supported\n");

	seq_printf(m, "GPU idle: %s (epoch %u)\n",
		   yesno(!dev_priv->gt.awake), dev_priv->gt.epoch);
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

	return 0;
}

static int i915_power_domain_info(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct i915_power_domains *power_domains = &dev_priv->power_domains;
	int i;

	mutex_lock(&power_domains->lock);

	seq_printf(m, "%-25s %s\n", "Power well/domain", "Use count");
	for (i = 0; i < power_domains->power_well_count; i++) {
		struct i915_power_well *power_well;
		enum intel_display_power_domain power_domain;

		power_well = &power_domains->power_wells[i];
		seq_printf(m, "%-25s %d\n", power_well->name,
			   power_well->count);

		for_each_power_domain(power_domain, power_well->domains)
			seq_printf(m, "  %-23s %d\n",
				 intel_display_power_domain_str(power_domain),
				 power_domains->domain_use_count[power_domain]);
	}

	mutex_unlock(&power_domains->lock);

	return 0;
}

static int i915_dmc_info(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct intel_csr *csr;

	if (!HAS_CSR(dev_priv))
		return -ENODEV;

	csr = &dev_priv->csr;

	intel_runtime_pm_get(dev_priv);

	seq_printf(m, "fw loaded: %s\n", yesno(csr->dmc_payload != NULL));
	seq_printf(m, "path: %s\n", csr->fw_path);

	if (!csr->dmc_payload)
		goto out;

	seq_printf(m, "version: %d.%d\n", CSR_VERSION_MAJOR(csr->version),
		   CSR_VERSION_MINOR(csr->version));

	if (IS_KABYLAKE(dev_priv) ||
	    (IS_SKYLAKE(dev_priv) && csr->version >= CSR_VERSION(1, 6))) {
		seq_printf(m, "DC3 -> DC5 count: %d\n",
			   I915_READ(SKL_CSR_DC3_DC5_COUNT));
		seq_printf(m, "DC5 -> DC6 count: %d\n",
			   I915_READ(SKL_CSR_DC5_DC6_COUNT));
	} else if (IS_BROXTON(dev_priv) && csr->version >= CSR_VERSION(1, 4)) {
		seq_printf(m, "DC3 -> DC5 count: %d\n",
			   I915_READ(BXT_CSR_DC3_DC5_COUNT));
	}

out:
	seq_printf(m, "program base: 0x%08x\n", I915_READ(CSR_PROGRAM(0)));
	seq_printf(m, "ssp base: 0x%08x\n", I915_READ(CSR_SSP_BASE));
	seq_printf(m, "htp: 0x%08x\n", I915_READ(CSR_HTP_SKL));

	intel_runtime_pm_put(dev_priv);

	return 0;
}

static void intel_seq_print_mode(struct seq_file *m, int tabs,
				 struct drm_display_mode *mode)
{
	int i;

	for (i = 0; i < tabs; i++)
		seq_putc(m, '\t');

	seq_printf(m, "id %d:\"%s\" freq %d clock %d hdisp %d hss %d hse %d htot %d vdisp %d vss %d vse %d vtot %d type 0x%x flags 0x%x\n",
		   mode->base.id, mode->name,
		   mode->vrefresh, mode->clock,
		   mode->hdisplay, mode->hsync_start,
		   mode->hsync_end, mode->htotal,
		   mode->vdisplay, mode->vsync_start,
		   mode->vsync_end, mode->vtotal,
		   mode->type, mode->flags);
}

static void intel_encoder_info(struct seq_file *m,
			       struct intel_crtc *intel_crtc,
			       struct intel_encoder *intel_encoder)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct drm_device *dev = &dev_priv->drm;
	struct drm_crtc *crtc = &intel_crtc->base;
	struct intel_connector *intel_connector;
	struct drm_encoder *encoder;

	encoder = &intel_encoder->base;
	seq_printf(m, "\tencoder %d: type: %s, connectors:\n",
		   encoder->base.id, encoder->name);
	for_each_connector_on_encoder(dev, encoder, intel_connector) {
		struct drm_connector *connector = &intel_connector->base;
		seq_printf(m, "\t\tconnector %d: type: %s, status: %s",
			   connector->base.id,
			   connector->name,
			   drm_get_connector_status_name(connector->status));
		if (connector->status == connector_status_connected) {
			struct drm_display_mode *mode = &crtc->mode;
			seq_printf(m, ", mode:\n");
			intel_seq_print_mode(m, 2, mode);
		} else {
			seq_putc(m, '\n');
		}
	}
}

static void intel_crtc_info(struct seq_file *m, struct intel_crtc *intel_crtc)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct drm_device *dev = &dev_priv->drm;
	struct drm_crtc *crtc = &intel_crtc->base;
	struct intel_encoder *intel_encoder;
	struct drm_plane_state *plane_state = crtc->primary->state;
	struct drm_framebuffer *fb = plane_state->fb;

	if (fb)
		seq_printf(m, "\tfb: %d, pos: %dx%d, size: %dx%d\n",
			   fb->base.id, plane_state->src_x >> 16,
			   plane_state->src_y >> 16, fb->width, fb->height);
	else
		seq_puts(m, "\tprimary plane disabled\n");
	for_each_encoder_on_crtc(dev, crtc, intel_encoder)
		intel_encoder_info(m, intel_crtc, intel_encoder);
}

static void intel_panel_info(struct seq_file *m, struct intel_panel *panel)
{
	struct drm_display_mode *mode = panel->fixed_mode;

	seq_printf(m, "\tfixed mode:\n");
	intel_seq_print_mode(m, 2, mode);
}

static void intel_dp_info(struct seq_file *m,
			  struct intel_connector *intel_connector)
{
	struct intel_encoder *intel_encoder = intel_connector->encoder;
	struct intel_dp *intel_dp = enc_to_intel_dp(&intel_encoder->base);

	seq_printf(m, "\tDPCD rev: %x\n", intel_dp->dpcd[DP_DPCD_REV]);
	seq_printf(m, "\taudio support: %s\n", yesno(intel_dp->has_audio));
	if (intel_connector->base.connector_type == DRM_MODE_CONNECTOR_eDP)
		intel_panel_info(m, &intel_connector->panel);

	drm_dp_downstream_debug(m, intel_dp->dpcd, intel_dp->downstream_ports,
				&intel_dp->aux);
}

static void intel_dp_mst_info(struct seq_file *m,
			  struct intel_connector *intel_connector)
{
	struct intel_encoder *intel_encoder = intel_connector->encoder;
	struct intel_dp_mst_encoder *intel_mst =
		enc_to_mst(&intel_encoder->base);
	struct intel_digital_port *intel_dig_port = intel_mst->primary;
	struct intel_dp *intel_dp = &intel_dig_port->dp;
	bool has_audio = drm_dp_mst_port_has_audio(&intel_dp->mst_mgr,
					intel_connector->port);

	seq_printf(m, "\taudio support: %s\n", yesno(has_audio));
}

static void intel_hdmi_info(struct seq_file *m,
			    struct intel_connector *intel_connector)
{
	struct intel_encoder *intel_encoder = intel_connector->encoder;
	struct intel_hdmi *intel_hdmi = enc_to_intel_hdmi(&intel_encoder->base);

	seq_printf(m, "\taudio support: %s\n", yesno(intel_hdmi->has_audio));
}

static void intel_lvds_info(struct seq_file *m,
			    struct intel_connector *intel_connector)
{
	intel_panel_info(m, &intel_connector->panel);
}

static void intel_connector_info(struct seq_file *m,
				 struct drm_connector *connector)
{
	struct intel_connector *intel_connector = to_intel_connector(connector);
	struct intel_encoder *intel_encoder = intel_connector->encoder;
	struct drm_display_mode *mode;

	seq_printf(m, "connector %d: type %s, status: %s\n",
		   connector->base.id, connector->name,
		   drm_get_connector_status_name(connector->status));
	if (connector->status == connector_status_connected) {
		seq_printf(m, "\tname: %s\n", connector->display_info.name);
		seq_printf(m, "\tphysical dimensions: %dx%dmm\n",
			   connector->display_info.width_mm,
			   connector->display_info.height_mm);
		seq_printf(m, "\tsubpixel order: %s\n",
			   drm_get_subpixel_order_name(connector->display_info.subpixel_order));
		seq_printf(m, "\tCEA rev: %d\n",
			   connector->display_info.cea_rev);
	}

	if (!intel_encoder)
		return;

	switch (connector->connector_type) {
	case DRM_MODE_CONNECTOR_DisplayPort:
	case DRM_MODE_CONNECTOR_eDP:
		if (intel_encoder->type == INTEL_OUTPUT_DP_MST)
			intel_dp_mst_info(m, intel_connector);
		else
			intel_dp_info(m, intel_connector);
		break;
	case DRM_MODE_CONNECTOR_LVDS:
		if (intel_encoder->type == INTEL_OUTPUT_LVDS)
			intel_lvds_info(m, intel_connector);
		break;
	case DRM_MODE_CONNECTOR_HDMIA:
		if (intel_encoder->type == INTEL_OUTPUT_HDMI ||
		    intel_encoder->type == INTEL_OUTPUT_DDI)
			intel_hdmi_info(m, intel_connector);
		break;
	default:
		break;
	}

	seq_printf(m, "\tmodes:\n");
	list_for_each_entry(mode, &connector->modes, head)
		intel_seq_print_mode(m, 2, mode);
}

static const char *plane_type(enum drm_plane_type type)
{
	switch (type) {
	case DRM_PLANE_TYPE_OVERLAY:
		return "OVL";
	case DRM_PLANE_TYPE_PRIMARY:
		return "PRI";
	case DRM_PLANE_TYPE_CURSOR:
		return "CUR";
	/*
	 * Deliberately omitting default: to generate compiler warnings
	 * when a new drm_plane_type gets added.
	 */
	}

	return "unknown";
}

static const char *plane_rotation(unsigned int rotation)
{
	static char buf[48];
	/*
	 * According to doc only one DRM_MODE_ROTATE_ is allowed but this
	 * will print them all to visualize if the values are misused
	 */
	snprintf(buf, sizeof(buf),
		 "%s%s%s%s%s%s(0x%08x)",
		 (rotation & DRM_MODE_ROTATE_0) ? "0 " : "",
		 (rotation & DRM_MODE_ROTATE_90) ? "90 " : "",
		 (rotation & DRM_MODE_ROTATE_180) ? "180 " : "",
		 (rotation & DRM_MODE_ROTATE_270) ? "270 " : "",
		 (rotation & DRM_MODE_REFLECT_X) ? "FLIPX " : "",
		 (rotation & DRM_MODE_REFLECT_Y) ? "FLIPY " : "",
		 rotation);

	return buf;
}

static void intel_plane_info(struct seq_file *m, struct intel_crtc *intel_crtc)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct drm_device *dev = &dev_priv->drm;
	struct intel_plane *intel_plane;

	for_each_intel_plane_on_crtc(dev, intel_crtc, intel_plane) {
		struct drm_plane_state *state;
		struct drm_plane *plane = &intel_plane->base;
		struct drm_format_name_buf format_name;

		if (!plane->state) {
			seq_puts(m, "plane->state is NULL!\n");
			continue;
		}

		state = plane->state;

		if (state->fb) {
			drm_get_format_name(state->fb->format->format,
					    &format_name);
		} else {
			sprintf(format_name.str, "N/A");
		}

		seq_printf(m, "\t--Plane id %d: type=%s, crtc_pos=%4dx%4d, crtc_size=%4dx%4d, src_pos=%d.%04ux%d.%04u, src_size=%d.%04ux%d.%04u, format=%s, rotation=%s\n",
			   plane->base.id,
			   plane_type(intel_plane->base.type),
			   state->crtc_x, state->crtc_y,
			   state->crtc_w, state->crtc_h,
			   (state->src_x >> 16),
			   ((state->src_x & 0xffff) * 15625) >> 10,
			   (state->src_y >> 16),
			   ((state->src_y & 0xffff) * 15625) >> 10,
			   (state->src_w >> 16),
			   ((state->src_w & 0xffff) * 15625) >> 10,
			   (state->src_h >> 16),
			   ((state->src_h & 0xffff) * 15625) >> 10,
			   format_name.str,
			   plane_rotation(state->rotation));
	}
}

static void intel_scaler_info(struct seq_file *m, struct intel_crtc *intel_crtc)
{
	struct intel_crtc_state *pipe_config;
	int num_scalers = intel_crtc->num_scalers;
	int i;

	pipe_config = to_intel_crtc_state(intel_crtc->base.state);

	/* Not all platformas have a scaler */
	if (num_scalers) {
		seq_printf(m, "\tnum_scalers=%d, scaler_users=%x scaler_id=%d",
			   num_scalers,
			   pipe_config->scaler_state.scaler_users,
			   pipe_config->scaler_state.scaler_id);

		for (i = 0; i < num_scalers; i++) {
			struct intel_scaler *sc =
					&pipe_config->scaler_state.scalers[i];

			seq_printf(m, ", scalers[%d]: use=%s, mode=%x",
				   i, yesno(sc->in_use), sc->mode);
		}
		seq_puts(m, "\n");
	} else {
		seq_puts(m, "\tNo scalers available on this platform\n");
	}
}

static int i915_display_info(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct drm_device *dev = &dev_priv->drm;
	struct intel_crtc *crtc;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;

	intel_runtime_pm_get(dev_priv);
	seq_printf(m, "CRTC info\n");
	seq_printf(m, "---------\n");
	for_each_intel_crtc(dev, crtc) {
		struct intel_crtc_state *pipe_config;

		drm_modeset_lock(&crtc->base.mutex, NULL);
		pipe_config = to_intel_crtc_state(crtc->base.state);

		seq_printf(m, "CRTC %d: pipe: %c, active=%s, (size=%dx%d), dither=%s, bpp=%d\n",
			   crtc->base.base.id, pipe_name(crtc->pipe),
			   yesno(pipe_config->base.active),
			   pipe_config->pipe_src_w, pipe_config->pipe_src_h,
			   yesno(pipe_config->dither), pipe_config->pipe_bpp);

		if (pipe_config->base.active) {
			struct intel_plane *cursor =
				to_intel_plane(crtc->base.cursor);

			intel_crtc_info(m, crtc);

			seq_printf(m, "\tcursor visible? %s, position (%d, %d), size %dx%d, addr 0x%08x\n",
				   yesno(cursor->base.state->visible),
				   cursor->base.state->crtc_x,
				   cursor->base.state->crtc_y,
				   cursor->base.state->crtc_w,
				   cursor->base.state->crtc_h,
				   cursor->cursor.base);
			intel_scaler_info(m, crtc);
			intel_plane_info(m, crtc);
		}

		seq_printf(m, "\tunderrun reporting: cpu=%s pch=%s \n",
			   yesno(!crtc->cpu_fifo_underrun_disabled),
			   yesno(!crtc->pch_fifo_underrun_disabled));
		drm_modeset_unlock(&crtc->base.mutex);
	}

	seq_printf(m, "\n");
	seq_printf(m, "Connector info\n");
	seq_printf(m, "--------------\n");
	mutex_lock(&dev->mode_config.mutex);
	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter)
		intel_connector_info(m, connector);
	drm_connector_list_iter_end(&conn_iter);
	mutex_unlock(&dev->mode_config.mutex);

	intel_runtime_pm_put(dev_priv);

	return 0;
}

static int i915_engine_info(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	struct drm_printer p;

	intel_runtime_pm_get(dev_priv);

	seq_printf(m, "GT awake? %s (epoch %u)\n",
		   yesno(dev_priv->gt.awake), dev_priv->gt.epoch);
	seq_printf(m, "Global active requests: %d\n",
		   dev_priv->gt.active_requests);
	seq_printf(m, "CS timestamp frequency: %u kHz\n",
		   dev_priv->info.cs_timestamp_frequency_khz);

	p = drm_seq_file_printer(m);
	for_each_engine(engine, dev_priv, id)
		intel_engine_dump(engine, &p, "%s\n", engine->name);

	intel_runtime_pm_put(dev_priv);

	return 0;
}

static int i915_rcs_topology(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct drm_printer p = drm_seq_file_printer(m);

	intel_device_info_dump_topology(&INTEL_INFO(dev_priv)->sseu, &p);

	return 0;
}

static int i915_shrinker_info(struct seq_file *m, void *unused)
{
	struct drm_i915_private *i915 = node_to_i915(m->private);

	seq_printf(m, "seeks = %d\n", i915->mm.shrinker.seeks);
	seq_printf(m, "batch = %lu\n", i915->mm.shrinker.batch);

	return 0;
}

static int i915_shared_dplls_info(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct drm_device *dev = &dev_priv->drm;
	int i;

	drm_modeset_lock_all(dev);
	for (i = 0; i < dev_priv->num_shared_dpll; i++) {
		struct intel_shared_dpll *pll = &dev_priv->shared_dplls[i];

		seq_printf(m, "DPLL%i: %s, id: %i\n", i, pll->info->name,
			   pll->info->id);
		seq_printf(m, " crtc_mask: 0x%08x, active: 0x%x, on: %s\n",
			   pll->state.crtc_mask, pll->active_mask, yesno(pll->on));
		seq_printf(m, " tracked hardware state:\n");
		seq_printf(m, " dpll:    0x%08x\n", pll->state.hw_state.dpll);
		seq_printf(m, " dpll_md: 0x%08x\n",
			   pll->state.hw_state.dpll_md);
		seq_printf(m, " fp0:     0x%08x\n", pll->state.hw_state.fp0);
		seq_printf(m, " fp1:     0x%08x\n", pll->state.hw_state.fp1);
		seq_printf(m, " wrpll:   0x%08x\n", pll->state.hw_state.wrpll);
		seq_printf(m, " cfgcr0:  0x%08x\n", pll->state.hw_state.cfgcr0);
		seq_printf(m, " cfgcr1:  0x%08x\n", pll->state.hw_state.cfgcr1);
		seq_printf(m, " mg_refclkin_ctl:        0x%08x\n",
			   pll->state.hw_state.mg_refclkin_ctl);
		seq_printf(m, " mg_clktop2_coreclkctl1: 0x%08x\n",
			   pll->state.hw_state.mg_clktop2_coreclkctl1);
		seq_printf(m, " mg_clktop2_hsclkctl:    0x%08x\n",
			   pll->state.hw_state.mg_clktop2_hsclkctl);
		seq_printf(m, " mg_pll_div0:  0x%08x\n",
			   pll->state.hw_state.mg_pll_div0);
		seq_printf(m, " mg_pll_div1:  0x%08x\n",
			   pll->state.hw_state.mg_pll_div1);
		seq_printf(m, " mg_pll_lf:    0x%08x\n",
			   pll->state.hw_state.mg_pll_lf);
		seq_printf(m, " mg_pll_frac_lock: 0x%08x\n",
			   pll->state.hw_state.mg_pll_frac_lock);
		seq_printf(m, " mg_pll_ssc:   0x%08x\n",
			   pll->state.hw_state.mg_pll_ssc);
		seq_printf(m, " mg_pll_bias:  0x%08x\n",
			   pll->state.hw_state.mg_pll_bias);
		seq_printf(m, " mg_pll_tdc_coldst_bias: 0x%08x\n",
			   pll->state.hw_state.mg_pll_tdc_coldst_bias);
	}
	drm_modeset_unlock_all(dev);

	return 0;
}

static int i915_wa_registers(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct i915_workarounds *workarounds = &dev_priv->workarounds;
	int i;

	intel_runtime_pm_get(dev_priv);

	seq_printf(m, "Workarounds applied: %d\n", workarounds->count);
	for (i = 0; i < workarounds->count; ++i) {
		i915_reg_t addr;
		u32 mask, value, read;
		bool ok;

		addr = workarounds->reg[i].addr;
		mask = workarounds->reg[i].mask;
		value = workarounds->reg[i].value;
		read = I915_READ(addr);
		ok = (value & mask) == (read & mask);
		seq_printf(m, "0x%X: 0x%08X, mask: 0x%08X, read: 0x%08x, status: %s\n",
			   i915_mmio_reg_offset(addr), value, mask, read, ok ? "OK" : "FAIL");
	}

	intel_runtime_pm_put(dev_priv);

	return 0;
}

static int i915_ipc_status_show(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = m->private;

	seq_printf(m, "Isochronous Priority Control: %s\n",
			yesno(dev_priv->ipc_enabled));
	return 0;
}

static int i915_ipc_status_open(struct inode *inode, struct file *file)
{
	struct drm_i915_private *dev_priv = inode->i_private;

	if (!HAS_IPC(dev_priv))
		return -ENODEV;

	return single_open(file, i915_ipc_status_show, dev_priv);
}

static ssize_t i915_ipc_status_write(struct file *file, const char __user *ubuf,
				     size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	struct drm_i915_private *dev_priv = m->private;
	int ret;
	bool enable;

	ret = kstrtobool_from_user(ubuf, len, &enable);
	if (ret < 0)
		return ret;

	intel_runtime_pm_get(dev_priv);
	if (!dev_priv->ipc_enabled && enable)
		DRM_INFO("Enabling IPC: WM will be proper only after next commit\n");
	dev_priv->wm.distrust_bios_wm = true;
	dev_priv->ipc_enabled = enable;
	intel_enable_ipc(dev_priv);
	intel_runtime_pm_put(dev_priv);

	return len;
}

static const struct file_operations i915_ipc_status_fops = {
	.owner = THIS_MODULE,
	.open = i915_ipc_status_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = i915_ipc_status_write
};

static int i915_ddb_info(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct drm_device *dev = &dev_priv->drm;
	struct skl_ddb_allocation *ddb;
	struct skl_ddb_entry *entry;
	enum pipe pipe;
	int plane;

	if (INTEL_GEN(dev_priv) < 9)
		return -ENODEV;

	drm_modeset_lock_all(dev);

	ddb = &dev_priv->wm.skl_hw.ddb;

	seq_printf(m, "%-15s%8s%8s%8s\n", "", "Start", "End", "Size");

	for_each_pipe(dev_priv, pipe) {
		seq_printf(m, "Pipe %c\n", pipe_name(pipe));

		for_each_universal_plane(dev_priv, pipe, plane) {
			entry = &ddb->plane[pipe][plane];
			seq_printf(m, "  Plane%-8d%8u%8u%8u\n", plane + 1,
				   entry->start, entry->end,
				   skl_ddb_entry_size(entry));
		}

		entry = &ddb->plane[pipe][PLANE_CURSOR];
		seq_printf(m, "  %-13s%8u%8u%8u\n", "Cursor", entry->start,
			   entry->end, skl_ddb_entry_size(entry));
	}

	drm_modeset_unlock_all(dev);

	return 0;
}

static void drrs_status_per_crtc(struct seq_file *m,
				 struct drm_device *dev,
				 struct intel_crtc *intel_crtc)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct i915_drrs *drrs = &dev_priv->drrs;
	int vrefresh = 0;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;

	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		if (connector->state->crtc != &intel_crtc->base)
			continue;

		seq_printf(m, "%s:\n", connector->name);
	}
	drm_connector_list_iter_end(&conn_iter);

	if (dev_priv->vbt.drrs_type == STATIC_DRRS_SUPPORT)
		seq_puts(m, "\tVBT: DRRS_type: Static");
	else if (dev_priv->vbt.drrs_type == SEAMLESS_DRRS_SUPPORT)
		seq_puts(m, "\tVBT: DRRS_type: Seamless");
	else if (dev_priv->vbt.drrs_type == DRRS_NOT_SUPPORTED)
		seq_puts(m, "\tVBT: DRRS_type: None");
	else
		seq_puts(m, "\tVBT: DRRS_type: FIXME: Unrecognized Value");

	seq_puts(m, "\n\n");

	if (to_intel_crtc_state(intel_crtc->base.state)->has_drrs) {
		struct intel_panel *panel;

		mutex_lock(&drrs->mutex);
		/* DRRS Supported */
		seq_puts(m, "\tDRRS Supported: Yes\n");

		/* disable_drrs() will make drrs->dp NULL */
		if (!drrs->dp) {
			seq_puts(m, "Idleness DRRS: Disabled\n");
			if (dev_priv->psr.enabled)
				seq_puts(m,
				"\tAs PSR is enabled, DRRS is not enabled\n");
			mutex_unlock(&drrs->mutex);
			return;
		}

		panel = &drrs->dp->attached_connector->panel;
		seq_printf(m, "\t\tBusy_frontbuffer_bits: 0x%X",
					drrs->busy_frontbuffer_bits);

		seq_puts(m, "\n\t\t");
		if (drrs->refresh_rate_type == DRRS_HIGH_RR) {
			seq_puts(m, "DRRS_State: DRRS_HIGH_RR\n");
			vrefresh = panel->fixed_mode->vrefresh;
		} else if (drrs->refresh_rate_type == DRRS_LOW_RR) {
			seq_puts(m, "DRRS_State: DRRS_LOW_RR\n");
			vrefresh = panel->downclock_mode->vrefresh;
		} else {
			seq_printf(m, "DRRS_State: Unknown(%d)\n",
						drrs->refresh_rate_type);
			mutex_unlock(&drrs->mutex);
			return;
		}
		seq_printf(m, "\t\tVrefresh: %d", vrefresh);

		seq_puts(m, "\n\t\t");
		mutex_unlock(&drrs->mutex);
	} else {
		/* DRRS not supported. Print the VBT parameter*/
		seq_puts(m, "\tDRRS Supported : No");
	}
	seq_puts(m, "\n");
}

static int i915_drrs_status(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct drm_device *dev = &dev_priv->drm;
	struct intel_crtc *intel_crtc;
	int active_crtc_cnt = 0;

	drm_modeset_lock_all(dev);
	for_each_intel_crtc(dev, intel_crtc) {
		if (intel_crtc->base.state->active) {
			active_crtc_cnt++;
			seq_printf(m, "\nCRTC %d:  ", active_crtc_cnt);

			drrs_status_per_crtc(m, dev, intel_crtc);
		}
	}
	drm_modeset_unlock_all(dev);

	if (!active_crtc_cnt)
		seq_puts(m, "No active crtc found\n");

	return 0;
}

static int i915_dp_mst_info(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct drm_device *dev = &dev_priv->drm;
	struct intel_encoder *intel_encoder;
	struct intel_digital_port *intel_dig_port;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;

	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		if (connector->connector_type != DRM_MODE_CONNECTOR_DisplayPort)
			continue;

		intel_encoder = intel_attached_encoder(connector);
		if (!intel_encoder || intel_encoder->type == INTEL_OUTPUT_DP_MST)
			continue;

		intel_dig_port = enc_to_dig_port(&intel_encoder->base);
		if (!intel_dig_port->dp.can_mst)
			continue;

		seq_printf(m, "MST Source Port %c\n",
			   port_name(intel_dig_port->base.port));
		drm_dp_mst_dump_topology(m, &intel_dig_port->dp.mst_mgr);
	}
	drm_connector_list_iter_end(&conn_iter);

	return 0;
}

static ssize_t i915_displayport_test_active_write(struct file *file,
						  const char __user *ubuf,
						  size_t len, loff_t *offp)
{
	char *input_buffer;
	int status = 0;
	struct drm_device *dev;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;
	struct intel_dp *intel_dp;
	int val = 0;

	dev = ((struct seq_file *)file->private_data)->private;

	if (len == 0)
		return 0;

	input_buffer = memdup_user_nul(ubuf, len);
	if (IS_ERR(input_buffer))
		return PTR_ERR(input_buffer);

	DRM_DEBUG_DRIVER("Copied %d bytes from user\n", (unsigned int)len);

	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		struct intel_encoder *encoder;

		if (connector->connector_type !=
		    DRM_MODE_CONNECTOR_DisplayPort)
			continue;

		encoder = to_intel_encoder(connector->encoder);
		if (encoder && encoder->type == INTEL_OUTPUT_DP_MST)
			continue;

		if (encoder && connector->status == connector_status_connected) {
			intel_dp = enc_to_intel_dp(&encoder->base);
			status = kstrtoint(input_buffer, 10, &val);
			if (status < 0)
				break;
			DRM_DEBUG_DRIVER("Got %d for test active\n", val);
			/* To prevent erroneous activation of the compliance
			 * testing code, only accept an actual value of 1 here
			 */
			if (val == 1)
				intel_dp->compliance.test_active = 1;
			else
				intel_dp->compliance.test_active = 0;
		}
	}
	drm_connector_list_iter_end(&conn_iter);
	kfree(input_buffer);
	if (status < 0)
		return status;

	*offp += len;
	return len;
}

static int i915_displayport_test_active_show(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = m->private;
	struct drm_device *dev = &dev_priv->drm;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;
	struct intel_dp *intel_dp;

	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		struct intel_encoder *encoder;

		if (connector->connector_type !=
		    DRM_MODE_CONNECTOR_DisplayPort)
			continue;

		encoder = to_intel_encoder(connector->encoder);
		if (encoder && encoder->type == INTEL_OUTPUT_DP_MST)
			continue;

		if (encoder && connector->status == connector_status_connected) {
			intel_dp = enc_to_intel_dp(&encoder->base);
			if (intel_dp->compliance.test_active)
				seq_puts(m, "1");
			else
				seq_puts(m, "0");
		} else
			seq_puts(m, "0");
	}
	drm_connector_list_iter_end(&conn_iter);

	return 0;
}

static int i915_displayport_test_active_open(struct inode *inode,
					     struct file *file)
{
	return single_open(file, i915_displayport_test_active_show,
			   inode->i_private);
}

static const struct file_operations i915_displayport_test_active_fops = {
	.owner = THIS_MODULE,
	.open = i915_displayport_test_active_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = i915_displayport_test_active_write
};

static int i915_displayport_test_data_show(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = m->private;
	struct drm_device *dev = &dev_priv->drm;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;
	struct intel_dp *intel_dp;

	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		struct intel_encoder *encoder;

		if (connector->connector_type !=
		    DRM_MODE_CONNECTOR_DisplayPort)
			continue;

		encoder = to_intel_encoder(connector->encoder);
		if (encoder && encoder->type == INTEL_OUTPUT_DP_MST)
			continue;

		if (encoder && connector->status == connector_status_connected) {
			intel_dp = enc_to_intel_dp(&encoder->base);
			if (intel_dp->compliance.test_type ==
			    DP_TEST_LINK_EDID_READ)
				seq_printf(m, "%lx",
					   intel_dp->compliance.test_data.edid);
			else if (intel_dp->compliance.test_type ==
				 DP_TEST_LINK_VIDEO_PATTERN) {
				seq_printf(m, "hdisplay: %d\n",
					   intel_dp->compliance.test_data.hdisplay);
				seq_printf(m, "vdisplay: %d\n",
					   intel_dp->compliance.test_data.vdisplay);
				seq_printf(m, "bpc: %u\n",
					   intel_dp->compliance.test_data.bpc);
			}
		} else
			seq_puts(m, "0");
	}
	drm_connector_list_iter_end(&conn_iter);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(i915_displayport_test_data);

static int i915_displayport_test_type_show(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = m->private;
	struct drm_device *dev = &dev_priv->drm;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;
	struct intel_dp *intel_dp;

	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		struct intel_encoder *encoder;

		if (connector->connector_type !=
		    DRM_MODE_CONNECTOR_DisplayPort)
			continue;

		encoder = to_intel_encoder(connector->encoder);
		if (encoder && encoder->type == INTEL_OUTPUT_DP_MST)
			continue;

		if (encoder && connector->status == connector_status_connected) {
			intel_dp = enc_to_intel_dp(&encoder->base);
			seq_printf(m, "%02lx", intel_dp->compliance.test_type);
		} else
			seq_puts(m, "0");
	}
	drm_connector_list_iter_end(&conn_iter);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(i915_displayport_test_type);

static void wm_latency_show(struct seq_file *m, const uint16_t wm[8])
{
	struct drm_i915_private *dev_priv = m->private;
	struct drm_device *dev = &dev_priv->drm;
	int level;
	int num_levels;

	if (IS_CHERRYVIEW(dev_priv))
		num_levels = 3;
	else if (IS_VALLEYVIEW(dev_priv))
		num_levels = 1;
	else if (IS_G4X(dev_priv))
		num_levels = 3;
	else
		num_levels = ilk_wm_max_level(dev_priv) + 1;

	drm_modeset_lock_all(dev);

	for (level = 0; level < num_levels; level++) {
		unsigned int latency = wm[level];

		/*
		 * - WM1+ latency values in 0.5us units
		 * - latencies are in us on gen9/vlv/chv
		 */
		if (INTEL_GEN(dev_priv) >= 9 ||
		    IS_VALLEYVIEW(dev_priv) ||
		    IS_CHERRYVIEW(dev_priv) ||
		    IS_G4X(dev_priv))
			latency *= 10;
		else if (level > 0)
			latency *= 5;

		seq_printf(m, "WM%d %u (%u.%u usec)\n",
			   level, wm[level], latency / 10, latency % 10);
	}

	drm_modeset_unlock_all(dev);
}

static int pri_wm_latency_show(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = m->private;
	const uint16_t *latencies;

	if (INTEL_GEN(dev_priv) >= 9)
		latencies = dev_priv->wm.skl_latency;
	else
		latencies = dev_priv->wm.pri_latency;

	wm_latency_show(m, latencies);

	return 0;
}

static int spr_wm_latency_show(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = m->private;
	const uint16_t *latencies;

	if (INTEL_GEN(dev_priv) >= 9)
		latencies = dev_priv->wm.skl_latency;
	else
		latencies = dev_priv->wm.spr_latency;

	wm_latency_show(m, latencies);

	return 0;
}

static int cur_wm_latency_show(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = m->private;
	const uint16_t *latencies;

	if (INTEL_GEN(dev_priv) >= 9)
		latencies = dev_priv->wm.skl_latency;
	else
		latencies = dev_priv->wm.cur_latency;

	wm_latency_show(m, latencies);

	return 0;
}

static int pri_wm_latency_open(struct inode *inode, struct file *file)
{
	struct drm_i915_private *dev_priv = inode->i_private;

	if (INTEL_GEN(dev_priv) < 5 && !IS_G4X(dev_priv))
		return -ENODEV;

	return single_open(file, pri_wm_latency_show, dev_priv);
}

static int spr_wm_latency_open(struct inode *inode, struct file *file)
{
	struct drm_i915_private *dev_priv = inode->i_private;

	if (HAS_GMCH_DISPLAY(dev_priv))
		return -ENODEV;

	return single_open(file, spr_wm_latency_show, dev_priv);
}

static int cur_wm_latency_open(struct inode *inode, struct file *file)
{
	struct drm_i915_private *dev_priv = inode->i_private;

	if (HAS_GMCH_DISPLAY(dev_priv))
		return -ENODEV;

	return single_open(file, cur_wm_latency_show, dev_priv);
}

static ssize_t wm_latency_write(struct file *file, const char __user *ubuf,
				size_t len, loff_t *offp, uint16_t wm[8])
{
	struct seq_file *m = file->private_data;
	struct drm_i915_private *dev_priv = m->private;
	struct drm_device *dev = &dev_priv->drm;
	uint16_t new[8] = { 0 };
	int num_levels;
	int level;
	int ret;
	char tmp[32];

	if (IS_CHERRYVIEW(dev_priv))
		num_levels = 3;
	else if (IS_VALLEYVIEW(dev_priv))
		num_levels = 1;
	else if (IS_G4X(dev_priv))
		num_levels = 3;
	else
		num_levels = ilk_wm_max_level(dev_priv) + 1;

	if (len >= sizeof(tmp))
		return -EINVAL;

	if (copy_from_user(tmp, ubuf, len))
		return -EFAULT;

	tmp[len] = '\0';

	ret = sscanf(tmp, "%hu %hu %hu %hu %hu %hu %hu %hu",
		     &new[0], &new[1], &new[2], &new[3],
		     &new[4], &new[5], &new[6], &new[7]);
	if (ret != num_levels)
		return -EINVAL;

	drm_modeset_lock_all(dev);

	for (level = 0; level < num_levels; level++)
		wm[level] = new[level];

	drm_modeset_unlock_all(dev);

	return len;
}


static ssize_t pri_wm_latency_write(struct file *file, const char __user *ubuf,
				    size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	struct drm_i915_private *dev_priv = m->private;
	uint16_t *latencies;

	if (INTEL_GEN(dev_priv) >= 9)
		latencies = dev_priv->wm.skl_latency;
	else
		latencies = dev_priv->wm.pri_latency;

	return wm_latency_write(file, ubuf, len, offp, latencies);
}

static ssize_t spr_wm_latency_write(struct file *file, const char __user *ubuf,
				    size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	struct drm_i915_private *dev_priv = m->private;
	uint16_t *latencies;

	if (INTEL_GEN(dev_priv) >= 9)
		latencies = dev_priv->wm.skl_latency;
	else
		latencies = dev_priv->wm.spr_latency;

	return wm_latency_write(file, ubuf, len, offp, latencies);
}

static ssize_t cur_wm_latency_write(struct file *file, const char __user *ubuf,
				    size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	struct drm_i915_private *dev_priv = m->private;
	uint16_t *latencies;

	if (INTEL_GEN(dev_priv) >= 9)
		latencies = dev_priv->wm.skl_latency;
	else
		latencies = dev_priv->wm.cur_latency;

	return wm_latency_write(file, ubuf, len, offp, latencies);
}

static const struct file_operations i915_pri_wm_latency_fops = {
	.owner = THIS_MODULE,
	.open = pri_wm_latency_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = pri_wm_latency_write
};

static const struct file_operations i915_spr_wm_latency_fops = {
	.owner = THIS_MODULE,
	.open = spr_wm_latency_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = spr_wm_latency_write
};

static const struct file_operations i915_cur_wm_latency_fops = {
	.owner = THIS_MODULE,
	.open = cur_wm_latency_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = cur_wm_latency_write
};

static int
i915_wedged_get(void *data, u64 *val)
{
	struct drm_i915_private *dev_priv = data;

	*val = i915_terminally_wedged(&dev_priv->gpu_error);

	return 0;
}

static int
i915_wedged_set(void *data, u64 val)
{
	struct drm_i915_private *i915 = data;
	struct intel_engine_cs *engine;
	unsigned int tmp;

	/*
	 * There is no safeguard against this debugfs entry colliding
	 * with the hangcheck calling same i915_handle_error() in
	 * parallel, causing an explosion. For now we assume that the
	 * test harness is responsible enough not to inject gpu hangs
	 * while it is writing to 'i915_wedged'
	 */

	if (i915_reset_backoff(&i915->gpu_error))
		return -EAGAIN;

	for_each_engine_masked(engine, i915, val, tmp) {
		engine->hangcheck.seqno = intel_engine_get_seqno(engine);
		engine->hangcheck.stalled = true;
	}

	i915_handle_error(i915, val, I915_ERROR_CAPTURE,
			  "Manually set wedged engine mask = %llx", val);

	wait_on_bit(&i915->gpu_error.flags,
		    I915_RESET_HANDOFF,
		    TASK_UNINTERRUPTIBLE);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(i915_wedged_fops,
			i915_wedged_get, i915_wedged_set,
			"%llu\n");

static int
fault_irq_set(struct drm_i915_private *i915,
	      unsigned long *irq,
	      unsigned long val)
{
	int err;

	err = mutex_lock_interruptible(&i915->drm.struct_mutex);
	if (err)
		return err;

	err = i915_gem_wait_for_idle(i915,
				     I915_WAIT_LOCKED |
				     I915_WAIT_INTERRUPTIBLE);
	if (err)
		goto err_unlock;

	*irq = val;
	mutex_unlock(&i915->drm.struct_mutex);

	/* Flush idle worker to disarm irq */
	drain_delayed_work(&i915->gt.idle_work);

	return 0;

err_unlock:
	mutex_unlock(&i915->drm.struct_mutex);
	return err;
}

static int
i915_ring_missed_irq_get(void *data, u64 *val)
{
	struct drm_i915_private *dev_priv = data;

	*val = dev_priv->gpu_error.missed_irq_rings;
	return 0;
}

static int
i915_ring_missed_irq_set(void *data, u64 val)
{
	struct drm_i915_private *i915 = data;

	return fault_irq_set(i915, &i915->gpu_error.missed_irq_rings, val);
}

DEFINE_SIMPLE_ATTRIBUTE(i915_ring_missed_irq_fops,
			i915_ring_missed_irq_get, i915_ring_missed_irq_set,
			"0x%08llx\n");

static int
i915_ring_test_irq_get(void *data, u64 *val)
{
	struct drm_i915_private *dev_priv = data;

	*val = dev_priv->gpu_error.test_irq_rings;

	return 0;
}

static int
i915_ring_test_irq_set(void *data, u64 val)
{
	struct drm_i915_private *i915 = data;

	val &= INTEL_INFO(i915)->ring_mask;
	DRM_DEBUG_DRIVER("Masking interrupts on rings 0x%08llx\n", val);

	return fault_irq_set(i915, &i915->gpu_error.test_irq_rings, val);
}

DEFINE_SIMPLE_ATTRIBUTE(i915_ring_test_irq_fops,
			i915_ring_test_irq_get, i915_ring_test_irq_set,
			"0x%08llx\n");

#define DROP_UNBOUND	BIT(0)
#define DROP_BOUND	BIT(1)
#define DROP_RETIRE	BIT(2)
#define DROP_ACTIVE	BIT(3)
#define DROP_FREED	BIT(4)
#define DROP_SHRINK_ALL	BIT(5)
#define DROP_IDLE	BIT(6)
#define DROP_ALL (DROP_UNBOUND	| \
		  DROP_BOUND	| \
		  DROP_RETIRE	| \
		  DROP_ACTIVE	| \
		  DROP_FREED	| \
		  DROP_SHRINK_ALL |\
		  DROP_IDLE)
static int
i915_drop_caches_get(void *data, u64 *val)
{
	*val = DROP_ALL;

	return 0;
}

static int
i915_drop_caches_set(void *data, u64 val)
{
	struct drm_i915_private *dev_priv = data;
	struct drm_device *dev = &dev_priv->drm;
	int ret = 0;

	DRM_DEBUG("Dropping caches: 0x%08llx [0x%08llx]\n",
		  val, val & DROP_ALL);

	/* No need to check and wait for gpu resets, only libdrm auto-restarts
	 * on ioctls on -EAGAIN. */
	if (val & (DROP_ACTIVE | DROP_RETIRE)) {
		ret = mutex_lock_interruptible(&dev->struct_mutex);
		if (ret)
			return ret;

		if (val & DROP_ACTIVE)
			ret = i915_gem_wait_for_idle(dev_priv,
						     I915_WAIT_INTERRUPTIBLE |
						     I915_WAIT_LOCKED);

		if (val & DROP_RETIRE)
			i915_retire_requests(dev_priv);

		mutex_unlock(&dev->struct_mutex);
	}

	fs_reclaim_acquire(GFP_KERNEL);
	if (val & DROP_BOUND)
		i915_gem_shrink(dev_priv, LONG_MAX, NULL, I915_SHRINK_BOUND);

	if (val & DROP_UNBOUND)
		i915_gem_shrink(dev_priv, LONG_MAX, NULL, I915_SHRINK_UNBOUND);

	if (val & DROP_SHRINK_ALL)
		i915_gem_shrink_all(dev_priv);
	fs_reclaim_release(GFP_KERNEL);

	if (val & DROP_IDLE)
		drain_delayed_work(&dev_priv->gt.idle_work);

	if (val & DROP_FREED)
		i915_gem_drain_freed_objects(dev_priv);

	return ret;
}

DEFINE_SIMPLE_ATTRIBUTE(i915_drop_caches_fops,
			i915_drop_caches_get, i915_drop_caches_set,
			"0x%08llx\n");

static int
i915_cache_sharing_get(void *data, u64 *val)
{
	struct drm_i915_private *dev_priv = data;
	u32 snpcr;

	if (!(IS_GEN6(dev_priv) || IS_GEN7(dev_priv)))
		return -ENODEV;

	intel_runtime_pm_get(dev_priv);

	snpcr = I915_READ(GEN6_MBCUNIT_SNPCR);

	intel_runtime_pm_put(dev_priv);

	*val = (snpcr & GEN6_MBC_SNPCR_MASK) >> GEN6_MBC_SNPCR_SHIFT;

	return 0;
}

static int
i915_cache_sharing_set(void *data, u64 val)
{
	struct drm_i915_private *dev_priv = data;
	u32 snpcr;

	if (!(IS_GEN6(dev_priv) || IS_GEN7(dev_priv)))
		return -ENODEV;

	if (val > 3)
		return -EINVAL;

	intel_runtime_pm_get(dev_priv);
	DRM_DEBUG_DRIVER("Manually setting uncore sharing to %llu\n", val);

	/* Update the cache sharing policy here as well */
	snpcr = I915_READ(GEN6_MBCUNIT_SNPCR);
	snpcr &= ~GEN6_MBC_SNPCR_MASK;
	snpcr |= (val << GEN6_MBC_SNPCR_SHIFT);
	I915_WRITE(GEN6_MBCUNIT_SNPCR, snpcr);

	intel_runtime_pm_put(dev_priv);
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(i915_cache_sharing_fops,
			i915_cache_sharing_get, i915_cache_sharing_set,
			"%llu\n");

static void cherryview_sseu_device_status(struct drm_i915_private *dev_priv,
					  struct sseu_dev_info *sseu)
{
#define SS_MAX 2
	const int ss_max = SS_MAX;
	u32 sig1[SS_MAX], sig2[SS_MAX];
	int ss;

	sig1[0] = I915_READ(CHV_POWER_SS0_SIG1);
	sig1[1] = I915_READ(CHV_POWER_SS1_SIG1);
	sig2[0] = I915_READ(CHV_POWER_SS0_SIG2);
	sig2[1] = I915_READ(CHV_POWER_SS1_SIG2);

	for (ss = 0; ss < ss_max; ss++) {
		unsigned int eu_cnt;

		if (sig1[ss] & CHV_SS_PG_ENABLE)
			/* skip disabled subslice */
			continue;

		sseu->slice_mask = BIT(0);
		sseu->subslice_mask[0] |= BIT(ss);
		eu_cnt = ((sig1[ss] & CHV_EU08_PG_ENABLE) ? 0 : 2) +
			 ((sig1[ss] & CHV_EU19_PG_ENABLE) ? 0 : 2) +
			 ((sig1[ss] & CHV_EU210_PG_ENABLE) ? 0 : 2) +
			 ((sig2[ss] & CHV_EU311_PG_ENABLE) ? 0 : 2);
		sseu->eu_total += eu_cnt;
		sseu->eu_per_subslice = max_t(unsigned int,
					      sseu->eu_per_subslice, eu_cnt);
	}
#undef SS_MAX
}

static void gen10_sseu_device_status(struct drm_i915_private *dev_priv,
				     struct sseu_dev_info *sseu)
{
#define SS_MAX 6
	const struct intel_device_info *info = INTEL_INFO(dev_priv);
	u32 s_reg[SS_MAX], eu_reg[2 * SS_MAX], eu_mask[2];
	int s, ss;

	for (s = 0; s < info->sseu.max_slices; s++) {
		/*
		 * FIXME: Valid SS Mask respects the spec and read
		 * only valid bits for those registers, excluding reserverd
		 * although this seems wrong because it would leave many
		 * subslices without ACK.
		 */
		s_reg[s] = I915_READ(GEN10_SLICE_PGCTL_ACK(s)) &
			GEN10_PGCTL_VALID_SS_MASK(s);
		eu_reg[2 * s] = I915_READ(GEN10_SS01_EU_PGCTL_ACK(s));
		eu_reg[2 * s + 1] = I915_READ(GEN10_SS23_EU_PGCTL_ACK(s));
	}

	eu_mask[0] = GEN9_PGCTL_SSA_EU08_ACK |
		     GEN9_PGCTL_SSA_EU19_ACK |
		     GEN9_PGCTL_SSA_EU210_ACK |
		     GEN9_PGCTL_SSA_EU311_ACK;
	eu_mask[1] = GEN9_PGCTL_SSB_EU08_ACK |
		     GEN9_PGCTL_SSB_EU19_ACK |
		     GEN9_PGCTL_SSB_EU210_ACK |
		     GEN9_PGCTL_SSB_EU311_ACK;

	for (s = 0; s < info->sseu.max_slices; s++) {
		if ((s_reg[s] & GEN9_PGCTL_SLICE_ACK) == 0)
			/* skip disabled slice */
			continue;

		sseu->slice_mask |= BIT(s);
		sseu->subslice_mask[s] = info->sseu.subslice_mask[s];

		for (ss = 0; ss < info->sseu.max_subslices; ss++) {
			unsigned int eu_cnt;

			if (!(s_reg[s] & (GEN9_PGCTL_SS_ACK(ss))))
				/* skip disabled subslice */
				continue;

			eu_cnt = 2 * hweight32(eu_reg[2 * s + ss / 2] &
					       eu_mask[ss % 2]);
			sseu->eu_total += eu_cnt;
			sseu->eu_per_subslice = max_t(unsigned int,
						      sseu->eu_per_subslice,
						      eu_cnt);
		}
	}
#undef SS_MAX
}

static void gen9_sseu_device_status(struct drm_i915_private *dev_priv,
				    struct sseu_dev_info *sseu)
{
#define SS_MAX 3
	const struct intel_device_info *info = INTEL_INFO(dev_priv);
	u32 s_reg[SS_MAX], eu_reg[2 * SS_MAX], eu_mask[2];
	int s, ss;

	for (s = 0; s < info->sseu.max_slices; s++) {
		s_reg[s] = I915_READ(GEN9_SLICE_PGCTL_ACK(s));
		eu_reg[2*s] = I915_READ(GEN9_SS01_EU_PGCTL_ACK(s));
		eu_reg[2*s + 1] = I915_READ(GEN9_SS23_EU_PGCTL_ACK(s));
	}

	eu_mask[0] = GEN9_PGCTL_SSA_EU08_ACK |
		     GEN9_PGCTL_SSA_EU19_ACK |
		     GEN9_PGCTL_SSA_EU210_ACK |
		     GEN9_PGCTL_SSA_EU311_ACK;
	eu_mask[1] = GEN9_PGCTL_SSB_EU08_ACK |
		     GEN9_PGCTL_SSB_EU19_ACK |
		     GEN9_PGCTL_SSB_EU210_ACK |
		     GEN9_PGCTL_SSB_EU311_ACK;

	for (s = 0; s < info->sseu.max_slices; s++) {
		if ((s_reg[s] & GEN9_PGCTL_SLICE_ACK) == 0)
			/* skip disabled slice */
			continue;

		sseu->slice_mask |= BIT(s);

		if (IS_GEN9_BC(dev_priv))
			sseu->subslice_mask[s] =
				INTEL_INFO(dev_priv)->sseu.subslice_mask[s];

		for (ss = 0; ss < info->sseu.max_subslices; ss++) {
			unsigned int eu_cnt;

			if (IS_GEN9_LP(dev_priv)) {
				if (!(s_reg[s] & (GEN9_PGCTL_SS_ACK(ss))))
					/* skip disabled subslice */
					continue;

				sseu->subslice_mask[s] |= BIT(ss);
			}

			eu_cnt = 2 * hweight32(eu_reg[2*s + ss/2] &
					       eu_mask[ss%2]);
			sseu->eu_total += eu_cnt;
			sseu->eu_per_subslice = max_t(unsigned int,
						      sseu->eu_per_subslice,
						      eu_cnt);
		}
	}
#undef SS_MAX
}

static void broadwell_sseu_device_status(struct drm_i915_private *dev_priv,
					 struct sseu_dev_info *sseu)
{
	u32 slice_info = I915_READ(GEN8_GT_SLICE_INFO);
	int s;

	sseu->slice_mask = slice_info & GEN8_LSLICESTAT_MASK;

	if (sseu->slice_mask) {
		sseu->eu_per_subslice =
				INTEL_INFO(dev_priv)->sseu.eu_per_subslice;
		for (s = 0; s < fls(sseu->slice_mask); s++) {
			sseu->subslice_mask[s] =
				INTEL_INFO(dev_priv)->sseu.subslice_mask[s];
		}
		sseu->eu_total = sseu->eu_per_subslice *
				 sseu_subslice_total(sseu);

		/* subtract fused off EU(s) from enabled slice(s) */
		for (s = 0; s < fls(sseu->slice_mask); s++) {
			u8 subslice_7eu =
				INTEL_INFO(dev_priv)->sseu.subslice_7eu[s];

			sseu->eu_total -= hweight8(subslice_7eu);
		}
	}
}

static void i915_print_sseu_info(struct seq_file *m, bool is_available_info,
				 const struct sseu_dev_info *sseu)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	const char *type = is_available_info ? "Available" : "Enabled";
	int s;

	seq_printf(m, "  %s Slice Mask: %04x\n", type,
		   sseu->slice_mask);
	seq_printf(m, "  %s Slice Total: %u\n", type,
		   hweight8(sseu->slice_mask));
	seq_printf(m, "  %s Subslice Total: %u\n", type,
		   sseu_subslice_total(sseu));
	for (s = 0; s < fls(sseu->slice_mask); s++) {
		seq_printf(m, "  %s Slice%i subslices: %u\n", type,
			   s, hweight8(sseu->subslice_mask[s]));
	}
	seq_printf(m, "  %s EU Total: %u\n", type,
		   sseu->eu_total);
	seq_printf(m, "  %s EU Per Subslice: %u\n", type,
		   sseu->eu_per_subslice);

	if (!is_available_info)
		return;

	seq_printf(m, "  Has Pooled EU: %s\n", yesno(HAS_POOLED_EU(dev_priv)));
	if (HAS_POOLED_EU(dev_priv))
		seq_printf(m, "  Min EU in pool: %u\n", sseu->min_eu_in_pool);

	seq_printf(m, "  Has Slice Power Gating: %s\n",
		   yesno(sseu->has_slice_pg));
	seq_printf(m, "  Has Subslice Power Gating: %s\n",
		   yesno(sseu->has_subslice_pg));
	seq_printf(m, "  Has EU Power Gating: %s\n",
		   yesno(sseu->has_eu_pg));
}

static int i915_sseu_status(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct sseu_dev_info sseu;

	if (INTEL_GEN(dev_priv) < 8)
		return -ENODEV;

	seq_puts(m, "SSEU Device Info\n");
	i915_print_sseu_info(m, true, &INTEL_INFO(dev_priv)->sseu);

	seq_puts(m, "SSEU Device Status\n");
	memset(&sseu, 0, sizeof(sseu));
	sseu.max_slices = INTEL_INFO(dev_priv)->sseu.max_slices;
	sseu.max_subslices = INTEL_INFO(dev_priv)->sseu.max_subslices;
	sseu.max_eus_per_subslice =
		INTEL_INFO(dev_priv)->sseu.max_eus_per_subslice;

	intel_runtime_pm_get(dev_priv);

	if (IS_CHERRYVIEW(dev_priv)) {
		cherryview_sseu_device_status(dev_priv, &sseu);
	} else if (IS_BROADWELL(dev_priv)) {
		broadwell_sseu_device_status(dev_priv, &sseu);
	} else if (IS_GEN9(dev_priv)) {
		gen9_sseu_device_status(dev_priv, &sseu);
	} else if (INTEL_GEN(dev_priv) >= 10) {
		gen10_sseu_device_status(dev_priv, &sseu);
	}

	intel_runtime_pm_put(dev_priv);

	i915_print_sseu_info(m, false, &sseu);

	return 0;
}

static int i915_forcewake_open(struct inode *inode, struct file *file)
{
	struct drm_i915_private *i915 = inode->i_private;

	if (INTEL_GEN(i915) < 6)
		return 0;

	intel_runtime_pm_get(i915);
	intel_uncore_forcewake_user_get(i915);

	return 0;
}

static int i915_forcewake_release(struct inode *inode, struct file *file)
{
	struct drm_i915_private *i915 = inode->i_private;

	if (INTEL_GEN(i915) < 6)
		return 0;

	intel_uncore_forcewake_user_put(i915);
	intel_runtime_pm_put(i915);

	return 0;
}

static const struct file_operations i915_forcewake_fops = {
	.owner = THIS_MODULE,
	.open = i915_forcewake_open,
	.release = i915_forcewake_release,
};

static int i915_hpd_storm_ctl_show(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = m->private;
	struct i915_hotplug *hotplug = &dev_priv->hotplug;

	seq_printf(m, "Threshold: %d\n", hotplug->hpd_storm_threshold);
	seq_printf(m, "Detected: %s\n",
		   yesno(delayed_work_pending(&hotplug->reenable_work)));

	return 0;
}

static ssize_t i915_hpd_storm_ctl_write(struct file *file,
					const char __user *ubuf, size_t len,
					loff_t *offp)
{
	struct seq_file *m = file->private_data;
	struct drm_i915_private *dev_priv = m->private;
	struct i915_hotplug *hotplug = &dev_priv->hotplug;
	unsigned int new_threshold;
	int i;
	char *newline;
	char tmp[16];

	if (len >= sizeof(tmp))
		return -EINVAL;

	if (copy_from_user(tmp, ubuf, len))
		return -EFAULT;

	tmp[len] = '\0';

	/* Strip newline, if any */
	newline = strchr(tmp, '\n');
	if (newline)
		*newline = '\0';

	if (strcmp(tmp, "reset") == 0)
		new_threshold = HPD_STORM_DEFAULT_THRESHOLD;
	else if (kstrtouint(tmp, 10, &new_threshold) != 0)
		return -EINVAL;

	if (new_threshold > 0)
		DRM_DEBUG_KMS("Setting HPD storm detection threshold to %d\n",
			      new_threshold);
	else
		DRM_DEBUG_KMS("Disabling HPD storm detection\n");

	spin_lock_irq(&dev_priv->irq_lock);
	hotplug->hpd_storm_threshold = new_threshold;
	/* Reset the HPD storm stats so we don't accidentally trigger a storm */
	for_each_hpd_pin(i)
		hotplug->stats[i].count = 0;
	spin_unlock_irq(&dev_priv->irq_lock);

	/* Re-enable hpd immediately if we were in an irq storm */
	flush_delayed_work(&dev_priv->hotplug.reenable_work);

	return len;
}

static int i915_hpd_storm_ctl_open(struct inode *inode, struct file *file)
{
	return single_open(file, i915_hpd_storm_ctl_show, inode->i_private);
}

static const struct file_operations i915_hpd_storm_ctl_fops = {
	.owner = THIS_MODULE,
	.open = i915_hpd_storm_ctl_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = i915_hpd_storm_ctl_write
};

static int i915_drrs_ctl_set(void *data, u64 val)
{
	struct drm_i915_private *dev_priv = data;
	struct drm_device *dev = &dev_priv->drm;
	struct intel_crtc *intel_crtc;
	struct intel_encoder *encoder;
	struct intel_dp *intel_dp;

	if (INTEL_GEN(dev_priv) < 7)
		return -ENODEV;

	drm_modeset_lock_all(dev);
	for_each_intel_crtc(dev, intel_crtc) {
		if (!intel_crtc->base.state->active ||
					!intel_crtc->config->has_drrs)
			continue;

		for_each_encoder_on_crtc(dev, &intel_crtc->base, encoder) {
			if (encoder->type != INTEL_OUTPUT_EDP)
				continue;

			DRM_DEBUG_DRIVER("Manually %sabling DRRS. %llu\n",
						val ? "en" : "dis", val);

			intel_dp = enc_to_intel_dp(&encoder->base);
			if (val)
				intel_edp_drrs_enable(intel_dp,
							intel_crtc->config);
			else
				intel_edp_drrs_disable(intel_dp,
							intel_crtc->config);
		}
	}
	drm_modeset_unlock_all(dev);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(i915_drrs_ctl_fops, NULL, i915_drrs_ctl_set, "%llu\n");

static ssize_t
i915_fifo_underrun_reset_write(struct file *filp,
			       const char __user *ubuf,
			       size_t cnt, loff_t *ppos)
{
	struct drm_i915_private *dev_priv = filp->private_data;
	struct intel_crtc *intel_crtc;
	struct drm_device *dev = &dev_priv->drm;
	int ret;
	bool reset;

	ret = kstrtobool_from_user(ubuf, cnt, &reset);
	if (ret)
		return ret;

	if (!reset)
		return cnt;

	for_each_intel_crtc(dev, intel_crtc) {
		struct drm_crtc_commit *commit;
		struct intel_crtc_state *crtc_state;

		ret = drm_modeset_lock_single_interruptible(&intel_crtc->base.mutex);
		if (ret)
			return ret;

		crtc_state = to_intel_crtc_state(intel_crtc->base.state);
		commit = crtc_state->base.commit;
		if (commit) {
			ret = wait_for_completion_interruptible(&commit->hw_done);
			if (!ret)
				ret = wait_for_completion_interruptible(&commit->flip_done);
		}

		if (!ret && crtc_state->base.active) {
			DRM_DEBUG_KMS("Re-arming FIFO underruns on pipe %c\n",
				      pipe_name(intel_crtc->pipe));

			intel_crtc_arm_fifo_underrun(intel_crtc, crtc_state);
		}

		drm_modeset_unlock(&intel_crtc->base.mutex);

		if (ret)
			return ret;
	}

	ret = intel_fbc_reset_underrun(dev_priv);
	if (ret)
		return ret;

	return cnt;
}

static const struct file_operations i915_fifo_underrun_reset_ops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = i915_fifo_underrun_reset_write,
	.llseek = default_llseek,
};

static const struct drm_info_list i915_debugfs_list[] = {
	{"i915_capabilities", i915_capabilities, 0},
	{"i915_gem_objects", i915_gem_object_info, 0},
	{"i915_gem_gtt", i915_gem_gtt_info, 0},
	{"i915_gem_stolen", i915_gem_stolen_list_info },
	{"i915_gem_fence_regs", i915_gem_fence_regs_info, 0},
	{"i915_gem_interrupt", i915_interrupt_info, 0},
	{"i915_gem_batch_pool", i915_gem_batch_pool_info, 0},
	{"i915_guc_info", i915_guc_info, 0},
	{"i915_guc_load_status", i915_guc_load_status_info, 0},
	{"i915_guc_log_dump", i915_guc_log_dump, 0},
	{"i915_guc_load_err_log_dump", i915_guc_log_dump, 0, (void *)1},
	{"i915_guc_stage_pool", i915_guc_stage_pool, 0},
	{"i915_huc_load_status", i915_huc_load_status_info, 0},
	{"i915_frequency_info", i915_frequency_info, 0},
	{"i915_hangcheck_info", i915_hangcheck_info, 0},
	{"i915_reset_info", i915_reset_info, 0},
	{"i915_drpc_info", i915_drpc_info, 0},
	{"i915_emon_status", i915_emon_status, 0},
	{"i915_ring_freq_table", i915_ring_freq_table, 0},
	{"i915_frontbuffer_tracking", i915_frontbuffer_tracking, 0},
	{"i915_fbc_status", i915_fbc_status, 0},
	{"i915_ips_status", i915_ips_status, 0},
	{"i915_sr_status", i915_sr_status, 0},
	{"i915_opregion", i915_opregion, 0},
	{"i915_vbt", i915_vbt, 0},
	{"i915_gem_framebuffer", i915_gem_framebuffer_info, 0},
	{"i915_context_status", i915_context_status, 0},
	{"i915_forcewake_domains", i915_forcewake_domains, 0},
	{"i915_swizzle_info", i915_swizzle_info, 0},
	{"i915_ppgtt_info", i915_ppgtt_info, 0},
	{"i915_llc", i915_llc, 0},
	{"i915_edp_psr_status", i915_edp_psr_status, 0},
	{"i915_sink_crc_eDP1", i915_sink_crc, 0},
	{"i915_energy_uJ", i915_energy_uJ, 0},
	{"i915_runtime_pm_status", i915_runtime_pm_status, 0},
	{"i915_power_domain_info", i915_power_domain_info, 0},
	{"i915_dmc_info", i915_dmc_info, 0},
	{"i915_display_info", i915_display_info, 0},
	{"i915_engine_info", i915_engine_info, 0},
	{"i915_rcs_topology", i915_rcs_topology, 0},
	{"i915_shrinker_info", i915_shrinker_info, 0},
	{"i915_shared_dplls_info", i915_shared_dplls_info, 0},
	{"i915_dp_mst_info", i915_dp_mst_info, 0},
	{"i915_wa_registers", i915_wa_registers, 0},
	{"i915_ddb_info", i915_ddb_info, 0},
	{"i915_sseu_status", i915_sseu_status, 0},
	{"i915_drrs_status", i915_drrs_status, 0},
	{"i915_rps_boost_info", i915_rps_boost_info, 0},
};
#define I915_DEBUGFS_ENTRIES ARRAY_SIZE(i915_debugfs_list)

static const struct i915_debugfs_files {
	const char *name;
	const struct file_operations *fops;
} i915_debugfs_files[] = {
	{"i915_wedged", &i915_wedged_fops},
	{"i915_cache_sharing", &i915_cache_sharing_fops},
	{"i915_ring_missed_irq", &i915_ring_missed_irq_fops},
	{"i915_ring_test_irq", &i915_ring_test_irq_fops},
	{"i915_gem_drop_caches", &i915_drop_caches_fops},
#if IS_ENABLED(CONFIG_DRM_I915_CAPTURE_ERROR)
	{"i915_error_state", &i915_error_state_fops},
	{"i915_gpu_info", &i915_gpu_info_fops},
#endif
	{"i915_fifo_underrun_reset", &i915_fifo_underrun_reset_ops},
	{"i915_next_seqno", &i915_next_seqno_fops},
	{"i915_display_crc_ctl", &i915_display_crc_ctl_fops},
	{"i915_pri_wm_latency", &i915_pri_wm_latency_fops},
	{"i915_spr_wm_latency", &i915_spr_wm_latency_fops},
	{"i915_cur_wm_latency", &i915_cur_wm_latency_fops},
	{"i915_fbc_false_color", &i915_fbc_false_color_fops},
	{"i915_dp_test_data", &i915_displayport_test_data_fops},
	{"i915_dp_test_type", &i915_displayport_test_type_fops},
	{"i915_dp_test_active", &i915_displayport_test_active_fops},
	{"i915_guc_log_level", &i915_guc_log_level_fops},
	{"i915_guc_log_relay", &i915_guc_log_relay_fops},
	{"i915_hpd_storm_ctl", &i915_hpd_storm_ctl_fops},
	{"i915_ipc_status", &i915_ipc_status_fops},
	{"i915_drrs_ctl", &i915_drrs_ctl_fops},
	{"i915_edp_psr_debug", &i915_edp_psr_debug_fops}
};

int i915_debugfs_register(struct drm_i915_private *dev_priv)
{
	struct drm_minor *minor = dev_priv->drm.primary;
	struct dentry *ent;
	int ret, i;

	ent = debugfs_create_file("i915_forcewake_user", S_IRUSR,
				  minor->debugfs_root, to_i915(minor->dev),
				  &i915_forcewake_fops);
	if (!ent)
		return -ENOMEM;

	ret = intel_pipe_crc_create(minor);
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(i915_debugfs_files); i++) {
		ent = debugfs_create_file(i915_debugfs_files[i].name,
					  S_IRUGO | S_IWUSR,
					  minor->debugfs_root,
					  to_i915(minor->dev),
					  i915_debugfs_files[i].fops);
		if (!ent)
			return -ENOMEM;
	}

	return drm_debugfs_create_files(i915_debugfs_list,
					I915_DEBUGFS_ENTRIES,
					minor->debugfs_root, minor);
}

struct dpcd_block {
	/* DPCD dump start address. */
	unsigned int offset;
	/* DPCD dump end address, inclusive. If unset, .size will be used. */
	unsigned int end;
	/* DPCD dump size. Used if .end is unset. If unset, defaults to 1. */
	size_t size;
	/* Only valid for eDP. */
	bool edp;
};

static const struct dpcd_block i915_dpcd_debug[] = {
	{ .offset = DP_DPCD_REV, .size = DP_RECEIVER_CAP_SIZE },
	{ .offset = DP_PSR_SUPPORT, .end = DP_PSR_CAPS },
	{ .offset = DP_DOWNSTREAM_PORT_0, .size = 16 },
	{ .offset = DP_LINK_BW_SET, .end = DP_EDP_CONFIGURATION_SET },
	{ .offset = DP_SINK_COUNT, .end = DP_ADJUST_REQUEST_LANE2_3 },
	{ .offset = DP_SET_POWER },
	{ .offset = DP_EDP_DPCD_REV },
	{ .offset = DP_EDP_GENERAL_CAP_1, .end = DP_EDP_GENERAL_CAP_3 },
	{ .offset = DP_EDP_DISPLAY_CONTROL_REGISTER, .end = DP_EDP_BACKLIGHT_FREQ_CAP_MAX_LSB },
	{ .offset = DP_EDP_DBC_MINIMUM_BRIGHTNESS_SET, .end = DP_EDP_DBC_MAXIMUM_BRIGHTNESS_SET },
};

static int i915_dpcd_show(struct seq_file *m, void *data)
{
	struct drm_connector *connector = m->private;
	struct intel_dp *intel_dp =
		enc_to_intel_dp(&intel_attached_encoder(connector)->base);
	uint8_t buf[16];
	ssize_t err;
	int i;

	if (connector->status != connector_status_connected)
		return -ENODEV;

	for (i = 0; i < ARRAY_SIZE(i915_dpcd_debug); i++) {
		const struct dpcd_block *b = &i915_dpcd_debug[i];
		size_t size = b->end ? b->end - b->offset + 1 : (b->size ?: 1);

		if (b->edp &&
		    connector->connector_type != DRM_MODE_CONNECTOR_eDP)
			continue;

		/* low tech for now */
		if (WARN_ON(size > sizeof(buf)))
			continue;

		err = drm_dp_dpcd_read(&intel_dp->aux, b->offset, buf, size);
		if (err <= 0) {
			DRM_ERROR("dpcd read (%zu bytes at %u) failed (%zd)\n",
				  size, b->offset, err);
			continue;
		}

		seq_printf(m, "%04x: %*ph\n", b->offset, (int) size, buf);
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(i915_dpcd);

static int i915_panel_show(struct seq_file *m, void *data)
{
	struct drm_connector *connector = m->private;
	struct intel_dp *intel_dp =
		enc_to_intel_dp(&intel_attached_encoder(connector)->base);

	if (connector->status != connector_status_connected)
		return -ENODEV;

	seq_printf(m, "Panel power up delay: %d\n",
		   intel_dp->panel_power_up_delay);
	seq_printf(m, "Panel power down delay: %d\n",
		   intel_dp->panel_power_down_delay);
	seq_printf(m, "Backlight on delay: %d\n",
		   intel_dp->backlight_on_delay);
	seq_printf(m, "Backlight off delay: %d\n",
		   intel_dp->backlight_off_delay);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(i915_panel);

/**
 * i915_debugfs_connector_add - add i915 specific connector debugfs files
 * @connector: pointer to a registered drm_connector
 *
 * Cleanup will be done by drm_connector_unregister() through a call to
 * drm_debugfs_connector_remove().
 *
 * Returns 0 on success, negative error codes on error.
 */
int i915_debugfs_connector_add(struct drm_connector *connector)
{
	struct dentry *root = connector->debugfs_entry;

	/* The connector must have been registered beforehands. */
	if (!root)
		return -ENODEV;

	if (connector->connector_type == DRM_MODE_CONNECTOR_DisplayPort ||
	    connector->connector_type == DRM_MODE_CONNECTOR_eDP)
		debugfs_create_file("i915_dpcd", S_IRUGO, root,
				    connector, &i915_dpcd_fops);

	if (connector->connector_type == DRM_MODE_CONNECTOR_eDP)
		debugfs_create_file("i915_panel_timings", S_IRUGO, root,
				    connector, &i915_panel_fops);

	return 0;
}
