// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "i915_drv.h"
#include "i915_reg.h"

#include "vlv_sideband_reg.h"

#include "intel_display_power_map.h"
#include "intel_display_power_well.h"
#include "intel_display_types.h"

#define __LIST_INLINE_ELEMS(__elem_type, ...) \
	((__elem_type[]) { __VA_ARGS__ })

#define __LIST(__elems) { \
	.list = __elems, \
	.count = ARRAY_SIZE(__elems), \
}

#define I915_PW_DOMAINS(...) \
	(const struct i915_power_domain_list) \
		__LIST(__LIST_INLINE_ELEMS(const enum intel_display_power_domain, __VA_ARGS__))

#define I915_DECL_PW_DOMAINS(__name, ...) \
	static const struct i915_power_domain_list __name = I915_PW_DOMAINS(__VA_ARGS__)

/* Zero-length list assigns all power domains, a NULL list assigns none. */
#define I915_PW_DOMAINS_NONE	NULL
#define I915_PW_DOMAINS_ALL	/* zero-length list */

#define I915_PW_INSTANCES(...) \
	(const struct i915_power_well_instance_list) \
		__LIST(__LIST_INLINE_ELEMS(const struct i915_power_well_instance, __VA_ARGS__))

#define I915_PW(_name, _domain_list, ...) \
	{ .name = _name, .domain_list = _domain_list, ## __VA_ARGS__ }


struct i915_power_well_desc_list {
	const struct i915_power_well_desc *list;
	u8 count;
};

#define I915_PW_DESCRIPTORS(x) __LIST(x)


I915_DECL_PW_DOMAINS(i9xx_pwdoms_always_on, I915_PW_DOMAINS_ALL);

static const struct i915_power_well_desc i9xx_power_wells_always_on[] = {
	{
		.instances = &I915_PW_INSTANCES(
			I915_PW("always-on", &i9xx_pwdoms_always_on),
		),
		.ops = &i9xx_always_on_power_well_ops,
		.always_on = true,
	},
};

static const struct i915_power_well_desc_list i9xx_power_wells[] = {
	I915_PW_DESCRIPTORS(i9xx_power_wells_always_on),
};

I915_DECL_PW_DOMAINS(i830_pwdoms_pipes,
	POWER_DOMAIN_PIPE_A,
	POWER_DOMAIN_PIPE_B,
	POWER_DOMAIN_PIPE_PANEL_FITTER_A,
	POWER_DOMAIN_PIPE_PANEL_FITTER_B,
	POWER_DOMAIN_TRANSCODER_A,
	POWER_DOMAIN_TRANSCODER_B,
	POWER_DOMAIN_INIT);

static const struct i915_power_well_desc i830_power_wells_main[] = {
	{
		.instances = &I915_PW_INSTANCES(
			I915_PW("pipes", &i830_pwdoms_pipes),
		),
		.ops = &i830_pipes_power_well_ops,
	},
};

static const struct i915_power_well_desc_list i830_power_wells[] = {
	I915_PW_DESCRIPTORS(i9xx_power_wells_always_on),
	I915_PW_DESCRIPTORS(i830_power_wells_main),
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

static const struct i915_power_well_desc hsw_power_wells_main[] = {
	{
		.instances = &I915_PW_INSTANCES(
			I915_PW("display", &hsw_pwdoms_display,
				.hsw.idx = HSW_PW_CTL_IDX_GLOBAL,
				.id = HSW_DISP_PW_GLOBAL),
		),
		.ops = &hsw_power_well_ops,
		.has_vga = true,
	},
};

static const struct i915_power_well_desc_list hsw_power_wells[] = {
	I915_PW_DESCRIPTORS(i9xx_power_wells_always_on),
	I915_PW_DESCRIPTORS(hsw_power_wells_main),
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

static const struct i915_power_well_desc bdw_power_wells_main[] = {
	{
		.instances = &I915_PW_INSTANCES(
			I915_PW("display", &bdw_pwdoms_display,
				.hsw.idx = HSW_PW_CTL_IDX_GLOBAL,
				.id = HSW_DISP_PW_GLOBAL),
		),
		.ops = &hsw_power_well_ops,
		.has_vga = true,
		.irq_pipe_mask = BIT(PIPE_B) | BIT(PIPE_C),
	},
};

static const struct i915_power_well_desc_list bdw_power_wells[] = {
	I915_PW_DESCRIPTORS(i9xx_power_wells_always_on),
	I915_PW_DESCRIPTORS(bdw_power_wells_main),
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
	POWER_DOMAIN_AUX_IO_B,
	POWER_DOMAIN_AUX_IO_C,
	POWER_DOMAIN_AUX_B,
	POWER_DOMAIN_AUX_C,
	POWER_DOMAIN_GMBUS,
	POWER_DOMAIN_INIT);

I915_DECL_PW_DOMAINS(vlv_pwdoms_dpio_cmn_bc,
	POWER_DOMAIN_PORT_DDI_LANES_B,
	POWER_DOMAIN_PORT_DDI_LANES_C,
	POWER_DOMAIN_PORT_CRT,
	POWER_DOMAIN_AUX_IO_B,
	POWER_DOMAIN_AUX_IO_C,
	POWER_DOMAIN_AUX_B,
	POWER_DOMAIN_AUX_C,
	POWER_DOMAIN_INIT);

I915_DECL_PW_DOMAINS(vlv_pwdoms_dpio_tx_bc_lanes,
	POWER_DOMAIN_PORT_DDI_LANES_B,
	POWER_DOMAIN_PORT_DDI_LANES_C,
	POWER_DOMAIN_AUX_IO_B,
	POWER_DOMAIN_AUX_IO_C,
	POWER_DOMAIN_AUX_B,
	POWER_DOMAIN_AUX_C,
	POWER_DOMAIN_INIT);

static const struct i915_power_well_desc vlv_power_wells_main[] = {
	{
		.instances = &I915_PW_INSTANCES(
			I915_PW("display", &vlv_pwdoms_display,
				.vlv.idx = PUNIT_PWGT_IDX_DISP2D,
				.id = VLV_DISP_PW_DISP2D),
		),
		.ops = &vlv_display_power_well_ops,
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("dpio-tx-b-01", &vlv_pwdoms_dpio_tx_bc_lanes,
				.vlv.idx = PUNIT_PWGT_IDX_DPIO_TX_B_LANES_01),
			I915_PW("dpio-tx-b-23", &vlv_pwdoms_dpio_tx_bc_lanes,
				.vlv.idx = PUNIT_PWGT_IDX_DPIO_TX_B_LANES_23),
			I915_PW("dpio-tx-c-01", &vlv_pwdoms_dpio_tx_bc_lanes,
				.vlv.idx = PUNIT_PWGT_IDX_DPIO_TX_C_LANES_01),
			I915_PW("dpio-tx-c-23", &vlv_pwdoms_dpio_tx_bc_lanes,
				.vlv.idx = PUNIT_PWGT_IDX_DPIO_TX_C_LANES_23),
		),
		.ops = &vlv_dpio_power_well_ops,
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("dpio-common", &vlv_pwdoms_dpio_cmn_bc,
				.vlv.idx = PUNIT_PWGT_IDX_DPIO_CMN_BC,
				.id = VLV_DISP_PW_DPIO_CMN_BC),
		),
		.ops = &vlv_dpio_cmn_power_well_ops,
	},
};

static const struct i915_power_well_desc_list vlv_power_wells[] = {
	I915_PW_DESCRIPTORS(i9xx_power_wells_always_on),
	I915_PW_DESCRIPTORS(vlv_power_wells_main),
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
	POWER_DOMAIN_AUX_IO_B,
	POWER_DOMAIN_AUX_IO_C,
	POWER_DOMAIN_AUX_IO_D,
	POWER_DOMAIN_AUX_B,
	POWER_DOMAIN_AUX_C,
	POWER_DOMAIN_AUX_D,
	POWER_DOMAIN_GMBUS,
	POWER_DOMAIN_INIT);

I915_DECL_PW_DOMAINS(chv_pwdoms_dpio_cmn_bc,
	POWER_DOMAIN_PORT_DDI_LANES_B,
	POWER_DOMAIN_PORT_DDI_LANES_C,
	POWER_DOMAIN_AUX_IO_B,
	POWER_DOMAIN_AUX_IO_C,
	POWER_DOMAIN_AUX_B,
	POWER_DOMAIN_AUX_C,
	POWER_DOMAIN_INIT);

I915_DECL_PW_DOMAINS(chv_pwdoms_dpio_cmn_d,
	POWER_DOMAIN_PORT_DDI_LANES_D,
	POWER_DOMAIN_AUX_IO_D,
	POWER_DOMAIN_AUX_D,
	POWER_DOMAIN_INIT);

static const struct i915_power_well_desc chv_power_wells_main[] = {
	{
		/*
		 * Pipe A power well is the new disp2d well. Pipe B and C
		 * power wells don't actually exist. Pipe A power well is
		 * required for any pipe to work.
		 */
		.instances = &I915_PW_INSTANCES(
			I915_PW("display", &chv_pwdoms_display),
		),
		.ops = &chv_pipe_power_well_ops,
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("dpio-common-bc", &chv_pwdoms_dpio_cmn_bc,
				.vlv.idx = PUNIT_PWGT_IDX_DPIO_CMN_BC,
				.id = VLV_DISP_PW_DPIO_CMN_BC),
			I915_PW("dpio-common-d", &chv_pwdoms_dpio_cmn_d,
				.vlv.idx = PUNIT_PWGT_IDX_DPIO_CMN_D,
				.id = CHV_DISP_PW_DPIO_CMN_D),
		),
		.ops = &chv_dpio_cmn_power_well_ops,
	},
};

