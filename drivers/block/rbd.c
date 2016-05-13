
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
#include <linux/bsearch.h>

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/blk-mq.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/idr.h>
#include <linux/workqueue.h>

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

/*
 * Increment the given counter and return its updated value.
 * If the counter is already 0 it will not be incremented.
 * If the counter is already at its maximum value returns
 * -EINVAL without updating it.
 */
static int atomic_inc_return_safe(atomic_t *v)
{
	unsigned int counter;

	counter = (unsigned int)__atomic_add_unless(v, 1, 0);
	if (counter <= (unsigned int)INT_MAX)
		return (int)counter;

	atomic_dec(v);

	return -EINVAL;
}

/* Decrement the counter.  Return the resulting value, or -EINVAL */
static int atomic_dec_return_safe(atomic_t *v)
{
	int counter;

	counter = atomic_dec_return(v);
	if (counter >= 0)
		return counter;

	atomic_inc(v);

	return -EINVAL;
}

#define RBD_DRV_NAME "rbd"

#define RBD_MINORS_PER_MAJOR		256
#define RBD_SINGLE_MAJOR_PART_SHIFT	4

#define RBD_MAX_PARENT_CHAIN_LEN	16

#define RBD_SNAP_DEV_NAME_PREFIX	"snap_"
#define RBD_MAX_SNAP_NAME_LEN	\
			(NAME_MAX - (sizeof (RBD_SNAP_DEV_NAME_PREFIX) - 1))

#define RBD_MAX_SNAP_COUNT	510	/* allows max snapc to fit in 4KB */

#define RBD_SNAP_HEAD_NAME	"-"

#define	BAD_SNAP_INDEX	U32_MAX		/* invalid index into snap array */

/* This allows a single page to hold an image name sent by OSD */
#define RBD_IMAGE_NAME_LEN_MAX	(PAGE_SIZE - sizeof (__le32) - 1)
#define RBD_IMAGE_ID_LEN_MAX	64

#define RBD_OBJ_PREFIX_LEN_MAX	64

/* Feature bits */

#define RBD_FEATURE_LAYERING	(1<<0)
#define RBD_FEATURE_STRIPINGV2	(1<<1)
#define RBD_FEATURES_ALL \
	    (RBD_FEATURE_LAYERING | RBD_FEATURE_STRIPINGV2)

/* Features supported by this (client software) implementation. */

#define RBD_FEATURES_SUPPORTED	(RBD_FEATURES_ALL)

/*
 * An RBD device name will be "rbd#", where the "rbd" comes from
 * RBD_DRV_NAME above, and # is a unique integer identifier.
 * MAX_INT_FORMAT_WIDTH is used in ensuring DEV_NAME_LEN is big
 * enough to hold all possible device names.
 */
#define DEV_NAME_LEN		32
#define MAX_INT_FORMAT_WIDTH	((5 * sizeof (int)) / 2 + 1)

/*
 * block device image metadata (in-memory version)
 */
struct rbd_image_header {
	/* These six fields never change for a given rbd image */
	char *object_prefix;
	__u8 obj_order;
	__u8 crypt_type;
	__u8 comp_type;
	u64 stripe_unit;
	u64 stripe_count;
	u64 features;		/* Might be changeable someday? */

	/* The remaining fields need to be updated occasionally */
	u64 image_size;
	struct ceph_snap_context *snapc;
	char *snap_names;	/* format 1 only */
	u64 *snap_sizes;	/* format 1 only */
};

/*
 * An rbd image specification.
 *
 * The tuple (pool_id, image_id, snap_id) is sufficient to uniquely
 * identify an image.  Each rbd_dev structure includes a pointer to
 * an rbd_spec structure that encapsulates this identity.
 *
 * Each of the id's in an rbd_spec has an associated name.  For a
 * user-mapped image, the names are supplied and the id's associated
 * with them are looked up.  For a layered image, a parent image is
 * defined by the tuple, and the names are looked up.
 *
 * An rbd_dev structure contains a parent_spec pointer which is
 * non-null if the image it represents is a child in a layered
 * image.  This pointer will refer to the rbd_spec structure used
 * by the parent rbd_dev for its own identity (i.e., the structure
 * is shared between the parent and child).
 *
 * Since these structures are populated once, during the discovery
 * phase of image construction, they are effectively immutable so
 * we make no effort to synchronize access to them.
 *
 * Note that code herein does not assume the image name is known (it
 * could be a null pointer).
 */
struct rbd_spec {
	u64		pool_id;
	const char	*pool_name;

	const char	*image_id;
	const char	*image_name;

	u64		snap_id;
	const char	*snap_name;

	struct kref	kref;
};

/*
 * an instance of the client.  multiple devices may share an rbd client.
 */
struct rbd_client {
	struct ceph_client	*client;
	struct kref		kref;
	struct list_head	node;
};

struct rbd_img_request;
typedef void (*rbd_img_callback_t)(struct rbd_img_request *);

#define	BAD_WHICH	U32_MAX		/* Good which or bad which, which? */

struct rbd_obj_request;
typedef void (*rbd_obj_callback_t)(struct rbd_obj_request *);

enum obj_request_type {
	OBJ_REQUEST_NODATA, OBJ_REQUEST_BIO, OBJ_REQUEST_PAGES
};

enum obj_operation_type {
	OBJ_OP_WRITE,
	OBJ_OP_READ,
	OBJ_OP_DISCARD,
};

enum obj_req_flags {
	OBJ_REQ_DONE,		/* completion flag: not done = 0, done = 1 */
	OBJ_REQ_IMG_DATA,	/* object usage: standalone = 0, image = 1 */
	OBJ_REQ_KNOWN,		/* EXISTS flag valid: no = 0, yes = 1 */
	OBJ_REQ_EXISTS,		/* target exists: no = 0, yes = 1 */
};

struct rbd_obj_request {
	const char		*object_name;
	u64			offset;		/* object start byte */
	u64			length;		/* bytes from offset */
	unsigned long		flags;

	/*
	 * An object request associated with an image will have its
	 * img_data flag set; a standalone object request will not.
	 *
	 * A standalone object request will have which == BAD_WHICH
	 * and a null obj_request pointer.
	 *
	 * An object request initiated in support of a layered image
	 * object (to check for its existence before a write) will
	 * have which == BAD_WHICH and a non-null obj_request pointer.
	 *
	 * Finally, an object request for rbd image data will have
	 * which != BAD_WHICH, and will have a non-null img_request
	 * pointer.  The value of which will be in the range
	 * 0..(img_request->obj_request_count-1).
	 */
	union {
		struct rbd_obj_request	*obj_request;	/* STAT op */
		struct {
			struct rbd_img_request	*img_request;
			u64			img_offset;
			/* links for img_request->obj_requests list */
			struct list_head	links;
		};
	};
	u32			which;		/* posn image request list */

	enum obj_request_type	type;
	union {
		struct bio	*bio_list;
		struct {
			struct page	**pages;
			u32		page_count;
		};
	};
	struct page		**copyup_pages;
	u32			copyup_page_count;

	struct ceph_osd_request	*osd_req;

	u64			xferred;	/* bytes transferred */
	int			result;

	rbd_obj_callback_t	callback;
	struct completion	completion;

	struct kref		kref;
};

enum img_req_flags {
	IMG_REQ_WRITE,		/* I/O direction: read = 0, write = 1 */
	IMG_REQ_CHILD,		/* initiator: block = 0, child image = 1 */
	IMG_REQ_LAYERED,	/* ENOENT handling: normal = 0, layered = 1 */
	IMG_REQ_DISCARD,	/* discard: normal = 0, discard request = 1 */
};

struct rbd_img_request {
	struct rbd_device	*rbd_dev;
	u64			offset;	/* starting image byte offset */
	u64			length;	/* byte count from offset */
	unsigned long		flags;
	union {
		u64			snap_id;	/* for reads */
		struct ceph_snap_context *snapc;	/* for writes */
	};
	union {
		struct request		*rq;		/* block request */
		struct rbd_obj_request	*obj_request;	/* obj req initiator */
	};
	struct page		**copyup_pages;
	u32			copyup_page_count;
	spinlock_t		completion_lock;/* protects next_completion */
	u32			next_completion;
	rbd_img_callback_t	callback;
	u64			xferred;/* aggregate bytes transferred */
	int			result;	/* first nonzero obj_request result */

	u32			obj_request_count;
	struct list_head	obj_requests;	/* rbd_obj_request structs */

	struct kref		kref;
};

#define for_each_obj_request(ireq, oreq) \
	list_for_each_entry(oreq, &(ireq)->obj_requests, links)
#define for_each_obj_request_from(ireq, oreq) \
	list_for_each_entry_from(oreq, &(ireq)->obj_requests, links)
#define for_each_obj_request_safe(ireq, oreq, n) \
	list_for_each_entry_safe_reverse(oreq, n, &(ireq)->obj_requests, links)

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
	int			minor;
	struct gendisk		*disk;		/* blkdev's gendisk and rq */

	u32			image_format;	/* Either 1 or 2 */
	struct rbd_client	*rbd_client;

	char			name[DEV_NAME_LEN]; /* blkdev name, e.g. rbd3 */

	spinlock_t		lock;		/* queue, flags, open_count */

	struct rbd_image_header	header;
	unsigned long		flags;		/* possibly lock protected */
	struct rbd_spec		*spec;
	struct rbd_options	*opts;

	char			*header_name;

	struct ceph_file_layout	layout;

	struct ceph_osd_event   *watch_event;
	struct rbd_obj_request	*watch_request;

	struct rbd_spec		*parent_spec;
	u64			parent_overlap;
	atomic_t		parent_ref;
	struct rbd_device	*parent;

	/* Block layer tags. */
	struct blk_mq_tag_set	tag_set;

	/* protects updating the header */
	struct rw_semaphore     header_rwsem;

	struct rbd_mapping	mapping;

	struct list_head	node;

	/* sysfs related */
	struct device		dev;
	unsigned long		open_count;	/* protected by lock */
};

/*
 * Flag bits for rbd_dev->flags.  If atomicity is required,
 * rbd_dev->lock is used to protect access.
 *
 * Currently, only the "removing" flag (which is coupled with the
 * "open_count" field) requires atomic access.
 */
enum rbd_dev_flags {
	RBD_DEV_FLAG_EXISTS,	/* mapped snapshot has not been deleted */
	RBD_DEV_FLAG_REMOVING,	/* this mapping is being removed */
};

static DEFINE_MUTEX(client_mutex);	/* Serialize client creation */

static LIST_HEAD(rbd_dev_list);    /* devices */
static DEFINE_SPINLOCK(rbd_dev_list_lock);

static LIST_HEAD(rbd_client_list);		/* clients */
static DEFINE_SPINLOCK(rbd_client_list_lock);

/* Slab caches for frequently-allocated structures */

static struct kmem_cache	*rbd_img_request_cache;
static struct kmem_cache	*rbd_obj_request_cache;
static struct kmem_cache	*rbd_segment_name_cache;

static int rbd_major;
static DEFINE_IDA(rbd_dev_id_ida);

static struct workqueue_struct *rbd_wq;

/*
 * Default to false for now, as single-major requires >= 0.75 version of
 * userspace rbd utility.
 */
static bool single_major = false;
module_param(single_major, bool, S_IRUGO);
MODULE_PARM_DESC(single_major, "Use a single major number for all rbd devices (default: false)");

static int rbd_img_request_submit(struct rbd_img_request *img_request);

static ssize_t rbd_add(struct bus_type *bus, const char *buf,
		       size_t count);
static ssize_t rbd_remove(struct bus_type *bus, const char *buf,
			  size_t count);
static ssize_t rbd_add_single_major(struct bus_type *bus, const char *buf,
				    size_t count);
static ssize_t rbd_remove_single_major(struct bus_type *bus, const char *buf,
				       size_t count);
static int rbd_dev_image_probe(struct rbd_device *rbd_dev, int depth);
static void rbd_spec_put(struct rbd_spec *spec);

static int rbd_dev_id_to_minor(int dev_id)
{
	return dev_id << RBD_SINGLE_MAJOR_PART_SHIFT;
}

static int minor_to_rbd_dev_id(int minor)
{
	return minor >> RBD_SINGLE_MAJOR_PART_SHIFT;
}

static BUS_ATTR(add, S_IWUSR, NULL, rbd_add);
static BUS_ATTR(remove, S_IWUSR, NULL, rbd_remove);
static BUS_ATTR(add_single_major, S_IWUSR, NULL, rbd_add_single_major);
static BUS_ATTR(remove_single_major, S_IWUSR, NULL, rbd_remove_single_major);

static struct attribute *rbd_bus_attrs[] = {
	&bus_attr_add.attr,
	&bus_attr_remove.attr,
	&bus_attr_add_single_major.attr,
	&bus_attr_remove_single_major.attr,
	NULL,
};

static umode_t rbd_bus_is_visible(struct kobject *kobj,
				  struct attribute *attr, int index)
{
	if (!single_major &&
	    (attr == &bus_attr_add_single_major.attr ||
	     attr == &bus_attr_remove_single_major.attr))
		return 0;

	return attr->mode;
}

static const struct attribute_group rbd_bus_group = {
	.attrs = rbd_bus_attrs,
	.is_visible = rbd_bus_is_visible,
};
__ATTRIBUTE_GROUPS(rbd_bus);

static struct bus_type rbd_bus_type = {
	.name		= "rbd",
	.bus_groups	= rbd_bus_groups,
};

static void rbd_root_dev_release(struct device *dev)
{
}

static struct device rbd_root_dev = {
	.init_name =    "rbd",
	.release =      rbd_root_dev_release,
};

static __printf(2, 3)
void rbd_warn(struct rbd_device *rbd_dev, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;

	if (!rbd_dev)
		printk(KERN_WARNING "%s: %pV\n", RBD_DRV_NAME, &vaf);
	else if (rbd_dev->disk)
		printk(KERN_WARNING "%s: %s: %pV\n",
			RBD_DRV_NAME, rbd_dev->disk->disk_name, &vaf);
	else if (rbd_dev->spec && rbd_dev->spec->image_name)
		printk(KERN_WARNING "%s: image %s: %pV\n",
			RBD_DRV_NAME, rbd_dev->spec->image_name, &vaf);
	else if (rbd_dev->spec && rbd_dev->spec->image_id)
		printk(KERN_WARNING "%s: id %s: %pV\n",
			RBD_DRV_NAME, rbd_dev->spec->image_id, &vaf);
	else	/* punt */
		printk(KERN_WARNING "%s: rbd_dev %p: %pV\n",
			RBD_DRV_NAME, rbd_dev, &vaf);
	va_end(args);
}

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

static void rbd_osd_copyup_callback(struct rbd_obj_request *obj_request);
static int rbd_img_obj_request_submit(struct rbd_obj_request *obj_request);
static void rbd_img_parent_read(struct rbd_obj_request *obj_request);
static void rbd_dev_remove_parent(struct rbd_device *rbd_dev);

static int rbd_dev_refresh(struct rbd_device *rbd_dev);
static int rbd_dev_v2_header_onetime(struct rbd_device *rbd_dev);
static int rbd_dev_header_info(struct rbd_device *rbd_dev);
static int rbd_dev_v2_parent_info(struct rbd_device *rbd_dev);
static const char *rbd_dev_v2_snap_name(struct rbd_device *rbd_dev,
					u64 snap_id);
static int _rbd_dev_v2_snap_size(struct rbd_device *rbd_dev, u64 snap_id,
				u8 *order, u64 *snap_size);
static int _rbd_dev_v2_snap_features(struct rbd_device *rbd_dev, u64 snap_id,
		u64 *snap_features);
static u64 rbd_snap_id_by_name(struct rbd_device *rbd_dev, const char *name);

static int rbd_open(struct block_device *bdev, fmode_t mode)
{
	struct rbd_device *rbd_dev = bdev->bd_disk->private_data;
	bool removing = false;

	if ((mode & FMODE_WRITE) && rbd_dev->mapping.read_only)
		return -EROFS;

	spin_lock_irq(&rbd_dev->lock);
	if (test_bit(RBD_DEV_FLAG_REMOVING, &rbd_dev->flags))
		removing = true;
	else
		rbd_dev->open_count++;
	spin_unlock_irq(&rbd_dev->lock);
	if (removing)
		return -ENOENT;

	(void) get_device(&rbd_dev->dev);

	return 0;
}

static void rbd_release(struct gendisk *disk, fmode_t mode)
{
	struct rbd_device *rbd_dev = disk->private_data;
	unsigned long open_count_before;

	spin_lock_irq(&rbd_dev->lock);
	open_count_before = rbd_dev->open_count--;
	spin_unlock_irq(&rbd_dev->lock);
	rbd_assert(open_count_before > 0);

	put_device(&rbd_dev->dev);
}

static int rbd_ioctl_set_ro(struct rbd_device *rbd_dev, unsigned long arg)
{
	int ret = 0;
	int val;
	bool ro;
	bool ro_changed = false;

	/* get_user() may sleep, so call it before taking rbd_dev->lock */
	if (get_user(val, (int __user *)(arg)))
		return -EFAULT;

	ro = val ? true : false;
	/* Snapshot doesn't allow to write*/
	if (rbd_dev->spec->snap_id != CEPH_NOSNAP && !ro)
		return -EROFS;

	spin_lock_irq(&rbd_dev->lock);
	/* prevent others open this device */
	if (rbd_dev->open_count > 1) {
		ret = -EBUSY;
		goto out;
	}

	if (rbd_dev->mapping.read_only != ro) {
		rbd_dev->mapping.read_only = ro;
		ro_changed = true;
	}

out:
	spin_unlock_irq(&rbd_dev->lock);
	/* set_disk_ro() may sleep, so call it after releasing rbd_dev->lock */
	if (ret == 0 && ro_changed)
		set_disk_ro(rbd_dev->disk, ro ? 1 : 0);

	return ret;
}

static int rbd_ioctl(struct block_device *bdev, fmode_t mode,
			unsigned int cmd, unsigned long arg)
{
	struct rbd_device *rbd_dev = bdev->bd_disk->private_data;
	int ret = 0;

	switch (cmd) {
	case BLKROSET:
		ret = rbd_ioctl_set_ro(rbd_dev, arg);
		break;
	default:
		ret = -ENOTTY;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static int rbd_compat_ioctl(struct block_device *bdev, fmode_t mode,
				unsigned int cmd, unsigned long arg)
{
	return rbd_ioctl(bdev, mode, cmd, arg);
}
#endif /* CONFIG_COMPAT */

static const struct block_device_operations rbd_bd_ops = {
	.owner			= THIS_MODULE,
	.open			= rbd_open,
	.release		= rbd_release,
	.ioctl			= rbd_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl		= rbd_compat_ioctl,
#endif
};

/*
 * Initialize an rbd client instance.  Success or not, this function
 * consumes ceph_opts.  Caller holds client_mutex.
 */
static struct rbd_client *rbd_client_create(struct ceph_options *ceph_opts)
{
	struct rbd_client *rbdc;
	int ret = -ENOMEM;

	dout("%s:\n", __func__);
	rbdc = kmalloc(sizeof(struct rbd_client), GFP_KERNEL);
	if (!rbdc)
		goto out_opt;

	kref_init(&rbdc->kref);
	INIT_LIST_HEAD(&rbdc->node);

	rbdc->client = ceph_create_client(ceph_opts, rbdc, 0, 0);
	if (IS_ERR(rbdc->client))
		goto out_rbdc;
	ceph_opts = NULL; /* Now rbdc->client is responsible for ceph_opts */

	ret = ceph_open_session(rbdc->client);
	if (ret < 0)
		goto out_client;

	spin_lock(&rbd_client_list_lock);
	list_add_tail(&rbdc->node, &rbd_client_list);
	spin_unlock(&rbd_client_list_lock);

	dout("%s: rbdc %p\n", __func__, rbdc);

	return rbdc;
out_client:
	ceph_destroy_client(rbdc->client);
out_rbdc:
	kfree(rbdc);
out_opt:
	if (ceph_opts)
		ceph_destroy_options(ceph_opts);
	dout("%s: error %d\n", __func__, ret);

	return ERR_PTR(ret);
}

static struct rbd_client *__rbd_get_client(struct rbd_client *rbdc)
{
	kref_get(&rbdc->kref);

	return rbdc;
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
			__rbd_get_client(client_node);

			found = true;
			break;
		}
	}
	spin_unlock(&rbd_client_list_lock);

	return found ? client_node : NULL;
}

