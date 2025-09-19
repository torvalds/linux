// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung S5P/Exynos SoC series MIPI CSIS/DSIM DPHY driver
 *
 * Copyright (C) 2013,2016 Samsung Electronics Co., Ltd.
 * Author: Sylwester Nawrocki <s.nawrocki@samsung.com>
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/spinlock.h>
#include <linux/soc/samsung/exynos-regs-pmu.h>
#include <linux/mfd/syscon.h>

enum exynos_mipi_phy_id {
	EXYNOS_MIPI_PHY_ID_NONE = -1,
	EXYNOS_MIPI_PHY_ID_CSIS0,
	EXYNOS_MIPI_PHY_ID_DSIM0,
	EXYNOS_MIPI_PHY_ID_CSIS1,
	EXYNOS_MIPI_PHY_ID_DSIM1,
	EXYNOS_MIPI_PHY_ID_CSIS2,
	EXYNOS_MIPI_PHYS_NUM
};

enum exynos_mipi_phy_regmap_id {
	EXYNOS_MIPI_REGMAP_PMU,
	EXYNOS_MIPI_REGMAP_DISP,
	EXYNOS_MIPI_REGMAP_CAM0,
	EXYNOS_MIPI_REGMAP_CAM1,
	EXYNOS_MIPI_REGMAPS_NUM
};

struct mipi_phy_device_desc {
	int num_phys;
	int num_regmaps;
	const char *regmap_names[EXYNOS_MIPI_REGMAPS_NUM];
	struct exynos_mipi_phy_desc {
		enum exynos_mipi_phy_id	coupled_phy_id;
		u32 enable_val;
		unsigned int enable_reg;
		enum exynos_mipi_phy_regmap_id enable_map;
		u32 resetn_val;
		unsigned int resetn_reg;
		enum exynos_mipi_phy_regmap_id resetn_map;
	} phys[EXYNOS_MIPI_PHYS_NUM];
};

static const struct mipi_phy_device_desc s5pv210_mipi_phy = {
	.num_regmaps = 1,
	.regmap_names = {"syscon"},
	.num_phys = 4,
	.phys = {
		{
			/* EXYNOS_MIPI_PHY_ID_CSIS0 */
			.coupled_phy_id = EXYNOS_MIPI_PHY_ID_DSIM0,
			.enable_val = EXYNOS4_PHY_ENABLE,
			.enable_reg = EXYNOS4_MIPI_PHY_CONTROL(0),
			.enable_map = EXYNOS_MIPI_REGMAP_PMU,
			.resetn_val = EXYNOS4_MIPI_PHY_SRESETN,
			.resetn_reg = EXYNOS4_MIPI_PHY_CONTROL(0),
			.resetn_map = EXYNOS_MIPI_REGMAP_PMU,
		}, {
			/* EXYNOS_MIPI_PHY_ID_DSIM0 */
			.coupled_phy_id = EXYNOS_MIPI_PHY_ID_CSIS0,
			.enable_val = EXYNOS4_PHY_ENABLE,
			.enable_reg = EXYNOS4_MIPI_PHY_CONTROL(0),
			.enable_map = EXYNOS_MIPI_REGMAP_PMU,
			.resetn_val = EXYNOS4_MIPI_PHY_MRESETN,
			.resetn_reg = EXYNOS4_MIPI_PHY_CONTROL(0),
			.resetn_map = EXYNOS_MIPI_REGMAP_PMU,
		}, {
			/* EXYNOS_MIPI_PHY_ID_CSIS1 */
			.coupled_phy_id = EXYNOS_MIPI_PHY_ID_DSIM1,
			.enable_val = EXYNOS4_PHY_ENABLE,
			.enable_reg = EXYNOS4_MIPI_PHY_CONTROL(1),
			.enable_map = EXYNOS_MIPI_REGMAP_PMU,
			.resetn_val = EXYNOS4_MIPI_PHY_SRESETN,
			.resetn_reg = EXYNOS4_MIPI_PHY_CONTROL(1),
			.resetn_map = EXYNOS_MIPI_REGMAP_PMU,
		}, {
			/* EXYNOS_MIPI_PHY_ID_DSIM1 */
			.coupled_phy_id = EXYNOS_MIPI_PHY_ID_CSIS1,
			.enable_val = EXYNOS4_PHY_ENABLE,
			.enable_reg = EXYNOS4_MIPI_PHY_CONTROL(1),
			.enable_map = EXYNOS_MIPI_REGMAP_PMU,
			.resetn_val = EXYNOS4_MIPI_PHY_MRESETN,
			.resetn_reg = EXYNOS4_MIPI_PHY_CONTROL(1),
			.resetn_map = EXYNOS_MIPI_REGMAP_PMU,
		},
	},
};

