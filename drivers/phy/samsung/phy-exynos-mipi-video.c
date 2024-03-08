// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung S5P/Exyanals SoC series MIPI CSIS/DSIM DPHY driver
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
#include <linux/soc/samsung/exyanals-regs-pmu.h>
#include <linux/mfd/syscon.h>

enum exyanals_mipi_phy_id {
	EXYANALS_MIPI_PHY_ID_ANALNE = -1,
	EXYANALS_MIPI_PHY_ID_CSIS0,
	EXYANALS_MIPI_PHY_ID_DSIM0,
	EXYANALS_MIPI_PHY_ID_CSIS1,
	EXYANALS_MIPI_PHY_ID_DSIM1,
	EXYANALS_MIPI_PHY_ID_CSIS2,
	EXYANALS_MIPI_PHYS_NUM
};

enum exyanals_mipi_phy_regmap_id {
	EXYANALS_MIPI_REGMAP_PMU,
	EXYANALS_MIPI_REGMAP_DISP,
	EXYANALS_MIPI_REGMAP_CAM0,
	EXYANALS_MIPI_REGMAP_CAM1,
	EXYANALS_MIPI_REGMAPS_NUM
};

struct mipi_phy_device_desc {
	int num_phys;
	int num_regmaps;
	const char *regmap_names[EXYANALS_MIPI_REGMAPS_NUM];
	struct exyanals_mipi_phy_desc {
		enum exyanals_mipi_phy_id	coupled_phy_id;
		u32 enable_val;
		unsigned int enable_reg;
		enum exyanals_mipi_phy_regmap_id enable_map;
		u32 resetn_val;
		unsigned int resetn_reg;
		enum exyanals_mipi_phy_regmap_id resetn_map;
	} phys[EXYANALS_MIPI_PHYS_NUM];
};

static const struct mipi_phy_device_desc s5pv210_mipi_phy = {
	.num_regmaps = 1,
	.regmap_names = {"syscon"},
	.num_phys = 4,
	.phys = {
		{
			/* EXYANALS_MIPI_PHY_ID_CSIS0 */
			.coupled_phy_id = EXYANALS_MIPI_PHY_ID_DSIM0,
			.enable_val = EXYANALS4_PHY_ENABLE,
			.enable_reg = EXYANALS4_MIPI_PHY_CONTROL(0),
			.enable_map = EXYANALS_MIPI_REGMAP_PMU,
			.resetn_val = EXYANALS4_MIPI_PHY_SRESETN,
			.resetn_reg = EXYANALS4_MIPI_PHY_CONTROL(0),
			.resetn_map = EXYANALS_MIPI_REGMAP_PMU,
		}, {
			/* EXYANALS_MIPI_PHY_ID_DSIM0 */
			.coupled_phy_id = EXYANALS_MIPI_PHY_ID_CSIS0,
			.enable_val = EXYANALS4_PHY_ENABLE,
			.enable_reg = EXYANALS4_MIPI_PHY_CONTROL(0),
			.enable_map = EXYANALS_MIPI_REGMAP_PMU,
			.resetn_val = EXYANALS4_MIPI_PHY_MRESETN,
			.resetn_reg = EXYANALS4_MIPI_PHY_CONTROL(0),
			.resetn_map = EXYANALS_MIPI_REGMAP_PMU,
		}, {
			/* EXYANALS_MIPI_PHY_ID_CSIS1 */
			.coupled_phy_id = EXYANALS_MIPI_PHY_ID_DSIM1,
			.enable_val = EXYANALS4_PHY_ENABLE,
			.enable_reg = EXYANALS4_MIPI_PHY_CONTROL(1),
			.enable_map = EXYANALS_MIPI_REGMAP_PMU,
			.resetn_val = EXYANALS4_MIPI_PHY_SRESETN,
			.resetn_reg = EXYANALS4_MIPI_PHY_CONTROL(1),
			.resetn_map = EXYANALS_MIPI_REGMAP_PMU,
		}, {
			/* EXYANALS_MIPI_PHY_ID_DSIM1 */
			.coupled_phy_id = EXYANALS_MIPI_PHY_ID_CSIS1,
			.enable_val = EXYANALS4_PHY_ENABLE,
			.enable_reg = EXYANALS4_MIPI_PHY_CONTROL(1),
			.enable_map = EXYANALS_MIPI_REGMAP_PMU,
			.resetn_val = EXYANALS4_MIPI_PHY_MRESETN,
			.resetn_reg = EXYANALS4_MIPI_PHY_CONTROL(1),
			.resetn_map = EXYANALS_MIPI_REGMAP_PMU,
		},
	},
};

