// SPDX-License-Identifier: GPL-2.0-only
/*
 * Tegra124 DFLL FCPU clock source driver
 *
 * Copyright (C) 2012-2019 NVIDIA Corporation.  All rights reserved.
 *
 * Aleksandr Frid <afrid@nvidia.com>
 * Paul Walmsley <pwalmsley@nvidia.com>
 */

#include <linux/cpu.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <soc/tegra/fuse.h>

#include "clk.h"
#include "clk-dfll.h"
#include "cvb.h"

struct dfll_fcpu_data {
	const unsigned long *cpu_max_freq_table;
	unsigned int cpu_max_freq_table_size;
	const struct cvb_table *cpu_cvb_tables;
	unsigned int cpu_cvb_tables_size;
};

/* Maximum CPU frequency, indexed by CPU speedo id */
static const unsigned long tegra124_cpu_max_freq_table[] = {
	[0] = 2014500000UL,
	[1] = 2320500000UL,
	[2] = 2116500000UL,
	[3] = 2524500000UL,
};

static const struct cvb_table tegra124_cpu_cvb_tables[] = {
	{
		.speedo_id = -1,
		.process_id = -1,
		.min_millivolts = 900,
		.max_millivolts = 1260,
		.speedo_scale = 100,
		.voltage_scale = 1000,
		.entries = {
			{  204000000UL, { 1112619, -29295, 402 } },
			{  306000000UL, { 1150460, -30585, 402 } },
			{  408000000UL, { 1190122, -31865, 402 } },
			{  510000000UL, { 1231606, -33155, 402 } },
			{  612000000UL, { 1274912, -34435, 402 } },
			{  714000000UL, { 1320040, -35725, 402 } },
			{  816000000UL, { 1366990, -37005, 402 } },
			{  918000000UL, { 1415762, -38295, 402 } },
			{ 1020000000UL, { 1466355, -39575, 402 } },
			{ 1122000000UL, { 1518771, -40865, 402 } },
			{ 1224000000UL, { 1573009, -42145, 402 } },
			{ 1326000000UL, { 1629068, -43435, 402 } },
			{ 1428000000UL, { 1686950, -44715, 402 } },
			{ 1530000000UL, { 1746653, -46005, 402 } },
			{ 1632000000UL, { 1808179, -47285, 402 } },
			{ 1734000000UL, { 1871526, -48575, 402 } },
			{ 1836000000UL, { 1936696, -49855, 402 } },
			{ 1938000000UL, { 2003687, -51145, 402 } },
			{ 2014500000UL, { 2054787, -52095, 402 } },
			{ 2116500000UL, { 2124957, -53385, 402 } },
			{ 2218500000UL, { 2196950, -54665, 402 } },
			{ 2320500000UL, { 2270765, -55955, 402 } },
			{ 2422500000UL, { 2346401, -57235, 402 } },
			{ 2524500000UL, { 2437299, -58535, 402 } },
			{          0UL, {       0,      0,   0 } },
		},
		.cpu_dfll_data = {
			.tune0_low = 0x005020ff,
			.tune0_high = 0x005040ff,
			.tune1 = 0x00000060,
		}
	},
};

static const unsigned long tegra210_cpu_max_freq_table[] = {
	[0] = 1912500000UL,
	[1] = 1912500000UL,
	[2] = 2218500000UL,
	[3] = 1785000000UL,
	[4] = 1632000000UL,
	[5] = 1912500000UL,
	[6] = 2014500000UL,
	[7] = 1734000000UL,
	[8] = 1683000000UL,
	[9] = 1555500000UL,
	[10] = 1504500000UL,
};

