// SPDX-License-Identifier: GPL-2.0-only
/*
 * HighPoint RR3xxx/4xxx controller driver for Linux
 * Copyright (C) 2006-2015 HighPoint Technologies, Inc. All Rights Reserved.
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
#include <linux/gfp.h>
#include <linux/uaccess.h>
#include <asm/io.h>
#include <asm/div64.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_host.h>

#include "hptiop.h"

MODULE_AUTHOR("HighPoint Technologies, Inc.");
MODULE_DESCRIPTION("HighPoint RocketRAID 3xxx/4xxx Controller Driver");

static char driver_name[] = "hptiop";
static const char driver_name_long[] = "RocketRAID 3xxx/4xxx Controller driver";
static const char driver_ver[] = "v1.10.0";

static int iop_send_sync_msg(struct hptiop_hba *hba, u32 msg, u32 millisec);
static void hptiop_finish_scsi_req(struct hptiop_hba *hba, u32 tag,
				struct hpt_iop_request_scsi_command *req);
static void hptiop_host_request_callback_itl(struct hptiop_hba *hba, u32 tag);
static void hptiop_iop_request_callback_itl(struct hptiop_hba *hba, u32 tag);
static void hptiop_message_callback(struct hptiop_hba *hba, u32 msg);

static int iop_wait_ready_itl(struct hptiop_hba *hba, u32 millisec)
{
	u32 req = 0;
	int i;

	for (i = 0; i < millisec; i++) {
		req = readl(&hba->u.itl.iop->inbound_queue);
		if (req != IOPMU_QUEUE_EMPTY)
			break;
		msleep(1);
	}

	if (req != IOPMU_QUEUE_EMPTY) {
		writel(req, &hba->u.itl.iop->outbound_queue);
		readl(&hba->u.itl.iop->outbound_intstatus);
		return 0;
	}

	return -1;
}

static int iop_wait_ready_mv(struct hptiop_hba *hba, u32 millisec)
{
	return iop_send_sync_msg(hba, IOPMU_INBOUND_MSG0_NOP, millisec);
}

static int iop_wait_ready_mvfrey(struct hptiop_hba *hba, u32 millisec)
{
	return iop_send_sync_msg(hba, IOPMU_INBOUND_MSG0_NOP, millisec);
}

static void hptiop_request_callback_itl(struct hptiop_hba *hba, u32 tag)
{
	if (tag & IOPMU_QUEUE_ADDR_HOST_BIT)
		hptiop_host_request_callback_itl(hba,
				tag & ~IOPMU_QUEUE_ADDR_HOST_BIT);
	else
		hptiop_iop_request_callback_itl(hba, tag);
}

static void hptiop_drain_outbound_queue_itl(struct hptiop_hba *hba)
{
	u32 req;

	while ((req = readl(&hba->u.itl.iop->outbound_queue)) !=
						IOPMU_QUEUE_EMPTY) {

		if (req & IOPMU_QUEUE_MASK_HOST_BITS)
			hptiop_request_callback_itl(hba, req);
		else {
			struct hpt_iop_request_header __iomem * p;

			p = (struct hpt_iop_request_header __iomem *)
				((char __iomem *)hba->u.itl.iop + req);

			if (readl(&p->flags) & IOP_REQUEST_FLAG_SYNC_REQUEST) {
				if (readl(&p->context))
					hptiop_request_callback_itl(hba, req);
				else
					writel(1, &p->context);
			}
			else
				hptiop_request_callback_itl(hba, req);
		}
	}
}

static int iop_intr_itl(struct hptiop_hba *hba)
{
	struct hpt_iopmu_itl __iomem *iop = hba->u.itl.iop;
	void __iomem *plx = hba->u.itl.plx;
	u32 status;
	int ret = 0;

	if (plx && readl(plx + 0x11C5C) & 0xf)
		writel(1, plx + 0x11C60);

	status = readl(&iop->outbound_intstatus);

	if (status & IOPMU_OUTBOUND_INT_MSG0) {
		u32 msg = readl(&iop->outbound_msgaddr0);

		dprintk("received outbound msg %x\n", msg);
		writel(IOPMU_OUTBOUND_INT_MSG0, &iop->outbound_intstatus);
		hptiop_message_callback(hba, msg);
		ret = 1;
	}

	if (status & IOPMU_OUTBOUND_INT_POSTQUEUE) {
		hptiop_drain_outbound_queue_itl(hba);
		ret = 1;
	}

	return ret;
}

static u64 mv_outbound_read(struct hpt_iopmu_mv __iomem *mu)
{
	u32 outbound_tail = readl(&mu->outbound_tail);
	u32 outbound_head = readl(&mu->outbound_head);

	if (outbound_tail != outbound_head) {
		u64 p;

		memcpy_fromio(&p, &mu->outbound_q[mu->outbound_tail], 8);
		outbound_tail++;

		if (outbound_tail == MVIOP_QUEUE_LEN)
			outbound_tail = 0;
		writel(outbound_tail, &mu->outbound_tail);
		return p;
	} else
		return 0;
}

static void mv_inbound_write(u64 p, struct hptiop_hba *hba)
{
	u32 inbound_head = readl(&hba->u.mv.mu->inbound_head);
	u32 head = inbound_head + 1;

	if (head == MVIOP_QUEUE_LEN)
		head = 0;

	memcpy_toio(&hba->u.mv.mu->inbound_q[inbound_head], &p, 8);
	writel(head, &hba->u.mv.mu->inbound_head);
	writel(MVIOP_MU_INBOUND_INT_POSTQUEUE,
			&hba->u.mv.regs->inbound_doorbell);
}

static void hptiop_request_callback_mv(struct hptiop_hba *hba, u64 tag)
{
	u32 req_type = (tag >> 5) & 0x7;
	struct hpt_iop_request_scsi_command *req;

	dprintk("hptiop_request_callback_mv: tag=%llx\n", tag);

	BUG_ON((tag & MVIOP_MU_QUEUE_REQUEST_RETURN_CONTEXT) == 0);

	switch (req_type) {
	case IOP_REQUEST_TYPE_GET_CONFIG:
	case IOP_REQUEST_TYPE_SET_CONFIG:
		hba->msg_done = 1;
		break;

	case IOP_REQUEST_TYPE_SCSI_COMMAND:
		req = hba->reqs[tag >> 8].req_virt;
		if (likely(tag & MVIOP_MU_QUEUE_REQUEST_RESULT_BIT))
			req->header.result = cpu_to_le32(IOP_RESULT_SUCCESS);

		hptiop_finish_scsi_req(hba, tag>>8, req);
		break;

	default:
		break;
	}
}

static int iop_intr_mv(struct hptiop_hba *hba)
{
	u32 status;
	int ret = 0;

	status = readl(&hba->u.mv.regs->outbound_doorbell);
	writel(~status, &hba->u.mv.regs->outbound_doorbell);

	if (status & MVIOP_MU_OUTBOUND_INT_MSG) {
		u32 msg;
		msg = readl(&hba->u.mv.mu->outbound_msg);
		dprintk("received outbound msg %x\n", msg);
		hptiop_message_callback(hba, msg);
		ret = 1;
	}

	if (status & MVIOP_MU_OUTBOUND_INT_POSTQUEUE) {
		u64 tag;

		while ((tag = mv_outbound_read(hba->u.mv.mu)))
			hptiop_request_callback_mv(hba, tag);
		ret = 1;
	}

	return ret;
}

static void hptiop_request_callback_mvfrey(struct hptiop_hba *hba, u32 _tag)
{
	u32 req_type = _tag & 0xf;
	struct hpt_iop_request_scsi_command *req;

	switch (req_type) {
	case IOP_REQUEST_TYPE_GET_CONFIG:
	case IOP_REQUEST_TYPE_SET_CONFIG:
		hba->msg_done = 1;
		break;

	case IOP_REQUEST_TYPE_SCSI_COMMAND:
		req = hba->reqs[(_tag >> 4) & 0xff].req_virt;
		if (likely(_tag & IOPMU_QUEUE_REQUEST_RESULT_BIT))
			req->header.result = IOP_RESULT_SUCCESS;
		hptiop_finish_scsi_req(hba, (_tag >> 4) & 0xff, req);
		break;

	default:
		break;
	}
}

static int iop_intr_mvfrey(struct hptiop_hba *hba)
{
	u32 _tag, status, cptr, cur_rptr;
	int ret = 0;

	if (hba->initialized)
		writel(0, &(hba->u.mvfrey.mu->pcie_f0_int_enable));

	status = readl(&(hba->u.mvfrey.mu->f0_doorbell));
	if (status) {
		writel(status, &(hba->u.mvfrey.mu->f0_doorbell));
		if (status & CPU_TO_F0_DRBL_MSG_BIT) {
			u32 msg = readl(&(hba->u.mvfrey.mu->cpu_to_f0_msg_a));
			dprintk("received outbound msg %x\n", msg);
			hptiop_message_callback(hba, msg);
		}
		ret = 1;
	}

	status = readl(&(hba->u.mvfrey.mu->isr_cause));
	if (status) {
		writel(status, &(hba->u.mvfrey.mu->isr_cause));
		do {
			cptr = *hba->u.mvfrey.outlist_cptr & 0xff;
			cur_rptr = hba->u.mvfrey.outlist_rptr;
			while (cur_rptr != cptr) {
				cur_rptr++;
				if (cur_rptr ==	hba->u.mvfrey.list_count)
					cur_rptr = 0;

				_tag = hba->u.mvfrey.outlist[cur_rptr].val;
				BUG_ON(!(_tag & IOPMU_QUEUE_MASK_HOST_BITS));
				hptiop_request_callback_mvfrey(hba, _tag);
				ret = 1;
			}
			hba->u.mvfrey.outlist_rptr = cur_rptr;
		} while (cptr != (*hba->u.mvfrey.outlist_cptr & 0xff));
	}

	if (hba->initialized)
		writel(0x1010, &(hba->u.mvfrey.mu->pcie_f0_int_enable));

	return ret;
}

static int iop_send_sync_request_itl(struct hptiop_hba *hba,
					void __iomem *_req, u32 millisec)
{
	struct hpt_iop_request_header __iomem *req = _req;
	u32 i;

	writel(readl(&req->flags) | IOP_REQUEST_FLAG_SYNC_REQUEST, &req->flags);
	writel(0, &req->context);
	writel((unsigned long)req - (unsigned long)hba->u.itl.iop,
			&hba->u.itl.iop->inbound_queue);
	readl(&hba->u.itl.iop->outbound_intstatus);

	for (i = 0; i < millisec; i++) {
		iop_intr_itl(hba);
		if (readl(&req->context))
			return 0;
		msleep(1);
	}

	return -1;
}

static int iop_send_sync_request_mv(struct hptiop_hba *hba,
					u32 size_bits, u32 millisec)
{
	struct hpt_iop_request_header *reqhdr = hba->u.mv.internal_req;
	u32 i;

	hba->msg_done = 0;
	reqhdr->flags |= cpu_to_le32(IOP_REQUEST_FLAG_SYNC_REQUEST);
	mv_inbound_write(hba->u.mv.internal_req_phy |
			MVIOP_MU_QUEUE_ADDR_HOST_BIT | size_bits, hba);

	for (i = 0; i < millisec; i++) {
		iop_intr_mv(hba);
		if (hba->msg_done)
			return 0;
		msleep(1);
	}
	return -1;
}

static int iop_send_sync_request_mvfrey(struct hptiop_hba *hba,
					u32 size_bits, u32 millisec)
{
	struct hpt_iop_request_header *reqhdr =
		hba->u.mvfrey.internal_req.req_virt;
	u32 i;

	hba->msg_done = 0;
	reqhdr->flags |= cpu_to_le32(IOP_REQUEST_FLAG_SYNC_REQUEST);
	hba->ops->post_req(hba, &(hba->u.mvfrey.internal_req));

	for (i = 0; i < millisec; i++) {
		iop_intr_mvfrey(hba);
		if (hba->msg_done)
			break;
		msleep(1);
	}
	return hba->msg_done ? 0 : -1;
}

static void hptiop_post_msg_itl(struct hptiop_hba *hba, u32 msg)
{
	writel(msg, &hba->u.itl.iop->inbound_msgaddr0);
	readl(&hba->u.itl.iop->outbound_intstatus);
}

static void hptiop_post_msg_mv(struct hptiop_hba *hba, u32 msg)
{
	writel(msg, &hba->u.mv.mu->inbound_msg);
	writel(MVIOP_MU_INBOUND_INT_MSG, &hba->u.mv.regs->inbound_doorbell);
	readl(&hba->u.mv.regs->inbound_doorbell);
}

static void hptiop_post_msg_mvfrey(struct hptiop_hba *hba, u32 msg)
{
	writel(msg, &(hba->u.mvfrey.mu->f0_to_cpu_msg_a));
	readl(&(hba->u.mvfrey.mu->f0_to_cpu_msg_a));
}

static int iop_send_sync_msg(struct hptiop_hba *hba, u32 msg, u32 millisec)
{
	u32 i;

	hba->msg_done = 0;
	hba->ops->disable_intr(hba);
	hba->ops->post_msg(hba, msg);

	for (i = 0; i < millisec; i++) {
		spin_lock_irq(hba->host->host_lock);
		hba->ops->iop_intr(hba);
		spin_unlock_irq(hba->host->host_lock);
		if (hba->msg_done)
			break;
		msleep(1);
	}

	hba->ops->enable_intr(hba);
	return hba->msg_done? 0 : -1;
}

static int iop_get_config_itl(struct hptiop_hba *hba,
				struct hpt_iop_request_get_config *config)
{
	u32 req32;
	struct hpt_iop_request_get_config __iomem *req;

	req32 = readl(&hba->u.itl.iop->inbound_queue);
	if (req32 == IOPMU_QUEUE_EMPTY)
		return -1;

	req = (struct hpt_iop_request_get_config __iomem *)
			((unsigned long)hba->u.itl.iop + req32);

	writel(0, &req->header.flags);
	writel(IOP_REQUEST_TYPE_GET_CONFIG, &req->header.type);
	writel(sizeof(struct hpt_iop_request_get_config), &req->header.size);
	writel(IOP_RESULT_PENDING, &req->header.result);

	if (iop_send_sync_request_itl(hba, req, 20000)) {
		dprintk("Get config send cmd failed\n");
		return -1;
	}

	memcpy_fromio(config, req, sizeof(*config));
	writel(req32, &hba->u.itl.iop->outbound_queue);
	return 0;
}

static int iop_get_config_mv(struct hptiop_hba *hba,
				struct hpt_iop_request_get_config *config)
{
	struct hpt_iop_request_get_config *req = hba->u.mv.internal_req;

	req->header.flags = cpu_to_le32(IOP_REQUEST_FLAG_OUTPUT_CONTEXT);
	req->header.type = cpu_to_le32(IOP_REQUEST_TYPE_GET_CONFIG);
	req->header.size =
		cpu_to_le32(sizeof(struct hpt_iop_request_get_config));
	req->header.result = cpu_to_le32(IOP_RESULT_PENDING);
	req->header.context = cpu_to_le32(IOP_REQUEST_TYPE_GET_CONFIG<<5);
	req->header.context_hi32 = 0;

	if (iop_send_sync_request_mv(hba, 0, 20000)) {
		dprintk("Get config send cmd failed\n");
		return -1;
	}

	memcpy(config, req, sizeof(struct hpt_iop_request_get_config));
	return 0;
}

static int iop_get_config_mvfrey(struct hptiop_hba *hba,
				struct hpt_iop_request_get_config *config)
{
	struct hpt_iop_request_get_config *info = hba->u.mvfrey.config;

	if (info->header.size != sizeof(struct hpt_iop_request_get_config) ||
			info->header.type != IOP_REQUEST_TYPE_GET_CONFIG)
		return -1;

	config->interface_version = info->interface_version;
	config->firmware_version = info->firmware_version;
	config->max_requests = info->max_requests;
	config->request_size = info->request_size;
	config->max_sg_count = info->max_sg_count;
	config->data_transfer_length = info->data_transfer_length;
	config->alignment_mask = info->alignment_mask;
	config->max_devices = info->max_devices;
	config->sdram_size = info->sdram_size;

	return 0;
}

static int iop_set_config_itl(struct hptiop_hba *hba,
				struct hpt_iop_request_set_config *config)
{
	u32 req32;
	struct hpt_iop_request_set_config __iomem *req;

	req32 = readl(&hba->u.itl.iop->inbound_queue);
	if (req32 == IOPMU_QUEUE_EMPTY)
		return -1;

	req = (struct hpt_iop_request_set_config __iomem *)
			((unsigned long)hba->u.itl.iop + req32);

	memcpy_toio((u8 __iomem *)req + sizeof(struct hpt_iop_request_header),
		(u8 *)config + sizeof(struct hpt_iop_request_header),
		sizeof(struct hpt_iop_request_set_config) -
			sizeof(struct hpt_iop_request_header));

	writel(0, &req->header.flags);
	writel(IOP_REQUEST_TYPE_SET_CONFIG, &req->header.type);
	writel(sizeof(struct hpt_iop_request_set_config), &req->header.size);
	writel(IOP_RESULT_PENDING, &req->header.result);

	if (iop_send_sync_request_itl(hba, req, 20000)) {
		dprintk("Set config send cmd failed\n");
		return -1;
	}

	writel(req32, &hba->u.itl.iop->outbound_queue);
	return 0;
}

static int iop_set_config_mv(struct hptiop_hba *hba,
				struct hpt_iop_request_set_config *config)
{
	struct hpt_iop_request_set_config *req = hba->u.mv.internal_req;

	memcpy(req, config, sizeof(struct hpt_iop_request_set_config));
	req->header.flags = cpu_to_le32(IOP_REQUEST_FLAG_OUTPUT_CONTEXT);
	req->header.type = cpu_to_le32(IOP_REQUEST_TYPE_SET_CONFIG);
	req->header.size =
		cpu_to_le32(sizeof(struct hpt_iop_request_set_config));
	req->header.result = cpu_to_le32(IOP_RESULT_PENDING);
	req->header.context = cpu_to_le32(IOP_REQUEST_TYPE_SET_CONFIG<<5);
	req->header.context_hi32 = 0;

	if (iop_send_sync_request_mv(hba, 0, 20000)) {
		dprintk("Set config send cmd failed\n");
		return -1;
	}

	return 0;
}

static int iop_set_config_mvfrey(struct hptiop_hba *hba,
				struct hpt_iop_request_set_config *config)
{
	struct hpt_iop_request_set_config *req =
		hba->u.mvfrey.internal_req.req_virt;

	memcpy(req, config, sizeof(struct hpt_iop_request_set_config));
	req->header.flags = cpu_to_le32(IOP_REQUEST_FLAG_OUTPUT_CONTEXT);
	req->header.type = cpu_to_le32(IOP_REQUEST_TYPE_SET_CONFIG);
	req->header.size =
		cpu_to_le32(sizeof(struct hpt_iop_request_set_config));
	req->header.result = cpu_to_le32(IOP_RESULT_PENDING);
	req->header.context = cpu_to_le32(IOP_REQUEST_TYPE_SET_CONFIG<<5);
	req->header.context_hi32 = 0;

	if (iop_send_sync_request_mvfrey(hba, 0, 20000)) {
		dprintk("Set config send cmd failed\n");
		return -1;
	}

	return 0;
}

static void hptiop_enable_intr_itl(struct hptiop_hba *hba)
{
	writel(~(IOPMU_OUTBOUND_INT_POSTQUEUE | IOPMU_OUTBOUND_INT_MSG0),
		&hba->u.itl.iop->outbound_intmask);
}

static void hptiop_enable_intr_mv(struct hptiop_hba *hba)
{
	writel(MVIOP_MU_OUTBOUND_INT_POSTQUEUE | MVIOP_MU_OUTBOUND_INT_MSG,
		&hba->u.mv.regs->outbound_intmask);
}

static void hptiop_enable_intr_mvfrey(struct hptiop_hba *hba)
{
	writel(CPU_TO_F0_DRBL_MSG_BIT, &(hba->u.mvfrey.mu->f0_doorbell_enable));
	writel(0x1, &(hba->u.mvfrey.mu->isr_enable));
	writel(0x1010, &(hba->u.mvfrey.mu->pcie_f0_int_enable));
}

static int hptiop_initialize_iop(struct hptiop_hba *hba)
{
	/* enable interrupts */
	hba->ops->enable_intr(hba);

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

