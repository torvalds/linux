/*
 * Copyright (C) 2006-2009 Red Hat, Inc.
 *
 * This file is released under the LGPL.
 */

#include <linux/bio.h>
#include <linux/dm-dirty-log.h>
#include <linux/device-mapper.h>
#include <linux/dm-log-userspace.h>

#include "dm-log-userspace-transfer.h"

struct flush_entry {
	int type;
	region_t region;
	struct list_head list;
};

struct log_c {
	struct dm_target *ti;
	uint32_t region_size;
	region_t region_count;
	uint64_t luid;
	char uuid[DM_UUID_LEN];

	char *usr_argv_str;
	uint32_t usr_argc;

	/*
	 * in_sync_hint gets set when doing is_remote_recovering.  It
	 * represents the first region that needs recovery.  IOW, the
	 * first zero bit of sync_bits.  This can be useful for to limit
	 * traffic for calls like is_remote_recovering and get_resync_work,
	 * but be take care in its use for anything else.
	 */
	uint64_t in_sync_hint;

	spinlock_t flush_lock;
	struct list_head flush_list;  /* only for clear and mark requests */
};

static mempool_t *flush_entry_pool;

static void *flush_entry_alloc(gfp_t gfp_mask, void *pool_data)
{
	return kmalloc(sizeof(struct flush_entry), gfp_mask);
}

static void flush_entry_free(void *element, void *pool_data)
{
	kfree(element);
}

static int userspace_do_request(struct log_c *lc, const char *uuid,
				int request_type, char *data, size_t data_size,
				char *rdata, size_t *rdata_size)
{
	int r;

	/*
	 * If the server isn't there, -ESRCH is returned,
	 * and we must keep trying until the server is
	 * restored.
	 */
retry:
	r = dm_consult_userspace(uuid, lc->luid, request_type, data,
				 data_size, rdata, rdata_size);

	if (r != -ESRCH)
		return r;

	DMERR(" Userspace log server not found.");
	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(2*HZ);
		DMWARN("Attempting to contact userspace log server...");
		r = dm_consult_userspace(uuid, lc->luid, DM_ULOG_CTR,
					 lc->usr_argv_str,
					 strlen(lc->usr_argv_str) + 1,
					 NULL, NULL);
		if (!r)
			break;
	}
	DMINFO("Reconnected to userspace log server... DM_ULOG_CTR complete");
	r = dm_consult_userspace(uuid, lc->luid, DM_ULOG_RESUME, NULL,
				 0, NULL, NULL);
	if (!r)
		goto retry;

	DMERR("Error trying to resume userspace log: %d", r);

	return -ESRCH;
}

static int build_constructor_string(struct dm_target *ti,
				    unsigned argc, char **argv,
				    char **ctr_str)
{
	int i, str_size;
	char *str = NULL;

	*ctr_str = NULL;

	for (i = 0, str_size = 0; i < argc; i++)
		str_size += strlen(argv[i]) + 1; /* +1 for space between args */

	str_size += 20; /* Max number of chars in a printed u64 number */

	str = kzalloc(str_size, GFP_KERNEL);
	if (!str) {
		DMWARN("Unable to allocate memory for constructor string");
		return -ENOMEM;
	}

	str_size = sprintf(str, "%llu", (unsigned long long)ti->len);
	for (i = 0; i < argc; i++)
		str_size += sprintf(str + str_size, " %s", argv[i]);

	*ctr_str = str;
	return str_size;
}

/*
 * userspace_ctr
 *
 * argv contains:
 *	<UUID> <other args>
 * Where 'other args' is the userspace implementation specific log
 * arguments.  An example might be:
 *	<UUID> clustered_disk <arg count> <log dev> <region_size> [[no]sync]
 *
 * So, this module will strip off the <UUID> for identification purposes
 * when communicating with userspace about a log; but will pass on everything
 * else.
 */