#define CPU_CVB_TABLE \
	.speedo_scale = 100,	\
	.voltage_scale = 1000,	\
	.entries = {		\
		{  204000000UL,	{ 1007452, -23865, 370 } }, \
		{  306000000UL,	{ 1052709, -24875, 370 } }, \
		{  408000000UL,	{ 1099069, -25895, 370 } }, \
		{  510000000UL,	{ 1146534, -26905, 370 } }, \
		{  612000000UL,	{ 1195102, -27915, 370 } }, \
		{  714000000UL,	{ 1244773, -28925, 370 } }, \
		{  816000000UL,	{ 1295549, -29935, 370 } }, \
		{  918000000UL,	{ 1347428, -30955, 370 } }, \
		{ 1020000000UL,	{ 1400411, -31965, 370 } }, \
		{ 1122000000UL,	{ 1454497, -32975, 370 } }, \
		{ 1224000000UL,	{ 1509687, -33985, 370 } }, \
		{ 1326000000UL,	{ 1565981, -35005, 370 } }, \
		{ 1428000000UL,	{ 1623379, -36015, 370 } }, \
		{ 1530000000UL,	{ 1681880, -37025, 370 } }, \
		{ 1632000000UL,	{ 1741485, -38035, 370 } }, \
		{ 1734000000UL,	{ 1802194, -39055, 370 } }, \
		{ 1836000000UL,	{ 1864006, -40065, 370 } }, \
		{ 1912500000UL,	{ 1910780, -40815, 370 } }, \
		{ 2014500000UL,	{ 1227000,      0,   0 } }, \
		{ 2218500000UL,	{ 1227000,      0,   0 } }, \
		{          0UL,	{       0,      0,   0 } }, \
	}

#define CPU_CVB_TABLE_XA \
	.speedo_scale = 100,	\
	.voltage_scale = 1000,	\
	.entries = {		\
		{  204000000UL,	{ 1250024, -39785, 565 } }, \
		{  306000000UL,	{ 1297556, -41145, 565 } }, \
		{  408000000UL,	{ 1346718, -42505, 565 } }, \
		{  510000000UL,	{ 1397511, -43855, 565 } }, \
		{  612000000UL,	{ 1449933, -45215, 565 } }, \
		{  714000000UL,	{ 1503986, -46575, 565 } }, \
		{  816000000UL,	{ 1559669, -47935, 565 } }, \
		{  918000000UL,	{ 1616982, -49295, 565 } }, \
		{ 1020000000UL,	{ 1675926, -50645, 565 } }, \
		{ 1122000000UL,	{ 1736500, -52005, 565 } }, \
		{ 1224000000UL,	{ 1798704, -53365, 565 } }, \
		{ 1326000000UL,	{ 1862538, -54725, 565 } }, \
		{ 1428000000UL,	{ 1928003, -56085, 565 } }, \
		{ 1530000000UL,	{ 1995097, -57435, 565 } }, \
		{ 1606500000UL,	{ 2046149, -58445, 565 } }, \
		{ 1632000000UL,	{ 2063822, -58795, 565 } }, \
		{          0UL,	{       0,      0,   0 } }, \
	}

#define CPU_CVB_TABLE_EUCM1 \
	.speedo_scale = 100,	\
	.voltage_scale = 1000,	\
	.entries = {		\
		{  204000000UL,	{  734429, 0, 0 } }, \
		{  306000000UL,	{  768191, 0, 0 } }, \
		{  408000000UL,	{  801953, 0, 0 } }, \
		{  510000000UL,	{  835715, 0, 0 } }, \
		{  612000000UL,	{  869477, 0, 0 } }, \
		{  714000000UL,	{  903239, 0, 0 } }, \
		{  816000000UL,	{  937001, 0, 0 } }, \
		{  918000000UL,	{  970763, 0, 0 } }, \
		{ 1020000000UL,	{ 1004525, 0, 0 } }, \
		{ 1122000000UL,	{ 1038287, 0, 0 } }, \
		{ 1224000000UL,	{ 1072049, 0, 0 } }, \
		{ 1326000000UL,	{ 1105811, 0, 0 } }, \
		{ 1428000000UL,	{ 1130000, 0, 0 } }, \
		{ 1555500000UL,	{ 1130000, 0, 0 } }, \
		{ 1632000000UL,	{ 1170000, 0, 0 } }, \
		{ 1734000000UL,	{ 1227500, 0, 0 } }, \
		{          0UL,	{       0, 0, 0 } }, \
	}

