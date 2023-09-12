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
#include <linux/err.h>
#include <linux/ctype.h>
#include <linux/processor.h>

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
static const struct smcd_ops ism_ops;

#define NO_CLIENT		0xff		/* must be >= MAX_CLIENTS */
static struct ism_client *clients[MAX_CLIENTS];	/* use an array rather than */
						/* a list for fast mapping  */
static u8 max_client;
static DEFINE_MUTEX(clients_lock);
struct ism_dev_list {
	struct list_head list;
	struct mutex mutex; /* protects ism device list */
};

static struct ism_dev_list ism_dev_list = {
	.list = LIST_HEAD_INIT(ism_dev_list.list),
	.mutex = __MUTEX_INITIALIZER(ism_dev_list.mutex),
};

static void ism_setup_forwarding(struct ism_client *client, struct ism_dev *ism)
{
	unsigned long flags;

	spin_lock_irqsave(&ism->lock, flags);
	ism->subs[client->id] = client;
	spin_unlock_irqrestore(&ism->lock, flags);
}

int ism_register_client(struct ism_client *client)
{
	struct ism_dev *ism;
	int i, rc = -ENOSPC;

	mutex_lock(&ism_dev_list.mutex);
	mutex_lock(&clients_lock);
	for (i = 0; i < MAX_CLIENTS; ++i) {
		if (!clients[i]) {
			clients[i] = client;
			client->id = i;
			if (i == max_client)
				max_client++;
			rc = 0;
			break;
		}
	}
	mutex_unlock(&clients_lock);

	if (i < MAX_CLIENTS) {
		/* initialize with all devices that we got so far */
		list_for_each_entry(ism, &ism_dev_list.list, list) {
			ism->priv[i] = NULL;
			client->add(ism);
			ism_setup_forwarding(client, ism);
		}
	}
	mutex_unlock(&ism_dev_list.mutex);

	return rc;
}
EXPORT_SYMBOL_GPL(ism_register_client);

int ism_unregister_client(struct ism_client *client)
{
	struct ism_dev *ism;
	unsigned long flags;
	int rc = 0;

	mutex_lock(&ism_dev_list.mutex);
	list_for_each_entry(ism, &ism_dev_list.list, list) {
		spin_lock_irqsave(&ism->lock, flags);
		/* Stop forwarding IRQs and events */
		ism->subs[client->id] = NULL;
		for (int i = 0; i < ISM_NR_DMBS; ++i) {
			if (ism->sba_client_arr[i] == client->id) {
				WARN(1, "%s: attempt to unregister '%s' with registered dmb(s)\n",
				     __func__, client->name);
				rc = -EBUSY;
				goto err_reg_dmb;
			}
		}
		spin_unlock_irqrestore(&ism->lock, flags);
	}
	mutex_unlock(&ism_dev_list.mutex);

	mutex_lock(&clients_lock);
	clients[client->id] = NULL;
	if (client->id + 1 == max_client)
		max_client--;
	mutex_unlock(&clients_lock);
	return rc;

err_reg_dmb:
	spin_unlock_irqrestore(&ism->lock, flags);
	mutex_unlock(&ism_dev_list.mutex);
	return rc;
}
EXPORT_SYMBOL_GPL(ism_unregister_client);

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

	ism->local_gid = cmd.response.gid;
out:
	return ret;
}

static int ism_query_rgid(struct ism_dev *ism, u64 rgid, u32 vid_valid,
			  u32 vid)
{
	union ism_query_rgid cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.request.hdr.cmd = ISM_QUERY_RGID;
	cmd.request.hdr.len = sizeof(cmd.request);

	cmd.request.rgid = rgid;
	cmd.request.vlan_valid = vid_valid;
	cmd.request.vlan_id = vid;

	return ism_cmd(ism, &cmd);
}

static void ism_free_dmb(struct ism_dev *ism, struct ism_dmb *dmb)
{
	clear_bit(dmb->sba_idx, ism->sba_bitmap);
	dma_free_coherent(&ism->pdev->dev, dmb->dmb_len,
			  dmb->cpu_addr, dmb->dma_addr);
}

