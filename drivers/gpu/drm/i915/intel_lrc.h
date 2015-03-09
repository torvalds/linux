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

#define GEN8_LR_CONTEXT_ALIGN 4096

/* Execlists regs */
#define RING_ELSP(ring)			((ring)->mmio_base+0x230)
#define RING_EXECLIST_STATUS(ring)	((ring)->mmio_base+0x234)
#define RING_CONTEXT_CONTROL(ring)	((ring)->mmio_base+0x244)
#define	  CTX_CTRL_INHIBIT_SYN_CTX_SWITCH	(1 << 3)
#define	  CTX_CTRL_ENGINE_CTX_RESTORE_INHIBIT	(1 << 0)
#define RING_CONTEXT_STATUS_BUF(ring)	((ring)->mmio_base+0x370)
#define RING_CONTEXT_STATUS_PTR(ring)	((ring)->mmio_base+0x3a0)

/* Logical Rings */
void intel_logical_ring_stop(struct intel_engine_cs *ring);
void intel_logical_ring_cleanup(struct intel_engine_cs *ring);
int intel_logical_rings_init(struct drm_device *dev);

int logical_ring_flush_all_caches(struct intel_ringbuffer *ringbuf,
				  struct intel_context *ctx);
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
int intel_logical_ring_begin(struct intel_ringbuffer *ringbuf,
			     struct intel_context *ctx,
			     int num_dwords);

/* Logical Ring Contexts */
void intel_lr_context_free(struct intel_context *ctx);
int intel_lr_context_deferred_create(struct intel_context *ctx,
				     struct intel_engine_cs *ring);
void intel_lr_context_unpin(struct intel_engine_cs *ring,
		struct intel_context *ctx);
void intel_lr_context_reset(struct drm_device *dev,
			struct intel_context *ctx);

/* Execlists */
int intel_sanitize_enable_execlists(struct drm_device *dev, int enable_execlists);
int intel_execlists_submission(struct drm_device *dev, struct drm_file *file,
			       struct intel_engine_cs *ring,
			       struct intel_context *ctx,
			       struct drm_i915_gem_execbuffer2 *args,
			       struct list_head *vmas,
			       struct drm_i915_gem_object *batch_obj,
			       u64 exec_start, u32 dispatch_flags);
u32 intel_execlists_ctx_id(struct drm_i915_gem_object *ctx_obj);

void intel_lrc_irq_handler(struct intel_engine_cs *ring);
void intel_execlists_retire_requests(struct intel_engine_cs *ring);

#endif /* _INTEL_LRC_H_ */
