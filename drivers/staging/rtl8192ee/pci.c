/******************************************************************************
 *
 * Copyright(c) 2009-2010  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#include "core.h"
#include "wifi.h"
#include "pci.h"
#include "base.h"
#include "ps.h"
#include "efuse.h"

static const u16 pcibridge_vendors[PCI_BRIDGE_VENDOR_MAX] = {
	INTEL_VENDOR_ID,
	ATI_VENDOR_ID,
	AMD_VENDOR_ID,
	SIS_VENDOR_ID
};

static const u8 ac_to_hwq[] = {
	VO_QUEUE,
	VI_QUEUE,
	BE_QUEUE,
	BK_QUEUE
};

static u8 _rtl_mac_to_hwqueue(struct ieee80211_hw *hw,
			      struct sk_buff *skb)
{
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	__le16 fc = rtl_get_fc(skb);
	u8 queue_index = skb_get_queue_mapping(skb);

	if (unlikely(ieee80211_is_beacon(fc)))
		return BEACON_QUEUE;
	if (ieee80211_is_mgmt(fc) || ieee80211_is_ctl(fc))
		return MGNT_QUEUE;
	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8192SE)
		if (ieee80211_is_nullfunc(fc))
			return HIGH_QUEUE;

	return ac_to_hwq[queue_index];
}

/* Update PCI dependent default settings*/
static void _rtl_pci_update_default_setting(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	u8 pcibridge_vendor = pcipriv->ndis_adapter.pcibridge_vendor;
	u8 init_aspm;

	ppsc->reg_rfps_level = 0;
	ppsc->b_support_aspm = 0;

	/*Update PCI ASPM setting */
	ppsc->const_amdpci_aspm = rtlpci->const_amdpci_aspm;
	switch (rtlpci->const_pci_aspm) {
	case 0:
		/*No ASPM */
		break;

	case 1:
		/*ASPM dynamically enabled/disable. */
		ppsc->reg_rfps_level |= RT_RF_LPS_LEVEL_ASPM;
		break;

	case 2:
		/*ASPM with Clock Req dynamically enabled/disable. */
		ppsc->reg_rfps_level |= (RT_RF_LPS_LEVEL_ASPM |
					 RT_RF_OFF_LEVL_CLK_REQ);
		break;

	case 3:
		/*
		 * Always enable ASPM and Clock Req
		 * from initialization to halt.
		 * */
		ppsc->reg_rfps_level &= ~(RT_RF_LPS_LEVEL_ASPM);
		ppsc->reg_rfps_level |= (RT_RF_PS_LEVEL_ALWAYS_ASPM |
					 RT_RF_OFF_LEVL_CLK_REQ);
		break;

	case 4:
		/*
		 * Always enable ASPM without Clock Req
		 * from initialization to halt.
		 * */
		ppsc->reg_rfps_level &= ~(RT_RF_LPS_LEVEL_ASPM |
					  RT_RF_OFF_LEVL_CLK_REQ);
		ppsc->reg_rfps_level |= RT_RF_PS_LEVEL_ALWAYS_ASPM;
		break;
	}

	ppsc->reg_rfps_level |= RT_RF_OFF_LEVL_HALT_NIC;

	/*Update Radio OFF setting */
	switch (rtlpci->const_hwsw_rfoff_d3) {
	case 1:
		if (ppsc->reg_rfps_level & RT_RF_LPS_LEVEL_ASPM)
			ppsc->reg_rfps_level |= RT_RF_OFF_LEVL_ASPM;
		break;

	case 2:
		if (ppsc->reg_rfps_level & RT_RF_LPS_LEVEL_ASPM)
			ppsc->reg_rfps_level |= RT_RF_OFF_LEVL_ASPM;
		ppsc->reg_rfps_level |= RT_RF_OFF_LEVL_HALT_NIC;
		break;

	case 3:
		ppsc->reg_rfps_level |= RT_RF_OFF_LEVL_PCI_D3;
		break;
	}

	/*Set HW definition to determine if it supports ASPM. */
	switch (rtlpci->const_support_pciaspm) {
	case 0:{
			/*Not support ASPM. */
			bool b_support_aspm = false;
			ppsc->b_support_aspm = b_support_aspm;
			break;
		}
	case 1:{
			/*Support ASPM. */
			bool b_support_aspm = true;
			bool b_support_backdoor = true;
			ppsc->b_support_aspm = b_support_aspm;

			/*if (priv->oem_id == RT_CID_TOSHIBA &&
			   !priv->ndis_adapter.amd_l1_patch)
			   b_support_backdoor = false; */

			ppsc->b_support_backdoor = b_support_backdoor;

			break;
		}
	case 2:
		/*ASPM value set by chipset. */
		if (pcibridge_vendor == PCI_BRIDGE_VENDOR_INTEL) {
			bool b_support_aspm = true;
			ppsc->b_support_aspm = b_support_aspm;
		}
		break;
	default:
		RT_TRACE(COMP_ERR, DBG_EMERG,
			 ("switch case not process\n"));
		break;
	}

	/* toshiba aspm issue, toshiba will set aspm selfly
	 * so we should not set aspm in driver */
	pci_read_config_byte(rtlpci->pdev, 0x80, &init_aspm);
	if (rtlpriv->rtlhal.hw_type == HARDWARE_TYPE_RTL8192SE &&
	    init_aspm == 0x43)
		ppsc->b_support_aspm = false;
}

static bool _rtl_pci_platform_switch_device_pci_aspm(struct ieee80211_hw *hw,
						     u8 value)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	bool bresult = false;

	if (rtlhal->hw_type != HARDWARE_TYPE_RTL8192SE)
		value |= 0x40;

	pci_write_config_byte(rtlpci->pdev, 0x80, value);

	return bresult;
}

/*When we set 0x01 to enable clk request. Set 0x0 to disable clk req.*/
static bool _rtl_pci_switch_clk_req(struct ieee80211_hw *hw, u8 value)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	bool bresult = false;

	pci_write_config_byte(rtlpci->pdev, 0x81, value);
	bresult = true;

	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8192SE)
		udelay(100);

	return bresult;
}

/*Disable RTL8192SE ASPM & Disable Pci Bridge ASPM*/
static void rtl_pci_disable_aspm(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	u8 pcibridge_vendor = pcipriv->ndis_adapter.pcibridge_vendor;
	u32 pcicfg_addrport = pcipriv->ndis_adapter.pcicfg_addrport;
	u8 num4bytes = pcipriv->ndis_adapter.num4bytes;
	/*Retrieve original configuration settings. */
	u8 linkctrl_reg = pcipriv->ndis_adapter.linkctrl_reg;
	u16 pcibridge_linkctrlreg = pcipriv->ndis_adapter.
				pcibridge_linkctrlreg;
	u16 aspmlevel = 0;

	if (!ppsc->b_support_aspm)
		return;

	if (pcibridge_vendor == PCI_BRIDGE_VENDOR_UNKNOWN) {
		RT_TRACE(COMP_POWER, DBG_TRACE,
			 ("PCI(Bridge) UNKNOWN.\n"));

		return;
	}

	if (ppsc->reg_rfps_level & RT_RF_OFF_LEVL_CLK_REQ) {
		RT_CLEAR_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_CLK_REQ);
		_rtl_pci_switch_clk_req(hw, 0x0);
	}

	if (1) {
		/*for promising device will in L0 state after an I/O. */
		u8 tmp_u1b;
		pci_read_config_byte(rtlpci->pdev, 0x80, &tmp_u1b);
	}

	/*Set corresponding value. */
	aspmlevel |= BIT(0) | BIT(1);
	linkctrl_reg &= ~aspmlevel;
	pcibridge_linkctrlreg &= ~(BIT(0) | BIT(1));

	_rtl_pci_platform_switch_device_pci_aspm(hw, linkctrl_reg);
	udelay(50);

	/*4 Disable Pci Bridge ASPM */
	rtl_pci_raw_write_port_ulong(PCI_CONF_ADDRESS,
				     pcicfg_addrport + (num4bytes << 2));
	rtl_pci_raw_write_port_uchar(PCI_CONF_DATA, pcibridge_linkctrlreg);

	udelay(50);
}

/*
 *Enable RTL8192SE ASPM & Enable Pci Bridge ASPM for
 *power saving We should follow the sequence to enable
 *RTL8192SE first then enable Pci Bridge ASPM
 *or the system will show bluescreen.
 */
static void rtl_pci_enable_aspm(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	u8 pcibridge_vendor = pcipriv->ndis_adapter.pcibridge_vendor;
	u32 pcicfg_addrport = pcipriv->ndis_adapter.pcicfg_addrport;
	u8 num4bytes = pcipriv->ndis_adapter.num4bytes;
	u16 aspmlevel;
	u8 u_pcibridge_aspmsetting;
	u8 u_device_aspmsetting;

	if (!ppsc->b_support_aspm)
		return;

	if (pcibridge_vendor == PCI_BRIDGE_VENDOR_UNKNOWN) {
		RT_TRACE(COMP_POWER, DBG_TRACE,
			 ("PCI(Bridge) UNKNOWN.\n"));
		return;
	}

	/*4 Enable Pci Bridge ASPM */
	rtl_pci_raw_write_port_ulong(PCI_CONF_ADDRESS,
				     pcicfg_addrport + (num4bytes << 2));

	u_pcibridge_aspmsetting =
	    pcipriv->ndis_adapter.pcibridge_linkctrlreg |
	    rtlpci->const_hostpci_aspm_setting;

	if (pcibridge_vendor == PCI_BRIDGE_VENDOR_INTEL)
		u_pcibridge_aspmsetting &= ~BIT(0);

	rtl_pci_raw_write_port_uchar(PCI_CONF_DATA, u_pcibridge_aspmsetting);

	RT_TRACE(COMP_INIT, DBG_LOUD,
		 ("PlatformEnableASPM(): Write reg[%x] = %x\n",
		  (pcipriv->ndis_adapter.pcibridge_pciehdr_offset + 0x10),
		  u_pcibridge_aspmsetting));

	udelay(50);

	/*Get ASPM level (with/without Clock Req) */
	aspmlevel = rtlpci->const_devicepci_aspm_setting;
	u_device_aspmsetting = pcipriv->ndis_adapter.linkctrl_reg;

	/*_rtl_pci_platform_switch_device_pci_aspm(dev,*/
	/*(priv->ndis_adapter.linkctrl_reg | ASPMLevel)); */

	u_device_aspmsetting |= aspmlevel;

	_rtl_pci_platform_switch_device_pci_aspm(hw, u_device_aspmsetting);

	if (ppsc->reg_rfps_level & RT_RF_OFF_LEVL_CLK_REQ) {
		_rtl_pci_switch_clk_req(hw, (ppsc->reg_rfps_level &
					     RT_RF_OFF_LEVL_CLK_REQ) ? 1 : 0);
		RT_SET_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_CLK_REQ);
	}
	udelay(100);
}

