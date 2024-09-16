// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2019-2022  Realtek Corporation
 */
#include "cam.h"
#include "core.h"
#include "debug.h"
#include "fw.h"
#include "mac.h"
#include "phy.h"
#include "ps.h"
#include "reg.h"
#include "util.h"
#include "wow.h"

void rtw89_wow_parse_akm(struct rtw89_dev *rtwdev, struct sk_buff *skb)
{
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)skb->data;
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	const u8 *rsn, *ies = mgmt->u.assoc_req.variable;
	struct rtw89_rsn_ie *rsn_ie;

	rsn = cfg80211_find_ie(WLAN_EID_RSN, ies, skb->len);
	if (!rsn)
		return;

	rsn_ie = (struct rtw89_rsn_ie *)rsn;
	rtw_wow->akm = rsn_ie->akm_cipher_suite.type;
}

#define RTW89_CIPHER_INFO_DEF(cipher) \
	{WLAN_CIPHER_SUITE_ ## cipher, .fw_alg = RTW89_WOW_FW_ALG_ ## cipher, \
	 .len = WLAN_KEY_LEN_ ## cipher}

static const struct rtw89_cipher_info rtw89_cipher_info_defs[] = {
	RTW89_CIPHER_INFO_DEF(WEP40),
	RTW89_CIPHER_INFO_DEF(WEP104),
	RTW89_CIPHER_INFO_DEF(TKIP),
	RTW89_CIPHER_INFO_DEF(CCMP),
	RTW89_CIPHER_INFO_DEF(GCMP),
	RTW89_CIPHER_INFO_DEF(CCMP_256),
	RTW89_CIPHER_INFO_DEF(GCMP_256),
	RTW89_CIPHER_INFO_DEF(AES_CMAC),
};

#undef RTW89_CIPHER_INFO_DEF

static const
struct rtw89_cipher_info *rtw89_cipher_alg_recognize(u32 cipher)
{
	const struct rtw89_cipher_info *cipher_info_defs;
	int i;

	for (i = 0; i < ARRAY_SIZE(rtw89_cipher_info_defs); i++) {
		cipher_info_defs = &rtw89_cipher_info_defs[i];
		if (cipher_info_defs->cipher == cipher)
			return cipher_info_defs;
	}

	return NULL;
}

static int _pn_to_iv(struct rtw89_dev *rtwdev, struct ieee80211_key_conf *key,
		     u8 *iv, u64 pn, u8 key_idx)
{
	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_TKIP:
		iv[0] = u64_get_bits(pn, RTW89_KEY_PN_1);
		iv[1] = (u64_get_bits(pn, RTW89_KEY_PN_1) | 0x20) & 0x7f;
		iv[2] = u64_get_bits(pn, RTW89_KEY_PN_0);
		break;
	case WLAN_CIPHER_SUITE_CCMP:
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_CCMP_256:
	case WLAN_CIPHER_SUITE_GCMP_256:
		iv[0] = u64_get_bits(pn, RTW89_KEY_PN_0);
		iv[1] = u64_get_bits(pn, RTW89_KEY_PN_1);
		iv[2] = 0;
		break;
	default:
		return -EINVAL;
	}

	iv[3] = BIT(5) | ((key_idx & 0x3) << 6);
	iv[4] = u64_get_bits(pn, RTW89_KEY_PN_2);
	iv[5] = u64_get_bits(pn, RTW89_KEY_PN_3);
	iv[6] = u64_get_bits(pn, RTW89_KEY_PN_4);
	iv[7] = u64_get_bits(pn, RTW89_KEY_PN_5);

	return 0;
}

static int rtw89_rx_pn_to_iv(struct rtw89_dev *rtwdev,
			     struct ieee80211_key_conf *key,
			     u8 *iv)
{
	struct ieee80211_key_seq seq;
	int err;
	u64 pn;

	ieee80211_get_key_rx_seq(key, 0, &seq);

	/* seq.ccmp.pn[] is BE order array */
	pn = u64_encode_bits(seq.ccmp.pn[0], RTW89_KEY_PN_5) |
	     u64_encode_bits(seq.ccmp.pn[1], RTW89_KEY_PN_4) |
	     u64_encode_bits(seq.ccmp.pn[2], RTW89_KEY_PN_3) |
	     u64_encode_bits(seq.ccmp.pn[3], RTW89_KEY_PN_2) |
	     u64_encode_bits(seq.ccmp.pn[4], RTW89_KEY_PN_1) |
	     u64_encode_bits(seq.ccmp.pn[5], RTW89_KEY_PN_0);

	err = _pn_to_iv(rtwdev, key, iv, pn, key->keyidx);
	if (err)
		return err;

	rtw89_debug(rtwdev, RTW89_DBG_WOW, "%s key %d pn-%llx to iv-%*ph\n",
		    __func__, key->keyidx, pn, 8, iv);

	return 0;
}

static int rtw89_tx_pn_to_iv(struct rtw89_dev *rtwdev,
			     struct ieee80211_key_conf *key,
			     u8 *iv)
{
	int err;
	u64 pn;

	pn = atomic64_inc_return(&key->tx_pn);
	err = _pn_to_iv(rtwdev, key, iv, pn, key->keyidx);
	if (err)
		return err;

	rtw89_debug(rtwdev, RTW89_DBG_WOW, "%s key %d pn-%llx to iv-%*ph\n",
		    __func__, key->keyidx, pn, 8, iv);

	return 0;
}

static int _iv_to_pn(struct rtw89_dev *rtwdev, u8 *iv, u64 *pn, u8 *key_id,
		     struct ieee80211_key_conf *key)
{
	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_TKIP:
		*pn = u64_encode_bits(iv[2], RTW89_KEY_PN_0) |
		      u64_encode_bits(iv[0], RTW89_KEY_PN_1);
		break;
	case WLAN_CIPHER_SUITE_CCMP:
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_CCMP_256:
	case WLAN_CIPHER_SUITE_GCMP_256:
		*pn = u64_encode_bits(iv[0], RTW89_KEY_PN_0) |
		      u64_encode_bits(iv[1], RTW89_KEY_PN_1);
		break;
	default:
		return -EINVAL;
	}

	*pn |= u64_encode_bits(iv[4], RTW89_KEY_PN_2) |
	       u64_encode_bits(iv[5], RTW89_KEY_PN_3) |
	       u64_encode_bits(iv[6], RTW89_KEY_PN_4) |
	       u64_encode_bits(iv[7], RTW89_KEY_PN_5);

	if (key_id)
		*key_id = *(iv + 3) >> 6;

	return 0;
}

static int rtw89_rx_iv_to_pn(struct rtw89_dev *rtwdev,
			     struct ieee80211_key_conf *key,
			     u8 *iv)
{
	struct ieee80211_key_seq seq;
	int err;
	u64 pn;

	err = _iv_to_pn(rtwdev, iv, &pn, NULL, key);
	if (err)
		return err;

	/* seq.ccmp.pn[] is BE order array */
	seq.ccmp.pn[0] = u64_get_bits(pn, RTW89_KEY_PN_5);
	seq.ccmp.pn[1] = u64_get_bits(pn, RTW89_KEY_PN_4);
	seq.ccmp.pn[2] = u64_get_bits(pn, RTW89_KEY_PN_3);
	seq.ccmp.pn[3] = u64_get_bits(pn, RTW89_KEY_PN_2);
	seq.ccmp.pn[4] = u64_get_bits(pn, RTW89_KEY_PN_1);
	seq.ccmp.pn[5] = u64_get_bits(pn, RTW89_KEY_PN_0);

	ieee80211_set_key_rx_seq(key, 0, &seq);
	rtw89_debug(rtwdev, RTW89_DBG_WOW, "%s key %d iv-%*ph to pn-%*ph\n",
		    __func__, key->keyidx, 8, iv, 6, seq.ccmp.pn);

	return 0;
}

static int rtw89_tx_iv_to_pn(struct rtw89_dev *rtwdev,
			     struct ieee80211_key_conf *key,
			     u8 *iv)
{
	int err;
	u64 pn;

	err = _iv_to_pn(rtwdev, iv, &pn, NULL, key);
	if (err)
		return err;

	atomic64_set(&key->tx_pn, pn);
	rtw89_debug(rtwdev, RTW89_DBG_WOW, "%s key %d iv-%*ph to pn-%llx\n",
		    __func__, key->keyidx, 8, iv, pn);

	return 0;
}

static int rtw89_rx_pn_get_pmf(struct rtw89_dev *rtwdev,
			       struct ieee80211_key_conf *key,
			       struct rtw89_wow_gtk_info *gtk_info)
{
	struct ieee80211_key_seq seq;
	u64 pn;

