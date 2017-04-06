/*
 * blkfront.c
 *
 * XenLinux virtual block device driver.
 *
 * Copyright (c) 2003-2004, Keir Fraser & Steve Hand
 * Modifications by Mark A. Williamson are (c) Intel Research Cambridge
 * Copyright (c) 2004, Christian Limpach
 * Copyright (c) 2004, Andrew Warfield
 * Copyright (c) 2005, Christopher Clark
 * Copyright (c) 2005, XenSource Ltd
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/hdreg.h>
#include <linux/cdrom.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/scatterlist.h>
#include <linux/bitmap.h>
#include <linux/list.h>

#include <xen/xen.h>
#include <xen/xenbus.h>
#include <xen/grant_table.h>
#include <xen/events.h>
#include <xen/page.h>
#include <xen/platform_pci.h>

#include <xen/interface/grant_table.h>
#include <xen/interface/io/blkif.h>
#include <xen/interface/io/protocols.h>

#include <asm/xen/hypervisor.h>

/*
 * The minimal size of segment supported by the block framework is PAGE_SIZE.
 * When Linux is using a different page size than Xen, it may not be possible
 * to put all the data in a single segment.
 * This can happen when the backend doesn't support indirect descriptor and
 * therefore the maximum amount of data that a request can carry is
 * BLKIF_MAX_SEGMENTS_PER_REQUEST * XEN_PAGE_SIZE = 44KB
 *
 * Note that we only support one extra request. So the Linux page size
 * should be <= ( 2 * BLKIF_MAX_SEGMENTS_PER_REQUEST * XEN_PAGE_SIZE) =
 * 88KB.
 */
#define HAS_EXTRA_REQ (BLKIF_MAX_SEGMENTS_PER_REQUEST < XEN_PFN_PER_PAGE)

enum blkif_state {
	BLKIF_STATE_DISCONNECTED,
	BLKIF_STATE_CONNECTED,
	BLKIF_STATE_SUSPENDED,
};

struct grant {
	grant_ref_t gref;
	struct page *page;
	struct list_head node;
};

enum blk_req_status {
	REQ_WAITING,
	REQ_DONE,
	REQ_ERROR,
	REQ_EOPNOTSUPP,
};

struct blk_shadow {
	struct blkif_request req;
	struct request *request;
	struct grant **grants_used;
	struct grant **indirect_grants;
	struct scatterlist *sg;
	unsigned int num_sg;
	enum blk_req_status status;

	#define NO_ASSOCIATED_ID ~0UL
	/*
	 * Id of the sibling if we ever need 2 requests when handling a
	 * block I/O request
	 */
	unsigned long associated_id;
};

struct split_bio {
	struct bio *bio;
	atomic_t pending;
};

static DEFINE_MUTEX(blkfront_mutex);
static const struct block_device_operations xlvbd_block_fops;

/*
 * Maximum number of segments in indirect requests, the actual value used by
 * the frontend driver is the minimum of this value and the value provided
 * by the backend driver.
 */

static unsigned int xen_blkif_max_segments = 32;
module_param_named(max_indirect_segments, xen_blkif_max_segments, uint,
		   S_IRUGO);
MODULE_PARM_DESC(max_indirect_segments,
		 "Maximum amount of segments in indirect requests (default is 32)");

static unsigned int xen_blkif_max_queues = 4;
module_param_named(max_queues, xen_blkif_max_queues, uint, S_IRUGO);
MODULE_PARM_DESC(max_queues, "Maximum number of hardware queues/rings used per virtual disk");

/*
 * Maximum order of pages to be used for the shared ring between front and
 * backend, 4KB page granularity is used.
 */
static unsigned int xen_blkif_max_ring_order;
module_param_named(max_ring_page_order, xen_blkif_max_ring_order, int, S_IRUGO);
MODULE_PARM_DESC(max_ring_page_order, "Maximum order of pages to be used for the shared ring");

#define BLK_RING_SIZE(info)	\
	__CONST_RING_SIZE(blkif, XEN_PAGE_SIZE * (info)->nr_ring_pages)

#define BLK_MAX_RING_SIZE	\
	__CONST_RING_SIZE(blkif, XEN_PAGE_SIZE * XENBUS_MAX_RING_GRANTS)

/*
 * ring-ref%u i=(-1UL) would take 11 characters + 'ring-ref' is 8, so 19
 * characters are enough. Define to 20 to keep consistent with backend.
 */
#define RINGREF_NAME_LEN (20)
/*
 * queue-%u would take 7 + 10(UINT_MAX) = 17 characters.
 */
#define QUEUE_NAME_LEN (17)

/*
 *  Per-ring info.
 *  Every blkfront device can associate with one or more blkfront_ring_info,
 *  depending on how many hardware queues/rings to be used.
 */
struct blkfront_ring_info {
	/* Lock to protect data in every ring buffer. */
	spinlock_t ring_lock;
	struct blkif_front_ring ring;
	unsigned int ring_ref[XENBUS_MAX_RING_GRANTS];
	unsigned int evtchn, irq;
	struct work_struct work;
	struct gnttab_free_callback callback;
	struct blk_shadow shadow[BLK_MAX_RING_SIZE];
	struct list_head indirect_pages;
	struct list_head grants;
	unsigned int persistent_gnts_c;
	unsigned long shadow_free;
	struct blkfront_info *dev_info;
};

/*
 * We have one of these per vbd, whether ide, scsi or 'other'.  They
 * hang in private_data off the gendisk structure. We may end up
 * putting all kinds of interesting stuff here :-)
 */
struct blkfront_info
{
	struct mutex mutex;
	struct xenbus_device *xbdev;
	struct gendisk *gd;
	u16 sector_size;
	unsigned int physical_sector_size;
	int vdevice;
	blkif_vdev_t handle;
	enum blkif_state connected;
	/* Number of pages per ring buffer. */
	unsigned int nr_ring_pages;
	struct request_queue *rq;
	unsigned int feature_flush:1;
	unsigned int feature_fua:1;
	unsigned int feature_discard:1;
	unsigned int feature_secdiscard:1;
	unsigned int feature_persistent:1;
	unsigned int discard_granularity;
	unsigned int discard_alignment;
	/* Number of 4KB segments handled */
	unsigned int max_indirect_segments;
	int is_ready;
	struct blk_mq_tag_set tag_set;
	struct blkfront_ring_info *rinfo;
	unsigned int nr_rings;
	/* Save uncomplete reqs and bios for migration. */
	struct list_head requests;
	struct bio_list bio_list;
};

static unsigned int nr_minors;
static unsigned long *minors;
static DEFINE_SPINLOCK(minor_lock);

#define GRANT_INVALID_REF	0

#define PARTS_PER_DISK		16
#define PARTS_PER_EXT_DISK      256

#define BLKIF_MAJOR(dev) ((dev)>>8)
#define BLKIF_MINOR(dev) ((dev) & 0xff)

#define EXT_SHIFT 28
#define EXTENDED (1<<EXT_SHIFT)
#define VDEV_IS_EXTENDED(dev) ((dev)&(EXTENDED))
#define BLKIF_MINOR_EXT(dev) ((dev)&(~EXTENDED))
#define EMULATED_HD_DISK_MINOR_OFFSET (0)
#define EMULATED_HD_DISK_NAME_OFFSET (EMULATED_HD_DISK_MINOR_OFFSET / 256)
#define EMULATED_SD_DISK_MINOR_OFFSET (0)
#define EMULATED_SD_DISK_NAME_OFFSET (EMULATED_SD_DISK_MINOR_OFFSET / 256)

#define DEV_NAME	"xvd"	/* name in /dev */

/*
 * Grants are always the same size as a Xen page (i.e 4KB).
 * A physical segment is always the same size as a Linux page.
 * Number of grants per physical segment
 */
#define GRANTS_PER_PSEG	(PAGE_SIZE / XEN_PAGE_SIZE)

#define GRANTS_PER_INDIRECT_FRAME \
	(XEN_PAGE_SIZE / sizeof(struct blkif_request_segment))

#define PSEGS_PER_INDIRECT_FRAME	\
	(GRANTS_INDIRECT_FRAME / GRANTS_PSEGS)

#define INDIRECT_GREFS(_grants)		\
	DIV_ROUND_UP(_grants, GRANTS_PER_INDIRECT_FRAME)

#define GREFS(_psegs)	((_psegs) * GRANTS_PER_PSEG)

static int blkfront_setup_indirect(struct blkfront_ring_info *rinfo);
static void blkfront_gather_backend_features(struct blkfront_info *info);

static int get_id_from_freelist(struct blkfront_ring_info *rinfo)
{
	unsigned long free = rinfo->shadow_free;

	BUG_ON(free >= BLK_RING_SIZE(rinfo->dev_info));
	rinfo->shadow_free = rinfo->shadow[free].req.u.rw.id;
	rinfo->shadow[free].req.u.rw.id = 0x0fffffee; /* debug */
	return free;
}

static int add_id_to_freelist(struct blkfront_ring_info *rinfo,
			      unsigned long id)
{
	if (rinfo->shadow[id].req.u.rw.id != id)
		return -EINVAL;
	if (rinfo->shadow[id].request == NULL)
		return -EINVAL;
	rinfo->shadow[id].req.u.rw.id  = rinfo->shadow_free;
	rinfo->shadow[id].request = NULL;
	rinfo->shadow_free = id;
	return 0;
}

static int fill_grant_buffer(struct blkfront_ring_info *rinfo, int num)
{
	struct blkfront_info *info = rinfo->dev_info;
	struct page *granted_page;
	struct grant *gnt_list_entry, *n;
	int i = 0;

	while (i < num) {
		gnt_list_entry = kzalloc(sizeof(struct grant), GFP_NOIO);
		if (!gnt_list_entry)
			goto out_of_memory;

		if (info->feature_persistent) {
			granted_page = alloc_page(GFP_NOIO);
			if (!granted_page) {
				kfree(gnt_list_entry);
				goto out_of_memory;
			}
			gnt_list_entry->page = granted_page;
		}

		gnt_list_entry->gref = GRANT_INVALID_REF;
		list_add(&gnt_list_entry->node, &rinfo->grants);
		i++;
	}

	return 0;

out_of_memory:
	list_for_each_entry_safe(gnt_list_entry, n,
	                         &rinfo->grants, node) {
		list_del(&gnt_list_entry->node);
		if (info->feature_persistent)
			__free_page(gnt_list_entry->page);
		kfree(gnt_list_entry);
		i--;
	}
	BUG_ON(i != 0);
	return -ENOMEM;
}

static struct grant *get_free_grant(struct blkfront_ring_info *rinfo)
{
	struct grant *gnt_list_entry;

	BUG_ON(list_empty(&rinfo->grants));
	gnt_list_entry = list_first_entry(&rinfo->grants, struct grant,
					  node);
	list_del(&gnt_list_entry->node);

	if (gnt_list_entry->gref != GRANT_INVALID_REF)
		rinfo->persistent_gnts_c--;

	return gnt_list_entry;
}

static inline void grant_foreign_access(const struct grant *gnt_list_entry,
					const struct blkfront_info *info)
{
	gnttab_page_grant_foreign_access_ref_one(gnt_list_entry->gref,
						 info->xbdev->otherend_id,
						 gnt_list_entry->page,
						 0);
}

static struct grant *get_grant(grant_ref_t *gref_head,
			       unsigned long gfn,
			       struct blkfront_ring_info *rinfo)
{
	struct grant *gnt_list_entry = get_free_grant(rinfo);
	struct blkfront_info *info = rinfo->dev_info;

