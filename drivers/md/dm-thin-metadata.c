/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * This file is released under the GPL.
 */

#include "dm-thin-metadata.h"
#include "persistent-data/dm-btree.h"
#include "persistent-data/dm-space-map.h"
#include "persistent-data/dm-space-map-disk.h"
#include "persistent-data/dm-transaction-manager.h"

#include <linux/list.h>
#include <linux/device-mapper.h>
#include <linux/workqueue.h>

/*--------------------------------------------------------------------------
 * As far as the metadata goes, there is:
 *
 * - A superblock in block zero, taking up fewer than 512 bytes for
 *   atomic writes.
 *
 * - A space map managing the metadata blocks.
 *
 * - A space map managing the data blocks.
 *
 * - A btree mapping our internal thin dev ids onto struct disk_device_details.
 *
 * - A hierarchical btree, with 2 levels which effectively maps (thin
 *   dev id, virtual block) -> block_time.  Block time is a 64-bit
 *   field holding the time in the low 24 bits, and block in the top 48
 *   bits.
 *
 * BTrees consist solely of btree_nodes, that fill a block.  Some are
 * internal nodes, as such their values are a __le64 pointing to other
 * nodes.  Leaf nodes can store data of any reasonable size (ie. much
 * smaller than the block size).  The nodes consist of the header,
 * followed by an array of keys, followed by an array of values.  We have
 * to binary search on the keys so they're all held together to help the
 * cpu cache.
 *
 * Space maps have 2 btrees:
 *
 * - One maps a uint64_t onto a struct index_entry.  Which points to a
 *   bitmap block, and has some details about how many free entries there
 *   are etc.
 *
 * - The bitmap blocks have a header (for the checksum).  Then the rest
 *   of the block is pairs of bits.  With the meaning being:
 *
 *   0 - ref count is 0
 *   1 - ref count is 1
 *   2 - ref count is 2
 *   3 - ref count is higher than 2
 *
 * - If the count is higher than 2 then the ref count is entered in a
 *   second btree that directly maps the block_address to a uint32_t ref
 *   count.
 *
 * The space map metadata variant doesn't have a bitmaps btree.  Instead
 * it has one single blocks worth of index_entries.  This avoids
 * recursive issues with the bitmap btree needing to allocate space in
 * order to insert.  With a small data block size such as 64k the
 * metadata support data devices that are hundreds of terrabytes.
 *
 * The space maps allocate space linearly from front to back.  Space that
 * is freed in a transaction is never recycled within that transaction.
 * To try and avoid fragmenting _free_ space the allocator always goes
 * back and fills in gaps.
 *
 * All metadata io is in THIN_METADATA_BLOCK_SIZE sized/aligned chunks
 * from the block manager.
 *--------------------------------------------------------------------------*/

#define DM_MSG_PREFIX   "thin metadata"

#define THIN_SUPERBLOCK_MAGIC 27022010
#define THIN_SUPERBLOCK_LOCATION 0
#define THIN_VERSION 1
#define THIN_METADATA_CACHE_SIZE 64
#define SECTOR_TO_BLOCK_SHIFT 3

/* This should be plenty */
#define SPACE_MAP_ROOT_SIZE 128

/*
 * Little endian on-disk superblock and device details.
 */
struct thin_disk_superblock {
	__le32 csum;	/* Checksum of superblock except for this field. */
	__le32 flags;
	__le64 blocknr;	/* This block number, dm_block_t. */

	__u8 uuid[16];
	__le64 magic;
	__le32 version;
	__le32 time;

	__le64 trans_id;

	/*
	 * Root held by userspace transactions.
	 */
	__le64 held_root;

	__u8 data_space_map_root[SPACE_MAP_ROOT_SIZE];
	__u8 metadata_space_map_root[SPACE_MAP_ROOT_SIZE];

	/*
	 * 2-level btree mapping (dev_id, (dev block, time)) -> data block
	 */
	__le64 data_mapping_root;

	/*
	 * Device detail root mapping dev_id -> device_details
	 */
	__le64 device_details_root;

	__le32 data_block_size;		/* In 512-byte sectors. */

	__le32 metadata_block_size;	/* In 512-byte sectors. */
	__le64 metadata_nr_blocks;

	__le32 compat_flags;
	__le32 compat_ro_flags;
	__le32 incompat_flags;
} __packed;

struct disk_device_details {
	__le64 mapped_blocks;
	__le64 transaction_id;		/* When created. */
	__le32 creation_time;
	__le32 snapshotted_time;
} __packed;

struct dm_pool_metadata {
	struct hlist_node hash;

	struct block_device *bdev;
	struct dm_block_manager *bm;
	struct dm_space_map *metadata_sm;
	struct dm_space_map *data_sm;
	struct dm_transaction_manager *tm;
	struct dm_transaction_manager *nb_tm;

	/*
	 * Two-level btree.
	 * First level holds thin_dev_t.
	 * Second level holds mappings.
	 */
	struct dm_btree_info info;

	/*
	 * Non-blocking version of the above.
	 */
	struct dm_btree_info nb_info;

	/*
	 * Just the top level for deleting whole devices.
	 */
	struct dm_btree_info tl_info;