#define CPU_CVB_TABLE_EUCM2 \
	.speedo_scale = 100,	\
	.voltage_scale = 1000,	\
	.entries = {		\
		{  204000000UL,	{  742283, 0, 0 } }, \
		{  306000000UL,	{  776249, 0, 0 } }, \
		{  408000000UL,	{  810215, 0, 0 } }, \
		{  510000000UL,	{  844181, 0, 0 } }, \
		{  612000000UL,	{  878147, 0, 0 } }, \
		{  714000000UL,	{  912113, 0, 0 } }, \
		{  816000000UL,	{  946079, 0, 0 } }, \
		{  918000000UL,	{  980045, 0, 0 } }, \
		{ 1020000000UL,	{ 1014011, 0, 0 } }, \
		{ 1122000000UL,	{ 1047977, 0, 0 } }, \
		{ 1224000000UL,	{ 1081943, 0, 0 } }, \
		{ 1326000000UL,	{ 1090000, 0, 0 } }, \
		{ 1479000000UL,	{ 1090000, 0, 0 } }, \
		{ 1555500000UL,	{ 1162000, 0, 0 } }, \
		{ 1683000000UL,	{ 1195000, 0, 0 } }, \
		{          0UL,	{       0, 0, 0 } }, \
	}

#define CPU_CVB_TABLE_EUCM2_JOINT_RAIL \
	.speedo_scale = 100,	\
	.voltage_scale = 1000,	\
	.entries = {		\
		{  204000000UL,	{  742283, 0, 0 } }, \
		{  306000000UL,	{  776249, 0, 0 } }, \
		{  408000000UL,	{  810215, 0, 0 } }, \
		{  510000000UL,	{  844181, 0, 0 } }, \
		{  612000000UL,	{  878147, 0, 0 } }, \
		{  714000000UL,	{  912113, 0, 0 } }, \
		{  816000000UL,	{  946079, 0, 0 } }, \
		{  918000000UL,	{  980045, 0, 0 } }, \
		{ 1020000000UL,	{ 1014011, 0, 0 } }, \
		{ 1122000000UL,	{ 1047977, 0, 0 } }, \
		{ 1224000000UL,	{ 1081943, 0, 0 } }, \
		{ 1326000000UL,	{ 1090000, 0, 0 } }, \
		{ 1479000000UL,	{ 1090000, 0, 0 } }, \
		{ 1504500000UL,	{ 1120000, 0, 0 } }, \
		{          0UL,	{       0, 0, 0 } }, \
	}

#define CPU_CVB_TABLE_ODN \
	.speedo_scale = 100,	\
	.voltage_scale = 1000,	\
	.entries = {		\
		{  204000000UL,	{  721094, 0, 0 } }, \
		{  306000000UL,	{  754040, 0, 0 } }, \
		{  408000000UL,	{  786986, 0, 0 } }, \
		{  510000000UL,	{  819932, 0, 0 } }, \
		{  612000000UL,	{  852878, 0, 0 } }, \
		{  714000000UL,	{  885824, 0, 0 } }, \
		{  816000000UL,	{  918770, 0, 0 } }, \
		{  918000000UL,	{  915716, 0, 0 } }, \
		{ 1020000000UL,	{  984662, 0, 0 } }, \
		{ 1122000000UL,	{ 1017608, 0, 0 } }, \
		{ 1224000000UL,	{ 1050554, 0, 0 } }, \
		{ 1326000000UL,	{ 1083500, 0, 0 } }, \
		{ 1428000000UL,	{ 1116446, 0, 0 } }, \
		{ 1581000000UL,	{ 1130000, 0, 0 } }, \
		{ 1683000000UL,	{ 1168000, 0, 0 } }, \
		{ 1785000000UL,	{ 1227500, 0, 0 } }, \
		{          0UL,	{       0, 0, 0 } }, \
	}