/*
 * (Per device) rbd map options
 */
enum {
	Opt_queue_depth,
	Opt_last_int,
	/* int args above */
	Opt_last_string,
	/* string args above */
	Opt_read_only,
	Opt_read_write,
	Opt_err
};

static match_table_t rbd_opts_tokens = {
	{Opt_queue_depth, "queue_depth=%d"},
	/* int args above */
	/* string args above */
	{Opt_read_only, "read_only"},
	{Opt_read_only, "ro"},		/* Alternate spelling */
	{Opt_read_write, "read_write"},
	{Opt_read_write, "rw"},		/* Alternate spelling */
	{Opt_err, NULL}
};

struct rbd_options {
	int	queue_depth;
	bool	read_only;
};

#define RBD_QUEUE_DEPTH_DEFAULT	BLKDEV_MAX_RQ
#define RBD_READ_ONLY_DEFAULT	false

static int parse_rbd_opts_token(char *c, void *private)
{
	struct rbd_options *rbd_opts = private;
	substring_t argstr[MAX_OPT_ARGS];
	int token, intval, ret;

	token = match_token(c, rbd_opts_tokens, argstr);
	if (token < Opt_last_int) {
		ret = match_int(&argstr[0], &intval);
		if (ret < 0) {
			pr_err("bad mount option arg (not int) at '%s'\n", c);
			return ret;
		}
		dout("got int token %d val %d\n", token, intval);
	} else if (token > Opt_last_int && token < Opt_last_string) {
		dout("got string token %d val %s\n", token, argstr[0].from);
	} else {
		dout("got token %d\n", token);
	}

	switch (token) {
	case Opt_queue_depth:
		if (intval < 1) {
			pr_err("queue_depth out of range\n");
			return -EINVAL;
		}
		rbd_opts->queue_depth = intval;
		break;
	case Opt_read_only:
		rbd_opts->read_only = true;
		break;
	case Opt_read_write:
		rbd_opts->read_only = false;
		break;
	default:
		/* libceph prints "bad option" msg */
		return -EINVAL;
	}

	return 0;
}

static char* obj_op_name(enum obj_operation_type op_type)
{
	switch (op_type) {
	case OBJ_OP_READ:
		return "read";
	case OBJ_OP_WRITE:
		return "write";
	case OBJ_OP_DISCARD:
		return "discard";
	default:
		return "???";
	}
}

/*
 * Get a ceph client with specific addr and configuration, if one does
 * not exist create it.  Either way, ceph_opts is consumed by this
 * function.
 */
static struct rbd_client *rbd_get_client(struct ceph_options *ceph_opts)
{
	struct rbd_client *rbdc;

	mutex_lock_nested(&client_mutex, SINGLE_DEPTH_NESTING);
	rbdc = rbd_client_find(ceph_opts);
	if (rbdc)	/* using an existing client */
		ceph_destroy_options(ceph_opts);
	else
		rbdc = rbd_client_create(ceph_opts);
	mutex_unlock(&client_mutex);

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

	dout("%s: rbdc %p\n", __func__, rbdc);
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
 * Fill an rbd image header with information from the given format 1
 * on-disk header.
 */
static int rbd_header_from_disk(struct rbd_device *rbd_dev,
				 struct rbd_image_header_ondisk *ondisk)
{
	struct rbd_image_header *header = &rbd_dev->header;
	bool first_time = header->object_prefix == NULL;
	struct ceph_snap_context *snapc;
	char *object_prefix = NULL;
	char *snap_names = NULL;
	u64 *snap_sizes = NULL;
	u32 snap_count;
	size_t size;
	int ret = -ENOMEM;
	u32 i;

	/* Allocate this now to avoid having to handle failure below */

	if (first_time) {
		size_t len;

		len = strnlen(ondisk->object_prefix,
				sizeof (ondisk->object_prefix));
		object_prefix = kmalloc(len + 1, GFP_KERNEL);
		if (!object_prefix)
			return -ENOMEM;
		memcpy(object_prefix, ondisk->object_prefix, len);
		object_prefix[len] = '\0';
	}

	/* Allocate the snapshot context and fill it in */

	snap_count = le32_to_cpu(ondisk->snap_count);
	snapc = ceph_create_snap_context(snap_count, GFP_KERNEL);
	if (!snapc)
		goto out_err;
	snapc->seq = le64_to_cpu(ondisk->snap_seq);
	if (snap_count) {
		struct rbd_image_snap_ondisk *snaps;
		u64 snap_names_len = le64_to_cpu(ondisk->snap_names_len);

		/* We'll keep a copy of the snapshot names... */

		if (snap_names_len > (u64)SIZE_MAX)
			goto out_2big;
		snap_names = kmalloc(snap_names_len, GFP_KERNEL);
		if (!snap_names)
			goto out_err;

		/* ...as well as the array of their sizes. */

		size = snap_count * sizeof (*header->snap_sizes);
		snap_sizes = kmalloc(size, GFP_KERNEL);
		if (!snap_sizes)
			goto out_err;

		/*
		 * Copy the names, and fill in each snapshot's id
		 * and size.
		 *
		 * Note that rbd_dev_v1_header_info() guarantees the
		 * ondisk buffer we're working with has
		 * snap_names_len bytes beyond the end of the
		 * snapshot id array, this memcpy() is safe.
		 */
		memcpy(snap_names, &ondisk->snaps[snap_count], snap_names_len);
		snaps = ondisk->snaps;
		for (i = 0; i < snap_count; i++) {
			snapc->snaps[i] = le64_to_cpu(snaps[i].id);
			snap_sizes[i] = le64_to_cpu(snaps[i].image_size);
		}
	}

	/* We won't fail any more, fill in the header */

	if (first_time) {
		header->object_prefix = object_prefix;
		header->obj_order = ondisk->options.order;
		header->crypt_type = ondisk->options.crypt_type;
		header->comp_type = ondisk->options.comp_type;
		/* The rest aren't used for format 1 images */
		header->stripe_unit = 0;
		header->stripe_count = 0;
		header->features = 0;
	} else {
		ceph_put_snap_context(header->snapc);
		kfree(header->snap_names);
		kfree(header->snap_sizes);
	}

	/* The remaining fields always get updated (when we refresh) */

	header->image_size = le64_to_cpu(ondisk->image_size);
	header->snapc = snapc;
	header->snap_names = snap_names;
	header->snap_sizes = snap_sizes;

	return 0;
out_2big:
	ret = -EIO;
out_err:
	kfree(snap_sizes);
	kfree(snap_names);
	ceph_put_snap_context(snapc);
	kfree(object_prefix);