static void __iomem *hptiop_map_pci_bar(struct hptiop_hba *hba, int index)
{
	u32 mem_base_phy, length;
	void __iomem *mem_base_virt;

	struct pci_dev *pcidev = hba->pcidev;


	if (!(pci_resource_flags(pcidev, index) & IORESOURCE_MEM)) {
		printk(KERN_ERR "scsi%d: pci resource invalid\n",
				hba->host->host_no);
		return NULL;
	}

	mem_base_phy = pci_resource_start(pcidev, index);
	length = pci_resource_len(pcidev, index);
	mem_base_virt = ioremap(mem_base_phy, length);

	if (!mem_base_virt) {
		printk(KERN_ERR "scsi%d: Fail to ioremap memory space\n",
				hba->host->host_no);
		return NULL;
	}
	return mem_base_virt;
}

static int hptiop_map_pci_bar_itl(struct hptiop_hba *hba)
{
	struct pci_dev *pcidev = hba->pcidev;
	hba->u.itl.iop = hptiop_map_pci_bar(hba, 0);
	if (hba->u.itl.iop == NULL)
		return -1;
	if ((pcidev->device & 0xff00) == 0x4400) {
		hba->u.itl.plx = hba->u.itl.iop;
		hba->u.itl.iop = hptiop_map_pci_bar(hba, 2);
		if (hba->u.itl.iop == NULL) {
			iounmap(hba->u.itl.plx);
			return -1;
		}
	}
	return 0;
}

