// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2015 - 2025 Beijing WangXun Technology Co., Ltd. */

#include <linux/etherdevice.h>
#include <linux/pci.h>

#include "wx_type.h"
#include "wx_hw.h"
#include "wx_mbx.h"
#include "wx_sriov.h"

static void wx_vf_configuration(struct pci_dev *pdev, int event_mask)
{
	bool enable = !!WX_VF_ENABLE_CHECK(event_mask);
	struct wx *wx = pci_get_drvdata(pdev);
	u32 vfn = WX_VF_NUM_GET(event_mask);

	if (enable)
		eth_zero_addr(wx->vfinfo[vfn].vf_mac_addr);
}

static int wx_alloc_vf_macvlans(struct wx *wx, u8 num_vfs)
{
	struct vf_macvlans *mv_list;
	int num_vf_macvlans, i;

	/* Initialize list of VF macvlans */
	INIT_LIST_HEAD(&wx->vf_mvs.mvlist);

	num_vf_macvlans = wx->mac.num_rar_entries -
			  (WX_MAX_PF_MACVLANS + 1 + num_vfs);
	if (!num_vf_macvlans)
		return -EINVAL;

	mv_list = kcalloc(num_vf_macvlans, sizeof(struct vf_macvlans),
			  GFP_KERNEL);
	if (!mv_list)
		return -ENOMEM;

	for (i = 0; i < num_vf_macvlans; i++) {
		mv_list[i].vf = -1;
		mv_list[i].free = true;
		list_add(&mv_list[i].mvlist, &wx->vf_mvs.mvlist);
	}
	wx->mv_list = mv_list;

	return 0;
}

static void wx_sriov_clear_data(struct wx *wx)
{
	/* set num VFs to 0 to prevent access to vfinfo */
	wx->num_vfs = 0;

	/* free VF control structures */
	kfree(wx->vfinfo);
	wx->vfinfo = NULL;

	/* free macvlan list */
	kfree(wx->mv_list);
	wx->mv_list = NULL;

	/* set default pool back to 0 */
	wr32m(wx, WX_PSR_VM_CTL, WX_PSR_VM_CTL_POOL_MASK, 0);
	wx->ring_feature[RING_F_VMDQ].offset = 0;

	clear_bit(WX_FLAG_IRQ_VECTOR_SHARED, wx->flags);
	clear_bit(WX_FLAG_SRIOV_ENABLED, wx->flags);
	/* Disable VMDq flag so device will be set in NM mode */
	if (wx->ring_feature[RING_F_VMDQ].limit == 1)
		clear_bit(WX_FLAG_VMDQ_ENABLED, wx->flags);
}

static int __wx_enable_sriov(struct wx *wx, u8 num_vfs)
{
	int i, ret = 0;
	u32 value = 0;

	set_bit(WX_FLAG_SRIOV_ENABLED, wx->flags);
	dev_info(&wx->pdev->dev, "SR-IOV enabled with %d VFs\n", num_vfs);

	if (num_vfs == 7 && wx->mac.type == wx_mac_em)
		set_bit(WX_FLAG_IRQ_VECTOR_SHARED, wx->flags);

	/* Enable VMDq flag so device will be set in VM mode */
	set_bit(WX_FLAG_VMDQ_ENABLED, wx->flags);
	if (!wx->ring_feature[RING_F_VMDQ].limit)
		wx->ring_feature[RING_F_VMDQ].limit = 1;
	wx->ring_feature[RING_F_VMDQ].offset = num_vfs;

	wx->vfinfo = kcalloc(num_vfs, sizeof(struct vf_data_storage),
			     GFP_KERNEL);
	if (!wx->vfinfo)
		return -ENOMEM;

	ret = wx_alloc_vf_macvlans(wx, num_vfs);
	if (ret)
		return ret;

	/* Initialize default switching mode VEB */
	wr32m(wx, WX_PSR_CTL, WX_PSR_CTL_SW_EN, WX_PSR_CTL_SW_EN);

	for (i = 0; i < num_vfs; i++) {
		/* enable spoof checking for all VFs */
		wx->vfinfo[i].spoofchk_enabled = true;
		wx->vfinfo[i].link_enable = true;
		/* untrust all VFs */
		wx->vfinfo[i].trusted = false;
		/* set the default xcast mode */
		wx->vfinfo[i].xcast_mode = WXVF_XCAST_MODE_NONE;
	}

	if (!test_bit(WX_FLAG_MULTI_64_FUNC, wx->flags)) {
		value = WX_CFG_PORT_CTL_NUM_VT_8;
	} else {
		if (num_vfs < 32)
			value = WX_CFG_PORT_CTL_NUM_VT_32;
		else
			value = WX_CFG_PORT_CTL_NUM_VT_64;
	}
	wr32m(wx, WX_CFG_PORT_CTL,
	      WX_CFG_PORT_CTL_NUM_VT_MASK,
	      value);

	return ret;
}

