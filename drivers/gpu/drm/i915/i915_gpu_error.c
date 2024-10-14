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

#include <linux/ascii85.h>
#include <linux/debugfs.h>
#include <linux/highmem.h>
#include <linux/nmi.h>
#include <linux/pagevec.h>
#include <linux/scatterlist.h>
#include <linux/string_helpers.h>
#include <linux/utsname.h>
#include <linux/zlib.h>

#include <drm/drm_cache.h>
#include <drm/drm_print.h>

#include "display/intel_dmc.h"
#include "display/intel_overlay.h"

#include "gem/i915_gem_context.h"
#include "gem/i915_gem_lmem.h"
#include "gt/intel_engine_regs.h"
#include "gt/intel_gt.h"
#include "gt/intel_gt_mcr.h"
#include "gt/intel_gt_pm.h"
#include "gt/intel_gt_regs.h"
#include "gt/uc/intel_guc_capture.h"

#include "i915_driver.h"
#include "i915_drv.h"
#include "i915_gpu_error.h"
#include "i915_memcpy.h"
#include "i915_reg.h"
#include "i915_scatterlist.h"
#include "i915_sysfs.h"
#include "i915_utils.h"

#define ALLOW_FAIL (__GFP_KSWAPD_RECLAIM | __GFP_RETRY_MAYFAIL | __GFP_NOWARN)
#define ATOMIC_MAYFAIL (GFP_ATOMIC | __GFP_NOWARN)

static void __sg_set_buf(struct scatterlist *sg,
			 void *addr, unsigned int len, loff_t it)
{
	sg->page_link = (unsigned long)virt_to_page(addr);
	sg->offset = offset_in_page(addr);
	sg->length = len;
	sg->dma_address = it;
}

static bool __i915_error_grow(struct drm_i915_error_state_buf *e, size_t len)
{
	if (!len)
		return false;

	if (e->bytes + len + 1 <= e->size)
		return true;

	if (e->bytes) {
		__sg_set_buf(e->cur++, e->buf, e->bytes, e->iter);
		e->iter += e->bytes;
		e->buf = NULL;
		e->bytes = 0;
	}

	if (e->cur == e->end) {
		struct scatterlist *sgl;

		sgl = (typeof(sgl))__get_free_page(ALLOW_FAIL);
		if (!sgl) {
			e->err = -ENOMEM;
			return false;
		}

		if (e->cur) {
			e->cur->offset = 0;
			e->cur->length = 0;
			e->cur->page_link =
				(unsigned long)sgl | SG_CHAIN;
		} else {
			e->sgl = sgl;
		}

		e->cur = sgl;
		e->end = sgl + SG_MAX_SINGLE_ALLOC - 1;
	}

	e->size = ALIGN(len + 1, SZ_64K);
	e->buf = kmalloc(e->size, ALLOW_FAIL);
	if (!e->buf) {
		e->size = PAGE_ALIGN(len + 1);
		e->buf = kmalloc(e->size, GFP_KERNEL);
	}
	if (!e->buf) {
		e->err = -ENOMEM;
		return false;
	}

	return true;
}

__printf(2, 0)
static void i915_error_vprintf(struct drm_i915_error_state_buf *e,
			       const char *fmt, va_list args)
{
	va_list ap;
	int len;

	if (e->err)
		return;

	va_copy(ap, args);
	len = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	if (len <= 0) {
		e->err = len;
		return;
	}

	if (!__i915_error_grow(e, len))
		return;

	GEM_BUG_ON(e->bytes >= e->size);
	len = vscnprintf(e->buf + e->bytes, e->size - e->bytes, fmt, args);
	if (len < 0) {
		e->err = len;
		return;
	}
	e->bytes += len;
}

static void i915_error_puts(struct drm_i915_error_state_buf *e, const char *str)
{
	unsigned len;

	if (e->err || !str)
		return;

	len = strlen(str);
	if (!__i915_error_grow(e, len))
		return;

	GEM_BUG_ON(e->bytes + len > e->size);
	memcpy(e->buf + e->bytes, str, len);
	e->bytes += len;
}

#define err_printf(e, ...) i915_error_printf(e, __VA_ARGS__)
#define err_puts(e, s) i915_error_puts(e, s)

static void __i915_printfn_error(struct drm_printer *p, struct va_format *vaf)
{
	i915_error_vprintf(p->arg, vaf->fmt, *vaf->va);
}

static inline struct drm_printer
i915_error_printer(struct drm_i915_error_state_buf *e)
{
	struct drm_printer p = {
		.printfn = __i915_printfn_error,
		.arg = e,
	};
	return p;
}

/* single threaded page allocator with a reserved stash for emergencies */
static void pool_fini(struct folio_batch *fbatch)
{
	folio_batch_release(fbatch);
}

static int pool_refill(struct folio_batch *fbatch, gfp_t gfp)
{
	while (folio_batch_space(fbatch)) {
		struct folio *folio;

		folio = folio_alloc(gfp, 0);
		if (!folio)
			return -ENOMEM;

		folio_batch_add(fbatch, folio);
	}

	return 0;
}

static int pool_init(struct folio_batch *fbatch, gfp_t gfp)
{
	int err;

	folio_batch_init(fbatch);

	err = pool_refill(fbatch, gfp);
	if (err)
		pool_fini(fbatch);

	return err;
}

static void *pool_alloc(struct folio_batch *fbatch, gfp_t gfp)
{
	struct folio *folio;

	folio = folio_alloc(gfp, 0);
	if (!folio && folio_batch_count(fbatch))
		folio = fbatch->folios[--fbatch->nr];

	return folio ? folio_address(folio) : NULL;
}

static void pool_free(struct folio_batch *fbatch, void *addr)
{
	struct folio *folio = virt_to_folio(addr);

	if (folio_batch_space(fbatch))
		folio_batch_add(fbatch, folio);
	else
		folio_put(folio);
}

#ifdef CONFIG_DRM_I915_COMPRESS_ERROR

struct i915_vma_compress {
	struct folio_batch pool;
	struct z_stream_s zstream;
	void *tmp;
};

static bool compress_init(struct i915_vma_compress *c)
{
	struct z_stream_s *zstream = &c->zstream;

	if (pool_init(&c->pool, ALLOW_FAIL))
		return false;

	zstream->workspace =
		kmalloc(zlib_deflate_workspacesize(MAX_WBITS, MAX_MEM_LEVEL),
			ALLOW_FAIL);
	if (!zstream->workspace) {
		pool_fini(&c->pool);
		return false;
	}

	c->tmp = NULL;
	if (i915_has_memcpy_from_wc())
		c->tmp = pool_alloc(&c->pool, ALLOW_FAIL);

	return true;
}

static bool compress_start(struct i915_vma_compress *c)
{
	struct z_stream_s *zstream = &c->zstream;
	void *workspace = zstream->workspace;

	memset(zstream, 0, sizeof(*zstream));
	zstream->workspace = workspace;

	return zlib_deflateInit(zstream, Z_DEFAULT_COMPRESSION) == Z_OK;
}

static void *compress_next_page(struct i915_vma_compress *c,
				struct i915_vma_coredump *dst)
{
	void *page_addr;
	struct page *page;

	page_addr = pool_alloc(&c->pool, ALLOW_FAIL);
	if (!page_addr)
		return ERR_PTR(-ENOMEM);

	page = virt_to_page(page_addr);
	list_add_tail(&page->lru, &dst->page_list);
	return page_addr;
}

static int compress_page(struct i915_vma_compress *c,
			 void *src,
			 struct i915_vma_coredump *dst,
			 bool wc)
{
	struct z_stream_s *zstream = &c->zstream;

	zstream->next_in = src;
	if (wc && c->tmp && i915_memcpy_from_wc(c->tmp, src, PAGE_SIZE))
		zstream->next_in = c->tmp;
	zstream->avail_in = PAGE_SIZE;

	do {
		if (zstream->avail_out == 0) {
			zstream->next_out = compress_next_page(c, dst);
			if (IS_ERR(zstream->next_out))
				return PTR_ERR(zstream->next_out);

			zstream->avail_out = PAGE_SIZE;
		}

		if (zlib_deflate(zstream, Z_NO_FLUSH) != Z_OK)
			return -EIO;

		cond_resched();
	} while (zstream->avail_in);

	/* Fallback to uncompressed if we increase size? */
	if (0 && zstream->total_out > zstream->total_in)
		return -E2BIG;

	return 0;
}

static int compress_flush(struct i915_vma_compress *c,
			  struct i915_vma_coredump *dst)
{
	struct z_stream_s *zstream = &c->zstream;

	do {
		switch (zlib_deflate(zstream, Z_FINISH)) {
		case Z_OK: /* more space requested */
			zstream->next_out = compress_next_page(c, dst);
			if (IS_ERR(zstream->next_out))
				return PTR_ERR(zstream->next_out);

			zstream->avail_out = PAGE_SIZE;
			break;

		case Z_STREAM_END:
			goto end;

		default: /* any error */
			return -EIO;
		}
	} while (1);

end:
	memset(zstream->next_out, 0, zstream->avail_out);
	dst->unused = zstream->avail_out;
	return 0;
}

static void compress_finish(struct i915_vma_compress *c)
{
	zlib_deflateEnd(&c->zstream);
}

static void compress_fini(struct i915_vma_compress *c)
{
	kfree(c->zstream.workspace);
	if (c->tmp)
		pool_free(&c->pool, c->tmp);
	pool_fini(&c->pool);
}

static void err_compression_marker(struct drm_i915_error_state_buf *m)
{
	err_puts(m, ":");
}

#else

struct i915_vma_compress {
	struct folio_batch pool;
};

static bool compress_init(struct i915_vma_compress *c)
{
	return pool_init(&c->pool, ALLOW_FAIL) == 0;
}

static bool compress_start(struct i915_vma_compress *c)
{
	return true;
}

static int compress_page(struct i915_vma_compress *c,
			 void *src,
			 struct i915_vma_coredump *dst,
			 bool wc)
{
	void *ptr;

	ptr = pool_alloc(&c->pool, ALLOW_FAIL);
	if (!ptr)
		return -ENOMEM;

	if (!(wc && i915_memcpy_from_wc(ptr, src, PAGE_SIZE)))
		memcpy(ptr, src, PAGE_SIZE);
	list_add_tail(&virt_to_page(ptr)->lru, &dst->page_list);
	cond_resched();

	return 0;
}

