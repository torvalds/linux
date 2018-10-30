// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2009-2012  Realtek Corporation.
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
#include "cam.h"
#include "base.h"
#include "ps.h"
#include "pwrseqcmd.h"

#include "btcoexist/rtl_btc.h"
#include <linux/firmware.h>
#include <linux/export.h>
#include <net/cfg80211.h>

u8 channel5g[CHANNEL_MAX_NUMBER_5G] = {
	36, 38, 40, 42, 44, 46, 48,		/* Band 1 */
	52, 54, 56, 58, 60, 62, 64,		/* Band 2 */
	100, 102, 104, 106, 108, 110, 112,	/* Band 3 */
	116, 118, 120, 122, 124, 126, 128,	/* Band 3 */
	132, 134, 136, 138, 140, 142, 144,	/* Band 3 */
	149, 151, 153, 155, 157, 159, 161,	/* Band 4 */
	165, 167, 169, 171, 173, 175, 177	/* Band 4 */
};

u8 channel5g_80m[CHANNEL_MAX_NUMBER_5G_80M] = {
	42, 58, 106, 122, 138, 155, 171
};

static void rtl_fw_do_work(const struct firmware *firmware, void *context,
			   bool is_wow)
{
	struct ieee80211_hw *hw = context;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	int err;

	RT_TRACE(rtlpriv, COMP_ERR, DBG_LOUD,
		 "Firmware callback routine entered!\n");
	complete(&rtlpriv->firmware_loading_complete);
	if (!firmware) {
		if (rtlpriv->cfg->alt_fw_name) {
			err = request_firmware(&firmware,
					       rtlpriv->cfg->alt_fw_name,
					       rtlpriv->io.dev);
			pr_info("Loading alternative firmware %s\n",
				rtlpriv->cfg->alt_fw_name);
			if (!err)
				goto found_alt;
		}
		pr_err("Selected firmware is not available\n");
		rtlpriv->max_fw_size = 0;
		return;
	}
found_alt:
	if (firmware->size > rtlpriv->max_fw_size) {
		pr_err("Firmware is too big!\n");
		release_firmware(firmware);
		return;
	}
	if (!is_wow) {
		memcpy(rtlpriv->rtlhal.pfirmware, firmware->data,
		       firmware->size);
		rtlpriv->rtlhal.fwsize = firmware->size;
	} else {
		memcpy(rtlpriv->rtlhal.wowlan_firmware, firmware->data,
		       firmware->size);
		rtlpriv->rtlhal.wowlan_fwsize = firmware->size;
	}
	rtlpriv->rtlhal.fwsize = firmware->size;
	release_firmware(firmware);
}

void rtl_fw_cb(const struct firmware *firmware, void *context)
{
	rtl_fw_do_work(firmware, context, false);
}

void rtl_wowlan_fw_cb(const struct firmware *firmware, void *context)
{
	rtl_fw_do_work(firmware, context, true);
}

/*mutex for start & stop is must here. */
static int rtl_op_start(struct ieee80211_hw *hw)
{
	int err = 0;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));

	if (!is_hal_stop(rtlhal))
		return 0;
	if (!test_bit(RTL_STATUS_INTERFACE_START, &rtlpriv->status))
		return 0;
	mutex_lock(&rtlpriv->locks.conf_mutex);
	err = rtlpriv->intf_ops->adapter_start(hw);
	if (!err)
		rtl_watch_dog_timer_callback(&rtlpriv->works.watchdog_timer);
	mutex_unlock(&rtlpriv->locks.conf_mutex);
	return err;
}

static void rtl_op_stop(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	bool support_remote_wakeup = false;

	if (is_hal_stop(rtlhal))
		return;

	rtlpriv->cfg->ops->get_hw_reg(hw, HAL_DEF_WOWLAN,
				      (u8 *)(&support_remote_wakeup));
	/* here is must, because adhoc do stop and start,
	 * but stop with RFOFF may cause something wrong,
	 * like adhoc TP
	 */
	if (unlikely(ppsc->rfpwr_state == ERFOFF))
		rtl_ips_nic_on(hw);

	mutex_lock(&rtlpriv->locks.conf_mutex);
	/* if wowlan supported, DON'T clear connected info */
	if (!(support_remote_wakeup &&
	      rtlhal->enter_pnp_sleep)) {
		mac->link_state = MAC80211_NOLINK;
		eth_zero_addr(mac->bssid);
		mac->vendor = PEER_UNKNOWN;

		/* reset sec info */
		rtl_cam_reset_sec_info(hw);

		rtl_deinit_deferred_work(hw);
	}
	rtlpriv->intf_ops->adapter_stop(hw);

	mutex_unlock(&rtlpriv->locks.conf_mutex);
}

static void rtl_op_tx(struct ieee80211_hw *hw,
		      struct ieee80211_tx_control *control,
		      struct sk_buff *skb)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_tcb_desc tcb_desc;

	memset(&tcb_desc, 0, sizeof(struct rtl_tcb_desc));

	if (unlikely(is_hal_stop(rtlhal) || ppsc->rfpwr_state != ERFON))
		goto err_free;

	if (!test_bit(RTL_STATUS_INTERFACE_START, &rtlpriv->status))
		goto err_free;

	if (!rtlpriv->intf_ops->waitq_insert(hw, control->sta, skb))
		rtlpriv->intf_ops->adapter_tx(hw, control->sta, skb, &tcb_desc);
	return;

err_free:
	dev_kfree_skb_any(skb);
}

static int rtl_op_add_interface(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	int err = 0;
	u8 retry_limit = 0x30;

	if (mac->vif) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
			 "vif has been set!! mac->vif = 0x%p\n", mac->vif);
		return -EOPNOTSUPP;
	}

	vif->driver_flags |= IEEE80211_VIF_BEACON_FILTER;

	rtl_ips_nic_on(hw);

	mutex_lock(&rtlpriv->locks.conf_mutex);
	switch (ieee80211_vif_type_p2p(vif)) {
	case NL80211_IFTYPE_P2P_CLIENT:
		mac->p2p = P2P_ROLE_CLIENT;
		/*fall through*/
	case NL80211_IFTYPE_STATION:
		if (mac->beacon_enabled == 1) {
			RT_TRACE(rtlpriv, COMP_MAC80211, DBG_LOUD,
				 "NL80211_IFTYPE_STATION\n");
			mac->beacon_enabled = 0;
			rtlpriv->cfg->ops->update_interrupt_mask(hw, 0,
					rtlpriv->cfg->maps[RTL_IBSS_INT_MASKS]);
		}
		break;
	case NL80211_IFTYPE_ADHOC:
		RT_TRACE(rtlpriv, COMP_MAC80211, DBG_LOUD,
			 "NL80211_IFTYPE_ADHOC\n");

		mac->link_state = MAC80211_LINKED;
		rtlpriv->cfg->ops->set_bcn_reg(hw);
		if (rtlpriv->rtlhal.current_bandtype == BAND_ON_2_4G)
			mac->basic_rates = 0xfff;
		else
			mac->basic_rates = 0xff0;
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_BASIC_RATE,
				(u8 *)(&mac->basic_rates));

		retry_limit = 0x07;
		break;
	case NL80211_IFTYPE_P2P_GO:
		mac->p2p = P2P_ROLE_GO;
		/*fall through*/
	case NL80211_IFTYPE_AP:
		RT_TRACE(rtlpriv, COMP_MAC80211, DBG_LOUD,
			 "NL80211_IFTYPE_AP\n");

		mac->link_state = MAC80211_LINKED;
		rtlpriv->cfg->ops->set_bcn_reg(hw);
		if (rtlpriv->rtlhal.current_bandtype == BAND_ON_2_4G)
			mac->basic_rates = 0xfff;
		else
			mac->basic_rates = 0xff0;
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_BASIC_RATE,
					      (u8 *)(&mac->basic_rates));

		retry_limit = 0x07;
		break;
	case NL80211_IFTYPE_MESH_POINT:
		RT_TRACE(rtlpriv, COMP_MAC80211, DBG_LOUD,
			 "NL80211_IFTYPE_MESH_POINT\n");

		mac->link_state = MAC80211_LINKED;
		rtlpriv->cfg->ops->set_bcn_reg(hw);
		if (rtlpriv->rtlhal.current_bandtype == BAND_ON_2_4G)
			mac->basic_rates = 0xfff;
		else
			mac->basic_rates = 0xff0;
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_BASIC_RATE,
				(u8 *)(&mac->basic_rates));

		retry_limit = 0x07;
		break;
	default:
		pr_err("operation mode %d is not supported!\n",
		       vif->type);
		err = -EOPNOTSUPP;
		goto out;
	}

	if (mac->p2p) {
		RT_TRACE(rtlpriv, COMP_MAC80211, DBG_LOUD,
			 "p2p role %x\n", vif->type);
		mac->basic_rates = 0xff0;/*disable cck rate for p2p*/
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_BASIC_RATE,
				(u8 *)(&mac->basic_rates));
	}
	mac->vif = vif;
	mac->opmode = vif->type;
	rtlpriv->cfg->ops->set_network_type(hw, vif->type);
	memcpy(mac->mac_addr, vif->addr, ETH_ALEN);
	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_ETHER_ADDR, mac->mac_addr);

	mac->retry_long = retry_limit;
	mac->retry_short = retry_limit;
	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_RETRY_LIMIT,
			(u8 *)(&retry_limit));
out:
	mutex_unlock(&rtlpriv->locks.conf_mutex);
	return err;
}

