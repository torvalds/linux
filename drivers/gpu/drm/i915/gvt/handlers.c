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
#include "gvt.h"
#include "i915_pvinfo.h"

/* XXX FIXME i915 has changed PP_XXX definition */
#define PCH_PP_STATUS  _MMIO(0xc7200)
#define PCH_PP_CONTROL _MMIO(0xc7204)
#define PCH_PP_ON_DELAYS _MMIO(0xc7208)
#define PCH_PP_OFF_DELAYS _MMIO(0xc720c)
#define PCH_PP_DIVISOR _MMIO(0xc7210)

unsigned long intel_gvt_get_device_type(struct intel_gvt *gvt)
{
	if (IS_BROADWELL(gvt->dev_priv))
		return D_BDW;
	else if (IS_SKYLAKE(gvt->dev_priv))
		return D_SKL;
	else if (IS_KABYLAKE(gvt->dev_priv))
		return D_KBL;

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

static struct intel_gvt_mmio_info *find_mmio_info(struct intel_gvt *gvt,
						  unsigned int offset)
{
	struct intel_gvt_mmio_info *e;

	hash_for_each_possible(gvt->mmio.mmio_info_table, e, node, offset) {
		if (e->offset == offset)
			return e;
	}
	return NULL;
}

static int new_mmio_info(struct intel_gvt *gvt,
		u32 offset, u8 flags, u32 size,
		u32 addr_mask, u32 ro_mask, u32 device,
		gvt_mmio_func read, gvt_mmio_func write)
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
		p = find_mmio_info(gvt, info->offset);
		if (p) {
			WARN(1, "dup mmio definition offset %x\n",
				info->offset);
			kfree(info);

			/* We return -EEXIST here to make GVT-g load fail.
			 * So duplicated MMIO can be found as soon as
			 * possible.
			 */
			return -EEXIST;
		}

		info->ro_mask = ro_mask;
		info->device = device;
		info->read = read ? read : intel_vgpu_default_mmio_read;
		info->write = write ? write : intel_vgpu_default_mmio_write;
		gvt->mmio.mmio_attribute[info->offset / 4] = flags;
		INIT_HLIST_NODE(&info->node);
		hash_add(gvt->mmio.mmio_info_table, &info->node, info->offset);
		gvt->mmio.num_tracked_mmio++;
	}
	return 0;
}

/**
 * intel_gvt_render_mmio_to_ring_id - convert a mmio offset into ring id
 * @gvt: a GVT device
 * @offset: register offset
 *
 * Returns:
 * Ring ID on success, negative error code if failed.
 */
int intel_gvt_render_mmio_to_ring_id(struct intel_gvt *gvt,
		unsigned int offset)
{
	enum intel_engine_id id;
	struct intel_engine_cs *engine;

	offset &= ~GENMASK(11, 0);
	for_each_engine(engine, gvt->dev_priv, id) {
		if (engine->mmio_base == offset)
			return id;
	}
	return -ENODEV;
}

#define offset_to_fence_num(offset) \
	((offset - i915_mmio_reg_offset(FENCE_REG_GEN6_LO(0))) >> 3)

#define fence_num_to_offset(num) \
	(num * 8 + i915_mmio_reg_offset(FENCE_REG_GEN6_LO(0)))


void enter_failsafe_mode(struct intel_vgpu *vgpu, int reason)
{
	switch (reason) {
	case GVT_FAILSAFE_UNSUPPORTED_GUEST:
		pr_err("Detected your guest driver doesn't support GVT-g.\n");
		break;
	case GVT_FAILSAFE_INSUFFICIENT_RESOURCE:
		pr_err("Graphics resource is not enough for the guest\n");
	case GVT_FAILSAFE_GUEST_ERR:
		pr_err("GVT Internal error  for the guest\n");
	default:
		break;
	}
	pr_err("Now vgpu %d will enter failsafe mode.\n", vgpu->id);
	vgpu->failsafe = true;
}

