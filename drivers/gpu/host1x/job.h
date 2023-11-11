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

struct host1x_job_wait {
	u32 id;
	u32 threshold;
	u32 next_class;
	bool relative;
};

struct host1x_job_cmd {
	bool is_wait;

	union {
		struct host1x_job_gather gather;
		struct host1x_job_wait wait;
	};
};

struct host1x_job_unpin_data {
	struct host1x_bo_mapping *map;
};

/*
 * Dump contents of job to debug output.
 */
void host1x_job_dump(struct device *dev, struct host1x_job *job);

#endif