static void rtl_op_remove_interface(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));

	mutex_lock(&rtlpriv->locks.conf_mutex);

	/* Free beacon resources */
	if (vif->type == NL80211_IFTYPE_AP ||
	    vif->type == NL80211_IFTYPE_ADHOC ||
	    vif->type == NL80211_IFTYPE_MESH_POINT) {
		if (mac->beacon_enabled == 1) {
			mac->beacon_enabled = 0;
			rtlpriv->cfg->ops->update_interrupt_mask(hw, 0,
					rtlpriv->cfg->maps[RTL_IBSS_INT_MASKS]);
		}
	}

	/*
	 *Note: We assume NL80211_IFTYPE_UNSPECIFIED as
	 *NO LINK for our hardware.
	 */
	mac->p2p = 0;
	mac->vif = NULL;
	mac->link_state = MAC80211_NOLINK;
	eth_zero_addr(mac->bssid);
	mac->vendor = PEER_UNKNOWN;
	mac->opmode = NL80211_IFTYPE_UNSPECIFIED;
	rtlpriv->cfg->ops->set_network_type(hw, mac->opmode);

	mutex_unlock(&rtlpriv->locks.conf_mutex);
}

static int rtl_op_change_interface(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   enum nl80211_iftype new_type, bool p2p)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	int ret;

	rtl_op_remove_interface(hw, vif);

	vif->type = new_type;
	vif->p2p = p2p;
	ret = rtl_op_add_interface(hw, vif);
	RT_TRACE(rtlpriv, COMP_MAC80211, DBG_LOUD,
		 "p2p  %x\n", p2p);
	return ret;
}

#ifdef CONFIG_PM
static u16 crc16_ccitt(u8 data, u16 crc)
{
	u8 shift_in, data_bit, crc_bit11, crc_bit4, crc_bit15;
	u8 i;
	u16 result;

	for (i = 0; i < 8; i++) {
		crc_bit15 = ((crc & BIT(15)) ? 1 : 0);
		data_bit  = (data & (BIT(0) << i) ? 1 : 0);
		shift_in = crc_bit15 ^ data_bit;

		result = crc << 1;
		if (shift_in == 0)
			result &= (~BIT(0));
		else
			result |= BIT(0);

		crc_bit11 = ((crc & BIT(11)) ? 1 : 0) ^ shift_in;
		if (crc_bit11 == 0)
			result &= (~BIT(12));
		else
			result |= BIT(12);

		crc_bit4 = ((crc & BIT(4)) ? 1 : 0) ^ shift_in;
		if (crc_bit4 == 0)
			result &= (~BIT(5));
		else
			result |= BIT(5);

		crc = result;
	}

	return crc;
}

static u16 _calculate_wol_pattern_crc(u8 *pattern, u16 len)
{
	u16 crc = 0xffff;
	u32 i;

	for (i = 0; i < len; i++)
		crc = crc16_ccitt(pattern[i], crc);

	crc = ~crc;

	return crc;
}

static void _rtl_add_wowlan_patterns(struct ieee80211_hw *hw,
				     struct cfg80211_wowlan *wow)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = &rtlpriv->mac80211;
	struct cfg80211_pkt_pattern *patterns = wow->patterns;
	struct rtl_wow_pattern rtl_pattern;
	const u8 *pattern_os, *mask_os;
	u8 mask[MAX_WOL_BIT_MASK_SIZE] = {0};
	u8 content[MAX_WOL_PATTERN_SIZE] = {0};
	u8 broadcast_addr[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	u8 multicast_addr1[2] = {0x33, 0x33};
	u8 multicast_addr2[3] = {0x01, 0x00, 0x5e};
	u8 i, mask_len;
	u16 j, len;

	for (i = 0; i < wow->n_patterns; i++) {
		memset(&rtl_pattern, 0, sizeof(struct rtl_wow_pattern));
		memset(mask, 0, MAX_WOL_BIT_MASK_SIZE);
		if (patterns[i].pattern_len < 0 ||
		    patterns[i].pattern_len > MAX_WOL_PATTERN_SIZE) {
			RT_TRACE(rtlpriv, COMP_POWER, DBG_WARNING,
				 "Pattern[%d] is too long\n", i);
			continue;
		}
		pattern_os = patterns[i].pattern;
		mask_len = DIV_ROUND_UP(patterns[i].pattern_len, 8);
		mask_os = patterns[i].mask;
		RT_PRINT_DATA(rtlpriv, COMP_POWER, DBG_TRACE,
			      "pattern content\n", pattern_os,
			       patterns[i].pattern_len);
		RT_PRINT_DATA(rtlpriv, COMP_POWER, DBG_TRACE,
			      "mask content\n", mask_os, mask_len);
		/* 1. unicast? multicast? or broadcast? */
		if (memcmp(pattern_os, broadcast_addr, 6) == 0)
			rtl_pattern.type = BROADCAST_PATTERN;
		else if (memcmp(pattern_os, multicast_addr1, 2) == 0 ||
			 memcmp(pattern_os, multicast_addr2, 3) == 0)
			rtl_pattern.type = MULTICAST_PATTERN;
		else if  (memcmp(pattern_os, mac->mac_addr, 6) == 0)
			rtl_pattern.type = UNICAST_PATTERN;
		else
			rtl_pattern.type = UNKNOWN_TYPE;

		/* 2. translate mask_from_os to mask_for_hw */

/******************************************************************************
 * pattern from OS uses 'ethenet frame', like this:

		   |    6   |    6   |   2  |     20    |  Variable  |	4  |
		   |--------+--------+------+-----------+------------+-----|
		   |    802.3 Mac Header    | IP Header | TCP Packet | FCS |
		   |   DA   |   SA   | Type |

 * BUT, packet catched by our HW is in '802.11 frame', begin from LLC,

	|     24 or 30      |    6   |   2  |     20    |  Variable  |  4  |
	|-------------------+--------+------+-----------+------------+-----|
	| 802.11 MAC Header |       LLC     | IP Header | TCP Packet | FCS |
			    | Others | Tpye |

 * Therefore, we need translate mask_from_OS to mask_to_hw.
 * We should left-shift mask by 6 bits, then set the new bit[0~5] = 0,
 * because new mask[0~5] means 'SA', but our HW packet begins from LLC,
 * bit[0~5] corresponds to first 6 Bytes in LLC, they just don't match.
 ******************************************************************************/

		/* Shift 6 bits */
		for (j = 0; j < mask_len - 1; j++) {
			mask[j] = mask_os[j] >> 6;
			mask[j] |= (mask_os[j + 1] & 0x3F) << 2;
		}
		mask[j] = (mask_os[j] >> 6) & 0x3F;
		/* Set bit 0-5 to zero */
		mask[0] &= 0xC0;

		RT_PRINT_DATA(rtlpriv, COMP_POWER, DBG_TRACE,
			      "mask to hw\n", mask, mask_len);
		for (j = 0; j < (MAX_WOL_BIT_MASK_SIZE + 1) / 4; j++) {
			rtl_pattern.mask[j] = mask[j * 4];
			rtl_pattern.mask[j] |= (mask[j * 4 + 1] << 8);
			rtl_pattern.mask[j] |= (mask[j * 4 + 2] << 16);
			rtl_pattern.mask[j] |= (mask[j * 4 + 3] << 24);
		}

		/* To get the wake up pattern from the mask.
		 * We do not count first 12 bits which means
		 * DA[6] and SA[6] in the pattern to match HW design.
		 */
		len = 0;
		for (j = 12; j < patterns[i].pattern_len; j++) {
			if ((mask_os[j / 8] >> (j % 8)) & 0x01) {
				content[len] = pattern_os[j];
				len++;
			}
		}

		RT_PRINT_DATA(rtlpriv, COMP_POWER, DBG_TRACE,
			      "pattern to hw\n", content, len);
		/* 3. calculate crc */
		rtl_pattern.crc = _calculate_wol_pattern_crc(content, len);
		RT_TRACE(rtlpriv, COMP_POWER, DBG_TRACE,
			 "CRC_Remainder = 0x%x\n", rtl_pattern.crc);

		/* 4. write crc & mask_for_hw to hw */
		rtlpriv->cfg->ops->add_wowlan_pattern(hw, &rtl_pattern, i);
	}
	rtl_write_byte(rtlpriv, 0x698, wow->n_patterns);
}

static int rtl_op_suspend(struct ieee80211_hw *hw,
			  struct cfg80211_wowlan *wow)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));

	RT_TRACE(rtlpriv, COMP_POWER, DBG_DMESG, "\n");
	if (WARN_ON(!wow))
		return -EINVAL;

	/* to resolve s4 can not wake up*/
	rtlhal->last_suspend_sec = ktime_get_real_seconds();

	if ((ppsc->wo_wlan_mode & WAKE_ON_PATTERN_MATCH) && wow->n_patterns)
		_rtl_add_wowlan_patterns(hw, wow);

	rtlhal->driver_is_goingto_unload = true;
	rtlhal->enter_pnp_sleep = true;

	rtl_lps_leave(hw);
	rtl_op_stop(hw);
	device_set_wakeup_enable(wiphy_dev(hw->wiphy), true);
	return 0;
}

