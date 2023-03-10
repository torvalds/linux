// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_lsm6dsvx machine learning core driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2022 STMicroelectronics Inc.
 */

#include <linux/delay.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/iio/buffer.h>
#include <linux/iio/events.h>
#include <linux/firmware.h>
#include <linux/version.h>
#include <linux/slab.h>

#include <linux/platform_data/st_sensors_pdata.h>

#include "st_lsm6dsvx.h"

#define ST_LSM6DSVX_MLC_LOADER_VERSION		"0.3"

/* number of machine learning core available on device hardware */
#define ST_LSM6DSVX_MLC_MAX_NUMBER		4
#define ST_LSM6DSVX_FSM_MAX_NUMBER		8

#ifdef CONFIG_IIO_LSM6DSVX_MLC_BUILTIN_FIRMWARE
static const u8 st_lsm6dsvx_mlc_fw[] = {
	#include "st_lsm6dsvx_mlc.fw"
};
DECLARE_BUILTIN_FIRMWARE(LSM6DSVX_MLC_FIRMWARE_NAME,
			 st_lsm6dsvx_mlc_fw);
#else /* CONFIG_IIO_LSM6DSVX_MLC_BUILTIN_FIRMWARE */
#define LSM6DSVX_MLC_FIRMWARE_NAME		"st_lsm6dsvx_mlc.bin"
#endif /* CONFIG_IIO_LSM6DSVX_MLC_BUILTIN_FIRMWARE */

#ifdef CONFIG_IIO_ST_LSM6DSVX_MLC_PRELOAD
#include "st_lsm6dsvx_preload_mlc.h"
#endif /* CONFIG_IIO_ST_LSM6DSVX_MLC_PRELOAD */

/* converts MLC odr to main sensor trigger odr (acc) */
static const uint16_t mlc_odr_data[] = {
	[0x00] = 15,
	[0x01] = 30,
	[0x02] = 50,
	[0x03] = 120,
	[0x04] = 240,
};

static const uint16_t fsm_odr_data[] = {
	[0x00] = 15,
	[0x01] = 30,
	[0x02] = 50,
	[0x03] = 120,
	[0x04] = 240,
	[0x05] = 480,
	[0x06] = 960,
};

static struct iio_dev *
st_lsm6dsvx_mlc_alloc_iio_dev(struct st_lsm6dsvx_hw *hw,
			      enum st_lsm6dsvx_sensor_id id);

static const unsigned long st_lsm6dsvx_mlc_available_scan_masks[] = {
	0x1, 0x0
};

static inline int
st_lsm6dsvx_read_page_locked(struct st_lsm6dsvx_hw *hw, unsigned int addr,
			     void *val, unsigned int len)
{
	int err;

	st_lsm6dsvx_set_page_access(hw, ST_LSM6DSVX_EMB_FUNC_REG_ACCESS_MASK, 1);
	err = regmap_bulk_read(hw->regmap, addr, val, len);
	st_lsm6dsvx_set_page_access(hw, ST_LSM6DSVX_EMB_FUNC_REG_ACCESS_MASK, 0);
	mutex_unlock(&hw->page_lock);

	return err;
}

static inline int
st_lsm6dsvx_write_page_locked(struct st_lsm6dsvx_hw *hw, unsigned int addr,
			      unsigned int *val, unsigned int len)
{
	int err;

	mutex_lock(&hw->page_lock);
	st_lsm6dsvx_set_page_access(hw, ST_LSM6DSVX_EMB_FUNC_REG_ACCESS_MASK, 1);
	err = regmap_bulk_write(hw->regmap, addr, val, len);
	st_lsm6dsvx_set_page_access(hw, ST_LSM6DSVX_EMB_FUNC_REG_ACCESS_MASK, 0);
	mutex_unlock(&hw->page_lock);

	return err;
}

static inline int
st_lsm6dsvx_update_page_bits_locked(struct st_lsm6dsvx_hw *hw,
				    unsigned int addr, unsigned int mask,
				    unsigned int val)
{
	int err;

	mutex_lock(&hw->page_lock);
	st_lsm6dsvx_set_page_access(hw, ST_LSM6DSVX_EMB_FUNC_REG_ACCESS_MASK, 1);
	err = regmap_update_bits(hw->regmap, addr, mask, val);
	st_lsm6dsvx_set_page_access(hw, ST_LSM6DSVX_EMB_FUNC_REG_ACCESS_MASK, 0);
	mutex_unlock(&hw->page_lock);

	return err;
}

