/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 *   redistributing this file, you may do so under either license.
 *
 *   GPL LICENSE SUMMARY
 *
 *   Copyright(c) 2012 Intel Corporation. All rights reserved.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of version 2 of the GNU General Public License as
 *   published by the Free Software Foundation.
 *
 *   BSD LICENSE
 *
 *   Copyright(c) 2012 Intel Corporation. All rights reserved.
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
 * Intel PCIe NTB Linux driver
 *
 * Contact Information:
 * Jon Mason <jon.mason@intel.com>
 */
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/ntb.h>
#include "ntb_hw.h"

#define NTB_TRANSPORT_VERSION	3

static unsigned int transport_mtu = 0x401E;
module_param(transport_mtu, uint, 0644);
MODULE_PARM_DESC(transport_mtu, "Maximum size of NTB transport packets");

static unsigned char max_num_clients = 2;
module_param(max_num_clients, byte, 0644);
MODULE_PARM_DESC(max_num_clients, "Maximum number of NTB transport clients");

struct ntb_queue_entry {
	/* ntb_queue list reference */
	struct list_head entry;
	/* pointers to data to be transfered */
	void *cb_data;
	void *buf;
	unsigned int len;
	unsigned int flags;
};

struct ntb_rx_info {
	unsigned int entry;
};

struct ntb_transport_qp {
	struct ntb_transport *transport;
	struct ntb_device *ndev;
	void *cb_data;

	bool client_ready;
	bool qp_link;
	u8 qp_num;	/* Only 64 QP's are allowed.  0-63 */

	struct ntb_rx_info __iomem *rx_info;
	struct ntb_rx_info *remote_rx_info;

	void (*tx_handler) (struct ntb_transport_qp *qp, void *qp_data,
			    void *data, int len);
	struct list_head tx_free_q;
	spinlock_t ntb_tx_free_q_lock;
	void __iomem *tx_mw;
	unsigned int tx_index;
	unsigned int tx_max_entry;
	unsigned int tx_max_frame;

	void (*rx_handler) (struct ntb_transport_qp *qp, void *qp_data,
			    void *data, int len);
	struct tasklet_struct rx_work;
	struct list_head rx_pend_q;
	struct list_head rx_free_q;
	spinlock_t ntb_rx_pend_q_lock;
	spinlock_t ntb_rx_free_q_lock;
	void *rx_buff;
	unsigned int rx_index;
	unsigned int rx_max_entry;
	unsigned int rx_max_frame;

	void (*event_handler) (void *data, int status);
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
	u64 tx_bytes;
	u64 tx_pkts;
	u64 tx_ring_full;
};

struct ntb_transport_mw {
	size_t size;
	void *virt_addr;
	dma_addr_t dma_addr;
};

struct ntb_transport_client_dev {
	struct list_head entry;
	struct device dev;
};

struct ntb_transport {
	struct list_head entry;
	struct list_head client_devs;

	struct ntb_device *ndev;
	struct ntb_transport_mw mw[NTB_NUM_MW];
	struct ntb_transport_qp *qps;
	unsigned int max_qps;
	unsigned long qp_bitmap;
	bool transport_link;
	struct delayed_work link_work;
	struct work_struct link_cleanup;
	struct dentry *debugfs_dir;
};

