// SPDX-License-Identifier: GPL-2.0
/*
 * ISM driver for s390.
 *
 * Copyright IBM Corp. 2018
 */
#define KMSG_COMPONENT "ism"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/module.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/err.h>
#include <linux/ctype.h>
#include <linux/processor.h>
#include <net/smc.h>

#include <asm/debug.h>

#include "ism.h"

MODULE_DESCRIPTION("ISM driver for s390");
MODULE_LICENSE("GPL");

#define PCI_DEVICE_ID_IBM_ISM 0x04ED
#define DRV_NAME "ism"

static const struct pci_device_id ism_device_table[] = {
	{ PCI_VDEVICE(IBM, PCI_DEVICE_ID_IBM_ISM), 0 },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, ism_device_table);

static debug_info_t *ism_debug_info;

static int ism_cmd(struct ism_dev *ism, void *cmd)
{
	struct ism_req_hdr *req = cmd;
	struct ism_resp_hdr *resp = cmd;

	__ism_write_cmd(ism, req + 1, sizeof(*req), req->len - sizeof(*req));
	__ism_write_cmd(ism, req, 0, sizeof(*req));

	WRITE_ONCE(resp->ret, ISM_ERROR);

	__ism_read_cmd(ism, resp, 0, sizeof(*resp));
	if (resp->ret) {
		debug_text_event(ism_debug_info, 0, "cmd failure");
		debug_event(ism_debug_info, 0, resp, sizeof(*resp));
		goto out;
	}
	__ism_read_cmd(ism, resp + 1, sizeof(*resp), resp->len - sizeof(*resp));
out:
	return resp->ret;
}

static int ism_cmd_simple(struct ism_dev *ism, u32 cmd_code)
{
	union ism_cmd_simple cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.request.hdr.cmd = cmd_code;
	cmd.request.hdr.len = sizeof(cmd.request);

	return ism_cmd(ism, &cmd);
}

static int query_info(struct ism_dev *ism)
{
	union ism_qi cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.request.hdr.cmd = ISM_QUERY_INFO;
	cmd.request.hdr.len = sizeof(cmd.request);

	if (ism_cmd(ism, &cmd))
		goto out;

	debug_text_event(ism_debug_info, 3, "query info");
	debug_event(ism_debug_info, 3, &cmd.response, sizeof(cmd.response));
out:
	return 0;
}

static int register_sba(struct ism_dev *ism)
{
	union ism_reg_sba cmd;
	dma_addr_t dma_handle;
	struct ism_sba *sba;

	sba = dma_alloc_coherent(&ism->pdev->dev, PAGE_SIZE, &dma_handle,
				 GFP_KERNEL);
	if (!sba)
		return -ENOMEM;

	memset(&cmd, 0, sizeof(cmd));
	cmd.request.hdr.cmd = ISM_REG_SBA;
	cmd.request.hdr.len = sizeof(cmd.request);
	cmd.request.sba = dma_handle;

	if (ism_cmd(ism, &cmd)) {
		dma_free_coherent(&ism->pdev->dev, PAGE_SIZE, sba, dma_handle);
		return -EIO;
	}

	ism->sba = sba;
	ism->sba_dma_addr = dma_handle;

	return 0;
}

static int register_ieq(struct ism_dev *ism)
{
	union ism_reg_ieq cmd;
	dma_addr_t dma_handle;
	struct ism_eq *ieq;

	ieq = dma_alloc_coherent(&ism->pdev->dev, PAGE_SIZE, &dma_handle,
				 GFP_KERNEL);
	if (!ieq)
		return -ENOMEM;

	memset(&cmd, 0, sizeof(cmd));
	cmd.request.hdr.cmd = ISM_REG_IEQ;
	cmd.request.hdr.len = sizeof(cmd.request);
	cmd.request.ieq = dma_handle;
	cmd.request.len = sizeof(*ieq);

	if (ism_cmd(ism, &cmd)) {
		dma_free_coherent(&ism->pdev->dev, PAGE_SIZE, ieq, dma_handle);
		return -EIO;
	}

	ism->ieq = ieq;
	ism->ieq_idx = -1;
	ism->ieq_dma_addr = dma_handle;

	return 0;
}

