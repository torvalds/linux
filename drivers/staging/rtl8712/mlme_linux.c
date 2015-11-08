/******************************************************************************
 * mlme_linux.c
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
 * Linux device driver for RTL8192SU
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
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>.
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/

#define _MLME_OSDEP_C_

#include "osdep_service.h"
#include "drv_types.h"
#include "mlme_osdep.h"

static void sitesurvey_ctrl_handler(unsigned long data)
{
	struct _adapter *adapter = (struct _adapter *)data;

	_r8712_sitesurvey_ctrl_handler(adapter);
	mod_timer(&adapter->mlmepriv.sitesurveyctrl.sitesurvey_ctrl_timer,
		  jiffies + msecs_to_jiffies(3000));
}

static void join_timeout_handler (unsigned long data)
{
	struct _adapter *adapter = (struct _adapter *)data;

	_r8712_join_timeout_handler(adapter);
}

static void _scan_timeout_handler (unsigned long data)
{
	struct _adapter *adapter = (struct _adapter *)data;

	r8712_scan_timeout_handler(adapter);
}

static void dhcp_timeout_handler (unsigned long data)
{
	struct _adapter *adapter = (struct _adapter *)data;

	_r8712_dhcp_timeout_handler(adapter);
}

static void wdg_timeout_handler (unsigned long data)
{
	struct _adapter *adapter = (struct _adapter *)data;

	_r8712_wdg_timeout_handler(adapter);

	mod_timer(&adapter->mlmepriv.wdg_timer,
		  jiffies + msecs_to_jiffies(2000));
}

void r8712_init_mlme_timer(struct _adapter *padapter)
{
	struct	mlme_priv *pmlmepriv = &padapter->mlmepriv;

	setup_timer(&pmlmepriv->assoc_timer, join_timeout_handler,
		    (unsigned long)padapter);
	setup_timer(&pmlmepriv->sitesurveyctrl.sitesurvey_ctrl_timer,
		    sitesurvey_ctrl_handler,
		    (unsigned long)padapter);
	setup_timer(&pmlmepriv->scan_to_timer, _scan_timeout_handler,
		    (unsigned long)padapter);
	setup_timer(&pmlmepriv->dhcp_timer, dhcp_timeout_handler,
		    (unsigned long)padapter);
	setup_timer(&pmlmepriv->wdg_timer, wdg_timeout_handler,
		    (unsigned long)padapter);
}

void r8712_os_indicate_connect(struct _adapter *adapter)
{
	r8712_indicate_wx_assoc_event(adapter);
	netif_carrier_on(adapter->pnetdev);
}

static struct RT_PMKID_LIST   backupPMKIDList[NUM_PMKID_CACHE];
void r8712_os_indicate_disconnect(struct _adapter *adapter)
{
	u8 backupPMKIDIndex = 0;
	u8 backupTKIPCountermeasure = 0x00;

	r8712_indicate_wx_disassoc_event(adapter);
	netif_carrier_off(adapter->pnetdev);
	if (adapter->securitypriv.AuthAlgrthm == 2) { /*/802.1x*/
		/* We have to backup the PMK information for WiFi PMK Caching
		 * test item. Backup the btkip_countermeasure information.
		 * When the countermeasure is trigger, the driver have to
		 * disconnect with AP for 60 seconds.
		 */

		memcpy(&backupPMKIDList[0], &adapter->securitypriv.
			PMKIDList[0], sizeof(struct RT_PMKID_LIST) *
			NUM_PMKID_CACHE);
		backupPMKIDIndex = adapter->securitypriv.PMKIDIndex;
		backupTKIPCountermeasure = adapter->securitypriv.
					   btkip_countermeasure;
		memset((unsigned char *)&adapter->securitypriv, 0,
			 sizeof(struct security_priv));
		setup_timer(&adapter->securitypriv.tkip_timer,
			    r8712_use_tkipkey_handler,
			    (unsigned long)adapter);
		/* Restore the PMK information to securitypriv structure
		 * for the following connection.
		 */
		memcpy(&adapter->securitypriv.PMKIDList[0],
			&backupPMKIDList[0],
			sizeof(struct RT_PMKID_LIST) * NUM_PMKID_CACHE);
		adapter->securitypriv.PMKIDIndex = backupPMKIDIndex;
		adapter->securitypriv.btkip_countermeasure =
					 backupTKIPCountermeasure;
	} else { /*reset values in securitypriv*/
		struct security_priv *psec_priv = &adapter->securitypriv;

		psec_priv->AuthAlgrthm = 0; /*open system*/
		psec_priv->PrivacyAlgrthm = _NO_PRIVACY_;
		psec_priv->PrivacyKeyIndex = 0;
		psec_priv->XGrpPrivacy = _NO_PRIVACY_;
		psec_priv->XGrpKeyid = 1;
		psec_priv->ndisauthtype = Ndis802_11AuthModeOpen;
		psec_priv->ndisencryptstatus = Ndis802_11WEPDisabled;
		psec_priv->wps_phase = false;
	}
}

void r8712_report_sec_ie(struct _adapter *adapter, u8 authmode, u8 *sec_ie)
{
	uint len;
	u8 *buff, *p, i;
	union iwreq_data wrqu;

	buff = NULL;
	if (authmode == _WPA_IE_ID_) {
		buff = kzalloc(IW_CUSTOM_MAX, GFP_ATOMIC);
		if (buff == NULL)
			return;
		p = buff;
		p += sprintf(p, "ASSOCINFO(ReqIEs=");
		len = sec_ie[1] + 2;
		len =  (len < IW_CUSTOM_MAX) ? len : IW_CUSTOM_MAX - 1;
		for (i = 0; i < len; i++)
			p += sprintf(p, "%02x", sec_ie[i]);
		p += sprintf(p, ")");
		memset(&wrqu, 0, sizeof(wrqu));
		wrqu.data.length = p - buff;
		wrqu.data.length = (wrqu.data.length < IW_CUSTOM_MAX) ?
				   wrqu.data.length : IW_CUSTOM_MAX;
		wireless_send_event(adapter->pnetdev, IWEVCUSTOM, &wrqu, buff);
		kfree(buff);
	}
}