static struct cvb_table tegra210_cpu_cvb_tables[] = {
	{
		.speedo_id = 10,
		.process_id = 0,
		.min_millivolts = 840,
		.max_millivolts = 1120,
		CPU_CVB_TABLE_EUCM2_JOINT_RAIL,
		.cpu_dfll_data = {
			.tune0_low = 0xffead0ff,
			.tune0_high = 0xffead0ff,
			.tune1 = 0x20091d9,
			.tune_high_min_millivolts = 864,
		}
	},
	{
		.speedo_id = 10,
		.process_id = 1,
		.min_millivolts = 840,
		.max_millivolts = 1120,
		CPU_CVB_TABLE_EUCM2_JOINT_RAIL,
		.cpu_dfll_data = {
			.tune0_low = 0xffead0ff,
			.tune0_high = 0xffead0ff,
			.tune1 = 0x20091d9,
			.tune_high_min_millivolts = 864,
		}
	},
	{
		.speedo_id = 9,
		.process_id = 0,
		.min_millivolts = 900,
		.max_millivolts = 1162,
		CPU_CVB_TABLE_EUCM2,
		.cpu_dfll_data = {
			.tune0_low = 0xffead0ff,
			.tune0_high = 0xffead0ff,
			.tune1 = 0x20091d9,
		}
	},
	{
		.speedo_id = 9,
		.process_id = 1,
		.min_millivolts = 900,
		.max_millivolts = 1162,
		CPU_CVB_TABLE_EUCM2,
		.cpu_dfll_data = {
			.tune0_low = 0xffead0ff,
			.tune0_high = 0xffead0ff,
			.tune1 = 0x20091d9,
		}
	},
	{
		.speedo_id = 8,
		.process_id = 0,
		.min_millivolts = 900,
		.max_millivolts = 1195,
		CPU_CVB_TABLE_EUCM2,
		.cpu_dfll_data = {
			.tune0_low = 0xffead0ff,
			.tune0_high = 0xffead0ff,
			.tune1 = 0x20091d9,
		}
	},
	{
		.speedo_id = 8,
		.process_id = 1,
		.min_millivolts = 900,
		.max_millivolts = 1195,
		CPU_CVB_TABLE_EUCM2,
		.cpu_dfll_data = {
			.tune0_low = 0xffead0ff,
			.tune0_high = 0xffead0ff,
			.tune1 = 0x20091d9,
		}
	},
	{
		.speedo_id = 7,
		.process_id = 0,
		.min_millivolts = 841,
		.max_millivolts = 1227,
		CPU_CVB_TABLE_EUCM1,
		.cpu_dfll_data = {
			.tune0_low = 0xffead0ff,
			.tune0_high = 0xffead0ff,
			.tune1 = 0x20091d9,
			.tune_high_min_millivolts = 864,
		}
	},
	{
		.speedo_id = 7,
		.process_id = 1,
		.min_millivolts = 841,
		.max_millivolts = 1227,
		CPU_CVB_TABLE_EUCM1,
		.cpu_dfll_data = {
			.tune0_low = 0xffead0ff,
			.tune0_high = 0xffead0ff,
			.tune1 = 0x20091d9,
			.tune_high_min_millivolts = 864,
		}
	},
	{
		.speedo_id = 6,
		.process_id = 0,
		.min_millivolts = 870,
		.max_millivolts = 1150,
		CPU_CVB_TABLE,
		.cpu_dfll_data = {
			.tune0_low = 0xffead0ff,
			.tune1 = 0x20091d9,
		}
	},
	{
		.speedo_id = 6,
		.process_id = 1,
		.min_millivolts = 870,
		.max_millivolts = 1150,
		CPU_CVB_TABLE,
		.cpu_dfll_data = {
			.tune0_low = 0xffead0ff,
			.tune1 = 0x25501d0,
		}
	},
	{
		.speedo_id = 5,
		.process_id = 0,
		.min_millivolts = 818,
		.max_millivolts = 1227,
		CPU_CVB_TABLE,
		.cpu_dfll_data = {
			.tune0_low = 0xffead0ff,
			.tune0_high = 0xffead0ff,
			.tune1 = 0x20091d9,
			.tune_high_min_millivolts = 864,
		}
	},
	{
		.speedo_id = 5,
		.process_id = 1,
		.min_millivolts = 818,
		.max_millivolts = 1227,
		CPU_CVB_TABLE,
		.cpu_dfll_data = {
			.tune0_low = 0xffead0ff,
			.tune0_high = 0xffead0ff,
			.tune1 = 0x25501d0,
			.tune_high_min_millivolts = 864,
		}
	},
	{
		.speedo_id = 4,
		.process_id = -1,
		.min_millivolts = 918,
		.max_millivolts = 1113,
		CPU_CVB_TABLE_XA,
		.cpu_dfll_data = {
			.tune0_low = 0xffead0ff,
			.tune1 = 0x17711BD,
		}
	},
	{
		.speedo_id = 3,
		.process_id = 0,
		.min_millivolts = 825,
		.max_millivolts = 1227,
		CPU_CVB_TABLE_ODN,
		.cpu_dfll_data = {
			.tune0_low = 0xffead0ff,
			.tune0_high = 0xffead0ff,
			.tune1 = 0x20091d9,
			.tune_high_min_millivolts = 864,
		}
	},
	{
		.speedo_id = 3,
		.process_id = 1,
		.min_millivolts = 825,
		.max_millivolts = 1227,
		CPU_CVB_TABLE_ODN,
		.cpu_dfll_data = {
			.tune0_low = 0xffead0ff,
			.tune0_high = 0xffead0ff,
			.tune1 = 0x25501d0,
			.tune_high_min_millivolts = 864,
		}
	},
	{
		.speedo_id = 2,
		.process_id = 0,
		.min_millivolts = 870,
		.max_millivolts = 1227,
		CPU_CVB_TABLE,
		.cpu_dfll_data = {
			.tune0_low = 0xffead0ff,
			.tune1 = 0x20091d9,
		}
	},
	{
		.speedo_id = 2,
		.process_id = 1,
		.min_millivolts = 870,
		.max_millivolts = 1227,
		CPU_CVB_TABLE,
		.cpu_dfll_data = {
			.tune0_low = 0xffead0ff,
			.tune1 = 0x25501d0,
		}
	},
	{
		.speedo_id = 1,
		.process_id = 0,
		.min_millivolts = 837,
		.max_millivolts = 1227,
		CPU_CVB_TABLE,
		.cpu_dfll_data = {
			.tune0_low = 0xffead0ff,
			.tune0_high = 0xffead0ff,
			.tune1 = 0x20091d9,
			.tune_high_min_millivolts = 864,
		}
	},
	{
		.speedo_id = 1,
		.process_id = 1,
		.min_millivolts = 837,
		.max_millivolts = 1227,
		CPU_CVB_TABLE,
		.cpu_dfll_data = {
			.tune0_low = 0xffead0ff,
			.tune0_high = 0xffead0ff,
			.tune1 = 0x25501d0,
			.tune_high_min_millivolts = 864,
		}
	},
	{
		.speedo_id = 0,
		.process_id = 0,
		.min_millivolts = 850,
		.max_millivolts = 1170,
		CPU_CVB_TABLE,
		.cpu_dfll_data = {
			.tune0_low = 0xffead0ff,
			.tune0_high = 0xffead0ff,
			.tune1 = 0x20091d9,
			.tune_high_min_millivolts = 864,
		}
	},
	{
		.speedo_id = 0,
		.process_id = 1,
		.min_millivolts = 850,
		.max_millivolts = 1170,
		CPU_CVB_TABLE,
		.cpu_dfll_data = {
			.tune0_low = 0xffead0ff,
			.tune0_high = 0xffead0ff,
			.tune1 = 0x25501d0,
			.tune_high_min_millivolts = 864,
		}
	},
};

