// SPDX-License-Identifier: GPL-2.0
/*
 * Physical device callbacks for vfio_ccw
 *
 * Copyright IBM Corp. 2017
 * Copyright Red Hat, Inc. 2019
 *
 * Author(s): Dong Jia Shi <bjsdjshi@linux.vnet.ibm.com>
 *            Xiao Feng Ren <renxiaof@linux.vnet.ibm.com>
 *            Cornelia Huck <cohuck@redhat.com>
 */

#include <linux/vfio.h>
#include <linux/mdev.h>
#include <linux/nospec.h>
#include <linux/slab.h>

#include "vfio_ccw_private.h"

static int vfio_ccw_mdev_reset(struct mdev_device *mdev)
{
	struct vfio_ccw_private *private;
	struct subchannel *sch;
	int ret;

	private = dev_get_drvdata(mdev_parent_dev(mdev));
	sch = private->sch;
	/*
	 * TODO:
	 * In the cureent stage, some things like "no I/O running" and "no
	 * interrupt pending" are clear, but we are not sure what other state
	 * we need to care about.
	 * There are still a lot more instructions need to be handled. We
	 * should come back here later.
	 */
	ret = vfio_ccw_sch_quiesce(sch);
	if (ret)
		return ret;

	ret = cio_enable_subchannel(sch, (u32)(unsigned long)sch);
	if (!ret)
		private->state = VFIO_CCW_STATE_IDLE;

	return ret;
}

static int vfio_ccw_mdev_notifier(struct notifier_block *nb,
				  unsigned long action,
				  void *data)
{
	struct vfio_ccw_private *private =
		container_of(nb, struct vfio_ccw_private, nb);

	/*
	 * Vendor drivers MUST unpin pages in response to an
	 * invalidation.
	 */
	if (action == VFIO_IOMMU_NOTIFY_DMA_UNMAP) {
		struct vfio_iommu_type1_dma_unmap *unmap = data;

		if (!cp_iova_pinned(&private->cp, unmap->iova))
			return NOTIFY_OK;

		if (vfio_ccw_mdev_reset(private->mdev))
			return NOTIFY_BAD;

		cp_free(&private->cp);
		return NOTIFY_OK;
	}

	return NOTIFY_DONE;
}

static ssize_t name_show(struct mdev_type *mtype,
			 struct mdev_type_attribute *attr, char *buf)
{
	return sprintf(buf, "I/O subchannel (Non-QDIO)\n");
}
static MDEV_TYPE_ATTR_RO(name);

static ssize_t device_api_show(struct mdev_type *mtype,
			       struct mdev_type_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", VFIO_DEVICE_API_CCW_STRING);
}
static MDEV_TYPE_ATTR_RO(device_api);

static ssize_t available_instances_show(struct mdev_type *mtype,
					struct mdev_type_attribute *attr,
					char *buf)
{
	struct vfio_ccw_private *private =
		dev_get_drvdata(mtype_get_parent_dev(mtype));

	return sprintf(buf, "%d\n", atomic_read(&private->avail));
}
static MDEV_TYPE_ATTR_RO(available_instances);

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

static struct attribute_group *mdev_type_groups[] = {
	&mdev_type_group,
	NULL,
};

static int vfio_ccw_mdev_create(struct mdev_device *mdev)
{
	struct vfio_ccw_private *private =
		dev_get_drvdata(mdev_parent_dev(mdev));

	if (private->state == VFIO_CCW_STATE_NOT_OPER)
		return -ENODEV;

	if (atomic_dec_if_positive(&private->avail) < 0)
		return -EPERM;

	private->mdev = mdev;
	private->state = VFIO_CCW_STATE_IDLE;

	VFIO_CCW_MSG_EVENT(2, "mdev %pUl, sch %x.%x.%04x: create\n",
			   mdev_uuid(mdev), private->sch->schid.cssid,
			   private->sch->schid.ssid,
			   private->sch->schid.sch_no);

	return 0;
}

