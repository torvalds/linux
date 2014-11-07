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

#ifndef _INTEL_RENDERSTATE_H
#define _INTEL_RENDERSTATE_H

#include "i915_drv.h"

extern const struct intel_renderstate_rodata gen6_null_state;
extern const struct intel_renderstate_rodata gen7_null_state;
extern const struct intel_renderstate_rodata gen8_null_state;

#define RO_RENDERSTATE(_g)						\
	const struct intel_renderstate_rodata gen ## _g ## _null_state = { \
		.reloc = gen ## _g ## _null_state_relocs,		\
		.batch = gen ## _g ## _null_state_batch,		\
		.batch_items = sizeof(gen ## _g ## _null_state_batch)/4, \
	}

#endif /* INTEL_RENDERSTATE_H */
