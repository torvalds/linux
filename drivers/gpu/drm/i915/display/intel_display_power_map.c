// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "i915_drv.h"
#include "i915_reg.h"

#include "vlv_sideband_reg.h"

#include "intel_display_power_map.h"
#include "intel_display_power_well.h"

#define __LIST_INLINE_ELEMS(__elem_type, ...) \
	((__elem_type[]) { __VA_ARGS__ })

#define __LIST(__elems) { \
	.list = __elems, \
	.count = ARRAY_SIZE(__elems), \
}

#define I915_PW_DOMAINS(...) \
	(const struct i915_power_domain_list) \
		__LIST(__LIST_INLINE_ELEMS(enum intel_display_power_domain, __VA_ARGS__))

#define I915_DECL_PW_DOMAINS(__name, ...) \
	static const struct i915_power_domain_list __name = I915_PW_DOMAINS(__VA_ARGS__)

/* Zero-length list assigns all power domains, a NULL list assigns none. */
#define I915_PW_DOMAINS_NONE	NULL
#define I915_PW_DOMAINS_ALL	/* zero-length list */


I915_DECL_PW_DOMAINS(i9xx_pwdoms_always_on, I915_PW_DOMAINS_ALL);

static const struct i915_power_well_desc i9xx_always_on_power_well[] = {
	{
		.name = "always-on",
		.domain_list = &i9xx_pwdoms_always_on,
		.ops = &i9xx_always_on_power_well_ops,
		.always_on = true,
		.id = DISP_PW_ID_NONE,
	},
};

I915_DECL_PW_DOMAINS(i830_pwdoms_pipes,
	POWER_DOMAIN_PIPE_A,
	POWER_DOMAIN_PIPE_B,
	POWER_DOMAIN_PIPE_PANEL_FITTER_A,
	POWER_DOMAIN_PIPE_PANEL_FITTER_B,
	POWER_DOMAIN_TRANSCODER_A,
	POWER_DOMAIN_TRANSCODER_B,
	POWER_DOMAIN_INIT);

static const struct i915_power_well_desc i830_power_wells[] = {
	{
		.name = "always-on",
		.domain_list = &i9xx_pwdoms_always_on,
		.ops = &i9xx_always_on_power_well_ops,
		.always_on = true,
		.id = DISP_PW_ID_NONE,
	}, {
		.name = "pipes",
		.domain_list = &i830_pwdoms_pipes,
		.ops = &i830_pipes_power_well_ops,
		.id = DISP_PW_ID_NONE,
	},
};

I915_DECL_PW_DOMAINS(hsw_pwdoms_display,
	POWER_DOMAIN_PIPE_B,
	POWER_DOMAIN_PIPE_C,
	POWER_DOMAIN_PIPE_PANEL_FITTER_A,
	POWER_DOMAIN_PIPE_PANEL_FITTER_B,
	POWER_DOMAIN_PIPE_PANEL_FITTER_C,
	POWER_DOMAIN_TRANSCODER_A,
	POWER_DOMAIN_TRANSCODER_B,
	POWER_DOMAIN_TRANSCODER_C,
	POWER_DOMAIN_PORT_DDI_LANES_B,
	POWER_DOMAIN_PORT_DDI_LANES_C,
	POWER_DOMAIN_PORT_DDI_LANES_D,
	POWER_DOMAIN_PORT_CRT, /* DDI E */
	POWER_DOMAIN_VGA,
	POWER_DOMAIN_AUDIO_MMIO,
	POWER_DOMAIN_AUDIO_PLAYBACK,
	POWER_DOMAIN_INIT);

static const struct i915_power_well_desc hsw_power_wells[] = {
	{
		.name = "always-on",
		.domain_list = &i9xx_pwdoms_always_on,
		.ops = &i9xx_always_on_power_well_ops,
		.always_on = true,
		.id = DISP_PW_ID_NONE,
	}, {
		.name = "display",
		.domain_list = &hsw_pwdoms_display,
		.ops = &hsw_power_well_ops,
		.has_vga = true,
		.id = HSW_DISP_PW_GLOBAL,
		{
			.hsw.idx = HSW_PW_CTL_IDX_GLOBAL,
		},
	},
};

I915_DECL_PW_DOMAINS(bdw_pwdoms_display,
	POWER_DOMAIN_PIPE_B,
	POWER_DOMAIN_PIPE_C,
	POWER_DOMAIN_PIPE_PANEL_FITTER_B,
	POWER_DOMAIN_PIPE_PANEL_FITTER_C,
	POWER_DOMAIN_TRANSCODER_A,
	POWER_DOMAIN_TRANSCODER_B,
	POWER_DOMAIN_TRANSCODER_C,
	POWER_DOMAIN_PORT_DDI_LANES_B,
	POWER_DOMAIN_PORT_DDI_LANES_C,
	POWER_DOMAIN_PORT_DDI_LANES_D,
	POWER_DOMAIN_PORT_CRT, /* DDI E */
	POWER_DOMAIN_VGA,
	POWER_DOMAIN_AUDIO_MMIO,
	POWER_DOMAIN_AUDIO_PLAYBACK,
	POWER_DOMAIN_INIT);

static const struct i915_power_well_desc bdw_power_wells[] = {
	{
		.name = "always-on",
		.domain_list = &i9xx_pwdoms_always_on,
		.ops = &i9xx_always_on_power_well_ops,
		.always_on = true,
		.id = DISP_PW_ID_NONE,
	}, {
		.name = "display",
		.domain_list = &bdw_pwdoms_display,
		.ops = &hsw_power_well_ops,
		.has_vga = true,
		.irq_pipe_mask = BIT(PIPE_B) | BIT(PIPE_C),
		.id = HSW_DISP_PW_GLOBAL,
		{
			.hsw.idx = HSW_PW_CTL_IDX_GLOBAL,
		},
	},
};

I915_DECL_PW_DOMAINS(vlv_pwdoms_display,
	POWER_DOMAIN_DISPLAY_CORE,
	POWER_DOMAIN_PIPE_A,
	POWER_DOMAIN_PIPE_B,
	POWER_DOMAIN_PIPE_PANEL_FITTER_A,
	POWER_DOMAIN_PIPE_PANEL_FITTER_B,
	POWER_DOMAIN_TRANSCODER_A,
	POWER_DOMAIN_TRANSCODER_B,
	POWER_DOMAIN_PORT_DDI_LANES_B,
	POWER_DOMAIN_PORT_DDI_LANES_C,
	POWER_DOMAIN_PORT_DSI,
	POWER_DOMAIN_PORT_CRT,
	POWER_DOMAIN_VGA,
	POWER_DOMAIN_AUDIO_MMIO,
	POWER_DOMAIN_AUDIO_PLAYBACK,
	POWER_DOMAIN_AUX_B,
	POWER_DOMAIN_AUX_C,
	POWER_DOMAIN_GMBUS,
	POWER_DOMAIN_INIT);

I915_DECL_PW_DOMAINS(vlv_pwdoms_dpio_cmn_bc,
	POWER_DOMAIN_PORT_DDI_LANES_B,
	POWER_DOMAIN_PORT_DDI_LANES_C,
	POWER_DOMAIN_PORT_CRT,
	POWER_DOMAIN_AUX_B,
	POWER_DOMAIN_AUX_C,
	POWER_DOMAIN_INIT);

I915_DECL_PW_DOMAINS(vlv_pwdoms_dpio_tx_bc_lanes,
	POWER_DOMAIN_PORT_DDI_LANES_B,
	POWER_DOMAIN_PORT_DDI_LANES_C,
	POWER_DOMAIN_AUX_B,
	POWER_DOMAIN_AUX_C,
	POWER_DOMAIN_INIT);

static const struct i915_power_well_desc vlv_power_wells[] = {
	{
		.name = "always-on",
		.domain_list = &i9xx_pwdoms_always_on,
		.ops = &i9xx_always_on_power_well_ops,
		.always_on = true,
		.id = DISP_PW_ID_NONE,
	}, {
		.name = "display",
		.domain_list = &vlv_pwdoms_display,
		.ops = &vlv_display_power_well_ops,
		.id = VLV_DISP_PW_DISP2D,
		{
			.vlv.idx = PUNIT_PWGT_IDX_DISP2D,
		},
	}, {
		.name = "dpio-tx-b-01",
		.domain_list = &vlv_pwdoms_dpio_tx_bc_lanes,
		.ops = &vlv_dpio_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.vlv.idx = PUNIT_PWGT_IDX_DPIO_TX_B_LANES_01,
		},
	}, {
		.name = "dpio-tx-b-23",
		.domain_list = &vlv_pwdoms_dpio_tx_bc_lanes,
		.ops = &vlv_dpio_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.vlv.idx = PUNIT_PWGT_IDX_DPIO_TX_B_LANES_23,
		},
	}, {
		.name = "dpio-tx-c-01",
		.domain_list = &vlv_pwdoms_dpio_tx_bc_lanes,
		.ops = &vlv_dpio_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.vlv.idx = PUNIT_PWGT_IDX_DPIO_TX_C_LANES_01,
		},
	}, {
		.name = "dpio-tx-c-23",
		.domain_list = &vlv_pwdoms_dpio_tx_bc_lanes,
		.ops = &vlv_dpio_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.vlv.idx = PUNIT_PWGT_IDX_DPIO_TX_C_LANES_23,
		},
	}, {
		.name = "dpio-common",
		.domain_list = &vlv_pwdoms_dpio_cmn_bc,
		.ops = &vlv_dpio_cmn_power_well_ops,
		.id = VLV_DISP_PW_DPIO_CMN_BC,
		{
			.vlv.idx = PUNIT_PWGT_IDX_DPIO_CMN_BC,
		},
	},
};

I915_DECL_PW_DOMAINS(chv_pwdoms_display,
	POWER_DOMAIN_DISPLAY_CORE,
	POWER_DOMAIN_PIPE_A,
	POWER_DOMAIN_PIPE_B,
	POWER_DOMAIN_PIPE_C,
	POWER_DOMAIN_PIPE_PANEL_FITTER_A,
	POWER_DOMAIN_PIPE_PANEL_FITTER_B,
	POWER_DOMAIN_PIPE_PANEL_FITTER_C,
	POWER_DOMAIN_TRANSCODER_A,
	POWER_DOMAIN_TRANSCODER_B,
	POWER_DOMAIN_TRANSCODER_C,
	POWER_DOMAIN_PORT_DDI_LANES_B,
	POWER_DOMAIN_PORT_DDI_LANES_C,
	POWER_DOMAIN_PORT_DDI_LANES_D,
	POWER_DOMAIN_PORT_DSI,
	POWER_DOMAIN_VGA,
	POWER_DOMAIN_AUDIO_MMIO,
	POWER_DOMAIN_AUDIO_PLAYBACK,
	POWER_DOMAIN_AUX_B,
	POWER_DOMAIN_AUX_C,
	POWER_DOMAIN_AUX_D,
	POWER_DOMAIN_GMBUS,
	POWER_DOMAIN_INIT);

