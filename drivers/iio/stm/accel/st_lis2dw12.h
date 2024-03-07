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

#define ST_LIS2DW12_DEV_NAME			"lis2dw12"
#define ST_IIS2DLPC_DEV_NAME			"iis2dlpc"
#define ST_AIS2IH_DEV_NAME			"ais2ih"
#define ST_LIS2DW12_REGMAP			"lis2dw12_regmap"

#define ST_LIS2DW12_MAX_WATERMARK		31
#define ST_LIS2DW12_DATA_SIZE			6

#define ST_LIS2DW12_WHOAMI_ADDR			0x0f
#define ST_LIS2DW12_WHOAMI_VAL			0x44

#define ST_LIS2DW12_CTRL1_ADDR			0x20
#define ST_LIS2DW12_ODR_MASK			GENMASK(7, 4)
#define ST_LIS2DW12_MODE_MASK			GENMASK(3, 2)
#define ST_LIS2DW12_LP_MODE_MASK		GENMASK(1, 0)

#define ST_LIS2DW12_CTRL2_ADDR			0x21
#define ST_LIS2DW12_BDU_MASK			BIT(3)
#define ST_LIS2DW12_RESET_MASK			BIT(6)

#define ST_LIS2DW12_CTRL3_ADDR			0x22
#define ST_LIS2DW12_LIR_MASK			BIT(4)
#define ST_LIS2DW12_ST_MASK			GENMASK(7, 6)

#define ST_LIS2DW12_CTRL4_INT1_CTRL_ADDR	0x23
#define ST_LIS2DW12_DRDY_MASK			BIT(0)
#define ST_LIS2DW12_FTH_INT_MASK		BIT(1)
#define ST_LIS2DW12_TAP_INT1_MASK		BIT(6)
#define ST_LIS2DW12_TAP_TAP_INT1_MASK		BIT(3)
#define ST_LIS2DW12_FF_INT1_MASK		BIT(4)
#define ST_LIS2DW12_WU_INT1_MASK		BIT(5)

#define ST_LIS2DW12_CTRL5_INT2_CTRL_ADDR	0x24

#define ST_LIS2DW12_CTRL6_ADDR			0x25
#define ST_LIS2DW12_LN_MASK			BIT(2)
#define ST_LIS2DW12_FS_MASK			GENMASK(5, 4)
#define ST_LIS2DW12_BW_MASK			GENMASK(7, 6)

#define ST_LIS2DW12_STATUS_ADDR			0x27
#define ST_LIS2DW12_STATUS_FF_MASK		BIT(0)
#define ST_LIS2DW12_STATUS_TAP_TAP_MASK		BIT(4)
#define ST_LIS2DW12_STATUS_TAP_MASK		BIT(3)
#define ST_LIS2DW12_STATUS_WU_MASK		BIT(6)
#define ST_LIS2DW12_STATUS_FTH_MASK		BIT(7)

#define ST_LIS2DW12_OUT_X_L_ADDR		0x28
#define ST_LIS2DW12_OUT_Y_L_ADDR		0x2a
#define ST_LIS2DW12_OUT_Z_L_ADDR		0x2c

#define ST_LIS2DW12_FIFO_CTRL_ADDR		0x2e
#define ST_LIS2DW12_FIFOMODE_MASK		GENMASK(7, 5)
#define ST_LIS2DW12_FTH_MASK			GENMASK(4, 0)

#define ST_LIS2DW12_FIFO_SAMPLES_ADDR		0x2f
#define ST_LIS2DW12_FIFO_SAMPLES_FTH_MASK	BIT(7)
#define ST_LIS2DW12_FIFO_SAMPLES_OVR_MASK	BIT(6)

#define ST_LIS2DW12_TAP_THS_X_ADDR		0x30
#define ST_LIS2DW12_TAP_THS_Y_ADDR		0x31
#define ST_LIS2DW12_TAP_THS_Z_ADDR		0x32

#define ST_LIS2DW12_TAP_AXIS_MASK		GENMASK(7, 5)
#define ST_LIS2DW12_TAP_THS_MAK			GENMASK(4, 0)

#define ST_LIS2DW12_INT_DUR_ADDR		0x33

#define ST_LIS2DW12_WAKE_UP_THS_ADDR		0x34
#define ST_LIS2DW12_WAKE_UP_THS_MAK		GENMASK(5, 0)
#define ST_LIS2DW12_SINGLE_DOUBLE_TAP_MAK	BIT(7)

