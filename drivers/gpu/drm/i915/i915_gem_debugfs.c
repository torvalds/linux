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

static int i915_gem_object_list_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	uintptr_t list = (uintptr_t) node->info_ent->data;
	struct list_head *head;
	struct drm_device *dev = node->minor->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj_priv;

	switch (list) {
	case ACTIVE_LIST:
		seq_printf(m, "Active:\n");
		head = &dev_priv->mm.active_list;
		break;
	case INACTIVE_LIST:
		seq_printf(m, "Inctive:\n");
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

	list_for_each_entry(obj_priv, head, list)
	{
		struct drm_gem_object *obj = obj_priv->obj;
		if (obj->name) {
			seq_printf(m, "    %p(%d): %08x %08x %d\n",
					obj, obj->name,
					obj->read_domains, obj->write_domain,
					obj_priv->last_rendering_seqno);
		} else {
			seq_printf(m, "       %p: %08x %08x %d\n",
					obj,
					obj->read_domains, obj->write_domain,
					obj_priv->last_rendering_seqno);
		}
	}
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

static struct drm_info_list i915_gem_debugfs_list[] = {
	{"i915_gem_active", i915_gem_object_list_info, 0, (void *) ACTIVE_LIST},
	{"i915_gem_flushing", i915_gem_object_list_info, 0, (void *) FLUSHING_LIST},
	{"i915_gem_inactive", i915_gem_object_list_info, 0, (void *) INACTIVE_LIST},
	{"i915_gem_request", i915_gem_request_info, 0},
	{"i915_gem_seqno", i915_gem_seqno_info, 0},
	{"i915_gem_interrupt", i915_interrupt_info, 0},
	{"i915_gem_hws", i915_hws_info, 0},
};
#define I915_GEM_DEBUGFS_ENTRIES ARRAY_SIZE(i915_gem_debugfs_list)

int i915_gem_debugfs_init(struct drm_minor *minor)
{
	return drm_debugfs_create_files(i915_gem_debugfs_list,
					I915_GEM_DEBUGFS_ENTRIES,
					minor->debugfs_root, minor);
}

void i915_gem_debugfs_cleanup(struct drm_minor *minor)
{
	drm_debugfs_remove_files(i915_gem_debugfs_list,
				 I915_GEM_DEBUGFS_ENTRIES, minor);
}

#endif /* CONFIG_DEBUG_FS */

