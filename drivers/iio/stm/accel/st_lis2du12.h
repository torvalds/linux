/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * STMicroelectronics lis2du12 driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2022 STMicroelectronics Inc.
 */

#ifndef ST_LIS2DU12_H
#define ST_LIS2DU12_H

#include <linux/device.h>
#include <linux/iio/events.h>
#include <linux/iio/iio.h>
#include <linux/regmap.h>
#include <linux/bitfield.h>

#include "../common/stm_iio_types.h"

#define ST_LIS2DU12_DEV_NAME			"lis2du12"
#define ST_LIS2DU12_MAX_WATERMARK		127
#define ST_LIS2DU12_ACC_DATA_SIZE		6
#define ST_LIS2DU12_TEMP_DATA_SIZE		2
#define ST_LIS2DU12_DATA_SIZE			(ST_LIS2DU12_ACC_DATA_SIZE + \
						 ST_LIS2DU12_TEMP_DATA_SIZE)
#define ST_LIS2DU12_ODR_EXPAND(odr, uodr)	((odr * 1000000) + uodr)

#define ST_LIS2DU12_IF_CTRL_ADDR		0x0e
#define ST_LIS2DU12_PD_DIS_INT1_MASK		BIT(2)

#define ST_LIS2DU12_CTRL1_ADDR			0x10
#define ST_LIS2DU12_WU_EN_MASK			GENMASK(2, 0)
#define ST_LIS2DU12_IF_ADD_INC_MASK		BIT(4)
#define ST_LIS2DU12_SW_RESET_MASK		BIT(5)
#define ST_LIS2DU12_PP_OD_MASK			BIT(7)

#define ST_LIS2DU12_CTRL2_ADDR			0x11
#define ST_LIS2DU12_INT_F_FTH_MASK		BIT(5)

#define ST_LIS2DU12_CTRL3_ADDR			0x12
#define ST_LIS2DU12_ST_MASK			GENMASK(1, 0)

#define ST_LIS2DU12_CTRL4_ADDR			0x13
#define ST_LIS2DU12_BOOT_MASK			BIT(0)
#define ST_LIS2DU12_BDU_MASK			BIT(5)

#define ST_LIS2DU12_CTRL5_ADDR			0x14
#define ST_LIS2DU12_ODR_MASK			GENMASK(7, 4)
#define ST_LIS2DU12_FS_MASK			GENMASK(1, 0)

#define ST_LIS2DU12_FIFO_CTRL_ADDR		0x15
#define ST_LIS2DU12_FIFOMODE_MASK		GENMASK(3, 0)
#define ST_LIS2DU12_ROUNDING_XYZ_MASK		BIT(7)

#define ST_LIS2DU12_FIFO_WTM_ADDR		0x16
#define ST_LIS2DU12_FTH_MASK			GENMASK(6, 0)

#define ST_LIS2DU12_INTERRUPT_CFG_ADDR		0x17
#define ST_LIS2DU12_INTERRUPTS_ENABLE_MASK	BIT(0)
#define ST_LIS2DU12_LIR_MASK			BIT(1)
#define ST_LIS2DU12_H_LACTIVE_MASK		BIT(2)
#define ST_LIS2DU12_SLEEP_STATUS_ON_INT_MASK	BIT(3)
#define ST_LIS2DU12_INT_SHORT_EN_MASK		BIT(6)

#define ST_LIS2DU12_TAP_THS_X_ADDR		0x18
#define ST_LIS2DU12_D4D_EN_MASK			BIT(7)
#define ST_LIS2DU12_D6D_THS_MASK		GENMASK(6, 5)
#define ST_LIS2DU12_TAP_THS_X_MASK		GENMASK(4, 0)

#define ST_LIS2DU12_TAP_THS_Y_ADDR		0x19
#define ST_LIS2DU12_TAP_PRIORITY_MASK		GENMASK(7, 5)
#define ST_LIS2DU12_TAP_THS_Y_MASK		GENMASK(4, 0)

#define ST_LIS2DU12_TAP_THS_Z_ADDR		0x1a
#define ST_LIS2DU12_TAP_EN_MASK			GENMASK(7, 5)
#define ST_LIS2DU12_TAP_THS_Z_MASK		GENMASK(4, 0)