	if (key->keyidx == 4)
		memcpy(gtk_info->igtk[0], key->key, key->keylen);
	else if (key->keyidx == 5)
		memcpy(gtk_info->igtk[1], key->key, key->keylen);
	else
		return -EINVAL;

	ieee80211_get_key_rx_seq(key, 0, &seq);

	/* seq.ccmp.pn[] is BE order array */
	pn = u64_encode_bits(seq.ccmp.pn[0], RTW89_KEY_PN_5) |
	     u64_encode_bits(seq.ccmp.pn[1], RTW89_KEY_PN_4) |
	     u64_encode_bits(seq.ccmp.pn[2], RTW89_KEY_PN_3) |
	     u64_encode_bits(seq.ccmp.pn[3], RTW89_KEY_PN_2) |
	     u64_encode_bits(seq.ccmp.pn[4], RTW89_KEY_PN_1) |
	     u64_encode_bits(seq.ccmp.pn[5], RTW89_KEY_PN_0);
	gtk_info->ipn = cpu_to_le64(pn);
	gtk_info->igtk_keyid = cpu_to_le32(key->keyidx);
	rtw89_debug(rtwdev, RTW89_DBG_WOW, "%s key %d pn-%llx\n",
		    __func__, key->keyidx, pn);

	return 0;
}

static int rtw89_rx_pn_set_pmf(struct rtw89_dev *rtwdev,
			       struct ieee80211_key_conf *key,
			       u64 pn)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct rtw89_wow_aoac_report *aoac_rpt = &rtw_wow->aoac_rpt;
	struct ieee80211_key_seq seq;

	if (key->keyidx != aoac_rpt->igtk_key_id)
		return 0;

	/* seq.ccmp.pn[] is BE order array */
	seq.ccmp.pn[0] = u64_get_bits(pn, RTW89_KEY_PN_5);
	seq.ccmp.pn[1] = u64_get_bits(pn, RTW89_KEY_PN_4);
	seq.ccmp.pn[2] = u64_get_bits(pn, RTW89_KEY_PN_3);
	seq.ccmp.pn[3] = u64_get_bits(pn, RTW89_KEY_PN_2);
	seq.ccmp.pn[4] = u64_get_bits(pn, RTW89_KEY_PN_1);
	seq.ccmp.pn[5] = u64_get_bits(pn, RTW89_KEY_PN_0);

	ieee80211_set_key_rx_seq(key, 0, &seq);
	rtw89_debug(rtwdev, RTW89_DBG_WOW, "%s key %d pn-%*ph\n",
		    __func__, key->keyidx, 6, seq.ccmp.pn);

	return 0;
}

static void rtw89_wow_get_key_info_iter(struct ieee80211_hw *hw,
					struct ieee80211_vif *vif,
					struct ieee80211_sta *sta,
					struct ieee80211_key_conf *key,
					void *data)
{
	struct rtw89_dev *rtwdev = hw->priv;
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct rtw89_wow_key_info *key_info = &rtw_wow->key_info;
	struct rtw89_wow_gtk_info *gtk_info = &rtw_wow->gtk_info;
	const struct rtw89_cipher_info *cipher_info;
	bool *err = data;
	int ret;

	cipher_info = rtw89_cipher_alg_recognize(key->cipher);

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_TKIP:
	case WLAN_CIPHER_SUITE_CCMP:
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_CCMP_256:
	case WLAN_CIPHER_SUITE_GCMP_256:
		if (sta) {
			ret = rtw89_tx_pn_to_iv(rtwdev, key,
						key_info->ptk_tx_iv);
			if (ret)
				goto err;
			ret = rtw89_rx_pn_to_iv(rtwdev, key,
						key_info->ptk_rx_iv);
			if (ret)
				goto err;

			rtw_wow->ptk_alg = cipher_info->fw_alg;
			rtw_wow->ptk_keyidx = key->keyidx;
		} else {
			ret = rtw89_rx_pn_to_iv(rtwdev, key,
						key_info->gtk_rx_iv[key->keyidx]);
			if (ret)
				goto err;

			rtw_wow->gtk_alg = cipher_info->fw_alg;
			key_info->gtk_keyidx = key->keyidx;
		}
		break;
	case WLAN_CIPHER_SUITE_AES_CMAC:
		ret = rtw89_rx_pn_get_pmf(rtwdev, key, gtk_info);
		if (ret)
			goto err;
		break;
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		/* WEP only set group key in mac80211, but fw need to set
		 * both of pairwise key and group key.
		 */
		rtw_wow->ptk_alg = cipher_info->fw_alg;
		rtw_wow->ptk_keyidx = key->keyidx;
		rtw_wow->gtk_alg = cipher_info->fw_alg;
		key_info->gtk_keyidx = key->keyidx;
		break;
	default:
		rtw89_debug(rtwdev, RTW89_DBG_WOW, "unsupport cipher %x\n",
			    key->cipher);
		goto err;
	}

	return;
err:
	*err = true;
}

static void rtw89_wow_set_key_info_iter(struct ieee80211_hw *hw,
					struct ieee80211_vif *vif,
					struct ieee80211_sta *sta,
					struct ieee80211_key_conf *key,
					void *data)
{
	struct rtw89_dev *rtwdev = hw->priv;
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct rtw89_wow_aoac_report *aoac_rpt = &rtw_wow->aoac_rpt;
	struct rtw89_set_key_info_iter_data *iter_data = data;
	bool update_tx_key_info = iter_data->rx_ready;
	int ret;

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_TKIP:
	case WLAN_CIPHER_SUITE_CCMP:
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_CCMP_256:
	case WLAN_CIPHER_SUITE_GCMP_256:
		if (sta && !update_tx_key_info) {
			ret = rtw89_rx_iv_to_pn(rtwdev, key,
						aoac_rpt->ptk_rx_iv);
			if (ret)
				goto err;
		}

		if (sta && update_tx_key_info) {
			ret = rtw89_tx_iv_to_pn(rtwdev, key,
						aoac_rpt->ptk_tx_iv);
			if (ret)
				goto err;
		}

		if (!sta && !update_tx_key_info) {
			ret = rtw89_rx_iv_to_pn(rtwdev, key,
						aoac_rpt->gtk_rx_iv[key->keyidx]);
			if (ret)
				goto err;
		}

		if (!sta && update_tx_key_info && aoac_rpt->rekey_ok)
			iter_data->gtk_cipher = key->cipher;
		break;
	case WLAN_CIPHER_SUITE_AES_CMAC:
		if (update_tx_key_info) {
			if (aoac_rpt->rekey_ok)
				iter_data->igtk_cipher = key->cipher;
		} else {
			ret = rtw89_rx_pn_set_pmf(rtwdev, key,
						  aoac_rpt->igtk_ipn);
			if (ret)
				goto err;
		}
		break;
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		break;
	default:
		rtw89_debug(rtwdev, RTW89_DBG_WOW, "unsupport cipher %x\n",
			    key->cipher);
		goto err;
	}

	return;

err:
	iter_data->error = true;
}

static void rtw89_wow_key_clear(struct rtw89_dev *rtwdev)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;

	memset(&rtw_wow->aoac_rpt, 0, sizeof(rtw_wow->aoac_rpt));
	memset(&rtw_wow->gtk_info, 0, sizeof(rtw_wow->gtk_info));
	memset(&rtw_wow->key_info, 0, sizeof(rtw_wow->key_info));
	rtw_wow->ptk_alg = 0;
	rtw_wow->gtk_alg = 0;
}

static void rtw89_wow_construct_key_info(struct rtw89_dev *rtwdev)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct rtw89_wow_key_info *key_info = &rtw_wow->key_info;
	struct ieee80211_vif *wow_vif = rtwdev->wow.wow_vif;
	bool err = false;

	rcu_read_lock();
	ieee80211_iter_keys_rcu(rtwdev->hw, wow_vif,
				rtw89_wow_get_key_info_iter, &err);
	rcu_read_unlock();

	if (err) {
		rtw89_wow_key_clear(rtwdev);
		return;
	}

	key_info->valid_check = RTW89_WOW_VALID_CHECK;
	key_info->symbol_check_en = RTW89_WOW_SYMBOL_CHK_PTK |
				    RTW89_WOW_SYMBOL_CHK_GTK;
}

