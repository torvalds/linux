// SPDX-License-Identifier: GPL-2.0
/*
 * bcache stats code
 *
 * Copyright 2012 Google, Inc.
 */

#include "bcache.h"
#include "stats.h"
#include "btree.h"
#include "sysfs.h"

/*
 * We keep absolute totals of various statistics, and addionally a set of three
 * rolling averages.
 *
 * Every so often, a timer goes off and rescales the rolling averages.
 * accounting_rescale[] is how many times the timer has to go off before we
 * rescale each set of numbers; that gets us half lives of 5 minutes, one hour,
 * and one day.
 *
 * accounting_delay is how often the timer goes off - 22 times in 5 minutes,
 * and accounting_weight is what we use to rescale:
 *
 * pow(31 / 32, 22) ~= 1/2
 *
 * So that we don't have to increment each set of numbers every time we (say)
 * get a cache hit, we increment a single atomic_t in acc->collector, and when
 * the rescale function runs it resets the atomic counter to 0 and adds its
 * old value to each of the exported numbers.
 *
 * To reduce rounding error, the numbers in struct cache_stats are all
 * stored left shifted by 16, and scaled back in the sysfs show() function.
 */

static const unsigned int DAY_RESCALE		= 288;
static const unsigned int HOUR_RESCALE		= 12;
static const unsigned int FIVE_MINUTE_RESCALE	= 1;
static const unsigned int accounting_delay	= (HZ * 300) / 22;
static const unsigned int accounting_weight	= 32;

/* sysfs reading/writing */

read_attribute(cache_hits);
read_attribute(cache_misses);
read_attribute(cache_bypass_hits);
read_attribute(cache_bypass_misses);
read_attribute(cache_hit_ratio);
read_attribute(cache_readaheads);
read_attribute(cache_miss_collisions);
read_attribute(bypassed);

SHOW(bch_stats)
{
	struct cache_stats *s =
		container_of(kobj, struct cache_stats, kobj);
#define var(stat)		(s->stat >> 16)
	var_print(cache_hits);
	var_print(cache_misses);
	var_print(cache_bypass_hits);
	var_print(cache_bypass_misses);

	sysfs_print(cache_hit_ratio,
		    DIV_SAFE(var(cache_hits) * 100,
			     var(cache_hits) + var(cache_misses)));

	var_print(cache_readaheads);
	var_print(cache_miss_collisions);
	sysfs_hprint(bypassed,	var(sectors_bypassed) << 9);
#undef var
	return 0;
}

STORE(bch_stats)
{
	return size;
}

static void bch_stats_release(struct kobject *k)
{
}

static struct attribute *bch_stats_files[] = {
	&sysfs_cache_hits,
	&sysfs_cache_misses,
	&sysfs_cache_bypass_hits,
	&sysfs_cache_bypass_misses,
	&sysfs_cache_hit_ratio,
	&sysfs_cache_readaheads,
	&sysfs_cache_miss_collisions,
	&sysfs_bypassed,
	NULL
};
static KTYPE(bch_stats);

int bch_cache_accounting_add_kobjs(struct cache_accounting *acc,
				   struct kobject *parent)
{
	int ret = kobject_add(&acc->total.kobj, parent,
			      "stats_total");
	ret = ret ?: kobject_add(&acc->five_minute.kobj, parent,
				 "stats_five_minute");
	ret = ret ?: kobject_add(&acc->hour.kobj, parent,
				 "stats_hour");
	ret = ret ?: kobject_add(&acc->day.kobj, parent,
				 "stats_day");
	return ret;
}

void bch_cache_accounting_clear(struct cache_accounting *acc)
{
	acc->total.cache_hits = 0;
	acc->total.cache_misses = 0;
	acc->total.cache_bypass_hits = 0;
	acc->total.cache_bypass_misses = 0;
	acc->total.cache_readaheads = 0;
	acc->total.cache_miss_collisions = 0;
	acc->total.sectors_bypassed = 0;
}

void bch_cache_accounting_destroy(struct cache_accounting *acc)
{
	kobject_put(&acc->total.kobj);
	kobject_put(&acc->five_minute.kobj);
	kobject_put(&acc->hour.kobj);
	kobject_put(&acc->day.kobj);

	atomic_set(&acc->closing, 1);
	if (del_timer_sync(&acc->timer))
		closure_return(&acc->cl);
}

