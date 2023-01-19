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
#include <linux/nospec.h>
#include <linux/slab.h>

#include "vfio_ccw_private.h"

static const struct vfio_device_ops vfio_ccw_dev_ops;

static int vfio_ccw_mdev_reset(struct vfio_ccw_private *private)
{
	/*
	 * If the FSM state is seen as Not Operational after closing
	 * and re-opening the mdev, return an error.
	 */
	vfio_ccw_fsm_event(private, VFIO_CCW_EVENT_CLOSE);
	vfio_ccw_fsm_event(private, VFIO_CCW_EVENT_OPEN);
	if (private->state == VFIO_CCW_STATE_NOT_OPER)
		return -EINVAL;

	return 0;
}

static void vfio_ccw_dma_unmap(struct vfio_device *vdev, u64 iova, u64 length)
{
	struct vfio_ccw_private *private =
		container_of(vdev, struct vfio_ccw_private, vdev);

	/* Drivers MUST unpin pages in response to an invalidation. */
	if (!cp_iova_pinned(&private->cp, iova, length))
		return;

	vfio_ccw_mdev_reset(private);
}

static int vfio_ccw_mdev_init_dev(struct vfio_device *vdev)
{
	struct vfio_ccw_private *private =
		container_of(vdev, struct vfio_ccw_private, vdev);

	mutex_init(&private->io_mutex);
	private->state = VFIO_CCW_STATE_STANDBY;
	INIT_LIST_HEAD(&private->crw);
	INIT_WORK(&private->io_work, vfio_ccw_sch_io_todo);
	INIT_WORK(&private->crw_work, vfio_ccw_crw_todo);

	private->cp.guest_cp = kcalloc(CCWCHAIN_LEN_MAX, sizeof(struct ccw1),
				       GFP_KERNEL);
	if (!private->cp.guest_cp)
		goto out_free_private;

	private->io_region = kmem_cache_zalloc(vfio_ccw_io_region,
					       GFP_KERNEL | GFP_DMA);
	if (!private->io_region)
		goto out_free_cp;

	private->cmd_region = kmem_cache_zalloc(vfio_ccw_cmd_region,
						GFP_KERNEL | GFP_DMA);
	if (!private->cmd_region)
		goto out_free_io;

	private->schib_region = kmem_cache_zalloc(vfio_ccw_schib_region,
						  GFP_KERNEL | GFP_DMA);
	if (!private->schib_region)
		goto out_free_cmd;

	private->crw_region = kmem_cache_zalloc(vfio_ccw_crw_region,
						GFP_KERNEL | GFP_DMA);
	if (!private->crw_region)
		goto out_free_schib;

	return 0;

out_free_schib:
	kmem_cache_free(vfio_ccw_schib_region, private->schib_region);
out_free_cmd:
	kmem_cache_free(vfio_ccw_cmd_region, private->cmd_region);
out_free_io:
	kmem_cache_free(vfio_ccw_io_region, private->io_region);
out_free_cp:
	kfree(private->cp.guest_cp);
out_free_private:
	mutex_destroy(&private->io_mutex);
	return -ENOMEM;
}

static int vfio_ccw_mdev_probe(struct mdev_device *mdev)
{
	struct subchannel *sch = to_subchannel(mdev->dev.parent);
	struct vfio_ccw_parent *parent = dev_get_drvdata(&sch->dev);
	struct vfio_ccw_private *private;
	int ret;

	private = vfio_alloc_device(vfio_ccw_private, vdev, &mdev->dev,
				    &vfio_ccw_dev_ops);
	if (IS_ERR(private))
		return PTR_ERR(private);

	dev_set_drvdata(&parent->dev, private);

	VFIO_CCW_MSG_EVENT(2, "sch %x.%x.%04x: create\n",
			   sch->schid.cssid,
			   sch->schid.ssid,
			   sch->schid.sch_no);

	ret = vfio_register_emulated_iommu_dev(&private->vdev);
	if (ret)
		goto err_put_vdev;
	dev_set_drvdata(&mdev->dev, private);
	return 0;

err_put_vdev:
	dev_set_drvdata(&parent->dev, NULL);
	vfio_put_device(&private->vdev);
	return ret;
}

static void vfio_ccw_mdev_release_dev(struct vfio_device *vdev)
{
	struct vfio_ccw_private *private =
		container_of(vdev, struct vfio_ccw_private, vdev);
	struct vfio_ccw_crw *crw, *temp;

	list_for_each_entry_safe(crw, temp, &private->crw, next) {
		list_del(&crw->next);
		kfree(crw);
	}

	kmem_cache_free(vfio_ccw_crw_region, private->crw_region);
	kmem_cache_free(vfio_ccw_schib_region, private->schib_region);
	kmem_cache_free(vfio_ccw_cmd_region, private->cmd_region);
	kmem_cache_free(vfio_ccw_io_region, private->io_region);
	kfree(private->cp.guest_cp);
	mutex_destroy(&private->io_mutex);
}

static void vfio_ccw_mdev_remove(struct mdev_device *mdev)
{
	struct subchannel *sch = to_subchannel(mdev->dev.parent);
	struct vfio_ccw_parent *parent = dev_get_drvdata(&sch->dev);
	struct vfio_ccw_private *private = dev_get_drvdata(&parent->dev);

	VFIO_CCW_MSG_EVENT(2, "sch %x.%x.%04x: remove\n",
			   sch->schid.cssid,
			   sch->schid.ssid,
			   sch->schid.sch_no);

	vfio_unregister_group_dev(&private->vdev);

	dev_set_drvdata(&parent->dev, NULL);
	vfio_put_device(&private->vdev);
}