static int userspace_ctr(struct dm_dirty_log *log, struct dm_target *ti,
			 unsigned argc, char **argv)
{
	int r = 0;
	int str_size;
	char *ctr_str = NULL;
	struct log_c *lc = NULL;
	uint64_t rdata;
	size_t rdata_size = sizeof(rdata);

	if (argc < 3) {
		DMWARN("Too few arguments to userspace dirty log");
		return -EINVAL;
	}

	lc = kmalloc(sizeof(*lc), GFP_KERNEL);
	if (!lc) {
		DMWARN("Unable to allocate userspace log context.");
		return -ENOMEM;
	}

	/* The ptr value is sufficient for local unique id */
	lc->luid = (uint64_t)lc;

	lc->ti = ti;

	if (strlen(argv[0]) > (DM_UUID_LEN - 1)) {
		DMWARN("UUID argument too long.");
		kfree(lc);
		return -EINVAL;
	}

	strncpy(lc->uuid, argv[0], DM_UUID_LEN);
	spin_lock_init(&lc->flush_lock);
	INIT_LIST_HEAD(&lc->flush_list);

	str_size = build_constructor_string(ti, argc - 1, argv + 1, &ctr_str);
	if (str_size < 0) {
		kfree(lc);
		return str_size;
	}

	/* Send table string */
	r = dm_consult_userspace(lc->uuid, lc->luid, DM_ULOG_CTR,
				 ctr_str, str_size, NULL, NULL);

	if (r == -ESRCH) {
		DMERR("Userspace log server not found");
		goto out;
	}

	/* Since the region size does not change, get it now */
	rdata_size = sizeof(rdata);
	r = dm_consult_userspace(lc->uuid, lc->luid, DM_ULOG_GET_REGION_SIZE,
				 NULL, 0, (char *)&rdata, &rdata_size);

	if (r) {
		DMERR("Failed to get region size of dirty log");
		goto out;
	}

	lc->region_size = (uint32_t)rdata;
	lc->region_count = dm_sector_div_up(ti->len, lc->region_size);

out:
	if (r) {
		kfree(lc);
		kfree(ctr_str);
	} else {
		lc->usr_argv_str = ctr_str;
		lc->usr_argc = argc;
		log->context = lc;
	}

	return r;
}

static void userspace_dtr(struct dm_dirty_log *log)
{
	int r;
	struct log_c *lc = log->context;

	r = dm_consult_userspace(lc->uuid, lc->luid, DM_ULOG_DTR,
				 NULL, 0,
				 NULL, NULL);

	kfree(lc->usr_argv_str);
	kfree(lc);

	return;
}

static int userspace_presuspend(struct dm_dirty_log *log)
{
	int r;
	struct log_c *lc = log->context;

	r = dm_consult_userspace(lc->uuid, lc->luid, DM_ULOG_PRESUSPEND,
				 NULL, 0,
				 NULL, NULL);

	return r;
}

static int userspace_postsuspend(struct dm_dirty_log *log)
{
	int r;
	struct log_c *lc = log->context;

	r = dm_consult_userspace(lc->uuid, lc->luid, DM_ULOG_POSTSUSPEND,
				 NULL, 0,
				 NULL, NULL);

	return r;
}

static int userspace_resume(struct dm_dirty_log *log)
{
	int r;
	struct log_c *lc = log->context;

	lc->in_sync_hint = 0;
	r = dm_consult_userspace(lc->uuid, lc->luid, DM_ULOG_RESUME,
				 NULL, 0,
				 NULL, NULL);

	return r;
}

static uint32_t userspace_get_region_size(struct dm_dirty_log *log)
{
	struct log_c *lc = log->context;

	return lc->region_size;
}

/*
 * userspace_is_clean
 *
 * Check whether a region is clean.  If there is any sort of
 * failure when consulting the server, we return not clean.
 *
 * Returns: 1 if clean, 0 otherwise
 */
static int userspace_is_clean(struct dm_dirty_log *log, region_t region)
{
	int r;
	uint64_t region64 = (uint64_t)region;
	int64_t is_clean;
	size_t rdata_size;
	struct log_c *lc = log->context;

	rdata_size = sizeof(is_clean);
	r = userspace_do_request(lc, lc->uuid, DM_ULOG_IS_CLEAN,
				 (char *)&region64, sizeof(region64),
				 (char *)&is_clean, &rdata_size);

	return (r) ? 0 : (int)is_clean;
}

