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

#define GEN8_LR_CONTEXT_ALIGN 4096

/* Execlists regs */
#define RING_ELSP(ring)				_MMIO((ring)->mmio_base + 0x230)
#define RING_EXECLIST_STATUS_LO(ring)		_MMIO((ring)->mmio_base + 0x234)
#define RING_EXECLIST_STATUS_HI(ring)		_MMIO((ring)->mmio_base + 0x234 + 4)
#define RING_CONTEXT_CONTROL(ring)		_MMIO((ring)->mmio_base + 0x244)
#define	  CTX_CTRL_INHIBIT_SYN_CTX_SWITCH	(1 << 3)
#define	  CTX_CTRL_ENGINE_CTX_RESTORE_INHIBIT	(1 << 0)
#define   CTX_CTRL_RS_CTX_ENABLE                (1 << 1)
#define RING_CONTEXT_STATUS_BUF_BASE(ring)	_MMIO((ring)->mmio_base + 0x370)
#define RING_CONTEXT_STATUS_BUF_LO(ring, i)	_MMIO((ring)->mmio_base + 0x370 + (i) * 8)
#define RING_CONTEXT_STATUS_BUF_HI(ring, i)	_MMIO((ring)->mmio_base + 0x370 + (i) * 8 + 4)
#define RING_CONTEXT_STATUS_PTR(ring)		_MMIO((ring)->mmio_base + 0x3a0)

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
#define GEN8_CSB_WRITE_PTR(csb_status) \
	(((csb_status) & GEN8_CSB_WRITE_PTR_MASK) >> 0)
#define GEN8_CSB_READ_PTR(csb_status) \
	(((csb_status) & GEN8_CSB_READ_PTR_MASK) >> 8)

/* Logical Rings */
int intel_logical_ring_alloc_request_extras(struct drm_i915_gem_request *request);
int intel_logical_ring_reserve_space(struct drm_i915_gem_request *request);
void intel_logical_ring_stop(struct intel_engine_cs *engine);
void intel_logical_ring_cleanup(struct intel_engine_cs *engine);
int intel_logical_rings_init(struct drm_device *dev);

int logical_ring_flush_all_caches(struct drm_i915_gem_request *req);
/**
 * intel_logical_ring_advance() - advance the ringbuffer tail
 * @ringbuf: Ringbuffer to advance.
 *
 * The tail is only updated in our logical ringbuffer struct.
 */
static inline void intel_logical_ring_advance(struct intel_ringbuffer *ringbuf)
{
	ringbuf->tail &= ringbuf->size - 1;
}
/**
 * intel_logical_ring_emit() - write a DWORD to the ringbuffer.
 * @ringbuf: Ringbuffer to write to.
 * @data: DWORD to write.
 */
static inline void intel_logical_ring_emit(struct intel_ringbuffer *ringbuf,
					   u32 data)
{
	iowrite32(data, ringbuf->virtual_start + ringbuf->tail);
	ringbuf->tail += 4;
}
static inline void intel_logical_ring_emit_reg(struct intel_ringbuffer *ringbuf,
					       i915_reg_t reg)
{
	intel_logical_ring_emit(ringbuf, i915_mmio_reg_offset(reg));
}

/* Logical Ring Contexts */

/* One extra page is added before LRC for GuC as shared data */
#define LRC_GUCSHR_PN	(0)
#define LRC_PPHWSP_PN	(LRC_GUCSHR_PN + 1)
#define LRC_STATE_PN	(LRC_PPHWSP_PN + 1)

struct i915_gem_context;

uint32_t intel_lr_context_size(struct intel_engine_cs *engine);
void intel_lr_context_unpin(struct i915_gem_context *ctx,
			    struct intel_engine_cs *engine);

struct drm_i915_private;

void intel_lr_context_reset(struct drm_i915_private *dev_priv,
			    struct i915_gem_context *ctx);
uint64_t intel_lr_context_descriptor(struct i915_gem_context *ctx,
				     struct intel_engine_cs *engine);

/* Execlists */
int intel_sanitize_enable_execlists(struct drm_i915_private *dev_priv,
				    int enable_execlists);
struct i915_execbuffer_params;
int intel_execlists_submission(struct i915_execbuffer_params *params,
			       struct drm_i915_gem_execbuffer2 *args,
			       struct list_head *vmas);

void intel_execlists_cancel_requests(struct intel_engine_cs *engine);

#endif /* _INTEL_LRC_H_ */
