// SPDX-License-Identifier: GPL-2.0-only

/* Copyright (c) 2020-2021, The Linux Foundation. All rights reserved. */
/* Copyright (c) 2021-2024 Qualcomm Innovation Center, Inc. All rights reserved. */

#include <asm/byteorder.h>
#include <drm/drm_file.h>
#include <drm/drm_managed.h>
#include <linux/devcoredump.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/mhi.h>
#include <linux/workqueue.h>

#include "qaic.h"
#include "qaic_ssr.h"

#define SSR_RESP_MSG_SZ 32
#define SSR_MHI_BUF_SIZE SZ_64K
#define SSR_MEM_READ_DATA_SIZE ((u64)SSR_MHI_BUF_SIZE - sizeof(struct ssr_crashdump))
#define SSR_MEM_READ_CHUNK_SIZE ((u64)SSR_MEM_READ_DATA_SIZE - sizeof(struct ssr_memory_read_rsp))

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

struct debug_info_table {
	/* Save preferences. Default is mandatory */
	u64 save_perf;
	/* Base address of the debug region */
	u64 mem_base;
	/* Size of debug region in bytes */
	u64 len;
	/* Description */
	char desc[20];
	/* Filename of debug region */
	char filename[20];
};

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

struct ssr_debug_transfer_info {
	struct ssr_hdr hdr;
	u32 resv;
	u64 tbl_addr;
	u64 tbl_len;
} __packed;

struct ssr_debug_transfer_info_rsp {
	struct _ssr_hdr hdr;
	__le32 ret;
} __packed;

struct ssr_memory_read {
	struct _ssr_hdr hdr;
	__le32 resv;
	__le64 addr;
	__le64 len;
} __packed;

struct ssr_memory_read_rsp {
	struct _ssr_hdr hdr;
	__le32 resv;
	u8 data[];
} __packed;

struct ssr_debug_transfer_done {
	struct _ssr_hdr hdr;
	__le32 resv;
} __packed;

