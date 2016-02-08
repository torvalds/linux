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
#ifndef _VOP_MAIN_H_
#define _VOP_MAIN_H_

#include <linux/vringh.h>
#include <linux/virtio_config.h>
#include <linux/virtio.h>
#include <linux/miscdevice.h>

#include <linux/mic_common.h>
#include "../common/mic_dev.h"

#include "../bus/vop_bus.h"

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

/*
 * vop_info - Allocated per invocation of VOP probe
 *
 * @vpdev: VOP device
 * @hotplug_work: Handle virtio device creation, deletion and configuration
 * @cookie: Cookie received upon requesting a virtio configuration interrupt
 * @h2c_config_db: The doorbell used by the peer to indicate a config change
 * @vdev_list: List of "active" virtio devices injected in the peer node
 * @vop_mutex: Synchronize access to the device page as well as serialize
 *             creation/deletion of virtio devices on the peer node
 * @dp: Peer device page information
 * @dbg: Debugfs entry
 * @dma_ch: The DMA channel used by this transport for data transfers.
 * @name: Name for this transport used in misc device creation.
 * @miscdev: The misc device registered.
 */
struct vop_info {
	struct vop_device *vpdev;
	struct work_struct hotplug_work;
	struct mic_irq *cookie;
	int h2c_config_db;
	struct list_head vdev_list;
	struct mutex vop_mutex;
	void __iomem *dp;
	struct dentry *dbg;
	struct dma_chan *dma_ch;
	char name[16];
	struct miscdevice miscdev;
};

/**
 * struct vop_vringh - Virtio ring host information.
 *
 * @vring: The VOP vring used for setting up user space mappings.
 * @vrh: The host VRINGH used for accessing the card vrings.
 * @riov: The VRINGH read kernel IOV.
 * @wiov: The VRINGH write kernel IOV.
 * @head: The VRINGH head index address passed to vringh_getdesc_kern(..).
 * @vr_mutex: Mutex for synchronizing access to the VRING.
 * @buf: Temporary kernel buffer used to copy in/out data
 * from/to the card via DMA.
 * @buf_da: dma address of buf.
 * @vdev: Back pointer to VOP virtio device for vringh_notify(..).
 */
struct vop_vringh {
	struct mic_vring vring;
	struct vringh vrh;
	struct vringh_kiov riov;
	struct vringh_kiov wiov;
	u16 head;
	struct mutex vr_mutex;
	void *buf;
	dma_addr_t buf_da;
	struct vop_vdev *vdev;
};

/**
 * struct vop_vdev - Host information for a card Virtio device.
 *
 * @virtio_id - Virtio device id.
 * @waitq - Waitqueue to allow ring3 apps to poll.
 * @vpdev - pointer to VOP bus device.
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
 * @vvr - Store per VRING data structures.
 * @virtio_bh_work - Work struct used to schedule virtio bottom half handling.
 * @dd - Virtio device descriptor.
 * @dc - Virtio device control fields.
 * @list - List of Virtio devices.
 * @virtio_db - The doorbell used by the card to interrupt the host.
 * @virtio_cookie - The cookie returned while requesting interrupts.
 * @vi: Transport information.
 * @vdev_mutex: Mutex synchronizing virtio device injection,
 *              removal and data transfers.
 * @destroy: Track if a virtio device is being destroyed.
 * @deleted: The virtio device has been deleted.
 */
struct vop_vdev {
	int virtio_id;
	wait_queue_head_t waitq;
	struct vop_device *vpdev;
	int poll_wake;
	unsigned long out_bytes;
	unsigned long in_bytes;
	unsigned long out_bytes_dma;
	unsigned long in_bytes_dma;
	unsigned long tx_len_unaligned;
	unsigned long tx_dst_unaligned;
	unsigned long rx_dst_unaligned;
	struct vop_vringh vvr[MIC_MAX_VRINGS];
	struct work_struct virtio_bh_work;
	struct mic_device_desc *dd;
	struct mic_device_ctrl *dc;
	struct list_head list;
	int virtio_db;
	struct mic_irq *virtio_cookie;
	struct vop_info *vi;
	struct mutex vdev_mutex;
	struct completion destroy;
	bool deleted;
};

/* Helper API to check if a virtio device is running */
static inline bool vop_vdevup(struct vop_vdev *vdev)
{
	return !!vdev->dd->status;
}

void vop_init_debugfs(struct vop_info *vi);
void vop_exit_debugfs(struct vop_info *vi);
int vop_host_init(struct vop_info *vi);
void vop_host_uninit(struct vop_info *vi);
#endif
