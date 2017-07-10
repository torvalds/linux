/*
 * Copyright (C) 2017 Western Digital Corporation or its affiliates.
 *
 * This file is released under the GPL.
 */

#ifndef DM_ZONED_H
#define DM_ZONED_H

#include <linux/types.h>
#include <linux/blkdev.h>
#include <linux/device-mapper.h>
#include <linux/dm-kcopyd.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/rwsem.h>
#include <linux/rbtree.h>
#include <linux/radix-tree.h>
#include <linux/shrinker.h>

/*
 * dm-zoned creates block devices with 4KB blocks, always.
 */
#define DMZ_BLOCK_SHIFT		12
#define DMZ_BLOCK_SIZE		(1 << DMZ_BLOCK_SHIFT)
#define DMZ_BLOCK_MASK		(DMZ_BLOCK_SIZE - 1)

#define DMZ_BLOCK_SHIFT_BITS	(DMZ_BLOCK_SHIFT + 3)
#define DMZ_BLOCK_SIZE_BITS	(1 << DMZ_BLOCK_SHIFT_BITS)
#define DMZ_BLOCK_MASK_BITS	(DMZ_BLOCK_SIZE_BITS - 1)

#define DMZ_BLOCK_SECTORS_SHIFT	(DMZ_BLOCK_SHIFT - SECTOR_SHIFT)
#define DMZ_BLOCK_SECTORS	(DMZ_BLOCK_SIZE >> SECTOR_SHIFT)
#define DMZ_BLOCK_SECTORS_MASK	(DMZ_BLOCK_SECTORS - 1)

/*
 * 4KB block <-> 512B sector conversion.
 */
#define dmz_blk2sect(b)		((sector_t)(b) << DMZ_BLOCK_SECTORS_SHIFT)
#define dmz_sect2blk(s)		((sector_t)(s) >> DMZ_BLOCK_SECTORS_SHIFT)

#define dmz_bio_block(bio)	dmz_sect2blk((bio)->bi_iter.bi_sector)
#define dmz_bio_blocks(bio)	dmz_sect2blk(bio_sectors(bio))

/*
 * Zoned block device information.
 */
struct dmz_dev {
	struct block_device	*bdev;

	char			name[BDEVNAME_SIZE];

	sector_t		capacity;

	unsigned int		nr_zones;

	sector_t		zone_nr_sectors;
	unsigned int		zone_nr_sectors_shift;

	sector_t		zone_nr_blocks;
	sector_t		zone_nr_blocks_shift;
};

#define dmz_bio_chunk(dev, bio)	((bio)->bi_iter.bi_sector >> \
				 (dev)->zone_nr_sectors_shift)
#define dmz_chunk_block(dev, b)	((b) & ((dev)->zone_nr_blocks - 1))

/*
 * Zone descriptor.
 */
struct dm_zone {
	/* For listing the zone depending on its state */
	struct list_head	link;

	/* Zone type and state */
	unsigned long		flags;

	/* Zone activation reference count */
	atomic_t		refcount;

	/* Zone write pointer block (relative to the zone start block) */
	unsigned int		wp_block;

	/* Zone weight (number of valid blocks in the zone) */
	unsigned int		weight;

	/* The chunk that the zone maps */
	unsigned int		chunk;

	/*
	 * For a sequential data zone, pointer to the random zone
	 * used as a buffer for processing unaligned writes.
	 * For a buffer zone, this points back to the data zone.
	 */
	struct dm_zone		*bzone;
};

/*
 * Zone flags.
 */
enum {
	/* Zone write type */
	DMZ_RND,
	DMZ_SEQ,

	/* Zone critical condition */
	DMZ_OFFLINE,
	DMZ_READ_ONLY,

	/* How the zone is being used */
	DMZ_META,
	DMZ_DATA,
	DMZ_BUF,

	/* Zone internal state */
	DMZ_ACTIVE,
	DMZ_RECLAIM,
	DMZ_SEQ_WRITE_ERR,
};

/*
 * Zone data accessors.
 */
#define dmz_is_rnd(z)		test_bit(DMZ_RND, &(z)->flags)
#define dmz_is_seq(z)		test_bit(DMZ_SEQ, &(z)->flags)
#define dmz_is_empty(z)		((z)->wp_block == 0)
#define dmz_is_offline(z)	test_bit(DMZ_OFFLINE, &(z)->flags)
#define dmz_is_readonly(z)	test_bit(DMZ_READ_ONLY, &(z)->flags)
#define dmz_is_active(z)	test_bit(DMZ_ACTIVE, &(z)->flags)
#define dmz_in_reclaim(z)	test_bit(DMZ_RECLAIM, &(z)->flags)
#define dmz_seq_write_err(z)	test_bit(DMZ_SEQ_WRITE_ERR, &(z)->flags)

