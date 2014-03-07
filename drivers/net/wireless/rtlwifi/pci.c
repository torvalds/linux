/******************************************************************************
 *
 * Copyright(c) 2009-2012  Realtek Corporation.
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
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

#include "wifi.h"
#include "core.h"
#include "pci.h"
#include "base.h"
#include "ps.h"
#include "efuse.h"
#include <linux/export.h>
#include <linux/kmemleak.h>
#include <linux/module.h>

MODULE_AUTHOR("lizhaoming	<chaoming_li@realsil.com.cn>");
MODULE_AUTHOR("Realtek WlanFAE	<wlanfae@realtek.com>");
MODULE_AUTHOR("Larry Finger	<Larry.FInger@lwfinger.net>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PCI basic driver for rtlwifi");

static const u16 pcibridge_vendors[PCI_BRIDGE_VENDOR_MAX] = {
	PCI_VENDOR_ID_INTEL,
	PCI_VENDOR_ID_ATI,
	PCI_VENDOR_ID_AMD,
	PCI_VENDOR_ID_SI
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
	ppsc->support_aspm = false;

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
			bool support_aspm = false;
			ppsc->support_aspm = support_aspm;
			break;
		}
	case 1:{
			/*Support ASPM. */
			bool support_aspm = true;
			bool support_backdoor = true;
			ppsc->support_aspm = support_aspm;

			/*if (priv->oem_id == RT_CID_TOSHIBA &&
			   !priv->ndis_adapter.amd_l1_patch)
			   support_backdoor = false; */

			ppsc->support_backdoor = support_backdoor;

			break;
		}
	case 2:
		/*ASPM value set by chipset. */
		if (pcibridge_vendor == PCI_BRIDGE_VENDOR_INTEL) {
			bool support_aspm = true;
			ppsc->support_aspm = support_aspm;
		}
		break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "switch case not processed\n");
		break;
	}

	/* toshiba aspm issue, toshiba will set aspm selfly
	 * so we should not set aspm in driver */
	pci_read_config_byte(rtlpci->pdev, 0x80, &init_aspm);
	if (rtlpriv->rtlhal.hw_type == HARDWARE_TYPE_RTL8192SE &&
		init_aspm == 0x43)
		ppsc->support_aspm = false;
}

static bool _rtl_pci_platform_switch_device_pci_aspm(
			struct ieee80211_hw *hw,
			u8 value)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));

	if (rtlhal->hw_type != HARDWARE_TYPE_RTL8192SE)
		value |= 0x40;

	pci_write_config_byte(rtlpci->pdev, 0x80, value);

	return false;
}

/*When we set 0x01 to enable clk request. Set 0x0 to disable clk req.*/
static void _rtl_pci_switch_clk_req(struct ieee80211_hw *hw, u8 value)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));

	pci_write_config_byte(rtlpci->pdev, 0x81, value);

	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8192SE)
		udelay(100);
}

/*Disable RTL8192SE ASPM & Disable Pci Bridge ASPM*/
static void rtl_pci_disable_aspm(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	u8 pcibridge_vendor = pcipriv->ndis_adapter.pcibridge_vendor;
	u8 num4bytes = pcipriv->ndis_adapter.num4bytes;
	/*Retrieve original configuration settings. */
	u8 linkctrl_reg = pcipriv->ndis_adapter.linkctrl_reg;
	u16 pcibridge_linkctrlreg = pcipriv->ndis_adapter.
				pcibridge_linkctrlreg;
	u16 aspmlevel = 0;
	u8 tmp_u1b = 0;

	if (!ppsc->support_aspm)
		return;

	if (pcibridge_vendor == PCI_BRIDGE_VENDOR_UNKNOWN) {
		RT_TRACE(rtlpriv, COMP_POWER, DBG_TRACE,
			 "PCI(Bridge) UNKNOWN\n");

		return;
	}

	if (ppsc->reg_rfps_level & RT_RF_OFF_LEVL_CLK_REQ) {
		RT_CLEAR_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_CLK_REQ);
		_rtl_pci_switch_clk_req(hw, 0x0);
	}

	/*for promising device will in L0 state after an I/O. */
	pci_read_config_byte(rtlpci->pdev, 0x80, &tmp_u1b);

	/*Set corresponding value. */
	aspmlevel |= BIT(0) | BIT(1);
	linkctrl_reg &= ~aspmlevel;
	pcibridge_linkctrlreg &= ~(BIT(0) | BIT(1));

	_rtl_pci_platform_switch_device_pci_aspm(hw, linkctrl_reg);
	udelay(50);

	/*4 Disable Pci Bridge ASPM */
	pci_write_config_byte(rtlpci->pdev, (num4bytes << 2),
			      pcibridge_linkctrlreg);

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
	u8 num4bytes = pcipriv->ndis_adapter.num4bytes;
	u16 aspmlevel;
	u8 u_pcibridge_aspmsetting;
	u8 u_device_aspmsetting;

	if (!ppsc->support_aspm)
		return;

	if (pcibridge_vendor == PCI_BRIDGE_VENDOR_UNKNOWN) {
		RT_TRACE(rtlpriv, COMP_POWER, DBG_TRACE,
			 "PCI(Bridge) UNKNOWN\n");
		return;
	}

	/*4 Enable Pci Bridge ASPM */

	u_pcibridge_aspmsetting =
	    pcipriv->ndis_adapter.pcibridge_linkctrlreg |
	    rtlpci->const_hostpci_aspm_setting;

	if (pcibridge_vendor == PCI_BRIDGE_VENDOR_INTEL)
		u_pcibridge_aspmsetting &= ~BIT(0);

	pci_write_config_byte(rtlpci->pdev, (num4bytes << 2),
			      u_pcibridge_aspmsetting);

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "PlatformEnableASPM(): Write reg[%x] = %x\n",
		 (pcipriv->ndis_adapter.pcibridge_pciehdr_offset + 0x10),
		 u_pcibridge_aspmsetting);

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
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	bool status = false;
	u8 offset_e0;
	unsigned offset_e4;

	pci_write_config_byte(rtlpci->pdev, 0xe0, 0xa0);

	pci_read_config_byte(rtlpci->pdev, 0xe0, &offset_e0);

	if (offset_e0 == 0xA0) {
		pci_read_config_dword(rtlpci->pdev, 0xe4, &offset_e4);
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
			if (tpriv) {
				tpcipriv = (struct rtl_pci_priv *)tpriv->priv;
				RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
					 "pcipriv->ndis_adapter.funcnumber %x\n",
					pcipriv->ndis_adapter.funcnumber);
				RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
					 "tpcipriv->ndis_adapter.funcnumber %x\n",
					tpcipriv->ndis_adapter.funcnumber);

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
	}

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 "find_buddy_priv %d\n", find_buddy_priv);

	if (find_buddy_priv)
		*buddy_priv = tpriv;

	return find_buddy_priv;
}

