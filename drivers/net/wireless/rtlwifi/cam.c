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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "wifi.h"
#include "cam.h"

void rtl_cam_reset_sec_info(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpriv->sec.use_defaultkey = false;
	rtlpriv->sec.pairwise_enc_algorithm = NO_ENCRYPTION;
	rtlpriv->sec.group_enc_algorithm = NO_ENCRYPTION;
	memset(rtlpriv->sec.key_buf, 0, KEY_BUF_SIZE * MAX_KEY_LEN);
	memset(rtlpriv->sec.key_len, 0, KEY_BUF_SIZE);
	rtlpriv->sec.pairwise_key = NULL;
}

static void rtl_cam_program_entry(struct ieee80211_hw *hw, u32 entry_no,
			   u8 *mac_addr, u8 *key_cont_128, u16 us_config)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	u32 target_command;
	u32 target_content = 0;
	u8 entry_i;

	RT_TRACE(rtlpriv, COMP_SEC, DBG_LOUD,
		 ("key_cont_128:\n %x:%x:%x:%x:%x:%x\n",
		  key_cont_128[0], key_cont_128[1],
		  key_cont_128[2], key_cont_128[3],
		  key_cont_128[4], key_cont_128[5]));

	for (entry_i = 0; entry_i < CAM_CONTENT_COUNT; entry_i++) {
		target_command = entry_i + CAM_CONTENT_COUNT * entry_no;
		target_command = target_command | BIT(31) | BIT(16);

		if (entry_i == 0) {
			target_content = (u32) (*(mac_addr + 0)) << 16 |
			    (u32) (*(mac_addr + 1)) << 24 | (u32) us_config;

			rtl_write_dword(rtlpriv, rtlpriv->cfg->maps[WCAMI],
					target_content);
			rtl_write_dword(rtlpriv, rtlpriv->cfg->maps[RWCAM],
					target_command);

			RT_TRACE(rtlpriv, COMP_SEC, DBG_LOUD,
				 ("WRITE %x: %x\n",
				  rtlpriv->cfg->maps[WCAMI], target_content));
			RT_TRACE(rtlpriv, COMP_SEC, DBG_LOUD,
				 ("The Key ID is %d\n", entry_no));
			RT_TRACE(rtlpriv, COMP_SEC, DBG_LOUD,
				 ("WRITE %x: %x\n",
				  rtlpriv->cfg->maps[RWCAM], target_command));

		} else if (entry_i == 1) {

			target_content = (u32) (*(mac_addr + 5)) << 24 |
			    (u32) (*(mac_addr + 4)) << 16 |
			    (u32) (*(mac_addr + 3)) << 8 |
			    (u32) (*(mac_addr + 2));

			rtl_write_dword(rtlpriv, rtlpriv->cfg->maps[WCAMI],
					target_content);
			rtl_write_dword(rtlpriv, rtlpriv->cfg->maps[RWCAM],
					target_command);

			RT_TRACE(rtlpriv, COMP_SEC, DBG_LOUD,
				 ("WRITE A4: %x\n", target_content));
			RT_TRACE(rtlpriv, COMP_SEC, DBG_LOUD,
				 ("WRITE A0: %x\n", target_command));

		} else {

			target_content =
			    (u32) (*(key_cont_128 + (entry_i * 4 - 8) + 3)) <<
			    24 | (u32) (*(key_cont_128 + (entry_i * 4 - 8) + 2))
			    << 16 |
			    (u32) (*(key_cont_128 + (entry_i * 4 - 8) + 1)) << 8
			    | (u32) (*(key_cont_128 + (entry_i * 4 - 8) + 0));

			rtl_write_dword(rtlpriv, rtlpriv->cfg->maps[WCAMI],
					target_content);
			rtl_write_dword(rtlpriv, rtlpriv->cfg->maps[RWCAM],
					target_command);
			udelay(100);

			RT_TRACE(rtlpriv, COMP_SEC, DBG_LOUD,
				 ("WRITE A4: %x\n", target_content));
			RT_TRACE(rtlpriv, COMP_SEC, DBG_LOUD,
				 ("WRITE A0: %x\n", target_command));
		}
	}

	RT_TRACE(rtlpriv, COMP_SEC, DBG_LOUD,
		 ("after set key, usconfig:%x\n", us_config));
}