static int compress_flush(struct i915_vma_compress *c,
			  struct i915_vma_coredump *dst)
{
	return 0;
}

static void compress_finish(struct i915_vma_compress *c)
{
}

static void compress_fini(struct i915_vma_compress *c)
{
	pool_fini(&c->pool);
}

static void err_compression_marker(struct drm_i915_error_state_buf *m)
{
	err_puts(m, "~");
}

#endif

static void error_print_instdone(struct drm_i915_error_state_buf *m,
				 const struct intel_engine_coredump *ee)
{
	int slice;
	int subslice;
	int iter;

	err_printf(m, "  INSTDONE: 0x%08x\n",
		   ee->instdone.instdone);

	if (ee->engine->class != RENDER_CLASS || GRAPHICS_VER(m->i915) <= 3)
		return;

	err_printf(m, "  SC_INSTDONE: 0x%08x\n",
		   ee->instdone.slice_common);

	if (GRAPHICS_VER(m->i915) <= 6)
		return;

	for_each_ss_steering(iter, ee->engine->gt, slice, subslice)
		err_printf(m, "  SAMPLER_INSTDONE[%d][%d]: 0x%08x\n",
			   slice, subslice,
			   ee->instdone.sampler[slice][subslice]);

	for_each_ss_steering(iter, ee->engine->gt, slice, subslice)
		err_printf(m, "  ROW_INSTDONE[%d][%d]: 0x%08x\n",
			   slice, subslice,
			   ee->instdone.row[slice][subslice]);

	if (GRAPHICS_VER(m->i915) < 12)
		return;

	if (GRAPHICS_VER_FULL(m->i915) >= IP_VER(12, 55)) {
		for_each_ss_steering(iter, ee->engine->gt, slice, subslice)
			err_printf(m, "  GEOM_SVGUNIT_INSTDONE[%d][%d]: 0x%08x\n",
				   slice, subslice,
				   ee->instdone.geom_svg[slice][subslice]);
	}

	err_printf(m, "  SC_INSTDONE_EXTRA: 0x%08x\n",
		   ee->instdone.slice_common_extra[0]);
	err_printf(m, "  SC_INSTDONE_EXTRA2: 0x%08x\n",
		   ee->instdone.slice_common_extra[1]);
}

static void error_print_request(struct drm_i915_error_state_buf *m,
				const char *prefix,
				const struct i915_request_coredump *erq)
{
	if (!erq->seqno)
		return;

	err_printf(m, "%s pid %d, seqno %8x:%08x%s%s, prio %d, head %08x, tail %08x\n",
		   prefix, erq->pid, erq->context, erq->seqno,
		   test_bit(DMA_FENCE_FLAG_SIGNALED_BIT,
			    &erq->flags) ? "!" : "",
		   test_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT,
			    &erq->flags) ? "+" : "",
		   erq->sched_attr.priority,
		   erq->head, erq->tail);
}

static void error_print_context(struct drm_i915_error_state_buf *m,
				const char *header,
				const struct i915_gem_context_coredump *ctx)
{
	err_printf(m, "%s%s[%d] prio %d, guilty %d active %d, runtime total %lluns, avg %lluns\n",
		   header, ctx->comm, ctx->pid, ctx->sched_attr.priority,
		   ctx->guilty, ctx->active,
		   ctx->total_runtime, ctx->avg_runtime);
	err_printf(m, "  context timeline seqno %u\n", ctx->hwsp_seqno);
}

static struct i915_vma_coredump *
__find_vma(struct i915_vma_coredump *vma, const char *name)
{
	while (vma) {
		if (strcmp(vma->name, name) == 0)
			return vma;
		vma = vma->next;
	}

	return NULL;
}

static struct i915_vma_coredump *
intel_gpu_error_find_batch(const struct intel_engine_coredump *ee)
{
	return __find_vma(ee->vma, "batch");
}

static void error_print_engine(struct drm_i915_error_state_buf *m,
			       const struct intel_engine_coredump *ee)
{
	struct i915_vma_coredump *batch;
	int n;

	err_printf(m, "%s command stream:\n", ee->engine->name);
	err_printf(m, "  CCID:  0x%08x\n", ee->ccid);
	err_printf(m, "  START: 0x%08x\n", ee->start);
	err_printf(m, "  HEAD:  0x%08x [0x%08x]\n", ee->head, ee->rq_head);
	err_printf(m, "  TAIL:  0x%08x [0x%08x, 0x%08x]\n",
		   ee->tail, ee->rq_post, ee->rq_tail);
	err_printf(m, "  CTL:   0x%08x\n", ee->ctl);
	err_printf(m, "  MODE:  0x%08x\n", ee->mode);
	err_printf(m, "  HWS:   0x%08x\n", ee->hws);
	err_printf(m, "  ACTHD: 0x%08x %08x\n",
		   (u32)(ee->acthd>>32), (u32)ee->acthd);
	err_printf(m, "  IPEIR: 0x%08x\n", ee->ipeir);
	err_printf(m, "  IPEHR: 0x%08x\n", ee->ipehr);
	err_printf(m, "  ESR:   0x%08x\n", ee->esr);

	error_print_instdone(m, ee);

	batch = intel_gpu_error_find_batch(ee);
	if (batch) {
		u64 start = batch->gtt_offset;
		u64 end = start + batch->gtt_size;

		err_printf(m, "  batch: [0x%08x_%08x, 0x%08x_%08x]\n",
			   upper_32_bits(start), lower_32_bits(start),
			   upper_32_bits(end), lower_32_bits(end));
	}
	if (GRAPHICS_VER(m->i915) >= 4) {
		err_printf(m, "  BBADDR: 0x%08x_%08x\n",
			   (u32)(ee->bbaddr>>32), (u32)ee->bbaddr);
		err_printf(m, "  BB_STATE: 0x%08x\n", ee->bbstate);
		err_printf(m, "  INSTPS: 0x%08x\n", ee->instps);
	}
	err_printf(m, "  INSTPM: 0x%08x\n", ee->instpm);
	err_printf(m, "  FADDR: 0x%08x %08x\n", upper_32_bits(ee->faddr),
		   lower_32_bits(ee->faddr));
	if (GRAPHICS_VER(m->i915) >= 6) {
		err_printf(m, "  RC PSMI: 0x%08x\n", ee->rc_psmi);
		err_printf(m, "  FAULT_REG: 0x%08x\n", ee->fault_reg);
	}
	if (GRAPHICS_VER(m->i915) >= 11) {
		err_printf(m, "  NOPID: 0x%08x\n", ee->nopid);
		err_printf(m, "  EXCC: 0x%08x\n", ee->excc);
		err_printf(m, "  CMD_CCTL: 0x%08x\n", ee->cmd_cctl);
		err_printf(m, "  CSCMDOP: 0x%08x\n", ee->cscmdop);
		err_printf(m, "  CTX_SR_CTL: 0x%08x\n", ee->ctx_sr_ctl);
		err_printf(m, "  DMA_FADDR_HI: 0x%08x\n", ee->dma_faddr_hi);
		err_printf(m, "  DMA_FADDR_LO: 0x%08x\n", ee->dma_faddr_lo);
	}
	if (HAS_PPGTT(m->i915)) {
		err_printf(m, "  GFX_MODE: 0x%08x\n", ee->vm_info.gfx_mode);

		if (GRAPHICS_VER(m->i915) >= 8) {
			int i;
			for (i = 0; i < 4; i++)
				err_printf(m, "  PDP%d: 0x%016llx\n",
					   i, ee->vm_info.pdp[i]);
		} else {
			err_printf(m, "  PP_DIR_BASE: 0x%08x\n",
				   ee->vm_info.pp_dir_base);
		}
	}

	for (n = 0; n < ee->num_ports; n++) {
		err_printf(m, "  ELSP[%d]:", n);
		error_print_request(m, " ", &ee->execlist[n]);
	}
}

void i915_error_printf(struct drm_i915_error_state_buf *e, const char *f, ...)
{
	va_list args;

	va_start(args, f);
	i915_error_vprintf(e, f, args);
	va_end(args);
}

static void intel_gpu_error_print_vma(struct drm_i915_error_state_buf *m,
				      const struct intel_engine_cs *engine,
				      const struct i915_vma_coredump *vma)
{
	char out[ASCII85_BUFSZ];
	struct page *page;

	if (!vma)
		return;

	err_printf(m, "%s --- %s = 0x%08x %08x\n",
		   engine ? engine->name : "global", vma->name,
		   upper_32_bits(vma->gtt_offset),
		   lower_32_bits(vma->gtt_offset));

	if (vma->gtt_page_sizes > I915_GTT_PAGE_SIZE_4K)
		err_printf(m, "gtt_page_sizes = 0x%08x\n", vma->gtt_page_sizes);

	err_compression_marker(m);
	list_for_each_entry(page, &vma->page_list, lru) {
		int i, len;
		const u32 *addr = page_address(page);

		len = PAGE_SIZE;
		if (page == list_last_entry(&vma->page_list, typeof(*page), lru))
			len -= vma->unused;
		len = ascii85_encode_len(len);

		for (i = 0; i < len; i++)
			err_puts(m, ascii85_encode(addr[i], out));
	}
	err_puts(m, "\n");
}

static void err_print_capabilities(struct drm_i915_error_state_buf *m,
				   struct i915_gpu_coredump *error)
{
	struct drm_printer p = i915_error_printer(m);

	intel_device_info_print(&error->device_info, &error->runtime_info, &p);
	intel_display_device_info_print(&error->display_device_info,
					&error->display_runtime_info, &p);
	intel_driver_caps_print(&error->driver_caps, &p);
}

static void err_print_params(struct drm_i915_error_state_buf *m,
			     const struct i915_params *params)
{
	struct drm_printer p = i915_error_printer(m);
	struct intel_display *display = &m->i915->display;

	i915_params_dump(params, &p);
	intel_display_params_dump(display, &p);
}

static void err_print_pciid(struct drm_i915_error_state_buf *m,
			    struct drm_i915_private *i915)
{
	struct pci_dev *pdev = to_pci_dev(i915->drm.dev);

