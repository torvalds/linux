// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_lis2duxs12 machine learning core driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2022 STMicroelectronics Inc.
 */

#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/iio/buffer.h>
#include <linux/iio/events.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/version.h>

#include <linux/platform_data/st_sensors_pdata.h>

#include "st_lis2duxs12.h"

#define ST_LIS2DUXS12_MLC_LOADER_VERSION	"0.4"

/* number of machine learning core available on device hardware */
#define ST_LIS2DUXS12_MLC_MAX_NUMBER		4
#define ST_LIS2DUXS12_FSM_MAX_NUMBER		8

#define ST_LIS2DUXS12_LOADER_CMD_WAIT		0xff

#ifdef CONFIG_IIO_LIS2DUXS12_MLC_BUILTIN_FIRMWARE
static const u8 st_lis2duxs12_mlc_fw[] = {
	#include "st_lis2duxs12_mlc.fw"
};
DECLARE_BUILTIN_FIRMWARE(LIS2DUXS12_MLC_FIRMWARE_NAME,
			 st_lis2duxs12_mlc_fw);
#else /* CONFIG_IIO_LIS2DUXS12_MLC_BUILTIN_FIRMWARE */
#define LIS2DUXS12_MLC_FIRMWARE_NAME		"st_lis2duxs12_mlc.bin"
#endif /* CONFIG_IIO_LIS2DUXS12_MLC_BUILTIN_FIRMWARE */

