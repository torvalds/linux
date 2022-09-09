/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2016-2017 Broadcom Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/rtnetlink.h>
#include <linux/jhash.h>
#include <net/pkt_cls.h>

#include "bnxt_hsi.h"
#include "bnxt.h"
#include "bnxt_hwrm.h"
#include "bnxt_vfr.h"
#include "bnxt_devlink.h"
#include "bnxt_tc.h"

#ifdef CONFIG_BNXT_SRIOV

#define CFA_HANDLE_INVALID		0xffff
#define VF_IDX_INVALID			0xffff

static int hwrm_cfa_vfr_alloc(struct bnxt *bp, u16 vf_idx,
			      u16 *tx_cfa_action, u16 *rx_cfa_code)
{
	struct hwrm_cfa_vfr_alloc_output *resp;
	struct hwrm_cfa_vfr_alloc_input *req;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_CFA_VFR_ALLOC);
	if (!rc) {
		req->vf_id = cpu_to_le16(vf_idx);
		sprintf(req->vfr_name, "vfr%d", vf_idx);

		resp = hwrm_req_hold(bp, req);
		rc = hwrm_req_send(bp, req);
		if (!rc) {
			*tx_cfa_action = le16_to_cpu(resp->tx_cfa_action);
			*rx_cfa_code = le16_to_cpu(resp->rx_cfa_code);
			netdev_dbg(bp->dev, "tx_cfa_action=0x%x, rx_cfa_code=0x%x",
				   *tx_cfa_action, *rx_cfa_code);
		}
		hwrm_req_drop(bp, req);
	}
	if (rc)
		netdev_info(bp->dev, "%s error rc=%d\n", __func__, rc);
	return rc;
}

static int hwrm_cfa_vfr_free(struct bnxt *bp, u16 vf_idx)
{
	struct hwrm_cfa_vfr_free_input *req;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_CFA_VFR_FREE);
	if (!rc) {
		sprintf(req->vfr_name, "vfr%d", vf_idx);
		rc = hwrm_req_send(bp, req);
	}
	if (rc)
		netdev_info(bp->dev, "%s error rc=%d\n", __func__, rc);
	return rc;
}

static int bnxt_hwrm_vfr_qcfg(struct bnxt *bp, struct bnxt_vf_rep *vf_rep,
			      u16 *max_mtu)
{
	struct hwrm_func_qcfg_output *resp;
	struct hwrm_func_qcfg_input *req;
	u16 mtu;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_FUNC_QCFG);
	if (rc)
		return rc;

	req->fid = cpu_to_le16(bp->pf.vf[vf_rep->vf_idx].fw_fid);
	resp = hwrm_req_hold(bp, req);
	rc = hwrm_req_send(bp, req);
	if (!rc) {
		mtu = le16_to_cpu(resp->max_mtu_configured);
		if (!mtu)
			*max_mtu = BNXT_MAX_MTU;
		else
			*max_mtu = mtu;
	}
	hwrm_req_drop(bp, req);
	return rc;
}

static int bnxt_vf_rep_open(struct net_device *dev)
{
	struct bnxt_vf_rep *vf_rep = netdev_priv(dev);
	struct bnxt *bp = vf_rep->bp;

	/* Enable link and TX only if the parent PF is open. */
	if (netif_running(bp->dev)) {
		netif_carrier_on(dev);
		netif_tx_start_all_queues(dev);
	}
	return 0;
}

static int bnxt_vf_rep_close(struct net_device *dev)
{
	netif_carrier_off(dev);
	netif_tx_disable(dev);

	return 0;
}

static netdev_tx_t bnxt_vf_rep_xmit(struct sk_buff *skb,
				    struct net_device *dev)
{
	struct bnxt_vf_rep *vf_rep = netdev_priv(dev);
	int rc, len = skb->len;

	skb_dst_drop(skb);
	dst_hold((struct dst_entry *)vf_rep->dst);
	skb_dst_set(skb, (struct dst_entry *)vf_rep->dst);
	skb->dev = vf_rep->dst->u.port_info.lower_dev;

	rc = dev_queue_xmit(skb);
	if (!rc) {
		vf_rep->tx_stats.packets++;
		vf_rep->tx_stats.bytes += len;
	}
	return rc;
}