static void rtl_pci_get_linkcontrol_field(struct ieee80211_hw *hw)
{
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(pcipriv);
	u8 capabilityoffset = pcipriv->ndis_adapter.pcibridge_pciehdr_offset;
	u8 linkctrl_reg;
	u8 num4bbytes;

	num4bbytes = (capabilityoffset + 0x10) / 4;

	/*Read  Link Control Register */
	pci_read_config_byte(rtlpci->pdev, (num4bbytes << 2), &linkctrl_reg);

	pcipriv->ndis_adapter.pcibridge_linkctrlreg = linkctrl_reg;
}

static void rtl_pci_parse_configuration(struct pci_dev *pdev,
		struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);

	u8 tmp;
	u16 linkctrl_reg;

	/*Link Control Register */
	pcie_capability_read_word(pdev, PCI_EXP_LNKCTL, &linkctrl_reg);
	pcipriv->ndis_adapter.linkctrl_reg = (u8)linkctrl_reg;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE, "Link Control Register =%x\n",
		 pcipriv->ndis_adapter.linkctrl_reg);

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

static bool _rtl_update_earlymode_info(struct ieee80211_hw *hw,
		struct sk_buff *skb, struct rtl_tcb_desc *tcb_desc, u8 tid)
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
	struct sk_buff *skb = NULL;
	struct ieee80211_tx_info *info = NULL;
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	int tid;

	if (!rtlpriv->rtlhal.earlymode_enable)
		return;

	if (rtlpriv->dm.supp_phymode_switch &&
	    (rtlpriv->easy_concurrent_ctl.switch_in_process ||
	    (rtlpriv->buddy_priv &&
	    rtlpriv->buddy_priv->easy_concurrent_ctl.switch_in_process)))
		return;
	/* we juse use em for BE/BK/VI/VO */
	for (tid = 7; tid >= 0; tid--) {
		u8 hw_queue = ac_to_hwq[rtl_tid_to_ac(tid)];
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
				_rtl_update_earlymode_info(hw, skb,
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
		struct rtl_tx_desc *entry = &ring->desc[ring->idx];
		struct sk_buff *skb;
		struct ieee80211_tx_info *info;
		__le16 fc;
		u8 tid;

		u8 own = (u8) rtlpriv->cfg->ops->get_desc((u8 *) entry, true,
							  HW_DESC_OWN);

		/*beacon packet will only use the first
		 *descriptor by defaut, and the own may not
		 *be cleared by the hardware
		 */
		if (own)
			return;
		ring->idx = (ring->idx + 1) % ring->entries;

		skb = __skb_dequeue(&ring->queue);
		pci_unmap_single(rtlpci->pdev,
				 rtlpriv->cfg->ops->
					     get_desc((u8 *) entry, true,
						      HW_DESC_TXBUFF_ADDR),
				 skb->len, PCI_DMA_TODEVICE);

		/* remove early mode header */
		if (rtlpriv->rtlhal.earlymode_enable)
			skb_pull(skb, EM_HDR_LEN);

		RT_TRACE(rtlpriv, (COMP_INTR | COMP_SEND), DBG_TRACE,
			 "new ring->idx:%d, free: skb_queue_len:%d, free: seq:%x\n",
			 ring->idx,
			 skb_queue_len(&ring->queue),
			 *(u16 *) (skb->data + 22));

		if (prio == TXCMD_QUEUE) {
			dev_kfree_skb(skb);
			goto tx_status_ok;

		}

		/* for sw LPS, just after NULL skb send out, we can
		 * sure AP knows we are sleeping, we should not let
		 * rf sleep
		 */
		fc = rtl_get_fc(skb);
		if (ieee80211_is_nullfunc(fc)) {
			if (ieee80211_has_pm(fc)) {
				rtlpriv->mac80211.offchan_delay = true;
				rtlpriv->psc.state_inap = true;
			} else {
				rtlpriv->psc.state_inap = false;
			}
		}
		if (ieee80211_is_action(fc)) {
			struct ieee80211_mgmt *action_frame =
				(struct ieee80211_mgmt *)skb->data;
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

		if ((ring->entries - skb_queue_len(&ring->queue))
				== 2) {

			RT_TRACE(rtlpriv, COMP_ERR, DBG_LOUD,
				 "more desc left, wake skb_queue@%d, ring->idx = %d, skb_queue_len = 0x%d\n",
				 prio, ring->idx,
				 skb_queue_len(&ring->queue));

			ieee80211_wake_queue(hw,
					skb_get_queue_mapping
					(skb));
		}
tx_status_ok:
		skb = NULL;
	}

	if (((rtlpriv->link_info.num_rx_inperiod +
		rtlpriv->link_info.num_tx_inperiod) > 8) ||
		(rtlpriv->link_info.num_rx_inperiod > 2)) {
		rtlpriv->enter_ps = false;
		schedule_work(&rtlpriv->works.lps_change_work);
	}
}

static void _rtl_receive_one(struct ieee80211_hw *hw, struct sk_buff *skb,
			     struct ieee80211_rx_status rx_status)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct ieee80211_hdr *hdr = rtl_get_hdr(skb);
	__le16 fc = rtl_get_fc(skb);
	bool unicast = false;
	struct sk_buff *uskb = NULL;
	u8 *pdata;


	memcpy(IEEE80211_SKB_RXCB(skb), &rx_status, sizeof(rx_status));

	if (is_broadcast_ether_addr(hdr->addr1)) {
		;/*TODO*/
	} else if (is_multicast_ether_addr(hdr->addr1)) {
		;/*TODO*/
	} else {
		unicast = true;
		rtlpriv->stats.rxbytesunicast += skb->len;
	}

	rtl_is_special_data(hw, skb, false);

	if (ieee80211_is_data(fc)) {
		rtlpriv->cfg->ops->led_control(hw, LED_CTL_RX);

		if (unicast)
			rtlpriv->link_info.num_rx_inperiod++;
	}

	/* static bcn for roaming */
	rtl_beacon_statistic(hw, skb);
	rtl_p2p_info(hw, (void *)skb->data, skb->len);

	/* for sw lps */
	rtl_swlps_beacon(hw, (void *)skb->data, skb->len);
	rtl_recognize_peer(hw, (void *)skb->data, skb->len);
	if ((rtlpriv->mac80211.opmode == NL80211_IFTYPE_AP) &&
	    (rtlpriv->rtlhal.current_bandtype == BAND_ON_2_4G) &&
	     (ieee80211_is_beacon(fc) || ieee80211_is_probe_resp(fc)))
		return;

	if (unlikely(!rtl_action_proc(hw, skb, false)))
		return;

	uskb = dev_alloc_skb(skb->len + 128);
	if (!uskb)
		return;		/* exit if allocation failed */
	memcpy(IEEE80211_SKB_RXCB(uskb), &rx_status, sizeof(rx_status));
	pdata = (u8 *)skb_put(uskb, skb->len);
	memcpy(pdata, skb->data, skb->len);

	ieee80211_rx_irqsafe(hw, uskb);
}

