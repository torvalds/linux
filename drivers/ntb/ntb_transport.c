/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 *   redistributing this file, you may do so under either license.
 *
 *   GPL LICENSE SUMMARY
 *
 *   Copyright(c) 2012 Intel Corporation. All rights reserved.
 *   Copyright (C) 2015 EMC Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of version 2 of the GNU General Public License as
 *   published by the Free Software Foundation.
 *
 *   BSD LICENSE
 *
 *   Copyright(c) 2012 Intel Corporation. All rights reserved.
 *   Copyright (C) 2015 EMC Corporation. All Rights Reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copy
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * PCIe NTB Transport Linux driver
 *
 * Contact Information:
 * Jon Mason <jon.mason@intel.com>
 */
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include "linux/ntb.h"
#include "linux/ntb_transport.h"

#define NTB_TRANSPORT_VERSION	4
#define NTB_TRANSPORT_VER	"4"
#define NTB_TRANSPORT_NAME	"ntb_transport"
#define NTB_TRANSPORT_DESC	"Software Queue-Pair Transport over NTB"

MODULE_DESCRIPTION(NTB_TRANSPORT_DESC);
MODULE_VERSION(NTB_TRANSPORT_VER);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Intel Corporation");

static unsigned long max_mw_size;
module_param(max_mw_size, ulong, 0644);
MODULE_PARM_DESC(max_mw_size, "Limit size of large memory windows");

static unsigned int transport_mtu = 0x10000;
module_param(transport_mtu, uint, 0644);
MODULE_PARM_DESC(transport_mtu, "Maximum size of NTB transport packets");

static unsigned char max_num_clients;
module_param(max_num_clients, byte, 0644);
MODULE_PARM_DESC(max_num_clients, "Maximum number of NTB transport clients");

static unsigned int copy_bytes = 1024;
module_param(copy_bytes, uint, 0644);
MODULE_PARM_DESC(copy_bytes, "Threshold under which NTB will use the CPU to copy instead of DMA");

static bool use_dma;
module_param(use_dma, bool, 0644);
MODULE_PARM_DESC(use_dma, "Use DMA engine to perform large data copy");

static struct dentry *nt_debugfs_dir;

struct ntb_queue_entry {
	/* ntb_queue list reference */
	struct list_head entry;
	/* pointers to data to be transferred */
	void *cb_data;
	void *buf;
	unsigned int len;
	unsigned int flags;
	int retries;
	int errors;
	unsigned int tx_index;
	unsigned int rx_index;

	struct ntb_transport_qp *qp;
	union {
		struct ntb_payload_header __iomem *tx_hdr;
		struct ntb_payload_header *rx_hdr;
	};
};

struct ntb_rx_info {
	unsigned int entry;
};

struct ntb_transport_qp {
	struct ntb_transport_ctx *transport;
	struct ntb_dev *ndev;
	void *cb_data;
	struct dma_chan *tx_dma_chan;
	struct dma_chan *rx_dma_chan;

	bool client_ready;
	bool link_is_up;
	bool active;

	u8 qp_num;	/* Only 64 QP's are allowed.  0-63 */
	u64 qp_bit;

	struct ntb_rx_info __iomem *rx_info;
	struct ntb_rx_info *remote_rx_info;

	void (*tx_handler)(struct ntb_transport_qp *qp, void *qp_data,
			   void *data, int len);
	struct list_head tx_free_q;
	spinlock_t ntb_tx_free_q_lock;
	void __iomem *tx_mw;
	dma_addr_t tx_mw_phys;
	unsigned int tx_index;
	unsigned int tx_max_entry;
	unsigned int tx_max_frame;

	void (*rx_handler)(struct ntb_transport_qp *qp, void *qp_data,
			   void *data, int len);
	struct list_head rx_post_q;
	struct list_head rx_pend_q;
	struct list_head rx_free_q;
	/* ntb_rx_q_lock: synchronize access to rx_XXXX_q */
	spinlock_t ntb_rx_q_lock;
	void *rx_buff;
	unsigned int rx_index;
	unsigned int rx_max_entry;
	unsigned int rx_max_frame;
	unsigned int rx_alloc_entry;
	dma_cookie_t last_cookie;
	struct tasklet_struct rxc_db_work;

	void (*event_handler)(void *data, int status);
	struct delayed_work link_work;
	struct work_struct link_cleanup;

	struct dentry *debugfs_dir;
	struct dentry *debugfs_stats;

	/* Stats */
	u64 rx_bytes;
	u64 rx_pkts;
	u64 rx_ring_empty;
	u64 rx_err_no_buf;
	u64 rx_err_oflow;
	u64 rx_err_ver;
	u64 rx_memcpy;
	u64 rx_async;
	u64 dma_rx_prep_err;
	u64 tx_bytes;
	u64 tx_pkts;
	u64 tx_ring_full;
	u64 tx_err_no_buf;
	u64 tx_memcpy;
	u64 tx_async;
	u64 dma_tx_prep_err;
};

struct ntb_transport_mw {
	phys_addr_t phys_addr;
	resource_size_t phys_size;
	resource_size_t xlat_align;
	resource_size_t xlat_align_size;
	void __iomem *vbase;
	size_t xlat_size;
	size_t buff_size;
	void *virt_addr;
	dma_addr_t dma_addr;
};

struct ntb_transport_client_dev {
	struct list_head entry;
	struct ntb_transport_ctx *nt;
	struct device dev;
};

struct ntb_transport_ctx {
	struct list_head entry;
	struct list_head client_devs;

	struct ntb_dev *ndev;

	struct ntb_transport_mw *mw_vec;
	struct ntb_transport_qp *qp_vec;
	unsigned int mw_count;
	unsigned int qp_count;
	u64 qp_bitmap;
	u64 qp_bitmap_free;

	bool link_is_up;
	struct delayed_work link_work;
	struct work_struct link_cleanup;

	struct dentry *debugfs_node_dir;
};

enum {
	DESC_DONE_FLAG = BIT(0),
	LINK_DOWN_FLAG = BIT(1),
};

struct ntb_payload_header {
	unsigned int ver;
	unsigned int len;
	unsigned int flags;
};

enum {
	VERSION = 0,
	QP_LINKS,
	NUM_QPS,
	NUM_MWS,
	MW0_SZ_HIGH,
	MW0_SZ_LOW,
	MW1_SZ_HIGH,
	MW1_SZ_LOW,
	MAX_SPAD,
};

#define dev_client_dev(__dev) \
	container_of((__dev), struct ntb_transport_client_dev, dev)

#define drv_client(__drv) \
	container_of((__drv), struct ntb_transport_client, driver)

#define QP_TO_MW(nt, qp)	((qp) % nt->mw_count)
#define NTB_QP_DEF_NUM_ENTRIES	100
#define NTB_LINK_DOWN_TIMEOUT	10
#define DMA_RETRIES		20
#define DMA_OUT_RESOURCE_TO	msecs_to_jiffies(50)

static void ntb_transport_rxc_db(unsigned long data);
static const struct ntb_ctx_ops ntb_transport_ops;
static struct ntb_client ntb_transport_client;
static int ntb_async_tx_submit(struct ntb_transport_qp *qp,
			       struct ntb_queue_entry *entry);
static void ntb_memcpy_tx(struct ntb_queue_entry *entry, void __iomem *offset);
static int ntb_async_rx_submit(struct ntb_queue_entry *entry, void *offset);
static void ntb_memcpy_rx(struct ntb_queue_entry *entry, void *offset);


static int ntb_transport_bus_match(struct device *dev,
				   struct device_driver *drv)
{
	return !strncmp(dev_name(dev), drv->name, strlen(drv->name));
}

static int ntb_transport_bus_probe(struct device *dev)
{
	const struct ntb_transport_client *client;
	int rc = -EINVAL;

	get_device(dev);

	client = drv_client(dev->driver);
	rc = client->probe(dev);
	if (rc)
		put_device(dev);

	return rc;
}

static int ntb_transport_bus_remove(struct device *dev)
{
	const struct ntb_transport_client *client;

	client = drv_client(dev->driver);
	client->remove(dev);

	put_device(dev);

	return 0;
}

static struct bus_type ntb_transport_bus = {
	.name = "ntb_transport",
	.match = ntb_transport_bus_match,
	.probe = ntb_transport_bus_probe,
	.remove = ntb_transport_bus_remove,
};

static LIST_HEAD(ntb_transport_list);

static int ntb_bus_init(struct ntb_transport_ctx *nt)
{
	list_add_tail(&nt->entry, &ntb_transport_list);
	return 0;
}

static void ntb_bus_remove(struct ntb_transport_ctx *nt)
{
	struct ntb_transport_client_dev *client_dev, *cd;

	list_for_each_entry_safe(client_dev, cd, &nt->client_devs, entry) {
		dev_err(client_dev->dev.parent, "%s still attached to bus, removing\n",
			dev_name(&client_dev->dev));
		list_del(&client_dev->entry);
		device_unregister(&client_dev->dev);
	}

	list_del(&nt->entry);
}

