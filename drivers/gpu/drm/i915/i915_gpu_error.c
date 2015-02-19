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
	case VCS2: return "bsd2";
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
		va_list tmp;

		va_copy(tmp, args);
		len = vsnprintf(NULL, 0, f, tmp);
		va_end(tmp);

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
	err_printf(m, "  %s [%d]:\n", name, count);

	while (count--) {
		err_printf(m, "    %08x %8u %02x %02x %x %x",
			   err->gtt_offset,
			   err->size,
			   err->read_domains,
			   err->write_domain,
			   err->rseqno, err->wseqno);
		err_puts(m, pin_flag(err->pinned));
		err_puts(m, tiling_flag(err->tiling));
		err_puts(m, dirty_flag(err->dirty));
		err_puts(m, purgeable_flag(err->purgeable));
		err_puts(m, err->userptr ? " userptr" : "");
		err_puts(m, err->ring != -1 ? " " : "");
		err_puts(m, ring_str(err->ring));
		err_puts(m, i915_cache_level_str(m->i915, err->cache_level));

		if (err->name)
			err_printf(m, " (name: %d)", err->name);
		if (err->fence_reg != I915_FENCE_REG_NONE)
			err_printf(m, " (fence: %d)", err->fence_reg);

		err_puts(m, "\n");
		err++;
	}
}

static const char *hangcheck_action_to_str(enum intel_ring_hangcheck_action a)
{
	switch (a) {
	case HANGCHECK_IDLE:
		return "idle";
	case HANGCHECK_WAIT:
		return "wait";
	case HANGCHECK_ACTIVE:
		return "active";
	case HANGCHECK_ACTIVE_LOOP:
		return "active (loop)";
	case HANGCHECK_KICK:
		return "kick";
	case HANGCHECK_HUNG:
		return "hung";
	}

	return "unknown";
}

static void i915_ring_error_state(struct drm_i915_error_state_buf *m,
				  struct drm_device *dev,
				  struct drm_i915_error_state *error,
				  int ring_idx)
{
	struct drm_i915_error_ring *ring = &error->ring[ring_idx];

	if (!ring->valid)
		return;

	err_printf(m, "%s command stream:\n", ring_str(ring_idx));
	err_printf(m, "  HEAD: 0x%08x\n", ring->head);
	err_printf(m, "  TAIL: 0x%08x\n", ring->tail);
	err_printf(m, "  CTL: 0x%08x\n", ring->ctl);
	err_printf(m, "  HWS: 0x%08x\n", ring->hws);
	err_printf(m, "  ACTHD: 0x%08x %08x\n", (u32)(ring->acthd>>32), (u32)ring->acthd);
	err_printf(m, "  IPEIR: 0x%08x\n", ring->ipeir);
	err_printf(m, "  IPEHR: 0x%08x\n", ring->ipehr);
	err_printf(m, "  INSTDONE: 0x%08x\n", ring->instdone);
	if (INTEL_INFO(dev)->gen >= 4) {
		err_printf(m, "  BBADDR: 0x%08x %08x\n", (u32)(ring->bbaddr>>32), (u32)ring->bbaddr);
		err_printf(m, "  BB_STATE: 0x%08x\n", ring->bbstate);
		err_printf(m, "  INSTPS: 0x%08x\n", ring->instps);
	}
	err_printf(m, "  INSTPM: 0x%08x\n", ring->instpm);
	err_printf(m, "  FADDR: 0x%08x %08x\n", upper_32_bits(ring->faddr),
		   lower_32_bits(ring->faddr));
	if (INTEL_INFO(dev)->gen >= 6) {
		err_printf(m, "  RC PSMI: 0x%08x\n", ring->rc_psmi);
		err_printf(m, "  FAULT_REG: 0x%08x\n", ring->fault_reg);
		err_printf(m, "  SYNC_0: 0x%08x [last synced 0x%08x]\n",
			   ring->semaphore_mboxes[0],
			   ring->semaphore_seqno[0]);
		err_printf(m, "  SYNC_1: 0x%08x [last synced 0x%08x]\n",
			   ring->semaphore_mboxes[1],
			   ring->semaphore_seqno[1]);
		if (HAS_VEBOX(dev)) {
			err_printf(m, "  SYNC_2: 0x%08x [last synced 0x%08x]\n",
				   ring->semaphore_mboxes[2],
				   ring->semaphore_seqno[2]);
		}
	}
	if (USES_PPGTT(dev)) {
		err_printf(m, "  GFX_MODE: 0x%08x\n", ring->vm_info.gfx_mode);

		if (INTEL_INFO(dev)->gen >= 8) {
			int i;
			for (i = 0; i < 4; i++)
				err_printf(m, "  PDP%d: 0x%016llx\n",
					   i, ring->vm_info.pdp[i]);
		} else {
			err_printf(m, "  PP_DIR_BASE: 0x%08x\n",
				   ring->vm_info.pp_dir_base);
		}
	}
	err_printf(m, "  seqno: 0x%08x\n", ring->seqno);
	err_printf(m, "  waiting: %s\n", yesno(ring->waiting));
	err_printf(m, "  ring->head: 0x%08x\n", ring->cpu_ring_head);
	err_printf(m, "  ring->tail: 0x%08x\n", ring->cpu_ring_tail);
	err_printf(m, "  hangcheck: %s [%d]\n",
		   hangcheck_action_to_str(ring->hangcheck_action),
		   ring->hangcheck_score);
}

