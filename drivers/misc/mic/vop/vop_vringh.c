/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2016 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Intel Virtio Over PCIe (VOP) driver.
 *
 */
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/dma-mapping.h>

#include <linux/mic_common.h>
#include "../common/mic_dev.h"

#include <linux/mic_ioctl.h>
#include "vop_main.h"

/* Helper API to obtain the VOP PCIe device */
static inline struct device *vop_dev(struct vop_vdev *vdev)
{
	return vdev->vpdev->dev.parent;
}

/* Helper API to check if a virtio device is initialized */
static inline int vop_vdev_inited(struct vop_vdev *vdev)
{
	if (!vdev)
		return -EINVAL;
	/* Device has not been created yet */
	if (!vdev->dd || !vdev->dd->type) {
		dev_err(vop_dev(vdev), "%s %d err %d\n",
			__func__, __LINE__, -EINVAL);
		return -EINVAL;
	}
	/* Device has been removed/deleted */
	if (vdev->dd->type == -1) {
		dev_dbg(vop_dev(vdev), "%s %d err %d\n",
			__func__, __LINE__, -ENODEV);
		return -ENODEV;
	}
	return 0;
}

static void _vop_notify(struct vringh *vrh)
{
	struct vop_vringh *vvrh = container_of(vrh, struct vop_vringh, vrh);
	struct vop_vdev *vdev = vvrh->vdev;
	struct vop_device *vpdev = vdev->vpdev;
	s8 db = vdev->dc->h2c_vdev_db;

	if (db != -1)
		vpdev->hw_ops->send_intr(vpdev, db);
}

static void vop_virtio_init_post(struct vop_vdev *vdev)
{
	struct mic_vqconfig *vqconfig = mic_vq_config(vdev->dd);
	struct vop_device *vpdev = vdev->vpdev;
	int i, used_size;

	for (i = 0; i < vdev->dd->num_vq; i++) {
		used_size = PAGE_ALIGN(sizeof(u16) * 3 +
				sizeof(struct vring_used_elem) *
				le16_to_cpu(vqconfig->num));
		if (!le64_to_cpu(vqconfig[i].used_address)) {
			dev_warn(vop_dev(vdev), "used_address zero??\n");
			continue;
		}
		vdev->vvr[i].vrh.vring.used =
			(void __force *)vpdev->hw_ops->ioremap(
			vpdev,
			le64_to_cpu(vqconfig[i].used_address),
			used_size);
	}

	vdev->dc->used_address_updated = 0;

	dev_info(vop_dev(vdev), "%s: device type %d LINKUP\n",
		 __func__, vdev->virtio_id);
}

static inline void vop_virtio_device_reset(struct vop_vdev *vdev)
{
	int i;

	dev_dbg(vop_dev(vdev), "%s: status %d device type %d RESET\n",
		__func__, vdev->dd->status, vdev->virtio_id);

	for (i = 0; i < vdev->dd->num_vq; i++)
		/*
		 * Avoid lockdep false positive. The + 1 is for the vop
		 * mutex which is held in the reset devices code path.
		 */
		mutex_lock_nested(&vdev->vvr[i].vr_mutex, i + 1);

	/* 0 status means "reset" */
	vdev->dd->status = 0;
	vdev->dc->vdev_reset = 0;
	vdev->dc->host_ack = 1;

	for (i = 0; i < vdev->dd->num_vq; i++) {
		struct vringh *vrh = &vdev->vvr[i].vrh;

		vdev->vvr[i].vring.info->avail_idx = 0;
		vrh->completed = 0;
		vrh->last_avail_idx = 0;
		vrh->last_used_idx = 0;
	}

	for (i = 0; i < vdev->dd->num_vq; i++)
		mutex_unlock(&vdev->vvr[i].vr_mutex);
}

static void vop_virtio_reset_devices(struct vop_info *vi)
{
	struct list_head *pos, *tmp;
	struct vop_vdev *vdev;

	list_for_each_safe(pos, tmp, &vi->vdev_list) {
		vdev = list_entry(pos, struct vop_vdev, list);
		vop_virtio_device_reset(vdev);
		vdev->poll_wake = 1;
		wake_up(&vdev->waitq);
	}
}

static void vop_bh_handler(struct work_struct *work)
{
	struct vop_vdev *vdev = container_of(work, struct vop_vdev,
			virtio_bh_work);

	if (vdev->dc->used_address_updated)
		vop_virtio_init_post(vdev);

	if (vdev->dc->vdev_reset)
		vop_virtio_device_reset(vdev);

	vdev->poll_wake = 1;
	wake_up(&vdev->waitq);
}

static irqreturn_t _vop_virtio_intr_handler(int irq, void *data)
{
	struct vop_vdev *vdev = data;
	struct vop_device *vpdev = vdev->vpdev;

	vpdev->hw_ops->ack_interrupt(vpdev, vdev->virtio_db);
	schedule_work(&vdev->virtio_bh_work);
	return IRQ_HANDLED;
}

