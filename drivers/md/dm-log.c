/*
 * Copyright (C) 2003 Sistina Software
 * Copyright (C) 2004-2008 Red Hat, Inc. All rights reserved.
 *
 * This file is released under the LGPL.
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/dm-io.h>
#include <linux/dm-dirty-log.h>

#include <linux/device-mapper.h>

#define DM_MSG_PREFIX "dirty region log"

static LIST_HEAD(_log_types);
static DEFINE_SPINLOCK(_lock);

static struct dm_dirty_log_type *__find_dirty_log_type(const char *name)
{
	struct dm_dirty_log_type *log_type;

	list_for_each_entry(log_type, &_log_types, list)
		if (!strcmp(name, log_type->name))
			return log_type;

	return NULL;
}

static struct dm_dirty_log_type *_get_dirty_log_type(const char *name)
{
	struct dm_dirty_log_type *log_type;

	spin_lock(&_lock);

	log_type = __find_dirty_log_type(name);
	if (log_type && !try_module_get(log_type->module))
		log_type = NULL;

	spin_unlock(&_lock);

	return log_type;
}

/*
 * get_type
 * @type_name
 *
 * Attempt to retrieve the dm_dirty_log_type by name.  If not already
 * available, attempt to load the appropriate module.
 *
 * Log modules are named "dm-log-" followed by the 'type_name'.
 * Modules may contain multiple types.
 * This function will first try the module "dm-log-<type_name>",
 * then truncate 'type_name' on the last '-' and try again.
 *
 * For example, if type_name was "clustered-disk", it would search
 * 'dm-log-clustered-disk' then 'dm-log-clustered'.
 *
 * Returns: dirty_log_type* on success, NULL on failure
 */
static struct dm_dirty_log_type *get_type(const char *type_name)
{
	char *p, *type_name_dup;
	struct dm_dirty_log_type *log_type;

	if (!type_name)
		return NULL;

	log_type = _get_dirty_log_type(type_name);
	if (log_type)
		return log_type;

	type_name_dup = kstrdup(type_name, GFP_KERNEL);
	if (!type_name_dup) {
		DMWARN("No memory left to attempt log module load for \"%s\"",
		       type_name);
		return NULL;
	}

	while (request_module("dm-log-%s", type_name_dup) ||
	       !(log_type = _get_dirty_log_type(type_name))) {
		p = strrchr(type_name_dup, '-');
		if (!p)
			break;
		p[0] = '\0';
	}

	if (!log_type)
		DMWARN("Module for logging type \"%s\" not found.", type_name);

	kfree(type_name_dup);

	return log_type;
}

static void put_type(struct dm_dirty_log_type *type)
{
	if (!type)
		return;

	spin_lock(&_lock);
	if (!__find_dirty_log_type(type->name))
		goto out;

	module_put(type->module);

out:
	spin_unlock(&_lock);
}

int dm_dirty_log_type_register(struct dm_dirty_log_type *type)
{
	int r = 0;

	spin_lock(&_lock);
	if (!__find_dirty_log_type(type->name))
		list_add(&type->list, &_log_types);
	else
		r = -EEXIST;
	spin_unlock(&_lock);

	return r;
}
EXPORT_SYMBOL(dm_dirty_log_type_register);

int dm_dirty_log_type_unregister(struct dm_dirty_log_type *type)
{
	spin_lock(&_lock);

	if (!__find_dirty_log_type(type->name)) {
		spin_unlock(&_lock);
		return -EINVAL;
	}

	list_del(&type->list);

	spin_unlock(&_lock);

	return 0;
}
EXPORT_SYMBOL(dm_dirty_log_type_unregister);

struct dm_dirty_log *dm_dirty_log_create(const char *type_name,
			struct dm_target *ti,
			int (*flush_callback_fn)(struct dm_target *ti),
			unsigned int argc, char **argv)
{
	struct dm_dirty_log_type *type;
	struct dm_dirty_log *log;

	log = kmalloc(sizeof(*log), GFP_KERNEL);
	if (!log)
		return NULL;

	type = get_type(type_name);
	if (!type) {
		kfree(log);
		return NULL;
	}

	log->flush_callback_fn = flush_callback_fn;
	log->type = type;
	if (type->ctr(log, ti, argc, argv)) {
		kfree(log);
		put_type(type);
		return NULL;
	}

	return log;
}
EXPORT_SYMBOL(dm_dirty_log_create);

