/*
   rbd.c -- Export ceph rados objects as a Linux block device


   based on drivers/block/osdblk.c:

   Copyright 2009 Red Hat, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING.  If not, write to
   the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.



   Instructions for use
   --------------------

   1) Map a Linux block device to an existing rbd image.

      Usage: <mon ip addr> <options> <pool name> <rbd image name> [snap name]

      $ echo "192.168.0.1 name=admin rbd foo" > /sys/class/rbd/add

      The snapshot name can be "-" or omitted to map the image read/write.

   2) List all active blkdev<->object mappings.

      In this example, we have performed step #1 twice, creating two blkdevs,
      mapped to two separate rados objects in the rados rbd pool

      $ cat /sys/class/rbd/list
      #id     major   client_name     pool    name    snap    KB
      0       254     client4143      rbd     foo     -      1024000

      The columns, in order, are:
      - blkdev unique id
      - blkdev assigned major
      - rados client id
      - rados pool name
      - rados block device name
      - mapped snapshot ("-" if none)
      - device size in KB


   3) Create a snapshot.

      Usage: <blkdev id> <snapname>

      $ echo "0 mysnap" > /sys/class/rbd/snap_create


   4) Listing a snapshot.

      $ cat /sys/class/rbd/snaps_list
      #id     snap    KB
      0       -       1024000 (*)
      0       foo     1024000

      The columns, in order, are:
      - blkdev unique id
      - snapshot name, '-' means none (active read/write version)
      - size of device at time of snapshot
      - the (*) indicates this is the active version

   5) Rollback to snapshot.

      Usage: <blkdev id> <snapname>

      $ echo "0 mysnap" > /sys/class/rbd/snap_rollback


   6) Mapping an image using snapshot.

      A snapshot mapping is read-only. This is being done by passing
      snap=<snapname> to the options when adding a device.

      $ echo "192.168.0.1 name=admin,snap=mysnap rbd foo" > /sys/class/rbd/add


   7) Remove an active blkdev<->rbd image mapping.

      In this example, we remove the mapping with blkdev unique id 1.

      $ echo 1 > /sys/class/rbd/remove


   NOTE:  The actual creation and deletion of rados objects is outside the scope
   of this driver.

 */

#include <linux/ceph/libceph.h>
#include <linux/ceph/osd_client.h>
#include <linux/ceph/mon_client.h>
#include <linux/ceph/decode.h>

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/blkdev.h>

#include "rbd_types.h"

#define DRV_NAME "rbd"
#define DRV_NAME_LONG "rbd (rados block device)"

#define RBD_MINORS_PER_MAJOR	256		/* max minors per blkdev */

#define RBD_MAX_MD_NAME_LEN	(96 + sizeof(RBD_SUFFIX))
#define RBD_MAX_POOL_NAME_LEN	64
#define RBD_MAX_SNAP_NAME_LEN	32
#define RBD_MAX_OPT_LEN		1024

#define RBD_SNAP_HEAD_NAME	"-"

#define DEV_NAME_LEN		32

/*
 * block device image metadata (in-memory version)
 */
struct rbd_image_header {
	u64 image_size;
	char block_name[32];
	__u8 obj_order;
	__u8 crypt_type;
	__u8 comp_type;
	struct rw_semaphore snap_rwsem;
	struct ceph_snap_context *snapc;
	size_t snap_names_len;
	u64 snap_seq;
	u32 total_snaps;

	char *snap_names;
	u64 *snap_sizes;
};

/*
 * an instance of the client.  multiple devices may share a client.
 */
struct rbd_client {
	struct ceph_client	*client;
	struct kref		kref;
	struct list_head	node;
};

/*
 * a single io request
 */
struct rbd_request {
	struct request		*rq;		/* blk layer request */
	struct bio		*bio;		/* cloned bio */
	struct page		**pages;	/* list of used pages */
	u64			len;
};

/*
 * a single device
 */
struct rbd_device {
	int			id;		/* blkdev unique id */

	int			major;		/* blkdev assigned major */
	struct gendisk		*disk;		/* blkdev's gendisk and rq */
	struct request_queue	*q;

	struct ceph_client	*client;
	struct rbd_client	*rbd_client;

	char			name[DEV_NAME_LEN]; /* blkdev name, e.g. rbd3 */

	spinlock_t		lock;		/* queue lock */

	struct rbd_image_header	header;
	char			obj[RBD_MAX_OBJ_NAME_LEN]; /* rbd image name */
	int			obj_len;
	char			obj_md_name[RBD_MAX_MD_NAME_LEN]; /* hdr nm. */
	char			pool_name[RBD_MAX_POOL_NAME_LEN];
	int			poolid;

	char                    snap_name[RBD_MAX_SNAP_NAME_LEN];
	u32 cur_snap;	/* index+1 of current snapshot within snap context
			   0 - for the head */
	int read_only;

	struct list_head	node;
};

static spinlock_t node_lock;      /* protects client get/put */

static struct class *class_rbd;	  /* /sys/class/rbd */
static DEFINE_MUTEX(ctl_mutex);	  /* Serialize open/close/setup/teardown */
static LIST_HEAD(rbd_dev_list);    /* devices */
static LIST_HEAD(rbd_client_list);      /* clients */


static int rbd_open(struct block_device *bdev, fmode_t mode)
{
	struct gendisk *disk = bdev->bd_disk;
	struct rbd_device *rbd_dev = disk->private_data;

	set_device_ro(bdev, rbd_dev->read_only);

	if ((mode & FMODE_WRITE) && rbd_dev->read_only)
		return -EROFS;

	return 0;
}

static const struct block_device_operations rbd_bd_ops = {
	.owner			= THIS_MODULE,
	.open			= rbd_open,
};

/*
 * Initialize an rbd client instance.
 * We own *opt.
 */
static struct rbd_client *rbd_client_create(struct ceph_options *opt)
{
	struct rbd_client *rbdc;
	int ret = -ENOMEM;

	dout("rbd_client_create\n");
	rbdc = kmalloc(sizeof(struct rbd_client), GFP_KERNEL);
	if (!rbdc)
		goto out_opt;

	kref_init(&rbdc->kref);
	INIT_LIST_HEAD(&rbdc->node);

	rbdc->client = ceph_create_client(opt, rbdc);
	if (IS_ERR(rbdc->client))
		goto out_rbdc;
	opt = NULL; /* Now rbdc->client is responsible for opt */

	ret = ceph_open_session(rbdc->client);
	if (ret < 0)
		goto out_err;

	spin_lock(&node_lock);
	list_add_tail(&rbdc->node, &rbd_client_list);
	spin_unlock(&node_lock);