static int vop_virtio_config_change(struct vop_vdev *vdev, void *argp)
{
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(wake);
	int ret = 0, retry, i;
	struct vop_device *vpdev = vdev->vpdev;
	struct vop_info *vi = dev_get_drvdata(&vpdev->dev);
	struct mic_bootparam *bootparam = vpdev->hw_ops->get_dp(vpdev);
	s8 db = bootparam->h2c_config_db;

	mutex_lock(&vi->vop_mutex);
	for (i = 0; i < vdev->dd->num_vq; i++)
		mutex_lock_nested(&vdev->vvr[i].vr_mutex, i + 1);

	if (db == -1 || vdev->dd->type == -1) {
		ret = -EIO;
		goto exit;
	}

	memcpy(mic_vq_configspace(vdev->dd), argp, vdev->dd->config_len);
	vdev->dc->config_change = MIC_VIRTIO_PARAM_CONFIG_CHANGED;
	vpdev->hw_ops->send_intr(vpdev, db);

	for (retry = 100; retry--;) {
		ret = wait_event_timeout(wake, vdev->dc->guest_ack,
					 msecs_to_jiffies(100));
		if (ret)
			break;
	}

	dev_dbg(vop_dev(vdev),
		"%s %d retry: %d\n", __func__, __LINE__, retry);
	vdev->dc->config_change = 0;
	vdev->dc->guest_ack = 0;
exit:
	for (i = 0; i < vdev->dd->num_vq; i++)
		mutex_unlock(&vdev->vvr[i].vr_mutex);
	mutex_unlock(&vi->vop_mutex);
	return ret;
}

static int vop_copy_dp_entry(struct vop_vdev *vdev,
			     struct mic_device_desc *argp, __u8 *type,
			     struct mic_device_desc **devpage)
{
	struct vop_device *vpdev = vdev->vpdev;
	struct mic_device_desc *devp;
	struct mic_vqconfig *vqconfig;
	int ret = 0, i;
	bool slot_found = false;

	vqconfig = mic_vq_config(argp);
	for (i = 0; i < argp->num_vq; i++) {
		if (le16_to_cpu(vqconfig[i].num) > MIC_MAX_VRING_ENTRIES) {
			ret =  -EINVAL;
			dev_err(vop_dev(vdev), "%s %d err %d\n",
				__func__, __LINE__, ret);
			goto exit;
		}
	}

	/* Find the first free device page entry */
	for (i = sizeof(struct mic_bootparam);
		i < MIC_DP_SIZE - mic_total_desc_size(argp);
		i += mic_total_desc_size(devp)) {
		devp = vpdev->hw_ops->get_dp(vpdev) + i;
		if (devp->type == 0 || devp->type == -1) {
			slot_found = true;
			break;
		}
	}
	if (!slot_found) {
		ret =  -EINVAL;
		dev_err(vop_dev(vdev), "%s %d err %d\n",
			__func__, __LINE__, ret);
		goto exit;
	}
	/*
	 * Save off the type before doing the memcpy. Type will be set in the
	 * end after completing all initialization for the new device.
	 */
	*type = argp->type;
	argp->type = 0;
	memcpy(devp, argp, mic_desc_size(argp));

	*devpage = devp;
exit:
	return ret;
}

static void vop_init_device_ctrl(struct vop_vdev *vdev,
				 struct mic_device_desc *devpage)
{
	struct mic_device_ctrl *dc;

	dc = (void *)devpage + mic_aligned_desc_size(devpage);

	dc->config_change = 0;
	dc->guest_ack = 0;
	dc->vdev_reset = 0;
	dc->host_ack = 0;
	dc->used_address_updated = 0;
	dc->c2h_vdev_db = -1;
	dc->h2c_vdev_db = -1;
	vdev->dc = dc;
}

static int vop_virtio_add_device(struct vop_vdev *vdev,
				 struct mic_device_desc *argp)
{
	struct vop_info *vi = vdev->vi;
	struct vop_device *vpdev = vi->vpdev;
	struct mic_device_desc *dd = NULL;
	struct mic_vqconfig *vqconfig;
	int vr_size, i, j, ret;
	u8 type = 0;
	s8 db = -1;
	char irqname[16];
	struct mic_bootparam *bootparam;
	u16 num;
	dma_addr_t vr_addr;

	bootparam = vpdev->hw_ops->get_dp(vpdev);
	init_waitqueue_head(&vdev->waitq);
	INIT_LIST_HEAD(&vdev->list);
	vdev->vpdev = vpdev;

	ret = vop_copy_dp_entry(vdev, argp, &type, &dd);
	if (ret) {
		dev_err(vop_dev(vdev), "%s %d err %d\n",
			__func__, __LINE__, ret);
		return ret;
	}

	vop_init_device_ctrl(vdev, dd);

	vdev->dd = dd;
	vdev->virtio_id = type;
	vqconfig = mic_vq_config(dd);
	INIT_WORK(&vdev->virtio_bh_work, vop_bh_handler);

