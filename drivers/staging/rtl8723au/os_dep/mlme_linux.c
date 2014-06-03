/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/

#define _MLME_OSDEP_C_

#include <osdep_service.h>
#include <drv_types.h>
#include <mlme_osdep.h>
#include <rtw_ioctl_set.h>

static struct rt_pmkid_list backupPMKIDList[NUM_PMKID_CACHE];

void rtw_reset_securitypriv23a(struct rtw_adapter *adapter)
{
	u8	backupPMKIDIndex = 0;
	u8	backupTKIPCountermeasure = 0x00;
	unsigned long backupTKIPcountermeasure_time = 0;

	if (adapter->securitypriv.dot11AuthAlgrthm ==
	    dot11AuthAlgrthm_8021X) { /* 802.1x */
		/*  We have to backup the PMK information for WiFi PMK
		 *  Caching test item.
		 *  Backup the btkip_countermeasure information.
		 *  When the countermeasure is trigger, the driver have to
		 *  disconnect with AP for 60 seconds.
		 */
		memcpy(&backupPMKIDList[0], &adapter->securitypriv.PMKIDList[0],
		       sizeof(struct rt_pmkid_list) * NUM_PMKID_CACHE);
		backupPMKIDIndex = adapter->securitypriv.PMKIDIndex;
		backupTKIPCountermeasure = adapter->securitypriv.btkip_countermeasure;
		backupTKIPcountermeasure_time = adapter->securitypriv.btkip_countermeasure_time;

		memset((unsigned char *)&adapter->securitypriv, 0,
		       sizeof (struct security_priv));
		/* Restore the PMK information to securitypriv structure
		 * for the following connection.
		 */
		memcpy(&adapter->securitypriv.PMKIDList[0], &backupPMKIDList[0],
		       sizeof(struct rt_pmkid_list) * NUM_PMKID_CACHE);
		adapter->securitypriv.PMKIDIndex = backupPMKIDIndex;
		adapter->securitypriv.btkip_countermeasure = backupTKIPCountermeasure;
		adapter->securitypriv.btkip_countermeasure_time = backupTKIPcountermeasure_time;

		adapter->securitypriv.ndisauthtype = Ndis802_11AuthModeOpen;
		adapter->securitypriv.ndisencryptstatus = Ndis802_11WEPDisabled;
	} else {  /* reset values in securitypriv */
		struct security_priv *psec_priv = &adapter->securitypriv;

		/* open system */
		psec_priv->dot11AuthAlgrthm = dot11AuthAlgrthm_Open;
		psec_priv->dot11PrivacyAlgrthm = 0;
		psec_priv->dot11PrivacyKeyIndex = 0;

		psec_priv->dot118021XGrpPrivacy = 0;
		psec_priv->dot118021XGrpKeyid = 1;

		psec_priv->ndisauthtype = Ndis802_11AuthModeOpen;
		psec_priv->ndisencryptstatus = Ndis802_11WEPDisabled;
	}
}

void rtw_os_indicate_disconnect23a(struct rtw_adapter *adapter)
{
	/* Do it first for tx broadcast pkt after disconnection issue! */
	netif_carrier_off(adapter->pnetdev);

	rtw_cfg80211_indicate_disconnect(adapter);

	rtw_reset_securitypriv23a(adapter);
}