	dout("rbd_client_create created %p\n", rbdc);
	return rbdc;

out_err:
	ceph_destroy_client(rbdc->client);
out_rbdc:
	kfree(rbdc);
out_opt:
	if (opt)
		ceph_destroy_options(opt);
	return ERR_PTR(ret);
}

/*
 * Find a ceph client with specific addr and configuration.
 */
static struct rbd_client *__rbd_client_find(struct ceph_options *opt)
{
	struct rbd_client *client_node;

	if (opt->flags & CEPH_OPT_NOSHARE)
		return NULL;

	list_for_each_entry(client_node, &rbd_client_list, node)
		if (ceph_compare_options(opt, client_node->client) == 0)
			return client_node;
	return NULL;
}

/*
 * Get a ceph client with specific addr and configuration, if one does
 * not exist create it.
 */
static int rbd_get_client(struct rbd_device *rbd_dev, const char *mon_addr,
			  char *options)
{
	struct rbd_client *rbdc;
	struct ceph_options *opt;
	int ret;

	ret = ceph_parse_options(&opt, options, mon_addr,
				 mon_addr + strlen(mon_addr), NULL, NULL);
	if (ret < 0)
		return ret;

	spin_lock(&node_lock);
	rbdc = __rbd_client_find(opt);
	if (rbdc) {
		ceph_destroy_options(opt);

		/* using an existing client */
		kref_get(&rbdc->kref);
		rbd_dev->rbd_client = rbdc;
		rbd_dev->client = rbdc->client;
		spin_unlock(&node_lock);
		return 0;
	}
	spin_unlock(&node_lock);

	rbdc = rbd_client_create(opt);
	if (IS_ERR(rbdc))
		return PTR_ERR(rbdc);

	rbd_dev->rbd_client = rbdc;
	rbd_dev->client = rbdc->client;
	return 0;
}

/*
 * Destroy ceph client
 */
static void rbd_client_release(struct kref *kref)
{
	struct rbd_client *rbdc = container_of(kref, struct rbd_client, kref);

	dout("rbd_release_client %p\n", rbdc);
	spin_lock(&node_lock);
	list_del(&rbdc->node);
	spin_unlock(&node_lock);

	ceph_destroy_client(rbdc->client);
	kfree(rbdc);
}

/*
 * Drop reference to ceph client node. If it's not referenced anymore, release
 * it.
 */
static void rbd_put_client(struct rbd_device *rbd_dev)
{
	kref_put(&rbd_dev->rbd_client->kref, rbd_client_release);
	rbd_dev->rbd_client = NULL;
	rbd_dev->client = NULL;
}


/*
 * Create a new header structure, translate header format from the on-disk
 * header.
 */
static int rbd_header_from_disk(struct rbd_image_header *header,
				 struct rbd_image_header_ondisk *ondisk,
				 int allocated_snaps,
				 gfp_t gfp_flags)
{
	int i;
	u32 snap_count = le32_to_cpu(ondisk->snap_count);
	int ret = -ENOMEM;

	init_rwsem(&header->snap_rwsem);

	header->snap_names_len = le64_to_cpu(ondisk->snap_names_len);
	header->snapc = kmalloc(sizeof(struct ceph_snap_context) +
				snap_count *
				 sizeof(struct rbd_image_snap_ondisk),
				gfp_flags);
	if (!header->snapc)
		return -ENOMEM;
	if (snap_count) {
		header->snap_names = kmalloc(header->snap_names_len,
					     GFP_KERNEL);
		if (!header->snap_names)
			goto err_snapc;
		header->snap_sizes = kmalloc(snap_count * sizeof(u64),
					     GFP_KERNEL);
		if (!header->snap_sizes)
			goto err_names;
	} else {
		header->snap_names = NULL;
		header->snap_sizes = NULL;
	}
	memcpy(header->block_name, ondisk->block_name,
	       sizeof(ondisk->block_name));

	header->image_size = le64_to_cpu(ondisk->image_size);
	header->obj_order = ondisk->options.order;
	header->crypt_type = ondisk->options.crypt_type;
	header->comp_type = ondisk->options.comp_type;

	atomic_set(&header->snapc->nref, 1);
	header->snap_seq = le64_to_cpu(ondisk->snap_seq);
	header->snapc->num_snaps = snap_count;
	header->total_snaps = snap_count;

	if (snap_count &&
	    allocated_snaps == snap_count) {
		for (i = 0; i < snap_count; i++) {
			header->snapc->snaps[i] =
				le64_to_cpu(ondisk->snaps[i].id);
			header->snap_sizes[i] =
				le64_to_cpu(ondisk->snaps[i].image_size);
		}

		/* copy snapshot names */
		memcpy(header->snap_names, &ondisk->snaps[i],
			header->snap_names_len);
	}

	return 0;

err_names:
	kfree(header->snap_names);
err_snapc:
	kfree(header->snapc);
	return ret;
}

static int snap_index(struct rbd_image_header *header, int snap_num)
{
	return header->total_snaps - snap_num;
}

static u64 cur_snap_id(struct rbd_device *rbd_dev)
{
	struct rbd_image_header *header = &rbd_dev->header;

	if (!rbd_dev->cur_snap)
		return 0;

	return header->snapc->snaps[snap_index(header, rbd_dev->cur_snap)];
}

static int snap_by_name(struct rbd_image_header *header, const char *snap_name,
			u64 *seq, u64 *size)
{
	int i;
	char *p = header->snap_names;

	for (i = 0; i < header->total_snaps; i++, p += strlen(p) + 1) {
		if (strcmp(snap_name, p) == 0)
			break;
	}
	if (i == header->total_snaps)
		return -ENOENT;
	if (seq)
		*seq = header->snapc->snaps[i];

	if (size)
		*size = header->snap_sizes[i];

	return i;
}

static int rbd_header_set_snap(struct rbd_device *dev,
			       const char *snap_name,
			       u64 *size)
{
	struct rbd_image_header *header = &dev->header;
	struct ceph_snap_context *snapc = header->snapc;
	int ret = -ENOENT;

	down_write(&header->snap_rwsem);

	if (!snap_name ||
	    !*snap_name ||
	    strcmp(snap_name, "-") == 0 ||
	    strcmp(snap_name, RBD_SNAP_HEAD_NAME) == 0) {
		if (header->total_snaps)
			snapc->seq = header->snap_seq;
		else
			snapc->seq = 0;
		dev->cur_snap = 0;
		dev->read_only = 0;
		if (size)
			*size = header->image_size;
	} else {
		ret = snap_by_name(header, snap_name, &snapc->seq, size);
		if (ret < 0)
			goto done;

		dev->cur_snap = header->total_snaps - ret;
		dev->read_only = 1;
	}

	ret = 0;
done:
	up_write(&header->snap_rwsem);
	return ret;
}

