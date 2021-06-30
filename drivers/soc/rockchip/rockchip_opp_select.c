/*
 * Copyright (c) 2017 Fuzhou Rockchip Electronics Co., Ltd
 *
 * SPDX-License-Identifier: GPL-2.0+
 */
#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <linux/devfreq.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/soc/rockchip/pvtm.h>
#include <linux/thermal.h>
#include <linux/pm_opp.h>
#include <linux/version.h>
#include <soc/rockchip/rockchip_opp_select.h>

#include "../../clk/rockchip/clk.h"
#include "../../opp/opp.h"
#include "../../devfreq/governor.h"

#define MAX_PROP_NAME_LEN	6
#define SEL_TABLE_END		~1
#define AVS_DELETE_OPP		0
#define AVS_SCALING_RATE	1

#define LEAKAGE_V1		1
#define LEAKAGE_V2		2
#define LEAKAGE_V3		3

#define to_thermal_opp_info(nb) container_of(nb, struct thermal_opp_info, \
					     thermal_nb)

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

struct lkg_conversion_table {
	int temp;
	int conv;
};

#define PVTM_CH_MAX	8
#define PVTM_SUB_CH_MAX	8

#define FRAC_BITS 10
#define int_to_frac(x) ((x) << FRAC_BITS)
#define frac_to_int(x) ((x) >> FRAC_BITS)

static int pvtm_value[PVTM_CH_MAX][PVTM_SUB_CH_MAX];
static int lkg_version;

/*
 * temp = temp * 10
 * conv = exp(-ln(1.2) / 5 * (temp - 23)) * 100
 */
static const struct lkg_conversion_table conv_table[] = {
	{ 200, 111 },
	{ 205, 109 },
	{ 210, 107 },
	{ 215, 105 },
	{ 220, 103 },
	{ 225, 101 },
	{ 230, 100 },
	{ 235, 98 },
	{ 240, 96 },
	{ 245, 94 },
	{ 250, 92 },
	{ 255, 91 },
	{ 260, 89 },
	{ 265, 88 },
	{ 270, 86 },
	{ 275, 84 },
	{ 280, 83 },
	{ 285, 81 },
	{ 290, 80 },
	{ 295, 78 },
	{ 300, 77 },
	{ 305, 76 },
	{ 310, 74 },
	{ 315, 73 },
	{ 320, 72 },
	{ 325, 70 },
	{ 330, 69 },
	{ 335, 68 },
	{ 340, 66 },
	{ 345, 65 },
	{ 350, 64 },
	{ 355, 63 },
	{ 360, 62 },
	{ 365, 61 },
	{ 370, 60 },
	{ 375, 58 },
	{ 380, 57 },
	{ 385, 56 },
	{ 390, 55 },
	{ 395, 54 },
	{ 400, 53 },
};

static int rockchip_nvmem_cell_read_common(struct device_node *np,
					   const char *cell_id,
					   void *val, size_t count)
{
	struct nvmem_cell *cell;
	void *buf;
	size_t len;

	cell = of_nvmem_cell_get(np, cell_id);
	if (IS_ERR(cell))
		return PTR_ERR(cell);

	buf = nvmem_cell_read(cell, &len);
	if (IS_ERR(buf)) {
		nvmem_cell_put(cell);
		return PTR_ERR(buf);
	}
	if (len != count) {
		kfree(buf);
		nvmem_cell_put(cell);
		return -EINVAL;
	}
	memcpy(val, buf, count);
	kfree(buf);
	nvmem_cell_put(cell);

	return 0;
}

int rockchip_nvmem_cell_read_u8(struct device_node *np, const char *cell_id,
				u8 *val)
{
	return rockchip_nvmem_cell_read_common(np, cell_id, val, sizeof(*val));
}
EXPORT_SYMBOL(rockchip_nvmem_cell_read_u8);

