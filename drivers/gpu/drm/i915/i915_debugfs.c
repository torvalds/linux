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
#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/export.h>
#include "drmP.h"
#include "drm.h"
#include "intel_drv.h"
#include "intel_ringbuffer.h"
#include "i915_drm.h"
#include "i915_drv.h"

#define DRM_I915_RING_DEBUG 1


#if defined(CONFIG_DEBUG_FS)

enum {
	ACTIVE_LIST,
	FLUSHING_LIST,
	INACTIVE_LIST,
	PINNED_LIST,
	DEFERRED_FREE_LIST,
};

static const char *yesno(int v)
{
	return v ? "yes" : "no";
}

static int i915_capabilities(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	const struct intel_device_info *info = INTEL_INFO(dev);

	seq_printf(m, "gen: %d\n", info->gen);
#define B(x) seq_printf(m, #x ": %s\n", yesno(info->x))
	B(is_mobile);
	B(is_i85x);
	B(is_i915g);
	B(is_i945gm);
	B(is_g33);
	B(need_gfx_hws);
	B(is_g4x);
	B(is_pineview);
	B(is_broadwater);
	B(is_crestline);
	B(has_fbc);
	B(has_pipe_cxsr);
	B(has_hotplug);
	B(cursor_needs_physical);
	B(has_overlay);
	B(overlay_needs_physical);
	B(supports_tv);
	B(has_bsd_ring);
	B(has_blt_ring);
#undef B

	return 0;
}

static const char *get_pin_flag(struct drm_i915_gem_object *obj)
{
	if (obj->user_pin_count > 0)
		return "P";
	else if (obj->pin_count > 0)
		return "p";
	else
		return " ";
}

static const char *get_tiling_flag(struct drm_i915_gem_object *obj)
{
	switch (obj->tiling_mode) {
	default:
	case I915_TILING_NONE: return " ";
	case I915_TILING_X: return "X";
	case I915_TILING_Y: return "Y";
	}
}

static const char *cache_level_str(int type)
{
	switch (type) {
	case I915_CACHE_NONE: return " uncached";
	case I915_CACHE_LLC: return " snooped (LLC)";
	case I915_CACHE_LLC_MLC: return " snooped (LLC+MLC)";
	default: return "";
	}
}

static void
describe_obj(struct seq_file *m, struct drm_i915_gem_object *obj)
{
	seq_printf(m, "%p: %s%s %8zd %04x %04x %d %d%s%s%s",
		   &obj->base,
		   get_pin_flag(obj),
		   get_tiling_flag(obj),
		   obj->base.size,
		   obj->base.read_domains,
		   obj->base.write_domain,
		   obj->last_rendering_seqno,
		   obj->last_fenced_seqno,
		   cache_level_str(obj->cache_level),
		   obj->dirty ? " dirty" : "",
		   obj->madv == I915_MADV_DONTNEED ? " purgeable" : "");
	if (obj->base.name)
		seq_printf(m, " (name: %d)", obj->base.name);
	if (obj->fence_reg != I915_FENCE_REG_NONE)
		seq_printf(m, " (fence: %d)", obj->fence_reg);
	if (obj->gtt_space != NULL)
		seq_printf(m, " (gtt offset: %08x, size: %08x)",
			   obj->gtt_offset, (unsigned int)obj->gtt_space->size);
	if (obj->pin_mappable || obj->fault_mappable) {
		char s[3], *t = s;
		if (obj->pin_mappable)
			*t++ = 'p';
		if (obj->fault_mappable)
			*t++ = 'f';
		*t = '\0';
		seq_printf(m, " (%s mappable)", s);
	}
	if (obj->ring != NULL)
		seq_printf(m, " (%s)", obj->ring->name);
}

static int i915_gem_object_list_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	uintptr_t list = (uintptr_t) node->info_ent->data;
	struct list_head *head;
	struct drm_device *dev = node->minor->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj;
	size_t total_obj_size, total_gtt_size;
	int count, ret;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	switch (list) {
	case ACTIVE_LIST:
		seq_printf(m, "Active:\n");
		head = &dev_priv->mm.active_list;
		break;
	case INACTIVE_LIST:
		seq_printf(m, "Inactive:\n");
		head = &dev_priv->mm.inactive_list;
		break;
	case PINNED_LIST:
		seq_printf(m, "Pinned:\n");
		head = &dev_priv->mm.pinned_list;
		break;
	case FLUSHING_LIST:
		seq_printf(m, "Flushing:\n");
		head = &dev_priv->mm.flushing_list;
		break;
	case DEFERRED_FREE_LIST:
		seq_printf(m, "Deferred free:\n");
		head = &dev_priv->mm.deferred_free_list;
		break;
	default:
		mutex_unlock(&dev->struct_mutex);
		return -EINVAL;
	}

	total_obj_size = total_gtt_size = count = 0;
	list_for_each_entry(obj, head, mm_list) {
		seq_printf(m, "   ");
		describe_obj(m, obj);
		seq_printf(m, "\n");
		total_obj_size += obj->base.size;
		total_gtt_size += obj->gtt_space->size;
		count++;
	}
	mutex_unlock(&dev->struct_mutex);

	seq_printf(m, "Total %d objects, %zu bytes, %zu GTT size\n",
		   count, total_obj_size, total_gtt_size);
	return 0;
}

#define count_objects(list, member) do { \
	list_for_each_entry(obj, list, member) { \
		size += obj->gtt_space->size; \
		++count; \
		if (obj->map_and_fenceable) { \
			mappable_size += obj->gtt_space->size; \
			++mappable_count; \
		} \
	} \
} while (0)

