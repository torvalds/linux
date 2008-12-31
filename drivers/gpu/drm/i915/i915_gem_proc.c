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

#include "drmP.h"
#include "drm.h"
#include "i915_drm.h"
#include "i915_drv.h"

static int i915_gem_active_info(char *buf, char **start, off_t offset,
				int request, int *eof, void *data)
{
	struct drm_minor *minor = (struct drm_minor *) data;
	struct drm_device *dev = minor->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj_priv;
	int len = 0;

	if (offset > DRM_PROC_LIMIT) {
		*eof = 1;
		return 0;
	}

	*start = &buf[offset];
	*eof = 0;
	DRM_PROC_PRINT("Active:\n");
	list_for_each_entry(obj_priv, &dev_priv->mm.active_list,
			    list)
	{
		struct drm_gem_object *obj = obj_priv->obj;
		if (obj->name) {
			DRM_PROC_PRINT("    %p(%d): %08x %08x %d\n",
				       obj, obj->name,
				       obj->read_domains, obj->write_domain,
				       obj_priv->last_rendering_seqno);
		} else {
			DRM_PROC_PRINT("       %p: %08x %08x %d\n",
				       obj,
				       obj->read_domains, obj->write_domain,
				       obj_priv->last_rendering_seqno);
		}
	}
	if (len > request + offset)
		return request;
	*eof = 1;
	return len - offset;
}

static int i915_gem_flushing_info(char *buf, char **start, off_t offset,
				  int request, int *eof, void *data)
{
	struct drm_minor *minor = (struct drm_minor *) data;
	struct drm_device *dev = minor->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj_priv;
	int len = 0;

	if (offset > DRM_PROC_LIMIT) {
		*eof = 1;
		return 0;
	}

	*start = &buf[offset];
	*eof = 0;
	DRM_PROC_PRINT("Flushing:\n");
	list_for_each_entry(obj_priv, &dev_priv->mm.flushing_list,
			    list)
	{
		struct drm_gem_object *obj = obj_priv->obj;
		if (obj->name) {
			DRM_PROC_PRINT("    %p(%d): %08x %08x %d\n",
				       obj, obj->name,
				       obj->read_domains, obj->write_domain,
				       obj_priv->last_rendering_seqno);
		} else {
			DRM_PROC_PRINT("       %p: %08x %08x %d\n", obj,
				       obj->read_domains, obj->write_domain,
				       obj_priv->last_rendering_seqno);
		}
	}
	if (len > request + offset)
		return request;
	*eof = 1;
	return len - offset;
}

static int i915_gem_inactive_info(char *buf, char **start, off_t offset,
				  int request, int *eof, void *data)
{
	struct drm_minor *minor = (struct drm_minor *) data;
	struct drm_device *dev = minor->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj_priv;
	int len = 0;

	if (offset > DRM_PROC_LIMIT) {
		*eof = 1;
		return 0;
	}

	*start = &buf[offset];
	*eof = 0;
	DRM_PROC_PRINT("Inactive:\n");
	list_for_each_entry(obj_priv, &dev_priv->mm.inactive_list,
			    list)
	{
		struct drm_gem_object *obj = obj_priv->obj;
		if (obj->name) {
			DRM_PROC_PRINT("    %p(%d): %08x %08x %d\n",
				       obj, obj->name,
				       obj->read_domains, obj->write_domain,
				       obj_priv->last_rendering_seqno);
		} else {
			DRM_PROC_PRINT("       %p: %08x %08x %d\n", obj,
				       obj->read_domains, obj->write_domain,
				       obj_priv->last_rendering_seqno);
		}
	}
	if (len > request + offset)
		return request;
	*eof = 1;
	return len - offset;
}

static int i915_gem_request_info(char *buf, char **start, off_t offset,
				 int request, int *eof, void *data)
{
	struct drm_minor *minor = (struct drm_minor *) data;
	struct drm_device *dev = minor->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_gem_request *gem_request;
	int len = 0;

	if (offset > DRM_PROC_LIMIT) {
		*eof = 1;
		return 0;
	}

	*start = &buf[offset];
	*eof = 0;
	DRM_PROC_PRINT("Request:\n");
	list_for_each_entry(gem_request, &dev_priv->mm.request_list,
			    list)
	{
		DRM_PROC_PRINT("    %d @ %d\n",
			       gem_request->seqno,
			       (int) (jiffies - gem_request->emitted_jiffies));
	}
	if (len > request + offset)
		return request;
	*eof = 1;
	return len - offset;
}

