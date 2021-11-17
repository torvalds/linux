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
	struct device device;
	struct cdev cdev;
	int max_queue;
	unsigned int timeout;
	unsigned int reserved_size;
	bsg_sg_io_fn *sg_io_fn;
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

static unsigned int bsg_timeout(struct bsg_device *bd, struct sg_io_v4 *hdr)
{
	unsigned int timeout = BLK_DEFAULT_SG_TIMEOUT;

	if (hdr->timeout)
		timeout = msecs_to_jiffies(hdr->timeout);
	else if (bd->timeout)
		timeout = bd->timeout;

	return max_t(unsigned int, timeout, BLK_MIN_SG_TIMEOUT);
}

static int bsg_sg_io(struct bsg_device *bd, fmode_t mode, void __user *uarg)
{
	struct sg_io_v4 hdr;
	int ret;

	if (copy_from_user(&hdr, uarg, sizeof(hdr)))
		return -EFAULT;
	if (hdr.guard != 'Q')
		return -EINVAL;
	ret = bd->sg_io_fn(bd->queue, &hdr, mode, bsg_timeout(bd, &hdr));
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

static void bsg_device_release(struct device *dev)
{
	struct bsg_device *bd = container_of(dev, struct bsg_device, device);

	ida_simple_remove(&bsg_minor_ida, MINOR(bd->device.devt));
	kfree(bd);
}

void bsg_unregister_queue(struct bsg_device *bd)
{
	if (bd->queue->kobj.sd)
		sysfs_remove_link(&bd->queue->kobj, "bsg");
	cdev_device_del(&bd->cdev, &bd->device);
	put_device(&bd->device);
}
EXPORT_SYMBOL_GPL(bsg_unregister_queue);

struct bsg_device *bsg_register_queue(struct request_queue *q,
		struct device *parent, const char *name, bsg_sg_io_fn *sg_io_fn)
{
	struct bsg_device *bd;
	int ret;

	bd = kzalloc(sizeof(*bd), GFP_KERNEL);
	if (!bd)
		return ERR_PTR(-ENOMEM);
	bd->max_queue = BSG_DEFAULT_CMDS;
	bd->reserved_size = INT_MAX;
	bd->queue = q;
	bd->sg_io_fn = sg_io_fn;

	ret = ida_simple_get(&bsg_minor_ida, 0, BSG_MAX_DEVS, GFP_KERNEL);
	if (ret < 0) {
		if (ret == -ENOSPC)
			dev_err(parent, "bsg: too many bsg devices\n");
		kfree(bd);
		return ERR_PTR(ret);
	}
	bd->device.devt = MKDEV(bsg_major, ret);
	bd->device.class = bsg_class;
	bd->device.parent = parent;
	bd->device.release = bsg_device_release;
	dev_set_name(&bd->device, "%s", name);
	device_initialize(&bd->device);

	cdev_init(&bd->cdev, &bsg_fops);
	bd->cdev.owner = THIS_MODULE;
	ret = cdev_device_add(&bd->cdev, &bd->device);
	if (ret)
		goto out_put_device;

	if (q->kobj.sd) {
		ret = sysfs_create_link(&q->kobj, &bd->device.kobj, "bsg");
		if (ret)
			goto out_device_del;
	}

	return bd;

out_device_del:
	cdev_device_del(&bd->cdev, &bd->device);
out_put_device:
	put_device(&bd->device);
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
