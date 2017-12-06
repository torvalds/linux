// SPDX-License-Identifier: GPL-2.0
/*
 * bcache sysfs interfaces
 *
 * Copyright 2010, 2011 Kent Overstreet <kent.overstreet@gmail.com>
 * Copyright 2012 Google, Inc.
 */

#include "bcache.h"
#include "sysfs.h"
#include "btree.h"
#include "request.h"
#include "writeback.h"

#include <linux/blkdev.h>
#include <linux/sort.h>
#include <linux/sched/clock.h>

static const char * const cache_replacement_policies[] = {
	"lru",
	"fifo",
	"random",
	NULL
};

static const char * const error_actions[] = {
	"unregister",
	"panic",
	NULL
};

write_attribute(attach);
write_attribute(detach);
write_attribute(unregister);
write_attribute(stop);
write_attribute(clear_stats);
write_attribute(trigger_gc);
write_attribute(prune_cache);
write_attribute(flash_vol_create);

read_attribute(bucket_size);
read_attribute(block_size);
read_attribute(nbuckets);
read_attribute(tree_depth);
read_attribute(root_usage_percent);
read_attribute(priority_stats);
read_attribute(btree_cache_size);
read_attribute(btree_cache_max_chain);
read_attribute(cache_available_percent);
read_attribute(written);
read_attribute(btree_written);
read_attribute(metadata_written);
read_attribute(active_journal_entries);

sysfs_time_stats_attribute(btree_gc,	sec, ms);
sysfs_time_stats_attribute(btree_split, sec, us);
sysfs_time_stats_attribute(btree_sort,	ms,  us);
sysfs_time_stats_attribute(btree_read,	ms,  us);

read_attribute(btree_nodes);
read_attribute(btree_used_percent);
read_attribute(average_key_size);
read_attribute(dirty_data);
read_attribute(bset_tree_stats);

read_attribute(state);
read_attribute(cache_read_races);
read_attribute(writeback_keys_done);
read_attribute(writeback_keys_failed);
read_attribute(io_errors);
read_attribute(congested);
rw_attribute(congested_read_threshold_us);
rw_attribute(congested_write_threshold_us);

rw_attribute(sequential_cutoff);
rw_attribute(data_csum);
rw_attribute(cache_mode);
rw_attribute(writeback_metadata);
rw_attribute(writeback_running);
rw_attribute(writeback_percent);
rw_attribute(writeback_delay);
rw_attribute(writeback_rate);

rw_attribute(writeback_rate_update_seconds);
rw_attribute(writeback_rate_d_term);
rw_attribute(writeback_rate_p_term_inverse);
read_attribute(writeback_rate_debug);

read_attribute(stripe_size);
read_attribute(partial_stripes_expensive);

rw_attribute(synchronous);
rw_attribute(journal_delay_ms);
rw_attribute(discard);
rw_attribute(running);
rw_attribute(label);
rw_attribute(readahead);
rw_attribute(errors);
rw_attribute(io_error_limit);
rw_attribute(io_error_halflife);
rw_attribute(verify);
rw_attribute(bypass_torture_test);
rw_attribute(key_merging_disabled);
rw_attribute(gc_always_rewrite);
rw_attribute(expensive_debug_checks);
rw_attribute(cache_replacement_policy);
rw_attribute(btree_shrinker_disabled);
rw_attribute(copy_gc_enabled);
rw_attribute(size);