static int unregister_sba(struct ism_dev *ism)
{
	int ret;

	if (!ism->sba)
		return 0;

	ret = ism_cmd_simple(ism, ISM_UNREG_SBA);
	if (ret && ret != ISM_ERROR)
		return -EIO;

	dma_free_coherent(&ism->pdev->dev, PAGE_SIZE,
			  ism->sba, ism->sba_dma_addr);

	ism->sba = NULL;
	ism->sba_dma_addr = 0;

	return 0;
}

static int unregister_ieq(struct ism_dev *ism)
{
	int ret;

	if (!ism->ieq)
		return 0;

	ret = ism_cmd_simple(ism, ISM_UNREG_IEQ);
	if (ret && ret != ISM_ERROR)
		return -EIO;

	dma_free_coherent(&ism->pdev->dev, PAGE_SIZE,
			  ism->ieq, ism->ieq_dma_addr);

	ism->ieq = NULL;
	ism->ieq_dma_addr = 0;

	return 0;
}

static int ism_read_local_gid(struct ism_dev *ism)
{
	union ism_read_gid cmd;
	int ret;

	memset(&cmd, 0, sizeof(cmd));
	cmd.request.hdr.cmd = ISM_READ_GID;
	cmd.request.hdr.len = sizeof(cmd.request);

	ret = ism_cmd(ism, &cmd);
	if (ret)
		goto out;

	ism->smcd->local_gid = cmd.response.gid;
out:
	return ret;
}

static int ism_query_rgid(struct smcd_dev *smcd, u64 rgid, u32 vid_valid,
			  u32 vid)
{
	struct ism_dev *ism = smcd->priv;
	union ism_query_rgid cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.request.hdr.cmd = ISM_QUERY_RGID;
	cmd.request.hdr.len = sizeof(cmd.request);

	cmd.request.rgid = rgid;
	cmd.request.vlan_valid = vid_valid;
	cmd.request.vlan_id = vid;

	return ism_cmd(ism, &cmd);
}

static void ism_free_dmb(struct ism_dev *ism, struct smcd_dmb *dmb)
{
	clear_bit(dmb->sba_idx, ism->sba_bitmap);
	dma_free_coherent(&ism->pdev->dev, dmb->dmb_len,
			  dmb->cpu_addr, dmb->dma_addr);
}

static int ism_alloc_dmb(struct ism_dev *ism, struct smcd_dmb *dmb)
{
	unsigned long bit;

	if (PAGE_ALIGN(dmb->dmb_len) > dma_get_max_seg_size(&ism->pdev->dev))
		return -EINVAL;

	if (!dmb->sba_idx) {
		bit = find_next_zero_bit(ism->sba_bitmap, ISM_NR_DMBS,
					 ISM_DMB_BIT_OFFSET);
		if (bit == ISM_NR_DMBS)
			return -ENOSPC;

		dmb->sba_idx = bit;
	}
	if (dmb->sba_idx < ISM_DMB_BIT_OFFSET ||
	    test_and_set_bit(dmb->sba_idx, ism->sba_bitmap))
		return -EINVAL;

	dmb->cpu_addr = dma_alloc_coherent(&ism->pdev->dev, dmb->dmb_len,
					   &dmb->dma_addr,
					   GFP_KERNEL | __GFP_NOWARN |
					   __GFP_NOMEMALLOC | __GFP_NORETRY);
	if (!dmb->cpu_addr)
		clear_bit(dmb->sba_idx, ism->sba_bitmap);

	return dmb->cpu_addr ? 0 : -ENOMEM;
}