static const u8 mlcdata[] = {
	/* lis2duxs12_mlc_fsm_hpd.ucf */
	0x14, 0x00, 0x13, 0x10, 0xff, 0x05, 0x3f, 0x80, 0x04, 0x00,
	0x05, 0x00, 0x39, 0x4b, 0x1a, 0x01, 0x0a, 0x00, 0x0b, 0x01,
	0x0e, 0x00, 0x0f, 0x00, 0x17, 0x40, 0x02, 0x01, 0x08, 0x54,
	0x09, 0x00, 0x09, 0x00, 0x09, 0x01, 0x09, 0x01, 0x09, 0x00,
	0x09, 0x02, 0x02, 0x21, 0x08, 0x00, 0x09, 0x24, 0x09, 0x08,
	0x09, 0x18, 0x09, 0x00, 0x09, 0x0f, 0x09, 0x00, 0x09, 0x01,
	0x09, 0x00, 0x09, 0x00, 0x09, 0x00, 0x09, 0x00, 0x09, 0x00,
	0x09, 0xee, 0x09, 0x02, 0x09, 0x04, 0x09, 0x0f, 0x09, 0x66,
	0x09, 0x99, 0x09, 0x33, 0x09, 0xf1, 0x09, 0x44, 0x09, 0x77,
	0x09, 0x22, 0x09, 0x00, 0x04, 0x00, 0x05, 0x11, 0x17, 0x80,
	0x3f, 0x00, 0x1f, 0x01, 0x14, 0x92, 0x14, 0x00, 0x13, 0x10,
	0xff, 0x05, 0x3f, 0x80, 0x04, 0x00, 0x05, 0x00, 0x39, 0x4b,
	0x1a, 0x01, 0x0a, 0x00, 0x0b, 0x01, 0x0e, 0x00, 0x0f, 0x00,
	0x17, 0x40, 0x02, 0x01, 0x08, 0x54, 0x09, 0x00, 0x09, 0x00,
	0x09, 0x01, 0x09, 0x01, 0x09, 0x00, 0x09, 0x02, 0x02, 0x21,
	0x08, 0x00, 0x09, 0x24, 0x09, 0x08, 0x09, 0x18, 0x09, 0x00,
	0x09, 0x0f, 0x09, 0x00, 0x09, 0x01, 0x09, 0x00, 0x09, 0x00,
	0x09, 0x00, 0x09, 0x00, 0x09, 0x00, 0x09, 0xee, 0x09, 0x02,
	0x09, 0x04, 0x09, 0x0f, 0x09, 0x66, 0x09, 0x99, 0x09, 0x33,
	0x09, 0xf1, 0x09, 0x44, 0x09, 0x77, 0x09, 0x22, 0x09, 0x00,
	0x04, 0x00, 0x05, 0x11, 0x17, 0x80, 0x3f, 0x00, 0x1f, 0x01,
	0x14, 0x92, 0x13, 0x10, 0xff, 0x05, 0x14, 0x00, 0x3f, 0x80,
	0x04, 0x00, 0x05, 0x00, 0x17, 0x40, 0x02, 0x01, 0x08, 0xb6,
	0x09, 0x00, 0x09, 0x3c, 0x09, 0xfe, 0x09, 0x00, 0x09, 0x18,
	0x09, 0x01, 0x09, 0x01, 0x09, 0x00, 0x09, 0x14, 0x09, 0x01,
	0x09, 0x0a, 0x02, 0x01, 0x08, 0xc8, 0x09, 0xdc, 0x09, 0x00,
	0x09, 0x1a, 0x09, 0x01, 0x09, 0x26, 0x09, 0x01, 0x02, 0x01,
	0x08, 0xdc, 0x09, 0x1c, 0x09, 0x00, 0x09, 0x00, 0x09, 0x00,
	0x09, 0x00, 0x09, 0x00, 0x09, 0x00, 0x09, 0x00, 0x09, 0x00,
	0x09, 0x00, 0x09, 0x00, 0x09, 0x00, 0x09, 0x00, 0x09, 0x00,
	0x09, 0xa2, 0x09, 0x1d, 0x09, 0xaf, 0x09, 0x21, 0x09, 0xa2,
	0x09, 0x1d, 0x09, 0x1d, 0x09, 0xbf, 0x09, 0x68, 0x09, 0x3a,
	0x09, 0x3f, 0x09, 0x00, 0x09, 0x03, 0x09, 0x2c, 0x09, 0x00,
	0x09, 0xfc, 0x09, 0x00, 0x09, 0x7c, 0x09, 0x1f, 0x09, 0x00,
	0x02, 0x11, 0x08, 0x1a, 0x09, 0x00, 0x09, 0x00, 0x09, 0x00,
	0x09, 0x00, 0x09, 0x00, 0x09, 0x00, 0x09, 0x00, 0x09, 0x00,
	0x09, 0x00, 0x09, 0x00, 0x09, 0x00, 0x3f, 0x00, 0x3f, 0x80,
	0x17, 0x40, 0x02, 0x11, 0x08, 0x26, 0x09, 0x00, 0x09, 0x58,
	0x09, 0x40, 0x09, 0xe0, 0x3f, 0x80, 0x17, 0x00, 0x04, 0x00,
	0x05, 0x10, 0x02, 0x01, 0x3f, 0x00, 0x1f, 0x01, 0x3f, 0x80,
	0x3a, 0x41, 0x17, 0x80, 0x04, 0x00, 0x05, 0x11, 0x02, 0x01,
	0x3f, 0x00, 0x0c, 0x00, 0x0e, 0x00, 0x10, 0x10, 0x11, 0x00,
	0x12, 0x00, 0x13, 0x10, 0x14, 0x92, 0x15, 0x00, 0x16, 0x00,
	0x17, 0x00, 0x18, 0x00, 0x1c, 0x00, 0x1d, 0x00, 0x1e, 0x00,
	0x1f, 0x01, 0x20, 0x00, 0x31, 0x9a, 0x32, 0x00, 0x33, 0x00,
	0x3d, 0x00, 0x3f, 0x00, 0x47, 0x00, 0x6f, 0x00, 0x70, 0x00,
	0x71, 0x00, 0x72, 0x00, 0x73, 0x00, 0x74, 0x00, 0x75, 0x00,
	0x3f, 0x80, 0x17, 0x80, 0x04, 0x00, 0x05, 0x11, 0x02, 0x01,
	0x3f, 0x00,
};

static const struct firmware st_lis2duxs12_mlc_preload = {
		.size = sizeof(mlcdata),
		.data = mlcdata
};

static struct
iio_dev *st_lis2duxs12_mlc_alloc_iio_dev(struct st_lis2duxs12_hw *hw,
					 enum st_lis2duxs12_sensor_id id);

static const unsigned long st_lis2duxs12_mlc_available_scan_masks[] = {
	BIT(1), 0x0
};