static bool rtl_pci_get_amd_l1_patch(struct ieee80211_hw *hw)
{
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	u32 pcicfg_addrport = pcipriv->ndis_adapter.pcicfg_addrport;

	bool status = false;
	u8 offset_e0;
	unsigned offset_e4;

	rtl_pci_raw_write_port_ulong(PCI_CONF_ADDRESS, pcicfg_addrport + 0xE0);
	rtl_pci_raw_write_port_uchar(PCI_CONF_DATA, 0xA0);

	rtl_pci_raw_write_port_ulong(PCI_CONF_ADDRESS, pcicfg_addrport + 0xE0);
	rtl_pci_raw_read_port_uchar(PCI_CONF_DATA, &offset_e0);

	if (offset_e0 == 0xA0) {
		rtl_pci_raw_write_port_ulong(PCI_CONF_ADDRESS,
					     pcicfg_addrport + 0xE4);
		rtl_pci_raw_read_port_ulong(PCI_CONF_DATA, &offset_e4);
		if (offset_e4 & BIT(23))
			status = true;
	}

	return status;
}

static bool rtl_pci_check_buddy_priv(struct ieee80211_hw *hw,
				     struct rtl_priv **buddy_priv)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	bool find_buddy_priv = false;
	struct rtl_priv *tpriv = NULL;
	struct rtl_pci_priv *tpcipriv = NULL;

	if (!list_empty(&rtlpriv->glb_var->glb_priv_list)) {
		list_for_each_entry(tpriv, &rtlpriv->glb_var->glb_priv_list,
				    list) {
			if (tpriv == NULL)
				break;

			tpcipriv = (struct rtl_pci_priv *)tpriv->priv;
			RT_TRACE(COMP_INIT, DBG_LOUD,
				 ("pcipriv->ndis_adapter.funcnumber %x\n",
				  pcipriv->ndis_adapter.funcnumber));
			RT_TRACE(COMP_INIT, DBG_LOUD,
				 ("tpcipriv->ndis_adapter.funcnumber %x\n",
				  tpcipriv->ndis_adapter.funcnumber));

			if ((pcipriv->ndis_adapter.busnumber ==
			     tpcipriv->ndis_adapter.busnumber) &&
			    (pcipriv->ndis_adapter.devnumber ==
			     tpcipriv->ndis_adapter.devnumber) &&
			    (pcipriv->ndis_adapter.funcnumber !=
			     tpcipriv->ndis_adapter.funcnumber)) {
				find_buddy_priv = true;
				break;
			}
		}
	}

	RT_TRACE(COMP_INIT, DBG_LOUD,
		 ("find_buddy_priv %d\n", find_buddy_priv));

	if (find_buddy_priv)
		*buddy_priv = tpriv;

	return find_buddy_priv;
}

static void rtl_pci_get_linkcontrol_field(struct ieee80211_hw *hw)
{
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	u8 capabilityoffset = pcipriv->ndis_adapter.pcibridge_pciehdr_offset;
	u32 pcicfg_addrport = pcipriv->ndis_adapter.pcicfg_addrport;
	u8 linkctrl_reg;
	u8 num4bbytes;

	num4bbytes = (capabilityoffset + 0x10) / 4;

	/*Read  Link Control Register */
	rtl_pci_raw_write_port_ulong(PCI_CONF_ADDRESS,
				     pcicfg_addrport + (num4bbytes << 2));
	rtl_pci_raw_read_port_uchar(PCI_CONF_DATA, &linkctrl_reg);

	pcipriv->ndis_adapter.pcibridge_linkctrlreg = linkctrl_reg;
}

static void rtl_pci_parse_configuration(struct pci_dev *pdev,
					struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);

	u8 tmp;
	int pos;
	u8 linkctrl_reg;

	/*Link Control Register */
	pos = pci_find_capability(pdev, PCI_CAP_ID_EXP);
	pci_read_config_byte(pdev, pos + PCI_EXP_LNKCTL, &linkctrl_reg);
	pcipriv->ndis_adapter.linkctrl_reg = linkctrl_reg;

	RT_TRACE(COMP_INIT, DBG_TRACE,
		 ("Link Control Register =%x\n",
		  pcipriv->ndis_adapter.linkctrl_reg));

	pci_read_config_byte(pdev, 0x98, &tmp);
	tmp |= BIT(4);
	pci_write_config_byte(pdev, 0x98, tmp);

	tmp = 0x17;
	pci_write_config_byte(pdev, 0x70f, tmp);
}

static void rtl_pci_init_aspm(struct ieee80211_hw *hw)
{
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));

	_rtl_pci_update_default_setting(hw);

	if (ppsc->reg_rfps_level & RT_RF_PS_LEVEL_ALWAYS_ASPM) {
		/*Always enable ASPM & Clock Req. */
		rtl_pci_enable_aspm(hw);
		RT_SET_PS_LEVEL(ppsc, RT_RF_PS_LEVEL_ALWAYS_ASPM);
	}
}

static void _rtl_pci_io_handler_init(struct device *dev,
				     struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpriv->io.dev = dev;

	rtlpriv->io.write8_async = pci_write8_async;
	rtlpriv->io.write16_async = pci_write16_async;
	rtlpriv->io.write32_async = pci_write32_async;

	rtlpriv->io.read8_sync = pci_read8_sync;
	rtlpriv->io.read16_sync = pci_read16_sync;
	rtlpriv->io.read32_sync = pci_read32_sync;
}

static bool _rtl_pci_update_earlymode_info(struct ieee80211_hw *hw,
					   struct sk_buff *skb,
					   struct rtl_tcb_desc *tcb_desc,
					   u8 tid)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct sk_buff *next_skb;
	u8 additionlen = FCS_LEN;

	/* here open is 4, wep/tkip is 8, aes is 12*/
	if (info->control.hw_key)
		additionlen += info->control.hw_key->icv_len;

	/* The most skb num is 6 */
	tcb_desc->empkt_num = 0;
	spin_lock_bh(&rtlpriv->locks.waitq_lock);
	skb_queue_walk(&rtlpriv->mac80211.skb_waitq[tid], next_skb) {
		struct ieee80211_tx_info *next_info;

		next_info = IEEE80211_SKB_CB(next_skb);
		if (next_info->flags & IEEE80211_TX_CTL_AMPDU) {
			tcb_desc->empkt_len[tcb_desc->empkt_num] =
				next_skb->len + additionlen;
			tcb_desc->empkt_num++;
		} else {
			break;
		}

		if (skb_queue_is_last(&rtlpriv->mac80211.skb_waitq[tid],
				      next_skb))
			break;

		if (tcb_desc->empkt_num >= rtlhal->max_earlymode_num)
			break;
	}
	spin_unlock_bh(&rtlpriv->locks.waitq_lock);
	return true;
}

/* just for early mode now */
static void _rtl_pci_tx_chk_waitq(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct sk_buff *skb = NULL;
	struct ieee80211_tx_info *info = NULL;
	int tid; /* should be int */

	if (!rtlpriv->rtlhal.b_earlymode_enable)
		return;
	if (rtlpriv->dm.supp_phymode_switch &&
	    (rtlpriv->easy_concurrent_ctl.bswitch_in_process ||
	    (rtlpriv->buddy_priv &&
	     rtlpriv->buddy_priv->easy_concurrent_ctl.bswitch_in_process)))
		return;
	/* we juse use em for BE/BK/VI/VO */
	for (tid = 7; tid >= 0; tid--) {
		u8 hw_queue = ac_to_hwq[rtl92e_tid_to_ac(hw, tid)];
		struct rtl8192_tx_ring *ring = &rtlpci->tx_ring[hw_queue];
		while (!mac->act_scanning &&
		       rtlpriv->psc.rfpwr_state == ERFON) {
			struct rtl_tcb_desc tcb_desc;
			memset(&tcb_desc, 0, sizeof(struct rtl_tcb_desc));

			spin_lock_bh(&rtlpriv->locks.waitq_lock);
			if (!skb_queue_empty(&mac->skb_waitq[tid]) &&
			    (ring->entries - skb_queue_len(&ring->queue) >
			     rtlhal->max_earlymode_num)) {
				skb = skb_dequeue(&mac->skb_waitq[tid]);
			} else {
				spin_unlock_bh(&rtlpriv->locks.waitq_lock);
				break;
			}
			spin_unlock_bh(&rtlpriv->locks.waitq_lock);

			/* Some macaddr can't do early mode. like
			 * multicast/broadcast/no_qos data */
			info = IEEE80211_SKB_CB(skb);
			if (info->flags & IEEE80211_TX_CTL_AMPDU)
				_rtl_pci_update_earlymode_info(hw, skb,
							       &tcb_desc, tid);

			rtlpriv->intf_ops->adapter_tx(hw, NULL, skb, &tcb_desc);
		}
	}
}

static void _rtl_pci_tx_isr(struct ieee80211_hw *hw, int prio)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl8192_tx_ring *ring = &rtlpci->tx_ring[prio];

	while (skb_queue_len(&ring->queue)) {
		struct sk_buff *skb;
		struct ieee80211_tx_info *info;
		__le16 fc;
		u8 tid;
		u8 *entry;


		if (rtlpriv->use_new_trx_flow)
			entry = (u8 *)(&ring->buffer_desc[ring->idx]);
		else
			entry = (u8 *)(&ring->desc[ring->idx]);

		if (rtlpriv->cfg->ops->is_tx_desc_closed &&
		    !rtlpriv->cfg->ops->is_tx_desc_closed(hw, prio, ring->idx))
			return;

		ring->idx = (ring->idx + 1) % ring->entries;

		skb = __skb_dequeue(&ring->queue);

		pci_unmap_single(rtlpci->pdev,
				 rtlpriv->cfg->ops->
					     get_desc((u8 *)entry, true,
						      HW_DESC_TXBUFF_ADDR),
				 skb->len, PCI_DMA_TODEVICE);

		/* remove early mode header */
		if (rtlpriv->rtlhal.b_earlymode_enable)
			skb_pull(skb, EM_HDR_LEN);

		RT_TRACE((COMP_INTR | COMP_SEND), DBG_TRACE,
			 ("new ring->idx:%d, free: skb_queue_len:%d, free: seq:%d\n",
			  ring->idx,
			  skb_queue_len(&ring->queue),
			  *(u16 *)(skb->data + 22)));

		if (prio == TXCMD_QUEUE) {
			dev_kfree_skb(skb);
			goto tx_status_ok;
		}

		/* for sw LPS, just after NULL skb send out, we can
		 * sure AP knows that we are sleeping, our we should not let
		 * rf to sleep
		 */
		fc = rtl_get_fc(skb);
		if (ieee80211_is_nullfunc(fc)) {
			if (ieee80211_has_pm(fc)) {
				rtlpriv->mac80211.offchan_deley = true;
				rtlpriv->psc.state_inap = 1;
			} else {
				rtlpriv->psc.state_inap = 0;
			}
		}
		if (ieee80211_is_action(fc)) {
			struct ieee80211_mgmt_compat *action_frame =
				(struct ieee80211_mgmt_compat *)skb->data;
			if (action_frame->u.action.u.ht_smps.action ==
				WLAN_HT_ACTION_SMPS) {
				dev_kfree_skb(skb);
				goto tx_status_ok;
			}
		}

		/* update tid tx pkt num */
		tid = rtl_get_tid(skb);
		if (tid <= 7)
			rtlpriv->link_info.tidtx_inperiod[tid]++;

		info = IEEE80211_SKB_CB(skb);
		ieee80211_tx_info_clear_status(info);

		info->flags |= IEEE80211_TX_STAT_ACK;
		/*info->status.rates[0].count = 1; */

		ieee80211_tx_status_irqsafe(hw, skb);

		if ((ring->entries - skb_queue_len(&ring->queue)) == 2) {
			RT_TRACE(COMP_ERR, DBG_LOUD,
				 ("more desc left, wake skb_queue@%d,ring->idx = %d, skb_queue_len = 0x%d\n",
					 prio, ring->idx,
					 skb_queue_len(&ring->queue)));

			ieee80211_wake_queue(hw, skb_get_queue_mapping
					     (skb));
		}
tx_status_ok:
		skb = NULL;
	}

	if (((rtlpriv->link_info.num_rx_inperiod +
		rtlpriv->link_info.num_tx_inperiod) > 8) ||
		(rtlpriv->link_info.num_rx_inperiod > 2)) {
		rtl92e_lps_leave(hw);
	}
}