static int ism_alloc_dmb(struct ism_dev *ism, struct ism_dmb *dmb)
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

int ism_register_dmb(struct ism_dev *ism, struct ism_dmb *dmb,
		     struct ism_client *client)
{
	union ism_reg_dmb cmd;
	unsigned long flags;
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
	spin_lock_irqsave(&ism->lock, flags);
	ism->sba_client_arr[dmb->sba_idx - ISM_DMB_BIT_OFFSET] = client->id;
	spin_unlock_irqrestore(&ism->lock, flags);
out:
	return ret;
}
EXPORT_SYMBOL_GPL(ism_register_dmb);

int ism_unregister_dmb(struct ism_dev *ism, struct ism_dmb *dmb)
{
	union ism_unreg_dmb cmd;
	unsigned long flags;
	int ret;

	memset(&cmd, 0, sizeof(cmd));
	cmd.request.hdr.cmd = ISM_UNREG_DMB;
	cmd.request.hdr.len = sizeof(cmd.request);

	cmd.request.dmb_tok = dmb->dmb_tok;

	spin_lock_irqsave(&ism->lock, flags);
	ism->sba_client_arr[dmb->sba_idx - ISM_DMB_BIT_OFFSET] = NO_CLIENT;
	spin_unlock_irqrestore(&ism->lock, flags);

	ret = ism_cmd(ism, &cmd);
	if (ret && ret != ISM_ERROR)
		goto out;

	ism_free_dmb(ism, dmb);
out:
	return ret;
}
EXPORT_SYMBOL_GPL(ism_unregister_dmb);

static int ism_add_vlan_id(struct ism_dev *ism, u64 vlan_id)
{
	union ism_set_vlan_id cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.request.hdr.cmd = ISM_ADD_VLAN_ID;
	cmd.request.hdr.len = sizeof(cmd.request);

	cmd.request.vlan_id = vlan_id;

	return ism_cmd(ism, &cmd);
}

static int ism_del_vlan_id(struct ism_dev *ism, u64 vlan_id)
{
	union ism_set_vlan_id cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.request.hdr.cmd = ISM_DEL_VLAN_ID;
	cmd.request.hdr.len = sizeof(cmd.request);

	cmd.request.vlan_id = vlan_id;

	return ism_cmd(ism, &cmd);
}

