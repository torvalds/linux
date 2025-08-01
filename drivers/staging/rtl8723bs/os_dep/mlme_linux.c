// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#include <drv_types.h>

static struct rt_pmkid_list   backupPMKIDList[NUM_PMKID_CACHE];
void rtw_reset_securitypriv(struct adapter *adapter)
{
	u8 backupPMKIDIndex = 0;
	u8 backupTKIPCountermeasure = 0x00;
	u32 backupTKIPcountermeasure_time = 0;
	/*  add for CONFIG_IEEE80211W, none 11w also can use */
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;

	spin_lock_bh(&adapter->security_key_mutex);

	if (adapter->securitypriv.dot11AuthAlgrthm == dot11AuthAlgrthm_8021X) {
		/* 802.1x */
		/*  Added by Albert 2009/02/18 */
		/*  We have to backup the PMK information for WiFi PMK Caching test item. */
		/*  */
		/*  Backup the btkip_countermeasure information. */
		/*  When the countermeasure is trigger, the driver have to disconnect with AP for 60 seconds. */

		memcpy(&backupPMKIDList[0], &adapter->securitypriv.PMKIDList[0], sizeof(struct rt_pmkid_list) * NUM_PMKID_CACHE);
		backupPMKIDIndex = adapter->securitypriv.PMKIDIndex;
		backupTKIPCountermeasure = adapter->securitypriv.btkip_countermeasure;
		backupTKIPcountermeasure_time = adapter->securitypriv.btkip_countermeasure_time;

		/* reset RX BIP packet number */
		pmlmeext->mgnt_80211w_IPN_rx = 0;

		memset((unsigned char *)&adapter->securitypriv, 0, sizeof(struct security_priv));

		/*  Added by Albert 2009/02/18 */
		/*  Restore the PMK information to securitypriv structure for the following connection. */
		memcpy(&adapter->securitypriv.PMKIDList[0], &backupPMKIDList[0], sizeof(struct rt_pmkid_list) * NUM_PMKID_CACHE);
		adapter->securitypriv.PMKIDIndex = backupPMKIDIndex;
		adapter->securitypriv.btkip_countermeasure = backupTKIPCountermeasure;
		adapter->securitypriv.btkip_countermeasure_time = backupTKIPcountermeasure_time;

		adapter->securitypriv.ndisauthtype = Ndis802_11AuthModeOpen;
		adapter->securitypriv.ndisencryptstatus = Ndis802_11WEPDisabled;

	} else {
		/* reset values in securitypriv */
		/* if (adapter->mlmepriv.fw_state & WIFI_STATION_STATE) */
		/*  */
		struct security_priv *psec_priv = &adapter->securitypriv;

		psec_priv->dot11AuthAlgrthm = dot11AuthAlgrthm_Open;  /* open system */
		psec_priv->dot11PrivacyAlgrthm = _NO_PRIVACY_;
		psec_priv->dot11PrivacyKeyIndex = 0;

		psec_priv->dot118021XGrpPrivacy = _NO_PRIVACY_;
		psec_priv->dot118021XGrpKeyid = 1;

		psec_priv->ndisauthtype = Ndis802_11AuthModeOpen;
		psec_priv->ndisencryptstatus = Ndis802_11WEPDisabled;
		/*  */
	}
	/*  add for CONFIG_IEEE80211W, none 11w also can use */
	spin_unlock_bh(&adapter->security_key_mutex);
}
