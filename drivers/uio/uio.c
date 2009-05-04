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
	struct kobject		*portio_dir;
};

static int uio_major;
static DEFINE_IDR(uio_idr);
static const struct file_operations uio_fops;

/* UIO class infrastructure */
static struct uio_class {
	struct kref kref;
	struct class *class;
} *uio_class;

/* Protect idr accesses */
static DEFINE_MUTEX(minor_lock);

/*
 * attributes
 */

struct uio_map {
	struct kobject kobj;
	struct uio_mem *mem;
};
#define to_map(map) container_of(map, struct uio_map, kobj)

static ssize_t map_name_show(struct uio_mem *mem, char *buf)
{
	if (unlikely(!mem->name))
		mem->name = "";

	return sprintf(buf, "%s\n", mem->name);
}

static ssize_t map_addr_show(struct uio_mem *mem, char *buf)
{
	return sprintf(buf, "0x%lx\n", mem->addr);
}

static ssize_t map_size_show(struct uio_mem *mem, char *buf)
{
	return sprintf(buf, "0x%lx\n", mem->size);
}

static ssize_t map_offset_show(struct uio_mem *mem, char *buf)
{
	return sprintf(buf, "0x%lx\n", mem->addr & ~PAGE_MASK);
}

struct map_sysfs_entry {
	struct attribute attr;
	ssize_t (*show)(struct uio_mem *, char *);
	ssize_t (*store)(struct uio_mem *, const char *, size_t);
};

static struct map_sysfs_entry name_attribute =
	__ATTR(name, S_IRUGO, map_name_show, NULL);
static struct map_sysfs_entry addr_attribute =
	__ATTR(addr, S_IRUGO, map_addr_show, NULL);
static struct map_sysfs_entry size_attribute =
	__ATTR(size, S_IRUGO, map_size_show, NULL);
static struct map_sysfs_entry offset_attribute =
	__ATTR(offset, S_IRUGO, map_offset_show, NULL);

static struct attribute *attrs[] = {
	&name_attribute.attr,
	&addr_attribute.attr,
	&size_attribute.attr,
	&offset_attribute.attr,
	NULL,	/* need to NULL terminate the list of attributes */
};

static void map_release(struct kobject *kobj)
{
	struct uio_map *map = to_map(kobj);
	kfree(map);
}

static ssize_t map_type_show(struct kobject *kobj, struct attribute *attr,
			     char *buf)
{
	struct uio_map *map = to_map(kobj);
	struct uio_mem *mem = map->mem;
	struct map_sysfs_entry *entry;

	entry = container_of(attr, struct map_sysfs_entry, attr);

	if (!entry->show)
		return -EIO;

	return entry->show(mem, buf);
}

static struct sysfs_ops map_sysfs_ops = {
	.show = map_type_show,
};

static struct kobj_type map_attr_type = {
	.release	= map_release,
	.sysfs_ops	= &map_sysfs_ops,
	.default_attrs	= attrs,
};

struct uio_portio {
	struct kobject kobj;
	struct uio_port *port;
};
#define to_portio(portio) container_of(portio, struct uio_portio, kobj)

static ssize_t portio_name_show(struct uio_port *port, char *buf)
{
	if (unlikely(!port->name))
		port->name = "";

	return sprintf(buf, "%s\n", port->name);
}

static ssize_t portio_start_show(struct uio_port *port, char *buf)
{
	return sprintf(buf, "0x%lx\n", port->start);
}

static ssize_t portio_size_show(struct uio_port *port, char *buf)
{
	return sprintf(buf, "0x%lx\n", port->size);
}

static ssize_t portio_porttype_show(struct uio_port *port, char *buf)
{
	const char *porttypes[] = {"none", "x86", "gpio", "other"};

	if ((port->porttype < 0) || (port->porttype > UIO_PORT_OTHER))
		return -EINVAL;

	return sprintf(buf, "port_%s\n", porttypes[port->porttype]);
}