static void rtw89_wow_debug_aoac_rpt(struct rtw89_dev *rtwdev)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct rtw89_wow_aoac_report *aoac_rpt = &rtw_wow->aoac_rpt;

	if (!rtw89_debug_is_enabled(rtwdev, RTW89_DBG_WOW))
		return;

	rtw89_debug(rtwdev, RTW89_DBG_WOW, "[aoac_rpt] rpt_ver = %d\n",
		    aoac_rpt->rpt_ver);
	rtw89_debug(rtwdev, RTW89_DBG_WOW, "[aoac_rpt] sec_type = %d\n",
		    aoac_rpt->sec_type);
	rtw89_debug(rtwdev, RTW89_DBG_WOW, "[aoac_rpt] key_idx = %d\n",
		    aoac_rpt->key_idx);
	rtw89_debug(rtwdev, RTW89_DBG_WOW, "[aoac_rpt] pattern_idx = %d\n",
		    aoac_rpt->pattern_idx);
	rtw89_debug(rtwdev, RTW89_DBG_WOW, "[aoac_rpt] rekey_ok = %d\n",
		    aoac_rpt->rekey_ok);
	rtw89_debug(rtwdev, RTW89_DBG_WOW, "[aoac_rpt] ptk_tx_iv = %*ph\n",
		    8, aoac_rpt->ptk_tx_iv);
	rtw89_debug(rtwdev, RTW89_DBG_WOW,
		    "[aoac_rpt] eapol_key_replay_count = %*ph\n",
		    8, aoac_rpt->eapol_key_replay_count);
	rtw89_debug(rtwdev, RTW89_DBG_WOW, "[aoac_rpt] ptk_rx_iv = %*ph\n",
		    8, aoac_rpt->ptk_rx_iv);
	rtw89_debug(rtwdev, RTW89_DBG_WOW, "[aoac_rpt] gtk_rx_iv[0] = %*ph\n",
		    8, aoac_rpt->gtk_rx_iv[0]);
	rtw89_debug(rtwdev, RTW89_DBG_WOW, "[aoac_rpt] gtk_rx_iv[1] = %*ph\n",
		    8, aoac_rpt->gtk_rx_iv[1]);
	rtw89_debug(rtwdev, RTW89_DBG_WOW, "[aoac_rpt] gtk_rx_iv[2] = %*ph\n",
		    8, aoac_rpt->gtk_rx_iv[2]);
	rtw89_debug(rtwdev, RTW89_DBG_WOW, "[aoac_rpt] gtk_rx_iv[3] = %*ph\n",
		    8, aoac_rpt->gtk_rx_iv[3]);
	rtw89_debug(rtwdev, RTW89_DBG_WOW, "[aoac_rpt] igtk_key_id = %llu\n",
		    aoac_rpt->igtk_key_id);
	rtw89_debug(rtwdev, RTW89_DBG_WOW, "[aoac_rpt] igtk_ipn = %llu\n",
		    aoac_rpt->igtk_ipn);
	rtw89_debug(rtwdev, RTW89_DBG_WOW, "[aoac_rpt] igtk = %*ph\n",
		    32, aoac_rpt->igtk);
}

static int rtw89_wow_get_aoac_rpt_reg(struct rtw89_dev *rtwdev)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct rtw89_wow_aoac_report *aoac_rpt = &rtw_wow->aoac_rpt;
	struct rtw89_mac_c2h_info c2h_info = {};
	struct rtw89_mac_h2c_info h2c_info = {};
	u8 igtk_ipn[8];
	u8 key_idx;
	int ret;

	h2c_info.id = RTW89_FWCMD_H2CREG_FUNC_AOAC_RPT_1;
	h2c_info.content_len = 2;
	ret = rtw89_fw_msg_reg(rtwdev, &h2c_info, &c2h_info);
	if (ret)
		return ret;

	aoac_rpt->key_idx =
		u32_get_bits(c2h_info.u.c2hreg[0], RTW89_C2HREG_AOAC_RPT_1_W0_KEY_IDX);
	key_idx = aoac_rpt->key_idx;
	aoac_rpt->gtk_rx_iv[key_idx][0] =
		u32_get_bits(c2h_info.u.c2hreg[1], RTW89_C2HREG_AOAC_RPT_1_W1_IV_0);
	aoac_rpt->gtk_rx_iv[key_idx][1] =
		u32_get_bits(c2h_info.u.c2hreg[1], RTW89_C2HREG_AOAC_RPT_1_W1_IV_1);
	aoac_rpt->gtk_rx_iv[key_idx][2] =
		u32_get_bits(c2h_info.u.c2hreg[1], RTW89_C2HREG_AOAC_RPT_1_W1_IV_2);
	aoac_rpt->gtk_rx_iv[key_idx][3] =
		u32_get_bits(c2h_info.u.c2hreg[1], RTW89_C2HREG_AOAC_RPT_1_W1_IV_3);
	aoac_rpt->gtk_rx_iv[key_idx][4] =
		u32_get_bits(c2h_info.u.c2hreg[2], RTW89_C2HREG_AOAC_RPT_1_W2_IV_4);
	aoac_rpt->gtk_rx_iv[key_idx][5] =
		u32_get_bits(c2h_info.u.c2hreg[2], RTW89_C2HREG_AOAC_RPT_1_W2_IV_5);
	aoac_rpt->gtk_rx_iv[key_idx][6] =
		u32_get_bits(c2h_info.u.c2hreg[2], RTW89_C2HREG_AOAC_RPT_1_W2_IV_6);
	aoac_rpt->gtk_rx_iv[key_idx][7] =
		u32_get_bits(c2h_info.u.c2hreg[2], RTW89_C2HREG_AOAC_RPT_1_W2_IV_7);
	aoac_rpt->ptk_rx_iv[0] =
		u32_get_bits(c2h_info.u.c2hreg[3], RTW89_C2HREG_AOAC_RPT_1_W3_PTK_IV_0);
	aoac_rpt->ptk_rx_iv[1] =
		u32_get_bits(c2h_info.u.c2hreg[3], RTW89_C2HREG_AOAC_RPT_1_W3_PTK_IV_1);
	aoac_rpt->ptk_rx_iv[2] =
		u32_get_bits(c2h_info.u.c2hreg[3], RTW89_C2HREG_AOAC_RPT_1_W3_PTK_IV_2);
	aoac_rpt->ptk_rx_iv[3] =
		u32_get_bits(c2h_info.u.c2hreg[3], RTW89_C2HREG_AOAC_RPT_1_W3_PTK_IV_3);

	h2c_info.id = RTW89_FWCMD_H2CREG_FUNC_AOAC_RPT_2;
	h2c_info.content_len = 2;
	ret = rtw89_fw_msg_reg(rtwdev, &h2c_info, &c2h_info);
	if (ret)
		return ret;

	aoac_rpt->ptk_rx_iv[4] =
		u32_get_bits(c2h_info.u.c2hreg[0], RTW89_C2HREG_AOAC_RPT_2_W0_PTK_IV_4);
	aoac_rpt->ptk_rx_iv[5] =
		u32_get_bits(c2h_info.u.c2hreg[0], RTW89_C2HREG_AOAC_RPT_2_W0_PTK_IV_5);
	aoac_rpt->ptk_rx_iv[6] =
		u32_get_bits(c2h_info.u.c2hreg[1], RTW89_C2HREG_AOAC_RPT_2_W1_PTK_IV_6);
	aoac_rpt->ptk_rx_iv[7] =
		u32_get_bits(c2h_info.u.c2hreg[1], RTW89_C2HREG_AOAC_RPT_2_W1_PTK_IV_7);
	igtk_ipn[0] =
		u32_get_bits(c2h_info.u.c2hreg[1], RTW89_C2HREG_AOAC_RPT_2_W1_IGTK_IPN_IV_0);
	igtk_ipn[1] =
		u32_get_bits(c2h_info.u.c2hreg[1], RTW89_C2HREG_AOAC_RPT_2_W1_IGTK_IPN_IV_1);
	igtk_ipn[2] =
		u32_get_bits(c2h_info.u.c2hreg[2], RTW89_C2HREG_AOAC_RPT_2_W2_IGTK_IPN_IV_2);
	igtk_ipn[3] =
		u32_get_bits(c2h_info.u.c2hreg[2], RTW89_C2HREG_AOAC_RPT_2_W2_IGTK_IPN_IV_3);
	igtk_ipn[4] =
		u32_get_bits(c2h_info.u.c2hreg[2], RTW89_C2HREG_AOAC_RPT_2_W2_IGTK_IPN_IV_4);
	igtk_ipn[5] =
		u32_get_bits(c2h_info.u.c2hreg[2], RTW89_C2HREG_AOAC_RPT_2_W2_IGTK_IPN_IV_5);
	igtk_ipn[6] =
		u32_get_bits(c2h_info.u.c2hreg[3], RTW89_C2HREG_AOAC_RPT_2_W3_IGTK_IPN_IV_6);
	igtk_ipn[7] =
		u32_get_bits(c2h_info.u.c2hreg[3], RTW89_C2HREG_AOAC_RPT_2_W3_IGTK_IPN_IV_7);
	aoac_rpt->igtk_ipn = u64_encode_bits(igtk_ipn[0], RTW89_IGTK_IPN_0) |
			     u64_encode_bits(igtk_ipn[1], RTW89_IGTK_IPN_1) |
			     u64_encode_bits(igtk_ipn[2], RTW89_IGTK_IPN_2) |
			     u64_encode_bits(igtk_ipn[3], RTW89_IGTK_IPN_3) |
			     u64_encode_bits(igtk_ipn[4], RTW89_IGTK_IPN_4) |
			     u64_encode_bits(igtk_ipn[5], RTW89_IGTK_IPN_5) |
			     u64_encode_bits(igtk_ipn[6], RTW89_IGTK_IPN_6) |
			     u64_encode_bits(igtk_ipn[7], RTW89_IGTK_IPN_7);

	return 0;
}

