/*
 * Copyright (c) 2017 Fuzhou Rockchip Electronics Co., Ltd
 *
 * SPDX-License-Identifier: GPL-2.0+
 */
#include <linux/clk.h>
#include <linux/nvmem-consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/soc/rockchip/pvtm.h>
#include <linux/thermal.h>
#include <linux/pm_opp.h>
#include <linux/version.h>
#include <soc/rockchip/rockchip_opp_select.h>

#include "../../clk/rockchip/clk.h"
#include "../../base/power/opp/opp.h"

#define LEAKAGE_TABLE_END	~1
#define LEAKAGE_INVALID		0xff

struct volt_sel_table {
	int min;
	int max;
	int sel;
};

struct pvtm_config {
	unsigned int freq;
	unsigned int volt;
	unsigned int ch[2];
	unsigned int sample_time;
	unsigned int num;
	unsigned int err;
	unsigned int ref_temp;
	int temp_prop[2];
	const char *tz_name;
	struct thermal_zone_device *tz;
};

static int rockchip_get_efuse_value(struct device_node *np, char *porp_name,
				    int *value)
{
	struct nvmem_cell *cell;
	unsigned char *buf;
	size_t len;

	cell = of_nvmem_cell_get(np, porp_name);
	if (IS_ERR(cell))
		return PTR_ERR(cell);

	buf = (unsigned char *)nvmem_cell_read(cell, &len);

	nvmem_cell_put(cell);

	if (IS_ERR(buf))
		return PTR_ERR(buf);

	if (buf[0] == LEAKAGE_INVALID)
		return -EINVAL;

	*value = buf[0];

	kfree(buf);

	return 0;
}

static int rockchip_get_volt_sel_table(struct device_node *np, char *porp_name,
				       struct volt_sel_table **table)
{
	struct volt_sel_table *sel_table;
	const struct property *prop;
	int count, i;

	prop = of_find_property(np, porp_name, NULL);
	if (!prop)
		return -EINVAL;

	if (!prop->value)
		return -ENODATA;

	count = of_property_count_u32_elems(np, porp_name);
	if (count < 0)
		return -EINVAL;

	if (count % 3)
		return -EINVAL;

	sel_table = kzalloc(sizeof(*sel_table) * (count / 3 + 1), GFP_KERNEL);
	if (!sel_table)
		return -ENOMEM;

	for (i = 0; i < count / 3; i++) {
		of_property_read_u32_index(np, porp_name, 3 * i,
					   &sel_table[i].min);
		of_property_read_u32_index(np, porp_name, 3 * i + 1,
					   &sel_table[i].max);
		of_property_read_u32_index(np, porp_name, 3 * i + 2,
					   &sel_table[i].sel);
	}
	sel_table[i].min = 0;
	sel_table[i].max = 0;
	sel_table[i].sel = LEAKAGE_TABLE_END;

	*table = sel_table;

	return 0;
}

static int rockchip_get_volt_sel(struct device_node *np, char *name,
				 int value, int *sel)
{
	struct volt_sel_table *table;
	int i, j = -1, ret;

	ret = rockchip_get_volt_sel_table(np, name, &table);
	if (ret)
		return -EINVAL;

	for (i = 0; table[i].sel != LEAKAGE_TABLE_END; i++) {
		if (value >= table[i].min)
			j = i;
	}
	if (j != -1)
		*sel = table[j].sel;
	else
		ret = -EINVAL;

	kfree(table);

	return ret;
}

static int rockchip_parse_pvtm_config(struct device_node *np,
				      struct pvtm_config *pvtm)
{
	if (!of_find_property(np, "rockchip,pvtm-voltage-sel", NULL))
		return -EINVAL;
	if (of_property_read_u32(np, "rockchip,pvtm-freq", &pvtm->freq))
		return -EINVAL;
	if (of_property_read_u32(np, "rockchip,pvtm-volt", &pvtm->volt))
		return -EINVAL;
	if (of_property_read_u32_array(np, "rockchip,pvtm-ch", pvtm->ch, 2))
		return -EINVAL;
	if (of_property_read_u32(np, "rockchip,pvtm-sample-time",
				 &pvtm->sample_time))
		return -EINVAL;
	if (of_property_read_u32(np, "rockchip,pvtm-number", &pvtm->num))
		return -EINVAL;
	if (of_property_read_u32(np, "rockchip,pvtm-error", &pvtm->err))
		return -EINVAL;
	if (of_property_read_u32(np, "rockchip,pvtm-ref-temp", &pvtm->ref_temp))
		return -EINVAL;
	if (of_property_read_u32_array(np, "rockchip,pvtm-temp-prop",
				       pvtm->temp_prop, 2))
		return -EINVAL;
	if (of_property_read_string(np, "rockchip,pvtm-thermal-zone",
				    &pvtm->tz_name))
		return -EINVAL;
	pvtm->tz = thermal_zone_get_zone_by_name(pvtm->tz_name);
	if (IS_ERR(pvtm->tz))
		return -EINVAL;
	if (!pvtm->tz->ops->get_temp)
		return -EINVAL;