I915_DECL_PW_DOMAINS(chv_pwdoms_dpio_cmn_bc,
	POWER_DOMAIN_PORT_DDI_LANES_B,
	POWER_DOMAIN_PORT_DDI_LANES_C,
	POWER_DOMAIN_AUX_B,
	POWER_DOMAIN_AUX_C,
	POWER_DOMAIN_INIT);

I915_DECL_PW_DOMAINS(chv_pwdoms_dpio_cmn_d,
	POWER_DOMAIN_PORT_DDI_LANES_D,
	POWER_DOMAIN_AUX_D,
	POWER_DOMAIN_INIT);

static const struct i915_power_well_desc chv_power_wells[] = {
	{
		.name = "always-on",
		.domain_list = &i9xx_pwdoms_always_on,
		.ops = &i9xx_always_on_power_well_ops,
		.always_on = true,
		.id = DISP_PW_ID_NONE,
	}, {
		.name = "display",
		/*
		 * Pipe A power well is the new disp2d well. Pipe B and C
		 * power wells don't actually exist. Pipe A power well is
		 * required for any pipe to work.
		 */
		.domain_list = &chv_pwdoms_display,
		.ops = &chv_pipe_power_well_ops,
		.id = DISP_PW_ID_NONE,
	}, {
		.name = "dpio-common-bc",
		.domain_list = &chv_pwdoms_dpio_cmn_bc,
		.ops = &chv_dpio_cmn_power_well_ops,
		.id = VLV_DISP_PW_DPIO_CMN_BC,
		{
			.vlv.idx = PUNIT_PWGT_IDX_DPIO_CMN_BC,
		},
	}, {
		.name = "dpio-common-d",
		.domain_list = &chv_pwdoms_dpio_cmn_d,
		.ops = &chv_dpio_cmn_power_well_ops,
		.id = CHV_DISP_PW_DPIO_CMN_D,
		{
			.vlv.idx = PUNIT_PWGT_IDX_DPIO_CMN_D,
		},
	},
};

#define SKL_PW_2_POWER_DOMAINS \
	POWER_DOMAIN_PIPE_B, \
	POWER_DOMAIN_PIPE_C, \
	POWER_DOMAIN_PIPE_PANEL_FITTER_B, \
	POWER_DOMAIN_PIPE_PANEL_FITTER_C, \
	POWER_DOMAIN_TRANSCODER_A, \
	POWER_DOMAIN_TRANSCODER_B, \
	POWER_DOMAIN_TRANSCODER_C, \
	POWER_DOMAIN_PORT_DDI_LANES_B, \
	POWER_DOMAIN_PORT_DDI_LANES_C, \
	POWER_DOMAIN_PORT_DDI_LANES_D, \
	POWER_DOMAIN_PORT_DDI_LANES_E, \
	POWER_DOMAIN_VGA, \
	POWER_DOMAIN_AUDIO_MMIO, \
	POWER_DOMAIN_AUDIO_PLAYBACK, \
	POWER_DOMAIN_AUX_B, \
	POWER_DOMAIN_AUX_C, \
	POWER_DOMAIN_AUX_D

I915_DECL_PW_DOMAINS(skl_pwdoms_pw_2,
	SKL_PW_2_POWER_DOMAINS,
	POWER_DOMAIN_INIT);

I915_DECL_PW_DOMAINS(skl_pwdoms_dc_off,
	SKL_PW_2_POWER_DOMAINS,
	POWER_DOMAIN_AUX_A,
	POWER_DOMAIN_MODESET,
	POWER_DOMAIN_GT_IRQ,
	POWER_DOMAIN_INIT);

I915_DECL_PW_DOMAINS(skl_pwdoms_ddi_io_a_e,
	POWER_DOMAIN_PORT_DDI_IO_A,
	POWER_DOMAIN_PORT_DDI_IO_E,
	POWER_DOMAIN_INIT);

I915_DECL_PW_DOMAINS(skl_pwdoms_ddi_io_b,
	POWER_DOMAIN_PORT_DDI_IO_B,
	POWER_DOMAIN_INIT);

I915_DECL_PW_DOMAINS(skl_pwdoms_ddi_io_c,
	POWER_DOMAIN_PORT_DDI_IO_C,
	POWER_DOMAIN_INIT);

I915_DECL_PW_DOMAINS(skl_pwdoms_ddi_io_d,
	POWER_DOMAIN_PORT_DDI_IO_D,
	POWER_DOMAIN_INIT);

static const struct i915_power_well_desc skl_power_wells[] = {
	{
		.name = "always-on",
		.domain_list = &i9xx_pwdoms_always_on,
		.ops = &i9xx_always_on_power_well_ops,
		.always_on = true,
		.id = DISP_PW_ID_NONE,
	}, {
		.name = "PW_1",
		/* Handled by the DMC firmware */
		.domain_list = I915_PW_DOMAINS_NONE,
		.ops = &hsw_power_well_ops,
		.always_on = true,
		.has_fuses = true,
		.id = SKL_DISP_PW_1,
		{
			.hsw.idx = SKL_PW_CTL_IDX_PW_1,
		},
	}, {
		.name = "MISC_IO",
		/* Handled by the DMC firmware */
		.domain_list = I915_PW_DOMAINS_NONE,
		.ops = &hsw_power_well_ops,
		.always_on = true,
		.id = SKL_DISP_PW_MISC_IO,
		{
			.hsw.idx = SKL_PW_CTL_IDX_MISC_IO,
		},
	}, {
		.name = "DC_off",
		.domain_list = &skl_pwdoms_dc_off,
		.ops = &gen9_dc_off_power_well_ops,
		.id = SKL_DISP_DC_OFF,
	}, {
		.name = "PW_2",
		.domain_list = &skl_pwdoms_pw_2,
		.ops = &hsw_power_well_ops,
		.has_vga = true,
		.irq_pipe_mask = BIT(PIPE_B) | BIT(PIPE_C),
		.has_fuses = true,
		.id = SKL_DISP_PW_2,
		{
			.hsw.idx = SKL_PW_CTL_IDX_PW_2,
		},
	}, {
		.name = "DDI_IO_A_E",
		.domain_list = &skl_pwdoms_ddi_io_a_e,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = SKL_PW_CTL_IDX_DDI_A_E,
		},
	}, {
		.name = "DDI_IO_B",
		.domain_list = &skl_pwdoms_ddi_io_b,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = SKL_PW_CTL_IDX_DDI_B,
		},
	}, {
		.name = "DDI_IO_C",
		.domain_list = &skl_pwdoms_ddi_io_c,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = SKL_PW_CTL_IDX_DDI_C,
		},
	}, {
		.name = "DDI_IO_D",
		.domain_list = &skl_pwdoms_ddi_io_d,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = SKL_PW_CTL_IDX_DDI_D,
		},
	},
};

#define BXT_PW_2_POWER_DOMAINS \
	POWER_DOMAIN_PIPE_B, \
	POWER_DOMAIN_PIPE_C, \
	POWER_DOMAIN_PIPE_PANEL_FITTER_B, \
	POWER_DOMAIN_PIPE_PANEL_FITTER_C, \
	POWER_DOMAIN_TRANSCODER_A, \
	POWER_DOMAIN_TRANSCODER_B, \
	POWER_DOMAIN_TRANSCODER_C, \
	POWER_DOMAIN_PORT_DDI_LANES_B, \
	POWER_DOMAIN_PORT_DDI_LANES_C, \
	POWER_DOMAIN_VGA, \
	POWER_DOMAIN_AUDIO_MMIO, \
	POWER_DOMAIN_AUDIO_PLAYBACK, \
	POWER_DOMAIN_AUX_B, \
	POWER_DOMAIN_AUX_C

I915_DECL_PW_DOMAINS(bxt_pwdoms_pw_2,
	BXT_PW_2_POWER_DOMAINS,
	POWER_DOMAIN_INIT);

I915_DECL_PW_DOMAINS(bxt_pwdoms_dc_off,
	BXT_PW_2_POWER_DOMAINS,
	POWER_DOMAIN_AUX_A,
	POWER_DOMAIN_GMBUS,
	POWER_DOMAIN_MODESET,
	POWER_DOMAIN_GT_IRQ,
	POWER_DOMAIN_INIT);

I915_DECL_PW_DOMAINS(bxt_pwdoms_dpio_cmn_a,
	POWER_DOMAIN_PORT_DDI_LANES_A,
	POWER_DOMAIN_AUX_A,
	POWER_DOMAIN_INIT);

I915_DECL_PW_DOMAINS(bxt_pwdoms_dpio_cmn_bc,
	POWER_DOMAIN_PORT_DDI_LANES_B,
	POWER_DOMAIN_PORT_DDI_LANES_C,
	POWER_DOMAIN_AUX_B,
	POWER_DOMAIN_AUX_C,
	POWER_DOMAIN_INIT);

static const struct i915_power_well_desc bxt_power_wells[] = {
	{
		.name = "always-on",
		.domain_list = &i9xx_pwdoms_always_on,
		.ops = &i9xx_always_on_power_well_ops,
		.always_on = true,
		.id = DISP_PW_ID_NONE,
	}, {
		.name = "PW_1",
		/* Handled by the DMC firmware */
		.domain_list = I915_PW_DOMAINS_NONE,
		.ops = &hsw_power_well_ops,
		.always_on = true,
		.has_fuses = true,
		.id = SKL_DISP_PW_1,
		{
			.hsw.idx = SKL_PW_CTL_IDX_PW_1,
		},
	}, {
		.name = "DC_off",
		.domain_list = &bxt_pwdoms_dc_off,
		.ops = &gen9_dc_off_power_well_ops,
		.id = SKL_DISP_DC_OFF,
	}, {
		.name = "PW_2",
		.domain_list = &bxt_pwdoms_pw_2,
		.ops = &hsw_power_well_ops,
		.has_vga = true,
		.irq_pipe_mask = BIT(PIPE_B) | BIT(PIPE_C),
		.has_fuses = true,
		.id = SKL_DISP_PW_2,
		{
			.hsw.idx = SKL_PW_CTL_IDX_PW_2,
		},
	}, {
		.name = "dpio-common-a",
		.domain_list = &bxt_pwdoms_dpio_cmn_a,
		.ops = &bxt_dpio_cmn_power_well_ops,
		.id = BXT_DISP_PW_DPIO_CMN_A,
		{
			.bxt.phy = DPIO_PHY1,
		},
	}, {
		.name = "dpio-common-bc",
		.domain_list = &bxt_pwdoms_dpio_cmn_bc,
		.ops = &bxt_dpio_cmn_power_well_ops,
		.id = VLV_DISP_PW_DPIO_CMN_BC,
		{
			.bxt.phy = DPIO_PHY0,
		},
	},
};