	/*
	 * Just the bottom level for creating new devices.
	 */
	struct dm_btree_info bl_info;

	/*
	 * Describes the device details btree.
	 */
	struct dm_btree_info details_info;

	struct rw_semaphore root_lock;
	uint32_t time;
	int need_commit;
	dm_block_t root;
	dm_block_t details_root;
	struct list_head thin_devices;
	uint64_t trans_id;
	unsigned long flags;
	sector_t data_block_size;
};

struct dm_thin_device {
	struct list_head list;
	struct dm_pool_metadata *pmd;
	dm_thin_id id;

	int open_count;
	int changed;
	uint64_t mapped_blocks;
	uint64_t transaction_id;
	uint32_t creation_time;
	uint32_t snapshotted_time;
};

/*----------------------------------------------------------------
 * superblock validator
 *--------------------------------------------------------------*/

#define SUPERBLOCK_CSUM_XOR 160774

static void sb_prepare_for_write(struct dm_block_validator *v,
				 struct dm_block *b,
				 size_t block_size)
{
	struct thin_disk_superblock *disk_super = dm_block_data(b);

	disk_super->blocknr = cpu_to_le64(dm_block_location(b));
	disk_super->csum = cpu_to_le32(dm_bm_checksum(&disk_super->flags,
						      block_size - sizeof(__le32),
						      SUPERBLOCK_CSUM_XOR));
}

static int sb_check(struct dm_block_validator *v,
		    struct dm_block *b,
		    size_t block_size)
{
	struct thin_disk_superblock *disk_super = dm_block_data(b);
	__le32 csum_le;

	if (dm_block_location(b) != le64_to_cpu(disk_super->blocknr)) {
		DMERR("sb_check failed: blocknr %llu: "
		      "wanted %llu", le64_to_cpu(disk_super->blocknr),
		      (unsigned long long)dm_block_location(b));
		return -ENOTBLK;
	}

	if (le64_to_cpu(disk_super->magic) != THIN_SUPERBLOCK_MAGIC) {
		DMERR("sb_check failed: magic %llu: "
		      "wanted %llu", le64_to_cpu(disk_super->magic),
		      (unsigned long long)THIN_SUPERBLOCK_MAGIC);
		return -EILSEQ;
	}

	csum_le = cpu_to_le32(dm_bm_checksum(&disk_super->flags,
					     block_size - sizeof(__le32),
					     SUPERBLOCK_CSUM_XOR));
	if (csum_le != disk_super->csum) {
		DMERR("sb_check failed: csum %u: wanted %u",
		      le32_to_cpu(csum_le), le32_to_cpu(disk_super->csum));
		return -EILSEQ;
	}

	return 0;
}

static struct dm_block_validator sb_validator = {
	.name = "superblock",
	.prepare_for_write = sb_prepare_for_write,
	.check = sb_check
};

/*----------------------------------------------------------------
 * Methods for the btree value types
 *--------------------------------------------------------------*/

static uint64_t pack_block_time(dm_block_t b, uint32_t t)
{
	return (b << 24) | t;
}

static void unpack_block_time(uint64_t v, dm_block_t *b, uint32_t *t)
{
	*b = v >> 24;
	*t = v & ((1 << 24) - 1);
}

static void data_block_inc(void *context, void *value_le)
{
	struct dm_space_map *sm = context;
	__le64 v_le;
	uint64_t b;
	uint32_t t;

	memcpy(&v_le, value_le, sizeof(v_le));
	unpack_block_time(le64_to_cpu(v_le), &b, &t);
	dm_sm_inc_block(sm, b);
}

static void data_block_dec(void *context, void *value_le)
{
	struct dm_space_map *sm = context;
	__le64 v_le;
	uint64_t b;
	uint32_t t;

	memcpy(&v_le, value_le, sizeof(v_le));
	unpack_block_time(le64_to_cpu(v_le), &b, &t);
	dm_sm_dec_block(sm, b);
}

static int data_block_equal(void *context, void *value1_le, void *value2_le)
{
	__le64 v1_le, v2_le;
	uint64_t b1, b2;
	uint32_t t;

	memcpy(&v1_le, value1_le, sizeof(v1_le));
	memcpy(&v2_le, value2_le, sizeof(v2_le));
	unpack_block_time(le64_to_cpu(v1_le), &b1, &t);
	unpack_block_time(le64_to_cpu(v2_le), &b2, &t);

	return b1 == b2;
}

static void subtree_inc(void *context, void *value)
{
	struct dm_btree_info *info = context;
	__le64 root_le;
	uint64_t root;

	memcpy(&root_le, value, sizeof(root_le));
	root = le64_to_cpu(root_le);
	dm_tm_inc(info->tm, root);
}

static void subtree_dec(void *context, void *value)
{
	struct dm_btree_info *info = context;
	__le64 root_le;
	uint64_t root;

	memcpy(&root_le, value, sizeof(root_le));
	root = le64_to_cpu(root_le);
	if (dm_btree_del(info, root))
		DMERR("btree delete failed\n");
}