static int i915_gem_object_info(struct seq_file *m, void* data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 count, mappable_count;
	size_t size, mappable_size;
	struct drm_i915_gem_object *obj;
	int ret;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	seq_printf(m, "%u objects, %zu bytes\n",
		   dev_priv->mm.object_count,
		   dev_priv->mm.object_memory);

	size = count = mappable_size = mappable_count = 0;
	count_objects(&dev_priv->mm.gtt_list, gtt_list);
	seq_printf(m, "%u [%u] objects, %zu [%zu] bytes in gtt\n",
		   count, mappable_count, size, mappable_size);

	size = count = mappable_size = mappable_count = 0;
	count_objects(&dev_priv->mm.active_list, mm_list);
	count_objects(&dev_priv->mm.flushing_list, mm_list);
	seq_printf(m, "  %u [%u] active objects, %zu [%zu] bytes\n",
		   count, mappable_count, size, mappable_size);

	size = count = mappable_size = mappable_count = 0;
	count_objects(&dev_priv->mm.pinned_list, mm_list);
	seq_printf(m, "  %u [%u] pinned objects, %zu [%zu] bytes\n",
		   count, mappable_count, size, mappable_size);

	size = count = mappable_size = mappable_count = 0;
	count_objects(&dev_priv->mm.inactive_list, mm_list);
	seq_printf(m, "  %u [%u] inactive objects, %zu [%zu] bytes\n",
		   count, mappable_count, size, mappable_size);

	size = count = mappable_size = mappable_count = 0;
	count_objects(&dev_priv->mm.deferred_free_list, mm_list);
	seq_printf(m, "  %u [%u] freed objects, %zu [%zu] bytes\n",
		   count, mappable_count, size, mappable_size);

	size = count = mappable_size = mappable_count = 0;
	list_for_each_entry(obj, &dev_priv->mm.gtt_list, gtt_list) {
		if (obj->fault_mappable) {
			size += obj->gtt_space->size;
			++count;
		}
		if (obj->pin_mappable) {
			mappable_size += obj->gtt_space->size;
			++mappable_count;
		}
	}
	seq_printf(m, "%u pinned mappable objects, %zu bytes\n",
		   mappable_count, mappable_size);
	seq_printf(m, "%u fault mappable objects, %zu bytes\n",
		   count, size);

	seq_printf(m, "%zu [%zu] gtt total\n",
		   dev_priv->mm.gtt_total, dev_priv->mm.mappable_gtt_total);

	mutex_unlock(&dev->struct_mutex);

	return 0;
}

static int i915_gem_gtt_info(struct seq_file *m, void* data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj;
	size_t total_obj_size, total_gtt_size;
	int count, ret;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	total_obj_size = total_gtt_size = count = 0;
	list_for_each_entry(obj, &dev_priv->mm.gtt_list, gtt_list) {
		seq_printf(m, "   ");
		describe_obj(m, obj);
		seq_printf(m, "\n");
		total_obj_size += obj->base.size;
		total_gtt_size += obj->gtt_space->size;
		count++;
	}

	mutex_unlock(&dev->struct_mutex);

	seq_printf(m, "Total %d objects, %zu bytes, %zu GTT size\n",
		   count, total_obj_size, total_gtt_size);

	return 0;
}


static int i915_gem_pageflip_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	unsigned long flags;
	struct intel_crtc *crtc;

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, base.head) {
		const char pipe = pipe_name(crtc->pipe);
		const char plane = plane_name(crtc->plane);
		struct intel_unpin_work *work;

		spin_lock_irqsave(&dev->event_lock, flags);
		work = crtc->unpin_work;
		if (work == NULL) {
			seq_printf(m, "No flip due on pipe %c (plane %c)\n",
				   pipe, plane);
		} else {
			if (!work->pending) {
				seq_printf(m, "Flip queued on pipe %c (plane %c)\n",
					   pipe, plane);
			} else {
				seq_printf(m, "Flip pending (waiting for vsync) on pipe %c (plane %c)\n",
					   pipe, plane);
			}
			if (work->enable_stall_check)
				seq_printf(m, "Stall check enabled, ");
			else
				seq_printf(m, "Stall check waiting for page flip ioctl, ");
			seq_printf(m, "%d prepares\n", work->pending);

			if (work->old_fb_obj) {
				struct drm_i915_gem_object *obj = work->old_fb_obj;
				if (obj)
					seq_printf(m, "Old framebuffer gtt_offset 0x%08x\n", obj->gtt_offset);
			}
			if (work->pending_flip_obj) {
				struct drm_i915_gem_object *obj = work->pending_flip_obj;
				if (obj)
					seq_printf(m, "New framebuffer gtt_offset 0x%08x\n", obj->gtt_offset);
			}
		}
		spin_unlock_irqrestore(&dev->event_lock, flags);
	}

	return 0;
}

static int i915_gem_request_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_gem_request *gem_request;
	int ret, count;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	count = 0;
	if (!list_empty(&dev_priv->ring[RCS].request_list)) {
		seq_printf(m, "Render requests:\n");
		list_for_each_entry(gem_request,
				    &dev_priv->ring[RCS].request_list,
				    list) {
			seq_printf(m, "    %d @ %d\n",
				   gem_request->seqno,
				   (int) (jiffies - gem_request->emitted_jiffies));
		}
		count++;
	}
	if (!list_empty(&dev_priv->ring[VCS].request_list)) {
		seq_printf(m, "BSD requests:\n");
		list_for_each_entry(gem_request,
				    &dev_priv->ring[VCS].request_list,
				    list) {
			seq_printf(m, "    %d @ %d\n",
				   gem_request->seqno,
				   (int) (jiffies - gem_request->emitted_jiffies));
		}
		count++;
	}
	if (!list_empty(&dev_priv->ring[BCS].request_list)) {
		seq_printf(m, "BLT requests:\n");
		list_for_each_entry(gem_request,
				    &dev_priv->ring[BCS].request_list,
				    list) {
			seq_printf(m, "    %d @ %d\n",
				   gem_request->seqno,
				   (int) (jiffies - gem_request->emitted_jiffies));
		}
		count++;
	}
	mutex_unlock(&dev->struct_mutex);

	if (count == 0)
		seq_printf(m, "No requests\n");

	return 0;
}

static void i915_ring_seqno_info(struct seq_file *m,
				 struct intel_ring_buffer *ring)
{
	if (ring->get_seqno) {
		seq_printf(m, "Current sequence (%s): %d\n",
			   ring->name, ring->get_seqno(ring));
		seq_printf(m, "Waiter sequence (%s):  %d\n",
			   ring->name, ring->waiting_seqno);
		seq_printf(m, "IRQ sequence (%s):     %d\n",
			   ring->name, ring->irq_seqno);
	}
}

static int i915_gem_seqno_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	int ret, i;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	for (i = 0; i < I915_NUM_RINGS; i++)
		i915_ring_seqno_info(m, &dev_priv->ring[i]);

	mutex_unlock(&dev->struct_mutex);

	return 0;
}


static int i915_interrupt_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	int ret, i, pipe;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	if (!HAS_PCH_SPLIT(dev)) {
		seq_printf(m, "Interrupt enable:    %08x\n",
			   I915_READ(IER));
		seq_printf(m, "Interrupt identity:  %08x\n",
			   I915_READ(IIR));
		seq_printf(m, "Interrupt mask:      %08x\n",
			   I915_READ(IMR));
		for_each_pipe(pipe)
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
	seq_printf(m, "Interrupts received: %d\n",
		   atomic_read(&dev_priv->irq_received));
	for (i = 0; i < I915_NUM_RINGS; i++) {
		if (IS_GEN6(dev) || IS_GEN7(dev)) {
			seq_printf(m, "Graphics Interrupt mask (%s):	%08x\n",
				   dev_priv->ring[i].name,
				   I915_READ_IMR(&dev_priv->ring[i]));
		}
		i915_ring_seqno_info(m, &dev_priv->ring[i]);
	}
	mutex_unlock(&dev->struct_mutex);

	return 0;
}

