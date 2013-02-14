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

#define RBD_DEBUG	/* Activate rbd_assert() calls */

/*
 * The basic unit of block I/O is a sector.  It is interpreted in a
 * number of contexts in Linux (blk, bio, genhd), but the default is
 * universally 512 bytes.  These symbols are just slightly more
 * meaningful than the bare numbers they represent.
 */
#define	SECTOR_SHIFT	9
#define	SECTOR_SIZE	(1ULL << SECTOR_SHIFT)

/* It might be useful to have this defined elsewhere too */

#define	U64_MAX	((u64) (~0ULL))

#define RBD_DRV_NAME "rbd"
#define RBD_DRV_NAME_LONG "rbd (rados block device)"

#define RBD_MINORS_PER_MAJOR	256		/* max minors per blkdev */

#define RBD_SNAP_DEV_NAME_PREFIX	"snap_"
#define RBD_MAX_SNAP_NAME_LEN	\
			(NAME_MAX - (sizeof (RBD_SNAP_DEV_NAME_PREFIX) - 1))

#define RBD_MAX_SNAP_COUNT	510	/* allows max snapc to fit in 4KB */
#define RBD_MAX_OPT_LEN		1024

#define RBD_SNAP_HEAD_NAME	"-"

/* This allows a single page to hold an image name sent by OSD */
#define RBD_IMAGE_NAME_LEN_MAX	(PAGE_SIZE - sizeof (__le32) - 1)
#define RBD_IMAGE_ID_LEN_MAX	64

#define RBD_OBJ_PREFIX_LEN_MAX	64

/* Feature bits */

#define RBD_FEATURE_LAYERING      1

/* Features supported by this (client software) implementation. */

#define RBD_FEATURES_ALL          (0)

/*
 * An RBD device name will be "rbd#", where the "rbd" comes from
 * RBD_DRV_NAME above, and # is a unique integer identifier.
 * MAX_INT_FORMAT_WIDTH is used in ensuring DEV_NAME_LEN is big
 * enough to hold all possible device names.
 */
#define DEV_NAME_LEN		32
#define MAX_INT_FORMAT_WIDTH	((5 * sizeof (int)) / 2 + 1)

#define RBD_READ_ONLY_DEFAULT		false

/*
 * block device image metadata (in-memory version)
 */
struct rbd_image_header {
	/* These four fields never change for a given rbd image */
	char *object_prefix;
	u64 features;
	__u8 obj_order;
	__u8 crypt_type;
	__u8 comp_type;

	/* The remaining fields need to be updated occasionally */
	u64 image_size;
	struct ceph_snap_context *snapc;
	char *snap_names;
	u64 *snap_sizes;

	u64 obj_version;
};

/*
 * An rbd image specification.
 *
 * The tuple (pool_id, image_id, snap_id) is sufficient to uniquely
 * identify an image.
 */
struct rbd_spec {
	u64		pool_id;
	char		*pool_name;

	char		*image_id;
	size_t		image_id_len;
	char		*image_name;
	size_t		image_name_len;

	u64		snap_id;
	char		*snap_name;

	struct kref	kref;
};

struct rbd_options {
	bool	read_only;
};

/*
 * an instance of the client.  multiple devices may share an rbd client.
 */
struct rbd_client {
	struct ceph_client	*client;
	struct kref		kref;
	struct list_head	node;
};

/*
 * a request completion status
 */
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

struct rbd_snap {
	struct	device		dev;
	const char		*name;
	u64			size;
	struct list_head	node;
	u64			id;
	u64			features;
};

struct rbd_mapping {
	u64                     size;
	u64                     features;
	bool			read_only;
};

/*
 * a single device
 */
struct rbd_device {
	int			dev_id;		/* blkdev unique id */

	int			major;		/* blkdev assigned major */
	struct gendisk		*disk;		/* blkdev's gendisk and rq */

	u32			image_format;	/* Either 1 or 2 */
	struct rbd_client	*rbd_client;

	char			name[DEV_NAME_LEN]; /* blkdev name, e.g. rbd3 */

	spinlock_t		lock;		/* queue lock */

	struct rbd_image_header	header;
	bool                    exists;
	struct rbd_spec		*spec;

	char			*header_name;

	struct ceph_osd_event   *watch_event;
	struct ceph_osd_request *watch_request;

	struct rbd_spec		*parent_spec;
	u64			parent_overlap;

	/* protects updating the header */
	struct rw_semaphore     header_rwsem;

	struct rbd_mapping	mapping;

	struct list_head	node;

	/* list of snapshots */
	struct list_head	snaps;

	/* sysfs related */
	struct device		dev;
	unsigned long		open_count;
};

static DEFINE_MUTEX(ctl_mutex);	  /* Serialize open/close/setup/teardown */

static LIST_HEAD(rbd_dev_list);    /* devices */
static DEFINE_SPINLOCK(rbd_dev_list_lock);

static LIST_HEAD(rbd_client_list);		/* clients */
static DEFINE_SPINLOCK(rbd_client_list_lock);

static int rbd_dev_snaps_update(struct rbd_device *rbd_dev);
static int rbd_dev_snaps_register(struct rbd_device *rbd_dev);

static void rbd_dev_release(struct device *dev);
static void rbd_remove_snap_dev(struct rbd_snap *snap);

static ssize_t rbd_add(struct bus_type *bus, const char *buf,
		       size_t count);
static ssize_t rbd_remove(struct bus_type *bus, const char *buf,
			  size_t count);

static struct bus_attribute rbd_bus_attrs[] = {
	__ATTR(add, S_IWUSR, NULL, rbd_add),
	__ATTR(remove, S_IWUSR, NULL, rbd_remove),
	__ATTR_NULL
};

static struct bus_type rbd_bus_type = {
	.name		= "rbd",
	.bus_attrs	= rbd_bus_attrs,
};

static void rbd_root_dev_release(struct device *dev)
{
}

static struct device rbd_root_dev = {
	.init_name =    "rbd",
	.release =      rbd_root_dev_release,
};

#ifdef RBD_DEBUG
#define rbd_assert(expr)						\
		if (unlikely(!(expr))) {				\
			printk(KERN_ERR "\nAssertion failure in %s() "	\
						"at line %d:\n\n"	\
					"\trbd_assert(%s);\n\n",	\
					__func__, __LINE__, #expr);	\
			BUG();						\
		}
#else /* !RBD_DEBUG */
#  define rbd_assert(expr)	((void) 0)
#endif /* !RBD_DEBUG */

static int rbd_dev_refresh(struct rbd_device *rbd_dev, u64 *hver);
static int rbd_dev_v2_refresh(struct rbd_device *rbd_dev, u64 *hver);

static int rbd_open(struct block_device *bdev, fmode_t mode)
{
	struct rbd_device *rbd_dev = bdev->bd_disk->private_data;

	if ((mode & FMODE_WRITE) && rbd_dev->mapping.read_only)
		return -EROFS;

	mutex_lock_nested(&ctl_mutex, SINGLE_DEPTH_NESTING);
	(void) get_device(&rbd_dev->dev);
	set_device_ro(bdev, rbd_dev->mapping.read_only);
	rbd_dev->open_count++;
	mutex_unlock(&ctl_mutex);

	return 0;
}

static int rbd_release(struct gendisk *disk, fmode_t mode)
{
	struct rbd_device *rbd_dev = disk->private_data;

	mutex_lock_nested(&ctl_mutex, SINGLE_DEPTH_NESTING);
	rbd_assert(rbd_dev->open_count > 0);
	rbd_dev->open_count--;
	put_device(&rbd_dev->dev);
	mutex_unlock(&ctl_mutex);

	return 0;
}

static const struct block_device_operations rbd_bd_ops = {
	.owner			= THIS_MODULE,
	.open			= rbd_open,
	.release		= rbd_release,
};

/*
 * Initialize an rbd client instance.
 * We own *ceph_opts.
 */
static struct rbd_client *rbd_client_create(struct ceph_options *ceph_opts)
{
	struct rbd_client *rbdc;
	int ret = -ENOMEM;

	dout("rbd_client_create\n");
	rbdc = kmalloc(sizeof(struct rbd_client), GFP_KERNEL);
	if (!rbdc)
		goto out_opt;

	kref_init(&rbdc->kref);
	INIT_LIST_HEAD(&rbdc->node);

	mutex_lock_nested(&ctl_mutex, SINGLE_DEPTH_NESTING);

	rbdc->client = ceph_create_client(ceph_opts, rbdc, 0, 0);
	if (IS_ERR(rbdc->client))
		goto out_mutex;
	ceph_opts = NULL; /* Now rbdc->client is responsible for ceph_opts */

	ret = ceph_open_session(rbdc->client);
	if (ret < 0)
		goto out_err;

	spin_lock(&rbd_client_list_lock);
	list_add_tail(&rbdc->node, &rbd_client_list);
	spin_unlock(&rbd_client_list_lock);

	mutex_unlock(&ctl_mutex);

	dout("rbd_client_create created %p\n", rbdc);
	return rbdc;

out_err:
	ceph_destroy_client(rbdc->client);
out_mutex:
	mutex_unlock(&ctl_mutex);
	kfree(rbdc);
out_opt:
	if (ceph_opts)
		ceph_destroy_options(ceph_opts);
	return ERR_PTR(ret);
}

/*
 * Find a ceph client with specific addr and configuration.  If
 * found, bump its reference count.
 */
static struct rbd_client *rbd_client_find(struct ceph_options *ceph_opts)
{
	struct rbd_client *client_node;
	bool found = false;

	if (ceph_opts->flags & CEPH_OPT_NOSHARE)
		return NULL;

	spin_lock(&rbd_client_list_lock);
	list_for_each_entry(client_node, &rbd_client_list, node) {
		if (!ceph_compare_options(ceph_opts, client_node->client)) {
			kref_get(&client_node->kref);
			found = true;
			break;
		}
	}
	spin_unlock(&rbd_client_list_lock);

	return found ? client_node : NULL;
}

/*
 * mount options
 */
enum {
	Opt_last_int,
	/* int args above */
	Opt_last_string,
	/* string args above */
	Opt_read_only,
	Opt_read_write,
	/* Boolean args above */
	Opt_last_bool,
};

static match_table_t rbd_opts_tokens = {
	/* int args above */
	/* string args above */
	{Opt_read_only, "read_only"},
	{Opt_read_only, "ro"},		/* Alternate spelling */
	{Opt_read_write, "read_write"},
	{Opt_read_write, "rw"},		/* Alternate spelling */
	/* Boolean args above */
	{-1, NULL}
};

static int parse_rbd_opts_token(char *c, void *private)
{
	struct rbd_options *rbd_opts = private;
	substring_t argstr[MAX_OPT_ARGS];
	int token, intval, ret;

	token = match_token(c, rbd_opts_tokens, argstr);
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
	} else if (token > Opt_last_string && token < Opt_last_bool) {
		dout("got Boolean token %d\n", token);
	} else {
		dout("got token %d\n", token);
	}

	switch (token) {
	case Opt_read_only:
		rbd_opts->read_only = true;
		break;
	case Opt_read_write:
		rbd_opts->read_only = false;
		break;
	default:
		rbd_assert(false);
		break;
	}
	return 0;
}

/*
 * Get a ceph client with specific addr and configuration, if one does
 * not exist create it.
 */
static struct rbd_client *rbd_get_client(struct ceph_options *ceph_opts)
{
	struct rbd_client *rbdc;

	rbdc = rbd_client_find(ceph_opts);
	if (rbdc)	/* using an existing client */
		ceph_destroy_options(ceph_opts);
	else
		rbdc = rbd_client_create(ceph_opts);

	return rbdc;
}

/*
 * Destroy ceph client
 *
 * Caller must hold rbd_client_list_lock.
 */
static void rbd_client_release(struct kref *kref)
{
	struct rbd_client *rbdc = container_of(kref, struct rbd_client, kref);

	dout("rbd_release_client %p\n", rbdc);
	spin_lock(&rbd_client_list_lock);
	list_del(&rbdc->node);
	spin_unlock(&rbd_client_list_lock);

	ceph_destroy_client(rbdc->client);
	kfree(rbdc);
}

/*
 * Drop reference to ceph client node. If it's not referenced anymore, release
 * it.
 */
