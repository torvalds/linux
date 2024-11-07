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
 *    Zhiyuan Lv <zhiyuan.lv@intel.com>
 *
 * Contributors:
 *    Terrence Xu <terrence.xu@intel.com>
 *    Changbin Du <changbin.du@intel.com>
 *    Bing Niu <bing.niu@intel.com>
 *    Zhi Wang <zhi.a.wang@intel.com>
 *
 */

#include <drm/display/drm_dp.h>

#include "i915_drv.h"
#include "i915_reg.h"
#include "gvt.h"

#include "display/bxt_dpio_phy_regs.h"
#include "display/i9xx_plane_regs.h"
#include "display/intel_crt_regs.h"
#include "display/intel_cursor_regs.h"
#include "display/intel_display.h"
#include "display/intel_dpio_phy.h"
#include "display/intel_sprite_regs.h"

static int get_edp_pipe(struct intel_vgpu *vgpu)
{
	u32 data = vgpu_vreg(vgpu, _TRANS_DDI_FUNC_CTL_EDP);
	int pipe = -1;

	switch (data & TRANS_DDI_EDP_INPUT_MASK) {
	case TRANS_DDI_EDP_INPUT_A_ON:
	case TRANS_DDI_EDP_INPUT_A_ONOFF:
		pipe = PIPE_A;
		break;
	case TRANS_DDI_EDP_INPUT_B_ONOFF:
		pipe = PIPE_B;
		break;
	case TRANS_DDI_EDP_INPUT_C_ONOFF:
		pipe = PIPE_C;
		break;
	}
	return pipe;
}

static int edp_pipe_is_enabled(struct intel_vgpu *vgpu)
{
	struct drm_i915_private *dev_priv = vgpu->gvt->gt->i915;

	if (!(vgpu_vreg_t(vgpu, TRANSCONF(dev_priv, TRANSCODER_EDP)) & TRANSCONF_ENABLE))
		return 0;

	if (!(vgpu_vreg(vgpu, _TRANS_DDI_FUNC_CTL_EDP) & TRANS_DDI_FUNC_ENABLE))
		return 0;
	return 1;
}

int pipe_is_enabled(struct intel_vgpu *vgpu, int pipe)
{
	struct drm_i915_private *dev_priv = vgpu->gvt->gt->i915;

	if (drm_WARN_ON(&dev_priv->drm,
			pipe < PIPE_A || pipe >= I915_MAX_PIPES))
		return -EINVAL;

	if (vgpu_vreg_t(vgpu, TRANSCONF(dev_priv, pipe)) & TRANSCONF_ENABLE)
		return 1;

	if (edp_pipe_is_enabled(vgpu) &&
			get_edp_pipe(vgpu) == pipe)
		return 1;
	return 0;
}

static unsigned char virtual_dp_monitor_edid[GVT_EDID_NUM][EDID_SIZE] = {
	{
/* EDID with 1024x768 as its resolution */
		/*Header*/
		0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00,
		/* Vendor & Product Identification */
		0x22, 0xf0, 0x54, 0x29, 0x00, 0x00, 0x00, 0x00, 0x04, 0x17,
		/* Version & Revision */
		0x01, 0x04,
		/* Basic Display Parameters & Features */
		0xa5, 0x34, 0x20, 0x78, 0x23,
		/* Color Characteristics */
		0xfc, 0x81, 0xa4, 0x55, 0x4d, 0x9d, 0x25, 0x12, 0x50, 0x54,
		/* Established Timings: maximum resolution is 1024x768 */
		0x21, 0x08, 0x00,
		/* Standard Timings. All invalid */
		0x00, 0xc0, 0x00, 0xc0, 0x00, 0x40, 0x00, 0x80, 0x00, 0x00,
		0x00, 0x40, 0x00, 0x00, 0x00, 0x01,
		/* 18 Byte Data Blocks 1: invalid */
		0x00, 0x00, 0x80, 0xa0, 0x70, 0xb0,
		0x23, 0x40, 0x30, 0x20, 0x36, 0x00, 0x06, 0x44, 0x21, 0x00, 0x00, 0x1a,
		/* 18 Byte Data Blocks 2: invalid */
		0x00, 0x00, 0x00, 0xfd, 0x00, 0x18, 0x3c, 0x18, 0x50, 0x11, 0x00, 0x0a,
		0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
		/* 18 Byte Data Blocks 3: invalid */
		0x00, 0x00, 0x00, 0xfc, 0x00, 0x48,
		0x50, 0x20, 0x5a, 0x52, 0x32, 0x34, 0x34, 0x30, 0x77, 0x0a, 0x20, 0x20,
		/* 18 Byte Data Blocks 4: invalid */
		0x00, 0x00, 0x00, 0xff, 0x00, 0x43, 0x4e, 0x34, 0x33, 0x30, 0x34, 0x30,
		0x44, 0x58, 0x51, 0x0a, 0x20, 0x20,
		/* Extension Block Count */
		0x00,
		/* Checksum */
		0xef,
	},
	{
/* EDID with 1920x1200 as its resolution */
		/*Header*/
		0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00,
		/* Vendor & Product Identification */
		0x22, 0xf0, 0x54, 0x29, 0x00, 0x00, 0x00, 0x00, 0x04, 0x17,
		/* Version & Revision */
		0x01, 0x04,
		/* Basic Display Parameters & Features */
		0xa5, 0x34, 0x20, 0x78, 0x23,
		/* Color Characteristics */
		0xfc, 0x81, 0xa4, 0x55, 0x4d, 0x9d, 0x25, 0x12, 0x50, 0x54,
		/* Established Timings: maximum resolution is 1024x768 */
		0x21, 0x08, 0x00,
		/*
		 * Standard Timings.
		 * below new resolutions can be supported:
		 * 1920x1080, 1280x720, 1280x960, 1280x1024,
		 * 1440x900, 1600x1200, 1680x1050
		 */
		0xd1, 0xc0, 0x81, 0xc0, 0x81, 0x40, 0x81, 0x80, 0x95, 0x00,
		0xa9, 0x40, 0xb3, 0x00, 0x01, 0x01,
		/* 18 Byte Data Blocks 1: max resolution is 1920x1200 */
		0x28, 0x3c, 0x80, 0xa0, 0x70, 0xb0,
		0x23, 0x40, 0x30, 0x20, 0x36, 0x00, 0x06, 0x44, 0x21, 0x00, 0x00, 0x1a,
		/* 18 Byte Data Blocks 2: invalid */
		0x00, 0x00, 0x00, 0xfd, 0x00, 0x18, 0x3c, 0x18, 0x50, 0x11, 0x00, 0x0a,
		0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
		/* 18 Byte Data Blocks 3: invalid */
		0x00, 0x00, 0x00, 0xfc, 0x00, 0x48,
		0x50, 0x20, 0x5a, 0x52, 0x32, 0x34, 0x34, 0x30, 0x77, 0x0a, 0x20, 0x20,
		/* 18 Byte Data Blocks 4: invalid */
		0x00, 0x00, 0x00, 0xff, 0x00, 0x43, 0x4e, 0x34, 0x33, 0x30, 0x34, 0x30,
		0x44, 0x58, 0x51, 0x0a, 0x20, 0x20,
		/* Extension Block Count */
		0x00,
		/* Checksum */
		0x45,
	},
};