static void hptiop_unmap_pci_bar_itl(struct hptiop_hba *hba)
{
	if (hba->u.itl.plx)
		iounmap(hba->u.itl.plx);
	iounmap(hba->u.itl.iop);
}

static int hptiop_map_pci_bar_mv(struct hptiop_hba *hba)
{
	hba->u.mv.regs = hptiop_map_pci_bar(hba, 0);
	if (hba->u.mv.regs == NULL)
		return -1;

	hba->u.mv.mu = hptiop_map_pci_bar(hba, 2);
	if (hba->u.mv.mu == NULL) {
		iounmap(hba->u.mv.regs);
		return -1;
	}

	return 0;
}

static int hptiop_map_pci_bar_mvfrey(struct hptiop_hba *hba)
{
	hba->u.mvfrey.config = hptiop_map_pci_bar(hba, 0);
	if (hba->u.mvfrey.config == NULL)
		return -1;

	hba->u.mvfrey.mu = hptiop_map_pci_bar(hba, 2);
	if (hba->u.mvfrey.mu == NULL) {
		iounmap(hba->u.mvfrey.config);
		return -1;
	}

	return 0;
}

static void hptiop_unmap_pci_bar_mv(struct hptiop_hba *hba)
{
	iounmap(hba->u.mv.regs);
	iounmap(hba->u.mv.mu);
}