/* EWMA scaling */

static void scale_stat(unsigned long *stat)
{
	*stat =  ewma_add(*stat, 0, accounting_weight, 0);
}

static void scale_stats(struct cache_stats *stats, unsigned long rescale_at)
{
	if (++stats->rescale == rescale_at) {
		stats->rescale = 0;
		scale_stat(&stats->cache_hits);
		scale_stat(&stats->cache_misses);
		scale_stat(&stats->cache_bypass_hits);
		scale_stat(&stats->cache_bypass_misses);
		scale_stat(&stats->cache_readaheads);
		scale_stat(&stats->cache_miss_collisions);
		scale_stat(&stats->sectors_bypassed);
	}
}

static void scale_accounting(struct timer_list *t)
{
	struct cache_accounting *acc = from_timer(acc, t, timer);

#define move_stat(name) do {						\
	unsigned int t = atomic_xchg(&acc->collector.name, 0);		\
	t <<= 16;							\
	acc->five_minute.name += t;					\
	acc->hour.name += t;						\
	acc->day.name += t;						\
	acc->total.name += t;						\
} while (0)

	move_stat(cache_hits);
	move_stat(cache_misses);
	move_stat(cache_bypass_hits);
	move_stat(cache_bypass_misses);
	move_stat(cache_readaheads);
	move_stat(cache_miss_collisions);
	move_stat(sectors_bypassed);

	scale_stats(&acc->total, 0);
	scale_stats(&acc->day, DAY_RESCALE);
	scale_stats(&acc->hour, HOUR_RESCALE);
	scale_stats(&acc->five_minute, FIVE_MINUTE_RESCALE);

	acc->timer.expires += accounting_delay;

	if (!atomic_read(&acc->closing))
		add_timer(&acc->timer);
	else
		closure_return(&acc->cl);
}

static void mark_cache_stats(struct cache_stat_collector *stats,
			     bool hit, bool bypass)
{
	if (!bypass)
		if (hit)
			atomic_inc(&stats->cache_hits);
		else
			atomic_inc(&stats->cache_misses);
	else
		if (hit)
			atomic_inc(&stats->cache_bypass_hits);
		else
			atomic_inc(&stats->cache_bypass_misses);
}

void bch_mark_cache_accounting(struct cache_set *c, struct bcache_device *d,
			       bool hit, bool bypass)
{
	struct cached_dev *dc = container_of(d, struct cached_dev, disk);

	mark_cache_stats(&dc->accounting.collector, hit, bypass);
	mark_cache_stats(&c->accounting.collector, hit, bypass);
}

void bch_mark_cache_readahead(struct cache_set *c, struct bcache_device *d)
{
	struct cached_dev *dc = container_of(d, struct cached_dev, disk);

	atomic_inc(&dc->accounting.collector.cache_readaheads);
	atomic_inc(&c->accounting.collector.cache_readaheads);
}

void bch_mark_cache_miss_collision(struct cache_set *c, struct bcache_device *d)
{
	struct cached_dev *dc = container_of(d, struct cached_dev, disk);

	atomic_inc(&dc->accounting.collector.cache_miss_collisions);
	atomic_inc(&c->accounting.collector.cache_miss_collisions);
}

void bch_mark_sectors_bypassed(struct cache_set *c, struct cached_dev *dc,
			       int sectors)
{
	atomic_add(sectors, &dc->accounting.collector.sectors_bypassed);
	atomic_add(sectors, &c->accounting.collector.sectors_bypassed);
}

void bch_cache_accounting_init(struct cache_accounting *acc,
			       struct closure *parent)
{
	kobject_init(&acc->total.kobj,		&bch_stats_ktype);
	kobject_init(&acc->five_minute.kobj,	&bch_stats_ktype);
	kobject_init(&acc->hour.kobj,		&bch_stats_ktype);
	kobject_init(&acc->day.kobj,		&bch_stats_ktype);

	closure_init(&acc->cl, parent);
	timer_setup(&acc->timer, scale_accounting, 0);
	acc->timer.expires	= jiffies + accounting_delay;
	add_timer(&acc->timer);
}