struct ssr_debug_transfer_done_rsp {
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

/* SSR crashdump book keeping structure */
struct ssr_dump_info {
	/* DBC associated with this SSR crashdump */
	struct dma_bridge_chan *dbc;
	/*
	 * It will be used when we complete the crashdump download and switch
	 * to waiting on SSR events
	 */
	struct ssr_resp *resp;
	/* MEMORY READ request MHI buffer.*/
	struct ssr_memory_read *read_buf_req;
	/* TRUE: ->read_buf_req is queued for MHI transaction. FALSE: Otherwise */
	bool read_buf_req_queued;
	/* Address of table in host */
	void *tbl_addr;
	/* Total size of table */
	u64 tbl_len;
	/* Offset of table(->tbl_addr) where the new chunk will be dumped */
	u64 tbl_off;
	/* Address of table in device/target */
	u64 tbl_addr_dev;
	/* Ptr to the entire dump */
	void *dump_addr;
	/* Entire crashdump size */
	u64 dump_sz;
	/* Offset of crashdump(->dump_addr) where the new chunk will be dumped */
	u64 dump_off;
	/* Points to the table entry we are currently downloading */
	struct debug_info_table *tbl_ent;
	/* Offset in the current table entry(->tbl_ent) for next chuck */
	u64 tbl_ent_off;
};

struct ssr_crashdump {
	/*
	 * Points to a book keeping struct maintained by MHI SSR device while
	 * downloading a SSR crashdump. It is NULL when crashdump downloading
	 * not in progress.
	 */
	struct ssr_dump_info *dump_info;
	/* Work struct to schedule work coming on QAIC_SSR channel */
	struct work_struct work;
	/* Root struct of device, used to access device resources */
	struct qaic_device *qdev;
	/* Buffer used by MHI for transfer requests */
	u8 data[];
};

#define QAIC_SSR_DUMP_V1_MAGIC 0x1234567890abcdef
#define QAIC_SSR_DUMP_V1_VER   1
struct dump_file_meta {
	u64 magic;
	u64 version;
	u64 size;		/* Total size of the entire dump */
	u64 tbl_len;		/* Length of the table in byte */
};

/*
 * Layout of crashdump
 *              +------------------------------------------+
 *              |         Crashdump Meta structure         |
 *              | type: struct dump_file_meta              |
 *              +------------------------------------------+
 *              |             Crashdump Table              |
 *              | type: array of struct debug_info_table   |
 *              |                                          |
 *              |                                          |
 *              |                                          |
 *              +------------------------------------------+
 *              |                Crashdump                 |
 *              |                                          |
 *              |                                          |
 *              |                                          |
 *              |                                          |
 *              |                                          |
 *              +------------------------------------------+
 */

static void free_ssr_dump_info(struct ssr_crashdump *ssr_crash)
{
	struct ssr_dump_info *dump_info = ssr_crash->dump_info;

	ssr_crash->dump_info = NULL;
	if (!dump_info)
		return;
	if (!dump_info->read_buf_req_queued)
		kfree(dump_info->read_buf_req);
	vfree(dump_info->tbl_addr);
	vfree(dump_info->dump_addr);
	kfree(dump_info);
}

void qaic_clean_up_ssr(struct qaic_device *qdev)
{
	struct ssr_crashdump *ssr_crash = qdev->ssr_mhi_buf;

	if (!ssr_crash)
		return;

	qaic_dbc_exit_ssr(qdev);
	free_ssr_dump_info(ssr_crash);
}

static int alloc_dump(struct ssr_dump_info *dump_info)
{
	struct debug_info_table *tbl_ent = dump_info->tbl_addr;
	struct dump_file_meta *dump_meta;
	u64 tbl_sz_lp = 0;
	u64 dump_size = 0;

	while (tbl_sz_lp < dump_info->tbl_len) {
		le64_to_cpus(&tbl_ent->save_perf);
		le64_to_cpus(&tbl_ent->mem_base);
		le64_to_cpus(&tbl_ent->len);

		if (tbl_ent->len == 0)
			return -EINVAL;

		dump_size += tbl_ent->len;
		tbl_ent++;
		tbl_sz_lp += sizeof(*tbl_ent);
	}

	dump_info->dump_sz = dump_size + dump_info->tbl_len + sizeof(*dump_meta);
	dump_info->dump_addr = vzalloc(dump_info->dump_sz);
	if (!dump_info->dump_addr)
		return -ENOMEM;

	/* Copy crashdump meta and table */
	dump_meta = dump_info->dump_addr;
	dump_meta->magic = QAIC_SSR_DUMP_V1_MAGIC;
	dump_meta->version = QAIC_SSR_DUMP_V1_VER;
	dump_meta->size = dump_info->dump_sz;
	dump_meta->tbl_len = dump_info->tbl_len;
	memcpy(dump_info->dump_addr + sizeof(*dump_meta), dump_info->tbl_addr, dump_info->tbl_len);
	/* Offset by crashdump meta and table (copied above) */
	dump_info->dump_off = dump_info->tbl_len + sizeof(*dump_meta);

	return 0;
}

static int send_xfer_done(struct qaic_device *qdev, void *resp, u32 dbc_id)
{
	struct ssr_debug_transfer_done *xfer_done;
	int ret;

	xfer_done = kmalloc(sizeof(*xfer_done), GFP_KERNEL);
	if (!xfer_done) {
		ret = -ENOMEM;
		goto out;
	}

	ret = mhi_queue_buf(qdev->ssr_ch, DMA_FROM_DEVICE, resp, SSR_RESP_MSG_SZ, MHI_EOT);
	if (ret)
		goto free_xfer_done;

	xfer_done->hdr.cmd = cpu_to_le32(DEBUG_TRANSFER_DONE);
	xfer_done->hdr.len = cpu_to_le32(sizeof(*xfer_done));
	xfer_done->hdr.dbc_id = cpu_to_le32(dbc_id);

	ret = mhi_queue_buf(qdev->ssr_ch, DMA_TO_DEVICE, xfer_done, sizeof(*xfer_done), MHI_EOT);
	if (ret)
		goto free_xfer_done;

	return 0;

free_xfer_done:
	kfree(xfer_done);
out:
	return ret;
}

static int mem_read_req(struct qaic_device *qdev, u64 dest_addr, u64 dest_len)
{
	struct ssr_crashdump *ssr_crash = qdev->ssr_mhi_buf;
	struct ssr_memory_read *read_buf_req;
	struct ssr_dump_info *dump_info;
	int ret;

	dump_info = ssr_crash->dump_info;
	ret = mhi_queue_buf(qdev->ssr_ch, DMA_FROM_DEVICE, ssr_crash->data, SSR_MEM_READ_DATA_SIZE,
			    MHI_EOT);
	if (ret)
		goto out;

	read_buf_req = dump_info->read_buf_req;
	read_buf_req->hdr.cmd = cpu_to_le32(MEMORY_READ);
	read_buf_req->hdr.len = cpu_to_le32(sizeof(*read_buf_req));
	read_buf_req->hdr.dbc_id = cpu_to_le32(qdev->ssr_dbc);
	read_buf_req->addr = cpu_to_le64(dest_addr);
	read_buf_req->len = cpu_to_le64(dest_len);

	ret = mhi_queue_buf(qdev->ssr_ch, DMA_TO_DEVICE, read_buf_req, sizeof(*read_buf_req),
			    MHI_EOT);
	if (!ret)
		dump_info->read_buf_req_queued = true;

out:
	return ret;
}

static int ssr_copy_table(struct ssr_dump_info *dump_info, void *data, u64 len)
{
	if (len > dump_info->tbl_len - dump_info->tbl_off)
		return -EINVAL;

	memcpy(dump_info->tbl_addr + dump_info->tbl_off, data, len);
	dump_info->tbl_off += len;

	/* Entire table has been downloaded, alloc dump memory */
	if (dump_info->tbl_off == dump_info->tbl_len) {
		dump_info->tbl_ent = dump_info->tbl_addr;
		return alloc_dump(dump_info);
	}

	return 0;
}

static int ssr_copy_dump(struct ssr_dump_info *dump_info, void *data, u64 len)
{
	struct debug_info_table *tbl_ent;

	tbl_ent = dump_info->tbl_ent;

	if (len > tbl_ent->len - dump_info->tbl_ent_off)
		return -EINVAL;

	memcpy(dump_info->dump_addr + dump_info->dump_off, data, len);
	dump_info->dump_off += len;
	dump_info->tbl_ent_off += len;

	/*
	 * Current segment (a entry in table) of the crashdump is complete,
	 * move to next one
	 */
	if (tbl_ent->len == dump_info->tbl_ent_off) {
		dump_info->tbl_ent++;
		dump_info->tbl_ent_off = 0;
	}

	return 0;
}

static void ssr_dump_worker(struct work_struct *work)
{
	struct ssr_crashdump *ssr_crash = container_of(work, struct ssr_crashdump, work);
	struct qaic_device *qdev = ssr_crash->qdev;
	struct ssr_memory_read_rsp *mem_rd_resp;
	struct debug_info_table *tbl_ent;
	struct ssr_dump_info *dump_info;
	u64 dest_addr, dest_len;
	struct _ssr_hdr *_hdr;
	struct ssr_hdr hdr;
	u64 data_len;
	int ret;

	mem_rd_resp = (struct ssr_memory_read_rsp *)ssr_crash->data;
	_hdr = &mem_rd_resp->hdr;
	hdr.cmd = le32_to_cpu(_hdr->cmd);
	hdr.len = le32_to_cpu(_hdr->len);
	hdr.dbc_id = le32_to_cpu(_hdr->dbc_id);

	if (hdr.dbc_id != qdev->ssr_dbc)
		goto reset_device;

	dump_info = ssr_crash->dump_info;
	if (!dump_info)
		goto reset_device;

	if (hdr.cmd != MEMORY_READ_RSP)
		goto free_dump_info;

	if (hdr.len > SSR_MEM_READ_DATA_SIZE)
		goto free_dump_info;

	data_len = hdr.len - sizeof(*mem_rd_resp);

	if (dump_info->tbl_off < dump_info->tbl_len) /* Chunk belongs to table */
		ret = ssr_copy_table(dump_info, mem_rd_resp->data, data_len);
	else /* Chunk belongs to crashdump */
		ret = ssr_copy_dump(dump_info, mem_rd_resp->data, data_len);

	if (ret)
		goto free_dump_info;

	if (dump_info->tbl_off < dump_info->tbl_len) {
		/* Continue downloading table */
		dest_addr = dump_info->tbl_addr_dev + dump_info->tbl_off;
		dest_len = min(SSR_MEM_READ_CHUNK_SIZE, dump_info->tbl_len - dump_info->tbl_off);
		ret = mem_read_req(qdev, dest_addr, dest_len);
	} else if (dump_info->dump_off < dump_info->dump_sz) {
		/* Continue downloading crashdump */
		tbl_ent = dump_info->tbl_ent;
		dest_addr = tbl_ent->mem_base + dump_info->tbl_ent_off;
		dest_len = min(SSR_MEM_READ_CHUNK_SIZE, tbl_ent->len - dump_info->tbl_ent_off);
		ret = mem_read_req(qdev, dest_addr, dest_len);
	} else {
		/* Crashdump download complete */
		ret = send_xfer_done(qdev, dump_info->resp->data, hdr.dbc_id);
	}

	/* Most likely a MHI xfer has failed */
	if (ret)
		goto free_dump_info;

	return;

free_dump_info:
	/* Free the allocated memory */
	free_ssr_dump_info(ssr_crash);
reset_device:
	/*
	 * After subsystem crashes in device crashdump collection begins but
	 * something went wrong while collecting crashdump, now instead of
	 * handling this error we just reset the device as the best effort has
	 * been made
	 */
	mhi_soc_reset(qdev->mhi_cntrl);
}

static struct ssr_dump_info *alloc_dump_info(struct qaic_device *qdev,
					     struct ssr_debug_transfer_info *debug_info)
{
	struct ssr_dump_info *dump_info;
	int ret;