static void rbd_header_free(struct rbd_image_header *header)
{
	kfree(header->snapc);
	kfree(header->snap_names);
	kfree(header->snap_sizes);
}

/*
 * get the actual striped segment name, offset and length
 */
static u64 rbd_get_segment(struct rbd_image_header *header,
			   const char *block_name,
			   u64 ofs, u64 len,
			   char *seg_name, u64 *segofs)
{
	u64 seg = ofs >> header->obj_order;

	if (seg_name)
		snprintf(seg_name, RBD_MAX_SEG_NAME_LEN,
			 "%s.%012llx", block_name, seg);

	ofs = ofs & ((1 << header->obj_order) - 1);
	len = min_t(u64, len, (1 << header->obj_order) - ofs);

	if (segofs)
		*segofs = ofs;

	return len;
}

/*
 * bio helpers
 */

static void bio_chain_put(struct bio *chain)
{
	struct bio *tmp;

	while (chain) {
		tmp = chain;
		chain = chain->bi_next;
		bio_put(tmp);
	}
}

/*
 * zeros a bio chain, starting at specific offset
 */
static void zero_bio_chain(struct bio *chain, int start_ofs)
{
	struct bio_vec *bv;
	unsigned long flags;
	void *buf;
	int i;
	int pos = 0;

	while (chain) {
		bio_for_each_segment(bv, chain, i) {
			if (pos + bv->bv_len > start_ofs) {
				int remainder = max(start_ofs - pos, 0);
				buf = bvec_kmap_irq(bv, &flags);
				memset(buf + remainder, 0,
				       bv->bv_len - remainder);
				bvec_kunmap_irq(buf, &flags);
			}
			pos += bv->bv_len;
		}

		chain = chain->bi_next;
	}
}

/*
 * bio_chain_clone - clone a chain of bios up to a certain length.
 * might return a bio_pair that will need to be released.
 */
static struct bio *bio_chain_clone(struct bio **old, struct bio **next,
				   struct bio_pair **bp,
				   int len, gfp_t gfpmask)
{
	struct bio *tmp, *old_chain = *old, *new_chain = NULL, *tail = NULL;
	int total = 0;

	if (*bp) {
		bio_pair_release(*bp);
		*bp = NULL;
	}

	while (old_chain && (total < len)) {
		tmp = bio_kmalloc(gfpmask, old_chain->bi_max_vecs);
		if (!tmp)
			goto err_out;

		if (total + old_chain->bi_size > len) {
			struct bio_pair *bp;

			/*
			 * this split can only happen with a single paged bio,
			 * split_bio will BUG_ON if this is not the case
			 */
			dout("bio_chain_clone split! total=%d remaining=%d"
			     "bi_size=%d\n",
			     (int)total, (int)len-total,
			     (int)old_chain->bi_size);

			/* split the bio. We'll release it either in the next
			   call, or it will have to be released outside */
			bp = bio_split(old_chain, (len - total) / 512ULL);
			if (!bp)
				goto err_out;

			__bio_clone(tmp, &bp->bio1);

			*next = &bp->bio2;
		} else {
			__bio_clone(tmp, old_chain);
			*next = old_chain->bi_next;
		}

		tmp->bi_bdev = NULL;
		gfpmask &= ~__GFP_WAIT;
		tmp->bi_next = NULL;

		if (!new_chain) {
			new_chain = tail = tmp;
		} else {
			tail->bi_next = tmp;
			tail = tmp;
		}
		old_chain = old_chain->bi_next;

		total += tmp->bi_size;
	}

	BUG_ON(total < len);

	if (tail)
		tail->bi_next = NULL;

	*old = old_chain;

	return new_chain;

err_out:
	dout("bio_chain_clone with err\n");
	bio_chain_put(new_chain);
	return NULL;
}

/*
 * helpers for osd request op vectors.
 */
static int rbd_create_rw_ops(struct ceph_osd_req_op **ops,
			    int num_ops,
			    int opcode,
			    u32 payload_len)
{
	*ops = kzalloc(sizeof(struct ceph_osd_req_op) * (num_ops + 1),
		       GFP_NOIO);
	if (!*ops)
		return -ENOMEM;
	(*ops)[0].op = opcode;
	/*
	 * op extent offset and length will be set later on
	 * in calc_raw_layout()
	 */
	(*ops)[0].payload_len = payload_len;
	return 0;
}

static void rbd_destroy_ops(struct ceph_osd_req_op *ops)
{
	kfree(ops);
}

/*
 * Send ceph osd request
 */
static int rbd_do_request(struct request *rq,
			  struct rbd_device *dev,
			  struct ceph_snap_context *snapc,
			  u64 snapid,
			  const char *obj, u64 ofs, u64 len,
			  struct bio *bio,
			  struct page **pages,
			  int num_pages,
			  int flags,
			  struct ceph_osd_req_op *ops,
			  int num_reply,
			  void (*rbd_cb)(struct ceph_osd_request *req,
					 struct ceph_msg *msg))
{
	struct ceph_osd_request *req;
	struct ceph_file_layout *layout;
	int ret;
	u64 bno;
	struct timespec mtime = CURRENT_TIME;
	struct rbd_request *req_data;
	struct ceph_osd_request_head *reqhead;
	struct rbd_image_header *header = &dev->header;

	ret = -ENOMEM;
	req_data = kzalloc(sizeof(*req_data), GFP_NOIO);
	if (!req_data)
		goto done;

	dout("rbd_do_request len=%lld ofs=%lld\n", len, ofs);

	down_read(&header->snap_rwsem);

	req = ceph_osdc_alloc_request(&dev->client->osdc, flags,
				      snapc,
				      ops,
				      false,
				      GFP_NOIO, pages, bio);
	if (IS_ERR(req)) {
		up_read(&header->snap_rwsem);
		ret = PTR_ERR(req);
		goto done_pages;
	}

	req->r_callback = rbd_cb;

	req_data->rq = rq;
	req_data->bio = bio;
	req_data->pages = pages;
	req_data->len = len;

	req->r_priv = req_data;

	reqhead = req->r_request->front.iov_base;
	reqhead->snapid = cpu_to_le64(CEPH_NOSNAP);

	strncpy(req->r_oid, obj, sizeof(req->r_oid));
	req->r_oid_len = strlen(req->r_oid);