static int i915_gem_fence_regs_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	int i, ret;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	seq_printf(m, "Reserved fences = %d\n", dev_priv->fence_reg_start);
	seq_printf(m, "Total fences = %d\n", dev_priv->num_fence_regs);
	for (i = 0; i < dev_priv->num_fence_regs; i++) {
		struct drm_i915_gem_object *obj = dev_priv->fence_regs[i].obj;

		seq_printf(m, "Fenced object[%2d] = ", i);
		if (obj == NULL)
			seq_printf(m, "unused");
		else
			describe_obj(m, obj);
		seq_printf(m, "\n");
	}

	mutex_unlock(&dev->struct_mutex);
	return 0;
}

static int i915_hws_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct intel_ring_buffer *ring;
	const volatile u32 __iomem *hws;
	int i;

	ring = &dev_priv->ring[(uintptr_t)node->info_ent->data];
	hws = (volatile u32 __iomem *)ring->status_page.page_addr;
	if (hws == NULL)
		return 0;

	for (i = 0; i < 4096 / sizeof(u32) / 4; i += 4) {
		seq_printf(m, "0x%08x: 0x%08x 0x%08x 0x%08x 0x%08x\n",
			   i * 4,
			   hws[i], hws[i + 1], hws[i + 2], hws[i + 3]);
	}
	return 0;
}

static void i915_dump_object(struct seq_file *m,
			     struct io_mapping *mapping,
			     struct drm_i915_gem_object *obj)
{
	int page, page_count, i;

	page_count = obj->base.size / PAGE_SIZE;
	for (page = 0; page < page_count; page++) {
		u32 *mem = io_mapping_map_wc(mapping,
					     obj->gtt_offset + page * PAGE_SIZE);
		for (i = 0; i < PAGE_SIZE; i += 4)
			seq_printf(m, "%08x :  %08x\n", i, mem[i / 4]);
		io_mapping_unmap(mem);
	}
}

static int i915_batchbuffer_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj;
	int ret;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	list_for_each_entry(obj, &dev_priv->mm.active_list, mm_list) {
		if (obj->base.read_domains & I915_GEM_DOMAIN_COMMAND) {
		    seq_printf(m, "--- gtt_offset = 0x%08x\n", obj->gtt_offset);
		    i915_dump_object(m, dev_priv->mm.gtt_mapping, obj);
		}
	}

	mutex_unlock(&dev->struct_mutex);
	return 0;
}

static int i915_ringbuffer_data(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct intel_ring_buffer *ring;
	int ret;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	ring = &dev_priv->ring[(uintptr_t)node->info_ent->data];
	if (!ring->obj) {
		seq_printf(m, "No ringbuffer setup\n");
	} else {
		const u8 __iomem *virt = ring->virtual_start;
		uint32_t off;

		for (off = 0; off < ring->size; off += 4) {
			uint32_t *ptr = (uint32_t *)(virt + off);
			seq_printf(m, "%08x :  %08x\n", off, *ptr);
		}
	}
	mutex_unlock(&dev->struct_mutex);

	return 0;
}

static int i915_ringbuffer_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct intel_ring_buffer *ring;

	ring = &dev_priv->ring[(uintptr_t)node->info_ent->data];
	if (ring->size == 0)
		return 0;

	seq_printf(m, "Ring %s:\n", ring->name);
	seq_printf(m, "  Head :    %08x\n", I915_READ_HEAD(ring) & HEAD_ADDR);
	seq_printf(m, "  Tail :    %08x\n", I915_READ_TAIL(ring) & TAIL_ADDR);
	seq_printf(m, "  Size :    %08x\n", ring->size);
	seq_printf(m, "  Active :  %08x\n", intel_ring_get_active_head(ring));
	seq_printf(m, "  NOPID :   %08x\n", I915_READ_NOPID(ring));
	if (IS_GEN6(dev)) {
		seq_printf(m, "  Sync 0 :   %08x\n", I915_READ_SYNC_0(ring));
		seq_printf(m, "  Sync 1 :   %08x\n", I915_READ_SYNC_1(ring));
	}
	seq_printf(m, "  Control : %08x\n", I915_READ_CTL(ring));
	seq_printf(m, "  Start :   %08x\n", I915_READ_START(ring));

	return 0;
}

static const char *ring_str(int ring)
{
	switch (ring) {
	case RING_RENDER: return " render";
	case RING_BSD: return " bsd";
	case RING_BLT: return " blt";
	default: return "";
	}
}

static const char *pin_flag(int pinned)
{
	if (pinned > 0)
		return " P";
	else if (pinned < 0)
		return " p";
	else
		return "";
}

static const char *tiling_flag(int tiling)
{
	switch (tiling) {
	default:
	case I915_TILING_NONE: return "";
	case I915_TILING_X: return " X";
	case I915_TILING_Y: return " Y";
	}
}

static const char *dirty_flag(int dirty)
{
	return dirty ? " dirty" : "";
}

static const char *purgeable_flag(int purgeable)
{
	return purgeable ? " purgeable" : "";
}

static void print_error_buffers(struct seq_file *m,
				const char *name,
				struct drm_i915_error_buffer *err,
				int count)
{
	seq_printf(m, "%s [%d]:\n", name, count);

	while (count--) {
		seq_printf(m, "  %08x %8u %04x %04x %08x%s%s%s%s%s%s",
			   err->gtt_offset,
			   err->size,
			   err->read_domains,
			   err->write_domain,
			   err->seqno,
			   pin_flag(err->pinned),
			   tiling_flag(err->tiling),
			   dirty_flag(err->dirty),
			   purgeable_flag(err->purgeable),
			   ring_str(err->ring),
			   cache_level_str(err->cache_level));

		if (err->name)
			seq_printf(m, " (name: %d)", err->name);
		if (err->fence_reg != I915_FENCE_REG_NONE)
			seq_printf(m, " (fence: %d)", err->fence_reg);

		seq_printf(m, "\n");
		err++;
	}
}

