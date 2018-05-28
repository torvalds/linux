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

#define MAX_PROP_NAME_LEN	6
#define SEL_TABLE_END		~1
#define LEAKAGE_INVALID		0xff

struct sel_table {
	int min;
	int max;
	int sel;
};

struct bin_sel_table {
	int bin;
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

#define PVTM_CH_MAX	8
#define PVTM_SUB_CH_MAX	8
static int pvtm_value[PVTM_CH_MAX][PVTM_SUB_CH_MAX];

int rockchip_get_efuse_value(struct device_node *np, char *porp_name,
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
EXPORT_SYMBOL(rockchip_get_efuse_value);

static int rockchip_get_sel_table(struct device_node *np, char *porp_name,
				  struct sel_table **table)
{
	struct sel_table *sel_table;
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
	sel_table[i].sel = SEL_TABLE_END;

	*table = sel_table;

	return 0;
}

static int rockchip_get_bin_sel_table(struct device_node *np, char *porp_name,
				      struct bin_sel_table **table)
{
	struct bin_sel_table *sel_table;
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

	if (count % 2)
		return -EINVAL;

	sel_table = kzalloc(sizeof(*sel_table) * (count / 2 + 1), GFP_KERNEL);
	if (!sel_table)
		return -ENOMEM;

	for (i = 0; i < count / 2; i++) {
		of_property_read_u32_index(np, porp_name, 2 * i,
					   &sel_table[i].bin);
		of_property_read_u32_index(np, porp_name, 2 * i + 1,
					   &sel_table[i].sel);
	}

	sel_table[i].bin = 0;
	sel_table[i].sel = SEL_TABLE_END;

	*table = sel_table;

	return 0;
}

static int rockchip_get_sel(struct device_node *np, char *name,
			    int value, int *sel)
{
	struct sel_table *table = NULL;
	int i, ret = -EINVAL;

	if (!sel)
		return -EINVAL;

	if (rockchip_get_sel_table(np, name, &table))
		return -EINVAL;

	for (i = 0; table[i].sel != SEL_TABLE_END; i++) {
		if (value >= table[i].min) {
			*sel = table[i].sel;
			ret = 0;
		}
	}
	kfree(table);

	return ret;
}

static int rockchip_get_bin_sel(struct device_node *np, char *name,
				int value, int *sel)
{
	struct bin_sel_table *table = NULL;
	int i, ret = -EINVAL;

	if (!sel)
		return -EINVAL;

	if (rockchip_get_bin_sel_table(np, name, &table))
		return -EINVAL;

	for (i = 0; table[i].sel != SEL_TABLE_END; i++) {
		if (value == table[i].bin) {
			*sel = table[i].sel;
			ret = 0;
			break;
		}
	}
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
	if (pvtm->ch[0] >= PVTM_CH_MAX || pvtm->ch[1] >= PVTM_SUB_CH_MAX)
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
	unsigned int old_volt, ch[2];
	int cur_temp, diff_temp;
	int cur_value, total_value, avg_value, diff_value;
	int min_value, max_value;
	int ret = 0, i = 0, retry = 2;

	if (of_property_read_u32_array(np, "rockchip,pvtm-ch", ch, 2))
		return -EINVAL;

	if (ch[0] >= PVTM_CH_MAX || ch[1] >= PVTM_SUB_CH_MAX)
		return -EINVAL;

	if (pvtm_value[ch[0]][ch[1]]) {
		*target_value = pvtm_value[ch[0]][ch[1]];
		return 0;
	}

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

	pvtm_value[pvtm->ch[0]][pvtm->ch[1]] = *target_value;

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

void rockchip_of_get_lkg_sel(struct device *dev, struct device_node *np,
			     char *lkg_name, int process,
			     int *volt_sel, int *scale_sel)
{
	struct property *prop = NULL;
	struct nvmem_cell *cell;
	int leakage = -EINVAL, ret;
	char name[NAME_MAX];

	cell = of_nvmem_cell_get(np, "leakage");
	if (IS_ERR(cell)) {
		ret = rockchip_get_efuse_value(np, lkg_name, &leakage);
	} else {
		nvmem_cell_put(cell);
		ret = rockchip_get_efuse_value(np, "leakage", &leakage);
	}
	if (ret) {
		dev_err(dev, "Failed to get leakage\n");
		return;
	}
	dev_info(dev, "leakage=%d\n", leakage);

	if (!volt_sel)
		goto next;
	if (process >= 0) {
		snprintf(name, sizeof(name),
			 "rockchip,p%d-leakage-voltage-sel", process);
		prop = of_find_property(np, name, NULL);
	}
	if (!prop)
		sprintf(name, "rockchip,leakage-voltage-sel");
	ret = rockchip_get_sel(np, name, leakage, volt_sel);
	if (!ret)
		dev_info(dev, "leakage-volt-sel=%d\n", *volt_sel);

next:
	if (!scale_sel)
		return;
	if (process >= 0) {
		snprintf(name, sizeof(name),
			 "rockchip,p%d-leakage-scaling-sel", process);
		prop = of_find_property(np, name, NULL);
	}
	if (!prop)
		sprintf(name, "rockchip,leakage-scaling-sel");
	ret = rockchip_get_sel(np, name, leakage, scale_sel);
	if (!ret)
		dev_info(dev, "leakage-scale=%d\n", *scale_sel);
}
EXPORT_SYMBOL(rockchip_of_get_lkg_sel);

void rockchip_of_get_pvtm_sel(struct device *dev, struct device_node *np,
			      char *reg_name, int process,
			      int *volt_sel, int *scale_sel)
{
	struct property *prop = NULL;
	struct regulator *reg;
	struct clk *clk;
	int pvtm = -EINVAL, ret;
	char name[NAME_MAX];

	clk = clk_get(dev, NULL);
	if (IS_ERR_OR_NULL(clk)) {
		dev_warn(dev, "Failed to get clk\n");
		return;
	}

	reg = regulator_get_optional(dev, reg_name);
	if (IS_ERR_OR_NULL(reg)) {
		dev_warn(dev, "Failed to get reg\n");
		goto clk_err;
	}

	ret = rockchip_get_pvtm_specific_value(dev, np, clk, reg, &pvtm);
	if (ret) {
		dev_err(dev, "Failed to get pvtm\n");
		goto out;
	}

	if (!volt_sel)
		goto next;
	if (process >= 0) {
		snprintf(name, sizeof(name),
			 "rockchip,p%d-pvtm-voltage-sel", process);
		prop = of_find_property(np, name, NULL);
	}
	if (!prop)
		sprintf(name, "rockchip,pvtm-voltage-sel");
	ret = rockchip_get_sel(np, name, pvtm, volt_sel);
	if (!ret && volt_sel)
		dev_info(dev, "pvtm-volt-sel=%d\n", *volt_sel);

next:
	if (!scale_sel)
		goto out;
	if (process >= 0) {
		snprintf(name, sizeof(name),
			 "rockchip,p%d-pvtm-scaling-sel", process);
		prop = of_find_property(np, name, NULL);
	}
	if (!prop)
		sprintf(name, "rockchip,pvtm-scaling-sel");
	ret = rockchip_get_sel(np, name, pvtm, scale_sel);
	if (!ret)
		dev_info(dev, "pvtm-scale=%d\n", *scale_sel);

out:
	regulator_put(reg);
clk_err:
	clk_put(clk);
}
EXPORT_SYMBOL(rockchip_of_get_pvtm_sel);

void rockchip_of_get_bin_sel(struct device *dev, struct device_node *np,
			     int bin, int *scale_sel)
{
	int ret = 0;

	if (!scale_sel || bin < 0)
		return;

	ret = rockchip_get_bin_sel(np, "rockchip,bin-scaling-sel",
				   bin, scale_sel);
	if (!ret)
		dev_info(dev, "bin-scale=%d\n", *scale_sel);
}
EXPORT_SYMBOL(rockchip_of_get_bin_sel);

void rockchip_get_soc_info(struct device *dev,
			   const struct of_device_id *matches,
			   int *bin, int *process)
{
	const struct of_device_id *match;
	struct device_node *np;
	struct device_node *node;
	int (*get_soc_info)(struct device *dev, struct device_node *np,
			    int *bin, int *process);
	int ret = 0;

	if (!matches)
		return;

	np = of_parse_phandle(dev->of_node, "operating-points-v2", 0);
	if (!np) {
		dev_warn(dev, "OPP-v2 not supported\n");
		return;
	}

	node = of_find_node_by_path("/");
	match = of_match_node(matches, node);
	if (match && match->data) {
		get_soc_info = match->data;
		ret = get_soc_info(dev, np, bin, process);
		if (ret)
			dev_err(dev, "Failed to get soc info\n");
	}

	of_node_put(node);
	of_node_put(np);
}
EXPORT_SYMBOL(rockchip_get_soc_info);

void rockchip_get_scale_volt_sel(struct device *dev, char *lkg_name,
				 char *reg_name, int bin, int process,
				 int *scale, int *volt_sel)
{
	struct device_node *np;
	int lkg_scale = 0, pvtm_scale = 0, bin_scale = 0;
	int lkg_volt_sel = -EINVAL, pvtm_volt_sel = -EINVAL;

	np = of_parse_phandle(dev->of_node, "operating-points-v2", 0);
	if (!np) {
		dev_warn(dev, "OPP-v2 not supported\n");
		return;
	}

	rockchip_of_get_lkg_sel(dev, np, lkg_name, process,
				&lkg_volt_sel, &lkg_scale);
	rockchip_of_get_pvtm_sel(dev, np, reg_name, process,
				 &pvtm_volt_sel, &pvtm_scale);
	rockchip_of_get_bin_sel(dev, np, bin, &bin_scale);
	if (scale)
		*scale = max3(lkg_scale, pvtm_scale, bin_scale);
	if (volt_sel)
		*volt_sel = max(lkg_volt_sel, pvtm_volt_sel);

	of_node_put(np);
}
EXPORT_SYMBOL(rockchip_get_scale_volt_sel);

int rockchip_set_opp_info(struct device *dev, int process, int volt_sel)
{
	int ret = 0;
	char name[MAX_PROP_NAME_LEN];

	if (process >= 0) {
		if (volt_sel >= 0)
			snprintf(name, MAX_PROP_NAME_LEN, "P%d-L%d",
				 process, volt_sel);
		else
			snprintf(name, MAX_PROP_NAME_LEN, "P%d", process);
	} else if (volt_sel >= 0) {
		snprintf(name, MAX_PROP_NAME_LEN, "L%d", volt_sel);
	} else {
		return 0;
	}

	ret = dev_pm_opp_set_prop_name(dev, name);
	if (ret)
		dev_err(dev, "Failed to set prop name\n");

	return ret;
}
EXPORT_SYMBOL(rockchip_set_opp_info);

static int rockchip_of_get_irdrop(struct device_node *np, unsigned long rate)
{
	int irdrop, ret;

	ret = rockchip_get_sel(np, "rockchip,board-irdrop", rate / 1000000,
			       &irdrop);
	return ret ? ret : irdrop;
}

static int rockchip_adjust_opp_by_irdrop(struct device *dev,
					 struct device_node *np,
					 int *irdrop_scale,
					 int *opp_scale)
{
	struct dev_pm_opp *opp, *safe_opp = NULL;
	struct clk *clk;
	unsigned long rate;
	u32 max_volt = UINT_MAX;
	int evb_irdrop = 0, board_irdrop, delta_irdrop;
	int i, count, ret = 0;
	bool reach_max_volt = false;

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

	clk = of_clk_get_by_name(np, NULL);
	if (IS_ERR(clk))
		goto out;
	if (safe_opp && safe_opp != opp && irdrop_scale) {
		*irdrop_scale = rockchip_pll_clk_rate_to_scale(clk,
							       safe_opp->rate);
		dev_info(dev, "irdrop-scale=%d\n", *irdrop_scale);
	}
	if (opp_scale)
		*opp_scale = rockchip_pll_clk_rate_to_scale(clk, opp->rate);
	clk_put(clk);

out:
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0)
	rcu_read_unlock();
#endif
	return ret;
}

static int rockchip_adjust_opp_table(struct device *dev,
				     unsigned long scale_rate)
{
	struct dev_pm_opp *opp;
	unsigned long rate;
	int i, count, ret = 0;

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
		if (opp->rate > scale_rate)
			dev_pm_opp_remove(dev, opp->rate);
	}
out:
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0)
	rcu_read_unlock();
#endif
	return ret;
}

