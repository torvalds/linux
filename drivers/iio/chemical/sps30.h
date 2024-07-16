/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SPS30_H
#define _SPS30_H

#include <linux/types.h>

struct sps30_state;
struct sps30_ops {
	int (*start_meas)(struct sps30_state *state);
	int (*stop_meas)(struct sps30_state *state);
	int (*read_meas)(struct sps30_state *state, __be32 *meas, size_t num);
	int (*reset)(struct sps30_state *state);
	int (*clean_fan)(struct sps30_state *state);
	int (*read_cleaning_period)(struct sps30_state *state, __be32 *period);
	int (*write_cleaning_period)(struct sps30_state *state, __be32 period);
	int (*show_info)(struct sps30_state *state);
};

struct sps30_state {
	/* serialize access to the device */
	struct mutex lock;
	struct device *dev;
	int state;
	/*
	 * priv pointer is solely for serdev driver private data. We keep it
	 * here because driver_data inside dev has been already used for iio and
	 * struct serdev_device doesn't have one.
	 */
	void *priv;
	const struct sps30_ops *ops;
};

int sps30_probe(struct device *dev, const char *name, void *priv, const struct sps30_ops *ops);

#endif
