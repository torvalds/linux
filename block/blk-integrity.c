// SPDX-License-Identifier: GPL-2.0
/*
 * blk-integrity.c - Block layer data integrity extensions
 *
 * Copyright (C) 2007, 2008 Oracle Corporation
 * Written by: Martin K. Petersen <martin.petersen@oracle.com>
 */

#include <linux/blkdev.h>
#include <linux/backing-dev.h>
#include <linux/mempool.h>
#include <linux/bio.h>
#include <linux/scatterlist.h>
#include <linux/export.h>
#include <linux/slab.h>

#include "blk.h"

/**
 * blk_rq_count_integrity_sg - Count number of integrity scatterlist elements
 * @q:		request queue
 * @bio:	bio with integrity metadata attached
 *
 * Description: Returns the number of elements required in a
 * scatterlist corresponding to the integrity metadata in a bio.
 */
int blk_rq_count_integrity_sg(struct request_queue *q, struct bio *bio)
{
	struct bio_vec iv, ivprv = { NULL };
	unsigned int segments = 0;
	unsigned int seg_size = 0;
	struct bvec_iter iter;
	int prev = 0;

	bio_for_each_integrity_vec(iv, bio, iter) {

		if (prev) {
			if (!biovec_phys_mergeable(q, &ivprv, &iv))
				goto new_segment;
			if (seg_size + iv.bv_len > queue_max_segment_size(q))
				goto new_segment;

			seg_size += iv.bv_len;
		} else {
new_segment:
			segments++;
			seg_size = iv.bv_len;
		}

		prev = 1;
		ivprv = iv;
	}

	return segments;
}
EXPORT_SYMBOL(blk_rq_count_integrity_sg);

/**
 * blk_rq_map_integrity_sg - Map integrity metadata into a scatterlist
 * @q:		request queue
 * @bio:	bio with integrity metadata attached
 * @sglist:	target scatterlist
 *
 * Description: Map the integrity vectors in request into a
 * scatterlist.  The scatterlist must be big enough to hold all
 * elements.  I.e. sized using blk_rq_count_integrity_sg().
 */
int blk_rq_map_integrity_sg(struct request_queue *q, struct bio *bio,
			    struct scatterlist *sglist)
{
	struct bio_vec iv, ivprv = { NULL };
	struct scatterlist *sg = NULL;
	unsigned int segments = 0;
	struct bvec_iter iter;
	int prev = 0;

	bio_for_each_integrity_vec(iv, bio, iter) {

		if (prev) {
			if (!biovec_phys_mergeable(q, &ivprv, &iv))
				goto new_segment;
			if (sg->length + iv.bv_len > queue_max_segment_size(q))
				goto new_segment;

			sg->length += iv.bv_len;
		} else {
new_segment:
			if (!sg)
				sg = sglist;
			else {
				sg_unmark_end(sg);
				sg = sg_next(sg);
			}

			sg_set_page(sg, iv.bv_page, iv.bv_len, iv.bv_offset);
			segments++;
		}

		prev = 1;
		ivprv = iv;
	}

	if (sg)
		sg_mark_end(sg);

	return segments;
}
EXPORT_SYMBOL(blk_rq_map_integrity_sg);

/**
 * blk_integrity_compare - Compare integrity profile of two disks
 * @gd1:	Disk to compare
 * @gd2:	Disk to compare
 *
 * Description: Meta-devices like DM and MD need to verify that all
 * sub-devices use the same integrity format before advertising to
 * upper layers that they can send/receive integrity metadata.  This
 * function can be used to check whether two gendisk devices have
 * compatible integrity formats.
 */
int blk_integrity_compare(struct gendisk *gd1, struct gendisk *gd2)
{
	struct blk_integrity *b1 = &gd1->queue->integrity;
	struct blk_integrity *b2 = &gd2->queue->integrity;

	if (!b1->profile && !b2->profile)
		return 0;

	if (!b1->profile || !b2->profile)
		return -1;

	if (b1->interval_exp != b2->interval_exp) {
		pr_err("%s: %s/%s protection interval %u != %u\n",
		       __func__, gd1->disk_name, gd2->disk_name,
		       1 << b1->interval_exp, 1 << b2->interval_exp);
		return -1;
	}

	if (b1->tuple_size != b2->tuple_size) {
		pr_err("%s: %s/%s tuple sz %u != %u\n", __func__,
		       gd1->disk_name, gd2->disk_name,
		       b1->tuple_size, b2->tuple_size);
		return -1;
	}

	if (b1->tag_size && b2->tag_size && (b1->tag_size != b2->tag_size)) {
		pr_err("%s: %s/%s tag sz %u != %u\n", __func__,
		       gd1->disk_name, gd2->disk_name,
		       b1->tag_size, b2->tag_size);
		return -1;
	}

	if (b1->profile != b2->profile) {
		pr_err("%s: %s/%s type %s != %s\n", __func__,
		       gd1->disk_name, gd2->disk_name,
		       b1->profile->name, b2->profile->name);
		return -1;
	}

	return 0;
}
EXPORT_SYMBOL(blk_integrity_compare);