	err_printf(m, "PCI ID: 0x%04x\n", pdev->device);
	err_printf(m, "PCI Revision: 0x%02x\n", pdev->revision);
	err_printf(m, "PCI Subsystem: %04x:%04x\n",
		   pdev->subsystem_vendor,
		   pdev->subsystem_device);
}

static void err_print_guc_ctb(struct drm_i915_error_state_buf *m,
			      const char *name,
			      const struct intel_ctb_coredump *ctb)
{
	if (!ctb->size)
		return;

	err_printf(m, "GuC %s CTB: raw: 0x%08X, 0x%08X/%08X, cached: 0x%08X/%08X, desc = 0x%08X, buf = 0x%08X x 0x%08X\n",
		   name, ctb->raw_status, ctb->raw_head, ctb->raw_tail,
		   ctb->head, ctb->tail, ctb->desc_offset, ctb->cmds_offset, ctb->size);
}

static void err_print_uc(struct drm_i915_error_state_buf *m,
			 const struct intel_uc_coredump *error_uc)
{
	struct drm_printer p = i915_error_printer(m);

	intel_uc_fw_dump(&error_uc->guc_fw, &p);
	intel_uc_fw_dump(&error_uc->huc_fw, &p);
	err_printf(m, "GuC timestamp: 0x%08x\n", error_uc->guc.timestamp);
	intel_gpu_error_print_vma(m, NULL, error_uc->guc.vma_log);
	err_printf(m, "GuC CTB fence: %d\n", error_uc->guc.last_fence);
	err_print_guc_ctb(m, "Send", error_uc->guc.ctb + 0);
	err_print_guc_ctb(m, "Recv", error_uc->guc.ctb + 1);
	intel_gpu_error_print_vma(m, NULL, error_uc->guc.vma_ctb);
}

static void err_free_sgl(struct scatterlist *sgl)
{
	while (sgl) {
		struct scatterlist *sg;

		for (sg = sgl; !sg_is_chain(sg); sg++) {
			kfree(sg_virt(sg));
			if (sg_is_last(sg))
				break;
		}

		sg = sg_is_last(sg) ? NULL : sg_chain_ptr(sg);
		free_page((unsigned long)sgl);
		sgl = sg;
	}
}

static void err_print_gt_info(struct drm_i915_error_state_buf *m,
			      struct intel_gt_coredump *gt)
{
	struct drm_printer p = i915_error_printer(m);

	intel_gt_info_print(&gt->info, &p);
	intel_sseu_print_topology(gt->_gt->i915, &gt->info.sseu, &p);
}

static void err_print_gt_display(struct drm_i915_error_state_buf *m,
				 struct intel_gt_coredump *gt)
{
	err_printf(m, "IER: 0x%08x\n", gt->ier);
	err_printf(m, "DERRMR: 0x%08x\n", gt->derrmr);
}

static void err_print_gt_global_nonguc(struct drm_i915_error_state_buf *m,
				       struct intel_gt_coredump *gt)
{
	int i;

	err_printf(m, "GT awake: %s\n", str_yes_no(gt->awake));
	err_printf(m, "CS timestamp frequency: %u Hz, %d ns\n",
		   gt->clock_frequency, gt->clock_period_ns);
	err_printf(m, "EIR: 0x%08x\n", gt->eir);
	err_printf(m, "PGTBL_ER: 0x%08x\n", gt->pgtbl_er);

	for (i = 0; i < gt->ngtier; i++)
		err_printf(m, "GTIER[%d]: 0x%08x\n", i, gt->gtier[i]);
}

static void err_print_gt_global(struct drm_i915_error_state_buf *m,
				struct intel_gt_coredump *gt)
{
	err_printf(m, "FORCEWAKE: 0x%08x\n", gt->forcewake);

	if (IS_GRAPHICS_VER(m->i915, 6, 11)) {
		err_printf(m, "ERROR: 0x%08x\n", gt->error);
		err_printf(m, "DONE_REG: 0x%08x\n", gt->done_reg);
	}

	if (GRAPHICS_VER(m->i915) >= 8)
		err_printf(m, "FAULT_TLB_DATA: 0x%08x 0x%08x\n",
			   gt->fault_data1, gt->fault_data0);

	if (GRAPHICS_VER(m->i915) == 7)
		err_printf(m, "ERR_INT: 0x%08x\n", gt->err_int);

	if (IS_GRAPHICS_VER(m->i915, 8, 11))
		err_printf(m, "GTT_CACHE_EN: 0x%08x\n", gt->gtt_cache);

	if (GRAPHICS_VER(m->i915) == 12)
		err_printf(m, "AUX_ERR_DBG: 0x%08x\n", gt->aux_err);

	if (GRAPHICS_VER(m->i915) >= 12) {
		int i;

		for (i = 0; i < I915_MAX_SFC; i++) {
			/*
			 * SFC_DONE resides in the VD forcewake domain, so it
			 * only exists if the corresponding VCS engine is
			 * present.
			 */
			if ((gt->_gt->info.sfc_mask & BIT(i)) == 0 ||
			    !HAS_ENGINE(gt->_gt, _VCS(i * 2)))
				continue;

			err_printf(m, "  SFC_DONE[%d]: 0x%08x\n", i,
				   gt->sfc_done[i]);
		}

		err_printf(m, "  GAM_DONE: 0x%08x\n", gt->gam_done);
	}
}

static void err_print_gt_fences(struct drm_i915_error_state_buf *m,
				struct intel_gt_coredump *gt)
{
	int i;

	for (i = 0; i < gt->nfence; i++)
		err_printf(m, "  fence[%d] = %08llx\n", i, gt->fence[i]);
}

static void err_print_gt_engines(struct drm_i915_error_state_buf *m,
				 struct intel_gt_coredump *gt)
{
	const struct intel_engine_coredump *ee;

	for (ee = gt->engine; ee; ee = ee->next) {
		const struct i915_vma_coredump *vma;

		if (gt->uc && gt->uc->guc.is_guc_capture) {
			if (ee->guc_capture_node)
				intel_guc_capture_print_engine_node(m, ee);
			else
				err_printf(m, "  Missing GuC capture node for %s\n",
					   ee->engine->name);
		} else {
			error_print_engine(m, ee);
		}

		err_printf(m, "  hung: %u\n", ee->hung);
		err_printf(m, "  engine reset count: %u\n", ee->reset_count);
		error_print_context(m, "  Active context: ", &ee->context);

		for (vma = ee->vma; vma; vma = vma->next)
			intel_gpu_error_print_vma(m, ee->engine, vma);
	}

}

static void __err_print_to_sgl(struct drm_i915_error_state_buf *m,
			       struct i915_gpu_coredump *error)
{
	struct drm_printer p = i915_error_printer(m);
	const struct intel_engine_coredump *ee;
	struct timespec64 ts;

	if (*error->error_msg)
		err_printf(m, "%s\n", error->error_msg);
	err_printf(m, "Kernel: %s %s\n",
		   init_utsname()->release,
		   init_utsname()->machine);
	err_printf(m, "Driver: %s\n", DRIVER_DATE);
	ts = ktime_to_timespec64(error->time);
	err_printf(m, "Time: %lld s %ld us\n",
		   (s64)ts.tv_sec, ts.tv_nsec / NSEC_PER_USEC);
	ts = ktime_to_timespec64(error->boottime);
	err_printf(m, "Boottime: %lld s %ld us\n",
		   (s64)ts.tv_sec, ts.tv_nsec / NSEC_PER_USEC);
	ts = ktime_to_timespec64(error->uptime);
	err_printf(m, "Uptime: %lld s %ld us\n",
		   (s64)ts.tv_sec, ts.tv_nsec / NSEC_PER_USEC);
	err_printf(m, "Capture: %lu jiffies; %d ms ago\n",
		   error->capture, jiffies_to_msecs(jiffies - error->capture));

	for (ee = error->gt ? error->gt->engine : NULL; ee; ee = ee->next)
		err_printf(m, "Active process (on ring %s): %s [%d]\n",
			   ee->engine->name,
			   ee->context.comm,
			   ee->context.pid);

	err_printf(m, "Reset count: %u\n", error->reset_count);
	err_printf(m, "Suspend count: %u\n", error->suspend_count);
	err_printf(m, "Platform: %s\n", intel_platform_name(error->device_info.platform));
	err_printf(m, "Subplatform: 0x%x\n",
		   intel_subplatform(&error->runtime_info,
				     error->device_info.platform));
	err_print_pciid(m, m->i915);

	err_printf(m, "IOMMU enabled?: %d\n", error->iommu);

	intel_dmc_print_error_state(&p, m->i915);

	err_printf(m, "RPM wakelock: %s\n", str_yes_no(error->wakelock));
	err_printf(m, "PM suspended: %s\n", str_yes_no(error->suspended));

	if (error->gt) {
		bool print_guc_capture = false;

		if (error->gt->uc && error->gt->uc->guc.is_guc_capture)
			print_guc_capture = true;

		err_print_gt_display(m, error->gt);
		err_print_gt_global_nonguc(m, error->gt);
		err_print_gt_fences(m, error->gt);

		/*
		 * GuC dumped global, eng-class and eng-instance registers together
		 * as part of engine state dump so we print in err_print_gt_engines
		 */
		if (!print_guc_capture)
			err_print_gt_global(m, error->gt);

		err_print_gt_engines(m, error->gt);

		if (error->gt->uc)
			err_print_uc(m, error->gt->uc);

		err_print_gt_info(m, error->gt);
	}

	if (error->overlay)
		intel_overlay_print_error_state(&p, error->overlay);

	err_print_capabilities(m, error);
	err_print_params(m, &error->params);
}

static int err_print_to_sgl(struct i915_gpu_coredump *error)
{
	struct drm_i915_error_state_buf m;

	if (IS_ERR(error))
		return PTR_ERR(error);

	if (READ_ONCE(error->sgl))
		return 0;

	memset(&m, 0, sizeof(m));
	m.i915 = error->i915;

	__err_print_to_sgl(&m, error);

	if (m.buf) {
		__sg_set_buf(m.cur++, m.buf, m.bytes, m.iter);
		m.bytes = 0;
		m.buf = NULL;
	}
	if (m.cur) {
		GEM_BUG_ON(m.end < m.cur);
		sg_mark_end(m.cur - 1);
	}
	GEM_BUG_ON(m.sgl && !m.cur);

	if (m.err) {
		err_free_sgl(m.sgl);
		return m.err;
	}

	if (cmpxchg(&error->sgl, NULL, m.sgl))
		err_free_sgl(m.sgl);

	return 0;
}

