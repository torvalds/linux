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
#include "drmP.h"
#include "drm.h"
#include "i915_drm.h"
#include "i915_drv.h"

#define DRM_I915_RING_DEBUG 1


#if defined(CONFIG_DEBUG_FS)

#define ACTIVE_LIST	1
#define FLUSHING_LIST	2
#define INACTIVE_LIST	3

static const char *get_pin_flag(struct drm_i915_gem_object *obj_priv)
{
	if (obj_priv->user_pin_count > 0)
		return "P";
	else if (obj_priv->pin_count > 0)
		return "p";
	else
		return " ";
}

static const char *get_tiling_flag(struct drm_i915_gem_object *obj_priv)
{
    switch (obj_priv->tiling_mode) {
    default:
    case I915_TILING_NONE: return " ";
    case I915_TILING_X: return "X";
    case I915_TILING_Y: return "Y";
    }
}

static int i915_gem_object_list_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	uintptr_t list = (uintptr_t) node->info_ent->data;
	struct list_head *head;
	struct drm_device *dev = node->minor->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj_priv;
	spinlock_t *lock = NULL;

	switch (list) {
	case ACTIVE_LIST:
		seq_printf(m, "Active:\n");
		lock = &dev_priv->mm.active_list_lock;
		head = &dev_priv->mm.active_list;
		break;
	case INACTIVE_LIST:
		seq_printf(m, "Inactive:\n");
		head = &dev_priv->mm.inactive_list;
		break;
	case FLUSHING_LIST:
		seq_printf(m, "Flushing:\n");
		head = &dev_priv->mm.flushing_list;
		break;
	default:
		DRM_INFO("Ooops, unexpected list\n");
		return 0;
	}

	if (lock)
		spin_lock(lock);
	list_for_each_entry(obj_priv, head, list)
	{
		struct drm_gem_object *obj = obj_priv->obj;

		seq_printf(m, "    %p: %s %8zd %08x %08x %d %s",
			   obj,
			   get_pin_flag(obj_priv),
			   obj->size,
			   obj->read_domains, obj->write_domain,
			   obj_priv->last_rendering_seqno,
			   obj_priv->dirty ? "dirty" : "");

		if (obj->name)
			seq_printf(m, " (name: %d)", obj->name);
		if (obj_priv->fence_reg != I915_FENCE_REG_NONE)
			seq_printf(m, " (fence: %d)", obj_priv->fence_reg);
		if (obj_priv->gtt_space != NULL)
			seq_printf(m, " (gtt_offset: %08x)", obj_priv->gtt_offset);

		seq_printf(m, "\n");
	}

	if (lock)
	    spin_unlock(lock);
	return 0;
}

static int i915_gem_request_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_gem_request *gem_request;

	seq_printf(m, "Request:\n");
	list_for_each_entry(gem_request, &dev_priv->mm.request_list, list) {
		seq_printf(m, "    %d @ %d\n",
			   gem_request->seqno,
			   (int) (jiffies - gem_request->emitted_jiffies));
	}
	return 0;
}

static int i915_gem_seqno_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;

	if (dev_priv->hw_status_page != NULL) {
		seq_printf(m, "Current sequence: %d\n",
			   i915_get_gem_seqno(dev));
	} else {
		seq_printf(m, "Current sequence: hws uninitialized\n");
	}
	seq_printf(m, "Waiter sequence:  %d\n",
			dev_priv->mm.waiting_gem_seqno);
	seq_printf(m, "IRQ sequence:     %d\n", dev_priv->mm.irq_gem_seqno);
	return 0;
}


static int i915_interrupt_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;

	if (!IS_IGDNG(dev)) {
		seq_printf(m, "Interrupt enable:    %08x\n",
			   I915_READ(IER));
		seq_printf(m, "Interrupt identity:  %08x\n",
			   I915_READ(IIR));
		seq_printf(m, "Interrupt mask:      %08x\n",
			   I915_READ(IMR));
		seq_printf(m, "Pipe A stat:         %08x\n",
			   I915_READ(PIPEASTAT));
		seq_printf(m, "Pipe B stat:         %08x\n",
			   I915_READ(PIPEBSTAT));
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
	if (dev_priv->hw_status_page != NULL) {
		seq_printf(m, "Current sequence:    %d\n",
			   i915_get_gem_seqno(dev));
	} else {
		seq_printf(m, "Current sequence:    hws uninitialized\n");
	}
	seq_printf(m, "Waiter sequence:     %d\n",
		   dev_priv->mm.waiting_gem_seqno);
	seq_printf(m, "IRQ sequence:        %d\n",
		   dev_priv->mm.irq_gem_seqno);
	return 0;
}