#define dmz_is_meta(z)		test_bit(DMZ_META, &(z)->flags)
#define dmz_is_buf(z)		test_bit(DMZ_BUF, &(z)->flags)
#define dmz_is_data(z)		test_bit(DMZ_DATA, &(z)->flags)

#define dmz_weight(z)		((z)->weight)

/*
 * Message functions.
 */
#define dmz_dev_info(dev, format, args...)	\
	DMINFO("(%s): " format, (dev)->name, ## args)

#define dmz_dev_err(dev, format, args...)	\
	DMERR("(%s): " format, (dev)->name, ## args)

#define dmz_dev_warn(dev, format, args...)	\
	DMWARN("(%s): " format, (dev)->name, ## args)

#define dmz_dev_debug(dev, format, args...)	\
	DMDEBUG("(%s): " format, (dev)->name, ## args)

struct dmz_metadata;
struct dmz_reclaim;

/*
 * Functions defined in dm-zoned-metadata.c
 */
int dmz_ctr_metadata(struct dmz_dev *dev, struct dmz_metadata **zmd);
void dmz_dtr_metadata(struct dmz_metadata *zmd);
int dmz_resume_metadata(struct dmz_metadata *zmd);

void dmz_lock_map(struct dmz_metadata *zmd);
void dmz_unlock_map(struct dmz_metadata *zmd);
void dmz_lock_metadata(struct dmz_metadata *zmd);
void dmz_unlock_metadata(struct dmz_metadata *zmd);
void dmz_lock_flush(struct dmz_metadata *zmd);
void dmz_unlock_flush(struct dmz_metadata *zmd);
int dmz_flush_metadata(struct dmz_metadata *zmd);

unsigned int dmz_id(struct dmz_metadata *zmd, struct dm_zone *zone);
sector_t dmz_start_sect(struct dmz_metadata *zmd, struct dm_zone *zone);
sector_t dmz_start_block(struct dmz_metadata *zmd, struct dm_zone *zone);
unsigned int dmz_nr_chunks(struct dmz_metadata *zmd);

#define DMZ_ALLOC_RND		0x01
#define DMZ_ALLOC_RECLAIM	0x02

struct dm_zone *dmz_alloc_zone(struct dmz_metadata *zmd, unsigned long flags);
void dmz_free_zone(struct dmz_metadata *zmd, struct dm_zone *zone);

void dmz_map_zone(struct dmz_metadata *zmd, struct dm_zone *zone,
		  unsigned int chunk);
void dmz_unmap_zone(struct dmz_metadata *zmd, struct dm_zone *zone);
unsigned int dmz_nr_rnd_zones(struct dmz_metadata *zmd);
unsigned int dmz_nr_unmap_rnd_zones(struct dmz_metadata *zmd);

void dmz_activate_zone(struct dm_zone *zone);
void dmz_deactivate_zone(struct dm_zone *zone);

int dmz_lock_zone_reclaim(struct dm_zone *zone);
void dmz_unlock_zone_reclaim(struct dm_zone *zone);
struct dm_zone *dmz_get_zone_for_reclaim(struct dmz_metadata *zmd);

struct dm_zone *dmz_get_chunk_mapping(struct dmz_metadata *zmd,
				      unsigned int chunk, int op);
void dmz_put_chunk_mapping(struct dmz_metadata *zmd, struct dm_zone *zone);
struct dm_zone *dmz_get_chunk_buffer(struct dmz_metadata *zmd,
				     struct dm_zone *dzone);

int dmz_validate_blocks(struct dmz_metadata *zmd, struct dm_zone *zone,
			sector_t chunk_block, unsigned int nr_blocks);
int dmz_invalidate_blocks(struct dmz_metadata *zmd, struct dm_zone *zone,
			  sector_t chunk_block, unsigned int nr_blocks);
int dmz_block_valid(struct dmz_metadata *zmd, struct dm_zone *zone,
		    sector_t chunk_block);
int dmz_first_valid_block(struct dmz_metadata *zmd, struct dm_zone *zone,
			  sector_t *chunk_block);
int dmz_copy_valid_blocks(struct dmz_metadata *zmd, struct dm_zone *from_zone,
			  struct dm_zone *to_zone);
int dmz_merge_valid_blocks(struct dmz_metadata *zmd, struct dm_zone *from_zone,
			   struct dm_zone *to_zone, sector_t chunk_block);

/*
 * Functions defined in dm-zoned-reclaim.c
 */
int dmz_ctr_reclaim(struct dmz_dev *dev, struct dmz_metadata *zmd,
		    struct dmz_reclaim **zrc);
void dmz_dtr_reclaim(struct dmz_reclaim *zrc);
void dmz_suspend_reclaim(struct dmz_reclaim *zrc);
void dmz_resume_reclaim(struct dmz_reclaim *zrc);
void dmz_reclaim_bio_acc(struct dmz_reclaim *zrc);
void dmz_schedule_reclaim(struct dmz_reclaim *zrc);

#endif /* DM_ZONED_H */