#define GLK_PW_2_POWER_DOMAINS \
	POWER_DOMAIN_PIPE_B, \
	POWER_DOMAIN_PIPE_C, \
	POWER_DOMAIN_PIPE_PANEL_FITTER_B, \
	POWER_DOMAIN_PIPE_PANEL_FITTER_C, \
	POWER_DOMAIN_TRANSCODER_A, \
	POWER_DOMAIN_TRANSCODER_B, \
	POWER_DOMAIN_TRANSCODER_C, \
	POWER_DOMAIN_PORT_DDI_LANES_B, \
	POWER_DOMAIN_PORT_DDI_LANES_C, \
	POWER_DOMAIN_VGA, \
	POWER_DOMAIN_AUDIO_MMIO, \
	POWER_DOMAIN_AUDIO_PLAYBACK, \
	POWER_DOMAIN_AUX_B, \
	POWER_DOMAIN_AUX_C

I915_DECL_PW_DOMAINS(glk_pwdoms_pw_2,
	GLK_PW_2_POWER_DOMAINS,
	POWER_DOMAIN_INIT);

I915_DECL_PW_DOMAINS(glk_pwdoms_dc_off,
	GLK_PW_2_POWER_DOMAINS,
	POWER_DOMAIN_AUX_A,
	POWER_DOMAIN_GMBUS,
	POWER_DOMAIN_MODESET,
	POWER_DOMAIN_GT_IRQ,
	POWER_DOMAIN_INIT);

I915_DECL_PW_DOMAINS(glk_pwdoms_ddi_io_a,	POWER_DOMAIN_PORT_DDI_IO_A);
I915_DECL_PW_DOMAINS(glk_pwdoms_ddi_io_b,	POWER_DOMAIN_PORT_DDI_IO_B);
I915_DECL_PW_DOMAINS(glk_pwdoms_ddi_io_c,	POWER_DOMAIN_PORT_DDI_IO_C);

I915_DECL_PW_DOMAINS(glk_pwdoms_dpio_cmn_a,
	POWER_DOMAIN_PORT_DDI_LANES_A,
	POWER_DOMAIN_AUX_A,
	POWER_DOMAIN_INIT);

I915_DECL_PW_DOMAINS(glk_pwdoms_dpio_cmn_b,
	POWER_DOMAIN_PORT_DDI_LANES_B,
	POWER_DOMAIN_AUX_B,
	POWER_DOMAIN_INIT);

I915_DECL_PW_DOMAINS(glk_pwdoms_dpio_cmn_c,
	POWER_DOMAIN_PORT_DDI_LANES_C,
	POWER_DOMAIN_AUX_C,
	POWER_DOMAIN_INIT);

I915_DECL_PW_DOMAINS(glk_pwdoms_aux_a,
	POWER_DOMAIN_AUX_A,
	POWER_DOMAIN_AUX_IO_A,
	POWER_DOMAIN_INIT);

I915_DECL_PW_DOMAINS(glk_pwdoms_aux_b,
	POWER_DOMAIN_AUX_B,
	POWER_DOMAIN_INIT);

I915_DECL_PW_DOMAINS(glk_pwdoms_aux_c,
	POWER_DOMAIN_AUX_C,
	POWER_DOMAIN_INIT);

static const struct i915_power_well_desc glk_power_wells[] = {
	{
		.name = "always-on",
		.domain_list = &i9xx_pwdoms_always_on,
		.ops = &i9xx_always_on_power_well_ops,
		.always_on = true,
		.id = DISP_PW_ID_NONE,
	}, {
		.name = "PW_1",
		/* Handled by the DMC firmware */
		.domain_list = I915_PW_DOMAINS_NONE,
		.ops = &hsw_power_well_ops,
		.always_on = true,
		.has_fuses = true,
		.id = SKL_DISP_PW_1,
		{
			.hsw.idx = SKL_PW_CTL_IDX_PW_1,
		},
	}, {
		.name = "DC_off",
		.domain_list = &glk_pwdoms_dc_off,
		.ops = &gen9_dc_off_power_well_ops,
		.id = SKL_DISP_DC_OFF,
	}, {
		.name = "PW_2",
		.domain_list = &glk_pwdoms_pw_2,
		.ops = &hsw_power_well_ops,
		.has_vga = true,
		.irq_pipe_mask = BIT(PIPE_B) | BIT(PIPE_C),
		.has_fuses = true,
		.id = SKL_DISP_PW_2,
		{
			.hsw.idx = SKL_PW_CTL_IDX_PW_2,
		},
	}, {
		.name = "dpio-common-a",
		.domain_list = &glk_pwdoms_dpio_cmn_a,
		.ops = &bxt_dpio_cmn_power_well_ops,
		.id = BXT_DISP_PW_DPIO_CMN_A,
		{
			.bxt.phy = DPIO_PHY1,
		},
	}, {
		.name = "dpio-common-b",
		.domain_list = &glk_pwdoms_dpio_cmn_b,
		.ops = &bxt_dpio_cmn_power_well_ops,
		.id = VLV_DISP_PW_DPIO_CMN_BC,
		{
			.bxt.phy = DPIO_PHY0,
		},
	}, {
		.name = "dpio-common-c",
		.domain_list = &glk_pwdoms_dpio_cmn_c,
		.ops = &bxt_dpio_cmn_power_well_ops,
		.id = GLK_DISP_PW_DPIO_CMN_C,
		{
			.bxt.phy = DPIO_PHY2,
		},
	}, {
		.name = "AUX_A",
		.domain_list = &glk_pwdoms_aux_a,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = GLK_PW_CTL_IDX_AUX_A,
		},
	}, {
		.name = "AUX_B",
		.domain_list = &glk_pwdoms_aux_b,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = GLK_PW_CTL_IDX_AUX_B,
		},
	}, {
		.name = "AUX_C",
		.domain_list = &glk_pwdoms_aux_c,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = GLK_PW_CTL_IDX_AUX_C,
		},
	}, {
		.name = "DDI_IO_A",
		.domain_list = &glk_pwdoms_ddi_io_a,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = GLK_PW_CTL_IDX_DDI_A,
		},
	}, {
		.name = "DDI_IO_B",
		.domain_list = &glk_pwdoms_ddi_io_b,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = SKL_PW_CTL_IDX_DDI_B,
		},
	}, {
		.name = "DDI_IO_C",
		.domain_list = &glk_pwdoms_ddi_io_c,
		.ops = &hsw_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = SKL_PW_CTL_IDX_DDI_C,
		},
	},
};

/*
 * ICL PW_0/PG_0 domains (HW/DMC control):
 * - PCI
 * - clocks except port PLL
 * - central power except FBC
 * - shared functions except pipe interrupts, pipe MBUS, DBUF registers
 * ICL PW_1/PG_1 domains (HW/DMC control):
 * - DBUF function
 * - PIPE_A and its planes, except VGA
 * - transcoder EDP + PSR
 * - transcoder DSI
 * - DDI_A
 * - FBC
 */
#define ICL_PW_4_POWER_DOMAINS \
	POWER_DOMAIN_PIPE_C, \
	POWER_DOMAIN_PIPE_PANEL_FITTER_C

I915_DECL_PW_DOMAINS(icl_pwdoms_pw_4,
	ICL_PW_4_POWER_DOMAINS,
	POWER_DOMAIN_INIT);
	/* VDSC/joining */

#define ICL_PW_3_POWER_DOMAINS \
	ICL_PW_4_POWER_DOMAINS, \
	POWER_DOMAIN_PIPE_B, \
	POWER_DOMAIN_PIPE_PANEL_FITTER_B, \
	POWER_DOMAIN_TRANSCODER_A, \
	POWER_DOMAIN_TRANSCODER_B, \
	POWER_DOMAIN_TRANSCODER_C, \
	POWER_DOMAIN_PORT_DDI_LANES_B, \
	POWER_DOMAIN_PORT_DDI_LANES_C, \
	POWER_DOMAIN_PORT_DDI_LANES_D, \
	POWER_DOMAIN_PORT_DDI_LANES_E, \
	POWER_DOMAIN_PORT_DDI_LANES_F, \
	POWER_DOMAIN_VGA, \
	POWER_DOMAIN_AUDIO_MMIO, \
	POWER_DOMAIN_AUDIO_PLAYBACK, \
	POWER_DOMAIN_AUX_B, \
	POWER_DOMAIN_AUX_C, \
	POWER_DOMAIN_AUX_D, \
	POWER_DOMAIN_AUX_E, \
	POWER_DOMAIN_AUX_F, \
	POWER_DOMAIN_AUX_TBT_C, \
	POWER_DOMAIN_AUX_TBT_D, \
	POWER_DOMAIN_AUX_TBT_E, \
	POWER_DOMAIN_AUX_TBT_F

I915_DECL_PW_DOMAINS(icl_pwdoms_pw_3,
	ICL_PW_3_POWER_DOMAINS,
	POWER_DOMAIN_INIT);
	/*
	 * - transcoder WD
	 * - KVMR (HW control)
	 */

#define ICL_PW_2_POWER_DOMAINS \
	ICL_PW_3_POWER_DOMAINS, \
	POWER_DOMAIN_TRANSCODER_VDSC_PW2

I915_DECL_PW_DOMAINS(icl_pwdoms_pw_2,
	ICL_PW_2_POWER_DOMAINS,
	POWER_DOMAIN_INIT);
	/*
	 * - KVMR (HW control)
	 */

I915_DECL_PW_DOMAINS(icl_pwdoms_dc_off,
	ICL_PW_2_POWER_DOMAINS,
	POWER_DOMAIN_AUX_A,
	POWER_DOMAIN_MODESET,
	POWER_DOMAIN_DC_OFF,
	POWER_DOMAIN_INIT);

I915_DECL_PW_DOMAINS(icl_pwdoms_ddi_io_a,	POWER_DOMAIN_PORT_DDI_IO_A);
I915_DECL_PW_DOMAINS(icl_pwdoms_ddi_io_b,	POWER_DOMAIN_PORT_DDI_IO_B);
I915_DECL_PW_DOMAINS(icl_pwdoms_ddi_io_c,	POWER_DOMAIN_PORT_DDI_IO_C);
I915_DECL_PW_DOMAINS(icl_pwdoms_ddi_io_d,	POWER_DOMAIN_PORT_DDI_IO_D);
I915_DECL_PW_DOMAINS(icl_pwdoms_ddi_io_e,	POWER_DOMAIN_PORT_DDI_IO_E);
I915_DECL_PW_DOMAINS(icl_pwdoms_ddi_io_f,	POWER_DOMAIN_PORT_DDI_IO_F);

