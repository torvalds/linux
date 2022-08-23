// SPDX-License-Identifier: GPL-2.0
/*
 * Async I/O region for vfio_ccw
 *
 * Copyright Red Hat, Inc. 2019
 *
 * Author(s): Cornelia Huck <cohuck@redhat.com>
 */

#include <linux/vfio.h>

#include "vfio_ccw_private.h"

static ssize_t vfio_ccw_async_region_read(struct vfio_ccw_private *private,
					  char __user *buf, size_t count,
					  loff_t *ppos)
{
	unsigned int i = VFIO_CCW_OFFSET_TO_INDEX(*ppos) - VFIO_CCW_NUM_REGIONS;
	loff_t pos = *ppos & VFIO_CCW_OFFSET_MASK;
	struct ccw_cmd_region *region;
	int ret;

	if (pos + count > sizeof(*region))
		return -EINVAL;

	mutex_lock(&private->io_mutex);
	region = private->region[i].data;
	if (copy_to_user(buf, (void *)region + pos, count))
		ret = -EFAULT;
	else
		ret = count;
	mutex_unlock(&private->io_mutex);
	return ret;
}

static ssize_t vfio_ccw_async_region_write(struct vfio_ccw_private *private,
					   const char __user *buf, size_t count,
					   loff_t *ppos)
{
	unsigned int i = VFIO_CCW_OFFSET_TO_INDEX(*ppos) - VFIO_CCW_NUM_REGIONS;
	loff_t pos = *ppos & VFIO_CCW_OFFSET_MASK;
	struct ccw_cmd_region *region;
	int ret;

	if (pos + count > sizeof(*region))
		return -EINVAL;

	if (!mutex_trylock(&private->io_mutex))
		return -EAGAIN;

	region = private->region[i].data;
	if (copy_from_user((void *)region + pos, buf, count)) {
		ret = -EFAULT;
		goto out_unlock;
	}

	vfio_ccw_fsm_event(private, VFIO_CCW_EVENT_ASYNC_REQ);

	ret = region->ret_code ? region->ret_code : count;

out_unlock:
	mutex_unlock(&private->io_mutex);
	return ret;
}

static void vfio_ccw_async_region_release(struct vfio_ccw_private *private,
					  struct vfio_ccw_region *region)
{

}

static const struct vfio_ccw_regops vfio_ccw_async_region_ops = {
	.read = vfio_ccw_async_region_read,
	.write = vfio_ccw_async_region_write,
	.release = vfio_ccw_async_region_release,
};

int vfio_ccw_register_async_dev_regions(struct vfio_ccw_private *private)
{
	return vfio_ccw_register_dev_region(private,
					    VFIO_REGION_SUBTYPE_CCW_ASYNC_CMD,
					    &vfio_ccw_async_region_ops,
					    sizeof(struct ccw_cmd_region),
					    VFIO_REGION_INFO_FLAG_READ |
					    VFIO_REGION_INFO_FLAG_WRITE,
					    private->cmd_region);
}