static int rtl_op_resume(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));

	RT_TRACE(rtlpriv, COMP_POWER, DBG_DMESG, "\n");
	rtlhal->driver_is_goingto_unload = false;
	rtlhal->enter_pnp_sleep = false;
	rtlhal->wake_from_pnp_sleep = true;

	/* to resovle s4 can not wake up*/
	if (ktime_get_real_seconds() - rtlhal->last_suspend_sec < 5)
		return -1;

	rtl_op_start(hw);
	device_set_wakeup_enable(wiphy_dev(hw->wiphy), false);
	ieee80211_resume_disconnect(mac->vif);
	rtlhal->wake_from_pnp_sleep = false;
	return 0;
}
#endif

static int rtl_op_config(struct ieee80211_hw *hw, u32 changed)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct ieee80211_conf *conf = &hw->conf;

	if (mac->skip_scan)
		return 1;

	mutex_lock(&rtlpriv->locks.conf_mutex);
	if (changed & IEEE80211_CONF_CHANGE_LISTEN_INTERVAL) {	/* BIT(2)*/
		RT_TRACE(rtlpriv, COMP_MAC80211, DBG_LOUD,
			 "IEEE80211_CONF_CHANGE_LISTEN_INTERVAL\n");
	}

	/*For IPS */
	if (changed & IEEE80211_CONF_CHANGE_IDLE) {
		if (hw->conf.flags & IEEE80211_CONF_IDLE)
			rtl_ips_nic_off(hw);
		else
			rtl_ips_nic_on(hw);
	} else {
		/*
		 *although rfoff may not cause by ips, but we will
		 *check the reason in set_rf_power_state function
		 */
		if (unlikely(ppsc->rfpwr_state == ERFOFF))
			rtl_ips_nic_on(hw);
	}

	/*For LPS */
	if ((changed & IEEE80211_CONF_CHANGE_PS) &&
	    rtlpriv->psc.swctrl_lps && !rtlpriv->psc.fwctrl_lps) {
		cancel_delayed_work(&rtlpriv->works.ps_work);
		cancel_delayed_work(&rtlpriv->works.ps_rfon_wq);
		if (conf->flags & IEEE80211_CONF_PS) {
			rtlpriv->psc.sw_ps_enabled = true;
			/* sleep here is must, or we may recv the beacon and
			 * cause mac80211 into wrong ps state, this will cause
			 * power save nullfunc send fail, and further cause
			 * pkt loss, So sleep must quickly but not immediately
			 * because that will cause nullfunc send by mac80211
			 * fail, and cause pkt loss, we have tested that 5mA
			 * works very well
			 */
			if (!rtlpriv->psc.multi_buffered)
				queue_delayed_work(rtlpriv->works.rtl_wq,
						   &rtlpriv->works.ps_work,
						   MSECS(5));
		} else {
			rtl_swlps_rf_awake(hw);
			rtlpriv->psc.sw_ps_enabled = false;
		}
	}

	if (changed & IEEE80211_CONF_CHANGE_RETRY_LIMITS) {
		RT_TRACE(rtlpriv, COMP_MAC80211, DBG_LOUD,
			 "IEEE80211_CONF_CHANGE_RETRY_LIMITS %x\n",
			 hw->conf.long_frame_max_tx_count);
		/* brought up everything changes (changed == ~0) indicates first
		 * open, so use our default value instead of that of wiphy.
		 */
		if (changed != ~0) {
			mac->retry_long = hw->conf.long_frame_max_tx_count;
			mac->retry_short = hw->conf.long_frame_max_tx_count;
			rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_RETRY_LIMIT,
				(u8 *)(&hw->conf.long_frame_max_tx_count));
		}
	}

	if (changed & IEEE80211_CONF_CHANGE_CHANNEL &&
	    !rtlpriv->proximity.proxim_on) {
		struct ieee80211_channel *channel = hw->conf.chandef.chan;
		enum nl80211_chan_width width = hw->conf.chandef.width;
		enum nl80211_channel_type channel_type = NL80211_CHAN_NO_HT;
		u8 wide_chan = (u8)channel->hw_value;

		/* channel_type is for 20&40M */
		if (width < NL80211_CHAN_WIDTH_80)
			channel_type =
				cfg80211_get_chandef_type(&hw->conf.chandef);
		if (mac->act_scanning)
			mac->n_channels++;

		if (rtlpriv->dm.supp_phymode_switch &&
		    mac->link_state < MAC80211_LINKED &&
		    !mac->act_scanning) {
			if (rtlpriv->cfg->ops->chk_switch_dmdp)
				rtlpriv->cfg->ops->chk_switch_dmdp(hw);
		}

		/*
		 *because we should back channel to
		 *current_network.chan in in scanning,
		 *So if set_chan == current_network.chan
		 *we should set it.
		 *because mac80211 tell us wrong bw40
		 *info for cisco1253 bw20, so we modify
		 *it here based on UPPER & LOWER
		 */

		if (width >= NL80211_CHAN_WIDTH_80) {
			if (width == NL80211_CHAN_WIDTH_80) {
				u32 center = hw->conf.chandef.center_freq1;
				u32 primary =
				(u32)hw->conf.chandef.chan->center_freq;

				rtlphy->current_chan_bw =
					HT_CHANNEL_WIDTH_80;
				mac->bw_80 = true;
				mac->bw_40 = true;
				if (center > primary) {
					mac->cur_80_prime_sc =
					PRIME_CHNL_OFFSET_LOWER;
					if (center - primary == 10) {
						mac->cur_40_prime_sc =
						PRIME_CHNL_OFFSET_UPPER;

						wide_chan += 2;
					} else if (center - primary == 30) {
						mac->cur_40_prime_sc =
						PRIME_CHNL_OFFSET_LOWER;

						wide_chan += 6;
					}
				} else {
					mac->cur_80_prime_sc =
					PRIME_CHNL_OFFSET_UPPER;
					if (primary - center == 10) {
						mac->cur_40_prime_sc =
						PRIME_CHNL_OFFSET_LOWER;

						wide_chan -= 2;
					} else if (primary - center == 30) {
						mac->cur_40_prime_sc =
						PRIME_CHNL_OFFSET_UPPER;

						wide_chan -= 6;
					}
				}
			}
		} else {
			switch (channel_type) {
			case NL80211_CHAN_HT20:
			case NL80211_CHAN_NO_HT:
					/* SC */
					mac->cur_40_prime_sc =
						PRIME_CHNL_OFFSET_DONT_CARE;
					rtlphy->current_chan_bw =
						HT_CHANNEL_WIDTH_20;
					mac->bw_40 = false;
					mac->bw_80 = false;
					break;
			case NL80211_CHAN_HT40MINUS:
					/* SC */
					mac->cur_40_prime_sc =
						PRIME_CHNL_OFFSET_UPPER;
					rtlphy->current_chan_bw =
						HT_CHANNEL_WIDTH_20_40;
					mac->bw_40 = true;
					mac->bw_80 = false;

					/*wide channel */
					wide_chan -= 2;

					break;
			case NL80211_CHAN_HT40PLUS:
					/* SC */
					mac->cur_40_prime_sc =
						PRIME_CHNL_OFFSET_LOWER;
					rtlphy->current_chan_bw =
						HT_CHANNEL_WIDTH_20_40;
					mac->bw_40 = true;
					mac->bw_80 = false;

					/*wide channel */
					wide_chan += 2;

					break;
			default:
					mac->bw_40 = false;
					mac->bw_80 = false;
					pr_err("switch case %#x not processed\n",
					       channel_type);
					break;
			}
		}

		if (wide_chan <= 0)
			wide_chan = 1;

		/* In scanning, when before we offchannel we may send a ps=1
		 * null to AP, and then we may send a ps = 0 null to AP quickly,
		 * but first null may have caused AP to put lots of packet to
		 * hw tx buffer. These packets must be tx'd before we go off
		 * channel so we must delay more time to let AP flush these
		 * packets before going offchannel, or dis-association or
		 * delete BA will be caused by AP
		 */
		if (rtlpriv->mac80211.offchan_delay) {
			rtlpriv->mac80211.offchan_delay = false;
			mdelay(50);
		}

		rtlphy->current_channel = wide_chan;

		rtlpriv->cfg->ops->switch_channel(hw);
		rtlpriv->cfg->ops->set_channel_access(hw);
		rtlpriv->cfg->ops->set_bw_mode(hw, channel_type);
	}

	mutex_unlock(&rtlpriv->locks.conf_mutex);

	return 0;
}