enum {
	DESC_DONE_FLAG = 1 << 0,
	LINK_DOWN_FLAG = 1 << 1,
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

#define QP_TO_MW(qp)		((qp) % NTB_NUM_MW)
#define NTB_QP_DEF_NUM_ENTRIES	100
#define NTB_LINK_DOWN_TIMEOUT	10

static int ntb_match_bus(struct device *dev, struct device_driver *drv)
{
	return !strncmp(dev_name(dev), drv->name, strlen(drv->name));
}

static int ntb_client_probe(struct device *dev)
{
	const struct ntb_client *drv = container_of(dev->driver,
						    struct ntb_client, driver);
	struct pci_dev *pdev = container_of(dev->parent, struct pci_dev, dev);
	int rc = -EINVAL;

	get_device(dev);
	if (drv && drv->probe)
		rc = drv->probe(pdev);
	if (rc)
		put_device(dev);

	return rc;
}

static int ntb_client_remove(struct device *dev)
{
	const struct ntb_client *drv = container_of(dev->driver,
						    struct ntb_client, driver);
	struct pci_dev *pdev = container_of(dev->parent, struct pci_dev, dev);

	if (drv && drv->remove)
		drv->remove(pdev);

	put_device(dev);

	return 0;
}

static struct bus_type ntb_bus_type = {
	.name = "ntb_bus",
	.match = ntb_match_bus,
	.probe = ntb_client_probe,
	.remove = ntb_client_remove,
};

static LIST_HEAD(ntb_transport_list);

static int ntb_bus_init(struct ntb_transport *nt)
{
	if (list_empty(&ntb_transport_list)) {
		int rc = bus_register(&ntb_bus_type);
		if (rc)
			return rc;
	}

	list_add(&nt->entry, &ntb_transport_list);

	return 0;
}

static void ntb_bus_remove(struct ntb_transport *nt)
{
	struct ntb_transport_client_dev *client_dev, *cd;

	list_for_each_entry_safe(client_dev, cd, &nt->client_devs, entry) {
		dev_err(client_dev->dev.parent, "%s still attached to bus, removing\n",
			dev_name(&client_dev->dev));
		list_del(&client_dev->entry);
		device_unregister(&client_dev->dev);
	}

	list_del(&nt->entry);

	if (list_empty(&ntb_transport_list))
		bus_unregister(&ntb_bus_type);
}

static void ntb_client_release(struct device *dev)
{
	struct ntb_transport_client_dev *client_dev;
	client_dev = container_of(dev, struct ntb_transport_client_dev, dev);

	kfree(client_dev);
}

/**
 * ntb_unregister_client_dev - Unregister NTB client device
 * @device_name: Name of NTB client device
 *
 * Unregister an NTB client device with the NTB transport layer
 */
void ntb_unregister_client_dev(char *device_name)
{
	struct ntb_transport_client_dev *client, *cd;
	struct ntb_transport *nt;

	list_for_each_entry(nt, &ntb_transport_list, entry)
		list_for_each_entry_safe(client, cd, &nt->client_devs, entry)
			if (!strncmp(dev_name(&client->dev), device_name,
				     strlen(device_name))) {
				list_del(&client->entry);
				device_unregister(&client->dev);
			}
}
EXPORT_SYMBOL_GPL(ntb_unregister_client_dev);

/**
 * ntb_register_client_dev - Register NTB client device
 * @device_name: Name of NTB client device
 *
 * Register an NTB client device with the NTB transport layer
 */
int ntb_register_client_dev(char *device_name)
{
	struct ntb_transport_client_dev *client_dev;
	struct ntb_transport *nt;
	int rc;

	if (list_empty(&ntb_transport_list))
		return -ENODEV;

	list_for_each_entry(nt, &ntb_transport_list, entry) {
		struct device *dev;

		client_dev = kzalloc(sizeof(struct ntb_transport_client_dev),
				     GFP_KERNEL);
		if (!client_dev) {
			rc = -ENOMEM;
			goto err;
		}

		dev = &client_dev->dev;

		/* setup and register client devices */
		dev_set_name(dev, "%s", device_name);
		dev->bus = &ntb_bus_type;
		dev->release = ntb_client_release;
		dev->parent = &ntb_query_pdev(nt->ndev)->dev;

		rc = device_register(dev);
		if (rc) {
			kfree(client_dev);
			goto err;
		}

		list_add_tail(&client_dev->entry, &nt->client_devs);
	}

	return 0;

err:
	ntb_unregister_client_dev(device_name);

	return rc;
}
EXPORT_SYMBOL_GPL(ntb_register_client_dev);

/**
 * ntb_register_client - Register NTB client driver
 * @drv: NTB client driver to be registered
 *
 * Register an NTB client driver with the NTB transport layer
 *
 * RETURNS: An appropriate -ERRNO error value on error, or zero for success.
 */
int ntb_register_client(struct ntb_client *drv)
{
	drv->driver.bus = &ntb_bus_type;

	if (list_empty(&ntb_transport_list))
		return -ENODEV;

	return driver_register(&drv->driver);
}
EXPORT_SYMBOL_GPL(ntb_register_client);

/**
 * ntb_unregister_client - Unregister NTB client driver
 * @drv: NTB client driver to be unregistered
 *
 * Unregister an NTB client driver with the NTB transport layer
 *
 * RETURNS: An appropriate -ERRNO error value on error, or zero for success.
 */
void ntb_unregister_client(struct ntb_client *drv)
{
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL_GPL(ntb_unregister_client);

static ssize_t debugfs_read(struct file *filp, char __user *ubuf, size_t count,
			    loff_t *offp)
{
	struct ntb_transport_qp *qp;
	char *buf;
	ssize_t ret, out_offset, out_count;

	out_count = 600;

	buf = kmalloc(out_count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	qp = filp->private_data;
	out_offset = 0;
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "NTB QP stats\n");
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "rx_bytes - \t%llu\n", qp->rx_bytes);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "rx_pkts - \t%llu\n", qp->rx_pkts);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "rx_ring_empty - %llu\n", qp->rx_ring_empty);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "rx_err_no_buf - %llu\n", qp->rx_err_no_buf);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "rx_err_oflow - \t%llu\n", qp->rx_err_oflow);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "rx_err_ver - \t%llu\n", qp->rx_err_ver);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "rx_buff - \t%p\n", qp->rx_buff);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "rx_index - \t%u\n", qp->rx_index);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "rx_max_entry - \t%u\n", qp->rx_max_entry);

	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "tx_bytes - \t%llu\n", qp->tx_bytes);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "tx_pkts - \t%llu\n", qp->tx_pkts);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "tx_ring_full - \t%llu\n", qp->tx_ring_full);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "tx_mw - \t%p\n", qp->tx_mw);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "tx_index - \t%u\n", qp->tx_index);
	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "tx_max_entry - \t%u\n", qp->tx_max_entry);

	out_offset += snprintf(buf + out_offset, out_count - out_offset,
			       "\nQP Link %s\n", (qp->qp_link == NTB_LINK_UP) ?
			       "Up" : "Down");
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