static int subtree_equal(void *context, void *value1_le, void *value2_le)
{
	__le64 v1_le, v2_le;
	memcpy(&v1_le, value1_le, sizeof(v1_le));
	memcpy(&v2_le, value2_le, sizeof(v2_le));

	return v1_le == v2_le;
}

/*----------------------------------------------------------------*/

static int superblock_all_zeroes(struct dm_block_manager *bm, int *result)
{
	int r;
	unsigned i;
	struct dm_block *b;
	__le64 *data_le, zero = cpu_to_le64(0);
	unsigned block_size = dm_bm_block_size(bm) / sizeof(__le64);

	/*
	 * We can't use a validator here - it may be all zeroes.
	 */
	r = dm_bm_read_lock(bm, THIN_SUPERBLOCK_LOCATION, NULL, &b);
	if (r)
		return r;

	data_le = dm_block_data(b);
	*result = 1;
	for (i = 0; i < block_size; i++) {
		if (data_le[i] != zero) {
			*result = 0;
			break;
		}
	}

	return dm_bm_unlock(b);
}

static int init_pmd(struct dm_pool_metadata *pmd,
		    struct dm_block_manager *bm,
		    dm_block_t nr_blocks, int create)
{
	int r;
	struct dm_space_map *sm, *data_sm;
	struct dm_transaction_manager *tm;
	struct dm_block *sblock;

	if (create) {
		r = dm_tm_create_with_sm(bm, THIN_SUPERBLOCK_LOCATION,
					 &sb_validator, &tm, &sm, &sblock);
		if (r < 0) {
			DMERR("tm_create_with_sm failed");
			return r;
		}

		data_sm = dm_sm_disk_create(tm, nr_blocks);
		if (IS_ERR(data_sm)) {
			DMERR("sm_disk_create failed");
			dm_tm_unlock(tm, sblock);
			r = PTR_ERR(data_sm);
			goto bad;
		}
	} else {
		struct thin_disk_superblock *disk_super = NULL;
		size_t space_map_root_offset =
			offsetof(struct thin_disk_superblock, metadata_space_map_root);

		r = dm_tm_open_with_sm(bm, THIN_SUPERBLOCK_LOCATION,
				       &sb_validator, space_map_root_offset,
				       SPACE_MAP_ROOT_SIZE, &tm, &sm, &sblock);
		if (r < 0) {
			DMERR("tm_open_with_sm failed");
			return r;
		}

		disk_super = dm_block_data(sblock);
		data_sm = dm_sm_disk_open(tm, disk_super->data_space_map_root,
					  sizeof(disk_super->data_space_map_root));
		if (IS_ERR(data_sm)) {
			DMERR("sm_disk_open failed");
			r = PTR_ERR(data_sm);
			goto bad;
		}
	}


	r = dm_tm_unlock(tm, sblock);
	if (r < 0) {
		DMERR("couldn't unlock superblock");
		goto bad_data_sm;
	}

	pmd->bm = bm;
	pmd->metadata_sm = sm;
	pmd->data_sm = data_sm;
	pmd->tm = tm;
	pmd->nb_tm = dm_tm_create_non_blocking_clone(tm);
	if (!pmd->nb_tm) {
		DMERR("could not create clone tm");
		r = -ENOMEM;
		goto bad_data_sm;
	}

	pmd->info.tm = tm;
	pmd->info.levels = 2;
	pmd->info.value_type.context = pmd->data_sm;
	pmd->info.value_type.size = sizeof(__le64);
	pmd->info.value_type.inc = data_block_inc;
	pmd->info.value_type.dec = data_block_dec;
	pmd->info.value_type.equal = data_block_equal;

	memcpy(&pmd->nb_info, &pmd->info, sizeof(pmd->nb_info));
	pmd->nb_info.tm = pmd->nb_tm;

	pmd->tl_info.tm = tm;
	pmd->tl_info.levels = 1;
	pmd->tl_info.value_type.context = &pmd->info;
	pmd->tl_info.value_type.size = sizeof(__le64);
	pmd->tl_info.value_type.inc = subtree_inc;
	pmd->tl_info.value_type.dec = subtree_dec;
	pmd->tl_info.value_type.equal = subtree_equal;

	pmd->bl_info.tm = tm;
	pmd->bl_info.levels = 1;
	pmd->bl_info.value_type.context = pmd->data_sm;
	pmd->bl_info.value_type.size = sizeof(__le64);
	pmd->bl_info.value_type.inc = data_block_inc;
	pmd->bl_info.value_type.dec = data_block_dec;
	pmd->bl_info.value_type.equal = data_block_equal;

	pmd->details_info.tm = tm;
	pmd->details_info.levels = 1;
	pmd->details_info.value_type.context = NULL;
	pmd->details_info.value_type.size = sizeof(struct disk_device_details);
	pmd->details_info.value_type.inc = NULL;
	pmd->details_info.value_type.dec = NULL;
	pmd->details_info.value_type.equal = NULL;

	pmd->root = 0;

	init_rwsem(&pmd->root_lock);
	pmd->time = 0;
	pmd->need_commit = 0;
	pmd->details_root = 0;
	pmd->trans_id = 0;
	pmd->flags = 0;
	INIT_LIST_HEAD(&pmd->thin_devices);

	return 0;

bad_data_sm:
	dm_sm_destroy(data_sm);
bad:
	dm_tm_destroy(tm);
	dm_sm_destroy(sm);

	return r;
}