static int _rtl_pci_init_one_rxdesc(struct ieee80211_hw *hw,
				    u8 *entry, int rxring_idx, int desc_idx)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct sk_buff *skb;
	u32 bufferaddress;
	u8 tmp_one = 1;

	skb = dev_alloc_skb(rtlpci->rxbuffersize);
	if (!skb)
		return 0;
	rtlpci->rx_ring[rxring_idx].rx_buf[desc_idx] = skb;

	/* just set skb->cb to mapping addr
	 * for pci_unmap_single use
	 */
	*((dma_addr_t *)skb->cb) = pci_map_single(rtlpci->pdev,
				skb_tail_pointer(skb), rtlpci->rxbuffersize,
				PCI_DMA_FROMDEVICE);
	bufferaddress = *((dma_addr_t *)skb->cb);
	if (pci_dma_mapping_error(rtlpci->pdev, bufferaddress))
		return 0;
	if (rtlpriv->use_new_trx_flow) {
		rtlpriv->cfg->ops->set_desc(hw, (u8 *)entry, false,
					    HW_DESC_RX_PREPARE,
					    (u8 *)&bufferaddress);
	} else {
		rtlpriv->cfg->ops->set_desc(hw, (u8 *)entry, false,
					    HW_DESC_RXBUFF_ADDR,
					    (u8 *)&bufferaddress);
		rtlpriv->cfg->ops->set_desc(hw, (u8 *)entry, false,
					    HW_DESC_RXPKT_LEN,
					    (u8 *)&rtlpci->rxbuffersize);
		rtlpriv->cfg->ops->set_desc(hw, (u8 *)entry, false,
					    HW_DESC_RXOWN,
					    (u8 *)&tmp_one);
	}
	return 1;
}

/* inorder to receive 8K AMSDU we have set skb to
 * 9100bytes in init rx ring, but if this packet is
 * not a AMSDU, this so big packet will be sent to
 * TCP/IP directly, this cause big packet ping fail
 * like: "ping -s 65507", so here we will realloc skb
 * based on the true size of packet, I think mac80211
 * do it will be better, but now mac80211 haven't */

/* but some platform will fail when alloc skb sometimes.
 * in this condition, we will send the old skb to
 * mac80211 directly, this will not cause any other
 * issues, but only be losted by TCP/IP */
static void _rtl_pci_rx_to_mac80211(struct ieee80211_hw *hw,
				    struct sk_buff *skb,
				    struct ieee80211_rx_status rx_status)
{
	if (unlikely(!rtl92e_action_proc(hw, skb, false))) {
		dev_kfree_skb_any(skb);
	} else {
		struct sk_buff *uskb = NULL;
		u8 *pdata;

		uskb = dev_alloc_skb(skb->len + 128);
		if (likely(uskb)) {
			memcpy(IEEE80211_SKB_RXCB(uskb), &rx_status,
			       sizeof(rx_status));
			pdata = (u8 *)skb_put(uskb, skb->len);
			memcpy(pdata, skb->data, skb->len);
			dev_kfree_skb_any(skb);

			ieee80211_rx_irqsafe(hw, uskb);
		} else {
			ieee80211_rx_irqsafe(hw, skb);
		}
	}
}

/*hsisr interrupt handler*/
static void _rtl_pci_hs_interrupt(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	rtl_write_byte(rtlpriv, rtlpriv->cfg->maps[MAC_HSISR],
		       rtl_read_byte(rtlpriv, rtlpriv->cfg->maps[MAC_HSISR]) |
		       rtlpci->sys_irq_mask);
}

static void _rtl_receive_one(struct ieee80211_hw *hw, struct sk_buff *skb,
			     struct ieee80211_rx_status rx_status)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct ieee80211_hdr *hdr = rtl_get_hdr(skb);
	__le16 fc = rtl_get_fc(skb);
	bool unicast = false;

	memcpy(IEEE80211_SKB_RXCB(skb), &rx_status, sizeof(rx_status));

	if (is_broadcast_ether_addr(hdr->addr1)) {
		;/*TODO*/
	} else if (is_multicast_ether_addr(hdr->addr1)) {
		;/*TODO*/
	} else {
		unicast = true;
		rtlpriv->stats.rxbytesunicast += skb->len;
	}

	rtl92e_is_special_data(hw, skb, false);
	if (ieee80211_is_data(fc)) {
		rtlpriv->cfg->ops->led_control(hw, LED_CTL_RX);

		if (unicast)
			rtlpriv->link_info.num_rx_inperiod++;
	}

	/* static bcn for roaming */
	rtl92e_beacon_statistic(hw, skb);
	rtl92e_p2p_info(hw, (void *)skb->data, skb->len);

	/* for sw lps */
	rtl92e_swlps_beacon(hw, (void *)skb->data, skb->len);
	rtl92e_recognize_peer(hw, (void *)skb->data, skb->len);
	if ((rtlpriv->mac80211.opmode == NL80211_IFTYPE_AP) &&
	    (rtlpriv->rtlhal.current_bandtype == BAND_ON_2_4G) &&
	    (ieee80211_is_beacon(fc) ||
	     ieee80211_is_probe_resp(fc)))
		dev_kfree_skb_any(skb);
	else
		_rtl_pci_rx_to_mac80211(hw, skb, rx_status);
}

static void _rtl_pci_rx_interrupt(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct ieee80211_rx_status rx_status = { 0 };
	int rxring_idx = RTL_PCI_RX_MPDU_QUEUE;
	unsigned int count = rtlpci->rxringcount;
	u8 hw_queue = 0;
	unsigned int rx_remained_cnt;
	u8 own;
	u8 tmp_one;
	static int err_count;
	struct rtl_stats stats = {
		.signal = 0,
		.rate = 0,
	};

	/*RX NORMAL PKT */
	while (count--) {
		struct ieee80211_hdr *hdr;
		__le16 fc;
		u16 len;
		/*rx buffer descriptor */
		struct rtl_rx_buffer_desc *buffer_desc = NULL;
		/*if use new trx flow, it means wifi info */
		struct rtl_rx_desc *pdesc = NULL;
		/*rx pkt */
		struct sk_buff *skb = rtlpci->rx_ring[rxring_idx].rx_buf[
					rtlpci->rx_ring[rxring_idx].idx];

		if (rtlpriv->use_new_trx_flow) {
			rx_remained_cnt =
				rtlpriv->cfg->ops->rx_desc_buff_remained_cnt(hw,
								      hw_queue);
			if (rx_remained_cnt < 1)
				return;

		} else {	/* rx descriptor */
			pdesc = &rtlpci->rx_ring[rxring_idx].desc[
				rtlpci->rx_ring[rxring_idx].idx];

			own = (u8) rtlpriv->cfg->ops->get_desc((u8 *)pdesc,
							       false,
							       HW_DESC_OWN);
			if (own) /* wait data to be filled by hardware */
				return;
		}

		/* If we get here, the data is filled already
		 * Attention !!!
		 * We can NOT access 'skb' before 'pci_unmap_single'
		 */
		pci_unmap_single(rtlpci->pdev, *((dma_addr_t *)skb->cb),
				 rtlpci->rxbuffersize, PCI_DMA_FROMDEVICE);

		if (rtlpriv->use_new_trx_flow) {
			buffer_desc = &rtlpci->rx_ring[rxring_idx].buffer_desc[
				rtlpci->rx_ring[rxring_idx].idx];
			/*means rx wifi info*/
			pdesc = (struct rtl_rx_desc *)skb->data;
		}
		memset(&rx_status , 0 , sizeof(rx_status));
		rtlpriv->cfg->ops->query_rx_desc(hw, &stats,
						 &rx_status, (u8 *)pdesc, skb);

		if (rtlpriv->use_new_trx_flow)
			rtlpriv->cfg->ops->rx_check_dma_ok(hw,
							   (u8 *)buffer_desc,
							   hw_queue);
		len = rtlpriv->cfg->ops->get_desc((u8 *)pdesc, false,
						  HW_DESC_RXPKT_LEN);

		if (skb->end - skb->tail > len) {
			skb_put(skb, len);
			if (rtlpriv->use_new_trx_flow)
				skb_reserve(skb, stats.rx_drvinfo_size +
						 stats.rx_bufshift + 24);
			else
				skb_reserve(skb, stats.rx_drvinfo_size +
						 stats.rx_bufshift);

		} else {
			if (err_count++ < 10) {
				pr_info("skb->end (%d) - skb->tail (%d) > len (%d)\n",
					skb->end, skb->tail, len);
				RT_PRINT_DATA(rtlpriv, COMP_CMD, DBG_EMERG,
					      "RX desc\n",
					      (u8 *)pdesc, 32);
			}
			break;
		}

		/* handle command packet here */
		if (rtlpriv->cfg->ops->rx_command_packet(hw, &stats, skb)) {
				dev_kfree_skb_any(skb);
				goto end;
		}

		/* NOTICE This can not be use for mac80211,
		 *this is done in mac80211 code,
		 *if you done here sec DHCP will fail
		 *skb_trim(skb, skb->len - 4);
		 */

		hdr = rtl_get_hdr(skb);
		fc = rtl_get_fc(skb);

		if (!stats.b_crc && !stats.b_hwerror)
			_rtl_receive_one(hw, skb, rx_status);
		else
			dev_kfree_skb_any(skb);
		if (rtlpriv->use_new_trx_flow) {
			rtlpci->rx_ring[hw_queue].next_rx_rp += 1;
			rtlpci->rx_ring[hw_queue].next_rx_rp %=
							RTL_PCI_MAX_RX_COUNT;


			rx_remained_cnt--;
			rtl_write_word(rtlpriv, 0x3B4,
				       rtlpci->rx_ring[hw_queue].next_rx_rp);
		}
		if (((rtlpriv->link_info.num_rx_inperiod +
		      rtlpriv->link_info.num_tx_inperiod) > 8) ||
		    (rtlpriv->link_info.num_rx_inperiod > 2)) {
			rtl92e_lps_leave(hw);
		}
end:
		if (rtlpriv->use_new_trx_flow) {
			_rtl_pci_init_one_rxdesc(hw, (u8 *)buffer_desc,
						 rxring_idx,
					       rtlpci->rx_ring[rxring_idx].idx);
		} else {
			_rtl_pci_init_one_rxdesc(hw, (u8 *)pdesc, rxring_idx,
						 rtlpci->rx_ring[rxring_idx].idx);

			if (rtlpci->rx_ring[rxring_idx].idx ==
			    rtlpci->rxringcount - 1)
				rtlpriv->cfg->ops->set_desc(hw, (u8 *)pdesc,
							    false,
							    HW_DESC_RXERO,
							    (u8 *)&tmp_one);
		}
		rtlpci->rx_ring[rxring_idx].idx =
				(rtlpci->rx_ring[rxring_idx].idx + 1) %
				rtlpci->rxringcount;
	}
}