#define ST_LIS2DU12_INT_DUR_ADDR		0x1b
#define ST_LIS2DU12_SHOCK_MASK			GENMASK(1, 0)
#define ST_LIS2DU12_QUIET_MASK			GENMASK(3, 2)
#define ST_LIS2DU12_LATENCY_MASK		GENMASK(7, 4)

#define ST_LIS2DU12_WAKE_UP_THS_ADDR		0x1c
#define ST_LIS2DU12_WK_THS_MASK			GENMASK(5, 0)
#define ST_LIS2DU12_SLEEP_ON_MASK		BIT(6)
#define ST_LIS2DU12_SINGLE_DOUBLE_TAP_MASK	BIT(7)

#define ST_LIS2DU12_WAKE_UP_DUR_ADDR		0x1d
#define ST_LIS2DU12_SLEEP_DUR_MASK		GENMASK(3, 0)
#define ST_LIS2DU12_WAKE_DUR_MASK		GENMASK(6, 5)
#define ST_LIS2DU12_FF_DUR5_MASK		BIT(7)

#define ST_LIS2DU12_FREE_FALL_ADDR		0x1e
#define ST_LIS2DU12_FF_THS_MASK			GENMASK(2, 0)
#define ST_LIS2DU12_FF_DUR_MASK			GENMASK(7, 3)

#define ST_LIS2DU12_MD1_CFG_ADDR		0x1f
#define ST_LIS2DU12_MD2_CFG_ADDR		0x20
#define ST_LIS2DU12_MD_INT_MASK			GENMASK(7, 2)
#define ST_LIS2DU12_INT_6D_MASK			BIT(2)
#define ST_LIS2DU12_INT_DOUBLE_TAP_MASK		BIT(3)
#define ST_LIS2DU12_INT_FF_MASK			BIT(4)
#define ST_LIS2DU12_INT_WU_MASK			BIT(5)
#define ST_LIS2DU12_INT_SINGLE_TAP_MASK		BIT(6)
#define ST_LIS2DU12_INT_SLEEP_CHANGE_MASK	BIT(7)

#define ST_LIS2DU12_WAKE_UP_SRC_ADDR		0x21
#define ST_LIS2DU12_WU_MASK			GENMASK(3, 0)
#define ST_LIS2DU12_WU_IA_MASK			BIT(3)
#define ST_LIS2DU12_SLEEP_STATE_MASK		BIT(4)
#define ST_LIS2DU12_FF_IA_MASK			BIT(5)
#define ST_LIS2DU12_SLEEP_CHANGE_IA_MASK	BIT(6)

#define ST_LIS2DU12_TAP_SRC_ADDR		0x22
#define ST_LIS2DU12_DOUBLE_TAP_IA_MASK		BIT(4)
#define ST_LIS2DU12_SINGLE_TAP_IA_MASK		BIT(5)

#define ST_LIS2DU12_SIXD_SRC_ADDR		0x23
#define ST_LIS2DU12_OVERTHRESHOLD_MASK		GENMASK(5, 0)
#define ST_LIS2DU12_D6D_IA_MASK			BIT(6)

#define ST_LIS2DU12_ALL_INT_SRC_ADDR		0x24
#define ST_LIS2DU12_FF_IA_ALL_MASK		BIT(0)
#define ST_LIS2DU12_WU_IA_ALL_MASK		BIT(1)
#define ST_LIS2DU12_SINGLE_TAP_ALL_MASK		BIT(2)
#define ST_LIS2DU12_DOUBLE_TAP_ALL_MASK		BIT(3)
#define ST_LIS2DU12_D6D_IA_ALL_MASK		BIT(4)
#define ST_LIS2DU12_SLEEP_CHANGE_IA_ALL_MASK	BIT(5)
#define ST_LIS2DU12_INT_GLOBAL_MASK		BIT(6)

#define ST_LIS2DU12_STATUS_ADDR			0x25
#define ST_LIS2DU12_DRDY_MASK			BIT(0)