	le64_to_cpus(&debug_info->tbl_len);
	le64_to_cpus(&debug_info->tbl_addr);

	if (debug_info->tbl_len == 0 ||
	    debug_info->tbl_len % sizeof(struct debug_info_table) != 0) {
		ret = -EINVAL;
		goto out;
	}

	/* Allocate SSR crashdump book keeping structure */
	dump_info = kzalloc(sizeof(*dump_info), GFP_KERNEL);
	if (!dump_info) {
		ret = -ENOMEM;
		goto out;
	}

	/* Buffer used to send MEMORY READ request to device via MHI */
	dump_info->read_buf_req = kzalloc(sizeof(*dump_info->read_buf_req), GFP_KERNEL);
	if (!dump_info->read_buf_req) {
		ret = -ENOMEM;
		goto free_dump_info;
	}

	/* Crashdump meta table buffer */
	dump_info->tbl_addr = vzalloc(debug_info->tbl_len);
	if (!dump_info->tbl_addr) {
		ret = -ENOMEM;
		goto free_read_buf_req;
	}

	dump_info->tbl_addr_dev = debug_info->tbl_addr;
	dump_info->tbl_len = debug_info->tbl_len;

	return dump_info;

free_read_buf_req:
	kfree(dump_info->read_buf_req);
free_dump_info:
	kfree(dump_info);
out:
	return ERR_PTR(ret);
}

static int dbg_xfer_info_rsp(struct qaic_device *qdev, struct dma_bridge_chan *dbc,
			     struct ssr_debug_transfer_info *debug_info)
{
	struct ssr_debug_transfer_info_rsp *debug_rsp;
	struct ssr_crashdump *ssr_crash = NULL;
	int ret = 0, ret2;