static const struct i915_power_well_desc_list chv_power_wells[] = {
	I915_PW_DESCRIPTORS(i9xx_power_wells_always_on),
	I915_PW_DESCRIPTORS(chv_power_wells_main),
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
	POWER_DOMAIN_AUX_IO_B, \
	POWER_DOMAIN_AUX_IO_C, \
	POWER_DOMAIN_AUX_IO_D, \
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
	POWER_DOMAIN_DC_OFF,
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

static const struct i915_power_well_desc skl_power_wells_pw_1[] = {
	{
		/* Handled by the DMC firmware */
		.instances = &I915_PW_INSTANCES(
			I915_PW("PW_1", I915_PW_DOMAINS_NONE,
				.hsw.idx = SKL_PW_CTL_IDX_PW_1,
				.id = SKL_DISP_PW_1),
		),
		.ops = &hsw_power_well_ops,
		.always_on = true,
		.has_fuses = true,
	},
};

static const struct i915_power_well_desc skl_power_wells_main[] = {
	{
		/* Handled by the DMC firmware */
		.instances = &I915_PW_INSTANCES(
			I915_PW("MISC_IO", I915_PW_DOMAINS_NONE,
				.hsw.idx = SKL_PW_CTL_IDX_MISC_IO,
				.id = SKL_DISP_PW_MISC_IO),
		),
		.ops = &hsw_power_well_ops,
		.always_on = true,
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("DC_off", &skl_pwdoms_dc_off,
				.id = SKL_DISP_DC_OFF),
		),
		.ops = &gen9_dc_off_power_well_ops,
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("PW_2", &skl_pwdoms_pw_2,
				.hsw.idx = SKL_PW_CTL_IDX_PW_2,
				.id = SKL_DISP_PW_2),
		),
		.ops = &hsw_power_well_ops,
		.has_vga = true,
		.irq_pipe_mask = BIT(PIPE_B) | BIT(PIPE_C),
		.has_fuses = true,
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("DDI_IO_A_E", &skl_pwdoms_ddi_io_a_e, .hsw.idx = SKL_PW_CTL_IDX_DDI_A_E),
			I915_PW("DDI_IO_B", &skl_pwdoms_ddi_io_b, .hsw.idx = SKL_PW_CTL_IDX_DDI_B),
			I915_PW("DDI_IO_C", &skl_pwdoms_ddi_io_c, .hsw.idx = SKL_PW_CTL_IDX_DDI_C),
			I915_PW("DDI_IO_D", &skl_pwdoms_ddi_io_d, .hsw.idx = SKL_PW_CTL_IDX_DDI_D),
		),
		.ops = &hsw_power_well_ops,
	},
};

static const struct i915_power_well_desc_list skl_power_wells[] = {
	I915_PW_DESCRIPTORS(i9xx_power_wells_always_on),
	I915_PW_DESCRIPTORS(skl_power_wells_pw_1),
	I915_PW_DESCRIPTORS(skl_power_wells_main),
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
	POWER_DOMAIN_AUX_IO_B, \
	POWER_DOMAIN_AUX_IO_C, \
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
	POWER_DOMAIN_DC_OFF,
	POWER_DOMAIN_INIT);

I915_DECL_PW_DOMAINS(bxt_pwdoms_dpio_cmn_a,
	POWER_DOMAIN_PORT_DDI_LANES_A,
	POWER_DOMAIN_AUX_IO_A,
	POWER_DOMAIN_AUX_A,
	POWER_DOMAIN_INIT);

I915_DECL_PW_DOMAINS(bxt_pwdoms_dpio_cmn_bc,
	POWER_DOMAIN_PORT_DDI_LANES_B,
	POWER_DOMAIN_PORT_DDI_LANES_C,
	POWER_DOMAIN_AUX_IO_B,
	POWER_DOMAIN_AUX_IO_C,
	POWER_DOMAIN_AUX_B,
	POWER_DOMAIN_AUX_C,
	POWER_DOMAIN_INIT);

static const struct i915_power_well_desc bxt_power_wells_main[] = {
	{
		.instances = &I915_PW_INSTANCES(
			I915_PW("DC_off", &bxt_pwdoms_dc_off,
				.id = SKL_DISP_DC_OFF),
		),
		.ops = &gen9_dc_off_power_well_ops,
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("PW_2", &bxt_pwdoms_pw_2,
				.hsw.idx = SKL_PW_CTL_IDX_PW_2,
				.id = SKL_DISP_PW_2),
		),
		.ops = &hsw_power_well_ops,
		.has_vga = true,
		.irq_pipe_mask = BIT(PIPE_B) | BIT(PIPE_C),
		.has_fuses = true,
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("dpio-common-a", &bxt_pwdoms_dpio_cmn_a,
				.bxt.phy = DPIO_PHY1,
				.id = BXT_DISP_PW_DPIO_CMN_A),
			I915_PW("dpio-common-bc", &bxt_pwdoms_dpio_cmn_bc,
				.bxt.phy = DPIO_PHY0,
				.id = VLV_DISP_PW_DPIO_CMN_BC),
		),
		.ops = &bxt_dpio_cmn_power_well_ops,
	},
};