static void ntb_transport_setup_qp_mw(struct ntb_transport *nt,
				      unsigned int qp_num)
{
	struct ntb_transport_qp *qp = &nt->qps[qp_num];
	unsigned int rx_size, num_qps_mw;
	u8 mw_num = QP_TO_MW(qp_num);
	unsigned int i;

	WARN_ON(nt->mw[mw_num].virt_addr == NULL);

	if (nt->max_qps % NTB_NUM_MW && mw_num < nt->max_qps % NTB_NUM_MW)
		num_qps_mw = nt->max_qps / NTB_NUM_MW + 1;
	else
		num_qps_mw = nt->max_qps / NTB_NUM_MW;

	rx_size = (unsigned int) nt->mw[mw_num].size / num_qps_mw;
	qp->remote_rx_info = nt->mw[mw_num].virt_addr +
			     (qp_num / NTB_NUM_MW * rx_size);
	rx_size -= sizeof(struct ntb_rx_info);

	qp->rx_buff = qp->remote_rx_info + 1;
	qp->rx_max_frame = min(transport_mtu, rx_size);
	qp->rx_max_entry = rx_size / qp->rx_max_frame;
	qp->rx_index = 0;

	qp->remote_rx_info->entry = qp->rx_max_entry;

	/* setup the hdr offsets with 0's */
	for (i = 0; i < qp->rx_max_entry; i++) {
		void *offset = qp->rx_buff + qp->rx_max_frame * (i + 1) -
			       sizeof(struct ntb_payload_header);
		memset(offset, 0, sizeof(struct ntb_payload_header));
	}

	qp->rx_pkts = 0;
	qp->tx_pkts = 0;
	qp->tx_index = 0;
}

static void ntb_free_mw(struct ntb_transport *nt, int num_mw)
{
	struct ntb_transport_mw *mw = &nt->mw[num_mw];
	struct pci_dev *pdev = ntb_query_pdev(nt->ndev);

	if (!mw->virt_addr)
		return;

	dma_free_coherent(&pdev->dev, mw->size, mw->virt_addr, mw->dma_addr);
	mw->virt_addr = NULL;
}

static int ntb_set_mw(struct ntb_transport *nt, int num_mw, unsigned int size)
{
	struct ntb_transport_mw *mw = &nt->mw[num_mw];
	struct pci_dev *pdev = ntb_query_pdev(nt->ndev);

	/* No need to re-setup */
	if (mw->size == ALIGN(size, 4096))
		return 0;

	if (mw->size != 0)
		ntb_free_mw(nt, num_mw);

	/* Alloc memory for receiving data.  Must be 4k aligned */
	mw->size = ALIGN(size, 4096);

	mw->virt_addr = dma_alloc_coherent(&pdev->dev, mw->size, &mw->dma_addr,
					   GFP_KERNEL);
	if (!mw->virt_addr) {
		mw->size = 0;
		dev_err(&pdev->dev, "Unable to allocate MW buffer of size %d\n",
		       (int) mw->size);
		return -ENOMEM;
	}

	/* Notify HW the memory location of the receive buffer */
	ntb_set_mw_addr(nt->ndev, num_mw, mw->dma_addr);

	return 0;
}

static void ntb_qp_link_cleanup(struct work_struct *work)
{
	struct ntb_transport_qp *qp = container_of(work,
						   struct ntb_transport_qp,
						   link_cleanup);
	struct ntb_transport *nt = qp->transport;
	struct pci_dev *pdev = ntb_query_pdev(nt->ndev);

	if (qp->qp_link == NTB_LINK_DOWN) {
		cancel_delayed_work_sync(&qp->link_work);
		return;
	}

	if (qp->event_handler)
		qp->event_handler(qp->cb_data, NTB_LINK_DOWN);

	dev_info(&pdev->dev, "qp %d: Link Down\n", qp->qp_num);
	qp->qp_link = NTB_LINK_DOWN;

	if (nt->transport_link == NTB_LINK_UP)
		schedule_delayed_work(&qp->link_work,
				      msecs_to_jiffies(NTB_LINK_DOWN_TIMEOUT));
}

static void ntb_qp_link_down(struct ntb_transport_qp *qp)
{
	schedule_work(&qp->link_cleanup);
}

static void ntb_transport_link_cleanup(struct work_struct *work)
{
	struct ntb_transport *nt = container_of(work, struct ntb_transport,
						link_cleanup);
	int i;

	if (nt->transport_link == NTB_LINK_DOWN)
		cancel_delayed_work_sync(&nt->link_work);
	else
		nt->transport_link = NTB_LINK_DOWN;

	/* Pass along the info to any clients */
	for (i = 0; i < nt->max_qps; i++)
		if (!test_bit(i, &nt->qp_bitmap))
			ntb_qp_link_down(&nt->qps[i]);

	/* The scratchpad registers keep the values if the remote side
	 * goes down, blast them now to give them a sane value the next
	 * time they are accessed
	 */
	for (i = 0; i < MAX_SPAD; i++)
		ntb_write_local_spad(nt->ndev, i, 0);
}