SHOW(__bch_cached_dev)
{
	struct cached_dev *dc = container_of(kobj, struct cached_dev,
					     disk.kobj);
	const char *states[] = { "no cache", "clean", "dirty", "inconsistent" };

#define var(stat)		(dc->stat)

	if (attr == &sysfs_cache_mode)
		return bch_snprint_string_list(buf, PAGE_SIZE,
					       bch_cache_modes + 1,
					       BDEV_CACHE_MODE(&dc->sb));

	sysfs_printf(data_csum,		"%i", dc->disk.data_csum);
	var_printf(verify,		"%i");
	var_printf(bypass_torture_test,	"%i");
	var_printf(writeback_metadata,	"%i");
	var_printf(writeback_running,	"%i");
	var_print(writeback_delay);
	var_print(writeback_percent);
	sysfs_hprint(writeback_rate,	dc->writeback_rate.rate << 9);

	var_print(writeback_rate_update_seconds);
	var_print(writeback_rate_d_term);
	var_print(writeback_rate_p_term_inverse);

	if (attr == &sysfs_writeback_rate_debug) {
		char rate[20];
		char dirty[20];
		char target[20];
		char proportional[20];
		char derivative[20];
		char change[20];
		s64 next_io;

		bch_hprint(rate,	dc->writeback_rate.rate << 9);
		bch_hprint(dirty,	bcache_dev_sectors_dirty(&dc->disk) << 9);
		bch_hprint(target,	dc->writeback_rate_target << 9);
		bch_hprint(proportional,dc->writeback_rate_proportional << 9);
		bch_hprint(derivative,	dc->writeback_rate_derivative << 9);
		bch_hprint(change,	dc->writeback_rate_change << 9);

		next_io = div64_s64(dc->writeback_rate.next - local_clock(),
				    NSEC_PER_MSEC);

		return sprintf(buf,
			       "rate:\t\t%s/sec\n"
			       "dirty:\t\t%s\n"
			       "target:\t\t%s\n"
			       "proportional:\t%s\n"
			       "derivative:\t%s\n"
			       "change:\t\t%s/sec\n"
			       "next io:\t%llims\n",
			       rate, dirty, target, proportional,
			       derivative, change, next_io);
	}

	sysfs_hprint(dirty_data,
		     bcache_dev_sectors_dirty(&dc->disk) << 9);

	sysfs_hprint(stripe_size,	dc->disk.stripe_size << 9);
	var_printf(partial_stripes_expensive,	"%u");

	var_hprint(sequential_cutoff);
	var_hprint(readahead);

	sysfs_print(running,		atomic_read(&dc->running));
	sysfs_print(state,		states[BDEV_STATE(&dc->sb)]);

	if (attr == &sysfs_label) {
		memcpy(buf, dc->sb.label, SB_LABEL_SIZE);
		buf[SB_LABEL_SIZE + 1] = '\0';
		strcat(buf, "\n");
		return strlen(buf);
	}

#undef var
	return 0;
}
SHOW_LOCKED(bch_cached_dev)