I915_DECL_PW_DOMAINS(icl_pwdoms_aux_a,
	POWER_DOMAIN_AUX_A,
	POWER_DOMAIN_AUX_IO_A);
I915_DECL_PW_DOMAINS(icl_pwdoms_aux_b,		POWER_DOMAIN_AUX_B);
I915_DECL_PW_DOMAINS(icl_pwdoms_aux_c,		POWER_DOMAIN_AUX_C);
I915_DECL_PW_DOMAINS(icl_pwdoms_aux_d,		POWER_DOMAIN_AUX_D);
I915_DECL_PW_DOMAINS(icl_pwdoms_aux_e,		POWER_DOMAIN_AUX_E);
I915_DECL_PW_DOMAINS(icl_pwdoms_aux_f,		POWER_DOMAIN_AUX_F);
I915_DECL_PW_DOMAINS(icl_pwdoms_aux_tbt1,	POWER_DOMAIN_AUX_TBT_C);
I915_DECL_PW_DOMAINS(icl_pwdoms_aux_tbt2,	POWER_DOMAIN_AUX_TBT_D);
I915_DECL_PW_DOMAINS(icl_pwdoms_aux_tbt3,	POWER_DOMAIN_AUX_TBT_E);
I915_DECL_PW_DOMAINS(icl_pwdoms_aux_tbt4,	POWER_DOMAIN_AUX_TBT_F);

static const struct i915_power_well_desc icl_power_wells[] = {
	{
		.name = "always-on",
		.domain_list = &i9xx_pwdoms_always_on,
		.ops = &i9xx_always_on_power_well_ops,
		.always_on = true,
		.id = DISP_PW_ID_NONE,
	}, {
		.name = "PW_1",
		/* Handled by the DMC firmware */
		.domain_list = I915_PW_DOMAINS_NONE,
		.ops = &hsw_power_well_ops,
		.always_on = true,
		.has_fuses = true,
		.id = SKL_DISP_PW_1,
		{
			.hsw.idx = ICL_PW_CTL_IDX_PW_1,
		},
	}, {
		.name = "DC_off",
		.domain_list = &icl_pwdoms_dc_off,
		.ops = &gen9_dc_off_power_well_ops,
		.id = SKL_DISP_DC_OFF,
	}, {
		.name = "PW_2",
		.domain_list = &icl_pwdoms_pw_2,
		.ops = &hsw_power_well_ops,
		.has_fuses = true,
		.id = SKL_DISP_PW_2,
		{
			.hsw.idx = ICL_PW_CTL_IDX_PW_2,
		},
	}, {
		.name = "PW_3",
		.domain_list = &icl_pwdoms_pw_3,
		.ops = &hsw_power_well_ops,
		.has_vga = true,
		.irq_pipe_mask = BIT(PIPE_B),
		.has_fuses = true,
		.id = ICL_DISP_PW_3,
		{
			.hsw.idx = ICL_PW_CTL_IDX_PW_3,
		},
	}, {
		.name = "DDI_IO_A",
		.domain_list = &icl_pwdoms_ddi_io_a,
		.ops = &icl_ddi_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = ICL_PW_CTL_IDX_DDI_A,
		},
	}, {
		.name = "DDI_IO_B",
		.domain_list = &icl_pwdoms_ddi_io_b,
		.ops = &icl_ddi_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = ICL_PW_CTL_IDX_DDI_B,
		},
	}, {
		.name = "DDI_IO_C",
		.domain_list = &icl_pwdoms_ddi_io_c,
		.ops = &icl_ddi_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = ICL_PW_CTL_IDX_DDI_C,
		},
	}, {
		.name = "DDI_IO_D",
		.domain_list = &icl_pwdoms_ddi_io_d,
		.ops = &icl_ddi_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = ICL_PW_CTL_IDX_DDI_D,
		},
	}, {
		.name = "DDI_IO_E",
		.domain_list = &icl_pwdoms_ddi_io_e,
		.ops = &icl_ddi_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = ICL_PW_CTL_IDX_DDI_E,
		},
	}, {
		.name = "DDI_IO_F",
		.domain_list = &icl_pwdoms_ddi_io_f,
		.ops = &icl_ddi_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = ICL_PW_CTL_IDX_DDI_F,
		},
	}, {
		.name = "AUX_A",
		.domain_list = &icl_pwdoms_aux_a,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = ICL_PW_CTL_IDX_AUX_A,
		},
	}, {
		.name = "AUX_B",
		.domain_list = &icl_pwdoms_aux_b,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = ICL_PW_CTL_IDX_AUX_B,
		},
	}, {
		.name = "AUX_C",
		.domain_list = &icl_pwdoms_aux_c,
		.ops = &icl_aux_power_well_ops,
		.is_tc_tbt = false,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = ICL_PW_CTL_IDX_AUX_C,
		},
	}, {
		.name = "AUX_D",
		.domain_list = &icl_pwdoms_aux_d,
		.ops = &icl_aux_power_well_ops,
		.is_tc_tbt = false,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = ICL_PW_CTL_IDX_AUX_D,
		},
	}, {
		.name = "AUX_E",
		.domain_list = &icl_pwdoms_aux_e,
		.ops = &icl_aux_power_well_ops,
		.is_tc_tbt = false,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = ICL_PW_CTL_IDX_AUX_E,
		},
	}, {
		.name = "AUX_F",
		.domain_list = &icl_pwdoms_aux_f,
		.ops = &icl_aux_power_well_ops,
		.is_tc_tbt = false,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = ICL_PW_CTL_IDX_AUX_F,
		},
	}, {
		.name = "AUX_TBT1",
		.domain_list = &icl_pwdoms_aux_tbt1,
		.ops = &icl_aux_power_well_ops,
		.is_tc_tbt = true,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = ICL_PW_CTL_IDX_AUX_TBT1,
		},
	}, {
		.name = "AUX_TBT2",
		.domain_list = &icl_pwdoms_aux_tbt2,
		.ops = &icl_aux_power_well_ops,
		.is_tc_tbt = true,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = ICL_PW_CTL_IDX_AUX_TBT2,
		},
	}, {
		.name = "AUX_TBT3",
		.domain_list = &icl_pwdoms_aux_tbt3,
		.ops = &icl_aux_power_well_ops,
		.is_tc_tbt = true,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = ICL_PW_CTL_IDX_AUX_TBT3,
		},
	}, {
		.name = "AUX_TBT4",
		.domain_list = &icl_pwdoms_aux_tbt4,
		.ops = &icl_aux_power_well_ops,
		.is_tc_tbt = true,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = ICL_PW_CTL_IDX_AUX_TBT4,
		},
	}, {
		.name = "PW_4",
		.domain_list = &icl_pwdoms_pw_4,
		.ops = &hsw_power_well_ops,
		.irq_pipe_mask = BIT(PIPE_C),
		.has_fuses = true,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = ICL_PW_CTL_IDX_PW_4,
		},
	},
};

#define TGL_PW_5_POWER_DOMAINS \
	POWER_DOMAIN_PIPE_D, \
	POWER_DOMAIN_PIPE_PANEL_FITTER_D, \
	POWER_DOMAIN_TRANSCODER_D

I915_DECL_PW_DOMAINS(tgl_pwdoms_pw_5,
	TGL_PW_5_POWER_DOMAINS,
	POWER_DOMAIN_INIT);

#define TGL_PW_4_POWER_DOMAINS \
	TGL_PW_5_POWER_DOMAINS, \
	POWER_DOMAIN_PIPE_C, \
	POWER_DOMAIN_PIPE_PANEL_FITTER_C, \
	POWER_DOMAIN_TRANSCODER_C

I915_DECL_PW_DOMAINS(tgl_pwdoms_pw_4,
	TGL_PW_4_POWER_DOMAINS,
	POWER_DOMAIN_INIT);

#define TGL_PW_3_POWER_DOMAINS \
	TGL_PW_4_POWER_DOMAINS, \
	POWER_DOMAIN_PIPE_B, \
	POWER_DOMAIN_PIPE_PANEL_FITTER_B, \
	POWER_DOMAIN_TRANSCODER_B, \
	POWER_DOMAIN_PORT_DDI_LANES_TC1, \
	POWER_DOMAIN_PORT_DDI_LANES_TC2, \
	POWER_DOMAIN_PORT_DDI_LANES_TC3, \
	POWER_DOMAIN_PORT_DDI_LANES_TC4, \
	POWER_DOMAIN_PORT_DDI_LANES_TC5, \
	POWER_DOMAIN_PORT_DDI_LANES_TC6, \
	POWER_DOMAIN_VGA, \
	POWER_DOMAIN_AUDIO_MMIO, \
	POWER_DOMAIN_AUDIO_PLAYBACK, \
	POWER_DOMAIN_AUX_USBC1, \
	POWER_DOMAIN_AUX_USBC2, \
	POWER_DOMAIN_AUX_USBC3, \
	POWER_DOMAIN_AUX_USBC4, \
	POWER_DOMAIN_AUX_USBC5, \
	POWER_DOMAIN_AUX_USBC6, \
	POWER_DOMAIN_AUX_TBT1, \
	POWER_DOMAIN_AUX_TBT2, \
	POWER_DOMAIN_AUX_TBT3, \
	POWER_DOMAIN_AUX_TBT4, \
	POWER_DOMAIN_AUX_TBT5, \
	POWER_DOMAIN_AUX_TBT6

I915_DECL_PW_DOMAINS(tgl_pwdoms_pw_3,
	TGL_PW_3_POWER_DOMAINS,
	POWER_DOMAIN_INIT);

I915_DECL_PW_DOMAINS(tgl_pwdoms_pw_2,
	TGL_PW_3_POWER_DOMAINS,
	POWER_DOMAIN_TRANSCODER_VDSC_PW2,
	POWER_DOMAIN_INIT);

I915_DECL_PW_DOMAINS(tgl_pwdoms_dc_off,
	TGL_PW_3_POWER_DOMAINS,
	POWER_DOMAIN_AUX_A,
	POWER_DOMAIN_AUX_B,
	POWER_DOMAIN_AUX_C,
	POWER_DOMAIN_MODESET,
	POWER_DOMAIN_INIT);

