/*
 * Copyright (c) 2008 Intel Corporation
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
 *    Mika Kuoppala <mika.kuoppala@intel.com>
 *
 */

#include <generated/utsrelease.h>
#include "i915_drv.h"

static const char *yesno(int v)
{
	return v ? "yes" : "no";
}

static const char *ring_str(int ring)
{
	switch (ring) {
	case RCS: return "render";
	case VCS: return "bsd";
	case BCS: return "blt";
	case VECS: return "vebox";
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

static bool __i915_error_ok(struct drm_i915_error_state_buf *e)
{

	if (!e->err && WARN(e->bytes > (e->size - 1), "overflow")) {
		e->err = -ENOSPC;
		return false;
	}

	if (e->bytes == e->size - 1 || e->err)
		return false;

	return true;
}

static bool __i915_error_seek(struct drm_i915_error_state_buf *e,
			      unsigned len)
{
	if (e->pos + len <= e->start) {
		e->pos += len;
		return false;
	}

	/* First vsnprintf needs to fit in its entirety for memmove */
	if (len >= e->size) {
		e->err = -EIO;
		return false;
	}

	return true;
}

static void __i915_error_advance(struct drm_i915_error_state_buf *e,
				 unsigned len)
{
	/* If this is first printf in this window, adjust it so that
	 * start position matches start of the buffer
	 */

	if (e->pos < e->start) {
		const size_t off = e->start - e->pos;

		/* Should not happen but be paranoid */
		if (off > len || e->bytes) {
			e->err = -EIO;
			return;
		}

		memmove(e->buf, e->buf + off, len - off);
		e->bytes = len - off;
		e->pos = e->start;
		return;
	}

	e->bytes += len;
	e->pos += len;
}

static void i915_error_vprintf(struct drm_i915_error_state_buf *e,
			       const char *f, va_list args)
{
	unsigned len;

	if (!__i915_error_ok(e))
		return;

	/* Seek the first printf which is hits start position */
	if (e->pos < e->start) {
		len = vsnprintf(NULL, 0, f, args);
		if (!__i915_error_seek(e, len))
			return;
	}

	len = vsnprintf(e->buf + e->bytes, e->size - e->bytes, f, args);
	if (len >= e->size - e->bytes)
		len = e->size - e->bytes - 1;

	__i915_error_advance(e, len);
}

static void i915_error_puts(struct drm_i915_error_state_buf *e,
			    const char *str)
{
	unsigned len;

	if (!__i915_error_ok(e))
		return;

	len = strlen(str);

	/* Seek the first printf which is hits start position */
	if (e->pos < e->start) {
		if (!__i915_error_seek(e, len))
			return;
	}

	if (len >= e->size - e->bytes)
		len = e->size - e->bytes - 1;
	memcpy(e->buf + e->bytes, str, len);

	__i915_error_advance(e, len);
}

#define err_printf(e, ...) i915_error_printf(e, __VA_ARGS__)
#define err_puts(e, s) i915_error_puts(e, s)

static void print_error_buffers(struct drm_i915_error_state_buf *m,
				const char *name,
				struct drm_i915_error_buffer *err,
				int count)
{
	err_printf(m, "%s [%d]:\n", name, count);

	while (count--) {
		err_printf(m, "  %08x %8u %02x %02x %x %x",
			   err->gtt_offset,
			   err->size,
			   err->read_domains,
			   err->write_domain,
			   err->rseqno, err->wseqno);
		err_puts(m, pin_flag(err->pinned));
		err_puts(m, tiling_flag(err->tiling));
		err_puts(m, dirty_flag(err->dirty));
		err_puts(m, purgeable_flag(err->purgeable));
		err_puts(m, err->ring != -1 ? " " : "");
		err_puts(m, ring_str(err->ring));
		err_puts(m, i915_cache_level_str(err->cache_level));

		if (err->name)
			err_printf(m, " (name: %d)", err->name);
		if (err->fence_reg != I915_FENCE_REG_NONE)
			err_printf(m, " (fence: %d)", err->fence_reg);

		err_puts(m, "\n");
		err++;
	}
}

static void i915_ring_error_state(struct drm_i915_error_state_buf *m,
				  struct drm_device *dev,
				  struct drm_i915_error_state *error,
				  unsigned ring)
{
	BUG_ON(ring >= I915_NUM_RINGS); /* shut up confused gcc */
	err_printf(m, "%s command stream:\n", ring_str(ring));
	err_printf(m, "  HEAD: 0x%08x\n", error->head[ring]);
	err_printf(m, "  TAIL: 0x%08x\n", error->tail[ring]);
	err_printf(m, "  CTL: 0x%08x\n", error->ctl[ring]);
	err_printf(m, "  ACTHD: 0x%08x\n", error->acthd[ring]);
	err_printf(m, "  IPEIR: 0x%08x\n", error->ipeir[ring]);
	err_printf(m, "  IPEHR: 0x%08x\n", error->ipehr[ring]);
	err_printf(m, "  INSTDONE: 0x%08x\n", error->instdone[ring]);
	if (ring == RCS && INTEL_INFO(dev)->gen >= 4)
		err_printf(m, "  BBADDR: 0x%08llx\n", error->bbaddr);

	if (INTEL_INFO(dev)->gen >= 4)
		err_printf(m, "  INSTPS: 0x%08x\n", error->instps[ring]);
	err_printf(m, "  INSTPM: 0x%08x\n", error->instpm[ring]);
	err_printf(m, "  FADDR: 0x%08x\n", error->faddr[ring]);
	if (INTEL_INFO(dev)->gen >= 6) {
		err_printf(m, "  RC PSMI: 0x%08x\n", error->rc_psmi[ring]);
		err_printf(m, "  FAULT_REG: 0x%08x\n", error->fault_reg[ring]);
		err_printf(m, "  SYNC_0: 0x%08x [last synced 0x%08x]\n",
			   error->semaphore_mboxes[ring][0],
			   error->semaphore_seqno[ring][0]);
		err_printf(m, "  SYNC_1: 0x%08x [last synced 0x%08x]\n",
			   error->semaphore_mboxes[ring][1],
			   error->semaphore_seqno[ring][1]);
	}
	err_printf(m, "  seqno: 0x%08x\n", error->seqno[ring]);
	err_printf(m, "  waiting: %s\n", yesno(error->waiting[ring]));
	err_printf(m, "  ring->head: 0x%08x\n", error->cpu_ring_head[ring]);
	err_printf(m, "  ring->tail: 0x%08x\n", error->cpu_ring_tail[ring]);
}

void i915_error_printf(struct drm_i915_error_state_buf *e, const char *f, ...)
{
	va_list args;

	va_start(args, f);
	i915_error_vprintf(e, f, args);
	va_end(args);
}

int i915_error_state_to_str(struct drm_i915_error_state_buf *m,
			    const struct i915_error_state_file_priv *error_priv)
{
	struct drm_device *dev = error_priv->dev;
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_error_state *error = error_priv->error;
	struct intel_ring_buffer *ring;
	int i, j, page, offset, elt;

	if (!error) {
		err_printf(m, "no error state collected\n");
		goto out;
	}

	err_printf(m, "Time: %ld s %ld us\n", error->time.tv_sec,
		   error->time.tv_usec);
	err_printf(m, "Kernel: " UTS_RELEASE "\n");
	err_printf(m, "PCI ID: 0x%04x\n", dev->pci_device);
	err_printf(m, "EIR: 0x%08x\n", error->eir);
	err_printf(m, "IER: 0x%08x\n", error->ier);
	err_printf(m, "PGTBL_ER: 0x%08x\n", error->pgtbl_er);
	err_printf(m, "FORCEWAKE: 0x%08x\n", error->forcewake);
	err_printf(m, "DERRMR: 0x%08x\n", error->derrmr);
	err_printf(m, "CCID: 0x%08x\n", error->ccid);

	for (i = 0; i < dev_priv->num_fence_regs; i++)
		err_printf(m, "  fence[%d] = %08llx\n", i, error->fence[i]);

	for (i = 0; i < ARRAY_SIZE(error->extra_instdone); i++)
		err_printf(m, "  INSTDONE_%d: 0x%08x\n", i,
			   error->extra_instdone[i]);

	if (INTEL_INFO(dev)->gen >= 6) {
		err_printf(m, "ERROR: 0x%08x\n", error->error);
		err_printf(m, "DONE_REG: 0x%08x\n", error->done_reg);
	}

	if (INTEL_INFO(dev)->gen == 7)
		err_printf(m, "ERR_INT: 0x%08x\n", error->err_int);

	for_each_ring(ring, dev_priv, i)
		i915_ring_error_state(m, dev, error, i);

	if (error->active_bo)
		print_error_buffers(m, "Active",
				    error->active_bo,
				    error->active_bo_count);

	if (error->pinned_bo)
		print_error_buffers(m, "Pinned",
				    error->pinned_bo,
				    error->pinned_bo_count);

	for (i = 0; i < ARRAY_SIZE(error->ring); i++) {
		struct drm_i915_error_object *obj;

		if ((obj = error->ring[i].batchbuffer)) {
			err_printf(m, "%s --- gtt_offset = 0x%08x\n",
				   dev_priv->ring[i].name,
				   obj->gtt_offset);
			offset = 0;
			for (page = 0; page < obj->page_count; page++) {
				for (elt = 0; elt < PAGE_SIZE/4; elt++) {
					err_printf(m, "%08x :  %08x\n", offset,
						   obj->pages[page][elt]);
					offset += 4;
				}
			}
		}

		if (error->ring[i].num_requests) {
			err_printf(m, "%s --- %d requests\n",
				   dev_priv->ring[i].name,
				   error->ring[i].num_requests);
			for (j = 0; j < error->ring[i].num_requests; j++) {
				err_printf(m, "  seqno 0x%08x, emitted %ld, tail 0x%08x\n",
					   error->ring[i].requests[j].seqno,
					   error->ring[i].requests[j].jiffies,
					   error->ring[i].requests[j].tail);
			}
		}

		if ((obj = error->ring[i].ringbuffer)) {
			err_printf(m, "%s --- ringbuffer = 0x%08x\n",
				   dev_priv->ring[i].name,
				   obj->gtt_offset);
			offset = 0;
			for (page = 0; page < obj->page_count; page++) {
				for (elt = 0; elt < PAGE_SIZE/4; elt++) {
					err_printf(m, "%08x :  %08x\n",
						   offset,
						   obj->pages[page][elt]);
					offset += 4;
				}
			}
		}

		obj = error->ring[i].ctx;
		if (obj) {
			err_printf(m, "%s --- HW Context = 0x%08x\n",
				   dev_priv->ring[i].name,
				   obj->gtt_offset);
			offset = 0;
			for (elt = 0; elt < PAGE_SIZE/16; elt += 4) {
				err_printf(m, "[%04x] %08x %08x %08x %08x\n",
					   offset,
					   obj->pages[0][elt],
					   obj->pages[0][elt+1],
					   obj->pages[0][elt+2],
					   obj->pages[0][elt+3]);
					offset += 16;
			}
		}
	}

	if (error->overlay)
		intel_overlay_print_error_state(m, error->overlay);

	if (error->display)
		intel_display_print_error_state(m, dev, error->display);

out:
	if (m->bytes == 0 && m->err)
		return m->err;

	return 0;
}

int i915_error_state_buf_init(struct drm_i915_error_state_buf *ebuf,
			      size_t count, loff_t pos)
{
	memset(ebuf, 0, sizeof(*ebuf));

	/* We need to have enough room to store any i915_error_state printf
	 * so that we can move it to start position.
	 */
	ebuf->size = count + 1 > PAGE_SIZE ? count + 1 : PAGE_SIZE;
	ebuf->buf = kmalloc(ebuf->size,
				GFP_TEMPORARY | __GFP_NORETRY | __GFP_NOWARN);

	if (ebuf->buf == NULL) {
		ebuf->size = PAGE_SIZE;
		ebuf->buf = kmalloc(ebuf->size, GFP_TEMPORARY);
	}

	if (ebuf->buf == NULL) {
		ebuf->size = 128;
		ebuf->buf = kmalloc(ebuf->size, GFP_TEMPORARY);
	}

	if (ebuf->buf == NULL)
		return -ENOMEM;

	ebuf->start = pos;

	return 0;
}

static void i915_error_object_free(struct drm_i915_error_object *obj)
{
	int page;

	if (obj == NULL)
		return;

	for (page = 0; page < obj->page_count; page++)
		kfree(obj->pages[page]);

	kfree(obj);
}

static void i915_error_state_free(struct kref *error_ref)
{
	struct drm_i915_error_state *error = container_of(error_ref,
							  typeof(*error), ref);
	int i;

	for (i = 0; i < ARRAY_SIZE(error->ring); i++) {
		i915_error_object_free(error->ring[i].batchbuffer);
		i915_error_object_free(error->ring[i].ringbuffer);
		i915_error_object_free(error->ring[i].ctx);
		kfree(error->ring[i].requests);
	}

	kfree(error->active_bo);
	kfree(error->overlay);
	kfree(error->display);
	kfree(error);
}

static struct drm_i915_error_object *
i915_error_object_create_sized(struct drm_i915_private *dev_priv,
			       struct drm_i915_gem_object *src,
			       const int num_pages)
{
	struct drm_i915_error_object *dst;
	int i;
	u32 reloc_offset;

	if (src == NULL || src->pages == NULL)
		return NULL;

	dst = kmalloc(sizeof(*dst) + num_pages * sizeof(u32 *), GFP_ATOMIC);
	if (dst == NULL)
		return NULL;

	reloc_offset = dst->gtt_offset = i915_gem_obj_ggtt_offset(src);
	for (i = 0; i < num_pages; i++) {
		unsigned long flags;
		void *d;

		d = kmalloc(PAGE_SIZE, GFP_ATOMIC);
		if (d == NULL)
			goto unwind;

		local_irq_save(flags);
		if (reloc_offset < dev_priv->gtt.mappable_end &&
		    src->has_global_gtt_mapping) {
			void __iomem *s;

			/* Simply ignore tiling or any overlapping fence.
			 * It's part of the error state, and this hopefully
			 * captures what the GPU read.
			 */

			s = io_mapping_map_atomic_wc(dev_priv->gtt.mappable,
						     reloc_offset);
			memcpy_fromio(d, s, PAGE_SIZE);
			io_mapping_unmap_atomic(s);
		} else if (src->stolen) {
			unsigned long offset;

			offset = dev_priv->mm.stolen_base;
			offset += src->stolen->start;
			offset += i << PAGE_SHIFT;

			memcpy_fromio(d, (void __iomem *) offset, PAGE_SIZE);
		} else {
			struct page *page;
			void *s;

			page = i915_gem_object_get_page(src, i);

			drm_clflush_pages(&page, 1);

			s = kmap_atomic(page);
			memcpy(d, s, PAGE_SIZE);
			kunmap_atomic(s);

			drm_clflush_pages(&page, 1);
		}
		local_irq_restore(flags);

		dst->pages[i] = d;

		reloc_offset += PAGE_SIZE;
	}
	dst->page_count = num_pages;

	return dst;

unwind:
	while (i--)
		kfree(dst->pages[i]);
	kfree(dst);
	return NULL;
}
#define i915_error_object_create(dev_priv, src) \
	i915_error_object_create_sized((dev_priv), (src), \
				       (src)->base.size>>PAGE_SHIFT)

static void capture_bo(struct drm_i915_error_buffer *err,
		       struct drm_i915_gem_object *obj)
{
	err->size = obj->base.size;
	err->name = obj->base.name;
	err->rseqno = obj->last_read_seqno;
	err->wseqno = obj->last_write_seqno;
	err->gtt_offset = i915_gem_obj_ggtt_offset(obj);
	err->read_domains = obj->base.read_domains;
	err->write_domain = obj->base.write_domain;
	err->fence_reg = obj->fence_reg;
	err->pinned = 0;
	if (obj->pin_count > 0)
		err->pinned = 1;
	if (obj->user_pin_count > 0)
		err->pinned = -1;
	err->tiling = obj->tiling_mode;
	err->dirty = obj->dirty;
	err->purgeable = obj->madv != I915_MADV_WILLNEED;
	err->ring = obj->ring ? obj->ring->id : -1;
	err->cache_level = obj->cache_level;
}

static u32 capture_active_bo(struct drm_i915_error_buffer *err,
			     int count, struct list_head *head)
{
	struct drm_i915_gem_object *obj;
	int i = 0;

	list_for_each_entry(obj, head, mm_list) {
		capture_bo(err++, obj);
		if (++i == count)
			break;
	}

	return i;
}

static u32 capture_pinned_bo(struct drm_i915_error_buffer *err,
			     int count, struct list_head *head)
{
	struct drm_i915_gem_object *obj;
	int i = 0;

	list_for_each_entry(obj, head, global_list) {
		if (obj->pin_count == 0)
			continue;

		capture_bo(err++, obj);
		if (++i == count)
			break;
	}

	return i;
}

static void i915_gem_record_fences(struct drm_device *dev,
				   struct drm_i915_error_state *error)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int i;

	/* Fences */
	switch (INTEL_INFO(dev)->gen) {
	case 7:
	case 6:
		for (i = 0; i < dev_priv->num_fence_regs; i++)
			error->fence[i] = I915_READ64(FENCE_REG_SANDYBRIDGE_0 + (i * 8));
		break;
	case 5:
	case 4:
		for (i = 0; i < 16; i++)
			error->fence[i] = I915_READ64(FENCE_REG_965_0 + (i * 8));
		break;
	case 3:
		if (IS_I945G(dev) || IS_I945GM(dev) || IS_G33(dev))
			for (i = 0; i < 8; i++)
				error->fence[i+8] = I915_READ(FENCE_REG_945_8 + (i * 4));
	case 2:
		for (i = 0; i < 8; i++)
			error->fence[i] = I915_READ(FENCE_REG_830_0 + (i * 4));
		break;

	default:
		BUG();
	}
}

static struct drm_i915_error_object *
i915_error_first_batchbuffer(struct drm_i915_private *dev_priv,
			     struct intel_ring_buffer *ring)
{
	struct i915_address_space *vm = &dev_priv->gtt.base;
	struct drm_i915_gem_object *obj;
	u32 seqno;

	if (!ring->get_seqno)
		return NULL;

	if (HAS_BROKEN_CS_TLB(dev_priv->dev)) {
		u32 acthd = I915_READ(ACTHD);

		if (WARN_ON(ring->id != RCS))
			return NULL;

		obj = ring->private;
		if (acthd >= i915_gem_obj_ggtt_offset(obj) &&
		    acthd < i915_gem_obj_ggtt_offset(obj) + obj->base.size)
			return i915_error_object_create(dev_priv, obj);
	}

	seqno = ring->get_seqno(ring, false);
	list_for_each_entry(obj, &vm->active_list, mm_list) {
		if (obj->ring != ring)
			continue;

		if (i915_seqno_passed(seqno, obj->last_read_seqno))
			continue;

		if ((obj->base.read_domains & I915_GEM_DOMAIN_COMMAND) == 0)
			continue;

		/* We need to copy these to an anonymous buffer as the simplest
		 * method to avoid being overwritten by userspace.
		 */
		return i915_error_object_create(dev_priv, obj);
	}

	return NULL;
}

static void i915_record_ring_state(struct drm_device *dev,
				   struct drm_i915_error_state *error,
				   struct intel_ring_buffer *ring)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (INTEL_INFO(dev)->gen >= 6) {
		error->rc_psmi[ring->id] = I915_READ(ring->mmio_base + 0x50);
		error->fault_reg[ring->id] = I915_READ(RING_FAULT_REG(ring));
		error->semaphore_mboxes[ring->id][0]
			= I915_READ(RING_SYNC_0(ring->mmio_base));
		error->semaphore_mboxes[ring->id][1]
			= I915_READ(RING_SYNC_1(ring->mmio_base));
		error->semaphore_seqno[ring->id][0] = ring->sync_seqno[0];
		error->semaphore_seqno[ring->id][1] = ring->sync_seqno[1];
	}

	if (INTEL_INFO(dev)->gen >= 4) {
		error->faddr[ring->id] = I915_READ(RING_DMA_FADD(ring->mmio_base));
		error->ipeir[ring->id] = I915_READ(RING_IPEIR(ring->mmio_base));
		error->ipehr[ring->id] = I915_READ(RING_IPEHR(ring->mmio_base));
		error->instdone[ring->id] = I915_READ(RING_INSTDONE(ring->mmio_base));
		error->instps[ring->id] = I915_READ(RING_INSTPS(ring->mmio_base));
		if (ring->id == RCS)
			error->bbaddr = I915_READ64(BB_ADDR);
	} else {
		error->faddr[ring->id] = I915_READ(DMA_FADD_I8XX);
		error->ipeir[ring->id] = I915_READ(IPEIR);
		error->ipehr[ring->id] = I915_READ(IPEHR);
		error->instdone[ring->id] = I915_READ(INSTDONE);
	}

	error->waiting[ring->id] = waitqueue_active(&ring->irq_queue);
	error->instpm[ring->id] = I915_READ(RING_INSTPM(ring->mmio_base));
	error->seqno[ring->id] = ring->get_seqno(ring, false);
	error->acthd[ring->id] = intel_ring_get_active_head(ring);
	error->head[ring->id] = I915_READ_HEAD(ring);
	error->tail[ring->id] = I915_READ_TAIL(ring);
	error->ctl[ring->id] = I915_READ_CTL(ring);

	error->cpu_ring_head[ring->id] = ring->head;
	error->cpu_ring_tail[ring->id] = ring->tail;
}


