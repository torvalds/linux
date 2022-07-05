/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * STMicroelectronics st_sths34pf80 sensor driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2022 STMicroelectronics Inc.
 */

#ifndef ST_STHS34PF80_H
#define ST_STHS34PF80_H

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/iio/iio.h>
#include <linux/regmap.h>

#define ST_STHS34PF80_TCOMP

#define ST_STHS34PF80_ODR_EXPAND(odr, uodr)	(((odr) * 1000000) + (uodr))

#define ST_STHS34PF80_DEV_NAME			"sths34pf80"

/* register map */
#define ST_STHS34PF80_LPF1_ADDR			0x0c
#define ST_STHS34PF80_LPF_P_M_MASK		GENMASK(5, 3)
#define ST_STHS34PF80_LPF_M_MASK		GENMASK(2, 0)

#define ST_STHS34PF80_LPF2_ADDR			0x0d
#define ST_STHS34PF80_LPF_P_MASK		GENMASK(5, 3)
#define ST_STHS34PF80_LPF_A_T_MASK		GENMASK(2, 0)

#define ST_STHS34PF80_WHOAMI_ADDR		0x0f
#define ST_STHS34PF80_WHOAMI_VAL		0xd3

#define ST_STHS34PF80_AVG_TRIM_ADDR		0x10
#define ST_STHS34PF80_AVG_T_MASK		GENMASK(5, 4)
#define ST_STHS34PF80_AVG_TMOS_MASK		GENMASK(2, 0)

#define ST_STHS34PF80_SENSITIVITY_DATA_ADDR	0x1d

#define ST_STHS34PF80_CTRL1_ADDR		0x20
#define ST_STHS34PF80_BDU_MASK			BIT(4)
#define ST_STHS34PF80_ODR_MASK			GENMASK(3, 0)

#define ST_STHS34PF80_CTRL2_ADDR		0x21
#define ST_STHS34PF80_BOOT_MASK			BIT(7)
#define ST_STHS34PF80_FUNC_CFG_ACCESS_MASK	BIT(4)
#define ST_STHS34PF80_ONE_SHOT_MASK		BIT(0)

#define ST_STHS34PF80_CTRL3_ADDR		0x22
#define ST_STHS34PF80_INT_H_L_MASK		BIT(7)
#define ST_STHS34PF80_PP_OD_MASK		BIT(6)
#define ST_STHS34PF80_INT_MSK_MASK		GENMASK(5, 3)
#define ST_STHS34PF80_INT_MSK2_MASK		BIT(5)
#define ST_STHS34PF80_INT_MSK1_MASK		BIT(4)
#define ST_STHS34PF80_INT_MSK0_MASK		BIT(3)
#define ST_STHS34PF80_INT_LATCHED_MASK		BIT(2)
#define ST_STHS34PF80_IEN_MASK			GENMASK(1, 0)

#define ST_STHS34PF80_IEN_DRDY_VAL		0x01
#define ST_STHS34PF80_IEN_INT_OR_VAL		0x02

#define ST_STHS34PF80_STATUS_ADDR		0x23
#define ST_STHS34PF80_DRDY_MASK			BIT(2)

#define ST_STHS34PF80_FUNC_STATUS_ADDR		0x25
#define ST_STHS34PF80_PRES_FLAG_MASK		BIT(2)
#define ST_STHS34PF80_MOT_FLAG_MASK		BIT(1)
#define ST_STHS34PF80_TAMB_SHOCK_FLAG_MASK	BIT(0)

#define ST_STHS34PF80_TOBJECT_L_ADDR		0x26
#define ST_STHS34PF80_TAMBIENT_L_ADDR		0x28

#ifdef ST_STHS34PF80_TCOMP
#define ST_STHS34PF80_TOBJECT_COMP_L_ADDR	0x38
#endif /* ST_STHS34PF80_TCOMP */

#define ST_STHS34PF80_TPRESENCE_L_ADDR		0x3a
#define ST_STHS34PF80_TMOTION_L_ADDR		0x3c
#define ST_STHS34PF80_TAMB_SHOCK_L_ADDR		0x3e

/* embedded functions register map */
#define ST_STHS34PF80_FUNC_CFG_ADDR_ADDR	0x08
#define ST_STHS34PF80_FUNC_CFG_DATA_ADDR	0x09

#define ST_STHS34PF80_PAGE_RW_ADDR		0x11
#define ST_STHS34PF80_FUNC_CFG_WRITE_MASK	BIT(6)
#define ST_STHS34PF80_FUNC_CFG_READ_MASK	BIT(5)

#define ST_STHS34PF80_PRESENCE_THS_ADDR		0x20
#define ST_STHS34PF80_MOTION_THS_ADDR		0x22
#define ST_STHS34PF80_TAMB_SHOCK_THS_ADDR	0x24
#define ST_STHS34PF80_HYST_MOTION_ADDR		0x26
#define ST_STHS34PF80_HYST_PRESENCE_ADDR	0x27