static int rtw89_wow_get_aoac_rpt(struct rtw89_dev *rtwdev, bool rx_ready)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	int ret;

	if (!rtw_wow->ptk_alg)
		return -EPERM;

	if (!rx_ready) {
		ret = rtw89_wow_get_aoac_rpt_reg(rtwdev);
		if (ret) {
			rtw89_err(rtwdev, "wow: failed to get aoac rpt by reg\n");
			return ret;
		}
	} else {
		ret = rtw89_fw_h2c_wow_request_aoac(rtwdev);
		if (ret) {
			rtw89_err(rtwdev, "wow: failed to get aoac rpt by pkt\n");
			return ret;
		}
	}

	rtw89_wow_debug_aoac_rpt(rtwdev);

	return 0;
}

static struct ieee80211_key_conf *rtw89_wow_gtk_rekey(struct rtw89_dev *rtwdev,
						      u32 cipher, u8 keyidx, u8 *gtk)
{
	struct ieee80211_vif *wow_vif = rtwdev->wow.wow_vif;
	const struct rtw89_cipher_info *cipher_info;
	struct ieee80211_key_conf *rekey_conf;
	struct ieee80211_key_conf *key;
	u8 sz;

	cipher_info = rtw89_cipher_alg_recognize(cipher);
	sz = struct_size(rekey_conf, key, cipher_info->len);
	rekey_conf = kmalloc(sz, GFP_KERNEL);
	if (!rekey_conf)
		return NULL;

	rekey_conf->cipher = cipher;
	rekey_conf->keyidx = keyidx;
	rekey_conf->keylen = cipher_info->len;
	memcpy(rekey_conf->key, gtk,
	       flex_array_size(rekey_conf, key, cipher_info->len));

	/* ieee80211_gtk_rekey_add() will call set_key(), therefore we
	 * need to unlock mutex
	 */
	mutex_unlock(&rtwdev->mutex);
	key = ieee80211_gtk_rekey_add(wow_vif, rekey_conf, -1);
	mutex_lock(&rtwdev->mutex);

	kfree(rekey_conf);
	if (IS_ERR(key)) {
		rtw89_err(rtwdev, "ieee80211_gtk_rekey_add failed\n");
		return NULL;
	}

	return key;
}

static void rtw89_wow_update_key_info(struct rtw89_dev *rtwdev, bool rx_ready)
{
	struct ieee80211_vif *wow_vif = rtwdev->wow.wow_vif;
	struct rtw89_vif_link *rtwvif_link = (struct rtw89_vif_link *)wow_vif->drv_priv;
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct rtw89_wow_aoac_report *aoac_rpt = &rtw_wow->aoac_rpt;
	struct rtw89_set_key_info_iter_data data = {.error = false,
						    .rx_ready = rx_ready};
	struct ieee80211_bss_conf *bss_conf;
	struct ieee80211_key_conf *key;

	rcu_read_lock();
	ieee80211_iter_keys_rcu(rtwdev->hw, wow_vif,
				rtw89_wow_set_key_info_iter, &data);
	rcu_read_unlock();

	if (data.error) {
		rtw89_debug(rtwdev, RTW89_DBG_WOW, "%s error\n", __func__);
		return;
	}

	if (!data.gtk_cipher)
		return;

	key = rtw89_wow_gtk_rekey(rtwdev, data.gtk_cipher, aoac_rpt->key_idx,
				  aoac_rpt->gtk);
	if (!key)
		return;

	rtw89_rx_iv_to_pn(rtwdev, key,
			  aoac_rpt->gtk_rx_iv[key->keyidx]);

	if (!data.igtk_cipher)
		return;

	key = rtw89_wow_gtk_rekey(rtwdev, data.igtk_cipher, aoac_rpt->igtk_key_id,
				  aoac_rpt->igtk);
	if (!key)
		return;

	rtw89_rx_pn_set_pmf(rtwdev, key, aoac_rpt->igtk_ipn);

	rcu_read_lock();

	bss_conf = rtw89_vif_rcu_dereference_link(rtwvif_link, true);
	ieee80211_gtk_rekey_notify(wow_vif, bss_conf->bssid,
				   aoac_rpt->eapol_key_replay_count,
				   GFP_ATOMIC);

	rcu_read_unlock();
}

static void rtw89_wow_leave_deep_ps(struct rtw89_dev *rtwdev)
{
	__rtw89_leave_ps_mode(rtwdev);
}

static void rtw89_wow_enter_deep_ps(struct rtw89_dev *rtwdev)
{
	struct ieee80211_vif *wow_vif = rtwdev->wow.wow_vif;
	struct rtw89_vif_link *rtwvif_link = (struct rtw89_vif_link *)wow_vif->drv_priv;

	__rtw89_enter_ps_mode(rtwdev, rtwvif_link);
}

static void rtw89_wow_enter_ps(struct rtw89_dev *rtwdev)
{
	struct ieee80211_vif *wow_vif = rtwdev->wow.wow_vif;
	struct rtw89_vif_link *rtwvif_link = (struct rtw89_vif_link *)wow_vif->drv_priv;

	if (rtw89_wow_mgd_linked(rtwdev))
		rtw89_enter_lps(rtwdev, rtwvif_link, false);
	else if (rtw89_wow_no_link(rtwdev))
		rtw89_fw_h2c_fwips(rtwdev, rtwvif_link, true);
}

static void rtw89_wow_leave_ps(struct rtw89_dev *rtwdev, bool enable_wow)
{
	struct ieee80211_vif *wow_vif = rtwdev->wow.wow_vif;
	struct rtw89_vif_link *rtwvif_link = (struct rtw89_vif_link *)wow_vif->drv_priv;

	if (rtw89_wow_mgd_linked(rtwdev)) {
		rtw89_leave_lps(rtwdev);
	} else if (rtw89_wow_no_link(rtwdev)) {
		if (enable_wow)
			rtw89_leave_ips(rtwdev);
		else
			rtw89_fw_h2c_fwips(rtwdev, rtwvif_link, false);
	}
}

static int rtw89_wow_config_mac(struct rtw89_dev *rtwdev, bool enable_wow)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;

	return mac->wow_config_mac(rtwdev, enable_wow);
}

static void rtw89_wow_set_rx_filter(struct rtw89_dev *rtwdev, bool enable)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	enum rtw89_mac_fwd_target fwd_target = enable ?
					       RTW89_FWD_DONT_CARE :
					       RTW89_FWD_TO_HOST;

	mac->typ_fltr_opt(rtwdev, RTW89_MGNT, fwd_target, RTW89_MAC_0);
	mac->typ_fltr_opt(rtwdev, RTW89_CTRL, fwd_target, RTW89_MAC_0);
	mac->typ_fltr_opt(rtwdev, RTW89_DATA, fwd_target, RTW89_MAC_0);
}