static void wx_sriov_reinit(struct wx *wx)
{
	rtnl_lock();
	wx->setup_tc(wx->netdev, netdev_get_num_tc(wx->netdev));
	rtnl_unlock();
}

void wx_disable_sriov(struct wx *wx)
{
	if (!pci_vfs_assigned(wx->pdev))
		pci_disable_sriov(wx->pdev);
	else
		wx_err(wx, "Unloading driver while VFs are assigned.\n");

	/* clear flags and free allloced data */
	wx_sriov_clear_data(wx);
}
EXPORT_SYMBOL(wx_disable_sriov);

static int wx_pci_sriov_enable(struct pci_dev *dev,
			       int num_vfs)
{
	struct wx *wx = pci_get_drvdata(dev);
	int err = 0, i;

	if (netif_is_rxfh_configured(wx->netdev)) {
		wx_err(wx, "Cannot enable SR-IOV while RXFH is configured\n");
		wx_err(wx, "Run 'ethtool -X <if> default' to reset RSS table\n");
		return -EBUSY;
	}

	err = __wx_enable_sriov(wx, num_vfs);
	if (err)
		return err;

	wx->num_vfs = num_vfs;
	for (i = 0; i < wx->num_vfs; i++)
		wx_vf_configuration(dev, (i | WX_VF_ENABLE));

	/* reset before enabling SRIOV to avoid mailbox issues */
	wx_sriov_reinit(wx);

	err = pci_enable_sriov(dev, num_vfs);
	if (err) {
		wx_err(wx, "Failed to enable PCI sriov: %d\n", err);
		goto err_out;
	}

	return num_vfs;
err_out:
	wx_sriov_clear_data(wx);
	return err;
}

static int wx_pci_sriov_disable(struct pci_dev *dev)
{
	struct wx *wx = pci_get_drvdata(dev);

	if (netif_is_rxfh_configured(wx->netdev)) {
		wx_err(wx, "Cannot disable SR-IOV while RXFH is configured\n");
		wx_err(wx, "Run 'ethtool -X <if> default' to reset RSS table\n");
		return -EBUSY;
	}

	wx_disable_sriov(wx);
	wx_sriov_reinit(wx);

	return 0;
}

int wx_pci_sriov_configure(struct pci_dev *pdev, int num_vfs)
{
	struct wx *wx = pci_get_drvdata(pdev);
	int err;

	if (!num_vfs) {
		if (!pci_vfs_assigned(pdev))
			return wx_pci_sriov_disable(pdev);

		wx_err(wx, "can't free VFs because some are assigned to VMs.\n");
		return -EBUSY;
	}

	err = wx_pci_sriov_enable(pdev, num_vfs);
	if (err)
		return err;

	return num_vfs;
}
EXPORT_SYMBOL(wx_pci_sriov_configure);

static int wx_set_vf_mac(struct wx *wx, u16 vf, const u8 *mac_addr)
{
	u8 hw_addr[ETH_ALEN];
	int ret = 0;

	ether_addr_copy(hw_addr, mac_addr);
	wx_del_mac_filter(wx, wx->vfinfo[vf].vf_mac_addr, vf);
	ret = wx_add_mac_filter(wx, hw_addr, vf);
	if (ret >= 0)
		ether_addr_copy(wx->vfinfo[vf].vf_mac_addr, mac_addr);
	else
		eth_zero_addr(wx->vfinfo[vf].vf_mac_addr);

	return ret;
}

static void wx_set_vmolr(struct wx *wx, u16 vf, bool aupe)
{
	u32 vmolr = rd32(wx, WX_PSR_VM_L2CTL(vf));

	vmolr |=  WX_PSR_VM_L2CTL_BAM;
	if (aupe)
		vmolr |= WX_PSR_VM_L2CTL_AUPE;
	else
		vmolr &= ~WX_PSR_VM_L2CTL_AUPE;
	wr32(wx, WX_PSR_VM_L2CTL(vf), vmolr);
}