STORE(__cached_dev)
{
	struct cached_dev *dc = container_of(kobj, struct cached_dev,
					     disk.kobj);
	ssize_t v = size;
	struct cache_set *c;
	struct kobj_uevent_env *env;

#define d_strtoul(var)		sysfs_strtoul(var, dc->var)
#define d_strtoul_nonzero(var)	sysfs_strtoul_clamp(var, dc->var, 1, INT_MAX)
#define d_strtoi_h(var)		sysfs_hatoi(var, dc->var)

	sysfs_strtoul(data_csum,	dc->disk.data_csum);
	d_strtoul(verify);
	d_strtoul(bypass_torture_test);
	d_strtoul(writeback_metadata);
	d_strtoul(writeback_running);
	d_strtoul(writeback_delay);

	sysfs_strtoul_clamp(writeback_percent, dc->writeback_percent, 0, 40);

	sysfs_strtoul_clamp(writeback_rate,
			    dc->writeback_rate.rate, 1, INT_MAX);

	d_strtoul_nonzero(writeback_rate_update_seconds);
	d_strtoul(writeback_rate_d_term);
	d_strtoul_nonzero(writeback_rate_p_term_inverse);

	d_strtoi_h(sequential_cutoff);
	d_strtoi_h(readahead);

	if (attr == &sysfs_clear_stats)
		bch_cache_accounting_clear(&dc->accounting);

	if (attr == &sysfs_running &&
	    strtoul_or_return(buf))
		bch_cached_dev_run(dc);

	if (attr == &sysfs_cache_mode) {
		v = bch_read_string_list(buf, bch_cache_modes + 1);

		if (v < 0)
			return v;

		if ((unsigned) v != BDEV_CACHE_MODE(&dc->sb)) {
			SET_BDEV_CACHE_MODE(&dc->sb, v);
			bch_write_bdev_super(dc, NULL);
		}
	}

	if (attr == &sysfs_label) {
		if (size > SB_LABEL_SIZE)
			return -EINVAL;
		memcpy(dc->sb.label, buf, size);
		if (size < SB_LABEL_SIZE)
			dc->sb.label[size] = '\0';
		if (size && dc->sb.label[size - 1] == '\n')
			dc->sb.label[size - 1] = '\0';
		bch_write_bdev_super(dc, NULL);
		if (dc->disk.c) {
			memcpy(dc->disk.c->uuids[dc->disk.id].label,
			       buf, SB_LABEL_SIZE);
			bch_uuid_write(dc->disk.c);
		}
		env = kzalloc(sizeof(struct kobj_uevent_env), GFP_KERNEL);
		if (!env)
			return -ENOMEM;
		add_uevent_var(env, "DRIVER=bcache");
		add_uevent_var(env, "CACHED_UUID=%pU", dc->sb.uuid),
		add_uevent_var(env, "CACHED_LABEL=%s", buf);
		kobject_uevent_env(
			&disk_to_dev(dc->disk.disk)->kobj, KOBJ_CHANGE, env->envp);
		kfree(env);
	}

	if (attr == &sysfs_attach) {
		if (bch_parse_uuid(buf, dc->sb.set_uuid) < 16)
			return -EINVAL;

		list_for_each_entry(c, &bch_cache_sets, list) {
			v = bch_cached_dev_attach(dc, c);
			if (!v)
				return size;
		}

		pr_err("Can't attach %s: cache set not found", buf);
		size = v;
	}

	if (attr == &sysfs_detach && dc->disk.c)
		bch_cached_dev_detach(dc);

	if (attr == &sysfs_stop)
		bcache_device_stop(&dc->disk);

	return size;
}

STORE(bch_cached_dev)
{
	struct cached_dev *dc = container_of(kobj, struct cached_dev,
					     disk.kobj);

	mutex_lock(&bch_register_lock);
	size = __cached_dev_store(kobj, attr, buf, size);

	if (attr == &sysfs_writeback_running)
		bch_writeback_queue(dc);

	if (attr == &sysfs_writeback_percent)
		schedule_delayed_work(&dc->writeback_rate_update,
				      dc->writeback_rate_update_seconds * HZ);

	mutex_unlock(&bch_register_lock);
	return size;
}

static struct attribute *bch_cached_dev_files[] = {
	&sysfs_attach,
	&sysfs_detach,
	&sysfs_stop,
#if 0
	&sysfs_data_csum,
#endif
	&sysfs_cache_mode,
	&sysfs_writeback_metadata,
	&sysfs_writeback_running,
	&sysfs_writeback_delay,
	&sysfs_writeback_percent,
	&sysfs_writeback_rate,
	&sysfs_writeback_rate_update_seconds,
	&sysfs_writeback_rate_d_term,
	&sysfs_writeback_rate_p_term_inverse,
	&sysfs_writeback_rate_debug,
	&sysfs_dirty_data,
	&sysfs_stripe_size,
	&sysfs_partial_stripes_expensive,
	&sysfs_sequential_cutoff,
	&sysfs_clear_stats,
	&sysfs_running,
	&sysfs_state,
	&sysfs_label,
	&sysfs_readahead,
#ifdef CONFIG_BCACHE_DEBUG
	&sysfs_verify,
	&sysfs_bypass_torture_test,
#endif
	NULL
};
KTYPE(bch_cached_dev);