static int
st_lis2duxs12_mlc_enable_sensor(struct st_lis2duxs12_sensor *sensor,
				bool enable)
{
	struct st_lis2duxs12_hw *hw = sensor->hw;
	int i, id, err = 0;

	/* enable acc sensor as trigger */
	err = st_lis2duxs12_sensor_set_enable(sensor, enable);
	if (err < 0)
		return err;

	if (sensor->status == ST_LIS2DUXS12_MLC_ENABLED) {
		int value;

		value = enable ? hw->mlc_config->mlc_int_mask : 0;
		err = st_lis2duxs12_write_page_locked(hw,
						   hw->mlc_config->mlc_int_addr,
						   &value, 1);
		if (err < 0)
			return err;

		/*
		 * enable mlc core
		 * only one mlc enable bit so not need to check if
		 * other running
		 */
		err = st_lis2duxs12_update_page_bits_locked(hw,
				ST_LIS2DUXS12_EMB_FUNC_EN_B_ADDR,
				ST_LIS2DUXS12_MLC_EN_MASK,
				ST_LIS2DUXS12_SHIFT_VAL(enable,
						    ST_LIS2DUXS12_MLC_EN_MASK));
		if (err < 0)
			return err;

		dev_info(sensor->hw->dev,
			 "Enabling MLC sensor %d to %d (INT %x)\n",
			 sensor->id, enable, value);
	} else if (sensor->status == ST_LIS2DUXS12_FSM_ENABLED) {
		int value;

		value = enable ? hw->mlc_config->fsm_int_mask : 0;
		err = st_lis2duxs12_write_page_locked(hw,
						   hw->mlc_config->fsm_int_addr,
						   &value, 1);
		if (err < 0)
			return err;

		/* enable fsm core */
		for (i = 0; i < ST_LIS2DUXS12_FSM_MAX_NUMBER; i++) {
			id = st_lis2duxs12_fsm_sensor_list[i];
			if (hw->enable_mask & BIT(id))
				break;
		}

		/* check for any other fsm already enabled */
		if (enable || i == ST_LIS2DUXS12_FSM_MAX_NUMBER) {
			err = st_lis2duxs12_update_page_bits_locked(hw,
					       ST_LIS2DUXS12_EMB_FUNC_EN_B_ADDR,
					       ST_LIS2DUXS12_FSM_EN_MASK,
					       ST_LIS2DUXS12_SHIFT_VAL(enable,
						    ST_LIS2DUXS12_FSM_EN_MASK));
			if (err < 0)
				return err;

			/* force mlc enable */
			err = st_lis2duxs12_update_page_bits_locked(hw,
					       ST_LIS2DUXS12_EMB_FUNC_EN_B_ADDR,
					       ST_LIS2DUXS12_MLC_EN_MASK,
					       ST_LIS2DUXS12_SHIFT_VAL(enable,
						    ST_LIS2DUXS12_MLC_EN_MASK));
			if (err < 0)
				return err;
		}

		dev_info(sensor->hw->dev,
			 "Enabling FSM sensor %d to %d (INT %x)\n",
			 sensor->id, enable, value);
	} else {
		dev_err(hw->dev, "invalid sensor configuration\n");
		err = -ENODEV;

		return err;
	}

	if (enable)
		hw->enable_mask |= BIT(sensor->id);
	else
		hw->enable_mask &= ~BIT(sensor->id);

	return err < 0 ? err : 0;
}

static int
st_lis2duxs12_mlc_write_event_config(struct iio_dev *iio_dev,
				     const struct iio_chan_spec *chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir,
				     int state)
{
	struct st_lis2duxs12_sensor *sensor = iio_priv(iio_dev);

	return st_lis2duxs12_mlc_enable_sensor(sensor, state);
}

static int
st_lis2duxs12_mlc_read_event_config(struct iio_dev *iio_dev,
				    const struct iio_chan_spec *chan,
				    enum iio_event_type type,
				    enum iio_event_direction dir)
{
	struct st_lis2duxs12_sensor *sensor = iio_priv(iio_dev);
	struct st_lis2duxs12_hw *hw = sensor->hw;

	return !!(hw->enable_mask & BIT(sensor->id));
}

static int
st_lis2duxs12_get_mlc_odr_val(struct st_lis2duxs12_hw *hw, u8 val_odr)
{
	int i;

	for (i = 0; i < hw->odr_table_entry[ST_LIS2DUXS12_ID_ACC].size; i++) {
		if (hw->odr_table_entry[ST_LIS2DUXS12_ID_ACC].odr_avl[i].val == val_odr)
			return hw->odr_table_entry[ST_LIS2DUXS12_ID_ACC].odr_avl[i].hz;
	}

	return -EINVAL;
}

/* checks mlc/fsm program consistence with hw device id */
static int st_lis2duxs12_check_valid_mlc(const struct firmware *fw,
					 struct st_lis2duxs12_hw *hw)
{
	bool stmc_page = false;
	u8 reg, val;
	int i = 0;