static void ntb_transport_event_callback(void *data, enum ntb_hw_event event)
{
	struct ntb_transport *nt = data;

	switch (event) {
	case NTB_EVENT_HW_LINK_UP:
		schedule_delayed_work(&nt->link_work, 0);
		break;
	case NTB_EVENT_HW_LINK_DOWN:
		schedule_work(&nt->link_cleanup);
		break;
	default:
		BUG();
	}
}

static void ntb_transport_link_work(struct work_struct *work)
{
	struct ntb_transport *nt = container_of(work, struct ntb_transport,
						link_work.work);
	struct ntb_device *ndev = nt->ndev;
	struct pci_dev *pdev = ntb_query_pdev(ndev);
	u32 val;
	int rc, i;

	/* send the local info, in the opposite order of the way we read it */
	for (i = 0; i < NTB_NUM_MW; i++) {
		rc = ntb_write_remote_spad(ndev, MW0_SZ_HIGH + (i * 2),
					   ntb_get_mw_size(ndev, i) >> 32);
		if (rc) {
			dev_err(&pdev->dev, "Error writing %u to remote spad %d\n",
				(u32)(ntb_get_mw_size(ndev, i) >> 32),
				MW0_SZ_HIGH + (i * 2));
			goto out;
		}

		rc = ntb_write_remote_spad(ndev, MW0_SZ_LOW + (i * 2),
					   (u32) ntb_get_mw_size(ndev, i));
		if (rc) {
			dev_err(&pdev->dev, "Error writing %u to remote spad %d\n",
				(u32) ntb_get_mw_size(ndev, i),
				MW0_SZ_LOW + (i * 2));
			goto out;
		}
	}

	rc = ntb_write_remote_spad(ndev, NUM_MWS, NTB_NUM_MW);
	if (rc) {
		dev_err(&pdev->dev, "Error writing %x to remote spad %d\n",
			NTB_NUM_MW, NUM_MWS);
		goto out;
	}

	rc = ntb_write_remote_spad(ndev, NUM_QPS, nt->max_qps);
	if (rc) {
		dev_err(&pdev->dev, "Error writing %x to remote spad %d\n",
			nt->max_qps, NUM_QPS);
		goto out;
	}

	rc = ntb_write_remote_spad(ndev, VERSION, NTB_TRANSPORT_VERSION);
	if (rc) {
		dev_err(&pdev->dev, "Error writing %x to remote spad %d\n",
			NTB_TRANSPORT_VERSION, VERSION);
		goto out;
	}

	/* Query the remote side for its info */
	rc = ntb_read_remote_spad(ndev, VERSION, &val);
	if (rc) {
		dev_err(&pdev->dev, "Error reading remote spad %d\n", VERSION);
		goto out;
	}

	if (val != NTB_TRANSPORT_VERSION)
		goto out;
	dev_dbg(&pdev->dev, "Remote version = %d\n", val);

	rc = ntb_read_remote_spad(ndev, NUM_QPS, &val);
	if (rc) {
		dev_err(&pdev->dev, "Error reading remote spad %d\n", NUM_QPS);
		goto out;
	}

	if (val != nt->max_qps)
		goto out;
	dev_dbg(&pdev->dev, "Remote max number of qps = %d\n", val);

	rc = ntb_read_remote_spad(ndev, NUM_MWS, &val);
	if (rc) {
		dev_err(&pdev->dev, "Error reading remote spad %d\n", NUM_MWS);
		goto out;
	}

	if (val != NTB_NUM_MW)
		goto out;
	dev_dbg(&pdev->dev, "Remote number of mws = %d\n", val);

	for (i = 0; i < NTB_NUM_MW; i++) {
		u64 val64;

		rc = ntb_read_remote_spad(ndev, MW0_SZ_HIGH + (i * 2), &val);
		if (rc) {
			dev_err(&pdev->dev, "Error reading remote spad %d\n",
				MW0_SZ_HIGH + (i * 2));
			goto out1;
		}

		val64 = (u64) val << 32;

		rc = ntb_read_remote_spad(ndev, MW0_SZ_LOW + (i * 2), &val);
		if (rc) {
			dev_err(&pdev->dev, "Error reading remote spad %d\n",
				MW0_SZ_LOW + (i * 2));
			goto out1;
		}

		val64 |= val;

		dev_dbg(&pdev->dev, "Remote MW%d size = %llu\n", i, val64);

		rc = ntb_set_mw(nt, i, val64);
		if (rc)
			goto out1;
	}

	nt->transport_link = NTB_LINK_UP;

	for (i = 0; i < nt->max_qps; i++) {
		struct ntb_transport_qp *qp = &nt->qps[i];

		ntb_transport_setup_qp_mw(nt, i);

		if (qp->client_ready == NTB_LINK_UP)
			schedule_delayed_work(&qp->link_work, 0);
	}

	return;

out1:
	for (i = 0; i < NTB_NUM_MW; i++)
		ntb_free_mw(nt, i);
out:
	if (ntb_hw_link_status(ndev))
		schedule_delayed_work(&nt->link_work,
				      msecs_to_jiffies(NTB_LINK_DOWN_TIMEOUT));
}