void dm_dirty_log_destroy(struct dm_dirty_log *log)
{
	log->type->dtr(log);
	put_type(log->type);
	kfree(log);
}
EXPORT_SYMBOL(dm_dirty_log_destroy);

/*-----------------------------------------------------------------
 * Persistent and core logs share a lot of their implementation.
 * FIXME: need a reload method to be called from a resume
 *---------------------------------------------------------------*/
/*
 * Magic for persistent mirrors: "MiRr"
 */
#define MIRROR_MAGIC 0x4D695272

/*
 * The on-disk version of the metadata.
 */
#define MIRROR_DISK_VERSION 2
#define LOG_OFFSET 2

struct log_header_disk {
	__le32 magic;

	/*
	 * Simple, incrementing version. no backward
	 * compatibility.
	 */
	__le32 version;
	__le64 nr_regions;
} __packed;

struct log_header_core {
	uint32_t magic;
	uint32_t version;
	uint64_t nr_regions;
};

struct log_c {
	struct dm_target *ti;
	int touched_dirtied;
	int touched_cleaned;
	int flush_failed;
	uint32_t region_size;
	unsigned int region_count;
	region_t sync_count;

	unsigned bitset_uint32_count;
	uint32_t *clean_bits;
	uint32_t *sync_bits;
	uint32_t *recovering_bits;	/* FIXME: this seems excessive */

	int sync_search;

	/* Resync flag */
	enum sync {
		DEFAULTSYNC,	/* Synchronize if necessary */
		NOSYNC,		/* Devices known to be already in sync */
		FORCESYNC,	/* Force a sync to happen */
	} sync;

	struct dm_io_request io_req;

	/*
	 * Disk log fields
	 */
	int log_dev_failed;
	int log_dev_flush_failed;
	struct dm_dev *log_dev;
	struct log_header_core header;

	struct dm_io_region header_location;
	struct log_header_disk *disk_header;
};

/*
 * The touched member needs to be updated every time we access
 * one of the bitsets.
 */
static inline int log_test_bit(uint32_t *bs, unsigned bit)
{
	return test_bit_le(bit, bs) ? 1 : 0;
}

static inline void log_set_bit(struct log_c *l,
			       uint32_t *bs, unsigned bit)
{
	__set_bit_le(bit, bs);
	l->touched_cleaned = 1;
}

static inline void log_clear_bit(struct log_c *l,
				 uint32_t *bs, unsigned bit)
{
	__clear_bit_le(bit, bs);
	l->touched_dirtied = 1;
}

/*----------------------------------------------------------------
 * Header IO
 *--------------------------------------------------------------*/
static void header_to_disk(struct log_header_core *core, struct log_header_disk *disk)
{
	disk->magic = cpu_to_le32(core->magic);
	disk->version = cpu_to_le32(core->version);
	disk->nr_regions = cpu_to_le64(core->nr_regions);
}

static void header_from_disk(struct log_header_core *core, struct log_header_disk *disk)
{
	core->magic = le32_to_cpu(disk->magic);
	core->version = le32_to_cpu(disk->version);
	core->nr_regions = le64_to_cpu(disk->nr_regions);
}

static int rw_header(struct log_c *lc, int op)
{
	lc->io_req.bi_op = op;
	lc->io_req.bi_op_flags = 0;

	return dm_io(&lc->io_req, 1, &lc->header_location, NULL);
}

static int flush_header(struct log_c *lc)
{
	struct dm_io_region null_location = {
		.bdev = lc->header_location.bdev,
		.sector = 0,
		.count = 0,
	};

	lc->io_req.bi_op = REQ_OP_WRITE;
	lc->io_req.bi_op_flags = REQ_PREFLUSH;

	return dm_io(&lc->io_req, 1, &null_location, NULL);
}

