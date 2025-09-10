// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Virtio PCI driver - modern (virtio 1.0) device support
 *
 * This module allows virtio devices to be used over a virtual PCI device.
 * This can be used with QEMU based VMMs like KVM or Xen.
 *
 * Copyright IBM Corp. 2007
 * Copyright Red Hat, Inc. 2014
 *
 * Authors:
 *  Anthony Liguori  <aliguori@us.ibm.com>
 *  Rusty Russell <rusty@rustcorp.com.au>
 *  Michael S. Tsirkin <mst@redhat.com>
 */

#include <linux/delay.h>
#include <linux/virtio_pci_admin.h>
#define VIRTIO_PCI_NO_LEGACY
#define VIRTIO_RING_NO_LEGACY
#include "virtio_pci_common.h"

#define VIRTIO_AVQ_SGS_MAX	4

static void vp_get_features(struct virtio_device *vdev, u64 *features)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);

	vp_modern_get_extended_features(&vp_dev->mdev, features);
}

static int vp_avq_index(struct virtio_device *vdev, u16 *index, u16 *num)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);

	*num = 0;
	if (!virtio_has_feature(vdev, VIRTIO_F_ADMIN_VQ))
		return 0;

	*num = vp_modern_avq_num(&vp_dev->mdev);
	if (!(*num))
		return -EINVAL;
	*index = vp_modern_avq_index(&vp_dev->mdev);
	return 0;
}

void vp_modern_avq_done(struct virtqueue *vq)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vq->vdev);
	struct virtio_pci_admin_vq *admin_vq = &vp_dev->admin_vq;
	unsigned int status_size = sizeof(struct virtio_admin_cmd_status);
	struct virtio_admin_cmd *cmd;
	unsigned long flags;
	unsigned int len;

	spin_lock_irqsave(&admin_vq->lock, flags);
	do {
		virtqueue_disable_cb(vq);
		while ((cmd = virtqueue_get_buf(vq, &len))) {
			/* If the number of bytes written by the device is less
			 * than the size of struct virtio_admin_cmd_status, the
			 * remaining status bytes will remain zero-initialized,
			 * since the buffer was zeroed during allocation.
			 * In this case, set the size of command_specific_result
			 * to 0.
			 */
			if (len < status_size)
				cmd->result_sg_size = 0;
			else
				cmd->result_sg_size = len - status_size;
			complete(&cmd->completion);
		}
	} while (!virtqueue_enable_cb(vq));
	spin_unlock_irqrestore(&admin_vq->lock, flags);
}

static int virtqueue_exec_admin_cmd(struct virtio_pci_admin_vq *admin_vq,
				    u16 opcode,
				    struct scatterlist **sgs,
				    unsigned int out_num,
				    unsigned int in_num,
				    struct virtio_admin_cmd *cmd)
{
	struct virtqueue *vq;
	unsigned long flags;
	int ret;

	vq = admin_vq->info->vq;
	if (!vq)
		return -EIO;

	if (opcode != VIRTIO_ADMIN_CMD_LIST_QUERY &&
	    opcode != VIRTIO_ADMIN_CMD_LIST_USE &&
	    !((1ULL << opcode) & admin_vq->supported_cmds))
		return -EOPNOTSUPP;

	init_completion(&cmd->completion);

again:
	if (virtqueue_is_broken(vq))
		return -EIO;

	spin_lock_irqsave(&admin_vq->lock, flags);
	ret = virtqueue_add_sgs(vq, sgs, out_num, in_num, cmd, GFP_KERNEL);
	if (ret < 0) {
		if (ret == -ENOSPC) {
			spin_unlock_irqrestore(&admin_vq->lock, flags);
			cpu_relax();
			goto again;
		}
		goto unlock_err;
	}
	if (!virtqueue_kick(vq))
		goto unlock_err;
	spin_unlock_irqrestore(&admin_vq->lock, flags);

	wait_for_completion(&cmd->completion);

	return cmd->ret;

unlock_err:
	spin_unlock_irqrestore(&admin_vq->lock, flags);
	return -EIO;
}

int vp_modern_admin_cmd_exec(struct virtio_device *vdev,
			     struct virtio_admin_cmd *cmd)
{
	struct scatterlist *sgs[VIRTIO_AVQ_SGS_MAX], hdr, stat;
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	struct virtio_admin_cmd_status *va_status;
	unsigned int out_num = 0, in_num = 0;
	struct virtio_admin_cmd_hdr *va_hdr;
	u16 status;
	int ret;

	if (!virtio_has_feature(vdev, VIRTIO_F_ADMIN_VQ))
		return -EOPNOTSUPP;

	va_status = kzalloc(sizeof(*va_status), GFP_KERNEL);
	if (!va_status)
		return -ENOMEM;

	va_hdr = kzalloc(sizeof(*va_hdr), GFP_KERNEL);
	if (!va_hdr) {
		ret = -ENOMEM;
		goto err_alloc;
	}

	va_hdr->opcode = cmd->opcode;
	va_hdr->group_type = cmd->group_type;
	va_hdr->group_member_id = cmd->group_member_id;

	/* Add header */
	sg_init_one(&hdr, va_hdr, sizeof(*va_hdr));
	sgs[out_num] = &hdr;
	out_num++;

	if (cmd->data_sg) {
		sgs[out_num] = cmd->data_sg;
		out_num++;
	}

	/* Add return status */
	sg_init_one(&stat, va_status, sizeof(*va_status));
	sgs[out_num + in_num] = &stat;
	in_num++;

	if (cmd->result_sg) {
		sgs[out_num + in_num] = cmd->result_sg;
		in_num++;
	}

