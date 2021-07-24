// SPDX-License-Identifier: GPL-2.0
/*
 * bsg.c - block layer implementation of the sg v4 interface
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/file.h>
#include <linux/blkdev.h>
#include <linux/cdev.h>
#include <linux/jiffies.h>
#include <linux/percpu.h>
#include <linux/idr.h>
#include <linux/bsg.h>
#include <linux/slab.h>

#include <scsi/scsi.h>
#include <scsi/scsi_ioctl.h>
#include <scsi/sg.h>

#define BSG_DESCRIPTION	"Block layer SCSI generic (bsg) driver"
#define BSG_VERSION	"0.4"

#define bsg_dbg(bd, fmt, ...) \
	pr_debug("%s: " fmt, (bd)->name, ##__VA_ARGS__)

struct bsg_device {
	struct request_queue *queue;
	spinlock_t lock;
	struct hlist_node dev_list;
	refcount_t ref_count;
	char name[20];
	int max_queue;
};

#define BSG_DEFAULT_CMDS	64
#define BSG_MAX_DEVS		32768

static DEFINE_MUTEX(bsg_mutex);
static DEFINE_IDR(bsg_minor_idr);

#define BSG_LIST_ARRAY_SIZE	8
static struct hlist_head bsg_device_list[BSG_LIST_ARRAY_SIZE];

static struct class *bsg_class;
static int bsg_major;

static inline struct hlist_head *bsg_dev_idx_hash(int index)
{
	return &bsg_device_list[index & (BSG_LIST_ARRAY_SIZE - 1)];
}

#define uptr64(val) ((void __user *)(uintptr_t)(val))

static int bsg_sg_io(struct request_queue *q, fmode_t mode, void __user *uarg)
{
	struct request *rq;
	struct bio *bio;
	struct sg_io_v4 hdr;
	int ret;

	if (copy_from_user(&hdr, uarg, sizeof(hdr)))
		return -EFAULT;

	if (!q->bsg_dev.class_dev)
		return -ENXIO;

	if (hdr.guard != 'Q')
		return -EINVAL;
	ret = q->bsg_dev.ops->check_proto(&hdr);
	if (ret)
		return ret;

	rq = blk_get_request(q, hdr.dout_xfer_len ?
			REQ_OP_DRV_OUT : REQ_OP_DRV_IN, 0);
	if (IS_ERR(rq))
		return PTR_ERR(rq);

	ret = q->bsg_dev.ops->fill_hdr(rq, &hdr, mode);
	if (ret) {
		blk_put_request(rq);
		return ret;
	}

	rq->timeout = msecs_to_jiffies(hdr.timeout);
	if (!rq->timeout)
		rq->timeout = q->sg_timeout;
	if (!rq->timeout)
		rq->timeout = BLK_DEFAULT_SG_TIMEOUT;
	if (rq->timeout < BLK_MIN_SG_TIMEOUT)
		rq->timeout = BLK_MIN_SG_TIMEOUT;

	if (hdr.dout_xfer_len) {
		ret = blk_rq_map_user(q, rq, NULL, uptr64(hdr.dout_xferp),
				hdr.dout_xfer_len, GFP_KERNEL);
	} else if (hdr.din_xfer_len) {
		ret = blk_rq_map_user(q, rq, NULL, uptr64(hdr.din_xferp),
				hdr.din_xfer_len, GFP_KERNEL);
	}

	if (ret)
		goto out_free_rq;

	bio = rq->bio;

	blk_execute_rq(NULL, rq, !(hdr.flags & BSG_FLAG_Q_AT_TAIL));
	ret = rq->q->bsg_dev.ops->complete_rq(rq, &hdr);
	blk_rq_unmap_user(bio);

out_free_rq:
	rq->q->bsg_dev.ops->free_rq(rq);
	blk_put_request(rq);
	if (!ret && copy_to_user(uarg, &hdr, sizeof(hdr)))
		return -EFAULT;
	return ret;
}

static struct bsg_device *bsg_alloc_device(void)
{
	struct bsg_device *bd;

	bd = kzalloc(sizeof(struct bsg_device), GFP_KERNEL);
	if (unlikely(!bd))
		return NULL;

	spin_lock_init(&bd->lock);
	bd->max_queue = BSG_DEFAULT_CMDS;
	INIT_HLIST_NODE(&bd->dev_list);
	return bd;
}

static int bsg_put_device(struct bsg_device *bd)
{
	struct request_queue *q = bd->queue;

	mutex_lock(&bsg_mutex);

	if (!refcount_dec_and_test(&bd->ref_count)) {
		mutex_unlock(&bsg_mutex);
		return 0;
	}

	hlist_del(&bd->dev_list);
	mutex_unlock(&bsg_mutex);

	bsg_dbg(bd, "tearing down\n");

	/*
	 * close can always block
	 */
	kfree(bd);
	blk_put_queue(q);
	return 0;
}