	for (i = 0; i < dd->num_vq; i++) {
		struct vop_vringh *vvr = &vdev->vvr[i];
		struct mic_vring *vr = &vdev->vvr[i].vring;

		num = le16_to_cpu(vqconfig[i].num);
		mutex_init(&vvr->vr_mutex);
		vr_size = PAGE_ALIGN(vring_size(num, MIC_VIRTIO_RING_ALIGN) +
			sizeof(struct _mic_vring_info));
		vr->va = (void *)
			__get_free_pages(GFP_KERNEL | __GFP_ZERO,
					 get_order(vr_size));
		if (!vr->va) {
			ret = -ENOMEM;
			dev_err(vop_dev(vdev), "%s %d err %d\n",
				__func__, __LINE__, ret);
			goto err;
		}
		vr->len = vr_size;
		vr->info = vr->va + vring_size(num, MIC_VIRTIO_RING_ALIGN);
		vr->info->magic = cpu_to_le32(MIC_MAGIC + vdev->virtio_id + i);
		vr_addr = dma_map_single(&vpdev->dev, vr->va, vr_size,
					 DMA_BIDIRECTIONAL);
		if (dma_mapping_error(&vpdev->dev, vr_addr)) {
			free_pages((unsigned long)vr->va, get_order(vr_size));
			ret = -ENOMEM;
			dev_err(vop_dev(vdev), "%s %d err %d\n",
				__func__, __LINE__, ret);
			goto err;
		}
		vqconfig[i].address = cpu_to_le64(vr_addr);

		vring_init(&vr->vr, num, vr->va, MIC_VIRTIO_RING_ALIGN);
		ret = vringh_init_kern(&vvr->vrh,
				       *(u32 *)mic_vq_features(vdev->dd),
				       num, false, vr->vr.desc, vr->vr.avail,
				       vr->vr.used);
		if (ret) {
			dev_err(vop_dev(vdev), "%s %d err %d\n",
				__func__, __LINE__, ret);
			goto err;
		}
		vringh_kiov_init(&vvr->riov, NULL, 0);
		vringh_kiov_init(&vvr->wiov, NULL, 0);
		vvr->head = USHRT_MAX;
		vvr->vdev = vdev;
		vvr->vrh.notify = _vop_notify;
		dev_dbg(&vpdev->dev,
			"%s %d index %d va %p info %p vr_size 0x%x\n",
			__func__, __LINE__, i, vr->va, vr->info, vr_size);
		vvr->buf = (void *)__get_free_pages(GFP_KERNEL,
					get_order(VOP_INT_DMA_BUF_SIZE));
		vvr->buf_da = dma_map_single(&vpdev->dev,
					  vvr->buf, VOP_INT_DMA_BUF_SIZE,
					  DMA_BIDIRECTIONAL);
	}

	snprintf(irqname, sizeof(irqname), "vop%dvirtio%d", vpdev->index,
		 vdev->virtio_id);
	vdev->virtio_db = vpdev->hw_ops->next_db(vpdev);
	vdev->virtio_cookie = vpdev->hw_ops->request_irq(vpdev,
			_vop_virtio_intr_handler, irqname, vdev,
			vdev->virtio_db);
	if (IS_ERR(vdev->virtio_cookie)) {
		ret = PTR_ERR(vdev->virtio_cookie);
		dev_dbg(&vpdev->dev, "request irq failed\n");
		goto err;
	}

	vdev->dc->c2h_vdev_db = vdev->virtio_db;

	/*
	 * Order the type update with previous stores. This write barrier
	 * is paired with the corresponding read barrier before the uncached
	 * system memory read of the type, on the card while scanning the
	 * device page.
	 */
	smp_wmb();
	dd->type = type;
	argp->type = type;

	if (bootparam) {
		db = bootparam->h2c_config_db;
		if (db != -1)
			vpdev->hw_ops->send_intr(vpdev, db);
	}
	dev_dbg(&vpdev->dev, "Added virtio id %d db %d\n", dd->type, db);
	return 0;
err:
	vqconfig = mic_vq_config(dd);
	for (j = 0; j < i; j++) {
		struct vop_vringh *vvr = &vdev->vvr[j];

		dma_unmap_single(&vpdev->dev, le64_to_cpu(vqconfig[j].address),
				 vvr->vring.len, DMA_BIDIRECTIONAL);
		free_pages((unsigned long)vvr->vring.va,
			   get_order(vvr->vring.len));
	}
	return ret;
}

static void vop_dev_remove(struct vop_info *pvi, struct mic_device_ctrl *devp,
			   struct vop_device *vpdev)
{
	struct mic_bootparam *bootparam = vpdev->hw_ops->get_dp(vpdev);
	s8 db;
	int ret, retry;
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(wake);

	devp->config_change = MIC_VIRTIO_PARAM_DEV_REMOVE;
	db = bootparam->h2c_config_db;
	if (db != -1)
		vpdev->hw_ops->send_intr(vpdev, db);
	else
		goto done;
	for (retry = 15; retry--;) {
		ret = wait_event_timeout(wake, devp->guest_ack,
					 msecs_to_jiffies(1000));
		if (ret)
			break;
	}
done:
	devp->config_change = 0;
	devp->guest_ack = 0;
}