static void _rtl_pci_rx_interrupt(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	int rx_queue_idx = RTL_PCI_RX_MPDU_QUEUE;

	struct ieee80211_rx_status rx_status = { 0 };
	unsigned int count = rtlpci->rxringcount;
	u8 own;
	u8 tmp_one;
	u32 bufferaddress;

	struct rtl_stats stats = {
		.signal = 0,
		.rate = 0,
	};
	int index = rtlpci->rx_ring[rx_queue_idx].idx;

	if (rtlpci->driver_is_goingto_unload)
		return;
	/*RX NORMAL PKT */
	while (count--) {
		/*rx descriptor */
		struct rtl_rx_desc *pdesc = &rtlpci->rx_ring[rx_queue_idx].desc[
				index];
		/*rx pkt */
		struct sk_buff *skb = rtlpci->rx_ring[rx_queue_idx].rx_buf[
				index];
		struct sk_buff *new_skb = NULL;

		own = (u8) rtlpriv->cfg->ops->get_desc((u8 *) pdesc,
						       false, HW_DESC_OWN);

		/*wait data to be filled by hardware */
		if (own)
			break;

		rtlpriv->cfg->ops->query_rx_desc(hw, &stats,
						 &rx_status,
						 (u8 *) pdesc, skb);

		if (stats.crc || stats.hwerror)
			goto done;

		new_skb = dev_alloc_skb(rtlpci->rxbuffersize);
		if (unlikely(!new_skb)) {
			RT_TRACE(rtlpriv, (COMP_INTR | COMP_RECV), DBG_DMESG,
				 "can't alloc skb for rx\n");
			goto done;
		}
		kmemleak_not_leak(new_skb);

		pci_unmap_single(rtlpci->pdev,
				 *((dma_addr_t *) skb->cb),
				 rtlpci->rxbuffersize,
				 PCI_DMA_FROMDEVICE);

		skb_put(skb, rtlpriv->cfg->ops->get_desc((u8 *) pdesc, false,
			HW_DESC_RXPKT_LEN));
		skb_reserve(skb, stats.rx_drvinfo_size + stats.rx_bufshift);

		/*
		 * NOTICE This can not be use for mac80211,
		 * this is done in mac80211 code,
		 * if you done here sec DHCP will fail
		 * skb_trim(skb, skb->len - 4);
		 */

		_rtl_receive_one(hw, skb, rx_status);

		if (((rtlpriv->link_info.num_rx_inperiod +
		      rtlpriv->link_info.num_tx_inperiod) > 8) ||
		      (rtlpriv->link_info.num_rx_inperiod > 2)) {
			rtlpriv->enter_ps = false;
			schedule_work(&rtlpriv->works.lps_change_work);
		}

		dev_kfree_skb_any(skb);
		skb = new_skb;

		rtlpci->rx_ring[rx_queue_idx].rx_buf[index] = skb;
		*((dma_addr_t *) skb->cb) =
			    pci_map_single(rtlpci->pdev, skb_tail_pointer(skb),
					   rtlpci->rxbuffersize,
					   PCI_DMA_FROMDEVICE);

done:
		bufferaddress = (*((dma_addr_t *)skb->cb));
		if (pci_dma_mapping_error(rtlpci->pdev, bufferaddress))
			return;
		tmp_one = 1;
		rtlpriv->cfg->ops->set_desc((u8 *) pdesc, false,
					    HW_DESC_RXBUFF_ADDR,
					    (u8 *)&bufferaddress);
		rtlpriv->cfg->ops->set_desc((u8 *)pdesc, false,
					    HW_DESC_RXPKT_LEN,
					    (u8 *)&rtlpci->rxbuffersize);

		if (index == rtlpci->rxringcount - 1)
			rtlpriv->cfg->ops->set_desc((u8 *)pdesc, false,
						    HW_DESC_RXERO,
						    &tmp_one);

		rtlpriv->cfg->ops->set_desc((u8 *)pdesc, false, HW_DESC_RXOWN,
					    &tmp_one);

		index = (index + 1) % rtlpci->rxringcount;
	}

	rtlpci->rx_ring[rx_queue_idx].idx = index;
}

static irqreturn_t _rtl_pci_interrupt(int irq, void *dev_id)
{
	struct ieee80211_hw *hw = dev_id;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	unsigned long flags;
	u32 inta = 0;
	u32 intb = 0;
	irqreturn_t ret = IRQ_HANDLED;

	spin_lock_irqsave(&rtlpriv->locks.irq_th_lock, flags);

	/*read ISR: 4/8bytes */
	rtlpriv->cfg->ops->interrupt_recognized(hw, &inta, &intb);

	/*Shared IRQ or HW disappared */
	if (!inta || inta == 0xffff) {
		ret = IRQ_NONE;
		goto done;
	}

	/*<1> beacon related */
	if (inta & rtlpriv->cfg->maps[RTL_IMR_TBDOK]) {
		RT_TRACE(rtlpriv, COMP_INTR, DBG_TRACE,
			 "beacon ok interrupt!\n");
	}

	if (unlikely(inta & rtlpriv->cfg->maps[RTL_IMR_TBDER])) {
		RT_TRACE(rtlpriv, COMP_INTR, DBG_TRACE,
			 "beacon err interrupt!\n");
	}

	if (inta & rtlpriv->cfg->maps[RTL_IMR_BDOK]) {
		RT_TRACE(rtlpriv, COMP_INTR, DBG_TRACE, "beacon interrupt!\n");
	}

	if (inta & rtlpriv->cfg->maps[RTL_IMR_BCNINT]) {
		RT_TRACE(rtlpriv, COMP_INTR, DBG_TRACE,
			 "prepare beacon for interrupt!\n");
		tasklet_schedule(&rtlpriv->works.irq_prepare_bcn_tasklet);
	}

	/*<3> Tx related */
	if (unlikely(inta & rtlpriv->cfg->maps[RTL_IMR_TXFOVW]))
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING, "IMR_TXFOVW!\n");

	if (inta & rtlpriv->cfg->maps[RTL_IMR_MGNTDOK]) {
		RT_TRACE(rtlpriv, COMP_INTR, DBG_TRACE,
			 "Manage ok interrupt!\n");
		_rtl_pci_tx_isr(hw, MGNT_QUEUE);
	}

	if (inta & rtlpriv->cfg->maps[RTL_IMR_HIGHDOK]) {
		RT_TRACE(rtlpriv, COMP_INTR, DBG_TRACE,
			 "HIGH_QUEUE ok interrupt!\n");
		_rtl_pci_tx_isr(hw, HIGH_QUEUE);
	}

	if (inta & rtlpriv->cfg->maps[RTL_IMR_BKDOK]) {
		rtlpriv->link_info.num_tx_inperiod++;

		RT_TRACE(rtlpriv, COMP_INTR, DBG_TRACE,
			 "BK Tx OK interrupt!\n");
		_rtl_pci_tx_isr(hw, BK_QUEUE);
	}

	if (inta & rtlpriv->cfg->maps[RTL_IMR_BEDOK]) {
		rtlpriv->link_info.num_tx_inperiod++;

		RT_TRACE(rtlpriv, COMP_INTR, DBG_TRACE,
			 "BE TX OK interrupt!\n");
		_rtl_pci_tx_isr(hw, BE_QUEUE);
	}

	if (inta & rtlpriv->cfg->maps[RTL_IMR_VIDOK]) {
		rtlpriv->link_info.num_tx_inperiod++;

		RT_TRACE(rtlpriv, COMP_INTR, DBG_TRACE,
			 "VI TX OK interrupt!\n");
		_rtl_pci_tx_isr(hw, VI_QUEUE);
	}

	if (inta & rtlpriv->cfg->maps[RTL_IMR_VODOK]) {
		rtlpriv->link_info.num_tx_inperiod++;

		RT_TRACE(rtlpriv, COMP_INTR, DBG_TRACE,
			 "Vo TX OK interrupt!\n");
		_rtl_pci_tx_isr(hw, VO_QUEUE);
	}

	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8192SE) {
		if (inta & rtlpriv->cfg->maps[RTL_IMR_COMDOK]) {
			rtlpriv->link_info.num_tx_inperiod++;

			RT_TRACE(rtlpriv, COMP_INTR, DBG_TRACE,
				 "CMD TX OK interrupt!\n");
			_rtl_pci_tx_isr(hw, TXCMD_QUEUE);
		}
	}

	/*<2> Rx related */
	if (inta & rtlpriv->cfg->maps[RTL_IMR_ROK]) {
		RT_TRACE(rtlpriv, COMP_INTR, DBG_TRACE, "Rx ok interrupt!\n");
		_rtl_pci_rx_interrupt(hw);
	}

	if (unlikely(inta & rtlpriv->cfg->maps[RTL_IMR_RDU])) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
			 "rx descriptor unavailable!\n");
		_rtl_pci_rx_interrupt(hw);
	}

	if (unlikely(inta & rtlpriv->cfg->maps[RTL_IMR_RXFOVW])) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING, "rx overflow !\n");
		_rtl_pci_rx_interrupt(hw);
	}

	/*fw related*/
	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8723AE) {
		if (inta & rtlpriv->cfg->maps[RTL_IMR_C2HCMD]) {
			RT_TRACE(rtlpriv, COMP_INTR, DBG_TRACE,
				 "firmware interrupt!\n");
			queue_delayed_work(rtlpriv->works.rtl_wq,
					   &rtlpriv->works.fwevt_wq, 0);
		}
	}

	if (rtlpriv->rtlhal.earlymode_enable)
		tasklet_schedule(&rtlpriv->works.irq_tasklet);