static void wx_set_vmvir(struct wx *wx, u16 vid, u16 qos, u16 vf)
{
	u32 vmvir = vid | (qos << VLAN_PRIO_SHIFT) |
		    WX_TDM_VLAN_INS_VLANA_DEFAULT;

	wr32(wx, WX_TDM_VLAN_INS(vf), vmvir);
}

static int wx_set_vf_vlan(struct wx *wx, int add, int vid, u16 vf)
{
	if (!vid && !add)
		return 0;

	return wx_set_vfta(wx, vid, vf, (bool)add);
}

static void wx_set_vlan_anti_spoofing(struct wx *wx, bool enable, int vf)
{
	u32 index = WX_VF_REG_OFFSET(vf), vf_bit = WX_VF_IND_SHIFT(vf);
	u32 pfvfspoof;

	pfvfspoof = rd32(wx, WX_TDM_VLAN_AS(index));
	if (enable)
		pfvfspoof |= BIT(vf_bit);
	else
		pfvfspoof &= ~BIT(vf_bit);
	wr32(wx, WX_TDM_VLAN_AS(index), pfvfspoof);
}

static void wx_write_qde(struct wx *wx, u32 vf, u32 qde)
{
	struct wx_ring_feature *vmdq = &wx->ring_feature[RING_F_VMDQ];
	u32 q_per_pool = __ALIGN_MASK(1, ~vmdq->mask);
	u32 reg = 0, n = vf * q_per_pool / 32;
	u32 i = vf * q_per_pool;

	reg = rd32(wx, WX_RDM_PF_QDE(n));
	for (i = (vf * q_per_pool - n * 32);
	     i < ((vf + 1) * q_per_pool - n * 32);
	     i++) {
		if (qde == 1)
			reg |= qde << i;
		else
			reg &= qde << i;
	}

	wr32(wx, WX_RDM_PF_QDE(n), reg);
}

static void wx_clear_vmvir(struct wx *wx, u32 vf)
{
	wr32(wx, WX_TDM_VLAN_INS(vf), 0);
}

static void wx_ping_vf(struct wx *wx, int vf)
{
	u32 ping = WX_PF_CONTROL_MSG;

	if (wx->vfinfo[vf].clear_to_send)
		ping |= WX_VT_MSGTYPE_CTS;
	wx_write_mbx_pf(wx, &ping, 1, vf);
}

static void wx_set_vf_rx_tx(struct wx *wx, int vf)
{
	u32 index = WX_VF_REG_OFFSET(vf), vf_bit = WX_VF_IND_SHIFT(vf);
	u32 reg_cur_tx, reg_cur_rx, reg_req_tx, reg_req_rx;

	reg_cur_tx = rd32(wx, WX_TDM_VF_TE(index));
	reg_cur_rx = rd32(wx, WX_RDM_VF_RE(index));

	if (wx->vfinfo[vf].link_enable) {
		reg_req_tx = reg_cur_tx | BIT(vf_bit);
		reg_req_rx = reg_cur_rx | BIT(vf_bit);
		/* Enable particular VF */
		if (reg_cur_tx != reg_req_tx)
			wr32(wx, WX_TDM_VF_TE(index), reg_req_tx);
		if (reg_cur_rx != reg_req_rx)
			wr32(wx, WX_RDM_VF_RE(index), reg_req_rx);
	} else {
		reg_req_tx = BIT(vf_bit);
		reg_req_rx = BIT(vf_bit);
		/* Disable particular VF */
		if (reg_cur_tx & reg_req_tx)
			wr32(wx, WX_TDM_VFTE_CLR(index), reg_req_tx);
		if (reg_cur_rx & reg_req_rx)
			wr32(wx, WX_RDM_VFRE_CLR(index), reg_req_rx);
	}
}

static int wx_get_vf_queues(struct wx *wx, u32 *msgbuf, u32 vf)
{
	struct wx_ring_feature *vmdq = &wx->ring_feature[RING_F_VMDQ];
	unsigned int default_tc = 0;

	msgbuf[WX_VF_TX_QUEUES] = __ALIGN_MASK(1, ~vmdq->mask);
	msgbuf[WX_VF_RX_QUEUES] = __ALIGN_MASK(1, ~vmdq->mask);

	if (wx->vfinfo[vf].pf_vlan || wx->vfinfo[vf].pf_qos)
		msgbuf[WX_VF_TRANS_VLAN] = 1;
	else
		msgbuf[WX_VF_TRANS_VLAN] = 0;

	/* notify VF of default queue */
	msgbuf[WX_VF_DEF_QUEUE] = default_tc;

	return 0;
}