static int ism_register_dmb(struct smcd_dev *smcd, struct smcd_dmb *dmb)
{
	struct ism_dev *ism = smcd->priv;
	union ism_reg_dmb cmd;
	int ret;

	ret = ism_alloc_dmb(ism, dmb);
	if (ret)
		goto out;

	memset(&cmd, 0, sizeof(cmd));
	cmd.request.hdr.cmd = ISM_REG_DMB;
	cmd.request.hdr.len = sizeof(cmd.request);

	cmd.request.dmb = dmb->dma_addr;
	cmd.request.dmb_len = dmb->dmb_len;
	cmd.request.sba_idx = dmb->sba_idx;
	cmd.request.vlan_valid = dmb->vlan_valid;
	cmd.request.vlan_id = dmb->vlan_id;
	cmd.request.rgid = dmb->rgid;

	ret = ism_cmd(ism, &cmd);
	if (ret) {
		ism_free_dmb(ism, dmb);
		goto out;
	}
	dmb->dmb_tok = cmd.response.dmb_tok;
out:
	return ret;
}

static int ism_unregister_dmb(struct smcd_dev *smcd, struct smcd_dmb *dmb)
{
	struct ism_dev *ism = smcd->priv;
	union ism_unreg_dmb cmd;
	int ret;

	memset(&cmd, 0, sizeof(cmd));
	cmd.request.hdr.cmd = ISM_UNREG_DMB;
	cmd.request.hdr.len = sizeof(cmd.request);

	cmd.request.dmb_tok = dmb->dmb_tok;

	ret = ism_cmd(ism, &cmd);
	if (ret && ret != ISM_ERROR)
		goto out;

	ism_free_dmb(ism, dmb);
out:
	return ret;
}

static int ism_add_vlan_id(struct smcd_dev *smcd, u64 vlan_id)
{
	struct ism_dev *ism = smcd->priv;
	union ism_set_vlan_id cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.request.hdr.cmd = ISM_ADD_VLAN_ID;
	cmd.request.hdr.len = sizeof(cmd.request);

	cmd.request.vlan_id = vlan_id;

	return ism_cmd(ism, &cmd);
}

static int ism_del_vlan_id(struct smcd_dev *smcd, u64 vlan_id)
{
	struct ism_dev *ism = smcd->priv;
	union ism_set_vlan_id cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.request.hdr.cmd = ISM_DEL_VLAN_ID;
	cmd.request.hdr.len = sizeof(cmd.request);

	cmd.request.vlan_id = vlan_id;

	return ism_cmd(ism, &cmd);
}

static int ism_set_vlan_required(struct smcd_dev *smcd)
{
	return ism_cmd_simple(smcd->priv, ISM_SET_VLAN);
}

static int ism_reset_vlan_required(struct smcd_dev *smcd)
{
	return ism_cmd_simple(smcd->priv, ISM_RESET_VLAN);
}

static int ism_signal_ieq(struct smcd_dev *smcd, u64 rgid, u32 trigger_irq,
			  u32 event_code, u64 info)
{
	struct ism_dev *ism = smcd->priv;
	union ism_sig_ieq cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.request.hdr.cmd = ISM_SIGNAL_IEQ;
	cmd.request.hdr.len = sizeof(cmd.request);

	cmd.request.rgid = rgid;
	cmd.request.trigger_irq = trigger_irq;
	cmd.request.event_code = event_code;
	cmd.request.info = info;

	return ism_cmd(ism, &cmd);
}

static unsigned int max_bytes(unsigned int start, unsigned int len,
			      unsigned int boundary)
{
	return min(boundary - (start & (boundary - 1)), len);
}

static int ism_move(struct smcd_dev *smcd, u64 dmb_tok, unsigned int idx,
		    bool sf, unsigned int offset, void *data, unsigned int size)
{
	struct ism_dev *ism = smcd->priv;
	unsigned int bytes;
	u64 dmb_req;
	int ret;

	while (size) {
		bytes = max_bytes(offset, size, PAGE_SIZE);
		dmb_req = ISM_CREATE_REQ(dmb_tok, idx, size == bytes ? sf : 0,
					 offset);

		ret = __ism_move(ism, dmb_req, data, bytes);
		if (ret)
			return ret;

		size -= bytes;
		data += bytes;
		offset += bytes;
	}

	return 0;
}