static int i915_gem_fence_regs_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	int i;

	seq_printf(m, "Reserved fences = %d\n", dev_priv->fence_reg_start);
	seq_printf(m, "Total fences = %d\n", dev_priv->num_fence_regs);
	for (i = 0; i < dev_priv->num_fence_regs; i++) {
		struct drm_gem_object *obj = dev_priv->fence_regs[i].obj;

		if (obj == NULL) {
			seq_printf(m, "Fenced object[%2d] = unused\n", i);
		} else {
			struct drm_i915_gem_object *obj_priv;

			obj_priv = obj->driver_private;
			seq_printf(m, "Fenced object[%2d] = %p: %s "
				   "%08x %08zx %08x %s %08x %08x %d",
				   i, obj, get_pin_flag(obj_priv),
				   obj_priv->gtt_offset,
				   obj->size, obj_priv->stride,
				   get_tiling_flag(obj_priv),
				   obj->read_domains, obj->write_domain,
				   obj_priv->last_rendering_seqno);
			if (obj->name)
				seq_printf(m, " (name: %d)", obj->name);
			seq_printf(m, "\n");
		}
	}

	return 0;
}

static int i915_hws_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	int i;
	volatile u32 *hws;

	hws = (volatile u32 *)dev_priv->hw_status_page;
	if (hws == NULL)
		return 0;

	for (i = 0; i < 4096 / sizeof(u32) / 4; i += 4) {
		seq_printf(m, "0x%08x: 0x%08x 0x%08x 0x%08x 0x%08x\n",
			   i * 4,
			   hws[i], hws[i + 1], hws[i + 2], hws[i + 3]);
	}
	return 0;
}

static void i915_dump_pages(struct seq_file *m, struct page **pages, int page_count)
{
	int page, i;
	uint32_t *mem;

	for (page = 0; page < page_count; page++) {
		mem = kmap_atomic(pages[page], KM_USER0);
		for (i = 0; i < PAGE_SIZE; i += 4)
			seq_printf(m, "%08x :  %08x\n", i, mem[i / 4]);
		kunmap_atomic(pages[page], KM_USER0);
	}
}

static int i915_batchbuffer_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_gem_object *obj;
	struct drm_i915_gem_object *obj_priv;
	int ret;

	spin_lock(&dev_priv->mm.active_list_lock);

	list_for_each_entry(obj_priv, &dev_priv->mm.active_list, list) {
		obj = obj_priv->obj;
		if (obj->read_domains & I915_GEM_DOMAIN_COMMAND) {
		    ret = i915_gem_object_get_pages(obj);
		    if (ret) {
			    DRM_ERROR("Failed to get pages: %d\n", ret);
			    spin_unlock(&dev_priv->mm.active_list_lock);
			    return ret;
		    }

		    seq_printf(m, "--- gtt_offset = 0x%08x\n", obj_priv->gtt_offset);
		    i915_dump_pages(m, obj_priv->pages, obj->size / PAGE_SIZE);

		    i915_gem_object_put_pages(obj);
		}
	}

	spin_unlock(&dev_priv->mm.active_list_lock);

	return 0;
}

static int i915_ringbuffer_data(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	u8 *virt;
	uint32_t *ptr, off;

	if (!dev_priv->ring.ring_obj) {
		seq_printf(m, "No ringbuffer setup\n");
		return 0;
	}

	virt = dev_priv->ring.virtual_start;

	for (off = 0; off < dev_priv->ring.Size; off += 4) {
		ptr = (uint32_t *)(virt + off);
		seq_printf(m, "%08x :  %08x\n", off, *ptr);
	}

	return 0;
}

static int i915_ringbuffer_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	unsigned int head, tail;

	head = I915_READ(PRB0_HEAD) & HEAD_ADDR;
	tail = I915_READ(PRB0_TAIL) & TAIL_ADDR;

	seq_printf(m, "RingHead :  %08x\n", head);
	seq_printf(m, "RingTail :  %08x\n", tail);
	seq_printf(m, "RingSize :  %08lx\n", dev_priv->ring.Size);
	seq_printf(m, "Acthd :     %08x\n", I915_READ(IS_I965G(dev) ? ACTHD_I965 : ACTHD));

	return 0;
}

