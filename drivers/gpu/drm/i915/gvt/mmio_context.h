/*
 * Copyright(c) 2011-2016 Intel Corporation. All rights reserved.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Eddie Dong <eddie.dong@intel.com>
 *    Kevin Tian <kevin.tian@intel.com>
 *
 * Contributors:
 *    Zhi Wang <zhi.a.wang@intel.com>
 *    Changbin Du <changbin.du@intel.com>
 *    Zhenyu Wang <zhenyuw@linux.intel.com>
 *    Tina Zhang <tina.zhang@intel.com>
 *    Bing Niu <bing.niu@intel.com>
 *
 */

#ifndef __GVT_RENDER_H__
#define __GVT_RENDER_H__

#include <linux/types.h>

#include "gt/intel_engine_regs.h"
#include "gt/intel_engine_types.h"
#include "gt/intel_lrc_reg.h"

struct i915_request;
struct intel_context;
struct intel_engine_cs;
struct intel_gvt;
struct intel_vgpu;

struct engine_mmio {
	enum intel_engine_id id;
	i915_reg_t reg;
	u32 mask;
	bool in_context;
	u32 value;
};

void intel_gvt_switch_mmio(struct intel_vgpu *pre,
			   struct intel_vgpu *next,
			   const struct intel_engine_cs *engine);

void intel_gvt_init_engine_mmio_context(struct intel_gvt *gvt);

bool is_inhibit_context(struct intel_context *ce);

int intel_vgpu_restore_inhibit_context(struct intel_vgpu *vgpu,
				       struct i915_request *req);

#define IS_RESTORE_INHIBIT(a) \
	IS_MASKED_BITS_ENABLED(a, CTX_CTRL_ENGINE_CTX_RESTORE_INHIBIT)

#endif
