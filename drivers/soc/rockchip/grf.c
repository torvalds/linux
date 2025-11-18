// SPDX-License-Identifier: GPL-2.0-only
/*
 * Rockchip Generic Register Files setup
 *
 * Copyright (c) 2016 Heiko Stuebner <heiko@sntech.de>
 */

#include <linux/err.h>
#include <linux/hw_bitfield.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>


struct rockchip_grf_value {
	const char *desc;
	u32 reg;
	u32 val;
};

struct rockchip_grf_info {
	const struct rockchip_grf_value *values;
	int num_values;
};

#define RK3036_GRF_SOC_CON0		0x140

static const struct rockchip_grf_value rk3036_defaults[] __initconst = {
	/*
	 * Disable auto jtag/sdmmc switching that causes issues with the
	 * clock-framework and the mmc controllers making them unreliable.
	 */
	{ "jtag switching", RK3036_GRF_SOC_CON0, FIELD_PREP_WM16_CONST(BIT(11), 0) },
};

static const struct rockchip_grf_info rk3036_grf __initconst = {
	.values = rk3036_defaults,
	.num_values = ARRAY_SIZE(rk3036_defaults),
};

#define RK3128_GRF_SOC_CON0		0x140
#define RK3128_GRF_SOC_CON1		0x144

static const struct rockchip_grf_value rk3128_defaults[] __initconst = {
	{ "jtag switching", RK3128_GRF_SOC_CON0, FIELD_PREP_WM16_CONST(BIT(8), 0) },
	{ "vpu main clock", RK3128_GRF_SOC_CON1, FIELD_PREP_WM16_CONST(BIT(10), 0) },
};

static const struct rockchip_grf_info rk3128_grf __initconst = {
	.values = rk3128_defaults,
	.num_values = ARRAY_SIZE(rk3128_defaults),
};

#define RK3228_GRF_SOC_CON6		0x418

static const struct rockchip_grf_value rk3228_defaults[] __initconst = {
	{ "jtag switching", RK3228_GRF_SOC_CON6, FIELD_PREP_WM16_CONST(BIT(8), 0) },
};

static const struct rockchip_grf_info rk3228_grf __initconst = {
	.values = rk3228_defaults,
	.num_values = ARRAY_SIZE(rk3228_defaults),
};

#define RK3288_GRF_SOC_CON0		0x244
#define RK3288_GRF_SOC_CON2		0x24c

static const struct rockchip_grf_value rk3288_defaults[] __initconst = {
	{ "jtag switching", RK3288_GRF_SOC_CON0, FIELD_PREP_WM16_CONST(BIT(12), 0) },
	{ "pwm select", RK3288_GRF_SOC_CON2, FIELD_PREP_WM16_CONST(BIT(0), 1) },
};

static const struct rockchip_grf_info rk3288_grf __initconst = {
	.values = rk3288_defaults,
	.num_values = ARRAY_SIZE(rk3288_defaults),
};

#define RK3328_GRF_SOC_CON4		0x410

static const struct rockchip_grf_value rk3328_defaults[] __initconst = {
	{ "jtag switching", RK3328_GRF_SOC_CON4, FIELD_PREP_WM16_CONST(BIT(12), 0) },
};

static const struct rockchip_grf_info rk3328_grf __initconst = {
	.values = rk3328_defaults,
	.num_values = ARRAY_SIZE(rk3328_defaults),
};

#define RK3368_GRF_SOC_CON15		0x43c

static const struct rockchip_grf_value rk3368_defaults[] __initconst = {
	{ "jtag switching", RK3368_GRF_SOC_CON15, FIELD_PREP_WM16_CONST(BIT(13), 0) },
};

static const struct rockchip_grf_info rk3368_grf __initconst = {
	.values = rk3368_defaults,
	.num_values = ARRAY_SIZE(rk3368_defaults),
};

#define RK3399_GRF_SOC_CON7		0xe21c

static const struct rockchip_grf_value rk3399_defaults[] __initconst = {
	{ "jtag switching", RK3399_GRF_SOC_CON7, FIELD_PREP_WM16_CONST(BIT(12), 0) },
};

static const struct rockchip_grf_info rk3399_grf __initconst = {
	.values = rk3399_defaults,
	.num_values = ARRAY_SIZE(rk3399_defaults),
};

#define RK3566_GRF_USB3OTG0_CON1	0x0104

static const struct rockchip_grf_value rk3566_defaults[] __initconst = {
	{ "usb3otg port switch", RK3566_GRF_USB3OTG0_CON1, FIELD_PREP_WM16_CONST(BIT(12), 0) },
	{ "usb3otg clock switch", RK3566_GRF_USB3OTG0_CON1, FIELD_PREP_WM16_CONST(BIT(7), 1) },
	{ "usb3otg disable usb3", RK3566_GRF_USB3OTG0_CON1, FIELD_PREP_WM16_CONST(BIT(0), 1) },
};

