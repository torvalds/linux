/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 */

#ifndef __QCOM_TSENS_H__
#define __QCOM_TSENS_H__

#define ONE_PT_CALIB		0x1
#define ONE_PT_CALIB2		0x2
#define TWO_PT_CALIB		0x3
#define CAL_DEGC_PT1		30
#define CAL_DEGC_PT2		120
#define SLOPE_FACTOR		1000
#define SLOPE_DEFAULT		3200
#define THRESHOLD_MAX_ADC_CODE	0x3ff
#define THRESHOLD_MIN_ADC_CODE	0x0

#include <linux/interrupt.h>
#include <linux/thermal.h>
#include <linux/regmap.h>
#include <linux/slab.h>

struct tsens_priv;

/* IP version numbers in ascending order */
enum tsens_ver {
	VER_0_1 = 0,
	VER_1_X,
	VER_2_X,
};

enum tsens_irq_type {
	LOWER,
	UPPER,
	CRITICAL,
};

/**
 * struct tsens_sensor - data for each sensor connected to the tsens device
 * @priv: tsens device instance that this sensor is connected to
 * @tzd: pointer to the thermal zone that this sensor is in
 * @offset: offset of temperature adjustment curve
 * @hw_id: HW ID can be used in case of platform-specific IDs
 * @slope: slope of temperature adjustment curve
 * @status: 8960-specific variable to track 8960 and 8660 status register offset
 */
struct tsens_sensor {
	struct tsens_priv		*priv;
	struct thermal_zone_device	*tzd;
	int				offset;
	unsigned int			hw_id;
	int				slope;
	u32				status;
};

/**
 * struct tsens_ops - operations as supported by the tsens device
 * @init: Function to initialize the tsens device
 * @calibrate: Function to calibrate the tsens device
 * @get_temp: Function which returns the temp in millidegC
 * @enable: Function to enable (clocks/power) tsens device
 * @disable: Function to disable the tsens device
 * @suspend: Function to suspend the tsens device
 * @resume: Function to resume the tsens device
 * @get_trend: Function to get the thermal/temp trend
 */
struct tsens_ops {
	/* mandatory callbacks */
	int (*init)(struct tsens_priv *priv);
	int (*calibrate)(struct tsens_priv *priv);
	int (*get_temp)(const struct tsens_sensor *s, int *temp);
	/* optional callbacks */
	int (*enable)(struct tsens_priv *priv, int i);
	void (*disable)(struct tsens_priv *priv);
	int (*suspend)(struct tsens_priv *priv);
	int (*resume)(struct tsens_priv *priv);
	int (*get_trend)(struct tsens_sensor *s, enum thermal_trend *trend);
};