static const struct dfll_fcpu_data tegra124_dfll_fcpu_data = {
	.cpu_max_freq_table = tegra124_cpu_max_freq_table,
	.cpu_max_freq_table_size = ARRAY_SIZE(tegra124_cpu_max_freq_table),
	.cpu_cvb_tables = tegra124_cpu_cvb_tables,
	.cpu_cvb_tables_size = ARRAY_SIZE(tegra124_cpu_cvb_tables)
};

static const struct dfll_fcpu_data tegra210_dfll_fcpu_data = {
	.cpu_max_freq_table = tegra210_cpu_max_freq_table,
	.cpu_max_freq_table_size = ARRAY_SIZE(tegra210_cpu_max_freq_table),
	.cpu_cvb_tables = tegra210_cpu_cvb_tables,
	.cpu_cvb_tables_size = ARRAY_SIZE(tegra210_cpu_cvb_tables),
};

static const struct of_device_id tegra124_dfll_fcpu_of_match[] = {
	{
		.compatible = "nvidia,tegra124-dfll",
		.data = &tegra124_dfll_fcpu_data,
	},
	{
		.compatible = "nvidia,tegra210-dfll",
		.data = &tegra210_dfll_fcpu_data
	},
	{ },
};

static void get_alignment_from_dt(struct device *dev,
				  struct rail_alignment *align)
{
	if (of_property_read_u32(dev->of_node,
				 "nvidia,pwm-voltage-step-microvolts",
				 &align->step_uv))
		align->step_uv = 0;