	return ret;
}

static const char *_rbd_dev_v1_snap_name(struct rbd_device *rbd_dev, u32 which)
{
	const char *snap_name;

	rbd_assert(which < rbd_dev->header.snapc->num_snaps);

	/* Skip over names until we find the one we are looking for */

	snap_name = rbd_dev->header.snap_names;
	while (which--)
		snap_name += strlen(snap_name) + 1;

	return kstrdup(snap_name, GFP_KERNEL);
}

/*
 * Snapshot id comparison function for use with qsort()/bsearch().
 * Note that result is for snapshots in *descending* order.
 */
static int snapid_compare_reverse(const void *s1, const void *s2)
{
	u64 snap_id1 = *(u64 *)s1;
	u64 snap_id2 = *(u64 *)s2;

	if (snap_id1 < snap_id2)
		return 1;
	return snap_id1 == snap_id2 ? 0 : -1;
}

/*
 * Search a snapshot context to see if the given snapshot id is
 * present.
 *
 * Returns the position of the snapshot id in the array if it's found,
 * or BAD_SNAP_INDEX otherwise.
 *
 * Note: The snapshot array is in kept sorted (by the osd) in
 * reverse order, highest snapshot id first.
 */
static u32 rbd_dev_snap_index(struct rbd_device *rbd_dev, u64 snap_id)
{
	struct ceph_snap_context *snapc = rbd_dev->header.snapc;
	u64 *found;

	found = bsearch(&snap_id, &snapc->snaps, snapc->num_snaps,
				sizeof (snap_id), snapid_compare_reverse);

	return found ? (u32)(found - &snapc->snaps[0]) : BAD_SNAP_INDEX;
}

static const char *rbd_dev_v1_snap_name(struct rbd_device *rbd_dev,
					u64 snap_id)
{
	u32 which;
	const char *snap_name;

	which = rbd_dev_snap_index(rbd_dev, snap_id);
	if (which == BAD_SNAP_INDEX)
		return ERR_PTR(-ENOENT);

	snap_name = _rbd_dev_v1_snap_name(rbd_dev, which);
	return snap_name ? snap_name : ERR_PTR(-ENOMEM);
}

static const char *rbd_snap_name(struct rbd_device *rbd_dev, u64 snap_id)
{
	if (snap_id == CEPH_NOSNAP)
		return RBD_SNAP_HEAD_NAME;

	rbd_assert(rbd_image_format_valid(rbd_dev->image_format));
	if (rbd_dev->image_format == 1)
		return rbd_dev_v1_snap_name(rbd_dev, snap_id);

	return rbd_dev_v2_snap_name(rbd_dev, snap_id);
}

static int rbd_snap_size(struct rbd_device *rbd_dev, u64 snap_id,
				u64 *snap_size)
{
	rbd_assert(rbd_image_format_valid(rbd_dev->image_format));
	if (snap_id == CEPH_NOSNAP) {
		*snap_size = rbd_dev->header.image_size;
	} else if (rbd_dev->image_format == 1) {
		u32 which;

		which = rbd_dev_snap_index(rbd_dev, snap_id);
		if (which == BAD_SNAP_INDEX)
			return -ENOENT;

		*snap_size = rbd_dev->header.snap_sizes[which];
	} else {
		u64 size = 0;
		int ret;

		ret = _rbd_dev_v2_snap_size(rbd_dev, snap_id, NULL, &size);
		if (ret)
			return ret;

		*snap_size = size;
	}
	return 0;
}

static int rbd_snap_features(struct rbd_device *rbd_dev, u64 snap_id,
			u64 *snap_features)
{
	rbd_assert(rbd_image_format_valid(rbd_dev->image_format));
	if (snap_id == CEPH_NOSNAP) {
		*snap_features = rbd_dev->header.features;
	} else if (rbd_dev->image_format == 1) {
		*snap_features = 0;	/* No features for format 1 */
	} else {
		u64 features = 0;
		int ret;

		ret = _rbd_dev_v2_snap_features(rbd_dev, snap_id, &features);
		if (ret)
			return ret;

		*snap_features = features;
	}
	return 0;
}

static int rbd_dev_mapping_set(struct rbd_device *rbd_dev)
{
	u64 snap_id = rbd_dev->spec->snap_id;
	u64 size = 0;
	u64 features = 0;
	int ret;

	ret = rbd_snap_size(rbd_dev, snap_id, &size);
	if (ret)
		return ret;
	ret = rbd_snap_features(rbd_dev, snap_id, &features);
	if (ret)
		return ret;

	rbd_dev->mapping.size = size;
	rbd_dev->mapping.features = features;

	return 0;
}

static void rbd_dev_mapping_clear(struct rbd_device *rbd_dev)
{
	rbd_dev->mapping.size = 0;
	rbd_dev->mapping.features = 0;
}

static void rbd_segment_name_free(const char *name)
{
	/* The explicit cast here is needed to drop the const qualifier */

	kmem_cache_free(rbd_segment_name_cache, (void *)name);
}

static const char *rbd_segment_name(struct rbd_device *rbd_dev, u64 offset)
{
	char *name;
	u64 segment;
	int ret;
	char *name_format;

	name = kmem_cache_alloc(rbd_segment_name_cache, GFP_NOIO);
	if (!name)
		return NULL;
	segment = offset >> rbd_dev->header.obj_order;
	name_format = "%s.%012llx";
	if (rbd_dev->image_format == 2)
		name_format = "%s.%016llx";
	ret = snprintf(name, CEPH_MAX_OID_NAME_LEN + 1, name_format,
			rbd_dev->header.object_prefix, segment);
	if (ret < 0 || ret > CEPH_MAX_OID_NAME_LEN) {
		pr_err("error formatting segment name for #%llu (%d)\n",
			segment, ret);
		rbd_segment_name_free(name);
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
	struct bio_vec bv;
	struct bvec_iter iter;
	unsigned long flags;
	void *buf;
	int pos = 0;

	while (chain) {
		bio_for_each_segment(bv, chain, iter) {
			if (pos + bv.bv_len > start_ofs) {
				int remainder = max(start_ofs - pos, 0);
				buf = bvec_kmap_irq(&bv, &flags);
				memset(buf + remainder, 0,
				       bv.bv_len - remainder);
				flush_dcache_page(bv.bv_page);
				bvec_kunmap_irq(buf, &flags);
			}
			pos += bv.bv_len;
		}

		chain = chain->bi_next;
	}
}

/*
 * similar to zero_bio_chain(), zeros data defined by a page array,
 * starting at the given byte offset from the start of the array and
 * continuing up to the given end offset.  The pages array is
 * assumed to be big enough to hold all bytes up to the end.
 */
static void zero_pages(struct page **pages, u64 offset, u64 end)
{
	struct page **page = &pages[offset >> PAGE_SHIFT];

	rbd_assert(end > offset);
	rbd_assert(end - offset <= (u64)SIZE_MAX);
	while (offset < end) {
		size_t page_offset;
		size_t length;
		unsigned long flags;
		void *kaddr;

		page_offset = offset & ~PAGE_MASK;
		length = min_t(size_t, PAGE_SIZE - page_offset, end - offset);
		local_irq_save(flags);
		kaddr = kmap_atomic(*page);
		memset(kaddr + page_offset, 0, length);
		flush_dcache_page(*page);
		kunmap_atomic(kaddr);
		local_irq_restore(flags);

		offset += length;
		page++;
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
	struct bio *bio;

	bio = bio_clone(bio_src, gfpmask);
	if (!bio)
		return NULL;	/* ENOMEM */

	bio_advance(bio, offset);
	bio->bi_iter.bi_size = len;

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

	if (!bi || off >= bi->bi_iter.bi_size || !len)
		return NULL;		/* Nothing to clone */

	end = &chain;
	while (len) {
		unsigned int bi_size;
		struct bio *bio;

		if (!bi) {
			rbd_warn(NULL, "bio_chain exhausted with %u left", len);
			goto out_err;	/* EINVAL; ran out of bio's */
		}
		bi_size = min_t(unsigned int, bi->bi_iter.bi_size - off, len);
		bio = bio_clone_range(bi, off, bi_size, gfpmask);
		if (!bio)
			goto out_err;	/* ENOMEM */

		*end = bio;
		end = &bio->bi_next;

		off += bi_size;
		if (off == bi->bi_iter.bi_size) {
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
 * The default/initial value for all object request flags is 0.  For
 * each flag, once its value is set to 1 it is never reset to 0
 * again.
 */
static void obj_request_img_data_set(struct rbd_obj_request *obj_request)
{
	if (test_and_set_bit(OBJ_REQ_IMG_DATA, &obj_request->flags)) {
		struct rbd_device *rbd_dev;

		rbd_dev = obj_request->img_request->rbd_dev;
		rbd_warn(rbd_dev, "obj_request %p already marked img_data",
			obj_request);
	}
}

static bool obj_request_img_data_test(struct rbd_obj_request *obj_request)
{
	smp_mb();
	return test_bit(OBJ_REQ_IMG_DATA, &obj_request->flags) != 0;
}

static void obj_request_done_set(struct rbd_obj_request *obj_request)
{
	if (test_and_set_bit(OBJ_REQ_DONE, &obj_request->flags)) {
		struct rbd_device *rbd_dev = NULL;

		if (obj_request_img_data_test(obj_request))
			rbd_dev = obj_request->img_request->rbd_dev;
		rbd_warn(rbd_dev, "obj_request %p already marked done",
			obj_request);
	}
}

static bool obj_request_done_test(struct rbd_obj_request *obj_request)
{
	smp_mb();
	return test_bit(OBJ_REQ_DONE, &obj_request->flags) != 0;
}

/*
 * This sets the KNOWN flag after (possibly) setting the EXISTS
 * flag.  The latter is set based on the "exists" value provided.
 *
 * Note that for our purposes once an object exists it never goes
 * away again.  It's possible that the response from two existence
 * checks are separated by the creation of the target object, and
 * the first ("doesn't exist") response arrives *after* the second
 * ("does exist").  In that case we ignore the second one.
 */
static void obj_request_existence_set(struct rbd_obj_request *obj_request,
				bool exists)
{
	if (exists)
		set_bit(OBJ_REQ_EXISTS, &obj_request->flags);
	set_bit(OBJ_REQ_KNOWN, &obj_request->flags);
	smp_mb();
}

static bool obj_request_known_test(struct rbd_obj_request *obj_request)
{
	smp_mb();
	return test_bit(OBJ_REQ_KNOWN, &obj_request->flags) != 0;
}

static bool obj_request_exists_test(struct rbd_obj_request *obj_request)
{
	smp_mb();
	return test_bit(OBJ_REQ_EXISTS, &obj_request->flags) != 0;
}

static bool obj_request_overlaps_parent(struct rbd_obj_request *obj_request)
{
	struct rbd_device *rbd_dev = obj_request->img_request->rbd_dev;

	return obj_request->img_offset <
	    round_up(rbd_dev->parent_overlap, rbd_obj_bytes(&rbd_dev->header));
}

static void rbd_obj_request_get(struct rbd_obj_request *obj_request)
{
	dout("%s: obj %p (was %d)\n", __func__, obj_request,
		atomic_read(&obj_request->kref.refcount));
	kref_get(&obj_request->kref);
}

static void rbd_obj_request_destroy(struct kref *kref);
static void rbd_obj_request_put(struct rbd_obj_request *obj_request)
{
	rbd_assert(obj_request != NULL);
	dout("%s: obj %p (was %d)\n", __func__, obj_request,
		atomic_read(&obj_request->kref.refcount));
	kref_put(&obj_request->kref, rbd_obj_request_destroy);
}

static void rbd_img_request_get(struct rbd_img_request *img_request)
{
	dout("%s: img %p (was %d)\n", __func__, img_request,
	     atomic_read(&img_request->kref.refcount));
	kref_get(&img_request->kref);
}

static bool img_request_child_test(struct rbd_img_request *img_request);
static void rbd_parent_request_destroy(struct kref *kref);
static void rbd_img_request_destroy(struct kref *kref);
static void rbd_img_request_put(struct rbd_img_request *img_request)
{
	rbd_assert(img_request != NULL);
	dout("%s: img %p (was %d)\n", __func__, img_request,
		atomic_read(&img_request->kref.refcount));
	if (img_request_child_test(img_request))
		kref_put(&img_request->kref, rbd_parent_request_destroy);
	else
		kref_put(&img_request->kref, rbd_img_request_destroy);
}

static inline void rbd_img_obj_request_add(struct rbd_img_request *img_request,
					struct rbd_obj_request *obj_request)
{
	rbd_assert(obj_request->img_request == NULL);

	/* Image request now owns object's original reference */
	obj_request->img_request = img_request;
	obj_request->which = img_request->obj_request_count;
	rbd_assert(!obj_request_img_data_test(obj_request));
	obj_request_img_data_set(obj_request);
	rbd_assert(obj_request->which != BAD_WHICH);
	img_request->obj_request_count++;
	list_add_tail(&obj_request->links, &img_request->obj_requests);
	dout("%s: img %p obj %p w=%u\n", __func__, img_request, obj_request,
		obj_request->which);
}

static inline void rbd_img_obj_request_del(struct rbd_img_request *img_request,
					struct rbd_obj_request *obj_request)
{
	rbd_assert(obj_request->which != BAD_WHICH);

	dout("%s: img %p obj %p w=%u\n", __func__, img_request, obj_request,
		obj_request->which);
	list_del(&obj_request->links);
	rbd_assert(img_request->obj_request_count > 0);
	img_request->obj_request_count--;
	rbd_assert(obj_request->which == img_request->obj_request_count);
	obj_request->which = BAD_WHICH;
	rbd_assert(obj_request_img_data_test(obj_request));
	rbd_assert(obj_request->img_request == img_request);
	obj_request->img_request = NULL;
	obj_request->callback = NULL;
	rbd_obj_request_put(obj_request);
}

static bool obj_request_type_valid(enum obj_request_type type)
{
	switch (type) {
	case OBJ_REQUEST_NODATA:
	case OBJ_REQUEST_BIO:
	case OBJ_REQUEST_PAGES:
		return true;
	default:
		return false;
	}
}

static int rbd_obj_request_submit(struct ceph_osd_client *osdc,
				struct rbd_obj_request *obj_request)
{
	dout("%s %p\n", __func__, obj_request);
	return ceph_osdc_start_request(osdc, obj_request->osd_req, false);
}

static void rbd_obj_request_end(struct rbd_obj_request *obj_request)
{
	dout("%s %p\n", __func__, obj_request);
	ceph_osdc_cancel_request(obj_request->osd_req);
}

/*
 * Wait for an object request to complete.  If interrupted, cancel the
 * underlying osd request.
 *
 * @timeout: in jiffies, 0 means "wait forever"
 */
static int __rbd_obj_request_wait(struct rbd_obj_request *obj_request,
				  unsigned long timeout)
{
	long ret;

	dout("%s %p\n", __func__, obj_request);
	ret = wait_for_completion_interruptible_timeout(
					&obj_request->completion,
					ceph_timeout_jiffies(timeout));
	if (ret <= 0) {
		if (ret == 0)
			ret = -ETIMEDOUT;
		rbd_obj_request_end(obj_request);
	} else {
		ret = 0;
	}

	dout("%s %p ret %d\n", __func__, obj_request, (int)ret);
	return ret;
}

static int rbd_obj_request_wait(struct rbd_obj_request *obj_request)
{
	return __rbd_obj_request_wait(obj_request, 0);
}

static int rbd_obj_request_wait_timeout(struct rbd_obj_request *obj_request,
					unsigned long timeout)
{
	return __rbd_obj_request_wait(obj_request, timeout);
}

static void rbd_img_request_complete(struct rbd_img_request *img_request)
{

	dout("%s: img %p\n", __func__, img_request);

	/*
	 * If no error occurred, compute the aggregate transfer
	 * count for the image request.  We could instead use
	 * atomic64_cmpxchg() to update it as each object request
	 * completes; not clear which way is better off hand.
	 */
	if (!img_request->result) {
		struct rbd_obj_request *obj_request;
		u64 xferred = 0;

		for_each_obj_request(img_request, obj_request)
			xferred += obj_request->xferred;
		img_request->xferred = xferred;
	}

	if (img_request->callback)
		img_request->callback(img_request);
	else
		rbd_img_request_put(img_request);
}

/*
 * The default/initial value for all image request flags is 0.  Each
 * is conditionally set to 1 at image request initialization time
 * and currently never change thereafter.
 */
static void img_request_write_set(struct rbd_img_request *img_request)
{
	set_bit(IMG_REQ_WRITE, &img_request->flags);
	smp_mb();
}

static bool img_request_write_test(struct rbd_img_request *img_request)
{
	smp_mb();
	return test_bit(IMG_REQ_WRITE, &img_request->flags) != 0;
}

/*
 * Set the discard flag when the img_request is an discard request
 */
static void img_request_discard_set(struct rbd_img_request *img_request)
{
	set_bit(IMG_REQ_DISCARD, &img_request->flags);
	smp_mb();
}

static bool img_request_discard_test(struct rbd_img_request *img_request)
{
	smp_mb();
	return test_bit(IMG_REQ_DISCARD, &img_request->flags) != 0;
}

static void img_request_child_set(struct rbd_img_request *img_request)
{
	set_bit(IMG_REQ_CHILD, &img_request->flags);
	smp_mb();
}

static void img_request_child_clear(struct rbd_img_request *img_request)
{
	clear_bit(IMG_REQ_CHILD, &img_request->flags);
	smp_mb();
}

static bool img_request_child_test(struct rbd_img_request *img_request)
{
	smp_mb();
	return test_bit(IMG_REQ_CHILD, &img_request->flags) != 0;
}

static void img_request_layered_set(struct rbd_img_request *img_request)
{
	set_bit(IMG_REQ_LAYERED, &img_request->flags);
	smp_mb();
}

static void img_request_layered_clear(struct rbd_img_request *img_request)
{
	clear_bit(IMG_REQ_LAYERED, &img_request->flags);
	smp_mb();
}

static bool img_request_layered_test(struct rbd_img_request *img_request)
{
	smp_mb();
	return test_bit(IMG_REQ_LAYERED, &img_request->flags) != 0;
}

static enum obj_operation_type
rbd_img_request_op_type(struct rbd_img_request *img_request)
{
	if (img_request_write_test(img_request))
		return OBJ_OP_WRITE;
	else if (img_request_discard_test(img_request))
		return OBJ_OP_DISCARD;
	else
		return OBJ_OP_READ;
}

static void
rbd_img_obj_request_read_callback(struct rbd_obj_request *obj_request)
{
	u64 xferred = obj_request->xferred;
	u64 length = obj_request->length;

	dout("%s: obj %p img %p result %d %llu/%llu\n", __func__,
		obj_request, obj_request->img_request, obj_request->result,
		xferred, length);
	/*
	 * ENOENT means a hole in the image.  We zero-fill the entire
	 * length of the request.  A short read also implies zero-fill
	 * to the end of the request.  An error requires the whole
	 * length of the request to be reported finished with an error
	 * to the block layer.  In each case we update the xferred
	 * count to indicate the whole request was satisfied.
	 */
	rbd_assert(obj_request->type != OBJ_REQUEST_NODATA);
	if (obj_request->result == -ENOENT) {
		if (obj_request->type == OBJ_REQUEST_BIO)
			zero_bio_chain(obj_request->bio_list, 0);
		else
			zero_pages(obj_request->pages, 0, length);
		obj_request->result = 0;
	} else if (xferred < length && !obj_request->result) {
		if (obj_request->type == OBJ_REQUEST_BIO)
			zero_bio_chain(obj_request->bio_list, xferred);
		else
			zero_pages(obj_request->pages, xferred, length);
	}
	obj_request->xferred = length;
	obj_request_done_set(obj_request);
}

static void rbd_obj_request_complete(struct rbd_obj_request *obj_request)
{
	dout("%s: obj %p cb %p\n", __func__, obj_request,
		obj_request->callback);
	if (obj_request->callback)
		obj_request->callback(obj_request);
	else
		complete_all(&obj_request->completion);
}

static void rbd_osd_trivial_callback(struct rbd_obj_request *obj_request)
{
	dout("%s: obj %p\n", __func__, obj_request);
	obj_request_done_set(obj_request);
}

static void rbd_osd_read_callback(struct rbd_obj_request *obj_request)
{
	struct rbd_img_request *img_request = NULL;
	struct rbd_device *rbd_dev = NULL;
	bool layered = false;

	if (obj_request_img_data_test(obj_request)) {
		img_request = obj_request->img_request;
		layered = img_request && img_request_layered_test(img_request);
		rbd_dev = img_request->rbd_dev;
	}

	dout("%s: obj %p img %p result %d %llu/%llu\n", __func__,
		obj_request, img_request, obj_request->result,
		obj_request->xferred, obj_request->length);
	if (layered && obj_request->result == -ENOENT &&
			obj_request->img_offset < rbd_dev->parent_overlap)
		rbd_img_parent_read(obj_request);
	else if (img_request)
		rbd_img_obj_request_read_callback(obj_request);
	else
		obj_request_done_set(obj_request);
}

static void rbd_osd_write_callback(struct rbd_obj_request *obj_request)
{
	dout("%s: obj %p result %d %llu\n", __func__, obj_request,
		obj_request->result, obj_request->length);
	/*
	 * There is no such thing as a successful short write.  Set
	 * it to our originally-requested length.
	 */
	obj_request->xferred = obj_request->length;
	obj_request_done_set(obj_request);
}

static void rbd_osd_discard_callback(struct rbd_obj_request *obj_request)
{
	dout("%s: obj %p result %d %llu\n", __func__, obj_request,
		obj_request->result, obj_request->length);
	/*
	 * There is no such thing as a successful short discard.  Set
	 * it to our originally-requested length.
	 */
	obj_request->xferred = obj_request->length;
	/* discarding a non-existent object is not a problem */
	if (obj_request->result == -ENOENT)
		obj_request->result = 0;
	obj_request_done_set(obj_request);
}

/*
 * For a simple stat call there's nothing to do.  We'll do more if
 * this is part of a write sequence for a layered image.
 */
static void rbd_osd_stat_callback(struct rbd_obj_request *obj_request)
{
	dout("%s: obj %p\n", __func__, obj_request);
	obj_request_done_set(obj_request);
}

static void rbd_osd_call_callback(struct rbd_obj_request *obj_request)
{
	dout("%s: obj %p\n", __func__, obj_request);

	if (obj_request_img_data_test(obj_request))
		rbd_osd_copyup_callback(obj_request);
	else
		obj_request_done_set(obj_request);
}

static void rbd_osd_req_callback(struct ceph_osd_request *osd_req,
				struct ceph_msg *msg)
{
	struct rbd_obj_request *obj_request = osd_req->r_priv;
	u16 opcode;

	dout("%s: osd_req %p msg %p\n", __func__, osd_req, msg);
	rbd_assert(osd_req == obj_request->osd_req);
	if (obj_request_img_data_test(obj_request)) {
		rbd_assert(obj_request->img_request);
		rbd_assert(obj_request->which != BAD_WHICH);
	} else {
		rbd_assert(obj_request->which == BAD_WHICH);
	}

	if (osd_req->r_result < 0)
		obj_request->result = osd_req->r_result;

	rbd_assert(osd_req->r_num_ops <= CEPH_OSD_MAX_OP);

	/*
	 * We support a 64-bit length, but ultimately it has to be
	 * passed to the block layer, which just supports a 32-bit
	 * length field.
	 */
	obj_request->xferred = osd_req->r_reply_op_len[0];
	rbd_assert(obj_request->xferred < (u64)UINT_MAX);

	opcode = osd_req->r_ops[0].op;
	switch (opcode) {
	case CEPH_OSD_OP_READ:
		rbd_osd_read_callback(obj_request);
		break;
	case CEPH_OSD_OP_SETALLOCHINT:
		rbd_assert(osd_req->r_ops[1].op == CEPH_OSD_OP_WRITE ||
			   osd_req->r_ops[1].op == CEPH_OSD_OP_WRITEFULL);
		/* fall through */
	case CEPH_OSD_OP_WRITE:
	case CEPH_OSD_OP_WRITEFULL:
		rbd_osd_write_callback(obj_request);
		break;
	case CEPH_OSD_OP_STAT:
		rbd_osd_stat_callback(obj_request);
		break;
	case CEPH_OSD_OP_DELETE:
	case CEPH_OSD_OP_TRUNCATE:
	case CEPH_OSD_OP_ZERO:
		rbd_osd_discard_callback(obj_request);
		break;
	case CEPH_OSD_OP_CALL:
		rbd_osd_call_callback(obj_request);
		break;
	case CEPH_OSD_OP_NOTIFY_ACK:
	case CEPH_OSD_OP_WATCH:
		rbd_osd_trivial_callback(obj_request);
		break;
	default:
		rbd_warn(NULL, "%s: unsupported op %hu",
			obj_request->object_name, (unsigned short) opcode);
		break;
	}

	if (obj_request_done_test(obj_request))
		rbd_obj_request_complete(obj_request);
}

static void rbd_osd_req_format_read(struct rbd_obj_request *obj_request)
{
	struct rbd_img_request *img_request = obj_request->img_request;
	struct ceph_osd_request *osd_req = obj_request->osd_req;
	u64 snap_id;

	rbd_assert(osd_req != NULL);

	snap_id = img_request ? img_request->snap_id : CEPH_NOSNAP;
	ceph_osdc_build_request(osd_req, obj_request->offset,
			NULL, snap_id, NULL);
}

static void rbd_osd_req_format_write(struct rbd_obj_request *obj_request)
{
	struct rbd_img_request *img_request = obj_request->img_request;
	struct ceph_osd_request *osd_req = obj_request->osd_req;
	struct ceph_snap_context *snapc;
	struct timespec mtime = CURRENT_TIME;

	rbd_assert(osd_req != NULL);

	snapc = img_request ? img_request->snapc : NULL;
	ceph_osdc_build_request(osd_req, obj_request->offset,
			snapc, CEPH_NOSNAP, &mtime);
}

/*
 * Create an osd request.  A read request has one osd op (read).
 * A write request has either one (watch) or two (hint+write) osd ops.
 * (All rbd data writes are prefixed with an allocation hint op, but
 * technically osd watch is a write request, hence this distinction.)
 */
static struct ceph_osd_request *rbd_osd_req_create(
					struct rbd_device *rbd_dev,
					enum obj_operation_type op_type,
					unsigned int num_ops,
					struct rbd_obj_request *obj_request)
{
	struct ceph_snap_context *snapc = NULL;
	struct ceph_osd_client *osdc;
	struct ceph_osd_request *osd_req;

	if (obj_request_img_data_test(obj_request) &&
		(op_type == OBJ_OP_DISCARD || op_type == OBJ_OP_WRITE)) {
		struct rbd_img_request *img_request = obj_request->img_request;
		if (op_type == OBJ_OP_WRITE) {
			rbd_assert(img_request_write_test(img_request));
		} else {
			rbd_assert(img_request_discard_test(img_request));
		}
		snapc = img_request->snapc;
	}

	rbd_assert(num_ops == 1 || ((op_type == OBJ_OP_WRITE) && num_ops == 2));

	/* Allocate and initialize the request, for the num_ops ops */

	osdc = &rbd_dev->rbd_client->client->osdc;
	osd_req = ceph_osdc_alloc_request(osdc, snapc, num_ops, false,
					  GFP_NOIO);
	if (!osd_req)
		return NULL;	/* ENOMEM */

	if (op_type == OBJ_OP_WRITE || op_type == OBJ_OP_DISCARD)
		osd_req->r_flags = CEPH_OSD_FLAG_WRITE | CEPH_OSD_FLAG_ONDISK;
	else
		osd_req->r_flags = CEPH_OSD_FLAG_READ;

	osd_req->r_callback = rbd_osd_req_callback;
	osd_req->r_priv = obj_request;

	osd_req->r_base_oloc.pool = ceph_file_layout_pg_pool(rbd_dev->layout);
	ceph_oid_set_name(&osd_req->r_base_oid, obj_request->object_name);

	return osd_req;
}

/*
 * Create a copyup osd request based on the information in the object
 * request supplied.  A copyup request has two or three osd ops, a
 * copyup method call, potentially a hint op, and a write or truncate
 * or zero op.
 */
static struct ceph_osd_request *
rbd_osd_req_create_copyup(struct rbd_obj_request *obj_request)
{
	struct rbd_img_request *img_request;
	struct ceph_snap_context *snapc;
	struct rbd_device *rbd_dev;
	struct ceph_osd_client *osdc;
	struct ceph_osd_request *osd_req;
	int num_osd_ops = 3;

	rbd_assert(obj_request_img_data_test(obj_request));
	img_request = obj_request->img_request;
	rbd_assert(img_request);
	rbd_assert(img_request_write_test(img_request) ||
			img_request_discard_test(img_request));

	if (img_request_discard_test(img_request))
		num_osd_ops = 2;

	/* Allocate and initialize the request, for all the ops */

	snapc = img_request->snapc;
	rbd_dev = img_request->rbd_dev;
	osdc = &rbd_dev->rbd_client->client->osdc;
	osd_req = ceph_osdc_alloc_request(osdc, snapc, num_osd_ops,
						false, GFP_NOIO);
	if (!osd_req)
		return NULL;	/* ENOMEM */

	osd_req->r_flags = CEPH_OSD_FLAG_WRITE | CEPH_OSD_FLAG_ONDISK;
	osd_req->r_callback = rbd_osd_req_callback;
	osd_req->r_priv = obj_request;

	osd_req->r_base_oloc.pool = ceph_file_layout_pg_pool(rbd_dev->layout);
	ceph_oid_set_name(&osd_req->r_base_oid, obj_request->object_name);

	return osd_req;
}


static void rbd_osd_req_destroy(struct ceph_osd_request *osd_req)
{
	ceph_osdc_put_request(osd_req);
}

/* object_name is assumed to be a non-null pointer and NUL-terminated */

static struct rbd_obj_request *rbd_obj_request_create(const char *object_name,
						u64 offset, u64 length,
						enum obj_request_type type)
{
	struct rbd_obj_request *obj_request;
	size_t size;
	char *name;

	rbd_assert(obj_request_type_valid(type));

	size = strlen(object_name) + 1;
	name = kmalloc(size, GFP_NOIO);
	if (!name)
		return NULL;

	obj_request = kmem_cache_zalloc(rbd_obj_request_cache, GFP_NOIO);
	if (!obj_request) {
		kfree(name);
		return NULL;
	}

	obj_request->object_name = memcpy(name, object_name, size);
	obj_request->offset = offset;
	obj_request->length = length;
	obj_request->flags = 0;
	obj_request->which = BAD_WHICH;
	obj_request->type = type;
	INIT_LIST_HEAD(&obj_request->links);
	init_completion(&obj_request->completion);
	kref_init(&obj_request->kref);

	dout("%s: \"%s\" %llu/%llu %d -> obj %p\n", __func__, object_name,
		offset, length, (int)type, obj_request);

	return obj_request;
}

static void rbd_obj_request_destroy(struct kref *kref)
{
	struct rbd_obj_request *obj_request;

	obj_request = container_of(kref, struct rbd_obj_request, kref);

	dout("%s: obj %p\n", __func__, obj_request);

	rbd_assert(obj_request->img_request == NULL);
	rbd_assert(obj_request->which == BAD_WHICH);

	if (obj_request->osd_req)
		rbd_osd_req_destroy(obj_request->osd_req);

	rbd_assert(obj_request_type_valid(obj_request->type));
	switch (obj_request->type) {
	case OBJ_REQUEST_NODATA:
		break;		/* Nothing to do */
	case OBJ_REQUEST_BIO:
		if (obj_request->bio_list)
			bio_chain_put(obj_request->bio_list);
		break;
	case OBJ_REQUEST_PAGES:
		if (obj_request->pages)
			ceph_release_page_vector(obj_request->pages,
						obj_request->page_count);
		break;
	}

	kfree(obj_request->object_name);
	obj_request->object_name = NULL;
	kmem_cache_free(rbd_obj_request_cache, obj_request);
}

/* It's OK to call this for a device with no parent */

static void rbd_spec_put(struct rbd_spec *spec);
static void rbd_dev_unparent(struct rbd_device *rbd_dev)
{
	rbd_dev_remove_parent(rbd_dev);
	rbd_spec_put(rbd_dev->parent_spec);
	rbd_dev->parent_spec = NULL;
	rbd_dev->parent_overlap = 0;
}

/*
 * Parent image reference counting is used to determine when an
 * image's parent fields can be safely torn down--after there are no
 * more in-flight requests to the parent image.  When the last
 * reference is dropped, cleaning them up is safe.
 */
static void rbd_dev_parent_put(struct rbd_device *rbd_dev)
{
	int counter;

	if (!rbd_dev->parent_spec)
		return;

	counter = atomic_dec_return_safe(&rbd_dev->parent_ref);
	if (counter > 0)
		return;

	/* Last reference; clean up parent data structures */

	if (!counter)
		rbd_dev_unparent(rbd_dev);
	else
		rbd_warn(rbd_dev, "parent reference underflow");
}

/*
 * If an image has a non-zero parent overlap, get a reference to its
 * parent.
 *
 * Returns true if the rbd device has a parent with a non-zero
 * overlap and a reference for it was successfully taken, or
 * false otherwise.
 */
static bool rbd_dev_parent_get(struct rbd_device *rbd_dev)
{
	int counter = 0;

	if (!rbd_dev->parent_spec)
		return false;

	down_read(&rbd_dev->header_rwsem);
	if (rbd_dev->parent_overlap)
		counter = atomic_inc_return_safe(&rbd_dev->parent_ref);
	up_read(&rbd_dev->header_rwsem);

	if (counter < 0)
		rbd_warn(rbd_dev, "parent reference overflow");

	return counter > 0;
}

/*
 * Caller is responsible for filling in the list of object requests
 * that comprises the image request, and the Linux request pointer
 * (if there is one).
 */
static struct rbd_img_request *rbd_img_request_create(
					struct rbd_device *rbd_dev,
					u64 offset, u64 length,
					enum obj_operation_type op_type,
					struct ceph_snap_context *snapc)
{
	struct rbd_img_request *img_request;

	img_request = kmem_cache_alloc(rbd_img_request_cache, GFP_NOIO);
	if (!img_request)
		return NULL;

	img_request->rq = NULL;
	img_request->rbd_dev = rbd_dev;
	img_request->offset = offset;
	img_request->length = length;
	img_request->flags = 0;
	if (op_type == OBJ_OP_DISCARD) {
		img_request_discard_set(img_request);
		img_request->snapc = snapc;
	} else if (op_type == OBJ_OP_WRITE) {
		img_request_write_set(img_request);
		img_request->snapc = snapc;
	} else {
		img_request->snap_id = rbd_dev->spec->snap_id;
	}
	if (rbd_dev_parent_get(rbd_dev))
		img_request_layered_set(img_request);
	spin_lock_init(&img_request->completion_lock);
	img_request->next_completion = 0;
	img_request->callback = NULL;
	img_request->result = 0;
	img_request->obj_request_count = 0;
	INIT_LIST_HEAD(&img_request->obj_requests);
	kref_init(&img_request->kref);

	dout("%s: rbd_dev %p %s %llu/%llu -> img %p\n", __func__, rbd_dev,
		obj_op_name(op_type), offset, length, img_request);

	return img_request;
}

static void rbd_img_request_destroy(struct kref *kref)
{
	struct rbd_img_request *img_request;
	struct rbd_obj_request *obj_request;
	struct rbd_obj_request *next_obj_request;

	img_request = container_of(kref, struct rbd_img_request, kref);

	dout("%s: img %p\n", __func__, img_request);

	for_each_obj_request_safe(img_request, obj_request, next_obj_request)
		rbd_img_obj_request_del(img_request, obj_request);
	rbd_assert(img_request->obj_request_count == 0);

	if (img_request_layered_test(img_request)) {
		img_request_layered_clear(img_request);
		rbd_dev_parent_put(img_request->rbd_dev);
	}

	if (img_request_write_test(img_request) ||
		img_request_discard_test(img_request))
		ceph_put_snap_context(img_request->snapc);

	kmem_cache_free(rbd_img_request_cache, img_request);
}

static struct rbd_img_request *rbd_parent_request_create(
					struct rbd_obj_request *obj_request,
					u64 img_offset, u64 length)
{
	struct rbd_img_request *parent_request;
	struct rbd_device *rbd_dev;

	rbd_assert(obj_request->img_request);
	rbd_dev = obj_request->img_request->rbd_dev;

	parent_request = rbd_img_request_create(rbd_dev->parent, img_offset,
						length, OBJ_OP_READ, NULL);
	if (!parent_request)
		return NULL;

	img_request_child_set(parent_request);
	rbd_obj_request_get(obj_request);
	parent_request->obj_request = obj_request;

	return parent_request;
}

static void rbd_parent_request_destroy(struct kref *kref)
{
	struct rbd_img_request *parent_request;
	struct rbd_obj_request *orig_request;

	parent_request = container_of(kref, struct rbd_img_request, kref);
	orig_request = parent_request->obj_request;

	parent_request->obj_request = NULL;
	rbd_obj_request_put(orig_request);
	img_request_child_clear(parent_request);

	rbd_img_request_destroy(kref);
}

static bool rbd_img_obj_end_request(struct rbd_obj_request *obj_request)
{
	struct rbd_img_request *img_request;
	unsigned int xferred;
	int result;
	bool more;

	rbd_assert(obj_request_img_data_test(obj_request));
	img_request = obj_request->img_request;

	rbd_assert(obj_request->xferred <= (u64)UINT_MAX);
	xferred = (unsigned int)obj_request->xferred;
	result = obj_request->result;
	if (result) {
		struct rbd_device *rbd_dev = img_request->rbd_dev;
		enum obj_operation_type op_type;

		if (img_request_discard_test(img_request))
			op_type = OBJ_OP_DISCARD;
		else if (img_request_write_test(img_request))
			op_type = OBJ_OP_WRITE;
		else
			op_type = OBJ_OP_READ;

		rbd_warn(rbd_dev, "%s %llx at %llx (%llx)",
			obj_op_name(op_type), obj_request->length,
			obj_request->img_offset, obj_request->offset);
		rbd_warn(rbd_dev, "  result %d xferred %x",
			result, xferred);
		if (!img_request->result)
			img_request->result = result;
		/*
		 * Need to end I/O on the entire obj_request worth of
		 * bytes in case of error.
		 */
		xferred = obj_request->length;
	}

	/* Image object requests don't own their page array */

	if (obj_request->type == OBJ_REQUEST_PAGES) {
		obj_request->pages = NULL;
		obj_request->page_count = 0;
	}

	if (img_request_child_test(img_request)) {
		rbd_assert(img_request->obj_request != NULL);
		more = obj_request->which < img_request->obj_request_count - 1;
	} else {
		rbd_assert(img_request->rq != NULL);

		more = blk_update_request(img_request->rq, result, xferred);
		if (!more)
			__blk_mq_end_request(img_request->rq, result);
	}

	return more;
}

static void rbd_img_obj_callback(struct rbd_obj_request *obj_request)
{
	struct rbd_img_request *img_request;
	u32 which = obj_request->which;
	bool more = true;

	rbd_assert(obj_request_img_data_test(obj_request));
	img_request = obj_request->img_request;

	dout("%s: img %p obj %p\n", __func__, img_request, obj_request);
	rbd_assert(img_request != NULL);
	rbd_assert(img_request->obj_request_count > 0);
	rbd_assert(which != BAD_WHICH);
	rbd_assert(which < img_request->obj_request_count);

	spin_lock_irq(&img_request->completion_lock);
	if (which != img_request->next_completion)
		goto out;

	for_each_obj_request_from(img_request, obj_request) {
		rbd_assert(more);
		rbd_assert(which < img_request->obj_request_count);

		if (!obj_request_done_test(obj_request))
			break;
		more = rbd_img_obj_end_request(obj_request);
		which++;
	}

	rbd_assert(more ^ (which == img_request->obj_request_count));
	img_request->next_completion = which;
out:
	spin_unlock_irq(&img_request->completion_lock);
	rbd_img_request_put(img_request);

	if (!more)
		rbd_img_request_complete(img_request);
}

/*
 * Add individual osd ops to the given ceph_osd_request and prepare
 * them for submission. num_ops is the current number of
 * osd operations already to the object request.
 */
static void rbd_img_obj_request_fill(struct rbd_obj_request *obj_request,
				struct ceph_osd_request *osd_request,
				enum obj_operation_type op_type,
				unsigned int num_ops)
{
	struct rbd_img_request *img_request = obj_request->img_request;
	struct rbd_device *rbd_dev = img_request->rbd_dev;
	u64 object_size = rbd_obj_bytes(&rbd_dev->header);
	u64 offset = obj_request->offset;
	u64 length = obj_request->length;
	u64 img_end;
	u16 opcode;

	if (op_type == OBJ_OP_DISCARD) {
		if (!offset && length == object_size &&
		    (!img_request_layered_test(img_request) ||
		     !obj_request_overlaps_parent(obj_request))) {
			opcode = CEPH_OSD_OP_DELETE;
		} else if ((offset + length == object_size)) {
			opcode = CEPH_OSD_OP_TRUNCATE;
		} else {
			down_read(&rbd_dev->header_rwsem);
			img_end = rbd_dev->header.image_size;
			up_read(&rbd_dev->header_rwsem);

			if (obj_request->img_offset + length == img_end)
				opcode = CEPH_OSD_OP_TRUNCATE;
			else
				opcode = CEPH_OSD_OP_ZERO;
		}
	} else if (op_type == OBJ_OP_WRITE) {
		if (!offset && length == object_size)
			opcode = CEPH_OSD_OP_WRITEFULL;
		else
			opcode = CEPH_OSD_OP_WRITE;
		osd_req_op_alloc_hint_init(osd_request, num_ops,
					object_size, object_size);
		num_ops++;
	} else {
		opcode = CEPH_OSD_OP_READ;
	}

	if (opcode == CEPH_OSD_OP_DELETE)
		osd_req_op_init(osd_request, num_ops, opcode, 0);
	else
		osd_req_op_extent_init(osd_request, num_ops, opcode,
				       offset, length, 0, 0);

	if (obj_request->type == OBJ_REQUEST_BIO)
		osd_req_op_extent_osd_data_bio(osd_request, num_ops,
					obj_request->bio_list, length);
	else if (obj_request->type == OBJ_REQUEST_PAGES)
		osd_req_op_extent_osd_data_pages(osd_request, num_ops,
					obj_request->pages, length,
					offset & ~PAGE_MASK, false, false);

	/* Discards are also writes */
	if (op_type == OBJ_OP_WRITE || op_type == OBJ_OP_DISCARD)
		rbd_osd_req_format_write(obj_request);
	else
		rbd_osd_req_format_read(obj_request);
}

/*
 * Split up an image request into one or more object requests, each
 * to a different object.  The "type" parameter indicates whether
 * "data_desc" is the pointer to the head of a list of bio
 * structures, or the base of a page array.  In either case this
 * function assumes data_desc describes memory sufficient to hold
 * all data described by the image request.
 */
static int rbd_img_request_fill(struct rbd_img_request *img_request,
					enum obj_request_type type,
					void *data_desc)
{
	struct rbd_device *rbd_dev = img_request->rbd_dev;
	struct rbd_obj_request *obj_request = NULL;
	struct rbd_obj_request *next_obj_request;
	struct bio *bio_list = NULL;
	unsigned int bio_offset = 0;
	struct page **pages = NULL;
	enum obj_operation_type op_type;
	u64 img_offset;
	u64 resid;

	dout("%s: img %p type %d data_desc %p\n", __func__, img_request,
		(int)type, data_desc);

	img_offset = img_request->offset;
	resid = img_request->length;
	rbd_assert(resid > 0);
	op_type = rbd_img_request_op_type(img_request);

	if (type == OBJ_REQUEST_BIO) {
		bio_list = data_desc;
		rbd_assert(img_offset ==
			   bio_list->bi_iter.bi_sector << SECTOR_SHIFT);
	} else if (type == OBJ_REQUEST_PAGES) {
		pages = data_desc;
	}

	while (resid) {
		struct ceph_osd_request *osd_req;
		const char *object_name;
		u64 offset;
		u64 length;

		object_name = rbd_segment_name(rbd_dev, img_offset);
		if (!object_name)
			goto out_unwind;
		offset = rbd_segment_offset(rbd_dev, img_offset);
		length = rbd_segment_length(rbd_dev, img_offset, resid);
		obj_request = rbd_obj_request_create(object_name,
						offset, length, type);
		/* object request has its own copy of the object name */
		rbd_segment_name_free(object_name);
		if (!obj_request)
			goto out_unwind;

		/*
		 * set obj_request->img_request before creating the
		 * osd_request so that it gets the right snapc
		 */
		rbd_img_obj_request_add(img_request, obj_request);

		if (type == OBJ_REQUEST_BIO) {
			unsigned int clone_size;

			rbd_assert(length <= (u64)UINT_MAX);
			clone_size = (unsigned int)length;
			obj_request->bio_list =
					bio_chain_clone_range(&bio_list,
								&bio_offset,
								clone_size,
								GFP_NOIO);
			if (!obj_request->bio_list)
				goto out_unwind;
		} else if (type == OBJ_REQUEST_PAGES) {
			unsigned int page_count;

			obj_request->pages = pages;
			page_count = (u32)calc_pages_for(offset, length);
			obj_request->page_count = page_count;
			if ((offset + length) & ~PAGE_MASK)
				page_count--;	/* more on last page */
			pages += page_count;
		}

		osd_req = rbd_osd_req_create(rbd_dev, op_type,
					(op_type == OBJ_OP_WRITE) ? 2 : 1,
					obj_request);
		if (!osd_req)
			goto out_unwind;

		obj_request->osd_req = osd_req;
		obj_request->callback = rbd_img_obj_callback;
		obj_request->img_offset = img_offset;

		rbd_img_obj_request_fill(obj_request, osd_req, op_type, 0);

		rbd_img_request_get(img_request);

		img_offset += length;
		resid -= length;
	}

	return 0;

out_unwind:
	for_each_obj_request_safe(img_request, obj_request, next_obj_request)
		rbd_img_obj_request_del(img_request, obj_request);

	return -ENOMEM;
}

static void
rbd_osd_copyup_callback(struct rbd_obj_request *obj_request)
{
	struct rbd_img_request *img_request;
	struct rbd_device *rbd_dev;
	struct page **pages;
	u32 page_count;

	dout("%s: obj %p\n", __func__, obj_request);

	rbd_assert(obj_request->type == OBJ_REQUEST_BIO ||
		obj_request->type == OBJ_REQUEST_NODATA);
	rbd_assert(obj_request_img_data_test(obj_request));
	img_request = obj_request->img_request;
	rbd_assert(img_request);

	rbd_dev = img_request->rbd_dev;
	rbd_assert(rbd_dev);

	pages = obj_request->copyup_pages;
	rbd_assert(pages != NULL);
	obj_request->copyup_pages = NULL;
	page_count = obj_request->copyup_page_count;
	rbd_assert(page_count);
	obj_request->copyup_page_count = 0;
	ceph_release_page_vector(pages, page_count);

	/*
	 * We want the transfer count to reflect the size of the
	 * original write request.  There is no such thing as a
	 * successful short write, so if the request was successful
	 * we can just set it to the originally-requested length.
	 */
	if (!obj_request->result)
		obj_request->xferred = obj_request->length;

	obj_request_done_set(obj_request);
}

static void
rbd_img_obj_parent_read_full_callback(struct rbd_img_request *img_request)
{
	struct rbd_obj_request *orig_request;
	struct ceph_osd_request *osd_req;
	struct ceph_osd_client *osdc;
	struct rbd_device *rbd_dev;
	struct page **pages;
	enum obj_operation_type op_type;
	u32 page_count;
	int img_result;
	u64 parent_length;

	rbd_assert(img_request_child_test(img_request));

	/* First get what we need from the image request */

	pages = img_request->copyup_pages;
	rbd_assert(pages != NULL);
	img_request->copyup_pages = NULL;
	page_count = img_request->copyup_page_count;
	rbd_assert(page_count);
	img_request->copyup_page_count = 0;

	orig_request = img_request->obj_request;
	rbd_assert(orig_request != NULL);
	rbd_assert(obj_request_type_valid(orig_request->type));
	img_result = img_request->result;
	parent_length = img_request->length;
	rbd_assert(parent_length == img_request->xferred);
	rbd_img_request_put(img_request);

	rbd_assert(orig_request->img_request);
	rbd_dev = orig_request->img_request->rbd_dev;
	rbd_assert(rbd_dev);

	/*
	 * If the overlap has become 0 (most likely because the
	 * image has been flattened) we need to free the pages
	 * and re-submit the original write request.
	 */
	if (!rbd_dev->parent_overlap) {
		struct ceph_osd_client *osdc;

		ceph_release_page_vector(pages, page_count);
		osdc = &rbd_dev->rbd_client->client->osdc;
		img_result = rbd_obj_request_submit(osdc, orig_request);
		if (!img_result)
			return;
	}

	if (img_result)
		goto out_err;

	/*
	 * The original osd request is of no use to use any more.
	 * We need a new one that can hold the three ops in a copyup
	 * request.  Allocate the new copyup osd request for the
	 * original request, and release the old one.
	 */
	img_result = -ENOMEM;
	osd_req = rbd_osd_req_create_copyup(orig_request);
	if (!osd_req)
		goto out_err;
	rbd_osd_req_destroy(orig_request->osd_req);
	orig_request->osd_req = osd_req;
	orig_request->copyup_pages = pages;
	orig_request->copyup_page_count = page_count;

	/* Initialize the copyup op */

	osd_req_op_cls_init(osd_req, 0, CEPH_OSD_OP_CALL, "rbd", "copyup");
	osd_req_op_cls_request_data_pages(osd_req, 0, pages, parent_length, 0,
						false, false);

	/* Add the other op(s) */

	op_type = rbd_img_request_op_type(orig_request->img_request);
	rbd_img_obj_request_fill(orig_request, osd_req, op_type, 1);

	/* All set, send it off. */

	osdc = &rbd_dev->rbd_client->client->osdc;
	img_result = rbd_obj_request_submit(osdc, orig_request);
	if (!img_result)
		return;
out_err:
	/* Record the error code and complete the request */

	orig_request->result = img_result;
	orig_request->xferred = 0;
	obj_request_done_set(orig_request);
	rbd_obj_request_complete(orig_request);
}

/*
 * Read from the parent image the range of data that covers the
 * entire target of the given object request.  This is used for
 * satisfying a layered image write request when the target of an
 * object request from the image request does not exist.
 *
 * A page array big enough to hold the returned data is allocated
 * and supplied to rbd_img_request_fill() as the "data descriptor."
 * When the read completes, this page array will be transferred to
 * the original object request for the copyup operation.
 *
 * If an error occurs, record it as the result of the original
 * object request and mark it done so it gets completed.
 */
static int rbd_img_obj_parent_read_full(struct rbd_obj_request *obj_request)
{
	struct rbd_img_request *img_request = NULL;
	struct rbd_img_request *parent_request = NULL;
	struct rbd_device *rbd_dev;
	u64 img_offset;
	u64 length;
	struct page **pages = NULL;
	u32 page_count;
	int result;

	rbd_assert(obj_request_img_data_test(obj_request));
	rbd_assert(obj_request_type_valid(obj_request->type));

	img_request = obj_request->img_request;
	rbd_assert(img_request != NULL);
	rbd_dev = img_request->rbd_dev;
	rbd_assert(rbd_dev->parent != NULL);

	/*
	 * Determine the byte range covered by the object in the
	 * child image to which the original request was to be sent.
	 */
	img_offset = obj_request->img_offset - obj_request->offset;
	length = (u64)1 << rbd_dev->header.obj_order;

	/*
	 * There is no defined parent data beyond the parent
	 * overlap, so limit what we read at that boundary if
	 * necessary.
	 */
	if (img_offset + length > rbd_dev->parent_overlap) {
		rbd_assert(img_offset < rbd_dev->parent_overlap);
		length = rbd_dev->parent_overlap - img_offset;
	}

	/*
	 * Allocate a page array big enough to receive the data read
	 * from the parent.
	 */
	page_count = (u32)calc_pages_for(0, length);
	pages = ceph_alloc_page_vector(page_count, GFP_KERNEL);
	if (IS_ERR(pages)) {
		result = PTR_ERR(pages);
		pages = NULL;
		goto out_err;
	}

	result = -ENOMEM;
	parent_request = rbd_parent_request_create(obj_request,
						img_offset, length);
	if (!parent_request)
		goto out_err;

	result = rbd_img_request_fill(parent_request, OBJ_REQUEST_PAGES, pages);
	if (result)
		goto out_err;
	parent_request->copyup_pages = pages;
	parent_request->copyup_page_count = page_count;

	parent_request->callback = rbd_img_obj_parent_read_full_callback;
	result = rbd_img_request_submit(parent_request);
	if (!result)
		return 0;

	parent_request->copyup_pages = NULL;
	parent_request->copyup_page_count = 0;
	parent_request->obj_request = NULL;
	rbd_obj_request_put(obj_request);
out_err:
	if (pages)
		ceph_release_page_vector(pages, page_count);
	if (parent_request)
		rbd_img_request_put(parent_request);
	obj_request->result = result;
	obj_request->xferred = 0;
	obj_request_done_set(obj_request);

	return result;
}

static void rbd_img_obj_exists_callback(struct rbd_obj_request *obj_request)
{
	struct rbd_obj_request *orig_request;
	struct rbd_device *rbd_dev;
	int result;

	rbd_assert(!obj_request_img_data_test(obj_request));

	/*
	 * All we need from the object request is the original
	 * request and the result of the STAT op.  Grab those, then
	 * we're done with the request.
	 */
	orig_request = obj_request->obj_request;
	obj_request->obj_request = NULL;
	rbd_obj_request_put(orig_request);
	rbd_assert(orig_request);
	rbd_assert(orig_request->img_request);

	result = obj_request->result;
	obj_request->result = 0;

	dout("%s: obj %p for obj %p result %d %llu/%llu\n", __func__,
		obj_request, orig_request, result,
		obj_request->xferred, obj_request->length);
	rbd_obj_request_put(obj_request);

	/*
	 * If the overlap has become 0 (most likely because the
	 * image has been flattened) we need to free the pages
	 * and re-submit the original write request.
	 */
	rbd_dev = orig_request->img_request->rbd_dev;
	if (!rbd_dev->parent_overlap) {
		struct ceph_osd_client *osdc;

		osdc = &rbd_dev->rbd_client->client->osdc;
		result = rbd_obj_request_submit(osdc, orig_request);
		if (!result)
			return;
	}

	/*
	 * Our only purpose here is to determine whether the object
	 * exists, and we don't want to treat the non-existence as
	 * an error.  If something else comes back, transfer the
	 * error to the original request and complete it now.
	 */
	if (!result) {
		obj_request_existence_set(orig_request, true);
	} else if (result == -ENOENT) {
		obj_request_existence_set(orig_request, false);
	} else if (result) {
		orig_request->result = result;
		goto out;
	}

	/*
	 * Resubmit the original request now that we have recorded
	 * whether the target object exists.
	 */
	orig_request->result = rbd_img_obj_request_submit(orig_request);
out:
	if (orig_request->result)
		rbd_obj_request_complete(orig_request);
}

static int rbd_img_obj_exists_submit(struct rbd_obj_request *obj_request)
{
	struct rbd_obj_request *stat_request;
	struct rbd_device *rbd_dev;
	struct ceph_osd_client *osdc;
	struct page **pages = NULL;
	u32 page_count;
	size_t size;
	int ret;

	/*
	 * The response data for a STAT call consists of:
	 *     le64 length;
	 *     struct {
	 *         le32 tv_sec;
	 *         le32 tv_nsec;
	 *     } mtime;
	 */
	size = sizeof (__le64) + sizeof (__le32) + sizeof (__le32);
	page_count = (u32)calc_pages_for(0, size);
	pages = ceph_alloc_page_vector(page_count, GFP_KERNEL);
	if (IS_ERR(pages))
		return PTR_ERR(pages);

	ret = -ENOMEM;
	stat_request = rbd_obj_request_create(obj_request->object_name, 0, 0,
							OBJ_REQUEST_PAGES);
	if (!stat_request)
		goto out;

	rbd_obj_request_get(obj_request);
	stat_request->obj_request = obj_request;
	stat_request->pages = pages;
	stat_request->page_count = page_count;

	rbd_assert(obj_request->img_request);
	rbd_dev = obj_request->img_request->rbd_dev;
	stat_request->osd_req = rbd_osd_req_create(rbd_dev, OBJ_OP_READ, 1,
						   stat_request);
	if (!stat_request->osd_req)
		goto out;
	stat_request->callback = rbd_img_obj_exists_callback;

	osd_req_op_init(stat_request->osd_req, 0, CEPH_OSD_OP_STAT, 0);
	osd_req_op_raw_data_in_pages(stat_request->osd_req, 0, pages, size, 0,
					false, false);
	rbd_osd_req_format_read(stat_request);

	osdc = &rbd_dev->rbd_client->client->osdc;
	ret = rbd_obj_request_submit(osdc, stat_request);
out:
	if (ret)
		rbd_obj_request_put(obj_request);

	return ret;
}

static bool img_obj_request_simple(struct rbd_obj_request *obj_request)
{
	struct rbd_img_request *img_request;
	struct rbd_device *rbd_dev;

	rbd_assert(obj_request_img_data_test(obj_request));

	img_request = obj_request->img_request;
	rbd_assert(img_request);
	rbd_dev = img_request->rbd_dev;

	/* Reads */
	if (!img_request_write_test(img_request) &&
	    !img_request_discard_test(img_request))
		return true;

	/* Non-layered writes */
	if (!img_request_layered_test(img_request))
		return true;

	/*
	 * Layered writes outside of the parent overlap range don't
	 * share any data with the parent.
	 */
	if (!obj_request_overlaps_parent(obj_request))
		return true;

	/*
	 * Entire-object layered writes - we will overwrite whatever
	 * parent data there is anyway.
	 */
	if (!obj_request->offset &&
	    obj_request->length == rbd_obj_bytes(&rbd_dev->header))
		return true;

	/*
	 * If the object is known to already exist, its parent data has
	 * already been copied.
	 */
	if (obj_request_known_test(obj_request) &&
	    obj_request_exists_test(obj_request))
		return true;

	return false;
}

static int rbd_img_obj_request_submit(struct rbd_obj_request *obj_request)
{
	if (img_obj_request_simple(obj_request)) {
		struct rbd_device *rbd_dev;
		struct ceph_osd_client *osdc;

		rbd_dev = obj_request->img_request->rbd_dev;
		osdc = &rbd_dev->rbd_client->client->osdc;

		return rbd_obj_request_submit(osdc, obj_request);
	}

	/*
	 * It's a layered write.  The target object might exist but
	 * we may not know that yet.  If we know it doesn't exist,
	 * start by reading the data for the full target object from
	 * the parent so we can use it for a copyup to the target.
	 */
	if (obj_request_known_test(obj_request))
		return rbd_img_obj_parent_read_full(obj_request);

	/* We don't know whether the target exists.  Go find out. */

	return rbd_img_obj_exists_submit(obj_request);
}

static int rbd_img_request_submit(struct rbd_img_request *img_request)
{
	struct rbd_obj_request *obj_request;
	struct rbd_obj_request *next_obj_request;

	dout("%s: img %p\n", __func__, img_request);
	for_each_obj_request_safe(img_request, obj_request, next_obj_request) {
		int ret;

		ret = rbd_img_obj_request_submit(obj_request);
		if (ret)
			return ret;
	}

	return 0;
}

static void rbd_img_parent_read_callback(struct rbd_img_request *img_request)
{
	struct rbd_obj_request *obj_request;
	struct rbd_device *rbd_dev;
	u64 obj_end;
	u64 img_xferred;
	int img_result;

	rbd_assert(img_request_child_test(img_request));

	/* First get what we need from the image request and release it */

	obj_request = img_request->obj_request;
	img_xferred = img_request->xferred;
	img_result = img_request->result;
	rbd_img_request_put(img_request);

	/*
	 * If the overlap has become 0 (most likely because the
	 * image has been flattened) we need to re-submit the
	 * original request.
	 */
	rbd_assert(obj_request);
	rbd_assert(obj_request->img_request);
	rbd_dev = obj_request->img_request->rbd_dev;
	if (!rbd_dev->parent_overlap) {
		struct ceph_osd_client *osdc;

		osdc = &rbd_dev->rbd_client->client->osdc;
		img_result = rbd_obj_request_submit(osdc, obj_request);
		if (!img_result)
			return;
	}

	obj_request->result = img_result;
	if (obj_request->result)
		goto out;

	/*
	 * We need to zero anything beyond the parent overlap
	 * boundary.  Since rbd_img_obj_request_read_callback()
	 * will zero anything beyond the end of a short read, an
	 * easy way to do this is to pretend the data from the
	 * parent came up short--ending at the overlap boundary.
	 */
	rbd_assert(obj_request->img_offset < U64_MAX - obj_request->length);
	obj_end = obj_request->img_offset + obj_request->length;
	if (obj_end > rbd_dev->parent_overlap) {
		u64 xferred = 0;

		if (obj_request->img_offset < rbd_dev->parent_overlap)
			xferred = rbd_dev->parent_overlap -
					obj_request->img_offset;

		obj_request->xferred = min(img_xferred, xferred);
	} else {
		obj_request->xferred = img_xferred;
	}
out:
	rbd_img_obj_request_read_callback(obj_request);
	rbd_obj_request_complete(obj_request);
}

static void rbd_img_parent_read(struct rbd_obj_request *obj_request)
{
	struct rbd_img_request *img_request;
	int result;

	rbd_assert(obj_request_img_data_test(obj_request));
	rbd_assert(obj_request->img_request != NULL);
	rbd_assert(obj_request->result == (s32) -ENOENT);
	rbd_assert(obj_request_type_valid(obj_request->type));

	/* rbd_read_finish(obj_request, obj_request->length); */
	img_request = rbd_parent_request_create(obj_request,
						obj_request->img_offset,
						obj_request->length);
	result = -ENOMEM;
	if (!img_request)
		goto out_err;

	if (obj_request->type == OBJ_REQUEST_BIO)
		result = rbd_img_request_fill(img_request, OBJ_REQUEST_BIO,
						obj_request->bio_list);
	else
		result = rbd_img_request_fill(img_request, OBJ_REQUEST_PAGES,
						obj_request->pages);
	if (result)
		goto out_err;

	img_request->callback = rbd_img_parent_read_callback;
	result = rbd_img_request_submit(img_request);
	if (result)
		goto out_err;

	return;
out_err:
	if (img_request)
		rbd_img_request_put(img_request);
	obj_request->result = result;
	obj_request->xferred = 0;
	obj_request_done_set(obj_request);
}

static int rbd_obj_notify_ack_sync(struct rbd_device *rbd_dev, u64 notify_id)
{
	struct rbd_obj_request *obj_request;
	struct ceph_osd_client *osdc = &rbd_dev->rbd_client->client->osdc;
	int ret;

	obj_request = rbd_obj_request_create(rbd_dev->header_name, 0, 0,
							OBJ_REQUEST_NODATA);
	if (!obj_request)
		return -ENOMEM;

	ret = -ENOMEM;
	obj_request->osd_req = rbd_osd_req_create(rbd_dev, OBJ_OP_READ, 1,
						  obj_request);
	if (!obj_request->osd_req)
		goto out;

	osd_req_op_watch_init(obj_request->osd_req, 0, CEPH_OSD_OP_NOTIFY_ACK,
					notify_id, 0, 0);
	rbd_osd_req_format_read(obj_request);

	ret = rbd_obj_request_submit(osdc, obj_request);
	if (ret)
		goto out;
	ret = rbd_obj_request_wait(obj_request);
out:
	rbd_obj_request_put(obj_request);

	return ret;
}

static void rbd_watch_cb(u64 ver, u64 notify_id, u8 opcode, void *data)
{
	struct rbd_device *rbd_dev = (struct rbd_device *)data;
	int ret;

	if (!rbd_dev)
		return;

	dout("%s: \"%s\" notify_id %llu opcode %u\n", __func__,
		rbd_dev->header_name, (unsigned long long)notify_id,
		(unsigned int)opcode);

	/*
	 * Until adequate refresh error handling is in place, there is
	 * not much we can do here, except warn.
	 *
	 * See http://tracker.ceph.com/issues/5040
	 */
	ret = rbd_dev_refresh(rbd_dev);
	if (ret)
		rbd_warn(rbd_dev, "refresh failed: %d", ret);

	ret = rbd_obj_notify_ack_sync(rbd_dev, notify_id);
	if (ret)
		rbd_warn(rbd_dev, "notify_ack ret %d", ret);
}

/*
 * Send a (un)watch request and wait for the ack.  Return a request
 * with a ref held on success or error.
 */
static struct rbd_obj_request *rbd_obj_watch_request_helper(
						struct rbd_device *rbd_dev,
						bool watch)
{
	struct ceph_osd_client *osdc = &rbd_dev->rbd_client->client->osdc;
	struct ceph_options *opts = osdc->client->options;
	struct rbd_obj_request *obj_request;
	int ret;

	obj_request = rbd_obj_request_create(rbd_dev->header_name, 0, 0,
					     OBJ_REQUEST_NODATA);
	if (!obj_request)
		return ERR_PTR(-ENOMEM);

	obj_request->osd_req = rbd_osd_req_create(rbd_dev, OBJ_OP_WRITE, 1,
						  obj_request);
	if (!obj_request->osd_req) {
		ret = -ENOMEM;
		goto out;
	}

	osd_req_op_watch_init(obj_request->osd_req, 0, CEPH_OSD_OP_WATCH,
			      rbd_dev->watch_event->cookie, 0, watch);
	rbd_osd_req_format_write(obj_request);

	if (watch)
		ceph_osdc_set_request_linger(osdc, obj_request->osd_req);

	ret = rbd_obj_request_submit(osdc, obj_request);
	if (ret)
		goto out;

	ret = rbd_obj_request_wait_timeout(obj_request, opts->mount_timeout);
	if (ret)
		goto out;

	ret = obj_request->result;
	if (ret) {
		if (watch)
			rbd_obj_request_end(obj_request);
		goto out;
	}

	return obj_request;

out:
	rbd_obj_request_put(obj_request);
	return ERR_PTR(ret);
}

/*
 * Initiate a watch request, synchronously.
 */
static int rbd_dev_header_watch_sync(struct rbd_device *rbd_dev)
{
	struct ceph_osd_client *osdc = &rbd_dev->rbd_client->client->osdc;
	struct rbd_obj_request *obj_request;
	int ret;

	rbd_assert(!rbd_dev->watch_event);
	rbd_assert(!rbd_dev->watch_request);

	ret = ceph_osdc_create_event(osdc, rbd_watch_cb, rbd_dev,
				     &rbd_dev->watch_event);
	if (ret < 0)
		return ret;

	obj_request = rbd_obj_watch_request_helper(rbd_dev, true);
	if (IS_ERR(obj_request)) {
		ceph_osdc_cancel_event(rbd_dev->watch_event);
		rbd_dev->watch_event = NULL;
		return PTR_ERR(obj_request);
	}

	/*
	 * A watch request is set to linger, so the underlying osd
	 * request won't go away until we unregister it.  We retain
	 * a pointer to the object request during that time (in
	 * rbd_dev->watch_request), so we'll keep a reference to it.
	 * We'll drop that reference after we've unregistered it in
	 * rbd_dev_header_unwatch_sync().
	 */
	rbd_dev->watch_request = obj_request;

	return 0;
}

/*
 * Tear down a watch request, synchronously.
 */
static void rbd_dev_header_unwatch_sync(struct rbd_device *rbd_dev)
{
	struct rbd_obj_request *obj_request;

	rbd_assert(rbd_dev->watch_event);
	rbd_assert(rbd_dev->watch_request);

	rbd_obj_request_end(rbd_dev->watch_request);
	rbd_obj_request_put(rbd_dev->watch_request);
	rbd_dev->watch_request = NULL;

	obj_request = rbd_obj_watch_request_helper(rbd_dev, false);
	if (!IS_ERR(obj_request))
		rbd_obj_request_put(obj_request);
	else
		rbd_warn(rbd_dev, "unable to tear down watch request (%ld)",
			 PTR_ERR(obj_request));

	ceph_osdc_cancel_event(rbd_dev->watch_event);
	rbd_dev->watch_event = NULL;
}

/*
 * Synchronous osd object method call.  Returns the number of bytes
 * returned in the outbound buffer, or a negative error code.
 */
static int rbd_obj_method_sync(struct rbd_device *rbd_dev,
			     const char *object_name,
			     const char *class_name,
			     const char *method_name,
			     const void *outbound,
			     size_t outbound_size,
			     void *inbound,
			     size_t inbound_size)
{
	struct ceph_osd_client *osdc = &rbd_dev->rbd_client->client->osdc;
	struct rbd_obj_request *obj_request;
	struct page **pages;
	u32 page_count;
	int ret;

	/*
	 * Method calls are ultimately read operations.  The result
	 * should placed into the inbound buffer provided.  They
	 * also supply outbound data--parameters for the object
	 * method.  Currently if this is present it will be a
	 * snapshot id.
	 */
	page_count = (u32)calc_pages_for(0, inbound_size);
	pages = ceph_alloc_page_vector(page_count, GFP_KERNEL);
	if (IS_ERR(pages))
		return PTR_ERR(pages);

	ret = -ENOMEM;
	obj_request = rbd_obj_request_create(object_name, 0, inbound_size,
							OBJ_REQUEST_PAGES);
	if (!obj_request)
		goto out;

	obj_request->pages = pages;
	obj_request->page_count = page_count;

	obj_request->osd_req = rbd_osd_req_create(rbd_dev, OBJ_OP_READ, 1,
						  obj_request);
	if (!obj_request->osd_req)
		goto out;

	osd_req_op_cls_init(obj_request->osd_req, 0, CEPH_OSD_OP_CALL,
					class_name, method_name);
	if (outbound_size) {
		struct ceph_pagelist *pagelist;

		pagelist = kmalloc(sizeof (*pagelist), GFP_NOFS);
		if (!pagelist)
			goto out;

		ceph_pagelist_init(pagelist);
		ceph_pagelist_append(pagelist, outbound, outbound_size);
		osd_req_op_cls_request_data_pagelist(obj_request->osd_req, 0,
						pagelist);
	}
	osd_req_op_cls_response_data_pages(obj_request->osd_req, 0,
					obj_request->pages, inbound_size,
					0, false, false);
	rbd_osd_req_format_read(obj_request);

	ret = rbd_obj_request_submit(osdc, obj_request);
	if (ret)
		goto out;
	ret = rbd_obj_request_wait(obj_request);
	if (ret)
		goto out;

	ret = obj_request->result;
	if (ret < 0)
		goto out;

	rbd_assert(obj_request->xferred < (u64)INT_MAX);
	ret = (int)obj_request->xferred;
	ceph_copy_from_page_vector(pages, inbound, 0, obj_request->xferred);
out:
	if (obj_request)
		rbd_obj_request_put(obj_request);
	else
		ceph_release_page_vector(pages, page_count);

	return ret;
}

static void rbd_queue_workfn(struct work_struct *work)
{
	struct request *rq = blk_mq_rq_from_pdu(work);
	struct rbd_device *rbd_dev = rq->q->queuedata;
	struct rbd_img_request *img_request;
	struct ceph_snap_context *snapc = NULL;
	u64 offset = (u64)blk_rq_pos(rq) << SECTOR_SHIFT;
	u64 length = blk_rq_bytes(rq);
	enum obj_operation_type op_type;
	u64 mapping_size;
	int result;

	if (rq->cmd_type != REQ_TYPE_FS) {
		dout("%s: non-fs request type %d\n", __func__,
			(int) rq->cmd_type);
		result = -EIO;
		goto err;
	}

	if (rq->cmd_flags & REQ_DISCARD)
		op_type = OBJ_OP_DISCARD;
	else if (rq->cmd_flags & REQ_WRITE)
		op_type = OBJ_OP_WRITE;
	else
		op_type = OBJ_OP_READ;

	/* Ignore/skip any zero-length requests */

	if (!length) {
		dout("%s: zero-length request\n", __func__);
		result = 0;
		goto err_rq;
	}

	/* Only reads are allowed to a read-only device */

	if (op_type != OBJ_OP_READ) {
		if (rbd_dev->mapping.read_only) {
			result = -EROFS;
			goto err_rq;
		}
		rbd_assert(rbd_dev->spec->snap_id == CEPH_NOSNAP);
	}

	/*
	 * Quit early if the mapped snapshot no longer exists.  It's
	 * still possible the snapshot will have disappeared by the
	 * time our request arrives at the osd, but there's no sense in
	 * sending it if we already know.
	 */
	if (!test_bit(RBD_DEV_FLAG_EXISTS, &rbd_dev->flags)) {
		dout("request for non-existent snapshot");
		rbd_assert(rbd_dev->spec->snap_id != CEPH_NOSNAP);
		result = -ENXIO;
		goto err_rq;
	}

	if (offset && length > U64_MAX - offset + 1) {
		rbd_warn(rbd_dev, "bad request range (%llu~%llu)", offset,
			 length);
		result = -EINVAL;
		goto err_rq;	/* Shouldn't happen */
	}

	blk_mq_start_request(rq);

	down_read(&rbd_dev->header_rwsem);
	mapping_size = rbd_dev->mapping.size;
	if (op_type != OBJ_OP_READ) {
		snapc = rbd_dev->header.snapc;
		ceph_get_snap_context(snapc);
	}
	up_read(&rbd_dev->header_rwsem);

	if (offset + length > mapping_size) {
		rbd_warn(rbd_dev, "beyond EOD (%llu~%llu > %llu)", offset,
			 length, mapping_size);
		result = -EIO;
		goto err_rq;
	}

	img_request = rbd_img_request_create(rbd_dev, offset, length, op_type,
					     snapc);
	if (!img_request) {
		result = -ENOMEM;
		goto err_rq;
	}
	img_request->rq = rq;
	snapc = NULL; /* img_request consumes a ref */

	if (op_type == OBJ_OP_DISCARD)
		result = rbd_img_request_fill(img_request, OBJ_REQUEST_NODATA,
					      NULL);
	else
		result = rbd_img_request_fill(img_request, OBJ_REQUEST_BIO,
					      rq->bio);
	if (result)
		goto err_img_request;

	result = rbd_img_request_submit(img_request);
	if (result)
		goto err_img_request;

	return;

err_img_request:
	rbd_img_request_put(img_request);
err_rq:
	if (result)
		rbd_warn(rbd_dev, "%s %llx at %llx result %d",
			 obj_op_name(op_type), length, offset, result);
	ceph_put_snap_context(snapc);
err:
	blk_mq_end_request(rq, result);
}

static int rbd_queue_rq(struct blk_mq_hw_ctx *hctx,
		const struct blk_mq_queue_data *bd)
{
	struct request *rq = bd->rq;
	struct work_struct *work = blk_mq_rq_to_pdu(rq);

	queue_work(rbd_wq, work);
	return BLK_MQ_RQ_QUEUE_OK;
}

static void rbd_free_disk(struct rbd_device *rbd_dev)
{
	struct gendisk *disk = rbd_dev->disk;

	if (!disk)
		return;

	rbd_dev->disk = NULL;
	if (disk->flags & GENHD_FL_UP) {
		del_gendisk(disk);
		if (disk->queue)
			blk_cleanup_queue(disk->queue);
		blk_mq_free_tag_set(&rbd_dev->tag_set);
	}
	put_disk(disk);
}

static int rbd_obj_read_sync(struct rbd_device *rbd_dev,
				const char *object_name,
				u64 offset, u64 length, void *buf)

{
	struct ceph_osd_client *osdc = &rbd_dev->rbd_client->client->osdc;
	struct rbd_obj_request *obj_request;
	struct page **pages = NULL;
	u32 page_count;
	size_t size;
	int ret;

	page_count = (u32) calc_pages_for(offset, length);
	pages = ceph_alloc_page_vector(page_count, GFP_KERNEL);
	if (IS_ERR(pages))
		return PTR_ERR(pages);

	ret = -ENOMEM;
	obj_request = rbd_obj_request_create(object_name, offset, length,
							OBJ_REQUEST_PAGES);
	if (!obj_request)
		goto out;

	obj_request->pages = pages;
	obj_request->page_count = page_count;

	obj_request->osd_req = rbd_osd_req_create(rbd_dev, OBJ_OP_READ, 1,
						  obj_request);
	if (!obj_request->osd_req)
		goto out;

	osd_req_op_extent_init(obj_request->osd_req, 0, CEPH_OSD_OP_READ,
					offset, length, 0, 0);
	osd_req_op_extent_osd_data_pages(obj_request->osd_req, 0,
					obj_request->pages,
					obj_request->length,
					obj_request->offset & ~PAGE_MASK,
					false, false);
	rbd_osd_req_format_read(obj_request);

	ret = rbd_obj_request_submit(osdc, obj_request);
	if (ret)
		goto out;
	ret = rbd_obj_request_wait(obj_request);
	if (ret)
		goto out;

	ret = obj_request->result;
	if (ret < 0)
		goto out;

	rbd_assert(obj_request->xferred <= (u64) SIZE_MAX);
	size = (size_t) obj_request->xferred;
	ceph_copy_from_page_vector(pages, buf, 0, size);
	rbd_assert(size <= (size_t)INT_MAX);
	ret = (int)size;
out:
	if (obj_request)
		rbd_obj_request_put(obj_request);
	else
		ceph_release_page_vector(pages, page_count);

	return ret;
}

/*
 * Read the complete header for the given rbd device.  On successful
 * return, the rbd_dev->header field will contain up-to-date
 * information about the image.
 */
static int rbd_dev_v1_header_info(struct rbd_device *rbd_dev)
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
			return -ENOMEM;

		ret = rbd_obj_read_sync(rbd_dev, rbd_dev->header_name,
				       0, size, ondisk);
		if (ret < 0)
			goto out;
		if ((size_t)ret < size) {
			ret = -ENXIO;
			rbd_warn(rbd_dev, "short header read (want %zd got %d)",
				size, ret);
			goto out;
		}
		if (!rbd_dev_ondisk_valid(ondisk)) {
			ret = -ENXIO;
			rbd_warn(rbd_dev, "invalid header");
			goto out;
		}

		names_size = le64_to_cpu(ondisk->snap_names_len);
		want_count = snap_count;
		snap_count = le32_to_cpu(ondisk->snap_count);
	} while (snap_count != want_count);

	ret = rbd_header_from_disk(rbd_dev, ondisk);
out:
	kfree(ondisk);

	return ret;
}

/*
 * Clear the rbd device's EXISTS flag if the snapshot it's mapped to
 * has disappeared from the (just updated) snapshot context.
 */
static void rbd_exists_validate(struct rbd_device *rbd_dev)
{
	u64 snap_id;

	if (!test_bit(RBD_DEV_FLAG_EXISTS, &rbd_dev->flags))
		return;

	snap_id = rbd_dev->spec->snap_id;
	if (snap_id == CEPH_NOSNAP)
		return;

	if (rbd_dev_snap_index(rbd_dev, snap_id) == BAD_SNAP_INDEX)
		clear_bit(RBD_DEV_FLAG_EXISTS, &rbd_dev->flags);
}

static void rbd_dev_update_size(struct rbd_device *rbd_dev)
{
	sector_t size;
	bool removing;

	/*
	 * Don't hold the lock while doing disk operations,
	 * or lock ordering will conflict with the bdev mutex via:
	 * rbd_add() -> blkdev_get() -> rbd_open()
	 */
	spin_lock_irq(&rbd_dev->lock);
	removing = test_bit(RBD_DEV_FLAG_REMOVING, &rbd_dev->flags);
	spin_unlock_irq(&rbd_dev->lock);
	/*
	 * If the device is being removed, rbd_dev->disk has
	 * been destroyed, so don't try to update its size
	 */
	if (!removing) {
		size = (sector_t)rbd_dev->mapping.size / SECTOR_SIZE;
		dout("setting size to %llu sectors", (unsigned long long)size);
		set_capacity(rbd_dev->disk, size);
		revalidate_disk(rbd_dev->disk);
	}
}

static int rbd_dev_refresh(struct rbd_device *rbd_dev)
{
	u64 mapping_size;
	int ret;

	down_write(&rbd_dev->header_rwsem);
	mapping_size = rbd_dev->mapping.size;

	ret = rbd_dev_header_info(rbd_dev);
	if (ret)
		goto out;

	/*
	 * If there is a parent, see if it has disappeared due to the
	 * mapped image getting flattened.
	 */
	if (rbd_dev->parent) {
		ret = rbd_dev_v2_parent_info(rbd_dev);
		if (ret)
			goto out;
	}

	if (rbd_dev->spec->snap_id == CEPH_NOSNAP) {
		rbd_dev->mapping.size = rbd_dev->header.image_size;
	} else {
		/* validate mapped snapshot's EXISTS flag */
		rbd_exists_validate(rbd_dev);
	}

out:
	up_write(&rbd_dev->header_rwsem);
	if (!ret && mapping_size != rbd_dev->mapping.size)
		rbd_dev_update_size(rbd_dev);

	return ret;
}

static int rbd_init_request(void *data, struct request *rq,
		unsigned int hctx_idx, unsigned int request_idx,
		unsigned int numa_node)
{
	struct work_struct *work = blk_mq_rq_to_pdu(rq);

	INIT_WORK(work, rbd_queue_workfn);
	return 0;
}

static struct blk_mq_ops rbd_mq_ops = {
	.queue_rq	= rbd_queue_rq,
	.map_queue	= blk_mq_map_queue,
	.init_request	= rbd_init_request,
};

static int rbd_init_disk(struct rbd_device *rbd_dev)
{
	struct gendisk *disk;
	struct request_queue *q;
	u64 segment_size;
	int err;

	/* create gendisk info */
	disk = alloc_disk(single_major ?
			  (1 << RBD_SINGLE_MAJOR_PART_SHIFT) :
			  RBD_MINORS_PER_MAJOR);
	if (!disk)
		return -ENOMEM;

	snprintf(disk->disk_name, sizeof(disk->disk_name), RBD_DRV_NAME "%d",
		 rbd_dev->dev_id);
	disk->major = rbd_dev->major;
	disk->first_minor = rbd_dev->minor;
	if (single_major)
		disk->flags |= GENHD_FL_EXT_DEVT;
	disk->fops = &rbd_bd_ops;
	disk->private_data = rbd_dev;

	memset(&rbd_dev->tag_set, 0, sizeof(rbd_dev->tag_set));
	rbd_dev->tag_set.ops = &rbd_mq_ops;
	rbd_dev->tag_set.queue_depth = rbd_dev->opts->queue_depth;
	rbd_dev->tag_set.numa_node = NUMA_NO_NODE;
	rbd_dev->tag_set.flags = BLK_MQ_F_SHOULD_MERGE | BLK_MQ_F_SG_MERGE;
	rbd_dev->tag_set.nr_hw_queues = 1;
	rbd_dev->tag_set.cmd_size = sizeof(struct work_struct);

	err = blk_mq_alloc_tag_set(&rbd_dev->tag_set);
	if (err)
		goto out_disk;

	q = blk_mq_init_queue(&rbd_dev->tag_set);
	if (IS_ERR(q)) {
		err = PTR_ERR(q);
		goto out_tag_set;
	}

	queue_flag_set_unlocked(QUEUE_FLAG_NONROT, q);
	/* QUEUE_FLAG_ADD_RANDOM is off by default for blk-mq */

	/* set io sizes to object size */
	segment_size = rbd_obj_bytes(&rbd_dev->header);
	blk_queue_max_hw_sectors(q, segment_size / SECTOR_SIZE);
	q->limits.max_sectors = queue_max_hw_sectors(q);
	blk_queue_max_segments(q, segment_size / SECTOR_SIZE);
	blk_queue_max_segment_size(q, segment_size);
	blk_queue_io_min(q, segment_size);
	blk_queue_io_opt(q, segment_size);

	/* enable the discard support */
	queue_flag_set_unlocked(QUEUE_FLAG_DISCARD, q);
	q->limits.discard_granularity = segment_size;
	q->limits.discard_alignment = segment_size;
	blk_queue_max_discard_sectors(q, segment_size / SECTOR_SIZE);
	q->limits.discard_zeroes_data = 1;

	if (!ceph_test_opt(rbd_dev->rbd_client->client, NOCRC))
		q->backing_dev_info.capabilities |= BDI_CAP_STABLE_WRITES;

	disk->queue = q;

	q->queuedata = rbd_dev;

	rbd_dev->disk = disk;

	return 0;
out_tag_set:
	blk_mq_free_tag_set(&rbd_dev->tag_set);
out_disk:
	put_disk(disk);
	return err;
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

	return sprintf(buf, "%llu\n",
		(unsigned long long)rbd_dev->mapping.size);
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
			(unsigned long long)rbd_dev->mapping.features);
}

static ssize_t rbd_major_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct rbd_device *rbd_dev = dev_to_rbd_dev(dev);

	if (rbd_dev->major)
		return sprintf(buf, "%d\n", rbd_dev->major);

	return sprintf(buf, "(none)\n");
}

static ssize_t rbd_minor_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct rbd_device *rbd_dev = dev_to_rbd_dev(dev);

	return sprintf(buf, "%d\n", rbd_dev->minor);
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
 * For a v2 image, shows the chain of parent images, separated by empty
 * lines.  For v1 images or if there is no parent, shows "(no parent
 * image)".
 */
static ssize_t rbd_parent_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct rbd_device *rbd_dev = dev_to_rbd_dev(dev);
	ssize_t count = 0;