#define ST_STHS34PF80_ALGO_CONFIG_ADDR		0x28
#define ST_STHS34PF80_INT_PULSED_MASK		BIT(3)
#define ST_STHS34PF80_COMP_TYPE_MASK		BIT(2)
#define ST_STHS34PF80_SEL_ABS_MASK		BIT(1)

#define ST_STHS34PF80_HYST_TAMB_SHOCK_ADDR	0x29

#define ST_STHS34PF80_RESET_ALGO_ADDR		0x2a
#define ST_STHS34PF80_ALGO_ENABLE_RESET_MASK	BIT(0)

/* default values */
#define ST_STHS34PF80_TOBJECT_GAIN		2000

#ifdef ST_STHS34PF80_TCOMP
#define ST_STHS34PF80_TOBJECT_COMP_GAIN		2000
#endif /* ST_STHS34PF80_TCOMP */

#define ST_STHS34PF80_TAMBIENT_GAIN		100

#define ST_STHS34PF80_LPF_M_DEFAULT		0x04
#define ST_STHS34PF80_LPF_P_M_DEFAULT		0x00
#define ST_STHS34PF80_LPF_P_DEFAULT		0x04
#define ST_STHS34PF80_LPF_A_T_DEFAULT		0x02

#define ST_STHS34PF80_PRESENCE_THS_DEFAULT	0x00c8
#define ST_STHS34PF80_MOTION_THS_DEFAULT	0x00c8
#define ST_STHS34PF80_TAMB_SHOCK_THS_DEFAULT	0x000a

#define ST_STHS34PF80_HYST_MOTION_DEFAULT	0x32
#define ST_STHS34PF80_HYST_PRESENCE_DEFAULT	0x32

#define ST_STHS34PF80_DATA_CHANNEL(chan_type, addr, mod,	\
				   ch2, scan_idx, rb, sb, sg)	\
{								\
	.type = chan_type,					\
	.address = addr,					\
	.modified = mod,					\
	.channel2 = ch2,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |		\
			      BIT(IIO_CHAN_INFO_SCALE),		\
	.info_mask_shared_by_all =				\
				BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
	.scan_index = scan_idx,					\
	.scan_type = {						\
		.sign = sg,					\
		.realbits = rb,					\
		.storagebits = sb,				\
		.endianness = IIO_LE,				\
	},							\
}

static const struct iio_event_spec st_sths34pf80_thr_event = {
	.type = IIO_EV_TYPE_THRESH,
	.dir = IIO_EV_DIR_RISING,
	.mask_separate = BIT(IIO_EV_INFO_ENABLE),
};

#define ST_STHS34PF80_EVENT_CHANNEL(ctype)			\
{								\
	.type = ctype,						\
	.modified = 0,						\
	.scan_index = -1,					\
	.indexed = -1,						\
	.event_spec = &st_sths34pf80_thr_event,			\
	.num_event_specs = 1,					\
}

#define ST_STHS34PF80_SHIFT_VAL(val, mask)	(((val) << __ffs(mask)) & (mask))

/**
 * struct st_sths34pf80_reg - Generic sensor register
 * description (addr + mask)
 *
 * @addr: Address of register.
 * @mask: Bitmask register for proper usage.
 */
struct st_sths34pf80_reg {
	u8 addr;
	u8 mask;
};

/**
 * struct st_sths34pf80_odr - Single ODR entry
 * @hz: Most significant part of the sensor ODR (Hz).
 * @uhz: Less significant part of the sensor ODR (micro Hz).
 * @val: ODR register value.
 * @avg: Average suggested for this ODR.
 */
struct st_sths34pf80_odr {
	u16 hz;
	u32 uhz;
	u8 val;
	u8 avg;
};

/**
 * struct st_sths34pf80_fs
 * brief Full scale entry
 *
 * @gain: The gain to obtain data value from raw data (LSB).
 * @val: Register value.
 */
struct st_sths34pf80_fs {
	u32 gain;
	u8 val;
};

/**
 * struct st_sths34pf80_lpf
 * brief Low pass filter setting
 *
 * @reg: Register to update LPF bandwidth.
 * @mask: Register bitmask.
 * @val: Register value.
 */
struct st_sths34pf80_lpf {
	u8 reg;
	u8 mask;
	u8 val;
};

/**
 * struct st_sths34pf80_threshold
 * brief Thresholds setting
 *
 * @reg: Register to update sensor threshold.
 * @val: Register value.
 */
struct st_sths34pf80_threshold {
	u8 reg;
	u16 val;
};

/**
 * struct st_sths34pf80_hysteresis
 * brief hysteresis setting
 *
 * @reg: Register to update sensor hysteresis.
 * @val: Register value.
 */
struct st_sths34pf80_hysteresis {
	u8 reg;
	u8 val;
};

enum st_sths34pf80_sensor_id {
	ST_STHS34PF80_ID_TAMB_OBJ = 0,
#ifdef ST_STHS34PF80_TCOMP
	ST_STHS34PF80_ID_TOBJECT_COMP,
#endif /* ST_STHS34PF80_TCOMP */
	ST_STHS34PF80_ID_TAMB_SHOCK,
	ST_STHS34PF80_ID_TMOTION,
	ST_STHS34PF80_ID_TPRESENCE,
	ST_STHS34PF80_ID_MAX,
};

