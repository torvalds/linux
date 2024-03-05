/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * STMicroelectronics ilps22qs driver
 *
 * Copyright 2023 STMicroelectronics Inc.
 *
 * MEMS Software Solutions Team
 */

#ifndef __ST_ILPS22QS_H
#define __ST_ILPS22QS_H

#include <linux/bitfield.h>
#include <linux/iio/iio.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#define ST_ILPS22QS_DEV_NAME			"ilps22qs"
#define ST_ILPS28QWS_DEV_NAME			"ilps28qws"

#define ST_ILPS22QS_WHO_AM_I_ADDR		0x0f
#define ST_ILPS22QS_WHOAMI_VAL			0xb4

#define ST_ILPS22QS_CTRL1_ADDR			0x10
#define ST_ILPS22QS_ODR_MASK			GENMASK(6, 3)

#define ST_ILPS22QS_CTRL2_ADDR			0x11
#define ST_ILPS22QS_SOFT_RESET_MASK		BIT(2)
#define ST_ILPS22QS_BDU_MASK			BIT(3)

#define ST_ILPS22QS_CTRL3_ADDR			0x12
#define ST_ILPS22QS_AH_QVAR_EN_MASK		BIT(7)
#define ST_ILPS22QS_AH_QVAR_P_AUTO_EN_MASK	BIT(5)

#define ST_ILPS22QS_PRESS_OUT_XL_ADDR		0x28
#define ST_ILPS22QS_TEMP_OUT_L_ADDR		0x2b

#define ST_ILPS22QS_PRESS_FS_AVL_GAIN		(1000000000UL / 4096UL)
#define ST_ILPS22QS_TEMP_FS_AVL_GAIN		100
#define ST_ILPS22QS_QVAR_FS_AVL_GAIN		438000

#define ST_ILPS22QS_SHIFT_VAL(val, mask)	(((val) << __ffs(mask)) & (mask))

#define ST_ILPS22QS_ODR_LIST_NUM		8

enum st_ilps22qs_sensor_id {
	ST_ILPS22QS_PRESS = 0,
	ST_ILPS22QS_TEMP,
	ST_ILPS22QS_QVAR,
	ST_ILPS22QS_SENSORS_NUM,
};

struct st_ilps22qs_odr_t {
	u8 hz;
	u8 val;
};

struct st_ilps22qs_reg {
	u8 addr;
	u8 mask;
};

struct st_ilps22qs_odr_table_t {
	u8 size;
	struct st_ilps22qs_reg reg;
	struct st_ilps22qs_odr_t odr_avl[ST_ILPS22QS_ODR_LIST_NUM];
};

struct st_ilps22qs_hw {
	struct iio_dev *iio_devs[ST_ILPS22QS_SENSORS_NUM];
	struct workqueue_struct *workqueue;
	struct regulator *vddio_supply;
	struct regulator *vdd_supply;
	struct regmap *regmap;
	struct device *dev;
	struct mutex lock;
	bool interleave;
	u8 enable_mask;
	u8 odr;
};

struct st_ilps22qs_sensor {
	enum st_ilps22qs_sensor_id id;
	struct work_struct iio_work;
	struct st_ilps22qs_hw *hw;
	struct hrtimer hr_timer;
	ktime_t ktime;
	int64_t timestamp;
	char name[32];
	u32 gain;
	u8 odr;
};

extern const struct dev_pm_ops st_ilps22qs_pm_ops;

static inline int st_ilps22qs_update_locked(struct st_ilps22qs_hw *hw,
					    unsigned int addr,
					    unsigned int mask,
					    unsigned int data)
{
	unsigned int val = ST_ILPS22QS_SHIFT_VAL(data, mask);
	int err;

	mutex_lock(&hw->lock);
	err = regmap_update_bits(hw->regmap, addr, mask, val);
	mutex_unlock(&hw->lock);

	return err;
}

static inline int st_ilps22qs_read_locked(struct st_ilps22qs_hw *hw,
					  unsigned int addr, void *val,
					  unsigned int len)
{
	int err;

	mutex_lock(&hw->lock);
	err = regmap_bulk_read(hw->regmap, addr, val, len);
	mutex_unlock(&hw->lock);

	return err;
}

static inline void st_ilps22qs_flush_works(struct st_ilps22qs_hw *hw)
{
	flush_workqueue(hw->workqueue);
}

static inline int st_ilps22qs_destroy_workqueue(struct st_ilps22qs_hw *hw)
{
	if (hw->workqueue)
		destroy_workqueue(hw->workqueue);

	return 0;
}

static inline int st_ilps22qs_allocate_workqueue(struct st_ilps22qs_hw *hw)
{
	if (!hw->workqueue)
		hw->workqueue = create_workqueue(ST_ILPS22QS_DEV_NAME);

	return !hw->workqueue ? -ENOMEM : 0;
}

static inline s64 st_ilps22qs_get_time_ns(struct st_ilps22qs_hw *hw)
{
	return iio_get_time_ns(hw->iio_devs[ST_ILPS22QS_PRESS]);
}

int st_ilps22qs_probe(struct device *dev, struct regmap *regmap);
int st_ilps22qs_remove(struct device *dev);

#endif /* __ST_ILPS22QS_H */