#define ST_LIS2DW12_FREE_FALL_ADDR		0x36
#define ST_LIS2DW12_FREE_FALL_THS_MASK		GENMASK(2, 0)
#define ST_LIS2DW12_FREE_FALL_DUR_MASK		GENMASK(7, 3)

#define ST_LIS2DW12_WU_SRC_ADDR			0x38
#define ST_LIS2DW12_TAP_SRC_ADDR		0x39
#define ST_LIS2DW12_STAP_SRC_MASK		BIT(5)
#define ST_LIS2DW12_DTAP_SRC_MASK		BIT(4)
#define ST_LIS2DW12_TAP_EVT_MASK		GENMASK(2, 0)
#define ST_LIS2DW12_FIFO_SAMPLES_DIFF_MASK	GENMASK(5, 0)

#define ST_LIS2DW12_ALL_INT_SRC_ADDR		0x3b
#define ST_LIS2DW12_ALL_INT_SRC_FF_MASK		BIT(0)
#define ST_LIS2DW12_ALL_INT_SRC_WU_MASK		BIT(1)
#define ST_LIS2DW12_ALL_INT_SRC_TAP_MASK	BIT(2)
#define ST_LIS2DW12_ALL_INT_SRC_TAP_TAP_MASK	BIT(3)

#define ST_LIS2DW12_ABS_INT_CFG_ADDR		0x3f
#define ST_LIS2DW12_ALL_INT_MASK		BIT(5)
#define ST_LIS2DW12_INT2_ON_INT1_MASK		BIT(6)
#define ST_LIS2DW12_DRDY_PULSED_MASK		BIT(7)

#define ST_LIS2DW12_FS_2G_GAIN			IIO_G_TO_M_S_2(244)
#define ST_LIS2DW12_FS_4G_GAIN			IIO_G_TO_M_S_2(488)
#define ST_LIS2DW12_FS_8G_GAIN			IIO_G_TO_M_S_2(976)
#define ST_LIS2DW12_FS_16G_GAIN			IIO_G_TO_M_S_2(1952)

#define ST_LIS2DW12_SELFTEST_MIN		285
#define ST_LIS2DW12_SELFTEST_MAX		6150

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
/* HW devices that can wakeup the target */
#define ST_LIS2DW12_WAKE_UP_SENSORS (BIT(ST_LIS2DW12_ID_ACC)      | \
				     BIT(ST_LIS2DW12_ID_TAP_TAP)  | \
				     BIT(ST_LIS2DW12_ID_TAP)      | \
				     BIT(ST_LIS2DW12_ID_WU))

/* this is the minimal ODR for wake-up sensors and dependencies */
#define ST_LIS2DW12_MIN_ODR_IN_WAKEUP	25

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

static inline bool st_lis2dw12_is_volatile_reg(struct device *dev,
					       unsigned int reg)
{
	switch (reg) {
	case ST_LIS2DW12_WHOAMI_ADDR:
	case ST_LIS2DW12_STATUS_ADDR:
	case ST_LIS2DW12_OUT_X_L_ADDR:
	case ST_LIS2DW12_OUT_X_L_ADDR + 1:
	case ST_LIS2DW12_OUT_Y_L_ADDR:
	case ST_LIS2DW12_OUT_Y_L_ADDR + 1:
	case ST_LIS2DW12_OUT_Z_L_ADDR:
	case ST_LIS2DW12_OUT_Z_L_ADDR + 1:
	case ST_LIS2DW12_FIFO_SAMPLES_ADDR:
	case ST_LIS2DW12_TAP_SRC_ADDR:
	case ST_LIS2DW12_ALL_INT_SRC_ADDR:
		return true;
	default:
		return false;
	}
}


static inline int
st_lis2dw12_set_fifo_mode(struct st_lis2dw12_hw *hw,
			  enum st_lis2dw12_fifo_mode mode)
{
	return st_lis2dw12_write_with_mask_locked(hw,
					     ST_LIS2DW12_FIFO_CTRL_ADDR,
					     ST_LIS2DW12_FIFOMODE_MASK,
					     mode);
}

static inline int st_lis2dw12_is_fifo_enabled(struct st_lis2dw12_hw *hw)
{
	return hw->enable_mask & (BIT(ST_LIS2DW12_ID_ACC));
}

extern const struct dev_pm_ops st_lis2dw12_pm_ops;

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
int st_lis2dw12_suspend_fifo(struct st_lis2dw12_hw *hw);

#endif /* ST_LIS2DW12_H */