static int read_header(struct log_c *log)
{
	int r;

	r = rw_header(log, REQ_OP_READ);
	if (r)
		return r;

	header_from_disk(&log->header, log->disk_header);

	/* New log required? */
	if (log->sync != DEFAULTSYNC || log->header.magic != MIRROR_MAGIC) {
		log->header.magic = MIRROR_MAGIC;
		log->header.version = MIRROR_DISK_VERSION;
		log->header.nr_regions = 0;
	}

#ifdef __LITTLE_ENDIAN
	if (log->header.version == 1)
		log->header.version = 2;
#endif

	if (log->header.version != MIRROR_DISK_VERSION) {
		DMWARN("incompatible disk log version");
		return -EINVAL;
	}

	return 0;
}

static int _check_region_size(struct dm_target *ti, uint32_t region_size)
{
	if (region_size < 2 || region_size > ti->len)
		return 0;

	if (!is_power_of_2(region_size))
		return 0;

	return 1;
}

/*----------------------------------------------------------------
 * core log constructor/destructor
 *
 * argv contains region_size followed optionally by [no]sync
 *--------------------------------------------------------------*/
#define BYTE_SHIFT 3
static int create_log_context(struct dm_dirty_log *log, struct dm_target *ti,
			      unsigned int argc, char **argv,
			      struct dm_dev *dev)
{
	enum sync sync = DEFAULTSYNC;

	struct log_c *lc;
	uint32_t region_size;
	unsigned int region_count;
	size_t bitset_size, buf_size;
	int r;
	char dummy;

	if (argc < 1 || argc > 2) {
		DMWARN("wrong number of arguments to dirty region log");
		return -EINVAL;
	}

	if (argc > 1) {
		if (!strcmp(argv[1], "sync"))
			sync = FORCESYNC;
		else if (!strcmp(argv[1], "nosync"))
			sync = NOSYNC;
		else {
			DMWARN("unrecognised sync argument to "
			       "dirty region log: %s", argv[1]);
			return -EINVAL;
		}
	}

	if (sscanf(argv[0], "%u%c", &region_size, &dummy) != 1 ||
	    !_check_region_size(ti, region_size)) {
		DMWARN("invalid region size %s", argv[0]);
		return -EINVAL;
	}

	region_count = dm_sector_div_up(ti->len, region_size);

	lc = kmalloc(sizeof(*lc), GFP_KERNEL);
	if (!lc) {
		DMWARN("couldn't allocate core log");
		return -ENOMEM;
	}

	lc->ti = ti;
	lc->touched_dirtied = 0;
	lc->touched_cleaned = 0;
	lc->flush_failed = 0;
	lc->region_size = region_size;
	lc->region_count = region_count;
	lc->sync = sync;

	/*
	 * Work out how many "unsigned long"s we need to hold the bitset.
	 */
	bitset_size = dm_round_up(region_count,
				  sizeof(*lc->clean_bits) << BYTE_SHIFT);
	bitset_size >>= BYTE_SHIFT;

	lc->bitset_uint32_count = bitset_size / sizeof(*lc->clean_bits);

	/*
	 * Disk log?
	 */
	if (!dev) {
		lc->clean_bits = vmalloc(bitset_size);
		if (!lc->clean_bits) {
			DMWARN("couldn't allocate clean bitset");
			kfree(lc);
			return -ENOMEM;
		}
		lc->disk_header = NULL;
	} else {
		lc->log_dev = dev;
		lc->log_dev_failed = 0;
		lc->log_dev_flush_failed = 0;
		lc->header_location.bdev = lc->log_dev->bdev;
		lc->header_location.sector = 0;

		/*
		 * Buffer holds both header and bitset.
		 */
		buf_size =
		    dm_round_up((LOG_OFFSET << SECTOR_SHIFT) + bitset_size,
				bdev_logical_block_size(lc->header_location.
							    bdev));

		if (buf_size > bdev_nr_bytes(dev->bdev)) {
			DMWARN("log device %s too small: need %llu bytes",
				dev->name, (unsigned long long)buf_size);
			kfree(lc);
			return -EINVAL;
		}

		lc->header_location.count = buf_size >> SECTOR_SHIFT;

		lc->io_req.mem.type = DM_IO_VMA;
		lc->io_req.notify.fn = NULL;
		lc->io_req.client = dm_io_client_create();
		if (IS_ERR(lc->io_req.client)) {
			r = PTR_ERR(lc->io_req.client);
			DMWARN("couldn't allocate disk io client");
			kfree(lc);
			return r;
		}

		lc->disk_header = vmalloc(buf_size);
		if (!lc->disk_header) {
			DMWARN("couldn't allocate disk log buffer");
			dm_io_client_destroy(lc->io_req.client);
			kfree(lc);
			return -ENOMEM;
		}

		lc->io_req.mem.ptr.vma = lc->disk_header;
		lc->clean_bits = (void *)lc->disk_header +
				 (LOG_OFFSET << SECTOR_SHIFT);
	}

