// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2019 Arrikto, Inc. All Rights Reserved.
 */

#include <linux/mm.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/rwsem.h>
#include <linux/bitops.h>
#include <linux/bitmap.h>
#include <linux/device-mapper.h>

#include "persistent-data/dm-bitset.h"
#include "persistent-data/dm-space-map.h"
#include "persistent-data/dm-block-manager.h"
#include "persistent-data/dm-transaction-manager.h"

#include "dm-clone-metadata.h"

#define DM_MSG_PREFIX "clone metadata"

#define SUPERBLOCK_LOCATION 0
#define SUPERBLOCK_MAGIC 0x8af27f64
#define SUPERBLOCK_CSUM_XOR 257649492

#define DM_CLONE_MAX_CONCURRENT_LOCKS 5

#define UUID_LEN 16

/* Min and max dm-clone metadata versions supported */
#define DM_CLONE_MIN_METADATA_VERSION 1
#define DM_CLONE_MAX_METADATA_VERSION 1

/*
 * On-disk metadata layout
 */
struct superblock_disk {
	__le32 csum;
	__le32 flags;
	__le64 blocknr;

	__u8 uuid[UUID_LEN];
	__le64 magic;
	__le32 version;

	__u8 metadata_space_map_root[SPACE_MAP_ROOT_SIZE];

	__le64 region_size;
	__le64 target_size;

	__le64 bitset_root;
} __packed;

/*
 * Region and Dirty bitmaps.
 *
 * dm-clone logically splits the source and destination devices in regions of
 * fixed size. The destination device's regions are gradually hydrated, i.e.,
 * we copy (clone) the source's regions to the destination device. Eventually,
 * all regions will get hydrated and all I/O will be served from the
 * destination device.
 *
 * We maintain an on-disk bitmap which tracks the state of each of the
 * destination device's regions, i.e., whether they are hydrated or not.
 *
 * To save constantly doing look ups on disk we keep an in core copy of the
 * on-disk bitmap, the region_map.
 *
 * In order to track which regions are hydrated during a metadata transaction,
 * we use a second set of bitmaps, the dmap (dirty bitmap), which includes two
 * bitmaps, namely dirty_regions and dirty_words. The dirty_regions bitmap
 * tracks the regions that got hydrated during the current metadata
 * transaction. The dirty_words bitmap tracks the dirty words, i.e. longs, of
 * the dirty_regions bitmap.
 *
 * This allows us to precisely track the regions that were hydrated during the
 * current metadata transaction and update the metadata accordingly, when we
 * commit the current transaction. This is important because dm-clone should
 * only commit the metadata of regions that were properly flushed to the
 * destination device beforehand. Otherwise, in case of a crash, we could end
 * up with a corrupted dm-clone device.
 *
 * When a region finishes hydrating dm-clone calls
 * dm_clone_set_region_hydrated(), or for discard requests
 * dm_clone_cond_set_range(), which sets the corresponding bits in region_map
 * and dmap.
 *
 * During a metadata commit we scan dmap->dirty_words and dmap->dirty_regions
 * and update the on-disk metadata accordingly. Thus, we don't have to flush to
 * disk the whole region_map. We can just flush the dirty region_map bits.
 *
 * We use the helper dmap->dirty_words bitmap, which is smaller than the
 * original region_map, to reduce the amount of memory accesses during a
 * metadata commit. Moreover, as dm-bitset also accesses the on-disk bitmap in
 * 64-bit word granularity, the dirty_words bitmap helps us avoid useless disk
 * accesses.
 *
 * We could update directly the on-disk bitmap, when dm-clone calls either
 * dm_clone_set_region_hydrated() or dm_clone_cond_set_range(), buts this
 * inserts significant metadata I/O overhead in dm-clone's I/O path. Also, as
 * these two functions don't block, we can call them in interrupt context,
 * e.g., in a hooked overwrite bio's completion routine, and further reduce the
 * I/O completion latency.
 *
 * We maintain two dirty bitmap sets. During a metadata commit we atomically
 * swap the currently used dmap with the unused one. This allows the metadata
 * update functions to run concurrently with an ongoing commit.
 */