static int
st_lsm6dsvx_mlc_enable_sensor(struct st_lsm6dsvx_sensor *sensor, bool enable)
{
	struct st_lsm6dsvx_hw *hw = sensor->hw;
	int i, id, err = 0;

	/* enable acc sensor as trigger */
	err = st_lsm6dsvx_sensor_set_enable(sensor, enable);
	if (err < 0)
		return err;

	if (sensor->status == ST_LSM6DSVX_MLC_ENABLED) {
		int value;

		value = enable ? hw->mlc_config->mlc_int_mask : 0;
		err = st_lsm6dsvx_write_page_locked(hw,
						hw->mlc_config->mlc_int_addr,
						&value, 1);
		if (err < 0)
			return err;

		/*
		 * enable mlc core
		 * only one mlc so not need to check if other running
		 */
		err = st_lsm6dsvx_update_page_bits_locked(hw,
				ST_LSM6DSVX_REG_EMB_FUNC_EN_B_ADDR,
				ST_LSM6DSVX_MLC_EN_MASK,
				ST_LSM6DSVX_SHIFT_VAL(enable,
					    ST_LSM6DSVX_MLC_EN_MASK));
		if (err < 0)
			return err;

		dev_info(sensor->hw->dev,
			"Enabling MLC sensor %d to %d (INT %x)\n",
			sensor->id, enable, value);
	} else if (sensor->status == ST_LSM6DSVX_FSM_ENABLED) {
		int value;

		value = enable ? hw->mlc_config->fsm_int_mask : 0;
		err = st_lsm6dsvx_write_page_locked(hw,
						hw->mlc_config->fsm_int_addr,
						&value, 1);
		if (err < 0)
			return err;

		/* enable fsm core */
		for (i = 0; i < ST_LSM6DSVX_FSM_MAX_NUMBER; i++) {
			id = st_lsm6dsvx_fsm_sensor_list[i];
			if (hw->enable_mask & BIT(id))
				break;
		}

		/* check for any other fsm already enabled */
		if (enable || i == ST_LSM6DSVX_FSM_MAX_NUMBER) {
			err = st_lsm6dsvx_update_page_bits_locked(hw,
				     ST_LSM6DSVX_REG_EMB_FUNC_EN_B_ADDR,
				     ST_LSM6DSVX_FSM_EN_MASK,
				     ST_LSM6DSVX_SHIFT_VAL(enable,
						      ST_LSM6DSVX_FSM_EN_MASK));
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
st_lsm6dsvx_mlc_write_event_config(struct iio_dev *iio_dev,
				   const struct iio_chan_spec *chan,
				   enum iio_event_type type,
				   enum iio_event_direction dir, int state)
{
	struct st_lsm6dsvx_sensor *sensor = iio_priv(iio_dev);

	return st_lsm6dsvx_mlc_enable_sensor(sensor, state);
}

static int
st_lsm6dsvx_mlc_read_event_config(struct iio_dev *iio_dev,
				  const struct iio_chan_spec *chan,
				  enum iio_event_type type,
				  enum iio_event_direction dir)
{
	struct st_lsm6dsvx_sensor *sensor = iio_priv(iio_dev);
	struct st_lsm6dsvx_hw *hw = sensor->hw;

	return !!(hw->enable_mask & BIT(sensor->id));
}

/*
 * st_lsm6dsvx_verify_mlc_fsm_support - Verify device supports MLC/FSM
 *
 * Before to load a MLC/FSM configuration check the MLC/FSM HW block
 * available for this hw device id.
 */
static int st_lsm6dsvx_verify_mlc_fsm_support(const struct firmware *fw,
					      struct st_lsm6dsvx_hw *hw)
{
	bool stmc_page = false;
	u8 reg, val;
	int i = 0;

	while (i < fw->size) {
		reg = fw->data[i++];
		val = fw->data[i++];

		if (reg == ST_LSM6DSVX_REG_FUNC_CFG_ACCESS_ADDR &&
		    (val & ST_LSM6DSVX_EMB_FUNC_REG_ACCESS_MASK)) {
			stmc_page = true;
		} else if (reg == ST_LSM6DSVX_REG_FUNC_CFG_ACCESS_ADDR &&
			   (val & ~ST_LSM6DSVX_EMB_FUNC_REG_ACCESS_MASK)) {
			stmc_page = false;
		} else if (stmc_page) {
			switch (reg) {
			case ST_LSM6DSVX_REG_MLC_INT1_ADDR:
			case ST_LSM6DSVX_REG_MLC_INT2_ADDR:
				if (!hw->settings->st_mlc_probe)
					return -ENODEV;
				break;
			case ST_LSM6DSVX_REG_FSM_INT1_ADDR:
			case ST_LSM6DSVX_REG_FSM_INT2_ADDR:
				if (!hw->settings->st_fsm_probe)
					return -ENODEV;
				break;
			default:
				break;
			}
		}
	}

	return 0;
}

/* parse and program mlc fragments */
static int st_lsm6dsvx_program_mlc(const struct firmware *fw,
				   struct st_lsm6dsvx_hw *hw)
{
	u8 mlc_int = 0, mlc_num = 0, fsm_num = 0, skip = 0;
	u8 fsm_int = 0, reg, val, req_odr = 0;
	bool stmc_page = false;
	int ret, i = 0;

	while (i < fw->size) {
		reg = fw->data[i++];
		val = fw->data[i++];

		if (reg == ST_LSM6DSVX_REG_FUNC_CFG_ACCESS_ADDR &&
		    (val & ST_LSM6DSVX_EMB_FUNC_REG_ACCESS_MASK)) {
			stmc_page = true;
		} else if (reg == ST_LSM6DSVX_REG_FUNC_CFG_ACCESS_ADDR &&
			   (val & ~ST_LSM6DSVX_EMB_FUNC_REG_ACCESS_MASK)) {
			stmc_page = false;
		} else if (stmc_page) {
			switch (reg) {
			case ST_LSM6DSVX_REG_MLC_INT1_ADDR:
			case ST_LSM6DSVX_REG_MLC_INT2_ADDR:
				mlc_int |= val;
				mlc_num++;
				skip = 1;
				break;
			case ST_LSM6DSVX_REG_FSM_INT1_ADDR:
			case ST_LSM6DSVX_REG_FSM_INT2_ADDR:
				fsm_int |= val;
				fsm_num++;
				skip = 1;
				break;
			case ST_LSM6DSVX_REG_EMB_FUNC_EN_B_ADDR:
				skip = 1;
				break;
			default:
				break;
			}
		} else if (reg == ST_LSM6DSVX_REG_CTRL1_ADDR) {
			/* save required xl odr and skip write to reg */
			req_odr = max_t(u8, req_odr, (val & GENMASK(3, 0)));
			skip = 1;
		}

		if (!skip) {
			ret = regmap_write(hw->regmap, reg, val);
			if (ret) {
				dev_err(hw->dev,
					"regmap_write fails\n");

				return ret;
			}
		}

		skip = 0;

		if (mlc_num >= ST_LSM6DSVX_MLC_MAX_NUMBER ||
		    fsm_num >= ST_LSM6DSVX_FSM_MAX_NUMBER)
			break;
	}

	hw->mlc_config->bin_len = fw->size;

	if (mlc_num) {
		hw->mlc_config->mlc_int_mask = mlc_int;
		hw->mlc_config->mlc_int_addr = (hw->int_pin == 1 ?
					 ST_LSM6DSVX_REG_MLC_INT1_ADDR :
					 ST_LSM6DSVX_REG_MLC_INT2_ADDR);

		hw->mlc_config->status |= ST_LSM6DSVX_MLC_ENABLED;
		hw->mlc_config->mlc_configured += mlc_num;
		hw->mlc_config->requested_odr = mlc_odr_data[req_odr];
	}

	if (fsm_num) {
		hw->mlc_config->fsm_int_mask = fsm_int;
		hw->mlc_config->fsm_int_addr = (hw->int_pin == 1 ?
					 ST_LSM6DSVX_REG_FSM_INT1_ADDR :
					 ST_LSM6DSVX_REG_FSM_INT2_ADDR);

		hw->mlc_config->status |= ST_LSM6DSVX_FSM_ENABLED;
		hw->mlc_config->fsm_configured += fsm_num;
		hw->mlc_config->requested_odr = fsm_odr_data[req_odr];
	}

	return fsm_num + mlc_num;
}

static void st_lsm6dsvx_mlc_update(const struct firmware *fw, void *context)
{
	struct st_lsm6dsvx_hw *hw = context;
	enum st_lsm6dsvx_sensor_id id;
	int ret, i;

	if (!fw) {
		dev_err(hw->dev, "could not get binary firmware\n");

		return;
	}

	ret = st_lsm6dsvx_verify_mlc_fsm_support(fw, hw);
	if (ret) {
		dev_err(hw->dev, "invalid file format for device\n");

		return;
	}

	ret = st_lsm6dsvx_program_mlc(fw, hw);
	if (ret > 0) {
		u8 fsm_mask = hw->mlc_config->fsm_int_mask;
		u8 mlc_mask = hw->mlc_config->mlc_int_mask;

		dev_info(hw->dev,
			 "MLC loaded (%d) MLC %01x FSM %02x\n",
			 ret, mlc_mask, fsm_mask);

		for (i = 0; i < ST_LSM6DSVX_MLC_MAX_NUMBER; i++) {
			if (mlc_mask & BIT(i)) {
				id = st_lsm6dsvx_mlc_sensor_list[i];
				hw->iio_devs[id] = st_lsm6dsvx_mlc_alloc_iio_dev(hw, id);
				if (!hw->iio_devs[id])
					goto release;

				ret = iio_device_register(hw->iio_devs[id]);
				if (ret)
					goto release;
			}
		}

		for (i = 0; i < ST_LSM6DSVX_FSM_MAX_NUMBER; i++) {
			if (fsm_mask & BIT(i)) {
				id = st_lsm6dsvx_fsm_sensor_list[i];
				hw->iio_devs[id] = st_lsm6dsvx_mlc_alloc_iio_dev(hw, id);
				if (!hw->iio_devs[id])
					goto release;

				ret = iio_device_register(hw->iio_devs[id]);
				if (ret)
					goto release;
			}
		}
	}

release:
	/*
	 * internal firmware don't release it because stored in
	 * const segment
	 */
	if (hw->preload_mlc) {
		hw->preload_mlc = 0;

		return;
	}

	release_firmware(fw);
}

static int st_lsm6dsvx_mlc_flush_single(struct st_lsm6dsvx_hw *hw,
					enum st_lsm6dsvx_sensor_id id)
{
	struct st_lsm6dsvx_sensor *sensor_mlc;
	struct iio_dev *iio_dev;
	int ret;

	iio_dev = hw->iio_devs[id];
	if (!iio_dev)
		return -ENODEV;

	sensor_mlc = iio_priv(iio_dev);
	ret = st_lsm6dsvx_mlc_enable_sensor(sensor_mlc, false);
	if (ret < 0)
		return ret;

	iio_device_unregister(iio_dev);
	kfree(iio_dev->channels);
	iio_device_free(iio_dev);
	hw->iio_devs[id] = NULL;

	return 0;
}

static int st_lsm6dsvx_mlc_flush_all(struct st_lsm6dsvx_hw *hw)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(st_lsm6dsvx_mlc_sensor_list); i++)
		st_lsm6dsvx_mlc_flush_single(hw, st_lsm6dsvx_mlc_sensor_list[i]);

	for (i = 0; i < ARRAY_SIZE(st_lsm6dsvx_fsm_sensor_list); i++)
		st_lsm6dsvx_mlc_flush_single(hw, st_lsm6dsvx_fsm_sensor_list[i]);

	return 0;
}

static ssize_t st_lsm6dsvx_mlc_info(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct st_lsm6dsvx_sensor *sensor = iio_priv(dev_to_iio_dev(dev));
	struct st_lsm6dsvx_hw *hw = sensor->hw;

	return scnprintf(buf, PAGE_SIZE, "mlc %02x fsm %02x\n",
			 hw->mlc_config->mlc_configured,
			 hw->mlc_config->fsm_configured);
}

static ssize_t
st_lsm6dsvx_mlc_get_version(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "mlc loader Version %s\n",
			 ST_LSM6DSVX_MLC_LOADER_VERSION);
}