static void hptiop_unmap_pci_bar_mvfrey(struct hptiop_hba *hba)
{
	iounmap(hba->u.mvfrey.config);
	iounmap(hba->u.mvfrey.mu);
}

static void hptiop_message_callback(struct hptiop_hba *hba, u32 msg)
{
	dprintk("iop message 0x%x\n", msg);

	if (msg == IOPMU_INBOUND_MSG0_NOP ||
		msg == IOPMU_INBOUND_MSG0_RESET_COMM)
		hba->msg_done = 1;

	if (!hba->initialized)
		return;

	if (msg == IOPMU_INBOUND_MSG0_RESET) {
		atomic_set(&hba->resetting, 0);
		wake_up(&hba->reset_wq);
	}
	else if (msg <= IOPMU_INBOUND_MSG0_MAX)
		hba->msg_done = 1;
}

static struct hptiop_request *get_req(struct hptiop_hba *hba)
{
	struct hptiop_request *ret;

	dprintk("get_req : req=%p\n", hba->req_list);

	ret = hba->req_list;
	if (ret)
		hba->req_list = ret->next;

	return ret;
}

static void free_req(struct hptiop_hba *hba, struct hptiop_request *req)
{
	dprintk("free_req(%d, %p)\n", req->index, req);
	req->next = hba->req_list;
	hba->req_list = req;
}

static void hptiop_finish_scsi_req(struct hptiop_hba *hba, u32 tag,
				struct hpt_iop_request_scsi_command *req)
{
	struct scsi_cmnd *scp;

	dprintk("hptiop_finish_scsi_req: req=%p, type=%d, "
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
		scsi_set_resid(scp,
			scsi_bufflen(scp) - le32_to_cpu(req->dataxfer_length));
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
	case IOP_RESULT_CHECK_CONDITION:
		scsi_set_resid(scp,
			scsi_bufflen(scp) - le32_to_cpu(req->dataxfer_length));
		scp->result = SAM_STAT_CHECK_CONDITION;
		memcpy(scp->sense_buffer, &req->sg_list, SCSI_SENSE_BUFFERSIZE);
		goto skip_resid;

	default:
		scp->result = DID_ABORT << 16;
		break;
	}

	scsi_set_resid(scp,
		scsi_bufflen(scp) - le32_to_cpu(req->dataxfer_length));

skip_resid:
	dprintk("scsi_done(%p)\n", scp);
	scsi_done(scp);
	free_req(hba, &hba->reqs[tag]);
}

static void hptiop_host_request_callback_itl(struct hptiop_hba *hba, u32 _tag)
{
	struct hpt_iop_request_scsi_command *req;
	u32 tag;

	if (hba->iopintf_v2) {
		tag = _tag & ~IOPMU_QUEUE_REQUEST_RESULT_BIT;
		req = hba->reqs[tag].req_virt;
		if (likely(_tag & IOPMU_QUEUE_REQUEST_RESULT_BIT))
			req->header.result = cpu_to_le32(IOP_RESULT_SUCCESS);
	} else {
		tag = _tag;
		req = hba->reqs[tag].req_virt;
	}

	hptiop_finish_scsi_req(hba, tag, req);
}

static void hptiop_iop_request_callback_itl(struct hptiop_hba *hba, u32 tag)
{
	struct hpt_iop_request_header __iomem *req;
	struct hpt_iop_request_ioctl_command __iomem *p;
	struct hpt_ioctl_k *arg;

	req = (struct hpt_iop_request_header __iomem *)
			((unsigned long)hba->u.itl.iop + tag);
	dprintk("hptiop_iop_request_callback_itl: req=%p, type=%d, "
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
	writel(tag, &hba->u.itl.iop->outbound_queue);
}

static irqreturn_t hptiop_intr(int irq, void *dev_id)
{
	struct hptiop_hba  *hba = dev_id;
	int  handled;
	unsigned long flags;

	spin_lock_irqsave(hba->host->host_lock, flags);
	handled = hba->ops->iop_intr(hba);
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
		psg[idx].pci_address = cpu_to_le64(sg_dma_address(sg)) |
			hba->ops->host_phy_flag;
		psg[idx].size = cpu_to_le32(sg_dma_len(sg));
		psg[idx].eot = (idx == HPT_SCP(scp)->sgcnt - 1) ?
			cpu_to_le32(1) : 0;
	}
	return HPT_SCP(scp)->sgcnt;
}

static void hptiop_post_req_itl(struct hptiop_hba *hba,
					struct hptiop_request *_req)
{
	struct hpt_iop_request_header *reqhdr = _req->req_virt;

	reqhdr->context = cpu_to_le32(IOPMU_QUEUE_ADDR_HOST_BIT |
							(u32)_req->index);
	reqhdr->context_hi32 = 0;

