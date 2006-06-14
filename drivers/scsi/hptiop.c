/*
 * HighPoint RR3xxx controller driver for Linux
 * Copyright (C) 2006 HighPoint Technologies, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Please report bugs/comments/suggestions to linux@highpoint-tech.com
 *
 * For more information, visit http://www.highpoint-tech.com
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/spinlock.h>
#include <linux/hdreg.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/div64.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_host.h>

#include "hptiop.h"

MODULE_AUTHOR("HighPoint Technologies, Inc.");
MODULE_DESCRIPTION("HighPoint RocketRAID 3xxx SATA Controller Driver");

static char driver_name[] = "hptiop";
static const char driver_name_long[] = "RocketRAID 3xxx SATA Controller driver";
static const char driver_ver[] = "v1.0 (060426)";

static DEFINE_SPINLOCK(hptiop_hba_list_lock);
static LIST_HEAD(hptiop_hba_list);
static int hptiop_cdev_major = -1;

static void hptiop_host_request_callback(struct hptiop_hba *hba, u32 tag);
static void hptiop_iop_request_callback(struct hptiop_hba *hba, u32 tag);
static void hptiop_message_callback(struct hptiop_hba *hba, u32 msg);

static inline void hptiop_pci_posting_flush(struct hpt_iopmu __iomem *iop)
{
	readl(&iop->outbound_intstatus);
}

static int iop_wait_ready(struct hpt_iopmu __iomem *iop, u32 millisec)
{
	u32 req = 0;
	int i;

	for (i = 0; i < millisec; i++) {
		req = readl(&iop->inbound_queue);
		if (req != IOPMU_QUEUE_EMPTY)
			break;
		msleep(1);
	}

	if (req != IOPMU_QUEUE_EMPTY) {
		writel(req, &iop->outbound_queue);
		hptiop_pci_posting_flush(iop);
		return 0;
	}

	return -1;
}

static void hptiop_request_callback(struct hptiop_hba *hba, u32 tag)
{
	if ((tag & IOPMU_QUEUE_MASK_HOST_BITS) == IOPMU_QUEUE_ADDR_HOST_BIT)
		return hptiop_host_request_callback(hba,
				tag & ~IOPMU_QUEUE_ADDR_HOST_BIT);
	else
		return hptiop_iop_request_callback(hba, tag);
}

static inline void hptiop_drain_outbound_queue(struct hptiop_hba *hba)
{
	u32 req;

	while ((req = readl(&hba->iop->outbound_queue)) != IOPMU_QUEUE_EMPTY) {

		if (req & IOPMU_QUEUE_MASK_HOST_BITS)
			hptiop_request_callback(hba, req);
		else {
			struct hpt_iop_request_header __iomem * p;

			p = (struct hpt_iop_request_header __iomem *)
				((char __iomem *)hba->iop + req);

			if (readl(&p->flags) & IOP_REQUEST_FLAG_SYNC_REQUEST) {
				if (readl(&p->context))
					hptiop_request_callback(hba, req);
				else
					writel(1, &p->context);
			}
			else
				hptiop_request_callback(hba, req);
		}
	}
}

static int __iop_intr(struct hptiop_hba *hba)
{
	struct hpt_iopmu __iomem *iop = hba->iop;
	u32 status;
	int ret = 0;

	status = readl(&iop->outbound_intstatus);

	if (status & IOPMU_OUTBOUND_INT_MSG0) {
		u32 msg = readl(&iop->outbound_msgaddr0);
		dprintk("received outbound msg %x\n", msg);
		writel(IOPMU_OUTBOUND_INT_MSG0, &iop->outbound_intstatus);
		hptiop_message_callback(hba, msg);
		ret = 1;
	}

	if (status & IOPMU_OUTBOUND_INT_POSTQUEUE) {
		hptiop_drain_outbound_queue(hba);
		ret = 1;
	}

	return ret;
}

static int iop_send_sync_request(struct hptiop_hba *hba,
					void __iomem *_req, u32 millisec)
{
	struct hpt_iop_request_header __iomem *req = _req;
	u32 i;

	writel(readl(&req->flags) | IOP_REQUEST_FLAG_SYNC_REQUEST,
			&req->flags);

	writel(0, &req->context);

	writel((unsigned long)req - (unsigned long)hba->iop,
			&hba->iop->inbound_queue);

	hptiop_pci_posting_flush(hba->iop);

	for (i = 0; i < millisec; i++) {
		__iop_intr(hba);
		if (readl(&req->context))
			return 0;
		msleep(1);
	}

	return -1;
}

static int iop_send_sync_msg(struct hptiop_hba *hba, u32 msg, u32 millisec)
{
	u32 i;

	hba->msg_done = 0;

	writel(msg, &hba->iop->inbound_msgaddr0);

	hptiop_pci_posting_flush(hba->iop);

	for (i = 0; i < millisec; i++) {
		spin_lock_irq(hba->host->host_lock);
		__iop_intr(hba);
		spin_unlock_irq(hba->host->host_lock);
		if (hba->msg_done)
			break;
		msleep(1);
	}

	return hba->msg_done? 0 : -1;
}

static int iop_get_config(struct hptiop_hba *hba,
				struct hpt_iop_request_get_config *config)
{
	u32 req32;
	struct hpt_iop_request_get_config __iomem *req;

	req32 = readl(&hba->iop->inbound_queue);
	if (req32 == IOPMU_QUEUE_EMPTY)
		return -1;

	req = (struct hpt_iop_request_get_config __iomem *)
			((unsigned long)hba->iop + req32);

	writel(0, &req->header.flags);
	writel(IOP_REQUEST_TYPE_GET_CONFIG, &req->header.type);
	writel(sizeof(struct hpt_iop_request_get_config), &req->header.size);
	writel(IOP_RESULT_PENDING, &req->header.result);

	if (iop_send_sync_request(hba, req, 20000)) {
		dprintk("Get config send cmd failed\n");
		return -1;
	}

	memcpy_fromio(config, req, sizeof(*config));
	writel(req32, &hba->iop->outbound_queue);
	return 0;
}

static int iop_set_config(struct hptiop_hba *hba,
				struct hpt_iop_request_set_config *config)
{
	u32 req32;
	struct hpt_iop_request_set_config __iomem *req;

	req32 = readl(&hba->iop->inbound_queue);
	if (req32 == IOPMU_QUEUE_EMPTY)
		return -1;

	req = (struct hpt_iop_request_set_config __iomem *)
			((unsigned long)hba->iop + req32);

	memcpy_toio((u8 __iomem *)req + sizeof(struct hpt_iop_request_header),
		(u8 *)config + sizeof(struct hpt_iop_request_header),
		sizeof(struct hpt_iop_request_set_config) -
			sizeof(struct hpt_iop_request_header));

	writel(0, &req->header.flags);
	writel(IOP_REQUEST_TYPE_SET_CONFIG, &req->header.type);
	writel(sizeof(struct hpt_iop_request_set_config), &req->header.size);
	writel(IOP_RESULT_PENDING, &req->header.result);

	if (iop_send_sync_request(hba, req, 20000)) {
		dprintk("Set config send cmd failed\n");
		return -1;
	}

	writel(req32, &hba->iop->outbound_queue);
	return 0;
}

static int hptiop_initialize_iop(struct hptiop_hba *hba)
{
	struct hpt_iopmu __iomem *iop = hba->iop;

	/* enable interrupts */
	writel(~(IOPMU_OUTBOUND_INT_POSTQUEUE | IOPMU_OUTBOUND_INT_MSG0),
			&iop->outbound_intmask);

	hba->initialized = 1;

	/* start background tasks */
	if (iop_send_sync_msg(hba,
			IOPMU_INBOUND_MSG0_START_BACKGROUND_TASK, 5000)) {
		printk(KERN_ERR "scsi%d: fail to start background task\n",
			hba->host->host_no);
		return -1;
	}
	return 0;
}