static int i915_error_state(struct seq_file *m, void *unused)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_error_state *error;
	unsigned long flags;
	int i, page, offset, elt;

	spin_lock_irqsave(&dev_priv->error_lock, flags);
	if (!dev_priv->first_error) {
		seq_printf(m, "no error state collected\n");
		goto out;
	}

	error = dev_priv->first_error;

	seq_printf(m, "Time: %ld s %ld us\n", error->time.tv_sec,
		   error->time.tv_usec);
	seq_printf(m, "PCI ID: 0x%04x\n", dev->pci_device);
	seq_printf(m, "EIR: 0x%08x\n", error->eir);
	seq_printf(m, "PGTBL_ER: 0x%08x\n", error->pgtbl_er);
	if (INTEL_INFO(dev)->gen >= 6) {
		seq_printf(m, "ERROR: 0x%08x\n", error->error);
		seq_printf(m, "Blitter command stream:\n");
		seq_printf(m, "  ACTHD:    0x%08x\n", error->bcs_acthd);
		seq_printf(m, "  IPEIR:    0x%08x\n", error->bcs_ipeir);
		seq_printf(m, "  IPEHR:    0x%08x\n", error->bcs_ipehr);
		seq_printf(m, "  INSTDONE: 0x%08x\n", error->bcs_instdone);
		seq_printf(m, "  seqno:    0x%08x\n", error->bcs_seqno);
		seq_printf(m, "Video (BSD) command stream:\n");
		seq_printf(m, "  ACTHD:    0x%08x\n", error->vcs_acthd);
		seq_printf(m, "  IPEIR:    0x%08x\n", error->vcs_ipeir);
		seq_printf(m, "  IPEHR:    0x%08x\n", error->vcs_ipehr);
		seq_printf(m, "  INSTDONE: 0x%08x\n", error->vcs_instdone);
		seq_printf(m, "  seqno:    0x%08x\n", error->vcs_seqno);
	}
	seq_printf(m, "Render command stream:\n");
	seq_printf(m, "  ACTHD: 0x%08x\n", error->acthd);
	seq_printf(m, "  IPEIR: 0x%08x\n", error->ipeir);
	seq_printf(m, "  IPEHR: 0x%08x\n", error->ipehr);
	seq_printf(m, "  INSTDONE: 0x%08x\n", error->instdone);
	if (INTEL_INFO(dev)->gen >= 4) {
		seq_printf(m, "  INSTDONE1: 0x%08x\n", error->instdone1);
		seq_printf(m, "  INSTPS: 0x%08x\n", error->instps);
	}
	seq_printf(m, "  INSTPM: 0x%08x\n", error->instpm);
	seq_printf(m, "  seqno: 0x%08x\n", error->seqno);

	for (i = 0; i < dev_priv->num_fence_regs; i++)
		seq_printf(m, "  fence[%d] = %08llx\n", i, error->fence[i]);

	if (error->active_bo)
		print_error_buffers(m, "Active",
				    error->active_bo,
				    error->active_bo_count);

	if (error->pinned_bo)
		print_error_buffers(m, "Pinned",
				    error->pinned_bo,
				    error->pinned_bo_count);

	for (i = 0; i < ARRAY_SIZE(error->batchbuffer); i++) {
		if (error->batchbuffer[i]) {
			struct drm_i915_error_object *obj = error->batchbuffer[i];

			seq_printf(m, "%s --- gtt_offset = 0x%08x\n",
				   dev_priv->ring[i].name,
				   obj->gtt_offset);
			offset = 0;
			for (page = 0; page < obj->page_count; page++) {
				for (elt = 0; elt < PAGE_SIZE/4; elt++) {
					seq_printf(m, "%08x :  %08x\n", offset, obj->pages[page][elt]);
					offset += 4;
				}
			}
		}
	}

	for (i = 0; i < ARRAY_SIZE(error->ringbuffer); i++) {
		if (error->ringbuffer[i]) {
			struct drm_i915_error_object *obj = error->ringbuffer[i];
			seq_printf(m, "%s --- ringbuffer = 0x%08x\n",
				   dev_priv->ring[i].name,
				   obj->gtt_offset);
			offset = 0;
			for (page = 0; page < obj->page_count; page++) {
				for (elt = 0; elt < PAGE_SIZE/4; elt++) {
					seq_printf(m, "%08x :  %08x\n",
						   offset,
						   obj->pages[page][elt]);
					offset += 4;
				}
			}
		}
	}

	if (error->overlay)
		intel_overlay_print_error_state(m, error->overlay);

	if (error->display)
		intel_display_print_error_state(m, dev, error->display);

out:
	spin_unlock_irqrestore(&dev_priv->error_lock, flags);

	return 0;
}

static int i915_rstdby_delays(struct seq_file *m, void *unused)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	u16 crstanddelay = I915_READ16(CRSTANDVID);

	seq_printf(m, "w/ctx: %d, w/o ctx: %d\n", (crstanddelay >> 8) & 0x3f, (crstanddelay & 0x3f));

	return 0;
}

