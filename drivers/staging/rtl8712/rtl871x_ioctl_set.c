/******************************************************************************
 * rtl871x_ioctl_set.c
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
 * WLAN FAE <wlanfae@realtek.com>
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/

#define _RTL871X_IOCTL_SET_C_

#include "osdep_service.h"
#include "drv_types.h"
#include "rtl871x_ioctl_set.h"
#include "usb_osintf.h"
#include "usb_ops.h"

#define IS_MAC_ADDRESS_BROADCAST(addr) \
( \
	((addr[0] == 0xff) && (addr[1] == 0xff) && \
	 (addr[2] == 0xff) && (addr[3] == 0xff) && \
	 (addr[4] == 0xff) && (addr[5] == 0xff)) ? true : false \
)

static u8 validate_ssid(struct ndis_802_11_ssid *ssid)
{
	u8 i;

	if (ssid->SsidLength > 32)
		return false;
	for (i = 0; i < ssid->SsidLength; i++) {
		/* wifi, printable ascii code must be supported */
		if (!((ssid->Ssid[i] >= 0x20) && (ssid->Ssid[i] <= 0x7e)))
			return false;
	}
	return true;
}

static u8 do_join(struct _adapter *padapter)
{
	struct list_head *plist, *phead;
	u8 *pibss = NULL;
	struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	struct  __queue	*queue	= &(pmlmepriv->scanned_queue);

	phead = &queue->queue;
	plist = phead->next;
	pmlmepriv->cur_network.join_res = -2;
	pmlmepriv->fw_state |= _FW_UNDER_LINKING;
	pmlmepriv->pscanned = plist;
	pmlmepriv->to_join = true;

	/* adhoc mode will start with an empty queue, but skip checking */
	if (!check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) &&
	    list_empty(&queue->queue)) {
		if (pmlmepriv->fw_state & _FW_UNDER_LINKING)
			pmlmepriv->fw_state ^= _FW_UNDER_LINKING;
		/* when set_ssid/set_bssid for do_join(), but scanning queue
		 * is empty we try to issue sitesurvey firstly
		 */
		if (!pmlmepriv->sitesurveyctrl.traffic_busy)
			r8712_sitesurvey_cmd(padapter, &pmlmepriv->assoc_ssid);
		return true;
	} else {
		int ret;

		ret = r8712_select_and_join_from_scan(pmlmepriv);
		if (ret == _SUCCESS)
			_set_timer(&pmlmepriv->assoc_timer, MAX_JOIN_TIMEOUT);
		else {
			if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE)) {
				/* submit r8712_createbss_cmd to change to an
				 * ADHOC_MASTER pmlmepriv->lock has been
				 * acquired by caller...
				 */
				struct wlan_bssid_ex *pdev_network =
					&(padapter->registrypriv.dev_network);
				pmlmepriv->fw_state = WIFI_ADHOC_MASTER_STATE;
				pibss = padapter->registrypriv.dev_network.
					MacAddress;
				memcpy(&pdev_network->Ssid,
					&pmlmepriv->assoc_ssid,
					sizeof(struct ndis_802_11_ssid));
				r8712_update_registrypriv_dev_network(padapter);
				r8712_generate_random_ibss(pibss);
				if (r8712_createbss_cmd(padapter) != _SUCCESS)
					return false;
				pmlmepriv->to_join = false;
			} else {
				/* can't associate ; reset under-linking */
				if (pmlmepriv->fw_state & _FW_UNDER_LINKING)
					pmlmepriv->fw_state ^=
							     _FW_UNDER_LINKING;
				/* when set_ssid/set_bssid for do_join(), but
				 * there are no desired bss in scanning queue
				 * we try to issue sitesurvey first
				 */
				if (!pmlmepriv->sitesurveyctrl.traffic_busy)
					r8712_sitesurvey_cmd(padapter,
						       &pmlmepriv->assoc_ssid);
			}
		}
	}
	return true;
}

