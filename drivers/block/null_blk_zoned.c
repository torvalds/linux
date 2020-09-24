// SPDX-License-Identifier: GPL-2.0
#include <linux/vmalloc.h>
#include "null_blk.h"

/* zone_size in MBs to sectors. */
#define ZONE_SIZE_SHIFT		11

static inline unsigned int null_zone_no(struct nullb_device *dev, sector_t sect)
{
	return sect >> ilog2(dev->zone_size_sects);
}

int null_zone_init(struct nullb_device *dev)
{
	sector_t dev_size = (sector_t)dev->size * 1024 * 1024;
	sector_t sector = 0;
	unsigned int i;

	if (!is_power_of_2(dev->zone_size)) {
		pr_err("null_blk: zone_size must be power-of-two\n");
		return -EINVAL;
	}
	if (dev->zone_size > dev->size) {
		pr_err("Zone size larger than device capacity\n");
		return -EINVAL;
	}

	dev->zone_size_sects = dev->zone_size << ZONE_SIZE_SHIFT;
	dev->nr_zones = dev_size >>
				(SECTOR_SHIFT + ilog2(dev->zone_size_sects));
	dev->zones = kvmalloc_array(dev->nr_zones, sizeof(struct blk_zone),
			GFP_KERNEL | __GFP_ZERO);
	if (!dev->zones)
		return -ENOMEM;

	for (i = 0; i < dev->nr_zones; i++) {
		struct blk_zone *zone = &dev->zones[i];

		zone->start = zone->wp = sector;
		zone->len = dev->zone_size_sects;
		zone->type = BLK_ZONE_TYPE_SEQWRITE_REQ;
		zone->cond = BLK_ZONE_COND_EMPTY;

		sector += dev->zone_size_sects;
	}

	return 0;
}

void null_zone_exit(struct nullb_device *dev)
{
	kvfree(dev->zones);
}

static void null_zone_fill_bio(struct nullb_device *dev, struct bio *bio,
			       unsigned int zno, unsigned int nr_zones)
{
	struct blk_zone_report_hdr *hdr = NULL;
	struct bio_vec bvec;
	struct bvec_iter iter;
	void *addr;
	unsigned int zones_to_cpy;

	bio_for_each_segment(bvec, bio, iter) {
		addr = kmap_atomic(bvec.bv_page);

		zones_to_cpy = bvec.bv_len / sizeof(struct blk_zone);

		if (!hdr) {
			hdr = (struct blk_zone_report_hdr *)addr;
			hdr->nr_zones = nr_zones;
			zones_to_cpy--;
			addr += sizeof(struct blk_zone_report_hdr);
		}

		zones_to_cpy = min_t(unsigned int, zones_to_cpy, nr_zones);

		memcpy(addr, &dev->zones[zno],
				zones_to_cpy * sizeof(struct blk_zone));

		kunmap_atomic(addr);

		nr_zones -= zones_to_cpy;
		zno += zones_to_cpy;

		if (!nr_zones)
			break;
	}
}

blk_status_t null_zone_report(struct nullb *nullb, struct bio *bio)
{
	struct nullb_device *dev = nullb->dev;
	unsigned int zno = null_zone_no(dev, bio->bi_iter.bi_sector);
	unsigned int nr_zones = dev->nr_zones - zno;
	unsigned int max_zones;

	max_zones = (bio->bi_iter.bi_size / sizeof(struct blk_zone)) - 1;
	nr_zones = min_t(unsigned int, nr_zones, max_zones);
	null_zone_fill_bio(nullb->dev, bio, zno, nr_zones);

	return BLK_STS_OK;
}

void null_zone_write(struct nullb_cmd *cmd, sector_t sector,
		     unsigned int nr_sectors)
{
	struct nullb_device *dev = cmd->nq->dev;
	unsigned int zno = null_zone_no(dev, sector);
	struct blk_zone *zone = &dev->zones[zno];

	switch (zone->cond) {
	case BLK_ZONE_COND_FULL:
		/* Cannot write to a full zone */
		cmd->error = BLK_STS_IOERR;
		break;
	case BLK_ZONE_COND_EMPTY:
	case BLK_ZONE_COND_IMP_OPEN:
		/* Writes must be at the write pointer position */
		if (sector != zone->wp) {
			cmd->error = BLK_STS_IOERR;
			break;
		}

		if (zone->cond == BLK_ZONE_COND_EMPTY)
			zone->cond = BLK_ZONE_COND_IMP_OPEN;

		zone->wp += nr_sectors;
		if (zone->wp == zone->start + zone->len)
			zone->cond = BLK_ZONE_COND_FULL;
		break;
	default:
		/* Invalid zone condition */
		cmd->error = BLK_STS_IOERR;
		break;
	}
}

void null_zone_reset(struct nullb_cmd *cmd, sector_t sector)
{
	struct nullb_device *dev = cmd->nq->dev;
	unsigned int zno = null_zone_no(dev, sector);
	struct blk_zone *zone = &dev->zones[zno];

	zone->cond = BLK_ZONE_COND_EMPTY;
	zone->wp = zone->start;
}
