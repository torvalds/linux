// SPDX-License-Identifier: GPL-2.0
/*
 * Channel path related status regions for vfio_ccw
 *
 * Copyright IBM Corp. 2020
 *
 * Author(s): Farhan Ali <alifm@linux.ibm.com>
 *            Eric Farman <farman@linux.ibm.com>
 */

#include <linux/vfio.h>
#include "vfio_ccw_private.h"

static ssize_t vfio_ccw_schib_region_read(struct vfio_ccw_private *private,
					  char __user *buf, size_t count,
					  loff_t *ppos)
{
	unsigned int i = VFIO_CCW_OFFSET_TO_INDEX(*ppos) - VFIO_CCW_NUM_REGIONS;
	loff_t pos = *ppos & VFIO_CCW_OFFSET_MASK;
	struct ccw_schib_region *region;
	int ret;

	if (pos + count > sizeof(*region))
		return -EINVAL;

	mutex_lock(&private->io_mutex);
	region = private->region[i].data;

	if (cio_update_schib(private->sch)) {
		ret = -ENODEV;
		goto out;
	}

	memcpy(region, &private->sch->schib, sizeof(*region));

	if (copy_to_user(buf, (void *)region + pos, count)) {
		ret = -EFAULT;
		goto out;
	}

	ret = count;

out:
	mutex_unlock(&private->io_mutex);
	return ret;
}

static ssize_t vfio_ccw_schib_region_write(struct vfio_ccw_private *private,
					   const char __user *buf, size_t count,
					   loff_t *ppos)
{
	return -EINVAL;
}


static void vfio_ccw_schib_region_release(struct vfio_ccw_private *private,
					  struct vfio_ccw_region *region)
{

}

static const struct vfio_ccw_regops vfio_ccw_schib_region_ops = {
	.read = vfio_ccw_schib_region_read,
	.write = vfio_ccw_schib_region_write,
	.release = vfio_ccw_schib_region_release,
};

int vfio_ccw_register_schib_dev_regions(struct vfio_ccw_private *private)
{
	return vfio_ccw_register_dev_region(private,
					    VFIO_REGION_SUBTYPE_CCW_SCHIB,
					    &vfio_ccw_schib_region_ops,
					    sizeof(struct ccw_schib_region),
					    VFIO_REGION_INFO_FLAG_READ,
					    private->schib_region);
}

static ssize_t vfio_ccw_crw_region_read(struct vfio_ccw_private *private,
					char __user *buf, size_t count,
					loff_t *ppos)
{
	unsigned int i = VFIO_CCW_OFFSET_TO_INDEX(*ppos) - VFIO_CCW_NUM_REGIONS;
	loff_t pos = *ppos & VFIO_CCW_OFFSET_MASK;
	struct ccw_crw_region *region;
	struct vfio_ccw_crw *crw;
	int ret;

	if (pos + count > sizeof(*region))
		return -EINVAL;

	crw = list_first_entry_or_null(&private->crw,
				       struct vfio_ccw_crw, next);

	if (crw)
		list_del(&crw->next);

	mutex_lock(&private->io_mutex);
	region = private->region[i].data;

	if (crw)
		memcpy(&region->crw, &crw->crw, sizeof(region->crw));

	if (copy_to_user(buf, (void *)region + pos, count))
		ret = -EFAULT;
	else
		ret = count;

	region->crw = 0;

	mutex_unlock(&private->io_mutex);

	kfree(crw);

	/* Notify the guest if more CRWs are on our queue */
	if (!list_empty(&private->crw) && private->crw_trigger)
		eventfd_signal(private->crw_trigger, 1);

	return ret;
}

static ssize_t vfio_ccw_crw_region_write(struct vfio_ccw_private *private,
					 const char __user *buf, size_t count,
					 loff_t *ppos)
{
	return -EINVAL;
}

static void vfio_ccw_crw_region_release(struct vfio_ccw_private *private,
					struct vfio_ccw_region *region)
{

}

static const struct vfio_ccw_regops vfio_ccw_crw_region_ops = {
	.read = vfio_ccw_crw_region_read,
	.write = vfio_ccw_crw_region_write,
	.release = vfio_ccw_crw_region_release,
};

int vfio_ccw_register_crw_dev_regions(struct vfio_ccw_private *private)
{
	return vfio_ccw_register_dev_region(private,
					    VFIO_REGION_SUBTYPE_CCW_CRW,
					    &vfio_ccw_crw_region_ops,
					    sizeof(struct ccw_crw_region),
					    VFIO_REGION_INFO_FLAG_READ,
					    private->crw_region);
}