static void ntb_qp_link_work(struct work_struct *work)
{
	struct ntb_transport_qp *qp = container_of(work,
						   struct ntb_transport_qp,
						   link_work.work);
	struct pci_dev *pdev = ntb_query_pdev(qp->ndev);
	struct ntb_transport *nt = qp->transport;
	int rc, val;

	WARN_ON(nt->transport_link != NTB_LINK_UP);

	rc = ntb_read_local_spad(nt->ndev, QP_LINKS, &val);
	if (rc) {
		dev_err(&pdev->dev, "Error reading spad %d\n", QP_LINKS);
		return;
	}

	rc = ntb_write_remote_spad(nt->ndev, QP_LINKS, val | 1 << qp->qp_num);
	if (rc)
		dev_err(&pdev->dev, "Error writing %x to remote spad %d\n",
			val | 1 << qp->qp_num, QP_LINKS);

	/* query remote spad for qp ready bits */
	rc = ntb_read_remote_spad(nt->ndev, QP_LINKS, &val);
	if (rc)
		dev_err(&pdev->dev, "Error reading remote spad %d\n", QP_LINKS);

	dev_dbg(&pdev->dev, "Remote QP link status = %x\n", val);

	/* See if the remote side is up */
	if (1 << qp->qp_num & val) {
		qp->qp_link = NTB_LINK_UP;

		dev_info(&pdev->dev, "qp %d: Link Up\n", qp->qp_num);
		if (qp->event_handler)
			qp->event_handler(qp->cb_data, NTB_LINK_UP);
	} else if (nt->transport_link == NTB_LINK_UP)
		schedule_delayed_work(&qp->link_work,
				      msecs_to_jiffies(NTB_LINK_DOWN_TIMEOUT));
}

static void ntb_transport_init_queue(struct ntb_transport *nt,
				     unsigned int qp_num)
{
	struct ntb_transport_qp *qp;
	unsigned int num_qps_mw, tx_size;
	u8 mw_num = QP_TO_MW(qp_num);

	qp = &nt->qps[qp_num];
	qp->qp_num = qp_num;
	qp->transport = nt;
	qp->ndev = nt->ndev;
	qp->qp_link = NTB_LINK_DOWN;
	qp->client_ready = NTB_LINK_DOWN;
	qp->event_handler = NULL;

	if (nt->max_qps % NTB_NUM_MW && mw_num < nt->max_qps % NTB_NUM_MW)
		num_qps_mw = nt->max_qps / NTB_NUM_MW + 1;
	else
		num_qps_mw = nt->max_qps / NTB_NUM_MW;

	tx_size = (unsigned int) ntb_get_mw_size(qp->ndev, mw_num) / num_qps_mw;
	qp->rx_info = ntb_get_mw_vbase(nt->ndev, mw_num) +
		      (qp_num / NTB_NUM_MW * tx_size);
	tx_size -= sizeof(struct ntb_rx_info);

	qp->tx_mw = qp->rx_info + 1;
	qp->tx_max_frame = min(transport_mtu, tx_size);
	qp->tx_max_entry = tx_size / qp->tx_max_frame;

	if (nt->debugfs_dir) {
		char debugfs_name[4];

		snprintf(debugfs_name, 4, "qp%d", qp_num);
		qp->debugfs_dir = debugfs_create_dir(debugfs_name,
						     nt->debugfs_dir);

		qp->debugfs_stats = debugfs_create_file("stats", S_IRUSR,
							qp->debugfs_dir, qp,
							&ntb_qp_debugfs_stats);
	}

	INIT_DELAYED_WORK(&qp->link_work, ntb_qp_link_work);
	INIT_WORK(&qp->link_cleanup, ntb_qp_link_cleanup);

	spin_lock_init(&qp->ntb_rx_pend_q_lock);
	spin_lock_init(&qp->ntb_rx_free_q_lock);
	spin_lock_init(&qp->ntb_tx_free_q_lock);

	INIT_LIST_HEAD(&qp->rx_pend_q);
	INIT_LIST_HEAD(&qp->rx_free_q);
	INIT_LIST_HEAD(&qp->tx_free_q);
}