static irqreturn_t _rtl_pci_interrupt(int irq, void *dev_id)
{
	struct ieee80211_hw *hw = dev_id;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	unsigned long flags;
	u32 inta = 0;
	u32 intb = 0;

	if (rtlpci->irq_enabled == 0)
		return IRQ_HANDLED;

	spin_lock_irqsave(&rtlpriv->locks.irq_th_lock , flags);
	rtl_write_dword(rtlpriv, rtlpriv->cfg->maps[MAC_HIMR], 0x0);
	rtl_write_dword(rtlpriv, rtlpriv->cfg->maps[MAC_HIMRE], 0x0);

	/*read ISR: 4/8bytes */
	rtlpriv->cfg->ops->interrupt_recognized(hw, &inta, &intb);

	/*Shared IRQ or HW disappared */
	if (!inta || inta == 0xffff)
		goto done;
	/*<1> beacon related */
	if (inta & rtlpriv->cfg->maps[RTL_IMR_TBDOK])
		RT_TRACE(COMP_INTR, DBG_TRACE, ("beacon ok interrupt!\n"));

	if (unlikely(inta & rtlpriv->cfg->maps[RTL_IMR_TBDER]))
		RT_TRACE(COMP_INTR, DBG_TRACE, ("beacon err interrupt!\n"));

	if (inta & rtlpriv->cfg->maps[RTL_IMR_BDOK])
		RT_TRACE(COMP_INTR, DBG_TRACE, ("beacon interrupt!\n"));

	if (inta & rtlpriv->cfg->maps[RTL_IMR_BcnInt]) {
		RT_TRACE(COMP_INTR, DBG_TRACE,
			 ("prepare beacon for interrupt!\n"));
		tasklet_schedule(&rtlpriv->works.irq_prepare_bcn_tasklet);
	}

	/*<2> tx related */
	if (unlikely(intb & rtlpriv->cfg->maps[RTL_IMR_TXFOVW]))
		RT_TRACE(COMP_ERR, DBG_TRACE, ("IMR_TXFOVW!\n"));

	if (inta & rtlpriv->cfg->maps[RTL_IMR_MGNTDOK]) {
		RT_TRACE(COMP_INTR, DBG_TRACE, ("Manage ok interrupt!\n"));
		_rtl_pci_tx_isr(hw, MGNT_QUEUE);
	}

	if (inta & rtlpriv->cfg->maps[RTL_IMR_HIGHDOK]) {
		RT_TRACE(COMP_INTR, DBG_TRACE, ("HIGH_QUEUE ok interrupt!\n"));
		_rtl_pci_tx_isr(hw, HIGH_QUEUE);
	}

	if (inta & rtlpriv->cfg->maps[RTL_IMR_BKDOK]) {
		rtlpriv->link_info.num_tx_inperiod++;

		RT_TRACE(COMP_INTR, DBG_TRACE, ("BK Tx OK interrupt!\n"));
		_rtl_pci_tx_isr(hw, BK_QUEUE);
	}

	if (inta & rtlpriv->cfg->maps[RTL_IMR_BEDOK]) {
		rtlpriv->link_info.num_tx_inperiod++;

		RT_TRACE(COMP_INTR, DBG_TRACE, ("BE TX OK interrupt!\n"));
		_rtl_pci_tx_isr(hw, BE_QUEUE);
	}

	if (inta & rtlpriv->cfg->maps[RTL_IMR_VIDOK]) {
		rtlpriv->link_info.num_tx_inperiod++;

		RT_TRACE(COMP_INTR, DBG_TRACE, ("VI TX OK interrupt!\n"));
		_rtl_pci_tx_isr(hw, VI_QUEUE);
	}

	if (inta & rtlpriv->cfg->maps[RTL_IMR_VODOK]) {
		rtlpriv->link_info.num_tx_inperiod++;

		RT_TRACE(COMP_INTR, DBG_TRACE, ("Vo TX OK interrupt!\n"));
		_rtl_pci_tx_isr(hw, VO_QUEUE);
	}

	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8192SE) {
		if (inta & rtlpriv->cfg->maps[RTL_IMR_COMDOK]) {
			rtlpriv->link_info.num_tx_inperiod++;

			RT_TRACE(COMP_INTR, DBG_TRACE,
				 ("CMD TX OK interrupt!\n"));
			_rtl_pci_tx_isr(hw, TXCMD_QUEUE);
		}
	}

	/*<3> rx related */
	if (inta & rtlpriv->cfg->maps[RTL_IMR_ROK]) {
		RT_TRACE(COMP_INTR, DBG_TRACE, ("Rx ok interrupt!\n"));
		_rtl_pci_rx_interrupt(hw);
	}

	if (unlikely(inta & rtlpriv->cfg->maps[RTL_IMR_RDU])) {
		RT_TRACE(COMP_ERR, DBG_WARNING,
			 ("rx descriptor unavailable!\n"));
		_rtl_pci_rx_interrupt(hw);
	}

	if (unlikely(intb & rtlpriv->cfg->maps[RTL_IMR_RXFOVW])) {
		RT_TRACE(COMP_ERR, DBG_WARNING, ("rx overflow !\n"));
		_rtl_pci_rx_interrupt(hw);
	}

	/*<4> fw related*/
	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8723AE) {
		if (inta & rtlpriv->cfg->maps[RTL_IMR_C2HCMD]) {
			RT_TRACE(COMP_INTR, DBG_TRACE,
				 ("firmware interrupt!\n"));
			queue_delayed_work(rtlpriv->works.rtl_wq,
					   &rtlpriv->works.fwevt_wq, 0);
		}
	}

	/*<5> hsisr related*/
	/* Only 8188EE & 8723BE Supported.
	 * If Other ICs Come in, System will corrupt,
	 * because maps[RTL_IMR_HSISR_IND] & maps[MAC_HSISR]
	 * are not initialized*/
	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8188EE ||
	    rtlhal->hw_type == HARDWARE_TYPE_RTL8723BE) {
		if (unlikely(inta & rtlpriv->cfg->maps[RTL_IMR_HSISR_IND])) {
			RT_TRACE(COMP_INTR, DBG_TRACE,
				 ("hsisr interrupt!\n"));
			_rtl_pci_hs_interrupt(hw);
		}
	}


	if (rtlpriv->rtlhal.b_earlymode_enable)
		tasklet_schedule(&rtlpriv->works.irq_tasklet);

done:
	rtl_write_dword(rtlpriv, rtlpriv->cfg->maps[MAC_HIMR],
			rtlpci->irq_mask[0]);
	rtl_write_dword(rtlpriv, rtlpriv->cfg->maps[MAC_HIMRE],
			rtlpci->irq_mask[1]);
	spin_unlock_irqrestore(&rtlpriv->locks.irq_th_lock, flags);
	return IRQ_HANDLED;
}

static void _rtl_pci_irq_tasklet(struct ieee80211_hw *hw)
{
	_rtl_pci_tx_chk_waitq(hw);
}

static void _rtl_pci_prepare_bcn_tasklet(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl8192_tx_ring *ring = NULL;
	struct ieee80211_hdr *hdr = NULL;
	struct ieee80211_tx_info *info = NULL;
	struct sk_buff *pskb = NULL;
	struct rtl_tx_desc *pdesc = NULL;
	struct rtl_tcb_desc tcb_desc;
	/*This is for new trx flow*/
	struct rtl_tx_buffer_desc *pbuffer_desc = NULL;
	u8 temp_one = 1;

	memset(&tcb_desc, 0, sizeof(struct rtl_tcb_desc));
	ring = &rtlpci->tx_ring[BEACON_QUEUE];
	pskb = __skb_dequeue(&ring->queue);
	if (pskb)
		kfree_skb(pskb);

	/*NB: the beacon data buffer must be 32-bit aligned. */
	pskb = ieee80211_beacon_get(hw, mac->vif);
	if (pskb == NULL)
		return;
	hdr = rtl_get_hdr(pskb);
	info = IEEE80211_SKB_CB(pskb);
	pdesc = &ring->desc[0];
	if (rtlpriv->use_new_trx_flow)
		pbuffer_desc = &ring->buffer_desc[0];

	rtlpriv->cfg->ops->fill_tx_desc(hw, hdr, (u8 *)pdesc,
					(u8 *)pbuffer_desc, info, NULL, pskb,
					BEACON_QUEUE, &tcb_desc);

	__skb_queue_tail(&ring->queue, pskb);

	rtlpriv->cfg->ops->set_desc(hw, (u8 *)pdesc, true, HW_DESC_OWN,
				    (u8 *)&temp_one);

	return;
}

static void _rtl_pci_init_trx_var(struct ieee80211_hw *hw)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	u8 i;
	u16 desc_num;

	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8192EE)
		desc_num = TX_DESC_NUM_92E;
	else
		desc_num = RT_TXDESC_NUM;

	for (i = 0; i < RTL_PCI_MAX_TX_QUEUE_COUNT; i++)
		rtlpci->txringcount[i] = desc_num;
	/*
	 *we just alloc 2 desc for beacon queue,
	 *because we just need first desc in hw beacon.
	 */
	rtlpci->txringcount[BEACON_QUEUE] = 2;

	/*
	 *BE queue need more descriptor for performance
	 *consideration or, No more tx desc will happen,
	 *and may cause mac80211 mem leakage.
	 */
	if (!rtl_priv(hw)->use_new_trx_flow)
		rtlpci->txringcount[BE_QUEUE] = RT_TXDESC_NUM_BE_QUEUE;

	rtlpci->rxbuffersize = 9100;	/*2048/1024; */
	rtlpci->rxringcount = RTL_PCI_MAX_RX_COUNT;	/*64; */
}

static void _rtl_pci_init_struct(struct ieee80211_hw *hw,
				 struct pci_dev *pdev)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));

	rtlpriv->rtlhal.up_first_time = true;
	rtlpriv->rtlhal.being_init_adapter = false;

	rtlhal->hw = hw;
	rtlpci->pdev = pdev;

	/*Tx/Rx related var */
	_rtl_pci_init_trx_var(hw);

	/*IBSS*/ mac->beacon_interval = 100;

	/*AMPDU*/
	mac->min_space_cfg = 0;
	mac->max_mss_density = 0;
	/*set sane AMPDU defaults */
	mac->current_ampdu_density = 7;
	mac->current_ampdu_factor = 3;

	/*QOS*/
	rtlpci->acm_method = eAcmWay2_SW;

	/*task */
	tasklet_init(&rtlpriv->works.irq_tasklet,
		     (void (*)(unsigned long))_rtl_pci_irq_tasklet,
		     (unsigned long)hw);
	tasklet_init(&rtlpriv->works.irq_prepare_bcn_tasklet,
		     (void (*)(unsigned long))_rtl_pci_prepare_bcn_tasklet,
		     (unsigned long)hw);
}