#define REG_FIELD_FOR_EACH_SENSOR11(_name, _offset, _startbit, _stopbit) \
	[_name##_##0]  = REG_FIELD(_offset,      _startbit, _stopbit),	\
	[_name##_##1]  = REG_FIELD(_offset +  4, _startbit, _stopbit), \
	[_name##_##2]  = REG_FIELD(_offset +  8, _startbit, _stopbit), \
	[_name##_##3]  = REG_FIELD(_offset + 12, _startbit, _stopbit), \
	[_name##_##4]  = REG_FIELD(_offset + 16, _startbit, _stopbit), \
	[_name##_##5]  = REG_FIELD(_offset + 20, _startbit, _stopbit), \
	[_name##_##6]  = REG_FIELD(_offset + 24, _startbit, _stopbit), \
	[_name##_##7]  = REG_FIELD(_offset + 28, _startbit, _stopbit), \
	[_name##_##8]  = REG_FIELD(_offset + 32, _startbit, _stopbit), \
	[_name##_##9]  = REG_FIELD(_offset + 36, _startbit, _stopbit), \
	[_name##_##10] = REG_FIELD(_offset + 40, _startbit, _stopbit)

#define REG_FIELD_FOR_EACH_SENSOR16(_name, _offset, _startbit, _stopbit) \
	[_name##_##0]  = REG_FIELD(_offset,      _startbit, _stopbit),	\
	[_name##_##1]  = REG_FIELD(_offset +  4, _startbit, _stopbit), \
	[_name##_##2]  = REG_FIELD(_offset +  8, _startbit, _stopbit), \
	[_name##_##3]  = REG_FIELD(_offset + 12, _startbit, _stopbit), \
	[_name##_##4]  = REG_FIELD(_offset + 16, _startbit, _stopbit), \
	[_name##_##5]  = REG_FIELD(_offset + 20, _startbit, _stopbit), \
	[_name##_##6]  = REG_FIELD(_offset + 24, _startbit, _stopbit), \
	[_name##_##7]  = REG_FIELD(_offset + 28, _startbit, _stopbit), \
	[_name##_##8]  = REG_FIELD(_offset + 32, _startbit, _stopbit), \
	[_name##_##9]  = REG_FIELD(_offset + 36, _startbit, _stopbit), \
	[_name##_##10] = REG_FIELD(_offset + 40, _startbit, _stopbit), \
	[_name##_##11] = REG_FIELD(_offset + 44, _startbit, _stopbit), \
	[_name##_##12] = REG_FIELD(_offset + 48, _startbit, _stopbit), \
	[_name##_##13] = REG_FIELD(_offset + 52, _startbit, _stopbit), \
	[_name##_##14] = REG_FIELD(_offset + 56, _startbit, _stopbit), \
	[_name##_##15] = REG_FIELD(_offset + 60, _startbit, _stopbit)

#define REG_FIELD_SPLIT_BITS_0_15(_name, _offset)		\
	[_name##_##0]  = REG_FIELD(_offset,  0,  0),		\
	[_name##_##1]  = REG_FIELD(_offset,  1,  1),	\
	[_name##_##2]  = REG_FIELD(_offset,  2,  2),	\
	[_name##_##3]  = REG_FIELD(_offset,  3,  3),	\
	[_name##_##4]  = REG_FIELD(_offset,  4,  4),	\
	[_name##_##5]  = REG_FIELD(_offset,  5,  5),	\
	[_name##_##6]  = REG_FIELD(_offset,  6,  6),	\
	[_name##_##7]  = REG_FIELD(_offset,  7,  7),	\
	[_name##_##8]  = REG_FIELD(_offset,  8,  8),	\
	[_name##_##9]  = REG_FIELD(_offset,  9,  9),	\
	[_name##_##10] = REG_FIELD(_offset, 10, 10),	\
	[_name##_##11] = REG_FIELD(_offset, 11, 11),	\
	[_name##_##12] = REG_FIELD(_offset, 12, 12),	\
	[_name##_##13] = REG_FIELD(_offset, 13, 13),	\
	[_name##_##14] = REG_FIELD(_offset, 14, 14),	\
	[_name##_##15] = REG_FIELD(_offset, 15, 15)

#define REG_FIELD_SPLIT_BITS_16_31(_name, _offset)		\
	[_name##_##0]  = REG_FIELD(_offset, 16, 16),		\
	[_name##_##1]  = REG_FIELD(_offset, 17, 17),	\
	[_name##_##2]  = REG_FIELD(_offset, 18, 18),	\
	[_name##_##3]  = REG_FIELD(_offset, 19, 19),	\
	[_name##_##4]  = REG_FIELD(_offset, 20, 20),	\
	[_name##_##5]  = REG_FIELD(_offset, 21, 21),	\
	[_name##_##6]  = REG_FIELD(_offset, 22, 22),	\
	[_name##_##7]  = REG_FIELD(_offset, 23, 23),	\
	[_name##_##8]  = REG_FIELD(_offset, 24, 24),	\
	[_name##_##9]  = REG_FIELD(_offset, 25, 25),	\
	[_name##_##10] = REG_FIELD(_offset, 26, 26),	\
	[_name##_##11] = REG_FIELD(_offset, 27, 27),	\
	[_name##_##12] = REG_FIELD(_offset, 28, 28),	\
	[_name##_##13] = REG_FIELD(_offset, 29, 29),	\
	[_name##_##14] = REG_FIELD(_offset, 30, 30),	\
	[_name##_##15] = REG_FIELD(_offset, 31, 31)

/*
 * reg_field IDs to use as an index into an array
 * If you change the order of the entries, check the devm_regmap_field_alloc()
 * calls in init_common()
 */
enum regfield_ids {
	/* ----- SROT ------ */
	/* HW_VER */
	VER_MAJOR,
	VER_MINOR,
	VER_STEP,
	/* CTRL_OFFSET */
	TSENS_EN,
	TSENS_SW_RST,
	SENSOR_EN,
	CODE_OR_TEMP,

	/* ----- TM ------ */
	/* TRDY */
	TRDY,
	/* INTERRUPT ENABLE */
	INT_EN,	/* v2+ has separate enables for crit, upper and lower irq */
	/* STATUS */
	LAST_TEMP_0,	/* Last temperature reading */
	LAST_TEMP_1,
	LAST_TEMP_2,
	LAST_TEMP_3,
	LAST_TEMP_4,
	LAST_TEMP_5,
	LAST_TEMP_6,
	LAST_TEMP_7,
	LAST_TEMP_8,
	LAST_TEMP_9,
	LAST_TEMP_10,
	LAST_TEMP_11,
	LAST_TEMP_12,
	LAST_TEMP_13,
	LAST_TEMP_14,
	LAST_TEMP_15,
	VALID_0,		/* VALID reading or not */
	VALID_1,
	VALID_2,
	VALID_3,
	VALID_4,
	VALID_5,
	VALID_6,
	VALID_7,
	VALID_8,
	VALID_9,
	VALID_10,
	VALID_11,
	VALID_12,
	VALID_13,
	VALID_14,
	VALID_15,
	LOWER_STATUS_0,	/* LOWER threshold violated */
	LOWER_STATUS_1,
	LOWER_STATUS_2,
	LOWER_STATUS_3,
	LOWER_STATUS_4,
	LOWER_STATUS_5,
	LOWER_STATUS_6,
	LOWER_STATUS_7,
	LOWER_STATUS_8,
	LOWER_STATUS_9,
	LOWER_STATUS_10,
	LOWER_STATUS_11,
	LOWER_STATUS_12,
	LOWER_STATUS_13,
	LOWER_STATUS_14,
	LOWER_STATUS_15,
	LOW_INT_STATUS_0,	/* LOWER interrupt status */
	LOW_INT_STATUS_1,
	LOW_INT_STATUS_2,
	LOW_INT_STATUS_3,
	LOW_INT_STATUS_4,
	LOW_INT_STATUS_5,
	LOW_INT_STATUS_6,
	LOW_INT_STATUS_7,
	LOW_INT_STATUS_8,
	LOW_INT_STATUS_9,
	LOW_INT_STATUS_10,
	LOW_INT_STATUS_11,
	LOW_INT_STATUS_12,
	LOW_INT_STATUS_13,
	LOW_INT_STATUS_14,
	LOW_INT_STATUS_15,
	LOW_INT_CLEAR_0,	/* LOWER interrupt clear */
	LOW_INT_CLEAR_1,
	LOW_INT_CLEAR_2,
	LOW_INT_CLEAR_3,
	LOW_INT_CLEAR_4,
	LOW_INT_CLEAR_5,
	LOW_INT_CLEAR_6,
	LOW_INT_CLEAR_7,
	LOW_INT_CLEAR_8,
	LOW_INT_CLEAR_9,
	LOW_INT_CLEAR_10,
	LOW_INT_CLEAR_11,
	LOW_INT_CLEAR_12,
	LOW_INT_CLEAR_13,
	LOW_INT_CLEAR_14,
	LOW_INT_CLEAR_15,
	LOW_INT_MASK_0,	/* LOWER interrupt mask */
	LOW_INT_MASK_1,
	LOW_INT_MASK_2,
	LOW_INT_MASK_3,
	LOW_INT_MASK_4,
	LOW_INT_MASK_5,
	LOW_INT_MASK_6,
	LOW_INT_MASK_7,
	LOW_INT_MASK_8,
	LOW_INT_MASK_9,
	LOW_INT_MASK_10,
	LOW_INT_MASK_11,
	LOW_INT_MASK_12,
	LOW_INT_MASK_13,
	LOW_INT_MASK_14,
	LOW_INT_MASK_15,
	LOW_THRESH_0,		/* LOWER threshold values */
	LOW_THRESH_1,
	LOW_THRESH_2,
	LOW_THRESH_3,
	LOW_THRESH_4,
	LOW_THRESH_5,
	LOW_THRESH_6,
	LOW_THRESH_7,
	LOW_THRESH_8,
	LOW_THRESH_9,
	LOW_THRESH_10,
	LOW_THRESH_11,
	LOW_THRESH_12,
	LOW_THRESH_13,
	LOW_THRESH_14,
	LOW_THRESH_15,
	UPPER_STATUS_0,	/* UPPER threshold violated */
	UPPER_STATUS_1,
	UPPER_STATUS_2,
	UPPER_STATUS_3,
	UPPER_STATUS_4,
	UPPER_STATUS_5,
	UPPER_STATUS_6,
	UPPER_STATUS_7,
	UPPER_STATUS_8,
	UPPER_STATUS_9,
	UPPER_STATUS_10,
	UPPER_STATUS_11,
	UPPER_STATUS_12,
	UPPER_STATUS_13,
	UPPER_STATUS_14,
	UPPER_STATUS_15,
	UP_INT_STATUS_0,	/* UPPER interrupt status */
	UP_INT_STATUS_1,
	UP_INT_STATUS_2,
	UP_INT_STATUS_3,
	UP_INT_STATUS_4,
	UP_INT_STATUS_5,
	UP_INT_STATUS_6,
	UP_INT_STATUS_7,
	UP_INT_STATUS_8,
	UP_INT_STATUS_9,
	UP_INT_STATUS_10,
	UP_INT_STATUS_11,
	UP_INT_STATUS_12,
	UP_INT_STATUS_13,
	UP_INT_STATUS_14,
	UP_INT_STATUS_15,
	UP_INT_CLEAR_0,	/* UPPER interrupt clear */
	UP_INT_CLEAR_1,
	UP_INT_CLEAR_2,
	UP_INT_CLEAR_3,
	UP_INT_CLEAR_4,
	UP_INT_CLEAR_5,
	UP_INT_CLEAR_6,
	UP_INT_CLEAR_7,
	UP_INT_CLEAR_8,
	UP_INT_CLEAR_9,
	UP_INT_CLEAR_10,
	UP_INT_CLEAR_11,
	UP_INT_CLEAR_12,
	UP_INT_CLEAR_13,
	UP_INT_CLEAR_14,
	UP_INT_CLEAR_15,
	UP_INT_MASK_0,		/* UPPER interrupt mask */
	UP_INT_MASK_1,
	UP_INT_MASK_2,
	UP_INT_MASK_3,
	UP_INT_MASK_4,
	UP_INT_MASK_5,
	UP_INT_MASK_6,
	UP_INT_MASK_7,
	UP_INT_MASK_8,
	UP_INT_MASK_9,
	UP_INT_MASK_10,
	UP_INT_MASK_11,
	UP_INT_MASK_12,
	UP_INT_MASK_13,
	UP_INT_MASK_14,
	UP_INT_MASK_15,
	UP_THRESH_0,		/* UPPER threshold values */
	UP_THRESH_1,
	UP_THRESH_2,
	UP_THRESH_3,
	UP_THRESH_4,
	UP_THRESH_5,
	UP_THRESH_6,
	UP_THRESH_7,
	UP_THRESH_8,
	UP_THRESH_9,
	UP_THRESH_10,
	UP_THRESH_11,
	UP_THRESH_12,
	UP_THRESH_13,
	UP_THRESH_14,
	UP_THRESH_15,
	CRITICAL_STATUS_0,	/* CRITICAL threshold violated */
	CRITICAL_STATUS_1,
	CRITICAL_STATUS_2,
	CRITICAL_STATUS_3,
	CRITICAL_STATUS_4,
	CRITICAL_STATUS_5,
	CRITICAL_STATUS_6,
	CRITICAL_STATUS_7,
	CRITICAL_STATUS_8,
	CRITICAL_STATUS_9,
	CRITICAL_STATUS_10,
	CRITICAL_STATUS_11,
	CRITICAL_STATUS_12,
	CRITICAL_STATUS_13,
	CRITICAL_STATUS_14,
	CRITICAL_STATUS_15,
	CRIT_INT_STATUS_0,	/* CRITICAL interrupt status */
	CRIT_INT_STATUS_1,
	CRIT_INT_STATUS_2,
	CRIT_INT_STATUS_3,
	CRIT_INT_STATUS_4,
	CRIT_INT_STATUS_5,
	CRIT_INT_STATUS_6,
	CRIT_INT_STATUS_7,
	CRIT_INT_STATUS_8,
	CRIT_INT_STATUS_9,
	CRIT_INT_STATUS_10,
	CRIT_INT_STATUS_11,
	CRIT_INT_STATUS_12,
	CRIT_INT_STATUS_13,
	CRIT_INT_STATUS_14,
	CRIT_INT_STATUS_15,
	CRIT_INT_CLEAR_0,	/* CRITICAL interrupt clear */
	CRIT_INT_CLEAR_1,
	CRIT_INT_CLEAR_2,
	CRIT_INT_CLEAR_3,
	CRIT_INT_CLEAR_4,
	CRIT_INT_CLEAR_5,
	CRIT_INT_CLEAR_6,
	CRIT_INT_CLEAR_7,
	CRIT_INT_CLEAR_8,
	CRIT_INT_CLEAR_9,
	CRIT_INT_CLEAR_10,
	CRIT_INT_CLEAR_11,
	CRIT_INT_CLEAR_12,
	CRIT_INT_CLEAR_13,
	CRIT_INT_CLEAR_14,
	CRIT_INT_CLEAR_15,
	CRIT_INT_MASK_0,	/* CRITICAL interrupt mask */
	CRIT_INT_MASK_1,
	CRIT_INT_MASK_2,
	CRIT_INT_MASK_3,
	CRIT_INT_MASK_4,
	CRIT_INT_MASK_5,
	CRIT_INT_MASK_6,
	CRIT_INT_MASK_7,
	CRIT_INT_MASK_8,
	CRIT_INT_MASK_9,
	CRIT_INT_MASK_10,
	CRIT_INT_MASK_11,
	CRIT_INT_MASK_12,
	CRIT_INT_MASK_13,
	CRIT_INT_MASK_14,
	CRIT_INT_MASK_15,
	CRIT_THRESH_0,		/* CRITICAL threshold values */
	CRIT_THRESH_1,
	CRIT_THRESH_2,
	CRIT_THRESH_3,
	CRIT_THRESH_4,
	CRIT_THRESH_5,
	CRIT_THRESH_6,
	CRIT_THRESH_7,
	CRIT_THRESH_8,
	CRIT_THRESH_9,
	CRIT_THRESH_10,
	CRIT_THRESH_11,
	CRIT_THRESH_12,
	CRIT_THRESH_13,
	CRIT_THRESH_14,
	CRIT_THRESH_15,

	/* WATCHDOG */
	WDOG_BARK_STATUS,
	WDOG_BARK_CLEAR,
	WDOG_BARK_MASK,
	WDOG_BARK_COUNT,

	/* CYCLE COMPLETION MONITOR */
	CC_MON_STATUS,
	CC_MON_CLEAR,
	CC_MON_MASK,

	MIN_STATUS_0,		/* MIN threshold violated */
	MIN_STATUS_1,
	MIN_STATUS_2,
	MIN_STATUS_3,
	MIN_STATUS_4,
	MIN_STATUS_5,
	MIN_STATUS_6,
	MIN_STATUS_7,
	MIN_STATUS_8,
	MIN_STATUS_9,
	MIN_STATUS_10,
	MIN_STATUS_11,
	MIN_STATUS_12,
	MIN_STATUS_13,
	MIN_STATUS_14,
	MIN_STATUS_15,
	MAX_STATUS_0,		/* MAX threshold violated */
	MAX_STATUS_1,
	MAX_STATUS_2,
	MAX_STATUS_3,
	MAX_STATUS_4,
	MAX_STATUS_5,
	MAX_STATUS_6,
	MAX_STATUS_7,
	MAX_STATUS_8,
	MAX_STATUS_9,
	MAX_STATUS_10,
	MAX_STATUS_11,
	MAX_STATUS_12,
	MAX_STATUS_13,
	MAX_STATUS_14,
	MAX_STATUS_15,

	/* Keep last */
	MAX_REGFIELDS
};

/**
 * struct tsens_features - Features supported by the IP
 * @ver_major: Major number of IP version
 * @crit_int: does the IP support critical interrupts?
 * @adc:      do the sensors only output adc code (instead of temperature)?
 * @srot_split: does the IP neatly splits the register space into SROT and TM,
 *              with SROT only being available to secure boot firmware?
 * @has_watchdog: does this IP support watchdog functionality?
 * @max_sensors: maximum sensors supported by this version of the IP
 */
struct tsens_features {
	unsigned int ver_major;
	unsigned int crit_int:1;
	unsigned int adc:1;
	unsigned int srot_split:1;
	unsigned int has_watchdog:1;
	unsigned int max_sensors;
};

/**
 * struct tsens_plat_data - tsens compile-time platform data
 * @num_sensors: Number of sensors supported by platform
 * @ops: operations the tsens instance supports
 * @hw_ids: Subset of sensors ids supported by platform, if not the first n
 * @feat: features of the IP
 * @fields: bitfield locations
 */
struct tsens_plat_data {
	const u32		num_sensors;
	const struct tsens_ops	*ops;
	unsigned int		*hw_ids;
	struct tsens_features	*feat;
	const struct reg_field		*fields;
};

/**
 * struct tsens_context - Registers to be saved/restored across a context loss
 * @threshold: Threshold register value
 * @control: Control register value
 */
struct tsens_context {
	int	threshold;
	int	control;
};

/**
 * struct tsens_priv - private data for each instance of the tsens IP
 * @dev: pointer to struct device
 * @num_sensors: number of sensors enabled on this device
 * @tm_map: pointer to TM register address space
 * @srot_map: pointer to SROT register address space
 * @tm_offset: deal with old device trees that don't address TM and SROT
 *             address space separately
 * @ul_lock: lock while processing upper/lower threshold interrupts
 * @crit_lock: lock while processing critical threshold interrupts
 * @rf: array of regmap_fields used to store value of the field
 * @ctx: registers to be saved and restored during suspend/resume
 * @feat: features of the IP
 * @fields: bitfield locations
 * @ops: pointer to list of callbacks supported by this device
 * @debug_root: pointer to debugfs dentry for all tsens
 * @debug: pointer to debugfs dentry for tsens controller
 * @sensor: list of sensors attached to this device
 */
struct tsens_priv {
	struct device			*dev;
	u32				num_sensors;
	struct regmap			*tm_map;
	struct regmap			*srot_map;
	u32				tm_offset;

	/* lock for upper/lower threshold interrupts */
	spinlock_t			ul_lock;

	struct regmap_field		*rf[MAX_REGFIELDS];
	struct tsens_context		ctx;
	struct tsens_features		*feat;
	const struct reg_field		*fields;
	const struct tsens_ops		*ops;

	struct dentry			*debug_root;
	struct dentry			*debug;

	struct tsens_sensor		sensor[];
};

char *qfprom_read(struct device *dev, const char *cname);
void compute_intercept_slope(struct tsens_priv *priv, u32 *pt1, u32 *pt2, u32 mode);
int init_common(struct tsens_priv *priv);
int get_temp_tsens_valid(const struct tsens_sensor *s, int *temp);
int get_temp_common(const struct tsens_sensor *s, int *temp);

/* TSENS target */
extern struct tsens_plat_data data_8960;

/* TSENS v0.1 targets */
extern struct tsens_plat_data data_8916, data_8974;

/* TSENS v1 targets */
extern struct tsens_plat_data data_tsens_v1, data_8976;

/* TSENS v2 targets */
extern struct tsens_plat_data data_8996, data_tsens_v2;

#endif /* __QCOM_TSENS_H__ */