static const struct rockchip_grf_info rk3566_pipegrf __initconst = {
	.values = rk3566_defaults,
	.num_values = ARRAY_SIZE(rk3566_defaults),
};

#define RK3576_SYSGRF_SOC_CON1		0x0004

static const struct rockchip_grf_value rk3576_defaults_sys_grf[] __initconst = {
	{ "i3c0 weakpull", RK3576_SYSGRF_SOC_CON1, FIELD_PREP_WM16_CONST(GENMASK(7, 6), 3) },
	{ "i3c1 weakpull", RK3576_SYSGRF_SOC_CON1, FIELD_PREP_WM16_CONST(GENMASK(9, 8), 3) },
};

static const struct rockchip_grf_info rk3576_sysgrf __initconst = {
	.values = rk3576_defaults_sys_grf,
	.num_values = ARRAY_SIZE(rk3576_defaults_sys_grf),
};

#define RK3576_IOCGRF_MISC_CON		0x04F0

static const struct rockchip_grf_value rk3576_defaults_ioc_grf[] __initconst = {
	{ "jtag switching", RK3576_IOCGRF_MISC_CON, FIELD_PREP_WM16_CONST(BIT(1), 0) },
};

static const struct rockchip_grf_info rk3576_iocgrf __initconst = {
	.values = rk3576_defaults_ioc_grf,
	.num_values = ARRAY_SIZE(rk3576_defaults_ioc_grf),
};

#define RK3588_GRF_SOC_CON6		0x0318

static const struct rockchip_grf_value rk3588_defaults[] __initconst = {
	{ "jtag switching", RK3588_GRF_SOC_CON6, FIELD_PREP_WM16_CONST(BIT(14), 0) },
};

static const struct rockchip_grf_info rk3588_sysgrf __initconst = {
	.values = rk3588_defaults,
	.num_values = ARRAY_SIZE(rk3588_defaults),
};

static const struct of_device_id rockchip_grf_dt_match[] __initconst = {
	{
		.compatible = "rockchip,rk3036-grf",
		.data = (void *)&rk3036_grf,
	}, {
		.compatible = "rockchip,rk3128-grf",
		.data = (void *)&rk3128_grf,
	}, {
		.compatible = "rockchip,rk3228-grf",
		.data = (void *)&rk3228_grf,
	}, {
		.compatible = "rockchip,rk3288-grf",
		.data = (void *)&rk3288_grf,
	}, {
		.compatible = "rockchip,rk3328-grf",
		.data = (void *)&rk3328_grf,
	}, {
		.compatible = "rockchip,rk3368-grf",
		.data = (void *)&rk3368_grf,
	}, {
		.compatible = "rockchip,rk3399-grf",
		.data = (void *)&rk3399_grf,
	}, {
		.compatible = "rockchip,rk3566-pipe-grf",
		.data = (void *)&rk3566_pipegrf,
	}, {
		.compatible = "rockchip,rk3576-sys-grf",
		.data = (void *)&rk3576_sysgrf,
	}, {
		.compatible = "rockchip,rk3576-ioc-grf",
		.data = (void *)&rk3576_iocgrf,
	}, {
		.compatible = "rockchip,rk3588-sys-grf",
		.data = (void *)&rk3588_sysgrf,
	},
	{ /* sentinel */ },
};

static int __init rockchip_grf_init(void)
{
	const struct rockchip_grf_info *grf_info;
	const struct of_device_id *match;
	struct device_node *np;
	struct regmap *grf;
	int ret, i;

	np = of_find_matching_node_and_match(NULL, rockchip_grf_dt_match,
					     &match);
	if (!np)
		return -ENODEV;
	if (!match || !match->data) {
		pr_err("%s: missing grf data\n", __func__);
		of_node_put(np);
		return -EINVAL;
	}

	grf_info = match->data;

	grf = syscon_node_to_regmap(np);
	of_node_put(np);
	if (IS_ERR(grf)) {
		pr_err("%s: could not get grf syscon\n", __func__);
		return PTR_ERR(grf);
	}

	for (i = 0; i < grf_info->num_values; i++) {
		const struct rockchip_grf_value *val = &grf_info->values[i];

		pr_debug("%s: adjusting %s in %#6x to %#10x\n", __func__,
			val->desc, val->reg, val->val);
		ret = regmap_write(grf, val->reg, val->val);
		if (ret < 0)
			pr_err("%s: write to %#6x failed with %d\n",
			       __func__, val->reg, ret);
	}

	return 0;
}
postcore_initcall(rockchip_grf_init);