ssize_t i915_gpu_coredump_copy_to_buffer(struct i915_gpu_coredump *error,
					 char *buf, loff_t off, size_t rem)
{
	struct scatterlist *sg;
	size_t count;
	loff_t pos;
	int err;

	if (!error || !rem)
		return 0;

	err = err_print_to_sgl(error);
	if (err)
		return err;

	sg = READ_ONCE(error->fit);
	if (!sg || off < sg->dma_address)
		sg = error->sgl;
	if (!sg)
		return 0;

	pos = sg->dma_address;
	count = 0;
	do {
		size_t len, start;

		if (sg_is_chain(sg)) {
			sg = sg_chain_ptr(sg);
			GEM_BUG_ON(sg_is_chain(sg));
		}

		len = sg->length;
		if (pos + len <= off) {
			pos += len;
			continue;
		}

		start = sg->offset;
		if (pos < off) {
			GEM_BUG_ON(off - pos > len);
			len -= off - pos;
			start += off - pos;
			pos = off;
		}

		len = min(len, rem);
		GEM_BUG_ON(!len || len > sg->length);

		memcpy(buf, page_address(sg_page(sg)) + start, len);

		count += len;
		pos += len;

		buf += len;
		rem -= len;
		if (!rem) {
			WRITE_ONCE(error->fit, sg);
			break;
		}
	} while (!sg_is_last(sg++));

	return count;
}

static void i915_vma_coredump_free(struct i915_vma_coredump *vma)
{
	while (vma) {
		struct i915_vma_coredump *next = vma->next;
		struct page *page, *n;

		list_for_each_entry_safe(page, n, &vma->page_list, lru) {
			list_del_init(&page->lru);
			__free_page(page);
		}

		kfree(vma);
		vma = next;
	}
}

static void cleanup_params(struct i915_gpu_coredump *error)
{
	i915_params_free(&error->params);
	intel_display_params_free(&error->display_params);
}

static void cleanup_uc(struct intel_uc_coredump *uc)
{
	kfree(uc->guc_fw.file_selected.path);
	kfree(uc->huc_fw.file_selected.path);
	kfree(uc->guc_fw.file_wanted.path);
	kfree(uc->huc_fw.file_wanted.path);
	i915_vma_coredump_free(uc->guc.vma_log);
	i915_vma_coredump_free(uc->guc.vma_ctb);

	kfree(uc);
}

static void cleanup_gt(struct intel_gt_coredump *gt)
{
	while (gt->engine) {
		struct intel_engine_coredump *ee = gt->engine;

		gt->engine = ee->next;

		i915_vma_coredump_free(ee->vma);
		intel_guc_capture_free_node(ee);
		kfree(ee);
	}

	if (gt->uc)
		cleanup_uc(gt->uc);

	kfree(gt);
}

void __i915_gpu_coredump_free(struct kref *error_ref)
{
	struct i915_gpu_coredump *error =
		container_of(error_ref, typeof(*error), ref);

	while (error->gt) {
		struct intel_gt_coredump *gt = error->gt;

		error->gt = gt->next;
		cleanup_gt(gt);
	}

	kfree(error->overlay);

	cleanup_params(error);

	err_free_sgl(error->sgl);
	kfree(error);
}

static struct i915_vma_coredump *
i915_vma_coredump_create(const struct intel_gt *gt,
			 const struct i915_vma_resource *vma_res,
			 struct i915_vma_compress *compress,
			 const char *name)

{
	struct i915_ggtt *ggtt = gt->ggtt;
	const u64 slot = ggtt->error_capture.start;
	struct i915_vma_coredump *dst;
	struct sgt_iter iter;
	int ret;

	might_sleep();

	if (!vma_res || !vma_res->bi.pages || !compress)
		return NULL;

	dst = kmalloc(sizeof(*dst), ALLOW_FAIL);
	if (!dst)
		return NULL;

	if (!compress_start(compress)) {
		kfree(dst);
		return NULL;
	}

	INIT_LIST_HEAD(&dst->page_list);
	strcpy(dst->name, name);
	dst->next = NULL;

	dst->gtt_offset = vma_res->start;
	dst->gtt_size = vma_res->node_size;
	dst->gtt_page_sizes = vma_res->page_sizes_gtt;
	dst->unused = 0;

	ret = -EINVAL;
	if (drm_mm_node_allocated(&ggtt->error_capture)) {
		void __iomem *s;
		dma_addr_t dma;

		for_each_sgt_daddr(dma, iter, vma_res->bi.pages) {
			mutex_lock(&ggtt->error_mutex);
			if (ggtt->vm.raw_insert_page)
				ggtt->vm.raw_insert_page(&ggtt->vm, dma, slot,
							 i915_gem_get_pat_index(gt->i915,
										I915_CACHE_NONE),
							 0);
			else
				ggtt->vm.insert_page(&ggtt->vm, dma, slot,
						     i915_gem_get_pat_index(gt->i915,
									    I915_CACHE_NONE),
						     0);
			mb();

			s = io_mapping_map_wc(&ggtt->iomap, slot, PAGE_SIZE);
			ret = compress_page(compress,
					    (void  __force *)s, dst,
					    true);
			io_mapping_unmap(s);

			mb();
			ggtt->vm.clear_range(&ggtt->vm, slot, PAGE_SIZE);
			mutex_unlock(&ggtt->error_mutex);
			if (ret)
				break;
		}
	} else if (vma_res->bi.lmem) {
		struct intel_memory_region *mem = vma_res->mr;
		dma_addr_t dma;

		for_each_sgt_daddr(dma, iter, vma_res->bi.pages) {
			dma_addr_t offset = dma - mem->region.start;
			void __iomem *s;

			if (offset + PAGE_SIZE > resource_size(&mem->io)) {
				ret = -EINVAL;
				break;
			}

			s = io_mapping_map_wc(&mem->iomap, offset, PAGE_SIZE);
			ret = compress_page(compress,
					    (void __force *)s, dst,
					    true);
			io_mapping_unmap(s);
			if (ret)
				break;
		}
	} else {
		struct page *page;

		for_each_sgt_page(page, iter, vma_res->bi.pages) {
			void *s;

			drm_clflush_pages(&page, 1);

			s = kmap_local_page(page);
			ret = compress_page(compress, s, dst, false);
			kunmap_local(s);

			drm_clflush_pages(&page, 1);

			if (ret)
				break;
		}
	}

	if (ret || compress_flush(compress, dst)) {
		struct page *page, *n;

		list_for_each_entry_safe_reverse(page, n, &dst->page_list, lru) {
			list_del_init(&page->lru);
			pool_free(&compress->pool, page_address(page));
		}

		kfree(dst);
		dst = NULL;
	}
	compress_finish(compress);

	return dst;
}

static void gt_record_fences(struct intel_gt_coredump *gt)
{
	struct i915_ggtt *ggtt = gt->_gt->ggtt;
	struct intel_uncore *uncore = gt->_gt->uncore;
	int i;

	if (GRAPHICS_VER(uncore->i915) >= 6) {
		for (i = 0; i < ggtt->num_fences; i++)
			gt->fence[i] =
				intel_uncore_read64(uncore,
						    FENCE_REG_GEN6_LO(i));
	} else if (GRAPHICS_VER(uncore->i915) >= 4) {
		for (i = 0; i < ggtt->num_fences; i++)
			gt->fence[i] =
				intel_uncore_read64(uncore,
						    FENCE_REG_965_LO(i));
	} else {
		for (i = 0; i < ggtt->num_fences; i++)
			gt->fence[i] =
				intel_uncore_read(uncore, FENCE_REG(i));
	}
	gt->nfence = i;
}