static int vfio_ccw_mdev_remove(struct mdev_device *mdev)
{
	struct vfio_ccw_private *private =
		dev_get_drvdata(mdev_parent_dev(mdev));

	VFIO_CCW_MSG_EVENT(2, "mdev %pUl, sch %x.%x.%04x: remove\n",
			   mdev_uuid(mdev), private->sch->schid.cssid,
			   private->sch->schid.ssid,
			   private->sch->schid.sch_no);

	if ((private->state != VFIO_CCW_STATE_NOT_OPER) &&
	    (private->state != VFIO_CCW_STATE_STANDBY)) {
		if (!vfio_ccw_sch_quiesce(private->sch))
			private->state = VFIO_CCW_STATE_STANDBY;
		/* The state will be NOT_OPER on error. */
	}

	cp_free(&private->cp);
	private->mdev = NULL;
	atomic_inc(&private->avail);

	return 0;
}

static int vfio_ccw_mdev_open(struct mdev_device *mdev)
{
	struct vfio_ccw_private *private =
		dev_get_drvdata(mdev_parent_dev(mdev));
	unsigned long events = VFIO_IOMMU_NOTIFY_DMA_UNMAP;
	int ret;

	private->nb.notifier_call = vfio_ccw_mdev_notifier;

	ret = vfio_register_notifier(mdev_dev(mdev), VFIO_IOMMU_NOTIFY,
				     &events, &private->nb);
	if (ret)
		return ret;

	ret = vfio_ccw_register_async_dev_regions(private);
	if (ret)
		goto out_unregister;

	ret = vfio_ccw_register_schib_dev_regions(private);
	if (ret)
		goto out_unregister;

	ret = vfio_ccw_register_crw_dev_regions(private);
	if (ret)
		goto out_unregister;

	return ret;

out_unregister:
	vfio_ccw_unregister_dev_regions(private);
	vfio_unregister_notifier(mdev_dev(mdev), VFIO_IOMMU_NOTIFY,
				 &private->nb);
	return ret;
}

static void vfio_ccw_mdev_release(struct mdev_device *mdev)
{
	struct vfio_ccw_private *private =
		dev_get_drvdata(mdev_parent_dev(mdev));

	if ((private->state != VFIO_CCW_STATE_NOT_OPER) &&
	    (private->state != VFIO_CCW_STATE_STANDBY)) {
		if (!vfio_ccw_mdev_reset(mdev))
			private->state = VFIO_CCW_STATE_STANDBY;
		/* The state will be NOT_OPER on error. */
	}

	cp_free(&private->cp);
	vfio_ccw_unregister_dev_regions(private);
	vfio_unregister_notifier(mdev_dev(mdev), VFIO_IOMMU_NOTIFY,
				 &private->nb);
}

static ssize_t vfio_ccw_mdev_read_io_region(struct vfio_ccw_private *private,
					    char __user *buf, size_t count,
					    loff_t *ppos)
{
	loff_t pos = *ppos & VFIO_CCW_OFFSET_MASK;
	struct ccw_io_region *region;
	int ret;

	if (pos + count > sizeof(*region))
		return -EINVAL;

	mutex_lock(&private->io_mutex);
	region = private->io_region;
	if (copy_to_user(buf, (void *)region + pos, count))
		ret = -EFAULT;
	else
		ret = count;
	mutex_unlock(&private->io_mutex);
	return ret;
}

static ssize_t vfio_ccw_mdev_read(struct mdev_device *mdev,
				  char __user *buf,
				  size_t count,
				  loff_t *ppos)
{
	unsigned int index = VFIO_CCW_OFFSET_TO_INDEX(*ppos);
	struct vfio_ccw_private *private;

	private = dev_get_drvdata(mdev_parent_dev(mdev));

	if (index >= VFIO_CCW_NUM_REGIONS + private->num_regions)
		return -EINVAL;

	switch (index) {
	case VFIO_CCW_CONFIG_REGION_INDEX:
		return vfio_ccw_mdev_read_io_region(private, buf, count, ppos);
	default:
		index -= VFIO_CCW_NUM_REGIONS;
		return private->region[index].ops->read(private, buf, count,
							ppos);
	}

	return -EINVAL;
}