static void ntb_transport_client_release(struct device *dev)
{
	struct ntb_transport_client_dev *client_dev;

	client_dev = dev_client_dev(dev);
	kfree(client_dev);
}

/**
 * ntb_transport_unregister_client_dev - Unregister NTB client device
 * @device_name: Name of NTB client device
 *
 * Unregister an NTB client device with the NTB transport layer
 */
void ntb_transport_unregister_client_dev(char *device_name)
{
	struct ntb_transport_client_dev *client, *cd;
	struct ntb_transport_ctx *nt;

	list_for_each_entry(nt, &ntb_transport_list, entry)
		list_for_each_entry_safe(client, cd, &nt->client_devs, entry)
			if (!strncmp(dev_name(&client->dev), device_name,
				     strlen(device_name))) {
				list_del(&client->entry);
				device_unregister(&client->dev);
			}
}
EXPORT_SYMBOL_GPL(ntb_transport_unregister_client_dev);

/**
 * ntb_transport_register_client_dev - Register NTB client device
 * @device_name: Name of NTB client device
 *
 * Register an NTB client device with the NTB transport layer
 */
int ntb_transport_register_client_dev(char *device_name)
{
	struct ntb_transport_client_dev *client_dev;
	struct ntb_transport_ctx *nt;
	int node;
	int rc, i = 0;

	if (list_empty(&ntb_transport_list))
		return -ENODEV;

	list_for_each_entry(nt, &ntb_transport_list, entry) {
		struct device *dev;

		node = dev_to_node(&nt->ndev->dev);

		client_dev = kzalloc_node(sizeof(*client_dev),
					  GFP_KERNEL, node);
		if (!client_dev) {
			rc = -ENOMEM;
			goto err;
		}

		dev = &client_dev->dev;

		/* setup and register client devices */
		dev_set_name(dev, "%s%d", device_name, i);
		dev->bus = &ntb_transport_bus;
		dev->release = ntb_transport_client_release;
		dev->parent = &nt->ndev->dev;

		rc = device_register(dev);
		if (rc) {
			kfree(client_dev);
			goto err;
		}

		list_add_tail(&client_dev->entry, &nt->client_devs);
		i++;
	}

	return 0;

err:
	ntb_transport_unregister_client_dev(device_name);

	return rc;
}
EXPORT_SYMBOL_GPL(ntb_transport_register_client_dev);

/**
 * ntb_transport_register_client - Register NTB client driver
 * @drv: NTB client driver to be registered
 *
 * Register an NTB client driver with the NTB transport layer
 *
 * RETURNS: An appropriate -ERRNO error value on error, or zero for success.
 */
int ntb_transport_register_client(struct ntb_transport_client *drv)
{
	drv->driver.bus = &ntb_transport_bus;

	if (list_empty(&ntb_transport_list))
		return -ENODEV;

	return driver_register(&drv->driver);
}
EXPORT_SYMBOL_GPL(ntb_transport_register_client);

/**
 * ntb_transport_unregister_client - Unregister NTB client driver
 * @drv: NTB client driver to be unregistered
 *
 * Unregister an NTB client driver with the NTB transport layer
 *
 * RETURNS: An appropriate -ERRNO error value on error, or zero for success.
 */
void ntb_transport_unregister_client(struct ntb_transport_client *drv)
{
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL_GPL(ntb_transport_unregister_client);

static ssize_t debugfs_read(struct file *filp, char __user *ubuf, size_t count,
			    loff_t *offp)
{
	struct ntb_transport_qp *qp;
	char *buf;
	ssize_t ret, out_offset, out_count;

	qp = filp->private_data;

	if (!qp || !qp->link_is_up)
		return 0;

	out_count = 1000;

	buf = kmalloc(out_count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	out_offset = 0;
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "\nNTB QP stats:\n\n");
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "rx_bytes - \t%llu\n", qp->rx_bytes);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "rx_pkts - \t%llu\n", qp->rx_pkts);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "rx_memcpy - \t%llu\n", qp->rx_memcpy);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "rx_async - \t%llu\n", qp->rx_async);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "rx_ring_empty - %llu\n", qp->rx_ring_empty);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "rx_err_no_buf - %llu\n", qp->rx_err_no_buf);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "rx_err_oflow - \t%llu\n", qp->rx_err_oflow);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "rx_err_ver - \t%llu\n", qp->rx_err_ver);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "rx_buff - \t0x%p\n", qp->rx_buff);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "rx_index - \t%u\n", qp->rx_index);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "rx_max_entry - \t%u\n", qp->rx_max_entry);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "rx_alloc_entry - \t%u\n\n", qp->rx_alloc_entry);

	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "tx_bytes - \t%llu\n", qp->tx_bytes);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "tx_pkts - \t%llu\n", qp->tx_pkts);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "tx_memcpy - \t%llu\n", qp->tx_memcpy);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "tx_async - \t%llu\n", qp->tx_async);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "tx_ring_full - \t%llu\n", qp->tx_ring_full);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "tx_err_no_buf - %llu\n", qp->tx_err_no_buf);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "tx_mw - \t0x%p\n", qp->tx_mw);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "tx_index (H) - \t%u\n", qp->tx_index);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "RRI (T) - \t%u\n",
			       qp->remote_rx_info->entry);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "tx_max_entry - \t%u\n", qp->tx_max_entry);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "free tx - \t%u\n",
			       ntb_transport_tx_free_entry(qp));
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "DMA tx prep err - \t%llu\n",
			       qp->dma_tx_prep_err);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "DMA rx prep err - \t%llu\n",
			       qp->dma_rx_prep_err);

	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "\n");
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "Using TX DMA - \t%s\n",
			       qp->tx_dma_chan ? "Yes" : "No");
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "Using RX DMA - \t%s\n",
			       qp->rx_dma_chan ? "Yes" : "No");
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "QP Link - \t%s\n",
			       qp->link_is_up ? "Up" : "Down");
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "\n");

	if (out_offset > out_count)
		out_offset = out_count;

	ret = simple_read_from_buffer(ubuf, count, offp, buf, out_offset);
	kfree(buf);
	return ret;
}

static const struct file_operations ntb_qp_debugfs_stats = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = debugfs_read,
};

static void ntb_list_add(spinlock_t *lock, struct list_head *entry,
			 struct list_head *list)
{
	unsigned long flags;

	spin_lock_irqsave(lock, flags);
	list_add_tail(entry, list);
	spin_unlock_irqrestore(lock, flags);
}

static struct ntb_queue_entry *ntb_list_rm(spinlock_t *lock,
					   struct list_head *list)
{
	struct ntb_queue_entry *entry;
	unsigned long flags;

	spin_lock_irqsave(lock, flags);
	if (list_empty(list)) {
		entry = NULL;
		goto out;
	}
	entry = list_first_entry(list, struct ntb_queue_entry, entry);
	list_del(&entry->entry);

out:
	spin_unlock_irqrestore(lock, flags);

	return entry;
}

static struct ntb_queue_entry *ntb_list_mv(spinlock_t *lock,
					   struct list_head *list,
					   struct list_head *to_list)
{
	struct ntb_queue_entry *entry;
	unsigned long flags;

	spin_lock_irqsave(lock, flags);

	if (list_empty(list)) {
		entry = NULL;
	} else {
		entry = list_first_entry(list, struct ntb_queue_entry, entry);
		list_move_tail(&entry->entry, to_list);
	}

	spin_unlock_irqrestore(lock, flags);

	return entry;
}

static int ntb_transport_setup_qp_mw(struct ntb_transport_ctx *nt,
				     unsigned int qp_num)
{
	struct ntb_transport_qp *qp = &nt->qp_vec[qp_num];
	struct ntb_transport_mw *mw;
	struct ntb_dev *ndev = nt->ndev;
	struct ntb_queue_entry *entry;
	unsigned int rx_size, num_qps_mw;
	unsigned int mw_num, mw_count, qp_count;
	unsigned int i;
	int node;

	mw_count = nt->mw_count;
	qp_count = nt->qp_count;

	mw_num = QP_TO_MW(nt, qp_num);
	mw = &nt->mw_vec[mw_num];

	if (!mw->virt_addr)
		return -ENOMEM;

	if (qp_count % mw_count && mw_num + 1 < qp_count / mw_count)
		num_qps_mw = qp_count / mw_count + 1;
	else
		num_qps_mw = qp_count / mw_count;

	rx_size = (unsigned int)mw->xlat_size / num_qps_mw;
	qp->rx_buff = mw->virt_addr + rx_size * (qp_num / mw_count);
	rx_size -= sizeof(struct ntb_rx_info);

	qp->remote_rx_info = qp->rx_buff + rx_size;

	/* Due to housekeeping, there must be atleast 2 buffs */
	qp->rx_max_frame = min(transport_mtu, rx_size / 2);
	qp->rx_max_entry = rx_size / qp->rx_max_frame;
	qp->rx_index = 0;

	/*
	 * Checking to see if we have more entries than the default.
	 * We should add additional entries if that is the case so we
	 * can be in sync with the transport frames.
	 */
	node = dev_to_node(&ndev->dev);
	for (i = qp->rx_alloc_entry; i < qp->rx_max_entry; i++) {
		entry = kzalloc_node(sizeof(*entry), GFP_ATOMIC, node);
		if (!entry)
			return -ENOMEM;

		entry->qp = qp;
		ntb_list_add(&qp->ntb_rx_q_lock, &entry->entry,
			     &qp->rx_free_q);
		qp->rx_alloc_entry++;
	}

	qp->remote_rx_info->entry = qp->rx_max_entry - 1;

	/* setup the hdr offsets with 0's */
	for (i = 0; i < qp->rx_max_entry; i++) {
		void *offset = (qp->rx_buff + qp->rx_max_frame * (i + 1) -
				sizeof(struct ntb_payload_header));
		memset(offset, 0, sizeof(struct ntb_payload_header));
	}

	qp->rx_pkts = 0;
	qp->tx_pkts = 0;
	qp->tx_index = 0;

	return 0;
}

