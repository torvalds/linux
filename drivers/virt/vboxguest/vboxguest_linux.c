/* SPDX-License-Identifier: GPL-2.0 */
/*
 * vboxguest linux pci driver, char-dev and input-device code,
 *
 * Copyright (C) 2006-2016 Oracle Corporation
 */

#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/poll.h>
#include <linux/vbox_utils.h>
#include "vboxguest_core.h"

/** The device name. */
#define DEVICE_NAME		"vboxguest"
/** The device name for the device node open to everyone. */
#define DEVICE_NAME_USER	"vboxuser"
/** VirtualBox PCI vendor ID. */
#define VBOX_VENDORID		0x80ee
/** VMMDev PCI card product ID. */
#define VMMDEV_DEVICEID		0xcafe

/** Mutex protecting the global vbg_gdev pointer used by vbg_get/put_gdev. */
static DEFINE_MUTEX(vbg_gdev_mutex);
/** Global vbg_gdev pointer used by vbg_get/put_gdev. */
static struct vbg_dev *vbg_gdev;

static int vbg_misc_device_open(struct inode *inode, struct file *filp)
{
	struct vbg_session *session;
	struct vbg_dev *gdev;

	/* misc_open sets filp->private_data to our misc device */
	gdev = container_of(filp->private_data, struct vbg_dev, misc_device);

	session = vbg_core_open_session(gdev, false);
	if (IS_ERR(session))
		return PTR_ERR(session);

	filp->private_data = session;
	return 0;
}

static int vbg_misc_device_user_open(struct inode *inode, struct file *filp)
{
	struct vbg_session *session;
	struct vbg_dev *gdev;

	/* misc_open sets filp->private_data to our misc device */
	gdev = container_of(filp->private_data, struct vbg_dev,
			    misc_device_user);

	session = vbg_core_open_session(gdev, false);
	if (IS_ERR(session))
		return PTR_ERR(session);

	filp->private_data = session;
	return 0;
}

/**
 * Close device.
 * Return: 0 on success, negated errno on failure.
 * @inode:		Pointer to inode info structure.
 * @filp:		Associated file pointer.
 */
static int vbg_misc_device_close(struct inode *inode, struct file *filp)
{
	vbg_core_close_session(filp->private_data);
	filp->private_data = NULL;
	return 0;
}

/**
 * Device I/O Control entry point.
 * Return: 0 on success, negated errno on failure.
 * @filp:		Associated file pointer.
 * @req:		The request specified to ioctl().
 * @arg:		The argument specified to ioctl().
 */
