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
 *    Kevin Tian <kevin.tian@intel.com>
 *    Eddie Dong <eddie.dong@intel.com>
 *    Zhiyuan Lv <zhiyuan.lv@intel.com>
 *
 * Contributors:
 *    Min He <min.he@intel.com>
 *    Tina Zhang <tina.zhang@intel.com>
 *    Pei Zhang <pei.zhang@intel.com>
 *    Niu Bing <bing.niu@intel.com>
 *    Ping Gao <ping.a.gao@intel.com>
 *    Zhi Wang <zhi.a.wang@intel.com>
 *

 */

#include "i915_drv.h"

/* XXX FIXME i915 has changed PP_XXX definition */
#define PCH_PP_STATUS  _MMIO(0xc7200)
#define PCH_PP_CONTROL _MMIO(0xc7204)
#define PCH_PP_ON_DELAYS _MMIO(0xc7208)
#define PCH_PP_OFF_DELAYS _MMIO(0xc720c)
#define PCH_PP_DIVISOR _MMIO(0xc7210)

/* Register contains RO bits */
#define F_RO		(1 << 0)
/* Register contains graphics address */
#define F_GMADR		(1 << 1)
/* Mode mask registers with high 16 bits as the mask bits */
#define F_MODE_MASK	(1 << 2)
/* This reg can be accessed by GPU commands */
#define F_CMD_ACCESS	(1 << 3)
/* This reg has been accessed by a VM */
#define F_ACCESSED	(1 << 4)
/* This reg has been accessed through GPU commands */
#define F_CMD_ACCESSED	(1 << 5)
/* This reg could be accessed by unaligned address */
#define F_UNALIGN	(1 << 6)

unsigned long intel_gvt_get_device_type(struct intel_gvt *gvt)
{
	if (IS_BROADWELL(gvt->dev_priv))
		return D_BDW;
	else if (IS_SKYLAKE(gvt->dev_priv))
		return D_SKL;

	return 0;
}

bool intel_gvt_match_device(struct intel_gvt *gvt,
		unsigned long device)
{
	return intel_gvt_get_device_type(gvt) & device;
}

static void read_vreg(struct intel_vgpu *vgpu, unsigned int offset,
	void *p_data, unsigned int bytes)
{
	memcpy(p_data, &vgpu_vreg(vgpu, offset), bytes);
}

static void write_vreg(struct intel_vgpu *vgpu, unsigned int offset,
	void *p_data, unsigned int bytes)
{
	memcpy(&vgpu_vreg(vgpu, offset), p_data, bytes);
}

static int new_mmio_info(struct intel_gvt *gvt,
		u32 offset, u32 flags, u32 size,
		u32 addr_mask, u32 ro_mask, u32 device,
		void *read, void *write)
{
	struct intel_gvt_mmio_info *info, *p;
	u32 start, end, i;

	if (!intel_gvt_match_device(gvt, device))
		return 0;

	if (WARN_ON(!IS_ALIGNED(offset, 4)))
		return -EINVAL;

	start = offset;
	end = offset + size;

	for (i = start; i < end; i += 4) {
		info = kzalloc(sizeof(*info), GFP_KERNEL);
		if (!info)
			return -ENOMEM;

		info->offset = i;
		p = intel_gvt_find_mmio_info(gvt, info->offset);
		if (p)
			gvt_err("dup mmio definition offset %x\n",
				info->offset);
		info->size = size;
		info->length = (i + 4) < end ? 4 : (end - i);
		info->addr_mask = addr_mask;
		info->device = device;
		info->read = read ? read : intel_vgpu_default_mmio_read;
		info->write = write ? write : intel_vgpu_default_mmio_write;
		gvt->mmio.mmio_attribute[info->offset / 4] = flags;
		INIT_HLIST_NODE(&info->node);
		hash_add(gvt->mmio.mmio_info_table, &info->node, info->offset);
	}
	return 0;
}

#define offset_to_fence_num(offset) \
	((offset - i915_mmio_reg_offset(FENCE_REG_GEN6_LO(0))) >> 3)

#define fence_num_to_offset(num) \
	(num * 8 + i915_mmio_reg_offset(FENCE_REG_GEN6_LO(0)))

static int sanitize_fence_mmio_access(struct intel_vgpu *vgpu,
		unsigned int fence_num, void *p_data, unsigned int bytes)
{
	if (fence_num >= vgpu_fence_sz(vgpu)) {
		gvt_err("vgpu%d: found oob fence register access\n",
				vgpu->id);
		gvt_err("vgpu%d: total fence num %d access fence num %d\n",
				vgpu->id, vgpu_fence_sz(vgpu), fence_num);
		memset(p_data, 0, bytes);
	}
	return 0;
}

static int fence_mmio_read(struct intel_vgpu *vgpu, unsigned int off,
		void *p_data, unsigned int bytes)
{
	int ret;

	ret = sanitize_fence_mmio_access(vgpu, offset_to_fence_num(off),
			p_data, bytes);
	if (ret)
		return ret;
	read_vreg(vgpu, off, p_data, bytes);
	return 0;
}

static int fence_mmio_write(struct intel_vgpu *vgpu, unsigned int off,
		void *p_data, unsigned int bytes)
{
	unsigned int fence_num = offset_to_fence_num(off);
	int ret;

	ret = sanitize_fence_mmio_access(vgpu, fence_num, p_data, bytes);
	if (ret)
		return ret;
	write_vreg(vgpu, off, p_data, bytes);

	intel_vgpu_write_fence(vgpu, fence_num,
			vgpu_vreg64(vgpu, fence_num_to_offset(fence_num)));
	return 0;
}

#define CALC_MODE_MASK_REG(old, new) \
	(((new) & GENMASK(31, 16)) \
	 | ((((old) & GENMASK(15, 0)) & ~((new) >> 16)) \
	 | ((new) & ((new) >> 16))))

static int mul_force_wake_write(struct intel_vgpu *vgpu,
		unsigned int offset, void *p_data, unsigned int bytes)
{
	u32 old, new;
	uint32_t ack_reg_offset;

	old = vgpu_vreg(vgpu, offset);
	new = CALC_MODE_MASK_REG(old, *(u32 *)p_data);

	if (IS_SKYLAKE(vgpu->gvt->dev_priv)) {
		switch (offset) {
		case FORCEWAKE_RENDER_GEN9_REG:
			ack_reg_offset = FORCEWAKE_ACK_RENDER_GEN9_REG;
			break;
		case FORCEWAKE_BLITTER_GEN9_REG:
			ack_reg_offset = FORCEWAKE_ACK_BLITTER_GEN9_REG;
			break;
		case FORCEWAKE_MEDIA_GEN9_REG:
			ack_reg_offset = FORCEWAKE_ACK_MEDIA_GEN9_REG;
			break;
		default:
			/*should not hit here*/
			gvt_err("invalid forcewake offset 0x%x\n", offset);
			return 1;
		}
	} else {
		ack_reg_offset = FORCEWAKE_ACK_HSW_REG;
	}

	vgpu_vreg(vgpu, offset) = new;
	vgpu_vreg(vgpu, ack_reg_offset) = (new & GENMASK(15, 0));
	return 0;
}

static int gdrst_mmio_write(struct intel_vgpu *vgpu, unsigned int offset,
		void *p_data, unsigned int bytes)
{
	u32 data;
	u32 bitmap = 0;

	data = vgpu_vreg(vgpu, offset);

	if (data & GEN6_GRDOM_FULL) {
		gvt_dbg_mmio("vgpu%d: request full GPU reset\n", vgpu->id);
		bitmap = 0xff;
	}
	if (data & GEN6_GRDOM_RENDER) {
		gvt_dbg_mmio("vgpu%d: request RCS reset\n", vgpu->id);
		bitmap |= (1 << RCS);
	}
	if (data & GEN6_GRDOM_MEDIA) {
		gvt_dbg_mmio("vgpu%d: request VCS reset\n", vgpu->id);
		bitmap |= (1 << VCS);
	}
	if (data & GEN6_GRDOM_BLT) {
		gvt_dbg_mmio("vgpu%d: request BCS Reset\n", vgpu->id);
		bitmap |= (1 << BCS);
	}
	if (data & GEN6_GRDOM_VECS) {
		gvt_dbg_mmio("vgpu%d: request VECS Reset\n", vgpu->id);
		bitmap |= (1 << VECS);
	}
	if (data & GEN8_GRDOM_MEDIA2) {
		gvt_dbg_mmio("vgpu%d: request VCS2 Reset\n", vgpu->id);
		if (HAS_BSD2(vgpu->gvt->dev_priv))
			bitmap |= (1 << VCS2);
	}
	return 0;
}

#define _vgtif_reg(x) \
	(VGT_PVINFO_PAGE + offsetof(struct vgt_if, x))

static int pvinfo_mmio_read(struct intel_vgpu *vgpu, unsigned int offset,
		void *p_data, unsigned int bytes)
{
	bool invalid_read = false;

	read_vreg(vgpu, offset, p_data, bytes);

	switch (offset) {
	case _vgtif_reg(magic) ... _vgtif_reg(vgt_id):
		if (offset + bytes > _vgtif_reg(vgt_id) + 4)
			invalid_read = true;
		break;
	case _vgtif_reg(avail_rs.mappable_gmadr.base) ...
		_vgtif_reg(avail_rs.fence_num):
		if (offset + bytes >
			_vgtif_reg(avail_rs.fence_num) + 4)
			invalid_read = true;
		break;
	case 0x78010:	/* vgt_caps */
	case 0x7881c:
		break;
	default:
		invalid_read = true;
		break;
	}
	if (invalid_read)
		gvt_err("invalid pvinfo read: [%x:%x] = %x\n",
				offset, bytes, *(u32 *)p_data);
	return 0;
}

static int handle_g2v_notification(struct intel_vgpu *vgpu, int notification)
{
	int ret = 0;

	switch (notification) {
	case VGT_G2V_PPGTT_L3_PAGE_TABLE_CREATE:
		ret = intel_vgpu_g2v_create_ppgtt_mm(vgpu, 3);
		break;
	case VGT_G2V_PPGTT_L3_PAGE_TABLE_DESTROY:
		ret = intel_vgpu_g2v_destroy_ppgtt_mm(vgpu, 3);
		break;
	case VGT_G2V_PPGTT_L4_PAGE_TABLE_CREATE:
		ret = intel_vgpu_g2v_create_ppgtt_mm(vgpu, 4);
		break;
	case VGT_G2V_PPGTT_L4_PAGE_TABLE_DESTROY:
		ret = intel_vgpu_g2v_destroy_ppgtt_mm(vgpu, 4);
		break;
	case VGT_G2V_EXECLIST_CONTEXT_CREATE:
	case VGT_G2V_EXECLIST_CONTEXT_DESTROY:
	case 1:	/* Remove this in guest driver. */
		break;
	default:
		gvt_err("Invalid PV notification %d\n", notification);
	}
	return ret;
}

static int pvinfo_mmio_write(struct intel_vgpu *vgpu, unsigned int offset,
		void *p_data, unsigned int bytes)
{
	u32 data;
	int ret;

	write_vreg(vgpu, offset, p_data, bytes);
	data = vgpu_vreg(vgpu, offset);

	switch (offset) {
	case _vgtif_reg(display_ready):
	case _vgtif_reg(g2v_notify):
		ret = handle_g2v_notification(vgpu, data);
		break;
	/* add xhot and yhot to handled list to avoid error log */
	case 0x78830:
	case 0x78834:
	case _vgtif_reg(pdp[0].lo):
	case _vgtif_reg(pdp[0].hi):
	case _vgtif_reg(pdp[1].lo):
	case _vgtif_reg(pdp[1].hi):
	case _vgtif_reg(pdp[2].lo):
	case _vgtif_reg(pdp[2].hi):
	case _vgtif_reg(pdp[3].lo):
	case _vgtif_reg(pdp[3].hi):
	case _vgtif_reg(execlist_context_descriptor_lo):
	case _vgtif_reg(execlist_context_descriptor_hi):
		break;
	default:
		gvt_err("invalid pvinfo write offset %x bytes %x data %x\n",
				offset, bytes, data);
		break;
	}
	return 0;
}

static int fpga_dbg_mmio_write(struct intel_vgpu *vgpu,
	unsigned int offset, void *p_data, unsigned int bytes)
{
	write_vreg(vgpu, offset, p_data, bytes);

	if (vgpu_vreg(vgpu, offset) & FPGA_DBG_RM_NOCLAIM)
		vgpu_vreg(vgpu, offset) &= ~FPGA_DBG_RM_NOCLAIM;
	return 0;
}

static int dma_ctrl_write(struct intel_vgpu *vgpu, unsigned int offset,
		void *p_data, unsigned int bytes)
{
	u32 mode = *(u32 *)p_data;

	if (GFX_MODE_BIT_SET_IN_MASK(mode, START_DMA)) {
		WARN_ONCE(1, "VM(%d): iGVT-g doesn't supporte GuC\n",
				vgpu->id);
		return 0;
	}

	return 0;
}