static void rbd_put_client(struct rbd_client *rbdc)
{
	if (rbdc)
		kref_put(&rbdc->kref, rbd_client_release);
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

static bool rbd_image_format_valid(u32 image_format)
{
	return image_format == 1 || image_format == 2;
}

static bool rbd_dev_ondisk_valid(struct rbd_image_header_ondisk *ondisk)
{
	size_t size;
	u32 snap_count;

	/* The header has to start with the magic rbd header text */
	if (memcmp(&ondisk->text, RBD_HEADER_TEXT, sizeof (RBD_HEADER_TEXT)))
		return false;

	/* The bio layer requires at least sector-sized I/O */

	if (ondisk->options.order < SECTOR_SHIFT)
		return false;

	/* If we use u64 in a few spots we may be able to loosen this */

	if (ondisk->options.order > 8 * sizeof (int) - 1)
		return false;

	/*
	 * The size of a snapshot header has to fit in a size_t, and
	 * that limits the number of snapshots.
	 */
	snap_count = le32_to_cpu(ondisk->snap_count);
	size = SIZE_MAX - sizeof (struct ceph_snap_context);
	if (snap_count > size / sizeof (__le64))
		return false;

	/*
	 * Not only that, but the size of the entire the snapshot
	 * header must also be representable in a size_t.
	 */
	size -= snap_count * sizeof (__le64);
	if ((u64) size < le64_to_cpu(ondisk->snap_names_len))
		return false;

	return true;
}

/*
 * Create a new header structure, translate header format from the on-disk
 * header.
 */
static int rbd_header_from_disk(struct rbd_image_header *header,
				 struct rbd_image_header_ondisk *ondisk)
{
	u32 snap_count;
	size_t len;
	size_t size;
	u32 i;

	memset(header, 0, sizeof (*header));

	snap_count = le32_to_cpu(ondisk->snap_count);

	len = strnlen(ondisk->object_prefix, sizeof (ondisk->object_prefix));
	header->object_prefix = kmalloc(len + 1, GFP_KERNEL);
	if (!header->object_prefix)
		return -ENOMEM;
	memcpy(header->object_prefix, ondisk->object_prefix, len);
	header->object_prefix[len] = '\0';

	if (snap_count) {
		u64 snap_names_len = le64_to_cpu(ondisk->snap_names_len);

		/* Save a copy of the snapshot names */

		if (snap_names_len > (u64) SIZE_MAX)
			return -EIO;
		header->snap_names = kmalloc(snap_names_len, GFP_KERNEL);
		if (!header->snap_names)
			goto out_err;
		/*
		 * Note that rbd_dev_v1_header_read() guarantees
		 * the ondisk buffer we're working with has
		 * snap_names_len bytes beyond the end of the
		 * snapshot id array, this memcpy() is safe.
		 */
		memcpy(header->snap_names, &ondisk->snaps[snap_count],
			snap_names_len);

		/* Record each snapshot's size */

		size = snap_count * sizeof (*header->snap_sizes);
		header->snap_sizes = kmalloc(size, GFP_KERNEL);
		if (!header->snap_sizes)
			goto out_err;
		for (i = 0; i < snap_count; i++)
			header->snap_sizes[i] =
				le64_to_cpu(ondisk->snaps[i].image_size);
	} else {
		WARN_ON(ondisk->snap_names_len);
		header->snap_names = NULL;
		header->snap_sizes = NULL;
	}

	header->features = 0;	/* No features support in v1 images */
	header->obj_order = ondisk->options.order;
	header->crypt_type = ondisk->options.crypt_type;
	header->comp_type = ondisk->options.comp_type;

	/* Allocate and fill in the snapshot context */

	header->image_size = le64_to_cpu(ondisk->image_size);
	size = sizeof (struct ceph_snap_context);
	size += snap_count * sizeof (header->snapc->snaps[0]);
	header->snapc = kzalloc(size, GFP_KERNEL);
	if (!header->snapc)
		goto out_err;

	atomic_set(&header->snapc->nref, 1);
	header->snapc->seq = le64_to_cpu(ondisk->snap_seq);
	header->snapc->num_snaps = snap_count;
	for (i = 0; i < snap_count; i++)
		header->snapc->snaps[i] =
			le64_to_cpu(ondisk->snaps[i].id);

	return 0;

out_err:
	kfree(header->snap_sizes);
	header->snap_sizes = NULL;
	kfree(header->snap_names);
	header->snap_names = NULL;
	kfree(header->object_prefix);
	header->object_prefix = NULL;

	return -ENOMEM;
}

static const char *rbd_snap_name(struct rbd_device *rbd_dev, u64 snap_id)
{
	struct rbd_snap *snap;

	if (snap_id == CEPH_NOSNAP)
		return RBD_SNAP_HEAD_NAME;

	list_for_each_entry(snap, &rbd_dev->snaps, node)
		if (snap_id == snap->id)
			return snap->name;

	return NULL;
}

static int snap_by_name(struct rbd_device *rbd_dev, const char *snap_name)
{

	struct rbd_snap *snap;

	list_for_each_entry(snap, &rbd_dev->snaps, node) {
		if (!strcmp(snap_name, snap->name)) {
			rbd_dev->spec->snap_id = snap->id;
			rbd_dev->mapping.size = snap->size;
			rbd_dev->mapping.features = snap->features;

			return 0;
		}
	}

	return -ENOENT;
}

static int rbd_dev_set_mapping(struct rbd_device *rbd_dev)
{
	int ret;

	if (!memcmp(rbd_dev->spec->snap_name, RBD_SNAP_HEAD_NAME,
		    sizeof (RBD_SNAP_HEAD_NAME))) {
		rbd_dev->spec->snap_id = CEPH_NOSNAP;
		rbd_dev->mapping.size = rbd_dev->header.image_size;
		rbd_dev->mapping.features = rbd_dev->header.features;
		ret = 0;
	} else {
		ret = snap_by_name(rbd_dev, rbd_dev->spec->snap_name);
		if (ret < 0)
			goto done;
		rbd_dev->mapping.read_only = true;
	}
	rbd_dev->exists = true;
done:
	return ret;
}

static void rbd_header_free(struct rbd_image_header *header)
{
	kfree(header->object_prefix);
	header->object_prefix = NULL;
	kfree(header->snap_sizes);
	header->snap_sizes = NULL;
	kfree(header->snap_names);
	header->snap_names = NULL;
	ceph_put_snap_context(header->snapc);
	header->snapc = NULL;
}

static char *rbd_segment_name(struct rbd_device *rbd_dev, u64 offset)
{
	char *name;
	u64 segment;
	int ret;

	name = kmalloc(MAX_OBJ_NAME_SIZE + 1, GFP_NOIO);
	if (!name)
		return NULL;
	segment = offset >> rbd_dev->header.obj_order;
	ret = snprintf(name, MAX_OBJ_NAME_SIZE + 1, "%s.%012llx",
			rbd_dev->header.object_prefix, segment);
	if (ret < 0 || ret > MAX_OBJ_NAME_SIZE) {
		pr_err("error formatting segment name for #%llu (%d)\n",
			segment, ret);
		kfree(name);
		name = NULL;
	}

	return name;
}

static u64 rbd_segment_offset(struct rbd_device *rbd_dev, u64 offset)
{
	u64 segment_size = (u64) 1 << rbd_dev->header.obj_order;

	return offset & (segment_size - 1);
}

static u64 rbd_segment_length(struct rbd_device *rbd_dev,
				u64 offset, u64 length)
{
	u64 segment_size = (u64) 1 << rbd_dev->header.obj_order;

	offset &= segment_size - 1;

	rbd_assert(length <= U64_MAX - offset);
	if (offset + length > segment_size)
		length = segment_size - offset;

	return length;
}

static int rbd_get_num_segments(struct rbd_image_header *header,
				u64 ofs, u64 len)
{
	u64 start_seg;
	u64 end_seg;

	if (!len)
		return 0;
	if (len - 1 > U64_MAX - ofs)
		return -ERANGE;

	start_seg = ofs >> header->obj_order;
	end_seg = (ofs + len - 1) >> header->obj_order;

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
 * Clone a portion of a bio, starting at the given byte offset
 * and continuing for the number of bytes indicated.
 */
static struct bio *bio_clone_range(struct bio *bio_src,
					unsigned int offset,
					unsigned int len,
					gfp_t gfpmask)
{
	struct bio_vec *bv;
	unsigned int resid;
	unsigned short idx;
	unsigned int voff;
	unsigned short end_idx;
	unsigned short vcnt;
	struct bio *bio;

	/* Handle the easy case for the caller */

	if (!offset && len == bio_src->bi_size)
		return bio_clone(bio_src, gfpmask);

	if (WARN_ON_ONCE(!len))
		return NULL;
	if (WARN_ON_ONCE(len > bio_src->bi_size))
		return NULL;
	if (WARN_ON_ONCE(offset > bio_src->bi_size - len))
		return NULL;

	/* Find first affected segment... */

	resid = offset;
	__bio_for_each_segment(bv, bio_src, idx, 0) {
		if (resid < bv->bv_len)
			break;
		resid -= bv->bv_len;
	}
	voff = resid;

	/* ...and the last affected segment */

	resid += len;
	__bio_for_each_segment(bv, bio_src, end_idx, idx) {
		if (resid <= bv->bv_len)
			break;
		resid -= bv->bv_len;
	}
	vcnt = end_idx - idx + 1;

	/* Build the clone */

	bio = bio_alloc(gfpmask, (unsigned int) vcnt);
	if (!bio)
		return NULL;	/* ENOMEM */

	bio->bi_bdev = bio_src->bi_bdev;
	bio->bi_sector = bio_src->bi_sector + (offset >> SECTOR_SHIFT);
	bio->bi_rw = bio_src->bi_rw;
	bio->bi_flags |= 1 << BIO_CLONED;

	/*
	 * Copy over our part of the bio_vec, then update the first
	 * and last (or only) entries.
	 */
	memcpy(&bio->bi_io_vec[0], &bio_src->bi_io_vec[idx],
			vcnt * sizeof (struct bio_vec));
	bio->bi_io_vec[0].bv_offset += voff;
	if (vcnt > 1) {
		bio->bi_io_vec[0].bv_len -= voff;
		bio->bi_io_vec[vcnt - 1].bv_len = resid;
	} else {
		bio->bi_io_vec[0].bv_len = len;
	}

	bio->bi_vcnt = vcnt;
	bio->bi_size = len;
	bio->bi_idx = 0;

	return bio;
}

/*
 * Clone a portion of a bio chain, starting at the given byte offset
 * into the first bio in the source chain and continuing for the
 * number of bytes indicated.  The result is another bio chain of
 * exactly the given length, or a null pointer on error.
 *
 * The bio_src and offset parameters are both in-out.  On entry they
 * refer to the first source bio and the offset into that bio where
 * the start of data to be cloned is located.
 *
 * On return, bio_src is updated to refer to the bio in the source
 * chain that contains first un-cloned byte, and *offset will
 * contain the offset of that byte within that bio.
 */
static struct bio *bio_chain_clone_range(struct bio **bio_src,
					unsigned int *offset,
					unsigned int len,
					gfp_t gfpmask)
{
	struct bio *bi = *bio_src;
	unsigned int off = *offset;
	struct bio *chain = NULL;
	struct bio **end;

	/* Build up a chain of clone bios up to the limit */

	if (!bi || off >= bi->bi_size || !len)
		return NULL;		/* Nothing to clone */

	end = &chain;
	while (len) {
		unsigned int bi_size;
		struct bio *bio;

		if (!bi)
			goto out_err;	/* EINVAL; ran out of bio's */
		bi_size = min_t(unsigned int, bi->bi_size - off, len);
		bio = bio_clone_range(bi, off, bi_size, gfpmask);
		if (!bio)
			goto out_err;	/* ENOMEM */

		*end = bio;
		end = &bio->bi_next;

		off += bi_size;
		if (off == bi->bi_size) {
			bi = bi->bi_next;
			off = 0;
		}
		len -= bi_size;
	}
	*bio_src = bi;
	*offset = off;

	return chain;
out_err:
	bio_chain_put(chain);

	return NULL;
}

/*
 * helpers for osd request op vectors.
 */
static struct ceph_osd_req_op *rbd_create_rw_ops(int num_ops,
					int opcode, u32 payload_len)
{
	struct ceph_osd_req_op *ops;

	ops = kzalloc(sizeof (*ops) * (num_ops + 1), GFP_NOIO);
	if (!ops)
		return NULL;

	ops[0].op = opcode;

	/*
	 * op extent offset and length will be set later on
	 * in calc_raw_layout()
	 */
	ops[0].payload_len = payload_len;

	return ops;
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

	dout("rbd_coll_end_req_index %p index %d ret %d len %llu\n",
	     coll, index, ret, (unsigned long long) len);

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
			  struct rbd_device *rbd_dev,
			  struct ceph_snap_context *snapc,
			  u64 snapid,
			  const char *object_name, u64 ofs, u64 len,
			  struct bio *bio,
			  struct page **pages,
			  int num_pages,
			  int flags,
			  struct ceph_osd_req_op *ops,
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
	struct ceph_osd_client *osdc;

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

	dout("rbd_do_request object_name=%s ofs=%llu len=%llu coll=%p[%d]\n",
		object_name, (unsigned long long) ofs,
		(unsigned long long) len, coll, coll_index);

	osdc = &rbd_dev->rbd_client->client->osdc;
	req = ceph_osdc_alloc_request(osdc, flags, snapc, ops,
					false, GFP_NOIO, pages, bio);
	if (!req) {
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

	strncpy(req->r_oid, object_name, sizeof(req->r_oid));
	req->r_oid_len = strlen(req->r_oid);

	layout = &req->r_file_layout;
	memset(layout, 0, sizeof(*layout));
	layout->fl_stripe_unit = cpu_to_le32(1 << RBD_MAX_OBJ_ORDER);
	layout->fl_stripe_count = cpu_to_le32(1);
	layout->fl_object_size = cpu_to_le32(1 << RBD_MAX_OBJ_ORDER);
	layout->fl_pg_pool = cpu_to_le32((int) rbd_dev->spec->pool_id);
	ret = ceph_calc_raw_layout(osdc, layout, snapid, ofs, &len, &bno,
				   req, ops);
	rbd_assert(ret == 0);

	ceph_osdc_build_request(req, ofs, &len,
				ops,
				snapc,
				&mtime,
				req->r_oid, req->r_oid_len);

	if (linger_req) {
		ceph_osdc_set_request_linger(osdc, req);
		*linger_req = req;
	}

	ret = ceph_osdc_start_request(osdc, req, false);
	if (ret < 0)
		goto done_err;

	if (!rbd_cb) {
		ret = ceph_osdc_wait_request(osdc, req);
		if (ver)
			*ver = le64_to_cpu(req->r_reassert_version.version);
		dout("reassert_ver=%llu\n",
			(unsigned long long)
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
	read_op = (le16_to_cpu(op->op) == CEPH_OSD_OP_READ);

	dout("rbd_req_cb bytes=%llu readop=%d rc=%d\n",
		(unsigned long long) bytes, read_op, (int) rc);

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
static int rbd_req_sync_op(struct rbd_device *rbd_dev,
			   struct ceph_snap_context *snapc,
			   u64 snapid,
			   int flags,
			   struct ceph_osd_req_op *ops,
			   const char *object_name,
			   u64 ofs, u64 inbound_size,
			   char *inbound,
			   struct ceph_osd_request **linger_req,
			   u64 *ver)
{
	int ret;
	struct page **pages;
	int num_pages;

	rbd_assert(ops != NULL);

	num_pages = calc_pages_for(ofs, inbound_size);
	pages = ceph_alloc_page_vector(num_pages, GFP_KERNEL);
	if (IS_ERR(pages))
		return PTR_ERR(pages);

	ret = rbd_do_request(NULL, rbd_dev, snapc, snapid,
			  object_name, ofs, inbound_size, NULL,
			  pages, num_pages,
			  flags,
			  ops,
			  NULL, 0,
			  NULL,
			  linger_req, ver);
	if (ret < 0)
		goto done;

	if ((flags & CEPH_OSD_FLAG_READ) && inbound)
		ret = ceph_copy_from_page_vector(pages, inbound, ofs, ret);

done:
	ceph_release_page_vector(pages, num_pages);
	return ret;
}

/*
 * Do an asynchronous ceph osd operation
 */
static int rbd_do_op(struct request *rq,
		     struct rbd_device *rbd_dev,
		     struct ceph_snap_context *snapc,
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
	int opcode;
	int flags;
	u64 snapid;

	seg_name = rbd_segment_name(rbd_dev, ofs);
	if (!seg_name)
		return -ENOMEM;
	seg_len = rbd_segment_length(rbd_dev, ofs, len);
	seg_ofs = rbd_segment_offset(rbd_dev, ofs);

	if (rq_data_dir(rq) == WRITE) {
		opcode = CEPH_OSD_OP_WRITE;
		flags = CEPH_OSD_FLAG_WRITE|CEPH_OSD_FLAG_ONDISK;
		snapid = CEPH_NOSNAP;
		payload_len = seg_len;
	} else {
		opcode = CEPH_OSD_OP_READ;
		flags = CEPH_OSD_FLAG_READ;
		snapc = NULL;
		snapid = rbd_dev->spec->snap_id;
		payload_len = 0;
	}

	ret = -ENOMEM;
	ops = rbd_create_rw_ops(1, opcode, payload_len);
	if (!ops)
		goto done;

	/* we've taken care of segment sizes earlier when we
	   cloned the bios. We should never have a segment
	   truncated at this point */
	rbd_assert(seg_len == len);

	ret = rbd_do_request(rq, rbd_dev, snapc, snapid,
			     seg_name, seg_ofs, seg_len,
			     bio,
			     NULL, 0,
			     flags,
			     ops,
			     coll, coll_index,
			     rbd_req_cb, 0, NULL);

	rbd_destroy_ops(ops);
done:
	kfree(seg_name);
	return ret;
}

/*
 * Request sync osd read
 */
static int rbd_req_sync_read(struct rbd_device *rbd_dev,
			  u64 snapid,
			  const char *object_name,
			  u64 ofs, u64 len,
			  char *buf,
			  u64 *ver)
{
	struct ceph_osd_req_op *ops;
	int ret;

	ops = rbd_create_rw_ops(1, CEPH_OSD_OP_READ, 0);
	if (!ops)
		return -ENOMEM;

	ret = rbd_req_sync_op(rbd_dev, NULL,
			       snapid,
			       CEPH_OSD_FLAG_READ,
			       ops, object_name, ofs, len, buf, NULL, ver);
	rbd_destroy_ops(ops);

	return ret;
}

/*
 * Request sync osd watch
 */
static int rbd_req_sync_notify_ack(struct rbd_device *rbd_dev,
				   u64 ver,
				   u64 notify_id)
{
	struct ceph_osd_req_op *ops;
	int ret;

	ops = rbd_create_rw_ops(1, CEPH_OSD_OP_NOTIFY_ACK, 0);
	if (!ops)
		return -ENOMEM;

	ops[0].watch.ver = cpu_to_le64(ver);
	ops[0].watch.cookie = notify_id;
	ops[0].watch.flag = 0;

	ret = rbd_do_request(NULL, rbd_dev, NULL, CEPH_NOSNAP,
			  rbd_dev->header_name, 0, 0, NULL,
			  NULL, 0,
			  CEPH_OSD_FLAG_READ,
			  ops,
			  NULL, 0,
			  rbd_simple_req_cb, 0, NULL);

	rbd_destroy_ops(ops);
	return ret;
}

static void rbd_watch_cb(u64 ver, u64 notify_id, u8 opcode, void *data)
{
	struct rbd_device *rbd_dev = (struct rbd_device *)data;
	u64 hver;
	int rc;

	if (!rbd_dev)
		return;

	dout("rbd_watch_cb %s notify_id=%llu opcode=%u\n",
		rbd_dev->header_name, (unsigned long long) notify_id,
		(unsigned int) opcode);
	rc = rbd_dev_refresh(rbd_dev, &hver);
	if (rc)
		pr_warning(RBD_DRV_NAME "%d got notification but failed to "
			   " update snaps: %d\n", rbd_dev->major, rc);

	rbd_req_sync_notify_ack(rbd_dev, hver, notify_id);
}

/*
 * Request sync osd watch
 */
static int rbd_req_sync_watch(struct rbd_device *rbd_dev)
{
	struct ceph_osd_req_op *ops;
	struct ceph_osd_client *osdc = &rbd_dev->rbd_client->client->osdc;
	int ret;

	ops = rbd_create_rw_ops(1, CEPH_OSD_OP_WATCH, 0);
	if (!ops)
		return -ENOMEM;

	ret = ceph_osdc_create_event(osdc, rbd_watch_cb, 0,
				     (void *)rbd_dev, &rbd_dev->watch_event);
	if (ret < 0)
		goto fail;

	ops[0].watch.ver = cpu_to_le64(rbd_dev->header.obj_version);
	ops[0].watch.cookie = cpu_to_le64(rbd_dev->watch_event->cookie);
	ops[0].watch.flag = 1;

	ret = rbd_req_sync_op(rbd_dev, NULL,
			      CEPH_NOSNAP,
			      CEPH_OSD_FLAG_WRITE | CEPH_OSD_FLAG_ONDISK,
			      ops,
			      rbd_dev->header_name,
			      0, 0, NULL,
			      &rbd_dev->watch_request, NULL);

	if (ret < 0)
		goto fail_event;

	rbd_destroy_ops(ops);
	return 0;

fail_event:
	ceph_osdc_cancel_event(rbd_dev->watch_event);
	rbd_dev->watch_event = NULL;
fail:
	rbd_destroy_ops(ops);
	return ret;
}

/*
 * Request sync osd unwatch
 */
static int rbd_req_sync_unwatch(struct rbd_device *rbd_dev)
{
	struct ceph_osd_req_op *ops;
	int ret;

	ops = rbd_create_rw_ops(1, CEPH_OSD_OP_WATCH, 0);
	if (!ops)
		return -ENOMEM;

	ops[0].watch.ver = 0;
	ops[0].watch.cookie = cpu_to_le64(rbd_dev->watch_event->cookie);
	ops[0].watch.flag = 0;

	ret = rbd_req_sync_op(rbd_dev, NULL,
			      CEPH_NOSNAP,
			      CEPH_OSD_FLAG_WRITE | CEPH_OSD_FLAG_ONDISK,
			      ops,
			      rbd_dev->header_name,
			      0, 0, NULL, NULL, NULL);


	rbd_destroy_ops(ops);
	ceph_osdc_cancel_event(rbd_dev->watch_event);
	rbd_dev->watch_event = NULL;
	return ret;
}

/*
 * Synchronous osd object method call
 */
static int rbd_req_sync_exec(struct rbd_device *rbd_dev,
			     const char *object_name,
			     const char *class_name,
			     const char *method_name,
			     const char *outbound,
			     size_t outbound_size,
			     char *inbound,
			     size_t inbound_size,
			     int flags,
			     u64 *ver)
{
	struct ceph_osd_req_op *ops;
	int class_name_len = strlen(class_name);
	int method_name_len = strlen(method_name);
	int payload_size;
	int ret;

	/*
	 * Any input parameters required by the method we're calling
	 * will be sent along with the class and method names as
	 * part of the message payload.  That data and its size are
	 * supplied via the indata and indata_len fields (named from
	 * the perspective of the server side) in the OSD request
	 * operation.
	 */
	payload_size = class_name_len + method_name_len + outbound_size;
	ops = rbd_create_rw_ops(1, CEPH_OSD_OP_CALL, payload_size);
	if (!ops)
		return -ENOMEM;

	ops[0].cls.class_name = class_name;
	ops[0].cls.class_len = (__u8) class_name_len;
	ops[0].cls.method_name = method_name;
	ops[0].cls.method_len = (__u8) method_name_len;
	ops[0].cls.argc = 0;
	ops[0].cls.indata = outbound;
	ops[0].cls.indata_len = outbound_size;

	ret = rbd_req_sync_op(rbd_dev, NULL,
			       CEPH_NOSNAP,
			       flags, ops,
			       object_name, 0, inbound_size, inbound,
			       NULL, ver);

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

	while ((rq = blk_fetch_request(q))) {
		struct bio *bio;
		bool do_write;
		unsigned int size;
		u64 ofs;
		int num_segs, cur_seg = 0;
		struct rbd_req_coll *coll;
		struct ceph_snap_context *snapc;
		unsigned int bio_offset;

		dout("fetched request\n");

		/* filter out block requests we don't understand */
		if ((rq->cmd_type != REQ_TYPE_FS)) {
			__blk_end_request_all(rq, 0);
			continue;
		}

		/* deduce our operation (read, write) */
		do_write = (rq_data_dir(rq) == WRITE);
		if (do_write && rbd_dev->mapping.read_only) {
			__blk_end_request_all(rq, -EROFS);
			continue;
		}

		spin_unlock_irq(q->queue_lock);

		down_read(&rbd_dev->header_rwsem);

		if (!rbd_dev->exists) {
			rbd_assert(rbd_dev->spec->snap_id != CEPH_NOSNAP);
			up_read(&rbd_dev->header_rwsem);
			dout("request for non-existent snapshot");
			spin_lock_irq(q->queue_lock);
			__blk_end_request_all(rq, -ENXIO);
			continue;
		}

		snapc = ceph_get_snap_context(rbd_dev->header.snapc);

		up_read(&rbd_dev->header_rwsem);

		size = blk_rq_bytes(rq);
		ofs = blk_rq_pos(rq) * SECTOR_SIZE;
		bio = rq->bio;

		dout("%s 0x%x bytes at 0x%llx\n",
		     do_write ? "write" : "read",
		     size, (unsigned long long) blk_rq_pos(rq) * SECTOR_SIZE);

		num_segs = rbd_get_num_segments(&rbd_dev->header, ofs, size);
		if (num_segs <= 0) {
			spin_lock_irq(q->queue_lock);
			__blk_end_request_all(rq, num_segs);
			ceph_put_snap_context(snapc);
			continue;
		}
		coll = rbd_alloc_coll(num_segs);
		if (!coll) {
			spin_lock_irq(q->queue_lock);
			__blk_end_request_all(rq, -ENOMEM);
			ceph_put_snap_context(snapc);
			continue;
		}

		bio_offset = 0;
		do {
			u64 limit = rbd_segment_length(rbd_dev, ofs, size);
			unsigned int chain_size;
			struct bio *bio_chain;

			BUG_ON(limit > (u64) UINT_MAX);
			chain_size = (unsigned int) limit;
			dout("rq->bio->bi_vcnt=%hu\n", rq->bio->bi_vcnt);

			kref_get(&coll->kref);

			/* Pass a cloned bio chain via an osd request */

			bio_chain = bio_chain_clone_range(&bio,
						&bio_offset, chain_size,
						GFP_ATOMIC);
			if (bio_chain)
				(void) rbd_do_op(rq, rbd_dev, snapc,
						ofs, chain_size,
						bio_chain, coll, cur_seg);
			else
				rbd_coll_end_req_index(rq, coll, cur_seg,
						       -ENOMEM, chain_size);
			size -= chain_size;
			ofs += chain_size;

			cur_seg++;
		} while (size > 0);
		kref_put(&coll->kref, rbd_coll_release);

		spin_lock_irq(q->queue_lock);

		ceph_put_snap_context(snapc);
	}
}

/*
 * a queue callback. Makes sure that we don't create a bio that spans across
 * multiple osd objects. One exception would be with a single page bios,
 * which we handle later at bio_chain_clone_range()
 */
static int rbd_merge_bvec(struct request_queue *q, struct bvec_merge_data *bmd,
			  struct bio_vec *bvec)
{
	struct rbd_device *rbd_dev = q->queuedata;
	sector_t sector_offset;
	sector_t sectors_per_obj;
	sector_t obj_sector_offset;
	int ret;

	/*
	 * Find how far into its rbd object the partition-relative
	 * bio start sector is to offset relative to the enclosing
	 * device.
	 */
	sector_offset = get_start_sect(bmd->bi_bdev) + bmd->bi_sector;
	sectors_per_obj = 1 << (rbd_dev->header.obj_order - SECTOR_SHIFT);
	obj_sector_offset = sector_offset & (sectors_per_obj - 1);

	/*
	 * Compute the number of bytes from that offset to the end
	 * of the object.  Account for what's already used by the bio.
	 */
	ret = (int) (sectors_per_obj - obj_sector_offset) << SECTOR_SHIFT;
	if (ret > bmd->bi_size)
		ret -= bmd->bi_size;
	else
		ret = 0;

	/*
	 * Don't send back more than was asked for.  And if the bio
	 * was empty, let the whole thing through because:  "Note
	 * that a block device *must* allow a single page to be
	 * added to an empty bio."
	 */
	rbd_assert(bvec->bv_len <= PAGE_SIZE);
	if (ret > (int) bvec->bv_len || !bmd->bi_size)
		ret = (int) bvec->bv_len;

	return ret;
}

static void rbd_free_disk(struct rbd_device *rbd_dev)
{
	struct gendisk *disk = rbd_dev->disk;

	if (!disk)
		return;

	if (disk->flags & GENHD_FL_UP)
		del_gendisk(disk);
	if (disk->queue)
		blk_cleanup_queue(disk->queue);
	put_disk(disk);
}

/*
 * Read the complete header for the given rbd device.
 *
 * Returns a pointer to a dynamically-allocated buffer containing
 * the complete and validated header.  Caller can pass the address
 * of a variable that will be filled in with the version of the
 * header object at the time it was read.
 *
 * Returns a pointer-coded errno if a failure occurs.
 */
static struct rbd_image_header_ondisk *
rbd_dev_v1_header_read(struct rbd_device *rbd_dev, u64 *version)
{
	struct rbd_image_header_ondisk *ondisk = NULL;
	u32 snap_count = 0;
	u64 names_size = 0;
	u32 want_count;
	int ret;

	/*
	 * The complete header will include an array of its 64-bit
	 * snapshot ids, followed by the names of those snapshots as
	 * a contiguous block of NUL-terminated strings.  Note that
	 * the number of snapshots could change by the time we read
	 * it in, in which case we re-read it.
	 */
	do {
		size_t size;

		kfree(ondisk);

		size = sizeof (*ondisk);
		size += snap_count * sizeof (struct rbd_image_snap_ondisk);
		size += names_size;
		ondisk = kmalloc(size, GFP_KERNEL);
		if (!ondisk)
			return ERR_PTR(-ENOMEM);

		ret = rbd_req_sync_read(rbd_dev, CEPH_NOSNAP,
				       rbd_dev->header_name,
				       0, size,
				       (char *) ondisk, version);

		if (ret < 0)
			goto out_err;
		if (WARN_ON((size_t) ret < size)) {
			ret = -ENXIO;
			pr_warning("short header read for image %s"
					" (want %zd got %d)\n",
				rbd_dev->spec->image_name, size, ret);
			goto out_err;
		}
		if (!rbd_dev_ondisk_valid(ondisk)) {
			ret = -ENXIO;
			pr_warning("invalid header for image %s\n",
				rbd_dev->spec->image_name);
			goto out_err;
		}

		names_size = le64_to_cpu(ondisk->snap_names_len);
		want_count = snap_count;
		snap_count = le32_to_cpu(ondisk->snap_count);
	} while (snap_count != want_count);

	return ondisk;

out_err:
	kfree(ondisk);

	return ERR_PTR(ret);
}

/*
 * reload the ondisk the header
 */
static int rbd_read_header(struct rbd_device *rbd_dev,
			   struct rbd_image_header *header)
{
	struct rbd_image_header_ondisk *ondisk;
	u64 ver = 0;
	int ret;

	ondisk = rbd_dev_v1_header_read(rbd_dev, &ver);
	if (IS_ERR(ondisk))
		return PTR_ERR(ondisk);
	ret = rbd_header_from_disk(header, ondisk);
	if (ret >= 0)
		header->obj_version = ver;
	kfree(ondisk);

	return ret;
}

static void rbd_remove_all_snaps(struct rbd_device *rbd_dev)
{
	struct rbd_snap *snap;
	struct rbd_snap *next;

	list_for_each_entry_safe(snap, next, &rbd_dev->snaps, node)
		rbd_remove_snap_dev(snap);
}

static void rbd_update_mapping_size(struct rbd_device *rbd_dev)
{
	sector_t size;

	if (rbd_dev->spec->snap_id != CEPH_NOSNAP)
		return;

	size = (sector_t) rbd_dev->header.image_size / SECTOR_SIZE;
	dout("setting size to %llu sectors", (unsigned long long) size);
	rbd_dev->mapping.size = (u64) size;
	set_capacity(rbd_dev->disk, size);
}

/*
 * only read the first part of the ondisk header, without the snaps info
 */
static int rbd_dev_v1_refresh(struct rbd_device *rbd_dev, u64 *hver)
{
	int ret;
	struct rbd_image_header h;

	ret = rbd_read_header(rbd_dev, &h);
	if (ret < 0)
		return ret;

	down_write(&rbd_dev->header_rwsem);

	/* Update image size, and check for resize of mapped image */
	rbd_dev->header.image_size = h.image_size;
	rbd_update_mapping_size(rbd_dev);

	/* rbd_dev->header.object_prefix shouldn't change */
	kfree(rbd_dev->header.snap_sizes);
	kfree(rbd_dev->header.snap_names);
	/* osd requests may still refer to snapc */
	ceph_put_snap_context(rbd_dev->header.snapc);

	if (hver)
		*hver = h.obj_version;
	rbd_dev->header.obj_version = h.obj_version;
	rbd_dev->header.image_size = h.image_size;
	rbd_dev->header.snapc = h.snapc;
	rbd_dev->header.snap_names = h.snap_names;
	rbd_dev->header.snap_sizes = h.snap_sizes;
	/* Free the extra copy of the object prefix */
	WARN_ON(strcmp(rbd_dev->header.object_prefix, h.object_prefix));
	kfree(h.object_prefix);

	ret = rbd_dev_snaps_update(rbd_dev);
	if (!ret)
		ret = rbd_dev_snaps_register(rbd_dev);

	up_write(&rbd_dev->header_rwsem);

	return ret;
}

static int rbd_dev_refresh(struct rbd_device *rbd_dev, u64 *hver)
{
	int ret;

	rbd_assert(rbd_image_format_valid(rbd_dev->image_format));
	mutex_lock_nested(&ctl_mutex, SINGLE_DEPTH_NESTING);
	if (rbd_dev->image_format == 1)
		ret = rbd_dev_v1_refresh(rbd_dev, hver);
	else
		ret = rbd_dev_v2_refresh(rbd_dev, hver);
	mutex_unlock(&ctl_mutex);

	return ret;
}

static int rbd_init_disk(struct rbd_device *rbd_dev)
{
	struct gendisk *disk;
	struct request_queue *q;
	u64 segment_size;

	/* create gendisk info */
	disk = alloc_disk(RBD_MINORS_PER_MAJOR);
	if (!disk)
		return -ENOMEM;

	snprintf(disk->disk_name, sizeof(disk->disk_name), RBD_DRV_NAME "%d",
		 rbd_dev->dev_id);
	disk->major = rbd_dev->major;
	disk->first_minor = 0;
	disk->fops = &rbd_bd_ops;
	disk->private_data = rbd_dev;

	/* init rq */
	q = blk_init_queue(rbd_rq_fn, &rbd_dev->lock);
	if (!q)
		goto out_disk;

	/* We use the default size, but let's be explicit about it. */
	blk_queue_physical_block_size(q, SECTOR_SIZE);

	/* set io sizes to object size */
	segment_size = rbd_obj_bytes(&rbd_dev->header);
	blk_queue_max_hw_sectors(q, segment_size / SECTOR_SIZE);
	blk_queue_max_segment_size(q, segment_size);
	blk_queue_io_min(q, segment_size);
	blk_queue_io_opt(q, segment_size);

	blk_queue_merge_bvec(q, rbd_merge_bvec);
	disk->queue = q;

	q->queuedata = rbd_dev;

	rbd_dev->disk = disk;

	set_capacity(rbd_dev->disk, rbd_dev->mapping.size / SECTOR_SIZE);

	return 0;
out_disk:
	put_disk(disk);

	return -ENOMEM;
}

/*
  sysfs
*/

static struct rbd_device *dev_to_rbd_dev(struct device *dev)
{
	return container_of(dev, struct rbd_device, dev);
}

static ssize_t rbd_size_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct rbd_device *rbd_dev = dev_to_rbd_dev(dev);
	sector_t size;

	down_read(&rbd_dev->header_rwsem);
	size = get_capacity(rbd_dev->disk);
	up_read(&rbd_dev->header_rwsem);

	return sprintf(buf, "%llu\n", (unsigned long long) size * SECTOR_SIZE);
}

/*
 * Note this shows the features for whatever's mapped, which is not
 * necessarily the base image.
 */
static ssize_t rbd_features_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct rbd_device *rbd_dev = dev_to_rbd_dev(dev);

	return sprintf(buf, "0x%016llx\n",
			(unsigned long long) rbd_dev->mapping.features);
}

static ssize_t rbd_major_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct rbd_device *rbd_dev = dev_to_rbd_dev(dev);

	return sprintf(buf, "%d\n", rbd_dev->major);
}

static ssize_t rbd_client_id_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct rbd_device *rbd_dev = dev_to_rbd_dev(dev);

	return sprintf(buf, "client%lld\n",
			ceph_client_id(rbd_dev->rbd_client->client));
}

static ssize_t rbd_pool_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct rbd_device *rbd_dev = dev_to_rbd_dev(dev);

	return sprintf(buf, "%s\n", rbd_dev->spec->pool_name);
}

static ssize_t rbd_pool_id_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct rbd_device *rbd_dev = dev_to_rbd_dev(dev);

	return sprintf(buf, "%llu\n",
		(unsigned long long) rbd_dev->spec->pool_id);
}