	if (gnt_list_entry->gref != GRANT_INVALID_REF)
		return gnt_list_entry;

	/* Assign a gref to this page */
	gnt_list_entry->gref = gnttab_claim_grant_reference(gref_head);
	BUG_ON(gnt_list_entry->gref == -ENOSPC);
	if (info->feature_persistent)
		grant_foreign_access(gnt_list_entry, info);
	else {
		/* Grant access to the GFN passed by the caller */
		gnttab_grant_foreign_access_ref(gnt_list_entry->gref,
						info->xbdev->otherend_id,
						gfn, 0);
	}

	return gnt_list_entry;
}

static struct grant *get_indirect_grant(grant_ref_t *gref_head,
					struct blkfront_ring_info *rinfo)
{
	struct grant *gnt_list_entry = get_free_grant(rinfo);
	struct blkfront_info *info = rinfo->dev_info;

	if (gnt_list_entry->gref != GRANT_INVALID_REF)
		return gnt_list_entry;

	/* Assign a gref to this page */
	gnt_list_entry->gref = gnttab_claim_grant_reference(gref_head);
	BUG_ON(gnt_list_entry->gref == -ENOSPC);
	if (!info->feature_persistent) {
		struct page *indirect_page;

		/* Fetch a pre-allocated page to use for indirect grefs */
		BUG_ON(list_empty(&rinfo->indirect_pages));
		indirect_page = list_first_entry(&rinfo->indirect_pages,
						 struct page, lru);
		list_del(&indirect_page->lru);
		gnt_list_entry->page = indirect_page;
	}
	grant_foreign_access(gnt_list_entry, info);

	return gnt_list_entry;
}

static const char *op_name(int op)
{
	static const char *const names[] = {
		[BLKIF_OP_READ] = "read",
		[BLKIF_OP_WRITE] = "write",
		[BLKIF_OP_WRITE_BARRIER] = "barrier",
		[BLKIF_OP_FLUSH_DISKCACHE] = "flush",
		[BLKIF_OP_DISCARD] = "discard" };

	if (op < 0 || op >= ARRAY_SIZE(names))
		return "unknown";

	if (!names[op])
		return "reserved";

	return names[op];
}
static int xlbd_reserve_minors(unsigned int minor, unsigned int nr)
{
	unsigned int end = minor + nr;
	int rc;

	if (end > nr_minors) {
		unsigned long *bitmap, *old;

		bitmap = kcalloc(BITS_TO_LONGS(end), sizeof(*bitmap),
				 GFP_KERNEL);
		if (bitmap == NULL)
			return -ENOMEM;

		spin_lock(&minor_lock);
		if (end > nr_minors) {
			old = minors;
			memcpy(bitmap, minors,
			       BITS_TO_LONGS(nr_minors) * sizeof(*bitmap));
			minors = bitmap;
			nr_minors = BITS_TO_LONGS(end) * BITS_PER_LONG;
		} else
			old = bitmap;
		spin_unlock(&minor_lock);
		kfree(old);
	}

	spin_lock(&minor_lock);
	if (find_next_bit(minors, end, minor) >= end) {
		bitmap_set(minors, minor, nr);
		rc = 0;
	} else
		rc = -EBUSY;
	spin_unlock(&minor_lock);

	return rc;
}

static void xlbd_release_minors(unsigned int minor, unsigned int nr)
{
	unsigned int end = minor + nr;

	BUG_ON(end > nr_minors);
	spin_lock(&minor_lock);
	bitmap_clear(minors,  minor, nr);
	spin_unlock(&minor_lock);
}

static void blkif_restart_queue_callback(void *arg)
{
	struct blkfront_ring_info *rinfo = (struct blkfront_ring_info *)arg;
	schedule_work(&rinfo->work);
}

static int blkif_getgeo(struct block_device *bd, struct hd_geometry *hg)
{
	/* We don't have real geometry info, but let's at least return
	   values consistent with the size of the device */
	sector_t nsect = get_capacity(bd->bd_disk);
	sector_t cylinders = nsect;

	hg->heads = 0xff;
	hg->sectors = 0x3f;
	sector_div(cylinders, hg->heads * hg->sectors);
	hg->cylinders = cylinders;
	if ((sector_t)(hg->cylinders + 1) * hg->heads * hg->sectors < nsect)
		hg->cylinders = 0xffff;
	return 0;
}

static int blkif_ioctl(struct block_device *bdev, fmode_t mode,
		       unsigned command, unsigned long argument)
{
	struct blkfront_info *info = bdev->bd_disk->private_data;
	int i;

	dev_dbg(&info->xbdev->dev, "command: 0x%x, argument: 0x%lx\n",
		command, (long)argument);

	switch (command) {
	case CDROMMULTISESSION:
		dev_dbg(&info->xbdev->dev, "FIXME: support multisession CDs later\n");
		for (i = 0; i < sizeof(struct cdrom_multisession); i++)
			if (put_user(0, (char __user *)(argument + i)))
				return -EFAULT;
		return 0;

	case CDROM_GET_CAPABILITY: {
		struct gendisk *gd = info->gd;
		if (gd->flags & GENHD_FL_CD)
			return 0;
		return -EINVAL;
	}

	default:
		/*printk(KERN_ALERT "ioctl %08x not supported by Xen blkdev\n",
		  command);*/
		return -EINVAL; /* same return as native Linux */
	}

	return 0;
}

static unsigned long blkif_ring_get_request(struct blkfront_ring_info *rinfo,
					    struct request *req,
					    struct blkif_request **ring_req)
{
	unsigned long id;

	*ring_req = RING_GET_REQUEST(&rinfo->ring, rinfo->ring.req_prod_pvt);
	rinfo->ring.req_prod_pvt++;

	id = get_id_from_freelist(rinfo);
	rinfo->shadow[id].request = req;
	rinfo->shadow[id].status = REQ_WAITING;
	rinfo->shadow[id].associated_id = NO_ASSOCIATED_ID;

	(*ring_req)->u.rw.id = id;

	return id;
}

static int blkif_queue_discard_req(struct request *req, struct blkfront_ring_info *rinfo)
{
	struct blkfront_info *info = rinfo->dev_info;
	struct blkif_request *ring_req;
	unsigned long id;

	/* Fill out a communications ring structure. */
	id = blkif_ring_get_request(rinfo, req, &ring_req);

	ring_req->operation = BLKIF_OP_DISCARD;
	ring_req->u.discard.nr_sectors = blk_rq_sectors(req);
	ring_req->u.discard.id = id;
	ring_req->u.discard.sector_number = (blkif_sector_t)blk_rq_pos(req);
	if (req_op(req) == REQ_OP_SECURE_ERASE && info->feature_secdiscard)
		ring_req->u.discard.flag = BLKIF_DISCARD_SECURE;
	else
		ring_req->u.discard.flag = 0;

	/* Keep a private copy so we can reissue requests when recovering. */
	rinfo->shadow[id].req = *ring_req;

	return 0;
}

struct setup_rw_req {
	unsigned int grant_idx;
	struct blkif_request_segment *segments;
	struct blkfront_ring_info *rinfo;
	struct blkif_request *ring_req;
	grant_ref_t gref_head;
	unsigned int id;
	/* Only used when persistent grant is used and it's a read request */
	bool need_copy;
	unsigned int bvec_off;
	char *bvec_data;

	bool require_extra_req;
	struct blkif_request *extra_ring_req;
};

static void blkif_setup_rw_req_grant(unsigned long gfn, unsigned int offset,
				     unsigned int len, void *data)
{
	struct setup_rw_req *setup = data;
	int n, ref;
	struct grant *gnt_list_entry;
	unsigned int fsect, lsect;
	/* Convenient aliases */
	unsigned int grant_idx = setup->grant_idx;
	struct blkif_request *ring_req = setup->ring_req;
	struct blkfront_ring_info *rinfo = setup->rinfo;
	/*
	 * We always use the shadow of the first request to store the list
	 * of grant associated to the block I/O request. This made the
	 * completion more easy to handle even if the block I/O request is
	 * split.
	 */
	struct blk_shadow *shadow = &rinfo->shadow[setup->id];

	if (unlikely(setup->require_extra_req &&
		     grant_idx >= BLKIF_MAX_SEGMENTS_PER_REQUEST)) {
		/*
		 * We are using the second request, setup grant_idx
		 * to be the index of the segment array.
		 */
		grant_idx -= BLKIF_MAX_SEGMENTS_PER_REQUEST;
		ring_req = setup->extra_ring_req;
	}

	if ((ring_req->operation == BLKIF_OP_INDIRECT) &&
	    (grant_idx % GRANTS_PER_INDIRECT_FRAME == 0)) {
		if (setup->segments)
			kunmap_atomic(setup->segments);

		n = grant_idx / GRANTS_PER_INDIRECT_FRAME;
		gnt_list_entry = get_indirect_grant(&setup->gref_head, rinfo);
		shadow->indirect_grants[n] = gnt_list_entry;
		setup->segments = kmap_atomic(gnt_list_entry->page);
		ring_req->u.indirect.indirect_grefs[n] = gnt_list_entry->gref;
	}

	gnt_list_entry = get_grant(&setup->gref_head, gfn, rinfo);
	ref = gnt_list_entry->gref;
	/*
	 * All the grants are stored in the shadow of the first
	 * request. Therefore we have to use the global index.
	 */
	shadow->grants_used[setup->grant_idx] = gnt_list_entry;

	if (setup->need_copy) {
		void *shared_data;

		shared_data = kmap_atomic(gnt_list_entry->page);
		/*
		 * this does not wipe data stored outside the
		 * range sg->offset..sg->offset+sg->length.
		 * Therefore, blkback *could* see data from
		 * previous requests. This is OK as long as
		 * persistent grants are shared with just one
		 * domain. It may need refactoring if this
		 * changes
		 */
		memcpy(shared_data + offset,
		       setup->bvec_data + setup->bvec_off,
		       len);

		kunmap_atomic(shared_data);
		setup->bvec_off += len;
	}

	fsect = offset >> 9;
	lsect = fsect + (len >> 9) - 1;
	if (ring_req->operation != BLKIF_OP_INDIRECT) {
		ring_req->u.rw.seg[grant_idx] =
			(struct blkif_request_segment) {
				.gref       = ref,
				.first_sect = fsect,
				.last_sect  = lsect };
	} else {
		setup->segments[grant_idx % GRANTS_PER_INDIRECT_FRAME] =
			(struct blkif_request_segment) {
				.gref       = ref,
				.first_sect = fsect,
				.last_sect  = lsect };
	}

	(setup->grant_idx)++;
}

static void blkif_setup_extra_req(struct blkif_request *first,
				  struct blkif_request *second)
{
	uint16_t nr_segments = first->u.rw.nr_segments;

	/*
	 * The second request is only present when the first request uses
	 * all its segments. It's always the continuity of the first one.
	 */
	first->u.rw.nr_segments = BLKIF_MAX_SEGMENTS_PER_REQUEST;

	second->u.rw.nr_segments = nr_segments - BLKIF_MAX_SEGMENTS_PER_REQUEST;
	second->u.rw.sector_number = first->u.rw.sector_number +
		(BLKIF_MAX_SEGMENTS_PER_REQUEST * XEN_PAGE_SIZE) / 512;

	second->u.rw.handle = first->u.rw.handle;
	second->operation = first->operation;
}

