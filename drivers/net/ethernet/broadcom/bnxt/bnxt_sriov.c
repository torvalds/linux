/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2014-2015 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/if_vlan.h>
#include <linux/interrupt.h>
#include <linux/etherdevice.h>
#include "bnxt_hsi.h"
#include "bnxt.h"
#include "bnxt_sriov.h"
#include "bnxt_ethtool.h"

#ifdef CONFIG_BNXT_SRIOV
static int bnxt_vf_ndo_prep(struct bnxt *bp, int vf_id)
{
	if (bp->state != BNXT_STATE_OPEN) {
		netdev_err(bp->dev, "vf ndo called though PF is down\n");
		return -EINVAL;
	}
	if (!bp->pf.active_vfs) {
		netdev_err(bp->dev, "vf ndo called though sriov is disabled\n");
		return -EINVAL;
	}
	if (vf_id >= bp->pf.max_vfs) {
		netdev_err(bp->dev, "Invalid VF id %d\n", vf_id);
		return -EINVAL;
	}
	return 0;
}

int bnxt_set_vf_spoofchk(struct net_device *dev, int vf_id, bool setting)
{
	struct hwrm_func_cfg_input req = {0};
	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_vf_info *vf;
	bool old_setting = false;
	u32 func_flags;
	int rc;

	rc = bnxt_vf_ndo_prep(bp, vf_id);
	if (rc)
		return rc;

	vf = &bp->pf.vf[vf_id];
	if (vf->flags & BNXT_VF_SPOOFCHK)
		old_setting = true;
	if (old_setting == setting)
		return 0;

	func_flags = vf->func_flags;
	if (setting)
		func_flags |= FUNC_CFG_REQ_FLAGS_SRC_MAC_ADDR_CHECK;
	else
		func_flags &= ~FUNC_CFG_REQ_FLAGS_SRC_MAC_ADDR_CHECK;
	/*TODO: if the driver supports VLAN filter on guest VLAN,
	 * the spoof check should also include vlan anti-spoofing
	 */
	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_FUNC_CFG, -1, -1);
	req.vf_id = cpu_to_le16(vf->fw_fid);
	req.flags = cpu_to_le32(func_flags);
	rc = hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (!rc) {
		vf->func_flags = func_flags;
		if (setting)
			vf->flags |= BNXT_VF_SPOOFCHK;
		else
			vf->flags &= ~BNXT_VF_SPOOFCHK;
	}
	return rc;
}

int bnxt_get_vf_config(struct net_device *dev, int vf_id,
		       struct ifla_vf_info *ivi)
{
	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_vf_info *vf;
	int rc;

	rc = bnxt_vf_ndo_prep(bp, vf_id);
	if (rc)
		return rc;

	ivi->vf = vf_id;
	vf = &bp->pf.vf[vf_id];

	memcpy(&ivi->mac, vf->mac_addr, ETH_ALEN);
	ivi->max_tx_rate = vf->max_tx_rate;
	ivi->min_tx_rate = vf->min_tx_rate;
	ivi->vlan = vf->vlan;
	ivi->qos = vf->flags & BNXT_VF_QOS;
	ivi->spoofchk = vf->flags & BNXT_VF_SPOOFCHK;
	if (!(vf->flags & BNXT_VF_LINK_FORCED))
		ivi->linkstate = IFLA_VF_LINK_STATE_AUTO;
	else if (vf->flags & BNXT_VF_LINK_UP)
		ivi->linkstate = IFLA_VF_LINK_STATE_ENABLE;
	else
		ivi->linkstate = IFLA_VF_LINK_STATE_DISABLE;

	return 0;
}