	ret = virtqueue_exec_admin_cmd(&vp_dev->admin_vq,
				       le16_to_cpu(cmd->opcode),
				       sgs, out_num, in_num, cmd);
	if (ret) {
		dev_err(&vdev->dev,
			"Failed to execute command on admin vq: %d\n.", ret);
		goto err_cmd_exec;
	}

	status = le16_to_cpu(va_status->status);
	if (status != VIRTIO_ADMIN_STATUS_OK) {
		dev_err(&vdev->dev,
			"admin command error: status(%#x) qualifier(%#x)\n",
			status, le16_to_cpu(va_status->status_qualifier));
		ret = -status;
	}

err_cmd_exec:
	kfree(va_hdr);
err_alloc:
	kfree(va_status);
	return ret;
}

static void virtio_pci_admin_cmd_list_init(struct virtio_device *virtio_dev)
{
	struct virtio_pci_device *vp_dev = to_vp_device(virtio_dev);
	struct virtio_admin_cmd cmd = {};
	struct scatterlist result_sg;
	struct scatterlist data_sg;
	__le64 *data;
	int ret;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return;

	sg_init_one(&result_sg, data, sizeof(*data));
	cmd.opcode = cpu_to_le16(VIRTIO_ADMIN_CMD_LIST_QUERY);
	cmd.group_type = cpu_to_le16(VIRTIO_ADMIN_GROUP_TYPE_SRIOV);
	cmd.result_sg = &result_sg;

	ret = vp_modern_admin_cmd_exec(virtio_dev, &cmd);
	if (ret)
		goto end;

	*data &= cpu_to_le64(VIRTIO_ADMIN_CMD_BITMAP);
	sg_init_one(&data_sg, data, sizeof(*data));
	cmd.opcode = cpu_to_le16(VIRTIO_ADMIN_CMD_LIST_USE);
	cmd.data_sg = &data_sg;
	cmd.result_sg = NULL;

	ret = vp_modern_admin_cmd_exec(virtio_dev, &cmd);
	if (ret)
		goto end;

	vp_dev->admin_vq.supported_cmds = le64_to_cpu(*data);
end:
	kfree(data);
}

static void
virtio_pci_admin_cmd_dev_parts_objects_enable(struct virtio_device *virtio_dev)
{
	struct virtio_pci_device *vp_dev = to_vp_device(virtio_dev);
	struct virtio_admin_cmd_cap_get_data *get_data;
	struct virtio_admin_cmd_cap_set_data *set_data;
	struct virtio_dev_parts_cap *result;
	struct virtio_admin_cmd cmd = {};
	struct scatterlist result_sg;
	struct scatterlist data_sg;
	u8 resource_objects_limit;
	u16 set_data_size;
	int ret;

	get_data = kzalloc(sizeof(*get_data), GFP_KERNEL);
	if (!get_data)
		return;

	result = kzalloc(sizeof(*result), GFP_KERNEL);
	if (!result)
		goto end;

	get_data->id = cpu_to_le16(VIRTIO_DEV_PARTS_CAP);
	sg_init_one(&data_sg, get_data, sizeof(*get_data));
	sg_init_one(&result_sg, result, sizeof(*result));
	cmd.opcode = cpu_to_le16(VIRTIO_ADMIN_CMD_DEVICE_CAP_GET);
	cmd.group_type = cpu_to_le16(VIRTIO_ADMIN_GROUP_TYPE_SELF);
	cmd.data_sg = &data_sg;
	cmd.result_sg = &result_sg;
	ret = vp_modern_admin_cmd_exec(virtio_dev, &cmd);
	if (ret)
		goto err_get;

	set_data_size = sizeof(*set_data) + sizeof(*result);
	set_data = kzalloc(set_data_size, GFP_KERNEL);
	if (!set_data)
		goto err_get;

	set_data->id = cpu_to_le16(VIRTIO_DEV_PARTS_CAP);

	/* Set the limit to the minimum value between the GET and SET values
	 * supported by the device. Since the obj_id for VIRTIO_DEV_PARTS_CAP
	 * is a globally unique value per PF, there is no possibility of
	 * overlap between GET and SET operations.
	 */
	resource_objects_limit = min(result->get_parts_resource_objects_limit,
				     result->set_parts_resource_objects_limit);
	result->get_parts_resource_objects_limit = resource_objects_limit;
	result->set_parts_resource_objects_limit = resource_objects_limit;
	memcpy(set_data->cap_specific_data, result, sizeof(*result));
	sg_init_one(&data_sg, set_data, set_data_size);
	cmd.data_sg = &data_sg;
	cmd.result_sg = NULL;
	cmd.opcode = cpu_to_le16(VIRTIO_ADMIN_CMD_DRIVER_CAP_SET);
	ret = vp_modern_admin_cmd_exec(virtio_dev, &cmd);
	if (ret)
		goto err_set;

	/* Allocate IDR to manage the dev caps objects */
	ida_init(&vp_dev->admin_vq.dev_parts_ida);
	vp_dev->admin_vq.max_dev_parts_objects = resource_objects_limit;

err_set:
	kfree(set_data);
err_get:
	kfree(result);
end:
	kfree(get_data);
}