static int __begin_transaction(struct dm_pool_metadata *pmd)
{
	int r;
	u32 features;
	struct thin_disk_superblock *disk_super;
	struct dm_block *sblock;

	/*
	 * __maybe_commit_transaction() resets these
	 */
	WARN_ON(pmd->need_commit);

	/*
	 * We re-read the superblock every time.  Shouldn't need to do this
	 * really.
	 */
	r = dm_bm_read_lock(pmd->bm, THIN_SUPERBLOCK_LOCATION,
			    &sb_validator, &sblock);
	if (r)
		return r;

	disk_super = dm_block_data(sblock);
	pmd->time = le32_to_cpu(disk_super->time);
	pmd->root = le64_to_cpu(disk_super->data_mapping_root);
	pmd->details_root = le64_to_cpu(disk_super->device_details_root);
	pmd->trans_id = le64_to_cpu(disk_super->trans_id);
	pmd->flags = le32_to_cpu(disk_super->flags);
	pmd->data_block_size = le32_to_cpu(disk_super->data_block_size);

	features = le32_to_cpu(disk_super->incompat_flags) & ~THIN_FEATURE_INCOMPAT_SUPP;
	if (features) {
		DMERR("could not access metadata due to "
		      "unsupported optional features (%lx).",
		      (unsigned long)features);
		r = -EINVAL;
		goto out;
	}

	/*
	 * Check for read-only metadata to skip the following RDWR checks.
	 */
	if (get_disk_ro(pmd->bdev->bd_disk))
		goto out;

	features = le32_to_cpu(disk_super->compat_ro_flags) & ~THIN_FEATURE_COMPAT_RO_SUPP;
	if (features) {
		DMERR("could not access metadata RDWR due to "
		      "unsupported optional features (%lx).",
		      (unsigned long)features);
		r = -EINVAL;
	}

out:
	dm_bm_unlock(sblock);
	return r;
}

static int __write_changed_details(struct dm_pool_metadata *pmd)
{
	int r;
	struct dm_thin_device *td, *tmp;
	struct disk_device_details details;
	uint64_t key;

	list_for_each_entry_safe(td, tmp, &pmd->thin_devices, list) {
		if (!td->changed)
			continue;

		key = td->id;

		details.mapped_blocks = cpu_to_le64(td->mapped_blocks);
		details.transaction_id = cpu_to_le64(td->transaction_id);
		details.creation_time = cpu_to_le32(td->creation_time);
		details.snapshotted_time = cpu_to_le32(td->snapshotted_time);
		__dm_bless_for_disk(&details);

		r = dm_btree_insert(&pmd->details_info, pmd->details_root,
				    &key, &details, &pmd->details_root);
		if (r)
			return r;

		if (td->open_count)
			td->changed = 0;
		else {
			list_del(&td->list);
			kfree(td);
		}

		pmd->need_commit = 1;
	}

	return 0;
}

static int __commit_transaction(struct dm_pool_metadata *pmd)
{
	/*
	 * FIXME: Associated pool should be made read-only on failure.
	 */
	int r;
	size_t metadata_len, data_len;
	struct thin_disk_superblock *disk_super;
	struct dm_block *sblock;

	/*
	 * We need to know if the thin_disk_superblock exceeds a 512-byte sector.
	 */
	BUILD_BUG_ON(sizeof(struct thin_disk_superblock) > 512);

	r = __write_changed_details(pmd);
	if (r < 0)
		goto out;

	if (!pmd->need_commit)
		goto out;

	r = dm_sm_commit(pmd->data_sm);
	if (r < 0)
		goto out;

	r = dm_tm_pre_commit(pmd->tm);
	if (r < 0)
		goto out;

	r = dm_sm_root_size(pmd->metadata_sm, &metadata_len);
	if (r < 0)
		goto out;

	r = dm_sm_root_size(pmd->metadata_sm, &data_len);
	if (r < 0)
		goto out;

	r = dm_bm_write_lock(pmd->bm, THIN_SUPERBLOCK_LOCATION,
			     &sb_validator, &sblock);
	if (r)
		goto out;

	disk_super = dm_block_data(sblock);
	disk_super->time = cpu_to_le32(pmd->time);
	disk_super->data_mapping_root = cpu_to_le64(pmd->root);
	disk_super->device_details_root = cpu_to_le64(pmd->details_root);
	disk_super->trans_id = cpu_to_le64(pmd->trans_id);
	disk_super->flags = cpu_to_le32(pmd->flags);

	r = dm_sm_copy_root(pmd->metadata_sm, &disk_super->metadata_space_map_root,
			    metadata_len);
	if (r < 0)
		goto out_locked;

	r = dm_sm_copy_root(pmd->data_sm, &disk_super->data_space_map_root,
			    data_len);
	if (r < 0)
		goto out_locked;

	r = dm_tm_commit(pmd->tm, sblock);
	if (!r)
		pmd->need_commit = 0;

out:
	return r;

out_locked:
	dm_bm_unlock(sblock);
	return r;
}