static const struct i915_power_well_desc_list bxt_power_wells[] = {
	I915_PW_DESCRIPTORS(i9xx_power_wells_always_on),
	I915_PW_DESCRIPTORS(skl_power_wells_pw_1),
	I915_PW_DESCRIPTORS(bxt_power_wells_main),
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
	POWER_DOMAIN_AUX_IO_B, \
	POWER_DOMAIN_AUX_IO_C, \
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
	POWER_DOMAIN_DC_OFF,
	POWER_DOMAIN_INIT);

I915_DECL_PW_DOMAINS(glk_pwdoms_ddi_io_a,	POWER_DOMAIN_PORT_DDI_IO_A);
I915_DECL_PW_DOMAINS(glk_pwdoms_ddi_io_b,	POWER_DOMAIN_PORT_DDI_IO_B);
I915_DECL_PW_DOMAINS(glk_pwdoms_ddi_io_c,	POWER_DOMAIN_PORT_DDI_IO_C);

I915_DECL_PW_DOMAINS(glk_pwdoms_dpio_cmn_a,
	POWER_DOMAIN_PORT_DDI_LANES_A,
	POWER_DOMAIN_AUX_IO_A,
	POWER_DOMAIN_AUX_A,
	POWER_DOMAIN_INIT);

I915_DECL_PW_DOMAINS(glk_pwdoms_dpio_cmn_b,
	POWER_DOMAIN_PORT_DDI_LANES_B,
	POWER_DOMAIN_AUX_IO_B,
	POWER_DOMAIN_AUX_B,
	POWER_DOMAIN_INIT);

I915_DECL_PW_DOMAINS(glk_pwdoms_dpio_cmn_c,
	POWER_DOMAIN_PORT_DDI_LANES_C,
	POWER_DOMAIN_AUX_IO_C,
	POWER_DOMAIN_AUX_C,
	POWER_DOMAIN_INIT);

I915_DECL_PW_DOMAINS(glk_pwdoms_aux_a,
	POWER_DOMAIN_AUX_IO_A,
	POWER_DOMAIN_AUX_A,
	POWER_DOMAIN_INIT);

I915_DECL_PW_DOMAINS(glk_pwdoms_aux_b,
	POWER_DOMAIN_AUX_IO_B,
	POWER_DOMAIN_AUX_B,
	POWER_DOMAIN_INIT);

I915_DECL_PW_DOMAINS(glk_pwdoms_aux_c,
	POWER_DOMAIN_AUX_IO_C,
	POWER_DOMAIN_AUX_C,
	POWER_DOMAIN_INIT);

static const struct i915_power_well_desc glk_power_wells_main[] = {
	{
		.instances = &I915_PW_INSTANCES(
			I915_PW("DC_off", &glk_pwdoms_dc_off,
				.id = SKL_DISP_DC_OFF),
		),
		.ops = &gen9_dc_off_power_well_ops,
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("PW_2", &glk_pwdoms_pw_2,
				.hsw.idx = SKL_PW_CTL_IDX_PW_2,
				.id = SKL_DISP_PW_2),
		),
		.ops = &hsw_power_well_ops,
		.has_vga = true,
		.irq_pipe_mask = BIT(PIPE_B) | BIT(PIPE_C),
		.has_fuses = true,
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("dpio-common-a", &glk_pwdoms_dpio_cmn_a,
				.bxt.phy = DPIO_PHY1,
				.id = BXT_DISP_PW_DPIO_CMN_A),
			I915_PW("dpio-common-b", &glk_pwdoms_dpio_cmn_b,
				.bxt.phy = DPIO_PHY0,
				.id = VLV_DISP_PW_DPIO_CMN_BC),
			I915_PW("dpio-common-c", &glk_pwdoms_dpio_cmn_c,
				.bxt.phy = DPIO_PHY2,
				.id = GLK_DISP_PW_DPIO_CMN_C),
		),
		.ops = &bxt_dpio_cmn_power_well_ops,
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("AUX_A", &glk_pwdoms_aux_a, .hsw.idx = GLK_PW_CTL_IDX_AUX_A),
			I915_PW("AUX_B", &glk_pwdoms_aux_b, .hsw.idx = GLK_PW_CTL_IDX_AUX_B),
			I915_PW("AUX_C", &glk_pwdoms_aux_c, .hsw.idx = GLK_PW_CTL_IDX_AUX_C),
			I915_PW("DDI_IO_A", &glk_pwdoms_ddi_io_a, .hsw.idx = GLK_PW_CTL_IDX_DDI_A),
			I915_PW("DDI_IO_B", &glk_pwdoms_ddi_io_b, .hsw.idx = SKL_PW_CTL_IDX_DDI_B),
			I915_PW("DDI_IO_C", &glk_pwdoms_ddi_io_c, .hsw.idx = SKL_PW_CTL_IDX_DDI_C),
		),
		.ops = &hsw_power_well_ops,
	},
};