static int blkif_queue_rw_req(struct request *req, struct blkfront_ring_info *rinfo)
{
	struct blkfront_info *info = rinfo->dev_info;
	struct blkif_request *ring_req, *extra_ring_req = NULL;
	unsigned long id, extra_id = NO_ASSOCIATED_ID;
	bool require_extra_req = false;
	int i;
	struct setup_rw_req setup = {
		.grant_idx = 0,
		.segments = NULL,
		.rinfo = rinfo,
		.need_copy = rq_data_dir(req) && info->feature_persistent,
	};

	/*
	 * Used to store if we are able to queue the request by just using
	 * existing persistent grants, or if we have to get new grants,
	 * as there are not sufficiently many free.
	 */
	struct scatterlist *sg;
	int num_sg, max_grefs, num_grant;

	max_grefs = req->nr_phys_segments * GRANTS_PER_PSEG;
	if (max_grefs > BLKIF_MAX_SEGMENTS_PER_REQUEST)
		/*
		 * If we are using indirect segments we need to account
		 * for the indirect grefs used in the request.
		 */
		max_grefs += INDIRECT_GREFS(max_grefs);

	/*
	 * We have to reserve 'max_grefs' grants because persistent
	 * grants are shared by all rings.
	 */
	if (max_grefs > 0)
		if (gnttab_alloc_grant_references(max_grefs, &setup.gref_head) < 0) {
			gnttab_request_free_callback(
				&rinfo->callback,
				blkif_restart_queue_callback,
				rinfo,
				max_grefs);
			return 1;
		}

	/* Fill out a communications ring structure. */
	id = blkif_ring_get_request(rinfo, req, &ring_req);

	num_sg = blk_rq_map_sg(req->q, req, rinfo->shadow[id].sg);
	num_grant = 0;
	/* Calculate the number of grant used */
	for_each_sg(rinfo->shadow[id].sg, sg, num_sg, i)
	       num_grant += gnttab_count_grant(sg->offset, sg->length);

	require_extra_req = info->max_indirect_segments == 0 &&
		num_grant > BLKIF_MAX_SEGMENTS_PER_REQUEST;
	BUG_ON(!HAS_EXTRA_REQ && require_extra_req);

	rinfo->shadow[id].num_sg = num_sg;
	if (num_grant > BLKIF_MAX_SEGMENTS_PER_REQUEST &&
	    likely(!require_extra_req)) {
		/*
		 * The indirect operation can only be a BLKIF_OP_READ or
		 * BLKIF_OP_WRITE
		 */
		BUG_ON(req_op(req) == REQ_OP_FLUSH || req->cmd_flags & REQ_FUA);
		ring_req->operation = BLKIF_OP_INDIRECT;
		ring_req->u.indirect.indirect_op = rq_data_dir(req) ?
			BLKIF_OP_WRITE : BLKIF_OP_READ;
		ring_req->u.indirect.sector_number = (blkif_sector_t)blk_rq_pos(req);
		ring_req->u.indirect.handle = info->handle;
		ring_req->u.indirect.nr_segments = num_grant;
	} else {
		ring_req->u.rw.sector_number = (blkif_sector_t)blk_rq_pos(req);
		ring_req->u.rw.handle = info->handle;
		ring_req->operation = rq_data_dir(req) ?
			BLKIF_OP_WRITE : BLKIF_OP_READ;
		if (req_op(req) == REQ_OP_FLUSH || req->cmd_flags & REQ_FUA) {
			/*
			 * Ideally we can do an unordered flush-to-disk.
			 * In case the backend onlysupports barriers, use that.
			 * A barrier request a superset of FUA, so we can
			 * implement it the same way.  (It's also a FLUSH+FUA,
			 * since it is guaranteed ordered WRT previous writes.)
			 */
			if (info->feature_flush && info->feature_fua)
				ring_req->operation =
					BLKIF_OP_WRITE_BARRIER;
			else if (info->feature_flush)
				ring_req->operation =
					BLKIF_OP_FLUSH_DISKCACHE;
			else
				ring_req->operation = 0;
		}
		ring_req->u.rw.nr_segments = num_grant;
		if (unlikely(require_extra_req)) {
			extra_id = blkif_ring_get_request(rinfo, req,
							  &extra_ring_req);
			/*
			 * Only the first request contains the scatter-gather
			 * list.
			 */
			rinfo->shadow[extra_id].num_sg = 0;

			blkif_setup_extra_req(ring_req, extra_ring_req);

			/* Link the 2 requests together */
			rinfo->shadow[extra_id].associated_id = id;
			rinfo->shadow[id].associated_id = extra_id;
		}
	}

	setup.ring_req = ring_req;
	setup.id = id;

	setup.require_extra_req = require_extra_req;
	if (unlikely(require_extra_req))
		setup.extra_ring_req = extra_ring_req;

	for_each_sg(rinfo->shadow[id].sg, sg, num_sg, i) {
		BUG_ON(sg->offset + sg->length > PAGE_SIZE);

		if (setup.need_copy) {
			setup.bvec_off = sg->offset;
			setup.bvec_data = kmap_atomic(sg_page(sg));
		}

		gnttab_foreach_grant_in_range(sg_page(sg),
					      sg->offset,
					      sg->length,
					      blkif_setup_rw_req_grant,
					      &setup);

		if (setup.need_copy)
			kunmap_atomic(setup.bvec_data);
	}
	if (setup.segments)
		kunmap_atomic(setup.segments);

	/* Keep a private copy so we can reissue requests when recovering. */
	rinfo->shadow[id].req = *ring_req;
	if (unlikely(require_extra_req))
		rinfo->shadow[extra_id].req = *extra_ring_req;

	if (max_grefs > 0)
		gnttab_free_grant_references(setup.gref_head);

	return 0;
}

/*
 * Generate a Xen blkfront IO request from a blk layer request.  Reads
 * and writes are handled as expected.
 *
 * @req: a request struct
 */
static int blkif_queue_request(struct request *req, struct blkfront_ring_info *rinfo)
{
	if (unlikely(rinfo->dev_info->connected != BLKIF_STATE_CONNECTED))
		return 1;

	if (unlikely(req_op(req) == REQ_OP_DISCARD ||
		     req_op(req) == REQ_OP_SECURE_ERASE))
		return blkif_queue_discard_req(req, rinfo);
	else
		return blkif_queue_rw_req(req, rinfo);
}

static inline void flush_requests(struct blkfront_ring_info *rinfo)
{
	int notify;

	RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(&rinfo->ring, notify);

	if (notify)
		notify_remote_via_irq(rinfo->irq);
}

static inline bool blkif_request_flush_invalid(struct request *req,
					       struct blkfront_info *info)
{
	return (blk_rq_is_passthrough(req) ||
		((req_op(req) == REQ_OP_FLUSH) &&
		 !info->feature_flush) ||
		((req->cmd_flags & REQ_FUA) &&
		 !info->feature_fua));
}

static int blkif_queue_rq(struct blk_mq_hw_ctx *hctx,
			  const struct blk_mq_queue_data *qd)
{
	unsigned long flags;
	int qid = hctx->queue_num;
	struct blkfront_info *info = hctx->queue->queuedata;
	struct blkfront_ring_info *rinfo = NULL;

	BUG_ON(info->nr_rings <= qid);
	rinfo = &info->rinfo[qid];
	blk_mq_start_request(qd->rq);
	spin_lock_irqsave(&rinfo->ring_lock, flags);
	if (RING_FULL(&rinfo->ring))
		goto out_busy;

	if (blkif_request_flush_invalid(qd->rq, rinfo->dev_info))
		goto out_err;

	if (blkif_queue_request(qd->rq, rinfo))
		goto out_busy;

	flush_requests(rinfo);
	spin_unlock_irqrestore(&rinfo->ring_lock, flags);
	return BLK_MQ_RQ_QUEUE_OK;

out_err:
	spin_unlock_irqrestore(&rinfo->ring_lock, flags);
	return BLK_MQ_RQ_QUEUE_ERROR;

out_busy:
	spin_unlock_irqrestore(&rinfo->ring_lock, flags);
	blk_mq_stop_hw_queue(hctx);
	return BLK_MQ_RQ_QUEUE_BUSY;
}

static struct blk_mq_ops blkfront_mq_ops = {
	.queue_rq = blkif_queue_rq,
};

static void blkif_set_queue_limits(struct blkfront_info *info)
{
	struct request_queue *rq = info->rq;
	struct gendisk *gd = info->gd;
	unsigned int segments = info->max_indirect_segments ? :
				BLKIF_MAX_SEGMENTS_PER_REQUEST;

	queue_flag_set_unlocked(QUEUE_FLAG_VIRT, rq);

	if (info->feature_discard) {
		queue_flag_set_unlocked(QUEUE_FLAG_DISCARD, rq);
		blk_queue_max_discard_sectors(rq, get_capacity(gd));
		rq->limits.discard_granularity = info->discard_granularity;
		rq->limits.discard_alignment = info->discard_alignment;
		if (info->feature_secdiscard)
			queue_flag_set_unlocked(QUEUE_FLAG_SECERASE, rq);
	}

	/* Hard sector size and max sectors impersonate the equiv. hardware. */
	blk_queue_logical_block_size(rq, info->sector_size);
	blk_queue_physical_block_size(rq, info->physical_sector_size);
	blk_queue_max_hw_sectors(rq, (segments * XEN_PAGE_SIZE) / 512);

	/* Each segment in a request is up to an aligned page in size. */
	blk_queue_segment_boundary(rq, PAGE_SIZE - 1);
	blk_queue_max_segment_size(rq, PAGE_SIZE);

	/* Ensure a merged request will fit in a single I/O ring slot. */
	blk_queue_max_segments(rq, segments / GRANTS_PER_PSEG);

	/* Make sure buffer addresses are sector-aligned. */
	blk_queue_dma_alignment(rq, 511);

	/* Make sure we don't use bounce buffers. */
	blk_queue_bounce_limit(rq, BLK_BOUNCE_ANY);
}

static int xlvbd_init_blk_queue(struct gendisk *gd, u16 sector_size,
				unsigned int physical_sector_size)
{
	struct request_queue *rq;
	struct blkfront_info *info = gd->private_data;

	memset(&info->tag_set, 0, sizeof(info->tag_set));
	info->tag_set.ops = &blkfront_mq_ops;
	info->tag_set.nr_hw_queues = info->nr_rings;
	if (HAS_EXTRA_REQ && info->max_indirect_segments == 0) {
		/*
		 * When indirect descriptior is not supported, the I/O request
		 * will be split between multiple request in the ring.
		 * To avoid problems when sending the request, divide by
		 * 2 the depth of the queue.
		 */
		info->tag_set.queue_depth =  BLK_RING_SIZE(info) / 2;
	} else
		info->tag_set.queue_depth = BLK_RING_SIZE(info);
	info->tag_set.numa_node = NUMA_NO_NODE;
	info->tag_set.flags = BLK_MQ_F_SHOULD_MERGE | BLK_MQ_F_SG_MERGE;
	info->tag_set.cmd_size = 0;
	info->tag_set.driver_data = info;

	if (blk_mq_alloc_tag_set(&info->tag_set))
		return -EINVAL;
	rq = blk_mq_init_queue(&info->tag_set);
	if (IS_ERR(rq)) {
		blk_mq_free_tag_set(&info->tag_set);
		return PTR_ERR(rq);
	}

	rq->queuedata = info;
	info->rq = gd->queue = rq;
	info->gd = gd;
	info->sector_size = sector_size;
	info->physical_sector_size = physical_sector_size;
	blkif_set_queue_limits(info);

	return 0;
}

