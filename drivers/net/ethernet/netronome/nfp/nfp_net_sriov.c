/*
 * Copyright (C) 2017 Netronome Systems, Inc.
 *
 * This software is dual licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree or the BSD 2-Clause License provided below.  You have the
 * option to license this software under the complete terms of either license.
 *
 * The BSD 2-Clause License:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      1. Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
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

#include <linux/bitfield.h>
#include <linux/errno.h>
#include <linux/etherdevice.h>
#include <linux/if_link.h>
#include <linux/if_ether.h>

#include "nfpcore/nfp_cpp.h"
#include "nfp_app.h"
#include "nfp_main.h"
#include "nfp_net_ctrl.h"
#include "nfp_net.h"
#include "nfp_net_sriov.h"

static int
nfp_net_sriov_check(struct nfp_app *app, int vf, u16 cap, const char *msg)
{
	u16 cap_vf;

	if (!app || !app->pf->vfcfg_tbl2)
		return -EOPNOTSUPP;

	cap_vf = readw(app->pf->vfcfg_tbl2 + NFP_NET_VF_CFG_MB_CAP);
	if ((cap_vf & cap) != cap) {
		nfp_warn(app->pf->cpp, "ndo_set_vf_%s not supported\n", msg);
		return -EOPNOTSUPP;
	}

	if (vf < 0 || vf >= app->pf->num_vfs) {
		nfp_warn(app->pf->cpp, "invalid VF id %d\n", vf);
		return -EINVAL;
	}

	return 0;
}

static int
nfp_net_sriov_update(struct nfp_app *app, int vf, u16 update, const char *msg)
{
	struct nfp_net *nn;
	int ret;

	/* Write update info to mailbox in VF config symbol */
	writeb(vf, app->pf->vfcfg_tbl2 + NFP_NET_VF_CFG_MB_VF_NUM);
	writew(update, app->pf->vfcfg_tbl2 + NFP_NET_VF_CFG_MB_UPD);

	nn = list_first_entry(&app->pf->vnics, struct nfp_net, vnic_list);
	/* Signal VF reconfiguration */
	ret = nfp_net_reconfig(nn, NFP_NET_CFG_UPDATE_VF);
	if (ret)
		return ret;

	ret = readw(app->pf->vfcfg_tbl2 + NFP_NET_VF_CFG_MB_RET);
	if (ret)
		nfp_warn(app->pf->cpp,
			 "FW refused VF %s update with errno: %d\n", msg, ret);
	return -ret;
}

int nfp_app_set_vf_mac(struct net_device *netdev, int vf, u8 *mac)
{
	struct nfp_app *app = nfp_app_from_netdev(netdev);
	unsigned int vf_offset;
	int err;

	err = nfp_net_sriov_check(app, vf, NFP_NET_VF_CFG_MB_CAP_MAC, "mac");
	if (err)
		return err;

	if (is_multicast_ether_addr(mac)) {
		nfp_warn(app->pf->cpp,
			 "invalid Ethernet address %pM for VF id %d\n",
			 mac, vf);
		return -EINVAL;
	}

	/* Write MAC to VF entry in VF config symbol */
	vf_offset = NFP_NET_VF_CFG_MB_SZ + vf * NFP_NET_VF_CFG_SZ;
	writel(get_unaligned_be32(mac), app->pf->vfcfg_tbl2 + vf_offset);
	writew(get_unaligned_be16(mac + 4),
	       app->pf->vfcfg_tbl2 + vf_offset + NFP_NET_VF_CFG_MAC_LO);

	return nfp_net_sriov_update(app, vf, NFP_NET_VF_CFG_MB_UPD_MAC, "MAC");
}

int nfp_app_set_vf_vlan(struct net_device *netdev, int vf, u16 vlan, u8 qos,
			__be16 vlan_proto)
{
	struct nfp_app *app = nfp_app_from_netdev(netdev);
	unsigned int vf_offset;
	u16 vlan_tci;
	int err;

	err = nfp_net_sriov_check(app, vf, NFP_NET_VF_CFG_MB_CAP_VLAN, "vlan");
	if (err)
		return err;

	if (vlan_proto != htons(ETH_P_8021Q))
		return -EOPNOTSUPP;

	if (vlan > 4095 || qos > 7) {
		nfp_warn(app->pf->cpp,
			 "invalid vlan id or qos for VF id %d\n", vf);
		return -EINVAL;
	}

	/* Write VLAN tag to VF entry in VF config symbol */
	vlan_tci = FIELD_PREP(NFP_NET_VF_CFG_VLAN_VID, vlan) |
		FIELD_PREP(NFP_NET_VF_CFG_VLAN_QOS, qos);
	vf_offset = NFP_NET_VF_CFG_MB_SZ + vf * NFP_NET_VF_CFG_SZ;
	writew(vlan_tci, app->pf->vfcfg_tbl2 + vf_offset + NFP_NET_VF_CFG_VLAN);

	return nfp_net_sriov_update(app, vf, NFP_NET_VF_CFG_MB_UPD_VLAN,
				    "vlan");
}