	if (hba->iopintf_v2) {
		u32 size, size_bits;

		size = le32_to_cpu(reqhdr->size);
		if (size < 256)
			size_bits = IOPMU_QUEUE_REQUEST_SIZE_BIT;
		else if (size < 512)
			size_bits = IOPMU_QUEUE_ADDR_HOST_BIT;
		else
			size_bits = IOPMU_QUEUE_REQUEST_SIZE_BIT |
						IOPMU_QUEUE_ADDR_HOST_BIT;
		writel(_req->req_shifted_phy | size_bits,
			&hba->u.itl.iop->inbound_queue);
	} else
		writel(_req->req_shifted_phy | IOPMU_QUEUE_ADDR_HOST_BIT,
					&hba->u.itl.iop->inbound_queue);
}

static void hptiop_post_req_mv(struct hptiop_hba *hba,
					struct hptiop_request *_req)
{
	struct hpt_iop_request_header *reqhdr = _req->req_virt;
	u32 size, size_bit;

	reqhdr->context = cpu_to_le32(_req->index<<8 |
					IOP_REQUEST_TYPE_SCSI_COMMAND<<5);
	reqhdr->context_hi32 = 0;
	size = le32_to_cpu(reqhdr->size);

	if (size <= 256)
		size_bit = 0;
	else if (size <= 256*2)
		size_bit = 1;
	else if (size <= 256*3)
		size_bit = 2;
	else
		size_bit = 3;

	mv_inbound_write((_req->req_shifted_phy << 5) |
		MVIOP_MU_QUEUE_ADDR_HOST_BIT | size_bit, hba);
}

static void hptiop_post_req_mvfrey(struct hptiop_hba *hba,
					struct hptiop_request *_req)
{
	struct hpt_iop_request_header *reqhdr = _req->req_virt;
	u32 index;

	reqhdr->flags |= cpu_to_le32(IOP_REQUEST_FLAG_OUTPUT_CONTEXT |
			IOP_REQUEST_FLAG_ADDR_BITS |
			((_req->req_shifted_phy >> 11) & 0xffff0000));
	reqhdr->context = cpu_to_le32(IOPMU_QUEUE_ADDR_HOST_BIT |
			(_req->index << 4) | reqhdr->type);
	reqhdr->context_hi32 = cpu_to_le32((_req->req_shifted_phy << 5) &
			0xffffffff);

	hba->u.mvfrey.inlist_wptr++;
	index = hba->u.mvfrey.inlist_wptr & 0x3fff;

	if (index == hba->u.mvfrey.list_count) {
		index = 0;
		hba->u.mvfrey.inlist_wptr &= ~0x3fff;
		hba->u.mvfrey.inlist_wptr ^= CL_POINTER_TOGGLE;
	}

	hba->u.mvfrey.inlist[index].addr =
			(dma_addr_t)_req->req_shifted_phy << 5;
	hba->u.mvfrey.inlist[index].intrfc_len = (reqhdr->size + 3) / 4;
	writel(hba->u.mvfrey.inlist_wptr,
		&(hba->u.mvfrey.mu->inbound_write_ptr));
	readl(&(hba->u.mvfrey.mu->inbound_write_ptr));
}

static int hptiop_reset_comm_itl(struct hptiop_hba *hba)
{
	return 0;
}

static int hptiop_reset_comm_mv(struct hptiop_hba *hba)
{
	return 0;
}

static int hptiop_reset_comm_mvfrey(struct hptiop_hba *hba)
{
	u32 list_count = hba->u.mvfrey.list_count;

	if (iop_send_sync_msg(hba, IOPMU_INBOUND_MSG0_RESET_COMM, 3000))
		return -1;

	/* wait 100ms for MCU ready */
	msleep(100);

	writel(cpu_to_le32(hba->u.mvfrey.inlist_phy & 0xffffffff),
			&(hba->u.mvfrey.mu->inbound_base));
	writel(cpu_to_le32((hba->u.mvfrey.inlist_phy >> 16) >> 16),
			&(hba->u.mvfrey.mu->inbound_base_high));

	writel(cpu_to_le32(hba->u.mvfrey.outlist_phy & 0xffffffff),
			&(hba->u.mvfrey.mu->outbound_base));
	writel(cpu_to_le32((hba->u.mvfrey.outlist_phy >> 16) >> 16),
			&(hba->u.mvfrey.mu->outbound_base_high));

	writel(cpu_to_le32(hba->u.mvfrey.outlist_cptr_phy & 0xffffffff),
			&(hba->u.mvfrey.mu->outbound_shadow_base));
	writel(cpu_to_le32((hba->u.mvfrey.outlist_cptr_phy >> 16) >> 16),
			&(hba->u.mvfrey.mu->outbound_shadow_base_high));

	hba->u.mvfrey.inlist_wptr = (list_count - 1) | CL_POINTER_TOGGLE;
	*hba->u.mvfrey.outlist_cptr = (list_count - 1) | CL_POINTER_TOGGLE;
	hba->u.mvfrey.outlist_rptr = list_count - 1;
	return 0;
}