static void vop_virtio_del_device(struct vop_vdev *vdev)
{
	struct vop_info *vi = vdev->vi;
	struct vop_device *vpdev = vdev->vpdev;
	int i;
	struct mic_vqconfig *vqconfig;
	struct mic_bootparam *bootparam = vpdev->hw_ops->get_dp(vpdev);

	if (!bootparam)
		goto skip_hot_remove;
	vop_dev_remove(vi, vdev->dc, vpdev);
skip_hot_remove:
	vpdev->hw_ops->free_irq(vpdev, vdev->virtio_cookie, vdev);
	flush_work(&vdev->virtio_bh_work);
	vqconfig = mic_vq_config(vdev->dd);
	for (i = 0; i < vdev->dd->num_vq; i++) {
		struct vop_vringh *vvr = &vdev->vvr[i];

		dma_unmap_single(&vpdev->dev,
				 vvr->buf_da, VOP_INT_DMA_BUF_SIZE,
				 DMA_BIDIRECTIONAL);
		free_pages((unsigned long)vvr->buf,
			   get_order(VOP_INT_DMA_BUF_SIZE));
		vringh_kiov_cleanup(&vvr->riov);
		vringh_kiov_cleanup(&vvr->wiov);
		dma_unmap_single(&vpdev->dev, le64_to_cpu(vqconfig[i].address),
				 vvr->vring.len, DMA_BIDIRECTIONAL);
		free_pages((unsigned long)vvr->vring.va,
			   get_order(vvr->vring.len));
	}
	/*
	 * Order the type update with previous stores. This write barrier
	 * is paired with the corresponding read barrier before the uncached
	 * system memory read of the type, on the card while scanning the
	 * device page.
	 */
	smp_wmb();
	vdev->dd->type = -1;
}

/*
 * vop_sync_dma - Wrapper for synchronous DMAs.
 *
 * @dev - The address of the pointer to the device instance used
 * for DMA registration.
 * @dst - destination DMA address.
 * @src - source DMA address.
 * @len - size of the transfer.
 *
 * Return DMA_SUCCESS on success
 */
static int vop_sync_dma(struct vop_vdev *vdev, dma_addr_t dst, dma_addr_t src,
			size_t len)
{
	int err = 0;
	struct dma_device *ddev;
	struct dma_async_tx_descriptor *tx;
	struct vop_info *vi = dev_get_drvdata(&vdev->vpdev->dev);
	struct dma_chan *vop_ch = vi->dma_ch;

	if (!vop_ch) {
		err = -EBUSY;
		goto error;
	}
	ddev = vop_ch->device;
	tx = ddev->device_prep_dma_memcpy(vop_ch, dst, src, len,
		DMA_PREP_FENCE);
	if (!tx) {
		err = -ENOMEM;
		goto error;
	} else {
		dma_cookie_t cookie;

		cookie = tx->tx_submit(tx);
		if (dma_submit_error(cookie)) {
			err = -ENOMEM;
			goto error;
		}
		dma_async_issue_pending(vop_ch);
		err = dma_sync_wait(vop_ch, cookie);
	}
error:
	if (err)
		dev_err(&vi->vpdev->dev, "%s %d err %d\n",
			__func__, __LINE__, err);
	return err;
}

#define VOP_USE_DMA true

/*
 * Initiates the copies across the PCIe bus from card memory to a user
 * space buffer. When transfers are done using DMA, source/destination
 * addresses and transfer length must follow the alignment requirements of
 * the MIC DMA engine.
 */
static int vop_virtio_copy_to_user(struct vop_vdev *vdev, void __user *ubuf,
				   size_t len, u64 daddr, size_t dlen,
				   int vr_idx)
{
	struct vop_device *vpdev = vdev->vpdev;
	void __iomem *dbuf = vpdev->hw_ops->ioremap(vpdev, daddr, len);
	struct vop_vringh *vvr = &vdev->vvr[vr_idx];
	struct vop_info *vi = dev_get_drvdata(&vpdev->dev);
	size_t dma_alignment = 1 << vi->dma_ch->device->copy_align;
	bool x200 = is_dma_copy_aligned(vi->dma_ch->device, 1, 1, 1);
	size_t dma_offset, partlen;
	int err;

	if (!VOP_USE_DMA) {
		if (copy_to_user(ubuf, (void __force *)dbuf, len)) {
			err = -EFAULT;
			dev_err(vop_dev(vdev), "%s %d err %d\n",
				__func__, __LINE__, err);
			goto err;
		}
		vdev->in_bytes += len;
		err = 0;
		goto err;
	}

	dma_offset = daddr - round_down(daddr, dma_alignment);
	daddr -= dma_offset;
	len += dma_offset;
	/*
	 * X100 uses DMA addresses as seen by the card so adding
	 * the aperture base is not required for DMA. However x200
	 * requires DMA addresses to be an offset into the bar so
	 * add the aperture base for x200.
	 */
	if (x200)
		daddr += vpdev->aper->pa;
	while (len) {
		partlen = min_t(size_t, len, VOP_INT_DMA_BUF_SIZE);
		err = vop_sync_dma(vdev, vvr->buf_da, daddr,
				   ALIGN(partlen, dma_alignment));
		if (err) {
			dev_err(vop_dev(vdev), "%s %d err %d\n",
				__func__, __LINE__, err);
			goto err;
		}
		if (copy_to_user(ubuf, vvr->buf + dma_offset,
				 partlen - dma_offset)) {
			err = -EFAULT;
			dev_err(vop_dev(vdev), "%s %d err %d\n",
				__func__, __LINE__, err);
			goto err;
		}
		daddr += partlen;
		ubuf += partlen;
		dbuf += partlen;
		vdev->in_bytes_dma += partlen;
		vdev->in_bytes += partlen;
		len -= partlen;
		dma_offset = 0;
	}
	err = 0;
err:
	vpdev->hw_ops->iounmap(vpdev, dbuf);
	dev_dbg(vop_dev(vdev),
		"%s: ubuf %p dbuf %p len 0x%lx vr_idx 0x%x\n",
		__func__, ubuf, dbuf, len, vr_idx);
	return err;
}