struct dm_pool_metadata *dm_pool_metadata_open(struct block_device *bdev,
					       sector_t data_block_size)
{
	int r;
	struct thin_disk_superblock *disk_super;
	struct dm_pool_metadata *pmd;
	sector_t bdev_size = i_size_read(bdev->bd_inode) >> SECTOR_SHIFT;
	struct dm_block_manager *bm;
	int create;
	struct dm_block *sblock;

	pmd = kmalloc(sizeof(*pmd), GFP_KERNEL);
	if (!pmd) {
		DMERR("could not allocate metadata struct");
		return ERR_PTR(-ENOMEM);
	}

	/*
	 * Max hex locks:
	 *  3 for btree insert +
	 *  2 for btree lookup used within space map
	 */
	bm = dm_block_manager_create(bdev, THIN_METADATA_BLOCK_SIZE,
				     THIN_METADATA_CACHE_SIZE, 5);
	if (!bm) {
		DMERR("could not create block manager");
		kfree(pmd);
		return ERR_PTR(-ENOMEM);
	}

	r = superblock_all_zeroes(bm, &create);
	if (r) {
		dm_block_manager_destroy(bm);
		kfree(pmd);
		return ERR_PTR(r);
	}


	r = init_pmd(pmd, bm, 0, create);
	if (r) {
		dm_block_manager_destroy(bm);
		kfree(pmd);
		return ERR_PTR(r);
	}
	pmd->bdev = bdev;

	if (!create) {
		r = __begin_transaction(pmd);
		if (r < 0)
			goto bad;
		return pmd;
	}

	/*
	 * Create.
	 */
	r = dm_bm_write_lock(pmd->bm, THIN_SUPERBLOCK_LOCATION,
			     &sb_validator, &sblock);
	if (r)
		goto bad;

	disk_super = dm_block_data(sblock);
	disk_super->magic = cpu_to_le64(THIN_SUPERBLOCK_MAGIC);
	disk_super->version = cpu_to_le32(THIN_VERSION);
	disk_super->time = 0;
	disk_super->metadata_block_size = cpu_to_le32(THIN_METADATA_BLOCK_SIZE >> SECTOR_SHIFT);
	disk_super->metadata_nr_blocks = cpu_to_le64(bdev_size >> SECTOR_TO_BLOCK_SHIFT);
	disk_super->data_block_size = cpu_to_le32(data_block_size);

	r = dm_bm_unlock(sblock);
	if (r < 0)
		goto bad;

	r = dm_btree_empty(&pmd->info, &pmd->root);
	if (r < 0)
		goto bad;

	r = dm_btree_empty(&pmd->details_info, &pmd->details_root);
	if (r < 0) {
		DMERR("couldn't create devices root");
		goto bad;
	}

	pmd->flags = 0;
	pmd->need_commit = 1;
	r = dm_pool_commit_metadata(pmd);
	if (r < 0) {
		DMERR("%s: dm_pool_commit_metadata() failed, error = %d",
		      __func__, r);
		goto bad;
	}

	return pmd;

bad:
	if (dm_pool_metadata_close(pmd) < 0)
		DMWARN("%s: dm_pool_metadata_close() failed.", __func__);
	return ERR_PTR(r);
}

int dm_pool_metadata_close(struct dm_pool_metadata *pmd)
{
	int r;
	unsigned open_devices = 0;
	struct dm_thin_device *td, *tmp;

	down_read(&pmd->root_lock);
	list_for_each_entry_safe(td, tmp, &pmd->thin_devices, list) {
		if (td->open_count)
			open_devices++;
		else {
			list_del(&td->list);
			kfree(td);
		}
	}
	up_read(&pmd->root_lock);

	if (open_devices) {
		DMERR("attempt to close pmd when %u device(s) are still open",
		       open_devices);
		return -EBUSY;
	}

	r = __commit_transaction(pmd);
	if (r < 0)
		DMWARN("%s: __commit_transaction() failed, error = %d",
		       __func__, r);

	dm_tm_destroy(pmd->tm);
	dm_tm_destroy(pmd->nb_tm);
	dm_block_manager_destroy(pmd->bm);
	dm_sm_destroy(pmd->metadata_sm);
	dm_sm_destroy(pmd->data_sm);
	kfree(pmd);

	return 0;
}

/*
 * __open_device: Returns @td corresponding to device with id @dev,
 * creating it if @create is set and incrementing @td->open_count.
 * On failure, @td is undefined.
 */
static int __open_device(struct dm_pool_metadata *pmd,
			 dm_thin_id dev, int create,
			 struct dm_thin_device **td)
{
	int r, changed = 0;
	struct dm_thin_device *td2;
	uint64_t key = dev;
	struct disk_device_details details_le;

	/*
	 * If the device is already open, return it.
	 */
	list_for_each_entry(td2, &pmd->thin_devices, list)
		if (td2->id == dev) {
			/*
			 * May not create an already-open device.
			 */
			if (create)
				return -EEXIST;

			td2->open_count++;
			*td = td2;
			return 0;
		}

