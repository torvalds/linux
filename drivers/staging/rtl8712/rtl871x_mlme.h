/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
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
#ifndef __RTL871X_MLME_H_
#define __RTL871X_MLME_H_

#include "osdep_service.h"
#include "drv_types.h"
#include "wlan_bssdef.h"

#define	MAX_BSS_CNT	64
#define   MAX_JOIN_TIMEOUT	6000

#define		SCANNING_TIMEOUT	4500

#define	SCANQUEUE_LIFETIME 20 /* unit:sec */

#define		WIFI_NULL_STATE	0x00000000
#define	WIFI_ASOC_STATE		0x00000001	/* Under Linked state...*/
#define		WIFI_REASOC_STATE 0x00000002
#define	WIFI_SLEEP_STATE	0x00000004
#define	WIFI_STATION_STATE	0x00000008
#define	WIFI_AP_STATE		0x00000010
#define	WIFI_ADHOC_STATE	0x00000020
#define   WIFI_ADHOC_MASTER_STATE 0x00000040
#define   WIFI_UNDER_LINKING	0x00000080
#define WIFI_SITE_MONITOR	0x00000800	/* to indicate the station
						 * is under site surveying
						 */
#define	WIFI_MP_STATE		0x00010000
#define	WIFI_MP_CTX_BACKGROUND	0x00020000	/* in cont. tx background*/
#define	WIFI_MP_CTX_ST		0x00040000	/* in cont. tx with
						 * single-tone
						 */
#define	WIFI_MP_CTX_BACKGROUND_PENDING	0x00080000 /* pending in cont, tx
						    * background due to out of skb
						    */
#define	WIFI_MP_CTX_CCK_HW	0x00100000	/* in continuous tx*/
#define	WIFI_MP_CTX_CCK_CS	0x00200000	/* in cont, tx with carrier
						 * suppression
						 */
#define   WIFI_MP_LPBK_STATE	0x00400000

#define _FW_UNDER_LINKING	WIFI_UNDER_LINKING
#define _FW_LINKED		WIFI_ASOC_STATE
#define _FW_UNDER_SURVEY	WIFI_SITE_MONITOR

/*
there are several "locks" in mlme_priv,
since mlme_priv is a shared resource between many threads,
like ISR/Call-Back functions, the OID handlers, and even timer functions.
Each _queue has its own locks, already.
Other items are protected by mlme_priv.lock.
To avoid possible dead lock, any thread trying to modify mlme_priv
SHALL not lock up more than one lock at a time!
*/

#define traffic_threshold	10
#define	traffic_scan_period	500

struct sitesurvey_ctrl {
	u64	last_tx_pkts;
	uint	last_rx_pkts;
	sint	traffic_busy;
	struct timer_list sitesurvey_ctrl_timer;
};

struct mlme_priv {

	spinlock_t lock;
	spinlock_t lock2;
	sint	fw_state;	/*shall we protect this variable? */
	u8 to_join; /*flag*/
	u8 *nic_hdl;
	struct list_head *pscanned;
	struct  __queue free_bss_pool;
	struct  __queue scanned_queue;
	u8 *free_bss_buf;
	unsigned long num_of_scanned;
	u8 passive_mode; /*add for Android's SCAN-ACTIVE/SCAN-PASSIVE */
	struct ndis_802_11_ssid	assoc_ssid;
	u8 assoc_bssid[6];
	struct wlan_network cur_network;
	struct sitesurvey_ctrl sitesurveyctrl;
	struct timer_list assoc_timer;
	uint assoc_by_bssid;
	uint assoc_by_rssi;
	struct timer_list scan_to_timer; /* driver handles scan_timeout.*/
	struct timer_list dhcp_timer; /* set dhcp to if driver in ps mode.*/
	struct qos_priv qospriv;
	struct ht_priv	htpriv;
	struct timer_list wdg_timer; /*watchdog periodic timer*/
};

static inline u8 *get_bssid(struct mlme_priv *pmlmepriv)
{
	return pmlmepriv->cur_network.network.MacAddress;
}

static inline u8 check_fwstate(struct mlme_priv *pmlmepriv, sint state)
{
	if (pmlmepriv->fw_state & state)
		return true;
	return false;
}

static inline sint get_fwstate(struct mlme_priv *pmlmepriv)
{
	return pmlmepriv->fw_state;
}

/*
 * No Limit on the calling context,
 * therefore set it to be the critical section...
 *
 * ### NOTE:#### (!!!!)
 * TAKE CARE BEFORE CALLING THIS FUNC, LOCK pmlmepriv->lock
 */
static inline void set_fwstate(struct mlme_priv *pmlmepriv, sint state)
{
	pmlmepriv->fw_state |= state;
}