static ssize_t st_lsm6dsvx_mlc_flush(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t size)
{
	struct st_lsm6dsvx_sensor *sensor = iio_priv(dev_to_iio_dev(dev));
	struct st_lsm6dsvx_hw *hw = sensor->hw;
	int ret;

	ret = st_lsm6dsvx_mlc_flush_all(hw);
	memset(hw->mlc_config, 0, sizeof(*hw->mlc_config));

	return ret < 0 ? ret : size;
}

static ssize_t
st_lsm6dsvx_mlc_upload_firmware(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	struct st_lsm6dsvx_sensor *sensor = iio_priv(dev_to_iio_dev(dev));
	int err;

	err = request_firmware_nowait(THIS_MODULE, true,
				      LSM6DSVX_MLC_FIRMWARE_NAME,
				      dev, GFP_KERNEL,
				      sensor->hw,
				      st_lsm6dsvx_mlc_update);

	return err < 0 ? err : size;
}

static ssize_t st_lsm6dsvx_mlc_odr(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct st_lsm6dsvx_sensor *sensor = iio_priv(dev_to_iio_dev(dev));
	struct st_lsm6dsvx_hw *hw = sensor->hw;

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			 hw->mlc_config->requested_odr);
}

static IIO_DEVICE_ATTR(config_info, 0444, st_lsm6dsvx_mlc_info, NULL, 0);
static IIO_DEVICE_ATTR(flush_config, 0200, NULL, st_lsm6dsvx_mlc_flush, 0);
static IIO_DEVICE_ATTR(loader_version, 0444,
		       st_lsm6dsvx_mlc_get_version, NULL, 0);