	/*
	 * Check the device exists.
	 */
	r = dm_btree_lookup(&pmd->details_info, pmd->details_root,
			    &key, &details_le);
	if (r) {
		if (r != -ENODATA || !create)
			return r;

		/*
		 * Create new device.
		 */
		changed = 1;
		details_le.mapped_blocks = 0;
		details_le.transaction_id = cpu_to_le64(pmd->trans_id);
		details_le.creation_time = cpu_to_le32(pmd->time);
		details_le.snapshotted_time = cpu_to_le32(pmd->time);
	}

	*td = kmalloc(sizeof(**td), GFP_NOIO);
	if (!*td)
		return -ENOMEM;

	(*td)->pmd = pmd;
	(*td)->id = dev;
	(*td)->open_count = 1;
	(*td)->changed = changed;
	(*td)->mapped_blocks = le64_to_cpu(details_le.mapped_blocks);
	(*td)->transaction_id = le64_to_cpu(details_le.transaction_id);
	(*td)->creation_time = le32_to_cpu(details_le.creation_time);
	(*td)->snapshotted_time = le32_to_cpu(details_le.snapshotted_time);

	list_add(&(*td)->list, &pmd->thin_devices);

	return 0;
}

static void __close_device(struct dm_thin_device *td)
{
	--td->open_count;
}

static int __create_thin(struct dm_pool_metadata *pmd,
			 dm_thin_id dev)
{
	int r;
	dm_block_t dev_root;
	uint64_t key = dev;
	struct disk_device_details details_le;
	struct dm_thin_device *td;
	__le64 value;

	r = dm_btree_lookup(&pmd->details_info, pmd->details_root,
			    &key, &details_le);
	if (!r)
		return -EEXIST;

	/*
	 * Create an empty btree for the mappings.
	 */
	r = dm_btree_empty(&pmd->bl_info, &dev_root);
	if (r)
		return r;

	/*
	 * Insert it into the main mapping tree.
	 */
	value = cpu_to_le64(dev_root);
	__dm_bless_for_disk(&value);
	r = dm_btree_insert(&pmd->tl_info, pmd->root, &key, &value, &pmd->root);
	if (r) {
		dm_btree_del(&pmd->bl_info, dev_root);
		return r;
	}

	r = __open_device(pmd, dev, 1, &td);
	if (r) {
		dm_btree_remove(&pmd->tl_info, pmd->root, &key, &pmd->root);
		dm_btree_del(&pmd->bl_info, dev_root);
		return r;
	}
	__close_device(td);

	return r;
}

int dm_pool_create_thin(struct dm_pool_metadata *pmd, dm_thin_id dev)
{
	int r;

	down_write(&pmd->root_lock);
	r = __create_thin(pmd, dev);
	up_write(&pmd->root_lock);

	return r;
}

static int __set_snapshot_details(struct dm_pool_metadata *pmd,
				  struct dm_thin_device *snap,
				  dm_thin_id origin, uint32_t time)
{
	int r;
	struct dm_thin_device *td;

	r = __open_device(pmd, origin, 0, &td);
	if (r)
		return r;

	td->changed = 1;
	td->snapshotted_time = time;

	snap->mapped_blocks = td->mapped_blocks;
	snap->snapshotted_time = time;
	__close_device(td);

	return 0;
}

static int __create_snap(struct dm_pool_metadata *pmd,
			 dm_thin_id dev, dm_thin_id origin)
{
	int r;
	dm_block_t origin_root;
	uint64_t key = origin, dev_key = dev;
	struct dm_thin_device *td;
	struct disk_device_details details_le;
	__le64 value;

	/* check this device is unused */
	r = dm_btree_lookup(&pmd->details_info, pmd->details_root,
			    &dev_key, &details_le);
	if (!r)
		return -EEXIST;

	/* find the mapping tree for the origin */
	r = dm_btree_lookup(&pmd->tl_info, pmd->root, &key, &value);
	if (r)
		return r;
	origin_root = le64_to_cpu(value);

	/* clone the origin, an inc will do */
	dm_tm_inc(pmd->tm, origin_root);

	/* insert into the main mapping tree */
	value = cpu_to_le64(origin_root);
	__dm_bless_for_disk(&value);
	key = dev;
	r = dm_btree_insert(&pmd->tl_info, pmd->root, &key, &value, &pmd->root);
	if (r) {
		dm_tm_dec(pmd->tm, origin_root);
		return r;
	}

	pmd->time++;

	r = __open_device(pmd, dev, 1, &td);
	if (r)
		goto bad;

	r = __set_snapshot_details(pmd, td, origin, pmd->time);
	__close_device(td);

	if (r)
		goto bad;

	return 0;

bad:
	dm_btree_remove(&pmd->tl_info, pmd->root, &key, &pmd->root);
	dm_btree_remove(&pmd->details_info, pmd->details_root,
			&key, &pmd->details_root);
	return r;
}

int dm_pool_create_snap(struct dm_pool_metadata *pmd,
				 dm_thin_id dev,
				 dm_thin_id origin)
{
	int r;

	down_write(&pmd->root_lock);
	r = __create_snap(pmd, dev, origin);
	up_write(&pmd->root_lock);

	return r;
}