int bnxt_set_vf_mac(struct net_device *dev, int vf_id, u8 *mac)
{
	struct hwrm_func_cfg_input req = {0};
	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_vf_info *vf;
	int rc;

	rc = bnxt_vf_ndo_prep(bp, vf_id);
	if (rc)
		return rc;
	/* reject bc or mc mac addr, zero mac addr means allow
	 * VF to use its own mac addr
	 */
	if (is_multicast_ether_addr(mac)) {
		netdev_err(dev, "Invalid VF ethernet address\n");
		return -EINVAL;
	}
	vf = &bp->pf.vf[vf_id];

	memcpy(vf->mac_addr, mac, ETH_ALEN);
	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_FUNC_CFG, -1, -1);
	req.vf_id = cpu_to_le16(vf->fw_fid);
	req.flags = cpu_to_le32(vf->func_flags);
	req.enables = cpu_to_le32(FUNC_CFG_REQ_ENABLES_DFLT_MAC_ADDR);
	memcpy(req.dflt_mac_addr, mac, ETH_ALEN);
	return hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
}

int bnxt_set_vf_vlan(struct net_device *dev, int vf_id, u16 vlan_id, u8 qos)
{
	struct hwrm_func_cfg_input req = {0};
	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_vf_info *vf;
	u16 vlan_tag;
	int rc;

	rc = bnxt_vf_ndo_prep(bp, vf_id);
	if (rc)
		return rc;

	/* TODO: needed to implement proper handling of user priority,
	 * currently fail the command if there is valid priority
	 */
	if (vlan_id > 4095 || qos)
		return -EINVAL;

	vf = &bp->pf.vf[vf_id];
	vlan_tag = vlan_id;
	if (vlan_tag == vf->vlan)
		return 0;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_FUNC_CFG, -1, -1);
	req.vf_id = cpu_to_le16(vf->fw_fid);
	req.flags = cpu_to_le32(vf->func_flags);
	req.dflt_vlan = cpu_to_le16(vlan_tag);
	req.enables = cpu_to_le32(FUNC_CFG_REQ_ENABLES_DFLT_VLAN);
	rc = hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (!rc)
		vf->vlan = vlan_tag;
	return rc;
}

int bnxt_set_vf_bw(struct net_device *dev, int vf_id, int min_tx_rate,
		   int max_tx_rate)
{
	struct hwrm_func_cfg_input req = {0};
	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_vf_info *vf;
	u32 pf_link_speed;
	int rc;

	rc = bnxt_vf_ndo_prep(bp, vf_id);
	if (rc)
		return rc;

	vf = &bp->pf.vf[vf_id];
	pf_link_speed = bnxt_fw_to_ethtool_speed(bp->link_info.link_speed);
	if (max_tx_rate > pf_link_speed) {
		netdev_info(bp->dev, "max tx rate %d exceed PF link speed for VF %d\n",
			    max_tx_rate, vf_id);
		return -EINVAL;
	}

	if (min_tx_rate > pf_link_speed || min_tx_rate > max_tx_rate) {
		netdev_info(bp->dev, "min tx rate %d is invalid for VF %d\n",
			    min_tx_rate, vf_id);
		return -EINVAL;
	}
	if (min_tx_rate == vf->min_tx_rate && max_tx_rate == vf->max_tx_rate)
		return 0;
	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_FUNC_CFG, -1, -1);
	req.vf_id = cpu_to_le16(vf->fw_fid);
	req.flags = cpu_to_le32(vf->func_flags);
	req.enables = cpu_to_le32(FUNC_CFG_REQ_ENABLES_MAX_BW);
	req.max_bw = cpu_to_le32(max_tx_rate);
	req.enables |= cpu_to_le32(FUNC_CFG_REQ_ENABLES_MIN_BW);
	req.min_bw = cpu_to_le32(min_tx_rate);
	rc = hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (!rc) {
		vf->min_tx_rate = min_tx_rate;
		vf->max_tx_rate = max_tx_rate;
	}
	return rc;
}