int ntb_transport_init(struct pci_dev *pdev)
{
	struct ntb_transport *nt;
	int rc, i;

	nt = kzalloc(sizeof(struct ntb_transport), GFP_KERNEL);
	if (!nt)
		return -ENOMEM;

	if (debugfs_initialized())
		nt->debugfs_dir = debugfs_create_dir(KBUILD_MODNAME, NULL);
	else
		nt->debugfs_dir = NULL;

	nt->ndev = ntb_register_transport(pdev, nt);
	if (!nt->ndev) {
		rc = -EIO;
		goto err;
	}

	nt->max_qps = min(nt->ndev->max_cbs, max_num_clients);

	nt->qps = kcalloc(nt->max_qps, sizeof(struct ntb_transport_qp),
			  GFP_KERNEL);
	if (!nt->qps) {
		rc = -ENOMEM;
		goto err1;
	}

	nt->qp_bitmap = ((u64) 1 << nt->max_qps) - 1;

	for (i = 0; i < nt->max_qps; i++)
		ntb_transport_init_queue(nt, i);

	INIT_DELAYED_WORK(&nt->link_work, ntb_transport_link_work);
	INIT_WORK(&nt->link_cleanup, ntb_transport_link_cleanup);

	rc = ntb_register_event_callback(nt->ndev,
					 ntb_transport_event_callback);
	if (rc)
		goto err2;

	INIT_LIST_HEAD(&nt->client_devs);
	rc = ntb_bus_init(nt);
	if (rc)
		goto err3;

	if (ntb_hw_link_status(nt->ndev))
		schedule_delayed_work(&nt->link_work, 0);

	return 0;

err3:
	ntb_unregister_event_callback(nt->ndev);
err2:
	kfree(nt->qps);
err1:
	ntb_unregister_transport(nt->ndev);
err:
	debugfs_remove_recursive(nt->debugfs_dir);
	kfree(nt);
	return rc;
}

void ntb_transport_free(void *transport)
{
	struct ntb_transport *nt = transport;
	struct pci_dev *pdev;
	int i;

	nt->transport_link = NTB_LINK_DOWN;

	/* verify that all the qp's are freed */
	for (i = 0; i < nt->max_qps; i++)
		if (!test_bit(i, &nt->qp_bitmap))
			ntb_transport_free_queue(&nt->qps[i]);

	ntb_bus_remove(nt);

	cancel_delayed_work_sync(&nt->link_work);

	debugfs_remove_recursive(nt->debugfs_dir);

	ntb_unregister_event_callback(nt->ndev);

	pdev = ntb_query_pdev(nt->ndev);

	for (i = 0; i < NTB_NUM_MW; i++)
		ntb_free_mw(nt, i);

	kfree(nt->qps);
	ntb_unregister_transport(nt->ndev);
	kfree(nt);
}

static void ntb_rx_copy_task(struct ntb_transport_qp *qp,
			     struct ntb_queue_entry *entry, void *offset)
{
	void *cb_data = entry->cb_data;
	unsigned int len = entry->len;

	memcpy(entry->buf, offset, entry->len);

	ntb_list_add(&qp->ntb_rx_free_q_lock, &entry->entry, &qp->rx_free_q);

	if (qp->rx_handler && qp->client_ready == NTB_LINK_UP)
		qp->rx_handler(qp, qp->cb_data, cb_data, len);
}

static int ntb_process_rxc(struct ntb_transport_qp *qp)
{
	struct ntb_payload_header *hdr;
	struct ntb_queue_entry *entry;
	void *offset;

	offset = qp->rx_buff + qp->rx_max_frame * qp->rx_index;
	hdr = offset + qp->rx_max_frame - sizeof(struct ntb_payload_header);

	entry = ntb_list_rm(&qp->ntb_rx_pend_q_lock, &qp->rx_pend_q);
	if (!entry) {
		dev_dbg(&ntb_query_pdev(qp->ndev)->dev,
			"no buffer - HDR ver %u, len %d, flags %x\n",
			hdr->ver, hdr->len, hdr->flags);
		qp->rx_err_no_buf++;
		return -ENOMEM;
	}

	if (!(hdr->flags & DESC_DONE_FLAG)) {
		ntb_list_add(&qp->ntb_rx_pend_q_lock, &entry->entry,
			     &qp->rx_pend_q);
		qp->rx_ring_empty++;
		return -EAGAIN;
	}

	if (hdr->ver != (u32) qp->rx_pkts) {
		dev_dbg(&ntb_query_pdev(qp->ndev)->dev,
			"qp %d: version mismatch, expected %llu - got %u\n",
			qp->qp_num, qp->rx_pkts, hdr->ver);
		ntb_list_add(&qp->ntb_rx_pend_q_lock, &entry->entry,
			     &qp->rx_pend_q);
		qp->rx_err_ver++;
		return -EIO;
	}

	if (hdr->flags & LINK_DOWN_FLAG) {
		ntb_qp_link_down(qp);

		ntb_list_add(&qp->ntb_rx_pend_q_lock, &entry->entry,
			     &qp->rx_pend_q);
		goto out;
	}

	dev_dbg(&ntb_query_pdev(qp->ndev)->dev,
		"rx offset %u, ver %u - %d payload received, buf size %d\n",
		qp->rx_index, hdr->ver, hdr->len, entry->len);

	if (hdr->len <= entry->len) {
		entry->len = hdr->len;
		ntb_rx_copy_task(qp, entry, offset);
	} else {
		ntb_list_add(&qp->ntb_rx_pend_q_lock, &entry->entry,
			     &qp->rx_pend_q);

		qp->rx_err_oflow++;
		dev_dbg(&ntb_query_pdev(qp->ndev)->dev,
			"RX overflow! Wanted %d got %d\n",
			hdr->len, entry->len);
	}

	qp->rx_bytes += hdr->len;
	qp->rx_pkts++;

out:
	/* Ensure that the data is fully copied out before clearing the flag */
	wmb();
	hdr->flags = 0;
	iowrite32(qp->rx_index, &qp->rx_info->entry);

	qp->rx_index++;
	qp->rx_index %= qp->rx_max_entry;

	return 0;
}