struct portio_sysfs_entry {
	struct attribute attr;
	ssize_t (*show)(struct uio_port *, char *);
	ssize_t (*store)(struct uio_port *, const char *, size_t);
};

static struct portio_sysfs_entry portio_name_attribute =
	__ATTR(name, S_IRUGO, portio_name_show, NULL);
static struct portio_sysfs_entry portio_start_attribute =
	__ATTR(start, S_IRUGO, portio_start_show, NULL);
static struct portio_sysfs_entry portio_size_attribute =
	__ATTR(size, S_IRUGO, portio_size_show, NULL);
static struct portio_sysfs_entry portio_porttype_attribute =
	__ATTR(porttype, S_IRUGO, portio_porttype_show, NULL);

static struct attribute *portio_attrs[] = {
	&portio_name_attribute.attr,
	&portio_start_attribute.attr,
	&portio_size_attribute.attr,
	&portio_porttype_attribute.attr,
	NULL,
};

static void portio_release(struct kobject *kobj)
{
	struct uio_portio *portio = to_portio(kobj);
	kfree(portio);
}

static ssize_t portio_type_show(struct kobject *kobj, struct attribute *attr,
			     char *buf)
{
	struct uio_portio *portio = to_portio(kobj);
	struct uio_port *port = portio->port;
	struct portio_sysfs_entry *entry;

	entry = container_of(attr, struct portio_sysfs_entry, attr);

	if (!entry->show)
		return -EIO;

	return entry->show(port, buf);
}

static struct sysfs_ops portio_sysfs_ops = {
	.show = portio_type_show,
};

static struct kobj_type portio_attr_type = {
	.release	= portio_release,
	.sysfs_ops	= &portio_sysfs_ops,
	.default_attrs	= portio_attrs,
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
	int mi, pi;
	int map_found = 0;
	int portio_found = 0;
	struct uio_mem *mem;
	struct uio_map *map;
	struct uio_port *port;
	struct uio_portio *portio;

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
				goto err_map;
		}
		map = kzalloc(sizeof(*map), GFP_KERNEL);
		if (!map)
			goto err_map;
		kobject_init(&map->kobj, &map_attr_type);
		map->mem = mem;
		mem->map = map;
		ret = kobject_add(&map->kobj, idev->map_dir, "map%d", mi);
		if (ret)
			goto err_map;
		ret = kobject_uevent(&map->kobj, KOBJ_ADD);
		if (ret)
			goto err_map;
	}

	for (pi = 0; pi < MAX_UIO_PORT_REGIONS; pi++) {
		port = &idev->info->port[pi];
		if (port->size == 0)
			break;
		if (!portio_found) {
			portio_found = 1;
			idev->portio_dir = kobject_create_and_add("portio",
							&idev->dev->kobj);
			if (!idev->portio_dir)
				goto err_portio;
		}
		portio = kzalloc(sizeof(*portio), GFP_KERNEL);
		if (!portio)
			goto err_portio;
		kobject_init(&portio->kobj, &portio_attr_type);
		portio->port = port;
		port->portio = portio;
		ret = kobject_add(&portio->kobj, idev->portio_dir,
							"port%d", pi);
		if (ret)
			goto err_portio;
		ret = kobject_uevent(&portio->kobj, KOBJ_ADD);
		if (ret)
			goto err_portio;
	}

	return 0;

err_portio:
	for (pi--; pi >= 0; pi--) {
		port = &idev->info->port[pi];
		portio = port->portio;
		kobject_put(&portio->kobj);
	}
	kobject_put(idev->portio_dir);
err_map:
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
	int i;
	struct uio_mem *mem;
	struct uio_port *port;

	for (i = 0; i < MAX_UIO_MAPS; i++) {
		mem = &idev->info->mem[i];
		if (mem->size == 0)
			break;
		kobject_put(&mem->map->kobj);
	}
	kobject_put(idev->map_dir);

	for (i = 0; i < MAX_UIO_PORT_REGIONS; i++) {
		port = &idev->info->port[i];
		if (port->size == 0)
			break;
		kobject_put(&port->portio->kobj);
	}
	kobject_put(idev->portio_dir);

	sysfs_remove_group(&idev->dev->kobj, &uio_attr_grp);
}