static void virtio_pci_admin_cmd_cap_init(struct virtio_device *virtio_dev)
{
	struct virtio_pci_device *vp_dev = to_vp_device(virtio_dev);
	struct virtio_admin_cmd_query_cap_id_result *data;
	struct virtio_admin_cmd cmd = {};
	struct scatterlist result_sg;
	int ret;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return;

	sg_init_one(&result_sg, data, sizeof(*data));
	cmd.opcode = cpu_to_le16(VIRTIO_ADMIN_CMD_CAP_ID_LIST_QUERY);
	cmd.group_type = cpu_to_le16(VIRTIO_ADMIN_GROUP_TYPE_SELF);
	cmd.result_sg = &result_sg;

	ret = vp_modern_admin_cmd_exec(virtio_dev, &cmd);
	if (ret)
		goto end;

	/* Max number of caps fits into a single u64 */
	BUILD_BUG_ON(sizeof(data->supported_caps) > sizeof(u64));

	vp_dev->admin_vq.supported_caps = le64_to_cpu(data->supported_caps[0]);

	if (!(vp_dev->admin_vq.supported_caps & (1 << VIRTIO_DEV_PARTS_CAP)))
		goto end;

	virtio_pci_admin_cmd_dev_parts_objects_enable(virtio_dev);
end:
	kfree(data);
}

static void vp_modern_avq_activate(struct virtio_device *vdev)
{
	if (!virtio_has_feature(vdev, VIRTIO_F_ADMIN_VQ))
		return;

	virtio_pci_admin_cmd_list_init(vdev);
	virtio_pci_admin_cmd_cap_init(vdev);
}

static void vp_modern_avq_cleanup(struct virtio_device *vdev)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	struct virtio_admin_cmd *cmd;
	struct virtqueue *vq;

	if (!virtio_has_feature(vdev, VIRTIO_F_ADMIN_VQ))
		return;

	vq = vp_dev->admin_vq.info->vq;
	if (!vq)
		return;

	while ((cmd = virtqueue_detach_unused_buf(vq))) {
		cmd->ret = -EIO;
		complete(&cmd->completion);
	}
}

static void vp_transport_features(struct virtio_device *vdev, u64 features)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	struct pci_dev *pci_dev = vp_dev->pci_dev;

	if ((features & BIT_ULL(VIRTIO_F_SR_IOV)) &&
			pci_find_ext_capability(pci_dev, PCI_EXT_CAP_ID_SRIOV))
		__virtio_set_bit(vdev, VIRTIO_F_SR_IOV);

	if (features & BIT_ULL(VIRTIO_F_RING_RESET))
		__virtio_set_bit(vdev, VIRTIO_F_RING_RESET);

	if (features & BIT_ULL(VIRTIO_F_ADMIN_VQ))
		__virtio_set_bit(vdev, VIRTIO_F_ADMIN_VQ);
}

static int __vp_check_common_size_one_feature(struct virtio_device *vdev, u32 fbit,
					    u32 offset, const char *fname)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);

	if (!__virtio_test_bit(vdev, fbit))
		return 0;

	if (likely(vp_dev->mdev.common_len >= offset))
		return 0;

	dev_err(&vdev->dev,
		"virtio: common cfg size(%zu) does not match the feature %s\n",
		vp_dev->mdev.common_len, fname);

	return -EINVAL;
}

#define vp_check_common_size_one_feature(vdev, fbit, field) \
	__vp_check_common_size_one_feature(vdev, fbit, \
		offsetofend(struct virtio_pci_modern_common_cfg, field), #fbit)

static int vp_check_common_size(struct virtio_device *vdev)
{
	if (vp_check_common_size_one_feature(vdev, VIRTIO_F_NOTIF_CONFIG_DATA, queue_notify_data))
		return -EINVAL;

	if (vp_check_common_size_one_feature(vdev, VIRTIO_F_RING_RESET, queue_reset))
		return -EINVAL;

	if (vp_check_common_size_one_feature(vdev, VIRTIO_F_ADMIN_VQ, admin_queue_num))
		return -EINVAL;

	return 0;
}

/* virtio config->finalize_features() implementation */
static int vp_finalize_features(struct virtio_device *vdev)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	u64 features = vdev->features;

	/* Give virtio_ring a chance to accept features. */
	vring_transport_features(vdev);

	/* Give virtio_pci a chance to accept features. */
	vp_transport_features(vdev, features);

	if (!__virtio_test_bit(vdev, VIRTIO_F_VERSION_1)) {
		dev_err(&vdev->dev, "virtio: device uses modern interface "
			"but does not have VIRTIO_F_VERSION_1\n");
		return -EINVAL;
	}

	if (vp_check_common_size(vdev))
		return -EINVAL;

	vp_modern_set_extended_features(&vp_dev->mdev, vdev->features_array);

	return 0;
}

/* virtio config->get() implementation */
static void vp_get(struct virtio_device *vdev, unsigned int offset,
		   void *buf, unsigned int len)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	struct virtio_pci_modern_device *mdev = &vp_dev->mdev;
	void __iomem *device = mdev->device;
	u8 b;
	__le16 w;
	__le32 l;

	BUG_ON(offset + len > mdev->device_len);

	switch (len) {
	case 1:
		b = ioread8(device + offset);
		memcpy(buf, &b, sizeof b);
		break;
	case 2:
		w = cpu_to_le16(ioread16(device + offset));
		memcpy(buf, &w, sizeof w);
		break;
	case 4:
		l = cpu_to_le32(ioread32(device + offset));
		memcpy(buf, &l, sizeof l);
		break;
	case 8:
		l = cpu_to_le32(ioread32(device + offset));
		memcpy(buf, &l, sizeof l);
		l = cpu_to_le32(ioread32(device + offset + sizeof l));
		memcpy(buf + sizeof l, &l, sizeof l);
		break;
	default:
		BUG();
	}
}

/* the config->set() implementation.  it's symmetric to the config->get()
 * implementation */