	if (of_property_read_u32(dev->of_node,
				 "nvidia,pwm-min-microvolts",
				 &align->offset_uv))
		align->offset_uv = 0;
}

static int get_alignment_from_regulator(struct device *dev,
					 struct rail_alignment *align)
{
	struct regulator *reg = devm_regulator_get(dev, "vdd-cpu");

	if (IS_ERR(reg))
		return PTR_ERR(reg);

	align->offset_uv = regulator_list_voltage(reg, 0);
	align->step_uv = regulator_get_linear_step(reg);

	devm_regulator_put(reg);

	return 0;
}

static int tegra124_dfll_fcpu_probe(struct platform_device *pdev)
{
	int process_id, speedo_id, speedo_value, err;
	struct tegra_dfll_soc_data *soc;
	const struct dfll_fcpu_data *fcpu_data;
	struct rail_alignment align;

	fcpu_data = of_device_get_match_data(&pdev->dev);
	if (!fcpu_data)
		return -ENODEV;

	process_id = tegra_sku_info.cpu_process_id;
	speedo_id = tegra_sku_info.cpu_speedo_id;
	speedo_value = tegra_sku_info.cpu_speedo_value;

	if (speedo_id >= fcpu_data->cpu_max_freq_table_size) {
		dev_err(&pdev->dev, "unknown max CPU freq for speedo_id=%d\n",
			speedo_id);
		return -ENODEV;
	}

	soc = devm_kzalloc(&pdev->dev, sizeof(*soc), GFP_KERNEL);
	if (!soc)
		return -ENOMEM;

	soc->dev = get_cpu_device(0);
	if (!soc->dev) {
		dev_err(&pdev->dev, "no CPU0 device\n");
		return -ENODEV;
	}

	if (of_property_read_bool(pdev->dev.of_node, "nvidia,pwm-to-pmic")) {
		get_alignment_from_dt(&pdev->dev, &align);
	} else {
		err = get_alignment_from_regulator(&pdev->dev, &align);
		if (err)
			return err;
	}

	soc->max_freq = fcpu_data->cpu_max_freq_table[speedo_id];

	soc->cvb = tegra_cvb_add_opp_table(soc->dev, fcpu_data->cpu_cvb_tables,
					   fcpu_data->cpu_cvb_tables_size,
					   &align, process_id, speedo_id,
					   speedo_value, soc->max_freq);
	soc->alignment = align;

	if (IS_ERR(soc->cvb)) {
		dev_err(&pdev->dev, "couldn't add OPP table: %ld\n",
			PTR_ERR(soc->cvb));
		return PTR_ERR(soc->cvb);
	}

	err = tegra_dfll_register(pdev, soc);
	if (err < 0) {
		tegra_cvb_remove_opp_table(soc->dev, soc->cvb, soc->max_freq);
		return err;
	}

	return 0;
}

static int tegra124_dfll_fcpu_remove(struct platform_device *pdev)
{
	struct tegra_dfll_soc_data *soc;

	soc = tegra_dfll_unregister(pdev);
	if (IS_ERR(soc)) {
		dev_err(&pdev->dev, "failed to unregister DFLL: %ld\n",
			PTR_ERR(soc));
		return PTR_ERR(soc);
	}

	tegra_cvb_remove_opp_table(soc->dev, soc->cvb, soc->max_freq);

	return 0;
}

static const struct dev_pm_ops tegra124_dfll_pm_ops = {
	SET_RUNTIME_PM_OPS(tegra_dfll_runtime_suspend,
			   tegra_dfll_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(tegra_dfll_suspend, tegra_dfll_resume)
};

static struct platform_driver tegra124_dfll_fcpu_driver = {
	.probe = tegra124_dfll_fcpu_probe,
	.remove = tegra124_dfll_fcpu_remove,
	.driver = {
		.name = "tegra124-dfll",
		.of_match_table = tegra124_dfll_fcpu_of_match,
		.pm = &tegra124_dfll_pm_ops,
	},
};
builtin_platform_driver(tegra124_dfll_fcpu_driver);
