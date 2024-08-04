// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2018-2021 Intel Corporation

#include <linux/auxiliary_bus.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/hwmon.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/peci.h>
#include <linux/peci-cpu.h>
#include <linux/units.h>

#include "common.h"

#define CORE_NUMS_MAX		64

#define BASE_CHANNEL_NUMS	5
#define CPUTEMP_CHANNEL_NUMS	(BASE_CHANNEL_NUMS + CORE_NUMS_MAX)

#define TEMP_TARGET_FAN_TEMP_MASK	GENMASK(15, 8)
#define TEMP_TARGET_REF_TEMP_MASK	GENMASK(23, 16)
#define TEMP_TARGET_TJ_OFFSET_MASK	GENMASK(29, 24)

#define DTS_MARGIN_MASK		GENMASK(15, 0)
#define PCS_MODULE_TEMP_MASK	GENMASK(15, 0)

struct resolved_cores_reg {
	u8 bus;
	u8 dev;
	u8 func;
	u8 offset;
};

struct cpu_info {
	struct resolved_cores_reg *reg;
	u8 min_peci_revision;
	s32 (*thermal_margin_to_millidegree)(u16 val);
};

struct peci_temp_target {
	s32 tcontrol;
	s32 tthrottle;
	s32 tjmax;
	struct peci_sensor_state state;
};

enum peci_temp_target_type {
	tcontrol_type,
	tthrottle_type,
	tjmax_type,
	crit_hyst_type,
};

struct peci_cputemp {
	struct peci_device *peci_dev;
	struct device *dev;
	const char *name;
	const struct cpu_info *gen_info;
	struct {
		struct peci_temp_target target;
		struct peci_sensor_data die;
		struct peci_sensor_data dts;
		struct peci_sensor_data core[CORE_NUMS_MAX];
	} temp;
	const char **coretemp_label;
	DECLARE_BITMAP(core_mask, CORE_NUMS_MAX);
};

enum cputemp_channels {
	channel_die,
	channel_dts,
	channel_tcontrol,
	channel_tthrottle,
	channel_tjmax,
	channel_core,
};

static const char * const cputemp_label[BASE_CHANNEL_NUMS] = {
	"Die",
	"DTS",
	"Tcontrol",
	"Tthrottle",
	"Tjmax",
};

static int update_temp_target(struct peci_cputemp *priv)
{
	s32 tthrottle_offset, tcontrol_margin;
	u32 pcs;
	int ret;

	if (!peci_sensor_need_update(&priv->temp.target.state))
		return 0;

	ret = peci_pcs_read(priv->peci_dev, PECI_PCS_TEMP_TARGET, 0, &pcs);
	if (ret)
		return ret;

	priv->temp.target.tjmax =
		FIELD_GET(TEMP_TARGET_REF_TEMP_MASK, pcs) * MILLIDEGREE_PER_DEGREE;

	tcontrol_margin = FIELD_GET(TEMP_TARGET_FAN_TEMP_MASK, pcs);
	tcontrol_margin = sign_extend32(tcontrol_margin, 7) * MILLIDEGREE_PER_DEGREE;
	priv->temp.target.tcontrol = priv->temp.target.tjmax - tcontrol_margin;

	tthrottle_offset = FIELD_GET(TEMP_TARGET_TJ_OFFSET_MASK, pcs) * MILLIDEGREE_PER_DEGREE;
	priv->temp.target.tthrottle = priv->temp.target.tjmax - tthrottle_offset;

	peci_sensor_mark_updated(&priv->temp.target.state);

	return 0;
}

static int get_temp_target(struct peci_cputemp *priv, enum peci_temp_target_type type, long *val)
{
	int ret;

	mutex_lock(&priv->temp.target.state.lock);

	ret = update_temp_target(priv);
	if (ret)
		goto unlock;

	switch (type) {
	case tcontrol_type:
		*val = priv->temp.target.tcontrol;
		break;
	case tthrottle_type:
		*val = priv->temp.target.tthrottle;
		break;
	case tjmax_type:
		*val = priv->temp.target.tjmax;
		break;
	case crit_hyst_type:
		*val = priv->temp.target.tjmax - priv->temp.target.tcontrol;
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}
unlock:
	mutex_unlock(&priv->temp.target.state.lock);

	return ret;
}

/*
 * Error codes:
 *   0x8000: General sensor error
 *   0x8001: Reserved
 *   0x8002: Underflow on reading value
 *   0x8003-0x81ff: Reserved
 */
static bool dts_valid(u16 val)
{
	return val < 0x8000 || val > 0x81ff;
}

/*
 * Processors return a value of DTS reading in S10.6 fixed point format
 * (16 bits: 10-bit signed magnitude, 6-bit fraction).
 */
