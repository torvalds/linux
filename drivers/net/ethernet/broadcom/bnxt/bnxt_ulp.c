/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2016-2018 Broadcom Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

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

#include "bnxt_hsi.h"
#include "bnxt.h"
#include "bnxt_hwrm.h"
#include "bnxt_ulp.h"

static DEFINE_IDA(bnxt_aux_dev_ids);

static void bnxt_fill_msix_vecs(struct bnxt *bp, struct bnxt_msix_entry *ent)
{
	struct bnxt_en_dev *edev = bp->edev;
	int num_msix, idx, i;

	if (!edev->ulp_tbl->msix_requested) {
		netdev_warn(bp->dev, "Requested MSI-X vectors insufficient\n");
		return;
	}
	num_msix = edev->ulp_tbl->msix_requested;
	idx = edev->ulp_tbl->msix_base;
	for (i = 0; i < num_msix; i++) {
		ent[i].vector = bp->irq_tbl[idx + i].vector;
		ent[i].ring_idx = idx + i;
		if (bp->flags & BNXT_FLAG_CHIP_P5) {
			ent[i].db_offset = DB_PF_OFFSET_P5;
			if (BNXT_VF(bp))
				ent[i].db_offset = DB_VF_OFFSET_P5;
		} else {
			ent[i].db_offset = (idx + i) * 0x80;
		}
	}
}

int bnxt_register_dev(struct bnxt_en_dev *edev,
		      struct bnxt_ulp_ops *ulp_ops,
		      void *handle)
{
	struct net_device *dev = edev->net;
	struct bnxt *bp = netdev_priv(dev);
	unsigned int max_stat_ctxs;
	struct bnxt_ulp *ulp;

	max_stat_ctxs = bnxt_get_max_func_stat_ctxs(bp);
	if (max_stat_ctxs <= BNXT_MIN_ROCE_STAT_CTXS ||
	    bp->cp_nr_rings == max_stat_ctxs)
		return -ENOMEM;

	ulp = edev->ulp_tbl;
	if (!ulp)
		return -ENOMEM;

	ulp->handle = handle;
	rcu_assign_pointer(ulp->ulp_ops, ulp_ops);

	if (test_bit(BNXT_STATE_OPEN, &bp->state))
		bnxt_hwrm_vnic_cfg(bp, 0);

	bnxt_fill_msix_vecs(bp, bp->edev->msix_entries);
	edev->flags |= BNXT_EN_FLAG_MSIX_REQUESTED;
	return 0;
}
EXPORT_SYMBOL(bnxt_register_dev);

void bnxt_unregister_dev(struct bnxt_en_dev *edev)
{
	struct net_device *dev = edev->net;
	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_ulp *ulp;
	int i = 0;

	ulp = edev->ulp_tbl;
	if (ulp->msix_requested)
		edev->flags &= ~BNXT_EN_FLAG_MSIX_REQUESTED;

	if (ulp->max_async_event_id)
		bnxt_hwrm_func_drv_rgtr(bp, NULL, 0, true);

	RCU_INIT_POINTER(ulp->ulp_ops, NULL);
	synchronize_rcu();
	ulp->max_async_event_id = 0;
	ulp->async_events_bmap = NULL;
	while (atomic_read(&ulp->ref_count) != 0 && i < 10) {
		msleep(100);
		i++;
	}
	return;
}
EXPORT_SYMBOL(bnxt_unregister_dev);

int bnxt_get_ulp_msix_num(struct bnxt *bp)
{
	u32 roce_msix = BNXT_VF(bp) ?
			BNXT_MAX_VF_ROCE_MSIX : BNXT_MAX_ROCE_MSIX;

	return ((bp->flags & BNXT_FLAG_ROCE_CAP) ?
		min_t(u32, roce_msix, num_online_cpus()) : 0);
}

int bnxt_get_ulp_msix_base(struct bnxt *bp)
{
	if (bnxt_ulp_registered(bp->edev)) {
		struct bnxt_en_dev *edev = bp->edev;

		if (edev->ulp_tbl->msix_requested)
			return edev->ulp_tbl->msix_base;
	}
	return 0;
}

int bnxt_get_ulp_stat_ctxs(struct bnxt *bp)
{
	if (bnxt_ulp_registered(bp->edev)) {
		struct bnxt_en_dev *edev = bp->edev;

		if (edev->ulp_tbl->msix_requested)
			return BNXT_MIN_ROCE_STAT_CTXS;
	}

	return 0;
}