int bnxt_set_vf_link_state(struct net_device *dev, int vf_id, int link)
{
	struct bnxt *bp = netdev_priv(dev);
	struct bnxt_vf_info *vf;
	int rc;

	rc = bnxt_vf_ndo_prep(bp, vf_id);
	if (rc)
		return rc;

	vf = &bp->pf.vf[vf_id];

	vf->flags &= ~(BNXT_VF_LINK_UP | BNXT_VF_LINK_FORCED);
	switch (link) {
	case IFLA_VF_LINK_STATE_AUTO:
		vf->flags |= BNXT_VF_LINK_UP;
		break;
	case IFLA_VF_LINK_STATE_DISABLE:
		vf->flags |= BNXT_VF_LINK_FORCED;
		break;
	case IFLA_VF_LINK_STATE_ENABLE:
		vf->flags |= BNXT_VF_LINK_UP | BNXT_VF_LINK_FORCED;
		break;
	default:
		netdev_err(bp->dev, "Invalid link option\n");
		rc = -EINVAL;
		break;
	}
	/* CHIMP TODO: send msg to VF to update new link state */

	return rc;
}

static int bnxt_set_vf_attr(struct bnxt *bp, int num_vfs)
{
	int i;
	struct bnxt_vf_info *vf;

	for (i = 0; i < num_vfs; i++) {
		vf = &bp->pf.vf[i];
		memset(vf, 0, sizeof(*vf));
		vf->flags = BNXT_VF_QOS | BNXT_VF_LINK_UP;
	}
	return 0;
}

static int bnxt_hwrm_func_vf_resource_free(struct bnxt *bp, int num_vfs)
{
	int i, rc = 0;
	struct bnxt_pf_info *pf = &bp->pf;
	struct hwrm_func_vf_resc_free_input req = {0};

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_FUNC_VF_RESC_FREE, -1, -1);

	mutex_lock(&bp->hwrm_cmd_lock);
	for (i = pf->first_vf_id; i < pf->first_vf_id + num_vfs; i++) {
		req.vf_id = cpu_to_le16(i);
		rc = _hwrm_send_message(bp, &req, sizeof(req),
					HWRM_CMD_TIMEOUT);
		if (rc)
			break;
	}
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

static void bnxt_free_vf_resources(struct bnxt *bp)
{
	struct pci_dev *pdev = bp->pdev;
	int i;

	kfree(bp->pf.vf_event_bmap);
	bp->pf.vf_event_bmap = NULL;

	for (i = 0; i < 4; i++) {
		if (bp->pf.hwrm_cmd_req_addr[i]) {
			dma_free_coherent(&pdev->dev, BNXT_PAGE_SIZE,
					  bp->pf.hwrm_cmd_req_addr[i],
					  bp->pf.hwrm_cmd_req_dma_addr[i]);
			bp->pf.hwrm_cmd_req_addr[i] = NULL;
		}
	}

	kfree(bp->pf.vf);
	bp->pf.vf = NULL;
}

static int bnxt_alloc_vf_resources(struct bnxt *bp, int num_vfs)
{
	struct pci_dev *pdev = bp->pdev;
	u32 nr_pages, size, i, j, k = 0;

	bp->pf.vf = kcalloc(num_vfs, sizeof(struct bnxt_vf_info), GFP_KERNEL);
	if (!bp->pf.vf)
		return -ENOMEM;

	bnxt_set_vf_attr(bp, num_vfs);

	size = num_vfs * BNXT_HWRM_REQ_MAX_SIZE;
	nr_pages = size / BNXT_PAGE_SIZE;
	if (size & (BNXT_PAGE_SIZE - 1))
		nr_pages++;

	for (i = 0; i < nr_pages; i++) {
		bp->pf.hwrm_cmd_req_addr[i] =
			dma_alloc_coherent(&pdev->dev, BNXT_PAGE_SIZE,
					   &bp->pf.hwrm_cmd_req_dma_addr[i],
					   GFP_KERNEL);

		if (!bp->pf.hwrm_cmd_req_addr[i])
			return -ENOMEM;

		for (j = 0; j < BNXT_HWRM_REQS_PER_PAGE && k < num_vfs; j++) {
			struct bnxt_vf_info *vf = &bp->pf.vf[k];

			vf->hwrm_cmd_req_addr = bp->pf.hwrm_cmd_req_addr[i] +
						j * BNXT_HWRM_REQ_MAX_SIZE;
			vf->hwrm_cmd_req_dma_addr =
				bp->pf.hwrm_cmd_req_dma_addr[i] + j *
				BNXT_HWRM_REQ_MAX_SIZE;
			k++;
		}
	}

	/* Max 128 VF's */
	bp->pf.vf_event_bmap = kzalloc(16, GFP_KERNEL);
	if (!bp->pf.vf_event_bmap)
		return -ENOMEM;

	bp->pf.hwrm_cmd_req_pages = nr_pages;
	return 0;
}

