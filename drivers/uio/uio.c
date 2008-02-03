/*
 * drivers/uio/uio.c
 *
 * Copyright(C) 2005, Benedikt Spranger <b.spranger@linutronix.de>
 * Copyright(C) 2005, Thomas Gleixner <tglx@linutronix.de>
 * Copyright(C) 2006, Hans J. Koch <hjk@linutronix.de>
 * Copyright(C) 2006, Greg Kroah-Hartman <greg@kroah.com>
 *
 * Userspace IO
 *
 * Base Functions
 *
 * Licensed under the GPLv2 only.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/device.h>
#include <linux/mm.h>
#include <linux/idr.h>
#include <linux/string.h>
#include <linux/kobject.h>
#include <linux/uio_driver.h>

#define UIO_MAX_DEVICES 255

struct uio_device {
	struct module		*owner;
	struct device		*dev;
	int			minor;
	atomic_t		event;
	struct fasync_struct	*async_queue;
	wait_queue_head_t	wait;
	int			vma_count;
	struct uio_info		*info;
	struct kobject		*map_dir;
};

static int uio_major;
static DEFINE_IDR(uio_idr);
static const struct file_operations uio_fops;

/* UIO class infrastructure */
static struct uio_class {
	struct kref kref;
	struct class *class;
} *uio_class;

/*
 * attributes
 */

struct uio_map {
	struct kobject kobj;
	struct uio_mem *mem;
};
#define to_map(map) container_of(map, struct uio_map, kobj)


static ssize_t map_attr_show(struct kobject *kobj, struct kobj_attribute *attr,
			     char *buf)
{
	struct uio_map *map = to_map(kobj);
	struct uio_mem *mem = map->mem;

	if (strncmp(attr->attr.name, "addr", 4) == 0)
		return sprintf(buf, "0x%lx\n", mem->addr);

	if (strncmp(attr->attr.name, "size", 4) == 0)
		return sprintf(buf, "0x%lx\n", mem->size);

	return -ENODEV;
}

static struct kobj_attribute attr_attribute =
	__ATTR(addr, S_IRUGO, map_attr_show, NULL);
static struct kobj_attribute size_attribute =
	__ATTR(size, S_IRUGO, map_attr_show, NULL);

static struct attribute *attrs[] = {
	&attr_attribute.attr,
	&size_attribute.attr,
	NULL,	/* need to NULL terminate the list of attributes */
};

static void map_release(struct kobject *kobj)
{
	struct uio_map *map = to_map(kobj);
	kfree(map);
}

static struct kobj_type map_attr_type = {
	.release	= map_release,
	.default_attrs	= attrs,
};

static ssize_t show_name(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct uio_device *idev = dev_get_drvdata(dev);
	if (idev)
		return sprintf(buf, "%s\n", idev->info->name);
	else
		return -ENODEV;
}
static DEVICE_ATTR(name, S_IRUGO, show_name, NULL);

static ssize_t show_version(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct uio_device *idev = dev_get_drvdata(dev);
	if (idev)
		return sprintf(buf, "%s\n", idev->info->version);
	else
		return -ENODEV;
}
static DEVICE_ATTR(version, S_IRUGO, show_version, NULL);

static ssize_t show_event(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct uio_device *idev = dev_get_drvdata(dev);
	if (idev)
		return sprintf(buf, "%u\n",
				(unsigned int)atomic_read(&idev->event));
	else
		return -ENODEV;
}
static DEVICE_ATTR(event, S_IRUGO, show_event, NULL);

static struct attribute *uio_attrs[] = {
	&dev_attr_name.attr,
	&dev_attr_version.attr,
	&dev_attr_event.attr,
	NULL,
};

static struct attribute_group uio_attr_grp = {
	.attrs = uio_attrs,
};

/*
 * device functions
 */
static int uio_dev_add_attributes(struct uio_device *idev)
{
	int ret;
	int mi;
	int map_found = 0;
	struct uio_mem *mem;
	struct uio_map *map;

	ret = sysfs_create_group(&idev->dev->kobj, &uio_attr_grp);
	if (ret)
		goto err_group;

	for (mi = 0; mi < MAX_UIO_MAPS; mi++) {
		mem = &idev->info->mem[mi];
		if (mem->size == 0)
			break;
		if (!map_found) {
			map_found = 1;
			idev->map_dir = kobject_create_and_add("maps",
							&idev->dev->kobj);
			if (!idev->map_dir)
				goto err;
		}
		map = kzalloc(sizeof(*map), GFP_KERNEL);
		if (!map)
			goto err;
		kobject_init(&map->kobj, &map_attr_type);
		map->mem = mem;
		mem->map = map;
		ret = kobject_add(&map->kobj, idev->map_dir, "map%d", mi);
		if (ret)
			goto err;
		ret = kobject_uevent(&map->kobj, KOBJ_ADD);
		if (ret)
			goto err;
	}

	return 0;

err:
	for (mi--; mi>=0; mi--) {
		mem = &idev->info->mem[mi];
		map = mem->map;
		kobject_put(&map->kobj);
	}
	kobject_put(idev->map_dir);
	sysfs_remove_group(&idev->dev->kobj, &uio_attr_grp);
err_group:
	dev_err(idev->dev, "error creating sysfs files (%d)\n", ret);
	return ret;
}

