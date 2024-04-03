// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved. */

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/mhi.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/time64.h>
#include <linux/timer.h>

#include "qaic.h"
#include "qaic_timesync.h"

#define QTIMER_REG_OFFSET			0xa28
#define QAIC_TIMESYNC_SIGNATURE			0x55aa
#define QAIC_CONV_QTIMER_TO_US(qtimer)		(mul_u64_u32_div(qtimer, 10, 192))

static unsigned int timesync_delay_ms = 1000; /* 1 sec default */
module_param(timesync_delay_ms, uint, 0600);
MODULE_PARM_DESC(timesync_delay_ms, "Delay in ms between two consecutive timesync operations");

enum qts_msg_type {
	QAIC_TS_CMD_TO_HOST,
	QAIC_TS_SYNC_REQ,
	QAIC_TS_ACK_TO_HOST,
	QAIC_TS_MSG_TYPE_MAX
};

/**
 * struct qts_hdr - Timesync message header structure.
 * @signature: Unique signature to identify the timesync message.
 * @reserved_1: Reserved for future use.
 * @reserved_2: Reserved for future use.
 * @msg_type: sub-type of the timesync message.
 * @reserved_3: Reserved for future use.
 */
struct qts_hdr {
	__le16	signature;
	__le16	reserved_1;
	u8	reserved_2;
	u8	msg_type;
	__le16	reserved_3;
} __packed;

/**
 * struct qts_timeval - Structure to carry time information.
 * @tv_sec: Seconds part of the time.
 * @tv_usec: uS (microseconds) part of the time.
 */
struct qts_timeval {
	__le64	tv_sec;
	__le64	tv_usec;
} __packed;

/**
 * struct qts_host_time_sync_msg_data - Structure to denote the timesync message.
 * @header: Header of the timesync message.
 * @data: Time information.
 */
struct qts_host_time_sync_msg_data {
	struct	qts_hdr header;
	struct	qts_timeval data;
} __packed;

/**
 * struct mqts_dev - MHI QAIC Timesync Control device.
 * @qdev: Pointer to the root device struct driven by QAIC driver.
 * @mhi_dev: Pointer to associated MHI device.
 * @timer: Timer handle used for timesync.
 * @qtimer_addr: Device QTimer register pointer.
 * @buff_in_use: atomic variable to track if the sync_msg buffer is in use.
 * @dev: Device pointer to qdev->pdev->dev stored for easy access.
 * @sync_msg: Buffer used to send timesync message over MHI.
 */
struct mqts_dev {
	struct qaic_device *qdev;
	struct mhi_device *mhi_dev;
	struct timer_list timer;
	void __iomem *qtimer_addr;
	atomic_t buff_in_use;
	struct device *dev;
	struct qts_host_time_sync_msg_data *sync_msg;
};

struct qts_resp_msg {
	struct qts_hdr	hdr;
} __packed;

struct qts_resp {
	struct qts_resp_msg	data;
	struct work_struct	work;
	struct qaic_device	*qdev;
};

#ifdef readq
static u64 read_qtimer(const volatile void __iomem *addr)
{
	return readq(addr);
}
#else
static u64 read_qtimer(const volatile void __iomem *addr)
{
	u64 low, high;

	low = readl(addr);
	high = readl(addr + sizeof(u32));
	return low | (high << 32);
}
#endif

static void qaic_timesync_ul_xfer_cb(struct mhi_device *mhi_dev, struct mhi_result *mhi_result)
{
	struct mqts_dev *mqtsdev = dev_get_drvdata(&mhi_dev->dev);

	dev_dbg(mqtsdev->dev, "%s status: %d xfer_len: %zu\n", __func__,
		mhi_result->transaction_status, mhi_result->bytes_xferd);

	atomic_set(&mqtsdev->buff_in_use, 0);
}

static void qaic_timesync_dl_xfer_cb(struct mhi_device *mhi_dev, struct mhi_result *mhi_result)
{
	struct mqts_dev *mqtsdev = dev_get_drvdata(&mhi_dev->dev);

	dev_err(mqtsdev->dev, "%s no data expected on dl channel\n", __func__);
}