static ssize_t rbd_name_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct rbd_device *rbd_dev = dev_to_rbd_dev(dev);

	if (rbd_dev->spec->image_name)
		return sprintf(buf, "%s\n", rbd_dev->spec->image_name);

	return sprintf(buf, "(unknown)\n");
}

static ssize_t rbd_image_id_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct rbd_device *rbd_dev = dev_to_rbd_dev(dev);

	return sprintf(buf, "%s\n", rbd_dev->spec->image_id);
}

/*
 * Shows the name of the currently-mapped snapshot (or
 * RBD_SNAP_HEAD_NAME for the base image).
 */
static ssize_t rbd_snap_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	struct rbd_device *rbd_dev = dev_to_rbd_dev(dev);

	return sprintf(buf, "%s\n", rbd_dev->spec->snap_name);
}

/*
 * For an rbd v2 image, shows the pool id, image id, and snapshot id
 * for the parent image.  If there is no parent, simply shows
 * "(no parent image)".
 */
static ssize_t rbd_parent_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	struct rbd_device *rbd_dev = dev_to_rbd_dev(dev);
	struct rbd_spec *spec = rbd_dev->parent_spec;
	int count;
	char *bufp = buf;

	if (!spec)
		return sprintf(buf, "(no parent image)\n");

	count = sprintf(bufp, "pool_id %llu\npool_name %s\n",
			(unsigned long long) spec->pool_id, spec->pool_name);
	if (count < 0)
		return count;
	bufp += count;

	count = sprintf(bufp, "image_id %s\nimage_name %s\n", spec->image_id,
			spec->image_name ? spec->image_name : "(unknown)");
	if (count < 0)
		return count;
	bufp += count;

	count = sprintf(bufp, "snap_id %llu\nsnap_name %s\n",
			(unsigned long long) spec->snap_id, spec->snap_name);
	if (count < 0)
		return count;
	bufp += count;

	count = sprintf(bufp, "overlap %llu\n", rbd_dev->parent_overlap);
	if (count < 0)
		return count;
	bufp += count;

	return (ssize_t) (bufp - buf);
}