static void rtw89_wow_show_wakeup_reason(struct rtw89_dev *rtwdev)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct rtw89_wow_aoac_report *aoac_rpt = &rtw_wow->aoac_rpt;
	struct cfg80211_wowlan_nd_info nd_info;
	struct cfg80211_wowlan_wakeup wakeup = {
		.pattern_idx = -1,
	};
	u32 wow_reason_reg;
	u8 reason;

	if (RTW89_CHK_FW_FEATURE(WOW_REASON_V1, &rtwdev->fw))
		wow_reason_reg = rtwdev->chip->wow_reason_reg[RTW89_WOW_REASON_V1];
	else
		wow_reason_reg = rtwdev->chip->wow_reason_reg[RTW89_WOW_REASON_V0];

	reason = rtw89_read8(rtwdev, wow_reason_reg);
	switch (reason) {
	case RTW89_WOW_RSN_RX_DEAUTH:
		wakeup.disconnect = true;
		rtw89_debug(rtwdev, RTW89_DBG_WOW, "WOW: Rx deauth\n");
		break;
	case RTW89_WOW_RSN_DISCONNECT:
		wakeup.disconnect = true;
		rtw89_debug(rtwdev, RTW89_DBG_WOW, "WOW: AP is off\n");
		break;
	case RTW89_WOW_RSN_RX_MAGIC_PKT:
		wakeup.magic_pkt = true;
		rtw89_debug(rtwdev, RTW89_DBG_WOW, "WOW: Rx magic packet\n");
		break;
	case RTW89_WOW_RSN_RX_GTK_REKEY:
		wakeup.gtk_rekey_failure = true;
		rtw89_debug(rtwdev, RTW89_DBG_WOW, "WOW: Rx gtk rekey\n");
		break;
	case RTW89_WOW_RSN_RX_PATTERN_MATCH:
		wakeup.pattern_idx = aoac_rpt->pattern_idx;
		rtw89_debug(rtwdev, RTW89_DBG_WOW, "WOW: Rx pattern match packet\n");
		break;
	case RTW89_WOW_RSN_RX_NLO:
		/* Current firmware and driver don't report ssid index.
		 * Use 0 for n_matches based on its comment.
		 */
		nd_info.n_matches = 0;
		wakeup.net_detect = &nd_info;
		rtw89_debug(rtwdev, RTW89_DBG_WOW, "Rx NLO\n");
		break;
	default:
		rtw89_warn(rtwdev, "Unknown wakeup reason %x\n", reason);
		ieee80211_report_wowlan_wakeup(rtwdev->wow.wow_vif, NULL,
					       GFP_KERNEL);
		return;
	}

	ieee80211_report_wowlan_wakeup(rtwdev->wow.wow_vif, &wakeup,
				       GFP_KERNEL);
}

static void rtw89_wow_vif_iter(struct rtw89_dev *rtwdev,
			       struct rtw89_vif_link *rtwvif_link)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct ieee80211_vif *vif = rtwvif_to_vif(rtwvif_link);

	/* Current WoWLAN function support setting of only vif in
	 * infra mode or no link mode. When one suitable vif is found,
	 * stop the iteration.
	 */
	if (rtw_wow->wow_vif || vif->type != NL80211_IFTYPE_STATION)
		return;

	switch (rtwvif_link->net_type) {
	case RTW89_NET_TYPE_INFRA:
		if (rtw_wow_has_mgd_features(rtwdev))
			rtw_wow->wow_vif = vif;
		break;
	case RTW89_NET_TYPE_NO_LINK:
		if (rtw_wow->pno_inited)
			rtw_wow->wow_vif = vif;
		break;
	default:
		break;
	}
}

static u16 __rtw89_cal_crc16(u8 data, u16 crc)
{
	u8 shift_in, data_bit;
	u8 crc_bit4, crc_bit11, crc_bit15;
	u16 crc_result;
	int index;

	for (index = 0; index < 8; index++) {
		crc_bit15 = crc & BIT(15) ? 1 : 0;
		data_bit = data & BIT(index) ? 1 : 0;
		shift_in = crc_bit15 ^ data_bit;

		crc_result = crc << 1;

		if (shift_in == 0)
			crc_result &= ~BIT(0);
		else
			crc_result |= BIT(0);

		crc_bit11 = (crc & BIT(11) ? 1 : 0) ^ shift_in;

		if (crc_bit11 == 0)
			crc_result &= ~BIT(12);
		else
			crc_result |= BIT(12);

		crc_bit4 = (crc & BIT(4) ? 1 : 0) ^ shift_in;

		if (crc_bit4 == 0)
			crc_result &= ~BIT(5);
		else
			crc_result |= BIT(5);

		crc = crc_result;
	}
	return crc;
}

static u16 rtw89_calc_crc(u8 *pdata, int length)
{
	u16 crc = 0xffff;
	int i;

	for (i = 0; i < length; i++)
		crc = __rtw89_cal_crc16(pdata[i], crc);

	/* get 1' complement */
	return ~crc;
}

static int rtw89_wow_pattern_get_type(struct rtw89_vif_link *rtwvif_link,
				      struct rtw89_wow_cam_info *rtw_pattern,
				      const u8 *pattern, u8 da_mask)
{
	u8 da[ETH_ALEN];

	ether_addr_copy_mask(da, pattern, da_mask);

	/* Each pattern is divided into different kinds by DA address
	 *  a. DA is broadcast address: set bc = 0;
	 *  b. DA is multicast address: set mc = 0
	 *  c. DA is unicast address same as dev's mac address: set uc = 0
	 *  d. DA is unmasked. Also called wildcard type: set uc = bc = mc = 0
	 *  e. Others is invalid type.
	 */

	if (is_broadcast_ether_addr(da))
		rtw_pattern->bc = true;
	else if (is_multicast_ether_addr(da))
		rtw_pattern->mc = true;
	else if (ether_addr_equal(da, rtwvif_link->mac_addr) &&
		 da_mask == GENMASK(5, 0))
		rtw_pattern->uc = true;
	else if (!da_mask) /*da_mask == 0 mean wildcard*/
		return 0;
	else
		return -EPERM;

	return 0;
}

static int rtw89_wow_pattern_generate(struct rtw89_dev *rtwdev,
				      struct rtw89_vif_link *rtwvif_link,
				      const struct cfg80211_pkt_pattern *pkt_pattern,
				      struct rtw89_wow_cam_info *rtw_pattern)
{
	u8 mask_hw[RTW89_MAX_PATTERN_MASK_SIZE * 4] = {0};
	u8 content[RTW89_MAX_PATTERN_SIZE] = {0};
	const u8 *mask;
	const u8 *pattern;
	u8 mask_len;
	u16 count;
	u32 len;
	int i, ret;

	pattern = pkt_pattern->pattern;
	len = pkt_pattern->pattern_len;
	mask = pkt_pattern->mask;
	mask_len = DIV_ROUND_UP(len, 8);
	memset(rtw_pattern, 0, sizeof(*rtw_pattern));

	ret = rtw89_wow_pattern_get_type(rtwvif_link, rtw_pattern, pattern,
					 mask[0] & GENMASK(5, 0));
	if (ret)
		return ret;

	/* translate mask from os to mask for hw
	 * pattern from OS uses 'ethenet frame', like this:
	 * |    6   |    6   |   2  |     20    |  Variable  |  4  |
	 * |--------+--------+------+-----------+------------+-----|
	 * |    802.3 Mac Header    | IP Header | TCP Packet | FCS |
	 * |   DA   |   SA   | Type |
	 *
	 * BUT, packet catched by our HW is in '802.11 frame', begin from LLC
	 * |     24 or 30      |    6   |   2  |     20    |  Variable  |  4  |
	 * |-------------------+--------+------+-----------+------------+-----|
	 * | 802.11 MAC Header |       LLC     | IP Header | TCP Packet | FCS |
	 *		       | Others | Tpye |
	 *
	 * Therefore, we need translate mask_from_OS to mask_to_hw.
	 * We should left-shift mask by 6 bits, then set the new bit[0~5] = 0,
	 * because new mask[0~5] means 'SA', but our HW packet begins from LLC,
	 * bit[0~5] corresponds to first 6 Bytes in LLC, they just don't match.
	 */

	/* Shift 6 bits */
	for (i = 0; i < mask_len - 1; i++) {
		mask_hw[i] = u8_get_bits(mask[i], GENMASK(7, 6)) |
			     u8_get_bits(mask[i + 1], GENMASK(5, 0)) << 2;
	}
	mask_hw[i] = u8_get_bits(mask[i], GENMASK(7, 6));

	/* Set bit 0-5 to zero */
	mask_hw[0] &= ~GENMASK(5, 0);

	memcpy(rtw_pattern->mask, mask_hw, sizeof(rtw_pattern->mask));

	/* To get the wake up pattern from the mask.
	 * We do not count first 12 bits which means
	 * DA[6] and SA[6] in the pattern to match HW design.
	 */
	count = 0;
	for (i = 12; i < len; i++) {
		if ((mask[i / 8] >> (i % 8)) & 0x01) {
			content[count] = pattern[i];
			count++;
		}
	}

	rtw_pattern->crc = rtw89_calc_crc(content, count);

	return 0;
}