static int sanitize_fence_mmio_access(struct intel_vgpu *vgpu,
		unsigned int fence_num, void *p_data, unsigned int bytes)
{
	if (fence_num >= vgpu_fence_sz(vgpu)) {

		/* When guest access oob fence regs without access
		 * pv_info first, we treat guest not supporting GVT,
		 * and we will let vgpu enter failsafe mode.
		 */
		if (!vgpu->pv_notified)
			enter_failsafe_mode(vgpu,
					GVT_FAILSAFE_UNSUPPORTED_GUEST);

		if (!vgpu->mmio.disable_warn_untrack) {
			gvt_vgpu_err("found oob fence register access\n");
			gvt_vgpu_err("total fence %d, access fence %d\n",
					vgpu_fence_sz(vgpu), fence_num);
		}
		memset(p_data, 0, bytes);
		return -EINVAL;
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
	struct drm_i915_private *dev_priv = vgpu->gvt->dev_priv;
	unsigned int fence_num = offset_to_fence_num(off);
	int ret;

	ret = sanitize_fence_mmio_access(vgpu, fence_num, p_data, bytes);
	if (ret)
		return ret;
	write_vreg(vgpu, off, p_data, bytes);

	mmio_hw_access_pre(dev_priv);
	intel_vgpu_write_fence(vgpu, fence_num,
			vgpu_vreg64(vgpu, fence_num_to_offset(fence_num)));
	mmio_hw_access_post(dev_priv);
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

	if (IS_SKYLAKE(vgpu->gvt->dev_priv)
		|| IS_KABYLAKE(vgpu->gvt->dev_priv)) {
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
			gvt_vgpu_err("invalid forcewake offset 0x%x\n", offset);
			return -EINVAL;
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
	unsigned int engine_mask = 0;
	u32 data;

	write_vreg(vgpu, offset, p_data, bytes);
	data = vgpu_vreg(vgpu, offset);

	if (data & GEN6_GRDOM_FULL) {
		gvt_dbg_mmio("vgpu%d: request full GPU reset\n", vgpu->id);
		engine_mask = ALL_ENGINES;
	} else {
		if (data & GEN6_GRDOM_RENDER) {
			gvt_dbg_mmio("vgpu%d: request RCS reset\n", vgpu->id);
			engine_mask |= (1 << RCS);
		}
		if (data & GEN6_GRDOM_MEDIA) {
			gvt_dbg_mmio("vgpu%d: request VCS reset\n", vgpu->id);
			engine_mask |= (1 << VCS);
		}
		if (data & GEN6_GRDOM_BLT) {
			gvt_dbg_mmio("vgpu%d: request BCS Reset\n", vgpu->id);
			engine_mask |= (1 << BCS);
		}
		if (data & GEN6_GRDOM_VECS) {
			gvt_dbg_mmio("vgpu%d: request VECS Reset\n", vgpu->id);
			engine_mask |= (1 << VECS);
		}
		if (data & GEN8_GRDOM_MEDIA2) {
			gvt_dbg_mmio("vgpu%d: request VCS2 Reset\n", vgpu->id);
			if (HAS_BSD2(vgpu->gvt->dev_priv))
				engine_mask |= (1 << VCS2);
		}
	}

	intel_gvt_reset_vgpu_locked(vgpu, false, engine_mask);

	/* sw will wait for the device to ack the reset request */
	 vgpu_vreg(vgpu, offset) = 0;

	return 0;
}

static int gmbus_mmio_read(struct intel_vgpu *vgpu, unsigned int offset,
		void *p_data, unsigned int bytes)
{
	return intel_gvt_i2c_handle_gmbus_read(vgpu, offset, p_data, bytes);
}

static int gmbus_mmio_write(struct intel_vgpu *vgpu, unsigned int offset,
		void *p_data, unsigned int bytes)
{
	return intel_gvt_i2c_handle_gmbus_write(vgpu, offset, p_data, bytes);
}

static int pch_pp_control_mmio_write(struct intel_vgpu *vgpu,
		unsigned int offset, void *p_data, unsigned int bytes)
{
	write_vreg(vgpu, offset, p_data, bytes);

	if (vgpu_vreg(vgpu, offset) & PANEL_POWER_ON) {
		vgpu_vreg(vgpu, PCH_PP_STATUS) |= PP_ON;
		vgpu_vreg(vgpu, PCH_PP_STATUS) |= PP_SEQUENCE_STATE_ON_IDLE;
		vgpu_vreg(vgpu, PCH_PP_STATUS) &= ~PP_SEQUENCE_POWER_DOWN;
		vgpu_vreg(vgpu, PCH_PP_STATUS) &= ~PP_CYCLE_DELAY_ACTIVE;

	} else
		vgpu_vreg(vgpu, PCH_PP_STATUS) &=
			~(PP_ON | PP_SEQUENCE_POWER_DOWN
					| PP_CYCLE_DELAY_ACTIVE);
	return 0;
}

static int transconf_mmio_write(struct intel_vgpu *vgpu,
		unsigned int offset, void *p_data, unsigned int bytes)
{
	write_vreg(vgpu, offset, p_data, bytes);

	if (vgpu_vreg(vgpu, offset) & TRANS_ENABLE)
		vgpu_vreg(vgpu, offset) |= TRANS_STATE_ENABLE;
	else
		vgpu_vreg(vgpu, offset) &= ~TRANS_STATE_ENABLE;
	return 0;
}

static int lcpll_ctl_mmio_write(struct intel_vgpu *vgpu, unsigned int offset,
		void *p_data, unsigned int bytes)
{
	write_vreg(vgpu, offset, p_data, bytes);

	if (vgpu_vreg(vgpu, offset) & LCPLL_PLL_DISABLE)
		vgpu_vreg(vgpu, offset) &= ~LCPLL_PLL_LOCK;
	else
		vgpu_vreg(vgpu, offset) |= LCPLL_PLL_LOCK;

	if (vgpu_vreg(vgpu, offset) & LCPLL_CD_SOURCE_FCLK)
		vgpu_vreg(vgpu, offset) |= LCPLL_CD_SOURCE_FCLK_DONE;
	else
		vgpu_vreg(vgpu, offset) &= ~LCPLL_CD_SOURCE_FCLK_DONE;

	return 0;
}

static int dpy_reg_mmio_read(struct intel_vgpu *vgpu, unsigned int offset,
		void *p_data, unsigned int bytes)
{
	switch (offset) {
	case 0xe651c:
	case 0xe661c:
	case 0xe671c:
	case 0xe681c:
		vgpu_vreg(vgpu, offset) = 1 << 17;
		break;
	case 0xe6c04:
		vgpu_vreg(vgpu, offset) = 0x3;
		break;
	case 0xe6e1c:
		vgpu_vreg(vgpu, offset) = 0x2f << 16;
		break;
	default:
		return -EINVAL;
	}

	read_vreg(vgpu, offset, p_data, bytes);
	return 0;
}

static int pipeconf_mmio_write(struct intel_vgpu *vgpu, unsigned int offset,
		void *p_data, unsigned int bytes)
{
	u32 data;

	write_vreg(vgpu, offset, p_data, bytes);
	data = vgpu_vreg(vgpu, offset);

	if (data & PIPECONF_ENABLE)
		vgpu_vreg(vgpu, offset) |= I965_PIPECONF_ACTIVE;
	else
		vgpu_vreg(vgpu, offset) &= ~I965_PIPECONF_ACTIVE;
	intel_gvt_check_vblank_emulation(vgpu->gvt);
	return 0;
}

/* ascendingly sorted */
static i915_reg_t force_nonpriv_white_list[] = {
	GEN9_CS_DEBUG_MODE1, //_MMIO(0x20ec)
	GEN9_CTX_PREEMPT_REG,//_MMIO(0x2248)
	GEN8_CS_CHICKEN1,//_MMIO(0x2580)
	_MMIO(0x2690),
	_MMIO(0x2694),
	_MMIO(0x2698),
	_MMIO(0x4de0),
	_MMIO(0x4de4),
	_MMIO(0x4dfc),
	GEN7_COMMON_SLICE_CHICKEN1,//_MMIO(0x7010)
	_MMIO(0x7014),
	HDC_CHICKEN0,//_MMIO(0x7300)
	GEN8_HDC_CHICKEN1,//_MMIO(0x7304)
	_MMIO(0x7700),
	_MMIO(0x7704),
	_MMIO(0x7708),
	_MMIO(0x770c),
	_MMIO(0xb110),
	GEN8_L3SQCREG4,//_MMIO(0xb118)
	_MMIO(0xe100),
	_MMIO(0xe18c),
	_MMIO(0xe48c),
	_MMIO(0xe5f4),
};

/* a simple bsearch */
static inline bool in_whitelist(unsigned int reg)
{
	int left = 0, right = ARRAY_SIZE(force_nonpriv_white_list);
	i915_reg_t *array = force_nonpriv_white_list;

	while (left < right) {
		int mid = (left + right)/2;

		if (reg > array[mid].reg)
			left = mid + 1;
		else if (reg < array[mid].reg)
			right = mid;
		else
			return true;
	}
	return false;
}

static int force_nonpriv_write(struct intel_vgpu *vgpu,
	unsigned int offset, void *p_data, unsigned int bytes)
{
	u32 reg_nonpriv = *(u32 *)p_data;
	int ret = -EINVAL;

	if ((bytes != 4) || ((offset & (bytes - 1)) != 0)) {
		gvt_err("vgpu(%d) Invalid FORCE_NONPRIV offset %x(%dB)\n",
			vgpu->id, offset, bytes);
		return ret;
	}

	if (in_whitelist(reg_nonpriv)) {
		ret = intel_vgpu_default_mmio_write(vgpu, offset, p_data,
			bytes);
	} else {
		gvt_err("vgpu(%d) Invalid FORCE_NONPRIV write %x\n",
			vgpu->id, reg_nonpriv);
	}
	return ret;
}

static int ddi_buf_ctl_mmio_write(struct intel_vgpu *vgpu, unsigned int offset,
		void *p_data, unsigned int bytes)
{
	write_vreg(vgpu, offset, p_data, bytes);

	if (vgpu_vreg(vgpu, offset) & DDI_BUF_CTL_ENABLE) {
		vgpu_vreg(vgpu, offset) &= ~DDI_BUF_IS_IDLE;
	} else {
		vgpu_vreg(vgpu, offset) |= DDI_BUF_IS_IDLE;
		if (offset == i915_mmio_reg_offset(DDI_BUF_CTL(PORT_E)))
			vgpu_vreg(vgpu, DP_TP_STATUS(PORT_E))
				&= ~DP_TP_STATUS_AUTOTRAIN_DONE;
	}
	return 0;
}

static int fdi_rx_iir_mmio_write(struct intel_vgpu *vgpu,
		unsigned int offset, void *p_data, unsigned int bytes)
{
	vgpu_vreg(vgpu, offset) &= ~*(u32 *)p_data;
	return 0;
}

#define FDI_LINK_TRAIN_PATTERN1         0
#define FDI_LINK_TRAIN_PATTERN2         1

static int fdi_auto_training_started(struct intel_vgpu *vgpu)
{
	u32 ddi_buf_ctl = vgpu_vreg(vgpu, DDI_BUF_CTL(PORT_E));
	u32 rx_ctl = vgpu_vreg(vgpu, _FDI_RXA_CTL);
	u32 tx_ctl = vgpu_vreg(vgpu, DP_TP_CTL(PORT_E));

	if ((ddi_buf_ctl & DDI_BUF_CTL_ENABLE) &&
			(rx_ctl & FDI_RX_ENABLE) &&
			(rx_ctl & FDI_AUTO_TRAINING) &&
			(tx_ctl & DP_TP_CTL_ENABLE) &&
			(tx_ctl & DP_TP_CTL_FDI_AUTOTRAIN))
		return 1;
	else
		return 0;
}

static int check_fdi_rx_train_status(struct intel_vgpu *vgpu,
		enum pipe pipe, unsigned int train_pattern)
{
	i915_reg_t fdi_rx_imr, fdi_tx_ctl, fdi_rx_ctl;
	unsigned int fdi_rx_check_bits, fdi_tx_check_bits;
	unsigned int fdi_rx_train_bits, fdi_tx_train_bits;
	unsigned int fdi_iir_check_bits;

	fdi_rx_imr = FDI_RX_IMR(pipe);
	fdi_tx_ctl = FDI_TX_CTL(pipe);
	fdi_rx_ctl = FDI_RX_CTL(pipe);

	if (train_pattern == FDI_LINK_TRAIN_PATTERN1) {
		fdi_rx_train_bits = FDI_LINK_TRAIN_PATTERN_1_CPT;
		fdi_tx_train_bits = FDI_LINK_TRAIN_PATTERN_1;
		fdi_iir_check_bits = FDI_RX_BIT_LOCK;
	} else if (train_pattern == FDI_LINK_TRAIN_PATTERN2) {
		fdi_rx_train_bits = FDI_LINK_TRAIN_PATTERN_2_CPT;
		fdi_tx_train_bits = FDI_LINK_TRAIN_PATTERN_2;
		fdi_iir_check_bits = FDI_RX_SYMBOL_LOCK;
	} else {
		gvt_vgpu_err("Invalid train pattern %d\n", train_pattern);
		return -EINVAL;
	}

	fdi_rx_check_bits = FDI_RX_ENABLE | fdi_rx_train_bits;
	fdi_tx_check_bits = FDI_TX_ENABLE | fdi_tx_train_bits;

	/* If imr bit has been masked */
	if (vgpu_vreg(vgpu, fdi_rx_imr) & fdi_iir_check_bits)
		return 0;

	if (((vgpu_vreg(vgpu, fdi_tx_ctl) & fdi_tx_check_bits)
			== fdi_tx_check_bits)
		&& ((vgpu_vreg(vgpu, fdi_rx_ctl) & fdi_rx_check_bits)
			== fdi_rx_check_bits))
		return 1;
	else
		return 0;
}

#define INVALID_INDEX (~0U)

static unsigned int calc_index(unsigned int offset, unsigned int start,
	unsigned int next, unsigned int end, i915_reg_t i915_end)
{
	unsigned int range = next - start;

	if (!end)
		end = i915_mmio_reg_offset(i915_end);
	if (offset < start || offset > end)
		return INVALID_INDEX;
	offset -= start;
	return offset / range;
}

#define FDI_RX_CTL_TO_PIPE(offset) \
	calc_index(offset, _FDI_RXA_CTL, _FDI_RXB_CTL, 0, FDI_RX_CTL(PIPE_C))

#define FDI_TX_CTL_TO_PIPE(offset) \
	calc_index(offset, _FDI_TXA_CTL, _FDI_TXB_CTL, 0, FDI_TX_CTL(PIPE_C))

#define FDI_RX_IMR_TO_PIPE(offset) \
	calc_index(offset, _FDI_RXA_IMR, _FDI_RXB_IMR, 0, FDI_RX_IMR(PIPE_C))

static int update_fdi_rx_iir_status(struct intel_vgpu *vgpu,
		unsigned int offset, void *p_data, unsigned int bytes)
{
	i915_reg_t fdi_rx_iir;
	unsigned int index;
	int ret;

	if (FDI_RX_CTL_TO_PIPE(offset) != INVALID_INDEX)
		index = FDI_RX_CTL_TO_PIPE(offset);
	else if (FDI_TX_CTL_TO_PIPE(offset) != INVALID_INDEX)
		index = FDI_TX_CTL_TO_PIPE(offset);
	else if (FDI_RX_IMR_TO_PIPE(offset) != INVALID_INDEX)
		index = FDI_RX_IMR_TO_PIPE(offset);
	else {
		gvt_vgpu_err("Unsupport registers %x\n", offset);
		return -EINVAL;
	}

	write_vreg(vgpu, offset, p_data, bytes);

	fdi_rx_iir = FDI_RX_IIR(index);

	ret = check_fdi_rx_train_status(vgpu, index, FDI_LINK_TRAIN_PATTERN1);
	if (ret < 0)
		return ret;
	if (ret)
		vgpu_vreg(vgpu, fdi_rx_iir) |= FDI_RX_BIT_LOCK;

	ret = check_fdi_rx_train_status(vgpu, index, FDI_LINK_TRAIN_PATTERN2);
	if (ret < 0)
		return ret;
	if (ret)
		vgpu_vreg(vgpu, fdi_rx_iir) |= FDI_RX_SYMBOL_LOCK;

	if (offset == _FDI_RXA_CTL)
		if (fdi_auto_training_started(vgpu))
			vgpu_vreg(vgpu, DP_TP_STATUS(PORT_E)) |=
				DP_TP_STATUS_AUTOTRAIN_DONE;
	return 0;
}

#define DP_TP_CTL_TO_PORT(offset) \
	calc_index(offset, _DP_TP_CTL_A, _DP_TP_CTL_B, 0, DP_TP_CTL(PORT_E))

static int dp_tp_ctl_mmio_write(struct intel_vgpu *vgpu, unsigned int offset,
		void *p_data, unsigned int bytes)
{
	i915_reg_t status_reg;
	unsigned int index;
	u32 data;

	write_vreg(vgpu, offset, p_data, bytes);

	index = DP_TP_CTL_TO_PORT(offset);
	data = (vgpu_vreg(vgpu, offset) & GENMASK(10, 8)) >> 8;
	if (data == 0x2) {
		status_reg = DP_TP_STATUS(index);
		vgpu_vreg(vgpu, status_reg) |= (1 << 25);
	}
	return 0;
}

static int dp_tp_status_mmio_write(struct intel_vgpu *vgpu,
		unsigned int offset, void *p_data, unsigned int bytes)
{
	u32 reg_val;
	u32 sticky_mask;

	reg_val = *((u32 *)p_data);
	sticky_mask = GENMASK(27, 26) | (1 << 24);

	vgpu_vreg(vgpu, offset) = (reg_val & ~sticky_mask) |
		(vgpu_vreg(vgpu, offset) & sticky_mask);
	vgpu_vreg(vgpu, offset) &= ~(reg_val & sticky_mask);
	return 0;
}

static int pch_adpa_mmio_write(struct intel_vgpu *vgpu,
		unsigned int offset, void *p_data, unsigned int bytes)
{
	u32 data;

	write_vreg(vgpu, offset, p_data, bytes);
	data = vgpu_vreg(vgpu, offset);

	if (data & ADPA_CRT_HOTPLUG_FORCE_TRIGGER)
		vgpu_vreg(vgpu, offset) &= ~ADPA_CRT_HOTPLUG_FORCE_TRIGGER;
	return 0;
}

static int south_chicken2_mmio_write(struct intel_vgpu *vgpu,
		unsigned int offset, void *p_data, unsigned int bytes)
{
	u32 data;

	write_vreg(vgpu, offset, p_data, bytes);
	data = vgpu_vreg(vgpu, offset);

	if (data & FDI_MPHY_IOSFSB_RESET_CTL)
		vgpu_vreg(vgpu, offset) |= FDI_MPHY_IOSFSB_RESET_STATUS;
	else
		vgpu_vreg(vgpu, offset) &= ~FDI_MPHY_IOSFSB_RESET_STATUS;
	return 0;
}

#define DSPSURF_TO_PIPE(offset) \
	calc_index(offset, _DSPASURF, _DSPBSURF, 0, DSPSURF(PIPE_C))

static int pri_surf_mmio_write(struct intel_vgpu *vgpu, unsigned int offset,
		void *p_data, unsigned int bytes)
{
	struct drm_i915_private *dev_priv = vgpu->gvt->dev_priv;
	unsigned int index = DSPSURF_TO_PIPE(offset);
	i915_reg_t surflive_reg = DSPSURFLIVE(index);
	int flip_event[] = {
		[PIPE_A] = PRIMARY_A_FLIP_DONE,
		[PIPE_B] = PRIMARY_B_FLIP_DONE,
		[PIPE_C] = PRIMARY_C_FLIP_DONE,
	};

	write_vreg(vgpu, offset, p_data, bytes);
	vgpu_vreg(vgpu, surflive_reg) = vgpu_vreg(vgpu, offset);

	set_bit(flip_event[index], vgpu->irq.flip_done_event[index]);
	return 0;
}

#define SPRSURF_TO_PIPE(offset) \
	calc_index(offset, _SPRA_SURF, _SPRB_SURF, 0, SPRSURF(PIPE_C))

static int spr_surf_mmio_write(struct intel_vgpu *vgpu, unsigned int offset,
		void *p_data, unsigned int bytes)
{
	unsigned int index = SPRSURF_TO_PIPE(offset);
	i915_reg_t surflive_reg = SPRSURFLIVE(index);
	int flip_event[] = {
		[PIPE_A] = SPRITE_A_FLIP_DONE,
		[PIPE_B] = SPRITE_B_FLIP_DONE,
		[PIPE_C] = SPRITE_C_FLIP_DONE,
	};

	write_vreg(vgpu, offset, p_data, bytes);
	vgpu_vreg(vgpu, surflive_reg) = vgpu_vreg(vgpu, offset);

	set_bit(flip_event[index], vgpu->irq.flip_done_event[index]);
	return 0;
}

static int trigger_aux_channel_interrupt(struct intel_vgpu *vgpu,
		unsigned int reg)
{
	struct drm_i915_private *dev_priv = vgpu->gvt->dev_priv;
	enum intel_gvt_event_type event;

	if (reg == _DPA_AUX_CH_CTL)
		event = AUX_CHANNEL_A;
	else if (reg == _PCH_DPB_AUX_CH_CTL || reg == _DPB_AUX_CH_CTL)
		event = AUX_CHANNEL_B;
	else if (reg == _PCH_DPC_AUX_CH_CTL || reg == _DPC_AUX_CH_CTL)
		event = AUX_CHANNEL_C;
	else if (reg == _PCH_DPD_AUX_CH_CTL || reg == _DPD_AUX_CH_CTL)
		event = AUX_CHANNEL_D;
	else {
		WARN_ON(true);
		return -EINVAL;
	}

	intel_vgpu_trigger_virtual_event(vgpu, event);
	return 0;
}

static int dp_aux_ch_ctl_trans_done(struct intel_vgpu *vgpu, u32 value,
		unsigned int reg, int len, bool data_valid)
{
	/* mark transaction done */
	value |= DP_AUX_CH_CTL_DONE;
	value &= ~DP_AUX_CH_CTL_SEND_BUSY;
	value &= ~DP_AUX_CH_CTL_RECEIVE_ERROR;

	if (data_valid)
		value &= ~DP_AUX_CH_CTL_TIME_OUT_ERROR;
	else
		value |= DP_AUX_CH_CTL_TIME_OUT_ERROR;

	/* message size */
	value &= ~(0xf << 20);
	value |= (len << 20);
	vgpu_vreg(vgpu, reg) = value;

	if (value & DP_AUX_CH_CTL_INTERRUPT)
		return trigger_aux_channel_interrupt(vgpu, reg);
	return 0;
}

static void dp_aux_ch_ctl_link_training(struct intel_vgpu_dpcd_data *dpcd,
		uint8_t t)
{
	if ((t & DPCD_TRAINING_PATTERN_SET_MASK) == DPCD_TRAINING_PATTERN_1) {
		/* training pattern 1 for CR */
		/* set LANE0_CR_DONE, LANE1_CR_DONE */
		dpcd->data[DPCD_LANE0_1_STATUS] |= DPCD_LANES_CR_DONE;
		/* set LANE2_CR_DONE, LANE3_CR_DONE */
		dpcd->data[DPCD_LANE2_3_STATUS] |= DPCD_LANES_CR_DONE;
	} else if ((t & DPCD_TRAINING_PATTERN_SET_MASK) ==
			DPCD_TRAINING_PATTERN_2) {
		/* training pattern 2 for EQ */
		/* Set CHANNEL_EQ_DONE and  SYMBOL_LOCKED for Lane0_1 */
		dpcd->data[DPCD_LANE0_1_STATUS] |= DPCD_LANES_EQ_DONE;
		dpcd->data[DPCD_LANE0_1_STATUS] |= DPCD_SYMBOL_LOCKED;
		/* Set CHANNEL_EQ_DONE and  SYMBOL_LOCKED for Lane2_3 */
		dpcd->data[DPCD_LANE2_3_STATUS] |= DPCD_LANES_EQ_DONE;
		dpcd->data[DPCD_LANE2_3_STATUS] |= DPCD_SYMBOL_LOCKED;
		/* set INTERLANE_ALIGN_DONE */
		dpcd->data[DPCD_LANE_ALIGN_STATUS_UPDATED] |=
			DPCD_INTERLANE_ALIGN_DONE;
	} else if ((t & DPCD_TRAINING_PATTERN_SET_MASK) ==
			DPCD_LINK_TRAINING_DISABLED) {
		/* finish link training */
		/* set sink status as synchronized */
		dpcd->data[DPCD_SINK_STATUS] = DPCD_SINK_IN_SYNC;
	}
}

#define _REG_HSW_DP_AUX_CH_CTL(dp) \
	((dp) ? (_PCH_DPB_AUX_CH_CTL + ((dp)-1)*0x100) : 0x64010)

#define _REG_SKL_DP_AUX_CH_CTL(dp) (0x64010 + (dp) * 0x100)

#define OFFSET_TO_DP_AUX_PORT(offset) (((offset) & 0xF00) >> 8)

#define dpy_is_valid_port(port)	\
		(((port) >= PORT_A) && ((port) < I915_MAX_PORTS))

static int dp_aux_ch_ctl_mmio_write(struct intel_vgpu *vgpu,
		unsigned int offset, void *p_data, unsigned int bytes)
{
	struct intel_vgpu_display *display = &vgpu->display;
	int msg, addr, ctrl, op, len;
	int port_index = OFFSET_TO_DP_AUX_PORT(offset);
	struct intel_vgpu_dpcd_data *dpcd = NULL;
	struct intel_vgpu_port *port = NULL;
	u32 data;

	if (!dpy_is_valid_port(port_index)) {
		gvt_vgpu_err("Unsupported DP port access!\n");
		return 0;
	}

	write_vreg(vgpu, offset, p_data, bytes);
	data = vgpu_vreg(vgpu, offset);

	if ((IS_SKYLAKE(vgpu->gvt->dev_priv)
		|| IS_KABYLAKE(vgpu->gvt->dev_priv))
		&& offset != _REG_SKL_DP_AUX_CH_CTL(port_index)) {
		/* SKL DPB/C/D aux ctl register changed */
		return 0;
	} else if (IS_BROADWELL(vgpu->gvt->dev_priv) &&
		   offset != _REG_HSW_DP_AUX_CH_CTL(port_index)) {
		/* write to the data registers */
		return 0;
	}

	if (!(data & DP_AUX_CH_CTL_SEND_BUSY)) {
		/* just want to clear the sticky bits */
		vgpu_vreg(vgpu, offset) = 0;
		return 0;
	}

	port = &display->ports[port_index];
	dpcd = port->dpcd;

	/* read out message from DATA1 register */
	msg = vgpu_vreg(vgpu, offset + 4);
	addr = (msg >> 8) & 0xffff;
	ctrl = (msg >> 24) & 0xff;
	len = msg & 0xff;
	op = ctrl >> 4;

	if (op == GVT_AUX_NATIVE_WRITE) {
		int t;
		uint8_t buf[16];

		if ((addr + len + 1) >= DPCD_SIZE) {
			/*
			 * Write request exceeds what we supported,
			 * DCPD spec: When a Source Device is writing a DPCD
			 * address not supported by the Sink Device, the Sink
			 * Device shall reply with AUX NACK and “M” equal to
			 * zero.
			 */

			/* NAK the write */
			vgpu_vreg(vgpu, offset + 4) = AUX_NATIVE_REPLY_NAK;
			dp_aux_ch_ctl_trans_done(vgpu, data, offset, 2, true);
			return 0;
		}

		/*
		 * Write request format: (command + address) occupies
		 * 3 bytes, followed by (len + 1) bytes of data.
		 */
		if (WARN_ON((len + 4) > AUX_BURST_SIZE))
			return -EINVAL;

		/* unpack data from vreg to buf */
		for (t = 0; t < 4; t++) {
			u32 r = vgpu_vreg(vgpu, offset + 8 + t * 4);

			buf[t * 4] = (r >> 24) & 0xff;
			buf[t * 4 + 1] = (r >> 16) & 0xff;
			buf[t * 4 + 2] = (r >> 8) & 0xff;
			buf[t * 4 + 3] = r & 0xff;
		}

		/* write to virtual DPCD */
		if (dpcd && dpcd->data_valid) {
			for (t = 0; t <= len; t++) {
				int p = addr + t;

				dpcd->data[p] = buf[t];
				/* check for link training */
				if (p == DPCD_TRAINING_PATTERN_SET)
					dp_aux_ch_ctl_link_training(dpcd,
							buf[t]);
			}
		}

		/* ACK the write */
		vgpu_vreg(vgpu, offset + 4) = 0;
		dp_aux_ch_ctl_trans_done(vgpu, data, offset, 1,
				dpcd && dpcd->data_valid);
		return 0;
	}

	if (op == GVT_AUX_NATIVE_READ) {
		int idx, i, ret = 0;

		if ((addr + len + 1) >= DPCD_SIZE) {
			/*
			 * read request exceeds what we supported
			 * DPCD spec: A Sink Device receiving a Native AUX CH
			 * read request for an unsupported DPCD address must
			 * reply with an AUX ACK and read data set equal to
			 * zero instead of replying with AUX NACK.
			 */

			/* ACK the READ*/
			vgpu_vreg(vgpu, offset + 4) = 0;
			vgpu_vreg(vgpu, offset + 8) = 0;
			vgpu_vreg(vgpu, offset + 12) = 0;
			vgpu_vreg(vgpu, offset + 16) = 0;
			vgpu_vreg(vgpu, offset + 20) = 0;

			dp_aux_ch_ctl_trans_done(vgpu, data, offset, len + 2,
					true);
			return 0;
		}

		for (idx = 1; idx <= 5; idx++) {
			/* clear the data registers */
			vgpu_vreg(vgpu, offset + 4 * idx) = 0;
		}

		/*
		 * Read reply format: ACK (1 byte) plus (len + 1) bytes of data.
		 */
		if (WARN_ON((len + 2) > AUX_BURST_SIZE))
			return -EINVAL;

		/* read from virtual DPCD to vreg */
		/* first 4 bytes: [ACK][addr][addr+1][addr+2] */
		if (dpcd && dpcd->data_valid) {
			for (i = 1; i <= (len + 1); i++) {
				int t;

				t = dpcd->data[addr + i - 1];
				t <<= (24 - 8 * (i % 4));
				ret |= t;

				if ((i % 4 == 3) || (i == (len + 1))) {
					vgpu_vreg(vgpu, offset +
							(i / 4 + 1) * 4) = ret;
					ret = 0;
				}
			}
		}
		dp_aux_ch_ctl_trans_done(vgpu, data, offset, len + 2,
				dpcd && dpcd->data_valid);
		return 0;
	}

	/* i2c transaction starts */
	intel_gvt_i2c_handle_aux_ch_write(vgpu, port_index, offset, p_data);

	if (data & DP_AUX_CH_CTL_INTERRUPT)
		trigger_aux_channel_interrupt(vgpu, offset);
	return 0;
}

static int mbctl_write(struct intel_vgpu *vgpu, unsigned int offset,
		void *p_data, unsigned int bytes)
{
	*(u32 *)p_data &= (~GEN6_MBCTL_ENABLE_BOOT_FETCH);
	write_vreg(vgpu, offset, p_data, bytes);
	return 0;
}

static int vga_control_mmio_write(struct intel_vgpu *vgpu, unsigned int offset,
		void *p_data, unsigned int bytes)
{
	bool vga_disable;

	write_vreg(vgpu, offset, p_data, bytes);
	vga_disable = vgpu_vreg(vgpu, offset) & VGA_DISP_DISABLE;

	gvt_dbg_core("vgpu%d: %s VGA mode\n", vgpu->id,
			vga_disable ? "Disable" : "Enable");
	return 0;
}

static u32 read_virtual_sbi_register(struct intel_vgpu *vgpu,
		unsigned int sbi_offset)
{
	struct intel_vgpu_display *display = &vgpu->display;
	int num = display->sbi.number;
	int i;

	for (i = 0; i < num; ++i)
		if (display->sbi.registers[i].offset == sbi_offset)
			break;

	if (i == num)
		return 0;

	return display->sbi.registers[i].value;
}

static void write_virtual_sbi_register(struct intel_vgpu *vgpu,
		unsigned int offset, u32 value)
{
	struct intel_vgpu_display *display = &vgpu->display;
	int num = display->sbi.number;
	int i;

	for (i = 0; i < num; ++i) {
		if (display->sbi.registers[i].offset == offset)
			break;
	}

	if (i == num) {
		if (num == SBI_REG_MAX) {
			gvt_vgpu_err("SBI caching meets maximum limits\n");
			return;
		}
		display->sbi.number++;
	}

	display->sbi.registers[i].offset = offset;
	display->sbi.registers[i].value = value;
}

static int sbi_data_mmio_read(struct intel_vgpu *vgpu, unsigned int offset,
		void *p_data, unsigned int bytes)
{
	if (((vgpu_vreg(vgpu, SBI_CTL_STAT) & SBI_OPCODE_MASK) >>
				SBI_OPCODE_SHIFT) == SBI_CMD_CRRD) {
		unsigned int sbi_offset = (vgpu_vreg(vgpu, SBI_ADDR) &
				SBI_ADDR_OFFSET_MASK) >> SBI_ADDR_OFFSET_SHIFT;
		vgpu_vreg(vgpu, offset) = read_virtual_sbi_register(vgpu,
				sbi_offset);
	}
	read_vreg(vgpu, offset, p_data, bytes);
	return 0;
}

static int sbi_ctl_mmio_write(struct intel_vgpu *vgpu, unsigned int offset,
		void *p_data, unsigned int bytes)
{
	u32 data;

	write_vreg(vgpu, offset, p_data, bytes);
	data = vgpu_vreg(vgpu, offset);

	data &= ~(SBI_STAT_MASK << SBI_STAT_SHIFT);
	data |= SBI_READY;

	data &= ~(SBI_RESPONSE_MASK << SBI_RESPONSE_SHIFT);
	data |= SBI_RESPONSE_SUCCESS;

	vgpu_vreg(vgpu, offset) = data;

	if (((vgpu_vreg(vgpu, SBI_CTL_STAT) & SBI_OPCODE_MASK) >>
				SBI_OPCODE_SHIFT) == SBI_CMD_CRWR) {
		unsigned int sbi_offset = (vgpu_vreg(vgpu, SBI_ADDR) &
				SBI_ADDR_OFFSET_MASK) >> SBI_ADDR_OFFSET_SHIFT;

		write_virtual_sbi_register(vgpu, sbi_offset,
				vgpu_vreg(vgpu, SBI_DATA));
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
		gvt_vgpu_err("invalid pvinfo read: [%x:%x] = %x\n",
				offset, bytes, *(u32 *)p_data);
	vgpu->pv_notified = true;
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
		gvt_vgpu_err("Invalid PV notification %d\n", notification);
	}
	return ret;
}

static int send_display_ready_uevent(struct intel_vgpu *vgpu, int ready)
{
	struct drm_i915_private *dev_priv = vgpu->gvt->dev_priv;
	struct kobject *kobj = &dev_priv->drm.primary->kdev->kobj;
	char *env[3] = {NULL, NULL, NULL};
	char vmid_str[20];
	char display_ready_str[20];

	snprintf(display_ready_str, 20, "GVT_DISPLAY_READY=%d", ready);
	env[0] = display_ready_str;

	snprintf(vmid_str, 20, "VMID=%d", vgpu->id);
	env[1] = vmid_str;

	return kobject_uevent_env(kobj, KOBJ_ADD, env);
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
		send_display_ready_uevent(vgpu, data ? 1 : 0);
		break;
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
	case _vgtif_reg(rsv5[0])..._vgtif_reg(rsv5[3]):
		enter_failsafe_mode(vgpu, GVT_FAILSAFE_INSUFFICIENT_RESOURCE);
		break;
	default:
		gvt_vgpu_err("invalid pvinfo write offset %x bytes %x data %x\n",
				offset, bytes, data);
		break;
	}
	return 0;
}

static int pf_write(struct intel_vgpu *vgpu,
		unsigned int offset, void *p_data, unsigned int bytes)
{
	u32 val = *(u32 *)p_data;

	if ((offset == _PS_1A_CTRL || offset == _PS_2A_CTRL ||
	   offset == _PS_1B_CTRL || offset == _PS_2B_CTRL ||
	   offset == _PS_1C_CTRL) && (val & PS_PLANE_SEL_MASK) != 0) {
		WARN_ONCE(true, "VM(%d): guest is trying to scaling a plane\n",
			  vgpu->id);
		return 0;
	}

	return intel_vgpu_default_mmio_write(vgpu, offset, p_data, bytes);
}

static int power_well_ctl_mmio_write(struct intel_vgpu *vgpu,
		unsigned int offset, void *p_data, unsigned int bytes)
{
	write_vreg(vgpu, offset, p_data, bytes);

	if (vgpu_vreg(vgpu, offset) & HSW_PWR_WELL_CTL_REQ(HSW_DISP_PW_GLOBAL))
		vgpu_vreg(vgpu, offset) |=
			HSW_PWR_WELL_CTL_STATE(HSW_DISP_PW_GLOBAL);
	else
		vgpu_vreg(vgpu, offset) &=
			~HSW_PWR_WELL_CTL_STATE(HSW_DISP_PW_GLOBAL);
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
	u32 mode;

	write_vreg(vgpu, offset, p_data, bytes);
	mode = vgpu_vreg(vgpu, offset);

	if (GFX_MODE_BIT_SET_IN_MASK(mode, START_DMA)) {
		WARN_ONCE(1, "VM(%d): iGVT-g doesn't support GuC\n",
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

	mmio_hw_access_pre(dev_priv);
	I915_WRITE(_MMIO(offset), vgpu_vreg(vgpu, offset));
	mmio_hw_access_post(dev_priv);

	return 0;
}

static int gen9_trtt_chicken_write(struct intel_vgpu *vgpu, unsigned int offset,
		void *p_data, unsigned int bytes)
{
	struct drm_i915_private *dev_priv = vgpu->gvt->dev_priv;
	u32 val = *(u32 *)p_data;

	if (val & 1) {
		/* unblock hw logic */
		mmio_hw_access_pre(dev_priv);
		I915_WRITE(_MMIO(offset), val);
		mmio_hw_access_post(dev_priv);
	}
	write_vreg(vgpu, offset, p_data, bytes);
	return 0;
}

static int dpll_status_read(struct intel_vgpu *vgpu, unsigned int offset,
		void *p_data, unsigned int bytes)
{
	u32 v = 0;

	if (vgpu_vreg(vgpu, 0x46010) & (1 << 31))
		v |= (1 << 0);

	if (vgpu_vreg(vgpu, 0x46014) & (1 << 31))
		v |= (1 << 8);

	if (vgpu_vreg(vgpu, 0x46040) & (1 << 31))
		v |= (1 << 16);

	if (vgpu_vreg(vgpu, 0x46060) & (1 << 31))
		v |= (1 << 24);

	vgpu_vreg(vgpu, offset) = v;

	return intel_vgpu_default_mmio_read(vgpu, offset, p_data, bytes);
}

static int mailbox_write(struct intel_vgpu *vgpu, unsigned int offset,
		void *p_data, unsigned int bytes)
{
	u32 value = *(u32 *)p_data;
	u32 cmd = value & 0xff;
	u32 *data0 = &vgpu_vreg(vgpu, GEN6_PCODE_DATA);

	switch (cmd) {
	case GEN9_PCODE_READ_MEM_LATENCY:
		if (IS_SKYLAKE(vgpu->gvt->dev_priv)
			 || IS_KABYLAKE(vgpu->gvt->dev_priv)) {
			/**
			 * "Read memory latency" command on gen9.
			 * Below memory latency values are read
			 * from skylake platform.
			 */
			if (!*data0)
				*data0 = 0x1e1a1100;
			else
				*data0 = 0x61514b3d;
		}
		break;
	case SKL_PCODE_CDCLK_CONTROL:
		if (IS_SKYLAKE(vgpu->gvt->dev_priv)
			 || IS_KABYLAKE(vgpu->gvt->dev_priv))
			*data0 = SKL_CDCLK_READY_FOR_CHANGE;
		break;
	case GEN6_PCODE_READ_RC6VIDS:
		*data0 |= 0x1;
		break;
	}

	gvt_dbg_core("VM(%d) write %x to mailbox, return data0 %x\n",
		     vgpu->id, value, *data0);
	/**
	 * PCODE_READY clear means ready for pcode read/write,
	 * PCODE_ERROR_MASK clear means no error happened. In GVT-g we
	 * always emulate as pcode read/write success and ready for access
	 * anytime, since we don't touch real physical registers here.
	 */
	value &= ~(GEN6_PCODE_READY | GEN6_PCODE_ERROR_MASK);
	return intel_vgpu_default_mmio_write(vgpu, offset, &value, bytes);
}

static int hws_pga_write(struct intel_vgpu *vgpu, unsigned int offset,
		void *p_data, unsigned int bytes)
{
	u32 value = *(u32 *)p_data;
	int ring_id = intel_gvt_render_mmio_to_ring_id(vgpu->gvt, offset);

	if (!intel_gvt_ggtt_validate_range(vgpu, value, I915_GTT_PAGE_SIZE)) {
		gvt_vgpu_err("VM(%d) write invalid HWSP address, reg:0x%x, value:0x%x\n",
			      vgpu->id, offset, value);
		return -EINVAL;
	}
	/*
	 * Need to emulate all the HWSP register write to ensure host can
	 * update the VM CSB status correctly. Here listed registers can
	 * support BDW, SKL or other platforms with same HWSP registers.
	 */
	if (unlikely(ring_id < 0 || ring_id > I915_NUM_ENGINES)) {
		gvt_vgpu_err("VM(%d) access unknown hardware status page register:0x%x\n",
			     vgpu->id, offset);
		return -EINVAL;
	}
	vgpu->hws_pga[ring_id] = value;
	gvt_dbg_mmio("VM(%d) write: 0x%x to HWSP: 0x%x\n",
		     vgpu->id, value, offset);

	return intel_vgpu_default_mmio_write(vgpu, offset, &value, bytes);
}

static int skl_power_well_ctl_write(struct intel_vgpu *vgpu,
		unsigned int offset, void *p_data, unsigned int bytes)
{
	u32 v = *(u32 *)p_data;

	v &= (1 << 31) | (1 << 29) | (1 << 9) |
	     (1 << 7) | (1 << 5) | (1 << 3) | (1 << 1);
	v |= (v >> 1);

	return intel_vgpu_default_mmio_write(vgpu, offset, &v, bytes);
}

static int skl_lcpll_write(struct intel_vgpu *vgpu, unsigned int offset,
		void *p_data, unsigned int bytes)
{
	u32 v = *(u32 *)p_data;

	/* other bits are MBZ. */
	v &= (1 << 31) | (1 << 30);
	v & (1 << 31) ? (v |= (1 << 30)) : (v &= ~(1 << 30));

	vgpu_vreg(vgpu, offset) = v;

	return 0;
}

static int mmio_read_from_hw(struct intel_vgpu *vgpu,
		unsigned int offset, void *p_data, unsigned int bytes)
{
	struct intel_gvt *gvt = vgpu->gvt;
	struct drm_i915_private *dev_priv = gvt->dev_priv;
	int ring_id;
	u32 ring_base;

	ring_id = intel_gvt_render_mmio_to_ring_id(gvt, offset);
	/**
	 * Read HW reg in following case
	 * a. the offset isn't a ring mmio
	 * b. the offset's ring is running on hw.
	 * c. the offset is ring time stamp mmio
	 */
	if (ring_id >= 0)
		ring_base = dev_priv->engine[ring_id]->mmio_base;

	if (ring_id < 0 || vgpu  == gvt->scheduler.engine_owner[ring_id] ||
	    offset == i915_mmio_reg_offset(RING_TIMESTAMP(ring_base)) ||
	    offset == i915_mmio_reg_offset(RING_TIMESTAMP_UDW(ring_base))) {
		mmio_hw_access_pre(dev_priv);
		vgpu_vreg(vgpu, offset) = I915_READ(_MMIO(offset));
		mmio_hw_access_post(dev_priv);
	}

	return intel_vgpu_default_mmio_read(vgpu, offset, p_data, bytes);
}

static int elsp_mmio_write(struct intel_vgpu *vgpu, unsigned int offset,
		void *p_data, unsigned int bytes)
{
	int ring_id = intel_gvt_render_mmio_to_ring_id(vgpu->gvt, offset);
	struct intel_vgpu_execlist *execlist;
	u32 data = *(u32 *)p_data;
	int ret = 0;

	if (WARN_ON(ring_id < 0 || ring_id > I915_NUM_ENGINES - 1))
		return -EINVAL;

	execlist = &vgpu->submission.execlist[ring_id];

	execlist->elsp_dwords.data[3 - execlist->elsp_dwords.index] = data;
	if (execlist->elsp_dwords.index == 3) {
		ret = intel_vgpu_submit_execlist(vgpu, ring_id);
		if(ret)
			gvt_vgpu_err("fail submit workload on ring %d\n",
				ring_id);
	}

	++execlist->elsp_dwords.index;
	execlist->elsp_dwords.index &= 0x3;
	return ret;
}

static int ring_mode_mmio_write(struct intel_vgpu *vgpu, unsigned int offset,
		void *p_data, unsigned int bytes)
{
	struct intel_vgpu_submission *s = &vgpu->submission;
	u32 data = *(u32 *)p_data;
	int ring_id = intel_gvt_render_mmio_to_ring_id(vgpu->gvt, offset);
	bool enable_execlist;
	int ret;

	write_vreg(vgpu, offset, p_data, bytes);

	/* when PPGTT mode enabled, we will check if guest has called
	 * pvinfo, if not, we will treat this guest as non-gvtg-aware
	 * guest, and stop emulating its cfg space, mmio, gtt, etc.
	 */
	if (((data & _MASKED_BIT_ENABLE(GFX_PPGTT_ENABLE)) ||
			(data & _MASKED_BIT_ENABLE(GFX_RUN_LIST_ENABLE)))
			&& !vgpu->pv_notified) {
		enter_failsafe_mode(vgpu, GVT_FAILSAFE_UNSUPPORTED_GUEST);
		return 0;
	}
	if ((data & _MASKED_BIT_ENABLE(GFX_RUN_LIST_ENABLE))
			|| (data & _MASKED_BIT_DISABLE(GFX_RUN_LIST_ENABLE))) {
		enable_execlist = !!(data & GFX_RUN_LIST_ENABLE);

		gvt_dbg_core("EXECLIST %s on ring %d\n",
				(enable_execlist ? "enabling" : "disabling"),
				ring_id);

		if (!enable_execlist)
			return 0;

		if (s->active)
			return 0;

		ret = intel_vgpu_select_submission_ops(vgpu,
				INTEL_VGPU_EXECLIST_SUBMISSION);
		if (ret)
			return ret;

		intel_vgpu_start_schedule(vgpu);
	}
	return 0;
}

static int gvt_reg_tlb_control_handler(struct intel_vgpu *vgpu,
		unsigned int offset, void *p_data, unsigned int bytes)
{
	unsigned int id = 0;

	write_vreg(vgpu, offset, p_data, bytes);
	vgpu_vreg(vgpu, offset) = 0;

	switch (offset) {
	case 0x4260:
		id = RCS;
		break;
	case 0x4264:
		id = VCS;
		break;
	case 0x4268:
		id = VCS2;
		break;
	case 0x426c:
		id = BCS;
		break;
	case 0x4270:
		id = VECS;
		break;
	default:
		return -EINVAL;
	}
	set_bit(id, (void *)vgpu->submission.tlb_handle_pending);

	return 0;
}

static int ring_reset_ctl_write(struct intel_vgpu *vgpu,
	unsigned int offset, void *p_data, unsigned int bytes)
{
	u32 data;

	write_vreg(vgpu, offset, p_data, bytes);
	data = vgpu_vreg(vgpu, offset);

	if (data & _MASKED_BIT_ENABLE(RESET_CTL_REQUEST_RESET))
		data |= RESET_CTL_READY_TO_RESET;
	else if (data & _MASKED_BIT_DISABLE(RESET_CTL_REQUEST_RESET))
		data &= ~RESET_CTL_READY_TO_RESET;

	vgpu_vreg(vgpu, offset) = data;
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

#define MMIO_GM_RDR(reg, d, r, w) \
	MMIO_F(reg, 4, F_GMADR | F_CMD_ACCESS, 0xFFFFF000, 0, d, r, w)

#define MMIO_RO(reg, d, f, rm, r, w) \
	MMIO_F(reg, 4, F_RO | f, 0, rm, d, r, w)

#define MMIO_RING_F(prefix, s, f, am, rm, d, r, w) do { \
	MMIO_F(prefix(RENDER_RING_BASE), s, f, am, rm, d, r, w); \
	MMIO_F(prefix(BLT_RING_BASE), s, f, am, rm, d, r, w); \
	MMIO_F(prefix(GEN6_BSD_RING_BASE), s, f, am, rm, d, r, w); \
	MMIO_F(prefix(VEBOX_RING_BASE), s, f, am, rm, d, r, w); \
	if (HAS_BSD2(dev_priv)) \
		MMIO_F(prefix(GEN8_BSD2_RING_BASE), s, f, am, rm, d, r, w); \
} while (0)

#define MMIO_RING_D(prefix, d) \
	MMIO_RING_F(prefix, 4, 0, 0, 0, d, NULL, NULL)

#define MMIO_RING_DFH(prefix, d, f, r, w) \
	MMIO_RING_F(prefix, 4, f, 0, 0, d, r, w)

#define MMIO_RING_GM(prefix, d, r, w) \
	MMIO_RING_F(prefix, 4, F_GMADR, 0xFFFF0000, 0, d, r, w)

#define MMIO_RING_GM_RDR(prefix, d, r, w) \
	MMIO_RING_F(prefix, 4, F_GMADR | F_CMD_ACCESS, 0xFFFF0000, 0, d, r, w)

#define MMIO_RING_RO(prefix, d, f, rm, r, w) \
	MMIO_RING_F(prefix, 4, F_RO | f, 0, rm, d, r, w)

static int init_generic_mmio_info(struct intel_gvt *gvt)
{
	struct drm_i915_private *dev_priv = gvt->dev_priv;
	int ret;

	MMIO_RING_DFH(RING_IMR, D_ALL, F_CMD_ACCESS, NULL,
		intel_vgpu_reg_imr_handler);

	MMIO_DFH(SDEIMR, D_ALL, 0, NULL, intel_vgpu_reg_imr_handler);
	MMIO_DFH(SDEIER, D_ALL, 0, NULL, intel_vgpu_reg_ier_handler);
	MMIO_DFH(SDEIIR, D_ALL, 0, NULL, intel_vgpu_reg_iir_handler);
	MMIO_D(SDEISR, D_ALL);

	MMIO_RING_DFH(RING_HWSTAM, D_ALL, F_CMD_ACCESS, NULL, NULL);

	MMIO_GM_RDR(RENDER_HWS_PGA_GEN7, D_ALL, NULL, NULL);
	MMIO_GM_RDR(BSD_HWS_PGA_GEN7, D_ALL, NULL, NULL);
	MMIO_GM_RDR(BLT_HWS_PGA_GEN7, D_ALL, NULL, NULL);
	MMIO_GM_RDR(VEBOX_HWS_PGA_GEN7, D_ALL, NULL, NULL);

#define RING_REG(base) (base + 0x28)
	MMIO_RING_DFH(RING_REG, D_ALL, F_CMD_ACCESS, NULL, NULL);
#undef RING_REG

#define RING_REG(base) (base + 0x134)
	MMIO_RING_DFH(RING_REG, D_ALL, F_CMD_ACCESS, NULL, NULL);
#undef RING_REG

#define RING_REG(base) (base + 0x6c)
	MMIO_RING_DFH(RING_REG, D_ALL, 0, mmio_read_from_hw, NULL);
#undef RING_REG
	MMIO_DH(GEN7_SC_INSTDONE, D_BDW_PLUS, mmio_read_from_hw, NULL);

	MMIO_GM_RDR(0x2148, D_ALL, NULL, NULL);
	MMIO_GM_RDR(CCID, D_ALL, NULL, NULL);
	MMIO_GM_RDR(0x12198, D_ALL, NULL, NULL);
	MMIO_D(GEN7_CXT_SIZE, D_ALL);

	MMIO_RING_DFH(RING_TAIL, D_ALL, F_CMD_ACCESS, NULL, NULL);
	MMIO_RING_DFH(RING_HEAD, D_ALL, F_CMD_ACCESS, NULL, NULL);
	MMIO_RING_DFH(RING_CTL, D_ALL, F_CMD_ACCESS, NULL, NULL);
	MMIO_RING_DFH(RING_ACTHD, D_ALL, F_CMD_ACCESS, mmio_read_from_hw, NULL);
	MMIO_RING_GM_RDR(RING_START, D_ALL, NULL, NULL);

	/* RING MODE */
#define RING_REG(base) (base + 0x29c)
	MMIO_RING_DFH(RING_REG, D_ALL, F_MODE_MASK | F_CMD_ACCESS, NULL,
		ring_mode_mmio_write);
#undef RING_REG

	MMIO_RING_DFH(RING_MI_MODE, D_ALL, F_MODE_MASK | F_CMD_ACCESS,
		NULL, NULL);
	MMIO_RING_DFH(RING_INSTPM, D_ALL, F_MODE_MASK | F_CMD_ACCESS,
			NULL, NULL);
	MMIO_RING_DFH(RING_TIMESTAMP, D_ALL, F_CMD_ACCESS,
			mmio_read_from_hw, NULL);
	MMIO_RING_DFH(RING_TIMESTAMP_UDW, D_ALL, F_CMD_ACCESS,
			mmio_read_from_hw, NULL);

	MMIO_DFH(GEN7_GT_MODE, D_ALL, F_MODE_MASK | F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(CACHE_MODE_0_GEN7, D_ALL, F_MODE_MASK | F_CMD_ACCESS,
		NULL, NULL);
	MMIO_DFH(CACHE_MODE_1, D_ALL, F_MODE_MASK | F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(CACHE_MODE_0, D_ALL, F_MODE_MASK | F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0x2124, D_ALL, F_MODE_MASK | F_CMD_ACCESS, NULL, NULL);

	MMIO_DFH(0x20dc, D_ALL, F_MODE_MASK | F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(_3D_CHICKEN3, D_ALL, F_MODE_MASK | F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0x2088, D_ALL, F_MODE_MASK | F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0x20e4, D_ALL, F_MODE_MASK | F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0x2470, D_ALL, F_MODE_MASK | F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(GAM_ECOCHK, D_ALL, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(GEN7_COMMON_SLICE_CHICKEN1, D_ALL, F_MODE_MASK | F_CMD_ACCESS,
		NULL, NULL);
	MMIO_DFH(COMMON_SLICE_CHICKEN2, D_ALL, F_MODE_MASK | F_CMD_ACCESS,
		 NULL, NULL);
	MMIO_DFH(0x9030, D_ALL, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0x20a0, D_ALL, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0x2420, D_ALL, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0x2430, D_ALL, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0x2434, D_ALL, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0x2438, D_ALL, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0x243c, D_ALL, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0x7018, D_ALL, F_MODE_MASK | F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(HALF_SLICE_CHICKEN3, D_ALL, F_MODE_MASK | F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(GEN7_HALF_SLICE_CHICKEN1, D_ALL, F_MODE_MASK | F_CMD_ACCESS, NULL, NULL);

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

	MMIO_DH(PIPECONF(PIPE_A), D_ALL, NULL, pipeconf_mmio_write);
	MMIO_DH(PIPECONF(PIPE_B), D_ALL, NULL, pipeconf_mmio_write);
	MMIO_DH(PIPECONF(PIPE_C), D_ALL, NULL, pipeconf_mmio_write);
	MMIO_DH(PIPECONF(_PIPE_EDP), D_ALL, NULL, pipeconf_mmio_write);

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
	MMIO_DH(DSPSURF(PIPE_A), D_ALL, NULL, pri_surf_mmio_write);
	MMIO_D(DSPOFFSET(PIPE_A), D_ALL);
	MMIO_D(DSPSURFLIVE(PIPE_A), D_ALL);

	MMIO_D(DSPCNTR(PIPE_B), D_ALL);
	MMIO_D(DSPADDR(PIPE_B), D_ALL);
	MMIO_D(DSPSTRIDE(PIPE_B), D_ALL);
	MMIO_D(DSPPOS(PIPE_B), D_ALL);
	MMIO_D(DSPSIZE(PIPE_B), D_ALL);
	MMIO_DH(DSPSURF(PIPE_B), D_ALL, NULL, pri_surf_mmio_write);
	MMIO_D(DSPOFFSET(PIPE_B), D_ALL);
	MMIO_D(DSPSURFLIVE(PIPE_B), D_ALL);

	MMIO_D(DSPCNTR(PIPE_C), D_ALL);
	MMIO_D(DSPADDR(PIPE_C), D_ALL);
	MMIO_D(DSPSTRIDE(PIPE_C), D_ALL);
	MMIO_D(DSPPOS(PIPE_C), D_ALL);
	MMIO_D(DSPSIZE(PIPE_C), D_ALL);
	MMIO_DH(DSPSURF(PIPE_C), D_ALL, NULL, pri_surf_mmio_write);
	MMIO_D(DSPOFFSET(PIPE_C), D_ALL);
	MMIO_D(DSPSURFLIVE(PIPE_C), D_ALL);

	MMIO_D(SPRCTL(PIPE_A), D_ALL);
	MMIO_D(SPRLINOFF(PIPE_A), D_ALL);
	MMIO_D(SPRSTRIDE(PIPE_A), D_ALL);
	MMIO_D(SPRPOS(PIPE_A), D_ALL);
	MMIO_D(SPRSIZE(PIPE_A), D_ALL);
	MMIO_D(SPRKEYVAL(PIPE_A), D_ALL);
	MMIO_D(SPRKEYMSK(PIPE_A), D_ALL);
	MMIO_DH(SPRSURF(PIPE_A), D_ALL, NULL, spr_surf_mmio_write);
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
	MMIO_DH(SPRSURF(PIPE_B), D_ALL, NULL, spr_surf_mmio_write);
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
	MMIO_DH(SPRSURF(PIPE_C), D_ALL, NULL, spr_surf_mmio_write);
	MMIO_D(SPRKEYMAX(PIPE_C), D_ALL);
	MMIO_D(SPROFFSET(PIPE_C), D_ALL);
	MMIO_D(SPRSCALE(PIPE_C), D_ALL);
	MMIO_D(SPRSURFLIVE(PIPE_C), D_ALL);

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

	MMIO_F(PCH_GMBUS0, 4 * 4, 0, 0, 0, D_ALL, gmbus_mmio_read,
		gmbus_mmio_write);
	MMIO_F(PCH_GPIOA, 6 * 4, F_UNALIGN, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(0xe4f00, 0x28, 0, 0, 0, D_ALL, NULL, NULL);

	MMIO_F(_PCH_DPB_AUX_CH_CTL, 6 * 4, 0, 0, 0, D_PRE_SKL, NULL,
		dp_aux_ch_ctl_mmio_write);
	MMIO_F(_PCH_DPC_AUX_CH_CTL, 6 * 4, 0, 0, 0, D_PRE_SKL, NULL,
		dp_aux_ch_ctl_mmio_write);
	MMIO_F(_PCH_DPD_AUX_CH_CTL, 6 * 4, 0, 0, 0, D_PRE_SKL, NULL,
		dp_aux_ch_ctl_mmio_write);

	MMIO_DH(PCH_ADPA, D_PRE_SKL, NULL, pch_adpa_mmio_write);

	MMIO_DH(_PCH_TRANSACONF, D_ALL, NULL, transconf_mmio_write);
	MMIO_DH(_PCH_TRANSBCONF, D_ALL, NULL, transconf_mmio_write);

	MMIO_DH(FDI_RX_IIR(PIPE_A), D_ALL, NULL, fdi_rx_iir_mmio_write);
	MMIO_DH(FDI_RX_IIR(PIPE_B), D_ALL, NULL, fdi_rx_iir_mmio_write);
	MMIO_DH(FDI_RX_IIR(PIPE_C), D_ALL, NULL, fdi_rx_iir_mmio_write);
	MMIO_DH(FDI_RX_IMR(PIPE_A), D_ALL, NULL, update_fdi_rx_iir_status);
	MMIO_DH(FDI_RX_IMR(PIPE_B), D_ALL, NULL, update_fdi_rx_iir_status);
	MMIO_DH(FDI_RX_IMR(PIPE_C), D_ALL, NULL, update_fdi_rx_iir_status);
	MMIO_DH(FDI_RX_CTL(PIPE_A), D_ALL, NULL, update_fdi_rx_iir_status);
	MMIO_DH(FDI_RX_CTL(PIPE_B), D_ALL, NULL, update_fdi_rx_iir_status);
	MMIO_DH(FDI_RX_CTL(PIPE_C), D_ALL, NULL, update_fdi_rx_iir_status);

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

	MMIO_DH(PCH_PP_CONTROL, D_ALL, NULL, pch_pp_control_mmio_write);
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

	MMIO_DH(0xe651c, D_ALL, dpy_reg_mmio_read, NULL);
	MMIO_DH(0xe661c, D_ALL, dpy_reg_mmio_read, NULL);
	MMIO_DH(0xe671c, D_ALL, dpy_reg_mmio_read, NULL);
	MMIO_DH(0xe681c, D_ALL, dpy_reg_mmio_read, NULL);
	MMIO_DH(0xe6c04, D_ALL, dpy_reg_mmio_read, NULL);
	MMIO_DH(0xe6e1c, D_ALL, dpy_reg_mmio_read, NULL);

	MMIO_RO(PCH_PORT_HOTPLUG, D_ALL, 0,
		PORTA_HOTPLUG_STATUS_MASK
		| PORTB_HOTPLUG_STATUS_MASK
		| PORTC_HOTPLUG_STATUS_MASK
		| PORTD_HOTPLUG_STATUS_MASK,
		NULL, NULL);

	MMIO_DH(LCPLL_CTL, D_ALL, NULL, lcpll_ctl_mmio_write);
	MMIO_D(FUSE_STRAP, D_ALL);
	MMIO_D(DIGITAL_PORT_HOTPLUG_CNTRL, D_ALL);

	MMIO_D(DISP_ARB_CTL, D_ALL);
	MMIO_D(DISP_ARB_CTL2, D_ALL);

	MMIO_D(ILK_DISPLAY_CHICKEN1, D_ALL);
	MMIO_D(ILK_DISPLAY_CHICKEN2, D_ALL);
	MMIO_D(ILK_DSPCLK_GATE_D, D_ALL);

	MMIO_D(SOUTH_CHICKEN1, D_ALL);
	MMIO_DH(SOUTH_CHICKEN2, D_ALL, NULL, south_chicken2_mmio_write);
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

	MMIO_D(PREC_PAL_INDEX(PIPE_A), D_ALL);
	MMIO_D(PREC_PAL_DATA(PIPE_A), D_ALL);
	MMIO_F(PREC_PAL_GC_MAX(PIPE_A, 0), 4 * 3, 0, 0, 0, D_ALL, NULL, NULL);

	MMIO_D(PREC_PAL_INDEX(PIPE_B), D_ALL);
	MMIO_D(PREC_PAL_DATA(PIPE_B), D_ALL);
	MMIO_F(PREC_PAL_GC_MAX(PIPE_B, 0), 4 * 3, 0, 0, 0, D_ALL, NULL, NULL);

	MMIO_D(PREC_PAL_INDEX(PIPE_C), D_ALL);
	MMIO_D(PREC_PAL_DATA(PIPE_C), D_ALL);
	MMIO_F(PREC_PAL_GC_MAX(PIPE_C, 0), 4 * 3, 0, 0, 0, D_ALL, NULL, NULL);

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

	MMIO_D(PIPE_MULT(PIPE_A), D_ALL);
	MMIO_D(PIPE_MULT(PIPE_B), D_ALL);
	MMIO_D(PIPE_MULT(PIPE_C), D_ALL);

	MMIO_D(HSW_TVIDEO_DIP_CTL(TRANSCODER_A), D_ALL);
	MMIO_D(HSW_TVIDEO_DIP_CTL(TRANSCODER_B), D_ALL);
	MMIO_D(HSW_TVIDEO_DIP_CTL(TRANSCODER_C), D_ALL);

	MMIO_DH(SFUSE_STRAP, D_ALL, NULL, NULL);
	MMIO_D(SBI_ADDR, D_ALL);
	MMIO_DH(SBI_DATA, D_ALL, sbi_data_mmio_read, NULL);
	MMIO_DH(SBI_CTL_STAT, D_ALL, NULL, sbi_ctl_mmio_write);
	MMIO_D(PIXCLK_GATE, D_ALL);

	MMIO_F(_DPA_AUX_CH_CTL, 6 * 4, 0, 0, 0, D_ALL, NULL,
		dp_aux_ch_ctl_mmio_write);

	MMIO_DH(DDI_BUF_CTL(PORT_A), D_ALL, NULL, ddi_buf_ctl_mmio_write);
	MMIO_DH(DDI_BUF_CTL(PORT_B), D_ALL, NULL, ddi_buf_ctl_mmio_write);
	MMIO_DH(DDI_BUF_CTL(PORT_C), D_ALL, NULL, ddi_buf_ctl_mmio_write);
	MMIO_DH(DDI_BUF_CTL(PORT_D), D_ALL, NULL, ddi_buf_ctl_mmio_write);
	MMIO_DH(DDI_BUF_CTL(PORT_E), D_ALL, NULL, ddi_buf_ctl_mmio_write);

	MMIO_DH(DP_TP_CTL(PORT_A), D_ALL, NULL, dp_tp_ctl_mmio_write);
	MMIO_DH(DP_TP_CTL(PORT_B), D_ALL, NULL, dp_tp_ctl_mmio_write);
	MMIO_DH(DP_TP_CTL(PORT_C), D_ALL, NULL, dp_tp_ctl_mmio_write);
	MMIO_DH(DP_TP_CTL(PORT_D), D_ALL, NULL, dp_tp_ctl_mmio_write);
	MMIO_DH(DP_TP_CTL(PORT_E), D_ALL, NULL, dp_tp_ctl_mmio_write);

	MMIO_DH(DP_TP_STATUS(PORT_A), D_ALL, NULL, dp_tp_status_mmio_write);
	MMIO_DH(DP_TP_STATUS(PORT_B), D_ALL, NULL, dp_tp_status_mmio_write);
	MMIO_DH(DP_TP_STATUS(PORT_C), D_ALL, NULL, dp_tp_status_mmio_write);
	MMIO_DH(DP_TP_STATUS(PORT_D), D_ALL, NULL, dp_tp_status_mmio_write);
	MMIO_DH(DP_TP_STATUS(PORT_E), D_ALL, NULL, NULL);

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
	MMIO_DFH(GTFIFODBG, D_ALL, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(GTFIFOCTL, D_ALL, F_CMD_ACCESS, NULL, NULL);
	MMIO_DH(FORCEWAKE_MT, D_PRE_SKL, NULL, mul_force_wake_write);
	MMIO_DH(FORCEWAKE_ACK_HSW, D_BDW, NULL, NULL);
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
	/*
	 * Use an arbitrary power well controlled by the PWR_WELL_CTL
	 * register.
	 */
	MMIO_DH(HSW_PWR_WELL_CTL_BIOS(HSW_DISP_PW_GLOBAL), D_BDW, NULL,
		power_well_ctl_mmio_write);
	MMIO_DH(HSW_PWR_WELL_CTL_DRIVER(HSW_DISP_PW_GLOBAL), D_BDW, NULL,
		power_well_ctl_mmio_write);
	MMIO_DH(HSW_PWR_WELL_CTL_KVMR, D_BDW, NULL, power_well_ctl_mmio_write);
	MMIO_DH(HSW_PWR_WELL_CTL_DEBUG(HSW_DISP_PW_GLOBAL), D_BDW, NULL,
		power_well_ctl_mmio_write);
	MMIO_DH(HSW_PWR_WELL_CTL5, D_BDW, NULL, power_well_ctl_mmio_write);
	MMIO_DH(HSW_PWR_WELL_CTL6, D_BDW, NULL, power_well_ctl_mmio_write);

	MMIO_D(RSTDBYCTL, D_ALL);

	MMIO_DH(GEN6_GDRST, D_ALL, NULL, gdrst_mmio_write);
	MMIO_F(FENCE_REG_GEN6_LO(0), 0x80, 0, 0, 0, D_ALL, fence_mmio_read, fence_mmio_write);
	MMIO_DH(CPU_VGACNTRL, D_ALL, NULL, vga_control_mmio_write);

	MMIO_D(TILECTL, D_ALL);

	MMIO_D(GEN6_UCGCTL1, D_ALL);
	MMIO_D(GEN6_UCGCTL2, D_ALL);

	MMIO_F(0x4f000, 0x90, 0, 0, 0, D_ALL, NULL, NULL);

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
	MMIO_DH(GEN6_MBCTL, D_ALL, NULL, mbctl_write);
	MMIO_D(0x911c, D_ALL);
	MMIO_D(0x9120, D_ALL);
	MMIO_DFH(GEN7_UCGCTL4, D_ALL, F_CMD_ACCESS, NULL, NULL);

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
	MMIO_DFH(0x215c, D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0x2178, D_ALL, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0x217c, D_ALL, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0x12178, D_ALL, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0x1217c, D_ALL, F_CMD_ACCESS, NULL, NULL);

	MMIO_F(0x2290, 8, F_CMD_ACCESS, 0, 0, D_BDW_PLUS, NULL, NULL);
	MMIO_D(0x2b00, D_BDW_PLUS);
	MMIO_D(0x2360, D_BDW_PLUS);
	MMIO_F(0x5200, 32, F_CMD_ACCESS, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(0x5240, 32, F_CMD_ACCESS, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(0x5280, 16, F_CMD_ACCESS, 0, 0, D_ALL, NULL, NULL);

	MMIO_DFH(0x1c17c, D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0x1c178, D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(BCS_SWCTRL, D_ALL, F_CMD_ACCESS, NULL, NULL);

	MMIO_F(HS_INVOCATION_COUNT, 8, F_CMD_ACCESS, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(DS_INVOCATION_COUNT, 8, F_CMD_ACCESS, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(IA_VERTICES_COUNT, 8, F_CMD_ACCESS, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(IA_PRIMITIVES_COUNT, 8, F_CMD_ACCESS, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(VS_INVOCATION_COUNT, 8, F_CMD_ACCESS, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(GS_INVOCATION_COUNT, 8, F_CMD_ACCESS, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(GS_PRIMITIVES_COUNT, 8, F_CMD_ACCESS, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(CL_INVOCATION_COUNT, 8, F_CMD_ACCESS, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(CL_PRIMITIVES_COUNT, 8, F_CMD_ACCESS, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(PS_INVOCATION_COUNT, 8, F_CMD_ACCESS, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(PS_DEPTH_COUNT, 8, F_CMD_ACCESS, 0, 0, D_ALL, NULL, NULL);
	MMIO_DH(0x4260, D_BDW_PLUS, NULL, gvt_reg_tlb_control_handler);
	MMIO_DH(0x4264, D_BDW_PLUS, NULL, gvt_reg_tlb_control_handler);
	MMIO_DH(0x4268, D_BDW_PLUS, NULL, gvt_reg_tlb_control_handler);
	MMIO_DH(0x426c, D_BDW_PLUS, NULL, gvt_reg_tlb_control_handler);
	MMIO_DH(0x4270, D_BDW_PLUS, NULL, gvt_reg_tlb_control_handler);
	MMIO_DFH(0x4094, D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);

	MMIO_DFH(ARB_MODE, D_ALL, F_MODE_MASK | F_CMD_ACCESS, NULL, NULL);
	MMIO_RING_GM_RDR(RING_BBADDR, D_ALL, NULL, NULL);
	MMIO_DFH(0x2220, D_ALL, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0x12220, D_ALL, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0x22220, D_ALL, F_CMD_ACCESS, NULL, NULL);
	MMIO_RING_DFH(RING_SYNC_1, D_ALL, F_CMD_ACCESS, NULL, NULL);
	MMIO_RING_DFH(RING_SYNC_0, D_ALL, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0x22178, D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0x1a178, D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0x1a17c, D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0x2217c, D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);
	return 0;
}

static int init_broadwell_mmio_info(struct intel_gvt *gvt)
{
	struct drm_i915_private *dev_priv = gvt->dev_priv;
	int ret;

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

	MMIO_RING_DFH(RING_ACTHD_UDW, D_BDW_PLUS, F_CMD_ACCESS,
		mmio_read_from_hw, NULL);

#define RING_REG(base) (base + 0xd0)
	MMIO_RING_F(RING_REG, 4, F_RO, 0,
		~_MASKED_BIT_ENABLE(RESET_CTL_REQUEST_RESET), D_BDW_PLUS, NULL,
		ring_reset_ctl_write);
#undef RING_REG

#define RING_REG(base) (base + 0x230)
	MMIO_RING_DFH(RING_REG, D_BDW_PLUS, 0, NULL, elsp_mmio_write);
#undef RING_REG

#define RING_REG(base) (base + 0x234)
	MMIO_RING_F(RING_REG, 8, F_RO | F_CMD_ACCESS, 0, ~0, D_BDW_PLUS,
		NULL, NULL);
#undef RING_REG

#define RING_REG(base) (base + 0x244)
	MMIO_RING_DFH(RING_REG, D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);
#undef RING_REG

#define RING_REG(base) (base + 0x370)
	MMIO_RING_F(RING_REG, 48, F_RO, 0, ~0, D_BDW_PLUS, NULL, NULL);
#undef RING_REG

#define RING_REG(base) (base + 0x3a0)
	MMIO_RING_DFH(RING_REG, D_BDW_PLUS, F_MODE_MASK, NULL, NULL);
#undef RING_REG

	MMIO_D(PIPEMISC(PIPE_A), D_BDW_PLUS);
	MMIO_D(PIPEMISC(PIPE_B), D_BDW_PLUS);
	MMIO_D(PIPEMISC(PIPE_C), D_BDW_PLUS);
	MMIO_D(0x1c1d0, D_BDW_PLUS);
	MMIO_D(GEN6_MBCUNIT_SNPCR, D_BDW_PLUS);
	MMIO_D(GEN7_MISCCPCTL, D_BDW_PLUS);
	MMIO_D(0x1c054, D_BDW_PLUS);

	MMIO_DH(GEN6_PCODE_MAILBOX, D_BDW_PLUS, NULL, mailbox_write);

	MMIO_D(GEN8_PRIVATE_PAT_LO, D_BDW_PLUS);
	MMIO_D(GEN8_PRIVATE_PAT_HI, D_BDW_PLUS);

	MMIO_D(GAMTARBMODE, D_BDW_PLUS);

#define RING_REG(base) (base + 0x270)
	MMIO_RING_F(RING_REG, 32, 0, 0, 0, D_BDW_PLUS, NULL, NULL);
#undef RING_REG

	MMIO_RING_GM_RDR(RING_HWS_PGA, D_BDW_PLUS, NULL, hws_pga_write);

	MMIO_DFH(HDC_CHICKEN0, D_BDW_PLUS, F_MODE_MASK | F_CMD_ACCESS, NULL, NULL);

	MMIO_D(CHICKEN_PIPESL_1(PIPE_A), D_BDW_PLUS);
	MMIO_D(CHICKEN_PIPESL_1(PIPE_B), D_BDW_PLUS);
	MMIO_D(CHICKEN_PIPESL_1(PIPE_C), D_BDW_PLUS);

	MMIO_D(WM_MISC, D_BDW);
	MMIO_D(BDW_EDP_PSR_BASE, D_BDW);

	MMIO_D(0x66c00, D_BDW_PLUS);
	MMIO_D(0x66c04, D_BDW_PLUS);

	MMIO_D(HSW_GTT_CACHE_EN, D_BDW_PLUS);

	MMIO_D(GEN8_EU_DISABLE0, D_BDW_PLUS);
	MMIO_D(GEN8_EU_DISABLE1, D_BDW_PLUS);
	MMIO_D(GEN8_EU_DISABLE2, D_BDW_PLUS);

	MMIO_D(0xfdc, D_BDW_PLUS);
	MMIO_DFH(GEN8_ROW_CHICKEN, D_BDW_PLUS, F_MODE_MASK | F_CMD_ACCESS,
		NULL, NULL);
	MMIO_DFH(GEN7_ROW_CHICKEN2, D_BDW_PLUS, F_MODE_MASK | F_CMD_ACCESS,
		NULL, NULL);
	MMIO_DFH(GEN8_UCGCTL6, D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);

	MMIO_DFH(0xb1f0, D_BDW, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0xb1c0, D_BDW, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(GEN8_L3SQCREG4, D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0xb100, D_BDW, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0xb10c, D_BDW, F_CMD_ACCESS, NULL, NULL);
	MMIO_D(0xb110, D_BDW);

	MMIO_F(0x24d0, 48, F_CMD_ACCESS, 0, 0, D_BDW_PLUS,
		NULL, force_nonpriv_write);

	MMIO_D(0x44484, D_BDW_PLUS);
	MMIO_D(0x4448c, D_BDW_PLUS);

	MMIO_DFH(0x83a4, D_BDW, F_CMD_ACCESS, NULL, NULL);
	MMIO_D(GEN8_L3_LRA_1_GPGPU, D_BDW_PLUS);

	MMIO_DFH(0x8430, D_BDW, F_CMD_ACCESS, NULL, NULL);

	MMIO_D(0x110000, D_BDW_PLUS);

	MMIO_D(0x48400, D_BDW_PLUS);

	MMIO_D(0x6e570, D_BDW_PLUS);
	MMIO_D(0x65f10, D_BDW_PLUS);

	MMIO_DFH(0xe194, D_BDW_PLUS, F_MODE_MASK | F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0xe188, D_BDW_PLUS, F_MODE_MASK | F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(HALF_SLICE_CHICKEN2, D_BDW_PLUS, F_MODE_MASK | F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0x2580, D_BDW_PLUS, F_MODE_MASK | F_CMD_ACCESS, NULL, NULL);

	MMIO_DFH(0x2248, D_BDW, F_CMD_ACCESS, NULL, NULL);

	MMIO_DFH(0xe220, D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0xe230, D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0xe240, D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0xe260, D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0xe270, D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0xe280, D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0xe2a0, D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0xe2b0, D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0xe2c0, D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);
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

	MMIO_F(_DPB_AUX_CH_CTL, 6 * 4, 0, 0, 0, D_SKL_PLUS, NULL,
						dp_aux_ch_ctl_mmio_write);
	MMIO_F(_DPC_AUX_CH_CTL, 6 * 4, 0, 0, 0, D_SKL_PLUS, NULL,
						dp_aux_ch_ctl_mmio_write);
	MMIO_F(_DPD_AUX_CH_CTL, 6 * 4, 0, 0, 0, D_SKL_PLUS, NULL,
						dp_aux_ch_ctl_mmio_write);

	/*
	 * Use an arbitrary power well controlled by the PWR_WELL_CTL
	 * register.
	 */
	MMIO_D(HSW_PWR_WELL_CTL_BIOS(SKL_DISP_PW_MISC_IO), D_SKL_PLUS);
	MMIO_DH(HSW_PWR_WELL_CTL_DRIVER(SKL_DISP_PW_MISC_IO), D_SKL_PLUS, NULL,
		skl_power_well_ctl_write);

	MMIO_D(0xa210, D_SKL_PLUS);
	MMIO_D(GEN9_MEDIA_PG_IDLE_HYSTERESIS, D_SKL_PLUS);
	MMIO_D(GEN9_RENDER_PG_IDLE_HYSTERESIS, D_SKL_PLUS);
	MMIO_DFH(GEN9_GAMT_ECO_REG_RW_IA, D_SKL_PLUS, F_CMD_ACCESS, NULL, NULL);
	MMIO_DH(0x4ddc, D_SKL_PLUS, NULL, NULL);
	MMIO_DH(0x42080, D_SKL_PLUS, NULL, NULL);
	MMIO_D(0x45504, D_SKL_PLUS);
	MMIO_D(0x45520, D_SKL_PLUS);
	MMIO_D(0x46000, D_SKL_PLUS);
	MMIO_DH(0x46010, D_SKL | D_KBL, NULL, skl_lcpll_write);
	MMIO_DH(0x46014, D_SKL | D_KBL, NULL, skl_lcpll_write);
	MMIO_D(0x6C040, D_SKL | D_KBL);
	MMIO_D(0x6C048, D_SKL | D_KBL);
	MMIO_D(0x6C050, D_SKL | D_KBL);
	MMIO_D(0x6C044, D_SKL | D_KBL);
	MMIO_D(0x6C04C, D_SKL | D_KBL);
	MMIO_D(0x6C054, D_SKL | D_KBL);
	MMIO_D(0x6c058, D_SKL | D_KBL);
	MMIO_D(0x6c05c, D_SKL | D_KBL);
	MMIO_DH(0X6c060, D_SKL | D_KBL, dpll_status_read, NULL);

	MMIO_DH(SKL_PS_WIN_POS(PIPE_A, 0), D_SKL_PLUS, NULL, pf_write);
	MMIO_DH(SKL_PS_WIN_POS(PIPE_A, 1), D_SKL_PLUS, NULL, pf_write);
	MMIO_DH(SKL_PS_WIN_POS(PIPE_B, 0), D_SKL_PLUS, NULL, pf_write);
	MMIO_DH(SKL_PS_WIN_POS(PIPE_B, 1), D_SKL_PLUS, NULL, pf_write);
	MMIO_DH(SKL_PS_WIN_POS(PIPE_C, 0), D_SKL_PLUS, NULL, pf_write);
	MMIO_DH(SKL_PS_WIN_POS(PIPE_C, 1), D_SKL_PLUS, NULL, pf_write);

	MMIO_DH(SKL_PS_WIN_SZ(PIPE_A, 0), D_SKL_PLUS, NULL, pf_write);
	MMIO_DH(SKL_PS_WIN_SZ(PIPE_A, 1), D_SKL_PLUS, NULL, pf_write);
	MMIO_DH(SKL_PS_WIN_SZ(PIPE_B, 0), D_SKL_PLUS, NULL, pf_write);
	MMIO_DH(SKL_PS_WIN_SZ(PIPE_B, 1), D_SKL_PLUS, NULL, pf_write);
	MMIO_DH(SKL_PS_WIN_SZ(PIPE_C, 0), D_SKL_PLUS, NULL, pf_write);
	MMIO_DH(SKL_PS_WIN_SZ(PIPE_C, 1), D_SKL_PLUS, NULL, pf_write);

	MMIO_DH(SKL_PS_CTRL(PIPE_A, 0), D_SKL_PLUS, NULL, pf_write);
	MMIO_DH(SKL_PS_CTRL(PIPE_A, 1), D_SKL_PLUS, NULL, pf_write);
	MMIO_DH(SKL_PS_CTRL(PIPE_B, 0), D_SKL_PLUS, NULL, pf_write);
	MMIO_DH(SKL_PS_CTRL(PIPE_B, 1), D_SKL_PLUS, NULL, pf_write);
	MMIO_DH(SKL_PS_CTRL(PIPE_C, 0), D_SKL_PLUS, NULL, pf_write);
	MMIO_DH(SKL_PS_CTRL(PIPE_C, 1), D_SKL_PLUS, NULL, pf_write);

	MMIO_DH(PLANE_BUF_CFG(PIPE_A, 0), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(PLANE_BUF_CFG(PIPE_A, 1), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(PLANE_BUF_CFG(PIPE_A, 2), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(PLANE_BUF_CFG(PIPE_A, 3), D_SKL_PLUS, NULL, NULL);

	MMIO_DH(PLANE_BUF_CFG(PIPE_B, 0), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(PLANE_BUF_CFG(PIPE_B, 1), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(PLANE_BUF_CFG(PIPE_B, 2), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(PLANE_BUF_CFG(PIPE_B, 3), D_SKL_PLUS, NULL, NULL);

	MMIO_DH(PLANE_BUF_CFG(PIPE_C, 0), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(PLANE_BUF_CFG(PIPE_C, 1), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(PLANE_BUF_CFG(PIPE_C, 2), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(PLANE_BUF_CFG(PIPE_C, 3), D_SKL_PLUS, NULL, NULL);

	MMIO_DH(CUR_BUF_CFG(PIPE_A), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(CUR_BUF_CFG(PIPE_B), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(CUR_BUF_CFG(PIPE_C), D_SKL_PLUS, NULL, NULL);

	MMIO_F(PLANE_WM(PIPE_A, 0, 0), 4 * 8, 0, 0, 0, D_SKL_PLUS, NULL, NULL);
	MMIO_F(PLANE_WM(PIPE_A, 1, 0), 4 * 8, 0, 0, 0, D_SKL_PLUS, NULL, NULL);
	MMIO_F(PLANE_WM(PIPE_A, 2, 0), 4 * 8, 0, 0, 0, D_SKL_PLUS, NULL, NULL);

	MMIO_F(PLANE_WM(PIPE_B, 0, 0), 4 * 8, 0, 0, 0, D_SKL_PLUS, NULL, NULL);
	MMIO_F(PLANE_WM(PIPE_B, 1, 0), 4 * 8, 0, 0, 0, D_SKL_PLUS, NULL, NULL);
	MMIO_F(PLANE_WM(PIPE_B, 2, 0), 4 * 8, 0, 0, 0, D_SKL_PLUS, NULL, NULL);

	MMIO_F(PLANE_WM(PIPE_C, 0, 0), 4 * 8, 0, 0, 0, D_SKL_PLUS, NULL, NULL);
	MMIO_F(PLANE_WM(PIPE_C, 1, 0), 4 * 8, 0, 0, 0, D_SKL_PLUS, NULL, NULL);
	MMIO_F(PLANE_WM(PIPE_C, 2, 0), 4 * 8, 0, 0, 0, D_SKL_PLUS, NULL, NULL);

	MMIO_F(CUR_WM(PIPE_A, 0), 4 * 8, 0, 0, 0, D_SKL_PLUS, NULL, NULL);
	MMIO_F(CUR_WM(PIPE_B, 0), 4 * 8, 0, 0, 0, D_SKL_PLUS, NULL, NULL);
	MMIO_F(CUR_WM(PIPE_C, 0), 4 * 8, 0, 0, 0, D_SKL_PLUS, NULL, NULL);

	MMIO_DH(PLANE_WM_TRANS(PIPE_A, 0), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(PLANE_WM_TRANS(PIPE_A, 1), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(PLANE_WM_TRANS(PIPE_A, 2), D_SKL_PLUS, NULL, NULL);

	MMIO_DH(PLANE_WM_TRANS(PIPE_B, 0), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(PLANE_WM_TRANS(PIPE_B, 1), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(PLANE_WM_TRANS(PIPE_B, 2), D_SKL_PLUS, NULL, NULL);

	MMIO_DH(PLANE_WM_TRANS(PIPE_C, 0), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(PLANE_WM_TRANS(PIPE_C, 1), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(PLANE_WM_TRANS(PIPE_C, 2), D_SKL_PLUS, NULL, NULL);

	MMIO_DH(CUR_WM_TRANS(PIPE_A), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(CUR_WM_TRANS(PIPE_B), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(CUR_WM_TRANS(PIPE_C), D_SKL_PLUS, NULL, NULL);

	MMIO_DH(PLANE_NV12_BUF_CFG(PIPE_A, 0), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(PLANE_NV12_BUF_CFG(PIPE_A, 1), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(PLANE_NV12_BUF_CFG(PIPE_A, 2), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(PLANE_NV12_BUF_CFG(PIPE_A, 3), D_SKL_PLUS, NULL, NULL);

	MMIO_DH(PLANE_NV12_BUF_CFG(PIPE_B, 0), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(PLANE_NV12_BUF_CFG(PIPE_B, 1), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(PLANE_NV12_BUF_CFG(PIPE_B, 2), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(PLANE_NV12_BUF_CFG(PIPE_B, 3), D_SKL_PLUS, NULL, NULL);

	MMIO_DH(PLANE_NV12_BUF_CFG(PIPE_C, 0), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(PLANE_NV12_BUF_CFG(PIPE_C, 1), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(PLANE_NV12_BUF_CFG(PIPE_C, 2), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(PLANE_NV12_BUF_CFG(PIPE_C, 3), D_SKL_PLUS, NULL, NULL);

	MMIO_DH(_REG_701C0(PIPE_A, 1), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(_REG_701C0(PIPE_A, 2), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(_REG_701C0(PIPE_A, 3), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(_REG_701C0(PIPE_A, 4), D_SKL_PLUS, NULL, NULL);

	MMIO_DH(_REG_701C0(PIPE_B, 1), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(_REG_701C0(PIPE_B, 2), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(_REG_701C0(PIPE_B, 3), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(_REG_701C0(PIPE_B, 4), D_SKL_PLUS, NULL, NULL);

	MMIO_DH(_REG_701C0(PIPE_C, 1), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(_REG_701C0(PIPE_C, 2), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(_REG_701C0(PIPE_C, 3), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(_REG_701C0(PIPE_C, 4), D_SKL_PLUS, NULL, NULL);

	MMIO_DH(_REG_701C4(PIPE_A, 1), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(_REG_701C4(PIPE_A, 2), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(_REG_701C4(PIPE_A, 3), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(_REG_701C4(PIPE_A, 4), D_SKL_PLUS, NULL, NULL);

	MMIO_DH(_REG_701C4(PIPE_B, 1), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(_REG_701C4(PIPE_B, 2), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(_REG_701C4(PIPE_B, 3), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(_REG_701C4(PIPE_B, 4), D_SKL_PLUS, NULL, NULL);

	MMIO_DH(_REG_701C4(PIPE_C, 1), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(_REG_701C4(PIPE_C, 2), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(_REG_701C4(PIPE_C, 3), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(_REG_701C4(PIPE_C, 4), D_SKL_PLUS, NULL, NULL);

	MMIO_D(0x70380, D_SKL_PLUS);
	MMIO_D(0x71380, D_SKL_PLUS);
	MMIO_D(0x72380, D_SKL_PLUS);
	MMIO_D(0x7039c, D_SKL_PLUS);

	MMIO_D(0x8f074, D_SKL | D_KBL);
	MMIO_D(0x8f004, D_SKL | D_KBL);
	MMIO_D(0x8f034, D_SKL | D_KBL);

	MMIO_D(0xb11c, D_SKL | D_KBL);

	MMIO_D(0x51000, D_SKL | D_KBL);
	MMIO_D(0x6c00c, D_SKL_PLUS);

	MMIO_F(0xc800, 0x7f8, F_CMD_ACCESS, 0, 0, D_SKL | D_KBL, NULL, NULL);
	MMIO_F(0xb020, 0x80, F_CMD_ACCESS, 0, 0, D_SKL | D_KBL, NULL, NULL);

	MMIO_D(0xd08, D_SKL_PLUS);
	MMIO_DFH(0x20e0, D_SKL_PLUS, F_MODE_MASK, NULL, NULL);
	MMIO_DFH(0x20ec, D_SKL_PLUS, F_MODE_MASK | F_CMD_ACCESS, NULL, NULL);

	/* TRTT */
	MMIO_DFH(0x4de0, D_SKL | D_KBL, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0x4de4, D_SKL | D_KBL, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0x4de8, D_SKL | D_KBL, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0x4dec, D_SKL | D_KBL, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0x4df0, D_SKL | D_KBL, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(0x4df4, D_SKL | D_KBL, F_CMD_ACCESS, NULL, gen9_trtte_write);
	MMIO_DH(0x4dfc, D_SKL | D_KBL, NULL, gen9_trtt_chicken_write);

	MMIO_D(0x45008, D_SKL | D_KBL);

	MMIO_D(0x46430, D_SKL | D_KBL);

	MMIO_D(0x46520, D_SKL | D_KBL);

	MMIO_D(0xc403c, D_SKL | D_KBL);
	MMIO_D(0xb004, D_SKL_PLUS);
	MMIO_DH(DMA_CTRL, D_SKL_PLUS, NULL, dma_ctrl_write);

	MMIO_D(0x65900, D_SKL_PLUS);
	MMIO_D(0x1082c0, D_SKL | D_KBL);
	MMIO_D(0x4068, D_SKL | D_KBL);
	MMIO_D(0x67054, D_SKL | D_KBL);
	MMIO_D(0x6e560, D_SKL | D_KBL);
	MMIO_D(0x6e554, D_SKL | D_KBL);
	MMIO_D(0x2b20, D_SKL | D_KBL);
	MMIO_D(0x65f00, D_SKL | D_KBL);
	MMIO_D(0x65f08, D_SKL | D_KBL);
	MMIO_D(0x320f0, D_SKL | D_KBL);

	MMIO_D(0x70034, D_SKL_PLUS);
	MMIO_D(0x71034, D_SKL_PLUS);
	MMIO_D(0x72034, D_SKL_PLUS);

	MMIO_D(_PLANE_KEYVAL_1(PIPE_A), D_SKL_PLUS);
	MMIO_D(_PLANE_KEYVAL_1(PIPE_B), D_SKL_PLUS);
	MMIO_D(_PLANE_KEYVAL_1(PIPE_C), D_SKL_PLUS);
	MMIO_D(_PLANE_KEYMSK_1(PIPE_A), D_SKL_PLUS);
	MMIO_D(_PLANE_KEYMSK_1(PIPE_B), D_SKL_PLUS);
	MMIO_D(_PLANE_KEYMSK_1(PIPE_C), D_SKL_PLUS);

	MMIO_D(0x44500, D_SKL_PLUS);
	MMIO_DFH(GEN9_CSFE_CHICKEN1_RCS, D_SKL_PLUS, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(GEN8_HDC_CHICKEN1, D_SKL | D_KBL, F_MODE_MASK | F_CMD_ACCESS,
		NULL, NULL);

	MMIO_D(0x4ab8, D_KBL);
	MMIO_D(0x2248, D_SKL_PLUS | D_KBL);

	return 0;
}

static struct gvt_mmio_block *find_mmio_block(struct intel_gvt *gvt,
					      unsigned int offset)
{
	unsigned long device = intel_gvt_get_device_type(gvt);
	struct gvt_mmio_block *block = gvt->mmio.mmio_block;
	int num = gvt->mmio.num_mmio_block;
	int i;

	for (i = 0; i < num; i++, block++) {
		if (!(device & block->device))
			continue;
		if (offset >= INTEL_GVT_MMIO_OFFSET(block->offset) &&
		    offset < INTEL_GVT_MMIO_OFFSET(block->offset) + block->size)
			return block;
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

/* Special MMIO blocks. */
static struct gvt_mmio_block mmio_blocks[] = {
	{D_SKL_PLUS, _MMIO(CSR_MMIO_START_RANGE), 0x3000, NULL, NULL},
	{D_ALL, _MMIO(MCHBAR_MIRROR_BASE_SNB), 0x40000, NULL, NULL},
	{D_ALL, _MMIO(VGT_PVINFO_PAGE), VGT_PVINFO_SIZE,
		pvinfo_mmio_read, pvinfo_mmio_write},
	{D_ALL, LGC_PALETTE(PIPE_A, 0), 1024, NULL, NULL},
	{D_ALL, LGC_PALETTE(PIPE_B, 0), 1024, NULL, NULL},
	{D_ALL, LGC_PALETTE(PIPE_C, 0), 1024, NULL, NULL},
};

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
	int size = info->mmio_size / 4 * sizeof(*gvt->mmio.mmio_attribute);
	int ret;

	gvt->mmio.mmio_attribute = vzalloc(size);
	if (!gvt->mmio.mmio_attribute)
		return -ENOMEM;

	ret = init_generic_mmio_info(gvt);
	if (ret)
		goto err;

	if (IS_BROADWELL(dev_priv)) {
		ret = init_broadwell_mmio_info(gvt);
		if (ret)
			goto err;
	} else if (IS_SKYLAKE(dev_priv)
		|| IS_KABYLAKE(dev_priv)) {
		ret = init_broadwell_mmio_info(gvt);
		if (ret)
			goto err;
		ret = init_skl_mmio_info(gvt);
		if (ret)
			goto err;
	}

	gvt->mmio.mmio_block = mmio_blocks;
	gvt->mmio.num_mmio_block = ARRAY_SIZE(mmio_blocks);

	return 0;
err:
	intel_gvt_clean_mmio_info(gvt);
	return ret;
}

/**
 * intel_gvt_for_each_tracked_mmio - iterate each tracked mmio
 * @gvt: a GVT device
 * @handler: the handler
 * @data: private data given to handler
 *
 * Returns:
 * Zero on success, negative error code if failed.
 */
int intel_gvt_for_each_tracked_mmio(struct intel_gvt *gvt,
	int (*handler)(struct intel_gvt *gvt, u32 offset, void *data),
	void *data)
{
	struct gvt_mmio_block *block = gvt->mmio.mmio_block;
	struct intel_gvt_mmio_info *e;
	int i, j, ret;

	hash_for_each(gvt->mmio.mmio_info_table, i, e, node) {
		ret = handler(gvt, e->offset, data);
		if (ret)
			return ret;
	}

	for (i = 0; i < gvt->mmio.num_mmio_block; i++, block++) {
		for (j = 0; j < block->size; j += 4) {
			ret = handler(gvt,
				INTEL_GVT_MMIO_OFFSET(block->offset) + j,
				data);
			if (ret)
				return ret;
		}
	}
	return 0;
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

/**
 * intel_gvt_in_force_nonpriv_whitelist - if a mmio is in whitelist to be
 * force-nopriv register
 *
 * @gvt: a GVT device
 * @offset: register offset
 *
 * Returns:
 * True if the register is in force-nonpriv whitelist;
 * False if outside;
 */
bool intel_gvt_in_force_nonpriv_whitelist(struct intel_gvt *gvt,
					  unsigned int offset)
{
	return in_whitelist(offset);
}

/**
 * intel_vgpu_mmio_reg_rw - emulate tracked mmio registers
 * @vgpu: a vGPU
 * @offset: register offset
 * @pdata: data buffer
 * @bytes: data length
 *
 * Returns:
 * Zero on success, negative error code if failed.
 */
int intel_vgpu_mmio_reg_rw(struct intel_vgpu *vgpu, unsigned int offset,
			   void *pdata, unsigned int bytes, bool is_read)
{
	struct intel_gvt *gvt = vgpu->gvt;
	struct intel_gvt_mmio_info *mmio_info;
	struct gvt_mmio_block *mmio_block;
	gvt_mmio_func func;
	int ret;

	if (WARN_ON(bytes > 8))
		return -EINVAL;

	/*
	 * Handle special MMIO blocks.
	 */
	mmio_block = find_mmio_block(gvt, offset);
	if (mmio_block) {
		func = is_read ? mmio_block->read : mmio_block->write;
		if (func)
			return func(vgpu, offset, pdata, bytes);
		goto default_rw;
	}

	/*
	 * Normal tracked MMIOs.
	 */
	mmio_info = find_mmio_info(gvt, offset);
	if (!mmio_info) {
		if (!vgpu->mmio.disable_warn_untrack)
			gvt_vgpu_err("untracked MMIO %08x len %d\n",
				     offset, bytes);
		goto default_rw;
	}

	if (is_read)
		return mmio_info->read(vgpu, offset, pdata, bytes);
	else {
		u64 ro_mask = mmio_info->ro_mask;
		u32 old_vreg = 0, old_sreg = 0;
		u64 data = 0;

		if (intel_gvt_mmio_has_mode_mask(gvt, mmio_info->offset)) {
			old_vreg = vgpu_vreg(vgpu, offset);
			old_sreg = vgpu_sreg(vgpu, offset);
		}

		if (likely(!ro_mask))
			ret = mmio_info->write(vgpu, offset, pdata, bytes);
		else if (!~ro_mask) {
			gvt_vgpu_err("try to write RO reg %x\n", offset);
			return 0;
		} else {
			/* keep the RO bits in the virtual register */
			memcpy(&data, pdata, bytes);
			data &= ~ro_mask;
			data |= vgpu_vreg(vgpu, offset) & ro_mask;
			ret = mmio_info->write(vgpu, offset, &data, bytes);
		}

		/* higher 16bits of mode ctl regs are mask bits for change */
		if (intel_gvt_mmio_has_mode_mask(gvt, mmio_info->offset)) {
			u32 mask = vgpu_vreg(vgpu, offset) >> 16;

			vgpu_vreg(vgpu, offset) = (old_vreg & ~mask)
					| (vgpu_vreg(vgpu, offset) & mask);
			vgpu_sreg(vgpu, offset) = (old_sreg & ~mask)
					| (vgpu_sreg(vgpu, offset) & mask);
		}
	}

	return ret;

default_rw:
	return is_read ?
		intel_vgpu_default_mmio_read(vgpu, offset, pdata, bytes) :
		intel_vgpu_default_mmio_write(vgpu, offset, pdata, bytes);
}
