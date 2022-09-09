/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SCD30_H
#define _SCD30_H

#include <linux/completion.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/pm.h>
#include <linux/regulator/consumer.h>
#include <linux/types.h>

struct scd30_state;

enum scd30_cmd {
	/* start continuous measurement with pressure compensation */
	CMD_START_MEAS,
	/* stop continuous measurement */
	CMD_STOP_MEAS,
	/* set/get measurement interval */
	CMD_MEAS_INTERVAL,
	/* check whether new measurement is ready */
	CMD_MEAS_READY,
	/* get measurement */
	CMD_READ_MEAS,
	/* turn on/off automatic self calibration */
	CMD_ASC,
	/* set/get forced recalibration value */
	CMD_FRC,
	/* set/get temperature offset */
	CMD_TEMP_OFFSET,
	/* get firmware version */
	CMD_FW_VERSION,
	/* reset sensor */
	CMD_RESET,
	/*
	 * Command for altitude compensation was omitted intentionally because
	 * the same can be achieved by means of CMD_START_MEAS which takes
	 * pressure above the sea level as an argument.
	 */
};

#define SCD30_MEAS_COUNT 3

typedef int (*scd30_command_t)(struct scd30_state *state, enum scd30_cmd cmd, u16 arg,
			       void *response, int size);

struct scd30_state {
	/* serialize access to the device */
	struct mutex lock;
	struct device *dev;
	struct regulator *vdd;
	struct completion meas_ready;
	/*
	 * priv pointer is solely for serdev driver private data. We keep it
	 * here because driver_data inside dev has been already used for iio and
	 * struct serdev_device doesn't have one.
	 */
	void *priv;
	int irq;
	/*
	 * no way to retrieve current ambient pressure compensation value from
	 * the sensor so keep one around
	 */
	u16 pressure_comp;
	u16 meas_interval;
	int meas[SCD30_MEAS_COUNT];

	scd30_command_t command;
};

extern const struct dev_pm_ops scd30_pm_ops;

int scd30_probe(struct device *dev, int irq, const char *name, void *priv, scd30_command_t command);

#endif
