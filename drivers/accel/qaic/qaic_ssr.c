// SPDX-License-Identifier: GPL-2.0-only

/* Copyright (c) 2020-2021, The Linux Foundation. All rights reserved. */
/* Copyright (c) 2021-2024 Qualcomm Innovation Center, Inc. All rights reserved. */

#include <asm/byteorder.h>
#include <drm/drm_file.h>
#include <drm/drm_managed.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/mhi.h>
#include <linux/workqueue.h>

#include "qaic.h"
#include "qaic_ssr.h"

#define SSR_RESP_MSG_SZ 32

#define DEBUG_TRANSFER_INFO		BIT(0)
#define DEBUG_TRANSFER_INFO_RSP		BIT(1)
#define MEMORY_READ			BIT(2)
#define MEMORY_READ_RSP			BIT(3)
#define DEBUG_TRANSFER_DONE		BIT(4)
#define DEBUG_TRANSFER_DONE_RSP		BIT(5)
#define SSR_EVENT			BIT(8)
#define SSR_EVENT_RSP			BIT(9)

#define SSR_EVENT_NACK		BIT(0)
#define BEFORE_SHUTDOWN		BIT(1)
#define AFTER_SHUTDOWN		BIT(2)
#define BEFORE_POWER_UP		BIT(3)
#define AFTER_POWER_UP		BIT(4)

struct _ssr_hdr {
	__le32 cmd;
	__le32 len;
	__le32 dbc_id;
};

struct ssr_hdr {
	u32 cmd;
	u32 len;
	u32 dbc_id;
};

struct ssr_debug_transfer_info_rsp {
	struct _ssr_hdr hdr;
	__le32 ret;
} __packed;

struct ssr_event {
	struct ssr_hdr hdr;
	u32 event;
} __packed;

struct ssr_event_rsp {
	struct _ssr_hdr hdr;
	__le32 event;
} __packed;

struct ssr_resp {
	/* Work struct to schedule work coming on QAIC_SSR channel */
	struct work_struct work;
	/* Root struct of device, used to access device resources */
	struct qaic_device *qdev;
	/* Buffer used by MHI for transfer requests */
	u8 data[] __aligned(8);
};

void qaic_clean_up_ssr(struct qaic_device *qdev)
{
	qaic_dbc_exit_ssr(qdev);
}

static void ssr_worker(struct work_struct *work)
{
	struct ssr_resp *resp = container_of(work, struct ssr_resp, work);
	struct ssr_hdr *hdr = (struct ssr_hdr *)resp->data;
	struct ssr_debug_transfer_info_rsp *debug_rsp;
	struct qaic_device *qdev = resp->qdev;
	struct ssr_event_rsp *event_rsp;
	struct dma_bridge_chan *dbc;
	struct ssr_event *event;
	u32 ssr_event_ack;
	int ret;

	le32_to_cpus(&hdr->cmd);
	le32_to_cpus(&hdr->len);
	le32_to_cpus(&hdr->dbc_id);

	if (hdr->len > SSR_RESP_MSG_SZ)
		goto out;

	if (hdr->dbc_id >= qdev->num_dbc)
		goto out;

	dbc = &qdev->dbc[hdr->dbc_id];

	switch (hdr->cmd) {
	case DEBUG_TRANSFER_INFO:
		/* Decline crash dump request from the device */
		debug_rsp = kmalloc(sizeof(*debug_rsp), GFP_KERNEL);
		if (!debug_rsp)
			break;

		debug_rsp->hdr.cmd = cpu_to_le32(DEBUG_TRANSFER_INFO_RSP);
		debug_rsp->hdr.len = cpu_to_le32(sizeof(*debug_rsp));
		debug_rsp->hdr.dbc_id = cpu_to_le32(event->hdr.dbc_id);
		debug_rsp->ret = cpu_to_le32(1);

		ret = mhi_queue_buf(qdev->ssr_ch, DMA_TO_DEVICE,
				    debug_rsp, sizeof(*debug_rsp), MHI_EOT);
		if (ret) {
			pci_warn(qdev->pdev, "Could not send DEBUG_TRANSFER_INFO_RSP %d\n", ret);
			kfree(debug_rsp);
		}
		return;
	case SSR_EVENT:
		event = (struct ssr_event *)hdr;
		le32_to_cpus(&event->event);
		ssr_event_ack = event->event;

		switch (event->event) {
		case BEFORE_SHUTDOWN:
			set_dbc_state(qdev, hdr->dbc_id, DBC_STATE_BEFORE_SHUTDOWN);
			qaic_dbc_enter_ssr(qdev, hdr->dbc_id);
			break;
		case AFTER_SHUTDOWN:
			set_dbc_state(qdev, hdr->dbc_id, DBC_STATE_AFTER_SHUTDOWN);
			break;
		case BEFORE_POWER_UP:
			set_dbc_state(qdev, hdr->dbc_id, DBC_STATE_BEFORE_POWER_UP);
			break;
		case AFTER_POWER_UP:
			set_dbc_state(qdev, hdr->dbc_id, DBC_STATE_AFTER_POWER_UP);
			break;
		default:
			break;
		}

		event_rsp = kmalloc(sizeof(*event_rsp), GFP_KERNEL);
		if (!event_rsp)
			break;

		event_rsp->hdr.cmd = cpu_to_le32(SSR_EVENT_RSP);
		event_rsp->hdr.len = cpu_to_le32(sizeof(*event_rsp));
		event_rsp->hdr.dbc_id = cpu_to_le32(hdr->dbc_id);
		event_rsp->event = cpu_to_le32(ssr_event_ack);

		ret = mhi_queue_buf(qdev->ssr_ch, DMA_TO_DEVICE, event_rsp, sizeof(*event_rsp),
				    MHI_EOT);
		if (ret)
			kfree(event_rsp);

		if (event->event == AFTER_POWER_UP) {
			qaic_dbc_exit_ssr(qdev);
			set_dbc_state(qdev, hdr->dbc_id, DBC_STATE_IDLE);
		}

		break;
	default:
		break;
	}

out:
	ret = mhi_queue_buf(qdev->ssr_ch, DMA_FROM_DEVICE, resp->data, SSR_RESP_MSG_SZ, MHI_EOT);
	if (ret)
		kfree(resp);
}

