// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * RDMA Transport Layer
 *
 * Copyright (c) 2014 - 2018 ProfitBricks GmbH. All rights reserved.
 * Copyright (c) 2018 - 2019 1&1 IONOS Cloud GmbH. All rights reserved.
 * Copyright (c) 2019 - 2020 1&1 IONOS SE. All rights reserved.
 */
#undef pr_fmt
#define pr_fmt(fmt) KBUILD_MODNAME " L" __stringify(__LINE__) ": " fmt

#include "rtrs-srv.h"

int rtrs_srv_reset_rdma_stats(struct rtrs_srv_stats *stats, bool enable)
{
	if (enable) {
		int cpu;
		struct rtrs_srv_stats_rdma_stats *r;

		for_each_possible_cpu(cpu) {
			r = per_cpu_ptr(stats->rdma_stats, cpu);
			memset(r, 0, sizeof(*r));
		}

		return 0;
	}

	return -EINVAL;
}

ssize_t rtrs_srv_stats_rdma_to_str(struct rtrs_srv_stats *stats, char *page)
{
	int cpu;
	struct rtrs_srv_stats_rdma_stats sum;
	struct rtrs_srv_stats_rdma_stats *r;

	memset(&sum, 0, sizeof(sum));

	for_each_possible_cpu(cpu) {
		r = per_cpu_ptr(stats->rdma_stats, cpu);

		sum.dir[READ].cnt	  += r->dir[READ].cnt;
		sum.dir[READ].size_total  += r->dir[READ].size_total;
		sum.dir[WRITE].cnt	  += r->dir[WRITE].cnt;
		sum.dir[WRITE].size_total += r->dir[WRITE].size_total;
	}

	return sysfs_emit(page, "%llu %llu %llu %llu\n",
			  sum.dir[READ].cnt, sum.dir[READ].size_total,
			  sum.dir[WRITE].cnt, sum.dir[WRITE].size_total);
}
