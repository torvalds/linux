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

#include "rtrs-clt.h"

void rtrs_clt_update_wc_stats(struct rtrs_clt_con *con)
{
	struct rtrs_clt_path *clt_path = to_clt_path(con->c.path);
	struct rtrs_clt_stats *stats = clt_path->stats;
	struct rtrs_clt_stats_pcpu *s;
	int cpu;

	cpu = raw_smp_processor_id();
	s = get_cpu_ptr(stats->pcpu_stats);
	if (con->cpu != cpu) {
		s->cpu_migr.to++;

		/* Careful here, override s pointer */
		s = per_cpu_ptr(stats->pcpu_stats, con->cpu);
		atomic_inc(&s->cpu_migr.from);
	}
	put_cpu_ptr(stats->pcpu_stats);
}

void rtrs_clt_inc_failover_cnt(struct rtrs_clt_stats *stats)
{
	this_cpu_inc(stats->pcpu_stats->rdma.failover_cnt);
}

int rtrs_clt_stats_migration_from_cnt_to_str(struct rtrs_clt_stats *stats, char *buf)
{
	struct rtrs_clt_stats_pcpu *s;

	size_t used;
	int cpu;

	used = 0;
	for_each_possible_cpu(cpu) {
		s = per_cpu_ptr(stats->pcpu_stats, cpu);
		used += sysfs_emit_at(buf, used, "%d ",
				  atomic_read(&s->cpu_migr.from));
	}

	used += sysfs_emit_at(buf, used, "\n");

	return used;
}

int rtrs_clt_stats_migration_to_cnt_to_str(struct rtrs_clt_stats *stats, char *buf)
{
	struct rtrs_clt_stats_pcpu *s;

	size_t used;
	int cpu;

	used = 0;
	for_each_possible_cpu(cpu) {
		s = per_cpu_ptr(stats->pcpu_stats, cpu);
		used += sysfs_emit_at(buf, used, "%d ", s->cpu_migr.to);
	}

	used += sysfs_emit_at(buf, used, "\n");

	return used;
}

int rtrs_clt_stats_reconnects_to_str(struct rtrs_clt_stats *stats, char *buf)
{
	return sysfs_emit(buf, "%d %d\n", stats->reconnects.successful_cnt,
			  stats->reconnects.fail_cnt);
}

ssize_t rtrs_clt_stats_rdma_to_str(struct rtrs_clt_stats *stats, char *page)
{
	struct rtrs_clt_stats_rdma sum;
	struct rtrs_clt_stats_rdma *r;
	int cpu;

	memset(&sum, 0, sizeof(sum));

	for_each_possible_cpu(cpu) {
		r = &per_cpu_ptr(stats->pcpu_stats, cpu)->rdma;

		sum.dir[READ].cnt	  += r->dir[READ].cnt;
		sum.dir[READ].size_total  += r->dir[READ].size_total;
		sum.dir[WRITE].cnt	  += r->dir[WRITE].cnt;
		sum.dir[WRITE].size_total += r->dir[WRITE].size_total;
		sum.failover_cnt	  += r->failover_cnt;
	}

	return sysfs_emit(page, "%llu %llu %llu %llu %u %llu\n",
			 sum.dir[READ].cnt, sum.dir[READ].size_total,
			 sum.dir[WRITE].cnt, sum.dir[WRITE].size_total,
			 atomic_read(&stats->inflight), sum.failover_cnt);
}

ssize_t rtrs_clt_reset_all_help(struct rtrs_clt_stats *s, char *page)
{
	return sysfs_emit(page, "echo 1 to reset all statistics\n");
}

int rtrs_clt_reset_rdma_stats(struct rtrs_clt_stats *stats, bool enable)
{
	struct rtrs_clt_stats_pcpu *s;
	int cpu;

	if (!enable)
		return -EINVAL;

	for_each_possible_cpu(cpu) {
		s = per_cpu_ptr(stats->pcpu_stats, cpu);
		memset(&s->rdma, 0, sizeof(s->rdma));
	}

	return 0;
}

int rtrs_clt_reset_cpu_migr_stats(struct rtrs_clt_stats *stats, bool enable)
{
	struct rtrs_clt_stats_pcpu *s;
	int cpu;

	if (!enable)
		return -EINVAL;

	for_each_possible_cpu(cpu) {
		s = per_cpu_ptr(stats->pcpu_stats, cpu);
		memset(&s->cpu_migr, 0, sizeof(s->cpu_migr));
	}

	return 0;
}

int rtrs_clt_reset_reconnects_stat(struct rtrs_clt_stats *stats, bool enable)
{
	if (!enable)
		return -EINVAL;

	memset(&stats->reconnects, 0, sizeof(stats->reconnects));

	return 0;
}

int rtrs_clt_reset_all_stats(struct rtrs_clt_stats *s, bool enable)
{
	if (enable) {
		rtrs_clt_reset_rdma_stats(s, enable);
		rtrs_clt_reset_cpu_migr_stats(s, enable);
		rtrs_clt_reset_reconnects_stat(s, enable);
		atomic_set(&s->inflight, 0);
		return 0;
	}

	return -EINVAL;
}

static inline void rtrs_clt_update_rdma_stats(struct rtrs_clt_stats *stats,
					       size_t size, int d)
{
	this_cpu_inc(stats->pcpu_stats->rdma.dir[d].cnt);
	this_cpu_add(stats->pcpu_stats->rdma.dir[d].size_total, size);
}

void rtrs_clt_update_all_stats(struct rtrs_clt_io_req *req, int dir)
{
	struct rtrs_clt_con *con = req->con;
	struct rtrs_clt_path *clt_path = to_clt_path(con->c.path);
	struct rtrs_clt_stats *stats = clt_path->stats;
	unsigned int len;

	len = req->usr_len + req->data_len;
	rtrs_clt_update_rdma_stats(stats, len, dir);
	if (req->mp_policy == MP_POLICY_MIN_INFLIGHT)
		atomic_inc(&stats->inflight);
}

int rtrs_clt_init_stats(struct rtrs_clt_stats *stats)
{
	stats->pcpu_stats = alloc_percpu(typeof(*stats->pcpu_stats));
	if (!stats->pcpu_stats)
		return -ENOMEM;

	/*
	 * successful_cnt will be set to 0 after session
	 * is established for the first time
	 */
	stats->reconnects.successful_cnt = -1;

	return 0;
}