	if (!rbd_dev->parent)
		return sprintf(buf, "(no parent image)\n");

	for ( ; rbd_dev->parent; rbd_dev = rbd_dev->parent) {
		struct rbd_spec *spec = rbd_dev->parent_spec;

		count += sprintf(&buf[count], "%s"
			    "pool_id %llu\npool_name %s\n"
			    "image_id %s\nimage_name %s\n"
			    "snap_id %llu\nsnap_name %s\n"
			    "overlap %llu\n",
			    !count ? "" : "\n", /* first? */
			    spec->pool_id, spec->pool_name,
			    spec->image_id, spec->image_name ?: "(unknown)",
			    spec->snap_id, spec->snap_name,
			    rbd_dev->parent_overlap);
	}

	return count;
}

static ssize_t rbd_image_refresh(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf,
				 size_t size)
{
	struct rbd_device *rbd_dev = dev_to_rbd_dev(dev);
	int ret;

	ret = rbd_dev_refresh(rbd_dev);
	if (ret)
		return ret;

	return size;
}

static DEVICE_ATTR(size, S_IRUGO, rbd_size_show, NULL);
static DEVICE_ATTR(features, S_IRUGO, rbd_features_show, NULL);
static DEVICE_ATTR(major, S_IRUGO, rbd_major_show, NULL);
static DEVICE_ATTR(minor, S_IRUGO, rbd_minor_show, NULL);
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
	&dev_attr_minor.attr,
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