#define ST_LIS2DU12_FIFO_STATUS1_ADDR		0x26
#define ST_LIS2DU12_FTH_WTM_MASK		BIT(7)

#define ST_LIS2DU12_FIFO_STATUS2_ADDR		0x27
#define ST_LIS2DU12_FSS_MASK			GENMASK(7, 0)

#define ST_LIS2DU12_OUT_X_L_ADDR		0x28
#define ST_LIS2DU12_OUT_Y_L_ADDR		0x2a
#define ST_LIS2DU12_OUT_Z_L_ADDR		0x2c

#define ST_LIS2DU12_TEMP_L_ADDR			0x30

#define ST_LIS2DU12_WHOAMI_ADDR			0x43
#define ST_LIS2DU12_WHOAMI_VAL			0x45

#define ST_LIS2DU12_ST_SIGN_ADDR		0x58
#define ST_LIS2DU12_STSIGN_MASK			GENMASK(7, 5)

#define ST_LIS2DU12_SHIFT_VAL(val, mask)	(((val) << __ffs(mask)) & (mask))

#define ST_LIS2DU12_ACC_CHAN(addr, ch2, idx)			 \
{								 \
	.type = IIO_ACCEL,					 \
	.address = addr,					 \
	.modified = 1,						 \
	.channel2 = ch2,					 \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |		 \
			      BIT(IIO_CHAN_INFO_SCALE),		 \
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ), \
	.scan_index = idx,					 \
	.scan_type = {						 \
		.sign = 's',					 \
		.realbits = 12,					 \
		.storagebits = 16,				 \
		.shift = 4,					 \
		.endianness = IIO_LE,				 \
	},							 \
}

#define ST_LIS2DU12_TEMP_CHAN(addr, ch2)			 \
{								 \
	.type = IIO_TEMP,					 \
	.address = addr,					 \
	.modified = 1,						 \
	.channel2 = ch2,					 \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |		 \
			      BIT(IIO_CHAN_INFO_OFFSET) |	 \
			      BIT(IIO_CHAN_INFO_SCALE),		 \
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ), \
	.scan_index = 0,					 \
	.scan_type = {						 \
		.sign = 's',					 \
		.realbits = 12,					 \
		.storagebits = 16,				 \
		.shift = 4,					 \
		.endianness = IIO_LE,				 \
	},							 \
}

#define ST_LIS2DU12_EVENT_CHANNEL(chan_type, evt_spec)		 \
{								 \
	.type = chan_type,					 \
	.modified = 0,						 \
	.scan_index = -1,					 \
	.indexed = -1,						 \
	.event_spec = evt_spec,					 \
	.num_event_specs = 1,					 \
}

enum st_lis2du12_fifo_mode {
	ST_LIS2DU12_FIFO_BYPASS = 0x0,
	ST_LIS2DU12_FIFO_CONTINUOUS = 0x6,
};

enum st_lis2du12_selftest_status {
	ST_LIS2DU12_ST_RESET,
	ST_LIS2DU12_ST_PASS,
	ST_LIS2DU12_ST_FAIL,
};

enum st_lis2du12_sensor_id {
	ST_LIS2DU12_ID_ACC,
	ST_LIS2DU12_ID_TEMP,
	ST_LIS2DU12_ID_TAP_TAP,
	ST_LIS2DU12_ID_TAP,
	ST_LIS2DU12_ID_WU,
	ST_LIS2DU12_ID_FF,
	ST_LIS2DU12_ID_6D,
	ST_LIS2DU12_ID_ACT,
	ST_LIS2DU12_ID_MAX,
};

enum st_lis2du12_attr_id {
	ST_LIS2DU12_WK_THS_ATTR_ID = 0x0,
	ST_LIS2DU12_WK_DUR_ATTR_ID,
	ST_LIS2DU12_FF_THS_ATTR_ID,
	ST_LIS2DU12_FF_DUR_ATTR_ID,
	ST_LIS2DU12_6D_THS_ATTR_ID,
	ST_LIS2DU12_LATENCY_ATTR_ID,
	ST_LIS2DU12_QUIET_ATTR_ID,
	ST_LIS2DU12_SHOCK_ATTR_ID,
	ST_LIS2DU12_TAP_PRIORITY_ATTR_ID,
	ST_LIS2DU12_TAP_THRESHOLD_X_ATTR_ID,
	ST_LIS2DU12_TAP_THRESHOLD_Y_ATTR_ID,
	ST_LIS2DU12_TAP_THRESHOLD_Z_ATTR_ID,
	ST_LIS2DU12_TAP_ENABLE_ATTR_ID,
	ST_LIS2DU12_SLEEP_DUR_ATTR_ID,
};