static ssize_t vfio_ccw_mdev_write_io_region(struct vfio_ccw_private *private,
					     const char __user *buf,
					     size_t count, loff_t *ppos)
{
	loff_t pos = *ppos & VFIO_CCW_OFFSET_MASK;
	struct ccw_io_region *region;
	int ret;

	if (pos + count > sizeof(*region))
		return -EINVAL;

	if (!mutex_trylock(&private->io_mutex))
		return -EAGAIN;

	region = private->io_region;
	if (copy_from_user((void *)region + pos, buf, count)) {
		ret = -EFAULT;
		goto out_unlock;
	}

	vfio_ccw_fsm_event(private, VFIO_CCW_EVENT_IO_REQ);
	if (region->ret_code != 0)
		private->state = VFIO_CCW_STATE_IDLE;
	ret = (region->ret_code != 0) ? region->ret_code : count;

out_unlock:
	mutex_unlock(&private->io_mutex);
	return ret;
}

static ssize_t vfio_ccw_mdev_write(struct mdev_device *mdev,
				   const char __user *buf,
				   size_t count,
				   loff_t *ppos)
{
	unsigned int index = VFIO_CCW_OFFSET_TO_INDEX(*ppos);
	struct vfio_ccw_private *private;

	private = dev_get_drvdata(mdev_parent_dev(mdev));

	if (index >= VFIO_CCW_NUM_REGIONS + private->num_regions)
		return -EINVAL;

	switch (index) {
	case VFIO_CCW_CONFIG_REGION_INDEX:
		return vfio_ccw_mdev_write_io_region(private, buf, count, ppos);
	default:
		index -= VFIO_CCW_NUM_REGIONS;
		return private->region[index].ops->write(private, buf, count,
							 ppos);
	}

	return -EINVAL;
}

static int vfio_ccw_mdev_get_device_info(struct vfio_device_info *info,
					 struct mdev_device *mdev)
{
	struct vfio_ccw_private *private;

	private = dev_get_drvdata(mdev_parent_dev(mdev));
	info->flags = VFIO_DEVICE_FLAGS_CCW | VFIO_DEVICE_FLAGS_RESET;
	info->num_regions = VFIO_CCW_NUM_REGIONS + private->num_regions;
	info->num_irqs = VFIO_CCW_NUM_IRQS;

	return 0;
}

static int vfio_ccw_mdev_get_region_info(struct vfio_region_info *info,
					 struct mdev_device *mdev,
					 unsigned long arg)
{
	struct vfio_ccw_private *private;
	int i;

	private = dev_get_drvdata(mdev_parent_dev(mdev));
	switch (info->index) {
	case VFIO_CCW_CONFIG_REGION_INDEX:
		info->offset = 0;
		info->size = sizeof(struct ccw_io_region);
		info->flags = VFIO_REGION_INFO_FLAG_READ
			      | VFIO_REGION_INFO_FLAG_WRITE;
		return 0;
	default: /* all other regions are handled via capability chain */
	{
		struct vfio_info_cap caps = { .buf = NULL, .size = 0 };
		struct vfio_region_info_cap_type cap_type = {
			.header.id = VFIO_REGION_INFO_CAP_TYPE,
			.header.version = 1 };
		int ret;

		if (info->index >=
		    VFIO_CCW_NUM_REGIONS + private->num_regions)
			return -EINVAL;

		info->index = array_index_nospec(info->index,
						 VFIO_CCW_NUM_REGIONS +
						 private->num_regions);

		i = info->index - VFIO_CCW_NUM_REGIONS;

		info->offset = VFIO_CCW_INDEX_TO_OFFSET(info->index);
		info->size = private->region[i].size;
		info->flags = private->region[i].flags;

		cap_type.type = private->region[i].type;
		cap_type.subtype = private->region[i].subtype;

		ret = vfio_info_add_capability(&caps, &cap_type.header,
					       sizeof(cap_type));
		if (ret)
			return ret;

		info->flags |= VFIO_REGION_INFO_FLAG_CAPS;
		if (info->argsz < sizeof(*info) + caps.size) {
			info->argsz = sizeof(*info) + caps.size;
			info->cap_offset = 0;
		} else {
			vfio_info_cap_shift(&caps, sizeof(*info));
			if (copy_to_user((void __user *)arg + sizeof(*info),
					 caps.buf, caps.size)) {
				kfree(caps.buf);
				return -EFAULT;
			}
			info->cap_offset = sizeof(*info);
		}

		kfree(caps.buf);

	}
	}
	return 0;
}