	return 0;
}

static int rockchip_get_pvtm_specific_value(struct device *dev,
					    struct device_node *np,
					    struct clk *clk,
					    struct regulator *reg,
					    int *target_value)
{
	struct pvtm_config *pvtm;
	unsigned long old_freq;
	unsigned int old_volt;
	int cur_temp, diff_temp;
	int cur_value, total_value, avg_value, diff_value;
	int min_value, max_value;
	int ret = 0, i = 0, retry = 2;

	pvtm = kzalloc(sizeof(*pvtm), GFP_KERNEL);
	if (!pvtm)
		return -ENOMEM;

	ret = rockchip_parse_pvtm_config(np, pvtm);
	if (ret)
		goto pvtm_value_out;

	old_freq = clk_get_rate(clk);
	old_volt = regulator_get_voltage(reg);

	/*
	 * Set pvtm_freq to the lowest frequency in dts,
	 * so change frequency first.
	 */
	ret = clk_set_rate(clk, pvtm->freq * 1000);
	if (ret) {
		dev_err(dev, "Failed to set pvtm freq\n");
		goto pvtm_value_out;
	}

	ret = regulator_set_voltage(reg, pvtm->volt, pvtm->volt);
	if (ret) {
		dev_err(dev, "Failed to set pvtm_volt\n");
		goto restore_clk;
	}

	/* The first few values may be fluctuant, if error is too big, retry*/
	while (retry--) {
		total_value = 0;
		min_value = INT_MAX;
		max_value = 0;

		for (i = 0; i < pvtm->num; i++) {
			cur_value = rockchip_get_pvtm_value(pvtm->ch[0],
							    pvtm->ch[1],
							    pvtm->sample_time);
			if (!cur_value)
				goto resetore_volt;
			if (cur_value < min_value)
				min_value = cur_value;
			if (cur_value > max_value)
				max_value = cur_value;
			total_value += cur_value;
		}
		if (max_value - min_value < pvtm->err)
			break;
	}
	avg_value = total_value / pvtm->num;

	/*
	 * As pvtm is influenced by temperature, compute difference between
	 * current temperature and reference temperature
	 */
	pvtm->tz->ops->get_temp(pvtm->tz, &cur_temp);
	diff_temp = (cur_temp / 1000 - pvtm->ref_temp);
	diff_value = diff_temp *
		(diff_temp < 0 ? pvtm->temp_prop[0] : pvtm->temp_prop[1]);
	*target_value = avg_value + diff_value;

	dev_info(dev, "temp=%d, pvtm=%d (%d + %d)\n",
		 cur_temp, *target_value, avg_value, diff_value);

resetore_volt:
	regulator_set_voltage(reg, old_volt, old_volt);
restore_clk:
	clk_set_rate(clk, old_freq);
pvtm_value_out:
	kfree(pvtm);

	return ret;
}

int rockchip_of_get_lkg_scale_sel(struct device *dev, char *name)
{
	struct device_node *np;
	int leakage, volt_sel;
	int ret;

	np = of_parse_phandle(dev->of_node, "operating-points-v2", 0);
	if (!np) {
		dev_warn(dev, "OPP-v2 not supported\n");
		return -ENOENT;
	}

	ret = rockchip_get_efuse_value(np, name, &leakage);
	if (!ret) {
		dev_info(dev, "%s=%d\n", name, leakage);
		ret = rockchip_get_volt_sel(np, "rockchip,leakage-scaling-sel",
					    leakage, &volt_sel);
		if (!ret) {
			dev_info(dev, "%s-scale-sel=%d\n", name, volt_sel);
			return volt_sel;
		}
	} else {
		dev_info(dev, "get %s fail\n", name);
		ret = rockchip_get_volt_sel(np, "rockchip,leakage-scaling-sel",
					    0, &volt_sel);
		if (!ret) {
			dev_info(dev, "%s-scale-sel=%d\n", name, volt_sel);
			return volt_sel;
		}
	}

	return ret;
}
EXPORT_SYMBOL(rockchip_of_get_lkg_scale_sel);

