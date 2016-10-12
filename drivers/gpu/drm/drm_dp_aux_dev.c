/*
 * Copyright © 2015 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Rafael Antognolli <rafael.antognolli@intel.com>
 *
 */

#include <linux/device.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drmP.h>

#include "drm_crtc_helper_internal.h"

struct drm_dp_aux_dev {
	unsigned index;
	struct drm_dp_aux *aux;
	struct device *dev;
	struct kref refcount;
	atomic_t usecount;
};

#define DRM_AUX_MINORS	256
#define AUX_MAX_OFFSET	(1 << 20)
static DEFINE_IDR(aux_idr);
static DEFINE_MUTEX(aux_idr_mutex);
static struct class *drm_dp_aux_dev_class;
static int drm_dev_major = -1;

static struct drm_dp_aux_dev *drm_dp_aux_dev_get_by_minor(unsigned index)
{
	struct drm_dp_aux_dev *aux_dev = NULL;

	mutex_lock(&aux_idr_mutex);
	aux_dev = idr_find(&aux_idr, index);
	if (!kref_get_unless_zero(&aux_dev->refcount))
		aux_dev = NULL;
	mutex_unlock(&aux_idr_mutex);

	return aux_dev;
}

static struct drm_dp_aux_dev *alloc_drm_dp_aux_dev(struct drm_dp_aux *aux)
{
	struct drm_dp_aux_dev *aux_dev;
	int index;

	aux_dev = kzalloc(sizeof(*aux_dev), GFP_KERNEL);
	if (!aux_dev)
		return ERR_PTR(-ENOMEM);
	aux_dev->aux = aux;
	atomic_set(&aux_dev->usecount, 1);
	kref_init(&aux_dev->refcount);

	mutex_lock(&aux_idr_mutex);
	index = idr_alloc_cyclic(&aux_idr, aux_dev, 0, DRM_AUX_MINORS,
				 GFP_KERNEL);
	mutex_unlock(&aux_idr_mutex);
	if (index < 0) {
		kfree(aux_dev);
		return ERR_PTR(index);
	}
	aux_dev->index = index;

	return aux_dev;
}

static void release_drm_dp_aux_dev(struct kref *ref)
{
	struct drm_dp_aux_dev *aux_dev =
		container_of(ref, struct drm_dp_aux_dev, refcount);

	kfree(aux_dev);
}

static ssize_t name_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	ssize_t res;
	struct drm_dp_aux_dev *aux_dev =
		drm_dp_aux_dev_get_by_minor(MINOR(dev->devt));

	if (!aux_dev)
		return -ENODEV;

	res = sprintf(buf, "%s\n", aux_dev->aux->name);
	kref_put(&aux_dev->refcount, release_drm_dp_aux_dev);

	return res;
}
static DEVICE_ATTR_RO(name);

static struct attribute *drm_dp_aux_attrs[] = {
	&dev_attr_name.attr,
	NULL,
};
ATTRIBUTE_GROUPS(drm_dp_aux);

static int auxdev_open(struct inode *inode, struct file *file)
{
	unsigned int minor = iminor(inode);
	struct drm_dp_aux_dev *aux_dev;

	aux_dev = drm_dp_aux_dev_get_by_minor(minor);
	if (!aux_dev)
		return -ENODEV;

	file->private_data = aux_dev;
	return 0;
}

static loff_t auxdev_llseek(struct file *file, loff_t offset, int whence)
{
	return fixed_size_llseek(file, offset, whence, AUX_MAX_OFFSET);
}

static ssize_t auxdev_read(struct file *file, char __user *buf, size_t count,
			   loff_t *offset)
{
	size_t bytes_pending, num_bytes_processed = 0;
	struct drm_dp_aux_dev *aux_dev = file->private_data;
	ssize_t res = 0;

	if (!atomic_inc_not_zero(&aux_dev->usecount))
		return -ENODEV;

	bytes_pending = min((loff_t)count, AUX_MAX_OFFSET - (*offset));

	if (!access_ok(VERIFY_WRITE, buf, bytes_pending)) {
		res = -EFAULT;
		goto out;
	}

	while (bytes_pending > 0) {
		uint8_t localbuf[DP_AUX_MAX_PAYLOAD_BYTES];
		ssize_t todo = min_t(size_t, bytes_pending, sizeof(localbuf));

		if (signal_pending(current)) {
			res = num_bytes_processed ?
				num_bytes_processed : -ERESTARTSYS;
			goto out;
		}

		res = drm_dp_dpcd_read(aux_dev->aux, *offset, localbuf, todo);
		if (res <= 0) {
			res = num_bytes_processed ? num_bytes_processed : res;
			goto out;
		}
		if (__copy_to_user(buf + num_bytes_processed, localbuf, res)) {
			res = num_bytes_processed ?
				num_bytes_processed : -EFAULT;
			goto out;
		}
		bytes_pending -= res;
		*offset += res;
		num_bytes_processed += res;
		res = num_bytes_processed;
	}

out:
	atomic_dec(&aux_dev->usecount);
	wake_up_atomic_t(&aux_dev->usecount);
	return res;
}