static void uio_dev_del_attributes(struct uio_device *idev)
{
	int mi;
	struct uio_mem *mem;
	for (mi = 0; mi < MAX_UIO_MAPS; mi++) {
		mem = &idev->info->mem[mi];
		if (mem->size == 0)
			break;
		kobject_put(&mem->map->kobj);
	}
	kobject_put(idev->map_dir);
	sysfs_remove_group(&idev->dev->kobj, &uio_attr_grp);
}

static int uio_get_minor(struct uio_device *idev)
{
	static DEFINE_MUTEX(minor_lock);
	int retval = -ENOMEM;
	int id;

	mutex_lock(&minor_lock);
	if (idr_pre_get(&uio_idr, GFP_KERNEL) == 0)
		goto exit;

	retval = idr_get_new(&uio_idr, idev, &id);
	if (retval < 0) {
		if (retval == -EAGAIN)
			retval = -ENOMEM;
		goto exit;
	}
	idev->minor = id & MAX_ID_MASK;
exit:
	mutex_unlock(&minor_lock);
	return retval;
}

static void uio_free_minor(struct uio_device *idev)
{
	idr_remove(&uio_idr, idev->minor);
}

/**
 * uio_event_notify - trigger an interrupt event
 * @info: UIO device capabilities
 */
void uio_event_notify(struct uio_info *info)
{
	struct uio_device *idev = info->uio_dev;

	atomic_inc(&idev->event);
	wake_up_interruptible(&idev->wait);
	kill_fasync(&idev->async_queue, SIGIO, POLL_IN);
}
EXPORT_SYMBOL_GPL(uio_event_notify);

/**
 * uio_interrupt - hardware interrupt handler
 * @irq: IRQ number, can be UIO_IRQ_CYCLIC for cyclic timer
 * @dev_id: Pointer to the devices uio_device structure
 */
static irqreturn_t uio_interrupt(int irq, void *dev_id)
{
	struct uio_device *idev = (struct uio_device *)dev_id;
	irqreturn_t ret = idev->info->handler(irq, idev->info);

	if (ret == IRQ_HANDLED)
		uio_event_notify(idev->info);

	return ret;
}

struct uio_listener {
	struct uio_device *dev;
	s32 event_count;
};

static int uio_open(struct inode *inode, struct file *filep)
{
	struct uio_device *idev;
	struct uio_listener *listener;
	int ret = 0;

	idev = idr_find(&uio_idr, iminor(inode));
	if (!idev)
		return -ENODEV;

	listener = kmalloc(sizeof(*listener), GFP_KERNEL);
	if (!listener)
		return -ENOMEM;

	listener->dev = idev;
	listener->event_count = atomic_read(&idev->event);
	filep->private_data = listener;

	if (idev->info->open) {
		if (!try_module_get(idev->owner))
			return -ENODEV;
		ret = idev->info->open(idev->info, inode);
		module_put(idev->owner);
	}

	if (ret)
		kfree(listener);

	return ret;
}

static int uio_fasync(int fd, struct file *filep, int on)
{
	struct uio_listener *listener = filep->private_data;
	struct uio_device *idev = listener->dev;

	return fasync_helper(fd, filep, on, &idev->async_queue);
}

static int uio_release(struct inode *inode, struct file *filep)
{
	int ret = 0;
	struct uio_listener *listener = filep->private_data;
	struct uio_device *idev = listener->dev;

	if (idev->info->release) {
		if (!try_module_get(idev->owner))
			return -ENODEV;
		ret = idev->info->release(idev->info, inode);
		module_put(idev->owner);
	}
	if (filep->f_flags & FASYNC)
		ret = uio_fasync(-1, filep, 0);
	kfree(listener);
	return ret;
}

static unsigned int uio_poll(struct file *filep, poll_table *wait)
{
	struct uio_listener *listener = filep->private_data;
	struct uio_device *idev = listener->dev;

	if (idev->info->irq == UIO_IRQ_NONE)
		return -EIO;

	poll_wait(filep, &idev->wait, wait);
	if (listener->event_count != atomic_read(&idev->event))
		return POLLIN | POLLRDNORM;
	return 0;
}