static void rbd_dev_release(struct device *dev);

static struct device_type rbd_device_type = {
	.name		= "rbd",
	.groups		= rbd_attr_groups,
	.release	= rbd_dev_release,
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

	spec->pool_id = CEPH_NOPOOL;
	spec->snap_id = CEPH_NOSNAP;
	kref_init(&spec->kref);

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

static void rbd_dev_release(struct device *dev)
{
	struct rbd_device *rbd_dev = dev_to_rbd_dev(dev);
	bool need_put = !!rbd_dev->opts;

	rbd_put_client(rbd_dev->rbd_client);
	rbd_spec_put(rbd_dev->spec);
	kfree(rbd_dev->opts);
	kfree(rbd_dev);

	/*
	 * This is racy, but way better than putting module outside of
	 * the release callback.  The race window is pretty small, so
	 * doing something similar to dm (dm-builtin.c) is overkill.
	 */
	if (need_put)
		module_put(THIS_MODULE);
}

static struct rbd_device *rbd_dev_create(struct rbd_client *rbdc,
					 struct rbd_spec *spec,
					 struct rbd_options *opts)
{
	struct rbd_device *rbd_dev;

	rbd_dev = kzalloc(sizeof (*rbd_dev), GFP_KERNEL);
	if (!rbd_dev)
		return NULL;