static int i915_gem_seqno_info(char *buf, char **start, off_t offset,
			       int request, int *eof, void *data)
{
	struct drm_minor *minor = (struct drm_minor *) data;
	struct drm_device *dev = minor->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	int len = 0;

	if (offset > DRM_PROC_LIMIT) {
		*eof = 1;
		return 0;
	}

	*start = &buf[offset];
	*eof = 0;
	if (dev_priv->hw_status_page != NULL) {
		DRM_PROC_PRINT("Current sequence: %d\n",
			       i915_get_gem_seqno(dev));
	} else {
		DRM_PROC_PRINT("Current sequence: hws uninitialized\n");
	}
	DRM_PROC_PRINT("Waiter sequence:  %d\n",
		       dev_priv->mm.waiting_gem_seqno);
	DRM_PROC_PRINT("IRQ sequence:     %d\n", dev_priv->mm.irq_gem_seqno);
	if (len > request + offset)
		return request;
	*eof = 1;
	return len - offset;
}


static int i915_interrupt_info(char *buf, char **start, off_t offset,
			       int request, int *eof, void *data)
{
	struct drm_minor *minor = (struct drm_minor *) data;
	struct drm_device *dev = minor->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	int len = 0;

	if (offset > DRM_PROC_LIMIT) {
		*eof = 1;
		return 0;
	}

	*start = &buf[offset];
	*eof = 0;
	DRM_PROC_PRINT("Interrupt enable:    %08x\n",
		       I915_READ(IER));
	DRM_PROC_PRINT("Interrupt identity:  %08x\n",
		       I915_READ(IIR));
	DRM_PROC_PRINT("Interrupt mask:      %08x\n",
		       I915_READ(IMR));
	DRM_PROC_PRINT("Pipe A stat:         %08x\n",
		       I915_READ(PIPEASTAT));
	DRM_PROC_PRINT("Pipe B stat:         %08x\n",
		       I915_READ(PIPEBSTAT));
	DRM_PROC_PRINT("Interrupts received: %d\n",
		       atomic_read(&dev_priv->irq_received));
	if (dev_priv->hw_status_page != NULL) {
		DRM_PROC_PRINT("Current sequence:    %d\n",
			       i915_get_gem_seqno(dev));
	} else {
		DRM_PROC_PRINT("Current sequence:    hws uninitialized\n");
	}
	DRM_PROC_PRINT("Waiter sequence:     %d\n",
		       dev_priv->mm.waiting_gem_seqno);
	DRM_PROC_PRINT("IRQ sequence:        %d\n",
		       dev_priv->mm.irq_gem_seqno);
	if (len > request + offset)
		return request;
	*eof = 1;
	return len - offset;
}

static int i915_hws_info(char *buf, char **start, off_t offset,
			 int request, int *eof, void *data)
{
	struct drm_minor *minor = (struct drm_minor *) data;
	struct drm_device *dev = minor->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	int len = 0, i;
	volatile u32 *hws;

	if (offset > DRM_PROC_LIMIT) {
		*eof = 1;
		return 0;
	}

	hws = (volatile u32 *)dev_priv->hw_status_page;
	if (hws == NULL) {
		*eof = 1;
		return 0;
	}

	*start = &buf[offset];
	*eof = 0;
	for (i = 0; i < 4096 / sizeof(u32) / 4; i += 4) {
		DRM_PROC_PRINT("0x%08x: 0x%08x 0x%08x 0x%08x 0x%08x\n",
			       i * 4,
			       hws[i], hws[i + 1], hws[i + 2], hws[i + 3]);
	}
	if (len > request + offset)
		return request;
	*eof = 1;
	return len - offset;
}

static struct drm_proc_list {
	/** file name */
	const char *name;
	/** proc callback*/
	int (*f) (char *, char **, off_t, int, int *, void *);
} i915_gem_proc_list[] = {
	{"i915_gem_active", i915_gem_active_info},
	{"i915_gem_flushing", i915_gem_flushing_info},
	{"i915_gem_inactive", i915_gem_inactive_info},
	{"i915_gem_request", i915_gem_request_info},
	{"i915_gem_seqno", i915_gem_seqno_info},
	{"i915_gem_interrupt", i915_interrupt_info},
	{"i915_gem_hws", i915_hws_info},
};

#define I915_GEM_PROC_ENTRIES ARRAY_SIZE(i915_gem_proc_list)

int i915_gem_proc_init(struct drm_minor *minor)
{
	struct proc_dir_entry *ent;
	int i, j;

	for (i = 0; i < I915_GEM_PROC_ENTRIES; i++) {
		ent = create_proc_entry(i915_gem_proc_list[i].name,
					S_IFREG | S_IRUGO, minor->dev_root);
		if (!ent) {
			DRM_ERROR("Cannot create /proc/dri/.../%s\n",
				  i915_gem_proc_list[i].name);
			for (j = 0; j < i; j++)
				remove_proc_entry(i915_gem_proc_list[i].name,
						  minor->dev_root);
			return -1;
		}
		ent->read_proc = i915_gem_proc_list[i].f;
		ent->data = minor;
	}
	return 0;
}

void i915_gem_proc_cleanup(struct drm_minor *minor)
{
	int i;

	if (!minor->dev_root)
		return;

	for (i = 0; i < I915_GEM_PROC_ENTRIES; i++)
		remove_proc_entry(i915_gem_proc_list[i].name, minor->dev_root);
}