static ssize_t rbd_image_refresh(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf,
				 size_t size)
{
	struct rbd_device *rbd_dev = dev_to_rbd_dev(dev);
	int ret;

	ret = rbd_dev_refresh(rbd_dev, NULL);

	return ret < 0 ? ret : size;
}

static DEVICE_ATTR(size, S_IRUGO, rbd_size_show, NULL);
static DEVICE_ATTR(features, S_IRUGO, rbd_features_show, NULL);
static DEVICE_ATTR(major, S_IRUGO, rbd_major_show, NULL);
static DEVICE_ATTR(client_id, S_IRUGO, rbd_client_id_show, NULL);
static DEVICE_ATTR(pool, S_IRUGO, rbd_pool_show, NULL);
static DEVICE_ATTR(pool_id, S_IRUGO, rbd_pool_id_show, NULL);
static DEVICE_ATTR(name, S_IRUGO, rbd_name_show, NULL);
static DEVICE_ATTR(image_id, S_IRUGO, rbd_image_id_show, NULL);
static DEVICE_ATTR(refresh, S_IWUSR, NULL, rbd_image_refresh);
static DEVICE_ATTR(current_snap, S_IRUGO, rbd_snap_show, NULL);
static DEVICE_ATTR(parent, S_IRUGO, rbd_parent_show, NULL);