static const struct mipi_phy_device_desc exynos5420_mipi_phy = {
	.num_regmaps = 1,
	.regmap_names = {"syscon"},
	.num_phys = 5,
	.phys = {
		{
			/* EXYNOS_MIPI_PHY_ID_CSIS0 */
			.coupled_phy_id = EXYNOS_MIPI_PHY_ID_DSIM0,
			.enable_val = EXYNOS4_PHY_ENABLE,
			.enable_reg = EXYNOS5420_MIPI_PHY_CONTROL(0),
			.enable_map = EXYNOS_MIPI_REGMAP_PMU,
			.resetn_val = EXYNOS4_MIPI_PHY_SRESETN,
			.resetn_reg = EXYNOS5420_MIPI_PHY_CONTROL(0),
			.resetn_map = EXYNOS_MIPI_REGMAP_PMU,
		}, {
			/* EXYNOS_MIPI_PHY_ID_DSIM0 */
			.coupled_phy_id = EXYNOS_MIPI_PHY_ID_CSIS0,
			.enable_val = EXYNOS4_PHY_ENABLE,
			.enable_reg = EXYNOS5420_MIPI_PHY_CONTROL(0),
			.enable_map = EXYNOS_MIPI_REGMAP_PMU,
			.resetn_val = EXYNOS4_MIPI_PHY_MRESETN,
			.resetn_reg = EXYNOS5420_MIPI_PHY_CONTROL(0),
			.resetn_map = EXYNOS_MIPI_REGMAP_PMU,
		}, {
			/* EXYNOS_MIPI_PHY_ID_CSIS1 */
			.coupled_phy_id = EXYNOS_MIPI_PHY_ID_DSIM1,
			.enable_val = EXYNOS4_PHY_ENABLE,
			.enable_reg = EXYNOS5420_MIPI_PHY_CONTROL(1),
			.enable_map = EXYNOS_MIPI_REGMAP_PMU,
			.resetn_val = EXYNOS4_MIPI_PHY_SRESETN,
			.resetn_reg = EXYNOS5420_MIPI_PHY_CONTROL(1),
			.resetn_map = EXYNOS_MIPI_REGMAP_PMU,
		}, {
			/* EXYNOS_MIPI_PHY_ID_DSIM1 */
			.coupled_phy_id = EXYNOS_MIPI_PHY_ID_CSIS1,
			.enable_val = EXYNOS4_PHY_ENABLE,
			.enable_reg = EXYNOS5420_MIPI_PHY_CONTROL(1),
			.enable_map = EXYNOS_MIPI_REGMAP_PMU,
			.resetn_val = EXYNOS4_MIPI_PHY_MRESETN,
			.resetn_reg = EXYNOS5420_MIPI_PHY_CONTROL(1),
			.resetn_map = EXYNOS_MIPI_REGMAP_PMU,
		}, {
			/* EXYNOS_MIPI_PHY_ID_CSIS2 */
			.coupled_phy_id = EXYNOS_MIPI_PHY_ID_NONE,
			.enable_val = EXYNOS4_PHY_ENABLE,
			.enable_reg = EXYNOS5420_MIPI_PHY_CONTROL(2),
			.enable_map = EXYNOS_MIPI_REGMAP_PMU,
			.resetn_val = EXYNOS4_MIPI_PHY_SRESETN,
			.resetn_reg = EXYNOS5420_MIPI_PHY_CONTROL(2),
			.resetn_map = EXYNOS_MIPI_REGMAP_PMU,
		},
	},
};