static int vfio_ccw_mdev_open_device(struct vfio_device *vdev)
{
	struct vfio_ccw_private *private =
		container_of(vdev, struct vfio_ccw_private, vdev);
	int ret;

	/* Device cannot simply be opened again from this state */
	if (private->state == VFIO_CCW_STATE_NOT_OPER)
		return -EINVAL;

	ret = vfio_ccw_register_async_dev_regions(private);
	if (ret)
		return ret;

	ret = vfio_ccw_register_schib_dev_regions(private);
	if (ret)
		goto out_unregister;

	ret = vfio_ccw_register_crw_dev_regions(private);
	if (ret)
		goto out_unregister;

	vfio_ccw_fsm_event(private, VFIO_CCW_EVENT_OPEN);
	if (private->state == VFIO_CCW_STATE_NOT_OPER) {
		ret = -EINVAL;
		goto out_unregister;
	}

	return ret;

out_unregister:
	vfio_ccw_unregister_dev_regions(private);
	return ret;
}

static void vfio_ccw_mdev_close_device(struct vfio_device *vdev)
{
	struct vfio_ccw_private *private =
		container_of(vdev, struct vfio_ccw_private, vdev);

	vfio_ccw_fsm_event(private, VFIO_CCW_EVENT_CLOSE);
	vfio_ccw_unregister_dev_regions(private);
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

static ssize_t vfio_ccw_mdev_read(struct vfio_device *vdev,
				  char __user *buf,
				  size_t count,
				  loff_t *ppos)
{
	struct vfio_ccw_private *private =
		container_of(vdev, struct vfio_ccw_private, vdev);
	unsigned int index = VFIO_CCW_OFFSET_TO_INDEX(*ppos);

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
	ret = (region->ret_code != 0) ? region->ret_code : count;

out_unlock:
	mutex_unlock(&private->io_mutex);
	return ret;
}

static ssize_t vfio_ccw_mdev_write(struct vfio_device *vdev,
				   const char __user *buf,
				   size_t count,
				   loff_t *ppos)
{
	struct vfio_ccw_private *private =
		container_of(vdev, struct vfio_ccw_private, vdev);
	unsigned int index = VFIO_CCW_OFFSET_TO_INDEX(*ppos);

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

static int vfio_ccw_mdev_get_device_info(struct vfio_ccw_private *private,
					 struct vfio_device_info *info)
{
	info->flags = VFIO_DEVICE_FLAGS_CCW | VFIO_DEVICE_FLAGS_RESET;
	info->num_regions = VFIO_CCW_NUM_REGIONS + private->num_regions;
	info->num_irqs = VFIO_CCW_NUM_IRQS;

	return 0;
}

static int vfio_ccw_mdev_get_region_info(struct vfio_ccw_private *private,
					 struct vfio_region_info *info,
					 unsigned long arg)
{
	int i;

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

static int vfio_ccw_mdev_set_irqs(struct vfio_ccw_private *private,
				  uint32_t flags,
				  uint32_t index,
				  void __user *data)
{
	struct eventfd_ctx **ctx;

	if (!(flags & VFIO_IRQ_SET_ACTION_TRIGGER))
		return -EINVAL;

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

static ssize_t vfio_ccw_mdev_ioctl(struct vfio_device *vdev,
				   unsigned int cmd,
				   unsigned long arg)
{
	struct vfio_ccw_private *private =
		container_of(vdev, struct vfio_ccw_private, vdev);
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

		ret = vfio_ccw_mdev_get_device_info(private, &info);
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

		ret = vfio_ccw_mdev_get_region_info(private, &info, arg);
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
		return vfio_ccw_mdev_set_irqs(private, hdr.flags, hdr.index,
					      data);
	}
	case VFIO_DEVICE_RESET:
		return vfio_ccw_mdev_reset(private);
	default:
		return -ENOTTY;
	}
}

/* Request removal of the device*/
static void vfio_ccw_mdev_request(struct vfio_device *vdev, unsigned int count)
{
	struct vfio_ccw_private *private =
		container_of(vdev, struct vfio_ccw_private, vdev);
	struct device *dev = vdev->dev;

	if (private->req_trigger) {
		if (!(count % 10))
			dev_notice_ratelimited(dev,
					       "Relaying device request to user (#%u)\n",
					       count);

		eventfd_signal(private->req_trigger, 1);
	} else if (count == 0) {
		dev_notice(dev,
			   "No device request channel registered, blocked until released by user\n");
	}
}

static const struct vfio_device_ops vfio_ccw_dev_ops = {
	.init = vfio_ccw_mdev_init_dev,
	.release = vfio_ccw_mdev_release_dev,
	.open_device = vfio_ccw_mdev_open_device,
	.close_device = vfio_ccw_mdev_close_device,
	.read = vfio_ccw_mdev_read,
	.write = vfio_ccw_mdev_write,
	.ioctl = vfio_ccw_mdev_ioctl,
	.request = vfio_ccw_mdev_request,
	.dma_unmap = vfio_ccw_dma_unmap,
	.bind_iommufd = vfio_iommufd_emulated_bind,
	.unbind_iommufd = vfio_iommufd_emulated_unbind,
	.attach_ioas = vfio_iommufd_emulated_attach_ioas,
};

struct mdev_driver vfio_ccw_mdev_driver = {
	.device_api = VFIO_DEVICE_API_CCW_STRING,
	.max_instances = 1,
	.driver = {
		.name = "vfio_ccw_mdev",
		.owner = THIS_MODULE,
		.mod_name = KBUILD_MODNAME,
	},
	.probe = vfio_ccw_mdev_probe,
	.remove = vfio_ccw_mdev_remove,
};
