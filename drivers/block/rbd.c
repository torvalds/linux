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



   For usage instructions, please refer to:

                 Documentation/ABI/testing/sysfs-bus-rbd

 */

#include <linux/ceph/libceph.h>
#include <linux/ceph/osd_client.h>
#include <linux/ceph/mon_client.h>
#include <linux/ceph/decode.h>
#include <linux/parser.h>

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

#define RBD_NOTIFY_TIMEOUT_DEFAULT 10

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

	u64 obj_version;
};

struct rbd_options {
	int	notify_timeout;
};

/*
 * an instance of the client.  multiple devices may share a client.
 */
struct rbd_client {
	struct ceph_client	*client;
	struct rbd_options	*rbd_opts;
	struct kref		kref;
	struct list_head	node;
};

struct rbd_req_coll;

/*
 * a single io request
 */
struct rbd_request {
	struct request		*rq;		/* blk layer request */
	struct bio		*bio;		/* cloned bio */
	struct page		**pages;	/* list of used pages */
	u64			len;
	int			coll_index;
	struct rbd_req_coll	*coll;
};

struct rbd_req_status {
	int done;
	int rc;
	u64 bytes;
};

/*
 * a collection of requests
 */
struct rbd_req_coll {
	int			total;
	int			num_done;
	struct kref		kref;
	struct rbd_req_status	status[0];
};

struct rbd_snap {
	struct	device		dev;
	const char		*name;
	size_t			size;
	struct list_head	node;
	u64			id;
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

	struct ceph_osd_event   *watch_event;
	struct ceph_osd_request *watch_request;

	char                    snap_name[RBD_MAX_SNAP_NAME_LEN];
	u32 cur_snap;	/* index+1 of current snapshot within snap context
			   0 - for the head */
	int read_only;

	struct list_head	node;

	/* list of snapshots */
	struct list_head	snaps;

	/* sysfs related */
	struct device		dev;
};

static struct bus_type rbd_bus_type = {
	.name		= "rbd",
};

static spinlock_t node_lock;      /* protects client get/put */

static DEFINE_MUTEX(ctl_mutex);	  /* Serialize open/close/setup/teardown */
static LIST_HEAD(rbd_dev_list);    /* devices */
static LIST_HEAD(rbd_client_list);      /* clients */

static int __rbd_init_snaps_header(struct rbd_device *rbd_dev);
static void rbd_dev_release(struct device *dev);
static ssize_t rbd_snap_add(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf,
			    size_t count);
static void __rbd_remove_snap_dev(struct rbd_device *rbd_dev,
				  struct rbd_snap *snap);


static struct rbd_device *dev_to_rbd(struct device *dev)
{
	return container_of(dev, struct rbd_device, dev);
}

static struct device *rbd_get_dev(struct rbd_device *rbd_dev)
{
	return get_device(&rbd_dev->dev);
}

static void rbd_put_dev(struct rbd_device *rbd_dev)
{
	put_device(&rbd_dev->dev);
}

static int __rbd_update_snaps(struct rbd_device *rbd_dev);

static int rbd_open(struct block_device *bdev, fmode_t mode)
{
	struct gendisk *disk = bdev->bd_disk;
	struct rbd_device *rbd_dev = disk->private_data;

	rbd_get_dev(rbd_dev);

	set_device_ro(bdev, rbd_dev->read_only);

	if ((mode & FMODE_WRITE) && rbd_dev->read_only)
		return -EROFS;

	return 0;
}

static int rbd_release(struct gendisk *disk, fmode_t mode)
{
	struct rbd_device *rbd_dev = disk->private_data;

	rbd_put_dev(rbd_dev);

	return 0;
}

static const struct block_device_operations rbd_bd_ops = {
	.owner			= THIS_MODULE,
	.open			= rbd_open,
	.release		= rbd_release,
};

/*
 * Initialize an rbd client instance.
 * We own *opt.
 */
static struct rbd_client *rbd_client_create(struct ceph_options *opt,
					    struct rbd_options *rbd_opts)
{
	struct rbd_client *rbdc;
	int ret = -ENOMEM;

	dout("rbd_client_create\n");
	rbdc = kmalloc(sizeof(struct rbd_client), GFP_KERNEL);
	if (!rbdc)
		goto out_opt;

	kref_init(&rbdc->kref);
	INIT_LIST_HEAD(&rbdc->node);

	rbdc->client = ceph_create_client(opt, rbdc, 0, 0);
	if (IS_ERR(rbdc->client))
		goto out_rbdc;
	opt = NULL; /* Now rbdc->client is responsible for opt */

	ret = ceph_open_session(rbdc->client);
	if (ret < 0)
		goto out_err;