	spin_lock_init(&rbd_dev->lock);
	rbd_dev->flags = 0;
	atomic_set(&rbd_dev->parent_ref, 0);
	INIT_LIST_HEAD(&rbd_dev->node);
	init_rwsem(&rbd_dev->header_rwsem);

	rbd_dev->dev.bus = &rbd_bus_type;
	rbd_dev->dev.type = &rbd_device_type;
	rbd_dev->dev.parent = &rbd_root_dev;
	device_initialize(&rbd_dev->dev);

	rbd_dev->rbd_client = rbdc;
	rbd_dev->spec = spec;
	rbd_dev->opts = opts;

	/* Initialize the layout used for all rbd requests */

	rbd_dev->layout.fl_stripe_unit = cpu_to_le32(1 << RBD_MAX_OBJ_ORDER);
	rbd_dev->layout.fl_stripe_count = cpu_to_le32(1);
	rbd_dev->layout.fl_object_size = cpu_to_le32(1 << RBD_MAX_OBJ_ORDER);
	rbd_dev->layout.fl_pg_pool = cpu_to_le32((u32) spec->pool_id);

	/*
	 * If this is a mapping rbd_dev (as opposed to a parent one),
	 * pin our module.  We have a ref from do_rbd_add(), so use
	 * __module_get().
	 */
	if (rbd_dev->opts)
		__module_get(THIS_MODULE);

	return rbd_dev;
}

static void rbd_dev_destroy(struct rbd_device *rbd_dev)
{
	if (rbd_dev)
		put_device(&rbd_dev->dev);
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

	ret = rbd_obj_method_sync(rbd_dev, rbd_dev->header_name,
				"rbd", "get_size",
				&snapid, sizeof (snapid),
				&size_buf, sizeof (size_buf));
	dout("%s: rbd_obj_method_sync returned %d\n", __func__, ret);
	if (ret < 0)
		return ret;
	if (ret < sizeof (size_buf))
		return -ERANGE;

	if (order) {
		*order = size_buf.order;
		dout("  order %u", (unsigned int)*order);
	}
	*snap_size = le64_to_cpu(size_buf.size);

	dout("  snap_id 0x%016llx snap_size = %llu\n",
		(unsigned long long)snap_id,
		(unsigned long long)*snap_size);

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

	ret = rbd_obj_method_sync(rbd_dev, rbd_dev->header_name,
				"rbd", "get_object_prefix", NULL, 0,
				reply_buf, RBD_OBJ_PREFIX_LEN_MAX);
	dout("%s: rbd_obj_method_sync returned %d\n", __func__, ret);
	if (ret < 0)
		goto out;

	p = reply_buf;
	rbd_dev->header.object_prefix = ceph_extract_encoded_string(&p,
						p + ret, NULL, GFP_NOIO);
	ret = 0;

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
	} __attribute__ ((packed)) features_buf = { 0 };
	u64 incompat;
	int ret;