static int qaic_ssr_mhi_probe(struct mhi_device *mhi_dev, const struct mhi_device_id *id)
{
	struct qaic_device *qdev = pci_get_drvdata(to_pci_dev(mhi_dev->mhi_cntrl->cntrl_dev));
	struct ssr_resp *resp;
	int ret;

	ret = mhi_prepare_for_transfer(mhi_dev);
	if (ret)
		return ret;

	resp = kzalloc(sizeof(*resp) + SSR_RESP_MSG_SZ, GFP_KERNEL);
	if (!resp) {
		mhi_unprepare_from_transfer(mhi_dev);
		return -ENOMEM;
	}

	resp->qdev = qdev;
	INIT_WORK(&resp->work, ssr_worker);

	ret = mhi_queue_buf(mhi_dev, DMA_FROM_DEVICE, resp->data, SSR_RESP_MSG_SZ, MHI_EOT);
	if (ret) {
		kfree(resp);
		mhi_unprepare_from_transfer(mhi_dev);
		return ret;
	}

	dev_set_drvdata(&mhi_dev->dev, qdev);
	qdev->ssr_ch = mhi_dev;

	return 0;
}

static void qaic_ssr_mhi_remove(struct mhi_device *mhi_dev)
{
	struct qaic_device *qdev;

	qdev = dev_get_drvdata(&mhi_dev->dev);
	mhi_unprepare_from_transfer(qdev->ssr_ch);
	qdev->ssr_ch = NULL;
}

static void qaic_ssr_mhi_ul_xfer_cb(struct mhi_device *mhi_dev, struct mhi_result *mhi_result)
{
	kfree(mhi_result->buf_addr);
}

static void qaic_ssr_mhi_dl_xfer_cb(struct mhi_device *mhi_dev, struct mhi_result *mhi_result)
{
	struct ssr_resp *resp = container_of(mhi_result->buf_addr, struct ssr_resp, data);
	struct qaic_device *qdev = dev_get_drvdata(&mhi_dev->dev);

	if (mhi_result->transaction_status) {
		kfree(resp);
		return;
	}
	queue_work(qdev->ssr_wq, &resp->work);
}

static const struct mhi_device_id qaic_ssr_mhi_match_table[] = {
	{ .chan = "QAIC_SSR", },
	{},
};

static struct mhi_driver qaic_ssr_mhi_driver = {
	.id_table = qaic_ssr_mhi_match_table,
	.remove = qaic_ssr_mhi_remove,
	.probe = qaic_ssr_mhi_probe,
	.ul_xfer_cb = qaic_ssr_mhi_ul_xfer_cb,
	.dl_xfer_cb = qaic_ssr_mhi_dl_xfer_cb,
	.driver = {
		.name = "qaic_ssr",
	},
};

int qaic_ssr_init(struct qaic_device *qdev)
{
	qdev->ssr_dbc = QAIC_SSR_DBC_SENTINEL;
	return 0;
}

int qaic_ssr_register(void)
{
	return mhi_driver_register(&qaic_ssr_mhi_driver);
}

void qaic_ssr_unregister(void)
{
	mhi_driver_unregister(&qaic_ssr_mhi_driver);
}
