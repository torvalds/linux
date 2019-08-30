/*
 * Copyright © 2014 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef _INTEL_LRC_H_
#define _INTEL_LRC_H_

#include "intel_ringbuffer.h"
#include "i915_gem_context.h"

/* Execlists regs */
#define RING_ELSP(base)				_MMIO((base) + 0x230)
#define RING_EXECLIST_STATUS_LO(base)		_MMIO((base) + 0x234)
#define RING_EXECLIST_STATUS_HI(base)		_MMIO((base) + 0x234 + 4)
#define RING_CONTEXT_CONTROL(base)		_MMIO((base) + 0x244)
#define	  CTX_CTRL_INHIBIT_SYN_CTX_SWITCH	(1 << 3)
#define	  CTX_CTRL_ENGINE_CTX_RESTORE_INHIBIT	(1 << 0)
#define   CTX_CTRL_RS_CTX_ENABLE		(1 << 1)
#define	  CTX_CTRL_ENGINE_CTX_SAVE_INHIBIT	(1 << 2)
#define RING_CONTEXT_STATUS_PTR(base)		_MMIO((base) + 0x3a0)
#define RING_EXECLIST_SQ_CONTENTS(base)		_MMIO((base) + 0x510)
#define RING_EXECLIST_CONTROL(base)		_MMIO((base) + 0x550)

#define	  EL_CTRL_LOAD				(1 << 0)

/* The docs specify that the write pointer wraps around after 5h, "After status
 * is written out to the last available status QW at offset 5h, this pointer
 * wraps to 0."
 *
 * Therefore, one must infer than even though there are 3 bits available, 6 and
 * 7 appear to be * reserved.
 */
#define GEN8_CSB_ENTRIES 6
#define GEN8_CSB_PTR_MASK 0x7
#define GEN8_CSB_READ_PTR_MASK (GEN8_CSB_PTR_MASK << 8)
#define GEN8_CSB_WRITE_PTR_MASK (GEN8_CSB_PTR_MASK << 0)

#define GEN11_CSB_ENTRIES 12
#define GEN11_CSB_PTR_MASK 0xf
#define GEN11_CSB_READ_PTR_MASK (GEN11_CSB_PTR_MASK << 8)
#define GEN11_CSB_WRITE_PTR_MASK (GEN11_CSB_PTR_MASK << 0)

enum {
	INTEL_CONTEXT_SCHEDULE_IN = 0,
	INTEL_CONTEXT_SCHEDULE_OUT,
	INTEL_CONTEXT_SCHEDULE_PREEMPTED,
};

/* Logical Rings */
void intel_logical_ring_cleanup(struct intel_engine_cs *engine);
int logical_render_ring_init(struct intel_engine_cs *engine);
int logical_xcs_ring_init(struct intel_engine_cs *engine);

/* Logical Ring Contexts */

/*
 * We allocate a header at the start of the context image for our own
 * use, therefore the actual location of the logical state is offset
 * from the start of the VMA. The layout is
 *
 * | [guc]          | [hwsp] [logical state] |
 * |<- our header ->|<- context image      ->|
 *
 */
/* The first page is used for sharing data with the GuC */
#define LRC_GUCSHR_PN	(0)
#define LRC_GUCSHR_SZ	(1)
/* At the start of the context image is its per-process HWS page */
#define LRC_PPHWSP_PN	(LRC_GUCSHR_PN + LRC_GUCSHR_SZ)
#define LRC_PPHWSP_SZ	(1)
/* Finally we have the logical state for the context */
#define LRC_STATE_PN	(LRC_PPHWSP_PN + LRC_PPHWSP_SZ)

/*
 * Currently we include the PPHWSP in __intel_engine_context_size() so
 * the size of the header is synonymous with the start of the PPHWSP.
 */
#define LRC_HEADER_PAGES LRC_PPHWSP_PN

struct drm_printer;

struct drm_i915_private;
struct i915_gem_context;

void intel_execlists_set_default_submission(struct intel_engine_cs *engine);

void intel_lr_context_reset(struct intel_engine_cs *engine,
			    struct intel_context *ce,
			    u32 head,
			    bool scrub);

void intel_execlists_show_requests(struct intel_engine_cs *engine,
				   struct drm_printer *m,
				   void (*show_request)(struct drm_printer *m,
							struct i915_request *rq,
							const char *prefix),
				   unsigned int max);

u32 gen8_make_rpcs(struct drm_i915_private *i915, struct intel_sseu *ctx_sseu);

#endif /* _INTEL_LRC_H_ */