static void engine_record_registers(struct intel_engine_coredump *ee)
{
	const struct intel_engine_cs *engine = ee->engine;
	struct drm_i915_private *i915 = engine->i915;

	if (GRAPHICS_VER(i915) >= 6) {
		ee->rc_psmi = ENGINE_READ(engine, RING_PSMI_CTL);

		/*
		 * For the media GT, this ring fault register is not replicated,
		 * so don't do multicast/replicated register read/write
		 * operation on it.
		 */
		if (MEDIA_VER(i915) >= 13 && engine->gt->type == GT_MEDIA)
			ee->fault_reg = intel_uncore_read(engine->uncore,
							  XELPMP_RING_FAULT_REG);
		else if (GRAPHICS_VER_FULL(i915) >= IP_VER(12, 55))
			ee->fault_reg = intel_gt_mcr_read_any(engine->gt,
							      XEHP_RING_FAULT_REG);
		else if (GRAPHICS_VER(i915) >= 12)
			ee->fault_reg = intel_uncore_read(engine->uncore,
							  GEN12_RING_FAULT_REG);
		else if (GRAPHICS_VER(i915) >= 8)
			ee->fault_reg = intel_uncore_read(engine->uncore,
							  GEN8_RING_FAULT_REG);
		else
			ee->fault_reg = GEN6_RING_FAULT_REG_READ(engine);
	}

	if (GRAPHICS_VER(i915) >= 4) {
		ee->esr = ENGINE_READ(engine, RING_ESR);
		ee->faddr = ENGINE_READ(engine, RING_DMA_FADD);
		ee->ipeir = ENGINE_READ(engine, RING_IPEIR);
		ee->ipehr = ENGINE_READ(engine, RING_IPEHR);
		ee->instps = ENGINE_READ(engine, RING_INSTPS);
		ee->bbaddr = ENGINE_READ(engine, RING_BBADDR);
		ee->ccid = ENGINE_READ(engine, CCID);
		if (GRAPHICS_VER(i915) >= 8) {
			ee->faddr |= (u64)ENGINE_READ(engine, RING_DMA_FADD_UDW) << 32;
			ee->bbaddr |= (u64)ENGINE_READ(engine, RING_BBADDR_UDW) << 32;
		}
		ee->bbstate = ENGINE_READ(engine, RING_BBSTATE);
	} else {
		ee->faddr = ENGINE_READ(engine, DMA_FADD_I8XX);
		ee->ipeir = ENGINE_READ(engine, IPEIR);
		ee->ipehr = ENGINE_READ(engine, IPEHR);
	}

	if (GRAPHICS_VER(i915) >= 11) {
		ee->cmd_cctl = ENGINE_READ(engine, RING_CMD_CCTL);
		ee->cscmdop = ENGINE_READ(engine, RING_CSCMDOP);
		ee->ctx_sr_ctl = ENGINE_READ(engine, RING_CTX_SR_CTL);
		ee->dma_faddr_hi = ENGINE_READ(engine, RING_DMA_FADD_UDW);
		ee->dma_faddr_lo = ENGINE_READ(engine, RING_DMA_FADD);
		ee->nopid = ENGINE_READ(engine, RING_NOPID);
		ee->excc = ENGINE_READ(engine, RING_EXCC);
	}

	intel_engine_get_instdone(engine, &ee->instdone);

	ee->instpm = ENGINE_READ(engine, RING_INSTPM);
	ee->acthd = intel_engine_get_active_head(engine);
	ee->start = ENGINE_READ(engine, RING_START);
	ee->head = ENGINE_READ(engine, RING_HEAD);
	ee->tail = ENGINE_READ(engine, RING_TAIL);
	ee->ctl = ENGINE_READ(engine, RING_CTL);
	if (GRAPHICS_VER(i915) > 2)
		ee->mode = ENGINE_READ(engine, RING_MI_MODE);

	if (!HWS_NEEDS_PHYSICAL(i915)) {
		i915_reg_t mmio;

		if (GRAPHICS_VER(i915) == 7) {
			switch (engine->id) {
			default:
				MISSING_CASE(engine->id);
				fallthrough;
			case RCS0:
				mmio = RENDER_HWS_PGA_GEN7;
				break;
			case BCS0:
				mmio = BLT_HWS_PGA_GEN7;
				break;
			case VCS0:
				mmio = BSD_HWS_PGA_GEN7;
				break;
			case VECS0:
				mmio = VEBOX_HWS_PGA_GEN7;
				break;
			}
		} else if (GRAPHICS_VER(engine->i915) == 6) {
			mmio = RING_HWS_PGA_GEN6(engine->mmio_base);
		} else {
			/* XXX: gen8 returns to sanity */
			mmio = RING_HWS_PGA(engine->mmio_base);
		}

		ee->hws = intel_uncore_read(engine->uncore, mmio);
	}

	ee->reset_count = i915_reset_engine_count(&i915->gpu_error, engine);

	if (HAS_PPGTT(i915)) {
		int i;

		ee->vm_info.gfx_mode = ENGINE_READ(engine, RING_MODE_GEN7);

		if (GRAPHICS_VER(i915) == 6) {
			ee->vm_info.pp_dir_base =
				ENGINE_READ(engine, RING_PP_DIR_BASE_READ);
		} else if (GRAPHICS_VER(i915) == 7) {
			ee->vm_info.pp_dir_base =
				ENGINE_READ(engine, RING_PP_DIR_BASE);
		} else if (GRAPHICS_VER(i915) >= 8) {
			u32 base = engine->mmio_base;

			for (i = 0; i < 4; i++) {
				ee->vm_info.pdp[i] =
					intel_uncore_read(engine->uncore,
							  GEN8_RING_PDP_UDW(base, i));
				ee->vm_info.pdp[i] <<= 32;
				ee->vm_info.pdp[i] |=
					intel_uncore_read(engine->uncore,
							  GEN8_RING_PDP_LDW(base, i));
			}
		}
	}
}

static void record_request(const struct i915_request *request,
			   struct i915_request_coredump *erq)
{
	erq->flags = request->fence.flags;
	erq->context = request->fence.context;
	erq->seqno = request->fence.seqno;
	erq->sched_attr = request->sched.attr;
	erq->head = request->head;
	erq->tail = request->tail;

	erq->pid = 0;
	rcu_read_lock();
	if (!intel_context_is_closed(request->context)) {
		const struct i915_gem_context *ctx;

		ctx = rcu_dereference(request->context->gem_context);
		if (ctx)
			erq->pid = pid_nr(ctx->pid);
	}
	rcu_read_unlock();
}

static void engine_record_execlists(struct intel_engine_coredump *ee)
{
	const struct intel_engine_execlists * const el = &ee->engine->execlists;
	struct i915_request * const *port = el->active;
	unsigned int n = 0;

	while (*port)
		record_request(*port++, &ee->execlist[n++]);

	ee->num_ports = n;
}

static bool record_context(struct i915_gem_context_coredump *e,
			   struct intel_context *ce)
{
	struct i915_gem_context *ctx;
	struct task_struct *task;
	bool simulated;

	rcu_read_lock();
	ctx = rcu_dereference(ce->gem_context);
	if (ctx && !kref_get_unless_zero(&ctx->ref))
		ctx = NULL;
	rcu_read_unlock();
	if (!ctx)
		return true;

	rcu_read_lock();
	task = pid_task(ctx->pid, PIDTYPE_PID);
	if (task) {
		strcpy(e->comm, task->comm);
		e->pid = task->pid;
	}
	rcu_read_unlock();

	e->sched_attr = ctx->sched;
	e->guilty = atomic_read(&ctx->guilty_count);
	e->active = atomic_read(&ctx->active_count);
	e->hwsp_seqno = (ce->timeline && ce->timeline->hwsp_seqno) ?
				*ce->timeline->hwsp_seqno : ~0U;

	e->total_runtime = intel_context_get_total_runtime_ns(ce);
	e->avg_runtime = intel_context_get_avg_runtime_ns(ce);

	simulated = i915_gem_context_no_error_capture(ctx);

	i915_gem_context_put(ctx);
	return simulated;
}

struct intel_engine_capture_vma {
	struct intel_engine_capture_vma *next;
	struct i915_vma_resource *vma_res;
	char name[16];
	bool lockdep_cookie;
};

static struct intel_engine_capture_vma *
capture_vma_snapshot(struct intel_engine_capture_vma *next,
		     struct i915_vma_resource *vma_res,
		     gfp_t gfp, const char *name)
{
	struct intel_engine_capture_vma *c;

	if (!vma_res)
		return next;

	c = kmalloc(sizeof(*c), gfp);
	if (!c)
		return next;

	if (!i915_vma_resource_hold(vma_res, &c->lockdep_cookie)) {
		kfree(c);
		return next;
	}

	strcpy(c->name, name);
	c->vma_res = i915_vma_resource_get(vma_res);

	c->next = next;
	return c;
}

static struct intel_engine_capture_vma *
capture_vma(struct intel_engine_capture_vma *next,
	    struct i915_vma *vma,
	    const char *name,
	    gfp_t gfp)
{
	if (!vma)
		return next;

	/*
	 * If the vma isn't pinned, then the vma should be snapshotted
	 * to a struct i915_vma_snapshot at command submission time.
	 * Not here.
	 */
	if (GEM_WARN_ON(!i915_vma_is_pinned(vma)))
		return next;

	next = capture_vma_snapshot(next, vma->resource, gfp, name);

	return next;
}

static struct intel_engine_capture_vma *
capture_user(struct intel_engine_capture_vma *capture,
	     const struct i915_request *rq,
	     gfp_t gfp)
{
	struct i915_capture_list *c;

	for (c = rq->capture_list; c; c = c->next)
		capture = capture_vma_snapshot(capture, c->vma_res, gfp,
					       "user");

	return capture;
}

static void add_vma(struct intel_engine_coredump *ee,
		    struct i915_vma_coredump *vma)
{
	if (vma) {
		vma->next = ee->vma;
		ee->vma = vma;
	}
}

static struct i915_vma_coredump *
create_vma_coredump(const struct intel_gt *gt, struct i915_vma *vma,
		    const char *name, struct i915_vma_compress *compress)
{
	struct i915_vma_coredump *ret = NULL;
	struct i915_vma_resource *vma_res;
	bool lockdep_cookie;

	if (!vma)
		return NULL;

	vma_res = vma->resource;

	if (i915_vma_resource_hold(vma_res, &lockdep_cookie)) {
		ret = i915_vma_coredump_create(gt, vma_res, compress, name);
		i915_vma_resource_unhold(vma_res, lockdep_cookie);
	}

	return ret;
}

static void add_vma_coredump(struct intel_engine_coredump *ee,
			     const struct intel_gt *gt,
			     struct i915_vma *vma,
			     const char *name,
			     struct i915_vma_compress *compress)
{
	add_vma(ee, create_vma_coredump(gt, vma, name, compress));
}

struct intel_engine_coredump *
intel_engine_coredump_alloc(struct intel_engine_cs *engine, gfp_t gfp, u32 dump_flags)
{
	struct intel_engine_coredump *ee;

	ee = kzalloc(sizeof(*ee), gfp);
	if (!ee)
		return NULL;

	ee->engine = engine;

	if (!(dump_flags & CORE_DUMP_FLAG_IS_GUC_CAPTURE)) {
		engine_record_registers(ee);
		engine_record_execlists(ee);
	}

	return ee;
}

static struct intel_engine_capture_vma *
engine_coredump_add_context(struct intel_engine_coredump *ee,
			    struct intel_context *ce,
			    gfp_t gfp)
{
	struct intel_engine_capture_vma *vma = NULL;

	ee->simulated |= record_context(&ee->context, ce);
	if (ee->simulated)
		return NULL;

	/*
	 * We need to copy these to an anonymous buffer
	 * as the simplest method to avoid being overwritten
	 * by userspace.
	 */
	vma = capture_vma(vma, ce->ring->vma, "ring", gfp);
	vma = capture_vma(vma, ce->state, "HW context", gfp);

	return vma;
}

struct intel_engine_capture_vma *
intel_engine_coredump_add_request(struct intel_engine_coredump *ee,
				  struct i915_request *rq,
				  gfp_t gfp)
{
	struct intel_engine_capture_vma *vma;

	vma = engine_coredump_add_context(ee, rq->context, gfp);
	if (!vma)
		return NULL;

	/*
	 * We need to copy these to an anonymous buffer
	 * as the simplest method to avoid being overwritten
	 * by userspace.
	 */
	vma = capture_vma_snapshot(vma, rq->batch_res, gfp, "batch");
	vma = capture_user(vma, rq, gfp);

	ee->rq_head = rq->head;
	ee->rq_post = rq->postfix;
	ee->rq_tail = rq->tail;

	return vma;
}