static int rtw89_wow_parse_patterns(struct rtw89_dev *rtwdev,
				    struct rtw89_vif_link *rtwvif_link,
				    struct cfg80211_wowlan *wowlan)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct rtw89_wow_cam_info *rtw_pattern = rtw_wow->patterns;
	int i;
	int ret;

	if (!wowlan->n_patterns || !wowlan->patterns)
		return 0;

	for (i = 0; i < wowlan->n_patterns; i++) {
		rtw_pattern = &rtw_wow->patterns[i];
		ret = rtw89_wow_pattern_generate(rtwdev, rtwvif_link,
						 &wowlan->patterns[i],
						 rtw_pattern);
		if (ret) {
			rtw89_err(rtwdev, "failed to generate pattern(%d)\n", i);
			rtw_wow->pattern_cnt = 0;
			return ret;
		}

		rtw_pattern->r_w = true;
		rtw_pattern->idx = i;
		rtw_pattern->negative_pattern_match = false;
		rtw_pattern->skip_mac_hdr = true;
		rtw_pattern->valid = true;
	}
	rtw_wow->pattern_cnt = wowlan->n_patterns;

	return 0;
}

static void rtw89_wow_pattern_clear_cam(struct rtw89_dev *rtwdev)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct rtw89_wow_cam_info *rtw_pattern = rtw_wow->patterns;
	int i = 0;

	for (i = 0; i < rtw_wow->pattern_cnt; i++) {
		rtw_pattern = &rtw_wow->patterns[i];
		rtw_pattern->valid = false;
		rtw89_fw_wow_cam_update(rtwdev, rtw_pattern);
	}
}

static void rtw89_wow_pattern_write(struct rtw89_dev *rtwdev)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct rtw89_wow_cam_info *rtw_pattern = rtw_wow->patterns;
	int i;

	for (i = 0; i < rtw_wow->pattern_cnt; i++)
		rtw89_fw_wow_cam_update(rtwdev, rtw_pattern + i);
}

static void rtw89_wow_pattern_clear(struct rtw89_dev *rtwdev)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;

	rtw89_wow_pattern_clear_cam(rtwdev);

	rtw_wow->pattern_cnt = 0;
	memset(rtw_wow->patterns, 0, sizeof(rtw_wow->patterns));
}

static void rtw89_wow_clear_wakeups(struct rtw89_dev *rtwdev)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;

	rtw_wow->wow_vif = NULL;
	rtw89_core_release_all_bits_map(rtw_wow->flags, RTW89_WOW_FLAG_NUM);
	rtw_wow->pattern_cnt = 0;
	rtw_wow->pno_inited = false;
}

static void rtw89_wow_init_pno(struct rtw89_dev *rtwdev,
			       struct cfg80211_sched_scan_request *nd_config)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;

	if (!nd_config->n_match_sets || !nd_config->n_channels)
		return;

	rtw_wow->nd_config = nd_config;
	rtw_wow->pno_inited = true;

	INIT_LIST_HEAD(&rtw_wow->pno_pkt_list);

	rtw89_debug(rtwdev, RTW89_DBG_WOW, "WOW: net-detect is enabled\n");
}

static int rtw89_wow_set_wakeups(struct rtw89_dev *rtwdev,
				 struct cfg80211_wowlan *wowlan)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct rtw89_vif_link *rtwvif_link;

	if (wowlan->disconnect)
		set_bit(RTW89_WOW_FLAG_EN_DISCONNECT, rtw_wow->flags);
	if (wowlan->magic_pkt)
		set_bit(RTW89_WOW_FLAG_EN_MAGIC_PKT, rtw_wow->flags);
	if (wowlan->n_patterns && wowlan->patterns)
		set_bit(RTW89_WOW_FLAG_EN_PATTERN, rtw_wow->flags);

	if (wowlan->nd_config)
		rtw89_wow_init_pno(rtwdev, wowlan->nd_config);

	rtw89_for_each_rtwvif(rtwdev, rtwvif_link)
		rtw89_wow_vif_iter(rtwdev, rtwvif_link);

	if (!rtw_wow->wow_vif)
		return -EPERM;

	rtwvif_link = (struct rtw89_vif_link *)rtw_wow->wow_vif->drv_priv;
	return rtw89_wow_parse_patterns(rtwdev, rtwvif_link, wowlan);
}

static int rtw89_wow_cfg_wake_pno(struct rtw89_dev *rtwdev, bool wow)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct ieee80211_vif *wow_vif = rtw_wow->wow_vif;
	struct rtw89_vif_link *rtwvif_link = (struct rtw89_vif_link *)wow_vif->drv_priv;
	int ret;

	ret = rtw89_fw_h2c_cfg_pno(rtwdev, rtwvif_link, true);
	if (ret) {
		rtw89_err(rtwdev, "failed to config pno\n");
		return ret;
	}

	ret = rtw89_fw_h2c_wow_wakeup_ctrl(rtwdev, rtwvif_link, wow);
	if (ret) {
		rtw89_err(rtwdev, "failed to fw wow wakeup ctrl\n");
		return ret;
	}

	ret = rtw89_fw_h2c_wow_global(rtwdev, rtwvif_link, wow);
	if (ret) {
		rtw89_err(rtwdev, "failed to fw wow global\n");
		return ret;
	}

	return 0;
}

static int rtw89_wow_cfg_wake(struct rtw89_dev *rtwdev, bool wow)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct ieee80211_vif *wow_vif = rtw_wow->wow_vif;
	struct rtw89_vif_link *rtwvif_link = (struct rtw89_vif_link *)wow_vif->drv_priv;
	struct ieee80211_sta *wow_sta;
	struct rtw89_sta_link *rtwsta_link = NULL;
	int ret;

	wow_sta = ieee80211_find_sta(wow_vif, rtwvif_link->bssid);
	if (wow_sta)
		rtwsta_link = (struct rtw89_sta_link *)wow_sta->drv_priv;

	if (wow) {
		if (rtw_wow->pattern_cnt)
			rtwvif_link->wowlan_pattern = true;
		if (test_bit(RTW89_WOW_FLAG_EN_MAGIC_PKT, rtw_wow->flags))
			rtwvif_link->wowlan_magic = true;
	} else {
		rtwvif_link->wowlan_pattern = false;
		rtwvif_link->wowlan_magic = false;
	}

	ret = rtw89_fw_h2c_wow_wakeup_ctrl(rtwdev, rtwvif_link, wow);
	if (ret) {
		rtw89_err(rtwdev, "failed to fw wow wakeup ctrl\n");
		return ret;
	}

	if (wow) {
		ret = rtw89_chip_h2c_dctl_sec_cam(rtwdev, rtwvif_link, rtwsta_link);
		if (ret) {
			rtw89_err(rtwdev, "failed to update dctl cam sec entry: %d\n",
				  ret);
			return ret;
		}
	}

	ret = rtw89_fw_h2c_cam(rtwdev, rtwvif_link, rtwsta_link, NULL);
	if (ret) {
		rtw89_warn(rtwdev, "failed to send h2c cam\n");
		return ret;
	}

	ret = rtw89_fw_h2c_wow_global(rtwdev, rtwvif_link, wow);
	if (ret) {
		rtw89_err(rtwdev, "failed to fw wow global\n");
		return ret;
	}

	return 0;
}

static int rtw89_wow_check_fw_status(struct rtw89_dev *rtwdev, bool wow_enable)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	u8 polling;
	int ret;

	ret = read_poll_timeout_atomic(rtw89_read8_mask, polling,
				       wow_enable == !!polling,
				       50, 50000, false, rtwdev,
				       mac->wow_ctrl.addr, mac->wow_ctrl.mask);
	if (ret)
		rtw89_err(rtwdev, "failed to check wow status %s\n",
			  wow_enable ? "enabled" : "disabled");
	return ret;
}