static struct attribute *rbd_attrs[] = {
	&dev_attr_size.attr,
	&dev_attr_features.attr,
	&dev_attr_major.attr,
	&dev_attr_client_id.attr,
	&dev_attr_pool.attr,
	&dev_attr_pool_id.attr,
	&dev_attr_name.attr,
	&dev_attr_image_id.attr,
	&dev_attr_current_snap.attr,
	&dev_attr_parent.attr,
	&dev_attr_refresh.attr,
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

	return sprintf(buf, "%llu\n", (unsigned long long)snap->size);
}

static ssize_t rbd_snap_id_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct rbd_snap *snap = container_of(dev, struct rbd_snap, dev);

	return sprintf(buf, "%llu\n", (unsigned long long)snap->id);
}

static ssize_t rbd_snap_features_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct rbd_snap *snap = container_of(dev, struct rbd_snap, dev);

	return sprintf(buf, "0x%016llx\n",
			(unsigned long long) snap->features);
}

static DEVICE_ATTR(snap_size, S_IRUGO, rbd_snap_size_show, NULL);
static DEVICE_ATTR(snap_id, S_IRUGO, rbd_snap_id_show, NULL);
static DEVICE_ATTR(snap_features, S_IRUGO, rbd_snap_features_show, NULL);

static struct attribute *rbd_snap_attrs[] = {
	&dev_attr_snap_size.attr,
	&dev_attr_snap_id.attr,
	&dev_attr_snap_features.attr,
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

static struct rbd_spec *rbd_spec_get(struct rbd_spec *spec)
{
	kref_get(&spec->kref);

	return spec;
}

static void rbd_spec_free(struct kref *kref);
static void rbd_spec_put(struct rbd_spec *spec)
{
	if (spec)
		kref_put(&spec->kref, rbd_spec_free);
}

static struct rbd_spec *rbd_spec_alloc(void)
{
	struct rbd_spec *spec;

	spec = kzalloc(sizeof (*spec), GFP_KERNEL);
	if (!spec)
		return NULL;
	kref_init(&spec->kref);

	rbd_spec_put(rbd_spec_get(spec));	/* TEMPORARY */

	return spec;
}

static void rbd_spec_free(struct kref *kref)
{
	struct rbd_spec *spec = container_of(kref, struct rbd_spec, kref);

	kfree(spec->pool_name);
	kfree(spec->image_id);
	kfree(spec->image_name);
	kfree(spec->snap_name);
	kfree(spec);
}

struct rbd_device *rbd_dev_create(struct rbd_client *rbdc,
				struct rbd_spec *spec)
{
	struct rbd_device *rbd_dev;

	rbd_dev = kzalloc(sizeof (*rbd_dev), GFP_KERNEL);
	if (!rbd_dev)
		return NULL;

	spin_lock_init(&rbd_dev->lock);
	INIT_LIST_HEAD(&rbd_dev->node);
	INIT_LIST_HEAD(&rbd_dev->snaps);
	init_rwsem(&rbd_dev->header_rwsem);

	rbd_dev->spec = spec;
	rbd_dev->rbd_client = rbdc;

	return rbd_dev;
}

static void rbd_dev_destroy(struct rbd_device *rbd_dev)
{
	rbd_spec_put(rbd_dev->parent_spec);
	kfree(rbd_dev->header_name);
	rbd_put_client(rbd_dev->rbd_client);
	rbd_spec_put(rbd_dev->spec);
	kfree(rbd_dev);
}

static bool rbd_snap_registered(struct rbd_snap *snap)
{
	bool ret = snap->dev.type == &rbd_snap_device_type;
	bool reg = device_is_registered(&snap->dev);

	rbd_assert(!ret ^ reg);

	return ret;
}

static void rbd_remove_snap_dev(struct rbd_snap *snap)
{
	list_del(&snap->node);
	if (device_is_registered(&snap->dev))
		device_unregister(&snap->dev);
}

static int rbd_register_snap_dev(struct rbd_snap *snap,
				  struct device *parent)
{
	struct device *dev = &snap->dev;
	int ret;

	dev->type = &rbd_snap_device_type;
	dev->parent = parent;
	dev->release = rbd_snap_dev_release;
	dev_set_name(dev, "%s%s", RBD_SNAP_DEV_NAME_PREFIX, snap->name);
	dout("%s: registering device for snapshot %s\n", __func__, snap->name);

	ret = device_register(dev);