static const char *flush_info(struct blkfront_info *info)
{
	if (info->feature_flush && info->feature_fua)
		return "barrier: enabled;";
	else if (info->feature_flush)
		return "flush diskcache: enabled;";
	else
		return "barrier or flush: disabled;";
}

static void xlvbd_flush(struct blkfront_info *info)
{
	blk_queue_write_cache(info->rq, info->feature_flush ? true : false,
			      info->feature_fua ? true : false);
	pr_info("blkfront: %s: %s %s %s %s %s\n",
		info->gd->disk_name, flush_info(info),
		"persistent grants:", info->feature_persistent ?
		"enabled;" : "disabled;", "indirect descriptors:",
		info->max_indirect_segments ? "enabled;" : "disabled;");
}

static int xen_translate_vdev(int vdevice, int *minor, unsigned int *offset)
{
	int major;
	major = BLKIF_MAJOR(vdevice);
	*minor = BLKIF_MINOR(vdevice);
	switch (major) {
		case XEN_IDE0_MAJOR:
			*offset = (*minor / 64) + EMULATED_HD_DISK_NAME_OFFSET;
			*minor = ((*minor / 64) * PARTS_PER_DISK) +
				EMULATED_HD_DISK_MINOR_OFFSET;
			break;
		case XEN_IDE1_MAJOR:
			*offset = (*minor / 64) + 2 + EMULATED_HD_DISK_NAME_OFFSET;
			*minor = (((*minor / 64) + 2) * PARTS_PER_DISK) +
				EMULATED_HD_DISK_MINOR_OFFSET;
			break;
		case XEN_SCSI_DISK0_MAJOR:
			*offset = (*minor / PARTS_PER_DISK) + EMULATED_SD_DISK_NAME_OFFSET;
			*minor = *minor + EMULATED_SD_DISK_MINOR_OFFSET;
			break;
		case XEN_SCSI_DISK1_MAJOR:
		case XEN_SCSI_DISK2_MAJOR:
		case XEN_SCSI_DISK3_MAJOR:
		case XEN_SCSI_DISK4_MAJOR:
		case XEN_SCSI_DISK5_MAJOR:
		case XEN_SCSI_DISK6_MAJOR:
		case XEN_SCSI_DISK7_MAJOR:
			*offset = (*minor / PARTS_PER_DISK) + 
				((major - XEN_SCSI_DISK1_MAJOR + 1) * 16) +
				EMULATED_SD_DISK_NAME_OFFSET;
			*minor = *minor +
				((major - XEN_SCSI_DISK1_MAJOR + 1) * 16 * PARTS_PER_DISK) +
				EMULATED_SD_DISK_MINOR_OFFSET;
			break;
		case XEN_SCSI_DISK8_MAJOR:
		case XEN_SCSI_DISK9_MAJOR:
		case XEN_SCSI_DISK10_MAJOR:
		case XEN_SCSI_DISK11_MAJOR:
		case XEN_SCSI_DISK12_MAJOR:
		case XEN_SCSI_DISK13_MAJOR:
		case XEN_SCSI_DISK14_MAJOR:
		case XEN_SCSI_DISK15_MAJOR:
			*offset = (*minor / PARTS_PER_DISK) + 
				((major - XEN_SCSI_DISK8_MAJOR + 8) * 16) +
				EMULATED_SD_DISK_NAME_OFFSET;
			*minor = *minor +
				((major - XEN_SCSI_DISK8_MAJOR + 8) * 16 * PARTS_PER_DISK) +
				EMULATED_SD_DISK_MINOR_OFFSET;
			break;
		case XENVBD_MAJOR:
			*offset = *minor / PARTS_PER_DISK;
			break;
		default:
			printk(KERN_WARNING "blkfront: your disk configuration is "
					"incorrect, please use an xvd device instead\n");
			return -ENODEV;
	}
	return 0;
}

static char *encode_disk_name(char *ptr, unsigned int n)
{
	if (n >= 26)
		ptr = encode_disk_name(ptr, n / 26 - 1);
	*ptr = 'a' + n % 26;
	return ptr + 1;
}

static int xlvbd_alloc_gendisk(blkif_sector_t capacity,
			       struct blkfront_info *info,
			       u16 vdisk_info, u16 sector_size,
			       unsigned int physical_sector_size)
{
	struct gendisk *gd;
	int nr_minors = 1;
	int err;
	unsigned int offset;
	int minor;
	int nr_parts;
	char *ptr;

	BUG_ON(info->gd != NULL);
	BUG_ON(info->rq != NULL);

	if ((info->vdevice>>EXT_SHIFT) > 1) {
		/* this is above the extended range; something is wrong */
		printk(KERN_WARNING "blkfront: vdevice 0x%x is above the extended range; ignoring\n", info->vdevice);
		return -ENODEV;
	}

	if (!VDEV_IS_EXTENDED(info->vdevice)) {
		err = xen_translate_vdev(info->vdevice, &minor, &offset);
		if (err)
			return err;		
 		nr_parts = PARTS_PER_DISK;
	} else {
		minor = BLKIF_MINOR_EXT(info->vdevice);
		nr_parts = PARTS_PER_EXT_DISK;
		offset = minor / nr_parts;
		if (xen_hvm_domain() && offset < EMULATED_HD_DISK_NAME_OFFSET + 4)
			printk(KERN_WARNING "blkfront: vdevice 0x%x might conflict with "
					"emulated IDE disks,\n\t choose an xvd device name"
					"from xvde on\n", info->vdevice);
	}
	if (minor >> MINORBITS) {
		pr_warn("blkfront: %#x's minor (%#x) out of range; ignoring\n",
			info->vdevice, minor);
		return -ENODEV;
	}

	if ((minor % nr_parts) == 0)
		nr_minors = nr_parts;

	err = xlbd_reserve_minors(minor, nr_minors);
	if (err)
		goto out;
	err = -ENODEV;

	gd = alloc_disk(nr_minors);
	if (gd == NULL)
		goto release;

	strcpy(gd->disk_name, DEV_NAME);
	ptr = encode_disk_name(gd->disk_name + sizeof(DEV_NAME) - 1, offset);
	BUG_ON(ptr >= gd->disk_name + DISK_NAME_LEN);
	if (nr_minors > 1)
		*ptr = 0;
	else
		snprintf(ptr, gd->disk_name + DISK_NAME_LEN - ptr,
			 "%d", minor & (nr_parts - 1));

	gd->major = XENVBD_MAJOR;
	gd->first_minor = minor;
	gd->fops = &xlvbd_block_fops;
	gd->private_data = info;
	set_capacity(gd, capacity);

	if (xlvbd_init_blk_queue(gd, sector_size, physical_sector_size)) {
		del_gendisk(gd);
		goto release;
	}

	xlvbd_flush(info);

	if (vdisk_info & VDISK_READONLY)
		set_disk_ro(gd, 1);

	if (vdisk_info & VDISK_REMOVABLE)
		gd->flags |= GENHD_FL_REMOVABLE;

	if (vdisk_info & VDISK_CDROM)
		gd->flags |= GENHD_FL_CD;

	return 0;

 release:
	xlbd_release_minors(minor, nr_minors);
 out:
	return err;
}

static void xlvbd_release_gendisk(struct blkfront_info *info)
{
	unsigned int minor, nr_minors, i;

	if (info->rq == NULL)
		return;

	/* No more blkif_request(). */
	blk_mq_stop_hw_queues(info->rq);

	for (i = 0; i < info->nr_rings; i++) {
		struct blkfront_ring_info *rinfo = &info->rinfo[i];

		/* No more gnttab callback work. */
		gnttab_cancel_free_callback(&rinfo->callback);

		/* Flush gnttab callback work. Must be done with no locks held. */
		flush_work(&rinfo->work);
	}

	del_gendisk(info->gd);

	minor = info->gd->first_minor;
	nr_minors = info->gd->minors;
	xlbd_release_minors(minor, nr_minors);

	blk_cleanup_queue(info->rq);
	blk_mq_free_tag_set(&info->tag_set);
	info->rq = NULL;

	put_disk(info->gd);
	info->gd = NULL;
}

/* Already hold rinfo->ring_lock. */
static inline void kick_pending_request_queues_locked(struct blkfront_ring_info *rinfo)
{
	if (!RING_FULL(&rinfo->ring))
		blk_mq_start_stopped_hw_queues(rinfo->dev_info->rq, true);
}

static void kick_pending_request_queues(struct blkfront_ring_info *rinfo)
{
	unsigned long flags;

	spin_lock_irqsave(&rinfo->ring_lock, flags);
	kick_pending_request_queues_locked(rinfo);
	spin_unlock_irqrestore(&rinfo->ring_lock, flags);
}

static void blkif_restart_queue(struct work_struct *work)
{
	struct blkfront_ring_info *rinfo = container_of(work, struct blkfront_ring_info, work);

	if (rinfo->dev_info->connected == BLKIF_STATE_CONNECTED)
		kick_pending_request_queues(rinfo);
}

static void blkif_free_ring(struct blkfront_ring_info *rinfo)
{
	struct grant *persistent_gnt, *n;
	struct blkfront_info *info = rinfo->dev_info;
	int i, j, segs;

	/*
	 * Remove indirect pages, this only happens when using indirect
	 * descriptors but not persistent grants
	 */
	if (!list_empty(&rinfo->indirect_pages)) {
		struct page *indirect_page, *n;

		BUG_ON(info->feature_persistent);
		list_for_each_entry_safe(indirect_page, n, &rinfo->indirect_pages, lru) {
			list_del(&indirect_page->lru);
			__free_page(indirect_page);
		}
	}

	/* Remove all persistent grants. */
	if (!list_empty(&rinfo->grants)) {
		list_for_each_entry_safe(persistent_gnt, n,
					 &rinfo->grants, node) {
			list_del(&persistent_gnt->node);
			if (persistent_gnt->gref != GRANT_INVALID_REF) {
				gnttab_end_foreign_access(persistent_gnt->gref,
							  0, 0UL);
				rinfo->persistent_gnts_c--;
			}
			if (info->feature_persistent)
				__free_page(persistent_gnt->page);
			kfree(persistent_gnt);
		}
	}
	BUG_ON(rinfo->persistent_gnts_c != 0);

	for (i = 0; i < BLK_RING_SIZE(info); i++) {
		/*
		 * Clear persistent grants present in requests already
		 * on the shared ring
		 */
		if (!rinfo->shadow[i].request)
			goto free_shadow;

		segs = rinfo->shadow[i].req.operation == BLKIF_OP_INDIRECT ?
		       rinfo->shadow[i].req.u.indirect.nr_segments :
		       rinfo->shadow[i].req.u.rw.nr_segments;
		for (j = 0; j < segs; j++) {
			persistent_gnt = rinfo->shadow[i].grants_used[j];
			gnttab_end_foreign_access(persistent_gnt->gref, 0, 0UL);
			if (info->feature_persistent)
				__free_page(persistent_gnt->page);
			kfree(persistent_gnt);
		}

		if (rinfo->shadow[i].req.operation != BLKIF_OP_INDIRECT)
			/*
			 * If this is not an indirect operation don't try to
			 * free indirect segments
			 */
			goto free_shadow;

		for (j = 0; j < INDIRECT_GREFS(segs); j++) {
			persistent_gnt = rinfo->shadow[i].indirect_grants[j];
			gnttab_end_foreign_access(persistent_gnt->gref, 0, 0UL);
			__free_page(persistent_gnt->page);
			kfree(persistent_gnt);
		}

free_shadow:
		kfree(rinfo->shadow[i].grants_used);
		rinfo->shadow[i].grants_used = NULL;
		kfree(rinfo->shadow[i].indirect_grants);
		rinfo->shadow[i].indirect_grants = NULL;
		kfree(rinfo->shadow[i].sg);
		rinfo->shadow[i].sg = NULL;
	}

	/* No more gnttab callback work. */
	gnttab_cancel_free_callback(&rinfo->callback);

	/* Flush gnttab callback work. Must be done with no locks held. */
	flush_work(&rinfo->work);

	/* Free resources associated with old device channel. */
	for (i = 0; i < info->nr_ring_pages; i++) {
		if (rinfo->ring_ref[i] != GRANT_INVALID_REF) {
			gnttab_end_foreign_access(rinfo->ring_ref[i], 0, 0);
			rinfo->ring_ref[i] = GRANT_INVALID_REF;
		}
	}
	free_pages((unsigned long)rinfo->ring.sring, get_order(info->nr_ring_pages * XEN_PAGE_SIZE));
	rinfo->ring.sring = NULL;

	if (rinfo->irq)
		unbind_from_irqhandler(rinfo->irq, rinfo);
	rinfo->evtchn = rinfo->irq = 0;
}