static void wx_vf_reset_event(struct wx *wx, u16 vf)
{
	struct vf_data_storage *vfinfo = &wx->vfinfo[vf];
	u8 num_tcs = netdev_get_num_tc(wx->netdev);

	/* add PF assigned VLAN */
	wx_set_vf_vlan(wx, true, vfinfo->pf_vlan, vf);

	/* reset offloads to defaults */
	wx_set_vmolr(wx, vf, !vfinfo->pf_vlan);

	/* set outgoing tags for VFs */
	if (!vfinfo->pf_vlan && !vfinfo->pf_qos && !num_tcs) {
		wx_clear_vmvir(wx, vf);
	} else {
		if (vfinfo->pf_qos || !num_tcs)
			wx_set_vmvir(wx, vfinfo->pf_vlan,
				     vfinfo->pf_qos, vf);
		else
			wx_set_vmvir(wx, vfinfo->pf_vlan,
				     wx->default_up, vf);
	}

	/* reset multicast table array for vf */
	wx->vfinfo[vf].num_vf_mc_hashes = 0;

	/* Flush and reset the mta with the new values */
	wx_set_rx_mode(wx->netdev);

	wx_del_mac_filter(wx, wx->vfinfo[vf].vf_mac_addr, vf);
	/* reset VF api back to unknown */
	wx->vfinfo[vf].vf_api = wx_mbox_api_null;
}

static void wx_vf_reset_msg(struct wx *wx, u16 vf)
{
	const u8 *vf_mac = wx->vfinfo[vf].vf_mac_addr;
	struct net_device *dev = wx->netdev;
	u32 msgbuf[5] = {0, 0, 0, 0, 0};
	u8 *addr = (u8 *)(&msgbuf[1]);
	u32 reg = 0, index, vf_bit;
	int pf_max_frame;

	/* reset the filters for the device */
	wx_vf_reset_event(wx, vf);

	/* set vf mac address */
	if (!is_zero_ether_addr(vf_mac))
		wx_set_vf_mac(wx, vf, vf_mac);

	index = WX_VF_REG_OFFSET(vf);
	vf_bit = WX_VF_IND_SHIFT(vf);

	/* force drop enable for all VF Rx queues */
	wx_write_qde(wx, vf, 1);

	/* set transmit and receive for vf */
	wx_set_vf_rx_tx(wx, vf);

	pf_max_frame = dev->mtu + ETH_HLEN;

	if (pf_max_frame > ETH_FRAME_LEN)
		reg = BIT(vf_bit);
	wr32(wx, WX_RDM_VFRE_CLR(index), reg);

	/* enable VF mailbox for further messages */
	wx->vfinfo[vf].clear_to_send = true;

	/* reply to reset with ack and vf mac address */
	msgbuf[0] = WX_VF_RESET;
	if (!is_zero_ether_addr(vf_mac)) {
		msgbuf[0] |= WX_VT_MSGTYPE_ACK;
		memcpy(addr, vf_mac, ETH_ALEN);
	} else {
		msgbuf[0] |= WX_VT_MSGTYPE_NACK;
		wx_err(wx, "VF %d has no MAC address assigned", vf);
	}

	msgbuf[3] = wx->mac.mc_filter_type;
	wx_write_mbx_pf(wx, msgbuf, WX_VF_PERMADDR_MSG_LEN, vf);
}

static int wx_set_vf_mac_addr(struct wx *wx, u32 *msgbuf, u16 vf)
{
	const u8 *new_mac = ((u8 *)(&msgbuf[1]));
	int ret;

	if (!is_valid_ether_addr(new_mac)) {
		wx_err(wx, "VF %d attempted to set invalid mac\n", vf);
		return -EINVAL;
	}

	if (wx->vfinfo[vf].pf_set_mac &&
	    memcmp(wx->vfinfo[vf].vf_mac_addr, new_mac, ETH_ALEN)) {
		wx_err(wx,
		       "VF %d attempt to set a MAC but it already had a MAC.",
		       vf);
		return -EBUSY;
	}

	ret = wx_set_vf_mac(wx, vf, new_mac);
	if (ret < 0)
		return ret;

	return 0;
}