static void ntb_free_mw(struct ntb_transport_ctx *nt, int num_mw)
{
	struct ntb_transport_mw *mw = &nt->mw_vec[num_mw];
	struct pci_dev *pdev = nt->ndev->pdev;

	if (!mw->virt_addr)
		return;

	ntb_mw_clear_trans(nt->ndev, num_mw);
	dma_free_coherent(&pdev->dev, mw->buff_size,
			  mw->virt_addr, mw->dma_addr);
	mw->xlat_size = 0;
	mw->buff_size = 0;
	mw->virt_addr = NULL;
}

static int ntb_set_mw(struct ntb_transport_ctx *nt, int num_mw,
		      resource_size_t size)
{
	struct ntb_transport_mw *mw = &nt->mw_vec[num_mw];
	struct pci_dev *pdev = nt->ndev->pdev;
	size_t xlat_size, buff_size;
	int rc;

	if (!size)
		return -EINVAL;

	xlat_size = round_up(size, mw->xlat_align_size);
	buff_size = round_up(size, mw->xlat_align);

	/* No need to re-setup */
	if (mw->xlat_size == xlat_size)
		return 0;

	if (mw->buff_size)
		ntb_free_mw(nt, num_mw);

	/* Alloc memory for receiving data.  Must be aligned */
	mw->xlat_size = xlat_size;
	mw->buff_size = buff_size;

	mw->virt_addr = dma_alloc_coherent(&pdev->dev, buff_size,
					   &mw->dma_addr, GFP_KERNEL);
	if (!mw->virt_addr) {
		mw->xlat_size = 0;
		mw->buff_size = 0;
		dev_err(&pdev->dev, "Unable to alloc MW buff of size %zu\n",
			buff_size);
		return -ENOMEM;
	}

	/*
	 * we must ensure that the memory address allocated is BAR size
	 * aligned in order for the XLAT register to take the value. This
	 * is a requirement of the hardware. It is recommended to setup CMA
	 * for BAR sizes equal or greater than 4MB.
	 */
	if (!IS_ALIGNED(mw->dma_addr, mw->xlat_align)) {
		dev_err(&pdev->dev, "DMA memory %pad is not aligned\n",
			&mw->dma_addr);
		ntb_free_mw(nt, num_mw);
		return -ENOMEM;
	}

	/* Notify HW the memory location of the receive buffer */
	rc = ntb_mw_set_trans(nt->ndev, num_mw, mw->dma_addr, mw->xlat_size);
	if (rc) {
		dev_err(&pdev->dev, "Unable to set mw%d translation", num_mw);
		ntb_free_mw(nt, num_mw);
		return -EIO;
	}

	return 0;
}

static void ntb_qp_link_down_reset(struct ntb_transport_qp *qp)
{
	qp->link_is_up = false;
	qp->active = false;

	qp->tx_index = 0;
	qp->rx_index = 0;
	qp->rx_bytes = 0;
	qp->rx_pkts = 0;
	qp->rx_ring_empty = 0;
	qp->rx_err_no_buf = 0;
	qp->rx_err_oflow = 0;
	qp->rx_err_ver = 0;
	qp->rx_memcpy = 0;
	qp->rx_async = 0;
	qp->tx_bytes = 0;
	qp->tx_pkts = 0;
	qp->tx_ring_full = 0;
	qp->tx_err_no_buf = 0;
	qp->tx_memcpy = 0;
	qp->tx_async = 0;
	qp->dma_tx_prep_err = 0;
	qp->dma_rx_prep_err = 0;
}

static void ntb_qp_link_cleanup(struct ntb_transport_qp *qp)
{
	struct ntb_transport_ctx *nt = qp->transport;
	struct pci_dev *pdev = nt->ndev->pdev;

	dev_info(&pdev->dev, "qp %d: Link Cleanup\n", qp->qp_num);

	cancel_delayed_work_sync(&qp->link_work);
	ntb_qp_link_down_reset(qp);

	if (qp->event_handler)
		qp->event_handler(qp->cb_data, qp->link_is_up);
}

static void ntb_qp_link_cleanup_work(struct work_struct *work)
{
	struct ntb_transport_qp *qp = container_of(work,
						   struct ntb_transport_qp,
						   link_cleanup);
	struct ntb_transport_ctx *nt = qp->transport;

	ntb_qp_link_cleanup(qp);

	if (nt->link_is_up)
		schedule_delayed_work(&qp->link_work,
				      msecs_to_jiffies(NTB_LINK_DOWN_TIMEOUT));
}

static void ntb_qp_link_down(struct ntb_transport_qp *qp)
{
	schedule_work(&qp->link_cleanup);
}

static void ntb_transport_link_cleanup(struct ntb_transport_ctx *nt)
{
	struct ntb_transport_qp *qp;
	u64 qp_bitmap_alloc;
	int i;

	qp_bitmap_alloc = nt->qp_bitmap & ~nt->qp_bitmap_free;

	/* Pass along the info to any clients */
	for (i = 0; i < nt->qp_count; i++)
		if (qp_bitmap_alloc & BIT_ULL(i)) {
			qp = &nt->qp_vec[i];
			ntb_qp_link_cleanup(qp);
			cancel_work_sync(&qp->link_cleanup);
			cancel_delayed_work_sync(&qp->link_work);
		}

	if (!nt->link_is_up)
		cancel_delayed_work_sync(&nt->link_work);

	/* The scratchpad registers keep the values if the remote side
	 * goes down, blast them now to give them a sane value the next
	 * time they are accessed
	 */
	for (i = 0; i < MAX_SPAD; i++)
		ntb_spad_write(nt->ndev, i, 0);
}

static void ntb_transport_link_cleanup_work(struct work_struct *work)
{
	struct ntb_transport_ctx *nt =
		container_of(work, struct ntb_transport_ctx, link_cleanup);

	ntb_transport_link_cleanup(nt);
}

static void ntb_transport_event_callback(void *data)
{
	struct ntb_transport_ctx *nt = data;

	if (ntb_link_is_up(nt->ndev, NULL, NULL) == 1)
		schedule_delayed_work(&nt->link_work, 0);
	else
		schedule_work(&nt->link_cleanup);
}