static IIO_DEVICE_ATTR(load_mlc, 0200,
		       NULL, st_lsm6dsvx_mlc_upload_firmware, 0);
static IIO_DEVICE_ATTR(odr, 0444, st_lsm6dsvx_mlc_odr, NULL, 0);
static IIO_DEVICE_ATTR(module_id, 0444, st_lsm6dsvx_get_module_id, NULL, 0);

static struct attribute *st_lsm6dsvx_mlc_event_attributes[] = {
	&iio_dev_attr_config_info.dev_attr.attr,
	&iio_dev_attr_loader_version.dev_attr.attr,
	&iio_dev_attr_load_mlc.dev_attr.attr,
	&iio_dev_attr_flush_config.dev_attr.attr,
	&iio_dev_attr_odr.dev_attr.attr,
	&iio_dev_attr_module_id.dev_attr.attr,
	NULL,
};

static const struct attribute_group
st_lsm6dsvx_mlc_event_attribute_group = {
	.attrs = st_lsm6dsvx_mlc_event_attributes,
};

static const struct iio_info st_lsm6dsvx_mlc_event_info = {
	.attrs = &st_lsm6dsvx_mlc_event_attribute_group,
	.read_event_config = st_lsm6dsvx_mlc_read_event_config,
	.write_event_config = st_lsm6dsvx_mlc_write_event_config,
};