	while (i < fw->size) {
		reg = fw->data[i++];
		val = fw->data[i++];
		if ((reg == ST_LIS2DUXS12_FUNC_CFG_ACCESS_ADDR) &&
		    (val & ST_LIS2DUXS12_EMB_FUNC_REG_ACCESS_MASK)) {
			stmc_page = true;
		} else if ((reg == ST_LIS2DUXS12_FUNC_CFG_ACCESS_ADDR) &&
			   (val & ST_LIS2DUXS12_EMB_FUNC_REG_ACCESS_MASK) == 0) {
			stmc_page = false;
		} else if (stmc_page) {
			continue;
		} else if (reg == ST_LIS2DUXS12_AH_QVAR_CFG_ADDR) {
			if (!hw->settings->st_qvar_support)
				return -EINVAL;
			break;
		}
	}

	return 0;
}

/* parse and program mlc fragments */
static int st_lis2duxs12_program_mlc(const struct firmware *fw,
				     struct st_lis2duxs12_hw *hw)
{
	u8 mlc_int = 0, mlc_num = 0, fsm_num = 0, skip = 0;
	u8 fsm_int = 0, reg, val, req_odr = 0;
	bool stmc_page = false;
	int ret, i = 0;

	mutex_lock(&hw->page_lock);
	while (i < fw->size) {
		reg = fw->data[i++];
		val = fw->data[i++];
		if ((reg == ST_LIS2DUXS12_FUNC_CFG_ACCESS_ADDR) &&
		    (val & ST_LIS2DUXS12_EMB_FUNC_REG_ACCESS_MASK)) {
			stmc_page = true;
		} else if ((reg == ST_LIS2DUXS12_FUNC_CFG_ACCESS_ADDR) &&
			   (val & ST_LIS2DUXS12_EMB_FUNC_REG_ACCESS_MASK) == 0) {
			stmc_page = false;
		} else if (reg == ST_LIS2DUXS12_LOADER_CMD_WAIT) {
			msleep(val);
		} else if (stmc_page) {
			switch (reg) {
			case ST_LIS2DUXS12_MLC_INT1_ADDR:
			case ST_LIS2DUXS12_MLC_INT2_ADDR:
				mlc_int |= val;
				mlc_num++;
				skip = 1;
				break;
			case ST_LIS2DUXS12_FSM_INT1_ADDR:
			case ST_LIS2DUXS12_FSM_INT2_ADDR:
				fsm_int |= val;
				fsm_num++;
				skip = 1;
				break;
			case ST_LIS2DUXS12_EMB_FUNC_EN_B_ADDR:
				skip = 1;
				break;
			default:
				break;
			}
		} else {
			switch (reg) {
			case ST_LIS2DUXS12_CTRL5_ADDR:
				/* save requested odr and skip write to reg */
				req_odr = max_t(u8, req_odr,
						(val >> __ffs(ST_LIS2DUXS12_ODR_MASK)) & 0x0f);
				skip = 1;
				break;
			case ST_LIS2DUXS12_AH_QVAR_CFG_ADDR:
				/* check qvar requirement */
				if (val & ST_LIS2DUXS12_AH_QVAR_EN_MASK)
					hw->mlc_config->requested_device |=
						     BIT(ST_LIS2DUXS12_ID_QVAR);

				/* remove qvar enable flag */
				val &= ~ST_LIS2DUXS12_AH_QVAR_EN_MASK;
				skip = 1;
				break;
			case ST_LIS2DUXS12_MD1_CFG_ADDR:
			case ST_LIS2DUXS12_MD2_CFG_ADDR:
				/* just write int on emb functions */
				val &= ST_LIS2DUXS12_INT_EMB_FUNC_MASK;
				break;
			default:
				skip = 1;
				break;
			}
		}

		if (!skip) {
			ret = regmap_write(hw->regmap, reg, val);
			if (ret) {
				dev_err(hw->dev, "regmap_write fails\n");

				mutex_unlock(&hw->page_lock);

				return ret;
			}
		}

		skip = 0;

		if (mlc_num >= ST_LIS2DUXS12_MLC_MAX_NUMBER ||
		    fsm_num >= ST_LIS2DUXS12_FSM_MAX_NUMBER)
			break;
	}

	hw->mlc_config->bin_len = fw->size;

	if (mlc_num) {
		hw->mlc_config->mlc_int_mask = mlc_int;
		hw->mlc_config->mlc_int_addr = (hw->int_pin == 1 ?
					    ST_LIS2DUXS12_MLC_INT1_ADDR :
					    ST_LIS2DUXS12_MLC_INT2_ADDR);

		hw->mlc_config->status |= ST_LIS2DUXS12_MLC_ENABLED;
		hw->mlc_config->mlc_configured += mlc_num;
		hw->mlc_config->requested_odr = st_lis2duxs12_get_mlc_odr_val(hw, req_odr);
	}

