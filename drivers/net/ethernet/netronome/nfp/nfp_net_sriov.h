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

#ifndef _NFP_NET_SRIOV_H_
#define _NFP_NET_SRIOV_H_

/**
 * SRIOV VF configuration.
 * The configuration memory begins with a mailbox region for communication with
 * the firmware followed by individual VF entries.
 */
#define NFP_NET_VF_CFG_SZ		16
#define NFP_NET_VF_CFG_MB_SZ		16

/* VF config mailbox */
#define NFP_NET_VF_CFG_MB				0x0
#define NFP_NET_VF_CFG_MB_CAP				0x0
#define   NFP_NET_VF_CFG_MB_CAP_MAC			  (0x1 << 0)
#define   NFP_NET_VF_CFG_MB_CAP_VLAN			  (0x1 << 1)
#define   NFP_NET_VF_CFG_MB_CAP_SPOOF			  (0x1 << 2)
#define   NFP_NET_VF_CFG_MB_CAP_LINK_STATE		  (0x1 << 3)
#define NFP_NET_VF_CFG_MB_RET				0x2
#define NFP_NET_VF_CFG_MB_UPD				0x4
#define   NFP_NET_VF_CFG_MB_UPD_MAC			  (0x1 << 0)
#define   NFP_NET_VF_CFG_MB_UPD_VLAN			  (0x1 << 1)
#define   NFP_NET_VF_CFG_MB_UPD_SPOOF			  (0x1 << 2)
#define   NFP_NET_VF_CFG_MB_UPD_LINK_STATE		  (0x1 << 3)
#define NFP_NET_VF_CFG_MB_VF_NUM			0x7

/* VF config entry
 * MAC_LO is set that the MAC address can be read in a single 6 byte read
 * by the NFP
 */
#define NFP_NET_VF_CFG_MAC				0x0
#define   NFP_NET_VF_CFG_MAC_HI				  0x0
#define   NFP_NET_VF_CFG_MAC_LO				  0x6
#define NFP_NET_VF_CFG_CTRL				0x4
#define   NFP_NET_VF_CFG_CTRL_SPOOF			  0x4
#define   NFP_NET_VF_CFG_CTRL_LINK_STATE		  0x3
#define     NFP_NET_VF_CFG_LS_MODE_AUTO			    0
#define     NFP_NET_VF_CFG_LS_MODE_ENABLE		    1
#define     NFP_NET_VF_CFG_LS_MODE_DISABLE		    2
#define NFP_NET_VF_CFG_VLAN				0x8
#define   NFP_NET_VF_CFG_VLAN_QOS			  0xe000
#define   NFP_NET_VF_CFG_VLAN_VID			  0x0fff

int nfp_app_set_vf_mac(struct net_device *netdev, int vf, u8 *mac);
int nfp_app_set_vf_vlan(struct net_device *netdev, int vf, u16 vlan, u8 qos,
			__be16 vlan_proto);
int nfp_app_set_vf_spoofchk(struct net_device *netdev, int vf, bool setting);
int nfp_app_set_vf_link_state(struct net_device *netdev, int vf,
			      int link_state);
int nfp_app_get_vf_config(struct net_device *netdev, int vf,
			  struct ifla_vf_info *ivi);

#endif /* _NFP_NET_SRIOV_H_ */