struct dirty_map {
	unsigned long *dirty_words;
	unsigned long *dirty_regions;
	unsigned int changed;
};

struct dm_clone_metadata {
	/* The metadata block device */
	struct block_device *bdev;

	sector_t target_size;
	sector_t region_size;
	unsigned long nr_regions;
	unsigned long nr_words;

	/* Spinlock protecting the region and dirty bitmaps. */
	spinlock_t bitmap_lock;
	struct dirty_map dmap[2];
	struct dirty_map *current_dmap;

	/* Protected by lock */
	struct dirty_map *committing_dmap;

	/*
	 * In core copy of the on-disk bitmap to save constantly doing look ups
	 * on disk.
	 */
	unsigned long *region_map;

	/* Protected by bitmap_lock */
	unsigned int read_only;

	struct dm_block_manager *bm;
	struct dm_space_map *sm;
	struct dm_transaction_manager *tm;

	struct rw_semaphore lock;

	struct dm_disk_bitset bitset_info;
	dm_block_t bitset_root;

	/*
	 * Reading the space map root can fail, so we read it into this
	 * buffer before the superblock is locked and updated.
	 */
	__u8 metadata_space_map_root[SPACE_MAP_ROOT_SIZE];

	bool hydration_done:1;
	bool fail_io:1;
};

/*---------------------------------------------------------------------------*/

/*
 * Superblock validation.
 */
static void sb_prepare_for_write(struct dm_block_validator *v,
				 struct dm_block *b, size_t sb_block_size)
{
	struct superblock_disk *sb;
	u32 csum;

	sb = dm_block_data(b);
	sb->blocknr = cpu_to_le64(dm_block_location(b));

	csum = dm_bm_checksum(&sb->flags, sb_block_size - sizeof(__le32),
			      SUPERBLOCK_CSUM_XOR);
	sb->csum = cpu_to_le32(csum);
}

static int sb_check(struct dm_block_validator *v, struct dm_block *b,
		    size_t sb_block_size)
{
	struct superblock_disk *sb;
	u32 csum, metadata_version;

	sb = dm_block_data(b);

	if (dm_block_location(b) != le64_to_cpu(sb->blocknr)) {
		DMERR("Superblock check failed: blocknr %llu, expected %llu",
		      le64_to_cpu(sb->blocknr),
		      (unsigned long long)dm_block_location(b));
		return -ENOTBLK;
	}

	if (le64_to_cpu(sb->magic) != SUPERBLOCK_MAGIC) {
		DMERR("Superblock check failed: magic %llu, expected %llu",
		      le64_to_cpu(sb->magic),
		      (unsigned long long)SUPERBLOCK_MAGIC);
		return -EILSEQ;
	}

	csum = dm_bm_checksum(&sb->flags, sb_block_size - sizeof(__le32),
			      SUPERBLOCK_CSUM_XOR);
	if (sb->csum != cpu_to_le32(csum)) {
		DMERR("Superblock check failed: checksum %u, expected %u",
		      csum, le32_to_cpu(sb->csum));
		return -EILSEQ;
	}

	/* Check metadata version */
	metadata_version = le32_to_cpu(sb->version);
	if (metadata_version < DM_CLONE_MIN_METADATA_VERSION ||
	    metadata_version > DM_CLONE_MAX_METADATA_VERSION) {
		DMERR("Clone metadata version %u found, but only versions between %u and %u supported.",
		      metadata_version, DM_CLONE_MIN_METADATA_VERSION,
		      DM_CLONE_MAX_METADATA_VERSION);
		return -EINVAL;
	}

	return 0;
}

