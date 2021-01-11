// SPDX-License-Identifier: GPL-2.0+
/*
 * Provides user-space access to the SSAM EC via the /dev/surface/aggregator
 * misc device. Intended for debugging and development.
 *
 * Copyright (C) 2020 Maximilian Luz <luzmaximilian@gmail.com>
 */

#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include <linux/surface_aggregator/cdev.h>
#include <linux/surface_aggregator/controller.h>

#define SSAM_CDEV_DEVICE_NAME	"surface_aggregator_cdev"

struct ssam_cdev {
	struct kref kref;
	struct rw_semaphore lock;
	struct ssam_controller *ctrl;
	struct miscdevice mdev;
};

static void __ssam_cdev_release(struct kref *kref)
{
	kfree(container_of(kref, struct ssam_cdev, kref));
}

static struct ssam_cdev *ssam_cdev_get(struct ssam_cdev *cdev)
{
	if (cdev)
		kref_get(&cdev->kref);

	return cdev;
}

static void ssam_cdev_put(struct ssam_cdev *cdev)
{
	if (cdev)
		kref_put(&cdev->kref, __ssam_cdev_release);
}

static int ssam_cdev_device_open(struct inode *inode, struct file *filp)
{
	struct miscdevice *mdev = filp->private_data;
	struct ssam_cdev *cdev = container_of(mdev, struct ssam_cdev, mdev);

	filp->private_data = ssam_cdev_get(cdev);
	return stream_open(inode, filp);
}

static int ssam_cdev_device_release(struct inode *inode, struct file *filp)
{
	ssam_cdev_put(filp->private_data);
	return 0;
}

static long ssam_cdev_request(struct ssam_cdev *cdev, unsigned long arg)
{
	struct ssam_cdev_request __user *r;
	struct ssam_cdev_request rqst;
	struct ssam_request spec = {};
	struct ssam_response rsp = {};
	const void __user *plddata;
	void __user *rspdata;
	int status = 0, ret = 0, tmp;

	r = (struct ssam_cdev_request __user *)arg;
	ret = copy_struct_from_user(&rqst, sizeof(rqst), r, sizeof(*r));
	if (ret)
		goto out;

	plddata = u64_to_user_ptr(rqst.payload.data);
	rspdata = u64_to_user_ptr(rqst.response.data);

	/* Setup basic request fields. */
	spec.target_category = rqst.target_category;
	spec.target_id = rqst.target_id;
	spec.command_id = rqst.command_id;
	spec.instance_id = rqst.instance_id;
	spec.flags = 0;
	spec.length = rqst.payload.length;
	spec.payload = NULL;

	if (rqst.flags & SSAM_CDEV_REQUEST_HAS_RESPONSE)
		spec.flags |= SSAM_REQUEST_HAS_RESPONSE;

	if (rqst.flags & SSAM_CDEV_REQUEST_UNSEQUENCED)
		spec.flags |= SSAM_REQUEST_UNSEQUENCED;

	rsp.capacity = rqst.response.length;
	rsp.length = 0;
	rsp.pointer = NULL;

	/* Get request payload from user-space. */
	if (spec.length) {
		if (!plddata) {
			ret = -EINVAL;
			goto out;
		}

		/*
		 * Note: spec.length is limited to U16_MAX bytes via struct
		 * ssam_cdev_request. This is slightly larger than the
		 * theoretical maximum (SSH_COMMAND_MAX_PAYLOAD_SIZE) of the
		 * underlying protocol (note that nothing remotely this size
		 * should ever be allocated in any normal case). This size is
		 * validated later in ssam_request_sync(), for allocation the
		 * bound imposed by u16 should be enough.
		 */
		spec.payload = kzalloc(spec.length, GFP_KERNEL);
		if (!spec.payload) {
			ret = -ENOMEM;
			goto out;
		}

		if (copy_from_user((void *)spec.payload, plddata, spec.length)) {
			ret = -EFAULT;
			goto out;
		}
	}

	/* Allocate response buffer. */
	if (rsp.capacity) {
		if (!rspdata) {
			ret = -EINVAL;
			goto out;
		}

		/*
		 * Note: rsp.capacity is limited to U16_MAX bytes via struct
		 * ssam_cdev_request. This is slightly larger than the
		 * theoretical maximum (SSH_COMMAND_MAX_PAYLOAD_SIZE) of the
		 * underlying protocol (note that nothing remotely this size
		 * should ever be allocated in any normal case). In later use,
		 * this capacity does not have to be strictly bounded, as it
		 * is only used as an output buffer to be written to. For
		 * allocation the bound imposed by u16 should be enough.
		 */
		rsp.pointer = kzalloc(rsp.capacity, GFP_KERNEL);
		if (!rsp.pointer) {
			ret = -ENOMEM;
			goto out;
		}
	}

	/* Perform request. */
	status = ssam_request_sync(cdev->ctrl, &spec, &rsp);
	if (status)
		goto out;

	/* Copy response to user-space. */
	if (rsp.length && copy_to_user(rspdata, rsp.pointer, rsp.length))
		ret = -EFAULT;

out:
	/* Always try to set response-length and status. */
	tmp = put_user(rsp.length, &r->response.length);
	if (tmp)
		ret = tmp;

	tmp = put_user(status, &r->status);
	if (tmp)
		ret = tmp;

	/* Cleanup. */
	kfree(spec.payload);
	kfree(rsp.pointer);

	return ret;
}