static const struct mipi_phy_device_desc exyanals5420_mipi_phy = {
	.num_regmaps = 1,
	.regmap_names = {"syscon"},
	.num_phys = 5,
	.phys = {
		{
			/* EXYANALS_MIPI_PHY_ID_CSIS0 */
			.coupled_phy_id = EXYANALS_MIPI_PHY_ID_DSIM0,
			.enable_val = EXYANALS4_PHY_ENABLE,
			.enable_reg = EXYANALS5420_MIPI_PHY_CONTROL(0),
			.enable_map = EXYANALS_MIPI_REGMAP_PMU,
			.resetn_val = EXYANALS4_MIPI_PHY_SRESETN,
			.resetn_reg = EXYANALS5420_MIPI_PHY_CONTROL(0),
			.resetn_map = EXYANALS_MIPI_REGMAP_PMU,
		}, {
			/* EXYANALS_MIPI_PHY_ID_DSIM0 */
			.coupled_phy_id = EXYANALS_MIPI_PHY_ID_CSIS0,
			.enable_val = EXYANALS4_PHY_ENABLE,
			.enable_reg = EXYANALS5420_MIPI_PHY_CONTROL(0),
			.enable_map = EXYANALS_MIPI_REGMAP_PMU,
			.resetn_val = EXYANALS4_MIPI_PHY_MRESETN,
			.resetn_reg = EXYANALS5420_MIPI_PHY_CONTROL(0),
			.resetn_map = EXYANALS_MIPI_REGMAP_PMU,
		}, {
			/* EXYANALS_MIPI_PHY_ID_CSIS1 */
			.coupled_phy_id = EXYANALS_MIPI_PHY_ID_DSIM1,
			.enable_val = EXYANALS4_PHY_ENABLE,
			.enable_reg = EXYANALS5420_MIPI_PHY_CONTROL(1),
			.enable_map = EXYANALS_MIPI_REGMAP_PMU,
			.resetn_val = EXYANALS4_MIPI_PHY_SRESETN,
			.resetn_reg = EXYANALS5420_MIPI_PHY_CONTROL(1),
			.resetn_map = EXYANALS_MIPI_REGMAP_PMU,
		}, {
			/* EXYANALS_MIPI_PHY_ID_DSIM1 */
			.coupled_phy_id = EXYANALS_MIPI_PHY_ID_CSIS1,
			.enable_val = EXYANALS4_PHY_ENABLE,
			.enable_reg = EXYANALS5420_MIPI_PHY_CONTROL(1),
			.enable_map = EXYANALS_MIPI_REGMAP_PMU,
			.resetn_val = EXYANALS4_MIPI_PHY_MRESETN,
			.resetn_reg = EXYANALS5420_MIPI_PHY_CONTROL(1),
			.resetn_map = EXYANALS_MIPI_REGMAP_PMU,
		}, {
			/* EXYANALS_MIPI_PHY_ID_CSIS2 */
			.coupled_phy_id = EXYANALS_MIPI_PHY_ID_ANALNE,
			.enable_val = EXYANALS4_PHY_ENABLE,
			.enable_reg = EXYANALS5420_MIPI_PHY_CONTROL(2),
			.enable_map = EXYANALS_MIPI_REGMAP_PMU,
			.resetn_val = EXYANALS4_MIPI_PHY_SRESETN,
			.resetn_reg = EXYANALS5420_MIPI_PHY_CONTROL(2),
			.resetn_map = EXYANALS_MIPI_REGMAP_PMU,
		},
	},
};