static int hptiop_map_pci_bar(struct hptiop_hba *hba)
{
	u32 mem_base_phy, length;
	void __iomem *mem_base_virt;
	struct pci_dev *pcidev = hba->pcidev;

	if (!(pci_resource_flags(pcidev, 0) & IORESOURCE_MEM)) {
		printk(KERN_ERR "scsi%d: pci resource invalid\n",
				hba->host->host_no);
		return -1;
	}

	mem_base_phy = pci_resource_start(pcidev, 0);
	length = pci_resource_len(pcidev, 0);
	mem_base_virt = ioremap(mem_base_phy, length);

	if (!mem_base_virt) {
		printk(KERN_ERR "scsi%d: Fail to ioremap memory space\n",
				hba->host->host_no);
		return -1;
	}

	hba->iop = mem_base_virt;
	dprintk("hptiop_map_pci_bar: iop=%p\n", hba->iop);
	return 0;
}

static void hptiop_message_callback(struct hptiop_hba *hba, u32 msg)
{
	dprintk("iop message 0x%x\n", msg);

	if (!hba->initialized)
		return;

	if (msg == IOPMU_INBOUND_MSG0_RESET) {
		atomic_set(&hba->resetting, 0);
		wake_up(&hba->reset_wq);
	}
	else if (msg <= IOPMU_INBOUND_MSG0_MAX)
		hba->msg_done = 1;
}

static inline struct hptiop_request *get_req(struct hptiop_hba *hba)
{
	struct hptiop_request *ret;

	dprintk("get_req : req=%p\n", hba->req_list);

	ret = hba->req_list;
	if (ret)
		hba->req_list = ret->next;

	return ret;
}

static inline void free_req(struct hptiop_hba *hba, struct hptiop_request *req)
{
	dprintk("free_req(%d, %p)\n", req->index, req);
	req->next = hba->req_list;
	hba->req_list = req;
}

static void hptiop_host_request_callback(struct hptiop_hba *hba, u32 tag)
{
	struct hpt_iop_request_scsi_command *req;
	struct scsi_cmnd *scp;

	req = (struct hpt_iop_request_scsi_command *)hba->reqs[tag].req_virt;
	dprintk("hptiop_host_request_callback: req=%p, type=%d, "
			"result=%d, context=0x%x tag=%d\n",
			req, req->header.type, req->header.result,
			req->header.context, tag);

	BUG_ON(!req->header.result);
	BUG_ON(req->header.type != cpu_to_le32(IOP_REQUEST_TYPE_SCSI_COMMAND));

	scp = hba->reqs[tag].scp;

	if (HPT_SCP(scp)->mapped) {
		if (scp->use_sg)
			pci_unmap_sg(hba->pcidev,
				(struct scatterlist *)scp->request_buffer,
				scp->use_sg,
				scp->sc_data_direction
			);
		else
			pci_unmap_single(hba->pcidev,
				HPT_SCP(scp)->dma_handle,
				scp->request_bufflen,
				scp->sc_data_direction
			);
	}

	switch (le32_to_cpu(req->header.result)) {
	case IOP_RESULT_SUCCESS:
		scp->result = (DID_OK<<16);
		break;
	case IOP_RESULT_BAD_TARGET:
		scp->result = (DID_BAD_TARGET<<16);
		break;
	case IOP_RESULT_BUSY:
		scp->result = (DID_BUS_BUSY<<16);
		break;
	case IOP_RESULT_RESET:
		scp->result = (DID_RESET<<16);
		break;
	case IOP_RESULT_FAIL:
		scp->result = (DID_ERROR<<16);
		break;
	case IOP_RESULT_INVALID_REQUEST:
		scp->result = (DID_ABORT<<16);
		break;
	case IOP_RESULT_MODE_SENSE_CHECK_CONDITION:
		scp->result = SAM_STAT_CHECK_CONDITION;
		memset(&scp->sense_buffer,
				0, sizeof(scp->sense_buffer));
		memcpy(&scp->sense_buffer,
			&req->sg_list, le32_to_cpu(req->dataxfer_length));
		break;

	default:
		scp->result = ((DRIVER_INVALID|SUGGEST_ABORT)<<24) |
					(DID_ABORT<<16);
		break;
	}

	dprintk("scsi_done(%p)\n", scp);
	scp->scsi_done(scp);
	free_req(hba, &hba->reqs[tag]);
}