void
intel_engine_coredump_add_vma(struct intel_engine_coredump *ee,
			      struct intel_engine_capture_vma *capture,
			      struct i915_vma_compress *compress)
{
	const struct intel_engine_cs *engine = ee->engine;

	while (capture) {
		struct intel_engine_capture_vma *this = capture;
		struct i915_vma_resource *vma_res = this->vma_res;

		add_vma(ee,
			i915_vma_coredump_create(engine->gt, vma_res,
						 compress, this->name));

		i915_vma_resource_unhold(vma_res, this->lockdep_cookie);
		i915_vma_resource_put(vma_res);

		capture = this->next;
		kfree(this);
	}

	add_vma_coredump(ee, engine->gt, engine->status_page.vma,
			 "HW Status", compress);

	add_vma_coredump(ee, engine->gt, engine->wa_ctx.vma,
			 "WA context", compress);
}

static struct intel_engine_coredump *
capture_engine(struct intel_engine_cs *engine,
	       struct i915_vma_compress *compress,
	       u32 dump_flags)
{
	struct intel_engine_capture_vma *capture = NULL;
	struct intel_engine_coredump *ee;
	struct intel_context *ce = NULL;
	struct i915_request *rq = NULL;

	ee = intel_engine_coredump_alloc(engine, ALLOW_FAIL, dump_flags);
	if (!ee)
		return NULL;

	intel_engine_get_hung_entity(engine, &ce, &rq);
	if (rq && !i915_request_started(rq))
		drm_info(&engine->gt->i915->drm, "Got hung context on %s with active request %lld:%lld [0x%04X] not yet started\n",
			 engine->name, rq->fence.context, rq->fence.seqno, ce->guc_id.id);

	if (rq) {
		capture = intel_engine_coredump_add_request(ee, rq, ATOMIC_MAYFAIL);
		i915_request_put(rq);
	} else if (ce) {
		capture = engine_coredump_add_context(ee, ce, ATOMIC_MAYFAIL);
	}

	if (capture) {
		intel_engine_coredump_add_vma(ee, capture, compress);

		if (dump_flags & CORE_DUMP_FLAG_IS_GUC_CAPTURE)
			intel_guc_capture_get_matching_node(engine->gt, ee, ce);
	} else {
		kfree(ee);
		ee = NULL;
	}

	return ee;
}

static void
gt_record_engines(struct intel_gt_coredump *gt,
		  intel_engine_mask_t engine_mask,
		  struct i915_vma_compress *compress,
		  u32 dump_flags)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	for_each_engine(engine, gt->_gt, id) {
		struct intel_engine_coredump *ee;

		/* Refill our page pool before entering atomic section */
		pool_refill(&compress->pool, ALLOW_FAIL);

		ee = capture_engine(engine, compress, dump_flags);
		if (!ee)
			continue;

		ee->hung = engine->mask & engine_mask;

		gt->simulated |= ee->simulated;
		if (ee->simulated) {
			if (dump_flags & CORE_DUMP_FLAG_IS_GUC_CAPTURE)
				intel_guc_capture_free_node(ee);
			kfree(ee);
			continue;
		}

		ee->next = gt->engine;
		gt->engine = ee;
	}
}

static void gt_record_guc_ctb(struct intel_ctb_coredump *saved,
			      const struct intel_guc_ct_buffer *ctb,
			      const void *blob_ptr, struct intel_guc *guc)
{
	if (!ctb || !ctb->desc)
		return;

	saved->raw_status = ctb->desc->status;
	saved->raw_head = ctb->desc->head;
	saved->raw_tail = ctb->desc->tail;
	saved->head = ctb->head;
	saved->tail = ctb->tail;
	saved->size = ctb->size;
	saved->desc_offset = ((void *)ctb->desc) - blob_ptr;
	saved->cmds_offset = ((void *)ctb->cmds) - blob_ptr;
}

static struct intel_uc_coredump *
gt_record_uc(struct intel_gt_coredump *gt,
	     struct i915_vma_compress *compress)
{
	const struct intel_uc *uc = &gt->_gt->uc;
	struct intel_uc_coredump *error_uc;

	error_uc = kzalloc(sizeof(*error_uc), ALLOW_FAIL);
	if (!error_uc)
		return NULL;

	memcpy(&error_uc->guc_fw, &uc->guc.fw, sizeof(uc->guc.fw));
	memcpy(&error_uc->huc_fw, &uc->huc.fw, sizeof(uc->huc.fw));

	error_uc->guc_fw.file_selected.path = kstrdup(uc->guc.fw.file_selected.path, ALLOW_FAIL);
	error_uc->huc_fw.file_selected.path = kstrdup(uc->huc.fw.file_selected.path, ALLOW_FAIL);
	error_uc->guc_fw.file_wanted.path = kstrdup(uc->guc.fw.file_wanted.path, ALLOW_FAIL);
	error_uc->huc_fw.file_wanted.path = kstrdup(uc->huc.fw.file_wanted.path, ALLOW_FAIL);

	/*
	 * Save the GuC log and include a timestamp reference for converting the
	 * log times to system times (in conjunction with the error->boottime and
	 * gt->clock_frequency fields saved elsewhere).
	 */
	error_uc->guc.timestamp = intel_uncore_read(gt->_gt->uncore, GUCPMTIMESTAMP);
	error_uc->guc.vma_log = create_vma_coredump(gt->_gt, uc->guc.log.vma,
						    "GuC log buffer", compress);
	error_uc->guc.vma_ctb = create_vma_coredump(gt->_gt, uc->guc.ct.vma,
						    "GuC CT buffer", compress);
	error_uc->guc.last_fence = uc->guc.ct.requests.last_fence;
	gt_record_guc_ctb(error_uc->guc.ctb + 0, &uc->guc.ct.ctbs.send,
			  uc->guc.ct.ctbs.send.desc, (struct intel_guc *)&uc->guc);
	gt_record_guc_ctb(error_uc->guc.ctb + 1, &uc->guc.ct.ctbs.recv,
			  uc->guc.ct.ctbs.send.desc, (struct intel_guc *)&uc->guc);

	return error_uc;
}

/* Capture display registers. */
static void gt_record_display_regs(struct intel_gt_coredump *gt)
{
	struct intel_uncore *uncore = gt->_gt->uncore;
	struct drm_i915_private *i915 = uncore->i915;

	if (DISPLAY_VER(i915) >= 6 && DISPLAY_VER(i915) < 20)
		gt->derrmr = intel_uncore_read(uncore, DERRMR);

	if (GRAPHICS_VER(i915) >= 8)
		gt->ier = intel_uncore_read(uncore, GEN8_DE_MISC_IER);
	else if (IS_VALLEYVIEW(i915))
		gt->ier = intel_uncore_read(uncore, VLV_IER);
	else if (HAS_PCH_SPLIT(i915))
		gt->ier = intel_uncore_read(uncore, DEIER);
	else if (GRAPHICS_VER(i915) == 2)
		gt->ier = intel_uncore_read16(uncore, GEN2_IER);
	else
		gt->ier = intel_uncore_read(uncore, GEN2_IER);
}

/* Capture all other registers that GuC doesn't capture. */
static void gt_record_global_nonguc_regs(struct intel_gt_coredump *gt)
{
	struct intel_uncore *uncore = gt->_gt->uncore;
	struct drm_i915_private *i915 = uncore->i915;
	int i;

	if (IS_VALLEYVIEW(i915)) {
		gt->gtier[0] = intel_uncore_read(uncore, GTIER);
		gt->ngtier = 1;
	} else if (GRAPHICS_VER(i915) >= 11) {
		gt->gtier[0] =
			intel_uncore_read(uncore,
					  GEN11_RENDER_COPY_INTR_ENABLE);
		gt->gtier[1] =
			intel_uncore_read(uncore, GEN11_VCS_VECS_INTR_ENABLE);
		gt->gtier[2] =
			intel_uncore_read(uncore, GEN11_GUC_SG_INTR_ENABLE);
		gt->gtier[3] =
			intel_uncore_read(uncore,
					  GEN11_GPM_WGBOXPERF_INTR_ENABLE);
		gt->gtier[4] =
			intel_uncore_read(uncore,
					  GEN11_CRYPTO_RSVD_INTR_ENABLE);
		gt->gtier[5] =
			intel_uncore_read(uncore,
					  GEN11_GUNIT_CSME_INTR_ENABLE);
		gt->ngtier = 6;
	} else if (GRAPHICS_VER(i915) >= 8) {
		for (i = 0; i < 4; i++)
			gt->gtier[i] =
				intel_uncore_read(uncore, GEN8_GT_IER(i));
		gt->ngtier = 4;
	} else if (HAS_PCH_SPLIT(i915)) {
		gt->gtier[0] = intel_uncore_read(uncore, GTIER);
		gt->ngtier = 1;
	}

	gt->eir = intel_uncore_read(uncore, EIR);
	gt->pgtbl_er = intel_uncore_read(uncore, PGTBL_ER);
}

/*
 * Capture all registers that relate to workload submission.
 * NOTE: In GuC submission, when GuC resets an engine, it can dump these for us
 */