	return ret;
}

static struct rbd_snap *__rbd_add_snap_dev(struct rbd_device *rbd_dev,
						const char *snap_name,
						u64 snap_id, u64 snap_size,
						u64 snap_features)
{
	struct rbd_snap *snap;
	int ret;

	snap = kzalloc(sizeof (*snap), GFP_KERNEL);
	if (!snap)
		return ERR_PTR(-ENOMEM);

	ret = -ENOMEM;
	snap->name = kstrdup(snap_name, GFP_KERNEL);
	if (!snap->name)
		goto err;

	snap->id = snap_id;
	snap->size = snap_size;
	snap->features = snap_features;

	return snap;

err:
	kfree(snap->name);
	kfree(snap);

	return ERR_PTR(ret);
}

static char *rbd_dev_v1_snap_info(struct rbd_device *rbd_dev, u32 which,
		u64 *snap_size, u64 *snap_features)
{
	char *snap_name;

	rbd_assert(which < rbd_dev->header.snapc->num_snaps);

	*snap_size = rbd_dev->header.snap_sizes[which];
	*snap_features = 0;	/* No features for v1 */

	/* Skip over names until we find the one we are looking for */

	snap_name = rbd_dev->header.snap_names;
	while (which--)
		snap_name += strlen(snap_name) + 1;

	return snap_name;
}

/*
 * Get the size and object order for an image snapshot, or if
 * snap_id is CEPH_NOSNAP, gets this information for the base
 * image.
 */
static int _rbd_dev_v2_snap_size(struct rbd_device *rbd_dev, u64 snap_id,
				u8 *order, u64 *snap_size)
{
	__le64 snapid = cpu_to_le64(snap_id);
	int ret;
	struct {
		u8 order;
		__le64 size;
	} __attribute__ ((packed)) size_buf = { 0 };

	ret = rbd_req_sync_exec(rbd_dev, rbd_dev->header_name,
				"rbd", "get_size",
				(char *) &snapid, sizeof (snapid),
				(char *) &size_buf, sizeof (size_buf),
				CEPH_OSD_FLAG_READ, NULL);
	dout("%s: rbd_req_sync_exec returned %d\n", __func__, ret);
	if (ret < 0)
		return ret;

	*order = size_buf.order;
	*snap_size = le64_to_cpu(size_buf.size);

	dout("  snap_id 0x%016llx order = %u, snap_size = %llu\n",
		(unsigned long long) snap_id, (unsigned int) *order,
		(unsigned long long) *snap_size);

	return 0;
}

static int rbd_dev_v2_image_size(struct rbd_device *rbd_dev)
{
	return _rbd_dev_v2_snap_size(rbd_dev, CEPH_NOSNAP,
					&rbd_dev->header.obj_order,
					&rbd_dev->header.image_size);
}

static int rbd_dev_v2_object_prefix(struct rbd_device *rbd_dev)
{
	void *reply_buf;
	int ret;
	void *p;

	reply_buf = kzalloc(RBD_OBJ_PREFIX_LEN_MAX, GFP_KERNEL);
	if (!reply_buf)
		return -ENOMEM;

	ret = rbd_req_sync_exec(rbd_dev, rbd_dev->header_name,
				"rbd", "get_object_prefix",
				NULL, 0,
				reply_buf, RBD_OBJ_PREFIX_LEN_MAX,
				CEPH_OSD_FLAG_READ, NULL);
	dout("%s: rbd_req_sync_exec returned %d\n", __func__, ret);
	if (ret < 0)
		goto out;
	ret = 0;    /* rbd_req_sync_exec() can return positive */

	p = reply_buf;
	rbd_dev->header.object_prefix = ceph_extract_encoded_string(&p,
						p + RBD_OBJ_PREFIX_LEN_MAX,
						NULL, GFP_NOIO);

	if (IS_ERR(rbd_dev->header.object_prefix)) {
		ret = PTR_ERR(rbd_dev->header.object_prefix);
		rbd_dev->header.object_prefix = NULL;
	} else {
		dout("  object_prefix = %s\n", rbd_dev->header.object_prefix);
	}

out:
	kfree(reply_buf);

	return ret;
}

static int _rbd_dev_v2_snap_features(struct rbd_device *rbd_dev, u64 snap_id,
		u64 *snap_features)
{
	__le64 snapid = cpu_to_le64(snap_id);
	struct {
		__le64 features;
		__le64 incompat;
	} features_buf = { 0 };
	u64 incompat;
	int ret;

	ret = rbd_req_sync_exec(rbd_dev, rbd_dev->header_name,
				"rbd", "get_features",
				(char *) &snapid, sizeof (snapid),
				(char *) &features_buf, sizeof (features_buf),
				CEPH_OSD_FLAG_READ, NULL);
	dout("%s: rbd_req_sync_exec returned %d\n", __func__, ret);
	if (ret < 0)
		return ret;

	incompat = le64_to_cpu(features_buf.incompat);
	if (incompat & ~RBD_FEATURES_ALL)
		return -ENXIO;

	*snap_features = le64_to_cpu(features_buf.features);

	dout("  snap_id 0x%016llx features = 0x%016llx incompat = 0x%016llx\n",
		(unsigned long long) snap_id,
		(unsigned long long) *snap_features,
		(unsigned long long) le64_to_cpu(features_buf.incompat));

	return 0;
}

static int rbd_dev_v2_features(struct rbd_device *rbd_dev)
{
	return _rbd_dev_v2_snap_features(rbd_dev, CEPH_NOSNAP,
						&rbd_dev->header.features);
}

static int rbd_dev_v2_parent_info(struct rbd_device *rbd_dev)
{
	struct rbd_spec *parent_spec;
	size_t size;
	void *reply_buf = NULL;
	__le64 snapid;
	void *p;
	void *end;
	char *image_id;
	u64 overlap;
	size_t len = 0;
	int ret;

	parent_spec = rbd_spec_alloc();
	if (!parent_spec)
		return -ENOMEM;

	size = sizeof (__le64) +				/* pool_id */
		sizeof (__le32) + RBD_IMAGE_ID_LEN_MAX +	/* image_id */
		sizeof (__le64) +				/* snap_id */
		sizeof (__le64);				/* overlap */
	reply_buf = kmalloc(size, GFP_KERNEL);
	if (!reply_buf) {
		ret = -ENOMEM;
		goto out_err;
	}

	snapid = cpu_to_le64(CEPH_NOSNAP);
	ret = rbd_req_sync_exec(rbd_dev, rbd_dev->header_name,
				"rbd", "get_parent",
				(char *) &snapid, sizeof (snapid),
				(char *) reply_buf, size,
				CEPH_OSD_FLAG_READ, NULL);
	dout("%s: rbd_req_sync_exec returned %d\n", __func__, ret);
	if (ret < 0)
		goto out_err;

	ret = -ERANGE;
	p = reply_buf;
	end = (char *) reply_buf + size;
	ceph_decode_64_safe(&p, end, parent_spec->pool_id, out_err);
	if (parent_spec->pool_id == CEPH_NOPOOL)
		goto out;	/* No parent?  No problem. */

	image_id = ceph_extract_encoded_string(&p, end, &len, GFP_KERNEL);
	if (IS_ERR(image_id)) {
		ret = PTR_ERR(image_id);
		goto out_err;
	}
	parent_spec->image_id = image_id;
	parent_spec->image_id_len = len;
	ceph_decode_64_safe(&p, end, parent_spec->snap_id, out_err);
	ceph_decode_64_safe(&p, end, overlap, out_err);

	rbd_dev->parent_overlap = overlap;
	rbd_dev->parent_spec = parent_spec;
	parent_spec = NULL;	/* rbd_dev now owns this */
out:
	ret = 0;
out_err:
	kfree(reply_buf);
	rbd_spec_put(parent_spec);

	return ret;
}

static char *rbd_dev_image_name(struct rbd_device *rbd_dev)
{
	size_t image_id_size;
	char *image_id;
	void *p;
	void *end;
	size_t size;
	void *reply_buf = NULL;
	size_t len = 0;
	char *image_name = NULL;
	int ret;

	rbd_assert(!rbd_dev->spec->image_name);

	image_id_size = sizeof (__le32) + rbd_dev->spec->image_id_len;
	image_id = kmalloc(image_id_size, GFP_KERNEL);
	if (!image_id)
		return NULL;

	p = image_id;
	end = (char *) image_id + image_id_size;
	ceph_encode_string(&p, end, rbd_dev->spec->image_id,
				(u32) rbd_dev->spec->image_id_len);

	size = sizeof (__le32) + RBD_IMAGE_NAME_LEN_MAX;
	reply_buf = kmalloc(size, GFP_KERNEL);
	if (!reply_buf)
		goto out;

	ret = rbd_req_sync_exec(rbd_dev, RBD_DIRECTORY,
				"rbd", "dir_get_name",
				image_id, image_id_size,
				(char *) reply_buf, size,
				CEPH_OSD_FLAG_READ, NULL);
	if (ret < 0)
		goto out;
	p = reply_buf;
	end = (char *) reply_buf + size;
	image_name = ceph_extract_encoded_string(&p, end, &len, GFP_KERNEL);
	if (IS_ERR(image_name))
		image_name = NULL;
	else
		dout("%s: name is %s len is %zd\n", __func__, image_name, len);
out:
	kfree(reply_buf);
	kfree(image_id);

	return image_name;
}

/*
 * When a parent image gets probed, we only have the pool, image,
 * and snapshot ids but not the names of any of them.  This call
 * is made later to fill in those names.  It has to be done after
 * rbd_dev_snaps_update() has completed because some of the
 * information (in particular, snapshot name) is not available
 * until then.
 */
static int rbd_dev_probe_update_spec(struct rbd_device *rbd_dev)
{
	struct ceph_osd_client *osdc;
	const char *name;
	void *reply_buf = NULL;
	int ret;

	if (rbd_dev->spec->pool_name)
		return 0;	/* Already have the names */

	/* Look up the pool name */

	osdc = &rbd_dev->rbd_client->client->osdc;
	name = ceph_pg_pool_name_by_id(osdc->osdmap, rbd_dev->spec->pool_id);
	if (!name)
		return -EIO;	/* pool id too large (>= 2^31) */

	rbd_dev->spec->pool_name = kstrdup(name, GFP_KERNEL);
	if (!rbd_dev->spec->pool_name)
		return -ENOMEM;

	/* Fetch the image name; tolerate failure here */

	name = rbd_dev_image_name(rbd_dev);
	if (name) {
		rbd_dev->spec->image_name_len = strlen(name);
		rbd_dev->spec->image_name = (char *) name;
	} else {
		pr_warning(RBD_DRV_NAME "%d "
			"unable to get image name for image id %s\n",
			rbd_dev->major, rbd_dev->spec->image_id);
	}

	/* Look up the snapshot name. */

	name = rbd_snap_name(rbd_dev, rbd_dev->spec->snap_id);
	if (!name) {
		ret = -EIO;
		goto out_err;
	}
	rbd_dev->spec->snap_name = kstrdup(name, GFP_KERNEL);
	if(!rbd_dev->spec->snap_name)
		goto out_err;

	return 0;
out_err:
	kfree(reply_buf);
	kfree(rbd_dev->spec->pool_name);
	rbd_dev->spec->pool_name = NULL;

	return ret;
}

static int rbd_dev_v2_snap_context(struct rbd_device *rbd_dev, u64 *ver)
{
	size_t size;
	int ret;
	void *reply_buf;
	void *p;
	void *end;
	u64 seq;
	u32 snap_count;
	struct ceph_snap_context *snapc;
	u32 i;

	/*
	 * We'll need room for the seq value (maximum snapshot id),
	 * snapshot count, and array of that many snapshot ids.
	 * For now we have a fixed upper limit on the number we're
	 * prepared to receive.
	 */
	size = sizeof (__le64) + sizeof (__le32) +
			RBD_MAX_SNAP_COUNT * sizeof (__le64);
	reply_buf = kzalloc(size, GFP_KERNEL);
	if (!reply_buf)
		return -ENOMEM;

	ret = rbd_req_sync_exec(rbd_dev, rbd_dev->header_name,
				"rbd", "get_snapcontext",
				NULL, 0,
				reply_buf, size,
				CEPH_OSD_FLAG_READ, ver);
	dout("%s: rbd_req_sync_exec returned %d\n", __func__, ret);
	if (ret < 0)
		goto out;

	ret = -ERANGE;
	p = reply_buf;
	end = (char *) reply_buf + size;
	ceph_decode_64_safe(&p, end, seq, out);
	ceph_decode_32_safe(&p, end, snap_count, out);

	/*
	 * Make sure the reported number of snapshot ids wouldn't go
	 * beyond the end of our buffer.  But before checking that,
	 * make sure the computed size of the snapshot context we
	 * allocate is representable in a size_t.
	 */
	if (snap_count > (SIZE_MAX - sizeof (struct ceph_snap_context))
				 / sizeof (u64)) {
		ret = -EINVAL;
		goto out;
	}
	if (!ceph_has_room(&p, end, snap_count * sizeof (__le64)))
		goto out;

	size = sizeof (struct ceph_snap_context) +
				snap_count * sizeof (snapc->snaps[0]);
	snapc = kmalloc(size, GFP_KERNEL);
	if (!snapc) {
		ret = -ENOMEM;
		goto out;
	}

	atomic_set(&snapc->nref, 1);
	snapc->seq = seq;
	snapc->num_snaps = snap_count;
	for (i = 0; i < snap_count; i++)
		snapc->snaps[i] = ceph_decode_64(&p);

	rbd_dev->header.snapc = snapc;

	dout("  snap context seq = %llu, snap_count = %u\n",
		(unsigned long long) seq, (unsigned int) snap_count);

out:
	kfree(reply_buf);

	return 0;
}

static char *rbd_dev_v2_snap_name(struct rbd_device *rbd_dev, u32 which)
{
	size_t size;
	void *reply_buf;
	__le64 snap_id;
	int ret;
	void *p;
	void *end;
	char *snap_name;

	size = sizeof (__le32) + RBD_MAX_SNAP_NAME_LEN;
	reply_buf = kmalloc(size, GFP_KERNEL);
	if (!reply_buf)
		return ERR_PTR(-ENOMEM);

	snap_id = cpu_to_le64(rbd_dev->header.snapc->snaps[which]);
	ret = rbd_req_sync_exec(rbd_dev, rbd_dev->header_name,
				"rbd", "get_snapshot_name",
				(char *) &snap_id, sizeof (snap_id),
				reply_buf, size,
				CEPH_OSD_FLAG_READ, NULL);
	dout("%s: rbd_req_sync_exec returned %d\n", __func__, ret);
	if (ret < 0)
		goto out;

	p = reply_buf;
	end = (char *) reply_buf + size;
	snap_name = ceph_extract_encoded_string(&p, end, NULL, GFP_KERNEL);
	if (IS_ERR(snap_name)) {
		ret = PTR_ERR(snap_name);
		goto out;
	} else {
		dout("  snap_id 0x%016llx snap_name = %s\n",
			(unsigned long long) le64_to_cpu(snap_id), snap_name);
	}
	kfree(reply_buf);

	return snap_name;
out:
	kfree(reply_buf);

	return ERR_PTR(ret);
}

static char *rbd_dev_v2_snap_info(struct rbd_device *rbd_dev, u32 which,
		u64 *snap_size, u64 *snap_features)
{
	__le64 snap_id;
	u8 order;
	int ret;

	snap_id = rbd_dev->header.snapc->snaps[which];
	ret = _rbd_dev_v2_snap_size(rbd_dev, snap_id, &order, snap_size);
	if (ret)
		return ERR_PTR(ret);
	ret = _rbd_dev_v2_snap_features(rbd_dev, snap_id, snap_features);
	if (ret)
		return ERR_PTR(ret);

	return rbd_dev_v2_snap_name(rbd_dev, which);
}

static char *rbd_dev_snap_info(struct rbd_device *rbd_dev, u32 which,
		u64 *snap_size, u64 *snap_features)
{
	if (rbd_dev->image_format == 1)
		return rbd_dev_v1_snap_info(rbd_dev, which,
					snap_size, snap_features);
	if (rbd_dev->image_format == 2)
		return rbd_dev_v2_snap_info(rbd_dev, which,
					snap_size, snap_features);
	return ERR_PTR(-EINVAL);
}

static int rbd_dev_v2_refresh(struct rbd_device *rbd_dev, u64 *hver)
{
	int ret;
	__u8 obj_order;

	down_write(&rbd_dev->header_rwsem);

	/* Grab old order first, to see if it changes */

	obj_order = rbd_dev->header.obj_order,
	ret = rbd_dev_v2_image_size(rbd_dev);
	if (ret)
		goto out;
	if (rbd_dev->header.obj_order != obj_order) {
		ret = -EIO;
		goto out;
	}
	rbd_update_mapping_size(rbd_dev);

	ret = rbd_dev_v2_snap_context(rbd_dev, hver);
	dout("rbd_dev_v2_snap_context returned %d\n", ret);
	if (ret)
		goto out;
	ret = rbd_dev_snaps_update(rbd_dev);
	dout("rbd_dev_snaps_update returned %d\n", ret);
	if (ret)
		goto out;
	ret = rbd_dev_snaps_register(rbd_dev);
	dout("rbd_dev_snaps_register returned %d\n", ret);
out:
	up_write(&rbd_dev->header_rwsem);

	return ret;
}

/*
 * Scan the rbd device's current snapshot list and compare it to the
 * newly-received snapshot context.  Remove any existing snapshots
 * not present in the new snapshot context.  Add a new snapshot for
 * any snaphots in the snapshot context not in the current list.
 * And verify there are no changes to snapshots we already know
 * about.
 *
 * Assumes the snapshots in the snapshot context are sorted by
 * snapshot id, highest id first.  (Snapshots in the rbd_dev's list
 * are also maintained in that order.)
 */
static int rbd_dev_snaps_update(struct rbd_device *rbd_dev)
{
	struct ceph_snap_context *snapc = rbd_dev->header.snapc;
	const u32 snap_count = snapc->num_snaps;
	struct list_head *head = &rbd_dev->snaps;
	struct list_head *links = head->next;
	u32 index = 0;

	dout("%s: snap count is %u\n", __func__, (unsigned int) snap_count);
	while (index < snap_count || links != head) {
		u64 snap_id;
		struct rbd_snap *snap;
		char *snap_name;
		u64 snap_size = 0;
		u64 snap_features = 0;

		snap_id = index < snap_count ? snapc->snaps[index]
					     : CEPH_NOSNAP;
		snap = links != head ? list_entry(links, struct rbd_snap, node)
				     : NULL;
		rbd_assert(!snap || snap->id != CEPH_NOSNAP);

		if (snap_id == CEPH_NOSNAP || (snap && snap->id > snap_id)) {
			struct list_head *next = links->next;

			/* Existing snapshot not in the new snap context */

			if (rbd_dev->spec->snap_id == snap->id)
				rbd_dev->exists = false;
			rbd_remove_snap_dev(snap);
			dout("%ssnap id %llu has been removed\n",
				rbd_dev->spec->snap_id == snap->id ?
							"mapped " : "",
				(unsigned long long) snap->id);

			/* Done with this list entry; advance */

			links = next;
			continue;
		}

		snap_name = rbd_dev_snap_info(rbd_dev, index,
					&snap_size, &snap_features);
		if (IS_ERR(snap_name))
			return PTR_ERR(snap_name);

		dout("entry %u: snap_id = %llu\n", (unsigned int) snap_count,
			(unsigned long long) snap_id);
		if (!snap || (snap_id != CEPH_NOSNAP && snap->id < snap_id)) {
			struct rbd_snap *new_snap;

			/* We haven't seen this snapshot before */

			new_snap = __rbd_add_snap_dev(rbd_dev, snap_name,
					snap_id, snap_size, snap_features);
			if (IS_ERR(new_snap)) {
				int err = PTR_ERR(new_snap);

				dout("  failed to add dev, error %d\n", err);

				return err;
			}

			/* New goes before existing, or at end of list */

			dout("  added dev%s\n", snap ? "" : " at end\n");
			if (snap)
				list_add_tail(&new_snap->node, &snap->node);
			else
				list_add_tail(&new_snap->node, head);
		} else {
			/* Already have this one */

			dout("  already present\n");

			rbd_assert(snap->size == snap_size);
			rbd_assert(!strcmp(snap->name, snap_name));
			rbd_assert(snap->features == snap_features);

			/* Done with this list entry; advance */

			links = links->next;
		}

		/* Advance to the next entry in the snapshot context */

		index++;
	}
	dout("%s: done\n", __func__);

	return 0;
}

/*
 * Scan the list of snapshots and register the devices for any that
 * have not already been registered.
 */
static int rbd_dev_snaps_register(struct rbd_device *rbd_dev)
{
	struct rbd_snap *snap;
	int ret = 0;

	dout("%s called\n", __func__);
	if (WARN_ON(!device_is_registered(&rbd_dev->dev)))
		return -EIO;

	list_for_each_entry(snap, &rbd_dev->snaps, node) {
		if (!rbd_snap_registered(snap)) {
			ret = rbd_register_snap_dev(snap, &rbd_dev->dev);
			if (ret < 0)
				break;
		}
	}
	dout("%s: returning %d\n", __func__, ret);

	return ret;
}

static int rbd_bus_add_dev(struct rbd_device *rbd_dev)
{
	struct device *dev;
	int ret;

	mutex_lock_nested(&ctl_mutex, SINGLE_DEPTH_NESTING);

	dev = &rbd_dev->dev;
	dev->bus = &rbd_bus_type;
	dev->type = &rbd_device_type;
	dev->parent = &rbd_root_dev;
	dev->release = rbd_dev_release;
	dev_set_name(dev, "%d", rbd_dev->dev_id);
	ret = device_register(dev);

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
		ret = rbd_req_sync_watch(rbd_dev);
		if (ret == -ERANGE) {
			rc = rbd_dev_refresh(rbd_dev, NULL);
			if (rc < 0)
				return rc;
		}
	} while (ret == -ERANGE);

