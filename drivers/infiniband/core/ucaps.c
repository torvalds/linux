// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved
 */

#include <linux/kref.h>
#include <linux/cdev.h>
#include <linux/mutex.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <rdma/ib_ucaps.h>

#define RDMA_UCAP_FIRST RDMA_UCAP_MLX5_CTRL_LOCAL

static DEFINE_MUTEX(ucaps_mutex);
static struct ib_ucap *ucaps_list[RDMA_UCAP_MAX];
static bool ucaps_class_is_registered;
static dev_t ucaps_base_dev;

struct ib_ucap {
	struct cdev cdev;
	struct device dev;
	struct kref ref;
};

static const char *ucap_names[RDMA_UCAP_MAX] = {
	[RDMA_UCAP_MLX5_CTRL_LOCAL] = "mlx5_perm_ctrl_local",
	[RDMA_UCAP_MLX5_CTRL_OTHER_VHCA] = "mlx5_perm_ctrl_other_vhca"
};

static char *ucaps_devnode(const struct device *dev, umode_t *mode)
{
	if (mode)
		*mode = 0600;

	return kasprintf(GFP_KERNEL, "infiniband/%s", dev_name(dev));
}

static const struct class ucaps_class = {
	.name = "infiniband_ucaps",
	.devnode = ucaps_devnode,
};

static const struct file_operations ucaps_cdev_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
};

/**
 * ib_cleanup_ucaps - cleanup all API resources and class.
 *
 * This is called once, when removing the ib_uverbs module.
 */
void ib_cleanup_ucaps(void)
{
	mutex_lock(&ucaps_mutex);
	if (!ucaps_class_is_registered) {
		mutex_unlock(&ucaps_mutex);
		return;
	}

	for (int i = RDMA_UCAP_FIRST; i < RDMA_UCAP_MAX; i++)
		WARN_ON(ucaps_list[i]);

	class_unregister(&ucaps_class);
	ucaps_class_is_registered = false;
	unregister_chrdev_region(ucaps_base_dev, RDMA_UCAP_MAX);
	mutex_unlock(&ucaps_mutex);
}

static int get_ucap_from_devt(dev_t devt, u64 *idx_mask)
{
	for (int type = RDMA_UCAP_FIRST; type < RDMA_UCAP_MAX; type++) {
		if (ucaps_list[type] && ucaps_list[type]->dev.devt == devt) {
			*idx_mask |= 1 << type;
			return 0;
		}
	}

	return -EINVAL;
}

static int get_devt_from_fd(unsigned int fd, dev_t *ret_dev)
{
	struct file *file;

	file = fget(fd);
	if (!file)
		return -EBADF;

	*ret_dev = file_inode(file)->i_rdev;
	fput(file);
	return 0;
}

/**
 * ib_ucaps_init - Initialization required before ucap creation.
 *
 * Return: 0 on success, or a negative errno value on failure
 */
static int ib_ucaps_init(void)
{
	int ret = 0;

	if (ucaps_class_is_registered)
		return ret;

	ret = class_register(&ucaps_class);
	if (ret)
		return ret;

	ret = alloc_chrdev_region(&ucaps_base_dev, 0, RDMA_UCAP_MAX,
				  ucaps_class.name);
	if (ret < 0) {
		class_unregister(&ucaps_class);
		return ret;
	}

	ucaps_class_is_registered = true;

	return 0;
}

static void ucap_dev_release(struct device *device)
{
	struct ib_ucap *ucap = container_of(device, struct ib_ucap, dev);

	kfree(ucap);
}

/**
 * ib_create_ucap - Add a ucap character device
 * @type: UCAP type
 *
 * Creates a ucap character device in the /dev/infiniband directory. By default,
 * the device has root-only read-write access.
 *
 * A driver may call this multiple times with the same UCAP type. A reference
 * count tracks creations and deletions.
 *
 * Return: 0 on success, or a negative errno value on failure
 */
int ib_create_ucap(enum rdma_user_cap type)
{
	struct ib_ucap *ucap;
	int ret;

	if (type >= RDMA_UCAP_MAX)
		return -EINVAL;

	mutex_lock(&ucaps_mutex);
	ret = ib_ucaps_init();
	if (ret)
		goto unlock;

	ucap = ucaps_list[type];
	if (ucap) {
		kref_get(&ucap->ref);
		mutex_unlock(&ucaps_mutex);
		return 0;
	}

	ucap = kzalloc(sizeof(*ucap), GFP_KERNEL);
	if (!ucap) {
		ret = -ENOMEM;
		goto unlock;
	}

	device_initialize(&ucap->dev);
	ucap->dev.class = &ucaps_class;
	ucap->dev.devt = MKDEV(MAJOR(ucaps_base_dev), type);
	ucap->dev.release = ucap_dev_release;
	ret = dev_set_name(&ucap->dev, ucap_names[type]);
	if (ret)
		goto err_device;

	cdev_init(&ucap->cdev, &ucaps_cdev_fops);
	ucap->cdev.owner = THIS_MODULE;

	ret = cdev_device_add(&ucap->cdev, &ucap->dev);
	if (ret)
		goto err_device;

	kref_init(&ucap->ref);
	ucaps_list[type] = ucap;
	mutex_unlock(&ucaps_mutex);

	return 0;

err_device:
	put_device(&ucap->dev);
unlock:
	mutex_unlock(&ucaps_mutex);
	return ret;
}
EXPORT_SYMBOL(ib_create_ucap);

static void ib_release_ucap(struct kref *ref)
{
	struct ib_ucap *ucap = container_of(ref, struct ib_ucap, ref);
	enum rdma_user_cap type;

	for (type = RDMA_UCAP_FIRST; type < RDMA_UCAP_MAX; type++) {
		if (ucaps_list[type] == ucap)
			break;
	}
	WARN_ON(type == RDMA_UCAP_MAX);

	ucaps_list[type] = NULL;
	cdev_device_del(&ucap->cdev, &ucap->dev);
	put_device(&ucap->dev);
}

/**
 * ib_remove_ucap - Remove a ucap character device
 * @type: User cap type
 *
 * Removes the ucap character device according to type. The device is completely
 * removed from the filesystem when its reference count reaches 0.
 */
void ib_remove_ucap(enum rdma_user_cap type)
{
	struct ib_ucap *ucap;

	mutex_lock(&ucaps_mutex);
	ucap = ucaps_list[type];
	if (WARN_ON(!ucap))
		goto end;

	kref_put(&ucap->ref, ib_release_ucap);
end:
	mutex_unlock(&ucaps_mutex);
}
EXPORT_SYMBOL(ib_remove_ucap);

/**
 * ib_get_ucaps - Get bitmask of ucap types from file descriptors
 * @fds: Array of file descriptors
 * @fd_count: Number of file descriptors in the array
 * @idx_mask: Bitmask to be updated based on the ucaps in the fd list
 *
 * Given an array of file descriptors, this function returns a bitmask of
 * the ucaps where a bit is set if an FD for that ucap type was in the array.
 *
 * Return: 0 on success, or a negative errno value on failure
 */
int ib_get_ucaps(int *fds, int fd_count, uint64_t *idx_mask)
{
	int ret = 0;
	dev_t dev;

	*idx_mask = 0;
	mutex_lock(&ucaps_mutex);
	for (int i = 0; i < fd_count; i++) {
		ret = get_devt_from_fd(fds[i], &dev);
		if (ret)
			goto end;

		ret = get_ucap_from_devt(dev, idx_mask);
		if (ret)
			goto end;
	}

end:
	mutex_unlock(&ucaps_mutex);
	return ret;
}
