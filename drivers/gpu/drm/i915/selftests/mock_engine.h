/*
 * Copyright © 2016 Intel Corporation
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
 */

#ifndef __MOCK_ENGINE_H__
#define __MOCK_ENGINE_H__

#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/timer.h>

#include "../intel_ringbuffer.h"

struct mock_engine {
	struct intel_engine_cs base;

	spinlock_t hw_lock;
	struct list_head hw_queue;
	struct timer_list hw_delay;
};

struct intel_engine_cs *mock_engine(struct drm_i915_private *i915,
				    const char *name,
				    int id);
void mock_engine_flush(struct intel_engine_cs *engine);
void mock_engine_reset(struct intel_engine_cs *engine);
void mock_engine_free(struct intel_engine_cs *engine);

#endif /* !__MOCK_ENGINE_H__ */