int rockchip_of_get_lkg_volt_sel(struct device *dev, char *name)
{
	struct device_node *np;
	int leakage, volt_sel;
	int ret;

	np = of_parse_phandle(dev->of_node, "operating-points-v2", 0);
	if (!np) {
		dev_warn(dev, "OPP-v2 not supported\n");
		return -ENOENT;
	}

	ret = rockchip_get_efuse_value(np, name, &leakage);
	if (!ret) {
		dev_info(dev, "%s=%d\n", name, leakage);
		ret = rockchip_get_volt_sel(np, "rockchip,leakage-voltage-sel",
					    leakage, &volt_sel);
		if (!ret) {
			dev_info(dev, "%s-volt-sel=%d\n", name, volt_sel);
			return volt_sel;
		}
	}

	return ret;
}
EXPORT_SYMBOL(rockchip_of_get_lkg_volt_sel);

int rockchip_of_get_pvtm_volt_sel(struct device *dev,
				  char *clk_name,
				  char *reg_name)
{
	struct device_node *np;
	struct regulator *reg;
	struct clk *clk;
	int pvtm, volt_sel;
	int ret;

	np = of_parse_phandle(dev->of_node, "operating-points-v2", 0);
	if (!np) {
		dev_warn(dev, "OPP-v2 not supported\n");
		return -ENOENT;
	}

	clk = clk_get(dev, clk_name);
	if (IS_ERR_OR_NULL(clk)) {
		dev_err(dev, "Failed to get clk\n");
		return PTR_ERR(clk);
	}

	reg = regulator_get_optional(dev, reg_name);
	if (IS_ERR_OR_NULL(reg)) {
		dev_err(dev, "Failed to get reg\n");
		clk_put(clk);
		return PTR_ERR(reg);
	}

	ret = rockchip_get_pvtm_specific_value(dev, np, clk, reg, &pvtm);
	if (!ret)
		ret = rockchip_get_volt_sel(np, "rockchip,pvtm-voltage-sel",
					    pvtm, &volt_sel);

	regulator_put(reg);
	clk_put(clk);

	return ret ? ret : volt_sel;
}
EXPORT_SYMBOL(rockchip_of_get_pvtm_volt_sel);

static int rockchip_of_get_irdrop(struct device_node *np, unsigned long rate)
{
	int irdrop, ret;

	ret = rockchip_get_volt_sel(np, "rockchip,board-irdrop",
				    rate / 1000000, &irdrop);
	return ret ? ret : irdrop;
}

int rockchip_adjust_opp_by_irdrop(struct device *dev)
{
	struct dev_pm_opp *opp, *safe_opp = NULL;
	struct device_node *np;
	unsigned long rate;
	u32 max_volt = UINT_MAX;
	int evb_irdrop = 0, board_irdrop, delta_irdrop;
	int i, count, ret = 0;
	bool reach_max_volt = false;

	np = of_parse_phandle(dev->of_node, "operating-points-v2", 0);
	if (!np) {
		dev_warn(dev, "OPP-v2 not supported\n");
		return -ENOENT;
	}

	of_property_read_u32_index(np, "rockchip,max-volt", 0, &max_volt);
	of_property_read_u32_index(np, "rockchip,evb-irdrop", 0, &evb_irdrop);

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0)
	rcu_read_lock();
#endif
	count = dev_pm_opp_get_opp_count(dev);
	if (count <= 0) {
		ret = count ? count : -ENODATA;
		goto out;
	}

	for (i = 0, rate = 0; i < count; i++, rate++) {
		/* find next rate */
		opp = dev_pm_opp_find_freq_ceil(dev, &rate);
		if (IS_ERR(opp)) {
			ret = PTR_ERR(opp);
			goto out;
		}
		board_irdrop = rockchip_of_get_irdrop(np, opp->rate);
		if (IS_ERR_VALUE(board_irdrop))
			/* Assume it has the same IR-Drop as evb */
			delta_irdrop = 0;
		else
			delta_irdrop = board_irdrop - evb_irdrop;
		if ((opp->u_volt + delta_irdrop) <= max_volt) {
			opp->u_volt += delta_irdrop;
			opp->u_volt_min += delta_irdrop;
			opp->u_volt_max += delta_irdrop;
			if (!reach_max_volt)
				safe_opp = opp;
			if (opp->u_volt == max_volt)
				reach_max_volt = true;
		} else {
			opp->u_volt = max_volt;
			opp->u_volt_min = max_volt;
			opp->u_volt_max = max_volt;
		}
	}

	if (safe_opp && safe_opp != opp) {
		struct clk *clk = of_clk_get_by_name(np, NULL);

		if (!IS_ERR(clk)) {
			rockchip_pll_clk_adaptive_rate(clk, safe_opp->rate);
			clk_put(clk);
		}
	}
out:
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0)
	rcu_read_unlock();
#endif
	of_node_put(np);
	return ret;
}
EXPORT_SYMBOL(rockchip_adjust_opp_by_irdrop);