	if (fsm_num) {
		hw->mlc_config->fsm_int_mask = fsm_int;
		hw->mlc_config->fsm_int_addr = (hw->int_pin == 1 ?
					    ST_LIS2DUXS12_FSM_INT1_ADDR :
					    ST_LIS2DUXS12_FSM_INT2_ADDR);

		hw->mlc_config->status |= ST_LIS2DUXS12_FSM_ENABLED;
		hw->mlc_config->fsm_configured += fsm_num;
		hw->mlc_config->requested_odr = st_lis2duxs12_get_mlc_odr_val(hw, req_odr);
	}

	mutex_unlock(&hw->page_lock);

	return fsm_num + mlc_num;
}

static void st_lis2duxs12_mlc_update(const struct firmware *fw, void *context)
{
	struct st_lis2duxs12_hw *hw = context;
	enum st_lis2duxs12_sensor_id id;
	int ret, i;

	if (!fw) {
		dev_err(hw->dev, "could not get binary firmware\n");

		return;
	}

	ret = st_lis2duxs12_check_valid_mlc(fw, hw);
	if (ret < 0) {
		dev_err(hw->dev, "unsupported mlc version for hw_id %d\n",
			hw->settings->id.hw_id);

		return;
	}

	ret = st_lis2duxs12_program_mlc(fw, hw);
	if (ret > 0) {
		u8 fsm_mask = hw->mlc_config->fsm_int_mask;
		u8 mlc_mask = hw->mlc_config->mlc_int_mask;

		dev_info(hw->dev, "MLC loaded (%d) MLC %01x FSM %02x\n",
			 ret, mlc_mask, fsm_mask);

		for (i = 0; i < ST_LIS2DUXS12_MLC_MAX_NUMBER; i++) {
			if (mlc_mask & BIT(i)) {
				id = st_lis2duxs12_mlc_sensor_list[i];
				hw->iio_devs[id] =
					st_lis2duxs12_mlc_alloc_iio_dev(hw, id);
				if (!hw->iio_devs[id])
					goto release;

				ret = iio_device_register(hw->iio_devs[id]);
				if (ret)
					goto release;
			}
		}

		for (i = 0; i < ST_LIS2DUXS12_FSM_MAX_NUMBER; i++) {
			if (fsm_mask & BIT(i)) {
				id = st_lis2duxs12_fsm_sensor_list[i];
				hw->iio_devs[id] =
					st_lis2duxs12_mlc_alloc_iio_dev(hw, id);
				if (!hw->iio_devs[id])
					goto release;

				ret = iio_device_register(hw->iio_devs[id]);
				if (ret)
					goto release;
			}
		}
	}

release:
	if (hw->preload_mlc) {
		hw->preload_mlc = 0;

		return;
	}

	release_firmware(fw);
}

static int st_lis2duxs12_mlc_flush_all(struct st_lis2duxs12_hw *hw)
{
	struct st_lis2duxs12_sensor *sensor_mlc;
	struct iio_dev *iio_dev;
	int ret = 0, id;

	for (id = ST_LIS2DUXS12_ID_MLC_0; id < ST_LIS2DUXS12_ID_MAX; id++) {
		iio_dev = hw->iio_devs[id];
		if (!iio_dev)
			continue;

		sensor_mlc = iio_priv(iio_dev);
		ret = st_lis2duxs12_mlc_enable_sensor(sensor_mlc, false);
		if (ret < 0)
			break;

		iio_device_unregister(iio_dev);
		kfree(iio_dev->channels);
		iio_device_free(iio_dev);
		hw->iio_devs[id] = NULL;
	}

	return ret;
}

static ssize_t st_lis2duxs12_mlc_info(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct st_lis2duxs12_sensor *sensor = iio_priv(dev_to_iio_dev(dev));
	struct st_lis2duxs12_hw *hw = sensor->hw;

	return scnprintf(buf, PAGE_SIZE, "mlc %02x fsm %02x\n",
			 hw->mlc_config->mlc_configured,
			 hw->mlc_config->fsm_configured);
}

static ssize_t st_lis2duxs12_mlc_get_version(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "mlc loader Version %s\n",
			 ST_LIS2DUXS12_MLC_LOADER_VERSION);
}

static ssize_t st_lis2duxs12_mlc_flush(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t size)
{
	struct st_lis2duxs12_sensor *sensor = iio_priv(dev_to_iio_dev(dev));
	struct st_lis2duxs12_hw *hw = sensor->hw;
	int ret;

	ret = st_lis2duxs12_mlc_flush_all(hw);
	memset(hw->mlc_config, 0, sizeof(*hw->mlc_config));

	return ret < 0 ? ret : size;
}