static const struct i915_power_well_desc_list glk_power_wells[] = {
	I915_PW_DESCRIPTORS(i9xx_power_wells_always_on),
	I915_PW_DESCRIPTORS(skl_power_wells_pw_1),
	I915_PW_DESCRIPTORS(glk_power_wells_main),
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
	POWER_DOMAIN_AUX_IO_B, \
	POWER_DOMAIN_AUX_IO_C, \
	POWER_DOMAIN_AUX_IO_D, \
	POWER_DOMAIN_AUX_IO_E, \
	POWER_DOMAIN_AUX_IO_F, \
	POWER_DOMAIN_AUX_B, \
	POWER_DOMAIN_AUX_C, \
	POWER_DOMAIN_AUX_D, \
	POWER_DOMAIN_AUX_E, \
	POWER_DOMAIN_AUX_F, \
	POWER_DOMAIN_AUX_TBT1, \
	POWER_DOMAIN_AUX_TBT2, \
	POWER_DOMAIN_AUX_TBT3, \
	POWER_DOMAIN_AUX_TBT4

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

I915_DECL_PW_DOMAINS(icl_pwdoms_ddi_io_d,	POWER_DOMAIN_PORT_DDI_IO_D);
I915_DECL_PW_DOMAINS(icl_pwdoms_ddi_io_e,	POWER_DOMAIN_PORT_DDI_IO_E);
I915_DECL_PW_DOMAINS(icl_pwdoms_ddi_io_f,	POWER_DOMAIN_PORT_DDI_IO_F);

I915_DECL_PW_DOMAINS(icl_pwdoms_aux_a,
	POWER_DOMAIN_AUX_IO_A,
	POWER_DOMAIN_AUX_A);
I915_DECL_PW_DOMAINS(icl_pwdoms_aux_b,
	POWER_DOMAIN_AUX_IO_B,
	POWER_DOMAIN_AUX_B);
I915_DECL_PW_DOMAINS(icl_pwdoms_aux_c,
	POWER_DOMAIN_AUX_IO_C,
	POWER_DOMAIN_AUX_C);
I915_DECL_PW_DOMAINS(icl_pwdoms_aux_d,
	POWER_DOMAIN_AUX_IO_D,
	POWER_DOMAIN_AUX_D);
I915_DECL_PW_DOMAINS(icl_pwdoms_aux_e,
	POWER_DOMAIN_AUX_IO_E,
	POWER_DOMAIN_AUX_E);
I915_DECL_PW_DOMAINS(icl_pwdoms_aux_f,
	POWER_DOMAIN_AUX_IO_F,
	POWER_DOMAIN_AUX_F);
I915_DECL_PW_DOMAINS(icl_pwdoms_aux_tbt1,	POWER_DOMAIN_AUX_TBT1);
I915_DECL_PW_DOMAINS(icl_pwdoms_aux_tbt2,	POWER_DOMAIN_AUX_TBT2);
I915_DECL_PW_DOMAINS(icl_pwdoms_aux_tbt3,	POWER_DOMAIN_AUX_TBT3);
I915_DECL_PW_DOMAINS(icl_pwdoms_aux_tbt4,	POWER_DOMAIN_AUX_TBT4);

static const struct i915_power_well_desc icl_power_wells_pw_1[] = {
	{
		/* Handled by the DMC firmware */
		.instances = &I915_PW_INSTANCES(
			I915_PW("PW_1", I915_PW_DOMAINS_NONE,
				.hsw.idx = ICL_PW_CTL_IDX_PW_1,
				.id = SKL_DISP_PW_1),
		),
		.ops = &hsw_power_well_ops,
		.always_on = true,
		.has_fuses = true,
	},
};

static const struct i915_power_well_desc icl_power_wells_main[] = {
	{
		.instances = &I915_PW_INSTANCES(
			I915_PW("DC_off", &icl_pwdoms_dc_off,
				.id = SKL_DISP_DC_OFF),
		),
		.ops = &gen9_dc_off_power_well_ops,
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("PW_2", &icl_pwdoms_pw_2,
				.hsw.idx = ICL_PW_CTL_IDX_PW_2,
				.id = SKL_DISP_PW_2),
		),
		.ops = &hsw_power_well_ops,
		.has_fuses = true,
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("PW_3", &icl_pwdoms_pw_3,
				.hsw.idx = ICL_PW_CTL_IDX_PW_3,
				.id = ICL_DISP_PW_3),
		),
		.ops = &hsw_power_well_ops,
		.has_vga = true,
		.irq_pipe_mask = BIT(PIPE_B),
		.has_fuses = true,
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("DDI_IO_A", &glk_pwdoms_ddi_io_a, .hsw.idx = ICL_PW_CTL_IDX_DDI_A),
			I915_PW("DDI_IO_B", &glk_pwdoms_ddi_io_b, .hsw.idx = ICL_PW_CTL_IDX_DDI_B),
			I915_PW("DDI_IO_C", &glk_pwdoms_ddi_io_c, .hsw.idx = ICL_PW_CTL_IDX_DDI_C),
			I915_PW("DDI_IO_D", &icl_pwdoms_ddi_io_d, .hsw.idx = ICL_PW_CTL_IDX_DDI_D),
			I915_PW("DDI_IO_E", &icl_pwdoms_ddi_io_e, .hsw.idx = ICL_PW_CTL_IDX_DDI_E),
			I915_PW("DDI_IO_F", &icl_pwdoms_ddi_io_f, .hsw.idx = ICL_PW_CTL_IDX_DDI_F),
		),
		.ops = &icl_ddi_power_well_ops,
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("AUX_A", &icl_pwdoms_aux_a, .hsw.idx = ICL_PW_CTL_IDX_AUX_A),
			I915_PW("AUX_B", &icl_pwdoms_aux_b, .hsw.idx = ICL_PW_CTL_IDX_AUX_B),
			I915_PW("AUX_C", &icl_pwdoms_aux_c, .hsw.idx = ICL_PW_CTL_IDX_AUX_C),
			I915_PW("AUX_D", &icl_pwdoms_aux_d, .hsw.idx = ICL_PW_CTL_IDX_AUX_D),
			I915_PW("AUX_E", &icl_pwdoms_aux_e, .hsw.idx = ICL_PW_CTL_IDX_AUX_E),
			I915_PW("AUX_F", &icl_pwdoms_aux_f, .hsw.idx = ICL_PW_CTL_IDX_AUX_F),
		),
		.ops = &icl_aux_power_well_ops,
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("AUX_TBT1", &icl_pwdoms_aux_tbt1, .hsw.idx = ICL_PW_CTL_IDX_AUX_TBT1),
			I915_PW("AUX_TBT2", &icl_pwdoms_aux_tbt2, .hsw.idx = ICL_PW_CTL_IDX_AUX_TBT2),
			I915_PW("AUX_TBT3", &icl_pwdoms_aux_tbt3, .hsw.idx = ICL_PW_CTL_IDX_AUX_TBT3),
			I915_PW("AUX_TBT4", &icl_pwdoms_aux_tbt4, .hsw.idx = ICL_PW_CTL_IDX_AUX_TBT4),
		),
		.ops = &icl_aux_power_well_ops,
		.is_tc_tbt = true,
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("PW_4", &icl_pwdoms_pw_4,
				.hsw.idx = ICL_PW_CTL_IDX_PW_4),
		),
		.ops = &hsw_power_well_ops,
		.irq_pipe_mask = BIT(PIPE_C),
		.has_fuses = true,
	},
};

static const struct i915_power_well_desc_list icl_power_wells[] = {
	I915_PW_DESCRIPTORS(i9xx_power_wells_always_on),
	I915_PW_DESCRIPTORS(icl_power_wells_pw_1),
	I915_PW_DESCRIPTORS(icl_power_wells_main),
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
	POWER_DOMAIN_DC_OFF,
	POWER_DOMAIN_INIT);

I915_DECL_PW_DOMAINS(tgl_pwdoms_ddi_io_tc1,	POWER_DOMAIN_PORT_DDI_IO_TC1);
I915_DECL_PW_DOMAINS(tgl_pwdoms_ddi_io_tc2,	POWER_DOMAIN_PORT_DDI_IO_TC2);
I915_DECL_PW_DOMAINS(tgl_pwdoms_ddi_io_tc3,	POWER_DOMAIN_PORT_DDI_IO_TC3);
I915_DECL_PW_DOMAINS(tgl_pwdoms_ddi_io_tc4,	POWER_DOMAIN_PORT_DDI_IO_TC4);
I915_DECL_PW_DOMAINS(tgl_pwdoms_ddi_io_tc5,	POWER_DOMAIN_PORT_DDI_IO_TC5);
I915_DECL_PW_DOMAINS(tgl_pwdoms_ddi_io_tc6,	POWER_DOMAIN_PORT_DDI_IO_TC6);