	layout = &req->r_file_layout;
	memset(layout, 0, sizeof(*layout));
	layout->fl_stripe_unit = cpu_to_le32(1 << RBD_MAX_OBJ_ORDER);
	layout->fl_stripe_count = cpu_to_le32(1);
	layout->fl_object_size = cpu_to_le32(1 << RBD_MAX_OBJ_ORDER);
	layout->fl_pg_preferred = cpu_to_le32(-1);
	layout->fl_pg_pool = cpu_to_le32(dev->poolid);
	ceph_calc_raw_layout(&dev->client->osdc, layout, snapid,
			     ofs, &len, &bno, req, ops);

	ceph_osdc_build_request(req, ofs, &len,
				ops,
				snapc,
				&mtime,
				req->r_oid, req->r_oid_len);
	up_read(&header->snap_rwsem);

	ret = ceph_osdc_start_request(&dev->client->osdc, req, false);
	if (ret < 0)
		goto done_err;

	if (!rbd_cb) {
		ret = ceph_osdc_wait_request(&dev->client->osdc, req);
		ceph_osdc_put_request(req);
	}
	return ret;

done_err:
	bio_chain_put(req_data->bio);
	ceph_osdc_put_request(req);
done_pages:
	kfree(req_data);
done:
	if (rq)
		blk_end_request(rq, ret, len);
	return ret;
}

/*
 * Ceph osd op callback
 */
static void rbd_req_cb(struct ceph_osd_request *req, struct ceph_msg *msg)
{
	struct rbd_request *req_data = req->r_priv;
	struct ceph_osd_reply_head *replyhead;
	struct ceph_osd_op *op;
	__s32 rc;
	u64 bytes;
	int read_op;

	/* parse reply */
	replyhead = msg->front.iov_base;
	WARN_ON(le32_to_cpu(replyhead->num_ops) == 0);
	op = (void *)(replyhead + 1);
	rc = le32_to_cpu(replyhead->result);
	bytes = le64_to_cpu(op->extent.length);
	read_op = (le32_to_cpu(op->op) == CEPH_OSD_OP_READ);

	dout("rbd_req_cb bytes=%lld readop=%d rc=%d\n", bytes, read_op, rc);

	if (rc == -ENOENT && read_op) {
		zero_bio_chain(req_data->bio, 0);
		rc = 0;
	} else if (rc == 0 && read_op && bytes < req_data->len) {
		zero_bio_chain(req_data->bio, bytes);
		bytes = req_data->len;
	}

	blk_end_request(req_data->rq, rc, bytes);

	if (req_data->bio)
		bio_chain_put(req_data->bio);

	ceph_osdc_put_request(req);
	kfree(req_data);
}

/*
 * Do a synchronous ceph osd operation
 */
static int rbd_req_sync_op(struct rbd_device *dev,
			   struct ceph_snap_context *snapc,
			   u64 snapid,
			   int opcode,
			   int flags,
			   struct ceph_osd_req_op *orig_ops,
			   int num_reply,
			   const char *obj,
			   u64 ofs, u64 len,
			   char *buf)
{
	int ret;
	struct page **pages;
	int num_pages;
	struct ceph_osd_req_op *ops = orig_ops;
	u32 payload_len;

	num_pages = calc_pages_for(ofs , len);
	pages = ceph_alloc_page_vector(num_pages, GFP_KERNEL);
	if (IS_ERR(pages))
		return PTR_ERR(pages);

	if (!orig_ops) {
		payload_len = (flags & CEPH_OSD_FLAG_WRITE ? len : 0);
		ret = rbd_create_rw_ops(&ops, 1, opcode, payload_len);
		if (ret < 0)
			goto done;

		if ((flags & CEPH_OSD_FLAG_WRITE) && buf) {
			ret = ceph_copy_to_page_vector(pages, buf, ofs, len);
			if (ret < 0)
				goto done_ops;
		}
	}

	ret = rbd_do_request(NULL, dev, snapc, snapid,
			  obj, ofs, len, NULL,
			  pages, num_pages,
			  flags,
			  ops,
			  2,
			  NULL);
	if (ret < 0)
		goto done_ops;

	if ((flags & CEPH_OSD_FLAG_READ) && buf)
		ret = ceph_copy_from_page_vector(pages, buf, ofs, ret);

done_ops:
	if (!orig_ops)
		rbd_destroy_ops(ops);
done:
	ceph_release_page_vector(pages, num_pages);
	return ret;
}

/*
 * Do an asynchronous ceph osd operation
 */
static int rbd_do_op(struct request *rq,
		     struct rbd_device *rbd_dev ,
		     struct ceph_snap_context *snapc,
		     u64 snapid,
		     int opcode, int flags, int num_reply,
		     u64 ofs, u64 len,
		     struct bio *bio)
{
	char *seg_name;
	u64 seg_ofs;
	u64 seg_len;
	int ret;
	struct ceph_osd_req_op *ops;
	u32 payload_len;

	seg_name = kmalloc(RBD_MAX_SEG_NAME_LEN + 1, GFP_NOIO);
	if (!seg_name)
		return -ENOMEM;

	seg_len = rbd_get_segment(&rbd_dev->header,
				  rbd_dev->header.block_name,
				  ofs, len,
				  seg_name, &seg_ofs);

	payload_len = (flags & CEPH_OSD_FLAG_WRITE ? seg_len : 0);

	ret = rbd_create_rw_ops(&ops, 1, opcode, payload_len);
	if (ret < 0)
		goto done;

	/* we've taken care of segment sizes earlier when we
	   cloned the bios. We should never have a segment
	   truncated at this point */
	BUG_ON(seg_len < len);

	ret = rbd_do_request(rq, rbd_dev, snapc, snapid,
			     seg_name, seg_ofs, seg_len,
			     bio,
			     NULL, 0,
			     flags,
			     ops,
			     num_reply,
			     rbd_req_cb);
done:
	kfree(seg_name);
	return ret;
}

/*
 * Request async osd write
 */
static int rbd_req_write(struct request *rq,
			 struct rbd_device *rbd_dev,
			 struct ceph_snap_context *snapc,
			 u64 ofs, u64 len,
			 struct bio *bio)
{
	return rbd_do_op(rq, rbd_dev, snapc, CEPH_NOSNAP,
			 CEPH_OSD_OP_WRITE,
			 CEPH_OSD_FLAG_WRITE | CEPH_OSD_FLAG_ONDISK,
			 2,
			 ofs, len, bio);
}

/*
 * Request async osd read
 */
static int rbd_req_read(struct request *rq,
			 struct rbd_device *rbd_dev,
			 u64 snapid,
			 u64 ofs, u64 len,
			 struct bio *bio)
{
	return rbd_do_op(rq, rbd_dev, NULL,
			 (snapid ? snapid : CEPH_NOSNAP),
			 CEPH_OSD_OP_READ,
			 CEPH_OSD_FLAG_READ,
			 2,
			 ofs, len, bio);
}