	rbdc->rbd_opts = rbd_opts;

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
 * mount options
 */
enum {
	Opt_notify_timeout,
	Opt_last_int,
	/* int args above */
	Opt_last_string,
	/* string args above */
};

static match_table_t rbdopt_tokens = {
	{Opt_notify_timeout, "notify_timeout=%d"},
	/* int args above */
	/* string args above */
	{-1, NULL}
};

static int parse_rbd_opts_token(char *c, void *private)
{
	struct rbd_options *rbdopt = private;
	substring_t argstr[MAX_OPT_ARGS];
	int token, intval, ret;

	token = match_token((char *)c, rbdopt_tokens, argstr);
	if (token < 0)
		return -EINVAL;

	if (token < Opt_last_int) {
		ret = match_int(&argstr[0], &intval);
		if (ret < 0) {
			pr_err("bad mount option arg (not int) "
			       "at '%s'\n", c);
			return ret;
		}
		dout("got int token %d val %d\n", token, intval);
	} else if (token > Opt_last_int && token < Opt_last_string) {
		dout("got string token %d val %s\n", token,
		     argstr[0].from);
	} else {
		dout("got token %d\n", token);
	}

	switch (token) {
	case Opt_notify_timeout:
		rbdopt->notify_timeout = intval;
		break;
	default:
		BUG_ON(token);
	}
	return 0;
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
	struct rbd_options *rbd_opts;

	rbd_opts = kzalloc(sizeof(*rbd_opts), GFP_KERNEL);
	if (!rbd_opts)
		return -ENOMEM;

	rbd_opts->notify_timeout = RBD_NOTIFY_TIMEOUT_DEFAULT;

	ret = ceph_parse_options(&opt, options, mon_addr,
				 mon_addr + strlen(mon_addr), parse_rbd_opts_token, rbd_opts);
	if (ret < 0)
		goto done_err;

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

	rbdc = rbd_client_create(opt, rbd_opts);
	if (IS_ERR(rbdc)) {
		ret = PTR_ERR(rbdc);
		goto done_err;
	}

	rbd_dev->rbd_client = rbdc;
	rbd_dev->client = rbdc->client;
	return 0;
done_err:
	kfree(rbd_opts);
	return ret;
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
	kfree(rbdc->rbd_opts);
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
 * Destroy requests collection
 */
static void rbd_coll_release(struct kref *kref)
{
	struct rbd_req_coll *coll =
		container_of(kref, struct rbd_req_coll, kref);

	dout("rbd_coll_release %p\n", coll);
	kfree(coll);
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

	if (memcmp(ondisk, RBD_HEADER_TEXT, sizeof(RBD_HEADER_TEXT))) {
		return -ENXIO;
	}

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

static int rbd_get_num_segments(struct rbd_image_header *header,
				u64 ofs, u64 len)
{
	u64 start_seg = ofs >> header->obj_order;
	u64 end_seg = (ofs + len - 1) >> header->obj_order;
	return end_seg - start_seg + 1;
}

/*
 * returns the size of an object in the image
 */
static u64 rbd_obj_bytes(struct rbd_image_header *header)
{
	return 1 << header->obj_order;
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

static void rbd_coll_end_req_index(struct request *rq,
				   struct rbd_req_coll *coll,
				   int index,
				   int ret, u64 len)
{
	struct request_queue *q;
	int min, max, i;

	dout("rbd_coll_end_req_index %p index %d ret %d len %lld\n",
	     coll, index, ret, len);

	if (!rq)
		return;

	if (!coll) {
		blk_end_request(rq, ret, len);
		return;
	}

	q = rq->q;

	spin_lock_irq(q->queue_lock);
	coll->status[index].done = 1;
	coll->status[index].rc = ret;
	coll->status[index].bytes = len;
	max = min = coll->num_done;
	while (max < coll->total && coll->status[max].done)
		max++;

	for (i = min; i<max; i++) {
		__blk_end_request(rq, coll->status[i].rc,
				  coll->status[i].bytes);
		coll->num_done++;
		kref_put(&coll->kref, rbd_coll_release);
	}
	spin_unlock_irq(q->queue_lock);
}

static void rbd_coll_end_req(struct rbd_request *req,
			     int ret, u64 len)
{
	rbd_coll_end_req_index(req->rq, req->coll, req->coll_index, ret, len);
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
			  struct rbd_req_coll *coll,
			  int coll_index,
			  void (*rbd_cb)(struct ceph_osd_request *req,
					 struct ceph_msg *msg),
			  struct ceph_osd_request **linger_req,
			  u64 *ver)
{
	struct ceph_osd_request *req;
	struct ceph_file_layout *layout;
	int ret;
	u64 bno;
	struct timespec mtime = CURRENT_TIME;
	struct rbd_request *req_data;
	struct ceph_osd_request_head *reqhead;
	struct rbd_image_header *header = &dev->header;

	req_data = kzalloc(sizeof(*req_data), GFP_NOIO);
	if (!req_data) {
		if (coll)
			rbd_coll_end_req_index(rq, coll, coll_index,
					       -ENOMEM, len);
		return -ENOMEM;
	}

	if (coll) {
		req_data->coll = coll;
		req_data->coll_index = coll_index;
	}

	dout("rbd_do_request obj=%s ofs=%lld len=%lld\n", obj, len, ofs);

	down_read(&header->snap_rwsem);

	req = ceph_osdc_alloc_request(&dev->client->osdc, flags,
				      snapc,
				      ops,
				      false,
				      GFP_NOIO, pages, bio);
	if (!req) {
		up_read(&header->snap_rwsem);
		ret = -ENOMEM;
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

	if (linger_req) {
		ceph_osdc_set_request_linger(&dev->client->osdc, req);
		*linger_req = req;
	}

	ret = ceph_osdc_start_request(&dev->client->osdc, req, false);
	if (ret < 0)
		goto done_err;

	if (!rbd_cb) {
		ret = ceph_osdc_wait_request(&dev->client->osdc, req);
		if (ver)
			*ver = le64_to_cpu(req->r_reassert_version.version);
		dout("reassert_ver=%lld\n",
		     le64_to_cpu(req->r_reassert_version.version));
		ceph_osdc_put_request(req);
	}
	return ret;

done_err:
	bio_chain_put(req_data->bio);
	ceph_osdc_put_request(req);
done_pages:
	rbd_coll_end_req(req_data, ret, len);
	kfree(req_data);
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

	rbd_coll_end_req(req_data, rc, bytes);

	if (req_data->bio)
		bio_chain_put(req_data->bio);

	ceph_osdc_put_request(req);
	kfree(req_data);
}

static void rbd_simple_req_cb(struct ceph_osd_request *req, struct ceph_msg *msg)
{
	ceph_osdc_put_request(req);
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
			   char *buf,
			   struct ceph_osd_request **linger_req,
			   u64 *ver)
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
			  NULL, 0,
			  NULL,
			  linger_req, ver);
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
		     struct bio *bio,
		     struct rbd_req_coll *coll,
		     int coll_index)
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
			     coll, coll_index,
			     rbd_req_cb, 0, NULL);

	rbd_destroy_ops(ops);
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
			 struct bio *bio,
			 struct rbd_req_coll *coll,
			 int coll_index)
{
	return rbd_do_op(rq, rbd_dev, snapc, CEPH_NOSNAP,
			 CEPH_OSD_OP_WRITE,
			 CEPH_OSD_FLAG_WRITE | CEPH_OSD_FLAG_ONDISK,
			 2,
			 ofs, len, bio, coll, coll_index);
}

/*
 * Request async osd read
 */
static int rbd_req_read(struct request *rq,
			 struct rbd_device *rbd_dev,
			 u64 snapid,
			 u64 ofs, u64 len,
			 struct bio *bio,
			 struct rbd_req_coll *coll,
			 int coll_index)
{
	return rbd_do_op(rq, rbd_dev, NULL,
			 (snapid ? snapid : CEPH_NOSNAP),
			 CEPH_OSD_OP_READ,
			 CEPH_OSD_FLAG_READ,
			 2,
			 ofs, len, bio, coll, coll_index);
}

/*
 * Request sync osd read
 */
static int rbd_req_sync_read(struct rbd_device *dev,
			  struct ceph_snap_context *snapc,
			  u64 snapid,
			  const char *obj,
			  u64 ofs, u64 len,
			  char *buf,
			  u64 *ver)
{
	return rbd_req_sync_op(dev, NULL,
			       (snapid ? snapid : CEPH_NOSNAP),
			       CEPH_OSD_OP_READ,
			       CEPH_OSD_FLAG_READ,
			       NULL,
			       1, obj, ofs, len, buf, NULL, ver);
}

/*
 * Request sync osd watch
 */
static int rbd_req_sync_notify_ack(struct rbd_device *dev,
				   u64 ver,
				   u64 notify_id,
				   const char *obj)
{
	struct ceph_osd_req_op *ops;
	struct page **pages = NULL;
	int ret;

	ret = rbd_create_rw_ops(&ops, 1, CEPH_OSD_OP_NOTIFY_ACK, 0);
	if (ret < 0)
		return ret;

	ops[0].watch.ver = cpu_to_le64(dev->header.obj_version);
	ops[0].watch.cookie = notify_id;
	ops[0].watch.flag = 0;

	ret = rbd_do_request(NULL, dev, NULL, CEPH_NOSNAP,
			  obj, 0, 0, NULL,
			  pages, 0,
			  CEPH_OSD_FLAG_READ,
			  ops,
			  1,
			  NULL, 0,
			  rbd_simple_req_cb, 0, NULL);

	rbd_destroy_ops(ops);
	return ret;
}

static void rbd_watch_cb(u64 ver, u64 notify_id, u8 opcode, void *data)
{
	struct rbd_device *dev = (struct rbd_device *)data;
	int rc;

	if (!dev)
		return;

	dout("rbd_watch_cb %s notify_id=%lld opcode=%d\n", dev->obj_md_name,
		notify_id, (int)opcode);
	mutex_lock_nested(&ctl_mutex, SINGLE_DEPTH_NESTING);
	rc = __rbd_update_snaps(dev);
	mutex_unlock(&ctl_mutex);
	if (rc)
		pr_warning(DRV_NAME "%d got notification but failed to update"
			   " snaps: %d\n", dev->major, rc);

	rbd_req_sync_notify_ack(dev, ver, notify_id, dev->obj_md_name);
}

/*
 * Request sync osd watch
 */
static int rbd_req_sync_watch(struct rbd_device *dev,
			      const char *obj,
			      u64 ver)
{
	struct ceph_osd_req_op *ops;
	struct ceph_osd_client *osdc = &dev->client->osdc;

	int ret = rbd_create_rw_ops(&ops, 1, CEPH_OSD_OP_WATCH, 0);
	if (ret < 0)
		return ret;

	ret = ceph_osdc_create_event(osdc, rbd_watch_cb, 0,
				     (void *)dev, &dev->watch_event);
	if (ret < 0)
		goto fail;

	ops[0].watch.ver = cpu_to_le64(ver);
	ops[0].watch.cookie = cpu_to_le64(dev->watch_event->cookie);
	ops[0].watch.flag = 1;

	ret = rbd_req_sync_op(dev, NULL,
			      CEPH_NOSNAP,
			      0,
			      CEPH_OSD_FLAG_WRITE | CEPH_OSD_FLAG_ONDISK,
			      ops,
			      1, obj, 0, 0, NULL,
			      &dev->watch_request, NULL);

	if (ret < 0)
		goto fail_event;

	rbd_destroy_ops(ops);
	return 0;

fail_event:
	ceph_osdc_cancel_event(dev->watch_event);
	dev->watch_event = NULL;
fail:
	rbd_destroy_ops(ops);
	return ret;
}

/*
 * Request sync osd unwatch
 */
static int rbd_req_sync_unwatch(struct rbd_device *dev,
				const char *obj)
{
	struct ceph_osd_req_op *ops;

	int ret = rbd_create_rw_ops(&ops, 1, CEPH_OSD_OP_WATCH, 0);
	if (ret < 0)
		return ret;

	ops[0].watch.ver = 0;
	ops[0].watch.cookie = cpu_to_le64(dev->watch_event->cookie);
	ops[0].watch.flag = 0;

	ret = rbd_req_sync_op(dev, NULL,
			      CEPH_NOSNAP,
			      0,
			      CEPH_OSD_FLAG_WRITE | CEPH_OSD_FLAG_ONDISK,
			      ops,
			      1, obj, 0, 0, NULL, NULL, NULL);

	rbd_destroy_ops(ops);
	ceph_osdc_cancel_event(dev->watch_event);
	dev->watch_event = NULL;
	return ret;
}

struct rbd_notify_info {
	struct rbd_device *dev;
};

static void rbd_notify_cb(u64 ver, u64 notify_id, u8 opcode, void *data)
{
	struct rbd_device *dev = (struct rbd_device *)data;
	if (!dev)
		return;

	dout("rbd_notify_cb %s notify_id=%lld opcode=%d\n", dev->obj_md_name,
		notify_id, (int)opcode);
}

/*
 * Request sync osd notify
 */
static int rbd_req_sync_notify(struct rbd_device *dev,
		          const char *obj)
{
	struct ceph_osd_req_op *ops;
	struct ceph_osd_client *osdc = &dev->client->osdc;
	struct ceph_osd_event *event;
	struct rbd_notify_info info;
	int payload_len = sizeof(u32) + sizeof(u32);
	int ret;

	ret = rbd_create_rw_ops(&ops, 1, CEPH_OSD_OP_NOTIFY, payload_len);
	if (ret < 0)
		return ret;

	info.dev = dev;

	ret = ceph_osdc_create_event(osdc, rbd_notify_cb, 1,
				     (void *)&info, &event);
	if (ret < 0)
		goto fail;

	ops[0].watch.ver = 1;
	ops[0].watch.flag = 1;
	ops[0].watch.cookie = event->cookie;
	ops[0].watch.prot_ver = RADOS_NOTIFY_VER;
	ops[0].watch.timeout = 12;

	ret = rbd_req_sync_op(dev, NULL,
			       CEPH_NOSNAP,
			       0,
			       CEPH_OSD_FLAG_WRITE | CEPH_OSD_FLAG_ONDISK,
			       ops,
			       1, obj, 0, 0, NULL, NULL, NULL);
	if (ret < 0)
		goto fail_event;

	ret = ceph_osdc_wait_event(event, CEPH_OSD_TIMEOUT_DEFAULT);
	dout("ceph_osdc_wait_event returned %d\n", ret);
	rbd_destroy_ops(ops);
	return 0;

fail_event:
	ceph_osdc_cancel_event(event);
fail:
	rbd_destroy_ops(ops);
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
			     int len,
			     u64 *ver)
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
			       1, obj, 0, 0, NULL, NULL, ver);

	rbd_destroy_ops(ops);

	dout("cls_exec returned %d\n", ret);
	return ret;
}