static void
bnxt_vf_rep_get_stats64(struct net_device *dev,
			struct rtnl_link_stats64 *stats)
{
	struct bnxt_vf_rep *vf_rep = netdev_priv(dev);

	stats->rx_packets = vf_rep->rx_stats.packets;
	stats->rx_bytes = vf_rep->rx_stats.bytes;
	stats->tx_packets = vf_rep->tx_stats.packets;
	stats->tx_bytes = vf_rep->tx_stats.bytes;
}

static int bnxt_vf_rep_setup_tc_block_cb(enum tc_setup_type type,
					 void *type_data,
					 void *cb_priv)
{
	struct bnxt_vf_rep *vf_rep = cb_priv;
	struct bnxt *bp = vf_rep->bp;
	int vf_fid = bp->pf.vf[vf_rep->vf_idx].fw_fid;

	if (!bnxt_tc_flower_enabled(vf_rep->bp) ||
	    !tc_cls_can_offload_and_chain0(bp->dev, type_data))
		return -EOPNOTSUPP;

	switch (type) {
	case TC_SETUP_CLSFLOWER:
		return bnxt_tc_setup_flower(bp, vf_fid, type_data);
	default:
		return -EOPNOTSUPP;
	}
}

static LIST_HEAD(bnxt_vf_block_cb_list);

static int bnxt_vf_rep_setup_tc(struct net_device *dev, enum tc_setup_type type,
				void *type_data)
{
	struct bnxt_vf_rep *vf_rep = netdev_priv(dev);

	switch (type) {
	case TC_SETUP_BLOCK:
		return flow_block_cb_setup_simple(type_data,
						  &bnxt_vf_block_cb_list,
						  bnxt_vf_rep_setup_tc_block_cb,
						  vf_rep, vf_rep, true);
	default:
		return -EOPNOTSUPP;
	}
}

struct net_device *bnxt_get_vf_rep(struct bnxt *bp, u16 cfa_code)
{
	u16 vf_idx;

	if (cfa_code && bp->cfa_code_map && BNXT_PF(bp)) {
		vf_idx = bp->cfa_code_map[cfa_code];
		if (vf_idx != VF_IDX_INVALID)
			return bp->vf_reps[vf_idx]->dev;
	}
	return NULL;
}

void bnxt_vf_rep_rx(struct bnxt *bp, struct sk_buff *skb)
{
	struct bnxt_vf_rep *vf_rep = netdev_priv(skb->dev);

	vf_rep->rx_stats.bytes += skb->len;
	vf_rep->rx_stats.packets++;

	netif_receive_skb(skb);
}

static int bnxt_vf_rep_get_phys_port_name(struct net_device *dev, char *buf,
					  size_t len)
{
	struct bnxt_vf_rep *vf_rep = netdev_priv(dev);
	struct pci_dev *pf_pdev = vf_rep->bp->pdev;
	int rc;

	rc = snprintf(buf, len, "pf%dvf%d", PCI_FUNC(pf_pdev->devfn),
		      vf_rep->vf_idx);
	if (rc >= len)
		return -EOPNOTSUPP;
	return 0;
}

static void bnxt_vf_rep_get_drvinfo(struct net_device *dev,
				    struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, DRV_MODULE_NAME, sizeof(info->driver));
}

static int bnxt_vf_rep_get_port_parent_id(struct net_device *dev,
					  struct netdev_phys_item_id *ppid)
{
	struct bnxt_vf_rep *vf_rep = netdev_priv(dev);

	/* as only PORT_PARENT_ID is supported currently use common code
	 * between PF and VF-rep for now.
	 */
	return bnxt_get_port_parent_id(vf_rep->bp->dev, ppid);
}

static const struct ethtool_ops bnxt_vf_rep_ethtool_ops = {
	.get_drvinfo		= bnxt_vf_rep_get_drvinfo
};

static const struct net_device_ops bnxt_vf_rep_netdev_ops = {
	.ndo_open		= bnxt_vf_rep_open,
	.ndo_stop		= bnxt_vf_rep_close,
	.ndo_start_xmit		= bnxt_vf_rep_xmit,
	.ndo_get_stats64	= bnxt_vf_rep_get_stats64,
	.ndo_setup_tc		= bnxt_vf_rep_setup_tc,
	.ndo_get_port_parent_id	= bnxt_vf_rep_get_port_parent_id,
	.ndo_get_phys_port_name = bnxt_vf_rep_get_phys_port_name
};