static void wx_set_vf_multicasts(struct wx *wx, u32 *msgbuf, u32 vf)
{
	struct vf_data_storage *vfinfo = &wx->vfinfo[vf];
	u16 entries = (msgbuf[0] & WX_VT_MSGINFO_MASK)
		      >> WX_VT_MSGINFO_SHIFT;
	u32 vmolr = rd32(wx, WX_PSR_VM_L2CTL(vf));
	u32 vector_bit, vector_reg, mta_reg, i;
	u16 *hash_list = (u16 *)&msgbuf[1];

	/* only so many hash values supported */
	entries = min_t(u16, entries, WX_MAX_VF_MC_ENTRIES);
	vfinfo->num_vf_mc_hashes = entries;

	for (i = 0; i < entries; i++)
		vfinfo->vf_mc_hashes[i] = hash_list[i];

	for (i = 0; i < vfinfo->num_vf_mc_hashes; i++) {
		vector_reg = WX_PSR_MC_TBL_REG(vfinfo->vf_mc_hashes[i]);
		vector_bit = WX_PSR_MC_TBL_BIT(vfinfo->vf_mc_hashes[i]);
		mta_reg = wx->mac.mta_shadow[vector_reg];
		mta_reg |= BIT(vector_bit);
		wx->mac.mta_shadow[vector_reg] = mta_reg;
		wr32(wx, WX_PSR_MC_TBL(vector_reg), mta_reg);
	}
	vmolr |= WX_PSR_VM_L2CTL_ROMPE;
	wr32(wx, WX_PSR_VM_L2CTL(vf), vmolr);
}

static void wx_set_vf_lpe(struct wx *wx, u32 max_frame, u32 vf)
{
	u32 index, vf_bit, vfre;
	u32 max_frs, reg_val;

	/* determine VF receive enable location */
	index = WX_VF_REG_OFFSET(vf);
	vf_bit = WX_VF_IND_SHIFT(vf);

	vfre = rd32(wx, WX_RDM_VF_RE(index));
	vfre |= BIT(vf_bit);
	wr32(wx, WX_RDM_VF_RE(index), vfre);

	/* pull current max frame size from hardware */
	max_frs = DIV_ROUND_UP(max_frame, 1024);
	reg_val = rd32(wx, WX_MAC_WDG_TIMEOUT) & WX_MAC_WDG_TIMEOUT_WTO_MASK;
	if (max_frs > (reg_val + WX_MAC_WDG_TIMEOUT_WTO_DELTA))
		wr32(wx, WX_MAC_WDG_TIMEOUT,
		     max_frs - WX_MAC_WDG_TIMEOUT_WTO_DELTA);
}

static int wx_find_vlvf_entry(struct wx *wx, u32 vlan)
{
	int regindex;
	u32 vlvf;

	/* short cut the special case */
	if (vlan == 0)
		return 0;

	/* Search for the vlan id in the VLVF entries */
	for (regindex = 1; regindex < WX_PSR_VLAN_SWC_ENTRIES; regindex++) {
		wr32(wx, WX_PSR_VLAN_SWC_IDX, regindex);
		vlvf = rd32(wx, WX_PSR_VLAN_SWC);
		if ((vlvf & VLAN_VID_MASK) == vlan)
			break;
	}

	/* Return a negative value if not found */
	if (regindex >= WX_PSR_VLAN_SWC_ENTRIES)
		regindex = -EINVAL;

	return regindex;
}

static int wx_set_vf_macvlan(struct wx *wx,
			     u16 vf, int index, unsigned char *mac_addr)
{
	struct vf_macvlans *entry;
	struct list_head *pos;
	int retval = 0;

	if (index <= 1) {
		list_for_each(pos, &wx->vf_mvs.mvlist) {
			entry = list_entry(pos, struct vf_macvlans, mvlist);
			if (entry->vf == vf) {
				entry->vf = -1;
				entry->free = true;
				entry->is_macvlan = false;
				wx_del_mac_filter(wx, entry->vf_macvlan, vf);
			}
		}
	}

	if (!index)
		return 0;

	entry = NULL;
	list_for_each(pos, &wx->vf_mvs.mvlist) {
		entry = list_entry(pos, struct vf_macvlans, mvlist);
		if (entry->free)
			break;
	}

	if (!entry || !entry->free)
		return -ENOSPC;

	retval = wx_add_mac_filter(wx, mac_addr, vf);
	if (retval >= 0) {
		entry->free = false;
		entry->is_macvlan = true;
		entry->vf = vf;
		memcpy(entry->vf_macvlan, mac_addr, ETH_ALEN);
	}

	return retval;
}

