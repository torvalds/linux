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
#include "features.h"

#include <linux/blkdev.h>
#include <linux/sort.h>
#include <linux/sched/clock.h>

extern bool bcache_is_reboot;

/* Default is 0 ("writethrough") */
static const char * const bch_cache_modes[] = {
	"writethrough",
	"writeback",
	"writearound",
	"none",
	NULL
};

static const char * const bch_reada_cache_policies[] = {
	"all",
	"meta-only",
	NULL
};

/* Default is 0 ("auto") */
static const char * const bch_stop_on_failure_modes[] = {
	"auto",
	"always",
	NULL
};

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
read_attribute(backing_dev_name);
read_attribute(backing_dev_uuid);

sysfs_time_stats_attribute(btree_gc,	sec, ms);
sysfs_time_stats_attribute(btree_split, sec, us);
sysfs_time_stats_attribute(btree_sort,	ms,  us);
sysfs_time_stats_attribute(btree_read,	ms,  us);

read_attribute(btree_nodes);
read_attribute(btree_used_percent);
read_attribute(average_key_size);
read_attribute(dirty_data);
read_attribute(bset_tree_stats);
read_attribute(feature_compat);
read_attribute(feature_ro_compat);
read_attribute(feature_incompat);

read_attribute(state);
read_attribute(cache_read_races);
read_attribute(reclaim);
read_attribute(reclaimed_journal_buckets);
read_attribute(flush_write);
read_attribute(writeback_keys_done);
read_attribute(writeback_keys_failed);
read_attribute(io_errors);
read_attribute(congested);
read_attribute(cutoff_writeback);
read_attribute(cutoff_writeback_sync);
rw_attribute(congested_read_threshold_us);
rw_attribute(congested_write_threshold_us);

rw_attribute(sequential_cutoff);
rw_attribute(data_csum);
rw_attribute(cache_mode);
rw_attribute(readahead_cache_policy);
rw_attribute(stop_when_cache_set_failed);
rw_attribute(writeback_metadata);
rw_attribute(writeback_running);
rw_attribute(writeback_percent);
rw_attribute(writeback_delay);
rw_attribute(writeback_rate);
rw_attribute(writeback_consider_fragment);

rw_attribute(writeback_rate_update_seconds);
rw_attribute(writeback_rate_i_term_inverse);
rw_attribute(writeback_rate_p_term_inverse);
rw_attribute(writeback_rate_fp_term_low);
rw_attribute(writeback_rate_fp_term_mid);
rw_attribute(writeback_rate_fp_term_high);
rw_attribute(writeback_rate_minimum);
read_attribute(writeback_rate_debug);

read_attribute(stripe_size);
read_attribute(partial_stripes_expensive);

rw_attribute(synchronous);
rw_attribute(journal_delay_ms);
rw_attribute(io_disable);
rw_attribute(discard);
rw_attribute(running);
rw_attribute(label);
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
rw_attribute(idle_max_writeback_rate);
rw_attribute(gc_after_writeback);
rw_attribute(size);

static ssize_t bch_snprint_string_list(char *buf,
				       size_t size,
				       const char * const list[],
				       size_t selected)
{
	char *out = buf;
	size_t i;

	for (i = 0; list[i]; i++)
		out += scnprintf(out, buf + size - out,
				i == selected ? "[%s] " : "%s ", list[i]);

	out[-1] = '\n';
	return out - buf;
}