static void rtl_op_configure_filter(struct ieee80211_hw *hw,
				    unsigned int changed_flags,
				    unsigned int *new_flags, u64 multicast)
{
	bool update_rcr = false;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));

	*new_flags &= RTL_SUPPORTED_FILTERS;
	if (changed_flags == 0)
		return;

	/*TODO: we disable broadcase now, so enable here */
	if (changed_flags & FIF_ALLMULTI) {
		if (*new_flags & FIF_ALLMULTI) {
			mac->rx_conf |= rtlpriv->cfg->maps[MAC_RCR_AM] |
			    rtlpriv->cfg->maps[MAC_RCR_AB];
			RT_TRACE(rtlpriv, COMP_MAC80211, DBG_LOUD,
				 "Enable receive multicast frame\n");
		} else {
			mac->rx_conf &= ~(rtlpriv->cfg->maps[MAC_RCR_AM] |
					  rtlpriv->cfg->maps[MAC_RCR_AB]);
			RT_TRACE(rtlpriv, COMP_MAC80211, DBG_LOUD,
				 "Disable receive multicast frame\n");
		}
		update_rcr = true;
	}

	if (changed_flags & FIF_FCSFAIL) {
		if (*new_flags & FIF_FCSFAIL) {
			mac->rx_conf |= rtlpriv->cfg->maps[MAC_RCR_ACRC32];
			RT_TRACE(rtlpriv, COMP_MAC80211, DBG_LOUD,
				 "Enable receive FCS error frame\n");
		} else {
			mac->rx_conf &= ~rtlpriv->cfg->maps[MAC_RCR_ACRC32];
			RT_TRACE(rtlpriv, COMP_MAC80211, DBG_LOUD,
				 "Disable receive FCS error frame\n");
		}
		if (!update_rcr)
			update_rcr = true;
	}

	/* if ssid not set to hw don't check bssid
	 * here just used for linked scanning, & linked
	 * and nolink check bssid is set in set network_type
	 */
	if (changed_flags & FIF_BCN_PRBRESP_PROMISC &&
	    mac->link_state >= MAC80211_LINKED) {
		if (mac->opmode != NL80211_IFTYPE_AP &&
		    mac->opmode != NL80211_IFTYPE_MESH_POINT) {
			if (*new_flags & FIF_BCN_PRBRESP_PROMISC)
				rtlpriv->cfg->ops->set_chk_bssid(hw, false);
			else
				rtlpriv->cfg->ops->set_chk_bssid(hw, true);
			if (update_rcr)
				update_rcr = false;
		}
	}

	if (changed_flags & FIF_CONTROL) {
		if (*new_flags & FIF_CONTROL) {
			mac->rx_conf |= rtlpriv->cfg->maps[MAC_RCR_ACF];

			RT_TRACE(rtlpriv, COMP_MAC80211, DBG_LOUD,
				 "Enable receive control frame.\n");
		} else {
			mac->rx_conf &= ~rtlpriv->cfg->maps[MAC_RCR_ACF];
			RT_TRACE(rtlpriv, COMP_MAC80211, DBG_LOUD,
				 "Disable receive control frame.\n");
		}
		if (!update_rcr)
			update_rcr = true;
	}

	if (changed_flags & FIF_OTHER_BSS) {
		if (*new_flags & FIF_OTHER_BSS) {
			mac->rx_conf |= rtlpriv->cfg->maps[MAC_RCR_AAP];
			RT_TRACE(rtlpriv, COMP_MAC80211, DBG_LOUD,
				 "Enable receive other BSS's frame.\n");
		} else {
			mac->rx_conf &= ~rtlpriv->cfg->maps[MAC_RCR_AAP];
			RT_TRACE(rtlpriv, COMP_MAC80211, DBG_LOUD,
				 "Disable receive other BSS's frame.\n");
		}
		if (!update_rcr)
			update_rcr = true;
	}

	if (update_rcr)
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_RCR,
					      (u8 *)(&mac->rx_conf));
}

static int rtl_op_sta_add(struct ieee80211_hw *hw,
			  struct ieee80211_vif *vif,
			  struct ieee80211_sta *sta)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_sta_info *sta_entry;

	if (sta) {
		sta_entry = (struct rtl_sta_info *)sta->drv_priv;
		spin_lock_bh(&rtlpriv->locks.entry_list_lock);
		list_add_tail(&sta_entry->list, &rtlpriv->entry_list);
		spin_unlock_bh(&rtlpriv->locks.entry_list_lock);
		if (rtlhal->current_bandtype == BAND_ON_2_4G) {
			sta_entry->wireless_mode = WIRELESS_MODE_G;
			if (sta->supp_rates[0] <= 0xf)
				sta_entry->wireless_mode = WIRELESS_MODE_B;
			if (sta->ht_cap.ht_supported)
				sta_entry->wireless_mode = WIRELESS_MODE_N_24G;

			if (vif->type == NL80211_IFTYPE_ADHOC)
				sta_entry->wireless_mode = WIRELESS_MODE_G;
		} else if (rtlhal->current_bandtype == BAND_ON_5G) {
			sta_entry->wireless_mode = WIRELESS_MODE_A;
			if (sta->ht_cap.ht_supported)
				sta_entry->wireless_mode = WIRELESS_MODE_N_5G;
			if (sta->vht_cap.vht_supported)
				sta_entry->wireless_mode = WIRELESS_MODE_AC_5G;

			if (vif->type == NL80211_IFTYPE_ADHOC)
				sta_entry->wireless_mode = WIRELESS_MODE_A;
		}
		/*disable cck rate for p2p*/
		if (mac->p2p)
			sta->supp_rates[0] &= 0xfffffff0;

		if (sta->ht_cap.ht_supported) {
			if (sta->ht_cap.cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40)
				rtlphy->max_ht_chan_bw = HT_CHANNEL_WIDTH_20_40;
			else
				rtlphy->max_ht_chan_bw = HT_CHANNEL_WIDTH_20;
		}

		if (sta->vht_cap.vht_supported)
			rtlphy->max_vht_chan_bw = HT_CHANNEL_WIDTH_80;

		memcpy(sta_entry->mac_addr, sta->addr, ETH_ALEN);
		RT_TRACE(rtlpriv, COMP_MAC80211, DBG_DMESG,
			 "Add sta addr is %pM\n", sta->addr);
		rtlpriv->cfg->ops->update_rate_tbl(hw, sta, 0, true);

		if (rtlpriv->phydm.ops)
			rtlpriv->phydm.ops->phydm_add_sta(rtlpriv, sta);
	}

	return 0;
}

static int rtl_op_sta_remove(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     struct ieee80211_sta *sta)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_sta_info *sta_entry;

	if (sta) {
		RT_TRACE(rtlpriv, COMP_MAC80211, DBG_DMESG,
			 "Remove sta addr is %pM\n", sta->addr);

		if (rtlpriv->phydm.ops)
			rtlpriv->phydm.ops->phydm_del_sta(rtlpriv, sta);

		sta_entry = (struct rtl_sta_info *)sta->drv_priv;
		sta_entry->wireless_mode = 0;
		sta_entry->ratr_index = 0;
		spin_lock_bh(&rtlpriv->locks.entry_list_lock);
		list_del(&sta_entry->list);
		spin_unlock_bh(&rtlpriv->locks.entry_list_lock);
	}
	return 0;
}

static int _rtl_get_hal_qnum(u16 queue)
{
	int qnum;

	switch (queue) {
	case 0:
		qnum = AC3_VO;
		break;
	case 1:
		qnum = AC2_VI;
		break;
	case 2:
		qnum = AC0_BE;
		break;
	case 3:
		qnum = AC1_BK;
		break;
	default:
		qnum = AC0_BE;
		break;
	}
	return qnum;
}

static void rtl_op_sta_statistics(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  struct ieee80211_sta *sta,
				  struct station_info *sinfo)
{
	/* nothing filled by driver, so mac80211 will update all info */
	sinfo->filled = 0;
}

static int rtl_op_set_frag_threshold(struct ieee80211_hw *hw, u32 value)
{
	return -EOPNOTSUPP;
}

/*
 *for mac80211 VO = 0, VI = 1, BE = 2, BK = 3
 *for rtl819x  BE = 0, BK = 1, VI = 2, VO = 3
 */
static int rtl_op_conf_tx(struct ieee80211_hw *hw,
			  struct ieee80211_vif *vif, u16 queue,
			  const struct ieee80211_tx_queue_params *param)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	int aci;

	if (queue >= AC_MAX) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
			 "queue number %d is incorrect!\n", queue);
		return -EINVAL;
	}

	aci = _rtl_get_hal_qnum(queue);
	mac->ac[aci].aifs = param->aifs;
	mac->ac[aci].cw_min = cpu_to_le16(param->cw_min);
	mac->ac[aci].cw_max = cpu_to_le16(param->cw_max);
	mac->ac[aci].tx_op = cpu_to_le16(param->txop);
	memcpy(&mac->edca_param[aci], param, sizeof(*param));
	rtlpriv->cfg->ops->set_qos(hw, aci);
	return 0;
}

static void send_beacon_frame(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct sk_buff *skb = ieee80211_beacon_get(hw, vif);
	struct rtl_tcb_desc tcb_desc;

	if (skb) {
		memset(&tcb_desc, 0, sizeof(struct rtl_tcb_desc));
		rtlpriv->intf_ops->adapter_tx(hw, NULL, skb, &tcb_desc);
	}
}