/*
 * Initiates copies across the PCIe bus from a user space buffer to card
 * memory. When transfers are done using DMA, source/destination addresses
 * and transfer length must follow the alignment requirements of the MIC
 * DMA engine.
 */
static int vop_virtio_copy_from_user(struct vop_vdev *vdev, void __user *ubuf,
				     size_t len, u64 daddr, size_t dlen,
				     int vr_idx)
{
	struct vop_device *vpdev = vdev->vpdev;
	void __iomem *dbuf = vpdev->hw_ops->ioremap(vpdev, daddr, len);
	struct vop_vringh *vvr = &vdev->vvr[vr_idx];
	struct vop_info *vi = dev_get_drvdata(&vdev->vpdev->dev);
	size_t dma_alignment = 1 << vi->dma_ch->device->copy_align;
	bool x200 = is_dma_copy_aligned(vi->dma_ch->device, 1, 1, 1);
	size_t partlen;
	bool dma = VOP_USE_DMA;
	int err = 0;

	if (daddr & (dma_alignment - 1)) {
		vdev->tx_dst_unaligned += len;
		dma = false;
	} else if (ALIGN(len, dma_alignment) > dlen) {
		vdev->tx_len_unaligned += len;
		dma = false;
	}

	if (!dma)
		goto memcpy;

	/*
	 * X100 uses DMA addresses as seen by the card so adding
	 * the aperture base is not required for DMA. However x200
	 * requires DMA addresses to be an offset into the bar so
	 * add the aperture base for x200.
	 */
	if (x200)
		daddr += vpdev->aper->pa;
	while (len) {
		partlen = min_t(size_t, len, VOP_INT_DMA_BUF_SIZE);

		if (copy_from_user(vvr->buf, ubuf, partlen)) {
			err = -EFAULT;
			dev_err(vop_dev(vdev), "%s %d err %d\n",
				__func__, __LINE__, err);
			goto err;
		}
		err = vop_sync_dma(vdev, daddr, vvr->buf_da,
				   ALIGN(partlen, dma_alignment));
		if (err) {
			dev_err(vop_dev(vdev), "%s %d err %d\n",
				__func__, __LINE__, err);
			goto err;
		}
		daddr += partlen;
		ubuf += partlen;
		dbuf += partlen;
		vdev->out_bytes_dma += partlen;
		vdev->out_bytes += partlen;
		len -= partlen;
	}
memcpy:
	/*
	 * We are copying to IO below and should ideally use something
	 * like copy_from_user_toio(..) if it existed.
	 */
	if (copy_from_user((void __force *)dbuf, ubuf, len)) {
		err = -EFAULT;
		dev_err(vop_dev(vdev), "%s %d err %d\n",
			__func__, __LINE__, err);
		goto err;
	}
	vdev->out_bytes += len;
	err = 0;
err:
	vpdev->hw_ops->iounmap(vpdev, dbuf);
	dev_dbg(vop_dev(vdev),
		"%s: ubuf %p dbuf %p len 0x%lx vr_idx 0x%x\n",
		__func__, ubuf, dbuf, len, vr_idx);
	return err;
}

#define MIC_VRINGH_READ true

/* Determine the total number of bytes consumed in a VRINGH KIOV */
static inline u32 vop_vringh_iov_consumed(struct vringh_kiov *iov)
{
	int i;
	u32 total = iov->consumed;

	for (i = 0; i < iov->i; i++)
		total += iov->iov[i].iov_len;
	return total;
}

/*
 * Traverse the VRINGH KIOV and issue the APIs to trigger the copies.
 * This API is heavily based on the vringh_iov_xfer(..) implementation
 * in vringh.c. The reason we cannot reuse vringh_iov_pull_kern(..)
 * and vringh_iov_push_kern(..) directly is because there is no
 * way to override the VRINGH xfer(..) routines as of v3.10.
 */