int rockchip_nvmem_cell_read_u16(struct device_node *np, const char *cell_id,
				 u16 *val)
{
	return rockchip_nvmem_cell_read_common(np, cell_id, val, sizeof(*val));
}
EXPORT_SYMBOL(rockchip_nvmem_cell_read_u16);

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
				    &pvtm->tz_name)) {
		if (of_property_read_string(np, "rockchip,thermal-zone",
					    &pvtm->tz_name))
			return -EINVAL;
	}
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
			if (cur_value <= 0) {
				ret = -EINVAL;
				goto resetore_volt;
			}
			if (cur_value < min_value)
				min_value = cur_value;
			if (cur_value > max_value)
				max_value = cur_value;
			total_value += cur_value;
		}
		if (max_value - min_value < pvtm->err)
			break;
	}
	if (!total_value || !pvtm->num) {
		ret = -EINVAL;
		goto resetore_volt;
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

/**
 * mul_frac() - multiply two fixed-point numbers
 * @x:	first multiplicand
 * @y:	second multiplicand
 *
 * Return: the result of multiplying two fixed-point numbers.  The
 * result is also a fixed-point number.
 */
static inline s64 mul_frac(s64 x, s64 y)
{
	return (x * y) >> FRAC_BITS;
}

static int temp_to_conversion_rate(int temp)
{
	int high, low, mid;

	low = 0;
	high = ARRAY_SIZE(conv_table) - 1;
	mid = (high + low) / 2;

	/* No temp available, return max conversion_rate */
	if (temp <= conv_table[low].temp)
		return conv_table[low].conv;
	if (temp >= conv_table[high].temp)
		return conv_table[high].conv;

	while (low <= high) {
		if (temp <= conv_table[mid].temp && temp >
		    conv_table[mid - 1].temp) {
			return conv_table[mid - 1].conv +
			    (conv_table[mid].conv - conv_table[mid - 1].conv) *
			    (temp - conv_table[mid - 1].temp) /
			    (conv_table[mid].temp - conv_table[mid - 1].temp);
		} else if (temp > conv_table[mid].temp) {
			low = mid + 1;
		} else {
			high = mid - 1;
		}
		mid = (low + high) / 2;
	}

	return 100;
}

static int rockchip_adjust_leakage(struct device *dev, struct device_node *np,
				   int *leakage)
{
	struct nvmem_cell *cell;
	u8 value = 0;
	u32 temp;
	int conversion;
	int ret;

	cell = of_nvmem_cell_get(np, "leakage_temp");
	if (IS_ERR(cell))
		goto next;
	nvmem_cell_put(cell);
	ret = rockchip_nvmem_cell_read_u8(np, "leakage_temp", &value);
	if (ret) {
		dev_err(dev, "Failed to get leakage temp\n");
		return -EINVAL;
	}
	/*
	 * The ambient temperature range: 20C to 40C
	 * In order to improve the precision, we do a conversion.
	 * The temp in efuse : temp_efuse = (temp - 20) / (40 - 20) * 63
	 * The ambient temp : temp = (temp_efuse / 63) * (40 - 20) + 20
	 * Reserves a decimal point : temp = temp * 10
	 */
	temp = value;
	temp = mul_frac((int_to_frac(temp) / 63 * 20 + int_to_frac(20)),
			int_to_frac(10));
	conversion = temp_to_conversion_rate(frac_to_int(temp));
	*leakage = *leakage * conversion / 100;

next:
	cell = of_nvmem_cell_get(np, "leakage_volt");
	if (IS_ERR(cell))
		return 0;
	nvmem_cell_put(cell);
	ret = rockchip_nvmem_cell_read_u8(np, "leakage_volt", &value);
	if (ret) {
		dev_err(dev, "Failed to get leakage volt\n");
		return -EINVAL;
	}
	/*
	 * if ft write leakage use 1.35v, need convert to 1v.
	 * leakage(1v) = leakage(1.35v) / 4
	 */
	if (value)
		*leakage = *leakage / 4;

	return 0;
}

static int rockchip_get_leakage_version(int *version)
{
	if (*version)
		return 0;

	if (of_machine_is_compatible("rockchip,rk3368"))
		*version = LEAKAGE_V2;
	else if (of_machine_is_compatible("rockchip,rv1126") ||
		 of_machine_is_compatible("rockchip,rv1109"))
		*version = LEAKAGE_V3;
	else
		*version = LEAKAGE_V1;

	return 0;
}

static int rockchip_get_leakage_v1(struct device *dev, struct device_node *np,
				   char *lkg_name, int *leakage)
{
	struct nvmem_cell *cell;
	int ret = 0;
	u8 value = 0;

	cell = of_nvmem_cell_get(np, "leakage");
	if (IS_ERR(cell)) {
		ret = rockchip_nvmem_cell_read_u8(np, lkg_name, &value);
	} else {
		nvmem_cell_put(cell);
		ret = rockchip_nvmem_cell_read_u8(np, "leakage", &value);
	}
	if (ret)
		dev_err(dev, "Failed to get %s\n", lkg_name);
	else
		*leakage = value;

	return ret;
}

static int rockchip_get_leakage_v2(struct device *dev, struct device_node *np,
				   char *lkg_name, int *leakage)
{
	int lkg = 0, ret = 0;

	if (rockchip_get_leakage_v1(dev, np, lkg_name, &lkg))
		return -EINVAL;

	ret = rockchip_adjust_leakage(dev, np, &lkg);
	if (ret)
		dev_err(dev, "Failed to adjust leakage, value=%d\n", lkg);
	else
		*leakage = lkg;

	return ret;
}

static int rockchip_get_leakage_v3(struct device *dev, struct device_node *np,
				   char *lkg_name, int *leakage)
{
	int lkg = 0;

	if (rockchip_get_leakage_v1(dev, np, lkg_name, &lkg))
		return -EINVAL;

	*leakage = (((lkg & 0xf8) >> 3) * 1000) + ((lkg & 0x7) * 125);

	return 0;
}

int rockchip_of_get_leakage(struct device *dev, char *lkg_name, int *leakage)
{
	struct device_node *np;
	int ret = -EINVAL;

	np = of_parse_phandle(dev->of_node, "operating-points-v2", 0);
	if (!np) {
		dev_warn(dev, "OPP-v2 not supported\n");
		return -ENOENT;
	}

	rockchip_get_leakage_version(&lkg_version);

	switch (lkg_version) {
	case LEAKAGE_V1:
		ret = rockchip_get_leakage_v1(dev, np, lkg_name, leakage);
		break;
	case LEAKAGE_V2:
		ret = rockchip_get_leakage_v2(dev, np, lkg_name, leakage);
		break;
	case LEAKAGE_V3:
		ret = rockchip_get_leakage_v3(dev, np, lkg_name, leakage);
		if (!ret) {
			/*
			 * round up to the nearest whole number for calculating
			 * static power,  it does not need to be precise.
			 */
			if (*leakage % 1000 > 500)
				*leakage = *leakage / 1000 + 1;
			else
				*leakage = *leakage / 1000;
		}
		break;
	default:
		break;
	}

	of_node_put(np);

	return ret;
}
EXPORT_SYMBOL(rockchip_of_get_leakage);

void rockchip_of_get_lkg_sel(struct device *dev, struct device_node *np,
			     char *lkg_name, int process,
			     int *volt_sel, int *scale_sel)
{
	struct property *prop = NULL;
	int leakage = -EINVAL, ret = 0;
	char name[NAME_MAX];

	rockchip_get_leakage_version(&lkg_version);

	switch (lkg_version) {
	case LEAKAGE_V1:
		ret = rockchip_get_leakage_v1(dev, np, lkg_name, &leakage);
		if (ret)
			return;
		dev_info(dev, "leakage=%d\n", leakage);
		break;
	case LEAKAGE_V2:
		ret = rockchip_get_leakage_v2(dev, np, lkg_name, &leakage);
		if (ret)
			return;
		dev_info(dev, "leakage=%d\n", leakage);
		break;
	case LEAKAGE_V3:
		ret = rockchip_get_leakage_v3(dev, np, lkg_name, &leakage);
		if (ret)
			return;
		dev_info(dev, "leakage=%d.%d\n", leakage / 1000,
			 leakage % 1000);
		break;
	default:
		return;
	}

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


static int rockchip_get_pvtm(struct device *dev, struct device_node *np,
			     char *reg_name)
{
	struct regulator *reg;
	struct clk *clk;
	unsigned int ch[2];
	int pvtm = 0;
	u16 tmp = 0;

	if (!rockchip_nvmem_cell_read_u16(np, "pvtm", &tmp) && tmp) {
		pvtm = 10 * tmp;
		dev_info(dev, "pvtm = %d, from nvmem\n", pvtm);
		return pvtm;
	}

	if (of_property_read_u32_array(np, "rockchip,pvtm-ch", ch, 2))
		return -EINVAL;

	if (ch[0] >= PVTM_CH_MAX || ch[1] >= PVTM_SUB_CH_MAX)
		return -EINVAL;

	if (pvtm_value[ch[0]][ch[1]]) {
		dev_info(dev, "pvtm = %d, form pvtm_value\n", pvtm_value[ch[0]][ch[1]]);
		return pvtm_value[ch[0]][ch[1]];
	}

	clk = clk_get(dev, NULL);
	if (IS_ERR_OR_NULL(clk)) {
		dev_warn(dev, "Failed to get clk\n");
		return PTR_ERR_OR_ZERO(clk);
	}

	reg = regulator_get_optional(dev, reg_name);
	if (IS_ERR_OR_NULL(reg)) {
		dev_warn(dev, "Failed to get reg\n");
		clk_put(clk);
		return PTR_ERR_OR_ZERO(reg);
	}

	rockchip_get_pvtm_specific_value(dev, np, clk, reg, &pvtm);

	regulator_put(reg);
	clk_put(clk);

	return pvtm;
}

void rockchip_of_get_pvtm_sel(struct device *dev, struct device_node *np,
			      char *reg_name, int process,
			      int *volt_sel, int *scale_sel)
{
	struct property *prop = NULL;
	char name[NAME_MAX];
	int pvtm, ret;

	pvtm = rockchip_get_pvtm(dev, np, reg_name);
	if (pvtm <= 0)
		return;

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
		return;
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

void rockchip_of_get_bin_volt_sel(struct device *dev, struct device_node *np,
				  int bin, int *bin_volt_sel)
{
	int ret = 0;

	if (!bin_volt_sel || bin < 0)
		return;

	ret = rockchip_get_bin_sel(np, "rockchip,bin-voltage-sel",
				   bin, bin_volt_sel);
	if (!ret)
		dev_info(dev, "bin-volt-sel=%d\n", *bin_volt_sel);
}
EXPORT_SYMBOL(rockchip_of_get_bin_volt_sel);

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
	int bin_volt_sel = -EINVAL;

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
	rockchip_of_get_bin_volt_sel(dev, np, bin, &bin_volt_sel);
	if (scale)
		*scale = max3(lkg_scale, pvtm_scale, bin_scale);
	if (volt_sel) {
		if (bin_volt_sel >= 0)
			*volt_sel = bin_volt_sel;
		else
			*volt_sel = max(lkg_volt_sel, pvtm_volt_sel);
	}

	of_node_put(np);
}
EXPORT_SYMBOL(rockchip_get_scale_volt_sel);

struct opp_table *rockchip_set_opp_prop_name(struct device *dev, int process,
					     int volt_sel)
{
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
		return NULL;
	}

	return dev_pm_opp_set_prop_name(dev, name);
}
EXPORT_SYMBOL(rockchip_set_opp_prop_name);

static int rockchip_adjust_opp_by_irdrop(struct device *dev,
					 struct device_node *np,
					 unsigned long *safe_rate,
					 unsigned long *max_rate)
{
	struct sel_table *irdrop_table = NULL;
	struct opp_table *opp_table;
	struct dev_pm_opp *opp;
	int evb_irdrop = 0, board_irdrop, delta_irdrop;
	int tmp_safe_rate = 0, opp_rate, i, ret = 0;
	u32 max_volt = UINT_MAX;
	bool reach_max_volt = false;

	of_property_read_u32_index(np, "rockchip,max-volt", 0, &max_volt);
	of_property_read_u32_index(np, "rockchip,evb-irdrop", 0, &evb_irdrop);
	rockchip_get_sel_table(np, "rockchip,board-irdrop", &irdrop_table);

	opp_table = dev_pm_opp_get_opp_table(dev);
	if (!opp_table) {
		ret =  -ENOMEM;
		goto out;
	}

	mutex_lock(&opp_table->lock);
	list_for_each_entry(opp, &opp_table->opp_list, node) {
		if (!irdrop_table) {
			delta_irdrop = 0;
		} else {
			opp_rate = opp->rate / 1000000;
			board_irdrop = -EINVAL;
			for (i = 0; irdrop_table[i].sel != SEL_TABLE_END; i++) {
				if (opp_rate >= irdrop_table[i].min)
					board_irdrop = irdrop_table[i].sel;
			}
			if (board_irdrop == -EINVAL)
				delta_irdrop = 0;
			else
				delta_irdrop = board_irdrop - evb_irdrop;
		}
		if ((opp->supplies[0].u_volt + delta_irdrop) <= max_volt) {
			opp->supplies[0].u_volt += delta_irdrop;
			opp->supplies[0].u_volt_min += delta_irdrop;
			if (opp->supplies[0].u_volt_max + delta_irdrop <=
			    max_volt)
				opp->supplies[0].u_volt_max += delta_irdrop;
			else
				opp->supplies[0].u_volt_max = max_volt;
			if (!reach_max_volt)
				tmp_safe_rate = opp->rate;
			if (opp->supplies[0].u_volt == max_volt)
				reach_max_volt = true;
		} else {
			opp->supplies[0].u_volt = max_volt;
			opp->supplies[0].u_volt_min = max_volt;
			opp->supplies[0].u_volt_max = max_volt;
		}
		if (max_rate)
			*max_rate = opp->rate;
		if (safe_rate && tmp_safe_rate != opp->rate)
			*safe_rate = tmp_safe_rate;
	}
	mutex_unlock(&opp_table->lock);

	dev_pm_opp_put_opp_table(opp_table);
out:
	kfree(irdrop_table);

	return ret;
}

static void rockchip_adjust_opp_by_mbist_vmin(struct device *dev,
					      struct device_node *np)
{
	struct opp_table *opp_table;
	struct dev_pm_opp *opp;
	u32 vmin = 0;
	u8 index = 0;

	if (rockchip_nvmem_cell_read_u8(np, "mbist-vmin", &index))
		return;

	if (!index)
		return;

	if (of_property_read_u32_index(np, "mbist-vmin", index-1, &vmin))
		return;

	opp_table = dev_pm_opp_get_opp_table(dev);
	if (!opp_table)
		return;

	mutex_lock(&opp_table->lock);
	list_for_each_entry(opp, &opp_table->opp_list, node) {
		if (opp->supplies->u_volt < vmin) {
			opp->supplies->u_volt = vmin;
			opp->supplies->u_volt_min = vmin;
		}
	}
	mutex_unlock(&opp_table->lock);
}

static int rockchip_adjust_opp_table(struct device *dev,
				     unsigned long scale_rate)
{
	struct dev_pm_opp *opp;
	unsigned long rate;
	int i, count, ret = 0;

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
		dev_pm_opp_put(opp);
	}
out:
	return ret;
}