/*
 * Request sync osd read
 */
static int rbd_req_sync_read(struct rbd_device *dev,
			  struct ceph_snap_context *snapc,
			  u64 snapid,
			  const char *obj,
			  u64 ofs, u64 len,
			  char *buf)
{
	return rbd_req_sync_op(dev, NULL,
			       (snapid ? snapid : CEPH_NOSNAP),
			       CEPH_OSD_OP_READ,
			       CEPH_OSD_FLAG_READ,
			       NULL,
			       1, obj, ofs, len, buf);
}

/*
 * Request sync osd read
 */
static int rbd_req_sync_rollback_obj(struct rbd_device *dev,
				     u64 snapid,
				     const char *obj)
{
	struct ceph_osd_req_op *ops;
	int ret = rbd_create_rw_ops(&ops, 1, CEPH_OSD_OP_ROLLBACK, 0);
	if (ret < 0)
		return ret;

	ops[0].snap.snapid = snapid;

	ret = rbd_req_sync_op(dev, NULL,
			       CEPH_NOSNAP,
			       0,
			       CEPH_OSD_FLAG_WRITE | CEPH_OSD_FLAG_ONDISK,
			       ops,
			       1, obj, 0, 0, NULL);

	rbd_destroy_ops(ops);

	if (ret < 0)
		return ret;

	return ret;
}

/*
 * Request sync osd read
 */
static int rbd_req_sync_exec(struct rbd_device *dev,
			     const char *obj,
			     const char *cls,
			     const char *method,
			     const char *data,
			     int len)
{
	struct ceph_osd_req_op *ops;
	int cls_len = strlen(cls);
	int method_len = strlen(method);
	int ret = rbd_create_rw_ops(&ops, 1, CEPH_OSD_OP_CALL,
				    cls_len + method_len + len);
	if (ret < 0)
		return ret;

	ops[0].cls.class_name = cls;
	ops[0].cls.class_len = (__u8)cls_len;
	ops[0].cls.method_name = method;
	ops[0].cls.method_len = (__u8)method_len;
	ops[0].cls.argc = 0;
	ops[0].cls.indata = data;
	ops[0].cls.indata_len = len;

	ret = rbd_req_sync_op(dev, NULL,
			       CEPH_NOSNAP,
			       0,
			       CEPH_OSD_FLAG_WRITE | CEPH_OSD_FLAG_ONDISK,
			       ops,
			       1, obj, 0, 0, NULL);

	rbd_destroy_ops(ops);

	dout("cls_exec returned %d\n", ret);
	return ret;
}

/*
 * block device queue callback
 */
static void rbd_rq_fn(struct request_queue *q)
{
	struct rbd_device *rbd_dev = q->queuedata;
	struct request *rq;
	struct bio_pair *bp = NULL;

	rq = blk_fetch_request(q);

	while (1) {
		struct bio *bio;
		struct bio *rq_bio, *next_bio = NULL;
		bool do_write;
		int size, op_size = 0;
		u64 ofs;

		/* peek at request from block layer */
		if (!rq)
			break;

		dout("fetched request\n");

		/* filter out block requests we don't understand */
		if ((rq->cmd_type != REQ_TYPE_FS)) {
			__blk_end_request_all(rq, 0);
			goto next;
		}

		/* deduce our operation (read, write) */
		do_write = (rq_data_dir(rq) == WRITE);

		size = blk_rq_bytes(rq);
		ofs = blk_rq_pos(rq) * 512ULL;
		rq_bio = rq->bio;
		if (do_write && rbd_dev->read_only) {
			__blk_end_request_all(rq, -EROFS);
			goto next;
		}

		spin_unlock_irq(q->queue_lock);

		dout("%s 0x%x bytes at 0x%llx\n",
		     do_write ? "write" : "read",
		     size, blk_rq_pos(rq) * 512ULL);

		do {
			/* a bio clone to be passed down to OSD req */
			dout("rq->bio->bi_vcnt=%d\n", rq->bio->bi_vcnt);
			op_size = rbd_get_segment(&rbd_dev->header,
						  rbd_dev->header.block_name,
						  ofs, size,
						  NULL, NULL);
			bio = bio_chain_clone(&rq_bio, &next_bio, &bp,
					      op_size, GFP_ATOMIC);
			if (!bio) {
				spin_lock_irq(q->queue_lock);
				__blk_end_request_all(rq, -ENOMEM);
				goto next;
			}

			/* init OSD command: write or read */
			if (do_write)
				rbd_req_write(rq, rbd_dev,
					      rbd_dev->header.snapc,
					      ofs,
					      op_size, bio);
			else
				rbd_req_read(rq, rbd_dev,
					     cur_snap_id(rbd_dev),
					     ofs,
					     op_size, bio);

			size -= op_size;
			ofs += op_size;

			rq_bio = next_bio;
		} while (size > 0);

		if (bp)
			bio_pair_release(bp);

		spin_lock_irq(q->queue_lock);
next:
		rq = blk_fetch_request(q);
	}
}

/*
 * a queue callback. Makes sure that we don't create a bio that spans across
 * multiple osd objects. One exception would be with a single page bios,
 * which we handle later at bio_chain_clone
 */
static int rbd_merge_bvec(struct request_queue *q, struct bvec_merge_data *bmd,
			  struct bio_vec *bvec)
{
	struct rbd_device *rbd_dev = q->queuedata;
	unsigned int chunk_sectors = 1 << (rbd_dev->header.obj_order - 9);
	sector_t sector = bmd->bi_sector + get_start_sect(bmd->bi_bdev);
	unsigned int bio_sectors = bmd->bi_size >> 9;
	int max;

	max =  (chunk_sectors - ((sector & (chunk_sectors - 1))
				 + bio_sectors)) << 9;
	if (max < 0)
		max = 0; /* bio_add cannot handle a negative return */
	if (max <= bvec->bv_len && bio_sectors == 0)
		return bvec->bv_len;
	return max;
}

static void rbd_free_disk(struct rbd_device *rbd_dev)
{
	struct gendisk *disk = rbd_dev->disk;

	if (!disk)
		return;

	rbd_header_free(&rbd_dev->header);

	if (disk->flags & GENHD_FL_UP)
		del_gendisk(disk);
	if (disk->queue)
		blk_cleanup_queue(disk->queue);
	put_disk(disk);
}

/*
 * reload the ondisk the header 
 */
static int rbd_read_header(struct rbd_device *rbd_dev,
			   struct rbd_image_header *header)
{
	ssize_t rc;
	struct rbd_image_header_ondisk *dh;
	int snap_count = 0;
	u64 snap_names_len = 0;