static void ntb_transport_link_work(struct work_struct *work)
{
	struct ntb_transport_ctx *nt =
		container_of(work, struct ntb_transport_ctx, link_work.work);
	struct ntb_dev *ndev = nt->ndev;
	struct pci_dev *pdev = ndev->pdev;
	resource_size_t size;
	u32 val;
	int rc = 0, i, spad;

	/* send the local info, in the opposite order of the way we read it */
	for (i = 0; i < nt->mw_count; i++) {
		size = nt->mw_vec[i].phys_size;

		if (max_mw_size && size > max_mw_size)
			size = max_mw_size;

		spad = MW0_SZ_HIGH + (i * 2);
		ntb_peer_spad_write(ndev, spad, upper_32_bits(size));

		spad = MW0_SZ_LOW + (i * 2);
		ntb_peer_spad_write(ndev, spad, lower_32_bits(size));
	}

	ntb_peer_spad_write(ndev, NUM_MWS, nt->mw_count);

	ntb_peer_spad_write(ndev, NUM_QPS, nt->qp_count);

	ntb_peer_spad_write(ndev, VERSION, NTB_TRANSPORT_VERSION);

	/* Query the remote side for its info */
	val = ntb_spad_read(ndev, VERSION);
	dev_dbg(&pdev->dev, "Remote version = %d\n", val);
	if (val != NTB_TRANSPORT_VERSION)
		goto out;

	val = ntb_spad_read(ndev, NUM_QPS);
	dev_dbg(&pdev->dev, "Remote max number of qps = %d\n", val);
	if (val != nt->qp_count)
		goto out;

	val = ntb_spad_read(ndev, NUM_MWS);
	dev_dbg(&pdev->dev, "Remote number of mws = %d\n", val);
	if (val != nt->mw_count)
		goto out;

	for (i = 0; i < nt->mw_count; i++) {
		u64 val64;

		val = ntb_spad_read(ndev, MW0_SZ_HIGH + (i * 2));
		val64 = (u64)val << 32;

		val = ntb_spad_read(ndev, MW0_SZ_LOW + (i * 2));
		val64 |= val;

		dev_dbg(&pdev->dev, "Remote MW%d size = %#llx\n", i, val64);

		rc = ntb_set_mw(nt, i, val64);
		if (rc)
			goto out1;
	}

	nt->link_is_up = true;

	for (i = 0; i < nt->qp_count; i++) {
		struct ntb_transport_qp *qp = &nt->qp_vec[i];

		ntb_transport_setup_qp_mw(nt, i);

		if (qp->client_ready)
			schedule_delayed_work(&qp->link_work, 0);
	}

	return;

out1:
	for (i = 0; i < nt->mw_count; i++)
		ntb_free_mw(nt, i);

	/* if there's an actual failure, we should just bail */
	if (rc < 0) {
		ntb_link_disable(ndev);
		return;
	}

out:
	if (ntb_link_is_up(ndev, NULL, NULL) == 1)
		schedule_delayed_work(&nt->link_work,
				      msecs_to_jiffies(NTB_LINK_DOWN_TIMEOUT));
}

static void ntb_qp_link_work(struct work_struct *work)
{
	struct ntb_transport_qp *qp = container_of(work,
						   struct ntb_transport_qp,
						   link_work.work);
	struct pci_dev *pdev = qp->ndev->pdev;
	struct ntb_transport_ctx *nt = qp->transport;
	int val;

	WARN_ON(!nt->link_is_up);

	val = ntb_spad_read(nt->ndev, QP_LINKS);

	ntb_peer_spad_write(nt->ndev, QP_LINKS, val | BIT(qp->qp_num));

	/* query remote spad for qp ready bits */
	ntb_peer_spad_read(nt->ndev, QP_LINKS);
	dev_dbg_ratelimited(&pdev->dev, "Remote QP link status = %x\n", val);

	/* See if the remote side is up */
	if (val & BIT(qp->qp_num)) {
		dev_info(&pdev->dev, "qp %d: Link Up\n", qp->qp_num);
		qp->link_is_up = true;
		qp->active = true;

		if (qp->event_handler)
			qp->event_handler(qp->cb_data, qp->link_is_up);

		if (qp->active)
			tasklet_schedule(&qp->rxc_db_work);
	} else if (nt->link_is_up)
		schedule_delayed_work(&qp->link_work,
				      msecs_to_jiffies(NTB_LINK_DOWN_TIMEOUT));
}

static int ntb_transport_init_queue(struct ntb_transport_ctx *nt,
				    unsigned int qp_num)
{
	struct ntb_transport_qp *qp;
	phys_addr_t mw_base;
	resource_size_t mw_size;
	unsigned int num_qps_mw, tx_size;
	unsigned int mw_num, mw_count, qp_count;
	u64 qp_offset;

	mw_count = nt->mw_count;
	qp_count = nt->qp_count;

	mw_num = QP_TO_MW(nt, qp_num);

	qp = &nt->qp_vec[qp_num];
	qp->qp_num = qp_num;
	qp->transport = nt;
	qp->ndev = nt->ndev;
	qp->client_ready = false;
	qp->event_handler = NULL;
	ntb_qp_link_down_reset(qp);

	if (qp_count % mw_count && mw_num + 1 < qp_count / mw_count)
		num_qps_mw = qp_count / mw_count + 1;
	else
		num_qps_mw = qp_count / mw_count;

	mw_base = nt->mw_vec[mw_num].phys_addr;
	mw_size = nt->mw_vec[mw_num].phys_size;

	tx_size = (unsigned int)mw_size / num_qps_mw;
	qp_offset = tx_size * (qp_num / mw_count);

	qp->tx_mw = nt->mw_vec[mw_num].vbase + qp_offset;
	if (!qp->tx_mw)
		return -EINVAL;

	qp->tx_mw_phys = mw_base + qp_offset;
	if (!qp->tx_mw_phys)
		return -EINVAL;

	tx_size -= sizeof(struct ntb_rx_info);
	qp->rx_info = qp->tx_mw + tx_size;

	/* Due to housekeeping, there must be atleast 2 buffs */
	qp->tx_max_frame = min(transport_mtu, tx_size / 2);
	qp->tx_max_entry = tx_size / qp->tx_max_frame;

	if (nt->debugfs_node_dir) {
		char debugfs_name[4];

		snprintf(debugfs_name, 4, "qp%d", qp_num);
		qp->debugfs_dir = debugfs_create_dir(debugfs_name,
						     nt->debugfs_node_dir);

		qp->debugfs_stats = debugfs_create_file("stats", S_IRUSR,
							qp->debugfs_dir, qp,
							&ntb_qp_debugfs_stats);
	} else {
		qp->debugfs_dir = NULL;
		qp->debugfs_stats = NULL;
	}

	INIT_DELAYED_WORK(&qp->link_work, ntb_qp_link_work);
	INIT_WORK(&qp->link_cleanup, ntb_qp_link_cleanup_work);

	spin_lock_init(&qp->ntb_rx_q_lock);
	spin_lock_init(&qp->ntb_tx_free_q_lock);

	INIT_LIST_HEAD(&qp->rx_post_q);
	INIT_LIST_HEAD(&qp->rx_pend_q);
	INIT_LIST_HEAD(&qp->rx_free_q);
	INIT_LIST_HEAD(&qp->tx_free_q);

	tasklet_init(&qp->rxc_db_work, ntb_transport_rxc_db,
		     (unsigned long)qp);

	return 0;
}

static int ntb_transport_probe(struct ntb_client *self, struct ntb_dev *ndev)
{
	struct ntb_transport_ctx *nt;
	struct ntb_transport_mw *mw;
	unsigned int mw_count, qp_count;
	u64 qp_bitmap;
	int node;
	int rc, i;

	mw_count = ntb_mw_count(ndev);
	if (ntb_spad_count(ndev) < (NUM_MWS + 1 + mw_count * 2)) {
		dev_err(&ndev->dev, "Not enough scratch pad registers for %s",
			NTB_TRANSPORT_NAME);
		return -EIO;
	}

	if (ntb_db_is_unsafe(ndev))
		dev_dbg(&ndev->dev,
			"doorbell is unsafe, proceed anyway...\n");
	if (ntb_spad_is_unsafe(ndev))
		dev_dbg(&ndev->dev,
			"scratchpad is unsafe, proceed anyway...\n");

	node = dev_to_node(&ndev->dev);

	nt = kzalloc_node(sizeof(*nt), GFP_KERNEL, node);
	if (!nt)
		return -ENOMEM;

	nt->ndev = ndev;

	nt->mw_count = mw_count;

	nt->mw_vec = kzalloc_node(mw_count * sizeof(*nt->mw_vec),
				  GFP_KERNEL, node);
	if (!nt->mw_vec) {
		rc = -ENOMEM;
		goto err;
	}

	for (i = 0; i < mw_count; i++) {
		mw = &nt->mw_vec[i];

		rc = ntb_mw_get_range(ndev, i, &mw->phys_addr, &mw->phys_size,
				      &mw->xlat_align, &mw->xlat_align_size);
		if (rc)
			goto err1;

		mw->vbase = ioremap_wc(mw->phys_addr, mw->phys_size);
		if (!mw->vbase) {
			rc = -ENOMEM;
			goto err1;
		}

		mw->buff_size = 0;
		mw->xlat_size = 0;
		mw->virt_addr = NULL;
		mw->dma_addr = 0;
	}

	qp_bitmap = ntb_db_valid_mask(ndev);

	qp_count = ilog2(qp_bitmap);
	if (max_num_clients && max_num_clients < qp_count)
		qp_count = max_num_clients;
	else if (mw_count < qp_count)
		qp_count = mw_count;

	qp_bitmap &= BIT_ULL(qp_count) - 1;

	nt->qp_count = qp_count;
	nt->qp_bitmap = qp_bitmap;
	nt->qp_bitmap_free = qp_bitmap;

	nt->qp_vec = kzalloc_node(qp_count * sizeof(*nt->qp_vec),
				  GFP_KERNEL, node);
	if (!nt->qp_vec) {
		rc = -ENOMEM;
		goto err1;
	}

	if (nt_debugfs_dir) {
		nt->debugfs_node_dir =
			debugfs_create_dir(pci_name(ndev->pdev),
					   nt_debugfs_dir);
	}

	for (i = 0; i < qp_count; i++) {
		rc = ntb_transport_init_queue(nt, i);
		if (rc)
			goto err2;
	}

	INIT_DELAYED_WORK(&nt->link_work, ntb_transport_link_work);
	INIT_WORK(&nt->link_cleanup, ntb_transport_link_cleanup_work);

	rc = ntb_set_ctx(ndev, nt, &ntb_transport_ops);
	if (rc)
		goto err2;

	INIT_LIST_HEAD(&nt->client_devs);
	rc = ntb_bus_init(nt);
	if (rc)
		goto err3;

	nt->link_is_up = false;
	ntb_link_enable(ndev, NTB_SPEED_AUTO, NTB_WIDTH_AUTO);
	ntb_link_event(ndev);

	return 0;

err3:
	ntb_clear_ctx(ndev);
err2:
	kfree(nt->qp_vec);
err1:
	while (i--) {
		mw = &nt->mw_vec[i];
		iounmap(mw->vbase);
	}
	kfree(nt->mw_vec);
err:
	kfree(nt);
	return rc;
}