static long vbg_misc_device_ioctl(struct file *filp, unsigned int req,
				  unsigned long arg)
{
	struct vbg_session *session = filp->private_data;
	size_t returned_size, size;
	struct vbg_ioctl_hdr hdr;
	bool is_vmmdev_req;
	int ret = 0;
	void *buf;

	if (copy_from_user(&hdr, (void *)arg, sizeof(hdr)))
		return -EFAULT;

	if (hdr.version != VBG_IOCTL_HDR_VERSION)
		return -EINVAL;

	if (hdr.size_in < sizeof(hdr) ||
	    (hdr.size_out && hdr.size_out < sizeof(hdr)))
		return -EINVAL;

	size = max(hdr.size_in, hdr.size_out);
	if (_IOC_SIZE(req) && _IOC_SIZE(req) != size)
		return -EINVAL;
	if (size > SZ_16M)
		return -E2BIG;

	/*
	 * IOCTL_VMMDEV_REQUEST needs the buffer to be below 4G to avoid
	 * the need for a bounce-buffer and another copy later on.
	 */
	is_vmmdev_req = (req & ~IOCSIZE_MASK) == VBG_IOCTL_VMMDEV_REQUEST(0) ||
			 req == VBG_IOCTL_VMMDEV_REQUEST_BIG;

	if (is_vmmdev_req)
		buf = vbg_req_alloc(size, VBG_IOCTL_HDR_TYPE_DEFAULT);
	else
		buf = kmalloc(size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	*((struct vbg_ioctl_hdr *)buf) = hdr;
	if (copy_from_user(buf + sizeof(hdr), (void *)arg + sizeof(hdr),
			   hdr.size_in - sizeof(hdr))) {
		ret = -EFAULT;
		goto out;
	}
	if (hdr.size_in < size)
		memset(buf + hdr.size_in, 0, size -  hdr.size_in);

	ret = vbg_core_ioctl(session, req, buf);
	if (ret)
		goto out;

	returned_size = ((struct vbg_ioctl_hdr *)buf)->size_out;
	if (returned_size > size) {
		vbg_debug("%s: too much output data %zu > %zu\n",
			  __func__, returned_size, size);
		returned_size = size;
	}
	if (copy_to_user((void *)arg, buf, returned_size) != 0)
		ret = -EFAULT;

out:
	if (is_vmmdev_req)
		vbg_req_free(buf, size);
	else
		kfree(buf);

	return ret;
}

/** The file_operations structures. */
static const struct file_operations vbg_misc_device_fops = {
	.owner			= THIS_MODULE,
	.open			= vbg_misc_device_open,
	.release		= vbg_misc_device_close,
	.unlocked_ioctl		= vbg_misc_device_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl		= vbg_misc_device_ioctl,
#endif
};
static const struct file_operations vbg_misc_device_user_fops = {
	.owner			= THIS_MODULE,
	.open			= vbg_misc_device_user_open,
	.release		= vbg_misc_device_close,
	.unlocked_ioctl		= vbg_misc_device_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl		= vbg_misc_device_ioctl,
#endif
};

/**
 * Called when the input device is first opened.
 *
 * Sets up absolute mouse reporting.
 */
static int vbg_input_open(struct input_dev *input)
{
	struct vbg_dev *gdev = input_get_drvdata(input);
	u32 feat = VMMDEV_MOUSE_GUEST_CAN_ABSOLUTE | VMMDEV_MOUSE_NEW_PROTOCOL;
	int ret;

	ret = vbg_core_set_mouse_status(gdev, feat);
	if (ret)
		return ret;

	return 0;
}

/**
 * Called if all open handles to the input device are closed.
 *
 * Disables absolute reporting.
 */
static void vbg_input_close(struct input_dev *input)
{
	struct vbg_dev *gdev = input_get_drvdata(input);

	vbg_core_set_mouse_status(gdev, 0);
}

/**
 * Creates the kernel input device.
 *
 * Return: 0 on success, negated errno on failure.
 */
static int vbg_create_input_device(struct vbg_dev *gdev)
{
	struct input_dev *input;

	input = devm_input_allocate_device(gdev->dev);
	if (!input)
		return -ENOMEM;

	input->id.bustype = BUS_PCI;
	input->id.vendor = VBOX_VENDORID;
	input->id.product = VMMDEV_DEVICEID;
	input->open = vbg_input_open;
	input->close = vbg_input_close;
	input->dev.parent = gdev->dev;
	input->name = "VirtualBox mouse integration";

	input_set_abs_params(input, ABS_X, VMMDEV_MOUSE_RANGE_MIN,
			     VMMDEV_MOUSE_RANGE_MAX, 0, 0);
	input_set_abs_params(input, ABS_Y, VMMDEV_MOUSE_RANGE_MIN,
			     VMMDEV_MOUSE_RANGE_MAX, 0, 0);
	input_set_capability(input, EV_KEY, BTN_MOUSE);
	input_set_drvdata(input, gdev);

	gdev->input = input;

	return input_register_device(gdev->input);
}

static ssize_t host_version_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct vbg_dev *gdev = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", gdev->host_version);
}

static ssize_t host_features_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct vbg_dev *gdev = dev_get_drvdata(dev);

	return sprintf(buf, "%#x\n", gdev->host_features);
}

static DEVICE_ATTR_RO(host_version);
static DEVICE_ATTR_RO(host_features);

/**
 * Does the PCI detection and init of the device.
 *
 * Return: 0 on success, negated errno on failure.
 */
