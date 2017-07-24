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

#include "bnxt_hsi.h"
#include "bnxt.h"
#include "bnxt_vfr.h"

#define CFA_HANDLE_INVALID		0xffff

static void __bnxt_vf_reps_destroy(struct bnxt *bp)
{
	u16 num_vfs = pci_num_vf(bp->pdev);
	struct bnxt_vf_rep *vf_rep;
	int i;

	for (i = 0; i < num_vfs; i++) {
		vf_rep = bp->vf_reps[i];
		if (vf_rep) {
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
	bp->eswitch_mode = DEVLINK_ESWITCH_MODE_LEGACY;

	if (closed)
		bnxt_open_nic(bp, false, false);
	rtnl_unlock();

	/* Need to call vf_reps_destroy() outside of rntl_lock
	 * as unregister_netdev takes rtnl_lock
	 */
	__bnxt_vf_reps_destroy(bp);
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
	ether_addr_copy(dev->dev_addr, dev->perm_addr);
}

static int bnxt_vf_reps_create(struct bnxt *bp)
{
	u16 num_vfs = pci_num_vf(bp->pdev);
	struct bnxt_vf_rep *vf_rep;
	struct net_device *dev;
	int rc, i;

	bp->vf_reps = kcalloc(num_vfs, sizeof(vf_rep), GFP_KERNEL);
	if (!bp->vf_reps)
		return -ENOMEM;

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

		bnxt_vf_rep_netdev_init(bp, vf_rep, dev);
		rc = register_netdev(dev);
		if (rc) {
			/* no need for unregister_netdev in cleanup */
			dev->netdev_ops = NULL;
			goto err;
		}
	}

	bp->eswitch_mode = DEVLINK_ESWITCH_MODE_SWITCHDEV;
	return 0;

err:
	netdev_info(bp->dev, "%s error=%d", __func__, rc);
	__bnxt_vf_reps_destroy(bp);
	return rc;
}

/* Devlink related routines */
static int bnxt_dl_eswitch_mode_get(struct devlink *devlink, u16 *mode)
{
	struct bnxt *bp = bnxt_get_bp_from_dl(devlink);

	*mode = bp->eswitch_mode;
	return 0;
}

static int bnxt_dl_eswitch_mode_set(struct devlink *devlink, u16 mode)
{
	struct bnxt *bp = bnxt_get_bp_from_dl(devlink);
	int rc = 0;

	mutex_lock(&bp->sriov_lock);
	if (bp->eswitch_mode == mode) {
		netdev_info(bp->dev, "already in %s eswitch mode",
			    mode == DEVLINK_ESWITCH_MODE_LEGACY ?
			    "legacy" : "switchdev");
		rc = -EINVAL;
		goto done;
	}

	switch (mode) {
	case DEVLINK_ESWITCH_MODE_LEGACY:
		bnxt_vf_reps_destroy(bp);
		break;

	case DEVLINK_ESWITCH_MODE_SWITCHDEV:
		if (pci_num_vf(bp->pdev) == 0) {
			netdev_info(bp->dev,
				    "Enable VFs before setting swtichdev mode");
			rc = -EPERM;
			goto done;
		}
		rc = bnxt_vf_reps_create(bp);
		break;

	default:
		rc = -EINVAL;
		goto done;
	}
done:
	mutex_unlock(&bp->sriov_lock);
	return rc;
}

static const struct devlink_ops bnxt_dl_ops = {
	.eswitch_mode_set = bnxt_dl_eswitch_mode_set,
	.eswitch_mode_get = bnxt_dl_eswitch_mode_get
};

int bnxt_dl_register(struct bnxt *bp)
{
	struct devlink *dl;
	int rc;

	if (!pci_find_ext_capability(bp->pdev, PCI_EXT_CAP_ID_SRIOV))
		return 0;

	if (bp->hwrm_spec_code < 0x10800) {
		netdev_warn(bp->dev, "Firmware does not support SR-IOV E-Switch SWITCHDEV mode.\n");
		return -ENOTSUPP;
	}

	dl = devlink_alloc(&bnxt_dl_ops, sizeof(struct bnxt_dl));
	if (!dl) {
		netdev_warn(bp->dev, "devlink_alloc failed");
		return -ENOMEM;
	}

	bnxt_link_bp_to_dl(dl, bp);
	bp->eswitch_mode = DEVLINK_ESWITCH_MODE_LEGACY;
	rc = devlink_register(dl, &bp->pdev->dev);
	if (rc) {
		bnxt_link_bp_to_dl(dl, NULL);
		devlink_free(dl);
		netdev_warn(bp->dev, "devlink_register failed. rc=%d", rc);
		return rc;
	}

	return 0;
}

void bnxt_dl_unregister(struct bnxt *bp)
{
	struct devlink *dl = bp->dl;

	if (!dl)
		return;

	devlink_unregister(dl);
	devlink_free(dl);
}