#define ST_LIS2DU12_MAX_BUFFER	ST_LIS2DU12_ID_TEMP

struct st_lis2du12_sensor {
	enum st_lis2du12_sensor_id id;
	struct st_lis2du12_hw *hw;

	u16 odr;
	u32 uodr;

	union {
		struct {
			u16 gain;
			u32 offset;
			u8 watermark;
		};

		struct {
			u8 wk_en;
			u8 d6d_ths;
			u8 tap_ths_x;
			u8 tap_ths_y;
			u8 tap_ths_z;
			u8 tap_priority;
			u8 tap_en;
			u8 latency;
			u8 quiet;
			u8 shock;
			u8 wh_ths;
			u8 wh_dur;
			u8 sleep_dur;
			u8 ff_dur;
			u8 ff_ths;
		};
	};
};

struct st_lis2du12_hw {
	struct device *dev;
	int irq;

	struct regmap *regmap;
	struct mutex fifo_lock;
	struct mutex lock;

	struct iio_dev *iio_devs[ST_LIS2DU12_ID_MAX];

	enum st_lis2du12_selftest_status st_status;
	enum st_lis2du12_fifo_mode fifo_mode;
	u16 enable_mask;

	u8 fifo_watermark;
	bool round_xl_xyz;
	u8 std_level;
	u64 samples;

	s64 delta_ts;
	s64 ts_irq;
	s64 ts;

	u8 drdy_reg;
	u8 md_reg;
	bool fourd_enabled;
};

extern const struct dev_pm_ops st_lis2du12_pm_ops;

static inline s64 st_lis2du12_get_timestamp(struct st_lis2du12_hw *hw)
{
	return iio_get_time_ns(hw->iio_devs[ST_LIS2DU12_ID_ACC]);
}

static inline bool
st_lis2du12_interrupts_enabled(struct st_lis2du12_hw *hw)
{
	return hw->enable_mask & (BIT(ST_LIS2DU12_ID_FF) |
				  BIT(ST_LIS2DU12_ID_TAP_TAP) |
				  BIT(ST_LIS2DU12_ID_TAP)  |
				  BIT(ST_LIS2DU12_ID_WU) |
				  BIT(ST_LIS2DU12_ID_6D) |
				  BIT(ST_LIS2DU12_ID_ACT));
}

static inline bool
st_lis2du12_fifo_enabled(struct st_lis2du12_hw *hw)
{
	return hw->enable_mask & (BIT(ST_LIS2DU12_ID_ACC) |
				  BIT(ST_LIS2DU12_ID_TEMP));
}

static inline int
st_lis2du12_read_locked(struct st_lis2du12_hw *hw, unsigned int addr,
			void *val, unsigned int len)
{
	int err;

	mutex_lock(&hw->lock);
	err = regmap_bulk_read(hw->regmap, addr, val, len);
	mutex_unlock(&hw->lock);

	return err;
}

static inline struct st_lis2du12_sensor *
st_lis2du12_get_sensor_from_id(struct st_lis2du12_hw *hw,
			       enum st_lis2du12_sensor_id id)
{
	return iio_priv(hw->iio_devs[id]);
}

int st_lis2du12_probe(struct device *dev, int irq,
		      struct regmap *regmap);
int st_lis2du12_buffer_setup(struct st_lis2du12_hw *hw);
ssize_t st_lis2du12_flush_fifo(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t size);
ssize_t st_lis2du12_set_hwfifo_watermark(struct device *device,
					 struct device_attribute *attr,
					 const char *buf, size_t size);
int st_lis2du12_sensor_set_enable(struct st_lis2du12_sensor *sensor,
				  bool enable);
#endif /* ST_LIS2DU12_H */
