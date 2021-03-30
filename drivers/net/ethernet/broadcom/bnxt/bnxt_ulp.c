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

#include "bnxt_hsi.h"
#include "bnxt.h"
#include "bnxt_ulp.h"

static int bnxt_register_dev(struct bnxt_en_dev *edev, int ulp_id,
			     struct bnxt_ulp_ops *ulp_ops, void *handle)
{
	struct net_device *dev = edev->net;
	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_ulp *ulp;

	ASSERT_RTNL();
	if (ulp_id >= BNXT_MAX_ULP)
		return -EINVAL;

	ulp = &edev->ulp_tbl[ulp_id];
	if (rcu_access_pointer(ulp->ulp_ops)) {
		netdev_err(bp->dev, "ulp id %d already registered\n", ulp_id);
		return -EBUSY;
	}
	if (ulp_id == BNXT_ROCE_ULP) {
		unsigned int max_stat_ctxs;

		max_stat_ctxs = bnxt_get_max_func_stat_ctxs(bp);
		if (max_stat_ctxs <= BNXT_MIN_ROCE_STAT_CTXS ||
		    bp->cp_nr_rings == max_stat_ctxs)
			return -ENOMEM;
	}

	atomic_set(&ulp->ref_count, 0);
	ulp->handle = handle;
	rcu_assign_pointer(ulp->ulp_ops, ulp_ops);

	if (ulp_id == BNXT_ROCE_ULP) {
		if (test_bit(BNXT_STATE_OPEN, &bp->state))
			bnxt_hwrm_vnic_cfg(bp, 0);
	}

	return 0;
}

static int bnxt_unregister_dev(struct bnxt_en_dev *edev, int ulp_id)
{
	struct net_device *dev = edev->net;
	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_ulp *ulp;
	int i = 0;

	ASSERT_RTNL();
	if (ulp_id >= BNXT_MAX_ULP)
		return -EINVAL;

	ulp = &edev->ulp_tbl[ulp_id];
	if (!rcu_access_pointer(ulp->ulp_ops)) {
		netdev_err(bp->dev, "ulp id %d not registered\n", ulp_id);
		return -EINVAL;
	}
	if (ulp_id == BNXT_ROCE_ULP && ulp->msix_requested)
		edev->en_ops->bnxt_free_msix(edev, ulp_id);

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
	return 0;
}

