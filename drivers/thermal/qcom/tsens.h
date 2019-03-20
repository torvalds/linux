/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 */

#ifndef __QCOM_TSENS_H__
#define __QCOM_TSENS_H__

#define ONE_PT_CALIB		0x1
#define ONE_PT_CALIB2		0x2
#define TWO_PT_CALIB		0x3

#include <linux/thermal.h>

struct tsens_priv;

/**
 * struct tsens_sensor - data for each sensor connected to the tsens device
 * @tmdev: tsens device instance that this sensor is connected to
 * @tzd: pointer to the thermal zone that this sensor is in
 * @offset: offset of temperature adjustment curve
 * @id: Sensor ID
 * @hw_id: HW ID can be used in case of platform-specific IDs
 * @slope: slope of temperature adjustment curve
 * @status: 8960-specific variable to track 8960 and 8660 status register offset
 */
struct tsens_sensor {
	struct tsens_priv		*tmdev;
	struct thermal_zone_device	*tzd;
	int				offset;
	int				id;
	int				hw_id;
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
	int (*init)(struct tsens_priv *);
	int (*calibrate)(struct tsens_priv *);
	int (*get_temp)(struct tsens_priv *, int, int *);
	/* optional callbacks */
	int (*enable)(struct tsens_priv *, int);
	void (*disable)(struct tsens_priv *);
	int (*suspend)(struct tsens_priv *);
	int (*resume)(struct tsens_priv *);
	int (*get_trend)(struct tsens_priv *, int, enum thermal_trend *);
};

enum reg_list {
	SROT_CTRL_OFFSET,

	REG_ARRAY_SIZE,
};

/**
 * struct tsens_plat_data - tsens compile-time platform data
 * @num_sensors: Number of sensors supported by platform
 * @ops: operations the tsens instance supports
 * @hw_ids: Subset of sensors ids supported by platform, if not the first n
 * @reg_offsets: Register offsets for commonly used registers
 */
struct tsens_plat_data {
	const u32		num_sensors;
	const struct tsens_ops	*ops;
	const u16		reg_offsets[REG_ARRAY_SIZE];
	unsigned int		*hw_ids;
};

/**
 * struct tsens_context - Registers to be saved/restored across a context loss
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
 * @reg_offsets: array of offsets to important regs for this version of IP
 * @ctx: registers to be saved and restored during suspend/resume
 * @ops: pointer to list of callbacks supported by this device
 * @sensor: list of sensors attached to this device
 */
struct tsens_priv {
	struct device			*dev;
	u32				num_sensors;
	struct regmap			*tm_map;
	struct regmap			*srot_map;
	u32				tm_offset;
	u16				reg_offsets[REG_ARRAY_SIZE];
	struct tsens_context		ctx;
	const struct tsens_ops		*ops;
	struct tsens_sensor		sensor[0];
};

char *qfprom_read(struct device *, const char *);
void compute_intercept_slope(struct tsens_priv *, u32 *, u32 *, u32);
int init_common(struct tsens_priv *);
int get_temp_common(struct tsens_priv *, int, int *);

/* TSENS v1 targets */
extern const struct tsens_plat_data data_8916, data_8974, data_8960;
/* TSENS v2 targets */
extern const struct tsens_plat_data data_8996, data_tsens_v2;

#endif /* __QCOM_TSENS_H__ */