static struct ism_systemeid SYSTEM_EID = {
	.seid_string = "IBM-SYSZ-ISMSEID00000000",
	.serial_number = "0000",
	.type = "0000",
};

static void ism_create_system_eid(void)
{
	struct cpuid id;
	u16 ident_tail;
	char tmp[5];

	get_cpu_id(&id);
	ident_tail = (u16)(id.ident & ISM_IDENT_MASK);
	snprintf(tmp, 5, "%04X", ident_tail);
	memcpy(&SYSTEM_EID.serial_number, tmp, 4);
	snprintf(tmp, 5, "%04X", id.machine);
	memcpy(&SYSTEM_EID.type, tmp, 4);
}

static u8 *ism_get_system_eid(void)
{
	return SYSTEM_EID.seid_string;
}

static u16 ism_get_chid(struct smcd_dev *smcd)
{
	struct ism_dev *ism = (struct ism_dev *)smcd->priv;

	if (!ism || !ism->pdev)
		return 0;

	return to_zpci(ism->pdev)->pchid;
}

static void ism_handle_event(struct ism_dev *ism)
{
	struct smcd_event *entry;

	while ((ism->ieq_idx + 1) != READ_ONCE(ism->ieq->header.idx)) {
		if (++(ism->ieq_idx) == ARRAY_SIZE(ism->ieq->entry))
			ism->ieq_idx = 0;

		entry = &ism->ieq->entry[ism->ieq_idx];
		debug_event(ism_debug_info, 2, entry, sizeof(*entry));
		smcd_handle_event(ism->smcd, entry);
	}
}

static irqreturn_t ism_handle_irq(int irq, void *data)
{
	struct ism_dev *ism = data;
	unsigned long bit, end;
	unsigned long *bv;
	u16 dmbemask;

	bv = (void *) &ism->sba->dmb_bits[ISM_DMB_WORD_OFFSET];
	end = sizeof(ism->sba->dmb_bits) * BITS_PER_BYTE - ISM_DMB_BIT_OFFSET;

	spin_lock(&ism->lock);
	ism->sba->s = 0;
	barrier();
	for (bit = 0;;) {
		bit = find_next_bit_inv(bv, end, bit);
		if (bit >= end)
			break;

		clear_bit_inv(bit, bv);
		dmbemask = ism->sba->dmbe_mask[bit + ISM_DMB_BIT_OFFSET];
		ism->sba->dmbe_mask[bit + ISM_DMB_BIT_OFFSET] = 0;
		barrier();
		smcd_handle_irq(ism->smcd, bit + ISM_DMB_BIT_OFFSET, dmbemask);
	}

	if (ism->sba->e) {
		ism->sba->e = 0;
		barrier();
		ism_handle_event(ism);
	}
	spin_unlock(&ism->lock);
	return IRQ_HANDLED;
}

static const struct smcd_ops ism_ops = {
	.query_remote_gid = ism_query_rgid,
	.register_dmb = ism_register_dmb,
	.unregister_dmb = ism_unregister_dmb,
	.add_vlan_id = ism_add_vlan_id,
	.del_vlan_id = ism_del_vlan_id,
	.set_vlan_required = ism_set_vlan_required,
	.reset_vlan_required = ism_reset_vlan_required,
	.signal_event = ism_signal_ieq,
	.move_data = ism_move,
	.get_system_eid = ism_get_system_eid,
	.get_chid = ism_get_chid,
};