void hptiop_iop_request_callback(struct hptiop_hba *hba, u32 tag)
{
	struct hpt_iop_request_header __iomem *req;
	struct hpt_iop_request_ioctl_command __iomem *p;
	struct hpt_ioctl_k *arg;

	req = (struct hpt_iop_request_header __iomem *)
			((unsigned long)hba->iop + tag);
	dprintk("hptiop_iop_request_callback: req=%p, type=%d, "
			"result=%d, context=0x%x tag=%d\n",
			req, readl(&req->type), readl(&req->result),
			readl(&req->context), tag);

	BUG_ON(!readl(&req->result));
	BUG_ON(readl(&req->type) != IOP_REQUEST_TYPE_IOCTL_COMMAND);

	p = (struct hpt_iop_request_ioctl_command __iomem *)req;
	arg = (struct hpt_ioctl_k *)(unsigned long)
		(readl(&req->context) |
			((u64)readl(&req->context_hi32)<<32));

	if (readl(&req->result) == IOP_RESULT_SUCCESS) {
		arg->result = HPT_IOCTL_RESULT_OK;

		if (arg->outbuf_size)
			memcpy_fromio(arg->outbuf,
				&p->buf[(readl(&p->inbuf_size) + 3)& ~3],
				arg->outbuf_size);

		if (arg->bytes_returned)
			*arg->bytes_returned = arg->outbuf_size;
	}
	else
		arg->result = HPT_IOCTL_RESULT_FAILED;

	arg->done(arg);
	writel(tag, &hba->iop->outbound_queue);
}

static irqreturn_t hptiop_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	struct hptiop_hba  *hba = dev_id;
	int  handled;
	unsigned long flags;

	spin_lock_irqsave(hba->host->host_lock, flags);
	handled = __iop_intr(hba);
	spin_unlock_irqrestore(hba->host->host_lock, flags);

	return handled;
}

static int hptiop_buildsgl(struct scsi_cmnd *scp, struct hpt_iopsg *psg)
{
	struct Scsi_Host *host = scp->device->host;
	struct hptiop_hba *hba = (struct hptiop_hba *)host->hostdata;
	struct scatterlist *sglist = (struct scatterlist *)scp->request_buffer;

	/*
	 * though we'll not get non-use_sg fields anymore,
	 * keep use_sg checking anyway
	 */
	if (scp->use_sg) {
		int idx;

		HPT_SCP(scp)->sgcnt = pci_map_sg(hba->pcidev,
				sglist, scp->use_sg,
				scp->sc_data_direction);
		HPT_SCP(scp)->mapped = 1;
		BUG_ON(HPT_SCP(scp)->sgcnt > hba->max_sg_descriptors);

		for (idx = 0; idx < HPT_SCP(scp)->sgcnt; idx++) {
			psg[idx].pci_address =
				cpu_to_le64(sg_dma_address(&sglist[idx]));
			psg[idx].size = cpu_to_le32(sg_dma_len(&sglist[idx]));
			psg[idx].eot = (idx == HPT_SCP(scp)->sgcnt - 1) ?
				cpu_to_le32(1) : 0;
		}

		return HPT_SCP(scp)->sgcnt;
	} else {
		HPT_SCP(scp)->dma_handle = pci_map_single(
				hba->pcidev,
				scp->request_buffer,
				scp->request_bufflen,
				scp->sc_data_direction
			);
		HPT_SCP(scp)->mapped = 1;
		psg->pci_address = cpu_to_le64(HPT_SCP(scp)->dma_handle);
		psg->size = cpu_to_le32(scp->request_bufflen);
		psg->eot = cpu_to_le32(1);
		return 1;
	}
}