u8 rtl_cam_add_one_entry(struct ieee80211_hw *hw, u8 *mac_addr,
			 u32 ul_key_id, u32 ul_entry_idx, u32 ul_enc_alg,
			 u32 ul_default_key, u8 *key_content)
{
	u32 us_config;
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	RT_TRACE(rtlpriv, COMP_SEC, DBG_DMESG,
		 ("EntryNo:%x, ulKeyId=%x, ulEncAlg=%x, "
		  "ulUseDK=%x MacAddr %pM\n",
		  ul_entry_idx, ul_key_id, ul_enc_alg,
		  ul_default_key, mac_addr));

	if (ul_key_id == TOTAL_CAM_ENTRY) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
			 ("<=== ulKeyId exceed!\n"));
		return 0;
	}

	if (ul_default_key == 1) {
		us_config = CFG_VALID | ((u16) (ul_enc_alg) << 2);
	} else {
		us_config = CFG_VALID | ((ul_enc_alg) << 2) | ul_key_id;
	}

	rtl_cam_program_entry(hw, ul_entry_idx, mac_addr,
			      (u8 *) key_content, us_config);

	RT_TRACE(rtlpriv, COMP_SEC, DBG_DMESG, ("<===\n"));

	return 1;

}
EXPORT_SYMBOL(rtl_cam_add_one_entry);

int rtl_cam_delete_one_entry(struct ieee80211_hw *hw,
			     u8 *mac_addr, u32 ul_key_id)
{
	u32 ul_command;
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	RT_TRACE(rtlpriv, COMP_SEC, DBG_DMESG, ("key_idx:%d\n", ul_key_id));

	ul_command = ul_key_id * CAM_CONTENT_COUNT;
	ul_command = ul_command | BIT(31) | BIT(16);

	rtl_write_dword(rtlpriv, rtlpriv->cfg->maps[WCAMI], 0);
	rtl_write_dword(rtlpriv, rtlpriv->cfg->maps[RWCAM], ul_command);

	RT_TRACE(rtlpriv, COMP_SEC, DBG_DMESG,
		 ("rtl_cam_delete_one_entry(): WRITE A4: %x\n", 0));
	RT_TRACE(rtlpriv, COMP_SEC, DBG_DMESG,
		 ("rtl_cam_delete_one_entry(): WRITE A0: %x\n", ul_command));

	return 0;

}
EXPORT_SYMBOL(rtl_cam_delete_one_entry);

void rtl_cam_reset_all_entry(struct ieee80211_hw *hw)
{
	u32 ul_command;
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	ul_command = BIT(31) | BIT(30);
	rtl_write_dword(rtlpriv, rtlpriv->cfg->maps[RWCAM], ul_command);
}
EXPORT_SYMBOL(rtl_cam_reset_all_entry);

void rtl_cam_mark_invalid(struct ieee80211_hw *hw, u8 uc_index)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	u32 ul_command;
	u32 ul_content;
	u32 ul_enc_algo = rtlpriv->cfg->maps[SEC_CAM_AES];

	switch (rtlpriv->sec.pairwise_enc_algorithm) {
	case WEP40_ENCRYPTION:
		ul_enc_algo = rtlpriv->cfg->maps[SEC_CAM_WEP40];
		break;
	case WEP104_ENCRYPTION:
		ul_enc_algo = rtlpriv->cfg->maps[SEC_CAM_WEP104];
		break;
	case TKIP_ENCRYPTION:
		ul_enc_algo = rtlpriv->cfg->maps[SEC_CAM_TKIP];
		break;
	case AESCCMP_ENCRYPTION:
		ul_enc_algo = rtlpriv->cfg->maps[SEC_CAM_AES];
		break;
	default:
		ul_enc_algo = rtlpriv->cfg->maps[SEC_CAM_AES];
	}

	ul_content = (uc_index & 3) | ((u16) (ul_enc_algo) << 2);

	ul_content |= BIT(15);
	ul_command = CAM_CONTENT_COUNT * uc_index;
	ul_command = ul_command | BIT(31) | BIT(16);

	rtl_write_dword(rtlpriv, rtlpriv->cfg->maps[WCAMI], ul_content);
	rtl_write_dword(rtlpriv, rtlpriv->cfg->maps[RWCAM], ul_command);

	RT_TRACE(rtlpriv, COMP_SEC, DBG_DMESG,
		 ("rtl_cam_mark_invalid(): WRITE A4: %x\n", ul_content));
	RT_TRACE(rtlpriv, COMP_SEC, DBG_DMESG,
		 ("rtl_cam_mark_invalid(): WRITE A0: %x\n", ul_command));
}
EXPORT_SYMBOL(rtl_cam_mark_invalid);