I915_DECL_PW_DOMAINS(tgl_pwdoms_aux_usbc1,	POWER_DOMAIN_AUX_USBC1);
I915_DECL_PW_DOMAINS(tgl_pwdoms_aux_usbc2,	POWER_DOMAIN_AUX_USBC2);
I915_DECL_PW_DOMAINS(tgl_pwdoms_aux_usbc3,	POWER_DOMAIN_AUX_USBC3);
I915_DECL_PW_DOMAINS(tgl_pwdoms_aux_usbc4,	POWER_DOMAIN_AUX_USBC4);
I915_DECL_PW_DOMAINS(tgl_pwdoms_aux_usbc5,	POWER_DOMAIN_AUX_USBC5);
I915_DECL_PW_DOMAINS(tgl_pwdoms_aux_usbc6,	POWER_DOMAIN_AUX_USBC6);

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

static const struct i915_power_well_desc tgl_power_wells_main[] = {
	{
		.instances = &I915_PW_INSTANCES(
			I915_PW("DC_off", &tgl_pwdoms_dc_off,
				.id = SKL_DISP_DC_OFF),
		),
		.ops = &gen9_dc_off_power_well_ops,
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("PW_2", &tgl_pwdoms_pw_2,
				.hsw.idx = ICL_PW_CTL_IDX_PW_2,
				.id = SKL_DISP_PW_2),
		),
		.ops = &hsw_power_well_ops,
		.has_fuses = true,
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("PW_3", &tgl_pwdoms_pw_3,
				.hsw.idx = ICL_PW_CTL_IDX_PW_3,
				.id = ICL_DISP_PW_3),
		),
		.ops = &hsw_power_well_ops,
		.has_vga = true,
		.irq_pipe_mask = BIT(PIPE_B),
		.has_fuses = true,
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("DDI_IO_A", &glk_pwdoms_ddi_io_a, .hsw.idx = ICL_PW_CTL_IDX_DDI_A),
			I915_PW("DDI_IO_B", &glk_pwdoms_ddi_io_b, .hsw.idx = ICL_PW_CTL_IDX_DDI_B),
			I915_PW("DDI_IO_C", &glk_pwdoms_ddi_io_c, .hsw.idx = ICL_PW_CTL_IDX_DDI_C),
			I915_PW("DDI_IO_TC1", &tgl_pwdoms_ddi_io_tc1, .hsw.idx = TGL_PW_CTL_IDX_DDI_TC1),
			I915_PW("DDI_IO_TC2", &tgl_pwdoms_ddi_io_tc2, .hsw.idx = TGL_PW_CTL_IDX_DDI_TC2),
			I915_PW("DDI_IO_TC3", &tgl_pwdoms_ddi_io_tc3, .hsw.idx = TGL_PW_CTL_IDX_DDI_TC3),
			I915_PW("DDI_IO_TC4", &tgl_pwdoms_ddi_io_tc4, .hsw.idx = TGL_PW_CTL_IDX_DDI_TC4),
			I915_PW("DDI_IO_TC5", &tgl_pwdoms_ddi_io_tc5, .hsw.idx = TGL_PW_CTL_IDX_DDI_TC5),
			I915_PW("DDI_IO_TC6", &tgl_pwdoms_ddi_io_tc6, .hsw.idx = TGL_PW_CTL_IDX_DDI_TC6),
		),
		.ops = &icl_ddi_power_well_ops,
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("PW_4", &tgl_pwdoms_pw_4,
				.hsw.idx = ICL_PW_CTL_IDX_PW_4),
		),
		.ops = &hsw_power_well_ops,
		.has_fuses = true,
		.irq_pipe_mask = BIT(PIPE_C),
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("PW_5", &tgl_pwdoms_pw_5,
				.hsw.idx = TGL_PW_CTL_IDX_PW_5),
		),
		.ops = &hsw_power_well_ops,
		.has_fuses = true,
		.irq_pipe_mask = BIT(PIPE_D),
	},
};

static const struct i915_power_well_desc tgl_power_wells_tc_cold_off[] = {
	{
		.instances = &I915_PW_INSTANCES(
			I915_PW("TC_cold_off", &tgl_pwdoms_tc_cold_off,
				.id = TGL_DISP_PW_TC_COLD_OFF),
		),
		.ops = &tgl_tc_cold_off_ops,
	},
};

static const struct i915_power_well_desc tgl_power_wells_aux[] = {
	{
		.instances = &I915_PW_INSTANCES(
			I915_PW("AUX_A", &icl_pwdoms_aux_a, .hsw.idx = ICL_PW_CTL_IDX_AUX_A),
			I915_PW("AUX_B", &icl_pwdoms_aux_b, .hsw.idx = ICL_PW_CTL_IDX_AUX_B),
			I915_PW("AUX_C", &icl_pwdoms_aux_c, .hsw.idx = ICL_PW_CTL_IDX_AUX_C),
			I915_PW("AUX_USBC1", &tgl_pwdoms_aux_usbc1, .hsw.idx = TGL_PW_CTL_IDX_AUX_TC1),
			I915_PW("AUX_USBC2", &tgl_pwdoms_aux_usbc2, .hsw.idx = TGL_PW_CTL_IDX_AUX_TC2),
			I915_PW("AUX_USBC3", &tgl_pwdoms_aux_usbc3, .hsw.idx = TGL_PW_CTL_IDX_AUX_TC3),
			I915_PW("AUX_USBC4", &tgl_pwdoms_aux_usbc4, .hsw.idx = TGL_PW_CTL_IDX_AUX_TC4),
			I915_PW("AUX_USBC5", &tgl_pwdoms_aux_usbc5, .hsw.idx = TGL_PW_CTL_IDX_AUX_TC5),
			I915_PW("AUX_USBC6", &tgl_pwdoms_aux_usbc6, .hsw.idx = TGL_PW_CTL_IDX_AUX_TC6),
		),
		.ops = &icl_aux_power_well_ops,
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("AUX_TBT1", &icl_pwdoms_aux_tbt1, .hsw.idx = TGL_PW_CTL_IDX_AUX_TBT1),
			I915_PW("AUX_TBT2", &icl_pwdoms_aux_tbt2, .hsw.idx = TGL_PW_CTL_IDX_AUX_TBT2),
			I915_PW("AUX_TBT3", &icl_pwdoms_aux_tbt3, .hsw.idx = TGL_PW_CTL_IDX_AUX_TBT3),
			I915_PW("AUX_TBT4", &icl_pwdoms_aux_tbt4, .hsw.idx = TGL_PW_CTL_IDX_AUX_TBT4),
			I915_PW("AUX_TBT5", &tgl_pwdoms_aux_tbt5, .hsw.idx = TGL_PW_CTL_IDX_AUX_TBT5),
			I915_PW("AUX_TBT6", &tgl_pwdoms_aux_tbt6, .hsw.idx = TGL_PW_CTL_IDX_AUX_TBT6),
		),
		.ops = &icl_aux_power_well_ops,
		.is_tc_tbt = true,
	},
};

static const struct i915_power_well_desc_list tgl_power_wells[] = {
	I915_PW_DESCRIPTORS(i9xx_power_wells_always_on),
	I915_PW_DESCRIPTORS(icl_power_wells_pw_1),
	I915_PW_DESCRIPTORS(tgl_power_wells_main),
	I915_PW_DESCRIPTORS(tgl_power_wells_tc_cold_off),
	I915_PW_DESCRIPTORS(tgl_power_wells_aux),
};