static struct dm_block_validator sb_validator = {
	.name = "superblock",
	.prepare_for_write = sb_prepare_for_write,
	.check = sb_check
};

/*
 * Check if the superblock is formatted or not. We consider the superblock to
 * be formatted in case we find non-zero bytes in it.
 */
static int __superblock_all_zeroes(struct dm_block_manager *bm, bool *formatted)
{
	int r;
	unsigned int i, nr_words;
	struct dm_block *sblock;
	__le64 *data_le, zero = cpu_to_le64(0);

	/*
	 * We don't use a validator here because the superblock could be all
	 * zeroes.
	 */
	r = dm_bm_read_lock(bm, SUPERBLOCK_LOCATION, NULL, &sblock);
	if (r) {
		DMERR("Failed to read_lock superblock");
		return r;
	}

	data_le = dm_block_data(sblock);
	*formatted = false;

	/* This assumes that the block size is a multiple of 8 bytes */
	BUG_ON(dm_bm_block_size(bm) % sizeof(__le64));
	nr_words = dm_bm_block_size(bm) / sizeof(__le64);
	for (i = 0; i < nr_words; i++) {
		if (data_le[i] != zero) {
			*formatted = true;
			break;
		}
	}

	dm_bm_unlock(sblock);

	return 0;
}

/*---------------------------------------------------------------------------*/

/*
 * Low-level metadata handling.
 */
static inline int superblock_read_lock(struct dm_clone_metadata *cmd,
				       struct dm_block **sblock)
{
	return dm_bm_read_lock(cmd->bm, SUPERBLOCK_LOCATION, &sb_validator, sblock);
}

static inline int superblock_write_lock_zero(struct dm_clone_metadata *cmd,
					     struct dm_block **sblock)
{
	return dm_bm_write_lock_zero(cmd->bm, SUPERBLOCK_LOCATION, &sb_validator, sblock);
}

static int __copy_sm_root(struct dm_clone_metadata *cmd)
{
	int r;
	size_t root_size;

	r = dm_sm_root_size(cmd->sm, &root_size);
	if (r)
		return r;

	return dm_sm_copy_root(cmd->sm, &cmd->metadata_space_map_root, root_size);
}

/* Save dm-clone metadata in superblock */
static void __prepare_superblock(struct dm_clone_metadata *cmd,
				 struct superblock_disk *sb)
{
	sb->flags = cpu_to_le32(0UL);

	/* FIXME: UUID is currently unused */
	memset(sb->uuid, 0, sizeof(sb->uuid));

	sb->magic = cpu_to_le64(SUPERBLOCK_MAGIC);
	sb->version = cpu_to_le32(DM_CLONE_MAX_METADATA_VERSION);

	/* Save the metadata space_map root */
	memcpy(&sb->metadata_space_map_root, &cmd->metadata_space_map_root,
	       sizeof(cmd->metadata_space_map_root));

	sb->region_size = cpu_to_le64(cmd->region_size);
	sb->target_size = cpu_to_le64(cmd->target_size);
	sb->bitset_root = cpu_to_le64(cmd->bitset_root);
}

static int __open_metadata(struct dm_clone_metadata *cmd)
{
	int r;
	struct dm_block *sblock;
	struct superblock_disk *sb;

	r = superblock_read_lock(cmd, &sblock);

	if (r) {
		DMERR("Failed to read_lock superblock");
		return r;
	}

	sb = dm_block_data(sblock);

	/* Verify that target_size and region_size haven't changed. */
	if (cmd->region_size != le64_to_cpu(sb->region_size) ||
	    cmd->target_size != le64_to_cpu(sb->target_size)) {
		DMERR("Region and/or target size don't match the ones in metadata");
		r = -EINVAL;
		goto out_with_lock;
	}

	r = dm_tm_open_with_sm(cmd->bm, SUPERBLOCK_LOCATION,
			       sb->metadata_space_map_root,
			       sizeof(sb->metadata_space_map_root),
			       &cmd->tm, &cmd->sm);

	if (r) {
		DMERR("dm_tm_open_with_sm failed");
		goto out_with_lock;
	}

	dm_disk_bitset_init(cmd->tm, &cmd->bitset_info);
	cmd->bitset_root = le64_to_cpu(sb->bitset_root);

out_with_lock:
	dm_bm_unlock(sblock);

	return r;
}