static int _rtl_pci_init_tx_ring(struct ieee80211_hw *hw,
				 unsigned int prio, unsigned int entries)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_tx_buffer_desc *buffer_desc;
	struct rtl_tx_desc *desc;
	dma_addr_t buffer_desc_dma, desc_dma;
	u32 nextdescaddress;
	int i;

	/* alloc tx buffer desc for new trx flow*/
	if (rtlpriv->use_new_trx_flow) {
		buffer_desc =
			pci_zalloc_consistent(rtlpci->pdev,
					      sizeof(*buffer_desc) * entries,
					      &buffer_desc_dma);
		if (!buffer_desc || (unsigned long)buffer_desc & 0xFF) {
			RT_TRACE(COMP_ERR, DBG_EMERG,
				 ("Cannot allocate TX ring (prio = %d)\n",
				 prio));
			return -ENOMEM;
		}

		rtlpci->tx_ring[prio].buffer_desc = buffer_desc;
		rtlpci->tx_ring[prio].buffer_desc_dma = buffer_desc_dma;

		rtlpci->tx_ring[prio].cur_tx_rp = 0;
		rtlpci->tx_ring[prio].cur_tx_wp = 0;
		rtlpci->tx_ring[prio].avl_desc = entries;
	}

	/* alloc dma for this ring */
	desc = pci_zalloc_consistent(rtlpci->pdev, sizeof(*desc) * entries,
				     &desc_dma);
	if (!desc || (unsigned long)desc & 0xFF) {
		RT_TRACE(COMP_ERR, DBG_EMERG,
			 ("Cannot allocate TX ring (prio = %d)\n", prio));
		return -ENOMEM;
	}

	rtlpci->tx_ring[prio].desc = desc;
	rtlpci->tx_ring[prio].dma = desc_dma;

	rtlpci->tx_ring[prio].idx = 0;
	rtlpci->tx_ring[prio].entries = entries;
	skb_queue_head_init(&rtlpci->tx_ring[prio].queue);
	RT_TRACE(COMP_INIT, DBG_LOUD,
		 ("queue:%d, ring_addr:%p\n", prio, desc));

	/* init every desc in this ring */
	if (!rtlpriv->use_new_trx_flow) {
		for (i = 0; i < entries; i++) {
			nextdescaddress = (u32) desc_dma +
						      ((i +	1) % entries) *
						      sizeof(*desc);

			rtlpriv->cfg->ops->set_desc(hw, (u8 *)&(desc[i]),
						    true,
						    HW_DESC_TX_NEXTDESC_ADDR,
						    (u8 *)&nextdescaddress);
		}
	}
	return 0;
}

static int _rtl_pci_init_rx_ring(struct ieee80211_hw *hw, int rxring_idx)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	int i;

	if (rtlpriv->use_new_trx_flow) {
		struct rtl_rx_buffer_desc *entry = NULL;
		/* alloc dma for this ring */
		rtlpci->rx_ring[rxring_idx].buffer_desc =
			pci_zalloc_consistent(rtlpci->pdev,
					      sizeof(*rtlpci->rx_ring[rxring_idx].buffer_desc) * rtlpci->rxringcount,
					      &rtlpci->rx_ring[rxring_idx].dma);
		if (!rtlpci->rx_ring[rxring_idx].buffer_desc ||
		    (unsigned long)rtlpci->rx_ring[rxring_idx].buffer_desc & 0xFF) {
			RT_TRACE(COMP_ERR, DBG_EMERG,
				 ("Cannot allocate RX ring\n"));
			return -ENOMEM;
		}

		/* init every desc in this ring */
		rtlpci->rx_ring[rxring_idx].idx = 0;

		for (i = 0; i < rtlpci->rxringcount; i++) {
			entry = &rtlpci->rx_ring[rxring_idx].buffer_desc[i];
			if (!_rtl_pci_init_one_rxdesc(hw, (u8 *)entry,
						      rxring_idx, i))
				return -ENOMEM;
		}
	} else {
		struct rtl_rx_desc *entry = NULL;
		u8 tmp_one = 1;
		/* alloc dma for this ring */
		rtlpci->rx_ring[rxring_idx].desc =
			pci_zalloc_consistent(rtlpci->pdev,
					      sizeof(*rtlpci->rx_ring[rxring_idx].desc) * rtlpci->rxringcount,
					      &rtlpci->rx_ring[rxring_idx].dma);
		if (!rtlpci->rx_ring[rxring_idx].desc ||
		    (unsigned long)rtlpci->rx_ring[rxring_idx].desc & 0xFF) {
			RT_TRACE(COMP_ERR, DBG_EMERG,
				 ("Cannot allocate RX ring\n"));
			return -ENOMEM;
		}

		/* init every desc in this ring */
		rtlpci->rx_ring[rxring_idx].idx = 0;
		for (i = 0; i < rtlpci->rxringcount; i++) {
			entry = &rtlpci->rx_ring[rxring_idx].desc[i];
			if (!_rtl_pci_init_one_rxdesc(hw, (u8 *)entry,
						      rxring_idx, i))
				return -ENOMEM;
		}
		rtlpriv->cfg->ops->set_desc(hw, (u8 *)entry, false,
					    HW_DESC_RXERO, (u8 *) &tmp_one);
	}
	return 0;
}

static void _rtl_pci_free_tx_ring(struct ieee80211_hw *hw,
				  unsigned int prio)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl8192_tx_ring *ring = &rtlpci->tx_ring[prio];

	/* free every desc in this ring */
	while (skb_queue_len(&ring->queue)) {
		struct sk_buff *skb = __skb_dequeue(&ring->queue);
		u8 *entry;

		if (rtlpriv->use_new_trx_flow)
			entry = (u8 *)(&ring->buffer_desc[ring->idx]);
		else
			entry = (u8 *)(&ring->desc[ring->idx]);

		pci_unmap_single(rtlpci->pdev,
				 rtlpriv->cfg->ops->get_desc((u8 *)entry, true,
				 HW_DESC_TXBUFF_ADDR),
				 skb->len, PCI_DMA_TODEVICE);
		kfree_skb(skb);
		ring->idx = (ring->idx + 1) % ring->entries;
	}

	/* free dma of this ring */
	pci_free_consistent(rtlpci->pdev,
			    sizeof(*ring->desc) * ring->entries,
			    ring->desc, ring->dma);
	ring->desc = NULL;
	if (rtlpriv->use_new_trx_flow) {
		pci_free_consistent(rtlpci->pdev,
				    sizeof(*ring->buffer_desc) * ring->entries,
				    ring->buffer_desc, ring->buffer_desc_dma);
		ring->buffer_desc = NULL;
	}
}

static void _rtl_pci_free_rx_ring(struct ieee80211_hw *hw, int rxring_idx)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	int i;

	/* free every desc in this ring */
	for (i = 0; i < rtlpci->rxringcount; i++) {
		struct sk_buff *skb = rtlpci->rx_ring[rxring_idx].rx_buf[i];

		if (!skb)
			continue;

		pci_unmap_single(rtlpci->pdev, *((dma_addr_t *)skb->cb),
				 rtlpci->rxbuffersize, PCI_DMA_FROMDEVICE);
		kfree_skb(skb);
	}

	/* free dma of this ring */
	if (rtlpriv->use_new_trx_flow) {
		pci_free_consistent(rtlpci->pdev,
				    sizeof(*rtlpci->rx_ring[rxring_idx].
				    buffer_desc) * rtlpci->rxringcount,
				    rtlpci->rx_ring[rxring_idx].buffer_desc,
				    rtlpci->rx_ring[rxring_idx].dma);
		rtlpci->rx_ring[rxring_idx].buffer_desc = NULL;
	} else {
		pci_free_consistent(rtlpci->pdev,
				    sizeof(*rtlpci->rx_ring[rxring_idx].desc) *
				    rtlpci->rxringcount,
				    rtlpci->rx_ring[rxring_idx].desc,
				    rtlpci->rx_ring[rxring_idx].dma);
		rtlpci->rx_ring[rxring_idx].desc = NULL;
	}
}

static int _rtl_pci_init_trx_ring(struct ieee80211_hw *hw)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	int ret;
	int i, rxring_idx;

	/* rxring_idx 0:RX_MPDU_QUEUE
	 * rxring_idx 1:RX_CMD_QUEUE */
	for (rxring_idx = 0; rxring_idx < RTL_PCI_MAX_RX_QUEUE; rxring_idx++) {
		ret = _rtl_pci_init_rx_ring(hw, rxring_idx);
		if (ret)
			return ret;
	}

	for (i = 0; i < RTL_PCI_MAX_TX_QUEUE_COUNT; i++) {
		ret = _rtl_pci_init_tx_ring(hw, i,
					    rtlpci->txringcount[i]);
		if (ret)
			goto err_free_rings;
	}

	return 0;

err_free_rings:
	for (rxring_idx = 0; rxring_idx < RTL_PCI_MAX_RX_QUEUE; rxring_idx++)
		_rtl_pci_free_rx_ring(hw, rxring_idx);

	for (i = 0; i < RTL_PCI_MAX_TX_QUEUE_COUNT; i++)
		if (rtlpci->tx_ring[i].desc ||
		    rtlpci->tx_ring[i].buffer_desc)
			_rtl_pci_free_tx_ring(hw, i);

	return 1;
}

static int _rtl_pci_deinit_trx_ring(struct ieee80211_hw *hw)
{
	u32 i, rxring_idx;

	/*free rx rings */
	for (rxring_idx = 0; rxring_idx < RTL_PCI_MAX_RX_QUEUE; rxring_idx++)
		_rtl_pci_free_rx_ring(hw, rxring_idx);

	/*free tx rings */
	for (i = 0; i < RTL_PCI_MAX_TX_QUEUE_COUNT; i++)
		_rtl_pci_free_tx_ring(hw, i);

	return 0;
}