static int bnxt_hwrm_func_buf_rgtr(struct bnxt *bp)
{
	struct hwrm_func_buf_rgtr_input req = {0};

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_FUNC_BUF_RGTR, -1, -1);

	req.req_buf_num_pages = cpu_to_le16(bp->pf.hwrm_cmd_req_pages);
	req.req_buf_page_size = cpu_to_le16(BNXT_PAGE_SHIFT);
	req.req_buf_len = cpu_to_le16(BNXT_HWRM_REQ_MAX_SIZE);
	req.req_buf_page_addr0 = cpu_to_le64(bp->pf.hwrm_cmd_req_dma_addr[0]);
	req.req_buf_page_addr1 = cpu_to_le64(bp->pf.hwrm_cmd_req_dma_addr[1]);
	req.req_buf_page_addr2 = cpu_to_le64(bp->pf.hwrm_cmd_req_dma_addr[2]);
	req.req_buf_page_addr3 = cpu_to_le64(bp->pf.hwrm_cmd_req_dma_addr[3]);

	return hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
}

/* only call by PF to reserve resources for VF */
static int bnxt_hwrm_func_cfg(struct bnxt *bp, int *num_vfs)
{
	u32 rc = 0, mtu, i;
	u16 vf_tx_rings, vf_rx_rings, vf_cp_rings, vf_stat_ctx, vf_vnics;
	struct hwrm_func_cfg_input req = {0};
	struct bnxt_pf_info *pf = &bp->pf;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_FUNC_CFG, -1, -1);

	/* Remaining rings are distributed equally amongs VF's for now */
	/* TODO: the following workaroud is needed to restrict total number
	 * of vf_cp_rings not exceed number of HW ring groups. This WA should
	 * be removed once new HWRM provides HW ring groups capability in
	 * hwrm_func_qcap.
	 */
	vf_cp_rings = min_t(u16, bp->pf.max_cp_rings, bp->pf.max_stat_ctxs);
	vf_cp_rings = (vf_cp_rings - bp->cp_nr_rings) / *num_vfs;
	/* TODO: restore this logic below once the WA above is removed */
	/* vf_cp_rings = (bp->pf.max_cp_rings - bp->cp_nr_rings) / *num_vfs; */
	vf_stat_ctx = (bp->pf.max_stat_ctxs - bp->num_stat_ctxs) / *num_vfs;
	if (bp->flags & BNXT_FLAG_AGG_RINGS)
		vf_rx_rings = (bp->pf.max_rx_rings - bp->rx_nr_rings * 2) /
			      *num_vfs;
	else
		vf_rx_rings = (bp->pf.max_rx_rings - bp->rx_nr_rings) /
			      *num_vfs;
	vf_tx_rings = (bp->pf.max_tx_rings - bp->tx_nr_rings) / *num_vfs;

	req.enables = cpu_to_le32(FUNC_CFG_REQ_ENABLES_MTU |
				  FUNC_CFG_REQ_ENABLES_MRU |
				  FUNC_CFG_REQ_ENABLES_NUM_RSSCOS_CTXS |
				  FUNC_CFG_REQ_ENABLES_NUM_STAT_CTXS |
				  FUNC_CFG_REQ_ENABLES_NUM_CMPL_RINGS |
				  FUNC_CFG_REQ_ENABLES_NUM_TX_RINGS |
				  FUNC_CFG_REQ_ENABLES_NUM_RX_RINGS |
				  FUNC_CFG_REQ_ENABLES_NUM_L2_CTXS |
				  FUNC_CFG_REQ_ENABLES_NUM_VNICS);

	mtu = bp->dev->mtu + ETH_HLEN + ETH_FCS_LEN + VLAN_HLEN;
	req.mru = cpu_to_le16(mtu);
	req.mtu = cpu_to_le16(mtu);

	req.num_rsscos_ctxs = cpu_to_le16(1);
	req.num_cmpl_rings = cpu_to_le16(vf_cp_rings);
	req.num_tx_rings = cpu_to_le16(vf_tx_rings);
	req.num_rx_rings = cpu_to_le16(vf_rx_rings);
	req.num_l2_ctxs = cpu_to_le16(4);
	vf_vnics = 1;

	req.num_vnics = cpu_to_le16(vf_vnics);
	/* FIXME spec currently uses 1 bit for stats ctx */
	req.num_stat_ctxs = cpu_to_le16(vf_stat_ctx);

	mutex_lock(&bp->hwrm_cmd_lock);
	for (i = 0; i < *num_vfs; i++) {
		req.vf_id = cpu_to_le16(pf->first_vf_id + i);
		rc = _hwrm_send_message(bp, &req, sizeof(req),
					HWRM_CMD_TIMEOUT);
		if (rc)
			break;
		bp->pf.active_vfs = i + 1;
		bp->pf.vf[i].fw_fid = le16_to_cpu(req.vf_id);
	}
	mutex_unlock(&bp->hwrm_cmd_lock);
	if (!rc) {
		bp->pf.max_pf_tx_rings = bp->tx_nr_rings;
		if (bp->flags & BNXT_FLAG_AGG_RINGS)
			bp->pf.max_pf_rx_rings = bp->rx_nr_rings * 2;
		else
			bp->pf.max_pf_rx_rings = bp->rx_nr_rings;
	}
	return rc;
}