SHOW(bch_flash_dev)
{
	struct bcache_device *d = container_of(kobj, struct bcache_device,
					       kobj);
	struct uuid_entry *u = &d->c->uuids[d->id];

	sysfs_printf(data_csum,	"%i", d->data_csum);
	sysfs_hprint(size,	u->sectors << 9);

	if (attr == &sysfs_label) {
		memcpy(buf, u->label, SB_LABEL_SIZE);
		buf[SB_LABEL_SIZE + 1] = '\0';
		strcat(buf, "\n");
		return strlen(buf);
	}

	return 0;
}

STORE(__bch_flash_dev)
{
	struct bcache_device *d = container_of(kobj, struct bcache_device,
					       kobj);
	struct uuid_entry *u = &d->c->uuids[d->id];

	sysfs_strtoul(data_csum,	d->data_csum);

	if (attr == &sysfs_size) {
		uint64_t v;
		strtoi_h_or_return(buf, v);

		u->sectors = v >> 9;
		bch_uuid_write(d->c);
		set_capacity(d->disk, u->sectors);
	}

	if (attr == &sysfs_label) {
		memcpy(u->label, buf, SB_LABEL_SIZE);
		bch_uuid_write(d->c);
	}

	if (attr == &sysfs_unregister) {
		set_bit(BCACHE_DEV_DETACHING, &d->flags);
		bcache_device_stop(d);
	}

	return size;
}
STORE_LOCKED(bch_flash_dev)

static struct attribute *bch_flash_dev_files[] = {
	&sysfs_unregister,
#if 0
	&sysfs_data_csum,
#endif
	&sysfs_label,
	&sysfs_size,
	NULL
};
KTYPE(bch_flash_dev);

struct bset_stats_op {
	struct btree_op op;
	size_t nodes;
	struct bset_stats stats;
};

static int bch_btree_bset_stats(struct btree_op *b_op, struct btree *b)
{
	struct bset_stats_op *op = container_of(b_op, struct bset_stats_op, op);

	op->nodes++;
	bch_btree_keys_stats(&b->keys, &op->stats);

	return MAP_CONTINUE;
}

static int bch_bset_print_stats(struct cache_set *c, char *buf)
{
	struct bset_stats_op op;
	int ret;

	memset(&op, 0, sizeof(op));
	bch_btree_op_init(&op.op, -1);

	ret = bch_btree_map_nodes(&op.op, c, &ZERO_KEY, bch_btree_bset_stats);
	if (ret < 0)
		return ret;

	return snprintf(buf, PAGE_SIZE,
			"btree nodes:		%zu\n"
			"written sets:		%zu\n"
			"unwritten sets:		%zu\n"
			"written key bytes:	%zu\n"
			"unwritten key bytes:	%zu\n"
			"floats:			%zu\n"
			"failed:			%zu\n",
			op.nodes,
			op.stats.sets_written, op.stats.sets_unwritten,
			op.stats.bytes_written, op.stats.bytes_unwritten,
			op.stats.floats, op.stats.failed);
}

static unsigned bch_root_usage(struct cache_set *c)
{
	unsigned bytes = 0;
	struct bkey *k;
	struct btree *b;
	struct btree_iter iter;

	goto lock_root;

	do {
		rw_unlock(false, b);
lock_root:
		b = c->root;
		rw_lock(false, b, b->level);
	} while (b != c->root);

	for_each_key_filter(&b->keys, k, &iter, bch_ptr_bad)
		bytes += bkey_bytes(k);

	rw_unlock(false, b);

	return (bytes * 100) / btree_bytes(c);
}

static size_t bch_cache_size(struct cache_set *c)
{
	size_t ret = 0;
	struct btree *b;

	mutex_lock(&c->bucket_lock);
	list_for_each_entry(b, &c->btree_cache, list)
		ret += 1 << (b->keys.page_order + PAGE_SHIFT);

	mutex_unlock(&c->bucket_lock);
	return ret;
}