static void vp_set(struct virtio_device *vdev, unsigned int offset,
		   const void *buf, unsigned int len)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	struct virtio_pci_modern_device *mdev = &vp_dev->mdev;
	void __iomem *device = mdev->device;
	u8 b;
	__le16 w;
	__le32 l;

	BUG_ON(offset + len > mdev->device_len);

	switch (len) {
	case 1:
		memcpy(&b, buf, sizeof b);
		iowrite8(b, device + offset);
		break;
	case 2:
		memcpy(&w, buf, sizeof w);
		iowrite16(le16_to_cpu(w), device + offset);
		break;
	case 4:
		memcpy(&l, buf, sizeof l);
		iowrite32(le32_to_cpu(l), device + offset);
		break;
	case 8:
		memcpy(&l, buf, sizeof l);
		iowrite32(le32_to_cpu(l), device + offset);
		memcpy(&l, buf + sizeof l, sizeof l);
		iowrite32(le32_to_cpu(l), device + offset + sizeof l);
		break;
	default:
		BUG();
	}
}

static u32 vp_generation(struct virtio_device *vdev)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);

	return vp_modern_generation(&vp_dev->mdev);
}

/* config->{get,set}_status() implementations */
static u8 vp_get_status(struct virtio_device *vdev)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);

	return vp_modern_get_status(&vp_dev->mdev);
}

static void vp_set_status(struct virtio_device *vdev, u8 status)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);

	/* We should never be setting status to 0. */
	BUG_ON(status == 0);
	vp_modern_set_status(&vp_dev->mdev, status);
	if (status & VIRTIO_CONFIG_S_DRIVER_OK)
		vp_modern_avq_activate(vdev);
}

static void vp_reset(struct virtio_device *vdev)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	struct virtio_pci_modern_device *mdev = &vp_dev->mdev;

	/* 0 status means a reset. */
	vp_modern_set_status(mdev, 0);
	/* After writing 0 to device_status, the driver MUST wait for a read of
	 * device_status to return 0 before reinitializing the device.
	 * This will flush out the status write, and flush in device writes,
	 * including MSI-X interrupts, if any.
	 */
	while (vp_modern_get_status(mdev))
		msleep(1);

	vp_modern_avq_cleanup(vdev);

	/* Flush pending VQ/configuration callbacks. */
	vp_synchronize_vectors(vdev);
}

static int vp_active_vq(struct virtqueue *vq, u16 msix_vec)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vq->vdev);
	struct virtio_pci_modern_device *mdev = &vp_dev->mdev;
	unsigned long index;

	index = vq->index;

	/* activate the queue */
	vp_modern_set_queue_size(mdev, index, virtqueue_get_vring_size(vq));
	vp_modern_queue_address(mdev, index, virtqueue_get_desc_addr(vq),
				virtqueue_get_avail_addr(vq),
				virtqueue_get_used_addr(vq));

	if (msix_vec != VIRTIO_MSI_NO_VECTOR) {
		msix_vec = vp_modern_queue_vector(mdev, index, msix_vec);
		if (msix_vec == VIRTIO_MSI_NO_VECTOR)
			return -EBUSY;
	}

	return 0;
}

static int vp_modern_disable_vq_and_reset(struct virtqueue *vq)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vq->vdev);
	struct virtio_pci_modern_device *mdev = &vp_dev->mdev;
	struct virtio_pci_vq_info *info;
	unsigned long flags;

	if (!virtio_has_feature(vq->vdev, VIRTIO_F_RING_RESET))
		return -ENOENT;

	vp_modern_set_queue_reset(mdev, vq->index);

	info = vp_dev->vqs[vq->index];

	/* delete vq from irq handler */
	spin_lock_irqsave(&vp_dev->lock, flags);
	list_del(&info->node);
	spin_unlock_irqrestore(&vp_dev->lock, flags);

	INIT_LIST_HEAD(&info->node);

#ifdef CONFIG_VIRTIO_HARDEN_NOTIFICATION
	__virtqueue_break(vq);
#endif

	/* For the case where vq has an exclusive irq, call synchronize_irq() to
	 * wait for completion.
	 *
	 * note: We can't use disable_irq() since it conflicts with the affinity
	 * managed IRQ that is used by some drivers.
	 */
	if (vp_dev->per_vq_vectors && info->msix_vector != VIRTIO_MSI_NO_VECTOR)
		synchronize_irq(pci_irq_vector(vp_dev->pci_dev, info->msix_vector));

	vq->reset = true;

	return 0;
}

static int vp_modern_enable_vq_after_reset(struct virtqueue *vq)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vq->vdev);
	struct virtio_pci_modern_device *mdev = &vp_dev->mdev;
	struct virtio_pci_vq_info *info;
	unsigned long flags, index;
	int err;

	if (!vq->reset)
		return -EBUSY;

	index = vq->index;
	info = vp_dev->vqs[index];

	if (vp_modern_get_queue_reset(mdev, index))
		return -EBUSY;

	if (vp_modern_get_queue_enable(mdev, index))
		return -EBUSY;

	err = vp_active_vq(vq, info->msix_vector);
	if (err)
		return err;

	if (vq->callback) {
		spin_lock_irqsave(&vp_dev->lock, flags);
		list_add(&info->node, &vp_dev->virtqueues);
		spin_unlock_irqrestore(&vp_dev->lock, flags);
	} else {
		INIT_LIST_HEAD(&info->node);
	}

#ifdef CONFIG_VIRTIO_HARDEN_NOTIFICATION
	__virtqueue_unbreak(vq);
#endif

	vp_modern_set_queue_enable(&vp_dev->mdev, index, true);
	vq->reset = false;

	return 0;
}

static u16 vp_config_vector(struct virtio_pci_device *vp_dev, u16 vector)
{
	return vp_modern_config_vector(&vp_dev->mdev, vector);
}

static bool vp_notify_with_data(struct virtqueue *vq)
{
	u32 data = vring_notification_data(vq);

	iowrite32(data, (void __iomem *)vq->priv);

	return true;
}

