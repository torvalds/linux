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
#include <linux/nmi.h>
#include <linux/pagevec.h>
#include <linux/scatterlist.h>
#include <linux/utsname.h>
#include <linux/zlib.h>

#include <drm/drm_print.h>

#include "display/intel_atomic.h"
#include "display/intel_overlay.h"

#include "gem/i915_gem_context.h"
#include "gem/i915_gem_lmem.h"
#include "gt/intel_gt_pm.h"

#include "i915_drv.h"
#include "i915_gpu_error.h"
#include "i915_memcpy.h"
#include "i915_scatterlist.h"
#include "intel_csr.h"

#define ALLOW_FAIL (GFP_KERNEL | __GFP_RETRY_MAYFAIL | __GFP_NOWARN)
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
static void pool_fini(struct pagevec *pv)
{
	pagevec_release(pv);
}

static int pool_refill(struct pagevec *pv, gfp_t gfp)
{
	while (pagevec_space(pv)) {
		struct page *p;

		p = alloc_page(gfp);
		if (!p)
			return -ENOMEM;

		pagevec_add(pv, p);
	}

	return 0;
}

static int pool_init(struct pagevec *pv, gfp_t gfp)
{
	int err;

	pagevec_init(pv);

	err = pool_refill(pv, gfp);
	if (err)
		pool_fini(pv);

	return err;
}

static void *pool_alloc(struct pagevec *pv, gfp_t gfp)
{
	struct page *p;

	p = alloc_page(gfp);
	if (!p && pagevec_count(pv))
		p = pv->pages[--pv->nr];

	return p ? page_address(p) : NULL;
}

static void pool_free(struct pagevec *pv, void *addr)
{
	struct page *p = virt_to_page(addr);

	if (pagevec_space(pv))
		pagevec_add(pv, p);
	else
		__free_page(p);
}

#ifdef CONFIG_DRM_I915_COMPRESS_ERROR

struct i915_vma_compress {
	struct pagevec pool;
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
	void *page;

	if (dst->page_count >= dst->num_pages)
		return ERR_PTR(-ENOSPC);

	page = pool_alloc(&c->pool, ALLOW_FAIL);
	if (!page)
		return ERR_PTR(-ENOMEM);

	return dst->pages[dst->page_count++] = page;
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
	struct pagevec pool;
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
	dst->pages[dst->page_count++] = ptr;

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
	const struct sseu_dev_info *sseu = &RUNTIME_INFO(m->i915)->sseu;
	int slice;
	int subslice;

	err_printf(m, "  INSTDONE: 0x%08x\n",
		   ee->instdone.instdone);

	if (ee->engine->class != RENDER_CLASS || INTEL_GEN(m->i915) <= 3)
		return;

	err_printf(m, "  SC_INSTDONE: 0x%08x\n",
		   ee->instdone.slice_common);

	if (INTEL_GEN(m->i915) <= 6)
		return;

	for_each_instdone_slice_subslice(m->i915, sseu, slice, subslice)
		err_printf(m, "  SAMPLER_INSTDONE[%d][%d]: 0x%08x\n",
			   slice, subslice,
			   ee->instdone.sampler[slice][subslice]);

	for_each_instdone_slice_subslice(m->i915, sseu, slice, subslice)
		err_printf(m, "  ROW_INSTDONE[%d][%d]: 0x%08x\n",
			   slice, subslice,
			   ee->instdone.row[slice][subslice]);
}

static void error_print_request(struct drm_i915_error_state_buf *m,
				const char *prefix,
				const struct i915_request_coredump *erq)
{
	if (!erq->seqno)
		return;

	err_printf(m, "%s pid %d, seqno %8x:%08x%s%s, prio %d, start %08x, head %08x, tail %08x\n",
		   prefix, erq->pid, erq->context, erq->seqno,
		   test_bit(DMA_FENCE_FLAG_SIGNALED_BIT,
			    &erq->flags) ? "!" : "",
		   test_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT,
			    &erq->flags) ? "+" : "",
		   erq->sched_attr.priority,
		   erq->start, erq->head, erq->tail);
}

