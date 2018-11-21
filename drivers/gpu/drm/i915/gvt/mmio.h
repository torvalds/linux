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

#define D_BDW   (1 << 0)
#define D_SKL	(1 << 1)
#define D_KBL	(1 << 2)

#define D_GEN9PLUS	(D_SKL | D_KBL)
#define D_GEN8PLUS	(D_BDW | D_SKL | D_KBL)

#define D_SKL_PLUS	(D_SKL | D_KBL)
#define D_BDW_PLUS	(D_BDW | D_SKL | D_KBL)

#define D_PRE_SKL	(D_BDW)
#define D_ALL		(D_BDW | D_SKL | D_KBL)

typedef int (*gvt_mmio_func)(struct intel_vgpu *, unsigned int, void *,
			     unsigned int);

struct intel_gvt_mmio_info {
	u32 offset;
	u64 ro_mask;
	u32 device;
	gvt_mmio_func read;
	gvt_mmio_func write;
	u32 addr_range;
	struct hlist_node node;
};

int intel_gvt_render_mmio_to_ring_id(struct intel_gvt *gvt,
		unsigned int reg);
unsigned long intel_gvt_get_device_type(struct intel_gvt *gvt);
bool intel_gvt_match_device(struct intel_gvt *gvt, unsigned long device);

int intel_gvt_setup_mmio_info(struct intel_gvt *gvt);
void intel_gvt_clean_mmio_info(struct intel_gvt *gvt);
int intel_gvt_for_each_tracked_mmio(struct intel_gvt *gvt,
	int (*handler)(struct intel_gvt *gvt, u32 offset, void *data),
	void *data);

int intel_vgpu_init_mmio(struct intel_vgpu *vgpu);
void intel_vgpu_reset_mmio(struct intel_vgpu *vgpu, bool dmlr);
void intel_vgpu_clean_mmio(struct intel_vgpu *vgpu);

int intel_vgpu_gpa_to_mmio_offset(struct intel_vgpu *vgpu, u64 gpa);

int intel_vgpu_emulate_mmio_read(struct intel_vgpu *vgpu, u64 pa,
				void *p_data, unsigned int bytes);
int intel_vgpu_emulate_mmio_write(struct intel_vgpu *vgpu, u64 pa,
				void *p_data, unsigned int bytes);

int intel_vgpu_default_mmio_read(struct intel_vgpu *vgpu, unsigned int offset,
				 void *p_data, unsigned int bytes);
int intel_vgpu_default_mmio_write(struct intel_vgpu *vgpu, unsigned int offset,
				  void *p_data, unsigned int bytes);

bool intel_gvt_in_force_nonpriv_whitelist(struct intel_gvt *gvt,
					  unsigned int offset);

int intel_vgpu_mmio_reg_rw(struct intel_vgpu *vgpu, unsigned int offset,
			   void *pdata, unsigned int bytes, bool is_read);

int intel_vgpu_mask_mmio_write(struct intel_vgpu *vgpu, unsigned int offset,
				  void *p_data, unsigned int bytes);
#endif
