/*
 * Physical device callbacks for vfio_ccw
 *
 * Copyright IBM Corp. 2017
 *
 * Author(s): Dong Jia Shi <bjsdjshi@linux.vnet.ibm.com>
 *            Xiao Feng Ren <renxiaof@linux.vnet.ibm.com>
 */

#include <linux/vfio.h>
#include <linux/mdev.h>

#include "vfio_ccw_private.h"

static int vfio_ccw_mdev_notifier(struct notifier_block *nb,
				  unsigned long action,
				  void *data)
{
	struct vfio_ccw_private *private =
		container_of(nb, struct vfio_ccw_private, nb);

	if (!private)
		return NOTIFY_STOP;

	/*
	 * TODO:
	 * Vendor drivers MUST unpin pages in response to an
	 * invalidation.
	 */
	if (action == VFIO_IOMMU_NOTIFY_DMA_UNMAP)
		return NOTIFY_BAD;

	return NOTIFY_DONE;
}

static ssize_t name_show(struct kobject *kobj, struct device *dev, char *buf)
{
	return sprintf(buf, "I/O subchannel (Non-QDIO)\n");
}
MDEV_TYPE_ATTR_RO(name);

static ssize_t device_api_show(struct kobject *kobj, struct device *dev,
			       char *buf)
{
	return sprintf(buf, "%s\n", VFIO_DEVICE_API_CCW_STRING);
}
MDEV_TYPE_ATTR_RO(device_api);

static ssize_t available_instances_show(struct kobject *kobj,
					struct device *dev, char *buf)
{
	struct vfio_ccw_private *private = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", atomic_read(&private->avail));
}
MDEV_TYPE_ATTR_RO(available_instances);

static struct attribute *mdev_types_attrs[] = {
	&mdev_type_attr_name.attr,
	&mdev_type_attr_device_api.attr,
	&mdev_type_attr_available_instances.attr,
	NULL,
};

static struct attribute_group mdev_type_group = {
	.name  = "io",
	.attrs = mdev_types_attrs,
};

struct attribute_group *mdev_type_groups[] = {
	&mdev_type_group,
	NULL,
};

static int vfio_ccw_mdev_create(struct kobject *kobj, struct mdev_device *mdev)
{
	struct vfio_ccw_private *private =
		dev_get_drvdata(mdev_parent_dev(mdev));

	if (atomic_dec_if_positive(&private->avail) < 0)
		return -EPERM;

	private->mdev = mdev;

	return 0;
}

static int vfio_ccw_mdev_remove(struct mdev_device *mdev)
{
	struct vfio_ccw_private *private;
	struct subchannel *sch;
	int ret;

	private = dev_get_drvdata(mdev_parent_dev(mdev));
	sch = private->sch;
	ret = vfio_ccw_sch_quiesce(sch);
	if (ret)
		return ret;
	ret = cio_enable_subchannel(sch, (u32)(unsigned long)sch);
	if (ret)
		return ret;

	private->mdev = NULL;
	atomic_inc(&private->avail);

	return 0;
}

static int vfio_ccw_mdev_open(struct mdev_device *mdev)
{
	struct vfio_ccw_private *private =
		dev_get_drvdata(mdev_parent_dev(mdev));
	unsigned long events = VFIO_IOMMU_NOTIFY_DMA_UNMAP;

	private->nb.notifier_call = vfio_ccw_mdev_notifier;

	return vfio_register_notifier(mdev_dev(mdev), VFIO_IOMMU_NOTIFY,
				      &events, &private->nb);
}

void vfio_ccw_mdev_release(struct mdev_device *mdev)
{
	struct vfio_ccw_private *private =
		dev_get_drvdata(mdev_parent_dev(mdev));

	vfio_unregister_notifier(mdev_dev(mdev), VFIO_IOMMU_NOTIFY,
				 &private->nb);
}

static const struct mdev_parent_ops vfio_ccw_mdev_ops = {
	.owner			= THIS_MODULE,
	.supported_type_groups  = mdev_type_groups,
	.create			= vfio_ccw_mdev_create,
	.remove			= vfio_ccw_mdev_remove,
	.open			= vfio_ccw_mdev_open,
	.release		= vfio_ccw_mdev_release,
};

int vfio_ccw_mdev_reg(struct subchannel *sch)
{
	return mdev_register_device(&sch->dev, &vfio_ccw_mdev_ops);
}

void vfio_ccw_mdev_unreg(struct subchannel *sch)
{
	mdev_unregister_device(&sch->dev);
}