done:
	spin_unlock_irqrestore(&rtlpriv->locks.irq_th_lock, flags);
	return ret;
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
	u8 temp_one = 1;

	memset(&tcb_desc, 0, sizeof(struct rtl_tcb_desc));
	ring = &rtlpci->tx_ring[BEACON_QUEUE];
	pskb = __skb_dequeue(&ring->queue);
	if (pskb) {
		struct rtl_tx_desc *entry = &ring->desc[ring->idx];
		pci_unmap_single(rtlpci->pdev, rtlpriv->cfg->ops->get_desc(
				 (u8 *) entry, true, HW_DESC_TXBUFF_ADDR),
				 pskb->len, PCI_DMA_TODEVICE);
		kfree_skb(pskb);
	}

	/*NB: the beacon data buffer must be 32-bit aligned. */
	pskb = ieee80211_beacon_get(hw, mac->vif);
	if (pskb == NULL)
		return;
	hdr = rtl_get_hdr(pskb);
	info = IEEE80211_SKB_CB(pskb);
	pdesc = &ring->desc[0];
	rtlpriv->cfg->ops->fill_tx_desc(hw, hdr, (u8 *) pdesc,
		info, NULL, pskb, BEACON_QUEUE, &tcb_desc);

	__skb_queue_tail(&ring->queue, pskb);

	rtlpriv->cfg->ops->set_desc((u8 *) pdesc, true, HW_DESC_OWN,
				    &temp_one);

	return;
}

static void _rtl_pci_init_trx_var(struct ieee80211_hw *hw)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	u8 i;

	for (i = 0; i < RTL_PCI_MAX_TX_QUEUE_COUNT; i++)
		rtlpci->txringcount[i] = RT_TXDESC_NUM;

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

	rtlpci->up_first_time = true;
	rtlpci->being_init_adapter = false;

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
	INIT_WORK(&rtlpriv->works.lps_change_work,
		  rtl_lps_change_work_callback);
}

static int _rtl_pci_init_tx_ring(struct ieee80211_hw *hw,
				 unsigned int prio, unsigned int entries)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_tx_desc *ring;
	dma_addr_t dma;
	u32 nextdescaddress;
	int i;

	ring = pci_alloc_consistent(rtlpci->pdev,
				    sizeof(*ring) * entries, &dma);

	if (!ring || (unsigned long)ring & 0xFF) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "Cannot allocate TX ring (prio = %d)\n", prio);
		return -ENOMEM;
	}

	memset(ring, 0, sizeof(*ring) * entries);
	rtlpci->tx_ring[prio].desc = ring;
	rtlpci->tx_ring[prio].dma = dma;
	rtlpci->tx_ring[prio].idx = 0;
	rtlpci->tx_ring[prio].entries = entries;
	skb_queue_head_init(&rtlpci->tx_ring[prio].queue);

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "queue:%d, ring_addr:%p\n",
		 prio, ring);

	for (i = 0; i < entries; i++) {
		nextdescaddress = (u32) dma +
					      ((i + 1) % entries) *
					      sizeof(*ring);

		rtlpriv->cfg->ops->set_desc((u8 *)&(ring[i]),
					    true, HW_DESC_TX_NEXTDESC_ADDR,
					    (u8 *)&nextdescaddress);
	}

	return 0;
}