I915_DECL_PW_DOMAINS(tgl_pwdoms_ddi_io_tc1,	POWER_DOMAIN_PORT_DDI_IO_TC1);
I915_DECL_PW_DOMAINS(tgl_pwdoms_ddi_io_tc2,	POWER_DOMAIN_PORT_DDI_IO_TC2);
I915_DECL_PW_DOMAINS(tgl_pwdoms_ddi_io_tc3,	POWER_DOMAIN_PORT_DDI_IO_TC3);
I915_DECL_PW_DOMAINS(tgl_pwdoms_ddi_io_tc4,	POWER_DOMAIN_PORT_DDI_IO_TC4);
I915_DECL_PW_DOMAINS(tgl_pwdoms_ddi_io_tc5,	POWER_DOMAIN_PORT_DDI_IO_TC5);
I915_DECL_PW_DOMAINS(tgl_pwdoms_ddi_io_tc6,	POWER_DOMAIN_PORT_DDI_IO_TC6);

I915_DECL_PW_DOMAINS(tgl_pwdoms_aux_a,
	POWER_DOMAIN_AUX_A,
	POWER_DOMAIN_AUX_IO_A);
I915_DECL_PW_DOMAINS(tgl_pwdoms_aux_b,		POWER_DOMAIN_AUX_B);
I915_DECL_PW_DOMAINS(tgl_pwdoms_aux_c,		POWER_DOMAIN_AUX_C);

I915_DECL_PW_DOMAINS(tgl_pwdoms_aux_usbc1,	POWER_DOMAIN_AUX_USBC1);
I915_DECL_PW_DOMAINS(tgl_pwdoms_aux_usbc2,	POWER_DOMAIN_AUX_USBC2);
I915_DECL_PW_DOMAINS(tgl_pwdoms_aux_usbc3,	POWER_DOMAIN_AUX_USBC3);
I915_DECL_PW_DOMAINS(tgl_pwdoms_aux_usbc4,	POWER_DOMAIN_AUX_USBC4);
I915_DECL_PW_DOMAINS(tgl_pwdoms_aux_usbc5,	POWER_DOMAIN_AUX_USBC5);
I915_DECL_PW_DOMAINS(tgl_pwdoms_aux_usbc6,	POWER_DOMAIN_AUX_USBC6);

I915_DECL_PW_DOMAINS(tgl_pwdoms_aux_tbt1,	POWER_DOMAIN_AUX_TBT1);
I915_DECL_PW_DOMAINS(tgl_pwdoms_aux_tbt2,	POWER_DOMAIN_AUX_TBT2);
I915_DECL_PW_DOMAINS(tgl_pwdoms_aux_tbt3,	POWER_DOMAIN_AUX_TBT3);
I915_DECL_PW_DOMAINS(tgl_pwdoms_aux_tbt4,	POWER_DOMAIN_AUX_TBT4);
I915_DECL_PW_DOMAINS(tgl_pwdoms_aux_tbt5,	POWER_DOMAIN_AUX_TBT5);
I915_DECL_PW_DOMAINS(tgl_pwdoms_aux_tbt6,	POWER_DOMAIN_AUX_TBT6);

I915_DECL_PW_DOMAINS(tgl_pwdoms_tc_cold_off,
	POWER_DOMAIN_AUX_USBC1,
	POWER_DOMAIN_AUX_USBC2,
	POWER_DOMAIN_AUX_USBC3,
	POWER_DOMAIN_AUX_USBC4,
	POWER_DOMAIN_AUX_USBC5,
	POWER_DOMAIN_AUX_USBC6,
	POWER_DOMAIN_AUX_TBT1,
	POWER_DOMAIN_AUX_TBT2,
	POWER_DOMAIN_AUX_TBT3,
	POWER_DOMAIN_AUX_TBT4,
	POWER_DOMAIN_AUX_TBT5,
	POWER_DOMAIN_AUX_TBT6,
	POWER_DOMAIN_TC_COLD_OFF);

static const struct i915_power_well_desc tgl_power_wells[] = {
	{
		.name = "always-on",
		.domain_list = &i9xx_pwdoms_always_on,
		.ops = &i9xx_always_on_power_well_ops,
		.always_on = true,
		.id = DISP_PW_ID_NONE,
	}, {
		.name = "PW_1",
		/* Handled by the DMC firmware */
		.domain_list = I915_PW_DOMAINS_NONE,
		.ops = &hsw_power_well_ops,
		.always_on = true,
		.has_fuses = true,
		.id = SKL_DISP_PW_1,
		{
			.hsw.idx = ICL_PW_CTL_IDX_PW_1,
		},
	}, {
		.name = "DC_off",
		.domain_list = &tgl_pwdoms_dc_off,
		.ops = &gen9_dc_off_power_well_ops,
		.id = SKL_DISP_DC_OFF,
	}, {
		.name = "PW_2",
		.domain_list = &tgl_pwdoms_pw_2,
		.ops = &hsw_power_well_ops,
		.has_fuses = true,
		.id = SKL_DISP_PW_2,
		{
			.hsw.idx = ICL_PW_CTL_IDX_PW_2,
		},
	}, {
		.name = "PW_3",
		.domain_list = &tgl_pwdoms_pw_3,
		.ops = &hsw_power_well_ops,
		.has_vga = true,
		.irq_pipe_mask = BIT(PIPE_B),
		.has_fuses = true,
		.id = ICL_DISP_PW_3,
		{
			.hsw.idx = ICL_PW_CTL_IDX_PW_3,
		},
	}, {
		.name = "DDI_IO_A",
		.domain_list = &icl_pwdoms_ddi_io_a,
		.ops = &icl_ddi_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = ICL_PW_CTL_IDX_DDI_A,
		}
	}, {
		.name = "DDI_IO_B",
		.domain_list = &icl_pwdoms_ddi_io_b,
		.ops = &icl_ddi_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = ICL_PW_CTL_IDX_DDI_B,
		}
	}, {
		.name = "DDI_IO_C",
		.domain_list = &icl_pwdoms_ddi_io_c,
		.ops = &icl_ddi_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = ICL_PW_CTL_IDX_DDI_C,
		}
	}, {
		.name = "DDI_IO_TC1",
		.domain_list = &tgl_pwdoms_ddi_io_tc1,
		.ops = &icl_ddi_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = TGL_PW_CTL_IDX_DDI_TC1,
		},
	}, {
		.name = "DDI_IO_TC2",
		.domain_list = &tgl_pwdoms_ddi_io_tc2,
		.ops = &icl_ddi_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = TGL_PW_CTL_IDX_DDI_TC2,
		},
	}, {
		.name = "DDI_IO_TC3",
		.domain_list = &tgl_pwdoms_ddi_io_tc3,
		.ops = &icl_ddi_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = TGL_PW_CTL_IDX_DDI_TC3,
		},
	}, {
		.name = "DDI_IO_TC4",
		.domain_list = &tgl_pwdoms_ddi_io_tc4,
		.ops = &icl_ddi_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = TGL_PW_CTL_IDX_DDI_TC4,
		},
	}, {
		.name = "DDI_IO_TC5",
		.domain_list = &tgl_pwdoms_ddi_io_tc5,
		.ops = &icl_ddi_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = TGL_PW_CTL_IDX_DDI_TC5,
		},
	}, {
		.name = "DDI_IO_TC6",
		.domain_list = &tgl_pwdoms_ddi_io_tc6,
		.ops = &icl_ddi_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = TGL_PW_CTL_IDX_DDI_TC6,
		},
	}, {
		.name = "TC_cold_off",
		.domain_list = &tgl_pwdoms_tc_cold_off,
		.ops = &tgl_tc_cold_off_ops,
		.id = TGL_DISP_PW_TC_COLD_OFF,
	}, {
		.name = "AUX_A",
		.domain_list = &tgl_pwdoms_aux_a,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = ICL_PW_CTL_IDX_AUX_A,
		},
	}, {
		.name = "AUX_B",
		.domain_list = &tgl_pwdoms_aux_b,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = ICL_PW_CTL_IDX_AUX_B,
		},
	}, {
		.name = "AUX_C",
		.domain_list = &tgl_pwdoms_aux_c,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = ICL_PW_CTL_IDX_AUX_C,
		},
	}, {
		.name = "AUX_USBC1",
		.domain_list = &tgl_pwdoms_aux_usbc1,
		.ops = &icl_aux_power_well_ops,
		.is_tc_tbt = false,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = TGL_PW_CTL_IDX_AUX_TC1,
		},
	}, {
		.name = "AUX_USBC2",
		.domain_list = &tgl_pwdoms_aux_usbc2,
		.ops = &icl_aux_power_well_ops,
		.is_tc_tbt = false,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = TGL_PW_CTL_IDX_AUX_TC2,
		},
	}, {
		.name = "AUX_USBC3",
		.domain_list = &tgl_pwdoms_aux_usbc3,
		.ops = &icl_aux_power_well_ops,
		.is_tc_tbt = false,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = TGL_PW_CTL_IDX_AUX_TC3,
		},
	}, {
		.name = "AUX_USBC4",
		.domain_list = &tgl_pwdoms_aux_usbc4,
		.ops = &icl_aux_power_well_ops,
		.is_tc_tbt = false,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = TGL_PW_CTL_IDX_AUX_TC4,
		},
	}, {
		.name = "AUX_USBC5",
		.domain_list = &tgl_pwdoms_aux_usbc5,
		.ops = &icl_aux_power_well_ops,
		.is_tc_tbt = false,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = TGL_PW_CTL_IDX_AUX_TC5,
		},
	}, {
		.name = "AUX_USBC6",
		.domain_list = &tgl_pwdoms_aux_usbc6,
		.ops = &icl_aux_power_well_ops,
		.is_tc_tbt = false,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = TGL_PW_CTL_IDX_AUX_TC6,
		},
	}, {
		.name = "AUX_TBT1",
		.domain_list = &tgl_pwdoms_aux_tbt1,
		.ops = &icl_aux_power_well_ops,
		.is_tc_tbt = true,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = TGL_PW_CTL_IDX_AUX_TBT1,
		},
	}, {
		.name = "AUX_TBT2",
		.domain_list = &tgl_pwdoms_aux_tbt2,
		.ops = &icl_aux_power_well_ops,
		.is_tc_tbt = true,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = TGL_PW_CTL_IDX_AUX_TBT2,
		},
	}, {
		.name = "AUX_TBT3",
		.domain_list = &tgl_pwdoms_aux_tbt3,
		.ops = &icl_aux_power_well_ops,
		.is_tc_tbt = true,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = TGL_PW_CTL_IDX_AUX_TBT3,
		},
	}, {
		.name = "AUX_TBT4",
		.domain_list = &tgl_pwdoms_aux_tbt4,
		.ops = &icl_aux_power_well_ops,
		.is_tc_tbt = true,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = TGL_PW_CTL_IDX_AUX_TBT4,
		},
	}, {
		.name = "AUX_TBT5",
		.domain_list = &tgl_pwdoms_aux_tbt5,
		.ops = &icl_aux_power_well_ops,
		.is_tc_tbt = true,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = TGL_PW_CTL_IDX_AUX_TBT5,
		},
	}, {
		.name = "AUX_TBT6",
		.domain_list = &tgl_pwdoms_aux_tbt6,
		.ops = &icl_aux_power_well_ops,
		.is_tc_tbt = true,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = TGL_PW_CTL_IDX_AUX_TBT6,
		},
	}, {
		.name = "PW_4",
		.domain_list = &tgl_pwdoms_pw_4,
		.ops = &hsw_power_well_ops,
		.has_fuses = true,
		.irq_pipe_mask = BIT(PIPE_C),
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = ICL_PW_CTL_IDX_PW_4,
		}
	}, {
		.name = "PW_5",
		.domain_list = &tgl_pwdoms_pw_5,
		.ops = &hsw_power_well_ops,
		.has_fuses = true,
		.irq_pipe_mask = BIT(PIPE_D),
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = TGL_PW_CTL_IDX_PW_5,
		},
	},
};