static int hptiop_queuecommand_lck(struct scsi_cmnd *scp)
{
	struct Scsi_Host *host = scp->device->host;
	struct hptiop_hba *hba = (struct hptiop_hba *)host->hostdata;
	struct hpt_iop_request_scsi_command *req;
	int sg_count = 0;
	struct hptiop_request *_req;

	_req = get_req(hba);
	if (_req == NULL) {
		dprintk("hptiop_queuecmd : no free req\n");
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	_req->scp = scp;

	dprintk("hptiop_queuecmd(scp=%p) %d/%d/%d/%llu cdb=(%08x-%08x-%08x-%08x) "
			"req_index=%d, req=%p\n",
			scp,
			host->host_no, scp->device->channel,
			scp->device->id, scp->device->lun,
			cpu_to_be32(((u32 *)scp->cmnd)[0]),
			cpu_to_be32(((u32 *)scp->cmnd)[1]),
			cpu_to_be32(((u32 *)scp->cmnd)[2]),
			cpu_to_be32(((u32 *)scp->cmnd)[3]),
			_req->index, _req->req_virt);

	scp->result = 0;

	if (scp->device->channel ||
			(scp->device->id > hba->max_devices) ||
			((scp->device->id == (hba->max_devices-1)) && scp->device->lun)) {
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
	req->dataxfer_length = cpu_to_le32(scsi_bufflen(scp));
	req->channel = scp->device->channel;
	req->target = scp->device->id;
	req->lun = scp->device->lun;
	req->header.size = cpu_to_le32(struct_size(req, sg_list, sg_count));

	memcpy(req->cdb, scp->cmnd, sizeof(req->cdb));
	hba->ops->post_req(hba, _req);
	return 0;

cmd_done:
	dprintk("scsi_done(scp=%p)\n", scp);
	scsi_done(scp);
	return 0;
}

static DEF_SCSI_QCMD(hptiop_queuecommand)

static const char *hptiop_info(struct Scsi_Host *host)
{
	return driver_name_long;
}

static int hptiop_reset_hba(struct hptiop_hba *hba)
{
	if (atomic_xchg(&hba->resetting, 1) == 0) {
		atomic_inc(&hba->reset_count);
		hba->ops->post_msg(hba, IOPMU_INBOUND_MSG0_RESET);
	}

	wait_event_timeout(hba->reset_wq,
			atomic_read(&hba->resetting) == 0, 60 * HZ);

	if (atomic_read(&hba->resetting)) {
		/* IOP is in unknown state, abort reset */
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
	struct hptiop_hba * hba = (struct hptiop_hba *)scp->device->host->hostdata;

	printk(KERN_WARNING "hptiop_reset(%d/%d/%d)\n",
	       scp->device->host->host_no, -1, -1);

	return hptiop_reset_hba(hba)? FAILED : SUCCESS;
}

static int hptiop_adjust_disk_queue_depth(struct scsi_device *sdev,
					  int queue_depth)
{
	struct hptiop_hba *hba = (struct hptiop_hba *)sdev->host->hostdata;

	if (queue_depth > hba->max_requests)
		queue_depth = hba->max_requests;
	return scsi_change_queue_depth(sdev, queue_depth);
}

static ssize_t hptiop_show_version(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", driver_ver);
}

static ssize_t hptiop_show_fw_version(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *host = class_to_shost(dev);
	struct hptiop_hba *hba = (struct hptiop_hba *)host->hostdata;

	return snprintf(buf, PAGE_SIZE, "%d.%d.%d.%d\n",
				hba->firmware_version >> 24,
				(hba->firmware_version >> 16) & 0xff,
				(hba->firmware_version >> 8) & 0xff,
				hba->firmware_version & 0xff);
}

static struct device_attribute hptiop_attr_version = {
	.attr = {
		.name = "driver-version",
		.mode = S_IRUGO,
	},
	.show = hptiop_show_version,
};

static struct device_attribute hptiop_attr_fw_version = {
	.attr = {
		.name = "firmware-version",
		.mode = S_IRUGO,
	},
	.show = hptiop_show_fw_version,
};

static struct attribute *hptiop_host_attrs[] = {
	&hptiop_attr_version.attr,
	&hptiop_attr_fw_version.attr,
	NULL
};

ATTRIBUTE_GROUPS(hptiop_host);

static int hptiop_slave_config(struct scsi_device *sdev)
{
	if (sdev->type == TYPE_TAPE)
		blk_queue_max_hw_sectors(sdev->request_queue, 8192);

	return 0;
}

static const struct scsi_host_template driver_template = {
	.module                     = THIS_MODULE,
	.name                       = driver_name,
	.queuecommand               = hptiop_queuecommand,
	.eh_host_reset_handler      = hptiop_reset,
	.info                       = hptiop_info,
	.emulated                   = 0,
	.proc_name                  = driver_name,
	.shost_groups		    = hptiop_host_groups,
	.slave_configure            = hptiop_slave_config,
	.this_id                    = -1,
	.change_queue_depth         = hptiop_adjust_disk_queue_depth,
	.cmd_size		    = sizeof(struct hpt_cmd_priv),
};

static int hptiop_internal_memalloc_itl(struct hptiop_hba *hba)
{
	return 0;
}

static int hptiop_internal_memalloc_mv(struct hptiop_hba *hba)
{
	hba->u.mv.internal_req = dma_alloc_coherent(&hba->pcidev->dev,
			0x800, &hba->u.mv.internal_req_phy, GFP_KERNEL);
	if (hba->u.mv.internal_req)
		return 0;
	else
		return -1;
}

static int hptiop_internal_memalloc_mvfrey(struct hptiop_hba *hba)
{
	u32 list_count = readl(&hba->u.mvfrey.mu->inbound_conf_ctl);
	char *p;
	dma_addr_t phy;

	BUG_ON(hba->max_request_size == 0);

	if (list_count == 0) {
		BUG_ON(1);
		return -1;
	}

	list_count >>= 16;

	hba->u.mvfrey.list_count = list_count;
	hba->u.mvfrey.internal_mem_size = 0x800 +
			list_count * sizeof(struct mvfrey_inlist_entry) +
			list_count * sizeof(struct mvfrey_outlist_entry) +
			sizeof(int);

	p = dma_alloc_coherent(&hba->pcidev->dev,
			hba->u.mvfrey.internal_mem_size, &phy, GFP_KERNEL);
	if (!p)
		return -1;

	hba->u.mvfrey.internal_req.req_virt = p;
	hba->u.mvfrey.internal_req.req_shifted_phy = phy >> 5;
	hba->u.mvfrey.internal_req.scp = NULL;
	hba->u.mvfrey.internal_req.next = NULL;

	p += 0x800;
	phy += 0x800;

	hba->u.mvfrey.inlist = (struct mvfrey_inlist_entry *)p;
	hba->u.mvfrey.inlist_phy = phy;

	p += list_count * sizeof(struct mvfrey_inlist_entry);
	phy += list_count * sizeof(struct mvfrey_inlist_entry);

	hba->u.mvfrey.outlist = (struct mvfrey_outlist_entry *)p;
	hba->u.mvfrey.outlist_phy = phy;

	p += list_count * sizeof(struct mvfrey_outlist_entry);
	phy += list_count * sizeof(struct mvfrey_outlist_entry);

	hba->u.mvfrey.outlist_cptr = (__le32 *)p;
	hba->u.mvfrey.outlist_cptr_phy = phy;

	return 0;
}

static int hptiop_internal_memfree_itl(struct hptiop_hba *hba)
{
	return 0;
}

static int hptiop_internal_memfree_mv(struct hptiop_hba *hba)
{
	if (hba->u.mv.internal_req) {
		dma_free_coherent(&hba->pcidev->dev, 0x800,
			hba->u.mv.internal_req, hba->u.mv.internal_req_phy);
		return 0;
	} else
		return -1;
}

static int hptiop_internal_memfree_mvfrey(struct hptiop_hba *hba)
{
	if (hba->u.mvfrey.internal_req.req_virt) {
		dma_free_coherent(&hba->pcidev->dev,
			hba->u.mvfrey.internal_mem_size,
			hba->u.mvfrey.internal_req.req_virt,
			(dma_addr_t)
			hba->u.mvfrey.internal_req.req_shifted_phy << 5);
		return 0;
	} else
		return -1;
}

static int hptiop_probe(struct pci_dev *pcidev, const struct pci_device_id *id)
{
	struct Scsi_Host *host = NULL;
	struct hptiop_hba *hba;
	struct hptiop_adapter_ops *iop_ops;
	struct hpt_iop_request_get_config iop_config;
	struct hpt_iop_request_set_config set_config;
	dma_addr_t start_phy;
	void *start_virt;
	u32 offset, i, req_size;
	int rc;

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
	iop_ops = (struct hptiop_adapter_ops *)id->driver_data;
	rc = dma_set_mask(&pcidev->dev,
			  DMA_BIT_MASK(iop_ops->hw_dma_bit_mask));
	if (rc)
		rc = dma_set_mask(&pcidev->dev, DMA_BIT_MASK(32));

	if (rc) {
		printk(KERN_ERR "hptiop: fail to set dma_mask\n");
		goto disable_pci_device;
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
	memset(hba, 0, sizeof(struct hptiop_hba));

	hba->ops = iop_ops;
	hba->pcidev = pcidev;
	hba->host = host;
	hba->initialized = 0;
	hba->iopintf_v2 = 0;

	atomic_set(&hba->resetting, 0);
	atomic_set(&hba->reset_count, 0);

	init_waitqueue_head(&hba->reset_wq);
	init_waitqueue_head(&hba->ioctl_wq);

	host->max_lun = 128;
	host->max_channel = 0;
	host->io_port = 0;
	host->n_io_port = 0;
	host->irq = pcidev->irq;

	if (hba->ops->map_pci_bar(hba))
		goto free_scsi_host;

	if (hba->ops->iop_wait_ready(hba, 20000)) {
		printk(KERN_ERR "scsi%d: firmware not ready\n",
				hba->host->host_no);
		goto unmap_pci_bar;
	}

	if (hba->ops->family == MV_BASED_IOP) {
		if (hba->ops->internal_memalloc(hba)) {
			printk(KERN_ERR "scsi%d: internal_memalloc failed\n",
				hba->host->host_no);
			goto unmap_pci_bar;
		}
	}

	if (hba->ops->get_config(hba, &iop_config)) {
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

	if (hba->ops->family == MVFREY_BASED_IOP) {
		if (hba->ops->internal_memalloc(hba)) {
			printk(KERN_ERR "scsi%d: internal_memalloc failed\n",
				hba->host->host_no);
			goto unmap_pci_bar;
		}
		if (hba->ops->reset_comm(hba)) {
			printk(KERN_ERR "scsi%d: reset comm failed\n",
					hba->host->host_no);
			goto unmap_pci_bar;
		}
	}

	if (hba->firmware_version > 0x01020000 ||
			hba->interface_version > 0x01020000)
		hba->iopintf_v2 = 1;

	host->max_sectors = le32_to_cpu(iop_config.data_transfer_length) >> 9;
	host->max_id = le32_to_cpu(iop_config.max_devices);
	host->sg_tablesize = le32_to_cpu(iop_config.max_sg_count);
	host->can_queue = le32_to_cpu(iop_config.max_requests);
	host->cmd_per_lun = le32_to_cpu(iop_config.max_requests);
	host->max_cmd_len = 16;

	req_size = struct_size((struct hpt_iop_request_scsi_command *)0,
			       sg_list, hba->max_sg_descriptors);
	if ((req_size & 0x1f) != 0)
		req_size = (req_size + 0x1f) & ~0x1f;

	memset(&set_config, 0, sizeof(struct hpt_iop_request_set_config));
	set_config.iop_id = cpu_to_le32(host->host_no);
	set_config.vbus_id = cpu_to_le16(host->host_no);
	set_config.max_host_request_size = cpu_to_le16(req_size);

	if (hba->ops->set_config(hba, &set_config)) {
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
	hba->req_list = NULL;

	for (i = 0; i < hba->max_requests; i++) {
		start_virt = dma_alloc_coherent(&pcidev->dev,
					hba->req_size + 0x20,
					&start_phy, GFP_KERNEL);

		if (!start_virt) {
			printk(KERN_ERR "scsi%d: fail to alloc request mem\n",
						hba->host->host_no);
			goto free_request_mem;
		}

		hba->dma_coherent[i] = start_virt;
		hba->dma_coherent_handle[i] = start_phy;

		if ((start_phy & 0x1f) != 0) {
			offset = ((start_phy + 0x1f) & ~0x1f) - start_phy;
			start_phy += offset;
			start_virt += offset;
		}

		hba->reqs[i].next = NULL;
		hba->reqs[i].req_virt = start_virt;
		hba->reqs[i].req_shifted_phy = start_phy >> 5;
		hba->reqs[i].index = i;
		free_req(hba, &hba->reqs[i]);
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
	for (i = 0; i < hba->max_requests; i++) {
		if (hba->dma_coherent[i] && hba->dma_coherent_handle[i])
			dma_free_coherent(&hba->pcidev->dev,
					hba->req_size + 0x20,
					hba->dma_coherent[i],
					hba->dma_coherent_handle[i]);
		else
			break;
	}

	free_irq(hba->pcidev->irq, hba);

unmap_pci_bar:
	hba->ops->internal_memfree(hba);

	hba->ops->unmap_pci_bar(hba);

free_scsi_host:
	scsi_host_put(host);

free_pci_regions:
	pci_release_regions(pcidev);

disable_pci_device:
	pci_disable_device(pcidev);

	dprintk("scsi%d: hptiop_probe fail\n", host ? host->host_no : 0);
	return -ENODEV;
}

static void hptiop_shutdown(struct pci_dev *pcidev)
{
	struct Scsi_Host *host = pci_get_drvdata(pcidev);
	struct hptiop_hba *hba = (struct hptiop_hba *)host->hostdata;

	dprintk("hptiop_shutdown(%p)\n", hba);

	/* stop the iop */
	if (iop_send_sync_msg(hba, IOPMU_INBOUND_MSG0_SHUTDOWN, 60000))
		printk(KERN_ERR "scsi%d: shutdown the iop timeout\n",
					hba->host->host_no);

	/* disable all outbound interrupts */
	hba->ops->disable_intr(hba);
}

static void hptiop_disable_intr_itl(struct hptiop_hba *hba)
{
	u32 int_mask;

	int_mask = readl(&hba->u.itl.iop->outbound_intmask);
	writel(int_mask |
		IOPMU_OUTBOUND_INT_MSG0 | IOPMU_OUTBOUND_INT_POSTQUEUE,
		&hba->u.itl.iop->outbound_intmask);
	readl(&hba->u.itl.iop->outbound_intmask);
}

static void hptiop_disable_intr_mv(struct hptiop_hba *hba)
{
	writel(0, &hba->u.mv.regs->outbound_intmask);
	readl(&hba->u.mv.regs->outbound_intmask);
}

static void hptiop_disable_intr_mvfrey(struct hptiop_hba *hba)
{
	writel(0, &(hba->u.mvfrey.mu->f0_doorbell_enable));
	readl(&(hba->u.mvfrey.mu->f0_doorbell_enable));
	writel(0, &(hba->u.mvfrey.mu->isr_enable));
	readl(&(hba->u.mvfrey.mu->isr_enable));
	writel(0, &(hba->u.mvfrey.mu->pcie_f0_int_enable));
	readl(&(hba->u.mvfrey.mu->pcie_f0_int_enable));
}

static void hptiop_remove(struct pci_dev *pcidev)
{
	struct Scsi_Host *host = pci_get_drvdata(pcidev);
	struct hptiop_hba *hba = (struct hptiop_hba *)host->hostdata;
	u32 i;

	dprintk("scsi%d: hptiop_remove\n", hba->host->host_no);

	scsi_remove_host(host);

	hptiop_shutdown(pcidev);

	free_irq(hba->pcidev->irq, hba);

	for (i = 0; i < hba->max_requests; i++) {
		if (hba->dma_coherent[i] && hba->dma_coherent_handle[i])
			dma_free_coherent(&hba->pcidev->dev,
					hba->req_size + 0x20,
					hba->dma_coherent[i],
					hba->dma_coherent_handle[i]);
		else
			break;
	}

	hba->ops->internal_memfree(hba);

	hba->ops->unmap_pci_bar(hba);

	pci_release_regions(hba->pcidev);
	pci_set_drvdata(hba->pcidev, NULL);
	pci_disable_device(hba->pcidev);

	scsi_host_put(host);
}

static struct hptiop_adapter_ops hptiop_itl_ops = {
	.family            = INTEL_BASED_IOP,
	.iop_wait_ready    = iop_wait_ready_itl,
	.internal_memalloc = hptiop_internal_memalloc_itl,
	.internal_memfree  = hptiop_internal_memfree_itl,
	.map_pci_bar       = hptiop_map_pci_bar_itl,
	.unmap_pci_bar     = hptiop_unmap_pci_bar_itl,
	.enable_intr       = hptiop_enable_intr_itl,
	.disable_intr      = hptiop_disable_intr_itl,
	.get_config        = iop_get_config_itl,
	.set_config        = iop_set_config_itl,
	.iop_intr          = iop_intr_itl,
	.post_msg          = hptiop_post_msg_itl,
	.post_req          = hptiop_post_req_itl,
	.hw_dma_bit_mask   = 64,
	.reset_comm        = hptiop_reset_comm_itl,
	.host_phy_flag     = cpu_to_le64(0),
};

static struct hptiop_adapter_ops hptiop_mv_ops = {
	.family            = MV_BASED_IOP,
	.iop_wait_ready    = iop_wait_ready_mv,
	.internal_memalloc = hptiop_internal_memalloc_mv,
	.internal_memfree  = hptiop_internal_memfree_mv,
	.map_pci_bar       = hptiop_map_pci_bar_mv,
	.unmap_pci_bar     = hptiop_unmap_pci_bar_mv,
	.enable_intr       = hptiop_enable_intr_mv,
	.disable_intr      = hptiop_disable_intr_mv,
	.get_config        = iop_get_config_mv,
	.set_config        = iop_set_config_mv,
	.iop_intr          = iop_intr_mv,
	.post_msg          = hptiop_post_msg_mv,
	.post_req          = hptiop_post_req_mv,
	.hw_dma_bit_mask   = 33,
	.reset_comm        = hptiop_reset_comm_mv,
	.host_phy_flag     = cpu_to_le64(0),
};

static struct hptiop_adapter_ops hptiop_mvfrey_ops = {
	.family            = MVFREY_BASED_IOP,
	.iop_wait_ready    = iop_wait_ready_mvfrey,
	.internal_memalloc = hptiop_internal_memalloc_mvfrey,
	.internal_memfree  = hptiop_internal_memfree_mvfrey,
	.map_pci_bar       = hptiop_map_pci_bar_mvfrey,
	.unmap_pci_bar     = hptiop_unmap_pci_bar_mvfrey,
	.enable_intr       = hptiop_enable_intr_mvfrey,
	.disable_intr      = hptiop_disable_intr_mvfrey,
	.get_config        = iop_get_config_mvfrey,
	.set_config        = iop_set_config_mvfrey,
	.iop_intr          = iop_intr_mvfrey,
	.post_msg          = hptiop_post_msg_mvfrey,
	.post_req          = hptiop_post_req_mvfrey,
	.hw_dma_bit_mask   = 64,
	.reset_comm        = hptiop_reset_comm_mvfrey,
	.host_phy_flag     = cpu_to_le64(1),
};

static struct pci_device_id hptiop_id_table[] = {
	{ PCI_VDEVICE(TTI, 0x3220), (kernel_ulong_t)&hptiop_itl_ops },
	{ PCI_VDEVICE(TTI, 0x3320), (kernel_ulong_t)&hptiop_itl_ops },
	{ PCI_VDEVICE(TTI, 0x3410), (kernel_ulong_t)&hptiop_itl_ops },
	{ PCI_VDEVICE(TTI, 0x3510), (kernel_ulong_t)&hptiop_itl_ops },
	{ PCI_VDEVICE(TTI, 0x3511), (kernel_ulong_t)&hptiop_itl_ops },
	{ PCI_VDEVICE(TTI, 0x3520), (kernel_ulong_t)&hptiop_itl_ops },
	{ PCI_VDEVICE(TTI, 0x3521), (kernel_ulong_t)&hptiop_itl_ops },
	{ PCI_VDEVICE(TTI, 0x3522), (kernel_ulong_t)&hptiop_itl_ops },
	{ PCI_VDEVICE(TTI, 0x3530), (kernel_ulong_t)&hptiop_itl_ops },
	{ PCI_VDEVICE(TTI, 0x3540), (kernel_ulong_t)&hptiop_itl_ops },
	{ PCI_VDEVICE(TTI, 0x3560), (kernel_ulong_t)&hptiop_itl_ops },
	{ PCI_VDEVICE(TTI, 0x4210), (kernel_ulong_t)&hptiop_itl_ops },
	{ PCI_VDEVICE(TTI, 0x4211), (kernel_ulong_t)&hptiop_itl_ops },
	{ PCI_VDEVICE(TTI, 0x4310), (kernel_ulong_t)&hptiop_itl_ops },
	{ PCI_VDEVICE(TTI, 0x4311), (kernel_ulong_t)&hptiop_itl_ops },
	{ PCI_VDEVICE(TTI, 0x4320), (kernel_ulong_t)&hptiop_itl_ops },
	{ PCI_VDEVICE(TTI, 0x4321), (kernel_ulong_t)&hptiop_itl_ops },
	{ PCI_VDEVICE(TTI, 0x4322), (kernel_ulong_t)&hptiop_itl_ops },
	{ PCI_VDEVICE(TTI, 0x4400), (kernel_ulong_t)&hptiop_itl_ops },
	{ PCI_VDEVICE(TTI, 0x3120), (kernel_ulong_t)&hptiop_mv_ops },
	{ PCI_VDEVICE(TTI, 0x3122), (kernel_ulong_t)&hptiop_mv_ops },
	{ PCI_VDEVICE(TTI, 0x3020), (kernel_ulong_t)&hptiop_mv_ops },
	{ PCI_VDEVICE(TTI, 0x4520), (kernel_ulong_t)&hptiop_mvfrey_ops },
	{ PCI_VDEVICE(TTI, 0x4522), (kernel_ulong_t)&hptiop_mvfrey_ops },
	{ PCI_VDEVICE(TTI, 0x3610), (kernel_ulong_t)&hptiop_mvfrey_ops },
	{ PCI_VDEVICE(TTI, 0x3611), (kernel_ulong_t)&hptiop_mvfrey_ops },
	{ PCI_VDEVICE(TTI, 0x3620), (kernel_ulong_t)&hptiop_mvfrey_ops },
	{ PCI_VDEVICE(TTI, 0x3622), (kernel_ulong_t)&hptiop_mvfrey_ops },
	{ PCI_VDEVICE(TTI, 0x3640), (kernel_ulong_t)&hptiop_mvfrey_ops },
	{ PCI_VDEVICE(TTI, 0x3660), (kernel_ulong_t)&hptiop_mvfrey_ops },
	{ PCI_VDEVICE(TTI, 0x3680), (kernel_ulong_t)&hptiop_mvfrey_ops },
	{ PCI_VDEVICE(TTI, 0x3690), (kernel_ulong_t)&hptiop_mvfrey_ops },
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