static int _rtl_pci_init_rx_ring(struct ieee80211_hw *hw)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_rx_desc *entry = NULL;
	int i, rx_queue_idx;
	u8 tmp_one = 1;

	/*
	 *rx_queue_idx 0:RX_MPDU_QUEUE
	 *rx_queue_idx 1:RX_CMD_QUEUE
	 */
	for (rx_queue_idx = 0; rx_queue_idx < RTL_PCI_MAX_RX_QUEUE;
	     rx_queue_idx++) {
		rtlpci->rx_ring[rx_queue_idx].desc =
		    pci_alloc_consistent(rtlpci->pdev,
					 sizeof(*rtlpci->rx_ring[rx_queue_idx].
						desc) * rtlpci->rxringcount,
					 &rtlpci->rx_ring[rx_queue_idx].dma);

		if (!rtlpci->rx_ring[rx_queue_idx].desc ||
		    (unsigned long)rtlpci->rx_ring[rx_queue_idx].desc & 0xFF) {
			RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
				 "Cannot allocate RX ring\n");
			return -ENOMEM;
		}

		memset(rtlpci->rx_ring[rx_queue_idx].desc, 0,
		       sizeof(*rtlpci->rx_ring[rx_queue_idx].desc) *
		       rtlpci->rxringcount);

		rtlpci->rx_ring[rx_queue_idx].idx = 0;

		/* If amsdu_8k is disabled, set buffersize to 4096. This
		 * change will reduce memory fragmentation.
		 */
		if (rtlpci->rxbuffersize > 4096 &&
		    rtlpriv->rtlhal.disable_amsdu_8k)
			rtlpci->rxbuffersize = 4096;

		for (i = 0; i < rtlpci->rxringcount; i++) {
			struct sk_buff *skb =
			    dev_alloc_skb(rtlpci->rxbuffersize);
			u32 bufferaddress;
			if (!skb)
				return 0;
			kmemleak_not_leak(skb);
			entry = &rtlpci->rx_ring[rx_queue_idx].desc[i];

			/*skb->dev = dev; */

			rtlpci->rx_ring[rx_queue_idx].rx_buf[i] = skb;

			/*
			 *just set skb->cb to mapping addr
			 *for pci_unmap_single use
			 */
			*((dma_addr_t *) skb->cb) =
			    pci_map_single(rtlpci->pdev, skb_tail_pointer(skb),
					   rtlpci->rxbuffersize,
					   PCI_DMA_FROMDEVICE);

			bufferaddress = (*((dma_addr_t *)skb->cb));
			if (pci_dma_mapping_error(rtlpci->pdev, bufferaddress)) {
				dev_kfree_skb_any(skb);
				return 1;
			}
			rtlpriv->cfg->ops->set_desc((u8 *)entry, false,
						    HW_DESC_RXBUFF_ADDR,
						    (u8 *)&bufferaddress);
			rtlpriv->cfg->ops->set_desc((u8 *)entry, false,
						    HW_DESC_RXPKT_LEN,
						    (u8 *)&rtlpci->
						    rxbuffersize);
			rtlpriv->cfg->ops->set_desc((u8 *) entry, false,
						    HW_DESC_RXOWN,
						    &tmp_one);
		}

		rtlpriv->cfg->ops->set_desc((u8 *) entry, false,
					    HW_DESC_RXERO, &tmp_one);
	}
	return 0;
}

static void _rtl_pci_free_tx_ring(struct ieee80211_hw *hw,
		unsigned int prio)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl8192_tx_ring *ring = &rtlpci->tx_ring[prio];

	while (skb_queue_len(&ring->queue)) {
		struct rtl_tx_desc *entry = &ring->desc[ring->idx];
		struct sk_buff *skb = __skb_dequeue(&ring->queue);

		pci_unmap_single(rtlpci->pdev,
				 rtlpriv->cfg->
					     ops->get_desc((u8 *) entry, true,
						   HW_DESC_TXBUFF_ADDR),
				 skb->len, PCI_DMA_TODEVICE);
		kfree_skb(skb);
		ring->idx = (ring->idx + 1) % ring->entries;
	}

	if (ring->desc) {
		pci_free_consistent(rtlpci->pdev,
				    sizeof(*ring->desc) * ring->entries,
				    ring->desc, ring->dma);
		ring->desc = NULL;
	}
}

static void _rtl_pci_free_rx_ring(struct rtl_pci *rtlpci)
{
	int i, rx_queue_idx;

	/*rx_queue_idx 0:RX_MPDU_QUEUE */
	/*rx_queue_idx 1:RX_CMD_QUEUE */
	for (rx_queue_idx = 0; rx_queue_idx < RTL_PCI_MAX_RX_QUEUE;
	     rx_queue_idx++) {
		for (i = 0; i < rtlpci->rxringcount; i++) {
			struct sk_buff *skb =
			    rtlpci->rx_ring[rx_queue_idx].rx_buf[i];
			if (!skb)
				continue;

			pci_unmap_single(rtlpci->pdev,
					 *((dma_addr_t *) skb->cb),
					 rtlpci->rxbuffersize,
					 PCI_DMA_FROMDEVICE);
			kfree_skb(skb);
		}

		if (rtlpci->rx_ring[rx_queue_idx].desc) {
			pci_free_consistent(rtlpci->pdev,
				    sizeof(*rtlpci->rx_ring[rx_queue_idx].
					   desc) * rtlpci->rxringcount,
				    rtlpci->rx_ring[rx_queue_idx].desc,
				    rtlpci->rx_ring[rx_queue_idx].dma);
			rtlpci->rx_ring[rx_queue_idx].desc = NULL;
		}
	}
}

static int _rtl_pci_init_trx_ring(struct ieee80211_hw *hw)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	int ret;
	int i;

	ret = _rtl_pci_init_rx_ring(hw);
	if (ret)
		return ret;

	for (i = 0; i < RTL_PCI_MAX_TX_QUEUE_COUNT; i++) {
		ret = _rtl_pci_init_tx_ring(hw, i,
				 rtlpci->txringcount[i]);
		if (ret)
			goto err_free_rings;
	}

	return 0;

err_free_rings:
	_rtl_pci_free_rx_ring(rtlpci);

	for (i = 0; i < RTL_PCI_MAX_TX_QUEUE_COUNT; i++)
		if (rtlpci->tx_ring[i].desc)
			_rtl_pci_free_tx_ring(hw, i);

	return 1;
}

static int _rtl_pci_deinit_trx_ring(struct ieee80211_hw *hw)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	u32 i;

	/*free rx rings */
	_rtl_pci_free_rx_ring(rtlpci);

	/*free tx rings */
	for (i = 0; i < RTL_PCI_MAX_TX_QUEUE_COUNT; i++)
		_rtl_pci_free_tx_ring(hw, i);

	return 0;
}