static unsigned bch_cache_max_chain(struct cache_set *c)
{
	unsigned ret = 0;
	struct hlist_head *h;

	mutex_lock(&c->bucket_lock);

	for (h = c->bucket_hash;
	     h < c->bucket_hash + (1 << BUCKET_HASH_BITS);
	     h++) {
		unsigned i = 0;
		struct hlist_node *p;

		hlist_for_each(p, h)
			i++;

		ret = max(ret, i);
	}

	mutex_unlock(&c->bucket_lock);
	return ret;
}

static unsigned bch_btree_used(struct cache_set *c)
{
	return div64_u64(c->gc_stats.key_bytes * 100,
			 (c->gc_stats.nodes ?: 1) * btree_bytes(c));
}

static unsigned bch_average_key_size(struct cache_set *c)
{
	return c->gc_stats.nkeys
		? div64_u64(c->gc_stats.data, c->gc_stats.nkeys)
		: 0;
}

SHOW(__bch_cache_set)
{
	struct cache_set *c = container_of(kobj, struct cache_set, kobj);

	sysfs_print(synchronous,		CACHE_SYNC(&c->sb));
	sysfs_print(journal_delay_ms,		c->journal_delay_ms);
	sysfs_hprint(bucket_size,		bucket_bytes(c));
	sysfs_hprint(block_size,		block_bytes(c));
	sysfs_print(tree_depth,			c->root->level);
	sysfs_print(root_usage_percent,		bch_root_usage(c));

	sysfs_hprint(btree_cache_size,		bch_cache_size(c));
	sysfs_print(btree_cache_max_chain,	bch_cache_max_chain(c));
	sysfs_print(cache_available_percent,	100 - c->gc_stats.in_use);

	sysfs_print_time_stats(&c->btree_gc_time,	btree_gc, sec, ms);
	sysfs_print_time_stats(&c->btree_split_time,	btree_split, sec, us);
	sysfs_print_time_stats(&c->sort.time,		btree_sort, ms, us);
	sysfs_print_time_stats(&c->btree_read_time,	btree_read, ms, us);

	sysfs_print(btree_used_percent,	bch_btree_used(c));
	sysfs_print(btree_nodes,	c->gc_stats.nodes);
	sysfs_hprint(average_key_size,	bch_average_key_size(c));

	sysfs_print(cache_read_races,
		    atomic_long_read(&c->cache_read_races));

	sysfs_print(writeback_keys_done,
		    atomic_long_read(&c->writeback_keys_done));
	sysfs_print(writeback_keys_failed,
		    atomic_long_read(&c->writeback_keys_failed));

	if (attr == &sysfs_errors)
		return bch_snprint_string_list(buf, PAGE_SIZE, error_actions,
					       c->on_error);

	/* See count_io_errors for why 88 */
	sysfs_print(io_error_halflife,	c->error_decay * 88);
	sysfs_print(io_error_limit,	c->error_limit >> IO_ERROR_SHIFT);

	sysfs_hprint(congested,
		     ((uint64_t) bch_get_congested(c)) << 9);
	sysfs_print(congested_read_threshold_us,
		    c->congested_read_threshold_us);
	sysfs_print(congested_write_threshold_us,
		    c->congested_write_threshold_us);

	sysfs_print(active_journal_entries,	fifo_used(&c->journal.pin));
	sysfs_printf(verify,			"%i", c->verify);
	sysfs_printf(key_merging_disabled,	"%i", c->key_merging_disabled);
	sysfs_printf(expensive_debug_checks,
		     "%i", c->expensive_debug_checks);
	sysfs_printf(gc_always_rewrite,		"%i", c->gc_always_rewrite);
	sysfs_printf(btree_shrinker_disabled,	"%i", c->shrinker_disabled);
	sysfs_printf(copy_gc_enabled,		"%i", c->copy_gc_enabled);

	if (attr == &sysfs_bset_tree_stats)
		return bch_bset_print_stats(c, buf);

	return 0;
}
SHOW_LOCKED(bch_cache_set)