static inline void _clr_fwstate_(struct mlme_priv *pmlmepriv, sint state)
{
	pmlmepriv->fw_state &= ~state;
}

/*
 * No Limit on the calling context,
 * therefore set it to be the critical section...
 */
static inline void clr_fwstate(struct mlme_priv *pmlmepriv, sint state)
{
	unsigned long irqL;

	spin_lock_irqsave(&pmlmepriv->lock, irqL);
	if (check_fwstate(pmlmepriv, state))
		pmlmepriv->fw_state ^= state;
	spin_unlock_irqrestore(&pmlmepriv->lock, irqL);
}

static inline void up_scanned_network(struct mlme_priv *pmlmepriv)
{
	unsigned long irqL;

	spin_lock_irqsave(&pmlmepriv->lock, irqL);
	pmlmepriv->num_of_scanned++;
	spin_unlock_irqrestore(&pmlmepriv->lock, irqL);
}

static inline void down_scanned_network(struct mlme_priv *pmlmepriv)
{
	unsigned long irqL;

	spin_lock_irqsave(&pmlmepriv->lock, irqL);
	pmlmepriv->num_of_scanned--;
	spin_unlock_irqrestore(&pmlmepriv->lock, irqL);
}

static inline void set_scanned_network_val(struct mlme_priv *pmlmepriv,
					     sint val)
{
	unsigned long irqL;

	spin_lock_irqsave(&pmlmepriv->lock, irqL);
	pmlmepriv->num_of_scanned = val;
	spin_unlock_irqrestore(&pmlmepriv->lock, irqL);
}

void r8712_survey_event_callback(struct _adapter *adapter, u8 *pbuf);
void r8712_surveydone_event_callback(struct _adapter *adapter, u8 *pbuf);
void r8712_joinbss_event_callback(struct _adapter *adapter, u8 *pbuf);
void r8712_stassoc_event_callback(struct _adapter *adapter, u8 *pbuf);
void r8712_stadel_event_callback(struct _adapter *adapter, u8 *pbuf);
void r8712_atimdone_event_callback(struct _adapter *adapter, u8 *pbuf);
void r8712_cpwm_event_callback(struct _adapter *adapter, u8 *pbuf);
void r8712_wpspbc_event_callback(struct _adapter *adapter, u8 *pbuf);
void r8712_free_network_queue(struct _adapter *adapter);
int r8712_init_mlme_priv(struct _adapter *adapter);
void r8712_free_mlme_priv(struct mlme_priv *pmlmepriv);
sint r8712_select_and_join_from_scan(struct mlme_priv *pmlmepriv);
sint r8712_set_key(struct _adapter *adapter,
		   struct security_priv *psecuritypriv, sint keyid);
sint r8712_set_auth(struct _adapter *adapter,
		    struct security_priv *psecuritypriv);
uint r8712_get_wlan_bssid_ex_sz(struct wlan_bssid_ex *bss);
void r8712_generate_random_ibss(u8 *pibss);
u8 *r8712_get_capability_from_ie(u8 *ie);
struct wlan_network *r8712_get_oldest_wlan_network(
				struct  __queue *scanned_queue);
void r8712_free_assoc_resources(struct _adapter *adapter);
void r8712_ind_disconnect(struct _adapter *adapter);
void r8712_indicate_connect(struct _adapter *adapter);
int r8712_restruct_sec_ie(struct _adapter *adapter, u8 *in_ie,
			  u8 *out_ie, uint in_len);
int r8712_restruct_wmm_ie(struct _adapter *adapter, u8 *in_ie,
			  u8 *out_ie, uint in_len, uint initial_out_len);
void r8712_init_registrypriv_dev_network(struct _adapter *adapter);
void r8712_update_registrypriv_dev_network(struct _adapter *adapter);
void _r8712_sitesurvey_ctrl_handler(struct _adapter *adapter);
void _r8712_join_timeout_handler(struct _adapter *adapter);
void r8712_scan_timeout_handler(struct _adapter *adapter);
void _r8712_dhcp_timeout_handler(struct _adapter *adapter);
void _r8712_wdg_timeout_handler(struct _adapter *adapter);
struct wlan_network *_r8712_alloc_network(struct mlme_priv *pmlmepriv);
sint r8712_if_up(struct _adapter *padapter);
void r8712_joinbss_reset(struct _adapter *padapter);
unsigned int r8712_restructure_ht_ie(struct _adapter *padapter, u8 *in_ie,
				     u8 *out_ie, uint in_len, uint *pout_len);
void r8712_issue_addbareq_cmd(struct _adapter *padapter, int priority);
int r8712_is_same_ibss(struct _adapter *adapter, struct wlan_network *pnetwork);

#endif /*__RTL871X_MLME_H_*/