	debug_rsp = kmalloc(sizeof(*debug_rsp), GFP_KERNEL);
	if (!debug_rsp)
		return -ENOMEM;

	if (!qdev->ssr_mhi_buf) {
		ret = -ENOMEM;
		goto send_rsp;
	}

	if (dbc->state != DBC_STATE_BEFORE_POWER_UP) {
		ret = -EINVAL;
		goto send_rsp;
	}

	ssr_crash = qdev->ssr_mhi_buf;
	ssr_crash->dump_info = alloc_dump_info(qdev, debug_info);
	if (IS_ERR(ssr_crash->dump_info)) {
		ret = PTR_ERR(ssr_crash->dump_info);
		ssr_crash->dump_info = NULL;
	}

send_rsp:
	debug_rsp->hdr.cmd = cpu_to_le32(DEBUG_TRANSFER_INFO_RSP);
	debug_rsp->hdr.len = cpu_to_le32(sizeof(*debug_rsp));
	debug_rsp->hdr.dbc_id = cpu_to_le32(dbc->id);
	/*
	 * 0 = Return an ACK confirming the host is ready to download crashdump
	 * 1 = Return an NACK confirming the host is not ready to download crashdump
	 */
	debug_rsp->ret = cpu_to_le32(ret ? 1 : 0);

