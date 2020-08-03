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
		struct rtrs_srv_stats_rdma_stats *r = &stats->rdma_stats;

		memset(r, 0, sizeof(*r));
		return 0;
	}

	return -EINVAL;
}

ssize_t rtrs_srv_stats_rdma_to_str(struct rtrs_srv_stats *stats,
				    char *page, size_t len)
{
	struct rtrs_srv_stats_rdma_stats *r = &stats->rdma_stats;
	struct rtrs_srv_sess *sess = stats->sess;

	return scnprintf(page, len, "%lld %lld %lld %lld %u\n",
			 (s64)atomic64_read(&r->dir[READ].cnt),
			 (s64)atomic64_read(&r->dir[READ].size_total),
			 (s64)atomic64_read(&r->dir[WRITE].cnt),
			 (s64)atomic64_read(&r->dir[WRITE].size_total),
			 atomic_read(&sess->ids_inflight));
}