static int vbg_pci_probe(struct pci_dev *pci, const struct pci_device_id *id)
{
	struct device *dev = &pci->dev;
	resource_size_t io, io_len, mmio, mmio_len;
	struct vmmdev_memory *vmmdev;
	struct vbg_dev *gdev;
	int ret;

	gdev = devm_kzalloc(dev, sizeof(*gdev), GFP_KERNEL);
	if (!gdev)
		return -ENOMEM;

	ret = pci_enable_device(pci);
	if (ret != 0) {
		vbg_err("vboxguest: Error enabling device: %d\n", ret);
		return ret;
	}

	ret = -ENODEV;

	io = pci_resource_start(pci, 0);
	io_len = pci_resource_len(pci, 0);
	if (!io || !io_len) {
		vbg_err("vboxguest: Error IO-port resource (0) is missing\n");
		goto err_disable_pcidev;
	}
	if (devm_request_region(dev, io, io_len, DEVICE_NAME) == NULL) {
		vbg_err("vboxguest: Error could not claim IO resource\n");
		ret = -EBUSY;
		goto err_disable_pcidev;
	}

	mmio = pci_resource_start(pci, 1);
	mmio_len = pci_resource_len(pci, 1);
	if (!mmio || !mmio_len) {
		vbg_err("vboxguest: Error MMIO resource (1) is missing\n");
		goto err_disable_pcidev;
	}

	if (devm_request_mem_region(dev, mmio, mmio_len, DEVICE_NAME) == NULL) {
		vbg_err("vboxguest: Error could not claim MMIO resource\n");
		ret = -EBUSY;
		goto err_disable_pcidev;
	}

	vmmdev = devm_ioremap(dev, mmio, mmio_len);
	if (!vmmdev) {
		vbg_err("vboxguest: Error ioremap failed; MMIO addr=%pap size=%pap\n",
			&mmio, &mmio_len);
		goto err_disable_pcidev;
	}

	/* Validate MMIO region version and size. */
	if (vmmdev->version != VMMDEV_MEMORY_VERSION ||
	    vmmdev->size < 32 || vmmdev->size > mmio_len) {
		vbg_err("vboxguest: Bogus VMMDev memory; version=%08x (expected %08x) size=%d (expected <= %d)\n",
			vmmdev->version, VMMDEV_MEMORY_VERSION,
			vmmdev->size, (int)mmio_len);
		goto err_disable_pcidev;
	}

	gdev->io_port = io;
	gdev->mmio = vmmdev;
	gdev->dev = dev;
	gdev->misc_device.minor = MISC_DYNAMIC_MINOR;
	gdev->misc_device.name = DEVICE_NAME;
	gdev->misc_device.fops = &vbg_misc_device_fops;
	gdev->misc_device_user.minor = MISC_DYNAMIC_MINOR;
	gdev->misc_device_user.name = DEVICE_NAME_USER;
	gdev->misc_device_user.fops = &vbg_misc_device_user_fops;

	ret = vbg_core_init(gdev, VMMDEV_EVENT_MOUSE_POSITION_CHANGED);
	if (ret)
		goto err_disable_pcidev;

	ret = vbg_create_input_device(gdev);
	if (ret) {
		vbg_err("vboxguest: Error creating input device: %d\n", ret);
		goto err_vbg_core_exit;
	}

	ret = devm_request_irq(dev, pci->irq, vbg_core_isr, IRQF_SHARED,
			       DEVICE_NAME, gdev);
	if (ret) {
		vbg_err("vboxguest: Error requesting irq: %d\n", ret);
		goto err_vbg_core_exit;
	}

	ret = misc_register(&gdev->misc_device);
	if (ret) {
		vbg_err("vboxguest: Error misc_register %s failed: %d\n",
			DEVICE_NAME, ret);
		goto err_vbg_core_exit;
	}

	ret = misc_register(&gdev->misc_device_user);
	if (ret) {
		vbg_err("vboxguest: Error misc_register %s failed: %d\n",
			DEVICE_NAME_USER, ret);
		goto err_unregister_misc_device;
	}

	mutex_lock(&vbg_gdev_mutex);
	if (!vbg_gdev)
		vbg_gdev = gdev;
	else
		ret = -EBUSY;
	mutex_unlock(&vbg_gdev_mutex);

	if (ret) {
		vbg_err("vboxguest: Error more then 1 vbox guest pci device\n");
		goto err_unregister_misc_device_user;
	}

	pci_set_drvdata(pci, gdev);
	device_create_file(dev, &dev_attr_host_version);
	device_create_file(dev, &dev_attr_host_features);

	vbg_info("vboxguest: misc device minor %d, IRQ %d, I/O port %x, MMIO at %pap (size %pap)\n",
		 gdev->misc_device.minor, pci->irq, gdev->io_port,
		 &mmio, &mmio_len);

	return 0;

err_unregister_misc_device_user:
	misc_deregister(&gdev->misc_device_user);
err_unregister_misc_device:
	misc_deregister(&gdev->misc_device);
err_vbg_core_exit:
	vbg_core_exit(gdev);
err_disable_pcidev:
	pci_disable_device(pci);

	return ret;
}