	return ret;
}

static atomic64_t rbd_dev_id_max = ATOMIC64_INIT(0);

/*
 * Get a unique rbd identifier for the given new rbd_dev, and add
 * the rbd_dev to the global list.  The minimum rbd id is 1.
 */
static void rbd_dev_id_get(struct rbd_device *rbd_dev)
{
	rbd_dev->dev_id = atomic64_inc_return(&rbd_dev_id_max);

	spin_lock(&rbd_dev_list_lock);
	list_add_tail(&rbd_dev->node, &rbd_dev_list);
	spin_unlock(&rbd_dev_list_lock);
	dout("rbd_dev %p given dev id %llu\n", rbd_dev,
		(unsigned long long) rbd_dev->dev_id);
}

/*
 * Remove an rbd_dev from the global list, and record that its
 * identifier is no longer in use.
 */
static void rbd_dev_id_put(struct rbd_device *rbd_dev)
{
	struct list_head *tmp;
	int rbd_id = rbd_dev->dev_id;
	int max_id;

	rbd_assert(rbd_id > 0);

	dout("rbd_dev %p released dev id %llu\n", rbd_dev,
		(unsigned long long) rbd_dev->dev_id);
	spin_lock(&rbd_dev_list_lock);
	list_del_init(&rbd_dev->node);

	/*
	 * If the id being "put" is not the current maximum, there
	 * is nothing special we need to do.
	 */
	if (rbd_id != atomic64_read(&rbd_dev_id_max)) {
		spin_unlock(&rbd_dev_list_lock);
		return;
	}

	/*
	 * We need to update the current maximum id.  Search the
	 * list to find out what it is.  We're more likely to find
	 * the maximum at the end, so search the list backward.
	 */
	max_id = 0;
	list_for_each_prev(tmp, &rbd_dev_list) {
		struct rbd_device *rbd_dev;

		rbd_dev = list_entry(tmp, struct rbd_device, node);
		if (rbd_dev->dev_id > max_id)
			max_id = rbd_dev->dev_id;
	}
	spin_unlock(&rbd_dev_list_lock);

	/*
	 * The max id could have been updated by rbd_dev_id_get(), in
	 * which case it now accurately reflects the new maximum.
	 * Be careful not to overwrite the maximum value in that
	 * case.
	 */
	atomic64_cmpxchg(&rbd_dev_id_max, rbd_id, max_id);
	dout("  max dev id has been reset\n");
}

/*
 * Skips over white space at *buf, and updates *buf to point to the
 * first found non-space character (if any). Returns the length of
 * the token (string of non-white space characters) found.  Note
 * that *buf must be terminated with '\0'.
 */
static inline size_t next_token(const char **buf)
{
        /*
        * These are the characters that produce nonzero for
        * isspace() in the "C" and "POSIX" locales.
        */
        const char *spaces = " \f\n\r\t\v";

        *buf += strspn(*buf, spaces);	/* Find start of token */

	return strcspn(*buf, spaces);   /* Return token length */
}

/*
 * Finds the next token in *buf, and if the provided token buffer is
 * big enough, copies the found token into it.  The result, if
 * copied, is guaranteed to be terminated with '\0'.  Note that *buf
 * must be terminated with '\0' on entry.
 *
 * Returns the length of the token found (not including the '\0').
 * Return value will be 0 if no token is found, and it will be >=
 * token_size if the token would not fit.
 *
 * The *buf pointer will be updated to point beyond the end of the
 * found token.  Note that this occurs even if the token buffer is
 * too small to hold it.
 */
static inline size_t copy_token(const char **buf,
				char *token,
				size_t token_size)
{
        size_t len;

	len = next_token(buf);
	if (len < token_size) {
		memcpy(token, *buf, len);
		*(token + len) = '\0';
	}
	*buf += len;

        return len;
}

/*
 * Finds the next token in *buf, dynamically allocates a buffer big
 * enough to hold a copy of it, and copies the token into the new
 * buffer.  The copy is guaranteed to be terminated with '\0'.  Note
 * that a duplicate buffer is created even for a zero-length token.
 *
 * Returns a pointer to the newly-allocated duplicate, or a null
 * pointer if memory for the duplicate was not available.  If
 * the lenp argument is a non-null pointer, the length of the token
 * (not including the '\0') is returned in *lenp.
 *
 * If successful, the *buf pointer will be updated to point beyond
 * the end of the found token.
 *
 * Note: uses GFP_KERNEL for allocation.
 */
static inline char *dup_token(const char **buf, size_t *lenp)
{
	char *dup;
	size_t len;

	len = next_token(buf);
	dup = kmalloc(len + 1, GFP_KERNEL);
	if (!dup)
		return NULL;

	memcpy(dup, *buf, len);
	*(dup + len) = '\0';
	*buf += len;

	if (lenp)
		*lenp = len;

	return dup;
}

/*
 * Parse the options provided for an "rbd add" (i.e., rbd image
 * mapping) request.  These arrive via a write to /sys/bus/rbd/add,
 * and the data written is passed here via a NUL-terminated buffer.
 * Returns 0 if successful or an error code otherwise.
 *
 * The information extracted from these options is recorded in
 * the other parameters which return dynamically-allocated
 * structures:
 *  ceph_opts
 *      The address of a pointer that will refer to a ceph options
 *      structure.  Caller must release the returned pointer using
 *      ceph_destroy_options() when it is no longer needed.
 *  rbd_opts
 *	Address of an rbd options pointer.  Fully initialized by
 *	this function; caller must release with kfree().
 *  spec
 *	Address of an rbd image specification pointer.  Fully
 *	initialized by this function based on parsed options.
 *	Caller must release with rbd_spec_put().
 *
 * The options passed take this form:
 *  <mon_addrs> <options> <pool_name> <image_name> [<snap_id>]
 * where:
 *  <mon_addrs>
 *      A comma-separated list of one or more monitor addresses.
 *      A monitor address is an ip address, optionally followed
 *      by a port number (separated by a colon).
 *        I.e.:  ip1[:port1][,ip2[:port2]...]
 *  <options>
 *      A comma-separated list of ceph and/or rbd options.
 *  <pool_name>
 *      The name of the rados pool containing the rbd image.
 *  <image_name>
 *      The name of the image in that pool to map.
 *  <snap_id>
 *      An optional snapshot id.  If provided, the mapping will
 *      present data from the image at the time that snapshot was
 *      created.  The image head is used if no snapshot id is
 *      provided.  Snapshot mappings are always read-only.
 */
static int rbd_add_parse_args(const char *buf,
				struct ceph_options **ceph_opts,
				struct rbd_options **opts,
				struct rbd_spec **rbd_spec)
{
	size_t len;
	char *options;
	const char *mon_addrs;
	size_t mon_addrs_size;
	struct rbd_spec *spec = NULL;
	struct rbd_options *rbd_opts = NULL;
	struct ceph_options *copts;
	int ret;

	/* The first four tokens are required */

	len = next_token(&buf);
	if (!len)
		return -EINVAL;	/* Missing monitor address(es) */
	mon_addrs = buf;
	mon_addrs_size = len + 1;
	buf += len;

	ret = -EINVAL;
	options = dup_token(&buf, NULL);
	if (!options)
		return -ENOMEM;
	if (!*options)
		goto out_err;	/* Missing options */

	spec = rbd_spec_alloc();
	if (!spec)
		goto out_mem;

	spec->pool_name = dup_token(&buf, NULL);
	if (!spec->pool_name)
		goto out_mem;
	if (!*spec->pool_name)
		goto out_err;	/* Missing pool name */

	spec->image_name = dup_token(&buf, &spec->image_name_len);
	if (!spec->image_name)
		goto out_mem;
	if (!*spec->image_name)
		goto out_err;	/* Missing image name */

	/*
	 * Snapshot name is optional; default is to use "-"
	 * (indicating the head/no snapshot).
	 */
	len = next_token(&buf);
	if (!len) {
		buf = RBD_SNAP_HEAD_NAME; /* No snapshot supplied */
		len = sizeof (RBD_SNAP_HEAD_NAME) - 1;
	} else if (len > RBD_MAX_SNAP_NAME_LEN) {
		ret = -ENAMETOOLONG;
		goto out_err;
	}
	spec->snap_name = kmalloc(len + 1, GFP_KERNEL);
	if (!spec->snap_name)
		goto out_mem;
	memcpy(spec->snap_name, buf, len);
	*(spec->snap_name + len) = '\0';

	/* Initialize all rbd options to the defaults */

	rbd_opts = kzalloc(sizeof (*rbd_opts), GFP_KERNEL);
	if (!rbd_opts)
		goto out_mem;

	rbd_opts->read_only = RBD_READ_ONLY_DEFAULT;

	copts = ceph_parse_options(options, mon_addrs,
					mon_addrs + mon_addrs_size - 1,
					parse_rbd_opts_token, rbd_opts);
	if (IS_ERR(copts)) {
		ret = PTR_ERR(copts);
		goto out_err;
	}
	kfree(options);

	*ceph_opts = copts;
	*opts = rbd_opts;
	*rbd_spec = spec;

	return 0;
out_mem:
	ret = -ENOMEM;
out_err:
	kfree(rbd_opts);
	rbd_spec_put(spec);
	kfree(options);

	return ret;
}

/*
 * An rbd format 2 image has a unique identifier, distinct from the
 * name given to it by the user.  Internally, that identifier is
 * what's used to specify the names of objects related to the image.
 *
 * A special "rbd id" object is used to map an rbd image name to its
 * id.  If that object doesn't exist, then there is no v2 rbd image
 * with the supplied name.
 *
 * This function will record the given rbd_dev's image_id field if
 * it can be determined, and in that case will return 0.  If any
 * errors occur a negative errno will be returned and the rbd_dev's
 * image_id field will be unchanged (and should be NULL).
 */