static int __format_metadata(struct dm_clone_metadata *cmd)
{
	int r;
	struct dm_block *sblock;
	struct superblock_disk *sb;

	r = dm_tm_create_with_sm(cmd->bm, SUPERBLOCK_LOCATION, &cmd->tm, &cmd->sm);
	if (r) {
		DMERR("Failed to create transaction manager");
		return r;
	}

	dm_disk_bitset_init(cmd->tm, &cmd->bitset_info);

	r = dm_bitset_empty(&cmd->bitset_info, &cmd->bitset_root);
	if (r) {
		DMERR("Failed to create empty on-disk bitset");
		goto err_with_tm;
	}

	r = dm_bitset_resize(&cmd->bitset_info, cmd->bitset_root, 0,
			     cmd->nr_regions, false, &cmd->bitset_root);
	if (r) {
		DMERR("Failed to resize on-disk bitset to %lu entries", cmd->nr_regions);
		goto err_with_tm;
	}

	/* Flush to disk all blocks, except the superblock */
	r = dm_tm_pre_commit(cmd->tm);
	if (r) {
		DMERR("dm_tm_pre_commit failed");
		goto err_with_tm;
	}

	r = __copy_sm_root(cmd);
	if (r) {
		DMERR("__copy_sm_root failed");
		goto err_with_tm;
	}

	r = superblock_write_lock_zero(cmd, &sblock);
	if (r) {
		DMERR("Failed to write_lock superblock");
		goto err_with_tm;
	}

	sb = dm_block_data(sblock);
	__prepare_superblock(cmd, sb);
	r = dm_tm_commit(cmd->tm, sblock);
	if (r) {
		DMERR("Failed to commit superblock");
		goto err_with_tm;
	}

	return 0;

err_with_tm:
	dm_sm_destroy(cmd->sm);
	dm_tm_destroy(cmd->tm);

	return r;
}

static int __open_or_format_metadata(struct dm_clone_metadata *cmd, bool may_format_device)
{
	int r;
	bool formatted = false;

	r = __superblock_all_zeroes(cmd->bm, &formatted);
	if (r)
		return r;

	if (!formatted)
		return may_format_device ? __format_metadata(cmd) : -EPERM;

	return __open_metadata(cmd);
}

static int __create_persistent_data_structures(struct dm_clone_metadata *cmd,
					       bool may_format_device)
{
	int r;

	/* Create block manager */
	cmd->bm = dm_block_manager_create(cmd->bdev,
					 DM_CLONE_METADATA_BLOCK_SIZE << SECTOR_SHIFT,
					 DM_CLONE_MAX_CONCURRENT_LOCKS);
	if (IS_ERR(cmd->bm)) {
		DMERR("Failed to create block manager");
		return PTR_ERR(cmd->bm);
	}

	r = __open_or_format_metadata(cmd, may_format_device);
	if (r)
		dm_block_manager_destroy(cmd->bm);

	return r;
}

static void __destroy_persistent_data_structures(struct dm_clone_metadata *cmd)
{
	dm_sm_destroy(cmd->sm);
	dm_tm_destroy(cmd->tm);
	dm_block_manager_destroy(cmd->bm);
}

/*---------------------------------------------------------------------------*/

