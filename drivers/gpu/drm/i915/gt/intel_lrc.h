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

#include <linux/types.h>

struct drm_printer;

struct drm_i915_private;
struct i915_gem_context;
struct i915_request;
struct intel_context;
struct intel_engine_cs;

enum {
	INTEL_CONTEXT_SCHEDULE_IN = 0,
	INTEL_CONTEXT_SCHEDULE_OUT,
	INTEL_CONTEXT_SCHEDULE_PREEMPTED,
};

/* Logical Rings */
void intel_logical_ring_cleanup(struct intel_engine_cs *engine);

int intel_execlists_submission_setup(struct intel_engine_cs *engine);

/* Logical Ring Contexts */
/* At the start of the context image is its per-process HWS page */
#define LRC_PPHWSP_PN	(0)
#define LRC_PPHWSP_SZ	(1)
/* After the PPHWSP we have the logical state for the context */
#define LRC_STATE_PN	(LRC_PPHWSP_PN + LRC_PPHWSP_SZ)
#define LRC_STATE_OFFSET (LRC_STATE_PN * PAGE_SIZE)

/* Space within PPHWSP reserved to be used as scratch */
#define LRC_PPHWSP_SCRATCH		0x34
#define LRC_PPHWSP_SCRATCH_ADDR		(LRC_PPHWSP_SCRATCH * sizeof(u32))

void intel_execlists_set_default_submission(struct intel_engine_cs *engine);

void intel_lr_context_reset(struct intel_engine_cs *engine,
			    struct intel_context *ce,
			    u32 head,
			    bool scrub);

void intel_execlists_show_requests(struct intel_engine_cs *engine,
				   struct drm_printer *m,
				   void (*show_request)(struct drm_printer *m,
							const struct i915_request *rq,
							const char *prefix,
							int indent),
				   unsigned int max);

struct intel_context *
intel_execlists_create_virtual(struct intel_engine_cs **siblings,
			       unsigned int count);

struct intel_context *
intel_execlists_clone_virtual(struct intel_engine_cs *src);

int intel_virtual_engine_attach_bond(struct intel_engine_cs *engine,
				     const struct intel_engine_cs *master,
				     const struct intel_engine_cs *sibling);

bool
intel_engine_in_execlists_submission_mode(const struct intel_engine_cs *engine);

#endif /* _INTEL_LRC_H_ */
