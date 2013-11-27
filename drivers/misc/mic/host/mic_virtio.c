/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2013 Intel Corporation.
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
 * Intel MIC Host driver.
 *
 */
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/uaccess.h>

#include <linux/mic_common.h>
#include "../common/mic_dev.h"
#include "mic_device.h"
#include "mic_smpt.h"
#include "mic_virtio.h"

/*
 * Initiates the copies across the PCIe bus from card memory to
 * a user space buffer.
 */
static int mic_virtio_copy_to_user(struct mic_vdev *mvdev,
		void __user *ubuf, size_t len, u64 addr)
{
	int err;
	void __iomem *dbuf = mvdev->mdev->aper.va + addr;
	/*
	 * We are copying from IO below an should ideally use something
	 * like copy_to_user_fromio(..) if it existed.
	 */
	if (copy_to_user(ubuf, (void __force *)dbuf, len)) {
		err = -EFAULT;
		dev_err(mic_dev(mvdev), "%s %d err %d\n",
			__func__, __LINE__, err);
		goto err;
	}
	mvdev->in_bytes += len;
	err = 0;
err:
	return err;
}

/*
 * Initiates copies across the PCIe bus from a user space
 * buffer to card memory.
 */
static int mic_virtio_copy_from_user(struct mic_vdev *mvdev,
		void __user *ubuf, size_t len, u64 addr)
{
	int err;
	void __iomem *dbuf = mvdev->mdev->aper.va + addr;
	/*
	 * We are copying to IO below and should ideally use something
	 * like copy_from_user_toio(..) if it existed.
	 */
	if (copy_from_user((void __force *)dbuf, ubuf, len)) {
		err = -EFAULT;
		dev_err(mic_dev(mvdev), "%s %d err %d\n",
			__func__, __LINE__, err);
		goto err;
	}
	mvdev->out_bytes += len;
	err = 0;
err:
	return err;
}

#define MIC_VRINGH_READ true

/* The function to call to notify the card about added buffers */
static void mic_notify(struct vringh *vrh)
{
	struct mic_vringh *mvrh = container_of(vrh, struct mic_vringh, vrh);
	struct mic_vdev *mvdev = mvrh->mvdev;
	s8 db = mvdev->dc->h2c_vdev_db;

	if (db != -1)
		mvdev->mdev->ops->send_intr(mvdev->mdev, db);
}