static int rbd_dev_image_id(struct rbd_device *rbd_dev)
{
	int ret;
	size_t size;
	char *object_name;
	void *response;
	void *p;

	/*
	 * When probing a parent image, the image id is already
	 * known (and the image name likely is not).  There's no
	 * need to fetch the image id again in this case.
	 */
	if (rbd_dev->spec->image_id)
		return 0;

	/*
	 * First, see if the format 2 image id file exists, and if
	 * so, get the image's persistent id from it.
	 */
	size = sizeof (RBD_ID_PREFIX) + rbd_dev->spec->image_name_len;
	object_name = kmalloc(size, GFP_NOIO);
	if (!object_name)
		return -ENOMEM;
	sprintf(object_name, "%s%s", RBD_ID_PREFIX, rbd_dev->spec->image_name);
	dout("rbd id object name is %s\n", object_name);

	/* Response will be an encoded string, which includes a length */

	size = sizeof (__le32) + RBD_IMAGE_ID_LEN_MAX;
	response = kzalloc(size, GFP_NOIO);
	if (!response) {
		ret = -ENOMEM;
		goto out;
	}

	ret = rbd_req_sync_exec(rbd_dev, object_name,
				"rbd", "get_id",
				NULL, 0,
				response, RBD_IMAGE_ID_LEN_MAX,
				CEPH_OSD_FLAG_READ, NULL);
	dout("%s: rbd_req_sync_exec returned %d\n", __func__, ret);
	if (ret < 0)
		goto out;
	ret = 0;    /* rbd_req_sync_exec() can return positive */

	p = response;
	rbd_dev->spec->image_id = ceph_extract_encoded_string(&p,
						p + RBD_IMAGE_ID_LEN_MAX,
						&rbd_dev->spec->image_id_len,
						GFP_NOIO);
	if (IS_ERR(rbd_dev->spec->image_id)) {
		ret = PTR_ERR(rbd_dev->spec->image_id);
		rbd_dev->spec->image_id = NULL;
	} else {
		dout("image_id is %s\n", rbd_dev->spec->image_id);
	}
out:
	kfree(response);
	kfree(object_name);

	return ret;
}

static int rbd_dev_v1_probe(struct rbd_device *rbd_dev)
{
	int ret;
	size_t size;

	/* Version 1 images have no id; empty string is used */

	rbd_dev->spec->image_id = kstrdup("", GFP_KERNEL);
	if (!rbd_dev->spec->image_id)
		return -ENOMEM;
	rbd_dev->spec->image_id_len = 0;

	/* Record the header object name for this rbd image. */

	size = rbd_dev->spec->image_name_len + sizeof (RBD_SUFFIX);
	rbd_dev->header_name = kmalloc(size, GFP_KERNEL);
	if (!rbd_dev->header_name) {
		ret = -ENOMEM;
		goto out_err;
	}
	sprintf(rbd_dev->header_name, "%s%s",
		rbd_dev->spec->image_name, RBD_SUFFIX);

	/* Populate rbd image metadata */

	ret = rbd_read_header(rbd_dev, &rbd_dev->header);
	if (ret < 0)
		goto out_err;

	/* Version 1 images have no parent (no layering) */

	rbd_dev->parent_spec = NULL;
	rbd_dev->parent_overlap = 0;

	rbd_dev->image_format = 1;

	dout("discovered version 1 image, header name is %s\n",
		rbd_dev->header_name);

	return 0;

out_err:
	kfree(rbd_dev->header_name);
	rbd_dev->header_name = NULL;
	kfree(rbd_dev->spec->image_id);
	rbd_dev->spec->image_id = NULL;

	return ret;
}

static int rbd_dev_v2_probe(struct rbd_device *rbd_dev)
{
	size_t size;
	int ret;
	u64 ver = 0;

	/*
	 * Image id was filled in by the caller.  Record the header
	 * object name for this rbd image.
	 */
	size = sizeof (RBD_HEADER_PREFIX) + rbd_dev->spec->image_id_len;
	rbd_dev->header_name = kmalloc(size, GFP_KERNEL);
	if (!rbd_dev->header_name)
		return -ENOMEM;
	sprintf(rbd_dev->header_name, "%s%s",
			RBD_HEADER_PREFIX, rbd_dev->spec->image_id);

	/* Get the size and object order for the image */

	ret = rbd_dev_v2_image_size(rbd_dev);
	if (ret < 0)
		goto out_err;

	/* Get the object prefix (a.k.a. block_name) for the image */

	ret = rbd_dev_v2_object_prefix(rbd_dev);
	if (ret < 0)
		goto out_err;

	/* Get the and check features for the image */

	ret = rbd_dev_v2_features(rbd_dev);
	if (ret < 0)
		goto out_err;

	/* If the image supports layering, get the parent info */

	if (rbd_dev->header.features & RBD_FEATURE_LAYERING) {
		ret = rbd_dev_v2_parent_info(rbd_dev);
		if (ret < 0)
			goto out_err;
	}

	/* crypto and compression type aren't (yet) supported for v2 images */

	rbd_dev->header.crypt_type = 0;
	rbd_dev->header.comp_type = 0;

	/* Get the snapshot context, plus the header version */

	ret = rbd_dev_v2_snap_context(rbd_dev, &ver);
	if (ret)
		goto out_err;
	rbd_dev->header.obj_version = ver;

	rbd_dev->image_format = 2;

	dout("discovered version 2 image, header name is %s\n",
		rbd_dev->header_name);

	return 0;
out_err:
	rbd_dev->parent_overlap = 0;
	rbd_spec_put(rbd_dev->parent_spec);
	rbd_dev->parent_spec = NULL;
	kfree(rbd_dev->header_name);
	rbd_dev->header_name = NULL;
	kfree(rbd_dev->header.object_prefix);
	rbd_dev->header.object_prefix = NULL;

	return ret;
}

static int rbd_dev_probe_finish(struct rbd_device *rbd_dev)
{
	int ret;

	/* no need to lock here, as rbd_dev is not registered yet */
	ret = rbd_dev_snaps_update(rbd_dev);
	if (ret)
		return ret;

	ret = rbd_dev_probe_update_spec(rbd_dev);
	if (ret)
		goto err_out_snaps;

	ret = rbd_dev_set_mapping(rbd_dev);
	if (ret)
		goto err_out_snaps;

	/* generate unique id: find highest unique id, add one */
	rbd_dev_id_get(rbd_dev);

	/* Fill in the device name, now that we have its id. */
	BUILD_BUG_ON(DEV_NAME_LEN
			< sizeof (RBD_DRV_NAME) + MAX_INT_FORMAT_WIDTH);
	sprintf(rbd_dev->name, "%s%d", RBD_DRV_NAME, rbd_dev->dev_id);

	/* Get our block major device number. */

	ret = register_blkdev(0, rbd_dev->name);
	if (ret < 0)
		goto err_out_id;
	rbd_dev->major = ret;

	/* Set up the blkdev mapping. */

	ret = rbd_init_disk(rbd_dev);
	if (ret)
		goto err_out_blkdev;

	ret = rbd_bus_add_dev(rbd_dev);
	if (ret)
		goto err_out_disk;

	/*
	 * At this point cleanup in the event of an error is the job
	 * of the sysfs code (initiated by rbd_bus_del_dev()).
	 */
	down_write(&rbd_dev->header_rwsem);
	ret = rbd_dev_snaps_register(rbd_dev);
	up_write(&rbd_dev->header_rwsem);
	if (ret)
		goto err_out_bus;

	ret = rbd_init_watch_dev(rbd_dev);
	if (ret)
		goto err_out_bus;

	/* Everything's ready.  Announce the disk to the world. */

	add_disk(rbd_dev->disk);

	pr_info("%s: added with size 0x%llx\n", rbd_dev->disk->disk_name,
		(unsigned long long) rbd_dev->mapping.size);

	return ret;
err_out_bus:
	/* this will also clean up rest of rbd_dev stuff */

	rbd_bus_del_dev(rbd_dev);

	return ret;
err_out_disk:
	rbd_free_disk(rbd_dev);
err_out_blkdev:
	unregister_blkdev(rbd_dev->major, rbd_dev->name);
err_out_id:
	rbd_dev_id_put(rbd_dev);
err_out_snaps:
	rbd_remove_all_snaps(rbd_dev);

	return ret;
}

/*
 * Probe for the existence of the header object for the given rbd
 * device.  For format 2 images this includes determining the image
 * id.
 */
static int rbd_dev_probe(struct rbd_device *rbd_dev)
{
	int ret;

	/*
	 * Get the id from the image id object.  If it's not a
	 * format 2 image, we'll get ENOENT back, and we'll assume
	 * it's a format 1 image.
	 */
	ret = rbd_dev_image_id(rbd_dev);
	if (ret)
		ret = rbd_dev_v1_probe(rbd_dev);
	else
		ret = rbd_dev_v2_probe(rbd_dev);
	if (ret) {
		dout("probe failed, returning %d\n", ret);

		return ret;
	}

	ret = rbd_dev_probe_finish(rbd_dev);
	if (ret)
		rbd_header_free(&rbd_dev->header);

	return ret;
}

static ssize_t rbd_add(struct bus_type *bus,
		       const char *buf,
		       size_t count)
{
	struct rbd_device *rbd_dev = NULL;
	struct ceph_options *ceph_opts = NULL;
	struct rbd_options *rbd_opts = NULL;
	struct rbd_spec *spec = NULL;
	struct rbd_client *rbdc;
	struct ceph_osd_client *osdc;
	int rc = -ENOMEM;

	if (!try_module_get(THIS_MODULE))
		return -ENODEV;

	/* parse add command */
	rc = rbd_add_parse_args(buf, &ceph_opts, &rbd_opts, &spec);
	if (rc < 0)
		goto err_out_module;

	rbdc = rbd_get_client(ceph_opts);
	if (IS_ERR(rbdc)) {
		rc = PTR_ERR(rbdc);
		goto err_out_args;
	}
	ceph_opts = NULL;	/* rbd_dev client now owns this */

	/* pick the pool */
	osdc = &rbdc->client->osdc;
	rc = ceph_pg_poolid_by_name(osdc->osdmap, spec->pool_name);
	if (rc < 0)
		goto err_out_client;
	spec->pool_id = (u64) rc;

	rbd_dev = rbd_dev_create(rbdc, spec);
	if (!rbd_dev)
		goto err_out_client;
	rbdc = NULL;		/* rbd_dev now owns this */
	spec = NULL;		/* rbd_dev now owns this */

	rbd_dev->mapping.read_only = rbd_opts->read_only;
	kfree(rbd_opts);
	rbd_opts = NULL;	/* done with this */

	rc = rbd_dev_probe(rbd_dev);
	if (rc < 0)
		goto err_out_rbd_dev;

	return count;
err_out_rbd_dev:
	rbd_dev_destroy(rbd_dev);
err_out_client:
	rbd_put_client(rbdc);
err_out_args:
	if (ceph_opts)
		ceph_destroy_options(ceph_opts);
	kfree(rbd_opts);
	rbd_spec_put(spec);
err_out_module:
	module_put(THIS_MODULE);

	dout("Error adding device %s\n", buf);

	return (ssize_t) rc;
}

static struct rbd_device *__rbd_get_dev(unsigned long dev_id)
{
	struct list_head *tmp;
	struct rbd_device *rbd_dev;

	spin_lock(&rbd_dev_list_lock);
	list_for_each(tmp, &rbd_dev_list) {
		rbd_dev = list_entry(tmp, struct rbd_device, node);
		if (rbd_dev->dev_id == dev_id) {
			spin_unlock(&rbd_dev_list_lock);
			return rbd_dev;
		}
	}
	spin_unlock(&rbd_dev_list_lock);
	return NULL;
}

static void rbd_dev_release(struct device *dev)
{
	struct rbd_device *rbd_dev = dev_to_rbd_dev(dev);

	if (rbd_dev->watch_request) {
		struct ceph_client *client = rbd_dev->rbd_client->client;

		ceph_osdc_unregister_linger_request(&client->osdc,
						    rbd_dev->watch_request);
	}
	if (rbd_dev->watch_event)
		rbd_req_sync_unwatch(rbd_dev);


	/* clean up and free blkdev */
	rbd_free_disk(rbd_dev);
	unregister_blkdev(rbd_dev->major, rbd_dev->name);

	/* release allocated disk header fields */
	rbd_header_free(&rbd_dev->header);

	/* done with the id, and with the rbd_dev */
	rbd_dev_id_put(rbd_dev);
	rbd_assert(rbd_dev->rbd_client != NULL);
	rbd_dev_destroy(rbd_dev);

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

	if (rbd_dev->open_count) {
		ret = -EBUSY;
		goto done;
	}

	rbd_remove_all_snaps(rbd_dev);
	rbd_bus_del_dev(rbd_dev);

done:
	mutex_unlock(&ctl_mutex);

	return ret;
}

/*
 * create control files in sysfs
 * /sys/bus/rbd/...
 */
static int rbd_sysfs_init(void)
{
	int ret;

	ret = device_register(&rbd_root_dev);
	if (ret < 0)
		return ret;

	ret = bus_register(&rbd_bus_type);
	if (ret < 0)
		device_unregister(&rbd_root_dev);

	return ret;
}

static void rbd_sysfs_cleanup(void)
{
	bus_unregister(&rbd_bus_type);
	device_unregister(&rbd_root_dev);
}

int __init rbd_init(void)
{
	int rc;

	rc = rbd_sysfs_init();
	if (rc)
		return rc;
	pr_info("loaded " RBD_DRV_NAME_LONG "\n");
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