static void i915_gem_record_active_context(struct intel_ring_buffer *ring,
					   struct drm_i915_error_state *error,
					   struct drm_i915_error_ring *ering)
{
	struct drm_i915_private *dev_priv = ring->dev->dev_private;
	struct drm_i915_gem_object *obj;

	/* Currently render ring is the only HW context user */
	if (ring->id != RCS || !error->ccid)
		return;

	list_for_each_entry(obj, &dev_priv->mm.bound_list, global_list) {
		if ((error->ccid & PAGE_MASK) == i915_gem_obj_ggtt_offset(obj)) {
			ering->ctx = i915_error_object_create_sized(dev_priv,
								    obj, 1);
			break;
		}
	}
}

static void i915_gem_record_rings(struct drm_device *dev,
				  struct drm_i915_error_state *error)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_ring_buffer *ring;
	struct drm_i915_gem_request *request;
	int i, count;

	for_each_ring(ring, dev_priv, i) {
		i915_record_ring_state(dev, error, ring);

		error->ring[i].batchbuffer =
			i915_error_first_batchbuffer(dev_priv, ring);

		error->ring[i].ringbuffer =
			i915_error_object_create(dev_priv, ring->obj);


		i915_gem_record_active_context(ring, error, &error->ring[i]);

		count = 0;
		list_for_each_entry(request, &ring->request_list, list)
			count++;

		error->ring[i].num_requests = count;
		error->ring[i].requests =
			kmalloc(count*sizeof(struct drm_i915_error_request),
				GFP_ATOMIC);
		if (error->ring[i].requests == NULL) {
			error->ring[i].num_requests = 0;
			continue;
		}

		count = 0;
		list_for_each_entry(request, &ring->request_list, list) {
			struct drm_i915_error_request *erq;

			erq = &error->ring[i].requests[count++];
			erq->seqno = request->seqno;
			erq->jiffies = request->emitted_jiffies;
			erq->tail = request->tail;
		}
	}
}