static struct virtqueue *setup_vq(struct virtio_pci_device *vp_dev,
				  struct virtio_pci_vq_info *info,
				  unsigned int index,
				  void (*callback)(struct virtqueue *vq),
				  const char *name,
				  bool ctx,
				  u16 msix_vec)
{

	struct virtio_pci_modern_device *mdev = &vp_dev->mdev;
	bool (*notify)(struct virtqueue *vq);
	struct virtqueue *vq;
	bool is_avq;
	u16 num;
	int err;

	if (__virtio_test_bit(&vp_dev->vdev, VIRTIO_F_NOTIFICATION_DATA))
		notify = vp_notify_with_data;
	else
		notify = vp_notify;

	is_avq = vp_is_avq(&vp_dev->vdev, index);
	if (index >= vp_modern_get_num_queues(mdev) && !is_avq)
		return ERR_PTR(-EINVAL);

	num = vp_modern_get_queue_size(mdev, index);
	/* Check if queue is either not available or already active. */
	if (!num || vp_modern_get_queue_enable(mdev, index))
		return ERR_PTR(-ENOENT);

	info->msix_vector = msix_vec;

	/* create the vring */
	vq = vring_create_virtqueue(index, num,
				    SMP_CACHE_BYTES, &vp_dev->vdev,
				    true, true, ctx,
				    notify, callback, name);
	if (!vq)
		return ERR_PTR(-ENOMEM);

	vq->num_max = num;

	err = vp_active_vq(vq, msix_vec);
	if (err)
		goto err;

	vq->priv = (void __force *)vp_modern_map_vq_notify(mdev, index, NULL);
	if (!vq->priv) {
		err = -ENOMEM;
		goto err;
	}

	return vq;

err:
	vring_del_virtqueue(vq);
	return ERR_PTR(err);
}

static int vp_modern_find_vqs(struct virtio_device *vdev, unsigned int nvqs,
			      struct virtqueue *vqs[],
			      struct virtqueue_info vqs_info[],
			      struct irq_affinity *desc)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	struct virtqueue *vq;
	int rc = vp_find_vqs(vdev, nvqs, vqs, vqs_info, desc);

	if (rc)
		return rc;

	/* Select and activate all queues. Has to be done last: once we do
	 * this, there's no way to go back except reset.
	 */
	list_for_each_entry(vq, &vdev->vqs, list)
		vp_modern_set_queue_enable(&vp_dev->mdev, vq->index, true);

	return 0;
}

static void del_vq(struct virtio_pci_vq_info *info)
{
	struct virtqueue *vq = info->vq;
	struct virtio_pci_device *vp_dev = to_vp_device(vq->vdev);
	struct virtio_pci_modern_device *mdev = &vp_dev->mdev;

	if (vp_dev->msix_enabled)
		vp_modern_queue_vector(mdev, vq->index,
				       VIRTIO_MSI_NO_VECTOR);

	if (!mdev->notify_base)
		pci_iounmap(mdev->pci_dev, (void __force __iomem *)vq->priv);

	vring_del_virtqueue(vq);
}

static int virtio_pci_find_shm_cap(struct pci_dev *dev, u8 required_id,
				   u8 *bar, u64 *offset, u64 *len)
{
	int pos;

	for (pos = pci_find_capability(dev, PCI_CAP_ID_VNDR); pos > 0;
	     pos = pci_find_next_capability(dev, pos, PCI_CAP_ID_VNDR)) {
		u8 type, cap_len, id, res_bar;
		u32 tmp32;
		u64 res_offset, res_length;

		pci_read_config_byte(dev, pos + offsetof(struct virtio_pci_cap,
							 cfg_type), &type);
		if (type != VIRTIO_PCI_CAP_SHARED_MEMORY_CFG)
			continue;

		pci_read_config_byte(dev, pos + offsetof(struct virtio_pci_cap,
							 cap_len), &cap_len);
		if (cap_len != sizeof(struct virtio_pci_cap64)) {
			dev_err(&dev->dev, "%s: shm cap with bad size offset:"
				" %d size: %d\n", __func__, pos, cap_len);
			continue;
		}

		pci_read_config_byte(dev, pos + offsetof(struct virtio_pci_cap,
							 id), &id);
		if (id != required_id)
			continue;

		pci_read_config_byte(dev, pos + offsetof(struct virtio_pci_cap,
							 bar), &res_bar);
		if (res_bar >= PCI_STD_NUM_BARS)
			continue;

		/* Type and ID match, and the BAR value isn't reserved.
		 * Looks good.
		 */

		/* Read the lower 32bit of length and offset */
		pci_read_config_dword(dev, pos + offsetof(struct virtio_pci_cap,
							  offset), &tmp32);
		res_offset = tmp32;
		pci_read_config_dword(dev, pos + offsetof(struct virtio_pci_cap,
							  length), &tmp32);
		res_length = tmp32;

		/* and now the top half */
		pci_read_config_dword(dev,
				      pos + offsetof(struct virtio_pci_cap64,
						     offset_hi), &tmp32);
		res_offset |= ((u64)tmp32) << 32;
		pci_read_config_dword(dev,
				      pos + offsetof(struct virtio_pci_cap64,
						     length_hi), &tmp32);
		res_length |= ((u64)tmp32) << 32;

		*bar = res_bar;
		*offset = res_offset;
		*len = res_length;

		return pos;
	}
	return 0;
}

static bool vp_get_shm_region(struct virtio_device *vdev,
			      struct virtio_shm_region *region, u8 id)
{
	struct virtio_pci_device *vp_dev = to_vp_device(vdev);
	struct pci_dev *pci_dev = vp_dev->pci_dev;
	u8 bar;
	u64 offset, len;
	phys_addr_t phys_addr;
	size_t bar_len;