STORE(__bch_cache_set)
{
	struct cache_set *c = container_of(kobj, struct cache_set, kobj);

	if (attr == &sysfs_unregister)
		bch_cache_set_unregister(c);

	if (attr == &sysfs_stop)
		bch_cache_set_stop(c);

	if (attr == &sysfs_synchronous) {
		bool sync = strtoul_or_return(buf);

		if (sync != CACHE_SYNC(&c->sb)) {
			SET_CACHE_SYNC(&c->sb, sync);
			bcache_write_super(c);
		}
	}

	if (attr == &sysfs_flash_vol_create) {
		int r;
		uint64_t v;
		strtoi_h_or_return(buf, v);

		r = bch_flash_dev_create(c, v);
		if (r)
			return r;
	}

	if (attr == &sysfs_clear_stats) {
		atomic_long_set(&c->writeback_keys_done,	0);
		atomic_long_set(&c->writeback_keys_failed,	0);

		memset(&c->gc_stats, 0, sizeof(struct gc_stat));
		bch_cache_accounting_clear(&c->accounting);
	}

	if (attr == &sysfs_trigger_gc) {
		/*
		 * Garbage collection thread only works when sectors_to_gc < 0,
		 * when users write to sysfs entry trigger_gc, most of time
		 * they want to forcibly triger gargage collection. Here -1 is
		 * set to c->sectors_to_gc, to make gc_should_run() give a
		 * chance to permit gc thread to run. "give a chance" means
		 * before going into gc_should_run(), there is still chance
		 * that c->sectors_to_gc being set to other positive value. So
		 * writing sysfs entry trigger_gc won't always make sure gc
		 * thread takes effect.
		 */
		atomic_set(&c->sectors_to_gc, -1);
		wake_up_gc(c);
	}

	if (attr == &sysfs_prune_cache) {
		struct shrink_control sc;
		sc.gfp_mask = GFP_KERNEL;
		sc.nr_to_scan = strtoul_or_return(buf);
		c->shrink.scan_objects(&c->shrink, &sc);
	}

	sysfs_strtoul(congested_read_threshold_us,
		      c->congested_read_threshold_us);
	sysfs_strtoul(congested_write_threshold_us,
		      c->congested_write_threshold_us);

	if (attr == &sysfs_errors) {
		ssize_t v = bch_read_string_list(buf, error_actions);

		if (v < 0)
			return v;

		c->on_error = v;
	}

	if (attr == &sysfs_io_error_limit)
		c->error_limit = strtoul_or_return(buf) << IO_ERROR_SHIFT;

	/* See count_io_errors() for why 88 */
	if (attr == &sysfs_io_error_halflife)
		c->error_decay = strtoul_or_return(buf) / 88;

	sysfs_strtoul(journal_delay_ms,		c->journal_delay_ms);
	sysfs_strtoul(verify,			c->verify);
	sysfs_strtoul(key_merging_disabled,	c->key_merging_disabled);
	sysfs_strtoul(expensive_debug_checks,	c->expensive_debug_checks);
	sysfs_strtoul(gc_always_rewrite,	c->gc_always_rewrite);
	sysfs_strtoul(btree_shrinker_disabled,	c->shrinker_disabled);
	sysfs_strtoul(copy_gc_enabled,		c->copy_gc_enabled);

	return size;
}
STORE_LOCKED(bch_cache_set)

SHOW(bch_cache_set_internal)
{
	struct cache_set *c = container_of(kobj, struct cache_set, internal);
	return bch_cache_set_show(&c->kobj, attr, buf);
}

STORE(bch_cache_set_internal)
{
	struct cache_set *c = container_of(kobj, struct cache_set, internal);
	return bch_cache_set_store(&c->kobj, attr, buf, size);
}

