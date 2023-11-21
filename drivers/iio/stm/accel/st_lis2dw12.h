/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * STMicroelectronics lis2dw12 driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2016 STMicroelectronics Inc.
 */

#ifndef ST_LIS2DW12_H
#define ST_LIS2DW12_H

#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/iio/events.h>
#include <linux/iio/iio.h>
#include <linux/of_device.h>
#include <linux/regmap.h>

#include "../common/stm_iio_types.h"

#define ST_LIS2DW12_DEV_NAME		"lis2dw12"
#define ST_IIS2DLPC_DEV_NAME		"iis2dlpc"
#define ST_AIS2IH_DEV_NAME		"ais2ih"
#define ST_LIS2DW12_MAX_WATERMARK	31
#define ST_LIS2DW12_DATA_SIZE		6

#define ST_LIS2DW12_SHIFT_VAL(val, mask) (((val) << __ffs(mask)) & \
					  (mask))

enum st_lis2dw12_fifo_mode {
	ST_LIS2DW12_FIFO_BYPASS = 0x0,
	ST_LIS2DW12_FIFO_CONTINUOUS = 0x6,
};

enum st_lis2dw12_selftest_status {
	ST_LIS2DW12_ST_RESET,
	ST_LIS2DW12_ST_PASS,
	ST_LIS2DW12_ST_FAIL,
};

enum st_lis2dw12_sensor_id {
	ST_LIS2DW12_ID_ACC,
	ST_LIS2DW12_ID_TAP_TAP,
	ST_LIS2DW12_ID_TAP,
	ST_LIS2DW12_ID_WU,
	ST_LIS2DW12_ID_MAX,
};

struct st_lis2dw12_sensor {
	enum st_lis2dw12_sensor_id id;
	struct st_lis2dw12_hw *hw;
	char name[32];

	u16 gain;
	u16 odr;
};

struct st_lis2dw12_hw {
	struct regmap *regmap;
	struct device *dev;
	int irq;
	int irq_emb;
	int irq_pin;
	u8 irq_reg;
	char name[32];

	struct mutex fifo_lock;
	struct mutex lock;

	struct iio_dev *iio_devs[ST_LIS2DW12_ID_MAX];

	enum st_lis2dw12_selftest_status st_status;
	u16 enable_mask;

	u8 watermark;
	u8 std_level;
	u64 samples;

	s64 delta_ts;
	s64 ts_irq;
	s64 ts;
};

static inline int
__st_lis2dw12_write_with_mask(struct st_lis2dw12_hw *hw,
			      unsigned int addr,  int mask,
			      unsigned int data)
{
	int err;
	unsigned int val = ST_LIS2DW12_SHIFT_VAL(data, mask);

	err = regmap_update_bits(hw->regmap, addr, mask, val);

	return err;
}

static inline int
st_lis2dw12_update_bits_locked(struct st_lis2dw12_hw *hw,
			       unsigned int addr, unsigned int mask,
			       unsigned int val)
{
	int err;

	mutex_lock(&hw->lock);
	err = __st_lis2dw12_write_with_mask(hw, addr, mask, val);
	mutex_unlock(&hw->lock);

	return err;
}

static inline int
st_lis2dw12_write_with_mask_locked(struct st_lis2dw12_hw *hw,
				   unsigned int addr, unsigned int mask,
				   unsigned int data)
{
	int err;

	mutex_lock(&hw->lock);
	err = __st_lis2dw12_write_with_mask(hw, addr, mask, data);
	mutex_unlock(&hw->lock);

	return err;
}

static inline int st_lis2dw12_write_locked(struct st_lis2dw12_hw *hw,
					   unsigned int addr, u8 *val,
					   unsigned int len)
{
	int err;

	mutex_lock(&hw->lock);
	err = regmap_bulk_write(hw->regmap, addr, val, len);
	mutex_unlock(&hw->lock);

	return err;
}


static inline int st_lis2dw12_read(struct st_lis2dw12_hw *hw, unsigned int addr,
				   void *val, unsigned int len)
{
	return regmap_bulk_read(hw->regmap, addr, val, len);
}

int st_lis2dw12_probe(struct device *dev, int irq, const char *name,
		      struct regmap *regmap);
int st_lis2dw12_fifo_setup(struct st_lis2dw12_hw *hw);
int st_lis2dw12_update_fifo_watermark(struct st_lis2dw12_hw *hw, u8 watermark);
ssize_t st_lis2dw12_flush_fifo(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t size);
ssize_t st_lis2dw12_set_hwfifo_watermark(struct device *device,
					 struct device_attribute *attr,
					 const char *buf, size_t size);
int st_lis2dw12_sensor_set_enable(struct st_lis2dw12_sensor *sensor,
				  bool enable);

#endif /* ST_LIS2DW12_H */
