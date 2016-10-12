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

#include <linux/seq_file.h>
#include <linux/circ_buf.h>
#include <linux/ctype.h>
#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/list_sort.h>
#include <asm/msr-index.h>
#include <drm/drmP.h>
#include "intel_drv.h"
#include "intel_ringbuffer.h"
#include <drm/i915_drm.h>
#include "i915_drv.h"

static inline struct drm_i915_private *node_to_i915(struct drm_info_node *node)
{
	return to_i915(node->minor->dev);
}

/* As the drm_debugfs_init() routines are called before dev->dev_private is
 * allocated we need to hook into the minor for release. */
static int
drm_add_fake_info_node(struct drm_minor *minor,
		       struct dentry *ent,
		       const void *key)
{
	struct drm_info_node *node;

	node = kmalloc(sizeof(*node), GFP_KERNEL);
	if (node == NULL) {
		debugfs_remove(ent);
		return -ENOMEM;
	}

	node->minor = minor;
	node->dent = ent;
	node->info_ent = (void *)key;

	mutex_lock(&minor->debugfs_lock);
	list_add(&node->list, &minor->debugfs_list);
	mutex_unlock(&minor->debugfs_lock);

	return 0;
}

static int i915_capabilities(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	const struct intel_device_info *info = INTEL_INFO(dev_priv);

	seq_printf(m, "gen: %d\n", INTEL_GEN(dev_priv));
	seq_printf(m, "pch: %d\n", INTEL_PCH_TYPE(dev_priv));
#define PRINT_FLAG(x)  seq_printf(m, #x ": %s\n", yesno(info->x))
	DEV_INFO_FOR_EACH_FLAG(PRINT_FLAG);
#undef PRINT_FLAG

	return 0;
}

static char get_active_flag(struct drm_i915_gem_object *obj)
{
	return i915_gem_object_is_active(obj) ? '*' : ' ';
}

static char get_pin_flag(struct drm_i915_gem_object *obj)
{
	return obj->pin_display ? 'p' : ' ';
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
	return i915_gem_object_to_ggtt(obj, NULL) ?  'g' : ' ';
}

static char get_pin_mapped_flag(struct drm_i915_gem_object *obj)
{
	return obj->mapping ? 'M' : ' ';
}

static u64 i915_gem_obj_total_ggtt_size(struct drm_i915_gem_object *obj)
{
	u64 size = 0;
	struct i915_vma *vma;

	list_for_each_entry(vma, &obj->vma_list, obj_link) {
		if (i915_vma_is_ggtt(vma) && drm_mm_node_allocated(&vma->node))
			size += vma->node.size;
	}

	return size;
}

static void
describe_obj(struct seq_file *m, struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *dev_priv = to_i915(obj->base.dev);
	struct intel_engine_cs *engine;
	struct i915_vma *vma;
	unsigned int frontbuffer_bits;
	int pin_count = 0;
	enum intel_engine_id id;

	lockdep_assert_held(&obj->base.dev->struct_mutex);

	seq_printf(m, "%pK: %c%c%c%c%c %8zdKiB %02x %02x [ ",
		   &obj->base,
		   get_active_flag(obj),
		   get_pin_flag(obj),
		   get_tiling_flag(obj),
		   get_global_flag(obj),
		   get_pin_mapped_flag(obj),
		   obj->base.size / 1024,
		   obj->base.read_domains,
		   obj->base.write_domain);
	for_each_engine_id(engine, dev_priv, id)
		seq_printf(m, "%x ",
			   i915_gem_active_get_seqno(&obj->last_read[id],
						     &obj->base.dev->struct_mutex));
	seq_printf(m, "] %x %s%s%s",
		   i915_gem_active_get_seqno(&obj->last_write,
					     &obj->base.dev->struct_mutex),
		   i915_cache_level_str(dev_priv, obj->cache_level),
		   obj->dirty ? " dirty" : "",
		   obj->madv == I915_MADV_DONTNEED ? " purgeable" : "");
	if (obj->base.name)
		seq_printf(m, " (name: %d)", obj->base.name);
	list_for_each_entry(vma, &obj->vma_list, obj_link) {
		if (i915_vma_is_pinned(vma))
			pin_count++;
	}
	seq_printf(m, " (pinned x %d)", pin_count);
	if (obj->pin_display)
		seq_printf(m, " (display)");
	list_for_each_entry(vma, &obj->vma_list, obj_link) {
		if (!drm_mm_node_allocated(&vma->node))
			continue;

		seq_printf(m, " (%sgtt offset: %08llx, size: %08llx",
			   i915_vma_is_ggtt(vma) ? "g" : "pp",
			   vma->node.start, vma->node.size);
		if (i915_vma_is_ggtt(vma))
			seq_printf(m, ", type: %u", vma->ggtt_view.type);
		if (vma->fence)
			seq_printf(m, " , fence: %d%s",
				   vma->fence->id,
				   i915_gem_active_isset(&vma->last_fence) ? "*" : "");
		seq_puts(m, ")");
	}
	if (obj->stolen)
		seq_printf(m, " (stolen: %08llx)", obj->stolen->start);
	if (obj->pin_display || obj->fault_mappable) {
		char s[3], *t = s;
		if (obj->pin_display)
			*t++ = 'p';
		if (obj->fault_mappable)
			*t++ = 'f';
		*t = '\0';
		seq_printf(m, " (%s mappable)", s);
	}

	engine = i915_gem_active_get_engine(&obj->last_write,
					    &dev_priv->drm.struct_mutex);
	if (engine)
		seq_printf(m, " (%s)", engine->name);

	frontbuffer_bits = atomic_read(&obj->frontbuffer_bits);
	if (frontbuffer_bits)
		seq_printf(m, " (frontbuffer: 0x%03x)", frontbuffer_bits);
}

static int obj_rank_by_stolen(void *priv,
			      struct list_head *A, struct list_head *B)
{
	struct drm_i915_gem_object *a =
		container_of(A, struct drm_i915_gem_object, obj_exec_link);
	struct drm_i915_gem_object *b =
		container_of(B, struct drm_i915_gem_object, obj_exec_link);

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
	struct drm_i915_gem_object *obj;
	u64 total_obj_size, total_gtt_size;
	LIST_HEAD(stolen);
	int count, ret;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	total_obj_size = total_gtt_size = count = 0;
	list_for_each_entry(obj, &dev_priv->mm.bound_list, global_list) {
		if (obj->stolen == NULL)
			continue;

		list_add(&obj->obj_exec_link, &stolen);

		total_obj_size += obj->base.size;
		total_gtt_size += i915_gem_obj_total_ggtt_size(obj);
		count++;
	}
	list_for_each_entry(obj, &dev_priv->mm.unbound_list, global_list) {
		if (obj->stolen == NULL)
			continue;

		list_add(&obj->obj_exec_link, &stolen);

		total_obj_size += obj->base.size;
		count++;
	}
	list_sort(NULL, &stolen, obj_rank_by_stolen);
	seq_puts(m, "Stolen:\n");
	while (!list_empty(&stolen)) {
		obj = list_first_entry(&stolen, typeof(*obj), obj_exec_link);
		seq_puts(m, "   ");
		describe_obj(m, obj);
		seq_putc(m, '\n');
		list_del_init(&obj->obj_exec_link);
	}
	mutex_unlock(&dev->struct_mutex);

	seq_printf(m, "Total %d objects, %llu bytes, %llu GTT size\n",
		   count, total_obj_size, total_gtt_size);
	return 0;
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
	int j;

	memset(&stats, 0, sizeof(stats));

	for_each_engine(engine, dev_priv) {
		for (j = 0; j < ARRAY_SIZE(engine->batch_pool.cache_list); j++) {
			list_for_each_entry(obj,
					    &engine->batch_pool.cache_list[j],
					    batch_pool_link)
				per_file_stats(0, obj, &stats);
		}
	}

	print_file_stats(m, "[k]batch pool", stats);
}

