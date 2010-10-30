/*
 * blk-integrity.c - Block layer data integrity extensions
 *
 * Copyright (C) 2007, 2008 Oracle Corporation
 * Written by: Martin K. Petersen <martin.petersen@oracle.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139,
 * USA.
 *
 */

#include <linux/blkdev.h>
#include <linux/mempool.h>
#include <linux/bio.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>

#include "blk.h"

static struct kmem_cache *integrity_cachep;

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
	struct bio_vec *iv, *ivprv = NULL;
	unsigned int segments = 0;
	unsigned int seg_size = 0;
	unsigned int i = 0;

	bio_for_each_integrity_vec(iv, bio, i) {

		if (ivprv) {
			if (!BIOVEC_PHYS_MERGEABLE(ivprv, iv))
				goto new_segment;

			if (!BIOVEC_SEG_BOUNDARY(q, ivprv, iv))
				goto new_segment;

			if (seg_size + iv->bv_len > queue_max_segment_size(q))
				goto new_segment;

			seg_size += iv->bv_len;
		} else {
new_segment:
			segments++;
			seg_size = iv->bv_len;
		}

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
	struct bio_vec *iv, *ivprv = NULL;
	struct scatterlist *sg = NULL;
	unsigned int segments = 0;
	unsigned int i = 0;

	bio_for_each_integrity_vec(iv, bio, i) {

		if (ivprv) {
			if (!BIOVEC_PHYS_MERGEABLE(ivprv, iv))
				goto new_segment;

			if (!BIOVEC_SEG_BOUNDARY(q, ivprv, iv))
				goto new_segment;

			if (sg->length + iv->bv_len > queue_max_segment_size(q))
				goto new_segment;

			sg->length += iv->bv_len;
		} else {
new_segment:
			if (!sg)
				sg = sglist;
			else {
				sg->page_link &= ~0x02;
				sg = sg_next(sg);
			}

			sg_set_page(sg, iv->bv_page, iv->bv_len, iv->bv_offset);
			segments++;
		}

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
	struct blk_integrity *b1 = gd1->integrity;
	struct blk_integrity *b2 = gd2->integrity;

	if (!b1 && !b2)
		return 0;

	if (!b1 || !b2)
		return -1;

	if (b1->sector_size != b2->sector_size) {
		printk(KERN_ERR "%s: %s/%s sector sz %u != %u\n", __func__,
		       gd1->disk_name, gd2->disk_name,
		       b1->sector_size, b2->sector_size);
		return -1;
	}

	if (b1->tuple_size != b2->tuple_size) {
		printk(KERN_ERR "%s: %s/%s tuple sz %u != %u\n", __func__,
		       gd1->disk_name, gd2->disk_name,
		       b1->tuple_size, b2->tuple_size);
		return -1;
	}

	if (b1->tag_size && b2->tag_size && (b1->tag_size != b2->tag_size)) {
		printk(KERN_ERR "%s: %s/%s tag sz %u != %u\n", __func__,
		       gd1->disk_name, gd2->disk_name,
		       b1->tag_size, b2->tag_size);
		return -1;
	}

	if (strcmp(b1->name, b2->name)) {
		printk(KERN_ERR "%s: %s/%s type %s != %s\n", __func__,
		       gd1->disk_name, gd2->disk_name,
		       b1->name, b2->name);
		return -1;
	}

	return 0;
}
EXPORT_SYMBOL(blk_integrity_compare);

int blk_integrity_merge_rq(struct request_queue *q, struct request *req,
			   struct request *next)
{
	if (blk_integrity_rq(req) != blk_integrity_rq(next))
		return -1;

	if (req->nr_integrity_segments + next->nr_integrity_segments >
	    q->limits.max_integrity_segments)
		return -1;

	return 0;
}
EXPORT_SYMBOL(blk_integrity_merge_rq);

int blk_integrity_merge_bio(struct request_queue *q, struct request *req,
			    struct bio *bio)
{
	int nr_integrity_segs;
	struct bio *next = bio->bi_next;

	bio->bi_next = NULL;
	nr_integrity_segs = blk_rq_count_integrity_sg(q, bio);
	bio->bi_next = next;

	if (req->nr_integrity_segments + nr_integrity_segs >
	    q->limits.max_integrity_segments)
		return -1;

	req->nr_integrity_segments += nr_integrity_segs;

	return 0;
}
EXPORT_SYMBOL(blk_integrity_merge_bio);

struct integrity_sysfs_entry {
	struct attribute attr;
	ssize_t (*show)(struct blk_integrity *, char *);
	ssize_t (*store)(struct blk_integrity *, const char *, size_t);
};

static ssize_t integrity_attr_show(struct kobject *kobj, struct attribute *attr,
				   char *page)
{
	struct blk_integrity *bi =
		container_of(kobj, struct blk_integrity, kobj);
	struct integrity_sysfs_entry *entry =
		container_of(attr, struct integrity_sysfs_entry, attr);

	return entry->show(bi, page);
}

static ssize_t integrity_attr_store(struct kobject *kobj,
				    struct attribute *attr, const char *page,
				    size_t count)
{
	struct blk_integrity *bi =
		container_of(kobj, struct blk_integrity, kobj);
	struct integrity_sysfs_entry *entry =
		container_of(attr, struct integrity_sysfs_entry, attr);
	ssize_t ret = 0;

	if (entry->store)
		ret = entry->store(bi, page, count);

	return ret;
}

static ssize_t integrity_format_show(struct blk_integrity *bi, char *page)
{
	if (bi != NULL && bi->name != NULL)
		return sprintf(page, "%s\n", bi->name);
	else
		return sprintf(page, "none\n");
}

static ssize_t integrity_tag_size_show(struct blk_integrity *bi, char *page)
{
	if (bi != NULL)
		return sprintf(page, "%u\n", bi->tag_size);
	else
		return sprintf(page, "0\n");
}

static ssize_t integrity_read_store(struct blk_integrity *bi,
				    const char *page, size_t count)
{
	char *p = (char *) page;
	unsigned long val = simple_strtoul(p, &p, 10);

	if (val)
		bi->flags |= INTEGRITY_FLAG_READ;
	else
		bi->flags &= ~INTEGRITY_FLAG_READ;

	return count;
}

static ssize_t integrity_read_show(struct blk_integrity *bi, char *page)
{
	return sprintf(page, "%d\n", (bi->flags & INTEGRITY_FLAG_READ) != 0);
}

static ssize_t integrity_write_store(struct blk_integrity *bi,
				     const char *page, size_t count)
{
	char *p = (char *) page;
	unsigned long val = simple_strtoul(p, &p, 10);

	if (val)
		bi->flags |= INTEGRITY_FLAG_WRITE;
	else
		bi->flags &= ~INTEGRITY_FLAG_WRITE;

	return count;
}

static ssize_t integrity_write_show(struct blk_integrity *bi, char *page)
{
	return sprintf(page, "%d\n", (bi->flags & INTEGRITY_FLAG_WRITE) != 0);
}

static struct integrity_sysfs_entry integrity_format_entry = {
	.attr = { .name = "format", .mode = S_IRUGO },
	.show = integrity_format_show,
};

static struct integrity_sysfs_entry integrity_tag_size_entry = {
	.attr = { .name = "tag_size", .mode = S_IRUGO },
	.show = integrity_tag_size_show,
};

static struct integrity_sysfs_entry integrity_read_entry = {
	.attr = { .name = "read_verify", .mode = S_IRUGO | S_IWUSR },
	.show = integrity_read_show,
	.store = integrity_read_store,
};

static struct integrity_sysfs_entry integrity_write_entry = {
	.attr = { .name = "write_generate", .mode = S_IRUGO | S_IWUSR },
	.show = integrity_write_show,
	.store = integrity_write_store,
};

static struct attribute *integrity_attrs[] = {
	&integrity_format_entry.attr,
	&integrity_tag_size_entry.attr,
	&integrity_read_entry.attr,
	&integrity_write_entry.attr,
	NULL,
};

static const struct sysfs_ops integrity_ops = {
	.show	= &integrity_attr_show,
	.store	= &integrity_attr_store,
};

static int __init blk_dev_integrity_init(void)
{
	integrity_cachep = kmem_cache_create("blkdev_integrity",
					     sizeof(struct blk_integrity),
					     0, SLAB_PANIC, NULL);
	return 0;
}
subsys_initcall(blk_dev_integrity_init);

static void blk_integrity_release(struct kobject *kobj)
{
	struct blk_integrity *bi =
		container_of(kobj, struct blk_integrity, kobj);

	kmem_cache_free(integrity_cachep, bi);
}

static struct kobj_type integrity_ktype = {
	.default_attrs	= integrity_attrs,
	.sysfs_ops	= &integrity_ops,
	.release	= blk_integrity_release,
};

/**
 * blk_integrity_register - Register a gendisk as being integrity-capable
 * @disk:	struct gendisk pointer to make integrity-aware
 * @template:	optional integrity profile to register
 *
 * Description: When a device needs to advertise itself as being able
 * to send/receive integrity metadata it must use this function to
 * register the capability with the block layer.  The template is a
 * blk_integrity struct with values appropriate for the underlying
 * hardware.  If template is NULL the new profile is allocated but
 * not filled out. See Documentation/block/data-integrity.txt.
 */
int blk_integrity_register(struct gendisk *disk, struct blk_integrity *template)
{
	struct blk_integrity *bi;

	BUG_ON(disk == NULL);

	if (disk->integrity == NULL) {
		bi = kmem_cache_alloc(integrity_cachep,
				      GFP_KERNEL | __GFP_ZERO);
		if (!bi)
			return -1;

		if (kobject_init_and_add(&bi->kobj, &integrity_ktype,
					 &disk_to_dev(disk)->kobj,
					 "%s", "integrity")) {
			kmem_cache_free(integrity_cachep, bi);
			return -1;
		}

		kobject_uevent(&bi->kobj, KOBJ_ADD);

		bi->flags |= INTEGRITY_FLAG_READ | INTEGRITY_FLAG_WRITE;
		bi->sector_size = queue_logical_block_size(disk->queue);
		disk->integrity = bi;
	} else
		bi = disk->integrity;

	/* Use the provided profile as template */
	if (template != NULL) {
		bi->name = template->name;
		bi->generate_fn = template->generate_fn;
		bi->verify_fn = template->verify_fn;
		bi->tuple_size = template->tuple_size;
		bi->set_tag_fn = template->set_tag_fn;
		bi->get_tag_fn = template->get_tag_fn;
		bi->tag_size = template->tag_size;
	} else
		bi->name = "unsupported";

	return 0;
}
EXPORT_SYMBOL(blk_integrity_register);

/**
 * blk_integrity_unregister - Remove block integrity profile
 * @disk:	disk whose integrity profile to deallocate
 *
 * Description: This function frees all memory used by the block
 * integrity profile.  To be called at device teardown.
 */
void blk_integrity_unregister(struct gendisk *disk)
{
	struct blk_integrity *bi;

	if (!disk || !disk->integrity)
		return;

	bi = disk->integrity;

	kobject_uevent(&bi->kobj, KOBJ_REMOVE);
	kobject_del(&bi->kobj);
	kobject_put(&bi->kobj);
	disk->integrity = NULL;
}
EXPORT_SYMBOL(blk_integrity_unregister);