static struct rbd_req_coll *rbd_alloc_coll(int num_reqs)
{
	struct rbd_req_coll *coll =
			kzalloc(sizeof(struct rbd_req_coll) +
			        sizeof(struct rbd_req_status) * num_reqs,
				GFP_ATOMIC);

	if (!coll)
		return NULL;
	coll->total = num_reqs;
	kref_init(&coll->kref);
	return coll;
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
		int num_segs, cur_seg = 0;
		struct rbd_req_coll *coll;

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

		num_segs = rbd_get_num_segments(&rbd_dev->header, ofs, size);
		coll = rbd_alloc_coll(num_segs);
		if (!coll) {
			spin_lock_irq(q->queue_lock);
			__blk_end_request_all(rq, -ENOMEM);
			goto next;
		}

		do {
			/* a bio clone to be passed down to OSD req */
			dout("rq->bio->bi_vcnt=%d\n", rq->bio->bi_vcnt);
			op_size = rbd_get_segment(&rbd_dev->header,
						  rbd_dev->header.block_name,
						  ofs, size,
						  NULL, NULL);
			kref_get(&coll->kref);
			bio = bio_chain_clone(&rq_bio, &next_bio, &bp,
					      op_size, GFP_ATOMIC);
			if (!bio) {
				rbd_coll_end_req_index(rq, coll, cur_seg,
						       -ENOMEM, op_size);
				goto next_seg;
			}


			/* init OSD command: write or read */
			if (do_write)
				rbd_req_write(rq, rbd_dev,
					      rbd_dev->header.snapc,
					      ofs,
					      op_size, bio,
					      coll, cur_seg);
			else
				rbd_req_read(rq, rbd_dev,
					     cur_snap_id(rbd_dev),
					     ofs,
					     op_size, bio,
					     coll, cur_seg);

next_seg:
			size -= op_size;
			ofs += op_size;

			cur_seg++;
			rq_bio = next_bio;
		} while (size > 0);
		kref_put(&coll->kref, rbd_coll_release);

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
	u64 ver;

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
				       (char *)dh, &ver);
		if (rc < 0)
			goto out_dh;

		rc = rbd_header_from_disk(header, dh, snap_count, GFP_KERNEL);
		if (rc < 0) {
			if (rc == -ENXIO) {
				pr_warning("unrecognized header format"
					   " for image %s", rbd_dev->obj);
			}
			goto out_dh;
		}

		if (snap_count != header->total_snaps) {
			snap_count = header->total_snaps;
			snap_names_len = header->snap_names_len;
			rbd_header_free(header);
			kfree(dh);
			continue;
		}
		break;
	}
	header->obj_version = ver;

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
	void *data, *p, *e;
	u64 ver;

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

	p = data;
	e = data + name_len + 16;

	ceph_encode_string_safe(&p, e, snap_name, name_len, bad);
	ceph_encode_64_safe(&p, e, new_snapid, bad);

	ret = rbd_req_sync_exec(dev, dev->obj_md_name, "rbd", "snap_add",
				data, p - data, &ver);

	kfree(data);

	if (ret < 0)
		return ret;

	dev->header.snapc->seq =  new_snapid;

	return 0;