static const struct i915_power_well_desc_list adls_power_wells[] = {
	I915_PW_DESCRIPTORS(i9xx_power_wells_always_on),
	I915_PW_DESCRIPTORS(icl_power_wells_pw_1),
	I915_PW_DESCRIPTORS(tgl_power_wells_main),
	I915_PW_DESCRIPTORS(tgl_power_wells_aux),
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
	POWER_DOMAIN_DC_OFF,
	POWER_DOMAIN_INIT);

static const struct i915_power_well_desc rkl_power_wells_main[] = {
	{
		.instances = &I915_PW_INSTANCES(
			I915_PW("DC_off", &rkl_pwdoms_dc_off,
				.id = SKL_DISP_DC_OFF),
		),
		.ops = &gen9_dc_off_power_well_ops,
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("PW_3", &rkl_pwdoms_pw_3,
				.hsw.idx = ICL_PW_CTL_IDX_PW_3,
				.id = ICL_DISP_PW_3),
		),
		.ops = &hsw_power_well_ops,
		.irq_pipe_mask = BIT(PIPE_B),
		.has_vga = true,
		.has_fuses = true,
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("PW_4", &rkl_pwdoms_pw_4,
				.hsw.idx = ICL_PW_CTL_IDX_PW_4),
		),
		.ops = &hsw_power_well_ops,
		.has_fuses = true,
		.irq_pipe_mask = BIT(PIPE_C),
	},
};

static const struct i915_power_well_desc rkl_power_wells_ddi_aux[] = {
	{
		.instances = &I915_PW_INSTANCES(
			I915_PW("DDI_IO_A", &glk_pwdoms_ddi_io_a, .hsw.idx = ICL_PW_CTL_IDX_DDI_A),
			I915_PW("DDI_IO_B", &glk_pwdoms_ddi_io_b, .hsw.idx = ICL_PW_CTL_IDX_DDI_B),
			I915_PW("DDI_IO_TC1", &tgl_pwdoms_ddi_io_tc1, .hsw.idx = TGL_PW_CTL_IDX_DDI_TC1),
			I915_PW("DDI_IO_TC2", &tgl_pwdoms_ddi_io_tc2, .hsw.idx = TGL_PW_CTL_IDX_DDI_TC2),
		),
		.ops = &icl_ddi_power_well_ops,
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("AUX_A", &icl_pwdoms_aux_a, .hsw.idx = ICL_PW_CTL_IDX_AUX_A),
			I915_PW("AUX_B", &icl_pwdoms_aux_b, .hsw.idx = ICL_PW_CTL_IDX_AUX_B),
			I915_PW("AUX_USBC1", &tgl_pwdoms_aux_usbc1, .hsw.idx = TGL_PW_CTL_IDX_AUX_TC1),
			I915_PW("AUX_USBC2", &tgl_pwdoms_aux_usbc2, .hsw.idx = TGL_PW_CTL_IDX_AUX_TC2),
		),
		.ops = &icl_aux_power_well_ops,
	},
};

static const struct i915_power_well_desc_list rkl_power_wells[] = {
	I915_PW_DESCRIPTORS(i9xx_power_wells_always_on),
	I915_PW_DESCRIPTORS(icl_power_wells_pw_1),
	I915_PW_DESCRIPTORS(rkl_power_wells_main),
	I915_PW_DESCRIPTORS(rkl_power_wells_ddi_aux),
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
	POWER_DOMAIN_DC_OFF,
	POWER_DOMAIN_INIT);

I915_DECL_PW_DOMAINS(dg1_pwdoms_pw_2,
	DG1_PW_3_POWER_DOMAINS,
	POWER_DOMAIN_TRANSCODER_VDSC_PW2,
	POWER_DOMAIN_INIT);

static const struct i915_power_well_desc dg1_power_wells_main[] = {
	{
		.instances = &I915_PW_INSTANCES(
			I915_PW("DC_off", &dg1_pwdoms_dc_off,
				.id = SKL_DISP_DC_OFF),
		),
		.ops = &gen9_dc_off_power_well_ops,
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("PW_2", &dg1_pwdoms_pw_2,
				.hsw.idx = ICL_PW_CTL_IDX_PW_2,
				.id = SKL_DISP_PW_2),
		),
		.ops = &hsw_power_well_ops,
		.has_fuses = true,
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("PW_3", &dg1_pwdoms_pw_3,
				.hsw.idx = ICL_PW_CTL_IDX_PW_3,
				.id = ICL_DISP_PW_3),
		),
		.ops = &hsw_power_well_ops,
		.irq_pipe_mask = BIT(PIPE_B),
		.has_vga = true,
		.has_fuses = true,
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("PW_4", &tgl_pwdoms_pw_4,
				.hsw.idx = ICL_PW_CTL_IDX_PW_4),
		),
		.ops = &hsw_power_well_ops,
		.has_fuses = true,
		.irq_pipe_mask = BIT(PIPE_C),
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("PW_5", &tgl_pwdoms_pw_5,
				.hsw.idx = TGL_PW_CTL_IDX_PW_5),
		),
		.ops = &hsw_power_well_ops,
		.has_fuses = true,
		.irq_pipe_mask = BIT(PIPE_D),
	},
};

static const struct i915_power_well_desc_list dg1_power_wells[] = {
	I915_PW_DESCRIPTORS(i9xx_power_wells_always_on),
	I915_PW_DESCRIPTORS(icl_power_wells_pw_1),
	I915_PW_DESCRIPTORS(dg1_power_wells_main),
	I915_PW_DESCRIPTORS(rkl_power_wells_ddi_aux),
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
	POWER_DOMAIN_PORT_DDI_LANES_D, \
	POWER_DOMAIN_PORT_DDI_LANES_E, \
	POWER_DOMAIN_PORT_DDI_LANES_TC1, \
	POWER_DOMAIN_PORT_DDI_LANES_TC2, \
	POWER_DOMAIN_PORT_DDI_LANES_TC3, \
	POWER_DOMAIN_PORT_DDI_LANES_TC4, \
	POWER_DOMAIN_VGA, \
	POWER_DOMAIN_AUDIO_PLAYBACK, \
	POWER_DOMAIN_AUX_IO_C, \
	POWER_DOMAIN_AUX_IO_D, \
	POWER_DOMAIN_AUX_IO_E, \
	POWER_DOMAIN_AUX_C, \
	POWER_DOMAIN_AUX_D, \
	POWER_DOMAIN_AUX_E, \
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
	POWER_DOMAIN_DC_OFF,
	POWER_DOMAIN_INIT);

