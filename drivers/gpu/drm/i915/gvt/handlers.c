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
#include "display/intel_display_types.h"

/* XXX FIXME i915 has changed PP_XXX definition */
#define PCH_PP_STATUS  _MMIO(0xc7200)
#define PCH_PP_CONTROL _MMIO(0xc7204)
#define PCH_PP_ON_DELAYS _MMIO(0xc7208)
#define PCH_PP_OFF_DELAYS _MMIO(0xc720c)
#define PCH_PP_DIVISOR _MMIO(0xc7210)

unsigned long intel_gvt_get_device_type(struct intel_gvt *gvt)
{
	struct drm_i915_private *i915 = gvt->gt->i915;

	if (IS_BROADWELL(i915))
		return D_BDW;
	else if (IS_SKYLAKE(i915))
		return D_SKL;
	else if (IS_KABYLAKE(i915))
		return D_KBL;
	else if (IS_BROXTON(i915))
		return D_BXT;
	else if (IS_COFFEELAKE(i915) || IS_COMETLAKE(i915))
		return D_CFL;

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

struct intel_gvt_mmio_info *intel_gvt_find_mmio_info(struct intel_gvt *gvt,
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
		u32 offset, u16 flags, u32 size,
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
		p = intel_gvt_find_mmio_info(gvt, info->offset);
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
 * intel_gvt_render_mmio_to_engine - convert a mmio offset into the engine
 * @gvt: a GVT device
 * @offset: register offset
 *
 * Returns:
 * The engine containing the offset within its mmio page.
 */
const struct intel_engine_cs *
intel_gvt_render_mmio_to_engine(struct intel_gvt *gvt, unsigned int offset)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	offset &= ~GENMASK(11, 0);
	for_each_engine(engine, gvt->gt, id)
		if (engine->mmio_base == offset)
			return engine;

	return NULL;
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
		break;
	case GVT_FAILSAFE_GUEST_ERR:
		pr_err("GVT Internal error  for the guest\n");
		break;
	default:
		break;
	}
	pr_err("Now vgpu %d will enter failsafe mode.\n", vgpu->id);
	vgpu->failsafe = true;
}

static int sanitize_fence_mmio_access(struct intel_vgpu *vgpu,
		unsigned int fence_num, void *p_data, unsigned int bytes)
{
	unsigned int max_fence = vgpu_fence_sz(vgpu);

	if (fence_num >= max_fence) {
		gvt_vgpu_err("access oob fence reg %d/%d\n",
			     fence_num, max_fence);

		/* When guest access oob fence regs without access
		 * pv_info first, we treat guest not supporting GVT,
		 * and we will let vgpu enter failsafe mode.
		 */
		if (!vgpu->pv_notified)
			enter_failsafe_mode(vgpu,
					GVT_FAILSAFE_UNSUPPORTED_GUEST);

		memset(p_data, 0, bytes);
		return -EINVAL;
	}
	return 0;
}