	while (1) {
		int len = sizeof(*dh) +
			  snap_count * sizeof(struct rbd_image_snap_ondisk) +
			  snap_names_len;

		rc = -ENOMEM;
		dh = kmalloc(len, GFP_KERNEL);
		if (!dh)
			return -ENOMEM;

		rc = rbd_req_sync_read(rbd_dev,
				       NULL, CEPH_NOSNAP,
				       rbd_dev->obj_md_name,
				       0, len,
				       (char *)dh);
		if (rc < 0)
			goto out_dh;

		rc = rbd_header_from_disk(header, dh, snap_count, GFP_KERNEL);
		if (rc < 0)
			goto out_dh;

		if (snap_count != header->total_snaps) {
			snap_count = header->total_snaps;
			snap_names_len = header->snap_names_len;
			rbd_header_free(header);
			kfree(dh);
			continue;
		}
		break;
	}

out_dh:
	kfree(dh);
	return rc;
}

/*
 * create a snapshot
 */
static int rbd_header_add_snap(struct rbd_device *dev,
			       const char *snap_name,
			       gfp_t gfp_flags)
{
	int name_len = strlen(snap_name);
	u64 new_snapid;
	int ret;
	void *data, *data_start, *data_end;

	/* we should create a snapshot only if we're pointing at the head */
	if (dev->cur_snap)
		return -EINVAL;

	ret = ceph_monc_create_snapid(&dev->client->monc, dev->poolid,
				      &new_snapid);
	dout("created snapid=%lld\n", new_snapid);
	if (ret < 0)
		return ret;

	data = kmalloc(name_len + 16, gfp_flags);
	if (!data)
		return -ENOMEM;

	data_start = data;
	data_end = data + name_len + 16;

	ceph_encode_string_safe(&data, data_end, snap_name, name_len, bad);
	ceph_encode_64_safe(&data, data_end, new_snapid, bad);

	ret = rbd_req_sync_exec(dev, dev->obj_md_name, "rbd", "snap_add",
				data_start, data - data_start);

	kfree(data_start);

	if (ret < 0)
		return ret;

	dev->header.snapc->seq =  new_snapid;

	return 0;
bad:
	return -ERANGE;
}

/*
 * only read the first part of the ondisk header, without the snaps info
 */
static int rbd_update_snaps(struct rbd_device *rbd_dev)
{
	int ret;
	struct rbd_image_header h;
	u64 snap_seq;

	ret = rbd_read_header(rbd_dev, &h);
	if (ret < 0)
		return ret;

	down_write(&rbd_dev->header.snap_rwsem);

	snap_seq = rbd_dev->header.snapc->seq;

	kfree(rbd_dev->header.snapc);
	kfree(rbd_dev->header.snap_names);
	kfree(rbd_dev->header.snap_sizes);

	rbd_dev->header.total_snaps = h.total_snaps;
	rbd_dev->header.snapc = h.snapc;
	rbd_dev->header.snap_names = h.snap_names;
	rbd_dev->header.snap_sizes = h.snap_sizes;
	rbd_dev->header.snapc->seq = snap_seq;

	up_write(&rbd_dev->header.snap_rwsem);

	return 0;
}

static int rbd_init_disk(struct rbd_device *rbd_dev)
{
	struct gendisk *disk;
	struct request_queue *q;
	int rc;
	u64 total_size = 0;

	/* contact OSD, request size info about the object being mapped */
	rc = rbd_read_header(rbd_dev, &rbd_dev->header);
	if (rc)
		return rc;

	rc = rbd_header_set_snap(rbd_dev, rbd_dev->snap_name, &total_size);
	if (rc)
		return rc;

	/* create gendisk info */
	rc = -ENOMEM;
	disk = alloc_disk(RBD_MINORS_PER_MAJOR);
	if (!disk)
		goto out;

	sprintf(disk->disk_name, DRV_NAME "%d", rbd_dev->id);
	disk->major = rbd_dev->major;
	disk->first_minor = 0;
	disk->fops = &rbd_bd_ops;
	disk->private_data = rbd_dev;

	/* init rq */
	rc = -ENOMEM;
	q = blk_init_queue(rbd_rq_fn, &rbd_dev->lock);
	if (!q)
		goto out_disk;
	blk_queue_merge_bvec(q, rbd_merge_bvec);
	disk->queue = q;

	q->queuedata = rbd_dev;

	rbd_dev->disk = disk;
	rbd_dev->q = q;

	/* finally, announce the disk to the world */
	set_capacity(disk, total_size / 512ULL);
	add_disk(disk);

	pr_info("%s: added with size 0x%llx\n",
		disk->disk_name, (unsigned long long)total_size);
	return 0;

out_disk:
	put_disk(disk);
out:
	return rc;
}

/********************************************************************
 * /sys/class/rbd/
 *                   add	map rados objects to blkdev
 *                   remove	unmap rados objects
 *                   list	show mappings
 *******************************************************************/

static void class_rbd_release(struct class *cls)
{
	kfree(cls);
}

static ssize_t class_rbd_list(struct class *c,
			      struct class_attribute *attr,
			      char *data)
{
	int n = 0;
	struct list_head *tmp;
	int max = PAGE_SIZE;

	mutex_lock_nested(&ctl_mutex, SINGLE_DEPTH_NESTING);

	n += snprintf(data, max,
		      "#id\tmajor\tclient_name\tpool\tname\tsnap\tKB\n");

	list_for_each(tmp, &rbd_dev_list) {
		struct rbd_device *rbd_dev;

		rbd_dev = list_entry(tmp, struct rbd_device, node);
		n += snprintf(data+n, max-n,
			      "%d\t%d\tclient%lld\t%s\t%s\t%s\t%lld\n",
			      rbd_dev->id,
			      rbd_dev->major,
			      ceph_client_id(rbd_dev->client),
			      rbd_dev->pool_name,
			      rbd_dev->obj, rbd_dev->snap_name,
			      rbd_dev->header.image_size >> 10);
		if (n == max)
			break;
	}

	mutex_unlock(&ctl_mutex);
	return n;
}