/*
 * userspace_in_sync
 *
 * Check if the region is in-sync.  If there is any sort
 * of failure when consulting the server, we assume that
 * the region is not in sync.
 *
 * If 'can_block' is set, return immediately
 *
 * Returns: 1 if in-sync, 0 if not-in-sync, -EWOULDBLOCK
 */
static int userspace_in_sync(struct dm_dirty_log *log, region_t region,
			     int can_block)
{
	int r;
	uint64_t region64 = region;
	int64_t in_sync;
	size_t rdata_size;
	struct log_c *lc = log->context;

	/*
	 * We can never respond directly - even if in_sync_hint is
	 * set.  This is because another machine could see a device
	 * failure and mark the region out-of-sync.  If we don't go
	 * to userspace to ask, we might think the region is in-sync
	 * and allow a read to pick up data that is stale.  (This is
	 * very unlikely if a device actually fails; but it is very
	 * likely if a connection to one device from one machine fails.)
	 *
	 * There still might be a problem if the mirror caches the region
	 * state as in-sync... but then this call would not be made.  So,
	 * that is a mirror problem.
	 */
	if (!can_block)
		return -EWOULDBLOCK;

	rdata_size = sizeof(in_sync);
	r = userspace_do_request(lc, lc->uuid, DM_ULOG_IN_SYNC,
				 (char *)&region64, sizeof(region64),
				 (char *)&in_sync, &rdata_size);
	return (r) ? 0 : (int)in_sync;
}

/*
 * userspace_flush
 *
 * This function is ok to block.
 * The flush happens in two stages.  First, it sends all
 * clear/mark requests that are on the list.  Then it
 * tells the server to commit them.  This gives the
 * server a chance to optimise the commit, instead of
 * doing it for every request.
 *
 * Additionally, we could implement another thread that
 * sends the requests up to the server - reducing the
 * load on flush.  Then the flush would have less in
 * the list and be responsible for the finishing commit.
 *
 * Returns: 0 on success, < 0 on failure
 */
static int userspace_flush(struct dm_dirty_log *log)
{
	int r = 0;
	unsigned long flags;
	struct log_c *lc = log->context;
	LIST_HEAD(flush_list);
	struct flush_entry *fe, *tmp_fe;

	spin_lock_irqsave(&lc->flush_lock, flags);
	list_splice_init(&lc->flush_list, &flush_list);
	spin_unlock_irqrestore(&lc->flush_lock, flags);

	if (list_empty(&flush_list))
		return 0;

	/*
	 * FIXME: Count up requests, group request types,
	 * allocate memory to stick all requests in and
	 * send to server in one go.  Failing the allocation,
	 * do it one by one.
	 */

	list_for_each_entry(fe, &flush_list, list) {
		r = userspace_do_request(lc, lc->uuid, fe->type,
					 (char *)&fe->region,
					 sizeof(fe->region),
					 NULL, NULL);
		if (r)
			goto fail;
	}

	r = userspace_do_request(lc, lc->uuid, DM_ULOG_FLUSH,
				 NULL, 0, NULL, NULL);

fail:
	/*
	 * We can safely remove these entries, even if failure.
	 * Calling code will receive an error and will know that
	 * the log facility has failed.
	 */
	list_for_each_entry_safe(fe, tmp_fe, &flush_list, list) {
		list_del(&fe->list);
		mempool_free(fe, flush_entry_pool);
	}

	if (r)
		dm_table_event(lc->ti->table);

	return r;
}

/*
 * userspace_mark_region
 *
 * This function should avoid blocking unless absolutely required.
 * (Memory allocation is valid for blocking.)
 */
static void userspace_mark_region(struct dm_dirty_log *log, region_t region)
{
	unsigned long flags;
	struct log_c *lc = log->context;
	struct flush_entry *fe;

	/* Wait for an allocation, but _never_ fail */
	fe = mempool_alloc(flush_entry_pool, GFP_NOIO);
	BUG_ON(!fe);

	spin_lock_irqsave(&lc->flush_lock, flags);
	fe->type = DM_ULOG_MARK_REGION;
	fe->region = region;
	list_add(&fe->list, &lc->flush_list);
	spin_unlock_irqrestore(&lc->flush_lock, flags);

	return;
}