static void ntb_transport_free(struct ntb_client *self, struct ntb_dev *ndev)
{
	struct ntb_transport_ctx *nt = ndev->ctx;
	struct ntb_transport_qp *qp;
	u64 qp_bitmap_alloc;
	int i;

	ntb_transport_link_cleanup(nt);
	cancel_work_sync(&nt->link_cleanup);
	cancel_delayed_work_sync(&nt->link_work);

	qp_bitmap_alloc = nt->qp_bitmap & ~nt->qp_bitmap_free;

	/* verify that all the qp's are freed */
	for (i = 0; i < nt->qp_count; i++) {
		qp = &nt->qp_vec[i];
		if (qp_bitmap_alloc & BIT_ULL(i))
			ntb_transport_free_queue(qp);
		debugfs_remove_recursive(qp->debugfs_dir);
	}

	ntb_link_disable(ndev);
	ntb_clear_ctx(ndev);

	ntb_bus_remove(nt);

	for (i = nt->mw_count; i--; ) {
		ntb_free_mw(nt, i);
		iounmap(nt->mw_vec[i].vbase);
	}

	kfree(nt->qp_vec);
	kfree(nt->mw_vec);
	kfree(nt);
}

static void ntb_complete_rxc(struct ntb_transport_qp *qp)
{
	struct ntb_queue_entry *entry;
	void *cb_data;
	unsigned int len;
	unsigned long irqflags;

	spin_lock_irqsave(&qp->ntb_rx_q_lock, irqflags);

	while (!list_empty(&qp->rx_post_q)) {
		entry = list_first_entry(&qp->rx_post_q,
					 struct ntb_queue_entry, entry);
		if (!(entry->flags & DESC_DONE_FLAG))
			break;

		entry->rx_hdr->flags = 0;
		iowrite32(entry->rx_index, &qp->rx_info->entry);

		cb_data = entry->cb_data;
		len = entry->len;

		list_move_tail(&entry->entry, &qp->rx_free_q);

		spin_unlock_irqrestore(&qp->ntb_rx_q_lock, irqflags);

		if (qp->rx_handler && qp->client_ready)
			qp->rx_handler(qp, qp->cb_data, cb_data, len);

		spin_lock_irqsave(&qp->ntb_rx_q_lock, irqflags);
	}

	spin_unlock_irqrestore(&qp->ntb_rx_q_lock, irqflags);
}

static void ntb_rx_copy_callback(void *data,
				 const struct dmaengine_result *res)
{
	struct ntb_queue_entry *entry = data;

	/* we need to check DMA results if we are using DMA */
	if (res) {
		enum dmaengine_tx_result dma_err = res->result;

		switch (dma_err) {
		case DMA_TRANS_READ_FAILED:
		case DMA_TRANS_WRITE_FAILED:
			entry->errors++;
		case DMA_TRANS_ABORTED:
		{
			struct ntb_transport_qp *qp = entry->qp;
			void *offset = qp->rx_buff + qp->rx_max_frame *
					qp->rx_index;

			ntb_memcpy_rx(entry, offset);
			qp->rx_memcpy++;
			return;
		}

		case DMA_TRANS_NOERROR:
		default:
			break;
		}
	}

	entry->flags |= DESC_DONE_FLAG;

	ntb_complete_rxc(entry->qp);
}

static void ntb_memcpy_rx(struct ntb_queue_entry *entry, void *offset)
{
	void *buf = entry->buf;
	size_t len = entry->len;

	memcpy(buf, offset, len);

	/* Ensure that the data is fully copied out before clearing the flag */
	wmb();

	ntb_rx_copy_callback(entry, NULL);
}

static int ntb_async_rx_submit(struct ntb_queue_entry *entry, void *offset)
{
	struct dma_async_tx_descriptor *txd;
	struct ntb_transport_qp *qp = entry->qp;
	struct dma_chan *chan = qp->rx_dma_chan;
	struct dma_device *device;
	size_t pay_off, buff_off, len;
	struct dmaengine_unmap_data *unmap;
	dma_cookie_t cookie;
	void *buf = entry->buf;
	int retries = 0;

	len = entry->len;
	device = chan->device;
	pay_off = (size_t)offset & ~PAGE_MASK;
	buff_off = (size_t)buf & ~PAGE_MASK;

	if (!is_dma_copy_aligned(device, pay_off, buff_off, len))
		goto err;

	unmap = dmaengine_get_unmap_data(device->dev, 2, GFP_NOWAIT);
	if (!unmap)
		goto err;

	unmap->len = len;
	unmap->addr[0] = dma_map_page(device->dev, virt_to_page(offset),
				      pay_off, len, DMA_TO_DEVICE);
	if (dma_mapping_error(device->dev, unmap->addr[0]))
		goto err_get_unmap;

	unmap->to_cnt = 1;

	unmap->addr[1] = dma_map_page(device->dev, virt_to_page(buf),
				      buff_off, len, DMA_FROM_DEVICE);
	if (dma_mapping_error(device->dev, unmap->addr[1]))
		goto err_get_unmap;

	unmap->from_cnt = 1;

	for (retries = 0; retries < DMA_RETRIES; retries++) {
		txd = device->device_prep_dma_memcpy(chan,
						     unmap->addr[1],
						     unmap->addr[0], len,
						     DMA_PREP_INTERRUPT);
		if (txd)
			break;

		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(DMA_OUT_RESOURCE_TO);
	}

	if (!txd) {
		qp->dma_rx_prep_err++;
		goto err_get_unmap;
	}

	txd->callback_result = ntb_rx_copy_callback;
	txd->callback_param = entry;
	dma_set_unmap(txd, unmap);

	cookie = dmaengine_submit(txd);
	if (dma_submit_error(cookie))
		goto err_set_unmap;

	dmaengine_unmap_put(unmap);

	qp->last_cookie = cookie;

	qp->rx_async++;

	return 0;

err_set_unmap:
	dmaengine_unmap_put(unmap);
err_get_unmap:
	dmaengine_unmap_put(unmap);
err:
	return -ENXIO;
}

static void ntb_async_rx(struct ntb_queue_entry *entry, void *offset)
{
	struct ntb_transport_qp *qp = entry->qp;
	struct dma_chan *chan = qp->rx_dma_chan;
	int res;

	if (!chan)
		goto err;

	if (entry->len < copy_bytes)
		goto err;

	res = ntb_async_rx_submit(entry, offset);
	if (res < 0)
		goto err;

	if (!entry->retries)
		qp->rx_async++;

	return;

err:
	ntb_memcpy_rx(entry, offset);
	qp->rx_memcpy++;
}

static int ntb_process_rxc(struct ntb_transport_qp *qp)
{
	struct ntb_payload_header *hdr;
	struct ntb_queue_entry *entry;
	void *offset;

	offset = qp->rx_buff + qp->rx_max_frame * qp->rx_index;
	hdr = offset + qp->rx_max_frame - sizeof(struct ntb_payload_header);

	dev_dbg(&qp->ndev->pdev->dev, "qp %d: RX ver %u len %d flags %x\n",
		qp->qp_num, hdr->ver, hdr->len, hdr->flags);

	if (!(hdr->flags & DESC_DONE_FLAG)) {
		dev_dbg(&qp->ndev->pdev->dev, "done flag not set\n");
		qp->rx_ring_empty++;
		return -EAGAIN;
	}

	if (hdr->flags & LINK_DOWN_FLAG) {
		dev_dbg(&qp->ndev->pdev->dev, "link down flag set\n");
		ntb_qp_link_down(qp);
		hdr->flags = 0;
		return -EAGAIN;
	}

	if (hdr->ver != (u32)qp->rx_pkts) {
		dev_dbg(&qp->ndev->pdev->dev,
			"version mismatch, expected %llu - got %u\n",
			qp->rx_pkts, hdr->ver);
		qp->rx_err_ver++;
		return -EIO;
	}

	entry = ntb_list_mv(&qp->ntb_rx_q_lock, &qp->rx_pend_q, &qp->rx_post_q);
	if (!entry) {
		dev_dbg(&qp->ndev->pdev->dev, "no receive buffer\n");
		qp->rx_err_no_buf++;
		return -EAGAIN;
	}

	entry->rx_hdr = hdr;
	entry->rx_index = qp->rx_index;

	if (hdr->len > entry->len) {
		dev_dbg(&qp->ndev->pdev->dev,
			"receive buffer overflow! Wanted %d got %d\n",
			hdr->len, entry->len);
		qp->rx_err_oflow++;

		entry->len = -EIO;
		entry->flags |= DESC_DONE_FLAG;

		ntb_complete_rxc(qp);
	} else {
		dev_dbg(&qp->ndev->pdev->dev,
			"RX OK index %u ver %u size %d into buf size %d\n",
			qp->rx_index, hdr->ver, hdr->len, entry->len);

		qp->rx_bytes += hdr->len;
		qp->rx_pkts++;

		entry->len = hdr->len;

		ntb_async_rx(entry, offset);
	}

	qp->rx_index++;
	qp->rx_index %= qp->rx_max_entry;

	return 0;
}