static void qaic_timesync_timer(struct timer_list *t)
{
	struct mqts_dev *mqtsdev = from_timer(mqtsdev, t, timer);
	struct qts_host_time_sync_msg_data *sync_msg;
	u64 device_qtimer_us;
	u64 device_qtimer;
	u64 host_time_us;
	u64 offset_us;
	u64 host_sec;
	int ret;

	if (atomic_read(&mqtsdev->buff_in_use)) {
		dev_dbg(mqtsdev->dev, "%s buffer not free, schedule next cycle\n", __func__);
		goto mod_timer;
	}
	atomic_set(&mqtsdev->buff_in_use, 1);

	sync_msg = mqtsdev->sync_msg;
	sync_msg->header.signature = cpu_to_le16(QAIC_TIMESYNC_SIGNATURE);
	sync_msg->header.msg_type = QAIC_TS_SYNC_REQ;
	/* Read host UTC time and convert to uS*/
	host_time_us = div_u64(ktime_get_real_ns(), NSEC_PER_USEC);
	device_qtimer = read_qtimer(mqtsdev->qtimer_addr);
	device_qtimer_us = QAIC_CONV_QTIMER_TO_US(device_qtimer);
	/* Offset between host UTC and device time */
	offset_us = host_time_us - device_qtimer_us;

	host_sec = div_u64(offset_us, USEC_PER_SEC);
	sync_msg->data.tv_usec = cpu_to_le64(offset_us - host_sec * USEC_PER_SEC);
	sync_msg->data.tv_sec = cpu_to_le64(host_sec);
	ret = mhi_queue_buf(mqtsdev->mhi_dev, DMA_TO_DEVICE, sync_msg, sizeof(*sync_msg), MHI_EOT);
	if (ret && (ret != -EAGAIN)) {
		dev_err(mqtsdev->dev, "%s unable to queue to mhi:%d\n", __func__, ret);
		return;
	} else if (ret == -EAGAIN) {
		atomic_set(&mqtsdev->buff_in_use, 0);
	}

mod_timer:
	ret = mod_timer(t, jiffies + msecs_to_jiffies(timesync_delay_ms));
	if (ret)
		dev_err(mqtsdev->dev, "%s mod_timer error:%d\n", __func__, ret);
}

static int qaic_timesync_probe(struct mhi_device *mhi_dev, const struct mhi_device_id *id)
{
	struct qaic_device *qdev = pci_get_drvdata(to_pci_dev(mhi_dev->mhi_cntrl->cntrl_dev));
	struct mqts_dev *mqtsdev;
	struct timer_list *timer;
	int ret;

	mqtsdev = kzalloc(sizeof(*mqtsdev), GFP_KERNEL);
	if (!mqtsdev) {
		ret = -ENOMEM;
		goto out;
	}

	timer = &mqtsdev->timer;
	mqtsdev->mhi_dev = mhi_dev;
	mqtsdev->qdev = qdev;
	mqtsdev->dev = &qdev->pdev->dev;

	mqtsdev->sync_msg = kzalloc(sizeof(*mqtsdev->sync_msg), GFP_KERNEL);
	if (!mqtsdev->sync_msg) {
		ret = -ENOMEM;
		goto free_mqts_dev;
	}
	atomic_set(&mqtsdev->buff_in_use, 0);

	ret = mhi_prepare_for_transfer(mhi_dev);
	if (ret)
		goto free_sync_msg;

	/* Qtimer register pointer */
	mqtsdev->qtimer_addr = qdev->bar_0 + QTIMER_REG_OFFSET;
	timer_setup(timer, qaic_timesync_timer, 0);
	timer->expires = jiffies + msecs_to_jiffies(timesync_delay_ms);
	add_timer(timer);
	dev_set_drvdata(&mhi_dev->dev, mqtsdev);

	return 0;

free_sync_msg:
	kfree(mqtsdev->sync_msg);
free_mqts_dev:
	kfree(mqtsdev);
out:
	return ret;
};

static void qaic_timesync_remove(struct mhi_device *mhi_dev)
{
	struct mqts_dev *mqtsdev = dev_get_drvdata(&mhi_dev->dev);

	del_timer_sync(&mqtsdev->timer);
	mhi_unprepare_from_transfer(mqtsdev->mhi_dev);
	kfree(mqtsdev->sync_msg);
	kfree(mqtsdev);
}

static const struct mhi_device_id qaic_timesync_match_table[] = {
	{ .chan = "QAIC_TIMESYNC_PERIODIC"},
	{},
};

MODULE_DEVICE_TABLE(mhi, qaic_timesync_match_table);

static struct mhi_driver qaic_timesync_driver = {
	.id_table = qaic_timesync_match_table,
	.remove = qaic_timesync_remove,
	.probe = qaic_timesync_probe,
	.ul_xfer_cb = qaic_timesync_ul_xfer_cb,
	.dl_xfer_cb = qaic_timesync_dl_xfer_cb,
	.driver = {
		.name = "qaic_timesync_periodic",
	},
};

