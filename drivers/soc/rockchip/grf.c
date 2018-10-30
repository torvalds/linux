// SPDX-License-Identifier: GPL-2.0-only
/*
 * Rockchip Generic Register Files setup
 *
 * Copyright (c) 2016 Heiko Stuebner <heiko@sntech.de>
 */

#include <linux/bitfield.h>
#include <linux/err.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

struct rockchip_grf;

struct rockchip_grf_funcs {
	int (*reset)(struct rockchip_grf *grf);
};

struct rockchip_grf {
	struct regmap *regmap;
	const struct rockchip_grf_funcs *funcs;
};

static int rockchip_edp_phy_grf_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rockchip_grf *grf;
	int ret;

	grf = devm_kzalloc(dev, sizeof(*grf), GFP_KERNEL);
	if (!grf)
		return -ENOMEM;

	grf->funcs = of_device_get_match_data(dev);
	if (!grf->funcs)
		return -ENODEV;

	grf->regmap = syscon_node_to_regmap(dev->of_node);
	if (IS_ERR(grf->regmap)) {
		ret = PTR_ERR(grf->regmap);
		dev_err(dev, "failed to get grf: %d\n", ret);
		return ret;
	}

	ret = grf->funcs->reset(grf);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, grf);

	return 0;
}

static int __maybe_unused rockchip_edp_phy_grf_resume(struct device *dev)
{
	struct rockchip_grf *grf = dev_get_drvdata(dev);

	return grf->funcs->reset(grf);
}

static const struct dev_pm_ops rockchip_edp_phy_grf_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(NULL, rockchip_edp_phy_grf_resume)
};

static int rk3568_edp_phy_grf_reset(struct rockchip_grf *grf)
{
	u32 status;
	int ret;

	ret = regmap_read(grf->regmap, 0x0030, &status);
	if (ret < 0)
		return ret;

	if (!FIELD_GET(0x1, status)) {
		regmap_write(grf->regmap, 0x0028, 0x00070007);
		regmap_write(grf->regmap, 0x0000, 0x0ff10ff1);
	}

	return 0;
}

static const struct rockchip_grf_funcs rk3568_edp_phy_grf_funcs = {
	.reset = rk3568_edp_phy_grf_reset,
};

static const struct of_device_id rockchip_edp_phy_grf_match[] = {
	{
		.compatible = "rockchip,rk3568-edp-phy-grf",
		.data = &rk3568_edp_phy_grf_funcs,
	},
	{}
};
MODULE_DEVICE_TABLE(of, rockchip_edp_phy_grf_match);

static struct platform_driver rockchip_edp_phy_grf_driver = {
	.driver = {
		.name = "rockchip-edp-phy-grf",
		.of_match_table = rockchip_edp_phy_grf_match,
		.pm = &rockchip_edp_phy_grf_pm_ops,
	},
	.probe = rockchip_edp_phy_grf_probe,
};

#define HIWORD_UPDATE(val, mask, shift) \
		((val) << (shift) | (mask) << ((shift) + 16))

struct rockchip_grf_value {
	const char *desc;
	u32 reg;
	u32 val;
};

struct rockchip_grf_info {
	const struct rockchip_grf_value *values;
	int num_values;
};

#define PX30_GRF_SOC_CON5		0x414

static const struct rockchip_grf_value px30_defaults[] __initconst = {
	/*
	 * Postponing auto jtag/sdmmc switching by 5 seconds.
	 * The counter value is calculated based on 24MHz clock.
	 */
	{ "jtag switching delay", PX30_GRF_SOC_CON5, 0x7270E00},
};

static const struct rockchip_grf_info px30_grf __initconst = {
	.values = px30_defaults,
	.num_values = ARRAY_SIZE(px30_defaults),
};

#define RK3036_GRF_SOC_CON0		0x140

static const struct rockchip_grf_value rk3036_defaults[] __initconst = {
	/*
	 * Disable auto jtag/sdmmc switching that causes issues with the
	 * clock-framework and the mmc controllers making them unreliable.
	 */
	{ "jtag switching", RK3036_GRF_SOC_CON0, HIWORD_UPDATE(0, 1, 11) },
};

static const struct rockchip_grf_info rk3036_grf __initconst = {
	.values = rk3036_defaults,
	.num_values = ARRAY_SIZE(rk3036_defaults),
};

#define RK3128_GRF_SOC_CON0		0x140

static const struct rockchip_grf_value rk3128_defaults[] __initconst = {
	{ "jtag switching", RK3128_GRF_SOC_CON0, HIWORD_UPDATE(0, 1, 8) },
};

static const struct rockchip_grf_info rk3128_grf __initconst = {
	.values = rk3128_defaults,
	.num_values = ARRAY_SIZE(rk3128_defaults),
};

#define RK3228_GRF_SOC_CON6		0x418

static const struct rockchip_grf_value rk3228_defaults[] __initconst = {
	{ "jtag switching", RK3228_GRF_SOC_CON6, HIWORD_UPDATE(0, 1, 8) },
};

static const struct rockchip_grf_info rk3228_grf __initconst = {
	.values = rk3228_defaults,
	.num_values = ARRAY_SIZE(rk3228_defaults),
};

#define RK3288_GRF_SOC_CON0		0x244
#define RK3288_GRF_SOC_CON2		0x24c

static const struct rockchip_grf_value rk3288_defaults[] __initconst = {
	{ "jtag switching", RK3288_GRF_SOC_CON0, HIWORD_UPDATE(0, 1, 12) },
	{ "pwm select", RK3288_GRF_SOC_CON2, HIWORD_UPDATE(1, 1, 0) },
};

