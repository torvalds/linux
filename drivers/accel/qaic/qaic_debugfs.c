// SPDX-License-Identifier: GPL-2.0-only

/* Copyright (c) 2020, The Linux Foundation. All rights reserved. */
/* Copyright (c) 2021-2024 Qualcomm Innovation Center, Inc. All rights reserved. */

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/mhi.h>
#include <linux/mutex.h>
#include <linux/overflow.h>
#include <linux/pci.h>
#include <linux/seq_file.h>
#include <linux/sprintf.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include "qaic.h"
#include "qaic_debugfs.h"

#define BOOTLOG_POOL_SIZE		16
#define BOOTLOG_MSG_SIZE		512
#define QAIC_DBC_DIR_NAME		9

struct bootlog_msg {
	/* Buffer for bootlog messages */
	char str[BOOTLOG_MSG_SIZE];
	/* Root struct of device, used to access device resources */
	struct qaic_device *qdev;
	/* Work struct to schedule work coming on QAIC_LOGGING channel */
	struct work_struct work;
};

struct bootlog_page {
	/* Node in list of bootlog pages maintained by root device struct */
	struct list_head node;
	/* Total size of the buffer that holds the bootlogs. It is PAGE_SIZE */
	unsigned int size;
	/* Offset for the next bootlog */
	unsigned int offset;
};

