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

#include "i915_drv.h"
#include "gvt.h"

struct render_mmio {
	int ring_id;
	i915_reg_t reg;
	u32 mask;
	bool in_context;
	u32 value;
};

static struct render_mmio gen8_render_mmio_list[] = {
	{RCS, _MMIO(0x229c), 0xffff, false},
	{RCS, _MMIO(0x2248), 0x0, false},
	{RCS, _MMIO(0x2098), 0x0, false},
	{RCS, _MMIO(0x20c0), 0xffff, true},
	{RCS, _MMIO(0x24d0), 0, false},
	{RCS, _MMIO(0x24d4), 0, false},
	{RCS, _MMIO(0x24d8), 0, false},
	{RCS, _MMIO(0x24dc), 0, false},
	{RCS, _MMIO(0x7004), 0xffff, true},
	{RCS, _MMIO(0x7008), 0xffff, true},
	{RCS, _MMIO(0x7000), 0xffff, true},
	{RCS, _MMIO(0x7010), 0xffff, true},
	{RCS, _MMIO(0x7300), 0xffff, true},
	{RCS, _MMIO(0x83a4), 0xffff, true},

	{BCS, _MMIO(0x2229c), 0xffff, false},
	{BCS, _MMIO(0x2209c), 0xffff, false},
	{BCS, _MMIO(0x220c0), 0xffff, false},
	{BCS, _MMIO(0x22098), 0x0, false},
	{BCS, _MMIO(0x22028), 0x0, false},
};

static struct render_mmio gen9_render_mmio_list[] = {
	{RCS, _MMIO(0x229c), 0xffff, false},
	{RCS, _MMIO(0x2248), 0x0, false},
	{RCS, _MMIO(0x2098), 0x0, false},
	{RCS, _MMIO(0x20c0), 0xffff, true},
	{RCS, _MMIO(0x24d0), 0, false},
	{RCS, _MMIO(0x24d4), 0, false},
	{RCS, _MMIO(0x24d8), 0, false},
	{RCS, _MMIO(0x24dc), 0, false},
	{RCS, _MMIO(0x7004), 0xffff, true},
	{RCS, _MMIO(0x7008), 0xffff, true},
	{RCS, _MMIO(0x7000), 0xffff, true},
	{RCS, _MMIO(0x7010), 0xffff, true},
	{RCS, _MMIO(0x7300), 0xffff, true},
	{RCS, _MMIO(0x83a4), 0xffff, true},

	{RCS, _MMIO(0x40e0), 0, false},
	{RCS, _MMIO(0x40e4), 0, false},
	{RCS, _MMIO(0x2580), 0xffff, true},
	{RCS, _MMIO(0x7014), 0xffff, true},
	{RCS, _MMIO(0x20ec), 0xffff, false},
	{RCS, _MMIO(0xb118), 0, false},
	{RCS, _MMIO(0xe100), 0xffff, true},
	{RCS, _MMIO(0xe180), 0xffff, true},
	{RCS, _MMIO(0xe184), 0xffff, true},
	{RCS, _MMIO(0xe188), 0xffff, true},
	{RCS, _MMIO(0xe194), 0xffff, true},
	{RCS, _MMIO(0x4de0), 0, false},
	{RCS, _MMIO(0x4de4), 0, false},
	{RCS, _MMIO(0x4de8), 0, false},
	{RCS, _MMIO(0x4dec), 0, false},
	{RCS, _MMIO(0x4df0), 0, false},
	{RCS, _MMIO(0x4df4), 0, false},

	{BCS, _MMIO(0x2229c), 0xffff, false},
	{BCS, _MMIO(0x2209c), 0xffff, false},
	{BCS, _MMIO(0x220c0), 0xffff, false},
	{BCS, _MMIO(0x22098), 0x0, false},
	{BCS, _MMIO(0x22028), 0x0, false},

	{VCS2, _MMIO(0x1c028), 0xffff, false},

	{VECS, _MMIO(0x1a028), 0xffff, false},
};

static u32 gen9_render_mocs[I915_NUM_ENGINES][64];
static u32 gen9_render_mocs_L3[32];

static void handle_tlb_pending_event(struct intel_vgpu *vgpu, int ring_id)
{
	struct drm_i915_private *dev_priv = vgpu->gvt->dev_priv;
	i915_reg_t reg;
	u32 regs[] = {
		[RCS] = 0x4260,
		[VCS] = 0x4264,
		[VCS2] = 0x4268,
		[BCS] = 0x426c,
		[VECS] = 0x4270,
	};

	if (WARN_ON(ring_id >= ARRAY_SIZE(regs)))
		return;

	if (!test_and_clear_bit(ring_id, (void *)vgpu->tlb_handle_pending))
		return;

	reg = _MMIO(regs[ring_id]);

	I915_WRITE(reg, 0x1);

	if (wait_for_atomic((I915_READ(reg) == 0), 50))
		gvt_err("timeout in invalidate ring (%d) tlb\n", ring_id);

	gvt_dbg_core("invalidate TLB for ring %d\n", ring_id);
}