	memset(lc->clean_bits, -1, bitset_size);

	lc->sync_bits = vmalloc(bitset_size);
	if (!lc->sync_bits) {
		DMWARN("couldn't allocate sync bitset");
		if (!dev)
			vfree(lc->clean_bits);
		else
			dm_io_client_destroy(lc->io_req.client);
		vfree(lc->disk_header);
		kfree(lc);
		return -ENOMEM;
	}
	memset(lc->sync_bits, (sync == NOSYNC) ? -1 : 0, bitset_size);
	lc->sync_count = (sync == NOSYNC) ? region_count : 0;

	lc->recovering_bits = vzalloc(bitset_size);
	if (!lc->recovering_bits) {
		DMWARN("couldn't allocate sync bitset");
		vfree(lc->sync_bits);
		if (!dev)
			vfree(lc->clean_bits);
		else
			dm_io_client_destroy(lc->io_req.client);
		vfree(lc->disk_header);
		kfree(lc);
		return -ENOMEM;
	}
	lc->sync_search = 0;
	log->context = lc;

	return 0;
}

static int core_ctr(struct dm_dirty_log *log, struct dm_target *ti,
		    unsigned int argc, char **argv)
{
	return create_log_context(log, ti, argc, argv, NULL);
}

static void destroy_log_context(struct log_c *lc)
{
	vfree(lc->sync_bits);
	vfree(lc->recovering_bits);
	kfree(lc);
}

static void core_dtr(struct dm_dirty_log *log)
{
	struct log_c *lc = (struct log_c *) log->context;

	vfree(lc->clean_bits);
	destroy_log_context(lc);
}

/*----------------------------------------------------------------
 * disk log constructor/destructor
 *
 * argv contains log_device region_size followed optionally by [no]sync
 *--------------------------------------------------------------*/
static int disk_ctr(struct dm_dirty_log *log, struct dm_target *ti,
		    unsigned int argc, char **argv)
{
	int r;
	struct dm_dev *dev;

	if (argc < 2 || argc > 3) {
		DMWARN("wrong number of arguments to disk dirty region log");
		return -EINVAL;
	}

	r = dm_get_device(ti, argv[0], dm_table_get_mode(ti->table), &dev);
	if (r)
		return r;

	r = create_log_context(log, ti, argc - 1, argv + 1, dev);
	if (r) {
		dm_put_device(ti, dev);
		return r;
	}

	return 0;
}

static void disk_dtr(struct dm_dirty_log *log)
{
	struct log_c *lc = (struct log_c *) log->context;

	dm_put_device(lc->ti, lc->log_dev);
	vfree(lc->disk_header);
	dm_io_client_destroy(lc->io_req.client);
	destroy_log_context(lc);
}

static void fail_log_device(struct log_c *lc)
{
	if (lc->log_dev_failed)
		return;

	lc->log_dev_failed = 1;
	dm_table_event(lc->ti->table);
}