static int ism_dev_init(struct ism_dev *ism)
{
	struct pci_dev *pdev = ism->pdev;
	int ret;

	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSI);
	if (ret <= 0)
		goto out;

	ret = request_irq(pci_irq_vector(pdev, 0), ism_handle_irq, 0,
			  pci_name(pdev), ism);
	if (ret)
		goto free_vectors;

	ret = register_sba(ism);
	if (ret)
		goto free_irq;

	ret = register_ieq(ism);
	if (ret)
		goto unreg_sba;

	ret = ism_read_local_gid(ism);
	if (ret)
		goto unreg_ieq;

	if (!ism_add_vlan_id(ism->smcd, ISM_RESERVED_VLANID))
		/* hardware is V2 capable */
		ism_create_system_eid();

	ret = smcd_register_dev(ism->smcd);
	if (ret)
		goto unreg_ieq;

	query_info(ism);
	return 0;

unreg_ieq:
	unregister_ieq(ism);
unreg_sba:
	unregister_sba(ism);
free_irq:
	free_irq(pci_irq_vector(pdev, 0), ism);
free_vectors:
	pci_free_irq_vectors(pdev);
out:
	return ret;
}

static int ism_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct ism_dev *ism;
	int ret;

	ism = kzalloc(sizeof(*ism), GFP_KERNEL);
	if (!ism)
		return -ENOMEM;

	spin_lock_init(&ism->lock);
	dev_set_drvdata(&pdev->dev, ism);
	ism->pdev = pdev;

	ret = pci_enable_device_mem(pdev);
	if (ret)
		goto err;

	ret = pci_request_mem_regions(pdev, DRV_NAME);
	if (ret)
		goto err_disable;

	ret = dma_set_mask(&pdev->dev, DMA_BIT_MASK(64));
	if (ret)
		goto err_resource;

	dma_set_seg_boundary(&pdev->dev, SZ_1M - 1);
	dma_set_max_seg_size(&pdev->dev, SZ_1M);
	pci_set_master(pdev);

	ism->smcd = smcd_alloc_dev(&pdev->dev, dev_name(&pdev->dev), &ism_ops,
				   ISM_NR_DMBS);
	if (!ism->smcd) {
		ret = -ENOMEM;
		goto err_resource;
	}

	ism->smcd->priv = ism;
	ret = ism_dev_init(ism);
	if (ret)
		goto err_free;

	return 0;

err_free:
	smcd_free_dev(ism->smcd);
err_resource:
	pci_release_mem_regions(pdev);
err_disable:
	pci_disable_device(pdev);
err:
	kfree(ism);
	dev_set_drvdata(&pdev->dev, NULL);
	return ret;
}

static void ism_dev_exit(struct ism_dev *ism)
{
	struct pci_dev *pdev = ism->pdev;

	smcd_unregister_dev(ism->smcd);
	if (SYSTEM_EID.serial_number[0] != '0' ||
	    SYSTEM_EID.type[0] != '0')
		ism_del_vlan_id(ism->smcd, ISM_RESERVED_VLANID);
	unregister_ieq(ism);
	unregister_sba(ism);
	free_irq(pci_irq_vector(pdev, 0), ism);
	pci_free_irq_vectors(pdev);
}

static void ism_remove(struct pci_dev *pdev)
{
	struct ism_dev *ism = dev_get_drvdata(&pdev->dev);

	ism_dev_exit(ism);

	smcd_free_dev(ism->smcd);
	pci_release_mem_regions(pdev);
	pci_disable_device(pdev);
	dev_set_drvdata(&pdev->dev, NULL);
	kfree(ism);
}

static struct pci_driver ism_driver = {
	.name	  = DRV_NAME,
	.id_table = ism_device_table,
	.probe	  = ism_probe,
	.remove	  = ism_remove,
};

static int __init ism_init(void)
{
	int ret;

	ism_debug_info = debug_register("ism", 2, 1, 16);
	if (!ism_debug_info)
		return -ENODEV;

	debug_register_view(ism_debug_info, &debug_hex_ascii_view);
	ret = pci_register_driver(&ism_driver);
	if (ret)
		debug_unregister(ism_debug_info);

	return ret;
}

static void __exit ism_exit(void)
{
	pci_unregister_driver(&ism_driver);
	debug_unregister(ism_debug_info);
}

module_init(ism_init);
module_exit(ism_exit);
