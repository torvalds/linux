/*
 * HighPoint RR3xxx controller driver for Linux
 * Copyright (C) 2006-2007 HighPoint Technologies, Inc. All Rights Reserved.
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
static const char driver_ver[] = "v1.2 (070830)";

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
	if (tag & IOPMU_QUEUE_ADDR_HOST_BIT)
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

static void hptiop_host_request_callback(struct hptiop_hba *hba, u32 _tag)
{
	struct hpt_iop_request_scsi_command *req;
	struct scsi_cmnd *scp;
	u32 tag;

	if (hba->iopintf_v2) {
		tag = _tag & ~ IOPMU_QUEUE_REQUEST_RESULT_BIT;
		req = hba->reqs[tag].req_virt;
		if (likely(_tag & IOPMU_QUEUE_REQUEST_RESULT_BIT))
			req->header.result = IOP_RESULT_SUCCESS;
	} else {
		tag = _tag;
		req = hba->reqs[tag].req_virt;
	}

	dprintk("hptiop_host_request_callback: req=%p, type=%d, "
			"result=%d, context=0x%x tag=%d\n",
			req, req->header.type, req->header.result,
			req->header.context, tag);

	BUG_ON(!req->header.result);
	BUG_ON(req->header.type != cpu_to_le32(IOP_REQUEST_TYPE_SCSI_COMMAND));

	scp = hba->reqs[tag].scp;

	if (HPT_SCP(scp)->mapped)
		scsi_dma_unmap(scp);

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
		memcpy(&scp->sense_buffer, &req->sg_list,
				min(sizeof(scp->sense_buffer),
					le32_to_cpu(req->dataxfer_length)));
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

static irqreturn_t hptiop_intr(int irq, void *dev_id)
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
	struct scatterlist *sg;
	int idx, nseg;

	nseg = scsi_dma_map(scp);
	BUG_ON(nseg < 0);
	if (!nseg)
		return 0;

	HPT_SCP(scp)->sgcnt = nseg;
	HPT_SCP(scp)->mapped = 1;

	BUG_ON(HPT_SCP(scp)->sgcnt > hba->max_sg_descriptors);

	scsi_for_each_sg(scp, sg, HPT_SCP(scp)->sgcnt, idx) {
		psg[idx].pci_address = cpu_to_le64(sg_dma_address(sg));
		psg[idx].size = cpu_to_le32(sg_dma_len(sg));
		psg[idx].eot = (idx == HPT_SCP(scp)->sgcnt - 1) ?
			cpu_to_le32(1) : 0;
	}
	return HPT_SCP(scp)->sgcnt;
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

	req = _req->req_virt;

	/* build S/G table */
	sg_count = hptiop_buildsgl(scp, req->sg_list);
	if (!sg_count)
		HPT_SCP(scp)->mapped = 0;

	req->header.flags = cpu_to_le32(IOP_REQUEST_FLAG_OUTPUT_CONTEXT);
	req->header.type = cpu_to_le32(IOP_REQUEST_TYPE_SCSI_COMMAND);
	req->header.result = cpu_to_le32(IOP_RESULT_PENDING);
	req->header.context = cpu_to_le32(IOPMU_QUEUE_ADDR_HOST_BIT |
							(u32)_req->index);
	req->header.context_hi32 = 0;
	req->dataxfer_length = cpu_to_le32(scsi_bufflen(scp));
	req->channel = scp->device->channel;
	req->target = scp->device->id;
	req->lun = scp->device->lun;
	req->header.size = cpu_to_le32(
				sizeof(struct hpt_iop_request_scsi_command)
				 - sizeof(struct hpt_iopsg)
				 + sg_count * sizeof(struct hpt_iopsg));

	memcpy(req->cdb, scp->cmnd, sizeof(req->cdb));

	if (hba->iopintf_v2) {
		u32 size_bits;
		if (req->header.size < 256)
			size_bits = IOPMU_QUEUE_REQUEST_SIZE_BIT;
		else if (req->header.size < 512)
			size_bits = IOPMU_QUEUE_ADDR_HOST_BIT;
		else
			size_bits = IOPMU_QUEUE_REQUEST_SIZE_BIT |
						IOPMU_QUEUE_ADDR_HOST_BIT;
		writel(_req->req_shifted_phy | size_bits, &hba->iop->inbound_queue);
	} else
		writel(_req->req_shifted_phy | IOPMU_QUEUE_ADDR_HOST_BIT,
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
				&hba->iop->inbound_msgaddr0);
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