static int gen9_trtte_write(struct intel_vgpu *vgpu, unsigned int offset,
		void *p_data, unsigned int bytes)
{
	struct drm_i915_private *dev_priv = vgpu->gvt->dev_priv;
	u32 trtte = *(u32 *)p_data;

	if ((trtte & 1) && (trtte & (1 << 1)) == 0) {
		WARN(1, "VM(%d): Use physical address for TRTT!\n",
				vgpu->id);
		return -EINVAL;
	}
	write_vreg(vgpu, offset, p_data, bytes);
	/* TRTTE is not per-context */
	I915_WRITE(_MMIO(offset), vgpu_vreg(vgpu, offset));

	return 0;
}

static int gen9_trtt_chicken_write(struct intel_vgpu *vgpu, unsigned int offset,
		void *p_data, unsigned int bytes)
{
	struct drm_i915_private *dev_priv = vgpu->gvt->dev_priv;
	u32 val = *(u32 *)p_data;

	if (val & 1) {
		/* unblock hw logic */
		I915_WRITE(_MMIO(offset), val);
	}
	write_vreg(vgpu, offset, p_data, bytes);
	return 0;
}

#define MMIO_F(reg, s, f, am, rm, d, r, w) do { \
	ret = new_mmio_info(gvt, INTEL_GVT_MMIO_OFFSET(reg), \
		f, s, am, rm, d, r, w); \
	if (ret) \
		return ret; \
} while (0)

#define MMIO_D(reg, d) \
	MMIO_F(reg, 4, 0, 0, 0, d, NULL, NULL)

#define MMIO_DH(reg, d, r, w) \
	MMIO_F(reg, 4, 0, 0, 0, d, r, w)

#define MMIO_DFH(reg, d, f, r, w) \
	MMIO_F(reg, 4, f, 0, 0, d, r, w)

#define MMIO_GM(reg, d, r, w) \
	MMIO_F(reg, 4, F_GMADR, 0xFFFFF000, 0, d, r, w)

#define MMIO_RO(reg, d, f, rm, r, w) \
	MMIO_F(reg, 4, F_RO | f, 0, rm, d, r, w)

#define MMIO_RING_F(prefix, s, f, am, rm, d, r, w) do { \
	MMIO_F(prefix(RENDER_RING_BASE), s, f, am, rm, d, r, w); \
	MMIO_F(prefix(BLT_RING_BASE), s, f, am, rm, d, r, w); \
	MMIO_F(prefix(GEN6_BSD_RING_BASE), s, f, am, rm, d, r, w); \
	MMIO_F(prefix(VEBOX_RING_BASE), s, f, am, rm, d, r, w); \
} while (0)

#define MMIO_RING_D(prefix, d) \
	MMIO_RING_F(prefix, 4, 0, 0, 0, d, NULL, NULL)

#define MMIO_RING_DFH(prefix, d, f, r, w) \
	MMIO_RING_F(prefix, 4, f, 0, 0, d, r, w)

#define MMIO_RING_GM(prefix, d, r, w) \
	MMIO_RING_F(prefix, 4, F_GMADR, 0xFFFF0000, 0, d, r, w)

#define MMIO_RING_RO(prefix, d, f, rm, r, w) \
	MMIO_RING_F(prefix, 4, F_RO | f, 0, rm, d, r, w)

