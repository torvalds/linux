// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2025 Broadcom.

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/bitops.h>
#include <linux/irq.h>
#include <asm/byteorder.h>
#include <linux/bitmap.h>
#include <linux/auxiliary_bus.h>
#include <linux/bnge/hsi.h>

#include "bnge.h"
#include "bnge_hwrm.h"
#include "bnge_auxr.h"

static DEFINE_IDA(bnge_aux_dev_ids);

static void bnge_fill_msix_vecs(struct bnge_dev *bd,
				struct bnge_msix_info *info)
{
	struct bnge_auxr_dev *auxr_dev = bd->auxr_dev;
	int num_msix, i;

	if (!auxr_dev->auxr_info->msix_requested) {
		dev_warn(bd->dev, "Requested MSI-X vectors not allocated\n");
		return;
	}
	num_msix = auxr_dev->auxr_info->msix_requested;
	for (i = 0; i < num_msix; i++) {
		info[i].vector = bd->irq_tbl[i].vector;
		info[i].db_offset = bd->db_offset;
		info[i].ring_idx = i;
	}
}

int bnge_register_dev(struct bnge_auxr_dev *auxr_dev,
		      void *handle)
{
	struct bnge_dev *bd = pci_get_drvdata(auxr_dev->pdev);
	struct bnge_auxr_info *auxr_info;
	int rc = 0;

	netdev_lock(bd->netdev);
	mutex_lock(&auxr_dev->auxr_dev_lock);
	if (!bd->irq_tbl) {
		rc = -ENODEV;
		goto exit;
	}

	if (!bnge_aux_has_enough_resources(bd)) {
		rc = -ENOMEM;
		goto exit;
	}

	auxr_info = auxr_dev->auxr_info;
	auxr_info->handle = handle;

	auxr_info->msix_requested = bd->aux_num_msix;

	bnge_fill_msix_vecs(bd, bd->auxr_dev->msix_info);
	auxr_dev->flags |= BNGE_ARDEV_MSIX_ALLOC;

exit:
	mutex_unlock(&auxr_dev->auxr_dev_lock);
	netdev_unlock(bd->netdev);
	return rc;
}
EXPORT_SYMBOL(bnge_register_dev);

void bnge_unregister_dev(struct bnge_auxr_dev *auxr_dev)
{
	struct bnge_dev *bd = pci_get_drvdata(auxr_dev->pdev);
	struct bnge_auxr_info *auxr_info;

	auxr_info = auxr_dev->auxr_info;
	netdev_lock(bd->netdev);
	mutex_lock(&auxr_dev->auxr_dev_lock);
	if (auxr_info->msix_requested)
		auxr_dev->flags &= ~BNGE_ARDEV_MSIX_ALLOC;
	auxr_info->msix_requested = 0;

	mutex_unlock(&auxr_dev->auxr_dev_lock);
	netdev_unlock(bd->netdev);
}
EXPORT_SYMBOL(bnge_unregister_dev);

int bnge_send_msg(struct bnge_auxr_dev *auxr_dev, struct bnge_fw_msg *fw_msg)
{
	struct bnge_dev *bd = pci_get_drvdata(auxr_dev->pdev);
	struct output *resp;
	struct input *req;
	u32 resp_len;
	int rc;

	rc = bnge_hwrm_req_init(bd, req, 0 /* don't care */);
	if (rc)
		return rc;

	rc = bnge_hwrm_req_replace(bd, req, fw_msg->msg, fw_msg->msg_len);
	if (rc)
		goto drop_req;

	bnge_hwrm_req_timeout(bd, req, fw_msg->timeout);
	resp = bnge_hwrm_req_hold(bd, req);
	rc = bnge_hwrm_req_send(bd, req);
	resp_len = le16_to_cpu(resp->resp_len);
	if (resp_len) {
		if (fw_msg->resp_max_len < resp_len)
			resp_len = fw_msg->resp_max_len;

		memcpy(fw_msg->resp, resp, resp_len);
	}
drop_req:
	bnge_hwrm_req_drop(bd, req);
	return rc;
}
EXPORT_SYMBOL(bnge_send_msg);

void bnge_rdma_aux_device_uninit(struct bnge_dev *bd)
{
	struct bnge_auxr_priv *aux_priv;
	struct auxiliary_device *adev;

	/* Skip if no auxiliary device init was done. */
	if (!bd->aux_priv)
		return;

	aux_priv = bd->aux_priv;
	adev = &aux_priv->aux_dev;
	auxiliary_device_uninit(adev);
}