void i915_error_printf(struct drm_i915_error_state_buf *e, const char *f, ...)
{
	va_list args;

	va_start(args, f);
	i915_error_vprintf(e, f, args);
	va_end(args);
}

static void print_error_obj(struct drm_i915_error_state_buf *m,
			    struct drm_i915_error_object *obj)
{
	int page, offset, elt;

	for (page = offset = 0; page < obj->page_count; page++) {
		for (elt = 0; elt < PAGE_SIZE/4; elt++) {
			err_printf(m, "%08x :  %08x\n", offset,
				   obj->pages[page][elt]);
			offset += 4;
		}
	}
}

int i915_error_state_to_str(struct drm_i915_error_state_buf *m,
			    const struct i915_error_state_file_priv *error_priv)
{
	struct drm_device *dev = error_priv->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_error_state *error = error_priv->error;
	struct drm_i915_error_object *obj;
	int i, j, offset, elt;
	int max_hangcheck_score;

	if (!error) {
		err_printf(m, "no error state collected\n");
		goto out;
	}

	err_printf(m, "%s\n", error->error_msg);
	err_printf(m, "Time: %ld s %ld us\n", error->time.tv_sec,
		   error->time.tv_usec);
	err_printf(m, "Kernel: " UTS_RELEASE "\n");
	max_hangcheck_score = 0;
	for (i = 0; i < ARRAY_SIZE(error->ring); i++) {
		if (error->ring[i].hangcheck_score > max_hangcheck_score)
			max_hangcheck_score = error->ring[i].hangcheck_score;
	}
	for (i = 0; i < ARRAY_SIZE(error->ring); i++) {
		if (error->ring[i].hangcheck_score == max_hangcheck_score &&
		    error->ring[i].pid != -1) {
			err_printf(m, "Active process (on ring %s): %s [%d]\n",
				   ring_str(i),
				   error->ring[i].comm,
				   error->ring[i].pid);
		}
	}
	err_printf(m, "Reset count: %u\n", error->reset_count);
	err_printf(m, "Suspend count: %u\n", error->suspend_count);
	err_printf(m, "PCI ID: 0x%04x\n", dev->pdev->device);
	err_printf(m, "EIR: 0x%08x\n", error->eir);
	err_printf(m, "IER: 0x%08x\n", error->ier);
	if (INTEL_INFO(dev)->gen >= 8) {
		for (i = 0; i < 4; i++)
			err_printf(m, "GTIER gt %d: 0x%08x\n", i,
				   error->gtier[i]);
	} else if (HAS_PCH_SPLIT(dev) || IS_VALLEYVIEW(dev))
		err_printf(m, "GTIER: 0x%08x\n", error->gtier[0]);
	err_printf(m, "PGTBL_ER: 0x%08x\n", error->pgtbl_er);
	err_printf(m, "FORCEWAKE: 0x%08x\n", error->forcewake);
	err_printf(m, "DERRMR: 0x%08x\n", error->derrmr);
	err_printf(m, "CCID: 0x%08x\n", error->ccid);
	err_printf(m, "Missed interrupts: 0x%08lx\n", dev_priv->gpu_error.missed_irq_rings);

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

	for (i = 0; i < ARRAY_SIZE(error->ring); i++)
		i915_ring_error_state(m, dev, error, i);

	for (i = 0; i < error->vm_count; i++) {
		err_printf(m, "vm[%d]\n", i);

		print_error_buffers(m, "Active",
				    error->active_bo[i],
				    error->active_bo_count[i]);

		print_error_buffers(m, "Pinned",
				    error->pinned_bo[i],
				    error->pinned_bo_count[i]);
	}

	for (i = 0; i < ARRAY_SIZE(error->ring); i++) {
		obj = error->ring[i].batchbuffer;
		if (obj) {
			err_puts(m, dev_priv->ring[i].name);
			if (error->ring[i].pid != -1)
				err_printf(m, " (submitted by %s [%d])",
					   error->ring[i].comm,
					   error->ring[i].pid);
			err_printf(m, " --- gtt_offset = 0x%08x\n",
				   obj->gtt_offset);
			print_error_obj(m, obj);
		}

		obj = error->ring[i].wa_batchbuffer;
		if (obj) {
			err_printf(m, "%s (w/a) --- gtt_offset = 0x%08x\n",
				   dev_priv->ring[i].name, obj->gtt_offset);
			print_error_obj(m, obj);
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
			print_error_obj(m, obj);
		}

		if ((obj = error->ring[i].hws_page)) {
			err_printf(m, "%s --- HW Status = 0x%08x\n",
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

		if ((obj = error->ring[i].ctx)) {
			err_printf(m, "%s --- HW Context = 0x%08x\n",
				   dev_priv->ring[i].name,
				   obj->gtt_offset);
			print_error_obj(m, obj);
		}
	}

	if ((obj = error->semaphore_obj)) {
		err_printf(m, "Semaphore page = 0x%08x\n", obj->gtt_offset);
		for (elt = 0; elt < PAGE_SIZE/16; elt += 4) {
			err_printf(m, "[%04x] %08x %08x %08x %08x\n",
				   elt * 4,
				   obj->pages[0][elt],
				   obj->pages[0][elt+1],
				   obj->pages[0][elt+2],
				   obj->pages[0][elt+3]);
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
			      struct drm_i915_private *i915,
			      size_t count, loff_t pos)
{
	memset(ebuf, 0, sizeof(*ebuf));
	ebuf->i915 = i915;

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
		i915_error_object_free(error->ring[i].hws_page);
		i915_error_object_free(error->ring[i].ctx);
		kfree(error->ring[i].requests);
	}

	i915_error_object_free(error->semaphore_obj);
	kfree(error->active_bo);
	kfree(error->overlay);
	kfree(error->display);
	kfree(error);
}

static struct drm_i915_error_object *
i915_error_object_create(struct drm_i915_private *dev_priv,
			 struct drm_i915_gem_object *src,
			 struct i915_address_space *vm)
{
	struct drm_i915_error_object *dst;
	struct i915_vma *vma = NULL;
	int num_pages;
	bool use_ggtt;
	int i = 0;
	u32 reloc_offset;

	if (src == NULL || src->pages == NULL)
		return NULL;

	num_pages = src->base.size >> PAGE_SHIFT;

	dst = kmalloc(sizeof(*dst) + num_pages * sizeof(u32 *), GFP_ATOMIC);
	if (dst == NULL)
		return NULL;

	if (i915_gem_obj_bound(src, vm))
		dst->gtt_offset = i915_gem_obj_offset(src, vm);
	else
		dst->gtt_offset = -1;

	reloc_offset = dst->gtt_offset;
	if (i915_is_ggtt(vm))
		vma = i915_gem_obj_to_ggtt(src);
	use_ggtt = (src->cache_level == I915_CACHE_NONE &&
		   vma && (vma->bound & GLOBAL_BIND) &&
		   reloc_offset + num_pages * PAGE_SIZE <= dev_priv->gtt.mappable_end);

	/* Cannot access stolen address directly, try to use the aperture */
	if (src->stolen) {
		use_ggtt = true;

		if (!(vma && vma->bound & GLOBAL_BIND))
			goto unwind;

		reloc_offset = i915_gem_obj_ggtt_offset(src);
		if (reloc_offset + num_pages * PAGE_SIZE > dev_priv->gtt.mappable_end)
			goto unwind;
	}

	/* Cannot access snooped pages through the aperture */
	if (use_ggtt && src->cache_level != I915_CACHE_NONE && !HAS_LLC(dev_priv->dev))
		goto unwind;

	dst->page_count = num_pages;
	while (num_pages--) {
		unsigned long flags;
		void *d;

		d = kmalloc(PAGE_SIZE, GFP_ATOMIC);
		if (d == NULL)
			goto unwind;

		local_irq_save(flags);
		if (use_ggtt) {
			void __iomem *s;

			/* Simply ignore tiling or any overlapping fence.
			 * It's part of the error state, and this hopefully
			 * captures what the GPU read.
			 */

			s = io_mapping_map_atomic_wc(dev_priv->gtt.mappable,
						     reloc_offset);
			memcpy_fromio(d, s, PAGE_SIZE);
			io_mapping_unmap_atomic(s);
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

		dst->pages[i++] = d;
		reloc_offset += PAGE_SIZE;
	}

	return dst;

unwind:
	while (i--)
		kfree(dst->pages[i]);
	kfree(dst);
	return NULL;
}
#define i915_error_ggtt_object_create(dev_priv, src) \
	i915_error_object_create((dev_priv), (src), &(dev_priv)->gtt.base)

static void capture_bo(struct drm_i915_error_buffer *err,
		       struct i915_vma *vma)
{
	struct drm_i915_gem_object *obj = vma->obj;

	err->size = obj->base.size;
	err->name = obj->base.name;
	err->rseqno = i915_gem_request_get_seqno(obj->last_read_req);
	err->wseqno = i915_gem_request_get_seqno(obj->last_write_req);
	err->gtt_offset = vma->node.start;
	err->read_domains = obj->base.read_domains;
	err->write_domain = obj->base.write_domain;
	err->fence_reg = obj->fence_reg;
	err->pinned = 0;
	if (i915_gem_obj_is_pinned(obj))
		err->pinned = 1;
	err->tiling = obj->tiling_mode;
	err->dirty = obj->dirty;
	err->purgeable = obj->madv != I915_MADV_WILLNEED;
	err->userptr = obj->userptr.mm != NULL;
	err->ring = obj->last_read_req ?
			i915_gem_request_get_ring(obj->last_read_req)->id : -1;
	err->cache_level = obj->cache_level;
}

static u32 capture_active_bo(struct drm_i915_error_buffer *err,
			     int count, struct list_head *head)
{
	struct i915_vma *vma;
	int i = 0;

	list_for_each_entry(vma, head, mm_list) {
		capture_bo(err++, vma);
		if (++i == count)
			break;
	}

	return i;
}

static u32 capture_pinned_bo(struct drm_i915_error_buffer *err,
			     int count, struct list_head *head,
			     struct i915_address_space *vm)
{
	struct drm_i915_gem_object *obj;
	struct drm_i915_error_buffer * const first = err;
	struct drm_i915_error_buffer * const last = err + count;

	list_for_each_entry(obj, head, global_list) {
		struct i915_vma *vma;

		if (err == last)
			break;

		list_for_each_entry(vma, &obj->vma_list, vma_link)
			if (vma->vm == vm && vma->pin_count > 0)
				capture_bo(err++, vma);
	}

	return err - first;
}

/* Generate a semi-unique error code. The code is not meant to have meaning, The
 * code's only purpose is to try to prevent false duplicated bug reports by
 * grossly estimating a GPU error state.
 *
 * TODO Ideally, hashing the batchbuffer would be a very nice way to determine
 * the hang if we could strip the GTT offset information from it.
 *
 * It's only a small step better than a random number in its current form.
 */
static uint32_t i915_error_generate_code(struct drm_i915_private *dev_priv,
					 struct drm_i915_error_state *error,
					 int *ring_id)
{
	uint32_t error_code = 0;
	int i;

	/* IPEHR would be an ideal way to detect errors, as it's the gross
	 * measure of "the command that hung." However, has some very common
	 * synchronization commands which almost always appear in the case
	 * strictly a client bug. Use instdone to differentiate those some.
	 */
	for (i = 0; i < I915_NUM_RINGS; i++) {
		if (error->ring[i].hangcheck_action == HANGCHECK_HUNG) {
			if (ring_id)
				*ring_id = i;

			return error->ring[i].ipehr ^ error->ring[i].instdone;
		}
	}

	return error_code;
}

static void i915_gem_record_fences(struct drm_device *dev,
				   struct drm_i915_error_state *error)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int i;

	if (IS_GEN3(dev) || IS_GEN2(dev)) {
		for (i = 0; i < 8; i++)
			error->fence[i] = I915_READ(FENCE_REG_830_0 + (i * 4));
		if (IS_I945G(dev) || IS_I945GM(dev) || IS_G33(dev))
			for (i = 0; i < 8; i++)
				error->fence[i+8] = I915_READ(FENCE_REG_945_8 +
							      (i * 4));
	} else if (IS_GEN5(dev) || IS_GEN4(dev))
		for (i = 0; i < 16; i++)
			error->fence[i] = I915_READ64(FENCE_REG_965_0 +
						      (i * 8));
	else if (INTEL_INFO(dev)->gen >= 6)
		for (i = 0; i < dev_priv->num_fence_regs; i++)
			error->fence[i] = I915_READ64(FENCE_REG_SANDYBRIDGE_0 +
						      (i * 8));
}


static void gen8_record_semaphore_state(struct drm_i915_private *dev_priv,
					struct drm_i915_error_state *error,
					struct intel_engine_cs *ring,
					struct drm_i915_error_ring *ering)
{
	struct intel_engine_cs *to;
	int i;

	if (!i915_semaphore_is_enabled(dev_priv->dev))
		return;

	if (!error->semaphore_obj)
		error->semaphore_obj =
			i915_error_ggtt_object_create(dev_priv,
						      dev_priv->semaphore_obj);

	for_each_ring(to, dev_priv, i) {
		int idx;
		u16 signal_offset;
		u32 *tmp;

		if (ring == to)
			continue;

		signal_offset = (GEN8_SIGNAL_OFFSET(ring, i) & (PAGE_SIZE - 1))
				/ 4;
		tmp = error->semaphore_obj->pages[0];
		idx = intel_ring_sync_index(ring, to);

		ering->semaphore_mboxes[idx] = tmp[signal_offset];
		ering->semaphore_seqno[idx] = ring->semaphore.sync_seqno[idx];
	}
}

static void gen6_record_semaphore_state(struct drm_i915_private *dev_priv,
					struct intel_engine_cs *ring,
					struct drm_i915_error_ring *ering)
{
	ering->semaphore_mboxes[0] = I915_READ(RING_SYNC_0(ring->mmio_base));
	ering->semaphore_mboxes[1] = I915_READ(RING_SYNC_1(ring->mmio_base));
	ering->semaphore_seqno[0] = ring->semaphore.sync_seqno[0];
	ering->semaphore_seqno[1] = ring->semaphore.sync_seqno[1];

	if (HAS_VEBOX(dev_priv->dev)) {
		ering->semaphore_mboxes[2] =
			I915_READ(RING_SYNC_2(ring->mmio_base));
		ering->semaphore_seqno[2] = ring->semaphore.sync_seqno[2];
	}
}

static void i915_record_ring_state(struct drm_device *dev,
				   struct drm_i915_error_state *error,
				   struct intel_engine_cs *ring,
				   struct drm_i915_error_ring *ering)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (INTEL_INFO(dev)->gen >= 6) {
		ering->rc_psmi = I915_READ(ring->mmio_base + 0x50);
		ering->fault_reg = I915_READ(RING_FAULT_REG(ring));
		if (INTEL_INFO(dev)->gen >= 8)
			gen8_record_semaphore_state(dev_priv, error, ring, ering);
		else
			gen6_record_semaphore_state(dev_priv, ring, ering);
	}

	if (INTEL_INFO(dev)->gen >= 4) {
		ering->faddr = I915_READ(RING_DMA_FADD(ring->mmio_base));
		ering->ipeir = I915_READ(RING_IPEIR(ring->mmio_base));
		ering->ipehr = I915_READ(RING_IPEHR(ring->mmio_base));
		ering->instdone = I915_READ(RING_INSTDONE(ring->mmio_base));
		ering->instps = I915_READ(RING_INSTPS(ring->mmio_base));
		ering->bbaddr = I915_READ(RING_BBADDR(ring->mmio_base));
		if (INTEL_INFO(dev)->gen >= 8) {
			ering->faddr |= (u64) I915_READ(RING_DMA_FADD_UDW(ring->mmio_base)) << 32;
			ering->bbaddr |= (u64) I915_READ(RING_BBADDR_UDW(ring->mmio_base)) << 32;
		}
		ering->bbstate = I915_READ(RING_BBSTATE(ring->mmio_base));
	} else {
		ering->faddr = I915_READ(DMA_FADD_I8XX);
		ering->ipeir = I915_READ(IPEIR);
		ering->ipehr = I915_READ(IPEHR);
		ering->instdone = I915_READ(INSTDONE);
	}

	ering->waiting = waitqueue_active(&ring->irq_queue);
	ering->instpm = I915_READ(RING_INSTPM(ring->mmio_base));
	ering->seqno = ring->get_seqno(ring, false);
	ering->acthd = intel_ring_get_active_head(ring);
	ering->head = I915_READ_HEAD(ring);
	ering->tail = I915_READ_TAIL(ring);
	ering->ctl = I915_READ_CTL(ring);

	if (I915_NEED_GFX_HWS(dev)) {
		int mmio;

		if (IS_GEN7(dev)) {
			switch (ring->id) {
			default:
			case RCS:
				mmio = RENDER_HWS_PGA_GEN7;
				break;
			case BCS:
				mmio = BLT_HWS_PGA_GEN7;
				break;
			case VCS:
				mmio = BSD_HWS_PGA_GEN7;
				break;
			case VECS:
				mmio = VEBOX_HWS_PGA_GEN7;
				break;
			}
		} else if (IS_GEN6(ring->dev)) {
			mmio = RING_HWS_PGA_GEN6(ring->mmio_base);
		} else {
			/* XXX: gen8 returns to sanity */
			mmio = RING_HWS_PGA(ring->mmio_base);
		}

		ering->hws = I915_READ(mmio);
	}

	ering->hangcheck_score = ring->hangcheck.score;
	ering->hangcheck_action = ring->hangcheck.action;

	if (USES_PPGTT(dev)) {
		int i;

		ering->vm_info.gfx_mode = I915_READ(RING_MODE_GEN7(ring));

		if (IS_GEN6(dev))
			ering->vm_info.pp_dir_base =
				I915_READ(RING_PP_DIR_BASE_READ(ring));
		else if (IS_GEN7(dev))
			ering->vm_info.pp_dir_base =
				I915_READ(RING_PP_DIR_BASE(ring));
		else if (INTEL_INFO(dev)->gen >= 8)
			for (i = 0; i < 4; i++) {
				ering->vm_info.pdp[i] =
					I915_READ(GEN8_RING_PDP_UDW(ring, i));
				ering->vm_info.pdp[i] <<= 32;
				ering->vm_info.pdp[i] |=
					I915_READ(GEN8_RING_PDP_LDW(ring, i));
			}
	}
}


static void i915_gem_record_active_context(struct intel_engine_cs *ring,
					   struct drm_i915_error_state *error,
					   struct drm_i915_error_ring *ering)
{
	struct drm_i915_private *dev_priv = ring->dev->dev_private;
	struct drm_i915_gem_object *obj;

	/* Currently render ring is the only HW context user */
	if (ring->id != RCS || !error->ccid)
		return;

	list_for_each_entry(obj, &dev_priv->mm.bound_list, global_list) {
		if (!i915_gem_obj_ggtt_bound(obj))
			continue;

		if ((error->ccid & PAGE_MASK) == i915_gem_obj_ggtt_offset(obj)) {
			ering->ctx = i915_error_ggtt_object_create(dev_priv, obj);
			break;
		}
	}
}

static void i915_gem_record_rings(struct drm_device *dev,
				  struct drm_i915_error_state *error)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_gem_request *request;
	int i, count;

	for (i = 0; i < I915_NUM_RINGS; i++) {
		struct intel_engine_cs *ring = &dev_priv->ring[i];
		struct intel_ringbuffer *rbuf;

		error->ring[i].pid = -1;

		if (ring->dev == NULL)
			continue;

		error->ring[i].valid = true;

		i915_record_ring_state(dev, error, ring, &error->ring[i]);

		request = i915_gem_find_active_request(ring);
		if (request) {
			struct i915_address_space *vm;

			vm = request->ctx && request->ctx->ppgtt ?
				&request->ctx->ppgtt->base :
				&dev_priv->gtt.base;

			/* We need to copy these to an anonymous buffer
			 * as the simplest method to avoid being overwritten
			 * by userspace.
			 */
			error->ring[i].batchbuffer =
				i915_error_object_create(dev_priv,
							 request->batch_obj,
							 vm);

			if (HAS_BROKEN_CS_TLB(dev_priv->dev))
				error->ring[i].wa_batchbuffer =
					i915_error_ggtt_object_create(dev_priv,
							     ring->scratch.obj);

			if (request->file_priv) {
				struct task_struct *task;

				rcu_read_lock();
				task = pid_task(request->file_priv->file->pid,
						PIDTYPE_PID);
				if (task) {
					strcpy(error->ring[i].comm, task->comm);
					error->ring[i].pid = task->pid;
				}
				rcu_read_unlock();
			}
		}

		if (i915.enable_execlists) {
			/* TODO: This is only a small fix to keep basic error
			 * capture working, but we need to add more information
			 * for it to be useful (e.g. dump the context being
			 * executed).
			 */
			if (request)
				rbuf = request->ctx->engine[ring->id].ringbuf;
			else
				rbuf = ring->default_context->engine[ring->id].ringbuf;
		} else
			rbuf = ring->buffer;

		error->ring[i].cpu_ring_head = rbuf->head;
		error->ring[i].cpu_ring_tail = rbuf->tail;

		error->ring[i].ringbuffer =
			i915_error_ggtt_object_create(dev_priv, rbuf->obj);

		error->ring[i].hws_page =
			i915_error_ggtt_object_create(dev_priv, ring->status_page.obj);

		i915_gem_record_active_context(ring, error, &error->ring[i]);

		count = 0;
		list_for_each_entry(request, &ring->request_list, list)
			count++;

		error->ring[i].num_requests = count;
		error->ring[i].requests =
			kcalloc(count, sizeof(*error->ring[i].requests),
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
			erq->tail = request->postfix;
		}
	}
}

/* FIXME: Since pin count/bound list is global, we duplicate what we capture per
 * VM.
 */
static void i915_gem_capture_vm(struct drm_i915_private *dev_priv,
				struct drm_i915_error_state *error,
				struct i915_address_space *vm,
				const int ndx)
{
	struct drm_i915_error_buffer *active_bo = NULL, *pinned_bo = NULL;
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	int i;

	i = 0;
	list_for_each_entry(vma, &vm->active_list, mm_list)
		i++;
	error->active_bo_count[ndx] = i;

	list_for_each_entry(obj, &dev_priv->mm.bound_list, global_list) {
		list_for_each_entry(vma, &obj->vma_list, vma_link)
			if (vma->vm == vm && vma->pin_count > 0)
				i++;
	}
	error->pinned_bo_count[ndx] = i - error->active_bo_count[ndx];

	if (i) {
		active_bo = kcalloc(i, sizeof(*active_bo), GFP_ATOMIC);
		if (active_bo)
			pinned_bo = active_bo + error->active_bo_count[ndx];
	}

	if (active_bo)
		error->active_bo_count[ndx] =
			capture_active_bo(active_bo,
					  error->active_bo_count[ndx],
					  &vm->active_list);

	if (pinned_bo)
		error->pinned_bo_count[ndx] =
			capture_pinned_bo(pinned_bo,
					  error->pinned_bo_count[ndx],
					  &dev_priv->mm.bound_list, vm);
	error->active_bo[ndx] = active_bo;
	error->pinned_bo[ndx] = pinned_bo;
}

static void i915_gem_capture_buffers(struct drm_i915_private *dev_priv,
				     struct drm_i915_error_state *error)
{
	struct i915_address_space *vm;
	int cnt = 0, i = 0;

	list_for_each_entry(vm, &dev_priv->vm_list, global_link)
		cnt++;

	error->active_bo = kcalloc(cnt, sizeof(*error->active_bo), GFP_ATOMIC);
	error->pinned_bo = kcalloc(cnt, sizeof(*error->pinned_bo), GFP_ATOMIC);
	error->active_bo_count = kcalloc(cnt, sizeof(*error->active_bo_count),
					 GFP_ATOMIC);
	error->pinned_bo_count = kcalloc(cnt, sizeof(*error->pinned_bo_count),
					 GFP_ATOMIC);

	if (error->active_bo == NULL ||
	    error->pinned_bo == NULL ||
	    error->active_bo_count == NULL ||
	    error->pinned_bo_count == NULL) {
		kfree(error->active_bo);
		kfree(error->active_bo_count);
		kfree(error->pinned_bo);
		kfree(error->pinned_bo_count);

		error->active_bo = NULL;
		error->active_bo_count = NULL;
		error->pinned_bo = NULL;
		error->pinned_bo_count = NULL;
	} else {
		list_for_each_entry(vm, &dev_priv->vm_list, global_link)
			i915_gem_capture_vm(dev_priv, error, vm, i++);

		error->vm_count = cnt;
	}
}

/* Capture all registers which don't fit into another category. */
static void i915_capture_reg_state(struct drm_i915_private *dev_priv,
				   struct drm_i915_error_state *error)
{
	struct drm_device *dev = dev_priv->dev;
	int i;

	/* General organization
	 * 1. Registers specific to a single generation
	 * 2. Registers which belong to multiple generations
	 * 3. Feature specific registers.
	 * 4. Everything else
	 * Please try to follow the order.
	 */

	/* 1: Registers specific to a single generation */
	if (IS_VALLEYVIEW(dev)) {
		error->gtier[0] = I915_READ(GTIER);
		error->ier = I915_READ(VLV_IER);
		error->forcewake = I915_READ(FORCEWAKE_VLV);
	}

	if (IS_GEN7(dev))
		error->err_int = I915_READ(GEN7_ERR_INT);

	if (IS_GEN6(dev)) {
		error->forcewake = I915_READ(FORCEWAKE);
		error->gab_ctl = I915_READ(GAB_CTL);
		error->gfx_mode = I915_READ(GFX_MODE);
	}

	/* 2: Registers which belong to multiple generations */
	if (INTEL_INFO(dev)->gen >= 7)
		error->forcewake = I915_READ(FORCEWAKE_MT);

	if (INTEL_INFO(dev)->gen >= 6) {
		error->derrmr = I915_READ(DERRMR);
		error->error = I915_READ(ERROR_GEN6);
		error->done_reg = I915_READ(DONE_REG);
	}

	/* 3: Feature specific registers */
	if (IS_GEN6(dev) || IS_GEN7(dev)) {
		error->gam_ecochk = I915_READ(GAM_ECOCHK);
		error->gac_eco = I915_READ(GAC_ECO_BITS);
	}

	/* 4: Everything else */
	if (HAS_HW_CONTEXTS(dev))
		error->ccid = I915_READ(CCID);

	if (INTEL_INFO(dev)->gen >= 8) {
		error->ier = I915_READ(GEN8_DE_MISC_IER);
		for (i = 0; i < 4; i++)
			error->gtier[i] = I915_READ(GEN8_GT_IER(i));
	} else if (HAS_PCH_SPLIT(dev)) {
		error->ier = I915_READ(DEIER);
		error->gtier[0] = I915_READ(GTIER);
	} else if (IS_GEN2(dev)) {
		error->ier = I915_READ16(IER);
	} else if (!IS_VALLEYVIEW(dev)) {
		error->ier = I915_READ(IER);
	}
	error->eir = I915_READ(EIR);
	error->pgtbl_er = I915_READ(PGTBL_ER);

	i915_get_extra_instdone(dev, error->extra_instdone);
}

static void i915_error_capture_msg(struct drm_device *dev,
				   struct drm_i915_error_state *error,
				   bool wedged,
				   const char *error_msg)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 ecode;
	int ring_id = -1, len;

	ecode = i915_error_generate_code(dev_priv, error, &ring_id);

	len = scnprintf(error->error_msg, sizeof(error->error_msg),
			"GPU HANG: ecode %d:%d:0x%08x",
			INTEL_INFO(dev)->gen, ring_id, ecode);

	if (ring_id != -1 && error->ring[ring_id].pid != -1)
		len += scnprintf(error->error_msg + len,
				 sizeof(error->error_msg) - len,
				 ", in %s [%d]",
				 error->ring[ring_id].comm,
				 error->ring[ring_id].pid);

	scnprintf(error->error_msg + len, sizeof(error->error_msg) - len,
		  ", reason: %s, action: %s",
		  error_msg,
		  wedged ? "reset" : "continue");
}