static int bnxt_sriov_enable(struct bnxt *bp, int *num_vfs)
{
	int rc = 0, vfs_supported;
	int min_rx_rings, min_tx_rings, min_rss_ctxs;
	int tx_ok = 0, rx_ok = 0, rss_ok = 0;

	/* Check if we can enable requested num of vf's. At a mininum
	 * we require 1 RX 1 TX rings for each VF. In this minimum conf
	 * features like TPA will not be available.
	 */
	vfs_supported = *num_vfs;

	while (vfs_supported) {
		min_rx_rings = vfs_supported;
		min_tx_rings = vfs_supported;
		min_rss_ctxs = vfs_supported;

		if (bp->flags & BNXT_FLAG_AGG_RINGS) {
			if (bp->pf.max_rx_rings - bp->rx_nr_rings * 2 >=
			    min_rx_rings)
				rx_ok = 1;
		} else {
			if (bp->pf.max_rx_rings - bp->rx_nr_rings >=
			    min_rx_rings)
				rx_ok = 1;
		}

		if (bp->pf.max_tx_rings - bp->tx_nr_rings >= min_tx_rings)
			tx_ok = 1;

		if (bp->pf.max_rsscos_ctxs - bp->rsscos_nr_ctxs >= min_rss_ctxs)
			rss_ok = 1;

		if (tx_ok && rx_ok && rss_ok)
			break;

		vfs_supported--;
	}

	if (!vfs_supported) {
		netdev_err(bp->dev, "Cannot enable VF's as all resources are used by PF\n");
		return -EINVAL;
	}

	if (vfs_supported != *num_vfs) {
		netdev_info(bp->dev, "Requested VFs %d, can enable %d\n",
			    *num_vfs, vfs_supported);
		*num_vfs = vfs_supported;
	}

	rc = bnxt_alloc_vf_resources(bp, *num_vfs);
	if (rc)
		goto err_out1;

	/* Reserve resources for VFs */
	rc = bnxt_hwrm_func_cfg(bp, num_vfs);
	if (rc)
		goto err_out2;

	/* Register buffers for VFs */
	rc = bnxt_hwrm_func_buf_rgtr(bp);
	if (rc)
		goto err_out2;

	rc = pci_enable_sriov(bp->pdev, *num_vfs);
	if (rc)
		goto err_out2;

	return 0;

err_out2:
	/* Free the resources reserved for various VF's */
	bnxt_hwrm_func_vf_resource_free(bp, *num_vfs);

err_out1:
	bnxt_free_vf_resources(bp);

	return rc;
}

