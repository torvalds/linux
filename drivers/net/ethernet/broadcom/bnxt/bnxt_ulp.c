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
	int num_msix, i;

	if (!edev->ulp_tbl->msix_requested) {
		netdev_warn(bp->dev, "Requested MSI-X vectors insufficient\n");
		return;
	}
	num_msix = edev->ulp_tbl->msix_requested;
	for (i = 0; i < num_msix; i++) {
		ent[i].vector = bp->irq_tbl[i].vector;
		ent[i].ring_idx = i;
		if (bp->flags & BNXT_FLAG_CHIP_P5_PLUS)
			ent[i].db_offset = bp->db_offset;
		else
			ent[i].db_offset = i * 0x80;
	}
}

int bnxt_get_ulp_msix_num(struct bnxt *bp)
{
	if (bp->edev)
		return bp->edev->ulp_num_msix_vec;
	return 0;
}

void bnxt_set_ulp_msix_num(struct bnxt *bp, int num)
{
	if (bp->edev)
		bp->edev->ulp_num_msix_vec = num;
}

int bnxt_get_ulp_msix_num_in_use(struct bnxt *bp)
{
	if (bnxt_ulp_registered(bp->edev))
		return bp->edev->ulp_num_msix_vec;
	return 0;
}

int bnxt_get_ulp_stat_ctxs(struct bnxt *bp)
{
	if (bp->edev)
		return bp->edev->ulp_num_ctxs;
	return 0;
}

void bnxt_set_ulp_stat_ctxs(struct bnxt *bp, int num_ulp_ctx)
{
	if (bp->edev)
		bp->edev->ulp_num_ctxs = num_ulp_ctx;
}

int bnxt_get_ulp_stat_ctxs_in_use(struct bnxt *bp)
{
	if (bnxt_ulp_registered(bp->edev))
		return bp->edev->ulp_num_ctxs;
	return 0;
}