static void error_print_context(struct drm_i915_error_state_buf *m,
				const char *header,
				const struct i915_gem_context_coredump *ctx)
{
	err_printf(m, "%s%s[%d] prio %d, guilty %d active %d\n",
		   header, ctx->comm, ctx->pid, ctx->sched_attr.priority,
		   ctx->guilty, ctx->active);
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
find_batch(const struct intel_engine_coredump *ee)
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

	error_print_instdone(m, ee);

	batch = find_batch(ee);
	if (batch) {
		u64 start = batch->gtt_offset;
		u64 end = start + batch->gtt_size;

		err_printf(m, "  batch: [0x%08x_%08x, 0x%08x_%08x]\n",
			   upper_32_bits(start), lower_32_bits(start),
			   upper_32_bits(end), lower_32_bits(end));
	}
	if (INTEL_GEN(m->i915) >= 4) {
		err_printf(m, "  BBADDR: 0x%08x_%08x\n",
			   (u32)(ee->bbaddr>>32), (u32)ee->bbaddr);
		err_printf(m, "  BB_STATE: 0x%08x\n", ee->bbstate);
		err_printf(m, "  INSTPS: 0x%08x\n", ee->instps);
	}
	err_printf(m, "  INSTPM: 0x%08x\n", ee->instpm);
	err_printf(m, "  FADDR: 0x%08x %08x\n", upper_32_bits(ee->faddr),
		   lower_32_bits(ee->faddr));
	if (INTEL_GEN(m->i915) >= 6) {
		err_printf(m, "  RC PSMI: 0x%08x\n", ee->rc_psmi);
		err_printf(m, "  FAULT_REG: 0x%08x\n", ee->fault_reg);
	}
	if (HAS_PPGTT(m->i915)) {
		err_printf(m, "  GFX_MODE: 0x%08x\n", ee->vm_info.gfx_mode);

		if (INTEL_GEN(m->i915) >= 8) {
			int i;
			for (i = 0; i < 4; i++)
				err_printf(m, "  PDP%d: 0x%016llx\n",
					   i, ee->vm_info.pdp[i]);
		} else {
			err_printf(m, "  PP_DIR_BASE: 0x%08x\n",
				   ee->vm_info.pp_dir_base);
		}
	}
	err_printf(m, "  engine reset count: %u\n", ee->reset_count);

	for (n = 0; n < ee->num_ports; n++) {
		err_printf(m, "  ELSP[%d]:", n);
		error_print_request(m, " ", &ee->execlist[n]);
	}

	error_print_context(m, "  Active context: ", &ee->context);
}

void i915_error_printf(struct drm_i915_error_state_buf *e, const char *f, ...)
{
	va_list args;

	va_start(args, f);
	i915_error_vprintf(e, f, args);
	va_end(args);
}

static void print_error_vma(struct drm_i915_error_state_buf *m,
			    const struct intel_engine_cs *engine,
			    const struct i915_vma_coredump *vma)
{
	char out[ASCII85_BUFSZ];
	int page;

	if (!vma)
		return;

	err_printf(m, "%s --- %s = 0x%08x %08x\n",
		   engine ? engine->name : "global", vma->name,
		   upper_32_bits(vma->gtt_offset),
		   lower_32_bits(vma->gtt_offset));

	if (vma->gtt_page_sizes > I915_GTT_PAGE_SIZE_4K)
		err_printf(m, "gtt_page_sizes = 0x%08x\n", vma->gtt_page_sizes);

	err_compression_marker(m);
	for (page = 0; page < vma->page_count; page++) {
		int i, len;

		len = PAGE_SIZE;
		if (page == vma->page_count - 1)
			len -= vma->unused;
		len = ascii85_encode_len(len);

		for (i = 0; i < len; i++)
			err_puts(m, ascii85_encode(vma->pages[page][i], out));
	}
	err_puts(m, "\n");
}

static void err_print_capabilities(struct drm_i915_error_state_buf *m,
				   const struct intel_device_info *info,
				   const struct intel_runtime_info *runtime,
				   const struct intel_driver_caps *caps)
{
	struct drm_printer p = i915_error_printer(m);

	intel_device_info_print_static(info, &p);
	intel_device_info_print_runtime(runtime, &p);
	intel_device_info_print_topology(&runtime->sseu, &p);
	intel_driver_caps_print(caps, &p);
}

static void err_print_params(struct drm_i915_error_state_buf *m,
			     const struct i915_params *params)
{
	struct drm_printer p = i915_error_printer(m);

	i915_params_dump(params, &p);
}

static void err_print_pciid(struct drm_i915_error_state_buf *m,
			    struct drm_i915_private *i915)
{
	struct pci_dev *pdev = i915->drm.pdev;

	err_printf(m, "PCI ID: 0x%04x\n", pdev->device);
	err_printf(m, "PCI Revision: 0x%02x\n", pdev->revision);
	err_printf(m, "PCI Subsystem: %04x:%04x\n",
		   pdev->subsystem_vendor,
		   pdev->subsystem_device);
}