bad:
	return -ERANGE;
}

static void __rbd_remove_all_snaps(struct rbd_device *rbd_dev)
{
	struct rbd_snap *snap;

	while (!list_empty(&rbd_dev->snaps)) {
		snap = list_first_entry(&rbd_dev->snaps, struct rbd_snap, node);
		__rbd_remove_snap_dev(rbd_dev, snap);
	}
}

/*
 * only read the first part of the ondisk header, without the snaps info
 */
static int __rbd_update_snaps(struct rbd_device *rbd_dev)
{
	int ret;
	struct rbd_image_header h;
	u64 snap_seq;
	int follow_seq = 0;

	ret = rbd_read_header(rbd_dev, &h);
	if (ret < 0)
		return ret;

	/* resized? */
	set_capacity(rbd_dev->disk, h.image_size / 512ULL);

	down_write(&rbd_dev->header.snap_rwsem);

	snap_seq = rbd_dev->header.snapc->seq;
	if (rbd_dev->header.total_snaps &&
	    rbd_dev->header.snapc->snaps[0] == snap_seq)
		/* pointing at the head, will need to follow that
		   if head moves */
		follow_seq = 1;

	kfree(rbd_dev->header.snapc);
	kfree(rbd_dev->header.snap_names);
	kfree(rbd_dev->header.snap_sizes);

	rbd_dev->header.total_snaps = h.total_snaps;
	rbd_dev->header.snapc = h.snapc;
	rbd_dev->header.snap_names = h.snap_names;
	rbd_dev->header.snap_names_len = h.snap_names_len;
	rbd_dev->header.snap_sizes = h.snap_sizes;
	if (follow_seq)
		rbd_dev->header.snapc->seq = rbd_dev->header.snapc->snaps[0];
	else
		rbd_dev->header.snapc->seq = snap_seq;

	ret = __rbd_init_snaps_header(rbd_dev);

	up_write(&rbd_dev->header.snap_rwsem);

	return ret;
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

	/* no need to lock here, as rbd_dev is not registered yet */
	rc = __rbd_init_snaps_header(rbd_dev);
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

	snprintf(disk->disk_name, sizeof(disk->disk_name), DRV_NAME "%d",
		 rbd_dev->id);
	disk->major = rbd_dev->major;
	disk->first_minor = 0;
	disk->fops = &rbd_bd_ops;
	disk->private_data = rbd_dev;

	/* init rq */
	rc = -ENOMEM;
	q = blk_init_queue(rbd_rq_fn, &rbd_dev->lock);
	if (!q)
		goto out_disk;

	/* set io sizes to object size */
	blk_queue_max_hw_sectors(q, rbd_obj_bytes(&rbd_dev->header) / 512ULL);
	blk_queue_max_segment_size(q, rbd_obj_bytes(&rbd_dev->header));
	blk_queue_io_min(q, rbd_obj_bytes(&rbd_dev->header));
	blk_queue_io_opt(q, rbd_obj_bytes(&rbd_dev->header));

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

/*
  sysfs
*/

static ssize_t rbd_size_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct rbd_device *rbd_dev = dev_to_rbd(dev);

	return sprintf(buf, "%llu\n", (unsigned long long)rbd_dev->header.image_size);
}

