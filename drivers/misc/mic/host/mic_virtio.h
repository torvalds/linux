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
#ifndef MIC_VIRTIO_H
#define MIC_VIRTIO_H

#include <linux/virtio_config.h>
#include <linux/mic_ioctl.h>

/*
 * Note on endianness.
 * 1. Host can be both BE or LE
 * 2. Guest/card is LE. Host uses le_to_cpu to access desc/avail
 *    rings and ioreadXX/iowriteXX to access used ring.
 * 3. Device page exposed by host to guest contains LE values. Guest
 *    accesses these using ioreadXX/iowriteXX etc. This way in general we
 *    obey the virtio spec according to which guest works with native
 *    endianness and host is aware of guest endianness and does all
 *    required endianness conversion.
 * 4. Data provided from user space to guest (in ADD_DEVICE and
 *    CONFIG_CHANGE ioctl's) is not interpreted by the driver and should be
 *    in guest endianness.
 */

/**
 * struct mic_vringh - Virtio ring host information.
 *
 * @vring: The MIC vring used for setting up user space mappings.
 * @vrh: The host VRINGH used for accessing the card vrings.
 * @riov: The VRINGH read kernel IOV.
 * @wiov: The VRINGH write kernel IOV.
 * @vr_mutex: Mutex for synchronizing access to the VRING.
 * @buf: Temporary kernel buffer used to copy in/out data
 * from/to the card via DMA.
 * @buf_da: dma address of buf.
 * @mvdev: Back pointer to MIC virtio device for vringh_notify(..).
 * @head: The VRINGH head index address passed to vringh_getdesc_kern(..).
 */
struct mic_vringh {
	struct mic_vring vring;
	struct vringh vrh;
	struct vringh_kiov riov;
	struct vringh_kiov wiov;
	struct mutex vr_mutex;
	void *buf;
	dma_addr_t buf_da;
	struct mic_vdev *mvdev;
	u16 head;
};

/**
 * struct mic_vdev - Host information for a card Virtio device.
 *
 * @virtio_id - Virtio device id.
 * @waitq - Waitqueue to allow ring3 apps to poll.
 * @mdev - Back pointer to host MIC device.
 * @poll_wake - Used for waking up threads blocked in poll.
 * @out_bytes - Debug stats for number of bytes copied from host to card.
 * @in_bytes - Debug stats for number of bytes copied from card to host.
 * @out_bytes_dma - Debug stats for number of bytes copied from host to card
 * using DMA.
 * @in_bytes_dma - Debug stats for number of bytes copied from card to host
 * using DMA.
 * @tx_len_unaligned - Debug stats for number of bytes copied to the card where
 * the transfer length did not have the required DMA alignment.
 * @tx_dst_unaligned - Debug stats for number of bytes copied where the
 * destination address on the card did not have the required DMA alignment.
 * @mvr - Store per VRING data structures.
 * @virtio_bh_work - Work struct used to schedule virtio bottom half handling.
 * @dd - Virtio device descriptor.
 * @dc - Virtio device control fields.
 * @list - List of Virtio devices.
 * @virtio_db - The doorbell used by the card to interrupt the host.
 * @virtio_cookie - The cookie returned while requesting interrupts.
 */
struct mic_vdev {
	int virtio_id;
	wait_queue_head_t waitq;
	struct mic_device *mdev;
	int poll_wake;
	unsigned long out_bytes;
	unsigned long in_bytes;
	unsigned long out_bytes_dma;
	unsigned long in_bytes_dma;
	unsigned long tx_len_unaligned;
	unsigned long tx_dst_unaligned;
	struct mic_vringh mvr[MIC_MAX_VRINGS];
	struct work_struct virtio_bh_work;
	struct mic_device_desc *dd;
	struct mic_device_ctrl *dc;
	struct list_head list;
	int virtio_db;
	struct mic_irq *virtio_cookie;
};

void mic_virtio_uninit(struct mic_device *mdev);
int mic_virtio_add_device(struct mic_vdev *mvdev,
			void __user *argp);
void mic_virtio_del_device(struct mic_vdev *mvdev);
int mic_virtio_config_change(struct mic_vdev *mvdev,
			void __user *argp);
int mic_virtio_copy_desc(struct mic_vdev *mvdev,
	struct mic_copy_desc *request);
void mic_virtio_reset_devices(struct mic_device *mdev);
void mic_bh_handler(struct work_struct *work);

/* Helper API to obtain the MIC PCIe device */
static inline struct device *mic_dev(struct mic_vdev *mvdev)
{
	return &mvdev->mdev->pdev->dev;
}

/* Helper API to check if a virtio device is initialized */
static inline int mic_vdev_inited(struct mic_vdev *mvdev)
{
	/* Device has not been created yet */
	if (!mvdev->dd || !mvdev->dd->type) {
		dev_err(mic_dev(mvdev), "%s %d err %d\n",
			__func__, __LINE__, -EINVAL);
		return -EINVAL;
	}

	/* Device has been removed/deleted */
	if (mvdev->dd->type == -1) {
		dev_err(mic_dev(mvdev), "%s %d err %d\n",
			__func__, __LINE__, -ENODEV);
		return -ENODEV;
	}

	return 0;
}

/* Helper API to check if a virtio device is running */
static inline bool mic_vdevup(struct mic_vdev *mvdev)
{
	return !!mvdev->dd->status;
}
#endif