static const struct i915_power_well_desc xelpd_power_wells_main[] = {
	{
		.instances = &I915_PW_INSTANCES(
			I915_PW("DC_off", &xelpd_pwdoms_dc_off,
				.id = SKL_DISP_DC_OFF),
		),
		.ops = &gen9_dc_off_power_well_ops,
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("PW_2", &xelpd_pwdoms_pw_2,
				.hsw.idx = ICL_PW_CTL_IDX_PW_2,
				.id = SKL_DISP_PW_2),
		),
		.ops = &hsw_power_well_ops,
		.has_vga = true,
		.has_fuses = true,
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("PW_A", &xelpd_pwdoms_pw_a,
				.hsw.idx = XELPD_PW_CTL_IDX_PW_A),
		),
		.ops = &hsw_power_well_ops,
		.irq_pipe_mask = BIT(PIPE_A),
		.has_fuses = true,
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("PW_B", &xelpd_pwdoms_pw_b,
				.hsw.idx = XELPD_PW_CTL_IDX_PW_B),
		),
		.ops = &hsw_power_well_ops,
		.irq_pipe_mask = BIT(PIPE_B),
		.has_fuses = true,
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("PW_C", &xelpd_pwdoms_pw_c,
				.hsw.idx = XELPD_PW_CTL_IDX_PW_C),
		),
		.ops = &hsw_power_well_ops,
		.irq_pipe_mask = BIT(PIPE_C),
		.has_fuses = true,
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("PW_D", &xelpd_pwdoms_pw_d,
				.hsw.idx = XELPD_PW_CTL_IDX_PW_D),
		),
		.ops = &hsw_power_well_ops,
		.irq_pipe_mask = BIT(PIPE_D),
		.has_fuses = true,
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("DDI_IO_A", &glk_pwdoms_ddi_io_a, .hsw.idx = ICL_PW_CTL_IDX_DDI_A),
			I915_PW("DDI_IO_B", &glk_pwdoms_ddi_io_b, .hsw.idx = ICL_PW_CTL_IDX_DDI_B),
			I915_PW("DDI_IO_C", &glk_pwdoms_ddi_io_c, .hsw.idx = ICL_PW_CTL_IDX_DDI_C),
			I915_PW("DDI_IO_D", &icl_pwdoms_ddi_io_d, .hsw.idx = XELPD_PW_CTL_IDX_DDI_D),
			I915_PW("DDI_IO_E", &icl_pwdoms_ddi_io_e, .hsw.idx = XELPD_PW_CTL_IDX_DDI_E),
			I915_PW("DDI_IO_TC1", &tgl_pwdoms_ddi_io_tc1, .hsw.idx = TGL_PW_CTL_IDX_DDI_TC1),
			I915_PW("DDI_IO_TC2", &tgl_pwdoms_ddi_io_tc2, .hsw.idx = TGL_PW_CTL_IDX_DDI_TC2),
			I915_PW("DDI_IO_TC3", &tgl_pwdoms_ddi_io_tc3, .hsw.idx = TGL_PW_CTL_IDX_DDI_TC3),
			I915_PW("DDI_IO_TC4", &tgl_pwdoms_ddi_io_tc4, .hsw.idx = TGL_PW_CTL_IDX_DDI_TC4),
		),
		.ops = &icl_ddi_power_well_ops,
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("AUX_A", &icl_pwdoms_aux_a, .hsw.idx = ICL_PW_CTL_IDX_AUX_A),
			I915_PW("AUX_B", &icl_pwdoms_aux_b, .hsw.idx = ICL_PW_CTL_IDX_AUX_B),
			I915_PW("AUX_C", &icl_pwdoms_aux_c, .hsw.idx = ICL_PW_CTL_IDX_AUX_C),
			I915_PW("AUX_D", &icl_pwdoms_aux_d, .hsw.idx = XELPD_PW_CTL_IDX_AUX_D),
			I915_PW("AUX_E", &icl_pwdoms_aux_e, .hsw.idx = XELPD_PW_CTL_IDX_AUX_E),
			I915_PW("AUX_USBC1", &tgl_pwdoms_aux_usbc1, .hsw.idx = TGL_PW_CTL_IDX_AUX_TC1),
			I915_PW("AUX_USBC2", &tgl_pwdoms_aux_usbc2, .hsw.idx = TGL_PW_CTL_IDX_AUX_TC2),
			I915_PW("AUX_USBC3", &tgl_pwdoms_aux_usbc3, .hsw.idx = TGL_PW_CTL_IDX_AUX_TC3),
			I915_PW("AUX_USBC4", &tgl_pwdoms_aux_usbc4, .hsw.idx = TGL_PW_CTL_IDX_AUX_TC4),
		),
		.ops = &icl_aux_power_well_ops,
		.fixed_enable_delay = true,
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("AUX_TBT1", &icl_pwdoms_aux_tbt1, .hsw.idx = TGL_PW_CTL_IDX_AUX_TBT1),
			I915_PW("AUX_TBT2", &icl_pwdoms_aux_tbt2, .hsw.idx = TGL_PW_CTL_IDX_AUX_TBT2),
			I915_PW("AUX_TBT3", &icl_pwdoms_aux_tbt3, .hsw.idx = TGL_PW_CTL_IDX_AUX_TBT3),
			I915_PW("AUX_TBT4", &icl_pwdoms_aux_tbt4, .hsw.idx = TGL_PW_CTL_IDX_AUX_TBT4),
		),
		.ops = &icl_aux_power_well_ops,
		.is_tc_tbt = true,
	},
};

static const struct i915_power_well_desc_list xelpd_power_wells[] = {
	I915_PW_DESCRIPTORS(i9xx_power_wells_always_on),
	I915_PW_DESCRIPTORS(icl_power_wells_pw_1),
	I915_PW_DESCRIPTORS(xelpd_power_wells_main),
};

/*
 * MTL is based on XELPD power domains with the exception of power gating for:
 * - DDI_IO (moved to PLL logic)
 * - AUX and AUX_IO functionality and register access for USBC1-4 (PICA always-on)
 */
#define XELPDP_PW_2_POWER_DOMAINS \
	XELPD_PW_B_POWER_DOMAINS, \
	XELPD_PW_C_POWER_DOMAINS, \
	XELPD_PW_D_POWER_DOMAINS, \
	POWER_DOMAIN_AUDIO_PLAYBACK, \
	POWER_DOMAIN_VGA, \
	POWER_DOMAIN_PORT_DDI_LANES_TC1, \
	POWER_DOMAIN_PORT_DDI_LANES_TC2, \
	POWER_DOMAIN_PORT_DDI_LANES_TC3, \
	POWER_DOMAIN_PORT_DDI_LANES_TC4

I915_DECL_PW_DOMAINS(xelpdp_pwdoms_pw_2,
	XELPDP_PW_2_POWER_DOMAINS,
	POWER_DOMAIN_INIT);

I915_DECL_PW_DOMAINS(xelpdp_pwdoms_dc_off,
	XELPDP_PW_2_POWER_DOMAINS,
	POWER_DOMAIN_AUDIO_MMIO,
	POWER_DOMAIN_MODESET,
	POWER_DOMAIN_AUX_A,
	POWER_DOMAIN_AUX_B,
	POWER_DOMAIN_DC_OFF,
	POWER_DOMAIN_INIT);

I915_DECL_PW_DOMAINS(xelpdp_pwdoms_aux_tc1,
	POWER_DOMAIN_AUX_USBC1,
	POWER_DOMAIN_AUX_TBT1);

I915_DECL_PW_DOMAINS(xelpdp_pwdoms_aux_tc2,
	POWER_DOMAIN_AUX_USBC2,
	POWER_DOMAIN_AUX_TBT2);

I915_DECL_PW_DOMAINS(xelpdp_pwdoms_aux_tc3,
	POWER_DOMAIN_AUX_USBC3,
	POWER_DOMAIN_AUX_TBT3);

