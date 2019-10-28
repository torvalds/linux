/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Tegra host1x Job
 *
 * Copyright (c) 2011-2013, NVIDIA Corporation.
 */

#ifndef __HOST1X_JOB_H
#define __HOST1X_JOB_H

#include <linux/dma-direction.h>

struct host1x_job_gather {
	unsigned int words;
	dma_addr_t base;
	struct host1x_bo *bo;
	unsigned int offset;
	bool handled;
};

struct host1x_job_unpin_data {
	struct host1x_bo *bo;
	struct sg_table *sgt;
	struct device *dev;
	size_t size;
	enum dma_data_direction dir;
};

/*
 * Dump contents of job to debug output.
 */
void host1x_job_dump(struct device *dev, struct host1x_job *job);

#endif