static s32 dts_ten_dot_six_to_millidegree(u16 val)
{
	return sign_extend32(val, 15) * MILLIDEGREE_PER_DEGREE / 64;
}

/*
 * For older processors, thermal margin reading is returned in S8.8 fixed
 * point format (16 bits: 8-bit signed magnitude, 8-bit fraction).
 */
static s32 dts_eight_dot_eight_to_millidegree(u16 val)
{
	return sign_extend32(val, 15) * MILLIDEGREE_PER_DEGREE / 256;
}

static int get_die_temp(struct peci_cputemp *priv, long *val)
{
	int ret = 0;
	long tjmax;
	u16 temp;

	mutex_lock(&priv->temp.die.state.lock);
	if (!peci_sensor_need_update(&priv->temp.die.state))
		goto skip_update;

	ret = peci_temp_read(priv->peci_dev, &temp);
	if (ret)
		goto err_unlock;

	if (!dts_valid(temp)) {
		ret = -EIO;
		goto err_unlock;
	}

	ret = get_temp_target(priv, tjmax_type, &tjmax);
	if (ret)
		goto err_unlock;

	priv->temp.die.value = (s32)tjmax + dts_ten_dot_six_to_millidegree(temp);

	peci_sensor_mark_updated(&priv->temp.die.state);

skip_update:
	*val = priv->temp.die.value;
err_unlock:
	mutex_unlock(&priv->temp.die.state.lock);
	return ret;
}

static int get_dts(struct peci_cputemp *priv, long *val)
{
	int ret = 0;
	u16 thermal_margin;
	long tcontrol;
	u32 pcs;

	mutex_lock(&priv->temp.dts.state.lock);
	if (!peci_sensor_need_update(&priv->temp.dts.state))
		goto skip_update;

	ret = peci_pcs_read(priv->peci_dev, PECI_PCS_THERMAL_MARGIN, 0, &pcs);
	if (ret)
		goto err_unlock;

	thermal_margin = FIELD_GET(DTS_MARGIN_MASK, pcs);
	if (!dts_valid(thermal_margin)) {
		ret = -EIO;
		goto err_unlock;
	}

	ret = get_temp_target(priv, tcontrol_type, &tcontrol);
	if (ret)
		goto err_unlock;

	/* Note that the tcontrol should be available before calling it */
	priv->temp.dts.value =
		(s32)tcontrol - priv->gen_info->thermal_margin_to_millidegree(thermal_margin);

	peci_sensor_mark_updated(&priv->temp.dts.state);

skip_update:
	*val = priv->temp.dts.value;
err_unlock:
	mutex_unlock(&priv->temp.dts.state.lock);
	return ret;
}

static int get_core_temp(struct peci_cputemp *priv, int core_index, long *val)
{
	int ret = 0;
	u16 core_dts_margin;
	long tjmax;
	u32 pcs;

	mutex_lock(&priv->temp.core[core_index].state.lock);
	if (!peci_sensor_need_update(&priv->temp.core[core_index].state))
		goto skip_update;

	ret = peci_pcs_read(priv->peci_dev, PECI_PCS_MODULE_TEMP, core_index, &pcs);
	if (ret)
		goto err_unlock;

	core_dts_margin = FIELD_GET(PCS_MODULE_TEMP_MASK, pcs);
	if (!dts_valid(core_dts_margin)) {
		ret = -EIO;
		goto err_unlock;
	}

	ret = get_temp_target(priv, tjmax_type, &tjmax);
	if (ret)
		goto err_unlock;

	/* Note that the tjmax should be available before calling it */
	priv->temp.core[core_index].value =
		(s32)tjmax + dts_ten_dot_six_to_millidegree(core_dts_margin);

	peci_sensor_mark_updated(&priv->temp.core[core_index].state);

skip_update:
	*val = priv->temp.core[core_index].value;
err_unlock:
	mutex_unlock(&priv->temp.core[core_index].state.lock);
	return ret;
}

static int cputemp_read_string(struct device *dev, enum hwmon_sensor_types type,
			       u32 attr, int channel, const char **str)
{
	struct peci_cputemp *priv = dev_get_drvdata(dev);

	if (attr != hwmon_temp_label)
		return -EOPNOTSUPP;

	*str = channel < channel_core ?
		cputemp_label[channel] : priv->coretemp_label[channel - channel_core];

	return 0;
}