SHOW(__bch_cached_dev)
{
	struct cached_dev *dc = container_of(kobj, struct cached_dev,
					     disk.kobj);
	char const *states[] = { "no cache", "clean", "dirty", "inconsistent" };
	int wb = dc->writeback_running;

#define var(stat)		(dc->stat)

	if (attr == &sysfs_cache_mode)
		return bch_snprint_string_list(buf, PAGE_SIZE,
					       bch_cache_modes,
					       BDEV_CACHE_MODE(&dc->sb));

	if (attr == &sysfs_readahead_cache_policy)
		return bch_snprint_string_list(buf, PAGE_SIZE,
					      bch_reada_cache_policies,
					      dc->cache_readahead_policy);

	if (attr == &sysfs_stop_when_cache_set_failed)
		return bch_snprint_string_list(buf, PAGE_SIZE,
					       bch_stop_on_failure_modes,
					       dc->stop_when_cache_set_failed);


	sysfs_printf(data_csum,		"%i", dc->disk.data_csum);
	var_printf(verify,		"%i");
	var_printf(bypass_torture_test,	"%i");
	var_printf(writeback_metadata,	"%i");
	var_printf(writeback_running,	"%i");
	var_printf(writeback_consider_fragment,	"%i");
	var_print(writeback_delay);
	var_print(writeback_percent);
	sysfs_hprint(writeback_rate,
		     wb ? atomic_long_read(&dc->writeback_rate.rate) << 9 : 0);
	sysfs_printf(io_errors,		"%i", atomic_read(&dc->io_errors));
	sysfs_printf(io_error_limit,	"%i", dc->error_limit);
	sysfs_printf(io_disable,	"%i", dc->io_disable);
	var_print(writeback_rate_update_seconds);
	var_print(writeback_rate_i_term_inverse);
	var_print(writeback_rate_p_term_inverse);
	var_print(writeback_rate_fp_term_low);
	var_print(writeback_rate_fp_term_mid);
	var_print(writeback_rate_fp_term_high);
	var_print(writeback_rate_minimum);

	if (attr == &sysfs_writeback_rate_debug) {
		char rate[20];
		char dirty[20];
		char target[20];
		char proportional[20];
		char integral[20];
		char change[20];
		s64 next_io;

		/*
		 * Except for dirty and target, other values should
		 * be 0 if writeback is not running.
		 */
		bch_hprint(rate,
			   wb ? atomic_long_read(&dc->writeback_rate.rate) << 9
			      : 0);
		bch_hprint(dirty, bcache_dev_sectors_dirty(&dc->disk) << 9);
		bch_hprint(target, dc->writeback_rate_target << 9);
		bch_hprint(proportional,
			   wb ? dc->writeback_rate_proportional << 9 : 0);
		bch_hprint(integral,
			   wb ? dc->writeback_rate_integral_scaled << 9 : 0);
		bch_hprint(change, wb ? dc->writeback_rate_change << 9 : 0);
		next_io = wb ? div64_s64(dc->writeback_rate.next-local_clock(),
					 NSEC_PER_MSEC) : 0;

		return sprintf(buf,
			       "rate:\t\t%s/sec\n"
			       "dirty:\t\t%s\n"
			       "target:\t\t%s\n"
			       "proportional:\t%s\n"
			       "integral:\t%s\n"
			       "change:\t\t%s/sec\n"
			       "next io:\t%llims\n",
			       rate, dirty, target, proportional,
			       integral, change, next_io);
	}

	sysfs_hprint(dirty_data,
		     bcache_dev_sectors_dirty(&dc->disk) << 9);

	sysfs_hprint(stripe_size,	 ((uint64_t)dc->disk.stripe_size) << 9);
	var_printf(partial_stripes_expensive,	"%u");

	var_hprint(sequential_cutoff);

	sysfs_print(running,		atomic_read(&dc->running));
	sysfs_print(state,		states[BDEV_STATE(&dc->sb)]);

	if (attr == &sysfs_label) {
		memcpy(buf, dc->sb.label, SB_LABEL_SIZE);
		buf[SB_LABEL_SIZE + 1] = '\0';
		strcat(buf, "\n");
		return strlen(buf);
	}

	if (attr == &sysfs_backing_dev_name) {
		snprintf(buf, BDEVNAME_SIZE + 1, "%pg", dc->bdev);
		strcat(buf, "\n");
		return strlen(buf);
	}

	if (attr == &sysfs_backing_dev_uuid) {
		/* convert binary uuid into 36-byte string plus '\0' */
		snprintf(buf, 36+1, "%pU", dc->sb.uuid);
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
	ssize_t v;
	struct cache_set *c;
	struct kobj_uevent_env *env;

	/* no user space access if system is rebooting */
	if (bcache_is_reboot)
		return -EBUSY;

#define d_strtoul(var)		sysfs_strtoul(var, dc->var)
#define d_strtoul_nonzero(var)	sysfs_strtoul_clamp(var, dc->var, 1, INT_MAX)
#define d_strtoi_h(var)		sysfs_hatoi(var, dc->var)

	sysfs_strtoul(data_csum,	dc->disk.data_csum);
	d_strtoul(verify);
	sysfs_strtoul_bool(bypass_torture_test, dc->bypass_torture_test);
	sysfs_strtoul_bool(writeback_metadata, dc->writeback_metadata);
	sysfs_strtoul_bool(writeback_running, dc->writeback_running);
	sysfs_strtoul_bool(writeback_consider_fragment, dc->writeback_consider_fragment);
	sysfs_strtoul_clamp(writeback_delay, dc->writeback_delay, 0, UINT_MAX);

	sysfs_strtoul_clamp(writeback_percent, dc->writeback_percent,
			    0, bch_cutoff_writeback);

	if (attr == &sysfs_writeback_rate) {
		ssize_t ret;
		long int v = atomic_long_read(&dc->writeback_rate.rate);

		ret = strtoul_safe_clamp(buf, v, 1, INT_MAX);

		if (!ret) {
			atomic_long_set(&dc->writeback_rate.rate, v);
			ret = size;
		}

		return ret;
	}

	sysfs_strtoul_clamp(writeback_rate_update_seconds,
			    dc->writeback_rate_update_seconds,
			    1, WRITEBACK_RATE_UPDATE_SECS_MAX);
	sysfs_strtoul_clamp(writeback_rate_i_term_inverse,
			    dc->writeback_rate_i_term_inverse,
			    1, UINT_MAX);
	sysfs_strtoul_clamp(writeback_rate_p_term_inverse,
			    dc->writeback_rate_p_term_inverse,
			    1, UINT_MAX);
	sysfs_strtoul_clamp(writeback_rate_fp_term_low,
			    dc->writeback_rate_fp_term_low,
			    1, dc->writeback_rate_fp_term_mid - 1);
	sysfs_strtoul_clamp(writeback_rate_fp_term_mid,
			    dc->writeback_rate_fp_term_mid,
			    dc->writeback_rate_fp_term_low + 1,
			    dc->writeback_rate_fp_term_high - 1);
	sysfs_strtoul_clamp(writeback_rate_fp_term_high,
			    dc->writeback_rate_fp_term_high,
			    dc->writeback_rate_fp_term_mid + 1, UINT_MAX);
	sysfs_strtoul_clamp(writeback_rate_minimum,
			    dc->writeback_rate_minimum,
			    1, UINT_MAX);

	sysfs_strtoul_clamp(io_error_limit, dc->error_limit, 0, INT_MAX);

	if (attr == &sysfs_io_disable) {
		int v = strtoul_or_return(buf);

		dc->io_disable = v ? 1 : 0;
	}

	sysfs_strtoul_clamp(sequential_cutoff,
			    dc->sequential_cutoff,
			    0, UINT_MAX);

	if (attr == &sysfs_clear_stats)
		bch_cache_accounting_clear(&dc->accounting);

	if (attr == &sysfs_running &&
	    strtoul_or_return(buf)) {
		v = bch_cached_dev_run(dc);
		if (v)
			return v;
	}

	if (attr == &sysfs_cache_mode) {
		v = __sysfs_match_string(bch_cache_modes, -1, buf);
		if (v < 0)
			return v;

		if ((unsigned int) v != BDEV_CACHE_MODE(&dc->sb)) {
			SET_BDEV_CACHE_MODE(&dc->sb, v);
			bch_write_bdev_super(dc, NULL);
		}
	}

	if (attr == &sysfs_readahead_cache_policy) {
		v = __sysfs_match_string(bch_reada_cache_policies, -1, buf);
		if (v < 0)
			return v;

		if ((unsigned int) v != dc->cache_readahead_policy)
			dc->cache_readahead_policy = v;
	}

	if (attr == &sysfs_stop_when_cache_set_failed) {
		v = __sysfs_match_string(bch_stop_on_failure_modes, -1, buf);
		if (v < 0)
			return v;

		dc->stop_when_cache_set_failed = v;
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
		add_uevent_var(env, "CACHED_UUID=%pU", dc->sb.uuid);
		add_uevent_var(env, "CACHED_LABEL=%s", buf);
		kobject_uevent_env(&disk_to_dev(dc->disk.disk)->kobj,
				   KOBJ_CHANGE,
				   env->envp);
		kfree(env);
	}

	if (attr == &sysfs_attach) {
		uint8_t		set_uuid[16];

		if (bch_parse_uuid(buf, set_uuid) < 16)
			return -EINVAL;

		v = -ENOENT;
		list_for_each_entry(c, &bch_cache_sets, list) {
			v = bch_cached_dev_attach(dc, c, set_uuid);
			if (!v)
				return size;
		}
		if (v == -ENOENT)
			pr_err("Can't attach %s: cache set not found\n", buf);
		return v;
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

	/* no user space access if system is rebooting */
	if (bcache_is_reboot)
		return -EBUSY;

	mutex_lock(&bch_register_lock);
	size = __cached_dev_store(kobj, attr, buf, size);

	if (attr == &sysfs_writeback_running) {
		/* dc->writeback_running changed in __cached_dev_store() */
		if (IS_ERR_OR_NULL(dc->writeback_thread)) {
			/*
			 * reject setting it to 1 via sysfs if writeback
			 * kthread is not created yet.
			 */
			if (dc->writeback_running) {
				dc->writeback_running = false;
				pr_err("%s: failed to run non-existent writeback thread\n",
						dc->disk.disk->disk_name);
			}
		} else
			/*
			 * writeback kthread will check if dc->writeback_running
			 * is true or false.
			 */
			bch_writeback_queue(dc);
	}

	/*
	 * Only set BCACHE_DEV_WB_RUNNING when cached device attached to
	 * a cache set, otherwise it doesn't make sense.
	 */
	if (attr == &sysfs_writeback_percent)
		if ((dc->disk.c != NULL) &&
		    (!test_and_set_bit(BCACHE_DEV_WB_RUNNING, &dc->disk.flags)))
			schedule_delayed_work(&dc->writeback_rate_update,
				      dc->writeback_rate_update_seconds * HZ);

	mutex_unlock(&bch_register_lock);
	return size;
}

static struct attribute *bch_cached_dev_attrs[] = {
	&sysfs_attach,
	&sysfs_detach,
	&sysfs_stop,
#if 0
	&sysfs_data_csum,
#endif
	&sysfs_cache_mode,
	&sysfs_readahead_cache_policy,
	&sysfs_stop_when_cache_set_failed,
	&sysfs_writeback_metadata,
	&sysfs_writeback_running,
	&sysfs_writeback_delay,
	&sysfs_writeback_percent,
	&sysfs_writeback_rate,
	&sysfs_writeback_consider_fragment,
	&sysfs_writeback_rate_update_seconds,
	&sysfs_writeback_rate_i_term_inverse,
	&sysfs_writeback_rate_p_term_inverse,
	&sysfs_writeback_rate_fp_term_low,
	&sysfs_writeback_rate_fp_term_mid,
	&sysfs_writeback_rate_fp_term_high,
	&sysfs_writeback_rate_minimum,
	&sysfs_writeback_rate_debug,
	&sysfs_io_errors,
	&sysfs_io_error_limit,
	&sysfs_io_disable,
	&sysfs_dirty_data,
	&sysfs_stripe_size,
	&sysfs_partial_stripes_expensive,
	&sysfs_sequential_cutoff,
	&sysfs_clear_stats,
	&sysfs_running,
	&sysfs_state,
	&sysfs_label,
#ifdef CONFIG_BCACHE_DEBUG
	&sysfs_verify,
	&sysfs_bypass_torture_test,
#endif
	&sysfs_backing_dev_name,
	&sysfs_backing_dev_uuid,
	NULL
};
ATTRIBUTE_GROUPS(bch_cached_dev);
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

	/* no user space access if system is rebooting */
	if (bcache_is_reboot)
		return -EBUSY;

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

static struct attribute *bch_flash_dev_attrs[] = {
	&sysfs_unregister,
#if 0
	&sysfs_data_csum,
#endif
	&sysfs_label,
	&sysfs_size,
	NULL
};
ATTRIBUTE_GROUPS(bch_flash_dev);
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

static unsigned int bch_root_usage(struct cache_set *c)
{
	unsigned int bytes = 0;
	struct bkey *k;
	struct btree *b;
	struct btree_iter_stack iter;

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

static unsigned int bch_cache_max_chain(struct cache_set *c)
{
	unsigned int ret = 0;
	struct hlist_head *h;

	mutex_lock(&c->bucket_lock);

	for (h = c->bucket_hash;
	     h < c->bucket_hash + (1 << BUCKET_HASH_BITS);
	     h++) {
		ret = max(ret, hlist_count_nodes(h));
	}

	mutex_unlock(&c->bucket_lock);
	return ret;
}

static unsigned int bch_btree_used(struct cache_set *c)
{
	return div64_u64(c->gc_stats.key_bytes * 100,
			 (c->gc_stats.nodes ?: 1) * btree_bytes(c));
}

static unsigned int bch_average_key_size(struct cache_set *c)
{
	return c->gc_stats.nkeys
		? div64_u64(c->gc_stats.data, c->gc_stats.nkeys)
		: 0;
}

SHOW(__bch_cache_set)
{
	struct cache_set *c = container_of(kobj, struct cache_set, kobj);

	sysfs_print(synchronous,		CACHE_SYNC(&c->cache->sb));
	sysfs_print(journal_delay_ms,		c->journal_delay_ms);
	sysfs_hprint(bucket_size,		bucket_bytes(c->cache));
	sysfs_hprint(block_size,		block_bytes(c->cache));
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

	sysfs_print(reclaim,
		    atomic_long_read(&c->reclaim));

	sysfs_print(reclaimed_journal_buckets,
		    atomic_long_read(&c->reclaimed_journal_buckets));

	sysfs_print(flush_write,
		    atomic_long_read(&c->flush_write));

	sysfs_print(writeback_keys_done,
		    atomic_long_read(&c->writeback_keys_done));
	sysfs_print(writeback_keys_failed,
		    atomic_long_read(&c->writeback_keys_failed));

	if (attr == &sysfs_errors)
		return bch_snprint_string_list(buf, PAGE_SIZE, error_actions,
					       c->on_error);

	/* See count_io_errors for why 88 */
	sysfs_print(io_error_halflife,	c->error_decay * 88);
	sysfs_print(io_error_limit,	c->error_limit);

	sysfs_hprint(congested,
		     ((uint64_t) bch_get_congested(c)) << 9);
	sysfs_print(congested_read_threshold_us,
		    c->congested_read_threshold_us);
	sysfs_print(congested_write_threshold_us,
		    c->congested_write_threshold_us);

	sysfs_print(cutoff_writeback, bch_cutoff_writeback);
	sysfs_print(cutoff_writeback_sync, bch_cutoff_writeback_sync);

	sysfs_print(active_journal_entries,	fifo_used(&c->journal.pin));
	sysfs_printf(verify,			"%i", c->verify);
	sysfs_printf(key_merging_disabled,	"%i", c->key_merging_disabled);
	sysfs_printf(expensive_debug_checks,
		     "%i", c->expensive_debug_checks);
	sysfs_printf(gc_always_rewrite,		"%i", c->gc_always_rewrite);
	sysfs_printf(btree_shrinker_disabled,	"%i", c->shrinker_disabled);
	sysfs_printf(copy_gc_enabled,		"%i", c->copy_gc_enabled);
	sysfs_printf(idle_max_writeback_rate,	"%i",
		     c->idle_max_writeback_rate_enabled);
	sysfs_printf(gc_after_writeback,	"%i", c->gc_after_writeback);
	sysfs_printf(io_disable,		"%i",
		     test_bit(CACHE_SET_IO_DISABLE, &c->flags));

	if (attr == &sysfs_bset_tree_stats)
		return bch_bset_print_stats(c, buf);

	if (attr == &sysfs_feature_compat)
		return bch_print_cache_set_feature_compat(c, buf, PAGE_SIZE);
	if (attr == &sysfs_feature_ro_compat)
		return bch_print_cache_set_feature_ro_compat(c, buf, PAGE_SIZE);
	if (attr == &sysfs_feature_incompat)
		return bch_print_cache_set_feature_incompat(c, buf, PAGE_SIZE);

	return 0;
}
SHOW_LOCKED(bch_cache_set)

STORE(__bch_cache_set)
{
	struct cache_set *c = container_of(kobj, struct cache_set, kobj);
	ssize_t v;

	/* no user space access if system is rebooting */
	if (bcache_is_reboot)
		return -EBUSY;

	if (attr == &sysfs_unregister)
		bch_cache_set_unregister(c);

	if (attr == &sysfs_stop)
		bch_cache_set_stop(c);

	if (attr == &sysfs_synchronous) {
		bool sync = strtoul_or_return(buf);

		if (sync != CACHE_SYNC(&c->cache->sb)) {
			SET_CACHE_SYNC(&c->cache->sb, sync);
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

	if (attr == &sysfs_trigger_gc)
		force_wake_up_gc(c);

	if (attr == &sysfs_prune_cache) {
		struct shrink_control sc;

		sc.gfp_mask = GFP_KERNEL;
		sc.nr_to_scan = strtoul_or_return(buf);
		if (c->shrink)
			c->shrink->scan_objects(c->shrink, &sc);
	}

	sysfs_strtoul_clamp(congested_read_threshold_us,
			    c->congested_read_threshold_us,
			    0, UINT_MAX);
	sysfs_strtoul_clamp(congested_write_threshold_us,
			    c->congested_write_threshold_us,
			    0, UINT_MAX);

	if (attr == &sysfs_errors) {
		v = __sysfs_match_string(error_actions, -1, buf);
		if (v < 0)
			return v;

		c->on_error = v;
	}

	sysfs_strtoul_clamp(io_error_limit, c->error_limit, 0, UINT_MAX);

	/* See count_io_errors() for why 88 */
	if (attr == &sysfs_io_error_halflife) {
		unsigned long v = 0;
		ssize_t ret;

		ret = strtoul_safe_clamp(buf, v, 0, UINT_MAX);
		if (!ret) {
			c->error_decay = v / 88;
			return size;
		}
		return ret;
	}

	if (attr == &sysfs_io_disable) {
		v = strtoul_or_return(buf);
		if (v) {
			if (test_and_set_bit(CACHE_SET_IO_DISABLE,
					     &c->flags))
				pr_warn("CACHE_SET_IO_DISABLE already set\n");
		} else {
			if (!test_and_clear_bit(CACHE_SET_IO_DISABLE,
						&c->flags))
				pr_warn("CACHE_SET_IO_DISABLE already cleared\n");
		}
	}

	sysfs_strtoul_clamp(journal_delay_ms,
			    c->journal_delay_ms,
			    0, USHRT_MAX);
	sysfs_strtoul_bool(verify,		c->verify);
	sysfs_strtoul_bool(key_merging_disabled, c->key_merging_disabled);
	sysfs_strtoul(expensive_debug_checks,	c->expensive_debug_checks);
	sysfs_strtoul_bool(gc_always_rewrite,	c->gc_always_rewrite);
	sysfs_strtoul_bool(btree_shrinker_disabled, c->shrinker_disabled);
	sysfs_strtoul_bool(copy_gc_enabled,	c->copy_gc_enabled);
	sysfs_strtoul_bool(idle_max_writeback_rate,
			   c->idle_max_writeback_rate_enabled);

	/*
	 * write gc_after_writeback here may overwrite an already set
	 * BCH_DO_AUTO_GC, it doesn't matter because this flag will be
	 * set in next chance.
	 */
	sysfs_strtoul_clamp(gc_after_writeback, c->gc_after_writeback, 0, 1);

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

	/* no user space access if system is rebooting */
	if (bcache_is_reboot)
		return -EBUSY;

	return bch_cache_set_store(&c->kobj, attr, buf, size);
}

static void bch_cache_set_internal_release(struct kobject *k)
{
}

static struct attribute *bch_cache_set_attrs[] = {
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
ATTRIBUTE_GROUPS(bch_cache_set);
KTYPE(bch_cache_set);

static struct attribute *bch_cache_set_internal_attrs[] = {
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
	&sysfs_reclaim,
	&sysfs_reclaimed_journal_buckets,
	&sysfs_flush_write,
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
	&sysfs_idle_max_writeback_rate,
	&sysfs_gc_after_writeback,
	&sysfs_io_disable,
	&sysfs_cutoff_writeback,
	&sysfs_cutoff_writeback_sync,
	&sysfs_feature_compat,
	&sysfs_feature_ro_compat,
	&sysfs_feature_incompat,
	NULL
};
ATTRIBUTE_GROUPS(bch_cache_set_internal);
KTYPE(bch_cache_set_internal);

static int __bch_cache_cmp(const void *l, const void *r)
{
	cond_resched();
	return *((uint16_t *)r) - *((uint16_t *)l);
}

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
		struct bucket *b;
		size_t n = ca->sb.nbuckets, i;
		size_t unused = 0, available = 0, dirty = 0, meta = 0;
		uint64_t sum = 0;
		/* Compute 31 quantiles */
		uint16_t q[31], *p, *cached;
		ssize_t ret;

		cached = p = vmalloc(array_size(sizeof(uint16_t),
						ca->sb.nbuckets));
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

		sort(p, n, sizeof(uint16_t), __bch_cache_cmp, NULL);

		while (n &&
		       !cached[n - 1])
			--n;

		while (cached < p + n &&
		       *cached == BTREE_PRIO) {
			cached++;
			n--;
		}

		for (i = 0; i < n; i++)
			sum += INITIAL_PRIO - cached[i];

		if (n)
			sum = div64_u64(sum, n);

		for (i = 0; i < ARRAY_SIZE(q); i++)
			q[i] = INITIAL_PRIO - cached[n * (i + 1) /
				(ARRAY_SIZE(q) + 1)];

		vfree(p);

		ret = sysfs_emit(buf,
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
			ret += sysfs_emit_at(buf, ret, "%u ", q[i]);
		ret--;

		ret += sysfs_emit_at(buf, ret, "]\n");

		return ret;
	}

	return 0;
}
SHOW_LOCKED(bch_cache)

STORE(__bch_cache)
{
	struct cache *ca = container_of(kobj, struct cache, kobj);
	ssize_t v;

	/* no user space access if system is rebooting */
	if (bcache_is_reboot)
		return -EBUSY;

	if (attr == &sysfs_discard) {
		bool v = strtoul_or_return(buf);

		if (bdev_max_discard_sectors(ca->bdev))
			ca->discard = v;

		if (v != CACHE_DISCARD(&ca->sb)) {
			SET_CACHE_DISCARD(&ca->sb, v);
			bcache_write_super(ca->set);
		}
	}

	if (attr == &sysfs_cache_replacement_policy) {
		v = __sysfs_match_string(cache_replacement_policies, -1, buf);
		if (v < 0)
			return v;

		if ((unsigned int) v != CACHE_REPLACEMENT(&ca->sb)) {
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

static struct attribute *bch_cache_attrs[] = {
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
ATTRIBUTE_GROUPS(bch_cache);
KTYPE(bch_cache);