	ret2 = mhi_queue_buf(qdev->ssr_ch, DMA_TO_DEVICE, debug_rsp, sizeof(*debug_rsp), MHI_EOT);
	if (ret2) {
		free_ssr_dump_info(ssr_crash);
		kfree(debug_rsp);
		return ret2;
	}

	return ret;
}

static void dbg_xfer_done_rsp(struct qaic_device *qdev, struct dma_bridge_chan *dbc,
			      struct ssr_debug_transfer_done_rsp *xfer_rsp)
{
	struct ssr_crashdump *ssr_crash = qdev->ssr_mhi_buf;
	u32 status = le32_to_cpu(xfer_rsp->ret);
	struct device *dev = &qdev->pdev->dev;
	struct ssr_dump_info *dump_info;

	dump_info = ssr_crash->dump_info;
	if (!dump_info)
		return;

	if (status) {
		free_ssr_dump_info(ssr_crash);
		return;
	}

	dev_coredumpv(dev, dump_info->dump_addr, dump_info->dump_sz, GFP_KERNEL);
	/* dev_coredumpv will free dump_info->dump_addr */
	dump_info->dump_addr = NULL;
	free_ssr_dump_info(ssr_crash);
}

static void ssr_worker(struct work_struct *work)
{
	struct ssr_resp *resp = container_of(work, struct ssr_resp, work);
	struct ssr_hdr *hdr = (struct ssr_hdr *)resp->data;
	struct ssr_dump_info *dump_info = NULL;
	struct qaic_device *qdev = resp->qdev;
	struct ssr_crashdump *ssr_crash;
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
		ret = dbg_xfer_info_rsp(qdev, dbc, (struct ssr_debug_transfer_info *)resp->data);
		if (ret)
			break;

		ssr_crash = qdev->ssr_mhi_buf;
		dump_info = ssr_crash->dump_info;
		dump_info->dbc = dbc;
		dump_info->resp = resp;

		/* Start by downloading debug table */
		ret = mem_read_req(qdev, dump_info->tbl_addr_dev,
				   min(dump_info->tbl_len, SSR_MEM_READ_CHUNK_SIZE));
		if (ret) {
			free_ssr_dump_info(ssr_crash);
			break;
		}

		/*
		 * Till now everything went fine, which means that we will be
		 * collecting crashdump chunk by chunk. Do not queue a response
		 * buffer for SSR cmds till the crashdump is complete.
		 */
		return;
	case SSR_EVENT:
		event = (struct ssr_event *)hdr;
		le32_to_cpus(&event->event);
		ssr_event_ack = event->event;
		ssr_crash = qdev->ssr_mhi_buf;

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
			/*
			 * If dump info is a non NULL value it means that we
			 * have received this SSR event while downloading a
			 * crashdump for this DBC is still in progress. NACK
			 * the SSR event
			 */
			if (ssr_crash && ssr_crash->dump_info) {
				free_ssr_dump_info(ssr_crash);
				ssr_event_ack = SSR_EVENT_NACK;
				break;
			}

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

		if (event->event == AFTER_POWER_UP && ssr_event_ack != SSR_EVENT_NACK) {
			qaic_dbc_exit_ssr(qdev);
			set_dbc_state(qdev, hdr->dbc_id, DBC_STATE_IDLE);
		}

		break;
	case DEBUG_TRANSFER_DONE_RSP:
		dbg_xfer_done_rsp(qdev, dbc, (struct ssr_debug_transfer_done_rsp *)hdr);
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
	struct qaic_device *qdev = dev_get_drvdata(&mhi_dev->dev);
	struct ssr_crashdump *ssr_crash = qdev->ssr_mhi_buf;
	struct _ssr_hdr *hdr = mhi_result->buf_addr;
	struct ssr_dump_info *dump_info;

	if (mhi_result->transaction_status) {
		kfree(mhi_result->buf_addr);
		return;
	}

	/*
	 * MEMORY READ is used to download crashdump. And crashdump is
	 * downloaded chunk by chunk in a series of MEMORY READ SSR commands.
	 * Hence to avoid too many kmalloc() and kfree() of the same MEMORY READ
	 * request buffer, we allocate only one such buffer and free it only
	 * once.
	 */
	if (le32_to_cpu(hdr->cmd) == MEMORY_READ) {
		dump_info = ssr_crash->dump_info;
		if (dump_info) {
			dump_info->read_buf_req_queued = false;
			return;
		}
	}

	kfree(mhi_result->buf_addr);
}

static void qaic_ssr_mhi_dl_xfer_cb(struct mhi_device *mhi_dev, struct mhi_result *mhi_result)
{
	struct ssr_resp *resp = container_of(mhi_result->buf_addr, struct ssr_resp, data);
	struct qaic_device *qdev = dev_get_drvdata(&mhi_dev->dev);
	struct ssr_crashdump *ssr_crash = qdev->ssr_mhi_buf;
	bool memory_read_rsp = false;

	if (ssr_crash && ssr_crash->data == mhi_result->buf_addr)
		memory_read_rsp = true;

	if (mhi_result->transaction_status) {
		/* Do not free SSR crashdump buffer as it allocated via managed APIs */
		if (!memory_read_rsp)
			kfree(resp);
		return;
	}

	if (memory_read_rsp)
		queue_work(qdev->ssr_wq, &ssr_crash->work);
	else
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

int qaic_ssr_init(struct qaic_device *qdev, struct drm_device *drm)
{
	struct ssr_crashdump *ssr_crash;

	qdev->ssr_dbc = QAIC_SSR_DBC_SENTINEL;

	/*
	 * Device requests only one SSR at a time. So allocating only one
	 * buffer to download crashdump is good enough.
	 */
	ssr_crash = drmm_kzalloc(drm, SSR_MHI_BUF_SIZE, GFP_KERNEL);
	if (!ssr_crash)
		return -ENOMEM;

	ssr_crash->qdev = qdev;
	INIT_WORK(&ssr_crash->work, ssr_dump_worker);
	qdev->ssr_mhi_buf = ssr_crash;

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