	if (!virtio_pci_find_shm_cap(pci_dev, id, &bar, &offset, &len))
		return false;

	phys_addr = pci_resource_start(pci_dev, bar);
	bar_len = pci_resource_len(pci_dev, bar);

	if ((offset + len) < offset) {
		dev_err(&pci_dev->dev, "%s: cap offset+len overflow detected\n",
			__func__);
		return false;
	}

	if (offset + len > bar_len) {
		dev_err(&pci_dev->dev, "%s: bar shorter than cap offset+len\n",
			__func__);
		return false;
	}

	region->len = len;
	region->addr = (u64) phys_addr + offset;

	return true;
}

/*
 * virtio_pci_admin_has_dev_parts - Checks whether the device parts
 * functionality is supported
 * @pdev: VF pci_dev
 *
 * Returns true on success.
 */
bool virtio_pci_admin_has_dev_parts(struct pci_dev *pdev)
{
	struct virtio_device *virtio_dev = virtio_pci_vf_get_pf_dev(pdev);
	struct virtio_pci_device *vp_dev;

	if (!virtio_dev)
		return false;

	if (!virtio_has_feature(virtio_dev, VIRTIO_F_ADMIN_VQ))
		return false;

	vp_dev = to_vp_device(virtio_dev);

	if (!((vp_dev->admin_vq.supported_cmds & VIRTIO_DEV_PARTS_ADMIN_CMD_BITMAP) ==
		VIRTIO_DEV_PARTS_ADMIN_CMD_BITMAP))
		return false;

	return vp_dev->admin_vq.max_dev_parts_objects;
}
EXPORT_SYMBOL_GPL(virtio_pci_admin_has_dev_parts);

/*
 * virtio_pci_admin_mode_set - Sets the mode of a member device
 * @pdev: VF pci_dev
 * @flags: device mode's flags
 *
 * Note: caller must serialize access for the given device.
 * Returns 0 on success, or negative on failure.
 */
int virtio_pci_admin_mode_set(struct pci_dev *pdev, u8 flags)
{
	struct virtio_device *virtio_dev = virtio_pci_vf_get_pf_dev(pdev);
	struct virtio_admin_cmd_dev_mode_set_data *data;
	struct virtio_admin_cmd cmd = {};
	struct scatterlist data_sg;
	int vf_id;
	int ret;

	if (!virtio_dev)
		return -ENODEV;

	vf_id = pci_iov_vf_id(pdev);
	if (vf_id < 0)
		return vf_id;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->flags = flags;
	sg_init_one(&data_sg, data, sizeof(*data));
	cmd.opcode = cpu_to_le16(VIRTIO_ADMIN_CMD_DEV_MODE_SET);
	cmd.group_type = cpu_to_le16(VIRTIO_ADMIN_GROUP_TYPE_SRIOV);
	cmd.group_member_id = cpu_to_le64(vf_id + 1);
	cmd.data_sg = &data_sg;
	ret = vp_modern_admin_cmd_exec(virtio_dev, &cmd);

	kfree(data);
	return ret;
}
EXPORT_SYMBOL_GPL(virtio_pci_admin_mode_set);

/*
 * virtio_pci_admin_obj_create - Creates an object for a given type and operation,
 * following the max objects that can be created for that request.
 * @pdev: VF pci_dev
 * @obj_type: Object type
 * @operation_type: Operation type
 * @obj_id: Output unique object id
 *
 * Note: caller must serialize access for the given device.
 * Returns 0 on success, or negative on failure.
 */
int virtio_pci_admin_obj_create(struct pci_dev *pdev, u16 obj_type, u8 operation_type,
				u32 *obj_id)
{
	struct virtio_device *virtio_dev = virtio_pci_vf_get_pf_dev(pdev);
	u16 data_size = sizeof(struct virtio_admin_cmd_resource_obj_create_data);
	struct virtio_admin_cmd_resource_obj_create_data *obj_create_data;
	struct virtio_resource_obj_dev_parts obj_dev_parts = {};
	struct virtio_pci_admin_vq *avq;
	struct virtio_admin_cmd cmd = {};
	struct scatterlist data_sg;
	void *data;
	int id = -1;
	int vf_id;
	int ret;

	if (!virtio_dev)
		return -ENODEV;

	vf_id = pci_iov_vf_id(pdev);
	if (vf_id < 0)
		return vf_id;

	if (obj_type != VIRTIO_RESOURCE_OBJ_DEV_PARTS)
		return -EOPNOTSUPP;

	if (operation_type != VIRTIO_RESOURCE_OBJ_DEV_PARTS_TYPE_GET &&
	    operation_type != VIRTIO_RESOURCE_OBJ_DEV_PARTS_TYPE_SET)
		return -EINVAL;

	avq = &to_vp_device(virtio_dev)->admin_vq;
	if (!avq->max_dev_parts_objects)
		return -EOPNOTSUPP;

	id = ida_alloc_range(&avq->dev_parts_ida, 0,
			     avq->max_dev_parts_objects - 1, GFP_KERNEL);
	if (id < 0)
		return id;

	*obj_id = id;
	data_size += sizeof(obj_dev_parts);
	data = kzalloc(data_size, GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto end;
	}

	obj_create_data = data;
	obj_create_data->hdr.type = cpu_to_le16(obj_type);
	obj_create_data->hdr.id = cpu_to_le32(*obj_id);
	obj_dev_parts.type = operation_type;
	memcpy(obj_create_data->resource_obj_specific_data, &obj_dev_parts,
	       sizeof(obj_dev_parts));
	sg_init_one(&data_sg, data, data_size);
	cmd.opcode = cpu_to_le16(VIRTIO_ADMIN_CMD_RESOURCE_OBJ_CREATE);
	cmd.group_type = cpu_to_le16(VIRTIO_ADMIN_GROUP_TYPE_SRIOV);
	cmd.group_member_id = cpu_to_le64(vf_id + 1);
	cmd.data_sg = &data_sg;
	ret = vp_modern_admin_cmd_exec(virtio_dev, &cmd);

	kfree(data);
end:
	if (ret)
		ida_free(&avq->dev_parts_ida, id);

	return ret;
}
EXPORT_SYMBOL_GPL(virtio_pci_admin_obj_create);