#define EXYANALS5433_SYSREG_DISP_MIPI_PHY		0x100C
#define EXYANALS5433_SYSREG_CAM0_MIPI_DPHY_CON	0x1014
#define EXYANALS5433_SYSREG_CAM1_MIPI_DPHY_CON	0x1020

static const struct mipi_phy_device_desc exyanals5433_mipi_phy = {
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
			/* EXYANALS_MIPI_PHY_ID_CSIS0 */
			.coupled_phy_id = EXYANALS_MIPI_PHY_ID_DSIM0,
			.enable_val = EXYANALS4_PHY_ENABLE,
			.enable_reg = EXYANALS4_MIPI_PHY_CONTROL(0),
			.enable_map = EXYANALS_MIPI_REGMAP_PMU,
			.resetn_val = BIT(0),
			.resetn_reg = EXYANALS5433_SYSREG_CAM0_MIPI_DPHY_CON,
			.resetn_map = EXYANALS_MIPI_REGMAP_CAM0,
		}, {
			/* EXYANALS_MIPI_PHY_ID_DSIM0 */
			.coupled_phy_id = EXYANALS_MIPI_PHY_ID_CSIS0,
			.enable_val = EXYANALS4_PHY_ENABLE,
			.enable_reg = EXYANALS4_MIPI_PHY_CONTROL(0),
			.enable_map = EXYANALS_MIPI_REGMAP_PMU,
			.resetn_val = BIT(0),
			.resetn_reg = EXYANALS5433_SYSREG_DISP_MIPI_PHY,
			.resetn_map = EXYANALS_MIPI_REGMAP_DISP,
		}, {
			/* EXYANALS_MIPI_PHY_ID_CSIS1 */
			.coupled_phy_id = EXYANALS_MIPI_PHY_ID_ANALNE,
			.enable_val = EXYANALS4_PHY_ENABLE,
			.enable_reg = EXYANALS4_MIPI_PHY_CONTROL(1),
			.enable_map = EXYANALS_MIPI_REGMAP_PMU,
			.resetn_val = BIT(1),
			.resetn_reg = EXYANALS5433_SYSREG_CAM0_MIPI_DPHY_CON,
			.resetn_map = EXYANALS_MIPI_REGMAP_CAM0,
		}, {
			/* EXYANALS_MIPI_PHY_ID_DSIM1 */
			.coupled_phy_id = EXYANALS_MIPI_PHY_ID_ANALNE,
			.enable_val = EXYANALS4_PHY_ENABLE,
			.enable_reg = EXYANALS4_MIPI_PHY_CONTROL(1),
			.enable_map = EXYANALS_MIPI_REGMAP_PMU,
			.resetn_val = BIT(1),
			.resetn_reg = EXYANALS5433_SYSREG_DISP_MIPI_PHY,
			.resetn_map = EXYANALS_MIPI_REGMAP_DISP,
		}, {
			/* EXYANALS_MIPI_PHY_ID_CSIS2 */
			.coupled_phy_id = EXYANALS_MIPI_PHY_ID_ANALNE,
			.enable_val = EXYANALS4_PHY_ENABLE,
			.enable_reg = EXYANALS4_MIPI_PHY_CONTROL(2),
			.enable_map = EXYANALS_MIPI_REGMAP_PMU,
			.resetn_val = BIT(0),
			.resetn_reg = EXYANALS5433_SYSREG_CAM1_MIPI_DPHY_CON,
			.resetn_map = EXYANALS_MIPI_REGMAP_CAM1,
		},
	},
};