static void rtl_op_bss_info_changed(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    struct ieee80211_bss_conf *bss_conf,
				    u32 changed)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));

	mutex_lock(&rtlpriv->locks.conf_mutex);
	if (vif->type == NL80211_IFTYPE_ADHOC ||
	    vif->type == NL80211_IFTYPE_AP ||
	    vif->type == NL80211_IFTYPE_MESH_POINT) {
		if (changed & BSS_CHANGED_BEACON ||
		    (changed & BSS_CHANGED_BEACON_ENABLED &&
		     bss_conf->enable_beacon)) {
			if (mac->beacon_enabled == 0) {
				RT_TRACE(rtlpriv, COMP_MAC80211, DBG_DMESG,
					 "BSS_CHANGED_BEACON_ENABLED\n");

				/*start hw beacon interrupt. */
				/*rtlpriv->cfg->ops->set_bcn_reg(hw); */
				mac->beacon_enabled = 1;
				rtlpriv->cfg->ops->update_interrupt_mask(hw,
						rtlpriv->cfg->maps
						[RTL_IBSS_INT_MASKS], 0);

				if (rtlpriv->cfg->ops->linked_set_reg)
					rtlpriv->cfg->ops->linked_set_reg(hw);
				send_beacon_frame(hw, vif);
			}
		}
		if ((changed & BSS_CHANGED_BEACON_ENABLED &&
		     !bss_conf->enable_beacon)) {
			if (mac->beacon_enabled == 1) {
				RT_TRACE(rtlpriv, COMP_MAC80211, DBG_DMESG,
					 "ADHOC DISABLE BEACON\n");

				mac->beacon_enabled = 0;
				rtlpriv->cfg->ops->update_interrupt_mask(hw, 0,
						rtlpriv->cfg->maps
						[RTL_IBSS_INT_MASKS]);
			}
		}
		if (changed & BSS_CHANGED_BEACON_INT) {
			RT_TRACE(rtlpriv, COMP_BEACON, DBG_TRACE,
				 "BSS_CHANGED_BEACON_INT\n");
			mac->beacon_interval = bss_conf->beacon_int;
			rtlpriv->cfg->ops->set_bcn_intv(hw);
		}
	}

	/*TODO: reference to enum ieee80211_bss_change */
	if (changed & BSS_CHANGED_ASSOC) {
		u8 mstatus;

		if (bss_conf->assoc) {
			struct ieee80211_sta *sta = NULL;
			u8 keep_alive = 10;

			mstatus = RT_MEDIA_CONNECT;
			/* we should reset all sec info & cam
			 * before set cam after linked, we should not
			 * reset in disassoc, that will cause tkip->wep
			 * fail because some flag will be wrong
			 * reset sec info
			 */
			rtl_cam_reset_sec_info(hw);
			/* reset cam to fix wep fail issue
			 * when change from wpa to wep
			 */
			rtl_cam_reset_all_entry(hw);

			mac->link_state = MAC80211_LINKED;
			mac->cnt_after_linked = 0;
			mac->assoc_id = bss_conf->aid;
			memcpy(mac->bssid, bss_conf->bssid, ETH_ALEN);

			if (rtlpriv->cfg->ops->linked_set_reg)
				rtlpriv->cfg->ops->linked_set_reg(hw);

			rcu_read_lock();
			sta = ieee80211_find_sta(vif, (u8 *)bss_conf->bssid);
			if (!sta) {
				rcu_read_unlock();
				goto out;
			}
			RT_TRACE(rtlpriv, COMP_EASY_CONCURRENT, DBG_LOUD,
				 "send PS STATIC frame\n");
			if (rtlpriv->dm.supp_phymode_switch) {
				if (sta->ht_cap.ht_supported)
					rtl_send_smps_action(hw, sta,
							     IEEE80211_SMPS_STATIC);
			}

			if (rtlhal->current_bandtype == BAND_ON_5G) {
				mac->mode = WIRELESS_MODE_A;
			} else {
				if (sta->supp_rates[0] <= 0xf)
					mac->mode = WIRELESS_MODE_B;
				else
					mac->mode = WIRELESS_MODE_G;
			}

			if (sta->ht_cap.ht_supported) {
				if (rtlhal->current_bandtype == BAND_ON_2_4G)
					mac->mode = WIRELESS_MODE_N_24G;
				else
					mac->mode = WIRELESS_MODE_N_5G;
			}

			if (sta->vht_cap.vht_supported) {
				if (rtlhal->current_bandtype == BAND_ON_5G)
					mac->mode = WIRELESS_MODE_AC_5G;
				else
					mac->mode = WIRELESS_MODE_AC_24G;
			}

			if (vif->type == NL80211_IFTYPE_STATION)
				rtlpriv->cfg->ops->update_rate_tbl(hw, sta, 0,
								   true);
			rcu_read_unlock();

			/* to avoid AP Disassociation caused by inactivity */
			rtlpriv->cfg->ops->set_hw_reg(hw,
						      HW_VAR_KEEP_ALIVE,
						      (u8 *)(&keep_alive));

			RT_TRACE(rtlpriv, COMP_MAC80211, DBG_DMESG,
				 "BSS_CHANGED_ASSOC\n");
		} else {
			struct cfg80211_bss *bss = NULL;

			mstatus = RT_MEDIA_DISCONNECT;

			if (mac->link_state == MAC80211_LINKED)
				rtl_lps_leave(hw);
			if (ppsc->p2p_ps_info.p2p_ps_mode > P2P_PS_NONE)
				rtl_p2p_ps_cmd(hw, P2P_PS_DISABLE);
			mac->link_state = MAC80211_NOLINK;

			bss = cfg80211_get_bss(hw->wiphy, NULL,
					       (u8 *)mac->bssid, NULL, 0,
					       IEEE80211_BSS_TYPE_ESS,
					       IEEE80211_PRIVACY_OFF);

			RT_TRACE(rtlpriv, COMP_MAC80211, DBG_DMESG,
				 "bssid = %x-%x-%x-%x-%x-%x\n",
				 mac->bssid[0], mac->bssid[1],
				 mac->bssid[2], mac->bssid[3],
				 mac->bssid[4], mac->bssid[5]);

			if (bss) {
				cfg80211_unlink_bss(hw->wiphy, bss);
				cfg80211_put_bss(hw->wiphy, bss);
				RT_TRACE(rtlpriv, COMP_MAC80211, DBG_DMESG,
					 "cfg80211_unlink !!\n");
			}

			eth_zero_addr(mac->bssid);
			mac->vendor = PEER_UNKNOWN;
			mac->mode = 0;

			if (rtlpriv->dm.supp_phymode_switch) {
				if (rtlpriv->cfg->ops->chk_switch_dmdp)
					rtlpriv->cfg->ops->chk_switch_dmdp(hw);
			}
			RT_TRACE(rtlpriv, COMP_MAC80211, DBG_DMESG,
				 "BSS_CHANGED_UN_ASSOC\n");
		}
		rtlpriv->cfg->ops->set_network_type(hw, vif->type);
		/* For FW LPS:
		 * To tell firmware we have connected or disconnected
		 */
		rtlpriv->cfg->ops->set_hw_reg(hw,
					      HW_VAR_H2C_FW_JOINBSSRPT,
					      (u8 *)(&mstatus));
		ppsc->report_linked = (mstatus == RT_MEDIA_CONNECT) ?
				      true : false;

		if (rtlpriv->cfg->ops->get_btc_status())
			rtlpriv->btcoexist.btc_ops->btc_mediastatus_notify(
							rtlpriv, mstatus);
	}

	if (changed & BSS_CHANGED_ERP_CTS_PROT) {
		RT_TRACE(rtlpriv, COMP_MAC80211, DBG_TRACE,
			 "BSS_CHANGED_ERP_CTS_PROT\n");
		mac->use_cts_protect = bss_conf->use_cts_prot;
	}

	if (changed & BSS_CHANGED_ERP_PREAMBLE) {
		RT_TRACE(rtlpriv, COMP_MAC80211, DBG_LOUD,
			 "BSS_CHANGED_ERP_PREAMBLE use short preamble:%x\n",
			  bss_conf->use_short_preamble);

		mac->short_preamble = bss_conf->use_short_preamble;
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_ACK_PREAMBLE,
					      (u8 *)(&mac->short_preamble));
	}

	if (changed & BSS_CHANGED_ERP_SLOT) {
		RT_TRACE(rtlpriv, COMP_MAC80211, DBG_TRACE,
			 "BSS_CHANGED_ERP_SLOT\n");

		if (bss_conf->use_short_slot)
			mac->slot_time = RTL_SLOT_TIME_9;
		else
			mac->slot_time = RTL_SLOT_TIME_20;

		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_SLOT_TIME,
					      (u8 *)(&mac->slot_time));
	}

	if (changed & BSS_CHANGED_HT) {
		struct ieee80211_sta *sta = NULL;

		RT_TRACE(rtlpriv, COMP_MAC80211, DBG_TRACE,
			 "BSS_CHANGED_HT\n");

		rcu_read_lock();
		sta = ieee80211_find_sta(vif, (u8 *)bss_conf->bssid);
		if (sta) {
			if (sta->ht_cap.ampdu_density >
			    mac->current_ampdu_density)
				mac->current_ampdu_density =
				    sta->ht_cap.ampdu_density;
			if (sta->ht_cap.ampdu_factor <
			    mac->current_ampdu_factor)
				mac->current_ampdu_factor =
				    sta->ht_cap.ampdu_factor;

			if (sta->ht_cap.ht_supported) {
				if (sta->ht_cap.cap &
				    IEEE80211_HT_CAP_SUP_WIDTH_20_40)
					rtlphy->max_ht_chan_bw =
							HT_CHANNEL_WIDTH_20_40;
				else
					rtlphy->max_ht_chan_bw =
							HT_CHANNEL_WIDTH_20;
			}
		}
		rcu_read_unlock();

		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_SHORTGI_DENSITY,
					      (u8 *)(&mac->max_mss_density));
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_AMPDU_FACTOR,
					      &mac->current_ampdu_factor);
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_AMPDU_MIN_SPACE,
					      &mac->current_ampdu_density);
	}

	if (changed & BSS_CHANGED_BANDWIDTH) {
		struct ieee80211_sta *sta = NULL;

		RT_TRACE(rtlpriv, COMP_MAC80211, DBG_TRACE,
			 "BSS_CHANGED_BANDWIDTH\n");

		rcu_read_lock();
		sta = ieee80211_find_sta(vif, (u8 *)bss_conf->bssid);

		if (sta) {
			if (sta->ht_cap.ht_supported) {
				if (sta->ht_cap.cap &
				    IEEE80211_HT_CAP_SUP_WIDTH_20_40)
					rtlphy->max_ht_chan_bw =
							HT_CHANNEL_WIDTH_20_40;
				else
					rtlphy->max_ht_chan_bw =
							HT_CHANNEL_WIDTH_20;
			}

			if (sta->vht_cap.vht_supported)
				rtlphy->max_vht_chan_bw = HT_CHANNEL_WIDTH_80;
		}
		rcu_read_unlock();
	}

	if (changed & BSS_CHANGED_BSSID) {
		u32 basic_rates;
		struct ieee80211_sta *sta = NULL;

		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_BSSID,
					      (u8 *)bss_conf->bssid);

		RT_TRACE(rtlpriv, COMP_MAC80211, DBG_DMESG,
			 "bssid: %pM\n", bss_conf->bssid);

		mac->vendor = PEER_UNKNOWN;
		memcpy(mac->bssid, bss_conf->bssid, ETH_ALEN);

		rcu_read_lock();
		sta = ieee80211_find_sta(vif, (u8 *)bss_conf->bssid);
		if (!sta) {
			rcu_read_unlock();
			goto out;
		}

		if (rtlhal->current_bandtype == BAND_ON_5G) {
			mac->mode = WIRELESS_MODE_A;
		} else {
			if (sta->supp_rates[0] <= 0xf)
				mac->mode = WIRELESS_MODE_B;
			else
				mac->mode = WIRELESS_MODE_G;
		}

		if (sta->ht_cap.ht_supported) {
			if (rtlhal->current_bandtype == BAND_ON_2_4G)
				mac->mode = WIRELESS_MODE_N_24G;
			else
				mac->mode = WIRELESS_MODE_N_5G;
		}

		if (sta->vht_cap.vht_supported) {
			if (rtlhal->current_bandtype == BAND_ON_5G)
				mac->mode = WIRELESS_MODE_AC_5G;
			else
				mac->mode = WIRELESS_MODE_AC_24G;
		}

		/* just station need it, because ibss & ap mode will
		 * set in sta_add, and will be NULL here
		 */
		if (vif->type == NL80211_IFTYPE_STATION) {
			struct rtl_sta_info *sta_entry;

			sta_entry = (struct rtl_sta_info *)sta->drv_priv;
			sta_entry->wireless_mode = mac->mode;
		}

		if (sta->ht_cap.ht_supported) {
			mac->ht_enable = true;

			/* for cisco 1252 bw20 it's wrong
			 * if (ht_cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40) {
			 *	mac->bw_40 = true;
			 * }
			 */
		}

		if (sta->vht_cap.vht_supported)
			mac->vht_enable = true;

		if (changed & BSS_CHANGED_BASIC_RATES) {
			/* for 5G must << RATE_6M_INDEX = 4,
			 * because 5G have no cck rate
			 */
			if (rtlhal->current_bandtype == BAND_ON_5G)
				basic_rates = sta->supp_rates[1] << 4;
			else
				basic_rates = sta->supp_rates[0];

			mac->basic_rates = basic_rates;
			rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_BASIC_RATE,
					(u8 *)(&basic_rates));
		}
		rcu_read_unlock();
	}