static ssize_t rbd_major_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct rbd_device *rbd_dev = dev_to_rbd(dev);

	return sprintf(buf, "%d\n", rbd_dev->major);
}

static ssize_t rbd_client_id_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct rbd_device *rbd_dev = dev_to_rbd(dev);

	return sprintf(buf, "client%lld\n", ceph_client_id(rbd_dev->client));
}

static ssize_t rbd_pool_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct rbd_device *rbd_dev = dev_to_rbd(dev);

	return sprintf(buf, "%s\n", rbd_dev->pool_name);
}

static ssize_t rbd_name_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct rbd_device *rbd_dev = dev_to_rbd(dev);

	return sprintf(buf, "%s\n", rbd_dev->obj);
}

static ssize_t rbd_snap_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	struct rbd_device *rbd_dev = dev_to_rbd(dev);

	return sprintf(buf, "%s\n", rbd_dev->snap_name);
}

static ssize_t rbd_image_refresh(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf,
				 size_t size)
{
	struct rbd_device *rbd_dev = dev_to_rbd(dev);
	int rc;
	int ret = size;

	mutex_lock_nested(&ctl_mutex, SINGLE_DEPTH_NESTING);

	rc = __rbd_update_snaps(rbd_dev);
	if (rc < 0)
		ret = rc;

	mutex_unlock(&ctl_mutex);
	return ret;
}

