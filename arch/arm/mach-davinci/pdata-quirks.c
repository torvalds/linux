// SPDX-License-Identifier: GPL-2.0-only
/*
 * Legacy platform_data quirks
 *
 * Copyright (C) 2016 BayLibre, Inc
 */
#include <linux/kernel.h>
#include <linux/of_platform.h>

#include <media/i2c/tvp514x.h>
#include <media/i2c/adv7343.h>

#include "common.h"
#include "da8xx.h"

struct pdata_init {
	const char *compatible;
	void (*fn)(void);
};

#define TVP5147_CH0		"tvp514x-0"
#define TVP5147_CH1		"tvp514x-1"

/* VPIF capture configuration */
static struct tvp514x_platform_data tvp5146_pdata = {
		.clk_polarity = 0,
		.hs_polarity  = 1,
		.vs_polarity  = 1,
};

#define TVP514X_STD_ALL (V4L2_STD_NTSC | V4L2_STD_PAL)

static struct vpif_input da850_ch0_inputs[] = {
	{
		.input = {
			.index = 0,
			.name  = "Composite",
			.type  = V4L2_INPUT_TYPE_CAMERA,
			.capabilities = V4L2_IN_CAP_STD,
			.std   = TVP514X_STD_ALL,
		},
		.input_route = INPUT_CVBS_VI2B,
		.output_route = OUTPUT_10BIT_422_EMBEDDED_SYNC,
		.subdev_name = TVP5147_CH0,
	},
};

static struct vpif_input da850_ch1_inputs[] = {
	{
		.input = {
			.index = 0,
			.name  = "S-Video",
			.type  = V4L2_INPUT_TYPE_CAMERA,
			.capabilities = V4L2_IN_CAP_STD,
			.std   = TVP514X_STD_ALL,
		},
		.input_route = INPUT_SVIDEO_VI2C_VI1C,
		.output_route = OUTPUT_10BIT_422_EMBEDDED_SYNC,
		.subdev_name = TVP5147_CH1,
	},
};

static struct vpif_subdev_info da850_vpif_capture_sdev_info[] = {
	{
		.name = TVP5147_CH0,
		.board_info = {
			I2C_BOARD_INFO("tvp5146", 0x5d),
			.platform_data = &tvp5146_pdata,
		},
	},
	{
		.name = TVP5147_CH1,
		.board_info = {
			I2C_BOARD_INFO("tvp5146", 0x5c),
			.platform_data = &tvp5146_pdata,
		},
	},
};

static struct vpif_capture_config da850_vpif_capture_config = {
	.subdev_info = da850_vpif_capture_sdev_info,
	.subdev_count = ARRAY_SIZE(da850_vpif_capture_sdev_info),
	.chan_config[0] = {
		.inputs = da850_ch0_inputs,
		.input_count = ARRAY_SIZE(da850_ch0_inputs),
		.vpif_if = {
			.if_type = VPIF_IF_BT656,
			.hd_pol  = 1,
			.vd_pol  = 1,
			.fid_pol = 0,
		},
	},
	.chan_config[1] = {
		.inputs = da850_ch1_inputs,
		.input_count = ARRAY_SIZE(da850_ch1_inputs),
		.vpif_if = {
			.if_type = VPIF_IF_BT656,
			.hd_pol  = 1,
			.vd_pol  = 1,
			.fid_pol = 0,
		},
	},
	.card_name = "DA850/OMAP-L138 Video Capture",
};

static void __init da850_vpif_legacy_register_capture(void)
{
	int ret;

	ret = da850_register_vpif_capture(&da850_vpif_capture_config);
	if (ret)
		pr_warn("%s: VPIF capture setup failed: %d\n",
			__func__, ret);
}

static void __init da850_vpif_capture_legacy_init_lcdk(void)
{
	da850_vpif_capture_config.subdev_count = 1;
	da850_vpif_legacy_register_capture();
}

static void __init da850_vpif_capture_legacy_init_evm(void)
{
	da850_vpif_legacy_register_capture();
}

static struct adv7343_platform_data adv7343_pdata = {
	.mode_config = {
		.dac = { 1, 1, 1 },
	},
	.sd_config = {
		.sd_dac_out = { 1 },
	},
};

static struct vpif_subdev_info da850_vpif_subdev[] = {
	{
		.name = "adv7343",
		.board_info = {
			I2C_BOARD_INFO("adv7343", 0x2a),
			.platform_data = &adv7343_pdata,
		},
	},
};

static const struct vpif_output da850_ch0_outputs[] = {
	{
		.output = {
			.index = 0,
			.name = "Composite",
			.type = V4L2_OUTPUT_TYPE_ANALOG,
			.capabilities = V4L2_OUT_CAP_STD,
			.std = V4L2_STD_ALL,
		},
		.subdev_name = "adv7343",
		.output_route = ADV7343_COMPOSITE_ID,
	},
	{
		.output = {
			.index = 1,
			.name = "S-Video",
			.type = V4L2_OUTPUT_TYPE_ANALOG,
			.capabilities = V4L2_OUT_CAP_STD,
			.std = V4L2_STD_ALL,
		},
		.subdev_name = "adv7343",
		.output_route = ADV7343_SVIDEO_ID,
	},
};

static struct vpif_display_config da850_vpif_display_config = {
	.subdevinfo   = da850_vpif_subdev,
	.subdev_count = ARRAY_SIZE(da850_vpif_subdev),
	.chan_config[0] = {
		.outputs = da850_ch0_outputs,
		.output_count = ARRAY_SIZE(da850_ch0_outputs),
	},
	.card_name    = "DA850/OMAP-L138 Video Display",
};

static void __init da850_vpif_display_legacy_init_evm(void)
{
	int ret;

	ret = da850_register_vpif_display(&da850_vpif_display_config);
	if (ret)
		pr_warn("%s: VPIF display setup failed: %d\n",
			__func__, ret);
}

static void pdata_quirks_check(struct pdata_init *quirks)
{
	while (quirks->compatible) {
		if (of_machine_is_compatible(quirks->compatible)) {
			if (quirks->fn)
				quirks->fn();
		}
		quirks++;
	}
}

static struct pdata_init pdata_quirks[] __initdata = {
	{ "ti,da850-lcdk", da850_vpif_capture_legacy_init_lcdk, },
	{ "ti,da850-evm", da850_vpif_display_legacy_init_evm, },
	{ "ti,da850-evm", da850_vpif_capture_legacy_init_evm, },
	{ /* sentinel */ },
};

void __init pdata_quirks_init(void)
{
	pdata_quirks_check(pdata_quirks);
}