#define DPCD_HEADER_SIZE        0xb

/* let the virtual display supports DP1.2 */
static u8 dpcd_fix_data[DPCD_HEADER_SIZE] = {
	0x12, 0x014, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static void emulate_monitor_status_change(struct intel_vgpu *vgpu)
{
	struct drm_i915_private *dev_priv = vgpu->gvt->gt->i915;
	int pipe;

	if (IS_BROXTON(dev_priv)) {
		enum transcoder trans;
		enum port port;

		/* Clear PIPE, DDI, PHY, HPD before setting new */
		vgpu_vreg_t(vgpu, GEN8_DE_PORT_ISR) &=
			~(GEN8_DE_PORT_HOTPLUG(HPD_PORT_A) |
			  GEN8_DE_PORT_HOTPLUG(HPD_PORT_B) |
			  GEN8_DE_PORT_HOTPLUG(HPD_PORT_C));

		for_each_pipe(dev_priv, pipe) {
			vgpu_vreg_t(vgpu, TRANSCONF(dev_priv, pipe)) &=
				~(TRANSCONF_ENABLE | TRANSCONF_STATE_ENABLE);
			vgpu_vreg_t(vgpu, DSPCNTR(dev_priv, pipe)) &= ~DISP_ENABLE;
			vgpu_vreg_t(vgpu, SPRCTL(pipe)) &= ~SPRITE_ENABLE;
			vgpu_vreg_t(vgpu, CURCNTR(dev_priv, pipe)) &= ~MCURSOR_MODE_MASK;
			vgpu_vreg_t(vgpu, CURCNTR(dev_priv, pipe)) |= MCURSOR_MODE_DISABLE;
		}

		for (trans = TRANSCODER_A; trans <= TRANSCODER_EDP; trans++) {
			vgpu_vreg_t(vgpu, TRANS_DDI_FUNC_CTL(dev_priv, trans)) &=
				~(TRANS_DDI_BPC_MASK | TRANS_DDI_MODE_SELECT_MASK |
				  TRANS_DDI_PORT_MASK | TRANS_DDI_FUNC_ENABLE);
		}
		vgpu_vreg_t(vgpu, TRANS_DDI_FUNC_CTL(dev_priv, TRANSCODER_A)) &=
			~(TRANS_DDI_BPC_MASK | TRANS_DDI_MODE_SELECT_MASK |
			  TRANS_DDI_PORT_MASK);

		for (port = PORT_A; port <= PORT_C; port++) {
			vgpu_vreg_t(vgpu, BXT_PHY_CTL(port)) &=
				~BXT_PHY_LANE_ENABLED;
			vgpu_vreg_t(vgpu, BXT_PHY_CTL(port)) |=
				(BXT_PHY_CMNLANE_POWERDOWN_ACK |
				 BXT_PHY_LANE_POWERDOWN_ACK);

			vgpu_vreg_t(vgpu, BXT_PORT_PLL_ENABLE(port)) &=
				~(PORT_PLL_POWER_STATE | PORT_PLL_POWER_ENABLE |
				  PORT_PLL_REF_SEL | PORT_PLL_LOCK |
				  PORT_PLL_ENABLE);

			vgpu_vreg_t(vgpu, DDI_BUF_CTL(port)) &=
				~(DDI_INIT_DISPLAY_DETECTED |
				  DDI_BUF_CTL_ENABLE);
			vgpu_vreg_t(vgpu, DDI_BUF_CTL(port)) |= DDI_BUF_IS_IDLE;
		}
		vgpu_vreg_t(vgpu, PCH_PORT_HOTPLUG) &=
			~(PORTA_HOTPLUG_ENABLE | PORTA_HOTPLUG_STATUS_MASK);
		vgpu_vreg_t(vgpu, PCH_PORT_HOTPLUG) &=
			~(PORTB_HOTPLUG_ENABLE | PORTB_HOTPLUG_STATUS_MASK);
		vgpu_vreg_t(vgpu, PCH_PORT_HOTPLUG) &=
			~(PORTC_HOTPLUG_ENABLE | PORTC_HOTPLUG_STATUS_MASK);
		/* No hpd_invert set in vgpu vbt, need to clear invert mask */
		vgpu_vreg_t(vgpu, PCH_PORT_HOTPLUG) &= ~BXT_DDI_HPD_INVERT_MASK;
		vgpu_vreg_t(vgpu, GEN8_DE_PORT_ISR) &= ~BXT_DE_PORT_HOTPLUG_MASK;

		vgpu_vreg_t(vgpu, BXT_P_CR_GT_DISP_PWRON) &= ~(BIT(0) | BIT(1));
		vgpu_vreg_t(vgpu, BXT_PORT_CL1CM_DW0(DPIO_PHY0)) &=
			~PHY_POWER_GOOD;
		vgpu_vreg_t(vgpu, BXT_PORT_CL1CM_DW0(DPIO_PHY1)) &=
			~PHY_POWER_GOOD;
		vgpu_vreg_t(vgpu, BXT_PHY_CTL_FAMILY(DPIO_PHY0)) &= ~BIT(30);
		vgpu_vreg_t(vgpu, BXT_PHY_CTL_FAMILY(DPIO_PHY1)) &= ~BIT(30);

		vgpu_vreg_t(vgpu, SFUSE_STRAP) &= ~SFUSE_STRAP_DDIB_DETECTED;
		vgpu_vreg_t(vgpu, SFUSE_STRAP) &= ~SFUSE_STRAP_DDIC_DETECTED;

		/*
		 * Only 1 PIPE enabled in current vGPU display and PIPE_A is
		 *  tied to TRANSCODER_A in HW, so it's safe to assume PIPE_A,
		 *   TRANSCODER_A can be enabled. PORT_x depends on the input of
		 *   setup_virtual_dp_monitor.
		 */
		vgpu_vreg_t(vgpu, TRANSCONF(dev_priv, TRANSCODER_A)) |= TRANSCONF_ENABLE;
		vgpu_vreg_t(vgpu, TRANSCONF(dev_priv, TRANSCODER_A)) |= TRANSCONF_STATE_ENABLE;

		/*
		 * Golden M/N are calculated based on:
		 *   24 bpp, 4 lanes, 154000 pixel clk (from virtual EDID),
		 *   DP link clk 1620 MHz and non-constant_n.
		 * TODO: calculate DP link symbol clk and stream clk m/n.
		 */
		vgpu_vreg_t(vgpu, PIPE_DATA_M1(dev_priv, TRANSCODER_A)) = TU_SIZE(64);
		vgpu_vreg_t(vgpu, PIPE_DATA_M1(dev_priv, TRANSCODER_A)) |= 0x5b425e;
		vgpu_vreg_t(vgpu, PIPE_DATA_N1(dev_priv, TRANSCODER_A)) = 0x800000;
		vgpu_vreg_t(vgpu, PIPE_LINK_M1(dev_priv, TRANSCODER_A)) = 0x3cd6e;
		vgpu_vreg_t(vgpu, PIPE_LINK_N1(dev_priv, TRANSCODER_A)) = 0x80000;

		/* Enable per-DDI/PORT vreg */
		if (intel_vgpu_has_monitor_on_port(vgpu, PORT_A)) {
			vgpu_vreg_t(vgpu, BXT_P_CR_GT_DISP_PWRON) |= BIT(1);
			vgpu_vreg_t(vgpu, BXT_PORT_CL1CM_DW0(DPIO_PHY1)) |=
				PHY_POWER_GOOD;
			vgpu_vreg_t(vgpu, BXT_PHY_CTL_FAMILY(DPIO_PHY1)) |=
				BIT(30);
			vgpu_vreg_t(vgpu, BXT_PHY_CTL(PORT_A)) |=
				BXT_PHY_LANE_ENABLED;
			vgpu_vreg_t(vgpu, BXT_PHY_CTL(PORT_A)) &=
				~(BXT_PHY_CMNLANE_POWERDOWN_ACK |
				  BXT_PHY_LANE_POWERDOWN_ACK);
			vgpu_vreg_t(vgpu, BXT_PORT_PLL_ENABLE(PORT_A)) |=
				(PORT_PLL_POWER_STATE | PORT_PLL_POWER_ENABLE |
				 PORT_PLL_REF_SEL | PORT_PLL_LOCK |
				 PORT_PLL_ENABLE);
			vgpu_vreg_t(vgpu, DDI_BUF_CTL(PORT_A)) |=
				(DDI_BUF_CTL_ENABLE | DDI_INIT_DISPLAY_DETECTED);
			vgpu_vreg_t(vgpu, DDI_BUF_CTL(PORT_A)) &=
				~DDI_BUF_IS_IDLE;
			vgpu_vreg_t(vgpu,
				    TRANS_DDI_FUNC_CTL(dev_priv, TRANSCODER_EDP)) |=
				(TRANS_DDI_BPC_8 | TRANS_DDI_MODE_SELECT_DP_SST |
				 TRANS_DDI_FUNC_ENABLE);
			vgpu_vreg_t(vgpu, PCH_PORT_HOTPLUG) |=
				PORTA_HOTPLUG_ENABLE;
			vgpu_vreg_t(vgpu, GEN8_DE_PORT_ISR) |=
				GEN8_DE_PORT_HOTPLUG(HPD_PORT_A);
		}

		if (intel_vgpu_has_monitor_on_port(vgpu, PORT_B)) {
			vgpu_vreg_t(vgpu, SFUSE_STRAP) |= SFUSE_STRAP_DDIB_DETECTED;
			vgpu_vreg_t(vgpu, BXT_P_CR_GT_DISP_PWRON) |= BIT(0);
			vgpu_vreg_t(vgpu, BXT_PORT_CL1CM_DW0(DPIO_PHY0)) |=
				PHY_POWER_GOOD;
			vgpu_vreg_t(vgpu, BXT_PHY_CTL_FAMILY(DPIO_PHY0)) |=
				BIT(30);
			vgpu_vreg_t(vgpu, BXT_PHY_CTL(PORT_B)) |=
				BXT_PHY_LANE_ENABLED;
			vgpu_vreg_t(vgpu, BXT_PHY_CTL(PORT_B)) &=
				~(BXT_PHY_CMNLANE_POWERDOWN_ACK |
				  BXT_PHY_LANE_POWERDOWN_ACK);
			vgpu_vreg_t(vgpu, BXT_PORT_PLL_ENABLE(PORT_B)) |=
				(PORT_PLL_POWER_STATE | PORT_PLL_POWER_ENABLE |
				 PORT_PLL_REF_SEL | PORT_PLL_LOCK |
				 PORT_PLL_ENABLE);
			vgpu_vreg_t(vgpu, DDI_BUF_CTL(PORT_B)) |=
				DDI_BUF_CTL_ENABLE;
			vgpu_vreg_t(vgpu, DDI_BUF_CTL(PORT_B)) &=
				~DDI_BUF_IS_IDLE;
			vgpu_vreg_t(vgpu,
				    TRANS_DDI_FUNC_CTL(dev_priv, TRANSCODER_A)) |=
				(TRANS_DDI_BPC_8 | TRANS_DDI_MODE_SELECT_DP_SST |
				 (PORT_B << TRANS_DDI_PORT_SHIFT) |
				 TRANS_DDI_FUNC_ENABLE);
			vgpu_vreg_t(vgpu, PCH_PORT_HOTPLUG) |=
				PORTB_HOTPLUG_ENABLE;
			vgpu_vreg_t(vgpu, GEN8_DE_PORT_ISR) |=
				GEN8_DE_PORT_HOTPLUG(HPD_PORT_B);
		}

		if (intel_vgpu_has_monitor_on_port(vgpu, PORT_C)) {
			vgpu_vreg_t(vgpu, SFUSE_STRAP) |= SFUSE_STRAP_DDIC_DETECTED;
			vgpu_vreg_t(vgpu, BXT_P_CR_GT_DISP_PWRON) |= BIT(0);
			vgpu_vreg_t(vgpu, BXT_PORT_CL1CM_DW0(DPIO_PHY0)) |=
				PHY_POWER_GOOD;
			vgpu_vreg_t(vgpu, BXT_PHY_CTL_FAMILY(DPIO_PHY0)) |=
				BIT(30);
			vgpu_vreg_t(vgpu, BXT_PHY_CTL(PORT_C)) |=
				BXT_PHY_LANE_ENABLED;
			vgpu_vreg_t(vgpu, BXT_PHY_CTL(PORT_C)) &=
				~(BXT_PHY_CMNLANE_POWERDOWN_ACK |
				  BXT_PHY_LANE_POWERDOWN_ACK);
			vgpu_vreg_t(vgpu, BXT_PORT_PLL_ENABLE(PORT_C)) |=
				(PORT_PLL_POWER_STATE | PORT_PLL_POWER_ENABLE |
				 PORT_PLL_REF_SEL | PORT_PLL_LOCK |
				 PORT_PLL_ENABLE);
			vgpu_vreg_t(vgpu, DDI_BUF_CTL(PORT_C)) |=
				DDI_BUF_CTL_ENABLE;
			vgpu_vreg_t(vgpu, DDI_BUF_CTL(PORT_C)) &=
				~DDI_BUF_IS_IDLE;
			vgpu_vreg_t(vgpu,
				    TRANS_DDI_FUNC_CTL(dev_priv, TRANSCODER_A)) |=
				(TRANS_DDI_BPC_8 | TRANS_DDI_MODE_SELECT_DP_SST |
				 (PORT_B << TRANS_DDI_PORT_SHIFT) |
				 TRANS_DDI_FUNC_ENABLE);
			vgpu_vreg_t(vgpu, PCH_PORT_HOTPLUG) |=
				PORTC_HOTPLUG_ENABLE;
			vgpu_vreg_t(vgpu, GEN8_DE_PORT_ISR) |=
				GEN8_DE_PORT_HOTPLUG(HPD_PORT_C);
		}

		return;
	}

	vgpu_vreg_t(vgpu, SDEISR) &= ~(SDE_PORTB_HOTPLUG_CPT |
			SDE_PORTC_HOTPLUG_CPT |
			SDE_PORTD_HOTPLUG_CPT);

	if (IS_SKYLAKE(dev_priv) ||
	    IS_KABYLAKE(dev_priv) ||
	    IS_COFFEELAKE(dev_priv) ||
	    IS_COMETLAKE(dev_priv)) {
		vgpu_vreg_t(vgpu, SDEISR) &= ~(SDE_PORTA_HOTPLUG_SPT |
				SDE_PORTE_HOTPLUG_SPT);
		vgpu_vreg_t(vgpu, SKL_FUSE_STATUS) |=
				SKL_FUSE_DOWNLOAD_STATUS |
				SKL_FUSE_PG_DIST_STATUS(SKL_PG0) |
				SKL_FUSE_PG_DIST_STATUS(SKL_PG1) |
				SKL_FUSE_PG_DIST_STATUS(SKL_PG2);
		/*
		 * Only 1 PIPE enabled in current vGPU display and PIPE_A is
		 *  tied to TRANSCODER_A in HW, so it's safe to assume PIPE_A,
		 *   TRANSCODER_A can be enabled. PORT_x depends on the input of
		 *   setup_virtual_dp_monitor, we can bind DPLL0 to any PORT_x
		 *   so we fixed to DPLL0 here.
		 * Setup DPLL0: DP link clk 1620 MHz, non SSC, DP Mode
		 */
		vgpu_vreg_t(vgpu, DPLL_CTRL1) =
			DPLL_CTRL1_OVERRIDE(DPLL_ID_SKL_DPLL0);
		vgpu_vreg_t(vgpu, DPLL_CTRL1) |=
			DPLL_CTRL1_LINK_RATE(DPLL_CTRL1_LINK_RATE_1620, DPLL_ID_SKL_DPLL0);
		vgpu_vreg_t(vgpu, LCPLL1_CTL) =
			LCPLL_PLL_ENABLE | LCPLL_PLL_LOCK;
		vgpu_vreg_t(vgpu, DPLL_STATUS) = DPLL_LOCK(DPLL_ID_SKL_DPLL0);
		/*
		 * Golden M/N are calculated based on:
		 *   24 bpp, 4 lanes, 154000 pixel clk (from virtual EDID),
		 *   DP link clk 1620 MHz and non-constant_n.
		 * TODO: calculate DP link symbol clk and stream clk m/n.
		 */
		vgpu_vreg_t(vgpu, PIPE_DATA_M1(dev_priv, TRANSCODER_A)) = TU_SIZE(64);
		vgpu_vreg_t(vgpu, PIPE_DATA_M1(dev_priv, TRANSCODER_A)) |= 0x5b425e;
		vgpu_vreg_t(vgpu, PIPE_DATA_N1(dev_priv, TRANSCODER_A)) = 0x800000;
		vgpu_vreg_t(vgpu, PIPE_LINK_M1(dev_priv, TRANSCODER_A)) = 0x3cd6e;
		vgpu_vreg_t(vgpu, PIPE_LINK_N1(dev_priv, TRANSCODER_A)) = 0x80000;
	}

	if (intel_vgpu_has_monitor_on_port(vgpu, PORT_B)) {
		vgpu_vreg_t(vgpu, DPLL_CTRL2) &=
			~DPLL_CTRL2_DDI_CLK_OFF(PORT_B);
		vgpu_vreg_t(vgpu, DPLL_CTRL2) |=
			DPLL_CTRL2_DDI_CLK_SEL(DPLL_ID_SKL_DPLL0, PORT_B);
		vgpu_vreg_t(vgpu, DPLL_CTRL2) |=
			DPLL_CTRL2_DDI_SEL_OVERRIDE(PORT_B);
		vgpu_vreg_t(vgpu, SFUSE_STRAP) |= SFUSE_STRAP_DDIB_DETECTED;
		vgpu_vreg_t(vgpu, TRANS_DDI_FUNC_CTL(dev_priv, TRANSCODER_A)) &=
			~(TRANS_DDI_BPC_MASK | TRANS_DDI_MODE_SELECT_MASK |
			TRANS_DDI_PORT_MASK);
		vgpu_vreg_t(vgpu, TRANS_DDI_FUNC_CTL(dev_priv, TRANSCODER_A)) |=
			(TRANS_DDI_BPC_8 | TRANS_DDI_MODE_SELECT_DP_SST |
			(PORT_B << TRANS_DDI_PORT_SHIFT) |
			TRANS_DDI_FUNC_ENABLE);
		if (IS_BROADWELL(dev_priv)) {
			vgpu_vreg_t(vgpu, PORT_CLK_SEL(PORT_B)) &=
				~PORT_CLK_SEL_MASK;
			vgpu_vreg_t(vgpu, PORT_CLK_SEL(PORT_B)) |=
				PORT_CLK_SEL_LCPLL_810;
		}
		vgpu_vreg_t(vgpu, DDI_BUF_CTL(PORT_B)) |= DDI_BUF_CTL_ENABLE;
		vgpu_vreg_t(vgpu, DDI_BUF_CTL(PORT_B)) &= ~DDI_BUF_IS_IDLE;
		vgpu_vreg_t(vgpu, SDEISR) |= SDE_PORTB_HOTPLUG_CPT;
	}

	if (intel_vgpu_has_monitor_on_port(vgpu, PORT_C)) {
		vgpu_vreg_t(vgpu, DPLL_CTRL2) &=
			~DPLL_CTRL2_DDI_CLK_OFF(PORT_C);
		vgpu_vreg_t(vgpu, DPLL_CTRL2) |=
			DPLL_CTRL2_DDI_CLK_SEL(DPLL_ID_SKL_DPLL0, PORT_C);
		vgpu_vreg_t(vgpu, DPLL_CTRL2) |=
			DPLL_CTRL2_DDI_SEL_OVERRIDE(PORT_C);
		vgpu_vreg_t(vgpu, SDEISR) |= SDE_PORTC_HOTPLUG_CPT;
		vgpu_vreg_t(vgpu, TRANS_DDI_FUNC_CTL(dev_priv, TRANSCODER_A)) &=
			~(TRANS_DDI_BPC_MASK | TRANS_DDI_MODE_SELECT_MASK |
			TRANS_DDI_PORT_MASK);
		vgpu_vreg_t(vgpu, TRANS_DDI_FUNC_CTL(dev_priv, TRANSCODER_A)) |=
			(TRANS_DDI_BPC_8 | TRANS_DDI_MODE_SELECT_DP_SST |
			(PORT_C << TRANS_DDI_PORT_SHIFT) |
			TRANS_DDI_FUNC_ENABLE);
		if (IS_BROADWELL(dev_priv)) {
			vgpu_vreg_t(vgpu, PORT_CLK_SEL(PORT_C)) &=
				~PORT_CLK_SEL_MASK;
			vgpu_vreg_t(vgpu, PORT_CLK_SEL(PORT_C)) |=
				PORT_CLK_SEL_LCPLL_810;
		}
		vgpu_vreg_t(vgpu, DDI_BUF_CTL(PORT_C)) |= DDI_BUF_CTL_ENABLE;
		vgpu_vreg_t(vgpu, DDI_BUF_CTL(PORT_C)) &= ~DDI_BUF_IS_IDLE;
		vgpu_vreg_t(vgpu, SFUSE_STRAP) |= SFUSE_STRAP_DDIC_DETECTED;
	}

	if (intel_vgpu_has_monitor_on_port(vgpu, PORT_D)) {
		vgpu_vreg_t(vgpu, DPLL_CTRL2) &=
			~DPLL_CTRL2_DDI_CLK_OFF(PORT_D);
		vgpu_vreg_t(vgpu, DPLL_CTRL2) |=
			DPLL_CTRL2_DDI_CLK_SEL(DPLL_ID_SKL_DPLL0, PORT_D);
		vgpu_vreg_t(vgpu, DPLL_CTRL2) |=
			DPLL_CTRL2_DDI_SEL_OVERRIDE(PORT_D);
		vgpu_vreg_t(vgpu, SDEISR) |= SDE_PORTD_HOTPLUG_CPT;
		vgpu_vreg_t(vgpu, TRANS_DDI_FUNC_CTL(dev_priv, TRANSCODER_A)) &=
			~(TRANS_DDI_BPC_MASK | TRANS_DDI_MODE_SELECT_MASK |
			TRANS_DDI_PORT_MASK);
		vgpu_vreg_t(vgpu, TRANS_DDI_FUNC_CTL(dev_priv, TRANSCODER_A)) |=
			(TRANS_DDI_BPC_8 | TRANS_DDI_MODE_SELECT_DP_SST |
			(PORT_D << TRANS_DDI_PORT_SHIFT) |
			TRANS_DDI_FUNC_ENABLE);
		if (IS_BROADWELL(dev_priv)) {
			vgpu_vreg_t(vgpu, PORT_CLK_SEL(PORT_D)) &=
				~PORT_CLK_SEL_MASK;
			vgpu_vreg_t(vgpu, PORT_CLK_SEL(PORT_D)) |=
				PORT_CLK_SEL_LCPLL_810;
		}
		vgpu_vreg_t(vgpu, DDI_BUF_CTL(PORT_D)) |= DDI_BUF_CTL_ENABLE;
		vgpu_vreg_t(vgpu, DDI_BUF_CTL(PORT_D)) &= ~DDI_BUF_IS_IDLE;
		vgpu_vreg_t(vgpu, SFUSE_STRAP) |= SFUSE_STRAP_DDID_DETECTED;
	}

	if ((IS_SKYLAKE(dev_priv) ||
	     IS_KABYLAKE(dev_priv) ||
	     IS_COFFEELAKE(dev_priv) ||
	     IS_COMETLAKE(dev_priv)) &&
			intel_vgpu_has_monitor_on_port(vgpu, PORT_E)) {
		vgpu_vreg_t(vgpu, SDEISR) |= SDE_PORTE_HOTPLUG_SPT;
	}

	if (intel_vgpu_has_monitor_on_port(vgpu, PORT_A)) {
		if (IS_BROADWELL(dev_priv))
			vgpu_vreg_t(vgpu, GEN8_DE_PORT_ISR) |=
				GEN8_DE_PORT_HOTPLUG(HPD_PORT_A);
		else
			vgpu_vreg_t(vgpu, SDEISR) |= SDE_PORTA_HOTPLUG_SPT;

		vgpu_vreg_t(vgpu, DDI_BUF_CTL(PORT_A)) |= DDI_INIT_DISPLAY_DETECTED;
	}

	/* Clear host CRT status, so guest couldn't detect this host CRT. */
	if (IS_BROADWELL(dev_priv))
		vgpu_vreg_t(vgpu, PCH_ADPA) &= ~ADPA_CRT_HOTPLUG_MONITOR_MASK;

	/* Disable Primary/Sprite/Cursor plane */
	for_each_pipe(dev_priv, pipe) {
		vgpu_vreg_t(vgpu, DSPCNTR(dev_priv, pipe)) &= ~DISP_ENABLE;
		vgpu_vreg_t(vgpu, SPRCTL(pipe)) &= ~SPRITE_ENABLE;
		vgpu_vreg_t(vgpu, CURCNTR(dev_priv, pipe)) &= ~MCURSOR_MODE_MASK;
		vgpu_vreg_t(vgpu, CURCNTR(dev_priv, pipe)) |= MCURSOR_MODE_DISABLE;
	}

	vgpu_vreg_t(vgpu, TRANSCONF(dev_priv, TRANSCODER_A)) |= TRANSCONF_ENABLE;
}

static void clean_virtual_dp_monitor(struct intel_vgpu *vgpu, int port_num)
{
	struct intel_vgpu_port *port = intel_vgpu_port(vgpu, port_num);

	kfree(port->edid);
	port->edid = NULL;

	kfree(port->dpcd);
	port->dpcd = NULL;
}

static enum hrtimer_restart vblank_timer_fn(struct hrtimer *data)
{
	struct intel_vgpu_vblank_timer *vblank_timer;
	struct intel_vgpu *vgpu;

	vblank_timer = container_of(data, struct intel_vgpu_vblank_timer, timer);
	vgpu = container_of(vblank_timer, struct intel_vgpu, vblank_timer);

	/* Set vblank emulation request per-vGPU bit */
	intel_gvt_request_service(vgpu->gvt,
				  INTEL_GVT_REQUEST_EMULATE_VBLANK + vgpu->id);
	hrtimer_add_expires_ns(&vblank_timer->timer, vblank_timer->period);
	return HRTIMER_RESTART;
}

static int setup_virtual_dp_monitor(struct intel_vgpu *vgpu, int port_num,
				    int type, unsigned int resolution)
{
	struct drm_i915_private *i915 = vgpu->gvt->gt->i915;
	struct intel_vgpu_port *port = intel_vgpu_port(vgpu, port_num);
	struct intel_vgpu_vblank_timer *vblank_timer = &vgpu->vblank_timer;

	if (drm_WARN_ON(&i915->drm, resolution >= GVT_EDID_NUM))
		return -EINVAL;

	port->edid = kzalloc(sizeof(*(port->edid)), GFP_KERNEL);
	if (!port->edid)
		return -ENOMEM;

	port->dpcd = kzalloc(sizeof(*(port->dpcd)), GFP_KERNEL);
	if (!port->dpcd) {
		kfree(port->edid);
		return -ENOMEM;
	}

	memcpy(port->edid->edid_block, virtual_dp_monitor_edid[resolution],
			EDID_SIZE);
	port->edid->data_valid = true;

	memcpy(port->dpcd->data, dpcd_fix_data, DPCD_HEADER_SIZE);
	port->dpcd->data_valid = true;
	port->dpcd->data[DP_SINK_COUNT] = 0x1;
	port->type = type;
	port->id = resolution;
	port->vrefresh_k = GVT_DEFAULT_REFRESH_RATE * MSEC_PER_SEC;
	vgpu->display.port_num = port_num;

	/* Init hrtimer based on default refresh rate */
	hrtimer_init(&vblank_timer->timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	vblank_timer->timer.function = vblank_timer_fn;
	vblank_timer->vrefresh_k = port->vrefresh_k;
	vblank_timer->period = DIV64_U64_ROUND_CLOSEST(NSEC_PER_SEC * MSEC_PER_SEC, vblank_timer->vrefresh_k);

	emulate_monitor_status_change(vgpu);

	return 0;
}

/**
 * vgpu_update_vblank_emulation - Update per-vGPU vblank_timer
 * @vgpu: vGPU operated
 * @turnon: Turn ON/OFF vblank_timer
 *
 * This function is used to turn on/off or update the per-vGPU vblank_timer
 * when TRANSCONF is enabled or disabled. vblank_timer period is also updated
 * if guest changed the refresh rate.
 *
 */
void vgpu_update_vblank_emulation(struct intel_vgpu *vgpu, bool turnon)
{
	struct intel_vgpu_vblank_timer *vblank_timer = &vgpu->vblank_timer;
	struct intel_vgpu_port *port =
		intel_vgpu_port(vgpu, vgpu->display.port_num);

	if (turnon) {
		/*
		 * Skip the re-enable if already active and vrefresh unchanged.
		 * Otherwise, stop timer if already active and restart with new
		 *   period.
		 */
		if (vblank_timer->vrefresh_k != port->vrefresh_k ||
		    !hrtimer_active(&vblank_timer->timer)) {
			/* Stop timer before start with new period if active */
			if (hrtimer_active(&vblank_timer->timer))
				hrtimer_cancel(&vblank_timer->timer);

			/* Make sure new refresh rate updated to timer period */
			vblank_timer->vrefresh_k = port->vrefresh_k;
			vblank_timer->period = DIV64_U64_ROUND_CLOSEST(NSEC_PER_SEC * MSEC_PER_SEC, vblank_timer->vrefresh_k);
			hrtimer_start(&vblank_timer->timer,
				      ktime_add_ns(ktime_get(), vblank_timer->period),
				      HRTIMER_MODE_ABS);
		}
	} else {
		/* Caller request to stop vblank */
		hrtimer_cancel(&vblank_timer->timer);
	}
}

static void emulate_vblank_on_pipe(struct intel_vgpu *vgpu, int pipe)
{
	struct drm_i915_private *dev_priv = vgpu->gvt->gt->i915;
	struct intel_vgpu_irq *irq = &vgpu->irq;
	int vblank_event[] = {
		[PIPE_A] = PIPE_A_VBLANK,
		[PIPE_B] = PIPE_B_VBLANK,
		[PIPE_C] = PIPE_C_VBLANK,
	};
	int event;

	if (pipe < PIPE_A || pipe > PIPE_C)
		return;

	for_each_set_bit(event, irq->flip_done_event[pipe],
			INTEL_GVT_EVENT_MAX) {
		clear_bit(event, irq->flip_done_event[pipe]);
		if (!pipe_is_enabled(vgpu, pipe))
			continue;

		intel_vgpu_trigger_virtual_event(vgpu, event);
	}

	if (pipe_is_enabled(vgpu, pipe)) {
		vgpu_vreg_t(vgpu, PIPE_FRMCOUNT_G4X(dev_priv, pipe))++;
		intel_vgpu_trigger_virtual_event(vgpu, vblank_event[pipe]);
	}
}

void intel_vgpu_emulate_vblank(struct intel_vgpu *vgpu)
{
	int pipe;

	mutex_lock(&vgpu->vgpu_lock);
	for_each_pipe(vgpu->gvt->gt->i915, pipe)
		emulate_vblank_on_pipe(vgpu, pipe);
	mutex_unlock(&vgpu->vgpu_lock);
}

/**
 * intel_vgpu_emulate_hotplug - trigger hotplug event for vGPU
 * @vgpu: a vGPU
 * @connected: link state
 *
 * This function is used to trigger hotplug interrupt for vGPU
 *
 */
void intel_vgpu_emulate_hotplug(struct intel_vgpu *vgpu, bool connected)
{
	struct drm_i915_private *i915 = vgpu->gvt->gt->i915;

	/* TODO: add more platforms support */
	if (IS_SKYLAKE(i915) ||
	    IS_KABYLAKE(i915) ||
	    IS_COFFEELAKE(i915) ||
	    IS_COMETLAKE(i915)) {
		if (connected) {
			vgpu_vreg_t(vgpu, SFUSE_STRAP) |=
				SFUSE_STRAP_DDID_DETECTED;
			vgpu_vreg_t(vgpu, SDEISR) |= SDE_PORTD_HOTPLUG_CPT;
		} else {
			vgpu_vreg_t(vgpu, SFUSE_STRAP) &=
				~SFUSE_STRAP_DDID_DETECTED;
			vgpu_vreg_t(vgpu, SDEISR) &= ~SDE_PORTD_HOTPLUG_CPT;
		}
		vgpu_vreg_t(vgpu, SDEIIR) |= SDE_PORTD_HOTPLUG_CPT;
		vgpu_vreg_t(vgpu, PCH_PORT_HOTPLUG) |=
				PORTD_HOTPLUG_STATUS_MASK;
		intel_vgpu_trigger_virtual_event(vgpu, DP_D_HOTPLUG);
	} else if (IS_BROXTON(i915)) {
		if (intel_vgpu_has_monitor_on_port(vgpu, PORT_A)) {
			if (connected) {
				vgpu_vreg_t(vgpu, GEN8_DE_PORT_ISR) |=
					GEN8_DE_PORT_HOTPLUG(HPD_PORT_A);
			} else {
				vgpu_vreg_t(vgpu, GEN8_DE_PORT_ISR) &=
					~GEN8_DE_PORT_HOTPLUG(HPD_PORT_A);
			}
			vgpu_vreg_t(vgpu, GEN8_DE_PORT_IIR) |=
				GEN8_DE_PORT_HOTPLUG(HPD_PORT_A);
			vgpu_vreg_t(vgpu, PCH_PORT_HOTPLUG) &=
				~PORTA_HOTPLUG_STATUS_MASK;
			vgpu_vreg_t(vgpu, PCH_PORT_HOTPLUG) |=
				PORTA_HOTPLUG_LONG_DETECT;
			intel_vgpu_trigger_virtual_event(vgpu, DP_A_HOTPLUG);
		}
		if (intel_vgpu_has_monitor_on_port(vgpu, PORT_B)) {
			if (connected) {
				vgpu_vreg_t(vgpu, GEN8_DE_PORT_ISR) |=
					GEN8_DE_PORT_HOTPLUG(HPD_PORT_B);
				vgpu_vreg_t(vgpu, SFUSE_STRAP) |=
					SFUSE_STRAP_DDIB_DETECTED;
			} else {
				vgpu_vreg_t(vgpu, GEN8_DE_PORT_ISR) &=
					~GEN8_DE_PORT_HOTPLUG(HPD_PORT_B);
				vgpu_vreg_t(vgpu, SFUSE_STRAP) &=
					~SFUSE_STRAP_DDIB_DETECTED;
			}
			vgpu_vreg_t(vgpu, GEN8_DE_PORT_IIR) |=
				GEN8_DE_PORT_HOTPLUG(HPD_PORT_B);
			vgpu_vreg_t(vgpu, PCH_PORT_HOTPLUG) &=
				~PORTB_HOTPLUG_STATUS_MASK;
			vgpu_vreg_t(vgpu, PCH_PORT_HOTPLUG) |=
				PORTB_HOTPLUG_LONG_DETECT;
			intel_vgpu_trigger_virtual_event(vgpu, DP_B_HOTPLUG);
		}
		if (intel_vgpu_has_monitor_on_port(vgpu, PORT_C)) {
			if (connected) {
				vgpu_vreg_t(vgpu, GEN8_DE_PORT_ISR) |=
					GEN8_DE_PORT_HOTPLUG(HPD_PORT_C);
				vgpu_vreg_t(vgpu, SFUSE_STRAP) |=
					SFUSE_STRAP_DDIC_DETECTED;
			} else {
				vgpu_vreg_t(vgpu, GEN8_DE_PORT_ISR) &=
					~GEN8_DE_PORT_HOTPLUG(HPD_PORT_C);
				vgpu_vreg_t(vgpu, SFUSE_STRAP) &=
					~SFUSE_STRAP_DDIC_DETECTED;
			}
			vgpu_vreg_t(vgpu, GEN8_DE_PORT_IIR) |=
				GEN8_DE_PORT_HOTPLUG(HPD_PORT_C);
			vgpu_vreg_t(vgpu, PCH_PORT_HOTPLUG) &=
				~PORTC_HOTPLUG_STATUS_MASK;
			vgpu_vreg_t(vgpu, PCH_PORT_HOTPLUG) |=
				PORTC_HOTPLUG_LONG_DETECT;
			intel_vgpu_trigger_virtual_event(vgpu, DP_C_HOTPLUG);
		}
	}
}

/**
 * intel_vgpu_clean_display - clean vGPU virtual display emulation
 * @vgpu: a vGPU
 *
 * This function is used to clean vGPU virtual display emulation stuffs
 *
 */
void intel_vgpu_clean_display(struct intel_vgpu *vgpu)
{
	struct drm_i915_private *dev_priv = vgpu->gvt->gt->i915;

	if (IS_SKYLAKE(dev_priv) ||
	    IS_KABYLAKE(dev_priv) ||
	    IS_COFFEELAKE(dev_priv) ||
	    IS_COMETLAKE(dev_priv))
		clean_virtual_dp_monitor(vgpu, PORT_D);
	else
		clean_virtual_dp_monitor(vgpu, PORT_B);

	vgpu_update_vblank_emulation(vgpu, false);
}

/**
 * intel_vgpu_init_display- initialize vGPU virtual display emulation
 * @vgpu: a vGPU
 * @resolution: resolution index for intel_vgpu_edid
 *
 * This function is used to initialize vGPU virtual display emulation stuffs
 *
 * Returns:
 * Zero on success, negative error code if failed.
 *
 */
int intel_vgpu_init_display(struct intel_vgpu *vgpu, u64 resolution)
{
	struct drm_i915_private *dev_priv = vgpu->gvt->gt->i915;

	intel_vgpu_init_i2c_edid(vgpu);

	if (IS_SKYLAKE(dev_priv) ||
	    IS_KABYLAKE(dev_priv) ||
	    IS_COFFEELAKE(dev_priv) ||
	    IS_COMETLAKE(dev_priv))
		return setup_virtual_dp_monitor(vgpu, PORT_D, GVT_DP_D,
						resolution);
	else
		return setup_virtual_dp_monitor(vgpu, PORT_B, GVT_DP_B,
						resolution);
}

/**
 * intel_vgpu_reset_display- reset vGPU virtual display emulation
 * @vgpu: a vGPU
 *
 * This function is used to reset vGPU virtual display emulation stuffs
 *
 */
void intel_vgpu_reset_display(struct intel_vgpu *vgpu)
{
	emulate_monitor_status_change(vgpu);
}