int rtl92e_pci_reset_trx_ring(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	int i, rxring_idx;
	unsigned long flags;
	u8 tmp_one = 1;
	/* rxring_idx 0:RX_MPDU_QUEUE */
	/* rxring_idx 1:RX_CMD_QUEUE */
	for (rxring_idx = 0; rxring_idx < RTL_PCI_MAX_RX_QUEUE; rxring_idx++) {
		/* force the rx_ring[RX_MPDU_QUEUE]
		 * RX_CMD_QUEUE].idx to the first one
		 * If using the new trx flow, do nothing
		 */
		if (!rtlpriv->use_new_trx_flow &&
		    rtlpci->rx_ring[rxring_idx].desc) {
			struct rtl_rx_desc *entry = NULL;

			for (i = 0; i < rtlpci->rxringcount; i++) {
				entry = &rtlpci->rx_ring[rxring_idx].desc[i];
				rtlpriv->cfg->ops->set_desc(hw, (u8 *)entry,
							    false,
							    HW_DESC_RXOWN,
							    &tmp_one);
			}
		}
		rtlpci->rx_ring[rxring_idx].idx = 0;
	}

	/* after reset, release previous pending packet,
	 * and force the  tx idx to the first one
	 */
	spin_lock_irqsave(&rtlpriv->locks.irq_th_lock, flags);
	for (i = 0; i < RTL_PCI_MAX_TX_QUEUE_COUNT; i++) {
		if (rtlpci->tx_ring[i].desc ||
		    rtlpci->tx_ring[i].buffer_desc) {
			struct rtl8192_tx_ring *ring = &rtlpci->tx_ring[i];

			while (skb_queue_len(&ring->queue)) {
				struct sk_buff *skb =
					__skb_dequeue(&ring->queue);
				u8 *entry;

				if (rtlpriv->use_new_trx_flow)
					entry = (u8 *)(&ring->buffer_desc
								[ring->idx]);
				else
					entry = (u8 *)(&ring->desc[ring->idx]);

				pci_unmap_single(rtlpci->pdev,
						 rtlpriv->cfg->ops->get_desc(
						 (u8 *)entry, true,
						 HW_DESC_TXBUFF_ADDR),
					skb->len, PCI_DMA_TODEVICE);
				kfree_skb(skb);
				ring->idx = (ring->idx + 1) % ring->entries;
			}
			ring->idx = 0;
		}
	}

	spin_unlock_irqrestore(&rtlpriv->locks.irq_th_lock, flags);

	return 0;
}

static bool rtl_pci_tx_chk_waitq_insert(struct ieee80211_hw *hw,
					struct ieee80211_sta *sta,
					struct sk_buff *skb)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_sta_info *sta_entry = NULL;
	u8 tid = rtl_get_tid(skb);
	__le16 fc = rtl_get_fc(skb);

	if (!sta)
		return false;
	sta_entry = (struct rtl_sta_info *)sta->drv_priv;

	if (!rtlpriv->rtlhal.b_earlymode_enable)
		return false;
	if (ieee80211_is_nullfunc(fc))
		return false;
	if (ieee80211_is_qos_nullfunc(fc))
		return false;
	if (ieee80211_is_pspoll(fc))
		return false;

	if (sta_entry->tids[tid].agg.agg_state != RTL_AGG_OPERATIONAL)
		return false;
	if (_rtl_mac_to_hwqueue(hw, skb) > VO_QUEUE)
		return false;
	if (tid > 7)
		return false;
	/* maybe every tid should be checked */
	if (!rtlpriv->link_info.higher_busytxtraffic[tid])
		return false;

	spin_lock_bh(&rtlpriv->locks.waitq_lock);
	skb_queue_tail(&rtlpriv->mac80211.skb_waitq[tid], skb);
	spin_unlock_bh(&rtlpriv->locks.waitq_lock);

	return true;
}

static int rtl_pci_tx(struct ieee80211_hw *hw,
		      struct ieee80211_sta *sta,
		      struct sk_buff *skb,
		      struct rtl_tcb_desc *ptcb_desc)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_sta_info *sta_entry = NULL;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct rtl8192_tx_ring *ring;
	struct rtl_tx_desc *pdesc;
	struct rtl_tx_buffer_desc *ptx_bd_desc = NULL;
	u16 idx;
	u8 own;
	u8 temp_one = 1;
	u8 hw_queue = _rtl_mac_to_hwqueue(hw, skb);
	unsigned long flags;
	struct ieee80211_hdr *hdr = rtl_get_hdr(skb);
	__le16 fc = rtl_get_fc(skb);
	u8 *pda_addr = hdr->addr1;
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	/*ssn */
	u8 tid = 0;
	u16 seq_number = 0;

	if (ieee80211_is_mgmt(fc))
		rtl92e_tx_mgmt_proc(hw, skb);

	if (rtlpriv->psc.sw_ps_enabled) {
		if (ieee80211_is_data(fc) && !ieee80211_is_nullfunc(fc) &&
		    !ieee80211_has_pm(fc))
			hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_PM);
	}

	rtl92e_action_proc(hw, skb, true);

	if (is_multicast_ether_addr(pda_addr))
		rtlpriv->stats.txbytesmulticast += skb->len;
	else if (is_broadcast_ether_addr(pda_addr))
		rtlpriv->stats.txbytesbroadcast += skb->len;
	else
		rtlpriv->stats.txbytesunicast += skb->len;

	spin_lock_irqsave(&rtlpriv->locks.irq_th_lock, flags);
	ring = &rtlpci->tx_ring[hw_queue];
	if (hw_queue != BEACON_QUEUE) {
		if (rtlpriv->use_new_trx_flow)
			idx = ring->cur_tx_wp;
		else
			idx = (ring->idx + skb_queue_len(&ring->queue)) %
			      ring->entries;
	} else {
		idx = 0;
	}

	pdesc = &ring->desc[idx];

	if (rtlpriv->use_new_trx_flow) {
		ptx_bd_desc = &ring->buffer_desc[idx];
	} else {
		own = (u8) rtlpriv->cfg->ops->get_desc((u8 *)pdesc,
				true, HW_DESC_OWN);

		if ((own == 1) && (hw_queue != BEACON_QUEUE)) {
			RT_TRACE(COMP_ERR, DBG_WARNING,
				 ("No more TX desc@%d, ring->idx = %d, idx = %d, skb_queue_len = 0x%d\n",
				  hw_queue, ring->idx, idx,
				  skb_queue_len(&ring->queue)));

			spin_unlock_irqrestore(&rtlpriv->locks.irq_th_lock,
					       flags);
			return skb->len;
		}
	}

	if (ieee80211_is_data_qos(fc)) {
		tid = rtl_get_tid(skb);
		if (sta) {
			sta_entry = (struct rtl_sta_info *)sta->drv_priv;
			seq_number = (le16_to_cpu(hdr->seq_ctrl) &
				      IEEE80211_SCTL_SEQ) >> 4;
			seq_number += 1;

			if (!ieee80211_has_morefrags(hdr->frame_control))
				sta_entry->tids[tid].seq_number = seq_number;
		}
	}

	if (ieee80211_is_data(fc))
		rtlpriv->cfg->ops->led_control(hw, LED_CTL_TX);

	rtlpriv->cfg->ops->fill_tx_desc(hw, hdr, (u8 *)pdesc,
					(u8 *)ptx_bd_desc, info, sta, skb,
					hw_queue, ptcb_desc);

	__skb_queue_tail(&ring->queue, skb);
	if (rtlpriv->use_new_trx_flow) {
		rtlpriv->cfg->ops->set_desc(hw, (u8 *)pdesc, true,
					    HW_DESC_OWN, (u8 *)&hw_queue);
	} else {
		rtlpriv->cfg->ops->set_desc(hw, (u8 *)pdesc, true,
					    HW_DESC_OWN, (u8 *)&temp_one);
	}

	if ((ring->entries - skb_queue_len(&ring->queue)) < 2 &&
	    hw_queue != BEACON_QUEUE) {
		RT_TRACE(COMP_ERR, DBG_LOUD,
			 ("less desc left, stop skb_queue@%d, ring->idx = %d, idx = %d, skb_queue_len = 0x%d\n",
			  hw_queue, ring->idx, idx,
			  skb_queue_len(&ring->queue)));

		ieee80211_stop_queue(hw, skb_get_queue_mapping(skb));
	}

	spin_unlock_irqrestore(&rtlpriv->locks.irq_th_lock, flags);

	if (rtlpriv->cfg->ops->tx_polling)
		rtlpriv->cfg->ops->tx_polling(hw, hw_queue);

	return 0;
}
static void rtl_pci_flush(struct ieee80211_hw *hw, u32 queues, bool drop)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u16 i = 0;
	int queue_id;
	struct rtl8192_tx_ring *ring;

	if (mac->skip_scan)
		return;

	for (queue_id = RTL_PCI_MAX_TX_QUEUE_COUNT - 1; queue_id >= 0;) {
		u32 queue_len;
		if (((queues >> queue_id) & 0x1) == 0) {
			queue_id--;
			continue;
		}
		ring = &pcipriv->dev.tx_ring[queue_id];
		queue_len = skb_queue_len(&ring->queue);
		if (queue_len == 0 || queue_id == BEACON_QUEUE ||
		    queue_id == TXCMD_QUEUE) {
			queue_id--;
			continue;
		} else {
			msleep(5);
			i++;
		}

		/* we just wait 1s for all queues */
		if (rtlpriv->psc.rfpwr_state == ERFOFF ||
		    is_hal_stop(rtlhal) || i >= 200)
			return;
	}
}

static void rtl_pci_deinit(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	_rtl_pci_deinit_trx_ring(hw);

	synchronize_irq(rtlpci->pdev->irq);
	tasklet_kill(&rtlpriv->works.irq_tasklet);

	flush_workqueue(rtlpriv->works.rtl_wq);
	destroy_workqueue(rtlpriv->works.rtl_wq);
}

static int rtl_pci_init(struct ieee80211_hw *hw, struct pci_dev *pdev)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	int err;

	_rtl_pci_init_struct(hw, pdev);

	err = _rtl_pci_init_trx_ring(hw);
	if (err) {
		RT_TRACE(COMP_ERR, DBG_EMERG,
			 ("tx ring initialization failed"));
		return err;
	}

	return 1;
}

static int rtl_pci_start(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	int err = 0;

	RT_TRACE(COMP_INIT, DBG_DMESG, (" rtl_pci_start\n"));
	rtl92e_pci_reset_trx_ring(hw);

	rtlpriv->rtlhal.driver_is_goingto_unload = false;
	if (rtlpriv->cfg->ops->get_btc_status()) {
		rtlpriv->btcoexist.btc_ops->btc_init_variables(rtlpriv);
		rtlpriv->btcoexist.btc_ops->btc_init_hal_vars(rtlpriv);
	}

	err = rtlpriv->cfg->ops->hw_init(hw);
	if (err) {
		RT_TRACE(COMP_INIT, DBG_DMESG,
			 ("Failed to config hardware err %x!\n" , err));
		return err;
	}

	rtlpriv->cfg->ops->enable_interrupt(hw);
	RT_TRACE(COMP_INIT, DBG_LOUD, ("enable_interrupt OK\n"));

	rtl92e_init_rx_config(hw);

	/*should after adapter start and interrupt enable. */
	set_hal_start(rtlhal);

	RT_CLEAR_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_HALT_NIC);

	rtlpriv->rtlhal.up_first_time = false;

	RT_TRACE(COMP_INIT, DBG_DMESG, ("rtl_pci_start OK\n"));
	return 0;
}

