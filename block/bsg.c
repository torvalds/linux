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

struct bsg_device {
	struct request_queue *queue;
	const struct bsg_ops *ops;
	struct device device;
	struct cdev cdev;
	int max_queue;
	unsigned int timeout;
	unsigned int reserved_size;
};

static inline struct bsg_device *to_bsg_device(struct inode *inode)
{
	return container_of(inode->i_cdev, struct bsg_device, cdev);
}

#define BSG_DEFAULT_CMDS	64
#define BSG_MAX_DEVS		32768

static DEFINE_IDA(bsg_minor_ida);
static struct class *bsg_class;
static int bsg_major;

#define uptr64(val) ((void __user *)(uintptr_t)(val))

static int bsg_sg_io(struct bsg_device *bd, fmode_t mode, void __user *uarg)
{
	struct request *rq;
	struct bio *bio;
	struct sg_io_v4 hdr;
	int ret;

	if (copy_from_user(&hdr, uarg, sizeof(hdr)))
		return -EFAULT;

	if (hdr.guard != 'Q')
		return -EINVAL;
	ret = bd->ops->check_proto(&hdr);
	if (ret)
		return ret;

	rq = blk_get_request(bd->queue, hdr.dout_xfer_len ?
			REQ_OP_DRV_OUT : REQ_OP_DRV_IN, 0);
	if (IS_ERR(rq))
		return PTR_ERR(rq);

	ret = bd->ops->fill_hdr(rq, &hdr, mode);
	if (ret) {
		blk_put_request(rq);
		return ret;
	}

	rq->timeout = msecs_to_jiffies(hdr.timeout);
	if (!rq->timeout)
		rq->timeout = bd->timeout;
	if (!rq->timeout)
		rq->timeout = BLK_DEFAULT_SG_TIMEOUT;
	if (rq->timeout < BLK_MIN_SG_TIMEOUT)
		rq->timeout = BLK_MIN_SG_TIMEOUT;

	if (hdr.dout_xfer_len) {
		ret = blk_rq_map_user(rq->q, rq, NULL, uptr64(hdr.dout_xferp),
				hdr.dout_xfer_len, GFP_KERNEL);
	} else if (hdr.din_xfer_len) {
		ret = blk_rq_map_user(rq->q, rq, NULL, uptr64(hdr.din_xferp),
				hdr.din_xfer_len, GFP_KERNEL);
	}

	if (ret)
		goto out_free_rq;

	bio = rq->bio;

	blk_execute_rq(NULL, rq, !(hdr.flags & BSG_FLAG_Q_AT_TAIL));
	ret = bd->ops->complete_rq(rq, &hdr);
	blk_rq_unmap_user(bio);

out_free_rq:
	bd->ops->free_rq(rq);
	blk_put_request(rq);
	if (!ret && copy_to_user(uarg, &hdr, sizeof(hdr)))
		return -EFAULT;
	return ret;
}

static int bsg_open(struct inode *inode, struct file *file)
{
	if (!blk_get_queue(to_bsg_device(inode)->queue))
		return -ENXIO;
	return 0;
}

static int bsg_release(struct inode *inode, struct file *file)
{
	blk_put_queue(to_bsg_device(inode)->queue);
	return 0;
}

static int bsg_get_command_q(struct bsg_device *bd, int __user *uarg)
{
	return put_user(READ_ONCE(bd->max_queue), uarg);
}

static int bsg_set_command_q(struct bsg_device *bd, int __user *uarg)
{
	int max_queue;

	if (get_user(max_queue, uarg))
		return -EFAULT;
	if (max_queue < 1)
		return -EINVAL;
	WRITE_ONCE(bd->max_queue, max_queue);
	return 0;
}

static long bsg_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct bsg_device *bd = to_bsg_device(file_inode(file));
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
		bd->timeout = clock_t_to_jiffies(val);
		return 0;
	case SG_GET_TIMEOUT:
		return jiffies_to_clock_t(bd->timeout);
	case SG_GET_RESERVED_SIZE:
		return put_user(min(bd->reserved_size, queue_max_bytes(q)),
				intp);
	case SG_SET_RESERVED_SIZE:
		if (get_user(val, intp))
			return -EFAULT;
		if (val < 0)
			return -EINVAL;
		bd->reserved_size =
			min_t(unsigned int, val, queue_max_bytes(q));
		return 0;
	case SG_EMULATED_HOST:
		return put_user(1, intp);
	case SG_IO:
		return bsg_sg_io(bd, file->f_mode, uarg);
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

void bsg_unregister_queue(struct bsg_device *bd)
{
	if (bd->queue->kobj.sd)
		sysfs_remove_link(&bd->queue->kobj, "bsg");
	cdev_device_del(&bd->cdev, &bd->device);
	ida_simple_remove(&bsg_minor_ida, MINOR(bd->device.devt));
	kfree(bd);
}
EXPORT_SYMBOL_GPL(bsg_unregister_queue);

struct bsg_device *bsg_register_queue(struct request_queue *q,
		struct device *parent, const char *name,
		const struct bsg_ops *ops)
{
	struct bsg_device *bd;
	int ret;

	bd = kzalloc(sizeof(*bd), GFP_KERNEL);
	if (!bd)
		return ERR_PTR(-ENOMEM);
	bd->max_queue = BSG_DEFAULT_CMDS;
	bd->reserved_size = INT_MAX;
	bd->queue = q;
	bd->ops = ops;

	ret = ida_simple_get(&bsg_minor_ida, 0, BSG_MAX_DEVS, GFP_KERNEL);
	if (ret < 0) {
		if (ret == -ENOSPC)
			dev_err(parent, "bsg: too many bsg devices\n");
		goto out_kfree;
	}
	bd->device.devt = MKDEV(bsg_major, ret);
	bd->device.class = bsg_class;
	bd->device.parent = parent;
	dev_set_name(&bd->device, "%s", name);
	device_initialize(&bd->device);

	cdev_init(&bd->cdev, &bsg_fops);
	bd->cdev.owner = THIS_MODULE;
	ret = cdev_device_add(&bd->cdev, &bd->device);
	if (ret)
		goto out_ida_remove;

	if (q->kobj.sd) {
		ret = sysfs_create_link(&q->kobj, &bd->device.kobj, "bsg");
		if (ret)
			goto out_device_del;
	}

	return bd;

out_device_del:
	cdev_device_del(&bd->cdev, &bd->device);
out_ida_remove:
	ida_simple_remove(&bsg_minor_ida, MINOR(bd->device.devt));
out_kfree:
	kfree(bd);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(bsg_register_queue);

static char *bsg_devnode(struct device *dev, umode_t *mode)
{
	return kasprintf(GFP_KERNEL, "bsg/%s", dev_name(dev));
}

static int __init bsg_init(void)
{
	dev_t devid;
	int ret;

	bsg_class = class_create(THIS_MODULE, "bsg");
	if (IS_ERR(bsg_class))
		return PTR_ERR(bsg_class);
	bsg_class->devnode = bsg_devnode;

	ret = alloc_chrdev_region(&devid, 0, BSG_MAX_DEVS, "bsg");
	if (ret)
		goto destroy_bsg_class;
	bsg_major = MAJOR(devid);

	printk(KERN_INFO BSG_DESCRIPTION " version " BSG_VERSION
	       " loaded (major %d)\n", bsg_major);
	return 0;

destroy_bsg_class:
	class_destroy(bsg_class);
	return ret;
}

MODULE_AUTHOR("Jens Axboe");
MODULE_DESCRIPTION(BSG_DESCRIPTION);
MODULE_LICENSE("GPL");

device_initcall(bsg_init);