/*
 * userspace_clear_region
 *
 * This function must not block.
 * So, the alloc can't block.  In the worst case, it is ok to
 * fail.  It would simply mean we can't clear the region.
 * Does nothing to current sync context, but does mean
 * the region will be re-sync'ed on a reload of the mirror
 * even though it is in-sync.
 */
static void userspace_clear_region(struct dm_dirty_log *log, region_t region)
{
	unsigned long flags;
	struct log_c *lc = log->context;
	struct flush_entry *fe;

	/*
	 * If we fail to allocate, we skip the clearing of
	 * the region.  This doesn't hurt us in any way, except
	 * to cause the region to be resync'ed when the
	 * device is activated next time.
	 */
	fe = mempool_alloc(flush_entry_pool, GFP_ATOMIC);
	if (!fe) {
		DMERR("Failed to allocate memory to clear region.");
		return;
	}

	spin_lock_irqsave(&lc->flush_lock, flags);
	fe->type = DM_ULOG_CLEAR_REGION;
	fe->region = region;
	list_add(&fe->list, &lc->flush_list);
	spin_unlock_irqrestore(&lc->flush_lock, flags);

	return;
}

/*
 * userspace_get_resync_work
 *
 * Get a region that needs recovery.  It is valid to return
 * an error for this function.
 *
 * Returns: 1 if region filled, 0 if no work, <0 on error
 */
static int userspace_get_resync_work(struct dm_dirty_log *log, region_t *region)
{
	int r;
	size_t rdata_size;
	struct log_c *lc = log->context;
	struct {
		int64_t i; /* 64-bit for mix arch compatibility */
		region_t r;
	} pkg;

	if (lc->in_sync_hint >= lc->region_count)
		return 0;

	rdata_size = sizeof(pkg);
	r = userspace_do_request(lc, lc->uuid, DM_ULOG_GET_RESYNC_WORK,
				 NULL, 0,
				 (char *)&pkg, &rdata_size);

	*region = pkg.r;
	return (r) ? r : (int)pkg.i;
}

/*
 * userspace_set_region_sync
 *
 * Set the sync status of a given region.  This function
 * must not fail.
 */
static void userspace_set_region_sync(struct dm_dirty_log *log,
				      region_t region, int in_sync)
{
	int r;
	struct log_c *lc = log->context;
	struct {
		region_t r;
		int64_t i;
	} pkg;

	pkg.r = region;
	pkg.i = (int64_t)in_sync;

	r = userspace_do_request(lc, lc->uuid, DM_ULOG_SET_REGION_SYNC,
				 (char *)&pkg, sizeof(pkg),
				 NULL, NULL);

	/*
	 * It would be nice to be able to report failures.
	 * However, it is easy emough to detect and resolve.
	 */
	return;
}

/*
 * userspace_get_sync_count
 *
 * If there is any sort of failure when consulting the server,
 * we assume that the sync count is zero.
 *
 * Returns: sync count on success, 0 on failure
 */
static region_t userspace_get_sync_count(struct dm_dirty_log *log)
{
	int r;
	size_t rdata_size;
	uint64_t sync_count;
	struct log_c *lc = log->context;

	rdata_size = sizeof(sync_count);
	r = userspace_do_request(lc, lc->uuid, DM_ULOG_GET_SYNC_COUNT,
				 NULL, 0,
				 (char *)&sync_count, &rdata_size);

	if (r)
		return 0;

	if (sync_count >= lc->region_count)
		lc->in_sync_hint = lc->region_count;

	return (region_t)sync_count;
}

/*
 * userspace_status
 *
 * Returns: amount of space consumed
 */