static ssize_t st_lis2duxs12_mlc_upload(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
	struct st_lis2duxs12_sensor *sensor = iio_priv(dev_to_iio_dev(dev));
	int err;

	err = request_firmware_nowait(THIS_MODULE, true,
				      LIS2DUXS12_MLC_FIRMWARE_NAME,
				      dev, GFP_KERNEL,
				      sensor->hw,
				      st_lis2duxs12_mlc_update);

	return err < 0 ? err : size;
}

static ssize_t st_lis2duxs12_mlc_odr(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct st_lis2duxs12_sensor *sensor = iio_priv(dev_to_iio_dev(dev));
	struct st_lis2duxs12_hw *hw = sensor->hw;

	return scnprintf(buf, PAGE_SIZE, "%d\n", hw->mlc_config->requested_odr);
}

static IIO_DEVICE_ATTR(mlc_info, 0444, st_lis2duxs12_mlc_info, NULL, 0);
static IIO_DEVICE_ATTR(mlc_flush, 0200, NULL, st_lis2duxs12_mlc_flush, 0);
static IIO_DEVICE_ATTR(mlc_version, 0444, st_lis2duxs12_mlc_get_version,
		       NULL, 0);
static IIO_DEVICE_ATTR(load_mlc, 0200, NULL, st_lis2duxs12_mlc_upload, 0);
static IIO_DEVICE_ATTR(mlc_odr, 0444, st_lis2duxs12_mlc_odr, NULL, 0);

static struct attribute *st_lis2duxs12_mlc_event_attributes[] = {
	&iio_dev_attr_mlc_info.dev_attr.attr,
	&iio_dev_attr_mlc_version.dev_attr.attr,
	&iio_dev_attr_load_mlc.dev_attr.attr,
	&iio_dev_attr_mlc_flush.dev_attr.attr,
	&iio_dev_attr_mlc_odr.dev_attr.attr,
	NULL,
};

static const struct
attribute_group st_lis2duxs12_mlc_event_attribute_group = {
	.attrs = st_lis2duxs12_mlc_event_attributes,
};

static const struct iio_info st_lis2duxs12_mlc_event_info = {
	.attrs = &st_lis2duxs12_mlc_event_attribute_group,
	.read_event_config = st_lis2duxs12_mlc_read_event_config,
	.write_event_config = st_lis2duxs12_mlc_write_event_config,
};

static ssize_t st_lis2duxs12_mlc_x_odr(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct st_lis2duxs12_sensor *sensor = iio_priv(dev_to_iio_dev(dev));

	return scnprintf(buf, PAGE_SIZE, "%d.%02d\n",
			 sensor->odr, sensor->uodr);
}

static IIO_DEVICE_ATTR(mlc_x_odr, 0444, st_lis2duxs12_mlc_x_odr, NULL, 0);

static struct attribute *st_lis2duxs12_mlc_x_event_attributes[] = {
	&iio_dev_attr_mlc_x_odr.dev_attr.attr,
	NULL,
};

static const struct
attribute_group st_lis2duxs12_mlc_x_event_attribute_group = {
	.attrs = st_lis2duxs12_mlc_x_event_attributes,
};

static const struct iio_info st_lis2duxs12_mlc_x_event_info = {
	.attrs = &st_lis2duxs12_mlc_x_event_attribute_group,
	.read_event_config = st_lis2duxs12_mlc_read_event_config,
	.write_event_config = st_lis2duxs12_mlc_write_event_config,
};

static struct
iio_dev *st_lis2duxs12_mlc_alloc_iio_dev(struct st_lis2duxs12_hw *hw,
					 enum st_lis2duxs12_sensor_id id)
{
	struct st_lis2duxs12_sensor *sensor;
	struct iio_chan_spec *channels;
	struct iio_dev *iio_dev;

	/* devm management only for ST_LIS2DUXS12_ID_MLC */
	if (id == ST_LIS2DUXS12_ID_MLC) {
		iio_dev = devm_iio_device_alloc(hw->dev, sizeof(*sensor));
	} else {
#if KERNEL_VERSION(5, 9, 0) <= LINUX_VERSION_CODE
		iio_dev = iio_device_alloc(NULL, sizeof(*sensor));
#else /* LINUX_VERSION_CODE */
		iio_dev = iio_device_alloc(sizeof(*sensor));
#endif /* LINUX_VERSION_CODE */
	}