static int uio_get_minor(struct uio_device *idev)
{
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
	mutex_lock(&minor_lock);
	idr_remove(&uio_idr, idev->minor);
	mutex_unlock(&minor_lock);
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

	mutex_lock(&minor_lock);
	idev = idr_find(&uio_idr, iminor(inode));
	mutex_unlock(&minor_lock);
	if (!idev) {
		ret = -ENODEV;
		goto out;
	}

	if (!try_module_get(idev->owner)) {
		ret = -ENODEV;
		goto out;
	}

	listener = kmalloc(sizeof(*listener), GFP_KERNEL);
	if (!listener) {
		ret = -ENOMEM;
		goto err_alloc_listener;
	}

	listener->dev = idev;
	listener->event_count = atomic_read(&idev->event);
	filep->private_data = listener;

	if (idev->info->open) {
		ret = idev->info->open(idev->info, inode);
		if (ret)
			goto err_infoopen;
	}
	return 0;

err_infoopen:
	kfree(listener);

err_alloc_listener:
	module_put(idev->owner);

out:
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

	if (idev->info->release)
		ret = idev->info->release(idev->info, inode);

	module_put(idev->owner);
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

static ssize_t uio_write(struct file *filep, const char __user *buf,
			size_t count, loff_t *ppos)
{
	struct uio_listener *listener = filep->private_data;
	struct uio_device *idev = listener->dev;
	ssize_t retval;
	s32 irq_on;

	if (idev->info->irq == UIO_IRQ_NONE)
		return -EIO;

	if (count != sizeof(s32))
		return -EINVAL;

	if (!idev->info->irqcontrol)
		return -ENOSYS;

	if (copy_from_user(&irq_on, buf, count))
		return -EFAULT;

	retval = idev->info->irqcontrol(idev->info, irq_on);

	return retval ? retval : sizeof(s32);
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

static int uio_vma_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct uio_device *idev = vma->vm_private_data;
	struct page *page;
	unsigned long offset;

	int mi = uio_find_mem_index(vma);
	if (mi < 0)
		return VM_FAULT_SIGBUS;

	/*
	 * We need to subtract mi because userspace uses offset = N*PAGE_SIZE
	 * to use mem[N].
	 */
	offset = (vmf->pgoff - mi) << PAGE_SHIFT;

	if (idev->info->mem[mi].memtype == UIO_MEM_LOGICAL)
		page = virt_to_page(idev->info->mem[mi].addr + offset);
	else
		page = vmalloc_to_page((void *)idev->info->mem[mi].addr
							+ offset);
	get_page(page);
	vmf->page = page;
	return 0;
}

static struct vm_operations_struct uio_vm_ops = {
	.open = uio_vma_open,
	.close = uio_vma_close,
	.fault = uio_vma_fault,
};

static int uio_mmap_physical(struct vm_area_struct *vma)
{
	struct uio_device *idev = vma->vm_private_data;
	int mi = uio_find_mem_index(vma);
	if (mi < 0)
		return -EINVAL;

	vma->vm_flags |= VM_IO | VM_RESERVED;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

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
	actual_pages = ((idev->info->mem[mi].addr & ~PAGE_MASK)
			+ idev->info->mem[mi].size + PAGE_SIZE -1) >> PAGE_SHIFT;
	if (requested_pages > actual_pages)
		return -EINVAL;

	if (idev->info->mmap) {
		ret = idev->info->mmap(idev->info, vma);
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
	.write		= uio_write,
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
				  MKDEV(uio_major, idev->minor), idev,
				  "uio%d", idev->minor);
	if (IS_ERR(idev->dev)) {
		printk(KERN_ERR "UIO: device register failed\n");
		ret = PTR_ERR(idev->dev);
		goto err_device_create;
	}

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