static int bootlog_show(struct seq_file *s, void *unused)
{
	struct bootlog_page *page;
	struct qaic_device *qdev;
	void *page_end;
	void *log;

	qdev = s->private;
	mutex_lock(&qdev->bootlog_mutex);
	list_for_each_entry(page, &qdev->bootlog, node) {
		log = page + 1;
		page_end = (void *)page + page->offset;
		while (log < page_end) {
			seq_printf(s, "%s", (char *)log);
			log += strlen(log) + 1;
		}
	}
	mutex_unlock(&qdev->bootlog_mutex);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(bootlog);

static int fifo_size_show(struct seq_file *s, void *unused)
{
	struct dma_bridge_chan *dbc = s->private;

	seq_printf(s, "%u\n", dbc->nelem);
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(fifo_size);

static int queued_show(struct seq_file *s, void *unused)
{
	struct dma_bridge_chan *dbc = s->private;
	u32 tail = 0, head = 0;

	qaic_data_get_fifo_info(dbc, &head, &tail);

	if (head == U32_MAX || tail == U32_MAX)
		seq_printf(s, "%u\n", 0);
	else if (head > tail)
		seq_printf(s, "%u\n", dbc->nelem - head + tail);
	else
		seq_printf(s, "%u\n", tail - head);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(queued);

void qaic_debugfs_init(struct qaic_drm_device *qddev)
{
	struct qaic_device *qdev = qddev->qdev;
	struct dentry *debugfs_root;
	struct dentry *debugfs_dir;
	char name[QAIC_DBC_DIR_NAME];
	u32 i;

	debugfs_root = to_drm(qddev)->debugfs_root;

	debugfs_create_file("bootlog", 0400, debugfs_root, qdev, &bootlog_fops);
	/*
	 * 256 dbcs per device is likely the max we will ever see and lets static checking see a
	 * reasonable range.
	 */
	for (i = 0; i < qdev->num_dbc && i < 256; ++i) {
		snprintf(name, QAIC_DBC_DIR_NAME, "dbc%03u", i);
		debugfs_dir = debugfs_create_dir(name, debugfs_root);
		debugfs_create_file("fifo_size", 0400, debugfs_dir, &qdev->dbc[i], &fifo_size_fops);
		debugfs_create_file("queued", 0400, debugfs_dir, &qdev->dbc[i], &queued_fops);
	}
}

static struct bootlog_page *alloc_bootlog_page(struct qaic_device *qdev)
{
	struct bootlog_page *page;

	page = (struct bootlog_page *)devm_get_free_pages(&qdev->pdev->dev, GFP_KERNEL, 0);
	if (!page)
		return page;

	page->size = PAGE_SIZE;
	page->offset = sizeof(*page);
	list_add_tail(&page->node, &qdev->bootlog);

	return page;
}

static int reset_bootlog(struct qaic_device *qdev)
{
	struct bootlog_page *page;
	struct bootlog_page *i;

	mutex_lock(&qdev->bootlog_mutex);
	list_for_each_entry_safe(page, i, &qdev->bootlog, node) {
		list_del(&page->node);
		devm_free_pages(&qdev->pdev->dev, (unsigned long)page);
	}

	page = alloc_bootlog_page(qdev);
	mutex_unlock(&qdev->bootlog_mutex);
	if (!page)
		return -ENOMEM;

	return 0;
}

static void *bootlog_get_space(struct qaic_device *qdev, unsigned int size)
{
	struct bootlog_page *page;

	page = list_last_entry(&qdev->bootlog, struct bootlog_page, node);

	if (size_add(size, sizeof(*page)) > page->size)
		return NULL;

	if (page->offset + size > page->size) {
		page = alloc_bootlog_page(qdev);
		if (!page)
			return NULL;
	}

	return (void *)page + page->offset;
}

static void bootlog_commit(struct qaic_device *qdev, unsigned int size)
{
	struct bootlog_page *page;

	page = list_last_entry(&qdev->bootlog, struct bootlog_page, node);

	page->offset += size;
}

static void bootlog_log(struct work_struct *work)
{
	struct bootlog_msg *msg = container_of(work, struct bootlog_msg, work);
	unsigned int len = strlen(msg->str) + 1;
	struct qaic_device *qdev = msg->qdev;
	void *log;

	mutex_lock(&qdev->bootlog_mutex);
	log = bootlog_get_space(qdev, len);
	if (log) {
		memcpy(log, msg, len);
		bootlog_commit(qdev, len);
	}
	mutex_unlock(&qdev->bootlog_mutex);

	if (mhi_queue_buf(qdev->bootlog_ch, DMA_FROM_DEVICE, msg, BOOTLOG_MSG_SIZE, MHI_EOT))
		devm_kfree(&qdev->pdev->dev, msg);
}

static int qaic_bootlog_mhi_probe(struct mhi_device *mhi_dev, const struct mhi_device_id *id)
{
	struct qaic_device *qdev = pci_get_drvdata(to_pci_dev(mhi_dev->mhi_cntrl->cntrl_dev));
	struct bootlog_msg *msg;
	int i, ret;

	qdev->bootlog_wq = alloc_ordered_workqueue("qaic_bootlog", 0);
	if (!qdev->bootlog_wq) {
		ret = -ENOMEM;
		goto out;
	}

	ret = reset_bootlog(qdev);
	if (ret)
		goto destroy_workqueue;

	ret = mhi_prepare_for_transfer(mhi_dev);
	if (ret)
		goto destroy_workqueue;

	for (i = 0; i < BOOTLOG_POOL_SIZE; i++) {
		msg = devm_kzalloc(&qdev->pdev->dev, sizeof(*msg), GFP_KERNEL);
		if (!msg) {
			ret = -ENOMEM;
			goto mhi_unprepare;
		}

		msg->qdev = qdev;
		INIT_WORK(&msg->work, bootlog_log);

		ret = mhi_queue_buf(mhi_dev, DMA_FROM_DEVICE, msg, BOOTLOG_MSG_SIZE, MHI_EOT);
		if (ret)
			goto mhi_unprepare;
	}

	dev_set_drvdata(&mhi_dev->dev, qdev);
	qdev->bootlog_ch = mhi_dev;
	return 0;

mhi_unprepare:
	mhi_unprepare_from_transfer(mhi_dev);
destroy_workqueue:
	flush_workqueue(qdev->bootlog_wq);
	destroy_workqueue(qdev->bootlog_wq);
out:
	return ret;
}

static void qaic_bootlog_mhi_remove(struct mhi_device *mhi_dev)
{
	struct qaic_device *qdev;

	qdev = dev_get_drvdata(&mhi_dev->dev);

	mhi_unprepare_from_transfer(qdev->bootlog_ch);
	flush_workqueue(qdev->bootlog_wq);
	destroy_workqueue(qdev->bootlog_wq);
	qdev->bootlog_ch = NULL;
}

static void qaic_bootlog_mhi_ul_xfer_cb(struct mhi_device *mhi_dev, struct mhi_result *mhi_result)
{
}

static void qaic_bootlog_mhi_dl_xfer_cb(struct mhi_device *mhi_dev, struct mhi_result *mhi_result)
{
	struct qaic_device *qdev = dev_get_drvdata(&mhi_dev->dev);
	struct bootlog_msg *msg = mhi_result->buf_addr;

	if (mhi_result->transaction_status) {
		devm_kfree(&qdev->pdev->dev, msg);
		return;
	}

	/* Force a null at the end of the transferred string */
	msg->str[mhi_result->bytes_xferd - 1] = 0;

	queue_work(qdev->bootlog_wq, &msg->work);
}

static const struct mhi_device_id qaic_bootlog_mhi_match_table[] = {
	{ .chan = "QAIC_LOGGING", },
	{},
};

static struct mhi_driver qaic_bootlog_mhi_driver = {
	.id_table = qaic_bootlog_mhi_match_table,
	.remove = qaic_bootlog_mhi_remove,
	.probe = qaic_bootlog_mhi_probe,
	.ul_xfer_cb = qaic_bootlog_mhi_ul_xfer_cb,
	.dl_xfer_cb = qaic_bootlog_mhi_dl_xfer_cb,
	.driver = {
		.name = "qaic_bootlog",
	},
};

int qaic_bootlog_register(void)
{
	return mhi_driver_register(&qaic_bootlog_mhi_driver);
}

void qaic_bootlog_unregister(void)
{
	mhi_driver_unregister(&qaic_bootlog_mhi_driver);
}