out:
	mutex_unlock(&rtlpriv->locks.conf_mutex);
}

static u64 rtl_op_get_tsf(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u64 tsf;

	rtlpriv->cfg->ops->get_hw_reg(hw, HW_VAR_CORRECT_TSF, (u8 *)(&tsf));
	return tsf;
}

static void rtl_op_set_tsf(struct ieee80211_hw *hw,
			   struct ieee80211_vif *vif, u64 tsf)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	u8 bibss = (mac->opmode == NL80211_IFTYPE_ADHOC) ? 1 : 0;

	mac->tsf = tsf;
	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_CORRECT_TSF, (u8 *)(&bibss));
}

static void rtl_op_reset_tsf(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 tmp = 0;

	rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_DUAL_TSF_RST, (u8 *)(&tmp));
}

static void rtl_op_sta_notify(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      enum sta_notify_cmd cmd,
			      struct ieee80211_sta *sta)
{
	switch (cmd) {
	case STA_NOTIFY_SLEEP:
		break;
	case STA_NOTIFY_AWAKE:
		break;
	default:
		break;
	}
}

static int rtl_op_ampdu_action(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif,
			       struct ieee80211_ampdu_params *params)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct ieee80211_sta *sta = params->sta;
	enum ieee80211_ampdu_mlme_action action = params->action;
	u16 tid = params->tid;
	u16 *ssn = &params->ssn;

	switch (action) {
	case IEEE80211_AMPDU_TX_START:
		RT_TRACE(rtlpriv, COMP_MAC80211, DBG_TRACE,
			 "IEEE80211_AMPDU_TX_START: TID:%d\n", tid);
		return rtl_tx_agg_start(hw, vif, sta, tid, ssn);
	case IEEE80211_AMPDU_TX_STOP_CONT:
	case IEEE80211_AMPDU_TX_STOP_FLUSH:
	case IEEE80211_AMPDU_TX_STOP_FLUSH_CONT:
		RT_TRACE(rtlpriv, COMP_MAC80211, DBG_TRACE,
			 "IEEE80211_AMPDU_TX_STOP: TID:%d\n", tid);
		return rtl_tx_agg_stop(hw, vif, sta, tid);
	case IEEE80211_AMPDU_TX_OPERATIONAL:
		RT_TRACE(rtlpriv, COMP_MAC80211, DBG_TRACE,
			 "IEEE80211_AMPDU_TX_OPERATIONAL:TID:%d\n", tid);
		rtl_tx_agg_oper(hw, sta, tid);
		break;
	case IEEE80211_AMPDU_RX_START:
		RT_TRACE(rtlpriv, COMP_MAC80211, DBG_TRACE,
			 "IEEE80211_AMPDU_RX_START:TID:%d\n", tid);
		return rtl_rx_agg_start(hw, sta, tid);
	case IEEE80211_AMPDU_RX_STOP:
		RT_TRACE(rtlpriv, COMP_MAC80211, DBG_TRACE,
			 "IEEE80211_AMPDU_RX_STOP:TID:%d\n", tid);
		return rtl_rx_agg_stop(hw, sta, tid);
	default:
		pr_err("IEEE80211_AMPDU_ERR!!!!:\n");
		return -EOPNOTSUPP;
	}
	return 0;
}

static void rtl_op_sw_scan_start(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 const u8 *mac_addr)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));

	RT_TRACE(rtlpriv, COMP_MAC80211, DBG_LOUD, "\n");
	mac->act_scanning = true;
	if (rtlpriv->link_info.higher_busytraffic) {
		mac->skip_scan = true;
		return;
	}

	if (rtlpriv->phydm.ops)
		rtlpriv->phydm.ops->phydm_pause_dig(rtlpriv, 1);

	if (rtlpriv->cfg->ops->get_btc_status())
		rtlpriv->btcoexist.btc_ops->btc_scan_notify(rtlpriv, 1);
	else if (rtlpriv->btcoexist.btc_ops)
		rtlpriv->btcoexist.btc_ops->btc_scan_notify_wifi_only(rtlpriv,
								      1);

	if (rtlpriv->dm.supp_phymode_switch) {
		if (rtlpriv->cfg->ops->chk_switch_dmdp)
			rtlpriv->cfg->ops->chk_switch_dmdp(hw);
	}

	if (mac->link_state == MAC80211_LINKED) {
		rtl_lps_leave(hw);
		mac->link_state = MAC80211_LINKED_SCANNING;
	} else {
		rtl_ips_nic_on(hw);
	}

	/* Dul mac */
	rtlpriv->rtlhal.load_imrandiqk_setting_for2g = false;

	rtlpriv->cfg->ops->led_control(hw, LED_CTL_SITE_SURVEY);
	rtlpriv->cfg->ops->scan_operation_backup(hw, SCAN_OPT_BACKUP_BAND0);
}

static void rtl_op_sw_scan_complete(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));

	RT_TRACE(rtlpriv, COMP_MAC80211, DBG_LOUD, "\n");
	mac->act_scanning = false;
	mac->skip_scan = false;

	rtlpriv->btcoexist.btc_info.ap_num = rtlpriv->scan_list.num;

	if (rtlpriv->link_info.higher_busytraffic)
		return;

	/* p2p will use 1/6/11 to scan */
	if (mac->n_channels == 3)
		mac->p2p_in_use = true;
	else
		mac->p2p_in_use = false;
	mac->n_channels = 0;
	/* Dul mac */
	rtlpriv->rtlhal.load_imrandiqk_setting_for2g = false;

	if (mac->link_state == MAC80211_LINKED_SCANNING) {
		mac->link_state = MAC80211_LINKED;
		if (mac->opmode == NL80211_IFTYPE_STATION) {
			/* fix fwlps issue */
			rtlpriv->cfg->ops->set_network_type(hw, mac->opmode);
		}
	}

	rtlpriv->cfg->ops->scan_operation_backup(hw, SCAN_OPT_RESTORE);
	if (rtlpriv->cfg->ops->get_btc_status())
		rtlpriv->btcoexist.btc_ops->btc_scan_notify(rtlpriv, 0);
	else if (rtlpriv->btcoexist.btc_ops)
		rtlpriv->btcoexist.btc_ops->btc_scan_notify_wifi_only(rtlpriv,
								      0);

	if (rtlpriv->phydm.ops)
		rtlpriv->phydm.ops->phydm_pause_dig(rtlpriv, 0);
}