static int i915_cur_delayinfo(struct seq_file *m, void *unused)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	int ret;

	if (IS_GEN5(dev)) {
		u16 rgvswctl = I915_READ16(MEMSWCTL);
		u16 rgvstat = I915_READ16(MEMSTAT_ILK);

		seq_printf(m, "Requested P-state: %d\n", (rgvswctl >> 8) & 0xf);
		seq_printf(m, "Requested VID: %d\n", rgvswctl & 0x3f);
		seq_printf(m, "Current VID: %d\n", (rgvstat & MEMSTAT_VID_MASK) >>
			   MEMSTAT_VID_SHIFT);
		seq_printf(m, "Current P-state: %d\n",
			   (rgvstat & MEMSTAT_PSTATE_MASK) >> MEMSTAT_PSTATE_SHIFT);
	} else if (IS_GEN6(dev) || IS_GEN7(dev)) {
		u32 gt_perf_status = I915_READ(GEN6_GT_PERF_STATUS);
		u32 rp_state_limits = I915_READ(GEN6_RP_STATE_LIMITS);
		u32 rp_state_cap = I915_READ(GEN6_RP_STATE_CAP);
		u32 rpstat;
		u32 rpupei, rpcurup, rpprevup;
		u32 rpdownei, rpcurdown, rpprevdown;
		int max_freq;

		/* RPSTAT1 is in the GT power well */
		ret = mutex_lock_interruptible(&dev->struct_mutex);
		if (ret)
			return ret;

		gen6_gt_force_wake_get(dev_priv);

		rpstat = I915_READ(GEN6_RPSTAT1);
		rpupei = I915_READ(GEN6_RP_CUR_UP_EI);
		rpcurup = I915_READ(GEN6_RP_CUR_UP);
		rpprevup = I915_READ(GEN6_RP_PREV_UP);
		rpdownei = I915_READ(GEN6_RP_CUR_DOWN_EI);
		rpcurdown = I915_READ(GEN6_RP_CUR_DOWN);
		rpprevdown = I915_READ(GEN6_RP_PREV_DOWN);

		gen6_gt_force_wake_put(dev_priv);
		mutex_unlock(&dev->struct_mutex);

		seq_printf(m, "GT_PERF_STATUS: 0x%08x\n", gt_perf_status);
		seq_printf(m, "RPSTAT1: 0x%08x\n", rpstat);
		seq_printf(m, "Render p-state ratio: %d\n",
			   (gt_perf_status & 0xff00) >> 8);
		seq_printf(m, "Render p-state VID: %d\n",
			   gt_perf_status & 0xff);
		seq_printf(m, "Render p-state limit: %d\n",
			   rp_state_limits & 0xff);
		seq_printf(m, "CAGF: %dMHz\n", ((rpstat & GEN6_CAGF_MASK) >>
						GEN6_CAGF_SHIFT) * 50);
		seq_printf(m, "RP CUR UP EI: %dus\n", rpupei &
			   GEN6_CURICONT_MASK);
		seq_printf(m, "RP CUR UP: %dus\n", rpcurup &
			   GEN6_CURBSYTAVG_MASK);
		seq_printf(m, "RP PREV UP: %dus\n", rpprevup &
			   GEN6_CURBSYTAVG_MASK);
		seq_printf(m, "RP CUR DOWN EI: %dus\n", rpdownei &
			   GEN6_CURIAVG_MASK);
		seq_printf(m, "RP CUR DOWN: %dus\n", rpcurdown &
			   GEN6_CURBSYTAVG_MASK);
		seq_printf(m, "RP PREV DOWN: %dus\n", rpprevdown &
			   GEN6_CURBSYTAVG_MASK);

		max_freq = (rp_state_cap & 0xff0000) >> 16;
		seq_printf(m, "Lowest (RPN) frequency: %dMHz\n",
			   max_freq * 50);

		max_freq = (rp_state_cap & 0xff00) >> 8;
		seq_printf(m, "Nominal (RP1) frequency: %dMHz\n",
			   max_freq * 50);

		max_freq = rp_state_cap & 0xff;
		seq_printf(m, "Max non-overclocked (RP0) frequency: %dMHz\n",
			   max_freq * 50);
	} else {
		seq_printf(m, "no P-state info available\n");
	}

	return 0;
}

static int i915_delayfreq_table(struct seq_file *m, void *unused)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	u32 delayfreq;
	int i;

	for (i = 0; i < 16; i++) {
		delayfreq = I915_READ(PXVFREQ_BASE + i * 4);
		seq_printf(m, "P%02dVIDFREQ: 0x%08x (VID: %d)\n", i, delayfreq,
			   (delayfreq & PXVFREQ_PX_MASK) >> PXVFREQ_PX_SHIFT);
	}

	return 0;
}

static inline int MAP_TO_MV(int map)
{
	return 1250 - (map * 25);
}

static int i915_inttoext_table(struct seq_file *m, void *unused)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	u32 inttoext;
	int i;

	for (i = 1; i <= 32; i++) {
		inttoext = I915_READ(INTTOEXT_BASE_ILK + i * 4);
		seq_printf(m, "INTTOEXT%02d: 0x%08x\n", i, inttoext);
	}

	return 0;
}

static int i915_drpc_info(struct seq_file *m, void *unused)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	u32 rgvmodectl = I915_READ(MEMMODECTL);
	u32 rstdbyctl = I915_READ(RSTDBYCTL);
	u16 crstandvid = I915_READ16(CRSTANDVID);

	seq_printf(m, "HD boost: %s\n", (rgvmodectl & MEMMODE_BOOST_EN) ?
		   "yes" : "no");
	seq_printf(m, "Boost freq: %d\n",
		   (rgvmodectl & MEMMODE_BOOST_FREQ_MASK) >>
		   MEMMODE_BOOST_FREQ_SHIFT);
	seq_printf(m, "HW control enabled: %s\n",
		   rgvmodectl & MEMMODE_HWIDLE_EN ? "yes" : "no");
	seq_printf(m, "SW control enabled: %s\n",
		   rgvmodectl & MEMMODE_SWMODE_EN ? "yes" : "no");
	seq_printf(m, "Gated voltage change: %s\n",
		   rgvmodectl & MEMMODE_RCLK_GATE ? "yes" : "no");
	seq_printf(m, "Starting frequency: P%d\n",
		   (rgvmodectl & MEMMODE_FSTART_MASK) >> MEMMODE_FSTART_SHIFT);
	seq_printf(m, "Max P-state: P%d\n",
		   (rgvmodectl & MEMMODE_FMAX_MASK) >> MEMMODE_FMAX_SHIFT);
	seq_printf(m, "Min P-state: P%d\n", (rgvmodectl & MEMMODE_FMIN_MASK));
	seq_printf(m, "RS1 VID: %d\n", (crstandvid & 0x3f));
	seq_printf(m, "RS2 VID: %d\n", ((crstandvid >> 8) & 0x3f));
	seq_printf(m, "Render standby enabled: %s\n",
		   (rstdbyctl & RCX_SW_EXIT) ? "no" : "yes");
	seq_printf(m, "Current RS state: ");
	switch (rstdbyctl & RSX_STATUS_MASK) {
	case RSX_STATUS_ON:
		seq_printf(m, "on\n");
		break;
	case RSX_STATUS_RC1:
		seq_printf(m, "RC1\n");
		break;
	case RSX_STATUS_RC1E:
		seq_printf(m, "RC1E\n");
		break;
	case RSX_STATUS_RS1:
		seq_printf(m, "RS1\n");
		break;
	case RSX_STATUS_RS2:
		seq_printf(m, "RS2 (RC6)\n");
		break;
	case RSX_STATUS_RS3:
		seq_printf(m, "RC3 (RC6+)\n");
		break;
	default:
		seq_printf(m, "unknown\n");
		break;
	}

	return 0;
}