static void bch_cache_set_internal_release(struct kobject *k)
{
}

static struct attribute *bch_cache_set_files[] = {
	&sysfs_unregister,
	&sysfs_stop,
	&sysfs_synchronous,
	&sysfs_journal_delay_ms,
	&sysfs_flash_vol_create,

	&sysfs_bucket_size,
	&sysfs_block_size,
	&sysfs_tree_depth,
	&sysfs_root_usage_percent,
	&sysfs_btree_cache_size,
	&sysfs_cache_available_percent,

	&sysfs_average_key_size,

	&sysfs_errors,
	&sysfs_io_error_limit,
	&sysfs_io_error_halflife,
	&sysfs_congested,
	&sysfs_congested_read_threshold_us,
	&sysfs_congested_write_threshold_us,
	&sysfs_clear_stats,
	NULL
};
KTYPE(bch_cache_set);

static struct attribute *bch_cache_set_internal_files[] = {
	&sysfs_active_journal_entries,

	sysfs_time_stats_attribute_list(btree_gc, sec, ms)
	sysfs_time_stats_attribute_list(btree_split, sec, us)
	sysfs_time_stats_attribute_list(btree_sort, ms, us)
	sysfs_time_stats_attribute_list(btree_read, ms, us)

	&sysfs_btree_nodes,
	&sysfs_btree_used_percent,
	&sysfs_btree_cache_max_chain,

	&sysfs_bset_tree_stats,
	&sysfs_cache_read_races,
	&sysfs_writeback_keys_done,
	&sysfs_writeback_keys_failed,

	&sysfs_trigger_gc,
	&sysfs_prune_cache,
#ifdef CONFIG_BCACHE_DEBUG
	&sysfs_verify,
	&sysfs_key_merging_disabled,
	&sysfs_expensive_debug_checks,
#endif
	&sysfs_gc_always_rewrite,
	&sysfs_btree_shrinker_disabled,
	&sysfs_copy_gc_enabled,
	NULL
};
KTYPE(bch_cache_set_internal);