static ssize_t uio_read(struct file *filep, char __user *buf,
			size_t count, loff_t *ppos)
{
	struct uio_listener *listener = filep->private_data;
	struct uio_device *idev = listener->dev;
	DECLARE_WAITQUEUE(wait, current);
	ssize_t retval;
	s32 event_count;

	if (idev->info->irq == UIO_IRQ_NONE)
		return -EIO;

	if (count != sizeof(s32))
		return -EINVAL;

	add_wait_queue(&idev->wait, &wait);

	do {
		set_current_state(TASK_INTERRUPTIBLE);

		event_count = atomic_read(&idev->event);
		if (event_count != listener->event_count) {
			if (copy_to_user(buf, &event_count, count))
				retval = -EFAULT;
			else {
				listener->event_count = event_count;
				retval = count;
			}
			break;
		}

		if (filep->f_flags & O_NONBLOCK) {
			retval = -EAGAIN;
			break;
		}

		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			break;
		}
		schedule();
	} while (1);

	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&idev->wait, &wait);

	return retval;
}

static int uio_find_mem_index(struct vm_area_struct *vma)
{
	int mi;
	struct uio_device *idev = vma->vm_private_data;

	for (mi = 0; mi < MAX_UIO_MAPS; mi++) {
		if (idev->info->mem[mi].size == 0)
			return -1;
		if (vma->vm_pgoff == mi)
			return mi;
	}
	return -1;
}

static void uio_vma_open(struct vm_area_struct *vma)
{
	struct uio_device *idev = vma->vm_private_data;
	idev->vma_count++;
}

static void uio_vma_close(struct vm_area_struct *vma)
{
	struct uio_device *idev = vma->vm_private_data;
	idev->vma_count--;
}

static struct page *uio_vma_nopage(struct vm_area_struct *vma,
				   unsigned long address, int *type)
{
	struct uio_device *idev = vma->vm_private_data;
	struct page* page = NOPAGE_SIGBUS;

	int mi = uio_find_mem_index(vma);
	if (mi < 0)
		return page;

	if (idev->info->mem[mi].memtype == UIO_MEM_LOGICAL)
		page = virt_to_page(idev->info->mem[mi].addr);
	else
		page = vmalloc_to_page((void*)idev->info->mem[mi].addr);
	get_page(page);
	if (type)
		*type = VM_FAULT_MINOR;
	return page;
}

static struct vm_operations_struct uio_vm_ops = {
	.open = uio_vma_open,
	.close = uio_vma_close,
	.nopage = uio_vma_nopage,
};

static int uio_mmap_physical(struct vm_area_struct *vma)
{
	struct uio_device *idev = vma->vm_private_data;
	int mi = uio_find_mem_index(vma);
	if (mi < 0)
		return -EINVAL;

	vma->vm_flags |= VM_IO | VM_RESERVED;

	return remap_pfn_range(vma,
			       vma->vm_start,
			       idev->info->mem[mi].addr >> PAGE_SHIFT,
			       vma->vm_end - vma->vm_start,
			       vma->vm_page_prot);
}

static int uio_mmap_logical(struct vm_area_struct *vma)
{
	vma->vm_flags |= VM_RESERVED;
	vma->vm_ops = &uio_vm_ops;
	uio_vma_open(vma);
	return 0;
}