static int i915_fbc_status(struct seq_file *m, void *unused)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;

	if (!I915_HAS_FBC(dev)) {
		seq_printf(m, "FBC unsupported on this chipset\n");
		return 0;
	}

	if (intel_fbc_enabled(dev)) {
		seq_printf(m, "FBC enabled\n");
	} else {
		seq_printf(m, "FBC disabled: ");
		switch (dev_priv->no_fbc_reason) {
		case FBC_NO_OUTPUT:
			seq_printf(m, "no outputs");
			break;
		case FBC_STOLEN_TOO_SMALL:
			seq_printf(m, "not enough stolen memory");
			break;
		case FBC_UNSUPPORTED_MODE:
			seq_printf(m, "mode not supported");
			break;
		case FBC_MODE_TOO_LARGE:
			seq_printf(m, "mode too large");
			break;
		case FBC_BAD_PLANE:
			seq_printf(m, "FBC unsupported on plane");
			break;
		case FBC_NOT_TILED:
			seq_printf(m, "scanout buffer not tiled");
			break;
		case FBC_MULTIPLE_PIPES:
			seq_printf(m, "multiple pipes are enabled");
			break;
		case FBC_MODULE_PARAM:
			seq_printf(m, "disabled per module param (default off)");
			break;
		default:
			seq_printf(m, "unknown reason");
		}
		seq_printf(m, "\n");
	}
	return 0;
}

static int i915_sr_status(struct seq_file *m, void *unused)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	bool sr_enabled = false;

	if (HAS_PCH_SPLIT(dev))
		sr_enabled = I915_READ(WM1_LP_ILK) & WM1_LP_SR_EN;
	else if (IS_CRESTLINE(dev) || IS_I945G(dev) || IS_I945GM(dev))
		sr_enabled = I915_READ(FW_BLC_SELF) & FW_BLC_SELF_EN;
	else if (IS_I915GM(dev))
		sr_enabled = I915_READ(INSTPM) & INSTPM_SELF_EN;
	else if (IS_PINEVIEW(dev))
		sr_enabled = I915_READ(DSPFW3) & PINEVIEW_SELF_REFRESH_EN;

	seq_printf(m, "self-refresh: %s\n",
		   sr_enabled ? "enabled" : "disabled");

	return 0;
}

static int i915_emon_status(struct seq_file *m, void *unused)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	unsigned long temp, chipset, gfx;
	int ret;

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
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	int ret;
	int gpu_freq, ia_freq;

	if (!(IS_GEN6(dev) || IS_GEN7(dev))) {
		seq_printf(m, "unsupported on this chipset\n");
		return 0;
	}

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	seq_printf(m, "GPU freq (MHz)\tEffective CPU freq (MHz)\n");

	for (gpu_freq = dev_priv->min_delay; gpu_freq <= dev_priv->max_delay;
	     gpu_freq++) {
		I915_WRITE(GEN6_PCODE_DATA, gpu_freq);
		I915_WRITE(GEN6_PCODE_MAILBOX, GEN6_PCODE_READY |
			   GEN6_PCODE_READ_MIN_FREQ_TABLE);
		if (wait_for((I915_READ(GEN6_PCODE_MAILBOX) &
			      GEN6_PCODE_READY) == 0, 10)) {
			DRM_ERROR("pcode read of freq table timed out\n");
			continue;
		}
		ia_freq = I915_READ(GEN6_PCODE_DATA);
		seq_printf(m, "%d\t\t%d\n", gpu_freq * 50, ia_freq * 100);
	}

	mutex_unlock(&dev->struct_mutex);

	return 0;
}

static int i915_gfxec(struct seq_file *m, void *unused)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;

	seq_printf(m, "GFXEC: %ld\n", (unsigned long)I915_READ(0x112f4));

	return 0;
}

static int i915_opregion(struct seq_file *m, void *unused)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct intel_opregion *opregion = &dev_priv->opregion;
	int ret;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;

	if (opregion->header)
		seq_write(m, opregion->header, OPREGION_SIZE);

	mutex_unlock(&dev->struct_mutex);

	return 0;
}

static int i915_gem_framebuffer_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct intel_fbdev *ifbdev;
	struct intel_framebuffer *fb;
	int ret;

	ret = mutex_lock_interruptible(&dev->mode_config.mutex);
	if (ret)
		return ret;

	ifbdev = dev_priv->fbdev;
	fb = to_intel_framebuffer(ifbdev->helper.fb);

	seq_printf(m, "fbcon size: %d x %d, depth %d, %d bpp, obj ",
		   fb->base.width,
		   fb->base.height,
		   fb->base.depth,
		   fb->base.bits_per_pixel);
	describe_obj(m, fb->obj);
	seq_printf(m, "\n");

	list_for_each_entry(fb, &dev->mode_config.fb_list, base.head) {
		if (&fb->base == ifbdev->helper.fb)
			continue;

		seq_printf(m, "user size: %d x %d, depth %d, %d bpp, obj ",
			   fb->base.width,
			   fb->base.height,
			   fb->base.depth,
			   fb->base.bits_per_pixel);
		describe_obj(m, fb->obj);
		seq_printf(m, "\n");
	}

	mutex_unlock(&dev->mode_config.mutex);

	return 0;
}

static int i915_context_status(struct seq_file *m, void *unused)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	int ret;

	ret = mutex_lock_interruptible(&dev->mode_config.mutex);
	if (ret)
		return ret;

	if (dev_priv->pwrctx) {
		seq_printf(m, "power context ");
		describe_obj(m, dev_priv->pwrctx);
		seq_printf(m, "\n");
	}

	if (dev_priv->renderctx) {
		seq_printf(m, "render context ");
		describe_obj(m, dev_priv->renderctx);
		seq_printf(m, "\n");
	}

	mutex_unlock(&dev->mode_config.mutex);

	return 0;
}

static int i915_gen6_forcewake_count_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	seq_printf(m, "forcewake count = %d\n",
		   atomic_read(&dev_priv->forcewake_count));

	return 0;
}

static int
i915_wedged_open(struct inode *inode,
		 struct file *filp)
{
	filp->private_data = inode->i_private;
	return 0;
}

static ssize_t
i915_wedged_read(struct file *filp,
		 char __user *ubuf,
		 size_t max,
		 loff_t *ppos)
{
	struct drm_device *dev = filp->private_data;
	drm_i915_private_t *dev_priv = dev->dev_private;
	char buf[80];
	int len;

	len = snprintf(buf, sizeof(buf),
		       "wedged :  %d\n",
		       atomic_read(&dev_priv->mm.wedged));

	if (len > sizeof(buf))
		len = sizeof(buf);

	return simple_read_from_buffer(ubuf, max, ppos, buf, len);
}