static DEVICE_ATTR(size, S_IRUGO, rbd_size_show, NULL);
static DEVICE_ATTR(major, S_IRUGO, rbd_major_show, NULL);
static DEVICE_ATTR(client_id, S_IRUGO, rbd_client_id_show, NULL);
static DEVICE_ATTR(pool, S_IRUGO, rbd_pool_show, NULL);
static DEVICE_ATTR(name, S_IRUGO, rbd_name_show, NULL);
static DEVICE_ATTR(refresh, S_IWUSR, NULL, rbd_image_refresh);
static DEVICE_ATTR(current_snap, S_IRUGO, rbd_snap_show, NULL);
static DEVICE_ATTR(create_snap, S_IWUSR, NULL, rbd_snap_add);

static struct attribute *rbd_attrs[] = {
	&dev_attr_size.attr,
	&dev_attr_major.attr,
	&dev_attr_client_id.attr,
	&dev_attr_pool.attr,
	&dev_attr_name.attr,
	&dev_attr_current_snap.attr,
	&dev_attr_refresh.attr,
	&dev_attr_create_snap.attr,
	NULL
};

static struct attribute_group rbd_attr_group = {
	.attrs = rbd_attrs,
};

static const struct attribute_group *rbd_attr_groups[] = {
	&rbd_attr_group,
	NULL
};

static void rbd_sysfs_dev_release(struct device *dev)
{
}

static struct device_type rbd_device_type = {
	.name		= "rbd",
	.groups		= rbd_attr_groups,
	.release	= rbd_sysfs_dev_release,
};


/*
  sysfs - snapshots
*/

static ssize_t rbd_snap_size_show(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	struct rbd_snap *snap = container_of(dev, struct rbd_snap, dev);

	return sprintf(buf, "%lld\n", (long long)snap->size);
}

static ssize_t rbd_snap_id_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct rbd_snap *snap = container_of(dev, struct rbd_snap, dev);

	return sprintf(buf, "%lld\n", (long long)snap->id);
}

static DEVICE_ATTR(snap_size, S_IRUGO, rbd_snap_size_show, NULL);
static DEVICE_ATTR(snap_id, S_IRUGO, rbd_snap_id_show, NULL);

static struct attribute *rbd_snap_attrs[] = {
	&dev_attr_snap_size.attr,
	&dev_attr_snap_id.attr,
	NULL,
};

static struct attribute_group rbd_snap_attr_group = {
	.attrs = rbd_snap_attrs,
};

static void rbd_snap_dev_release(struct device *dev)
{
	struct rbd_snap *snap = container_of(dev, struct rbd_snap, dev);
	kfree(snap->name);
	kfree(snap);
}

static const struct attribute_group *rbd_snap_attr_groups[] = {
	&rbd_snap_attr_group,
	NULL
};