int nfp_app_set_vf_spoofchk(struct net_device *netdev, int vf, bool enable)
{
	struct nfp_app *app = nfp_app_from_netdev(netdev);
	unsigned int vf_offset;
	u8 vf_ctrl;
	int err;

	err = nfp_net_sriov_check(app, vf, NFP_NET_VF_CFG_MB_CAP_SPOOF,
				  "spoofchk");
	if (err)
		return err;

	/* Write spoof check control bit to VF entry in VF config symbol */
	vf_offset = NFP_NET_VF_CFG_MB_SZ + vf * NFP_NET_VF_CFG_SZ +
		NFP_NET_VF_CFG_CTRL;
	vf_ctrl = readb(app->pf->vfcfg_tbl2 + vf_offset);
	vf_ctrl &= ~NFP_NET_VF_CFG_CTRL_SPOOF;
	vf_ctrl |= FIELD_PREP(NFP_NET_VF_CFG_CTRL_SPOOF, enable);
	writeb(vf_ctrl, app->pf->vfcfg_tbl2 + vf_offset);

	return nfp_net_sriov_update(app, vf, NFP_NET_VF_CFG_MB_UPD_SPOOF,
				    "spoofchk");
}

int nfp_app_set_vf_link_state(struct net_device *netdev, int vf,
			      int link_state)
{
	struct nfp_app *app = nfp_app_from_netdev(netdev);
	unsigned int vf_offset;
	u8 vf_ctrl;
	int err;

	err = nfp_net_sriov_check(app, vf, NFP_NET_VF_CFG_MB_CAP_LINK_STATE,
				  "link_state");
	if (err)
		return err;

	switch (link_state) {
	case IFLA_VF_LINK_STATE_AUTO:
	case IFLA_VF_LINK_STATE_ENABLE:
	case IFLA_VF_LINK_STATE_DISABLE:
		break;
	default:
		return -EINVAL;
	}

	/* Write link state to VF entry in VF config symbol */
	vf_offset = NFP_NET_VF_CFG_MB_SZ + vf * NFP_NET_VF_CFG_SZ +
		NFP_NET_VF_CFG_CTRL;
	vf_ctrl = readb(app->pf->vfcfg_tbl2 + vf_offset);
	vf_ctrl &= ~NFP_NET_VF_CFG_CTRL_LINK_STATE;
	vf_ctrl |= FIELD_PREP(NFP_NET_VF_CFG_CTRL_LINK_STATE, link_state);
	writeb(vf_ctrl, app->pf->vfcfg_tbl2 + vf_offset);

	return nfp_net_sriov_update(app, vf, NFP_NET_VF_CFG_MB_UPD_LINK_STATE,
				    "link state");
}

int nfp_app_get_vf_config(struct net_device *netdev, int vf,
			  struct ifla_vf_info *ivi)
{
	struct nfp_app *app = nfp_app_from_netdev(netdev);
	unsigned int vf_offset;
	u16 vlan_tci;
	u32 mac_hi;
	u16 mac_lo;
	u8 flags;
	int err;

	err = nfp_net_sriov_check(app, vf, 0, "");
	if (err)
		return err;

	vf_offset = NFP_NET_VF_CFG_MB_SZ + vf * NFP_NET_VF_CFG_SZ;

	mac_hi = readl(app->pf->vfcfg_tbl2 + vf_offset);
	mac_lo = readw(app->pf->vfcfg_tbl2 + vf_offset + NFP_NET_VF_CFG_MAC_LO);

	flags = readb(app->pf->vfcfg_tbl2 + vf_offset + NFP_NET_VF_CFG_CTRL);
	vlan_tci = readw(app->pf->vfcfg_tbl2 + vf_offset + NFP_NET_VF_CFG_VLAN);

	memset(ivi, 0, sizeof(*ivi));
	ivi->vf = vf;

	put_unaligned_be32(mac_hi, &ivi->mac[0]);
	put_unaligned_be16(mac_lo, &ivi->mac[4]);

	ivi->vlan = FIELD_GET(NFP_NET_VF_CFG_VLAN_VID, vlan_tci);
	ivi->qos = FIELD_GET(NFP_NET_VF_CFG_VLAN_QOS, vlan_tci);

	ivi->spoofchk = FIELD_GET(NFP_NET_VF_CFG_CTRL_SPOOF, flags);
	ivi->linkstate = FIELD_GET(NFP_NET_VF_CFG_CTRL_LINK_STATE, flags);

	return 0;
}