static int gamw_echo_dev_rw_ia_write(struct intel_vgpu *vgpu,
		unsigned int offset, void *p_data, unsigned int bytes)
{
	u32 ips = (*(u32 *)p_data) & GAMW_ECO_ENABLE_64K_IPS_FIELD;

	if (INTEL_GEN(vgpu->gvt->gt->i915) <= 10) {
		if (ips == GAMW_ECO_ENABLE_64K_IPS_FIELD)
			gvt_dbg_core("vgpu%d: ips enabled\n", vgpu->id);
		else if (!ips)
			gvt_dbg_core("vgpu%d: ips disabled\n", vgpu->id);
		else {
			/* All engines must be enabled together for vGPU,
			 * since we don't know which engine the ppgtt will
			 * bind to when shadowing.
			 */
			gvt_vgpu_err("Unsupported IPS setting %x, cannot enable 64K gtt.\n",
				     ips);
			return -EINVAL;
		}
	}

	write_vreg(vgpu, offset, p_data, bytes);
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
	struct intel_gvt *gvt = vgpu->gvt;
	unsigned int fence_num = offset_to_fence_num(off);
	int ret;

	ret = sanitize_fence_mmio_access(vgpu, fence_num, p_data, bytes);
	if (ret)
		return ret;
	write_vreg(vgpu, off, p_data, bytes);

	mmio_hw_access_pre(gvt->gt);
	intel_vgpu_write_fence(vgpu, fence_num,
			vgpu_vreg64(vgpu, fence_num_to_offset(fence_num)));
	mmio_hw_access_post(gvt->gt);
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
	u32 ack_reg_offset;

	old = vgpu_vreg(vgpu, offset);
	new = CALC_MODE_MASK_REG(old, *(u32 *)p_data);

	if (INTEL_GEN(vgpu->gvt->gt->i915)  >=  9) {
		switch (offset) {
		case FORCEWAKE_RENDER_GEN9_REG:
			ack_reg_offset = FORCEWAKE_ACK_RENDER_GEN9_REG;
			break;
		case FORCEWAKE_GT_GEN9_REG:
			ack_reg_offset = FORCEWAKE_ACK_GT_GEN9_REG;
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
	intel_engine_mask_t engine_mask = 0;
	u32 data;

	write_vreg(vgpu, offset, p_data, bytes);
	data = vgpu_vreg(vgpu, offset);

	if (data & GEN6_GRDOM_FULL) {
		gvt_dbg_mmio("vgpu%d: request full GPU reset\n", vgpu->id);
		engine_mask = ALL_ENGINES;
	} else {
		if (data & GEN6_GRDOM_RENDER) {
			gvt_dbg_mmio("vgpu%d: request RCS reset\n", vgpu->id);
			engine_mask |= BIT(RCS0);
		}
		if (data & GEN6_GRDOM_MEDIA) {
			gvt_dbg_mmio("vgpu%d: request VCS reset\n", vgpu->id);
			engine_mask |= BIT(VCS0);
		}
		if (data & GEN6_GRDOM_BLT) {
			gvt_dbg_mmio("vgpu%d: request BCS Reset\n", vgpu->id);
			engine_mask |= BIT(BCS0);
		}
		if (data & GEN6_GRDOM_VECS) {
			gvt_dbg_mmio("vgpu%d: request VECS Reset\n", vgpu->id);
			engine_mask |= BIT(VECS0);
		}
		if (data & GEN8_GRDOM_MEDIA2) {
			gvt_dbg_mmio("vgpu%d: request VCS2 Reset\n", vgpu->id);
			engine_mask |= BIT(VCS1);
		}
		if (data & GEN9_GRDOM_GUC) {
			gvt_dbg_mmio("vgpu%d: request GUC Reset\n", vgpu->id);
			vgpu_vreg_t(vgpu, GUC_STATUS) |= GS_MIA_IN_RESET;
		}
		engine_mask &= vgpu->gvt->gt->info.engine_mask;
	}

	/* vgpu_lock already hold by emulate mmio r/w */
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
		vgpu_vreg_t(vgpu, PCH_PP_STATUS) |= PP_ON;
		vgpu_vreg_t(vgpu, PCH_PP_STATUS) |= PP_SEQUENCE_STATE_ON_IDLE;
		vgpu_vreg_t(vgpu, PCH_PP_STATUS) &= ~PP_SEQUENCE_POWER_DOWN;
		vgpu_vreg_t(vgpu, PCH_PP_STATUS) &= ~PP_CYCLE_DELAY_ACTIVE;

	} else
		vgpu_vreg_t(vgpu, PCH_PP_STATUS) &=
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

/*
 * Only PIPE_A is enabled in current vGPU display and PIPE_A is tied to
 *   TRANSCODER_A in HW. DDI/PORT could be PORT_x depends on
 *   setup_virtual_dp_monitor().
 * emulate_monitor_status_change() set up PLL for PORT_x as the initial enabled
 *   DPLL. Later guest driver may setup a different DPLLx when setting mode.
 * So the correct sequence to find DP stream clock is:
 *   Check TRANS_DDI_FUNC_CTL on TRANSCODER_A to get PORT_x.
 *   Check correct PLLx for PORT_x to get PLL frequency and DP bitrate.
 * Then Refresh rate then can be calculated based on follow equations:
 *   Pixel clock = h_total * v_total * refresh_rate
 *   stream clock = Pixel clock
 *   ls_clk = DP bitrate
 *   Link M/N = strm_clk / ls_clk
 */

static u32 bdw_vgpu_get_dp_bitrate(struct intel_vgpu *vgpu, enum port port)
{
	u32 dp_br = 0;
	u32 ddi_pll_sel = vgpu_vreg_t(vgpu, PORT_CLK_SEL(port));

	switch (ddi_pll_sel) {
	case PORT_CLK_SEL_LCPLL_2700:
		dp_br = 270000 * 2;
		break;
	case PORT_CLK_SEL_LCPLL_1350:
		dp_br = 135000 * 2;
		break;
	case PORT_CLK_SEL_LCPLL_810:
		dp_br = 81000 * 2;
		break;
	case PORT_CLK_SEL_SPLL:
	{
		switch (vgpu_vreg_t(vgpu, SPLL_CTL) & SPLL_FREQ_MASK) {
		case SPLL_FREQ_810MHz:
			dp_br = 81000 * 2;
			break;
		case SPLL_FREQ_1350MHz:
			dp_br = 135000 * 2;
			break;
		case SPLL_FREQ_2700MHz:
			dp_br = 270000 * 2;
			break;
		default:
			gvt_dbg_dpy("vgpu-%d PORT_%c can't get freq from SPLL 0x%08x\n",
				    vgpu->id, port_name(port), vgpu_vreg_t(vgpu, SPLL_CTL));
			break;
		}
		break;
	}
	case PORT_CLK_SEL_WRPLL1:
	case PORT_CLK_SEL_WRPLL2:
	{
		u32 wrpll_ctl;
		int refclk, n, p, r;

		if (ddi_pll_sel == PORT_CLK_SEL_WRPLL1)
			wrpll_ctl = vgpu_vreg_t(vgpu, WRPLL_CTL(DPLL_ID_WRPLL1));
		else
			wrpll_ctl = vgpu_vreg_t(vgpu, WRPLL_CTL(DPLL_ID_WRPLL2));

		switch (wrpll_ctl & WRPLL_REF_MASK) {
		case WRPLL_REF_PCH_SSC:
			refclk = vgpu->gvt->gt->i915->dpll.ref_clks.ssc;
			break;
		case WRPLL_REF_LCPLL:
			refclk = 2700000;
			break;
		default:
			gvt_dbg_dpy("vgpu-%d PORT_%c WRPLL can't get refclk 0x%08x\n",
				    vgpu->id, port_name(port), wrpll_ctl);
			goto out;
		}

		r = wrpll_ctl & WRPLL_DIVIDER_REF_MASK;
		p = (wrpll_ctl & WRPLL_DIVIDER_POST_MASK) >> WRPLL_DIVIDER_POST_SHIFT;
		n = (wrpll_ctl & WRPLL_DIVIDER_FB_MASK) >> WRPLL_DIVIDER_FB_SHIFT;

		dp_br = (refclk * n / 10) / (p * r) * 2;
		break;
	}
	default:
		gvt_dbg_dpy("vgpu-%d PORT_%c has invalid clock select 0x%08x\n",
			    vgpu->id, port_name(port), vgpu_vreg_t(vgpu, PORT_CLK_SEL(port)));
		break;
	}

out:
	return dp_br;
}

static u32 bxt_vgpu_get_dp_bitrate(struct intel_vgpu *vgpu, enum port port)
{
	u32 dp_br = 0;
	int refclk = vgpu->gvt->gt->i915->dpll.ref_clks.nssc;
	enum dpio_phy phy = DPIO_PHY0;
	enum dpio_channel ch = DPIO_CH0;
	struct dpll clock = {0};
	u32 temp;

	/* Port to PHY mapping is fixed, see bxt_ddi_phy_info{} */
	switch (port) {
	case PORT_A:
		phy = DPIO_PHY1;
		ch = DPIO_CH0;
		break;
	case PORT_B:
		phy = DPIO_PHY0;
		ch = DPIO_CH0;
		break;
	case PORT_C:
		phy = DPIO_PHY0;
		ch = DPIO_CH1;
		break;
	default:
		gvt_dbg_dpy("vgpu-%d no PHY for PORT_%c\n", vgpu->id, port_name(port));
		goto out;
	}

	temp = vgpu_vreg_t(vgpu, BXT_PORT_PLL_ENABLE(port));
	if (!(temp & PORT_PLL_ENABLE) || !(temp & PORT_PLL_LOCK)) {
		gvt_dbg_dpy("vgpu-%d PORT_%c PLL_ENABLE 0x%08x isn't enabled or locked\n",
			    vgpu->id, port_name(port), temp);
		goto out;
	}

	clock.m1 = 2;
	clock.m2 = (vgpu_vreg_t(vgpu, BXT_PORT_PLL(phy, ch, 0)) & PORT_PLL_M2_MASK) << 22;
	if (vgpu_vreg_t(vgpu, BXT_PORT_PLL(phy, ch, 3)) & PORT_PLL_M2_FRAC_ENABLE)
		clock.m2 |= vgpu_vreg_t(vgpu, BXT_PORT_PLL(phy, ch, 2)) & PORT_PLL_M2_FRAC_MASK;
	clock.n = (vgpu_vreg_t(vgpu, BXT_PORT_PLL(phy, ch, 1)) & PORT_PLL_N_MASK) >> PORT_PLL_N_SHIFT;
	clock.p1 = (vgpu_vreg_t(vgpu, BXT_PORT_PLL_EBB_0(phy, ch)) & PORT_PLL_P1_MASK) >> PORT_PLL_P1_SHIFT;
	clock.p2 = (vgpu_vreg_t(vgpu, BXT_PORT_PLL_EBB_0(phy, ch)) & PORT_PLL_P2_MASK) >> PORT_PLL_P2_SHIFT;
	clock.m = clock.m1 * clock.m2;
	clock.p = clock.p1 * clock.p2;

	if (clock.n == 0 || clock.p == 0) {
		gvt_dbg_dpy("vgpu-%d PORT_%c PLL has invalid divider\n", vgpu->id, port_name(port));
		goto out;
	}

	clock.vco = DIV_ROUND_CLOSEST_ULL(mul_u32_u32(refclk, clock.m), clock.n << 22);
	clock.dot = DIV_ROUND_CLOSEST(clock.vco, clock.p);

	dp_br = clock.dot / 5;

out:
	return dp_br;
}

static u32 skl_vgpu_get_dp_bitrate(struct intel_vgpu *vgpu, enum port port)
{
	u32 dp_br = 0;
	enum intel_dpll_id dpll_id = DPLL_ID_SKL_DPLL0;

	/* Find the enabled DPLL for the DDI/PORT */
	if (!(vgpu_vreg_t(vgpu, DPLL_CTRL2) & DPLL_CTRL2_DDI_CLK_OFF(port)) &&
	    (vgpu_vreg_t(vgpu, DPLL_CTRL2) & DPLL_CTRL2_DDI_SEL_OVERRIDE(port))) {
		dpll_id += (vgpu_vreg_t(vgpu, DPLL_CTRL2) &
			DPLL_CTRL2_DDI_CLK_SEL_MASK(port)) >>
			DPLL_CTRL2_DDI_CLK_SEL_SHIFT(port);
	} else {
		gvt_dbg_dpy("vgpu-%d DPLL for PORT_%c isn't turned on\n",
			    vgpu->id, port_name(port));
		return dp_br;
	}

	/* Find PLL output frequency from correct DPLL, and get bir rate */
	switch ((vgpu_vreg_t(vgpu, DPLL_CTRL1) &
		DPLL_CTRL1_LINK_RATE_MASK(dpll_id)) >>
		DPLL_CTRL1_LINK_RATE_SHIFT(dpll_id)) {
		case DPLL_CTRL1_LINK_RATE_810:
			dp_br = 81000 * 2;
			break;
		case DPLL_CTRL1_LINK_RATE_1080:
			dp_br = 108000 * 2;
			break;
		case DPLL_CTRL1_LINK_RATE_1350:
			dp_br = 135000 * 2;
			break;
		case DPLL_CTRL1_LINK_RATE_1620:
			dp_br = 162000 * 2;
			break;
		case DPLL_CTRL1_LINK_RATE_2160:
			dp_br = 216000 * 2;
			break;
		case DPLL_CTRL1_LINK_RATE_2700:
			dp_br = 270000 * 2;
			break;
		default:
			dp_br = 0;
			gvt_dbg_dpy("vgpu-%d PORT_%c fail to get DPLL-%d freq\n",
				    vgpu->id, port_name(port), dpll_id);
	}

	return dp_br;
}

static void vgpu_update_refresh_rate(struct intel_vgpu *vgpu)
{
	struct drm_i915_private *dev_priv = vgpu->gvt->gt->i915;
	enum port port;
	u32 dp_br, link_m, link_n, htotal, vtotal;

	/* Find DDI/PORT assigned to TRANSCODER_A, expect B or D */
	port = (vgpu_vreg_t(vgpu, TRANS_DDI_FUNC_CTL(TRANSCODER_A)) &
		TRANS_DDI_PORT_MASK) >> TRANS_DDI_PORT_SHIFT;
	if (port != PORT_B && port != PORT_D) {
		gvt_dbg_dpy("vgpu-%d unsupported PORT_%c\n", vgpu->id, port_name(port));
		return;
	}

	/* Calculate DP bitrate from PLL */
	if (IS_BROADWELL(dev_priv))
		dp_br = bdw_vgpu_get_dp_bitrate(vgpu, port);
	else if (IS_BROXTON(dev_priv))
		dp_br = bxt_vgpu_get_dp_bitrate(vgpu, port);
	else
		dp_br = skl_vgpu_get_dp_bitrate(vgpu, port);

	/* Get DP link symbol clock M/N */
	link_m = vgpu_vreg_t(vgpu, PIPE_LINK_M1(TRANSCODER_A));
	link_n = vgpu_vreg_t(vgpu, PIPE_LINK_N1(TRANSCODER_A));

	/* Get H/V total from transcoder timing */
	htotal = (vgpu_vreg_t(vgpu, HTOTAL(TRANSCODER_A)) >> TRANS_HTOTAL_SHIFT);
	vtotal = (vgpu_vreg_t(vgpu, VTOTAL(TRANSCODER_A)) >> TRANS_VTOTAL_SHIFT);

	if (dp_br && link_n && htotal && vtotal) {
		u64 pixel_clk = 0;
		u32 new_rate = 0;
		u32 *old_rate = &(intel_vgpu_port(vgpu, vgpu->display.port_num)->vrefresh_k);

		/* Calcuate pixel clock by (ls_clk * M / N) */
		pixel_clk = div_u64(mul_u32_u32(link_m, dp_br), link_n);
		pixel_clk *= MSEC_PER_SEC;

		/* Calcuate refresh rate by (pixel_clk / (h_total * v_total)) */
		new_rate = DIV64_U64_ROUND_CLOSEST(mul_u64_u32_shr(pixel_clk, MSEC_PER_SEC, 0), mul_u32_u32(htotal + 1, vtotal + 1));

		if (*old_rate != new_rate)
			*old_rate = new_rate;

		gvt_dbg_dpy("vgpu-%d PIPE_%c refresh rate updated to %d\n",
			    vgpu->id, pipe_name(PIPE_A), new_rate);
	}
}

static int pipeconf_mmio_write(struct intel_vgpu *vgpu, unsigned int offset,
		void *p_data, unsigned int bytes)
{
	u32 data;

	write_vreg(vgpu, offset, p_data, bytes);
	data = vgpu_vreg(vgpu, offset);

	if (data & PIPECONF_ENABLE) {
		vgpu_vreg(vgpu, offset) |= I965_PIPECONF_ACTIVE;
		vgpu_update_refresh_rate(vgpu);
		vgpu_update_vblank_emulation(vgpu, true);
	} else {
		vgpu_vreg(vgpu, offset) &= ~I965_PIPECONF_ACTIVE;
		vgpu_update_vblank_emulation(vgpu, false);
	}
	return 0;
}

/* sorted in ascending order */
static i915_reg_t force_nonpriv_white_list[] = {
	_MMIO(0xd80),
	GEN9_CS_DEBUG_MODE1, //_MMIO(0x20ec)
	GEN9_CTX_PREEMPT_REG,//_MMIO(0x2248)
	CL_PRIMITIVES_COUNT, //_MMIO(0x2340)
	PS_INVOCATION_COUNT, //_MMIO(0x2348)
	PS_DEPTH_COUNT, //_MMIO(0x2350)
	GEN8_CS_CHICKEN1,//_MMIO(0x2580)
	_MMIO(0x2690),
	_MMIO(0x2694),
	_MMIO(0x2698),
	_MMIO(0x2754),
	_MMIO(0x28a0),
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
	_MMIO(0x83a8),
	_MMIO(0xb110),
	GEN8_L3SQCREG4,//_MMIO(0xb118)
	_MMIO(0xe100),
	_MMIO(0xe18c),
	_MMIO(0xe48c),
	_MMIO(0xe5f4),
	_MMIO(0x64844),
};

/* a simple bsearch */
static inline bool in_whitelist(u32 reg)
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
	u32 reg_nonpriv = (*(u32 *)p_data) & REG_GENMASK(25, 2);
	const struct intel_engine_cs *engine =
		intel_gvt_render_mmio_to_engine(vgpu->gvt, offset);

	if (bytes != 4 || !IS_ALIGNED(offset, bytes) || !engine) {
		gvt_err("vgpu(%d) Invalid FORCE_NONPRIV offset %x(%dB)\n",
			vgpu->id, offset, bytes);
		return -EINVAL;
	}

	if (!in_whitelist(reg_nonpriv) &&
	    reg_nonpriv != i915_mmio_reg_offset(RING_NOPID(engine->mmio_base))) {
		gvt_err("vgpu(%d) Invalid FORCE_NONPRIV write %x at offset %x\n",
			vgpu->id, reg_nonpriv, offset);
	} else
		intel_vgpu_default_mmio_write(vgpu, offset, p_data, bytes);

	return 0;
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
			vgpu_vreg_t(vgpu, DP_TP_STATUS(PORT_E))
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
	u32 ddi_buf_ctl = vgpu_vreg_t(vgpu, DDI_BUF_CTL(PORT_E));
	u32 rx_ctl = vgpu_vreg(vgpu, _FDI_RXA_CTL);
	u32 tx_ctl = vgpu_vreg_t(vgpu, DP_TP_CTL(PORT_E));

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
	if (vgpu_vreg_t(vgpu, fdi_rx_imr) & fdi_iir_check_bits)
		return 0;

	if (((vgpu_vreg_t(vgpu, fdi_tx_ctl) & fdi_tx_check_bits)
			== fdi_tx_check_bits)
		&& ((vgpu_vreg_t(vgpu, fdi_rx_ctl) & fdi_rx_check_bits)
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
		vgpu_vreg_t(vgpu, fdi_rx_iir) |= FDI_RX_BIT_LOCK;

	ret = check_fdi_rx_train_status(vgpu, index, FDI_LINK_TRAIN_PATTERN2);
	if (ret < 0)
		return ret;
	if (ret)
		vgpu_vreg_t(vgpu, fdi_rx_iir) |= FDI_RX_SYMBOL_LOCK;

	if (offset == _FDI_RXA_CTL)
		if (fdi_auto_training_started(vgpu))
			vgpu_vreg_t(vgpu, DP_TP_STATUS(PORT_E)) |=
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
		vgpu_vreg_t(vgpu, status_reg) |= (1 << 25);
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
	struct drm_i915_private *dev_priv = vgpu->gvt->gt->i915;
	u32 pipe = DSPSURF_TO_PIPE(offset);
	int event = SKL_FLIP_EVENT(pipe, PLANE_PRIMARY);

	write_vreg(vgpu, offset, p_data, bytes);
	vgpu_vreg_t(vgpu, DSPSURFLIVE(pipe)) = vgpu_vreg(vgpu, offset);

	vgpu_vreg_t(vgpu, PIPE_FLIPCOUNT_G4X(pipe))++;

	if (vgpu_vreg_t(vgpu, DSPCNTR(pipe)) & PLANE_CTL_ASYNC_FLIP)
		intel_vgpu_trigger_virtual_event(vgpu, event);
	else
		set_bit(event, vgpu->irq.flip_done_event[pipe]);

	return 0;
}

#define SPRSURF_TO_PIPE(offset) \
	calc_index(offset, _SPRA_SURF, _SPRB_SURF, 0, SPRSURF(PIPE_C))

static int spr_surf_mmio_write(struct intel_vgpu *vgpu, unsigned int offset,
		void *p_data, unsigned int bytes)
{
	u32 pipe = SPRSURF_TO_PIPE(offset);
	int event = SKL_FLIP_EVENT(pipe, PLANE_SPRITE0);

	write_vreg(vgpu, offset, p_data, bytes);
	vgpu_vreg_t(vgpu, SPRSURFLIVE(pipe)) = vgpu_vreg(vgpu, offset);

	if (vgpu_vreg_t(vgpu, SPRCTL(pipe)) & PLANE_CTL_ASYNC_FLIP)
		intel_vgpu_trigger_virtual_event(vgpu, event);
	else
		set_bit(event, vgpu->irq.flip_done_event[pipe]);

	return 0;
}

static int reg50080_mmio_write(struct intel_vgpu *vgpu,
			       unsigned int offset, void *p_data,
			       unsigned int bytes)
{
	struct drm_i915_private *dev_priv = vgpu->gvt->gt->i915;
	enum pipe pipe = REG_50080_TO_PIPE(offset);
	enum plane_id plane = REG_50080_TO_PLANE(offset);
	int event = SKL_FLIP_EVENT(pipe, plane);

	write_vreg(vgpu, offset, p_data, bytes);
	if (plane == PLANE_PRIMARY) {
		vgpu_vreg_t(vgpu, DSPSURFLIVE(pipe)) = vgpu_vreg(vgpu, offset);
		vgpu_vreg_t(vgpu, PIPE_FLIPCOUNT_G4X(pipe))++;
	} else {
		vgpu_vreg_t(vgpu, SPRSURFLIVE(pipe)) = vgpu_vreg(vgpu, offset);
	}

	if ((vgpu_vreg(vgpu, offset) & REG50080_FLIP_TYPE_MASK) == REG50080_FLIP_TYPE_ASYNC)
		intel_vgpu_trigger_virtual_event(vgpu, event);
	else
		set_bit(event, vgpu->irq.flip_done_event[pipe]);

	return 0;
}

static int trigger_aux_channel_interrupt(struct intel_vgpu *vgpu,
		unsigned int reg)
{
	struct drm_i915_private *dev_priv = vgpu->gvt->gt->i915;
	enum intel_gvt_event_type event;

	if (reg == i915_mmio_reg_offset(DP_AUX_CH_CTL(AUX_CH_A)))
		event = AUX_CHANNEL_A;
	else if (reg == _PCH_DPB_AUX_CH_CTL ||
		 reg == i915_mmio_reg_offset(DP_AUX_CH_CTL(AUX_CH_B)))
		event = AUX_CHANNEL_B;
	else if (reg == _PCH_DPC_AUX_CH_CTL ||
		 reg == i915_mmio_reg_offset(DP_AUX_CH_CTL(AUX_CH_C)))
		event = AUX_CHANNEL_C;
	else if (reg == _PCH_DPD_AUX_CH_CTL ||
		 reg == i915_mmio_reg_offset(DP_AUX_CH_CTL(AUX_CH_D)))
		event = AUX_CHANNEL_D;
	else {
		drm_WARN_ON(&dev_priv->drm, true);
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
		u8 t)
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

	if ((INTEL_GEN(vgpu->gvt->gt->i915) >= 9)
		&& offset != _REG_SKL_DP_AUX_CH_CTL(port_index)) {
		/* SKL DPB/C/D aux ctl register changed */
		return 0;
	} else if (IS_BROADWELL(vgpu->gvt->gt->i915) &&
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
		u8 buf[16];

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
		 * Write request format: Headr (command + address + size) occupies
		 * 4 bytes, followed by (len + 1) bytes of data. See details at
		 * intel_dp_aux_transfer().
		 */
		if ((len + 1 + 4) > AUX_BURST_SIZE) {
			gvt_vgpu_err("dp_aux_header: len %d is too large\n", len);
			return -EINVAL;
		}

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
		if ((len + 2) > AUX_BURST_SIZE) {
			gvt_vgpu_err("dp_aux_header: len %d is too large\n", len);
			return -EINVAL;
		}

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
	if (((vgpu_vreg_t(vgpu, SBI_CTL_STAT) & SBI_OPCODE_MASK) >>
				SBI_OPCODE_SHIFT) == SBI_CMD_CRRD) {
		unsigned int sbi_offset = (vgpu_vreg_t(vgpu, SBI_ADDR) &
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

	if (((vgpu_vreg_t(vgpu, SBI_CTL_STAT) & SBI_OPCODE_MASK) >>
				SBI_OPCODE_SHIFT) == SBI_CMD_CRWR) {
		unsigned int sbi_offset = (vgpu_vreg_t(vgpu, SBI_ADDR) &
				SBI_ADDR_OFFSET_MASK) >> SBI_ADDR_OFFSET_SHIFT;

		write_virtual_sbi_register(vgpu, sbi_offset,
					   vgpu_vreg_t(vgpu, SBI_DATA));
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
	enum intel_gvt_gtt_type root_entry_type = GTT_TYPE_PPGTT_ROOT_L4_ENTRY;
	struct intel_vgpu_mm *mm;
	u64 *pdps;

	pdps = (u64 *)&vgpu_vreg64_t(vgpu, vgtif_reg(pdp[0]));

	switch (notification) {
	case VGT_G2V_PPGTT_L3_PAGE_TABLE_CREATE:
		root_entry_type = GTT_TYPE_PPGTT_ROOT_L3_ENTRY;
		fallthrough;
	case VGT_G2V_PPGTT_L4_PAGE_TABLE_CREATE:
		mm = intel_vgpu_get_ppgtt_mm(vgpu, root_entry_type, pdps);
		return PTR_ERR_OR_ZERO(mm);
	case VGT_G2V_PPGTT_L3_PAGE_TABLE_DESTROY:
	case VGT_G2V_PPGTT_L4_PAGE_TABLE_DESTROY:
		return intel_vgpu_put_ppgtt_mm(vgpu, pdps);
	case VGT_G2V_EXECLIST_CONTEXT_CREATE:
	case VGT_G2V_EXECLIST_CONTEXT_DESTROY:
	case 1:	/* Remove this in guest driver. */
		break;
	default:
		gvt_vgpu_err("Invalid PV notification %d\n", notification);
	}
	return 0;
}

static int send_display_ready_uevent(struct intel_vgpu *vgpu, int ready)
{
	struct kobject *kobj = &vgpu->gvt->gt->i915->drm.primary->kdev->kobj;
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
	u32 data = *(u32 *)p_data;
	bool invalid_write = false;

	switch (offset) {
	case _vgtif_reg(display_ready):
		send_display_ready_uevent(vgpu, data ? 1 : 0);
		break;
	case _vgtif_reg(g2v_notify):
		handle_g2v_notification(vgpu, data);
		break;
	/* add xhot and yhot to handled list to avoid error log */
	case _vgtif_reg(cursor_x_hot):
	case _vgtif_reg(cursor_y_hot):
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
		invalid_write = true;
		enter_failsafe_mode(vgpu, GVT_FAILSAFE_INSUFFICIENT_RESOURCE);
		break;
	default:
		invalid_write = true;
		gvt_vgpu_err("invalid pvinfo write offset %x bytes %x data %x\n",
				offset, bytes, data);
		break;
	}

	if (!invalid_write)
		write_vreg(vgpu, offset, p_data, bytes);

	return 0;
}

static int pf_write(struct intel_vgpu *vgpu,
		unsigned int offset, void *p_data, unsigned int bytes)
{
	struct drm_i915_private *i915 = vgpu->gvt->gt->i915;
	u32 val = *(u32 *)p_data;

	if ((offset == _PS_1A_CTRL || offset == _PS_2A_CTRL ||
	   offset == _PS_1B_CTRL || offset == _PS_2B_CTRL ||
	   offset == _PS_1C_CTRL) && (val & PS_PLANE_SEL_MASK) != 0) {
		drm_WARN_ONCE(&i915->drm, true,
			      "VM(%d): guest is trying to scaling a plane\n",
			      vgpu->id);
		return 0;
	}

	return intel_vgpu_default_mmio_write(vgpu, offset, p_data, bytes);
}

static int power_well_ctl_mmio_write(struct intel_vgpu *vgpu,
		unsigned int offset, void *p_data, unsigned int bytes)
{
	write_vreg(vgpu, offset, p_data, bytes);

	if (vgpu_vreg(vgpu, offset) &
	    HSW_PWR_WELL_CTL_REQ(HSW_PW_CTL_IDX_GLOBAL))
		vgpu_vreg(vgpu, offset) |=
			HSW_PWR_WELL_CTL_STATE(HSW_PW_CTL_IDX_GLOBAL);
	else
		vgpu_vreg(vgpu, offset) &=
			~HSW_PWR_WELL_CTL_STATE(HSW_PW_CTL_IDX_GLOBAL);
	return 0;
}

static int gen9_dbuf_ctl_mmio_write(struct intel_vgpu *vgpu,
		unsigned int offset, void *p_data, unsigned int bytes)
{
	write_vreg(vgpu, offset, p_data, bytes);

	if (vgpu_vreg(vgpu, offset) & DBUF_POWER_REQUEST)
		vgpu_vreg(vgpu, offset) |= DBUF_POWER_STATE;
	else
		vgpu_vreg(vgpu, offset) &= ~DBUF_POWER_STATE;

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
	struct drm_i915_private *i915 = vgpu->gvt->gt->i915;
	u32 mode;

	write_vreg(vgpu, offset, p_data, bytes);
	mode = vgpu_vreg(vgpu, offset);

	if (GFX_MODE_BIT_SET_IN_MASK(mode, START_DMA)) {
		drm_WARN_ONCE(&i915->drm, 1,
				"VM(%d): iGVT-g doesn't support GuC\n",
				vgpu->id);
		return 0;
	}

	return 0;
}

static int gen9_trtte_write(struct intel_vgpu *vgpu, unsigned int offset,
		void *p_data, unsigned int bytes)
{
	struct drm_i915_private *i915 = vgpu->gvt->gt->i915;
	u32 trtte = *(u32 *)p_data;

	if ((trtte & 1) && (trtte & (1 << 1)) == 0) {
		drm_WARN(&i915->drm, 1,
				"VM(%d): Use physical address for TRTT!\n",
				vgpu->id);
		return -EINVAL;
	}
	write_vreg(vgpu, offset, p_data, bytes);

	return 0;
}

static int gen9_trtt_chicken_write(struct intel_vgpu *vgpu, unsigned int offset,
		void *p_data, unsigned int bytes)
{
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
	u32 *data0 = &vgpu_vreg_t(vgpu, GEN6_PCODE_DATA);

	switch (cmd) {
	case GEN9_PCODE_READ_MEM_LATENCY:
		if (IS_SKYLAKE(vgpu->gvt->gt->i915) ||
		    IS_KABYLAKE(vgpu->gvt->gt->i915) ||
		    IS_COFFEELAKE(vgpu->gvt->gt->i915) ||
		    IS_COMETLAKE(vgpu->gvt->gt->i915)) {
			/**
			 * "Read memory latency" command on gen9.
			 * Below memory latency values are read
			 * from skylake platform.
			 */
			if (!*data0)
				*data0 = 0x1e1a1100;
			else
				*data0 = 0x61514b3d;
		} else if (IS_BROXTON(vgpu->gvt->gt->i915)) {
			/**
			 * "Read memory latency" command on gen9.
			 * Below memory latency values are read
			 * from Broxton MRB.
			 */
			if (!*data0)
				*data0 = 0x16080707;
			else
				*data0 = 0x16161616;
		}
		break;
	case SKL_PCODE_CDCLK_CONTROL:
		if (IS_SKYLAKE(vgpu->gvt->gt->i915) ||
		    IS_KABYLAKE(vgpu->gvt->gt->i915) ||
		    IS_COFFEELAKE(vgpu->gvt->gt->i915) ||
		    IS_COMETLAKE(vgpu->gvt->gt->i915))
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
	const struct intel_engine_cs *engine =
		intel_gvt_render_mmio_to_engine(vgpu->gvt, offset);

	if (value != 0 &&
	    !intel_gvt_ggtt_validate_range(vgpu, value, I915_GTT_PAGE_SIZE)) {
		gvt_vgpu_err("write invalid HWSP address, reg:0x%x, value:0x%x\n",
			      offset, value);
		return -EINVAL;
	}

	/*
	 * Need to emulate all the HWSP register write to ensure host can
	 * update the VM CSB status correctly. Here listed registers can
	 * support BDW, SKL or other platforms with same HWSP registers.
	 */
	if (unlikely(!engine)) {
		gvt_vgpu_err("access unknown hardware status page register:0x%x\n",
			     offset);
		return -EINVAL;
	}
	vgpu->hws_pga[engine->id] = value;
	gvt_dbg_mmio("VM(%d) write: 0x%x to HWSP: 0x%x\n",
		     vgpu->id, value, offset);

	return intel_vgpu_default_mmio_write(vgpu, offset, &value, bytes);
}

static int skl_power_well_ctl_write(struct intel_vgpu *vgpu,
		unsigned int offset, void *p_data, unsigned int bytes)
{
	u32 v = *(u32 *)p_data;

	if (IS_BROXTON(vgpu->gvt->gt->i915))
		v &= (1 << 31) | (1 << 29);
	else
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

static int bxt_de_pll_enable_write(struct intel_vgpu *vgpu,
		unsigned int offset, void *p_data, unsigned int bytes)
{
	u32 v = *(u32 *)p_data;

	if (v & BXT_DE_PLL_PLL_ENABLE)
		v |= BXT_DE_PLL_LOCK;

	vgpu_vreg(vgpu, offset) = v;

	return 0;
}

static int bxt_port_pll_enable_write(struct intel_vgpu *vgpu,
		unsigned int offset, void *p_data, unsigned int bytes)
{
	u32 v = *(u32 *)p_data;

	if (v & PORT_PLL_ENABLE)
		v |= PORT_PLL_LOCK;

	vgpu_vreg(vgpu, offset) = v;

	return 0;
}

static int bxt_phy_ctl_family_write(struct intel_vgpu *vgpu,
		unsigned int offset, void *p_data, unsigned int bytes)
{
	u32 v = *(u32 *)p_data;
	u32 data = v & COMMON_RESET_DIS ? BXT_PHY_LANE_ENABLED : 0;

	switch (offset) {
	case _PHY_CTL_FAMILY_EDP:
		vgpu_vreg(vgpu, _BXT_PHY_CTL_DDI_A) = data;
		break;
	case _PHY_CTL_FAMILY_DDI:
		vgpu_vreg(vgpu, _BXT_PHY_CTL_DDI_B) = data;
		vgpu_vreg(vgpu, _BXT_PHY_CTL_DDI_C) = data;
		break;
	}

	vgpu_vreg(vgpu, offset) = v;

	return 0;
}

static int bxt_port_tx_dw3_read(struct intel_vgpu *vgpu,
		unsigned int offset, void *p_data, unsigned int bytes)
{
	u32 v = vgpu_vreg(vgpu, offset);

	v &= ~UNIQUE_TRANGE_EN_METHOD;

	vgpu_vreg(vgpu, offset) = v;

	return intel_vgpu_default_mmio_read(vgpu, offset, p_data, bytes);
}

static int bxt_pcs_dw12_grp_write(struct intel_vgpu *vgpu,
		unsigned int offset, void *p_data, unsigned int bytes)
{
	u32 v = *(u32 *)p_data;

	if (offset == _PORT_PCS_DW12_GRP_A || offset == _PORT_PCS_DW12_GRP_B) {
		vgpu_vreg(vgpu, offset - 0x600) = v;
		vgpu_vreg(vgpu, offset - 0x800) = v;
	} else {
		vgpu_vreg(vgpu, offset - 0x400) = v;
		vgpu_vreg(vgpu, offset - 0x600) = v;
	}

	vgpu_vreg(vgpu, offset) = v;

	return 0;
}

static int bxt_gt_disp_pwron_write(struct intel_vgpu *vgpu,
		unsigned int offset, void *p_data, unsigned int bytes)
{
	u32 v = *(u32 *)p_data;

	if (v & BIT(0)) {
		vgpu_vreg_t(vgpu, BXT_PORT_CL1CM_DW0(DPIO_PHY0)) &=
			~PHY_RESERVED;
		vgpu_vreg_t(vgpu, BXT_PORT_CL1CM_DW0(DPIO_PHY0)) |=
			PHY_POWER_GOOD;
	}

	if (v & BIT(1)) {
		vgpu_vreg_t(vgpu, BXT_PORT_CL1CM_DW0(DPIO_PHY1)) &=
			~PHY_RESERVED;
		vgpu_vreg_t(vgpu, BXT_PORT_CL1CM_DW0(DPIO_PHY1)) |=
			PHY_POWER_GOOD;
	}


	vgpu_vreg(vgpu, offset) = v;

	return 0;
}

static int edp_psr_imr_iir_write(struct intel_vgpu *vgpu,
		unsigned int offset, void *p_data, unsigned int bytes)
{
	vgpu_vreg(vgpu, offset) = 0;
	return 0;
}

/*
 * FixMe:
 * If guest fills non-priv batch buffer on ApolloLake/Broxton as Mesa i965 did:
 * 717e7539124d (i965: Use a WC map and memcpy for the batch instead of pwrite.)
 * Due to the missing flush of bb filled by VM vCPU, host GPU hangs on executing
 * these MI_BATCH_BUFFER.
 * Temporarily workaround this by setting SNOOP bit for PAT3 used by PPGTT
 * PML4 PTE: PAT(0) PCD(1) PWT(1).
 * The performance is still expected to be low, will need further improvement.
 */
static int bxt_ppat_low_write(struct intel_vgpu *vgpu, unsigned int offset,
			      void *p_data, unsigned int bytes)
{
	u64 pat =
		GEN8_PPAT(0, CHV_PPAT_SNOOP) |
		GEN8_PPAT(1, 0) |
		GEN8_PPAT(2, 0) |
		GEN8_PPAT(3, CHV_PPAT_SNOOP) |
		GEN8_PPAT(4, CHV_PPAT_SNOOP) |
		GEN8_PPAT(5, CHV_PPAT_SNOOP) |
		GEN8_PPAT(6, CHV_PPAT_SNOOP) |
		GEN8_PPAT(7, CHV_PPAT_SNOOP);

	vgpu_vreg(vgpu, offset) = lower_32_bits(pat);

	return 0;
}

static int guc_status_read(struct intel_vgpu *vgpu,
			   unsigned int offset, void *p_data,
			   unsigned int bytes)
{
	/* keep MIA_IN_RESET before clearing */
	read_vreg(vgpu, offset, p_data, bytes);
	vgpu_vreg(vgpu, offset) &= ~GS_MIA_IN_RESET;
	return 0;
}

static int mmio_read_from_hw(struct intel_vgpu *vgpu,
		unsigned int offset, void *p_data, unsigned int bytes)
{
	struct intel_gvt *gvt = vgpu->gvt;
	const struct intel_engine_cs *engine =
		intel_gvt_render_mmio_to_engine(gvt, offset);

	/**
	 * Read HW reg in following case
	 * a. the offset isn't a ring mmio
	 * b. the offset's ring is running on hw.
	 * c. the offset is ring time stamp mmio
	 */

	if (!engine ||
	    vgpu == gvt->scheduler.engine_owner[engine->id] ||
	    offset == i915_mmio_reg_offset(RING_TIMESTAMP(engine->mmio_base)) ||
	    offset == i915_mmio_reg_offset(RING_TIMESTAMP_UDW(engine->mmio_base))) {
		mmio_hw_access_pre(gvt->gt);
		vgpu_vreg(vgpu, offset) =
			intel_uncore_read(gvt->gt->uncore, _MMIO(offset));
		mmio_hw_access_post(gvt->gt);
	}

	return intel_vgpu_default_mmio_read(vgpu, offset, p_data, bytes);
}

static int elsp_mmio_write(struct intel_vgpu *vgpu, unsigned int offset,
		void *p_data, unsigned int bytes)
{
	struct drm_i915_private *i915 = vgpu->gvt->gt->i915;
	const struct intel_engine_cs *engine = intel_gvt_render_mmio_to_engine(vgpu->gvt, offset);
	struct intel_vgpu_execlist *execlist;
	u32 data = *(u32 *)p_data;
	int ret = 0;

	if (drm_WARN_ON(&i915->drm, !engine))
		return -EINVAL;

	/*
	 * Due to d3_entered is used to indicate skipping PPGTT invalidation on
	 * vGPU reset, it's set on D0->D3 on PCI config write, and cleared after
	 * vGPU reset if in resuming.
	 * In S0ix exit, the device power state also transite from D3 to D0 as
	 * S3 resume, but no vGPU reset (triggered by QEMU devic model). After
	 * S0ix exit, all engines continue to work. However the d3_entered
	 * remains set which will break next vGPU reset logic (miss the expected
	 * PPGTT invalidation).
	 * Engines can only work in D0. Thus the 1st elsp write gives GVT a
	 * chance to clear d3_entered.
	 */
	if (vgpu->d3_entered)
		vgpu->d3_entered = false;

	execlist = &vgpu->submission.execlist[engine->id];

	execlist->elsp_dwords.data[3 - execlist->elsp_dwords.index] = data;
	if (execlist->elsp_dwords.index == 3) {
		ret = intel_vgpu_submit_execlist(vgpu, engine);
		if(ret)
			gvt_vgpu_err("fail submit workload on ring %s\n",
				     engine->name);
	}

	++execlist->elsp_dwords.index;
	execlist->elsp_dwords.index &= 0x3;
	return ret;
}

static int ring_mode_mmio_write(struct intel_vgpu *vgpu, unsigned int offset,
		void *p_data, unsigned int bytes)
{
	u32 data = *(u32 *)p_data;
	const struct intel_engine_cs *engine =
		intel_gvt_render_mmio_to_engine(vgpu->gvt, offset);
	bool enable_execlist;
	int ret;

	(*(u32 *)p_data) &= ~_MASKED_BIT_ENABLE(1);
	if (IS_COFFEELAKE(vgpu->gvt->gt->i915) ||
	    IS_COMETLAKE(vgpu->gvt->gt->i915))
		(*(u32 *)p_data) &= ~_MASKED_BIT_ENABLE(2);
	write_vreg(vgpu, offset, p_data, bytes);

	if (IS_MASKED_BITS_ENABLED(data, 1)) {
		enter_failsafe_mode(vgpu, GVT_FAILSAFE_UNSUPPORTED_GUEST);
		return 0;
	}

	if ((IS_COFFEELAKE(vgpu->gvt->gt->i915) ||
	     IS_COMETLAKE(vgpu->gvt->gt->i915)) &&
	    IS_MASKED_BITS_ENABLED(data, 2)) {
		enter_failsafe_mode(vgpu, GVT_FAILSAFE_UNSUPPORTED_GUEST);
		return 0;
	}

	/* when PPGTT mode enabled, we will check if guest has called
	 * pvinfo, if not, we will treat this guest as non-gvtg-aware
	 * guest, and stop emulating its cfg space, mmio, gtt, etc.
	 */
	if ((IS_MASKED_BITS_ENABLED(data, GFX_PPGTT_ENABLE) ||
	    IS_MASKED_BITS_ENABLED(data, GFX_RUN_LIST_ENABLE)) &&
	    !vgpu->pv_notified) {
		enter_failsafe_mode(vgpu, GVT_FAILSAFE_UNSUPPORTED_GUEST);
		return 0;
	}
	if (IS_MASKED_BITS_ENABLED(data, GFX_RUN_LIST_ENABLE) ||
	    IS_MASKED_BITS_DISABLED(data, GFX_RUN_LIST_ENABLE)) {
		enable_execlist = !!(data & GFX_RUN_LIST_ENABLE);

		gvt_dbg_core("EXECLIST %s on ring %s\n",
			     (enable_execlist ? "enabling" : "disabling"),
			     engine->name);

		if (!enable_execlist)
			return 0;

		ret = intel_vgpu_select_submission_ops(vgpu,
						       engine->mask,
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
		id = RCS0;
		break;
	case 0x4264:
		id = VCS0;
		break;
	case 0x4268:
		id = VCS1;
		break;
	case 0x426c:
		id = BCS0;
		break;
	case 0x4270:
		id = VECS0;
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

	if (IS_MASKED_BITS_ENABLED(data, RESET_CTL_REQUEST_RESET))
		data |= RESET_CTL_READY_TO_RESET;
	else if (data & _MASKED_BIT_DISABLE(RESET_CTL_REQUEST_RESET))
		data &= ~RESET_CTL_READY_TO_RESET;

	vgpu_vreg(vgpu, offset) = data;
	return 0;
}

static int csfe_chicken1_mmio_write(struct intel_vgpu *vgpu,
				    unsigned int offset, void *p_data,
				    unsigned int bytes)
{
	u32 data = *(u32 *)p_data;

	(*(u32 *)p_data) &= ~_MASKED_BIT_ENABLE(0x18);
	write_vreg(vgpu, offset, p_data, bytes);

	if (IS_MASKED_BITS_ENABLED(data, 0x10) ||
	    IS_MASKED_BITS_ENABLED(data, 0x8))
		enter_failsafe_mode(vgpu, GVT_FAILSAFE_UNSUPPORTED_GUEST);

	return 0;
}

#define MMIO_F(reg, s, f, am, rm, d, r, w) do { \
	ret = new_mmio_info(gvt, i915_mmio_reg_offset(reg), \
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
	if (HAS_ENGINE(gvt->gt, VCS1)) \
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
	struct drm_i915_private *dev_priv = gvt->gt->i915;
	int ret;

	MMIO_RING_DFH(RING_IMR, D_ALL, 0, NULL,
		intel_vgpu_reg_imr_handler);

	MMIO_DFH(SDEIMR, D_ALL, 0, NULL, intel_vgpu_reg_imr_handler);
	MMIO_DFH(SDEIER, D_ALL, 0, NULL, intel_vgpu_reg_ier_handler);
	MMIO_DFH(SDEIIR, D_ALL, 0, NULL, intel_vgpu_reg_iir_handler);
	MMIO_D(SDEISR, D_ALL);

	MMIO_RING_DFH(RING_HWSTAM, D_ALL, 0, NULL, NULL);


	MMIO_DH(GEN8_GAMW_ECO_DEV_RW_IA, D_BDW_PLUS, NULL,
		gamw_echo_dev_rw_ia_write);

	MMIO_GM_RDR(BSD_HWS_PGA_GEN7, D_ALL, NULL, NULL);
	MMIO_GM_RDR(BLT_HWS_PGA_GEN7, D_ALL, NULL, NULL);
	MMIO_GM_RDR(VEBOX_HWS_PGA_GEN7, D_ALL, NULL, NULL);

#define RING_REG(base) _MMIO((base) + 0x28)
	MMIO_RING_DFH(RING_REG, D_ALL, F_CMD_ACCESS, NULL, NULL);
#undef RING_REG

#define RING_REG(base) _MMIO((base) + 0x134)
	MMIO_RING_DFH(RING_REG, D_ALL, F_CMD_ACCESS, NULL, NULL);
#undef RING_REG

#define RING_REG(base) _MMIO((base) + 0x6c)
	MMIO_RING_DFH(RING_REG, D_ALL, 0, mmio_read_from_hw, NULL);
#undef RING_REG
	MMIO_DH(GEN7_SC_INSTDONE, D_BDW_PLUS, mmio_read_from_hw, NULL);

	MMIO_GM_RDR(_MMIO(0x2148), D_ALL, NULL, NULL);
	MMIO_GM_RDR(CCID(RENDER_RING_BASE), D_ALL, NULL, NULL);
	MMIO_GM_RDR(_MMIO(0x12198), D_ALL, NULL, NULL);
	MMIO_D(GEN7_CXT_SIZE, D_ALL);

	MMIO_RING_DFH(RING_TAIL, D_ALL, 0, NULL, NULL);
	MMIO_RING_DFH(RING_HEAD, D_ALL, 0, NULL, NULL);
	MMIO_RING_DFH(RING_CTL, D_ALL, 0, NULL, NULL);
	MMIO_RING_DFH(RING_ACTHD, D_ALL, 0, mmio_read_from_hw, NULL);
	MMIO_RING_GM(RING_START, D_ALL, NULL, NULL);

	/* RING MODE */
#define RING_REG(base) _MMIO((base) + 0x29c)
	MMIO_RING_DFH(RING_REG, D_ALL,
		F_MODE_MASK | F_CMD_ACCESS | F_CMD_WRITE_PATCH, NULL,
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
	MMIO_DFH(_MMIO(0x2124), D_ALL, F_MODE_MASK | F_CMD_ACCESS, NULL, NULL);

	MMIO_DFH(_MMIO(0x20dc), D_ALL, F_MODE_MASK | F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(_3D_CHICKEN3, D_ALL, F_MODE_MASK | F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(_MMIO(0x2088), D_ALL, F_MODE_MASK | F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(FF_SLICE_CS_CHICKEN2, D_ALL,
		 F_MODE_MASK | F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(_MMIO(0x2470), D_ALL, F_MODE_MASK | F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(GAM_ECOCHK, D_ALL, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(GEN7_COMMON_SLICE_CHICKEN1, D_ALL, F_MODE_MASK | F_CMD_ACCESS,
		NULL, NULL);
	MMIO_DFH(COMMON_SLICE_CHICKEN2, D_ALL, F_MODE_MASK | F_CMD_ACCESS,
		 NULL, NULL);
	MMIO_DFH(_MMIO(0x9030), D_ALL, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(_MMIO(0x20a0), D_ALL, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(_MMIO(0x2420), D_ALL, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(_MMIO(0x2430), D_ALL, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(_MMIO(0x2434), D_ALL, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(_MMIO(0x2438), D_ALL, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(_MMIO(0x243c), D_ALL, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(_MMIO(0x7018), D_ALL, F_MODE_MASK | F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(HALF_SLICE_CHICKEN3, D_ALL, F_MODE_MASK | F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(GEN7_HALF_SLICE_CHICKEN1, D_ALL, F_MODE_MASK | F_CMD_ACCESS, NULL, NULL);

	/* display */
	MMIO_F(_MMIO(0x60220), 0x20, 0, 0, 0, D_ALL, NULL, NULL);
	MMIO_D(_MMIO(0x602a0), D_ALL);

	MMIO_D(_MMIO(0x65050), D_ALL);
	MMIO_D(_MMIO(0x650b4), D_ALL);

	MMIO_D(_MMIO(0xc4040), D_ALL);
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

	MMIO_D(CUR_FBC_CTL(PIPE_A), D_ALL);
	MMIO_D(CUR_FBC_CTL(PIPE_B), D_ALL);
	MMIO_D(CUR_FBC_CTL(PIPE_C), D_ALL);

	MMIO_D(_MMIO(0x700ac), D_ALL);
	MMIO_D(_MMIO(0x710ac), D_ALL);
	MMIO_D(_MMIO(0x720ac), D_ALL);

	MMIO_D(_MMIO(0x70090), D_ALL);
	MMIO_D(_MMIO(0x70094), D_ALL);
	MMIO_D(_MMIO(0x70098), D_ALL);
	MMIO_D(_MMIO(0x7009c), D_ALL);

	MMIO_D(DSPCNTR(PIPE_A), D_ALL);
	MMIO_D(DSPADDR(PIPE_A), D_ALL);
	MMIO_D(DSPSTRIDE(PIPE_A), D_ALL);
	MMIO_D(DSPPOS(PIPE_A), D_ALL);
	MMIO_D(DSPSIZE(PIPE_A), D_ALL);
	MMIO_DH(DSPSURF(PIPE_A), D_ALL, NULL, pri_surf_mmio_write);
	MMIO_D(DSPOFFSET(PIPE_A), D_ALL);
	MMIO_D(DSPSURFLIVE(PIPE_A), D_ALL);
	MMIO_DH(REG_50080(PIPE_A, PLANE_PRIMARY), D_ALL, NULL,
		reg50080_mmio_write);

	MMIO_D(DSPCNTR(PIPE_B), D_ALL);
	MMIO_D(DSPADDR(PIPE_B), D_ALL);
	MMIO_D(DSPSTRIDE(PIPE_B), D_ALL);
	MMIO_D(DSPPOS(PIPE_B), D_ALL);
	MMIO_D(DSPSIZE(PIPE_B), D_ALL);
	MMIO_DH(DSPSURF(PIPE_B), D_ALL, NULL, pri_surf_mmio_write);
	MMIO_D(DSPOFFSET(PIPE_B), D_ALL);
	MMIO_D(DSPSURFLIVE(PIPE_B), D_ALL);
	MMIO_DH(REG_50080(PIPE_B, PLANE_PRIMARY), D_ALL, NULL,
		reg50080_mmio_write);

	MMIO_D(DSPCNTR(PIPE_C), D_ALL);
	MMIO_D(DSPADDR(PIPE_C), D_ALL);
	MMIO_D(DSPSTRIDE(PIPE_C), D_ALL);
	MMIO_D(DSPPOS(PIPE_C), D_ALL);
	MMIO_D(DSPSIZE(PIPE_C), D_ALL);
	MMIO_DH(DSPSURF(PIPE_C), D_ALL, NULL, pri_surf_mmio_write);
	MMIO_D(DSPOFFSET(PIPE_C), D_ALL);
	MMIO_D(DSPSURFLIVE(PIPE_C), D_ALL);
	MMIO_DH(REG_50080(PIPE_C, PLANE_PRIMARY), D_ALL, NULL,
		reg50080_mmio_write);

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
	MMIO_DH(REG_50080(PIPE_A, PLANE_SPRITE0), D_ALL, NULL,
		reg50080_mmio_write);

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
	MMIO_DH(REG_50080(PIPE_B, PLANE_SPRITE0), D_ALL, NULL,
		reg50080_mmio_write);

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
	MMIO_DH(REG_50080(PIPE_C, PLANE_SPRITE0), D_ALL, NULL,
		reg50080_mmio_write);

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

	MMIO_D(WM0_PIPE_ILK(PIPE_A), D_ALL);
	MMIO_D(WM0_PIPE_ILK(PIPE_B), D_ALL);
	MMIO_D(WM0_PIPE_ILK(PIPE_C), D_ALL);
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

	MMIO_D(_MMIO(0x48268), D_ALL);

	MMIO_F(PCH_GMBUS0, 4 * 4, 0, 0, 0, D_ALL, gmbus_mmio_read,
		gmbus_mmio_write);
	MMIO_F(PCH_GPIO_BASE, 6 * 4, F_UNALIGN, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(_MMIO(0xe4f00), 0x28, 0, 0, 0, D_ALL, NULL, NULL);

	MMIO_F(_MMIO(_PCH_DPB_AUX_CH_CTL), 6 * 4, 0, 0, 0, D_PRE_SKL, NULL,
		dp_aux_ch_ctl_mmio_write);
	MMIO_F(_MMIO(_PCH_DPC_AUX_CH_CTL), 6 * 4, 0, 0, 0, D_PRE_SKL, NULL,
		dp_aux_ch_ctl_mmio_write);
	MMIO_F(_MMIO(_PCH_DPD_AUX_CH_CTL), 6 * 4, 0, 0, 0, D_PRE_SKL, NULL,
		dp_aux_ch_ctl_mmio_write);

	MMIO_DH(PCH_ADPA, D_PRE_SKL, NULL, pch_adpa_mmio_write);

	MMIO_DH(_MMIO(_PCH_TRANSACONF), D_ALL, NULL, transconf_mmio_write);
	MMIO_DH(_MMIO(_PCH_TRANSBCONF), D_ALL, NULL, transconf_mmio_write);

	MMIO_DH(FDI_RX_IIR(PIPE_A), D_ALL, NULL, fdi_rx_iir_mmio_write);
	MMIO_DH(FDI_RX_IIR(PIPE_B), D_ALL, NULL, fdi_rx_iir_mmio_write);
	MMIO_DH(FDI_RX_IIR(PIPE_C), D_ALL, NULL, fdi_rx_iir_mmio_write);
	MMIO_DH(FDI_RX_IMR(PIPE_A), D_ALL, NULL, update_fdi_rx_iir_status);
	MMIO_DH(FDI_RX_IMR(PIPE_B), D_ALL, NULL, update_fdi_rx_iir_status);
	MMIO_DH(FDI_RX_IMR(PIPE_C), D_ALL, NULL, update_fdi_rx_iir_status);
	MMIO_DH(FDI_RX_CTL(PIPE_A), D_ALL, NULL, update_fdi_rx_iir_status);
	MMIO_DH(FDI_RX_CTL(PIPE_B), D_ALL, NULL, update_fdi_rx_iir_status);
	MMIO_DH(FDI_RX_CTL(PIPE_C), D_ALL, NULL, update_fdi_rx_iir_status);

	MMIO_D(_MMIO(_PCH_TRANS_HTOTAL_A), D_ALL);
	MMIO_D(_MMIO(_PCH_TRANS_HBLANK_A), D_ALL);
	MMIO_D(_MMIO(_PCH_TRANS_HSYNC_A), D_ALL);
	MMIO_D(_MMIO(_PCH_TRANS_VTOTAL_A), D_ALL);
	MMIO_D(_MMIO(_PCH_TRANS_VBLANK_A), D_ALL);
	MMIO_D(_MMIO(_PCH_TRANS_VSYNC_A), D_ALL);
	MMIO_D(_MMIO(_PCH_TRANS_VSYNCSHIFT_A), D_ALL);

	MMIO_D(_MMIO(_PCH_TRANS_HTOTAL_B), D_ALL);
	MMIO_D(_MMIO(_PCH_TRANS_HBLANK_B), D_ALL);
	MMIO_D(_MMIO(_PCH_TRANS_HSYNC_B), D_ALL);
	MMIO_D(_MMIO(_PCH_TRANS_VTOTAL_B), D_ALL);
	MMIO_D(_MMIO(_PCH_TRANS_VBLANK_B), D_ALL);
	MMIO_D(_MMIO(_PCH_TRANS_VSYNC_B), D_ALL);
	MMIO_D(_MMIO(_PCH_TRANS_VSYNCSHIFT_B), D_ALL);

	MMIO_D(_MMIO(_PCH_TRANSA_DATA_M1), D_ALL);
	MMIO_D(_MMIO(_PCH_TRANSA_DATA_N1), D_ALL);
	MMIO_D(_MMIO(_PCH_TRANSA_DATA_M2), D_ALL);
	MMIO_D(_MMIO(_PCH_TRANSA_DATA_N2), D_ALL);
	MMIO_D(_MMIO(_PCH_TRANSA_LINK_M1), D_ALL);
	MMIO_D(_MMIO(_PCH_TRANSA_LINK_N1), D_ALL);
	MMIO_D(_MMIO(_PCH_TRANSA_LINK_M2), D_ALL);
	MMIO_D(_MMIO(_PCH_TRANSA_LINK_N2), D_ALL);

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

	MMIO_D(_MMIO(_FDI_RXA_MISC), D_ALL);
	MMIO_D(_MMIO(_FDI_RXB_MISC), D_ALL);
	MMIO_D(_MMIO(_FDI_RXA_TUSIZE1), D_ALL);
	MMIO_D(_MMIO(_FDI_RXA_TUSIZE2), D_ALL);
	MMIO_D(_MMIO(_FDI_RXB_TUSIZE1), D_ALL);
	MMIO_D(_MMIO(_FDI_RXB_TUSIZE2), D_ALL);

	MMIO_DH(PCH_PP_CONTROL, D_ALL, NULL, pch_pp_control_mmio_write);
	MMIO_D(PCH_PP_DIVISOR, D_ALL);
	MMIO_D(PCH_PP_STATUS,  D_ALL);
	MMIO_D(PCH_LVDS, D_ALL);
	MMIO_D(_MMIO(_PCH_DPLL_A), D_ALL);
	MMIO_D(_MMIO(_PCH_DPLL_B), D_ALL);
	MMIO_D(_MMIO(_PCH_FPA0), D_ALL);
	MMIO_D(_MMIO(_PCH_FPA1), D_ALL);
	MMIO_D(_MMIO(_PCH_FPB0), D_ALL);
	MMIO_D(_MMIO(_PCH_FPB1), D_ALL);
	MMIO_D(PCH_DREF_CONTROL, D_ALL);
	MMIO_D(PCH_RAWCLK_FREQ, D_ALL);
	MMIO_D(PCH_DPLL_SEL, D_ALL);

	MMIO_D(_MMIO(0x61208), D_ALL);
	MMIO_D(_MMIO(0x6120c), D_ALL);
	MMIO_D(PCH_PP_ON_DELAYS, D_ALL);
	MMIO_D(PCH_PP_OFF_DELAYS, D_ALL);

	MMIO_DH(_MMIO(0xe651c), D_ALL, dpy_reg_mmio_read, NULL);
	MMIO_DH(_MMIO(0xe661c), D_ALL, dpy_reg_mmio_read, NULL);
	MMIO_DH(_MMIO(0xe671c), D_ALL, dpy_reg_mmio_read, NULL);
	MMIO_DH(_MMIO(0xe681c), D_ALL, dpy_reg_mmio_read, NULL);
	MMIO_DH(_MMIO(0xe6c04), D_ALL, dpy_reg_mmio_read, NULL);
	MMIO_DH(_MMIO(0xe6e1c), D_ALL, dpy_reg_mmio_read, NULL);

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
	MMIO_D(_MMIO(_TRANSA_CHICKEN1), D_ALL);
	MMIO_D(_MMIO(_TRANSB_CHICKEN1), D_ALL);
	MMIO_D(SOUTH_DSPCLK_GATE_D, D_ALL);
	MMIO_D(_MMIO(_TRANSA_CHICKEN2), D_ALL);
	MMIO_D(_MMIO(_TRANSB_CHICKEN2), D_ALL);

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

	MMIO_D(_MMIO(0x60110), D_ALL);
	MMIO_D(_MMIO(0x61110), D_ALL);
	MMIO_F(_MMIO(0x70400), 0x40, 0, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(_MMIO(0x71400), 0x40, 0, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(_MMIO(0x72400), 0x40, 0, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(_MMIO(0x70440), 0xc, 0, 0, 0, D_PRE_SKL, NULL, NULL);
	MMIO_F(_MMIO(0x71440), 0xc, 0, 0, 0, D_PRE_SKL, NULL, NULL);
	MMIO_F(_MMIO(0x72440), 0xc, 0, 0, 0, D_PRE_SKL, NULL, NULL);
	MMIO_F(_MMIO(0x7044c), 0xc, 0, 0, 0, D_PRE_SKL, NULL, NULL);
	MMIO_F(_MMIO(0x7144c), 0xc, 0, 0, 0, D_PRE_SKL, NULL, NULL);
	MMIO_F(_MMIO(0x7244c), 0xc, 0, 0, 0, D_PRE_SKL, NULL, NULL);

	MMIO_D(WM_LINETIME(PIPE_A), D_ALL);
	MMIO_D(WM_LINETIME(PIPE_B), D_ALL);
	MMIO_D(WM_LINETIME(PIPE_C), D_ALL);
	MMIO_D(SPLL_CTL, D_ALL);
	MMIO_D(_MMIO(_WRPLL_CTL1), D_ALL);
	MMIO_D(_MMIO(_WRPLL_CTL2), D_ALL);
	MMIO_D(PORT_CLK_SEL(PORT_A), D_ALL);
	MMIO_D(PORT_CLK_SEL(PORT_B), D_ALL);
	MMIO_D(PORT_CLK_SEL(PORT_C), D_ALL);
	MMIO_D(PORT_CLK_SEL(PORT_D), D_ALL);
	MMIO_D(PORT_CLK_SEL(PORT_E), D_ALL);
	MMIO_D(TRANS_CLK_SEL(TRANSCODER_A), D_ALL);
	MMIO_D(TRANS_CLK_SEL(TRANSCODER_B), D_ALL);
	MMIO_D(TRANS_CLK_SEL(TRANSCODER_C), D_ALL);

	MMIO_D(HSW_NDE_RSTWRN_OPT, D_ALL);
	MMIO_D(_MMIO(0x46508), D_ALL);

	MMIO_D(_MMIO(0x49080), D_ALL);
	MMIO_D(_MMIO(0x49180), D_ALL);
	MMIO_D(_MMIO(0x49280), D_ALL);

	MMIO_F(_MMIO(0x49090), 0x14, 0, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(_MMIO(0x49190), 0x14, 0, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(_MMIO(0x49290), 0x14, 0, 0, 0, D_ALL, NULL, NULL);

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

	MMIO_F(_MMIO(_DPA_AUX_CH_CTL), 6 * 4, 0, 0, 0, D_ALL, NULL,
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

	MMIO_F(_MMIO(_DDI_BUF_TRANS_A), 0x50, 0, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(_MMIO(0x64e60), 0x50, 0, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(_MMIO(0x64eC0), 0x50, 0, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(_MMIO(0x64f20), 0x50, 0, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(_MMIO(0x64f80), 0x50, 0, 0, 0, D_ALL, NULL, NULL);

	MMIO_D(HSW_AUD_CFG(PIPE_A), D_ALL);
	MMIO_D(HSW_AUD_PIN_ELD_CP_VLD, D_ALL);
	MMIO_D(HSW_AUD_MISC_CTRL(PIPE_A), D_ALL);

	MMIO_DH(_MMIO(_TRANS_DDI_FUNC_CTL_A), D_ALL, NULL, NULL);
	MMIO_DH(_MMIO(_TRANS_DDI_FUNC_CTL_B), D_ALL, NULL, NULL);
	MMIO_DH(_MMIO(_TRANS_DDI_FUNC_CTL_C), D_ALL, NULL, NULL);
	MMIO_DH(_MMIO(_TRANS_DDI_FUNC_CTL_EDP), D_ALL, NULL, NULL);

	MMIO_D(_MMIO(_TRANSA_MSA_MISC), D_ALL);
	MMIO_D(_MMIO(_TRANSB_MSA_MISC), D_ALL);
	MMIO_D(_MMIO(_TRANSC_MSA_MISC), D_ALL);
	MMIO_D(_MMIO(_TRANS_EDP_MSA_MISC), D_ALL);

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
	MMIO_DH(HSW_PWR_WELL_CTL1, D_BDW, NULL, power_well_ctl_mmio_write);
	MMIO_DH(HSW_PWR_WELL_CTL2, D_BDW, NULL, power_well_ctl_mmio_write);
	MMIO_DH(HSW_PWR_WELL_CTL3, D_BDW, NULL, power_well_ctl_mmio_write);
	MMIO_DH(HSW_PWR_WELL_CTL4, D_BDW, NULL, power_well_ctl_mmio_write);
	MMIO_DH(HSW_PWR_WELL_CTL5, D_BDW, NULL, power_well_ctl_mmio_write);
	MMIO_DH(HSW_PWR_WELL_CTL6, D_BDW, NULL, power_well_ctl_mmio_write);

	MMIO_D(RSTDBYCTL, D_ALL);

	MMIO_DH(GEN6_GDRST, D_ALL, NULL, gdrst_mmio_write);
	MMIO_F(FENCE_REG_GEN6_LO(0), 0x80, 0, 0, 0, D_ALL, fence_mmio_read, fence_mmio_write);
	MMIO_DH(CPU_VGACNTRL, D_ALL, NULL, vga_control_mmio_write);

	MMIO_D(TILECTL, D_ALL);

	MMIO_D(GEN6_UCGCTL1, D_ALL);
	MMIO_D(GEN6_UCGCTL2, D_ALL);

	MMIO_F(_MMIO(0x4f000), 0x90, 0, 0, 0, D_ALL, NULL, NULL);

	MMIO_D(GEN6_PCODE_DATA, D_ALL);
	MMIO_D(_MMIO(0x13812c), D_ALL);
	MMIO_DH(GEN7_ERR_INT, D_ALL, NULL, NULL);
	MMIO_D(HSW_EDRAM_CAP, D_ALL);
	MMIO_D(HSW_IDICR, D_ALL);
	MMIO_DH(GFX_FLSH_CNTL_GEN6, D_ALL, NULL, NULL);

	MMIO_D(_MMIO(0x3c), D_ALL);
	MMIO_D(_MMIO(0x860), D_ALL);
	MMIO_D(ECOSKPD, D_ALL);
	MMIO_D(_MMIO(0x121d0), D_ALL);
	MMIO_D(GEN6_BLITTER_ECOSKPD, D_ALL);
	MMIO_D(_MMIO(0x41d0), D_ALL);
	MMIO_D(GAC_ECO_BITS, D_ALL);
	MMIO_D(_MMIO(0x6200), D_ALL);
	MMIO_D(_MMIO(0x6204), D_ALL);
	MMIO_D(_MMIO(0x6208), D_ALL);
	MMIO_D(_MMIO(0x7118), D_ALL);
	MMIO_D(_MMIO(0x7180), D_ALL);
	MMIO_D(_MMIO(0x7408), D_ALL);
	MMIO_D(_MMIO(0x7c00), D_ALL);
	MMIO_DH(GEN6_MBCTL, D_ALL, NULL, mbctl_write);
	MMIO_D(_MMIO(0x911c), D_ALL);
	MMIO_D(_MMIO(0x9120), D_ALL);
	MMIO_DFH(GEN7_UCGCTL4, D_ALL, F_CMD_ACCESS, NULL, NULL);

	MMIO_D(GAB_CTL, D_ALL);
	MMIO_D(_MMIO(0x48800), D_ALL);
	MMIO_D(_MMIO(0xce044), D_ALL);
	MMIO_D(_MMIO(0xe6500), D_ALL);
	MMIO_D(_MMIO(0xe6504), D_ALL);
	MMIO_D(_MMIO(0xe6600), D_ALL);
	MMIO_D(_MMIO(0xe6604), D_ALL);
	MMIO_D(_MMIO(0xe6700), D_ALL);
	MMIO_D(_MMIO(0xe6704), D_ALL);
	MMIO_D(_MMIO(0xe6800), D_ALL);
	MMIO_D(_MMIO(0xe6804), D_ALL);
	MMIO_D(PCH_GMBUS4, D_ALL);
	MMIO_D(PCH_GMBUS5, D_ALL);

	MMIO_D(_MMIO(0x902c), D_ALL);
	MMIO_D(_MMIO(0xec008), D_ALL);
	MMIO_D(_MMIO(0xec00c), D_ALL);
	MMIO_D(_MMIO(0xec008 + 0x18), D_ALL);
	MMIO_D(_MMIO(0xec00c + 0x18), D_ALL);
	MMIO_D(_MMIO(0xec008 + 0x18 * 2), D_ALL);
	MMIO_D(_MMIO(0xec00c + 0x18 * 2), D_ALL);
	MMIO_D(_MMIO(0xec008 + 0x18 * 3), D_ALL);
	MMIO_D(_MMIO(0xec00c + 0x18 * 3), D_ALL);
	MMIO_D(_MMIO(0xec408), D_ALL);
	MMIO_D(_MMIO(0xec40c), D_ALL);
	MMIO_D(_MMIO(0xec408 + 0x18), D_ALL);
	MMIO_D(_MMIO(0xec40c + 0x18), D_ALL);
	MMIO_D(_MMIO(0xec408 + 0x18 * 2), D_ALL);
	MMIO_D(_MMIO(0xec40c + 0x18 * 2), D_ALL);
	MMIO_D(_MMIO(0xec408 + 0x18 * 3), D_ALL);
	MMIO_D(_MMIO(0xec40c + 0x18 * 3), D_ALL);
	MMIO_D(_MMIO(0xfc810), D_ALL);
	MMIO_D(_MMIO(0xfc81c), D_ALL);
	MMIO_D(_MMIO(0xfc828), D_ALL);
	MMIO_D(_MMIO(0xfc834), D_ALL);
	MMIO_D(_MMIO(0xfcc00), D_ALL);
	MMIO_D(_MMIO(0xfcc0c), D_ALL);
	MMIO_D(_MMIO(0xfcc18), D_ALL);
	MMIO_D(_MMIO(0xfcc24), D_ALL);
	MMIO_D(_MMIO(0xfd000), D_ALL);
	MMIO_D(_MMIO(0xfd00c), D_ALL);
	MMIO_D(_MMIO(0xfd018), D_ALL);
	MMIO_D(_MMIO(0xfd024), D_ALL);
	MMIO_D(_MMIO(0xfd034), D_ALL);

	MMIO_DH(FPGA_DBG, D_ALL, NULL, fpga_dbg_mmio_write);
	MMIO_D(_MMIO(0x2054), D_ALL);
	MMIO_D(_MMIO(0x12054), D_ALL);
	MMIO_D(_MMIO(0x22054), D_ALL);
	MMIO_D(_MMIO(0x1a054), D_ALL);

	MMIO_D(_MMIO(0x44070), D_ALL);
	MMIO_DFH(_MMIO(0x215c), D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(_MMIO(0x2178), D_ALL, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(_MMIO(0x217c), D_ALL, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(_MMIO(0x12178), D_ALL, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(_MMIO(0x1217c), D_ALL, F_CMD_ACCESS, NULL, NULL);

	MMIO_F(_MMIO(0x2290), 8, F_CMD_ACCESS, 0, 0, D_BDW_PLUS, NULL, NULL);
	MMIO_D(_MMIO(0x2b00), D_BDW_PLUS);
	MMIO_D(_MMIO(0x2360), D_BDW_PLUS);
	MMIO_F(_MMIO(0x5200), 32, F_CMD_ACCESS, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(_MMIO(0x5240), 32, F_CMD_ACCESS, 0, 0, D_ALL, NULL, NULL);
	MMIO_F(_MMIO(0x5280), 16, F_CMD_ACCESS, 0, 0, D_ALL, NULL, NULL);

	MMIO_DFH(_MMIO(0x1c17c), D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(_MMIO(0x1c178), D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);
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
	MMIO_DH(_MMIO(0x4260), D_BDW_PLUS, NULL, gvt_reg_tlb_control_handler);
	MMIO_DH(_MMIO(0x4264), D_BDW_PLUS, NULL, gvt_reg_tlb_control_handler);
	MMIO_DH(_MMIO(0x4268), D_BDW_PLUS, NULL, gvt_reg_tlb_control_handler);
	MMIO_DH(_MMIO(0x426c), D_BDW_PLUS, NULL, gvt_reg_tlb_control_handler);
	MMIO_DH(_MMIO(0x4270), D_BDW_PLUS, NULL, gvt_reg_tlb_control_handler);
	MMIO_DFH(_MMIO(0x4094), D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);

	MMIO_DFH(ARB_MODE, D_ALL, F_MODE_MASK | F_CMD_ACCESS, NULL, NULL);
	MMIO_RING_GM(RING_BBADDR, D_ALL, NULL, NULL);
	MMIO_DFH(_MMIO(0x2220), D_ALL, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(_MMIO(0x12220), D_ALL, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(_MMIO(0x22220), D_ALL, F_CMD_ACCESS, NULL, NULL);
	MMIO_RING_DFH(RING_SYNC_1, D_ALL, F_CMD_ACCESS, NULL, NULL);
	MMIO_RING_DFH(RING_SYNC_0, D_ALL, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(_MMIO(0x22178), D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(_MMIO(0x1a178), D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(_MMIO(0x1a17c), D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(_MMIO(0x2217c), D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);

	MMIO_DH(EDP_PSR_IMR, D_BDW_PLUS, NULL, edp_psr_imr_iir_write);
	MMIO_DH(EDP_PSR_IIR, D_BDW_PLUS, NULL, edp_psr_imr_iir_write);
	MMIO_DH(GUC_STATUS, D_ALL, guc_status_read, NULL);

	return 0;
}

static int init_bdw_mmio_info(struct intel_gvt *gvt)
{
	struct drm_i915_private *dev_priv = gvt->gt->i915;
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

	MMIO_RING_DFH(RING_ACTHD_UDW, D_BDW_PLUS, 0,
		mmio_read_from_hw, NULL);

#define RING_REG(base) _MMIO((base) + 0xd0)
	MMIO_RING_F(RING_REG, 4, F_RO, 0,
		~_MASKED_BIT_ENABLE(RESET_CTL_REQUEST_RESET), D_BDW_PLUS, NULL,
		ring_reset_ctl_write);
#undef RING_REG

#define RING_REG(base) _MMIO((base) + 0x230)
	MMIO_RING_DFH(RING_REG, D_BDW_PLUS, 0, NULL, elsp_mmio_write);
#undef RING_REG

#define RING_REG(base) _MMIO((base) + 0x234)
	MMIO_RING_F(RING_REG, 8, F_RO, 0, ~0, D_BDW_PLUS,
		NULL, NULL);
#undef RING_REG

#define RING_REG(base) _MMIO((base) + 0x244)
	MMIO_RING_DFH(RING_REG, D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);
#undef RING_REG

#define RING_REG(base) _MMIO((base) + 0x370)
	MMIO_RING_F(RING_REG, 48, F_RO, 0, ~0, D_BDW_PLUS, NULL, NULL);
#undef RING_REG

#define RING_REG(base) _MMIO((base) + 0x3a0)
	MMIO_RING_DFH(RING_REG, D_BDW_PLUS, F_MODE_MASK, NULL, NULL);
#undef RING_REG

	MMIO_D(PIPEMISC(PIPE_A), D_BDW_PLUS);
	MMIO_D(PIPEMISC(PIPE_B), D_BDW_PLUS);
	MMIO_D(PIPEMISC(PIPE_C), D_BDW_PLUS);
	MMIO_D(_MMIO(0x1c1d0), D_BDW_PLUS);
	MMIO_D(GEN6_MBCUNIT_SNPCR, D_BDW_PLUS);
	MMIO_D(GEN7_MISCCPCTL, D_BDW_PLUS);
	MMIO_D(_MMIO(0x1c054), D_BDW_PLUS);

	MMIO_DH(GEN6_PCODE_MAILBOX, D_BDW_PLUS, NULL, mailbox_write);

	MMIO_D(GEN8_PRIVATE_PAT_LO, D_BDW_PLUS & ~D_BXT);
	MMIO_D(GEN8_PRIVATE_PAT_HI, D_BDW_PLUS);

	MMIO_D(GAMTARBMODE, D_BDW_PLUS);

#define RING_REG(base) _MMIO((base) + 0x270)
	MMIO_RING_F(RING_REG, 32, F_CMD_ACCESS, 0, 0, D_BDW_PLUS, NULL, NULL);
#undef RING_REG

	MMIO_RING_GM(RING_HWS_PGA, D_BDW_PLUS, NULL, hws_pga_write);

	MMIO_DFH(HDC_CHICKEN0, D_BDW_PLUS, F_MODE_MASK | F_CMD_ACCESS, NULL, NULL);

	MMIO_D(CHICKEN_PIPESL_1(PIPE_A), D_BDW_PLUS);
	MMIO_D(CHICKEN_PIPESL_1(PIPE_B), D_BDW_PLUS);
	MMIO_D(CHICKEN_PIPESL_1(PIPE_C), D_BDW_PLUS);

	MMIO_D(WM_MISC, D_BDW);
	MMIO_D(_MMIO(_SRD_CTL_EDP), D_BDW);

	MMIO_D(_MMIO(0x6671c), D_BDW_PLUS);
	MMIO_D(_MMIO(0x66c00), D_BDW_PLUS);
	MMIO_D(_MMIO(0x66c04), D_BDW_PLUS);

	MMIO_D(HSW_GTT_CACHE_EN, D_BDW_PLUS);

	MMIO_D(GEN8_EU_DISABLE0, D_BDW_PLUS);
	MMIO_D(GEN8_EU_DISABLE1, D_BDW_PLUS);
	MMIO_D(GEN8_EU_DISABLE2, D_BDW_PLUS);

	MMIO_D(_MMIO(0xfdc), D_BDW_PLUS);
	MMIO_DFH(GEN8_ROW_CHICKEN, D_BDW_PLUS, F_MODE_MASK | F_CMD_ACCESS,
		NULL, NULL);
	MMIO_DFH(GEN7_ROW_CHICKEN2, D_BDW_PLUS, F_MODE_MASK | F_CMD_ACCESS,
		NULL, NULL);
	MMIO_DFH(GEN8_UCGCTL6, D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);

	MMIO_DFH(_MMIO(0xb1f0), D_BDW, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(_MMIO(0xb1c0), D_BDW, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(GEN8_L3SQCREG4, D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(_MMIO(0xb100), D_BDW, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(_MMIO(0xb10c), D_BDW, F_CMD_ACCESS, NULL, NULL);
	MMIO_D(_MMIO(0xb110), D_BDW);

	MMIO_F(_MMIO(0x24d0), 48, F_CMD_ACCESS | F_CMD_WRITE_PATCH, 0, 0,
		D_BDW_PLUS, NULL, force_nonpriv_write);

	MMIO_D(_MMIO(0x44484), D_BDW_PLUS);
	MMIO_D(_MMIO(0x4448c), D_BDW_PLUS);

	MMIO_DFH(_MMIO(0x83a4), D_BDW, F_CMD_ACCESS, NULL, NULL);
	MMIO_D(GEN8_L3_LRA_1_GPGPU, D_BDW_PLUS);

	MMIO_DFH(_MMIO(0x8430), D_BDW, F_CMD_ACCESS, NULL, NULL);

	MMIO_D(_MMIO(0x110000), D_BDW_PLUS);

	MMIO_D(_MMIO(0x48400), D_BDW_PLUS);

	MMIO_D(_MMIO(0x6e570), D_BDW_PLUS);
	MMIO_D(_MMIO(0x65f10), D_BDW_PLUS);

	MMIO_DFH(_MMIO(0xe194), D_BDW_PLUS, F_MODE_MASK | F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(_MMIO(0xe188), D_BDW_PLUS, F_MODE_MASK | F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(HALF_SLICE_CHICKEN2, D_BDW_PLUS, F_MODE_MASK | F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(_MMIO(0x2580), D_BDW_PLUS, F_MODE_MASK | F_CMD_ACCESS, NULL, NULL);

	MMIO_DFH(_MMIO(0x2248), D_BDW, F_CMD_ACCESS, NULL, NULL);

	MMIO_DFH(_MMIO(0xe220), D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(_MMIO(0xe230), D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(_MMIO(0xe240), D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(_MMIO(0xe260), D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(_MMIO(0xe270), D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(_MMIO(0xe280), D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(_MMIO(0xe2a0), D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(_MMIO(0xe2b0), D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(_MMIO(0xe2c0), D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(_MMIO(0x21f0), D_BDW_PLUS, F_CMD_ACCESS, NULL, NULL);
	return 0;
}

static int init_skl_mmio_info(struct intel_gvt *gvt)
{
	struct drm_i915_private *dev_priv = gvt->gt->i915;
	int ret;

	MMIO_DH(FORCEWAKE_RENDER_GEN9, D_SKL_PLUS, NULL, mul_force_wake_write);
	MMIO_DH(FORCEWAKE_ACK_RENDER_GEN9, D_SKL_PLUS, NULL, NULL);
	MMIO_DH(FORCEWAKE_GT_GEN9, D_SKL_PLUS, NULL, mul_force_wake_write);
	MMIO_DH(FORCEWAKE_ACK_GT_GEN9, D_SKL_PLUS, NULL, NULL);
	MMIO_DH(FORCEWAKE_MEDIA_GEN9, D_SKL_PLUS, NULL, mul_force_wake_write);
	MMIO_DH(FORCEWAKE_ACK_MEDIA_GEN9, D_SKL_PLUS, NULL, NULL);

	MMIO_F(DP_AUX_CH_CTL(AUX_CH_B), 6 * 4, 0, 0, 0, D_SKL_PLUS, NULL,
						dp_aux_ch_ctl_mmio_write);
	MMIO_F(DP_AUX_CH_CTL(AUX_CH_C), 6 * 4, 0, 0, 0, D_SKL_PLUS, NULL,
						dp_aux_ch_ctl_mmio_write);
	MMIO_F(DP_AUX_CH_CTL(AUX_CH_D), 6 * 4, 0, 0, 0, D_SKL_PLUS, NULL,
						dp_aux_ch_ctl_mmio_write);

	MMIO_D(HSW_PWR_WELL_CTL1, D_SKL_PLUS);
	MMIO_DH(HSW_PWR_WELL_CTL2, D_SKL_PLUS, NULL, skl_power_well_ctl_write);

	MMIO_DH(DBUF_CTL_S(0), D_SKL_PLUS, NULL, gen9_dbuf_ctl_mmio_write);

	MMIO_D(GEN9_PG_ENABLE, D_SKL_PLUS);
	MMIO_D(GEN9_MEDIA_PG_IDLE_HYSTERESIS, D_SKL_PLUS);
	MMIO_D(GEN9_RENDER_PG_IDLE_HYSTERESIS, D_SKL_PLUS);
	MMIO_DFH(GEN9_GAMT_ECO_REG_RW_IA, D_SKL_PLUS, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(MMCD_MISC_CTRL, D_SKL_PLUS, F_CMD_ACCESS, NULL, NULL);
	MMIO_DH(CHICKEN_PAR1_1, D_SKL_PLUS, NULL, NULL);
	MMIO_D(DC_STATE_EN, D_SKL_PLUS);
	MMIO_D(DC_STATE_DEBUG, D_SKL_PLUS);
	MMIO_D(CDCLK_CTL, D_SKL_PLUS);
	MMIO_DH(LCPLL1_CTL, D_SKL_PLUS, NULL, skl_lcpll_write);
	MMIO_DH(LCPLL2_CTL, D_SKL_PLUS, NULL, skl_lcpll_write);
	MMIO_D(_MMIO(_DPLL1_CFGCR1), D_SKL_PLUS);
	MMIO_D(_MMIO(_DPLL2_CFGCR1), D_SKL_PLUS);
	MMIO_D(_MMIO(_DPLL3_CFGCR1), D_SKL_PLUS);
	MMIO_D(_MMIO(_DPLL1_CFGCR2), D_SKL_PLUS);
	MMIO_D(_MMIO(_DPLL2_CFGCR2), D_SKL_PLUS);
	MMIO_D(_MMIO(_DPLL3_CFGCR2), D_SKL_PLUS);
	MMIO_D(DPLL_CTRL1, D_SKL_PLUS);
	MMIO_D(DPLL_CTRL2, D_SKL_PLUS);
	MMIO_DH(DPLL_STATUS, D_SKL_PLUS, dpll_status_read, NULL);

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

	MMIO_DH(_MMIO(_REG_701C0(PIPE_A, 1)), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(_MMIO(_REG_701C0(PIPE_A, 2)), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(_MMIO(_REG_701C0(PIPE_A, 3)), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(_MMIO(_REG_701C0(PIPE_A, 4)), D_SKL_PLUS, NULL, NULL);

	MMIO_DH(_MMIO(_REG_701C0(PIPE_B, 1)), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(_MMIO(_REG_701C0(PIPE_B, 2)), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(_MMIO(_REG_701C0(PIPE_B, 3)), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(_MMIO(_REG_701C0(PIPE_B, 4)), D_SKL_PLUS, NULL, NULL);

	MMIO_DH(_MMIO(_REG_701C0(PIPE_C, 1)), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(_MMIO(_REG_701C0(PIPE_C, 2)), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(_MMIO(_REG_701C0(PIPE_C, 3)), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(_MMIO(_REG_701C0(PIPE_C, 4)), D_SKL_PLUS, NULL, NULL);

	MMIO_DH(_MMIO(_REG_701C4(PIPE_A, 1)), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(_MMIO(_REG_701C4(PIPE_A, 2)), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(_MMIO(_REG_701C4(PIPE_A, 3)), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(_MMIO(_REG_701C4(PIPE_A, 4)), D_SKL_PLUS, NULL, NULL);

	MMIO_DH(_MMIO(_REG_701C4(PIPE_B, 1)), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(_MMIO(_REG_701C4(PIPE_B, 2)), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(_MMIO(_REG_701C4(PIPE_B, 3)), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(_MMIO(_REG_701C4(PIPE_B, 4)), D_SKL_PLUS, NULL, NULL);

	MMIO_DH(_MMIO(_REG_701C4(PIPE_C, 1)), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(_MMIO(_REG_701C4(PIPE_C, 2)), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(_MMIO(_REG_701C4(PIPE_C, 3)), D_SKL_PLUS, NULL, NULL);
	MMIO_DH(_MMIO(_REG_701C4(PIPE_C, 4)), D_SKL_PLUS, NULL, NULL);

	MMIO_D(_MMIO(_PLANE_CTL_3_A), D_SKL_PLUS);
	MMIO_D(_MMIO(_PLANE_CTL_3_B), D_SKL_PLUS);
	MMIO_D(_MMIO(0x72380), D_SKL_PLUS);
	MMIO_D(_MMIO(0x7239c), D_SKL_PLUS);
	MMIO_D(_MMIO(_PLANE_SURF_3_A), D_SKL_PLUS);
	MMIO_D(_MMIO(_PLANE_SURF_3_B), D_SKL_PLUS);

	MMIO_D(CSR_SSP_BASE, D_SKL_PLUS);
	MMIO_D(CSR_HTP_SKL, D_SKL_PLUS);
	MMIO_D(CSR_LAST_WRITE, D_SKL_PLUS);

	MMIO_DFH(BDW_SCRATCH1, D_SKL_PLUS, F_CMD_ACCESS, NULL, NULL);

	MMIO_D(SKL_DFSM, D_SKL_PLUS);
	MMIO_D(DISPIO_CR_TX_BMU_CR0, D_SKL_PLUS);

	MMIO_F(GEN9_GFX_MOCS(0), 0x7f8, F_CMD_ACCESS, 0, 0, D_SKL_PLUS,
		NULL, NULL);
	MMIO_F(GEN7_L3CNTLREG2, 0x80, F_CMD_ACCESS, 0, 0, D_SKL_PLUS,
		NULL, NULL);

	MMIO_D(RPM_CONFIG0, D_SKL_PLUS);
	MMIO_D(_MMIO(0xd08), D_SKL_PLUS);
	MMIO_D(RC6_LOCATION, D_SKL_PLUS);
	MMIO_DFH(GEN7_FF_SLICE_CS_CHICKEN1, D_SKL_PLUS,
		 F_MODE_MASK | F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(GEN9_CS_DEBUG_MODE1, D_SKL_PLUS, F_MODE_MASK | F_CMD_ACCESS,
		NULL, NULL);

	/* TRTT */
	MMIO_DFH(TRVATTL3PTRDW(0), D_SKL_PLUS, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(TRVATTL3PTRDW(1), D_SKL_PLUS, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(TRVATTL3PTRDW(2), D_SKL_PLUS, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(TRVATTL3PTRDW(3), D_SKL_PLUS, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(TRVADR, D_SKL_PLUS, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(TRTTE, D_SKL_PLUS, F_CMD_ACCESS | F_PM_SAVE,
		 NULL, gen9_trtte_write);
	MMIO_DFH(_MMIO(0x4dfc), D_SKL_PLUS, F_PM_SAVE,
		 NULL, gen9_trtt_chicken_write);

	MMIO_D(_MMIO(0x46430), D_SKL_PLUS);

	MMIO_D(_MMIO(0x46520), D_SKL_PLUS);

	MMIO_D(_MMIO(0xc403c), D_SKL_PLUS);
	MMIO_DFH(GEN8_GARBCNTL, D_SKL_PLUS, F_CMD_ACCESS, NULL, NULL);
	MMIO_DH(DMA_CTRL, D_SKL_PLUS, NULL, dma_ctrl_write);

	MMIO_D(_MMIO(0x65900), D_SKL_PLUS);
	MMIO_D(GEN6_STOLEN_RESERVED, D_SKL_PLUS);
	MMIO_D(_MMIO(0x4068), D_SKL_PLUS);
	MMIO_D(_MMIO(0x67054), D_SKL_PLUS);
	MMIO_D(_MMIO(0x6e560), D_SKL_PLUS);
	MMIO_D(_MMIO(0x6e554), D_SKL_PLUS);
	MMIO_D(_MMIO(0x2b20), D_SKL_PLUS);
	MMIO_D(_MMIO(0x65f00), D_SKL_PLUS);
	MMIO_D(_MMIO(0x65f08), D_SKL_PLUS);
	MMIO_D(_MMIO(0x320f0), D_SKL_PLUS);

	MMIO_D(_MMIO(0x70034), D_SKL_PLUS);
	MMIO_D(_MMIO(0x71034), D_SKL_PLUS);
	MMIO_D(_MMIO(0x72034), D_SKL_PLUS);

	MMIO_D(_MMIO(_PLANE_KEYVAL_1(PIPE_A)), D_SKL_PLUS);
	MMIO_D(_MMIO(_PLANE_KEYVAL_1(PIPE_B)), D_SKL_PLUS);
	MMIO_D(_MMIO(_PLANE_KEYVAL_1(PIPE_C)), D_SKL_PLUS);
	MMIO_D(_MMIO(_PLANE_KEYMAX_1(PIPE_A)), D_SKL_PLUS);
	MMIO_D(_MMIO(_PLANE_KEYMAX_1(PIPE_B)), D_SKL_PLUS);
	MMIO_D(_MMIO(_PLANE_KEYMAX_1(PIPE_C)), D_SKL_PLUS);
	MMIO_D(_MMIO(_PLANE_KEYMSK_1(PIPE_A)), D_SKL_PLUS);
	MMIO_D(_MMIO(_PLANE_KEYMSK_1(PIPE_B)), D_SKL_PLUS);
	MMIO_D(_MMIO(_PLANE_KEYMSK_1(PIPE_C)), D_SKL_PLUS);

	MMIO_D(_MMIO(0x44500), D_SKL_PLUS);
#define CSFE_CHICKEN1_REG(base) _MMIO((base) + 0xD4)
	MMIO_RING_DFH(CSFE_CHICKEN1_REG, D_SKL_PLUS, F_MODE_MASK | F_CMD_ACCESS,
		      NULL, csfe_chicken1_mmio_write);
#undef CSFE_CHICKEN1_REG
	MMIO_DFH(GEN8_HDC_CHICKEN1, D_SKL_PLUS, F_MODE_MASK | F_CMD_ACCESS,
		 NULL, NULL);
	MMIO_DFH(GEN9_WM_CHICKEN3, D_SKL_PLUS, F_MODE_MASK | F_CMD_ACCESS,
		 NULL, NULL);

	MMIO_DFH(GAMT_CHKN_BIT_REG, D_KBL | D_CFL, F_CMD_ACCESS, NULL, NULL);
	MMIO_D(GEN9_CTX_PREEMPT_REG, D_SKL_PLUS & ~D_BXT);

	return 0;
}

static int init_bxt_mmio_info(struct intel_gvt *gvt)
{
	struct drm_i915_private *dev_priv = gvt->gt->i915;
	int ret;

	MMIO_F(_MMIO(0x80000), 0x3000, 0, 0, 0, D_BXT, NULL, NULL);

	MMIO_D(GEN7_SAMPLER_INSTDONE, D_BXT);
	MMIO_D(GEN7_ROW_INSTDONE, D_BXT);
	MMIO_D(GEN8_FAULT_TLB_DATA0, D_BXT);
	MMIO_D(GEN8_FAULT_TLB_DATA1, D_BXT);
	MMIO_D(ERROR_GEN6, D_BXT);
	MMIO_D(DONE_REG, D_BXT);
	MMIO_D(EIR, D_BXT);
	MMIO_D(PGTBL_ER, D_BXT);
	MMIO_D(_MMIO(0x4194), D_BXT);
	MMIO_D(_MMIO(0x4294), D_BXT);
	MMIO_D(_MMIO(0x4494), D_BXT);

	MMIO_RING_D(RING_PSMI_CTL, D_BXT);
	MMIO_RING_D(RING_DMA_FADD, D_BXT);
	MMIO_RING_D(RING_DMA_FADD_UDW, D_BXT);
	MMIO_RING_D(RING_IPEHR, D_BXT);
	MMIO_RING_D(RING_INSTPS, D_BXT);
	MMIO_RING_D(RING_BBADDR_UDW, D_BXT);
	MMIO_RING_D(RING_BBSTATE, D_BXT);
	MMIO_RING_D(RING_IPEIR, D_BXT);

	MMIO_F(SOFT_SCRATCH(0), 16 * 4, 0, 0, 0, D_BXT, NULL, NULL);

	MMIO_DH(BXT_P_CR_GT_DISP_PWRON, D_BXT, NULL, bxt_gt_disp_pwron_write);
	MMIO_D(BXT_RP_STATE_CAP, D_BXT);
	MMIO_DH(BXT_PHY_CTL_FAMILY(DPIO_PHY0), D_BXT,
		NULL, bxt_phy_ctl_family_write);
	MMIO_DH(BXT_PHY_CTL_FAMILY(DPIO_PHY1), D_BXT,
		NULL, bxt_phy_ctl_family_write);
	MMIO_D(BXT_PHY_CTL(PORT_A), D_BXT);
	MMIO_D(BXT_PHY_CTL(PORT_B), D_BXT);
	MMIO_D(BXT_PHY_CTL(PORT_C), D_BXT);
	MMIO_DH(BXT_PORT_PLL_ENABLE(PORT_A), D_BXT,
		NULL, bxt_port_pll_enable_write);
	MMIO_DH(BXT_PORT_PLL_ENABLE(PORT_B), D_BXT,
		NULL, bxt_port_pll_enable_write);
	MMIO_DH(BXT_PORT_PLL_ENABLE(PORT_C), D_BXT, NULL,
		bxt_port_pll_enable_write);

	MMIO_D(BXT_PORT_CL1CM_DW0(DPIO_PHY0), D_BXT);
	MMIO_D(BXT_PORT_CL1CM_DW9(DPIO_PHY0), D_BXT);
	MMIO_D(BXT_PORT_CL1CM_DW10(DPIO_PHY0), D_BXT);
	MMIO_D(BXT_PORT_CL1CM_DW28(DPIO_PHY0), D_BXT);
	MMIO_D(BXT_PORT_CL1CM_DW30(DPIO_PHY0), D_BXT);
	MMIO_D(BXT_PORT_CL2CM_DW6(DPIO_PHY0), D_BXT);
	MMIO_D(BXT_PORT_REF_DW3(DPIO_PHY0), D_BXT);
	MMIO_D(BXT_PORT_REF_DW6(DPIO_PHY0), D_BXT);
	MMIO_D(BXT_PORT_REF_DW8(DPIO_PHY0), D_BXT);

	MMIO_D(BXT_PORT_CL1CM_DW0(DPIO_PHY1), D_BXT);
	MMIO_D(BXT_PORT_CL1CM_DW9(DPIO_PHY1), D_BXT);
	MMIO_D(BXT_PORT_CL1CM_DW10(DPIO_PHY1), D_BXT);
	MMIO_D(BXT_PORT_CL1CM_DW28(DPIO_PHY1), D_BXT);
	MMIO_D(BXT_PORT_CL1CM_DW30(DPIO_PHY1), D_BXT);
	MMIO_D(BXT_PORT_CL2CM_DW6(DPIO_PHY1), D_BXT);
	MMIO_D(BXT_PORT_REF_DW3(DPIO_PHY1), D_BXT);
	MMIO_D(BXT_PORT_REF_DW6(DPIO_PHY1), D_BXT);
	MMIO_D(BXT_PORT_REF_DW8(DPIO_PHY1), D_BXT);

	MMIO_D(BXT_PORT_PLL_EBB_0(DPIO_PHY0, DPIO_CH0), D_BXT);
	MMIO_D(BXT_PORT_PLL_EBB_4(DPIO_PHY0, DPIO_CH0), D_BXT);
	MMIO_D(BXT_PORT_PCS_DW10_LN01(DPIO_PHY0, DPIO_CH0), D_BXT);
	MMIO_D(BXT_PORT_PCS_DW10_GRP(DPIO_PHY0, DPIO_CH0), D_BXT);
	MMIO_D(BXT_PORT_PCS_DW12_LN01(DPIO_PHY0, DPIO_CH0), D_BXT);
	MMIO_D(BXT_PORT_PCS_DW12_LN23(DPIO_PHY0, DPIO_CH0), D_BXT);
	MMIO_DH(BXT_PORT_PCS_DW12_GRP(DPIO_PHY0, DPIO_CH0), D_BXT,
		NULL, bxt_pcs_dw12_grp_write);
	MMIO_D(BXT_PORT_TX_DW2_LN0(DPIO_PHY0, DPIO_CH0), D_BXT);
	MMIO_D(BXT_PORT_TX_DW2_GRP(DPIO_PHY0, DPIO_CH0), D_BXT);
	MMIO_DH(BXT_PORT_TX_DW3_LN0(DPIO_PHY0, DPIO_CH0), D_BXT,
		bxt_port_tx_dw3_read, NULL);
	MMIO_D(BXT_PORT_TX_DW3_GRP(DPIO_PHY0, DPIO_CH0), D_BXT);
	MMIO_D(BXT_PORT_TX_DW4_LN0(DPIO_PHY0, DPIO_CH0), D_BXT);
	MMIO_D(BXT_PORT_TX_DW4_GRP(DPIO_PHY0, DPIO_CH0), D_BXT);
	MMIO_D(BXT_PORT_TX_DW14_LN(DPIO_PHY0, DPIO_CH0, 0), D_BXT);
	MMIO_D(BXT_PORT_TX_DW14_LN(DPIO_PHY0, DPIO_CH0, 1), D_BXT);
	MMIO_D(BXT_PORT_TX_DW14_LN(DPIO_PHY0, DPIO_CH0, 2), D_BXT);
	MMIO_D(BXT_PORT_TX_DW14_LN(DPIO_PHY0, DPIO_CH0, 3), D_BXT);
	MMIO_D(BXT_PORT_PLL(DPIO_PHY0, DPIO_CH0, 0), D_BXT);
	MMIO_D(BXT_PORT_PLL(DPIO_PHY0, DPIO_CH0, 1), D_BXT);
	MMIO_D(BXT_PORT_PLL(DPIO_PHY0, DPIO_CH0, 2), D_BXT);
	MMIO_D(BXT_PORT_PLL(DPIO_PHY0, DPIO_CH0, 3), D_BXT);
	MMIO_D(BXT_PORT_PLL(DPIO_PHY0, DPIO_CH0, 6), D_BXT);
	MMIO_D(BXT_PORT_PLL(DPIO_PHY0, DPIO_CH0, 8), D_BXT);
	MMIO_D(BXT_PORT_PLL(DPIO_PHY0, DPIO_CH0, 9), D_BXT);
	MMIO_D(BXT_PORT_PLL(DPIO_PHY0, DPIO_CH0, 10), D_BXT);

	MMIO_D(BXT_PORT_PLL_EBB_0(DPIO_PHY0, DPIO_CH1), D_BXT);
	MMIO_D(BXT_PORT_PLL_EBB_4(DPIO_PHY0, DPIO_CH1), D_BXT);
	MMIO_D(BXT_PORT_PCS_DW10_LN01(DPIO_PHY0, DPIO_CH1), D_BXT);
	MMIO_D(BXT_PORT_PCS_DW10_GRP(DPIO_PHY0, DPIO_CH1), D_BXT);
	MMIO_D(BXT_PORT_PCS_DW12_LN01(DPIO_PHY0, DPIO_CH1), D_BXT);
	MMIO_D(BXT_PORT_PCS_DW12_LN23(DPIO_PHY0, DPIO_CH1), D_BXT);
	MMIO_DH(BXT_PORT_PCS_DW12_GRP(DPIO_PHY0, DPIO_CH1), D_BXT,
		NULL, bxt_pcs_dw12_grp_write);
	MMIO_D(BXT_PORT_TX_DW2_LN0(DPIO_PHY0, DPIO_CH1), D_BXT);
	MMIO_D(BXT_PORT_TX_DW2_GRP(DPIO_PHY0, DPIO_CH1), D_BXT);
	MMIO_DH(BXT_PORT_TX_DW3_LN0(DPIO_PHY0, DPIO_CH1), D_BXT,
		bxt_port_tx_dw3_read, NULL);
	MMIO_D(BXT_PORT_TX_DW3_GRP(DPIO_PHY0, DPIO_CH1), D_BXT);
	MMIO_D(BXT_PORT_TX_DW4_LN0(DPIO_PHY0, DPIO_CH1), D_BXT);
	MMIO_D(BXT_PORT_TX_DW4_GRP(DPIO_PHY0, DPIO_CH1), D_BXT);
	MMIO_D(BXT_PORT_TX_DW14_LN(DPIO_PHY0, DPIO_CH1, 0), D_BXT);
	MMIO_D(BXT_PORT_TX_DW14_LN(DPIO_PHY0, DPIO_CH1, 1), D_BXT);
	MMIO_D(BXT_PORT_TX_DW14_LN(DPIO_PHY0, DPIO_CH1, 2), D_BXT);
	MMIO_D(BXT_PORT_TX_DW14_LN(DPIO_PHY0, DPIO_CH1, 3), D_BXT);
	MMIO_D(BXT_PORT_PLL(DPIO_PHY0, DPIO_CH1, 0), D_BXT);
	MMIO_D(BXT_PORT_PLL(DPIO_PHY0, DPIO_CH1, 1), D_BXT);
	MMIO_D(BXT_PORT_PLL(DPIO_PHY0, DPIO_CH1, 2), D_BXT);
	MMIO_D(BXT_PORT_PLL(DPIO_PHY0, DPIO_CH1, 3), D_BXT);
	MMIO_D(BXT_PORT_PLL(DPIO_PHY0, DPIO_CH1, 6), D_BXT);
	MMIO_D(BXT_PORT_PLL(DPIO_PHY0, DPIO_CH1, 8), D_BXT);
	MMIO_D(BXT_PORT_PLL(DPIO_PHY0, DPIO_CH1, 9), D_BXT);
	MMIO_D(BXT_PORT_PLL(DPIO_PHY0, DPIO_CH1, 10), D_BXT);

	MMIO_D(BXT_PORT_PLL_EBB_0(DPIO_PHY1, DPIO_CH0), D_BXT);
	MMIO_D(BXT_PORT_PLL_EBB_4(DPIO_PHY1, DPIO_CH0), D_BXT);
	MMIO_D(BXT_PORT_PCS_DW10_LN01(DPIO_PHY1, DPIO_CH0), D_BXT);
	MMIO_D(BXT_PORT_PCS_DW10_GRP(DPIO_PHY1, DPIO_CH0), D_BXT);
	MMIO_D(BXT_PORT_PCS_DW12_LN01(DPIO_PHY1, DPIO_CH0), D_BXT);
	MMIO_D(BXT_PORT_PCS_DW12_LN23(DPIO_PHY1, DPIO_CH0), D_BXT);
	MMIO_DH(BXT_PORT_PCS_DW12_GRP(DPIO_PHY1, DPIO_CH0), D_BXT,
		NULL, bxt_pcs_dw12_grp_write);
	MMIO_D(BXT_PORT_TX_DW2_LN0(DPIO_PHY1, DPIO_CH0), D_BXT);
	MMIO_D(BXT_PORT_TX_DW2_GRP(DPIO_PHY1, DPIO_CH0), D_BXT);
	MMIO_DH(BXT_PORT_TX_DW3_LN0(DPIO_PHY1, DPIO_CH0), D_BXT,
		bxt_port_tx_dw3_read, NULL);
	MMIO_D(BXT_PORT_TX_DW3_GRP(DPIO_PHY1, DPIO_CH0), D_BXT);
	MMIO_D(BXT_PORT_TX_DW4_LN0(DPIO_PHY1, DPIO_CH0), D_BXT);
	MMIO_D(BXT_PORT_TX_DW4_GRP(DPIO_PHY1, DPIO_CH0), D_BXT);
	MMIO_D(BXT_PORT_TX_DW14_LN(DPIO_PHY1, DPIO_CH0, 0), D_BXT);
	MMIO_D(BXT_PORT_TX_DW14_LN(DPIO_PHY1, DPIO_CH0, 1), D_BXT);
	MMIO_D(BXT_PORT_TX_DW14_LN(DPIO_PHY1, DPIO_CH0, 2), D_BXT);
	MMIO_D(BXT_PORT_TX_DW14_LN(DPIO_PHY1, DPIO_CH0, 3), D_BXT);
	MMIO_D(BXT_PORT_PLL(DPIO_PHY1, DPIO_CH0, 0), D_BXT);
	MMIO_D(BXT_PORT_PLL(DPIO_PHY1, DPIO_CH0, 1), D_BXT);
	MMIO_D(BXT_PORT_PLL(DPIO_PHY1, DPIO_CH0, 2), D_BXT);
	MMIO_D(BXT_PORT_PLL(DPIO_PHY1, DPIO_CH0, 3), D_BXT);
	MMIO_D(BXT_PORT_PLL(DPIO_PHY1, DPIO_CH0, 6), D_BXT);
	MMIO_D(BXT_PORT_PLL(DPIO_PHY1, DPIO_CH0, 8), D_BXT);
	MMIO_D(BXT_PORT_PLL(DPIO_PHY1, DPIO_CH0, 9), D_BXT);
	MMIO_D(BXT_PORT_PLL(DPIO_PHY1, DPIO_CH0, 10), D_BXT);

	MMIO_D(BXT_DE_PLL_CTL, D_BXT);
	MMIO_DH(BXT_DE_PLL_ENABLE, D_BXT, NULL, bxt_de_pll_enable_write);
	MMIO_D(BXT_DSI_PLL_CTL, D_BXT);
	MMIO_D(BXT_DSI_PLL_ENABLE, D_BXT);

	MMIO_D(GEN9_CLKGATE_DIS_0, D_BXT);
	MMIO_D(GEN9_CLKGATE_DIS_4, D_BXT);

	MMIO_D(HSW_TVIDEO_DIP_GCP(TRANSCODER_A), D_BXT);
	MMIO_D(HSW_TVIDEO_DIP_GCP(TRANSCODER_B), D_BXT);
	MMIO_D(HSW_TVIDEO_DIP_GCP(TRANSCODER_C), D_BXT);

	MMIO_D(RC6_CTX_BASE, D_BXT);

	MMIO_D(GEN8_PUSHBUS_CONTROL, D_BXT);
	MMIO_D(GEN8_PUSHBUS_ENABLE, D_BXT);
	MMIO_D(GEN8_PUSHBUS_SHIFT, D_BXT);
	MMIO_D(GEN6_GFXPAUSE, D_BXT);
	MMIO_DFH(GEN8_L3SQCREG1, D_BXT, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(GEN8_L3CNTLREG, D_BXT, F_CMD_ACCESS, NULL, NULL);
	MMIO_DFH(_MMIO(0x20D8), D_BXT, F_CMD_ACCESS, NULL, NULL);
	MMIO_F(GEN8_RING_CS_GPR(RENDER_RING_BASE, 0), 0x40, F_CMD_ACCESS,
	       0, 0, D_BXT, NULL, NULL);
	MMIO_F(GEN8_RING_CS_GPR(GEN6_BSD_RING_BASE, 0), 0x40, F_CMD_ACCESS,
	       0, 0, D_BXT, NULL, NULL);
	MMIO_F(GEN8_RING_CS_GPR(BLT_RING_BASE, 0), 0x40, F_CMD_ACCESS,
	       0, 0, D_BXT, NULL, NULL);
	MMIO_F(GEN8_RING_CS_GPR(VEBOX_RING_BASE, 0), 0x40, F_CMD_ACCESS,
	       0, 0, D_BXT, NULL, NULL);

	MMIO_DFH(GEN9_CTX_PREEMPT_REG, D_BXT, F_CMD_ACCESS, NULL, NULL);

	MMIO_DH(GEN8_PRIVATE_PAT_LO, D_BXT, NULL, bxt_ppat_low_write);

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
		if (offset >= i915_mmio_reg_offset(block->offset) &&
		    offset < i915_mmio_reg_offset(block->offset) + block->size)
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

/* Special MMIO blocks. registers in MMIO block ranges should not be command
 * accessible (should have no F_CMD_ACCESS flag).
 * otherwise, need to update cmd_reg_handler in cmd_parser.c
 */
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
	struct drm_i915_private *i915 = gvt->gt->i915;
	int size = info->mmio_size / 4 * sizeof(*gvt->mmio.mmio_attribute);
	int ret;

	gvt->mmio.mmio_attribute = vzalloc(size);
	if (!gvt->mmio.mmio_attribute)
		return -ENOMEM;

	ret = init_generic_mmio_info(gvt);
	if (ret)
		goto err;

	if (IS_BROADWELL(i915)) {
		ret = init_bdw_mmio_info(gvt);
		if (ret)
			goto err;
	} else if (IS_SKYLAKE(i915) ||
		   IS_KABYLAKE(i915) ||
		   IS_COFFEELAKE(i915) ||
		   IS_COMETLAKE(i915)) {
		ret = init_bdw_mmio_info(gvt);
		if (ret)
			goto err;
		ret = init_skl_mmio_info(gvt);
		if (ret)
			goto err;
	} else if (IS_BROXTON(i915)) {
		ret = init_bdw_mmio_info(gvt);
		if (ret)
			goto err;
		ret = init_skl_mmio_info(gvt);
		if (ret)
			goto err;
		ret = init_bxt_mmio_info(gvt);
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
		/* pvinfo data doesn't come from hw mmio */
		if (i915_mmio_reg_offset(block->offset) == VGT_PVINFO_PAGE)
			continue;

		for (j = 0; j < block->size; j += 4) {
			ret = handler(gvt,
				      i915_mmio_reg_offset(block->offset) + j,
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
 * intel_vgpu_mask_mmio_write - write mask register
 * @vgpu: a vGPU
 * @offset: access offset
 * @p_data: write data buffer
 * @bytes: access data length
 *
 * Returns:
 * Zero on success, negative error code if failed.
 */
int intel_vgpu_mask_mmio_write(struct intel_vgpu *vgpu, unsigned int offset,
		void *p_data, unsigned int bytes)
{
	u32 mask, old_vreg;

	old_vreg = vgpu_vreg(vgpu, offset);
	write_vreg(vgpu, offset, p_data, bytes);
	mask = vgpu_vreg(vgpu, offset) >> 16;
	vgpu_vreg(vgpu, offset) = (old_vreg & ~mask) |
				(vgpu_vreg(vgpu, offset) & mask);

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
 * @is_read: read or write
 *
 * Returns:
 * Zero on success, negative error code if failed.
 */
int intel_vgpu_mmio_reg_rw(struct intel_vgpu *vgpu, unsigned int offset,
			   void *pdata, unsigned int bytes, bool is_read)
{
	struct drm_i915_private *i915 = vgpu->gvt->gt->i915;
	struct intel_gvt *gvt = vgpu->gvt;
	struct intel_gvt_mmio_info *mmio_info;
	struct gvt_mmio_block *mmio_block;
	gvt_mmio_func func;
	int ret;

	if (drm_WARN_ON(&i915->drm, bytes > 8))
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
	mmio_info = intel_gvt_find_mmio_info(gvt, offset);
	if (!mmio_info) {
		gvt_dbg_mmio("untracked MMIO %08x len %d\n", offset, bytes);
		goto default_rw;
	}

	if (is_read)
		return mmio_info->read(vgpu, offset, pdata, bytes);
	else {
		u64 ro_mask = mmio_info->ro_mask;
		u32 old_vreg = 0;
		u64 data = 0;

		if (intel_gvt_mmio_has_mode_mask(gvt, mmio_info->offset)) {
			old_vreg = vgpu_vreg(vgpu, offset);
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
		}
	}

	return ret;

default_rw:
	return is_read ?
		intel_vgpu_default_mmio_read(vgpu, offset, pdata, bytes) :
		intel_vgpu_default_mmio_write(vgpu, offset, pdata, bytes);
}

void intel_gvt_restore_fence(struct intel_gvt *gvt)
{
	struct intel_vgpu *vgpu;
	int i, id;

	idr_for_each_entry(&(gvt)->vgpu_idr, vgpu, id) {
		mmio_hw_access_pre(gvt->gt);
		for (i = 0; i < vgpu_fence_sz(vgpu); i++)
			intel_vgpu_write_fence(vgpu, i, vgpu_vreg64(vgpu, fence_num_to_offset(i)));
		mmio_hw_access_post(gvt->gt);
	}
}

static int mmio_pm_restore_handler(struct intel_gvt *gvt, u32 offset, void *data)
{
	struct intel_vgpu *vgpu = data;
	struct drm_i915_private *dev_priv = gvt->gt->i915;

	if (gvt->mmio.mmio_attribute[offset >> 2] & F_PM_SAVE)
		intel_uncore_write(&dev_priv->uncore, _MMIO(offset), vgpu_vreg(vgpu, offset));

	return 0;
}

void intel_gvt_restore_mmio(struct intel_gvt *gvt)
{
	struct intel_vgpu *vgpu;
	int id;

	idr_for_each_entry(&(gvt)->vgpu_idr, vgpu, id) {
		mmio_hw_access_pre(gvt->gt);
		intel_gvt_for_each_tracked_mmio(gvt, mmio_pm_restore_handler, vgpu);
		mmio_hw_access_post(gvt->gt);
	}
}