static int uio_mmap(struct file *filep, struct vm_area_struct *vma)
{
	struct uio_listener *listener = filep->private_data;
	struct uio_device *idev = listener->dev;
	int mi;
	unsigned long requested_pages, actual_pages;
	int ret = 0;

	if (vma->vm_end < vma->vm_start)
		return -EINVAL;

	vma->vm_private_data = idev;

	mi = uio_find_mem_index(vma);
	if (mi < 0)
		return -EINVAL;

	requested_pages = (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;
	actual_pages = (idev->info->mem[mi].size + PAGE_SIZE -1) >> PAGE_SHIFT;
	if (requested_pages > actual_pages)
		return -EINVAL;

	if (idev->info->mmap) {
		if (!try_module_get(idev->owner))
			return -ENODEV;
		ret = idev->info->mmap(idev->info, vma);
		module_put(idev->owner);
		return ret;
	}

	switch (idev->info->mem[mi].memtype) {
		case UIO_MEM_PHYS:
			return uio_mmap_physical(vma);
		case UIO_MEM_LOGICAL:
		case UIO_MEM_VIRTUAL:
			return uio_mmap_logical(vma);
		default:
			return -EINVAL;
	}
}

static const struct file_operations uio_fops = {
	.owner		= THIS_MODULE,
	.open		= uio_open,
	.release	= uio_release,
	.read		= uio_read,
	.mmap		= uio_mmap,
	.poll		= uio_poll,
	.fasync		= uio_fasync,
};

static int uio_major_init(void)
{
	uio_major = register_chrdev(0, "uio", &uio_fops);
	if (uio_major < 0)
		return uio_major;
	return 0;
}

static void uio_major_cleanup(void)
{
	unregister_chrdev(uio_major, "uio");
}

static int init_uio_class(void)
{
	int ret = 0;

	if (uio_class != NULL) {
		kref_get(&uio_class->kref);
		goto exit;
	}

	/* This is the first time in here, set everything up properly */
	ret = uio_major_init();
	if (ret)
		goto exit;

	uio_class = kzalloc(sizeof(*uio_class), GFP_KERNEL);
	if (!uio_class) {
		ret = -ENOMEM;
		goto err_kzalloc;
	}

	kref_init(&uio_class->kref);
	uio_class->class = class_create(THIS_MODULE, "uio");
	if (IS_ERR(uio_class->class)) {
		ret = IS_ERR(uio_class->class);
		printk(KERN_ERR "class_create failed for uio\n");
		goto err_class_create;
	}
	return 0;

err_class_create:
	kfree(uio_class);
	uio_class = NULL;
err_kzalloc:
	uio_major_cleanup();
exit:
	return ret;
}

static void release_uio_class(struct kref *kref)
{
	/* Ok, we cheat as we know we only have one uio_class */
	class_destroy(uio_class->class);
	kfree(uio_class);
	uio_major_cleanup();
	uio_class = NULL;
}

static void uio_class_destroy(void)
{
	if (uio_class)
		kref_put(&uio_class->kref, release_uio_class);
}

/**
 * uio_register_device - register a new userspace IO device
 * @owner:	module that creates the new device
 * @parent:	parent device
 * @info:	UIO device capabilities
 *
 * returns zero on success or a negative error code.
 */
int __uio_register_device(struct module *owner,
			  struct device *parent,
			  struct uio_info *info)
{
	struct uio_device *idev;
	int ret = 0;

	if (!parent || !info || !info->name || !info->version)
		return -EINVAL;

	info->uio_dev = NULL;

	ret = init_uio_class();
	if (ret)
		return ret;

	idev = kzalloc(sizeof(*idev), GFP_KERNEL);
	if (!idev) {
		ret = -ENOMEM;
		goto err_kzalloc;
	}

	idev->owner = owner;
	idev->info = info;
	init_waitqueue_head(&idev->wait);
	atomic_set(&idev->event, 0);

	ret = uio_get_minor(idev);
	if (ret)
		goto err_get_minor;

	idev->dev = device_create(uio_class->class, parent,
				  MKDEV(uio_major, idev->minor),
				  "uio%d", idev->minor);
	if (IS_ERR(idev->dev)) {
		printk(KERN_ERR "UIO: device register failed\n");
		ret = PTR_ERR(idev->dev);
		goto err_device_create;
	}
	dev_set_drvdata(idev->dev, idev);

	ret = uio_dev_add_attributes(idev);
	if (ret)
		goto err_uio_dev_add_attributes;

	info->uio_dev = idev;

	if (idev->info->irq >= 0) {
		ret = request_irq(idev->info->irq, uio_interrupt,
				  idev->info->irq_flags, idev->info->name, idev);
		if (ret)
			goto err_request_irq;
	}

	return 0;

err_request_irq:
	uio_dev_del_attributes(idev);
err_uio_dev_add_attributes:
	device_destroy(uio_class->class, MKDEV(uio_major, idev->minor));
err_device_create:
	uio_free_minor(idev);
err_get_minor:
	kfree(idev);
err_kzalloc:
	uio_class_destroy();
	return ret;
}
EXPORT_SYMBOL_GPL(__uio_register_device);

/**
 * uio_unregister_device - unregister a industrial IO device
 * @info:	UIO device capabilities
 *
 */
void uio_unregister_device(struct uio_info *info)
{
	struct uio_device *idev;

	if (!info || !info->uio_dev)
		return;

	idev = info->uio_dev;

	uio_free_minor(idev);

	if (info->irq >= 0)
		free_irq(info->irq, idev);

	uio_dev_del_attributes(idev);

	dev_set_drvdata(idev->dev, NULL);
	device_destroy(uio_class->class, MKDEV(uio_major, idev->minor));
	kfree(idev);
	uio_class_destroy();

	return;
}
EXPORT_SYMBOL_GPL(uio_unregister_device);

static int __init uio_init(void)
{
	return 0;
}

static void __exit uio_exit(void)
{
}

module_init(uio_init)
module_exit(uio_exit)
MODULE_LICENSE("GPL v2");