static void blkif_free(struct blkfront_info *info, int suspend)
{
	unsigned int i;

	/* Prevent new requests being issued until we fix things up. */
	info->connected = suspend ?
		BLKIF_STATE_SUSPENDED : BLKIF_STATE_DISCONNECTED;
	/* No more blkif_request(). */
	if (info->rq)
		blk_mq_stop_hw_queues(info->rq);

	for (i = 0; i < info->nr_rings; i++)
		blkif_free_ring(&info->rinfo[i]);

	kfree(info->rinfo);
	info->rinfo = NULL;
	info->nr_rings = 0;
}

struct copy_from_grant {
	const struct blk_shadow *s;
	unsigned int grant_idx;
	unsigned int bvec_offset;
	char *bvec_data;
};

static void blkif_copy_from_grant(unsigned long gfn, unsigned int offset,
				  unsigned int len, void *data)
{
	struct copy_from_grant *info = data;
	char *shared_data;
	/* Convenient aliases */
	const struct blk_shadow *s = info->s;

	shared_data = kmap_atomic(s->grants_used[info->grant_idx]->page);

	memcpy(info->bvec_data + info->bvec_offset,
	       shared_data + offset, len);

	info->bvec_offset += len;
	info->grant_idx++;

	kunmap_atomic(shared_data);
}

static enum blk_req_status blkif_rsp_to_req_status(int rsp)
{
	switch (rsp)
	{
	case BLKIF_RSP_OKAY:
		return REQ_DONE;
	case BLKIF_RSP_EOPNOTSUPP:
		return REQ_EOPNOTSUPP;
	case BLKIF_RSP_ERROR:
		/* Fallthrough. */
	default:
		return REQ_ERROR;
	}
}

/*
 * Get the final status of the block request based on two ring response
 */
static int blkif_get_final_status(enum blk_req_status s1,
				  enum blk_req_status s2)
{
	BUG_ON(s1 == REQ_WAITING);
	BUG_ON(s2 == REQ_WAITING);

	if (s1 == REQ_ERROR || s2 == REQ_ERROR)
		return BLKIF_RSP_ERROR;
	else if (s1 == REQ_EOPNOTSUPP || s2 == REQ_EOPNOTSUPP)
		return BLKIF_RSP_EOPNOTSUPP;
	return BLKIF_RSP_OKAY;
}

static bool blkif_completion(unsigned long *id,
			     struct blkfront_ring_info *rinfo,
			     struct blkif_response *bret)
{
	int i = 0;
	struct scatterlist *sg;
	int num_sg, num_grant;
	struct blkfront_info *info = rinfo->dev_info;
	struct blk_shadow *s = &rinfo->shadow[*id];
	struct copy_from_grant data = {
		.grant_idx = 0,
	};

	num_grant = s->req.operation == BLKIF_OP_INDIRECT ?
		s->req.u.indirect.nr_segments : s->req.u.rw.nr_segments;

	/* The I/O request may be split in two. */
	if (unlikely(s->associated_id != NO_ASSOCIATED_ID)) {
		struct blk_shadow *s2 = &rinfo->shadow[s->associated_id];

		/* Keep the status of the current response in shadow. */
		s->status = blkif_rsp_to_req_status(bret->status);

		/* Wait the second response if not yet here. */
		if (s2->status == REQ_WAITING)
			return 0;

		bret->status = blkif_get_final_status(s->status,
						      s2->status);

		/*
		 * All the grants is stored in the first shadow in order
		 * to make the completion code simpler.
		 */
		num_grant += s2->req.u.rw.nr_segments;

		/*
		 * The two responses may not come in order. Only the
		 * first request will store the scatter-gather list.
		 */
		if (s2->num_sg != 0) {
			/* Update "id" with the ID of the first response. */
			*id = s->associated_id;
			s = s2;
		}

		/*
		 * We don't need anymore the second request, so recycling
		 * it now.
		 */
		if (add_id_to_freelist(rinfo, s->associated_id))
			WARN(1, "%s: can't recycle the second part (id = %ld) of the request\n",
			     info->gd->disk_name, s->associated_id);
	}

	data.s = s;
	num_sg = s->num_sg;

	if (bret->operation == BLKIF_OP_READ && info->feature_persistent) {
		for_each_sg(s->sg, sg, num_sg, i) {
			BUG_ON(sg->offset + sg->length > PAGE_SIZE);

			data.bvec_offset = sg->offset;
			data.bvec_data = kmap_atomic(sg_page(sg));

			gnttab_foreach_grant_in_range(sg_page(sg),
						      sg->offset,
						      sg->length,
						      blkif_copy_from_grant,
						      &data);

			kunmap_atomic(data.bvec_data);
		}
	}
	/* Add the persistent grant into the list of free grants */
	for (i = 0; i < num_grant; i++) {
		if (gnttab_query_foreign_access(s->grants_used[i]->gref)) {
			/*
			 * If the grant is still mapped by the backend (the
			 * backend has chosen to make this grant persistent)
			 * we add it at the head of the list, so it will be
			 * reused first.
			 */
			if (!info->feature_persistent)
				pr_alert_ratelimited("backed has not unmapped grant: %u\n",
						     s->grants_used[i]->gref);
			list_add(&s->grants_used[i]->node, &rinfo->grants);
			rinfo->persistent_gnts_c++;
		} else {
			/*
			 * If the grant is not mapped by the backend we end the
			 * foreign access and add it to the tail of the list,
			 * so it will not be picked again unless we run out of
			 * persistent grants.
			 */
			gnttab_end_foreign_access(s->grants_used[i]->gref, 0, 0UL);
			s->grants_used[i]->gref = GRANT_INVALID_REF;
			list_add_tail(&s->grants_used[i]->node, &rinfo->grants);
		}
	}
	if (s->req.operation == BLKIF_OP_INDIRECT) {
		for (i = 0; i < INDIRECT_GREFS(num_grant); i++) {
			if (gnttab_query_foreign_access(s->indirect_grants[i]->gref)) {
				if (!info->feature_persistent)
					pr_alert_ratelimited("backed has not unmapped grant: %u\n",
							     s->indirect_grants[i]->gref);
				list_add(&s->indirect_grants[i]->node, &rinfo->grants);
				rinfo->persistent_gnts_c++;
			} else {
				struct page *indirect_page;

				gnttab_end_foreign_access(s->indirect_grants[i]->gref, 0, 0UL);
				/*
				 * Add the used indirect page back to the list of
				 * available pages for indirect grefs.
				 */
				if (!info->feature_persistent) {
					indirect_page = s->indirect_grants[i]->page;
					list_add(&indirect_page->lru, &rinfo->indirect_pages);
				}
				s->indirect_grants[i]->gref = GRANT_INVALID_REF;
				list_add_tail(&s->indirect_grants[i]->node, &rinfo->grants);
			}
		}
	}

	return 1;
}

static irqreturn_t blkif_interrupt(int irq, void *dev_id)
{
	struct request *req;
	struct blkif_response *bret;
	RING_IDX i, rp;
	unsigned long flags;
	struct blkfront_ring_info *rinfo = (struct blkfront_ring_info *)dev_id;
	struct blkfront_info *info = rinfo->dev_info;
	int error;

	if (unlikely(info->connected != BLKIF_STATE_CONNECTED))
		return IRQ_HANDLED;

	spin_lock_irqsave(&rinfo->ring_lock, flags);
 again:
	rp = rinfo->ring.sring->rsp_prod;
	rmb(); /* Ensure we see queued responses up to 'rp'. */

	for (i = rinfo->ring.rsp_cons; i != rp; i++) {
		unsigned long id;

		bret = RING_GET_RESPONSE(&rinfo->ring, i);
		id   = bret->id;
		/*
		 * The backend has messed up and given us an id that we would
		 * never have given to it (we stamp it up to BLK_RING_SIZE -
		 * look in get_id_from_freelist.
		 */
		if (id >= BLK_RING_SIZE(info)) {
			WARN(1, "%s: response to %s has incorrect id (%ld)\n",
			     info->gd->disk_name, op_name(bret->operation), id);
			/* We can't safely get the 'struct request' as
			 * the id is busted. */
			continue;
		}
		req  = rinfo->shadow[id].request;

		if (bret->operation != BLKIF_OP_DISCARD) {
			/*
			 * We may need to wait for an extra response if the
			 * I/O request is split in 2
			 */
			if (!blkif_completion(&id, rinfo, bret))
				continue;
		}

		if (add_id_to_freelist(rinfo, id)) {
			WARN(1, "%s: response to %s (id %ld) couldn't be recycled!\n",
			     info->gd->disk_name, op_name(bret->operation), id);
			continue;
		}

		error = (bret->status == BLKIF_RSP_OKAY) ? 0 : -EIO;
		switch (bret->operation) {
		case BLKIF_OP_DISCARD:
			if (unlikely(bret->status == BLKIF_RSP_EOPNOTSUPP)) {
				struct request_queue *rq = info->rq;
				printk(KERN_WARNING "blkfront: %s: %s op failed\n",
					   info->gd->disk_name, op_name(bret->operation));
				error = -EOPNOTSUPP;
				info->feature_discard = 0;
				info->feature_secdiscard = 0;
				queue_flag_clear(QUEUE_FLAG_DISCARD, rq);
				queue_flag_clear(QUEUE_FLAG_SECERASE, rq);
			}
			blk_mq_complete_request(req, error);
			break;
		case BLKIF_OP_FLUSH_DISKCACHE:
		case BLKIF_OP_WRITE_BARRIER:
			if (unlikely(bret->status == BLKIF_RSP_EOPNOTSUPP)) {
				printk(KERN_WARNING "blkfront: %s: %s op failed\n",
				       info->gd->disk_name, op_name(bret->operation));
				error = -EOPNOTSUPP;
			}
			if (unlikely(bret->status == BLKIF_RSP_ERROR &&
				     rinfo->shadow[id].req.u.rw.nr_segments == 0)) {
				printk(KERN_WARNING "blkfront: %s: empty %s op failed\n",
				       info->gd->disk_name, op_name(bret->operation));
				error = -EOPNOTSUPP;
			}
			if (unlikely(error)) {
				if (error == -EOPNOTSUPP)
					error = 0;
				info->feature_fua = 0;
				info->feature_flush = 0;
				xlvbd_flush(info);
			}
			/* fall through */
		case BLKIF_OP_READ:
		case BLKIF_OP_WRITE:
			if (unlikely(bret->status != BLKIF_RSP_OKAY))
				dev_dbg(&info->xbdev->dev, "Bad return from blkdev data "
					"request: %x\n", bret->status);

			blk_mq_complete_request(req, error);
			break;
		default:
			BUG();
		}
	}

	rinfo->ring.rsp_cons = i;

	if (i != rinfo->ring.req_prod_pvt) {
		int more_to_do;
		RING_FINAL_CHECK_FOR_RESPONSES(&rinfo->ring, more_to_do);
		if (more_to_do)
			goto again;
	} else
		rinfo->ring.sring->rsp_event = i + 1;

	kick_pending_request_queues_locked(rinfo);

	spin_unlock_irqrestore(&rinfo->ring_lock, flags);

	return IRQ_HANDLED;
}