#define RKL_PW_4_POWER_DOMAINS \
	POWER_DOMAIN_PIPE_C, \
	POWER_DOMAIN_PIPE_PANEL_FITTER_C, \
	POWER_DOMAIN_TRANSCODER_C

I915_DECL_PW_DOMAINS(rkl_pwdoms_pw_4,
	RKL_PW_4_POWER_DOMAINS,
	POWER_DOMAIN_INIT);

#define RKL_PW_3_POWER_DOMAINS \
	RKL_PW_4_POWER_DOMAINS, \
	POWER_DOMAIN_PIPE_B, \
	POWER_DOMAIN_PIPE_PANEL_FITTER_B, \
	POWER_DOMAIN_TRANSCODER_B, \
	POWER_DOMAIN_PORT_DDI_LANES_TC1, \
	POWER_DOMAIN_PORT_DDI_LANES_TC2, \
	POWER_DOMAIN_VGA, \
	POWER_DOMAIN_AUDIO_MMIO, \
	POWER_DOMAIN_AUDIO_PLAYBACK, \
	POWER_DOMAIN_AUX_USBC1, \
	POWER_DOMAIN_AUX_USBC2

I915_DECL_PW_DOMAINS(rkl_pwdoms_pw_3,
	RKL_PW_3_POWER_DOMAINS,
	POWER_DOMAIN_INIT);

/*
 * There is no PW_2/PG_2 on RKL.
 *
 * RKL PW_1/PG_1 domains (under HW/DMC control):
 * - DBUF function (note: registers are in PW0)
 * - PIPE_A and its planes and VDSC/joining, except VGA
 * - transcoder A
 * - DDI_A and DDI_B
 * - FBC
 *
 * RKL PW_0/PG_0 domains (under HW/DMC control):
 * - PCI
 * - clocks except port PLL
 * - shared functions:
 *     * interrupts except pipe interrupts
 *     * MBus except PIPE_MBUS_DBOX_CTL
 *     * DBUF registers
 * - central power except FBC
 * - top-level GTC (DDI-level GTC is in the well associated with the DDI)
 */

I915_DECL_PW_DOMAINS(rkl_pwdoms_dc_off,
	RKL_PW_3_POWER_DOMAINS,
	POWER_DOMAIN_AUX_A,
	POWER_DOMAIN_AUX_B,
	POWER_DOMAIN_MODESET,
	POWER_DOMAIN_INIT);

static const struct i915_power_well_desc rkl_power_wells[] = {
	{
		.name = "always-on",
		.domain_list = &i9xx_pwdoms_always_on,
		.ops = &i9xx_always_on_power_well_ops,
		.always_on = true,
		.id = DISP_PW_ID_NONE,
	}, {
		.name = "PW_1",
		/* Handled by the DMC firmware */
		.domain_list = I915_PW_DOMAINS_NONE,
		.ops = &hsw_power_well_ops,
		.always_on = true,
		.has_fuses = true,
		.id = SKL_DISP_PW_1,
		{
			.hsw.idx = ICL_PW_CTL_IDX_PW_1,
		},
	}, {
		.name = "DC_off",
		.domain_list = &rkl_pwdoms_dc_off,
		.ops = &gen9_dc_off_power_well_ops,
		.id = SKL_DISP_DC_OFF,
	}, {
		.name = "PW_3",
		.domain_list = &rkl_pwdoms_pw_3,
		.ops = &hsw_power_well_ops,
		.irq_pipe_mask = BIT(PIPE_B),
		.has_vga = true,
		.has_fuses = true,
		.id = ICL_DISP_PW_3,
		{
			.hsw.idx = ICL_PW_CTL_IDX_PW_3,
		},
	}, {
		.name = "PW_4",
		.domain_list = &rkl_pwdoms_pw_4,
		.ops = &hsw_power_well_ops,
		.has_fuses = true,
		.irq_pipe_mask = BIT(PIPE_C),
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = ICL_PW_CTL_IDX_PW_4,
		}
	}, {
		.name = "DDI_IO_A",
		.domain_list = &icl_pwdoms_ddi_io_a,
		.ops = &icl_ddi_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = ICL_PW_CTL_IDX_DDI_A,
		}
	}, {
		.name = "DDI_IO_B",
		.domain_list = &icl_pwdoms_ddi_io_b,
		.ops = &icl_ddi_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = ICL_PW_CTL_IDX_DDI_B,
		}
	}, {
		.name = "DDI_IO_TC1",
		.domain_list = &tgl_pwdoms_ddi_io_tc1,
		.ops = &icl_ddi_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = TGL_PW_CTL_IDX_DDI_TC1,
		},
	}, {
		.name = "DDI_IO_TC2",
		.domain_list = &tgl_pwdoms_ddi_io_tc2,
		.ops = &icl_ddi_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = TGL_PW_CTL_IDX_DDI_TC2,
		},
	}, {
		.name = "AUX_A",
		.domain_list = &icl_pwdoms_aux_a,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = ICL_PW_CTL_IDX_AUX_A,
		},
	}, {
		.name = "AUX_B",
		.domain_list = &icl_pwdoms_aux_b,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = ICL_PW_CTL_IDX_AUX_B,
		},
	}, {
		.name = "AUX_USBC1",
		.domain_list = &tgl_pwdoms_aux_usbc1,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = TGL_PW_CTL_IDX_AUX_TC1,
		},
	}, {
		.name = "AUX_USBC2",
		.domain_list = &tgl_pwdoms_aux_usbc2,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = TGL_PW_CTL_IDX_AUX_TC2,
		},
	},
};

/*
 * DG1 onwards Audio MMIO/VERBS lies in PG0 power well.
 */
#define DG1_PW_3_POWER_DOMAINS \
	TGL_PW_4_POWER_DOMAINS, \
	POWER_DOMAIN_PIPE_B, \
	POWER_DOMAIN_PIPE_PANEL_FITTER_B, \
	POWER_DOMAIN_TRANSCODER_B, \
	POWER_DOMAIN_PORT_DDI_LANES_TC1, \
	POWER_DOMAIN_PORT_DDI_LANES_TC2, \
	POWER_DOMAIN_VGA, \
	POWER_DOMAIN_AUDIO_PLAYBACK, \
	POWER_DOMAIN_AUX_USBC1, \
	POWER_DOMAIN_AUX_USBC2

I915_DECL_PW_DOMAINS(dg1_pwdoms_pw_3,
	DG1_PW_3_POWER_DOMAINS,
	POWER_DOMAIN_INIT);

I915_DECL_PW_DOMAINS(dg1_pwdoms_dc_off,
	DG1_PW_3_POWER_DOMAINS,
	POWER_DOMAIN_AUDIO_MMIO,
	POWER_DOMAIN_AUX_A,
	POWER_DOMAIN_AUX_B,
	POWER_DOMAIN_MODESET,
	POWER_DOMAIN_INIT);

I915_DECL_PW_DOMAINS(dg1_pwdoms_pw_2,
	DG1_PW_3_POWER_DOMAINS,
	POWER_DOMAIN_TRANSCODER_VDSC_PW2,
	POWER_DOMAIN_INIT);

static const struct i915_power_well_desc dg1_power_wells[] = {
	{
		.name = "always-on",
		.domain_list = &i9xx_pwdoms_always_on,
		.ops = &i9xx_always_on_power_well_ops,
		.always_on = true,
		.id = DISP_PW_ID_NONE,
	}, {
		.name = "PW_1",
		/* Handled by the DMC firmware */
		.domain_list = I915_PW_DOMAINS_NONE,
		.ops = &hsw_power_well_ops,
		.always_on = true,
		.has_fuses = true,
		.id = SKL_DISP_PW_1,
		{
			.hsw.idx = ICL_PW_CTL_IDX_PW_1,
		},
	}, {
		.name = "DC_off",
		.domain_list = &dg1_pwdoms_dc_off,
		.ops = &gen9_dc_off_power_well_ops,
		.id = SKL_DISP_DC_OFF,
	}, {
		.name = "PW_2",
		.domain_list = &dg1_pwdoms_pw_2,
		.ops = &hsw_power_well_ops,
		.has_fuses = true,
		.id = SKL_DISP_PW_2,
		{
			.hsw.idx = ICL_PW_CTL_IDX_PW_2,
		},
	}, {
		.name = "PW_3",
		.domain_list = &dg1_pwdoms_pw_3,
		.ops = &hsw_power_well_ops,
		.irq_pipe_mask = BIT(PIPE_B),
		.has_vga = true,
		.has_fuses = true,
		.id = ICL_DISP_PW_3,
		{
			.hsw.idx = ICL_PW_CTL_IDX_PW_3,
		},
	}, {
		.name = "DDI_IO_A",
		.domain_list = &icl_pwdoms_ddi_io_a,
		.ops = &icl_ddi_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = ICL_PW_CTL_IDX_DDI_A,
		}
	}, {
		.name = "DDI_IO_B",
		.domain_list = &icl_pwdoms_ddi_io_b,
		.ops = &icl_ddi_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = ICL_PW_CTL_IDX_DDI_B,
		}
	}, {
		.name = "DDI_IO_TC1",
		.domain_list = &tgl_pwdoms_ddi_io_tc1,
		.ops = &icl_ddi_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = TGL_PW_CTL_IDX_DDI_TC1,
		},
	}, {
		.name = "DDI_IO_TC2",
		.domain_list = &tgl_pwdoms_ddi_io_tc2,
		.ops = &icl_ddi_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = TGL_PW_CTL_IDX_DDI_TC2,
		},
	}, {
		.name = "AUX_A",
		.domain_list = &tgl_pwdoms_aux_a,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = ICL_PW_CTL_IDX_AUX_A,
		},
	}, {
		.name = "AUX_B",
		.domain_list = &tgl_pwdoms_aux_b,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = ICL_PW_CTL_IDX_AUX_B,
		},
	}, {
		.name = "AUX_USBC1",
		.domain_list = &tgl_pwdoms_aux_usbc1,
		.ops = &icl_aux_power_well_ops,
		.is_tc_tbt = false,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = TGL_PW_CTL_IDX_AUX_TC1,
		},
	}, {
		.name = "AUX_USBC2",
		.domain_list = &tgl_pwdoms_aux_usbc2,
		.ops = &icl_aux_power_well_ops,
		.is_tc_tbt = false,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = TGL_PW_CTL_IDX_AUX_TC2,
		},
	}, {
		.name = "PW_4",
		.domain_list = &tgl_pwdoms_pw_4,
		.ops = &hsw_power_well_ops,
		.has_fuses = true,
		.irq_pipe_mask = BIT(PIPE_C),
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = ICL_PW_CTL_IDX_PW_4,
		}
	}, {
		.name = "PW_5",
		.domain_list = &tgl_pwdoms_pw_5,
		.ops = &hsw_power_well_ops,
		.has_fuses = true,
		.irq_pipe_mask = BIT(PIPE_D),
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = TGL_PW_CTL_IDX_PW_5,
		},
	},
};