static const struct rockchip_grf_info rk3288_grf __initconst = {
	.values = rk3288_defaults,
	.num_values = ARRAY_SIZE(rk3288_defaults),
};

#define RK3328_GRF_SOC_CON4		0x410

static const struct rockchip_grf_value rk3328_defaults[] __initconst = {
	{ "jtag switching", RK3328_GRF_SOC_CON4, HIWORD_UPDATE(0, 1, 12) },
};

static const struct rockchip_grf_info rk3328_grf __initconst = {
	.values = rk3328_defaults,
	.num_values = ARRAY_SIZE(rk3328_defaults),
};

#define RK3308_GRF_SOC_CON3		0x30c
#define RK3308_GRF_SOC_CON13		0x608

static const struct rockchip_grf_value rk3308_defaults[] __initconst = {
	{ "uart dma mask", RK3308_GRF_SOC_CON3, HIWORD_UPDATE(0, 0x1f, 10) },
	{ "uart2 auto switching", RK3308_GRF_SOC_CON13, HIWORD_UPDATE(0, 0x1, 12) },
};

static const struct rockchip_grf_info rk3308_grf __initconst = {
	.values = rk3308_defaults,
	.num_values = ARRAY_SIZE(rk3308_defaults),
};

#define RK3368_GRF_SOC_CON15		0x43c

static const struct rockchip_grf_value rk3368_defaults[] __initconst = {
	{ "jtag switching", RK3368_GRF_SOC_CON15, HIWORD_UPDATE(0, 1, 13) },
};

static const struct rockchip_grf_info rk3368_grf __initconst = {
	.values = rk3368_defaults,
	.num_values = ARRAY_SIZE(rk3368_defaults),
};

#define RK3399_GRF_SOC_CON7		0xe21c

static const struct rockchip_grf_value rk3399_defaults[] __initconst = {
	{ "jtag switching", RK3399_GRF_SOC_CON7, HIWORD_UPDATE(0, 1, 12) },
};

static const struct rockchip_grf_info rk3399_grf __initconst = {
	.values = rk3399_defaults,
	.num_values = ARRAY_SIZE(rk3399_defaults),
};

#define RK3588_SYS_GRF_SOC_CON7		0x031c

static const struct rockchip_grf_value rk3588_sys_grf_defaults[] __initconst = {
	{ "Connect EDP hpd to IO", RK3588_SYS_GRF_SOC_CON7, HIWORD_UPDATE(0x3, 0x3, 14) },
};

static const struct rockchip_grf_info rk3588_sys_grf __initconst = {
	.values = rk3588_sys_grf_defaults,
	.num_values = ARRAY_SIZE(rk3588_sys_grf_defaults),
};

#define DELAY_ONE_SECOND		0x16E3600

#define RV1126_GRF1_SDDETFLT_CON	0x10254
#define RV1126_GRF1_UART2RX_LOW_CON	0x10258
#define RV1126_GRF1_IOFUNC_CON1		0x10264
#define RV1126_GRF1_IOFUNC_CON3		0x1026C
#define RV1126_JTAG_GROUP0		0x0      /* mux to sdmmc*/
#define RV1126_JTAG_GROUP1		0x1      /* mux to uart2 */
#define FORCE_JTAG_ENABLE		0x1
#define FORCE_JTAG_DISABLE		0x0

static const struct rockchip_grf_value rv1126_defaults[] __initconst = {
	{ "jtag group0 force", RV1126_GRF1_IOFUNC_CON3,
		HIWORD_UPDATE(FORCE_JTAG_DISABLE, 1, 4) },
	{ "jtag group1 force", RV1126_GRF1_IOFUNC_CON3,
		HIWORD_UPDATE(FORCE_JTAG_DISABLE, 1, 5) },
	{ "jtag group1 tms low delay", RV1126_GRF1_UART2RX_LOW_CON, DELAY_ONE_SECOND },
	{ "switch to jtag groupx", RV1126_GRF1_IOFUNC_CON1, HIWORD_UPDATE(RV1126_JTAG_GROUP0, 1, 15) },
	{ "jtag group0 switching delay", RV1126_GRF1_SDDETFLT_CON, DELAY_ONE_SECOND * 5 },
};

static const struct rockchip_grf_info rv1126_grf __initconst = {
	.values = rv1126_defaults,
	.num_values = ARRAY_SIZE(rv1126_defaults),
};

static const struct of_device_id rockchip_grf_dt_match[] __initconst = {
	{
		.compatible = "rockchip,px30-grf",
		.data = (void *)&px30_grf,
	}, {
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
		.compatible = "rockchip,rk3308-grf",
		.data = (void *)&rk3308_grf,
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
		.compatible = "rockchip,rk3588-sys-grf",
		.data = (void *)&rk3588_sys_grf,
	}, {
		.compatible = "rockchip,rv1126-grf",
		.data = (void *)&rv1126_grf,
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

	ret = platform_driver_register(&rockchip_edp_phy_grf_driver);
	if (ret)
		return ret;

	np = of_find_matching_node_and_match(NULL, rockchip_grf_dt_match,
					     &match);
	if (!np)
		return -ENODEV;
	if (!match || !match->data) {
		pr_err("%s: missing grf data\n", __func__);
		return -EINVAL;
	}

	grf_info = match->data;

	grf = syscon_node_to_regmap(np);
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

MODULE_DESCRIPTION("Rockchip GRF");
MODULE_LICENSE("GPL");