u8 r8712_set_802_11_bssid(struct _adapter *padapter, u8 *bssid)
{
	unsigned long irqL;
	u8 status = true;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	if (is_zero_ether_addr(bssid) || is_broadcast_ether_addr(bssid)) {
		status = false;
		return status;
	}
	spin_lock_irqsave(&pmlmepriv->lock, irqL);
	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY |
	    _FW_UNDER_LINKING) == true) {
		status = check_fwstate(pmlmepriv, _FW_UNDER_LINKING);
		goto _Abort_Set_BSSID;
	}
	if (check_fwstate(pmlmepriv,
	    _FW_LINKED|WIFI_ADHOC_MASTER_STATE) == true) {
		if (!memcmp(&pmlmepriv->cur_network.network.MacAddress, bssid,
		    ETH_ALEN)) {
			if (!check_fwstate(pmlmepriv, WIFI_STATION_STATE))
				goto _Abort_Set_BSSID; /* driver is in
						* WIFI_ADHOC_MASTER_STATE */
		} else {
			r8712_disassoc_cmd(padapter);
			if (check_fwstate(pmlmepriv, _FW_LINKED) == true)
				r8712_ind_disconnect(padapter);
			r8712_free_assoc_resources(padapter);
			if ((check_fwstate(pmlmepriv,
			     WIFI_ADHOC_MASTER_STATE))) {
				_clr_fwstate_(pmlmepriv,
					      WIFI_ADHOC_MASTER_STATE);
				set_fwstate(pmlmepriv, WIFI_ADHOC_STATE);
			}
		}
	}
	memcpy(&pmlmepriv->assoc_bssid, bssid, ETH_ALEN);
	pmlmepriv->assoc_by_bssid = true;
	status = do_join(padapter);
	goto done;
_Abort_Set_BSSID:
done:
	spin_unlock_irqrestore(&pmlmepriv->lock, irqL);
	return status;
}

void r8712_set_802_11_ssid(struct _adapter *padapter,
			   struct ndis_802_11_ssid *ssid)
{
	unsigned long irqL;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wlan_network *pnetwork = &pmlmepriv->cur_network;

	if (!padapter->hw_init_completed)
		return;
	spin_lock_irqsave(&pmlmepriv->lock, irqL);
	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY|_FW_UNDER_LINKING)) {
		check_fwstate(pmlmepriv, _FW_UNDER_LINKING);
		goto _Abort_Set_SSID;
	}
	if (check_fwstate(pmlmepriv, _FW_LINKED|WIFI_ADHOC_MASTER_STATE)) {
		if ((pmlmepriv->assoc_ssid.SsidLength == ssid->SsidLength) &&
		    (!memcmp(&pmlmepriv->assoc_ssid.Ssid, ssid->Ssid,
		    ssid->SsidLength))) {
			if (!check_fwstate(pmlmepriv, WIFI_STATION_STATE)) {
				if (!r8712_is_same_ibss(padapter,
				     pnetwork)) {
					/* if in WIFI_ADHOC_MASTER_STATE or
					 *  WIFI_ADHOC_STATE, create bss or
					 * rejoin again
					 */
					r8712_disassoc_cmd(padapter);
					if (check_fwstate(pmlmepriv,
					    _FW_LINKED) == true)
						r8712_ind_disconnect(padapter);
					r8712_free_assoc_resources(padapter);
					if (check_fwstate(pmlmepriv,
					     WIFI_ADHOC_MASTER_STATE)) {
						_clr_fwstate_(pmlmepriv,
						    WIFI_ADHOC_MASTER_STATE);
						set_fwstate(pmlmepriv,
							    WIFI_ADHOC_STATE);
					}
				} else
					goto _Abort_Set_SSID; /* driver is in
						  * WIFI_ADHOC_MASTER_STATE */
			}
		} else {
			r8712_disassoc_cmd(padapter);
			if (check_fwstate(pmlmepriv, _FW_LINKED) == true)
				r8712_ind_disconnect(padapter);
			r8712_free_assoc_resources(padapter);
			if (check_fwstate(pmlmepriv,
			    WIFI_ADHOC_MASTER_STATE) == true) {
				_clr_fwstate_(pmlmepriv,
					      WIFI_ADHOC_MASTER_STATE);
				set_fwstate(pmlmepriv, WIFI_ADHOC_STATE);
			}
		}
	}
	if (padapter->securitypriv.btkip_countermeasure == true)
		goto _Abort_Set_SSID;
	if (!validate_ssid(ssid))
		goto _Abort_Set_SSID;
	memcpy(&pmlmepriv->assoc_ssid, ssid, sizeof(struct ndis_802_11_ssid));
	pmlmepriv->assoc_by_bssid = false;
	do_join(padapter);
	goto done;
_Abort_Set_SSID:
done:
	spin_unlock_irqrestore(&pmlmepriv->lock, irqL);
}

void r8712_set_802_11_infrastructure_mode(struct _adapter *padapter,
	enum NDIS_802_11_NETWORK_INFRASTRUCTURE networktype)
{
	unsigned long irqL;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct wlan_network	*cur_network = &pmlmepriv->cur_network;
	enum NDIS_802_11_NETWORK_INFRASTRUCTURE *pold_state =
				&(cur_network->network.InfrastructureMode);