static int vop_vringh_copy(struct vop_vdev *vdev, struct vringh_kiov *iov,
			   void __user *ubuf, size_t len, bool read, int vr_idx,
			   size_t *out_len)
{
	int ret = 0;
	size_t partlen, tot_len = 0;

	while (len && iov->i < iov->used) {
		struct kvec *kiov = &iov->iov[iov->i];

		partlen = min(kiov->iov_len, len);
		if (read)
			ret = vop_virtio_copy_to_user(vdev, ubuf, partlen,
						      (u64)kiov->iov_base,
						      kiov->iov_len,
						      vr_idx);
		else
			ret = vop_virtio_copy_from_user(vdev, ubuf, partlen,
							(u64)kiov->iov_base,
							kiov->iov_len,
							vr_idx);
		if (ret) {
			dev_err(vop_dev(vdev), "%s %d err %d\n",
				__func__, __LINE__, ret);
			break;
		}
		len -= partlen;
		ubuf += partlen;
		tot_len += partlen;
		iov->consumed += partlen;
		kiov->iov_len -= partlen;
		kiov->iov_base += partlen;
		if (!kiov->iov_len) {
			/* Fix up old iov element then increment. */
			kiov->iov_len = iov->consumed;
			kiov->iov_base -= iov->consumed;

			iov->consumed = 0;
			iov->i++;
		}
	}
	*out_len = tot_len;
	return ret;
}

/*
 * Use the standard VRINGH infrastructure in the kernel to fetch new
 * descriptors, initiate the copies and update the used ring.
 */
static int _vop_virtio_copy(struct vop_vdev *vdev, struct mic_copy_desc *copy)
{
	int ret = 0;
	u32 iovcnt = copy->iovcnt;
	struct iovec iov;
	struct iovec __user *u_iov = copy->iov;
	void __user *ubuf = NULL;
	struct vop_vringh *vvr = &vdev->vvr[copy->vr_idx];
	struct vringh_kiov *riov = &vvr->riov;
	struct vringh_kiov *wiov = &vvr->wiov;
	struct vringh *vrh = &vvr->vrh;
	u16 *head = &vvr->head;
	struct mic_vring *vr = &vvr->vring;
	size_t len = 0, out_len;

	copy->out_len = 0;
	/* Fetch a new IOVEC if all previous elements have been processed */
	if (riov->i == riov->used && wiov->i == wiov->used) {
		ret = vringh_getdesc_kern(vrh, riov, wiov,
					  head, GFP_KERNEL);
		/* Check if there are available descriptors */
		if (ret <= 0)
			return ret;
	}
	while (iovcnt) {
		if (!len) {
			/* Copy over a new iovec from user space. */
			ret = copy_from_user(&iov, u_iov, sizeof(*u_iov));
			if (ret) {
				ret = -EINVAL;
				dev_err(vop_dev(vdev), "%s %d err %d\n",
					__func__, __LINE__, ret);
				break;
			}
			len = iov.iov_len;
			ubuf = iov.iov_base;
		}
		/* Issue all the read descriptors first */
		ret = vop_vringh_copy(vdev, riov, ubuf, len,
				      MIC_VRINGH_READ, copy->vr_idx, &out_len);
		if (ret) {
			dev_err(vop_dev(vdev), "%s %d err %d\n",
				__func__, __LINE__, ret);
			break;
		}
		len -= out_len;
		ubuf += out_len;
		copy->out_len += out_len;
		/* Issue the write descriptors next */
		ret = vop_vringh_copy(vdev, wiov, ubuf, len,
				      !MIC_VRINGH_READ, copy->vr_idx, &out_len);
		if (ret) {
			dev_err(vop_dev(vdev), "%s %d err %d\n",
				__func__, __LINE__, ret);
			break;
		}
		len -= out_len;
		ubuf += out_len;
		copy->out_len += out_len;
		if (!len) {
			/* One user space iovec is now completed */
			iovcnt--;
			u_iov++;
		}
		/* Exit loop if all elements in KIOVs have been processed. */
		if (riov->i == riov->used && wiov->i == wiov->used)
			break;
	}
	/*
	 * Update the used ring if a descriptor was available and some data was
	 * copied in/out and the user asked for a used ring update.
	 */
	if (*head != USHRT_MAX && copy->out_len && copy->update_used) {
		u32 total = 0;

		/* Determine the total data consumed */
		total += vop_vringh_iov_consumed(riov);
		total += vop_vringh_iov_consumed(wiov);
		vringh_complete_kern(vrh, *head, total);
		*head = USHRT_MAX;
		if (vringh_need_notify_kern(vrh) > 0)
			vringh_notify(vrh);
		vringh_kiov_cleanup(riov);
		vringh_kiov_cleanup(wiov);
		/* Update avail idx for user space */
		vr->info->avail_idx = vrh->last_avail_idx;
	}
	return ret;
}

static inline int vop_verify_copy_args(struct vop_vdev *vdev,
				       struct mic_copy_desc *copy)
{
	if (!vdev || copy->vr_idx >= vdev->dd->num_vq)
		return -EINVAL;
	return 0;
}