static void ntb_transport_rxc_db(unsigned long data)
{
	struct ntb_transport_qp *qp = (void *)data;
	int rc, i;

	dev_dbg(&qp->ndev->pdev->dev, "%s: doorbell %d received\n",
		__func__, qp->qp_num);

	/* Limit the number of packets processed in a single interrupt to
	 * provide fairness to others
	 */
	for (i = 0; i < qp->rx_max_entry; i++) {
		rc = ntb_process_rxc(qp);
		if (rc)
			break;
	}

	if (i && qp->rx_dma_chan)
		dma_async_issue_pending(qp->rx_dma_chan);

	if (i == qp->rx_max_entry) {
		/* there is more work to do */
		if (qp->active)
			tasklet_schedule(&qp->rxc_db_work);
	} else if (ntb_db_read(qp->ndev) & BIT_ULL(qp->qp_num)) {
		/* the doorbell bit is set: clear it */
		ntb_db_clear(qp->ndev, BIT_ULL(qp->qp_num));
		/* ntb_db_read ensures ntb_db_clear write is committed */
		ntb_db_read(qp->ndev);

		/* an interrupt may have arrived between finishing
		 * ntb_process_rxc and clearing the doorbell bit:
		 * there might be some more work to do.
		 */
		if (qp->active)
			tasklet_schedule(&qp->rxc_db_work);
	}
}

static void ntb_tx_copy_callback(void *data,
				 const struct dmaengine_result *res)
{
	struct ntb_queue_entry *entry = data;
	struct ntb_transport_qp *qp = entry->qp;
	struct ntb_payload_header __iomem *hdr = entry->tx_hdr;

	/* we need to check DMA results if we are using DMA */
	if (res) {
		enum dmaengine_tx_result dma_err = res->result;

		switch (dma_err) {
		case DMA_TRANS_READ_FAILED:
		case DMA_TRANS_WRITE_FAILED:
			entry->errors++;
		case DMA_TRANS_ABORTED:
		{
			void __iomem *offset =
				qp->tx_mw + qp->tx_max_frame *
				entry->tx_index;

			/* resubmit via CPU */
			ntb_memcpy_tx(entry, offset);
			qp->tx_memcpy++;
			return;
		}

		case DMA_TRANS_NOERROR:
		default:
			break;
		}
	}

	iowrite32(entry->flags | DESC_DONE_FLAG, &hdr->flags);

	ntb_peer_db_set(qp->ndev, BIT_ULL(qp->qp_num));

	/* The entry length can only be zero if the packet is intended to be a
	 * "link down" or similar.  Since no payload is being sent in these
	 * cases, there is nothing to add to the completion queue.
	 */
	if (entry->len > 0) {
		qp->tx_bytes += entry->len;

		if (qp->tx_handler)
			qp->tx_handler(qp, qp->cb_data, entry->cb_data,
				       entry->len);
	}

	ntb_list_add(&qp->ntb_tx_free_q_lock, &entry->entry, &qp->tx_free_q);
}

static void ntb_memcpy_tx(struct ntb_queue_entry *entry, void __iomem *offset)
{
#ifdef ARCH_HAS_NOCACHE_UACCESS
	/*
	 * Using non-temporal mov to improve performance on non-cached
	 * writes, even though we aren't actually copying from user space.
	 */
	__copy_from_user_inatomic_nocache(offset, entry->buf, entry->len);
#else
	memcpy_toio(offset, entry->buf, entry->len);
#endif

	/* Ensure that the data is fully copied out before setting the flags */
	wmb();

	ntb_tx_copy_callback(entry, NULL);
}

static int ntb_async_tx_submit(struct ntb_transport_qp *qp,
			       struct ntb_queue_entry *entry)
{
	struct dma_async_tx_descriptor *txd;
	struct dma_chan *chan = qp->tx_dma_chan;
	struct dma_device *device;
	size_t len = entry->len;
	void *buf = entry->buf;
	size_t dest_off, buff_off;
	struct dmaengine_unmap_data *unmap;
	dma_addr_t dest;
	dma_cookie_t cookie;
	int retries = 0;

	device = chan->device;
	dest = qp->tx_mw_phys + qp->tx_max_frame * entry->tx_index;
	buff_off = (size_t)buf & ~PAGE_MASK;
	dest_off = (size_t)dest & ~PAGE_MASK;

	if (!is_dma_copy_aligned(device, buff_off, dest_off, len))
		goto err;

	unmap = dmaengine_get_unmap_data(device->dev, 1, GFP_NOWAIT);
	if (!unmap)
		goto err;

	unmap->len = len;
	unmap->addr[0] = dma_map_page(device->dev, virt_to_page(buf),
				      buff_off, len, DMA_TO_DEVICE);
	if (dma_mapping_error(device->dev, unmap->addr[0]))
		goto err_get_unmap;

	unmap->to_cnt = 1;

	for (retries = 0; retries < DMA_RETRIES; retries++) {
		txd = device->device_prep_dma_memcpy(chan, dest,
						     unmap->addr[0], len,
						     DMA_PREP_INTERRUPT);
		if (txd)
			break;

		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(DMA_OUT_RESOURCE_TO);
	}

	if (!txd) {
		qp->dma_tx_prep_err++;
		goto err_get_unmap;
	}

	txd->callback_result = ntb_tx_copy_callback;
	txd->callback_param = entry;
	dma_set_unmap(txd, unmap);

	cookie = dmaengine_submit(txd);
	if (dma_submit_error(cookie))
		goto err_set_unmap;

	dmaengine_unmap_put(unmap);

	dma_async_issue_pending(chan);

	return 0;
err_set_unmap:
	dmaengine_unmap_put(unmap);
err_get_unmap:
	dmaengine_unmap_put(unmap);
err:
	return -ENXIO;
}

static void ntb_async_tx(struct ntb_transport_qp *qp,
			 struct ntb_queue_entry *entry)
{
	struct ntb_payload_header __iomem *hdr;
	struct dma_chan *chan = qp->tx_dma_chan;
	void __iomem *offset;
	int res;

	entry->tx_index = qp->tx_index;
	offset = qp->tx_mw + qp->tx_max_frame * entry->tx_index;
	hdr = offset + qp->tx_max_frame - sizeof(struct ntb_payload_header);
	entry->tx_hdr = hdr;

	iowrite32(entry->len, &hdr->len);
	iowrite32((u32)qp->tx_pkts, &hdr->ver);

	if (!chan)
		goto err;

	if (entry->len < copy_bytes)
		goto err;

	res = ntb_async_tx_submit(qp, entry);
	if (res < 0)
		goto err;

	if (!entry->retries)
		qp->tx_async++;

	return;

err:
	ntb_memcpy_tx(entry, offset);
	qp->tx_memcpy++;
}

static int ntb_process_tx(struct ntb_transport_qp *qp,
			  struct ntb_queue_entry *entry)
{
	if (qp->tx_index == qp->remote_rx_info->entry) {
		qp->tx_ring_full++;
		return -EAGAIN;
	}

	if (entry->len > qp->tx_max_frame - sizeof(struct ntb_payload_header)) {
		if (qp->tx_handler)
			qp->tx_handler(qp, qp->cb_data, NULL, -EIO);

		ntb_list_add(&qp->ntb_tx_free_q_lock, &entry->entry,
			     &qp->tx_free_q);
		return 0;
	}

	ntb_async_tx(qp, entry);

	qp->tx_index++;
	qp->tx_index %= qp->tx_max_entry;

	qp->tx_pkts++;

	return 0;
}