/*
 * virtio_pci_admin_obj_destroy - Destroys an object of a given type and id
 * @pdev: VF pci_dev
 * @obj_type: Object type
 * @id: Object id
 *
 * Note: caller must serialize access for the given device.
 * Returns 0 on success, or negative on failure.
 */
int virtio_pci_admin_obj_destroy(struct pci_dev *pdev, u16 obj_type, u32 id)
{
	struct virtio_device *virtio_dev = virtio_pci_vf_get_pf_dev(pdev);
	struct virtio_admin_cmd_resource_obj_cmd_hdr *data;
	struct virtio_pci_device *vp_dev;
	struct virtio_admin_cmd cmd = {};
	struct scatterlist data_sg;
	int vf_id;
	int ret;

	if (!virtio_dev)
		return -ENODEV;

	vf_id = pci_iov_vf_id(pdev);
	if (vf_id < 0)
		return vf_id;

	if (obj_type != VIRTIO_RESOURCE_OBJ_DEV_PARTS)
		return -EINVAL;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->type = cpu_to_le16(obj_type);
	data->id = cpu_to_le32(id);
	sg_init_one(&data_sg, data, sizeof(*data));
	cmd.opcode = cpu_to_le16(VIRTIO_ADMIN_CMD_RESOURCE_OBJ_DESTROY);
	cmd.group_type = cpu_to_le16(VIRTIO_ADMIN_GROUP_TYPE_SRIOV);
	cmd.group_member_id = cpu_to_le64(vf_id + 1);
	cmd.data_sg = &data_sg;
	ret = vp_modern_admin_cmd_exec(virtio_dev, &cmd);
	if (!ret) {
		vp_dev = to_vp_device(virtio_dev);
		ida_free(&vp_dev->admin_vq.dev_parts_ida, id);
	}

	kfree(data);
	return ret;
}
EXPORT_SYMBOL_GPL(virtio_pci_admin_obj_destroy);

/*
 * virtio_pci_admin_dev_parts_metadata_get - Gets the metadata of the device parts
 * identified by the below attributes.
 * @pdev: VF pci_dev
 * @obj_type: Object type
 * @id: Object id
 * @metadata_type: Metadata type
 * @out: Upon success holds the output for 'metadata type size'
 *
 * Note: caller must serialize access for the given device.
 * Returns 0 on success, or negative on failure.
 */
int virtio_pci_admin_dev_parts_metadata_get(struct pci_dev *pdev, u16 obj_type,
					    u32 id, u8 metadata_type, u32 *out)
{
	struct virtio_device *virtio_dev = virtio_pci_vf_get_pf_dev(pdev);
	struct virtio_admin_cmd_dev_parts_metadata_result *result;
	struct virtio_admin_cmd_dev_parts_metadata_data *data;
	struct scatterlist data_sg, result_sg;
	struct virtio_admin_cmd cmd = {};
	int vf_id;
	int ret;

	if (!virtio_dev)
		return -ENODEV;

	if (metadata_type != VIRTIO_ADMIN_CMD_DEV_PARTS_METADATA_TYPE_SIZE)
		return -EOPNOTSUPP;

	vf_id = pci_iov_vf_id(pdev);
	if (vf_id < 0)
		return vf_id;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	result = kzalloc(sizeof(*result), GFP_KERNEL);
	if (!result) {
		ret = -ENOMEM;
		goto end;
	}

	data->hdr.type = cpu_to_le16(obj_type);
	data->hdr.id = cpu_to_le32(id);
	data->type = metadata_type;
	sg_init_one(&data_sg, data, sizeof(*data));
	sg_init_one(&result_sg, result, sizeof(*result));
	cmd.opcode = cpu_to_le16(VIRTIO_ADMIN_CMD_DEV_PARTS_METADATA_GET);
	cmd.group_type = cpu_to_le16(VIRTIO_ADMIN_GROUP_TYPE_SRIOV);
	cmd.group_member_id = cpu_to_le64(vf_id + 1);
	cmd.data_sg = &data_sg;
	cmd.result_sg = &result_sg;
	ret = vp_modern_admin_cmd_exec(virtio_dev, &cmd);
	if (!ret)
		*out = le32_to_cpu(result->parts_size.size);

	kfree(result);
end:
	kfree(data);
	return ret;
}
EXPORT_SYMBOL_GPL(virtio_pci_admin_dev_parts_metadata_get);

/*
 * virtio_pci_admin_dev_parts_get - Gets the device parts identified by the below attributes.
 * @pdev: VF pci_dev
 * @obj_type: Object type
 * @id: Object id
 * @get_type: Get type
 * @res_sg: Upon success holds the output result data
 * @res_size: Upon success holds the output result size
 *
 * Note: caller must serialize access for the given device.
 * Returns 0 on success, or negative on failure.
 */