static int rtl_op_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
			  struct ieee80211_vif *vif, struct ieee80211_sta *sta,
			  struct ieee80211_key_conf *key)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 key_type = NO_ENCRYPTION;
	u8 key_idx;
	bool group_key = false;
	bool wep_only = false;
	int err = 0;
	u8 mac_addr[ETH_ALEN];
	u8 bcast_addr[ETH_ALEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

	rtlpriv->btcoexist.btc_info.in_4way = false;

	if (rtlpriv->cfg->mod_params->sw_crypto || rtlpriv->sec.use_sw_sec) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
			 "not open hw encryption\n");
		return -ENOSPC;	/*User disabled HW-crypto */
	}
	/* To support IBSS, use sw-crypto for GTK */
	if ((vif->type == NL80211_IFTYPE_ADHOC ||
	     vif->type == NL80211_IFTYPE_MESH_POINT) &&
	    !(key->flags & IEEE80211_KEY_FLAG_PAIRWISE))
		return -ENOSPC;
	RT_TRACE(rtlpriv, COMP_SEC, DBG_DMESG,
		 "%s hardware based encryption for keyidx: %d, mac: %pM\n",
		  cmd == SET_KEY ? "Using" : "Disabling", key->keyidx,
		  sta ? sta->addr : bcast_addr);
	rtlpriv->sec.being_setkey = true;
	rtl_ips_nic_on(hw);
	mutex_lock(&rtlpriv->locks.conf_mutex);
	/* <1> get encryption alg */

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
		key_type = WEP40_ENCRYPTION;
		RT_TRACE(rtlpriv, COMP_SEC, DBG_DMESG, "alg:WEP40\n");
		break;
	case WLAN_CIPHER_SUITE_WEP104:
		RT_TRACE(rtlpriv, COMP_SEC, DBG_DMESG, "alg:WEP104\n");
		key_type = WEP104_ENCRYPTION;
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		key_type = TKIP_ENCRYPTION;
		RT_TRACE(rtlpriv, COMP_SEC, DBG_DMESG, "alg:TKIP\n");
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		key_type = AESCCMP_ENCRYPTION;
		RT_TRACE(rtlpriv, COMP_SEC, DBG_DMESG, "alg:CCMP\n");
		break;
	case WLAN_CIPHER_SUITE_AES_CMAC:
		/* HW don't support CMAC encryption,
		 * use software CMAC encryption
		 */
		key_type = AESCMAC_ENCRYPTION;
		RT_TRACE(rtlpriv, COMP_SEC, DBG_DMESG, "alg:CMAC\n");
		RT_TRACE(rtlpriv, COMP_SEC, DBG_DMESG,
			 "HW don't support CMAC encryption, use software CMAC encryption\n");
		err = -EOPNOTSUPP;
		goto out_unlock;
	default:
		pr_err("alg_err:%x!!!!:\n", key->cipher);
		goto out_unlock;
	}
	if (key_type == WEP40_ENCRYPTION ||
	    key_type == WEP104_ENCRYPTION ||
	    vif->type == NL80211_IFTYPE_ADHOC)
		rtlpriv->sec.use_defaultkey = true;

	/* <2> get key_idx */
	key_idx = (u8)(key->keyidx);
	if (key_idx > 3)
		goto out_unlock;
	/* <3> if pairwise key enable_hw_sec */
	group_key = !(key->flags & IEEE80211_KEY_FLAG_PAIRWISE);

	/* wep always be group key, but there are two conditions:
	 * 1) wep only: is just for wep enc, in this condition
	 * rtlpriv->sec.pairwise_enc_algorithm == NO_ENCRYPTION
	 * will be true & enable_hw_sec will be set when wep
	 * ke setting.
	 * 2) wep(group) + AES(pairwise): some AP like cisco
	 * may use it, in this condition enable_hw_sec will not
	 * be set when wep key setting.
	 * we must reset sec_info after lingked before set key,
	 * or some flag will be wrong
	 */
	if (vif->type == NL80211_IFTYPE_AP ||
	    vif->type == NL80211_IFTYPE_MESH_POINT) {
		if (!group_key || key_type == WEP40_ENCRYPTION ||
		    key_type == WEP104_ENCRYPTION) {
			if (group_key)
				wep_only = true;
			rtlpriv->cfg->ops->enable_hw_sec(hw);
		}
	} else {
		if (!group_key || vif->type == NL80211_IFTYPE_ADHOC ||
		    rtlpriv->sec.pairwise_enc_algorithm == NO_ENCRYPTION) {
			if (rtlpriv->sec.pairwise_enc_algorithm ==
			    NO_ENCRYPTION &&
			   (key_type == WEP40_ENCRYPTION ||
			    key_type == WEP104_ENCRYPTION))
				wep_only = true;
			rtlpriv->sec.pairwise_enc_algorithm = key_type;
			RT_TRACE(rtlpriv, COMP_SEC, DBG_DMESG,
				 "set enable_hw_sec, key_type:%x(OPEN:0 WEP40:1 TKIP:2 AES:4 WEP104:5)\n",
				 key_type);
			rtlpriv->cfg->ops->enable_hw_sec(hw);
		}
	}
	/* <4> set key based on cmd */
	switch (cmd) {
	case SET_KEY:
		if (wep_only) {
			RT_TRACE(rtlpriv, COMP_SEC, DBG_DMESG,
				 "set WEP(group/pairwise) key\n");
			/* Pairwise key with an assigned MAC address. */
			rtlpriv->sec.pairwise_enc_algorithm = key_type;
			rtlpriv->sec.group_enc_algorithm = key_type;
			/*set local buf about wep key. */
			memcpy(rtlpriv->sec.key_buf[key_idx],
			       key->key, key->keylen);
			rtlpriv->sec.key_len[key_idx] = key->keylen;
			eth_zero_addr(mac_addr);
		} else if (group_key) {	/* group key */
			RT_TRACE(rtlpriv, COMP_SEC, DBG_DMESG,
				 "set group key\n");
			/* group key */
			rtlpriv->sec.group_enc_algorithm = key_type;
			/*set local buf about group key. */
			memcpy(rtlpriv->sec.key_buf[key_idx],
			       key->key, key->keylen);
			rtlpriv->sec.key_len[key_idx] = key->keylen;
			memcpy(mac_addr, bcast_addr, ETH_ALEN);
		} else {	/* pairwise key */
			RT_TRACE(rtlpriv, COMP_SEC, DBG_DMESG,
				 "set pairwise key\n");
			if (!sta) {
				WARN_ONCE(true,
					  "rtlwifi: pairwise key without mac_addr\n");

				err = -EOPNOTSUPP;
				goto out_unlock;
			}
			/* Pairwise key with an assigned MAC address. */
			rtlpriv->sec.pairwise_enc_algorithm = key_type;
			/*set local buf about pairwise key. */
			memcpy(rtlpriv->sec.key_buf[PAIRWISE_KEYIDX],
			       key->key, key->keylen);
			rtlpriv->sec.key_len[PAIRWISE_KEYIDX] = key->keylen;
			rtlpriv->sec.pairwise_key =
			    rtlpriv->sec.key_buf[PAIRWISE_KEYIDX];
			memcpy(mac_addr, sta->addr, ETH_ALEN);
		}
		rtlpriv->cfg->ops->set_key(hw, key_idx, mac_addr,
					   group_key, key_type, wep_only,
					   false);
		/* <5> tell mac80211 do something: */
		/*must use sw generate IV, or can not work !!!!. */
		key->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;
		key->hw_key_idx = key_idx;
		if (key_type == TKIP_ENCRYPTION)
			key->flags |= IEEE80211_KEY_FLAG_GENERATE_MMIC;
		/*use software CCMP encryption for management frames (MFP) */
		if (key_type == AESCCMP_ENCRYPTION)
			key->flags |= IEEE80211_KEY_FLAG_SW_MGMT_TX;
		break;
	case DISABLE_KEY:
		RT_TRACE(rtlpriv, COMP_SEC, DBG_DMESG,
			 "disable key delete one entry\n");
		/*set local buf about wep key. */
		if (vif->type == NL80211_IFTYPE_AP ||
		    vif->type == NL80211_IFTYPE_MESH_POINT) {
			if (sta)
				rtl_cam_del_entry(hw, sta->addr);
		}
		memset(rtlpriv->sec.key_buf[key_idx], 0, key->keylen);
		rtlpriv->sec.key_len[key_idx] = 0;
		eth_zero_addr(mac_addr);
		/*
		 *mac80211 will delete entries one by one,
		 *so don't use rtl_cam_reset_all_entry
		 *or clear all entries here.
		 */
		rtl_wait_tx_report_acked(hw, 500); /* wait 500ms for TX ack */

		rtl_cam_delete_one_entry(hw, mac_addr, key_idx);
		break;
	default:
		pr_err("cmd_err:%x!!!!:\n", cmd);
	}
out_unlock:
	mutex_unlock(&rtlpriv->locks.conf_mutex);
	rtlpriv->sec.being_setkey = false;
	return err;
}

static void rtl_op_rfkill_poll(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	bool radio_state;
	bool blocked;
	u8 valid = 0;

	if (!test_bit(RTL_STATUS_INTERFACE_START, &rtlpriv->status))
		return;

	mutex_lock(&rtlpriv->locks.conf_mutex);

	/*if Radio On return true here */
	radio_state = rtlpriv->cfg->ops->radio_onoff_checking(hw, &valid);

	if (valid) {
		if (unlikely(radio_state != rtlpriv->rfkill.rfkill_state)) {
			rtlpriv->rfkill.rfkill_state = radio_state;

			RT_TRACE(rtlpriv, COMP_RF, DBG_DMESG,
				 "wireless radio switch turned %s\n",
				  radio_state ? "on" : "off");

			blocked = (rtlpriv->rfkill.rfkill_state == 1) ? 0 : 1;
			wiphy_rfkill_set_hw_state(hw->wiphy, blocked);
		}
	}

	mutex_unlock(&rtlpriv->locks.conf_mutex);
}