static int per_file_ctx_stats(int id, void *ptr, void *data)
{
	struct i915_gem_context *ctx = ptr;
	int n;

	for (n = 0; n < ARRAY_SIZE(ctx->engine); n++) {
		if (ctx->engine[n].state)
			per_file_stats(0, ctx->engine[n].state->obj, data);
		if (ctx->engine[n].ring)
			per_file_stats(0, ctx->engine[n].ring->vma->obj, data);
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
	u32 count, mapped_count, purgeable_count, dpy_count;
	u64 size, mapped_size, purgeable_size, dpy_size;
	struct drm_i915_gem_object *obj;
	struct drm_file *file;
	int ret;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	seq_printf(m, "%u objects, %zu bytes\n",
		   dev_priv->mm.object_count,
		   dev_priv->mm.object_memory);

	size = count = 0;
	mapped_size = mapped_count = 0;
	purgeable_size = purgeable_count = 0;
	list_for_each_entry(obj, &dev_priv->mm.unbound_list, global_list) {
		size += obj->base.size;
		++count;

		if (obj->madv == I915_MADV_DONTNEED) {
			purgeable_size += obj->base.size;
			++purgeable_count;
		}

		if (obj->mapping) {
			mapped_count++;
			mapped_size += obj->base.size;
		}
	}
	seq_printf(m, "%u unbound objects, %llu bytes\n", count, size);

	size = count = dpy_size = dpy_count = 0;
	list_for_each_entry(obj, &dev_priv->mm.bound_list, global_list) {
		size += obj->base.size;
		++count;

		if (obj->pin_display) {
			dpy_size += obj->base.size;
			++dpy_count;
		}

		if (obj->madv == I915_MADV_DONTNEED) {
			purgeable_size += obj->base.size;
			++purgeable_count;
		}

		if (obj->mapping) {
			mapped_count++;
			mapped_size += obj->base.size;
		}
	}
	seq_printf(m, "%u bound objects, %llu bytes\n",
		   count, size);
	seq_printf(m, "%u purgeable objects, %llu bytes\n",
		   purgeable_count, purgeable_size);
	seq_printf(m, "%u mapped objects, %llu bytes\n",
		   mapped_count, mapped_size);
	seq_printf(m, "%u display objects (pinned), %llu bytes\n",
		   dpy_count, dpy_size);

	seq_printf(m, "%llu [%llu] gtt total\n",
		   ggtt->base.total, ggtt->mappable_end - ggtt->base.start);

	seq_putc(m, '\n');
	print_batch_pool_stats(m, dev_priv);
	mutex_unlock(&dev->struct_mutex);

	mutex_lock(&dev->filelist_mutex);
	print_context_stats(m, dev_priv);
	list_for_each_entry_reverse(file, &dev->filelist, lhead) {
		struct file_stats stats;
		struct drm_i915_file_private *file_priv = file->driver_priv;
		struct drm_i915_gem_request *request;
		struct task_struct *task;

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
		mutex_lock(&dev->struct_mutex);
		request = list_first_entry_or_null(&file_priv->mm.request_list,
						   struct drm_i915_gem_request,
						   client_list);
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
	bool show_pin_display_only = !!node->info_ent->data;
	struct drm_i915_gem_object *obj;
	u64 total_obj_size, total_gtt_size;
	int count, ret;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	total_obj_size = total_gtt_size = count = 0;
	list_for_each_entry(obj, &dev_priv->mm.bound_list, global_list) {
		if (show_pin_display_only && !obj->pin_display)
			continue;

		seq_puts(m, "   ");
		describe_obj(m, obj);
		seq_putc(m, '\n');
		total_obj_size += obj->base.size;
		total_gtt_size += i915_gem_obj_total_ggtt_size(obj);
		count++;
	}

	mutex_unlock(&dev->struct_mutex);

	seq_printf(m, "Total %d objects, %llu bytes, %llu GTT size\n",
		   count, total_obj_size, total_gtt_size);

	return 0;
}

static int i915_gem_pageflip_info(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct drm_device *dev = &dev_priv->drm;
	struct intel_crtc *crtc;
	int ret;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	for_each_intel_crtc(dev, crtc) {
		const char pipe = pipe_name(crtc->pipe);
		const char plane = plane_name(crtc->plane);
		struct intel_flip_work *work;

		spin_lock_irq(&dev->event_lock);
		work = crtc->flip_work;
		if (work == NULL) {
			seq_printf(m, "No flip due on pipe %c (plane %c)\n",
				   pipe, plane);
		} else {
			u32 pending;
			u32 addr;

			pending = atomic_read(&work->pending);
			if (pending) {
				seq_printf(m, "Flip ioctl preparing on pipe %c (plane %c)\n",
					   pipe, plane);
			} else {
				seq_printf(m, "Flip pending (waiting for vsync) on pipe %c (plane %c)\n",
					   pipe, plane);
			}
			if (work->flip_queued_req) {
				struct intel_engine_cs *engine = i915_gem_request_get_engine(work->flip_queued_req);

				seq_printf(m, "Flip queued on %s at seqno %x, next seqno %x [current breadcrumb %x], completed? %d\n",
					   engine->name,
					   i915_gem_request_get_seqno(work->flip_queued_req),
					   dev_priv->next_seqno,
					   intel_engine_get_seqno(engine),
					   i915_gem_request_completed(work->flip_queued_req));
			} else
				seq_printf(m, "Flip not associated with any ring\n");
			seq_printf(m, "Flip queued on frame %d, (was ready on frame %d), now %d\n",
				   work->flip_queued_vblank,
				   work->flip_ready_vblank,
				   intel_crtc_get_vblank_counter(crtc));
			seq_printf(m, "%d prepares\n", atomic_read(&work->pending));

			if (INTEL_GEN(dev_priv) >= 4)
				addr = I915_HI_DISPBASE(I915_READ(DSPSURF(crtc->plane)));
			else
				addr = I915_READ(DSPADDR(crtc->plane));
			seq_printf(m, "Current scanout address 0x%08x\n", addr);

			if (work->pending_flip_obj) {
				seq_printf(m, "New framebuffer address 0x%08lx\n", (long)work->gtt_offset);
				seq_printf(m, "MMIO update completed? %d\n",  addr == work->gtt_offset);
			}
		}
		spin_unlock_irq(&dev->event_lock);
	}

	mutex_unlock(&dev->struct_mutex);

	return 0;
}

static int i915_gem_batch_pool_info(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct drm_device *dev = &dev_priv->drm;
	struct drm_i915_gem_object *obj;
	struct intel_engine_cs *engine;
	int total = 0;
	int ret, j;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	for_each_engine(engine, dev_priv) {
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

static void print_request(struct seq_file *m,
			  struct drm_i915_gem_request *rq,
			  const char *prefix)
{
	struct pid *pid = rq->ctx->pid;
	struct task_struct *task;

	rcu_read_lock();
	task = pid ? pid_task(pid, PIDTYPE_PID) : NULL;
	seq_printf(m, "%s%x [%x:%x] @ %d: %s [%d]\n", prefix,
		   rq->fence.seqno, rq->ctx->hw_id, rq->fence.seqno,
		   jiffies_to_msecs(jiffies - rq->emitted_jiffies),
		   task ? task->comm : "<unknown>",
		   task ? task->pid : -1);
	rcu_read_unlock();
}

static int i915_gem_request_info(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct drm_device *dev = &dev_priv->drm;
	struct intel_engine_cs *engine;
	struct drm_i915_gem_request *req;
	int ret, any;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	any = 0;
	for_each_engine(engine, dev_priv) {
		int count;

		count = 0;
		list_for_each_entry(req, &engine->request_list, link)
			count++;
		if (count == 0)
			continue;

		seq_printf(m, "%s requests: %d\n", engine->name, count);
		list_for_each_entry(req, &engine->request_list, link)
			print_request(m, req, "    ");

		any++;
	}
	mutex_unlock(&dev->struct_mutex);

	if (any == 0)
		seq_puts(m, "No requests\n");

	return 0;
}

static void i915_ring_seqno_info(struct seq_file *m,
				 struct intel_engine_cs *engine)
{
	struct intel_breadcrumbs *b = &engine->breadcrumbs;
	struct rb_node *rb;

	seq_printf(m, "Current sequence (%s): %x\n",
		   engine->name, intel_engine_get_seqno(engine));

	spin_lock(&b->lock);
	for (rb = rb_first(&b->waiters); rb; rb = rb_next(rb)) {
		struct intel_wait *w = container_of(rb, typeof(*w), node);

		seq_printf(m, "Waiting (%s): %s [%d] on %x\n",
			   engine->name, w->tsk->comm, w->tsk->pid, w->seqno);
	}
	spin_unlock(&b->lock);
}

static int i915_gem_seqno_info(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct intel_engine_cs *engine;

	for_each_engine(engine, dev_priv)
		i915_ring_seqno_info(m, engine);

	return 0;
}


static int i915_interrupt_info(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct intel_engine_cs *engine;
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
		for_each_pipe(dev_priv, pipe)
			seq_printf(m, "Pipe %c stat:\t%08x\n",
				   pipe_name(pipe),
				   I915_READ(PIPESTAT(pipe)));

		seq_printf(m, "Port hotplug:\t%08x\n",
			   I915_READ(PORT_HOTPLUG_EN));
		seq_printf(m, "DPFLIPSTAT:\t%08x\n",
			   I915_READ(VLV_DPFLIPSTAT));
		seq_printf(m, "DPINVGTT:\t%08x\n",
			   I915_READ(DPINVGTT));

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
	} else if (IS_VALLEYVIEW(dev_priv)) {
		seq_printf(m, "Display IER:\t%08x\n",
			   I915_READ(VLV_IER));
		seq_printf(m, "Display IIR:\t%08x\n",
			   I915_READ(VLV_IIR));
		seq_printf(m, "Display IIR_RW:\t%08x\n",
			   I915_READ(VLV_IIR_RW));
		seq_printf(m, "Display IMR:\t%08x\n",
			   I915_READ(VLV_IMR));
		for_each_pipe(dev_priv, pipe)
			seq_printf(m, "Pipe %c stat:\t%08x\n",
				   pipe_name(pipe),
				   I915_READ(PIPESTAT(pipe)));

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
	for_each_engine(engine, dev_priv) {
		if (INTEL_GEN(dev_priv) >= 6) {
			seq_printf(m,
				   "Graphics Interrupt mask (%s):	%08x\n",
				   engine->name, I915_READ_IMR(engine));
		}
		i915_ring_seqno_info(m, engine);
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

static int i915_hws_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = m->private;
	struct drm_i915_private *dev_priv = node_to_i915(node);
	struct intel_engine_cs *engine;
	const u32 *hws;
	int i;

	engine = &dev_priv->engine[(uintptr_t)node->info_ent->data];
	hws = engine->status_page.page_addr;
	if (hws == NULL)
		return 0;

	for (i = 0; i < 4096 / sizeof(u32) / 4; i += 4) {
		seq_printf(m, "0x%08x: 0x%08x 0x%08x 0x%08x 0x%08x\n",
			   i * 4,
			   hws[i], hws[i + 1], hws[i + 2], hws[i + 3]);
	}
	return 0;
}

static ssize_t
i915_error_state_write(struct file *filp,
		       const char __user *ubuf,
		       size_t cnt,
		       loff_t *ppos)
{
	struct i915_error_state_file_priv *error_priv = filp->private_data;

	DRM_DEBUG_DRIVER("Resetting error state\n");
	i915_destroy_error_state(error_priv->dev);

	return cnt;
}

static int i915_error_state_open(struct inode *inode, struct file *file)
{
	struct drm_i915_private *dev_priv = inode->i_private;
	struct i915_error_state_file_priv *error_priv;

	error_priv = kzalloc(sizeof(*error_priv), GFP_KERNEL);
	if (!error_priv)
		return -ENOMEM;

	error_priv->dev = &dev_priv->drm;

	i915_error_state_get(&dev_priv->drm, error_priv);

	file->private_data = error_priv;

	return 0;
}

static int i915_error_state_release(struct inode *inode, struct file *file)
{
	struct i915_error_state_file_priv *error_priv = file->private_data;

	i915_error_state_put(error_priv);
	kfree(error_priv);

	return 0;
}

static ssize_t i915_error_state_read(struct file *file, char __user *userbuf,
				     size_t count, loff_t *pos)
{
	struct i915_error_state_file_priv *error_priv = file->private_data;
	struct drm_i915_error_state_buf error_str;
	loff_t tmp_pos = 0;
	ssize_t ret_count = 0;
	int ret;

	ret = i915_error_state_buf_init(&error_str,
					to_i915(error_priv->dev), count, *pos);
	if (ret)
		return ret;

	ret = i915_error_state_to_str(&error_str, error_priv);
	if (ret)
		goto out;

	ret_count = simple_read_from_buffer(userbuf, count, &tmp_pos,
					    error_str.buf,
					    error_str.bytes);

	if (ret_count < 0)
		ret = ret_count;
	else
		*pos = error_str.start + ret_count;
out:
	i915_error_state_buf_release(&error_str);
	return ret ?: ret_count;
}

static const struct file_operations i915_error_state_fops = {
	.owner = THIS_MODULE,
	.open = i915_error_state_open,
	.read = i915_error_state_read,
	.write = i915_error_state_write,
	.llseek = default_llseek,
	.release = i915_error_state_release,
};

static int
i915_next_seqno_get(void *data, u64 *val)
{
	struct drm_i915_private *dev_priv = data;
	int ret;

	ret = mutex_lock_interruptible(&dev_priv->drm.struct_mutex);
	if (ret)
		return ret;

	*val = dev_priv->next_seqno;
	mutex_unlock(&dev_priv->drm.struct_mutex);

	return 0;
}

static int
i915_next_seqno_set(void *data, u64 val)
{
	struct drm_i915_private *dev_priv = data;
	struct drm_device *dev = &dev_priv->drm;
	int ret;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	ret = i915_gem_set_seqno(dev, val);
	mutex_unlock(&dev->struct_mutex);

	return ret;
}

DEFINE_SIMPLE_ATTRIBUTE(i915_next_seqno_fops,
			i915_next_seqno_get, i915_next_seqno_set,
			"0x%llx\n");

static int i915_frequency_info(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct drm_device *dev = &dev_priv->drm;
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
		u32 freq_sts;

		mutex_lock(&dev_priv->rps.hw_lock);
		freq_sts = vlv_punit_read(dev_priv, PUNIT_REG_GPU_FREQ_STS);
		seq_printf(m, "PUNIT_REG_GPU_FREQ_STS: 0x%08x\n", freq_sts);
		seq_printf(m, "DDR freq: %d MHz\n", dev_priv->mem_freq);

		seq_printf(m, "actual GPU freq: %d MHz\n",
			   intel_gpu_freq(dev_priv, (freq_sts >> 8) & 0xff));

		seq_printf(m, "current GPU freq: %d MHz\n",
			   intel_gpu_freq(dev_priv, dev_priv->rps.cur_freq));

		seq_printf(m, "max GPU freq: %d MHz\n",
			   intel_gpu_freq(dev_priv, dev_priv->rps.max_freq));

		seq_printf(m, "min GPU freq: %d MHz\n",
			   intel_gpu_freq(dev_priv, dev_priv->rps.min_freq));

		seq_printf(m, "idle GPU freq: %d MHz\n",
			   intel_gpu_freq(dev_priv, dev_priv->rps.idle_freq));

		seq_printf(m,
			   "efficient (RPe) frequency: %d MHz\n",
			   intel_gpu_freq(dev_priv, dev_priv->rps.efficient_freq));
		mutex_unlock(&dev_priv->rps.hw_lock);
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
		if (IS_BROXTON(dev_priv)) {
			rp_state_cap = I915_READ(BXT_RP_STATE_CAP);
			gt_perf_status = I915_READ(BXT_GT_PERF_STATUS);
		} else {
			rp_state_cap = I915_READ(GEN6_RP_STATE_CAP);
			gt_perf_status = I915_READ(GEN6_GT_PERF_STATUS);
		}

		/* RPSTAT1 is in the GT power well */
		ret = mutex_lock_interruptible(&dev->struct_mutex);
		if (ret)
			goto out;

		intel_uncore_forcewake_get(dev_priv, FORCEWAKE_ALL);

		reqf = I915_READ(GEN6_RPNSWREQ);
		if (IS_GEN9(dev_priv))
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
		if (IS_GEN9(dev_priv))
			cagf = (rpstat & GEN9_CAGF_MASK) >> GEN9_CAGF_SHIFT;
		else if (IS_HASWELL(dev_priv) || IS_BROADWELL(dev_priv))
			cagf = (rpstat & HSW_CAGF_MASK) >> HSW_CAGF_SHIFT;
		else
			cagf = (rpstat & GEN6_CAGF_MASK) >> GEN6_CAGF_SHIFT;
		cagf = intel_gpu_freq(dev_priv, cagf);

		intel_uncore_forcewake_put(dev_priv, FORCEWAKE_ALL);
		mutex_unlock(&dev->struct_mutex);

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
		seq_printf(m, "PM IER=0x%08x IMR=0x%08x ISR=0x%08x IIR=0x%08x, MASK=0x%08x\n",
			   pm_ier, pm_imr, pm_isr, pm_iir, pm_mask);
		seq_printf(m, "pm_intr_keep: 0x%08x\n", dev_priv->rps.pm_intr_keep);
		seq_printf(m, "GT_PERF_STATUS: 0x%08x\n", gt_perf_status);
		seq_printf(m, "Render p-state ratio: %d\n",
			   (gt_perf_status & (IS_GEN9(dev_priv) ? 0x1ff00 : 0xff00)) >> 8);
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
		seq_printf(m, "Up threshold: %d%%\n",
			   dev_priv->rps.up_threshold);

		seq_printf(m, "RP CUR DOWN EI: %d (%dus)\n",
			   rpdownei, GT_PM_INTERVAL_TO_US(dev_priv, rpdownei));
		seq_printf(m, "RP CUR DOWN: %d (%dus)\n",
			   rpcurdown, GT_PM_INTERVAL_TO_US(dev_priv, rpcurdown));
		seq_printf(m, "RP PREV DOWN: %d (%dus)\n",
			   rpprevdown, GT_PM_INTERVAL_TO_US(dev_priv, rpprevdown));
		seq_printf(m, "Down threshold: %d%%\n",
			   dev_priv->rps.down_threshold);

		max_freq = (IS_BROXTON(dev_priv) ? rp_state_cap >> 0 :
			    rp_state_cap >> 16) & 0xff;
		max_freq *= (IS_SKYLAKE(dev_priv) || IS_KABYLAKE(dev_priv) ?
			     GEN9_FREQ_SCALER : 1);
		seq_printf(m, "Lowest (RPN) frequency: %dMHz\n",
			   intel_gpu_freq(dev_priv, max_freq));

		max_freq = (rp_state_cap & 0xff00) >> 8;
		max_freq *= (IS_SKYLAKE(dev_priv) || IS_KABYLAKE(dev_priv) ?
			     GEN9_FREQ_SCALER : 1);
		seq_printf(m, "Nominal (RP1) frequency: %dMHz\n",
			   intel_gpu_freq(dev_priv, max_freq));

		max_freq = (IS_BROXTON(dev_priv) ? rp_state_cap >> 16 :
			    rp_state_cap >> 0) & 0xff;
		max_freq *= (IS_SKYLAKE(dev_priv) || IS_KABYLAKE(dev_priv) ?
			     GEN9_FREQ_SCALER : 1);
		seq_printf(m, "Max non-overclocked (RP0) frequency: %dMHz\n",
			   intel_gpu_freq(dev_priv, max_freq));
		seq_printf(m, "Max overclocked frequency: %dMHz\n",
			   intel_gpu_freq(dev_priv, dev_priv->rps.max_freq));

		seq_printf(m, "Current freq: %d MHz\n",
			   intel_gpu_freq(dev_priv, dev_priv->rps.cur_freq));
		seq_printf(m, "Actual freq: %d MHz\n", cagf);
		seq_printf(m, "Idle freq: %d MHz\n",
			   intel_gpu_freq(dev_priv, dev_priv->rps.idle_freq));
		seq_printf(m, "Min freq: %d MHz\n",
			   intel_gpu_freq(dev_priv, dev_priv->rps.min_freq));
		seq_printf(m, "Boost freq: %d MHz\n",
			   intel_gpu_freq(dev_priv, dev_priv->rps.boost_freq));
		seq_printf(m, "Max freq: %d MHz\n",
			   intel_gpu_freq(dev_priv, dev_priv->rps.max_freq));
		seq_printf(m,
			   "efficient (RPe) frequency: %d MHz\n",
			   intel_gpu_freq(dev_priv, dev_priv->rps.efficient_freq));
	} else {
		seq_puts(m, "no P-state info available\n");
	}

	seq_printf(m, "Current CD clock frequency: %d kHz\n", dev_priv->cdclk_freq);
	seq_printf(m, "Max CD clock frequency: %d kHz\n", dev_priv->max_cdclk_freq);
	seq_printf(m, "Max pixel clock frequency: %d kHz\n", dev_priv->max_dotclk_freq);

out:
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
		seq_printf(m, "Wedged\n");
	if (test_bit(I915_RESET_IN_PROGRESS, &dev_priv->gpu_error.flags))
		seq_printf(m, "Reset in progress\n");
	if (waitqueue_active(&dev_priv->gpu_error.wait_queue))
		seq_printf(m, "Waiter holding struct mutex\n");
	if (waitqueue_active(&dev_priv->gpu_error.reset_queue))
		seq_printf(m, "struct_mutex blocked for reset\n");

	if (!i915.enable_hangcheck) {
		seq_printf(m, "Hangcheck disabled\n");
		return 0;
	}

	intel_runtime_pm_get(dev_priv);

	for_each_engine_id(engine, dev_priv, id) {
		acthd[id] = intel_engine_get_active_head(engine);
		seqno[id] = intel_engine_get_seqno(engine);
	}

	i915_get_engine_instdone(dev_priv, RCS, &instdone);

	intel_runtime_pm_put(dev_priv);

	if (delayed_work_pending(&dev_priv->gpu_error.hangcheck_work)) {
		seq_printf(m, "Hangcheck active, fires in %dms\n",
			   jiffies_to_msecs(dev_priv->gpu_error.hangcheck_work.timer.expires -
					    jiffies));
	} else
		seq_printf(m, "Hangcheck inactive\n");

	for_each_engine_id(engine, dev_priv, id) {
		struct intel_breadcrumbs *b = &engine->breadcrumbs;
		struct rb_node *rb;

		seq_printf(m, "%s:\n", engine->name);
		seq_printf(m, "\tseqno = %x [current %x, last %x]\n",
			   engine->hangcheck.seqno,
			   seqno[id],
			   engine->last_submitted_seqno);
		seq_printf(m, "\twaiters? %s, fake irq active? %s\n",
			   yesno(intel_engine_has_waiter(engine)),
			   yesno(test_bit(engine->id,
					  &dev_priv->gpu_error.missed_irq_rings)));
		spin_lock(&b->lock);
		for (rb = rb_first(&b->waiters); rb; rb = rb_next(rb)) {
			struct intel_wait *w = container_of(rb, typeof(*w), node);

			seq_printf(m, "\t%s [%d] waiting for %x\n",
				   w->tsk->comm, w->tsk->pid, w->seqno);
		}
		spin_unlock(&b->lock);

		seq_printf(m, "\tACTHD = 0x%08llx [current 0x%08llx]\n",
			   (long long)engine->hangcheck.acthd,
			   (long long)acthd[id]);
		seq_printf(m, "\tscore = %d\n", engine->hangcheck.score);
		seq_printf(m, "\taction = %d\n", engine->hangcheck.action);

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

static int ironlake_drpc_info(struct seq_file *m)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct drm_device *dev = &dev_priv->drm;
	u32 rgvmodectl, rstdbyctl;
	u16 crstandvid;
	int ret;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;
	intel_runtime_pm_get(dev_priv);

	rgvmodectl = I915_READ(MEMMODECTL);
	rstdbyctl = I915_READ(RSTDBYCTL);
	crstandvid = I915_READ16(CRSTANDVID);

	intel_runtime_pm_put(dev_priv);
	mutex_unlock(&dev->struct_mutex);

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
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct intel_uncore_forcewake_domain *fw_domain;

	spin_lock_irq(&dev_priv->uncore.lock);
	for_each_fw_domain(fw_domain, dev_priv) {
		seq_printf(m, "%s.wake_count = %u\n",
			   intel_uncore_forcewake_domain_to_str(fw_domain->id),
			   fw_domain->wake_count);
	}
	spin_unlock_irq(&dev_priv->uncore.lock);

	return 0;
}

static int vlv_drpc_info(struct seq_file *m)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	u32 rpmodectl1, rcctl1, pw_status;

	intel_runtime_pm_get(dev_priv);

	pw_status = I915_READ(VLV_GTLC_PW_STATUS);
	rpmodectl1 = I915_READ(GEN6_RP_CONTROL);
	rcctl1 = I915_READ(GEN6_RC_CONTROL);

	intel_runtime_pm_put(dev_priv);

	seq_printf(m, "Video Turbo Mode: %s\n",
		   yesno(rpmodectl1 & GEN6_RP_MEDIA_TURBO));
	seq_printf(m, "Turbo enabled: %s\n",
		   yesno(rpmodectl1 & GEN6_RP_ENABLE));
	seq_printf(m, "HW control enabled: %s\n",
		   yesno(rpmodectl1 & GEN6_RP_ENABLE));
	seq_printf(m, "SW control enabled: %s\n",
		   yesno((rpmodectl1 & GEN6_RP_MEDIA_MODE_MASK) ==
			  GEN6_RP_MEDIA_SW_MODE));
	seq_printf(m, "RC6 Enabled: %s\n",
		   yesno(rcctl1 & (GEN7_RC_CTL_TO_MODE |
					GEN6_RC_CTL_EI_MODE(1))));
	seq_printf(m, "Render Power Well: %s\n",
		   (pw_status & VLV_GTLC_PW_RENDER_STATUS_MASK) ? "Up" : "Down");
	seq_printf(m, "Media Power Well: %s\n",
		   (pw_status & VLV_GTLC_PW_MEDIA_STATUS_MASK) ? "Up" : "Down");

	seq_printf(m, "Render RC6 residency since boot: %u\n",
		   I915_READ(VLV_GT_RENDER_RC6));
	seq_printf(m, "Media RC6 residency since boot: %u\n",
		   I915_READ(VLV_GT_MEDIA_RC6));

	return i915_forcewake_domains(m, NULL);
}

static int gen6_drpc_info(struct seq_file *m)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct drm_device *dev = &dev_priv->drm;
	u32 rpmodectl1, gt_core_status, rcctl1, rc6vids = 0;
	u32 gen9_powergate_enable = 0, gen9_powergate_status = 0;
	unsigned forcewake_count;
	int count = 0, ret;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;
	intel_runtime_pm_get(dev_priv);

	spin_lock_irq(&dev_priv->uncore.lock);
	forcewake_count = dev_priv->uncore.fw_domain[FW_DOMAIN_ID_RENDER].wake_count;
	spin_unlock_irq(&dev_priv->uncore.lock);

	if (forcewake_count) {
		seq_puts(m, "RC information inaccurate because somebody "
			    "holds a forcewake reference \n");
	} else {
		/* NB: we cannot use forcewake, else we read the wrong values */
		while (count++ < 50 && (I915_READ_NOTRACE(FORCEWAKE_ACK) & 1))
			udelay(10);
		seq_printf(m, "RC information accurate: %s\n", yesno(count < 51));
	}

	gt_core_status = I915_READ_FW(GEN6_GT_CORE_STATUS);
	trace_i915_reg_rw(false, GEN6_GT_CORE_STATUS, gt_core_status, 4, true);

	rpmodectl1 = I915_READ(GEN6_RP_CONTROL);
	rcctl1 = I915_READ(GEN6_RC_CONTROL);
	if (INTEL_GEN(dev_priv) >= 9) {
		gen9_powergate_enable = I915_READ(GEN9_PG_ENABLE);
		gen9_powergate_status = I915_READ(GEN9_PWRGT_DOMAIN_STATUS);
	}
	mutex_unlock(&dev->struct_mutex);
	mutex_lock(&dev_priv->rps.hw_lock);
	sandybridge_pcode_read(dev_priv, GEN6_PCODE_READ_RC6VIDS, &rc6vids);
	mutex_unlock(&dev_priv->rps.hw_lock);

	intel_runtime_pm_put(dev_priv);

	seq_printf(m, "Video Turbo Mode: %s\n",
		   yesno(rpmodectl1 & GEN6_RP_MEDIA_TURBO));
	seq_printf(m, "HW control enabled: %s\n",
		   yesno(rpmodectl1 & GEN6_RP_ENABLE));
	seq_printf(m, "SW control enabled: %s\n",
		   yesno((rpmodectl1 & GEN6_RP_MEDIA_MODE_MASK) ==
			  GEN6_RP_MEDIA_SW_MODE));
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
	seq_printf(m, "RC6 \"Locked to RPn\" residency since boot: %u\n",
		   I915_READ(GEN6_GT_GFX_RC6_LOCKED));
	seq_printf(m, "RC6 residency since boot: %u\n",
		   I915_READ(GEN6_GT_GFX_RC6));
	seq_printf(m, "RC6+ residency since boot: %u\n",
		   I915_READ(GEN6_GT_GFX_RC6p));
	seq_printf(m, "RC6++ residency since boot: %u\n",
		   I915_READ(GEN6_GT_GFX_RC6pp));

	seq_printf(m, "RC6   voltage: %dmV\n",
		   GEN6_DECODE_RC6_VID(((rc6vids >> 0) & 0xff)));
	seq_printf(m, "RC6+  voltage: %dmV\n",
		   GEN6_DECODE_RC6_VID(((rc6vids >> 8) & 0xff)));
	seq_printf(m, "RC6++ voltage: %dmV\n",
		   GEN6_DECODE_RC6_VID(((rc6vids >> 16) & 0xff)));
	return i915_forcewake_domains(m, NULL);
}

static int i915_drpc_info(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);

	if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		return vlv_drpc_info(m);
	else if (INTEL_GEN(dev_priv) >= 6)
		return gen6_drpc_info(m);
	else
		return ironlake_drpc_info(m);
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

	if (!HAS_FBC(dev_priv)) {
		seq_puts(m, "FBC unsupported on this chipset\n");
		return 0;
	}

	intel_runtime_pm_get(dev_priv);
	mutex_lock(&dev_priv->fbc.lock);

	if (intel_fbc_is_active(dev_priv))
		seq_puts(m, "FBC enabled\n");
	else
		seq_printf(m, "FBC disabled: %s\n",
			   dev_priv->fbc.no_fbc_reason);

	if (intel_fbc_is_active(dev_priv) &&
	    INTEL_GEN(dev_priv) >= 7)
		seq_printf(m, "Compressing: %s\n",
			   yesno(I915_READ(FBC_STATUS2) &
				 FBC_COMPRESSION_MASK));

	mutex_unlock(&dev_priv->fbc.lock);
	intel_runtime_pm_put(dev_priv);

	return 0;
}

static int i915_fbc_fc_get(void *data, u64 *val)
{
	struct drm_i915_private *dev_priv = data;

	if (INTEL_GEN(dev_priv) < 7 || !HAS_FBC(dev_priv))
		return -ENODEV;

	*val = dev_priv->fbc.false_color;

	return 0;
}

static int i915_fbc_fc_set(void *data, u64 val)
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

DEFINE_SIMPLE_ATTRIBUTE(i915_fbc_fc_fops,
			i915_fbc_fc_get, i915_fbc_fc_set,
			"%llu\n");

static int i915_ips_status(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);

	if (!HAS_IPS(dev_priv)) {
		seq_puts(m, "not supported\n");
		return 0;
	}

	intel_runtime_pm_get(dev_priv);

	seq_printf(m, "Enabled by kernel parameter: %s\n",
		   yesno(i915.enable_ips));

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

	if (HAS_PCH_SPLIT(dev_priv))
		sr_enabled = I915_READ(WM1_LP_ILK) & WM1_LP_SR_EN;
	else if (IS_CRESTLINE(dev_priv) || IS_G4X(dev_priv) ||
		 IS_I945G(dev_priv) || IS_I945GM(dev_priv))
		sr_enabled = I915_READ(FW_BLC_SELF) & FW_BLC_SELF_EN;
	else if (IS_I915GM(dev_priv))
		sr_enabled = I915_READ(INSTPM) & INSTPM_SELF_EN;
	else if (IS_PINEVIEW(dev_priv))
		sr_enabled = I915_READ(DSPFW3) & PINEVIEW_SELF_REFRESH_EN;
	else if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		sr_enabled = I915_READ(FW_BLC_SELF_VLV) & FW_CSPWRDWNEN;

	intel_runtime_pm_put(dev_priv);

	seq_printf(m, "self-refresh: %s\n",
		   sr_enabled ? "enabled" : "disabled");

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
	int ret = 0;
	int gpu_freq, ia_freq;
	unsigned int max_gpu_freq, min_gpu_freq;

	if (!HAS_LLC(dev_priv)) {
		seq_puts(m, "unsupported on this chipset\n");
		return 0;
	}

	intel_runtime_pm_get(dev_priv);

	ret = mutex_lock_interruptible(&dev_priv->rps.hw_lock);
	if (ret)
		goto out;

	if (IS_SKYLAKE(dev_priv) || IS_KABYLAKE(dev_priv)) {
		/* Convert GT frequency to 50 HZ units */
		min_gpu_freq =
			dev_priv->rps.min_freq_softlimit / GEN9_FREQ_SCALER;
		max_gpu_freq =
			dev_priv->rps.max_freq_softlimit / GEN9_FREQ_SCALER;
	} else {
		min_gpu_freq = dev_priv->rps.min_freq_softlimit;
		max_gpu_freq = dev_priv->rps.max_freq_softlimit;
	}

	seq_puts(m, "GPU freq (MHz)\tEffective CPU freq (MHz)\tEffective Ring freq (MHz)\n");

	for (gpu_freq = min_gpu_freq; gpu_freq <= max_gpu_freq; gpu_freq++) {
		ia_freq = gpu_freq;
		sandybridge_pcode_read(dev_priv,
				       GEN6_PCODE_READ_MIN_FREQ_TABLE,
				       &ia_freq);
		seq_printf(m, "%d\t\t%d\t\t\t\t%d\n",
			   intel_gpu_freq(dev_priv, (gpu_freq *
				(IS_SKYLAKE(dev_priv) || IS_KABYLAKE(dev_priv) ?
				 GEN9_FREQ_SCALER : 1))),
			   ((ia_freq >> 0) & 0xff) * 100,
			   ((ia_freq >> 8) & 0xff) * 100);
	}

	mutex_unlock(&dev_priv->rps.hw_lock);

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
	if (dev_priv->fbdev) {
		fbdev_fb = to_intel_framebuffer(dev_priv->fbdev->helper.fb);

		seq_printf(m, "fbcon size: %d x %d, depth %d, %d bpp, modifier 0x%llx, refcount %d, obj ",
			   fbdev_fb->base.width,
			   fbdev_fb->base.height,
			   fbdev_fb->base.depth,
			   fbdev_fb->base.bits_per_pixel,
			   fbdev_fb->base.modifier[0],
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
			   fb->base.depth,
			   fb->base.bits_per_pixel,
			   fb->base.modifier[0],
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
	seq_printf(m, " (ringbuffer, space: %d, head: %u, tail: %u, last head: %d)",
		   ring->space, ring->head, ring->tail,
		   ring->last_retired_head);
}

static int i915_context_status(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct drm_device *dev = &dev_priv->drm;
	struct intel_engine_cs *engine;
	struct i915_gem_context *ctx;
	int ret;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	list_for_each_entry(ctx, &dev_priv->context_list, link) {
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

		for_each_engine(engine, dev_priv) {
			struct intel_context *ce = &ctx->engine[engine->id];

			seq_printf(m, "%s: ", engine->name);
			seq_putc(m, ce->initialised ? 'I' : 'i');
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

static void i915_dump_lrc_obj(struct seq_file *m,
			      struct i915_gem_context *ctx,
			      struct intel_engine_cs *engine)
{
	struct i915_vma *vma = ctx->engine[engine->id].state;
	struct page *page;
	int j;

	seq_printf(m, "CONTEXT: %s %u\n", engine->name, ctx->hw_id);

	if (!vma) {
		seq_puts(m, "\tFake context\n");
		return;
	}

	if (vma->flags & I915_VMA_GLOBAL_BIND)
		seq_printf(m, "\tBound in GGTT at 0x%08x\n",
			   i915_ggtt_offset(vma));

	if (i915_gem_object_get_pages(vma->obj)) {
		seq_puts(m, "\tFailed to get pages for context object\n\n");
		return;
	}

	page = i915_gem_object_get_page(vma->obj, LRC_STATE_PN);
	if (page) {
		u32 *reg_state = kmap_atomic(page);

		for (j = 0; j < 0x600 / sizeof(u32) / 4; j += 4) {
			seq_printf(m,
				   "\t[0x%04x] 0x%08x 0x%08x 0x%08x 0x%08x\n",
				   j * 4,
				   reg_state[j], reg_state[j + 1],
				   reg_state[j + 2], reg_state[j + 3]);
		}
		kunmap_atomic(reg_state);
	}

	seq_putc(m, '\n');
}

static int i915_dump_lrc(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct drm_device *dev = &dev_priv->drm;
	struct intel_engine_cs *engine;
	struct i915_gem_context *ctx;
	int ret;

	if (!i915.enable_execlists) {
		seq_printf(m, "Logical Ring Contexts are disabled\n");
		return 0;
	}

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	list_for_each_entry(ctx, &dev_priv->context_list, link)
		for_each_engine(engine, dev_priv)
			i915_dump_lrc_obj(m, ctx, engine);

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
	struct drm_device *dev = &dev_priv->drm;
	int ret;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;
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
	mutex_unlock(&dev->struct_mutex);

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
	struct intel_engine_cs *engine;
	struct i915_hw_ppgtt *ppgtt = dev_priv->mm.aliasing_ppgtt;
	int i;

	if (!ppgtt)
		return;

	for_each_engine(engine, dev_priv) {
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

	if (IS_GEN6(dev_priv))
		seq_printf(m, "GFX_MODE: 0x%08x\n", I915_READ(GFX_MODE));

	for_each_engine(engine, dev_priv) {
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
	int count = 0;

	for_each_engine(engine, i915)
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
	struct drm_file *file;

	seq_printf(m, "RPS enabled? %d\n", dev_priv->rps.enabled);
	seq_printf(m, "GPU busy? %s [%x]\n",
		   yesno(dev_priv->gt.awake), dev_priv->gt.active_engines);
	seq_printf(m, "CPU waiting? %d\n", count_irq_waiters(dev_priv));
	seq_printf(m, "Frequency requested %d\n",
		   intel_gpu_freq(dev_priv, dev_priv->rps.cur_freq));
	seq_printf(m, "  min hard:%d, soft:%d; max soft:%d, hard:%d\n",
		   intel_gpu_freq(dev_priv, dev_priv->rps.min_freq),
		   intel_gpu_freq(dev_priv, dev_priv->rps.min_freq_softlimit),
		   intel_gpu_freq(dev_priv, dev_priv->rps.max_freq_softlimit),
		   intel_gpu_freq(dev_priv, dev_priv->rps.max_freq));
	seq_printf(m, "  idle:%d, efficient:%d, boost:%d\n",
		   intel_gpu_freq(dev_priv, dev_priv->rps.idle_freq),
		   intel_gpu_freq(dev_priv, dev_priv->rps.efficient_freq),
		   intel_gpu_freq(dev_priv, dev_priv->rps.boost_freq));

	mutex_lock(&dev->filelist_mutex);
	spin_lock(&dev_priv->rps.client_lock);
	list_for_each_entry_reverse(file, &dev->filelist, lhead) {
		struct drm_i915_file_private *file_priv = file->driver_priv;
		struct task_struct *task;

		rcu_read_lock();
		task = pid_task(file->pid, PIDTYPE_PID);
		seq_printf(m, "%s [%d]: %d boosts%s\n",
			   task ? task->comm : "<unknown>",
			   task ? task->pid : -1,
			   file_priv->rps.boosts,
			   list_empty(&file_priv->rps.link) ? "" : ", active");
		rcu_read_unlock();
	}
	seq_printf(m, "Kernel (anonymous) boosts: %d\n", dev_priv->rps.boosts);
	spin_unlock(&dev_priv->rps.client_lock);
	mutex_unlock(&dev->filelist_mutex);

	if (INTEL_GEN(dev_priv) >= 6 &&
	    dev_priv->rps.enabled &&
	    dev_priv->gt.active_engines) {
		u32 rpup, rpupei;
		u32 rpdown, rpdownei;

		intel_uncore_forcewake_get(dev_priv, FORCEWAKE_ALL);
		rpup = I915_READ_FW(GEN6_RP_CUR_UP) & GEN6_RP_EI_MASK;
		rpupei = I915_READ_FW(GEN6_RP_CUR_UP_EI) & GEN6_RP_EI_MASK;
		rpdown = I915_READ_FW(GEN6_RP_CUR_DOWN) & GEN6_RP_EI_MASK;
		rpdownei = I915_READ_FW(GEN6_RP_CUR_DOWN_EI) & GEN6_RP_EI_MASK;
		intel_uncore_forcewake_put(dev_priv, FORCEWAKE_ALL);

		seq_printf(m, "\nRPS Autotuning (current \"%s\" window):\n",
			   rps_power_to_str(dev_priv->rps.power));
		seq_printf(m, "  Avg. up: %d%% [above threshold? %d%%]\n",
			   100 * rpup / rpupei,
			   dev_priv->rps.up_threshold);
		seq_printf(m, "  Avg. down: %d%% [below threshold? %d%%]\n",
			   100 * rpdown / rpdownei,
			   dev_priv->rps.down_threshold);
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

static int i915_guc_load_status_info(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct intel_guc_fw *guc_fw = &dev_priv->guc.guc_fw;
	u32 tmp, i;

	if (!HAS_GUC_UCODE(dev_priv))
		return 0;

	seq_printf(m, "GuC firmware status:\n");
	seq_printf(m, "\tpath: %s\n",
		guc_fw->guc_fw_path);
	seq_printf(m, "\tfetch: %s\n",
		intel_guc_fw_status_repr(guc_fw->guc_fw_fetch_status));
	seq_printf(m, "\tload: %s\n",
		intel_guc_fw_status_repr(guc_fw->guc_fw_load_status));
	seq_printf(m, "\tversion wanted: %d.%d\n",
		guc_fw->guc_fw_major_wanted, guc_fw->guc_fw_minor_wanted);
	seq_printf(m, "\tversion found: %d.%d\n",
		guc_fw->guc_fw_major_found, guc_fw->guc_fw_minor_found);
	seq_printf(m, "\theader: offset is %d; size = %d\n",
		guc_fw->header_offset, guc_fw->header_size);
	seq_printf(m, "\tuCode: offset is %d; size = %d\n",
		guc_fw->ucode_offset, guc_fw->ucode_size);
	seq_printf(m, "\tRSA: offset is %d; size = %d\n",
		guc_fw->rsa_offset, guc_fw->rsa_size);

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

	return 0;
}

static void i915_guc_client_info(struct seq_file *m,
				 struct drm_i915_private *dev_priv,
				 struct i915_guc_client *client)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	uint64_t tot = 0;

	seq_printf(m, "\tPriority %d, GuC ctx index: %u, PD offset 0x%x\n",
		client->priority, client->ctx_index, client->proc_desc_offset);
	seq_printf(m, "\tDoorbell id %d, offset: 0x%x, cookie 0x%x\n",
		client->doorbell_id, client->doorbell_offset, client->cookie);
	seq_printf(m, "\tWQ size %d, offset: 0x%x, tail %d\n",
		client->wq_size, client->wq_offset, client->wq_tail);

	seq_printf(m, "\tWork queue full: %u\n", client->no_wq_space);
	seq_printf(m, "\tFailed doorbell: %u\n", client->b_fail);
	seq_printf(m, "\tLast submission result: %d\n", client->retcode);

	for_each_engine_id(engine, dev_priv, id) {
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
	struct drm_device *dev = &dev_priv->drm;
	struct intel_guc guc;
	struct i915_guc_client client = {};
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	u64 total = 0;

	if (!HAS_GUC_SCHED(dev_priv))
		return 0;

	if (mutex_lock_interruptible(&dev->struct_mutex))
		return 0;

	/* Take a local copy of the GuC data, so we can dump it at leisure */
	guc = dev_priv->guc;
	if (guc.execbuf_client)
		client = *guc.execbuf_client;

	mutex_unlock(&dev->struct_mutex);

	seq_printf(m, "Doorbell map:\n");
	seq_printf(m, "\t%*pb\n", GUC_MAX_DOORBELLS, guc.doorbell_bitmap);
	seq_printf(m, "Doorbell next cacheline: 0x%x\n\n", guc.db_cacheline);

	seq_printf(m, "GuC total action count: %llu\n", guc.action_count);
	seq_printf(m, "GuC action failure count: %u\n", guc.action_fail);
	seq_printf(m, "GuC last action command: 0x%x\n", guc.action_cmd);
	seq_printf(m, "GuC last action status: 0x%x\n", guc.action_status);
	seq_printf(m, "GuC last action error code: %d\n", guc.action_err);

	seq_printf(m, "\nGuC submissions:\n");
	for_each_engine_id(engine, dev_priv, id) {
		u64 submissions = guc.submissions[id];
		total += submissions;
		seq_printf(m, "\t%-24s: %10llu, last seqno 0x%08x\n",
			engine->name, submissions, guc.last_seqno[id]);
	}
	seq_printf(m, "\t%s: %llu\n", "Total", total);

	seq_printf(m, "\nGuC execbuf client @ %p:\n", guc.execbuf_client);
	i915_guc_client_info(m, dev_priv, &client);

	/* Add more as required ... */

	return 0;
}

static int i915_guc_log_dump(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct drm_i915_gem_object *obj;
	int i = 0, pg;

	if (!dev_priv->guc.log_vma)
		return 0;

	obj = dev_priv->guc.log_vma->obj;
	for (pg = 0; pg < obj->base.size / PAGE_SIZE; pg++) {
		u32 *log = kmap_atomic(i915_gem_object_get_page(obj, pg));

		for (i = 0; i < PAGE_SIZE / sizeof(u32); i += 4)
			seq_printf(m, "0x%08x 0x%08x 0x%08x 0x%08x\n",
				   *(log + i), *(log + i + 1),
				   *(log + i + 2), *(log + i + 3));

		kunmap_atomic(log);
	}

	seq_putc(m, '\n');

	return 0;
}

static int i915_edp_psr_status(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	u32 psrperf = 0;
	u32 stat[3];
	enum pipe pipe;
	bool enabled = false;

	if (!HAS_PSR(dev_priv)) {
		seq_puts(m, "PSR not supported\n");
		return 0;
	}

	intel_runtime_pm_get(dev_priv);

	mutex_lock(&dev_priv->psr.lock);
	seq_printf(m, "Sink_Support: %s\n", yesno(dev_priv->psr.sink_support));
	seq_printf(m, "Source_OK: %s\n", yesno(dev_priv->psr.source_ok));
	seq_printf(m, "Enabled: %s\n", yesno((bool)dev_priv->psr.enabled));
	seq_printf(m, "Active: %s\n", yesno(dev_priv->psr.active));
	seq_printf(m, "Busy frontbuffer bits: 0x%03x\n",
		   dev_priv->psr.busy_frontbuffer_bits);
	seq_printf(m, "Re-enable work scheduled: %s\n",
		   yesno(work_busy(&dev_priv->psr.work.work)));

	if (HAS_DDI(dev_priv))
		enabled = I915_READ(EDP_PSR_CTL) & EDP_PSR_ENABLE;
	else {
		for_each_pipe(dev_priv, pipe) {
			stat[pipe] = I915_READ(VLV_PSRSTAT(pipe)) &
				VLV_EDP_PSR_CURR_STATE_MASK;
			if ((stat[pipe] == VLV_EDP_PSR_ACTIVE_NORFB_UP) ||
			    (stat[pipe] == VLV_EDP_PSR_ACTIVE_SF_UPDATE))
				enabled = true;
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
	mutex_unlock(&dev_priv->psr.lock);

	intel_runtime_pm_put(dev_priv);
	return 0;
}

static int i915_sink_crc(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct drm_device *dev = &dev_priv->drm;
	struct intel_connector *connector;
	struct intel_dp *intel_dp = NULL;
	int ret;
	u8 crc[6];

	drm_modeset_lock_all(dev);
	for_each_intel_connector(dev, connector) {
		struct drm_crtc *crtc;

		if (!connector->base.state->best_encoder)
			continue;

		crtc = connector->base.state->crtc;
		if (!crtc->state->active)
			continue;

		if (connector->base.connector_type != DRM_MODE_CONNECTOR_eDP)
			continue;

		intel_dp = enc_to_intel_dp(connector->base.state->best_encoder);

		ret = intel_dp_sink_crc(intel_dp, crc);
		if (ret)
			goto out;

		seq_printf(m, "%02x%02x%02x%02x%02x%02x\n",
			   crc[0], crc[1], crc[2],
			   crc[3], crc[4], crc[5]);
		goto out;
	}
	ret = -ENODEV;
out:
	drm_modeset_unlock_all(dev);
	return ret;
}

static int i915_energy_uJ(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	u64 power;
	u32 units;

	if (INTEL_GEN(dev_priv) < 6)
		return -ENODEV;

	intel_runtime_pm_get(dev_priv);

	rdmsrl(MSR_RAPL_POWER_UNIT, power);
	power = (power & 0x1f00) >> 8;
	units = 1000000 / (1 << power); /* convert to uJ */
	power = I915_READ(MCH_SECP_NRG_STTS);
	power *= units;

	intel_runtime_pm_put(dev_priv);

	seq_printf(m, "%llu", (long long unsigned)power);

	return 0;
}

static int i915_runtime_pm_status(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct pci_dev *pdev = dev_priv->drm.pdev;

	if (!HAS_RUNTIME_PM(dev_priv))
		seq_puts(m, "Runtime power management not supported\n");

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

		for (power_domain = 0; power_domain < POWER_DOMAIN_NUM;
		     power_domain++) {
			if (!(BIT(power_domain) & power_well->domains))
				continue;

			seq_printf(m, "  %-23s %d\n",
				 intel_display_power_domain_str(power_domain),
				 power_domains->domain_use_count[power_domain]);
		}
	}

	mutex_unlock(&power_domains->lock);

	return 0;
}

static int i915_dmc_info(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct intel_csr *csr;

	if (!HAS_CSR(dev_priv)) {
		seq_puts(m, "not supported\n");
		return 0;
	}

	csr = &dev_priv->csr;

	intel_runtime_pm_get(dev_priv);

	seq_printf(m, "fw loaded: %s\n", yesno(csr->dmc_payload != NULL));
	seq_printf(m, "path: %s\n", csr->fw_path);

	if (!csr->dmc_payload)
		goto out;

	seq_printf(m, "version: %d.%d\n", CSR_VERSION_MAJOR(csr->version),
		   CSR_VERSION_MINOR(csr->version));

	if (IS_SKYLAKE(dev_priv) && csr->version >= CSR_VERSION(1, 6)) {
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

	if (!intel_encoder || intel_encoder->type == INTEL_OUTPUT_DP_MST)
		return;

	switch (connector->connector_type) {
	case DRM_MODE_CONNECTOR_DisplayPort:
	case DRM_MODE_CONNECTOR_eDP:
		intel_dp_info(m, intel_connector);
		break;
	case DRM_MODE_CONNECTOR_LVDS:
		if (intel_encoder->type == INTEL_OUTPUT_LVDS)
			intel_lvds_info(m, intel_connector);
		break;
	case DRM_MODE_CONNECTOR_HDMIA:
		if (intel_encoder->type == INTEL_OUTPUT_HDMI ||
		    intel_encoder->type == INTEL_OUTPUT_UNKNOWN)
			intel_hdmi_info(m, intel_connector);
		break;
	default:
		break;
	}

	seq_printf(m, "\tmodes:\n");
	list_for_each_entry(mode, &connector->modes, head)
		intel_seq_print_mode(m, 2, mode);
}

static bool cursor_active(struct drm_i915_private *dev_priv, int pipe)
{
	u32 state;

	if (IS_845G(dev_priv) || IS_I865G(dev_priv))
		state = I915_READ(CURCNTR(PIPE_A)) & CURSOR_ENABLE;
	else
		state = I915_READ(CURCNTR(pipe)) & CURSOR_MODE;

	return state;
}

static bool cursor_position(struct drm_i915_private *dev_priv,
			    int pipe, int *x, int *y)
{
	u32 pos;

	pos = I915_READ(CURPOS(pipe));

	*x = (pos >> CURSOR_X_SHIFT) & CURSOR_POS_MASK;
	if (pos & (CURSOR_POS_SIGN << CURSOR_X_SHIFT))
		*x = -*x;

	*y = (pos >> CURSOR_Y_SHIFT) & CURSOR_POS_MASK;
	if (pos & (CURSOR_POS_SIGN << CURSOR_Y_SHIFT))
		*y = -*y;

	return cursor_active(dev_priv, pipe);
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
	 * According to doc only one DRM_ROTATE_ is allowed but this
	 * will print them all to visualize if the values are misused
	 */
	snprintf(buf, sizeof(buf),
		 "%s%s%s%s%s%s(0x%08x)",
		 (rotation & DRM_ROTATE_0) ? "0 " : "",
		 (rotation & DRM_ROTATE_90) ? "90 " : "",
		 (rotation & DRM_ROTATE_180) ? "180 " : "",
		 (rotation & DRM_ROTATE_270) ? "270 " : "",
		 (rotation & DRM_REFLECT_X) ? "FLIPX " : "",
		 (rotation & DRM_REFLECT_Y) ? "FLIPY " : "",
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
		char *format_name;

		if (!plane->state) {
			seq_puts(m, "plane->state is NULL!\n");
			continue;
		}

		state = plane->state;

		if (state->fb) {
			format_name = drm_get_format_name(state->fb->pixel_format);
		} else {
			format_name = kstrdup("N/A", GFP_KERNEL);
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
			   format_name,
			   plane_rotation(state->rotation));

		kfree(format_name);
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

		for (i = 0; i < SKL_NUM_SCALERS; i++) {
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

	intel_runtime_pm_get(dev_priv);
	drm_modeset_lock_all(dev);
	seq_printf(m, "CRTC info\n");
	seq_printf(m, "---------\n");
	for_each_intel_crtc(dev, crtc) {
		bool active;
		struct intel_crtc_state *pipe_config;
		int x, y;

		pipe_config = to_intel_crtc_state(crtc->base.state);

		seq_printf(m, "CRTC %d: pipe: %c, active=%s, (size=%dx%d), dither=%s, bpp=%d\n",
			   crtc->base.base.id, pipe_name(crtc->pipe),
			   yesno(pipe_config->base.active),
			   pipe_config->pipe_src_w, pipe_config->pipe_src_h,
			   yesno(pipe_config->dither), pipe_config->pipe_bpp);

		if (pipe_config->base.active) {
			intel_crtc_info(m, crtc);

			active = cursor_position(dev_priv, crtc->pipe, &x, &y);
			seq_printf(m, "\tcursor visible? %s, position (%d, %d), size %dx%d, addr 0x%08x, active? %s\n",
				   yesno(crtc->cursor_base),
				   x, y, crtc->base.cursor->state->crtc_w,
				   crtc->base.cursor->state->crtc_h,
				   crtc->cursor_addr, yesno(active));
			intel_scaler_info(m, crtc);
			intel_plane_info(m, crtc);
		}

		seq_printf(m, "\tunderrun reporting: cpu=%s pch=%s \n",
			   yesno(!crtc->cpu_fifo_underrun_disabled),
			   yesno(!crtc->pch_fifo_underrun_disabled));
	}

	seq_printf(m, "\n");
	seq_printf(m, "Connector info\n");
	seq_printf(m, "--------------\n");
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		intel_connector_info(m, connector);
	}
	drm_modeset_unlock_all(dev);
	intel_runtime_pm_put(dev_priv);

	return 0;
}

static int i915_engine_info(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct intel_engine_cs *engine;

	for_each_engine(engine, dev_priv) {
		struct intel_breadcrumbs *b = &engine->breadcrumbs;
		struct drm_i915_gem_request *rq;
		struct rb_node *rb;
		u64 addr;

		seq_printf(m, "%s\n", engine->name);
		seq_printf(m, "\tcurrent seqno %x, last %x, hangcheck %x [score %d]\n",
			   intel_engine_get_seqno(engine),
			   engine->last_submitted_seqno,
			   engine->hangcheck.seqno,
			   engine->hangcheck.score);

		rcu_read_lock();

		seq_printf(m, "\tRequests:\n");

		rq = list_first_entry(&engine->request_list,
				struct drm_i915_gem_request, link);
		if (&rq->link != &engine->request_list)
			print_request(m, rq, "\t\tfirst  ");

		rq = list_last_entry(&engine->request_list,
				struct drm_i915_gem_request, link);
		if (&rq->link != &engine->request_list)
			print_request(m, rq, "\t\tlast   ");

		rq = i915_gem_find_active_request(engine);
		if (rq) {
			print_request(m, rq, "\t\tactive ");
			seq_printf(m,
				   "\t\t[head %04x, postfix %04x, tail %04x, batch 0x%08x_%08x]\n",
				   rq->head, rq->postfix, rq->tail,
				   rq->batch ? upper_32_bits(rq->batch->node.start) : ~0u,
				   rq->batch ? lower_32_bits(rq->batch->node.start) : ~0u);
		}

		seq_printf(m, "\tRING_START: 0x%08x [0x%08x]\n",
			   I915_READ(RING_START(engine->mmio_base)),
			   rq ? i915_ggtt_offset(rq->ring->vma) : 0);
		seq_printf(m, "\tRING_HEAD:  0x%08x [0x%08x]\n",
			   I915_READ(RING_HEAD(engine->mmio_base)) & HEAD_ADDR,
			   rq ? rq->ring->head : 0);
		seq_printf(m, "\tRING_TAIL:  0x%08x [0x%08x]\n",
			   I915_READ(RING_TAIL(engine->mmio_base)) & TAIL_ADDR,
			   rq ? rq->ring->tail : 0);
		seq_printf(m, "\tRING_CTL:   0x%08x [%s]\n",
			   I915_READ(RING_CTL(engine->mmio_base)),
			   I915_READ(RING_CTL(engine->mmio_base)) & (RING_WAIT | RING_WAIT_SEMAPHORE) ? "waiting" : "");

		rcu_read_unlock();

		addr = intel_engine_get_active_head(engine);
		seq_printf(m, "\tACTHD:  0x%08x_%08x\n",
			   upper_32_bits(addr), lower_32_bits(addr));
		addr = intel_engine_get_last_batch_head(engine);
		seq_printf(m, "\tBBADDR: 0x%08x_%08x\n",
			   upper_32_bits(addr), lower_32_bits(addr));

		if (i915.enable_execlists) {
			u32 ptr, read, write;

			seq_printf(m, "\tExeclist status: 0x%08x %08x\n",
				   I915_READ(RING_EXECLIST_STATUS_LO(engine)),
				   I915_READ(RING_EXECLIST_STATUS_HI(engine)));

			ptr = I915_READ(RING_CONTEXT_STATUS_PTR(engine));
			read = GEN8_CSB_READ_PTR(ptr);
			write = GEN8_CSB_WRITE_PTR(ptr);
			seq_printf(m, "\tExeclist CSB read %d, write %d\n",
				   read, write);
			if (read >= GEN8_CSB_ENTRIES)
				read = 0;
			if (write >= GEN8_CSB_ENTRIES)
				write = 0;
			if (read > write)
				write += GEN8_CSB_ENTRIES;
			while (read < write) {
				unsigned int idx = ++read % GEN8_CSB_ENTRIES;

				seq_printf(m, "\tExeclist CSB[%d]: 0x%08x, context: %d\n",
					   idx,
					   I915_READ(RING_CONTEXT_STATUS_BUF_LO(engine, idx)),
					   I915_READ(RING_CONTEXT_STATUS_BUF_HI(engine, idx)));
			}

			rcu_read_lock();
			rq = READ_ONCE(engine->execlist_port[0].request);
			if (rq)
				print_request(m, rq, "\t\tELSP[0] ");
			else
				seq_printf(m, "\t\tELSP[0] idle\n");
			rq = READ_ONCE(engine->execlist_port[1].request);
			if (rq)
				print_request(m, rq, "\t\tELSP[1] ");
			else
				seq_printf(m, "\t\tELSP[1] idle\n");
			rcu_read_unlock();
		} else if (INTEL_GEN(dev_priv) > 6) {
			seq_printf(m, "\tPP_DIR_BASE: 0x%08x\n",
				   I915_READ(RING_PP_DIR_BASE(engine)));
			seq_printf(m, "\tPP_DIR_BASE_READ: 0x%08x\n",
				   I915_READ(RING_PP_DIR_BASE_READ(engine)));
			seq_printf(m, "\tPP_DIR_DCLV: 0x%08x\n",
				   I915_READ(RING_PP_DIR_DCLV(engine)));
		}

		spin_lock(&b->lock);
		for (rb = rb_first(&b->waiters); rb; rb = rb_next(rb)) {
			struct intel_wait *w = container_of(rb, typeof(*w), node);

			seq_printf(m, "\t%s [%d] waiting for %x\n",
				   w->tsk->comm, w->tsk->pid, w->seqno);
		}
		spin_unlock(&b->lock);

		seq_puts(m, "\n");
	}

	return 0;
}

static int i915_semaphore_status(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct drm_device *dev = &dev_priv->drm;
	struct intel_engine_cs *engine;
	int num_rings = INTEL_INFO(dev_priv)->num_rings;
	enum intel_engine_id id;
	int j, ret;

	if (!i915.semaphores) {
		seq_puts(m, "Semaphores are disabled\n");
		return 0;
	}

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;
	intel_runtime_pm_get(dev_priv);

	if (IS_BROADWELL(dev_priv)) {
		struct page *page;
		uint64_t *seqno;

		page = i915_gem_object_get_page(dev_priv->semaphore->obj, 0);

		seqno = (uint64_t *)kmap_atomic(page);
		for_each_engine_id(engine, dev_priv, id) {
			uint64_t offset;

			seq_printf(m, "%s\n", engine->name);

			seq_puts(m, "  Last signal:");
			for (j = 0; j < num_rings; j++) {
				offset = id * I915_NUM_ENGINES + j;
				seq_printf(m, "0x%08llx (0x%02llx) ",
					   seqno[offset], offset * 8);
			}
			seq_putc(m, '\n');

			seq_puts(m, "  Last wait:  ");
			for (j = 0; j < num_rings; j++) {
				offset = id + (j * I915_NUM_ENGINES);
				seq_printf(m, "0x%08llx (0x%02llx) ",
					   seqno[offset], offset * 8);
			}
			seq_putc(m, '\n');

		}
		kunmap_atomic(seqno);
	} else {
		seq_puts(m, "  Last signal:");
		for_each_engine(engine, dev_priv)
			for (j = 0; j < num_rings; j++)
				seq_printf(m, "0x%08x\n",
					   I915_READ(engine->semaphore.mbox.signal[j]));
		seq_putc(m, '\n');
	}

	seq_puts(m, "\nSync seqno:\n");
	for_each_engine(engine, dev_priv) {
		for (j = 0; j < num_rings; j++)
			seq_printf(m, "  0x%08x ",
				   engine->semaphore.sync_seqno[j]);
		seq_putc(m, '\n');
	}
	seq_putc(m, '\n');

	intel_runtime_pm_put(dev_priv);
	mutex_unlock(&dev->struct_mutex);
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

		seq_printf(m, "DPLL%i: %s, id: %i\n", i, pll->name, pll->id);
		seq_printf(m, " crtc_mask: 0x%08x, active: 0x%x, on: %s\n",
			   pll->config.crtc_mask, pll->active_mask, yesno(pll->on));
		seq_printf(m, " tracked hardware state:\n");
		seq_printf(m, " dpll:    0x%08x\n", pll->config.hw_state.dpll);
		seq_printf(m, " dpll_md: 0x%08x\n",
			   pll->config.hw_state.dpll_md);
		seq_printf(m, " fp0:     0x%08x\n", pll->config.hw_state.fp0);
		seq_printf(m, " fp1:     0x%08x\n", pll->config.hw_state.fp1);
		seq_printf(m, " wrpll:   0x%08x\n", pll->config.hw_state.wrpll);
	}
	drm_modeset_unlock_all(dev);

	return 0;
}

static int i915_wa_registers(struct seq_file *m, void *unused)
{
	int i;
	int ret;
	struct intel_engine_cs *engine;
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct drm_device *dev = &dev_priv->drm;
	struct i915_workarounds *workarounds = &dev_priv->workarounds;
	enum intel_engine_id id;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	intel_runtime_pm_get(dev_priv);

	seq_printf(m, "Workarounds applied: %d\n", workarounds->count);
	for_each_engine_id(engine, dev_priv, id)
		seq_printf(m, "HW whitelist count for %s: %d\n",
			   engine->name, workarounds->hw_whitelist_count[id]);
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
	mutex_unlock(&dev->struct_mutex);

	return 0;
}

static int i915_ddb_info(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct drm_device *dev = &dev_priv->drm;
	struct skl_ddb_allocation *ddb;
	struct skl_ddb_entry *entry;
	enum pipe pipe;
	int plane;

	if (INTEL_GEN(dev_priv) < 9)
		return 0;

	drm_modeset_lock_all(dev);

	ddb = &dev_priv->wm.skl_hw.ddb;

	seq_printf(m, "%-15s%8s%8s%8s\n", "", "Start", "End", "Size");

	for_each_pipe(dev_priv, pipe) {
		seq_printf(m, "Pipe %c\n", pipe_name(pipe));

		for_each_plane(dev_priv, pipe, plane) {
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

	drm_for_each_connector(connector, dev) {
		if (connector->state->crtc != &intel_crtc->base)
			continue;

		seq_printf(m, "%s:\n", connector->name);
	}

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
			seq_puts(m, "Idleness DRRS: Disabled");
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

struct pipe_crc_info {
	const char *name;
	struct drm_i915_private *dev_priv;
	enum pipe pipe;
};

static int i915_dp_mst_info(struct seq_file *m, void *unused)
{
	struct drm_i915_private *dev_priv = node_to_i915(m->private);
	struct drm_device *dev = &dev_priv->drm;
	struct intel_encoder *intel_encoder;
	struct intel_digital_port *intel_dig_port;
	struct drm_connector *connector;

	drm_modeset_lock_all(dev);
	drm_for_each_connector(connector, dev) {
		if (connector->connector_type != DRM_MODE_CONNECTOR_DisplayPort)
			continue;

		intel_encoder = intel_attached_encoder(connector);
		if (!intel_encoder || intel_encoder->type == INTEL_OUTPUT_DP_MST)
			continue;

		intel_dig_port = enc_to_dig_port(&intel_encoder->base);
		if (!intel_dig_port->dp.can_mst)
			continue;

		seq_printf(m, "MST Source Port %c\n",
			   port_name(intel_dig_port->port));
		drm_dp_mst_dump_topology(m, &intel_dig_port->dp.mst_mgr);
	}
	drm_modeset_unlock_all(dev);
	return 0;
}

static int i915_pipe_crc_open(struct inode *inode, struct file *filep)
{
	struct pipe_crc_info *info = inode->i_private;
	struct drm_i915_private *dev_priv = info->dev_priv;
	struct intel_pipe_crc *pipe_crc = &dev_priv->pipe_crc[info->pipe];

	if (info->pipe >= INTEL_INFO(dev_priv)->num_pipes)
		return -ENODEV;

	spin_lock_irq(&pipe_crc->lock);

	if (pipe_crc->opened) {
		spin_unlock_irq(&pipe_crc->lock);
		return -EBUSY; /* already open */
	}

	pipe_crc->opened = true;
	filep->private_data = inode->i_private;

	spin_unlock_irq(&pipe_crc->lock);

	return 0;
}

static int i915_pipe_crc_release(struct inode *inode, struct file *filep)
{
	struct pipe_crc_info *info = inode->i_private;
	struct drm_i915_private *dev_priv = info->dev_priv;
	struct intel_pipe_crc *pipe_crc = &dev_priv->pipe_crc[info->pipe];

	spin_lock_irq(&pipe_crc->lock);
	pipe_crc->opened = false;
	spin_unlock_irq(&pipe_crc->lock);

	return 0;
}

/* (6 fields, 8 chars each, space separated (5) + '\n') */
#define PIPE_CRC_LINE_LEN	(6 * 8 + 5 + 1)
/* account for \'0' */
#define PIPE_CRC_BUFFER_LEN	(PIPE_CRC_LINE_LEN + 1)

static int pipe_crc_data_count(struct intel_pipe_crc *pipe_crc)
{
	assert_spin_locked(&pipe_crc->lock);
	return CIRC_CNT(pipe_crc->head, pipe_crc->tail,
			INTEL_PIPE_CRC_ENTRIES_NR);
}

static ssize_t
i915_pipe_crc_read(struct file *filep, char __user *user_buf, size_t count,
		   loff_t *pos)
{
	struct pipe_crc_info *info = filep->private_data;
	struct drm_i915_private *dev_priv = info->dev_priv;
	struct intel_pipe_crc *pipe_crc = &dev_priv->pipe_crc[info->pipe];
	char buf[PIPE_CRC_BUFFER_LEN];
	int n_entries;
	ssize_t bytes_read;

	/*
	 * Don't allow user space to provide buffers not big enough to hold
	 * a line of data.
	 */
	if (count < PIPE_CRC_LINE_LEN)
		return -EINVAL;

	if (pipe_crc->source == INTEL_PIPE_CRC_SOURCE_NONE)
		return 0;

	/* nothing to read */
	spin_lock_irq(&pipe_crc->lock);
	while (pipe_crc_data_count(pipe_crc) == 0) {
		int ret;

		if (filep->f_flags & O_NONBLOCK) {
			spin_unlock_irq(&pipe_crc->lock);
			return -EAGAIN;
		}

		ret = wait_event_interruptible_lock_irq(pipe_crc->wq,
				pipe_crc_data_count(pipe_crc), pipe_crc->lock);
		if (ret) {
			spin_unlock_irq(&pipe_crc->lock);
			return ret;
		}
	}

	/* We now have one or more entries to read */
	n_entries = count / PIPE_CRC_LINE_LEN;

	bytes_read = 0;
	while (n_entries > 0) {
		struct intel_pipe_crc_entry *entry =
			&pipe_crc->entries[pipe_crc->tail];

		if (CIRC_CNT(pipe_crc->head, pipe_crc->tail,
			     INTEL_PIPE_CRC_ENTRIES_NR) < 1)
			break;

		BUILD_BUG_ON_NOT_POWER_OF_2(INTEL_PIPE_CRC_ENTRIES_NR);
		pipe_crc->tail = (pipe_crc->tail + 1) & (INTEL_PIPE_CRC_ENTRIES_NR - 1);

		bytes_read += snprintf(buf, PIPE_CRC_BUFFER_LEN,
				       "%8u %8x %8x %8x %8x %8x\n",
				       entry->frame, entry->crc[0],
				       entry->crc[1], entry->crc[2],
				       entry->crc[3], entry->crc[4]);

		spin_unlock_irq(&pipe_crc->lock);

		if (copy_to_user(user_buf, buf, PIPE_CRC_LINE_LEN))
			return -EFAULT;

		user_buf += PIPE_CRC_LINE_LEN;
		n_entries--;

		spin_lock_irq(&pipe_crc->lock);
	}

	spin_unlock_irq(&pipe_crc->lock);

	return bytes_read;
}

static const struct file_operations i915_pipe_crc_fops = {
	.owner = THIS_MODULE,
	.open = i915_pipe_crc_open,
	.read = i915_pipe_crc_read,
	.release = i915_pipe_crc_release,
};

static struct pipe_crc_info i915_pipe_crc_data[I915_MAX_PIPES] = {
	{
		.name = "i915_pipe_A_crc",
		.pipe = PIPE_A,
	},
	{
		.name = "i915_pipe_B_crc",
		.pipe = PIPE_B,
	},
	{
		.name = "i915_pipe_C_crc",
		.pipe = PIPE_C,
	},
};

static int i915_pipe_crc_create(struct dentry *root, struct drm_minor *minor,
				enum pipe pipe)
{
	struct drm_i915_private *dev_priv = to_i915(minor->dev);
	struct dentry *ent;
	struct pipe_crc_info *info = &i915_pipe_crc_data[pipe];

	info->dev_priv = dev_priv;
	ent = debugfs_create_file(info->name, S_IRUGO, root, info,
				  &i915_pipe_crc_fops);
	if (!ent)
		return -ENOMEM;

	return drm_add_fake_info_node(minor, ent, info);
}

static const char * const pipe_crc_sources[] = {
	"none",
	"plane1",
	"plane2",
	"pf",
	"pipe",
	"TV",
	"DP-B",
	"DP-C",
	"DP-D",
	"auto",
};

static const char *pipe_crc_source_name(enum intel_pipe_crc_source source)
{
	BUILD_BUG_ON(ARRAY_SIZE(pipe_crc_sources) != INTEL_PIPE_CRC_SOURCE_MAX);
	return pipe_crc_sources[source];
}

static int display_crc_ctl_show(struct seq_file *m, void *data)
{
	struct drm_i915_private *dev_priv = m->private;
	int i;

	for (i = 0; i < I915_MAX_PIPES; i++)
		seq_printf(m, "%c %s\n", pipe_name(i),
			   pipe_crc_source_name(dev_priv->pipe_crc[i].source));

	return 0;
}

static int display_crc_ctl_open(struct inode *inode, struct file *file)
{
	return single_open(file, display_crc_ctl_show, inode->i_private);
}

static int i8xx_pipe_crc_ctl_reg(enum intel_pipe_crc_source *source,
				 uint32_t *val)
{
	if (*source == INTEL_PIPE_CRC_SOURCE_AUTO)
		*source = INTEL_PIPE_CRC_SOURCE_PIPE;

	switch (*source) {
	case INTEL_PIPE_CRC_SOURCE_PIPE:
		*val = PIPE_CRC_ENABLE | PIPE_CRC_INCLUDE_BORDER_I8XX;
		break;
	case INTEL_PIPE_CRC_SOURCE_NONE:
		*val = 0;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int i9xx_pipe_crc_auto_source(struct drm_i915_private *dev_priv,
				     enum pipe pipe,
				     enum intel_pipe_crc_source *source)
{
	struct drm_device *dev = &dev_priv->drm;
	struct intel_encoder *encoder;
	struct intel_crtc *crtc;
	struct intel_digital_port *dig_port;
	int ret = 0;

	*source = INTEL_PIPE_CRC_SOURCE_PIPE;

	drm_modeset_lock_all(dev);
	for_each_intel_encoder(dev, encoder) {
		if (!encoder->base.crtc)
			continue;

		crtc = to_intel_crtc(encoder->base.crtc);

		if (crtc->pipe != pipe)
			continue;

		switch (encoder->type) {
		case INTEL_OUTPUT_TVOUT:
			*source = INTEL_PIPE_CRC_SOURCE_TV;
			break;
		case INTEL_OUTPUT_DP:
		case INTEL_OUTPUT_EDP:
			dig_port = enc_to_dig_port(&encoder->base);
			switch (dig_port->port) {
			case PORT_B:
				*source = INTEL_PIPE_CRC_SOURCE_DP_B;
				break;
			case PORT_C:
				*source = INTEL_PIPE_CRC_SOURCE_DP_C;
				break;
			case PORT_D:
				*source = INTEL_PIPE_CRC_SOURCE_DP_D;
				break;
			default:
				WARN(1, "nonexisting DP port %c\n",
				     port_name(dig_port->port));
				break;
			}
			break;
		default:
			break;
		}
	}
	drm_modeset_unlock_all(dev);

	return ret;
}

static int vlv_pipe_crc_ctl_reg(struct drm_i915_private *dev_priv,
				enum pipe pipe,
				enum intel_pipe_crc_source *source,
				uint32_t *val)
{
	bool need_stable_symbols = false;

	if (*source == INTEL_PIPE_CRC_SOURCE_AUTO) {
		int ret = i9xx_pipe_crc_auto_source(dev_priv, pipe, source);
		if (ret)
			return ret;
	}

	switch (*source) {
	case INTEL_PIPE_CRC_SOURCE_PIPE:
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_PIPE_VLV;
		break;
	case INTEL_PIPE_CRC_SOURCE_DP_B:
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_DP_B_VLV;
		need_stable_symbols = true;
		break;
	case INTEL_PIPE_CRC_SOURCE_DP_C:
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_DP_C_VLV;
		need_stable_symbols = true;
		break;
	case INTEL_PIPE_CRC_SOURCE_DP_D:
		if (!IS_CHERRYVIEW(dev_priv))
			return -EINVAL;
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_DP_D_VLV;
		need_stable_symbols = true;
		break;
	case INTEL_PIPE_CRC_SOURCE_NONE:
		*val = 0;
		break;
	default:
		return -EINVAL;
	}

	/*
	 * When the pipe CRC tap point is after the transcoders we need
	 * to tweak symbol-level features to produce a deterministic series of
	 * symbols for a given frame. We need to reset those features only once
	 * a frame (instead of every nth symbol):
	 *   - DC-balance: used to ensure a better clock recovery from the data
	 *     link (SDVO)
	 *   - DisplayPort scrambling: used for EMI reduction
	 */
	if (need_stable_symbols) {
		uint32_t tmp = I915_READ(PORT_DFT2_G4X);

		tmp |= DC_BALANCE_RESET_VLV;
		switch (pipe) {
		case PIPE_A:
			tmp |= PIPE_A_SCRAMBLE_RESET;
			break;
		case PIPE_B:
			tmp |= PIPE_B_SCRAMBLE_RESET;
			break;
		case PIPE_C:
			tmp |= PIPE_C_SCRAMBLE_RESET;
			break;
		default:
			return -EINVAL;
		}
		I915_WRITE(PORT_DFT2_G4X, tmp);
	}

	return 0;
}

static int i9xx_pipe_crc_ctl_reg(struct drm_i915_private *dev_priv,
				 enum pipe pipe,
				 enum intel_pipe_crc_source *source,
				 uint32_t *val)
{
	bool need_stable_symbols = false;

	if (*source == INTEL_PIPE_CRC_SOURCE_AUTO) {
		int ret = i9xx_pipe_crc_auto_source(dev_priv, pipe, source);
		if (ret)
			return ret;
	}

	switch (*source) {
	case INTEL_PIPE_CRC_SOURCE_PIPE:
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_PIPE_I9XX;
		break;
	case INTEL_PIPE_CRC_SOURCE_TV:
		if (!SUPPORTS_TV(dev_priv))
			return -EINVAL;
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_TV_PRE;
		break;
	case INTEL_PIPE_CRC_SOURCE_DP_B:
		if (!IS_G4X(dev_priv))
			return -EINVAL;
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_DP_B_G4X;
		need_stable_symbols = true;
		break;
	case INTEL_PIPE_CRC_SOURCE_DP_C:
		if (!IS_G4X(dev_priv))
			return -EINVAL;
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_DP_C_G4X;
		need_stable_symbols = true;
		break;
	case INTEL_PIPE_CRC_SOURCE_DP_D:
		if (!IS_G4X(dev_priv))
			return -EINVAL;
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_DP_D_G4X;
		need_stable_symbols = true;
		break;
	case INTEL_PIPE_CRC_SOURCE_NONE:
		*val = 0;
		break;
	default:
		return -EINVAL;
	}

	/*
	 * When the pipe CRC tap point is after the transcoders we need
	 * to tweak symbol-level features to produce a deterministic series of
	 * symbols for a given frame. We need to reset those features only once
	 * a frame (instead of every nth symbol):
	 *   - DC-balance: used to ensure a better clock recovery from the data
	 *     link (SDVO)
	 *   - DisplayPort scrambling: used for EMI reduction
	 */
	if (need_stable_symbols) {
		uint32_t tmp = I915_READ(PORT_DFT2_G4X);

		WARN_ON(!IS_G4X(dev_priv));

		I915_WRITE(PORT_DFT_I9XX,
			   I915_READ(PORT_DFT_I9XX) | DC_BALANCE_RESET);

		if (pipe == PIPE_A)
			tmp |= PIPE_A_SCRAMBLE_RESET;
		else
			tmp |= PIPE_B_SCRAMBLE_RESET;

		I915_WRITE(PORT_DFT2_G4X, tmp);
	}

	return 0;
}

static void vlv_undo_pipe_scramble_reset(struct drm_i915_private *dev_priv,
					 enum pipe pipe)
{
	uint32_t tmp = I915_READ(PORT_DFT2_G4X);

	switch (pipe) {
	case PIPE_A:
		tmp &= ~PIPE_A_SCRAMBLE_RESET;
		break;
	case PIPE_B:
		tmp &= ~PIPE_B_SCRAMBLE_RESET;
		break;
	case PIPE_C:
		tmp &= ~PIPE_C_SCRAMBLE_RESET;
		break;
	default:
		return;
	}
	if (!(tmp & PIPE_SCRAMBLE_RESET_MASK))
		tmp &= ~DC_BALANCE_RESET_VLV;
	I915_WRITE(PORT_DFT2_G4X, tmp);

}

static void g4x_undo_pipe_scramble_reset(struct drm_i915_private *dev_priv,
					 enum pipe pipe)
{
	uint32_t tmp = I915_READ(PORT_DFT2_G4X);

	if (pipe == PIPE_A)
		tmp &= ~PIPE_A_SCRAMBLE_RESET;
	else
		tmp &= ~PIPE_B_SCRAMBLE_RESET;
	I915_WRITE(PORT_DFT2_G4X, tmp);

	if (!(tmp & PIPE_SCRAMBLE_RESET_MASK)) {
		I915_WRITE(PORT_DFT_I9XX,
			   I915_READ(PORT_DFT_I9XX) & ~DC_BALANCE_RESET);
	}
}

static int ilk_pipe_crc_ctl_reg(enum intel_pipe_crc_source *source,
				uint32_t *val)
{
	if (*source == INTEL_PIPE_CRC_SOURCE_AUTO)
		*source = INTEL_PIPE_CRC_SOURCE_PIPE;

	switch (*source) {
	case INTEL_PIPE_CRC_SOURCE_PLANE1:
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_PRIMARY_ILK;
		break;
	case INTEL_PIPE_CRC_SOURCE_PLANE2:
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_SPRITE_ILK;
		break;
	case INTEL_PIPE_CRC_SOURCE_PIPE:
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_PIPE_ILK;
		break;
	case INTEL_PIPE_CRC_SOURCE_NONE:
		*val = 0;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void hsw_trans_edp_pipe_A_crc_wa(struct drm_i915_private *dev_priv,
					bool enable)
{
	struct drm_device *dev = &dev_priv->drm;
	struct intel_crtc *crtc =
		to_intel_crtc(dev_priv->pipe_to_crtc_mapping[PIPE_A]);
	struct intel_crtc_state *pipe_config;
	struct drm_atomic_state *state;
	int ret = 0;

	drm_modeset_lock_all(dev);
	state = drm_atomic_state_alloc(dev);
	if (!state) {
		ret = -ENOMEM;
		goto out;
	}

	state->acquire_ctx = drm_modeset_legacy_acquire_ctx(&crtc->base);
	pipe_config = intel_atomic_get_crtc_state(state, crtc);
	if (IS_ERR(pipe_config)) {
		ret = PTR_ERR(pipe_config);
		goto out;
	}

	pipe_config->pch_pfit.force_thru = enable;
	if (pipe_config->cpu_transcoder == TRANSCODER_EDP &&
	    pipe_config->pch_pfit.enabled != enable)
		pipe_config->base.connectors_changed = true;

	ret = drm_atomic_commit(state);
out:
	drm_modeset_unlock_all(dev);
	WARN(ret, "Toggling workaround to %i returns %i\n", enable, ret);
	if (ret)
		drm_atomic_state_free(state);
}

static int ivb_pipe_crc_ctl_reg(struct drm_i915_private *dev_priv,
				enum pipe pipe,
				enum intel_pipe_crc_source *source,
				uint32_t *val)
{
	if (*source == INTEL_PIPE_CRC_SOURCE_AUTO)
		*source = INTEL_PIPE_CRC_SOURCE_PF;

	switch (*source) {
	case INTEL_PIPE_CRC_SOURCE_PLANE1:
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_PRIMARY_IVB;
		break;
	case INTEL_PIPE_CRC_SOURCE_PLANE2:
		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_SPRITE_IVB;
		break;
	case INTEL_PIPE_CRC_SOURCE_PF:
		if (IS_HASWELL(dev_priv) && pipe == PIPE_A)
			hsw_trans_edp_pipe_A_crc_wa(dev_priv, true);

		*val = PIPE_CRC_ENABLE | PIPE_CRC_SOURCE_PF_IVB;
		break;
	case INTEL_PIPE_CRC_SOURCE_NONE:
		*val = 0;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int pipe_crc_set_source(struct drm_i915_private *dev_priv,
			       enum pipe pipe,
			       enum intel_pipe_crc_source source)
{
	struct drm_device *dev = &dev_priv->drm;
	struct intel_pipe_crc *pipe_crc = &dev_priv->pipe_crc[pipe];
	struct intel_crtc *crtc =
			to_intel_crtc(intel_get_crtc_for_pipe(dev, pipe));
	enum intel_display_power_domain power_domain;
	u32 val = 0; /* shut up gcc */
	int ret;

	if (pipe_crc->source == source)
		return 0;

	/* forbid changing the source without going back to 'none' */
	if (pipe_crc->source && source)
		return -EINVAL;

	power_domain = POWER_DOMAIN_PIPE(pipe);
	if (!intel_display_power_get_if_enabled(dev_priv, power_domain)) {
		DRM_DEBUG_KMS("Trying to capture CRC while pipe is off\n");
		return -EIO;
	}

	if (IS_GEN2(dev_priv))
		ret = i8xx_pipe_crc_ctl_reg(&source, &val);
	else if (INTEL_GEN(dev_priv) < 5)
		ret = i9xx_pipe_crc_ctl_reg(dev_priv, pipe, &source, &val);
	else if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
		ret = vlv_pipe_crc_ctl_reg(dev_priv, pipe, &source, &val);
	else if (IS_GEN5(dev_priv) || IS_GEN6(dev_priv))
		ret = ilk_pipe_crc_ctl_reg(&source, &val);
	else
		ret = ivb_pipe_crc_ctl_reg(dev_priv, pipe, &source, &val);

	if (ret != 0)
		goto out;

	/* none -> real source transition */
	if (source) {
		struct intel_pipe_crc_entry *entries;

		DRM_DEBUG_DRIVER("collecting CRCs for pipe %c, %s\n",
				 pipe_name(pipe), pipe_crc_source_name(source));

		entries = kcalloc(INTEL_PIPE_CRC_ENTRIES_NR,
				  sizeof(pipe_crc->entries[0]),
				  GFP_KERNEL);
		if (!entries) {
			ret = -ENOMEM;
			goto out;
		}

		/*
		 * When IPS gets enabled, the pipe CRC changes. Since IPS gets
		 * enabled and disabled dynamically based on package C states,
		 * user space can't make reliable use of the CRCs, so let's just
		 * completely disable it.
		 */
		hsw_disable_ips(crtc);

		spin_lock_irq(&pipe_crc->lock);
		kfree(pipe_crc->entries);
		pipe_crc->entries = entries;
		pipe_crc->head = 0;
		pipe_crc->tail = 0;
		spin_unlock_irq(&pipe_crc->lock);
	}

	pipe_crc->source = source;

	I915_WRITE(PIPE_CRC_CTL(pipe), val);
	POSTING_READ(PIPE_CRC_CTL(pipe));

	/* real source -> none transition */
	if (source == INTEL_PIPE_CRC_SOURCE_NONE) {
		struct intel_pipe_crc_entry *entries;
		struct intel_crtc *crtc =
			to_intel_crtc(dev_priv->pipe_to_crtc_mapping[pipe]);

		DRM_DEBUG_DRIVER("stopping CRCs for pipe %c\n",
				 pipe_name(pipe));

		drm_modeset_lock(&crtc->base.mutex, NULL);
		if (crtc->base.state->active)
			intel_wait_for_vblank(dev, pipe);
		drm_modeset_unlock(&crtc->base.mutex);

		spin_lock_irq(&pipe_crc->lock);
		entries = pipe_crc->entries;
		pipe_crc->entries = NULL;
		pipe_crc->head = 0;
		pipe_crc->tail = 0;
		spin_unlock_irq(&pipe_crc->lock);

		kfree(entries);

		if (IS_G4X(dev_priv))
			g4x_undo_pipe_scramble_reset(dev_priv, pipe);
		else if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv))
			vlv_undo_pipe_scramble_reset(dev_priv, pipe);
		else if (IS_HASWELL(dev_priv) && pipe == PIPE_A)
			hsw_trans_edp_pipe_A_crc_wa(dev_priv, false);

		hsw_enable_ips(crtc);
	}

	ret = 0;

out:
	intel_display_power_put(dev_priv, power_domain);

	return ret;
}

/*
 * Parse pipe CRC command strings:
 *   command: wsp* object wsp+ name wsp+ source wsp*
 *   object: 'pipe'
 *   name: (A | B | C)
 *   source: (none | plane1 | plane2 | pf)
 *   wsp: (#0x20 | #0x9 | #0xA)+
 *
 * eg.:
 *  "pipe A plane1"  ->  Start CRC computations on plane1 of pipe A
 *  "pipe A none"    ->  Stop CRC
 */
static int display_crc_ctl_tokenize(char *buf, char *words[], int max_words)
{
	int n_words = 0;

	while (*buf) {
		char *end;

		/* skip leading white space */
		buf = skip_spaces(buf);
		if (!*buf)
			break;	/* end of buffer */

		/* find end of word */
		for (end = buf; *end && !isspace(*end); end++)
			;

		if (n_words == max_words) {
			DRM_DEBUG_DRIVER("too many words, allowed <= %d\n",
					 max_words);
			return -EINVAL;	/* ran out of words[] before bytes */
		}

		if (*end)
			*end++ = '\0';
		words[n_words++] = buf;
		buf = end;
	}

	return n_words;
}

enum intel_pipe_crc_object {
	PIPE_CRC_OBJECT_PIPE,
};

static const char * const pipe_crc_objects[] = {
	"pipe",
};

static int
display_crc_ctl_parse_object(const char *buf, enum intel_pipe_crc_object *o)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(pipe_crc_objects); i++)
		if (!strcmp(buf, pipe_crc_objects[i])) {
			*o = i;
			return 0;
		    }

	return -EINVAL;
}

static int display_crc_ctl_parse_pipe(const char *buf, enum pipe *pipe)
{
	const char name = buf[0];

	if (name < 'A' || name >= pipe_name(I915_MAX_PIPES))
		return -EINVAL;

	*pipe = name - 'A';

	return 0;
}

static int
display_crc_ctl_parse_source(const char *buf, enum intel_pipe_crc_source *s)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(pipe_crc_sources); i++)
		if (!strcmp(buf, pipe_crc_sources[i])) {
			*s = i;
			return 0;
		    }

	return -EINVAL;
}

static int display_crc_ctl_parse(struct drm_i915_private *dev_priv,
				 char *buf, size_t len)
{
#define N_WORDS 3
	int n_words;
	char *words[N_WORDS];
	enum pipe pipe;
	enum intel_pipe_crc_object object;
	enum intel_pipe_crc_source source;

	n_words = display_crc_ctl_tokenize(buf, words, N_WORDS);
	if (n_words != N_WORDS) {
		DRM_DEBUG_DRIVER("tokenize failed, a command is %d words\n",
				 N_WORDS);
		return -EINVAL;
	}

	if (display_crc_ctl_parse_object(words[0], &object) < 0) {
		DRM_DEBUG_DRIVER("unknown object %s\n", words[0]);
		return -EINVAL;
	}

	if (display_crc_ctl_parse_pipe(words[1], &pipe) < 0) {
		DRM_DEBUG_DRIVER("unknown pipe %s\n", words[1]);
		return -EINVAL;
	}

	if (display_crc_ctl_parse_source(words[2], &source) < 0) {
		DRM_DEBUG_DRIVER("unknown source %s\n", words[2]);
		return -EINVAL;
	}

	return pipe_crc_set_source(dev_priv, pipe, source);
}

static ssize_t display_crc_ctl_write(struct file *file, const char __user *ubuf,
				     size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	struct drm_i915_private *dev_priv = m->private;
	char *tmpbuf;
	int ret;

	if (len == 0)
		return 0;

	if (len > PAGE_SIZE - 1) {
		DRM_DEBUG_DRIVER("expected <%lu bytes into pipe crc control\n",
				 PAGE_SIZE);
		return -E2BIG;
	}

	tmpbuf = kmalloc(len + 1, GFP_KERNEL);
	if (!tmpbuf)
		return -ENOMEM;

	if (copy_from_user(tmpbuf, ubuf, len)) {
		ret = -EFAULT;
		goto out;
	}
	tmpbuf[len] = '\0';

	ret = display_crc_ctl_parse(dev_priv, tmpbuf, len);

out:
	kfree(tmpbuf);
	if (ret < 0)
		return ret;

	*offp += len;
	return len;
}

static const struct file_operations i915_display_crc_ctl_fops = {
	.owner = THIS_MODULE,
	.open = display_crc_ctl_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = display_crc_ctl_write
};

static ssize_t i915_displayport_test_active_write(struct file *file,
						  const char __user *ubuf,
						  size_t len, loff_t *offp)
{
	char *input_buffer;
	int status = 0;
	struct drm_device *dev;
	struct drm_connector *connector;
	struct list_head *connector_list;
	struct intel_dp *intel_dp;
	int val = 0;

	dev = ((struct seq_file *)file->private_data)->private;

	connector_list = &dev->mode_config.connector_list;

	if (len == 0)
		return 0;

	input_buffer = kmalloc(len + 1, GFP_KERNEL);
	if (!input_buffer)
		return -ENOMEM;

	if (copy_from_user(input_buffer, ubuf, len)) {
		status = -EFAULT;
		goto out;
	}

	input_buffer[len] = '\0';
	DRM_DEBUG_DRIVER("Copied %d bytes from user\n", (unsigned int)len);

	list_for_each_entry(connector, connector_list, head) {
		if (connector->connector_type !=
		    DRM_MODE_CONNECTOR_DisplayPort)
			continue;

		if (connector->status == connector_status_connected &&
		    connector->encoder != NULL) {
			intel_dp = enc_to_intel_dp(connector->encoder);
			status = kstrtoint(input_buffer, 10, &val);
			if (status < 0)
				goto out;
			DRM_DEBUG_DRIVER("Got %d for test active\n", val);
			/* To prevent erroneous activation of the compliance
			 * testing code, only accept an actual value of 1 here
			 */
			if (val == 1)
				intel_dp->compliance_test_active = 1;
			else
				intel_dp->compliance_test_active = 0;
		}
	}
out:
	kfree(input_buffer);
	if (status < 0)
		return status;

	*offp += len;
	return len;
}

static int i915_displayport_test_active_show(struct seq_file *m, void *data)
{
	struct drm_device *dev = m->private;
	struct drm_connector *connector;
	struct list_head *connector_list = &dev->mode_config.connector_list;
	struct intel_dp *intel_dp;

	list_for_each_entry(connector, connector_list, head) {
		if (connector->connector_type !=
		    DRM_MODE_CONNECTOR_DisplayPort)
			continue;

		if (connector->status == connector_status_connected &&
		    connector->encoder != NULL) {
			intel_dp = enc_to_intel_dp(connector->encoder);
			if (intel_dp->compliance_test_active)
				seq_puts(m, "1");
			else
				seq_puts(m, "0");
		} else
			seq_puts(m, "0");
	}

	return 0;
}

static int i915_displayport_test_active_open(struct inode *inode,
					     struct file *file)
{
	struct drm_i915_private *dev_priv = inode->i_private;

	return single_open(file, i915_displayport_test_active_show,
			   &dev_priv->drm);
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
	struct drm_device *dev = m->private;
	struct drm_connector *connector;
	struct list_head *connector_list = &dev->mode_config.connector_list;
	struct intel_dp *intel_dp;

	list_for_each_entry(connector, connector_list, head) {
		if (connector->connector_type !=
		    DRM_MODE_CONNECTOR_DisplayPort)
			continue;

		if (connector->status == connector_status_connected &&
		    connector->encoder != NULL) {
			intel_dp = enc_to_intel_dp(connector->encoder);
			seq_printf(m, "%lx", intel_dp->compliance_test_data);
		} else
			seq_puts(m, "0");
	}

	return 0;
}
static int i915_displayport_test_data_open(struct inode *inode,
					   struct file *file)
{
	struct drm_i915_private *dev_priv = inode->i_private;

	return single_open(file, i915_displayport_test_data_show,
			   &dev_priv->drm);
}

static const struct file_operations i915_displayport_test_data_fops = {
	.owner = THIS_MODULE,
	.open = i915_displayport_test_data_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release
};

static int i915_displayport_test_type_show(struct seq_file *m, void *data)
{
	struct drm_device *dev = m->private;
	struct drm_connector *connector;
	struct list_head *connector_list = &dev->mode_config.connector_list;
	struct intel_dp *intel_dp;

	list_for_each_entry(connector, connector_list, head) {
		if (connector->connector_type !=
		    DRM_MODE_CONNECTOR_DisplayPort)
			continue;

		if (connector->status == connector_status_connected &&
		    connector->encoder != NULL) {
			intel_dp = enc_to_intel_dp(connector->encoder);
			seq_printf(m, "%02lx", intel_dp->compliance_test_type);
		} else
			seq_puts(m, "0");
	}

	return 0;
}

static int i915_displayport_test_type_open(struct inode *inode,
				       struct file *file)
{
	struct drm_i915_private *dev_priv = inode->i_private;

	return single_open(file, i915_displayport_test_type_show,
			   &dev_priv->drm);
}

static const struct file_operations i915_displayport_test_type_fops = {
	.owner = THIS_MODULE,
	.open = i915_displayport_test_type_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release
};

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
	else
		num_levels = ilk_wm_max_level(dev) + 1;

	drm_modeset_lock_all(dev);

	for (level = 0; level < num_levels; level++) {
		unsigned int latency = wm[level];

		/*
		 * - WM1+ latency values in 0.5us units
		 * - latencies are in us on gen9/vlv/chv
		 */
		if (INTEL_GEN(dev_priv) >= 9 || IS_VALLEYVIEW(dev_priv) ||
		    IS_CHERRYVIEW(dev_priv))
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

	if (INTEL_GEN(dev_priv) < 5)
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
	else
		num_levels = ilk_wm_max_level(dev) + 1;

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
	struct drm_i915_private *dev_priv = data;

	/*
	 * There is no safeguard against this debugfs entry colliding
	 * with the hangcheck calling same i915_handle_error() in
	 * parallel, causing an explosion. For now we assume that the
	 * test harness is responsible enough not to inject gpu hangs
	 * while it is writing to 'i915_wedged'
	 */

	if (i915_reset_in_progress(&dev_priv->gpu_error))
		return -EAGAIN;

	intel_runtime_pm_get(dev_priv);

	i915_handle_error(dev_priv, val,
			  "Manually setting wedged to %llu", val);

	intel_runtime_pm_put(dev_priv);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(i915_wedged_fops,
			i915_wedged_get, i915_wedged_set,
			"%llu\n");

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
	struct drm_i915_private *dev_priv = data;
	struct drm_device *dev = &dev_priv->drm;
	int ret;

	/* Lock against concurrent debugfs callers */
	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;
	dev_priv->gpu_error.missed_irq_rings = val;
	mutex_unlock(&dev->struct_mutex);

	return 0;
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
	struct drm_i915_private *dev_priv = data;

	val &= INTEL_INFO(dev_priv)->ring_mask;
	DRM_DEBUG_DRIVER("Masking interrupts on rings 0x%08llx\n", val);
	dev_priv->gpu_error.test_irq_rings = val;

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(i915_ring_test_irq_fops,
			i915_ring_test_irq_get, i915_ring_test_irq_set,
			"0x%08llx\n");

#define DROP_UNBOUND 0x1
#define DROP_BOUND 0x2
#define DROP_RETIRE 0x4
#define DROP_ACTIVE 0x8
#define DROP_ALL (DROP_UNBOUND | \
		  DROP_BOUND | \
		  DROP_RETIRE | \
		  DROP_ACTIVE)
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
	int ret;

	DRM_DEBUG("Dropping caches: 0x%08llx\n", val);

	/* No need to check and wait for gpu resets, only libdrm auto-restarts
	 * on ioctls on -EAGAIN. */
	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	if (val & DROP_ACTIVE) {
		ret = i915_gem_wait_for_idle(dev_priv,
					     I915_WAIT_INTERRUPTIBLE |
					     I915_WAIT_LOCKED);
		if (ret)
			goto unlock;
	}

	if (val & (DROP_RETIRE | DROP_ACTIVE))
		i915_gem_retire_requests(dev_priv);

	if (val & DROP_BOUND)
		i915_gem_shrink(dev_priv, LONG_MAX, I915_SHRINK_BOUND);

	if (val & DROP_UNBOUND)
		i915_gem_shrink(dev_priv, LONG_MAX, I915_SHRINK_UNBOUND);

unlock:
	mutex_unlock(&dev->struct_mutex);

	return ret;
}

DEFINE_SIMPLE_ATTRIBUTE(i915_drop_caches_fops,
			i915_drop_caches_get, i915_drop_caches_set,
			"0x%08llx\n");

static int
i915_max_freq_get(void *data, u64 *val)
{
	struct drm_i915_private *dev_priv = data;

	if (INTEL_GEN(dev_priv) < 6)
		return -ENODEV;

	*val = intel_gpu_freq(dev_priv, dev_priv->rps.max_freq_softlimit);
	return 0;
}

static int
i915_max_freq_set(void *data, u64 val)
{
	struct drm_i915_private *dev_priv = data;
	u32 hw_max, hw_min;
	int ret;

	if (INTEL_GEN(dev_priv) < 6)
		return -ENODEV;

	DRM_DEBUG_DRIVER("Manually setting max freq to %llu\n", val);

	ret = mutex_lock_interruptible(&dev_priv->rps.hw_lock);
	if (ret)
		return ret;

	/*
	 * Turbo will still be enabled, but won't go above the set value.
	 */
	val = intel_freq_opcode(dev_priv, val);

	hw_max = dev_priv->rps.max_freq;
	hw_min = dev_priv->rps.min_freq;

	if (val < hw_min || val > hw_max || val < dev_priv->rps.min_freq_softlimit) {
		mutex_unlock(&dev_priv->rps.hw_lock);
		return -EINVAL;
	}

	dev_priv->rps.max_freq_softlimit = val;

	intel_set_rps(dev_priv, val);

	mutex_unlock(&dev_priv->rps.hw_lock);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(i915_max_freq_fops,
			i915_max_freq_get, i915_max_freq_set,
			"%llu\n");

static int
i915_min_freq_get(void *data, u64 *val)
{
	struct drm_i915_private *dev_priv = data;

	if (INTEL_GEN(dev_priv) < 6)
		return -ENODEV;

	*val = intel_gpu_freq(dev_priv, dev_priv->rps.min_freq_softlimit);
	return 0;
}

static int
i915_min_freq_set(void *data, u64 val)
{
	struct drm_i915_private *dev_priv = data;
	u32 hw_max, hw_min;
	int ret;

	if (INTEL_GEN(dev_priv) < 6)
		return -ENODEV;

	DRM_DEBUG_DRIVER("Manually setting min freq to %llu\n", val);

	ret = mutex_lock_interruptible(&dev_priv->rps.hw_lock);
	if (ret)
		return ret;

	/*
	 * Turbo will still be enabled, but won't go below the set value.
	 */
	val = intel_freq_opcode(dev_priv, val);

	hw_max = dev_priv->rps.max_freq;
	hw_min = dev_priv->rps.min_freq;

	if (val < hw_min ||
	    val > hw_max || val > dev_priv->rps.max_freq_softlimit) {
		mutex_unlock(&dev_priv->rps.hw_lock);
		return -EINVAL;
	}

	dev_priv->rps.min_freq_softlimit = val;

	intel_set_rps(dev_priv, val);

	mutex_unlock(&dev_priv->rps.hw_lock);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(i915_min_freq_fops,
			i915_min_freq_get, i915_min_freq_set,
			"%llu\n");

static int
i915_cache_sharing_get(void *data, u64 *val)
{
	struct drm_i915_private *dev_priv = data;
	struct drm_device *dev = &dev_priv->drm;
	u32 snpcr;
	int ret;

	if (!(IS_GEN6(dev_priv) || IS_GEN7(dev_priv)))
		return -ENODEV;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;
	intel_runtime_pm_get(dev_priv);

	snpcr = I915_READ(GEN6_MBCUNIT_SNPCR);

	intel_runtime_pm_put(dev_priv);
	mutex_unlock(&dev->struct_mutex);

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
	int ss_max = 2;
	int ss;
	u32 sig1[ss_max], sig2[ss_max];

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
		sseu->subslice_mask |= BIT(ss);
		eu_cnt = ((sig1[ss] & CHV_EU08_PG_ENABLE) ? 0 : 2) +
			 ((sig1[ss] & CHV_EU19_PG_ENABLE) ? 0 : 2) +
			 ((sig1[ss] & CHV_EU210_PG_ENABLE) ? 0 : 2) +
			 ((sig2[ss] & CHV_EU311_PG_ENABLE) ? 0 : 2);
		sseu->eu_total += eu_cnt;
		sseu->eu_per_subslice = max_t(unsigned int,
					      sseu->eu_per_subslice, eu_cnt);
	}
}

static void gen9_sseu_device_status(struct drm_i915_private *dev_priv,
				    struct sseu_dev_info *sseu)
{
	int s_max = 3, ss_max = 4;
	int s, ss;
	u32 s_reg[s_max], eu_reg[2*s_max], eu_mask[2];

	/* BXT has a single slice and at most 3 subslices. */
	if (IS_BROXTON(dev_priv)) {
		s_max = 1;
		ss_max = 3;
	}

	for (s = 0; s < s_max; s++) {
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

	for (s = 0; s < s_max; s++) {
		if ((s_reg[s] & GEN9_PGCTL_SLICE_ACK) == 0)
			/* skip disabled slice */
			continue;

		sseu->slice_mask |= BIT(s);

		if (IS_SKYLAKE(dev_priv) || IS_KABYLAKE(dev_priv))
			sseu->subslice_mask =
				INTEL_INFO(dev_priv)->sseu.subslice_mask;

		for (ss = 0; ss < ss_max; ss++) {
			unsigned int eu_cnt;

			if (IS_BROXTON(dev_priv)) {
				if (!(s_reg[s] & (GEN9_PGCTL_SS_ACK(ss))))
					/* skip disabled subslice */
					continue;

				sseu->subslice_mask |= BIT(ss);
			}

			eu_cnt = 2 * hweight32(eu_reg[2*s + ss/2] &
					       eu_mask[ss%2]);
			sseu->eu_total += eu_cnt;
			sseu->eu_per_subslice = max_t(unsigned int,
						      sseu->eu_per_subslice,
						      eu_cnt);
		}
	}
}

static void broadwell_sseu_device_status(struct drm_i915_private *dev_priv,
					 struct sseu_dev_info *sseu)
{
	u32 slice_info = I915_READ(GEN8_GT_SLICE_INFO);
	int s;

	sseu->slice_mask = slice_info & GEN8_LSLICESTAT_MASK;

	if (sseu->slice_mask) {
		sseu->subslice_mask = INTEL_INFO(dev_priv)->sseu.subslice_mask;
		sseu->eu_per_subslice =
				INTEL_INFO(dev_priv)->sseu.eu_per_subslice;
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

	seq_printf(m, "  %s Slice Mask: %04x\n", type,
		   sseu->slice_mask);
	seq_printf(m, "  %s Slice Total: %u\n", type,
		   hweight8(sseu->slice_mask));
	seq_printf(m, "  %s Subslice Total: %u\n", type,
		   sseu_subslice_total(sseu));
	seq_printf(m, "  %s Subslice Mask: %04x\n", type,
		   sseu->subslice_mask);
	seq_printf(m, "  %s Subslice Per Slice: %u\n", type,
		   hweight8(sseu->subslice_mask));
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

	intel_runtime_pm_get(dev_priv);

	if (IS_CHERRYVIEW(dev_priv)) {
		cherryview_sseu_device_status(dev_priv, &sseu);
	} else if (IS_BROADWELL(dev_priv)) {
		broadwell_sseu_device_status(dev_priv, &sseu);
	} else if (INTEL_GEN(dev_priv) >= 9) {
		gen9_sseu_device_status(dev_priv, &sseu);
	}

	intel_runtime_pm_put(dev_priv);

	i915_print_sseu_info(m, false, &sseu);

	return 0;
}

static int i915_forcewake_open(struct inode *inode, struct file *file)
{
	struct drm_i915_private *dev_priv = inode->i_private;

	if (INTEL_GEN(dev_priv) < 6)
		return 0;

	intel_runtime_pm_get(dev_priv);
	intel_uncore_forcewake_get(dev_priv, FORCEWAKE_ALL);

	return 0;
}

static int i915_forcewake_release(struct inode *inode, struct file *file)
{
	struct drm_i915_private *dev_priv = inode->i_private;

	if (INTEL_GEN(dev_priv) < 6)
		return 0;

	intel_uncore_forcewake_put(dev_priv, FORCEWAKE_ALL);
	intel_runtime_pm_put(dev_priv);

	return 0;
}

static const struct file_operations i915_forcewake_fops = {
	.owner = THIS_MODULE,
	.open = i915_forcewake_open,
	.release = i915_forcewake_release,
};

static int i915_forcewake_create(struct dentry *root, struct drm_minor *minor)
{
	struct dentry *ent;

	ent = debugfs_create_file("i915_forcewake_user",
				  S_IRUSR,
				  root, to_i915(minor->dev),
				  &i915_forcewake_fops);
	if (!ent)
		return -ENOMEM;

	return drm_add_fake_info_node(minor, ent, &i915_forcewake_fops);
}

static int i915_debugfs_create(struct dentry *root,
			       struct drm_minor *minor,
			       const char *name,
			       const struct file_operations *fops)
{
	struct dentry *ent;

	ent = debugfs_create_file(name,
				  S_IRUGO | S_IWUSR,
				  root, to_i915(minor->dev),
				  fops);
	if (!ent)
		return -ENOMEM;

	return drm_add_fake_info_node(minor, ent, fops);
}

static const struct drm_info_list i915_debugfs_list[] = {
	{"i915_capabilities", i915_capabilities, 0},
	{"i915_gem_objects", i915_gem_object_info, 0},
	{"i915_gem_gtt", i915_gem_gtt_info, 0},
	{"i915_gem_pin_display", i915_gem_gtt_info, 0, (void *)1},
	{"i915_gem_stolen", i915_gem_stolen_list_info },
	{"i915_gem_pageflip", i915_gem_pageflip_info, 0},
	{"i915_gem_request", i915_gem_request_info, 0},
	{"i915_gem_seqno", i915_gem_seqno_info, 0},
	{"i915_gem_fence_regs", i915_gem_fence_regs_info, 0},
	{"i915_gem_interrupt", i915_interrupt_info, 0},
	{"i915_gem_hws", i915_hws_info, 0, (void *)RCS},
	{"i915_gem_hws_blt", i915_hws_info, 0, (void *)BCS},
	{"i915_gem_hws_bsd", i915_hws_info, 0, (void *)VCS},
	{"i915_gem_hws_vebox", i915_hws_info, 0, (void *)VECS},
	{"i915_gem_batch_pool", i915_gem_batch_pool_info, 0},
	{"i915_guc_info", i915_guc_info, 0},
	{"i915_guc_load_status", i915_guc_load_status_info, 0},
	{"i915_guc_log_dump", i915_guc_log_dump, 0},
	{"i915_frequency_info", i915_frequency_info, 0},
	{"i915_hangcheck_info", i915_hangcheck_info, 0},
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
	{"i915_dump_lrc", i915_dump_lrc, 0},
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
	{"i915_semaphore_status", i915_semaphore_status, 0},
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
	{"i915_max_freq", &i915_max_freq_fops},
	{"i915_min_freq", &i915_min_freq_fops},
	{"i915_cache_sharing", &i915_cache_sharing_fops},
	{"i915_ring_missed_irq", &i915_ring_missed_irq_fops},
	{"i915_ring_test_irq", &i915_ring_test_irq_fops},
	{"i915_gem_drop_caches", &i915_drop_caches_fops},
	{"i915_error_state", &i915_error_state_fops},
	{"i915_next_seqno", &i915_next_seqno_fops},
	{"i915_display_crc_ctl", &i915_display_crc_ctl_fops},
	{"i915_pri_wm_latency", &i915_pri_wm_latency_fops},
	{"i915_spr_wm_latency", &i915_spr_wm_latency_fops},
	{"i915_cur_wm_latency", &i915_cur_wm_latency_fops},
	{"i915_fbc_false_color", &i915_fbc_fc_fops},
	{"i915_dp_test_data", &i915_displayport_test_data_fops},
	{"i915_dp_test_type", &i915_displayport_test_type_fops},
	{"i915_dp_test_active", &i915_displayport_test_active_fops}
};

void intel_display_crc_init(struct drm_i915_private *dev_priv)
{
	enum pipe pipe;

	for_each_pipe(dev_priv, pipe) {
		struct intel_pipe_crc *pipe_crc = &dev_priv->pipe_crc[pipe];

		pipe_crc->opened = false;
		spin_lock_init(&pipe_crc->lock);
		init_waitqueue_head(&pipe_crc->wq);
	}
}

int i915_debugfs_register(struct drm_i915_private *dev_priv)
{
	struct drm_minor *minor = dev_priv->drm.primary;
	int ret, i;

	ret = i915_forcewake_create(minor->debugfs_root, minor);
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(i915_pipe_crc_data); i++) {
		ret = i915_pipe_crc_create(minor->debugfs_root, minor, i);
		if (ret)
			return ret;
	}

	for (i = 0; i < ARRAY_SIZE(i915_debugfs_files); i++) {
		ret = i915_debugfs_create(minor->debugfs_root, minor,
					  i915_debugfs_files[i].name,
					  i915_debugfs_files[i].fops);
		if (ret)
			return ret;
	}

	return drm_debugfs_create_files(i915_debugfs_list,
					I915_DEBUGFS_ENTRIES,
					minor->debugfs_root, minor);
}

void i915_debugfs_unregister(struct drm_i915_private *dev_priv)
{
	struct drm_minor *minor = dev_priv->drm.primary;
	int i;

	drm_debugfs_remove_files(i915_debugfs_list,
				 I915_DEBUGFS_ENTRIES, minor);

	drm_debugfs_remove_files((struct drm_info_list *)&i915_forcewake_fops,
				 1, minor);

	for (i = 0; i < ARRAY_SIZE(i915_pipe_crc_data); i++) {
		struct drm_info_list *info_list =
			(struct drm_info_list *)&i915_pipe_crc_data[i];

		drm_debugfs_remove_files(info_list, 1, minor);
	}

	for (i = 0; i < ARRAY_SIZE(i915_debugfs_files); i++) {
		struct drm_info_list *info_list =
			(struct drm_info_list *)i915_debugfs_files[i].fops;

		drm_debugfs_remove_files(info_list, 1, minor);
	}
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

static int i915_dpcd_open(struct inode *inode, struct file *file)
{
	return single_open(file, i915_dpcd_show, inode->i_private);
}

static const struct file_operations i915_dpcd_fops = {
	.owner = THIS_MODULE,
	.open = i915_dpcd_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

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

static int i915_panel_open(struct inode *inode, struct file *file)
{
	return single_open(file, i915_panel_show, inode->i_private);
}

static const struct file_operations i915_panel_fops = {
	.owner = THIS_MODULE,
	.open = i915_panel_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

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
