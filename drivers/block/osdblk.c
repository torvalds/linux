
/*
   osdblk.c -- Export a single SCSI OSD object as a Linux block device


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

   1) Map a Linux block device to an existing OSD object.

      In this example, we will use partition id 1234, object id 5678,
      OSD device /dev/osd1.

      $ echo "1234 5678 /dev/osd1" > /sys/class/osdblk/add


   2) List all active blkdev<->object mappings.

      In this example, we have performed step #1 twice, creating two blkdevs,
      mapped to two separate OSD objects.

      $ cat /sys/class/osdblk/list
      0 174 1234 5678 /dev/osd1
      1 179 1994 897123 /dev/osd0

      The columns, in order, are:
      - blkdev unique id
      - blkdev assigned major
      - OSD object partition id
      - OSD object id
      - OSD device


   3) Remove an active blkdev<->object mapping.

      In this example, we remove the mapping with blkdev unique id 1.

      $ echo 1 > /sys/class/osdblk/remove


   NOTE:  The actual creation and deletion of OSD objects is outside the scope
   of this driver.

 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <scsi/osd_initiator.h>
#include <scsi/osd_attributes.h>
#include <scsi/osd_sec.h>
#include <scsi/scsi_device.h>

#define DRV_NAME "osdblk"
#define PFX DRV_NAME ": "

/* #define _OSDBLK_DEBUG */
#ifdef _OSDBLK_DEBUG
#define OSDBLK_DEBUG(fmt, a...) \
	printk(KERN_NOTICE "osdblk @%s:%d: " fmt, __func__, __LINE__, ##a)
#else
#define OSDBLK_DEBUG(fmt, a...) \
	do { if (0) printk(fmt, ##a); } while (0)
#endif

MODULE_AUTHOR("Jeff Garzik <jeff@garzik.org>");
MODULE_DESCRIPTION("block device inside an OSD object osdblk.ko");
MODULE_LICENSE("GPL");

struct osdblk_device;

enum {
	OSDBLK_MINORS_PER_MAJOR	= 256,		/* max minors per blkdev */
	OSDBLK_MAX_REQ		= 32,		/* max parallel requests */
	OSDBLK_OP_TIMEOUT	= 4 * 60,	/* sync OSD req timeout */
};

struct osdblk_request {
	struct request		*rq;		/* blk layer request */
	struct bio		*bio;		/* cloned bio */
	struct osdblk_device	*osdev;		/* associated blkdev */
};

struct osdblk_device {
	int			id;		/* blkdev unique id */

	int			major;		/* blkdev assigned major */
	struct gendisk		*disk;		/* blkdev's gendisk and rq */
	struct request_queue	*q;

	struct osd_dev		*osd;		/* associated OSD */

	char			name[32];	/* blkdev name, e.g. osdblk34 */

	spinlock_t		lock;		/* queue lock */

	struct osd_obj_id	obj;		/* OSD partition, obj id */
	uint8_t			obj_cred[OSD_CAP_LEN]; /* OSD cred */

	struct osdblk_request	req[OSDBLK_MAX_REQ]; /* request table */

	struct list_head	node;

	char			osd_path[0];	/* OSD device path */
};

static struct class *class_osdblk;		/* /sys/class/osdblk */
static DEFINE_MUTEX(ctl_mutex);	/* Serialize open/close/setup/teardown */
static LIST_HEAD(osdblkdev_list);

static const struct block_device_operations osdblk_bd_ops = {
	.owner		= THIS_MODULE,
};

static const struct osd_attr g_attr_logical_length = ATTR_DEF(
	OSD_APAGE_OBJECT_INFORMATION, OSD_ATTR_OI_LOGICAL_LENGTH, 8);

static void osdblk_make_credential(u8 cred_a[OSD_CAP_LEN],
				   const struct osd_obj_id *obj)
{
	osd_sec_init_nosec_doall_caps(cred_a, obj, false, true);
}

/* copied from exofs; move to libosd? */
/*
 * Perform a synchronous OSD operation.  copied from exofs; move to libosd?
 */
static int osd_sync_op(struct osd_request *or, int timeout, uint8_t *credential)
{
	int ret;

	or->timeout = timeout;
	ret = osd_finalize_request(or, 0, credential, NULL);
	if (ret)
		return ret;

	ret = osd_execute_request(or);

	/* osd_req_decode_sense(or, ret); */
	return ret;
}

/*
 * Perform an asynchronous OSD operation.  copied from exofs; move to libosd?
 */
static int osd_async_op(struct osd_request *or, osd_req_done_fn *async_done,
		   void *caller_context, u8 *cred)
{
	int ret;

	ret = osd_finalize_request(or, 0, cred, NULL);
	if (ret)
		return ret;

	ret = osd_execute_request_async(or, async_done, caller_context);

	return ret;
}

/* copied from exofs; move to libosd? */
static int extract_attr_from_req(struct osd_request *or, struct osd_attr *attr)
{
	struct osd_attr cur_attr = {.attr_page = 0}; /* start with zeros */
	void *iter = NULL;
	int nelem;

	do {
		nelem = 1;
		osd_req_decode_get_attr_list(or, &cur_attr, &nelem, &iter);
		if ((cur_attr.attr_page == attr->attr_page) &&
		    (cur_attr.attr_id == attr->attr_id)) {
			attr->len = cur_attr.len;
			attr->val_ptr = cur_attr.val_ptr;
			return 0;
		}
	} while (iter);

	return -EIO;
}

static int osdblk_get_obj_size(struct osdblk_device *osdev, u64 *size_out)
{
	struct osd_request *or;
	struct osd_attr attr;
	int ret;

	/* start request */
	or = osd_start_request(osdev->osd, GFP_KERNEL);
	if (!or)
		return -ENOMEM;

	/* create a get-attributes(length) request */
	osd_req_get_attributes(or, &osdev->obj);

	osd_req_add_get_attr_list(or, &g_attr_logical_length, 1);

	/* execute op synchronously */
	ret = osd_sync_op(or, OSDBLK_OP_TIMEOUT, osdev->obj_cred);
	if (ret)
		goto out;

	/* extract length from returned attribute info */
	attr = g_attr_logical_length;
	ret = extract_attr_from_req(or, &attr);
	if (ret)
		goto out;

	*size_out = get_unaligned_be64(attr.val_ptr);

out:
	osd_end_request(or);
	return ret;

}

static void osdblk_osd_complete(struct osd_request *or, void *private)
{
	struct osdblk_request *orq = private;
	struct osd_sense_info osi;
	int ret = osd_req_decode_sense(or, &osi);

	if (ret) {
		ret = -EIO;
		OSDBLK_DEBUG("osdblk_osd_complete with err=%d\n", ret);
	}

	/* complete OSD request */
	osd_end_request(or);

	/* complete request passed to osdblk by block layer */
	__blk_end_request_all(orq->rq, ret);
}

static void bio_chain_put(struct bio *chain)
{
	struct bio *tmp;

	while (chain) {
		tmp = chain;
		chain = chain->bi_next;

		bio_put(tmp);
	}
}

static struct bio *bio_chain_clone(struct bio *old_chain, gfp_t gfpmask)
{
	struct bio *tmp, *new_chain = NULL, *tail = NULL;

	while (old_chain) {
		tmp = bio_kmalloc(gfpmask, old_chain->bi_max_vecs);
		if (!tmp)
			goto err_out;

		__bio_clone(tmp, old_chain);
		tmp->bi_bdev = NULL;
		gfpmask &= ~__GFP_WAIT;
		tmp->bi_next = NULL;

		if (!new_chain)
			new_chain = tail = tmp;
		else {
			tail->bi_next = tmp;
			tail = tmp;
		}

		old_chain = old_chain->bi_next;
	}

	return new_chain;

err_out:
	OSDBLK_DEBUG("bio_chain_clone with err\n");
	bio_chain_put(new_chain);
	return NULL;
}

static void osdblk_rq_fn(struct request_queue *q)
{
	struct osdblk_device *osdev = q->queuedata;

	while (1) {
		struct request *rq;
		struct osdblk_request *orq;
		struct osd_request *or;
		struct bio *bio;
		bool do_write, do_flush;

		/* peek at request from block layer */
		rq = blk_fetch_request(q);
		if (!rq)
			break;

		/* filter out block requests we don't understand */
		if (!blk_fs_request(rq) && !blk_barrier_rq(rq)) {
			blk_end_request_all(rq, 0);
			continue;
		}

		/* deduce our operation (read, write, flush) */
		/* I wish the block layer simplified cmd_type/cmd_flags/cmd[]
		 * into a clearly defined set of RPC commands:
		 * read, write, flush, scsi command, power mgmt req,
		 * driver-specific, etc.
		 */

		do_flush = (rq->special == (void *) 0xdeadbeefUL);
		do_write = (rq_data_dir(rq) == WRITE);

		if (!do_flush) { /* osd_flush does not use a bio */
			/* a bio clone to be passed down to OSD request */
			bio = bio_chain_clone(rq->bio, GFP_ATOMIC);
			if (!bio)
				break;
		} else
			bio = NULL;

		/* alloc internal OSD request, for OSD command execution */
		or = osd_start_request(osdev->osd, GFP_ATOMIC);
		if (!or) {
			bio_chain_put(bio);
			OSDBLK_DEBUG("osd_start_request with err\n");
			break;
		}

		orq = &osdev->req[rq->tag];
		orq->rq = rq;
		orq->bio = bio;
		orq->osdev = osdev;

		/* init OSD command: flush, write or read */
		if (do_flush)
			osd_req_flush_object(or, &osdev->obj,
					     OSD_CDB_FLUSH_ALL, 0, 0);
		else if (do_write)
			osd_req_write(or, &osdev->obj, blk_rq_pos(rq) * 512ULL,
				      bio, blk_rq_bytes(rq));
		else
			osd_req_read(or, &osdev->obj, blk_rq_pos(rq) * 512ULL,
				     bio, blk_rq_bytes(rq));

		OSDBLK_DEBUG("%s 0x%x bytes at 0x%llx\n",
			do_flush ? "flush" : do_write ?
				"write" : "read", blk_rq_bytes(rq),
			blk_rq_pos(rq) * 512ULL);

		/* begin OSD command execution */
		if (osd_async_op(or, osdblk_osd_complete, orq,
				 osdev->obj_cred)) {
			osd_end_request(or);
			blk_requeue_request(q, rq);
			bio_chain_put(bio);
			OSDBLK_DEBUG("osd_execute_request_async with err\n");
			break;
		}

		/* remove the special 'flush' marker, now that the command
		 * is executing
		 */
		rq->special = NULL;
	}
}

static void osdblk_prepare_flush(struct request_queue *q, struct request *rq)
{
	/* add driver-specific marker, to indicate that this request
	 * is a flush command
	 */
	rq->special = (void *) 0xdeadbeefUL;
}

static void osdblk_free_disk(struct osdblk_device *osdev)
{
	struct gendisk *disk = osdev->disk;

	if (!disk)
		return;

	if (disk->flags & GENHD_FL_UP)
		del_gendisk(disk);
	if (disk->queue)
		blk_cleanup_queue(disk->queue);
	put_disk(disk);
}

static int osdblk_init_disk(struct osdblk_device *osdev)
{
	struct gendisk *disk;
	struct request_queue *q;
	int rc;
	u64 obj_size = 0;

	/* contact OSD, request size info about the object being mapped */
	rc = osdblk_get_obj_size(osdev, &obj_size);
	if (rc)
		return rc;

	/* create gendisk info */
	disk = alloc_disk(OSDBLK_MINORS_PER_MAJOR);
	if (!disk)
		return -ENOMEM;

	sprintf(disk->disk_name, DRV_NAME "%d", osdev->id);
	disk->major = osdev->major;
	disk->first_minor = 0;
	disk->fops = &osdblk_bd_ops;
	disk->private_data = osdev;

	/* init rq */
	q = blk_init_queue(osdblk_rq_fn, &osdev->lock);
	if (!q) {
		put_disk(disk);
		return -ENOMEM;
	}

	/* switch queue to TCQ mode; allocate tag map */
	rc = blk_queue_init_tags(q, OSDBLK_MAX_REQ, NULL);
	if (rc) {
		blk_cleanup_queue(q);
		put_disk(disk);
		return rc;
	}

	/* Set our limits to the lower device limits, because osdblk cannot
	 * sleep when allocating a lower-request and therefore cannot be
	 * bouncing.
	 */
	blk_queue_stack_limits(q, osd_request_queue(osdev->osd));

	blk_queue_prep_rq(q, blk_queue_start_tag);
	blk_queue_ordered(q, QUEUE_ORDERED_DRAIN_FLUSH, osdblk_prepare_flush);

	disk->queue = q;

	q->queuedata = osdev;

	osdev->disk = disk;
	osdev->q = q;

	/* finally, announce the disk to the world */
	set_capacity(disk, obj_size / 512ULL);
	add_disk(disk);

	printk(KERN_INFO "%s: Added of size 0x%llx\n",
		disk->disk_name, (unsigned long long)obj_size);

	return 0;
}

/********************************************************************
 * /sys/class/osdblk/
 *                   add	map OSD object to blkdev
 *                   remove	unmap OSD object
 *                   list	show mappings
 *******************************************************************/

static void class_osdblk_release(struct class *cls)
{
	kfree(cls);
}

static ssize_t class_osdblk_list(struct class *c,
				struct class_attribute *attr,
				char *data)
{
	int n = 0;
	struct list_head *tmp;

	mutex_lock_nested(&ctl_mutex, SINGLE_DEPTH_NESTING);

	list_for_each(tmp, &osdblkdev_list) {
		struct osdblk_device *osdev;

		osdev = list_entry(tmp, struct osdblk_device, node);

		n += sprintf(data+n, "%d %d %llu %llu %s\n",
			osdev->id,
			osdev->major,
			osdev->obj.partition,
			osdev->obj.id,
			osdev->osd_path);
	}

	mutex_unlock(&ctl_mutex);
	return n;
}

static ssize_t class_osdblk_add(struct class *c,
				struct class_attribute *attr,
				const char *buf, size_t count)
{
	struct osdblk_device *osdev;
	ssize_t rc;
	int irc, new_id = 0;
	struct list_head *tmp;

	if (!try_module_get(THIS_MODULE))
		return -ENODEV;

	/* new osdblk_device object */
	osdev = kzalloc(sizeof(*osdev) + strlen(buf) + 1, GFP_KERNEL);
	if (!osdev) {
		rc = -ENOMEM;
		goto err_out_mod;
	}

	/* static osdblk_device initialization */
	spin_lock_init(&osdev->lock);
	INIT_LIST_HEAD(&osdev->node);

	/* generate unique id: find highest unique id, add one */

	mutex_lock_nested(&ctl_mutex, SINGLE_DEPTH_NESTING);

	list_for_each(tmp, &osdblkdev_list) {
		struct osdblk_device *osdev;

		osdev = list_entry(tmp, struct osdblk_device, node);
		if (osdev->id > new_id)
			new_id = osdev->id + 1;
	}

	osdev->id = new_id;

	/* add to global list */
	list_add_tail(&osdev->node, &osdblkdev_list);

	mutex_unlock(&ctl_mutex);

	/* parse add command */
	if (sscanf(buf, "%llu %llu %s", &osdev->obj.partition, &osdev->obj.id,
		   osdev->osd_path) != 3) {
		rc = -EINVAL;
		goto err_out_slot;
	}

	/* initialize rest of new object */
	sprintf(osdev->name, DRV_NAME "%d", osdev->id);

	/* contact requested OSD */
	osdev->osd = osduld_path_lookup(osdev->osd_path);
	if (IS_ERR(osdev->osd)) {
		rc = PTR_ERR(osdev->osd);
		goto err_out_slot;
	}

	/* build OSD credential */
	osdblk_make_credential(osdev->obj_cred, &osdev->obj);

	/* register our block device */
	irc = register_blkdev(0, osdev->name);
	if (irc < 0) {
		rc = irc;
		goto err_out_osd;
	}

	osdev->major = irc;

	/* set up and announce blkdev mapping */
	rc = osdblk_init_disk(osdev);
	if (rc)
		goto err_out_blkdev;

	return count;

err_out_blkdev:
	unregister_blkdev(osdev->major, osdev->name);
err_out_osd:
	osduld_put_device(osdev->osd);
err_out_slot:
	mutex_lock_nested(&ctl_mutex, SINGLE_DEPTH_NESTING);
	list_del_init(&osdev->node);
	mutex_unlock(&ctl_mutex);

	kfree(osdev);
err_out_mod:
	OSDBLK_DEBUG("Error adding device %s\n", buf);
	module_put(THIS_MODULE);
	return rc;
}

static ssize_t class_osdblk_remove(struct class *c,
					struct class_attribute *attr,
					const char *buf,
					size_t count)
{
	struct osdblk_device *osdev = NULL;
	int target_id, rc;
	unsigned long ul;
	struct list_head *tmp;

	rc = strict_strtoul(buf, 10, &ul);
	if (rc)
		return rc;

	/* convert to int; abort if we lost anything in the conversion */
	target_id = (int) ul;
	if (target_id != ul)
		return -EINVAL;

	/* remove object from list immediately */
	mutex_lock_nested(&ctl_mutex, SINGLE_DEPTH_NESTING);

	list_for_each(tmp, &osdblkdev_list) {
		osdev = list_entry(tmp, struct osdblk_device, node);
		if (osdev->id == target_id) {
			list_del_init(&osdev->node);
			break;
		}
		osdev = NULL;
	}

	mutex_unlock(&ctl_mutex);

	if (!osdev)
		return -ENOENT;

	/* clean up and free blkdev and associated OSD connection */
	osdblk_free_disk(osdev);
	unregister_blkdev(osdev->major, osdev->name);
	osduld_put_device(osdev->osd);
	kfree(osdev);

	/* release module ref */
	module_put(THIS_MODULE);

	return count;
}

static struct class_attribute class_osdblk_attrs[] = {
	__ATTR(add,	0200, NULL, class_osdblk_add),
	__ATTR(remove,	0200, NULL, class_osdblk_remove),
	__ATTR(list,	0444, class_osdblk_list, NULL),
	__ATTR_NULL
};

static int osdblk_sysfs_init(void)
{
	int ret = 0;

	/*
	 * create control files in sysfs
	 * /sys/class/osdblk/...
	 */
	class_osdblk = kzalloc(sizeof(*class_osdblk), GFP_KERNEL);
	if (!class_osdblk)
		return -ENOMEM;

	class_osdblk->name = DRV_NAME;
	class_osdblk->owner = THIS_MODULE;
	class_osdblk->class_release = class_osdblk_release;
	class_osdblk->class_attrs = class_osdblk_attrs;

	ret = class_register(class_osdblk);
	if (ret) {
		kfree(class_osdblk);
		class_osdblk = NULL;
		printk(PFX "failed to create class osdblk\n");
		return ret;
	}

	return 0;
}

static void osdblk_sysfs_cleanup(void)
{
	if (class_osdblk)
		class_destroy(class_osdblk);
	class_osdblk = NULL;
}

static int __init osdblk_init(void)
{
	int rc;

	rc = osdblk_sysfs_init();
	if (rc)
		return rc;

	return 0;
}

static void __exit osdblk_exit(void)
{
	osdblk_sysfs_cleanup();
}

module_init(osdblk_init);
module_exit(osdblk_exit);