static int __dirty_map_init(struct dirty_map *dmap, unsigned long nr_words,
			    unsigned long nr_regions)
{
	dmap->changed = 0;

	dmap->dirty_words = kvzalloc(bitmap_size(nr_words), GFP_KERNEL);
	if (!dmap->dirty_words)
		return -ENOMEM;

	dmap->dirty_regions = kvzalloc(bitmap_size(nr_regions), GFP_KERNEL);
	if (!dmap->dirty_regions) {
		kvfree(dmap->dirty_words);
		return -ENOMEM;
	}

	return 0;
}

static void __dirty_map_exit(struct dirty_map *dmap)
{
	kvfree(dmap->dirty_words);
	kvfree(dmap->dirty_regions);
}

static int dirty_map_init(struct dm_clone_metadata *cmd)
{
	if (__dirty_map_init(&cmd->dmap[0], cmd->nr_words, cmd->nr_regions)) {
		DMERR("Failed to allocate dirty bitmap");
		return -ENOMEM;
	}

	if (__dirty_map_init(&cmd->dmap[1], cmd->nr_words, cmd->nr_regions)) {
		DMERR("Failed to allocate dirty bitmap");
		__dirty_map_exit(&cmd->dmap[0]);
		return -ENOMEM;
	}

	cmd->current_dmap = &cmd->dmap[0];
	cmd->committing_dmap = NULL;

	return 0;
}

static void dirty_map_exit(struct dm_clone_metadata *cmd)
{
	__dirty_map_exit(&cmd->dmap[0]);
	__dirty_map_exit(&cmd->dmap[1]);
}

static int __load_bitset_in_core(struct dm_clone_metadata *cmd)
{
	int r;
	unsigned long i;
	struct dm_bitset_cursor c;

	/* Flush bitset cache */
	r = dm_bitset_flush(&cmd->bitset_info, cmd->bitset_root, &cmd->bitset_root);
	if (r)
		return r;

	r = dm_bitset_cursor_begin(&cmd->bitset_info, cmd->bitset_root, cmd->nr_regions, &c);
	if (r)
		return r;

	for (i = 0; ; i++) {
		if (dm_bitset_cursor_get_value(&c))
			__set_bit(i, cmd->region_map);
		else
			__clear_bit(i, cmd->region_map);

		if (i >= (cmd->nr_regions - 1))
			break;

		r = dm_bitset_cursor_next(&c);

		if (r)
			break;
	}

	dm_bitset_cursor_end(&c);

	return r;
}

struct dm_clone_metadata *dm_clone_metadata_open(struct block_device *bdev,
						 sector_t target_size,
						 sector_t region_size)
{
	int r;
	struct dm_clone_metadata *cmd;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		DMERR("Failed to allocate memory for dm-clone metadata");
		return ERR_PTR(-ENOMEM);
	}

	cmd->bdev = bdev;
	cmd->target_size = target_size;
	cmd->region_size = region_size;
	cmd->nr_regions = dm_sector_div_up(cmd->target_size, cmd->region_size);
	cmd->nr_words = BITS_TO_LONGS(cmd->nr_regions);

	init_rwsem(&cmd->lock);
	spin_lock_init(&cmd->bitmap_lock);
	cmd->read_only = 0;
	cmd->fail_io = false;
	cmd->hydration_done = false;

	cmd->region_map = kvmalloc(bitmap_size(cmd->nr_regions), GFP_KERNEL);
	if (!cmd->region_map) {
		DMERR("Failed to allocate memory for region bitmap");
		r = -ENOMEM;
		goto out_with_md;
	}

	r = __create_persistent_data_structures(cmd, true);
	if (r)
		goto out_with_region_map;

	r = __load_bitset_in_core(cmd);
	if (r) {
		DMERR("Failed to load on-disk region map");
		goto out_with_pds;
	}

	r = dirty_map_init(cmd);
	if (r)
		goto out_with_pds;

	if (bitmap_full(cmd->region_map, cmd->nr_regions))
		cmd->hydration_done = true;

	return cmd;

out_with_pds:
	__destroy_persistent_data_structures(cmd);