static int ism_signal_ieq(struct ism_dev *ism, u64 rgid, u32 trigger_irq,
			  u32 event_code, u64 info)
{
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

int ism_move(struct ism_dev *ism, u64 dmb_tok, unsigned int idx, bool sf,
	     unsigned int offset, void *data, unsigned int size)
{
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
EXPORT_SYMBOL_GPL(ism_move);

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

u8 *ism_get_seid(void)
{
	return SYSTEM_EID.seid_string;
}
EXPORT_SYMBOL_GPL(ism_get_seid);

static u16 ism_get_chid(struct ism_dev *ism)
{
	if (!ism || !ism->pdev)
		return 0;

	return to_zpci(ism->pdev)->pchid;
}

static void ism_handle_event(struct ism_dev *ism)
{
	struct ism_event *entry;
	struct ism_client *clt;
	int i;

	while ((ism->ieq_idx + 1) != READ_ONCE(ism->ieq->header.idx)) {
		if (++(ism->ieq_idx) == ARRAY_SIZE(ism->ieq->entry))
			ism->ieq_idx = 0;

		entry = &ism->ieq->entry[ism->ieq_idx];
		debug_event(ism_debug_info, 2, entry, sizeof(*entry));
		for (i = 0; i < max_client; ++i) {
			clt = ism->subs[i];
			if (clt)
				clt->handle_event(ism, entry);
		}
	}
}

static irqreturn_t ism_handle_irq(int irq, void *data)
{
	struct ism_dev *ism = data;
	unsigned long bit, end;
	unsigned long *bv;
	u16 dmbemask;
	u8 client_id;

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
		client_id = ism->sba_client_arr[bit];
		if (unlikely(client_id == NO_CLIENT || !ism->subs[client_id]))
			continue;
		ism->subs[client_id]->handle_irq(ism, bit + ISM_DMB_BIT_OFFSET, dmbemask);
	}

	if (ism->sba->e) {
		ism->sba->e = 0;
		barrier();
		ism_handle_event(ism);
	}
	spin_unlock(&ism->lock);
	return IRQ_HANDLED;
}

static u64 ism_get_local_gid(struct ism_dev *ism)
{
	return ism->local_gid;
}

static int ism_dev_init(struct ism_dev *ism)
{
	struct pci_dev *pdev = ism->pdev;
	int i, ret;

	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSI);
	if (ret <= 0)
		goto out;

	ism->sba_client_arr = kzalloc(ISM_NR_DMBS, GFP_KERNEL);
	if (!ism->sba_client_arr)
		goto free_vectors;
	memset(ism->sba_client_arr, NO_CLIENT, ISM_NR_DMBS);

	ret = request_irq(pci_irq_vector(pdev, 0), ism_handle_irq, 0,
			  pci_name(pdev), ism);
	if (ret)
		goto free_client_arr;

	ret = register_sba(ism);
	if (ret)
		goto free_irq;

	ret = register_ieq(ism);
	if (ret)
		goto unreg_sba;

	ret = ism_read_local_gid(ism);
	if (ret)
		goto unreg_ieq;

	if (!ism_add_vlan_id(ism, ISM_RESERVED_VLANID))
		/* hardware is V2 capable */
		ism_create_system_eid();

	mutex_lock(&ism_dev_list.mutex);
	mutex_lock(&clients_lock);
	for (i = 0; i < max_client; ++i) {
		if (clients[i]) {
			clients[i]->add(ism);
			ism_setup_forwarding(clients[i], ism);
		}
	}
	mutex_unlock(&clients_lock);

	list_add(&ism->list, &ism_dev_list.list);
	mutex_unlock(&ism_dev_list.mutex);

	query_info(ism);
	return 0;

unreg_ieq:
	unregister_ieq(ism);
unreg_sba:
	unregister_sba(ism);
free_irq:
	free_irq(pci_irq_vector(pdev, 0), ism);
free_client_arr:
	kfree(ism->sba_client_arr);
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
	ism->dev.parent = &pdev->dev;
	device_initialize(&ism->dev);
	dev_set_name(&ism->dev, dev_name(&pdev->dev));
	ret = device_add(&ism->dev);
	if (ret)
		goto err_dev;

	ret = pci_enable_device_mem(pdev);
	if (ret)
		goto err;

	ret = pci_request_mem_regions(pdev, DRV_NAME);
	if (ret)
		goto err_disable;

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (ret)
		goto err_resource;

	dma_set_seg_boundary(&pdev->dev, SZ_1M - 1);
	dma_set_max_seg_size(&pdev->dev, SZ_1M);
	pci_set_master(pdev);

	ret = ism_dev_init(ism);
	if (ret)
		goto err_resource;

	return 0;

err_resource:
	pci_release_mem_regions(pdev);
err_disable:
	pci_disable_device(pdev);
err:
	device_del(&ism->dev);
err_dev:
	dev_set_drvdata(&pdev->dev, NULL);
	kfree(ism);

	return ret;
}

static void ism_dev_exit(struct ism_dev *ism)
{
	struct pci_dev *pdev = ism->pdev;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&ism->lock, flags);
	for (i = 0; i < max_client; ++i)
		ism->subs[i] = NULL;
	spin_unlock_irqrestore(&ism->lock, flags);

	mutex_lock(&ism_dev_list.mutex);
	mutex_lock(&clients_lock);
	for (i = 0; i < max_client; ++i) {
		if (clients[i])
			clients[i]->remove(ism);
	}
	mutex_unlock(&clients_lock);

	if (SYSTEM_EID.serial_number[0] != '0' ||
	    SYSTEM_EID.type[0] != '0')
		ism_del_vlan_id(ism, ISM_RESERVED_VLANID);
	unregister_ieq(ism);
	unregister_sba(ism);
	free_irq(pci_irq_vector(pdev, 0), ism);
	kfree(ism->sba_client_arr);
	pci_free_irq_vectors(pdev);
	list_del_init(&ism->list);
	mutex_unlock(&ism_dev_list.mutex);
}