static void i915_gem_capture_buffers(struct drm_i915_private *dev_priv,
				     struct drm_i915_error_state *error)
{
	struct i915_address_space *vm = &dev_priv->gtt.base;
	struct drm_i915_gem_object *obj;
	int i;

	i = 0;
	list_for_each_entry(obj, &vm->active_list, mm_list)
		i++;
	error->active_bo_count = i;
	list_for_each_entry(obj, &dev_priv->mm.bound_list, global_list)
		if (obj->pin_count)
			i++;
	error->pinned_bo_count = i - error->active_bo_count;

	if (i) {
		error->active_bo = kmalloc(sizeof(*error->active_bo)*i,
					   GFP_ATOMIC);
		if (error->active_bo)
			error->pinned_bo =
				error->active_bo + error->active_bo_count;
	}

	if (error->active_bo)
		error->active_bo_count =
			capture_active_bo(error->active_bo,
					  error->active_bo_count,
					  &vm->active_list);

	if (error->pinned_bo)
		error->pinned_bo_count =
			capture_pinned_bo(error->pinned_bo,
					  error->pinned_bo_count,
					  &dev_priv->mm.bound_list);
}

/**
 * i915_capture_error_state - capture an error record for later analysis
 * @dev: drm device
 *
 * Should be called when an error is detected (either a hang or an error
 * interrupt) to capture error state from the time of the error.  Fills
 * out a structure which becomes available in debugfs for user level tools
 * to pick up.
 */