static void load_mocs(struct intel_vgpu *vgpu, int ring_id)
{
	struct drm_i915_private *dev_priv = vgpu->gvt->dev_priv;
	i915_reg_t offset, l3_offset;
	u32 regs[] = {
		[RCS] = 0xc800,
		[VCS] = 0xc900,
		[VCS2] = 0xca00,
		[BCS] = 0xcc00,
		[VECS] = 0xcb00,
	};
	int i;

	if (WARN_ON(ring_id >= ARRAY_SIZE(regs)))
		return;

	if (!IS_SKYLAKE(dev_priv))
		return;

	for (i = 0; i < 64; i++) {
		gen9_render_mocs[ring_id][i] = I915_READ(offset);
		I915_WRITE(offset, vgpu_vreg(vgpu, offset));
		POSTING_READ(offset);
		offset.reg += 4;
	}

	if (ring_id == RCS) {
		l3_offset.reg = 0xb020;
		for (i = 0; i < 32; i++) {
			gen9_render_mocs_L3[i] = I915_READ(l3_offset);
			I915_WRITE(l3_offset, vgpu_vreg(vgpu, offset));
			POSTING_READ(l3_offset);
			l3_offset.reg += 4;
		}
	}
}

static void restore_mocs(struct intel_vgpu *vgpu, int ring_id)
{
	struct drm_i915_private *dev_priv = vgpu->gvt->dev_priv;
	i915_reg_t offset, l3_offset;
	u32 regs[] = {
		[RCS] = 0xc800,
		[VCS] = 0xc900,
		[VCS2] = 0xca00,
		[BCS] = 0xcc00,
		[VECS] = 0xcb00,
	};
	int i;

	if (WARN_ON(ring_id >= ARRAY_SIZE(regs)))
		return;

	if (!IS_SKYLAKE(dev_priv))
		return;

	for (i = 0; i < 64; i++) {
		vgpu_vreg(vgpu, offset) = I915_READ(offset);
		I915_WRITE(offset, gen9_render_mocs[ring_id][i]);
		POSTING_READ(offset);
		offset.reg += 4;
	}

	if (ring_id == RCS) {
		l3_offset.reg = 0xb020;
		for (i = 0; i < 32; i++) {
			vgpu_vreg(vgpu, l3_offset) = I915_READ(l3_offset);
			I915_WRITE(l3_offset, gen9_render_mocs_L3[i]);
			POSTING_READ(l3_offset);
			l3_offset.reg += 4;
		}
	}
}

void intel_gvt_load_render_mmio(struct intel_vgpu *vgpu, int ring_id)
{
	struct drm_i915_private *dev_priv = vgpu->gvt->dev_priv;
	struct render_mmio *mmio;
	u32 v;
	int i, array_size;

	if (IS_SKYLAKE(vgpu->gvt->dev_priv)) {
		mmio = gen9_render_mmio_list;
		array_size = ARRAY_SIZE(gen9_render_mmio_list);
		load_mocs(vgpu, ring_id);
	} else {
		mmio = gen8_render_mmio_list;
		array_size = ARRAY_SIZE(gen8_render_mmio_list);
	}

	for (i = 0; i < array_size; i++, mmio++) {
		if (mmio->ring_id != ring_id)
			continue;

		mmio->value = I915_READ(mmio->reg);
		if (mmio->mask)
			v = vgpu_vreg(vgpu, mmio->reg) | (mmio->mask << 16);
		else
			v = vgpu_vreg(vgpu, mmio->reg);

		I915_WRITE(mmio->reg, v);
		POSTING_READ(mmio->reg);

		gvt_dbg_render("load reg %x old %x new %x\n",
				i915_mmio_reg_offset(mmio->reg),
				mmio->value, v);
	}
	handle_tlb_pending_event(vgpu, ring_id);
}

void intel_gvt_restore_render_mmio(struct intel_vgpu *vgpu, int ring_id)
{
	struct drm_i915_private *dev_priv = vgpu->gvt->dev_priv;
	struct render_mmio *mmio;
	u32 v;
	int i, array_size;

	if (IS_SKYLAKE(dev_priv)) {
		mmio = gen9_render_mmio_list;
		array_size = ARRAY_SIZE(gen9_render_mmio_list);
		restore_mocs(vgpu, ring_id);
	} else {
		mmio = gen8_render_mmio_list;
		array_size = ARRAY_SIZE(gen8_render_mmio_list);
	}

	for (i = 0; i < array_size; i++, mmio++) {
		if (mmio->ring_id != ring_id)
			continue;

		vgpu_vreg(vgpu, mmio->reg) = I915_READ(mmio->reg);

		if (mmio->mask) {
			vgpu_vreg(vgpu, mmio->reg) &= ~(mmio->mask << 16);
			v = mmio->value | (mmio->mask << 16);
		} else
			v = mmio->value;

		I915_WRITE(mmio->reg, v);
		POSTING_READ(mmio->reg);

		gvt_dbg_render("restore reg %x old %x new %x\n",
				i915_mmio_reg_offset(mmio->reg),
				mmio->value, v);
	}
}