/*
 * XE_LPD Power Domains
 *
 * Previous platforms required that PG(n-1) be enabled before PG(n).  That
 * dependency chain turns into a dependency tree on XE_LPD:
 *
 *       PG0
 *        |
 *     --PG1--
 *    /       \
 *  PGA     --PG2--
 *         /   |   \
 *       PGB  PGC  PGD
 *
 * Power wells must be enabled from top to bottom and disabled from bottom
 * to top.  This allows pipes to be power gated independently.
 */

#define XELPD_PW_D_POWER_DOMAINS \
	POWER_DOMAIN_PIPE_D, \
	POWER_DOMAIN_PIPE_PANEL_FITTER_D, \
	POWER_DOMAIN_TRANSCODER_D

I915_DECL_PW_DOMAINS(xelpd_pwdoms_pw_d,
	XELPD_PW_D_POWER_DOMAINS,
	POWER_DOMAIN_INIT);

#define XELPD_PW_C_POWER_DOMAINS \
	POWER_DOMAIN_PIPE_C, \
	POWER_DOMAIN_PIPE_PANEL_FITTER_C, \
	POWER_DOMAIN_TRANSCODER_C

I915_DECL_PW_DOMAINS(xelpd_pwdoms_pw_c,
	XELPD_PW_C_POWER_DOMAINS,
	POWER_DOMAIN_INIT);

#define XELPD_PW_B_POWER_DOMAINS \
	POWER_DOMAIN_PIPE_B, \
	POWER_DOMAIN_PIPE_PANEL_FITTER_B, \
	POWER_DOMAIN_TRANSCODER_B

I915_DECL_PW_DOMAINS(xelpd_pwdoms_pw_b,
	XELPD_PW_B_POWER_DOMAINS,
	POWER_DOMAIN_INIT);

I915_DECL_PW_DOMAINS(xelpd_pwdoms_pw_a,
	POWER_DOMAIN_PIPE_A,
	POWER_DOMAIN_PIPE_PANEL_FITTER_A,
	POWER_DOMAIN_INIT);

#define XELPD_PW_2_POWER_DOMAINS \
	XELPD_PW_B_POWER_DOMAINS, \
	XELPD_PW_C_POWER_DOMAINS, \
	XELPD_PW_D_POWER_DOMAINS, \
	POWER_DOMAIN_PORT_DDI_LANES_C, \
	POWER_DOMAIN_PORT_DDI_LANES_D_XELPD, \
	POWER_DOMAIN_PORT_DDI_LANES_E_XELPD, \
	POWER_DOMAIN_PORT_DDI_LANES_TC1, \
	POWER_DOMAIN_PORT_DDI_LANES_TC2, \
	POWER_DOMAIN_PORT_DDI_LANES_TC3, \
	POWER_DOMAIN_PORT_DDI_LANES_TC4, \
	POWER_DOMAIN_VGA, \
	POWER_DOMAIN_AUDIO_PLAYBACK, \
	POWER_DOMAIN_AUX_C, \
	POWER_DOMAIN_AUX_D_XELPD, \
	POWER_DOMAIN_AUX_E_XELPD, \
	POWER_DOMAIN_AUX_USBC1, \
	POWER_DOMAIN_AUX_USBC2, \
	POWER_DOMAIN_AUX_USBC3, \
	POWER_DOMAIN_AUX_USBC4, \
	POWER_DOMAIN_AUX_TBT1, \
	POWER_DOMAIN_AUX_TBT2, \
	POWER_DOMAIN_AUX_TBT3, \
	POWER_DOMAIN_AUX_TBT4

I915_DECL_PW_DOMAINS(xelpd_pwdoms_pw_2,
	XELPD_PW_2_POWER_DOMAINS,
	POWER_DOMAIN_INIT);

/*
 * XELPD PW_1/PG_1 domains (under HW/DMC control):
 *  - DBUF function (registers are in PW0)
 *  - Transcoder A
 *  - DDI_A and DDI_B
 *
 * XELPD PW_0/PW_1 domains (under HW/DMC control):
 *  - PCI
 *  - Clocks except port PLL
 *  - Shared functions:
 *     * interrupts except pipe interrupts
 *     * MBus except PIPE_MBUS_DBOX_CTL
 *     * DBUF registers
 *  - Central power except FBC
 *  - Top-level GTC (DDI-level GTC is in the well associated with the DDI)
 */

I915_DECL_PW_DOMAINS(xelpd_pwdoms_dc_off,
	XELPD_PW_2_POWER_DOMAINS,
	POWER_DOMAIN_PORT_DSI,
	POWER_DOMAIN_AUDIO_MMIO,
	POWER_DOMAIN_AUX_A,
	POWER_DOMAIN_AUX_B,
	POWER_DOMAIN_MODESET,
	POWER_DOMAIN_INIT);

I915_DECL_PW_DOMAINS(xelpd_pwdoms_aux_d_xelpd,		POWER_DOMAIN_AUX_D_XELPD);
I915_DECL_PW_DOMAINS(xelpd_pwdoms_aux_e_xelpd,		POWER_DOMAIN_AUX_E_XELPD);

I915_DECL_PW_DOMAINS(xelpd_pwdoms_aux_usbc1,		POWER_DOMAIN_AUX_USBC1);
I915_DECL_PW_DOMAINS(xelpd_pwdoms_aux_usbc2,		POWER_DOMAIN_AUX_USBC2);
I915_DECL_PW_DOMAINS(xelpd_pwdoms_aux_usbc3,		POWER_DOMAIN_AUX_USBC3);
I915_DECL_PW_DOMAINS(xelpd_pwdoms_aux_usbc4,		POWER_DOMAIN_AUX_USBC4);

I915_DECL_PW_DOMAINS(xelpd_pwdoms_aux_tbt1,		POWER_DOMAIN_AUX_TBT1);
I915_DECL_PW_DOMAINS(xelpd_pwdoms_aux_tbt2,		POWER_DOMAIN_AUX_TBT2);
I915_DECL_PW_DOMAINS(xelpd_pwdoms_aux_tbt3,		POWER_DOMAIN_AUX_TBT3);
I915_DECL_PW_DOMAINS(xelpd_pwdoms_aux_tbt4,		POWER_DOMAIN_AUX_TBT4);

I915_DECL_PW_DOMAINS(xelpd_pwdoms_ddi_io_d_xelpd,	POWER_DOMAIN_PORT_DDI_IO_D_XELPD);
I915_DECL_PW_DOMAINS(xelpd_pwdoms_ddi_io_e_xelpd,	POWER_DOMAIN_PORT_DDI_IO_E_XELPD);
I915_DECL_PW_DOMAINS(xelpd_pwdoms_ddi_io_tc1,		POWER_DOMAIN_PORT_DDI_IO_TC1);
I915_DECL_PW_DOMAINS(xelpd_pwdoms_ddi_io_tc2,		POWER_DOMAIN_PORT_DDI_IO_TC2);
I915_DECL_PW_DOMAINS(xelpd_pwdoms_ddi_io_tc3,		POWER_DOMAIN_PORT_DDI_IO_TC3);
I915_DECL_PW_DOMAINS(xelpd_pwdoms_ddi_io_tc4,		POWER_DOMAIN_PORT_DDI_IO_TC4);

