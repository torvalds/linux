// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 MediaTek Inc.
 * Copyright (c) 2024 Collabora Ltd.
 *                    AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/soc/mediatek/dvfsrc.h>

enum dvfsrc_regulator_id {
	DVFSRC_ID_VCORE,
	DVFSRC_ID_VSCP,
	DVFSRC_ID_MAX
};

struct dvfsrc_regulator_pdata {
	struct regulator_desc *descs;
	u32 size;
};

#define MTK_DVFSRC_VREG(match, _name, _volt_table)	\
{							\
	.name = match,					\
	.of_match = match,				\
	.ops = &dvfsrc_vcore_ops,			\
	.type = REGULATOR_VOLTAGE,			\
	.id = DVFSRC_ID_##_name,			\
	.owner = THIS_MODULE,				\
	.n_voltages = ARRAY_SIZE(_volt_table),		\
	.volt_table = _volt_table,			\
}

static inline struct device *to_dvfs_regulator_dev(struct regulator_dev *rdev)
{
	return rdev_get_dev(rdev)->parent;
}

static inline struct device *to_dvfsrc_dev(struct regulator_dev *rdev)
{
	return to_dvfs_regulator_dev(rdev)->parent;
}

static int dvfsrc_get_cmd(int rdev_id, enum mtk_dvfsrc_cmd *cmd)
{
	switch (rdev_id) {
	case DVFSRC_ID_VCORE:
		*cmd = MTK_DVFSRC_CMD_VCORE_LEVEL;
		break;
	case DVFSRC_ID_VSCP:
		*cmd = MTK_DVFSRC_CMD_VSCP_LEVEL;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int dvfsrc_set_voltage_sel(struct regulator_dev *rdev,
				  unsigned int selector)
{
	struct device *dvfsrc_dev = to_dvfsrc_dev(rdev);
	enum mtk_dvfsrc_cmd req_cmd;
	int id = rdev_get_id(rdev);
	int ret;

	ret = dvfsrc_get_cmd(id, &req_cmd);
	if (ret)
		return ret;

	return mtk_dvfsrc_send_request(dvfsrc_dev, req_cmd, selector);
}

static int dvfsrc_get_voltage_sel(struct regulator_dev *rdev)
{
	struct device *dvfsrc_dev = to_dvfsrc_dev(rdev);
	enum mtk_dvfsrc_cmd query_cmd;
	int id = rdev_get_id(rdev);
	int val, ret;

	ret = dvfsrc_get_cmd(id, &query_cmd);
	if (ret)
		return ret;

	ret = mtk_dvfsrc_query_info(dvfsrc_dev, query_cmd, &val);
	if (ret)
		return ret;

	return val;
}

static const struct regulator_ops dvfsrc_vcore_ops = {
	.list_voltage = regulator_list_voltage_table,
	.get_voltage_sel = dvfsrc_get_voltage_sel,
	.set_voltage_sel = dvfsrc_set_voltage_sel,
};

static const unsigned int mt6873_voltages[] = {
	575000,
	600000,
	650000,
	725000,
};

static struct regulator_desc mt6873_regulators[] = {
	MTK_DVFSRC_VREG("dvfsrc-vcore", VCORE, mt6873_voltages),
	MTK_DVFSRC_VREG("dvfsrc-vscp", VSCP, mt6873_voltages),
};

static const struct dvfsrc_regulator_pdata mt6873_data = {
	.descs = mt6873_regulators,
	.size = ARRAY_SIZE(mt6873_regulators),
};

static const unsigned int mt8183_voltages[] = {
	725000,
	800000,
};

static struct regulator_desc mt8183_regulators[] = {
	MTK_DVFSRC_VREG("dvfsrc-vcore", VCORE, mt8183_voltages),
};

static const struct dvfsrc_regulator_pdata mt8183_data = {
	.descs = mt8183_regulators,
	.size = ARRAY_SIZE(mt8183_regulators),
};

static const unsigned int mt8195_voltages[] = {
	550000,
	600000,
	650000,
	750000,
};

static struct regulator_desc mt8195_regulators[] = {
	MTK_DVFSRC_VREG("dvfsrc-vcore", VCORE, mt8195_voltages),
	MTK_DVFSRC_VREG("dvfsrc-vscp", VSCP, mt8195_voltages),
};

static const struct dvfsrc_regulator_pdata mt8195_data = {
	.descs = mt8195_regulators,
	.size = ARRAY_SIZE(mt8195_regulators),
};

static int dvfsrc_vcore_regulator_probe(struct platform_device *pdev)
{
	struct regulator_config config = { .dev = &pdev->dev };
	const struct dvfsrc_regulator_pdata *pdata;
	int i;

	pdata = device_get_match_data(&pdev->dev);
	if (!pdata)
		return -EINVAL;

	for (i = 0; i < pdata->size; i++) {
		struct regulator_desc *vrdesc = &pdata->descs[i];
		struct regulator_dev *rdev;

		rdev = devm_regulator_register(&pdev->dev, vrdesc, &config);
		if (IS_ERR(rdev))
			return dev_err_probe(&pdev->dev, PTR_ERR(rdev),
					     "failed to register %s\n", vrdesc->name);
	}

	return 0;
}

static const struct of_device_id mtk_dvfsrc_regulator_match[] = {
	{ .compatible = "mediatek,mt6873-dvfsrc-regulator", .data = &mt6873_data },
	{ .compatible = "mediatek,mt8183-dvfsrc-regulator", .data = &mt8183_data },
	{ .compatible = "mediatek,mt8192-dvfsrc-regulator", .data = &mt6873_data },
	{ .compatible = "mediatek,mt8195-dvfsrc-regulator", .data = &mt8195_data },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mtk_dvfsrc_regulator_match);

static struct platform_driver mtk_dvfsrc_regulator_driver = {
	.driver = {
		.name  = "mtk-dvfsrc-regulator",
		.of_match_table = mtk_dvfsrc_regulator_match,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.probe = dvfsrc_vcore_regulator_probe,
};
module_platform_driver(mtk_dvfsrc_regulator_driver);

MODULE_AUTHOR("AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>");
MODULE_AUTHOR("Arvin wang <arvin.wang@mediatek.com>");
MODULE_DESCRIPTION("MediaTek DVFS Resource Collector Regulator driver");
MODULE_LICENSE("GPL");