I915_DECL_PW_DOMAINS(xelpdp_pwdoms_aux_tc4,
	POWER_DOMAIN_AUX_USBC4,
	POWER_DOMAIN_AUX_TBT4);

static const struct i915_power_well_desc xelpdp_power_wells_main[] = {
	{
		.instances = &I915_PW_INSTANCES(
			I915_PW("DC_off", &xelpdp_pwdoms_dc_off,
				.id = SKL_DISP_DC_OFF),
		),
		.ops = &gen9_dc_off_power_well_ops,
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("PW_2", &xelpdp_pwdoms_pw_2,
				.hsw.idx = ICL_PW_CTL_IDX_PW_2,
				.id = SKL_DISP_PW_2),
		),
		.ops = &hsw_power_well_ops,
		.has_vga = true,
		.has_fuses = true,
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("PW_A", &xelpd_pwdoms_pw_a,
				.hsw.idx = XELPD_PW_CTL_IDX_PW_A),
		),
		.ops = &hsw_power_well_ops,
		.irq_pipe_mask = BIT(PIPE_A),
		.has_fuses = true,
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("PW_B", &xelpd_pwdoms_pw_b,
				.hsw.idx = XELPD_PW_CTL_IDX_PW_B),
		),
		.ops = &hsw_power_well_ops,
		.irq_pipe_mask = BIT(PIPE_B),
		.has_fuses = true,
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("PW_C", &xelpd_pwdoms_pw_c,
				.hsw.idx = XELPD_PW_CTL_IDX_PW_C),
		),
		.ops = &hsw_power_well_ops,
		.irq_pipe_mask = BIT(PIPE_C),
		.has_fuses = true,
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("PW_D", &xelpd_pwdoms_pw_d,
				.hsw.idx = XELPD_PW_CTL_IDX_PW_D),
		),
		.ops = &hsw_power_well_ops,
		.irq_pipe_mask = BIT(PIPE_D),
		.has_fuses = true,
	}, {
		.instances = &I915_PW_INSTANCES(
			I915_PW("AUX_A", &icl_pwdoms_aux_a, .xelpdp.aux_ch = AUX_CH_A),
			I915_PW("AUX_B", &icl_pwdoms_aux_b, .xelpdp.aux_ch = AUX_CH_B),
			I915_PW("AUX_TC1", &xelpdp_pwdoms_aux_tc1, .xelpdp.aux_ch = AUX_CH_USBC1),
			I915_PW("AUX_TC2", &xelpdp_pwdoms_aux_tc2, .xelpdp.aux_ch = AUX_CH_USBC2),
			I915_PW("AUX_TC3", &xelpdp_pwdoms_aux_tc3, .xelpdp.aux_ch = AUX_CH_USBC3),
			I915_PW("AUX_TC4", &xelpdp_pwdoms_aux_tc4, .xelpdp.aux_ch = AUX_CH_USBC4),
		),
		.ops = &xelpdp_aux_power_well_ops,
	},
};

static const struct i915_power_well_desc_list xelpdp_power_wells[] = {
	I915_PW_DESCRIPTORS(i9xx_power_wells_always_on),
	I915_PW_DESCRIPTORS(icl_power_wells_pw_1),
	I915_PW_DESCRIPTORS(xelpdp_power_wells_main),
};

static void init_power_well_domains(const struct i915_power_well_instance *inst,
				    struct i915_power_well *power_well)
{
	int j;

	if (!inst->domain_list)
		return;

	if (inst->domain_list->count == 0) {
		bitmap_fill(power_well->domains.bits, POWER_DOMAIN_NUM);

		return;
	}

	for (j = 0; j < inst->domain_list->count; j++)
		set_bit(inst->domain_list->list[j], power_well->domains.bits);
}

#define for_each_power_well_instance_in_desc_list(_desc_list, _desc_count, _desc, _inst) \
	for ((_desc) = (_desc_list); (_desc) - (_desc_list) < (_desc_count); (_desc)++) \
		for ((_inst) = (_desc)->instances->list; \
		     (_inst) - (_desc)->instances->list < (_desc)->instances->count; \
		     (_inst)++)

#define for_each_power_well_instance(_desc_list, _desc_count, _descs, _desc, _inst) \
	for ((_descs) = (_desc_list); \
	     (_descs) - (_desc_list) < (_desc_count); \
	     (_descs)++) \
		for_each_power_well_instance_in_desc_list((_descs)->list, (_descs)->count, \
							  (_desc), (_inst))

static int
__set_power_wells(struct i915_power_domains *power_domains,
		  const struct i915_power_well_desc_list *power_well_descs,
		  int power_well_descs_sz)
{
	struct drm_i915_private *i915 = container_of(power_domains,
						     struct drm_i915_private,
						     display.power.domains);
	u64 power_well_ids = 0;
	const struct i915_power_well_desc_list *desc_list;
	const struct i915_power_well_desc *desc;
	const struct i915_power_well_instance *inst;
	int power_well_count = 0;
	int plt_idx = 0;

	for_each_power_well_instance(power_well_descs, power_well_descs_sz, desc_list, desc, inst)
		power_well_count++;

	power_domains->power_well_count = power_well_count;
	power_domains->power_wells =
				kcalloc(power_well_count,
					sizeof(*power_domains->power_wells),
					GFP_KERNEL);
	if (!power_domains->power_wells)
		return -ENOMEM;

	for_each_power_well_instance(power_well_descs, power_well_descs_sz, desc_list, desc, inst) {
		struct i915_power_well *pw = &power_domains->power_wells[plt_idx];
		enum i915_power_well_id id = inst->id;

		pw->desc = desc;
		drm_WARN_ON(&i915->drm,
			    overflows_type(inst - desc->instances->list, pw->instance_idx));
		pw->instance_idx = inst - desc->instances->list;

		init_power_well_domains(inst, pw);

		plt_idx++;

		if (id == DISP_PW_ID_NONE)
			continue;

		drm_WARN_ON(&i915->drm, id >= sizeof(power_well_ids) * 8);
		drm_WARN_ON(&i915->drm, power_well_ids & BIT_ULL(id));
		power_well_ids |= BIT_ULL(id);
	}

	return 0;
}

#define set_power_wells(power_domains, __power_well_descs) \
	__set_power_wells(power_domains, __power_well_descs, \
			  ARRAY_SIZE(__power_well_descs))

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
						     display.power.domains);
	/*
	 * The enabling order will be from lower to higher indexed wells,
	 * the disabling order is reversed.
	 */
	if (!HAS_DISPLAY(i915)) {
		power_domains->power_well_count = 0;
		return 0;
	}

	if (DISPLAY_VER(i915) >= 14)
		return set_power_wells(power_domains, xelpdp_power_wells);
	else if (DISPLAY_VER(i915) >= 13)
		return set_power_wells(power_domains, xelpd_power_wells);
	else if (IS_DG1(i915))
		return set_power_wells(power_domains, dg1_power_wells);
	else if (IS_ALDERLAKE_S(i915))
		return set_power_wells(power_domains, adls_power_wells);
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
		return set_power_wells(power_domains, i9xx_power_wells);
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