static int __delete_device(struct dm_pool_metadata *pmd, dm_thin_id dev)
{
	int r;
	uint64_t key = dev;
	struct dm_thin_device *td;

	/* TODO: failure should mark the transaction invalid */
	r = __open_device(pmd, dev, 0, &td);
	if (r)
		return r;

	if (td->open_count > 1) {
		__close_device(td);
		return -EBUSY;
	}

	list_del(&td->list);
	kfree(td);
	r = dm_btree_remove(&pmd->details_info, pmd->details_root,
			    &key, &pmd->details_root);
	if (r)
		return r;

	r = dm_btree_remove(&pmd->tl_info, pmd->root, &key, &pmd->root);
	if (r)
		return r;

	pmd->need_commit = 1;

	return 0;
}

int dm_pool_delete_thin_device(struct dm_pool_metadata *pmd,
			       dm_thin_id dev)
{
	int r;

	down_write(&pmd->root_lock);
	r = __delete_device(pmd, dev);
	up_write(&pmd->root_lock);

	return r;
}

int dm_pool_set_metadata_transaction_id(struct dm_pool_metadata *pmd,
					uint64_t current_id,
					uint64_t new_id)
{
	down_write(&pmd->root_lock);
	if (pmd->trans_id != current_id) {
		up_write(&pmd->root_lock);
		DMERR("mismatched transaction id");
		return -EINVAL;
	}

	pmd->trans_id = new_id;
	pmd->need_commit = 1;
	up_write(&pmd->root_lock);

	return 0;
}

int dm_pool_get_metadata_transaction_id(struct dm_pool_metadata *pmd,
					uint64_t *result)
{
	down_read(&pmd->root_lock);
	*result = pmd->trans_id;
	up_read(&pmd->root_lock);

	return 0;
}

static int __get_held_metadata_root(struct dm_pool_metadata *pmd,
				    dm_block_t *result)
{
	int r;
	struct thin_disk_superblock *disk_super;
	struct dm_block *sblock;

	r = dm_bm_write_lock(pmd->bm, THIN_SUPERBLOCK_LOCATION,
			     &sb_validator, &sblock);
	if (r)
		return r;

	disk_super = dm_block_data(sblock);
	*result = le64_to_cpu(disk_super->held_root);

	return dm_bm_unlock(sblock);
}

int dm_pool_get_held_metadata_root(struct dm_pool_metadata *pmd,
				   dm_block_t *result)
{
	int r;

	down_read(&pmd->root_lock);
	r = __get_held_metadata_root(pmd, result);
	up_read(&pmd->root_lock);

	return r;
}

int dm_pool_open_thin_device(struct dm_pool_metadata *pmd, dm_thin_id dev,
			     struct dm_thin_device **td)
{
	int r;

	down_write(&pmd->root_lock);
	r = __open_device(pmd, dev, 0, td);
	up_write(&pmd->root_lock);

	return r;
}

int dm_pool_close_thin_device(struct dm_thin_device *td)
{
	down_write(&td->pmd->root_lock);
	__close_device(td);
	up_write(&td->pmd->root_lock);

	return 0;
}

dm_thin_id dm_thin_dev_id(struct dm_thin_device *td)
{
	return td->id;
}

static int __snapshotted_since(struct dm_thin_device *td, uint32_t time)
{
	return td->snapshotted_time > time;
}

int dm_thin_find_block(struct dm_thin_device *td, dm_block_t block,
		       int can_block, struct dm_thin_lookup_result *result)
{
	int r;
	uint64_t block_time = 0;
	__le64 value;
	struct dm_pool_metadata *pmd = td->pmd;
	dm_block_t keys[2] = { td->id, block };

	if (can_block) {
		down_read(&pmd->root_lock);
		r = dm_btree_lookup(&pmd->info, pmd->root, keys, &value);
		if (!r)
			block_time = le64_to_cpu(value);
		up_read(&pmd->root_lock);

	} else if (down_read_trylock(&pmd->root_lock)) {
		r = dm_btree_lookup(&pmd->nb_info, pmd->root, keys, &value);
		if (!r)
			block_time = le64_to_cpu(value);
		up_read(&pmd->root_lock);

	} else
		return -EWOULDBLOCK;

	if (!r) {
		dm_block_t exception_block;
		uint32_t exception_time;
		unpack_block_time(block_time, &exception_block,
				  &exception_time);
		result->block = exception_block;
		result->shared = __snapshotted_since(td, exception_time);
	}

	return r;
}

static int __insert(struct dm_thin_device *td, dm_block_t block,
		    dm_block_t data_block)
{
	int r, inserted;
	__le64 value;
	struct dm_pool_metadata *pmd = td->pmd;
	dm_block_t keys[2] = { td->id, block };

	pmd->need_commit = 1;
	value = cpu_to_le64(pack_block_time(data_block, pmd->time));
	__dm_bless_for_disk(&value);

	r = dm_btree_insert_notify(&pmd->info, pmd->root, keys, &value,
				   &pmd->root, &inserted);
	if (r)
		return r;

	if (inserted) {
		td->mapped_blocks++;
		td->changed = 1;
	}

	return 0;
}