bool bnxt_dev_is_vf_rep(struct net_device *dev)
{
	return dev->netdev_ops == &bnxt_vf_rep_netdev_ops;
}

/* Called when the parent PF interface is closed:
 * As the mode transition from SWITCHDEV to LEGACY
 * happens under the rtnl_lock() this routine is safe
 * under the rtnl_lock()
 */
void bnxt_vf_reps_close(struct bnxt *bp)
{
	struct bnxt_vf_rep *vf_rep;
	u16 num_vfs, i;

	if (bp->eswitch_mode != DEVLINK_ESWITCH_MODE_SWITCHDEV)
		return;

	num_vfs = pci_num_vf(bp->pdev);
	for (i = 0; i < num_vfs; i++) {
		vf_rep = bp->vf_reps[i];
		if (netif_running(vf_rep->dev))
			bnxt_vf_rep_close(vf_rep->dev);
	}
}

/* Called when the parent PF interface is opened (re-opened):
 * As the mode transition from SWITCHDEV to LEGACY
 * happen under the rtnl_lock() this routine is safe
 * under the rtnl_lock()
 */
void bnxt_vf_reps_open(struct bnxt *bp)
{
	int i;

	if (bp->eswitch_mode != DEVLINK_ESWITCH_MODE_SWITCHDEV)
		return;

	for (i = 0; i < pci_num_vf(bp->pdev); i++) {
		/* Open the VF-Rep only if it is allocated in the FW */
		if (bp->vf_reps[i]->tx_cfa_action != CFA_HANDLE_INVALID)
			bnxt_vf_rep_open(bp->vf_reps[i]->dev);
	}
}

static void __bnxt_free_one_vf_rep(struct bnxt *bp, struct bnxt_vf_rep *vf_rep)
{
	if (!vf_rep)
		return;

	if (vf_rep->dst) {
		dst_release((struct dst_entry *)vf_rep->dst);
		vf_rep->dst = NULL;
	}
	if (vf_rep->tx_cfa_action != CFA_HANDLE_INVALID) {
		hwrm_cfa_vfr_free(bp, vf_rep->vf_idx);
		vf_rep->tx_cfa_action = CFA_HANDLE_INVALID;
	}
}

static void __bnxt_vf_reps_destroy(struct bnxt *bp)
{
	u16 num_vfs = pci_num_vf(bp->pdev);
	struct bnxt_vf_rep *vf_rep;
	int i;

	for (i = 0; i < num_vfs; i++) {
		vf_rep = bp->vf_reps[i];
		if (vf_rep) {
			__bnxt_free_one_vf_rep(bp, vf_rep);
			if (vf_rep->dev) {
				/* if register_netdev failed, then netdev_ops
				 * would have been set to NULL
				 */
				if (vf_rep->dev->netdev_ops)
					unregister_netdev(vf_rep->dev);
				free_netdev(vf_rep->dev);
			}
		}
	}

	kfree(bp->vf_reps);
	bp->vf_reps = NULL;
}

void bnxt_vf_reps_destroy(struct bnxt *bp)
{
	bool closed = false;

	if (bp->eswitch_mode != DEVLINK_ESWITCH_MODE_SWITCHDEV)
		return;

	if (!bp->vf_reps)
		return;

	/* Ensure that parent PF's and VF-reps' RX/TX has been quiesced
	 * before proceeding with VF-rep cleanup.
	 */
	rtnl_lock();
	if (netif_running(bp->dev)) {
		bnxt_close_nic(bp, false, false);
		closed = true;
	}
	/* un-publish cfa_code_map so that RX path can't see it anymore */
	kfree(bp->cfa_code_map);
	bp->cfa_code_map = NULL;
	bp->eswitch_mode = DEVLINK_ESWITCH_MODE_LEGACY;

	if (closed)
		bnxt_open_nic(bp, false, false);
	rtnl_unlock();

	/* Need to call vf_reps_destroy() outside of rntl_lock
	 * as unregister_netdev takes rtnl_lock
	 */
	__bnxt_vf_reps_destroy(bp);
}