/**
 * struct st_sths34pf80_sensor - ST TMOS sensor instance
 * @name: Sensor name.
 * @id: Sensor identifier.
 * @hw: Pointer to instance of struct st_sths34pf80_hw.
 * @odr: Output data rate of the sensor [Hz].
 * @uodr: Output data rate of the sensor [uHz].
 * @lpf: Sensor low pass filter settings.
 * @threshold: Sensor thresholds.
 * @hysteresis: Sensor hysteresis.
 */
struct st_sths34pf80_sensor {
	char name[32];

	enum st_sths34pf80_sensor_id id;
	struct st_sths34pf80_hw *hw;

	int odr;
	int uodr;

	struct st_sths34pf80_lpf lpf;
	struct st_sths34pf80_threshold threshold;
	struct st_sths34pf80_hysteresis hysteresis;
};

/**
 * struct st_sths34pf80_hw - ST TMOS MEMS hw instance
 * @dev_name: STM device name.
 * @dev: Pointer to instance of struct device (I2C or SPI).
 * @irq: Device interrupt line (I2C or SPI).
 * @regmap: Register map of the device.
 * @page_lock: Mutex to prevent concurrent access to the page selector.
 * @int_lock: Mutex to prevent concurrent access to interrupt configuration.
 * @state: hw operational state.
 * @enable_mask: Enabled sensor bitmask.
 * @event_mask: Enabled event generation bitmask.
 * @vdd_supply: Voltage regulator for VDD.
 * @vddio_supply: Voltage regulator for VDDIIO.
 * @iio_devs: Pointers to iio_dev instances.
 * @ts: Event hardware timestamp.
 * @tcomp: Temperature compensation enabled flag.
 * @edge_trigger: Interrupt configuration type.
 * @sensitivity: Data sensitivity (for tobj compensated only)
 */
struct st_sths34pf80_hw {
	char dev_name[16];
	struct device *dev;
	int irq;
	struct regmap *regmap;
	struct mutex page_lock;
	struct mutex int_lock;

	unsigned long state;
	u64 enable_mask;
	u64 event_mask;

	struct regulator *vdd_supply;
	struct regulator *vddio_supply;

	struct iio_dev *iio_devs[ST_STHS34PF80_ID_MAX];

	s64 ts;

#ifdef ST_STHS34PF80_TCOMP
	bool tcomp;
#endif /* ST_STHS34PF80_TCOMP */

	bool edge_trigger;

	u8 sensitivity;
};

static inline int
__st_sths34pf80_write_with_mask(struct st_sths34pf80_hw *hw,
				unsigned int addr,
				unsigned int mask,
				unsigned int data)
{
	int err;
	unsigned int val = ST_STHS34PF80_SHIFT_VAL(data, mask);

	err = regmap_update_bits(hw->regmap, addr, mask, val);

	return err;
}

static inline int
st_sths34pf80_update_bits_locked(struct st_sths34pf80_hw *hw,
				 unsigned int addr, unsigned int mask,
				 unsigned int val)
{
	int err;

	mutex_lock(&hw->page_lock);
	err = __st_sths34pf80_write_with_mask(hw, addr, mask, val);
	mutex_unlock(&hw->page_lock);

	return err;
}

static inline int
st_sths34pf80_read_locked(struct st_sths34pf80_hw *hw,
			  unsigned int addr, void *val,
			  unsigned int len)
{
	int err;

	mutex_lock(&hw->page_lock);
	err = regmap_bulk_read(hw->regmap, addr, val, len);
	mutex_unlock(&hw->page_lock);

	return err;
}

static inline int
st_sths34pf80_write_locked(struct st_sths34pf80_hw *hw,
			   unsigned int addr, unsigned int val)
{
	int err;

	mutex_lock(&hw->page_lock);
	err = regmap_write(hw->regmap, addr, val);
	mutex_unlock(&hw->page_lock);

	return err;
}

static inline int
st_sths34pf80_set_page_access(struct st_sths34pf80_hw *hw,
			      unsigned int val)
{
	return regmap_update_bits(hw->regmap,
		   ST_STHS34PF80_CTRL2_ADDR,
		   ST_STHS34PF80_FUNC_CFG_ACCESS_MASK,
		   FIELD_PREP(ST_STHS34PF80_FUNC_CFG_ACCESS_MASK, val));
}

static inline s64 st_sths34pf80_get_time_ns(struct st_sths34pf80_hw *hw)
{
	return iio_get_time_ns(hw->iio_devs[ST_STHS34PF80_ID_TAMB_OBJ]);
}

int st_sths34pf80_probe(struct device *dev, int irq,
			struct regmap *regmap);
int st_sths34pf80_remove(struct device *dev);

#ifdef CONFIG_PM_SLEEP
extern const struct dev_pm_ops st_sths34pf80_pm_ops;
#endif /* CONFIG_PM_SLEEP */
#endif /* ST_STHS34PF80_H */