static void vbg_pci_remove(struct pci_dev *pci)
{
	struct vbg_dev *gdev = pci_get_drvdata(pci);

	mutex_lock(&vbg_gdev_mutex);
	vbg_gdev = NULL;
	mutex_unlock(&vbg_gdev_mutex);

	device_remove_file(gdev->dev, &dev_attr_host_features);
	device_remove_file(gdev->dev, &dev_attr_host_version);
	misc_deregister(&gdev->misc_device_user);
	misc_deregister(&gdev->misc_device);
	vbg_core_exit(gdev);
	pci_disable_device(pci);
}

struct vbg_dev *vbg_get_gdev(void)
{
	mutex_lock(&vbg_gdev_mutex);

	/*
	 * Note on success we keep the mutex locked until vbg_put_gdev(),
	 * this stops vbg_pci_remove from removing the device from underneath
	 * vboxsf. vboxsf will only hold a reference for a short while.
	 */
	if (vbg_gdev)
		return vbg_gdev;

	mutex_unlock(&vbg_gdev_mutex);
	return ERR_PTR(-ENODEV);
}
EXPORT_SYMBOL(vbg_get_gdev);

void vbg_put_gdev(struct vbg_dev *gdev)
{
	WARN_ON(gdev != vbg_gdev);
	mutex_unlock(&vbg_gdev_mutex);
}
EXPORT_SYMBOL(vbg_put_gdev);

/**
 * Callback for mouse events.
 *
 * This is called at the end of the ISR, after leaving the event spinlock, if
 * VMMDEV_EVENT_MOUSE_POSITION_CHANGED was raised by the host.
 *
 * @gdev:		The device extension.
 */
void vbg_linux_mouse_event(struct vbg_dev *gdev)
{
	int rc;

	/* Report events to the kernel input device */
	gdev->mouse_status_req->mouse_features = 0;
	gdev->mouse_status_req->pointer_pos_x = 0;
	gdev->mouse_status_req->pointer_pos_y = 0;
	rc = vbg_req_perform(gdev, gdev->mouse_status_req);
	if (rc >= 0) {
		input_report_abs(gdev->input, ABS_X,
				 gdev->mouse_status_req->pointer_pos_x);
		input_report_abs(gdev->input, ABS_Y,
				 gdev->mouse_status_req->pointer_pos_y);
		input_sync(gdev->input);
	}
}

static const struct pci_device_id vbg_pci_ids[] = {
	{ .vendor = VBOX_VENDORID, .device = VMMDEV_DEVICEID },
	{}
};
MODULE_DEVICE_TABLE(pci,  vbg_pci_ids);

static struct pci_driver vbg_pci_driver = {
	.name		= DEVICE_NAME,
	.id_table	= vbg_pci_ids,
	.probe		= vbg_pci_probe,
	.remove		= vbg_pci_remove,
};

module_pci_driver(vbg_pci_driver);

MODULE_AUTHOR("Oracle Corporation");
MODULE_DESCRIPTION("Oracle VM VirtualBox Guest Additions for Linux Module");
MODULE_LICENSE("GPL");