static struct device_type rbd_snap_device_type = {
	.groups		= rbd_snap_attr_groups,
	.release	= rbd_snap_dev_release,
};

static void __rbd_remove_snap_dev(struct rbd_device *rbd_dev,
				  struct rbd_snap *snap)
{
	list_del(&snap->node);
	device_unregister(&snap->dev);
}

static int rbd_register_snap_dev(struct rbd_device *rbd_dev,
				  struct rbd_snap *snap,
				  struct device *parent)
{
	struct device *dev = &snap->dev;
	int ret;

	dev->type = &rbd_snap_device_type;
	dev->parent = parent;
	dev->release = rbd_snap_dev_release;
	dev_set_name(dev, "snap_%s", snap->name);
	ret = device_register(dev);

	return ret;
}

static int __rbd_add_snap_dev(struct rbd_device *rbd_dev,
			      int i, const char *name,
			      struct rbd_snap **snapp)
{
	int ret;
	struct rbd_snap *snap = kzalloc(sizeof(*snap), GFP_KERNEL);
	if (!snap)
		return -ENOMEM;
	snap->name = kstrdup(name, GFP_KERNEL);
	snap->size = rbd_dev->header.snap_sizes[i];
	snap->id = rbd_dev->header.snapc->snaps[i];
	if (device_is_registered(&rbd_dev->dev)) {
		ret = rbd_register_snap_dev(rbd_dev, snap,
					     &rbd_dev->dev);
		if (ret < 0)
			goto err;
	}
	*snapp = snap;
	return 0;
err:
	kfree(snap->name);
	kfree(snap);
	return ret;
}

/*
 * search for the previous snap in a null delimited string list
 */
const char *rbd_prev_snap_name(const char *name, const char *start)
{
	if (name < start + 2)
		return NULL;

	name -= 2;
	while (*name) {
		if (name == start)
			return start;
		name--;
	}
	return name + 1;
}

/*
 * compare the old list of snapshots that we have to what's in the header
 * and update it accordingly. Note that the header holds the snapshots
 * in a reverse order (from newest to oldest) and we need to go from
 * older to new so that we don't get a duplicate snap name when
 * doing the process (e.g., removed snapshot and recreated a new
 * one with the same name.
 */
static int __rbd_init_snaps_header(struct rbd_device *rbd_dev)
{
	const char *name, *first_name;
	int i = rbd_dev->header.total_snaps;
	struct rbd_snap *snap, *old_snap = NULL;
	int ret;
	struct list_head *p, *n;

	first_name = rbd_dev->header.snap_names;
	name = first_name + rbd_dev->header.snap_names_len;

	list_for_each_prev_safe(p, n, &rbd_dev->snaps) {
		u64 cur_id;

		old_snap = list_entry(p, struct rbd_snap, node);

		if (i)
			cur_id = rbd_dev->header.snapc->snaps[i - 1];

		if (!i || old_snap->id < cur_id) {
			/* old_snap->id was skipped, thus was removed */
			__rbd_remove_snap_dev(rbd_dev, old_snap);
			continue;
		}
		if (old_snap->id == cur_id) {
			/* we have this snapshot already */
			i--;
			name = rbd_prev_snap_name(name, first_name);
			continue;
		}
		for (; i > 0;
		     i--, name = rbd_prev_snap_name(name, first_name)) {
			if (!name) {
				WARN_ON(1);
				return -EINVAL;
			}
			cur_id = rbd_dev->header.snapc->snaps[i];
			/* snapshot removal? handle it above */
			if (cur_id >= old_snap->id)
				break;
			/* a new snapshot */
			ret = __rbd_add_snap_dev(rbd_dev, i - 1, name, &snap);
			if (ret < 0)
				return ret;

			/* note that we add it backward so using n and not p */
			list_add(&snap->node, n);
			p = &snap->node;
		}
	}
	/* we're done going over the old snap list, just add what's left */
	for (; i > 0; i--) {
		name = rbd_prev_snap_name(name, first_name);
		if (!name) {
			WARN_ON(1);
			return -EINVAL;
		}
		ret = __rbd_add_snap_dev(rbd_dev, i - 1, name, &snap);
		if (ret < 0)
			return ret;
		list_add(&snap->node, &rbd_dev->snaps);
	}

	return 0;
}


static void rbd_root_dev_release(struct device *dev)
{
}

static struct device rbd_root_dev = {
	.init_name =    "rbd",
	.release =      rbd_root_dev_release,
};

static int rbd_bus_add_dev(struct rbd_device *rbd_dev)
{
	int ret = -ENOMEM;
	struct device *dev;
	struct rbd_snap *snap;

	mutex_lock_nested(&ctl_mutex, SINGLE_DEPTH_NESTING);
	dev = &rbd_dev->dev;

	dev->bus = &rbd_bus_type;
	dev->type = &rbd_device_type;
	dev->parent = &rbd_root_dev;
	dev->release = rbd_dev_release;
	dev_set_name(dev, "%d", rbd_dev->id);
	ret = device_register(dev);
	if (ret < 0)
		goto done_free;

	list_for_each_entry(snap, &rbd_dev->snaps, node) {
		ret = rbd_register_snap_dev(rbd_dev, snap,
					     &rbd_dev->dev);
		if (ret < 0)
			break;
	}

	mutex_unlock(&ctl_mutex);
	return 0;
done_free:
	mutex_unlock(&ctl_mutex);
	return ret;
}