static int cputemp_read(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long *val)
{
	struct peci_cputemp *priv = dev_get_drvdata(dev);

	switch (attr) {
	case hwmon_temp_input:
		switch (channel) {
		case channel_die:
			return get_die_temp(priv, val);
		case channel_dts:
			return get_dts(priv, val);
		case channel_tcontrol:
			return get_temp_target(priv, tcontrol_type, val);
		case channel_tthrottle:
			return get_temp_target(priv, tthrottle_type, val);
		case channel_tjmax:
			return get_temp_target(priv, tjmax_type, val);
		default:
			return get_core_temp(priv, channel - channel_core, val);
		}
		break;
	case hwmon_temp_max:
		return get_temp_target(priv, tcontrol_type, val);
	case hwmon_temp_crit:
		return get_temp_target(priv, tjmax_type, val);
	case hwmon_temp_crit_hyst:
		return get_temp_target(priv, crit_hyst_type, val);
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static umode_t cputemp_is_visible(const void *data, enum hwmon_sensor_types type,
				  u32 attr, int channel)
{
	const struct peci_cputemp *priv = data;

	if (channel > CPUTEMP_CHANNEL_NUMS)
		return 0;

	if (channel < channel_core)
		return 0444;

	if (test_bit(channel - channel_core, priv->core_mask))
		return 0444;

	return 0;
}

static int init_core_mask(struct peci_cputemp *priv)
{
	struct peci_device *peci_dev = priv->peci_dev;
	struct resolved_cores_reg *reg = priv->gen_info->reg;
	u64 core_mask;
	u32 data;
	int ret;

	/* Get the RESOLVED_CORES register value */
	switch (peci_dev->info.x86_vfm) {
	case INTEL_ICELAKE_X:
	case INTEL_ICELAKE_D:
	case INTEL_SAPPHIRERAPIDS_X:
		ret = peci_ep_pci_local_read(peci_dev, 0, reg->bus, reg->dev,
					     reg->func, reg->offset + 4, &data);
		if (ret)
			return ret;

		core_mask = (u64)data << 32;

		ret = peci_ep_pci_local_read(peci_dev, 0, reg->bus, reg->dev,
					     reg->func, reg->offset, &data);
		if (ret)
			return ret;

		core_mask |= data;

		break;
	default:
		ret = peci_pci_local_read(peci_dev, reg->bus, reg->dev,
					  reg->func, reg->offset, &data);
		if (ret)
			return ret;

		core_mask = data;

		break;
	}

	if (!core_mask)
		return -EIO;

	bitmap_from_u64(priv->core_mask, core_mask);

	return 0;
}

static int create_temp_label(struct peci_cputemp *priv)
{
	unsigned long core_max = find_last_bit(priv->core_mask, CORE_NUMS_MAX);
	int i;

	priv->coretemp_label = devm_kzalloc(priv->dev, (core_max + 1) * sizeof(char *), GFP_KERNEL);
	if (!priv->coretemp_label)
		return -ENOMEM;

	for_each_set_bit(i, priv->core_mask, CORE_NUMS_MAX) {
		priv->coretemp_label[i] = devm_kasprintf(priv->dev, GFP_KERNEL, "Core %d", i);
		if (!priv->coretemp_label[i])
			return -ENOMEM;
	}

	return 0;
}

static void check_resolved_cores(struct peci_cputemp *priv)
{
	/*
	 * Failure to resolve cores is non-critical, we're still able to
	 * provide other sensor data.
	 */

	if (init_core_mask(priv))
		return;

	if (create_temp_label(priv))
		bitmap_zero(priv->core_mask, CORE_NUMS_MAX);
}

static void sensor_init(struct peci_cputemp *priv)
{
	int i;

	mutex_init(&priv->temp.target.state.lock);
	mutex_init(&priv->temp.die.state.lock);
	mutex_init(&priv->temp.dts.state.lock);

	for_each_set_bit(i, priv->core_mask, CORE_NUMS_MAX)
		mutex_init(&priv->temp.core[i].state.lock);
}

static const struct hwmon_ops peci_cputemp_ops = {
	.is_visible = cputemp_is_visible,
	.read_string = cputemp_read_string,
	.read = cputemp_read,
};

static const struct hwmon_channel_info * const peci_cputemp_info[] = {
	HWMON_CHANNEL_INFO(temp,
			   /* Die temperature */
			   HWMON_T_LABEL | HWMON_T_INPUT | HWMON_T_MAX |
			   HWMON_T_CRIT | HWMON_T_CRIT_HYST,
			   /* DTS margin */
			   HWMON_T_LABEL | HWMON_T_INPUT | HWMON_T_MAX |
			   HWMON_T_CRIT | HWMON_T_CRIT_HYST,
			   /* Tcontrol temperature */
			   HWMON_T_LABEL | HWMON_T_INPUT | HWMON_T_CRIT,
			   /* Tthrottle temperature */
			   HWMON_T_LABEL | HWMON_T_INPUT,
			   /* Tjmax temperature */
			   HWMON_T_LABEL | HWMON_T_INPUT,
			   /* Core temperature - for all core channels */
			   [channel_core ... CPUTEMP_CHANNEL_NUMS - 1] =
						HWMON_T_LABEL | HWMON_T_INPUT),
	NULL
};

static const struct hwmon_chip_info peci_cputemp_chip_info = {
	.ops = &peci_cputemp_ops,
	.info = peci_cputemp_info,
};

static int peci_cputemp_probe(struct auxiliary_device *adev,
			      const struct auxiliary_device_id *id)
{
	struct device *dev = &adev->dev;
	struct peci_device *peci_dev = to_peci_device(dev->parent);
	struct peci_cputemp *priv;
	struct device *hwmon_dev;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->name = devm_kasprintf(dev, GFP_KERNEL, "peci_cputemp.cpu%d",
				    peci_dev->info.socket_id);
	if (!priv->name)
		return -ENOMEM;

	priv->dev = dev;
	priv->peci_dev = peci_dev;
	priv->gen_info = (const struct cpu_info *)id->driver_data;

	/*
	 * This is just a sanity check. Since we're using commands that are
	 * guaranteed to be supported on a given platform, we should never see
	 * revision lower than expected.
	 */
	if (peci_dev->info.peci_revision < priv->gen_info->min_peci_revision)
		dev_warn(priv->dev,
			 "Unexpected PECI revision %#x, some features may be unavailable\n",
			 peci_dev->info.peci_revision);

	check_resolved_cores(priv);

	sensor_init(priv);

	hwmon_dev = devm_hwmon_device_register_with_info(priv->dev, priv->name,
							 priv, &peci_cputemp_chip_info, NULL);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

/*
 * RESOLVED_CORES PCI configuration register may have different location on
 * different platforms.
 */
static struct resolved_cores_reg resolved_cores_reg_hsx = {
	.bus = 1,
	.dev = 30,
	.func = 3,
	.offset = 0xb4,
};

static struct resolved_cores_reg resolved_cores_reg_icx = {
	.bus = 14,
	.dev = 30,
	.func = 3,
	.offset = 0xd0,
};

static struct resolved_cores_reg resolved_cores_reg_spr = {
	.bus = 31,
	.dev = 30,
	.func = 6,
	.offset = 0x80,
};

static const struct cpu_info cpu_hsx = {
	.reg		= &resolved_cores_reg_hsx,
	.min_peci_revision = 0x33,
	.thermal_margin_to_millidegree = &dts_eight_dot_eight_to_millidegree,
};

static const struct cpu_info cpu_skx = {
	.reg		= &resolved_cores_reg_hsx,
	.min_peci_revision = 0x33,
	.thermal_margin_to_millidegree = &dts_ten_dot_six_to_millidegree,
};

static const struct cpu_info cpu_icx = {
	.reg		= &resolved_cores_reg_icx,
	.min_peci_revision = 0x40,
	.thermal_margin_to_millidegree = &dts_ten_dot_six_to_millidegree,
};

static const struct cpu_info cpu_spr = {
	.reg		= &resolved_cores_reg_spr,
	.min_peci_revision = 0x40,
	.thermal_margin_to_millidegree = &dts_ten_dot_six_to_millidegree,
};

static const struct auxiliary_device_id peci_cputemp_ids[] = {
	{
		.name = "peci_cpu.cputemp.hsx",
		.driver_data = (kernel_ulong_t)&cpu_hsx,
	},
	{
		.name = "peci_cpu.cputemp.bdx",
		.driver_data = (kernel_ulong_t)&cpu_hsx,
	},
	{
		.name = "peci_cpu.cputemp.bdxd",
		.driver_data = (kernel_ulong_t)&cpu_hsx,
	},
	{
		.name = "peci_cpu.cputemp.skx",
		.driver_data = (kernel_ulong_t)&cpu_skx,
	},
	{
		.name = "peci_cpu.cputemp.icx",
		.driver_data = (kernel_ulong_t)&cpu_icx,
	},
	{
		.name = "peci_cpu.cputemp.icxd",
		.driver_data = (kernel_ulong_t)&cpu_icx,
	},
	{
		.name = "peci_cpu.cputemp.spr",
		.driver_data = (kernel_ulong_t)&cpu_spr,
	},
	{ }
};
MODULE_DEVICE_TABLE(auxiliary, peci_cputemp_ids);

static struct auxiliary_driver peci_cputemp_driver = {
	.probe		= peci_cputemp_probe,
	.id_table	= peci_cputemp_ids,
};

module_auxiliary_driver(peci_cputemp_driver);

MODULE_AUTHOR("Jae Hyun Yoo <jae.hyun.yoo@linux.intel.com>");
MODULE_AUTHOR("Iwona Winiarska <iwona.winiarska@intel.com>");
MODULE_DESCRIPTION("PECI cputemp driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(PECI_CPU);