SHOW(__bch_cache)
{
	struct cache *ca = container_of(kobj, struct cache, kobj);

	sysfs_hprint(bucket_size,	bucket_bytes(ca));
	sysfs_hprint(block_size,	block_bytes(ca));
	sysfs_print(nbuckets,		ca->sb.nbuckets);
	sysfs_print(discard,		ca->discard);
	sysfs_hprint(written, atomic_long_read(&ca->sectors_written) << 9);
	sysfs_hprint(btree_written,
		     atomic_long_read(&ca->btree_sectors_written) << 9);
	sysfs_hprint(metadata_written,
		     (atomic_long_read(&ca->meta_sectors_written) +
		      atomic_long_read(&ca->btree_sectors_written)) << 9);

	sysfs_print(io_errors,
		    atomic_read(&ca->io_errors) >> IO_ERROR_SHIFT);

	if (attr == &sysfs_cache_replacement_policy)
		return bch_snprint_string_list(buf, PAGE_SIZE,
					       cache_replacement_policies,
					       CACHE_REPLACEMENT(&ca->sb));

	if (attr == &sysfs_priority_stats) {
		int cmp(const void *l, const void *r)
		{	return *((uint16_t *) r) - *((uint16_t *) l); }

		struct bucket *b;
		size_t n = ca->sb.nbuckets, i;
		size_t unused = 0, available = 0, dirty = 0, meta = 0;
		uint64_t sum = 0;
		/* Compute 31 quantiles */
		uint16_t q[31], *p, *cached;
		ssize_t ret;

		cached = p = vmalloc(ca->sb.nbuckets * sizeof(uint16_t));
		if (!p)
			return -ENOMEM;

		mutex_lock(&ca->set->bucket_lock);
		for_each_bucket(b, ca) {
			if (!GC_SECTORS_USED(b))
				unused++;
			if (GC_MARK(b) == GC_MARK_RECLAIMABLE)
				available++;
			if (GC_MARK(b) == GC_MARK_DIRTY)
				dirty++;
			if (GC_MARK(b) == GC_MARK_METADATA)
				meta++;
		}

		for (i = ca->sb.first_bucket; i < n; i++)
			p[i] = ca->buckets[i].prio;
		mutex_unlock(&ca->set->bucket_lock);

		sort(p, n, sizeof(uint16_t), cmp, NULL);

		while (n &&
		       !cached[n - 1])
			--n;

		unused = ca->sb.nbuckets - n;

		while (cached < p + n &&
		       *cached == BTREE_PRIO)
			cached++, n--;

		for (i = 0; i < n; i++)
			sum += INITIAL_PRIO - cached[i];

		if (n)
			do_div(sum, n);

		for (i = 0; i < ARRAY_SIZE(q); i++)
			q[i] = INITIAL_PRIO - cached[n * (i + 1) /
				(ARRAY_SIZE(q) + 1)];

		vfree(p);

		ret = scnprintf(buf, PAGE_SIZE,
				"Unused:		%zu%%\n"
				"Clean:		%zu%%\n"
				"Dirty:		%zu%%\n"
				"Metadata:	%zu%%\n"
				"Average:	%llu\n"
				"Sectors per Q:	%zu\n"
				"Quantiles:	[",
				unused * 100 / (size_t) ca->sb.nbuckets,
				available * 100 / (size_t) ca->sb.nbuckets,
				dirty * 100 / (size_t) ca->sb.nbuckets,
				meta * 100 / (size_t) ca->sb.nbuckets, sum,
				n * ca->sb.bucket_size / (ARRAY_SIZE(q) + 1));

		for (i = 0; i < ARRAY_SIZE(q); i++)
			ret += scnprintf(buf + ret, PAGE_SIZE - ret,
					 "%u ", q[i]);
		ret--;

		ret += scnprintf(buf + ret, PAGE_SIZE - ret, "]\n");

		return ret;
	}

	return 0;
}
SHOW_LOCKED(bch_cache)

STORE(__bch_cache)
{
	struct cache *ca = container_of(kobj, struct cache, kobj);

	if (attr == &sysfs_discard) {
		bool v = strtoul_or_return(buf);

		if (blk_queue_discard(bdev_get_queue(ca->bdev)))
			ca->discard = v;

		if (v != CACHE_DISCARD(&ca->sb)) {
			SET_CACHE_DISCARD(&ca->sb, v);
			bcache_write_super(ca->set);
		}
	}

	if (attr == &sysfs_cache_replacement_policy) {
		ssize_t v = bch_read_string_list(buf, cache_replacement_policies);

		if (v < 0)
			return v;

		if ((unsigned) v != CACHE_REPLACEMENT(&ca->sb)) {
			mutex_lock(&ca->set->bucket_lock);
			SET_CACHE_REPLACEMENT(&ca->sb, v);
			mutex_unlock(&ca->set->bucket_lock);

			bcache_write_super(ca->set);
		}
	}

	if (attr == &sysfs_clear_stats) {
		atomic_long_set(&ca->sectors_written, 0);
		atomic_long_set(&ca->btree_sectors_written, 0);
		atomic_long_set(&ca->meta_sectors_written, 0);
		atomic_set(&ca->io_count, 0);
		atomic_set(&ca->io_errors, 0);
	}

	return size;
}
STORE_LOCKED(bch_cache)

static struct attribute *bch_cache_files[] = {
	&sysfs_bucket_size,
	&sysfs_block_size,
	&sysfs_nbuckets,
	&sysfs_priority_stats,
	&sysfs_discard,
	&sysfs_written,
	&sysfs_btree_written,
	&sysfs_metadata_written,
	&sysfs_io_errors,
	&sysfs_clear_stats,
	&sysfs_cache_replacement_policy,
	NULL
};
KTYPE(bch_cache);