static void err_print_uc(struct drm_i915_error_state_buf *m,
			 const struct intel_uc_coredump *error_uc)
{
	struct drm_printer p = i915_error_printer(m);

	intel_uc_fw_dump(&error_uc->guc_fw, &p);
	intel_uc_fw_dump(&error_uc->huc_fw, &p);
	print_error_vma(m, NULL, error_uc->guc_log);
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

static void err_print_gt(struct drm_i915_error_state_buf *m,
			 struct intel_gt_coredump *gt)
{
	const struct intel_engine_coredump *ee;
	int i;

	err_printf(m, "GT awake: %s\n", yesno(gt->awake));
	err_printf(m, "EIR: 0x%08x\n", gt->eir);
	err_printf(m, "IER: 0x%08x\n", gt->ier);
	for (i = 0; i < gt->ngtier; i++)
		err_printf(m, "GTIER[%d]: 0x%08x\n", i, gt->gtier[i]);
	err_printf(m, "PGTBL_ER: 0x%08x\n", gt->pgtbl_er);
	err_printf(m, "FORCEWAKE: 0x%08x\n", gt->forcewake);
	err_printf(m, "DERRMR: 0x%08x\n", gt->derrmr);

	for (i = 0; i < gt->nfence; i++)
		err_printf(m, "  fence[%d] = %08llx\n", i, gt->fence[i]);

	if (IS_GEN_RANGE(m->i915, 6, 11)) {
		err_printf(m, "ERROR: 0x%08x\n", gt->error);
		err_printf(m, "DONE_REG: 0x%08x\n", gt->done_reg);
	}

	if (INTEL_GEN(m->i915) >= 8)
		err_printf(m, "FAULT_TLB_DATA: 0x%08x 0x%08x\n",
			   gt->fault_data1, gt->fault_data0);

	if (IS_GEN(m->i915, 7))
		err_printf(m, "ERR_INT: 0x%08x\n", gt->err_int);

	if (IS_GEN_RANGE(m->i915, 8, 11))
		err_printf(m, "GTT_CACHE_EN: 0x%08x\n", gt->gtt_cache);

	if (IS_GEN(m->i915, 12))
		err_printf(m, "AUX_ERR_DBG: 0x%08x\n", gt->aux_err);

	if (INTEL_GEN(m->i915) >= 12) {
		int i;

		for (i = 0; i < GEN12_SFC_DONE_MAX; i++)
			err_printf(m, "  SFC_DONE[%d]: 0x%08x\n", i,
				   gt->sfc_done[i]);

		err_printf(m, "  GAM_DONE: 0x%08x\n", gt->gam_done);
	}

	for (ee = gt->engine; ee; ee = ee->next) {
		const struct i915_vma_coredump *vma;

		error_print_engine(m, ee);
		for (vma = ee->vma; vma; vma = vma->next)
			print_error_vma(m, ee->engine, vma);
	}

	if (gt->uc)
		err_print_uc(m, gt->uc);
}

static void __err_print_to_sgl(struct drm_i915_error_state_buf *m,
			       struct i915_gpu_coredump *error)
{
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

	if (HAS_CSR(m->i915)) {
		struct intel_csr *csr = &m->i915->csr;

		err_printf(m, "DMC loaded: %s\n",
			   yesno(csr->dmc_payload != NULL));
		err_printf(m, "DMC fw version: %d.%d\n",
			   CSR_VERSION_MAJOR(csr->version),
			   CSR_VERSION_MINOR(csr->version));
	}

	err_printf(m, "RPM wakelock: %s\n", yesno(error->wakelock));
	err_printf(m, "PM suspended: %s\n", yesno(error->suspended));

	if (error->gt)
		err_print_gt(m, error->gt);

	if (error->overlay)
		intel_overlay_print_error_state(m, error->overlay);

	if (error->display)
		intel_display_print_error_state(m, error->display);

	err_print_capabilities(m, &error->device_info, &error->runtime_info,
			       &error->driver_caps);
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
		int page;

		for (page = 0; page < vma->page_count; page++)
			free_page((unsigned long)vma->pages[page]);

		kfree(vma);
		vma = next;
	}
}

static void cleanup_params(struct i915_gpu_coredump *error)
{
	i915_params_free(&error->params);
}

static void cleanup_uc(struct intel_uc_coredump *uc)
{
	kfree(uc->guc_fw.path);
	kfree(uc->huc_fw.path);
	i915_vma_coredump_free(uc->guc_log);

	kfree(uc);
}