static void i915_capture_gen_state(struct drm_i915_private *dev_priv,
				   struct drm_i915_error_state *error)
{
	error->reset_count = i915_reset_count(&dev_priv->gpu_error);
	error->suspend_count = dev_priv->suspend_count;
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
void i915_capture_error_state(struct drm_device *dev, bool wedged,
			      const char *error_msg)
{
	static bool warned;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_error_state *error;
	unsigned long flags;

	/* Account for pipe specific data like PIPE*STAT */
	error = kzalloc(sizeof(*error), GFP_ATOMIC);
	if (!error) {
		DRM_DEBUG_DRIVER("out of memory, not capturing error state\n");
		return;
	}

	kref_init(&error->ref);

	i915_capture_gen_state(dev_priv, error);
	i915_capture_reg_state(dev_priv, error);
	i915_gem_capture_buffers(dev_priv, error);
	i915_gem_record_fences(dev, error);
	i915_gem_record_rings(dev, error);

	do_gettimeofday(&error->time);

	error->overlay = intel_overlay_capture_error_state(dev);
	error->display = intel_display_capture_error_state(dev);

	i915_error_capture_msg(dev, error, wedged, error_msg);
	DRM_INFO("%s\n", error->error_msg);

	spin_lock_irqsave(&dev_priv->gpu_error.lock, flags);
	if (dev_priv->gpu_error.first_error == NULL) {
		dev_priv->gpu_error.first_error = error;
		error = NULL;
	}
	spin_unlock_irqrestore(&dev_priv->gpu_error.lock, flags);

	if (error) {
		i915_error_state_free(&error->ref);
		return;
	}

	if (!warned) {
		DRM_INFO("GPU hangs can indicate a bug anywhere in the entire gfx stack, including userspace.\n");
		DRM_INFO("Please file a _new_ bug report on bugs.freedesktop.org against DRI -> DRM/Intel\n");
		DRM_INFO("drm/i915 developers can then reassign to the right component if it's not a kernel issue.\n");
		DRM_INFO("The gpu crash dump is required to analyze gpu hangs, so please always attach it.\n");
		DRM_INFO("GPU crash dump saved to /sys/class/drm/card%d/error\n", dev->primary->index);
		warned = true;
	}
}

void i915_error_state_get(struct drm_device *dev,
			  struct i915_error_state_file_priv *error_priv)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	spin_lock_irq(&dev_priv->gpu_error.lock);
	error_priv->error = dev_priv->gpu_error.first_error;
	if (error_priv->error)
		kref_get(&error_priv->error->ref);
	spin_unlock_irq(&dev_priv->gpu_error.lock);

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