static int setup_blkring(struct xenbus_device *dev,
			 struct blkfront_ring_info *rinfo)
{
	struct blkif_sring *sring;
	int err, i;
	struct blkfront_info *info = rinfo->dev_info;
	unsigned long ring_size = info->nr_ring_pages * XEN_PAGE_SIZE;
	grant_ref_t gref[XENBUS_MAX_RING_GRANTS];

	for (i = 0; i < info->nr_ring_pages; i++)
		rinfo->ring_ref[i] = GRANT_INVALID_REF;

	sring = (struct blkif_sring *)__get_free_pages(GFP_NOIO | __GFP_HIGH,
						       get_order(ring_size));
	if (!sring) {
		xenbus_dev_fatal(dev, -ENOMEM, "allocating shared ring");
		return -ENOMEM;
	}
	SHARED_RING_INIT(sring);
	FRONT_RING_INIT(&rinfo->ring, sring, ring_size);

	err = xenbus_grant_ring(dev, rinfo->ring.sring, info->nr_ring_pages, gref);
	if (err < 0) {
		free_pages((unsigned long)sring, get_order(ring_size));
		rinfo->ring.sring = NULL;
		goto fail;
	}
	for (i = 0; i < info->nr_ring_pages; i++)
		rinfo->ring_ref[i] = gref[i];

	err = xenbus_alloc_evtchn(dev, &rinfo->evtchn);
	if (err)
		goto fail;

	err = bind_evtchn_to_irqhandler(rinfo->evtchn, blkif_interrupt, 0,
					"blkif", rinfo);
	if (err <= 0) {
		xenbus_dev_fatal(dev, err,
				 "bind_evtchn_to_irqhandler failed");
		goto fail;
	}
	rinfo->irq = err;

	return 0;
fail:
	blkif_free(info, 0);
	return err;
}

/*
 * Write out per-ring/queue nodes including ring-ref and event-channel, and each
 * ring buffer may have multi pages depending on ->nr_ring_pages.
 */
static int write_per_ring_nodes(struct xenbus_transaction xbt,
				struct blkfront_ring_info *rinfo, const char *dir)
{
	int err;
	unsigned int i;
	const char *message = NULL;
	struct blkfront_info *info = rinfo->dev_info;

	if (info->nr_ring_pages == 1) {
		err = xenbus_printf(xbt, dir, "ring-ref", "%u", rinfo->ring_ref[0]);
		if (err) {
			message = "writing ring-ref";
			goto abort_transaction;
		}
	} else {
		for (i = 0; i < info->nr_ring_pages; i++) {
			char ring_ref_name[RINGREF_NAME_LEN];

			snprintf(ring_ref_name, RINGREF_NAME_LEN, "ring-ref%u", i);
			err = xenbus_printf(xbt, dir, ring_ref_name,
					    "%u", rinfo->ring_ref[i]);
			if (err) {
				message = "writing ring-ref";
				goto abort_transaction;
			}
		}
	}

	err = xenbus_printf(xbt, dir, "event-channel", "%u", rinfo->evtchn);
	if (err) {
		message = "writing event-channel";
		goto abort_transaction;
	}

	return 0;

abort_transaction:
	xenbus_transaction_end(xbt, 1);
	if (message)
		xenbus_dev_fatal(info->xbdev, err, "%s", message);

	return err;
}

/* Common code used when first setting up, and when resuming. */
static int talk_to_blkback(struct xenbus_device *dev,
			   struct blkfront_info *info)
{
	const char *message = NULL;
	struct xenbus_transaction xbt;
	int err;
	unsigned int i, max_page_order;
	unsigned int ring_page_order;

	max_page_order = xenbus_read_unsigned(info->xbdev->otherend,
					      "max-ring-page-order", 0);
	ring_page_order = min(xen_blkif_max_ring_order, max_page_order);
	info->nr_ring_pages = 1 << ring_page_order;

	for (i = 0; i < info->nr_rings; i++) {
		struct blkfront_ring_info *rinfo = &info->rinfo[i];

		/* Create shared ring, alloc event channel. */
		err = setup_blkring(dev, rinfo);
		if (err)
			goto destroy_blkring;
	}

again:
	err = xenbus_transaction_start(&xbt);
	if (err) {
		xenbus_dev_fatal(dev, err, "starting transaction");
		goto destroy_blkring;
	}

	if (info->nr_ring_pages > 1) {
		err = xenbus_printf(xbt, dev->nodename, "ring-page-order", "%u",
				    ring_page_order);
		if (err) {
			message = "writing ring-page-order";
			goto abort_transaction;
		}
	}

	/* We already got the number of queues/rings in _probe */
	if (info->nr_rings == 1) {
		err = write_per_ring_nodes(xbt, &info->rinfo[0], dev->nodename);
		if (err)
			goto destroy_blkring;
	} else {
		char *path;
		size_t pathsize;

		err = xenbus_printf(xbt, dev->nodename, "multi-queue-num-queues", "%u",
				    info->nr_rings);
		if (err) {
			message = "writing multi-queue-num-queues";
			goto abort_transaction;
		}

		pathsize = strlen(dev->nodename) + QUEUE_NAME_LEN;
		path = kmalloc(pathsize, GFP_KERNEL);
		if (!path) {
			err = -ENOMEM;
			message = "ENOMEM while writing ring references";
			goto abort_transaction;
		}

		for (i = 0; i < info->nr_rings; i++) {
			memset(path, 0, pathsize);
			snprintf(path, pathsize, "%s/queue-%u", dev->nodename, i);
			err = write_per_ring_nodes(xbt, &info->rinfo[i], path);
			if (err) {
				kfree(path);
				goto destroy_blkring;
			}
		}
		kfree(path);
	}
	err = xenbus_printf(xbt, dev->nodename, "protocol", "%s",
			    XEN_IO_PROTO_ABI_NATIVE);
	if (err) {
		message = "writing protocol";
		goto abort_transaction;
	}
	err = xenbus_printf(xbt, dev->nodename,
			    "feature-persistent", "%u", 1);
	if (err)
		dev_warn(&dev->dev,
			 "writing persistent grants feature to xenbus");

	err = xenbus_transaction_end(xbt, 0);
	if (err) {
		if (err == -EAGAIN)
			goto again;
		xenbus_dev_fatal(dev, err, "completing transaction");
		goto destroy_blkring;
	}

	for (i = 0; i < info->nr_rings; i++) {
		unsigned int j;
		struct blkfront_ring_info *rinfo = &info->rinfo[i];

		for (j = 0; j < BLK_RING_SIZE(info); j++)
			rinfo->shadow[j].req.u.rw.id = j + 1;
		rinfo->shadow[BLK_RING_SIZE(info)-1].req.u.rw.id = 0x0fffffff;
	}
	xenbus_switch_state(dev, XenbusStateInitialised);

	return 0;

 abort_transaction:
	xenbus_transaction_end(xbt, 1);
	if (message)
		xenbus_dev_fatal(dev, err, "%s", message);
 destroy_blkring:
	blkif_free(info, 0);

	kfree(info);
	dev_set_drvdata(&dev->dev, NULL);

	return err;
}

static int negotiate_mq(struct blkfront_info *info)
{
	unsigned int backend_max_queues;
	unsigned int i;

	BUG_ON(info->nr_rings);

	/* Check if backend supports multiple queues. */
	backend_max_queues = xenbus_read_unsigned(info->xbdev->otherend,
						  "multi-queue-max-queues", 1);
	info->nr_rings = min(backend_max_queues, xen_blkif_max_queues);
	/* We need at least one ring. */
	if (!info->nr_rings)
		info->nr_rings = 1;

	info->rinfo = kzalloc(sizeof(struct blkfront_ring_info) * info->nr_rings, GFP_KERNEL);
	if (!info->rinfo) {
		xenbus_dev_fatal(info->xbdev, -ENOMEM, "allocating ring_info structure");
		return -ENOMEM;
	}

	for (i = 0; i < info->nr_rings; i++) {
		struct blkfront_ring_info *rinfo;

		rinfo = &info->rinfo[i];
		INIT_LIST_HEAD(&rinfo->indirect_pages);
		INIT_LIST_HEAD(&rinfo->grants);
		rinfo->dev_info = info;
		INIT_WORK(&rinfo->work, blkif_restart_queue);
		spin_lock_init(&rinfo->ring_lock);
	}
	return 0;
}
/**
 * Entry point to this code when a new device is created.  Allocate the basic
 * structures and the ring buffer for communication with the backend, and
 * inform the backend of the appropriate details for those.  Switch to
 * Initialised state.
 */
static int blkfront_probe(struct xenbus_device *dev,
			  const struct xenbus_device_id *id)
{
	int err, vdevice;
	struct blkfront_info *info;

	/* FIXME: Use dynamic device id if this is not set. */
	err = xenbus_scanf(XBT_NIL, dev->nodename,
			   "virtual-device", "%i", &vdevice);
	if (err != 1) {
		/* go looking in the extended area instead */
		err = xenbus_scanf(XBT_NIL, dev->nodename, "virtual-device-ext",
				   "%i", &vdevice);
		if (err != 1) {
			xenbus_dev_fatal(dev, err, "reading virtual-device");
			return err;
		}
	}

	if (xen_hvm_domain()) {
		char *type;
		int len;
		/* no unplug has been done: do not hook devices != xen vbds */
		if (xen_has_pv_and_legacy_disk_devices()) {
			int major;

			if (!VDEV_IS_EXTENDED(vdevice))
				major = BLKIF_MAJOR(vdevice);
			else
				major = XENVBD_MAJOR;

			if (major != XENVBD_MAJOR) {
				printk(KERN_INFO
						"%s: HVM does not support vbd %d as xen block device\n",
						__func__, vdevice);
				return -ENODEV;
			}
		}
		/* do not create a PV cdrom device if we are an HVM guest */
		type = xenbus_read(XBT_NIL, dev->nodename, "device-type", &len);
		if (IS_ERR(type))
			return -ENODEV;
		if (strncmp(type, "cdrom", 5) == 0) {
			kfree(type);
			return -ENODEV;
		}
		kfree(type);
	}
	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		xenbus_dev_fatal(dev, -ENOMEM, "allocating info structure");
		return -ENOMEM;
	}

	info->xbdev = dev;
	err = negotiate_mq(info);
	if (err) {
		kfree(info);
		return err;
	}

	mutex_init(&info->mutex);
	info->vdevice = vdevice;
	info->connected = BLKIF_STATE_DISCONNECTED;

	/* Front end dir is a number, which is used as the id. */
	info->handle = simple_strtoul(strrchr(dev->nodename, '/')+1, NULL, 0);
	dev_set_drvdata(&dev->dev, info);

	return 0;
}