static void ntb_send_link_down(struct ntb_transport_qp *qp)
{
	struct pci_dev *pdev = qp->ndev->pdev;
	struct ntb_queue_entry *entry;
	int i, rc;

	if (!qp->link_is_up)
		return;

	dev_info(&pdev->dev, "qp %d: Send Link Down\n", qp->qp_num);

	for (i = 0; i < NTB_LINK_DOWN_TIMEOUT; i++) {
		entry = ntb_list_rm(&qp->ntb_tx_free_q_lock, &qp->tx_free_q);
		if (entry)
			break;
		msleep(100);
	}

	if (!entry)
		return;

	entry->cb_data = NULL;
	entry->buf = NULL;
	entry->len = 0;
	entry->flags = LINK_DOWN_FLAG;

	rc = ntb_process_tx(qp, entry);
	if (rc)
		dev_err(&pdev->dev, "ntb: QP%d unable to send linkdown msg\n",
			qp->qp_num);

	ntb_qp_link_down_reset(qp);
}

static bool ntb_dma_filter_fn(struct dma_chan *chan, void *node)
{
	return dev_to_node(&chan->dev->device) == (int)(unsigned long)node;
}

/**
 * ntb_transport_create_queue - Create a new NTB transport layer queue
 * @rx_handler: receive callback function
 * @tx_handler: transmit callback function
 * @event_handler: event callback function
 *
 * Create a new NTB transport layer queue and provide the queue with a callback
 * routine for both transmit and receive.  The receive callback routine will be
 * used to pass up data when the transport has received it on the queue.   The
 * transmit callback routine will be called when the transport has completed the
 * transmission of the data on the queue and the data is ready to be freed.
 *
 * RETURNS: pointer to newly created ntb_queue, NULL on error.
 */
struct ntb_transport_qp *
ntb_transport_create_queue(void *data, struct device *client_dev,
			   const struct ntb_queue_handlers *handlers)
{
	struct ntb_dev *ndev;
	struct pci_dev *pdev;
	struct ntb_transport_ctx *nt;
	struct ntb_queue_entry *entry;
	struct ntb_transport_qp *qp;
	u64 qp_bit;
	unsigned int free_queue;
	dma_cap_mask_t dma_mask;
	int node;
	int i;

	ndev = dev_ntb(client_dev->parent);
	pdev = ndev->pdev;
	nt = ndev->ctx;

	node = dev_to_node(&ndev->dev);

	free_queue = ffs(nt->qp_bitmap_free);
	if (!free_queue)
		goto err;

	/* decrement free_queue to make it zero based */
	free_queue--;

	qp = &nt->qp_vec[free_queue];
	qp_bit = BIT_ULL(qp->qp_num);

	nt->qp_bitmap_free &= ~qp_bit;

	qp->cb_data = data;
	qp->rx_handler = handlers->rx_handler;
	qp->tx_handler = handlers->tx_handler;
	qp->event_handler = handlers->event_handler;

	dma_cap_zero(dma_mask);
	dma_cap_set(DMA_MEMCPY, dma_mask);

	if (use_dma) {
		qp->tx_dma_chan =
			dma_request_channel(dma_mask, ntb_dma_filter_fn,
					    (void *)(unsigned long)node);
		if (!qp->tx_dma_chan)
			dev_info(&pdev->dev, "Unable to allocate TX DMA channel\n");

		qp->rx_dma_chan =
			dma_request_channel(dma_mask, ntb_dma_filter_fn,
					    (void *)(unsigned long)node);
		if (!qp->rx_dma_chan)
			dev_info(&pdev->dev, "Unable to allocate RX DMA channel\n");
	} else {
		qp->tx_dma_chan = NULL;
		qp->rx_dma_chan = NULL;
	}

	dev_dbg(&pdev->dev, "Using %s memcpy for TX\n",
		qp->tx_dma_chan ? "DMA" : "CPU");

	dev_dbg(&pdev->dev, "Using %s memcpy for RX\n",
		qp->rx_dma_chan ? "DMA" : "CPU");

	for (i = 0; i < NTB_QP_DEF_NUM_ENTRIES; i++) {
		entry = kzalloc_node(sizeof(*entry), GFP_ATOMIC, node);
		if (!entry)
			goto err1;

		entry->qp = qp;
		ntb_list_add(&qp->ntb_rx_q_lock, &entry->entry,
			     &qp->rx_free_q);
	}
	qp->rx_alloc_entry = NTB_QP_DEF_NUM_ENTRIES;

	for (i = 0; i < qp->tx_max_entry; i++) {
		entry = kzalloc_node(sizeof(*entry), GFP_ATOMIC, node);
		if (!entry)
			goto err2;

		entry->qp = qp;
		ntb_list_add(&qp->ntb_tx_free_q_lock, &entry->entry,
			     &qp->tx_free_q);
	}

	ntb_db_clear(qp->ndev, qp_bit);
	ntb_db_clear_mask(qp->ndev, qp_bit);

	dev_info(&pdev->dev, "NTB Transport QP %d created\n", qp->qp_num);

	return qp;

err2:
	while ((entry = ntb_list_rm(&qp->ntb_tx_free_q_lock, &qp->tx_free_q)))
		kfree(entry);
err1:
	qp->rx_alloc_entry = 0;
	while ((entry = ntb_list_rm(&qp->ntb_rx_q_lock, &qp->rx_free_q)))
		kfree(entry);
	if (qp->tx_dma_chan)
		dma_release_channel(qp->tx_dma_chan);
	if (qp->rx_dma_chan)
		dma_release_channel(qp->rx_dma_chan);
	nt->qp_bitmap_free |= qp_bit;
err:
	return NULL;
}
EXPORT_SYMBOL_GPL(ntb_transport_create_queue);

/**
 * ntb_transport_free_queue - Frees NTB transport queue
 * @qp: NTB queue to be freed
 *
 * Frees NTB transport queue
 */
void ntb_transport_free_queue(struct ntb_transport_qp *qp)
{
	struct pci_dev *pdev;
	struct ntb_queue_entry *entry;
	u64 qp_bit;

	if (!qp)
		return;

	pdev = qp->ndev->pdev;

	qp->active = false;

	if (qp->tx_dma_chan) {
		struct dma_chan *chan = qp->tx_dma_chan;
		/* Putting the dma_chan to NULL will force any new traffic to be
		 * processed by the CPU instead of the DAM engine
		 */
		qp->tx_dma_chan = NULL;

		/* Try to be nice and wait for any queued DMA engine
		 * transactions to process before smashing it with a rock
		 */
		dma_sync_wait(chan, qp->last_cookie);
		dmaengine_terminate_all(chan);
		dma_release_channel(chan);
	}

	if (qp->rx_dma_chan) {
		struct dma_chan *chan = qp->rx_dma_chan;
		/* Putting the dma_chan to NULL will force any new traffic to be
		 * processed by the CPU instead of the DAM engine
		 */
		qp->rx_dma_chan = NULL;

		/* Try to be nice and wait for any queued DMA engine
		 * transactions to process before smashing it with a rock
		 */
		dma_sync_wait(chan, qp->last_cookie);
		dmaengine_terminate_all(chan);
		dma_release_channel(chan);
	}

	qp_bit = BIT_ULL(qp->qp_num);

	ntb_db_set_mask(qp->ndev, qp_bit);
	tasklet_kill(&qp->rxc_db_work);

	cancel_delayed_work_sync(&qp->link_work);

	qp->cb_data = NULL;
	qp->rx_handler = NULL;
	qp->tx_handler = NULL;
	qp->event_handler = NULL;

	while ((entry = ntb_list_rm(&qp->ntb_rx_q_lock, &qp->rx_free_q)))
		kfree(entry);

	while ((entry = ntb_list_rm(&qp->ntb_rx_q_lock, &qp->rx_pend_q))) {
		dev_warn(&pdev->dev, "Freeing item from non-empty rx_pend_q\n");
		kfree(entry);
	}

	while ((entry = ntb_list_rm(&qp->ntb_rx_q_lock, &qp->rx_post_q))) {
		dev_warn(&pdev->dev, "Freeing item from non-empty rx_post_q\n");
		kfree(entry);
	}

	while ((entry = ntb_list_rm(&qp->ntb_tx_free_q_lock, &qp->tx_free_q)))
		kfree(entry);

	qp->transport->qp_bitmap_free |= qp_bit;

	dev_info(&pdev->dev, "NTB Transport QP %d freed\n", qp->qp_num);
}
EXPORT_SYMBOL_GPL(ntb_transport_free_queue);

/**
 * ntb_transport_rx_remove - Dequeues enqueued rx packet
 * @qp: NTB queue to be freed
 * @len: pointer to variable to write enqueued buffers length
 *
 * Dequeues unused buffers from receive queue.  Should only be used during
 * shutdown of qp.
 *
 * RETURNS: NULL error value on error, or void* for success.
 */