static int hptiop_queuecommand(struct scsi_cmnd *scp,
				void (*done)(struct scsi_cmnd *))
{
	struct Scsi_Host *host = scp->device->host;
	struct hptiop_hba *hba = (struct hptiop_hba *)host->hostdata;
	struct hpt_iop_request_scsi_command *req;
	int sg_count = 0;
	struct hptiop_request *_req;

	BUG_ON(!done);
	scp->scsi_done = done;

	_req = get_req(hba);
	if (_req == NULL) {
		dprintk("hptiop_queuecmd : no free req\n");
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	_req->scp = scp;

	dprintk("hptiop_queuecmd(scp=%p) %d/%d/%d/%d cdb=(%x-%x-%x) "
			"req_index=%d, req=%p\n",
			scp,
			host->host_no, scp->device->channel,
			scp->device->id, scp->device->lun,
			*((u32 *)&scp->cmnd),
			*((u32 *)&scp->cmnd + 1),
			*((u32 *)&scp->cmnd + 2),
			_req->index, _req->req_virt);

	scp->result = 0;

	if (scp->device->channel || scp->device->lun ||
			scp->device->id > hba->max_devices) {
		scp->result = DID_BAD_TARGET << 16;
		free_req(hba, _req);
		goto cmd_done;
	}

	req = (struct hpt_iop_request_scsi_command *)_req->req_virt;

	/* build S/G table */
	if (scp->request_bufflen)
		sg_count = hptiop_buildsgl(scp, req->sg_list);
	else
		HPT_SCP(scp)->mapped = 0;

	req->header.flags = cpu_to_le32(IOP_REQUEST_FLAG_OUTPUT_CONTEXT);
	req->header.type = cpu_to_le32(IOP_REQUEST_TYPE_SCSI_COMMAND);
	req->header.result = cpu_to_le32(IOP_RESULT_PENDING);
	req->header.context = cpu_to_le32(IOPMU_QUEUE_ADDR_HOST_BIT |
							(u32)_req->index);
	req->header.context_hi32 = 0;
	req->dataxfer_length = cpu_to_le32(scp->bufflen);
	req->channel = scp->device->channel;
	req->target = scp->device->id;
	req->lun = scp->device->lun;
	req->header.size = cpu_to_le32(
				sizeof(struct hpt_iop_request_scsi_command)
				 - sizeof(struct hpt_iopsg)
				 + sg_count * sizeof(struct hpt_iopsg));

	memcpy(req->cdb, scp->cmnd, sizeof(req->cdb));

	writel(IOPMU_QUEUE_ADDR_HOST_BIT | _req->req_shifted_phy,
			&hba->iop->inbound_queue);

	return 0;

cmd_done:
	dprintk("scsi_done(scp=%p)\n", scp);
	scp->scsi_done(scp);
	return 0;
}

static const char *hptiop_info(struct Scsi_Host *host)
{
	return driver_name_long;
}

static int hptiop_reset_hba(struct hptiop_hba *hba)
{
	if (atomic_xchg(&hba->resetting, 1) == 0) {
		atomic_inc(&hba->reset_count);
		writel(IOPMU_INBOUND_MSG0_RESET,
				&hba->iop->outbound_msgaddr0);
		hptiop_pci_posting_flush(hba->iop);
	}

	wait_event_timeout(hba->reset_wq,
			atomic_read(&hba->resetting) == 0, 60 * HZ);

	if (atomic_read(&hba->resetting)) {
		/* IOP is in unkown state, abort reset */
		printk(KERN_ERR "scsi%d: reset failed\n", hba->host->host_no);
		return -1;
	}

	if (iop_send_sync_msg(hba,
		IOPMU_INBOUND_MSG0_START_BACKGROUND_TASK, 5000)) {
		dprintk("scsi%d: fail to start background task\n",
				hba->host->host_no);
	}

	return 0;
}

static int hptiop_reset(struct scsi_cmnd *scp)
{
	struct Scsi_Host * host = scp->device->host;
	struct hptiop_hba * hba = (struct hptiop_hba *)host->hostdata;

	printk(KERN_WARNING "hptiop_reset(%d/%d/%d) scp=%p\n",
			scp->device->host->host_no, scp->device->channel,
			scp->device->id, scp);

	return hptiop_reset_hba(hba)? FAILED : SUCCESS;
}

static int hptiop_adjust_disk_queue_depth(struct scsi_device *sdev,
						int queue_depth)
{
	if(queue_depth > 256)
		queue_depth = 256;
	scsi_adjust_queue_depth(sdev, MSG_ORDERED_TAG, queue_depth);
	return queue_depth;
}

struct hptiop_getinfo {
	char __user *buffer;
	loff_t buflength;
	loff_t bufoffset;
	loff_t buffillen;
	loff_t filpos;
};

static void hptiop_copy_mem_info(struct hptiop_getinfo *pinfo,
					char *data, int datalen)
{
	if (pinfo->filpos < pinfo->bufoffset) {
		if (pinfo->filpos + datalen <= pinfo->bufoffset) {
			pinfo->filpos += datalen;
			return;
		} else {
			data += (pinfo->bufoffset - pinfo->filpos);
			datalen  -= (pinfo->bufoffset - pinfo->filpos);
			pinfo->filpos = pinfo->bufoffset;
		}
	}

	pinfo->filpos += datalen;
	if (pinfo->buffillen == pinfo->buflength)
		return;

	if (pinfo->buflength - pinfo->buffillen < datalen)
		datalen = pinfo->buflength - pinfo->buffillen;

	if (copy_to_user(pinfo->buffer + pinfo->buffillen, data, datalen))
		return;

	pinfo->buffillen += datalen;
}

static int hptiop_copy_info(struct hptiop_getinfo *pinfo, char *fmt, ...)
{
	va_list args;
	char buf[128];
	int len;

	va_start(args, fmt);
	len = vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	hptiop_copy_mem_info(pinfo, buf, len);
	return len;
}

static void hptiop_ioctl_done(struct hpt_ioctl_k *arg)
{
	arg->done = NULL;
	wake_up(&arg->hba->ioctl_wq);
}

static void hptiop_do_ioctl(struct hpt_ioctl_k *arg)
{
	struct hptiop_hba *hba = arg->hba;
	u32 val;
	struct hpt_iop_request_ioctl_command __iomem *req;
	int ioctl_retry = 0;

	dprintk("scsi%d: hptiop_do_ioctl\n", hba->host->host_no);

	/*
	 * check (in + out) buff size from application.
	 * outbuf must be dword aligned.
	 */
	if (((arg->inbuf_size + 3) & ~3) + arg->outbuf_size >
			hba->max_request_size
				- sizeof(struct hpt_iop_request_header)
				- 4 * sizeof(u32)) {
		dprintk("scsi%d: ioctl buf size (%d/%d) is too large\n",
				hba->host->host_no,
				arg->inbuf_size, arg->outbuf_size);
		arg->result = HPT_IOCTL_RESULT_FAILED;
		return;
	}

retry:
	spin_lock_irq(hba->host->host_lock);

	val = readl(&hba->iop->inbound_queue);
	if (val == IOPMU_QUEUE_EMPTY) {
		spin_unlock_irq(hba->host->host_lock);
		dprintk("scsi%d: no free req for ioctl\n", hba->host->host_no);
		arg->result = -1;
		return;
	}

	req = (struct hpt_iop_request_ioctl_command __iomem *)
			((unsigned long)hba->iop + val);

	writel(HPT_CTL_CODE_LINUX_TO_IOP(arg->ioctl_code),
			&req->ioctl_code);
	writel(arg->inbuf_size, &req->inbuf_size);
	writel(arg->outbuf_size, &req->outbuf_size);

	/*
	 * use the buffer on the IOP local memory first, then copy it
	 * back to host.
	 * the caller's request buffer shoudl be little-endian.
	 */
	if (arg->inbuf_size)
		memcpy_toio(req->buf, arg->inbuf, arg->inbuf_size);

	/* correct the controller ID for IOP */
	if ((arg->ioctl_code == HPT_IOCTL_GET_CHANNEL_INFO ||
		arg->ioctl_code == HPT_IOCTL_GET_CONTROLLER_INFO_V2 ||
		arg->ioctl_code == HPT_IOCTL_GET_CONTROLLER_INFO)
		&& arg->inbuf_size >= sizeof(u32))
		writel(0, req->buf);

	writel(IOP_REQUEST_TYPE_IOCTL_COMMAND, &req->header.type);
	writel(0, &req->header.flags);
	writel(offsetof(struct hpt_iop_request_ioctl_command, buf)
			+ arg->inbuf_size, &req->header.size);
	writel((u32)(unsigned long)arg, &req->header.context);
	writel(BITS_PER_LONG > 32 ? (u32)((unsigned long)arg>>32) : 0,
			&req->header.context_hi32);
	writel(IOP_RESULT_PENDING, &req->header.result);

	arg->result = HPT_IOCTL_RESULT_FAILED;
	arg->done = hptiop_ioctl_done;

	writel(val, &hba->iop->inbound_queue);
	hptiop_pci_posting_flush(hba->iop);

	spin_unlock_irq(hba->host->host_lock);

	wait_event_timeout(hba->ioctl_wq, arg->done == NULL, 60 * HZ);

	if (arg->done != NULL) {
		hptiop_reset_hba(hba);
		if (ioctl_retry++ < 3)
			goto retry;
	}

	dprintk("hpt_iop_ioctl %x result %d\n",
			arg->ioctl_code, arg->result);
}

static int __hpt_do_ioctl(struct hptiop_hba *hba, u32 code, void *inbuf,
			u32 insize, void *outbuf, u32 outsize)
{
	struct hpt_ioctl_k arg;
	arg.hba = hba;
	arg.ioctl_code = code;
	arg.inbuf = inbuf;
	arg.outbuf = outbuf;
	arg.inbuf_size = insize;
	arg.outbuf_size = outsize;
	arg.bytes_returned = NULL;
	hptiop_do_ioctl(&arg);
	return arg.result;
}

static inline int hpt_id_valid(__le32 id)
{
	return id != 0 && id != cpu_to_le32(0xffffffff);
}

static int hptiop_get_controller_info(struct hptiop_hba *hba,
					struct hpt_controller_info *pinfo)
{
	int id = 0;

	return __hpt_do_ioctl(hba, HPT_IOCTL_GET_CONTROLLER_INFO,
		&id, sizeof(int), pinfo, sizeof(*pinfo));
}


static int hptiop_get_channel_info(struct hptiop_hba *hba, int bus,
					struct hpt_channel_info *pinfo)
{
	u32 ids[2];

	ids[0] = 0;
	ids[1] = bus;
	return __hpt_do_ioctl(hba, HPT_IOCTL_GET_CHANNEL_INFO,
				ids, sizeof(ids), pinfo, sizeof(*pinfo));

}

static int hptiop_get_logical_devices(struct hptiop_hba *hba,
					__le32 *pids, int maxcount)
{
	int i;
	u32 count = maxcount - 1;

	if (__hpt_do_ioctl(hba, HPT_IOCTL_GET_LOGICAL_DEVICES,
			&count, sizeof(u32),
			pids, sizeof(u32) * maxcount))
		return -1;

	maxcount = le32_to_cpu(pids[0]);
	for (i = 0; i < maxcount; i++)
		pids[i] = pids[i+1];

	return maxcount;
}

static int hptiop_get_device_info_v3(struct hptiop_hba *hba, __le32 id,
				struct hpt_logical_device_info_v3 *pinfo)
{
	return __hpt_do_ioctl(hba, HPT_IOCTL_GET_DEVICE_INFO_V3,
				&id, sizeof(u32),
				pinfo, sizeof(*pinfo));
}

static const char *get_array_status(struct hpt_logical_device_info_v3 *devinfo)
{
	static char s[64];
	u32 flags = le32_to_cpu(devinfo->u.array.flags);
	u32 trans_prog = le32_to_cpu(devinfo->u.array.transforming_progress);
	u32 reb_prog = le32_to_cpu(devinfo->u.array.rebuilding_progress);

	if (flags & ARRAY_FLAG_DISABLED)
		return "Disabled";
	else if (flags & ARRAY_FLAG_TRANSFORMING)
		sprintf(s, "Expanding/Migrating %d.%d%%%s%s",
			trans_prog / 100,
			trans_prog % 100,
			(flags & (ARRAY_FLAG_NEEDBUILDING|ARRAY_FLAG_BROKEN))?
					", Critical" : "",
			((flags & ARRAY_FLAG_NEEDINITIALIZING) &&
			 !(flags & ARRAY_FLAG_REBUILDING) &&
			 !(flags & ARRAY_FLAG_INITIALIZING))?
					", Unintialized" : "");
	else if ((flags & ARRAY_FLAG_BROKEN) &&
				devinfo->u.array.array_type != AT_RAID6)
		return "Critical";
	else if (flags & ARRAY_FLAG_REBUILDING)
		sprintf(s,
			(flags & ARRAY_FLAG_NEEDINITIALIZING)?
				"%sBackground initializing %d.%d%%" :
					"%sRebuilding %d.%d%%",
			(flags & ARRAY_FLAG_BROKEN)? "Critical, " : "",
			reb_prog / 100,
			reb_prog % 100);
	else if (flags & ARRAY_FLAG_VERIFYING)
		sprintf(s, "%sVerifying %d.%d%%",
			(flags & ARRAY_FLAG_BROKEN)? "Critical, " : "",
			reb_prog / 100,
			reb_prog % 100);
	else if (flags & ARRAY_FLAG_INITIALIZING)
		sprintf(s, "%sForground initializing %d.%d%%",
			(flags & ARRAY_FLAG_BROKEN)? "Critical, " : "",
			reb_prog / 100,
			reb_prog % 100);
	else if (flags & ARRAY_FLAG_NEEDTRANSFORM)
		sprintf(s,"%s%s%s", "Need Expanding/Migrating",
			(flags & ARRAY_FLAG_BROKEN)? "Critical, " : "",
			((flags & ARRAY_FLAG_NEEDINITIALIZING) &&
			 !(flags & ARRAY_FLAG_REBUILDING) &&
			 !(flags & ARRAY_FLAG_INITIALIZING))?
				", Unintialized" : "");
	else if (flags & ARRAY_FLAG_NEEDINITIALIZING &&
		!(flags & ARRAY_FLAG_REBUILDING) &&
		!(flags & ARRAY_FLAG_INITIALIZING))
		sprintf(s,"%sUninitialized",
			(flags & ARRAY_FLAG_BROKEN)? "Critical, " : "");
	else if ((flags & ARRAY_FLAG_NEEDBUILDING) ||
			(flags & ARRAY_FLAG_BROKEN))
		return "Critical";
	else
		return "Normal";
	return s;
}

static void hptiop_dump_devinfo(struct hptiop_hba *hba,
			struct hptiop_getinfo *pinfo, __le32 id, int indent)
{
	struct hpt_logical_device_info_v3 devinfo;
	int i;
	u64 capacity;

	for (i = 0; i < indent; i++)
		hptiop_copy_info(pinfo, "\t");

	if (hptiop_get_device_info_v3(hba, id, &devinfo)) {
		hptiop_copy_info(pinfo, "unknown\n");
		return;
	}

	switch (devinfo.type) {

	case LDT_DEVICE: {
		struct hd_driveid *driveid;
		u32 flags = le32_to_cpu(devinfo.u.device.flags);

		driveid = (struct hd_driveid *)devinfo.u.device.ident;
		/* model[] is 40 chars long, but we just want 20 chars here */
		driveid->model[20] = 0;

		if (indent)
			if (flags & DEVICE_FLAG_DISABLED)
				hptiop_copy_info(pinfo,"Missing\n");
			else
				hptiop_copy_info(pinfo, "CH%d %s\n",
					devinfo.u.device.path_id + 1,
					driveid->model);
		else {
			capacity = le64_to_cpu(devinfo.capacity) * 512;
			do_div(capacity, 1000000);
			hptiop_copy_info(pinfo,
				"CH%d %s, %lluMB, %s %s%s%s%s\n",
				devinfo.u.device.path_id + 1,
				driveid->model,
				capacity,
				(flags & DEVICE_FLAG_DISABLED)?
					"Disabled" : "Normal",
				devinfo.u.device.read_ahead_enabled?
						"[RA]" : "",
				devinfo.u.device.write_cache_enabled?
						"[WC]" : "",
				devinfo.u.device.TCQ_enabled?
						"[TCQ]" : "",
				devinfo.u.device.NCQ_enabled?
						"[NCQ]" : ""
			);
		}
		break;
	}

	case LDT_ARRAY:
		if (devinfo.target_id != INVALID_TARGET_ID)
			hptiop_copy_info(pinfo, "[DISK %d_%d] ",
					devinfo.vbus_id, devinfo.target_id);

		capacity = le64_to_cpu(devinfo.capacity) * 512;
		do_div(capacity, 1000000);
		hptiop_copy_info(pinfo, "%s (%s), %lluMB, %s\n",
			devinfo.u.array.name,
			devinfo.u.array.array_type==AT_RAID0? "RAID0" :
				devinfo.u.array.array_type==AT_RAID1? "RAID1" :
				devinfo.u.array.array_type==AT_RAID5? "RAID5" :
				devinfo.u.array.array_type==AT_RAID6? "RAID6" :
				devinfo.u.array.array_type==AT_JBOD? "JBOD" :
					"unknown",
			capacity,
			get_array_status(&devinfo));
		for (i = 0; i < devinfo.u.array.ndisk; i++) {
			if (hpt_id_valid(devinfo.u.array.members[i])) {
				if (cpu_to_le16(1<<i) &
					devinfo.u.array.critical_members)
					hptiop_copy_info(pinfo, "\t*");
				hptiop_dump_devinfo(hba, pinfo,
					devinfo.u.array.members[i], indent+1);
			}
			else
				hptiop_copy_info(pinfo, "\tMissing\n");
		}
		if (id == devinfo.u.array.transform_source) {
			hptiop_copy_info(pinfo, "\tExpanding/Migrating to:\n");
			hptiop_dump_devinfo(hba, pinfo,
				devinfo.u.array.transform_target, indent+1);
		}
		break;
	}
}

static ssize_t hptiop_show_version(struct class_device *class_dev, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", driver_ver);
}

static ssize_t hptiop_cdev_read(struct file *filp, char __user *buf,
				size_t count, loff_t *ppos)
{
	struct hptiop_hba *hba = filp->private_data;
	struct hptiop_getinfo info;
	int i, j, ndev;
	struct hpt_controller_info con_info;
	struct hpt_channel_info chan_info;
	__le32 ids[32];

	info.buffer     = buf;
	info.buflength  = count;
	info.bufoffset  = ppos ? *ppos : 0;
	info.filpos     = 0;
	info.buffillen  = 0;

	if (hptiop_get_controller_info(hba, &con_info))
		return -EIO;

	for (i = 0; i < con_info.num_buses; i++) {
		if (hptiop_get_channel_info(hba, i, &chan_info) == 0) {
			if (hpt_id_valid(chan_info.devices[0]))
				hptiop_dump_devinfo(hba, &info,
						chan_info.devices[0], 0);
			if (hpt_id_valid(chan_info.devices[1]))
				hptiop_dump_devinfo(hba, &info,
						chan_info.devices[1], 0);
		}
	}

	ndev = hptiop_get_logical_devices(hba, ids,
					sizeof(ids) / sizeof(ids[0]));

	/*
	 * if hptiop_get_logical_devices fails, ndev==-1 and it just
	 * output nothing here
	 */
	for (j = 0; j < ndev; j++)
		hptiop_dump_devinfo(hba, &info, ids[j], 0);

	if (ppos)
		*ppos += info.buffillen;

	return info.buffillen;
}

static int hptiop_cdev_ioctl(struct inode *inode,  struct file *file,
					unsigned int cmd, unsigned long arg)
{
	struct hptiop_hba *hba = file->private_data;
	struct hpt_ioctl_u ioctl_u;
	struct hpt_ioctl_k ioctl_k;
	u32 bytes_returned;
	int err = -EINVAL;

	if (copy_from_user(&ioctl_u,
		(void __user *)arg, sizeof(struct hpt_ioctl_u)))
		return -EINVAL;

	if (ioctl_u.magic != HPT_IOCTL_MAGIC)
		return -EINVAL;

	ioctl_k.ioctl_code = ioctl_u.ioctl_code;
	ioctl_k.inbuf = NULL;
	ioctl_k.inbuf_size = ioctl_u.inbuf_size;
	ioctl_k.outbuf = NULL;
	ioctl_k.outbuf_size = ioctl_u.outbuf_size;
	ioctl_k.hba = hba;
	ioctl_k.bytes_returned = &bytes_returned;

	/* verify user buffer */
	if ((ioctl_k.inbuf_size && !access_ok(VERIFY_READ,
			ioctl_u.inbuf, ioctl_k.inbuf_size)) ||
		(ioctl_k.outbuf_size && !access_ok(VERIFY_WRITE,
			ioctl_u.outbuf, ioctl_k.outbuf_size)) ||
		(ioctl_u.bytes_returned && !access_ok(VERIFY_WRITE,
			ioctl_u.bytes_returned, sizeof(u32))) ||
		ioctl_k.inbuf_size + ioctl_k.outbuf_size > 0x10000) {

		dprintk("scsi%d: got bad user address\n", hba->host->host_no);
		return -EINVAL;
	}

	/* map buffer to kernel. */
	if (ioctl_k.inbuf_size) {
		ioctl_k.inbuf = kmalloc(ioctl_k.inbuf_size, GFP_KERNEL);
		if (!ioctl_k.inbuf) {
			dprintk("scsi%d: fail to alloc inbuf\n",
					hba->host->host_no);
			err = -ENOMEM;
			goto err_exit;
		}

		if (copy_from_user(ioctl_k.inbuf,
				ioctl_u.inbuf, ioctl_k.inbuf_size)) {
			goto err_exit;
		}
	}

	if (ioctl_k.outbuf_size) {
		ioctl_k.outbuf = kmalloc(ioctl_k.outbuf_size, GFP_KERNEL);
		if (!ioctl_k.outbuf) {
			dprintk("scsi%d: fail to alloc outbuf\n",
					hba->host->host_no);
			err = -ENOMEM;
			goto err_exit;
		}
	}

	hptiop_do_ioctl(&ioctl_k);

	if (ioctl_k.result == HPT_IOCTL_RESULT_OK) {
		if (ioctl_k.outbuf_size &&
			copy_to_user(ioctl_u.outbuf,
				ioctl_k.outbuf, ioctl_k.outbuf_size))
			goto err_exit;

		if (ioctl_u.bytes_returned &&
			copy_to_user(ioctl_u.bytes_returned,
				&bytes_returned, sizeof(u32)))
			goto err_exit;

		err = 0;
	}

err_exit:
	kfree(ioctl_k.inbuf);
	kfree(ioctl_k.outbuf);

	return err;
}

static int hptiop_cdev_open(struct inode *inode, struct file *file)
{
	struct hptiop_hba *hba;
	unsigned i = 0, minor = iminor(inode);
	int ret = -ENODEV;

	spin_lock(&hptiop_hba_list_lock);
	list_for_each_entry(hba, &hptiop_hba_list, link) {
		if (i == minor) {
			file->private_data = hba;
			ret = 0;
			goto out;
		}
		i++;
	}

out:
	spin_unlock(&hptiop_hba_list_lock);
	return ret;
}

static struct file_operations hptiop_cdev_fops = {
	.owner = THIS_MODULE,
	.read  = hptiop_cdev_read,
	.ioctl = hptiop_cdev_ioctl,
	.open  = hptiop_cdev_open,
};

static ssize_t hptiop_show_fw_version(struct class_device *class_dev, char *buf)
{
	struct Scsi_Host *host = class_to_shost(class_dev);
	struct hptiop_hba *hba = (struct hptiop_hba *)host->hostdata;

	return snprintf(buf, PAGE_SIZE, "%d.%d.%d.%d\n",
				hba->firmware_version >> 24,
				(hba->firmware_version >> 16) & 0xff,
				(hba->firmware_version >> 8) & 0xff,
				hba->firmware_version & 0xff);
}

static struct class_device_attribute hptiop_attr_version = {
	.attr = {
		.name = "driver-version",
		.mode = S_IRUGO,
	},
	.show = hptiop_show_version,
};

static struct class_device_attribute hptiop_attr_fw_version = {
	.attr = {
		.name = "firmware-version",
		.mode = S_IRUGO,
	},
	.show = hptiop_show_fw_version,
};

static struct class_device_attribute *hptiop_attrs[] = {
	&hptiop_attr_version,
	&hptiop_attr_fw_version,
	NULL
};

static struct scsi_host_template driver_template = {
	.module                     = THIS_MODULE,
	.name                       = driver_name,
	.queuecommand               = hptiop_queuecommand,
	.eh_device_reset_handler    = hptiop_reset,
	.eh_bus_reset_handler       = hptiop_reset,
	.info                       = hptiop_info,
	.unchecked_isa_dma          = 0,
	.emulated                   = 0,
	.use_clustering             = ENABLE_CLUSTERING,
	.proc_name                  = driver_name,
	.shost_attrs                = hptiop_attrs,
	.this_id                    = -1,
	.change_queue_depth         = hptiop_adjust_disk_queue_depth,
};

static int __devinit hptiop_probe(struct pci_dev *pcidev,
					const struct pci_device_id *id)
{
	struct Scsi_Host *host = NULL;
	struct hptiop_hba *hba;
	struct hpt_iop_request_get_config iop_config;
	struct hpt_iop_request_set_config set_config;
	dma_addr_t start_phy;
	void *start_virt;
	u32 offset, i, req_size;

	dprintk("hptiop_probe(%p)\n", pcidev);

	if (pci_enable_device(pcidev)) {
		printk(KERN_ERR "hptiop: fail to enable pci device\n");
		return -ENODEV;
	}

	printk(KERN_INFO "adapter at PCI %d:%d:%d, IRQ %d\n",
		pcidev->bus->number, pcidev->devfn >> 3, pcidev->devfn & 7,
		pcidev->irq);

	pci_set_master(pcidev);

	/* Enable 64bit DMA if possible */
	if (pci_set_dma_mask(pcidev, DMA_64BIT_MASK)) {
		if (pci_set_dma_mask(pcidev, DMA_32BIT_MASK)) {
			printk(KERN_ERR "hptiop: fail to set dma_mask\n");
			goto disable_pci_device;
		}
	}

	if (pci_request_regions(pcidev, driver_name)) {
		printk(KERN_ERR "hptiop: pci_request_regions failed\n");
		goto disable_pci_device;
	}

	host = scsi_host_alloc(&driver_template, sizeof(struct hptiop_hba));
	if (!host) {
		printk(KERN_ERR "hptiop: fail to alloc scsi host\n");
		goto free_pci_regions;
	}

	hba = (struct hptiop_hba *)host->hostdata;

	hba->pcidev = pcidev;
	hba->host = host;
	hba->initialized = 0;

	atomic_set(&hba->resetting, 0);
	atomic_set(&hba->reset_count, 0);

	init_waitqueue_head(&hba->reset_wq);
	init_waitqueue_head(&hba->ioctl_wq);

	host->max_lun = 1;
	host->max_channel = 0;
	host->io_port = 0;
	host->n_io_port = 0;
	host->irq = pcidev->irq;

	if (hptiop_map_pci_bar(hba))
		goto free_scsi_host;

	if (iop_wait_ready(hba->iop, 20000)) {
		printk(KERN_ERR "scsi%d: firmware not ready\n",
				hba->host->host_no);
		goto unmap_pci_bar;
	}

	if (iop_get_config(hba, &iop_config)) {
		printk(KERN_ERR "scsi%d: get config failed\n",
				hba->host->host_no);
		goto unmap_pci_bar;
	}

	hba->max_requests = min(le32_to_cpu(iop_config.max_requests),
				HPTIOP_MAX_REQUESTS);
	hba->max_devices = le32_to_cpu(iop_config.max_devices);
	hba->max_request_size = le32_to_cpu(iop_config.request_size);
	hba->max_sg_descriptors = le32_to_cpu(iop_config.max_sg_count);
	hba->firmware_version = le32_to_cpu(iop_config.firmware_version);
	hba->sdram_size = le32_to_cpu(iop_config.sdram_size);

	host->max_sectors = le32_to_cpu(iop_config.data_transfer_length) >> 9;
	host->max_id = le32_to_cpu(iop_config.max_devices);
	host->sg_tablesize = le32_to_cpu(iop_config.max_sg_count);
	host->can_queue = le32_to_cpu(iop_config.max_requests);
	host->cmd_per_lun = le32_to_cpu(iop_config.max_requests);
	host->max_cmd_len = 16;

	set_config.vbus_id = cpu_to_le32(host->host_no);
	set_config.iop_id = cpu_to_le32(host->host_no);

	if (iop_set_config(hba, &set_config)) {
		printk(KERN_ERR "scsi%d: set config failed\n",
				hba->host->host_no);
		goto unmap_pci_bar;
	}

	if (scsi_add_host(host, &pcidev->dev)) {
		printk(KERN_ERR "scsi%d: scsi_add_host failed\n",
					hba->host->host_no);
		goto unmap_pci_bar;
	}

	pci_set_drvdata(pcidev, host);

	if (request_irq(pcidev->irq, hptiop_intr, SA_SHIRQ,
					driver_name, hba)) {
		printk(KERN_ERR "scsi%d: request irq %d failed\n",
					hba->host->host_no, pcidev->irq);
		goto remove_scsi_host;
	}

	/* Allocate request mem */
	req_size = sizeof(struct hpt_iop_request_scsi_command)
		+ sizeof(struct hpt_iopsg) * (hba->max_sg_descriptors - 1);
	if ((req_size& 0x1f) != 0)
		req_size = (req_size + 0x1f) & ~0x1f;

	dprintk("req_size=%d, max_requests=%d\n", req_size, hba->max_requests);

	hba->req_size = req_size;
	start_virt = dma_alloc_coherent(&pcidev->dev,
				hba->req_size*hba->max_requests + 0x20,
				&start_phy, GFP_KERNEL);

	if (!start_virt) {
		printk(KERN_ERR "scsi%d: fail to alloc request mem\n",
					hba->host->host_no);
		goto free_request_irq;
	}

	hba->dma_coherent = start_virt;
	hba->dma_coherent_handle = start_phy;

	if ((start_phy & 0x1f) != 0)
	{
		offset = ((start_phy + 0x1f) & ~0x1f) - start_phy;
		start_phy += offset;
		start_virt += offset;
	}

	hba->req_list = start_virt;
	for (i = 0; i < hba->max_requests; i++) {
		hba->reqs[i].next = NULL;
		hba->reqs[i].req_virt = start_virt;
		hba->reqs[i].req_shifted_phy = start_phy >> 5;
		hba->reqs[i].index = i;
		free_req(hba, &hba->reqs[i]);
		start_virt = (char *)start_virt + hba->req_size;
		start_phy = start_phy + hba->req_size;
	}

	/* Enable Interrupt and start background task */
	if (hptiop_initialize_iop(hba))
		goto free_request_mem;

	spin_lock(&hptiop_hba_list_lock);
	list_add_tail(&hba->link, &hptiop_hba_list);
	spin_unlock(&hptiop_hba_list_lock);

	scsi_scan_host(host);

	dprintk("scsi%d: hptiop_probe successfully\n", hba->host->host_no);
	return 0;

free_request_mem:
	dma_free_coherent(&hba->pcidev->dev,
			hba->req_size*hba->max_requests + 0x20,
			hba->dma_coherent, hba->dma_coherent_handle);

free_request_irq:
	free_irq(hba->pcidev->irq, hba);

remove_scsi_host:
	scsi_remove_host(host);

unmap_pci_bar:
	iounmap(hba->iop);

free_pci_regions:
	pci_release_regions(pcidev) ;

free_scsi_host:
	scsi_host_put(host);

disable_pci_device:
	pci_disable_device(pcidev);

	dprintk("scsi%d: hptiop_probe fail\n", host->host_no);
	return -ENODEV;
}

static void hptiop_shutdown(struct pci_dev *pcidev)
{
	struct Scsi_Host *host = pci_get_drvdata(pcidev);
	struct hptiop_hba *hba = (struct hptiop_hba *)host->hostdata;
	struct hpt_iopmu __iomem *iop = hba->iop;
	u32    int_mask;

	dprintk("hptiop_shutdown(%p)\n", hba);

	/* stop the iop */
	if (iop_send_sync_msg(hba, IOPMU_INBOUND_MSG0_SHUTDOWN, 60000))
		printk(KERN_ERR "scsi%d: shutdown the iop timeout\n",
					hba->host->host_no);

	/* disable all outbound interrupts */
	int_mask = readl(&iop->outbound_intmask);
	writel(int_mask |
		IOPMU_OUTBOUND_INT_MSG0 | IOPMU_OUTBOUND_INT_POSTQUEUE,
		&iop->outbound_intmask);
	hptiop_pci_posting_flush(iop);
}

static void hptiop_remove(struct pci_dev *pcidev)
{
	struct Scsi_Host *host = pci_get_drvdata(pcidev);
	struct hptiop_hba *hba = (struct hptiop_hba *)host->hostdata;

	dprintk("scsi%d: hptiop_remove\n", hba->host->host_no);

	scsi_remove_host(host);

	spin_lock(&hptiop_hba_list_lock);
	list_del_init(&hba->link);
	spin_unlock(&hptiop_hba_list_lock);

	hptiop_shutdown(pcidev);

	free_irq(hba->pcidev->irq, hba);

	dma_free_coherent(&hba->pcidev->dev,
			hba->req_size * hba->max_requests + 0x20,
			hba->dma_coherent,
			hba->dma_coherent_handle);

	iounmap(hba->iop);

	pci_release_regions(hba->pcidev);
	pci_set_drvdata(hba->pcidev, NULL);
	pci_disable_device(hba->pcidev);

	scsi_host_put(host);
}

static struct pci_device_id hptiop_id_table[] = {
	{ PCI_DEVICE(0x1103, 0x3220) },
	{ PCI_DEVICE(0x1103, 0x3320) },
	{},
};

MODULE_DEVICE_TABLE(pci, hptiop_id_table);

static struct pci_driver hptiop_pci_driver = {
	.name       = driver_name,
	.id_table   = hptiop_id_table,
	.probe      = hptiop_probe,
	.remove     = hptiop_remove,
	.shutdown   = hptiop_shutdown,
};

static int __init hptiop_module_init(void)
{
	int error;

	printk(KERN_INFO "%s %s\n", driver_name_long, driver_ver);

	error = pci_register_driver(&hptiop_pci_driver);
	if (error < 0)
		return error;

	hptiop_cdev_major = register_chrdev(0, "hptiop", &hptiop_cdev_fops);
	if (hptiop_cdev_major < 0) {
		printk(KERN_WARNING "unable to register hptiop device.\n");
		return hptiop_cdev_major;
	}

	return 0;
}

static void __exit hptiop_module_exit(void)
{
	dprintk("hptiop_module_exit\n");
	unregister_chrdev(hptiop_cdev_major, "hptiop");
	pci_unregister_driver(&hptiop_pci_driver);
}


module_init(hptiop_module_init);
module_exit(hptiop_module_exit);

MODULE_LICENSE("GPL");
