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

struct tsens_device;

struct tsens_sensor {
	struct tsens_device		*tmdev;
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
	int (*init)(struct tsens_device *);
	int (*calibrate)(struct tsens_device *);
	int (*get_temp)(struct tsens_device *, int, int *);
	/* optional callbacks */
	int (*enable)(struct tsens_device *, int);
	void (*disable)(struct tsens_device *);
	int (*suspend)(struct tsens_device *);
	int (*resume)(struct tsens_device *);
	int (*get_trend)(struct tsens_device *, int, enum thermal_trend *);
};

/**
 * struct tsens_data - tsens instance specific data
 * @num_sensors: Max number of sensors supported by platform
 * @ops: operations the tsens instance supports
 * @hw_ids: Subset of sensors ids supported by platform, if not the first n
 */
struct tsens_data {
	const u32		num_sensors;
	const struct tsens_ops	*ops;
	unsigned int		*hw_ids;
};

/* Registers to be saved/restored across a context loss */
struct tsens_context {
	int	threshold;
	int	control;
};

struct tsens_device {
	struct device			*dev;
	u32				num_sensors;
	struct regmap			*map;
	u32				tm_offset;
	struct tsens_context		ctx;
	const struct tsens_ops		*ops;
	struct tsens_sensor		sensor[0];
};

char *qfprom_read(struct device *, const char *);
void compute_intercept_slope(struct tsens_device *, u32 *, u32 *, u32);
int init_common(struct tsens_device *);
int get_temp_common(struct tsens_device *, int, int *);

/* TSENS v1 targets */
extern const struct tsens_data data_8916, data_8974, data_8960;
/* TSENS v2 targets */
extern const struct tsens_data data_8996, data_tsens_v2;

#endif /* __QCOM_TSENS_H__ */