void bnxt_sriov_disable(struct bnxt *bp)
{
	u16 num_vfs = pci_num_vf(bp->pdev);

	if (!num_vfs)
		return;

	if (pci_vfs_assigned(bp->pdev)) {
		netdev_warn(bp->dev, "Unable to free %d VFs because some are assigned to VMs.\n",
			    num_vfs);
	} else {
		pci_disable_sriov(bp->pdev);
		/* Free the HW resources reserved for various VF's */
		bnxt_hwrm_func_vf_resource_free(bp, num_vfs);
	}

	bnxt_free_vf_resources(bp);

	bp->pf.active_vfs = 0;
	bp->pf.max_pf_rx_rings = bp->pf.max_rx_rings;
	bp->pf.max_pf_tx_rings = bp->pf.max_tx_rings;
}

int bnxt_sriov_configure(struct pci_dev *pdev, int num_vfs)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct bnxt *bp = netdev_priv(dev);

	if (!(bp->flags & BNXT_FLAG_USING_MSIX)) {
		netdev_warn(dev, "Not allow SRIOV if the irq mode is not MSIX\n");
		return 0;
	}

	rtnl_lock();
	if (!netif_running(dev)) {
		netdev_warn(dev, "Reject SRIOV config request since if is down!\n");
		rtnl_unlock();
		return 0;
	}
	bp->sriov_cfg = true;
	rtnl_unlock();

	if (pci_vfs_assigned(bp->pdev)) {
		netdev_warn(dev, "Unable to configure SRIOV since some VFs are assigned to VMs.\n");
		num_vfs = 0;
		goto sriov_cfg_exit;
	}

	/* Check if enabled VFs is same as requested */
	if (num_vfs && num_vfs == bp->pf.active_vfs)
		goto sriov_cfg_exit;

	/* if there are previous existing VFs, clean them up */
	bnxt_sriov_disable(bp);
	if (!num_vfs)
		goto sriov_cfg_exit;

	bnxt_sriov_enable(bp, &num_vfs);

sriov_cfg_exit:
	bp->sriov_cfg = false;
	wake_up(&bp->sriov_cfg_wait);

	return num_vfs;
}

static int bnxt_hwrm_fwd_resp(struct bnxt *bp, struct bnxt_vf_info *vf,
			      void *encap_resp, __le64 encap_resp_addr,
			      __le16 encap_resp_cpr, u32 msg_size)
{
	int rc = 0;
	struct hwrm_fwd_resp_input req = {0};
	struct hwrm_fwd_resp_output *resp = bp->hwrm_cmd_resp_addr;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_FWD_RESP, -1, -1);

	/* Set the new target id */
	req.target_id = cpu_to_le16(vf->fw_fid);
	req.encap_resp_len = cpu_to_le16(msg_size);
	req.encap_resp_addr = encap_resp_addr;
	req.encap_resp_cmpl_ring = encap_resp_cpr;
	memcpy(req.encap_resp, encap_resp, msg_size);

	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);

	if (rc) {
		netdev_err(bp->dev, "hwrm_fwd_resp failed. rc:%d\n", rc);
		goto fwd_resp_exit;
	}

	if (resp->error_code) {
		netdev_err(bp->dev, "hwrm_fwd_resp error %d\n",
			   resp->error_code);
		rc = -1;
	}

fwd_resp_exit:
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