/* Free the VF-Reps in firmware, during firmware hot-reset processing.
 * Note that the VF-Rep netdevs are still active (not unregistered) during
 * this process. As the mode transition from SWITCHDEV to LEGACY happens
 * under the rtnl_lock() this routine is safe under the rtnl_lock().
 */
void bnxt_vf_reps_free(struct bnxt *bp)
{
	u16 num_vfs = pci_num_vf(bp->pdev);
	int i;

	if (bp->eswitch_mode != DEVLINK_ESWITCH_MODE_SWITCHDEV)
		return;

	for (i = 0; i < num_vfs; i++)
		__bnxt_free_one_vf_rep(bp, bp->vf_reps[i]);
}

static int bnxt_alloc_vf_rep(struct bnxt *bp, struct bnxt_vf_rep *vf_rep,
			     u16 *cfa_code_map)
{
	/* get cfa handles from FW */
	if (hwrm_cfa_vfr_alloc(bp, vf_rep->vf_idx, &vf_rep->tx_cfa_action,
			       &vf_rep->rx_cfa_code))
		return -ENOLINK;

	cfa_code_map[vf_rep->rx_cfa_code] = vf_rep->vf_idx;
	vf_rep->dst = metadata_dst_alloc(0, METADATA_HW_PORT_MUX, GFP_KERNEL);
	if (!vf_rep->dst)
		return -ENOMEM;

	/* only cfa_action is needed to mux a packet while TXing */
	vf_rep->dst->u.port_info.port_id = vf_rep->tx_cfa_action;
	vf_rep->dst->u.port_info.lower_dev = bp->dev;

	return 0;
}

/* Allocate the VF-Reps in firmware, during firmware hot-reset processing.
 * Note that the VF-Rep netdevs are still active (not unregistered) during
 * this process. As the mode transition from SWITCHDEV to LEGACY happens
 * under the rtnl_lock() this routine is safe under the rtnl_lock().
 */
int bnxt_vf_reps_alloc(struct bnxt *bp)
{
	u16 *cfa_code_map = bp->cfa_code_map, num_vfs = pci_num_vf(bp->pdev);
	struct bnxt_vf_rep *vf_rep;
	int rc, i;

	if (bp->eswitch_mode != DEVLINK_ESWITCH_MODE_SWITCHDEV)
		return 0;

	if (!cfa_code_map)
		return -EINVAL;

	for (i = 0; i < MAX_CFA_CODE; i++)
		cfa_code_map[i] = VF_IDX_INVALID;

	for (i = 0; i < num_vfs; i++) {
		vf_rep = bp->vf_reps[i];
		vf_rep->vf_idx = i;

		rc = bnxt_alloc_vf_rep(bp, vf_rep, cfa_code_map);
		if (rc)
			goto err;
	}

	return 0;

err:
	netdev_info(bp->dev, "%s error=%d\n", __func__, rc);
	bnxt_vf_reps_free(bp);
	return rc;
}

/* Use the OUI of the PF's perm addr and report the same mac addr
 * for the same VF-rep each time
 */
static void bnxt_vf_rep_eth_addr_gen(u8 *src_mac, u16 vf_idx, u8 *mac)
{
	u32 addr;

	ether_addr_copy(mac, src_mac);

	addr = jhash(src_mac, ETH_ALEN, 0) + vf_idx;
	mac[3] = (u8)(addr & 0xFF);
	mac[4] = (u8)((addr >> 8) & 0xFF);
	mac[5] = (u8)((addr >> 16) & 0xFF);
}

static void bnxt_vf_rep_netdev_init(struct bnxt *bp, struct bnxt_vf_rep *vf_rep,
				    struct net_device *dev)
{
	struct net_device *pf_dev = bp->dev;
	u16 max_mtu;

	dev->netdev_ops = &bnxt_vf_rep_netdev_ops;
	dev->ethtool_ops = &bnxt_vf_rep_ethtool_ops;
	/* Just inherit all the featues of the parent PF as the VF-R
	 * uses the RX/TX rings of the parent PF
	 */
	dev->hw_features = pf_dev->hw_features;
	dev->gso_partial_features = pf_dev->gso_partial_features;
	dev->vlan_features = pf_dev->vlan_features;
	dev->hw_enc_features = pf_dev->hw_enc_features;
	dev->features |= pf_dev->features;
	bnxt_vf_rep_eth_addr_gen(bp->pf.mac_addr, vf_rep->vf_idx,
				 dev->perm_addr);
	eth_hw_addr_set(dev, dev->perm_addr);
	/* Set VF-Rep's max-mtu to the corresponding VF's max-mtu */
	if (!bnxt_hwrm_vfr_qcfg(bp, vf_rep, &max_mtu))
		dev->max_mtu = max_mtu;
	dev->min_mtu = ETH_ZLEN;
}