static void rtl_pci_stop(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	u8 RFInProgressTimeOut = 0;

	if (rtlpriv->cfg->ops->get_btc_status())
		rtlpriv->btcoexist.btc_ops->btc_halt_notify();

	/*
	 *should before disable interrrupt&adapter
	 *and will do it immediately.
	 */
	set_hal_stop(rtlhal);

	rtlpriv->cfg->ops->disable_interrupt(hw);

	spin_lock(&rtlpriv->locks.rf_ps_lock);
	while (ppsc->rfchange_inprogress) {
		spin_unlock(&rtlpriv->locks.rf_ps_lock);
		if (RFInProgressTimeOut > 100) {
			spin_lock(&rtlpriv->locks.rf_ps_lock);
			break;
		}
		mdelay(1);
		RFInProgressTimeOut++;
		spin_lock(&rtlpriv->locks.rf_ps_lock);
	}
	ppsc->rfchange_inprogress = true;
	spin_unlock(&rtlpriv->locks.rf_ps_lock);

	rtlpriv->rtlhal.driver_is_goingto_unload = true;
	rtlpriv->cfg->ops->hw_disable(hw);
	rtlpriv->cfg->ops->led_control(hw, LED_CTL_POWER_OFF);

	spin_lock(&rtlpriv->locks.rf_ps_lock);
	ppsc->rfchange_inprogress = false;
	spin_unlock(&rtlpriv->locks.rf_ps_lock);

	rtl_pci_enable_aspm(hw);
}

static bool _rtl_pci_find_adapter(struct pci_dev *pdev,
				  struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct pci_dev *bridge_pdev = pdev->bus->self;
	u16 venderid;
	u16 deviceid;
	u8 revisionid;
	u16 irqline;
	u8 tmp;

	venderid = pdev->vendor;
	deviceid = pdev->device;
	pci_read_config_byte(pdev, 0x8, &revisionid);
	pci_read_config_word(pdev, 0x3C, &irqline);

	if (deviceid == RTL_PCI_8192_DID ||
	    deviceid == RTL_PCI_0044_DID ||
	    deviceid == RTL_PCI_0047_DID ||
	    deviceid == RTL_PCI_8192SE_DID ||
	    deviceid == RTL_PCI_8174_DID ||
	    deviceid == RTL_PCI_8173_DID ||
	    deviceid == RTL_PCI_8172_DID ||
	    deviceid == RTL_PCI_8171_DID) {
		switch (revisionid) {
		case RTL_PCI_REVISION_ID_8192PCIE:
			RT_TRACE(COMP_INIT, DBG_DMESG,
				 ("8192E is found but not supported now-vid/did=%x/%x\n",
				  venderid, deviceid));
			rtlhal->hw_type = HARDWARE_TYPE_RTL8192E;
			return false;
			break;
		case RTL_PCI_REVISION_ID_8192SE:
			RT_TRACE(COMP_INIT, DBG_DMESG,
				 ("8192SE is found - vid/did=%x/%x\n",
				  venderid, deviceid));
			rtlhal->hw_type = HARDWARE_TYPE_RTL8192SE;
			break;
		default:
			RT_TRACE(COMP_ERR, DBG_WARNING,
				 ("Err: Unknown device - vid/did=%x/%x\n",
				  venderid, deviceid));
			rtlhal->hw_type = HARDWARE_TYPE_RTL8192SE;
			break;
		}
	} else if (deviceid == RTL_PCI_8723AE_DID) {
		rtlhal->hw_type = HARDWARE_TYPE_RTL8723AE;
		RT_TRACE(COMP_INIT, DBG_DMESG,
			 ("8723AE PCI-E is found - vid/did=%x/%x\n",
			  venderid, deviceid));
	} else if (deviceid == RTL_PCI_8192CET_DID ||
		   deviceid == RTL_PCI_8192CE_DID ||
		   deviceid == RTL_PCI_8191CE_DID ||
		   deviceid == RTL_PCI_8188CE_DID) {
		rtlhal->hw_type = HARDWARE_TYPE_RTL8192CE;
		RT_TRACE(COMP_INIT, DBG_DMESG,
			 ("8192C PCI-E is found - vid/did=%x/%x\n",
			  venderid, deviceid));
	} else if (deviceid == RTL_PCI_8192DE_DID ||
		   deviceid == RTL_PCI_8192DE_DID2) {
		rtlhal->hw_type = HARDWARE_TYPE_RTL8192DE;
		RT_TRACE(COMP_INIT, DBG_DMESG,
			 ("8192D PCI-E is found - vid/did=%x/%x\n",
			  venderid, deviceid));
	} else if (deviceid == RTL_PCI_8188EE_DID) {
			rtlhal->hw_type = HARDWARE_TYPE_RTL8188EE;
			RT_TRACE(COMP_INIT , DBG_LOUD,
				 ("Find adapter, Hardware type is 8188EE\n"));
	} else if (deviceid == RTL_PCI_8723BE_DID) {
			rtlhal->hw_type = HARDWARE_TYPE_RTL8723BE;
			RT_TRACE(COMP_INIT , DBG_LOUD,
				 ("Find adapter, Hardware type is 8723BE\n"));
	} else if (deviceid == RTL_PCI_8192EE_DID) {
			rtlhal->hw_type = HARDWARE_TYPE_RTL8192EE;
			RT_TRACE(COMP_INIT , DBG_LOUD,
				 ("Find adapter, Hardware type is 8192EE\n"));
	} else if (deviceid == RTL_PCI_8821AE_DID) {
			rtlhal->hw_type = HARDWARE_TYPE_RTL8821AE;
			RT_TRACE(COMP_INIT , DBG_LOUD,
				 ("Find adapter, Hardware type is 8821AE\n"));
	} else if (deviceid == RTL_PCI_8812AE_DID) {
			rtlhal->hw_type = HARDWARE_TYPE_RTL8812AE;
			RT_TRACE(COMP_INIT , DBG_LOUD,
				 ("Find adapter, Hardware type is 8812AE\n"));
	} else {
		RT_TRACE(COMP_ERR, DBG_WARNING,
			 ("Err: Unknown device - vid/did=%x/%x\n",
			  venderid, deviceid));

		rtlhal->hw_type = RTL_DEFAULT_HARDWARE_TYPE;
	}

	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8192DE) {
		if (revisionid == 0 || revisionid == 1) {
			if (revisionid == 0) {
				RT_TRACE(COMP_INIT, DBG_LOUD,
					 ("Find 92DE MAC0.\n"));
				rtlhal->interfaceindex = 0;
			} else if (revisionid == 1) {
				RT_TRACE(COMP_INIT, DBG_LOUD,
					 ("Find 92DE MAC1.\n"));
				rtlhal->interfaceindex = 1;
			}
		} else {
			RT_TRACE(COMP_INIT, DBG_LOUD,
				 ("Unknown device - VendorID/DeviceID=%x/%x, Revision=%x\n",
				  venderid, deviceid, revisionid));
			rtlhal->interfaceindex = 0;
		}
	}

	/* 92ee use new trx flow */
	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8192EE)
		rtlpriv->use_new_trx_flow = true;
	else
		rtlpriv->use_new_trx_flow = false;

	/*find bus info */
	pcipriv->ndis_adapter.busnumber = pdev->bus->number;
	pcipriv->ndis_adapter.devnumber = PCI_SLOT(pdev->devfn);
	pcipriv->ndis_adapter.funcnumber = PCI_FUNC(pdev->devfn);

	/*find bridge info */
	pcipriv->ndis_adapter.pcibridge_vendor = PCI_BRIDGE_VENDOR_UNKNOWN;
	/* some ARM have no bridge_pdev and will crash here
	 * so we should check if bridge_pdev is NULL */
	if (bridge_pdev) {
		pcipriv->ndis_adapter.pcibridge_vendorid = bridge_pdev->vendor;
		for (tmp = 0; tmp < PCI_BRIDGE_VENDOR_MAX; tmp++) {
			if (bridge_pdev->vendor == pcibridge_vendors[tmp]) {
				pcipriv->ndis_adapter.pcibridge_vendor = tmp;
				RT_TRACE(COMP_INIT, DBG_DMESG,
					 ("Pci Bridge Vendor is found index: %d\n",
					  tmp));
				break;
			}
		}
	}

	if (pcipriv->ndis_adapter.pcibridge_vendor !=
	    PCI_BRIDGE_VENDOR_UNKNOWN) {
		pcipriv->ndis_adapter.pcibridge_busnum =
		    bridge_pdev->bus->number;
		pcipriv->ndis_adapter.pcibridge_devnum =
		    PCI_SLOT(bridge_pdev->devfn);
		pcipriv->ndis_adapter.pcibridge_funcnum =
		    PCI_FUNC(bridge_pdev->devfn);
		pcipriv->ndis_adapter.pcicfg_addrport =
		    (pcipriv->ndis_adapter.pcibridge_busnum << 16) |
		    (pcipriv->ndis_adapter.pcibridge_devnum << 11) |
		    (pcipriv->ndis_adapter.pcibridge_funcnum << 8) | (1 << 31);
		pcipriv->ndis_adapter.pcibridge_pciehdr_offset =
		    pci_pcie_cap(bridge_pdev);
		pcipriv->ndis_adapter.num4bytes =
		    (pcipriv->ndis_adapter.pcibridge_pciehdr_offset + 0x10) / 4;

		rtl_pci_get_linkcontrol_field(hw);

		if (pcipriv->ndis_adapter.pcibridge_vendor ==
		    PCI_BRIDGE_VENDOR_AMD) {
			pcipriv->ndis_adapter.amd_l1_patch =
			    rtl_pci_get_amd_l1_patch(hw);
		}
	}

	RT_TRACE(COMP_INIT, DBG_DMESG,
		 ("pcidev busnumber:devnumber:funcnumber:vendor:link_ctl %d:%d:%d:%x:%x\n",
		  pcipriv->ndis_adapter.busnumber,
		  pcipriv->ndis_adapter.devnumber,
		  pcipriv->ndis_adapter.funcnumber,
		  pdev->vendor, pcipriv->ndis_adapter.linkctrl_reg));

	RT_TRACE(COMP_INIT, DBG_DMESG,
		 ("pci_bridge busnumber:devnumber:funcnumber:vendor:pcie_cap:link_ctl_reg:amd %d:%d:%d:%x:%x:%x:%x\n",
		  pcipriv->ndis_adapter.pcibridge_busnum,
		  pcipriv->ndis_adapter.pcibridge_devnum,
		  pcipriv->ndis_adapter.pcibridge_funcnum,
		  pcibridge_vendors[pcipriv->ndis_adapter.pcibridge_vendor],
		  pcipriv->ndis_adapter.pcibridge_pciehdr_offset,
		  pcipriv->ndis_adapter.pcibridge_linkctrlreg,
		  pcipriv->ndis_adapter.amd_l1_patch));

	rtl_pci_parse_configuration(pdev, hw);
	list_add_tail(&rtlpriv->list, &rtlpriv->glb_var->glb_priv_list);
	return true;
}

static int rtl_pci_intr_mode_msi(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(pcipriv);
	int ret;
	ret = pci_enable_msi(rtlpci->pdev);
	if (ret < 0)
		return ret;

	ret = request_irq(rtlpci->pdev->irq, &_rtl_pci_interrupt,
			  IRQF_SHARED, KBUILD_MODNAME, hw);
	if (ret < 0) {
		pci_disable_msi(rtlpci->pdev);
		return ret;
	}

	rtlpci->using_msi = true;

	RT_TRACE(COMP_INIT|COMP_INTR, DBG_DMESG, ("MSI Interrupt Mode!\n"));
	return 0;
}

