/*
 * This file is part of the Chelsio T4 Ethernet driver for Linux.
 *
 * Copyright (c) 2015 Chelsio Communications, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifdef CONFIG_CHELSIO_T4_FCOE

#include <scsi/fc/fc_fs.h>
#include <scsi/libfcoe.h>
#include "cxgb4.h"

bool cxgb_fcoe_sof_eof_supported(struct adapter *adap, struct sk_buff *skb)
{
	struct fcoe_hdr *fcoeh = (struct fcoe_hdr *)skb_network_header(skb);
	u8 sof = fcoeh->fcoe_sof;
	u8 eof = 0;

	if ((sof != FC_SOF_I3) && (sof != FC_SOF_N3)) {
		dev_err(adap->pdev_dev, "Unsupported SOF 0x%x\n", sof);
		return false;
	}

	skb_copy_bits(skb, skb->len - 4, &eof, 1);

	if ((eof != FC_EOF_N) && (eof != FC_EOF_T)) {
		dev_err(adap->pdev_dev, "Unsupported EOF 0x%x\n", eof);
		return false;
	}

	return true;
}

/**
 * cxgb_fcoe_enable - enable FCoE offload features
 * @netdev: net device
 *
 * Returns 0 on success or -EINVAL on failure.
 */
int cxgb_fcoe_enable(struct net_device *netdev)
{
	struct port_info *pi = netdev_priv(netdev);
	struct adapter *adap = pi->adapter;
	struct cxgb_fcoe *fcoe = &pi->fcoe;

	if (is_t4(adap->params.chip))
		return -EINVAL;

	if (!(adap->flags & FULL_INIT_DONE))
		return -EINVAL;

	dev_info(adap->pdev_dev, "Enabling FCoE offload features\n");

	netdev->features |= NETIF_F_FCOE_CRC;
	netdev->vlan_features |= NETIF_F_FCOE_CRC;
	netdev->features |= NETIF_F_FCOE_MTU;
	netdev->vlan_features |= NETIF_F_FCOE_MTU;

	netdev_features_change(netdev);

	fcoe->flags |= CXGB_FCOE_ENABLED;

	return 0;
}

/**
 * cxgb_fcoe_disable - disable FCoE offload
 * @netdev: net device
 *
 * Returns 0 on success or -EINVAL on failure.
 */
int cxgb_fcoe_disable(struct net_device *netdev)
{
	struct port_info *pi = netdev_priv(netdev);
	struct adapter *adap = pi->adapter;
	struct cxgb_fcoe *fcoe = &pi->fcoe;

	if (!(fcoe->flags & CXGB_FCOE_ENABLED))
		return -EINVAL;

	dev_info(adap->pdev_dev, "Disabling FCoE offload features\n");

	fcoe->flags &= ~CXGB_FCOE_ENABLED;

	netdev->features &= ~NETIF_F_FCOE_CRC;
	netdev->vlan_features &= ~NETIF_F_FCOE_CRC;
	netdev->features &= ~NETIF_F_FCOE_MTU;
	netdev->vlan_features &= ~NETIF_F_FCOE_MTU;

	netdev_features_change(netdev);

	return 0;
}
#endif /* CONFIG_CHELSIO_T4_FCOE */