static ssize_t auxdev_write(struct file *file, const char __user *buf,
			    size_t count, loff_t *offset)
{
	size_t bytes_pending, num_bytes_processed = 0;
	struct drm_dp_aux_dev *aux_dev = file->private_data;
	ssize_t res = 0;

	if (!atomic_inc_not_zero(&aux_dev->usecount))
		return -ENODEV;

	bytes_pending = min((loff_t)count, AUX_MAX_OFFSET - *offset);

	if (!access_ok(VERIFY_READ, buf, bytes_pending)) {
		res = -EFAULT;
		goto out;
	}

	while (bytes_pending > 0) {
		uint8_t localbuf[DP_AUX_MAX_PAYLOAD_BYTES];
		ssize_t todo = min_t(size_t, bytes_pending, sizeof(localbuf));

		if (signal_pending(current)) {
			res = num_bytes_processed ?
				num_bytes_processed : -ERESTARTSYS;
			goto out;
		}

		if (__copy_from_user(localbuf,
				     buf + num_bytes_processed, todo)) {
			res = num_bytes_processed ?
				num_bytes_processed : -EFAULT;
			goto out;
		}

		res = drm_dp_dpcd_write(aux_dev->aux, *offset, localbuf, todo);
		if (res <= 0) {
			res = num_bytes_processed ? num_bytes_processed : res;
			goto out;
		}
		bytes_pending -= res;
		*offset += res;
		num_bytes_processed += res;
		res = num_bytes_processed;
	}

out:
	atomic_dec(&aux_dev->usecount);
	wake_up_atomic_t(&aux_dev->usecount);
	return res;
}

static int auxdev_release(struct inode *inode, struct file *file)
{
	struct drm_dp_aux_dev *aux_dev = file->private_data;

	kref_put(&aux_dev->refcount, release_drm_dp_aux_dev);
	return 0;
}

static const struct file_operations auxdev_fops = {
	.owner		= THIS_MODULE,
	.llseek		= auxdev_llseek,
	.read		= auxdev_read,
	.write		= auxdev_write,
	.open		= auxdev_open,
	.release	= auxdev_release,
};

#define to_auxdev(d) container_of(d, struct drm_dp_aux_dev, aux)

static struct drm_dp_aux_dev *drm_dp_aux_dev_get_by_aux(struct drm_dp_aux *aux)
{
	struct drm_dp_aux_dev *iter, *aux_dev = NULL;
	int id;

	/* don't increase kref count here because this function should only be
	 * used by drm_dp_aux_unregister_devnode. Thus, it will always have at
	 * least one reference - the one that drm_dp_aux_register_devnode
	 * created
	 */
	mutex_lock(&aux_idr_mutex);
	idr_for_each_entry(&aux_idr, iter, id) {
		if (iter->aux == aux) {
			aux_dev = iter;
			break;
		}
	}
	mutex_unlock(&aux_idr_mutex);
	return aux_dev;
}

static int auxdev_wait_atomic_t(atomic_t *p)
{
	schedule();
	return 0;
}

void drm_dp_aux_unregister_devnode(struct drm_dp_aux *aux)
{
	struct drm_dp_aux_dev *aux_dev;
	unsigned int minor;

	aux_dev = drm_dp_aux_dev_get_by_aux(aux);
	if (!aux_dev) /* attach must have failed */
		return;

	mutex_lock(&aux_idr_mutex);
	idr_remove(&aux_idr, aux_dev->index);
	mutex_unlock(&aux_idr_mutex);

	atomic_dec(&aux_dev->usecount);
	wait_on_atomic_t(&aux_dev->usecount, auxdev_wait_atomic_t,
			 TASK_UNINTERRUPTIBLE);

	minor = aux_dev->index;
	if (aux_dev->dev)
		device_destroy(drm_dp_aux_dev_class,
			       MKDEV(drm_dev_major, minor));

	DRM_DEBUG("drm_dp_aux_dev: aux [%s] unregistering\n", aux->name);
	kref_put(&aux_dev->refcount, release_drm_dp_aux_dev);
}

int drm_dp_aux_register_devnode(struct drm_dp_aux *aux)
{
	struct drm_dp_aux_dev *aux_dev;
	int res;

	aux_dev = alloc_drm_dp_aux_dev(aux);
	if (IS_ERR(aux_dev))
		return PTR_ERR(aux_dev);

	aux_dev->dev = device_create(drm_dp_aux_dev_class, aux->dev,
				     MKDEV(drm_dev_major, aux_dev->index), NULL,
				     "drm_dp_aux%d", aux_dev->index);
	if (IS_ERR(aux_dev->dev)) {
		res = PTR_ERR(aux_dev->dev);
		aux_dev->dev = NULL;
		goto error;
	}

	DRM_DEBUG("drm_dp_aux_dev: aux [%s] registered as minor %d\n",
		  aux->name, aux_dev->index);
	return 0;
error:
	drm_dp_aux_unregister_devnode(aux);
	return res;
}

int drm_dp_aux_dev_init(void)
{
	int res;

	drm_dp_aux_dev_class = class_create(THIS_MODULE, "drm_dp_aux_dev");
	if (IS_ERR(drm_dp_aux_dev_class)) {
		return PTR_ERR(drm_dp_aux_dev_class);
	}
	drm_dp_aux_dev_class->dev_groups = drm_dp_aux_groups;

	res = register_chrdev(0, "aux", &auxdev_fops);
	if (res < 0)
		goto out;
	drm_dev_major = res;

	return 0;
out:
	class_destroy(drm_dp_aux_dev_class);
	return res;
}

void drm_dp_aux_dev_exit(void)
{
	unregister_chrdev(drm_dev_major, "aux");
	class_destroy(drm_dp_aux_dev_class);
}