static int rtl_pci_intr_mode_legacy(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(pcipriv);
	int ret;

	ret = request_irq(rtlpci->pdev->irq, &_rtl_pci_interrupt,
			  IRQF_SHARED, KBUILD_MODNAME, hw);
	if (ret < 0)
		return ret;

	rtlpci->using_msi = false;
	RT_TRACE(COMP_INIT|COMP_INTR, DBG_DMESG,
		 ("Pin-based Interrupt Mode!\n"));
	return 0;
}

static int rtl_pci_intr_mode_decide(struct ieee80211_hw *hw)
{
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(pcipriv);
	int ret;
	if (rtlpci->msi_support) {
		ret = rtl_pci_intr_mode_msi(hw);
		if (ret < 0)
			ret = rtl_pci_intr_mode_legacy(hw);
	} else {
		ret = rtl_pci_intr_mode_legacy(hw);
	}
	return ret;
}

/* this is used for other modules get
 * hw pointer in rtl_pci_get_hw_pointer */
static struct ieee80211_hw *hw_export;

int stg_rtl_pci_probe(struct pci_dev *pdev,
		      const struct pci_device_id *id)
{
	struct ieee80211_hw *hw = NULL;
	struct rtl_priv *rtlpriv = NULL;
	struct rtl_pci_priv *pcipriv = NULL;
	struct rtl_pci *rtlpci;
	unsigned long pmem_start, pmem_len, pmem_flags;
	int err;

	err = pci_enable_device(pdev);
	if (err) {
		RT_ASSERT(false,
			  ("%s : Cannot enable new PCI device\n",
			   pci_name(pdev)));
		return err;
	}

	if (!pci_set_dma_mask(pdev, DMA_BIT_MASK(32))) {
		if (pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32))) {
			RT_ASSERT(false,
				  ("Unable to obtain 32bit DMA for consistent allocations\n"));
			pci_disable_device(pdev);
			return -ENOMEM;
		}
	}

	pci_set_master(pdev);

	hw = ieee80211_alloc_hw(sizeof(struct rtl_pci_priv) +
				sizeof(struct rtl_priv), &rtl92e_ops);
	if (!hw) {
		RT_ASSERT(false,
			  ("%s : ieee80211 alloc failed\n", pci_name(pdev)));
		err = -ENOMEM;
		goto fail1;
	}
	hw_export = hw;

	SET_IEEE80211_DEV(hw, &pdev->dev);
	pci_set_drvdata(pdev, hw);

	rtlpriv = hw->priv;
	pcipriv = (void *)rtlpriv->priv;
	pcipriv->dev.pdev = pdev;

	/* init cfg & intf_ops */
	rtlpriv->rtlhal.interface = INTF_PCI;
	rtlpriv->cfg = (struct rtl_hal_cfg *)(id->driver_data);
	rtlpriv->intf_ops = &rtl92e_pci_ops;
	rtlpriv->glb_var = &global_var;

	/*
	 *init dbgp flags before all
	 *other functions, because we will
	 *use it in other funtions like
	 *RT_TRACE/RT_PRINT/RTL_PRINT_DATA
	 *you can not use these macro
	 *before this
	 */
	rtl92e_dbgp_flag_init(hw);

	/* MEM map */
	err = pci_request_regions(pdev, KBUILD_MODNAME);
	if (err) {
		RT_ASSERT(false, ("Can't obtain PCI resources\n"));
		return err;
	}

	pmem_start = pci_resource_start(pdev, rtlpriv->cfg->bar_id);
	pmem_len = pci_resource_len(pdev, rtlpriv->cfg->bar_id);
	pmem_flags = pci_resource_flags(pdev, rtlpriv->cfg->bar_id);

	/*shared mem start */
	rtlpriv->io.pci_mem_start =
			(unsigned long)pci_iomap(pdev,
			rtlpriv->cfg->bar_id, pmem_len);
	if (rtlpriv->io.pci_mem_start == 0) {
		RT_ASSERT(false, ("Can't map PCI mem\n"));
		goto fail2;
	}

	RT_TRACE(COMP_INIT, DBG_DMESG,
		 ("mem mapped space: start: 0x%08lx len:%08lx flags:%08lx, after map:0x%08lx\n",
		  pmem_start, pmem_len, pmem_flags,
		  rtlpriv->io.pci_mem_start));

	/* Disable Clk Request */
	pci_write_config_byte(pdev, 0x81, 0);
	/* leave D3 mode */
	pci_write_config_byte(pdev, 0x44, 0);
	pci_write_config_byte(pdev, 0x04, 0x06);
	pci_write_config_byte(pdev, 0x04, 0x07);

	/* The next statement is needed when built as single module */
	rtl_core_module_init();

	/* find adapter */
	/* if chip not support, will return false */
	if (!_rtl_pci_find_adapter(pdev, hw))
		goto fail3;

	/* Init IO handler */
	_rtl_pci_io_handler_init(&pdev->dev, hw);

	/*like read eeprom and so on */
	rtlpriv->cfg->ops->read_eeprom_info(hw);

	if (rtlpriv->cfg->ops->init_sw_vars(hw)) {
		RT_TRACE(COMP_ERR, DBG_EMERG, ("Can't init_sw_vars.\n"));
		goto fail3;
	}

	rtlpriv->cfg->ops->init_sw_leds(hw);

	/*aspm */
	rtl_pci_init_aspm(hw);

	/* Init mac80211 sw */
	err = rtl92e_init_core(hw);
	if (err) {
		RT_TRACE(COMP_ERR, DBG_EMERG,
			 ("Can't allocate sw for mac80211.\n"));
		goto fail3;
	}

	/* Init PCI sw */
	err = !rtl_pci_init(hw, pdev);
	if (err) {
		RT_TRACE(COMP_ERR, DBG_EMERG, ("Failed to init PCI.\n"));
		goto fail3;
	}

	err = ieee80211_register_hw(hw);
	if (err) {
		RT_TRACE(COMP_ERR, DBG_EMERG,
			 ("Can't register mac80211 hw.\n"));
		goto fail3;
	} else {
		rtlpriv->mac80211.mac80211_registered = 1;
	}
	/* the wiphy must have been registed to
	 * cfg80211 prior to regulatory_hint */
	if (regulatory_hint(hw->wiphy, rtlpriv->regd.alpha2))
		RT_TRACE(COMP_ERR, DBG_WARNING, ("regulatory_hint fail\n"));

	/* add for prov */
	rtl_proc_add_one(hw);

	/*init rfkill */
	rtl92e_init_rfkill(hw);

	rtlpci = rtl_pcidev(pcipriv);
	err = rtl_pci_intr_mode_decide(hw);
	if (err) {
		RT_TRACE(COMP_INIT, DBG_DMESG,
			 ("%s: failed to register IRQ handler\n",
			  wiphy_name(hw->wiphy)));
		goto fail3;
	} else {
		rtlpci->irq_alloc = 1;
	}

	set_bit(RTL_STATUS_INTERFACE_START, &rtlpriv->status);
	return 0;

fail3:
	pci_set_drvdata(pdev, NULL);
	rtl92e_deinit_core(hw);
	ieee80211_free_hw(hw);

	if (rtlpriv->io.pci_mem_start != 0)
		pci_iounmap(pdev, (void __iomem *)rtlpriv->io.pci_mem_start);

fail2:
	pci_release_regions(pdev);

fail1:

	pci_disable_device(pdev);

	return -ENODEV;
}
EXPORT_SYMBOL(stg_rtl_pci_probe);

struct ieee80211_hw *rtl_pci_get_hw_pointer(void)
{
	return hw_export;
}
EXPORT_SYMBOL(rtl_pci_get_hw_pointer);

void stg_rtl_pci_disconnect(struct pci_dev *pdev)
{
	struct ieee80211_hw *hw = pci_get_drvdata(pdev);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(pcipriv);
	struct rtl_mac *rtlmac = rtl_mac(rtlpriv);

	clear_bit(RTL_STATUS_INTERFACE_START, &rtlpriv->status);

	/* add for prov */
	rtl_proc_remove_one(hw);

	/*ieee80211_unregister_hw will call ops_stop */
	if (rtlmac->mac80211_registered == 1) {
		ieee80211_unregister_hw(hw);
		rtlmac->mac80211_registered = 0;
	} else {
		rtl92e_deinit_deferred_work(hw);
		rtlpriv->intf_ops->adapter_stop(hw);
	}

	/*deinit rfkill */
	rtl92e_deinit_rfkill(hw);

	rtl_pci_deinit(hw);
	rtl92e_deinit_core(hw);
	rtlpriv->cfg->ops->deinit_sw_vars(hw);

	if (rtlpci->irq_alloc) {
		synchronize_irq(rtlpci->pdev->irq);
		free_irq(rtlpci->pdev->irq, hw);
		rtlpci->irq_alloc = 0;
	}

	if (rtlpci->using_msi)
		pci_disable_msi(rtlpci->pdev);

	list_del(&rtlpriv->list);
	if (rtlpriv->io.pci_mem_start != 0) {
		pci_iounmap(pdev, (void __iomem *)rtlpriv->io.pci_mem_start);
		pci_release_regions(pdev);
	}

	pci_disable_device(pdev);

	rtl_pci_disable_aspm(hw);

	pci_set_drvdata(pdev, NULL);

	ieee80211_free_hw(hw);
}
EXPORT_SYMBOL(stg_rtl_pci_disconnect);

/***************************************
kernel pci power state define:
PCI_D0         ((pci_power_t __force) 0)
PCI_D1         ((pci_power_t __force) 1)
PCI_D2         ((pci_power_t __force) 2)
PCI_D3hot      ((pci_power_t __force) 3)
PCI_D3cold     ((pci_power_t __force) 4)
PCI_UNKNOWN    ((pci_power_t __force) 5)

This function is called when system
goes into suspend state mac80211 will
call rtl_mac_stop() from the mac80211
suspend function first, So there is
no need to call hw_disable here.
****************************************/
int stg_rtl_pci_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct ieee80211_hw *hw = pci_get_drvdata(pdev);
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpriv->cfg->ops->hw_suspend(hw);
	rtl92e_deinit_rfkill(hw);

	return 0;
}
EXPORT_SYMBOL(stg_rtl_pci_suspend);

int stg_rtl_pci_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct ieee80211_hw *hw = pci_get_drvdata(pdev);
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpriv->cfg->ops->hw_resume(hw);
	rtl92e_init_rfkill(hw);

	return 0;
}
EXPORT_SYMBOL(stg_rtl_pci_resume);

struct rtl_intf_ops rtl92e_pci_ops = {
	.read92e_efuse_byte = read92e_efuse_byte,
	.adapter_start = rtl_pci_start,
	.adapter_stop = rtl_pci_stop,
	.check_buddy_priv = rtl_pci_check_buddy_priv,
	.adapter_tx = rtl_pci_tx,
	.flush = rtl_pci_flush,
	.reset_trx_ring = rtl92e_pci_reset_trx_ring,
	.waitq_insert = rtl_pci_tx_chk_waitq_insert,

	.disable_aspm = rtl_pci_disable_aspm,
	.enable_aspm = rtl_pci_enable_aspm,
};