static int vfio_ccw_mdev_get_irq_info(struct vfio_irq_info *info)
{
	switch (info->index) {
	case VFIO_CCW_IO_IRQ_INDEX:
	case VFIO_CCW_CRW_IRQ_INDEX:
	case VFIO_CCW_REQ_IRQ_INDEX:
		info->count = 1;
		info->flags = VFIO_IRQ_INFO_EVENTFD;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int vfio_ccw_mdev_set_irqs(struct mdev_device *mdev,
				  uint32_t flags,
				  uint32_t index,
				  void __user *data)
{
	struct vfio_ccw_private *private;
	struct eventfd_ctx **ctx;

	if (!(flags & VFIO_IRQ_SET_ACTION_TRIGGER))
		return -EINVAL;

	private = dev_get_drvdata(mdev_parent_dev(mdev));

	switch (index) {
	case VFIO_CCW_IO_IRQ_INDEX:
		ctx = &private->io_trigger;
		break;
	case VFIO_CCW_CRW_IRQ_INDEX:
		ctx = &private->crw_trigger;
		break;
	case VFIO_CCW_REQ_IRQ_INDEX:
		ctx = &private->req_trigger;
		break;
	default:
		return -EINVAL;
	}

	switch (flags & VFIO_IRQ_SET_DATA_TYPE_MASK) {
	case VFIO_IRQ_SET_DATA_NONE:
	{
		if (*ctx)
			eventfd_signal(*ctx, 1);
		return 0;
	}
	case VFIO_IRQ_SET_DATA_BOOL:
	{
		uint8_t trigger;

		if (get_user(trigger, (uint8_t __user *)data))
			return -EFAULT;

		if (trigger && *ctx)
			eventfd_signal(*ctx, 1);
		return 0;
	}
	case VFIO_IRQ_SET_DATA_EVENTFD:
	{
		int32_t fd;

		if (get_user(fd, (int32_t __user *)data))
			return -EFAULT;

		if (fd == -1) {
			if (*ctx)
				eventfd_ctx_put(*ctx);
			*ctx = NULL;
		} else if (fd >= 0) {
			struct eventfd_ctx *efdctx;

			efdctx = eventfd_ctx_fdget(fd);
			if (IS_ERR(efdctx))
				return PTR_ERR(efdctx);

			if (*ctx)
				eventfd_ctx_put(*ctx);

			*ctx = efdctx;
		} else
			return -EINVAL;

		return 0;
	}
	default:
		return -EINVAL;
	}
}

int vfio_ccw_register_dev_region(struct vfio_ccw_private *private,
				 unsigned int subtype,
				 const struct vfio_ccw_regops *ops,
				 size_t size, u32 flags, void *data)
{
	struct vfio_ccw_region *region;

	region = krealloc(private->region,
			  (private->num_regions + 1) * sizeof(*region),
			  GFP_KERNEL);
	if (!region)
		return -ENOMEM;

	private->region = region;
	private->region[private->num_regions].type = VFIO_REGION_TYPE_CCW;
	private->region[private->num_regions].subtype = subtype;
	private->region[private->num_regions].ops = ops;
	private->region[private->num_regions].size = size;
	private->region[private->num_regions].flags = flags;
	private->region[private->num_regions].data = data;

	private->num_regions++;

	return 0;
}

void vfio_ccw_unregister_dev_regions(struct vfio_ccw_private *private)
{
	int i;

	for (i = 0; i < private->num_regions; i++)
		private->region[i].ops->release(private, &private->region[i]);
	private->num_regions = 0;
	kfree(private->region);
	private->region = NULL;
}

static ssize_t vfio_ccw_mdev_ioctl(struct mdev_device *mdev,
				   unsigned int cmd,
				   unsigned long arg)
{
	int ret = 0;
	unsigned long minsz;

	switch (cmd) {
	case VFIO_DEVICE_GET_INFO:
	{
		struct vfio_device_info info;

		minsz = offsetofend(struct vfio_device_info, num_irqs);

		if (copy_from_user(&info, (void __user *)arg, minsz))
			return -EFAULT;

		if (info.argsz < minsz)
			return -EINVAL;

		ret = vfio_ccw_mdev_get_device_info(&info, mdev);
		if (ret)
			return ret;

		return copy_to_user((void __user *)arg, &info, minsz) ? -EFAULT : 0;
	}
	case VFIO_DEVICE_GET_REGION_INFO:
	{
		struct vfio_region_info info;

		minsz = offsetofend(struct vfio_region_info, offset);

		if (copy_from_user(&info, (void __user *)arg, minsz))
			return -EFAULT;

		if (info.argsz < minsz)
			return -EINVAL;

		ret = vfio_ccw_mdev_get_region_info(&info, mdev, arg);
		if (ret)
			return ret;

		return copy_to_user((void __user *)arg, &info, minsz) ? -EFAULT : 0;
	}
	case VFIO_DEVICE_GET_IRQ_INFO:
	{
		struct vfio_irq_info info;

		minsz = offsetofend(struct vfio_irq_info, count);

		if (copy_from_user(&info, (void __user *)arg, minsz))
			return -EFAULT;

		if (info.argsz < minsz || info.index >= VFIO_CCW_NUM_IRQS)
			return -EINVAL;

		ret = vfio_ccw_mdev_get_irq_info(&info);
		if (ret)
			return ret;

		if (info.count == -1)
			return -EINVAL;

		return copy_to_user((void __user *)arg, &info, minsz) ? -EFAULT : 0;
	}
	case VFIO_DEVICE_SET_IRQS:
	{
		struct vfio_irq_set hdr;
		size_t data_size;
		void __user *data;

		minsz = offsetofend(struct vfio_irq_set, count);

		if (copy_from_user(&hdr, (void __user *)arg, minsz))
			return -EFAULT;

		ret = vfio_set_irqs_validate_and_prepare(&hdr, 1,
							 VFIO_CCW_NUM_IRQS,
							 &data_size);
		if (ret)
			return ret;

		data = (void __user *)(arg + minsz);
		return vfio_ccw_mdev_set_irqs(mdev, hdr.flags, hdr.index, data);
	}
	case VFIO_DEVICE_RESET:
		return vfio_ccw_mdev_reset(mdev);
	default:
		return -ENOTTY;
	}
}

/* Request removal of the device*/
static void vfio_ccw_mdev_request(struct mdev_device *mdev, unsigned int count)
{
	struct vfio_ccw_private *private = dev_get_drvdata(mdev_parent_dev(mdev));

	if (!private)
		return;

	if (private->req_trigger) {
		if (!(count % 10))
			dev_notice_ratelimited(mdev_dev(private->mdev),
					       "Relaying device request to user (#%u)\n",
					       count);

		eventfd_signal(private->req_trigger, 1);
	} else if (count == 0) {
		dev_notice(mdev_dev(private->mdev),
			   "No device request channel registered, blocked until released by user\n");
	}
}

static const struct mdev_parent_ops vfio_ccw_mdev_ops = {
	.owner			= THIS_MODULE,
	.supported_type_groups  = mdev_type_groups,
	.create			= vfio_ccw_mdev_create,
	.remove			= vfio_ccw_mdev_remove,
	.open			= vfio_ccw_mdev_open,
	.release		= vfio_ccw_mdev_release,
	.read			= vfio_ccw_mdev_read,
	.write			= vfio_ccw_mdev_write,
	.ioctl			= vfio_ccw_mdev_ioctl,
	.request		= vfio_ccw_mdev_request,
};

int vfio_ccw_mdev_reg(struct subchannel *sch)
{
	return mdev_register_device(&sch->dev, &vfio_ccw_mdev_ops);
}

void vfio_ccw_mdev_unreg(struct subchannel *sch)
{
	mdev_unregister_device(&sch->dev);
}