bool blk_integrity_merge_rq(struct request_queue *q, struct request *req,
			    struct request *next)
{
	if (blk_integrity_rq(req) == 0 && blk_integrity_rq(next) == 0)
		return true;

	if (blk_integrity_rq(req) == 0 || blk_integrity_rq(next) == 0)
		return false;

	if (bio_integrity(req->bio)->bip_flags !=
	    bio_integrity(next->bio)->bip_flags)
		return false;

	if (req->nr_integrity_segments + next->nr_integrity_segments >
	    q->limits.max_integrity_segments)
		return false;

	if (integrity_req_gap_back_merge(req, next->bio))
		return false;

	return true;
}

bool blk_integrity_merge_bio(struct request_queue *q, struct request *req,
			     struct bio *bio)
{
	int nr_integrity_segs;
	struct bio *next = bio->bi_next;

	if (blk_integrity_rq(req) == 0 && bio_integrity(bio) == NULL)
		return true;

	if (blk_integrity_rq(req) == 0 || bio_integrity(bio) == NULL)
		return false;

	if (bio_integrity(req->bio)->bip_flags != bio_integrity(bio)->bip_flags)
		return false;

	bio->bi_next = NULL;
	nr_integrity_segs = blk_rq_count_integrity_sg(q, bio);
	bio->bi_next = next;

	if (req->nr_integrity_segments + nr_integrity_segs >
	    q->limits.max_integrity_segments)
		return false;

	req->nr_integrity_segments += nr_integrity_segs;

	return true;
}

struct integrity_sysfs_entry {
	struct attribute attr;
	ssize_t (*show)(struct blk_integrity *, char *);
	ssize_t (*store)(struct blk_integrity *, const char *, size_t);
};

static ssize_t integrity_attr_show(struct kobject *kobj, struct attribute *attr,
				   char *page)
{
	struct gendisk *disk = container_of(kobj, struct gendisk, integrity_kobj);
	struct blk_integrity *bi = &disk->queue->integrity;
	struct integrity_sysfs_entry *entry =
		container_of(attr, struct integrity_sysfs_entry, attr);

	return entry->show(bi, page);
}

static ssize_t integrity_attr_store(struct kobject *kobj,
				    struct attribute *attr, const char *page,
				    size_t count)
{
	struct gendisk *disk = container_of(kobj, struct gendisk, integrity_kobj);
	struct blk_integrity *bi = &disk->queue->integrity;
	struct integrity_sysfs_entry *entry =
		container_of(attr, struct integrity_sysfs_entry, attr);
	ssize_t ret = 0;

	if (entry->store)
		ret = entry->store(bi, page, count);

	return ret;
}

static ssize_t integrity_format_show(struct blk_integrity *bi, char *page)
{
	if (bi->profile && bi->profile->name)
		return sprintf(page, "%s\n", bi->profile->name);
	else
		return sprintf(page, "none\n");
}

static ssize_t integrity_tag_size_show(struct blk_integrity *bi, char *page)
{
	return sprintf(page, "%u\n", bi->tag_size);
}

static ssize_t integrity_interval_show(struct blk_integrity *bi, char *page)
{
	return sprintf(page, "%u\n",
		       bi->interval_exp ? 1 << bi->interval_exp : 0);
}

static ssize_t integrity_verify_store(struct blk_integrity *bi,
				      const char *page, size_t count)
{
	char *p = (char *) page;
	unsigned long val = simple_strtoul(p, &p, 10);

	if (val)
		bi->flags |= BLK_INTEGRITY_VERIFY;
	else
		bi->flags &= ~BLK_INTEGRITY_VERIFY;

	return count;
}

static ssize_t integrity_verify_show(struct blk_integrity *bi, char *page)
{
	return sprintf(page, "%d\n", (bi->flags & BLK_INTEGRITY_VERIFY) != 0);
}

static ssize_t integrity_generate_store(struct blk_integrity *bi,
					const char *page, size_t count)
{
	char *p = (char *) page;
	unsigned long val = simple_strtoul(p, &p, 10);

	if (val)
		bi->flags |= BLK_INTEGRITY_GENERATE;
	else
		bi->flags &= ~BLK_INTEGRITY_GENERATE;

	return count;
}

static ssize_t integrity_generate_show(struct blk_integrity *bi, char *page)
{
	return sprintf(page, "%d\n", (bi->flags & BLK_INTEGRITY_GENERATE) != 0);
}

static ssize_t integrity_device_show(struct blk_integrity *bi, char *page)
{
	return sprintf(page, "%u\n",
		       (bi->flags & BLK_INTEGRITY_DEVICE_CAPABLE) != 0);
}