int bnxt_send_msg(struct bnxt_en_dev *edev,
			 struct bnxt_fw_msg *fw_msg)
{
	struct net_device *dev = edev->net;
	struct bnxt *bp = netdev_priv(dev);
	struct output *resp;
	struct input *req;
	u32 resp_len;
	int rc;

	if (bp->fw_reset_state)
		return -EBUSY;

	rc = hwrm_req_init(bp, req, 0 /* don't care */);
	if (rc)
		return rc;

	rc = hwrm_req_replace(bp, req, fw_msg->msg, fw_msg->msg_len);
	if (rc)
		return rc;

	hwrm_req_timeout(bp, req, fw_msg->timeout);
	resp = hwrm_req_hold(bp, req);
	rc = hwrm_req_send(bp, req);
	resp_len = le16_to_cpu(resp->resp_len);
	if (resp_len) {
		if (fw_msg->resp_max_len < resp_len)
			resp_len = fw_msg->resp_max_len;

		memcpy(fw_msg->resp, resp, resp_len);
	}
	hwrm_req_drop(bp, req);
	return rc;
}
EXPORT_SYMBOL(bnxt_send_msg);

void bnxt_ulp_stop(struct bnxt *bp)
{
	struct bnxt_aux_priv *aux_priv = bp->aux_priv;
	struct bnxt_en_dev *edev = bp->edev;

	if (!edev)
		return;

	edev->flags |= BNXT_EN_FLAG_ULP_STOPPED;
	if (aux_priv) {
		struct auxiliary_device *adev;

		adev = &aux_priv->aux_dev;
		if (adev->dev.driver) {
			struct auxiliary_driver *adrv;
			pm_message_t pm = {};

			adrv = to_auxiliary_drv(adev->dev.driver);
			edev->en_state = bp->state;
			adrv->suspend(adev, pm);
		}
	}
}

void bnxt_ulp_start(struct bnxt *bp, int err)
{
	struct bnxt_aux_priv *aux_priv = bp->aux_priv;
	struct bnxt_en_dev *edev = bp->edev;

	if (!edev)
		return;

	edev->flags &= ~BNXT_EN_FLAG_ULP_STOPPED;

	if (err)
		return;

	if (aux_priv) {
		struct auxiliary_device *adev;

		adev = &aux_priv->aux_dev;
		if (adev->dev.driver) {
			struct auxiliary_driver *adrv;

			adrv = to_auxiliary_drv(adev->dev.driver);
			edev->en_state = bp->state;
			adrv->resume(adev);
		}
	}

}

void bnxt_ulp_irq_stop(struct bnxt *bp)
{
	struct bnxt_en_dev *edev = bp->edev;
	struct bnxt_ulp_ops *ops;

	if (!edev || !(edev->flags & BNXT_EN_FLAG_MSIX_REQUESTED))
		return;

	if (bnxt_ulp_registered(bp->edev)) {
		struct bnxt_ulp *ulp = edev->ulp_tbl;

		if (!ulp->msix_requested)
			return;

		ops = rtnl_dereference(ulp->ulp_ops);
		if (!ops || !ops->ulp_irq_stop)
			return;
		ops->ulp_irq_stop(ulp->handle);
	}
}

void bnxt_ulp_irq_restart(struct bnxt *bp, int err)
{
	struct bnxt_en_dev *edev = bp->edev;
	struct bnxt_ulp_ops *ops;

	if (!edev || !(edev->flags & BNXT_EN_FLAG_MSIX_REQUESTED))
		return;

	if (bnxt_ulp_registered(bp->edev)) {
		struct bnxt_ulp *ulp = edev->ulp_tbl;
		struct bnxt_msix_entry *ent = NULL;

		if (!ulp->msix_requested)
			return;

		ops = rtnl_dereference(ulp->ulp_ops);
		if (!ops || !ops->ulp_irq_restart)
			return;

		if (!err) {
			ent = kcalloc(ulp->msix_requested, sizeof(*ent),
				      GFP_KERNEL);
			if (!ent)
				return;
			bnxt_fill_msix_vecs(bp, ent);
		}
		ops->ulp_irq_restart(ulp->handle, ent);
		kfree(ent);
	}
}

int bnxt_register_async_events(struct bnxt_en_dev *edev,
			       unsigned long *events_bmap,
			       u16 max_id)
{
	struct net_device *dev = edev->net;
	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_ulp *ulp;

	ulp = edev->ulp_tbl;
	ulp->async_events_bmap = events_bmap;
	/* Make sure bnxt_ulp_async_events() sees this order */
	smp_wmb();
	ulp->max_async_event_id = max_id;
	bnxt_hwrm_func_drv_rgtr(bp, events_bmap, max_id + 1, true);
	return 0;
}
EXPORT_SYMBOL(bnxt_register_async_events);

