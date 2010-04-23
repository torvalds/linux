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

		seq_printf(m, "    %p: %s %8zd %08x %08x %d%s%s",
			   obj,
			   get_pin_flag(obj_priv),
			   obj->size,
			   obj->read_domains, obj->write_domain,
			   obj_priv->last_rendering_seqno,
			   obj_priv->dirty ? " dirty" : "",
			   obj_priv->madv == I915_MADV_DONTNEED ? " purgeable" : "");

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

	if (!HAS_PCH_SPLIT(dev)) {
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

			obj_priv = to_intel_bo(obj);
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
		kunmap_atomic(mem, KM_USER0);
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
		    ret = i915_gem_object_get_pages(obj, 0);
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
	seq_printf(m, "seqno: 0x%08x\n", error->seqno);

	if (error->active_bo_count) {
		seq_printf(m, "Buffers [%d]:\n", error->active_bo_count);

		for (i = 0; i < error->active_bo_count; i++) {
			seq_printf(m, "  %08x %8zd %08x %08x %08x%s%s%s%s",
				   error->active_bo[i].gtt_offset,
				   error->active_bo[i].size,
				   error->active_bo[i].read_domains,
				   error->active_bo[i].write_domain,
				   error->active_bo[i].seqno,
				   pin_flag(error->active_bo[i].pinned),
				   tiling_flag(error->active_bo[i].tiling),
				   dirty_flag(error->active_bo[i].dirty),
				   purgeable_flag(error->active_bo[i].purgeable));

			if (error->active_bo[i].name)
				seq_printf(m, " (name: %d)", error->active_bo[i].name);
			if (error->active_bo[i].fence_reg != I915_FENCE_REG_NONE)
				seq_printf(m, " (fence: %d)", error->active_bo[i].fence_reg);

			seq_printf(m, "\n");
		}
	}

	for (i = 0; i < ARRAY_SIZE(error->batchbuffer); i++) {
		if (error->batchbuffer[i]) {
			struct drm_i915_error_object *obj = error->batchbuffer[i];

			seq_printf(m, "--- gtt_offset = 0x%08x\n", obj->gtt_offset);
			offset = 0;
			for (page = 0; page < obj->page_count; page++) {
				for (elt = 0; elt < PAGE_SIZE/4; elt++) {
					seq_printf(m, "%08x :  %08x\n", offset, obj->pages[page][elt]);
					offset += 4;
				}
			}
		}
	}

	if (error->ringbuffer) {
		struct drm_i915_error_object *obj = error->ringbuffer;

		seq_printf(m, "--- ringbuffer = 0x%08x\n", obj->gtt_offset);
		offset = 0;
		for (page = 0; page < obj->page_count; page++) {
			for (elt = 0; elt < PAGE_SIZE/4; elt++) {
				seq_printf(m, "%08x :  %08x\n", offset, obj->pages[page][elt]);
				offset += 4;
			}
		}
	}

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
	u16 rgvswctl = I915_READ16(MEMSWCTL);

	seq_printf(m, "Last command: 0x%01x\n", (rgvswctl >> 13) & 0x3);
	seq_printf(m, "Command status: %d\n", (rgvswctl >> 12) & 1);
	seq_printf(m, "P%d DELAY 0x%02x\n", (rgvswctl >> 8) & 0xf,
		   rgvswctl & 0x3f);

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
		seq_printf(m, "P%02dVIDFREQ: 0x%08x\n", i, delayfreq);
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
	seq_printf(m, "Max frequency: P%d\n",
		   (rgvmodectl & MEMMODE_FMAX_MASK) >> MEMMODE_FMAX_SHIFT);
	seq_printf(m, "Min frequency: P%d\n", (rgvmodectl & MEMMODE_FMIN_MASK));

	return 0;
}

static int i915_fbc_status(struct seq_file *m, void *unused)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct drm_crtc *crtc;
	drm_i915_private_t *dev_priv = dev->dev_private;
	bool fbc_enabled = false;

	if (!dev_priv->display.fbc_enabled) {
		seq_printf(m, "FBC unsupported on this chipset\n");
		return 0;
	}

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		if (!crtc->enabled)
			continue;
		if (dev_priv->display.fbc_enabled(crtc))
			fbc_enabled = true;
	}

	if (fbc_enabled) {
		seq_printf(m, "FBC enabled\n");
	} else {
		seq_printf(m, "FBC disabled: ");
		switch (dev_priv->no_fbc_reason) {
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

	if (IS_I965G(dev) || IS_I945G(dev) || IS_I945GM(dev))
		sr_enabled = I915_READ(FW_BLC_SELF) & FW_BLC_SELF_EN;
	else if (IS_I915GM(dev))
		sr_enabled = I915_READ(INSTPM) & INSTPM_SELF_EN;
	else if (IS_PINEVIEW(dev))
		sr_enabled = I915_READ(DSPFW3) & PINEVIEW_SELF_REFRESH_EN;

	seq_printf(m, "self-refresh: %s\n", sr_enabled ? "enabled" :
		   "disabled");

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

	len = snprintf(buf, sizeof (buf),
		       "wedged :  %d\n",
		       atomic_read(&dev_priv->mm.wedged));

	return simple_read_from_buffer(ubuf, max, ppos, buf, len);
}

static ssize_t
i915_wedged_write(struct file *filp,
		  const char __user *ubuf,
		  size_t cnt,
		  loff_t *ppos)
{
	struct drm_device *dev = filp->private_data;
	drm_i915_private_t *dev_priv = dev->dev_private;
	char buf[20];
	int val = 1;

	if (cnt > 0) {
		if (cnt > sizeof (buf) - 1)
			return -EINVAL;

		if (copy_from_user(buf, ubuf, cnt))
			return -EFAULT;
		buf[cnt] = 0;

		val = simple_strtoul(buf, NULL, 0);
	}

	DRM_INFO("Manually setting wedged to %d\n", val);

	atomic_set(&dev_priv->mm.wedged, val);
	if (val) {
		DRM_WAKEUP(&dev_priv->irq_queue);
		queue_work(dev_priv->wq, &dev_priv->error_work);
	}

	return cnt;
}

static const struct file_operations i915_wedged_fops = {
	.owner = THIS_MODULE,
	.open = i915_wedged_open,
	.read = i915_wedged_read,
	.write = i915_wedged_write,
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

static struct drm_info_list i915_debugfs_list[] = {
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
	{"i915_rstdby_delays", i915_rstdby_delays, 0},
	{"i915_cur_delayinfo", i915_cur_delayinfo, 0},
	{"i915_delayfreq_table", i915_delayfreq_table, 0},
	{"i915_inttoext_table", i915_inttoext_table, 0},
	{"i915_drpc_info", i915_drpc_info, 0},
	{"i915_fbc_status", i915_fbc_status, 0},
	{"i915_sr_status", i915_sr_status, 0},
};
#define I915_DEBUGFS_ENTRIES ARRAY_SIZE(i915_debugfs_list)

int i915_debugfs_init(struct drm_minor *minor)
{
	int ret;

	ret = i915_wedged_create(minor->debugfs_root, minor);
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
	drm_debugfs_remove_files((struct drm_info_list *) &i915_wedged_fops,
				 1, minor);
}

#endif /* CONFIG_DEBUG_FS */