void bnxt_set_dflt_ulp_stat_ctxs(struct bnxt *bp)
{
	if (bp->edev) {
		bp->edev->ulp_num_ctxs = BNXT_MIN_ROCE_STAT_CTXS;
		/* Reserve one additional stat_ctx for PF0 (except
		 * on 1-port NICs) as it also creates one stat_ctx
		 * for PF1 in case of RoCE bonding.
		 */
		if (BNXT_PF(bp) && !bp->pf.port_id &&
		    bp->port_count > 1)
			bp->edev->ulp_num_ctxs++;
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
	int rc = 0;

	netdev_lock(dev);
	mutex_lock(&edev->en_dev_lock);
	if (!bp->irq_tbl) {
		rc = -ENODEV;
		goto exit;
	}
	max_stat_ctxs = bnxt_get_max_func_stat_ctxs(bp);
	if (max_stat_ctxs <= BNXT_MIN_ROCE_STAT_CTXS ||
	    bp->cp_nr_rings == max_stat_ctxs) {
		rc = -ENOMEM;
		goto exit;
	}

	ulp = edev->ulp_tbl;
	ulp->handle = handle;
	rcu_assign_pointer(ulp->ulp_ops, ulp_ops);

	if (test_bit(BNXT_STATE_OPEN, &bp->state))
		bnxt_hwrm_vnic_cfg(bp, &bp->vnic_info[BNXT_VNIC_DEFAULT]);

	edev->ulp_tbl->msix_requested = bnxt_get_ulp_msix_num(bp);

	bnxt_fill_msix_vecs(bp, bp->edev->msix_entries);
	edev->flags |= BNXT_EN_FLAG_MSIX_REQUESTED;
exit:
	mutex_unlock(&edev->en_dev_lock);
	netdev_unlock(dev);
	return rc;
}
EXPORT_SYMBOL(bnxt_register_dev);

void bnxt_unregister_dev(struct bnxt_en_dev *edev)
{
	struct net_device *dev = edev->net;
	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_ulp *ulp;
	int i = 0;

	ulp = edev->ulp_tbl;
	netdev_lock(dev);
	mutex_lock(&edev->en_dev_lock);
	if (ulp->msix_requested)
		edev->flags &= ~BNXT_EN_FLAG_MSIX_REQUESTED;
	edev->ulp_tbl->msix_requested = 0;

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
	mutex_unlock(&edev->en_dev_lock);
	netdev_unlock(dev);
	return;
}
EXPORT_SYMBOL(bnxt_unregister_dev);

static int bnxt_set_dflt_ulp_msix(struct bnxt *bp)
{
	int roce_msix = BNXT_MAX_ROCE_MSIX;

	if (BNXT_VF(bp))
		roce_msix = BNXT_MAX_ROCE_MSIX_VF;
	else if (bp->port_partition_type)
		roce_msix = BNXT_MAX_ROCE_MSIX_NPAR_PF;

	/* NQ MSIX vectors should match the number of CPUs plus 1 more for
	 * the CREQ MSIX, up to the default.
	 */
	return min_t(int, roce_msix, num_online_cpus() + 1);
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
		goto drop_req;

	hwrm_req_timeout(bp, req, fw_msg->timeout);
	resp = hwrm_req_hold(bp, req);
	rc = hwrm_req_send(bp, req);
	resp_len = le16_to_cpu(resp->resp_len);
	if (resp_len) {
		if (fw_msg->resp_max_len < resp_len)
			resp_len = fw_msg->resp_max_len;

		memcpy(fw_msg->resp, resp, resp_len);
	}
drop_req:
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

	mutex_lock(&edev->en_dev_lock);
	if (!bnxt_ulp_registered(edev)) {
		mutex_unlock(&edev->en_dev_lock);
		return;
	}

	edev->flags |= BNXT_EN_FLAG_ULP_STOPPED;
	if (aux_priv) {
		struct auxiliary_device *adev;

		adev = &aux_priv->aux_dev;
		if (adev->dev.driver) {
			const struct auxiliary_driver *adrv;
			pm_message_t pm = {};

			adrv = to_auxiliary_drv(adev->dev.driver);
			edev->en_state = bp->state;
			adrv->suspend(adev, pm);
		}
	}
	mutex_unlock(&edev->en_dev_lock);
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

	mutex_lock(&edev->en_dev_lock);
	if (!bnxt_ulp_registered(edev)) {
		mutex_unlock(&edev->en_dev_lock);
		return;
	}

	if (edev->ulp_tbl->msix_requested)
		bnxt_fill_msix_vecs(bp, edev->msix_entries);

	if (aux_priv) {
		struct auxiliary_device *adev;

		adev = &aux_priv->aux_dev;
		if (adev->dev.driver) {
			const struct auxiliary_driver *adrv;

			adrv = to_auxiliary_drv(adev->dev.driver);
			edev->en_state = bp->state;
			adrv->resume(adev);
		}
	}
	mutex_unlock(&edev->en_dev_lock);
}

void bnxt_ulp_irq_stop(struct bnxt *bp)
{
	struct bnxt_en_dev *edev = bp->edev;
	struct bnxt_ulp_ops *ops;
	bool reset = false;

	if (!edev || !(edev->flags & BNXT_EN_FLAG_MSIX_REQUESTED))
		return;

	if (bnxt_ulp_registered(bp->edev)) {
		struct bnxt_ulp *ulp = edev->ulp_tbl;

		if (!ulp->msix_requested)
			return;

		netdev_lock(bp->dev);
		ops = rcu_dereference(ulp->ulp_ops);
		if (!ops || !ops->ulp_irq_stop)
			return;
		if (test_bit(BNXT_STATE_FW_RESET_DET, &bp->state))
			reset = true;
		ops->ulp_irq_stop(ulp->handle, reset);
		netdev_unlock(bp->dev);
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

		netdev_lock(bp->dev);
		ops = rcu_dereference(ulp->ulp_ops);
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
		netdev_unlock(bp->dev);
		kfree(ent);
	}
}

void bnxt_ulp_async_events(struct bnxt *bp, struct hwrm_async_event_cmpl *cmpl)
{
	u16 event_id = le16_to_cpu(cmpl->event_id);
	struct bnxt_en_dev *edev = bp->edev;
	struct bnxt_ulp_ops *ops;
	struct bnxt_ulp *ulp;

	if (!bnxt_ulp_registered(edev))
		return;
	ulp = edev->ulp_tbl;

	rcu_read_lock();

	ops = rcu_dereference(ulp->ulp_ops);
	if (!ops || !ops->ulp_async_notifier)
		goto exit_unlock_rcu;
	if (!ulp->async_events_bmap || event_id > ulp->max_async_event_id)
		goto exit_unlock_rcu;

	/* Read max_async_event_id first before testing the bitmap. */
	smp_rmb();

	if (test_bit(event_id, ulp->async_events_bmap))
		ops->ulp_async_notifier(ulp->handle, cmpl);
exit_unlock_rcu:
	rcu_read_unlock();
}

void bnxt_register_async_events(struct bnxt_en_dev *edev,
				unsigned long *events_bmap, u16 max_id)
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
}
EXPORT_SYMBOL(bnxt_register_async_events);