static ssize_t
i915_wedged_write(struct file *filp,
		  const char __user *ubuf,
		  size_t cnt,
		  loff_t *ppos)
{
	struct drm_device *dev = filp->private_data;
	char buf[20];
	int val = 1;

	if (cnt > 0) {
		if (cnt > sizeof(buf) - 1)
			return -EINVAL;

		if (copy_from_user(buf, ubuf, cnt))
			return -EFAULT;
		buf[cnt] = 0;

		val = simple_strtoul(buf, NULL, 0);
	}

	DRM_INFO("Manually setting wedged to %d\n", val);
	i915_handle_error(dev, val);

	return cnt;
}

static const struct file_operations i915_wedged_fops = {
	.owner = THIS_MODULE,
	.open = i915_wedged_open,
	.read = i915_wedged_read,
	.write = i915_wedged_write,
	.llseek = default_llseek,
};

static int
i915_max_freq_open(struct inode *inode,
		   struct file *filp)
{
	filp->private_data = inode->i_private;
	return 0;
}

static ssize_t
i915_max_freq_read(struct file *filp,
		   char __user *ubuf,
		   size_t max,
		   loff_t *ppos)
{
	struct drm_device *dev = filp->private_data;
	drm_i915_private_t *dev_priv = dev->dev_private;
	char buf[80];
	int len;

	len = snprintf(buf, sizeof(buf),
		       "max freq: %d\n", dev_priv->max_delay * 50);

	if (len > sizeof(buf))
		len = sizeof(buf);

	return simple_read_from_buffer(ubuf, max, ppos, buf, len);
}

static ssize_t
i915_max_freq_write(struct file *filp,
		  const char __user *ubuf,
		  size_t cnt,
		  loff_t *ppos)
{
	struct drm_device *dev = filp->private_data;
	struct drm_i915_private *dev_priv = dev->dev_private;
	char buf[20];
	int val = 1;

	if (cnt > 0) {
		if (cnt > sizeof(buf) - 1)
			return -EINVAL;

		if (copy_from_user(buf, ubuf, cnt))
			return -EFAULT;
		buf[cnt] = 0;

		val = simple_strtoul(buf, NULL, 0);
	}

	DRM_DEBUG_DRIVER("Manually setting max freq to %d\n", val);

	/*
	 * Turbo will still be enabled, but won't go above the set value.
	 */
	dev_priv->max_delay = val / 50;

	gen6_set_rps(dev, val / 50);

	return cnt;
}

static const struct file_operations i915_max_freq_fops = {
	.owner = THIS_MODULE,
	.open = i915_max_freq_open,
	.read = i915_max_freq_read,
	.write = i915_max_freq_write,
	.llseek = default_llseek,
};

static int
i915_cache_sharing_open(struct inode *inode,
		   struct file *filp)
{
	filp->private_data = inode->i_private;
	return 0;
}

static ssize_t
i915_cache_sharing_read(struct file *filp,
		   char __user *ubuf,
		   size_t max,
		   loff_t *ppos)
{
	struct drm_device *dev = filp->private_data;
	drm_i915_private_t *dev_priv = dev->dev_private;
	char buf[80];
	u32 snpcr;
	int len;

	mutex_lock(&dev_priv->dev->struct_mutex);
	snpcr = I915_READ(GEN6_MBCUNIT_SNPCR);
	mutex_unlock(&dev_priv->dev->struct_mutex);

	len = snprintf(buf, sizeof(buf),
		       "%d\n", (snpcr & GEN6_MBC_SNPCR_MASK) >>
		       GEN6_MBC_SNPCR_SHIFT);

	if (len > sizeof(buf))
		len = sizeof(buf);

	return simple_read_from_buffer(ubuf, max, ppos, buf, len);
}

static ssize_t
i915_cache_sharing_write(struct file *filp,
		  const char __user *ubuf,
		  size_t cnt,
		  loff_t *ppos)
{
	struct drm_device *dev = filp->private_data;
	struct drm_i915_private *dev_priv = dev->dev_private;
	char buf[20];
	u32 snpcr;
	int val = 1;

	if (cnt > 0) {
		if (cnt > sizeof(buf) - 1)
			return -EINVAL;

		if (copy_from_user(buf, ubuf, cnt))
			return -EFAULT;
		buf[cnt] = 0;

		val = simple_strtoul(buf, NULL, 0);
	}

	if (val < 0 || val > 3)
		return -EINVAL;

	DRM_DEBUG_DRIVER("Manually setting uncore sharing to %d\n", val);

	/* Update the cache sharing policy here as well */
	snpcr = I915_READ(GEN6_MBCUNIT_SNPCR);
	snpcr &= ~GEN6_MBC_SNPCR_MASK;
	snpcr |= (val << GEN6_MBC_SNPCR_SHIFT);
	I915_WRITE(GEN6_MBCUNIT_SNPCR, snpcr);

	return cnt;
}

static const struct file_operations i915_cache_sharing_fops = {
	.owner = THIS_MODULE,
	.open = i915_cache_sharing_open,
	.read = i915_cache_sharing_read,
	.write = i915_cache_sharing_write,
	.llseek = default_llseek,
};

/* As the drm_debugfs_init() routines are called before dev->dev_private is
 * allocated we need to hook into the minor for release. */
static int
drm_add_fake_info_node(struct drm_minor *minor,
		       struct dentry *ent,
		       const void *key)
{
	struct drm_info_node *node;

	node = kmalloc(sizeof(struct drm_info_node), GFP_KERNEL);
	if (node == NULL) {
		debugfs_remove(ent);
		return -ENOMEM;
	}

	node->minor = minor;
	node->dent = ent;
	node->info_ent = (void *) key;
	list_add(&node->list, &minor->debugfs_nodes.list);

	return 0;
}

static int i915_wedged_create(struct dentry *root, struct drm_minor *minor)
{
	struct drm_device *dev = minor->dev;
	struct dentry *ent;

	ent = debugfs_create_file("i915_wedged",
				  S_IRUGO | S_IWUSR,
				  root, dev,
				  &i915_wedged_fops);
	if (IS_ERR(ent))
		return PTR_ERR(ent);

	return drm_add_fake_info_node(minor, ent, &i915_wedged_fops);
}

static int i915_forcewake_open(struct inode *inode, struct file *file)
{
	struct drm_device *dev = inode->i_private;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret;

	if (!IS_GEN6(dev))
		return 0;

	ret = mutex_lock_interruptible(&dev->struct_mutex);
	if (ret)
		return ret;
	gen6_gt_force_wake_get(dev_priv);
	mutex_unlock(&dev->struct_mutex);

	return 0;
}