void i915_capture_error_state(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_error_state *error;
	unsigned long flags;
	int pipe;

	spin_lock_irqsave(&dev_priv->gpu_error.lock, flags);
	error = dev_priv->gpu_error.first_error;
	spin_unlock_irqrestore(&dev_priv->gpu_error.lock, flags);
	if (error)
		return;

	/* Account for pipe specific data like PIPE*STAT */
	error = kzalloc(sizeof(*error), GFP_ATOMIC);
	if (!error) {
		DRM_DEBUG_DRIVER("out of memory, not capturing error state\n");
		return;
	}

	DRM_INFO("capturing error event; look for more information in "
		 "/sys/class/drm/card%d/error\n", dev->primary->index);

	kref_init(&error->ref);
	error->eir = I915_READ(EIR);
	error->pgtbl_er = I915_READ(PGTBL_ER);
	if (HAS_HW_CONTEXTS(dev))
		error->ccid = I915_READ(CCID);

	if (HAS_PCH_SPLIT(dev))
		error->ier = I915_READ(DEIER) | I915_READ(GTIER);
	else if (IS_VALLEYVIEW(dev))
		error->ier = I915_READ(GTIER) | I915_READ(VLV_IER);
	else if (IS_GEN2(dev))
		error->ier = I915_READ16(IER);
	else
		error->ier = I915_READ(IER);

	if (INTEL_INFO(dev)->gen >= 6)
		error->derrmr = I915_READ(DERRMR);

	if (IS_VALLEYVIEW(dev))
		error->forcewake = I915_READ(FORCEWAKE_VLV);
	else if (INTEL_INFO(dev)->gen >= 7)
		error->forcewake = I915_READ(FORCEWAKE_MT);
	else if (INTEL_INFO(dev)->gen == 6)
		error->forcewake = I915_READ(FORCEWAKE);

	if (!HAS_PCH_SPLIT(dev))
		for_each_pipe(pipe)
			error->pipestat[pipe] = I915_READ(PIPESTAT(pipe));

	if (INTEL_INFO(dev)->gen >= 6) {
		error->error = I915_READ(ERROR_GEN6);
		error->done_reg = I915_READ(DONE_REG);
	}

	if (INTEL_INFO(dev)->gen == 7)
		error->err_int = I915_READ(GEN7_ERR_INT);

	i915_get_extra_instdone(dev, error->extra_instdone);

	i915_gem_capture_buffers(dev_priv, error);
	i915_gem_record_fences(dev, error);
	i915_gem_record_rings(dev, error);

	do_gettimeofday(&error->time);

	error->overlay = intel_overlay_capture_error_state(dev);
	error->display = intel_display_capture_error_state(dev);

	spin_lock_irqsave(&dev_priv->gpu_error.lock, flags);
	if (dev_priv->gpu_error.first_error == NULL) {
		dev_priv->gpu_error.first_error = error;
		error = NULL;
	}
	spin_unlock_irqrestore(&dev_priv->gpu_error.lock, flags);

	if (error)
		i915_error_state_free(&error->ref);
}