int rtl_pci_reset_trx_ring(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	int i, rx_queue_idx;
	unsigned long flags;
	u8 tmp_one = 1;

	/*rx_queue_idx 0:RX_MPDU_QUEUE */
	/*rx_queue_idx 1:RX_CMD_QUEUE */
	for (rx_queue_idx = 0; rx_queue_idx < RTL_PCI_MAX_RX_QUEUE;
	     rx_queue_idx++) {
		/*
		 *force the rx_ring[RX_MPDU_QUEUE/
		 *RX_CMD_QUEUE].idx to the first one
		 */
		if (rtlpci->rx_ring[rx_queue_idx].desc) {
			struct rtl_rx_desc *entry = NULL;

			for (i = 0; i < rtlpci->rxringcount; i++) {
				entry = &rtlpci->rx_ring[rx_queue_idx].desc[i];
				rtlpriv->cfg->ops->set_desc((u8 *) entry,
							    false,
							    HW_DESC_RXOWN,
							    &tmp_one);
			}
			rtlpci->rx_ring[rx_queue_idx].idx = 0;
		}
	}

	/*
	 *after reset, release previous pending packet,
	 *and force the  tx idx to the first one
	 */
	for (i = 0; i < RTL_PCI_MAX_TX_QUEUE_COUNT; i++) {
		if (rtlpci->tx_ring[i].desc) {
			struct rtl8192_tx_ring *ring = &rtlpci->tx_ring[i];

			while (skb_queue_len(&ring->queue)) {
				struct rtl_tx_desc *entry;
				struct sk_buff *skb;

				spin_lock_irqsave(&rtlpriv->locks.irq_th_lock,
						  flags);
				entry = &ring->desc[ring->idx];
				skb = __skb_dequeue(&ring->queue);
				pci_unmap_single(rtlpci->pdev,
						 rtlpriv->cfg->ops->
							 get_desc((u8 *)
							 entry,
							 true,
							 HW_DESC_TXBUFF_ADDR),
						 skb->len, PCI_DMA_TODEVICE);
				ring->idx = (ring->idx + 1) % ring->entries;
				spin_unlock_irqrestore(&rtlpriv->locks.irq_th_lock,
						  flags);
				kfree_skb(skb);
			}
			ring->idx = 0;
		}
	}

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

	if (!rtlpriv->rtlhal.earlymode_enable)
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
	u8 idx;
	u8 hw_queue = _rtl_mac_to_hwqueue(hw, skb);
	unsigned long flags;
	struct ieee80211_hdr *hdr = rtl_get_hdr(skb);
	__le16 fc = rtl_get_fc(skb);
	u8 *pda_addr = hdr->addr1;
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	/*ssn */
	u8 tid = 0;
	u16 seq_number = 0;
	u8 own;
	u8 temp_one = 1;

	if (ieee80211_is_mgmt(fc))
		rtl_tx_mgmt_proc(hw, skb);

	if (rtlpriv->psc.sw_ps_enabled) {
		if (ieee80211_is_data(fc) && !ieee80211_is_nullfunc(fc) &&
			!ieee80211_has_pm(fc))
			hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_PM);
	}

	rtl_action_proc(hw, skb, true);

	if (is_multicast_ether_addr(pda_addr))
		rtlpriv->stats.txbytesmulticast += skb->len;
	else if (is_broadcast_ether_addr(pda_addr))
		rtlpriv->stats.txbytesbroadcast += skb->len;
	else
		rtlpriv->stats.txbytesunicast += skb->len;

	spin_lock_irqsave(&rtlpriv->locks.irq_th_lock, flags);
	ring = &rtlpci->tx_ring[hw_queue];
	if (hw_queue != BEACON_QUEUE)
		idx = (ring->idx + skb_queue_len(&ring->queue)) %
				ring->entries;
	else
		idx = 0;

	pdesc = &ring->desc[idx];
	own = (u8) rtlpriv->cfg->ops->get_desc((u8 *) pdesc,
			true, HW_DESC_OWN);

	if ((own == 1) && (hw_queue != BEACON_QUEUE)) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
			 "No more TX desc@%d, ring->idx = %d, idx = %d, skb_queue_len = 0x%d\n",
			 hw_queue, ring->idx, idx,
			 skb_queue_len(&ring->queue));

		spin_unlock_irqrestore(&rtlpriv->locks.irq_th_lock, flags);
		return skb->len;
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
			info, sta, skb, hw_queue, ptcb_desc);

	__skb_queue_tail(&ring->queue, skb);

	rtlpriv->cfg->ops->set_desc((u8 *)pdesc, true,
				    HW_DESC_OWN, &temp_one);


	if ((ring->entries - skb_queue_len(&ring->queue)) < 2 &&
	    hw_queue != BEACON_QUEUE) {

		RT_TRACE(rtlpriv, COMP_ERR, DBG_LOUD,
			 "less desc left, stop skb_queue@%d, ring->idx = %d, idx = %d, skb_queue_len = 0x%d\n",
			 hw_queue, ring->idx, idx,
			 skb_queue_len(&ring->queue));

		ieee80211_stop_queue(hw, skb_get_queue_mapping(skb));
	}

	spin_unlock_irqrestore(&rtlpriv->locks.irq_th_lock, flags);

	rtlpriv->cfg->ops->tx_polling(hw, hw_queue);

	return 0;
}

static void rtl_pci_flush(struct ieee80211_hw *hw, bool drop)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	u16 i = 0;
	int queue_id;
	struct rtl8192_tx_ring *ring;

	if (mac->skip_scan)
		return;

	for (queue_id = RTL_PCI_MAX_TX_QUEUE_COUNT - 1; queue_id >= 0;) {
		u32 queue_len;
		ring = &pcipriv->dev.tx_ring[queue_id];
		queue_len = skb_queue_len(&ring->queue);
		if (queue_len == 0 || queue_id == BEACON_QUEUE ||
			queue_id == TXCMD_QUEUE) {
			queue_id--;
			continue;
		} else {
			msleep(20);
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
	cancel_work_sync(&rtlpriv->works.lps_change_work);

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
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "tx ring initialization failed\n");
		return err;
	}

	return 0;
}

static int rtl_pci_start(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));

	int err;

	rtl_pci_reset_trx_ring(hw);

	rtlpci->driver_is_goingto_unload = false;
	err = rtlpriv->cfg->ops->hw_init(hw);
	if (err) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
			 "Failed to config hardware!\n");
		return err;
	}

	rtlpriv->cfg->ops->enable_interrupt(hw);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, "enable_interrupt OK\n");

	rtl_init_rx_config(hw);

	/*should be after adapter start and interrupt enable. */
	set_hal_start(rtlhal);

	RT_CLEAR_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_HALT_NIC);

	rtlpci->up_first_time = false;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "OK\n");
	return 0;
}