static void gt_record_global_regs(struct intel_gt_coredump *gt)
{
	struct intel_uncore *uncore = gt->_gt->uncore;
	struct drm_i915_private *i915 = uncore->i915;
	int i;

	/*
	 * General organization
	 * 1. Registers specific to a single generation
	 * 2. Registers which belong to multiple generations
	 * 3. Feature specific registers.
	 * 4. Everything else
	 * Please try to follow the order.
	 */

	/* 1: Registers specific to a single generation */
	if (IS_VALLEYVIEW(i915))
		gt->forcewake = intel_uncore_read_fw(uncore, FORCEWAKE_VLV);

	if (GRAPHICS_VER(i915) == 7)
		gt->err_int = intel_uncore_read(uncore, GEN7_ERR_INT);

	if (GRAPHICS_VER_FULL(i915) >= IP_VER(12, 55)) {
		gt->fault_data0 = intel_gt_mcr_read_any((struct intel_gt *)gt->_gt,
							XEHP_FAULT_TLB_DATA0);
		gt->fault_data1 = intel_gt_mcr_read_any((struct intel_gt *)gt->_gt,
							XEHP_FAULT_TLB_DATA1);
	} else if (GRAPHICS_VER(i915) >= 12) {
		gt->fault_data0 = intel_uncore_read(uncore,
						    GEN12_FAULT_TLB_DATA0);
		gt->fault_data1 = intel_uncore_read(uncore,
						    GEN12_FAULT_TLB_DATA1);
	} else if (GRAPHICS_VER(i915) >= 8) {
		gt->fault_data0 = intel_uncore_read(uncore,
						    GEN8_FAULT_TLB_DATA0);
		gt->fault_data1 = intel_uncore_read(uncore,
						    GEN8_FAULT_TLB_DATA1);
	}

	if (GRAPHICS_VER(i915) == 6) {
		gt->forcewake = intel_uncore_read_fw(uncore, FORCEWAKE);
		gt->gab_ctl = intel_uncore_read(uncore, GAB_CTL);
		gt->gfx_mode = intel_uncore_read(uncore, GFX_MODE);
	}

	/* 2: Registers which belong to multiple generations */
	if (GRAPHICS_VER(i915) >= 7)
		gt->forcewake = intel_uncore_read_fw(uncore, FORCEWAKE_MT);

	if (GRAPHICS_VER(i915) >= 6) {
		if (GRAPHICS_VER(i915) < 12) {
			gt->error = intel_uncore_read(uncore, ERROR_GEN6);
			gt->done_reg = intel_uncore_read(uncore, DONE_REG);
		}
	}

	/* 3: Feature specific registers */
	if (IS_GRAPHICS_VER(i915, 6, 7)) {
		gt->gam_ecochk = intel_uncore_read(uncore, GAM_ECOCHK);
		gt->gac_eco = intel_uncore_read(uncore, GAC_ECO_BITS);
	}

	if (IS_GRAPHICS_VER(i915, 8, 11))
		gt->gtt_cache = intel_uncore_read(uncore, HSW_GTT_CACHE_EN);

	if (GRAPHICS_VER(i915) == 12)
		gt->aux_err = intel_uncore_read(uncore, GEN12_AUX_ERR_DBG);

	if (GRAPHICS_VER(i915) >= 12) {
		for (i = 0; i < I915_MAX_SFC; i++) {
			/*
			 * SFC_DONE resides in the VD forcewake domain, so it
			 * only exists if the corresponding VCS engine is
			 * present.
			 */
			if ((gt->_gt->info.sfc_mask & BIT(i)) == 0 ||
			    !HAS_ENGINE(gt->_gt, _VCS(i * 2)))
				continue;

			gt->sfc_done[i] =
				intel_uncore_read(uncore, GEN12_SFC_DONE(i));
		}

		gt->gam_done = intel_uncore_read(uncore, GEN12_GAM_DONE);
	}
}

static void gt_record_info(struct intel_gt_coredump *gt)
{
	memcpy(&gt->info, &gt->_gt->info, sizeof(struct intel_gt_info));
	gt->clock_frequency = gt->_gt->clock_frequency;
	gt->clock_period_ns = gt->_gt->clock_period_ns;
}

/*
 * Generate a semi-unique error code. The code is not meant to have meaning, The
 * code's only purpose is to try to prevent false duplicated bug reports by
 * grossly estimating a GPU error state.
 *
 * TODO Ideally, hashing the batchbuffer would be a very nice way to determine
 * the hang if we could strip the GTT offset information from it.
 *
 * It's only a small step better than a random number in its current form.
 */
static u32 generate_ecode(const struct intel_engine_coredump *ee)
{
	/*
	 * IPEHR would be an ideal way to detect errors, as it's the gross
	 * measure of "the command that hung." However, has some very common
	 * synchronization commands which almost always appear in the case
	 * strictly a client bug. Use instdone to differentiate those some.
	 */
	return ee ? ee->ipehr ^ ee->instdone.instdone : 0;
}

static const char *error_msg(struct i915_gpu_coredump *error)
{
	struct intel_engine_coredump *first = NULL;
	unsigned int hung_classes = 0;
	struct intel_gt_coredump *gt;
	int len;

	for (gt = error->gt; gt; gt = gt->next) {
		struct intel_engine_coredump *cs;

		for (cs = gt->engine; cs; cs = cs->next) {
			if (cs->hung) {
				hung_classes |= BIT(cs->engine->uabi_class);
				if (!first)
					first = cs;
			}
		}
	}

	len = scnprintf(error->error_msg, sizeof(error->error_msg),
			"GPU HANG: ecode %d:%x:%08x",
			GRAPHICS_VER(error->i915), hung_classes,
			generate_ecode(first));
	if (first && first->context.pid) {
		/* Just show the first executing process, more is confusing */
		len += scnprintf(error->error_msg + len,
				 sizeof(error->error_msg) - len,
				 ", in %s [%d]",
				 first->context.comm, first->context.pid);
	}

	return error->error_msg;
}

static void capture_gen(struct i915_gpu_coredump *error)
{
	struct drm_i915_private *i915 = error->i915;

	error->wakelock = atomic_read(&i915->runtime_pm.wakeref_count);
	error->suspended = pm_runtime_suspended(i915->drm.dev);

	error->iommu = i915_vtd_active(i915);
	error->reset_count = i915_reset_count(&i915->gpu_error);
	error->suspend_count = i915->suspend_count;

	i915_params_copy(&error->params, &i915->params);
	intel_display_params_copy(&error->display_params);
	memcpy(&error->device_info,
	       INTEL_INFO(i915),
	       sizeof(error->device_info));
	memcpy(&error->runtime_info,
	       RUNTIME_INFO(i915),
	       sizeof(error->runtime_info));
	memcpy(&error->display_device_info, DISPLAY_INFO(i915),
	       sizeof(error->display_device_info));
	memcpy(&error->display_runtime_info, DISPLAY_RUNTIME_INFO(i915),
	       sizeof(error->display_runtime_info));
	error->driver_caps = i915->caps;
}

struct i915_gpu_coredump *
i915_gpu_coredump_alloc(struct drm_i915_private *i915, gfp_t gfp)
{
	struct i915_gpu_coredump *error;

	if (!i915->params.error_capture)
		return NULL;

	error = kzalloc(sizeof(*error), gfp);
	if (!error)
		return NULL;

	kref_init(&error->ref);
	error->i915 = i915;

	error->time = ktime_get_real();
	error->boottime = ktime_get_boottime();
	error->uptime = ktime_sub(ktime_get(), to_gt(i915)->last_init_time);
	error->capture = jiffies;

	capture_gen(error);

	return error;
}

#define DAY_AS_SECONDS(x) (24 * 60 * 60 * (x))

struct intel_gt_coredump *
intel_gt_coredump_alloc(struct intel_gt *gt, gfp_t gfp, u32 dump_flags)
{
	struct intel_gt_coredump *gc;

	gc = kzalloc(sizeof(*gc), gfp);
	if (!gc)
		return NULL;

	gc->_gt = gt;
	gc->awake = intel_gt_pm_is_awake(gt);

	gt_record_display_regs(gc);
	gt_record_global_nonguc_regs(gc);

	/*
	 * GuC dumps global, eng-class and eng-instance registers
	 * (that can change as part of engine state during execution)
	 * before an engine is reset due to a hung context.
	 * GuC captures and reports all three groups of registers
	 * together as a single set before the engine is reset.
	 * Thus, if GuC triggered the context reset we retrieve
	 * the register values as part of gt_record_engines.
	 */
	if (!(dump_flags & CORE_DUMP_FLAG_IS_GUC_CAPTURE))
		gt_record_global_regs(gc);

	gt_record_fences(gc);

	return gc;
}

struct i915_vma_compress *
i915_vma_capture_prepare(struct intel_gt_coredump *gt)
{
	struct i915_vma_compress *compress;

	compress = kmalloc(sizeof(*compress), ALLOW_FAIL);
	if (!compress)
		return NULL;

	if (!compress_init(compress)) {
		kfree(compress);
		return NULL;
	}

	return compress;
}

void i915_vma_capture_finish(struct intel_gt_coredump *gt,
			     struct i915_vma_compress *compress)
{
	if (!compress)
		return;

	compress_fini(compress);
	kfree(compress);
}

static struct i915_gpu_coredump *
__i915_gpu_coredump(struct intel_gt *gt, intel_engine_mask_t engine_mask, u32 dump_flags)
{
	struct drm_i915_private *i915 = gt->i915;
	struct i915_gpu_coredump *error;

	/* Check if GPU capture has been disabled */
	error = READ_ONCE(i915->gpu_error.first_error);
	if (IS_ERR(error))
		return error;

	error = i915_gpu_coredump_alloc(i915, ALLOW_FAIL);
	if (!error)
		return ERR_PTR(-ENOMEM);

	error->gt = intel_gt_coredump_alloc(gt, ALLOW_FAIL, dump_flags);
	if (error->gt) {
		struct i915_vma_compress *compress;

		compress = i915_vma_capture_prepare(error->gt);
		if (!compress) {
			kfree(error->gt);
			kfree(error);
			return ERR_PTR(-ENOMEM);
		}

		if (INTEL_INFO(i915)->has_gt_uc) {
			error->gt->uc = gt_record_uc(error->gt, compress);
			if (error->gt->uc) {
				if (dump_flags & CORE_DUMP_FLAG_IS_GUC_CAPTURE)
					error->gt->uc->guc.is_guc_capture = true;
				else
					GEM_BUG_ON(error->gt->uc->guc.is_guc_capture);
			}
		}

		gt_record_info(error->gt);
		gt_record_engines(error->gt, engine_mask, compress, dump_flags);


		i915_vma_capture_finish(error->gt, compress);

		error->simulated |= error->gt->simulated;
	}

	error->overlay = intel_overlay_capture_error_state(i915);

	return error;
}

static struct i915_gpu_coredump *
i915_gpu_coredump(struct intel_gt *gt, intel_engine_mask_t engine_mask, u32 dump_flags)
{
	static DEFINE_MUTEX(capture_mutex);
	int ret = mutex_lock_interruptible(&capture_mutex);
	struct i915_gpu_coredump *dump;

	if (ret)
		return ERR_PTR(ret);

	dump = __i915_gpu_coredump(gt, engine_mask, dump_flags);
	mutex_unlock(&capture_mutex);

	return dump;
}