static struct bsg_device *bsg_add_device(struct inode *inode,
					 struct request_queue *rq,
					 struct file *file)
{
	struct bsg_device *bd;
	unsigned char buf[32];

	lockdep_assert_held(&bsg_mutex);

	if (!blk_get_queue(rq))
		return ERR_PTR(-ENXIO);

	bd = bsg_alloc_device();
	if (!bd) {
		blk_put_queue(rq);
		return ERR_PTR(-ENOMEM);
	}

	bd->queue = rq;

	refcount_set(&bd->ref_count, 1);
	hlist_add_head(&bd->dev_list, bsg_dev_idx_hash(iminor(inode)));

	strncpy(bd->name, dev_name(rq->bsg_dev.class_dev), sizeof(bd->name) - 1);
	bsg_dbg(bd, "bound to <%s>, max queue %d\n",
		format_dev_t(buf, inode->i_rdev), bd->max_queue);

	return bd;
}

static struct bsg_device *__bsg_get_device(int minor, struct request_queue *q)
{
	struct bsg_device *bd;

	lockdep_assert_held(&bsg_mutex);

	hlist_for_each_entry(bd, bsg_dev_idx_hash(minor), dev_list) {
		if (bd->queue == q) {
			refcount_inc(&bd->ref_count);
			goto found;
		}
	}
	bd = NULL;
found:
	return bd;
}

static struct bsg_device *bsg_get_device(struct inode *inode, struct file *file)
{
	struct bsg_device *bd;
	struct bsg_class_device *bcd;

	/*
	 * find the class device
	 */
	mutex_lock(&bsg_mutex);
	bcd = idr_find(&bsg_minor_idr, iminor(inode));

	if (!bcd) {
		bd = ERR_PTR(-ENODEV);
		goto out_unlock;
	}

	bd = __bsg_get_device(iminor(inode), bcd->queue);
	if (!bd)
		bd = bsg_add_device(inode, bcd->queue, file);

out_unlock:
	mutex_unlock(&bsg_mutex);
	return bd;
}

static int bsg_open(struct inode *inode, struct file *file)
{
	struct bsg_device *bd;

	bd = bsg_get_device(inode, file);

	if (IS_ERR(bd))
		return PTR_ERR(bd);

	file->private_data = bd;
	return 0;
}

static int bsg_release(struct inode *inode, struct file *file)
{
	struct bsg_device *bd = file->private_data;

	file->private_data = NULL;
	return bsg_put_device(bd);
}

static int bsg_get_command_q(struct bsg_device *bd, int __user *uarg)
{
	return put_user(bd->max_queue, uarg);
}

static int bsg_set_command_q(struct bsg_device *bd, int __user *uarg)
{
	int queue;

	if (get_user(queue, uarg))
		return -EFAULT;
	if (queue < 1)
		return -EINVAL;

	spin_lock_irq(&bd->lock);
	bd->max_queue = queue;
	spin_unlock_irq(&bd->lock);
	return 0;
}

static long bsg_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct bsg_device *bd = file->private_data;
	struct request_queue *q = bd->queue;
	void __user *uarg = (void __user *) arg;
	int __user *intp = uarg;
	int val;

	switch (cmd) {
	/*
	 * Our own ioctls
	 */
	case SG_GET_COMMAND_Q:
		return bsg_get_command_q(bd, uarg);
	case SG_SET_COMMAND_Q:
		return bsg_set_command_q(bd, uarg);

	/*
	 * SCSI/sg ioctls
	 */
	case SG_GET_VERSION_NUM:
		return put_user(30527, intp);
	case SCSI_IOCTL_GET_IDLUN:
		return put_user(0, intp);
	case SCSI_IOCTL_GET_BUS_NUMBER:
		return put_user(0, intp);
	case SG_SET_TIMEOUT:
		if (get_user(val, intp))
			return -EFAULT;
		q->sg_timeout = clock_t_to_jiffies(val);
		return 0;
	case SG_GET_TIMEOUT:
		return jiffies_to_clock_t(q->sg_timeout);
	case SG_GET_RESERVED_SIZE:
		return put_user(min(q->sg_reserved_size, queue_max_bytes(q)),
				intp);
	case SG_SET_RESERVED_SIZE:
		if (get_user(val, intp))
			return -EFAULT;
		if (val < 0)
			return -EINVAL;
		q->sg_reserved_size =
			min_t(unsigned int, val, queue_max_bytes(q));
		return 0;
	case SG_EMULATED_HOST:
		return put_user(1, intp);
	case SG_IO:
		return bsg_sg_io(q, file->f_mode, uarg);
	case SCSI_IOCTL_SEND_COMMAND:
		pr_warn_ratelimited("%s: calling unsupported SCSI_IOCTL_SEND_COMMAND\n",
				current->comm);
		return -EINVAL;
	default:
		return -ENOTTY;
	}
}