static void rtl_pci_stop(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	unsigned long flags;
	u8 RFInProgressTimeOut = 0;

	/*
	 *should be before disable interrupt&adapter
	 *and will do it immediately.
	 */
	set_hal_stop(rtlhal);

	rtlpci->driver_is_goingto_unload = true;
	rtlpriv->cfg->ops->disable_interrupt(hw);
	cancel_work_sync(&rtlpriv->works.lps_change_work);

	spin_lock_irqsave(&rtlpriv->locks.rf_ps_lock, flags);
	while (ppsc->rfchange_inprogress) {
		spin_unlock_irqrestore(&rtlpriv->locks.rf_ps_lock, flags);
		if (RFInProgressTimeOut > 100) {
			spin_lock_irqsave(&rtlpriv->locks.rf_ps_lock, flags);
			break;
		}
		mdelay(1);
		RFInProgressTimeOut++;
		spin_lock_irqsave(&rtlpriv->locks.rf_ps_lock, flags);
	}
	ppsc->rfchange_inprogress = true;
	spin_unlock_irqrestore(&rtlpriv->locks.rf_ps_lock, flags);

	rtlpriv->cfg->ops->hw_disable(hw);
	/* some things are not needed if firmware not available */
	if (!rtlpriv->max_fw_size)
		return;
	rtlpriv->cfg->ops->led_control(hw, LED_CTL_POWER_OFF);

	spin_lock_irqsave(&rtlpriv->locks.rf_ps_lock, flags);
	ppsc->rfchange_inprogress = false;
	spin_unlock_irqrestore(&rtlpriv->locks.rf_ps_lock, flags);

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

	pcipriv->ndis_adapter.pcibridge_vendor = PCI_BRIDGE_VENDOR_UNKNOWN;
	venderid = pdev->vendor;
	deviceid = pdev->device;
	pci_read_config_byte(pdev, 0x8, &revisionid);
	pci_read_config_word(pdev, 0x3C, &irqline);

	/* PCI ID 0x10ec:0x8192 occurs for both RTL8192E, which uses
	 * r8192e_pci, and RTL8192SE, which uses this driver. If the
	 * revision ID is RTL_PCI_REVISION_ID_8192PCIE (0x01), then
	 * the correct driver is r8192e_pci, thus this routine should
	 * return false.
	 */
	if (deviceid == RTL_PCI_8192SE_DID &&
	    revisionid == RTL_PCI_REVISION_ID_8192PCIE)
		return false;

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
			RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
				 "8192 PCI-E is found - vid/did=%x/%x\n",
				 venderid, deviceid);
			rtlhal->hw_type = HARDWARE_TYPE_RTL8192E;
			return false;
		case RTL_PCI_REVISION_ID_8192SE:
			RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
				 "8192SE is found - vid/did=%x/%x\n",
				 venderid, deviceid);
			rtlhal->hw_type = HARDWARE_TYPE_RTL8192SE;
			break;
		default:
			RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
				 "Err: Unknown device - vid/did=%x/%x\n",
				 venderid, deviceid);
			rtlhal->hw_type = HARDWARE_TYPE_RTL8192SE;
			break;

		}
	} else if (deviceid == RTL_PCI_8723AE_DID) {
		rtlhal->hw_type = HARDWARE_TYPE_RTL8723AE;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
			 "8723AE PCI-E is found - "
			 "vid/did=%x/%x\n", venderid, deviceid);
	} else if (deviceid == RTL_PCI_8192CET_DID ||
		   deviceid == RTL_PCI_8192CE_DID ||
		   deviceid == RTL_PCI_8191CE_DID ||
		   deviceid == RTL_PCI_8188CE_DID) {
		rtlhal->hw_type = HARDWARE_TYPE_RTL8192CE;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
			 "8192C PCI-E is found - vid/did=%x/%x\n",
			 venderid, deviceid);
	} else if (deviceid == RTL_PCI_8192DE_DID ||
		   deviceid == RTL_PCI_8192DE_DID2) {
		rtlhal->hw_type = HARDWARE_TYPE_RTL8192DE;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
			 "8192D PCI-E is found - vid/did=%x/%x\n",
			 venderid, deviceid);
	} else if (deviceid == RTL_PCI_8188EE_DID) {
		rtlhal->hw_type = HARDWARE_TYPE_RTL8188EE;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
			 "Find adapter, Hardware type is 8188EE\n");
	} else {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
			 "Err: Unknown device - vid/did=%x/%x\n",
			 venderid, deviceid);

		rtlhal->hw_type = RTL_DEFAULT_HARDWARE_TYPE;
	}

	if (rtlhal->hw_type == HARDWARE_TYPE_RTL8192DE) {
		if (revisionid == 0 || revisionid == 1) {
			if (revisionid == 0) {
				RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
					 "Find 92DE MAC0\n");
				rtlhal->interfaceindex = 0;
			} else if (revisionid == 1) {
				RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
					 "Find 92DE MAC1\n");
				rtlhal->interfaceindex = 1;
			}
		} else {
			RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
				 "Unknown device - VendorID/DeviceID=%x/%x, Revision=%x\n",
				 venderid, deviceid, revisionid);
			rtlhal->interfaceindex = 0;
		}
	}
	/*find bus info */
	pcipriv->ndis_adapter.busnumber = pdev->bus->number;
	pcipriv->ndis_adapter.devnumber = PCI_SLOT(pdev->devfn);
	pcipriv->ndis_adapter.funcnumber = PCI_FUNC(pdev->devfn);

	/* some ARM have no bridge_pdev and will crash here
	 * so we should check if bridge_pdev is NULL
	 */
	if (bridge_pdev) {
		/*find bridge info if available */
		pcipriv->ndis_adapter.pcibridge_vendorid = bridge_pdev->vendor;
		for (tmp = 0; tmp < PCI_BRIDGE_VENDOR_MAX; tmp++) {
			if (bridge_pdev->vendor == pcibridge_vendors[tmp]) {
				pcipriv->ndis_adapter.pcibridge_vendor = tmp;
				RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
					 "Pci Bridge Vendor is found index: %d\n",
					 tmp);
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

	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
		 "pcidev busnumber:devnumber:funcnumber:vendor:link_ctl %d:%d:%d:%x:%x\n",
		 pcipriv->ndis_adapter.busnumber,
		 pcipriv->ndis_adapter.devnumber,
		 pcipriv->ndis_adapter.funcnumber,
		 pdev->vendor, pcipriv->ndis_adapter.linkctrl_reg);

	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
		 "pci_bridge busnumber:devnumber:funcnumber:vendor:pcie_cap:link_ctl_reg:amd %d:%d:%d:%x:%x:%x:%x\n",
		 pcipriv->ndis_adapter.pcibridge_busnum,
		 pcipriv->ndis_adapter.pcibridge_devnum,
		 pcipriv->ndis_adapter.pcibridge_funcnum,
		 pcibridge_vendors[pcipriv->ndis_adapter.pcibridge_vendor],
		 pcipriv->ndis_adapter.pcibridge_pciehdr_offset,
		 pcipriv->ndis_adapter.pcibridge_linkctrlreg,
		 pcipriv->ndis_adapter.amd_l1_patch);

	rtl_pci_parse_configuration(pdev, hw);
	list_add_tail(&rtlpriv->list, &rtlpriv->glb_var->glb_priv_list);

	return true;
}