static long __ssam_cdev_device_ioctl(struct ssam_cdev *cdev, unsigned int cmd,
				     unsigned long arg)
{
	switch (cmd) {
	case SSAM_CDEV_REQUEST:
		return ssam_cdev_request(cdev, arg);

	default:
		return -ENOTTY;
	}
}

static long ssam_cdev_device_ioctl(struct file *file, unsigned int cmd,
				   unsigned long arg)
{
	struct ssam_cdev *cdev = file->private_data;
	long status;

	/* Ensure that controller is valid for as long as we need it. */
	if (down_read_killable(&cdev->lock))
		return -ERESTARTSYS;

	if (!cdev->ctrl) {
		up_read(&cdev->lock);
		return -ENODEV;
	}

	status = __ssam_cdev_device_ioctl(cdev, cmd, arg);

	up_read(&cdev->lock);
	return status;
}

static const struct file_operations ssam_controller_fops = {
	.owner          = THIS_MODULE,
	.open           = ssam_cdev_device_open,
	.release        = ssam_cdev_device_release,
	.unlocked_ioctl = ssam_cdev_device_ioctl,
	.compat_ioctl   = ssam_cdev_device_ioctl,
	.llseek         = noop_llseek,
};

static int ssam_dbg_device_probe(struct platform_device *pdev)
{
	struct ssam_controller *ctrl;
	struct ssam_cdev *cdev;
	int status;

	ctrl = ssam_client_bind(&pdev->dev);
	if (IS_ERR(ctrl))
		return PTR_ERR(ctrl) == -ENODEV ? -EPROBE_DEFER : PTR_ERR(ctrl);

	cdev = kzalloc(sizeof(*cdev), GFP_KERNEL);
	if (!cdev)
		return -ENOMEM;

	kref_init(&cdev->kref);
	init_rwsem(&cdev->lock);
	cdev->ctrl = ctrl;

	cdev->mdev.parent   = &pdev->dev;
	cdev->mdev.minor    = MISC_DYNAMIC_MINOR;
	cdev->mdev.name     = "surface_aggregator";
	cdev->mdev.nodename = "surface/aggregator";
	cdev->mdev.fops     = &ssam_controller_fops;

	status = misc_register(&cdev->mdev);
	if (status) {
		kfree(cdev);
		return status;
	}

	platform_set_drvdata(pdev, cdev);
	return 0;
}

static int ssam_dbg_device_remove(struct platform_device *pdev)
{
	struct ssam_cdev *cdev = platform_get_drvdata(pdev);

	misc_deregister(&cdev->mdev);

	/*
	 * The controller is only guaranteed to be valid for as long as the
	 * driver is bound. Remove controller so that any lingering open files
	 * cannot access it any more after we're gone.
	 */
	down_write(&cdev->lock);
	cdev->ctrl = NULL;
	up_write(&cdev->lock);

	ssam_cdev_put(cdev);
	return 0;
}

static struct platform_device *ssam_cdev_device;

static struct platform_driver ssam_cdev_driver = {
	.probe = ssam_dbg_device_probe,
	.remove = ssam_dbg_device_remove,
	.driver = {
		.name = SSAM_CDEV_DEVICE_NAME,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
};

static int __init ssam_debug_init(void)
{
	int status;

	ssam_cdev_device = platform_device_alloc(SSAM_CDEV_DEVICE_NAME,
						 PLATFORM_DEVID_NONE);
	if (!ssam_cdev_device)
		return -ENOMEM;

	status = platform_device_add(ssam_cdev_device);
	if (status)
		goto err_device;

	status = platform_driver_register(&ssam_cdev_driver);
	if (status)
		goto err_driver;

	return 0;

err_driver:
	platform_device_del(ssam_cdev_device);
err_device:
	platform_device_put(ssam_cdev_device);
	return status;
}
module_init(ssam_debug_init);

static void __exit ssam_debug_exit(void)
{
	platform_driver_unregister(&ssam_cdev_driver);
	platform_device_unregister(ssam_cdev_device);
}
module_exit(ssam_debug_exit);

MODULE_AUTHOR("Maximilian Luz <luzmaximilian@gmail.com>");
MODULE_DESCRIPTION("User-space interface for Surface System Aggregator Module");
MODULE_LICENSE("GPL");