static int disk_resume(struct dm_dirty_log *log)
{
	int r;
	unsigned i;
	struct log_c *lc = (struct log_c *) log->context;
	size_t size = lc->bitset_uint32_count * sizeof(uint32_t);

	/* read the disk header */
	r = read_header(lc);
	if (r) {
		DMWARN("%s: Failed to read header on dirty region log device",
		       lc->log_dev->name);
		fail_log_device(lc);
		/*
		 * If the log device cannot be read, we must assume
		 * all regions are out-of-sync.  If we simply return
		 * here, the state will be uninitialized and could
		 * lead us to return 'in-sync' status for regions
		 * that are actually 'out-of-sync'.
		 */
		lc->header.nr_regions = 0;
	}

	/* set or clear any new bits -- device has grown */
	if (lc->sync == NOSYNC)
		for (i = lc->header.nr_regions; i < lc->region_count; i++)
			/* FIXME: amazingly inefficient */
			log_set_bit(lc, lc->clean_bits, i);
	else
		for (i = lc->header.nr_regions; i < lc->region_count; i++)
			/* FIXME: amazingly inefficient */
			log_clear_bit(lc, lc->clean_bits, i);

	/* clear any old bits -- device has shrunk */
	for (i = lc->region_count; i % (sizeof(*lc->clean_bits) << BYTE_SHIFT); i++)
		log_clear_bit(lc, lc->clean_bits, i);

	/* copy clean across to sync */
	memcpy(lc->sync_bits, lc->clean_bits, size);
	lc->sync_count = memweight(lc->clean_bits,
				lc->bitset_uint32_count * sizeof(uint32_t));
	lc->sync_search = 0;

	/* set the correct number of regions in the header */
	lc->header.nr_regions = lc->region_count;

	header_to_disk(&lc->header, lc->disk_header);

	/* write the new header */
	r = rw_header(lc, REQ_OP_WRITE);
	if (!r) {
		r = flush_header(lc);
		if (r)
			lc->log_dev_flush_failed = 1;
	}
	if (r) {
		DMWARN("%s: Failed to write header on dirty region log device",
		       lc->log_dev->name);
		fail_log_device(lc);
	}

	return r;
}

static uint32_t core_get_region_size(struct dm_dirty_log *log)
{
	struct log_c *lc = (struct log_c *) log->context;
	return lc->region_size;
}

static int core_resume(struct dm_dirty_log *log)
{
	struct log_c *lc = (struct log_c *) log->context;
	lc->sync_search = 0;
	return 0;
}

static int core_is_clean(struct dm_dirty_log *log, region_t region)
{
	struct log_c *lc = (struct log_c *) log->context;
	return log_test_bit(lc->clean_bits, region);
}

static int core_in_sync(struct dm_dirty_log *log, region_t region, int block)
{
	struct log_c *lc = (struct log_c *) log->context;
	return log_test_bit(lc->sync_bits, region);
}

static int core_flush(struct dm_dirty_log *log)
{
	/* no op */
	return 0;
}

static int disk_flush(struct dm_dirty_log *log)
{
	int r, i;
	struct log_c *lc = log->context;

	/* only write if the log has changed */
	if (!lc->touched_cleaned && !lc->touched_dirtied)
		return 0;

	if (lc->touched_cleaned && log->flush_callback_fn &&
	    log->flush_callback_fn(lc->ti)) {
		/*
		 * At this point it is impossible to determine which
		 * regions are clean and which are dirty (without
		 * re-reading the log off disk). So mark all of them
		 * dirty.
		 */
		lc->flush_failed = 1;
		for (i = 0; i < lc->region_count; i++)
			log_clear_bit(lc, lc->clean_bits, i);
	}

	r = rw_header(lc, REQ_OP_WRITE);
	if (r)
		fail_log_device(lc);
	else {
		if (lc->touched_dirtied) {
			r = flush_header(lc);
			if (r) {
				lc->log_dev_flush_failed = 1;
				fail_log_device(lc);
			} else
				lc->touched_dirtied = 0;
		}
		lc->touched_cleaned = 0;
	}

	return r;
}

static void core_mark_region(struct dm_dirty_log *log, region_t region)
{
	struct log_c *lc = (struct log_c *) log->context;
	log_clear_bit(lc, lc->clean_bits, region);
}

static void core_clear_region(struct dm_dirty_log *log, region_t region)
{
	struct log_c *lc = (struct log_c *) log->context;
	if (likely(!lc->flush_failed))
		log_set_bit(lc, lc->clean_bits, region);
}

static int core_get_resync_work(struct dm_dirty_log *log, region_t *region)
{
	struct log_c *lc = (struct log_c *) log->context;

	if (lc->sync_search >= lc->region_count)
		return 0;

	do {
		*region = find_next_zero_bit_le(lc->sync_bits,
					     lc->region_count,
					     lc->sync_search);
		lc->sync_search = *region + 1;

		if (*region >= lc->region_count)
			return 0;

	} while (log_test_bit(lc->recovering_bits, *region));

	log_set_bit(lc, lc->recovering_bits, *region);
	return 1;
}