static void split_bio_end(struct bio *bio)
{
	struct split_bio *split_bio = bio->bi_private;

	if (atomic_dec_and_test(&split_bio->pending)) {
		split_bio->bio->bi_phys_segments = 0;
		split_bio->bio->bi_error = bio->bi_error;
		bio_endio(split_bio->bio);
		kfree(split_bio);
	}
	bio_put(bio);
}

static int blkif_recover(struct blkfront_info *info)
{
	unsigned int i, r_index;
	struct request *req, *n;
	int rc;
	struct bio *bio, *cloned_bio;
	unsigned int segs, offset;
	int pending, size;
	struct split_bio *split_bio;

	blkfront_gather_backend_features(info);
	/* Reset limits changed by blk_mq_update_nr_hw_queues(). */
	blkif_set_queue_limits(info);
	segs = info->max_indirect_segments ? : BLKIF_MAX_SEGMENTS_PER_REQUEST;
	blk_queue_max_segments(info->rq, segs / GRANTS_PER_PSEG);

	for (r_index = 0; r_index < info->nr_rings; r_index++) {
		struct blkfront_ring_info *rinfo = &info->rinfo[r_index];

		rc = blkfront_setup_indirect(rinfo);
		if (rc)
			return rc;
	}
	xenbus_switch_state(info->xbdev, XenbusStateConnected);

	/* Now safe for us to use the shared ring */
	info->connected = BLKIF_STATE_CONNECTED;

	for (r_index = 0; r_index < info->nr_rings; r_index++) {
		struct blkfront_ring_info *rinfo;

		rinfo = &info->rinfo[r_index];
		/* Kick any other new requests queued since we resumed */
		kick_pending_request_queues(rinfo);
	}

	list_for_each_entry_safe(req, n, &info->requests, queuelist) {
		/* Requeue pending requests (flush or discard) */
		list_del_init(&req->queuelist);
		BUG_ON(req->nr_phys_segments > segs);
		blk_mq_requeue_request(req, false);
	}
	blk_mq_start_stopped_hw_queues(info->rq, true);
	blk_mq_kick_requeue_list(info->rq);

	while ((bio = bio_list_pop(&info->bio_list)) != NULL) {
		/* Traverse the list of pending bios and re-queue them */
		if (bio_segments(bio) > segs) {
			/*
			 * This bio has more segments than what we can
			 * handle, we have to split it.
			 */
			pending = (bio_segments(bio) + segs - 1) / segs;
			split_bio = kzalloc(sizeof(*split_bio), GFP_NOIO);
			BUG_ON(split_bio == NULL);
			atomic_set(&split_bio->pending, pending);
			split_bio->bio = bio;
			for (i = 0; i < pending; i++) {
				offset = (i * segs * XEN_PAGE_SIZE) >> 9;
				size = min((unsigned int)(segs * XEN_PAGE_SIZE) >> 9,
					   (unsigned int)bio_sectors(bio) - offset);
				cloned_bio = bio_clone(bio, GFP_NOIO);
				BUG_ON(cloned_bio == NULL);
				bio_trim(cloned_bio, offset, size);
				cloned_bio->bi_private = split_bio;
				cloned_bio->bi_end_io = split_bio_end;
				submit_bio(cloned_bio);
			}
			/*
			 * Now we have to wait for all those smaller bios to
			 * end, so we can also end the "parent" bio.
			 */
			continue;
		}
		/* We don't need to split this bio */
		submit_bio(bio);
	}

	return 0;
}

/**
 * We are reconnecting to the backend, due to a suspend/resume, or a backend
 * driver restart.  We tear down our blkif structure and recreate it, but
 * leave the device-layer structures intact so that this is transparent to the
 * rest of the kernel.
 */
static int blkfront_resume(struct xenbus_device *dev)
{
	struct blkfront_info *info = dev_get_drvdata(&dev->dev);
	int err = 0;
	unsigned int i, j;

	dev_dbg(&dev->dev, "blkfront_resume: %s\n", dev->nodename);

	bio_list_init(&info->bio_list);
	INIT_LIST_HEAD(&info->requests);
	for (i = 0; i < info->nr_rings; i++) {
		struct blkfront_ring_info *rinfo = &info->rinfo[i];
		struct bio_list merge_bio;
		struct blk_shadow *shadow = rinfo->shadow;

		for (j = 0; j < BLK_RING_SIZE(info); j++) {
			/* Not in use? */
			if (!shadow[j].request)
				continue;

			/*
			 * Get the bios in the request so we can re-queue them.
			 */
			if (req_op(shadow[i].request) == REQ_OP_FLUSH ||
			    req_op(shadow[i].request) == REQ_OP_DISCARD ||
			    req_op(shadow[i].request) == REQ_OP_SECURE_ERASE ||
			    shadow[j].request->cmd_flags & REQ_FUA) {
				/*
				 * Flush operations don't contain bios, so
				 * we need to requeue the whole request
				 *
				 * XXX: but this doesn't make any sense for a
				 * write with the FUA flag set..
				 */
				list_add(&shadow[j].request->queuelist, &info->requests);
				continue;
			}
			merge_bio.head = shadow[j].request->bio;
			merge_bio.tail = shadow[j].request->biotail;
			bio_list_merge(&info->bio_list, &merge_bio);
			shadow[j].request->bio = NULL;
			blk_mq_end_request(shadow[j].request, 0);
		}
	}

	blkif_free(info, info->connected == BLKIF_STATE_CONNECTED);

	err = negotiate_mq(info);
	if (err)
		return err;

	err = talk_to_blkback(dev, info);
	if (!err)
		blk_mq_update_nr_hw_queues(&info->tag_set, info->nr_rings);

	/*
	 * We have to wait for the backend to switch to
	 * connected state, since we want to read which
	 * features it supports.
	 */

	return err;
}

static void blkfront_closing(struct blkfront_info *info)
{
	struct xenbus_device *xbdev = info->xbdev;
	struct block_device *bdev = NULL;

	mutex_lock(&info->mutex);

	if (xbdev->state == XenbusStateClosing) {
		mutex_unlock(&info->mutex);
		return;
	}

	if (info->gd)
		bdev = bdget_disk(info->gd, 0);

	mutex_unlock(&info->mutex);

	if (!bdev) {
		xenbus_frontend_closed(xbdev);
		return;
	}

	mutex_lock(&bdev->bd_mutex);

	if (bdev->bd_openers) {
		xenbus_dev_error(xbdev, -EBUSY,
				 "Device in use; refusing to close");
		xenbus_switch_state(xbdev, XenbusStateClosing);
	} else {
		xlvbd_release_gendisk(info);
		xenbus_frontend_closed(xbdev);
	}

	mutex_unlock(&bdev->bd_mutex);
	bdput(bdev);
}

static void blkfront_setup_discard(struct blkfront_info *info)
{
	int err;
	unsigned int discard_granularity;
	unsigned int discard_alignment;

	info->feature_discard = 1;
	err = xenbus_gather(XBT_NIL, info->xbdev->otherend,
		"discard-granularity", "%u", &discard_granularity,
		"discard-alignment", "%u", &discard_alignment,
		NULL);
	if (!err) {
		info->discard_granularity = discard_granularity;
		info->discard_alignment = discard_alignment;
	}
	info->feature_secdiscard =
		!!xenbus_read_unsigned(info->xbdev->otherend, "discard-secure",
				       0);
}

static int blkfront_setup_indirect(struct blkfront_ring_info *rinfo)
{
	unsigned int psegs, grants;
	int err, i;
	struct blkfront_info *info = rinfo->dev_info;

	if (info->max_indirect_segments == 0) {
		if (!HAS_EXTRA_REQ)
			grants = BLKIF_MAX_SEGMENTS_PER_REQUEST;
		else {
			/*
			 * When an extra req is required, the maximum
			 * grants supported is related to the size of the
			 * Linux block segment.
			 */
			grants = GRANTS_PER_PSEG;
		}
	}
	else
		grants = info->max_indirect_segments;
	psegs = DIV_ROUND_UP(grants, GRANTS_PER_PSEG);

	err = fill_grant_buffer(rinfo,
				(grants + INDIRECT_GREFS(grants)) * BLK_RING_SIZE(info));
	if (err)
		goto out_of_memory;

	if (!info->feature_persistent && info->max_indirect_segments) {
		/*
		 * We are using indirect descriptors but not persistent
		 * grants, we need to allocate a set of pages that can be
		 * used for mapping indirect grefs
		 */
		int num = INDIRECT_GREFS(grants) * BLK_RING_SIZE(info);

		BUG_ON(!list_empty(&rinfo->indirect_pages));
		for (i = 0; i < num; i++) {
			struct page *indirect_page = alloc_page(GFP_NOIO);
			if (!indirect_page)
				goto out_of_memory;
			list_add(&indirect_page->lru, &rinfo->indirect_pages);
		}
	}

	for (i = 0; i < BLK_RING_SIZE(info); i++) {
		rinfo->shadow[i].grants_used = kzalloc(
			sizeof(rinfo->shadow[i].grants_used[0]) * grants,
			GFP_NOIO);
		rinfo->shadow[i].sg = kzalloc(sizeof(rinfo->shadow[i].sg[0]) * psegs, GFP_NOIO);
		if (info->max_indirect_segments)
			rinfo->shadow[i].indirect_grants = kzalloc(
				sizeof(rinfo->shadow[i].indirect_grants[0]) *
				INDIRECT_GREFS(grants),
				GFP_NOIO);
		if ((rinfo->shadow[i].grants_used == NULL) ||
			(rinfo->shadow[i].sg == NULL) ||
		     (info->max_indirect_segments &&
		     (rinfo->shadow[i].indirect_grants == NULL)))
			goto out_of_memory;
		sg_init_table(rinfo->shadow[i].sg, psegs);
	}


	return 0;

out_of_memory:
	for (i = 0; i < BLK_RING_SIZE(info); i++) {
		kfree(rinfo->shadow[i].grants_used);
		rinfo->shadow[i].grants_used = NULL;
		kfree(rinfo->shadow[i].sg);
		rinfo->shadow[i].sg = NULL;
		kfree(rinfo->shadow[i].indirect_grants);
		rinfo->shadow[i].indirect_grants = NULL;
	}
	if (!list_empty(&rinfo->indirect_pages)) {
		struct page *indirect_page, *n;
		list_for_each_entry_safe(indirect_page, n, &rinfo->indirect_pages, lru) {
			list_del(&indirect_page->lru);
			__free_page(indirect_page);
		}
	}
	return -ENOMEM;
}

/*
 * Gather all backend feature-*
 */
static void blkfront_gather_backend_features(struct blkfront_info *info)
{
	unsigned int indirect_segments;

	info->feature_flush = 0;
	info->feature_fua = 0;

	/*
	 * If there's no "feature-barrier" defined, then it means
	 * we're dealing with a very old backend which writes
	 * synchronously; nothing to do.
	 *
	 * If there are barriers, then we use flush.
	 */
	if (xenbus_read_unsigned(info->xbdev->otherend, "feature-barrier", 0)) {
		info->feature_flush = 1;
		info->feature_fua = 1;
	}

	/*
	 * And if there is "feature-flush-cache" use that above
	 * barriers.
	 */
	if (xenbus_read_unsigned(info->xbdev->otherend, "feature-flush-cache",
				 0)) {
		info->feature_flush = 1;
		info->feature_fua = 0;
	}

	if (xenbus_read_unsigned(info->xbdev->otherend, "feature-discard", 0))
		blkfront_setup_discard(info);

	info->feature_persistent =
		!!xenbus_read_unsigned(info->xbdev->otherend,
				       "feature-persistent", 0);

	indirect_segments = xenbus_read_unsigned(info->xbdev->otherend,
					"feature-max-indirect-segments", 0);
	if (indirect_segments > xen_blkif_max_segments)
		indirect_segments = xen_blkif_max_segments;
	if (indirect_segments <= BLKIF_MAX_SEGMENTS_PER_REQUEST)
		indirect_segments = 0;
	info->max_indirect_segments = indirect_segments;
}