static struct integrity_sysfs_entry integrity_format_entry = {
	.attr = { .name = "format", .mode = 0444 },
	.show = integrity_format_show,
};

static struct integrity_sysfs_entry integrity_tag_size_entry = {
	.attr = { .name = "tag_size", .mode = 0444 },
	.show = integrity_tag_size_show,
};

static struct integrity_sysfs_entry integrity_interval_entry = {
	.attr = { .name = "protection_interval_bytes", .mode = 0444 },
	.show = integrity_interval_show,
};

static struct integrity_sysfs_entry integrity_verify_entry = {
	.attr = { .name = "read_verify", .mode = 0644 },
	.show = integrity_verify_show,
	.store = integrity_verify_store,
};

static struct integrity_sysfs_entry integrity_generate_entry = {
	.attr = { .name = "write_generate", .mode = 0644 },
	.show = integrity_generate_show,
	.store = integrity_generate_store,
};

static struct integrity_sysfs_entry integrity_device_entry = {
	.attr = { .name = "device_is_integrity_capable", .mode = 0444 },
	.show = integrity_device_show,
};

static struct attribute *integrity_attrs[] = {
	&integrity_format_entry.attr,
	&integrity_tag_size_entry.attr,
	&integrity_interval_entry.attr,
	&integrity_verify_entry.attr,
	&integrity_generate_entry.attr,
	&integrity_device_entry.attr,
	NULL,
};
ATTRIBUTE_GROUPS(integrity);

static const struct sysfs_ops integrity_ops = {
	.show	= &integrity_attr_show,
	.store	= &integrity_attr_store,
};

static struct kobj_type integrity_ktype = {
	.default_groups = integrity_groups,
	.sysfs_ops	= &integrity_ops,
};

static blk_status_t blk_integrity_nop_fn(struct blk_integrity_iter *iter)
{
	return BLK_STS_OK;
}

static void blk_integrity_nop_prepare(struct request *rq)
{
}

static void blk_integrity_nop_complete(struct request *rq,
		unsigned int nr_bytes)
{
}

static const struct blk_integrity_profile nop_profile = {
	.name = "nop",
	.generate_fn = blk_integrity_nop_fn,
	.verify_fn = blk_integrity_nop_fn,
	.prepare_fn = blk_integrity_nop_prepare,
	.complete_fn = blk_integrity_nop_complete,
};

/**
 * blk_integrity_register - Register a gendisk as being integrity-capable
 * @disk:	struct gendisk pointer to make integrity-aware
 * @template:	block integrity profile to register
 *
 * Description: When a device needs to advertise itself as being able to
 * send/receive integrity metadata it must use this function to register
 * the capability with the block layer. The template is a blk_integrity
 * struct with values appropriate for the underlying hardware. See
 * Documentation/block/data-integrity.rst.
 */
void blk_integrity_register(struct gendisk *disk, struct blk_integrity *template)
{
	struct blk_integrity *bi = &disk->queue->integrity;

	bi->flags = BLK_INTEGRITY_VERIFY | BLK_INTEGRITY_GENERATE |
		template->flags;
	bi->interval_exp = template->interval_exp ? :
		ilog2(queue_logical_block_size(disk->queue));
	bi->profile = template->profile ? template->profile : &nop_profile;
	bi->tuple_size = template->tuple_size;
	bi->tag_size = template->tag_size;

	blk_queue_flag_set(QUEUE_FLAG_STABLE_WRITES, disk->queue);

#ifdef CONFIG_BLK_INLINE_ENCRYPTION
	if (disk->queue->ksm) {
		pr_warn("blk-integrity: Integrity and hardware inline encryption are not supported together. Disabling hardware inline encryption.\n");
		blk_ksm_unregister(disk->queue);
	}
#endif
}
EXPORT_SYMBOL(blk_integrity_register);

/**
 * blk_integrity_unregister - Unregister block integrity profile
 * @disk:	disk whose integrity profile to unregister
 *
 * Description: This function unregisters the integrity capability from
 * a block device.
 */
void blk_integrity_unregister(struct gendisk *disk)
{
	blk_queue_flag_clear(QUEUE_FLAG_STABLE_WRITES, disk->queue);
	memset(&disk->queue->integrity, 0, sizeof(struct blk_integrity));
}
EXPORT_SYMBOL(blk_integrity_unregister);

void blk_integrity_add(struct gendisk *disk)
{
	if (kobject_init_and_add(&disk->integrity_kobj, &integrity_ktype,
				 &disk_to_dev(disk)->kobj, "%s", "integrity"))
		return;

	kobject_uevent(&disk->integrity_kobj, KOBJ_ADD);
}

void blk_integrity_del(struct gendisk *disk)
{
	kobject_uevent(&disk->integrity_kobj, KOBJ_REMOVE);
	kobject_del(&disk->integrity_kobj);
	kobject_put(&disk->integrity_kobj);
}