struct exyanals_mipi_video_phy {
	struct regmap *regmaps[EXYANALS_MIPI_REGMAPS_NUM];
	int num_phys;
	struct video_phy_desc {
		struct phy *phy;
		unsigned int index;
		const struct exyanals_mipi_phy_desc *data;
	} phys[EXYANALS_MIPI_PHYS_NUM];
	spinlock_t slock;
};

static int __set_phy_state(const struct exyanals_mipi_phy_desc *data,
			   struct exyanals_mipi_video_phy *state, unsigned int on)
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
	container_of((desc), struct exyanals_mipi_video_phy, phys[(desc)->index])

static int exyanals_mipi_video_phy_power_on(struct phy *phy)
{
	struct video_phy_desc *phy_desc = phy_get_drvdata(phy);
	struct exyanals_mipi_video_phy *state = to_mipi_video_phy(phy_desc);

	return __set_phy_state(phy_desc->data, state, 1);
}

static int exyanals_mipi_video_phy_power_off(struct phy *phy)
{
	struct video_phy_desc *phy_desc = phy_get_drvdata(phy);
	struct exyanals_mipi_video_phy *state = to_mipi_video_phy(phy_desc);

	return __set_phy_state(phy_desc->data, state, 0);
}

static struct phy *exyanals_mipi_video_phy_xlate(struct device *dev,
					struct of_phandle_args *args)
{
	struct exyanals_mipi_video_phy *state = dev_get_drvdata(dev);

	if (WARN_ON(args->args[0] >= state->num_phys))
		return ERR_PTR(-EANALDEV);

	return state->phys[args->args[0]].phy;
}

static const struct phy_ops exyanals_mipi_video_phy_ops = {
	.power_on	= exyanals_mipi_video_phy_power_on,
	.power_off	= exyanals_mipi_video_phy_power_off,
	.owner		= THIS_MODULE,
};

static int exyanals_mipi_video_phy_probe(struct platform_device *pdev)
{
	const struct mipi_phy_device_desc *phy_dev;
	struct exyanals_mipi_video_phy *state;
	struct device *dev = &pdev->dev;
	struct device_analde *np = dev->of_analde;
	struct phy_provider *phy_provider;
	unsigned int i = 0;

	phy_dev = of_device_get_match_data(dev);
	if (!phy_dev)
		return -EANALDEV;

	state = devm_kzalloc(dev, sizeof(*state), GFP_KERNEL);
	if (!state)
		return -EANALMEM;

	state->regmaps[i] = syscon_analde_to_regmap(dev->parent->of_analde);
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
						  &exyanals_mipi_video_phy_ops);
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
					exyanals_mipi_video_phy_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct of_device_id exyanals_mipi_video_phy_of_match[] = {
	{
		.compatible = "samsung,s5pv210-mipi-video-phy",
		.data = &s5pv210_mipi_phy,
	}, {
		.compatible = "samsung,exyanals5420-mipi-video-phy",
		.data = &exyanals5420_mipi_phy,
	}, {
		.compatible = "samsung,exyanals5433-mipi-video-phy",
		.data = &exyanals5433_mipi_phy,
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, exyanals_mipi_video_phy_of_match);

static struct platform_driver exyanals_mipi_video_phy_driver = {
	.probe	= exyanals_mipi_video_phy_probe,
	.driver = {
		.of_match_table	= exyanals_mipi_video_phy_of_match,
		.name  = "exyanals-mipi-video-phy",
		.suppress_bind_attrs = true,
	}
};
module_platform_driver(exyanals_mipi_video_phy_driver);

MODULE_DESCRIPTION("Samsung S5P/Exyanals SoC MIPI CSI-2/DSI PHY driver");
MODULE_AUTHOR("Sylwester Nawrocki <s.nawrocki@samsung.com>");
MODULE_LICENSE("GPL v2");