int rockchip_adjust_power_scale(struct device *dev, int scale)
{
	struct device_node *np;
	struct clk *clk;
	int irdrop_scale = 0, opp_scale = 0;
	u32 target_scale, avs_enable = 0, avs_scale = 0;
	long scale_rate = 0;
	int ret = 0;

	np = of_parse_phandle(dev->of_node, "operating-points-v2", 0);
	if (!np) {
		dev_warn(dev, "OPP-v2 not supported\n");
		return -ENOENT;
	}
	of_property_read_u32(np, "rockchip,avs-enable", &avs_enable);
	of_property_read_u32(np, "rockchip,avs-scale", &avs_scale);

	rockchip_adjust_opp_by_irdrop(dev, np, &irdrop_scale, &opp_scale);
	target_scale = max(irdrop_scale, scale);
	if (target_scale <= 0)
		return 0;
	dev_info(dev, "target-scale=%d\n", target_scale);

	clk = of_clk_get_by_name(np, NULL);
	if (IS_ERR(clk)) {
		dev_err(dev, "Failed to get opp clk\n");
		goto np_err;
	}

	if (avs_enable) {
		ret = rockchip_pll_clk_adaptive_scaling(clk, target_scale);
		if (ret)
			dev_err(dev, "Failed to adaptive scaling\n");
		if (opp_scale < avs_scale) {
			dev_info(dev, "avs-scale=%d, opp-scale=%d\n",
				 avs_scale, opp_scale);
			scale_rate = rockchip_pll_clk_scale_to_rate(clk,
								    avs_scale);
			if (scale_rate <= 0) {
				dev_err(dev,
					"Failed to get avs scale rate, %d\n",
					avs_scale);
				goto clk_err;
			}
			dev_info(dev, "avs scale_rate=%lu\n", scale_rate);
			ret = rockchip_adjust_opp_table(dev, scale_rate);
			if (ret)
				dev_err(dev, "Failed to adjust opp table\n");
		}
	} else {
		if (opp_scale >= target_scale)
			goto clk_err;
		scale_rate = rockchip_pll_clk_scale_to_rate(clk, target_scale);
		if (scale_rate <= 0) {
			dev_err(dev, "Failed to get scale rate, %d\n",
				target_scale);
			goto clk_err;
		}
		dev_info(dev, "scale_rate=%lu\n", scale_rate);
		ret = rockchip_adjust_opp_table(dev, scale_rate);
		if (ret)
			dev_err(dev, "Failed to adjust opp table\n");
	}

clk_err:
	clk_put(clk);
np_err:
	of_node_put(np);

	return 0;
}
EXPORT_SYMBOL(rockchip_adjust_power_scale);

int rockchip_init_opp_table(struct device *dev,
			    const struct of_device_id *matches,
			    char *lkg_name, char *reg_name)
{
	struct device_node *np;
	int bin = -EINVAL, process = -EINVAL;
	int scale = 0, volt_sel = -EINVAL;
	int ret = 0;

	/* Get OPP descriptor node */
	np = _of_get_opp_desc_node(dev);
	if (!np) {
		dev_dbg(dev, "Failed to find operating-points-v2\n");
		return -ENOENT;
	}
	of_node_put(np);

	rockchip_get_soc_info(dev, NULL, &bin, &process);
	rockchip_get_scale_volt_sel(dev, lkg_name, reg_name, bin, process,
				    &scale, &volt_sel);
	rockchip_set_opp_info(dev, process, volt_sel);
	ret = dev_pm_opp_of_add_table(dev);
	if (ret) {
		dev_err(dev, "Invalid operating-points in device tree.\n");
		return ret;
	}
	rockchip_adjust_power_scale(dev, scale);

	return 0;
}
EXPORT_SYMBOL(rockchip_init_opp_table);