static int bnxt_vf_reps_create(struct bnxt *bp)
{
	u16 *cfa_code_map = NULL, num_vfs = pci_num_vf(bp->pdev);
	struct bnxt_vf_rep *vf_rep;
	struct net_device *dev;
	int rc, i;

	if (!(bp->flags & BNXT_FLAG_DSN_VALID))
		return -ENODEV;

	bp->vf_reps = kcalloc(num_vfs, sizeof(vf_rep), GFP_KERNEL);
	if (!bp->vf_reps)
		return -ENOMEM;

	/* storage for cfa_code to vf-idx mapping */
	cfa_code_map = kmalloc_array(MAX_CFA_CODE, sizeof(*bp->cfa_code_map),
				     GFP_KERNEL);
	if (!cfa_code_map) {
		rc = -ENOMEM;
		goto err;
	}
	for (i = 0; i < MAX_CFA_CODE; i++)
		cfa_code_map[i] = VF_IDX_INVALID;

	for (i = 0; i < num_vfs; i++) {
		dev = alloc_etherdev(sizeof(*vf_rep));
		if (!dev) {
			rc = -ENOMEM;
			goto err;
		}

		vf_rep = netdev_priv(dev);
		bp->vf_reps[i] = vf_rep;
		vf_rep->dev = dev;
		vf_rep->bp = bp;
		vf_rep->vf_idx = i;
		vf_rep->tx_cfa_action = CFA_HANDLE_INVALID;

		rc = bnxt_alloc_vf_rep(bp, vf_rep, cfa_code_map);
		if (rc)
			goto err;

		bnxt_vf_rep_netdev_init(bp, vf_rep, dev);
		rc = register_netdev(dev);
		if (rc) {
			/* no need for unregister_netdev in cleanup */
			dev->netdev_ops = NULL;
			goto err;
		}
	}

	/* publish cfa_code_map only after all VF-reps have been initialized */
	bp->cfa_code_map = cfa_code_map;
	bp->eswitch_mode = DEVLINK_ESWITCH_MODE_SWITCHDEV;
	netif_keep_dst(bp->dev);
	return 0;

err:
	netdev_info(bp->dev, "%s error=%d\n", __func__, rc);
	kfree(cfa_code_map);
	__bnxt_vf_reps_destroy(bp);
	return rc;
}

/* Devlink related routines */
int bnxt_dl_eswitch_mode_get(struct devlink *devlink, u16 *mode)
{
	struct bnxt *bp = bnxt_get_bp_from_dl(devlink);

	*mode = bp->eswitch_mode;
	return 0;
}

int bnxt_dl_eswitch_mode_set(struct devlink *devlink, u16 mode,
			     struct netlink_ext_ack *extack)
{
	struct bnxt *bp = bnxt_get_bp_from_dl(devlink);

	if (bp->eswitch_mode == mode) {
		netdev_info(bp->dev, "already in %s eswitch mode\n",
			    mode == DEVLINK_ESWITCH_MODE_LEGACY ?
			    "legacy" : "switchdev");
		return -EINVAL;
	}

	switch (mode) {
	case DEVLINK_ESWITCH_MODE_LEGACY:
		bnxt_vf_reps_destroy(bp);
		return 0;

	case DEVLINK_ESWITCH_MODE_SWITCHDEV:
		if (bp->hwrm_spec_code < 0x10803) {
			netdev_warn(bp->dev, "FW does not support SRIOV E-Switch SWITCHDEV mode\n");
			return -ENOTSUPP;
		}

		if (pci_num_vf(bp->pdev) == 0) {
			netdev_info(bp->dev, "Enable VFs before setting switchdev mode\n");
			return -EPERM;
		}
		return bnxt_vf_reps_create(bp);

	default:
		return -EINVAL;
	}
}

#endif