	if (!iio_dev)
		return NULL;

	iio_dev->modes = INDIO_DIRECT_MODE;
	iio_dev->dev.parent = hw->dev;

	sensor = iio_priv(iio_dev);
	sensor->id = id;
	sensor->hw = hw;
	sensor->pm = ST_LIS2DUXS12_NO_MODE;

	switch (id) {
	case ST_LIS2DUXS12_ID_MLC: {
		const struct iio_chan_spec st_lis2duxs12_mlc_channels[] = {
			ST_LIS2DUXS12_EVENT_CHANNEL(IIO_ACTIVITY, thr),
		};

		channels = devm_kzalloc(hw->dev,
					sizeof(st_lis2duxs12_mlc_channels),
					GFP_KERNEL);
		if (!channels)
			return NULL;

		memcpy(channels, st_lis2duxs12_mlc_channels,
		       sizeof(st_lis2duxs12_mlc_channels));

		iio_dev->available_scan_masks =
					 st_lis2duxs12_mlc_available_scan_masks;
		iio_dev->channels = channels;
		iio_dev->num_channels = ARRAY_SIZE(st_lis2duxs12_mlc_channels);
		iio_dev->info = &st_lis2duxs12_mlc_event_info;
		scnprintf(sensor->name, sizeof(sensor->name),
			  "%s_mlc", hw->settings->id.name);
		break;
	}
	case ST_LIS2DUXS12_ID_MLC_0:
	case ST_LIS2DUXS12_ID_MLC_1:
	case ST_LIS2DUXS12_ID_MLC_2:
	case ST_LIS2DUXS12_ID_MLC_3: {
		const struct iio_chan_spec st_lis2duxs12_mlc_x_ch[] = {
			ST_LIS2DUXS12_EVENT_CHANNEL(IIO_ACTIVITY, thr),
		};

		channels = kzalloc(sizeof(st_lis2duxs12_mlc_x_ch),
				   GFP_KERNEL);
		if (!channels)
			return NULL;

		memcpy(channels, st_lis2duxs12_mlc_x_ch,
		       sizeof(st_lis2duxs12_mlc_x_ch));

		iio_dev->available_scan_masks =
					 st_lis2duxs12_mlc_available_scan_masks;
		iio_dev->channels = channels;
		iio_dev->num_channels = ARRAY_SIZE(st_lis2duxs12_mlc_x_ch);
		iio_dev->info = &st_lis2duxs12_mlc_x_event_info;
		scnprintf(sensor->name, sizeof(sensor->name),
			  "%s_mlc_%d", hw->settings->id.name,
			  id - ST_LIS2DUXS12_ID_MLC_0);
		sensor->outreg_addr = ST_LIS2DUXS12_MLC1_SRC_ADDR + id -
				      ST_LIS2DUXS12_ID_MLC_0;
		sensor->status = ST_LIS2DUXS12_MLC_ENABLED;
		sensor->odr = hw->mlc_config->requested_odr;
		sensor->uodr = 0;
		break;
	}
	case ST_LIS2DUXS12_ID_FSM_0:
	case ST_LIS2DUXS12_ID_FSM_1:
	case ST_LIS2DUXS12_ID_FSM_2:
	case ST_LIS2DUXS12_ID_FSM_3:
	case ST_LIS2DUXS12_ID_FSM_4:
	case ST_LIS2DUXS12_ID_FSM_5:
	case ST_LIS2DUXS12_ID_FSM_6:
	case ST_LIS2DUXS12_ID_FSM_7: {
		const struct iio_chan_spec st_lis2duxs12_fsm_x_ch[] = {
			ST_LIS2DUXS12_EVENT_CHANNEL(IIO_ACTIVITY, thr),
		};

		channels = kzalloc(sizeof(st_lis2duxs12_fsm_x_ch),
				   GFP_KERNEL);
		if (!channels)
			return NULL;

		memcpy(channels, st_lis2duxs12_fsm_x_ch,
		       sizeof(st_lis2duxs12_fsm_x_ch));

		iio_dev->available_scan_masks =
			st_lis2duxs12_mlc_available_scan_masks;
		iio_dev->channels = channels;
		iio_dev->num_channels = ARRAY_SIZE(st_lis2duxs12_fsm_x_ch);
		iio_dev->info = &st_lis2duxs12_mlc_x_event_info;
		scnprintf(sensor->name, sizeof(sensor->name),
			  "%s_fsm_%d", hw->settings->id.name,
			  id - ST_LIS2DUXS12_ID_FSM_0);
		sensor->outreg_addr = ST_LIS2DUXS12_FSM_OUTS1_ADDR +
				id - ST_LIS2DUXS12_ID_FSM_0;
		sensor->status = ST_LIS2DUXS12_FSM_ENABLED;
		sensor->odr = hw->mlc_config->requested_odr;
		sensor->uodr = 0;
		break;
	}
	default:
		dev_err(hw->dev, "invalid sensor id %d\n", id);

		return NULL;
	}