void rtl_cam_empty_entry(struct ieee80211_hw *hw, u8 uc_index)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	u32 ul_command;
	u32 ul_content;
	u32 ul_encalgo = rtlpriv->cfg->maps[SEC_CAM_AES];
	u8 entry_i;

	switch (rtlpriv->sec.pairwise_enc_algorithm) {
	case WEP40_ENCRYPTION:
		ul_encalgo = rtlpriv->cfg->maps[SEC_CAM_WEP40];
		break;
	case WEP104_ENCRYPTION:
		ul_encalgo = rtlpriv->cfg->maps[SEC_CAM_WEP104];
		break;
	case TKIP_ENCRYPTION:
		ul_encalgo = rtlpriv->cfg->maps[SEC_CAM_TKIP];
		break;
	case AESCCMP_ENCRYPTION:
		ul_encalgo = rtlpriv->cfg->maps[SEC_CAM_AES];
		break;
	default:
		ul_encalgo = rtlpriv->cfg->maps[SEC_CAM_AES];
	}

	for (entry_i = 0; entry_i < CAM_CONTENT_COUNT; entry_i++) {

		if (entry_i == 0) {
			ul_content =
			    (uc_index & 0x03) | ((u16) (ul_encalgo) << 2);
			ul_content |= BIT(15);

		} else {
			ul_content = 0;
		}

		ul_command = CAM_CONTENT_COUNT * uc_index + entry_i;
		ul_command = ul_command | BIT(31) | BIT(16);

		rtl_write_dword(rtlpriv, rtlpriv->cfg->maps[WCAMI], ul_content);
		rtl_write_dword(rtlpriv, rtlpriv->cfg->maps[RWCAM], ul_command);

		RT_TRACE(rtlpriv, COMP_SEC, DBG_LOUD,
			 ("rtl_cam_empty_entry(): WRITE A4: %x\n",
			  ul_content));
		RT_TRACE(rtlpriv, COMP_SEC, DBG_LOUD,
			 ("rtl_cam_empty_entry(): WRITE A0: %x\n",
			  ul_command));
	}

}
EXPORT_SYMBOL(rtl_cam_empty_entry);

u8 rtl_cam_get_free_entry(struct ieee80211_hw *hw, u8 *sta_addr)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 bitmap = (rtlpriv->sec.hwsec_cam_bitmap) >> 4;
	u8 entry_idx = 0;
	u8 i, *addr;

	if (NULL == sta_addr) {
		RT_TRACE(rtlpriv, COMP_SEC, DBG_EMERG,
			("sta_addr is NULL.\n"));
		return TOTAL_CAM_ENTRY;
	}
	/* Does STA already exist? */
	for (i = 4; i < TOTAL_CAM_ENTRY; i++) {
		addr = rtlpriv->sec.hwsec_cam_sta_addr[i];
		if (memcmp(addr, sta_addr, ETH_ALEN) == 0)
			return i;
	}
	/* Get a free CAM entry. */
	for (entry_idx = 4; entry_idx < TOTAL_CAM_ENTRY; entry_idx++) {
		if ((bitmap & BIT(0)) == 0) {
			RT_TRACE(rtlpriv, COMP_SEC, DBG_EMERG,
				("-----hwsec_cam_bitmap: 0x%x entry_idx=%d\n",
				 rtlpriv->sec.hwsec_cam_bitmap, entry_idx));
			rtlpriv->sec.hwsec_cam_bitmap |= BIT(0) << entry_idx;
			memcpy(rtlpriv->sec.hwsec_cam_sta_addr[entry_idx],
			       sta_addr, ETH_ALEN);
			return entry_idx;
		}
		bitmap = bitmap >> 1;
	}
	return TOTAL_CAM_ENTRY;
}
EXPORT_SYMBOL(rtl_cam_get_free_entry);

void rtl_cam_del_entry(struct ieee80211_hw *hw, u8 *sta_addr)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 bitmap;
	u8 i, *addr;

	if (NULL == sta_addr) {
		RT_TRACE(rtlpriv, COMP_SEC, DBG_EMERG,
			("sta_addr is NULL.\n"));
	}

	if ((sta_addr[0]|sta_addr[1]|sta_addr[2]|sta_addr[3]|\
				sta_addr[4]|sta_addr[5]) == 0) {
		RT_TRACE(rtlpriv, COMP_SEC, DBG_EMERG,
			("sta_addr is 00:00:00:00:00:00.\n"));
		return;
	}
	/* Does STA already exist? */
	for (i = 4; i < TOTAL_CAM_ENTRY; i++) {
		addr = rtlpriv->sec.hwsec_cam_sta_addr[i];
		bitmap = (rtlpriv->sec.hwsec_cam_bitmap) >> i;
		if (((bitmap & BIT(0)) == BIT(0)) &&
		    (memcmp(addr, sta_addr, ETH_ALEN) == 0)) {
			/* Remove from HW Security CAM */
			memset(rtlpriv->sec.hwsec_cam_sta_addr[i], 0, ETH_ALEN);
			rtlpriv->sec.hwsec_cam_bitmap &= ~(BIT(0) << i);
			pr_info("&&&&&&&&&del entry %d\n", i);
		}
	}
	return;
}
EXPORT_SYMBOL(rtl_cam_del_entry);