int rtl_pci_probe(struct pci_dev *pdev,
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
		RT_ASSERT(false, "%s : Cannot enable new PCI device\n",
			  pci_name(pdev));
		return err;
	}

	if (!pci_set_dma_mask(pdev, DMA_BIT_MASK(32))) {
		if (pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32))) {
			RT_ASSERT(false,
				  "Unable to obtain 32bit DMA for consistent allocations\n");
			err = -ENOMEM;
			goto fail1;
		}
	}

	pci_set_master(pdev);

	hw = ieee80211_alloc_hw(sizeof(struct rtl_pci_priv) +
				sizeof(struct rtl_priv), &rtl_ops);
	if (!hw) {
		RT_ASSERT(false,
			  "%s : ieee80211 alloc failed\n", pci_name(pdev));
		err = -ENOMEM;
		goto fail1;
	}

	SET_IEEE80211_DEV(hw, &pdev->dev);
	pci_set_drvdata(pdev, hw);

	rtlpriv = hw->priv;
	rtlpriv->hw = hw;
	pcipriv = (void *)rtlpriv->priv;
	pcipriv->dev.pdev = pdev;
	init_completion(&rtlpriv->firmware_loading_complete);

	/* init cfg & intf_ops */
	rtlpriv->rtlhal.interface = INTF_PCI;
	rtlpriv->cfg = (struct rtl_hal_cfg *)(id->driver_data);
	rtlpriv->intf_ops = &rtl_pci_ops;
	rtlpriv->glb_var = &rtl_global_var;

	/*
	 *init dbgp flags before all
	 *other functions, because we will
	 *use it in other funtions like
	 *RT_TRACE/RT_PRINT/RTL_PRINT_DATA
	 *you can not use these macro
	 *before this
	 */
	rtl_dbgp_flag_init(hw);

	/* MEM map */
	err = pci_request_regions(pdev, KBUILD_MODNAME);
	if (err) {
		RT_ASSERT(false, "Can't obtain PCI resources\n");
		goto fail1;
	}

	pmem_start = pci_resource_start(pdev, rtlpriv->cfg->bar_id);
	pmem_len = pci_resource_len(pdev, rtlpriv->cfg->bar_id);
	pmem_flags = pci_resource_flags(pdev, rtlpriv->cfg->bar_id);

	/*shared mem start */
	rtlpriv->io.pci_mem_start =
			(unsigned long)pci_iomap(pdev,
			rtlpriv->cfg->bar_id, pmem_len);
	if (rtlpriv->io.pci_mem_start == 0) {
		RT_ASSERT(false, "Can't map PCI mem\n");
		err = -ENOMEM;
		goto fail2;
	}

	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
		 "mem mapped space: start: 0x%08lx len:%08lx flags:%08lx, after map:0x%08lx\n",
		 pmem_start, pmem_len, pmem_flags,
		 rtlpriv->io.pci_mem_start);

	/* Disable Clk Request */
	pci_write_config_byte(pdev, 0x81, 0);
	/* leave D3 mode */
	pci_write_config_byte(pdev, 0x44, 0);
	pci_write_config_byte(pdev, 0x04, 0x06);
	pci_write_config_byte(pdev, 0x04, 0x07);

	/* find adapter */
	if (!_rtl_pci_find_adapter(pdev, hw)) {
		err = -ENODEV;
		goto fail3;
	}

	/* Init IO handler */
	_rtl_pci_io_handler_init(&pdev->dev, hw);

	/*like read eeprom and so on */
	rtlpriv->cfg->ops->read_eeprom_info(hw);

	/*aspm */
	rtl_pci_init_aspm(hw);

	/* Init mac80211 sw */
	err = rtl_init_core(hw);
	if (err) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "Can't allocate sw for mac80211\n");
		goto fail3;
	}

	/* Init PCI sw */
	err = rtl_pci_init(hw, pdev);
	if (err) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "Failed to init PCI\n");
		goto fail3;
	}

	if (rtlpriv->cfg->ops->init_sw_vars(hw)) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, "Can't init_sw_vars\n");
		err = -ENODEV;
		goto fail3;
	}

	rtlpriv->cfg->ops->init_sw_leds(hw);

	err = sysfs_create_group(&pdev->dev.kobj, &rtl_attribute_group);
	if (err) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 "failed to create sysfs device attributes\n");
		goto fail3;
	}

	rtlpci = rtl_pcidev(pcipriv);
	err = request_irq(rtlpci->pdev->irq, &_rtl_pci_interrupt,
			  IRQF_SHARED, KBUILD_MODNAME, hw);
	if (err) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
			 "%s: failed to register IRQ handler\n",
			 wiphy_name(hw->wiphy));
		goto fail3;
	}
	rtlpci->irq_alloc = 1;

	return 0;

fail3:
	rtl_deinit_core(hw);

	if (rtlpriv->io.pci_mem_start != 0)
		pci_iounmap(pdev, (void __iomem *)rtlpriv->io.pci_mem_start);

fail2:
	pci_release_regions(pdev);
	complete(&rtlpriv->firmware_loading_complete);

fail1:
	if (hw)
		ieee80211_free_hw(hw);
	pci_disable_device(pdev);

	return err;

}
EXPORT_SYMBOL(rtl_pci_probe);

void rtl_pci_disconnect(struct pci_dev *pdev)
{
	struct ieee80211_hw *hw = pci_get_drvdata(pdev);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(pcipriv);
	struct rtl_mac *rtlmac = rtl_mac(rtlpriv);

	/* just in case driver is removed before firmware callback */
	wait_for_completion(&rtlpriv->firmware_loading_complete);
	clear_bit(RTL_STATUS_INTERFACE_START, &rtlpriv->status);

	sysfs_remove_group(&pdev->dev.kobj, &rtl_attribute_group);

	/*ieee80211_unregister_hw will call ops_stop */
	if (rtlmac->mac80211_registered == 1) {
		ieee80211_unregister_hw(hw);
		rtlmac->mac80211_registered = 0;
	} else {
		rtl_deinit_deferred_work(hw);
		rtlpriv->intf_ops->adapter_stop(hw);
	}
	rtlpriv->cfg->ops->disable_interrupt(hw);

	/*deinit rfkill */
	rtl_deinit_rfkill(hw);

	rtl_pci_deinit(hw);
	rtl_deinit_core(hw);
	rtlpriv->cfg->ops->deinit_sw_vars(hw);

	if (rtlpci->irq_alloc) {
		synchronize_irq(rtlpci->pdev->irq);
		free_irq(rtlpci->pdev->irq, hw);
		rtlpci->irq_alloc = 0;
	}

	list_del(&rtlpriv->list);
	if (rtlpriv->io.pci_mem_start != 0) {
		pci_iounmap(pdev, (void __iomem *)rtlpriv->io.pci_mem_start);
		pci_release_regions(pdev);
	}

	pci_disable_device(pdev);

	rtl_pci_disable_aspm(hw);

	ieee80211_free_hw(hw);
}
EXPORT_SYMBOL(rtl_pci_disconnect);

#ifdef CONFIG_PM_SLEEP
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
int rtl_pci_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct ieee80211_hw *hw = pci_get_drvdata(pdev);
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpriv->cfg->ops->hw_suspend(hw);
	rtl_deinit_rfkill(hw);

	return 0;
}
EXPORT_SYMBOL(rtl_pci_suspend);

int rtl_pci_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct ieee80211_hw *hw = pci_get_drvdata(pdev);
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpriv->cfg->ops->hw_resume(hw);
	rtl_init_rfkill(hw);
	return 0;
}
EXPORT_SYMBOL(rtl_pci_resume);
#endif /* CONFIG_PM_SLEEP */

struct rtl_intf_ops rtl_pci_ops = {
	.read_efuse_byte = read_efuse_byte,
	.adapter_start = rtl_pci_start,
	.adapter_stop = rtl_pci_stop,
	.check_buddy_priv = rtl_pci_check_buddy_priv,
	.adapter_tx = rtl_pci_tx,
	.flush = rtl_pci_flush,
	.reset_trx_ring = rtl_pci_reset_trx_ring,
	.waitq_insert = rtl_pci_tx_chk_waitq_insert,

	.disable_aspm = rtl_pci_disable_aspm,
	.enable_aspm = rtl_pci_enable_aspm,
};