static void ntb_transport_rx(unsigned long data)
{
	struct ntb_transport_qp *qp = (struct ntb_transport_qp *)data;
	int rc;

	do {
		rc = ntb_process_rxc(qp);
	} while (!rc);
}

static void ntb_transport_rxc_db(void *data, int db_num)
{
	struct ntb_transport_qp *qp = data;

	dev_dbg(&ntb_query_pdev(qp->ndev)->dev, "%s: doorbell %d received\n",
		__func__, db_num);

	tasklet_schedule(&qp->rx_work);
}

static void ntb_tx_copy_task(struct ntb_transport_qp *qp,
			     struct ntb_queue_entry *entry,
			     void __iomem *offset)
{
	struct ntb_payload_header __iomem *hdr;

	memcpy_toio(offset, entry->buf, entry->len);

	hdr = offset + qp->tx_max_frame - sizeof(struct ntb_payload_header);
	iowrite32(entry->len, &hdr->len);
	iowrite32((u32) qp->tx_pkts, &hdr->ver);

	/* Ensure that the data is fully copied out before setting the flag */
	wmb();
	iowrite32(entry->flags | DESC_DONE_FLAG, &hdr->flags);

	ntb_ring_sdb(qp->ndev, qp->qp_num);

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

static int ntb_process_tx(struct ntb_transport_qp *qp,
			  struct ntb_queue_entry *entry)
{
	void __iomem *offset;

	offset = qp->tx_mw + qp->tx_max_frame * qp->tx_index;

	dev_dbg(&ntb_query_pdev(qp->ndev)->dev, "%lld - offset %p, tx %u, entry len %d flags %x buff %p\n",
		qp->tx_pkts, offset, qp->tx_index, entry->len, entry->flags,
		entry->buf);
	if (qp->tx_index == qp->remote_rx_info->entry) {
		qp->tx_ring_full++;
		return -EAGAIN;
	}

	if (entry->len > qp->tx_max_frame - sizeof(struct ntb_payload_header)) {
		if (qp->tx_handler)
			qp->tx_handler(qp->cb_data, qp, NULL, -EIO);

		ntb_list_add(&qp->ntb_tx_free_q_lock, &entry->entry,
			     &qp->tx_free_q);
		return 0;
	}

	ntb_tx_copy_task(qp, entry, offset);

	qp->tx_index++;
	qp->tx_index %= qp->tx_max_entry;

	qp->tx_pkts++;

	return 0;
}

static void ntb_send_link_down(struct ntb_transport_qp *qp)
{
	struct pci_dev *pdev = ntb_query_pdev(qp->ndev);
	struct ntb_queue_entry *entry;
	int i, rc;

	if (qp->qp_link == NTB_LINK_DOWN)
		return;

	qp->qp_link = NTB_LINK_DOWN;
	dev_info(&pdev->dev, "qp %d: Link Down\n", qp->qp_num);

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
ntb_transport_create_queue(void *data, struct pci_dev *pdev,
			   const struct ntb_queue_handlers *handlers)
{
	struct ntb_queue_entry *entry;
	struct ntb_transport_qp *qp;
	struct ntb_transport *nt;
	unsigned int free_queue;
	int rc, i;

	nt = ntb_find_transport(pdev);
	if (!nt)
		goto err;

	free_queue = ffs(nt->qp_bitmap);
	if (!free_queue)
		goto err;

	/* decrement free_queue to make it zero based */
	free_queue--;

	clear_bit(free_queue, &nt->qp_bitmap);

	qp = &nt->qps[free_queue];
	qp->cb_data = data;
	qp->rx_handler = handlers->rx_handler;
	qp->tx_handler = handlers->tx_handler;
	qp->event_handler = handlers->event_handler;

	for (i = 0; i < NTB_QP_DEF_NUM_ENTRIES; i++) {
		entry = kzalloc(sizeof(struct ntb_queue_entry), GFP_ATOMIC);
		if (!entry)
			goto err1;

		ntb_list_add(&qp->ntb_rx_free_q_lock, &entry->entry,
			     &qp->rx_free_q);
	}

	for (i = 0; i < NTB_QP_DEF_NUM_ENTRIES; i++) {
		entry = kzalloc(sizeof(struct ntb_queue_entry), GFP_ATOMIC);
		if (!entry)
			goto err2;

		ntb_list_add(&qp->ntb_tx_free_q_lock, &entry->entry,
			     &qp->tx_free_q);
	}

	tasklet_init(&qp->rx_work, ntb_transport_rx, (unsigned long) qp);

	rc = ntb_register_db_callback(qp->ndev, free_queue, qp,
				      ntb_transport_rxc_db);
	if (rc)
		goto err3;

	dev_info(&pdev->dev, "NTB Transport QP %d created\n", qp->qp_num);

	return qp;

err3:
	tasklet_disable(&qp->rx_work);
err2:
	while ((entry = ntb_list_rm(&qp->ntb_tx_free_q_lock, &qp->tx_free_q)))
		kfree(entry);
err1:
	while ((entry = ntb_list_rm(&qp->ntb_rx_free_q_lock, &qp->rx_free_q)))
		kfree(entry);
	set_bit(free_queue, &nt->qp_bitmap);
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

	if (!qp)
		return;

	pdev = ntb_query_pdev(qp->ndev);

	cancel_delayed_work_sync(&qp->link_work);

	ntb_unregister_db_callback(qp->ndev, qp->qp_num);
	tasklet_disable(&qp->rx_work);

	while ((entry = ntb_list_rm(&qp->ntb_rx_free_q_lock, &qp->rx_free_q)))
		kfree(entry);

	while ((entry = ntb_list_rm(&qp->ntb_rx_pend_q_lock, &qp->rx_pend_q))) {
		dev_warn(&pdev->dev, "Freeing item from a non-empty queue\n");
		kfree(entry);
	}

	while ((entry = ntb_list_rm(&qp->ntb_tx_free_q_lock, &qp->tx_free_q)))
		kfree(entry);

	set_bit(qp->qp_num, &qp->transport->qp_bitmap);

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

	if (!qp || qp->client_ready == NTB_LINK_UP)
		return NULL;

	entry = ntb_list_rm(&qp->ntb_rx_pend_q_lock, &qp->rx_pend_q);
	if (!entry)
		return NULL;

	buf = entry->cb_data;
	*len = entry->len;

	ntb_list_add(&qp->ntb_rx_free_q_lock, &entry->entry, &qp->rx_free_q);

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

	entry = ntb_list_rm(&qp->ntb_rx_free_q_lock, &qp->rx_free_q);
	if (!entry)
		return -ENOMEM;

	entry->cb_data = cb;
	entry->buf = data;
	entry->len = len;

	ntb_list_add(&qp->ntb_rx_pend_q_lock, &entry->entry, &qp->rx_pend_q);

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
 * payload will be transmitted.  This assumes that a lock is behing held to
 * serialize access to the qp.
 *
 * RETURNS: An appropriate -ERRNO error value on error, or zero for success.
 */
int ntb_transport_tx_enqueue(struct ntb_transport_qp *qp, void *cb, void *data,
			     unsigned int len)
{
	struct ntb_queue_entry *entry;
	int rc;

	if (!qp || qp->qp_link != NTB_LINK_UP || !len)
		return -EINVAL;

	entry = ntb_list_rm(&qp->ntb_tx_free_q_lock, &qp->tx_free_q);
	if (!entry)
		return -ENOMEM;

	entry->cb_data = cb;
	entry->buf = data;
	entry->len = len;
	entry->flags = 0;

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

	qp->client_ready = NTB_LINK_UP;

	if (qp->transport->transport_link == NTB_LINK_UP)
		schedule_delayed_work(&qp->link_work, 0);
}
EXPORT_SYMBOL_GPL(ntb_transport_link_up);

/**
 * ntb_transport_link_down - Notify NTB transport to no longer enqueue data
 * @qp: NTB transport layer queue to be disabled
 *
 * Notify NTB transport layer of client's desire to no longer receive data on
 * transport queue specified.  It is the client's responsibility to ensure all
 * entries on queue are purged or otherwise handled appropraitely.
 */
void ntb_transport_link_down(struct ntb_transport_qp *qp)
{
	struct pci_dev *pdev;
	int rc, val;

	if (!qp)
		return;

	pdev = ntb_query_pdev(qp->ndev);
	qp->client_ready = NTB_LINK_DOWN;

	rc = ntb_read_local_spad(qp->ndev, QP_LINKS, &val);
	if (rc) {
		dev_err(&pdev->dev, "Error reading spad %d\n", QP_LINKS);
		return;
	}

	rc = ntb_write_remote_spad(qp->ndev, QP_LINKS,
				   val & ~(1 << qp->qp_num));
	if (rc)
		dev_err(&pdev->dev, "Error writing %x to remote spad %d\n",
			val & ~(1 << qp->qp_num), QP_LINKS);

	if (qp->qp_link == NTB_LINK_UP)
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

	return qp->qp_link == NTB_LINK_UP;
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
	if (!qp)
		return 0;

	return qp->tx_max_frame - sizeof(struct ntb_payload_header);
}
EXPORT_SYMBOL_GPL(ntb_transport_max_size);