static int bnxt_hwrm_fwd_err_resp(struct bnxt *bp, struct bnxt_vf_info *vf,
				  u32 msg_size)
{
	int rc = 0;
	struct hwrm_reject_fwd_resp_input req = {0};
	struct hwrm_reject_fwd_resp_output *resp = bp->hwrm_cmd_resp_addr;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_REJECT_FWD_RESP, -1, -1);
	/* Set the new target id */
	req.target_id = cpu_to_le16(vf->fw_fid);
	memcpy(req.encap_request, vf->hwrm_cmd_req_addr, msg_size);

	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);

	if (rc) {
		netdev_err(bp->dev, "hwrm_fwd_err_resp failed. rc:%d\n", rc);
		goto fwd_err_resp_exit;
	}

	if (resp->error_code) {
		netdev_err(bp->dev, "hwrm_fwd_err_resp error %d\n",
			   resp->error_code);
		rc = -1;
	}

fwd_err_resp_exit:
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

static int bnxt_hwrm_exec_fwd_resp(struct bnxt *bp, struct bnxt_vf_info *vf,
				   u32 msg_size)
{
	int rc = 0;
	struct hwrm_exec_fwd_resp_input req = {0};
	struct hwrm_exec_fwd_resp_output *resp = bp->hwrm_cmd_resp_addr;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_EXEC_FWD_RESP, -1, -1);
	/* Set the new target id */
	req.target_id = cpu_to_le16(vf->fw_fid);
	memcpy(req.encap_request, vf->hwrm_cmd_req_addr, msg_size);

	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);

	if (rc) {
		netdev_err(bp->dev, "hwrm_exec_fw_resp failed. rc:%d\n", rc);
		goto exec_fwd_resp_exit;
	}

	if (resp->error_code) {
		netdev_err(bp->dev, "hwrm_exec_fw_resp error %d\n",
			   resp->error_code);
		rc = -1;
	}

exec_fwd_resp_exit:
	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

static int bnxt_vf_validate_set_mac(struct bnxt *bp, struct bnxt_vf_info *vf)
{
	u32 msg_size = sizeof(struct hwrm_cfa_l2_filter_alloc_input);
	struct hwrm_cfa_l2_filter_alloc_input *req =
		(struct hwrm_cfa_l2_filter_alloc_input *)vf->hwrm_cmd_req_addr;

	if (!is_valid_ether_addr(vf->mac_addr) ||
	    ether_addr_equal((const u8 *)req->l2_addr, vf->mac_addr))
		return bnxt_hwrm_exec_fwd_resp(bp, vf, msg_size);
	else
		return bnxt_hwrm_fwd_err_resp(bp, vf, msg_size);
}

static int bnxt_vf_set_link(struct bnxt *bp, struct bnxt_vf_info *vf)
{
	int rc = 0;

	if (!(vf->flags & BNXT_VF_LINK_FORCED)) {
		/* real link */
		rc = bnxt_hwrm_exec_fwd_resp(
			bp, vf, sizeof(struct hwrm_port_phy_qcfg_input));
	} else {
		struct hwrm_port_phy_qcfg_output phy_qcfg_resp;
		struct hwrm_port_phy_qcfg_input *phy_qcfg_req;

		phy_qcfg_req =
		(struct hwrm_port_phy_qcfg_input *)vf->hwrm_cmd_req_addr;
		mutex_lock(&bp->hwrm_cmd_lock);
		memcpy(&phy_qcfg_resp, &bp->link_info.phy_qcfg_resp,
		       sizeof(phy_qcfg_resp));
		mutex_unlock(&bp->hwrm_cmd_lock);
		phy_qcfg_resp.seq_id = phy_qcfg_req->seq_id;

		if (vf->flags & BNXT_VF_LINK_UP) {
			/* if physical link is down, force link up on VF */
			if (phy_qcfg_resp.link ==
			    PORT_PHY_QCFG_RESP_LINK_NO_LINK) {
				phy_qcfg_resp.link =
					PORT_PHY_QCFG_RESP_LINK_LINK;
				if (phy_qcfg_resp.auto_link_speed)
					phy_qcfg_resp.link_speed =
						phy_qcfg_resp.auto_link_speed;
				else
					phy_qcfg_resp.link_speed =
						phy_qcfg_resp.force_link_speed;
				phy_qcfg_resp.duplex =
					PORT_PHY_QCFG_RESP_DUPLEX_FULL;
				phy_qcfg_resp.pause =
					(PORT_PHY_QCFG_RESP_PAUSE_TX |
					 PORT_PHY_QCFG_RESP_PAUSE_RX);
			}
		} else {
			/* force link down */
			phy_qcfg_resp.link = PORT_PHY_QCFG_RESP_LINK_NO_LINK;
			phy_qcfg_resp.link_speed = 0;
			phy_qcfg_resp.duplex = PORT_PHY_QCFG_RESP_DUPLEX_HALF;
			phy_qcfg_resp.pause = 0;
		}
		rc = bnxt_hwrm_fwd_resp(bp, vf, &phy_qcfg_resp,
					phy_qcfg_req->resp_addr,
					phy_qcfg_req->cmpl_ring,
					sizeof(phy_qcfg_resp));
	}
	return rc;
}