/*
 * Invoked when the backend is finally 'ready' (and has told produced
 * the details about the physical device - #sectors, size, etc).
 */
static void blkfront_connect(struct blkfront_info *info)
{
	unsigned long long sectors;
	unsigned long sector_size;
	unsigned int physical_sector_size;
	unsigned int binfo;
	int err, i;

	switch (info->connected) {
	case BLKIF_STATE_CONNECTED:
		/*
		 * Potentially, the back-end may be signalling
		 * a capacity change; update the capacity.
		 */
		err = xenbus_scanf(XBT_NIL, info->xbdev->otherend,
				   "sectors", "%Lu", &sectors);
		if (XENBUS_EXIST_ERR(err))
			return;
		printk(KERN_INFO "Setting capacity to %Lu\n",
		       sectors);
		set_capacity(info->gd, sectors);
		revalidate_disk(info->gd);

		return;
	case BLKIF_STATE_SUSPENDED:
		/*
		 * If we are recovering from suspension, we need to wait
		 * for the backend to announce it's features before
		 * reconnecting, at least we need to know if the backend
		 * supports indirect descriptors, and how many.
		 */
		blkif_recover(info);
		return;

	default:
		break;
	}

	dev_dbg(&info->xbdev->dev, "%s:%s.\n",
		__func__, info->xbdev->otherend);

	err = xenbus_gather(XBT_NIL, info->xbdev->otherend,
			    "sectors", "%llu", &sectors,
			    "info", "%u", &binfo,
			    "sector-size", "%lu", &sector_size,
			    NULL);
	if (err) {
		xenbus_dev_fatal(info->xbdev, err,
				 "reading backend fields at %s",
				 info->xbdev->otherend);
		return;
	}

	/*
	 * physcial-sector-size is a newer field, so old backends may not
	 * provide this. Assume physical sector size to be the same as
	 * sector_size in that case.
	 */
	physical_sector_size = xenbus_read_unsigned(info->xbdev->otherend,
						    "physical-sector-size",
						    sector_size);
	blkfront_gather_backend_features(info);
	for (i = 0; i < info->nr_rings; i++) {
		err = blkfront_setup_indirect(&info->rinfo[i]);
		if (err) {
			xenbus_dev_fatal(info->xbdev, err, "setup_indirect at %s",
					 info->xbdev->otherend);
			blkif_free(info, 0);
			break;
		}
	}

	err = xlvbd_alloc_gendisk(sectors, info, binfo, sector_size,
				  physical_sector_size);
	if (err) {
		xenbus_dev_fatal(info->xbdev, err, "xlvbd_add at %s",
				 info->xbdev->otherend);
		goto fail;
	}

	xenbus_switch_state(info->xbdev, XenbusStateConnected);

	/* Kick pending requests. */
	info->connected = BLKIF_STATE_CONNECTED;
	for (i = 0; i < info->nr_rings; i++)
		kick_pending_request_queues(&info->rinfo[i]);

	device_add_disk(&info->xbdev->dev, info->gd);

	info->is_ready = 1;
	return;

fail:
	blkif_free(info, 0);
	return;
}

/**
 * Callback received when the backend's state changes.
 */
static void blkback_changed(struct xenbus_device *dev,
			    enum xenbus_state backend_state)
{
	struct blkfront_info *info = dev_get_drvdata(&dev->dev);

	dev_dbg(&dev->dev, "blkfront:blkback_changed to state %d.\n", backend_state);

	switch (backend_state) {
	case XenbusStateInitWait:
		if (dev->state != XenbusStateInitialising)
			break;
		if (talk_to_blkback(dev, info))
			break;
	case XenbusStateInitialising:
	case XenbusStateInitialised:
	case XenbusStateReconfiguring:
	case XenbusStateReconfigured:
	case XenbusStateUnknown:
		break;

	case XenbusStateConnected:
		/*
		 * talk_to_blkback sets state to XenbusStateInitialised
		 * and blkfront_connect sets it to XenbusStateConnected
		 * (if connection went OK).
		 *
		 * If the backend (or toolstack) decides to poke at backend
		 * state (and re-trigger the watch by setting the state repeatedly
		 * to XenbusStateConnected (4)) we need to deal with this.
		 * This is allowed as this is used to communicate to the guest
		 * that the size of disk has changed!
		 */
		if ((dev->state != XenbusStateInitialised) &&
		    (dev->state != XenbusStateConnected)) {
			if (talk_to_blkback(dev, info))
				break;
		}

		blkfront_connect(info);
		break;

	case XenbusStateClosed:
		if (dev->state == XenbusStateClosed)
			break;
		/* Missed the backend's Closing state -- fallthrough */
	case XenbusStateClosing:
		if (info)
			blkfront_closing(info);
		break;
	}
}

static int blkfront_remove(struct xenbus_device *xbdev)
{
	struct blkfront_info *info = dev_get_drvdata(&xbdev->dev);
	struct block_device *bdev = NULL;
	struct gendisk *disk;

	dev_dbg(&xbdev->dev, "%s removed", xbdev->nodename);

	blkif_free(info, 0);

	mutex_lock(&info->mutex);

	disk = info->gd;
	if (disk)
		bdev = bdget_disk(disk, 0);

	info->xbdev = NULL;
	mutex_unlock(&info->mutex);

	if (!bdev) {
		kfree(info);
		return 0;
	}

	/*
	 * The xbdev was removed before we reached the Closed
	 * state. See if it's safe to remove the disk. If the bdev
	 * isn't closed yet, we let release take care of it.
	 */

	mutex_lock(&bdev->bd_mutex);
	info = disk->private_data;

	dev_warn(disk_to_dev(disk),
		 "%s was hot-unplugged, %d stale handles\n",
		 xbdev->nodename, bdev->bd_openers);

	if (info && !bdev->bd_openers) {
		xlvbd_release_gendisk(info);
		disk->private_data = NULL;
		kfree(info);
	}

	mutex_unlock(&bdev->bd_mutex);
	bdput(bdev);

	return 0;
}

static int blkfront_is_ready(struct xenbus_device *dev)
{
	struct blkfront_info *info = dev_get_drvdata(&dev->dev);

	return info->is_ready && info->xbdev;
}

static int blkif_open(struct block_device *bdev, fmode_t mode)
{
	struct gendisk *disk = bdev->bd_disk;
	struct blkfront_info *info;
	int err = 0;

	mutex_lock(&blkfront_mutex);

	info = disk->private_data;
	if (!info) {
		/* xbdev gone */
		err = -ERESTARTSYS;
		goto out;
	}

	mutex_lock(&info->mutex);

	if (!info->gd)
		/* xbdev is closed */
		err = -ERESTARTSYS;

	mutex_unlock(&info->mutex);

out:
	mutex_unlock(&blkfront_mutex);
	return err;
}

static void blkif_release(struct gendisk *disk, fmode_t mode)
{
	struct blkfront_info *info = disk->private_data;
	struct block_device *bdev;
	struct xenbus_device *xbdev;

	mutex_lock(&blkfront_mutex);

	bdev = bdget_disk(disk, 0);

	if (!bdev) {
		WARN(1, "Block device %s yanked out from us!\n", disk->disk_name);
		goto out_mutex;
	}
	if (bdev->bd_openers)
		goto out;

	/*
	 * Check if we have been instructed to close. We will have
	 * deferred this request, because the bdev was still open.
	 */

	mutex_lock(&info->mutex);
	xbdev = info->xbdev;

	if (xbdev && xbdev->state == XenbusStateClosing) {
		/* pending switch to state closed */
		dev_info(disk_to_dev(bdev->bd_disk), "releasing disk\n");
		xlvbd_release_gendisk(info);
		xenbus_frontend_closed(info->xbdev);
 	}

	mutex_unlock(&info->mutex);

	if (!xbdev) {
		/* sudden device removal */
		dev_info(disk_to_dev(bdev->bd_disk), "releasing disk\n");
		xlvbd_release_gendisk(info);
		disk->private_data = NULL;
		kfree(info);
	}

out:
	bdput(bdev);
out_mutex:
	mutex_unlock(&blkfront_mutex);
}

static const struct block_device_operations xlvbd_block_fops =
{
	.owner = THIS_MODULE,
	.open = blkif_open,
	.release = blkif_release,
	.getgeo = blkif_getgeo,
	.ioctl = blkif_ioctl,
};


static const struct xenbus_device_id blkfront_ids[] = {
	{ "vbd" },
	{ "" }
};

static struct xenbus_driver blkfront_driver = {
	.ids  = blkfront_ids,
	.probe = blkfront_probe,
	.remove = blkfront_remove,
	.resume = blkfront_resume,
	.otherend_changed = blkback_changed,
	.is_ready = blkfront_is_ready,
};

static int __init xlblk_init(void)
{
	int ret;
	int nr_cpus = num_online_cpus();

	if (!xen_domain())
		return -ENODEV;

	if (xen_blkif_max_segments < BLKIF_MAX_SEGMENTS_PER_REQUEST)
		xen_blkif_max_segments = BLKIF_MAX_SEGMENTS_PER_REQUEST;

	if (xen_blkif_max_ring_order > XENBUS_MAX_RING_GRANT_ORDER) {
		pr_info("Invalid max_ring_order (%d), will use default max: %d.\n",
			xen_blkif_max_ring_order, XENBUS_MAX_RING_GRANT_ORDER);
		xen_blkif_max_ring_order = XENBUS_MAX_RING_GRANT_ORDER;
	}

	if (xen_blkif_max_queues > nr_cpus) {
		pr_info("Invalid max_queues (%d), will use default max: %d.\n",
			xen_blkif_max_queues, nr_cpus);
		xen_blkif_max_queues = nr_cpus;
	}

	if (!xen_has_pv_disk_devices())
		return -ENODEV;

	if (register_blkdev(XENVBD_MAJOR, DEV_NAME)) {
		printk(KERN_WARNING "xen_blk: can't get major %d with name %s\n",
		       XENVBD_MAJOR, DEV_NAME);
		return -ENODEV;
	}

	ret = xenbus_register_frontend(&blkfront_driver);
	if (ret) {
		unregister_blkdev(XENVBD_MAJOR, DEV_NAME);
		return ret;
	}

	return 0;
}
module_init(xlblk_init);


static void __exit xlblk_exit(void)
{
	xenbus_unregister_driver(&blkfront_driver);
	unregister_blkdev(XENVBD_MAJOR, DEV_NAME);
	kfree(minors);
}
module_exit(xlblk_exit);

MODULE_DESCRIPTION("Xen virtual block device frontend");
MODULE_LICENSE("GPL");
MODULE_ALIAS_BLOCKDEV_MAJOR(XENVBD_MAJOR);
MODULE_ALIAS("xen:vbd");
MODULE_ALIAS("xenblk");