#define EXYNOS5433_SYSREG_DISP_MIPI_PHY		0x100C
#define EXYNOS5433_SYSREG_CAM0_MIPI_DPHY_CON	0x1014
#define EXYNOS5433_SYSREG_CAM1_MIPI_DPHY_CON	0x1020

static const struct mipi_phy_device_desc exynos5433_mipi_phy = {
	.num_regmaps = 4,
	.regmap_names = {
		"samsung,pmu-syscon",
		"samsung,disp-sysreg",
		"samsung,cam0-sysreg",
		"samsung,cam1-sysreg"
	},
	.num_phys = 5,
	.phys = {
		{
			/* EXYNOS_MIPI_PHY_ID_CSIS0 */
			.coupled_phy_id = EXYNOS_MIPI_PHY_ID_DSIM0,
			.enable_val = EXYNOS4_PHY_ENABLE,
			.enable_reg = EXYNOS4_MIPI_PHY_CONTROL(0),
			.enable_map = EXYNOS_MIPI_REGMAP_PMU,
			.resetn_val = BIT(0),
			.resetn_reg = EXYNOS5433_SYSREG_CAM0_MIPI_DPHY_CON,
			.resetn_map = EXYNOS_MIPI_REGMAP_CAM0,
		}, {
			/* EXYNOS_MIPI_PHY_ID_DSIM0 */
			.coupled_phy_id = EXYNOS_MIPI_PHY_ID_CSIS0,
			.enable_val = EXYNOS4_PHY_ENABLE,
			.enable_reg = EXYNOS4_MIPI_PHY_CONTROL(0),
			.enable_map = EXYNOS_MIPI_REGMAP_PMU,
			.resetn_val = BIT(0),
			.resetn_reg = EXYNOS5433_SYSREG_DISP_MIPI_PHY,
			.resetn_map = EXYNOS_MIPI_REGMAP_DISP,
		}, {
			/* EXYNOS_MIPI_PHY_ID_CSIS1 */
			.coupled_phy_id = EXYNOS_MIPI_PHY_ID_NONE,
			.enable_val = EXYNOS4_PHY_ENABLE,
			.enable_reg = EXYNOS4_MIPI_PHY_CONTROL(1),
			.enable_map = EXYNOS_MIPI_REGMAP_PMU,
			.resetn_val = BIT(1),
			.resetn_reg = EXYNOS5433_SYSREG_CAM0_MIPI_DPHY_CON,
			.resetn_map = EXYNOS_MIPI_REGMAP_CAM0,
		}, {
			/* EXYNOS_MIPI_PHY_ID_DSIM1 */
			.coupled_phy_id = EXYNOS_MIPI_PHY_ID_NONE,
			.enable_val = EXYNOS4_PHY_ENABLE,
			.enable_reg = EXYNOS4_MIPI_PHY_CONTROL(1),
			.enable_map = EXYNOS_MIPI_REGMAP_PMU,
			.resetn_val = BIT(1),
			.resetn_reg = EXYNOS5433_SYSREG_DISP_MIPI_PHY,
			.resetn_map = EXYNOS_MIPI_REGMAP_DISP,
		}, {
			/* EXYNOS_MIPI_PHY_ID_CSIS2 */
			.coupled_phy_id = EXYNOS_MIPI_PHY_ID_NONE,
			.enable_val = EXYNOS4_PHY_ENABLE,
			.enable_reg = EXYNOS4_MIPI_PHY_CONTROL(2),
			.enable_map = EXYNOS_MIPI_REGMAP_PMU,
			.resetn_val = BIT(0),
			.resetn_reg = EXYNOS5433_SYSREG_CAM1_MIPI_DPHY_CON,
			.resetn_map = EXYNOS_MIPI_REGMAP_CAM1,
		},
	},
};