static const struct i915_power_well_desc xelpd_power_wells[] = {
	{
		.name = "always-on",
		.domain_list = &i9xx_pwdoms_always_on,
		.ops = &i9xx_always_on_power_well_ops,
		.always_on = true,
		.id = DISP_PW_ID_NONE,
	}, {
		.name = "PW_1",
		/* Handled by the DMC firmware */
		.domain_list = I915_PW_DOMAINS_NONE,
		.ops = &hsw_power_well_ops,
		.always_on = true,
		.has_fuses = true,
		.id = SKL_DISP_PW_1,
		{
			.hsw.idx = ICL_PW_CTL_IDX_PW_1,
		},
	}, {
		.name = "DC_off",
		.domain_list = &xelpd_pwdoms_dc_off,
		.ops = &gen9_dc_off_power_well_ops,
		.id = SKL_DISP_DC_OFF,
	}, {
		.name = "PW_2",
		.domain_list = &xelpd_pwdoms_pw_2,
		.ops = &hsw_power_well_ops,
		.has_vga = true,
		.has_fuses = true,
		.id = SKL_DISP_PW_2,
		{
			.hsw.idx = ICL_PW_CTL_IDX_PW_2,
		},
	}, {
		.name = "PW_A",
		.domain_list = &xelpd_pwdoms_pw_a,
		.ops = &hsw_power_well_ops,
		.irq_pipe_mask = BIT(PIPE_A),
		.has_fuses = true,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = XELPD_PW_CTL_IDX_PW_A,
		},
	}, {
		.name = "PW_B",
		.domain_list = &xelpd_pwdoms_pw_b,
		.ops = &hsw_power_well_ops,
		.irq_pipe_mask = BIT(PIPE_B),
		.has_fuses = true,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = XELPD_PW_CTL_IDX_PW_B,
		},
	}, {
		.name = "PW_C",
		.domain_list = &xelpd_pwdoms_pw_c,
		.ops = &hsw_power_well_ops,
		.irq_pipe_mask = BIT(PIPE_C),
		.has_fuses = true,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = XELPD_PW_CTL_IDX_PW_C,
		},
	}, {
		.name = "PW_D",
		.domain_list = &xelpd_pwdoms_pw_d,
		.ops = &hsw_power_well_ops,
		.irq_pipe_mask = BIT(PIPE_D),
		.has_fuses = true,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = XELPD_PW_CTL_IDX_PW_D,
		},
	}, {
		.name = "DDI_IO_A",
		.domain_list = &icl_pwdoms_ddi_io_a,
		.ops = &icl_ddi_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = ICL_PW_CTL_IDX_DDI_A,
		}
	}, {
		.name = "DDI_IO_B",
		.domain_list = &icl_pwdoms_ddi_io_b,
		.ops = &icl_ddi_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = ICL_PW_CTL_IDX_DDI_B,
		}
	}, {
		.name = "DDI_IO_C",
		.domain_list = &icl_pwdoms_ddi_io_c,
		.ops = &icl_ddi_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = ICL_PW_CTL_IDX_DDI_C,
		}
	}, {
		.name = "DDI_IO_D_XELPD",
		.domain_list = &xelpd_pwdoms_ddi_io_d_xelpd,
		.ops = &icl_ddi_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = XELPD_PW_CTL_IDX_DDI_D,
		}
	}, {
		.name = "DDI_IO_E_XELPD",
		.domain_list = &xelpd_pwdoms_ddi_io_e_xelpd,
		.ops = &icl_ddi_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = XELPD_PW_CTL_IDX_DDI_E,
		}
	}, {
		.name = "DDI_IO_TC1",
		.domain_list = &xelpd_pwdoms_ddi_io_tc1,
		.ops = &icl_ddi_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = TGL_PW_CTL_IDX_DDI_TC1,
		}
	}, {
		.name = "DDI_IO_TC2",
		.domain_list = &xelpd_pwdoms_ddi_io_tc2,
		.ops = &icl_ddi_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = TGL_PW_CTL_IDX_DDI_TC2,
		}
	}, {
		.name = "DDI_IO_TC3",
		.domain_list = &xelpd_pwdoms_ddi_io_tc3,
		.ops = &icl_ddi_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = TGL_PW_CTL_IDX_DDI_TC3,
		}
	}, {
		.name = "DDI_IO_TC4",
		.domain_list = &xelpd_pwdoms_ddi_io_tc4,
		.ops = &icl_ddi_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = TGL_PW_CTL_IDX_DDI_TC4,
		}
	}, {
		.name = "AUX_A",
		.domain_list = &icl_pwdoms_aux_a,
		.ops = &icl_aux_power_well_ops,
		.fixed_enable_delay = true,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = ICL_PW_CTL_IDX_AUX_A,
		},
	}, {
		.name = "AUX_B",
		.domain_list = &icl_pwdoms_aux_b,
		.ops = &icl_aux_power_well_ops,
		.fixed_enable_delay = true,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = ICL_PW_CTL_IDX_AUX_B,
		},
	}, {
		.name = "AUX_C",
		.domain_list = &tgl_pwdoms_aux_c,
		.ops = &icl_aux_power_well_ops,
		.fixed_enable_delay = true,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = ICL_PW_CTL_IDX_AUX_C,
		},
	}, {
		.name = "AUX_D_XELPD",
		.domain_list = &xelpd_pwdoms_aux_d_xelpd,
		.ops = &icl_aux_power_well_ops,
		.fixed_enable_delay = true,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = XELPD_PW_CTL_IDX_AUX_D,
		},
	}, {
		.name = "AUX_E_XELPD",
		.domain_list = &xelpd_pwdoms_aux_e_xelpd,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = XELPD_PW_CTL_IDX_AUX_E,
		},
	}, {
		.name = "AUX_USBC1",
		.domain_list = &xelpd_pwdoms_aux_usbc1,
		.ops = &icl_aux_power_well_ops,
		.fixed_enable_delay = true,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = TGL_PW_CTL_IDX_AUX_TC1,
		},
	}, {
		.name = "AUX_USBC2",
		.domain_list = &xelpd_pwdoms_aux_usbc2,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = TGL_PW_CTL_IDX_AUX_TC2,
		},
	}, {
		.name = "AUX_USBC3",
		.domain_list = &xelpd_pwdoms_aux_usbc3,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = TGL_PW_CTL_IDX_AUX_TC3,
		},
	}, {
		.name = "AUX_USBC4",
		.domain_list = &xelpd_pwdoms_aux_usbc4,
		.ops = &icl_aux_power_well_ops,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = TGL_PW_CTL_IDX_AUX_TC4,
		},
	}, {
		.name = "AUX_TBT1",
		.domain_list = &xelpd_pwdoms_aux_tbt1,
		.ops = &icl_aux_power_well_ops,
		.is_tc_tbt = true,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = TGL_PW_CTL_IDX_AUX_TBT1,
		},
	}, {
		.name = "AUX_TBT2",
		.domain_list = &xelpd_pwdoms_aux_tbt2,
		.ops = &icl_aux_power_well_ops,
		.is_tc_tbt = true,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = TGL_PW_CTL_IDX_AUX_TBT2,
		},
	}, {
		.name = "AUX_TBT3",
		.domain_list = &xelpd_pwdoms_aux_tbt3,
		.ops = &icl_aux_power_well_ops,
		.is_tc_tbt = true,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = TGL_PW_CTL_IDX_AUX_TBT3,
		},
	}, {
		.name = "AUX_TBT4",
		.domain_list = &xelpd_pwdoms_aux_tbt4,
		.ops = &icl_aux_power_well_ops,
		.is_tc_tbt = true,
		.id = DISP_PW_ID_NONE,
		{
			.hsw.idx = TGL_PW_CTL_IDX_AUX_TBT4,
		},
	},
};

static void init_power_well_domains(const struct i915_power_well_desc *desc,
				    struct i915_power_well *power_well)
{
	int j;

	if (!desc->domain_list)
		return;

	if (desc->domain_list->count == 0) {
		bitmap_fill(power_well->domains.bits, POWER_DOMAIN_NUM);

		return;
	}

	for (j = 0; j < desc->domain_list->count; j++)
		set_bit(desc->domain_list->list[j], power_well->domains.bits);
}

static int
__set_power_wells(struct i915_power_domains *power_domains,
		  const struct i915_power_well_desc *power_well_descs,
		  int power_well_descs_sz, u64 skip_mask)
{
	struct drm_i915_private *i915 = container_of(power_domains,
						     struct drm_i915_private,
						     power_domains);
	u64 power_well_ids = 0;
	int power_well_count = 0;
	int i, plt_idx = 0;

	for (i = 0; i < power_well_descs_sz; i++)
		if (!(BIT_ULL(power_well_descs[i].id) & skip_mask))
			power_well_count++;

	power_domains->power_well_count = power_well_count;
	power_domains->power_wells =
				kcalloc(power_well_count,
					sizeof(*power_domains->power_wells),
					GFP_KERNEL);
	if (!power_domains->power_wells)
		return -ENOMEM;

	for (i = 0; i < power_well_descs_sz; i++) {
		enum i915_power_well_id id = power_well_descs[i].id;

		if (BIT_ULL(id) & skip_mask)
			continue;

		power_domains->power_wells[plt_idx].desc =
			&power_well_descs[i];

		init_power_well_domains(&power_well_descs[i], &power_domains->power_wells[plt_idx]);

		plt_idx++;

		if (id == DISP_PW_ID_NONE)
			continue;

		drm_WARN_ON(&i915->drm, id >= sizeof(power_well_ids) * 8);
		drm_WARN_ON(&i915->drm, power_well_ids & BIT_ULL(id));
		power_well_ids |= BIT_ULL(id);
	}

	return 0;
}

#define set_power_wells_mask(power_domains, __power_well_descs, skip_mask) \
	__set_power_wells(power_domains, __power_well_descs, \
			  ARRAY_SIZE(__power_well_descs), skip_mask)

#define set_power_wells(power_domains, __power_well_descs) \
	set_power_wells_mask(power_domains, __power_well_descs, 0)

/**
 * intel_display_power_map_init - initialize power domain -> power well mappings
 * @power_domains: power domain state
 *
 * Creates all the power wells for the current platform, initializes the
 * dynamic state for them and initializes the mapping of each power well to
 * all the power domains the power well belongs to.
 */
int intel_display_power_map_init(struct i915_power_domains *power_domains)
{
	struct drm_i915_private *i915 = container_of(power_domains,
						     struct drm_i915_private,
						     power_domains);
	/*
	 * The enabling order will be from lower to higher indexed wells,
	 * the disabling order is reversed.
	 */
	if (!HAS_DISPLAY(i915)) {
		power_domains->power_well_count = 0;
		return 0;
	}

	if (DISPLAY_VER(i915) >= 13)
		return set_power_wells(power_domains, xelpd_power_wells);
	else if (IS_DG1(i915))
		return set_power_wells(power_domains, dg1_power_wells);
	else if (IS_ALDERLAKE_S(i915))
		return set_power_wells_mask(power_domains, tgl_power_wells,
					   BIT_ULL(TGL_DISP_PW_TC_COLD_OFF));
	else if (IS_ROCKETLAKE(i915))
		return set_power_wells(power_domains, rkl_power_wells);
	else if (DISPLAY_VER(i915) == 12)
		return set_power_wells(power_domains, tgl_power_wells);
	else if (DISPLAY_VER(i915) == 11)
		return set_power_wells(power_domains, icl_power_wells);
	else if (IS_GEMINILAKE(i915))
		return set_power_wells(power_domains, glk_power_wells);
	else if (IS_BROXTON(i915))
		return set_power_wells(power_domains, bxt_power_wells);
	else if (DISPLAY_VER(i915) == 9)
		return set_power_wells(power_domains, skl_power_wells);
	else if (IS_CHERRYVIEW(i915))
		return set_power_wells(power_domains, chv_power_wells);
	else if (IS_BROADWELL(i915))
		return set_power_wells(power_domains, bdw_power_wells);
	else if (IS_HASWELL(i915))
		return set_power_wells(power_domains, hsw_power_wells);
	else if (IS_VALLEYVIEW(i915))
		return set_power_wells(power_domains, vlv_power_wells);
	else if (IS_I830(i915))
		return set_power_wells(power_domains, i830_power_wells);
	else
		return set_power_wells(power_domains, i9xx_always_on_power_well);
}

/**
 * intel_display_power_map_cleanup - clean up power domain -> power well mappings
 * @power_domains: power domain state
 *
 * Cleans up all the state that was initialized by intel_display_power_map_init().
 */
void intel_display_power_map_cleanup(struct i915_power_domains *power_domains)
{
	kfree(power_domains->power_wells);
}