static int wx_set_vf_vlan_msg(struct wx *wx, u32 *msgbuf, u16 vf)
{
	int add = (msgbuf[0] & WX_VT_MSGINFO_MASK) >> WX_VT_MSGINFO_SHIFT;
	int vid = (msgbuf[1] & WX_PSR_VLAN_SWC_VLANID_MASK);
	int ret;

	if (add)
		wx->vfinfo[vf].vlan_count++;
	else if (wx->vfinfo[vf].vlan_count)
		wx->vfinfo[vf].vlan_count--;

	/* in case of promiscuous mode any VLAN filter set for a VF must
	 * also have the PF pool added to it.
	 */
	if (add && wx->netdev->flags & IFF_PROMISC)
		wx_set_vf_vlan(wx, add, vid, VMDQ_P(0));

	ret = wx_set_vf_vlan(wx, add, vid, vf);
	if (!ret && wx->vfinfo[vf].spoofchk_enabled)
		wx_set_vlan_anti_spoofing(wx, true, vf);

	/* Go through all the checks to see if the VLAN filter should
	 * be wiped completely.
	 */
	if (!add && wx->netdev->flags & IFF_PROMISC) {
		u32 bits = 0, vlvf;
		int reg_ndx;

		reg_ndx = wx_find_vlvf_entry(wx, vid);
		if (reg_ndx < 0)
			return -ENOSPC;
		wr32(wx, WX_PSR_VLAN_SWC_IDX, reg_ndx);
		vlvf = rd32(wx, WX_PSR_VLAN_SWC);
		/* See if any other pools are set for this VLAN filter
		 * entry other than the PF.
		 */
		if (VMDQ_P(0) < 32) {
			bits = rd32(wx, WX_PSR_VLAN_SWC_VM_L);
			bits &= ~BIT(VMDQ_P(0));
			if (test_bit(WX_FLAG_MULTI_64_FUNC, wx->flags))
				bits |= rd32(wx, WX_PSR_VLAN_SWC_VM_H);
		} else {
			if (test_bit(WX_FLAG_MULTI_64_FUNC, wx->flags))
				bits = rd32(wx, WX_PSR_VLAN_SWC_VM_H);
			bits &= ~BIT(VMDQ_P(0) % 32);
			bits |= rd32(wx, WX_PSR_VLAN_SWC_VM_L);
		}
		/* If the filter was removed then ensure PF pool bit
		 * is cleared if the PF only added itself to the pool
		 * because the PF is in promiscuous mode.
		 */
		if ((vlvf & VLAN_VID_MASK) == vid && !bits)
			wx_set_vf_vlan(wx, add, vid, VMDQ_P(0));
	}

	return 0;
}

static int wx_set_vf_macvlan_msg(struct wx *wx, u32 *msgbuf, u16 vf)
{
	int index = (msgbuf[0] & WX_VT_MSGINFO_MASK) >>
		    WX_VT_MSGINFO_SHIFT;
	u8 *new_mac = ((u8 *)(&msgbuf[1]));
	int err;

	if (wx->vfinfo[vf].pf_set_mac && index > 0) {
		wx_err(wx, "VF %d request MACVLAN filter but is denied\n", vf);
		return -EINVAL;
	}

	/* An non-zero index indicates the VF is setting a filter */
	if (index) {
		if (!is_valid_ether_addr(new_mac)) {
			wx_err(wx, "VF %d attempted to set invalid mac\n", vf);
			return -EINVAL;
		}
		/* If the VF is allowed to set MAC filters then turn off
		 * anti-spoofing to avoid false positives.
		 */
		if (wx->vfinfo[vf].spoofchk_enabled)
			wx_set_vf_spoofchk(wx->netdev, vf, false);
	}

	err = wx_set_vf_macvlan(wx, vf, index, new_mac);
	if (err == -ENOSPC)
		wx_err(wx,
		       "VF %d request MACVLAN filter but there is no space\n",
		       vf);
	if (err < 0)
		return err;

	return 0;
}

static int wx_negotiate_vf_api(struct wx *wx, u32 *msgbuf, u32 vf)
{
	int api = msgbuf[1];

	switch (api) {
	case wx_mbox_api_13:
		wx->vfinfo[vf].vf_api = api;
		return 0;
	default:
		wx_err(wx, "VF %d requested invalid api version %u\n", vf, api);
		return -EINVAL;
	}
}

static int wx_get_vf_link_state(struct wx *wx, u32 *msgbuf, u32 vf)
{
	msgbuf[1] = wx->vfinfo[vf].link_enable;

	return 0;
}

static int wx_get_fw_version(struct wx *wx, u32 *msgbuf, u32 vf)
{
	unsigned long fw_version = 0ULL;
	int ret = 0;

	ret = kstrtoul(wx->eeprom_id, 16, &fw_version);
	if (ret)
		return -EOPNOTSUPP;
	msgbuf[1] = fw_version;

	return 0;
}