void bnxt_rdma_aux_device_uninit(struct bnxt *bp)
{
	struct bnxt_aux_priv *aux_priv;
	struct auxiliary_device *adev;

	/* Skip if no auxiliary device init was done. */
	if (!bp->aux_priv)
		return;

	aux_priv = bp->aux_priv;
	adev = &aux_priv->aux_dev;
	auxiliary_device_uninit(adev);
}

static void bnxt_aux_dev_release(struct device *dev)
{
	struct bnxt_aux_priv *aux_priv =
		container_of(dev, struct bnxt_aux_priv, aux_dev.dev);
	struct bnxt *bp = netdev_priv(aux_priv->edev->net);

	ida_free(&bnxt_aux_dev_ids, aux_priv->id);
	kfree(aux_priv->edev->ulp_tbl);
	bp->edev = NULL;
	kfree(aux_priv->edev);
	kfree(aux_priv);
	bp->aux_priv = NULL;
}

void bnxt_rdma_aux_device_del(struct bnxt *bp)
{
	if (!bp->edev)
		return;

	auxiliary_device_delete(&bp->aux_priv->aux_dev);
}

static void bnxt_set_edev_info(struct bnxt_en_dev *edev, struct bnxt *bp)
{
	edev->net = bp->dev;
	edev->pdev = bp->pdev;
	edev->l2_db_size = bp->db_size;
	edev->l2_db_size_nc = bp->db_size;
	edev->l2_db_offset = bp->db_offset;
	mutex_init(&edev->en_dev_lock);

	if (bp->flags & BNXT_FLAG_ROCEV1_CAP)
		edev->flags |= BNXT_EN_FLAG_ROCEV1_CAP;
	if (bp->flags & BNXT_FLAG_ROCEV2_CAP)
		edev->flags |= BNXT_EN_FLAG_ROCEV2_CAP;
	if (bp->flags & BNXT_FLAG_VF)
		edev->flags |= BNXT_EN_FLAG_VF;
	if (BNXT_ROCE_VF_RESC_CAP(bp))
		edev->flags |= BNXT_EN_FLAG_ROCE_VF_RES_MGMT;
	if (BNXT_SW_RES_LMT(bp))
		edev->flags |= BNXT_EN_FLAG_SW_RES_LMT;

	edev->chip_num = bp->chip_num;
	edev->hw_ring_stats_size = bp->hw_ring_stats_size;
	edev->pf_port_id = bp->pf.port_id;
	edev->en_state = bp->state;
	edev->bar0 = bp->bar0;
}

void bnxt_rdma_aux_device_add(struct bnxt *bp)
{
	struct auxiliary_device *aux_dev;
	int rc;

	if (!bp->edev)
		return;

	aux_dev = &bp->aux_priv->aux_dev;
	rc = auxiliary_device_add(aux_dev);
	if (rc) {
		netdev_warn(bp->dev, "Failed to add auxiliary device for ROCE\n");
		auxiliary_device_uninit(aux_dev);
		bp->flags &= ~BNXT_FLAG_ROCE_CAP;
	}
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

	aux_priv = kzalloc(sizeof(*bp->aux_priv), GFP_KERNEL);
	if (!aux_priv)
		goto exit;

	aux_priv->id = ida_alloc(&bnxt_aux_dev_ids, GFP_KERNEL);
	if (aux_priv->id < 0) {
		netdev_warn(bp->dev,
			    "ida alloc failed for ROCE auxiliary device\n");
		kfree(aux_priv);
		goto exit;
	}

	aux_dev = &aux_priv->aux_dev;
	aux_dev->id = aux_priv->id;
	aux_dev->name = "rdma";
	aux_dev->dev.parent = &bp->pdev->dev;
	aux_dev->dev.release = bnxt_aux_dev_release;

	rc = auxiliary_device_init(aux_dev);
	if (rc) {
		ida_free(&bnxt_aux_dev_ids, aux_priv->id);
		kfree(aux_priv);
		goto exit;
	}
	bp->aux_priv = aux_priv;

	/* From this point, all cleanup will happen via the .release callback &
	 * any error unwinding will need to include a call to
	 * auxiliary_device_uninit.
	 */
	edev = kzalloc(sizeof(*edev), GFP_KERNEL);
	if (!edev)
		goto aux_dev_uninit;

	aux_priv->edev = edev;

	ulp = kzalloc(sizeof(*ulp), GFP_KERNEL);
	if (!ulp)
		goto aux_dev_uninit;

	edev->ulp_tbl = ulp;
	bp->edev = edev;
	bnxt_set_edev_info(edev, bp);
	bp->ulp_num_msix_want = bnxt_set_dflt_ulp_msix(bp);

	return;

aux_dev_uninit:
	auxiliary_device_uninit(aux_dev);
exit:
	bp->flags &= ~BNXT_FLAG_ROCE_CAP;
}