int virtio_pci_admin_dev_parts_get(struct pci_dev *pdev, u16 obj_type, u32 id,
				   u8 get_type, struct scatterlist *res_sg,
				   u32 *res_size)
{
	struct virtio_device *virtio_dev = virtio_pci_vf_get_pf_dev(pdev);
	struct virtio_admin_cmd_dev_parts_get_data *data;
	struct scatterlist data_sg;
	struct virtio_admin_cmd cmd = {};
	int vf_id;
	int ret;

	if (!virtio_dev)
		return -ENODEV;

	if (get_type != VIRTIO_ADMIN_CMD_DEV_PARTS_GET_TYPE_ALL)
		return -EOPNOTSUPP;

	vf_id = pci_iov_vf_id(pdev);
	if (vf_id < 0)
		return vf_id;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->hdr.type = cpu_to_le16(obj_type);
	data->hdr.id = cpu_to_le32(id);
	data->type = get_type;
	sg_init_one(&data_sg, data, sizeof(*data));
	cmd.opcode = cpu_to_le16(VIRTIO_ADMIN_CMD_DEV_PARTS_GET);
	cmd.group_type = cpu_to_le16(VIRTIO_ADMIN_GROUP_TYPE_SRIOV);
	cmd.group_member_id = cpu_to_le64(vf_id + 1);
	cmd.data_sg = &data_sg;
	cmd.result_sg = res_sg;
	ret = vp_modern_admin_cmd_exec(virtio_dev, &cmd);
	if (!ret)
		*res_size = cmd.result_sg_size;

	kfree(data);
	return ret;
}
EXPORT_SYMBOL_GPL(virtio_pci_admin_dev_parts_get);

/*
 * virtio_pci_admin_dev_parts_set - Sets the device parts identified by the below attributes.
 * @pdev: VF pci_dev
 * @data_sg: The device parts data, its layout follows struct virtio_admin_cmd_dev_parts_set_data
 *
 * Note: caller must serialize access for the given device.
 * Returns 0 on success, or negative on failure.
 */
int virtio_pci_admin_dev_parts_set(struct pci_dev *pdev, struct scatterlist *data_sg)
{
	struct virtio_device *virtio_dev = virtio_pci_vf_get_pf_dev(pdev);
	struct virtio_admin_cmd cmd = {};
	int vf_id;

	if (!virtio_dev)
		return -ENODEV;

	vf_id = pci_iov_vf_id(pdev);
	if (vf_id < 0)
		return vf_id;

	cmd.opcode = cpu_to_le16(VIRTIO_ADMIN_CMD_DEV_PARTS_SET);
	cmd.group_type = cpu_to_le16(VIRTIO_ADMIN_GROUP_TYPE_SRIOV);
	cmd.group_member_id = cpu_to_le64(vf_id + 1);
	cmd.data_sg = data_sg;
	return vp_modern_admin_cmd_exec(virtio_dev, &cmd);
}
EXPORT_SYMBOL_GPL(virtio_pci_admin_dev_parts_set);

static const struct virtio_config_ops virtio_pci_config_nodev_ops = {
	.get		= NULL,
	.set		= NULL,
	.generation	= vp_generation,
	.get_status	= vp_get_status,
	.set_status	= vp_set_status,
	.reset		= vp_reset,
	.find_vqs	= vp_modern_find_vqs,
	.del_vqs	= vp_del_vqs,
	.synchronize_cbs = vp_synchronize_vectors,
	.get_extended_features = vp_get_features,
	.finalize_features = vp_finalize_features,
	.bus_name	= vp_bus_name,
	.set_vq_affinity = vp_set_vq_affinity,
	.get_vq_affinity = vp_get_vq_affinity,
	.get_shm_region  = vp_get_shm_region,
	.disable_vq_and_reset = vp_modern_disable_vq_and_reset,
	.enable_vq_after_reset = vp_modern_enable_vq_after_reset,
};

static const struct virtio_config_ops virtio_pci_config_ops = {
	.get		= vp_get,
	.set		= vp_set,
	.generation	= vp_generation,
	.get_status	= vp_get_status,
	.set_status	= vp_set_status,
	.reset		= vp_reset,
	.find_vqs	= vp_modern_find_vqs,
	.del_vqs	= vp_del_vqs,
	.synchronize_cbs = vp_synchronize_vectors,
	.get_extended_features = vp_get_features,
	.finalize_features = vp_finalize_features,
	.bus_name	= vp_bus_name,
	.set_vq_affinity = vp_set_vq_affinity,
	.get_vq_affinity = vp_get_vq_affinity,
	.get_shm_region  = vp_get_shm_region,
	.disable_vq_and_reset = vp_modern_disable_vq_and_reset,
	.enable_vq_after_reset = vp_modern_enable_vq_after_reset,
};

/* the PCI probing function */
int virtio_pci_modern_probe(struct virtio_pci_device *vp_dev)
{
	struct virtio_pci_modern_device *mdev = &vp_dev->mdev;
	struct pci_dev *pci_dev = vp_dev->pci_dev;
	int err;

	mdev->pci_dev = pci_dev;

	err = vp_modern_probe(mdev);
	if (err)
		return err;

	if (mdev->device)
		vp_dev->vdev.config = &virtio_pci_config_ops;
	else
		vp_dev->vdev.config = &virtio_pci_config_nodev_ops;

	vp_dev->config_vector = vp_config_vector;
	vp_dev->setup_vq = setup_vq;
	vp_dev->del_vq = del_vq;
	vp_dev->avq_index = vp_avq_index;
	vp_dev->isr = mdev->isr;
	vp_dev->vdev.id = mdev->id;

	spin_lock_init(&vp_dev->admin_vq.lock);
	return 0;
}

void virtio_pci_modern_remove(struct virtio_pci_device *vp_dev)
{
	struct virtio_pci_modern_device *mdev = &vp_dev->mdev;

	vp_modern_remove(mdev);
}