	if (*pold_state != networktype) {
		spin_lock_irqsave(&pmlmepriv->lock, irqL);
		if ((check_fwstate(pmlmepriv, _FW_LINKED) == true) ||
		    (*pold_state == Ndis802_11IBSS))
			r8712_disassoc_cmd(padapter);
		if (check_fwstate(pmlmepriv,
		    _FW_LINKED|WIFI_ADHOC_MASTER_STATE) == true)
			r8712_free_assoc_resources(padapter);
		if ((check_fwstate(pmlmepriv, _FW_LINKED) == true) ||
		    (*pold_state == Ndis802_11Infrastructure) ||
		    (*pold_state == Ndis802_11IBSS)) {
			/* will clr Linked_state before this function,
			 * we must have checked whether issue dis-assoc_cmd or
			 * not */
			r8712_ind_disconnect(padapter);
		}
		*pold_state = networktype;
		/* clear WIFI_STATION_STATE; WIFI_AP_STATE; WIFI_ADHOC_STATE;
		 * WIFI_ADHOC_MASTER_STATE */
		_clr_fwstate_(pmlmepriv, WIFI_STATION_STATE | WIFI_AP_STATE |
			      WIFI_ADHOC_STATE | WIFI_ADHOC_MASTER_STATE |
			      WIFI_AP_STATE);
		switch (networktype) {
		case Ndis802_11IBSS:
			set_fwstate(pmlmepriv, WIFI_ADHOC_STATE);
			break;
		case Ndis802_11Infrastructure:
			set_fwstate(pmlmepriv, WIFI_STATION_STATE);
			break;
		case Ndis802_11APMode:
			set_fwstate(pmlmepriv, WIFI_AP_STATE);
			break;
		case Ndis802_11AutoUnknown:
		case Ndis802_11InfrastructureMax:
			break;
		}
		spin_unlock_irqrestore(&pmlmepriv->lock, irqL);
	}
}

u8 r8712_set_802_11_disassociate(struct _adapter *padapter)
{
	unsigned long irqL;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	spin_lock_irqsave(&pmlmepriv->lock, irqL);
	if (check_fwstate(pmlmepriv, _FW_LINKED) == true) {
		r8712_disassoc_cmd(padapter);
		r8712_ind_disconnect(padapter);
		r8712_free_assoc_resources(padapter);
	}
	spin_unlock_irqrestore(&pmlmepriv->lock, irqL);
	return true;
}

u8 r8712_set_802_11_bssid_list_scan(struct _adapter *padapter)
{
	struct mlme_priv *pmlmepriv = NULL;
	unsigned long irqL;
	u8 ret = true;

	if (!padapter)
		return false;
	pmlmepriv = &padapter->mlmepriv;
	if (!padapter->hw_init_completed)
		return false;
	spin_lock_irqsave(&pmlmepriv->lock, irqL);
	if ((check_fwstate(pmlmepriv, _FW_UNDER_SURVEY|_FW_UNDER_LINKING)) ||
	    (pmlmepriv->sitesurveyctrl.traffic_busy == true)) {
		/* Scan or linking is in progress, do nothing. */
		ret = (u8)check_fwstate(pmlmepriv, _FW_UNDER_SURVEY);
	} else {
		r8712_free_network_queue(padapter);
		ret = r8712_sitesurvey_cmd(padapter, NULL);
	}
	spin_unlock_irqrestore(&pmlmepriv->lock, irqL);
	return ret;
}

u8 r8712_set_802_11_authentication_mode(struct _adapter *padapter,
				enum NDIS_802_11_AUTHENTICATION_MODE authmode)
{
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	u8 ret;

	psecuritypriv->ndisauthtype = authmode;
	if (psecuritypriv->ndisauthtype > 3)
		psecuritypriv->AuthAlgrthm = 2; /* 802.1x */
	if (r8712_set_auth(padapter, psecuritypriv) == _SUCCESS)
		ret = true;
	else
		ret = false;
	return ret;
}

u8 r8712_set_802_11_add_wep(struct _adapter *padapter,
			    struct NDIS_802_11_WEP *wep)
{
	sint	keyid;
	struct security_priv *psecuritypriv = &padapter->securitypriv;

	keyid = wep->KeyIndex & 0x3fffffff;
	if (keyid >= WEP_KEYS)
		return false;
	switch (wep->KeyLength) {
	case 5:
		psecuritypriv->PrivacyAlgrthm = _WEP40_;
		break;
	case 13:
		psecuritypriv->PrivacyAlgrthm = _WEP104_;
		break;
	default:
		psecuritypriv->PrivacyAlgrthm = _NO_PRIVACY_;
		break;
	}
	memcpy(psecuritypriv->DefKey[keyid].skey, &wep->KeyMaterial,
		wep->KeyLength);
	psecuritypriv->DefKeylen[keyid] = wep->KeyLength;
	psecuritypriv->PrivacyKeyIndex = keyid;
	if (r8712_set_key(padapter, psecuritypriv, keyid) == _FAIL)
		return false;
	return _SUCCESS;
}