void *ntb_transport_rx_remove(struct ntb_transport_qp *qp, unsigned int *len)
{
	struct ntb_queue_entry *entry;
	void *buf;

	if (!qp || qp->client_ready)
		return NULL;

	entry = ntb_list_rm(&qp->ntb_rx_q_lock, &qp->rx_pend_q);
	if (!entry)
		return NULL;

	buf = entry->cb_data;
	*len = entry->len;

	ntb_list_add(&qp->ntb_rx_q_lock, &entry->entry, &qp->rx_free_q);

	return buf;
}
EXPORT_SYMBOL_GPL(ntb_transport_rx_remove);

/**
 * ntb_transport_rx_enqueue - Enqueue a new NTB queue entry
 * @qp: NTB transport layer queue the entry is to be enqueued on
 * @cb: per buffer pointer for callback function to use
 * @data: pointer to data buffer that incoming packets will be copied into
 * @len: length of the data buffer
 *
 * Enqueue a new receive buffer onto the transport queue into which a NTB
 * payload can be received into.
 *
 * RETURNS: An appropriate -ERRNO error value on error, or zero for success.
 */
int ntb_transport_rx_enqueue(struct ntb_transport_qp *qp, void *cb, void *data,
			     unsigned int len)
{
	struct ntb_queue_entry *entry;

	if (!qp)
		return -EINVAL;

	entry = ntb_list_rm(&qp->ntb_rx_q_lock, &qp->rx_free_q);
	if (!entry)
		return -ENOMEM;

	entry->cb_data = cb;
	entry->buf = data;
	entry->len = len;
	entry->flags = 0;
	entry->retries = 0;
	entry->errors = 0;
	entry->rx_index = 0;

	ntb_list_add(&qp->ntb_rx_q_lock, &entry->entry, &qp->rx_pend_q);

	if (qp->active)
		tasklet_schedule(&qp->rxc_db_work);

	return 0;
}
EXPORT_SYMBOL_GPL(ntb_transport_rx_enqueue);

/**
 * ntb_transport_tx_enqueue - Enqueue a new NTB queue entry
 * @qp: NTB transport layer queue the entry is to be enqueued on
 * @cb: per buffer pointer for callback function to use
 * @data: pointer to data buffer that will be sent
 * @len: length of the data buffer
 *
 * Enqueue a new transmit buffer onto the transport queue from which a NTB
 * payload will be transmitted.  This assumes that a lock is being held to
 * serialize access to the qp.
 *
 * RETURNS: An appropriate -ERRNO error value on error, or zero for success.
 */
int ntb_transport_tx_enqueue(struct ntb_transport_qp *qp, void *cb, void *data,
			     unsigned int len)
{
	struct ntb_queue_entry *entry;
	int rc;

	if (!qp || !qp->link_is_up || !len)
		return -EINVAL;

	entry = ntb_list_rm(&qp->ntb_tx_free_q_lock, &qp->tx_free_q);
	if (!entry) {
		qp->tx_err_no_buf++;
		return -EBUSY;
	}

	entry->cb_data = cb;
	entry->buf = data;
	entry->len = len;
	entry->flags = 0;
	entry->errors = 0;
	entry->retries = 0;
	entry->tx_index = 0;

	rc = ntb_process_tx(qp, entry);
	if (rc)
		ntb_list_add(&qp->ntb_tx_free_q_lock, &entry->entry,
			     &qp->tx_free_q);

	return rc;
}
EXPORT_SYMBOL_GPL(ntb_transport_tx_enqueue);

/**
 * ntb_transport_link_up - Notify NTB transport of client readiness to use queue
 * @qp: NTB transport layer queue to be enabled
 *
 * Notify NTB transport layer of client readiness to use queue
 */
void ntb_transport_link_up(struct ntb_transport_qp *qp)
{
	if (!qp)
		return;

	qp->client_ready = true;

	if (qp->transport->link_is_up)
		schedule_delayed_work(&qp->link_work, 0);
}
EXPORT_SYMBOL_GPL(ntb_transport_link_up);

/**
 * ntb_transport_link_down - Notify NTB transport to no longer enqueue data
 * @qp: NTB transport layer queue to be disabled
 *
 * Notify NTB transport layer of client's desire to no longer receive data on
 * transport queue specified.  It is the client's responsibility to ensure all
 * entries on queue are purged or otherwise handled appropriately.
 */
void ntb_transport_link_down(struct ntb_transport_qp *qp)
{
	int val;

	if (!qp)
		return;

	qp->client_ready = false;

	val = ntb_spad_read(qp->ndev, QP_LINKS);

	ntb_peer_spad_write(qp->ndev, QP_LINKS,
			    val & ~BIT(qp->qp_num));

	if (qp->link_is_up)
		ntb_send_link_down(qp);
	else
		cancel_delayed_work_sync(&qp->link_work);
}
EXPORT_SYMBOL_GPL(ntb_transport_link_down);

/**
 * ntb_transport_link_query - Query transport link state
 * @qp: NTB transport layer queue to be queried
 *
 * Query connectivity to the remote system of the NTB transport queue
 *
 * RETURNS: true for link up or false for link down
 */
bool ntb_transport_link_query(struct ntb_transport_qp *qp)
{
	if (!qp)
		return false;

	return qp->link_is_up;
}
EXPORT_SYMBOL_GPL(ntb_transport_link_query);

/**
 * ntb_transport_qp_num - Query the qp number
 * @qp: NTB transport layer queue to be queried
 *
 * Query qp number of the NTB transport queue
 *
 * RETURNS: a zero based number specifying the qp number
 */
unsigned char ntb_transport_qp_num(struct ntb_transport_qp *qp)
{
	if (!qp)
		return 0;

	return qp->qp_num;
}
EXPORT_SYMBOL_GPL(ntb_transport_qp_num);

/**
 * ntb_transport_max_size - Query the max payload size of a qp
 * @qp: NTB transport layer queue to be queried
 *
 * Query the maximum payload size permissible on the given qp
 *
 * RETURNS: the max payload size of a qp
 */
unsigned int ntb_transport_max_size(struct ntb_transport_qp *qp)
{
	unsigned int max_size;
	unsigned int copy_align;
	struct dma_chan *rx_chan, *tx_chan;

	if (!qp)
		return 0;

	rx_chan = qp->rx_dma_chan;
	tx_chan = qp->tx_dma_chan;

	copy_align = max(rx_chan ? rx_chan->device->copy_align : 0,
			 tx_chan ? tx_chan->device->copy_align : 0);

	/* If DMA engine usage is possible, try to find the max size for that */
	max_size = qp->tx_max_frame - sizeof(struct ntb_payload_header);
	max_size = round_down(max_size, 1 << copy_align);

	return max_size;
}
EXPORT_SYMBOL_GPL(ntb_transport_max_size);

unsigned int ntb_transport_tx_free_entry(struct ntb_transport_qp *qp)
{
	unsigned int head = qp->tx_index;
	unsigned int tail = qp->remote_rx_info->entry;

	return tail > head ? tail - head : qp->tx_max_entry + tail - head;
}
EXPORT_SYMBOL_GPL(ntb_transport_tx_free_entry);

static void ntb_transport_doorbell_callback(void *data, int vector)
{
	struct ntb_transport_ctx *nt = data;
	struct ntb_transport_qp *qp;
	u64 db_bits;
	unsigned int qp_num;

	db_bits = (nt->qp_bitmap & ~nt->qp_bitmap_free &
		   ntb_db_vector_mask(nt->ndev, vector));

	while (db_bits) {
		qp_num = __ffs(db_bits);
		qp = &nt->qp_vec[qp_num];

		if (qp->active)
			tasklet_schedule(&qp->rxc_db_work);

		db_bits &= ~BIT_ULL(qp_num);
	}
}

static const struct ntb_ctx_ops ntb_transport_ops = {
	.link_event = ntb_transport_event_callback,
	.db_event = ntb_transport_doorbell_callback,
};

static struct ntb_client ntb_transport_client = {
	.ops = {
		.probe = ntb_transport_probe,
		.remove = ntb_transport_free,
	},
};

static int __init ntb_transport_init(void)
{
	int rc;

	pr_info("%s, version %s\n", NTB_TRANSPORT_DESC, NTB_TRANSPORT_VER);

	if (debugfs_initialized())
		nt_debugfs_dir = debugfs_create_dir(KBUILD_MODNAME, NULL);

	rc = bus_register(&ntb_transport_bus);
	if (rc)
		goto err_bus;

	rc = ntb_register_client(&ntb_transport_client);
	if (rc)
		goto err_client;

	return 0;

err_client:
	bus_unregister(&ntb_transport_bus);
err_bus:
	debugfs_remove_recursive(nt_debugfs_dir);
	return rc;
}
module_init(ntb_transport_init);

static void __exit ntb_transport_exit(void)
{
	ntb_unregister_client(&ntb_transport_client);
	bus_unregister(&ntb_transport_bus);
	debugfs_remove_recursive(nt_debugfs_dir);
}
module_exit(ntb_transport_exit);