out_with_region_map:
	kvfree(cmd->region_map);

out_with_md:
	kfree(cmd);

	return ERR_PTR(r);
}

void dm_clone_metadata_close(struct dm_clone_metadata *cmd)
{
	if (!cmd->fail_io)
		__destroy_persistent_data_structures(cmd);

	dirty_map_exit(cmd);
	kvfree(cmd->region_map);
	kfree(cmd);
}

bool dm_clone_is_hydration_done(struct dm_clone_metadata *cmd)
{
	return cmd->hydration_done;
}

bool dm_clone_is_region_hydrated(struct dm_clone_metadata *cmd, unsigned long region_nr)
{
	return dm_clone_is_hydration_done(cmd) || test_bit(region_nr, cmd->region_map);
}

bool dm_clone_is_range_hydrated(struct dm_clone_metadata *cmd,
				unsigned long start, unsigned long nr_regions)
{
	unsigned long bit;

	if (dm_clone_is_hydration_done(cmd))
		return true;

	bit = find_next_zero_bit(cmd->region_map, cmd->nr_regions, start);

	return (bit >= (start + nr_regions));
}

unsigned int dm_clone_nr_of_hydrated_regions(struct dm_clone_metadata *cmd)
{
	return bitmap_weight(cmd->region_map, cmd->nr_regions);
}

unsigned long dm_clone_find_next_unhydrated_region(struct dm_clone_metadata *cmd,
						   unsigned long start)
{
	return find_next_zero_bit(cmd->region_map, cmd->nr_regions, start);
}

static int __update_metadata_word(struct dm_clone_metadata *cmd,
				  unsigned long *dirty_regions,
				  unsigned long word)
{
	int r;
	unsigned long index = word * BITS_PER_LONG;
	unsigned long max_index = min(cmd->nr_regions, (word + 1) * BITS_PER_LONG);

	while (index < max_index) {
		if (test_bit(index, dirty_regions)) {
			r = dm_bitset_set_bit(&cmd->bitset_info, cmd->bitset_root,
					      index, &cmd->bitset_root);
			if (r) {
				DMERR("dm_bitset_set_bit failed");
				return r;
			}
			__clear_bit(index, dirty_regions);
		}
		index++;
	}

	return 0;
}

static int __metadata_commit(struct dm_clone_metadata *cmd)
{
	int r;
	struct dm_block *sblock;
	struct superblock_disk *sb;

	/* Flush bitset cache */
	r = dm_bitset_flush(&cmd->bitset_info, cmd->bitset_root, &cmd->bitset_root);
	if (r) {
		DMERR("dm_bitset_flush failed");
		return r;
	}

	/* Flush to disk all blocks, except the superblock */
	r = dm_tm_pre_commit(cmd->tm);
	if (r) {
		DMERR("dm_tm_pre_commit failed");
		return r;
	}

	/* Save the space map root in cmd->metadata_space_map_root */
	r = __copy_sm_root(cmd);
	if (r) {
		DMERR("__copy_sm_root failed");
		return r;
	}

	/* Lock the superblock */
	r = superblock_write_lock_zero(cmd, &sblock);
	if (r) {
		DMERR("Failed to write_lock superblock");
		return r;
	}

	/* Save the metadata in superblock */
	sb = dm_block_data(sblock);
	__prepare_superblock(cmd, sb);

	/* Unlock superblock and commit it to disk */
	r = dm_tm_commit(cmd->tm, sblock);
	if (r) {
		DMERR("Failed to commit superblock");
		return r;
	}

	/*
	 * FIXME: Find a more efficient way to check if the hydration is done.
	 */
	if (bitmap_full(cmd->region_map, cmd->nr_regions))
		cmd->hydration_done = true;

	return 0;
}