static int rtw89_wow_swap_fw(struct rtw89_dev *rtwdev, bool wow)
{
	enum rtw89_fw_type fw_type = wow ? RTW89_FW_WOWLAN : RTW89_FW_NORMAL;
	enum rtw89_chip_gen chip_gen = rtwdev->chip->chip_gen;
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct ieee80211_vif *wow_vif = rtw_wow->wow_vif;
	struct rtw89_vif_link *rtwvif_link = (struct rtw89_vif_link *)wow_vif->drv_priv;
	enum rtw89_core_chip_id chip_id = rtwdev->chip->chip_id;
	const struct rtw89_chip_info *chip = rtwdev->chip;
	bool include_bb = !!chip->bbmcu_nr;
	bool disable_intr_for_dlfw = false;
	struct ieee80211_sta *wow_sta;
	struct rtw89_sta_link *rtwsta_link = NULL;
	bool is_conn = true;
	int ret;

	if (chip_id == RTL8852C || chip_id == RTL8922A)
		disable_intr_for_dlfw = true;

	wow_sta = ieee80211_find_sta(wow_vif, rtwvif_link->bssid);
	if (wow_sta)
		rtwsta_link = (struct rtw89_sta_link *)wow_sta->drv_priv;
	else
		is_conn = false;

	if (disable_intr_for_dlfw)
		rtw89_hci_disable_intr(rtwdev);

	ret = rtw89_fw_download(rtwdev, fw_type, include_bb);
	if (ret) {
		rtw89_warn(rtwdev, "download fw failed\n");
		return ret;
	}

	if (disable_intr_for_dlfw)
		rtw89_hci_enable_intr(rtwdev);

	rtw89_phy_init_rf_reg(rtwdev, true);

	ret = rtw89_fw_h2c_role_maintain(rtwdev, rtwvif_link, rtwsta_link,
					 RTW89_ROLE_FW_RESTORE);
	if (ret) {
		rtw89_warn(rtwdev, "failed to send h2c role maintain\n");
		return ret;
	}

	ret = rtw89_chip_h2c_assoc_cmac_tbl(rtwdev, wow_vif, wow_sta);
	if (ret) {
		rtw89_warn(rtwdev, "failed to send h2c assoc cmac tbl\n");
		return ret;
	}

	if (!is_conn)
		rtw89_cam_reset_keys(rtwdev);

	ret = rtw89_fw_h2c_join_info(rtwdev, rtwvif_link, rtwsta_link, !is_conn);
	if (ret) {
		rtw89_warn(rtwdev, "failed to send h2c join info\n");
		return ret;
	}

	ret = rtw89_fw_h2c_cam(rtwdev, rtwvif_link, rtwsta_link, NULL);
	if (ret) {
		rtw89_warn(rtwdev, "failed to send h2c cam\n");
		return ret;
	}

	if (is_conn) {
		ret = rtw89_fw_h2c_general_pkt(rtwdev, rtwvif_link, rtwsta_link->mac_id);
		if (ret) {
			rtw89_warn(rtwdev, "failed to send h2c general packet\n");
			return ret;
		}
		rtw89_phy_ra_assoc(rtwdev, wow_sta);
		rtw89_phy_set_bss_color(rtwdev, rtwvif_link);
		rtw89_chip_cfg_txpwr_ul_tb_offset(rtwdev, rtwvif_link);
	}

	if (chip_gen == RTW89_CHIP_BE)
		rtw89_phy_rfk_pre_ntfy_and_wait(rtwdev, RTW89_PHY_0, 5);

	rtw89_mac_hw_mgnt_sec(rtwdev, wow);

	return 0;
}

static int rtw89_wow_enable_trx_pre(struct rtw89_dev *rtwdev)
{
	int ret;

	rtw89_hci_ctrl_txdma_ch(rtwdev, false);
	rtw89_hci_ctrl_txdma_fw_ch(rtwdev, true);

	rtw89_mac_ptk_drop_by_band_and_wait(rtwdev, RTW89_MAC_0);

	ret = rtw89_hci_poll_txdma_ch_idle(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "txdma ch busy\n");
		return ret;
	}
	rtw89_wow_set_rx_filter(rtwdev, true);

	ret = rtw89_mac_cfg_ppdu_status(rtwdev, RTW89_MAC_0, false);
	if (ret) {
		rtw89_err(rtwdev, "cfg ppdu status\n");
		return ret;
	}

	return 0;
}

static int rtw89_wow_enable_trx_post(struct rtw89_dev *rtwdev)
{
	int ret;

	rtw89_hci_disable_intr(rtwdev);
	rtw89_hci_ctrl_trxhci(rtwdev, false);

	ret = rtw89_hci_poll_txdma_ch_idle(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "failed to poll txdma ch idle pcie\n");
		return ret;
	}

	ret = rtw89_wow_config_mac(rtwdev, true);
	if (ret) {
		rtw89_err(rtwdev, "failed to config mac\n");
		return ret;
	}

	rtw89_wow_set_rx_filter(rtwdev, false);
	rtw89_hci_reset(rtwdev);

	return 0;
}

static int rtw89_wow_disable_trx_pre(struct rtw89_dev *rtwdev)
{
	int ret;

	rtw89_hci_clr_idx_all(rtwdev);

	ret = rtw89_hci_rst_bdram(rtwdev);
	if (ret) {
		rtw89_warn(rtwdev, "reset bdram busy\n");
		return ret;
	}

	rtw89_hci_ctrl_trxhci(rtwdev, true);
	rtw89_hci_ctrl_txdma_ch(rtwdev, true);

	ret = rtw89_wow_config_mac(rtwdev, false);
	if (ret) {
		rtw89_err(rtwdev, "failed to config mac\n");
		return ret;
	}

	/* Before enabling interrupt, we need to get AOAC report by reg due to RX
	 * not enabled yet. Also, we need to sync RX related IV from firmware to
	 * mac80211 before receiving RX packets from driver.
	 * After enabling interrupt, we can get AOAC report from h2c and c2h, and
	 * can get TX IV and complete rekey info. We need to update TX related IV
	 * and new GTK info if rekey happened.
	 */
	ret = rtw89_wow_get_aoac_rpt(rtwdev, false);
	if (!ret)
		rtw89_wow_update_key_info(rtwdev, false);

	rtw89_hci_enable_intr(rtwdev);
	ret = rtw89_wow_get_aoac_rpt(rtwdev, true);
	if (!ret)
		rtw89_wow_update_key_info(rtwdev, true);

	return 0;
}

static int rtw89_wow_disable_trx_post(struct rtw89_dev *rtwdev)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct ieee80211_vif *vif = rtw_wow->wow_vif;
	int ret;

	ret = rtw89_mac_cfg_ppdu_status(rtwdev, RTW89_MAC_0, true);
	if (ret)
		rtw89_err(rtwdev, "cfg ppdu status\n");

	rtw89_fw_h2c_set_bcn_fltr_cfg(rtwdev, vif, true);

	return ret;
}

static void rtw89_fw_release_pno_pkt_list(struct rtw89_dev *rtwdev,
					  struct rtw89_vif_link *rtwvif_link)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct list_head *pkt_list = &rtw_wow->pno_pkt_list;
	struct rtw89_pktofld_info *info, *tmp;

	list_for_each_entry_safe(info, tmp, pkt_list, list) {
		rtw89_fw_h2c_del_pkt_offload(rtwdev, info->id);
		list_del(&info->list);
		kfree(info);
	}
}

static int rtw89_pno_scan_update_probe_req(struct rtw89_dev *rtwdev,
					   struct rtw89_vif_link *rtwvif_link)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct cfg80211_sched_scan_request *nd_config = rtw_wow->nd_config;
	u8 num = nd_config->n_match_sets, i;
	struct rtw89_pktofld_info *info;
	struct sk_buff *skb;
	int ret;

	for (i = 0; i < num; i++) {
		skb = ieee80211_probereq_get(rtwdev->hw, rtwvif_link->mac_addr,
					     nd_config->match_sets[i].ssid.ssid,
					     nd_config->match_sets[i].ssid.ssid_len,
					     nd_config->ie_len);
		if (!skb)
			return -ENOMEM;

		skb_put_data(skb, nd_config->ie, nd_config->ie_len);

		info = kzalloc(sizeof(*info), GFP_KERNEL);
		if (!info) {
			kfree_skb(skb);
			rtw89_fw_release_pno_pkt_list(rtwdev, rtwvif_link);
			return -ENOMEM;
		}

		ret = rtw89_fw_h2c_add_pkt_offload(rtwdev, &info->id, skb);
		if (ret) {
			kfree_skb(skb);
			kfree(info);
			rtw89_fw_release_pno_pkt_list(rtwdev, rtwvif_link);
			return ret;
		}

		list_add_tail(&info->list, &rtw_wow->pno_pkt_list);
		kfree_skb(skb);
	}

	return 0;
}

static int rtw89_pno_scan_offload(struct rtw89_dev *rtwdev, bool enable)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct ieee80211_vif *wow_vif = rtw_wow->wow_vif;
	struct rtw89_vif_link *rtwvif_link = (struct rtw89_vif_link *)wow_vif->drv_priv;
	int interval = rtw_wow->nd_config->scan_plans[0].interval;
	struct rtw89_scan_option opt = {};
	int ret;

	if (enable) {
		ret = rtw89_pno_scan_update_probe_req(rtwdev, rtwvif_link);
		if (ret) {
			rtw89_err(rtwdev, "Update probe request failed\n");
			return ret;
		}

		ret = mac->add_chan_list_pno(rtwdev, rtwvif_link);
		if (ret) {
			rtw89_err(rtwdev, "Update channel list failed\n");
			return ret;
		}
	}

	opt.enable = enable;
	opt.repeat = RTW89_SCAN_NORMAL;
	opt.norm_pd = max(interval, 1) * 10; /* in unit of 100ms */
	opt.delay = max(rtw_wow->nd_config->delay, 1);

	if (rtwdev->chip->chip_gen == RTW89_CHIP_BE) {
		opt.operation = enable ? RTW89_SCAN_OP_START : RTW89_SCAN_OP_STOP;
		opt.scan_mode = RTW89_SCAN_MODE_SA;
		opt.band = RTW89_PHY_0;
		opt.num_macc_role = 0;
		opt.mlo_mode = rtwdev->mlo_dbcc_mode;
		opt.num_opch = 0;
		opt.opch_end = RTW89_CHAN_INVALID;
	}

	mac->scan_offload(rtwdev, &opt, rtwvif_link, true);

	return 0;
}