static ssize_t st_lsm6dsvx_mlc_x_odr(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct st_lsm6dsvx_sensor *sensor = iio_priv(dev_to_iio_dev(dev));

	return scnprintf(buf, PAGE_SIZE, "%d.%02d\n",
			 sensor->odr, sensor->uodr);
}

static IIO_DEVICE_ATTR(odr_x, 0444, st_lsm6dsvx_mlc_x_odr, NULL, 0);

static struct attribute *st_lsm6dsvx_mlc_x_event_attributes[] = {
	&iio_dev_attr_odr_x.dev_attr.attr,
	&iio_dev_attr_module_id.dev_attr.attr,
	NULL,
};

static const struct attribute_group
st_lsm6dsvx_mlc_x_event_attribute_group = {
	.attrs = st_lsm6dsvx_mlc_x_event_attributes,
};
static const struct iio_info st_lsm6dsvx_mlc_x_event_info = {
	.attrs = &st_lsm6dsvx_mlc_x_event_attribute_group,
	.read_event_config = st_lsm6dsvx_mlc_read_event_config,
	.write_event_config = st_lsm6dsvx_mlc_write_event_config,
};

static struct iio_dev *
st_lsm6dsvx_mlc_alloc_iio_dev(struct st_lsm6dsvx_hw *hw,
			      enum st_lsm6dsvx_sensor_id id)
{
	struct st_lsm6dsvx_sensor *sensor;
	struct iio_chan_spec *channels;
	struct iio_dev *iio_dev;

