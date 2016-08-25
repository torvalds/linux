/*
 * Copyright(C) 2015 Linaro Limited. All rights reserved.
 * Author: Mathieu Poirier <mathieu.poirier@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _CORESIGHT_ETM_PERF_H
#define _CORESIGHT_ETM_PERF_H

#include "coresight-priv.h"

struct coresight_device;

/*
 * In both ETMv3 and v4 the maximum number of address comparator implentable
 * is 8.  The actual number is implementation specific and will be checked
 * when filters are applied.
 */
#define ETM_ADDR_CMP_MAX	8

/**
 * struct etm_filter - single instruction range or start/stop configuration.
 * @start_addr:	The address to start tracing on.
 * @stop_addr:	The address to stop tracing on.
 * @type:	Is this a range or start/stop filter.
 */
struct etm_filter {
	unsigned long start_addr;
	unsigned long stop_addr;
	enum etm_addr_type type;
};

/**
 * struct etm_filters - set of filters for a session
 * @etm_filter:	All the filters for this session.
 * @nr_filters:	Number of filters
 * @ssstatus:	Status of the start/stop logic.
 */
struct etm_filters {
	struct etm_filter	etm_filter[ETM_ADDR_CMP_MAX];
	unsigned int		nr_filters;
	bool			ssstatus;
};


#ifdef CONFIG_CORESIGHT
int etm_perf_symlink(struct coresight_device *csdev, bool link);

#else
static inline int etm_perf_symlink(struct coresight_device *csdev, bool link)
{ return -EINVAL; }

#endif /* CONFIG_CORESIGHT */

#endif