static int __flush_dmap(struct dm_clone_metadata *cmd, struct dirty_map *dmap)
{
	int r;
	unsigned long word;

	word = 0;
	do {
		word = find_next_bit(dmap->dirty_words, cmd->nr_words, word);

		if (word == cmd->nr_words)
			break;

		r = __update_metadata_word(cmd, dmap->dirty_regions, word);

		if (r)
			return r;

		__clear_bit(word, dmap->dirty_words);
		word++;
	} while (word < cmd->nr_words);

	r = __metadata_commit(cmd);

	if (r)
		return r;

	/* Update the changed flag */
	spin_lock_irq(&cmd->bitmap_lock);
	dmap->changed = 0;
	spin_unlock_irq(&cmd->bitmap_lock);

	return 0;
}

int dm_clone_metadata_pre_commit(struct dm_clone_metadata *cmd)
{
	int r = 0;
	struct dirty_map *dmap, *next_dmap;

	down_write(&cmd->lock);

	if (cmd->fail_io || dm_bm_is_read_only(cmd->bm)) {
		r = -EPERM;
		goto out;
	}

	/* Get current dirty bitmap */
	dmap = cmd->current_dmap;

	/* Get next dirty bitmap */
	next_dmap = (dmap == &cmd->dmap[0]) ? &cmd->dmap[1] : &cmd->dmap[0];

	/*
	 * The last commit failed, so we don't have a clean dirty-bitmap to
	 * use.
	 */
	if (WARN_ON(next_dmap->changed || cmd->committing_dmap)) {
		r = -EINVAL;
		goto out;
	}

	/* Swap dirty bitmaps */
	spin_lock_irq(&cmd->bitmap_lock);
	cmd->current_dmap = next_dmap;
	spin_unlock_irq(&cmd->bitmap_lock);

	/* Set old dirty bitmap as currently committing */
	cmd->committing_dmap = dmap;
out:
	up_write(&cmd->lock);

	return r;
}

int dm_clone_metadata_commit(struct dm_clone_metadata *cmd)
{
	int r = -EPERM;

	down_write(&cmd->lock);

	if (cmd->fail_io || dm_bm_is_read_only(cmd->bm))
		goto out;

	if (WARN_ON(!cmd->committing_dmap)) {
		r = -EINVAL;
		goto out;
	}

	r = __flush_dmap(cmd, cmd->committing_dmap);
	if (!r) {
		/* Clear committing dmap */
		cmd->committing_dmap = NULL;
	}
out:
	up_write(&cmd->lock);

	return r;
}

int dm_clone_set_region_hydrated(struct dm_clone_metadata *cmd, unsigned long region_nr)
{
	int r = 0;
	struct dirty_map *dmap;
	unsigned long word, flags;

	if (unlikely(region_nr >= cmd->nr_regions)) {
		DMERR("Region %lu out of range (total number of regions %lu)",
		      region_nr, cmd->nr_regions);
		return -ERANGE;
	}

	word = region_nr / BITS_PER_LONG;

	spin_lock_irqsave(&cmd->bitmap_lock, flags);

	if (cmd->read_only) {
		r = -EPERM;
		goto out;
	}

	dmap = cmd->current_dmap;

	__set_bit(word, dmap->dirty_words);
	__set_bit(region_nr, dmap->dirty_regions);
	__set_bit(region_nr, cmd->region_map);
	dmap->changed = 1;

out:
	spin_unlock_irqrestore(&cmd->bitmap_lock, flags);

	return r;
}