	ret = rbd_obj_method_sync(rbd_dev, rbd_dev->header_name,
				"rbd", "get_features",
				&snapid, sizeof (snapid),
				&features_buf, sizeof (features_buf));
	dout("%s: rbd_obj_method_sync returned %d\n", __func__, ret);
	if (ret < 0)
		return ret;
	if (ret < sizeof (features_buf))
		return -ERANGE;

	incompat = le64_to_cpu(features_buf.incompat);
	if (incompat & ~RBD_FEATURES_SUPPORTED)
		return -ENXIO;

	*snap_features = le64_to_cpu(features_buf.features);

	dout("  snap_id 0x%016llx features = 0x%016llx incompat = 0x%016llx\n",
		(unsigned long long)snap_id,
		(unsigned long long)*snap_features,
		(unsigned long long)le64_to_cpu(features_buf.incompat));

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
	u64 pool_id;
	char *image_id;
	u64 snap_id;
	u64 overlap;
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

	snapid = cpu_to_le64(rbd_dev->spec->snap_id);
	ret = rbd_obj_method_sync(rbd_dev, rbd_dev->header_name,
				"rbd", "get_parent",
				&snapid, sizeof (snapid),
				reply_buf, size);
	dout("%s: rbd_obj_method_sync returned %d\n", __func__, ret);
	if (ret < 0)
		goto out_err;

	p = reply_buf;
	end = reply_buf + ret;
	ret = -ERANGE;
	ceph_decode_64_safe(&p, end, pool_id, out_err);
	if (pool_id == CEPH_NOPOOL) {
		/*
		 * Either the parent never existed, or we have
		 * record of it but the image got flattened so it no
		 * longer has a parent.  When the parent of a
		 * layered image disappears we immediately set the
		 * overlap to 0.  The effect of this is that all new
		 * requests will be treated as if the image had no
		 * parent.
		 */
		if (rbd_dev->parent_overlap) {
			rbd_dev->parent_overlap = 0;
			rbd_dev_parent_put(rbd_dev);
			pr_info("%s: clone image has been flattened\n",
				rbd_dev->disk->disk_name);
		}

		goto out;	/* No parent?  No problem. */
	}

	/* The ceph file layout needs to fit pool id in 32 bits */

	ret = -EIO;
	if (pool_id > (u64)U32_MAX) {
		rbd_warn(NULL, "parent pool id too large (%llu > %u)",
			(unsigned long long)pool_id, U32_MAX);
		goto out_err;
	}

	image_id = ceph_extract_encoded_string(&p, end, NULL, GFP_KERNEL);
	if (IS_ERR(image_id)) {
		ret = PTR_ERR(image_id);
		goto out_err;
	}
	ceph_decode_64_safe(&p, end, snap_id, out_err);
	ceph_decode_64_safe(&p, end, overlap, out_err);

	/*
	 * The parent won't change (except when the clone is
	 * flattened, already handled that).  So we only need to
	 * record the parent spec we have not already done so.
	 */
	if (!rbd_dev->parent_spec) {
		parent_spec->pool_id = pool_id;
		parent_spec->image_id = image_id;
		parent_spec->snap_id = snap_id;
		rbd_dev->parent_spec = parent_spec;
		parent_spec = NULL;	/* rbd_dev now owns this */
	} else {
		kfree(image_id);
	}

	/*
	 * We always update the parent overlap.  If it's zero we issue
	 * a warning, as we will proceed as if there was no parent.
	 */
	if (!overlap) {
		if (parent_spec) {
			/* refresh, careful to warn just once */
			if (rbd_dev->parent_overlap)
				rbd_warn(rbd_dev,
				    "clone now standalone (overlap became 0)");
		} else {
			/* initial probe */
			rbd_warn(rbd_dev, "clone is standalone (overlap 0)");
		}
	}
	rbd_dev->parent_overlap = overlap;

out:
	ret = 0;
out_err:
	kfree(reply_buf);
	rbd_spec_put(parent_spec);

	return ret;
}

static int rbd_dev_v2_striping_info(struct rbd_device *rbd_dev)
{
	struct {
		__le64 stripe_unit;
		__le64 stripe_count;
	} __attribute__ ((packed)) striping_info_buf = { 0 };
	size_t size = sizeof (striping_info_buf);
	void *p;
	u64 obj_size;
	u64 stripe_unit;
	u64 stripe_count;
	int ret;

	ret = rbd_obj_method_sync(rbd_dev, rbd_dev->header_name,
				"rbd", "get_stripe_unit_count", NULL, 0,
				(char *)&striping_info_buf, size);
	dout("%s: rbd_obj_method_sync returned %d\n", __func__, ret);
	if (ret < 0)
		return ret;
	if (ret < size)
		return -ERANGE;

	/*
	 * We don't actually support the "fancy striping" feature
	 * (STRIPINGV2) yet, but if the striping sizes are the
	 * defaults the behavior is the same as before.  So find
	 * out, and only fail if the image has non-default values.
	 */
	ret = -EINVAL;
	obj_size = (u64)1 << rbd_dev->header.obj_order;
	p = &striping_info_buf;
	stripe_unit = ceph_decode_64(&p);
	if (stripe_unit != obj_size) {
		rbd_warn(rbd_dev, "unsupported stripe unit "
				"(got %llu want %llu)",
				stripe_unit, obj_size);
		return -EINVAL;
	}
	stripe_count = ceph_decode_64(&p);
	if (stripe_count != 1) {
		rbd_warn(rbd_dev, "unsupported stripe count "
				"(got %llu want 1)", stripe_count);
		return -EINVAL;
	}
	rbd_dev->header.stripe_unit = stripe_unit;
	rbd_dev->header.stripe_count = stripe_count;

	return 0;
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

	len = strlen(rbd_dev->spec->image_id);
	image_id_size = sizeof (__le32) + len;
	image_id = kmalloc(image_id_size, GFP_KERNEL);
	if (!image_id)
		return NULL;

	p = image_id;
	end = image_id + image_id_size;
	ceph_encode_string(&p, end, rbd_dev->spec->image_id, (u32)len);

	size = sizeof (__le32) + RBD_IMAGE_NAME_LEN_MAX;
	reply_buf = kmalloc(size, GFP_KERNEL);
	if (!reply_buf)
		goto out;

	ret = rbd_obj_method_sync(rbd_dev, RBD_DIRECTORY,
				"rbd", "dir_get_name",
				image_id, image_id_size,
				reply_buf, size);
	if (ret < 0)
		goto out;
	p = reply_buf;
	end = reply_buf + ret;

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

static u64 rbd_v1_snap_id_by_name(struct rbd_device *rbd_dev, const char *name)
{
	struct ceph_snap_context *snapc = rbd_dev->header.snapc;
	const char *snap_name;
	u32 which = 0;

	/* Skip over names until we find the one we are looking for */

	snap_name = rbd_dev->header.snap_names;
	while (which < snapc->num_snaps) {
		if (!strcmp(name, snap_name))
			return snapc->snaps[which];
		snap_name += strlen(snap_name) + 1;
		which++;
	}
	return CEPH_NOSNAP;
}

static u64 rbd_v2_snap_id_by_name(struct rbd_device *rbd_dev, const char *name)
{
	struct ceph_snap_context *snapc = rbd_dev->header.snapc;
	u32 which;
	bool found = false;
	u64 snap_id;

	for (which = 0; !found && which < snapc->num_snaps; which++) {
		const char *snap_name;

		snap_id = snapc->snaps[which];
		snap_name = rbd_dev_v2_snap_name(rbd_dev, snap_id);
		if (IS_ERR(snap_name)) {
			/* ignore no-longer existing snapshots */
			if (PTR_ERR(snap_name) == -ENOENT)
				continue;
			else
				break;
		}
		found = !strcmp(name, snap_name);
		kfree(snap_name);
	}
	return found ? snap_id : CEPH_NOSNAP;
}

/*
 * Assumes name is never RBD_SNAP_HEAD_NAME; returns CEPH_NOSNAP if
 * no snapshot by that name is found, or if an error occurs.
 */
static u64 rbd_snap_id_by_name(struct rbd_device *rbd_dev, const char *name)
{
	if (rbd_dev->image_format == 1)
		return rbd_v1_snap_id_by_name(rbd_dev, name);

	return rbd_v2_snap_id_by_name(rbd_dev, name);
}

/*
 * An image being mapped will have everything but the snap id.
 */
static int rbd_spec_fill_snap_id(struct rbd_device *rbd_dev)
{
	struct rbd_spec *spec = rbd_dev->spec;

	rbd_assert(spec->pool_id != CEPH_NOPOOL && spec->pool_name);
	rbd_assert(spec->image_id && spec->image_name);
	rbd_assert(spec->snap_name);

	if (strcmp(spec->snap_name, RBD_SNAP_HEAD_NAME)) {
		u64 snap_id;

		snap_id = rbd_snap_id_by_name(rbd_dev, spec->snap_name);
		if (snap_id == CEPH_NOSNAP)
			return -ENOENT;

		spec->snap_id = snap_id;
	} else {
		spec->snap_id = CEPH_NOSNAP;
	}

	return 0;
}

/*
 * A parent image will have all ids but none of the names.
 *
 * All names in an rbd spec are dynamically allocated.  It's OK if we
 * can't figure out the name for an image id.
 */
static int rbd_spec_fill_names(struct rbd_device *rbd_dev)
{
	struct ceph_osd_client *osdc = &rbd_dev->rbd_client->client->osdc;
	struct rbd_spec *spec = rbd_dev->spec;
	const char *pool_name;
	const char *image_name;
	const char *snap_name;
	int ret;

	rbd_assert(spec->pool_id != CEPH_NOPOOL);
	rbd_assert(spec->image_id);
	rbd_assert(spec->snap_id != CEPH_NOSNAP);

	/* Get the pool name; we have to make our own copy of this */

	pool_name = ceph_pg_pool_name_by_id(osdc->osdmap, spec->pool_id);
	if (!pool_name) {
		rbd_warn(rbd_dev, "no pool with id %llu", spec->pool_id);
		return -EIO;
	}
	pool_name = kstrdup(pool_name, GFP_KERNEL);
	if (!pool_name)
		return -ENOMEM;

	/* Fetch the image name; tolerate failure here */

	image_name = rbd_dev_image_name(rbd_dev);
	if (!image_name)
		rbd_warn(rbd_dev, "unable to get image name");

	/* Fetch the snapshot name */

	snap_name = rbd_snap_name(rbd_dev, spec->snap_id);
	if (IS_ERR(snap_name)) {
		ret = PTR_ERR(snap_name);
		goto out_err;
	}

	spec->pool_name = pool_name;
	spec->image_name = image_name;
	spec->snap_name = snap_name;

	return 0;

out_err:
	kfree(image_name);
	kfree(pool_name);
	return ret;
}

static int rbd_dev_v2_snap_context(struct rbd_device *rbd_dev)
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

	ret = rbd_obj_method_sync(rbd_dev, rbd_dev->header_name,
				"rbd", "get_snapcontext", NULL, 0,
				reply_buf, size);
	dout("%s: rbd_obj_method_sync returned %d\n", __func__, ret);
	if (ret < 0)
		goto out;

	p = reply_buf;
	end = reply_buf + ret;
	ret = -ERANGE;
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
	ret = 0;

	snapc = ceph_create_snap_context(snap_count, GFP_KERNEL);
	if (!snapc) {
		ret = -ENOMEM;
		goto out;
	}
	snapc->seq = seq;
	for (i = 0; i < snap_count; i++)
		snapc->snaps[i] = ceph_decode_64(&p);

	ceph_put_snap_context(rbd_dev->header.snapc);
	rbd_dev->header.snapc = snapc;

	dout("  snap context seq = %llu, snap_count = %u\n",
		(unsigned long long)seq, (unsigned int)snap_count);
out:
	kfree(reply_buf);

	return ret;
}

static const char *rbd_dev_v2_snap_name(struct rbd_device *rbd_dev,
					u64 snap_id)
{
	size_t size;
	void *reply_buf;
	__le64 snapid;
	int ret;
	void *p;
	void *end;
	char *snap_name;

	size = sizeof (__le32) + RBD_MAX_SNAP_NAME_LEN;
	reply_buf = kmalloc(size, GFP_KERNEL);
	if (!reply_buf)
		return ERR_PTR(-ENOMEM);

	snapid = cpu_to_le64(snap_id);
	ret = rbd_obj_method_sync(rbd_dev, rbd_dev->header_name,
				"rbd", "get_snapshot_name",
				&snapid, sizeof (snapid),
				reply_buf, size);
	dout("%s: rbd_obj_method_sync returned %d\n", __func__, ret);
	if (ret < 0) {
		snap_name = ERR_PTR(ret);
		goto out;
	}

	p = reply_buf;
	end = reply_buf + ret;
	snap_name = ceph_extract_encoded_string(&p, end, NULL, GFP_KERNEL);
	if (IS_ERR(snap_name))
		goto out;

	dout("  snap_id 0x%016llx snap_name = %s\n",
		(unsigned long long)snap_id, snap_name);
out:
	kfree(reply_buf);

	return snap_name;
}

static int rbd_dev_v2_header_info(struct rbd_device *rbd_dev)
{
	bool first_time = rbd_dev->header.object_prefix == NULL;
	int ret;

	ret = rbd_dev_v2_image_size(rbd_dev);
	if (ret)
		return ret;

	if (first_time) {
		ret = rbd_dev_v2_header_onetime(rbd_dev);
		if (ret)
			return ret;
	}

	ret = rbd_dev_v2_snap_context(rbd_dev);
	if (ret && first_time) {
		kfree(rbd_dev->header.object_prefix);
		rbd_dev->header.object_prefix = NULL;
	}

	return ret;
}

static int rbd_dev_header_info(struct rbd_device *rbd_dev)
{
	rbd_assert(rbd_image_format_valid(rbd_dev->image_format));

	if (rbd_dev->image_format == 1)
		return rbd_dev_v1_header_info(rbd_dev);

	return rbd_dev_v2_header_info(rbd_dev);
}

/*
 * Get a unique rbd identifier for the given new rbd_dev, and add
 * the rbd_dev to the global list.
 */
static int rbd_dev_id_get(struct rbd_device *rbd_dev)
{
	int new_dev_id;

	new_dev_id = ida_simple_get(&rbd_dev_id_ida,
				    0, minor_to_rbd_dev_id(1 << MINORBITS),
				    GFP_KERNEL);
	if (new_dev_id < 0)
		return new_dev_id;

	rbd_dev->dev_id = new_dev_id;

	spin_lock(&rbd_dev_list_lock);
	list_add_tail(&rbd_dev->node, &rbd_dev_list);
	spin_unlock(&rbd_dev_list_lock);

	dout("rbd_dev %p given dev id %d\n", rbd_dev, rbd_dev->dev_id);

	return 0;
}

/*
 * Remove an rbd_dev from the global list, and record that its
 * identifier is no longer in use.
 */
