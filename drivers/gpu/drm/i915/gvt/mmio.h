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
 *    Ke Yu
 *    Kevin Tian <kevin.tian@intel.com>
 *    Dexuan Cui
 *
 * Contributors:
 *    Tina Zhang <tina.zhang@intel.com>
 *    Min He <min.he@intel.com>
 *    Niu Bing <bing.niu@intel.com>
 *    Zhi Wang <zhi.a.wang@intel.com>
 *
 */

#ifndef _GVT_MMIO_H_
#define _GVT_MMIO_H_

struct intel_gvt;
struct intel_vgpu;

#define D_SNB   (1 << 0)
#define D_IVB   (1 << 1)
#define D_HSW   (1 << 2)
#define D_BDW   (1 << 3)
#define D_SKL	(1 << 4)

#define D_GEN9PLUS	(D_SKL)
#define D_GEN8PLUS	(D_BDW | D_SKL)
#define D_GEN75PLUS	(D_HSW | D_BDW | D_SKL)
#define D_GEN7PLUS	(D_IVB | D_HSW | D_BDW | D_SKL)

#define D_SKL_PLUS	(D_SKL)
#define D_BDW_PLUS	(D_BDW | D_SKL)
#define D_HSW_PLUS	(D_HSW | D_BDW | D_SKL)
#define D_IVB_PLUS	(D_IVB | D_HSW | D_BDW | D_SKL)

#define D_PRE_BDW	(D_SNB | D_IVB | D_HSW)
#define D_PRE_SKL	(D_SNB | D_IVB | D_HSW | D_BDW)
#define D_ALL		(D_SNB | D_IVB | D_HSW | D_BDW | D_SKL)

struct intel_gvt_mmio_info {
	u32 offset;
	u32 size;
	u32 length;
	u32 addr_mask;
	u64 ro_mask;
	u32 device;
	int (*read)(struct intel_vgpu *, unsigned int, void *, unsigned int);
	int (*write)(struct intel_vgpu *, unsigned int, void *, unsigned int);
	u32 addr_range;
	struct hlist_node node;
};

unsigned long intel_gvt_get_device_type(struct intel_gvt *gvt);
bool intel_gvt_match_device(struct intel_gvt *gvt, unsigned long device);

int intel_gvt_setup_mmio_info(struct intel_gvt *gvt);
void intel_gvt_clean_mmio_info(struct intel_gvt *gvt);

struct intel_gvt_mmio_info *intel_gvt_find_mmio_info(struct intel_gvt *gvt,
						     unsigned int offset);
#define INTEL_GVT_MMIO_OFFSET(reg) ({ \
	typeof(reg) __reg = reg; \
	u32 *offset = (u32 *)&__reg; \
	*offset; \
})

#endif