/* Copy a specified number of virtio descriptors in a chain */
static int vop_virtio_copy_desc(struct vop_vdev *vdev,
				struct mic_copy_desc *copy)
{
	int err;
	struct vop_vringh *vvr;

	err = vop_verify_copy_args(vdev, copy);
	if (err)
		return err;

	vvr = &vdev->vvr[copy->vr_idx];
	mutex_lock(&vvr->vr_mutex);
	if (!vop_vdevup(vdev)) {
		err = -ENODEV;
		dev_err(vop_dev(vdev), "%s %d err %d\n",
			__func__, __LINE__, err);
		goto err;
	}
	err = _vop_virtio_copy(vdev, copy);
	if (err) {
		dev_err(vop_dev(vdev), "%s %d err %d\n",
			__func__, __LINE__, err);
	}
err:
	mutex_unlock(&vvr->vr_mutex);
	return err;
}

static int vop_open(struct inode *inode, struct file *f)
{
	struct vop_vdev *vdev;
	struct vop_info *vi = container_of(f->private_data,
		struct vop_info, miscdev);

	vdev = kzalloc(sizeof(*vdev), GFP_KERNEL);
	if (!vdev)
		return -ENOMEM;
	vdev->vi = vi;
	mutex_init(&vdev->vdev_mutex);
	f->private_data = vdev;
	init_completion(&vdev->destroy);
	complete(&vdev->destroy);
	return 0;
}

static int vop_release(struct inode *inode, struct file *f)
{
	struct vop_vdev *vdev = f->private_data, *vdev_tmp;
	struct vop_info *vi = vdev->vi;
	struct list_head *pos, *tmp;
	bool found = false;

	mutex_lock(&vdev->vdev_mutex);
	if (vdev->deleted)
		goto unlock;
	mutex_lock(&vi->vop_mutex);
	list_for_each_safe(pos, tmp, &vi->vdev_list) {
		vdev_tmp = list_entry(pos, struct vop_vdev, list);
		if (vdev == vdev_tmp) {
			vop_virtio_del_device(vdev);
			list_del(pos);
			found = true;
			break;
		}
	}
	mutex_unlock(&vi->vop_mutex);
unlock:
	mutex_unlock(&vdev->vdev_mutex);
	if (!found)
		wait_for_completion(&vdev->destroy);
	f->private_data = NULL;
	kfree(vdev);
	return 0;
}

static long vop_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	struct vop_vdev *vdev = f->private_data;
	struct vop_info *vi = vdev->vi;
	void __user *argp = (void __user *)arg;
	int ret;

	switch (cmd) {
	case MIC_VIRTIO_ADD_DEVICE:
	{
		struct mic_device_desc dd, *dd_config;

		if (copy_from_user(&dd, argp, sizeof(dd)))
			return -EFAULT;

		if (mic_aligned_desc_size(&dd) > MIC_MAX_DESC_BLK_SIZE ||
		    dd.num_vq > MIC_MAX_VRINGS)
			return -EINVAL;

		dd_config = kzalloc(mic_desc_size(&dd), GFP_KERNEL);
		if (!dd_config)
			return -ENOMEM;
		if (copy_from_user(dd_config, argp, mic_desc_size(&dd))) {
			ret = -EFAULT;
			goto free_ret;
		}
		/* Ensure desc has not changed between the two reads */
		if (memcmp(&dd, dd_config, sizeof(dd))) {
			ret = -EINVAL;
			goto free_ret;
		}
		mutex_lock(&vdev->vdev_mutex);
		mutex_lock(&vi->vop_mutex);
		ret = vop_virtio_add_device(vdev, dd_config);
		if (ret)
			goto unlock_ret;
		list_add_tail(&vdev->list, &vi->vdev_list);
unlock_ret:
		mutex_unlock(&vi->vop_mutex);
		mutex_unlock(&vdev->vdev_mutex);
free_ret:
		kfree(dd_config);
		return ret;
	}
	case MIC_VIRTIO_COPY_DESC:
	{
		struct mic_copy_desc copy;

		mutex_lock(&vdev->vdev_mutex);
		ret = vop_vdev_inited(vdev);
		if (ret)
			goto _unlock_ret;

		if (copy_from_user(&copy, argp, sizeof(copy))) {
			ret = -EFAULT;
			goto _unlock_ret;
		}

		ret = vop_virtio_copy_desc(vdev, &copy);
		if (ret < 0)
			goto _unlock_ret;
		if (copy_to_user(
			&((struct mic_copy_desc __user *)argp)->out_len,
			&copy.out_len, sizeof(copy.out_len)))
			ret = -EFAULT;
_unlock_ret:
		mutex_unlock(&vdev->vdev_mutex);
		return ret;
	}
	case MIC_VIRTIO_CONFIG_CHANGE:
	{
		void *buf;

		mutex_lock(&vdev->vdev_mutex);
		ret = vop_vdev_inited(vdev);
		if (ret)
			goto __unlock_ret;
		buf = kzalloc(vdev->dd->config_len, GFP_KERNEL);
		if (!buf) {
			ret = -ENOMEM;
			goto __unlock_ret;
		}
		if (copy_from_user(buf, argp, vdev->dd->config_len)) {
			ret = -EFAULT;
			goto done;
		}
		ret = vop_virtio_config_change(vdev, buf);
done:
		kfree(buf);
__unlock_ret:
		mutex_unlock(&vdev->vdev_mutex);
		return ret;
	}
	default:
		return -ENOIOCTLCMD;
	};
	return 0;
}

