// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved

#include <linux/hwmon.h>
#include <linux/bitmap.h>
#include <linux/mlx5/device.h>
#include <linux/mlx5/mlx5_ifc.h>
#include <linux/mlx5/port.h>
#include "mlx5_core.h"
#include "hwmon.h"

#define CHANNELS_TYPE_NUM 2 /* chip channel and temp channel */
#define CHIP_CONFIG_NUM 1

/* module 0 is mapped to sensor_index 64 in MTMP register */
#define to_mtmp_module_sensor_idx(idx) (64 + (idx))

/* All temperatures retrieved in units of 0.125C. hwmon framework expect
 * it in units of millidegrees C. Hence multiply values by 125.
 */
#define mtmp_temp_to_mdeg(temp) ((temp) * 125)

struct temp_channel_desc {
	u32 sensor_index;
	char sensor_name[32];
};

/* chip_channel_config and channel_info arrays must be 0-terminated, hence + 1 */
struct mlx5_hwmon {
	struct mlx5_core_dev *mdev;
	struct device *hwmon_dev;
	struct hwmon_channel_info chip_info;
	u32 chip_channel_config[CHIP_CONFIG_NUM + 1];
	struct hwmon_channel_info temp_info;
	u32 *temp_channel_config;
	const struct hwmon_channel_info *channel_info[CHANNELS_TYPE_NUM + 1];
	struct hwmon_chip_info chip;
	struct temp_channel_desc *temp_channel_desc;
	u32 asic_platform_scount;
	u32 module_scount;
};

static int mlx5_hwmon_query_mtmp(struct mlx5_core_dev *mdev, u32 sensor_index, u32 *mtmp_out)
{
	u32 mtmp_in[MLX5_ST_SZ_DW(mtmp_reg)] = {};

	MLX5_SET(mtmp_reg, mtmp_in, sensor_index, sensor_index);

	return mlx5_core_access_reg(mdev, mtmp_in,  sizeof(mtmp_in),
				    mtmp_out, MLX5_ST_SZ_BYTES(mtmp_reg),
				    MLX5_REG_MTMP, 0, 0);
}

static int mlx5_hwmon_reset_max_temp(struct mlx5_core_dev *mdev, int sensor_index)
{
	u32 mtmp_out[MLX5_ST_SZ_DW(mtmp_reg)] = {};
	u32 mtmp_in[MLX5_ST_SZ_DW(mtmp_reg)] = {};

	MLX5_SET(mtmp_reg, mtmp_in, sensor_index, sensor_index);
	MLX5_SET(mtmp_reg, mtmp_in, mtr, 1);

	return mlx5_core_access_reg(mdev, mtmp_in,  sizeof(mtmp_in),
				    mtmp_out, sizeof(mtmp_out),
				    MLX5_REG_MTMP, 0, 0);
}

static int mlx5_hwmon_enable_max_temp(struct mlx5_core_dev *mdev, int sensor_index)
{
	u32 mtmp_out[MLX5_ST_SZ_DW(mtmp_reg)] = {};
	u32 mtmp_in[MLX5_ST_SZ_DW(mtmp_reg)] = {};
	int err;

	err = mlx5_hwmon_query_mtmp(mdev, sensor_index, mtmp_in);
	if (err)
		return err;

	MLX5_SET(mtmp_reg, mtmp_in, mte, 1);
	return mlx5_core_access_reg(mdev, mtmp_in,  sizeof(mtmp_in),
				    mtmp_out, sizeof(mtmp_out),
				    MLX5_REG_MTMP, 0, 1);
}

