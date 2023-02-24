
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
#include <linux/ceph/cls_lock_client.h>
#include <linux/ceph/striper.h>
#include <linux/ceph/decode.h>
#include <linux/fs_parser.h>
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
 * Increment the given counter and return its updated value.
 * If the counter is already 0 it will not be incremented.
 * If the counter is already at its maximum value returns
 * -EINVAL without updating it.
 */
static int atomic_inc_return_safe(atomic_t *v)
{
	unsigned int counter;

	counter = (unsigned int)atomic_fetch_add_unless(v, 1, 0);
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

#define RBD_NOTIFY_TIMEOUT	5	/* seconds */
#define RBD_RETRY_DELAY		msecs_to_jiffies(1000)

/* Feature bits */

#define RBD_FEATURE_LAYERING		(1ULL<<0)
#define RBD_FEATURE_STRIPINGV2		(1ULL<<1)
#define RBD_FEATURE_EXCLUSIVE_LOCK	(1ULL<<2)
#define RBD_FEATURE_OBJECT_MAP		(1ULL<<3)
#define RBD_FEATURE_FAST_DIFF		(1ULL<<4)
#define RBD_FEATURE_DEEP_FLATTEN	(1ULL<<5)
#define RBD_FEATURE_DATA_POOL		(1ULL<<7)
#define RBD_FEATURE_OPERATIONS		(1ULL<<8)

#define RBD_FEATURES_ALL	(RBD_FEATURE_LAYERING |		\
				 RBD_FEATURE_STRIPINGV2 |	\
				 RBD_FEATURE_EXCLUSIVE_LOCK |	\
				 RBD_FEATURE_OBJECT_MAP |	\
				 RBD_FEATURE_FAST_DIFF |	\
				 RBD_FEATURE_DEEP_FLATTEN |	\
				 RBD_FEATURE_DATA_POOL |	\
				 RBD_FEATURE_OPERATIONS)

/* Features supported by this (client software) implementation. */

#define RBD_FEATURES_SUPPORTED	(RBD_FEATURES_ALL)

/*
 * An RBD device name will be "rbd#", where the "rbd" comes from
 * RBD_DRV_NAME above, and # is a unique integer identifier.
 */
#define DEV_NAME_LEN		32

/*
 * block device image metadata (in-memory version)
 */
struct rbd_image_header {
	/* These six fields never change for a given rbd image */
	char *object_prefix;
	__u8 obj_order;
	u64 stripe_unit;
	u64 stripe_count;
	s64 data_pool_id;
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
	const char	*pool_ns;	/* NULL if default, never "" */

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

struct pending_result {
	int			result;		/* first nonzero result */
	int			num_pending;
};

struct rbd_img_request;

enum obj_request_type {
	OBJ_REQUEST_NODATA = 1,
	OBJ_REQUEST_BIO,	/* pointer into provided bio (list) */
	OBJ_REQUEST_BVECS,	/* pointer into provided bio_vec array */
	OBJ_REQUEST_OWN_BVECS,	/* private bio_vec array, doesn't own pages */
};

enum obj_operation_type {
	OBJ_OP_READ = 1,
	OBJ_OP_WRITE,
	OBJ_OP_DISCARD,
	OBJ_OP_ZEROOUT,
};

#define RBD_OBJ_FLAG_DELETION			(1U << 0)
#define RBD_OBJ_FLAG_COPYUP_ENABLED		(1U << 1)
#define RBD_OBJ_FLAG_COPYUP_ZEROS		(1U << 2)
#define RBD_OBJ_FLAG_MAY_EXIST			(1U << 3)
#define RBD_OBJ_FLAG_NOOP_FOR_NONEXISTENT	(1U << 4)

enum rbd_obj_read_state {
	RBD_OBJ_READ_START = 1,
	RBD_OBJ_READ_OBJECT,
	RBD_OBJ_READ_PARENT,
};

/*
 * Writes go through the following state machine to deal with
 * layering:
 *
 *            . . . . . RBD_OBJ_WRITE_GUARD. . . . . . . . . . . . . .
 *            .                 |                                    .
 *            .                 v                                    .
 *            .    RBD_OBJ_WRITE_READ_FROM_PARENT. . .               .
 *            .                 |                    .               .
 *            .                 v                    v (deep-copyup  .
 *    (image  .   RBD_OBJ_WRITE_COPYUP_EMPTY_SNAPC   .  not needed)  .
 * flattened) v                 |                    .               .
 *            .                 v                    .               .
 *            . . . .RBD_OBJ_WRITE_COPYUP_OPS. . . . .      (copyup  .
 *                              |                        not needed) v
 *                              v                                    .
 *                            done . . . . . . . . . . . . . . . . . .
 *                              ^
 *                              |
 *                     RBD_OBJ_WRITE_FLAT
 *
 * Writes start in RBD_OBJ_WRITE_GUARD or _FLAT, depending on whether
 * assert_exists guard is needed or not (in some cases it's not needed
 * even if there is a parent).
 */
enum rbd_obj_write_state {
	RBD_OBJ_WRITE_START = 1,
	RBD_OBJ_WRITE_PRE_OBJECT_MAP,
	RBD_OBJ_WRITE_OBJECT,
	__RBD_OBJ_WRITE_COPYUP,
	RBD_OBJ_WRITE_COPYUP,
	RBD_OBJ_WRITE_POST_OBJECT_MAP,
};

enum rbd_obj_copyup_state {
	RBD_OBJ_COPYUP_START = 1,
	RBD_OBJ_COPYUP_READ_PARENT,
	__RBD_OBJ_COPYUP_OBJECT_MAPS,
	RBD_OBJ_COPYUP_OBJECT_MAPS,
	__RBD_OBJ_COPYUP_WRITE_OBJECT,
	RBD_OBJ_COPYUP_WRITE_OBJECT,
};

struct rbd_obj_request {
	struct ceph_object_extent ex;
	unsigned int		flags;	/* RBD_OBJ_FLAG_* */
	union {
		enum rbd_obj_read_state	 read_state;	/* for reads */
		enum rbd_obj_write_state write_state;	/* for writes */
	};

	struct rbd_img_request	*img_request;
	struct ceph_file_extent	*img_extents;
	u32			num_img_extents;

	union {
		struct ceph_bio_iter	bio_pos;
		struct {
			struct ceph_bvec_iter	bvec_pos;
			u32			bvec_count;
			u32			bvec_idx;
		};
	};

	enum rbd_obj_copyup_state copyup_state;
	struct bio_vec		*copyup_bvecs;
	u32			copyup_bvec_count;

	struct list_head	osd_reqs;	/* w/ r_private_item */

	struct mutex		state_mutex;
	struct pending_result	pending;
	struct kref		kref;
};

enum img_req_flags {
	IMG_REQ_CHILD,		/* initiator: block = 0, child image = 1 */
	IMG_REQ_LAYERED,	/* ENOENT handling: normal = 0, layered = 1 */
};

enum rbd_img_state {
	RBD_IMG_START = 1,
	RBD_IMG_EXCLUSIVE_LOCK,
	__RBD_IMG_OBJECT_REQUESTS,
	RBD_IMG_OBJECT_REQUESTS,
};

struct rbd_img_request {
	struct rbd_device	*rbd_dev;
	enum obj_operation_type	op_type;
	enum obj_request_type	data_type;
	unsigned long		flags;
	enum rbd_img_state	state;
	union {
		u64			snap_id;	/* for reads */
		struct ceph_snap_context *snapc;	/* for writes */
	};
	struct rbd_obj_request	*obj_request;	/* obj req initiator */

	struct list_head	lock_item;
	struct list_head	object_extents;	/* obj_req.ex structs */

	struct mutex		state_mutex;
	struct pending_result	pending;
	struct work_struct	work;
	int			work_result;
};

#define for_each_obj_request(ireq, oreq) \
	list_for_each_entry(oreq, &(ireq)->object_extents, ex.oe_item)
#define for_each_obj_request_safe(ireq, oreq, n) \
	list_for_each_entry_safe(oreq, n, &(ireq)->object_extents, ex.oe_item)

enum rbd_watch_state {
	RBD_WATCH_STATE_UNREGISTERED,
	RBD_WATCH_STATE_REGISTERED,
	RBD_WATCH_STATE_ERROR,
};

enum rbd_lock_state {
	RBD_LOCK_STATE_UNLOCKED,
	RBD_LOCK_STATE_LOCKED,
	RBD_LOCK_STATE_RELEASING,
};

/* WatchNotify::ClientId */
struct rbd_client_id {
	u64 gid;
	u64 handle;
};

struct rbd_mapping {
	u64                     size;
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
	char			*config_info;	/* add{,_single_major} string */

	struct ceph_object_id	header_oid;
	struct ceph_object_locator header_oloc;

	struct ceph_file_layout	layout;		/* used for all rbd requests */

	struct mutex		watch_mutex;
	enum rbd_watch_state	watch_state;
	struct ceph_osd_linger_request *watch_handle;
	u64			watch_cookie;
	struct delayed_work	watch_dwork;

	struct rw_semaphore	lock_rwsem;
	enum rbd_lock_state	lock_state;
	char			lock_cookie[32];
	struct rbd_client_id	owner_cid;
	struct work_struct	acquired_lock_work;
	struct work_struct	released_lock_work;
	struct delayed_work	lock_dwork;
	struct work_struct	unlock_work;
	spinlock_t		lock_lists_lock;
	struct list_head	acquiring_list;
	struct list_head	running_list;
	struct completion	acquire_wait;
	int			acquire_err;
	struct completion	releasing_wait;

	spinlock_t		object_map_lock;
	u8			*object_map;
	u64			object_map_size;	/* in objects */
	u64			object_map_flags;