static void rbd_bus_del_dev(struct rbd_device *rbd_dev)
{
	device_unregister(&rbd_dev->dev);
}

static int rbd_init_watch_dev(struct rbd_device *rbd_dev)
{
	int ret, rc;

	do {
		ret = rbd_req_sync_watch(rbd_dev, rbd_dev->obj_md_name,
					 rbd_dev->header.obj_version);
		if (ret == -ERANGE) {
			mutex_lock_nested(&ctl_mutex, SINGLE_DEPTH_NESTING);
			rc = __rbd_update_snaps(rbd_dev);
			mutex_unlock(&ctl_mutex);
			if (rc < 0)
				return rc;
		}
	} while (ret == -ERANGE);

	return ret;
}

static ssize_t rbd_add(struct bus_type *bus,
		       const char *buf,
		       size_t count)
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
	INIT_LIST_HEAD(&rbd_dev->snaps);

	init_rwsem(&rbd_dev->header.snap_rwsem);

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

	rc = rbd_bus_add_dev(rbd_dev);
	if (rc)
		goto err_out_blkdev;

	/* set up and announce blkdev mapping */
	rc = rbd_init_disk(rbd_dev);
	if (rc)
		goto err_out_bus;

	rc = rbd_init_watch_dev(rbd_dev);
	if (rc)
		goto err_out_bus;

	return count;

err_out_bus:
	mutex_lock_nested(&ctl_mutex, SINGLE_DEPTH_NESTING);
	list_del_init(&rbd_dev->node);
	mutex_unlock(&ctl_mutex);

	/* this will also clean up rest of rbd_dev stuff */

	rbd_bus_del_dev(rbd_dev);
	kfree(options);
	kfree(mon_dev_name);
	return rc;

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

static void rbd_dev_release(struct device *dev)
{
	struct rbd_device *rbd_dev =
			container_of(dev, struct rbd_device, dev);

	if (rbd_dev->watch_request)
		ceph_osdc_unregister_linger_request(&rbd_dev->client->osdc,
						    rbd_dev->watch_request);
	if (rbd_dev->watch_event)
		rbd_req_sync_unwatch(rbd_dev, rbd_dev->obj_md_name);

	rbd_put_client(rbd_dev);

	/* clean up and free blkdev */
	rbd_free_disk(rbd_dev);
	unregister_blkdev(rbd_dev->major, rbd_dev->name);
	kfree(rbd_dev);

	/* release module ref */
	module_put(THIS_MODULE);
}

static ssize_t rbd_remove(struct bus_type *bus,
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

	list_del_init(&rbd_dev->node);

	__rbd_remove_all_snaps(rbd_dev);
	rbd_bus_del_dev(rbd_dev);

done:
	mutex_unlock(&ctl_mutex);
	return ret;
}

static ssize_t rbd_snap_add(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf,
			    size_t count)
{
	struct rbd_device *rbd_dev = dev_to_rbd(dev);
	int ret;
	char *name = kmalloc(count + 1, GFP_KERNEL);
	if (!name)
		return -ENOMEM;

	snprintf(name, count, "%s", buf);

	mutex_lock_nested(&ctl_mutex, SINGLE_DEPTH_NESTING);

	ret = rbd_header_add_snap(rbd_dev,
				  name, GFP_KERNEL);
	if (ret < 0)
		goto err_unlock;

	ret = __rbd_update_snaps(rbd_dev);
	if (ret < 0)
		goto err_unlock;

	/* shouldn't hold ctl_mutex when notifying.. notify might
	   trigger a watch callback that would need to get that mutex */
	mutex_unlock(&ctl_mutex);

	/* make a best effort, don't error if failed */
	rbd_req_sync_notify(rbd_dev, rbd_dev->obj_md_name);

	ret = count;
	kfree(name);
	return ret;

err_unlock:
	mutex_unlock(&ctl_mutex);
	kfree(name);
	return ret;
}

static struct bus_attribute rbd_bus_attrs[] = {
	__ATTR(add, S_IWUSR, NULL, rbd_add),
	__ATTR(remove, S_IWUSR, NULL, rbd_remove),
	__ATTR_NULL
};

/*
 * create control files in sysfs
 * /sys/bus/rbd/...
 */
static int rbd_sysfs_init(void)
{
	int ret;

	rbd_bus_type.bus_attrs = rbd_bus_attrs;

	ret = bus_register(&rbd_bus_type);
	 if (ret < 0)
		return ret;

	ret = device_register(&rbd_root_dev);

	return ret;
}

static void rbd_sysfs_cleanup(void)
{
	device_unregister(&rbd_root_dev);
	bus_unregister(&rbd_bus_type);
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