	spin_lock_irq(&dev_priv->gpu_error.lock);
	error = dev_priv->gpu_error.first_error;
	dev_priv->gpu_error.first_error = NULL;
	spin_unlock_irq(&dev_priv->gpu_error.lock);

	if (error)
		kref_put(&error->ref, i915_error_state_free);
}

const char *i915_cache_level_str(struct drm_i915_private *i915, int type)
{
	switch (type) {
	case I915_CACHE_NONE: return " uncached";
	case I915_CACHE_LLC: return HAS_LLC(i915) ? " LLC" : " snooped";
	case I915_CACHE_L3_LLC: return " L3+LLC";
	case I915_CACHE_WT: return " WT";
	default: return "";
	}
}

/* NB: please notice the memset */
void i915_get_extra_instdone(struct drm_device *dev, uint32_t *instdone)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	memset(instdone, 0, sizeof(*instdone) * I915_NUM_INSTDONE_REG);

	if (IS_GEN2(dev) || IS_GEN3(dev))
		instdone[0] = I915_READ(INSTDONE);
	else if (IS_GEN4(dev) || IS_GEN5(dev) || IS_GEN6(dev)) {
		instdone[0] = I915_READ(INSTDONE_I965);
		instdone[1] = I915_READ(INSTDONE1);
	} else if (INTEL_INFO(dev)->gen >= 7) {
		instdone[0] = I915_READ(GEN7_INSTDONE_1);
		instdone[1] = I915_READ(GEN7_SC_INSTDONE);
		instdone[2] = I915_READ(GEN7_SAMPLER_INSTDONE);
		instdone[3] = I915_READ(GEN7_ROW_INSTDONE);
	}
}