	iio_dev->name = sensor->name;

	return iio_dev;
}

int st_lis2duxs12_mlc_check_status(struct st_lis2duxs12_hw *hw)
{
	struct st_lis2duxs12_sensor *sensor;
	u8 i, mlc_status, id, event[8];
	struct iio_dev *iio_dev;
	u8 fsm_status;
	int err = 0;

	if (hw->mlc_config->status & ST_LIS2DUXS12_MLC_ENABLED) {
		err = st_lis2duxs12_read_locked(hw,
				 ST_LIS2DUXS12_MLC_STATUS_MAINPAGE_ADDR,
				 (void *)&mlc_status, 1);
		if (err)
			return err;

		if (mlc_status) {
			for (i = 0; i < ST_LIS2DUXS12_MLC_MAX_NUMBER; i++) {
				id = st_lis2duxs12_mlc_sensor_list[i];
				if (!(hw->enable_mask & BIT(id)))
					continue;

				if (mlc_status & BIT(i)) {
					iio_dev = hw->iio_devs[id];
					if (!iio_dev) {
						err = -ENOENT;

						return err;
					}

					sensor = iio_priv(iio_dev);
					err = st_lis2duxs12_read_page_locked(hw,
							  sensor->outreg_addr,
							  (void *)&event[i], 1);
					if (err)
						return err;

					iio_push_event(iio_dev, (u64)event[i],
						       iio_get_time_ns(iio_dev));

					dev_info(hw->dev,
						 "MLC %d Status %x MLC EVENT %llx\n",
						 id, mlc_status, (u64)event[i]);
				}
			}
		}
	}

	if (hw->mlc_config->status & ST_LIS2DUXS12_FSM_ENABLED) {
		err = st_lis2duxs12_read_locked(hw,
					 ST_LIS2DUXS12_FSM_STATUS_MAINPAGE_ADDR,
					 (void *)&fsm_status, 1);
		if (err)
			return err;

		if (fsm_status) {
			for (i = 0; i < ST_LIS2DUXS12_FSM_MAX_NUMBER; i++) {
				id = st_lis2duxs12_fsm_sensor_list[i];
				if (!(hw->enable_mask & BIT(id)))
					continue;

				if (fsm_status & BIT(i)) {
					iio_dev = hw->iio_devs[id];
					if (!iio_dev) {
						err = -ENOENT;

						return err;
					}

					sensor = iio_priv(iio_dev);
					err = st_lis2duxs12_read_page_locked(hw,
							  sensor->outreg_addr,
							  (void *)&event[i], 1);
					if (err)
						return err;

					iio_push_event(iio_dev, (u64)event[i],
						       iio_get_time_ns(iio_dev));

					dev_info(hw->dev,
						 "FSM %d Status %x FSM EVENT %llx\n",
						 id, fsm_status, (u64)event[i]);
				}
			}
		}
	}

	return err;
}

int st_lis2duxs12_mlc_init_preload(struct st_lis2duxs12_hw *hw)
{
	hw->preload_mlc = 1;
	st_lis2duxs12_mlc_update(&st_lis2duxs12_mlc_preload, hw);

	return 0;
}

int st_lis2duxs12_mlc_probe(struct st_lis2duxs12_hw *hw)
{
	hw->iio_devs[ST_LIS2DUXS12_ID_MLC] =
		      st_lis2duxs12_mlc_alloc_iio_dev(hw, ST_LIS2DUXS12_ID_MLC);
	if (!hw->iio_devs[ST_LIS2DUXS12_ID_MLC])
		return -ENOMEM;

	hw->mlc_config = devm_kzalloc(hw->dev,
				      sizeof(struct st_lis2duxs12_mlc_config_t),
				      GFP_KERNEL);
	if (!hw->mlc_config)
		return -ENOMEM;

	return 0;
}

int st_lis2duxs12_mlc_remove(struct device *dev)
{
	struct st_lis2duxs12_hw *hw = dev_get_drvdata(dev);

	return st_lis2duxs12_mlc_flush_all(hw);
}
EXPORT_SYMBOL(st_lis2duxs12_mlc_remove);