static void rbd_dev_id_put(struct rbd_device *rbd_dev)
{
	spin_lock(&rbd_dev_list_lock);
	list_del_init(&rbd_dev->node);
	spin_unlock(&rbd_dev_list_lock);

	ida_simple_remove(&rbd_dev_id_ida, rbd_dev->dev_id);

	dout("rbd_dev %p released dev id %d\n", rbd_dev, rbd_dev->dev_id);
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
	dup = kmemdup(*buf, len + 1, GFP_KERNEL);
	if (!dup)
		return NULL;
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
	char *snap_name;
	size_t mon_addrs_size;
	struct rbd_spec *spec = NULL;
	struct rbd_options *rbd_opts = NULL;
	struct ceph_options *copts;
	int ret;

	/* The first four tokens are required */

	len = next_token(&buf);
	if (!len) {
		rbd_warn(NULL, "no monitor address(es) provided");
		return -EINVAL;
	}
	mon_addrs = buf;
	mon_addrs_size = len + 1;
	buf += len;

	ret = -EINVAL;
	options = dup_token(&buf, NULL);
	if (!options)
		return -ENOMEM;
	if (!*options) {
		rbd_warn(NULL, "no options provided");
		goto out_err;
	}

	spec = rbd_spec_alloc();
	if (!spec)
		goto out_mem;

	spec->pool_name = dup_token(&buf, NULL);
	if (!spec->pool_name)
		goto out_mem;
	if (!*spec->pool_name) {
		rbd_warn(NULL, "no pool name provided");
		goto out_err;
	}

	spec->image_name = dup_token(&buf, NULL);
	if (!spec->image_name)
		goto out_mem;
	if (!*spec->image_name) {
		rbd_warn(NULL, "no image name provided");
		goto out_err;
	}

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
	snap_name = kmemdup(buf, len + 1, GFP_KERNEL);
	if (!snap_name)
		goto out_mem;
	*(snap_name + len) = '\0';
	spec->snap_name = snap_name;

	/* Initialize all rbd options to the defaults */

	rbd_opts = kzalloc(sizeof (*rbd_opts), GFP_KERNEL);
	if (!rbd_opts)
		goto out_mem;

	rbd_opts->read_only = RBD_READ_ONLY_DEFAULT;
	rbd_opts->queue_depth = RBD_QUEUE_DEPTH_DEFAULT;

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
 * Return pool id (>= 0) or a negative error code.
 */
static int rbd_add_get_pool_id(struct rbd_client *rbdc, const char *pool_name)
{
	struct ceph_options *opts = rbdc->client->options;
	u64 newest_epoch;
	int tries = 0;
	int ret;

again:
	ret = ceph_pg_poolid_by_name(rbdc->client->osdc.osdmap, pool_name);
	if (ret == -ENOENT && tries++ < 1) {
		ret = ceph_monc_do_get_version(&rbdc->client->monc, "osdmap",
					       &newest_epoch);
		if (ret < 0)
			return ret;

		if (rbdc->client->osdc.osdmap->epoch < newest_epoch) {
			ceph_monc_request_next_osdmap(&rbdc->client->monc);
			(void) ceph_monc_wait_osdmap(&rbdc->client->monc,
						     newest_epoch,
						     opts->mount_timeout);
			goto again;
		} else {
			/* the osdmap we have is new enough */
			return -ENOENT;
		}
	}

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
	char *image_id;

	/*
	 * When probing a parent image, the image id is already
	 * known (and the image name likely is not).  There's no
	 * need to fetch the image id again in this case.  We
	 * do still need to set the image format though.
	 */
	if (rbd_dev->spec->image_id) {
		rbd_dev->image_format = *rbd_dev->spec->image_id ? 2 : 1;

		return 0;
	}

	/*
	 * First, see if the format 2 image id file exists, and if
	 * so, get the image's persistent id from it.
	 */
	size = sizeof (RBD_ID_PREFIX) + strlen(rbd_dev->spec->image_name);
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

	/* If it doesn't exist we'll assume it's a format 1 image */

	ret = rbd_obj_method_sync(rbd_dev, object_name,
				"rbd", "get_id", NULL, 0,
				response, RBD_IMAGE_ID_LEN_MAX);
	dout("%s: rbd_obj_method_sync returned %d\n", __func__, ret);
	if (ret == -ENOENT) {
		image_id = kstrdup("", GFP_KERNEL);
		ret = image_id ? 0 : -ENOMEM;
		if (!ret)
			rbd_dev->image_format = 1;
	} else if (ret >= 0) {
		void *p = response;

		image_id = ceph_extract_encoded_string(&p, p + ret,
						NULL, GFP_NOIO);
		ret = PTR_ERR_OR_ZERO(image_id);
		if (!ret)
			rbd_dev->image_format = 2;
	}

	if (!ret) {
		rbd_dev->spec->image_id = image_id;
		dout("image_id is %s\n", image_id);
	}
out:
	kfree(response);
	kfree(object_name);

	return ret;
}

/*
 * Undo whatever state changes are made by v1 or v2 header info
 * call.
 */
static void rbd_dev_unprobe(struct rbd_device *rbd_dev)
{
	struct rbd_image_header	*header;

	rbd_dev_parent_put(rbd_dev);

	/* Free dynamic fields from the header, then zero it out */

	header = &rbd_dev->header;
	ceph_put_snap_context(header->snapc);
	kfree(header->snap_sizes);
	kfree(header->snap_names);
	kfree(header->object_prefix);
	memset(header, 0, sizeof (*header));
}

static int rbd_dev_v2_header_onetime(struct rbd_device *rbd_dev)
{
	int ret;

	ret = rbd_dev_v2_object_prefix(rbd_dev);
	if (ret)
		goto out_err;

	/*
	 * Get the and check features for the image.  Currently the
	 * features are assumed to never change.
	 */
	ret = rbd_dev_v2_features(rbd_dev);
	if (ret)
		goto out_err;

	/* If the image supports fancy striping, get its parameters */

	if (rbd_dev->header.features & RBD_FEATURE_STRIPINGV2) {
		ret = rbd_dev_v2_striping_info(rbd_dev);
		if (ret < 0)
			goto out_err;
	}
	/* No support for crypto and compression type format 2 images */

	return 0;
out_err:
	rbd_dev->header.features = 0;
	kfree(rbd_dev->header.object_prefix);
	rbd_dev->header.object_prefix = NULL;

	return ret;
}

/*
 * @depth is rbd_dev_image_probe() -> rbd_dev_probe_parent() ->
 * rbd_dev_image_probe() recursion depth, which means it's also the
 * length of the already discovered part of the parent chain.
 */
static int rbd_dev_probe_parent(struct rbd_device *rbd_dev, int depth)
{
	struct rbd_device *parent = NULL;
	int ret;

	if (!rbd_dev->parent_spec)
		return 0;

	if (++depth > RBD_MAX_PARENT_CHAIN_LEN) {
		pr_info("parent chain is too long (%d)\n", depth);
		ret = -EINVAL;
		goto out_err;
	}

	parent = rbd_dev_create(rbd_dev->rbd_client, rbd_dev->parent_spec,
				NULL);
	if (!parent) {
		ret = -ENOMEM;
		goto out_err;
	}

	/*
	 * Images related by parent/child relationships always share
	 * rbd_client and spec/parent_spec, so bump their refcounts.
	 */
	__rbd_get_client(rbd_dev->rbd_client);
	rbd_spec_get(rbd_dev->parent_spec);

	ret = rbd_dev_image_probe(parent, depth);
	if (ret < 0)
		goto out_err;

	rbd_dev->parent = parent;
	atomic_set(&rbd_dev->parent_ref, 1);
	return 0;

out_err:
	rbd_dev_unparent(rbd_dev);
	if (parent)
		rbd_dev_destroy(parent);
	return ret;
}

static int rbd_dev_device_setup(struct rbd_device *rbd_dev)
{
	int ret;

	/* Get an id and fill in device name. */

	ret = rbd_dev_id_get(rbd_dev);
	if (ret)
		return ret;

	BUILD_BUG_ON(DEV_NAME_LEN
			< sizeof (RBD_DRV_NAME) + MAX_INT_FORMAT_WIDTH);
	sprintf(rbd_dev->name, "%s%d", RBD_DRV_NAME, rbd_dev->dev_id);

	/* Record our major and minor device numbers. */

	if (!single_major) {
		ret = register_blkdev(0, rbd_dev->name);
		if (ret < 0)
			goto err_out_id;

		rbd_dev->major = ret;
		rbd_dev->minor = 0;
	} else {
		rbd_dev->major = rbd_major;
		rbd_dev->minor = rbd_dev_id_to_minor(rbd_dev->dev_id);
	}

	/* Set up the blkdev mapping. */

	ret = rbd_init_disk(rbd_dev);
	if (ret)
		goto err_out_blkdev;

	ret = rbd_dev_mapping_set(rbd_dev);
	if (ret)
		goto err_out_disk;

	set_capacity(rbd_dev->disk, rbd_dev->mapping.size / SECTOR_SIZE);
	set_disk_ro(rbd_dev->disk, rbd_dev->mapping.read_only);

	dev_set_name(&rbd_dev->dev, "%d", rbd_dev->dev_id);
	ret = device_add(&rbd_dev->dev);
	if (ret)
		goto err_out_mapping;

	/* Everything's ready.  Announce the disk to the world. */

	set_bit(RBD_DEV_FLAG_EXISTS, &rbd_dev->flags);
	add_disk(rbd_dev->disk);

	pr_info("%s: added with size 0x%llx\n", rbd_dev->disk->disk_name,
		(unsigned long long) rbd_dev->mapping.size);

	return ret;

err_out_mapping:
	rbd_dev_mapping_clear(rbd_dev);
err_out_disk:
	rbd_free_disk(rbd_dev);
err_out_blkdev:
	if (!single_major)
		unregister_blkdev(rbd_dev->major, rbd_dev->name);
err_out_id:
	rbd_dev_id_put(rbd_dev);
	return ret;
}

static int rbd_dev_header_name(struct rbd_device *rbd_dev)
{
	struct rbd_spec *spec = rbd_dev->spec;
	size_t size;

	/* Record the header object name for this rbd image. */

	rbd_assert(rbd_image_format_valid(rbd_dev->image_format));

	if (rbd_dev->image_format == 1)
		size = strlen(spec->image_name) + sizeof (RBD_SUFFIX);
	else
		size = sizeof (RBD_HEADER_PREFIX) + strlen(spec->image_id);

	rbd_dev->header_name = kmalloc(size, GFP_KERNEL);
	if (!rbd_dev->header_name)
		return -ENOMEM;

	if (rbd_dev->image_format == 1)
		sprintf(rbd_dev->header_name, "%s%s",
			spec->image_name, RBD_SUFFIX);
	else
		sprintf(rbd_dev->header_name, "%s%s",
			RBD_HEADER_PREFIX, spec->image_id);
	return 0;
}

static void rbd_dev_image_release(struct rbd_device *rbd_dev)
{
	rbd_dev_unprobe(rbd_dev);
	kfree(rbd_dev->header_name);
	rbd_dev->header_name = NULL;
	rbd_dev->image_format = 0;
	kfree(rbd_dev->spec->image_id);
	rbd_dev->spec->image_id = NULL;

	rbd_dev_destroy(rbd_dev);
}

/*
 * Probe for the existence of the header object for the given rbd
 * device.  If this image is the one being mapped (i.e., not a
 * parent), initiate a watch on its header object before using that
 * object to get detailed information about the rbd image.
 */
static int rbd_dev_image_probe(struct rbd_device *rbd_dev, int depth)
{
	int ret;

	/*
	 * Get the id from the image id object.  Unless there's an
	 * error, rbd_dev->spec->image_id will be filled in with
	 * a dynamically-allocated string, and rbd_dev->image_format
	 * will be set to either 1 or 2.
	 */
	ret = rbd_dev_image_id(rbd_dev);
	if (ret)
		return ret;

	ret = rbd_dev_header_name(rbd_dev);
	if (ret)
		goto err_out_format;

	if (!depth) {
		ret = rbd_dev_header_watch_sync(rbd_dev);
		if (ret) {
			if (ret == -ENOENT)
				pr_info("image %s/%s does not exist\n",
					rbd_dev->spec->pool_name,
					rbd_dev->spec->image_name);
			goto out_header_name;
		}
	}

	ret = rbd_dev_header_info(rbd_dev);
	if (ret)
		goto err_out_watch;

	/*
	 * If this image is the one being mapped, we have pool name and
	 * id, image name and id, and snap name - need to fill snap id.
	 * Otherwise this is a parent image, identified by pool, image
	 * and snap ids - need to fill in names for those ids.
	 */
	if (!depth)
		ret = rbd_spec_fill_snap_id(rbd_dev);
	else
		ret = rbd_spec_fill_names(rbd_dev);
	if (ret) {
		if (ret == -ENOENT)
			pr_info("snap %s/%s@%s does not exist\n",
				rbd_dev->spec->pool_name,
				rbd_dev->spec->image_name,
				rbd_dev->spec->snap_name);
		goto err_out_probe;
	}

	if (rbd_dev->header.features & RBD_FEATURE_LAYERING) {
		ret = rbd_dev_v2_parent_info(rbd_dev);
		if (ret)
			goto err_out_probe;

		/*
		 * Need to warn users if this image is the one being
		 * mapped and has a parent.
		 */
		if (!depth && rbd_dev->parent_spec)
			rbd_warn(rbd_dev,
				 "WARNING: kernel layering is EXPERIMENTAL!");
	}

	ret = rbd_dev_probe_parent(rbd_dev, depth);
	if (ret)
		goto err_out_probe;

	dout("discovered format %u image, header name is %s\n",
		rbd_dev->image_format, rbd_dev->header_name);
	return 0;

err_out_probe:
	rbd_dev_unprobe(rbd_dev);
err_out_watch:
	if (!depth)
		rbd_dev_header_unwatch_sync(rbd_dev);
out_header_name:
	kfree(rbd_dev->header_name);
	rbd_dev->header_name = NULL;
err_out_format:
	rbd_dev->image_format = 0;
	kfree(rbd_dev->spec->image_id);
	rbd_dev->spec->image_id = NULL;
	return ret;
}

static ssize_t do_rbd_add(struct bus_type *bus,
			  const char *buf,
			  size_t count)
{
	struct rbd_device *rbd_dev = NULL;
	struct ceph_options *ceph_opts = NULL;
	struct rbd_options *rbd_opts = NULL;
	struct rbd_spec *spec = NULL;
	struct rbd_client *rbdc;
	bool read_only;
	int rc;

	if (!try_module_get(THIS_MODULE))
		return -ENODEV;

	/* parse add command */
	rc = rbd_add_parse_args(buf, &ceph_opts, &rbd_opts, &spec);
	if (rc < 0)
		goto out;

	rbdc = rbd_get_client(ceph_opts);
	if (IS_ERR(rbdc)) {
		rc = PTR_ERR(rbdc);
		goto err_out_args;
	}

	/* pick the pool */
	rc = rbd_add_get_pool_id(rbdc, spec->pool_name);
	if (rc < 0) {
		if (rc == -ENOENT)
			pr_info("pool %s does not exist\n", spec->pool_name);
		goto err_out_client;
	}
	spec->pool_id = (u64)rc;

	/* The ceph file layout needs to fit pool id in 32 bits */

	if (spec->pool_id > (u64)U32_MAX) {
		rbd_warn(NULL, "pool id too large (%llu > %u)",
				(unsigned long long)spec->pool_id, U32_MAX);
		rc = -EIO;
		goto err_out_client;
	}

	rbd_dev = rbd_dev_create(rbdc, spec, rbd_opts);
	if (!rbd_dev) {
		rc = -ENOMEM;
		goto err_out_client;
	}
	rbdc = NULL;		/* rbd_dev now owns this */
	spec = NULL;		/* rbd_dev now owns this */
	rbd_opts = NULL;	/* rbd_dev now owns this */

	rc = rbd_dev_image_probe(rbd_dev, 0);
	if (rc < 0)
		goto err_out_rbd_dev;

	/* If we are mapping a snapshot it must be marked read-only */

	read_only = rbd_dev->opts->read_only;
	if (rbd_dev->spec->snap_id != CEPH_NOSNAP)
		read_only = true;
	rbd_dev->mapping.read_only = read_only;

	rc = rbd_dev_device_setup(rbd_dev);
	if (rc) {
		/*
		 * rbd_dev_header_unwatch_sync() can't be moved into
		 * rbd_dev_image_release() without refactoring, see
		 * commit 1f3ef78861ac.
		 */
		rbd_dev_header_unwatch_sync(rbd_dev);
		rbd_dev_image_release(rbd_dev);
		goto out;
	}

	rc = count;
out:
	module_put(THIS_MODULE);
	return rc;

err_out_rbd_dev:
	rbd_dev_destroy(rbd_dev);
err_out_client:
	rbd_put_client(rbdc);
err_out_args:
	rbd_spec_put(spec);
	kfree(rbd_opts);
	goto out;
}

static ssize_t rbd_add(struct bus_type *bus,
		       const char *buf,
		       size_t count)
{
	if (single_major)
		return -EINVAL;

	return do_rbd_add(bus, buf, count);
}

static ssize_t rbd_add_single_major(struct bus_type *bus,
				    const char *buf,
				    size_t count)
{
	return do_rbd_add(bus, buf, count);
}

static void rbd_dev_device_release(struct rbd_device *rbd_dev)
{
	rbd_free_disk(rbd_dev);
	clear_bit(RBD_DEV_FLAG_EXISTS, &rbd_dev->flags);
	device_del(&rbd_dev->dev);
	rbd_dev_mapping_clear(rbd_dev);
	if (!single_major)
		unregister_blkdev(rbd_dev->major, rbd_dev->name);
	rbd_dev_id_put(rbd_dev);
}

static void rbd_dev_remove_parent(struct rbd_device *rbd_dev)
{
	while (rbd_dev->parent) {
		struct rbd_device *first = rbd_dev;
		struct rbd_device *second = first->parent;
		struct rbd_device *third;

		/*
		 * Follow to the parent with no grandparent and
		 * remove it.
		 */
		while (second && (third = second->parent)) {
			first = second;
			second = third;
		}
		rbd_assert(second);
		rbd_dev_image_release(second);
		first->parent = NULL;
		first->parent_overlap = 0;

		rbd_assert(first->parent_spec);
		rbd_spec_put(first->parent_spec);
		first->parent_spec = NULL;
	}
}

static ssize_t do_rbd_remove(struct bus_type *bus,
			     const char *buf,
			     size_t count)
{
	struct rbd_device *rbd_dev = NULL;
	struct list_head *tmp;
	int dev_id;
	unsigned long ul;
	bool already = false;
	int ret;

	ret = kstrtoul(buf, 10, &ul);
	if (ret)
		return ret;

	/* convert to int; abort if we lost anything in the conversion */
	dev_id = (int)ul;
	if (dev_id != ul)
		return -EINVAL;

	ret = -ENOENT;
	spin_lock(&rbd_dev_list_lock);
	list_for_each(tmp, &rbd_dev_list) {
		rbd_dev = list_entry(tmp, struct rbd_device, node);
		if (rbd_dev->dev_id == dev_id) {
			ret = 0;
			break;
		}
	}
	if (!ret) {
		spin_lock_irq(&rbd_dev->lock);
		if (rbd_dev->open_count)
			ret = -EBUSY;
		else
			already = test_and_set_bit(RBD_DEV_FLAG_REMOVING,
							&rbd_dev->flags);
		spin_unlock_irq(&rbd_dev->lock);
	}
	spin_unlock(&rbd_dev_list_lock);
	if (ret < 0 || already)
		return ret;

	rbd_dev_header_unwatch_sync(rbd_dev);
	/*
	 * flush remaining watch callbacks - these must be complete
	 * before the osd_client is shutdown
	 */
	dout("%s: flushing notifies", __func__);
	ceph_osdc_flush_notifies(&rbd_dev->rbd_client->client->osdc);

	/*
	 * Don't free anything from rbd_dev->disk until after all
	 * notifies are completely processed. Otherwise
	 * rbd_bus_del_dev() will race with rbd_watch_cb(), resulting
	 * in a potential use after free of rbd_dev->disk or rbd_dev.
	 */
	rbd_dev_device_release(rbd_dev);
	rbd_dev_image_release(rbd_dev);

	return count;
}

static ssize_t rbd_remove(struct bus_type *bus,
			  const char *buf,
			  size_t count)
{
	if (single_major)
		return -EINVAL;

	return do_rbd_remove(bus, buf, count);
}

static ssize_t rbd_remove_single_major(struct bus_type *bus,
				       const char *buf,
				       size_t count)
{
	return do_rbd_remove(bus, buf, count);
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

static int rbd_slab_init(void)
{
	rbd_assert(!rbd_img_request_cache);
	rbd_img_request_cache = kmem_cache_create("rbd_img_request",
					sizeof (struct rbd_img_request),
					__alignof__(struct rbd_img_request),
					0, NULL);
	if (!rbd_img_request_cache)
		return -ENOMEM;

	rbd_assert(!rbd_obj_request_cache);
	rbd_obj_request_cache = kmem_cache_create("rbd_obj_request",
					sizeof (struct rbd_obj_request),
					__alignof__(struct rbd_obj_request),
					0, NULL);
	if (!rbd_obj_request_cache)
		goto out_err;

	rbd_assert(!rbd_segment_name_cache);
	rbd_segment_name_cache = kmem_cache_create("rbd_segment_name",
					CEPH_MAX_OID_NAME_LEN + 1, 1, 0, NULL);
	if (rbd_segment_name_cache)
		return 0;
out_err:
	kmem_cache_destroy(rbd_obj_request_cache);
	rbd_obj_request_cache = NULL;

	kmem_cache_destroy(rbd_img_request_cache);
	rbd_img_request_cache = NULL;

	return -ENOMEM;
}

static void rbd_slab_exit(void)
{
	rbd_assert(rbd_segment_name_cache);
	kmem_cache_destroy(rbd_segment_name_cache);
	rbd_segment_name_cache = NULL;

	rbd_assert(rbd_obj_request_cache);
	kmem_cache_destroy(rbd_obj_request_cache);
	rbd_obj_request_cache = NULL;

	rbd_assert(rbd_img_request_cache);
	kmem_cache_destroy(rbd_img_request_cache);
	rbd_img_request_cache = NULL;
}

static int __init rbd_init(void)
{
	int rc;

	if (!libceph_compatible(NULL)) {
		rbd_warn(NULL, "libceph incompatibility (quitting)");
		return -EINVAL;
	}

	rc = rbd_slab_init();
	if (rc)
		return rc;

	/*
	 * The number of active work items is limited by the number of
	 * rbd devices * queue depth, so leave @max_active at default.
	 */
	rbd_wq = alloc_workqueue(RBD_DRV_NAME, WQ_MEM_RECLAIM, 0);
	if (!rbd_wq) {
		rc = -ENOMEM;
		goto err_out_slab;
	}

	if (single_major) {
		rbd_major = register_blkdev(0, RBD_DRV_NAME);
		if (rbd_major < 0) {
			rc = rbd_major;
			goto err_out_wq;
		}
	}

	rc = rbd_sysfs_init();
	if (rc)
		goto err_out_blkdev;

	if (single_major)
		pr_info("loaded (major %d)\n", rbd_major);
	else
		pr_info("loaded\n");

	return 0;

err_out_blkdev:
	if (single_major)
		unregister_blkdev(rbd_major, RBD_DRV_NAME);
err_out_wq:
	destroy_workqueue(rbd_wq);
err_out_slab:
	rbd_slab_exit();
	return rc;
}

static void __exit rbd_exit(void)
{
	ida_destroy(&rbd_dev_id_ida);
	rbd_sysfs_cleanup();
	if (single_major)
		unregister_blkdev(rbd_major, RBD_DRV_NAME);
	destroy_workqueue(rbd_wq);
	rbd_slab_exit();
}

module_init(rbd_init);
module_exit(rbd_exit);

MODULE_AUTHOR("Alex Elder <elder@inktank.com>");
MODULE_AUTHOR("Sage Weil <sage@newdream.net>");
MODULE_AUTHOR("Yehuda Sadeh <yehuda@hq.newdream.net>");
/* following authorship retained from original osdblk.c */
MODULE_AUTHOR("Jeff Garzik <jeff@garzik.org>");

MODULE_DESCRIPTION("RADOS Block Device (RBD) driver");
MODULE_LICENSE("GPL");