int i915_forcewake_release(struct inode *inode, struct file *file)
{
	struct drm_device *dev = inode->i_private;
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (!IS_GEN6(dev))
		return 0;

	/*
	 * It's bad that we can potentially hang userspace if struct_mutex gets
	 * forever stuck.  However, if we cannot acquire this lock it means that
	 * almost certainly the driver has hung, is not unload-able. Therefore
	 * hanging here is probably a minor inconvenience not to be seen my
	 * almost every user.
	 */
	mutex_lock(&dev->struct_mutex);
	gen6_gt_force_wake_put(dev_priv);
	mutex_unlock(&dev->struct_mutex);

	return 0;
}

static const struct file_operations i915_forcewake_fops = {
	.owner = THIS_MODULE,
	.open = i915_forcewake_open,
	.release = i915_forcewake_release,
};

static int i915_forcewake_create(struct dentry *root, struct drm_minor *minor)
{
	struct drm_device *dev = minor->dev;
	struct dentry *ent;

	ent = debugfs_create_file("i915_forcewake_user",
				  S_IRUSR,
				  root, dev,
				  &i915_forcewake_fops);
	if (IS_ERR(ent))
		return PTR_ERR(ent);

	return drm_add_fake_info_node(minor, ent, &i915_forcewake_fops);
}

static int i915_max_freq_create(struct dentry *root, struct drm_minor *minor)
{
	struct drm_device *dev = minor->dev;
	struct dentry *ent;

	ent = debugfs_create_file("i915_max_freq",
				  S_IRUGO | S_IWUSR,
				  root, dev,
				  &i915_max_freq_fops);
	if (IS_ERR(ent))
		return PTR_ERR(ent);

	return drm_add_fake_info_node(minor, ent, &i915_max_freq_fops);
}

static int i915_cache_sharing_create(struct dentry *root, struct drm_minor *minor)
{
	struct drm_device *dev = minor->dev;
	struct dentry *ent;

	ent = debugfs_create_file("i915_cache_sharing",
				  S_IRUGO | S_IWUSR,
				  root, dev,
				  &i915_cache_sharing_fops);
	if (IS_ERR(ent))
		return PTR_ERR(ent);

	return drm_add_fake_info_node(minor, ent, &i915_cache_sharing_fops);
}

static struct drm_info_list i915_debugfs_list[] = {
	{"i915_capabilities", i915_capabilities, 0},
	{"i915_gem_objects", i915_gem_object_info, 0},
	{"i915_gem_gtt", i915_gem_gtt_info, 0},
	{"i915_gem_active", i915_gem_object_list_info, 0, (void *) ACTIVE_LIST},
	{"i915_gem_flushing", i915_gem_object_list_info, 0, (void *) FLUSHING_LIST},
	{"i915_gem_inactive", i915_gem_object_list_info, 0, (void *) INACTIVE_LIST},
	{"i915_gem_pinned", i915_gem_object_list_info, 0, (void *) PINNED_LIST},
	{"i915_gem_deferred_free", i915_gem_object_list_info, 0, (void *) DEFERRED_FREE_LIST},
	{"i915_gem_pageflip", i915_gem_pageflip_info, 0},
	{"i915_gem_request", i915_gem_request_info, 0},
	{"i915_gem_seqno", i915_gem_seqno_info, 0},
	{"i915_gem_fence_regs", i915_gem_fence_regs_info, 0},
	{"i915_gem_interrupt", i915_interrupt_info, 0},
	{"i915_gem_hws", i915_hws_info, 0, (void *)RCS},
	{"i915_gem_hws_blt", i915_hws_info, 0, (void *)BCS},
	{"i915_gem_hws_bsd", i915_hws_info, 0, (void *)VCS},
	{"i915_ringbuffer_data", i915_ringbuffer_data, 0, (void *)RCS},
	{"i915_ringbuffer_info", i915_ringbuffer_info, 0, (void *)RCS},
	{"i915_bsd_ringbuffer_data", i915_ringbuffer_data, 0, (void *)VCS},
	{"i915_bsd_ringbuffer_info", i915_ringbuffer_info, 0, (void *)VCS},
	{"i915_blt_ringbuffer_data", i915_ringbuffer_data, 0, (void *)BCS},
	{"i915_blt_ringbuffer_info", i915_ringbuffer_info, 0, (void *)BCS},
	{"i915_batchbuffers", i915_batchbuffer_info, 0},
	{"i915_error_state", i915_error_state, 0},
	{"i915_rstdby_delays", i915_rstdby_delays, 0},
	{"i915_cur_delayinfo", i915_cur_delayinfo, 0},
	{"i915_delayfreq_table", i915_delayfreq_table, 0},
	{"i915_inttoext_table", i915_inttoext_table, 0},
	{"i915_drpc_info", i915_drpc_info, 0},
	{"i915_emon_status", i915_emon_status, 0},
	{"i915_ring_freq_table", i915_ring_freq_table, 0},
	{"i915_gfxec", i915_gfxec, 0},
	{"i915_fbc_status", i915_fbc_status, 0},
	{"i915_sr_status", i915_sr_status, 0},
	{"i915_opregion", i915_opregion, 0},
	{"i915_gem_framebuffer", i915_gem_framebuffer_info, 0},
	{"i915_context_status", i915_context_status, 0},
	{"i915_gen6_forcewake_count", i915_gen6_forcewake_count_info, 0},
};
#define I915_DEBUGFS_ENTRIES ARRAY_SIZE(i915_debugfs_list)

int i915_debugfs_init(struct drm_minor *minor)
{
	int ret;

	ret = i915_wedged_create(minor->debugfs_root, minor);
	if (ret)
		return ret;

	ret = i915_forcewake_create(minor->debugfs_root, minor);
	if (ret)
		return ret;
	ret = i915_max_freq_create(minor->debugfs_root, minor);
	if (ret)
		return ret;
	ret = i915_cache_sharing_create(minor->debugfs_root, minor);
	if (ret)
		return ret;

	return drm_debugfs_create_files(i915_debugfs_list,
					I915_DEBUGFS_ENTRIES,
					minor->debugfs_root, minor);
}

void i915_debugfs_cleanup(struct drm_minor *minor)
{
	drm_debugfs_remove_files(i915_debugfs_list,
				 I915_DEBUGFS_ENTRIES, minor);
	drm_debugfs_remove_files((struct drm_info_list *) &i915_forcewake_fops,
				 1, minor);
	drm_debugfs_remove_files((struct drm_info_list *) &i915_wedged_fops,
				 1, minor);
	drm_debugfs_remove_files((struct drm_info_list *) &i915_max_freq_fops,
				 1, minor);
	drm_debugfs_remove_files((struct drm_info_list *) &i915_cache_sharing_fops,
				 1, minor);
}

#endif /* CONFIG_DEBUG_FS */