int dm_thin_insert_block(struct dm_thin_device *td, dm_block_t block,
			 dm_block_t data_block)
{
	int r;

	down_write(&td->pmd->root_lock);
	r = __insert(td, block, data_block);
	up_write(&td->pmd->root_lock);

	return r;
}

static int __remove(struct dm_thin_device *td, dm_block_t block)
{
	int r;
	struct dm_pool_metadata *pmd = td->pmd;
	dm_block_t keys[2] = { td->id, block };

	r = dm_btree_remove(&pmd->info, pmd->root, keys, &pmd->root);
	if (r)
		return r;

	td->mapped_blocks--;
	td->changed = 1;
	pmd->need_commit = 1;

	return 0;
}

int dm_thin_remove_block(struct dm_thin_device *td, dm_block_t block)
{
	int r;

	down_write(&td->pmd->root_lock);
	r = __remove(td, block);
	up_write(&td->pmd->root_lock);

	return r;
}

int dm_pool_alloc_data_block(struct dm_pool_metadata *pmd, dm_block_t *result)
{
	int r;

	down_write(&pmd->root_lock);

	r = dm_sm_new_block(pmd->data_sm, result);
	pmd->need_commit = 1;

	up_write(&pmd->root_lock);

	return r;
}

int dm_pool_commit_metadata(struct dm_pool_metadata *pmd)
{
	int r;

	down_write(&pmd->root_lock);

	r = __commit_transaction(pmd);
	if (r <= 0)
		goto out;

	/*
	 * Open the next transaction.
	 */
	r = __begin_transaction(pmd);
out:
	up_write(&pmd->root_lock);
	return r;
}

int dm_pool_get_free_block_count(struct dm_pool_metadata *pmd, dm_block_t *result)
{
	int r;

	down_read(&pmd->root_lock);
	r = dm_sm_get_nr_free(pmd->data_sm, result);
	up_read(&pmd->root_lock);

	return r;
}

int dm_pool_get_free_metadata_block_count(struct dm_pool_metadata *pmd,
					  dm_block_t *result)
{
	int r;

	down_read(&pmd->root_lock);
	r = dm_sm_get_nr_free(pmd->metadata_sm, result);
	up_read(&pmd->root_lock);

	return r;
}

int dm_pool_get_metadata_dev_size(struct dm_pool_metadata *pmd,
				  dm_block_t *result)
{
	int r;

	down_read(&pmd->root_lock);
	r = dm_sm_get_nr_blocks(pmd->metadata_sm, result);
	up_read(&pmd->root_lock);

	return r;
}

int dm_pool_get_data_block_size(struct dm_pool_metadata *pmd, sector_t *result)
{
	down_read(&pmd->root_lock);
	*result = pmd->data_block_size;
	up_read(&pmd->root_lock);

	return 0;
}

int dm_pool_get_data_dev_size(struct dm_pool_metadata *pmd, dm_block_t *result)
{
	int r;

	down_read(&pmd->root_lock);
	r = dm_sm_get_nr_blocks(pmd->data_sm, result);
	up_read(&pmd->root_lock);

	return r;
}

int dm_thin_get_mapped_count(struct dm_thin_device *td, dm_block_t *result)
{
	struct dm_pool_metadata *pmd = td->pmd;

	down_read(&pmd->root_lock);
	*result = td->mapped_blocks;
	up_read(&pmd->root_lock);

	return 0;
}

static int __highest_block(struct dm_thin_device *td, dm_block_t *result)
{
	int r;
	__le64 value_le;
	dm_block_t thin_root;
	struct dm_pool_metadata *pmd = td->pmd;

	r = dm_btree_lookup(&pmd->tl_info, pmd->root, &td->id, &value_le);
	if (r)
		return r;

	thin_root = le64_to_cpu(value_le);

	return dm_btree_find_highest_key(&pmd->bl_info, thin_root, result);
}

int dm_thin_get_highest_mapped_block(struct dm_thin_device *td,
				     dm_block_t *result)
{
	int r;
	struct dm_pool_metadata *pmd = td->pmd;

	down_read(&pmd->root_lock);
	r = __highest_block(td, result);
	up_read(&pmd->root_lock);

	return r;
}

static int __resize_data_dev(struct dm_pool_metadata *pmd, dm_block_t new_count)
{
	int r;
	dm_block_t old_count;

	r = dm_sm_get_nr_blocks(pmd->data_sm, &old_count);
	if (r)
		return r;

	if (new_count == old_count)
		return 0;

	if (new_count < old_count) {
		DMERR("cannot reduce size of data device");
		return -EINVAL;
	}

	r = dm_sm_extend(pmd->data_sm, new_count - old_count);
	if (!r)
		pmd->need_commit = 1;

	return r;
}

int dm_pool_resize_data_dev(struct dm_pool_metadata *pmd, dm_block_t new_count)
{
	int r;

	down_write(&pmd->root_lock);
	r = __resize_data_dev(pmd, new_count);
	up_write(&pmd->root_lock);

	return r;
}