void i915_error_state_get(struct drm_device *dev,
			  struct i915_error_state_file_priv *error_priv)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long flags;

	spin_lock_irqsave(&dev_priv->gpu_error.lock, flags);
	error_priv->error = dev_priv->gpu_error.first_error;
	if (error_priv->error)
		kref_get(&error_priv->error->ref);
	spin_unlock_irqrestore(&dev_priv->gpu_error.lock, flags);

}

void i915_error_state_put(struct i915_error_state_file_priv *error_priv)
{
	if (error_priv->error)
		kref_put(&error_priv->error->ref, i915_error_state_free);
}

void i915_destroy_error_state(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_error_state *error;
	unsigned long flags;

	spin_lock_irqsave(&dev_priv->gpu_error.lock, flags);
	error = dev_priv->gpu_error.first_error;
	dev_priv->gpu_error.first_error = NULL;
	spin_unlock_irqrestore(&dev_priv->gpu_error.lock, flags);

	if (error)
		kref_put(&error->ref, i915_error_state_free);
}

const char *i915_cache_level_str(int type)
{
	switch (type) {
	case I915_CACHE_NONE: return " uncached";
	case I915_CACHE_LLC: return " snooped (LLC)";
	case I915_CACHE_LLC_MLC: return " snooped (LLC+MLC)";
	default: return "";
	}
}

/* NB: please notice the memset */
void i915_get_extra_instdone(struct drm_device *dev, uint32_t *instdone)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	memset(instdone, 0, sizeof(*instdone) * I915_NUM_INSTDONE_REG);

	switch (INTEL_INFO(dev)->gen) {
	case 2:
	case 3:
		instdone[0] = I915_READ(INSTDONE);
		break;
	case 4:
	case 5:
	case 6:
		instdone[0] = I915_READ(INSTDONE_I965);
		instdone[1] = I915_READ(INSTDONE1);
		break;
	default:
		WARN_ONCE(1, "Unsupported platform\n");
	case 7:
		instdone[0] = I915_READ(GEN7_INSTDONE_1);
		instdone[1] = I915_READ(GEN7_SC_INSTDONE);
		instdone[2] = I915_READ(GEN7_SAMPLER_INSTDONE);
		instdone[3] = I915_READ(GEN7_ROW_INSTDONE);
		break;
	}
}