static const struct mipi_phy_device_desc exynos7870_mipi_phy = {
	.num_regmaps = 3,
	.regmap_names = {
		"samsung,pmu-syscon",
		"samsung,disp-sysreg",
		"samsung,cam0-sysreg"
	},
	.num_phys = 4,
	.phys = {
		{
			/* EXYNOS_MIPI_PHY_ID_CSIS0 */
			.coupled_phy_id = EXYNOS_MIPI_PHY_ID_DSIM0,
			.enable_val = EXYNOS4_PHY_ENABLE,
			.enable_reg = EXYNOS7870_MIPI_PHY_CONTROL0,
			.enable_map = EXYNOS_MIPI_REGMAP_PMU,
			.resetn_val = BIT(0),
			.resetn_reg = 0,
			.resetn_map = EXYNOS_MIPI_REGMAP_CAM0,
		}, {
			/* EXYNOS_MIPI_PHY_ID_DSIM0 */
			.coupled_phy_id = EXYNOS_MIPI_PHY_ID_CSIS0,
			.enable_val = EXYNOS4_PHY_ENABLE,
			.enable_reg = EXYNOS7870_MIPI_PHY_CONTROL0,
			.enable_map = EXYNOS_MIPI_REGMAP_PMU,
			.resetn_val = BIT(0),
			.resetn_reg = 0,
			.resetn_map = EXYNOS_MIPI_REGMAP_DISP,
		}, {
			/* EXYNOS_MIPI_PHY_ID_CSIS1 */
			.coupled_phy_id = EXYNOS_MIPI_PHY_ID_NONE,
			.enable_val = EXYNOS4_PHY_ENABLE,
			.enable_reg = EXYNOS7870_MIPI_PHY_CONTROL1,
			.enable_map = EXYNOS_MIPI_REGMAP_PMU,
			.resetn_val = BIT(1),
			.resetn_reg = 0,
			.resetn_map = EXYNOS_MIPI_REGMAP_CAM0,
		}, {
			/* EXYNOS_MIPI_PHY_ID_CSIS2 */
			.coupled_phy_id = EXYNOS_MIPI_PHY_ID_NONE,
			.enable_val = EXYNOS4_PHY_ENABLE,
			.enable_reg = EXYNOS7870_MIPI_PHY_CONTROL2,
			.enable_map = EXYNOS_MIPI_REGMAP_PMU,
			.resetn_val = BIT(2),
			.resetn_reg = 0,
			.resetn_map = EXYNOS_MIPI_REGMAP_CAM0,
		},
	},
};

struct exynos_mipi_video_phy {
	struct regmap *regmaps[EXYNOS_MIPI_REGMAPS_NUM];
	int num_phys;
	struct video_phy_desc {
		struct phy *phy;
		unsigned int index;
		const struct exynos_mipi_phy_desc *data;
	} phys[EXYNOS_MIPI_PHYS_NUM];
	spinlock_t slock;
};

static int __set_phy_state(const struct exynos_mipi_phy_desc *data,
			   struct exynos_mipi_video_phy *state, unsigned int on)
{
	struct regmap *enable_map = state->regmaps[data->enable_map];
	struct regmap *resetn_map = state->regmaps[data->resetn_map];

	spin_lock(&state->slock);

	/* disable in PMU sysreg */
	if (!on && data->coupled_phy_id >= 0 &&
	    state->phys[data->coupled_phy_id].phy->power_count == 0)
		regmap_update_bits(enable_map, data->enable_reg,
				   data->enable_val, 0);
	/* PHY reset */
	if (on)
		regmap_update_bits(resetn_map, data->resetn_reg,
				   data->resetn_val, data->resetn_val);
	else
		regmap_update_bits(resetn_map, data->resetn_reg,
				   data->resetn_val, 0);
	/* enable in PMU sysreg */
	if (on)
		regmap_update_bits(enable_map, data->enable_reg,
				   data->enable_val, data->enable_val);

	spin_unlock(&state->slock);

	return 0;
}

#define to_mipi_video_phy(desc) \
	container_of((desc), struct exynos_mipi_video_phy, phys[(desc)->index])