void i915_error_state_store(struct i915_gpu_coredump *error)
{
	struct drm_i915_private *i915;
	static bool warned;

	if (IS_ERR_OR_NULL(error))
		return;

	i915 = error->i915;
	drm_info(&i915->drm, "%s\n", error_msg(error));

	if (error->simulated ||
	    cmpxchg(&i915->gpu_error.first_error, NULL, error))
		return;

	i915_gpu_coredump_get(error);

	if (!xchg(&warned, true) &&
	    ktime_get_real_seconds() - DRIVER_TIMESTAMP < DAY_AS_SECONDS(180)) {
		pr_info("GPU hangs can indicate a bug anywhere in the entire gfx stack, including userspace.\n");
		pr_info("Please file a _new_ bug report at https://gitlab.freedesktop.org/drm/intel/issues/new.\n");
		pr_info("Please see https://drm.pages.freedesktop.org/intel-docs/how-to-file-i915-bugs.html for details.\n");
		pr_info("drm/i915 developers can then reassign to the right component if it's not a kernel issue.\n");
		pr_info("The GPU crash dump is required to analyze GPU hangs, so please always attach it.\n");
		pr_info("GPU crash dump saved to /sys/class/drm/card%d/error\n",
			i915->drm.primary->index);
	}
}

/**
 * i915_capture_error_state - capture an error record for later analysis
 * @gt: intel_gt which originated the hang
 * @engine_mask: hung engines
 * @dump_flags: dump flags
 *
 * Should be called when an error is detected (either a hang or an error
 * interrupt) to capture error state from the time of the error.  Fills
 * out a structure which becomes available in debugfs for user level tools
 * to pick up.
 */
void i915_capture_error_state(struct intel_gt *gt,
			      intel_engine_mask_t engine_mask, u32 dump_flags)
{
	struct i915_gpu_coredump *error;

	error = i915_gpu_coredump(gt, engine_mask, dump_flags);
	if (IS_ERR(error)) {
		cmpxchg(&gt->i915->gpu_error.first_error, NULL, error);
		return;
	}

	i915_error_state_store(error);
	i915_gpu_coredump_put(error);
}

static struct i915_gpu_coredump *
i915_first_error_state(struct drm_i915_private *i915)
{
	struct i915_gpu_coredump *error;

	spin_lock_irq(&i915->gpu_error.lock);
	error = i915->gpu_error.first_error;
	if (!IS_ERR_OR_NULL(error))
		i915_gpu_coredump_get(error);
	spin_unlock_irq(&i915->gpu_error.lock);

	return error;
}

void i915_reset_error_state(struct drm_i915_private *i915)
{
	struct i915_gpu_coredump *error;

	spin_lock_irq(&i915->gpu_error.lock);
	error = i915->gpu_error.first_error;
	if (error != ERR_PTR(-ENODEV)) /* if disabled, always disabled */
		i915->gpu_error.first_error = NULL;
	spin_unlock_irq(&i915->gpu_error.lock);

	if (!IS_ERR_OR_NULL(error))
		i915_gpu_coredump_put(error);
}

void i915_disable_error_state(struct drm_i915_private *i915, int err)
{
	spin_lock_irq(&i915->gpu_error.lock);
	if (!i915->gpu_error.first_error)
		i915->gpu_error.first_error = ERR_PTR(err);
	spin_unlock_irq(&i915->gpu_error.lock);
}

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM)
void intel_klog_error_capture(struct intel_gt *gt,
			      intel_engine_mask_t engine_mask)
{
	static int g_count;
	struct drm_i915_private *i915 = gt->i915;
	struct i915_gpu_coredump *error;
	intel_wakeref_t wakeref;
	size_t buf_size = PAGE_SIZE * 128;
	size_t pos_err;
	char *buf, *ptr, *next;
	int l_count = g_count++;
	int line = 0;

	/* Can't allocate memory during a reset */
	if (test_bit(I915_RESET_BACKOFF, &gt->reset.flags)) {
		drm_err(&gt->i915->drm, "[Capture/%d.%d] Inside GT reset, skipping error capture :(\n",
			l_count, line++);
		return;
	}

	error = READ_ONCE(i915->gpu_error.first_error);
	if (error) {
		drm_err(&i915->drm, "[Capture/%d.%d] Clearing existing error capture first...\n",
			l_count, line++);
		i915_reset_error_state(i915);
	}

	with_intel_runtime_pm(&i915->runtime_pm, wakeref)
		error = i915_gpu_coredump(gt, engine_mask, CORE_DUMP_FLAG_NONE);

	if (IS_ERR(error)) {
		drm_err(&i915->drm, "[Capture/%d.%d] Failed to capture error capture: %ld!\n",
			l_count, line++, PTR_ERR(error));
		return;
	}

	buf = kvmalloc(buf_size, GFP_KERNEL);
	if (!buf) {
		drm_err(&i915->drm, "[Capture/%d.%d] Failed to allocate buffer for error capture!\n",
			l_count, line++);
		i915_gpu_coredump_put(error);
		return;
	}

	drm_info(&i915->drm, "[Capture/%d.%d] Dumping i915 error capture for %ps...\n",
		 l_count, line++, __builtin_return_address(0));

	/* Largest string length safe to print via dmesg */
#	define MAX_CHUNK	800

	pos_err = 0;
	while (1) {
		ssize_t got = i915_gpu_coredump_copy_to_buffer(error, buf, pos_err, buf_size - 1);

		if (got <= 0)
			break;

		buf[got] = 0;
		pos_err += got;

		ptr = buf;
		while (got > 0) {
			size_t count;
			char tag[2];

			next = strnchr(ptr, got, '\n');
			if (next) {
				count = next - ptr;
				*next = 0;
				tag[0] = '>';
				tag[1] = '<';
			} else {
				count = got;
				tag[0] = '}';
				tag[1] = '{';
			}

			if (count > MAX_CHUNK) {
				size_t pos;
				char *ptr2 = ptr;

				for (pos = MAX_CHUNK; pos < count; pos += MAX_CHUNK) {
					char chr = ptr[pos];

					ptr[pos] = 0;
					drm_info(&i915->drm, "[Capture/%d.%d] }%s{\n",
						 l_count, line++, ptr2);
					ptr[pos] = chr;
					ptr2 = ptr + pos;

					/*
					 * If spewing large amounts of data via a serial console,
					 * this can be a very slow process. So be friendly and try
					 * not to cause 'softlockup on CPU' problems.
					 */
					cond_resched();
				}

				if (ptr2 < (ptr + count))
					drm_info(&i915->drm, "[Capture/%d.%d] %c%s%c\n",
						 l_count, line++, tag[0], ptr2, tag[1]);
				else if (tag[0] == '>')
					drm_info(&i915->drm, "[Capture/%d.%d] ><\n",
						 l_count, line++);
			} else {
				drm_info(&i915->drm, "[Capture/%d.%d] %c%s%c\n",
					 l_count, line++, tag[0], ptr, tag[1]);
			}

			ptr = next;
			got -= count;
			if (next) {
				ptr++;
				got--;
			}

			/* As above. */
			cond_resched();
		}

		if (got)
			drm_info(&i915->drm, "[Capture/%d.%d] Got %zd bytes remaining!\n",
				 l_count, line++, got);
	}

	kvfree(buf);

	drm_info(&i915->drm, "[Capture/%d.%d] Dumped %zd bytes\n", l_count, line++, pos_err);
}
#endif

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
		gpu = i915_gpu_coredump(to_gt(i915), ALL_ENGINES, CORE_DUMP_FLAG_NONE);

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

void i915_gpu_error_debugfs_register(struct drm_i915_private *i915)
{
	struct drm_minor *minor = i915->drm.primary;

	debugfs_create_file("i915_error_state", 0644, minor->debugfs_root, i915,
			    &i915_error_state_fops);
	debugfs_create_file("i915_gpu_info", 0644, minor->debugfs_root, i915,
			    &i915_gpu_info_fops);
}

static ssize_t error_state_read(struct file *filp, struct kobject *kobj,
				struct bin_attribute *attr, char *buf,
				loff_t off, size_t count)
{

	struct device *kdev = kobj_to_dev(kobj);
	struct drm_i915_private *i915 = kdev_minor_to_i915(kdev);
	struct i915_gpu_coredump *gpu;
	ssize_t ret = 0;

	/*
	 * FIXME: Concurrent clients triggering resets and reading + clearing
	 * dumps can cause inconsistent sysfs reads when a user calls in with a
	 * non-zero offset to complete a prior partial read but the
	 * gpu_coredump has been cleared or replaced.
	 */

	gpu = i915_first_error_state(i915);
	if (IS_ERR(gpu)) {
		ret = PTR_ERR(gpu);
	} else if (gpu) {
		ret = i915_gpu_coredump_copy_to_buffer(gpu, buf, off, count);
		i915_gpu_coredump_put(gpu);
	} else {
		const char *str = "No error state collected\n";
		size_t len = strlen(str);

		if (off < len) {
			ret = min_t(size_t, count, len - off);
			memcpy(buf, str + off, ret);
		}
	}

	return ret;
}

static ssize_t error_state_write(struct file *file, struct kobject *kobj,
				 struct bin_attribute *attr, char *buf,
				 loff_t off, size_t count)
{
	struct device *kdev = kobj_to_dev(kobj);
	struct drm_i915_private *dev_priv = kdev_minor_to_i915(kdev);

	drm_dbg(&dev_priv->drm, "Resetting error state\n");
	i915_reset_error_state(dev_priv);

	return count;
}

static const struct bin_attribute error_state_attr = {
	.attr.name = "error",
	.attr.mode = S_IRUSR | S_IWUSR,
	.size = 0,
	.read = error_state_read,
	.write = error_state_write,
};

void i915_gpu_error_sysfs_setup(struct drm_i915_private *i915)
{
	struct device *kdev = i915->drm.primary->kdev;

	if (sysfs_create_bin_file(&kdev->kobj, &error_state_attr))
		drm_err(&i915->drm, "error_state sysfs setup failed\n");
}

void i915_gpu_error_sysfs_teardown(struct drm_i915_private *i915)
{
	struct device *kdev = i915->drm.primary->kdev;

	sysfs_remove_bin_file(&kdev->kobj, &error_state_attr);
}