/*
 * We return POLLIN | POLLOUT from poll when new buffers are enqueued, and
 * not when previously enqueued buffers may be available. This means that
 * in the card->host (TX) path, when userspace is unblocked by poll it
 * must drain all available descriptors or it can stall.
 */
static unsigned int vop_poll(struct file *f, poll_table *wait)
{
	struct vop_vdev *vdev = f->private_data;
	int mask = 0;

	mutex_lock(&vdev->vdev_mutex);
	if (vop_vdev_inited(vdev)) {
		mask = POLLERR;
		goto done;
	}
	poll_wait(f, &vdev->waitq, wait);
	if (vop_vdev_inited(vdev)) {
		mask = POLLERR;
	} else if (vdev->poll_wake) {
		vdev->poll_wake = 0;
		mask = POLLIN | POLLOUT;
	}
done:
	mutex_unlock(&vdev->vdev_mutex);
	return mask;
}

static inline int
vop_query_offset(struct vop_vdev *vdev, unsigned long offset,
		 unsigned long *size, unsigned long *pa)
{
	struct vop_device *vpdev = vdev->vpdev;
	unsigned long start = MIC_DP_SIZE;
	int i;

	/*
	 * MMAP interface is as follows:
	 * offset				region
	 * 0x0					virtio device_page
	 * 0x1000				first vring
	 * 0x1000 + size of 1st vring		second vring
	 * ....
	 */
	if (!offset) {
		*pa = virt_to_phys(vpdev->hw_ops->get_dp(vpdev));
		*size = MIC_DP_SIZE;
		return 0;
	}

	for (i = 0; i < vdev->dd->num_vq; i++) {
		struct vop_vringh *vvr = &vdev->vvr[i];

		if (offset == start) {
			*pa = virt_to_phys(vvr->vring.va);
			*size = vvr->vring.len;
			return 0;
		}
		start += vvr->vring.len;
	}
	return -1;
}

/*
 * Maps the device page and virtio rings to user space for readonly access.
 */
static int vop_mmap(struct file *f, struct vm_area_struct *vma)
{
	struct vop_vdev *vdev = f->private_data;
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	unsigned long pa, size = vma->vm_end - vma->vm_start, size_rem = size;
	int i, err;

	err = vop_vdev_inited(vdev);
	if (err)
		goto ret;
	if (vma->vm_flags & VM_WRITE) {
		err = -EACCES;
		goto ret;
	}
	while (size_rem) {
		i = vop_query_offset(vdev, offset, &size, &pa);
		if (i < 0) {
			err = -EINVAL;
			goto ret;
		}
		err = remap_pfn_range(vma, vma->vm_start + offset,
				      pa >> PAGE_SHIFT, size,
				      vma->vm_page_prot);
		if (err)
			goto ret;
		size_rem -= size;
		offset += size;
	}
ret:
	return err;
}

static const struct file_operations vop_fops = {
	.open = vop_open,
	.release = vop_release,
	.unlocked_ioctl = vop_ioctl,
	.poll = vop_poll,
	.mmap = vop_mmap,
	.owner = THIS_MODULE,
};

int vop_host_init(struct vop_info *vi)
{
	int rc;
	struct miscdevice *mdev;
	struct vop_device *vpdev = vi->vpdev;

	INIT_LIST_HEAD(&vi->vdev_list);
	vi->dma_ch = vpdev->dma_ch;
	mdev = &vi->miscdev;
	mdev->minor = MISC_DYNAMIC_MINOR;
	snprintf(vi->name, sizeof(vi->name), "vop_virtio%d", vpdev->index);
	mdev->name = vi->name;
	mdev->fops = &vop_fops;
	mdev->parent = &vpdev->dev;

	rc = misc_register(mdev);
	if (rc)
		dev_err(&vpdev->dev, "%s failed rc %d\n", __func__, rc);
	return rc;
}

void vop_host_uninit(struct vop_info *vi)
{
	struct list_head *pos, *tmp;
	struct vop_vdev *vdev;

	mutex_lock(&vi->vop_mutex);
	vop_virtio_reset_devices(vi);
	list_for_each_safe(pos, tmp, &vi->vdev_list) {
		vdev = list_entry(pos, struct vop_vdev, list);
		list_del(pos);
		reinit_completion(&vdev->destroy);
		mutex_unlock(&vi->vop_mutex);
		mutex_lock(&vdev->vdev_mutex);
		vop_virtio_del_device(vdev);
		vdev->deleted = true;
		mutex_unlock(&vdev->vdev_mutex);
		complete(&vdev->destroy);
		mutex_lock(&vi->vop_mutex);
	}
	mutex_unlock(&vi->vop_mutex);
	misc_deregister(&vi->miscdev);
}