static void cleanup_gt(struct intel_gt_coredump *gt)
{
	while (gt->engine) {
		struct intel_engine_coredump *ee = gt->engine;

		gt->engine = ee->next;

		i915_vma_coredump_free(ee->vma);
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
	kfree(error->display);

	cleanup_params(error);

	err_free_sgl(error->sgl);
	kfree(error);
}

static struct i915_vma_coredump *
i915_vma_coredump_create(const struct intel_gt *gt,
			 const struct i915_vma *vma,
			 const char *name,
			 struct i915_vma_compress *compress)
{
	struct i915_ggtt *ggtt = gt->ggtt;
	const u64 slot = ggtt->error_capture.start;
	struct i915_vma_coredump *dst;
	unsigned long num_pages;
	struct sgt_iter iter;
	int ret;

	might_sleep();

	if (!vma || !vma->pages || !compress)
		return NULL;

	num_pages = min_t(u64, vma->size, vma->obj->base.size) >> PAGE_SHIFT;
	num_pages = DIV_ROUND_UP(10 * num_pages, 8); /* worstcase zlib growth */
	dst = kmalloc(sizeof(*dst) + num_pages * sizeof(u32 *), ALLOW_FAIL);
	if (!dst)
		return NULL;

	if (!compress_start(compress)) {
		kfree(dst);
		return NULL;
	}

	strcpy(dst->name, name);
	dst->next = NULL;

	dst->gtt_offset = vma->node.start;
	dst->gtt_size = vma->node.size;
	dst->gtt_page_sizes = vma->page_sizes.gtt;
	dst->num_pages = num_pages;
	dst->page_count = 0;
	dst->unused = 0;

	ret = -EINVAL;
	if (drm_mm_node_allocated(&ggtt->error_capture)) {
		void __iomem *s;
		dma_addr_t dma;

		for_each_sgt_daddr(dma, iter, vma->pages) {
			ggtt->vm.insert_page(&ggtt->vm, dma, slot,
					     I915_CACHE_NONE, 0);
			mb();

			s = io_mapping_map_wc(&ggtt->iomap, slot, PAGE_SIZE);
			ret = compress_page(compress,
					    (void  __force *)s, dst,
					    true);
			io_mapping_unmap(s);
			if (ret)
				break;
		}
	} else if (i915_gem_object_is_lmem(vma->obj)) {
		struct intel_memory_region *mem = vma->obj->mm.region;
		dma_addr_t dma;

		for_each_sgt_daddr(dma, iter, vma->pages) {
			void __iomem *s;

			s = io_mapping_map_wc(&mem->iomap, dma, PAGE_SIZE);
			ret = compress_page(compress,
					    (void __force *)s, dst,
					    true);
			io_mapping_unmap(s);
			if (ret)
				break;
		}
	} else {
		struct page *page;

		for_each_sgt_page(page, iter, vma->pages) {
			void *s;

			drm_clflush_pages(&page, 1);

			s = kmap(page);
			ret = compress_page(compress, s, dst, false);
			kunmap(page);

			drm_clflush_pages(&page, 1);

			if (ret)
				break;
		}
	}

	if (ret || compress_flush(compress, dst)) {
		while (dst->page_count--)
			pool_free(&compress->pool, dst->pages[dst->page_count]);
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

	if (INTEL_GEN(uncore->i915) >= 6) {
		for (i = 0; i < ggtt->num_fences; i++)
			gt->fence[i] =
				intel_uncore_read64(uncore,
						    FENCE_REG_GEN6_LO(i));
	} else if (INTEL_GEN(uncore->i915) >= 4) {
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

	if (INTEL_GEN(i915) >= 6) {
		ee->rc_psmi = ENGINE_READ(engine, RING_PSMI_CTL);

		if (INTEL_GEN(i915) >= 12)
			ee->fault_reg = intel_uncore_read(engine->uncore,
							  GEN12_RING_FAULT_REG);
		else if (INTEL_GEN(i915) >= 8)
			ee->fault_reg = intel_uncore_read(engine->uncore,
							  GEN8_RING_FAULT_REG);
		else
			ee->fault_reg = GEN6_RING_FAULT_REG_READ(engine);
	}

	if (INTEL_GEN(i915) >= 4) {
		ee->faddr = ENGINE_READ(engine, RING_DMA_FADD);
		ee->ipeir = ENGINE_READ(engine, RING_IPEIR);
		ee->ipehr = ENGINE_READ(engine, RING_IPEHR);
		ee->instps = ENGINE_READ(engine, RING_INSTPS);
		ee->bbaddr = ENGINE_READ(engine, RING_BBADDR);
		ee->ccid = ENGINE_READ(engine, CCID);
		if (INTEL_GEN(i915) >= 8) {
			ee->faddr |= (u64)ENGINE_READ(engine, RING_DMA_FADD_UDW) << 32;
			ee->bbaddr |= (u64)ENGINE_READ(engine, RING_BBADDR_UDW) << 32;
		}
		ee->bbstate = ENGINE_READ(engine, RING_BBSTATE);
	} else {
		ee->faddr = ENGINE_READ(engine, DMA_FADD_I8XX);
		ee->ipeir = ENGINE_READ(engine, IPEIR);
		ee->ipehr = ENGINE_READ(engine, IPEHR);
	}

	intel_engine_get_instdone(engine, &ee->instdone);

	ee->instpm = ENGINE_READ(engine, RING_INSTPM);
	ee->acthd = intel_engine_get_active_head(engine);
	ee->start = ENGINE_READ(engine, RING_START);
	ee->head = ENGINE_READ(engine, RING_HEAD);
	ee->tail = ENGINE_READ(engine, RING_TAIL);
	ee->ctl = ENGINE_READ(engine, RING_CTL);
	if (INTEL_GEN(i915) > 2)
		ee->mode = ENGINE_READ(engine, RING_MI_MODE);

	if (!HWS_NEEDS_PHYSICAL(i915)) {
		i915_reg_t mmio;

		if (IS_GEN(i915, 7)) {
			switch (engine->id) {
			default:
				MISSING_CASE(engine->id);
				/* fall through */
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
		} else if (IS_GEN(engine->i915, 6)) {
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

		if (IS_GEN(i915, 6)) {
			ee->vm_info.pp_dir_base =
				ENGINE_READ(engine, RING_PP_DIR_BASE_READ);
		} else if (IS_GEN(i915, 7)) {
			ee->vm_info.pp_dir_base =
				ENGINE_READ(engine, RING_PP_DIR_BASE);
		} else if (INTEL_GEN(i915) >= 8) {
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
	const struct i915_gem_context *ctx;

	erq->flags = request->fence.flags;
	erq->context = request->fence.context;
	erq->seqno = request->fence.seqno;
	erq->sched_attr = request->sched.attr;
	erq->start = i915_ggtt_offset(request->ring->vma);
	erq->head = request->head;
	erq->tail = request->tail;

	erq->pid = 0;
	rcu_read_lock();
	ctx = rcu_dereference(request->context->gem_context);
	if (ctx)
		erq->pid = pid_nr(ctx->pid);
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
			   const struct i915_request *rq)
{
	struct i915_gem_context *ctx;
	struct task_struct *task;
	bool capture;

	rcu_read_lock();
	ctx = rcu_dereference(rq->context->gem_context);
	if (ctx && !kref_get_unless_zero(&ctx->ref))
		ctx = NULL;
	rcu_read_unlock();
	if (!ctx)
		return false;

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

	capture = i915_gem_context_no_error_capture(ctx);

	i915_gem_context_put(ctx);
	return capture;
}

struct intel_engine_capture_vma {
	struct intel_engine_capture_vma *next;
	struct i915_vma *vma;
	char name[16];
};

static struct intel_engine_capture_vma *
capture_vma(struct intel_engine_capture_vma *next,
	    struct i915_vma *vma,
	    const char *name,
	    gfp_t gfp)
{
	struct intel_engine_capture_vma *c;

	if (!vma)
		return next;

	c = kmalloc(sizeof(*c), gfp);
	if (!c)
		return next;

	if (!i915_active_acquire_if_busy(&vma->active)) {
		kfree(c);
		return next;
	}

	strcpy(c->name, name);
	c->vma = i915_vma_get(vma);

	c->next = next;
	return c;
}

static struct intel_engine_capture_vma *
capture_user(struct intel_engine_capture_vma *capture,
	     const struct i915_request *rq,
	     gfp_t gfp)
{
	struct i915_capture_list *c;

	for (c = rq->capture_list; c; c = c->next)
		capture = capture_vma(capture, c->vma, "user", gfp);

	return capture;
}

static struct i915_vma_coredump *
capture_object(const struct intel_gt *gt,
	       struct drm_i915_gem_object *obj,
	       const char *name,
	       struct i915_vma_compress *compress)
{
	if (obj && i915_gem_object_has_pages(obj)) {
		struct i915_vma fake = {
			.node = { .start = U64_MAX, .size = obj->base.size },
			.size = obj->base.size,
			.pages = obj->mm.pages,
			.obj = obj,
		};

		return i915_vma_coredump_create(gt, &fake, name, compress);
	} else {
		return NULL;
	}
}

static void add_vma(struct intel_engine_coredump *ee,
		    struct i915_vma_coredump *vma)
{
	if (vma) {
		vma->next = ee->vma;
		ee->vma = vma;
	}
}

struct intel_engine_coredump *
intel_engine_coredump_alloc(struct intel_engine_cs *engine, gfp_t gfp)
{
	struct intel_engine_coredump *ee;

	ee = kzalloc(sizeof(*ee), gfp);
	if (!ee)
		return NULL;

	ee->engine = engine;

	engine_record_registers(ee);
	engine_record_execlists(ee);

	return ee;
}

struct intel_engine_capture_vma *
intel_engine_coredump_add_request(struct intel_engine_coredump *ee,
				  struct i915_request *rq,
				  gfp_t gfp)
{
	struct intel_engine_capture_vma *vma = NULL;

	ee->simulated |= record_context(&ee->context, rq);
	if (ee->simulated)
		return NULL;

	/*
	 * We need to copy these to an anonymous buffer
	 * as the simplest method to avoid being overwritten
	 * by userspace.
	 */
	vma = capture_vma(vma, rq->batch, "batch", gfp);
	vma = capture_user(vma, rq, gfp);
	vma = capture_vma(vma, rq->ring->vma, "ring", gfp);
	vma = capture_vma(vma, rq->context->state, "HW context", gfp);

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
		struct i915_vma *vma = this->vma;

		add_vma(ee,
			i915_vma_coredump_create(engine->gt,
						 vma, this->name,
						 compress));

		i915_active_release(&vma->active);
		i915_vma_put(vma);

		capture = this->next;
		kfree(this);
	}

	add_vma(ee,
		i915_vma_coredump_create(engine->gt,
					 engine->status_page.vma,
					 "HW Status",
					 compress));

	add_vma(ee,
		i915_vma_coredump_create(engine->gt,
					 engine->wa_ctx.vma,
					 "WA context",
					 compress));

	add_vma(ee,
		capture_object(engine->gt,
			       engine->default_state,
			       "NULL context",
			       compress));
}

static struct intel_engine_coredump *
capture_engine(struct intel_engine_cs *engine,
	       struct i915_vma_compress *compress)
{
	struct intel_engine_capture_vma *capture = NULL;
	struct intel_engine_coredump *ee;
	struct i915_request *rq;
	unsigned long flags;

	ee = intel_engine_coredump_alloc(engine, GFP_KERNEL);
	if (!ee)
		return NULL;

	spin_lock_irqsave(&engine->active.lock, flags);
	rq = intel_engine_find_active_request(engine);
	if (rq)
		capture = intel_engine_coredump_add_request(ee, rq,
							    ATOMIC_MAYFAIL);
	spin_unlock_irqrestore(&engine->active.lock, flags);
	if (!capture) {
		kfree(ee);
		return NULL;
	}

	intel_engine_coredump_add_vma(ee, capture, compress);

	return ee;
}

static void
gt_record_engines(struct intel_gt_coredump *gt,
		  struct i915_vma_compress *compress)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	for_each_engine(engine, gt->_gt, id) {
		struct intel_engine_coredump *ee;

		/* Refill our page pool before entering atomic section */
		pool_refill(&compress->pool, ALLOW_FAIL);

		ee = capture_engine(engine, compress);
		if (!ee)
			continue;

		gt->simulated |= ee->simulated;
		if (ee->simulated) {
			kfree(ee);
			continue;
		}

		ee->next = gt->engine;
		gt->engine = ee;
	}
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

	/* Non-default firmware paths will be specified by the modparam.
	 * As modparams are generally accesible from the userspace make
	 * explicit copies of the firmware paths.
	 */
	error_uc->guc_fw.path = kstrdup(uc->guc.fw.path, ALLOW_FAIL);
	error_uc->huc_fw.path = kstrdup(uc->huc.fw.path, ALLOW_FAIL);
	error_uc->guc_log =
		i915_vma_coredump_create(gt->_gt,
					 uc->guc.log.vma, "GuC log buffer",
					 compress);

	return error_uc;
}

static void gt_capture_prepare(struct intel_gt_coredump *gt)
{
	struct i915_ggtt *ggtt = gt->_gt->ggtt;

	mutex_lock(&ggtt->error_mutex);
}

static void gt_capture_finish(struct intel_gt_coredump *gt)
{
	struct i915_ggtt *ggtt = gt->_gt->ggtt;

	if (drm_mm_node_allocated(&ggtt->error_capture))
		ggtt->vm.clear_range(&ggtt->vm,
				     ggtt->error_capture.start,
				     PAGE_SIZE);

	mutex_unlock(&ggtt->error_mutex);
}

/* Capture all registers which don't fit into another category. */
static void gt_record_regs(struct intel_gt_coredump *gt)
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
	if (IS_VALLEYVIEW(i915)) {
		gt->gtier[0] = intel_uncore_read(uncore, GTIER);
		gt->ier = intel_uncore_read(uncore, VLV_IER);
		gt->forcewake = intel_uncore_read_fw(uncore, FORCEWAKE_VLV);
	}

	if (IS_GEN(i915, 7))
		gt->err_int = intel_uncore_read(uncore, GEN7_ERR_INT);

	if (INTEL_GEN(i915) >= 12) {
		gt->fault_data0 = intel_uncore_read(uncore,
						    GEN12_FAULT_TLB_DATA0);
		gt->fault_data1 = intel_uncore_read(uncore,
						    GEN12_FAULT_TLB_DATA1);
	} else if (INTEL_GEN(i915) >= 8) {
		gt->fault_data0 = intel_uncore_read(uncore,
						    GEN8_FAULT_TLB_DATA0);
		gt->fault_data1 = intel_uncore_read(uncore,
						    GEN8_FAULT_TLB_DATA1);
	}

	if (IS_GEN(i915, 6)) {
		gt->forcewake = intel_uncore_read_fw(uncore, FORCEWAKE);
		gt->gab_ctl = intel_uncore_read(uncore, GAB_CTL);
		gt->gfx_mode = intel_uncore_read(uncore, GFX_MODE);
	}

	/* 2: Registers which belong to multiple generations */
	if (INTEL_GEN(i915) >= 7)
		gt->forcewake = intel_uncore_read_fw(uncore, FORCEWAKE_MT);

	if (INTEL_GEN(i915) >= 6) {
		gt->derrmr = intel_uncore_read(uncore, DERRMR);
		if (INTEL_GEN(i915) < 12) {
			gt->error = intel_uncore_read(uncore, ERROR_GEN6);
			gt->done_reg = intel_uncore_read(uncore, DONE_REG);
		}
	}

	/* 3: Feature specific registers */
	if (IS_GEN_RANGE(i915, 6, 7)) {
		gt->gam_ecochk = intel_uncore_read(uncore, GAM_ECOCHK);
		gt->gac_eco = intel_uncore_read(uncore, GAC_ECO_BITS);
	}

	if (IS_GEN_RANGE(i915, 8, 11))
		gt->gtt_cache = intel_uncore_read(uncore, HSW_GTT_CACHE_EN);

	if (IS_GEN(i915, 12))
		gt->aux_err = intel_uncore_read(uncore, GEN12_AUX_ERR_DBG);

	if (INTEL_GEN(i915) >= 12) {
		for (i = 0; i < GEN12_SFC_DONE_MAX; i++) {
			gt->sfc_done[i] =
				intel_uncore_read(uncore, GEN12_SFC_DONE(i));
		}

		gt->gam_done = intel_uncore_read(uncore, GEN12_GAM_DONE);
	}

	/* 4: Everything else */
	if (INTEL_GEN(i915) >= 11) {
		gt->ier = intel_uncore_read(uncore, GEN8_DE_MISC_IER);
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
	} else if (INTEL_GEN(i915) >= 8) {
		gt->ier = intel_uncore_read(uncore, GEN8_DE_MISC_IER);
		for (i = 0; i < 4; i++)
			gt->gtier[i] =
				intel_uncore_read(uncore, GEN8_GT_IER(i));
		gt->ngtier = 4;
	} else if (HAS_PCH_SPLIT(i915)) {
		gt->ier = intel_uncore_read(uncore, DEIER);
		gt->gtier[0] = intel_uncore_read(uncore, GTIER);
		gt->ngtier = 1;
	} else if (IS_GEN(i915, 2)) {
		gt->ier = intel_uncore_read16(uncore, GEN2_IER);
	} else if (!IS_VALLEYVIEW(i915)) {
		gt->ier = intel_uncore_read(uncore, GEN2_IER);
	}
	gt->eir = intel_uncore_read(uncore, EIR);
	gt->pgtbl_er = intel_uncore_read(uncore, PGTBL_ER);
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
	struct intel_gt_coredump *gt;
	intel_engine_mask_t engines;
	int len;

	engines = 0;
	for (gt = error->gt; gt; gt = gt->next) {
		struct intel_engine_coredump *cs;

		if (gt->engine && !first)
			first = gt->engine;

		for (cs = gt->engine; cs; cs = cs->next)
			engines |= cs->engine->mask;
	}

	len = scnprintf(error->error_msg, sizeof(error->error_msg),
			"GPU HANG: ecode %d:%x:%08x",
			INTEL_GEN(error->i915), engines,
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
	error->suspended = i915->runtime_pm.suspended;

	error->iommu = -1;
#ifdef CONFIG_INTEL_IOMMU
	error->iommu = intel_iommu_gfx_mapped;
#endif
	error->reset_count = i915_reset_count(&i915->gpu_error);
	error->suspend_count = i915->suspend_count;

	i915_params_copy(&error->params, &i915_modparams);
	memcpy(&error->device_info,
	       INTEL_INFO(i915),
	       sizeof(error->device_info));
	memcpy(&error->runtime_info,
	       RUNTIME_INFO(i915),
	       sizeof(error->runtime_info));
	error->driver_caps = i915->caps;
}

struct i915_gpu_coredump *
i915_gpu_coredump_alloc(struct drm_i915_private *i915, gfp_t gfp)
{
	struct i915_gpu_coredump *error;

	if (!i915_modparams.error_capture)
		return NULL;

	error = kzalloc(sizeof(*error), gfp);
	if (!error)
		return NULL;

	kref_init(&error->ref);
	error->i915 = i915;

	error->time = ktime_get_real();
	error->boottime = ktime_get_boottime();
	error->uptime = ktime_sub(ktime_get(), i915->gt.last_init_time);
	error->capture = jiffies;

	capture_gen(error);

	return error;
}

#define DAY_AS_SECONDS(x) (24 * 60 * 60 * (x))

struct intel_gt_coredump *
intel_gt_coredump_alloc(struct intel_gt *gt, gfp_t gfp)
{
	struct intel_gt_coredump *gc;

	gc = kzalloc(sizeof(*gc), gfp);
	if (!gc)
		return NULL;

	gc->_gt = gt;
	gc->awake = intel_gt_pm_is_awake(gt);

	gt_record_regs(gc);
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

	gt_capture_prepare(gt);

	return compress;
}

void i915_vma_capture_finish(struct intel_gt_coredump *gt,
			     struct i915_vma_compress *compress)
{
	if (!compress)
		return;

	gt_capture_finish(gt);

	compress_fini(compress);
	kfree(compress);
}

struct i915_gpu_coredump *i915_gpu_coredump(struct drm_i915_private *i915)
{
	struct i915_gpu_coredump *error;

	/* Check if GPU capture has been disabled */
	error = READ_ONCE(i915->gpu_error.first_error);
	if (IS_ERR(error))
		return error;

	error = i915_gpu_coredump_alloc(i915, ALLOW_FAIL);
	if (!error)
		return ERR_PTR(-ENOMEM);

	error->gt = intel_gt_coredump_alloc(&i915->gt, ALLOW_FAIL);
	if (error->gt) {
		struct i915_vma_compress *compress;

		compress = i915_vma_capture_prepare(error->gt);
		if (!compress) {
			kfree(error->gt);
			kfree(error);
			return ERR_PTR(-ENOMEM);
		}

		gt_record_engines(error->gt, compress);

		if (INTEL_INFO(i915)->has_gt_uc)
			error->gt->uc = gt_record_uc(error->gt, compress);

		i915_vma_capture_finish(error->gt, compress);

		error->simulated |= error->gt->simulated;
	}

	error->overlay = intel_overlay_capture_error_state(i915);
	error->display = intel_display_capture_error_state(i915);

	return error;
}

void i915_error_state_store(struct i915_gpu_coredump *error)
{
	struct drm_i915_private *i915;
	static bool warned;

	if (IS_ERR_OR_NULL(error))
		return;

	i915 = error->i915;
	dev_info(i915->drm.dev, "%s\n", error_msg(error));

	if (error->simulated ||
	    cmpxchg(&i915->gpu_error.first_error, NULL, error))
		return;

	i915_gpu_coredump_get(error);

	if (!xchg(&warned, true) &&
	    ktime_get_real_seconds() - DRIVER_TIMESTAMP < DAY_AS_SECONDS(180)) {
		pr_info("GPU hangs can indicate a bug anywhere in the entire gfx stack, including userspace.\n");
		pr_info("Please file a _new_ bug report on bugs.freedesktop.org against DRI -> DRM/Intel\n");
		pr_info("drm/i915 developers can then reassign to the right component if it's not a kernel issue.\n");
		pr_info("The GPU crash dump is required to analyze GPU hangs, so please always attach it.\n");
		pr_info("GPU crash dump saved to /sys/class/drm/card%d/error\n",
			i915->drm.primary->index);
	}
}

/**
 * i915_capture_error_state - capture an error record for later analysis
 * @i915: i915 device
 *
 * Should be called when an error is detected (either a hang or an error
 * interrupt) to capture error state from the time of the error.  Fills
 * out a structure which becomes available in debugfs for user level tools
 * to pick up.
 */
void i915_capture_error_state(struct drm_i915_private *i915)
{
	struct i915_gpu_coredump *error;

	error = i915_gpu_coredump(i915);
	if (IS_ERR(error)) {
		cmpxchg(&i915->gpu_error.first_error, NULL, error);
		return;
	}

	i915_error_state_store(error);
	i915_gpu_coredump_put(error);
}

struct i915_gpu_coredump *
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
