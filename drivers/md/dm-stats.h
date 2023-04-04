/* SPDX-License-Identifier: GPL-2.0 */
#ifndef DM_STATS_H
#define DM_STATS_H

#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/list.h>

int dm_statistics_init(void);
void dm_statistics_exit(void);

struct dm_stats {
	struct mutex mutex;
	struct list_head list;	/* list of struct dm_stat */
	struct dm_stats_last_position __percpu *last;
	bool precise_timestamps;
};

struct dm_stats_aux {
	bool merged;
	unsigned long long duration_ns;
};

int dm_stats_init(struct dm_stats *st);
void dm_stats_cleanup(struct dm_stats *st);

struct mapped_device;

int dm_stats_message(struct mapped_device *md, unsigned argc, char **argv,
		     char *result, unsigned maxlen);

void dm_stats_account_io(struct dm_stats *stats, unsigned long bi_rw,
			 sector_t bi_sector, unsigned bi_sectors, bool end,
			 unsigned long start_time,
			 struct dm_stats_aux *aux);

static inline bool dm_stats_used(struct dm_stats *st)
{
	return !list_empty(&st->list);
}

static inline void dm_stats_record_start(struct dm_stats *stats, struct dm_stats_aux *aux)
{
	if (unlikely(stats->precise_timestamps))
		aux->duration_ns = ktime_to_ns(ktime_get());
}

#endif