static void bnxt_fill_msix_vecs(struct bnxt *bp, struct bnxt_msix_entry *ent)
{
	struct bnxt_en_dev *edev = bp->edev;
	int num_msix, idx, i;

	num_msix = edev->ulp_tbl[BNXT_ROCE_ULP].msix_requested;
	idx = edev->ulp_tbl[BNXT_ROCE_ULP].msix_base;
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

static int bnxt_req_msix_vecs(struct bnxt_en_dev *edev, int ulp_id,
			      struct bnxt_msix_entry *ent, int num_msix)
{
	struct net_device *dev = edev->net;
	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_hw_resc *hw_resc;
	int max_idx, max_cp_rings;
	int avail_msix, idx;
	int total_vecs;
	int rc = 0;

	ASSERT_RTNL();
	if (ulp_id != BNXT_ROCE_ULP)
		return -EINVAL;

	if (!(bp->flags & BNXT_FLAG_USING_MSIX))
		return -ENODEV;

	if (edev->ulp_tbl[ulp_id].msix_requested)
		return -EAGAIN;

	max_cp_rings = bnxt_get_max_func_cp_rings(bp);
	avail_msix = bnxt_get_avail_msix(bp, num_msix);
	if (!avail_msix)
		return -ENOMEM;
	if (avail_msix > num_msix)
		avail_msix = num_msix;

	if (BNXT_NEW_RM(bp)) {
		idx = bp->cp_nr_rings;
	} else {
		max_idx = min_t(int, bp->total_irqs, max_cp_rings);
		idx = max_idx - avail_msix;
	}
	edev->ulp_tbl[ulp_id].msix_base = idx;
	edev->ulp_tbl[ulp_id].msix_requested = avail_msix;
	hw_resc = &bp->hw_resc;
	total_vecs = idx + avail_msix;
	if (bp->total_irqs < total_vecs ||
	    (BNXT_NEW_RM(bp) && hw_resc->resv_irqs < total_vecs)) {
		if (netif_running(dev)) {
			bnxt_close_nic(bp, true, false);
			rc = bnxt_open_nic(bp, true, false);
		} else {
			rc = bnxt_reserve_rings(bp, true);
		}
	}
	if (rc) {
		edev->ulp_tbl[ulp_id].msix_requested = 0;
		return -EAGAIN;
	}

	if (BNXT_NEW_RM(bp)) {
		int resv_msix;

		resv_msix = hw_resc->resv_irqs - bp->cp_nr_rings;
		avail_msix = min_t(int, resv_msix, avail_msix);
		edev->ulp_tbl[ulp_id].msix_requested = avail_msix;
	}
	bnxt_fill_msix_vecs(bp, ent);
	edev->flags |= BNXT_EN_FLAG_MSIX_REQUESTED;
	return avail_msix;
}

static int bnxt_free_msix_vecs(struct bnxt_en_dev *edev, int ulp_id)
{
	struct net_device *dev = edev->net;
	struct bnxt *bp = netdev_priv(dev);

	ASSERT_RTNL();
	if (ulp_id != BNXT_ROCE_ULP)
		return -EINVAL;

	if (!(edev->flags & BNXT_EN_FLAG_MSIX_REQUESTED))
		return 0;

	edev->ulp_tbl[ulp_id].msix_requested = 0;
	edev->flags &= ~BNXT_EN_FLAG_MSIX_REQUESTED;
	if (netif_running(dev) && !(edev->flags & BNXT_EN_FLAG_ULP_STOPPED)) {
		bnxt_close_nic(bp, true, false);
		bnxt_open_nic(bp, true, false);
	}
	return 0;
}

int bnxt_get_ulp_msix_num(struct bnxt *bp)
{
	if (bnxt_ulp_registered(bp->edev, BNXT_ROCE_ULP)) {
		struct bnxt_en_dev *edev = bp->edev;

		return edev->ulp_tbl[BNXT_ROCE_ULP].msix_requested;
	}
	return 0;
}

int bnxt_get_ulp_msix_base(struct bnxt *bp)
{
	if (bnxt_ulp_registered(bp->edev, BNXT_ROCE_ULP)) {
		struct bnxt_en_dev *edev = bp->edev;

		if (edev->ulp_tbl[BNXT_ROCE_ULP].msix_requested)
			return edev->ulp_tbl[BNXT_ROCE_ULP].msix_base;
	}
	return 0;
}

int bnxt_get_ulp_stat_ctxs(struct bnxt *bp)
{
	if (bnxt_ulp_registered(bp->edev, BNXT_ROCE_ULP)) {
		struct bnxt_en_dev *edev = bp->edev;

		if (edev->ulp_tbl[BNXT_ROCE_ULP].msix_requested)
			return BNXT_MIN_ROCE_STAT_CTXS;
	}

	return 0;
}

static int bnxt_send_msg(struct bnxt_en_dev *edev, int ulp_id,
			 struct bnxt_fw_msg *fw_msg)
{
	struct net_device *dev = edev->net;
	struct bnxt *bp = netdev_priv(dev);
	struct input *req;
	int rc;

	if (ulp_id != BNXT_ROCE_ULP && bp->fw_reset_state)
		return -EBUSY;

	mutex_lock(&bp->hwrm_cmd_lock);
	req = fw_msg->msg;
	req->resp_addr = cpu_to_le64(bp->hwrm_cmd_resp_dma_addr);
	rc = _hwrm_send_message(bp, fw_msg->msg, fw_msg->msg_len,
				fw_msg->timeout);
	if (!rc) {
		struct output *resp = bp->hwrm_cmd_resp_addr;
		u32 len = le16_to_cpu(resp->resp_len);

		if (fw_msg->resp_max_len < len)
			len = fw_msg->resp_max_len;

		memcpy(fw_msg->resp, resp, len);
	}
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

static void bnxt_ulp_get(struct bnxt_ulp *ulp)
{
	atomic_inc(&ulp->ref_count);
}

static void bnxt_ulp_put(struct bnxt_ulp *ulp)
{
	atomic_dec(&ulp->ref_count);
}

void bnxt_ulp_stop(struct bnxt *bp)
{
	struct bnxt_en_dev *edev = bp->edev;
	struct bnxt_ulp_ops *ops;
	int i;

	if (!edev)
		return;

	edev->flags |= BNXT_EN_FLAG_ULP_STOPPED;
	for (i = 0; i < BNXT_MAX_ULP; i++) {
		struct bnxt_ulp *ulp = &edev->ulp_tbl[i];

		ops = rtnl_dereference(ulp->ulp_ops);
		if (!ops || !ops->ulp_stop)
			continue;
		ops->ulp_stop(ulp->handle);
	}
}

void bnxt_ulp_start(struct bnxt *bp, int err)
{
	struct bnxt_en_dev *edev = bp->edev;
	struct bnxt_ulp_ops *ops;
	int i;

	if (!edev)
		return;

	edev->flags &= ~BNXT_EN_FLAG_ULP_STOPPED;

	if (err)
		return;

	for (i = 0; i < BNXT_MAX_ULP; i++) {
		struct bnxt_ulp *ulp = &edev->ulp_tbl[i];

		ops = rtnl_dereference(ulp->ulp_ops);
		if (!ops || !ops->ulp_start)
			continue;
		ops->ulp_start(ulp->handle);
	}
}

void bnxt_ulp_sriov_cfg(struct bnxt *bp, int num_vfs)
{
	struct bnxt_en_dev *edev = bp->edev;
	struct bnxt_ulp_ops *ops;
	int i;

	if (!edev)
		return;

	for (i = 0; i < BNXT_MAX_ULP; i++) {
		struct bnxt_ulp *ulp = &edev->ulp_tbl[i];

		rcu_read_lock();
		ops = rcu_dereference(ulp->ulp_ops);
		if (!ops || !ops->ulp_sriov_config) {
			rcu_read_unlock();
			continue;
		}
		bnxt_ulp_get(ulp);
		rcu_read_unlock();
		ops->ulp_sriov_config(ulp->handle, num_vfs);
		bnxt_ulp_put(ulp);
	}
}

void bnxt_ulp_shutdown(struct bnxt *bp)
{
	struct bnxt_en_dev *edev = bp->edev;
	struct bnxt_ulp_ops *ops;
	int i;

	if (!edev)
		return;

	for (i = 0; i < BNXT_MAX_ULP; i++) {
		struct bnxt_ulp *ulp = &edev->ulp_tbl[i];

		ops = rtnl_dereference(ulp->ulp_ops);
		if (!ops || !ops->ulp_shutdown)
			continue;
		ops->ulp_shutdown(ulp->handle);
	}
}

void bnxt_ulp_irq_stop(struct bnxt *bp)
{
	struct bnxt_en_dev *edev = bp->edev;
	struct bnxt_ulp_ops *ops;

	if (!edev || !(edev->flags & BNXT_EN_FLAG_MSIX_REQUESTED))
		return;

	if (bnxt_ulp_registered(bp->edev, BNXT_ROCE_ULP)) {
		struct bnxt_ulp *ulp = &edev->ulp_tbl[BNXT_ROCE_ULP];

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

	if (bnxt_ulp_registered(bp->edev, BNXT_ROCE_ULP)) {
		struct bnxt_ulp *ulp = &edev->ulp_tbl[BNXT_ROCE_ULP];
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

void bnxt_ulp_async_events(struct bnxt *bp, struct hwrm_async_event_cmpl *cmpl)
{
	u16 event_id = le16_to_cpu(cmpl->event_id);
	struct bnxt_en_dev *edev = bp->edev;
	struct bnxt_ulp_ops *ops;
	int i;

	if (!edev)
		return;

	rcu_read_lock();
	for (i = 0; i < BNXT_MAX_ULP; i++) {
		struct bnxt_ulp *ulp = &edev->ulp_tbl[i];

		ops = rcu_dereference(ulp->ulp_ops);
		if (!ops || !ops->ulp_async_notifier)
			continue;
		if (!ulp->async_events_bmap ||
		    event_id > ulp->max_async_event_id)
			continue;

		/* Read max_async_event_id first before testing the bitmap. */
		smp_rmb();
		if (test_bit(event_id, ulp->async_events_bmap))
			ops->ulp_async_notifier(ulp->handle, cmpl);
	}
	rcu_read_unlock();
}

static int bnxt_register_async_events(struct bnxt_en_dev *edev, int ulp_id,
				      unsigned long *events_bmap, u16 max_id)
{
	struct net_device *dev = edev->net;
	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_ulp *ulp;

	if (ulp_id >= BNXT_MAX_ULP)
		return -EINVAL;

	ulp = &edev->ulp_tbl[ulp_id];
	ulp->async_events_bmap = events_bmap;
	/* Make sure bnxt_ulp_async_events() sees this order */
	smp_wmb();
	ulp->max_async_event_id = max_id;
	bnxt_hwrm_func_drv_rgtr(bp, events_bmap, max_id + 1, true);
	return 0;
}

static const struct bnxt_en_ops bnxt_en_ops_tbl = {
	.bnxt_register_device	= bnxt_register_dev,
	.bnxt_unregister_device	= bnxt_unregister_dev,
	.bnxt_request_msix	= bnxt_req_msix_vecs,
	.bnxt_free_msix		= bnxt_free_msix_vecs,
	.bnxt_send_fw_msg	= bnxt_send_msg,
	.bnxt_register_fw_async_events	= bnxt_register_async_events,
};

struct bnxt_en_dev *bnxt_ulp_probe(struct net_device *dev)
{
	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_en_dev *edev;

	edev = bp->edev;
	if (!edev) {
		edev = kzalloc(sizeof(*edev), GFP_KERNEL);
		if (!edev)
			return ERR_PTR(-ENOMEM);
		edev->en_ops = &bnxt_en_ops_tbl;
		if (bp->flags & BNXT_FLAG_ROCEV1_CAP)
			edev->flags |= BNXT_EN_FLAG_ROCEV1_CAP;
		if (bp->flags & BNXT_FLAG_ROCEV2_CAP)
			edev->flags |= BNXT_EN_FLAG_ROCEV2_CAP;
		edev->net = dev;
		edev->pdev = bp->pdev;
		edev->l2_db_size = bp->db_size;
		edev->l2_db_size_nc = bp->db_size;
		bp->edev = edev;
	}
	return bp->edev;
}