static int rtw89_wow_fw_start(struct rtw89_dev *rtwdev)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct ieee80211_vif *wow_vif = rtw_wow->wow_vif;
	struct rtw89_vif_link *rtwvif_link = (struct rtw89_vif_link *)wow_vif->drv_priv;
	int ret;

	if (rtw89_wow_no_link(rtwdev)) {
		ret = rtw89_pno_scan_offload(rtwdev, false);
		if (ret) {
			rtw89_err(rtwdev, "wow: failed to disable pno scan offload\n");
			return ret;
		}

		ret = rtw89_pno_scan_offload(rtwdev, true);
		if (ret) {
			rtw89_err(rtwdev, "wow: failed to enable pno scan offload\n");
			return ret;
		}
	} else {
		rtw89_wow_pattern_write(rtwdev);
		rtw89_wow_construct_key_info(rtwdev);

		ret = rtw89_fw_h2c_keep_alive(rtwdev, rtwvif_link, true);
		if (ret) {
			rtw89_err(rtwdev, "wow: failed to enable keep alive\n");
			return ret;
		}

		ret = rtw89_fw_h2c_disconnect_detect(rtwdev, rtwvif_link, true);
		if (ret) {
			rtw89_err(rtwdev, "wow: failed to enable disconnect detect\n");
			return ret;
		}

		ret = rtw89_fw_h2c_wow_gtk_ofld(rtwdev, rtwvif_link, true);
		if (ret) {
			rtw89_err(rtwdev, "wow: failed to enable GTK offload\n");
			return ret;
		}

		ret = rtw89_fw_h2c_arp_offload(rtwdev, rtwvif_link, true);
		if (ret)
			rtw89_warn(rtwdev, "wow: failed to enable arp offload\n");
	}

	if (rtw89_wow_no_link(rtwdev)) {
		ret = rtw89_wow_cfg_wake_pno(rtwdev, true);
		if (ret) {
			rtw89_err(rtwdev, "wow: failed to config wake PNO\n");
			return ret;
		}
	} else {
		ret = rtw89_wow_cfg_wake(rtwdev, true);
		if (ret) {
			rtw89_err(rtwdev, "wow: failed to config wake\n");
			return ret;
		}
	}

	ret = rtw89_wow_check_fw_status(rtwdev, true);
	if (ret) {
		rtw89_err(rtwdev, "wow: failed to check enable fw ready\n");
		return ret;
	}

	return 0;
}

static int rtw89_wow_fw_stop(struct rtw89_dev *rtwdev)
{
	struct rtw89_wow_param *rtw_wow = &rtwdev->wow;
	struct ieee80211_vif *wow_vif = rtw_wow->wow_vif;
	struct rtw89_vif_link *rtwvif_link = (struct rtw89_vif_link *)wow_vif->drv_priv;
	int ret;

	if (rtw89_wow_no_link(rtwdev)) {
		ret = rtw89_pno_scan_offload(rtwdev, false);
		if (ret) {
			rtw89_err(rtwdev, "wow: failed to disable pno scan offload\n");
			return ret;
		}

		ret = rtw89_fw_h2c_cfg_pno(rtwdev, rtwvif_link, false);
		if (ret) {
			rtw89_err(rtwdev, "wow: failed to disable pno\n");
			return ret;
		}

		rtw89_fw_release_pno_pkt_list(rtwdev, rtwvif_link);
	} else {
		rtw89_wow_pattern_clear(rtwdev);

		ret = rtw89_fw_h2c_keep_alive(rtwdev, rtwvif_link, false);
		if (ret) {
			rtw89_err(rtwdev, "wow: failed to disable keep alive\n");
			return ret;
		}

		ret = rtw89_fw_h2c_disconnect_detect(rtwdev, rtwvif_link, false);
		if (ret) {
			rtw89_err(rtwdev, "wow: failed to disable disconnect detect\n");
			return ret;
		}

		ret = rtw89_fw_h2c_wow_gtk_ofld(rtwdev, rtwvif_link, false);
		if (ret) {
			rtw89_err(rtwdev, "wow: failed to disable GTK offload\n");
			return ret;
		}

		ret = rtw89_fw_h2c_arp_offload(rtwdev, rtwvif_link, false);
		if (ret)
			rtw89_warn(rtwdev, "wow: failed to disable arp offload\n");

		rtw89_wow_key_clear(rtwdev);
		rtw89_fw_release_general_pkt_list(rtwdev, true);
	}


	ret = rtw89_wow_cfg_wake(rtwdev, false);
	if (ret) {
		rtw89_err(rtwdev, "wow: failed to disable config wake\n");
		return ret;
	}

	ret = rtw89_wow_check_fw_status(rtwdev, false);
	if (ret) {
		rtw89_err(rtwdev, "wow: failed to check disable fw ready\n");
		return ret;
	}

	return 0;
}

static int rtw89_wow_enable(struct rtw89_dev *rtwdev)
{
	int ret;

	set_bit(RTW89_FLAG_WOWLAN, rtwdev->flags);

	ret = rtw89_wow_enable_trx_pre(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "wow: failed to enable trx_pre\n");
		goto out;
	}

	rtw89_fw_release_general_pkt_list(rtwdev, true);

	ret = rtw89_wow_swap_fw(rtwdev, true);
	if (ret) {
		rtw89_err(rtwdev, "wow: failed to swap to wow fw\n");
		goto out;
	}

	ret = rtw89_wow_fw_start(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "wow: failed to let wow fw start\n");
		goto out;
	}

	rtw89_wow_enter_ps(rtwdev);

	ret = rtw89_wow_enable_trx_post(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "wow: failed to enable trx_post\n");
		goto out;
	}

	return 0;

out:
	clear_bit(RTW89_FLAG_WOWLAN, rtwdev->flags);
	return ret;
}

static int rtw89_wow_disable(struct rtw89_dev *rtwdev)
{
	int ret;

	ret = rtw89_wow_disable_trx_pre(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "wow: failed to disable trx_pre\n");
		goto out;
	}

	rtw89_wow_leave_ps(rtwdev, false);

	ret = rtw89_wow_fw_stop(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "wow: failed to swap to normal fw\n");
		goto out;
	}

	ret = rtw89_wow_swap_fw(rtwdev, false);
	if (ret) {
		rtw89_err(rtwdev, "wow: failed to disable trx_post\n");
		goto out;
	}

	ret = rtw89_wow_disable_trx_post(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "wow: failed to disable trx_pre\n");
		goto out;
	}

out:
	clear_bit(RTW89_FLAG_WOWLAN, rtwdev->flags);
	return ret;
}

static void rtw89_wow_restore_ps(struct rtw89_dev *rtwdev)
{
	if (rtw89_wow_no_link(rtwdev))
		rtw89_enter_ips(rtwdev);
}

int rtw89_wow_resume(struct rtw89_dev *rtwdev)
{
	int ret;

	if (!test_bit(RTW89_FLAG_WOWLAN, rtwdev->flags)) {
		rtw89_err(rtwdev, "wow is not enabled\n");
		ret = -EPERM;
		goto out;
	}

	if (!rtw89_mac_get_power_state(rtwdev)) {
		rtw89_err(rtwdev, "chip is no power when resume\n");
		ret = -EPERM;
		goto out;
	}

	rtw89_wow_leave_deep_ps(rtwdev);

	rtw89_wow_show_wakeup_reason(rtwdev);

	ret = rtw89_wow_disable(rtwdev);
	if (ret)
		rtw89_err(rtwdev, "failed to disable wow\n");

	rtw89_wow_restore_ps(rtwdev);
out:
	rtw89_wow_clear_wakeups(rtwdev);
	return ret;
}

int rtw89_wow_suspend(struct rtw89_dev *rtwdev, struct cfg80211_wowlan *wowlan)
{
	int ret;

	ret = rtw89_wow_set_wakeups(rtwdev, wowlan);
	if (ret) {
		rtw89_err(rtwdev, "failed to set wakeup event\n");
		return ret;
	}

	rtw89_wow_leave_ps(rtwdev, true);

	ret = rtw89_wow_enable(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "failed to enable wow\n");
		return ret;
	}

	rtw89_wow_enter_deep_ps(rtwdev);

	return 0;
}