static ssize_t class_rbd_add(struct class *c,
			     struct class_attribute *attr,
			     const char *buf, size_t count)
{
	struct ceph_osd_client *osdc;
	struct rbd_device *rbd_dev;
	ssize_t rc = -ENOMEM;
	int irc, new_id = 0;
	struct list_head *tmp;
	char *mon_dev_name;
	char *options;

	if (!try_module_get(THIS_MODULE))
		return -ENODEV;

	mon_dev_name = kmalloc(RBD_MAX_OPT_LEN, GFP_KERNEL);
	if (!mon_dev_name)
		goto err_out_mod;

	options = kmalloc(RBD_MAX_OPT_LEN, GFP_KERNEL);
	if (!options)
		goto err_mon_dev;

	/* new rbd_device object */
	rbd_dev = kzalloc(sizeof(*rbd_dev), GFP_KERNEL);
	if (!rbd_dev)
		goto err_out_opt;

	/* static rbd_device initialization */
	spin_lock_init(&rbd_dev->lock);
	INIT_LIST_HEAD(&rbd_dev->node);

	/* generate unique id: find highest unique id, add one */
	mutex_lock_nested(&ctl_mutex, SINGLE_DEPTH_NESTING);

	list_for_each(tmp, &rbd_dev_list) {
		struct rbd_device *rbd_dev;

		rbd_dev = list_entry(tmp, struct rbd_device, node);
		if (rbd_dev->id >= new_id)
			new_id = rbd_dev->id + 1;
	}

	rbd_dev->id = new_id;

	/* add to global list */
	list_add_tail(&rbd_dev->node, &rbd_dev_list);

	/* parse add command */
	if (sscanf(buf, "%" __stringify(RBD_MAX_OPT_LEN) "s "
		   "%" __stringify(RBD_MAX_OPT_LEN) "s "
		   "%" __stringify(RBD_MAX_POOL_NAME_LEN) "s "
		   "%" __stringify(RBD_MAX_OBJ_NAME_LEN) "s"
		   "%" __stringify(RBD_MAX_SNAP_NAME_LEN) "s",
		   mon_dev_name, options, rbd_dev->pool_name,
		   rbd_dev->obj, rbd_dev->snap_name) < 4) {
		rc = -EINVAL;
		goto err_out_slot;
	}

	if (rbd_dev->snap_name[0] == 0)
		rbd_dev->snap_name[0] = '-';

	rbd_dev->obj_len = strlen(rbd_dev->obj);
	snprintf(rbd_dev->obj_md_name, sizeof(rbd_dev->obj_md_name), "%s%s",
		 rbd_dev->obj, RBD_SUFFIX);

	/* initialize rest of new object */
	snprintf(rbd_dev->name, DEV_NAME_LEN, DRV_NAME "%d", rbd_dev->id);
	rc = rbd_get_client(rbd_dev, mon_dev_name, options);
	if (rc < 0)
		goto err_out_slot;

	mutex_unlock(&ctl_mutex);

	/* pick the pool */
	osdc = &rbd_dev->client->osdc;
	rc = ceph_pg_poolid_by_name(osdc->osdmap, rbd_dev->pool_name);
	if (rc < 0)
		goto err_out_client;
	rbd_dev->poolid = rc;

	/* register our block device */
	irc = register_blkdev(0, rbd_dev->name);
	if (irc < 0) {
		rc = irc;
		goto err_out_client;
	}
	rbd_dev->major = irc;

	/* set up and announce blkdev mapping */
	rc = rbd_init_disk(rbd_dev);
	if (rc)
		goto err_out_blkdev;

	return count;

err_out_blkdev:
	unregister_blkdev(rbd_dev->major, rbd_dev->name);
err_out_client:
	rbd_put_client(rbd_dev);
	mutex_lock_nested(&ctl_mutex, SINGLE_DEPTH_NESTING);
err_out_slot:
	list_del_init(&rbd_dev->node);
	mutex_unlock(&ctl_mutex);

	kfree(rbd_dev);
err_out_opt:
	kfree(options);
err_mon_dev:
	kfree(mon_dev_name);
err_out_mod:
	dout("Error adding device %s\n", buf);
	module_put(THIS_MODULE);
	return rc;
}

static struct rbd_device *__rbd_get_dev(unsigned long id)
{
	struct list_head *tmp;
	struct rbd_device *rbd_dev;

	list_for_each(tmp, &rbd_dev_list) {
		rbd_dev = list_entry(tmp, struct rbd_device, node);
		if (rbd_dev->id == id)
			return rbd_dev;
	}
	return NULL;
}

static ssize_t class_rbd_remove(struct class *c,
				struct class_attribute *attr,
				const char *buf,
				size_t count)
{
	struct rbd_device *rbd_dev = NULL;
	int target_id, rc;
	unsigned long ul;

	rc = strict_strtoul(buf, 10, &ul);
	if (rc)
		return rc;

	/* convert to int; abort if we lost anything in the conversion */
	target_id = (int) ul;
	if (target_id != ul)
		return -EINVAL;

	/* remove object from list immediately */
	mutex_lock_nested(&ctl_mutex, SINGLE_DEPTH_NESTING);

	rbd_dev = __rbd_get_dev(target_id);
	if (rbd_dev)
		list_del_init(&rbd_dev->node);

	mutex_unlock(&ctl_mutex);

	if (!rbd_dev)
		return -ENOENT;

	rbd_put_client(rbd_dev);

	/* clean up and free blkdev */
	rbd_free_disk(rbd_dev);
	unregister_blkdev(rbd_dev->major, rbd_dev->name);
	kfree(rbd_dev);

	/* release module ref */
	module_put(THIS_MODULE);

	return count;
}

static ssize_t class_rbd_snaps_list(struct class *c,
			      struct class_attribute *attr,
			      char *data)
{
	struct rbd_device *rbd_dev = NULL;
	struct list_head *tmp;
	struct rbd_image_header *header;
	int i, n = 0, max = PAGE_SIZE;
	int ret;

	mutex_lock_nested(&ctl_mutex, SINGLE_DEPTH_NESTING);

	n += snprintf(data, max, "#id\tsnap\tKB\n");

	list_for_each(tmp, &rbd_dev_list) {
		char *names, *p;
		struct ceph_snap_context *snapc;

		rbd_dev = list_entry(tmp, struct rbd_device, node);
		header = &rbd_dev->header;

		down_read(&header->snap_rwsem);

		names = header->snap_names;
		snapc = header->snapc;

		n += snprintf(data + n, max - n, "%d\t%s\t%lld%s\n",
			      rbd_dev->id, RBD_SNAP_HEAD_NAME,
			      header->image_size >> 10,
			      (!rbd_dev->cur_snap ? " (*)" : ""));
		if (n == max)
			break;

		p = names;
		for (i = 0; i < header->total_snaps; i++, p += strlen(p) + 1) {
			n += snprintf(data + n, max - n, "%d\t%s\t%lld%s\n",
			      rbd_dev->id, p, header->snap_sizes[i] >> 10,
			      (rbd_dev->cur_snap &&
			       (snap_index(header, i) == rbd_dev->cur_snap) ?
			       " (*)" : ""));
			if (n == max)
				break;
		}

		up_read(&header->snap_rwsem);
	}


	ret = n;
	mutex_unlock(&ctl_mutex);
	return ret;
}