static int bnxt_vf_req_validate_snd(struct bnxt *bp, struct bnxt_vf_info *vf)
{
	int rc = 0;
	struct hwrm_cmd_req_hdr *encap_req = vf->hwrm_cmd_req_addr;
	u32 req_type = le32_to_cpu(encap_req->cmpl_ring_req_type) & 0xffff;

	switch (req_type) {
	case HWRM_CFA_L2_FILTER_ALLOC:
		rc = bnxt_vf_validate_set_mac(bp, vf);
		break;
	case HWRM_FUNC_CFG:
		/* TODO Validate if VF is allowed to change mac address,
		 * mtu, num of rings etc
		 */
		rc = bnxt_hwrm_exec_fwd_resp(
			bp, vf, sizeof(struct hwrm_func_cfg_input));
		break;
	case HWRM_PORT_PHY_QCFG:
		rc = bnxt_vf_set_link(bp, vf);
		break;
	default:
		break;
	}
	return rc;
}

void bnxt_hwrm_exec_fwd_req(struct bnxt *bp)
{
	u32 i = 0, active_vfs = bp->pf.active_vfs, vf_id;

	/* Scan through VF's and process commands */
	while (1) {
		vf_id = find_next_bit(bp->pf.vf_event_bmap, active_vfs, i);
		if (vf_id >= active_vfs)
			break;

		clear_bit(vf_id, bp->pf.vf_event_bmap);
		bnxt_vf_req_validate_snd(bp, &bp->pf.vf[vf_id]);
		i = vf_id + 1;
	}
}

void bnxt_update_vf_mac(struct bnxt *bp)
{
	struct hwrm_func_qcaps_input req = {0};
	struct hwrm_func_qcaps_output *resp = bp->hwrm_cmd_resp_addr;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_FUNC_QCAPS, -1, -1);
	req.fid = cpu_to_le16(0xffff);

	mutex_lock(&bp->hwrm_cmd_lock);
	if (_hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT))
		goto update_vf_mac_exit;

	if (!is_valid_ether_addr(resp->perm_mac_address))
		goto update_vf_mac_exit;

	if (ether_addr_equal(resp->perm_mac_address, bp->vf.mac_addr))
		goto update_vf_mac_exit;

	memcpy(bp->vf.mac_addr, resp->perm_mac_address, ETH_ALEN);
	memcpy(bp->dev->dev_addr, bp->vf.mac_addr, ETH_ALEN);
update_vf_mac_exit:
	mutex_unlock(&bp->hwrm_cmd_lock);
}

#else

void bnxt_sriov_disable(struct bnxt *bp)
{
}

void bnxt_hwrm_exec_fwd_req(struct bnxt *bp)
{
	netdev_err(bp->dev, "Invalid VF message received when SRIOV is not enable\n");
}

void bnxt_update_vf_mac(struct bnxt *bp)
{
}
#endif