static int userspace_status(struct dm_dirty_log *log, status_type_t status_type,
			    char *result, unsigned maxlen)
{
	int r = 0;
	char *table_args;
	size_t sz = (size_t)maxlen;
	struct log_c *lc = log->context;

	switch (status_type) {
	case STATUSTYPE_INFO:
		r = userspace_do_request(lc, lc->uuid, DM_ULOG_STATUS_INFO,
					 NULL, 0,
					 result, &sz);

		if (r) {
			sz = 0;
			DMEMIT("%s 1 COM_FAILURE", log->type->name);
		}
		break;
	case STATUSTYPE_TABLE:
		sz = 0;
		table_args = strstr(lc->usr_argv_str, " ");
		BUG_ON(!table_args); /* There will always be a ' ' */
		table_args++;

		DMEMIT("%s %u %s %s ", log->type->name, lc->usr_argc,
		       lc->uuid, table_args);
		break;
	}
	return (r) ? 0 : (int)sz;
}

/*
 * userspace_is_remote_recovering
 *
 * Returns: 1 if region recovering, 0 otherwise
 */
static int userspace_is_remote_recovering(struct dm_dirty_log *log,
					  region_t region)
{
	int r;
	uint64_t region64 = region;
	struct log_c *lc = log->context;
	static unsigned long long limit;
	struct {
		int64_t is_recovering;
		uint64_t in_sync_hint;
	} pkg;
	size_t rdata_size = sizeof(pkg);

	/*
	 * Once the mirror has been reported to be in-sync,
	 * it will never again ask for recovery work.  So,
	 * we can safely say there is not a remote machine
	 * recovering if the device is in-sync.  (in_sync_hint
	 * must be reset at resume time.)
	 */
	if (region < lc->in_sync_hint)
		return 0;
	else if (jiffies < limit)
		return 1;

	limit = jiffies + (HZ / 4);
	r = userspace_do_request(lc, lc->uuid, DM_ULOG_IS_REMOTE_RECOVERING,
				 (char *)&region64, sizeof(region64),
				 (char *)&pkg, &rdata_size);
	if (r)
		return 1;

	lc->in_sync_hint = pkg.in_sync_hint;

	return (int)pkg.is_recovering;
}

static struct dm_dirty_log_type _userspace_type = {
	.name = "userspace",
	.module = THIS_MODULE,
	.ctr = userspace_ctr,
	.dtr = userspace_dtr,
	.presuspend = userspace_presuspend,
	.postsuspend = userspace_postsuspend,
	.resume = userspace_resume,
	.get_region_size = userspace_get_region_size,
	.is_clean = userspace_is_clean,
	.in_sync = userspace_in_sync,
	.flush = userspace_flush,
	.mark_region = userspace_mark_region,
	.clear_region = userspace_clear_region,
	.get_resync_work = userspace_get_resync_work,
	.set_region_sync = userspace_set_region_sync,
	.get_sync_count = userspace_get_sync_count,
	.status = userspace_status,
	.is_remote_recovering = userspace_is_remote_recovering,
};

static int __init userspace_dirty_log_init(void)
{
	int r = 0;

	flush_entry_pool = mempool_create(100, flush_entry_alloc,
					  flush_entry_free, NULL);

	if (!flush_entry_pool) {
		DMWARN("Unable to create flush_entry_pool:  No memory.");
		return -ENOMEM;
	}

	r = dm_ulog_tfr_init();
	if (r) {
		DMWARN("Unable to initialize userspace log communications");
		mempool_destroy(flush_entry_pool);
		return r;
	}

	r = dm_dirty_log_type_register(&_userspace_type);
	if (r) {
		DMWARN("Couldn't register userspace dirty log type");
		dm_ulog_tfr_exit();
		mempool_destroy(flush_entry_pool);
		return r;
	}

	DMINFO("version 1.0.0 loaded");
	return 0;
}

static void __exit userspace_dirty_log_exit(void)
{
	dm_dirty_log_type_unregister(&_userspace_type);
	dm_ulog_tfr_exit();
	mempool_destroy(flush_entry_pool);

	DMINFO("version 1.0.0 unloaded");
	return;
}

module_init(userspace_dirty_log_init);
module_exit(userspace_dirty_log_exit);

MODULE_DESCRIPTION(DM_NAME " userspace dirty log link");
MODULE_AUTHOR("Jonathan Brassow <dm-devel@redhat.com>");
MODULE_LICENSE("GPL");