static void ism_remove(struct pci_dev *pdev)
{
	struct ism_dev *ism = dev_get_drvdata(&pdev->dev);

	ism_dev_exit(ism);

	pci_release_mem_regions(pdev);
	pci_disable_device(pdev);
	device_del(&ism->dev);
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

	memset(clients, 0, sizeof(clients));
	max_client = 0;
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

/*************************** SMC-D Implementation *****************************/

#if IS_ENABLED(CONFIG_SMC)
static int smcd_query_rgid(struct smcd_dev *smcd, u64 rgid, u32 vid_valid,
			   u32 vid)
{
	return ism_query_rgid(smcd->priv, rgid, vid_valid, vid);
}

static int smcd_register_dmb(struct smcd_dev *smcd, struct smcd_dmb *dmb,
			     struct ism_client *client)
{
	return ism_register_dmb(smcd->priv, (struct ism_dmb *)dmb, client);
}

static int smcd_unregister_dmb(struct smcd_dev *smcd, struct smcd_dmb *dmb)
{
	return ism_unregister_dmb(smcd->priv, (struct ism_dmb *)dmb);
}

static int smcd_add_vlan_id(struct smcd_dev *smcd, u64 vlan_id)
{
	return ism_add_vlan_id(smcd->priv, vlan_id);
}

static int smcd_del_vlan_id(struct smcd_dev *smcd, u64 vlan_id)
{
	return ism_del_vlan_id(smcd->priv, vlan_id);
}

static int smcd_set_vlan_required(struct smcd_dev *smcd)
{
	return ism_cmd_simple(smcd->priv, ISM_SET_VLAN);
}

static int smcd_reset_vlan_required(struct smcd_dev *smcd)
{
	return ism_cmd_simple(smcd->priv, ISM_RESET_VLAN);
}

static int smcd_signal_ieq(struct smcd_dev *smcd, u64 rgid, u32 trigger_irq,
			   u32 event_code, u64 info)
{
	return ism_signal_ieq(smcd->priv, rgid, trigger_irq, event_code, info);
}

static int smcd_move(struct smcd_dev *smcd, u64 dmb_tok, unsigned int idx,
		     bool sf, unsigned int offset, void *data,
		     unsigned int size)
{
	return ism_move(smcd->priv, dmb_tok, idx, sf, offset, data, size);
}

static int smcd_supports_v2(void)
{
	return SYSTEM_EID.serial_number[0] != '0' ||
		SYSTEM_EID.type[0] != '0';
}

static u64 smcd_get_local_gid(struct smcd_dev *smcd)
{
	return ism_get_local_gid(smcd->priv);
}

static u16 smcd_get_chid(struct smcd_dev *smcd)
{
	return ism_get_chid(smcd->priv);
}

static inline struct device *smcd_get_dev(struct smcd_dev *dev)
{
	struct ism_dev *ism = dev->priv;

	return &ism->dev;
}

static const struct smcd_ops ism_ops = {
	.query_remote_gid = smcd_query_rgid,
	.register_dmb = smcd_register_dmb,
	.unregister_dmb = smcd_unregister_dmb,
	.add_vlan_id = smcd_add_vlan_id,
	.del_vlan_id = smcd_del_vlan_id,
	.set_vlan_required = smcd_set_vlan_required,
	.reset_vlan_required = smcd_reset_vlan_required,
	.signal_event = smcd_signal_ieq,
	.move_data = smcd_move,
	.supports_v2 = smcd_supports_v2,
	.get_system_eid = ism_get_seid,
	.get_local_gid = smcd_get_local_gid,
	.get_chid = smcd_get_chid,
	.get_dev = smcd_get_dev,
};

const struct smcd_ops *ism_get_smcd_ops(void)
{
	return &ism_ops;
}
EXPORT_SYMBOL_GPL(ism_get_smcd_ops);
#endif