int dm_clone_cond_set_range(struct dm_clone_metadata *cmd, unsigned long start,
			    unsigned long nr_regions)
{
	int r = 0;
	struct dirty_map *dmap;
	unsigned long word, region_nr;

	if (unlikely(start >= cmd->nr_regions || (start + nr_regions) < start ||
		     (start + nr_regions) > cmd->nr_regions)) {
		DMERR("Invalid region range: start %lu, nr_regions %lu (total number of regions %lu)",
		      start, nr_regions, cmd->nr_regions);
		return -ERANGE;
	}

	spin_lock_irq(&cmd->bitmap_lock);

	if (cmd->read_only) {
		r = -EPERM;
		goto out;
	}

	dmap = cmd->current_dmap;
	for (region_nr = start; region_nr < (start + nr_regions); region_nr++) {
		if (!test_bit(region_nr, cmd->region_map)) {
			word = region_nr / BITS_PER_LONG;
			__set_bit(word, dmap->dirty_words);
			__set_bit(region_nr, dmap->dirty_regions);
			__set_bit(region_nr, cmd->region_map);
			dmap->changed = 1;
		}
	}
out:
	spin_unlock_irq(&cmd->bitmap_lock);

	return r;
}

/*
 * WARNING: This must not be called concurrently with either
 * dm_clone_set_region_hydrated() or dm_clone_cond_set_range(), as it changes
 * cmd->region_map without taking the cmd->bitmap_lock spinlock. The only
 * exception is after setting the metadata to read-only mode, using
 * dm_clone_metadata_set_read_only().
 *
 * We don't take the spinlock because __load_bitset_in_core() does I/O, so it
 * may block.
 */
int dm_clone_reload_in_core_bitset(struct dm_clone_metadata *cmd)
{
	int r = -EINVAL;

	down_write(&cmd->lock);

	if (cmd->fail_io)
		goto out;

	r = __load_bitset_in_core(cmd);
out:
	up_write(&cmd->lock);

	return r;
}

bool dm_clone_changed_this_transaction(struct dm_clone_metadata *cmd)
{
	bool r;
	unsigned long flags;

	spin_lock_irqsave(&cmd->bitmap_lock, flags);
	r = cmd->dmap[0].changed || cmd->dmap[1].changed;
	spin_unlock_irqrestore(&cmd->bitmap_lock, flags);

	return r;
}

int dm_clone_metadata_abort(struct dm_clone_metadata *cmd)
{
	int r = -EPERM;

	down_write(&cmd->lock);

	if (cmd->fail_io || dm_bm_is_read_only(cmd->bm))
		goto out;

	__destroy_persistent_data_structures(cmd);

	r = __create_persistent_data_structures(cmd, false);
	if (r) {
		/* If something went wrong we can neither write nor read the metadata */
		cmd->fail_io = true;
	}
out:
	up_write(&cmd->lock);

	return r;
}

void dm_clone_metadata_set_read_only(struct dm_clone_metadata *cmd)
{
	down_write(&cmd->lock);

	spin_lock_irq(&cmd->bitmap_lock);
	cmd->read_only = 1;
	spin_unlock_irq(&cmd->bitmap_lock);

	if (!cmd->fail_io)
		dm_bm_set_read_only(cmd->bm);

	up_write(&cmd->lock);
}

void dm_clone_metadata_set_read_write(struct dm_clone_metadata *cmd)
{
	down_write(&cmd->lock);

	spin_lock_irq(&cmd->bitmap_lock);
	cmd->read_only = 0;
	spin_unlock_irq(&cmd->bitmap_lock);

	if (!cmd->fail_io)
		dm_bm_set_read_write(cmd->bm);

	up_write(&cmd->lock);
}

int dm_clone_get_free_metadata_block_count(struct dm_clone_metadata *cmd,
					   dm_block_t *result)
{
	int r = -EINVAL;

	down_read(&cmd->lock);

	if (!cmd->fail_io)
		r = dm_sm_get_nr_free(cmd->sm, result);

	up_read(&cmd->lock);

	return r;
}

int dm_clone_get_metadata_dev_size(struct dm_clone_metadata *cmd,
				   dm_block_t *result)
{
	int r = -EINVAL;

	down_read(&cmd->lock);

	if (!cmd->fail_io)
		r = dm_sm_get_nr_blocks(cmd->sm, result);

	up_read(&cmd->lock);

	return r;
}