static void bnge_aux_dev_release(struct device *dev)
{
	struct bnge_auxr_priv *aux_priv =
			container_of(dev, struct bnge_auxr_priv, aux_dev.dev);
	struct bnge_dev *bd = pci_get_drvdata(aux_priv->auxr_dev->pdev);

	ida_free(&bnge_aux_dev_ids, aux_priv->id);
	kfree(aux_priv->auxr_dev->auxr_info);
	bd->auxr_dev = NULL;
	kfree(aux_priv->auxr_dev);
	kfree(aux_priv);
	bd->aux_priv = NULL;
}

void bnge_rdma_aux_device_del(struct bnge_dev *bd)
{
	if (!bd->auxr_dev)
		return;

	auxiliary_device_delete(&bd->aux_priv->aux_dev);
}

static void bnge_set_auxr_dev_info(struct bnge_auxr_dev *auxr_dev,
				   struct bnge_dev *bd)
{
	auxr_dev->pdev = bd->pdev;
	auxr_dev->l2_db_size = bd->db_size;
	auxr_dev->l2_db_size_nc = bd->db_size;
	auxr_dev->l2_db_offset = bd->db_offset;
	mutex_init(&auxr_dev->auxr_dev_lock);

	if (bd->flags & BNGE_EN_ROCE_V1)
		auxr_dev->flags |= BNGE_ARDEV_ROCEV1_SUPP;
	if (bd->flags & BNGE_EN_ROCE_V2)
		auxr_dev->flags |= BNGE_ARDEV_ROCEV2_SUPP;

	auxr_dev->chip_num = bd->chip_num;
	auxr_dev->hw_ring_stats_size = bd->hw_ring_stats_size;
	auxr_dev->pf_port_id = bd->pf.port_id;
	auxr_dev->en_state = bd->state;
	auxr_dev->bar0 = bd->bar0;
}

void bnge_rdma_aux_device_add(struct bnge_dev *bd)
{
	struct auxiliary_device *aux_dev;
	int rc;

	if (!bd->auxr_dev)
		return;

	aux_dev = &bd->aux_priv->aux_dev;
	rc = auxiliary_device_add(aux_dev);
	if (rc) {
		dev_warn(bd->dev, "Failed to add auxiliary device for ROCE\n");
		auxiliary_device_uninit(aux_dev);
		bd->flags &= ~BNGE_EN_ROCE;
	}

	bd->auxr_dev->net = bd->netdev;
}

void bnge_rdma_aux_device_init(struct bnge_dev *bd)
{
	struct auxiliary_device *aux_dev;
	struct bnge_auxr_info *auxr_info;
	struct bnge_auxr_priv *aux_priv;
	struct bnge_auxr_dev *auxr_dev;
	int rc;

	if (!bnge_is_roce_en(bd))
		return;

	aux_priv = kzalloc(sizeof(*aux_priv), GFP_KERNEL);
	if (!aux_priv)
		goto exit;

	aux_priv->id = ida_alloc(&bnge_aux_dev_ids, GFP_KERNEL);
	if (aux_priv->id < 0) {
		dev_warn(bd->dev, "ida alloc failed for aux device\n");
		kfree(aux_priv);
		goto exit;
	}

	aux_dev = &aux_priv->aux_dev;
	aux_dev->id = aux_priv->id;
	aux_dev->name = "rdma";
	aux_dev->dev.parent = &bd->pdev->dev;
	aux_dev->dev.release = bnge_aux_dev_release;

	rc = auxiliary_device_init(aux_dev);
	if (rc) {
		ida_free(&bnge_aux_dev_ids, aux_priv->id);
		kfree(aux_priv);
		goto exit;
	}
	bd->aux_priv = aux_priv;

	auxr_dev = kzalloc(sizeof(*auxr_dev), GFP_KERNEL);
	if (!auxr_dev)
		goto aux_dev_uninit;

	aux_priv->auxr_dev = auxr_dev;

	auxr_info = kzalloc(sizeof(*auxr_info), GFP_KERNEL);
	if (!auxr_info)
		goto aux_dev_uninit;

	auxr_dev->auxr_info = auxr_info;
	bd->auxr_dev = auxr_dev;
	bnge_set_auxr_dev_info(auxr_dev, bd);

	return;

aux_dev_uninit:
	auxiliary_device_uninit(aux_dev);
exit:
	bd->flags &= ~BNGE_EN_ROCE;
}