	/* devm management only for ST_LSM6DSVX_ID_MLC */
	if (id == ST_LSM6DSVX_ID_MLC) {
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

	switch (id) {
	case ST_LSM6DSVX_ID_MLC: {
		const struct iio_chan_spec st_lsm6dsvx_mlc_channels[] = {
			ST_LSM6DSVX_EVENT_CHANNEL(IIO_ACTIVITY, thr),
		};

		channels = devm_kzalloc(hw->dev,
					sizeof(st_lsm6dsvx_mlc_channels),
					GFP_KERNEL);
		if (!channels)
			return NULL;

		memcpy(channels, st_lsm6dsvx_mlc_channels,
		       sizeof(st_lsm6dsvx_mlc_channels));

		iio_dev->available_scan_masks =
			st_lsm6dsvx_mlc_available_scan_masks;
		iio_dev->channels = channels;
		iio_dev->num_channels = ARRAY_SIZE(st_lsm6dsvx_mlc_channels);
		iio_dev->info = &st_lsm6dsvx_mlc_event_info;
		scnprintf(sensor->name, sizeof(sensor->name),
			  "%s_loader", hw->settings->id.name);
		break;
	}
	case ST_LSM6DSVX_ID_MLC_0:
	case ST_LSM6DSVX_ID_MLC_1:
	case ST_LSM6DSVX_ID_MLC_2:
	case ST_LSM6DSVX_ID_MLC_3: {
		const struct iio_chan_spec st_lsm6dsvx_mlc_x_ch[] = {
			ST_LSM6DSVX_EVENT_CHANNEL(IIO_ACTIVITY, thr),
		};

		channels = kzalloc(sizeof(st_lsm6dsvx_mlc_x_ch), GFP_KERNEL);
		if (!channels)
			return NULL;

		memcpy(channels, st_lsm6dsvx_mlc_x_ch,
		       sizeof(st_lsm6dsvx_mlc_x_ch));

		iio_dev->available_scan_masks =
			st_lsm6dsvx_mlc_available_scan_masks;
		iio_dev->channels = channels;
		iio_dev->num_channels = ARRAY_SIZE(st_lsm6dsvx_mlc_x_ch);
		iio_dev->info = &st_lsm6dsvx_mlc_x_event_info;
		scnprintf(sensor->name, sizeof(sensor->name),
			  "%s_mlc_%d", hw->settings->id.name,
			  id - ST_LSM6DSVX_ID_MLC_0);
		sensor->outreg_addr = ST_LSM6DSVX_REG_MLC1_SRC_ADDR +
				      id - ST_LSM6DSVX_ID_MLC_0;
		sensor->status = ST_LSM6DSVX_MLC_ENABLED;
		sensor->odr = hw->mlc_config->requested_odr;
		sensor->uodr = 0;

		break;
	}
	case ST_LSM6DSVX_ID_FSM_0:
	case ST_LSM6DSVX_ID_FSM_1:
	case ST_LSM6DSVX_ID_FSM_2:
	case ST_LSM6DSVX_ID_FSM_3:
	case ST_LSM6DSVX_ID_FSM_4:
	case ST_LSM6DSVX_ID_FSM_5:
	case ST_LSM6DSVX_ID_FSM_6:
	case ST_LSM6DSVX_ID_FSM_7: {
		const struct iio_chan_spec st_lsm6dsvx_fsm_x_ch[] = {
			ST_LSM6DSVX_EVENT_CHANNEL(IIO_ACTIVITY, thr),
		};

		channels = kzalloc(sizeof(st_lsm6dsvx_fsm_x_ch), GFP_KERNEL);
		if (!channels)
			return NULL;

		memcpy(channels, st_lsm6dsvx_fsm_x_ch,
		       sizeof(st_lsm6dsvx_fsm_x_ch));

		iio_dev->available_scan_masks =
			st_lsm6dsvx_mlc_available_scan_masks;
		iio_dev->channels = channels;
		iio_dev->num_channels = ARRAY_SIZE(st_lsm6dsvx_fsm_x_ch);
		iio_dev->info = &st_lsm6dsvx_mlc_x_event_info;
		scnprintf(sensor->name, sizeof(sensor->name),
			  "%s_fsm_%d", hw->settings->id.name,
			  id - ST_LSM6DSVX_ID_FSM_0);
		sensor->outreg_addr = ST_LSM6DSVX_REG_FSM_OUTS1_ADDR +
				      id - ST_LSM6DSVX_ID_FSM_0;
		sensor->status = ST_LSM6DSVX_FSM_ENABLED;
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

int st_lsm6dsvx_mlc_check_status(struct st_lsm6dsvx_hw *hw)
{
	struct st_lsm6dsvx_sensor *sensor;
	u8 i, mlc_status, id, event[8];
	struct iio_dev *iio_dev;
	u8 fsm_status;
	int err = 0;

	if (hw->mlc_config->status & ST_LSM6DSVX_MLC_ENABLED) {
		err = st_lsm6dsvx_read_locked(hw,
				       ST_LSM6DSVX_REG_MLC_STATUS_MAINPAGE_ADDR,
				       (void *)&mlc_status, 1);
		if (err)
			return err;

		if (mlc_status) {
			for (i = 0; i < ST_LSM6DSVX_MLC_MAX_NUMBER; i++) {
				id = st_lsm6dsvx_mlc_sensor_list[i];
				if (!(hw->enable_mask & BIT(id)))
					continue;

				if (mlc_status & BIT(i)) {
					iio_dev = hw->iio_devs[id];
					if (!iio_dev) {
						err = -ENOENT;

						return err;
					}

					sensor = iio_priv(iio_dev);
					err = st_lsm6dsvx_read_page_locked(hw,
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

	if (hw->mlc_config->status & ST_LSM6DSVX_FSM_ENABLED) {
		err = st_lsm6dsvx_read_locked(hw,
				ST_LSM6DSVX_REG_FSM_STATUS_MAINPAGE_ADDR,
				(void *)&fsm_status, 1);
		if (err)
			return err;

		if (fsm_status) {
			for (i = 0; i < ST_LSM6DSVX_FSM_MAX_NUMBER; i++) {
				id = st_lsm6dsvx_fsm_sensor_list[i];
				if (!(hw->enable_mask & BIT(id)))
					continue;

				if (fsm_status & BIT(i)) {
					iio_dev = hw->iio_devs[id];
					if (!iio_dev) {
						err = -ENOENT;

						return err;
					}

					sensor = iio_priv(iio_dev);
					err = st_lsm6dsvx_read_page_locked(hw,
							  sensor->outreg_addr,
							  (void *)&event[i], 1);
					if (err)
						return err;

					iio_push_event(iio_dev, (u64)event[i],
						       iio_get_time_ns(iio_dev));

					dev_info(hw->dev,
						 "FSM %d Status %x FSM EVENT %llx\n",
						 id, mlc_status, (u64)event[i]);
				}
			}
		}
	}

	return err;
}

int st_lsm6dsvx_mlc_init_preload(struct st_lsm6dsvx_hw *hw)
{
	hw->preload_mlc = 1;
	st_lsm6dsvx_mlc_update(&st_lsm6dsvx_mlc_preload, hw);

	return 0;
}

int st_lsm6dsvx_mlc_probe(struct st_lsm6dsvx_hw *hw)
{
	hw->iio_devs[ST_LSM6DSVX_ID_MLC] =
		st_lsm6dsvx_mlc_alloc_iio_dev(hw, ST_LSM6DSVX_ID_MLC);
	if (!hw->iio_devs[ST_LSM6DSVX_ID_MLC])
		return -ENOMEM;

	hw->mlc_config = devm_kzalloc(hw->dev,
				sizeof(struct st_lsm6dsvx_mlc_config_t),
				GFP_KERNEL);
	if (!hw->mlc_config)
		return -ENOMEM;

	return 0;
}

int st_lsm6dsvx_mlc_remove(struct device *dev)
{
	struct st_lsm6dsvx_hw *hw = dev_get_drvdata(dev);

	return st_lsm6dsvx_mlc_flush_all(hw);
}
EXPORT_SYMBOL(st_lsm6dsvx_mlc_remove);