static int wx_update_vf_xcast_mode(struct wx *wx, u32 *msgbuf, u32 vf)
{
	int xcast_mode = msgbuf[1];
	u32 vmolr, disable, enable;

	if (wx->vfinfo[vf].xcast_mode == xcast_mode)
		return 0;

	switch (xcast_mode) {
	case WXVF_XCAST_MODE_NONE:
		disable = WX_PSR_VM_L2CTL_BAM | WX_PSR_VM_L2CTL_ROMPE |
			  WX_PSR_VM_L2CTL_MPE | WX_PSR_VM_L2CTL_UPE |
			  WX_PSR_VM_L2CTL_VPE;
		enable = 0;
		break;
	case WXVF_XCAST_MODE_MULTI:
		disable = WX_PSR_VM_L2CTL_MPE | WX_PSR_VM_L2CTL_UPE |
			  WX_PSR_VM_L2CTL_VPE;
		enable = WX_PSR_VM_L2CTL_BAM | WX_PSR_VM_L2CTL_ROMPE;
		break;
	case WXVF_XCAST_MODE_ALLMULTI:
		disable = WX_PSR_VM_L2CTL_UPE | WX_PSR_VM_L2CTL_VPE;
		enable = WX_PSR_VM_L2CTL_BAM | WX_PSR_VM_L2CTL_ROMPE |
			 WX_PSR_VM_L2CTL_MPE;
		break;
	case WXVF_XCAST_MODE_PROMISC:
		disable = 0;
		enable = WX_PSR_VM_L2CTL_BAM | WX_PSR_VM_L2CTL_ROMPE |
			 WX_PSR_VM_L2CTL_MPE | WX_PSR_VM_L2CTL_UPE |
			 WX_PSR_VM_L2CTL_VPE;
		break;
	default:
		return -EOPNOTSUPP;
	}

	vmolr = rd32(wx, WX_PSR_VM_L2CTL(vf));
	vmolr &= ~disable;
	vmolr |= enable;
	wr32(wx, WX_PSR_VM_L2CTL(vf), vmolr);

	wx->vfinfo[vf].xcast_mode = xcast_mode;
	msgbuf[1] = xcast_mode;

	return 0;
}

static void wx_rcv_msg_from_vf(struct wx *wx, u16 vf)
{
	u16 mbx_size = WX_VXMAILBOX_SIZE;
	u32 msgbuf[WX_VXMAILBOX_SIZE];
	int retval;

	retval = wx_read_mbx_pf(wx, msgbuf, mbx_size, vf);
	if (retval) {
		wx_err(wx, "Error receiving message from VF\n");
		return;
	}

	/* this is a message we already processed, do nothing */
	if (msgbuf[0] & (WX_VT_MSGTYPE_ACK | WX_VT_MSGTYPE_NACK))
		return;

	if (msgbuf[0] == WX_VF_RESET) {
		wx_vf_reset_msg(wx, vf);
		return;
	}

	/* until the vf completes a virtual function reset it should not be
	 * allowed to start any configuration.
	 */
	if (!wx->vfinfo[vf].clear_to_send) {
		msgbuf[0] |= WX_VT_MSGTYPE_NACK;
		wx_write_mbx_pf(wx, msgbuf, 1, vf);
		return;
	}

	switch ((msgbuf[0] & U16_MAX)) {
	case WX_VF_SET_MAC_ADDR:
		retval = wx_set_vf_mac_addr(wx, msgbuf, vf);
		break;
	case WX_VF_SET_MULTICAST:
		wx_set_vf_multicasts(wx, msgbuf, vf);
		retval = 0;
		break;
	case WX_VF_SET_VLAN:
		retval = wx_set_vf_vlan_msg(wx, msgbuf, vf);
		break;
	case WX_VF_SET_LPE:
		wx_set_vf_lpe(wx, msgbuf[1], vf);
		retval = 0;
		break;
	case WX_VF_SET_MACVLAN:
		retval = wx_set_vf_macvlan_msg(wx, msgbuf, vf);
		break;
	case WX_VF_API_NEGOTIATE:
		retval = wx_negotiate_vf_api(wx, msgbuf, vf);
		break;
	case WX_VF_GET_QUEUES:
		retval = wx_get_vf_queues(wx, msgbuf, vf);
		break;
	case WX_VF_GET_LINK_STATE:
		retval = wx_get_vf_link_state(wx, msgbuf, vf);
		break;
	case WX_VF_GET_FW_VERSION:
		retval = wx_get_fw_version(wx, msgbuf, vf);
		break;
	case WX_VF_UPDATE_XCAST_MODE:
		retval = wx_update_vf_xcast_mode(wx, msgbuf, vf);
		break;
	case WX_VF_BACKUP:
		break;
	default:
		wx_err(wx, "Unhandled Msg %8.8x\n", msgbuf[0]);
		break;
	}

	/* notify the VF of the results of what it sent us */
	if (retval)
		msgbuf[0] |= WX_VT_MSGTYPE_NACK;
	else
		msgbuf[0] |= WX_VT_MSGTYPE_ACK;

	msgbuf[0] |= WX_VT_MSGTYPE_CTS;

	wx_write_mbx_pf(wx, msgbuf, mbx_size, vf);
}