static void core_set_region_sync(struct dm_dirty_log *log, region_t region,
				 int in_sync)
{
	struct log_c *lc = (struct log_c *) log->context;

	log_clear_bit(lc, lc->recovering_bits, region);
	if (in_sync) {
		log_set_bit(lc, lc->sync_bits, region);
                lc->sync_count++;
        } else if (log_test_bit(lc->sync_bits, region)) {
		lc->sync_count--;
		log_clear_bit(lc, lc->sync_bits, region);
	}
}

static region_t core_get_sync_count(struct dm_dirty_log *log)
{
        struct log_c *lc = (struct log_c *) log->context;

        return lc->sync_count;
}

#define	DMEMIT_SYNC \
	if (lc->sync != DEFAULTSYNC) \
		DMEMIT("%ssync ", lc->sync == NOSYNC ? "no" : "")

static int core_status(struct dm_dirty_log *log, status_type_t status,
		       char *result, unsigned int maxlen)
{
	int sz = 0;
	struct log_c *lc = log->context;

	switch(status) {
	case STATUSTYPE_INFO:
		DMEMIT("1 %s", log->type->name);
		break;

	case STATUSTYPE_TABLE:
		DMEMIT("%s %u %u ", log->type->name,
		       lc->sync == DEFAULTSYNC ? 1 : 2, lc->region_size);
		DMEMIT_SYNC;
		break;

	case STATUSTYPE_IMA:
		*result = '\0';
		break;
	}

	return sz;
}

static int disk_status(struct dm_dirty_log *log, status_type_t status,
		       char *result, unsigned int maxlen)
{
	int sz = 0;
	struct log_c *lc = log->context;

	switch(status) {
	case STATUSTYPE_INFO:
		DMEMIT("3 %s %s %c", log->type->name, lc->log_dev->name,
		       lc->log_dev_flush_failed ? 'F' :
		       lc->log_dev_failed ? 'D' :
		       'A');
		break;

	case STATUSTYPE_TABLE:
		DMEMIT("%s %u %s %u ", log->type->name,
		       lc->sync == DEFAULTSYNC ? 2 : 3, lc->log_dev->name,
		       lc->region_size);
		DMEMIT_SYNC;
		break;

	case STATUSTYPE_IMA:
		*result = '\0';
		break;
	}

	return sz;
}

static struct dm_dirty_log_type _core_type = {
	.name = "core",
	.module = THIS_MODULE,
	.ctr = core_ctr,
	.dtr = core_dtr,
	.resume = core_resume,
	.get_region_size = core_get_region_size,
	.is_clean = core_is_clean,
	.in_sync = core_in_sync,
	.flush = core_flush,
	.mark_region = core_mark_region,
	.clear_region = core_clear_region,
	.get_resync_work = core_get_resync_work,
	.set_region_sync = core_set_region_sync,
	.get_sync_count = core_get_sync_count,
	.status = core_status,
};

static struct dm_dirty_log_type _disk_type = {
	.name = "disk",
	.module = THIS_MODULE,
	.ctr = disk_ctr,
	.dtr = disk_dtr,
	.postsuspend = disk_flush,
	.resume = disk_resume,
	.get_region_size = core_get_region_size,
	.is_clean = core_is_clean,
	.in_sync = core_in_sync,
	.flush = disk_flush,
	.mark_region = core_mark_region,
	.clear_region = core_clear_region,
	.get_resync_work = core_get_resync_work,
	.set_region_sync = core_set_region_sync,
	.get_sync_count = core_get_sync_count,
	.status = disk_status,
};

static int __init dm_dirty_log_init(void)
{
	int r;

	r = dm_dirty_log_type_register(&_core_type);
	if (r)
		DMWARN("couldn't register core log");

	r = dm_dirty_log_type_register(&_disk_type);
	if (r) {
		DMWARN("couldn't register disk type");
		dm_dirty_log_type_unregister(&_core_type);
	}

	return r;
}

static void __exit dm_dirty_log_exit(void)
{
	dm_dirty_log_type_unregister(&_disk_type);
	dm_dirty_log_type_unregister(&_core_type);
}

module_init(dm_dirty_log_init);
module_exit(dm_dirty_log_exit);

MODULE_DESCRIPTION(DM_NAME " dirty region log");
MODULE_AUTHOR("Joe Thornber, Heinz Mauelshagen <dm-devel@redhat.com>");
MODULE_LICENSE("GPL");