static int exynos_mipi_video_phy_power_on(struct phy *phy)
{
	struct video_phy_desc *phy_desc = phy_get_drvdata(phy);
	struct exynos_mipi_video_phy *state = to_mipi_video_phy(phy_desc);

	return __set_phy_state(phy_desc->data, state, 1);
}

static int exynos_mipi_video_phy_power_off(struct phy *phy)
{
	struct video_phy_desc *phy_desc = phy_get_drvdata(phy);
	struct exynos_mipi_video_phy *state = to_mipi_video_phy(phy_desc);

	return __set_phy_state(phy_desc->data, state, 0);
}

static struct phy *exynos_mipi_video_phy_xlate(struct device *dev,
					const struct of_phandle_args *args)
{
	struct exynos_mipi_video_phy *state = dev_get_drvdata(dev);

	if (WARN_ON(args->args[0] >= state->num_phys))
		return ERR_PTR(-ENODEV);

	return state->phys[args->args[0]].phy;
}

static const struct phy_ops exynos_mipi_video_phy_ops = {
	.power_on	= exynos_mipi_video_phy_power_on,
	.power_off	= exynos_mipi_video_phy_power_off,
	.owner		= THIS_MODULE,
};

static int exynos_mipi_video_phy_probe(struct platform_device *pdev)
{
	const struct mipi_phy_device_desc *phy_dev;
	struct exynos_mipi_video_phy *state;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct phy_provider *phy_provider;
	unsigned int i = 0;

	phy_dev = of_device_get_match_data(dev);
	if (!phy_dev)
		return -ENODEV;

	state = devm_kzalloc(dev, sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	state->regmaps[i] = syscon_node_to_regmap(dev->parent->of_node);
	if (!IS_ERR(state->regmaps[i]))
		i++;
	for (; i < phy_dev->num_regmaps; i++) {
		state->regmaps[i] = syscon_regmap_lookup_by_phandle(np,
						phy_dev->regmap_names[i]);
		if (IS_ERR(state->regmaps[i]))
			return PTR_ERR(state->regmaps[i]);
	}
	state->num_phys = phy_dev->num_phys;
	spin_lock_init(&state->slock);

	dev_set_drvdata(dev, state);

	for (i = 0; i < state->num_phys; i++) {
		struct phy *phy = devm_phy_create(dev, NULL,
						  &exynos_mipi_video_phy_ops);
		if (IS_ERR(phy)) {
			dev_err(dev, "failed to create PHY %d\n", i);
			return PTR_ERR(phy);
		}

		state->phys[i].phy = phy;
		state->phys[i].index = i;
		state->phys[i].data = &phy_dev->phys[i];
		phy_set_drvdata(phy, &state->phys[i]);
	}

	phy_provider = devm_of_phy_provider_register(dev,
					exynos_mipi_video_phy_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct of_device_id exynos_mipi_video_phy_of_match[] = {
	{
		.compatible = "samsung,s5pv210-mipi-video-phy",
		.data = &s5pv210_mipi_phy,
	}, {
		.compatible = "samsung,exynos5420-mipi-video-phy",
		.data = &exynos5420_mipi_phy,
	}, {
		.compatible = "samsung,exynos5433-mipi-video-phy",
		.data = &exynos5433_mipi_phy,
	}, {
		.compatible = "samsung,exynos7870-mipi-video-phy",
		.data = &exynos7870_mipi_phy,
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, exynos_mipi_video_phy_of_match);

static struct platform_driver exynos_mipi_video_phy_driver = {
	.probe	= exynos_mipi_video_phy_probe,
	.driver = {
		.of_match_table	= exynos_mipi_video_phy_of_match,
		.name  = "exynos-mipi-video-phy",
		.suppress_bind_attrs = true,
	}
};
module_platform_driver(exynos_mipi_video_phy_driver);

MODULE_DESCRIPTION("Samsung S5P/Exynos SoC MIPI CSI-2/DSI PHY driver");
MODULE_AUTHOR("Sylwester Nawrocki <s.nawrocki@samsung.com>");
MODULE_LICENSE("GPL v2");