static const struct file_operations bsg_fops = {
	.open		=	bsg_open,
	.release	=	bsg_release,
	.unlocked_ioctl	=	bsg_ioctl,
	.compat_ioctl	=	compat_ptr_ioctl,
	.owner		=	THIS_MODULE,
	.llseek		=	default_llseek,
};

void bsg_unregister_queue(struct request_queue *q)
{
	struct bsg_class_device *bcd = &q->bsg_dev;

	if (!bcd->class_dev)
		return;

	mutex_lock(&bsg_mutex);
	idr_remove(&bsg_minor_idr, bcd->minor);
	if (q->kobj.sd)
		sysfs_remove_link(&q->kobj, "bsg");
	device_unregister(bcd->class_dev);
	bcd->class_dev = NULL;
	mutex_unlock(&bsg_mutex);
}
EXPORT_SYMBOL_GPL(bsg_unregister_queue);

int bsg_register_queue(struct request_queue *q, struct device *parent,
		const char *name, const struct bsg_ops *ops)
{
	struct bsg_class_device *bcd;
	dev_t dev;
	int ret;
	struct device *class_dev = NULL;

	/*
	 * we need a proper transport to send commands, not a stacked device
	 */
	if (!queue_is_mq(q))
		return 0;

	bcd = &q->bsg_dev;
	memset(bcd, 0, sizeof(*bcd));

	mutex_lock(&bsg_mutex);

	ret = idr_alloc(&bsg_minor_idr, bcd, 0, BSG_MAX_DEVS, GFP_KERNEL);
	if (ret < 0) {
		if (ret == -ENOSPC) {
			printk(KERN_ERR "bsg: too many bsg devices\n");
			ret = -EINVAL;
		}
		goto unlock;
	}

	bcd->minor = ret;
	bcd->queue = q;
	bcd->ops = ops;
	dev = MKDEV(bsg_major, bcd->minor);
	class_dev = device_create(bsg_class, parent, dev, NULL, "%s", name);
	if (IS_ERR(class_dev)) {
		ret = PTR_ERR(class_dev);
		goto idr_remove;
	}
	bcd->class_dev = class_dev;

	if (q->kobj.sd) {
		ret = sysfs_create_link(&q->kobj, &bcd->class_dev->kobj, "bsg");
		if (ret)
			goto unregister_class_dev;
	}

	mutex_unlock(&bsg_mutex);
	return 0;

unregister_class_dev:
	device_unregister(class_dev);
idr_remove:
	idr_remove(&bsg_minor_idr, bcd->minor);
unlock:
	mutex_unlock(&bsg_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(bsg_register_queue);

static struct cdev bsg_cdev;

static char *bsg_devnode(struct device *dev, umode_t *mode)
{
	return kasprintf(GFP_KERNEL, "bsg/%s", dev_name(dev));
}

static int __init bsg_init(void)
{
	int ret, i;
	dev_t devid;

	for (i = 0; i < BSG_LIST_ARRAY_SIZE; i++)
		INIT_HLIST_HEAD(&bsg_device_list[i]);

	bsg_class = class_create(THIS_MODULE, "bsg");
	if (IS_ERR(bsg_class))
		return PTR_ERR(bsg_class);
	bsg_class->devnode = bsg_devnode;

	ret = alloc_chrdev_region(&devid, 0, BSG_MAX_DEVS, "bsg");
	if (ret)
		goto destroy_bsg_class;

	bsg_major = MAJOR(devid);

	cdev_init(&bsg_cdev, &bsg_fops);
	ret = cdev_add(&bsg_cdev, MKDEV(bsg_major, 0), BSG_MAX_DEVS);
	if (ret)
		goto unregister_chrdev;

	printk(KERN_INFO BSG_DESCRIPTION " version " BSG_VERSION
	       " loaded (major %d)\n", bsg_major);
	return 0;
unregister_chrdev:
	unregister_chrdev_region(MKDEV(bsg_major, 0), BSG_MAX_DEVS);
destroy_bsg_class:
	class_destroy(bsg_class);
	return ret;
}

MODULE_AUTHOR("Jens Axboe");
MODULE_DESCRIPTION(BSG_DESCRIPTION);
MODULE_LICENSE("GPL");

device_initcall(bsg_init);