static int mlx5_hwmon_read(struct device *dev, enum hwmon_sensor_types type, u32 attr,
			   int channel, long *val)
{
	struct mlx5_hwmon *hwmon = dev_get_drvdata(dev);
	u32 mtmp_out[MLX5_ST_SZ_DW(mtmp_reg)] = {};
	int err;

	if (type != hwmon_temp)
		return -EOPNOTSUPP;

	err = mlx5_hwmon_query_mtmp(hwmon->mdev, hwmon->temp_channel_desc[channel].sensor_index,
				    mtmp_out);
	if (err)
		return err;

	switch (attr) {
	case hwmon_temp_input:
		*val = mtmp_temp_to_mdeg(MLX5_GET(mtmp_reg, mtmp_out, temperature));
		return 0;
	case hwmon_temp_highest:
		*val = mtmp_temp_to_mdeg(MLX5_GET(mtmp_reg, mtmp_out, max_temperature));
		return 0;
	case hwmon_temp_crit:
		*val = mtmp_temp_to_mdeg(MLX5_GET(mtmp_reg, mtmp_out, temp_threshold_hi));
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int mlx5_hwmon_write(struct device *dev, enum hwmon_sensor_types type, u32 attr,
			    int channel, long val)
{
	struct mlx5_hwmon *hwmon = dev_get_drvdata(dev);

	if (type != hwmon_temp || attr != hwmon_temp_reset_history)
		return -EOPNOTSUPP;

	return mlx5_hwmon_reset_max_temp(hwmon->mdev,
				hwmon->temp_channel_desc[channel].sensor_index);
}

static umode_t mlx5_hwmon_is_visible(const void *data, enum hwmon_sensor_types type, u32 attr,
				     int channel)
{
	if (type != hwmon_temp)
		return 0;

	switch (attr) {
	case hwmon_temp_input:
	case hwmon_temp_highest:
	case hwmon_temp_crit:
	case hwmon_temp_label:
		return 0444;
	case hwmon_temp_reset_history:
		return 0200;
	default:
		return 0;
	}
}

static int mlx5_hwmon_read_string(struct device *dev, enum hwmon_sensor_types type, u32 attr,
				  int channel, const char **str)
{
	struct mlx5_hwmon *hwmon = dev_get_drvdata(dev);

	if (type != hwmon_temp || attr != hwmon_temp_label)
		return -EOPNOTSUPP;

	*str = (const char *)hwmon->temp_channel_desc[channel].sensor_name;
	return 0;
}

static const struct hwmon_ops mlx5_hwmon_ops = {
	.read = mlx5_hwmon_read,
	.read_string = mlx5_hwmon_read_string,
	.is_visible = mlx5_hwmon_is_visible,
	.write = mlx5_hwmon_write,
};

static int mlx5_hwmon_init_channels_names(struct mlx5_hwmon *hwmon)
{
	u32 i;

	for (i = 0; i < hwmon->asic_platform_scount + hwmon->module_scount; i++) {
		u32 mtmp_out[MLX5_ST_SZ_DW(mtmp_reg)] = {};
		char *sensor_name;
		int err;

		err = mlx5_hwmon_query_mtmp(hwmon->mdev, hwmon->temp_channel_desc[i].sensor_index,
					    mtmp_out);
		if (err)
			return err;

		sensor_name = MLX5_ADDR_OF(mtmp_reg, mtmp_out, sensor_name_hi);
		if (!*sensor_name) {
			snprintf(hwmon->temp_channel_desc[i].sensor_name,
				 sizeof(hwmon->temp_channel_desc[i].sensor_name), "sensor%u",
				 hwmon->temp_channel_desc[i].sensor_index);
			continue;
		}

		memcpy(&hwmon->temp_channel_desc[i].sensor_name, sensor_name,
		       MLX5_FLD_SZ_BYTES(mtmp_reg, sensor_name_hi) +
		       MLX5_FLD_SZ_BYTES(mtmp_reg, sensor_name_lo));
	}

	return 0;
}

static int mlx5_hwmon_get_module_sensor_index(struct mlx5_core_dev *mdev, u32 *module_index)
{
	int module_num;
	int err;

	err = mlx5_query_module_num(mdev, &module_num);
	if (err)
		return err;

	*module_index = to_mtmp_module_sensor_idx(module_num);

	return 0;
}

static int mlx5_hwmon_init_sensors_indexes(struct mlx5_hwmon *hwmon, u64 sensor_map)
{
	DECLARE_BITMAP(smap, BITS_PER_TYPE(sensor_map));
	unsigned long bit_pos;
	int err = 0;
	int i = 0;

	bitmap_from_u64(smap, sensor_map);

	for_each_set_bit(bit_pos, smap, BITS_PER_TYPE(sensor_map)) {
		hwmon->temp_channel_desc[i].sensor_index = bit_pos;
		i++;
	}

	if (hwmon->module_scount)
		err = mlx5_hwmon_get_module_sensor_index(hwmon->mdev,
							 &hwmon->temp_channel_desc[i].sensor_index);

	return err;
}

static void mlx5_hwmon_channel_info_init(struct mlx5_hwmon *hwmon)
{
	int i;

	hwmon->channel_info[0] = &hwmon->chip_info;
	hwmon->channel_info[1] = &hwmon->temp_info;

	hwmon->chip_channel_config[0] = HWMON_C_REGISTER_TZ;
	hwmon->chip_info.config = (const u32 *)hwmon->chip_channel_config;
	hwmon->chip_info.type = hwmon_chip;

	for (i = 0; i < hwmon->asic_platform_scount + hwmon->module_scount; i++)
		hwmon->temp_channel_config[i] = HWMON_T_INPUT | HWMON_T_HIGHEST | HWMON_T_CRIT |
					     HWMON_T_RESET_HISTORY | HWMON_T_LABEL;

	hwmon->temp_info.config = (const u32 *)hwmon->temp_channel_config;
	hwmon->temp_info.type = hwmon_temp;
}

static int mlx5_hwmon_is_module_mon_cap(struct mlx5_core_dev *mdev, bool *mon_cap)
{
	u32 mtmp_out[MLX5_ST_SZ_DW(mtmp_reg)];
	u32 module_index;
	int err;

	err = mlx5_hwmon_get_module_sensor_index(mdev, &module_index);
	if (err)
		return err;

	err = mlx5_hwmon_query_mtmp(mdev, module_index, mtmp_out);
	if (err)
		return err;

	if (MLX5_GET(mtmp_reg, mtmp_out, temperature))
		*mon_cap = true;

	return 0;
}

static int mlx5_hwmon_get_sensors_count(struct mlx5_core_dev *mdev, u32 *asic_platform_scount)
{
	u32 mtcap_out[MLX5_ST_SZ_DW(mtcap_reg)] = {};
	u32 mtcap_in[MLX5_ST_SZ_DW(mtcap_reg)] = {};
	int err;

	err = mlx5_core_access_reg(mdev, mtcap_in,  sizeof(mtcap_in),
				   mtcap_out, sizeof(mtcap_out),
				   MLX5_REG_MTCAP, 0, 0);
	if (err)
		return err;

	*asic_platform_scount = MLX5_GET(mtcap_reg, mtcap_out, sensor_count);

	return 0;
}

static void mlx5_hwmon_free(struct mlx5_hwmon *hwmon)
{
	if (!hwmon)
		return;

	kfree(hwmon->temp_channel_config);
	kfree(hwmon->temp_channel_desc);
	kfree(hwmon);
}

static struct mlx5_hwmon *mlx5_hwmon_alloc(struct mlx5_core_dev *mdev)
{
	struct mlx5_hwmon *hwmon;
	bool mon_cap = false;
	u32 sensors_count;
	int err;

	hwmon = kzalloc(sizeof(*mdev->hwmon), GFP_KERNEL);
	if (!hwmon)
		return ERR_PTR(-ENOMEM);

	err = mlx5_hwmon_get_sensors_count(mdev, &hwmon->asic_platform_scount);
	if (err)
		goto err_free_hwmon;

	/* check if module sensor has thermal mon cap. if yes, allocate channel desc for it */
	err = mlx5_hwmon_is_module_mon_cap(mdev, &mon_cap);
	if (err)
		goto err_free_hwmon;

	hwmon->module_scount = mon_cap ? 1 : 0;
	sensors_count = hwmon->asic_platform_scount + hwmon->module_scount;
	hwmon->temp_channel_desc = kcalloc(sensors_count, sizeof(*hwmon->temp_channel_desc),
					   GFP_KERNEL);
	if (!hwmon->temp_channel_desc) {
		err = -ENOMEM;
		goto err_free_hwmon;
	}

	/* sensors configuration values array, must be 0-terminated hence, + 1 */
	hwmon->temp_channel_config = kcalloc(sensors_count + 1, sizeof(*hwmon->temp_channel_config),
					     GFP_KERNEL);
	if (!hwmon->temp_channel_config) {
		err = -ENOMEM;
		goto err_free_temp_channel_desc;
	}

	hwmon->mdev = mdev;

	return hwmon;

err_free_temp_channel_desc:
	kfree(hwmon->temp_channel_desc);
err_free_hwmon:
	kfree(hwmon);
	return ERR_PTR(err);
}

static int mlx5_hwmon_dev_init(struct mlx5_hwmon *hwmon)
{
	u32 mtcap_out[MLX5_ST_SZ_DW(mtcap_reg)] = {};
	u32 mtcap_in[MLX5_ST_SZ_DW(mtcap_reg)] = {};
	int err;
	int i;

	err =  mlx5_core_access_reg(hwmon->mdev, mtcap_in,  sizeof(mtcap_in),
				    mtcap_out, sizeof(mtcap_out),
				    MLX5_REG_MTCAP, 0, 0);
	if (err)
		return err;

	mlx5_hwmon_channel_info_init(hwmon);
	mlx5_hwmon_init_sensors_indexes(hwmon, MLX5_GET64(mtcap_reg, mtcap_out, sensor_map));
	err = mlx5_hwmon_init_channels_names(hwmon);
	if (err)
		return err;

	for (i = 0; i < hwmon->asic_platform_scount + hwmon->module_scount; i++) {
		err = mlx5_hwmon_enable_max_temp(hwmon->mdev,
						 hwmon->temp_channel_desc[i].sensor_index);
		if (err)
			return err;
	}

	hwmon->chip.ops = &mlx5_hwmon_ops;
	hwmon->chip.info = (const struct hwmon_channel_info **)hwmon->channel_info;

	return 0;
}

int mlx5_hwmon_dev_register(struct mlx5_core_dev *mdev)
{
	struct device *dev = mdev->device;
	struct mlx5_hwmon *hwmon;
	int err;

	if (!MLX5_CAP_MCAM_REG(mdev, mtmp))
		return 0;

	hwmon = mlx5_hwmon_alloc(mdev);
	if (IS_ERR(hwmon))
		return PTR_ERR(hwmon);

	err = mlx5_hwmon_dev_init(hwmon);
	if (err)
		goto err_free_hwmon;

	hwmon->hwmon_dev = hwmon_device_register_with_info(dev, "mlx5",
							   hwmon,
							   &hwmon->chip,
							   NULL);
	if (IS_ERR(hwmon->hwmon_dev)) {
		err = PTR_ERR(hwmon->hwmon_dev);
		goto err_free_hwmon;
	}

	mdev->hwmon = hwmon;
	return 0;

err_free_hwmon:
	mlx5_hwmon_free(hwmon);
	return err;
}

void mlx5_hwmon_dev_unregister(struct mlx5_core_dev *mdev)
{
	struct mlx5_hwmon *hwmon = mdev->hwmon;

	if (!hwmon)
		return;

	hwmon_device_unregister(hwmon->hwmon_dev);
	mlx5_hwmon_free(hwmon);
	mdev->hwmon = NULL;
}

const char *hwmon_get_sensor_name(struct mlx5_hwmon *hwmon, int channel)
{
	return hwmon->temp_channel_desc[channel].sensor_name;
}