/* Determine the total number of bytes consumed in a VRINGH KIOV */
static inline u32 mic_vringh_iov_consumed(struct vringh_kiov *iov)
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
static int mic_vringh_copy(struct mic_vdev *mvdev, struct vringh_kiov *iov,
	void __user *ubuf, size_t len, bool read, size_t *out_len)
{
	int ret = 0;
	size_t partlen, tot_len = 0;

	while (len && iov->i < iov->used) {
		partlen = min(iov->iov[iov->i].iov_len, len);
		if (read)
			ret = mic_virtio_copy_to_user(mvdev,
				ubuf, partlen,
				(u64)iov->iov[iov->i].iov_base);
		else
			ret = mic_virtio_copy_from_user(mvdev,
				ubuf, partlen,
				(u64)iov->iov[iov->i].iov_base);
		if (ret) {
			dev_err(mic_dev(mvdev), "%s %d err %d\n",
				__func__, __LINE__, ret);
			break;
		}
		len -= partlen;
		ubuf += partlen;
		tot_len += partlen;
		iov->consumed += partlen;
		iov->iov[iov->i].iov_len -= partlen;
		iov->iov[iov->i].iov_base += partlen;
		if (!iov->iov[iov->i].iov_len) {
			/* Fix up old iov element then increment. */
			iov->iov[iov->i].iov_len = iov->consumed;
			iov->iov[iov->i].iov_base -= iov->consumed;

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
static int _mic_virtio_copy(struct mic_vdev *mvdev,
	struct mic_copy_desc *copy)
{
	int ret = 0, iovcnt = copy->iovcnt;
	struct iovec iov;
	struct iovec __user *u_iov = copy->iov;
	void __user *ubuf = NULL;
	struct mic_vringh *mvr = &mvdev->mvr[copy->vr_idx];
	struct vringh_kiov *riov = &mvr->riov;
	struct vringh_kiov *wiov = &mvr->wiov;
	struct vringh *vrh = &mvr->vrh;
	u16 *head = &mvr->head;
	struct mic_vring *vr = &mvr->vring;
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
				dev_err(mic_dev(mvdev), "%s %d err %d\n",
					__func__, __LINE__, ret);
				break;
			}
			len = iov.iov_len;
			ubuf = iov.iov_base;
		}
		/* Issue all the read descriptors first */
		ret = mic_vringh_copy(mvdev, riov, ubuf, len,
			MIC_VRINGH_READ, &out_len);
		if (ret) {
			dev_err(mic_dev(mvdev), "%s %d err %d\n",
				__func__, __LINE__, ret);
			break;
		}
		len -= out_len;
		ubuf += out_len;
		copy->out_len += out_len;
		/* Issue the write descriptors next */
		ret = mic_vringh_copy(mvdev, wiov, ubuf, len,
			!MIC_VRINGH_READ, &out_len);
		if (ret) {
			dev_err(mic_dev(mvdev), "%s %d err %d\n",
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
		total += mic_vringh_iov_consumed(riov);
		total += mic_vringh_iov_consumed(wiov);
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

static inline int mic_verify_copy_args(struct mic_vdev *mvdev,
		struct mic_copy_desc *copy)
{
	if (copy->vr_idx >= mvdev->dd->num_vq) {
		dev_err(mic_dev(mvdev), "%s %d err %d\n",
			__func__, __LINE__, -EINVAL);
		return -EINVAL;
	}
	return 0;
}

/* Copy a specified number of virtio descriptors in a chain */
int mic_virtio_copy_desc(struct mic_vdev *mvdev,
		struct mic_copy_desc *copy)
{
	int err;
	struct mic_vringh *mvr = &mvdev->mvr[copy->vr_idx];

	err = mic_verify_copy_args(mvdev, copy);
	if (err)
		return err;

	mutex_lock(&mvr->vr_mutex);
	if (!mic_vdevup(mvdev)) {
		err = -ENODEV;
		dev_err(mic_dev(mvdev), "%s %d err %d\n",
			__func__, __LINE__, err);
		goto err;
	}
	err = _mic_virtio_copy(mvdev, copy);
	if (err) {
		dev_err(mic_dev(mvdev), "%s %d err %d\n",
			__func__, __LINE__, err);
	}
err:
	mutex_unlock(&mvr->vr_mutex);
	return err;
}

static void mic_virtio_init_post(struct mic_vdev *mvdev)
{
	struct mic_vqconfig *vqconfig = mic_vq_config(mvdev->dd);
	int i;

	for (i = 0; i < mvdev->dd->num_vq; i++) {
		if (!le64_to_cpu(vqconfig[i].used_address)) {
			dev_warn(mic_dev(mvdev), "used_address zero??\n");
			continue;
		}
		mvdev->mvr[i].vrh.vring.used =
			(void __force *)mvdev->mdev->aper.va +
			le64_to_cpu(vqconfig[i].used_address);
	}

	mvdev->dc->used_address_updated = 0;

	dev_dbg(mic_dev(mvdev), "%s: device type %d LINKUP\n",
		__func__, mvdev->virtio_id);
}

static inline void mic_virtio_device_reset(struct mic_vdev *mvdev)
{
	int i;

	dev_dbg(mic_dev(mvdev), "%s: status %d device type %d RESET\n",
		__func__, mvdev->dd->status, mvdev->virtio_id);

	for (i = 0; i < mvdev->dd->num_vq; i++)
		/*
		 * Avoid lockdep false positive. The + 1 is for the mic
		 * mutex which is held in the reset devices code path.
		 */
		mutex_lock_nested(&mvdev->mvr[i].vr_mutex, i + 1);

	/* 0 status means "reset" */
	mvdev->dd->status = 0;
	mvdev->dc->vdev_reset = 0;
	mvdev->dc->host_ack = 1;

	for (i = 0; i < mvdev->dd->num_vq; i++) {
		struct vringh *vrh = &mvdev->mvr[i].vrh;
		mvdev->mvr[i].vring.info->avail_idx = 0;
		vrh->completed = 0;
		vrh->last_avail_idx = 0;
		vrh->last_used_idx = 0;
	}

	for (i = 0; i < mvdev->dd->num_vq; i++)
		mutex_unlock(&mvdev->mvr[i].vr_mutex);
}

void mic_virtio_reset_devices(struct mic_device *mdev)
{
	struct list_head *pos, *tmp;
	struct mic_vdev *mvdev;

	dev_dbg(mdev->sdev->parent, "%s\n",  __func__);

	list_for_each_safe(pos, tmp, &mdev->vdev_list) {
		mvdev = list_entry(pos, struct mic_vdev, list);
		mic_virtio_device_reset(mvdev);
		mvdev->poll_wake = 1;
		wake_up(&mvdev->waitq);
	}
}

void mic_bh_handler(struct work_struct *work)
{
	struct mic_vdev *mvdev = container_of(work, struct mic_vdev,
			virtio_bh_work);

	if (mvdev->dc->used_address_updated)
		mic_virtio_init_post(mvdev);

	if (mvdev->dc->vdev_reset)
		mic_virtio_device_reset(mvdev);

	mvdev->poll_wake = 1;
	wake_up(&mvdev->waitq);
}

static irqreturn_t mic_virtio_intr_handler(int irq, void *data)
{
	struct mic_vdev *mvdev = data;
	struct mic_device *mdev = mvdev->mdev;

	mdev->ops->ack_interrupt(mdev);
	schedule_work(&mvdev->virtio_bh_work);
	return IRQ_HANDLED;
}

int mic_virtio_config_change(struct mic_vdev *mvdev,
			void __user *argp)
{
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(wake);
	int ret = 0, retry, i;
	struct mic_bootparam *bootparam = mvdev->mdev->dp;
	s8 db = bootparam->h2c_config_db;

	mutex_lock(&mvdev->mdev->mic_mutex);
	for (i = 0; i < mvdev->dd->num_vq; i++)
		mutex_lock_nested(&mvdev->mvr[i].vr_mutex, i + 1);

	if (db == -1 || mvdev->dd->type == -1) {
		ret = -EIO;
		goto exit;
	}

	if (copy_from_user(mic_vq_configspace(mvdev->dd),
			   argp, mvdev->dd->config_len)) {
		dev_err(mic_dev(mvdev), "%s %d err %d\n",
			__func__, __LINE__, -EFAULT);
		ret = -EFAULT;
		goto exit;
	}
	mvdev->dc->config_change = MIC_VIRTIO_PARAM_CONFIG_CHANGED;
	mvdev->mdev->ops->send_intr(mvdev->mdev, db);

	for (retry = 100; retry--;) {
		ret = wait_event_timeout(wake,
			mvdev->dc->guest_ack, msecs_to_jiffies(100));
		if (ret)
			break;
	}

	dev_dbg(mic_dev(mvdev),
		"%s %d retry: %d\n", __func__, __LINE__, retry);
	mvdev->dc->config_change = 0;
	mvdev->dc->guest_ack = 0;
exit:
	for (i = 0; i < mvdev->dd->num_vq; i++)
		mutex_unlock(&mvdev->mvr[i].vr_mutex);
	mutex_unlock(&mvdev->mdev->mic_mutex);
	return ret;
}

static int mic_copy_dp_entry(struct mic_vdev *mvdev,
					void __user *argp,
					__u8 *type,
					struct mic_device_desc **devpage)
{
	struct mic_device *mdev = mvdev->mdev;
	struct mic_device_desc dd, *dd_config, *devp;
	struct mic_vqconfig *vqconfig;
	int ret = 0, i;
	bool slot_found = false;

	if (copy_from_user(&dd, argp, sizeof(dd))) {
		dev_err(mic_dev(mvdev), "%s %d err %d\n",
			__func__, __LINE__, -EFAULT);
		return -EFAULT;
	}

	if (mic_aligned_desc_size(&dd) > MIC_MAX_DESC_BLK_SIZE ||
	    dd.num_vq > MIC_MAX_VRINGS) {
		dev_err(mic_dev(mvdev), "%s %d err %d\n",
			__func__, __LINE__, -EINVAL);
		return -EINVAL;
	}

	dd_config = kmalloc(mic_desc_size(&dd), GFP_KERNEL);
	if (dd_config == NULL) {
		dev_err(mic_dev(mvdev), "%s %d err %d\n",
			__func__, __LINE__, -ENOMEM);
		return -ENOMEM;
	}
	if (copy_from_user(dd_config, argp, mic_desc_size(&dd))) {
		ret = -EFAULT;
		dev_err(mic_dev(mvdev), "%s %d err %d\n",
			__func__, __LINE__, ret);
		goto exit;
	}

	vqconfig = mic_vq_config(dd_config);
	for (i = 0; i < dd.num_vq; i++) {
		if (le16_to_cpu(vqconfig[i].num) > MIC_MAX_VRING_ENTRIES) {
			ret =  -EINVAL;
			dev_err(mic_dev(mvdev), "%s %d err %d\n",
				__func__, __LINE__, ret);
			goto exit;
		}
	}

	/* Find the first free device page entry */
	for (i = sizeof(struct mic_bootparam);
		i < MIC_DP_SIZE - mic_total_desc_size(dd_config);
		i += mic_total_desc_size(devp)) {
		devp = mdev->dp + i;
		if (devp->type == 0 || devp->type == -1) {
			slot_found = true;
			break;
		}
	}
	if (!slot_found) {
		ret =  -EINVAL;
		dev_err(mic_dev(mvdev), "%s %d err %d\n",
			__func__, __LINE__, ret);
		goto exit;
	}
	/*
	 * Save off the type before doing the memcpy. Type will be set in the
	 * end after completing all initialization for the new device.
	 */
	*type = dd_config->type;
	dd_config->type = 0;
	memcpy(devp, dd_config, mic_desc_size(dd_config));

	*devpage = devp;
exit:
	kfree(dd_config);
	return ret;
}

static void mic_init_device_ctrl(struct mic_vdev *mvdev,
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
	mvdev->dc = dc;
}

int mic_virtio_add_device(struct mic_vdev *mvdev,
			void __user *argp)
{
	struct mic_device *mdev = mvdev->mdev;
	struct mic_device_desc *dd = NULL;
	struct mic_vqconfig *vqconfig;
	int vr_size, i, j, ret;
	u8 type = 0;
	s8 db;
	char irqname[10];
	struct mic_bootparam *bootparam = mdev->dp;
	u16 num;
	dma_addr_t vr_addr;

	mutex_lock(&mdev->mic_mutex);

	ret = mic_copy_dp_entry(mvdev, argp, &type, &dd);
	if (ret) {
		mutex_unlock(&mdev->mic_mutex);
		return ret;
	}

	mic_init_device_ctrl(mvdev, dd);

	mvdev->dd = dd;
	mvdev->virtio_id = type;
	vqconfig = mic_vq_config(dd);
	INIT_WORK(&mvdev->virtio_bh_work, mic_bh_handler);

	for (i = 0; i < dd->num_vq; i++) {
		struct mic_vringh *mvr = &mvdev->mvr[i];
		struct mic_vring *vr = &mvdev->mvr[i].vring;
		num = le16_to_cpu(vqconfig[i].num);
		mutex_init(&mvr->vr_mutex);
		vr_size = PAGE_ALIGN(vring_size(num, MIC_VIRTIO_RING_ALIGN) +
			sizeof(struct _mic_vring_info));
		vr->va = (void *)
			__get_free_pages(GFP_KERNEL | __GFP_ZERO,
					 get_order(vr_size));
		if (!vr->va) {
			ret = -ENOMEM;
			dev_err(mic_dev(mvdev), "%s %d err %d\n",
				__func__, __LINE__, ret);
			goto err;
		}
		vr->len = vr_size;
		vr->info = vr->va + vring_size(num, MIC_VIRTIO_RING_ALIGN);
		vr->info->magic = cpu_to_le32(MIC_MAGIC + mvdev->virtio_id + i);
		vr_addr = mic_map_single(mdev, vr->va, vr_size);
		if (mic_map_error(vr_addr)) {
			free_pages((unsigned long)vr->va, get_order(vr_size));
			ret = -ENOMEM;
			dev_err(mic_dev(mvdev), "%s %d err %d\n",
				__func__, __LINE__, ret);
			goto err;
		}
		vqconfig[i].address = cpu_to_le64(vr_addr);

		vring_init(&vr->vr, num, vr->va, MIC_VIRTIO_RING_ALIGN);
		ret = vringh_init_kern(&mvr->vrh,
			*(u32 *)mic_vq_features(mvdev->dd), num, false,
			vr->vr.desc, vr->vr.avail, vr->vr.used);
		if (ret) {
			dev_err(mic_dev(mvdev), "%s %d err %d\n",
				__func__, __LINE__, ret);
			goto err;
		}
		vringh_kiov_init(&mvr->riov, NULL, 0);
		vringh_kiov_init(&mvr->wiov, NULL, 0);
		mvr->head = USHRT_MAX;
		mvr->mvdev = mvdev;
		mvr->vrh.notify = mic_notify;
		dev_dbg(mdev->sdev->parent,
			"%s %d index %d va %p info %p vr_size 0x%x\n",
			__func__, __LINE__, i, vr->va, vr->info, vr_size);
	}

	snprintf(irqname, sizeof(irqname), "mic%dvirtio%d", mdev->id,
		 mvdev->virtio_id);
	mvdev->virtio_db = mic_next_db(mdev);
	mvdev->virtio_cookie = mic_request_irq(mdev, mic_virtio_intr_handler,
			irqname, mvdev, mvdev->virtio_db, MIC_INTR_DB);
	if (IS_ERR(mvdev->virtio_cookie)) {
		ret = PTR_ERR(mvdev->virtio_cookie);
		dev_dbg(mdev->sdev->parent, "request irq failed\n");
		goto err;
	}

	mvdev->dc->c2h_vdev_db = mvdev->virtio_db;

	list_add_tail(&mvdev->list, &mdev->vdev_list);
	/*
	 * Order the type update with previous stores. This write barrier
	 * is paired with the corresponding read barrier before the uncached
	 * system memory read of the type, on the card while scanning the
	 * device page.
	 */
	smp_wmb();
	dd->type = type;

	dev_dbg(mdev->sdev->parent, "Added virtio device id %d\n", dd->type);

	db = bootparam->h2c_config_db;
	if (db != -1)
		mdev->ops->send_intr(mdev, db);
	mutex_unlock(&mdev->mic_mutex);
	return 0;
err:
	vqconfig = mic_vq_config(dd);
	for (j = 0; j < i; j++) {
		struct mic_vringh *mvr = &mvdev->mvr[j];
		mic_unmap_single(mdev, le64_to_cpu(vqconfig[j].address),
				 mvr->vring.len);
		free_pages((unsigned long)mvr->vring.va,
			   get_order(mvr->vring.len));
	}
	mutex_unlock(&mdev->mic_mutex);
	return ret;
}

void mic_virtio_del_device(struct mic_vdev *mvdev)
{
	struct list_head *pos, *tmp;
	struct mic_vdev *tmp_mvdev;
	struct mic_device *mdev = mvdev->mdev;
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(wake);
	int i, ret, retry;
	struct mic_vqconfig *vqconfig;
	struct mic_bootparam *bootparam = mdev->dp;
	s8 db;

	mutex_lock(&mdev->mic_mutex);
	db = bootparam->h2c_config_db;
	if (db == -1)
		goto skip_hot_remove;
	dev_dbg(mdev->sdev->parent,
		"Requesting hot remove id %d\n", mvdev->virtio_id);
	mvdev->dc->config_change = MIC_VIRTIO_PARAM_DEV_REMOVE;
	mdev->ops->send_intr(mdev, db);
	for (retry = 100; retry--;) {
		ret = wait_event_timeout(wake,
			mvdev->dc->guest_ack, msecs_to_jiffies(100));
		if (ret)
			break;
	}
	dev_dbg(mdev->sdev->parent,
		"Device id %d config_change %d guest_ack %d retry %d\n",
		mvdev->virtio_id, mvdev->dc->config_change,
		mvdev->dc->guest_ack, retry);
	mvdev->dc->config_change = 0;
	mvdev->dc->guest_ack = 0;
skip_hot_remove:
	mic_free_irq(mdev, mvdev->virtio_cookie, mvdev);
	flush_work(&mvdev->virtio_bh_work);
	vqconfig = mic_vq_config(mvdev->dd);
	for (i = 0; i < mvdev->dd->num_vq; i++) {
		struct mic_vringh *mvr = &mvdev->mvr[i];
		vringh_kiov_cleanup(&mvr->riov);
		vringh_kiov_cleanup(&mvr->wiov);
		mic_unmap_single(mdev, le64_to_cpu(vqconfig[i].address),
				 mvr->vring.len);
		free_pages((unsigned long)mvr->vring.va,
			   get_order(mvr->vring.len));
	}

	list_for_each_safe(pos, tmp, &mdev->vdev_list) {
		tmp_mvdev = list_entry(pos, struct mic_vdev, list);
		if (tmp_mvdev == mvdev) {
			list_del(pos);
			dev_dbg(mdev->sdev->parent,
				"Removing virtio device id %d\n",
				mvdev->virtio_id);
			break;
		}
	}
	/*
	 * Order the type update with previous stores. This write barrier
	 * is paired with the corresponding read barrier before the uncached
	 * system memory read of the type, on the card while scanning the
	 * device page.
	 */
	smp_wmb();
	mvdev->dd->type = -1;
	mutex_unlock(&mdev->mic_mutex);
}