static void qaic_boot_timesync_worker(struct work_struct *work)
{
	struct qts_resp *resp = container_of(work, struct qts_resp, work);
	struct qts_host_time_sync_msg_data *req;
	struct qts_resp_msg data = resp->data;
	struct qaic_device *qdev = resp->qdev;
	struct mhi_device *mhi_dev;
	struct timespec64 ts;
	int ret;

	mhi_dev = qdev->qts_ch;
	/* Queue the response message beforehand to avoid race conditions */
	ret = mhi_queue_buf(mhi_dev, DMA_FROM_DEVICE, &resp->data, sizeof(resp->data), MHI_EOT);
	if (ret) {
		kfree(resp);
		dev_warn(&mhi_dev->dev, "Failed to re-queue response buffer %d\n", ret);
		return;
	}

	switch (data.hdr.msg_type) {
	case QAIC_TS_CMD_TO_HOST:
		req = kzalloc(sizeof(*req), GFP_KERNEL);
		if (!req)
			break;

		req->header = data.hdr;
		req->header.msg_type = QAIC_TS_SYNC_REQ;
		ktime_get_real_ts64(&ts);
		req->data.tv_sec = cpu_to_le64(ts.tv_sec);
		req->data.tv_usec = cpu_to_le64(div_u64(ts.tv_nsec, NSEC_PER_USEC));

		ret = mhi_queue_buf(mhi_dev, DMA_TO_DEVICE, req, sizeof(*req), MHI_EOT);
		if (ret) {
			kfree(req);
			dev_dbg(&mhi_dev->dev, "Failed to send request message. Error %d\n", ret);
		}
		break;
	case QAIC_TS_ACK_TO_HOST:
		dev_dbg(&mhi_dev->dev, "ACK received from device\n");
		break;
	default:
		dev_err(&mhi_dev->dev, "Invalid message type %u.\n", data.hdr.msg_type);
	}
}

static int qaic_boot_timesync_queue_resp(struct mhi_device *mhi_dev, struct qaic_device *qdev)
{
	struct qts_resp *resp;
	int ret;

	resp = kzalloc(sizeof(*resp), GFP_KERNEL);
	if (!resp)
		return -ENOMEM;

	resp->qdev = qdev;
	INIT_WORK(&resp->work, qaic_boot_timesync_worker);

	ret = mhi_queue_buf(mhi_dev, DMA_FROM_DEVICE, &resp->data, sizeof(resp->data), MHI_EOT);
	if (ret) {
		kfree(resp);
		dev_warn(&mhi_dev->dev, "Failed to queue response buffer %d\n", ret);
		return ret;
	}

	return 0;
}

static void qaic_boot_timesync_remove(struct mhi_device *mhi_dev)
{
	struct qaic_device *qdev;

	qdev = dev_get_drvdata(&mhi_dev->dev);
	mhi_unprepare_from_transfer(qdev->qts_ch);
	qdev->qts_ch = NULL;
}

static int qaic_boot_timesync_probe(struct mhi_device *mhi_dev, const struct mhi_device_id *id)
{
	struct qaic_device *qdev = pci_get_drvdata(to_pci_dev(mhi_dev->mhi_cntrl->cntrl_dev));
	int ret;

	ret = mhi_prepare_for_transfer(mhi_dev);
	if (ret)
		return ret;

	qdev->qts_ch = mhi_dev;
	dev_set_drvdata(&mhi_dev->dev, qdev);

	ret = qaic_boot_timesync_queue_resp(mhi_dev, qdev);
	if (ret) {
		dev_set_drvdata(&mhi_dev->dev, NULL);
		qdev->qts_ch = NULL;
		mhi_unprepare_from_transfer(mhi_dev);
	}

	return ret;
}

static void qaic_boot_timesync_ul_xfer_cb(struct mhi_device *mhi_dev, struct mhi_result *mhi_result)
{
	kfree(mhi_result->buf_addr);
}

static void qaic_boot_timesync_dl_xfer_cb(struct mhi_device *mhi_dev, struct mhi_result *mhi_result)
{
	struct qts_resp *resp = container_of(mhi_result->buf_addr, struct qts_resp, data);

	if (mhi_result->transaction_status || mhi_result->bytes_xferd != sizeof(resp->data)) {
		kfree(resp);
		return;
	}

	queue_work(resp->qdev->qts_wq, &resp->work);
}

static const struct mhi_device_id qaic_boot_timesync_match_table[] = {
	{ .chan = "QAIC_TIMESYNC"},
	{},
};

static struct mhi_driver qaic_boot_timesync_driver = {
	.id_table = qaic_boot_timesync_match_table,
	.remove = qaic_boot_timesync_remove,
	.probe = qaic_boot_timesync_probe,
	.ul_xfer_cb = qaic_boot_timesync_ul_xfer_cb,
	.dl_xfer_cb = qaic_boot_timesync_dl_xfer_cb,
	.driver = {
		.name = "qaic_timesync",
	},
};

int qaic_timesync_init(void)
{
	int ret;

	ret = mhi_driver_register(&qaic_timesync_driver);
	if (ret)
		return ret;
	ret = mhi_driver_register(&qaic_boot_timesync_driver);

	return ret;
}

void qaic_timesync_deinit(void)
{
	mhi_driver_unregister(&qaic_boot_timesync_driver);
	mhi_driver_unregister(&qaic_timesync_driver);
}