int rockchip_adjust_power_scale(struct device *dev, int scale)
{
	struct device_node *np;
	struct clk *clk;
	unsigned long safe_rate = 0, max_rate = 0;
	int irdrop_scale = 0, opp_scale = 0;
	u32 target_scale, avs = 0, avs_scale = 0;
	long scale_rate = 0;
	int ret = 0;

	np = of_parse_phandle(dev->of_node, "operating-points-v2", 0);
	if (!np) {
		dev_warn(dev, "OPP-v2 not supported\n");
		return -ENOENT;
	}
	of_property_read_u32(np, "rockchip,avs-enable", &avs);
	of_property_read_u32(np, "rockchip,avs", &avs);
	of_property_read_u32(np, "rockchip,avs-scale", &avs_scale);
	rockchip_adjust_opp_by_mbist_vmin(dev, np);
	rockchip_adjust_opp_by_irdrop(dev, np, &safe_rate, &max_rate);

	dev_info(dev, "avs=%d\n", avs);
	clk = of_clk_get_by_name(np, NULL);
	if (IS_ERR(clk)) {
		if (!safe_rate)
			goto out_np;
		dev_dbg(dev, "Failed to get clk, safe_rate=%lu\n", safe_rate);
		ret = rockchip_adjust_opp_table(dev, safe_rate);
		if (ret)
			dev_err(dev, "Failed to adjust opp table\n");
		goto out_np;
	}

	if (safe_rate)
		irdrop_scale = rockchip_pll_clk_rate_to_scale(clk, safe_rate);
	if (max_rate)
		opp_scale = rockchip_pll_clk_rate_to_scale(clk, max_rate);
	target_scale = max(irdrop_scale, scale);
	if (target_scale <= 0)
		goto out_clk;
	dev_dbg(dev, "target_scale=%d, irdrop_scale=%d, scale=%d\n",
		target_scale, irdrop_scale, scale);

	if (avs == AVS_SCALING_RATE) {
		ret = rockchip_pll_clk_adaptive_scaling(clk, target_scale);
		if (ret)
			dev_err(dev, "Failed to adaptive scaling\n");
		if (opp_scale >= avs_scale)
			goto out_clk;
		dev_info(dev, "avs-scale=%d, opp-scale=%d\n", avs_scale,
			 opp_scale);
		scale_rate = rockchip_pll_clk_scale_to_rate(clk, avs_scale);
		if (scale_rate <= 0) {
			dev_err(dev, "Failed to get avs scale rate, %d\n",
				avs_scale);
			goto out_clk;
		}
		dev_dbg(dev, "scale_rate=%lu\n", scale_rate);
		ret = rockchip_adjust_opp_table(dev, scale_rate);
		if (ret)
			dev_err(dev, "Failed to adjust opp table\n");
	} else if (avs == AVS_DELETE_OPP) {
		if (opp_scale >= target_scale)
			goto out_clk;
		dev_info(dev, "target_scale=%d, opp-scale=%d\n", target_scale,
			 opp_scale);
		scale_rate = rockchip_pll_clk_scale_to_rate(clk, target_scale);
		if (scale_rate <= 0) {
			dev_err(dev, "Failed to get scale rate, %d\n",
				target_scale);
			goto out_clk;
		}
		dev_dbg(dev, "scale_rate=%lu\n", scale_rate);
		ret = rockchip_adjust_opp_table(dev, scale_rate);
		if (ret)
			dev_err(dev, "Failed to adjust opp table\n");
	}

out_clk:
	clk_put(clk);
out_np:
	of_node_put(np);

	return ret;
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
	np = of_parse_phandle(dev->of_node, "operating-points-v2", 0);
	if (!np) {
		dev_dbg(dev, "Failed to find operating-points-v2\n");
		return -ENOENT;
	}
	of_node_put(np);

	rockchip_get_soc_info(dev, matches, &bin, &process);
	rockchip_get_scale_volt_sel(dev, lkg_name, reg_name, bin, process,
				    &scale, &volt_sel);
	rockchip_set_opp_prop_name(dev, process, volt_sel);
	ret = dev_pm_opp_of_add_table(dev);
	if (ret) {
		dev_err(dev, "Invalid operating-points in device tree.\n");
		return ret;
	}
	rockchip_adjust_power_scale(dev, scale);

	return 0;
}
EXPORT_SYMBOL(rockchip_init_opp_table);

MODULE_DESCRIPTION("ROCKCHIP OPP Select");
MODULE_AUTHOR("Finley Xiao <finley.xiao@rock-chips.com>, Liang Chen <cl@rock-chips.com>");
MODULE_LICENSE("GPL");