static ssize_t hptiop_show_version(struct class_device *class_dev, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", driver_ver);
}

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
	.use_sg_chaining            = ENABLE_SG_CHAINING,
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
	hba->iopintf_v2 = 0;

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
	hba->interface_version = le32_to_cpu(iop_config.interface_version);
	hba->sdram_size = le32_to_cpu(iop_config.sdram_size);

	if (hba->firmware_version > 0x01020000 ||
			hba->interface_version > 0x01020000)
		hba->iopintf_v2 = 1;

	host->max_sectors = le32_to_cpu(iop_config.data_transfer_length) >> 9;
	host->max_id = le32_to_cpu(iop_config.max_devices);
	host->sg_tablesize = le32_to_cpu(iop_config.max_sg_count);
	host->can_queue = le32_to_cpu(iop_config.max_requests);
	host->cmd_per_lun = le32_to_cpu(iop_config.max_requests);
	host->max_cmd_len = 16;

	req_size = sizeof(struct hpt_iop_request_scsi_command)
		+ sizeof(struct hpt_iopsg) * (hba->max_sg_descriptors - 1);
	if ((req_size & 0x1f) != 0)
		req_size = (req_size + 0x1f) & ~0x1f;

	memset(&set_config, 0, sizeof(struct hpt_iop_request_set_config));
	set_config.iop_id = cpu_to_le32(host->host_no);
	set_config.vbus_id = cpu_to_le16(host->host_no);
	set_config.max_host_request_size = cpu_to_le16(req_size);

	if (iop_set_config(hba, &set_config)) {
		printk(KERN_ERR "scsi%d: set config failed\n",
				hba->host->host_no);
		goto unmap_pci_bar;
	}

	pci_set_drvdata(pcidev, host);

	if (request_irq(pcidev->irq, hptiop_intr, IRQF_SHARED,
					driver_name, hba)) {
		printk(KERN_ERR "scsi%d: request irq %d failed\n",
					hba->host->host_no, pcidev->irq);
		goto unmap_pci_bar;
	}

	/* Allocate request mem */

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

	if (scsi_add_host(host, &pcidev->dev)) {
		printk(KERN_ERR "scsi%d: scsi_add_host failed\n",
					hba->host->host_no);
		goto free_request_mem;
	}


	scsi_scan_host(host);

	dprintk("scsi%d: hptiop_probe successfully\n", hba->host->host_no);
	return 0;

free_request_mem:
	dma_free_coherent(&hba->pcidev->dev,
			hba->req_size*hba->max_requests + 0x20,
			hba->dma_coherent, hba->dma_coherent_handle);

free_request_irq:
	free_irq(hba->pcidev->irq, hba);

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
	{ PCI_VDEVICE(TTI, 0x3220) },
	{ PCI_VDEVICE(TTI, 0x3320) },
	{ PCI_VDEVICE(TTI, 0x3520) },
	{ PCI_VDEVICE(TTI, 0x4320) },
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
	printk(KERN_INFO "%s %s\n", driver_name_long, driver_ver);
	return pci_register_driver(&hptiop_pci_driver);
}

static void __exit hptiop_module_exit(void)
{
	pci_unregister_driver(&hptiop_pci_driver);
}


module_init(hptiop_module_init);
module_exit(hptiop_module_exit);

MODULE_LICENSE("GPL");