static void wx_rcv_ack_from_vf(struct wx *wx, u16 vf)
{
	u32 msg = WX_VT_MSGTYPE_NACK;

	/* if device isn't clear to send it shouldn't be reading either */
	if (!wx->vfinfo[vf].clear_to_send)
		wx_write_mbx_pf(wx, &msg, 1, vf);
}

void wx_msg_task(struct wx *wx)
{
	u16 vf;

	for (vf = 0; vf < wx->num_vfs; vf++) {
		/* process any reset requests */
		if (!wx_check_for_rst_pf(wx, vf))
			wx_vf_reset_event(wx, vf);

		/* process any messages pending */
		if (!wx_check_for_msg_pf(wx, vf))
			wx_rcv_msg_from_vf(wx, vf);

		/* process any acks */
		if (!wx_check_for_ack_pf(wx, vf))
			wx_rcv_ack_from_vf(wx, vf);
	}
}
EXPORT_SYMBOL(wx_msg_task);

void wx_disable_vf_rx_tx(struct wx *wx)
{
	wr32(wx, WX_TDM_VFTE_CLR(0), U32_MAX);
	wr32(wx, WX_RDM_VFRE_CLR(0), U32_MAX);
	if (test_bit(WX_FLAG_MULTI_64_FUNC, wx->flags)) {
		wr32(wx, WX_TDM_VFTE_CLR(1), U32_MAX);
		wr32(wx, WX_RDM_VFRE_CLR(1), U32_MAX);
	}
}
EXPORT_SYMBOL(wx_disable_vf_rx_tx);

void wx_ping_all_vfs_with_link_status(struct wx *wx, bool link_up)
{
	u32 msgbuf[2] = {0, 0};
	u16 i;

	if (!wx->num_vfs)
		return;
	msgbuf[0] = WX_PF_NOFITY_VF_LINK_STATUS | WX_PF_CONTROL_MSG;
	if (link_up)
		msgbuf[1] = FIELD_PREP(GENMASK(31, 1), wx->speed) | link_up;
	if (wx->notify_down)
		msgbuf[1] |= WX_PF_NOFITY_VF_NET_NOT_RUNNING;
	for (i = 0; i < wx->num_vfs; i++) {
		if (wx->vfinfo[i].clear_to_send)
			msgbuf[0] |= WX_VT_MSGTYPE_CTS;
		wx_write_mbx_pf(wx, msgbuf, 2, i);
	}
}
EXPORT_SYMBOL(wx_ping_all_vfs_with_link_status);

static void wx_set_vf_link_state(struct wx *wx, int vf, int state)
{
	wx->vfinfo[vf].link_state = state;
	switch (state) {
	case IFLA_VF_LINK_STATE_AUTO:
		if (netif_running(wx->netdev))
			wx->vfinfo[vf].link_enable = true;
		else
			wx->vfinfo[vf].link_enable = false;
		break;
	case IFLA_VF_LINK_STATE_ENABLE:
		wx->vfinfo[vf].link_enable = true;
		break;
	case IFLA_VF_LINK_STATE_DISABLE:
		wx->vfinfo[vf].link_enable = false;
		break;
	}
	/* restart the VF */
	wx->vfinfo[vf].clear_to_send = false;
	wx_ping_vf(wx, vf);

	wx_set_vf_rx_tx(wx, vf);
}

void wx_set_all_vfs(struct wx *wx)
{
	int i;

	for (i = 0; i < wx->num_vfs; i++)
		wx_set_vf_link_state(wx, i, wx->vfinfo[i].link_state);
}
EXPORT_SYMBOL(wx_set_all_vfs);