static int init_generic_mmio_info(struct intel_gvt *gvt)
{
	struct drm_i915_private *dev_priv = gvt->dev_priv;
	int ret;

	MMIO_RING_DFH(RING_IMR, D_ALL, 0, NULL, intel_vgpu_reg_imr_handler);

	MMIO_DFH(SDEIMR, D_ALL, 0, NULL, intel_vgpu_reg_imr_handler);
	MMIO_DFH(SDEIER, D_ALL, 0, NULL, intel_vgpu_reg_ier_handler);
	MMIO_DFH(SDEIIR, D_ALL, 0, NULL, intel_vgpu_reg_iir_handler);
	MMIO_D(SDEISR, D_ALL);

	MMIO_RING_D(RING_HWSTAM, D_ALL);

	MMIO_GM(RENDER_HWS_PGA_GEN7, D_ALL, NULL, NULL);
	MMIO_GM(BSD_HWS_PGA_GEN7, D_ALL, NULL, NULL);
	MMIO_GM(BLT_HWS_PGA_GEN7, D_ALL, NULL, NULL);
	MMIO_GM(VEBOX_HWS_PGA_GEN7, D_ALL, NULL, NULL);

#define RING_REG(base) (base + 0x28)
	MMIO_RING_D(RING_REG, D_ALL);
#undef RING_REG

#define RING_REG(base) (base + 0x134)
	MMIO_RING_D(RING_REG, D_ALL);
#undef RING_REG

	MMIO_GM(0x2148, D_ALL, NULL, NULL);
	MMIO_GM(CCID, D_ALL, NULL, NULL);
	MMIO_GM(0x12198, D_ALL, NULL, NULL);
	MMIO_D(GEN7_CXT_SIZE, D_ALL);

	MMIO_RING_D(RING_TAIL, D_ALL);
	MMIO_RING_D(RING_HEAD, D_ALL);
	MMIO_RING_D(RING_CTL, D_ALL);
	MMIO_RING_D(RING_ACTHD, D_ALL);
	MMIO_RING_GM(RING_START, D_ALL, NULL, NULL);

	/* RING MODE */
#define RING_REG(base) (base + 0x29c)
	MMIO_RING_DFH(RING_REG, D_ALL, F_MODE_MASK, NULL, NULL);
#undef RING_REG

	MMIO_RING_DFH(RING_MI_MODE, D_ALL, F_MODE_MASK, NULL, NULL);
	MMIO_RING_DFH(RING_INSTPM, D_ALL, F_MODE_MASK, NULL, NULL);
	MMIO_RING_DFH(RING_TIMESTAMP, D_ALL, F_CMD_ACCESS, NULL, NULL);
	MMIO_RING_DFH(RING_TIMESTAMP_UDW, D_ALL, F_CMD_ACCESS, NULL, NULL);

	MMIO_DFH(GEN7_GT_MODE, D_ALL, F_MODE_MASK, NULL, NULL);
	MMIO_DFH(CACHE_MODE_0_GEN7, D_ALL, F_MODE_MASK, NULL, NULL);
	MMIO_DFH(CACHE_MODE_1, D_ALL, F_MODE_MASK, NULL, NULL);

	MMIO_DFH(0x20dc, D_ALL, F_MODE_MASK, NULL, NULL);
	MMIO_DFH(_3D_CHICKEN3, D_ALL, F_MODE_MASK, NULL, NULL);
	MMIO_DFH(0x2088, D_ALL, F_MODE_MASK, NULL, NULL);
	MMIO_DFH(0x20e4, D_ALL, F_MODE_MASK, NULL, NULL);
	MMIO_DFH(0x2470, D_ALL, F_MODE_MASK, NULL, NULL);
	MMIO_D(GAM_ECOCHK, D_ALL);
	MMIO_DFH(GEN7_COMMON_SLICE_CHICKEN1, D_ALL, F_MODE_MASK, NULL, NULL);
	MMIO_DFH(COMMON_SLICE_CHICKEN2, D_ALL, F_MODE_MASK, NULL, NULL);
	MMIO_D(0x9030, D_ALL);
	MMIO_D(0x20a0, D_ALL);
	MMIO_D(0x2420, D_ALL);
	MMIO_D(0x2430, D_ALL);
	MMIO_D(0x2434, D_ALL);
	MMIO_D(0x2438, D_ALL);
	MMIO_D(0x243c, D_ALL);
	MMIO_DFH(0x7018, D_ALL, F_MODE_MASK, NULL, NULL);
	MMIO_DFH(0xe184, D_ALL, F_MODE_MASK, NULL, NULL);
	MMIO_DFH(0xe100, D_ALL, F_MODE_MASK, NULL, NULL);

	/* display */
	MMIO_F(0x60220, 0x20, 0, 0, 0, D_ALL, NULL, NULL);
	MMIO_D(0x602a0, D_ALL);

	MMIO_D(0x65050, D_ALL);
	MMIO_D(0x650b4, D_ALL);

	MMIO_D(0xc4040, D_ALL);
	MMIO_D(DERRMR, D_ALL);

	MMIO_D(PIPEDSL(PIPE_A), D_ALL);
	MMIO_D(PIPEDSL(PIPE_B), D_ALL);
	MMIO_D(PIPEDSL(PIPE_C), D_ALL);
	MMIO_D(PIPEDSL(_PIPE_EDP), D_ALL);

	MMIO_DH(PIPECONF(PIPE_A), D_ALL, NULL, NULL);
	MMIO_DH(PIPECONF(PIPE_B), D_ALL, NULL, NULL);
	MMIO_DH(PIPECONF(PIPE_C), D_ALL, NULL, NULL);
	MMIO_DH(PIPECONF(_PIPE_EDP), D_ALL, NULL, NULL);

	MMIO_D(PIPESTAT(PIPE_A), D_ALL);
	MMIO_D(PIPESTAT(PIPE_B), D_ALL);
	MMIO_D(PIPESTAT(PIPE_C), D_ALL);
	MMIO_D(PIPESTAT(_PIPE_EDP), D_ALL);

	MMIO_D(PIPE_FLIPCOUNT_G4X(PIPE_A), D_ALL);
	MMIO_D(PIPE_FLIPCOUNT_G4X(PIPE_B), D_ALL);
	MMIO_D(PIPE_FLIPCOUNT_G4X(PIPE_C), D_ALL);
	MMIO_D(PIPE_FLIPCOUNT_G4X(_PIPE_EDP), D_ALL);

	MMIO_D(PIPE_FRMCOUNT_G4X(PIPE_A), D_ALL);
	MMIO_D(PIPE_FRMCOUNT_G4X(PIPE_B), D_ALL);
	MMIO_D(PIPE_FRMCOUNT_G4X(PIPE_C), D_ALL);
	MMIO_D(PIPE_FRMCOUNT_G4X(_PIPE_EDP), D_ALL);

	MMIO_D(CURCNTR(PIPE_A), D_ALL);
	MMIO_D(CURCNTR(PIPE_B), D_ALL);
	MMIO_D(CURCNTR(PIPE_C), D_ALL);

	MMIO_D(CURPOS(PIPE_A), D_ALL);
	MMIO_D(CURPOS(PIPE_B), D_ALL);
	MMIO_D(CURPOS(PIPE_C), D_ALL);

	MMIO_D(CURBASE(PIPE_A), D_ALL);
	MMIO_D(CURBASE(PIPE_B), D_ALL);
	MMIO_D(CURBASE(PIPE_C), D_ALL);

	MMIO_D(0x700ac, D_ALL);
	MMIO_D(0x710ac, D_ALL);
	MMIO_D(0x720ac, D_ALL);

	MMIO_D(0x70090, D_ALL);
	MMIO_D(0x70094, D_ALL);
	MMIO_D(0x70098, D_ALL);
	MMIO_D(0x7009c, D_ALL);

	MMIO_D(DSPCNTR(PIPE_A), D_ALL);
	MMIO_D(DSPADDR(PIPE_A), D_ALL);
	MMIO_D(DSPSTRIDE(PIPE_A), D_ALL);
	MMIO_D(DSPPOS(PIPE_A), D_ALL);
	MMIO_D(DSPSIZE(PIPE_A), D_ALL);
	MMIO_D(DSPSURF(PIPE_A), D_ALL);
	MMIO_D(DSPOFFSET(PIPE_A), D_ALL);
	MMIO_D(DSPSURFLIVE(PIPE_A), D_ALL);

	MMIO_D(DSPCNTR(PIPE_B), D_ALL);
	MMIO_D(DSPADDR(PIPE_B), D_ALL);
	MMIO_D(DSPSTRIDE(PIPE_B), D_ALL);
	MMIO_D(DSPPOS(PIPE_B), D_ALL);
	MMIO_D(DSPSIZE(PIPE_B), D_ALL);
	MMIO_D(DSPSURF(PIPE_B), D_ALL);
	MMIO_D(DSPOFFSET(PIPE_B), D_ALL);
	MMIO_D(DSPSURFLIVE(PIPE_B), D_ALL);

	MMIO_D(DSPCNTR(PIPE_C), D_ALL);
	MMIO_D(DSPADDR(PIPE_C), D_ALL);
	MMIO_D(DSPSTRIDE(PIPE_C), D_ALL);
	MMIO_D(DSPPOS(PIPE_C), D_ALL);
	MMIO_D(DSPSIZE(PIPE_C), D_ALL);
	MMIO_D(DSPSURF(PIPE_C), D_ALL);
	MMIO_D(DSPOFFSET(PIPE_C), D_ALL);
	MMIO_D(DSPSURFLIVE(PIPE_C), D_ALL);

	MMIO_D(SPRCTL(PIPE_A), D_ALL);
	MMIO_D(SPRLINOFF(PIPE_A), D_ALL);
	MMIO_D(SPRSTRIDE(PIPE_A), D_ALL);
	MMIO_D(SPRPOS(PIPE_A), D_ALL);
	MMIO_D(SPRSIZE(PIPE_A), D_ALL);
	MMIO_D(SPRKEYVAL(PIPE_A), D_ALL);
	MMIO_D(SPRKEYMSK(PIPE_A), D_ALL);
	MMIO_D(SPRSURF(PIPE_A), D_ALL);
	MMIO_D(SPRKEYMAX(PIPE_A), D_ALL);
	MMIO_D(SPROFFSET(PIPE_A), D_ALL);
	MMIO_D(SPRSCALE(PIPE_A), D_ALL);
	MMIO_D(SPRSURFLIVE(PIPE_A), D_ALL);

	MMIO_D(SPRCTL(PIPE_B), D_ALL);
	MMIO_D(SPRLINOFF(PIPE_B), D_ALL);
	MMIO_D(SPRSTRIDE(PIPE_B), D_ALL);
	MMIO_D(SPRPOS(PIPE_B), D_ALL);
	MMIO_D(SPRSIZE(PIPE_B), D_ALL);
	MMIO_D(SPRKEYVAL(PIPE_B), D_ALL);
	MMIO_D(SPRKEYMSK(PIPE_B), D_ALL);
	MMIO_D(SPRSURF(PIPE_B), D_ALL);
	MMIO_D(SPRKEYMAX(PIPE_B), D_ALL);
	MMIO_D(SPROFFSET(PIPE_B), D_ALL);
	MMIO_D(SPRSCALE(PIPE_B), D_ALL);
	MMIO_D(SPRSURFLIVE(PIPE_B), D_ALL);

	MMIO_D(SPRCTL(PIPE_C), D_ALL);
	MMIO_D(SPRLINOFF(PIPE_C), D_ALL);
	MMIO_D(SPRSTRIDE(PIPE_C), D_ALL);
	MMIO_D(SPRPOS(PIPE_C), D_ALL);
	MMIO_D(SPRSIZE(PIPE_C), D_ALL);
	MMIO_D(SPRKEYVAL(PIPE_C), D_ALL);
	MMIO_D(SPRKEYMSK(PIPE_C), D_ALL);
	MMIO_D(SPRSURF(PIPE_C), D_ALL);
	MMIO_D(SPRKEYMAX(PIPE_C), D_ALL);
	MMIO_D(SPROFFSET(PIPE_C), D_ALL);
	MMIO_D(SPRSCALE(PIPE_C), D_ALL);
	MMIO_D(SPRSURFLIVE(PIPE_C), D_ALL);

	MMIO_F(LGC_PALETTE(PIPE_A, 0), 4 * 256, 0, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(LGC_PALETTE(PIPE_B, 0), 4 * 256, 0, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(LGC_PALETTE(PIPE_C, 0), 4 * 256, 0, 0, 0, D_ALL, NULL, NULL);

	MMIO_D(HTOTAL(TRANSCODER_A), D_ALL);
	MMIO_D(HBLANK(TRANSCODER_A), D_ALL);
	MMIO_D(HSYNC(TRANSCODER_A), D_ALL);
	MMIO_D(VTOTAL(TRANSCODER_A), D_ALL);
	MMIO_D(VBLANK(TRANSCODER_A), D_ALL);
	MMIO_D(VSYNC(TRANSCODER_A), D_ALL);
	MMIO_D(BCLRPAT(TRANSCODER_A), D_ALL);
	MMIO_D(VSYNCSHIFT(TRANSCODER_A), D_ALL);
	MMIO_D(PIPESRC(TRANSCODER_A), D_ALL);

	MMIO_D(HTOTAL(TRANSCODER_B), D_ALL);
	MMIO_D(HBLANK(TRANSCODER_B), D_ALL);
	MMIO_D(HSYNC(TRANSCODER_B), D_ALL);
	MMIO_D(VTOTAL(TRANSCODER_B), D_ALL);
	MMIO_D(VBLANK(TRANSCODER_B), D_ALL);
	MMIO_D(VSYNC(TRANSCODER_B), D_ALL);
	MMIO_D(BCLRPAT(TRANSCODER_B), D_ALL);
	MMIO_D(VSYNCSHIFT(TRANSCODER_B), D_ALL);
	MMIO_D(PIPESRC(TRANSCODER_B), D_ALL);

	MMIO_D(HTOTAL(TRANSCODER_C), D_ALL);
	MMIO_D(HBLANK(TRANSCODER_C), D_ALL);
	MMIO_D(HSYNC(TRANSCODER_C), D_ALL);
	MMIO_D(VTOTAL(TRANSCODER_C), D_ALL);
	MMIO_D(VBLANK(TRANSCODER_C), D_ALL);
	MMIO_D(VSYNC(TRANSCODER_C), D_ALL);
	MMIO_D(BCLRPAT(TRANSCODER_C), D_ALL);
	MMIO_D(VSYNCSHIFT(TRANSCODER_C), D_ALL);
	MMIO_D(PIPESRC(TRANSCODER_C), D_ALL);

	MMIO_D(HTOTAL(TRANSCODER_EDP), D_ALL);
	MMIO_D(HBLANK(TRANSCODER_EDP), D_ALL);
	MMIO_D(HSYNC(TRANSCODER_EDP), D_ALL);
	MMIO_D(VTOTAL(TRANSCODER_EDP), D_ALL);
	MMIO_D(VBLANK(TRANSCODER_EDP), D_ALL);
	MMIO_D(VSYNC(TRANSCODER_EDP), D_ALL);
	MMIO_D(BCLRPAT(TRANSCODER_EDP), D_ALL);
	MMIO_D(VSYNCSHIFT(TRANSCODER_EDP), D_ALL);

	MMIO_D(PIPE_DATA_M1(TRANSCODER_A), D_ALL);
	MMIO_D(PIPE_DATA_N1(TRANSCODER_A), D_ALL);
	MMIO_D(PIPE_DATA_M2(TRANSCODER_A), D_ALL);
	MMIO_D(PIPE_DATA_N2(TRANSCODER_A), D_ALL);
	MMIO_D(PIPE_LINK_M1(TRANSCODER_A), D_ALL);
	MMIO_D(PIPE_LINK_N1(TRANSCODER_A), D_ALL);
	MMIO_D(PIPE_LINK_M2(TRANSCODER_A), D_ALL);
	MMIO_D(PIPE_LINK_N2(TRANSCODER_A), D_ALL);

	MMIO_D(PIPE_DATA_M1(TRANSCODER_B), D_ALL);
	MMIO_D(PIPE_DATA_N1(TRANSCODER_B), D_ALL);
	MMIO_D(PIPE_DATA_M2(TRANSCODER_B), D_ALL);
	MMIO_D(PIPE_DATA_N2(TRANSCODER_B), D_ALL);
	MMIO_D(PIPE_LINK_M1(TRANSCODER_B), D_ALL);
	MMIO_D(PIPE_LINK_N1(TRANSCODER_B), D_ALL);
	MMIO_D(PIPE_LINK_M2(TRANSCODER_B), D_ALL);
	MMIO_D(PIPE_LINK_N2(TRANSCODER_B), D_ALL);

	MMIO_D(PIPE_DATA_M1(TRANSCODER_C), D_ALL);
	MMIO_D(PIPE_DATA_N1(TRANSCODER_C), D_ALL);
	MMIO_D(PIPE_DATA_M2(TRANSCODER_C), D_ALL);
	MMIO_D(PIPE_DATA_N2(TRANSCODER_C), D_ALL);
	MMIO_D(PIPE_LINK_M1(TRANSCODER_C), D_ALL);
	MMIO_D(PIPE_LINK_N1(TRANSCODER_C), D_ALL);
	MMIO_D(PIPE_LINK_M2(TRANSCODER_C), D_ALL);
	MMIO_D(PIPE_LINK_N2(TRANSCODER_C), D_ALL);

	MMIO_D(PIPE_DATA_M1(TRANSCODER_EDP), D_ALL);
	MMIO_D(PIPE_DATA_N1(TRANSCODER_EDP), D_ALL);
	MMIO_D(PIPE_DATA_M2(TRANSCODER_EDP), D_ALL);
	MMIO_D(PIPE_DATA_N2(TRANSCODER_EDP), D_ALL);
	MMIO_D(PIPE_LINK_M1(TRANSCODER_EDP), D_ALL);
	MMIO_D(PIPE_LINK_N1(TRANSCODER_EDP), D_ALL);
	MMIO_D(PIPE_LINK_M2(TRANSCODER_EDP), D_ALL);
	MMIO_D(PIPE_LINK_N2(TRANSCODER_EDP), D_ALL);

	MMIO_D(PF_CTL(PIPE_A), D_ALL);
	MMIO_D(PF_WIN_SZ(PIPE_A), D_ALL);
	MMIO_D(PF_WIN_POS(PIPE_A), D_ALL);
	MMIO_D(PF_VSCALE(PIPE_A), D_ALL);
	MMIO_D(PF_HSCALE(PIPE_A), D_ALL);

	MMIO_D(PF_CTL(PIPE_B), D_ALL);
	MMIO_D(PF_WIN_SZ(PIPE_B), D_ALL);
	MMIO_D(PF_WIN_POS(PIPE_B), D_ALL);
	MMIO_D(PF_VSCALE(PIPE_B), D_ALL);
	MMIO_D(PF_HSCALE(PIPE_B), D_ALL);

	MMIO_D(PF_CTL(PIPE_C), D_ALL);
	MMIO_D(PF_WIN_SZ(PIPE_C), D_ALL);
	MMIO_D(PF_WIN_POS(PIPE_C), D_ALL);
	MMIO_D(PF_VSCALE(PIPE_C), D_ALL);
	MMIO_D(PF_HSCALE(PIPE_C), D_ALL);

	MMIO_D(WM0_PIPEA_ILK, D_ALL);
	MMIO_D(WM0_PIPEB_ILK, D_ALL);
	MMIO_D(WM0_PIPEC_IVB, D_ALL);
	MMIO_D(WM1_LP_ILK, D_ALL);
	MMIO_D(WM2_LP_ILK, D_ALL);
	MMIO_D(WM3_LP_ILK, D_ALL);
	MMIO_D(WM1S_LP_ILK, D_ALL);
	MMIO_D(WM2S_LP_IVB, D_ALL);
	MMIO_D(WM3S_LP_IVB, D_ALL);

	MMIO_D(BLC_PWM_CPU_CTL2, D_ALL);
	MMIO_D(BLC_PWM_CPU_CTL, D_ALL);
	MMIO_D(BLC_PWM_PCH_CTL1, D_ALL);
	MMIO_D(BLC_PWM_PCH_CTL2, D_ALL);

	MMIO_D(0x48268, D_ALL);

	MMIO_F(PCH_GMBUS0, 4 * 4, 0, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(PCH_GPIOA, 6 * 4, 0, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(0xe4f00, 0x28, 0, 0, 0, D_ALL, NULL, NULL);

	MMIO_F(_PCH_DPB_AUX_CH_CTL, 6 * 4, 0, 0, 0, D_PRE_SKL, NULL, NULL);
	MMIO_F(_PCH_DPC_AUX_CH_CTL, 6 * 4, 0, 0, 0, D_PRE_SKL, NULL, NULL);
	MMIO_F(_PCH_DPD_AUX_CH_CTL, 6 * 4, 0, 0, 0, D_PRE_SKL, NULL, NULL);

	MMIO_RO(PCH_ADPA, D_ALL, 0,
			ADPA_CRT_HOTPLUG_MONITOR_MASK, NULL, NULL);

	MMIO_DH(_PCH_TRANSACONF, D_ALL, NULL, NULL);
	MMIO_DH(_PCH_TRANSBCONF, D_ALL, NULL, NULL);

	MMIO_DH(FDI_RX_IIR(PIPE_A), D_ALL, NULL, NULL);
	MMIO_DH(FDI_RX_IIR(PIPE_B), D_ALL, NULL, NULL);
	MMIO_DH(FDI_RX_IIR(PIPE_C), D_ALL, NULL, NULL);
	MMIO_DH(FDI_RX_IMR(PIPE_A), D_ALL, NULL, NULL);
	MMIO_DH(FDI_RX_IMR(PIPE_B), D_ALL, NULL, NULL);
	MMIO_DH(FDI_RX_IMR(PIPE_C), D_ALL, NULL, NULL);
	MMIO_DH(FDI_RX_CTL(PIPE_A), D_ALL, NULL, NULL);
	MMIO_DH(FDI_RX_CTL(PIPE_B), D_ALL, NULL, NULL);
	MMIO_DH(FDI_RX_CTL(PIPE_C), D_ALL, NULL, NULL);

	MMIO_D(_PCH_TRANS_HTOTAL_A, D_ALL);
	MMIO_D(_PCH_TRANS_HBLANK_A, D_ALL);
	MMIO_D(_PCH_TRANS_HSYNC_A, D_ALL);
	MMIO_D(_PCH_TRANS_VTOTAL_A, D_ALL);
	MMIO_D(_PCH_TRANS_VBLANK_A, D_ALL);
	MMIO_D(_PCH_TRANS_VSYNC_A, D_ALL);
	MMIO_D(_PCH_TRANS_VSYNCSHIFT_A, D_ALL);

	MMIO_D(_PCH_TRANS_HTOTAL_B, D_ALL);
	MMIO_D(_PCH_TRANS_HBLANK_B, D_ALL);
	MMIO_D(_PCH_TRANS_HSYNC_B, D_ALL);
	MMIO_D(_PCH_TRANS_VTOTAL_B, D_ALL);
	MMIO_D(_PCH_TRANS_VBLANK_B, D_ALL);
	MMIO_D(_PCH_TRANS_VSYNC_B, D_ALL);
	MMIO_D(_PCH_TRANS_VSYNCSHIFT_B, D_ALL);

	MMIO_D(_PCH_TRANSA_DATA_M1, D_ALL);
	MMIO_D(_PCH_TRANSA_DATA_N1, D_ALL);
	MMIO_D(_PCH_TRANSA_DATA_M2, D_ALL);
	MMIO_D(_PCH_TRANSA_DATA_N2, D_ALL);
	MMIO_D(_PCH_TRANSA_LINK_M1, D_ALL);
	MMIO_D(_PCH_TRANSA_LINK_N1, D_ALL);
	MMIO_D(_PCH_TRANSA_LINK_M2, D_ALL);
	MMIO_D(_PCH_TRANSA_LINK_N2, D_ALL);

	MMIO_D(TRANS_DP_CTL(PIPE_A), D_ALL);
	MMIO_D(TRANS_DP_CTL(PIPE_B), D_ALL);
	MMIO_D(TRANS_DP_CTL(PIPE_C), D_ALL);

	MMIO_D(TVIDEO_DIP_CTL(PIPE_A), D_ALL);
	MMIO_D(TVIDEO_DIP_DATA(PIPE_A), D_ALL);
	MMIO_D(TVIDEO_DIP_GCP(PIPE_A), D_ALL);

	MMIO_D(TVIDEO_DIP_CTL(PIPE_B), D_ALL);
	MMIO_D(TVIDEO_DIP_DATA(PIPE_B), D_ALL);
	MMIO_D(TVIDEO_DIP_GCP(PIPE_B), D_ALL);

	MMIO_D(TVIDEO_DIP_CTL(PIPE_C), D_ALL);
	MMIO_D(TVIDEO_DIP_DATA(PIPE_C), D_ALL);
	MMIO_D(TVIDEO_DIP_GCP(PIPE_C), D_ALL);

	MMIO_D(_FDI_RXA_MISC, D_ALL);
	MMIO_D(_FDI_RXB_MISC, D_ALL);
	MMIO_D(_FDI_RXA_TUSIZE1, D_ALL);
	MMIO_D(_FDI_RXA_TUSIZE2, D_ALL);
	MMIO_D(_FDI_RXB_TUSIZE1, D_ALL);
	MMIO_D(_FDI_RXB_TUSIZE2, D_ALL);

	MMIO_DH(PCH_PP_CONTROL, D_ALL, NULL, NULL);
	MMIO_D(PCH_PP_DIVISOR, D_ALL);
	MMIO_D(PCH_PP_STATUS,  D_ALL);
	MMIO_D(PCH_LVDS, D_ALL);
	MMIO_D(_PCH_DPLL_A, D_ALL);
	MMIO_D(_PCH_DPLL_B, D_ALL);
	MMIO_D(_PCH_FPA0, D_ALL);
	MMIO_D(_PCH_FPA1, D_ALL);
	MMIO_D(_PCH_FPB0, D_ALL);
	MMIO_D(_PCH_FPB1, D_ALL);
	MMIO_D(PCH_DREF_CONTROL, D_ALL);
	MMIO_D(PCH_RAWCLK_FREQ, D_ALL);
	MMIO_D(PCH_DPLL_SEL, D_ALL);

	MMIO_D(0x61208, D_ALL);
	MMIO_D(0x6120c, D_ALL);
	MMIO_D(PCH_PP_ON_DELAYS, D_ALL);
	MMIO_D(PCH_PP_OFF_DELAYS, D_ALL);

	MMIO_DH(0xe651c, D_ALL, NULL, NULL);
	MMIO_DH(0xe661c, D_ALL, NULL, NULL);
	MMIO_DH(0xe671c, D_ALL, NULL, NULL);
	MMIO_DH(0xe681c, D_ALL, NULL, NULL);
	MMIO_DH(0xe6c04, D_ALL, NULL, NULL);
	MMIO_DH(0xe6e1c, D_ALL, NULL, NULL);

	MMIO_RO(PCH_PORT_HOTPLUG, D_ALL, 0,
		PORTA_HOTPLUG_STATUS_MASK
		| PORTB_HOTPLUG_STATUS_MASK
		| PORTC_HOTPLUG_STATUS_MASK
		| PORTD_HOTPLUG_STATUS_MASK,
		NULL, NULL);

	MMIO_DH(LCPLL_CTL, D_ALL, NULL, NULL);
	MMIO_D(FUSE_STRAP, D_ALL);
	MMIO_D(DIGITAL_PORT_HOTPLUG_CNTRL, D_ALL);

	MMIO_D(DISP_ARB_CTL, D_ALL);
	MMIO_D(DISP_ARB_CTL2, D_ALL);

	MMIO_D(ILK_DISPLAY_CHICKEN1, D_ALL);
	MMIO_D(ILK_DISPLAY_CHICKEN2, D_ALL);
	MMIO_D(ILK_DSPCLK_GATE_D, D_ALL);

	MMIO_D(SOUTH_CHICKEN1, D_ALL);
	MMIO_DH(SOUTH_CHICKEN2, D_ALL, NULL, NULL);
	MMIO_D(_TRANSA_CHICKEN1, D_ALL);
	MMIO_D(_TRANSB_CHICKEN1, D_ALL);
	MMIO_D(SOUTH_DSPCLK_GATE_D, D_ALL);
	MMIO_D(_TRANSA_CHICKEN2, D_ALL);
	MMIO_D(_TRANSB_CHICKEN2, D_ALL);

	MMIO_D(ILK_DPFC_CB_BASE, D_ALL);
	MMIO_D(ILK_DPFC_CONTROL, D_ALL);
	MMIO_D(ILK_DPFC_RECOMP_CTL, D_ALL);
	MMIO_D(ILK_DPFC_STATUS, D_ALL);
	MMIO_D(ILK_DPFC_FENCE_YOFF, D_ALL);
	MMIO_D(ILK_DPFC_CHICKEN, D_ALL);
	MMIO_D(ILK_FBC_RT_BASE, D_ALL);

	MMIO_D(IPS_CTL, D_ALL);

	MMIO_D(PIPE_CSC_COEFF_RY_GY(PIPE_A), D_ALL);
	MMIO_D(PIPE_CSC_COEFF_BY(PIPE_A), D_ALL);
	MMIO_D(PIPE_CSC_COEFF_RU_GU(PIPE_A), D_ALL);
	MMIO_D(PIPE_CSC_COEFF_BU(PIPE_A), D_ALL);
	MMIO_D(PIPE_CSC_COEFF_RV_GV(PIPE_A), D_ALL);
	MMIO_D(PIPE_CSC_COEFF_BV(PIPE_A), D_ALL);
	MMIO_D(PIPE_CSC_MODE(PIPE_A), D_ALL);
	MMIO_D(PIPE_CSC_PREOFF_HI(PIPE_A), D_ALL);
	MMIO_D(PIPE_CSC_PREOFF_ME(PIPE_A), D_ALL);
	MMIO_D(PIPE_CSC_PREOFF_LO(PIPE_A), D_ALL);
	MMIO_D(PIPE_CSC_POSTOFF_HI(PIPE_A), D_ALL);
	MMIO_D(PIPE_CSC_POSTOFF_ME(PIPE_A), D_ALL);
	MMIO_D(PIPE_CSC_POSTOFF_LO(PIPE_A), D_ALL);

	MMIO_D(PIPE_CSC_COEFF_RY_GY(PIPE_B), D_ALL);
	MMIO_D(PIPE_CSC_COEFF_BY(PIPE_B), D_ALL);
	MMIO_D(PIPE_CSC_COEFF_RU_GU(PIPE_B), D_ALL);
	MMIO_D(PIPE_CSC_COEFF_BU(PIPE_B), D_ALL);
	MMIO_D(PIPE_CSC_COEFF_RV_GV(PIPE_B), D_ALL);
	MMIO_D(PIPE_CSC_COEFF_BV(PIPE_B), D_ALL);
	MMIO_D(PIPE_CSC_MODE(PIPE_B), D_ALL);
	MMIO_D(PIPE_CSC_PREOFF_HI(PIPE_B), D_ALL);
	MMIO_D(PIPE_CSC_PREOFF_ME(PIPE_B), D_ALL);
	MMIO_D(PIPE_CSC_PREOFF_LO(PIPE_B), D_ALL);
	MMIO_D(PIPE_CSC_POSTOFF_HI(PIPE_B), D_ALL);
	MMIO_D(PIPE_CSC_POSTOFF_ME(PIPE_B), D_ALL);
	MMIO_D(PIPE_CSC_POSTOFF_LO(PIPE_B), D_ALL);

	MMIO_D(PIPE_CSC_COEFF_RY_GY(PIPE_C), D_ALL);
	MMIO_D(PIPE_CSC_COEFF_BY(PIPE_C), D_ALL);
	MMIO_D(PIPE_CSC_COEFF_RU_GU(PIPE_C), D_ALL);
	MMIO_D(PIPE_CSC_COEFF_BU(PIPE_C), D_ALL);
	MMIO_D(PIPE_CSC_COEFF_RV_GV(PIPE_C), D_ALL);
	MMIO_D(PIPE_CSC_COEFF_BV(PIPE_C), D_ALL);
	MMIO_D(PIPE_CSC_MODE(PIPE_C), D_ALL);
	MMIO_D(PIPE_CSC_PREOFF_HI(PIPE_C), D_ALL);
	MMIO_D(PIPE_CSC_PREOFF_ME(PIPE_C), D_ALL);
	MMIO_D(PIPE_CSC_PREOFF_LO(PIPE_C), D_ALL);
	MMIO_D(PIPE_CSC_POSTOFF_HI(PIPE_C), D_ALL);
	MMIO_D(PIPE_CSC_POSTOFF_ME(PIPE_C), D_ALL);
	MMIO_D(PIPE_CSC_POSTOFF_LO(PIPE_C), D_ALL);

	MMIO_D(0x60110, D_ALL);
	MMIO_D(0x61110, D_ALL);
	MMIO_F(0x70400, 0x40, 0, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(0x71400, 0x40, 0, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(0x72400, 0x40, 0, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(0x70440, 0xc, 0, 0, 0, D_PRE_SKL, NULL, NULL);
	MMIO_F(0x71440, 0xc, 0, 0, 0, D_PRE_SKL, NULL, NULL);
	MMIO_F(0x72440, 0xc, 0, 0, 0, D_PRE_SKL, NULL, NULL);
	MMIO_F(0x7044c, 0xc, 0, 0, 0, D_PRE_SKL, NULL, NULL);
	MMIO_F(0x7144c, 0xc, 0, 0, 0, D_PRE_SKL, NULL, NULL);
	MMIO_F(0x7244c, 0xc, 0, 0, 0, D_PRE_SKL, NULL, NULL);

	MMIO_D(PIPE_WM_LINETIME(PIPE_A), D_ALL);
	MMIO_D(PIPE_WM_LINETIME(PIPE_B), D_ALL);
	MMIO_D(PIPE_WM_LINETIME(PIPE_C), D_ALL);
	MMIO_D(SPLL_CTL, D_ALL);
	MMIO_D(_WRPLL_CTL1, D_ALL);
	MMIO_D(_WRPLL_CTL2, D_ALL);
	MMIO_D(PORT_CLK_SEL(PORT_A), D_ALL);
	MMIO_D(PORT_CLK_SEL(PORT_B), D_ALL);
	MMIO_D(PORT_CLK_SEL(PORT_C), D_ALL);
	MMIO_D(PORT_CLK_SEL(PORT_D), D_ALL);
	MMIO_D(PORT_CLK_SEL(PORT_E), D_ALL);
	MMIO_D(TRANS_CLK_SEL(TRANSCODER_A), D_ALL);
	MMIO_D(TRANS_CLK_SEL(TRANSCODER_B), D_ALL);
	MMIO_D(TRANS_CLK_SEL(TRANSCODER_C), D_ALL);

	MMIO_D(HSW_NDE_RSTWRN_OPT, D_ALL);
	MMIO_D(0x46508, D_ALL);

	MMIO_D(0x49080, D_ALL);
	MMIO_D(0x49180, D_ALL);
	MMIO_D(0x49280, D_ALL);

	MMIO_F(0x49090, 0x14, 0, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(0x49190, 0x14, 0, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(0x49290, 0x14, 0, 0, 0, D_ALL, NULL, NULL);

	MMIO_D(GAMMA_MODE(PIPE_A), D_ALL);
	MMIO_D(GAMMA_MODE(PIPE_B), D_ALL);
	MMIO_D(GAMMA_MODE(PIPE_C), D_ALL);

	MMIO_D(0x4a400, D_ALL);
	MMIO_D(0x4ac00, D_ALL);
	MMIO_D(0x4b400, D_ALL);

	MMIO_D(PIPE_MULT(PIPE_A), D_ALL);
	MMIO_D(PIPE_MULT(PIPE_B), D_ALL);
	MMIO_D(PIPE_MULT(PIPE_C), D_ALL);

	MMIO_D(HSW_TVIDEO_DIP_CTL(TRANSCODER_A), D_ALL);
	MMIO_D(HSW_TVIDEO_DIP_CTL(TRANSCODER_B), D_ALL);
	MMIO_D(HSW_TVIDEO_DIP_CTL(TRANSCODER_C), D_ALL);

	MMIO_DH(SFUSE_STRAP, D_ALL, NULL, NULL);
	MMIO_D(SBI_ADDR, D_ALL);
	MMIO_DH(SBI_DATA, D_ALL, NULL, NULL);
	MMIO_DH(SBI_CTL_STAT, D_ALL, NULL, NULL);
	MMIO_D(PIXCLK_GATE, D_ALL);

	MMIO_F(_DPA_AUX_CH_CTL, 6 * 4, 0, 0, 0, D_ALL, NULL, NULL);

	MMIO_RO(DDI_BUF_CTL(PORT_A), D_ALL, 0,
		DDI_INIT_DISPLAY_DETECTED, NULL, NULL);
	MMIO_RO(DDI_BUF_CTL(PORT_B), D_ALL, 0,
		DDI_INIT_DISPLAY_DETECTED, NULL, NULL);
	MMIO_RO(DDI_BUF_CTL(PORT_C), D_ALL, 0,
		DDI_INIT_DISPLAY_DETECTED, NULL, NULL);
	MMIO_RO(DDI_BUF_CTL(PORT_D), D_ALL, 0,
		DDI_INIT_DISPLAY_DETECTED, NULL, NULL);
	MMIO_RO(DDI_BUF_CTL(PORT_E), D_ALL, 0,
		DDI_INIT_DISPLAY_DETECTED, NULL, NULL);

	MMIO_DH(DP_TP_CTL(PORT_A), D_ALL, NULL, NULL);
	MMIO_DH(DP_TP_CTL(PORT_B), D_ALL, NULL, NULL);
	MMIO_DH(DP_TP_CTL(PORT_C), D_ALL, NULL, NULL);
	MMIO_DH(DP_TP_CTL(PORT_D), D_ALL, NULL, NULL);
	MMIO_DH(DP_TP_CTL(PORT_E), D_ALL, NULL, NULL);

	MMIO_RO(DP_TP_STATUS(PORT_A), D_ALL, 0,
			(1 << 27) | (1 << 26) | (1 << 24), NULL, NULL);
	MMIO_RO(DP_TP_STATUS(PORT_B), D_ALL, 0,
			(1 << 27) | (1 << 26) | (1 << 24), NULL, NULL);
	MMIO_RO(DP_TP_STATUS(PORT_C), D_ALL, 0,
			(1 << 27) | (1 << 26) | (1 << 24), NULL, NULL);
	MMIO_RO(DP_TP_STATUS(PORT_D), D_ALL, 0,
			(1 << 27) | (1 << 26) | (1 << 24), NULL, NULL);
	MMIO_RO(DP_TP_STATUS(PORT_E), D_ALL, 0,
			(1 << 27) | (1 << 26) | (1 << 24), NULL, NULL);

	MMIO_F(_DDI_BUF_TRANS_A, 0x50, 0, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(0x64e60, 0x50, 0, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(0x64eC0, 0x50, 0, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(0x64f20, 0x50, 0, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(0x64f80, 0x50, 0, 0, 0, D_ALL, NULL, NULL);

	MMIO_D(HSW_AUD_CFG(PIPE_A), D_ALL);
	MMIO_D(HSW_AUD_PIN_ELD_CP_VLD, D_ALL);

	MMIO_DH(_TRANS_DDI_FUNC_CTL_A, D_ALL, NULL, NULL);
	MMIO_DH(_TRANS_DDI_FUNC_CTL_B, D_ALL, NULL, NULL);
	MMIO_DH(_TRANS_DDI_FUNC_CTL_C, D_ALL, NULL, NULL);
	MMIO_DH(_TRANS_DDI_FUNC_CTL_EDP, D_ALL, NULL, NULL);

	MMIO_D(_TRANSA_MSA_MISC, D_ALL);
	MMIO_D(_TRANSB_MSA_MISC, D_ALL);
	MMIO_D(_TRANSC_MSA_MISC, D_ALL);
	MMIO_D(_TRANS_EDP_MSA_MISC, D_ALL);

	MMIO_DH(FORCEWAKE, D_ALL, NULL, NULL);
	MMIO_D(FORCEWAKE_ACK, D_ALL);
	MMIO_D(GEN6_GT_CORE_STATUS, D_ALL);
	MMIO_D(GEN6_GT_THREAD_STATUS_REG, D_ALL);
	MMIO_D(GTFIFODBG, D_ALL);
	MMIO_D(GTFIFOCTL, D_ALL);
	MMIO_DH(FORCEWAKE_MT, D_PRE_SKL, NULL, mul_force_wake_write);
	MMIO_DH(FORCEWAKE_ACK_HSW, D_HSW | D_BDW, NULL, NULL);
	MMIO_D(ECOBUS, D_ALL);
	MMIO_DH(GEN6_RC_CONTROL, D_ALL, NULL, NULL);
	MMIO_DH(GEN6_RC_STATE, D_ALL, NULL, NULL);
	MMIO_D(GEN6_RPNSWREQ, D_ALL);
	MMIO_D(GEN6_RC_VIDEO_FREQ, D_ALL);
	MMIO_D(GEN6_RP_DOWN_TIMEOUT, D_ALL);
	MMIO_D(GEN6_RP_INTERRUPT_LIMITS, D_ALL);
	MMIO_D(GEN6_RPSTAT1, D_ALL);
	MMIO_D(GEN6_RP_CONTROL, D_ALL);
	MMIO_D(GEN6_RP_UP_THRESHOLD, D_ALL);
	MMIO_D(GEN6_RP_DOWN_THRESHOLD, D_ALL);
	MMIO_D(GEN6_RP_CUR_UP_EI, D_ALL);
	MMIO_D(GEN6_RP_CUR_UP, D_ALL);
	MMIO_D(GEN6_RP_PREV_UP, D_ALL);
	MMIO_D(GEN6_RP_CUR_DOWN_EI, D_ALL);
	MMIO_D(GEN6_RP_CUR_DOWN, D_ALL);
	MMIO_D(GEN6_RP_PREV_DOWN, D_ALL);
	MMIO_D(GEN6_RP_UP_EI, D_ALL);
	MMIO_D(GEN6_RP_DOWN_EI, D_ALL);
	MMIO_D(GEN6_RP_IDLE_HYSTERSIS, D_ALL);
	MMIO_D(GEN6_RC1_WAKE_RATE_LIMIT, D_ALL);
	MMIO_D(GEN6_RC6_WAKE_RATE_LIMIT, D_ALL);
	MMIO_D(GEN6_RC6pp_WAKE_RATE_LIMIT, D_ALL);
	MMIO_D(GEN6_RC_EVALUATION_INTERVAL, D_ALL);
	MMIO_D(GEN6_RC_IDLE_HYSTERSIS, D_ALL);
	MMIO_D(GEN6_RC_SLEEP, D_ALL);
	MMIO_D(GEN6_RC1e_THRESHOLD, D_ALL);
	MMIO_D(GEN6_RC6_THRESHOLD, D_ALL);
	MMIO_D(GEN6_RC6p_THRESHOLD, D_ALL);
	MMIO_D(GEN6_RC6pp_THRESHOLD, D_ALL);
	MMIO_D(GEN6_PMINTRMSK, D_ALL);
	MMIO_DH(HSW_PWR_WELL_BIOS, D_HSW | D_BDW, NULL, NULL);
	MMIO_DH(HSW_PWR_WELL_DRIVER, D_HSW | D_BDW, NULL, NULL);
	MMIO_DH(HSW_PWR_WELL_KVMR, D_HSW | D_BDW, NULL, NULL);
	MMIO_DH(HSW_PWR_WELL_DEBUG, D_HSW | D_BDW, NULL, NULL);
	MMIO_DH(HSW_PWR_WELL_CTL5, D_HSW | D_BDW, NULL, NULL);
	MMIO_DH(HSW_PWR_WELL_CTL6, D_HSW | D_BDW, NULL, NULL);

	MMIO_D(RSTDBYCTL, D_ALL);

	MMIO_DH(GEN6_GDRST, D_ALL, NULL, gdrst_mmio_write);
	MMIO_F(FENCE_REG_GEN6_LO(0), 0x80, 0, 0, 0, D_ALL, fence_mmio_read, fence_mmio_write);
	MMIO_F(VGT_PVINFO_PAGE, VGT_PVINFO_SIZE, F_UNALIGN, 0, 0, D_ALL, pvinfo_mmio_read, pvinfo_mmio_write);
	MMIO_DH(CPU_VGACNTRL, D_ALL, NULL, NULL);

	MMIO_F(MCHBAR_MIRROR_BASE_SNB, 0x40000, 0, 0, 0, D_ALL, NULL, NULL);

	MMIO_D(TILECTL, D_ALL);

	MMIO_D(GEN6_UCGCTL1, D_ALL);
	MMIO_D(GEN6_UCGCTL2, D_ALL);

	MMIO_F(0x4f000, 0x90, 0, 0, 0, D_ALL, NULL, NULL);

	MMIO_D(GEN6_PCODE_MAILBOX, D_PRE_SKL);
	MMIO_D(GEN6_PCODE_DATA, D_ALL);
	MMIO_D(0x13812c, D_ALL);
	MMIO_DH(GEN7_ERR_INT, D_ALL, NULL, NULL);
	MMIO_D(HSW_EDRAM_CAP, D_ALL);
	MMIO_D(HSW_IDICR, D_ALL);
	MMIO_DH(GFX_FLSH_CNTL_GEN6, D_ALL, NULL, NULL);

	MMIO_D(0x3c, D_ALL);
	MMIO_D(0x860, D_ALL);
	MMIO_D(ECOSKPD, D_ALL);
	MMIO_D(0x121d0, D_ALL);
	MMIO_D(GEN6_BLITTER_ECOSKPD, D_ALL);
	MMIO_D(0x41d0, D_ALL);
	MMIO_D(GAC_ECO_BITS, D_ALL);
	MMIO_D(0x6200, D_ALL);
	MMIO_D(0x6204, D_ALL);
	MMIO_D(0x6208, D_ALL);
	MMIO_D(0x7118, D_ALL);
	MMIO_D(0x7180, D_ALL);
	MMIO_D(0x7408, D_ALL);
	MMIO_D(0x7c00, D_ALL);
	MMIO_D(GEN6_MBCTL, D_ALL);
	MMIO_D(0x911c, D_ALL);
	MMIO_D(0x9120, D_ALL);

	MMIO_D(GAB_CTL, D_ALL);
	MMIO_D(0x48800, D_ALL);
	MMIO_D(0xce044, D_ALL);
	MMIO_D(0xe6500, D_ALL);
	MMIO_D(0xe6504, D_ALL);
	MMIO_D(0xe6600, D_ALL);
	MMIO_D(0xe6604, D_ALL);
	MMIO_D(0xe6700, D_ALL);
	MMIO_D(0xe6704, D_ALL);
	MMIO_D(0xe6800, D_ALL);
	MMIO_D(0xe6804, D_ALL);
	MMIO_D(PCH_GMBUS4, D_ALL);
	MMIO_D(PCH_GMBUS5, D_ALL);

	MMIO_D(0x902c, D_ALL);
	MMIO_D(0xec008, D_ALL);
	MMIO_D(0xec00c, D_ALL);
	MMIO_D(0xec008 + 0x18, D_ALL);
	MMIO_D(0xec00c + 0x18, D_ALL);
	MMIO_D(0xec008 + 0x18 * 2, D_ALL);
	MMIO_D(0xec00c + 0x18 * 2, D_ALL);
	MMIO_D(0xec008 + 0x18 * 3, D_ALL);
	MMIO_D(0xec00c + 0x18 * 3, D_ALL);
	MMIO_D(0xec408, D_ALL);
	MMIO_D(0xec40c, D_ALL);
	MMIO_D(0xec408 + 0x18, D_ALL);
	MMIO_D(0xec40c + 0x18, D_ALL);
	MMIO_D(0xec408 + 0x18 * 2, D_ALL);
	MMIO_D(0xec40c + 0x18 * 2, D_ALL);
	MMIO_D(0xec408 + 0x18 * 3, D_ALL);
	MMIO_D(0xec40c + 0x18 * 3, D_ALL);
	MMIO_D(0xfc810, D_ALL);
	MMIO_D(0xfc81c, D_ALL);
	MMIO_D(0xfc828, D_ALL);
	MMIO_D(0xfc834, D_ALL);
	MMIO_D(0xfcc00, D_ALL);
	MMIO_D(0xfcc0c, D_ALL);
	MMIO_D(0xfcc18, D_ALL);
	MMIO_D(0xfcc24, D_ALL);
	MMIO_D(0xfd000, D_ALL);
	MMIO_D(0xfd00c, D_ALL);
	MMIO_D(0xfd018, D_ALL);
	MMIO_D(0xfd024, D_ALL);
	MMIO_D(0xfd034, D_ALL);

	MMIO_DH(FPGA_DBG, D_ALL, NULL, fpga_dbg_mmio_write);
	MMIO_D(0x2054, D_ALL);
	MMIO_D(0x12054, D_ALL);
	MMIO_D(0x22054, D_ALL);
	MMIO_D(0x1a054, D_ALL);

	MMIO_D(0x44070, D_ALL);

	MMIO_D(0x215c, D_HSW_PLUS);
	MMIO_DFH(0x2178, D_ALL, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0x217c, D_ALL, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0x12178, D_ALL, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0x1217c, D_ALL, F_CMD_ACCESS, NULL, NULL);

	MMIO_F(0x2290, 8, 0, 0, 0, D_HSW_PLUS, NULL, NULL);
	MMIO_D(OACONTROL, D_HSW);
	MMIO_D(0x2b00, D_BDW_PLUS);
	MMIO_D(0x2360, D_BDW_PLUS);
	MMIO_F(0x5200, 32, 0, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(0x5240, 32, 0, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(0x5280, 16, 0, 0, 0, D_ALL, NULL, NULL);

	MMIO_DFH(0x1c17c, D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0x1c178, D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);
	MMIO_D(BCS_SWCTRL, D_ALL);

	MMIO_F(HS_INVOCATION_COUNT, 8, 0, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(DS_INVOCATION_COUNT, 8, 0, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(IA_VERTICES_COUNT, 8, 0, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(IA_PRIMITIVES_COUNT, 8, 0, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(VS_INVOCATION_COUNT, 8, 0, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(GS_INVOCATION_COUNT, 8, 0, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(GS_PRIMITIVES_COUNT, 8, 0, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(CL_INVOCATION_COUNT, 8, 0, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(CL_PRIMITIVES_COUNT, 8, 0, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(PS_INVOCATION_COUNT, 8, 0, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(PS_DEPTH_COUNT, 8, 0, 0, 0, D_ALL, NULL, NULL);
	MMIO_DH(0x4260, D_BDW_PLUS, NULL, NULL);
	MMIO_DH(0x4264, D_BDW_PLUS, NULL, NULL);
	MMIO_DH(0x4268, D_BDW_PLUS, NULL, NULL);
	MMIO_DH(0x426c, D_BDW_PLUS, NULL, NULL);
	MMIO_DH(0x4270, D_BDW_PLUS, NULL, NULL);
	MMIO_DFH(0x4094, D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);

	return 0;
}

static int init_broadwell_mmio_info(struct intel_gvt *gvt)
{
	struct drm_i915_private *dev_priv = gvt->dev_priv;
	int ret;

	MMIO_DH(RING_IMR(GEN8_BSD2_RING_BASE), D_BDW_PLUS, NULL,
			intel_vgpu_reg_imr_handler);

	MMIO_DH(GEN8_GT_IMR(0), D_BDW_PLUS, NULL, intel_vgpu_reg_imr_handler);
	MMIO_DH(GEN8_GT_IER(0), D_BDW_PLUS, NULL, intel_vgpu_reg_ier_handler);
	MMIO_DH(GEN8_GT_IIR(0), D_BDW_PLUS, NULL, intel_vgpu_reg_iir_handler);
	MMIO_D(GEN8_GT_ISR(0), D_BDW_PLUS);

	MMIO_DH(GEN8_GT_IMR(1), D_BDW_PLUS, NULL, intel_vgpu_reg_imr_handler);
	MMIO_DH(GEN8_GT_IER(1), D_BDW_PLUS, NULL, intel_vgpu_reg_ier_handler);
	MMIO_DH(GEN8_GT_IIR(1), D_BDW_PLUS, NULL, intel_vgpu_reg_iir_handler);
	MMIO_D(GEN8_GT_ISR(1), D_BDW_PLUS);

	MMIO_DH(GEN8_GT_IMR(2), D_BDW_PLUS, NULL, intel_vgpu_reg_imr_handler);
	MMIO_DH(GEN8_GT_IER(2), D_BDW_PLUS, NULL, intel_vgpu_reg_ier_handler);
	MMIO_DH(GEN8_GT_IIR(2), D_BDW_PLUS, NULL, intel_vgpu_reg_iir_handler);
	MMIO_D(GEN8_GT_ISR(2), D_BDW_PLUS);

	MMIO_DH(GEN8_GT_IMR(3), D_BDW_PLUS, NULL, intel_vgpu_reg_imr_handler);
	MMIO_DH(GEN8_GT_IER(3), D_BDW_PLUS, NULL, intel_vgpu_reg_ier_handler);
	MMIO_DH(GEN8_GT_IIR(3), D_BDW_PLUS, NULL, intel_vgpu_reg_iir_handler);
	MMIO_D(GEN8_GT_ISR(3), D_BDW_PLUS);

	MMIO_DH(GEN8_DE_PIPE_IMR(PIPE_A), D_BDW_PLUS, NULL,
		intel_vgpu_reg_imr_handler);
	MMIO_DH(GEN8_DE_PIPE_IER(PIPE_A), D_BDW_PLUS, NULL,
		intel_vgpu_reg_ier_handler);
	MMIO_DH(GEN8_DE_PIPE_IIR(PIPE_A), D_BDW_PLUS, NULL,
		intel_vgpu_reg_iir_handler);
	MMIO_D(GEN8_DE_PIPE_ISR(PIPE_A), D_BDW_PLUS);

	MMIO_DH(GEN8_DE_PIPE_IMR(PIPE_B), D_BDW_PLUS, NULL,
		intel_vgpu_reg_imr_handler);
	MMIO_DH(GEN8_DE_PIPE_IER(PIPE_B), D_BDW_PLUS, NULL,
		intel_vgpu_reg_ier_handler);
	MMIO_DH(GEN8_DE_PIPE_IIR(PIPE_B), D_BDW_PLUS, NULL,
		intel_vgpu_reg_iir_handler);
	MMIO_D(GEN8_DE_PIPE_ISR(PIPE_B), D_BDW_PLUS);

	MMIO_DH(GEN8_DE_PIPE_IMR(PIPE_C), D_BDW_PLUS, NULL,
		intel_vgpu_reg_imr_handler);
	MMIO_DH(GEN8_DE_PIPE_IER(PIPE_C), D_BDW_PLUS, NULL,
		intel_vgpu_reg_ier_handler);
	MMIO_DH(GEN8_DE_PIPE_IIR(PIPE_C), D_BDW_PLUS, NULL,
		intel_vgpu_reg_iir_handler);
	MMIO_D(GEN8_DE_PIPE_ISR(PIPE_C), D_BDW_PLUS);

	MMIO_DH(GEN8_DE_PORT_IMR, D_BDW_PLUS, NULL, intel_vgpu_reg_imr_handler);
	MMIO_DH(GEN8_DE_PORT_IER, D_BDW_PLUS, NULL, intel_vgpu_reg_ier_handler);
	MMIO_DH(GEN8_DE_PORT_IIR, D_BDW_PLUS, NULL, intel_vgpu_reg_iir_handler);
	MMIO_D(GEN8_DE_PORT_ISR, D_BDW_PLUS);

	MMIO_DH(GEN8_DE_MISC_IMR, D_BDW_PLUS, NULL, intel_vgpu_reg_imr_handler);
	MMIO_DH(GEN8_DE_MISC_IER, D_BDW_PLUS, NULL, intel_vgpu_reg_ier_handler);
	MMIO_DH(GEN8_DE_MISC_IIR, D_BDW_PLUS, NULL, intel_vgpu_reg_iir_handler);
	MMIO_D(GEN8_DE_MISC_ISR, D_BDW_PLUS);

	MMIO_DH(GEN8_PCU_IMR, D_BDW_PLUS, NULL, intel_vgpu_reg_imr_handler);
	MMIO_DH(GEN8_PCU_IER, D_BDW_PLUS, NULL, intel_vgpu_reg_ier_handler);
	MMIO_DH(GEN8_PCU_IIR, D_BDW_PLUS, NULL, intel_vgpu_reg_iir_handler);
	MMIO_D(GEN8_PCU_ISR, D_BDW_PLUS);

	MMIO_DH(GEN8_MASTER_IRQ, D_BDW_PLUS, NULL,
		intel_vgpu_reg_master_irq_handler);

	MMIO_D(RING_HWSTAM(GEN8_BSD2_RING_BASE), D_BDW_PLUS);
	MMIO_D(0x1c134, D_BDW_PLUS);

	MMIO_D(RING_TAIL(GEN8_BSD2_RING_BASE), D_BDW_PLUS);
	MMIO_D(RING_HEAD(GEN8_BSD2_RING_BASE),  D_BDW_PLUS);
	MMIO_GM(RING_START(GEN8_BSD2_RING_BASE), D_BDW_PLUS, NULL, NULL);
	MMIO_D(RING_CTL(GEN8_BSD2_RING_BASE), D_BDW_PLUS);
	MMIO_D(RING_ACTHD(GEN8_BSD2_RING_BASE), D_BDW_PLUS);
	MMIO_D(RING_ACTHD_UDW(GEN8_BSD2_RING_BASE), D_BDW_PLUS);
	MMIO_DFH(0x1c29c, D_BDW_PLUS, F_MODE_MASK, NULL, NULL);
	MMIO_DFH(RING_MI_MODE(GEN8_BSD2_RING_BASE), D_BDW_PLUS, F_MODE_MASK,
			NULL, NULL);
	MMIO_DFH(RING_INSTPM(GEN8_BSD2_RING_BASE), D_BDW_PLUS, F_MODE_MASK,
			NULL, NULL);
	MMIO_DFH(RING_TIMESTAMP(GEN8_BSD2_RING_BASE), D_BDW_PLUS, F_MODE_MASK,
			NULL, NULL);

	MMIO_RING_D(RING_ACTHD_UDW, D_BDW_PLUS);

#define RING_REG(base) (base + 0x230)
	MMIO_RING_DFH(RING_REG, D_BDW_PLUS, 0, NULL, NULL);
	MMIO_DH(RING_REG(GEN8_BSD2_RING_BASE), D_BDW_PLUS, NULL, NULL);
#undef RING_REG

#define RING_REG(base) (base + 0x234)
	MMIO_RING_F(RING_REG, 8, F_RO, 0, ~0, D_BDW_PLUS, NULL, NULL);
	MMIO_F(RING_REG(GEN8_BSD2_RING_BASE), 4, F_RO, 0, ~0LL, D_BDW_PLUS, NULL, NULL);
#undef RING_REG

#define RING_REG(base) (base + 0x244)
	MMIO_RING_D(RING_REG, D_BDW_PLUS);
	MMIO_D(RING_REG(GEN8_BSD2_RING_BASE), D_BDW_PLUS);
#undef RING_REG

#define RING_REG(base) (base + 0x370)
	MMIO_RING_F(RING_REG, 48, F_RO, 0, ~0, D_BDW_PLUS, NULL, NULL);
	MMIO_F(RING_REG(GEN8_BSD2_RING_BASE), 48, F_RO, 0, ~0, D_BDW_PLUS,
			NULL, NULL);
#undef RING_REG

#define RING_REG(base) (base + 0x3a0)
	MMIO_RING_DFH(RING_REG, D_BDW_PLUS, F_MODE_MASK, NULL, NULL);
	MMIO_DFH(RING_REG(GEN8_BSD2_RING_BASE), D_BDW_PLUS, F_MODE_MASK, NULL, NULL);
#undef RING_REG

	MMIO_D(PIPEMISC(PIPE_A), D_BDW_PLUS);
	MMIO_D(PIPEMISC(PIPE_B), D_BDW_PLUS);
	MMIO_D(PIPEMISC(PIPE_C), D_BDW_PLUS);
	MMIO_D(0x1c1d0, D_BDW_PLUS);
	MMIO_D(GEN6_MBCUNIT_SNPCR, D_BDW_PLUS);
	MMIO_D(GEN7_MISCCPCTL, D_BDW_PLUS);
	MMIO_D(0x1c054, D_BDW_PLUS);

	MMIO_D(GEN8_PRIVATE_PAT_LO, D_BDW_PLUS);
	MMIO_D(GEN8_PRIVATE_PAT_HI, D_BDW_PLUS);

	MMIO_D(GAMTARBMODE, D_BDW_PLUS);

#define RING_REG(base) (base + 0x270)
	MMIO_RING_F(RING_REG, 32, 0, 0, 0, D_BDW_PLUS, NULL, NULL);
	MMIO_F(RING_REG(GEN8_BSD2_RING_BASE), 32, 0, 0, 0, D_BDW_PLUS, NULL, NULL);
#undef RING_REG

	MMIO_RING_GM(RING_HWS_PGA, D_BDW_PLUS, NULL, NULL);
	MMIO_GM(0x1c080, D_BDW_PLUS, NULL, NULL);

	MMIO_DFH(HDC_CHICKEN0, D_BDW_PLUS, F_MODE_MASK, NULL, NULL);

	MMIO_D(CHICKEN_PIPESL_1(PIPE_A), D_BDW);
	MMIO_D(CHICKEN_PIPESL_1(PIPE_B), D_BDW);
	MMIO_D(CHICKEN_PIPESL_1(PIPE_C), D_BDW);

	MMIO_D(WM_MISC, D_BDW);
	MMIO_D(BDW_EDP_PSR_BASE, D_BDW);

	MMIO_D(0x66c00, D_BDW_PLUS);
	MMIO_D(0x66c04, D_BDW_PLUS);

	MMIO_D(HSW_GTT_CACHE_EN, D_BDW_PLUS);

	MMIO_D(GEN8_EU_DISABLE0, D_BDW_PLUS);
	MMIO_D(GEN8_EU_DISABLE1, D_BDW_PLUS);
	MMIO_D(GEN8_EU_DISABLE2, D_BDW_PLUS);

	MMIO_D(0xfdc, D_BDW);
	MMIO_D(GEN8_ROW_CHICKEN, D_BDW_PLUS);
	MMIO_D(GEN7_ROW_CHICKEN2, D_BDW_PLUS);
	MMIO_D(GEN8_UCGCTL6, D_BDW_PLUS);

	MMIO_D(0xb1f0, D_BDW);
	MMIO_D(0xb1c0, D_BDW);
	MMIO_DFH(GEN8_L3SQCREG4, D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);
	MMIO_D(0xb100, D_BDW);
	MMIO_D(0xb10c, D_BDW);
	MMIO_D(0xb110, D_BDW);

	MMIO_DH(0x24d0, D_BDW_PLUS, NULL, NULL);
	MMIO_DH(0x24d4, D_BDW_PLUS, NULL, NULL);
	MMIO_DH(0x24d8, D_BDW_PLUS, NULL, NULL);
	MMIO_DH(0x24dc, D_BDW_PLUS, NULL, NULL);

	MMIO_D(0x83a4, D_BDW);
	MMIO_D(GEN8_L3_LRA_1_GPGPU, D_BDW_PLUS);

	MMIO_D(0x8430, D_BDW);

	MMIO_D(0x110000, D_BDW_PLUS);

	MMIO_D(0x48400, D_BDW_PLUS);

	MMIO_D(0x6e570, D_BDW_PLUS);
	MMIO_D(0x65f10, D_BDW_PLUS);

	MMIO_DFH(0xe194, D_BDW_PLUS, F_MODE_MASK, NULL, NULL);
	MMIO_DFH(0xe188, D_BDW_PLUS, F_MODE_MASK, NULL, NULL);
	MMIO_DFH(0xe180, D_BDW_PLUS, F_MODE_MASK, NULL, NULL);
	MMIO_DFH(0x2580, D_BDW_PLUS, F_MODE_MASK, NULL, NULL);

	MMIO_D(0x2248, D_BDW);

	return 0;
}

static int init_skl_mmio_info(struct intel_gvt *gvt)
{
	struct drm_i915_private *dev_priv = gvt->dev_priv;
	int ret;

	MMIO_DH(FORCEWAKE_RENDER_GEN9, D_SKL_PLUS, NULL, mul_force_wake_write);
	MMIO_DH(FORCEWAKE_ACK_RENDER_GEN9, D_SKL_PLUS, NULL, NULL);
	MMIO_DH(FORCEWAKE_BLITTER_GEN9, D_SKL_PLUS, NULL, mul_force_wake_write);
	MMIO_DH(FORCEWAKE_ACK_BLITTER_GEN9, D_SKL_PLUS, NULL, NULL);
	MMIO_DH(FORCEWAKE_MEDIA_GEN9, D_SKL_PLUS, NULL, mul_force_wake_write);
	MMIO_DH(FORCEWAKE_ACK_MEDIA_GEN9, D_SKL_PLUS, NULL, NULL);

	MMIO_F(_DPB_AUX_CH_CTL, 6 * 4, 0, 0, 0, D_SKL, NULL, NULL);
	MMIO_F(_DPC_AUX_CH_CTL, 6 * 4, 0, 0, 0, D_SKL, NULL, NULL);
	MMIO_F(_DPD_AUX_CH_CTL, 6 * 4, 0, 0, 0, D_SKL, NULL, NULL);

	MMIO_D(HSW_PWR_WELL_BIOS, D_SKL);
	MMIO_DH(HSW_PWR_WELL_DRIVER, D_SKL, NULL, NULL);

	MMIO_DH(GEN6_PCODE_MAILBOX, D_SKL, NULL, NULL);
	MMIO_D(0xa210, D_SKL_PLUS);
	MMIO_D(GEN9_MEDIA_PG_IDLE_HYSTERESIS, D_SKL_PLUS);
	MMIO_D(GEN9_RENDER_PG_IDLE_HYSTERESIS, D_SKL_PLUS);
	MMIO_DH(0x4ddc, D_SKL, NULL, NULL);
	MMIO_DH(0x42080, D_SKL, NULL, NULL);
	MMIO_D(0x45504, D_SKL);
	MMIO_D(0x45520, D_SKL);
	MMIO_D(0x46000, D_SKL);
	MMIO_DH(0x46010, D_SKL, NULL, NULL);
	MMIO_DH(0x46014, D_SKL, NULL, NULL);
	MMIO_D(0x6C040, D_SKL);
	MMIO_D(0x6C048, D_SKL);
	MMIO_D(0x6C050, D_SKL);
	MMIO_D(0x6C044, D_SKL);
	MMIO_D(0x6C04C, D_SKL);
	MMIO_D(0x6C054, D_SKL);
	MMIO_D(0x6c058, D_SKL);
	MMIO_D(0x6c05c, D_SKL);
	MMIO_DH(0x6c060, D_SKL, NULL, NULL);

	MMIO_DH(SKL_PS_WIN_POS(PIPE_A, 0), D_SKL, NULL, NULL);
	MMIO_DH(SKL_PS_WIN_POS(PIPE_A, 1), D_SKL, NULL, NULL);
	MMIO_DH(SKL_PS_WIN_POS(PIPE_B, 0), D_SKL, NULL, NULL);
	MMIO_DH(SKL_PS_WIN_POS(PIPE_B, 1), D_SKL, NULL, NULL);
	MMIO_DH(SKL_PS_WIN_POS(PIPE_C, 0), D_SKL, NULL, NULL);
	MMIO_DH(SKL_PS_WIN_POS(PIPE_C, 1), D_SKL, NULL, NULL);

	MMIO_DH(SKL_PS_WIN_SZ(PIPE_A, 0), D_SKL, NULL, NULL);
	MMIO_DH(SKL_PS_WIN_SZ(PIPE_A, 1), D_SKL, NULL, NULL);
	MMIO_DH(SKL_PS_WIN_SZ(PIPE_B, 0), D_SKL, NULL, NULL);
	MMIO_DH(SKL_PS_WIN_SZ(PIPE_B, 1), D_SKL, NULL, NULL);
	MMIO_DH(SKL_PS_WIN_SZ(PIPE_C, 0), D_SKL, NULL, NULL);
	MMIO_DH(SKL_PS_WIN_SZ(PIPE_C, 1), D_SKL, NULL, NULL);

	MMIO_DH(SKL_PS_CTRL(PIPE_A, 0), D_SKL, NULL, NULL);
	MMIO_DH(SKL_PS_CTRL(PIPE_A, 1), D_SKL, NULL, NULL);
	MMIO_DH(SKL_PS_CTRL(PIPE_B, 0), D_SKL, NULL, NULL);
	MMIO_DH(SKL_PS_CTRL(PIPE_B, 1), D_SKL, NULL, NULL);
	MMIO_DH(SKL_PS_CTRL(PIPE_C, 0), D_SKL, NULL, NULL);
	MMIO_DH(SKL_PS_CTRL(PIPE_C, 1), D_SKL, NULL, NULL);

	MMIO_DH(PLANE_BUF_CFG(PIPE_A, 0), D_SKL, NULL, NULL);
	MMIO_DH(PLANE_BUF_CFG(PIPE_A, 1), D_SKL, NULL, NULL);
	MMIO_DH(PLANE_BUF_CFG(PIPE_A, 2), D_SKL, NULL, NULL);
	MMIO_DH(PLANE_BUF_CFG(PIPE_A, 3), D_SKL, NULL, NULL);

	MMIO_DH(PLANE_BUF_CFG(PIPE_B, 0), D_SKL, NULL, NULL);
	MMIO_DH(PLANE_BUF_CFG(PIPE_B, 1), D_SKL, NULL, NULL);
	MMIO_DH(PLANE_BUF_CFG(PIPE_B, 2), D_SKL, NULL, NULL);
	MMIO_DH(PLANE_BUF_CFG(PIPE_B, 3), D_SKL, NULL, NULL);

	MMIO_DH(PLANE_BUF_CFG(PIPE_C, 0), D_SKL, NULL, NULL);
	MMIO_DH(PLANE_BUF_CFG(PIPE_C, 1), D_SKL, NULL, NULL);
	MMIO_DH(PLANE_BUF_CFG(PIPE_C, 2), D_SKL, NULL, NULL);
	MMIO_DH(PLANE_BUF_CFG(PIPE_C, 3), D_SKL, NULL, NULL);

	MMIO_DH(CUR_BUF_CFG(PIPE_A), D_SKL, NULL, NULL);
	MMIO_DH(CUR_BUF_CFG(PIPE_B), D_SKL, NULL, NULL);
	MMIO_DH(CUR_BUF_CFG(PIPE_C), D_SKL, NULL, NULL);

	MMIO_F(PLANE_WM(PIPE_A, 0, 0), 4 * 8, 0, 0, 0, D_SKL, NULL, NULL);
	MMIO_F(PLANE_WM(PIPE_A, 1, 0), 4 * 8, 0, 0, 0, D_SKL, NULL, NULL);
	MMIO_F(PLANE_WM(PIPE_A, 2, 0), 4 * 8, 0, 0, 0, D_SKL, NULL, NULL);

	MMIO_F(PLANE_WM(PIPE_B, 0, 0), 4 * 8, 0, 0, 0, D_SKL, NULL, NULL);
	MMIO_F(PLANE_WM(PIPE_B, 1, 0), 4 * 8, 0, 0, 0, D_SKL, NULL, NULL);
	MMIO_F(PLANE_WM(PIPE_B, 2, 0), 4 * 8, 0, 0, 0, D_SKL, NULL, NULL);

	MMIO_F(PLANE_WM(PIPE_C, 0, 0), 4 * 8, 0, 0, 0, D_SKL, NULL, NULL);
	MMIO_F(PLANE_WM(PIPE_C, 1, 0), 4 * 8, 0, 0, 0, D_SKL, NULL, NULL);
	MMIO_F(PLANE_WM(PIPE_C, 2, 0), 4 * 8, 0, 0, 0, D_SKL, NULL, NULL);

	MMIO_F(CUR_WM(PIPE_A, 0), 4 * 8, 0, 0, 0, D_SKL, NULL, NULL);
	MMIO_F(CUR_WM(PIPE_B, 0), 4 * 8, 0, 0, 0, D_SKL, NULL, NULL);
	MMIO_F(CUR_WM(PIPE_C, 0), 4 * 8, 0, 0, 0, D_SKL, NULL, NULL);

	MMIO_DH(PLANE_WM_TRANS(PIPE_A, 0), D_SKL, NULL, NULL);
	MMIO_DH(PLANE_WM_TRANS(PIPE_A, 1), D_SKL, NULL, NULL);
	MMIO_DH(PLANE_WM_TRANS(PIPE_A, 2), D_SKL, NULL, NULL);

	MMIO_DH(PLANE_WM_TRANS(PIPE_B, 0), D_SKL, NULL, NULL);
	MMIO_DH(PLANE_WM_TRANS(PIPE_B, 1), D_SKL, NULL, NULL);
	MMIO_DH(PLANE_WM_TRANS(PIPE_B, 2), D_SKL, NULL, NULL);

	MMIO_DH(PLANE_WM_TRANS(PIPE_C, 0), D_SKL, NULL, NULL);
	MMIO_DH(PLANE_WM_TRANS(PIPE_C, 1), D_SKL, NULL, NULL);
	MMIO_DH(PLANE_WM_TRANS(PIPE_C, 2), D_SKL, NULL, NULL);

	MMIO_DH(CUR_WM_TRANS(PIPE_A), D_SKL, NULL, NULL);
	MMIO_DH(CUR_WM_TRANS(PIPE_B), D_SKL, NULL, NULL);
	MMIO_DH(CUR_WM_TRANS(PIPE_C), D_SKL, NULL, NULL);

	MMIO_DH(PLANE_NV12_BUF_CFG(PIPE_A, 0), D_SKL, NULL, NULL);
	MMIO_DH(PLANE_NV12_BUF_CFG(PIPE_A, 1), D_SKL, NULL, NULL);
	MMIO_DH(PLANE_NV12_BUF_CFG(PIPE_A, 2), D_SKL, NULL, NULL);
	MMIO_DH(PLANE_NV12_BUF_CFG(PIPE_A, 3), D_SKL, NULL, NULL);

	MMIO_DH(PLANE_NV12_BUF_CFG(PIPE_B, 0), D_SKL, NULL, NULL);
	MMIO_DH(PLANE_NV12_BUF_CFG(PIPE_B, 1), D_SKL, NULL, NULL);
	MMIO_DH(PLANE_NV12_BUF_CFG(PIPE_B, 2), D_SKL, NULL, NULL);
	MMIO_DH(PLANE_NV12_BUF_CFG(PIPE_B, 3), D_SKL, NULL, NULL);

	MMIO_DH(PLANE_NV12_BUF_CFG(PIPE_C, 0), D_SKL, NULL, NULL);
	MMIO_DH(PLANE_NV12_BUF_CFG(PIPE_C, 1), D_SKL, NULL, NULL);
	MMIO_DH(PLANE_NV12_BUF_CFG(PIPE_C, 2), D_SKL, NULL, NULL);
	MMIO_DH(PLANE_NV12_BUF_CFG(PIPE_C, 3), D_SKL, NULL, NULL);

	MMIO_DH(_REG_701C0(PIPE_A, 1), D_SKL, NULL, NULL);
	MMIO_DH(_REG_701C0(PIPE_A, 2), D_SKL, NULL, NULL);
	MMIO_DH(_REG_701C0(PIPE_A, 3), D_SKL, NULL, NULL);
	MMIO_DH(_REG_701C0(PIPE_A, 4), D_SKL, NULL, NULL);

	MMIO_DH(_REG_701C0(PIPE_B, 1), D_SKL, NULL, NULL);
	MMIO_DH(_REG_701C0(PIPE_B, 2), D_SKL, NULL, NULL);
	MMIO_DH(_REG_701C0(PIPE_B, 3), D_SKL, NULL, NULL);
	MMIO_DH(_REG_701C0(PIPE_B, 4), D_SKL, NULL, NULL);

	MMIO_DH(_REG_701C0(PIPE_C, 1), D_SKL, NULL, NULL);
	MMIO_DH(_REG_701C0(PIPE_C, 2), D_SKL, NULL, NULL);
	MMIO_DH(_REG_701C0(PIPE_C, 3), D_SKL, NULL, NULL);
	MMIO_DH(_REG_701C0(PIPE_C, 4), D_SKL, NULL, NULL);

	MMIO_DH(_REG_701C4(PIPE_A, 1), D_SKL, NULL, NULL);
	MMIO_DH(_REG_701C4(PIPE_A, 2), D_SKL, NULL, NULL);
	MMIO_DH(_REG_701C4(PIPE_A, 3), D_SKL, NULL, NULL);
	MMIO_DH(_REG_701C4(PIPE_A, 4), D_SKL, NULL, NULL);

	MMIO_DH(_REG_701C4(PIPE_B, 1), D_SKL, NULL, NULL);
	MMIO_DH(_REG_701C4(PIPE_B, 2), D_SKL, NULL, NULL);
	MMIO_DH(_REG_701C4(PIPE_B, 3), D_SKL, NULL, NULL);
	MMIO_DH(_REG_701C4(PIPE_B, 4), D_SKL, NULL, NULL);

	MMIO_DH(_REG_701C4(PIPE_C, 1), D_SKL, NULL, NULL);
	MMIO_DH(_REG_701C4(PIPE_C, 2), D_SKL, NULL, NULL);
	MMIO_DH(_REG_701C4(PIPE_C, 3), D_SKL, NULL, NULL);
	MMIO_DH(_REG_701C4(PIPE_C, 4), D_SKL, NULL, NULL);

	MMIO_D(0x70380, D_SKL);
	MMIO_D(0x71380, D_SKL);
	MMIO_D(0x72380, D_SKL);
	MMIO_D(0x7039c, D_SKL);

	MMIO_F(0x80000, 0x3000, 0, 0, 0, D_SKL, NULL, NULL);
	MMIO_D(0x8f074, D_SKL);
	MMIO_D(0x8f004, D_SKL);
	MMIO_D(0x8f034, D_SKL);

	MMIO_D(0xb11c, D_SKL);

	MMIO_D(0x51000, D_SKL);
	MMIO_D(0x6c00c, D_SKL);

	MMIO_F(0xc800, 0x7f8, 0, 0, 0, D_SKL, NULL, NULL);
	MMIO_F(0xb020, 0x80, 0, 0, 0, D_SKL, NULL, NULL);

	MMIO_D(0xd08, D_SKL);
	MMIO_D(0x20e0, D_SKL);
	MMIO_D(0x20ec, D_SKL);

	/* TRTT */
	MMIO_D(0x4de0, D_SKL);
	MMIO_D(0x4de4, D_SKL);
	MMIO_D(0x4de8, D_SKL);
	MMIO_D(0x4dec, D_SKL);
	MMIO_D(0x4df0, D_SKL);
	MMIO_DH(0x4df4, D_SKL, NULL, gen9_trtte_write);
	MMIO_DH(0x4dfc, D_SKL, NULL, gen9_trtt_chicken_write);

	MMIO_D(0x45008, D_SKL);

	MMIO_D(0x46430, D_SKL);

	MMIO_D(0x46520, D_SKL);

	MMIO_D(0xc403c, D_SKL);
	MMIO_D(0xb004, D_SKL);
	MMIO_DH(DMA_CTRL, D_SKL_PLUS, NULL, dma_ctrl_write);

	MMIO_D(0x65900, D_SKL);
	MMIO_D(0x1082c0, D_SKL);
	MMIO_D(0x4068, D_SKL);
	MMIO_D(0x67054, D_SKL);
	MMIO_D(0x6e560, D_SKL);
	MMIO_D(0x6e554, D_SKL);
	MMIO_D(0x2b20, D_SKL);
	MMIO_D(0x65f00, D_SKL);
	MMIO_D(0x65f08, D_SKL);
	MMIO_D(0x320f0, D_SKL);

	MMIO_D(_REG_VCS2_EXCC, D_SKL);
	MMIO_D(0x70034, D_SKL);
	MMIO_D(0x71034, D_SKL);
	MMIO_D(0x72034, D_SKL);

	MMIO_D(_PLANE_KEYVAL_1(PIPE_A), D_SKL);
	MMIO_D(_PLANE_KEYVAL_1(PIPE_B), D_SKL);
	MMIO_D(_PLANE_KEYVAL_1(PIPE_C), D_SKL);
	MMIO_D(_PLANE_KEYMSK_1(PIPE_A), D_SKL);
	MMIO_D(_PLANE_KEYMSK_1(PIPE_B), D_SKL);
	MMIO_D(_PLANE_KEYMSK_1(PIPE_C), D_SKL);

	MMIO_D(0x44500, D_SKL);
	return 0;
}
/**
 * intel_gvt_find_mmio_info - find MMIO information entry by aligned offset
 * @gvt: GVT device
 * @offset: register offset
 *
 * This function is used to find the MMIO information entry from hash table
 *
 * Returns:
 * pointer to MMIO information entry, NULL if not exists
 */
struct intel_gvt_mmio_info *intel_gvt_find_mmio_info(struct intel_gvt *gvt,
	unsigned int offset)
{
	struct intel_gvt_mmio_info *e;

	WARN_ON(!IS_ALIGNED(offset, 4));

	hash_for_each_possible(gvt->mmio.mmio_info_table, e, node, offset) {
		if (e->offset == offset)
			return e;
	}
	return NULL;
}

/**
 * intel_gvt_clean_mmio_info - clean up MMIO information table for GVT device
 * @gvt: GVT device
 *
 * This function is called at the driver unloading stage, to clean up the MMIO
 * information table of GVT device
 *
 */
void intel_gvt_clean_mmio_info(struct intel_gvt *gvt)
{
	struct hlist_node *tmp;
	struct intel_gvt_mmio_info *e;
	int i;

	hash_for_each_safe(gvt->mmio.mmio_info_table, i, tmp, e, node)
		kfree(e);

	vfree(gvt->mmio.mmio_attribute);
	gvt->mmio.mmio_attribute = NULL;
}

/**
 * intel_gvt_setup_mmio_info - setup MMIO information table for GVT device
 * @gvt: GVT device
 *
 * This function is called at the initialization stage, to setup the MMIO
 * information table for GVT device
 *
 * Returns:
 * zero on success, negative if failed.
 */
int intel_gvt_setup_mmio_info(struct intel_gvt *gvt)
{
	struct intel_gvt_device_info *info = &gvt->device_info;
	struct drm_i915_private *dev_priv = gvt->dev_priv;
	int ret;

	gvt->mmio.mmio_attribute = vzalloc(info->mmio_size);
	if (!gvt->mmio.mmio_attribute)
		return -ENOMEM;

	ret = init_generic_mmio_info(gvt);
	if (ret)
		goto err;

	if (IS_BROADWELL(dev_priv)) {
		ret = init_broadwell_mmio_info(gvt);
		if (ret)
			goto err;
	} else if (IS_SKYLAKE(dev_priv)) {
		ret = init_broadwell_mmio_info(gvt);
		if (ret)
			goto err;
		ret = init_skl_mmio_info(gvt);
		if (ret)
			goto err;
	}
	return 0;
err:
	intel_gvt_clean_mmio_info(gvt);
	return ret;
}

/**
 * intel_gvt_mmio_set_accessed - mark a MMIO has been accessed
 * @gvt: a GVT device
 * @offset: register offset
 *
 */
void intel_gvt_mmio_set_accessed(struct intel_gvt *gvt, unsigned int offset)
{
	gvt->mmio.mmio_attribute[offset >> 2] |=
		F_ACCESSED;
}

/**
 * intel_gvt_mmio_is_cmd_accessed - mark a MMIO could be accessed by command
 * @gvt: a GVT device
 * @offset: register offset
 *
 */
bool intel_gvt_mmio_is_cmd_access(struct intel_gvt *gvt,
		unsigned int offset)
{
	return gvt->mmio.mmio_attribute[offset >> 2] &
		F_CMD_ACCESS;
}

/**
 * intel_gvt_mmio_is_unalign - mark a MMIO could be accessed unaligned
 * @gvt: a GVT device
 * @offset: register offset
 *
 */
bool intel_gvt_mmio_is_unalign(struct intel_gvt *gvt,
		unsigned int offset)
{
	return gvt->mmio.mmio_attribute[offset >> 2] &
		F_UNALIGN;
}

/**
 * intel_gvt_mmio_set_cmd_accessed - mark a MMIO has been accessed by command
 * @gvt: a GVT device
 * @offset: register offset
 *
 */
void intel_gvt_mmio_set_cmd_accessed(struct intel_gvt *gvt,
		unsigned int offset)
{
	gvt->mmio.mmio_attribute[offset >> 2] |=
		F_CMD_ACCESSED;
}

/**
 * intel_gvt_mmio_has_mode_mask - if a MMIO has a mode mask
 * @gvt: a GVT device
 * @offset: register offset
 *
 * Returns:
 * True if a MMIO has a mode mask in its higher 16 bits, false if it isn't.
 *
 */
bool intel_gvt_mmio_has_mode_mask(struct intel_gvt *gvt, unsigned int offset)
{
	return gvt->mmio.mmio_attribute[offset >> 2] &
		F_MODE_MASK;
}

/**
 * intel_vgpu_default_mmio_read - default MMIO read handler
 * @vgpu: a vGPU
 * @offset: access offset
 * @p_data: data return buffer
 * @bytes: access data length
 *
 * Returns:
 * Zero on success, negative error code if failed.
 */
int intel_vgpu_default_mmio_read(struct intel_vgpu *vgpu, unsigned int offset,
		void *p_data, unsigned int bytes)
{
	read_vreg(vgpu, offset, p_data, bytes);
	return 0;
}

/**
 * intel_t_default_mmio_write - default MMIO write handler
 * @vgpu: a vGPU
 * @offset: access offset
 * @p_data: write data buffer
 * @bytes: access data length
 *
 * Returns:
 * Zero on success, negative error code if failed.
 */
int intel_vgpu_default_mmio_write(struct intel_vgpu *vgpu, unsigned int offset,
		void *p_data, unsigned int bytes)
{
	write_vreg(vgpu, offset, p_data, bytes);
	return 0;
}