static ssize_t class_rbd_snaps_refresh(struct class *c,
				struct class_attribute *attr,
				const char *buf,
				size_t count)
{
	struct rbd_device *rbd_dev = NULL;
	int target_id, rc;
	unsigned long ul;
	int ret = count;

	rc = strict_strtoul(buf, 10, &ul);
	if (rc)
		return rc;

	/* convert to int; abort if we lost anything in the conversion */
	target_id = (int) ul;
	if (target_id != ul)
		return -EINVAL;

	mutex_lock_nested(&ctl_mutex, SINGLE_DEPTH_NESTING);

	rbd_dev = __rbd_get_dev(target_id);
	if (!rbd_dev) {
		ret = -ENOENT;
		goto done;
	}

	rc = rbd_update_snaps(rbd_dev);
	if (rc < 0)
		ret = rc;

done:
	mutex_unlock(&ctl_mutex);
	return ret;
}

static ssize_t class_rbd_snap_create(struct class *c,
				struct class_attribute *attr,
				const char *buf,
				size_t count)
{
	struct rbd_device *rbd_dev = NULL;
	int target_id, ret;
	char *name;

	name = kmalloc(RBD_MAX_SNAP_NAME_LEN + 1, GFP_KERNEL);
	if (!name)
		return -ENOMEM;

	/* parse snaps add command */
	if (sscanf(buf, "%d "
		   "%" __stringify(RBD_MAX_SNAP_NAME_LEN) "s",
		   &target_id,
		   name) != 2) {
		ret = -EINVAL;
		goto done;
	}

	mutex_lock_nested(&ctl_mutex, SINGLE_DEPTH_NESTING);

	rbd_dev = __rbd_get_dev(target_id);
	if (!rbd_dev) {
		ret = -ENOENT;
		goto done_unlock;
	}

	ret = rbd_header_add_snap(rbd_dev,
				  name, GFP_KERNEL);
	if (ret < 0)
		goto done_unlock;

	ret = rbd_update_snaps(rbd_dev);
	if (ret < 0)
		goto done_unlock;

	ret = count;
done_unlock:
	mutex_unlock(&ctl_mutex);
done:
	kfree(name);
	return ret;
}

static ssize_t class_rbd_rollback(struct class *c,
				struct class_attribute *attr,
				const char *buf,
				size_t count)
{
	struct rbd_device *rbd_dev = NULL;
	int target_id, ret;
	u64 snapid;
	char snap_name[RBD_MAX_SNAP_NAME_LEN];
	u64 cur_ofs;
	char *seg_name;

	/* parse snaps add command */
	if (sscanf(buf, "%d "
		   "%" __stringify(RBD_MAX_SNAP_NAME_LEN) "s",
		   &target_id,
		   snap_name) != 2) {
		return -EINVAL;
	}

	ret = -ENOMEM;
	seg_name = kmalloc(RBD_MAX_SEG_NAME_LEN + 1, GFP_NOIO);
	if (!seg_name)
		return ret;

	mutex_lock_nested(&ctl_mutex, SINGLE_DEPTH_NESTING);

	rbd_dev = __rbd_get_dev(target_id);
	if (!rbd_dev) {
		ret = -ENOENT;
		goto done_unlock;
	}

	ret = snap_by_name(&rbd_dev->header, snap_name, &snapid, NULL);
	if (ret < 0)
		goto done_unlock;

	dout("snapid=%lld\n", snapid);

	cur_ofs = 0;
	while (cur_ofs < rbd_dev->header.image_size) {
		cur_ofs += rbd_get_segment(&rbd_dev->header,
					   rbd_dev->obj,
					   cur_ofs, (u64)-1,
					   seg_name, NULL);
		dout("seg_name=%s\n", seg_name);

		ret = rbd_req_sync_rollback_obj(rbd_dev, snapid, seg_name);
		if (ret < 0)
			pr_warning("could not roll back obj %s err=%d\n",
				   seg_name, ret);
	}

	ret = rbd_update_snaps(rbd_dev);
	if (ret < 0)
		goto done_unlock;

	ret = count;

done_unlock:
	mutex_unlock(&ctl_mutex);
	kfree(seg_name);

	return ret;
}

static struct class_attribute class_rbd_attrs[] = {
	__ATTR(add,		0200, NULL, class_rbd_add),
	__ATTR(remove,		0200, NULL, class_rbd_remove),
	__ATTR(list,		0444, class_rbd_list, NULL),
	__ATTR(snaps_refresh,	0200, NULL, class_rbd_snaps_refresh),
	__ATTR(snap_create,	0200, NULL, class_rbd_snap_create),
	__ATTR(snaps_list,	0444, class_rbd_snaps_list, NULL),
	__ATTR(snap_rollback,	0200, NULL, class_rbd_rollback),
	__ATTR_NULL
};

/*
 * create control files in sysfs
 * /sys/class/rbd/...
 */
static int rbd_sysfs_init(void)
{
	int ret = -ENOMEM;

	class_rbd = kzalloc(sizeof(*class_rbd), GFP_KERNEL);
	if (!class_rbd)
		goto out;

	class_rbd->name = DRV_NAME;
	class_rbd->owner = THIS_MODULE;
	class_rbd->class_release = class_rbd_release;
	class_rbd->class_attrs = class_rbd_attrs;

	ret = class_register(class_rbd);
	if (ret)
		goto out_class;
	return 0;

out_class:
	kfree(class_rbd);
	class_rbd = NULL;
	pr_err(DRV_NAME ": failed to create class rbd\n");
out:
	return ret;
}

static void rbd_sysfs_cleanup(void)
{
	if (class_rbd)
		class_destroy(class_rbd);
	class_rbd = NULL;
}

int __init rbd_init(void)
{
	int rc;

	rc = rbd_sysfs_init();
	if (rc)
		return rc;
	spin_lock_init(&node_lock);
	pr_info("loaded " DRV_NAME_LONG "\n");
	return 0;
}

void __exit rbd_exit(void)
{
	rbd_sysfs_cleanup();
}

module_init(rbd_init);
module_exit(rbd_exit);

MODULE_AUTHOR("Sage Weil <sage@newdream.net>");
MODULE_AUTHOR("Yehuda Sadeh <yehuda@hq.newdream.net>");
MODULE_DESCRIPTION("rados block device");

/* following authorship retained from original osdblk.c */
MODULE_AUTHOR("Jeff Garzik <jeff@garzik.org>");

MODULE_LICENSE("GPL");