	struct workqueue_struct	*task_wq;

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
 * Flag bits for rbd_dev->flags:
 * - REMOVING (which is coupled with rbd_dev->open_count) is protected
 *   by rbd_dev->lock
 */
enum rbd_dev_flags {
	RBD_DEV_FLAG_EXISTS,	/* rbd_dev_device_setup() ran */
	RBD_DEV_FLAG_REMOVING,	/* this mapping is being removed */
	RBD_DEV_FLAG_READONLY,  /* -o ro or snapshot */
};

static DEFINE_MUTEX(client_mutex);	/* Serialize client creation */

static LIST_HEAD(rbd_dev_list);    /* devices */
static DEFINE_SPINLOCK(rbd_dev_list_lock);

static LIST_HEAD(rbd_client_list);		/* clients */
static DEFINE_SPINLOCK(rbd_client_list_lock);

/* Slab caches for frequently-allocated structures */

static struct kmem_cache	*rbd_img_request_cache;
static struct kmem_cache	*rbd_obj_request_cache;

static int rbd_major;
static DEFINE_IDA(rbd_dev_id_ida);

static struct workqueue_struct *rbd_wq;

static struct ceph_snap_context rbd_empty_snapc = {
	.nref = REFCOUNT_INIT(1),
};

/*
 * single-major requires >= 0.75 version of userspace rbd utility.
 */
static bool single_major = true;
module_param(single_major, bool, 0444);
MODULE_PARM_DESC(single_major, "Use a single major number for all rbd devices (default: true)");

static ssize_t add_store(struct bus_type *bus, const char *buf, size_t count);
static ssize_t remove_store(struct bus_type *bus, const char *buf,
			    size_t count);
static ssize_t add_single_major_store(struct bus_type *bus, const char *buf,
				      size_t count);
static ssize_t remove_single_major_store(struct bus_type *bus, const char *buf,
					 size_t count);
static int rbd_dev_image_probe(struct rbd_device *rbd_dev, int depth);

static int rbd_dev_id_to_minor(int dev_id)
{
	return dev_id << RBD_SINGLE_MAJOR_PART_SHIFT;
}

static int minor_to_rbd_dev_id(int minor)
{
	return minor >> RBD_SINGLE_MAJOR_PART_SHIFT;
}

static bool rbd_is_ro(struct rbd_device *rbd_dev)
{
	return test_bit(RBD_DEV_FLAG_READONLY, &rbd_dev->flags);
}

static bool rbd_is_snap(struct rbd_device *rbd_dev)
{
	return rbd_dev->spec->snap_id != CEPH_NOSNAP;
}

static bool __rbd_is_lock_owner(struct rbd_device *rbd_dev)
{
	lockdep_assert_held(&rbd_dev->lock_rwsem);

	return rbd_dev->lock_state == RBD_LOCK_STATE_LOCKED ||
	       rbd_dev->lock_state == RBD_LOCK_STATE_RELEASING;
}

static bool rbd_is_lock_owner(struct rbd_device *rbd_dev)
{
	bool is_lock_owner;

	down_read(&rbd_dev->lock_rwsem);
	is_lock_owner = __rbd_is_lock_owner(rbd_dev);
	up_read(&rbd_dev->lock_rwsem);
	return is_lock_owner;
}

static ssize_t supported_features_show(struct bus_type *bus, char *buf)
{
	return sprintf(buf, "0x%llx\n", RBD_FEATURES_SUPPORTED);
}

static BUS_ATTR_WO(add);
static BUS_ATTR_WO(remove);
static BUS_ATTR_WO(add_single_major);
static BUS_ATTR_WO(remove_single_major);
static BUS_ATTR_RO(supported_features);

static struct attribute *rbd_bus_attrs[] = {
	&bus_attr_add.attr,
	&bus_attr_remove.attr,
	&bus_attr_add_single_major.attr,
	&bus_attr_remove_single_major.attr,
	&bus_attr_supported_features.attr,
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

static void rbd_dev_remove_parent(struct rbd_device *rbd_dev);

static int rbd_dev_refresh(struct rbd_device *rbd_dev);
static int rbd_dev_v2_header_onetime(struct rbd_device *rbd_dev);
static int rbd_dev_header_info(struct rbd_device *rbd_dev);
static int rbd_dev_v2_parent_info(struct rbd_device *rbd_dev);
static const char *rbd_dev_v2_snap_name(struct rbd_device *rbd_dev,
					u64 snap_id);
static int _rbd_dev_v2_snap_size(struct rbd_device *rbd_dev, u64 snap_id,
				u8 *order, u64 *snap_size);
static int rbd_dev_v2_get_flags(struct rbd_device *rbd_dev);

static void rbd_obj_handle_request(struct rbd_obj_request *obj_req, int result);
static void rbd_img_handle_request(struct rbd_img_request *img_req, int result);

/*
 * Return true if nothing else is pending.
 */
static bool pending_result_dec(struct pending_result *pending, int *result)
{
	rbd_assert(pending->num_pending > 0);

	if (*result && !pending->result)
		pending->result = *result;
	if (--pending->num_pending)
		return false;

	*result = pending->result;
	return true;
}

static int rbd_open(struct block_device *bdev, fmode_t mode)
{
	struct rbd_device *rbd_dev = bdev->bd_disk->private_data;
	bool removing = false;

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

static const struct block_device_operations rbd_bd_ops = {
	.owner			= THIS_MODULE,
	.open			= rbd_open,
	.release		= rbd_release,
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

	rbdc->client = ceph_create_client(ceph_opts, rbdc);
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
	struct rbd_client *rbdc = NULL, *iter;

	if (ceph_opts->flags & CEPH_OPT_NOSHARE)
		return NULL;

	spin_lock(&rbd_client_list_lock);
	list_for_each_entry(iter, &rbd_client_list, node) {
		if (!ceph_compare_options(ceph_opts, iter->client)) {
			__rbd_get_client(iter);

			rbdc = iter;
			break;
		}
	}
	spin_unlock(&rbd_client_list_lock);

	return rbdc;
}

/*
 * (Per device) rbd map options
 */
enum {
	Opt_queue_depth,
	Opt_alloc_size,
	Opt_lock_timeout,
	/* int args above */
	Opt_pool_ns,
	Opt_compression_hint,
	/* string args above */
	Opt_read_only,
	Opt_read_write,
	Opt_lock_on_read,
	Opt_exclusive,
	Opt_notrim,
};

enum {
	Opt_compression_hint_none,
	Opt_compression_hint_compressible,
	Opt_compression_hint_incompressible,
};

static const struct constant_table rbd_param_compression_hint[] = {
	{"none",		Opt_compression_hint_none},
	{"compressible",	Opt_compression_hint_compressible},
	{"incompressible",	Opt_compression_hint_incompressible},
	{}
};

static const struct fs_parameter_spec rbd_parameters[] = {
	fsparam_u32	("alloc_size",			Opt_alloc_size),
	fsparam_enum	("compression_hint",		Opt_compression_hint,
			 rbd_param_compression_hint),
	fsparam_flag	("exclusive",			Opt_exclusive),
	fsparam_flag	("lock_on_read",		Opt_lock_on_read),
	fsparam_u32	("lock_timeout",		Opt_lock_timeout),
	fsparam_flag	("notrim",			Opt_notrim),
	fsparam_string	("_pool_ns",			Opt_pool_ns),
	fsparam_u32	("queue_depth",			Opt_queue_depth),
	fsparam_flag	("read_only",			Opt_read_only),
	fsparam_flag	("read_write",			Opt_read_write),
	fsparam_flag	("ro",				Opt_read_only),
	fsparam_flag	("rw",				Opt_read_write),
	{}
};

struct rbd_options {
	int	queue_depth;
	int	alloc_size;
	unsigned long	lock_timeout;
	bool	read_only;
	bool	lock_on_read;
	bool	exclusive;
	bool	trim;

	u32 alloc_hint_flags;  /* CEPH_OSD_OP_ALLOC_HINT_FLAG_* */
};

#define RBD_QUEUE_DEPTH_DEFAULT	BLKDEV_DEFAULT_RQ
#define RBD_ALLOC_SIZE_DEFAULT	(64 * 1024)
#define RBD_LOCK_TIMEOUT_DEFAULT 0  /* no timeout */
#define RBD_READ_ONLY_DEFAULT	false
#define RBD_LOCK_ON_READ_DEFAULT false
#define RBD_EXCLUSIVE_DEFAULT	false
#define RBD_TRIM_DEFAULT	true

struct rbd_parse_opts_ctx {
	struct rbd_spec		*spec;
	struct ceph_options	*copts;
	struct rbd_options	*opts;
};

static char* obj_op_name(enum obj_operation_type op_type)
{
	switch (op_type) {
	case OBJ_OP_READ:
		return "read";
	case OBJ_OP_WRITE:
		return "write";
	case OBJ_OP_DISCARD:
		return "discard";
	case OBJ_OP_ZEROOUT:
		return "zeroout";
	default:
		return "???";
	}
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

/*
 * Get a ceph client with specific addr and configuration, if one does
 * not exist create it.  Either way, ceph_opts is consumed by this
 * function.
 */
static struct rbd_client *rbd_get_client(struct ceph_options *ceph_opts)
{
	struct rbd_client *rbdc;
	int ret;

	mutex_lock(&client_mutex);
	rbdc = rbd_client_find(ceph_opts);
	if (rbdc) {
		ceph_destroy_options(ceph_opts);

		/*
		 * Using an existing client.  Make sure ->pg_pools is up to
		 * date before we look up the pool id in do_rbd_add().
		 */
		ret = ceph_wait_for_latest_osdmap(rbdc->client,
					rbdc->client->options->mount_timeout);
		if (ret) {
			rbd_warn(NULL, "failed to get latest osdmap: %d", ret);
			rbd_put_client(rbdc);
			rbdc = ERR_PTR(ret);
		}
	} else {
		rbdc = rbd_client_create(ceph_opts);
	}
	mutex_unlock(&client_mutex);

	return rbdc;
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
 * returns the size of an object in the image
 */
static u32 rbd_obj_bytes(struct rbd_image_header *header)
{
	return 1U << header->obj_order;
}

static void rbd_init_layout(struct rbd_device *rbd_dev)
{
	if (rbd_dev->header.stripe_unit == 0 ||
	    rbd_dev->header.stripe_count == 0) {
		rbd_dev->header.stripe_unit = rbd_obj_bytes(&rbd_dev->header);
		rbd_dev->header.stripe_count = 1;
	}

	rbd_dev->layout.stripe_unit = rbd_dev->header.stripe_unit;
	rbd_dev->layout.stripe_count = rbd_dev->header.stripe_count;
	rbd_dev->layout.object_size = rbd_obj_bytes(&rbd_dev->header);
	rbd_dev->layout.pool_id = rbd_dev->header.data_pool_id == CEPH_NOPOOL ?
			  rbd_dev->spec->pool_id : rbd_dev->header.data_pool_id;
	RCU_INIT_POINTER(rbd_dev->layout.pool_ns, NULL);
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
	int ret = -ENOMEM;
	u32 i;

	/* Allocate this now to avoid having to handle failure below */

	if (first_time) {
		object_prefix = kstrndup(ondisk->object_prefix,
					 sizeof(ondisk->object_prefix),
					 GFP_KERNEL);
		if (!object_prefix)
			return -ENOMEM;
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
		snap_sizes = kmalloc_array(snap_count,
					   sizeof(*header->snap_sizes),
					   GFP_KERNEL);
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
		rbd_init_layout(rbd_dev);
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

static int rbd_dev_mapping_set(struct rbd_device *rbd_dev)
{
	u64 snap_id = rbd_dev->spec->snap_id;
	u64 size = 0;
	int ret;

	ret = rbd_snap_size(rbd_dev, snap_id, &size);
	if (ret)
		return ret;

	rbd_dev->mapping.size = size;
	return 0;
}

static void rbd_dev_mapping_clear(struct rbd_device *rbd_dev)
{
	rbd_dev->mapping.size = 0;
}

static void zero_bios(struct ceph_bio_iter *bio_pos, u32 off, u32 bytes)
{
	struct ceph_bio_iter it = *bio_pos;

	ceph_bio_iter_advance(&it, off);
	ceph_bio_iter_advance_step(&it, bytes, ({
		memzero_bvec(&bv);
	}));
}

static void zero_bvecs(struct ceph_bvec_iter *bvec_pos, u32 off, u32 bytes)
{
	struct ceph_bvec_iter it = *bvec_pos;

	ceph_bvec_iter_advance(&it, off);
	ceph_bvec_iter_advance_step(&it, bytes, ({
		memzero_bvec(&bv);
	}));
}

/*
 * Zero a range in @obj_req data buffer defined by a bio (list) or
 * (private) bio_vec array.
 *
 * @off is relative to the start of the data buffer.
 */
static void rbd_obj_zero_range(struct rbd_obj_request *obj_req, u32 off,
			       u32 bytes)
{
	dout("%s %p data buf %u~%u\n", __func__, obj_req, off, bytes);

	switch (obj_req->img_request->data_type) {
	case OBJ_REQUEST_BIO:
		zero_bios(&obj_req->bio_pos, off, bytes);
		break;
	case OBJ_REQUEST_BVECS:
	case OBJ_REQUEST_OWN_BVECS:
		zero_bvecs(&obj_req->bvec_pos, off, bytes);
		break;
	default:
		BUG();
	}
}

static void rbd_obj_request_destroy(struct kref *kref);
static void rbd_obj_request_put(struct rbd_obj_request *obj_request)
{
	rbd_assert(obj_request != NULL);
	dout("%s: obj %p (was %d)\n", __func__, obj_request,
		kref_read(&obj_request->kref));
	kref_put(&obj_request->kref, rbd_obj_request_destroy);
}

static inline void rbd_img_obj_request_add(struct rbd_img_request *img_request,
					struct rbd_obj_request *obj_request)
{
	rbd_assert(obj_request->img_request == NULL);

	/* Image request now owns object's original reference */
	obj_request->img_request = img_request;
	dout("%s: img %p obj %p\n", __func__, img_request, obj_request);
}

static inline void rbd_img_obj_request_del(struct rbd_img_request *img_request,
					struct rbd_obj_request *obj_request)
{
	dout("%s: img %p obj %p\n", __func__, img_request, obj_request);
	list_del(&obj_request->ex.oe_item);
	rbd_assert(obj_request->img_request == img_request);
	rbd_obj_request_put(obj_request);
}

static void rbd_osd_submit(struct ceph_osd_request *osd_req)
{
	struct rbd_obj_request *obj_req = osd_req->r_priv;

	dout("%s osd_req %p for obj_req %p objno %llu %llu~%llu\n",
	     __func__, osd_req, obj_req, obj_req->ex.oe_objno,
	     obj_req->ex.oe_off, obj_req->ex.oe_len);
	ceph_osdc_start_request(osd_req->r_osdc, osd_req);
}

/*
 * The default/initial value for all image request flags is 0.  Each
 * is conditionally set to 1 at image request initialization time
 * and currently never change thereafter.
 */
static void img_request_layered_set(struct rbd_img_request *img_request)
{
	set_bit(IMG_REQ_LAYERED, &img_request->flags);
}

static bool img_request_layered_test(struct rbd_img_request *img_request)
{
	return test_bit(IMG_REQ_LAYERED, &img_request->flags) != 0;
}

static bool rbd_obj_is_entire(struct rbd_obj_request *obj_req)
{
	struct rbd_device *rbd_dev = obj_req->img_request->rbd_dev;

	return !obj_req->ex.oe_off &&
	       obj_req->ex.oe_len == rbd_dev->layout.object_size;
}

static bool rbd_obj_is_tail(struct rbd_obj_request *obj_req)
{
	struct rbd_device *rbd_dev = obj_req->img_request->rbd_dev;

	return obj_req->ex.oe_off + obj_req->ex.oe_len ==
					rbd_dev->layout.object_size;
}

/*
 * Must be called after rbd_obj_calc_img_extents().
 */
static bool rbd_obj_copyup_enabled(struct rbd_obj_request *obj_req)
{
	if (!obj_req->num_img_extents ||
	    (rbd_obj_is_entire(obj_req) &&
	     !obj_req->img_request->snapc->num_snaps))
		return false;

	return true;
}

static u64 rbd_obj_img_extents_bytes(struct rbd_obj_request *obj_req)
{
	return ceph_file_extents_bytes(obj_req->img_extents,
				       obj_req->num_img_extents);
}

static bool rbd_img_is_write(struct rbd_img_request *img_req)
{
	switch (img_req->op_type) {
	case OBJ_OP_READ:
		return false;
	case OBJ_OP_WRITE:
	case OBJ_OP_DISCARD:
	case OBJ_OP_ZEROOUT:
		return true;
	default:
		BUG();
	}
}

static void rbd_osd_req_callback(struct ceph_osd_request *osd_req)
{
	struct rbd_obj_request *obj_req = osd_req->r_priv;
	int result;

	dout("%s osd_req %p result %d for obj_req %p\n", __func__, osd_req,
	     osd_req->r_result, obj_req);

	/*
	 * Writes aren't allowed to return a data payload.  In some
	 * guarded write cases (e.g. stat + zero on an empty object)
	 * a stat response makes it through, but we don't care.
	 */
	if (osd_req->r_result > 0 && rbd_img_is_write(obj_req->img_request))
		result = 0;
	else
		result = osd_req->r_result;

	rbd_obj_handle_request(obj_req, result);
}

static void rbd_osd_format_read(struct ceph_osd_request *osd_req)
{
	struct rbd_obj_request *obj_request = osd_req->r_priv;
	struct rbd_device *rbd_dev = obj_request->img_request->rbd_dev;
	struct ceph_options *opt = rbd_dev->rbd_client->client->options;

	osd_req->r_flags = CEPH_OSD_FLAG_READ | opt->read_from_replica;
	osd_req->r_snapid = obj_request->img_request->snap_id;
}

static void rbd_osd_format_write(struct ceph_osd_request *osd_req)
{
	struct rbd_obj_request *obj_request = osd_req->r_priv;

	osd_req->r_flags = CEPH_OSD_FLAG_WRITE;
	ktime_get_real_ts64(&osd_req->r_mtime);
	osd_req->r_data_offset = obj_request->ex.oe_off;
}

static struct ceph_osd_request *
__rbd_obj_add_osd_request(struct rbd_obj_request *obj_req,
			  struct ceph_snap_context *snapc, int num_ops)
{
	struct rbd_device *rbd_dev = obj_req->img_request->rbd_dev;
	struct ceph_osd_client *osdc = &rbd_dev->rbd_client->client->osdc;
	struct ceph_osd_request *req;
	const char *name_format = rbd_dev->image_format == 1 ?
				      RBD_V1_DATA_FORMAT : RBD_V2_DATA_FORMAT;
	int ret;

	req = ceph_osdc_alloc_request(osdc, snapc, num_ops, false, GFP_NOIO);
	if (!req)
		return ERR_PTR(-ENOMEM);

	list_add_tail(&req->r_private_item, &obj_req->osd_reqs);
	req->r_callback = rbd_osd_req_callback;
	req->r_priv = obj_req;

	/*
	 * Data objects may be stored in a separate pool, but always in
	 * the same namespace in that pool as the header in its pool.
	 */
	ceph_oloc_copy(&req->r_base_oloc, &rbd_dev->header_oloc);
	req->r_base_oloc.pool = rbd_dev->layout.pool_id;

	ret = ceph_oid_aprintf(&req->r_base_oid, GFP_NOIO, name_format,
			       rbd_dev->header.object_prefix,
			       obj_req->ex.oe_objno);
	if (ret)
		return ERR_PTR(ret);

	return req;
}

static struct ceph_osd_request *
rbd_obj_add_osd_request(struct rbd_obj_request *obj_req, int num_ops)
{
	return __rbd_obj_add_osd_request(obj_req, obj_req->img_request->snapc,
					 num_ops);
}

static struct rbd_obj_request *rbd_obj_request_create(void)
{
	struct rbd_obj_request *obj_request;

	obj_request = kmem_cache_zalloc(rbd_obj_request_cache, GFP_NOIO);
	if (!obj_request)
		return NULL;

	ceph_object_extent_init(&obj_request->ex);
	INIT_LIST_HEAD(&obj_request->osd_reqs);
	mutex_init(&obj_request->state_mutex);
	kref_init(&obj_request->kref);

	dout("%s %p\n", __func__, obj_request);
	return obj_request;
}

static void rbd_obj_request_destroy(struct kref *kref)
{
	struct rbd_obj_request *obj_request;
	struct ceph_osd_request *osd_req;
	u32 i;

	obj_request = container_of(kref, struct rbd_obj_request, kref);

	dout("%s: obj %p\n", __func__, obj_request);

	while (!list_empty(&obj_request->osd_reqs)) {
		osd_req = list_first_entry(&obj_request->osd_reqs,
				    struct ceph_osd_request, r_private_item);
		list_del_init(&osd_req->r_private_item);
		ceph_osdc_put_request(osd_req);
	}

	switch (obj_request->img_request->data_type) {
	case OBJ_REQUEST_NODATA:
	case OBJ_REQUEST_BIO:
	case OBJ_REQUEST_BVECS:
		break;		/* Nothing to do */
	case OBJ_REQUEST_OWN_BVECS:
		kfree(obj_request->bvec_pos.bvecs);
		break;
	default:
		BUG();
	}

	kfree(obj_request->img_extents);
	if (obj_request->copyup_bvecs) {
		for (i = 0; i < obj_request->copyup_bvec_count; i++) {
			if (obj_request->copyup_bvecs[i].bv_page)
				__free_page(obj_request->copyup_bvecs[i].bv_page);
		}
		kfree(obj_request->copyup_bvecs);
	}

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

	if (rbd_dev->parent_overlap)
		counter = atomic_inc_return_safe(&rbd_dev->parent_ref);

	if (counter < 0)
		rbd_warn(rbd_dev, "parent reference overflow");

	return counter > 0;
}

static void rbd_img_request_init(struct rbd_img_request *img_request,
				 struct rbd_device *rbd_dev,
				 enum obj_operation_type op_type)
{
	memset(img_request, 0, sizeof(*img_request));

	img_request->rbd_dev = rbd_dev;
	img_request->op_type = op_type;

	INIT_LIST_HEAD(&img_request->lock_item);
	INIT_LIST_HEAD(&img_request->object_extents);
	mutex_init(&img_request->state_mutex);
}

static void rbd_img_capture_header(struct rbd_img_request *img_req)
{
	struct rbd_device *rbd_dev = img_req->rbd_dev;

	lockdep_assert_held(&rbd_dev->header_rwsem);

	if (rbd_img_is_write(img_req))
		img_req->snapc = ceph_get_snap_context(rbd_dev->header.snapc);
	else
		img_req->snap_id = rbd_dev->spec->snap_id;

	if (rbd_dev_parent_get(rbd_dev))
		img_request_layered_set(img_req);
}

static void rbd_img_request_destroy(struct rbd_img_request *img_request)
{
	struct rbd_obj_request *obj_request;
	struct rbd_obj_request *next_obj_request;

	dout("%s: img %p\n", __func__, img_request);

	WARN_ON(!list_empty(&img_request->lock_item));
	for_each_obj_request_safe(img_request, obj_request, next_obj_request)
		rbd_img_obj_request_del(img_request, obj_request);

	if (img_request_layered_test(img_request))
		rbd_dev_parent_put(img_request->rbd_dev);

	if (rbd_img_is_write(img_request))
		ceph_put_snap_context(img_request->snapc);

	if (test_bit(IMG_REQ_CHILD, &img_request->flags))
		kmem_cache_free(rbd_img_request_cache, img_request);
}

#define BITS_PER_OBJ	2
#define OBJS_PER_BYTE	(BITS_PER_BYTE / BITS_PER_OBJ)
#define OBJ_MASK	((1 << BITS_PER_OBJ) - 1)

static void __rbd_object_map_index(struct rbd_device *rbd_dev, u64 objno,
				   u64 *index, u8 *shift)
{
	u32 off;

	rbd_assert(objno < rbd_dev->object_map_size);
	*index = div_u64_rem(objno, OBJS_PER_BYTE, &off);
	*shift = (OBJS_PER_BYTE - off - 1) * BITS_PER_OBJ;
}

static u8 __rbd_object_map_get(struct rbd_device *rbd_dev, u64 objno)
{
	u64 index;
	u8 shift;

	lockdep_assert_held(&rbd_dev->object_map_lock);
	__rbd_object_map_index(rbd_dev, objno, &index, &shift);
	return (rbd_dev->object_map[index] >> shift) & OBJ_MASK;
}

static void __rbd_object_map_set(struct rbd_device *rbd_dev, u64 objno, u8 val)
{
	u64 index;
	u8 shift;
	u8 *p;

	lockdep_assert_held(&rbd_dev->object_map_lock);
	rbd_assert(!(val & ~OBJ_MASK));

	__rbd_object_map_index(rbd_dev, objno, &index, &shift);
	p = &rbd_dev->object_map[index];
	*p = (*p & ~(OBJ_MASK << shift)) | (val << shift);
}

static u8 rbd_object_map_get(struct rbd_device *rbd_dev, u64 objno)
{
	u8 state;

	spin_lock(&rbd_dev->object_map_lock);
	state = __rbd_object_map_get(rbd_dev, objno);
	spin_unlock(&rbd_dev->object_map_lock);
	return state;
}

static bool use_object_map(struct rbd_device *rbd_dev)
{
	/*
	 * An image mapped read-only can't use the object map -- it isn't
	 * loaded because the header lock isn't acquired.  Someone else can
	 * write to the image and update the object map behind our back.
	 *
	 * A snapshot can't be written to, so using the object map is always
	 * safe.
	 */
	if (!rbd_is_snap(rbd_dev) && rbd_is_ro(rbd_dev))
		return false;

	return ((rbd_dev->header.features & RBD_FEATURE_OBJECT_MAP) &&
		!(rbd_dev->object_map_flags & RBD_FLAG_OBJECT_MAP_INVALID));
}

static bool rbd_object_map_may_exist(struct rbd_device *rbd_dev, u64 objno)
{
	u8 state;

	/* fall back to default logic if object map is disabled or invalid */
	if (!use_object_map(rbd_dev))
		return true;

	state = rbd_object_map_get(rbd_dev, objno);
	return state != OBJECT_NONEXISTENT;
}

static void rbd_object_map_name(struct rbd_device *rbd_dev, u64 snap_id,
				struct ceph_object_id *oid)
{
	if (snap_id == CEPH_NOSNAP)
		ceph_oid_printf(oid, "%s%s", RBD_OBJECT_MAP_PREFIX,
				rbd_dev->spec->image_id);
	else
		ceph_oid_printf(oid, "%s%s.%016llx", RBD_OBJECT_MAP_PREFIX,
				rbd_dev->spec->image_id, snap_id);
}

static int rbd_object_map_lock(struct rbd_device *rbd_dev)
{
	struct ceph_osd_client *osdc = &rbd_dev->rbd_client->client->osdc;
	CEPH_DEFINE_OID_ONSTACK(oid);
	u8 lock_type;
	char *lock_tag;
	struct ceph_locker *lockers;
	u32 num_lockers;
	bool broke_lock = false;
	int ret;

	rbd_object_map_name(rbd_dev, CEPH_NOSNAP, &oid);

again:
	ret = ceph_cls_lock(osdc, &oid, &rbd_dev->header_oloc, RBD_LOCK_NAME,
			    CEPH_CLS_LOCK_EXCLUSIVE, "", "", "", 0);
	if (ret != -EBUSY || broke_lock) {
		if (ret == -EEXIST)
			ret = 0; /* already locked by myself */
		if (ret)
			rbd_warn(rbd_dev, "failed to lock object map: %d", ret);
		return ret;
	}

	ret = ceph_cls_lock_info(osdc, &oid, &rbd_dev->header_oloc,
				 RBD_LOCK_NAME, &lock_type, &lock_tag,
				 &lockers, &num_lockers);
	if (ret) {
		if (ret == -ENOENT)
			goto again;

		rbd_warn(rbd_dev, "failed to get object map lockers: %d", ret);
		return ret;
	}

	kfree(lock_tag);
	if (num_lockers == 0)
		goto again;

	rbd_warn(rbd_dev, "breaking object map lock owned by %s%llu",
		 ENTITY_NAME(lockers[0].id.name));

	ret = ceph_cls_break_lock(osdc, &oid, &rbd_dev->header_oloc,
				  RBD_LOCK_NAME, lockers[0].id.cookie,
				  &lockers[0].id.name);
	ceph_free_lockers(lockers, num_lockers);
	if (ret) {
		if (ret == -ENOENT)
			goto again;

		rbd_warn(rbd_dev, "failed to break object map lock: %d", ret);
		return ret;
	}

	broke_lock = true;
	goto again;
}

static void rbd_object_map_unlock(struct rbd_device *rbd_dev)
{
	struct ceph_osd_client *osdc = &rbd_dev->rbd_client->client->osdc;
	CEPH_DEFINE_OID_ONSTACK(oid);
	int ret;

	rbd_object_map_name(rbd_dev, CEPH_NOSNAP, &oid);

	ret = ceph_cls_unlock(osdc, &oid, &rbd_dev->header_oloc, RBD_LOCK_NAME,
			      "");
	if (ret && ret != -ENOENT)
		rbd_warn(rbd_dev, "failed to unlock object map: %d", ret);
}

static int decode_object_map_header(void **p, void *end, u64 *object_map_size)
{
	u8 struct_v;
	u32 struct_len;
	u32 header_len;
	void *header_end;
	int ret;

	ceph_decode_32_safe(p, end, header_len, e_inval);
	header_end = *p + header_len;

	ret = ceph_start_decoding(p, end, 1, "BitVector header", &struct_v,
				  &struct_len);
	if (ret)
		return ret;

	ceph_decode_64_safe(p, end, *object_map_size, e_inval);

	*p = header_end;
	return 0;

e_inval:
	return -EINVAL;
}

static int __rbd_object_map_load(struct rbd_device *rbd_dev)
{
	struct ceph_osd_client *osdc = &rbd_dev->rbd_client->client->osdc;
	CEPH_DEFINE_OID_ONSTACK(oid);
	struct page **pages;
	void *p, *end;
	size_t reply_len;
	u64 num_objects;
	u64 object_map_bytes;
	u64 object_map_size;
	int num_pages;
	int ret;

	rbd_assert(!rbd_dev->object_map && !rbd_dev->object_map_size);

	num_objects = ceph_get_num_objects(&rbd_dev->layout,
					   rbd_dev->mapping.size);
	object_map_bytes = DIV_ROUND_UP_ULL(num_objects * BITS_PER_OBJ,
					    BITS_PER_BYTE);
	num_pages = calc_pages_for(0, object_map_bytes) + 1;
	pages = ceph_alloc_page_vector(num_pages, GFP_KERNEL);
	if (IS_ERR(pages))
		return PTR_ERR(pages);

	reply_len = num_pages * PAGE_SIZE;
	rbd_object_map_name(rbd_dev, rbd_dev->spec->snap_id, &oid);
	ret = ceph_osdc_call(osdc, &oid, &rbd_dev->header_oloc,
			     "rbd", "object_map_load", CEPH_OSD_FLAG_READ,
			     NULL, 0, pages, &reply_len);
	if (ret)
		goto out;

	p = page_address(pages[0]);
	end = p + min(reply_len, (size_t)PAGE_SIZE);
	ret = decode_object_map_header(&p, end, &object_map_size);
	if (ret)
		goto out;

	if (object_map_size != num_objects) {
		rbd_warn(rbd_dev, "object map size mismatch: %llu vs %llu",
			 object_map_size, num_objects);
		ret = -EINVAL;
		goto out;
	}

	if (offset_in_page(p) + object_map_bytes > reply_len) {
		ret = -EINVAL;
		goto out;
	}

	rbd_dev->object_map = kvmalloc(object_map_bytes, GFP_KERNEL);
	if (!rbd_dev->object_map) {
		ret = -ENOMEM;
		goto out;
	}

	rbd_dev->object_map_size = object_map_size;
	ceph_copy_from_page_vector(pages, rbd_dev->object_map,
				   offset_in_page(p), object_map_bytes);

out:
	ceph_release_page_vector(pages, num_pages);
	return ret;
}

static void rbd_object_map_free(struct rbd_device *rbd_dev)
{
	kvfree(rbd_dev->object_map);
	rbd_dev->object_map = NULL;
	rbd_dev->object_map_size = 0;
}

static int rbd_object_map_load(struct rbd_device *rbd_dev)
{
	int ret;

	ret = __rbd_object_map_load(rbd_dev);
	if (ret)
		return ret;

	ret = rbd_dev_v2_get_flags(rbd_dev);
	if (ret) {
		rbd_object_map_free(rbd_dev);
		return ret;
	}

	if (rbd_dev->object_map_flags & RBD_FLAG_OBJECT_MAP_INVALID)
		rbd_warn(rbd_dev, "object map is invalid");

	return 0;
}

static int rbd_object_map_open(struct rbd_device *rbd_dev)
{
	int ret;

	ret = rbd_object_map_lock(rbd_dev);
	if (ret)
		return ret;

	ret = rbd_object_map_load(rbd_dev);
	if (ret) {
		rbd_object_map_unlock(rbd_dev);
		return ret;
	}

	return 0;
}

static void rbd_object_map_close(struct rbd_device *rbd_dev)
{
	rbd_object_map_free(rbd_dev);
	rbd_object_map_unlock(rbd_dev);
}

/*
 * This function needs snap_id (or more precisely just something to
 * distinguish between HEAD and snapshot object maps), new_state and
 * current_state that were passed to rbd_object_map_update().
 *
 * To avoid allocating and stashing a context we piggyback on the OSD
 * request.  A HEAD update has two ops (assert_locked).  For new_state
 * and current_state we decode our own object_map_update op, encoded in
 * rbd_cls_object_map_update().
 */
static int rbd_object_map_update_finish(struct rbd_obj_request *obj_req,
					struct ceph_osd_request *osd_req)
{
	struct rbd_device *rbd_dev = obj_req->img_request->rbd_dev;
	struct ceph_osd_data *osd_data;
	u64 objno;
	u8 state, new_state, current_state;
	bool has_current_state;
	void *p;

	if (osd_req->r_result)
		return osd_req->r_result;

	/*
	 * Nothing to do for a snapshot object map.
	 */
	if (osd_req->r_num_ops == 1)
		return 0;

	/*
	 * Update in-memory HEAD object map.
	 */
	rbd_assert(osd_req->r_num_ops == 2);
	osd_data = osd_req_op_data(osd_req, 1, cls, request_data);
	rbd_assert(osd_data->type == CEPH_OSD_DATA_TYPE_PAGES);

	p = page_address(osd_data->pages[0]);
	objno = ceph_decode_64(&p);
	rbd_assert(objno == obj_req->ex.oe_objno);
	rbd_assert(ceph_decode_64(&p) == objno + 1);
	new_state = ceph_decode_8(&p);
	has_current_state = ceph_decode_8(&p);
	if (has_current_state)
		current_state = ceph_decode_8(&p);

	spin_lock(&rbd_dev->object_map_lock);
	state = __rbd_object_map_get(rbd_dev, objno);
	if (!has_current_state || current_state == state ||
	    (current_state == OBJECT_EXISTS && state == OBJECT_EXISTS_CLEAN))
		__rbd_object_map_set(rbd_dev, objno, new_state);
	spin_unlock(&rbd_dev->object_map_lock);

	return 0;
}

static void rbd_object_map_callback(struct ceph_osd_request *osd_req)
{
	struct rbd_obj_request *obj_req = osd_req->r_priv;
	int result;

	dout("%s osd_req %p result %d for obj_req %p\n", __func__, osd_req,
	     osd_req->r_result, obj_req);

	result = rbd_object_map_update_finish(obj_req, osd_req);
	rbd_obj_handle_request(obj_req, result);
}

static bool update_needed(struct rbd_device *rbd_dev, u64 objno, u8 new_state)
{
	u8 state = rbd_object_map_get(rbd_dev, objno);

	if (state == new_state ||
	    (new_state == OBJECT_PENDING && state == OBJECT_NONEXISTENT) ||
	    (new_state == OBJECT_NONEXISTENT && state != OBJECT_PENDING))
		return false;

	return true;
}

static int rbd_cls_object_map_update(struct ceph_osd_request *req,
				     int which, u64 objno, u8 new_state,
				     const u8 *current_state)
{
	struct page **pages;
	void *p, *start;
	int ret;

	ret = osd_req_op_cls_init(req, which, "rbd", "object_map_update");
	if (ret)
		return ret;

	pages = ceph_alloc_page_vector(1, GFP_NOIO);
	if (IS_ERR(pages))
		return PTR_ERR(pages);

	p = start = page_address(pages[0]);
	ceph_encode_64(&p, objno);
	ceph_encode_64(&p, objno + 1);
	ceph_encode_8(&p, new_state);
	if (current_state) {
		ceph_encode_8(&p, 1);
		ceph_encode_8(&p, *current_state);
	} else {
		ceph_encode_8(&p, 0);
	}

	osd_req_op_cls_request_data_pages(req, which, pages, p - start, 0,
					  false, true);
	return 0;
}

/*
 * Return:
 *   0 - object map update sent
 *   1 - object map update isn't needed
 *  <0 - error
 */
static int rbd_object_map_update(struct rbd_obj_request *obj_req, u64 snap_id,
				 u8 new_state, const u8 *current_state)
{
	struct rbd_device *rbd_dev = obj_req->img_request->rbd_dev;
	struct ceph_osd_client *osdc = &rbd_dev->rbd_client->client->osdc;
	struct ceph_osd_request *req;
	int num_ops = 1;
	int which = 0;
	int ret;

	if (snap_id == CEPH_NOSNAP) {
		if (!update_needed(rbd_dev, obj_req->ex.oe_objno, new_state))
			return 1;

		num_ops++; /* assert_locked */
	}

	req = ceph_osdc_alloc_request(osdc, NULL, num_ops, false, GFP_NOIO);
	if (!req)
		return -ENOMEM;

	list_add_tail(&req->r_private_item, &obj_req->osd_reqs);
	req->r_callback = rbd_object_map_callback;
	req->r_priv = obj_req;

	rbd_object_map_name(rbd_dev, snap_id, &req->r_base_oid);
	ceph_oloc_copy(&req->r_base_oloc, &rbd_dev->header_oloc);
	req->r_flags = CEPH_OSD_FLAG_WRITE;
	ktime_get_real_ts64(&req->r_mtime);

	if (snap_id == CEPH_NOSNAP) {
		/*
		 * Protect against possible race conditions during lock
		 * ownership transitions.
		 */
		ret = ceph_cls_assert_locked(req, which++, RBD_LOCK_NAME,
					     CEPH_CLS_LOCK_EXCLUSIVE, "", "");
		if (ret)
			return ret;
	}

	ret = rbd_cls_object_map_update(req, which, obj_req->ex.oe_objno,
					new_state, current_state);
	if (ret)
		return ret;

	ret = ceph_osdc_alloc_messages(req, GFP_NOIO);
	if (ret)
		return ret;

	ceph_osdc_start_request(osdc, req);
	return 0;
}

static void prune_extents(struct ceph_file_extent *img_extents,
			  u32 *num_img_extents, u64 overlap)
{
	u32 cnt = *num_img_extents;

	/* drop extents completely beyond the overlap */
	while (cnt && img_extents[cnt - 1].fe_off >= overlap)
		cnt--;

	if (cnt) {
		struct ceph_file_extent *ex = &img_extents[cnt - 1];

		/* trim final overlapping extent */
		if (ex->fe_off + ex->fe_len > overlap)
			ex->fe_len = overlap - ex->fe_off;
	}

	*num_img_extents = cnt;
}

/*
 * Determine the byte range(s) covered by either just the object extent
 * or the entire object in the parent image.
 */
static int rbd_obj_calc_img_extents(struct rbd_obj_request *obj_req,
				    bool entire)
{
	struct rbd_device *rbd_dev = obj_req->img_request->rbd_dev;
	int ret;

	if (!rbd_dev->parent_overlap)
		return 0;

	ret = ceph_extent_to_file(&rbd_dev->layout, obj_req->ex.oe_objno,
				  entire ? 0 : obj_req->ex.oe_off,
				  entire ? rbd_dev->layout.object_size :
							obj_req->ex.oe_len,
				  &obj_req->img_extents,
				  &obj_req->num_img_extents);
	if (ret)
		return ret;

	prune_extents(obj_req->img_extents, &obj_req->num_img_extents,
		      rbd_dev->parent_overlap);
	return 0;
}

static void rbd_osd_setup_data(struct ceph_osd_request *osd_req, int which)
{
	struct rbd_obj_request *obj_req = osd_req->r_priv;

	switch (obj_req->img_request->data_type) {
	case OBJ_REQUEST_BIO:
		osd_req_op_extent_osd_data_bio(osd_req, which,
					       &obj_req->bio_pos,
					       obj_req->ex.oe_len);
		break;
	case OBJ_REQUEST_BVECS:
	case OBJ_REQUEST_OWN_BVECS:
		rbd_assert(obj_req->bvec_pos.iter.bi_size ==
							obj_req->ex.oe_len);
		rbd_assert(obj_req->bvec_idx == obj_req->bvec_count);
		osd_req_op_extent_osd_data_bvec_pos(osd_req, which,
						    &obj_req->bvec_pos);
		break;
	default:
		BUG();
	}
}

static int rbd_osd_setup_stat(struct ceph_osd_request *osd_req, int which)
{
	struct page **pages;

	/*
	 * The response data for a STAT call consists of:
	 *     le64 length;
	 *     struct {
	 *         le32 tv_sec;
	 *         le32 tv_nsec;
	 *     } mtime;
	 */
	pages = ceph_alloc_page_vector(1, GFP_NOIO);
	if (IS_ERR(pages))
		return PTR_ERR(pages);

	osd_req_op_init(osd_req, which, CEPH_OSD_OP_STAT, 0);
	osd_req_op_raw_data_in_pages(osd_req, which, pages,
				     8 + sizeof(struct ceph_timespec),
				     0, false, true);
	return 0;
}

static int rbd_osd_setup_copyup(struct ceph_osd_request *osd_req, int which,
				u32 bytes)
{
	struct rbd_obj_request *obj_req = osd_req->r_priv;
	int ret;

	ret = osd_req_op_cls_init(osd_req, which, "rbd", "copyup");
	if (ret)
		return ret;

	osd_req_op_cls_request_data_bvecs(osd_req, which, obj_req->copyup_bvecs,
					  obj_req->copyup_bvec_count, bytes);
	return 0;
}

static int rbd_obj_init_read(struct rbd_obj_request *obj_req)
{
	obj_req->read_state = RBD_OBJ_READ_START;
	return 0;
}

static void __rbd_osd_setup_write_ops(struct ceph_osd_request *osd_req,
				      int which)
{
	struct rbd_obj_request *obj_req = osd_req->r_priv;
	struct rbd_device *rbd_dev = obj_req->img_request->rbd_dev;
	u16 opcode;

	if (!use_object_map(rbd_dev) ||
	    !(obj_req->flags & RBD_OBJ_FLAG_MAY_EXIST)) {
		osd_req_op_alloc_hint_init(osd_req, which++,
					   rbd_dev->layout.object_size,
					   rbd_dev->layout.object_size,
					   rbd_dev->opts->alloc_hint_flags);
	}

	if (rbd_obj_is_entire(obj_req))
		opcode = CEPH_OSD_OP_WRITEFULL;
	else
		opcode = CEPH_OSD_OP_WRITE;

	osd_req_op_extent_init(osd_req, which, opcode,
			       obj_req->ex.oe_off, obj_req->ex.oe_len, 0, 0);
	rbd_osd_setup_data(osd_req, which);
}

static int rbd_obj_init_write(struct rbd_obj_request *obj_req)
{
	int ret;

	/* reverse map the entire object onto the parent */
	ret = rbd_obj_calc_img_extents(obj_req, true);
	if (ret)
		return ret;

	if (rbd_obj_copyup_enabled(obj_req))
		obj_req->flags |= RBD_OBJ_FLAG_COPYUP_ENABLED;

	obj_req->write_state = RBD_OBJ_WRITE_START;
	return 0;
}

static u16 truncate_or_zero_opcode(struct rbd_obj_request *obj_req)
{
	return rbd_obj_is_tail(obj_req) ? CEPH_OSD_OP_TRUNCATE :
					  CEPH_OSD_OP_ZERO;
}

static void __rbd_osd_setup_discard_ops(struct ceph_osd_request *osd_req,
					int which)
{
	struct rbd_obj_request *obj_req = osd_req->r_priv;

	if (rbd_obj_is_entire(obj_req) && !obj_req->num_img_extents) {
		rbd_assert(obj_req->flags & RBD_OBJ_FLAG_DELETION);
		osd_req_op_init(osd_req, which, CEPH_OSD_OP_DELETE, 0);
	} else {
		osd_req_op_extent_init(osd_req, which,
				       truncate_or_zero_opcode(obj_req),
				       obj_req->ex.oe_off, obj_req->ex.oe_len,
				       0, 0);
	}
}

static int rbd_obj_init_discard(struct rbd_obj_request *obj_req)
{
	struct rbd_device *rbd_dev = obj_req->img_request->rbd_dev;
	u64 off, next_off;
	int ret;

	/*
	 * Align the range to alloc_size boundary and punt on discards
	 * that are too small to free up any space.
	 *
	 * alloc_size == object_size && is_tail() is a special case for
	 * filestore with filestore_punch_hole = false, needed to allow
	 * truncate (in addition to delete).
	 */
	if (rbd_dev->opts->alloc_size != rbd_dev->layout.object_size ||
	    !rbd_obj_is_tail(obj_req)) {
		off = round_up(obj_req->ex.oe_off, rbd_dev->opts->alloc_size);
		next_off = round_down(obj_req->ex.oe_off + obj_req->ex.oe_len,
				      rbd_dev->opts->alloc_size);
		if (off >= next_off)
			return 1;

		dout("%s %p %llu~%llu -> %llu~%llu\n", __func__,
		     obj_req, obj_req->ex.oe_off, obj_req->ex.oe_len,
		     off, next_off - off);
		obj_req->ex.oe_off = off;
		obj_req->ex.oe_len = next_off - off;
	}

	/* reverse map the entire object onto the parent */
	ret = rbd_obj_calc_img_extents(obj_req, true);
	if (ret)
		return ret;

	obj_req->flags |= RBD_OBJ_FLAG_NOOP_FOR_NONEXISTENT;
	if (rbd_obj_is_entire(obj_req) && !obj_req->num_img_extents)
		obj_req->flags |= RBD_OBJ_FLAG_DELETION;

	obj_req->write_state = RBD_OBJ_WRITE_START;
	return 0;
}

static void __rbd_osd_setup_zeroout_ops(struct ceph_osd_request *osd_req,
					int which)
{
	struct rbd_obj_request *obj_req = osd_req->r_priv;
	u16 opcode;

	if (rbd_obj_is_entire(obj_req)) {
		if (obj_req->num_img_extents) {
			if (!(obj_req->flags & RBD_OBJ_FLAG_COPYUP_ENABLED))
				osd_req_op_init(osd_req, which++,
						CEPH_OSD_OP_CREATE, 0);
			opcode = CEPH_OSD_OP_TRUNCATE;
		} else {
			rbd_assert(obj_req->flags & RBD_OBJ_FLAG_DELETION);
			osd_req_op_init(osd_req, which++,
					CEPH_OSD_OP_DELETE, 0);
			opcode = 0;
		}
	} else {
		opcode = truncate_or_zero_opcode(obj_req);
	}

	if (opcode)
		osd_req_op_extent_init(osd_req, which, opcode,
				       obj_req->ex.oe_off, obj_req->ex.oe_len,
				       0, 0);
}

static int rbd_obj_init_zeroout(struct rbd_obj_request *obj_req)
{
	int ret;

	/* reverse map the entire object onto the parent */
	ret = rbd_obj_calc_img_extents(obj_req, true);
	if (ret)
		return ret;

	if (rbd_obj_copyup_enabled(obj_req))
		obj_req->flags |= RBD_OBJ_FLAG_COPYUP_ENABLED;
	if (!obj_req->num_img_extents) {
		obj_req->flags |= RBD_OBJ_FLAG_NOOP_FOR_NONEXISTENT;
		if (rbd_obj_is_entire(obj_req))
			obj_req->flags |= RBD_OBJ_FLAG_DELETION;
	}

	obj_req->write_state = RBD_OBJ_WRITE_START;
	return 0;
}

static int count_write_ops(struct rbd_obj_request *obj_req)
{
	struct rbd_img_request *img_req = obj_req->img_request;

	switch (img_req->op_type) {
	case OBJ_OP_WRITE:
		if (!use_object_map(img_req->rbd_dev) ||
		    !(obj_req->flags & RBD_OBJ_FLAG_MAY_EXIST))
			return 2; /* setallochint + write/writefull */

		return 1; /* write/writefull */
	case OBJ_OP_DISCARD:
		return 1; /* delete/truncate/zero */
	case OBJ_OP_ZEROOUT:
		if (rbd_obj_is_entire(obj_req) && obj_req->num_img_extents &&
		    !(obj_req->flags & RBD_OBJ_FLAG_COPYUP_ENABLED))
			return 2; /* create + truncate */

		return 1; /* delete/truncate/zero */
	default:
		BUG();
	}
}

static void rbd_osd_setup_write_ops(struct ceph_osd_request *osd_req,
				    int which)
{
	struct rbd_obj_request *obj_req = osd_req->r_priv;

	switch (obj_req->img_request->op_type) {
	case OBJ_OP_WRITE:
		__rbd_osd_setup_write_ops(osd_req, which);
		break;
	case OBJ_OP_DISCARD:
		__rbd_osd_setup_discard_ops(osd_req, which);
		break;
	case OBJ_OP_ZEROOUT:
		__rbd_osd_setup_zeroout_ops(osd_req, which);
		break;
	default:
		BUG();
	}
}

/*
 * Prune the list of object requests (adjust offset and/or length, drop
 * redundant requests).  Prepare object request state machines and image
 * request state machine for execution.
 */
static int __rbd_img_fill_request(struct rbd_img_request *img_req)
{
	struct rbd_obj_request *obj_req, *next_obj_req;
	int ret;

	for_each_obj_request_safe(img_req, obj_req, next_obj_req) {
		switch (img_req->op_type) {
		case OBJ_OP_READ:
			ret = rbd_obj_init_read(obj_req);
			break;
		case OBJ_OP_WRITE:
			ret = rbd_obj_init_write(obj_req);
			break;
		case OBJ_OP_DISCARD:
			ret = rbd_obj_init_discard(obj_req);
			break;
		case OBJ_OP_ZEROOUT:
			ret = rbd_obj_init_zeroout(obj_req);
			break;
		default:
			BUG();
		}
		if (ret < 0)
			return ret;
		if (ret > 0) {
			rbd_img_obj_request_del(img_req, obj_req);
			continue;
		}
	}

	img_req->state = RBD_IMG_START;
	return 0;
}

union rbd_img_fill_iter {
	struct ceph_bio_iter	bio_iter;
	struct ceph_bvec_iter	bvec_iter;
};

struct rbd_img_fill_ctx {
	enum obj_request_type	pos_type;
	union rbd_img_fill_iter	*pos;
	union rbd_img_fill_iter	iter;
	ceph_object_extent_fn_t	set_pos_fn;
	ceph_object_extent_fn_t	count_fn;
	ceph_object_extent_fn_t	copy_fn;
};

static struct ceph_object_extent *alloc_object_extent(void *arg)
{
	struct rbd_img_request *img_req = arg;
	struct rbd_obj_request *obj_req;

	obj_req = rbd_obj_request_create();
	if (!obj_req)
		return NULL;

	rbd_img_obj_request_add(img_req, obj_req);
	return &obj_req->ex;
}

/*
 * While su != os && sc == 1 is technically not fancy (it's the same
 * layout as su == os && sc == 1), we can't use the nocopy path for it
 * because ->set_pos_fn() should be called only once per object.
 * ceph_file_to_extents() invokes action_fn once per stripe unit, so
 * treat su != os && sc == 1 as fancy.
 */
static bool rbd_layout_is_fancy(struct ceph_file_layout *l)
{
	return l->stripe_unit != l->object_size;
}

static int rbd_img_fill_request_nocopy(struct rbd_img_request *img_req,
				       struct ceph_file_extent *img_extents,
				       u32 num_img_extents,
				       struct rbd_img_fill_ctx *fctx)
{
	u32 i;
	int ret;

	img_req->data_type = fctx->pos_type;

	/*
	 * Create object requests and set each object request's starting
	 * position in the provided bio (list) or bio_vec array.
	 */
	fctx->iter = *fctx->pos;
	for (i = 0; i < num_img_extents; i++) {
		ret = ceph_file_to_extents(&img_req->rbd_dev->layout,
					   img_extents[i].fe_off,
					   img_extents[i].fe_len,
					   &img_req->object_extents,
					   alloc_object_extent, img_req,
					   fctx->set_pos_fn, &fctx->iter);
		if (ret)
			return ret;
	}

	return __rbd_img_fill_request(img_req);
}

/*
 * Map a list of image extents to a list of object extents, create the
 * corresponding object requests (normally each to a different object,
 * but not always) and add them to @img_req.  For each object request,
 * set up its data descriptor to point to the corresponding chunk(s) of
 * @fctx->pos data buffer.
 *
 * Because ceph_file_to_extents() will merge adjacent object extents
 * together, each object request's data descriptor may point to multiple
 * different chunks of @fctx->pos data buffer.
 *
 * @fctx->pos data buffer is assumed to be large enough.
 */
static int rbd_img_fill_request(struct rbd_img_request *img_req,
				struct ceph_file_extent *img_extents,
				u32 num_img_extents,
				struct rbd_img_fill_ctx *fctx)
{
	struct rbd_device *rbd_dev = img_req->rbd_dev;
	struct rbd_obj_request *obj_req;
	u32 i;
	int ret;

	if (fctx->pos_type == OBJ_REQUEST_NODATA ||
	    !rbd_layout_is_fancy(&rbd_dev->layout))
		return rbd_img_fill_request_nocopy(img_req, img_extents,
						   num_img_extents, fctx);

	img_req->data_type = OBJ_REQUEST_OWN_BVECS;

	/*
	 * Create object requests and determine ->bvec_count for each object
	 * request.  Note that ->bvec_count sum over all object requests may
	 * be greater than the number of bio_vecs in the provided bio (list)
	 * or bio_vec array because when mapped, those bio_vecs can straddle
	 * stripe unit boundaries.
	 */
	fctx->iter = *fctx->pos;
	for (i = 0; i < num_img_extents; i++) {
		ret = ceph_file_to_extents(&rbd_dev->layout,
					   img_extents[i].fe_off,
					   img_extents[i].fe_len,
					   &img_req->object_extents,
					   alloc_object_extent, img_req,
					   fctx->count_fn, &fctx->iter);
		if (ret)
			return ret;
	}

	for_each_obj_request(img_req, obj_req) {
		obj_req->bvec_pos.bvecs = kmalloc_array(obj_req->bvec_count,
					      sizeof(*obj_req->bvec_pos.bvecs),
					      GFP_NOIO);
		if (!obj_req->bvec_pos.bvecs)
			return -ENOMEM;
	}

	/*
	 * Fill in each object request's private bio_vec array, splitting and
	 * rearranging the provided bio_vecs in stripe unit chunks as needed.
	 */
	fctx->iter = *fctx->pos;
	for (i = 0; i < num_img_extents; i++) {
		ret = ceph_iterate_extents(&rbd_dev->layout,
					   img_extents[i].fe_off,
					   img_extents[i].fe_len,
					   &img_req->object_extents,
					   fctx->copy_fn, &fctx->iter);
		if (ret)
			return ret;
	}

	return __rbd_img_fill_request(img_req);
}

static int rbd_img_fill_nodata(struct rbd_img_request *img_req,
			       u64 off, u64 len)
{
	struct ceph_file_extent ex = { off, len };
	union rbd_img_fill_iter dummy = {};
	struct rbd_img_fill_ctx fctx = {
		.pos_type = OBJ_REQUEST_NODATA,
		.pos = &dummy,
	};

	return rbd_img_fill_request(img_req, &ex, 1, &fctx);
}

static void set_bio_pos(struct ceph_object_extent *ex, u32 bytes, void *arg)
{
	struct rbd_obj_request *obj_req =
	    container_of(ex, struct rbd_obj_request, ex);
	struct ceph_bio_iter *it = arg;

	dout("%s objno %llu bytes %u\n", __func__, ex->oe_objno, bytes);
	obj_req->bio_pos = *it;
	ceph_bio_iter_advance(it, bytes);
}

static void count_bio_bvecs(struct ceph_object_extent *ex, u32 bytes, void *arg)
{
	struct rbd_obj_request *obj_req =
	    container_of(ex, struct rbd_obj_request, ex);
	struct ceph_bio_iter *it = arg;

	dout("%s objno %llu bytes %u\n", __func__, ex->oe_objno, bytes);
	ceph_bio_iter_advance_step(it, bytes, ({
		obj_req->bvec_count++;
	}));

}

static void copy_bio_bvecs(struct ceph_object_extent *ex, u32 bytes, void *arg)
{
	struct rbd_obj_request *obj_req =
	    container_of(ex, struct rbd_obj_request, ex);
	struct ceph_bio_iter *it = arg;

	dout("%s objno %llu bytes %u\n", __func__, ex->oe_objno, bytes);
	ceph_bio_iter_advance_step(it, bytes, ({
		obj_req->bvec_pos.bvecs[obj_req->bvec_idx++] = bv;
		obj_req->bvec_pos.iter.bi_size += bv.bv_len;
	}));
}

static int __rbd_img_fill_from_bio(struct rbd_img_request *img_req,
				   struct ceph_file_extent *img_extents,
				   u32 num_img_extents,
				   struct ceph_bio_iter *bio_pos)
{
	struct rbd_img_fill_ctx fctx = {
		.pos_type = OBJ_REQUEST_BIO,
		.pos = (union rbd_img_fill_iter *)bio_pos,
		.set_pos_fn = set_bio_pos,
		.count_fn = count_bio_bvecs,
		.copy_fn = copy_bio_bvecs,
	};

	return rbd_img_fill_request(img_req, img_extents, num_img_extents,
				    &fctx);
}

static int rbd_img_fill_from_bio(struct rbd_img_request *img_req,
				 u64 off, u64 len, struct bio *bio)
{
	struct ceph_file_extent ex = { off, len };
	struct ceph_bio_iter it = { .bio = bio, .iter = bio->bi_iter };

	return __rbd_img_fill_from_bio(img_req, &ex, 1, &it);
}

static void set_bvec_pos(struct ceph_object_extent *ex, u32 bytes, void *arg)
{
	struct rbd_obj_request *obj_req =
	    container_of(ex, struct rbd_obj_request, ex);
	struct ceph_bvec_iter *it = arg;

	obj_req->bvec_pos = *it;
	ceph_bvec_iter_shorten(&obj_req->bvec_pos, bytes);
	ceph_bvec_iter_advance(it, bytes);
}

static void count_bvecs(struct ceph_object_extent *ex, u32 bytes, void *arg)
{
	struct rbd_obj_request *obj_req =
	    container_of(ex, struct rbd_obj_request, ex);
	struct ceph_bvec_iter *it = arg;

	ceph_bvec_iter_advance_step(it, bytes, ({
		obj_req->bvec_count++;
	}));
}

static void copy_bvecs(struct ceph_object_extent *ex, u32 bytes, void *arg)
{
	struct rbd_obj_request *obj_req =
	    container_of(ex, struct rbd_obj_request, ex);
	struct ceph_bvec_iter *it = arg;

	ceph_bvec_iter_advance_step(it, bytes, ({
		obj_req->bvec_pos.bvecs[obj_req->bvec_idx++] = bv;
		obj_req->bvec_pos.iter.bi_size += bv.bv_len;
	}));
}

static int __rbd_img_fill_from_bvecs(struct rbd_img_request *img_req,
				     struct ceph_file_extent *img_extents,
				     u32 num_img_extents,
				     struct ceph_bvec_iter *bvec_pos)
{
	struct rbd_img_fill_ctx fctx = {
		.pos_type = OBJ_REQUEST_BVECS,
		.pos = (union rbd_img_fill_iter *)bvec_pos,
		.set_pos_fn = set_bvec_pos,
		.count_fn = count_bvecs,
		.copy_fn = copy_bvecs,
	};

	return rbd_img_fill_request(img_req, img_extents, num_img_extents,
				    &fctx);
}

static int rbd_img_fill_from_bvecs(struct rbd_img_request *img_req,
				   struct ceph_file_extent *img_extents,
				   u32 num_img_extents,
				   struct bio_vec *bvecs)
{
	struct ceph_bvec_iter it = {
		.bvecs = bvecs,
		.iter = { .bi_size = ceph_file_extents_bytes(img_extents,
							     num_img_extents) },
	};

	return __rbd_img_fill_from_bvecs(img_req, img_extents, num_img_extents,
					 &it);
}

static void rbd_img_handle_request_work(struct work_struct *work)
{
	struct rbd_img_request *img_req =
	    container_of(work, struct rbd_img_request, work);

	rbd_img_handle_request(img_req, img_req->work_result);
}

static void rbd_img_schedule(struct rbd_img_request *img_req, int result)
{
	INIT_WORK(&img_req->work, rbd_img_handle_request_work);
	img_req->work_result = result;
	queue_work(rbd_wq, &img_req->work);
}

static bool rbd_obj_may_exist(struct rbd_obj_request *obj_req)
{
	struct rbd_device *rbd_dev = obj_req->img_request->rbd_dev;

	if (rbd_object_map_may_exist(rbd_dev, obj_req->ex.oe_objno)) {
		obj_req->flags |= RBD_OBJ_FLAG_MAY_EXIST;
		return true;
	}

	dout("%s %p objno %llu assuming dne\n", __func__, obj_req,
	     obj_req->ex.oe_objno);
	return false;
}

static int rbd_obj_read_object(struct rbd_obj_request *obj_req)
{
	struct ceph_osd_request *osd_req;
	int ret;

	osd_req = __rbd_obj_add_osd_request(obj_req, NULL, 1);
	if (IS_ERR(osd_req))
		return PTR_ERR(osd_req);

	osd_req_op_extent_init(osd_req, 0, CEPH_OSD_OP_READ,
			       obj_req->ex.oe_off, obj_req->ex.oe_len, 0, 0);
	rbd_osd_setup_data(osd_req, 0);
	rbd_osd_format_read(osd_req);

	ret = ceph_osdc_alloc_messages(osd_req, GFP_NOIO);
	if (ret)
		return ret;

	rbd_osd_submit(osd_req);
	return 0;
}

static int rbd_obj_read_from_parent(struct rbd_obj_request *obj_req)
{
	struct rbd_img_request *img_req = obj_req->img_request;
	struct rbd_device *parent = img_req->rbd_dev->parent;
	struct rbd_img_request *child_img_req;
	int ret;

	child_img_req = kmem_cache_alloc(rbd_img_request_cache, GFP_NOIO);
	if (!child_img_req)
		return -ENOMEM;

	rbd_img_request_init(child_img_req, parent, OBJ_OP_READ);
	__set_bit(IMG_REQ_CHILD, &child_img_req->flags);
	child_img_req->obj_request = obj_req;

	down_read(&parent->header_rwsem);
	rbd_img_capture_header(child_img_req);
	up_read(&parent->header_rwsem);

	dout("%s child_img_req %p for obj_req %p\n", __func__, child_img_req,
	     obj_req);

	if (!rbd_img_is_write(img_req)) {
		switch (img_req->data_type) {
		case OBJ_REQUEST_BIO:
			ret = __rbd_img_fill_from_bio(child_img_req,
						      obj_req->img_extents,
						      obj_req->num_img_extents,
						      &obj_req->bio_pos);
			break;
		case OBJ_REQUEST_BVECS:
		case OBJ_REQUEST_OWN_BVECS:
			ret = __rbd_img_fill_from_bvecs(child_img_req,
						      obj_req->img_extents,
						      obj_req->num_img_extents,
						      &obj_req->bvec_pos);
			break;
		default:
			BUG();
		}
	} else {
		ret = rbd_img_fill_from_bvecs(child_img_req,
					      obj_req->img_extents,
					      obj_req->num_img_extents,
					      obj_req->copyup_bvecs);
	}
	if (ret) {
		rbd_img_request_destroy(child_img_req);
		return ret;
	}

	/* avoid parent chain recursion */
	rbd_img_schedule(child_img_req, 0);
	return 0;
}

static bool rbd_obj_advance_read(struct rbd_obj_request *obj_req, int *result)
{
	struct rbd_device *rbd_dev = obj_req->img_request->rbd_dev;
	int ret;

again:
	switch (obj_req->read_state) {
	case RBD_OBJ_READ_START:
		rbd_assert(!*result);

		if (!rbd_obj_may_exist(obj_req)) {
			*result = -ENOENT;
			obj_req->read_state = RBD_OBJ_READ_OBJECT;
			goto again;
		}

		ret = rbd_obj_read_object(obj_req);
		if (ret) {
			*result = ret;
			return true;
		}
		obj_req->read_state = RBD_OBJ_READ_OBJECT;
		return false;
	case RBD_OBJ_READ_OBJECT:
		if (*result == -ENOENT && rbd_dev->parent_overlap) {
			/* reverse map this object extent onto the parent */
			ret = rbd_obj_calc_img_extents(obj_req, false);
			if (ret) {
				*result = ret;
				return true;
			}
			if (obj_req->num_img_extents) {
				ret = rbd_obj_read_from_parent(obj_req);
				if (ret) {
					*result = ret;
					return true;
				}
				obj_req->read_state = RBD_OBJ_READ_PARENT;
				return false;
			}
		}

		/*
		 * -ENOENT means a hole in the image -- zero-fill the entire
		 * length of the request.  A short read also implies zero-fill
		 * to the end of the request.
		 */
		if (*result == -ENOENT) {
			rbd_obj_zero_range(obj_req, 0, obj_req->ex.oe_len);
			*result = 0;
		} else if (*result >= 0) {
			if (*result < obj_req->ex.oe_len)
				rbd_obj_zero_range(obj_req, *result,
						obj_req->ex.oe_len - *result);
			else
				rbd_assert(*result == obj_req->ex.oe_len);
			*result = 0;
		}
		return true;
	case RBD_OBJ_READ_PARENT:
		/*
		 * The parent image is read only up to the overlap -- zero-fill
		 * from the overlap to the end of the request.
		 */
		if (!*result) {
			u32 obj_overlap = rbd_obj_img_extents_bytes(obj_req);

			if (obj_overlap < obj_req->ex.oe_len)
				rbd_obj_zero_range(obj_req, obj_overlap,
					    obj_req->ex.oe_len - obj_overlap);
		}
		return true;
	default:
		BUG();
	}
}

static bool rbd_obj_write_is_noop(struct rbd_obj_request *obj_req)
{
	struct rbd_device *rbd_dev = obj_req->img_request->rbd_dev;

	if (rbd_object_map_may_exist(rbd_dev, obj_req->ex.oe_objno))
		obj_req->flags |= RBD_OBJ_FLAG_MAY_EXIST;

	if (!(obj_req->flags & RBD_OBJ_FLAG_MAY_EXIST) &&
	    (obj_req->flags & RBD_OBJ_FLAG_NOOP_FOR_NONEXISTENT)) {
		dout("%s %p noop for nonexistent\n", __func__, obj_req);
		return true;
	}

	return false;
}

/*
 * Return:
 *   0 - object map update sent
 *   1 - object map update isn't needed
 *  <0 - error
 */
static int rbd_obj_write_pre_object_map(struct rbd_obj_request *obj_req)
{
	struct rbd_device *rbd_dev = obj_req->img_request->rbd_dev;
	u8 new_state;

	if (!(rbd_dev->header.features & RBD_FEATURE_OBJECT_MAP))
		return 1;

	if (obj_req->flags & RBD_OBJ_FLAG_DELETION)
		new_state = OBJECT_PENDING;
	else
		new_state = OBJECT_EXISTS;

	return rbd_object_map_update(obj_req, CEPH_NOSNAP, new_state, NULL);
}

static int rbd_obj_write_object(struct rbd_obj_request *obj_req)
{
	struct ceph_osd_request *osd_req;
	int num_ops = count_write_ops(obj_req);
	int which = 0;
	int ret;

	if (obj_req->flags & RBD_OBJ_FLAG_COPYUP_ENABLED)
		num_ops++; /* stat */

	osd_req = rbd_obj_add_osd_request(obj_req, num_ops);
	if (IS_ERR(osd_req))
		return PTR_ERR(osd_req);

	if (obj_req->flags & RBD_OBJ_FLAG_COPYUP_ENABLED) {
		ret = rbd_osd_setup_stat(osd_req, which++);
		if (ret)
			return ret;
	}

	rbd_osd_setup_write_ops(osd_req, which);
	rbd_osd_format_write(osd_req);

	ret = ceph_osdc_alloc_messages(osd_req, GFP_NOIO);
	if (ret)
		return ret;

	rbd_osd_submit(osd_req);
	return 0;
}

/*
 * copyup_bvecs pages are never highmem pages
 */
static bool is_zero_bvecs(struct bio_vec *bvecs, u32 bytes)
{
	struct ceph_bvec_iter it = {
		.bvecs = bvecs,
		.iter = { .bi_size = bytes },
	};

	ceph_bvec_iter_advance_step(&it, bytes, ({
		if (memchr_inv(bvec_virt(&bv), 0, bv.bv_len))
			return false;
	}));
	return true;
}

#define MODS_ONLY	U32_MAX

static int rbd_obj_copyup_empty_snapc(struct rbd_obj_request *obj_req,
				      u32 bytes)
{
	struct ceph_osd_request *osd_req;
	int ret;

	dout("%s obj_req %p bytes %u\n", __func__, obj_req, bytes);
	rbd_assert(bytes > 0 && bytes != MODS_ONLY);

	osd_req = __rbd_obj_add_osd_request(obj_req, &rbd_empty_snapc, 1);
	if (IS_ERR(osd_req))
		return PTR_ERR(osd_req);

	ret = rbd_osd_setup_copyup(osd_req, 0, bytes);
	if (ret)
		return ret;

	rbd_osd_format_write(osd_req);

	ret = ceph_osdc_alloc_messages(osd_req, GFP_NOIO);
	if (ret)
		return ret;

	rbd_osd_submit(osd_req);
	return 0;
}

static int rbd_obj_copyup_current_snapc(struct rbd_obj_request *obj_req,
					u32 bytes)
{
	struct ceph_osd_request *osd_req;
	int num_ops = count_write_ops(obj_req);
	int which = 0;
	int ret;

	dout("%s obj_req %p bytes %u\n", __func__, obj_req, bytes);

	if (bytes != MODS_ONLY)
		num_ops++; /* copyup */

	osd_req = rbd_obj_add_osd_request(obj_req, num_ops);
	if (IS_ERR(osd_req))
		return PTR_ERR(osd_req);

	if (bytes != MODS_ONLY) {
		ret = rbd_osd_setup_copyup(osd_req, which++, bytes);
		if (ret)
			return ret;
	}

	rbd_osd_setup_write_ops(osd_req, which);
	rbd_osd_format_write(osd_req);

	ret = ceph_osdc_alloc_messages(osd_req, GFP_NOIO);
	if (ret)
		return ret;

	rbd_osd_submit(osd_req);
	return 0;
}

static int setup_copyup_bvecs(struct rbd_obj_request *obj_req, u64 obj_overlap)
{
	u32 i;

	rbd_assert(!obj_req->copyup_bvecs);
	obj_req->copyup_bvec_count = calc_pages_for(0, obj_overlap);
	obj_req->copyup_bvecs = kcalloc(obj_req->copyup_bvec_count,
					sizeof(*obj_req->copyup_bvecs),
					GFP_NOIO);
	if (!obj_req->copyup_bvecs)
		return -ENOMEM;

	for (i = 0; i < obj_req->copyup_bvec_count; i++) {
		unsigned int len = min(obj_overlap, (u64)PAGE_SIZE);

		obj_req->copyup_bvecs[i].bv_page = alloc_page(GFP_NOIO);
		if (!obj_req->copyup_bvecs[i].bv_page)
			return -ENOMEM;

		obj_req->copyup_bvecs[i].bv_offset = 0;
		obj_req->copyup_bvecs[i].bv_len = len;
		obj_overlap -= len;
	}

	rbd_assert(!obj_overlap);
	return 0;
}

/*
 * The target object doesn't exist.  Read the data for the entire
 * target object up to the overlap point (if any) from the parent,
 * so we can use it for a copyup.
 */
static int rbd_obj_copyup_read_parent(struct rbd_obj_request *obj_req)
{
	struct rbd_device *rbd_dev = obj_req->img_request->rbd_dev;
	int ret;

	rbd_assert(obj_req->num_img_extents);
	prune_extents(obj_req->img_extents, &obj_req->num_img_extents,
		      rbd_dev->parent_overlap);
	if (!obj_req->num_img_extents) {
		/*
		 * The overlap has become 0 (most likely because the
		 * image has been flattened).  Re-submit the original write
		 * request -- pass MODS_ONLY since the copyup isn't needed
		 * anymore.
		 */
		return rbd_obj_copyup_current_snapc(obj_req, MODS_ONLY);
	}

	ret = setup_copyup_bvecs(obj_req, rbd_obj_img_extents_bytes(obj_req));
	if (ret)
		return ret;

	return rbd_obj_read_from_parent(obj_req);
}

static void rbd_obj_copyup_object_maps(struct rbd_obj_request *obj_req)
{
	struct rbd_device *rbd_dev = obj_req->img_request->rbd_dev;
	struct ceph_snap_context *snapc = obj_req->img_request->snapc;
	u8 new_state;
	u32 i;
	int ret;

	rbd_assert(!obj_req->pending.result && !obj_req->pending.num_pending);

	if (!(rbd_dev->header.features & RBD_FEATURE_OBJECT_MAP))
		return;

	if (obj_req->flags & RBD_OBJ_FLAG_COPYUP_ZEROS)
		return;

	for (i = 0; i < snapc->num_snaps; i++) {
		if ((rbd_dev->header.features & RBD_FEATURE_FAST_DIFF) &&
		    i + 1 < snapc->num_snaps)
			new_state = OBJECT_EXISTS_CLEAN;
		else
			new_state = OBJECT_EXISTS;

		ret = rbd_object_map_update(obj_req, snapc->snaps[i],
					    new_state, NULL);
		if (ret < 0) {
			obj_req->pending.result = ret;
			return;
		}

		rbd_assert(!ret);
		obj_req->pending.num_pending++;
	}
}

static void rbd_obj_copyup_write_object(struct rbd_obj_request *obj_req)
{
	u32 bytes = rbd_obj_img_extents_bytes(obj_req);
	int ret;

	rbd_assert(!obj_req->pending.result && !obj_req->pending.num_pending);

	/*
	 * Only send non-zero copyup data to save some I/O and network
	 * bandwidth -- zero copyup data is equivalent to the object not
	 * existing.
	 */
	if (obj_req->flags & RBD_OBJ_FLAG_COPYUP_ZEROS)
		bytes = 0;

	if (obj_req->img_request->snapc->num_snaps && bytes > 0) {
		/*
		 * Send a copyup request with an empty snapshot context to
		 * deep-copyup the object through all existing snapshots.
		 * A second request with the current snapshot context will be
		 * sent for the actual modification.
		 */
		ret = rbd_obj_copyup_empty_snapc(obj_req, bytes);
		if (ret) {
			obj_req->pending.result = ret;
			return;
		}

		obj_req->pending.num_pending++;
		bytes = MODS_ONLY;
	}

	ret = rbd_obj_copyup_current_snapc(obj_req, bytes);
	if (ret) {
		obj_req->pending.result = ret;
		return;
	}

	obj_req->pending.num_pending++;
}

static bool rbd_obj_advance_copyup(struct rbd_obj_request *obj_req, int *result)
{
	struct rbd_device *rbd_dev = obj_req->img_request->rbd_dev;
	int ret;

again:
	switch (obj_req->copyup_state) {
	case RBD_OBJ_COPYUP_START:
		rbd_assert(!*result);

		ret = rbd_obj_copyup_read_parent(obj_req);
		if (ret) {
			*result = ret;
			return true;
		}
		if (obj_req->num_img_extents)
			obj_req->copyup_state = RBD_OBJ_COPYUP_READ_PARENT;
		else
			obj_req->copyup_state = RBD_OBJ_COPYUP_WRITE_OBJECT;
		return false;
	case RBD_OBJ_COPYUP_READ_PARENT:
		if (*result)
			return true;

		if (is_zero_bvecs(obj_req->copyup_bvecs,
				  rbd_obj_img_extents_bytes(obj_req))) {
			dout("%s %p detected zeros\n", __func__, obj_req);
			obj_req->flags |= RBD_OBJ_FLAG_COPYUP_ZEROS;
		}

		rbd_obj_copyup_object_maps(obj_req);
		if (!obj_req->pending.num_pending) {
			*result = obj_req->pending.result;
			obj_req->copyup_state = RBD_OBJ_COPYUP_OBJECT_MAPS;
			goto again;
		}
		obj_req->copyup_state = __RBD_OBJ_COPYUP_OBJECT_MAPS;
		return false;
	case __RBD_OBJ_COPYUP_OBJECT_MAPS:
		if (!pending_result_dec(&obj_req->pending, result))
			return false;
		fallthrough;
	case RBD_OBJ_COPYUP_OBJECT_MAPS:
		if (*result) {
			rbd_warn(rbd_dev, "snap object map update failed: %d",
				 *result);
			return true;
		}

		rbd_obj_copyup_write_object(obj_req);
		if (!obj_req->pending.num_pending) {
			*result = obj_req->pending.result;
			obj_req->copyup_state = RBD_OBJ_COPYUP_WRITE_OBJECT;
			goto again;
		}
		obj_req->copyup_state = __RBD_OBJ_COPYUP_WRITE_OBJECT;
		return false;
	case __RBD_OBJ_COPYUP_WRITE_OBJECT:
		if (!pending_result_dec(&obj_req->pending, result))
			return false;
		fallthrough;
	case RBD_OBJ_COPYUP_WRITE_OBJECT:
		return true;
	default:
		BUG();
	}
}

/*
 * Return:
 *   0 - object map update sent
 *   1 - object map update isn't needed
 *  <0 - error
 */
static int rbd_obj_write_post_object_map(struct rbd_obj_request *obj_req)
{
	struct rbd_device *rbd_dev = obj_req->img_request->rbd_dev;
	u8 current_state = OBJECT_PENDING;

	if (!(rbd_dev->header.features & RBD_FEATURE_OBJECT_MAP))
		return 1;

	if (!(obj_req->flags & RBD_OBJ_FLAG_DELETION))
		return 1;

	return rbd_object_map_update(obj_req, CEPH_NOSNAP, OBJECT_NONEXISTENT,
				     &current_state);
}

static bool rbd_obj_advance_write(struct rbd_obj_request *obj_req, int *result)
{
	struct rbd_device *rbd_dev = obj_req->img_request->rbd_dev;
	int ret;

again:
	switch (obj_req->write_state) {
	case RBD_OBJ_WRITE_START:
		rbd_assert(!*result);

		if (rbd_obj_write_is_noop(obj_req))
			return true;

		ret = rbd_obj_write_pre_object_map(obj_req);
		if (ret < 0) {
			*result = ret;
			return true;
		}
		obj_req->write_state = RBD_OBJ_WRITE_PRE_OBJECT_MAP;
		if (ret > 0)
			goto again;
		return false;
	case RBD_OBJ_WRITE_PRE_OBJECT_MAP:
		if (*result) {
			rbd_warn(rbd_dev, "pre object map update failed: %d",
				 *result);
			return true;
		}
		ret = rbd_obj_write_object(obj_req);
		if (ret) {
			*result = ret;
			return true;
		}
		obj_req->write_state = RBD_OBJ_WRITE_OBJECT;
		return false;
	case RBD_OBJ_WRITE_OBJECT:
		if (*result == -ENOENT) {
			if (obj_req->flags & RBD_OBJ_FLAG_COPYUP_ENABLED) {
				*result = 0;
				obj_req->copyup_state = RBD_OBJ_COPYUP_START;
				obj_req->write_state = __RBD_OBJ_WRITE_COPYUP;
				goto again;
			}
			/*
			 * On a non-existent object:
			 *   delete - -ENOENT, truncate/zero - 0
			 */
			if (obj_req->flags & RBD_OBJ_FLAG_DELETION)
				*result = 0;
		}
		if (*result)
			return true;

		obj_req->write_state = RBD_OBJ_WRITE_COPYUP;
		goto again;
	case __RBD_OBJ_WRITE_COPYUP:
		if (!rbd_obj_advance_copyup(obj_req, result))
			return false;
		fallthrough;
	case RBD_OBJ_WRITE_COPYUP:
		if (*result) {
			rbd_warn(rbd_dev, "copyup failed: %d", *result);
			return true;
		}
		ret = rbd_obj_write_post_object_map(obj_req);
		if (ret < 0) {
			*result = ret;
			return true;
		}
		obj_req->write_state = RBD_OBJ_WRITE_POST_OBJECT_MAP;
		if (ret > 0)
			goto again;
		return false;
	case RBD_OBJ_WRITE_POST_OBJECT_MAP:
		if (*result)
			rbd_warn(rbd_dev, "post object map update failed: %d",
				 *result);
		return true;
	default:
		BUG();
	}
}

/*
 * Return true if @obj_req is completed.
 */
static bool __rbd_obj_handle_request(struct rbd_obj_request *obj_req,
				     int *result)
{
	struct rbd_img_request *img_req = obj_req->img_request;
	struct rbd_device *rbd_dev = img_req->rbd_dev;
	bool done;

	mutex_lock(&obj_req->state_mutex);
	if (!rbd_img_is_write(img_req))
		done = rbd_obj_advance_read(obj_req, result);
	else
		done = rbd_obj_advance_write(obj_req, result);
	mutex_unlock(&obj_req->state_mutex);

	if (done && *result) {
		rbd_assert(*result < 0);
		rbd_warn(rbd_dev, "%s at objno %llu %llu~%llu result %d",
			 obj_op_name(img_req->op_type), obj_req->ex.oe_objno,
			 obj_req->ex.oe_off, obj_req->ex.oe_len, *result);
	}
	return done;
}

/*
 * This is open-coded in rbd_img_handle_request() to avoid parent chain
 * recursion.
 */
static void rbd_obj_handle_request(struct rbd_obj_request *obj_req, int result)
{
	if (__rbd_obj_handle_request(obj_req, &result))
		rbd_img_handle_request(obj_req->img_request, result);
}

static bool need_exclusive_lock(struct rbd_img_request *img_req)
{
	struct rbd_device *rbd_dev = img_req->rbd_dev;

	if (!(rbd_dev->header.features & RBD_FEATURE_EXCLUSIVE_LOCK))
		return false;

	if (rbd_is_ro(rbd_dev))
		return false;

	rbd_assert(!test_bit(IMG_REQ_CHILD, &img_req->flags));
	if (rbd_dev->opts->lock_on_read ||
	    (rbd_dev->header.features & RBD_FEATURE_OBJECT_MAP))
		return true;

	return rbd_img_is_write(img_req);
}

static bool rbd_lock_add_request(struct rbd_img_request *img_req)
{
	struct rbd_device *rbd_dev = img_req->rbd_dev;
	bool locked;

	lockdep_assert_held(&rbd_dev->lock_rwsem);
	locked = rbd_dev->lock_state == RBD_LOCK_STATE_LOCKED;
	spin_lock(&rbd_dev->lock_lists_lock);
	rbd_assert(list_empty(&img_req->lock_item));
	if (!locked)
		list_add_tail(&img_req->lock_item, &rbd_dev->acquiring_list);
	else
		list_add_tail(&img_req->lock_item, &rbd_dev->running_list);
	spin_unlock(&rbd_dev->lock_lists_lock);
	return locked;
}

static void rbd_lock_del_request(struct rbd_img_request *img_req)
{
	struct rbd_device *rbd_dev = img_req->rbd_dev;
	bool need_wakeup;

	lockdep_assert_held(&rbd_dev->lock_rwsem);
	spin_lock(&rbd_dev->lock_lists_lock);
	rbd_assert(!list_empty(&img_req->lock_item));
	list_del_init(&img_req->lock_item);
	need_wakeup = (rbd_dev->lock_state == RBD_LOCK_STATE_RELEASING &&
		       list_empty(&rbd_dev->running_list));
	spin_unlock(&rbd_dev->lock_lists_lock);
	if (need_wakeup)
		complete(&rbd_dev->releasing_wait);
}

static int rbd_img_exclusive_lock(struct rbd_img_request *img_req)
{
	struct rbd_device *rbd_dev = img_req->rbd_dev;

	if (!need_exclusive_lock(img_req))
		return 1;

	if (rbd_lock_add_request(img_req))
		return 1;

	if (rbd_dev->opts->exclusive) {
		WARN_ON(1); /* lock got released? */
		return -EROFS;
	}

	/*
	 * Note the use of mod_delayed_work() in rbd_acquire_lock()
	 * and cancel_delayed_work() in wake_lock_waiters().
	 */
	dout("%s rbd_dev %p queueing lock_dwork\n", __func__, rbd_dev);
	queue_delayed_work(rbd_dev->task_wq, &rbd_dev->lock_dwork, 0);
	return 0;
}

static void rbd_img_object_requests(struct rbd_img_request *img_req)
{
	struct rbd_obj_request *obj_req;

	rbd_assert(!img_req->pending.result && !img_req->pending.num_pending);

	for_each_obj_request(img_req, obj_req) {
		int result = 0;

		if (__rbd_obj_handle_request(obj_req, &result)) {
			if (result) {
				img_req->pending.result = result;
				return;
			}
		} else {
			img_req->pending.num_pending++;
		}
	}
}

static bool rbd_img_advance(struct rbd_img_request *img_req, int *result)
{
	struct rbd_device *rbd_dev = img_req->rbd_dev;
	int ret;

again:
	switch (img_req->state) {
	case RBD_IMG_START:
		rbd_assert(!*result);

		ret = rbd_img_exclusive_lock(img_req);
		if (ret < 0) {
			*result = ret;
			return true;
		}
		img_req->state = RBD_IMG_EXCLUSIVE_LOCK;
		if (ret > 0)
			goto again;
		return false;
	case RBD_IMG_EXCLUSIVE_LOCK:
		if (*result)
			return true;

		rbd_assert(!need_exclusive_lock(img_req) ||
			   __rbd_is_lock_owner(rbd_dev));

		rbd_img_object_requests(img_req);
		if (!img_req->pending.num_pending) {
			*result = img_req->pending.result;
			img_req->state = RBD_IMG_OBJECT_REQUESTS;
			goto again;
		}
		img_req->state = __RBD_IMG_OBJECT_REQUESTS;
		return false;
	case __RBD_IMG_OBJECT_REQUESTS:
		if (!pending_result_dec(&img_req->pending, result))
			return false;
		fallthrough;
	case RBD_IMG_OBJECT_REQUESTS:
		return true;
	default:
		BUG();
	}
}

/*
 * Return true if @img_req is completed.
 */
static bool __rbd_img_handle_request(struct rbd_img_request *img_req,
				     int *result)
{
	struct rbd_device *rbd_dev = img_req->rbd_dev;
	bool done;

	if (need_exclusive_lock(img_req)) {
		down_read(&rbd_dev->lock_rwsem);
		mutex_lock(&img_req->state_mutex);
		done = rbd_img_advance(img_req, result);
		if (done)
			rbd_lock_del_request(img_req);
		mutex_unlock(&img_req->state_mutex);
		up_read(&rbd_dev->lock_rwsem);
	} else {
		mutex_lock(&img_req->state_mutex);
		done = rbd_img_advance(img_req, result);
		mutex_unlock(&img_req->state_mutex);
	}

	if (done && *result) {
		rbd_assert(*result < 0);
		rbd_warn(rbd_dev, "%s%s result %d",
		      test_bit(IMG_REQ_CHILD, &img_req->flags) ? "child " : "",
		      obj_op_name(img_req->op_type), *result);
	}
	return done;
}

static void rbd_img_handle_request(struct rbd_img_request *img_req, int result)
{
again:
	if (!__rbd_img_handle_request(img_req, &result))
		return;

	if (test_bit(IMG_REQ_CHILD, &img_req->flags)) {
		struct rbd_obj_request *obj_req = img_req->obj_request;

		rbd_img_request_destroy(img_req);
		if (__rbd_obj_handle_request(obj_req, &result)) {
			img_req = obj_req->img_request;
			goto again;
		}
	} else {
		struct request *rq = blk_mq_rq_from_pdu(img_req);

		rbd_img_request_destroy(img_req);
		blk_mq_end_request(rq, errno_to_blk_status(result));
	}
}

static const struct rbd_client_id rbd_empty_cid;

static bool rbd_cid_equal(const struct rbd_client_id *lhs,
			  const struct rbd_client_id *rhs)
{
	return lhs->gid == rhs->gid && lhs->handle == rhs->handle;
}

static struct rbd_client_id rbd_get_cid(struct rbd_device *rbd_dev)
{
	struct rbd_client_id cid;

	mutex_lock(&rbd_dev->watch_mutex);
	cid.gid = ceph_client_gid(rbd_dev->rbd_client->client);
	cid.handle = rbd_dev->watch_cookie;
	mutex_unlock(&rbd_dev->watch_mutex);
	return cid;
}

/*
 * lock_rwsem must be held for write
 */
static void rbd_set_owner_cid(struct rbd_device *rbd_dev,
			      const struct rbd_client_id *cid)
{
	dout("%s rbd_dev %p %llu-%llu -> %llu-%llu\n", __func__, rbd_dev,
	     rbd_dev->owner_cid.gid, rbd_dev->owner_cid.handle,
	     cid->gid, cid->handle);
	rbd_dev->owner_cid = *cid; /* struct */
}

static void format_lock_cookie(struct rbd_device *rbd_dev, char *buf)
{
	mutex_lock(&rbd_dev->watch_mutex);
	sprintf(buf, "%s %llu", RBD_LOCK_COOKIE_PREFIX, rbd_dev->watch_cookie);
	mutex_unlock(&rbd_dev->watch_mutex);
}

static void __rbd_lock(struct rbd_device *rbd_dev, const char *cookie)
{
	struct rbd_client_id cid = rbd_get_cid(rbd_dev);

	rbd_dev->lock_state = RBD_LOCK_STATE_LOCKED;
	strcpy(rbd_dev->lock_cookie, cookie);
	rbd_set_owner_cid(rbd_dev, &cid);
	queue_work(rbd_dev->task_wq, &rbd_dev->acquired_lock_work);
}

/*
 * lock_rwsem must be held for write
 */
static int rbd_lock(struct rbd_device *rbd_dev)
{
	struct ceph_osd_client *osdc = &rbd_dev->rbd_client->client->osdc;
	char cookie[32];
	int ret;

	WARN_ON(__rbd_is_lock_owner(rbd_dev) ||
		rbd_dev->lock_cookie[0] != '\0');

	format_lock_cookie(rbd_dev, cookie);
	ret = ceph_cls_lock(osdc, &rbd_dev->header_oid, &rbd_dev->header_oloc,
			    RBD_LOCK_NAME, CEPH_CLS_LOCK_EXCLUSIVE, cookie,
			    RBD_LOCK_TAG, "", 0);
	if (ret)
		return ret;

	__rbd_lock(rbd_dev, cookie);
	return 0;
}

/*
 * lock_rwsem must be held for write
 */
static void rbd_unlock(struct rbd_device *rbd_dev)
{
	struct ceph_osd_client *osdc = &rbd_dev->rbd_client->client->osdc;
	int ret;

	WARN_ON(!__rbd_is_lock_owner(rbd_dev) ||
		rbd_dev->lock_cookie[0] == '\0');

	ret = ceph_cls_unlock(osdc, &rbd_dev->header_oid, &rbd_dev->header_oloc,
			      RBD_LOCK_NAME, rbd_dev->lock_cookie);
	if (ret && ret != -ENOENT)
		rbd_warn(rbd_dev, "failed to unlock header: %d", ret);

	/* treat errors as the image is unlocked */
	rbd_dev->lock_state = RBD_LOCK_STATE_UNLOCKED;
	rbd_dev->lock_cookie[0] = '\0';
	rbd_set_owner_cid(rbd_dev, &rbd_empty_cid);
	queue_work(rbd_dev->task_wq, &rbd_dev->released_lock_work);
}

static int __rbd_notify_op_lock(struct rbd_device *rbd_dev,
				enum rbd_notify_op notify_op,
				struct page ***preply_pages,
				size_t *preply_len)
{
	struct ceph_osd_client *osdc = &rbd_dev->rbd_client->client->osdc;
	struct rbd_client_id cid = rbd_get_cid(rbd_dev);
	char buf[4 + 8 + 8 + CEPH_ENCODING_START_BLK_LEN];
	int buf_size = sizeof(buf);
	void *p = buf;

	dout("%s rbd_dev %p notify_op %d\n", __func__, rbd_dev, notify_op);

	/* encode *LockPayload NotifyMessage (op + ClientId) */
	ceph_start_encoding(&p, 2, 1, buf_size - CEPH_ENCODING_START_BLK_LEN);
	ceph_encode_32(&p, notify_op);
	ceph_encode_64(&p, cid.gid);
	ceph_encode_64(&p, cid.handle);

	return ceph_osdc_notify(osdc, &rbd_dev->header_oid,
				&rbd_dev->header_oloc, buf, buf_size,
				RBD_NOTIFY_TIMEOUT, preply_pages, preply_len);
}

static void rbd_notify_op_lock(struct rbd_device *rbd_dev,
			       enum rbd_notify_op notify_op)
{
	__rbd_notify_op_lock(rbd_dev, notify_op, NULL, NULL);
}

static void rbd_notify_acquired_lock(struct work_struct *work)
{
	struct rbd_device *rbd_dev = container_of(work, struct rbd_device,
						  acquired_lock_work);

	rbd_notify_op_lock(rbd_dev, RBD_NOTIFY_OP_ACQUIRED_LOCK);
}

static void rbd_notify_released_lock(struct work_struct *work)
{
	struct rbd_device *rbd_dev = container_of(work, struct rbd_device,
						  released_lock_work);

	rbd_notify_op_lock(rbd_dev, RBD_NOTIFY_OP_RELEASED_LOCK);
}

static int rbd_request_lock(struct rbd_device *rbd_dev)
{
	struct page **reply_pages;
	size_t reply_len;
	bool lock_owner_responded = false;
	int ret;

	dout("%s rbd_dev %p\n", __func__, rbd_dev);

	ret = __rbd_notify_op_lock(rbd_dev, RBD_NOTIFY_OP_REQUEST_LOCK,
				   &reply_pages, &reply_len);
	if (ret && ret != -ETIMEDOUT) {
		rbd_warn(rbd_dev, "failed to request lock: %d", ret);
		goto out;
	}

	if (reply_len > 0 && reply_len <= PAGE_SIZE) {
		void *p = page_address(reply_pages[0]);
		void *const end = p + reply_len;
		u32 n;

		ceph_decode_32_safe(&p, end, n, e_inval); /* num_acks */
		while (n--) {
			u8 struct_v;
			u32 len;

			ceph_decode_need(&p, end, 8 + 8, e_inval);
			p += 8 + 8; /* skip gid and cookie */

			ceph_decode_32_safe(&p, end, len, e_inval);
			if (!len)
				continue;

			if (lock_owner_responded) {
				rbd_warn(rbd_dev,
					 "duplicate lock owners detected");
				ret = -EIO;
				goto out;
			}

			lock_owner_responded = true;
			ret = ceph_start_decoding(&p, end, 1, "ResponseMessage",
						  &struct_v, &len);
			if (ret) {
				rbd_warn(rbd_dev,
					 "failed to decode ResponseMessage: %d",
					 ret);
				goto e_inval;
			}

			ret = ceph_decode_32(&p);
		}
	}

	if (!lock_owner_responded) {
		rbd_warn(rbd_dev, "no lock owners detected");
		ret = -ETIMEDOUT;
	}

out:
	ceph_release_page_vector(reply_pages, calc_pages_for(0, reply_len));
	return ret;

e_inval:
	ret = -EINVAL;
	goto out;
}

/*
 * Either image request state machine(s) or rbd_add_acquire_lock()
 * (i.e. "rbd map").
 */
static void wake_lock_waiters(struct rbd_device *rbd_dev, int result)
{
	struct rbd_img_request *img_req;

	dout("%s rbd_dev %p result %d\n", __func__, rbd_dev, result);
	lockdep_assert_held_write(&rbd_dev->lock_rwsem);

	cancel_delayed_work(&rbd_dev->lock_dwork);
	if (!completion_done(&rbd_dev->acquire_wait)) {
		rbd_assert(list_empty(&rbd_dev->acquiring_list) &&
			   list_empty(&rbd_dev->running_list));
		rbd_dev->acquire_err = result;
		complete_all(&rbd_dev->acquire_wait);
		return;
	}

	list_for_each_entry(img_req, &rbd_dev->acquiring_list, lock_item) {
		mutex_lock(&img_req->state_mutex);
		rbd_assert(img_req->state == RBD_IMG_EXCLUSIVE_LOCK);
		rbd_img_schedule(img_req, result);
		mutex_unlock(&img_req->state_mutex);
	}

	list_splice_tail_init(&rbd_dev->acquiring_list, &rbd_dev->running_list);
}

static int get_lock_owner_info(struct rbd_device *rbd_dev,
			       struct ceph_locker **lockers, u32 *num_lockers)
{
	struct ceph_osd_client *osdc = &rbd_dev->rbd_client->client->osdc;
	u8 lock_type;
	char *lock_tag;
	int ret;

	dout("%s rbd_dev %p\n", __func__, rbd_dev);

	ret = ceph_cls_lock_info(osdc, &rbd_dev->header_oid,
				 &rbd_dev->header_oloc, RBD_LOCK_NAME,
				 &lock_type, &lock_tag, lockers, num_lockers);
	if (ret)
		return ret;

	if (*num_lockers == 0) {
		dout("%s rbd_dev %p no lockers detected\n", __func__, rbd_dev);
		goto out;
	}

	if (strcmp(lock_tag, RBD_LOCK_TAG)) {
		rbd_warn(rbd_dev, "locked by external mechanism, tag %s",
			 lock_tag);
		ret = -EBUSY;
		goto out;
	}

	if (lock_type == CEPH_CLS_LOCK_SHARED) {
		rbd_warn(rbd_dev, "shared lock type detected");
		ret = -EBUSY;
		goto out;
	}

	if (strncmp((*lockers)[0].id.cookie, RBD_LOCK_COOKIE_PREFIX,
		    strlen(RBD_LOCK_COOKIE_PREFIX))) {
		rbd_warn(rbd_dev, "locked by external mechanism, cookie %s",
			 (*lockers)[0].id.cookie);
		ret = -EBUSY;
		goto out;
	}

out:
	kfree(lock_tag);
	return ret;
}

static int find_watcher(struct rbd_device *rbd_dev,
			const struct ceph_locker *locker)
{
	struct ceph_osd_client *osdc = &rbd_dev->rbd_client->client->osdc;
	struct ceph_watch_item *watchers;
	u32 num_watchers;
	u64 cookie;
	int i;
	int ret;

	ret = ceph_osdc_list_watchers(osdc, &rbd_dev->header_oid,
				      &rbd_dev->header_oloc, &watchers,
				      &num_watchers);
	if (ret)
		return ret;

	sscanf(locker->id.cookie, RBD_LOCK_COOKIE_PREFIX " %llu", &cookie);
	for (i = 0; i < num_watchers; i++) {
		/*
		 * Ignore addr->type while comparing.  This mimics
		 * entity_addr_t::get_legacy_str() + strcmp().
		 */
		if (ceph_addr_equal_no_type(&watchers[i].addr,
					    &locker->info.addr) &&
		    watchers[i].cookie == cookie) {
			struct rbd_client_id cid = {
				.gid = le64_to_cpu(watchers[i].name.num),
				.handle = cookie,
			};

			dout("%s rbd_dev %p found cid %llu-%llu\n", __func__,
			     rbd_dev, cid.gid, cid.handle);
			rbd_set_owner_cid(rbd_dev, &cid);
			ret = 1;
			goto out;
		}
	}

	dout("%s rbd_dev %p no watchers\n", __func__, rbd_dev);
	ret = 0;
out:
	kfree(watchers);
	return ret;
}

/*
 * lock_rwsem must be held for write
 */
static int rbd_try_lock(struct rbd_device *rbd_dev)
{
	struct ceph_client *client = rbd_dev->rbd_client->client;
	struct ceph_locker *lockers;
	u32 num_lockers;
	int ret;

	for (;;) {
		ret = rbd_lock(rbd_dev);
		if (ret != -EBUSY)
			return ret;

		/* determine if the current lock holder is still alive */
		ret = get_lock_owner_info(rbd_dev, &lockers, &num_lockers);
		if (ret)
			return ret;

		if (num_lockers == 0)
			goto again;

		ret = find_watcher(rbd_dev, lockers);
		if (ret)
			goto out; /* request lock or error */

		rbd_warn(rbd_dev, "breaking header lock owned by %s%llu",
			 ENTITY_NAME(lockers[0].id.name));

		ret = ceph_monc_blocklist_add(&client->monc,
					      &lockers[0].info.addr);
		if (ret) {
			rbd_warn(rbd_dev, "blocklist of %s%llu failed: %d",
				 ENTITY_NAME(lockers[0].id.name), ret);
			goto out;
		}

		ret = ceph_cls_break_lock(&client->osdc, &rbd_dev->header_oid,
					  &rbd_dev->header_oloc, RBD_LOCK_NAME,
					  lockers[0].id.cookie,
					  &lockers[0].id.name);
		if (ret && ret != -ENOENT)
			goto out;

again:
		ceph_free_lockers(lockers, num_lockers);
	}

out:
	ceph_free_lockers(lockers, num_lockers);
	return ret;
}

static int rbd_post_acquire_action(struct rbd_device *rbd_dev)
{
	int ret;

	if (rbd_dev->header.features & RBD_FEATURE_OBJECT_MAP) {
		ret = rbd_object_map_open(rbd_dev);
		if (ret)
			return ret;
	}

	return 0;
}

/*
 * Return:
 *   0 - lock acquired
 *   1 - caller should call rbd_request_lock()
 *  <0 - error
 */
static int rbd_try_acquire_lock(struct rbd_device *rbd_dev)
{
	int ret;

	down_read(&rbd_dev->lock_rwsem);
	dout("%s rbd_dev %p read lock_state %d\n", __func__, rbd_dev,
	     rbd_dev->lock_state);
	if (__rbd_is_lock_owner(rbd_dev)) {
		up_read(&rbd_dev->lock_rwsem);
		return 0;
	}

	up_read(&rbd_dev->lock_rwsem);
	down_write(&rbd_dev->lock_rwsem);
	dout("%s rbd_dev %p write lock_state %d\n", __func__, rbd_dev,
	     rbd_dev->lock_state);
	if (__rbd_is_lock_owner(rbd_dev)) {
		up_write(&rbd_dev->lock_rwsem);
		return 0;
	}

	ret = rbd_try_lock(rbd_dev);
	if (ret < 0) {
		rbd_warn(rbd_dev, "failed to lock header: %d", ret);
		if (ret == -EBLOCKLISTED)
			goto out;

		ret = 1; /* request lock anyway */
	}
	if (ret > 0) {
		up_write(&rbd_dev->lock_rwsem);
		return ret;
	}

	rbd_assert(rbd_dev->lock_state == RBD_LOCK_STATE_LOCKED);
	rbd_assert(list_empty(&rbd_dev->running_list));

	ret = rbd_post_acquire_action(rbd_dev);
	if (ret) {
		rbd_warn(rbd_dev, "post-acquire action failed: %d", ret);
		/*
		 * Can't stay in RBD_LOCK_STATE_LOCKED because
		 * rbd_lock_add_request() would let the request through,
		 * assuming that e.g. object map is locked and loaded.
		 */
		rbd_unlock(rbd_dev);
	}

out:
	wake_lock_waiters(rbd_dev, ret);
	up_write(&rbd_dev->lock_rwsem);
	return ret;
}

static void rbd_acquire_lock(struct work_struct *work)
{
	struct rbd_device *rbd_dev = container_of(to_delayed_work(work),
					    struct rbd_device, lock_dwork);
	int ret;

	dout("%s rbd_dev %p\n", __func__, rbd_dev);
again:
	ret = rbd_try_acquire_lock(rbd_dev);
	if (ret <= 0) {
		dout("%s rbd_dev %p ret %d - done\n", __func__, rbd_dev, ret);
		return;
	}

	ret = rbd_request_lock(rbd_dev);
	if (ret == -ETIMEDOUT) {
		goto again; /* treat this as a dead client */
	} else if (ret == -EROFS) {
		rbd_warn(rbd_dev, "peer will not release lock");
		down_write(&rbd_dev->lock_rwsem);
		wake_lock_waiters(rbd_dev, ret);
		up_write(&rbd_dev->lock_rwsem);
	} else if (ret < 0) {
		rbd_warn(rbd_dev, "error requesting lock: %d", ret);
		mod_delayed_work(rbd_dev->task_wq, &rbd_dev->lock_dwork,
				 RBD_RETRY_DELAY);
	} else {
		/*
		 * lock owner acked, but resend if we don't see them
		 * release the lock
		 */
		dout("%s rbd_dev %p requeuing lock_dwork\n", __func__,
		     rbd_dev);
		mod_delayed_work(rbd_dev->task_wq, &rbd_dev->lock_dwork,
		    msecs_to_jiffies(2 * RBD_NOTIFY_TIMEOUT * MSEC_PER_SEC));
	}
}

static bool rbd_quiesce_lock(struct rbd_device *rbd_dev)
{
	dout("%s rbd_dev %p\n", __func__, rbd_dev);
	lockdep_assert_held_write(&rbd_dev->lock_rwsem);

	if (rbd_dev->lock_state != RBD_LOCK_STATE_LOCKED)
		return false;

	/*
	 * Ensure that all in-flight IO is flushed.
	 */
	rbd_dev->lock_state = RBD_LOCK_STATE_RELEASING;
	rbd_assert(!completion_done(&rbd_dev->releasing_wait));
	if (list_empty(&rbd_dev->running_list))
		return true;

	up_write(&rbd_dev->lock_rwsem);
	wait_for_completion(&rbd_dev->releasing_wait);

	down_write(&rbd_dev->lock_rwsem);
	if (rbd_dev->lock_state != RBD_LOCK_STATE_RELEASING)
		return false;

	rbd_assert(list_empty(&rbd_dev->running_list));
	return true;
}

static void rbd_pre_release_action(struct rbd_device *rbd_dev)
{
	if (rbd_dev->header.features & RBD_FEATURE_OBJECT_MAP)
		rbd_object_map_close(rbd_dev);
}

static void __rbd_release_lock(struct rbd_device *rbd_dev)
{
	rbd_assert(list_empty(&rbd_dev->running_list));

	rbd_pre_release_action(rbd_dev);
	rbd_unlock(rbd_dev);
}

/*
 * lock_rwsem must be held for write
 */
static void rbd_release_lock(struct rbd_device *rbd_dev)
{
	if (!rbd_quiesce_lock(rbd_dev))
		return;

	__rbd_release_lock(rbd_dev);

	/*
	 * Give others a chance to grab the lock - we would re-acquire
	 * almost immediately if we got new IO while draining the running
	 * list otherwise.  We need to ack our own notifications, so this
	 * lock_dwork will be requeued from rbd_handle_released_lock() by
	 * way of maybe_kick_acquire().
	 */
	cancel_delayed_work(&rbd_dev->lock_dwork);
}

static void rbd_release_lock_work(struct work_struct *work)
{
	struct rbd_device *rbd_dev = container_of(work, struct rbd_device,
						  unlock_work);

	down_write(&rbd_dev->lock_rwsem);
	rbd_release_lock(rbd_dev);
	up_write(&rbd_dev->lock_rwsem);
}

static void maybe_kick_acquire(struct rbd_device *rbd_dev)
{
	bool have_requests;

	dout("%s rbd_dev %p\n", __func__, rbd_dev);
	if (__rbd_is_lock_owner(rbd_dev))
		return;

	spin_lock(&rbd_dev->lock_lists_lock);
	have_requests = !list_empty(&rbd_dev->acquiring_list);
	spin_unlock(&rbd_dev->lock_lists_lock);
	if (have_requests || delayed_work_pending(&rbd_dev->lock_dwork)) {
		dout("%s rbd_dev %p kicking lock_dwork\n", __func__, rbd_dev);
		mod_delayed_work(rbd_dev->task_wq, &rbd_dev->lock_dwork, 0);
	}
}

static void rbd_handle_acquired_lock(struct rbd_device *rbd_dev, u8 struct_v,
				     void **p)
{
	struct rbd_client_id cid = { 0 };

	if (struct_v >= 2) {
		cid.gid = ceph_decode_64(p);
		cid.handle = ceph_decode_64(p);
	}

	dout("%s rbd_dev %p cid %llu-%llu\n", __func__, rbd_dev, cid.gid,
	     cid.handle);
	if (!rbd_cid_equal(&cid, &rbd_empty_cid)) {
		down_write(&rbd_dev->lock_rwsem);
		if (rbd_cid_equal(&cid, &rbd_dev->owner_cid)) {
			dout("%s rbd_dev %p cid %llu-%llu == owner_cid\n",
			     __func__, rbd_dev, cid.gid, cid.handle);
		} else {
			rbd_set_owner_cid(rbd_dev, &cid);
		}
		downgrade_write(&rbd_dev->lock_rwsem);
	} else {
		down_read(&rbd_dev->lock_rwsem);
	}

	maybe_kick_acquire(rbd_dev);
	up_read(&rbd_dev->lock_rwsem);
}

static void rbd_handle_released_lock(struct rbd_device *rbd_dev, u8 struct_v,
				     void **p)
{
	struct rbd_client_id cid = { 0 };

	if (struct_v >= 2) {
		cid.gid = ceph_decode_64(p);
		cid.handle = ceph_decode_64(p);
	}

	dout("%s rbd_dev %p cid %llu-%llu\n", __func__, rbd_dev, cid.gid,
	     cid.handle);
	if (!rbd_cid_equal(&cid, &rbd_empty_cid)) {
		down_write(&rbd_dev->lock_rwsem);
		if (!rbd_cid_equal(&cid, &rbd_dev->owner_cid)) {
			dout("%s rbd_dev %p cid %llu-%llu != owner_cid %llu-%llu\n",
			     __func__, rbd_dev, cid.gid, cid.handle,
			     rbd_dev->owner_cid.gid, rbd_dev->owner_cid.handle);
		} else {
			rbd_set_owner_cid(rbd_dev, &rbd_empty_cid);
		}
		downgrade_write(&rbd_dev->lock_rwsem);
	} else {
		down_read(&rbd_dev->lock_rwsem);
	}

	maybe_kick_acquire(rbd_dev);
	up_read(&rbd_dev->lock_rwsem);
}

/*
 * Returns result for ResponseMessage to be encoded (<= 0), or 1 if no
 * ResponseMessage is needed.
 */
static int rbd_handle_request_lock(struct rbd_device *rbd_dev, u8 struct_v,
				   void **p)
{
	struct rbd_client_id my_cid = rbd_get_cid(rbd_dev);
	struct rbd_client_id cid = { 0 };
	int result = 1;

	if (struct_v >= 2) {
		cid.gid = ceph_decode_64(p);
		cid.handle = ceph_decode_64(p);
	}

	dout("%s rbd_dev %p cid %llu-%llu\n", __func__, rbd_dev, cid.gid,
	     cid.handle);
	if (rbd_cid_equal(&cid, &my_cid))
		return result;

	down_read(&rbd_dev->lock_rwsem);
	if (__rbd_is_lock_owner(rbd_dev)) {
		if (rbd_dev->lock_state == RBD_LOCK_STATE_LOCKED &&
		    rbd_cid_equal(&rbd_dev->owner_cid, &rbd_empty_cid))
			goto out_unlock;

		/*
		 * encode ResponseMessage(0) so the peer can detect
		 * a missing owner
		 */
		result = 0;

		if (rbd_dev->lock_state == RBD_LOCK_STATE_LOCKED) {
			if (!rbd_dev->opts->exclusive) {
				dout("%s rbd_dev %p queueing unlock_work\n",
				     __func__, rbd_dev);
				queue_work(rbd_dev->task_wq,
					   &rbd_dev->unlock_work);
			} else {
				/* refuse to release the lock */
				result = -EROFS;
			}
		}
	}

out_unlock:
	up_read(&rbd_dev->lock_rwsem);
	return result;
}

static void __rbd_acknowledge_notify(struct rbd_device *rbd_dev,
				     u64 notify_id, u64 cookie, s32 *result)
{
	struct ceph_osd_client *osdc = &rbd_dev->rbd_client->client->osdc;
	char buf[4 + CEPH_ENCODING_START_BLK_LEN];
	int buf_size = sizeof(buf);
	int ret;

	if (result) {
		void *p = buf;

		/* encode ResponseMessage */
		ceph_start_encoding(&p, 1, 1,
				    buf_size - CEPH_ENCODING_START_BLK_LEN);
		ceph_encode_32(&p, *result);
	} else {
		buf_size = 0;
	}

	ret = ceph_osdc_notify_ack(osdc, &rbd_dev->header_oid,
				   &rbd_dev->header_oloc, notify_id, cookie,
				   buf, buf_size);
	if (ret)
		rbd_warn(rbd_dev, "acknowledge_notify failed: %d", ret);
}

static void rbd_acknowledge_notify(struct rbd_device *rbd_dev, u64 notify_id,
				   u64 cookie)
{
	dout("%s rbd_dev %p\n", __func__, rbd_dev);
	__rbd_acknowledge_notify(rbd_dev, notify_id, cookie, NULL);
}

static void rbd_acknowledge_notify_result(struct rbd_device *rbd_dev,
					  u64 notify_id, u64 cookie, s32 result)
{
	dout("%s rbd_dev %p result %d\n", __func__, rbd_dev, result);
	__rbd_acknowledge_notify(rbd_dev, notify_id, cookie, &result);
}

static void rbd_watch_cb(void *arg, u64 notify_id, u64 cookie,
			 u64 notifier_id, void *data, size_t data_len)
{
	struct rbd_device *rbd_dev = arg;
	void *p = data;
	void *const end = p + data_len;
	u8 struct_v = 0;
	u32 len;
	u32 notify_op;
	int ret;

	dout("%s rbd_dev %p cookie %llu notify_id %llu data_len %zu\n",
	     __func__, rbd_dev, cookie, notify_id, data_len);
	if (data_len) {
		ret = ceph_start_decoding(&p, end, 1, "NotifyMessage",
					  &struct_v, &len);
		if (ret) {
			rbd_warn(rbd_dev, "failed to decode NotifyMessage: %d",
				 ret);
			return;
		}

		notify_op = ceph_decode_32(&p);
	} else {
		/* legacy notification for header updates */
		notify_op = RBD_NOTIFY_OP_HEADER_UPDATE;
		len = 0;
	}

	dout("%s rbd_dev %p notify_op %u\n", __func__, rbd_dev, notify_op);
	switch (notify_op) {
	case RBD_NOTIFY_OP_ACQUIRED_LOCK:
		rbd_handle_acquired_lock(rbd_dev, struct_v, &p);
		rbd_acknowledge_notify(rbd_dev, notify_id, cookie);
		break;
	case RBD_NOTIFY_OP_RELEASED_LOCK:
		rbd_handle_released_lock(rbd_dev, struct_v, &p);
		rbd_acknowledge_notify(rbd_dev, notify_id, cookie);
		break;
	case RBD_NOTIFY_OP_REQUEST_LOCK:
		ret = rbd_handle_request_lock(rbd_dev, struct_v, &p);
		if (ret <= 0)
			rbd_acknowledge_notify_result(rbd_dev, notify_id,
						      cookie, ret);
		else
			rbd_acknowledge_notify(rbd_dev, notify_id, cookie);
		break;
	case RBD_NOTIFY_OP_HEADER_UPDATE:
		ret = rbd_dev_refresh(rbd_dev);
		if (ret)
			rbd_warn(rbd_dev, "refresh failed: %d", ret);

		rbd_acknowledge_notify(rbd_dev, notify_id, cookie);
		break;
	default:
		if (rbd_is_lock_owner(rbd_dev))
			rbd_acknowledge_notify_result(rbd_dev, notify_id,
						      cookie, -EOPNOTSUPP);
		else
			rbd_acknowledge_notify(rbd_dev, notify_id, cookie);
		break;
	}
}

static void __rbd_unregister_watch(struct rbd_device *rbd_dev);

static void rbd_watch_errcb(void *arg, u64 cookie, int err)
{
	struct rbd_device *rbd_dev = arg;

	rbd_warn(rbd_dev, "encountered watch error: %d", err);

	down_write(&rbd_dev->lock_rwsem);
	rbd_set_owner_cid(rbd_dev, &rbd_empty_cid);
	up_write(&rbd_dev->lock_rwsem);

	mutex_lock(&rbd_dev->watch_mutex);
	if (rbd_dev->watch_state == RBD_WATCH_STATE_REGISTERED) {
		__rbd_unregister_watch(rbd_dev);
		rbd_dev->watch_state = RBD_WATCH_STATE_ERROR;

		queue_delayed_work(rbd_dev->task_wq, &rbd_dev->watch_dwork, 0);
	}
	mutex_unlock(&rbd_dev->watch_mutex);
}

/*
 * watch_mutex must be locked
 */
static int __rbd_register_watch(struct rbd_device *rbd_dev)
{
	struct ceph_osd_client *osdc = &rbd_dev->rbd_client->client->osdc;
	struct ceph_osd_linger_request *handle;

	rbd_assert(!rbd_dev->watch_handle);
	dout("%s rbd_dev %p\n", __func__, rbd_dev);

	handle = ceph_osdc_watch(osdc, &rbd_dev->header_oid,
				 &rbd_dev->header_oloc, rbd_watch_cb,
				 rbd_watch_errcb, rbd_dev);
	if (IS_ERR(handle))
		return PTR_ERR(handle);

	rbd_dev->watch_handle = handle;
	return 0;
}

/*
 * watch_mutex must be locked
 */
static void __rbd_unregister_watch(struct rbd_device *rbd_dev)
{
	struct ceph_osd_client *osdc = &rbd_dev->rbd_client->client->osdc;
	int ret;

	rbd_assert(rbd_dev->watch_handle);
	dout("%s rbd_dev %p\n", __func__, rbd_dev);

	ret = ceph_osdc_unwatch(osdc, rbd_dev->watch_handle);
	if (ret)
		rbd_warn(rbd_dev, "failed to unwatch: %d", ret);

	rbd_dev->watch_handle = NULL;
}

static int rbd_register_watch(struct rbd_device *rbd_dev)
{
	int ret;

	mutex_lock(&rbd_dev->watch_mutex);
	rbd_assert(rbd_dev->watch_state == RBD_WATCH_STATE_UNREGISTERED);
	ret = __rbd_register_watch(rbd_dev);
	if (ret)
		goto out;

	rbd_dev->watch_state = RBD_WATCH_STATE_REGISTERED;
	rbd_dev->watch_cookie = rbd_dev->watch_handle->linger_id;

out:
	mutex_unlock(&rbd_dev->watch_mutex);
	return ret;
}

static void cancel_tasks_sync(struct rbd_device *rbd_dev)
{
	dout("%s rbd_dev %p\n", __func__, rbd_dev);

	cancel_work_sync(&rbd_dev->acquired_lock_work);
	cancel_work_sync(&rbd_dev->released_lock_work);
	cancel_delayed_work_sync(&rbd_dev->lock_dwork);
	cancel_work_sync(&rbd_dev->unlock_work);
}

/*
 * header_rwsem must not be held to avoid a deadlock with
 * rbd_dev_refresh() when flushing notifies.
 */
static void rbd_unregister_watch(struct rbd_device *rbd_dev)
{
	cancel_tasks_sync(rbd_dev);

	mutex_lock(&rbd_dev->watch_mutex);
	if (rbd_dev->watch_state == RBD_WATCH_STATE_REGISTERED)
		__rbd_unregister_watch(rbd_dev);
	rbd_dev->watch_state = RBD_WATCH_STATE_UNREGISTERED;
	mutex_unlock(&rbd_dev->watch_mutex);

	cancel_delayed_work_sync(&rbd_dev->watch_dwork);
	ceph_osdc_flush_notifies(&rbd_dev->rbd_client->client->osdc);
}

/*
 * lock_rwsem must be held for write
 */
static void rbd_reacquire_lock(struct rbd_device *rbd_dev)
{
	struct ceph_osd_client *osdc = &rbd_dev->rbd_client->client->osdc;
	char cookie[32];
	int ret;

	if (!rbd_quiesce_lock(rbd_dev))
		return;

	format_lock_cookie(rbd_dev, cookie);
	ret = ceph_cls_set_cookie(osdc, &rbd_dev->header_oid,
				  &rbd_dev->header_oloc, RBD_LOCK_NAME,
				  CEPH_CLS_LOCK_EXCLUSIVE, rbd_dev->lock_cookie,
				  RBD_LOCK_TAG, cookie);
	if (ret) {
		if (ret != -EOPNOTSUPP)
			rbd_warn(rbd_dev, "failed to update lock cookie: %d",
				 ret);

		/*
		 * Lock cookie cannot be updated on older OSDs, so do
		 * a manual release and queue an acquire.
		 */
		__rbd_release_lock(rbd_dev);
		queue_delayed_work(rbd_dev->task_wq, &rbd_dev->lock_dwork, 0);
	} else {
		__rbd_lock(rbd_dev, cookie);
		wake_lock_waiters(rbd_dev, 0);
	}
}

static void rbd_reregister_watch(struct work_struct *work)
{
	struct rbd_device *rbd_dev = container_of(to_delayed_work(work),
					    struct rbd_device, watch_dwork);
	int ret;

	dout("%s rbd_dev %p\n", __func__, rbd_dev);

	mutex_lock(&rbd_dev->watch_mutex);
	if (rbd_dev->watch_state != RBD_WATCH_STATE_ERROR) {
		mutex_unlock(&rbd_dev->watch_mutex);
		return;
	}

	ret = __rbd_register_watch(rbd_dev);
	if (ret) {
		rbd_warn(rbd_dev, "failed to reregister watch: %d", ret);
		if (ret != -EBLOCKLISTED && ret != -ENOENT) {
			queue_delayed_work(rbd_dev->task_wq,
					   &rbd_dev->watch_dwork,
					   RBD_RETRY_DELAY);
			mutex_unlock(&rbd_dev->watch_mutex);
			return;
		}

		mutex_unlock(&rbd_dev->watch_mutex);
		down_write(&rbd_dev->lock_rwsem);
		wake_lock_waiters(rbd_dev, ret);
		up_write(&rbd_dev->lock_rwsem);
		return;
	}

	rbd_dev->watch_state = RBD_WATCH_STATE_REGISTERED;
	rbd_dev->watch_cookie = rbd_dev->watch_handle->linger_id;
	mutex_unlock(&rbd_dev->watch_mutex);

	down_write(&rbd_dev->lock_rwsem);
	if (rbd_dev->lock_state == RBD_LOCK_STATE_LOCKED)
		rbd_reacquire_lock(rbd_dev);
	up_write(&rbd_dev->lock_rwsem);

	ret = rbd_dev_refresh(rbd_dev);
	if (ret)
		rbd_warn(rbd_dev, "reregistration refresh failed: %d", ret);
}

/*
 * Synchronous osd object method call.  Returns the number of bytes
 * returned in the outbound buffer, or a negative error code.
 */
static int rbd_obj_method_sync(struct rbd_device *rbd_dev,
			     struct ceph_object_id *oid,
			     struct ceph_object_locator *oloc,
			     const char *method_name,
			     const void *outbound,
			     size_t outbound_size,
			     void *inbound,
			     size_t inbound_size)
{
	struct ceph_osd_client *osdc = &rbd_dev->rbd_client->client->osdc;
	struct page *req_page = NULL;
	struct page *reply_page;
	int ret;

	/*
	 * Method calls are ultimately read operations.  The result
	 * should placed into the inbound buffer provided.  They
	 * also supply outbound data--parameters for the object
	 * method.  Currently if this is present it will be a
	 * snapshot id.
	 */
	if (outbound) {
		if (outbound_size > PAGE_SIZE)
			return -E2BIG;

		req_page = alloc_page(GFP_KERNEL);
		if (!req_page)
			return -ENOMEM;

		memcpy(page_address(req_page), outbound, outbound_size);
	}

	reply_page = alloc_page(GFP_KERNEL);
	if (!reply_page) {
		if (req_page)
			__free_page(req_page);
		return -ENOMEM;
	}

	ret = ceph_osdc_call(osdc, oid, oloc, RBD_DRV_NAME, method_name,
			     CEPH_OSD_FLAG_READ, req_page, outbound_size,
			     &reply_page, &inbound_size);
	if (!ret) {
		memcpy(inbound, page_address(reply_page), inbound_size);
		ret = inbound_size;
	}

	if (req_page)
		__free_page(req_page);
	__free_page(reply_page);
	return ret;
}

static void rbd_queue_workfn(struct work_struct *work)
{
	struct rbd_img_request *img_request =
	    container_of(work, struct rbd_img_request, work);
	struct rbd_device *rbd_dev = img_request->rbd_dev;
	enum obj_operation_type op_type = img_request->op_type;
	struct request *rq = blk_mq_rq_from_pdu(img_request);
	u64 offset = (u64)blk_rq_pos(rq) << SECTOR_SHIFT;
	u64 length = blk_rq_bytes(rq);
	u64 mapping_size;
	int result;

	/* Ignore/skip any zero-length requests */
	if (!length) {
		dout("%s: zero-length request\n", __func__);
		result = 0;
		goto err_img_request;
	}

	blk_mq_start_request(rq);

	down_read(&rbd_dev->header_rwsem);
	mapping_size = rbd_dev->mapping.size;
	rbd_img_capture_header(img_request);
	up_read(&rbd_dev->header_rwsem);

	if (offset + length > mapping_size) {
		rbd_warn(rbd_dev, "beyond EOD (%llu~%llu > %llu)", offset,
			 length, mapping_size);
		result = -EIO;
		goto err_img_request;
	}

	dout("%s rbd_dev %p img_req %p %s %llu~%llu\n", __func__, rbd_dev,
	     img_request, obj_op_name(op_type), offset, length);

	if (op_type == OBJ_OP_DISCARD || op_type == OBJ_OP_ZEROOUT)
		result = rbd_img_fill_nodata(img_request, offset, length);
	else
		result = rbd_img_fill_from_bio(img_request, offset, length,
					       rq->bio);
	if (result)
		goto err_img_request;

	rbd_img_handle_request(img_request, 0);
	return;

err_img_request:
	rbd_img_request_destroy(img_request);
	if (result)
		rbd_warn(rbd_dev, "%s %llx at %llx result %d",
			 obj_op_name(op_type), length, offset, result);
	blk_mq_end_request(rq, errno_to_blk_status(result));
}

static blk_status_t rbd_queue_rq(struct blk_mq_hw_ctx *hctx,
		const struct blk_mq_queue_data *bd)
{
	struct rbd_device *rbd_dev = hctx->queue->queuedata;
	struct rbd_img_request *img_req = blk_mq_rq_to_pdu(bd->rq);
	enum obj_operation_type op_type;

	switch (req_op(bd->rq)) {
	case REQ_OP_DISCARD:
		op_type = OBJ_OP_DISCARD;
		break;
	case REQ_OP_WRITE_ZEROES:
		op_type = OBJ_OP_ZEROOUT;
		break;
	case REQ_OP_WRITE:
		op_type = OBJ_OP_WRITE;
		break;
	case REQ_OP_READ:
		op_type = OBJ_OP_READ;
		break;
	default:
		rbd_warn(rbd_dev, "unknown req_op %d", req_op(bd->rq));
		return BLK_STS_IOERR;
	}

	rbd_img_request_init(img_req, rbd_dev, op_type);

	if (rbd_img_is_write(img_req)) {
		if (rbd_is_ro(rbd_dev)) {
			rbd_warn(rbd_dev, "%s on read-only mapping",
				 obj_op_name(img_req->op_type));
			return BLK_STS_IOERR;
		}
		rbd_assert(!rbd_is_snap(rbd_dev));
	}

	INIT_WORK(&img_req->work, rbd_queue_workfn);
	queue_work(rbd_wq, &img_req->work);
	return BLK_STS_OK;
}

static void rbd_free_disk(struct rbd_device *rbd_dev)
{
	put_disk(rbd_dev->disk);
	blk_mq_free_tag_set(&rbd_dev->tag_set);
	rbd_dev->disk = NULL;
}

static int rbd_obj_read_sync(struct rbd_device *rbd_dev,
			     struct ceph_object_id *oid,
			     struct ceph_object_locator *oloc,
			     void *buf, int buf_len)

{
	struct ceph_osd_client *osdc = &rbd_dev->rbd_client->client->osdc;
	struct ceph_osd_request *req;
	struct page **pages;
	int num_pages = calc_pages_for(0, buf_len);
	int ret;

	req = ceph_osdc_alloc_request(osdc, NULL, 1, false, GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	ceph_oid_copy(&req->r_base_oid, oid);
	ceph_oloc_copy(&req->r_base_oloc, oloc);
	req->r_flags = CEPH_OSD_FLAG_READ;

	pages = ceph_alloc_page_vector(num_pages, GFP_KERNEL);
	if (IS_ERR(pages)) {
		ret = PTR_ERR(pages);
		goto out_req;
	}

	osd_req_op_extent_init(req, 0, CEPH_OSD_OP_READ, 0, buf_len, 0, 0);
	osd_req_op_extent_osd_data_pages(req, 0, pages, buf_len, 0, false,
					 true);

	ret = ceph_osdc_alloc_messages(req, GFP_KERNEL);
	if (ret)
		goto out_req;

	ceph_osdc_start_request(osdc, req);
	ret = ceph_osdc_wait_request(osdc, req);
	if (ret >= 0)
		ceph_copy_from_page_vector(pages, buf, 0, ret);

out_req:
	ceph_osdc_put_request(req);
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

		ret = rbd_obj_read_sync(rbd_dev, &rbd_dev->header_oid,
					&rbd_dev->header_oloc, ondisk, size);
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

static void rbd_dev_update_size(struct rbd_device *rbd_dev)
{
	sector_t size;

	/*
	 * If EXISTS is not set, rbd_dev->disk may be NULL, so don't
	 * try to update its size.  If REMOVING is set, updating size
	 * is just useless work since the device can't be opened.
	 */
	if (test_bit(RBD_DEV_FLAG_EXISTS, &rbd_dev->flags) &&
	    !test_bit(RBD_DEV_FLAG_REMOVING, &rbd_dev->flags)) {
		size = (sector_t)rbd_dev->mapping.size / SECTOR_SIZE;
		dout("setting size to %llu sectors", (unsigned long long)size);
		set_capacity_and_notify(rbd_dev->disk, size);
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

	rbd_assert(!rbd_is_snap(rbd_dev));
	rbd_dev->mapping.size = rbd_dev->header.image_size;

out:
	up_write(&rbd_dev->header_rwsem);
	if (!ret && mapping_size != rbd_dev->mapping.size)
		rbd_dev_update_size(rbd_dev);

	return ret;
}

static const struct blk_mq_ops rbd_mq_ops = {
	.queue_rq	= rbd_queue_rq,
};

static int rbd_init_disk(struct rbd_device *rbd_dev)
{
	struct gendisk *disk;
	struct request_queue *q;
	unsigned int objset_bytes =
	    rbd_dev->layout.object_size * rbd_dev->layout.stripe_count;
	int err;

	memset(&rbd_dev->tag_set, 0, sizeof(rbd_dev->tag_set));
	rbd_dev->tag_set.ops = &rbd_mq_ops;
	rbd_dev->tag_set.queue_depth = rbd_dev->opts->queue_depth;
	rbd_dev->tag_set.numa_node = NUMA_NO_NODE;
	rbd_dev->tag_set.flags = BLK_MQ_F_SHOULD_MERGE;
	rbd_dev->tag_set.nr_hw_queues = num_present_cpus();
	rbd_dev->tag_set.cmd_size = sizeof(struct rbd_img_request);

	err = blk_mq_alloc_tag_set(&rbd_dev->tag_set);
	if (err)
		return err;

	disk = blk_mq_alloc_disk(&rbd_dev->tag_set, rbd_dev);
	if (IS_ERR(disk)) {
		err = PTR_ERR(disk);
		goto out_tag_set;
	}
	q = disk->queue;

	snprintf(disk->disk_name, sizeof(disk->disk_name), RBD_DRV_NAME "%d",
		 rbd_dev->dev_id);
	disk->major = rbd_dev->major;
	disk->first_minor = rbd_dev->minor;
	if (single_major)
		disk->minors = (1 << RBD_SINGLE_MAJOR_PART_SHIFT);
	else
		disk->minors = RBD_MINORS_PER_MAJOR;
	disk->fops = &rbd_bd_ops;
	disk->private_data = rbd_dev;

	blk_queue_flag_set(QUEUE_FLAG_NONROT, q);
	/* QUEUE_FLAG_ADD_RANDOM is off by default for blk-mq */

	blk_queue_max_hw_sectors(q, objset_bytes >> SECTOR_SHIFT);
	q->limits.max_sectors = queue_max_hw_sectors(q);
	blk_queue_max_segments(q, USHRT_MAX);
	blk_queue_max_segment_size(q, UINT_MAX);
	blk_queue_io_min(q, rbd_dev->opts->alloc_size);
	blk_queue_io_opt(q, rbd_dev->opts->alloc_size);

	if (rbd_dev->opts->trim) {
		q->limits.discard_granularity = rbd_dev->opts->alloc_size;
		blk_queue_max_discard_sectors(q, objset_bytes >> SECTOR_SHIFT);
		blk_queue_max_write_zeroes_sectors(q, objset_bytes >> SECTOR_SHIFT);
	}

	if (!ceph_test_opt(rbd_dev->rbd_client->client, NOCRC))
		blk_queue_flag_set(QUEUE_FLAG_STABLE_WRITES, q);

	rbd_dev->disk = disk;

	return 0;
out_tag_set:
	blk_mq_free_tag_set(&rbd_dev->tag_set);
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

static ssize_t rbd_features_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct rbd_device *rbd_dev = dev_to_rbd_dev(dev);

	return sprintf(buf, "0x%016llx\n", rbd_dev->header.features);
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

static ssize_t rbd_client_addr_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct rbd_device *rbd_dev = dev_to_rbd_dev(dev);
	struct ceph_entity_addr *client_addr =
	    ceph_client_addr(rbd_dev->rbd_client->client);

	return sprintf(buf, "%pISpc/%u\n", &client_addr->in_addr,
		       le32_to_cpu(client_addr->nonce));
}

static ssize_t rbd_client_id_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct rbd_device *rbd_dev = dev_to_rbd_dev(dev);

	return sprintf(buf, "client%lld\n",
		       ceph_client_gid(rbd_dev->rbd_client->client));
}

static ssize_t rbd_cluster_fsid_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct rbd_device *rbd_dev = dev_to_rbd_dev(dev);

	return sprintf(buf, "%pU\n", &rbd_dev->rbd_client->client->fsid);
}

static ssize_t rbd_config_info_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct rbd_device *rbd_dev = dev_to_rbd_dev(dev);

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	return sprintf(buf, "%s\n", rbd_dev->config_info);
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

static ssize_t rbd_pool_ns_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct rbd_device *rbd_dev = dev_to_rbd_dev(dev);

	return sprintf(buf, "%s\n", rbd_dev->spec->pool_ns ?: "");
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

static ssize_t rbd_snap_id_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct rbd_device *rbd_dev = dev_to_rbd_dev(dev);

	return sprintf(buf, "%llu\n", rbd_dev->spec->snap_id);
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
			    "pool_ns %s\n"
			    "image_id %s\nimage_name %s\n"
			    "snap_id %llu\nsnap_name %s\n"
			    "overlap %llu\n",
			    !count ? "" : "\n", /* first? */
			    spec->pool_id, spec->pool_name,
			    spec->pool_ns ?: "",
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

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	ret = rbd_dev_refresh(rbd_dev);
	if (ret)
		return ret;

	return size;
}

static DEVICE_ATTR(size, 0444, rbd_size_show, NULL);
static DEVICE_ATTR(features, 0444, rbd_features_show, NULL);
static DEVICE_ATTR(major, 0444, rbd_major_show, NULL);
static DEVICE_ATTR(minor, 0444, rbd_minor_show, NULL);
static DEVICE_ATTR(client_addr, 0444, rbd_client_addr_show, NULL);
static DEVICE_ATTR(client_id, 0444, rbd_client_id_show, NULL);
static DEVICE_ATTR(cluster_fsid, 0444, rbd_cluster_fsid_show, NULL);
static DEVICE_ATTR(config_info, 0400, rbd_config_info_show, NULL);
static DEVICE_ATTR(pool, 0444, rbd_pool_show, NULL);
static DEVICE_ATTR(pool_id, 0444, rbd_pool_id_show, NULL);
static DEVICE_ATTR(pool_ns, 0444, rbd_pool_ns_show, NULL);
static DEVICE_ATTR(name, 0444, rbd_name_show, NULL);
static DEVICE_ATTR(image_id, 0444, rbd_image_id_show, NULL);
static DEVICE_ATTR(refresh, 0200, NULL, rbd_image_refresh);
static DEVICE_ATTR(current_snap, 0444, rbd_snap_show, NULL);
static DEVICE_ATTR(snap_id, 0444, rbd_snap_id_show, NULL);
static DEVICE_ATTR(parent, 0444, rbd_parent_show, NULL);

static struct attribute *rbd_attrs[] = {
	&dev_attr_size.attr,
	&dev_attr_features.attr,
	&dev_attr_major.attr,
	&dev_attr_minor.attr,
	&dev_attr_client_addr.attr,
	&dev_attr_client_id.attr,
	&dev_attr_cluster_fsid.attr,
	&dev_attr_config_info.attr,
	&dev_attr_pool.attr,
	&dev_attr_pool_id.attr,
	&dev_attr_pool_ns.attr,
	&dev_attr_name.attr,
	&dev_attr_image_id.attr,
	&dev_attr_current_snap.attr,
	&dev_attr_snap_id.attr,
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

static const struct device_type rbd_device_type = {
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
	kfree(spec->pool_ns);
	kfree(spec->image_id);
	kfree(spec->image_name);
	kfree(spec->snap_name);
	kfree(spec);
}

static void rbd_dev_free(struct rbd_device *rbd_dev)
{
	WARN_ON(rbd_dev->watch_state != RBD_WATCH_STATE_UNREGISTERED);
	WARN_ON(rbd_dev->lock_state != RBD_LOCK_STATE_UNLOCKED);

	ceph_oid_destroy(&rbd_dev->header_oid);
	ceph_oloc_destroy(&rbd_dev->header_oloc);
	kfree(rbd_dev->config_info);

	rbd_put_client(rbd_dev->rbd_client);
	rbd_spec_put(rbd_dev->spec);
	kfree(rbd_dev->opts);
	kfree(rbd_dev);
}

static void rbd_dev_release(struct device *dev)
{
	struct rbd_device *rbd_dev = dev_to_rbd_dev(dev);
	bool need_put = !!rbd_dev->opts;

	if (need_put) {
		destroy_workqueue(rbd_dev->task_wq);
		ida_simple_remove(&rbd_dev_id_ida, rbd_dev->dev_id);
	}

	rbd_dev_free(rbd_dev);

	/*
	 * This is racy, but way better than putting module outside of
	 * the release callback.  The race window is pretty small, so
	 * doing something similar to dm (dm-builtin.c) is overkill.
	 */
	if (need_put)
		module_put(THIS_MODULE);
}

static struct rbd_device *__rbd_dev_create(struct rbd_spec *spec)
{
	struct rbd_device *rbd_dev;

	rbd_dev = kzalloc(sizeof(*rbd_dev), GFP_KERNEL);
	if (!rbd_dev)
		return NULL;

	spin_lock_init(&rbd_dev->lock);
	INIT_LIST_HEAD(&rbd_dev->node);
	init_rwsem(&rbd_dev->header_rwsem);

	rbd_dev->header.data_pool_id = CEPH_NOPOOL;
	ceph_oid_init(&rbd_dev->header_oid);
	rbd_dev->header_oloc.pool = spec->pool_id;
	if (spec->pool_ns) {
		WARN_ON(!*spec->pool_ns);
		rbd_dev->header_oloc.pool_ns =
		    ceph_find_or_create_string(spec->pool_ns,
					       strlen(spec->pool_ns));
	}

	mutex_init(&rbd_dev->watch_mutex);
	rbd_dev->watch_state = RBD_WATCH_STATE_UNREGISTERED;
	INIT_DELAYED_WORK(&rbd_dev->watch_dwork, rbd_reregister_watch);

	init_rwsem(&rbd_dev->lock_rwsem);
	rbd_dev->lock_state = RBD_LOCK_STATE_UNLOCKED;
	INIT_WORK(&rbd_dev->acquired_lock_work, rbd_notify_acquired_lock);
	INIT_WORK(&rbd_dev->released_lock_work, rbd_notify_released_lock);
	INIT_DELAYED_WORK(&rbd_dev->lock_dwork, rbd_acquire_lock);
	INIT_WORK(&rbd_dev->unlock_work, rbd_release_lock_work);
	spin_lock_init(&rbd_dev->lock_lists_lock);
	INIT_LIST_HEAD(&rbd_dev->acquiring_list);
	INIT_LIST_HEAD(&rbd_dev->running_list);
	init_completion(&rbd_dev->acquire_wait);
	init_completion(&rbd_dev->releasing_wait);

	spin_lock_init(&rbd_dev->object_map_lock);

	rbd_dev->dev.bus = &rbd_bus_type;
	rbd_dev->dev.type = &rbd_device_type;
	rbd_dev->dev.parent = &rbd_root_dev;
	device_initialize(&rbd_dev->dev);

	return rbd_dev;
}

/*
 * Create a mapping rbd_dev.
 */
static struct rbd_device *rbd_dev_create(struct rbd_client *rbdc,
					 struct rbd_spec *spec,
					 struct rbd_options *opts)
{
	struct rbd_device *rbd_dev;

	rbd_dev = __rbd_dev_create(spec);
	if (!rbd_dev)
		return NULL;

	/* get an id and fill in device name */
	rbd_dev->dev_id = ida_simple_get(&rbd_dev_id_ida, 0,
					 minor_to_rbd_dev_id(1 << MINORBITS),
					 GFP_KERNEL);
	if (rbd_dev->dev_id < 0)
		goto fail_rbd_dev;

	sprintf(rbd_dev->name, RBD_DRV_NAME "%d", rbd_dev->dev_id);
	rbd_dev->task_wq = alloc_ordered_workqueue("%s-tasks", WQ_MEM_RECLAIM,
						   rbd_dev->name);
	if (!rbd_dev->task_wq)
		goto fail_dev_id;

	/* we have a ref from do_rbd_add() */
	__module_get(THIS_MODULE);

	rbd_dev->rbd_client = rbdc;
	rbd_dev->spec = spec;
	rbd_dev->opts = opts;

	dout("%s rbd_dev %p dev_id %d\n", __func__, rbd_dev, rbd_dev->dev_id);
	return rbd_dev;

fail_dev_id:
	ida_simple_remove(&rbd_dev_id_ida, rbd_dev->dev_id);
fail_rbd_dev:
	rbd_dev_free(rbd_dev);
	return NULL;
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

	ret = rbd_obj_method_sync(rbd_dev, &rbd_dev->header_oid,
				  &rbd_dev->header_oloc, "get_size",
				  &snapid, sizeof(snapid),
				  &size_buf, sizeof(size_buf));
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
	size_t size;
	void *reply_buf;
	int ret;
	void *p;

	/* Response will be an encoded string, which includes a length */
	size = sizeof(__le32) + RBD_OBJ_PREFIX_LEN_MAX;
	reply_buf = kzalloc(size, GFP_KERNEL);
	if (!reply_buf)
		return -ENOMEM;

	ret = rbd_obj_method_sync(rbd_dev, &rbd_dev->header_oid,
				  &rbd_dev->header_oloc, "get_object_prefix",
				  NULL, 0, reply_buf, size);
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
				     bool read_only, u64 *snap_features)
{
	struct {
		__le64 snap_id;
		u8 read_only;
	} features_in;
	struct {
		__le64 features;
		__le64 incompat;
	} __attribute__ ((packed)) features_buf = { 0 };
	u64 unsup;
	int ret;

	features_in.snap_id = cpu_to_le64(snap_id);
	features_in.read_only = read_only;

	ret = rbd_obj_method_sync(rbd_dev, &rbd_dev->header_oid,
				  &rbd_dev->header_oloc, "get_features",
				  &features_in, sizeof(features_in),
				  &features_buf, sizeof(features_buf));
	dout("%s: rbd_obj_method_sync returned %d\n", __func__, ret);
	if (ret < 0)
		return ret;
	if (ret < sizeof (features_buf))
		return -ERANGE;

	unsup = le64_to_cpu(features_buf.incompat) & ~RBD_FEATURES_SUPPORTED;
	if (unsup) {
		rbd_warn(rbd_dev, "image uses unsupported features: 0x%llx",
			 unsup);
		return -ENXIO;
	}

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
					 rbd_is_ro(rbd_dev),
					 &rbd_dev->header.features);
}

/*
 * These are generic image flags, but since they are used only for
 * object map, store them in rbd_dev->object_map_flags.
 *
 * For the same reason, this function is called only on object map
 * (re)load and not on header refresh.
 */
static int rbd_dev_v2_get_flags(struct rbd_device *rbd_dev)
{
	__le64 snapid = cpu_to_le64(rbd_dev->spec->snap_id);
	__le64 flags;
	int ret;

	ret = rbd_obj_method_sync(rbd_dev, &rbd_dev->header_oid,
				  &rbd_dev->header_oloc, "get_flags",
				  &snapid, sizeof(snapid),
				  &flags, sizeof(flags));
	if (ret < 0)
		return ret;
	if (ret < sizeof(flags))
		return -EBADMSG;

	rbd_dev->object_map_flags = le64_to_cpu(flags);
	return 0;
}

struct parent_image_info {
	u64		pool_id;
	const char	*pool_ns;
	const char	*image_id;
	u64		snap_id;

	bool		has_overlap;
	u64		overlap;
};

/*
 * The caller is responsible for @pii.
 */
static int decode_parent_image_spec(void **p, void *end,
				    struct parent_image_info *pii)
{
	u8 struct_v;
	u32 struct_len;
	int ret;

	ret = ceph_start_decoding(p, end, 1, "ParentImageSpec",
				  &struct_v, &struct_len);
	if (ret)
		return ret;

	ceph_decode_64_safe(p, end, pii->pool_id, e_inval);
	pii->pool_ns = ceph_extract_encoded_string(p, end, NULL, GFP_KERNEL);
	if (IS_ERR(pii->pool_ns)) {
		ret = PTR_ERR(pii->pool_ns);
		pii->pool_ns = NULL;
		return ret;
	}
	pii->image_id = ceph_extract_encoded_string(p, end, NULL, GFP_KERNEL);
	if (IS_ERR(pii->image_id)) {
		ret = PTR_ERR(pii->image_id);
		pii->image_id = NULL;
		return ret;
	}
	ceph_decode_64_safe(p, end, pii->snap_id, e_inval);
	return 0;

e_inval:
	return -EINVAL;
}

static int __get_parent_info(struct rbd_device *rbd_dev,
			     struct page *req_page,
			     struct page *reply_page,
			     struct parent_image_info *pii)
{
	struct ceph_osd_client *osdc = &rbd_dev->rbd_client->client->osdc;
	size_t reply_len = PAGE_SIZE;
	void *p, *end;
	int ret;

	ret = ceph_osdc_call(osdc, &rbd_dev->header_oid, &rbd_dev->header_oloc,
			     "rbd", "parent_get", CEPH_OSD_FLAG_READ,
			     req_page, sizeof(u64), &reply_page, &reply_len);
	if (ret)
		return ret == -EOPNOTSUPP ? 1 : ret;

	p = page_address(reply_page);
	end = p + reply_len;
	ret = decode_parent_image_spec(&p, end, pii);
	if (ret)
		return ret;

	ret = ceph_osdc_call(osdc, &rbd_dev->header_oid, &rbd_dev->header_oloc,
			     "rbd", "parent_overlap_get", CEPH_OSD_FLAG_READ,
			     req_page, sizeof(u64), &reply_page, &reply_len);
	if (ret)
		return ret;

	p = page_address(reply_page);
	end = p + reply_len;
	ceph_decode_8_safe(&p, end, pii->has_overlap, e_inval);
	if (pii->has_overlap)
		ceph_decode_64_safe(&p, end, pii->overlap, e_inval);

	return 0;

e_inval:
	return -EINVAL;
}

/*
 * The caller is responsible for @pii.
 */
static int __get_parent_info_legacy(struct rbd_device *rbd_dev,
				    struct page *req_page,
				    struct page *reply_page,
				    struct parent_image_info *pii)
{
	struct ceph_osd_client *osdc = &rbd_dev->rbd_client->client->osdc;
	size_t reply_len = PAGE_SIZE;
	void *p, *end;
	int ret;

	ret = ceph_osdc_call(osdc, &rbd_dev->header_oid, &rbd_dev->header_oloc,
			     "rbd", "get_parent", CEPH_OSD_FLAG_READ,
			     req_page, sizeof(u64), &reply_page, &reply_len);
	if (ret)
		return ret;

	p = page_address(reply_page);
	end = p + reply_len;
	ceph_decode_64_safe(&p, end, pii->pool_id, e_inval);
	pii->image_id = ceph_extract_encoded_string(&p, end, NULL, GFP_KERNEL);
	if (IS_ERR(pii->image_id)) {
		ret = PTR_ERR(pii->image_id);
		pii->image_id = NULL;
		return ret;
	}
	ceph_decode_64_safe(&p, end, pii->snap_id, e_inval);
	pii->has_overlap = true;
	ceph_decode_64_safe(&p, end, pii->overlap, e_inval);

	return 0;

e_inval:
	return -EINVAL;
}

static int get_parent_info(struct rbd_device *rbd_dev,
			   struct parent_image_info *pii)
{
	struct page *req_page, *reply_page;
	void *p;
	int ret;

	req_page = alloc_page(GFP_KERNEL);
	if (!req_page)
		return -ENOMEM;

	reply_page = alloc_page(GFP_KERNEL);
	if (!reply_page) {
		__free_page(req_page);
		return -ENOMEM;
	}

	p = page_address(req_page);
	ceph_encode_64(&p, rbd_dev->spec->snap_id);
	ret = __get_parent_info(rbd_dev, req_page, reply_page, pii);
	if (ret > 0)
		ret = __get_parent_info_legacy(rbd_dev, req_page, reply_page,
					       pii);

	__free_page(req_page);
	__free_page(reply_page);
	return ret;
}

static int rbd_dev_v2_parent_info(struct rbd_device *rbd_dev)
{
	struct rbd_spec *parent_spec;
	struct parent_image_info pii = { 0 };
	int ret;

	parent_spec = rbd_spec_alloc();
	if (!parent_spec)
		return -ENOMEM;

	ret = get_parent_info(rbd_dev, &pii);
	if (ret)
		goto out_err;

	dout("%s pool_id %llu pool_ns %s image_id %s snap_id %llu has_overlap %d overlap %llu\n",
	     __func__, pii.pool_id, pii.pool_ns, pii.image_id, pii.snap_id,
	     pii.has_overlap, pii.overlap);

	if (pii.pool_id == CEPH_NOPOOL || !pii.has_overlap) {
		/*
		 * Either the parent never existed, or we have
		 * record of it but the image got flattened so it no
		 * longer has a parent.  When the parent of a
		 * layered image disappears we immediately set the
		 * overlap to 0.  The effect of this is that all new
		 * requests will be treated as if the image had no
		 * parent.
		 *
		 * If !pii.has_overlap, the parent image spec is not
		 * applicable.  It's there to avoid duplication in each
		 * snapshot record.
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
	if (pii.pool_id > (u64)U32_MAX) {
		rbd_warn(NULL, "parent pool id too large (%llu > %u)",
			(unsigned long long)pii.pool_id, U32_MAX);
		goto out_err;
	}

	/*
	 * The parent won't change (except when the clone is
	 * flattened, already handled that).  So we only need to
	 * record the parent spec we have not already done so.
	 */
	if (!rbd_dev->parent_spec) {
		parent_spec->pool_id = pii.pool_id;
		if (pii.pool_ns && *pii.pool_ns) {
			parent_spec->pool_ns = pii.pool_ns;
			pii.pool_ns = NULL;
		}
		parent_spec->image_id = pii.image_id;
		pii.image_id = NULL;
		parent_spec->snap_id = pii.snap_id;

		rbd_dev->parent_spec = parent_spec;
		parent_spec = NULL;	/* rbd_dev now owns this */
	}

	/*
	 * We always update the parent overlap.  If it's zero we issue
	 * a warning, as we will proceed as if there was no parent.
	 */
	if (!pii.overlap) {
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
	rbd_dev->parent_overlap = pii.overlap;

out:
	ret = 0;
out_err:
	kfree(pii.pool_ns);
	kfree(pii.image_id);
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
	int ret;

	ret = rbd_obj_method_sync(rbd_dev, &rbd_dev->header_oid,
				&rbd_dev->header_oloc, "get_stripe_unit_count",
				NULL, 0, &striping_info_buf, size);
	dout("%s: rbd_obj_method_sync returned %d\n", __func__, ret);
	if (ret < 0)
		return ret;
	if (ret < size)
		return -ERANGE;

	p = &striping_info_buf;
	rbd_dev->header.stripe_unit = ceph_decode_64(&p);
	rbd_dev->header.stripe_count = ceph_decode_64(&p);
	return 0;
}

static int rbd_dev_v2_data_pool(struct rbd_device *rbd_dev)
{
	__le64 data_pool_id;
	int ret;

	ret = rbd_obj_method_sync(rbd_dev, &rbd_dev->header_oid,
				  &rbd_dev->header_oloc, "get_data_pool",
				  NULL, 0, &data_pool_id, sizeof(data_pool_id));
	if (ret < 0)
		return ret;
	if (ret < sizeof(data_pool_id))
		return -EBADMSG;

	rbd_dev->header.data_pool_id = le64_to_cpu(data_pool_id);
	WARN_ON(rbd_dev->header.data_pool_id == CEPH_NOPOOL);
	return 0;
}

static char *rbd_dev_image_name(struct rbd_device *rbd_dev)
{
	CEPH_DEFINE_OID_ONSTACK(oid);
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

	ceph_oid_printf(&oid, "%s", RBD_DIRECTORY);
	ret = rbd_obj_method_sync(rbd_dev, &oid, &rbd_dev->header_oloc,
				  "dir_get_name", image_id, image_id_size,
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

	ret = rbd_obj_method_sync(rbd_dev, &rbd_dev->header_oid,
				  &rbd_dev->header_oloc, "get_snapcontext",
				  NULL, 0, reply_buf, size);
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
	ret = rbd_obj_method_sync(rbd_dev, &rbd_dev->header_oid,
				  &rbd_dev->header_oloc, "get_snapshot_name",
				  &snapid, sizeof(snapid), reply_buf, size);
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
	static const char spaces[] = " \f\n\r\t\v";

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

static int rbd_parse_param(struct fs_parameter *param,
			    struct rbd_parse_opts_ctx *pctx)
{
	struct rbd_options *opt = pctx->opts;
	struct fs_parse_result result;
	struct p_log log = {.prefix = "rbd"};
	int token, ret;

	ret = ceph_parse_param(param, pctx->copts, NULL);
	if (ret != -ENOPARAM)
		return ret;

	token = __fs_parse(&log, rbd_parameters, param, &result);
	dout("%s fs_parse '%s' token %d\n", __func__, param->key, token);
	if (token < 0) {
		if (token == -ENOPARAM)
			return inval_plog(&log, "Unknown parameter '%s'",
					  param->key);
		return token;
	}

	switch (token) {
	case Opt_queue_depth:
		if (result.uint_32 < 1)
			goto out_of_range;
		opt->queue_depth = result.uint_32;
		break;
	case Opt_alloc_size:
		if (result.uint_32 < SECTOR_SIZE)
			goto out_of_range;
		if (!is_power_of_2(result.uint_32))
			return inval_plog(&log, "alloc_size must be a power of 2");
		opt->alloc_size = result.uint_32;
		break;
	case Opt_lock_timeout:
		/* 0 is "wait forever" (i.e. infinite timeout) */
		if (result.uint_32 > INT_MAX / 1000)
			goto out_of_range;
		opt->lock_timeout = msecs_to_jiffies(result.uint_32 * 1000);
		break;
	case Opt_pool_ns:
		kfree(pctx->spec->pool_ns);
		pctx->spec->pool_ns = param->string;
		param->string = NULL;
		break;
	case Opt_compression_hint:
		switch (result.uint_32) {
		case Opt_compression_hint_none:
			opt->alloc_hint_flags &=
			    ~(CEPH_OSD_ALLOC_HINT_FLAG_COMPRESSIBLE |
			      CEPH_OSD_ALLOC_HINT_FLAG_INCOMPRESSIBLE);
			break;
		case Opt_compression_hint_compressible:
			opt->alloc_hint_flags |=
			    CEPH_OSD_ALLOC_HINT_FLAG_COMPRESSIBLE;
			opt->alloc_hint_flags &=
			    ~CEPH_OSD_ALLOC_HINT_FLAG_INCOMPRESSIBLE;
			break;
		case Opt_compression_hint_incompressible:
			opt->alloc_hint_flags |=
			    CEPH_OSD_ALLOC_HINT_FLAG_INCOMPRESSIBLE;
			opt->alloc_hint_flags &=
			    ~CEPH_OSD_ALLOC_HINT_FLAG_COMPRESSIBLE;
			break;
		default:
			BUG();
		}
		break;
	case Opt_read_only:
		opt->read_only = true;
		break;
	case Opt_read_write:
		opt->read_only = false;
		break;
	case Opt_lock_on_read:
		opt->lock_on_read = true;
		break;
	case Opt_exclusive:
		opt->exclusive = true;
		break;
	case Opt_notrim:
		opt->trim = false;
		break;
	default:
		BUG();
	}

	return 0;

out_of_range:
	return inval_plog(&log, "%s out of range", param->key);
}

/*
 * This duplicates most of generic_parse_monolithic(), untying it from
 * fs_context and skipping standard superblock and security options.
 */
static int rbd_parse_options(char *options, struct rbd_parse_opts_ctx *pctx)
{
	char *key;
	int ret = 0;

	dout("%s '%s'\n", __func__, options);
	while ((key = strsep(&options, ",")) != NULL) {
		if (*key) {
			struct fs_parameter param = {
				.key	= key,
				.type	= fs_value_is_flag,
			};
			char *value = strchr(key, '=');
			size_t v_len = 0;

			if (value) {
				if (value == key)
					continue;
				*value++ = 0;
				v_len = strlen(value);
				param.string = kmemdup_nul(value, v_len,
							   GFP_KERNEL);
				if (!param.string)
					return -ENOMEM;
				param.type = fs_value_is_string;
			}
			param.size = v_len;

			ret = rbd_parse_param(&param, pctx);
			kfree(param.string);
			if (ret)
				break;
		}
	}

	return ret;
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
	struct rbd_parse_opts_ctx pctx = { 0 };
	int ret;

	/* The first four tokens are required */

	len = next_token(&buf);
	if (!len) {
		rbd_warn(NULL, "no monitor address(es) provided");
		return -EINVAL;
	}
	mon_addrs = buf;
	mon_addrs_size = len;
	buf += len;

	ret = -EINVAL;
	options = dup_token(&buf, NULL);
	if (!options)
		return -ENOMEM;
	if (!*options) {
		rbd_warn(NULL, "no options provided");
		goto out_err;
	}

	pctx.spec = rbd_spec_alloc();
	if (!pctx.spec)
		goto out_mem;

	pctx.spec->pool_name = dup_token(&buf, NULL);
	if (!pctx.spec->pool_name)
		goto out_mem;
	if (!*pctx.spec->pool_name) {
		rbd_warn(NULL, "no pool name provided");
		goto out_err;
	}

	pctx.spec->image_name = dup_token(&buf, NULL);
	if (!pctx.spec->image_name)
		goto out_mem;
	if (!*pctx.spec->image_name) {
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
	pctx.spec->snap_name = snap_name;

	pctx.copts = ceph_alloc_options();
	if (!pctx.copts)
		goto out_mem;

	/* Initialize all rbd options to the defaults */

	pctx.opts = kzalloc(sizeof(*pctx.opts), GFP_KERNEL);
	if (!pctx.opts)
		goto out_mem;

	pctx.opts->read_only = RBD_READ_ONLY_DEFAULT;
	pctx.opts->queue_depth = RBD_QUEUE_DEPTH_DEFAULT;
	pctx.opts->alloc_size = RBD_ALLOC_SIZE_DEFAULT;
	pctx.opts->lock_timeout = RBD_LOCK_TIMEOUT_DEFAULT;
	pctx.opts->lock_on_read = RBD_LOCK_ON_READ_DEFAULT;
	pctx.opts->exclusive = RBD_EXCLUSIVE_DEFAULT;
	pctx.opts->trim = RBD_TRIM_DEFAULT;

	ret = ceph_parse_mon_ips(mon_addrs, mon_addrs_size, pctx.copts, NULL,
				 ',');
	if (ret)
		goto out_err;

	ret = rbd_parse_options(options, &pctx);
	if (ret)
		goto out_err;

	*ceph_opts = pctx.copts;
	*opts = pctx.opts;
	*rbd_spec = pctx.spec;
	kfree(options);
	return 0;

out_mem:
	ret = -ENOMEM;
out_err:
	kfree(pctx.opts);
	ceph_destroy_options(pctx.copts);
	rbd_spec_put(pctx.spec);
	kfree(options);
	return ret;
}

static void rbd_dev_image_unlock(struct rbd_device *rbd_dev)
{
	down_write(&rbd_dev->lock_rwsem);
	if (__rbd_is_lock_owner(rbd_dev))
		__rbd_release_lock(rbd_dev);
	up_write(&rbd_dev->lock_rwsem);
}

/*
 * If the wait is interrupted, an error is returned even if the lock
 * was successfully acquired.  rbd_dev_image_unlock() will release it
 * if needed.
 */
static int rbd_add_acquire_lock(struct rbd_device *rbd_dev)
{
	long ret;

	if (!(rbd_dev->header.features & RBD_FEATURE_EXCLUSIVE_LOCK)) {
		if (!rbd_dev->opts->exclusive && !rbd_dev->opts->lock_on_read)
			return 0;

		rbd_warn(rbd_dev, "exclusive-lock feature is not enabled");
		return -EINVAL;
	}

	if (rbd_is_ro(rbd_dev))
		return 0;

	rbd_assert(!rbd_is_lock_owner(rbd_dev));
	queue_delayed_work(rbd_dev->task_wq, &rbd_dev->lock_dwork, 0);
	ret = wait_for_completion_killable_timeout(&rbd_dev->acquire_wait,
			    ceph_timeout_jiffies(rbd_dev->opts->lock_timeout));
	if (ret > 0) {
		ret = rbd_dev->acquire_err;
	} else {
		cancel_delayed_work_sync(&rbd_dev->lock_dwork);
		if (!ret)
			ret = -ETIMEDOUT;
	}

	if (ret) {
		rbd_warn(rbd_dev, "failed to acquire exclusive lock: %ld", ret);
		return ret;
	}

	/*
	 * The lock may have been released by now, unless automatic lock
	 * transitions are disabled.
	 */
	rbd_assert(!rbd_dev->opts->exclusive || rbd_is_lock_owner(rbd_dev));
	return 0;
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
	CEPH_DEFINE_OID_ONSTACK(oid);
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
	ret = ceph_oid_aprintf(&oid, GFP_KERNEL, "%s%s", RBD_ID_PREFIX,
			       rbd_dev->spec->image_name);
	if (ret)
		return ret;

	dout("rbd id object name is %s\n", oid.name);

	/* Response will be an encoded string, which includes a length */
	size = sizeof (__le32) + RBD_IMAGE_ID_LEN_MAX;
	response = kzalloc(size, GFP_NOIO);
	if (!response) {
		ret = -ENOMEM;
		goto out;
	}

	/* If it doesn't exist we'll assume it's a format 1 image */

	ret = rbd_obj_method_sync(rbd_dev, &oid, &rbd_dev->header_oloc,
				  "get_id", NULL, 0,
				  response, size);
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
	ceph_oid_destroy(&oid);
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
	rbd_object_map_free(rbd_dev);
	rbd_dev_mapping_clear(rbd_dev);

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

	if (rbd_dev->header.features & RBD_FEATURE_DATA_POOL) {
		ret = rbd_dev_v2_data_pool(rbd_dev);
		if (ret)
			goto out_err;
	}

	rbd_init_layout(rbd_dev);
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

	parent = __rbd_dev_create(rbd_dev->parent_spec);
	if (!parent) {
		ret = -ENOMEM;
		goto out_err;
	}

	/*
	 * Images related by parent/child relationships always share
	 * rbd_client and spec/parent_spec, so bump their refcounts.
	 */
	parent->rbd_client = __rbd_get_client(rbd_dev->rbd_client);
	parent->spec = rbd_spec_get(rbd_dev->parent_spec);

	__set_bit(RBD_DEV_FLAG_READONLY, &parent->flags);

	ret = rbd_dev_image_probe(parent, depth);
	if (ret < 0)
		goto out_err;

	rbd_dev->parent = parent;
	atomic_set(&rbd_dev->parent_ref, 1);
	return 0;

out_err:
	rbd_dev_unparent(rbd_dev);
	rbd_dev_destroy(parent);
	return ret;
}

static void rbd_dev_device_release(struct rbd_device *rbd_dev)
{
	clear_bit(RBD_DEV_FLAG_EXISTS, &rbd_dev->flags);
	rbd_free_disk(rbd_dev);
	if (!single_major)
		unregister_blkdev(rbd_dev->major, rbd_dev->name);
}

/*
 * rbd_dev->header_rwsem must be locked for write and will be unlocked
 * upon return.
 */
static int rbd_dev_device_setup(struct rbd_device *rbd_dev)
{
	int ret;

	/* Record our major and minor device numbers. */

	if (!single_major) {
		ret = register_blkdev(0, rbd_dev->name);
		if (ret < 0)
			goto err_out_unlock;

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

	set_capacity(rbd_dev->disk, rbd_dev->mapping.size / SECTOR_SIZE);
	set_disk_ro(rbd_dev->disk, rbd_is_ro(rbd_dev));

	ret = dev_set_name(&rbd_dev->dev, "%d", rbd_dev->dev_id);
	if (ret)
		goto err_out_disk;

	set_bit(RBD_DEV_FLAG_EXISTS, &rbd_dev->flags);
	up_write(&rbd_dev->header_rwsem);
	return 0;

err_out_disk:
	rbd_free_disk(rbd_dev);
err_out_blkdev:
	if (!single_major)
		unregister_blkdev(rbd_dev->major, rbd_dev->name);
err_out_unlock:
	up_write(&rbd_dev->header_rwsem);
	return ret;
}

static int rbd_dev_header_name(struct rbd_device *rbd_dev)
{
	struct rbd_spec *spec = rbd_dev->spec;
	int ret;

	/* Record the header object name for this rbd image. */

	rbd_assert(rbd_image_format_valid(rbd_dev->image_format));
	if (rbd_dev->image_format == 1)
		ret = ceph_oid_aprintf(&rbd_dev->header_oid, GFP_KERNEL, "%s%s",
				       spec->image_name, RBD_SUFFIX);
	else
		ret = ceph_oid_aprintf(&rbd_dev->header_oid, GFP_KERNEL, "%s%s",
				       RBD_HEADER_PREFIX, spec->image_id);

	return ret;
}

static void rbd_print_dne(struct rbd_device *rbd_dev, bool is_snap)
{
	if (!is_snap) {
		pr_info("image %s/%s%s%s does not exist\n",
			rbd_dev->spec->pool_name,
			rbd_dev->spec->pool_ns ?: "",
			rbd_dev->spec->pool_ns ? "/" : "",
			rbd_dev->spec->image_name);
	} else {
		pr_info("snap %s/%s%s%s@%s does not exist\n",
			rbd_dev->spec->pool_name,
			rbd_dev->spec->pool_ns ?: "",
			rbd_dev->spec->pool_ns ? "/" : "",
			rbd_dev->spec->image_name,
			rbd_dev->spec->snap_name);
	}
}

static void rbd_dev_image_release(struct rbd_device *rbd_dev)
{
	if (!rbd_is_ro(rbd_dev))
		rbd_unregister_watch(rbd_dev);

	rbd_dev_unprobe(rbd_dev);
	rbd_dev->image_format = 0;
	kfree(rbd_dev->spec->image_id);
	rbd_dev->spec->image_id = NULL;
}

/*
 * Probe for the existence of the header object for the given rbd
 * device.  If this image is the one being mapped (i.e., not a
 * parent), initiate a watch on its header object before using that
 * object to get detailed information about the rbd image.
 *
 * On success, returns with header_rwsem held for write if called
 * with @depth == 0.
 */
static int rbd_dev_image_probe(struct rbd_device *rbd_dev, int depth)
{
	bool need_watch = !rbd_is_ro(rbd_dev);
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

	if (need_watch) {
		ret = rbd_register_watch(rbd_dev);
		if (ret) {
			if (ret == -ENOENT)
				rbd_print_dne(rbd_dev, false);
			goto err_out_format;
		}
	}

	if (!depth)
		down_write(&rbd_dev->header_rwsem);

	ret = rbd_dev_header_info(rbd_dev);
	if (ret) {
		if (ret == -ENOENT && !need_watch)
			rbd_print_dne(rbd_dev, false);
		goto err_out_probe;
	}

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
			rbd_print_dne(rbd_dev, true);
		goto err_out_probe;
	}

	ret = rbd_dev_mapping_set(rbd_dev);
	if (ret)
		goto err_out_probe;

	if (rbd_is_snap(rbd_dev) &&
	    (rbd_dev->header.features & RBD_FEATURE_OBJECT_MAP)) {
		ret = rbd_object_map_load(rbd_dev);
		if (ret)
			goto err_out_probe;
	}

	if (rbd_dev->header.features & RBD_FEATURE_LAYERING) {
		ret = rbd_dev_v2_parent_info(rbd_dev);
		if (ret)
			goto err_out_probe;
	}

	ret = rbd_dev_probe_parent(rbd_dev, depth);
	if (ret)
		goto err_out_probe;

	dout("discovered format %u image, header name is %s\n",
		rbd_dev->image_format, rbd_dev->header_oid.name);
	return 0;

err_out_probe:
	if (!depth)
		up_write(&rbd_dev->header_rwsem);
	if (need_watch)
		rbd_unregister_watch(rbd_dev);
	rbd_dev_unprobe(rbd_dev);
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
	int rc;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

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
	rc = ceph_pg_poolid_by_name(rbdc->client->osdc.osdmap, spec->pool_name);
	if (rc < 0) {
		if (rc == -ENOENT)
			pr_info("pool %s does not exist\n", spec->pool_name);
		goto err_out_client;
	}
	spec->pool_id = (u64)rc;

	rbd_dev = rbd_dev_create(rbdc, spec, rbd_opts);
	if (!rbd_dev) {
		rc = -ENOMEM;
		goto err_out_client;
	}
	rbdc = NULL;		/* rbd_dev now owns this */
	spec = NULL;		/* rbd_dev now owns this */
	rbd_opts = NULL;	/* rbd_dev now owns this */

	/* if we are mapping a snapshot it will be a read-only mapping */
	if (rbd_dev->opts->read_only ||
	    strcmp(rbd_dev->spec->snap_name, RBD_SNAP_HEAD_NAME))
		__set_bit(RBD_DEV_FLAG_READONLY, &rbd_dev->flags);

	rbd_dev->config_info = kstrdup(buf, GFP_KERNEL);
	if (!rbd_dev->config_info) {
		rc = -ENOMEM;
		goto err_out_rbd_dev;
	}

	rc = rbd_dev_image_probe(rbd_dev, 0);
	if (rc < 0)
		goto err_out_rbd_dev;

	if (rbd_dev->opts->alloc_size > rbd_dev->layout.object_size) {
		rbd_warn(rbd_dev, "alloc_size adjusted to %u",
			 rbd_dev->layout.object_size);
		rbd_dev->opts->alloc_size = rbd_dev->layout.object_size;
	}

	rc = rbd_dev_device_setup(rbd_dev);
	if (rc)
		goto err_out_image_probe;

	rc = rbd_add_acquire_lock(rbd_dev);
	if (rc)
		goto err_out_image_lock;

	/* Everything's ready.  Announce the disk to the world. */

	rc = device_add(&rbd_dev->dev);
	if (rc)
		goto err_out_image_lock;

	rc = device_add_disk(&rbd_dev->dev, rbd_dev->disk, NULL);
	if (rc)
		goto err_out_cleanup_disk;

	spin_lock(&rbd_dev_list_lock);
	list_add_tail(&rbd_dev->node, &rbd_dev_list);
	spin_unlock(&rbd_dev_list_lock);

	pr_info("%s: capacity %llu features 0x%llx\n", rbd_dev->disk->disk_name,
		(unsigned long long)get_capacity(rbd_dev->disk) << SECTOR_SHIFT,
		rbd_dev->header.features);
	rc = count;
out:
	module_put(THIS_MODULE);
	return rc;

err_out_cleanup_disk:
	rbd_free_disk(rbd_dev);
err_out_image_lock:
	rbd_dev_image_unlock(rbd_dev);
	rbd_dev_device_release(rbd_dev);
err_out_image_probe:
	rbd_dev_image_release(rbd_dev);
err_out_rbd_dev:
	rbd_dev_destroy(rbd_dev);
err_out_client:
	rbd_put_client(rbdc);
err_out_args:
	rbd_spec_put(spec);
	kfree(rbd_opts);
	goto out;
}

static ssize_t add_store(struct bus_type *bus, const char *buf, size_t count)
{
	if (single_major)
		return -EINVAL;

	return do_rbd_add(bus, buf, count);
}

static ssize_t add_single_major_store(struct bus_type *bus, const char *buf,
				      size_t count)
{
	return do_rbd_add(bus, buf, count);
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
		rbd_dev_destroy(second);
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
	char opt_buf[6];
	bool force = false;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	dev_id = -1;
	opt_buf[0] = '\0';
	sscanf(buf, "%d %5s", &dev_id, opt_buf);
	if (dev_id < 0) {
		pr_err("dev_id out of range\n");
		return -EINVAL;
	}
	if (opt_buf[0] != '\0') {
		if (!strcmp(opt_buf, "force")) {
			force = true;
		} else {
			pr_err("bad remove option at '%s'\n", opt_buf);
			return -EINVAL;
		}
	}

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
		if (rbd_dev->open_count && !force)
			ret = -EBUSY;
		else if (test_and_set_bit(RBD_DEV_FLAG_REMOVING,
					  &rbd_dev->flags))
			ret = -EINPROGRESS;
		spin_unlock_irq(&rbd_dev->lock);
	}
	spin_unlock(&rbd_dev_list_lock);
	if (ret)
		return ret;

	if (force) {
		/*
		 * Prevent new IO from being queued and wait for existing
		 * IO to complete/fail.
		 */
		blk_mq_freeze_queue(rbd_dev->disk->queue);
		blk_mark_disk_dead(rbd_dev->disk);
	}

	del_gendisk(rbd_dev->disk);
	spin_lock(&rbd_dev_list_lock);
	list_del_init(&rbd_dev->node);
	spin_unlock(&rbd_dev_list_lock);
	device_del(&rbd_dev->dev);

	rbd_dev_image_unlock(rbd_dev);
	rbd_dev_device_release(rbd_dev);
	rbd_dev_image_release(rbd_dev);
	rbd_dev_destroy(rbd_dev);
	return count;
}

static ssize_t remove_store(struct bus_type *bus, const char *buf, size_t count)
{
	if (single_major)
		return -EINVAL;

	return do_rbd_remove(bus, buf, count);
}

static ssize_t remove_single_major_store(struct bus_type *bus, const char *buf,
					 size_t count)
{
	return do_rbd_remove(bus, buf, count);
}

/*
 * create control files in sysfs
 * /sys/bus/rbd/...
 */
static int __init rbd_sysfs_init(void)
{
	int ret;

	ret = device_register(&rbd_root_dev);
	if (ret < 0) {
		put_device(&rbd_root_dev);
		return ret;
	}

	ret = bus_register(&rbd_bus_type);
	if (ret < 0)
		device_unregister(&rbd_root_dev);

	return ret;
}

static void __exit rbd_sysfs_cleanup(void)
{
	bus_unregister(&rbd_bus_type);
	device_unregister(&rbd_root_dev);
}

static int __init rbd_slab_init(void)
{
	rbd_assert(!rbd_img_request_cache);
	rbd_img_request_cache = KMEM_CACHE(rbd_img_request, 0);
	if (!rbd_img_request_cache)
		return -ENOMEM;

	rbd_assert(!rbd_obj_request_cache);
	rbd_obj_request_cache = KMEM_CACHE(rbd_obj_request, 0);
	if (!rbd_obj_request_cache)
		goto out_err;

	return 0;

out_err:
	kmem_cache_destroy(rbd_img_request_cache);
	rbd_img_request_cache = NULL;
	return -ENOMEM;
}

static void rbd_slab_exit(void)
{
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