void bnxt_rdma_aux_device_uninit(struct bnxt *bp)
{
	struct bnxt_aux_priv *aux_priv;
	struct auxiliary_device *adev;

	/* Skip if no auxiliary device init was done. */
	if (!(bp->flags & BNXT_FLAG_ROCE_CAP))
		return;

	aux_priv = bp->aux_priv;
	adev = &aux_priv->aux_dev;
	auxiliary_device_delete(adev);
	auxiliary_device_uninit(adev);
}

static void bnxt_aux_dev_release(struct device *dev)
{
	struct bnxt_aux_priv *aux_priv =
		container_of(dev, struct bnxt_aux_priv, aux_dev.dev);

	ida_free(&bnxt_aux_dev_ids, aux_priv->id);
	kfree(aux_priv->edev->ulp_tbl);
	kfree(aux_priv->edev);
	kfree(aux_priv);
}

static void bnxt_set_edev_info(struct bnxt_en_dev *edev, struct bnxt *bp)
{
	edev->net = bp->dev;
	edev->pdev = bp->pdev;
	edev->l2_db_size = bp->db_size;
	edev->l2_db_size_nc = bp->db_size;

	if (bp->flags & BNXT_FLAG_ROCEV1_CAP)
		edev->flags |= BNXT_EN_FLAG_ROCEV1_CAP;
	if (bp->flags & BNXT_FLAG_ROCEV2_CAP)
		edev->flags |= BNXT_EN_FLAG_ROCEV2_CAP;
	if (bp->flags & BNXT_FLAG_VF)
		edev->flags |= BNXT_EN_FLAG_VF;

	edev->chip_num = bp->chip_num;
	edev->hw_ring_stats_size = bp->hw_ring_stats_size;
	edev->pf_port_id = bp->pf.port_id;
	edev->en_state = bp->state;

	edev->ulp_tbl->msix_requested = bnxt_get_ulp_msix_num(bp);
}

void bnxt_rdma_aux_device_init(struct bnxt *bp)
{
	struct auxiliary_device *aux_dev;
	struct bnxt_aux_priv *aux_priv;
	struct bnxt_en_dev *edev;
	struct bnxt_ulp *ulp;
	int rc;

	if (!(bp->flags & BNXT_FLAG_ROCE_CAP))
		return;

	bp->aux_priv = kzalloc(sizeof(*bp->aux_priv), GFP_KERNEL);
	if (!bp->aux_priv)
		goto exit;

	bp->aux_priv->id = ida_alloc(&bnxt_aux_dev_ids, GFP_KERNEL);
	if (bp->aux_priv->id < 0) {
		netdev_warn(bp->dev,
			    "ida alloc failed for ROCE auxiliary device\n");
		kfree(bp->aux_priv);
		goto exit;
	}

	aux_priv = bp->aux_priv;
	aux_dev = &aux_priv->aux_dev;
	aux_dev->id = aux_priv->id;
	aux_dev->name = "rdma";
	aux_dev->dev.parent = &bp->pdev->dev;
	aux_dev->dev.release = bnxt_aux_dev_release;

	rc = auxiliary_device_init(aux_dev);
	if (rc) {
		ida_free(&bnxt_aux_dev_ids, bp->aux_priv->id);
		kfree(bp->aux_priv);
		goto exit;
	}

	/* From this point, all cleanup will happen via the .release callback &
	 * any error unwinding will need to include a call to
	 * auxiliary_device_uninit.
	 */
	edev = kzalloc(sizeof(*edev), GFP_KERNEL);
	if (!edev)
		goto aux_dev_uninit;

	ulp = kzalloc(sizeof(*ulp), GFP_KERNEL);
	if (!ulp)
		goto aux_dev_uninit;

	edev->ulp_tbl = ulp;
	aux_priv->edev = edev;
	bp->edev = edev;
	bnxt_set_edev_info(edev, bp);

	rc = auxiliary_device_add(aux_dev);
	if (rc) {
		netdev_warn(bp->dev,
			    "Failed to add auxiliary device for ROCE\n");
		goto aux_dev_uninit;
	}

	return;

aux_dev_uninit:
	auxiliary_device_uninit(aux_dev);
exit:
	bp->flags &= ~BNXT_FLAG_ROCE_CAP;
}