/* this function is called by mac80211 to flush tx buffer
 * before switch channel or power save, or tx buffer packet
 * maybe send after offchannel or rf sleep, this may cause
 * dis-association by AP
 */
static void rtl_op_flush(struct ieee80211_hw *hw,
			 struct ieee80211_vif *vif,
			 u32 queues,
			 bool drop)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (rtlpriv->intf_ops->flush)
		rtlpriv->intf_ops->flush(hw, queues, drop);
}

/*	Description:
 *		This routine deals with the Power Configuration CMD
 *		 parsing for RTL8723/RTL8188E Series IC.
 *	Assumption:
 *		We should follow specific format that was released from HW SD.
 */
bool rtl_hal_pwrseqcmdparsing(struct rtl_priv *rtlpriv, u8 cut_version,
			      u8 faversion, u8 interface_type,
			      struct wlan_pwr_cfg pwrcfgcmd[])
{
	struct wlan_pwr_cfg cfg_cmd;
	bool polling_bit = false;
	u32 ary_idx = 0;
	u8 value = 0;
	u32 offset = 0;
	u32 polling_count = 0;
	u32 max_polling_cnt = 5000;

	do {
		cfg_cmd = pwrcfgcmd[ary_idx];
		RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
			 "%s(): offset(%#x),cut_msk(%#x), famsk(%#x), interface_msk(%#x), base(%#x), cmd(%#x), msk(%#x), value(%#x)\n",
			 __func__, GET_PWR_CFG_OFFSET(cfg_cmd),
					    GET_PWR_CFG_CUT_MASK(cfg_cmd),
			 GET_PWR_CFG_FAB_MASK(cfg_cmd),
					      GET_PWR_CFG_INTF_MASK(cfg_cmd),
			 GET_PWR_CFG_BASE(cfg_cmd), GET_PWR_CFG_CMD(cfg_cmd),
			 GET_PWR_CFG_MASK(cfg_cmd), GET_PWR_CFG_VALUE(cfg_cmd));

		if ((GET_PWR_CFG_FAB_MASK(cfg_cmd) & faversion) &&
		    (GET_PWR_CFG_CUT_MASK(cfg_cmd) & cut_version) &&
		    (GET_PWR_CFG_INTF_MASK(cfg_cmd) & interface_type)) {
			switch (GET_PWR_CFG_CMD(cfg_cmd)) {
			case PWR_CMD_READ:
				RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
					 "%s(): PWR_CMD_READ\n", __func__);
				break;
			case PWR_CMD_WRITE:
				RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
					 "%s(): PWR_CMD_WRITE\n", __func__);
				offset = GET_PWR_CFG_OFFSET(cfg_cmd);

				/*Read the value from system register*/
				value = rtl_read_byte(rtlpriv, offset);
				value &= (~(GET_PWR_CFG_MASK(cfg_cmd)));
				value |= (GET_PWR_CFG_VALUE(cfg_cmd) &
					  GET_PWR_CFG_MASK(cfg_cmd));

				/*Write the value back to system register*/
				rtl_write_byte(rtlpriv, offset, value);
				break;
			case PWR_CMD_POLLING:
				RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
					 "%s(): PWR_CMD_POLLING\n", __func__);
				polling_bit = false;
				offset = GET_PWR_CFG_OFFSET(cfg_cmd);

				do {
					value = rtl_read_byte(rtlpriv, offset);

					value &= GET_PWR_CFG_MASK(cfg_cmd);
					if (value ==
					    (GET_PWR_CFG_VALUE(cfg_cmd) &
					     GET_PWR_CFG_MASK(cfg_cmd)))
						polling_bit = true;
					else
						udelay(10);

					if (polling_count++ > max_polling_cnt)
						return false;
				} while (!polling_bit);
				break;
			case PWR_CMD_DELAY:
				RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
					 "%s(): PWR_CMD_DELAY\n", __func__);
				if (GET_PWR_CFG_VALUE(cfg_cmd) ==
				    PWRSEQ_DELAY_US)
					udelay(GET_PWR_CFG_OFFSET(cfg_cmd));
				else
					mdelay(GET_PWR_CFG_OFFSET(cfg_cmd));
				break;
			case PWR_CMD_END:
				RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
					 "%s(): PWR_CMD_END\n", __func__);
				return true;
			default:
				WARN_ONCE(true,
					  "rtlwifi: %s(): Unknown CMD!!\n", __func__);
				break;
			}
		}
		ary_idx++;
	} while (1);

	return true;
}

bool rtl_cmd_send_packet(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl8192_tx_ring *ring;
	struct rtl_tx_desc *pdesc;
	unsigned long flags;
	struct sk_buff *pskb = NULL;

	ring = &rtlpci->tx_ring[BEACON_QUEUE];

	spin_lock_irqsave(&rtlpriv->locks.irq_th_lock, flags);
	pskb = __skb_dequeue(&ring->queue);
	if (pskb)
		dev_kfree_skb_irq(pskb);

	/*this is wrong, fill_tx_cmddesc needs update*/
	pdesc = &ring->desc[0];

	rtlpriv->cfg->ops->fill_tx_cmddesc(hw, (u8 *)pdesc, 1, 1, skb);

	__skb_queue_tail(&ring->queue, skb);

	spin_unlock_irqrestore(&rtlpriv->locks.irq_th_lock, flags);

	rtlpriv->cfg->ops->tx_polling(hw, BEACON_QUEUE);

	return true;
}

const struct ieee80211_ops rtl_ops = {
	.start = rtl_op_start,
	.stop = rtl_op_stop,
	.tx = rtl_op_tx,
	.add_interface = rtl_op_add_interface,
	.remove_interface = rtl_op_remove_interface,
	.change_interface = rtl_op_change_interface,
#ifdef CONFIG_PM
	.suspend = rtl_op_suspend,
	.resume = rtl_op_resume,
#endif
	.config = rtl_op_config,
	.configure_filter = rtl_op_configure_filter,
	.set_key = rtl_op_set_key,
	.sta_statistics = rtl_op_sta_statistics,
	.set_frag_threshold = rtl_op_set_frag_threshold,
	.conf_tx = rtl_op_conf_tx,
	.bss_info_changed = rtl_op_bss_info_changed,
	.get_tsf = rtl_op_get_tsf,
	.set_tsf = rtl_op_set_tsf,
	.reset_tsf = rtl_op_reset_tsf,
	.sta_notify = rtl_op_sta_notify,
	.ampdu_action = rtl_op_ampdu_action,
	.sw_scan_start = rtl_op_sw_scan_start,
	.sw_scan_complete = rtl_op_sw_scan_complete,
	.rfkill_poll = rtl_op_rfkill_poll,
	.sta_add = rtl_op_sta_add,
	.sta_remove = rtl_op_sta_remove,
	.flush = rtl_op_flush,
};

bool rtl_btc_status_false(void)
{
	return false;
}

void rtl_dm_diginit(struct ieee80211_hw *hw, u32 cur_igvalue)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct dig_t *dm_digtable = &rtlpriv->dm_digtable;

	dm_digtable->dig_enable_flag = true;
	dm_digtable->dig_ext_port_stage = DIG_EXT_PORT_STAGE_MAX;
	dm_digtable->cur_igvalue = cur_igvalue;
	dm_digtable->pre_igvalue = 0;
	dm_digtable->cur_sta_cstate = DIG_STA_DISCONNECT;
	dm_digtable->presta_cstate = DIG_STA_DISCONNECT;
	dm_digtable->curmultista_cstate = DIG_MULTISTA_DISCONNECT;
	dm_digtable->rssi_lowthresh = DM_DIG_THRESH_LOW;
	dm_digtable->rssi_highthresh = DM_DIG_THRESH_HIGH;
	dm_digtable->fa_lowthresh = DM_FALSEALARM_THRESH_LOW;
	dm_digtable->fa_highthresh = DM_FALSEALARM_THRESH_HIGH;
	dm_digtable->rx_gain_max = DM_DIG_MAX;
	dm_digtable->rx_gain_min = DM_DIG_MIN;
	dm_digtable->back_val = DM_DIG_BACKOFF_DEFAULT;
	dm_digtable->back_range_max = DM_DIG_BACKOFF_MAX;
	dm_digtable->back_range_min = DM_DIG_BACKOFF_MIN;
	dm_digtable->pre_cck_cca_thres = 0xff;
	dm_digtable->cur_cck_cca_thres = 0x83;
	dm_digtable->forbidden_igi = DM_DIG_MIN;
	dm_digtable->large_fa_hit = 0;
	dm_digtable->recover_cnt = 0;
	dm_digtable->dig_min_0 = 0x25;
	dm_digtable->dig_min_1 = 0x25;
	dm_digtable->media_connect_0 = false;
	dm_digtable->media_connect_1 = false;
	rtlpriv->dm.dm_initialgain_enable = true;
	dm_digtable->bt30_cur_igi = 0x32;
	dm_digtable->pre_cck_pd_state = CCK_PD_STAGE_MAX;
	dm_digtable->cur_cck_pd_state = CCK_PD_STAGE_LOWRSSI;
}