static int i915_error_state(struct seq_file *m, void *unused)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_error_state *error;
	unsigned long flags;

	spin_lock_irqsave(&dev_priv->error_lock, flags);
	if (!dev_priv->first_error) {
		seq_printf(m, "no error state collected\n");
		goto out;
	}

	error = dev_priv->first_error;

	seq_printf(m, "Time: %ld s %ld us\n", error->time.tv_sec,
		   error->time.tv_usec);
	seq_printf(m, "EIR: 0x%08x\n", error->eir);
	seq_printf(m, "  PGTBL_ER: 0x%08x\n", error->pgtbl_er);
	seq_printf(m, "  INSTPM: 0x%08x\n", error->instpm);
	seq_printf(m, "  IPEIR: 0x%08x\n", error->ipeir);
	seq_printf(m, "  IPEHR: 0x%08x\n", error->ipehr);
	seq_printf(m, "  INSTDONE: 0x%08x\n", error->instdone);
	seq_printf(m, "  ACTHD: 0x%08x\n", error->acthd);
	if (IS_I965G(dev)) {
		seq_printf(m, "  INSTPS: 0x%08x\n", error->instps);
		seq_printf(m, "  INSTDONE1: 0x%08x\n", error->instdone1);
	}

out:
	spin_unlock_irqrestore(&dev_priv->error_lock, flags);

	return 0;
}

static int i915_registers_info(struct seq_file *m, void *data) {
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	uint32_t reg;

#define DUMP_RANGE(start, end) \
	for (reg=start; reg < end; reg += 4) \
	seq_printf(m, "%08x\t%08x\n", reg, I915_READ(reg));

	DUMP_RANGE(0x00000, 0x00fff);   /* VGA registers */
	DUMP_RANGE(0x02000, 0x02fff);   /* instruction, memory, interrupt control registers */
	DUMP_RANGE(0x03000, 0x031ff);   /* FENCE and PPGTT control registers */
	DUMP_RANGE(0x03200, 0x03fff);   /* frame buffer compression registers */
	DUMP_RANGE(0x05000, 0x05fff);   /* I/O control registers */
	DUMP_RANGE(0x06000, 0x06fff);   /* clock control registers */
	DUMP_RANGE(0x07000, 0x07fff);   /* 3D internal debug registers */
	DUMP_RANGE(0x07400, 0x088ff);   /* GPE debug registers */
	DUMP_RANGE(0x0a000, 0x0afff);   /* display palette registers */
	DUMP_RANGE(0x10000, 0x13fff);   /* MMIO MCHBAR */
	DUMP_RANGE(0x30000, 0x3ffff);   /* overlay registers */
	DUMP_RANGE(0x60000, 0x6ffff);   /* display engine pipeline registers */
	DUMP_RANGE(0x70000, 0x72fff);   /* display and cursor registers */
	DUMP_RANGE(0x73000, 0x73fff);   /* performance counters */

	return 0;
}


static struct drm_info_list i915_debugfs_list[] = {
	{"i915_regs", i915_registers_info, 0},
	{"i915_gem_active", i915_gem_object_list_info, 0, (void *) ACTIVE_LIST},
	{"i915_gem_flushing", i915_gem_object_list_info, 0, (void *) FLUSHING_LIST},
	{"i915_gem_inactive", i915_gem_object_list_info, 0, (void *) INACTIVE_LIST},
	{"i915_gem_request", i915_gem_request_info, 0},
	{"i915_gem_seqno", i915_gem_seqno_info, 0},
	{"i915_gem_fence_regs", i915_gem_fence_regs_info, 0},
	{"i915_gem_interrupt", i915_interrupt_info, 0},
	{"i915_gem_hws", i915_hws_info, 0},
	{"i915_ringbuffer_data", i915_ringbuffer_data, 0},
	{"i915_ringbuffer_info", i915_ringbuffer_info, 0},
	{"i915_batchbuffers", i915_batchbuffer_info, 0},
	{"i915_error_state", i915_error_state, 0},
};
#define I915_DEBUGFS_ENTRIES ARRAY_SIZE(i915_debugfs_list)

int i915_debugfs_init(struct drm_minor *minor)
{
	return drm_debugfs_create_files(i915_debugfs_list,
					I915_DEBUGFS_ENTRIES,
					minor->debugfs_root, minor);
}

void i915_debugfs_cleanup(struct drm_minor *minor)
{
	drm_debugfs_remove_files(i915_debugfs_list,
				 I915_DEBUGFS_ENTRIES, minor);
}

#endif /* CONFIG_DEBUG_FS */

