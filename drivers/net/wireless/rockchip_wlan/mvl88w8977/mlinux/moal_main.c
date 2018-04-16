/** @file moal_main.c
  *
  * @brief This file contains the major functions in WLAN
  * driver.
  *
  * Copyright (C) 2008-2017, Marvell International Ltd.
  *
  * This software file (the "File") is distributed by Marvell International
  * Ltd. under the terms of the GNU General Public License Version 2, June 1991
  * (the "License").  You may use, redistribute and/or modify this File in
  * accordance with the terms and conditions of the License, a copy of which
  * is available by writing to the Free Software Foundation, Inc.,
  * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
  * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
  *
  * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
  * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
  * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
  * this warranty disclaimer.
  *
  */

/********************************************************
Change log:
    10/21/2008: initial version
********************************************************/

#include	"moal_main.h"
#include    "moal_sdio.h"
#ifdef UAP_SUPPORT
#include    "moal_uap.h"
#endif
#if defined(STA_CFG80211) || defined(UAP_CFG80211)
#include    "moal_cfg80211.h"
#include    "moal_cfgvendor.h"
#endif
#ifdef STA_CFG80211
#ifdef STA_SUPPORT
#include    "moal_sta_cfg80211.h"
#endif
#endif
#ifdef UAP_CFG80211
#ifdef UAP_SUPPORT
#include    "moal_uap_cfg80211.h"
#endif
#endif
#include "moal_eth_ioctl.h"

#include <linux/if_ether.h>
#include <linux/in.h>
#include <linux/tcp.h>
#include <net/tcp.h>
#include <net/dsfield.h>

#ifdef CONFIG_OF
#include <linux/of.h>
#endif

/********************************************************
		Local Variables
********************************************************/

#define KERN_VERSION    "4X"

/** Driver version */
char driver_version[] =
	"SD8977-%s-C" KERN_VERSION "16" MLAN_RELEASE_VERSION
	"-GPL" "-(" "FP" FPNUM ")"
#ifdef	DEBUG_LEVEL2
	"-dbg"
#endif
	" ";

/** Firmware name */
char *fw_name;
int req_fw_nowait;
int fw_reload;

/** MAC address */
char *mac_addr;

#ifdef MFG_CMD_SUPPORT
/** Mfg mode */
int mfg_mode;
#endif

/** SDIO interrupt mode (0: INT_MODE_SDIO, 1: INT_MODE_GPIO) */
int intmode = INT_MODE_SDIO;
/** GPIO interrupt pin number */
int gpiopin;

#ifdef CONFIG_OF
#if defined(STA_CFG80211) || defined(UAP_CFG80211)
/** Region alpha2 string */
extern char *reg_alpha2;
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
extern int country_ie_ignore;
extern int beacon_hints;
#endif
#endif
extern int cfg80211_drcs;
#endif

int drcs_chantime_mode = 0;

/** Auto deep sleep */
int auto_ds;

/** IEEE PS mode */
int ps_mode;

/** Max Tx buffer size */
int max_tx_buf;

#ifdef STA_SUPPORT
/** Max STA interfaces */
int max_sta_bss = DEF_STA_BSS;
/** STA interface name */
char *sta_name;
#endif

#ifdef UAP_SUPPORT
/** Max uAP interfaces */
int max_uap_bss = DEF_UAP_BSS;
/** uAP interface name */
char *uap_name;
#endif

#if defined(WIFI_DIRECT_SUPPORT)
/** Max WIFIDIRECT interfaces */
int max_wfd_bss = DEF_WIFIDIRECT_BSS;
/** WIFIDIRECT interface name */
char *wfd_name;
#if defined(STA_CFG80211) && defined(UAP_CFG80211)
/** max VIRTUAL bss */
int max_vir_bss = DEF_VIRTUAL_BSS;
#endif
#endif

/** Max NAN interfaces */
int max_nan_bss = DEF_NAN_BSS;
/** NAN interface name */
char *nan_name;

#ifdef SDIO_SUSPEND_RESUME
/** PM keep power */
int pm_keep_power = 1;
/** HS when shutdown */
int shutdown_hs;
#endif

#if defined(STA_SUPPORT)
/** 802.11d configuration */
int cfg_11d;
#endif

/** fw serial download check */
int fw_serial = 1;

/** napi support*/
int napi;

/** DPD data config file */
char *dpd_data_cfg;

/** CAL data config file */
char *cal_data_cfg;
/** Init config file (MAC address, register etc.) */
char *init_cfg;

/** Set configuration data of Tx power limitation */
char *txpwrlimit_cfg;
/** Set configuration data of Tx power limitatio */
char *country_txpwrlimit;
/** Allow setting tx power table of country */
int cntry_txpwr = 0;

/** Init hostcmd file */
char *init_hostcmd_cfg;

#if defined(STA_WEXT) || defined(UAP_WEXT)
/** CFG80211 and WEXT mode */
int cfg80211_wext = STA_WEXT_MASK | UAP_WEXT_MASK;
#else
/** CFG80211 mode */
int cfg80211_wext = STA_CFG80211_MASK | UAP_CFG80211_MASK;
#endif

int fw_region = 1;

/** Work queue priority */
int wq_sched_prio;
/** Work queue scheduling policy */
int wq_sched_policy = SCHED_NORMAL;
/** rx_work flag */
int rx_work;

int hw_test;

#ifdef CONFIG_OF
int dts_enable = 1;
#endif

#if defined(WIFI_DIRECT_SUPPORT)
#if defined(STA_CFG80211) && defined(UAP_CFG80211)
#if CFG80211_VERSION_CODE >= WIFI_DIRECT_KERNEL_VERSION
int p2p_enh;
#endif
#endif
#endif

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
int dfs_offload = 0;
#endif

int roamoffload_in_hs = 0;

#ifdef ANDROID_KERNEL
int wakelock_timeout = WAKE_LOCK_TIMEOUT;
#endif

/** woal_callbacks */
static mlan_callbacks woal_callbacks = {
	.moal_get_fw_data = moal_get_fw_data,
	.moal_get_hw_spec_complete = moal_get_hw_spec_complete,
	.moal_init_fw_complete = moal_init_fw_complete,
	.moal_shutdown_fw_complete = moal_shutdown_fw_complete,
	.moal_send_packet_complete = moal_send_packet_complete,
	.moal_recv_packet = moal_recv_packet,
	.moal_recv_event = moal_recv_event,
	.moal_ioctl_complete = moal_ioctl_complete,
	.moal_alloc_mlan_buffer = moal_alloc_mlan_buffer,
	.moal_free_mlan_buffer = moal_free_mlan_buffer,

	.moal_write_reg = moal_write_reg,
	.moal_read_reg = moal_read_reg,
	.moal_write_data_sync = moal_write_data_sync,
	.moal_read_data_sync = moal_read_data_sync,
	.moal_malloc = moal_malloc,
	.moal_mfree = moal_mfree,
	.moal_vmalloc = moal_vmalloc,
	.moal_vfree = moal_vfree,
	.moal_memset = moal_memset,
	.moal_memcpy = moal_memcpy,
	.moal_memmove = moal_memmove,
	.moal_memcmp = moal_memcmp,
	.moal_udelay = moal_udelay,
	.moal_get_system_time = moal_get_system_time,
	.moal_init_timer = moal_init_timer,
	.moal_free_timer = moal_free_timer,
	.moal_start_timer = moal_start_timer,
	.moal_stop_timer = moal_stop_timer,
	.moal_init_lock = moal_init_lock,
	.moal_free_lock = moal_free_lock,
	.moal_spin_lock = moal_spin_lock,
	.moal_spin_unlock = moal_spin_unlock,
	.moal_print = moal_print,
	.moal_print_netintf = moal_print_netintf,
	.moal_assert = moal_assert,
	.moal_hist_data_add = moal_hist_data_add,
	.moal_updata_peer_signal = moal_updata_peer_signal,
	.moal_get_host_time_ns = moal_get_host_time_ns,
	.moal_do_div = moal_do_div,
};

#if defined(STA_SUPPORT) && defined(UAP_SUPPORT)
#if defined(WIFI_DIRECT_SUPPORT)
int drv_mode = (DRV_MODE_STA | DRV_MODE_UAP | DRV_MODE_WIFIDIRECT);
#else
int drv_mode = (DRV_MODE_STA | DRV_MODE_UAP);
#endif
#else
#ifdef STA_SUPPORT
int drv_mode = DRV_MODE_STA;
#else
int drv_mode = DRV_MODE_UAP;
#endif /* STA_SUPPORT */
#endif /* STA_SUPPORT & UAP_SUPPORT */

int gtk_rekey_offload = GTK_REKEY_OFFLOAD_DISABLE;

int pmic = 0;

t_u32 uap_oper_ctrl = 0;

int hs_wake_interval = 400;
int indication_gpio = 0xff;

int indrstcfg = 0xffffffff;

/** all the feature are enabled */
#define DEFAULT_DEV_CAP_MASK 0xffffffff
t_u32 dev_cap_mask = DEFAULT_DEV_CAP_MASK;
int sdio_rx_aggr = MTRUE;
/********************************************************
		Global Variables
********************************************************/

/** Semaphore for add/remove card */
struct semaphore AddRemoveCardSem;
/**
 * The global variable of a pointer to moal_handle
 * structure variable
 **/
moal_handle *m_handle[MAX_MLAN_ADAPTER];

/** The global variable of scan beacon buffer **/
int fixed_beacon_buffer = 0;
/** the pointer of new fwdump fname for each dump**/
char *fwdump_fname = NULL;

#ifdef WIFI_DIRECT_SUPPORT
int GoAgeoutTime = 0;
#endif

int multi_dtim = 0;

int inact_tmo = 0;

#ifdef DEBUG_LEVEL1
#ifdef DEBUG_LEVEL2
#define	DEFAULT_DEBUG_MASK	(0xffffffff)
#else
#define DEFAULT_DEBUG_MASK	(MMSG | MFATAL | MERROR)
#endif /* DEBUG_LEVEL2 */
t_u32 drvdbg = DEFAULT_DEBUG_MASK;

#endif /* DEBUG_LEVEL1 */

int woal_open(struct net_device *dev);
int woal_close(struct net_device *dev);
int woal_set_mac_address(struct net_device *dev, void *addr);
void woal_tx_timeout(struct net_device *dev);
struct net_device_stats *woal_get_stats(struct net_device *dev);
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 29)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
u16 woal_select_queue(struct net_device *dev, struct sk_buff *skb,
		      void *accel_priv, select_queue_fallback_t fallback);
#else
u16 woal_select_queue(struct net_device *dev, struct sk_buff *skb,
		      void *accel_priv);
#endif
#else
u16 woal_select_queue(struct net_device *dev, struct sk_buff *skb);
#endif
#endif

void woal_sdio_reg_dbg(moal_handle *phandle);

mlan_debug_info info;

static moal_handle *reset_handle;
/** Hang workqueue */
static struct workqueue_struct *hang_workqueue;
/** Hang work */
static struct work_struct hang_work;

/**
 *  @brief This function process FW hang
 *
 *  @param handle       Pointer to structure moal_handle
 *
 *  @return        N/A
 */
static void
woal_hang_work_queue(struct work_struct *work)
{
	int i;
	ENTER();
	if (!reset_handle) {
		LEAVE();
		return;
	}
	for (i = 0; i < reset_handle->priv_num; i++) {
		if (reset_handle->priv[i] && reset_handle->priv[i]->netdev) {
			PRINTM(MMSG, "Close netdev %s\n",
			       reset_handle->priv[i]->netdev->name);
			rtnl_lock();
			dev_close(reset_handle->priv[i]->netdev);
			rtnl_unlock();
			break;
		}
	}
	reset_handle = NULL;
	LEAVE();
}

/**
 *  @brief This function process FW hang
 *
 *  @param handle       Pointer to structure moal_handle
 *
 *  @return        N/A
 */
void
woal_process_hang(moal_handle *handle)
{
	ENTER();
	if (reset_handle == NULL) {
		PRINTM(MMSG, "Start to process hanging\n");
		reset_handle = handle;
		mlan_ioctl(handle->pmlan_adapter, NULL);
		queue_work(hang_workqueue, &hang_work);
#ifdef ANDROID_KERNEL
#define WAKE_LOCK_HANG 5000
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0)
		__pm_wakeup_event(&reset_handle->ws, WAKE_LOCK_HANG);
#else
		wake_lock_timeout(&reset_handle->wake_lock, WAKE_LOCK_HANG);
#endif
#endif
	}
	LEAVE();
}

/**
 *  @brief Check if any interface is active
 *
 *  @param handle        A pointer to moal_handle
 *
 *
 *  @return              MTRUE/MFALSE;
 */
t_u8
woal_is_any_interface_active(moal_handle *handle)
{
	int i;
	for (i = 0; i < handle->priv_num; i++) {
#ifdef STA_SUPPORT
		if (GET_BSS_ROLE(handle->priv[i]) == MLAN_BSS_ROLE_STA) {
			if (handle->priv[i]->media_connected == MTRUE)
				return MTRUE;
		}
#endif
#ifdef UAP_SUPPORT
		if (GET_BSS_ROLE(handle->priv[i]) == MLAN_BSS_ROLE_UAP) {
			if (handle->priv[i]->bss_started == MTRUE)
				return MTRUE;
		}
#endif
	}
	return MFALSE;
}

/**  @brief This function set/clear pmk to FW
 *
 *  @param priv     A Pointer to the moal_private structure
 *  @param action     set/clear action
 *
 *  @return      0: success  fail otherwise
 */
int
woal_set_clear_pmk(moal_private *priv, t_u8 action)
{
	mlan_ioctl_req *req;
	mlan_ds_sec_cfg *sec;
	mlan_status status;
	int ret = 0;
	t_u8 zero[MLAN_MAX_KEY_LENGTH] = { 0 };
	ENTER();

	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_sec_cfg));

	if (req == NULL) {
		ret = -ENOMEM;
	} else {
		sec = (mlan_ds_sec_cfg *)req->pbuf;
		sec->sub_command = MLAN_OID_SEC_CFG_PASSPHRASE;
		req->req_id = MLAN_IOCTL_SEC_CFG;
		req->action = action;

		if (action == MLAN_ACT_SET) {
			sec->param.passphrase.psk_type = MLAN_PSK_PMK;
			if (memcmp(priv->pmk.pmk, zero, MLAN_MAX_KEY_LENGTH))
				memcpy(&sec->param.passphrase.psk.pmk.pmk,
				       priv->pmk.pmk, MLAN_MAX_KEY_LENGTH);
			if (memcmp(priv->pmk.pmk_r0, zero, MLAN_MAX_KEY_LENGTH)
			    && memcmp(priv->pmk.pmk_r0_name, zero,
				      MLAN_MAX_PMKR0_NAME_LENGTH)) {
				memcpy(&sec->param.passphrase.psk.pmk.pmk_r0,
				       priv->pmk.pmk_r0, MLAN_MAX_KEY_LENGTH);
				memcpy(&sec->param.passphrase.psk.pmk.
				       pmk_r0_name, priv->pmk.pmk_r0_name,
				       MLAN_MAX_PMKR0_NAME_LENGTH);
			}
		}

		status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
		if (MLAN_STATUS_SUCCESS != status)
			ret = -EFAULT;
		if (status != MLAN_STATUS_PENDING)
			kfree(req);
	}

	LEAVE();
	return ret;
}

/**
 *  @brief This function handle the net interface ipaddr change event
 *
 *  @param nb      pointer to the notifier_block
 *  @param event   event type
 *  @param ptr     pointer to event struct
 *
 *  @return        NOTIFY_DONE or NOTIFY_OK
 */
static int
woal_netdevice_event(struct notifier_block *nb, unsigned long event, void *ptr)
{
	struct in_ifaddr *ifa = (struct in_ifaddr *)ptr;
	struct net_device *ndev;
	moal_private *priv;

	int ret = NOTIFY_OK;
#ifdef STA_CFG80211
	char rssi_low[11];
#endif
	ENTER();

	ndev = ifa->ifa_dev->dev;
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 29)
	if (!ndev || ndev->netdev_ops->ndo_open != woal_open)
#else
	if (!ndev || ndev->open != woal_open)
#endif
	{
		PRINTM(MIOCTL, "IP changes not for us, ignore. ndev[%p]\n",
		       ndev);
		if (ndev)
			PRINTM(MIOCTL, "changes on %s\n", ndev->name);
		ret = NOTIFY_DONE;
		goto done;
	}
	priv = (moal_private *)netdev_priv(ndev);
	if (priv->bss_type != MLAN_BSS_TYPE_STA
#if defined(WIFI_DIRECT_SUPPORT)
	    && priv->bss_type != MLAN_BSS_TYPE_WIFIDIRECT
#endif
	    && priv->bss_type != MLAN_BSS_TYPE_NAN) {
		PRINTM(MIOCTL, "Bss type [%d] is not STA/P2P, ignore\n",
		       (int)priv->bss_type);
		ret = NOTIFY_DONE;
		goto done;
	}

	switch (event) {
	case NETDEV_UP:
		PRINTM(MIOCTL, "[%s]: New ip addr: 0x%08x\n", ndev->name,
		       ifa->ifa_address);
		/* Save the IP addr now */
		memcpy(priv->ip_addr, &ifa->ifa_address,
		       sizeof(ifa->ifa_address));
		priv->ip_addr_type = IPADDR_TYPE_IPV4;
#ifdef STA_CFG80211
		if (!hw_test && priv->roaming_enabled) {
			sprintf(rssi_low, "%d", priv->rssi_low);
			woal_set_rssi_low_threshold(priv, rssi_low,
						    MOAL_IOCTL_WAIT);
		}
#endif
#ifdef STA_CFG80211
		if (priv->phandle->fw_roam_enable &&
		    (priv->phandle->fw_roam_enable != AUTO_RECONNECT)
		    && !roamoffload_in_hs) {
			sprintf(rssi_low, "%d", priv->rssi_low);
			woal_set_rssi_low_threshold(priv, rssi_low,
						    MOAL_IOCTL_WAIT);
			if (priv->pmk_saved) {
				woal_set_clear_pmk(priv, MLAN_ACT_SET);
				priv->pmk_saved = false;
			}
		}
#endif
		break;
	case NETDEV_DOWN:
		PRINTM(MIOCTL, "[%s]: Ip addr removed.\n", ndev->name);
		priv->ip_addr_type = IPADDR_TYPE_NONE;
		memset(priv->ip_addr, 0, sizeof(priv->ip_addr));
		break;
	default:
		PRINTM(MIOCTL, "[%s]: Ignore event: %u\n", ndev->name,
		       (unsigned int)event);
		ret = NOTIFY_DONE;
		goto done;
	}

done:
	LEAVE();
	return ret;
}

/**
 *  @brief This function validates a SSID as being able to be printed
 *
 *  @param pssid   SSID structure to validate
 *
 *  @return        MTRUE or MFALSE
 */
BOOLEAN
woal_ssid_valid(mlan_802_11_ssid *pssid)
{
#ifdef ASCII_SSID_CHECK
	unsigned int ssid_idx;

	ENTER();

	for (ssid_idx = 0; ssid_idx < pssid->ssid_len; ssid_idx++) {
		if ((pssid->ssid[ssid_idx] < 0x20) ||
		    (pssid->ssid[ssid_idx] > 0x7e)) {
			LEAVE();
			return MFALSE;
		}
	}
	LEAVE();
#endif
	return MTRUE;
}

#if defined(STA_CFG80211) || defined(UAP_CFG80211)
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(2, 6, 39)
/**
 *  @brief Remain on Channel timeout function
 *
 *  @param context  A pointer to context
 *  @return         N/A
 */
void
woal_remain_timer_func(void *context)
{
	moal_handle *handle = (moal_handle *)context;
	moal_private *priv = handle->priv[handle->remain_bss_index];

	ENTER();

	PRINTM(MEVENT, "remain_timer fired.\n");
	if (handle->cookie) {
		cfg80211_remain_on_channel_expired(
#if CFG80211_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
							  priv->netdev,
#else
							  priv->wdev,
#endif
							  handle->cookie,
							  &handle->chan,
#if CFG80211_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
							  handle->channel_type,
#endif
							  GFP_ATOMIC);
		handle->cookie = 0;
	}
	handle->is_remain_timer_set = MFALSE;

	LEAVE();
	return;
}
#endif
#endif

#if defined(WIFI_DIRECT_SUPPORT)
#if defined(STA_CFG80211) && defined(UAP_CFG80211)
/**
 *  @brief GO timeout function
 *
 *  @param context  A pointer to context
 *  @return         N/A
 */
void
woal_go_timer_func(void *context)
{
	moal_handle *handle = (moal_handle *)context;

	ENTER();

	PRINTM(MEVENT, "go_timer fired.\n");
	handle->is_go_timer_set = MFALSE;

	LEAVE();
	return;
}
#endif
#endif

/**
 *  @brief check if we already connect to the AP.
 *  @param priv         A pointer to moal_private structure
 *  @param ssid_bssid   A pointer to mlan_ssid_bssid structure
 *
 *  @return             MTRUE/MFALSE;
 */
int
woal_is_connected(moal_private *priv, mlan_ssid_bssid *ssid_bssid)
{
	mlan_bss_info bss_info;
	int ret = MFALSE;
	t_u8 zero_mac[] = { 0, 0, 0, 0, 0, 0 };
	ENTER();
	memset(&bss_info, 0, sizeof(bss_info));
	if (MLAN_STATUS_SUCCESS !=
	    woal_get_bss_info(priv, MOAL_IOCTL_WAIT, &bss_info))
		goto done;
	if (bss_info.media_connected) {
		if (memcmp(ssid_bssid->bssid, zero_mac, sizeof(zero_mac))) {
			if (ssid_bssid->ssid.ssid_len) {	/* compare ssid and bssid */
				if ((ssid_bssid->ssid.ssid_len ==
				     bss_info.ssid.ssid_len) &&
				    !memcmp(ssid_bssid->ssid.ssid,
					    bss_info.ssid.ssid,
					    bss_info.ssid.ssid_len) &&
				    !memcmp(ssid_bssid->bssid, bss_info.bssid,
					    MLAN_MAC_ADDR_LENGTH))
					ret = MTRUE;
			} else {	/* compare bssid */
				if (!memcmp
				    (ssid_bssid->bssid, bss_info.bssid,
				     MLAN_MAC_ADDR_LENGTH)) {
					memcpy(&ssid_bssid->ssid,
					       &bss_info.ssid,
					       sizeof(bss_info.ssid));
					ret = MTRUE;
				}
			}
		} else {	/* compare ssid */
			if (ssid_bssid->ssid.ssid_len &&
			    (ssid_bssid->ssid.ssid_len ==
			     bss_info.ssid.ssid_len) &&
			    !memcmp(ssid_bssid->ssid.ssid, bss_info.ssid.ssid,
				    bss_info.ssid.ssid_len)) {
				memcpy(&ssid_bssid->bssid, &bss_info.bssid,
				       MLAN_MAC_ADDR_LENGTH);
				ret = MTRUE;
			}
		}
	}
done:
	LEAVE();
	return ret;
}

/**
 * @brief Look up specific IE in a buf
 *
 * @param ie              Pointer to IEs
 * @param len             Total length of ie
 * @param id              Element id to lookup
 *
 * @return                Pointer of the specific IE -- success, NULL -- fail
 */
const t_u8 *
woal_parse_ie_tlv(const t_u8 *ie, int len, t_u8 id)
{
	int left_len = len;
	const t_u8 *pos = ie;
	int length;

	/* IE format:
	 * |   u8  |   id   |
	 * |   u8  |   len  |
	 * |   var |   data |
	 */
	while (left_len >= 2) {
		length = *(pos + 1);
		if ((*pos == id) && (length + 2) <= left_len)
			return pos;
		pos += (length + 2);
		left_len -= (length + 2);
	}

	return NULL;
}

/**
 *  @brief Get mode
 *
 *  @param priv          A pointer to moal_private structure
 *  @param wait_option   Wait option (MOAL_IOCTL_WAIT or MOAL_NO_WAIT)
 *
 *  @return              Wireless mode
 */
t_u32
woal_get_mode(moal_private *priv, t_u8 wait_option)
{
	int ret = 0;
	mlan_ds_bss *bss = NULL;
	mlan_ioctl_req *req = NULL;
	mlan_status status = MLAN_STATUS_SUCCESS;
	t_u32 mode = 0;

	ENTER();

#if defined(STA_WEXT) || defined(UAP_WEXT)
	mode = priv->w_stats.status;
#endif
	/* Allocate an IOCTL request buffer */
	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_bss));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	/* Fill request buffer */
	bss = (mlan_ds_bss *)req->pbuf;
	bss->sub_command = MLAN_OID_BSS_MODE;
	req->req_id = MLAN_IOCTL_BSS;
	req->action = MLAN_ACT_GET;

	/* Send IOCTL request to MLAN */
	status = woal_request_ioctl(priv, req, wait_option);
	if (status == MLAN_STATUS_SUCCESS) {
		switch (bss->param.bss_mode) {
		case MLAN_BSS_MODE_INFRA:
			mode = MW_MODE_INFRA;
			break;
		case MLAN_BSS_MODE_IBSS:
			mode = MW_MODE_ADHOC;
			break;
		default:
			mode = MW_MODE_AUTO;
			break;
		}
	}
done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return mode;
}

/********************************************************
		Local Functions
********************************************************/
/**
 *  @brief This function update the default firmware name
 *
 *  @param handle           A pointer to moal_handle structure
 *
 *  @return        N/A
 */
static void
woal_update_firmware_name(moal_handle *handle)
{
	if (fw_name) {
		handle->drv_mode.fw_name = fw_name;
	} else {
		if (!fw_serial || handle->fw_reload || fw_reload)
			handle->drv_mode.fw_name = DEFAULT_WLAN_FW_NAME;
		else
#if defined(UAP_SUPPORT) && defined(STA_SUPPORT)
			handle->drv_mode.fw_name = DEFAULT_AP_STA_FW_NAME;
#else
#ifdef UAP_SUPPORT
			handle->drv_mode.fw_name = DEFAULT_AP_FW_NAME;
#else
			handle->drv_mode.fw_name = DEFAULT_FW_NAME;
#endif /* UAP_SUPPORT */
#endif /* UAP_SUPPORT && STA_SUPPORT */
	}
}

/**
 *  @brief This function dynamically populates the driver mode table
 *
 *  @param handle           A pointer to moal_handle structure
 *  @param drv_mode_local   Driver mode
 *
 *  @return        MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
woal_update_drv_tbl(moal_handle *handle, int drv_mode_local)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	unsigned int intf_num = 0;
	int i = 0, j = 0;
	mlan_bss_attr *bss_tbl = NULL;
#if defined(WIFI_DIRECT_SUPPORT)
#if defined(STA_CFG80211) && defined(UAP_CFG80211)
	int last_wfd_index = 0;
#endif
#endif

	ENTER();

	/* Calculate number of interfaces */
#ifdef STA_SUPPORT
	if (drv_mode_local & DRV_MODE_STA) {
		if ((max_sta_bss < 1) || (max_sta_bss > MAX_STA_BSS)) {
			PRINTM(MWARN,
			       "Unsupported max_sta_bss (%d), setting to default\n",
			       max_sta_bss);
			max_sta_bss = DEF_STA_BSS;
		}
		intf_num += max_sta_bss;
	}
#endif /* STA_SUPPORT */

#ifdef UAP_SUPPORT
	if (drv_mode_local & DRV_MODE_UAP) {
		if ((max_uap_bss < 1) || (max_uap_bss > MAX_UAP_BSS)) {
			PRINTM(MWARN,
			       "Unsupported max_uap_bss (%d), setting to default\n",
			       max_uap_bss);
			max_uap_bss = DEF_UAP_BSS;
		}
		intf_num += max_uap_bss;
	}
#endif /* UAP_SUPPORT */

#if defined(WIFI_DIRECT_SUPPORT)
	if (drv_mode_local & DRV_MODE_WIFIDIRECT) {
		if ((max_wfd_bss < 1) || (max_wfd_bss > MAX_WIFIDIRECT_BSS)) {
			PRINTM(MWARN,
			       "Unsupported max_wfd_bss (%d), setting to default\n",
			       max_wfd_bss);
			max_wfd_bss = DEF_WIFIDIRECT_BSS;
		}
		intf_num += max_wfd_bss;
#if defined(STA_CFG80211) && defined(UAP_CFG80211)
		intf_num += max_vir_bss;
#endif
	}
#endif /* WIFI_DIRECT_SUPPORT  && V14_FEATURE */

	if (drv_mode_local & DRV_MODE_NAN) {
		if ((max_nan_bss < 1) || (max_nan_bss > MAX_NAN_BSS)) {
			PRINTM(MWARN,
			       "Unsupported max_nan_bss (%d), setting to default\n",
			       max_nan_bss);
			max_nan_bss = DEF_NAN_BSS;
		}
		intf_num += max_nan_bss;
	}

	/* Create BSS attribute table */
	if ((intf_num == 0) || (intf_num > MLAN_MAX_BSS_NUM)) {
		PRINTM(MERROR, "Unsupported number of BSS %d\n", intf_num);
		ret = MLAN_STATUS_FAILURE;
		goto done;
	} else {
		/* Create new table */
		bss_tbl = kmalloc(sizeof(mlan_bss_attr) * intf_num, GFP_KERNEL);
		if (!bss_tbl) {
			PRINTM(MERROR,
			       "Could not create BSS attribute table\n");
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}
	}

	/* Populate BSS attribute table */
#ifdef STA_SUPPORT
	if (drv_mode_local & DRV_MODE_STA) {
		for (j = 0; j < max_sta_bss; j++) {
			if (i >= intf_num)
				break;
			bss_tbl[i].bss_type = MLAN_BSS_TYPE_STA;
			bss_tbl[i].frame_type = MLAN_DATA_FRAME_TYPE_ETH_II;
			bss_tbl[i].active = MTRUE;
			bss_tbl[i].bss_priority = 0;
			bss_tbl[i].bss_num = j;
			bss_tbl[i].bss_virtual = MFALSE;
			i++;
		}
	}
#endif /* STA_SUPPORT */

#ifdef UAP_SUPPORT
	if (drv_mode_local & DRV_MODE_UAP) {
		for (j = 0; j < max_uap_bss; j++) {
			if (i >= intf_num)
				break;
			bss_tbl[i].bss_type = MLAN_BSS_TYPE_UAP;
			bss_tbl[i].frame_type = MLAN_DATA_FRAME_TYPE_ETH_II;
			bss_tbl[i].active = MTRUE;
			bss_tbl[i].bss_priority = 0;
			bss_tbl[i].bss_num = j;
			bss_tbl[i].bss_virtual = MFALSE;
			i++;
		}
	}
#endif /* UAP_SUPPORT */

#if defined(WIFI_DIRECT_SUPPORT)
	if (drv_mode_local & DRV_MODE_WIFIDIRECT) {
		for (j = 0; j < max_wfd_bss; j++) {
			if (i >= intf_num)
				break;
			bss_tbl[i].bss_type = MLAN_BSS_TYPE_WIFIDIRECT;
			bss_tbl[i].frame_type = MLAN_DATA_FRAME_TYPE_ETH_II;
			bss_tbl[i].active = MTRUE;
			bss_tbl[i].bss_priority = 0;
			bss_tbl[i].bss_num = j;
			bss_tbl[i].bss_virtual = MFALSE;
			i++;
		}
#if defined(STA_CFG80211) && defined(UAP_CFG80211)
		last_wfd_index = j;
#endif
	}
#endif /* WIFI_DIRECT_SUPPORT && V14_FEATURE */

	if (drv_mode_local & DRV_MODE_NAN) {
		for (j = 0; j < max_nan_bss; j++) {
			if (i >= intf_num)
				break;
			bss_tbl[i].bss_type = MLAN_BSS_TYPE_NAN;
			bss_tbl[i].frame_type = MLAN_DATA_FRAME_TYPE_ETH_II;
			bss_tbl[i].active = MTRUE;
			bss_tbl[i].bss_priority = 0;
			bss_tbl[i].bss_num = j;
			bss_tbl[i].bss_virtual = MFALSE;
			i++;
		}
	}

#if defined(WIFI_DIRECT_SUPPORT)
#if defined(STA_CFG80211) && defined(UAP_CFG80211)
    /** append virtual interface at the end of table */
	for (j = 0; j < max_vir_bss; j++) {
		if (i >= intf_num)
			break;
		bss_tbl[i].bss_type = MLAN_BSS_TYPE_WIFIDIRECT;
		bss_tbl[i].frame_type = MLAN_DATA_FRAME_TYPE_ETH_II;
		bss_tbl[i].active = MTRUE;
		bss_tbl[i].bss_priority = 0;
		bss_tbl[i].bss_num = j + last_wfd_index;
		bss_tbl[i].bss_virtual = MTRUE;
		i++;
	}
#endif
#endif
	/* Clear existing table, if any */
	kfree(handle->drv_mode.bss_attr);
	handle->drv_mode.bss_attr = NULL;

	/* Create moal_drv_mode entry */
	handle->drv_mode.drv_mode = drv_mode;
	handle->drv_mode.intf_num = intf_num;
	handle->drv_mode.bss_attr = bss_tbl;

	/* update default firmware name */
	woal_update_firmware_name(handle);
done:
	LEAVE();
	return ret;
}

#ifdef CONFIG_OF
/**
 *  @brief This function read the initial parameter from device tress
 *
 *  @return         N/A
 */
static void
woal_init_from_dev_tree(void)
{
	struct device_node *dt_node = NULL;
	struct property *prop;
	t_u32 data;
	const char *string_data;

	ENTER();

	if (!dts_enable) {
		PRINTM(MIOCTL, "DTS is disabled!");
		return;
	}

	dt_node = of_find_node_by_name(NULL, "sd8xxx-wlan");
	if (!dt_node) {
		LEAVE();
		return;
	}
	for_each_property_of_node(dt_node, prop) {
		if (!strncmp(prop->name, "drv_mode", strlen("drv_mode"))) {
			if (!of_property_read_u32(dt_node, prop->name, &data)) {
				PRINTM(MIOCTL, "drv_mode=0x%x\n", data);
				drv_mode = data;
			}
		}
#ifdef DEBUG_LEVEL1
		else if (!strncmp(prop->name, "drvdbg", strlen("drvdbg"))) {
			if (!of_property_read_u32(dt_node, prop->name, &data)) {
				PRINTM(MIOCTL, "drvdbg=0x%x\n", data);
				drvdbg = data;
			}
		}
#endif
		else if (!strncmp
			 (prop->name, "dev_cap_mask", strlen("dev_cap_mask"))) {
			if (!of_property_read_u32(dt_node, prop->name, &data)) {
				PRINTM(MIOCTL, "dev_cap_mask=0x%x\n", data);
				dev_cap_mask = data;
			}
		} else if (!strncmp(prop->name, "hw_test", strlen("hw_test"))) {
			if (!of_property_read_u32(dt_node, prop->name, &data)) {
				PRINTM(MIOCTL, "hw_test=0x%x\n", data);
				hw_test = data;
			}
		}
#ifdef MFG_CMD_SUPPORT
		else if (!strncmp(prop->name, "mfg_mode", strlen("mfg_mode"))) {
			if (!of_property_read_u32(dt_node, prop->name, &data)) {
				PRINTM(MIOCTL, "mfg_mode=0x%x\n", data);
				mfg_mode = data;
			}
		}
#endif
		else if (!strncmp(prop->name, "mac_addr", strlen("mac_addr"))) {
			if (!of_property_read_string
			    (dt_node, prop->name, &string_data)) {
				mac_addr = (char *)string_data;
				PRINTM(MIOCTL, "mac_addr=%s\n", mac_addr);
			}
		} else if (!strncmp(prop->name, "fw_name", strlen("fw_name"))) {
			if (!of_property_read_string
			    (dt_node, prop->name, &string_data)) {
				fw_name = (char *)string_data;
				PRINTM(MIOCTL, "fw_name=%s\n", fw_name);
			}
		}
#if defined(STA_WEXT) || defined(UAP_WEXT)
		else if (!strncmp
			 (prop->name, "cfg80211_wext",
			  strlen("cfg80211_wext"))) {
			if (!of_property_read_u32(dt_node, prop->name, &data)) {
				PRINTM(MIOCTL, "cfg80211_wext=0x%x\n", data);
				cfg80211_wext = data;
			}
		}
#endif
#ifdef STA_SUPPORT
		else if (!strncmp(prop->name, "sta_name", strlen("sta_name"))) {
			if (!of_property_read_string
			    (dt_node, prop->name, &string_data)) {
				sta_name = (char *)string_data;
				PRINTM(MIOCTL, "sta_name=%s\n", sta_name);
			}
		}
#endif
#if defined(WIFI_DIRECT_SUPPORT)
		else if (!strncmp(prop->name, "wfd_name", strlen("wfd_name"))) {
			if (!of_property_read_string
			    (dt_node, prop->name, &string_data)) {
				wfd_name = (char *)string_data;
				PRINTM(MIOCTL, "wfd_name=%s\n", wfd_name);
			}
		}
#endif
#if defined(STA_CFG80211) || defined(UAP_CFG80211)
		else if (!strncmp
			 (prop->name, "reg_alpha2", strlen("reg_alpha2"))) {
			if (!of_property_read_string
			    (dt_node, prop->name, &string_data)) {
				reg_alpha2 = (char *)string_data;
				PRINTM(MIOCTL, "reg_alpha2=%s\n", reg_alpha2);
			}
		}
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
		else if (!strncmp
			 (prop->name, "country_ie_ignore",
			  strlen("country_ie_ignore"))) {
			if (!of_property_read_u32(dt_node, prop->name, &data)) {
				PRINTM(MIOCTL, "country_ie_ignore=0x%x\n",
				       data);
				country_ie_ignore = data;
			}
		} else if (!strncmp
			   (prop->name, "beacon_hints",
			    strlen("beacon_hints"))) {
			if (!of_property_read_u32(dt_node, prop->name, &data)) {
				PRINTM(MIOCTL, "beacon_hints=0x%x\n", data);
				beacon_hints = data;
			}
		}
#endif
#endif
#if defined(WIFI_DIRECT_SUPPORT)
#if defined(STA_CFG80211) && defined(UAP_CFG80211)
		else if (!strncmp
			 (prop->name, "max_vir_bss", strlen("max_vir_bss"))) {
			if (!of_property_read_u32(dt_node, prop->name, &data)) {
				PRINTM(MIOCTL, "max_vir_bss=0x%x\n", data);
				max_vir_bss = data;
			}
		} else if (!strncmp(prop->name, "p2p_enh", strlen("p2p_enh"))) {
			if (!of_property_read_u32(dt_node, prop->name, &data)) {
				PRINTM(MIOCTL, "p2p_enh=0x%x\n", data);
				p2p_enh = data;
			}
		} else if (!strncmp
			   (prop->name, "cfg80211_drcs",
			    strlen("cfg80211_drcs"))) {
			if (!of_property_read_u32(dt_node, prop->name, &data)) {
				PRINTM(MIOCTL, "cfg80211_drcs=0x%x\n", data);
				cfg80211_drcs = data;
			}
		}
#endif
#endif
		else if (!strncmp
			 (prop->name, "dpd_data_cfg", strlen("dpd_data_cfg"))) {
			if (!of_property_read_string
			    (dt_node, prop->name, &string_data)) {
				dpd_data_cfg = (char *)string_data;
				PRINTM(MIOCTL, "dpd_data_cfg=%s\n",
				       dpd_data_cfg);
			}
		} else if (!strncmp(prop->name, "init_cfg", strlen("init_cfg"))) {
			if (!of_property_read_string
			    (dt_node, prop->name, &string_data)) {
				init_cfg = (char *)string_data;
				PRINTM(MIOCTL, "init_cfg=%s\n", init_cfg);
			}
		} else if (!strncmp
			   (prop->name, "cal_data_cfg",
			    strlen("cal_data_cfg"))) {
			if (!of_property_read_string
			    (dt_node, prop->name, &string_data)) {
				cal_data_cfg = (char *)string_data;
				PRINTM(MIOCTL, "cal_data_cfg=%s\n",
				       cal_data_cfg);
			}
		} else if (!strncmp
			   (prop->name, "txpwrlimit_cfg",
			    strlen("txpwrlimit_cfg"))) {
			if (!of_property_read_string
			    (dt_node, prop->name, &string_data)) {
				txpwrlimit_cfg = (char *)string_data;
				PRINTM(MIOCTL, "txpwrlimit_cfg=%s\n",
				       txpwrlimit_cfg);
			}
		} else if (!strncmp
			   (prop->name, "cntry_txpwr", strlen("cntry_txpwr"))) {
			if (!of_property_read_u32(dt_node, prop->name, &data)) {
				cntry_txpwr = data;
				PRINTM(MIOCTL, "cntry_txpwr=%d\n", cntry_txpwr);
			}
		} else if (!strncmp(prop->name, "pmic", strlen("pmic"))) {
			if (!of_property_read_u32(dt_node, prop->name, &data)) {
				pmic = data;
				PRINTM(MIOCTL, "pmic=%d\n", pmic);
			}
		} else if (!strncmp
			   (prop->name, "hs_wake_interval",
			    strlen("hs_wake_interval"))) {
			if (!of_property_read_u32(dt_node, prop->name, &data)) {
				hs_wake_interval = data;
				PRINTM(MIOCTL, "hs_wake_interval=%d\n",
				       hs_wake_interval);
			}
		} else if (!strncmp
			   (prop->name, "indication_gpio",
			    strlen("indication_gpio"))) {
			if (!of_property_read_u32(dt_node, prop->name, &data)) {
				indication_gpio = (t_u8)data;
				PRINTM(MIOCTL, "indication_gpio=%d\n",
				       indication_gpio);
			}
		}
#ifdef WIFI_DIRECT_SUPPORT
		else if (!strncmp
			 (prop->name, "GoAgeoutTime", strlen("GoAgeoutTime"))) {
			if (!of_property_read_u32(dt_node, prop->name, &data)) {
				GoAgeoutTime = data;
				PRINTM(MIOCTL, "GoAgeoutTime=%d\n",
				       GoAgeoutTime);
			}
		}
#endif
		else if (!strncmp(prop->name, "indrstcfg", strlen("indrstcfg"))) {
			if (!of_property_read_u32(dt_node, prop->name, &data)) {
				indrstcfg = data;
				PRINTM(MIOCTL, "indrstcfg=%d\n", indrstcfg);
			}
		} else if (!strncmp
			   (prop->name, "drcs_chantime_mode",
			    strlen("drcs_chantime_mode"))) {
			if (!of_property_read_u32(dt_node, prop->name, &data)) {
				drcs_chantime_mode = data;
				PRINTM(MIOCTL, "drcs_chantime_mode=%d\n",
				       drcs_chantime_mode);
			}
		} else if (!strncmp
			   (prop->name, "fixed_beacon_buffer",
			    strlen("fixed_beacon_buffer"))) {
			if (!of_property_read_u32(dt_node, prop->name, &data)) {
				fixed_beacon_buffer = data;
				PRINTM(MIOCTL, "fixed_beacon_buffer=%d\n",
				       fixed_beacon_buffer);
			}
		} else if (!strncmp
			   (prop->name, "multi_dtim", strlen("multi_dtim"))) {
			if (!of_property_read_u32(dt_node, prop->name, &data)) {
				multi_dtim = data;
				PRINTM(MIOCTL, "multi_dtim=%d\n", multi_dtim);
			}
		} else if (!strncmp
			   (prop->name, "inact_tmo", strlen("inact_tmo"))) {
			if (!of_property_read_u32(dt_node, prop->name, &data)) {
				inact_tmo = data;
				PRINTM(MIOCTL, "inact_tmo=%d\n", inact_tmo);
			}
		}
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
		else if (!strncmp
			 (prop->name, "dfs_offload", strlen("dfs_offload"))) {
			if (!of_property_read_u32(dt_node, prop->name, &data)) {
				dfs_offload = data;
				PRINTM(MIOCTL, "dfs_offload=%d\n", dfs_offload);
			}
		}
#endif
		else if (!strncmp
			 (prop->name, "roamoffload_in_hs",
			  strlen("roamoffload_in_hs"))) {
			if (!of_property_read_u32(dt_node, prop->name, &data)) {
				roamoffload_in_hs = data;
				PRINTM(MIOCTL, "roamoffload_in_hs=%d\n",
				       roamoffload_in_hs);
			}
		} else if (!strncmp
			   (prop->name, "gtk_rekey_offload",
			    strlen("gtk_rekey_offload"))) {
			if (!of_property_read_u32(dt_node, prop->name, &data)) {
				gtk_rekey_offload = data;
				PRINTM(MIOCTL, "gtk_rekey_offload=%d\n",
				       gtk_rekey_offload);
			}
		}
	}
	LEAVE();
	return;
}
#endif

/**
 *  @brief This function initializes software
 *
 *  @param handle   A pointer to moal_handle structure
 *
 *  @return         MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
woal_init_sw(moal_handle *handle)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	unsigned int i;
	mlan_device device;
	t_void *pmlan;

	ENTER();

	/* Initialize moal_handle structure */
	handle->hardware_status = HardwareStatusInitializing;
	handle->main_state = MOAL_STATE_IDLE;

#ifdef STA_SUPPORT
	if ((drv_mode & DRV_MODE_STA)
#ifdef STA_WEXT
	    && !IS_STA_WEXT(cfg80211_wext)
#endif
#ifdef STA_CFG80211
	    && !IS_STA_CFG80211(cfg80211_wext)
#endif
		) {
		PRINTM(MERROR,
		       "STA without WEXT or CFG80211 bit definition!\n");
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}
#endif /* STA_SUPPORT */

#if defined(STA_CFG80211) && defined(STA_SUPPORT)
	if (IS_STA_CFG80211(cfg80211_wext))
		cfg80211_wext |= STA_CFG80211_MASK | UAP_CFG80211_MASK;
#endif

#if defined(UAP_CFG80211) && defined(UAP_SUPPORT)
	if (IS_UAP_CFG80211(cfg80211_wext))
		cfg80211_wext |= STA_CFG80211_MASK | UAP_CFG80211_MASK;
#endif

	memcpy(handle->driver_version, driver_version, strlen(driver_version));

	if (woal_update_drv_tbl(handle, drv_mode) != MLAN_STATUS_SUCCESS) {
		PRINTM(MERROR, "Could not update driver mode table\n");
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

    /** user config file */
	init_waitqueue_head(&handle->init_user_conf_wait_q);

	/* PnP and power profile */
	handle->surprise_removed = MFALSE;
	init_waitqueue_head(&handle->init_wait_q);
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 29)
	spin_lock_init(&handle->queue_lock);
#endif
	spin_lock_init(&handle->driver_lock);
	spin_lock_init(&handle->ioctl_lock);
	spin_lock_init(&handle->scan_req_lock);

#if defined(SDIO_SUSPEND_RESUME)
	handle->is_suspended = MFALSE;
	handle->hs_activated = MFALSE;
	handle->hs_auto_arp = MTRUE;
	handle->suspend_fail = MFALSE;
#ifdef SDIO_SUSPEND_RESUME
	handle->suspend_notify_req = MFALSE;
#endif
	handle->hs_skip_count = 0;
	handle->hs_force_count = 0;
	handle->cmd52_func = 0;
	handle->cmd52_reg = 0;
	handle->cmd52_val = 0;
#if defined(STA_CFG80211) || defined(UAP_CFG80211)
	handle->scan_chan_gap = DEF_SCAN_CHAN_GAP;
#ifdef WIFI_DIRECT_SUPPORT
	handle->miracast_scan_time = DEF_MIRACAST_SCAN_TIME;
#define DEF_NOA_DURATION    0
#define DEF_NOA_INTERVAL    100
	handle->noa_duration = DEF_NOA_DURATION;
	handle->noa_interval = DEF_NOA_INTERVAL;
#endif
#endif
	init_waitqueue_head(&handle->hs_activate_wait_q);
#endif

	/* Initialize measurement wait queue */
	handle->meas_wait_q_woken = MFALSE;
	handle->meas_start_jiffies = 0;
	handle->cac_period = MFALSE;
	handle->delay_bss_start = MFALSE;
	init_waitqueue_head(&handle->meas_wait_q);
#if defined(UAP_SUPPORT)
	handle->chsw_wait_q_woken = MFALSE;
	init_waitqueue_head(&handle->chsw_wait_q);
#endif

#ifdef DFS_TESTING_SUPPORT
	handle->cac_period_jiffies = 0;
#endif
#ifdef UAP_CFG80211
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
	memset(&handle->dfs_channel, 0, sizeof(struct cfg80211_chan_def));
	woal_initialize_timer(&handle->cac_timer, woal_cac_timer_func, handle);
	handle->is_cac_timer_set = MFALSE;
	handle->cac_bss_index = 0xff;
#endif
#endif
	handle->mon_if = NULL;

#ifdef REASSOCIATION
	MOAL_INIT_SEMAPHORE(&handle->reassoc_sem);
	handle->reassoc_on = 0;

	/* Initialize the timer for the reassociation */
	woal_initialize_timer(&handle->reassoc_timer,
			      woal_reassoc_timer_func, handle);

	handle->is_reassoc_timer_set = MFALSE;
#endif /* REASSOCIATION */

#if defined(WIFI_DIRECT_SUPPORT)
#if defined(STA_CFG80211) && defined(UAP_CFG80211)
	/* Initialize the timer for GO timeout */
	woal_initialize_timer(&handle->go_timer, woal_go_timer_func, handle);

	handle->is_go_timer_set = MFALSE;
#endif
#endif

#if defined(STA_CFG80211) || defined(UAP_CFG80211)
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(2, 6, 39)
	handle->remain_on_channel = MFALSE;

	/* Initialize the timer for remain on channel */
	woal_initialize_timer(&handle->remain_timer,
			      woal_remain_timer_func, handle);

	handle->is_remain_timer_set = MFALSE;
#endif
#endif

	/* Register to MLAN */
	memset(&device, 0, sizeof(mlan_device));
	device.pmoal_handle = handle;

	device.sdio_rx_aggr_enable = sdio_rx_aggr;

#ifdef MFG_CMD_SUPPORT
	device.mfg_mode = (t_u32)mfg_mode;
#endif
	device.int_mode = (t_u32)intmode;
	device.gpio_pin = (t_u32)gpiopin;
#ifdef DEBUG_LEVEL1
	device.drvdbg = drvdbg;
#endif
	device.fixed_beacon_buffer = (t_u32)fixed_beacon_buffer;
	device.auto_ds = (t_u32)auto_ds;
	device.ps_mode = (t_u32)ps_mode;
	device.max_tx_buf = (t_u32)max_tx_buf;
#if defined(STA_SUPPORT)
	device.cfg_11d = (t_u32)cfg_11d;
#endif
	device.indrstcfg = (t_u32)indrstcfg;
	device.drcs_chantime_mode = (t_u32)drcs_chantime_mode;
#if defined(SDIO_MULTI_PORT_TX_AGGR) || defined(SDIO_MULTI_PORT_RX_AGGR)
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 36)
	device.max_segs =
		((struct sdio_mmc_card *)handle->card)->func->card->host->
		max_segs;
	device.max_seg_size =
		((struct sdio_mmc_card *)handle->card)->func->card->host->
		max_seg_size;
#endif
	PRINTM(MMSG, "SDIO: max_segs=%d max_seg_size=%d\n", device.max_segs,
	       device.max_seg_size);
#endif
#ifdef SDIO_MULTI_PORT_TX_AGGR
#ifdef MMC_QUIRK_BLKSZ_FOR_BYTE_MODE
	device.mpa_tx_cfg = MLAN_INIT_PARA_ENABLED;
#else
	device.mpa_tx_cfg = MLAN_INIT_PARA_DISABLED;
#endif
#endif
#ifdef SDIO_MULTI_PORT_RX_AGGR
#ifdef MMC_QUIRK_BLKSZ_FOR_BYTE_MODE
	device.mpa_rx_cfg = MLAN_INIT_PARA_ENABLED;
#else
	device.mpa_rx_cfg = MLAN_INIT_PARA_DISABLED;
#endif
#endif

	if (rx_work == MLAN_INIT_PARA_ENABLED)
		device.rx_work = MTRUE;
	else if (rx_work == MLAN_INIT_PARA_DISABLED)
		device.rx_work = MFALSE;
	else {
		if (num_possible_cpus() > 1)
			device.rx_work = MTRUE;
		else
			device.rx_work = MFALSE;
	}
	PRINTM(MMSG, "rx_work=%d cpu_num=%d\n", device.rx_work,
	       num_possible_cpus());
	if (napi)
		device.rx_work = MTRUE;

	device.dev_cap_mask = dev_cap_mask;

	device.multi_dtim = multi_dtim;

	device.inact_tmo = inact_tmo;
	device.hs_wake_interval = hs_wake_interval;
	device.indication_gpio = indication_gpio;

	for (i = 0; i < handle->drv_mode.intf_num; i++) {
		device.bss_attr[i].bss_type =
			handle->drv_mode.bss_attr[i].bss_type;
		device.bss_attr[i].frame_type =
			handle->drv_mode.bss_attr[i].frame_type;
		device.bss_attr[i].active = handle->drv_mode.bss_attr[i].active;
		device.bss_attr[i].bss_priority =
			handle->drv_mode.bss_attr[i].bss_priority;
		device.bss_attr[i].bss_num =
			handle->drv_mode.bss_attr[i].bss_num;
		device.bss_attr[i].bss_virtual =
			handle->drv_mode.bss_attr[i].bss_virtual;
	}
	memcpy(&device.callbacks, &woal_callbacks, sizeof(mlan_callbacks));
	if (fw_region)
		device.fw_region = MTRUE;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 32)
	sdio_claim_host(((struct sdio_mmc_card *)handle->card)->func);
#endif
	if (MLAN_STATUS_SUCCESS == mlan_register(&device, &pmlan))
		handle->pmlan_adapter = pmlan;
	else
		ret = MLAN_STATUS_FAILURE;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 32)
	sdio_release_host(((struct sdio_mmc_card *)handle->card)->func);
#endif

	LEAVE();
	return ret;
}

/**
 *  @brief This function frees the structure of moal_handle
 *
 *  @param handle   A pointer to moal_handle structure
 *
 *  @return         N/A
 */
static void
woal_free_moal_handle(moal_handle *handle)
{
	ENTER();
	if (!handle) {
		PRINTM(MERROR, "The handle is NULL\n");
		LEAVE();
		return;
	}
#if defined(STA_CFG80211) || defined(UAP_CFG80211)
	/* Unregister wiphy device and free */
	if (handle->wiphy) {
		wiphy_unregister(handle->wiphy);
		wiphy_free(handle->wiphy);
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
		kfree(handle->regd);
#endif
		handle->wiphy = NULL;
	}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 25)
	if ((handle->nl_sk) && ((handle->nl_sk)->sk_socket)) {
		sock_release((handle->nl_sk)->sk_socket);
		handle->nl_sk = NULL;
	}
#else
	netlink_kernel_release(handle->nl_sk);
#endif

	if (handle->pmlan_adapter)
		mlan_unregister(handle->pmlan_adapter);

	/* Free BSS attribute table */
	kfree(handle->drv_mode.bss_attr);
	handle->drv_mode.bss_attr = NULL;
	PRINTM(MINFO, "Free Adapter\n");
	if (atomic_read(&handle->lock_count) ||
	    atomic_read(&handle->malloc_count) ||
	    atomic_read(&handle->mbufalloc_count)) {
		PRINTM(MERROR,
		       "mlan has memory leak: lock_count=%d, malloc_count=%d, mbufalloc_count=%d\n",
		       atomic_read(&handle->lock_count),
		       atomic_read(&handle->malloc_count),
		       atomic_read(&handle->mbufalloc_count));
	}
	/* Free allocated memory for fwdump filename */
	kfree(handle->fwdump_fname);
	if (fwdump_fname) {
		kfree(fwdump_fname);
		fwdump_fname = NULL;
	}
	/* Free the moal handle itself */
	kfree(handle);
	LEAVE();
}

/**
 *    @brief WOAL get one line data from ASCII format data
 *
 *    @param data         Source data
 *    @param size         Source data length
 *    @param line_pos     Destination data
 *    @return             routnine status
 */
static t_size
parse_cfg_get_line(t_u8 *data, t_size size, t_u8 *line_pos)
{
	t_u8 *src, *dest;
	static t_s32 pos;

	ENTER();

	if (pos >= size) {	/* reach the end */
		pos = 0;	/* Reset position for rfkill */
		LEAVE();
		return -1;
	}
	memset(line_pos, 0, MAX_LINE_LEN);
	src = data + pos;
	dest = line_pos;

	while (pos < size && *src != '\x0A' && *src != '\0') {
		if (*src != ' ' && *src != '\t')	/* parse space */
			*dest++ = *src++;
		else
			src++;
		pos++;
	}
	/* parse new line */
	pos++;
	*dest = '\0';
	LEAVE();
	return strlen(line_pos);
}

/**
 *  @brief Process register access request
 *  @param type_string     String format Register type
 *  @param offset_string   String format Register offset
 *  @param value_string    String format Pointer to value
 *  @return                MLAN_STATUS_SUCCESS--success, otherwise--fail
 */
static t_u32
woal_process_regrdwr(moal_handle *handle, t_u8 *type_string,
		     t_u8 *offset_string, t_u8 *value_string)
{
	mlan_status ret = MLAN_STATUS_FAILURE;
	int type, offset, value;
	pmlan_ioctl_req ioctl_req = NULL;
	mlan_ds_reg_mem *reg = NULL;

	ENTER();

	/* Alloc ioctl_req */
	ioctl_req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_reg_mem));

	if (ioctl_req == NULL) {
		PRINTM(MERROR, "Can't alloc memory\n");
		goto done;
	}

	if (MLAN_STATUS_SUCCESS != woal_atoi(&type, type_string))
		goto done;
	if (MLAN_STATUS_SUCCESS != woal_atoi(&offset, offset_string))
		goto done;
	if (MLAN_STATUS_SUCCESS != woal_atoi(&value, value_string))
		goto done;

	ioctl_req->req_id = MLAN_IOCTL_REG_MEM;
	ioctl_req->action = MLAN_ACT_SET;

	reg = (mlan_ds_reg_mem *)ioctl_req->pbuf;
	reg->sub_command = MLAN_OID_REG_RW;
	if (type < 5) {
		reg->param.reg_rw.type = type;
	} else {
		PRINTM(MERROR, "Unsupported Type\n");
		goto done;
	}
	reg->param.reg_rw.offset = offset;
	reg->param.reg_rw.value = value;

	/* request ioctl for STA */
	ret = woal_request_ioctl(handle->priv[0], ioctl_req, MOAL_IOCTL_WAIT);
	if (ret != MLAN_STATUS_SUCCESS)
		goto done;
	PRINTM(MINFO, "Register type: %d, offset: 0x%x, value: 0x%x\n", type,
	       offset, value);
	ret = MLAN_STATUS_SUCCESS;

done:
	if (ret != MLAN_STATUS_PENDING)
		kfree(ioctl_req);
	LEAVE();
	return ret;
}

#ifdef UAP_SUPPORT
/**
 *    @brief set uap operation contrl value
 *
 *    @param handle       MOAL handle
 *    @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
woal_set_uap_operation_ctrl(moal_handle *handle)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	moal_private *priv = NULL;
	mlan_ds_bss *bss = NULL;
	mlan_ioctl_req *req = NULL;

	ENTER();

	priv = woal_get_priv(handle, MLAN_BSS_ROLE_UAP);
	if (!priv) {
		PRINTM(MERROR,
		       "woal_set_uap_operation_ctrl failed, no uap interface\n");
		LEAVE();
		return ret;
	}

	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_bss));
	if (req == NULL) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	bss = (mlan_ds_bss *)req->pbuf;
	bss->sub_command = MLAN_OID_UAP_OPER_CTRL;
	req->req_id = MLAN_IOCTL_BSS;
	req->action = MLAN_ACT_SET;

	bss->param.ap_oper_ctrl.ctrl_value =
		(t_u16)((uap_oper_ctrl & 0xffff0000) >> 16);
	bss->param.ap_oper_ctrl.chan_opt = (t_u16)(uap_oper_ctrl & 0xffff);
	PRINTM(MMSG, "Uap oper_ctrl=0x%x chan_opt=0x%x\n",
	       bss->param.ap_oper_ctrl.ctrl_value,
	       bss->param.ap_oper_ctrl.chan_opt);
	ret = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);

done:
	if (ret != MLAN_STATUS_PENDING)
		kfree(req);

	LEAVE();
	return ret;

}
#endif

/**
 *    @brief WOAL parse ASCII format data to MAC address
 *
 *    @param handle       MOAL handle
 *    @param data         Source data
 *    @param size         data length
 *    @return             MLAN_STATUS_SUCCESS--success, otherwise--fail
 */
static t_u32
woal_process_init_cfg(moal_handle *handle, t_u8 *data, t_size size)
{
	mlan_status ret = MLAN_STATUS_FAILURE;
	t_u8 *pos;
	t_u8 *intf_s, *intf_e;
	t_u8 s[MAX_LINE_LEN];	/* 1 line data */
	t_size line_len;
	t_u8 index = 0;
	t_u32 i;
	t_u8 bss_mac_addr[MAX_MAC_ADDR_LEN];
	t_u8 bss_mac_name[MAX_PARAM_LEN];
	t_u8 type[MAX_PARAM_LEN];
	t_u8 offset[MAX_PARAM_LEN];
	t_u8 value[MAX_PARAM_LEN];

	ENTER();

	while ((line_len = parse_cfg_get_line(data, size, s)) != -1) {

		pos = s;
		while (*pos == ' ' || *pos == '\t')
			pos++;

		if (*pos == '#' || (*pos == '\r' && *(pos + 1) == '\n') ||
		    *pos == '\n' || *pos == '\0')
			continue;	/* Needn't process this line */

		/* Process MAC addr */
		if (strncmp(pos, "mac_addr", 8) == 0) {
			intf_s = strchr(pos, '=');
			if (intf_s != NULL)
				intf_e = strchr(intf_s, ':');
			else
				intf_e = NULL;
			if (intf_s != NULL && intf_e != NULL) {
				strncpy(bss_mac_addr, intf_e + 1,
					MAX_MAC_ADDR_LEN - 1);
				bss_mac_addr[MAX_MAC_ADDR_LEN - 1] = '\0';
				if ((intf_e - intf_s) > MAX_PARAM_LEN) {
					PRINTM(MERROR,
					       "Too long interface name %d\n",
					       __LINE__);
					goto done;
				}
				strncpy(bss_mac_name, intf_s + 1,
					intf_e - intf_s - 1);
				bss_mac_name[intf_e - intf_s - 1] = '\0';
				for (i = 0; i < handle->priv_num; i++) {
					if (strcmp
					    (bss_mac_name,
					     handle->priv[i]->netdev->name) ==
					    0) {
						memset(handle->priv[i]->
						       current_addr, 0,
						       ETH_ALEN);
						PRINTM(MINFO,
						       "Interface name: %s mac: %s\n",
						       bss_mac_name,
						       bss_mac_addr);
						woal_mac2u8(handle->priv[i]->
							    current_addr,
							    bss_mac_addr);
#if defined(WIFI_DIRECT_SUPPORT)
#if defined(STA_CFG80211) && defined(UAP_CFG80211)
#if CFG80211_VERSION_CODE >= WIFI_DIRECT_KERNEL_VERSION
						if (handle->priv[i]->bss_type ==
						    MLAN_BSS_TYPE_WIFIDIRECT) {
							handle->priv[i]->
								current_addr[0]
								|= 0x02;
							PRINTM(MCMND,
							       "Set WFD device addr: "
							       MACSTR "\n",
							       MAC2STR(handle->
								       priv[i]->
								       current_addr));
						}
#endif
#endif
#endif
						/* Set WLAN MAC addresses */
						if (MLAN_STATUS_SUCCESS !=
						    woal_request_set_mac_address
						    (handle->priv[i])) {
							PRINTM(MERROR,
							       "Set MAC address failed\n");
							goto done;
						}
						memcpy(handle->priv[i]->netdev->
						       dev_addr,
						       handle->priv[i]->
						       current_addr, ETH_ALEN);
						index++;	/* Mark found one interface matching */
					}
				}
			} else {
				PRINTM(MERROR, "Wrong config file format %d\n",
				       __LINE__);
				goto done;
			}
		}
		/* Process REG value */
		else if (strncmp(pos, "wlan_reg", 8) == 0) {
			intf_s = strchr(pos, '=');
			if (intf_s != NULL)
				intf_e = strchr(intf_s, ',');
			else
				intf_e = NULL;
			if (intf_s != NULL && intf_e != NULL) {
				/* Copy type */
				strncpy(type, intf_s + 1, 1);
				type[1] = '\0';
			} else {
				PRINTM(MERROR, "Wrong config file format %d\n",
				       __LINE__);
				goto done;
			}
			intf_s = intf_e + 1;
			intf_e = strchr(intf_s, ',');
			if (intf_e != NULL) {
				if ((intf_e - intf_s) >= MAX_PARAM_LEN) {
					PRINTM(MERROR,
					       "Regsier offset is too long %d\n",
					       __LINE__);
					goto done;
				}
				/* Copy offset */
				strncpy(offset, intf_s, intf_e - intf_s);
				offset[intf_e - intf_s] = '\0';
			} else {
				PRINTM(MERROR, "Wrong config file format %d\n",
				       __LINE__);
				goto done;
			}
			intf_s = intf_e + 1;
			if ((strlen(intf_s) >= MAX_PARAM_LEN)) {
				PRINTM(MERROR, "Regsier value is too long %d\n",
				       __LINE__);
				goto done;
			}
			/* Copy value */
			strncpy(value, intf_s,
				MIN((MAX_PARAM_LEN - 1), strlen(intf_s)));

			if (MLAN_STATUS_SUCCESS !=
			    woal_process_regrdwr(handle, type, offset, value)) {
				PRINTM(MERROR, "Access Reg failed\n");
				goto done;
			}
			PRINTM(MINFO, "Reg type: %s, offset: %s, value: %s\n",
			       type, offset, value);
		}
	}

	if (index == 0)
		PRINTM(MINFO, "Can't find any matching MAC Address");
	ret = MLAN_STATUS_SUCCESS;

done:
	LEAVE();
	return ret;
}

/**
 *    @brief WOAL parse ASCII format raw data to hex format
 *
 *    @param handle       MOAL handle
 *    @param data         Source data
 *    @param size         data length
 *    @param wait_option  wait option
 *    @return             MLAN_STATUS_SUCCESS--success, otherwise--fail
 */
static t_u32
woal_process_hostcmd_cfg(moal_handle *handle, t_u8 *data, t_size size,
			 t_u8 wait_option)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u8 *pos = data;
	t_u8 *intf_s, *intf_e;
	t_u8 *buf = NULL;
	t_u8 *ptr = NULL;
	t_u32 cmd_len = 0;
	t_u8 start_raw = MFALSE;
	gfp_t flag;

#define CMD_STR     "MRVL_CMDhostcmd"
#define CMD_BUF_LEN 2048

	ENTER();
	flag = (in_atomic() || irqs_disabled())? GFP_ATOMIC : GFP_KERNEL;
	buf = kzalloc(CMD_BUF_LEN, flag);
	if (!buf) {
		PRINTM(MERROR, "Could not allocate buffer space!\n");
		goto done;
	}
	ptr = buf;
	strcpy(ptr, CMD_STR);
	ptr = buf + strlen(CMD_STR) + sizeof(t_u32);
	while ((pos - data) < size) {
		while (*pos == ' ' || *pos == '\t')
			pos++;
		if (*pos == '#') {	/* Line comment */
			while (*pos != '\n')
				pos++;
			pos++;
		}
		if ((*pos == '\r' && *(pos + 1) == '\n') ||
		    *pos == '\n' || *pos == '\0') {
			pos++;
			continue;	/* Needn't process this line */
		}

		if (*pos == '}') {
			cmd_len =
				*((t_u16 *)(buf + strlen(CMD_STR) +
					    sizeof(t_u32) + sizeof(t_u16)));
			memcpy(buf + strlen(CMD_STR), &cmd_len, sizeof(t_u32));

			/* fire the hostcommand from here */
			woal_priv_hostcmd(handle->priv[0], buf, CMD_BUF_LEN,
					  wait_option);
			memset(buf + strlen(CMD_STR), 0,
			       CMD_BUF_LEN - strlen(CMD_STR));
			ptr = buf + strlen(CMD_STR) + sizeof(t_u32);
			start_raw = MFALSE;
			pos++;
			continue;
		}

		if (start_raw == MFALSE) {
			intf_s = strchr(pos, '=');
			if (intf_s)
				intf_e = strchr(intf_s, '{');
			else
				intf_e = NULL;

			if (intf_s && intf_e) {
				start_raw = MTRUE;
				pos = intf_e + 1;
				continue;
			}
		}

		if (start_raw) {
			/* Raw data block exists */
			while (*pos != '\n') {
				if ((*pos <= 'f' && *pos >= 'a') ||
				    (*pos <= 'F' && *pos >= 'A') ||
				    (*pos <= '9' && *pos >= '0')) {
					*ptr++ = woal_atox(pos);
					pos += 2;
				} else
					pos++;
			}
		}
	}

done:
	kfree(buf);
	LEAVE();
	return ret;
}

#define INIT_CFG_DATA           0x00
#define INIT_HOSTCMD_CFG_DATA   0x02
#define COUNTRY_POWER_TABLE     0x04

/**
 * @brief Request init conf firmware callback
 *        This function is invoked by request_firmware_nowait system call
 *
 * @param firmware  A pointer to firmware image
 * @param context   A pointer to moal_handle structure
 *
 * @return          N/A
 */
static void
woal_request_init_user_conf_callback(const struct firmware *firmware,
				     void *context)
{
	moal_handle *handle;

	ENTER();

	handle = (moal_handle *)context;
	if (!handle) {
		LEAVE();
		return;
	}
	if (firmware)
		handle->user_data = firmware;
	else
		PRINTM(MERROR, "User init config request firmware failed\n");

	handle->init_user_conf_wait_flag = MTRUE;
	wake_up_interruptible(&handle->init_user_conf_wait_q);

	LEAVE();
	return;
}

/**
 * @brief Request init conf firmware callback
 *        This function is invoked by request_firmware_nowait system call
 *
 * @param firmware  A pointer to firmware image
 * @param context   A pointer to moal_handle structure
 *
 * @return          N/A
 */
static void
woal_request_init_dpd_conf_callback(const struct firmware *firmware,
				    void *context)
{
	moal_handle *handle;

	ENTER();

	handle = (moal_handle *)context;
	if (!handle) {
		LEAVE();
		return;
	}
	if (firmware && handle)
		handle->dpd_data = firmware;
	else
		PRINTM(MERROR, "User init cfg data request firmware failed\n");

	handle->init_user_conf_wait_flag = MTRUE;
	wake_up_interruptible(&handle->init_user_conf_wait_q);

	LEAVE();
	return;
}

/**
 * @brief Request init conf firmware callback
 *        This function is invoked by request_firmware_nowait system call
 *
 * @param firmware  A pointer to firmware image
 * @param context   A pointer to moal_handle structure
 *
 * @return          N/A
 */
static void
woal_request_init_txpwr_conf_callback(const struct firmware *firmware,
				      void *context)
{
	moal_handle *handle;

	ENTER();

	handle = (moal_handle *)context;
	if (!handle) {
		LEAVE();
		return;
	}
	if (firmware && handle)
		handle->txpwr_data = firmware;
	else
		PRINTM(MERROR, "User init cfg data request firmware failed\n");

	handle->init_user_conf_wait_flag = MTRUE;
	wake_up_interruptible(&handle->init_user_conf_wait_q);

	LEAVE();
	return;
}

/**
 * @brief Request init conf firmware callback
 *        This function is invoked by request_firmware_nowait system call
 *
 * @param firmware  A pointer to firmware image
 * @param context   A pointer to moal_handle structure
 *
 * @return          N/A
 */
static void
woal_request_init_cfg_data_callback(const struct firmware *firmware,
				    void *context)
{
	moal_handle *handle;

	ENTER();

	handle = (moal_handle *)context;
	if (!handle) {
		LEAVE();
		return;
	}
	if (firmware && handle)
		handle->init_cfg_data = firmware;
	else
		PRINTM(MERROR, "User init cfg data request firmware failed\n");

	handle->init_user_conf_wait_flag = MTRUE;
	wake_up_interruptible(&handle->init_user_conf_wait_q);

	LEAVE();
	return;
}

/**
 *    @brief WOAL set user defined init data and param
 *
 *    @param handle       MOAL handle structure
 *    @param type         type argument
 *    @param wait_option  wait option
 *    @return             MLAN_STATUS_SUCCESS--success, otherwise--fail
 */
static t_u32
woal_set_user_init_data(moal_handle *handle, int type, t_u8 wait_option)
{
	mlan_status ret = MLAN_STATUS_FAILURE;
	t_u8 *cfg_data = NULL;
	t_size len;

	ENTER();

	if (type == INIT_CFG_DATA) {
		if (req_fw_nowait) {
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 32)
			if ((request_firmware_nowait
			     (THIS_MODULE, FW_ACTION_HOTPLUG, init_cfg,
			      handle->hotplug_device, GFP_KERNEL, handle,
			      woal_request_init_cfg_data_callback)) < 0) {
#else
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 13)
			if ((request_firmware_nowait
			     (THIS_MODULE, FW_ACTION_HOTPLUG, init_cfg,
			      handle->hotplug_device, handle,
			      woal_request_init_cfg_data_callback)) < 0) {
#else
			if ((request_firmware_nowait
			     (THIS_MODULE, init_cfg, handle->hotplug_device,
			      handle,
			      woal_request_init_cfg_data_callback)) < 0) {
#endif
#endif
				PRINTM(MERROR,
				       "Init config file request_firmware_nowait() failed\n");
				goto done;
			}
			handle->init_user_conf_wait_flag = MFALSE;
			wait_event_interruptible(handle->init_user_conf_wait_q,
						 handle->
						 init_user_conf_wait_flag);
		} else {
			if ((request_firmware
			     (&handle->init_cfg_data, init_cfg,
			      handle->hotplug_device)) < 0) {
				PRINTM(MERROR,
				       "Init config file request_firmware() failed\n");
				goto done;
			}
		}
	} else if (type == COUNTRY_POWER_TABLE) {
		if (req_fw_nowait) {
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 32)
			if ((request_firmware_nowait
			     (THIS_MODULE, FW_ACTION_HOTPLUG,
			      country_txpwrlimit, handle->hotplug_device,
			      GFP_KERNEL, handle,
			      woal_request_init_user_conf_callback)) < 0) {
#else
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 13)
			if ((request_firmware_nowait
			     (THIS_MODULE, FW_ACTION_HOTPLUG,
			      country_txpwrlimit, handle->hotplug_device,
			      handle,
			      woal_request_init_user_conf_callback)) < 0) {
#else
			if ((request_firmware_nowait
			     (THIS_MODULE, country_txpwrlimit,
			      handle->hotplug_device, handle,
			      woal_request_init_user_conf_callback)) < 0) {
#endif
#endif
				PRINTM(MERROR,
				       "country txpwrlimit config file request_firmware_nowait() failed\n");
				goto done;
			}
			handle->init_user_conf_wait_flag = MFALSE;
			wait_event_interruptible(handle->init_user_conf_wait_q,
						 handle->
						 init_user_conf_wait_flag);
		} else {
			int status =
				request_firmware(&handle->user_data,
						 country_txpwrlimit,
						 handle->hotplug_device);
			/* File does not exist, skip download */
			if (status == -ENOENT) {
				PRINTM(MIOCTL,
				       "Country power table file does not exist\n");
				LEAVE();
				return MLAN_STATUS_SUCCESS;
			} else if (status) {
				PRINTM(MERROR,
				       "country txpwrlimit config file request_firmware() failed\n");
				goto done;
			}
		}
	} else if (type == INIT_HOSTCMD_CFG_DATA) {
		if (req_fw_nowait) {
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 32)
			if ((request_firmware_nowait
			     (THIS_MODULE, FW_ACTION_HOTPLUG, init_hostcmd_cfg,
			      handle->hotplug_device, GFP_KERNEL, handle,
			      woal_request_init_user_conf_callback)) < 0) {
#else
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 13)
			if ((request_firmware_nowait
			     (THIS_MODULE, FW_ACTION_HOTPLUG, init_hostcmd_cfg,
			      handle->hotplug_device, handle,
			      woal_request_init_user_conf_callback)) < 0) {
#else
			if ((request_firmware_nowait
			     (THIS_MODULE, init_hostcmd_cfg,
			      handle->hotplug_device, handle,
			      woal_request_init_user_conf_callback)) < 0) {
#endif
#endif
				PRINTM(MERROR,
				       "Init hostcmd config file request_firmware_nowait() failed\n");
				goto done;
			}
			handle->init_user_conf_wait_flag = MFALSE;
			wait_event_interruptible(handle->init_user_conf_wait_q,
						 handle->
						 init_user_conf_wait_flag);
		} else {
			if ((request_firmware
			     (&handle->user_data, init_hostcmd_cfg,
			      handle->hotplug_device)) < 0) {
				PRINTM(MERROR,
				       "Init hostcmd config file request_firmware() failed\n");
				goto done;
			}
		}
	}

	if (handle->user_data) {
		cfg_data = (t_u8 *)(handle->user_data)->data;
		len = (handle->user_data)->size;
		if (type == INIT_HOSTCMD_CFG_DATA
		    || type == COUNTRY_POWER_TABLE) {
			if (MLAN_STATUS_SUCCESS !=
			    woal_process_hostcmd_cfg(handle, cfg_data, len,
						     wait_option)) {
				PRINTM(MERROR,
				       "Can't process hostcmd config file\n");
				goto done;
			}
		}
		ret = MLAN_STATUS_SUCCESS;
	} else if (type == INIT_CFG_DATA && handle->init_cfg_data) {
		PRINTM(MIOCTL, "Load init_cfg success\n");
		ret = MLAN_STATUS_SUCCESS;
	}
done:
	if (handle->user_data) {
		release_firmware(handle->user_data);
		handle->user_data = NULL;
	}

	LEAVE();
	return ret;
}

static int woal_netdevice_event(struct notifier_block *nb,
				unsigned long event, void *ptr);

/**
 * @brief Add interfaces DPC
 *
 * @param handle    A pointer to moal_handle structure
 *
 * @return        MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
woal_add_card_dpc(moal_handle *handle)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	int i;
	char str_buf[MLAN_MAX_VER_STR_LEN];
	ENTER();

#ifdef CONFIG_PROC_FS
	/* Initialize proc fs */
	woal_proc_init(handle);
#endif /* CONFIG_PROC_FS */

	/* Add interfaces */
	for (i = 0; i < handle->drv_mode.intf_num; i++) {
		if (handle->drv_mode.bss_attr[i].bss_virtual)
			continue;
		if (!woal_add_interface
		    (handle, handle->priv_num,
		     handle->drv_mode.bss_attr[i].bss_type)) {
			ret = MLAN_STATUS_FAILURE;
			goto err;
		}
	}
	woal_get_version(handle, str_buf, sizeof(str_buf) - 1);
	PRINTM(MMSG, "wlan: version = %s\n", str_buf);

	handle->woal_notifier.notifier_call = woal_netdevice_event;
	if (register_inetaddr_notifier(&handle->woal_notifier)) {
		PRINTM(MFATAL,
		       "Error registering register_inetaddr_notifier\n");
		goto err;
	}
#ifdef MFG_CMD_SUPPORT
	if (mfg_mode == MLAN_INIT_PARA_ENABLED)
		goto done;
#endif

	if (init_cfg && handle->init_cfg_data) {
		if (MLAN_STATUS_SUCCESS !=
		    woal_process_init_cfg(handle,
					  (t_u8 *)(handle->init_cfg_data)->data,
					  (handle->init_cfg_data)->size)) {
			PRINTM(MERROR, "Can't process init config file\n");
			ret = MLAN_STATUS_FAILURE;
			goto err;
		}
	}

	if (pmic) {
		if (MLAN_STATUS_SUCCESS !=
		    woal_pmic_configure(handle, MOAL_IOCTL_WAIT)) {
			PRINTM(MFATAL, "Failed to configure PMIC\n");
			ret = MLAN_STATUS_FAILURE;
			goto err;
		}
	}

#ifdef UAP_SUPPORT
	if (uap_oper_ctrl)
		woal_set_uap_operation_ctrl(handle);
#endif

#ifdef MFG_CMD_SUPPORT
done:
#endif
err:
	if (init_cfg && handle->init_cfg_data) {
		release_firmware(handle->init_cfg_data);
		handle->init_cfg_data = NULL;
	}
	if (ret != MLAN_STATUS_SUCCESS) {
		PRINTM(MERROR, "Failed to add interface\n");
		unregister_inetaddr_notifier(&handle->woal_notifier);

		for (i = 0; i < MIN(MLAN_MAX_BSS_NUM, handle->priv_num); i++)
			woal_remove_interface(handle, i);
		handle->priv_num = 0;
#ifdef CONFIG_PROC_FS
		woal_proc_exit(handle);
#endif
	}
	LEAVE();
	return ret;
}

/**
 * @brief Download and Initialize firmware DPC
 *
 * @param handle    A pointer to moal_handle structure
 *
 * @return        MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
woal_init_fw_dpc(moal_handle *handle)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_fw_image fw;
	t_u8 retry = 0;

	mlan_init_param param;

	ENTER();

	if (handle->firmware) {
		memset(&fw, 0, sizeof(mlan_fw_image));
		fw.pfw_buf = (t_u8 *)handle->firmware->data;
		fw.fw_len = handle->firmware->size;
		if (fw_reload == FW_RELOAD_SDIO_INBAND_RESET)
			fw.fw_reload = fw_reload;
		else
			fw.fw_reload = 0;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 32)
		sdio_claim_host(((struct sdio_mmc_card *)handle->card)->func);
#endif
		ret = mlan_dnld_fw(handle->pmlan_adapter, &fw);
#ifdef MFG_CMD_SUPPORT
		if (mfg_mode == MLAN_INIT_PARA_ENABLED)
			fw_name = NULL;
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 32)
		sdio_release_host(((struct sdio_mmc_card *)handle->card)->func);
#endif
		if (ret == MLAN_STATUS_FAILURE) {
			PRINTM(MERROR,
			       "WLAN: Fail download FW with nowwait: %d\n",
			       req_fw_nowait);
			woal_sdio_reg_dbg(handle);
			goto done;
		}
		PRINTM(MMSG, "WLAN FW is active\n");
	}

    /** data request */
	memset(&param, 0, sizeof(mlan_init_param));

	if (dpd_data_cfg && strncmp(dpd_data_cfg, "none", strlen("none"))) {
		if (req_fw_nowait) {
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 32)
			if ((request_firmware_nowait
			     (THIS_MODULE, FW_ACTION_HOTPLUG, dpd_data_cfg,
			      handle->hotplug_device, GFP_KERNEL, handle,
			      woal_request_init_dpd_conf_callback)) < 0) {
#else
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 13)
			if ((request_firmware_nowait
			     (THIS_MODULE, FW_ACTION_HOTPLUG, dpd_data_cfg,
			      handle->hotplug_device, handle,
			      woal_request_init_dpd_conf_callback)) < 0) {
#else
			if ((request_firmware_nowait
			     (THIS_MODULE, dpd_data_cfg, handle->hotplug_device,
			      handle,
			      woal_request_init_dpd_conf_callback)) < 0) {
#endif
#endif
				PRINTM(MERROR,
				       "DPD data request_firmware_nowait() failed\n");
				ret = MLAN_STATUS_FAILURE;
				goto done;
			}
			handle->init_user_conf_wait_flag = MFALSE;
			wait_event_interruptible(handle->init_user_conf_wait_q,
						 handle->
						 init_user_conf_wait_flag);
		} else {
			if ((request_firmware
			     (&handle->dpd_data, dpd_data_cfg,
			      handle->hotplug_device)) < 0) {
				PRINTM(MERROR,
				       "DPD data request_firmware() failed\n");
				ret = MLAN_STATUS_FAILURE;
				goto done;
			}
		}
		if (handle->dpd_data) {
			param.pdpd_data_buf = (t_u8 *)handle->dpd_data->data;
			param.dpd_data_len = handle->dpd_data->size;
		}
	}
	if (txpwrlimit_cfg && strncmp(txpwrlimit_cfg, "none", strlen("none"))) {
		if (req_fw_nowait) {
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 32)
			if ((request_firmware_nowait
			     (THIS_MODULE, FW_ACTION_HOTPLUG, txpwrlimit_cfg,
			      handle->hotplug_device, GFP_KERNEL, handle,
			      woal_request_init_txpwr_conf_callback)) < 0) {
#else
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 13)
			if ((request_firmware_nowait
			     (THIS_MODULE, FW_ACTION_HOTPLUG, txpwrlimit_cfg,
			      handle->hotplug_device, handle,
			      woal_request_init_txpwr_conf_callback)) < 0) {
#else
			if ((request_firmware_nowait
			     (THIS_MODULE, txpwrlimit_cfg,
			      handle->hotplug_device, handle,
			      woal_request_init_txpwr_conf_callback)) < 0) {
#endif
#endif
				PRINTM(MERROR, "Region txpwrlimit cfg data "
				       "request_firmware_nowait() failed\n");
				ret = MLAN_STATUS_FAILURE;
				goto done;
			}
			handle->init_user_conf_wait_flag = MFALSE;
			wait_event_interruptible(handle->init_user_conf_wait_q,
						 handle->
						 init_user_conf_wait_flag);
		} else {
			if ((request_firmware
			     (&handle->txpwr_data, txpwrlimit_cfg,
			      handle->hotplug_device)) < 0) {
				PRINTM(MERROR,
				       "Region txpwrlimit cfg data "
				       "request_firmware() failed\n");
				ret = MLAN_STATUS_FAILURE;
				goto done;
			}
		}
		if (handle->txpwr_data) {
			param.ptxpwr_data_buf =
				(t_u8 *)handle->txpwr_data->data;
			param.txpwr_data_len = handle->txpwr_data->size;
		}
	}

	if (cal_data_cfg && strncmp(cal_data_cfg, "none", strlen("none"))) {
		if (req_fw_nowait) {
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 32)
			if ((request_firmware_nowait
			     (THIS_MODULE, FW_ACTION_HOTPLUG, cal_data_cfg,
			      handle->hotplug_device, GFP_KERNEL, handle,
			      woal_request_init_user_conf_callback)) < 0) {
#else
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 13)
			if ((request_firmware_nowait
			     (THIS_MODULE, FW_ACTION_HOTPLUG, cal_data_cfg,
			      handle->hotplug_device, handle,
			      woal_request_init_user_conf_callback)) < 0) {
#else
			if ((request_firmware_nowait
			     (THIS_MODULE, cal_data_cfg, handle->hotplug_device,
			      handle,
			      woal_request_init_user_conf_callback)) < 0) {
#endif
#endif
				PRINTM(MERROR,
				       "Cal data request_firmware_nowait() failed\n");
				ret = MLAN_STATUS_FAILURE;
				goto done;
			}
			handle->init_user_conf_wait_flag = MFALSE;
			wait_event_interruptible(handle->init_user_conf_wait_q,
						 handle->
						 init_user_conf_wait_flag);
		} else {
			if ((request_firmware
			     (&handle->user_data, cal_data_cfg,
			      handle->hotplug_device)) < 0) {
				PRINTM(MERROR,
				       "Cal data request_firmware() failed\n");
				ret = MLAN_STATUS_FAILURE;
				goto done;
			}
		}
	} else if (!cal_data_cfg) {
		PRINTM(MERROR,
		       "Please add cal_data_cfg for 8887/8977/8997/8987/9098\n");
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
	if (handle->user_data) {
		param.pcal_data_buf = (t_u8 *)handle->user_data->data;
		param.cal_data_len = handle->user_data->size;
	}

	handle->hardware_status = HardwareStatusFwReady;
	if (ret != MLAN_STATUS_SUCCESS)
		goto done;
	if (handle->fw_reload)
		goto done;
	handle->init_wait_q_woken = MFALSE;

	ret = mlan_set_init_param(handle->pmlan_adapter, &param);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 32)
	sdio_claim_host(((struct sdio_mmc_card *)handle->card)->func);
#endif
	ret = mlan_init_fw(handle->pmlan_adapter);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 32)
	sdio_release_host(((struct sdio_mmc_card *)handle->card)->func);
#endif
	if (ret == MLAN_STATUS_FAILURE)
		goto done;
	else if (ret == MLAN_STATUS_SUCCESS) {
		handle->hardware_status = HardwareStatusReady;
		goto done;
	}
	/* Wait for mlan_init to complete */
	while (wait_event_interruptible(handle->init_wait_q,
					handle->init_wait_q_woken) ==
	       -ERESTARTSYS && retry < MAX_RETRY_CNT) {
		retry++;
	}
	if (handle->hardware_status != HardwareStatusReady) {
		woal_moal_debug_info(woal_get_priv(handle, MLAN_BSS_ROLE_ANY),
				     handle, MTRUE);
#if defined(DEBUG_LEVEL1)
		if (drvdbg & MFW_D) {
			drvdbg &= ~MFW_D;
			woal_dump_firmware_info_v3(handle);
		}
#endif
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
	ret = MLAN_STATUS_SUCCESS;
done:
	if (handle->dpd_data) {
		release_firmware(handle->dpd_data);
		handle->dpd_data = NULL;
	}
	if (handle->txpwr_data) {
		release_firmware(handle->txpwr_data);
		handle->txpwr_data = NULL;
	}
	if (handle->user_data) {
		release_firmware(handle->user_data);
		handle->user_data = NULL;
	}
	LEAVE();
	return ret;
}

/**
 * @brief Request firmware DPC
 *
 * @param handle    A pointer to moal_handle structure
 * @param firmware  A pointer to firmware image
 *
 * @return        MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
woal_request_fw_dpc(moal_handle *handle, const struct firmware *firmware)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	struct timeval tstamp;

	ENTER();

	if (!firmware) {
		do_gettimeofday(&tstamp);
		if (tstamp.tv_sec >
		    (handle->req_fw_time.tv_sec + REQUEST_FW_TIMEOUT)) {
			PRINTM(MERROR,
			       "No firmware image found. Skipping download\n");
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}
		PRINTM(MERROR,
		       "request_firmware_nowait failed for %s. Retrying..\n",
		       handle->drv_mode.fw_name);
		woal_sched_timeout(MOAL_TIMER_1S);
		woal_request_fw(handle);
		LEAVE();
		return ret;
	}
	handle->firmware = firmware;

	ret = woal_init_fw_dpc(handle);
	if (ret)
		goto done;
	ret = woal_add_card_dpc(handle);
	if (ret)
		goto done;

done:
	/* We should hold the semaphore until callback finishes execution */
	MOAL_REL_SEMAPHORE(&AddRemoveCardSem);
	LEAVE();
	return ret;
}

/**
 * @brief Request firmware callback
 *        This function is invoked by request_firmware_nowait system call
 *
 * @param firmware  A pointer to firmware image
 * @param context   A pointer to moal_handle structure
 *
 * @return          N/A
 */
static void
woal_request_fw_callback(const struct firmware *firmware, void *context)
{
	ENTER();
	woal_request_fw_dpc((moal_handle *)context, firmware);
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 32)
	if (firmware)
		release_firmware(firmware);
#endif
	LEAVE();
	return;
}

/**
 * @brief   Download firmware using helper
 *
 * @param handle  A pointer to moal_handle structure
 *
 * @return        MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
woal_request_fw(moal_handle *handle)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	int err;
	t_u32 revision_id = 0;

	ENTER();

	if (!fw_name) {
#define REV_ID_REG      0xc8
/** Revision ID register */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 32)
		sdio_claim_host(((struct sdio_mmc_card *)handle->card)->func);
#endif
		woal_read_reg(handle, REV_ID_REG, &revision_id);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 32)
		sdio_release_host(((struct sdio_mmc_card *)handle->card)->func);
#endif
		switch (revision_id) {
		case SD8977_V0:
			if (fw_serial && !handle->fw_reload && !fw_reload)
				handle->drv_mode.fw_name = SD8977_V0_FW_NAME;
			else
				handle->drv_mode.fw_name =
					SD8977_WLAN_V0_FW_NAME;
			break;
		case SD8977_V1:
			if (fw_serial && !handle->fw_reload && !fw_reload)
				handle->drv_mode.fw_name = SD8977_V1_FW_NAME;
			else
				handle->drv_mode.fw_name =
					SD8977_WLAN_V1_FW_NAME;
			break;
		case SD8977_V2:
			if (fw_serial && !handle->fw_reload && !fw_reload)
				handle->drv_mode.fw_name = SD8977_V2_FW_NAME;
			else
				handle->drv_mode.fw_name =
					SD8977_WLAN_V2_FW_NAME;
			break;
		default:
			break;
		}
	}

	PRINTM(MMSG, "Request firmware: %s\n", handle->drv_mode.fw_name);
	if (req_fw_nowait) {
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 32)
		err = request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
					      handle->drv_mode.fw_name,
					      handle->hotplug_device,
					      GFP_KERNEL, handle,
					      woal_request_fw_callback);
#else
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 13)
		err = request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
					      handle->drv_mode.fw_name,
					      handle->hotplug_device, handle,
					      woal_request_fw_callback);
#else
		err = request_firmware_nowait(THIS_MODULE,
					      handle->drv_mode.fw_name,
					      handle->hotplug_device, handle,
					      woal_request_fw_callback);
#endif
#endif
		if (err < 0) {
			PRINTM(MFATAL,
			       "WLAN: request_firmware_nowait() failed, error code = %d\n",
			       err);
			ret = MLAN_STATUS_FAILURE;
		}
	} else {
		err = request_firmware(&handle->firmware,
				       handle->drv_mode.fw_name,
				       handle->hotplug_device);
		if (err < 0) {
			PRINTM(MFATAL,
			       "WLAN: request_firmware() failed, error code = %d\n",
			       err);
			ret = MLAN_STATUS_FAILURE;
		} else {
			if (handle->fw_reload)
				ret = woal_init_fw_dpc(handle);
			else
				ret = woal_request_fw_dpc(handle,
							  handle->firmware);
			release_firmware(handle->firmware);
		}
	}

	LEAVE();
	return ret;
}

/**
 *  @brief This function initializes firmware
 *
 *  @param handle   A pointer to moal_handle structure
 *
 *  @return         MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
woal_init_fw(moal_handle *handle)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	do_gettimeofday(&handle->req_fw_time);

	ret = woal_request_fw(handle);
	if (ret < 0) {
		PRINTM(MFATAL, "woal_request_fw failed\n");
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

done:
	LEAVE();
	return ret;
}

/**
 *  @brief This function will fill in the mlan_buffer
 *
 *  @param pmbuf   A pointer to mlan_buffer
 *  @param skb     A pointer to struct sk_buff
 *
 *  @return        N/A
 */
static void
woal_fill_mlan_buffer(moal_private *priv,
		      mlan_buffer *pmbuf, struct sk_buff *skb)
{
	struct timeval tstamp;
	struct ethhdr *eth;
	t_u8 tid;
	dot11_txcontrol *txcontrol;
	t_u8 tx_ctrl_flag = MFALSE;
	int i = 0;
	ENTER();
	/*
	 * skb->priority values from 256->263 are magic values to
	 * directly indicate a specific 802.1d priority.  This is used
	 * to allow 802.1d priority to be passed directly in from VLAN
	 * tags, etc.
	 */
	if (IS_SKB_MAGIC_VLAN(skb)) {
		tid = GET_VLAN_PRIO(skb);
	} else {
		eth = (struct ethhdr *)skb->data;

		switch (eth->h_proto) {
		case __constant_htons(ETH_P_IP):
			tid = priv->dscp_map[SKB_TOS(skb) >> DSCP_OFFSET];
			if (tid == 0xFF)
				tid = (IPTOS_PREC(SKB_TOS(skb)) >>
				       IPTOS_OFFSET);
			PRINTM(MDAT_D,
			       "packet type ETH_P_IP: dscp[%x], map[%x], tid=%d\n",
			       SKB_TOS(skb) >> DSCP_OFFSET,
			       priv->dscp_map[SKB_TOS(skb) >> DSCP_OFFSET],
			       tid);
			break;
		case __constant_htons(ETH_P_IPV6):
			tid = SKB_TIDV6(skb);
			PRINTM(MDAT_D,
			       "packet type ETH_P_IPV6: %04x, tid=%#x prio=%#x\n",
			       eth->h_proto, tid, skb->priority);
			break;
		case __constant_htons(ETH_P_ARP):
			tid = 0;
			PRINTM(MDATA, "ARP packet %04x\n", eth->h_proto);
			break;
		default:
			tid = 0;
			if (priv->tx_protocols.protocol_num) {
				for (i = 0; i < priv->tx_protocols.protocol_num;
				     i++) {
					if (eth->h_proto ==
					    __constant_htons(priv->tx_protocols.
							     protocols[i]))
						tx_ctrl_flag = MTRUE;
				}
			}
			if (tx_ctrl_flag) {
				txcontrol =
					(dot11_txcontrol *) (skb->data +
							     sizeof(struct
								    ethhdr));
				pmbuf->u.tx_info.data_rate =
					txcontrol->datarate;
				pmbuf->u.tx_info.channel = txcontrol->channel;
				pmbuf->u.tx_info.bw = txcontrol->bw;
				pmbuf->u.tx_info.tx_power.val =
					txcontrol->power;
				pmbuf->u.tx_info.retry_limit =
					txcontrol->retry_limit;
				tid = txcontrol->priority;
				memmove(skb->data + sizeof(dot11_txcontrol),
					skb->data, sizeof(struct ethhdr));
				skb_pull(skb, sizeof(dot11_txcontrol));
				pmbuf->flags |= MLAN_BUF_FLAG_TX_CTRL;
			}
			break;
		}
	}

	skb->priority = tid;

	/* Record the current time the packet was queued; used to determine
	 *   the amount of time the packet was queued in the driver before it
	 *   was sent to the firmware.  The delay is then sent along with the
	 *   packet to the firmware for aggregate delay calculation for stats
	 *   and MSDU lifetime expiry.
	 */
	do_gettimeofday(&tstamp);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 22)
	skb->tstamp = timeval_to_ktime(tstamp);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 14)
	skb_set_timestamp(skb, &tstamp);
#else
	memcpy(&skb->stamp, &tstamp, sizeof(skb->stamp));
#endif

	pmbuf->pdesc = skb;
	pmbuf->pbuf = skb->head + sizeof(mlan_buffer);
	pmbuf->data_offset = skb->data - (skb->head + sizeof(mlan_buffer));
	pmbuf->data_len = skb->len;
	pmbuf->priority = skb->priority;
	pmbuf->buf_type = 0;
	pmbuf->in_ts_sec = (t_u32)tstamp.tv_sec;
	pmbuf->in_ts_usec = (t_u32)tstamp.tv_usec;

	LEAVE();
	return;
}

/**
 * @brief This function opens the network device for monitor interface
 *
 * @param dev             A pointer to net_device structure
 *
 * @return                0 -- success, otherwise fail
 */
int
woal_mon_open(struct net_device *ndev)
{
	ENTER();
	LEAVE();
	return 0;
}

/**
 * @brief This function closes the network device for monitor interface
 *
 * @param dev             A pointer to net_device structure
 *
 * @return                0 -- success, otherwise fail
 */
int
woal_mon_close(struct net_device *ndev)
{
	ENTER();
	LEAVE();
	return 0;
}

/**
 * @brief This function sets the MAC address to firmware for monitor interface
 *
 * @param dev             A pointer to net_device structure
 * @param addr            MAC address to set
 *
 * @return                0 -- success, otherwise fail
 */
int
woal_mon_set_mac_address(struct net_device *ndev, void *addr)
{
	ENTER();
	LEAVE();
	return 0;
}

/**
 * @brief This function sets multicast address to firmware for monitor interface
 *
 * @param dev             A pointer to net_device structure
 *
 * @return                0 -- success, otherwise fail
 */
void
woal_mon_set_multicast_list(struct net_device *ndev)
{
	ENTER();
	LEAVE();
}

/**
 * @brief This function handles packet transmission for monitor interface
 *
 * @param skb             A pointer to sk_buff structure
 * @param dev             A pointer to net_device structure
 *
 * @return                0 -- success, otherwise fail
 */
int
woal_mon_hard_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	int len_rthdr;
	int qos_len = 0;
	int dot11_hdr_len = 24;
	int snap_len = 6;
	unsigned char *pdata;
	unsigned short fc;
	unsigned char src_mac_addr[6];
	unsigned char dst_mac_addr[6];
	struct ieee80211_hdr *dot11_hdr;
	struct ieee80211_radiotap_header *prthdr =
		(struct ieee80211_radiotap_header *)skb->data;
	monitor_iface *mon_if = netdev_priv(ndev);

	ENTER();

	if (mon_if == NULL || mon_if->base_ndev == NULL) {
		goto fail;
	}

	/* check for not even having the fixed radiotap header part */
	if (unlikely(skb->len < sizeof(struct ieee80211_radiotap_header))) {
		PRINTM(MERROR, "Invalid radiotap hdr length,"
		       "skb->len: %d\n", skb->len);
		goto fail;	/* too short to be possibly valid */
	}

	/* is it a header version we can trust to find length from? */
	if (unlikely(prthdr->it_version))
		goto fail;	/* only version 0 is supported */

	/* then there must be a radiotap header with a length we can use */
	len_rthdr = ieee80211_get_radiotap_len(skb->data);

	/* does the skb contain enough to deliver on the alleged length? */
	if (unlikely(skb->len < len_rthdr)) {
		PRINTM(MERROR, "Invalid data length,"
		       "skb->len: %d\n", skb->len);
		goto fail;	/* skb too short for claimed rt header extent */
	}

	/* Skip the ratiotap header */
	skb_pull(skb, len_rthdr);

	dot11_hdr = (struct ieee80211_hdr *)skb->data;
	fc = le16_to_cpu(dot11_hdr->frame_control);
	if ((fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_DATA) {
		/* Check if this ia a Wireless Distribution System (WDS) frame
		 * which has 4 MAC addresses
		 */
		if (dot11_hdr->frame_control & 0x0080)
			qos_len = 2;
		if ((dot11_hdr->frame_control & 0x0300) == 0x0300)
			dot11_hdr_len += 6;

		memcpy(dst_mac_addr, dot11_hdr->addr1, sizeof(dst_mac_addr));
		memcpy(src_mac_addr, dot11_hdr->addr2, sizeof(src_mac_addr));

		/* Skip the 802.11 header, QoS (if any) and SNAP, but leave spaces for
		 * for two MAC addresses
		 */
		skb_pull(skb,
			 dot11_hdr_len + qos_len + snap_len -
			 sizeof(src_mac_addr) * 2);
		pdata = (unsigned char *)skb->data;
		memcpy(pdata, dst_mac_addr, sizeof(dst_mac_addr));
		memcpy(pdata + sizeof(dst_mac_addr), src_mac_addr,
		       sizeof(src_mac_addr));

		LEAVE();
		return woal_hard_start_xmit(skb, mon_if->base_ndev);
	}

fail:
	dev_kfree_skb(skb);
	LEAVE();
	return NETDEV_TX_OK;
}

/**
 *  @brief This function returns the network statistics
 *
 *  @param dev     A pointer to net_device structure
 *
 *  @return        A pointer to net_device_stats structure
 */
struct net_device_stats *
woal_mon_get_stats(struct net_device *dev)
{
	monitor_iface *mon_if = (monitor_iface *)netdev_priv(dev);
	return &mon_if->stats;
}

static const struct net_device_ops woal_cfg80211_mon_if_ops = {
	.ndo_open = woal_mon_open,
	.ndo_start_xmit = woal_mon_hard_start_xmit,
	.ndo_stop = woal_mon_close,
	.ndo_get_stats = woal_mon_get_stats,
	.ndo_set_mac_address = woal_mon_set_mac_address,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0)
	.ndo_set_rx_mode = woal_mon_set_multicast_list,
#else
	.ndo_set_multicast_list = woal_mon_set_multicast_list,
#endif
};

/**
 * @brief This function setup monitor interface
 *
 * @param dev             A pointer to net_device structure
 * @param addr            MAC address to set
 *
 * @return                0 -- success, otherwise fail
 */

void
woal_mon_if_setup(struct net_device *dev)
{
	ENTER();
	ether_setup(dev);
	dev->netdev_ops = &woal_cfg80211_mon_if_ops;
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 11, 9)
	dev->needs_free_netdev = true;
#else
	dev->destructor = free_netdev;
#endif
	LEAVE();
}

/**
 * @brief Request the driver to add a monitor interface
 *
 * @param priv             A pointer to moal_private
 * @param name              Virtual interface name
 * @param name_assign_type  Interface name assignment type
 * @param sniffer_mode           Sniffer mode
 *
 * @return                  A pointer to monitor_iface
 */
monitor_iface *
woal_prepare_mon_if(moal_private *priv,
		    const char *name,
		    unsigned char name_assign_type, int sniffer_mode)
{
	int ret = 0;
	moal_handle *handle = priv->phandle;
	struct net_device *ndev = NULL;
	monitor_iface *mon_if = NULL;

	ENTER();

	if (sniffer_mode != CHANNEL_SPEC_SNIFFER_MODE) {
		PRINTM(MERROR, "Sniffer mode is not valid\n");
		ret = -EFAULT;
		goto fail;
	}
	if ((sniffer_mode == CHANNEL_SPEC_SNIFFER_MODE) &&
	    woal_is_any_interface_active(handle)) {
		PRINTM(MERROR,
		       "Cannot start channel specified net monitor when Interface Active\n");
		ret = -EFAULT;
		goto fail;
	}
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 29)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)
	ndev = alloc_netdev_mq(sizeof(*mon_if), name, name_assign_type,
			       woal_mon_if_setup, 1);
#else
	ndev = alloc_netdev_mq(sizeof(*mon_if), name, NET_NAME_UNKNOWN,
			       woal_mon_if_setup, 1);
#endif
#else
	ndev = alloc_netdev_mq(sizeof(*mon_if), name, woal_mon_if_setup, 1);
#endif
#else
	ndev = alloc_netdev_mq(sizeof(*mon_if), name, woal_mon_if_setup);
#endif
	if (!ndev) {
		PRINTM(MFATAL, "Init virtual ethernet device failed\n");
		ret = -EFAULT;
		goto fail;
	}

	ret = dev_alloc_name(ndev, ndev->name);
	if (ret < 0) {
		PRINTM(MFATAL, "Net device alloc name fail.\n");
		ret = -EFAULT;
		goto fail;
	}
	//?memcpy(ndev->dev_addr, ndev->perm_addr, ETH_ALEN);

	mon_if = netdev_priv(ndev);
	memcpy(mon_if->ifname, ndev->name, IFNAMSIZ);

	ndev->type = ARPHRD_IEEE80211_RADIOTAP;
	ndev->netdev_ops = &woal_cfg80211_mon_if_ops;

	mon_if->priv = priv;
	mon_if->mon_ndev = ndev;
	mon_if->base_ndev = priv->netdev;
	mon_if->sniffer_mode = sniffer_mode;
	mon_if->radiotap_enabled = 1;
	mon_if->flag = 1;

fail:
	if (ret) {
		if (ndev)
			free_netdev(ndev);
		LEAVE();
		return NULL;
	}

	LEAVE();
	return mon_if;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 33)
static struct device_type wlan_type = {.name = "wlan", };
#endif

#ifdef STA_SUPPORT
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 29)
/** Network device handlers */
const struct net_device_ops woal_netdev_ops = {
	.ndo_open = woal_open,
	.ndo_start_xmit = woal_hard_start_xmit,
	.ndo_stop = woal_close,
	.ndo_do_ioctl = woal_do_ioctl,
	.ndo_set_mac_address = woal_set_mac_address,
	.ndo_tx_timeout = woal_tx_timeout,
	.ndo_get_stats = woal_get_stats,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0)
	.ndo_set_rx_mode = woal_set_multicast_list,
#else
	.ndo_set_multicast_list = woal_set_multicast_list,
#endif
	.ndo_select_queue = woal_select_queue,
	.ndo_validate_addr = eth_validate_addr,
};
#endif

/**
 *  @brief This function initializes the private structure
 *          and dev structure for station mode
 *
 *  @param dev      A pointer to net_device structure
 *  @param priv     A pointer to moal_private structure
 *
 *  @return         MLAN_STATUS_SUCCESS
 */
mlan_status
woal_init_sta_dev(struct net_device *dev, moal_private *priv)
{
	ENTER();

	/* Setup the OS Interface to our functions */
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 29)
	dev->open = woal_open;
	dev->hard_start_xmit = woal_hard_start_xmit;
	dev->stop = woal_close;
	dev->do_ioctl = woal_do_ioctl;
	dev->set_mac_address = woal_set_mac_address;
	dev->tx_timeout = woal_tx_timeout;
	dev->get_stats = woal_get_stats;
	dev->set_multicast_list = woal_set_multicast_list;
#else
	dev->netdev_ops = &woal_netdev_ops;
#endif
	dev->watchdog_timeo = MRVDRV_DEFAULT_WATCHDOG_TIMEOUT;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0)
	dev->needed_headroom += MLAN_MIN_DATA_HEADER_LEN + sizeof(mlan_buffer)
		+ priv->extra_tx_head_len;
#else
	dev->hard_header_len += MLAN_MIN_DATA_HEADER_LEN + sizeof(mlan_buffer)
		+ priv->extra_tx_head_len;
#endif
#ifdef STA_WEXT
	if (IS_STA_WEXT(cfg80211_wext)) {
#if WIRELESS_EXT < 21
		dev->get_wireless_stats = woal_get_wireless_stats;
#endif
		dev->wireless_handlers =
			(struct iw_handler_def *)&woal_handler_def;
	}
#endif
	dev->flags |= IFF_BROADCAST | IFF_MULTICAST;

#ifdef STA_CFG80211
	if (IS_STA_CFG80211(cfg80211_wext))
		init_waitqueue_head(&priv->ft_wait_q);
#endif
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}
#endif /* STA_SUPPORT */

#ifdef UAP_SUPPORT
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 29)
/** Network device handlers */
const struct net_device_ops woal_uap_netdev_ops = {
	.ndo_open = woal_open,
	.ndo_start_xmit = woal_hard_start_xmit,
	.ndo_stop = woal_close,
	.ndo_do_ioctl = woal_uap_do_ioctl,
	.ndo_set_mac_address = woal_set_mac_address,
	.ndo_tx_timeout = woal_tx_timeout,
	.ndo_get_stats = woal_get_stats,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 2, 0)
	.ndo_set_rx_mode = woal_uap_set_multicast_list,
#else
	.ndo_set_multicast_list = woal_uap_set_multicast_list,
#endif
	.ndo_select_queue = woal_select_queue,
	.ndo_validate_addr = eth_validate_addr,
};
#endif

/**
 *  @brief This function initializes the private structure
 *          and dev structure for uap mode
 *
 *  @param dev      A pointer to net_device structure
 *  @param priv     A pointer to moal_private structure
 *
 *  @return         MLAN_STATUS_SUCCESS
 */
mlan_status
woal_init_uap_dev(struct net_device *dev, moal_private *priv)
{
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	/* Setup the OS Interface to our functions */
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 29)
	dev->open = woal_open;
	dev->hard_start_xmit = woal_hard_start_xmit;
	dev->stop = woal_close;
	dev->set_mac_address = woal_set_mac_address;
	dev->tx_timeout = woal_tx_timeout;
	dev->get_stats = woal_get_stats;
	dev->do_ioctl = woal_uap_do_ioctl;
	dev->set_multicast_list = woal_uap_set_multicast_list;
#else
	dev->netdev_ops = &woal_uap_netdev_ops;
#endif
	dev->watchdog_timeo = MRVDRV_DEFAULT_UAP_WATCHDOG_TIMEOUT;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0)
	dev->needed_headroom += MLAN_MIN_DATA_HEADER_LEN + sizeof(mlan_buffer)
		+ priv->extra_tx_head_len;
#else
	dev->hard_header_len += MLAN_MIN_DATA_HEADER_LEN + sizeof(mlan_buffer)
		+ priv->extra_tx_head_len;
#endif
#ifdef UAP_WEXT
	if (IS_UAP_WEXT(cfg80211_wext)) {
#if WIRELESS_EXT < 21
		dev->get_wireless_stats = woal_get_uap_wireless_stats;
#endif
		dev->wireless_handlers =
			(struct iw_handler_def *)&woal_uap_handler_def;
	}
#endif /* UAP_WEXT */
	dev->flags |= IFF_BROADCAST | IFF_MULTICAST;

	LEAVE();
	return status;
}
#endif /* UAP_SUPPORT */

/**
 * @brief This function adds a new interface. It will
 *      allocate, initialize and register the device.
 *
 *  @param handle    A pointer to moal_handle structure
 *  @param bss_index BSS index number
 *  @param bss_type  BSS type
 *
 *  @return          A pointer to the new priv structure
 */
moal_private *
woal_add_interface(moal_handle *handle, t_u8 bss_index, t_u8 bss_type)
{
	struct net_device *dev = NULL;
	moal_private *priv = NULL;
	char name[256];
	int i = 0;
#ifdef UAP_CFG80211
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
	char csa_str[256];
#endif
#endif
	ENTER();

#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 29)
#define MAX_WMM_QUEUE   4
	/* Allocate an Ethernet device */
	dev = alloc_etherdev_mq(sizeof(moal_private), MAX_WMM_QUEUE);
#else
	dev = alloc_etherdev(sizeof(moal_private));
#endif
	if (!dev) {
		PRINTM(MFATAL, "Init virtual ethernet device failed\n");
		goto error;
	}
	/* Allocate device name */
#ifdef STA_SUPPORT
	memset(name, 0, sizeof(name));
	if (sta_name)
		snprintf(name, sizeof(name), "%s%%d", sta_name);
	else
		sprintf(name, default_mlan_name);
	if ((bss_type == MLAN_BSS_TYPE_STA) && (dev_alloc_name(dev, name) < 0)) {
		PRINTM(MERROR, "Could not allocate mlan device name\n");
		goto error;
	}
#endif
#ifdef UAP_SUPPORT
	memset(name, 0, sizeof(name));
	if (uap_name)
		snprintf(name, sizeof(name), "%s%%d", uap_name);
	else
		sprintf(name, default_uap_name);
	if ((bss_type == MLAN_BSS_TYPE_UAP) && (dev_alloc_name(dev, name) < 0)) {
		PRINTM(MERROR, "Could not allocate uap device name\n");
		goto error;
	}
#endif
#if defined(WIFI_DIRECT_SUPPORT)
	memset(name, 0, sizeof(name));
	if (wfd_name)
		snprintf(name, sizeof(name), "%s%%d", wfd_name);
	else
		sprintf(name, default_wfd_name);
	if ((bss_type == MLAN_BSS_TYPE_WIFIDIRECT) &&
	    (dev_alloc_name(dev, name) < 0)) {
		PRINTM(MERROR, "Could not allocate wifidirect device name\n");
		goto error;
	}
#endif
	memset(name, 0, sizeof(name));
	if (nan_name)
		snprintf(name, sizeof(name), "%s%%d", nan_name);
	else
		sprintf(name, default_nan_name);
	if ((bss_type == MLAN_BSS_TYPE_NAN) && (dev_alloc_name(dev, name) < 0)) {
		PRINTM(MERROR, "Could not allocate nan device name\n");
		goto error;
	}
	priv = (moal_private *)netdev_priv(dev);
	/* Save the priv to handle */
	handle->priv[bss_index] = priv;

	/* Use the same handle structure */
	priv->phandle = handle;
	priv->netdev = dev;
	priv->bss_index = bss_index;
	priv->bss_type = bss_type;
	priv->extra_tx_head_len = 0;
	if (bss_type == MLAN_BSS_TYPE_STA)
		priv->bss_role = MLAN_BSS_ROLE_STA;
	else if (bss_type == MLAN_BSS_TYPE_UAP)
		priv->bss_role = MLAN_BSS_ROLE_UAP;
#if defined(WIFI_DIRECT_SUPPORT)
	else if (bss_type == MLAN_BSS_TYPE_WIFIDIRECT)
		priv->bss_role = MLAN_BSS_ROLE_STA;
#endif
	else if (bss_type == MLAN_BSS_TYPE_NAN)
		priv->bss_role = MLAN_BSS_ROLE_STA;

	INIT_LIST_HEAD(&priv->tcp_sess_queue);
	spin_lock_init(&priv->tcp_sess_lock);
#ifdef STA_SUPPORT
	INIT_LIST_HEAD(&priv->tdls_list);
	spin_lock_init(&priv->tdls_lock);
#endif

	INIT_LIST_HEAD(&priv->tx_stat_queue);
	spin_lock_init(&priv->tx_stat_lock);
#ifdef STA_CFG80211
#ifdef STA_SUPPORT
	spin_lock_init(&priv->connect_lock);
#endif
#endif

#ifdef STA_SUPPORT
	INIT_LIST_HEAD(&priv->pmksa_cache_list);
	if (bss_type == MLAN_BSS_TYPE_STA) {
		init_waitqueue_head(&priv->okc_wait_q);
		spin_lock_init(&priv->pmksa_list_lock);
		priv->okc_roaming_ie = NULL;
		priv->okc_ie_len = 0;
	}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 24)
	SET_MODULE_OWNER(dev);
#endif
#ifdef STA_SUPPORT
	if (bss_type == MLAN_BSS_TYPE_STA
#if defined(WIFI_DIRECT_SUPPORT)
	    || bss_type == MLAN_BSS_TYPE_WIFIDIRECT
#endif
	    || bss_type == MLAN_BSS_TYPE_NAN)
		woal_init_sta_dev(dev, priv);
#endif
#ifdef UAP_SUPPORT
	if (bss_type == MLAN_BSS_TYPE_UAP) {
		if (MLAN_STATUS_SUCCESS != woal_init_uap_dev(dev, priv))
			goto error;
	}
#endif
	if (!handle->priv_num
#ifdef MFG_CMD_SUPPORT
	    && (mfg_mode != MLAN_INIT_PARA_ENABLED)
#endif
		) {
		if (init_cfg) {
			if (MLAN_STATUS_SUCCESS !=
			    woal_set_user_init_data(handle, INIT_CFG_DATA,
						    MOAL_IOCTL_WAIT)) {
				PRINTM(MFATAL,
				       "Set user init data and param failed\n");
				goto error;
			}
		}
		if (init_hostcmd_cfg) {
			if (MLAN_STATUS_SUCCESS !=
			    woal_set_user_init_data(handle,
						    INIT_HOSTCMD_CFG_DATA,
						    MOAL_IOCTL_WAIT)) {
				PRINTM(MFATAL,
				       "Set user init hostcmd data and param failed\n");
				goto error;
			}
		}
	}

	handle->priv_num++;
#if defined(STA_CFG80211) || defined(UAP_CFG80211)
	if (!priv->phandle->wiphy && IS_STA_OR_UAP_CFG80211(cfg80211_wext)) {
		if (woal_register_cfg80211(priv)) {
			PRINTM(MERROR, "Cannot register with cfg80211\n");
			goto error;
		}
	}
#endif

#ifdef STA_CFG80211
#ifdef STA_SUPPORT
	if ((priv->bss_role == MLAN_BSS_ROLE_STA) &&
	    IS_STA_CFG80211(cfg80211_wext)) {
		if (bss_type == MLAN_BSS_TYPE_STA
#if defined(WIFI_DIRECT_SUPPORT)
#if CFG80211_VERSION_CODE >= WIFI_DIRECT_KERNEL_VERSION
		    || bss_type == MLAN_BSS_TYPE_WIFIDIRECT
#endif
#endif
		    || bss_type == MLAN_BSS_TYPE_NAN)
			/* Register cfg80211 for STA or Wifi direct */
			if (woal_register_sta_cfg80211(dev, bss_type)) {
				PRINTM(MERROR,
				       "Cannot register STA with cfg80211\n");
				goto error;
			}
	}
#endif /* STA_SUPPORT */
#endif /* STA_CFG80211 */
#ifdef UAP_CFG80211
#ifdef UAP_SUPPORT
	if ((priv->bss_role == MLAN_BSS_ROLE_UAP) &&
	    IS_UAP_CFG80211(cfg80211_wext)) {
		/* Register cfg80211 for UAP */
		if (woal_register_uap_cfg80211(dev, bss_type)) {
			PRINTM(MERROR, "Cannot register UAP with cfg80211\n");
			goto error;
		}
	}
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
	strcpy(csa_str, "CSA");
	strcat(csa_str, name);
	priv->csa_workqueue =
		alloc_workqueue(csa_str,
				WQ_HIGHPRI | WQ_MEM_RECLAIM | WQ_UNBOUND, 1);
	if (!priv->csa_workqueue) {
		PRINTM(MERROR, "cannot alloc csa workqueue \n");
		goto error;
	}
	INIT_DELAYED_WORK(&priv->csa_work, woal_csa_work_queue);
#endif
#endif
#endif /*UAP_CFG80211 */

	/* Initialize priv structure */
	woal_init_priv(priv, MOAL_IOCTL_WAIT);

#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 29)
	SET_NETDEV_DEV(dev, handle->hotplug_device);
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 33)
	SET_NETDEV_DEVTYPE(dev, &wlan_type);
#endif

	/* Register network device */
	if (register_netdev(dev)) {
		PRINTM(MERROR, "Cannot register virtual network device\n");
		goto error;
	}
	netif_carrier_off(dev);
	woal_stop_queue(dev);

	PRINTM(MINFO, "%s: Marvell 802.11 Adapter\n", dev->name);
	/* Set MAC address from the insmod command line */
	if (handle->set_mac_addr) {
		memset(priv->current_addr, 0, ETH_ALEN);
		memcpy(priv->current_addr, handle->mac_addr, ETH_ALEN);
#if defined(WIFI_DIRECT_SUPPORT)
#if defined(STA_CFG80211) && defined(UAP_CFG80211)
#if CFG80211_VERSION_CODE >= WIFI_DIRECT_KERNEL_VERSION
		if (priv->bss_type == MLAN_BSS_TYPE_WIFIDIRECT) {
			priv->current_addr[0] |= 0x02;
			PRINTM(MCMND, "Set WFD device addr: " MACSTR "\n",
			       MAC2STR(priv->current_addr));
		}
#endif
#endif
#endif

		if (MLAN_STATUS_SUCCESS != woal_request_set_mac_address(priv)) {
			PRINTM(MERROR, "Set MAC address failed\n");
			goto error;
		}
		memcpy(dev->dev_addr, priv->current_addr, ETH_ALEN);
	}
	/* Set MAC address for UAPx/MLANx/WFDx and let them different with each other */
	if (priv->bss_index > 0
#ifdef WIFI_DIRECT_SUPPORT
	    && priv->bss_type != MLAN_BSS_TYPE_WIFIDIRECT
#endif
		) {
		priv->current_addr[4] += priv->bss_index;
		woal_request_set_mac_address(priv);
		memcpy(dev->dev_addr, priv->current_addr, ETH_ALEN);
		PRINTM(MCMND, "Set %s interface addr: " MACSTR "\n", dev->name,
		       MAC2STR(priv->current_addr));
	}
	if (bss_type == MLAN_BSS_TYPE_STA ||
	    priv->bss_type == MLAN_BSS_TYPE_UAP) {
		mlan_fw_info fw_info;
		woal_request_get_fw_info(priv, MOAL_IOCTL_WAIT, &fw_info);

		for (i = 0; i < handle->histogram_table_num; i++) {
			priv->hist_data[i] = kmalloc(sizeof(hgm_data) +
						     RX_RATE_MAX *
						     sizeof(atomic_t)
						     , GFP_KERNEL);
			if (!(priv->hist_data[i])) {
				PRINTM(MERROR,
				       "kmalloc priv->hist_data[%d] failed\n",
				       i);
				goto error;
			}
		}
		if (priv->hist_data)
			woal_hist_data_reset(priv);
	}
#ifdef CONFIG_PROC_FS
	woal_create_proc_entry(priv);
#ifdef PROC_DEBUG
	woal_debug_entry(priv);
#endif /* PROC_DEBUG */
#endif /* CONFIG_PROC_FS */

	LEAVE();
	return priv;
error:
	handle->priv_num = bss_index;
#if defined(STA_CFG80211) || defined(UAP_CFG80211)
	/* Unregister wiphy device and free */
	if (priv) {
		if (priv->wdev && IS_STA_OR_UAP_CFG80211(cfg80211_wext))
			priv->wdev = NULL;
#ifdef UAP_CFG80211
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
		if (priv->csa_workqueue) {
			destroy_workqueue(priv->csa_workqueue);
			priv->csa_workqueue = NULL;
		}
#endif
#endif
	}
#endif
	if (dev && dev->reg_state == NETREG_REGISTERED)
		unregister_netdev(dev);
	if (dev)
		free_netdev(dev);
	LEAVE();
	return NULL;
}

/**
 *  @brief This function removes an interface.
 *
 *  @param handle       A pointer to the moal_handle structure
 *  @param bss_index    BSS index number
 *
 *  @return             N/A
 */
void
woal_remove_interface(moal_handle *handle, t_u8 bss_index)
{
	struct net_device *dev = NULL;
	moal_private *priv = handle->priv[bss_index];
#if defined(STA_WEXT) || defined(UAP_WEXT)
	union iwreq_data wrqu;
#endif
	int i = 0;

	ENTER();
	if (!priv || !priv->netdev)
		goto error;
	dev = priv->netdev;

	if (priv->media_connected == MTRUE) {
		priv->media_connected = MFALSE;
#if defined(STA_WEXT) || defined(UAP_WEXT)
		if (IS_STA_OR_UAP_WEXT(cfg80211_wext) &&
		    GET_BSS_ROLE(priv) == MLAN_BSS_ROLE_STA) {
			memset(wrqu.ap_addr.sa_data, 0x00, ETH_ALEN);
			wrqu.ap_addr.sa_family = ARPHRD_ETHER;
			wireless_send_event(priv->netdev, SIOCGIWAP, &wrqu,
					    NULL);
		}
#endif
	}
	woal_flush_tcp_sess_queue(priv);

	woal_flush_tx_stat_queue(priv);

	if (priv->bss_type == MLAN_BSS_TYPE_STA)
		woal_flush_tdls_list(priv);
#ifdef STA_CFG80211
	if (priv->bss_type == MLAN_BSS_TYPE_STA &&
	    IS_STA_CFG80211(cfg80211_wext)) {
		woal_flush_pmksa_list(priv);
		if (priv->okc_roaming_ie) {
			kfree(priv->okc_roaming_ie);
			priv->okc_roaming_ie = NULL;
			priv->okc_ie_len = 0;
		}
	}
#endif

	if (priv->bss_type == MLAN_BSS_TYPE_STA ||
	    priv->bss_type == MLAN_BSS_TYPE_UAP) {
		for (i = 0; i < handle->histogram_table_num; i++) {
			kfree(priv->hist_data[i]);
			priv->hist_data[i] = NULL;
		}
	}
#ifdef CONFIG_PROC_FS
#ifdef PROC_DEBUG
	/* Remove proc debug */
	woal_debug_remove(priv);
#endif /* PROC_DEBUG */
	woal_proc_remove(priv);
#endif /* CONFIG_PROC_FS */
	/* Last reference is our one */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 37)
	PRINTM(MINFO, "refcnt = %d\n", atomic_read(&dev->refcnt));
#else
	PRINTM(MINFO, "refcnt = %d\n", netdev_refcnt_read(dev));
#endif

	PRINTM(MINFO, "netdev_finish_unregister: %s\n", dev->name);

	if (dev->reg_state == NETREG_REGISTERED)
		unregister_netdev(dev);

#if defined(STA_CFG80211) || defined(UAP_CFG80211)
	/* Unregister wiphy device and free */
	if (priv->wdev && IS_STA_OR_UAP_CFG80211(cfg80211_wext))
		priv->wdev = NULL;
#ifdef UAP_CFG80211
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
	if (priv->csa_workqueue) {
		flush_workqueue(priv->csa_workqueue);
		destroy_workqueue(priv->csa_workqueue);
		priv->csa_workqueue = NULL;
	}
#endif
#endif
#endif

	/* Clear the priv in handle */
	priv->phandle->priv[priv->bss_index] = NULL;
	priv->phandle = NULL;
	priv->netdev = NULL;
	free_netdev(dev);
error:
	LEAVE();
	return;
}

/**
 *  @brief Configure pmic in firmware
 *
 *  @param handle    A pointer to moal_handle
 *  @param wait_option  Wait option
 *
 *  @return             MLAN_STATUS_SUCCESS/MLAN_STATUS_PENDING -- success,
 *                          otherwise fail
 */
mlan_status
woal_pmic_configure(moal_handle *handle, t_u8 wait_option)
{
	moal_private *priv = NULL;
	mlan_ioctl_req *req = NULL;
	mlan_ds_misc_cfg *misc = NULL;
	mlan_status status;

	ENTER();

	priv = woal_get_priv(handle, MLAN_BSS_ROLE_ANY);
	if (!priv) {
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	/* Allocate an IOCTL request buffer */
	req = (mlan_ioctl_req *)
		woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_misc_cfg));
	if (req == NULL) {
		status = MLAN_STATUS_FAILURE;
		goto done;
	}

	/* Fill request buffer */
	misc = (mlan_ds_misc_cfg *)req->pbuf;
	misc->sub_command = MLAN_OID_MISC_PMIC_CFG;
	req->req_id = MLAN_IOCTL_MISC_CFG;
	req->action = MLAN_ACT_SET;

	/* Send IOCTL request to MLAN */
	status = woal_request_ioctl(priv, req, wait_option);
done:
	kfree(req);
	LEAVE();
	return status;
}

/**
 *  @brief Send FW shutdown command to MLAN
 *
 *  @param priv         A pointer to moal_private structure
 *  @param wait_option  Wait option
 *
 *  @return             MLAN_STATUS_SUCCESS/MLAN_STATUS_PENDING -- success,
 *                          otherwise fail
 */
static mlan_status
woal_shutdown_fw(moal_private *priv, t_u8 wait_option)
{
	mlan_ioctl_req *req = NULL;
	mlan_ds_misc_cfg *misc = NULL;
	mlan_status status;

	ENTER();

	/* Allocate an IOCTL request buffer */
	req = (mlan_ioctl_req *)
		woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_misc_cfg));
	if (req == NULL) {
		status = MLAN_STATUS_FAILURE;
		goto done;
	}

	/* Fill request buffer */
	misc = (mlan_ds_misc_cfg *)req->pbuf;
	misc->sub_command = MLAN_OID_MISC_INIT_SHUTDOWN;
	misc->param.func_init_shutdown = MLAN_FUNC_SHUTDOWN;
	req->req_id = MLAN_IOCTL_MISC_CFG;
	req->action = MLAN_ACT_SET;

	/* Send IOCTL request to MLAN */
	status = woal_request_ioctl(priv, req, wait_option);
	/* add 100 ms delay to avoid back to back init/shutdown */
	mdelay(100);
done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return status;
}

/**
 *  @brief Return hex value of a give character
 *
 *  @param chr      Character to be converted
 *
 *  @return         The converted character if chr is a valid hex, else 0
 */
int
woal_hexval(char chr)
{
	if (chr >= '0' && chr <= '9')
		return chr - '0';
	if (chr >= 'A' && chr <= 'F')
		return chr - 'A' + 10;
	if (chr >= 'a' && chr <= 'f')
		return chr - 'a' + 10;

	return 0;
}

#ifdef STA_SUPPORT
#endif

/**
 *  @brief This function cancel all works in the queue
 *  and destroy the main workqueue.
 *
 *  @param handle    A pointer to moal_handle
 *
 *  @return        N/A
 */
static void
woal_terminate_workqueue(moal_handle *handle)
{
	ENTER();

	/* Terminate main workqueue */
	if (handle->workqueue) {
		flush_workqueue(handle->workqueue);
		destroy_workqueue(handle->workqueue);
		handle->workqueue = NULL;
	}
	if (handle->rx_workqueue) {
		flush_workqueue(handle->rx_workqueue);
		destroy_workqueue(handle->rx_workqueue);
		handle->rx_workqueue = NULL;
	}
	LEAVE();
}

/********************************************************
		Global Functions
********************************************************/

/**
 *  @brief This function opens the network device
 *
 *  @param dev     A pointer to net_device structure
 *
 *  @return        0 --success, otherwise fail
 */
int
woal_open(struct net_device *dev)
{
	moal_private *priv = (moal_private *)netdev_priv(dev);
	t_u8 carrier_on = MFALSE;

	ENTER();

	if (priv->phandle->surprise_removed == MTRUE) {
		PRINTM(MERROR,
		       "open is not allowed in surprise remove state.\n");
		LEAVE();
		return -EFAULT;
	}
#if defined(SYSKT)
	/* On some systems the device open handler will be called before HW ready.
	   Use the following flag check and wait function to work around the issue. */
	{
		int i = 0;

		while ((priv->phandle->hardware_status != HardwareStatusReady)
		       && (i < MAX_WAIT_DEVICE_READY_COUNT)) {
			i++;
			woal_sched_timeout(100);
		}
		if (i >= MAX_WAIT_DEVICE_READY_COUNT) {
			PRINTM(MFATAL,
			       "HW not ready, wlan_open() return failure\n");
			LEAVE();
			return -EFAULT;
		}
	}
#endif /* USB || SYSKT || SYSKT_MULTI */
	if (!MODULE_GET) {
		LEAVE();
		return -EFAULT;
	}
#ifdef UAP_SUPPORT
	if ((GET_BSS_ROLE(priv) == MLAN_BSS_ROLE_UAP) &&
	    (priv->media_connected))
		carrier_on = MTRUE;
#endif
#ifdef STA_SUPPORT
	if ((GET_BSS_ROLE(priv) == MLAN_BSS_ROLE_STA) &&
	    (priv->media_connected || priv->is_adhoc_link_sensed))
		carrier_on = MTRUE;
#endif
#if defined(WIFI_DIRECT_SUPPORT)
#if defined(STA_CFG80211) && defined(UAP_CFG80211)
#if CFG80211_VERSION_CODE >= WIFI_DIRECT_KERNEL_VERSION
	if (!p2p_enh) {
		if (priv->bss_type == MLAN_BSS_TYPE_WIFIDIRECT &&
		    IS_STA_CFG80211(cfg80211_wext)) {
			priv->phandle->wiphy->interface_modes |=
				MBIT(NL80211_IFTYPE_P2P_GO) |
				MBIT(NL80211_IFTYPE_P2P_CLIENT);
		}
	}
#endif
#endif
#endif
	if (carrier_on == MTRUE) {
		if (!netif_carrier_ok(priv->netdev))
			netif_carrier_on(priv->netdev);
		woal_wake_queue(priv->netdev);
	} else {
		if (netif_carrier_ok(priv->netdev))
			netif_carrier_off(priv->netdev);
	}

	LEAVE();
	return 0;
}

/**
 *  @brief This function closes the network device
 *
 *  @param dev     A pointer to net_device structure
 *
 *  @return        0
 */
int
woal_close(struct net_device *dev)
{
	moal_private *priv = (moal_private *)netdev_priv(dev);

	ENTER();
	woal_flush_tx_stat_queue(priv);

#ifdef STA_SUPPORT
#ifdef STA_CFG80211
	if (IS_STA_CFG80211(cfg80211_wext) &&
	    (priv->bss_type == MLAN_BSS_TYPE_STA))
		woal_clear_conn_params(priv);
	woal_cancel_scan(priv, MOAL_IOCTL_WAIT);

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)
	if (IS_STA_CFG80211(cfg80211_wext) && priv->wdev->current_bss)
		cfg80211_disconnected(priv->netdev, 0, NULL, 0,
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 2, 0)
				      true,
#endif
				      GFP_KERNEL);
#endif

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 2, 0)
	if (IS_STA_CFG80211(cfg80211_wext) && priv->sched_scanning) {
		woal_stop_bg_scan(priv, MOAL_IOCTL_WAIT);
		priv->bg_scan_start = MFALSE;
		priv->bg_scan_reported = MFALSE;
		cfg80211_sched_scan_stopped(priv->wdev->wiphy
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 12, 0)
					    , 0
#endif
			);
		priv->sched_scanning = MFALSE;
	}
#endif
#endif
#endif
	if (!priv->bss_virtual)
		woal_stop_queue(priv->netdev);
#if defined(WIFI_DIRECT_SUPPORT)
#if defined(STA_CFG80211) && defined(UAP_CFG80211)
#if CFG80211_VERSION_CODE >= WIFI_DIRECT_KERNEL_VERSION
	if (!p2p_enh) {
		if (priv->bss_type == MLAN_BSS_TYPE_WIFIDIRECT &&
		    !priv->bss_virtual &&
		    IS_STA_CFG80211(cfg80211_wext) &&
		    IS_UAP_CFG80211(cfg80211_wext)) {
			priv->phandle->wiphy->interface_modes &=
				~(MBIT(NL80211_IFTYPE_P2P_GO) |
				  MBIT(NL80211_IFTYPE_P2P_CLIENT));
		}
	}
#endif
#endif
#endif
	MODULE_PUT;

	LEAVE();
	return 0;
}

/**
 *  @brief This function sets the MAC address to firmware.
 *
 *  @param dev     A pointer to mlan_private structure
 *  @param addr    MAC address to set
 *
 *  @return        0 --success, otherwise fail
 */
int
woal_set_mac_address(struct net_device *dev, void *addr)
{
	int ret = 0;
	moal_private *priv = (moal_private *)netdev_priv(dev);
	struct sockaddr *phw_addr = (struct sockaddr *)addr;
	t_u8 prev_addr[ETH_ALEN];

	ENTER();

	if (priv->phandle->surprise_removed == MTRUE) {
		PRINTM(MERROR,
		       "Set mac address is not allowed in surprise remove state.\n");
		LEAVE();
		return -EFAULT;
	}

	memcpy(prev_addr, priv->current_addr, ETH_ALEN);
	memset(priv->current_addr, 0, ETH_ALEN);
	/* dev->dev_addr is 6 bytes */
	HEXDUMP("dev->dev_addr:", dev->dev_addr, ETH_ALEN);

	HEXDUMP("addr:", (t_u8 *)phw_addr->sa_data, ETH_ALEN);
	memcpy(priv->current_addr, phw_addr->sa_data, ETH_ALEN);
#if defined(WIFI_DIRECT_SUPPORT)
#if defined(STA_CFG80211) && defined(UAP_CFG80211)
#if CFG80211_VERSION_CODE >= WIFI_DIRECT_KERNEL_VERSION
	if (priv->bss_type == MLAN_BSS_TYPE_WIFIDIRECT) {
		priv->current_addr[0] |= 0x02;
		PRINTM(MCMND, "Set WFD device addr: " MACSTR "\n",
		       MAC2STR(priv->current_addr));
	}
#endif
#endif
#endif
	if (MLAN_STATUS_SUCCESS != woal_request_set_mac_address(priv)) {
		PRINTM(MERROR, "Set MAC address failed\n");
		/* For failure restore the MAC address */
		memcpy(priv->current_addr, prev_addr, ETH_ALEN);
		ret = -EFAULT;
		goto done;
	}
	HEXDUMP("priv->MacAddr:", priv->current_addr, ETH_ALEN);
	memcpy(dev->dev_addr, priv->current_addr, ETH_ALEN);
done:
	LEAVE();
	return ret;
}

/**
 *  @brief Check driver status
 *
 *  @param handle   A pointer to moal_handle
 *
 *  @return         MTRUE/MFALSE
 */
t_u8
woal_check_driver_status(moal_handle *handle)
{
	moal_private *priv = NULL;
	struct timeval t;
	int i = 0;
	ENTER();

	priv = woal_get_priv(handle, MLAN_BSS_ROLE_ANY);
	if (!priv || woal_get_debug_info(priv, MOAL_IOCTL_WAIT, &info)) {
		PRINTM(MERROR,
		       "Could not retrieve debug information from MLAN\n");
		LEAVE();
		return MTRUE;
	}
#define MOAL_CMD_TIMEOUT_MAX			9
#define MOAL_CMD_TIMEOUT                20
	do_gettimeofday(&t);
	if (info.pending_cmd &&
	    (t.tv_sec > (info.dnld_cmd_in_secs + MOAL_CMD_TIMEOUT_MAX))) {
		if (t.tv_sec > (info.dnld_cmd_in_secs + MOAL_CMD_TIMEOUT) &&
		    !info.num_cmd_timeout) {
			PRINTM(MERROR, "Ignore invalid time, wait=%d\n",
			       (int)(t.tv_sec - info.dnld_cmd_in_secs));
		} else {
			PRINTM(MERROR, "Timeout cmd id = 0x%x wait=%d\n",
			       info.pending_cmd,
			       (int)(t.tv_sec - info.dnld_cmd_in_secs));
			LEAVE();
			return MTRUE;
		}
	}
	if (info.num_cmd_timeout) {
		PRINTM(MERROR, "num_cmd_timeout = %d\n", info.num_cmd_timeout);
		PRINTM(MERROR, "Timeout cmd id = 0x%x, act = 0x%x\n",
		       info.timeout_cmd_id, info.timeout_cmd_act);
		LEAVE();
		return MTRUE;
	}
	if (info.num_cmd_host_to_card_failure) {
		PRINTM(MERROR, "num_cmd_host_to_card_failure = %d\n",
		       info.num_cmd_host_to_card_failure);
		LEAVE();
		return MTRUE;
	}
	if (info.num_no_cmd_node) {
		PRINTM(MERROR, "num_no_cmd_node = %d\n", info.num_no_cmd_node);
		LEAVE();
		return MTRUE;
	}
	for (i = 0; i < handle->priv_num; i++) {
		priv = handle->priv[i];
		if (priv) {
			if (priv->num_tx_timeout >= NUM_TX_TIMEOUT_THRESHOLD) {
				PRINTM(MERROR, "num_tx_timeout = %d\n",
				       priv->num_tx_timeout);
				LEAVE();
				return MTRUE;
			}
		}
	}
	if (info.pm_wakeup_card_req && info.pm_wakeup_fw_try) {
#define MAX_WAIT_TIME     3
		if (t.tv_sec > (info.pm_wakeup_in_secs + MAX_WAIT_TIME)) {
			PRINTM(MERROR,
			       "wakeup_dev_req=%d wakeup_tries=%d wait=%d\n",
			       info.pm_wakeup_card_req, info.pm_wakeup_fw_try,
			       (int)(t.tv_sec - info.pm_wakeup_in_secs));
			LEAVE();
			return MTRUE;
		}
	}
	LEAVE();
	return MFALSE;
}

/**
 *  @brief Display MLAN debug information
 *
 *  @param priv     A pointer to moal_private
 *
 *  @return         N/A
 */
void
woal_mlan_debug_info(moal_private *priv)
{
	int i;
#ifdef SDIO_MULTI_PORT_TX_AGGR
	int j;
	t_u8 mp_aggr_pkt_limit = SDIO_MP_AGGR_DEF_PKT_LIMIT;
#endif
	char str[512] = { 0 };
	char *s;

	ENTER();

	if (!priv || woal_get_debug_info(priv, MOAL_IOCTL_WAIT, &info)) {
		PRINTM(MERROR,
		       "Could not retrieve debug information from MLAN\n");
		LEAVE();
		return;
	}
	PRINTM(MERROR, "------------mlan_debug_info-------------\n");
	PRINTM(MERROR, "mlan_processing =%d\n", info.mlan_processing);
	PRINTM(MERROR, "main_lock_flag =%d\n", info.main_lock_flag);
	PRINTM(MERROR, "main_process_cnt =%d\n", info.main_process_cnt);
	PRINTM(MERROR, "delay_task_flag =%d\n", info.delay_task_flag);
	PRINTM(MERROR, "mlan_rx_processing =%d\n", info.mlan_rx_processing);
	PRINTM(MERROR, "rx_pkts_queued=%d\n", info.rx_pkts_queued);
	PRINTM(MERROR, "tx_pkts_queued=%d\n", info.tx_pkts_queued);
	PRINTM(MERROR, "num_cmd_timeout = %d\n", info.num_cmd_timeout);
	PRINTM(MERROR, "dbg.num_cmd_timeout = %d\n", info.dbg_num_cmd_timeout);
	PRINTM(MERROR, "Timeout cmd id = 0x%x, act = 0x%x\n",
	       info.timeout_cmd_id, info.timeout_cmd_act);

	PRINTM(MERROR, "last_cmd_index = %d\n", info.last_cmd_index);
	for (s = str, i = 0; i < DBG_CMD_NUM; i++)
		s += sprintf(s, "0x%x ", info.last_cmd_id[i]);
	PRINTM(MERROR, "last_cmd_id = %s\n", str);
	for (s = str, i = 0; i < DBG_CMD_NUM; i++)
		s += sprintf(s, "0x%x ", info.last_cmd_act[i]);
	PRINTM(MERROR, "last_cmd_act = %s\n", str);
	PRINTM(MERROR, "last_cmd_resp_index = %d\n", info.last_cmd_resp_index);
	for (s = str, i = 0; i < DBG_CMD_NUM; i++)
		s += sprintf(s, "0x%x ", info.last_cmd_resp_id[i]);
	PRINTM(MERROR, "last_cmd_resp_id = %s\n", str);
	PRINTM(MERROR, "last_event_index = %d\n", info.last_event_index);
	for (s = str, i = 0; i < DBG_CMD_NUM; i++)
		s += sprintf(s, "0x%x ", info.last_event[i]);
	PRINTM(MERROR, "last_event = %s", str);

	PRINTM(MERROR, "num_data_h2c_failure = %d\n",
	       info.num_tx_host_to_card_failure);
	PRINTM(MERROR, "num_cmd_h2c_failure = %d\n",
	       info.num_cmd_host_to_card_failure);
	PRINTM(MERROR, "num_alloc_buffer_failure = %d\n",
	       info.num_alloc_buffer_failure);
	PRINTM(MERROR, "num_pkt_dropped = %d\n", info.num_pkt_dropped);

	PRINTM(MERROR, "num_data_c2h_failure = %d\n",
	       info.num_rx_card_to_host_failure);
	PRINTM(MERROR, "num_cmdevt_c2h_failure = %d\n",
	       info.num_cmdevt_card_to_host_failure);
	PRINTM(MERROR, "num_int_read_failure = %d\n",
	       info.num_int_read_failure);
	PRINTM(MERROR, "last_int_status = %d\n", info.last_int_status);

	PRINTM(MERROR, "num_event_deauth = %d\n", info.num_event_deauth);
	PRINTM(MERROR, "num_event_disassoc = %d\n", info.num_event_disassoc);
	PRINTM(MERROR, "num_event_link_lost = %d\n", info.num_event_link_lost);
	PRINTM(MERROR, "num_cmd_deauth = %d\n", info.num_cmd_deauth);
	PRINTM(MERROR, "num_cmd_assoc_success = %d\n",
	       info.num_cmd_assoc_success);
	PRINTM(MERROR, "num_cmd_assoc_failure = %d\n",
	       info.num_cmd_assoc_failure);
	PRINTM(MERROR, "cmd_resp_received = %d\n", info.cmd_resp_received);
	PRINTM(MERROR, "event_received = %d\n", info.event_received);

	PRINTM(MERROR, "max_tx_buf_size = %d\n", info.max_tx_buf_size);
	PRINTM(MERROR, "tx_buf_size = %d\n", info.tx_buf_size);
	PRINTM(MERROR, "curr_tx_buf_size = %d\n", info.curr_tx_buf_size);

	PRINTM(MERROR, "data_sent=%d cmd_sent=%d\n", info.data_sent,
	       info.cmd_sent);

	PRINTM(MERROR, "ps_mode=%d ps_state=%d\n", info.ps_mode, info.ps_state);
	PRINTM(MERROR, "wakeup_dev_req=%d wakeup_tries=%d\n",
	       info.pm_wakeup_card_req, info.pm_wakeup_fw_try);
	PRINTM(MERROR, "hs_configured=%d hs_activated=%d\n",
	       info.is_hs_configured, info.hs_activated);
	PRINTM(MERROR, "pps_uapsd_mode=%d sleep_pd=%d\n",
	       info.pps_uapsd_mode, info.sleep_pd);
	PRINTM(MERROR, "tx_lock_flag = %d\n", info.tx_lock_flag);
	PRINTM(MERROR, "port_open = %d\n", info.port_open);
	PRINTM(MERROR, "scan_processing = %d\n", info.scan_processing);

	PRINTM(MERROR, "mp_rd_bitmap=0x%x curr_rd_port=0x%x\n",
	       (unsigned int)info.mp_rd_bitmap, info.curr_rd_port);
	PRINTM(MERROR, "mp_wr_bitmap=0x%x curr_wr_port=0x%x\n",
	       (unsigned int)info.mp_wr_bitmap, info.curr_wr_port);
	PRINTM(MERROR, "mp_invalid_update=%d\n", info.mp_invalid_update);
#ifdef SDIO_MULTI_PORT_TX_AGGR
	PRINTM(MERROR, "last_recv_wr_bitmap=0x%x last_mp_index = %d\n",
	       info.last_recv_wr_bitmap, info.last_mp_index);
	for (i = 0; i < SDIO_MP_DBG_NUM; i++) {
		for (s = str, j = 0; j < mp_aggr_pkt_limit; j++)
			s += sprintf(s, "0x%02x ",
				     info.last_mp_wr_info[i *
							  mp_aggr_pkt_limit +
							  j]);

		PRINTM(MERROR,
		       "mp_wr_bitmap: 0x%x mp_wr_ports=0x%x len=%d curr_wr_port=0x%x\n%s\n",
		       info.last_mp_wr_bitmap[i], info.last_mp_wr_ports[i],
		       info.last_mp_wr_len[i], info.last_curr_wr_port[i], str);
	}
#endif
	for (i = 0; i < info.ralist_num; i++) {
		PRINTM(MERROR,
		       "ralist ra: %02x:%02x:%02x:%02x:%02x:%02x tid=%d pkts=%d pause=%d\n",
		       info.ralist[i].ra[0], info.ralist[i].ra[1],
		       info.ralist[i].ra[2], info.ralist[i].ra[3],
		       info.ralist[i].ra[4], info.ralist[i].ra[5],
		       info.ralist[i].tid, info.ralist[i].total_pkts,
		       info.ralist[i].tx_pause);
	}

	PRINTM(MERROR, "------------mlan_debug_info End-------------\n");
	LEAVE();
}

/**
 *  @brief This function handle the shutdown timeout issue
 *
 *  @param handle   Pointer to structure moal_handle
 *
 *  @return         N/A
 */
void
woal_ioctl_timeout(moal_handle *handle)
{
	moal_private *priv = NULL;

	ENTER();

	PRINTM(MMSG, "woal_ioctl_timout.\n");
	priv = woal_get_priv(handle, MLAN_BSS_ROLE_ANY);
	if (priv) {
		woal_mlan_debug_info(priv);
		woal_moal_debug_info(priv, NULL, MFALSE);
	}
	LEAVE();
	return;
}

/**
 *  @brief This function handles the timeout of packet
 *          transmission
 *
 *  @param dev     A pointer to net_device structure
 *
 *  @return        N/A
 */
void
woal_tx_timeout(struct net_device *dev)
{
	moal_private *priv = (moal_private *)netdev_priv(dev);

	ENTER();

	priv->num_tx_timeout++;
	PRINTM(MERROR, "%lu : %s (bss=%d): Tx timeout (%d)\n",
	       jiffies, dev->name, priv->bss_index, priv->num_tx_timeout);
	woal_set_trans_start(dev);

	if (priv->num_tx_timeout == NUM_TX_TIMEOUT_THRESHOLD) {
		woal_mlan_debug_info(priv);
		woal_moal_debug_info(priv, NULL, MFALSE);
		woal_broadcast_event(priv, CUS_EVT_DRIVER_HANG,
				     strlen(CUS_EVT_DRIVER_HANG));
#if defined(STA_CFG80211) || defined(UAP_CFG80211)
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
		if (IS_STA_OR_UAP_CFG80211(cfg80211_wext))
			woal_cfg80211_vendor_event(priv, event_hang,
						   CUS_EVT_DRIVER_HANG,
						   strlen(CUS_EVT_DRIVER_HANG));
#endif
#endif
		priv->phandle->driver_state = MTRUE;
		woal_process_hang(priv->phandle);
	}

	LEAVE();
}

/**
 *  @brief This function returns the network statistics
 *
 *  @param dev     A pointer to net_device structure
 *
 *  @return        A pointer to net_device_stats structure
 */
struct net_device_stats *
woal_get_stats(struct net_device *dev)
{
	moal_private *priv = (moal_private *)netdev_priv(dev);
	return &priv->stats;
}

#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 29)
/**
 *  @brief This function handles wmm queue select
 *
 *  @param dev     A pointer to net_device structure
 *  @param skb     A pointer to sk_buff structure
 *
 *  @return        tx_queue index (0-3)
 */
u16
woal_select_queue(struct net_device *dev, struct sk_buff *skb
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0)
		  , void *accel_priv
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
		  , select_queue_fallback_t fallback
#endif
#endif
	)
{
	moal_private *priv = (moal_private *)netdev_priv(dev);
	struct ethhdr *eth = NULL;
	t_u8 tid = 0;
	t_u8 index = 0;

	ENTER();

	/*
	 * skb->priority values from 256->263 are magic values to
	 * directly indicate a specific 802.1d priority.  This is used
	 * to allow 802.1d priority to be passed directly in from VLAN
	 * tags, etc.
	 */
	if (IS_SKB_MAGIC_VLAN(skb)) {
		tid = GET_VLAN_PRIO(skb);
	} else {
		eth = (struct ethhdr *)skb->data;
		switch (eth->h_proto) {
		case __constant_htons(ETH_P_IP):
			tid = priv->dscp_map[SKB_TOS(skb) >> DSCP_OFFSET];
			if (tid == 0xFF)
				tid = (IPTOS_PREC(SKB_TOS(skb)) >>
				       IPTOS_OFFSET);
			break;
		case __constant_htons(ETH_P_IPV6):
			tid = SKB_TIDV6(skb);
			break;
		case __constant_htons(ETH_P_ARP):
		default:
			break;
		}
	}

	index = mlan_select_wmm_queue(priv->phandle->pmlan_adapter,
				      priv->bss_index, tid);
	PRINTM(MDATA, "select queue: tid=%d, index=%d\n", tid, index);
	LEAVE();
	return index;
}
#endif

/**
 *  @brief This function flush tx status queue
 *
 *  @param priv      A pointer to moal_private structure
 *
 *  @return          N/A
 */
void
woal_flush_tx_stat_queue(moal_private *priv)
{
	struct tx_status_info *tx_info = NULL, *tmp_node;
	unsigned long flags;
	struct sk_buff *skb = NULL;
	spin_lock_irqsave(&priv->tx_stat_lock, flags);
	list_for_each_entry_safe(tx_info, tmp_node, &priv->tx_stat_queue, link) {
		list_del(&tx_info->link);
		spin_unlock_irqrestore(&priv->tx_stat_lock, flags);
		skb = (struct sk_buff *)tx_info->tx_skb;
		if (tx_info->tx_cookie) {
#if defined(STA_CFG80211) || defined(UAP_CFG80211)
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)
#if CFG80211_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
			cfg80211_mgmt_tx_status(priv->netdev,
						tx_info->tx_cookie, skb->data,
						skb->len, true, GFP_ATOMIC);
#else
			cfg80211_mgmt_tx_status(priv->wdev, tx_info->tx_cookie,
						skb->data, skb->len, true,
						GFP_ATOMIC);
#endif
#endif
#endif
		}
		dev_kfree_skb_any(skb);
		kfree(tx_info);
		spin_lock_irqsave(&priv->tx_stat_lock, flags);
	}
	INIT_LIST_HEAD(&priv->tx_stat_queue);
	spin_unlock_irqrestore(&priv->tx_stat_lock, flags);
}

/**
 *  @brief This function gets tx info from tx_stat_queue
 *
 *  @param priv      	A pointer to moal_private structure
 *  @param tx_seq_num   tx seq number
 *
 *  @return          A pointer to the tcp tx_status_info structure, if found.
 *                   Otherwise, null
 */
struct tx_status_info *
woal_get_tx_info(moal_private *priv, t_u8 tx_seq_num)
{
	struct tx_status_info *tx_info = NULL;
	ENTER();

	list_for_each_entry(tx_info, &priv->tx_stat_queue, link) {
		if (tx_info->tx_seq_num == tx_seq_num) {
			LEAVE();
			return tx_info;
		}
	}
	LEAVE();
	return NULL;
}

/**
 *  @brief This function remove tx info from queue
 *
 *  @param priv      		A pointer to moal_private structure
 *  @param tx_seq_num           tx seq number
 *
 *  @return	         N/A
 */
void
woal_remove_tx_info(moal_private *priv, t_u8 tx_seq_num)
{
	struct tx_status_info *tx_info, *tmp = NULL;
	unsigned long flags;
	ENTER();

	spin_lock_irqsave(&priv->tx_stat_lock, flags);
	list_for_each_entry_safe(tx_info, tmp, &priv->tx_stat_queue, link) {
		if (tx_info->tx_seq_num == tx_seq_num) {
			list_del(&tx_info->link);
			dev_kfree_skb_any((struct sk_buff *)tx_info->tx_skb);
			kfree(tx_info);
			break;
		}
	}
	spin_unlock_irqrestore(&priv->tx_stat_lock, flags);

	LEAVE();
}

/**
 *  @brief This function flush tcp session queue
 *
 *  @param priv      A pointer to moal_private structure
 *
 *  @return          N/A
 */
void
woal_flush_tdls_list(moal_private *priv)
{
	struct tdls_peer *peer = NULL, *tmp_node;
	unsigned long flags;
	spin_lock_irqsave(&priv->tdls_lock, flags);
	list_for_each_entry_safe(peer, tmp_node, &priv->tdls_list, link) {
		list_del(&peer->link);
		kfree(peer);
	}
	INIT_LIST_HEAD(&priv->tdls_list);
	spin_unlock_irqrestore(&priv->tdls_lock, flags);
	priv->tdls_check_tx = MFALSE;
}

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
/**
 *  @brief  check the tx packet for tdls auto set up
 *
 *  @param priv      A pointer to moal_private structure
 *  @param skb       A pointer to skb buffer.
 *
 *  @return          N/A
 */
void
woal_tdls_check_tx(moal_private *priv, struct sk_buff *skb)
{
	struct tdls_peer *peer = NULL;
	unsigned long flags;
	t_u8 ra[MLAN_MAC_ADDR_LENGTH];
	ENTER();
	memcpy(ra, skb->data, MLAN_MAC_ADDR_LENGTH);
	spin_lock_irqsave(&priv->tdls_lock, flags);
	list_for_each_entry(peer, &priv->tdls_list, link) {
		if (!memcmp(peer->peer_addr, ra, ETH_ALEN)) {
			if (peer->rssi &&
			    (peer->rssi <= TDLS_RSSI_HIGH_THRESHOLD)) {
				if ((peer->link_status == TDLS_NOT_SETUP) &&
				    (peer->num_failure <
				     TDLS_MAX_FAILURE_COUNT)) {
					peer->link_status =
						TDLS_SETUP_INPROGRESS;
					PRINTM(MMSG,
					       "Wlan: Set up TDLS link,peer="
					       MACSTR " rssi=%d\n",
					       MAC2STR(peer->peer_addr),
					       -peer->rssi);
					cfg80211_tdls_oper_request(priv->netdev,
								   peer->
								   peer_addr,
								   NL80211_TDLS_SETUP,
								   0,
								   GFP_ATOMIC);
					priv->tdls_check_tx = MFALSE;
				}

			}
			break;
		}
	}
	spin_unlock_irqrestore(&priv->tdls_lock, flags);
	LEAVE();
}
#endif

/**
 *  @brief This function flush tcp session queue
 *
 *  @param priv      A pointer to moal_private structure
 *
 *  @return          N/A
 */
void
woal_flush_tcp_sess_queue(moal_private *priv)
{
	struct tcp_sess *tcp_sess = NULL, *tmp_node;
	unsigned long flags;
	struct sk_buff *skb;
	spin_lock_irqsave(&priv->tcp_sess_lock, flags);
	list_for_each_entry_safe(tcp_sess, tmp_node, &priv->tcp_sess_queue,
				 link) {
		list_del(&tcp_sess->link);
		if (tcp_sess->is_timer_set)
			woal_cancel_timer(&tcp_sess->ack_timer);
		skb = (struct sk_buff *)tcp_sess->ack_skb;
		if (skb)
			dev_kfree_skb_any(skb);
		kfree(tcp_sess);
	}
	INIT_LIST_HEAD(&priv->tcp_sess_queue);
	priv->tcp_ack_drop_cnt = 0;
	priv->tcp_ack_cnt = 0;
	spin_unlock_irqrestore(&priv->tcp_sess_lock, flags);
}

/**
 *  @brief This function gets tcp session from the tcp session queue
 *
 *  @param priv      A pointer to moal_private structure
 *  @param src_ip    IP address of the device
 *  @param src_port  TCP port of the device
 *  @param dst_ip    IP address of the client
 *  @param dst_port  TCP port of the client
 *
 *  @return          A pointer to the tcp session data structure, if found.
 *                   Otherwise, null
 */
static inline struct tcp_sess *
woal_get_tcp_sess(moal_private *priv,
		  t_u32 src_ip, t_u16 src_port, t_u32 dst_ip, t_u16 dst_port)
{
	struct tcp_sess *tcp_sess = NULL;
	ENTER();

	list_for_each_entry(tcp_sess, &priv->tcp_sess_queue, link) {
		if ((tcp_sess->src_ip_addr == src_ip) &&
		    (tcp_sess->src_tcp_port == src_port) &&
		    (tcp_sess->dst_ip_addr == dst_ip) &&
		    (tcp_sess->dst_tcp_port == dst_port)) {
			LEAVE();
			return tcp_sess;
		}
	}
	LEAVE();
	return NULL;
}

/**
 *  @brief This function send the holding tcp ack packet
 *  re-assoc thread.
 *
 *  @param context  A pointer to context
 *  @return         N/A
 */
void
woal_tcp_ack_timer_func(void *context)
{
	struct tcp_sess *tcp_session = (struct tcp_sess *)context;
	moal_private *priv = (moal_private *)tcp_session->priv;
	unsigned long flags;
	mlan_buffer *pmbuf;
	struct sk_buff *skb;
	mlan_status status;
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 29)
	t_u32 index = 0;
#endif
	ENTER();
	spin_lock_irqsave(&priv->tcp_sess_lock, flags);
	tcp_session->is_timer_set = MFALSE;
	skb = (struct sk_buff *)tcp_session->ack_skb;
	pmbuf = (mlan_buffer *)tcp_session->pmbuf;
	tcp_session->ack_skb = NULL;
	tcp_session->pmbuf = NULL;
	spin_unlock_irqrestore(&priv->tcp_sess_lock, flags);
	if (skb && pmbuf) {
		status = mlan_send_packet(priv->phandle->pmlan_adapter, pmbuf);
		switch (status) {
		case MLAN_STATUS_PENDING:
			atomic_inc(&priv->phandle->tx_pending);
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 29)
			index = skb_get_queue_mapping(skb);
			atomic_inc(&priv->wmm_tx_pending[index]);
			if (atomic_read(&priv->wmm_tx_pending[index]) >=
			    MAX_TX_PENDING) {
				struct netdev_queue *txq =
					netdev_get_tx_queue(priv->netdev,
							    index);
				netif_tx_stop_queue(txq);
				PRINTM(MINFO, "Stop Kernel Queue : %d\n",
				       index);
			}
#else
			if (atomic_read(&priv->phandle->tx_pending) >=
			    MAX_TX_PENDING)
				woal_stop_queue(priv->netdev);
#endif /*#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,29) */
			queue_work(priv->phandle->workqueue,
				   &priv->phandle->main_work);
			break;
		case MLAN_STATUS_SUCCESS:
			priv->stats.tx_packets++;
			priv->stats.tx_bytes += skb->len;
			dev_kfree_skb_any(skb);
			break;
		case MLAN_STATUS_FAILURE:
		default:
			priv->stats.tx_dropped++;
			dev_kfree_skb_any(skb);
			break;
		}
	}
	LEAVE();
	return;
}

/**
 *  @brief This function send the tcp ack
 *
 *
 *  @param priv         A pointer to moal_private structure
 *  @param tcp_session  A pointer to tcp_session
 *  @return         N/A
 */
void
woal_send_tcp_ack(moal_private *priv, struct tcp_sess *tcp_session)
{
	mlan_status status;
	struct sk_buff *skb = (struct sk_buff *)tcp_session->ack_skb;
	mlan_buffer *pmbuf = (mlan_buffer *)tcp_session->pmbuf;
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 29)
	t_u32 index = 0;
#endif
	ENTER();
	if (tcp_session->is_timer_set) {
		woal_cancel_timer(&tcp_session->ack_timer);
		tcp_session->is_timer_set = MFALSE;
	}
	tcp_session->ack_skb = NULL;
	tcp_session->pmbuf = NULL;
	status = mlan_send_packet(priv->phandle->pmlan_adapter, pmbuf);
	switch (status) {
	case MLAN_STATUS_PENDING:
		atomic_inc(&priv->phandle->tx_pending);
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 29)
		index = skb_get_queue_mapping(skb);
		atomic_inc(&priv->wmm_tx_pending[index]);
		if (atomic_read(&priv->wmm_tx_pending[index]) >= MAX_TX_PENDING) {
			struct netdev_queue *txq =
				netdev_get_tx_queue(priv->netdev, index);
			netif_tx_stop_queue(txq);
			PRINTM(MINFO, "Stop Kernel Queue : %d\n", index);
		}
#else
		if (atomic_read(&priv->phandle->tx_pending) >= MAX_TX_PENDING)
			woal_stop_queue(priv->netdev);
#endif /*#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,29) */
		queue_work(priv->phandle->workqueue, &priv->phandle->main_work);
		break;
	case MLAN_STATUS_SUCCESS:
		priv->stats.tx_packets++;
		priv->stats.tx_bytes += skb->len;
		dev_kfree_skb_any(skb);
		break;
	case MLAN_STATUS_FAILURE:
	default:
		priv->stats.tx_dropped++;
		dev_kfree_skb_any(skb);
		break;
	}
	LEAVE();
}

/**
 *  @brief This function get the tcp ack session node
 *
 *  @param priv      A pointer to moal_private structure
 *  @param pmbuf     A pointer to mlan_buffer associated with a skb
 *
 *  @return          1, if it's dropped; 0, if not dropped
 */
int
woal_process_tcp_ack(moal_private *priv, mlan_buffer *pmbuf)
{
	int ret = 0;
	unsigned long flags;
	struct tcp_sess *tcp_session;
	struct ethhdr *ethh = NULL;
	struct iphdr *iph = NULL;
	struct tcphdr *tcph = NULL;
	t_u32 ack_seq;
	struct sk_buff *skb;

	ENTER();

	/** check the tcp packet */
	ethh = (struct ethhdr *)(pmbuf->pbuf + pmbuf->data_offset);
	if (ntohs(ethh->h_proto) != ETH_P_IP) {
		LEAVE();
		return 0;
	}
	iph = (struct iphdr *)((t_u8 *)ethh + sizeof(struct ethhdr));
	if (iph->protocol != IPPROTO_TCP) {
		LEAVE();
		return 0;
	}
	tcph = (struct tcphdr *)((t_u8 *)iph + iph->ihl * 4);

	if (*((t_u8 *)tcph + 13) == 0x10) {
		/* Only replace ACK */
		if (ntohs(iph->tot_len) > (iph->ihl + tcph->doff) * 4) {
			/* Don't drop ACK with payload */
			/* TODO: should we delete previous TCP session */
			LEAVE();
			return ret;
		}
		priv->tcp_ack_cnt++;
		spin_lock_irqsave(&priv->tcp_sess_lock, flags);
		tcp_session = woal_get_tcp_sess(priv, iph->saddr,
						tcph->source, iph->daddr,
						tcph->dest);
		if (!tcp_session) {
			tcp_session =
				kmalloc(sizeof(struct tcp_sess), GFP_ATOMIC);
			if (!tcp_session) {
				PRINTM(MERROR, "Fail to allocate tcp_sess.\n");
				spin_unlock_irqrestore(&priv->tcp_sess_lock,
						       flags);
				goto done;
			}
			tcp_session->ack_skb = pmbuf->pdesc;
			tcp_session->pmbuf = pmbuf;
			pmbuf->flags |= MLAN_BUF_FLAG_TCP_ACK;
			tcp_session->src_ip_addr = iph->saddr;
			tcp_session->dst_ip_addr = iph->daddr;
			tcp_session->src_tcp_port = tcph->source;
			tcp_session->dst_tcp_port = tcph->dest;
			tcp_session->ack_seq = ntohl(tcph->ack_seq);
			tcp_session->priv = (void *)priv;
			skb = (struct sk_buff *)pmbuf->pdesc;
			skb->cb[0] = 0;
			/* Initialize the timer for tcp ack */
			woal_initialize_timer(&tcp_session->ack_timer,
					      woal_tcp_ack_timer_func,
					      tcp_session);
			tcp_session->is_timer_set = MTRUE;
			woal_mod_timer(&tcp_session->ack_timer, MOAL_TIMER_1MS);
			list_add_tail(&tcp_session->link,
				      &priv->tcp_sess_queue);
			spin_unlock_irqrestore(&priv->tcp_sess_lock, flags);
			ret = HOLD_TCP_ACK;
			LEAVE();
			return ret;
		} else if (!tcp_session->ack_skb) {
			tcp_session->ack_skb = pmbuf->pdesc;
			tcp_session->pmbuf = pmbuf;
			pmbuf->flags |= MLAN_BUF_FLAG_TCP_ACK;
			tcp_session->ack_seq = ntohl(tcph->ack_seq);
			tcp_session->priv = (void *)priv;
			skb = (struct sk_buff *)pmbuf->pdesc;
			skb->cb[0] = 0;
			tcp_session->is_timer_set = MTRUE;
			woal_mod_timer(&tcp_session->ack_timer, MOAL_TIMER_1MS);
			spin_unlock_irqrestore(&priv->tcp_sess_lock, flags);
			ret = HOLD_TCP_ACK;
			LEAVE();
			return ret;
		}
		ack_seq = ntohl(tcph->ack_seq);
		skb = (struct sk_buff *)tcp_session->ack_skb;
		if (likely(ack_seq > tcp_session->ack_seq) &&
		    (skb->len == pmbuf->data_len)) {
			memcpy(skb->data, pmbuf->pbuf + pmbuf->data_offset,
			       pmbuf->data_len);
			tcp_session->ack_seq = ack_seq;
			ret = DROP_TCP_ACK;
			skb->cb[0]++;
//We will drop 90% tcp ack
#define TCP_ACK_MAX_HOLD    9
			if (skb->cb[0] >= TCP_ACK_MAX_HOLD)
				woal_send_tcp_ack(priv, tcp_session);
			spin_unlock_irqrestore(&priv->tcp_sess_lock, flags);
			skb = (struct sk_buff *)pmbuf->pdesc;
			dev_kfree_skb_any(skb);
			priv->tcp_ack_drop_cnt++;
		} else {
			pmbuf->flags |= MLAN_BUF_FLAG_TCP_ACK;
			spin_unlock_irqrestore(&priv->tcp_sess_lock, flags);
			LEAVE();
			return ret;
		}
	}
done:
	LEAVE();
	return ret;
}

/**
 *  @brief This function handles packet transmission
 *
 *  @param skb     A pointer to sk_buff structure
 *  @param dev     A pointer to net_device structure
 *
 *  @return        0 --success
 */
int
woal_hard_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	moal_private *priv = (moal_private *)netdev_priv(dev);
	mlan_buffer *pmbuf = NULL;
	mlan_status status;
	struct sk_buff *new_skb = NULL;
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 29)
	t_u32 index = 0;
#endif
	int ret = 0;
	ENTER();
	PRINTM(MDATA, "%lu : %s (bss=%d): Data <= kernel\n",
	       jiffies, dev->name, priv->bss_index);

	if (priv->phandle->surprise_removed == MTRUE) {
		dev_kfree_skb_any(skb);
		priv->stats.tx_dropped++;
		goto done;
	}
	priv->num_tx_timeout = 0;
	if (!skb->len || (skb->len > ETH_FRAME_LEN)) {
		PRINTM(MERROR, "Tx Error: Bad skb length %d : %d\n",
		       skb->len, ETH_FRAME_LEN);
		dev_kfree_skb_any(skb);
		priv->stats.tx_dropped++;
		goto done;
	}
	if (skb_headroom(skb) < (MLAN_MIN_DATA_HEADER_LEN +
				 sizeof(mlan_buffer) +
				 priv->extra_tx_head_len)) {
		PRINTM(MWARN, "Tx: Insufficient skb headroom %d\n",
		       skb_headroom(skb));
		/* Insufficient skb headroom - allocate a new skb */
		new_skb = skb_realloc_headroom(skb, MLAN_MIN_DATA_HEADER_LEN +
					       sizeof(mlan_buffer) +
					       priv->extra_tx_head_len);
		if (unlikely(!new_skb)) {
			PRINTM(MERROR, "Tx: Cannot allocate skb\n");
			dev_kfree_skb_any(skb);
			priv->stats.tx_dropped++;
			goto done;
		}
		if (new_skb != skb)
			dev_kfree_skb_any(skb);
		skb = new_skb;
		PRINTM(MINFO, "new skb headroom %d\n", skb_headroom(skb));
	}
	pmbuf = (mlan_buffer *)skb->head;
	memset((t_u8 *)pmbuf, 0, sizeof(mlan_buffer));
	pmbuf->bss_index = priv->bss_index;
	woal_fill_mlan_buffer(priv, pmbuf, skb);
	if (priv->enable_tcp_ack_enh == MTRUE) {
		ret = woal_process_tcp_ack(priv, pmbuf);
		if (ret)
			goto done;
	}
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
	if (priv->enable_auto_tdls && priv->tdls_check_tx)
		woal_tdls_check_tx(priv, skb);
#endif

	status = mlan_send_packet(priv->phandle->pmlan_adapter, pmbuf);
	switch (status) {
	case MLAN_STATUS_PENDING:
		atomic_inc(&priv->phandle->tx_pending);
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 29)
		index = skb_get_queue_mapping(skb);
		atomic_inc(&priv->wmm_tx_pending[index]);
		if (atomic_read(&priv->wmm_tx_pending[index]) >= MAX_TX_PENDING) {
			struct netdev_queue *txq =
				netdev_get_tx_queue(priv->netdev, index);
			netif_tx_stop_queue(txq);
			PRINTM(MINFO, "Stop Kernel Queue : %d\n", index);
		}
#else
		if (atomic_read(&priv->phandle->tx_pending) >= MAX_TX_PENDING)
			woal_stop_queue(priv->netdev);
#endif /*#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,29) */
		if (!mlan_is_main_process_running(priv->phandle->pmlan_adapter))
			 queue_work(priv->phandle->workqueue,
				    &priv->phandle->main_work);
		break;
	case MLAN_STATUS_SUCCESS:
		priv->stats.tx_packets++;
		priv->stats.tx_bytes += skb->len;
		dev_kfree_skb_any(skb);
		break;
	case MLAN_STATUS_FAILURE:
	default:
		priv->stats.tx_dropped++;
		dev_kfree_skb_any(skb);
		break;
	}
done:
	LEAVE();
	return 0;
}

/**
 *  @brief Convert ascii string to Hex integer
 *
 *  @param d        A pointer to integer buf
 *  @param s        A pointer to ascii string
 *  @param dlen     The byte number of ascii string in hex
 *
 *  @return         Number of integer
 */
int
woal_ascii2hex(t_u8 *d, char *s, t_u32 dlen)
{
	unsigned int i;
	t_u8 n;

	ENTER();

	memset(d, 0x00, dlen);

	for (i = 0; i < dlen * 2; i++) {
		if ((s[i] >= 48) && (s[i] <= 57))
			n = s[i] - 48;
		else if ((s[i] >= 65) && (s[i] <= 70))
			n = s[i] - 55;
		else if ((s[i] >= 97) && (s[i] <= 102))
			n = s[i] - 87;
		else
			break;
		if (!(i % 2))
			n = n * 16;
		d[i / 2] += n;
	}

	LEAVE();
	return i;
}

/**
 *  @brief Return integer value of a given ascii string
 *
 *  @param data    Converted data to be returned
 *  @param a       String to be converted
 *
 *  @return        MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
woal_atoi(int *data, char *a)
{
	int i, val = 0, len;
	int mul = 1;

	ENTER();

	len = strlen(a);
	if (len > 2) {
		if (!strncmp(a, "0x", 2)) {
			a = a + 2;
			len -= 2;
			*data = woal_atox(a);
			LEAVE();
			return MLAN_STATUS_SUCCESS;
		}
	}
	for (i = 0; i < len; i++) {
		if (isdigit(a[i])) {
			val = val * 10 + (a[i] - '0');
		} else {
			if ((i == 0) && (a[i] == '-')) {
				mul = -1;
			} else {
				PRINTM(MERROR, "Invalid char %c in string %s\n",
				       a[i], a);
				LEAVE();
				return MLAN_STATUS_FAILURE;
			}
		}
	}
	*data = (mul * val);

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief Return hex value of a given ascii string
 *
 *  @param a        String to be converted to ascii
 *
 *  @return         The converted character if a is a valid hex, else 0
 */
int
woal_atox(char *a)
{
	int i = 0;

	ENTER();

	while (isxdigit(*a))
		i = i * 16 + woal_hexval(*a++);

	LEAVE();
	return i;
}

/**
 *  @brief Extension of strsep lib command. This function will also take care
 *      escape character
 *
 *  @param s         A pointer to array of chars to process
 *  @param delim     The delimiter character to end the string
 *  @param esc       The escape character to ignore for delimiter
 *
 *  @return          Pointer to the separated string if delim found, else NULL
 */
char *
woal_strsep(char **s, char delim, char esc)
{
	char *se = *s, *sb;

	ENTER();

	if (!(*s) || (*se == '\0')) {
		LEAVE();
		return NULL;
	}

	for (sb = *s; *sb != '\0'; ++sb) {
		if (*sb == esc && *(sb + 1) == esc) {
			/*
			 * We get a esc + esc seq then keep the one esc
			 * and chop off the other esc character
			 */
			memmove(sb, sb + 1, strlen(sb));
			continue;
		}
		if (*sb == esc && *(sb + 1) == delim) {
			/*
			 * We get a delim + esc seq then keep the delim
			 * and chop off the esc character
			 */
			memmove(sb, sb + 1, strlen(sb));
			continue;
		}
		if (*sb == delim)
			break;
	}

	if (*sb == '\0')
		sb = NULL;
	else
		*sb++ = '\0';

	*s = sb;

	LEAVE();
	return se;
}

/**
 *  @brief Convert mac address from string to t_u8 buffer.
 *
 *  @param mac_addr The buffer to store the mac address in.
 *  @param buf      The source of mac address which is a string.
 *
 *  @return         N/A
 */
void
woal_mac2u8(t_u8 *mac_addr, char *buf)
{
	char *begin, *end, *mac_buff;
	int i;

	ENTER();

	if (!buf) {
		LEAVE();
		return;
	}

	mac_buff = kzalloc(strlen(buf) + 1, GFP_KERNEL);
	if (!mac_buff) {
		LEAVE();
		return;
	}
	memcpy(mac_buff, buf, strlen(buf));

	begin = mac_buff;
	for (i = 0; i < ETH_ALEN; ++i) {
		end = woal_strsep(&begin, ':', '/');
		if (end)
			mac_addr[i] = woal_atox(end);
	}

	kfree(mac_buff);
	LEAVE();
}

#ifdef STA_SUPPORT
/**
 *  @brief This function sets multicast addresses to firmware
 *
 *  @param dev     A pointer to net_device structure
 *
 *  @return        N/A
 */
void
woal_set_multicast_list(struct net_device *dev)
{
	moal_private *priv = (moal_private *)netdev_priv(dev);
	ENTER();
	woal_request_set_multicast_list(priv, dev);
	LEAVE();
}
#endif

/**
 *  @brief This function initializes the private structure
 *          and set default value to the member of moal_private.
 *
 *  @param priv             A pointer to moal_private structure
 *  @param wait_option      Wait option
 *
 *  @return                 N/A
 */
void
woal_init_priv(moal_private *priv, t_u8 wait_option)
{
	ENTER();
#ifdef STA_SUPPORT
	if (GET_BSS_ROLE(priv) == MLAN_BSS_ROLE_STA) {
		priv->current_key_index = 0;
		priv->rate_index = AUTO_RATE;
		priv->is_adhoc_link_sensed = MFALSE;
		priv->scan_type = MLAN_SCAN_TYPE_ACTIVE;
		priv->bg_scan_start = MFALSE;
		priv->bg_scan_reported = MFALSE;
		memset(&priv->nick_name, 0, sizeof(priv->nick_name));
		priv->num_tx_timeout = 0;
		priv->rx_filter = 0;

#ifdef REASSOCIATION
		priv->reassoc_on = MFALSE;
		priv->set_asynced_essid_flag = MFALSE;
#endif
#ifdef STA_CFG80211
		memset(&priv->sme_current, 0,
		       sizeof(struct cfg80211_connect_params));
#endif
	}
#endif /* STA_SUPPORT */
#ifdef UAP_SUPPORT
	if (GET_BSS_ROLE(priv) == MLAN_BSS_ROLE_UAP) {
		priv->bss_started = MFALSE;
#ifdef UAP_CFG80211
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
		memset(&priv->chan, 0, sizeof(struct cfg80211_chan_def));
		memset(&priv->csa_chan, 0, sizeof(struct cfg80211_chan_def));
		priv->uap_tx_blocked = MFALSE;
		memset(&priv->beacon_after, 0,
		       sizeof(struct cfg80211_beacon_data));
#endif
#endif
	}
#endif

	memset(&priv->tx_protocols, 0, sizeof(dot11_protocol));
	memset(&priv->rx_protocols, 0, sizeof(dot11_protocol));
	priv->media_connected = MFALSE;

	memset(priv->dscp_map, 0xFF, sizeof(priv->dscp_map));

#if defined(STA_CFG80211) || defined(UAP_CFG80211)
	priv->probereq_index = MLAN_CUSTOM_IE_AUTO_IDX_MASK;
	priv->beacon_index = MLAN_CUSTOM_IE_AUTO_IDX_MASK;
	priv->proberesp_index = MLAN_CUSTOM_IE_AUTO_IDX_MASK;
	priv->assocresp_index = MLAN_CUSTOM_IE_AUTO_IDX_MASK;
	priv->beacon_wps_index = MLAN_CUSTOM_IE_AUTO_IDX_MASK;
	priv->proberesp_p2p_index = MLAN_CUSTOM_IE_AUTO_IDX_MASK;
	priv->assocresp_qos_map_index = MLAN_CUSTOM_IE_AUTO_IDX_MASK;
	priv->proberesp_vendor_index = MLAN_CUSTOM_IE_AUTO_IDX_MASK;
#endif

#ifdef STA_SUPPORT
	priv->pmk_saved = MFALSE;
	memset(&priv->pmk, 0, sizeof(mlan_pmk_t));
#endif

	priv->enable_tcp_ack_enh = MTRUE;

	priv->enable_auto_tdls = MFALSE;
	priv->tdls_check_tx = MFALSE;

	priv->gtk_data_ready = MFALSE;
	memset(&priv->gtk_rekey_data, 0, sizeof(mlan_ds_misc_gtk_rekey_data));

	woal_request_get_fw_info(priv, wait_option, NULL);

#if defined(WIFI_DIRECT_SUPPORT)
#if defined(STA_CFG80211) && defined(UAP_CFG80211)
#if CFG80211_VERSION_CODE >= WIFI_DIRECT_KERNEL_VERSION
#ifdef MFG_CMD_SUPPORT
	if (mfg_mode != MLAN_INIT_PARA_ENABLED)
#endif
		if (priv->bss_type == MLAN_BSS_TYPE_WIFIDIRECT) {
			if (priv->bss_virtual) {
				if (priv->pa_netdev) {
					memcpy(priv->current_addr,
					       priv->pa_netdev->dev_addr,
					       ETH_ALEN);
					priv->current_addr[4] ^= 0x80;
					woal_request_set_mac_address(priv);
					memcpy(priv->netdev->dev_addr,
					       priv->current_addr, ETH_ALEN);
					PRINTM(MCMND,
					       "Set WFD interface addr: " MACSTR
					       "\n",
					       MAC2STR(priv->current_addr));
				}
			} else {
				priv->current_addr[0] |= 0x02;
				woal_request_set_mac_address(priv);
				memcpy(priv->netdev->dev_addr,
				       priv->current_addr, ETH_ALEN);
				PRINTM(MCMND,
				       "Set WFD device addr: " MACSTR "\n",
				       MAC2STR(priv->current_addr));
			}
		}
#endif
#endif
#endif
#ifdef UAP_SUPPORT
#if defined(DFS_TESTING_SUPPORT)
	priv->user_cac_period_msec = 0;
#endif
#endif
	LEAVE();
}

/**
 *  @brief Reset all interfaces if all_intf flag is TRUE,
 *          otherwise specified interface only
 *
 *  @param priv          A pointer to moal_private structure
 *  @param wait_option   Wait option
 *  @param all_intf      TRUE  : all interfaces
 *                       FALSE : current interface only
 *
 *  @return             MLAN_STATUS_SUCCESS --success, otherwise fail
 */
int
woal_reset_intf(moal_private *priv, t_u8 wait_option, int all_intf)
{
	int ret = MLAN_STATUS_SUCCESS;
	int intf_num;
	moal_handle *handle = NULL;
	mlan_bss_info bss_info;

	ENTER();

	if (!priv) {
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}
	handle = priv->phandle;

	/* Stop queue and detach device */
	if (!all_intf) {
		woal_stop_queue(priv->netdev);
		netif_device_detach(priv->netdev);
	} else {
		for (intf_num = 0; intf_num < handle->priv_num; intf_num++) {
			woal_stop_queue(handle->priv[intf_num]->netdev);
			netif_device_detach(handle->priv[intf_num]->netdev);
		}
	}

	/* Get BSS info */
	memset(&bss_info, 0, sizeof(bss_info));
	woal_get_bss_info(priv, wait_option, &bss_info);

#ifdef STA_SUPPORT
	woal_cancel_scan(priv, wait_option);
#endif

	/* Cancel host sleep */
	if (bss_info.is_hs_configured) {
		if (MLAN_STATUS_SUCCESS != woal_cancel_hs(priv, wait_option)) {
			ret = -EFAULT;
			goto done;
		}
	}

	/* Disconnect from network */
	if (!all_intf) {
		/* Disconnect specified interface only */
		if ((priv->media_connected == MTRUE)
#ifdef UAP_SUPPORT
		    || (GET_BSS_ROLE(priv) == MLAN_BSS_ROLE_UAP)
#endif
			) {
			woal_disconnect(priv, wait_option, NULL,
					DEF_DEAUTH_REASON_CODE);
			priv->media_connected = MFALSE;
		}
	} else {
		/* Disconnect all interfaces */
		for (intf_num = 0; intf_num < handle->priv_num; intf_num++) {
			if (handle->priv[intf_num]->media_connected == MTRUE
#ifdef UAP_SUPPORT
			    || (GET_BSS_ROLE(handle->priv[intf_num]) ==
				MLAN_BSS_ROLE_UAP)
#endif
				) {
				woal_disconnect(handle->priv[intf_num],
						wait_option, NULL,
						DEF_DEAUTH_REASON_CODE);
				handle->priv[intf_num]->media_connected =
					MFALSE;
			}
		}
	}

#ifdef REASSOCIATION
	/* Reset the reassoc timer and status */
	if (!all_intf) {
		handle->reassoc_on &= ~MBIT(priv->bss_index);
		priv->reassoc_on = MFALSE;
		priv->set_asynced_essid_flag = MFALSE;
	} else {
		handle->reassoc_on = 0;
		for (intf_num = 0; intf_num < handle->priv_num; intf_num++) {
			handle->priv[intf_num]->reassoc_on = MFALSE;
			handle->priv[intf_num]->set_asynced_essid_flag = MFALSE;
		}
	}
	if (!handle->reassoc_on && handle->is_reassoc_timer_set) {
		woal_cancel_timer(&handle->reassoc_timer);
		handle->is_reassoc_timer_set = MFALSE;
	}
#endif /* REASSOCIATION */

#if defined(WIFI_DIRECT_SUPPORT)
#if defined(STA_CFG80211) && defined(UAP_CFG80211)
	if (handle->is_go_timer_set) {
		woal_cancel_timer(&handle->go_timer);
		handle->is_go_timer_set = MFALSE;
	}
#endif
#endif

#if defined(STA_CFG80211) || defined(UAP_CFG80211)
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(2, 6, 39)
	if (handle->is_remain_timer_set) {
		woal_cancel_timer(&handle->remain_timer);
		woal_remain_timer_func(handle);
	}
#endif
#endif

done:
	LEAVE();
	return ret;
}

/**
 *  @brief This function return the point to structure moal_private
 *
 *  @param handle       Pointer to structure moal_handle
 *  @param bss_index    BSS index number
 *
 *  @return             moal_private pointer or NULL
 */
moal_private *
woal_bss_index_to_priv(moal_handle *handle, t_u8 bss_index)
{
	int i;

	ENTER();
	if (!handle) {
		LEAVE();
		return NULL;
	}
	for (i = 0; i < MLAN_MAX_BSS_NUM; i++) {
		if (handle->priv[i] &&
		    (handle->priv[i]->bss_index == bss_index)) {
			LEAVE();
			return handle->priv[i];
		}
	}

	LEAVE();
	return NULL;
}

/**
 *  @brief This function alloc mlan_buffer.
 *  @param handle  A pointer to moal_handle structure
 *  @param size	   buffer size to allocate
 *
 *  @return        mlan_buffer pointer or NULL
 */
pmlan_buffer
woal_alloc_mlan_buffer(moal_handle *handle, int size)
{
	mlan_buffer *pmbuf = NULL;
	struct sk_buff *skb;
	gfp_t flag;

	ENTER();

	flag = (in_atomic() || irqs_disabled())? GFP_ATOMIC : GFP_KERNEL;
	if (size <= 0) {
		PRINTM(MERROR, "Buffer size must be positive\n");
		LEAVE();
		return NULL;
	}

	skb = __dev_alloc_skb(size + sizeof(mlan_buffer), flag);
	if (!skb) {
		PRINTM(MERROR, "%s: No free skb\n", __func__);
		LEAVE();
		return NULL;
	}
	skb_reserve(skb, sizeof(mlan_buffer));
	pmbuf = (mlan_buffer *)skb->head;
	memset((u8 *)pmbuf, 0, sizeof(mlan_buffer));
	pmbuf->pdesc = (t_void *)skb;
	pmbuf->pbuf = (t_u8 *)skb->data;
	atomic_inc(&handle->mbufalloc_count);
	LEAVE();
	return pmbuf;
}

/**
 *  @brief This function alloc mlan_ioctl_req.
 *
 *  @param size	   buffer size to allocate
 *
 *  @return        mlan_ioctl_req pointer or NULL
 */
pmlan_ioctl_req
woal_alloc_mlan_ioctl_req(int size)
{
	mlan_ioctl_req *req = NULL;
	gfp_t flag;

	ENTER();

	flag = (in_atomic() || irqs_disabled())? GFP_ATOMIC : GFP_KERNEL;
	req = kzalloc((sizeof(mlan_ioctl_req) + size + sizeof(int) +
		       sizeof(wait_queue)), flag);
	if (!req) {
		PRINTM(MERROR, "%s: Fail to alloc ioctl buffer\n", __func__);
		LEAVE();
		return NULL;
	}
	req->pbuf = (t_u8 *)req + sizeof(mlan_ioctl_req) + sizeof(wait_queue);
	req->buf_len = (t_u32)size;
	req->reserved_1 = (t_ptr)((t_u8 *)req + sizeof(mlan_ioctl_req));

	LEAVE();
	return req;
}

/**
 *  @brief This function frees mlan_buffer.
 *  @param handle  A pointer to moal_handle structure
 *  @param pmbuf   Pointer to mlan_buffer
 *
 *  @return        N/A
 */
void
woal_free_mlan_buffer(moal_handle *handle, pmlan_buffer pmbuf)
{
	ENTER();
	if (!pmbuf) {
		LEAVE();
		return;
	}
	if (pmbuf->pdesc)
		dev_kfree_skb_any((struct sk_buff *)pmbuf->pdesc);
	else
		PRINTM(MERROR, "free mlan buffer without pdesc\n");
	atomic_dec(&handle->mbufalloc_count);
	LEAVE();
	return;
}

#ifdef STA_SUPPORT
#endif /* STA_SUPPORT */

/**
 *  @brief This function handles events generated by firmware
 *
 *  @param priv     A pointer to moal_private structure
 *  @param payload  A pointer to payload buffer
 *  @param len      Length of the payload
 *  @return         MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
woal_broadcast_event(moal_private *priv, t_u8 *payload, t_u32 len)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	struct sk_buff *skb = NULL;
	struct nlmsghdr *nlh = NULL;
	moal_handle *handle = priv->phandle;
	struct net_device *netdev = priv->netdev;
	struct sock *sk = handle->nl_sk;

	ENTER();

	/* interface name to be prepended to event */
	if ((len + IFNAMSIZ) > NL_MAX_PAYLOAD
#ifdef WIFI_DIRECT_SUPPORT
	    * 2
#endif
		) {
		PRINTM(MERROR, "event size is too big, len=%d\n", (int)len);
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
	if (sk) {
		/* Allocate skb */
#ifdef WIFI_DIRECT_SUPPORT
		if ((len + IFNAMSIZ) > NL_MAX_PAYLOAD) {
			skb = alloc_skb(NLMSG_SPACE(NL_MAX_PAYLOAD * 2),
					GFP_ATOMIC);
			if (!skb) {
				PRINTM(MERROR,
				       "Could not allocate skb for netlink\n");
				ret = MLAN_STATUS_FAILURE;
				goto done;
			}
		} else {
#endif
			skb = alloc_skb(NLMSG_SPACE(NL_MAX_PAYLOAD),
					GFP_ATOMIC);
			if (!skb) {
				PRINTM(MERROR,
				       "Could not allocate skb for netlink\n");
				ret = MLAN_STATUS_FAILURE;
				goto done;
			}
#ifdef WIFI_DIRECT_SUPPORT
		}
#endif
		nlh = (struct nlmsghdr *)skb->data;
		nlh->nlmsg_len = NLMSG_SPACE(len + IFNAMSIZ);

		/* From kernel */
		nlh->nlmsg_pid = 0;
		nlh->nlmsg_flags = 0;

		/* Data */
		skb_put(skb, nlh->nlmsg_len);
		memcpy(NLMSG_DATA(nlh), netdev->name, IFNAMSIZ);
		memcpy(((t_u8 *)(NLMSG_DATA(nlh))) + IFNAMSIZ, payload, len);

		/* From Kernel */
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 7, 0)
		NETLINK_CB(skb).pid = 0;
#else
		NETLINK_CB(skb).portid = 0;
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 20)
		/* Multicast message */
		NETLINK_CB(skb).dst_pid = 0;
#endif

		/* Multicast group number */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 14)
		NETLINK_CB(skb).dst_groups = NL_MULTICAST_GROUP;
#else
		NETLINK_CB(skb).dst_group = NL_MULTICAST_GROUP;
#endif

		/* Send message */
		ret = netlink_broadcast(sk, skb, 0, NL_MULTICAST_GROUP,
					GFP_ATOMIC);
		if (ret) {
			PRINTM(MWARN, "netlink_broadcast failed: ret=%d\n",
			       ret);
			goto done;
		}

		ret = MLAN_STATUS_SUCCESS;
	} else {
		PRINTM(MERROR,
		       "Could not send event through NETLINK. Link down.\n");
		ret = MLAN_STATUS_FAILURE;
	}
done:
	LEAVE();
	return ret;
}

#ifdef REASSOCIATION
/**
 *  @brief This function handles re-association. it is triggered
 *  by re-assoc timer.
 *
 *  @param data    A pointer to wlan_thread structure
 *  @return        MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
int
woal_reassociation_thread(void *data)
{
	moal_thread *pmoal_thread = data;
	moal_private *priv = NULL;
	moal_handle *handle = (moal_handle *)pmoal_thread->handle;
#if CFG80211_VERSION_CODE < KERNEL_VERSION(4, 13, 0)
	wait_queue_t wait;
#else
	wait_queue_entry_t wait;
#endif
	int i;
	BOOLEAN reassoc_timer_req;
	mlan_802_11_ssid req_ssid;
	mlan_ssid_bssid ssid_bssid;
	mlan_status status;
	mlan_bss_info bss_info;
	t_u32 timer_val = MOAL_TIMER_10S;
	t_u8 zero_mac[] = { 0, 0, 0, 0, 0, 0 };
	ENTER();

	woal_activate_thread(pmoal_thread);
	init_waitqueue_entry(&wait, current);

	current->flags |= PF_NOFREEZE;

	for (;;) {
		add_wait_queue(&pmoal_thread->wait_q, &wait);
		set_current_state(TASK_INTERRUPTIBLE);

		schedule();

		set_current_state(TASK_RUNNING);
		remove_wait_queue(&pmoal_thread->wait_q, &wait);

		/* Cancel re-association timer */
		if (handle->is_reassoc_timer_set == MTRUE) {
			woal_cancel_timer(&handle->reassoc_timer);
			handle->is_reassoc_timer_set = MFALSE;
		}

		if (handle->surprise_removed)
			break;
		if (kthread_should_stop())
			break;

		if (handle->hardware_status != HardwareStatusReady) {
			PRINTM(MINFO,
			       "Reassoc: Hardware status is not correct\n");
			continue;
		}

		PRINTM(MEVENT, "Reassoc: Thread waking up...\n");
		reassoc_timer_req = MFALSE;
#ifdef STA_CFG80211
		for (i = 0;
		     i < MIN(handle->priv_num, MLAN_MAX_BSS_NUM) &&
		     (priv = handle->priv[i]); i++) {
			if (priv->roaming_required) {
				priv->roaming_required = MFALSE;
				PRINTM(MEVENT, "Try to roaming......\n");
				woal_start_roaming(priv);
				break;
			}
		}
#endif

		for (i = 0;
		     i < MIN(handle->priv_num, MLAN_MAX_BSS_NUM) &&
		     (priv = handle->priv[i]); i++) {

			if (priv->reassoc_required == MFALSE) {
				priv->set_asynced_essid_flag = MFALSE;
				continue;
			}

			memset(&bss_info, 0x00, sizeof(bss_info));

			if (MLAN_STATUS_SUCCESS != woal_get_bss_info(priv,
								     MOAL_IOCTL_WAIT,
								     &bss_info))
			{
				PRINTM(MINFO, "Ressoc: Fail to get bss info\n");
				priv->reassoc_required = MFALSE;
				priv->set_asynced_essid_flag = MFALSE;
				continue;
			}

			if (bss_info.bss_mode != MLAN_BSS_MODE_INFRA ||
			    priv->media_connected != MFALSE) {
				PRINTM(MINFO,
				       "Reassoc: ad-hoc mode or media connected\n");
				priv->reassoc_required = MFALSE;
				priv->set_asynced_essid_flag = MFALSE;
				continue;
			}
	    /** avoid on going scan from other thread */
			if (handle->scan_pending_on_block) {
				reassoc_timer_req = MTRUE;
				break;
			}

			/* The semaphore is used to avoid reassociation thread and
			   wlan_set_scan/wlan_set_essid interrupting each other.
			   Reassociation should be disabled completely by application if
			   wlan_set_user_scan_ioctl/wlan_set_wap is used.
			 */
			if (MOAL_ACQ_SEMAPHORE_BLOCK(&handle->reassoc_sem)) {
				PRINTM(MERROR,
				       "Acquire semaphore error, reassociation thread\n");
				reassoc_timer_req = MTRUE;
				break;
			}

			PRINTM(MINFO, "Reassoc: Required ESSID: %s\n",
			       priv->prev_ssid_bssid.ssid.ssid);
			PRINTM(MINFO, "Reassoc: Performing Active Scan\n");

			memset(&req_ssid, 0x00, sizeof(mlan_802_11_ssid));
			memcpy(&req_ssid,
			       &priv->prev_ssid_bssid.ssid,
			       sizeof(mlan_802_11_ssid));

			/* Do specific SSID scanning */
			if (MLAN_STATUS_SUCCESS !=
			    woal_request_scan(priv, MOAL_IOCTL_WAIT,
					      &req_ssid)) {
				PRINTM(MERROR,
				       "Reassoc: Fail to do specific scan\n");
				reassoc_timer_req = MTRUE;
				MOAL_REL_SEMAPHORE(&handle->reassoc_sem);
				break;
			}

			if (handle->surprise_removed) {
				MOAL_REL_SEMAPHORE(&handle->reassoc_sem);
				break;
			}

			memset(&ssid_bssid, 0, sizeof(mlan_ssid_bssid));

			if (priv->set_asynced_essid_flag == MTRUE) {
				if (priv->assoc_with_mac &&
				    memcmp(priv->prev_ssid_bssid.bssid,
					   zero_mac, MLAN_MAC_ADDR_LENGTH)) {
					/* Search AP by BSSID & SSID */
					PRINTM(MINFO,
					       "Reassoc: Search AP by BSSID & SSID\n");
					memcpy(&ssid_bssid.bssid,
					       &priv->prev_ssid_bssid.bssid,
					       MLAN_MAC_ADDR_LENGTH);
				} else {
					/* Search AP by ESSID for asynced essid setting */
					PRINTM(MINFO,
					       "Set asynced essid: Search AP by ESSID\n");
				}

				memcpy(&ssid_bssid.ssid,
				       &priv->prev_ssid_bssid.ssid,
				       sizeof(mlan_802_11_ssid));
			} else {
				/* Search AP by BSSID first */
				PRINTM(MINFO,
				       "Reassoc: Search AP by BSSID first\n");
				memcpy(&ssid_bssid.bssid,
				       &priv->prev_ssid_bssid.bssid,
				       MLAN_MAC_ADDR_LENGTH);
			}

			status = woal_find_best_network(priv, MOAL_IOCTL_WAIT,
							&ssid_bssid);
#ifdef STA_WEXT
			if (status == MLAN_STATUS_SUCCESS) {
				if (MLAN_STATUS_SUCCESS !=
				    woal_11d_check_ap_channel(priv,
							      MOAL_IOCTL_WAIT,
							      &ssid_bssid)) {
					PRINTM(MERROR,
					       "Reassoc: The AP's channel is invalid for current region\n");
					status = MLAN_STATUS_FAILURE;
				}
			}
#endif
	    /** The find AP without ssid, we need re-search */
			if (status == MLAN_STATUS_SUCCESS &&
			    !ssid_bssid.ssid.ssid_len) {
				PRINTM(MINFO,
				       "Reassoc: Skip AP without ssid\n");
				status = MLAN_STATUS_FAILURE;
			}

			if (priv->set_asynced_essid_flag != MTRUE &&
			    MLAN_STATUS_SUCCESS != status) {
				PRINTM(MINFO,
				       "Reassoc: AP not found in scan list\n");
				PRINTM(MINFO, "Reassoc: Search AP by SSID\n");
				/* Search AP by SSID */
				memset(&ssid_bssid, 0, sizeof(mlan_ssid_bssid));
				memcpy(&ssid_bssid.ssid,
				       &priv->prev_ssid_bssid.ssid,
				       sizeof(mlan_802_11_ssid));
				status = woal_find_best_network(priv,
								MOAL_IOCTL_WAIT,
								&ssid_bssid);
#ifdef STA_WEXT
				if (status == MLAN_STATUS_SUCCESS) {
					if (MLAN_STATUS_SUCCESS !=
					    woal_11d_check_ap_channel(priv,
								      MOAL_IOCTL_WAIT,
								      &ssid_bssid))
					{
						PRINTM(MERROR,
						       "Reassoc: The AP's channel is invalid for current region\n");
						status = MLAN_STATUS_FAILURE;
					}
				}
#endif
			}

			if (status == MLAN_STATUS_SUCCESS) {
				/* set the wep key */
				if (bss_info.wep_status)
					woal_enable_wep_key(priv,
							    MOAL_IOCTL_WAIT);
				/* Zero SSID implies use BSSID to connect */
				memset(&ssid_bssid.ssid, 0,
				       sizeof(mlan_802_11_ssid));
				status = woal_bss_start(priv, MOAL_IOCTL_WAIT,
							&ssid_bssid);
			}

			if (priv->media_connected == MFALSE)
				reassoc_timer_req = MTRUE;
			else {
				mlan_ds_rate *rate = NULL;
				mlan_ioctl_req *req = NULL;

				reassoc_timer_req = MFALSE;
				if (priv->set_asynced_essid_flag == MTRUE) {
					memset(&bss_info, 0, sizeof(bss_info));
					if (MLAN_STATUS_SUCCESS !=
					    woal_get_bss_info(priv,
							      MOAL_IOCTL_WAIT,
							      &bss_info)) {
						PRINTM(MINFO,
						       "Set asynced essid: Fail to get bss info after assoc\n");
					} else {
						memcpy(&priv->prev_ssid_bssid.
						       ssid, &bss_info.ssid,
						       sizeof
						       (mlan_802_11_ssid));
						memcpy(&priv->prev_ssid_bssid.
						       bssid, &bss_info.bssid,
						       MLAN_MAC_ADDR_LENGTH);
					}
					priv->set_asynced_essid_flag = MFALSE;
				}
				if (priv->rate_index != AUTO_RATE) {
					req = woal_alloc_mlan_ioctl_req(sizeof
									(mlan_ds_rate));

					if (req == NULL) {
						LEAVE();
						return MLAN_STATUS_FAILURE;
					}

					rate = (mlan_ds_rate *)req->pbuf;
					rate->param.rate_cfg.rate_type =
						MLAN_RATE_INDEX;
					rate->sub_command = MLAN_OID_RATE_CFG;
					req->req_id = MLAN_IOCTL_RATE;

					req->action = MLAN_ACT_SET;

					rate->param.rate_cfg.rate =
						priv->rate_index;

					status = woal_request_ioctl(priv, req,
								    MOAL_IOCTL_WAIT);
					if (status != MLAN_STATUS_SUCCESS) {
						if (status !=
						    MLAN_STATUS_PENDING)
							kfree(req);
						LEAVE();
						return MLAN_STATUS_FAILURE;
					}
					kfree(req);
				}
			}

			MOAL_REL_SEMAPHORE(&handle->reassoc_sem);
		}

		if (handle->surprise_removed)
			break;

		if (reassoc_timer_req == MTRUE) {
			handle->is_reassoc_timer_set = MTRUE;
			if (priv && (priv->set_asynced_essid_flag == MTRUE)) {
				PRINTM(MERROR,
				       "Set Async ESSID: No AP found or assoc failed.\n");
				priv->set_asynced_essid_flag = MFALSE;
			} else {
				PRINTM(MEVENT,
				       "Reassoc: No AP found or assoc failed. "
				       "Restarting re-assoc Timer: %d\n",
				       (int)timer_val);
				woal_mod_timer(&handle->reassoc_timer,
					       timer_val);
			}
		} else {
			if (priv) {
				priv->set_asynced_essid_flag = MFALSE;
			}
		}
	}
	woal_deactivate_thread(pmoal_thread);

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function triggers re-association by waking up
 *  re-assoc thread.
 *
 *  @param context  A pointer to context
 *  @return         N/A
 */
void
woal_reassoc_timer_func(void *context)
{
	moal_handle *handle = (moal_handle *)context;

	ENTER();

	PRINTM(MINFO, "reassoc_timer fired.\n");
	handle->is_reassoc_timer_set = MFALSE;

	PRINTM(MINFO, "Waking Up the Reassoc Thread\n");
	wake_up_interruptible(&handle->reassoc_thread.wait_q);

	LEAVE();
	return;
}
#endif /* REASSOCIATION */

#ifdef STA_SUPPORT
/**
 *  @brief update dscp mapping from assoc_resp/reassoc_resp
 *
 *  @param priv      Pointer to the moal_private driver data struct
 *
 *  @return          N/A
 */
void
woal_update_dscp_mapping(moal_private *priv)
{
	mlan_ds_misc_assoc_rsp assoc_rsp;
	IEEEtypes_AssocRsp_t *passoc_rsp = NULL;
	IEEEtypes_Header_t *qos_mapping_ie = NULL;
	DSCP_Range_t *pdscp_range = NULL;
	t_u8 dscp_except_num = 0;
	DSCP_Exception_t dscp_except[MAX_DSCP_EXCEPTION_NUM];
	int i, j;
	ENTER();

	memset(&assoc_rsp, 0, sizeof(mlan_ds_misc_assoc_rsp));
	woal_get_assoc_rsp(priv, &assoc_rsp, MOAL_NO_WAIT);
	passoc_rsp = (IEEEtypes_AssocRsp_t *)assoc_rsp.assoc_resp_buf;
	memset(priv->dscp_map, 0xFF, sizeof(priv->dscp_map));
	qos_mapping_ie =
		(IEEEtypes_Header_t *)woal_parse_ie_tlv(passoc_rsp->ie_buffer,
							assoc_rsp.
							assoc_resp_len -
							ASSOC_RESP_FIXED_SIZE,
							QOS_MAPPING);
	if (qos_mapping_ie &&
	    (qos_mapping_ie->len >= (sizeof(DSCP_Range_t) * MAX_NUM_TID))) {
		dscp_except_num =
			(qos_mapping_ie->len -
			 sizeof(DSCP_Range_t) * MAX_NUM_TID) /
			sizeof(DSCP_Exception_t);
		if (dscp_except_num > MAX_DSCP_EXCEPTION_NUM) {
			PRINTM(MERROR, "dscp_except_num exceeds MAX limit\n");
			LEAVE();
			return;
		}
		memcpy(dscp_except,
		       (t_u8 *)qos_mapping_ie + sizeof(IEEEtypes_Header_t),
		       dscp_except_num * sizeof(DSCP_Exception_t));
		pdscp_range =
			(DSCP_Range_t *)((t_u8 *)qos_mapping_ie +
					 sizeof(IEEEtypes_Header_t) +
					 dscp_except_num *
					 sizeof(DSCP_Exception_t));
		for (i = 0; i < MAX_NUM_TID; i++) {
			PRINTM(MEVENT, "TID %d: dscp_low=%d, dscp_high=%d\n", i,
			       pdscp_range->dscp_low_value,
			       pdscp_range->dscp_high_value);
			if (pdscp_range->dscp_low_value != 0xff &&
			    pdscp_range->dscp_high_value != 0xff &&
			    pdscp_range->dscp_high_value <= 63) {
				for (j = pdscp_range->dscp_low_value;
				     j <= pdscp_range->dscp_high_value; j++)
					priv->dscp_map[j] = i;
			}
			pdscp_range++;
		}
		for (i = 0; i < dscp_except_num; i++) {
			if ((dscp_except[i].dscp_value <= 63) &&
			    (dscp_except[i].user_priority <= 7)) {
				PRINTM(MEVENT,
				       "dscp excpt: value=%d priority=%d\n",
				       dscp_except[i].dscp_value,
				       dscp_except[i].user_priority);
				priv->dscp_map[dscp_except[i].dscp_value] =
					dscp_except[i].user_priority;
			}
		}
	}
	LEAVE();
}

/**
 *  @brief Sends disconnect event
 *
 *  @param priv A pointer to moal_private struct
 *  @return     N/A
 */
t_void
woal_send_disconnect_to_system(moal_private *priv)
{
	int custom_len = 0;
	t_u8 event_buf[32];
#ifdef STA_WEXT
	union iwreq_data wrqu;
#endif
#ifdef STA_CFG80211
	unsigned long flags;
#endif
	mlan_ds_misc_gtk_rekey_data zero_gtk;

	ENTER();
	priv->media_connected = MFALSE;
	woal_stop_queue(priv->netdev);
	if (netif_carrier_ok(priv->netdev))
		netif_carrier_off(priv->netdev);
	woal_flush_tcp_sess_queue(priv);

	priv->gtk_data_ready = MFALSE;
	memset(&zero_gtk, 0x00, sizeof(zero_gtk));
	if (gtk_rekey_offload == GTK_REKEY_OFFLOAD_ENABLE &&
	    memcmp(&priv->gtk_rekey_data, &zero_gtk,
		   sizeof(priv->gtk_rekey_data)) != 0) {
		PRINTM(MCMND, "clear GTK in woal_send_disconnect_to_system\n");
		woal_set_rekey_data(priv, NULL, MLAN_ACT_CLEAR);
	}
	memset(&priv->gtk_rekey_data, 0, sizeof(mlan_ds_misc_gtk_rekey_data));

	if (priv->bss_type == MLAN_BSS_TYPE_STA)
		woal_flush_tdls_list(priv);
#ifdef STA_CFG80211
	if (priv->bss_type == MLAN_BSS_TYPE_STA &&
	    IS_STA_CFG80211(cfg80211_wext)) {
		woal_flush_pmksa_list(priv);
		if (priv->okc_roaming_ie) {
			kfree(priv->okc_roaming_ie);
			priv->okc_roaming_ie = NULL;
			priv->okc_ie_len = 0;
		}
	}
#endif
	if (priv->bss_type == MLAN_BSS_TYPE_STA && priv->hist_data)
		woal_hist_data_reset(priv);

#ifdef STA_WEXT
	if (IS_STA_WEXT(cfg80211_wext)) {
		memset(wrqu.ap_addr.sa_data, 0x00, ETH_ALEN);
		wrqu.ap_addr.sa_family = ARPHRD_ETHER;
		wireless_send_event(priv->netdev, SIOCGIWAP, &wrqu, NULL);
	}
#endif
#ifdef STA_CFG80211
	if (IS_STA_CFG80211(cfg80211_wext)) {
		spin_lock_irqsave(&priv->connect_lock, flags);
		if (!priv->cfg_disconnect && !priv->cfg_connect &&
		    priv->wdev &&
		    priv->wdev->iftype != NL80211_IFTYPE_ADHOC
		    && ((priv->bss_type != MLAN_BSS_TYPE_STA) ||
			(priv->bss_type == MLAN_BSS_TYPE_STA &&
			 priv->sme_current.ssid))
			) {
			PRINTM(MMSG,
			       "wlan: Disconnected from " MACSTR
			       ": Reason code %d\n", MAC2STR(priv->cfg_bssid),
			       WLAN_REASON_DEAUTH_LEAVING);
			spin_unlock_irqrestore(&priv->connect_lock, flags);
			/* This function must be called only when disconnect issued by
			   the FW, i.e. disconnected by AP. For IBSS mode this call is
			   not valid */
			cfg80211_disconnected(priv->netdev,
					      WLAN_REASON_DEAUTH_LEAVING, NULL,
					      0,
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 2, 0)
					      false,
#endif
					      GFP_KERNEL);
		} else {
			spin_unlock_irqrestore(&priv->connect_lock, flags);
		}
		if (!woal_is_any_interface_active(priv->phandle))
			woal_set_scan_time(priv, ACTIVE_SCAN_CHAN_TIME,
					   PASSIVE_SCAN_CHAN_TIME,
					   SPECIFIC_SCAN_CHAN_TIME);
		priv->ft_ie_len = 0;
		priv->ft_pre_connect = MFALSE;
		memset(priv->dscp_map, 0xFF, sizeof(priv->dscp_map));
	}
#endif /* STA_CFG80211 */

	memset(event_buf, 0, sizeof(event_buf));
	custom_len = strlen(CUS_EVT_AP_CONNECTED);
	strncpy(event_buf, CUS_EVT_AP_CONNECTED,
		MIN((sizeof(event_buf) - 1), custom_len));
	woal_broadcast_event(priv, event_buf, custom_len + ETH_ALEN);
	LEAVE();
}
#endif /* STA_SUPPORT */

#define OFFSET_SEQNUM 4
/**
 *  @brief  This function stores the FW dumps received from events in a file
 *
 *  @param phandle     A pointer to moal_handle
 *  @param pmevent  A pointer to mlan_event structure
 *
 *  @return         N/A
 */

t_void
woal_store_firmware_dump(moal_handle *phandle, mlan_event *pmevent)
{
	struct file *pfile_fwdump = NULL;
	loff_t pos = 0;
	t_u16 seqnum;
	t_u8 path_name[64];

	ENTER();
	if (phandle->fwdump_fname)
		pfile_fwdump =
			filp_open(phandle->fwdump_fname,
				  O_CREAT | O_WRONLY | O_APPEND, 0644);
	else {
		seqnum = woal_le16_to_cpu(*(t_u16 *)
					  (pmevent->event_buf + OFFSET_SEQNUM));
		if (seqnum == 1) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 1, 0)
	    /** Create dump directort*/
			woal_create_dump_dir(phandle, path_name,
					     sizeof(path_name));
#else
			memset(path_name, 0, sizeof(path_name));
			strcpy(path_name, "/data");
#endif
			PRINTM(MMSG, "Firmware Dump directory name is %s\n",
			       path_name);
			woal_dump_drv_info(phandle, path_name);
			if (fwdump_fname) {
				memset(fwdump_fname, 0, 64);
			} else {
				gfp_t flag;
				flag = (in_atomic() ||
					irqs_disabled())? GFP_ATOMIC :
					GFP_KERNEL;
				fwdump_fname = kzalloc(64, flag);
			}
			sprintf(fwdump_fname, "%s/file_fwdump", path_name);
			pfile_fwdump =
				filp_open(fwdump_fname,
					  O_CREAT | O_WRONLY | O_APPEND, 0644);
			if (IS_ERR(pfile_fwdump)) {
				memset(fwdump_fname, 0, 64);
				sprintf(fwdump_fname, "%s/%s", "/var",
					"file_fwdump");
				pfile_fwdump =
					filp_open(fwdump_fname,
						  O_CREAT | O_WRONLY | O_APPEND,
						  0644);
			}
		} else
			pfile_fwdump =
				filp_open(fwdump_fname,
					  O_CREAT | O_WRONLY | O_APPEND, 0644);
	}
	if (IS_ERR(pfile_fwdump)) {
		PRINTM(MERROR, "Cannot create firmware dump file\n");
		LEAVE();
		return;
	}
	vfs_write(pfile_fwdump, pmevent->event_buf, pmevent->event_len, &pos);
	filp_close(pfile_fwdump, NULL);
	LEAVE();
	return;
}

#define DRV_INFO_SIZE 0x40000
#define ROW_SIZE_16      16
#define ROW_SIZE_32      32
/**
 *  @brief This function save moal_priv's debug log
 *
 *  @param phandle   A pointer to moal_handle
 *  @param buf       A pointer buffer saving log
 *
 *  @return          The length of this log
 */
static int
woal_dump_priv_drv_info(moal_handle *handle, t_u8 *buf)
{
	char *ptr = (char *)buf;
	int index;
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 29)
	int i = 0;
#endif
	moal_private *priv;

	ENTER();
	if (!handle || !buf) {
		PRINTM(MMSG, "%s: can't retreive info\n", __func__);
		LEAVE();
		return 0;
	}
	for (index = 0; index < MIN(handle->priv_num, MLAN_MAX_BSS_NUM);
	     index++) {
		priv = handle->priv[index];
		if (priv) {
			ptr += sprintf(ptr, "[Interface : %s]\n",
				       priv->proc_entry_name);
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 29)
			ptr += sprintf(ptr, "wmm_tx_pending[0] = %d\n",
				       atomic_read(&priv->wmm_tx_pending[0]));
			ptr += sprintf(ptr, "wmm_tx_pending[1] = %d\n",
				       atomic_read(&priv->wmm_tx_pending[1]));
			ptr += sprintf(ptr, "wmm_tx_pending[2] = %d\n",
				       atomic_read(&priv->wmm_tx_pending[2]));
			ptr += sprintf(ptr, "wmm_tx_pending[3] = %d\n",
				       atomic_read(&priv->wmm_tx_pending[3]));
#endif
			ptr += sprintf(ptr, "Media state = \"%s\"\n",
				       ((priv->media_connected ==
					 MFALSE) ? "Disconnected" :
					"Connected"));
			ptr += sprintf(ptr, "carrier %s\n",
				       ((netif_carrier_ok(priv->netdev)) ? "on"
					: "off"));
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 29)
			for (i = 0; i < (priv->netdev->num_tx_queues); i++) {
				ptr += sprintf(ptr, "tx queue %d: %s\n", i,
					       ((netif_tx_queue_stopped
						 (netdev_get_tx_queue
						  (priv->netdev,
						   i))) ? "stopped" :
						"started"));
			}
#else
			ptr += sprintf(ptr, "tx queue %s\n",
				       ((netif_queue_stopped(priv->netdev)) ?
					"stopped" : "started"));
#endif
			ptr += sprintf(ptr, "%s: num_tx_timeout = %d\n",
				       priv->netdev->name,
				       priv->num_tx_timeout);
		}
	}

	LEAVE();
	return ptr - (char *)buf;
}

#define SDIO_SCRATCH_REG 0xE8
/**
 *  @brief This function save sdio reg info
 *
 *  @param phandle   A pointer to moal_handle
 *  @param buf       A pointer buffer saving log
 *
 *  @return          The length of this log
 */
static int
woal_dump_sdio_reg_info(moal_handle *phandle, t_u8 *drv_buf)
{
	char *drv_ptr = (char *)drv_buf;
	int ret = 0;
	t_u8 loop, index = 0, func, data;
	unsigned int reg, reg_start, reg_end;
	unsigned int scratch_reg = SDIO_SCRATCH_REG;
	unsigned int reg_table[] = { 0x08, 0x58, 0x5C, 0x5D, 0x60, 0x61, 0x62,
		0x64, 0x65, 0x66, 0x68, 0x69, 0x6a
	};
	char buf[256], *ptr;

	ENTER();

	if (!phandle || !drv_buf) {
		PRINTM(MMSG, "%s: can't retreive info\n", __func__);
		LEAVE();
		return 0;
	}
	mlan_pm_wakeup_card(phandle->pmlan_adapter);

	drv_ptr += sprintf(drv_ptr, "--------sdio_reg_debug_info---------\n");
	sdio_claim_host(((struct sdio_mmc_card *)phandle->card)->func);
	for (loop = 0; loop < 5; loop++) {
		memset(buf, 0, sizeof(buf));
		ptr = buf;
		if (loop == 0) {
			/* Read the registers of SDIO function0 */
			func = loop;
			reg_start = 0;
			reg_end = 9;

		} else if (loop == 1) {
			/* Read the registers of SDIO function1 */
			func = loop;
			reg_start = 0x10;
			reg_end = 0x17;
		} else if (loop == 2) {
			/* Read specific registers of SDIO function1 */
			index = 0;
			func = 1;
			reg_start = reg_table[index++];
			reg_end =
				reg_table[sizeof(reg_table) / sizeof(int) - 1];
		} else {
			/* Read the scratch registers of SDIO function1 */
			if (loop == 4)
				mdelay(100);
			func = 1;
			reg_start = scratch_reg;
			reg_end = scratch_reg + 10;
		}
		if (loop != 2)
			ptr += sprintf(ptr, "SDIO Func%d (%#x-%#x): ", func,
				       reg_start, reg_end);
		else
			ptr += sprintf(ptr, "SDIO Func%d: ", func);
		for (reg = reg_start; reg <= reg_end;) {
			if (func == 0)
				data = sdio_f0_readb(((struct sdio_mmc_card *)
						      phandle->card)->func, reg,
						     &ret);
			else
				data = sdio_readb(((struct sdio_mmc_card *)
						   phandle->card)->func, reg,
						  &ret);
			if (loop == 2)
				ptr += sprintf(ptr, "(%#x) ", reg);
			if (!ret)
				ptr += sprintf(ptr, "%02x ", data);
			else {
				ptr += sprintf(ptr, "ERR");
				break;
			}
			if (loop == 2 && reg < reg_end)
				reg = reg_table[index++];
			else
				reg++;
		}
		drv_ptr += sprintf(drv_ptr, "%s\n", buf);
	}
	sdio_release_host(((struct sdio_mmc_card *)phandle->card)->func);

	drv_ptr +=
		sprintf(drv_ptr, "--------sdio_reg_debug_info End---------\n");

	LEAVE();
	return drv_ptr - (char *)drv_buf;
}

/**
 *  @brief This function save moal_handle's info
 *
 *  @param phandle   A pointer to moal_handle
 *  @param buf       A pointer buffer saving log
 *
 *  @return          The length of this log
 */
static int
woal_dump_moal_drv_info(moal_handle *phandle, t_u8 *buf)
{
	char *ptr;
	char str_buf[MLAN_MAX_VER_STR_LEN];

	ENTER();
	if (!phandle || !buf) {
		PRINTM(MMSG, "%s: can't retreive info\n", __func__);
		LEAVE();
		return 0;
	}
	ptr = (char *)buf;
	ptr += sprintf(ptr, "------------moal_debug_info-------------\n");
	woal_get_version(phandle, str_buf, sizeof(str_buf) - 1);
	ptr += sprintf(ptr, "Driver version = %s\n", str_buf);
	ptr += sprintf(ptr, "main_state = %d\n", phandle->main_state);
	ptr += sprintf(ptr, "ioctl_pending = %d\n",
		       atomic_read(&phandle->ioctl_pending));
	ptr += sprintf(ptr, "tx_pending = %d\n",
		       atomic_read(&phandle->tx_pending));
	ptr += sprintf(ptr, "rx_pending = %d\n",
		       atomic_read(&phandle->rx_pending));
	ptr += sprintf(ptr, "lock_count = %d\n",
		       atomic_read(&phandle->lock_count));
	ptr += sprintf(ptr, "malloc_count = %d\n",
		       atomic_read(&phandle->malloc_count));
	ptr += sprintf(ptr, "mbufalloc_count = %d\n",
		       atomic_read(&phandle->mbufalloc_count));
#if defined(SDIO_SUSPEND_RESUME)
	ptr += sprintf(ptr, "hs_skip_count = %u\n", phandle->hs_skip_count);
	ptr += sprintf(ptr, "hs_force_count = %u\n", phandle->hs_force_count);
#endif

	ptr += woal_dump_priv_drv_info(phandle, ptr);
	ptr += sprintf(ptr, "------------moal_debug_info End-------------\n");

	ptr += woal_dump_sdio_reg_info(phandle, ptr);

	LEAVE();
	return ptr - (char *)buf;
}

/**
 *  @brief This function save mlan's info
 *
 *  @param phandle   A pointer to moal_handle
 *  @param buf       A pointer buffer saving log
 *
 *  @return          The length of this log
 */
static int
woal_dump_mlan_drv_info(moal_private *priv, t_u8 *buf)
{
	char *ptr = (char *)buf;
	int i;
#ifdef SDIO_MULTI_PORT_TX_AGGR
	int j;
	t_u8 mp_aggr_pkt_limit = SDIO_MP_AGGR_DEF_PKT_LIMIT;
#endif
	char str[11 * DBG_CMD_NUM + 1] = { 0 };
	char *s;

	ENTER();
	if (!priv || woal_get_debug_info(priv, MOAL_IOCTL_WAIT, &info)) {
		PRINTM(MERROR,
		       "Could not retrieve debug information from MLAN\n");
		LEAVE();
		return 0;
	}
	ptr += sprintf(ptr, "------------mlan_debug_info-------------\n");
	ptr += sprintf(ptr, "mlan_processing =%d\n", info.mlan_processing);
	ptr += sprintf(ptr, "main_lock_flag =%d\n", info.main_lock_flag);
	ptr += sprintf(ptr, "main_process_cnt =%d\n", info.main_process_cnt);
	ptr += sprintf(ptr, "delay_task_flag =%d\n", info.delay_task_flag);
	ptr += sprintf(ptr, "mlan_rx_processing =%d\n",
		       info.mlan_rx_processing);
	ptr += sprintf(ptr, "rx_pkts_queued =%d\n", info.rx_pkts_queued);
	ptr += sprintf(ptr, "tx_pkts_queued =%d\n", info.tx_pkts_queued);

	ptr += sprintf(ptr, "num_cmd_timeout = %d\n", info.num_cmd_timeout);
	ptr += sprintf(ptr, "dbg.num_cmd_timeout = %d\n",
		       info.dbg_num_cmd_timeout);
	ptr += sprintf(ptr, "Timeout cmd id = 0x%x, act = 0x%x\n",
		       info.timeout_cmd_id, info.timeout_cmd_act);
	ptr += sprintf(ptr, "last_cmd_index = %d\n", info.last_cmd_index);
	for (s = str, i = 0; i < DBG_CMD_NUM; i++)
		s += sprintf(s, "0x%x ", info.last_cmd_id[i]);
	ptr += sprintf(ptr, "last_cmd_id = %s\n", str);

	for (s = str, i = 0; i < DBG_CMD_NUM; i++)
		s += sprintf(s, "0x%x ", info.last_cmd_act[i]);

	ptr += sprintf(ptr, "last_cmd_act = %s\n", str);
	ptr += sprintf(ptr, "last_cmd_resp_index = %d\n",
		       info.last_cmd_resp_index);
	for (s = str, i = 0; i < DBG_CMD_NUM; i++)
		s += sprintf(s, "0x%x ", info.last_cmd_resp_id[i]);

	ptr += sprintf(ptr, "last_cmd_resp_id = %s\n", str);
	ptr += sprintf(ptr, "last_event_index = %d\n", info.last_event_index);
	for (s = str, i = 0; i < DBG_CMD_NUM; i++)
		s += sprintf(s, "0x%x ", info.last_event[i]);

	ptr += sprintf(ptr, "last_event = %s\n", str);
	ptr += sprintf(ptr, "num_data_h2c_failure = %d\n",
		       info.num_tx_host_to_card_failure);
	ptr += sprintf(ptr, "num_cmd_h2c_failure = %d\n",
		       info.num_cmd_host_to_card_failure);
	ptr += sprintf(ptr, "num_alloc_buffer_failure = %d\n",
		       info.num_alloc_buffer_failure);
	ptr += sprintf(ptr, "num_pkt_dropped = %d\n", info.num_pkt_dropped);
	ptr += sprintf(ptr, "num_data_c2h_failure = %d\n",
		       info.num_rx_card_to_host_failure);
	ptr += sprintf(ptr, "num_cmdevt_c2h_failure = %d\n",
		       info.num_cmdevt_card_to_host_failure);
	ptr += sprintf(ptr, "num_int_read_failure = %d\n",
		       info.num_int_read_failure);
	ptr += sprintf(ptr, "last_int_status = %d\n", info.last_int_status);
	ptr += sprintf(ptr, "num_event_deauth = %d\n", info.num_event_deauth);
	ptr += sprintf(ptr, "num_event_disassoc = %d\n",
		       info.num_event_disassoc);
	ptr += sprintf(ptr, "num_event_link_lost = %d\n",
		       info.num_event_link_lost);
	ptr += sprintf(ptr, "num_cmd_deauth = %d\n", info.num_cmd_deauth);
	ptr += sprintf(ptr, "num_cmd_assoc_success = %d\n",
		       info.num_cmd_assoc_success);
	ptr += sprintf(ptr, "num_cmd_assoc_failure = %d\n",
		       info.num_cmd_assoc_failure);
	ptr += sprintf(ptr, "cmd_resp_received = %d\n", info.cmd_resp_received);
	ptr += sprintf(ptr, "event_received = %d\n", info.event_received);
	ptr += sprintf(ptr, "max_tx_buf_size = %d\n", info.max_tx_buf_size);
	ptr += sprintf(ptr, "tx_buf_size = %d\n", info.tx_buf_size);
	ptr += sprintf(ptr, "curr_tx_buf_size = %d\n", info.curr_tx_buf_size);

	ptr += sprintf(ptr, "data_sent=%d cmd_sent=%d\n", info.data_sent,
		       info.cmd_sent);
	ptr += sprintf(ptr, "ps_mode=%d ps_state=%d\n", info.ps_mode,
		       info.ps_state);
	ptr += sprintf(ptr, "wakeup_dev_req=%d wakeup_tries=%d\n",
		       info.pm_wakeup_card_req, info.pm_wakeup_fw_try);
	ptr += sprintf(ptr, "hs_configured=%d hs_activated=%d\n",
		       info.is_hs_configured, info.hs_activated);
	ptr += sprintf(ptr, "pps_uapsd_mode=%d sleep_pd=%d\n",
		       info.pps_uapsd_mode, info.sleep_pd);
	ptr += sprintf(ptr, "tx_lock_flag = %d\n", info.tx_lock_flag);
	ptr += sprintf(ptr, "port_open = %d\n", info.port_open);
	ptr += sprintf(ptr, "scan_processing = %d\n", info.scan_processing);

	ptr += sprintf(ptr, "mp_rd_bitmap=0x%x curr_rd_port=0x%x\n",
		       (unsigned int)info.mp_rd_bitmap, info.curr_rd_port);
	ptr += sprintf(ptr, "mp_wr_bitmap=0x%x curr_wr_port=0x%x\n",
		       (unsigned int)info.mp_wr_bitmap, info.curr_wr_port);
	ptr += sprintf(ptr, "mp_invalid_update=%d\n", info.mp_invalid_update);
#ifdef SDIO_MULTI_PORT_TX_AGGR
	ptr += sprintf(ptr, "last_recv_wr_bitmap=0x%x last_mp_index = %d\n",
		       info.last_recv_wr_bitmap, info.last_mp_index);
	for (i = 0; i < SDIO_MP_DBG_NUM; i++) {
		for (s = str, j = 0; j < mp_aggr_pkt_limit; j++)
			s += sprintf(s, "0x%02x ",
				     info.last_mp_wr_info[i *
							  mp_aggr_pkt_limit +
							  j]);

		ptr += sprintf(ptr,
			       "mp_wr_bitmap: 0x%x mp_wr_ports=0x%x len=%d curr_wr_port=0x%x\n%s\n",
			       info.last_mp_wr_bitmap[i],
			       info.last_mp_wr_ports[i], info.last_mp_wr_len[i],
			       info.last_curr_wr_port[i], str);
	}
#endif
	ptr += sprintf(ptr, "------------mlan_debug_info End-------------\n");

	LEAVE();
	return ptr - (char *)buf;
}

/**
 *  @brief This function dump hex to file
 *
 *  @param phandle   A pointer to moal_handle
 *  @param buf       A pointer to buffer to dump
 *  @param len       lengh of buf
 *  @param ascii     Whether add ascii at the end
 *  @param save_buf  Buffer which is saved to
 *
 *  @return          The length of this log
 */
static int
woal_save_hex_dump(int rowsize, const void *buf, size_t len,
		   bool ascii, t_u8 *save_buf)
{
	const u8 *ptr = buf;
	int i, linelen, remaining = len;
	unsigned char linebuf[ROW_SIZE_32 * 3 + 2 + ROW_SIZE_32 + 1];
	char *pos = (char *)save_buf;

	if (rowsize != ROW_SIZE_16 && rowsize != ROW_SIZE_32)
		rowsize = ROW_SIZE_16;

	for (i = 0; i < len; i += rowsize) {
		linelen = min(remaining, rowsize);
		remaining -= rowsize;

		hex_dump_to_buffer(ptr + i, linelen, rowsize, 1, linebuf,
				   sizeof(linebuf), ascii);

		pos += sprintf(pos, "%p: %s\n", ptr + i, linebuf);
	}

	return pos - (char *)save_buf;
}

/**
 *  @brief This function dump moal hex to file
 *
 *  @param phandle   A pointer to moal_handle
 *  @param buf       A pointer to buffer
 *
 *  @return          The length of this log
 */
static int
woal_dump_moal_hex(moal_handle *phandle, t_u8 *buf)
{
	char *ptr = (char *)buf;
	int i;
	ENTER();

	if (!phandle || !buf) {
		PRINTM(MMSG, "%s: can't retreive info\n", __func__);
		LEAVE();
		return 0;
	}

	ptr += sprintf(ptr, "<--moal_handle-->\n");
	ptr += sprintf(ptr, "moal_handle=%p, size=%ld(0x%lx)\n", phandle,
		       (long int)sizeof(*phandle),
		       (long unsigned int)sizeof(*phandle));
	ptr += woal_save_hex_dump(ROW_SIZE_16, phandle, sizeof(*phandle), MTRUE,
				  ptr);
	ptr += sprintf(ptr, "<--moal_handle End-->\n");

	for (i = 0; i < phandle->priv_num; i++) {
		ptr += sprintf(ptr, "<--moal_private(%d)-->\n", i);
		ptr += sprintf(ptr, "moal_private=%p, size=%ld(0x%lx)\n",
			       phandle->priv[i],
			       (long int)sizeof(*(phandle->priv[i])),
			       (long unsigned int)sizeof(*(phandle->priv[i])));
		ptr += woal_save_hex_dump(ROW_SIZE_16, phandle->priv[i],
					  sizeof(*(phandle->priv[i])), MTRUE,
					  ptr);
		ptr += sprintf(ptr, "<--moal_private(%d) End-->\n", i);
	}
	LEAVE();
	return ptr - (char *)buf;
}

/**
 *  @brief This function dump mlan hex to file
 *
 *  @param priv   A pointer to moal_private structure
 *  @param buf       A pointer to buffer
 *  @param pfile  A pointer to file structure
 *
 *  @return          The length of this log
 */
static int
woal_dump_mlan_hex(moal_private *priv, t_u8 *buf, struct file *pfile)
{
	char *ptr = (char *)buf;
	int i;
	int len = 0;

	ENTER();

	if (!buf || !priv || !pfile ||
	    woal_get_debug_info(priv, MOAL_IOCTL_WAIT, &info)) {
		PRINTM(MMSG, "%s: can't retreive info\n", __func__);
		LEAVE();
		return 0;
	}

	ptr += sprintf(ptr, "<--mlan_adapter-->\n");
	ptr += sprintf(ptr, "mlan_adapter=%p, size=%d(0x%x)\n",
		       info.mlan_adapter, info.mlan_adapter_size,
		       info.mlan_adapter_size);
	ptr += woal_save_hex_dump(ROW_SIZE_16, info.mlan_adapter,
				  info.mlan_adapter_size, MTRUE, ptr);
	ptr += sprintf(ptr, "<--mlan_adapter End-->\n");
	vfs_write(pfile, buf, ptr - (char *)buf, &pfile->f_pos);
	len += ptr - (char *)buf;
#ifdef SDIO_MULTI_PORT_TX_AGGR
	if (info.mpa_buf && info.mpa_buf_size) {
		ptr = (char *)buf;
		ptr += sprintf(ptr, "<--mlan_mpa_buf-->\n");
		ptr += sprintf(ptr, "mlan_mpa_buf=%p, size=%d(0x%x)\n",
			       info.mpa_buf, info.mpa_buf_size,
			       info.mpa_buf_size);
		ptr += woal_save_hex_dump(ROW_SIZE_16, info.mpa_buf,
					  info.mpa_buf_size, MTRUE, ptr);
		ptr += sprintf(ptr, "<--mlan_mpa_buf End-->\n");
		vfs_write(pfile, buf, ptr - (char *)buf, &pfile->f_pos);
		len += ptr - (char *)buf;
	}
#endif
	for (i = 0; i < info.mlan_priv_num; i++) {
		ptr = (char *)buf;
		ptr += sprintf(ptr, "<--mlan_private(%d)-->\n", i);
		ptr += sprintf(ptr, "mlan_private=%p, size=%d(0x%x)\n",
			       info.mlan_priv[i], info.mlan_priv_size[i],
			       info.mlan_priv_size[i]);
		ptr += woal_save_hex_dump(ROW_SIZE_16, info.mlan_priv[i],
					  info.mlan_priv_size[i], MTRUE, ptr);
		ptr += sprintf(ptr, "<--mlan_private(%d) End-->\n", i);
		vfs_write(pfile, buf, ptr - (char *)buf, &pfile->f_pos);
		len += ptr - (char *)buf;
	}

	LEAVE();
	return len;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 1, 0)
/**
 *  @brief This function create dump directory
 *
 *  @param phandle   A pointer to moal_handle
 *  @param dir_buf   A pointer to dir_buf buffer
 *  @param buf_size  Size of dir_buf buffer
 *
 *  @return         N/A
 */
void
woal_create_dump_dir(moal_handle *phandle, char *dir_buf, int buf_size)
{
	struct dentry *dentry;
	struct path path;
	t_u32 sec, usec;
	int ret;

	ENTER();

	if (!phandle || !dir_buf) {
		PRINTM(MERROR, "Can't create directory\n");
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	moal_get_system_time(phandle, &sec, &usec);
	memset(dir_buf, 0, buf_size);
	sprintf(dir_buf, "%s%u", "/data/dump_", sec);

	dentry = kern_path_create(AT_FDCWD, dir_buf, &path, 1);
	if (IS_ERR(dentry)) {
		PRINTM(MMSG,
		       "Create directory %s error, try create dir in /var",
		       dir_buf);
		memset(dir_buf, 0, buf_size);
		sprintf(dir_buf, "%s%u", "/var/dump_", sec);
		dentry = kern_path_create(AT_FDCWD, dir_buf, &path, 1);
	}
	if (IS_ERR(dentry)) {
		PRINTM(MMSG, "Create directory %s error, use default folder",
		       dir_buf);
		goto default_dir;
	}
	ret = vfs_mkdir(path.dentry->d_inode, dentry, 0777);
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,7,0)
	mutex_unlock(&path.dentry->d_inode->i_mutex);
#else
	inode_unlock(path.dentry->d_inode);
#endif

	if (ret < 0) {
		PRINTM(MMSG, "Create directory failure, use default folder\n");
		goto default_dir;
	} else {
		PRINTM(MMSG, "Create directory %s successfully\n", dir_buf);
		goto done;
	}

default_dir:
	memset(dir_buf, 0, buf_size);
	sprintf(dir_buf, "%s", "/data");
done:
	LEAVE();
}
#endif

/**
 *  @brief This function save dump buf to file
 *
 *  @param dir_name  A pointer to directory name
 *  @param file_name A pointer to file name
 *  @param buf       A pointer to dump data
 *  @param buf_len   The length of dump buf
 *
 *  @return         SUCCESS OR FAILURE
 */
mlan_status
woal_save_dump_info_to_file(char *dir_name, char *file_name, t_u8 *buf,
			    t_u32 buf_len)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	struct file *pfile = NULL;
	t_u8 name[64];
	mm_segment_t fs;
	loff_t pos;

	ENTER();

	if (!dir_name || !file_name || !buf) {
		PRINTM(MERROR, "Can't save dump info to file\n");
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	memset(name, 0, sizeof(name));
	sprintf(name, "%s/%s", dir_name, file_name);
	pfile = filp_open(name, O_CREAT | O_RDWR, 0644);
	if (IS_ERR(pfile)) {
		PRINTM(MMSG,
		       "Create file %s error, try to save dump file in /var\n",
		       name);
		memset(name, 0, sizeof(name));
		sprintf(name, "%s/%s", "/var", file_name);
		pfile = filp_open(name, O_CREAT | O_RDWR, 0644);
	}
	if (IS_ERR(pfile)) {
		PRINTM(MERROR, "Create Dump file for %s error\n", name);
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	PRINTM(MMSG, "Dump data %s saved in %s\n", file_name, name);

	fs = get_fs();
	set_fs(KERNEL_DS);
	pos = 0;
	vfs_write(pfile, buf, buf_len, &pos);
	filp_close(pfile, NULL);
	set_fs(fs);

	PRINTM(MMSG, "Dump data %s saved in %s successfully\n", file_name,
	       name);

done:
	LEAVE();
	return ret;
}

/**
 *  @brief This function dump drv info to file
 *
 *  @param phandle   A pointer to moal_handle
 *  @param dir_name   A pointer to directory name
 *
 *  @return         N/A
 */
void
woal_dump_drv_info(moal_handle *phandle, t_u8 *dir_name)
{
	int ret = 0;
	struct file *pfile = NULL;
	mm_segment_t fs;
	t_u8 *drv_buf;
	t_u8 file_name[64];
	t_u32 len = 0;
	t_u32 total_len = 0;

	ENTER();

	PRINTM(MMSG, "=== START DRIVER INFO DUMP===");
	ret = moal_vmalloc(phandle, DRV_INFO_SIZE + 1, &drv_buf);
	if ((ret != MLAN_STATUS_SUCCESS) || !drv_buf) {
		PRINTM(MERROR, "Error: vmalloc drv buffer failed!\n");
		goto done;
	}
	memset(file_name, 0, sizeof(file_name));
	sprintf(file_name, "%s/%s", dir_name, "file_drv_info");
	pfile = filp_open(file_name, O_CREAT | O_RDWR, 0644);
	if (IS_ERR(pfile)) {
		PRINTM(MMSG,
		       "Create file %s error, try create /var/file_drv_info",
		       file_name);
		pfile = filp_open("/var/file_drv_info", O_CREAT | O_RDWR, 0644);
	} else {
		PRINTM(MMSG, "DRV dump data in %s\n", file_name);
	}
	if (IS_ERR(pfile)) {
		PRINTM(MMSG, "Create file_drv_info file failed\n");
		goto done;
	}
	fs = get_fs();
	set_fs(KERNEL_DS);

	len = woal_dump_moal_drv_info(phandle, drv_buf);
	total_len += len;
	vfs_write(pfile, drv_buf, len, &pfile->f_pos);

	len = woal_dump_mlan_drv_info(woal_get_priv(phandle, MLAN_BSS_ROLE_ANY),
				      drv_buf);
	total_len += len;
	vfs_write(pfile, drv_buf, len, &pfile->f_pos);

	len = woal_dump_moal_hex(phandle, drv_buf);
	total_len += len;
	vfs_write(pfile, drv_buf, len, &pfile->f_pos);

	len = woal_dump_mlan_hex(woal_get_priv(phandle, MLAN_BSS_ROLE_ANY),
				 drv_buf, pfile);
	total_len += len;

	PRINTM(MMSG, "Drv info total bytes = %ld (0x%lx)\n",
	       (long int)total_len, (long unsigned int)total_len);

	filp_close(pfile, NULL);
	set_fs(fs);

	PRINTM(MMSG, "=== DRIVER INFO DUMP END===");
done:
	if (drv_buf)
		moal_vfree(phandle, drv_buf);
	LEAVE();
}

#define DEBUG_HOST_READY		0xCC
#define DEBUG_FW_DONE			0xFF
#define DEBUG_MEMDUMP_FINISH            0xFE
#define MAX_POLL_TRIES			100

#define DEBUG_DUMP_CTRL_REG               0xF9
#define DEBUG_DUMP_START_REG              0xF1
#define DEBUG_DUMP_END_REG                0xF8

typedef enum {
	DUMP_TYPE_ITCM = 0,
	DUMP_TYPE_DTCM = 1,
	DUMP_TYPE_SQRAM = 2,
	DUMP_TYPE_APU_REGS = 3,
	DUMP_TYPE_CIU_REGS = 4,
	DUMP_TYPE_ICU_REGS = 5,
	DUMP_TYPE_MAC_REGS = 6,
	DUMP_TYPE_EXTEND_7 = 7,
	DUMP_TYPE_EXTEND_8 = 8,
	DUMP_TYPE_EXTEND_9 = 9,
	DUMP_TYPE_EXTEND_10 = 10,
	DUMP_TYPE_EXTEND_11 = 11,
	DUMP_TYPE_EXTEND_12 = 12,
	DUMP_TYPE_EXTEND_13 = 13,
	DUMP_TYPE_EXTEND_LAST = 14
} dumped_mem_type;

#define MAX_NAME_LEN               8
#define MAX_FULL_NAME_LEN               32
t_u8 *name_prefix = "/data/file_";

typedef struct {
	t_u8 mem_name[MAX_NAME_LEN];
	t_u8 *mem_Ptr;
	struct file *pfile_mem;
	t_u8 done_flag;
} memory_type_mapping;

memory_type_mapping mem_type_mapping_tbl = { "DUMP", NULL, NULL, 0xDD };

typedef enum {
	RDWR_STATUS_SUCCESS = 0,
	RDWR_STATUS_FAILURE = 1,
	RDWR_STATUS_DONE = 2
} rdwr_status;

/**
 *  @brief This function read/write firmware via cmd52
 *
 *  @param phandle   A pointer to moal_handle
 *  @param doneflag  A flag
 *
 *  @return         MLAN_STATUS_SUCCESS
 */
rdwr_status
woal_cmd52_rdwr_firmware(moal_handle *phandle, t_u8 doneflag)
{
	int ret = 0;
	int tries = 0;
	t_u8 ctrl_data = 0;
	t_u8 dbg_dump_ctrl_reg = 0;
	t_u8 debug_host_ready = 0;

	dbg_dump_ctrl_reg = DEBUG_DUMP_CTRL_REG;
	debug_host_ready = DEBUG_HOST_READY;
	sdio_writeb(((struct sdio_mmc_card *)phandle->card)->func,
		    debug_host_ready, dbg_dump_ctrl_reg, &ret);
	if (ret) {
		PRINTM(MERROR, "SDIO Write ERR\n");
		return RDWR_STATUS_FAILURE;
	}
	for (tries = 0; tries < MAX_POLL_TRIES; tries++) {
		ctrl_data =
			sdio_readb(((struct sdio_mmc_card *)phandle->card)->
				   func, dbg_dump_ctrl_reg, &ret);
		if (ret) {
			PRINTM(MERROR, "SDIO READ ERR\n");
			return RDWR_STATUS_FAILURE;
		}
		if (ctrl_data == DEBUG_FW_DONE)
			break;
		if (doneflag && ctrl_data == doneflag)
			return RDWR_STATUS_DONE;
		if (ctrl_data != debug_host_ready) {
			PRINTM(MMSG,
			       "The ctrl reg was changed, re-try again!\n");
			sdio_writeb(((struct sdio_mmc_card *)phandle->card)->
				    func, debug_host_ready, dbg_dump_ctrl_reg,
				    &ret);
			if (ret) {
				PRINTM(MERROR, "SDIO Write ERR\n");
				return RDWR_STATUS_FAILURE;
			}
		}
		udelay(100);
	}
	if (ctrl_data == debug_host_ready) {
		PRINTM(MERROR, "Fail to pull ctrl_data\n");
		return RDWR_STATUS_FAILURE;
	}

	return RDWR_STATUS_SUCCESS;
}

/**
 *  @brief This function dump firmware memory to file
 *
 *  @param phandle   A pointer to moal_handle
 *
 *  @return         N/A
 */
void
woal_dump_firmware_info_v3(moal_handle *phandle)
{

	int ret = 0;
	int tries = 0;
	unsigned int reg, reg_start, reg_end;
	t_u8 *dbg_ptr = NULL;
	t_u8 *temp_Ptr = NULL;
	t_u32 sec, usec;
	t_u8 start_flag = 0;
	t_u8 doneflag = 0;
	rdwr_status stat;
	t_u32 memory_size = 0;
	t_u8 path_name[64], file_name[32];
	t_u8 *end_ptr = NULL;
	t_u8 dbg_dump_start_reg = 0;
	t_u8 dbg_dump_end_reg = 0;
	t_u8 dbg_dump_ctrl_reg = 0;
	memory_type_mapping *pmem_type_mapping_tbl = &mem_type_mapping_tbl;

	if (!phandle) {
		PRINTM(MERROR, "Could not dump firmwware info\n");
		return;
	}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 1, 0)
    /** Create dump directort*/
	woal_create_dump_dir(phandle, path_name, sizeof(path_name));
#else
	memset(path_name, 0, sizeof(path_name));
	strcpy(path_name, "/data");
#endif
	PRINTM(MMSG, "Directory name is %s\n", path_name);

	woal_dump_drv_info(phandle, path_name);

	dbg_dump_start_reg = DEBUG_DUMP_START_REG;
	dbg_dump_end_reg = DEBUG_DUMP_END_REG;
	dbg_dump_ctrl_reg = DEBUG_DUMP_CTRL_REG;

	mlan_pm_wakeup_card(phandle->pmlan_adapter);
	phandle->fw_dump = MTRUE;
	sdio_claim_host(((struct sdio_mmc_card *)phandle->card)->func);
	/* start dump fw memory */
	moal_get_system_time(phandle, &sec, &usec);
	PRINTM(MMSG, "==== DEBUG MODE OUTPUT START: %u.%06u ====\n", sec, usec);
	/* read the number of the memories which will dump */
	if (RDWR_STATUS_FAILURE == woal_cmd52_rdwr_firmware(phandle, doneflag))
		goto done;

    /** check the reg which indicate dump starting */
	for (reg = dbg_dump_start_reg; reg <= dbg_dump_end_reg; reg++) {
		for (tries = 0; tries < MAX_POLL_TRIES; tries++) {
			start_flag =
				sdio_readb(((struct sdio_mmc_card *)phandle->
					    card)->func, reg, &ret);
			if (ret) {
				PRINTM(MMSG, "SDIO READ ERR\n");
				goto done;
			}
	     /** 0 means dump starting*/
			if (start_flag == 0)
				break;
			if (tries == MAX_POLL_TRIES) {
				PRINTM(MMSG, "FW not ready to dump\n");
				goto done;
			}
			udelay(100);
		}
	}

	memory_size = 0xF0000;
	PRINTM(MMSG, "%s_SIZE=0x%x\n", pmem_type_mapping_tbl->mem_name,
	       memory_size);
	ret = moal_vmalloc(phandle, memory_size + 1,
			   (t_u8 **)&pmem_type_mapping_tbl->mem_Ptr);
	if ((ret != MLAN_STATUS_SUCCESS) || !pmem_type_mapping_tbl->mem_Ptr) {
		PRINTM(MERROR, "Error: vmalloc %s buffer failed!!!\n",
		       pmem_type_mapping_tbl->mem_name);
		goto done;
	}
	dbg_ptr = pmem_type_mapping_tbl->mem_Ptr;
	end_ptr = dbg_ptr + memory_size;

	doneflag = pmem_type_mapping_tbl->done_flag;
	moal_get_system_time(phandle, &sec, &usec);
	PRINTM(MMSG, "Start %s output %u.%06u, please wait...\n",
	       pmem_type_mapping_tbl->mem_name, sec, usec);
	do {
		stat = woal_cmd52_rdwr_firmware(phandle, doneflag);
		if (RDWR_STATUS_FAILURE == stat)
			goto done;

		reg_start = dbg_dump_start_reg;
		reg_end = dbg_dump_end_reg;
		for (reg = reg_start; reg <= reg_end; reg++) {
			*dbg_ptr =
				sdio_readb(((struct sdio_mmc_card *)phandle->
					    card)->func, reg, &ret);
			if (ret) {
				PRINTM(MMSG, "SDIO READ ERR\n");
				goto done;
			}
			dbg_ptr++;
			if (dbg_ptr >= end_ptr) {
				PRINTM(MMSG,
				       "pre-allocced buf is not enough\n");
				ret = moal_vmalloc(phandle,
						   memory_size + 0x4000 + 1,
						   (t_u8 **)&temp_Ptr);
				if ((ret != MLAN_STATUS_SUCCESS) || !temp_Ptr) {
					PRINTM(MERROR,
					       "Error: vmalloc  buffer failed!!!\n");
					goto done;
				}
				moal_memcpy(phandle, temp_Ptr,
					    pmem_type_mapping_tbl->mem_Ptr,
					    memory_size);
				moal_vfree(phandle,
					   pmem_type_mapping_tbl->mem_Ptr);
				pmem_type_mapping_tbl->mem_Ptr = temp_Ptr;
				temp_Ptr = NULL;
				dbg_ptr =
					pmem_type_mapping_tbl->mem_Ptr +
					memory_size;
				memory_size += 0x4000;
				end_ptr =
					pmem_type_mapping_tbl->mem_Ptr +
					memory_size;
			}

		}
		if (RDWR_STATUS_DONE == stat) {
			PRINTM(MMSG, "%s done:"
#ifdef MLAN_64BIT
			       "size = 0x%lx\n",
#else
			       "size = 0x%x\n",
#endif
			       pmem_type_mapping_tbl->mem_name,
			       dbg_ptr - pmem_type_mapping_tbl->mem_Ptr);

			memset(file_name, 0, sizeof(file_name));
			sprintf(file_name, "%s%s", "file_sdio_",
				pmem_type_mapping_tbl->mem_name);
			if (MLAN_STATUS_SUCCESS !=
			    woal_save_dump_info_to_file(path_name, file_name,
							pmem_type_mapping_tbl->
							mem_Ptr,
							dbg_ptr -
							pmem_type_mapping_tbl->
							mem_Ptr))
				PRINTM(MMSG, "Can't save dump file %s in %s\n",
				       file_name, path_name);
			moal_vfree(phandle, pmem_type_mapping_tbl->mem_Ptr);
			pmem_type_mapping_tbl->mem_Ptr = NULL;
			break;
		}
	} while (1);

	moal_get_system_time(phandle, &sec, &usec);
	PRINTM(MMSG, "==== DEBUG MODE OUTPUT END: %u.%06u ====\n", sec, usec);
	/* end dump fw memory */
done:
	phandle->fw_dump = MFALSE;
	sdio_release_host(((struct sdio_mmc_card *)phandle->card)->func);
	if (pmem_type_mapping_tbl->mem_Ptr) {
		moal_vfree(phandle, pmem_type_mapping_tbl->mem_Ptr);
		pmem_type_mapping_tbl->mem_Ptr = NULL;
	}
	PRINTM(MMSG, "==== DEBUG MODE END ====\n");
	return;
}

/**
 *  @brief This function reads and displays SDIO registers for debugging
 *
 *  @param phandle  A pointer to moal_handle
 *
 *  @return         N/A
 */
void
woal_sdio_reg_dbg(moal_handle *phandle)
{
	int ret = 0;
	t_u8 loop, index = 0, func, data;
	unsigned int reg, reg_start, reg_end;
	unsigned int scratch_reg = SDIO_SCRATCH_REG;
	unsigned int reg_table[] = { 0x08, 0x58, 0x5C, 0x5D, 0x60, 0x61, 0x62,
		0x64, 0x65, 0x66, 0x68, 0x69, 0x6a
	};
	char buf[256], *ptr;

	mlan_pm_wakeup_card(phandle->pmlan_adapter);

	sdio_claim_host(((struct sdio_mmc_card *)phandle->card)->func);
	for (loop = 0; loop < 5; loop++) {
		memset(buf, 0, sizeof(buf));
		ptr = buf;
		if (loop == 0) {
			/* Read the registers of SDIO function0 */
			func = loop;
			reg_start = 0;
			reg_end = 9;

		} else if (loop == 1) {
			/* Read the registers of SDIO function1 */
			func = loop;
			reg_start = 0x10;
			reg_end = 0x17;
		} else if (loop == 2) {
			/* Read specific registers of SDIO function1 */
			index = 0;
			func = 1;
			reg_start = reg_table[index++];
			reg_end = reg_table[ARRAY_SIZE(reg_table) - 1];
		} else {
			/* Read the scratch registers of SDIO function1 */
			if (loop == 4)
				mdelay(100);
			func = 1;
			reg_start = scratch_reg;
			reg_end = scratch_reg + 10;
		}
		if (loop != 2)
			ptr += sprintf(ptr, "SDIO Func%d (%#x-%#x): ", func,
				       reg_start, reg_end);
		else
			ptr += sprintf(ptr, "SDIO Func%d: ", func);
		for (reg = reg_start; reg <= reg_end;) {
			if (func == 0)
				data = sdio_f0_readb(((struct sdio_mmc_card *)
						      phandle->card)->func, reg,
						     &ret);
			else
				data = sdio_readb(((struct sdio_mmc_card *)
						   phandle->card)->func, reg,
						  &ret);
			if (loop == 2)
				ptr += sprintf(ptr, "(%#x) ", reg);
			if (!ret)
				ptr += sprintf(ptr, "%02x ", data);
			else {
				ptr += sprintf(ptr, "ERR");
				break;
			}
			if (loop == 2 && reg < reg_end)
				reg = reg_table[index++];
			else
				reg++;
		}
		PRINTM(MMSG, "%s\n", buf);
	}
	sdio_release_host(((struct sdio_mmc_card *)phandle->card)->func);
}

/**
 *  @brief This function displays extra MOAL debug information
 *
 *  @param priv     A pointer to moal_private
 *  @param handle   A pointer to moal_handle
 *  @param flag     Indicates whether register read can be done directly
 *
 *  @return         N/A
 */
void
woal_moal_debug_info(moal_private *priv, moal_handle *handle, u8 flag)
{
	moal_handle *phandle = NULL;
	char buf[MLAN_MAX_VER_STR_LEN];
	int i = 0;

	ENTER();

	if (!priv) {
		if (handle) {
			phandle = handle;
		} else {
			PRINTM(MERROR,
			       "Could not retrieve debug information from MOAL\n");
			LEAVE();
			return;
		}
	} else {
		phandle = priv->phandle;
	}

	woal_get_version(phandle, buf, sizeof(buf) - 1);
	PRINTM(MERROR, "Driver version = %s\n", buf);
	PRINTM(MERROR, "main_state = %d\n", phandle->main_state);
	PRINTM(MERROR, "ioctl_pending = %d\n",
	       atomic_read(&phandle->ioctl_pending));
	PRINTM(MERROR, "tx_pending = %d\n", atomic_read(&phandle->tx_pending));
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 29)
	if (priv) {
		PRINTM(MERROR, "wmm_tx_pending[0] = %d\n",
		       atomic_read(&priv->wmm_tx_pending[0]));
		PRINTM(MERROR, "wmm_tx_pending[1] = %d\n",
		       atomic_read(&priv->wmm_tx_pending[1]));
		PRINTM(MERROR, "wmm_tx_pending[2] = %d\n",
		       atomic_read(&priv->wmm_tx_pending[2]));
		PRINTM(MERROR, "wmm_tx_pending[3] = %d\n",
		       atomic_read(&priv->wmm_tx_pending[3]));
	}
#endif
	PRINTM(MERROR, "rx_pending = %d\n", atomic_read(&phandle->rx_pending));
	PRINTM(MERROR, "lock_count = %d\n", atomic_read(&phandle->lock_count));
	PRINTM(MERROR, "malloc_count = %d\n",
	       atomic_read(&phandle->malloc_count));
	PRINTM(MERROR, "mbufalloc_count = %d\n",
	       atomic_read(&phandle->mbufalloc_count));
#if defined(SDIO_SUSPEND_RESUME)
	PRINTM(MERROR, "hs_skip_count = %u\n", phandle->hs_skip_count);
	PRINTM(MERROR, "hs_force_count = %u\n", phandle->hs_force_count);
#endif

	if (priv) {
		PRINTM(MERROR, "Media state = \"%s\"\n",
		       ((priv->media_connected ==
			 MFALSE) ? "Disconnected" : "Connected"));
		PRINTM(MERROR, "carrier %s\n",
		       ((netif_carrier_ok(priv->netdev)) ? "on" : "off"));
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 29)
		for (i = 0; i < (priv->netdev->num_tx_queues); i++) {
			PRINTM(MERROR, "tx queue %d: %s\n", i,
			       ((netif_tx_queue_stopped
				 (netdev_get_tx_queue(priv->netdev, i))) ?
				"stopped" : "started"));
		}
#else
		PRINTM(MERROR, "tx queue %s\n",
		       ((netif_queue_stopped(priv->netdev)) ? "stopped" :
			"started"));
#endif
	}

	for (i = 0; i < phandle->priv_num; i++) {
		priv = phandle->priv[i];
		if (priv)
			PRINTM(MERROR, "%s: num_tx_timeout = %d\n",
			       priv->netdev->name, priv->num_tx_timeout);
	}

#if defined(SDIO_SUSPEND_RESUME)
	if (phandle->is_suspended == MTRUE) {
		LEAVE();
		return;
	}
#endif

	/* Display SDIO registers */
	if (flag &&
	    ((phandle->main_state == MOAL_END_MAIN_PROCESS) ||
	     (phandle->main_state == MOAL_STATE_IDLE))) {
		woal_sdio_reg_dbg(phandle);
	} else {
		phandle->sdio_reg_dbg = MTRUE;
		queue_work(phandle->workqueue, &phandle->main_work);
	}

	LEAVE();
	return;
}

/**
 *    @brief Download power table to firmware for a specific country
 *
 *    @param priv         A pointer to moal_private
 *    @param country      ISO 3166-1 alpha-2 country code
 *
 *    @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
woal_request_country_power_table(moal_private *priv, char *country)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	moal_handle *handle = NULL;
	char country_name[] = "txpower_XX.bin";
	char file_path[256];
	char *last_slash = NULL;

	ENTER();

	if (!priv || !priv->phandle) {
		PRINTM(MERROR, "Priv or handle is null\n");
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	if (!country) {
		PRINTM(MERROR, "Country is null\n");
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	handle = priv->phandle;

	/* Replace XX with ISO 3166-1 alpha-2 country code */
	strncpy(strstr(country_name, "XX"), country, strlen(country));

	memset(file_path, 0, sizeof(file_path));
	/* file_path should be Null terminated */
	if (fw_name && (strlen(fw_name) < sizeof(file_path))) {
		strncpy(file_path, fw_name,
			MIN((sizeof(file_path) - 1), strlen(fw_name)));
		last_slash = strrchr(file_path, '/');
		if (last_slash)
			memset(last_slash + 1, 0,
			       sizeof(file_path) - 1 - (last_slash -
							file_path));
		else
			memset(file_path, 0, sizeof(file_path));
	} else {
		strncpy(file_path, "mrvl/", sizeof(file_path));
	}
	strncpy(file_path + strlen(file_path), country_name,
		strlen(country_name));
	country_txpwrlimit = file_path;

	if (MLAN_STATUS_SUCCESS !=
	    woal_set_user_init_data(handle, COUNTRY_POWER_TABLE,
				    MOAL_IOCTL_WAIT)) {
		PRINTM(MFATAL, "Download power table to firmware failed\n");
		ret = MLAN_STATUS_FAILURE;
	}
	LEAVE();
	return ret;
}

/**
 *  @brief napi polling call back function.
 *
 *  @param napi     A pointer to napi_struct
 *  @param budget   the limit of packets driver should poll
 *
 *  @return       packets received
 */
int
woal_netdev_poll_rx(struct napi_struct *napi, int budget)
{
	moal_handle *handle = container_of(napi, moal_handle, napi_rx);
	t_u8 recv = budget;

	ENTER();
	if (handle->surprise_removed == MTRUE) {
		napi_complete(napi);
		LEAVE();
		return 0;
	}
	mlan_rx_process(handle->pmlan_adapter, &recv);
	if (recv < budget)
		napi_complete(napi);
	LEAVE();
	return recv;
}

/**
 *  @brief This workqueue function handles rx_process
 *
 *  @param work    A pointer to work_struct
 *
 *  @return        N/A
 */
t_void
woal_rx_work_queue(struct work_struct *work)
{
	moal_handle *handle = container_of(work, moal_handle, rx_work);
	ENTER();
	if (handle->surprise_removed == MTRUE) {
		LEAVE();
		return;
	}
	mlan_rx_process(handle->pmlan_adapter, NULL);
	LEAVE();
}

/**
 *  @brief This workqueue function handles main_process
 *
 *  @param work    A pointer to work_struct
 *
 *  @return        N/A
 */
t_void
woal_main_work_queue(struct work_struct *work)
{
	moal_handle *handle = container_of(work, moal_handle, main_work);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 36)
	struct sched_param sp = {.sched_priority = wq_sched_prio };
#endif

	ENTER();

	if (handle->surprise_removed == MTRUE) {
		LEAVE();
		return;
	}

	if (handle->sdio_reg_dbg == MTRUE) {
		handle->sdio_reg_dbg = MFALSE;
		woal_sdio_reg_dbg(handle);
#if defined(DEBUG_LEVEL1)
		if (drvdbg & MFW_D) {
			drvdbg &= ~MFW_D;
			woal_dump_firmware_info_v3(handle);
		}
#endif
		LEAVE();
		return;
	}
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 36)
	/* Change the priority and scheduling policy of main work queue */
	if ((wq_sched_prio != current->rt_priority) ||
	    (wq_sched_policy != current->policy)) {
		PRINTM(MMSG,
		       "Set work queue priority %d and scheduling policy %d\n",
		       wq_sched_prio, wq_sched_policy);
		sched_setscheduler(current, wq_sched_policy, &sp);
	}
#endif

	handle->main_state = MOAL_ENTER_WORK_QUEUE;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 32)
	sdio_claim_host(((struct sdio_mmc_card *)handle->card)->func);
#endif
	handle->main_state = MOAL_START_MAIN_PROCESS;
	/* Call MLAN main process */
	mlan_main_process(handle->pmlan_adapter);
	handle->main_state = MOAL_END_MAIN_PROCESS;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 32)
	sdio_release_host(((struct sdio_mmc_card *)handle->card)->func);
#endif

	LEAVE();
}

/**
 * @brief Handles interrupt
 *
 */
/**
 * @param handle  A pointer to moal_handle struct
 *
 * @return        MLAN_STATUS_FAILURE-- when the interrupt is not for us.
 */
mlan_status
woal_interrupt(moal_handle *handle)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	ENTER();
	handle->main_state = MOAL_RECV_INT;
	PRINTM(MINTR, "*\n");
	if (handle->surprise_removed == MTRUE) {
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}
	/* call mlan_interrupt to read int status */
	mlan_interrupt(handle->pmlan_adapter);
#ifdef SDIO_SUSPEND_RESUME
	if (handle->is_suspended) {
		PRINTM(MINTR, "Receive interrupt in hs_suspended\n");
		LEAVE();
		return ret;
	}
#endif
	handle->main_state = MOAL_START_MAIN_PROCESS;
	/* Call MLAN main process */
	mlan_main_process(handle->pmlan_adapter);
	handle->main_state = MOAL_END_MAIN_PROCESS;
	LEAVE();
	return ret;
}

/**
 * @brief This function adds the card. it will probe the
 *      card, allocate the mlan_private and initialize the device.
 *
 *  @param card    A pointer to card
 *
 *  @return        A pointer to moal_handle structure
 */
moal_handle *
woal_add_card(void *card)
{
	moal_handle *handle = NULL;
	mlan_status status = MLAN_STATUS_SUCCESS;
	int netlink_num = NETLINK_MARVELL;
	int index = 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)
	struct netlink_kernel_cfg cfg = {
		.groups = NL_MULTICAST_GROUP,
	};
#endif

	ENTER();

	if (MOAL_ACQ_SEMAPHORE_BLOCK(&AddRemoveCardSem))
		goto exit_sem_err;

	/* Allocate buffer for moal_handle */
	handle = kzalloc(sizeof(moal_handle), GFP_KERNEL);
	if (!handle) {
		PRINTM(MERROR, "Allocate buffer for moal_handle failed!\n");
		goto err_handle;
	}

	/* Init moal_handle */
	handle->card = card;
	/* Save the handle */
	for (index = 0; index < MAX_MLAN_ADAPTER; index++) {
		if (m_handle[index] == NULL)
			break;
	}
	if (index < MAX_MLAN_ADAPTER) {
		m_handle[index] = handle;
		handle->handle_idx = index;
	} else {
		PRINTM(MERROR, "Exceeded maximum cards supported!\n");
		goto err_kmalloc;
	}

	if (mac_addr
#ifdef MFG_CMD_SUPPORT
	    && mfg_mode != MLAN_INIT_PARA_ENABLED
#endif
		) {
		t_u8 temp[20];
		t_u8 len = strlen(mac_addr) + 1;
		if (len < sizeof(temp)) {
			memcpy(temp, mac_addr, len);
			handle->set_mac_addr = 1;
			/* note: the following function overwrites the temp buffer */
			woal_mac2u8(handle->mac_addr, temp);
		}
	}

	handle->histogram_table_num = 1;

	((struct sdio_mmc_card *)card)->handle = handle;
#ifdef SPI_SUPPORT
	((struct woal_spi_card *)card)->handle = handle;
#endif

#ifdef STA_SUPPORT
	handle->scan_pending_on_block = MFALSE;
	MOAL_INIT_SEMAPHORE(&handle->async_sem);
#endif

	/* Init SW */
	if (MLAN_STATUS_SUCCESS != woal_init_sw(handle)) {
		PRINTM(MFATAL, "Software Init Failed\n");
		goto err_kmalloc;
	}

	do {
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 14)
		handle->nl_sk = netlink_kernel_create(netlink_num, NULL);
#else
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 22)
		handle->nl_sk =
			netlink_kernel_create(netlink_num, NL_MULTICAST_GROUP,
					      NULL, THIS_MODULE);
#else
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 24)
		handle->nl_sk =
			netlink_kernel_create(netlink_num, NL_MULTICAST_GROUP,
					      NULL, NULL, THIS_MODULE);
#else
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
		handle->nl_sk =
			netlink_kernel_create(&init_net, netlink_num,
					      NL_MULTICAST_GROUP, NULL, NULL,
					      THIS_MODULE);
#else
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 7, 0)
		handle->nl_sk =
			netlink_kernel_create(&init_net, netlink_num,
					      THIS_MODULE, &cfg);
#else
		handle->nl_sk =
			netlink_kernel_create(&init_net, netlink_num, &cfg);
#endif
#endif
#endif
#endif
#endif
		if (handle->nl_sk) {
			PRINTM(MINFO, "Netlink number = %d\n", netlink_num);
			handle->netlink_num = netlink_num;
			break;
		}
		netlink_num--;
	} while (netlink_num > 0);

	if (handle->nl_sk == NULL) {
		PRINTM(MERROR,
		       "Could not initialize netlink event passing mechanism!\n");
		goto err_kmalloc;
	}

	/* Create workqueue for main process */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 14)
	/* For kernel less than 2.6.14 name can not be
	 * greater than 10 characters */
	handle->workqueue = create_workqueue("MOAL_WORKQ");
#else
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)
	handle->workqueue =
		alloc_workqueue("MOAL_WORK_QUEUE",
				WQ_HIGHPRI | WQ_MEM_RECLAIM | WQ_UNBOUND, 1);
#else
	handle->workqueue = create_workqueue("MOAL_WORK_QUEUE");
#endif
#endif
	if (!handle->workqueue)
		goto err_kmalloc;

	MLAN_INIT_WORK(&handle->main_work, woal_main_work_queue);

	if (!napi) {
		/* Create workqueue for rx process */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 14)
		/* For kernel less than 2.6.14 name can not be
		 * greater than 10 characters */
		handle->rx_workqueue = create_workqueue("MOAL_RX_WORKQ");
#else
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)
		handle->rx_workqueue =
			alloc_workqueue("MOAL_RX_WORK_QUEUE",
					WQ_HIGHPRI | WQ_MEM_RECLAIM |
					WQ_UNBOUND, 1);
#else
		handle->rx_workqueue = create_workqueue("MOAL_RX_WORK_QUEUE");
#endif
#endif
		if (!handle->rx_workqueue) {
			woal_terminate_workqueue(handle);
			goto err_kmalloc;
		}
		MLAN_INIT_WORK(&handle->rx_work, woal_rx_work_queue);
	}
#define NAPI_BUDGET     64
	if (napi) {
		init_dummy_netdev(&handle->napi_dev);
		netif_napi_add(&handle->napi_dev, &handle->napi_rx,
			       woal_netdev_poll_rx, NAPI_BUDGET);
		napi_enable(&handle->napi_rx);
	}

#ifdef REASSOCIATION
	PRINTM(MINFO, "Starting re-association thread...\n");
	handle->reassoc_thread.handle = handle;
	woal_create_thread(woal_reassociation_thread,
			   &handle->reassoc_thread, "woal_reassoc_service");

	while (!handle->reassoc_thread.pid)
		woal_sched_timeout(2);
#endif /* REASSOCIATION */

	/* Register the device. Fill up the private data structure with
	 * relevant information from the card and request for the required
	 * IRQ.
	 */
	if (woal_register_dev(handle) != MLAN_STATUS_SUCCESS) {
		PRINTM(MFATAL, "Failed to register wlan device!\n");
		goto err_registerdev;
	}
#ifdef ANDROID_KERNEL
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0)
	wakeup_source_init(&handle->ws, "mwlan");
#else
	wake_lock_init(&handle->wake_lock, WAKE_LOCK_SUSPEND, "mwlan");
#endif
#endif

	/* Init FW and HW */
	if (MLAN_STATUS_SUCCESS != woal_init_fw(handle)) {
		PRINTM(MFATAL, "Firmware Init Failed\n");
		goto err_init_fw;
	}

	LEAVE();
	return handle;

err_init_fw:
	if ((handle->hardware_status == HardwareStatusFwReady) ||
	    (handle->hardware_status == HardwareStatusReady)) {
		PRINTM(MINFO, "shutdown mlan\n");
		handle->init_wait_q_woken = MFALSE;
		status = mlan_shutdown_fw(handle->pmlan_adapter);
		if (status == MLAN_STATUS_PENDING)
			wait_event_interruptible(handle->init_wait_q,
						 handle->init_wait_q_woken);
	}
#ifdef ANDROID_KERNEL
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0)
	wakeup_source_trash(&handle->ws);
#else
	wake_lock_destroy(&handle->wake_lock);
#endif
#endif
	/* Unregister device */
	PRINTM(MINFO, "unregister device\n");
	woal_unregister_dev(handle);
err_registerdev:
	handle->surprise_removed = MTRUE;
#ifdef REASSOCIATION
	if (handle->reassoc_thread.pid)
		wake_up_interruptible(&handle->reassoc_thread.wait_q);
	/* waiting for main thread quit */
	while (handle->reassoc_thread.pid)
		woal_sched_timeout(2);
#endif /* REASSOCIATION */
	if (napi)
		netif_napi_del(&handle->napi_rx);
	woal_terminate_workqueue(handle);
err_kmalloc:
	woal_free_moal_handle(handle);
	if (index < MAX_MLAN_ADAPTER)
		m_handle[index] = NULL;
	((struct sdio_mmc_card *)card)->handle = NULL;
err_handle:
	MOAL_REL_SEMAPHORE(&AddRemoveCardSem);
exit_sem_err:
	LEAVE();
	return NULL;
}

/**
 *  @brief This function removes the card.
 *
 *  @param card    A pointer to card
 *
 *  @return        MLAN_STATUS_SUCCESS
 */
mlan_status
woal_remove_card(void *card)
{
	moal_handle *handle = NULL;
	moal_private *priv = NULL;
	mlan_status status;
	int i;
	int index = 0;

	ENTER();

	if (MOAL_ACQ_SEMAPHORE_BLOCK(&AddRemoveCardSem))
		goto exit_sem_err;
	/* Find the correct handle */
	for (index = 0; index < MAX_MLAN_ADAPTER; index++) {
		if (m_handle[index] && (m_handle[index]->card == card)) {
			handle = m_handle[index];
			break;
		}
	}
	if (!handle)
		goto exit_remove;
	handle->surprise_removed = MTRUE;

	flush_workqueue(handle->workqueue);
	if (handle->rx_workqueue)
		flush_workqueue(handle->rx_workqueue);

	if (napi) {
		napi_disable(&handle->napi_rx);
		netif_napi_del(&handle->napi_rx);
	}

	/* Stop data */
	for (i = 0; i < MIN(handle->priv_num, MLAN_MAX_BSS_NUM); i++) {
		priv = handle->priv[i];
		if (priv) {
			woal_stop_queue(priv->netdev);
			if (netif_carrier_ok(priv->netdev))
				netif_carrier_off(priv->netdev);
		}
	}
	if ((handle->hardware_status == HardwareStatusFwReady) ||
	    (handle->hardware_status == HardwareStatusReady)) {
		/* Shutdown firmware */
		PRINTM(MIOCTL, "mlan_shutdown_fw.....\n");
		handle->init_wait_q_woken = MFALSE;

		status = mlan_shutdown_fw(handle->pmlan_adapter);
		if (status == MLAN_STATUS_PENDING)
			wait_event_interruptible(handle->init_wait_q,
						 handle->init_wait_q_woken);
		PRINTM(MIOCTL, "mlan_shutdown_fw done!\n");
	}
	if (atomic_read(&handle->rx_pending) || atomic_read(&handle->tx_pending)
	    || atomic_read(&handle->ioctl_pending)) {
		PRINTM(MERROR,
		       "ERR: rx_pending=%d,tx_pending=%d,ioctl_pending=%d\n",
		       atomic_read(&handle->rx_pending),
		       atomic_read(&handle->tx_pending),
		       atomic_read(&handle->ioctl_pending));
	}
	unregister_inetaddr_notifier(&handle->woal_notifier);

#if defined(STA_CFG80211) || defined(UAP_CFG80211)
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(2, 6, 39)
	if (handle->is_remain_timer_set) {
		woal_cancel_timer(&handle->remain_timer);
		woal_remain_timer_func(handle);
	}
#endif
#endif
#if defined(WIFI_DIRECT_SUPPORT)
#if defined(STA_CFG80211) && defined(UAP_CFG80211)
	if (handle->is_go_timer_set) {
		woal_cancel_timer(&handle->go_timer);
		handle->is_go_timer_set = MFALSE;
	}
#if CFG80211_VERSION_CODE >= WIFI_DIRECT_KERNEL_VERSION
	/* Remove virtual interface */
	woal_remove_virtual_interface(handle);
#endif
#endif
#endif
	/* Remove interface */
	for (i = 0; i < MIN(MLAN_MAX_BSS_NUM, handle->priv_num); i++)
		woal_remove_interface(handle, i);

	woal_terminate_workqueue(handle);

#ifdef UAP_CFG80211
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
	if (handle->is_cac_timer_set) {
		woal_cancel_timer(&handle->cac_timer);
		handle->is_cac_timer_set = MFALSE;
	}
#endif
#endif
#ifdef REASSOCIATION
	PRINTM(MINFO, "Free reassoc_timer\n");
	if (handle->is_reassoc_timer_set) {
		woal_cancel_timer(&handle->reassoc_timer);
		handle->is_reassoc_timer_set = MFALSE;
	}
	if (handle->reassoc_thread.pid)
		wake_up_interruptible(&handle->reassoc_thread.wait_q);

	/* waiting for main thread quit */
	while (handle->reassoc_thread.pid)
		woal_sched_timeout(2);
#endif /* REASSOCIATION */
#ifdef CONFIG_PROC_FS
	woal_proc_exit(handle);
#endif
	/* Unregister device */
	PRINTM(MINFO, "unregister device\n");
	woal_unregister_dev(handle);
#ifdef ANDROID_KERNEL
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0)
	wakeup_source_trash(&handle->ws);
#else
	wake_lock_destroy(&handle->wake_lock);
#endif
#endif
	/* Free adapter structure */
	PRINTM(MINFO, "Free Adapter\n");
	woal_free_moal_handle(handle);

	for (index = 0; index < MAX_MLAN_ADAPTER; index++) {
		if (m_handle[index] == handle) {
			m_handle[index] = NULL;
			break;
		}
	}
exit_remove:
	MOAL_REL_SEMAPHORE(&AddRemoveCardSem);
exit_sem_err:
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

#ifdef CONFIG_PROC_FS
/**
 *  @brief This function switch the drv_mode
 *
 *  @param handle   A pointer to moal_handle structure
 *  @param mode     new drv_mode to switch.
 *
 *  @return        MLAN_STATUS_SUCCESS /MLAN_STATUS_FAILURE /MLAN_STATUS_PENDING
 */
mlan_status
woal_switch_drv_mode(moal_handle *handle, t_u32 mode)
{
	unsigned int i;
	mlan_status status = MLAN_STATUS_SUCCESS;
	moal_private *priv = NULL;

	ENTER();

	if (MOAL_ACQ_SEMAPHORE_BLOCK(&AddRemoveCardSem))
		goto exit_sem_err;

	if (woal_update_drv_tbl(handle, mode) != MLAN_STATUS_SUCCESS) {
		PRINTM(MERROR, "Could not update driver mode table!\n");
		status = MLAN_STATUS_FAILURE;
		goto exit;
	}

	/* Reset all interfaces */
	priv = woal_get_priv(handle, MLAN_BSS_ROLE_ANY);
	woal_reset_intf(priv, MOAL_IOCTL_WAIT, MTRUE);

	status = woal_shutdown_fw(priv, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		PRINTM(MERROR, "func shutdown failed!\n");
		goto exit;
	}

	/* Shutdown firmware */
	PRINTM(MIOCTL, "mlan_shutdown_fw.....\n");
	handle->init_wait_q_woken = MFALSE;
	status = mlan_shutdown_fw(handle->pmlan_adapter);
	if (status == MLAN_STATUS_PENDING)
		wait_event_interruptible(handle->init_wait_q,
					 handle->init_wait_q_woken);
	PRINTM(MIOCTL, "mlan_shutdown_fw done!\n");
	if (atomic_read(&handle->rx_pending) || atomic_read(&handle->tx_pending)
	    || atomic_read(&handle->ioctl_pending)) {
		PRINTM(MERROR,
		       "ERR: rx_pending=%d,tx_pending=%d,ioctl_pending=%d\n",
		       atomic_read(&handle->rx_pending),
		       atomic_read(&handle->tx_pending),
		       atomic_read(&handle->ioctl_pending));
	}

	unregister_inetaddr_notifier(&handle->woal_notifier);

	/* Remove interface */
	for (i = 0; i < MIN(MLAN_MAX_BSS_NUM, handle->priv_num); i++)
		woal_remove_interface(handle, i);

	/* Unregister mlan */
	if (handle->pmlan_adapter) {
		mlan_unregister(handle->pmlan_adapter);
		if (atomic_read(&handle->lock_count) ||
		    atomic_read(&handle->malloc_count) ||
		    atomic_read(&handle->mbufalloc_count)) {
			PRINTM(MERROR,
			       "mlan has memory leak: lock_count=%d, malloc_count=%d, mbufalloc_count=%d\n",
			       atomic_read(&handle->lock_count),
			       atomic_read(&handle->malloc_count),
			       atomic_read(&handle->mbufalloc_count));
		}
		handle->pmlan_adapter = NULL;
	}

	handle->priv_num = 0;
	drv_mode = mode;
	/* Init SW */
	if (woal_init_sw(handle)) {
		PRINTM(MFATAL, "Software Init Failed\n");
		goto exit;
	}
	/* Init FW and HW */
	if (woal_init_fw(handle)) {
		PRINTM(MFATAL, "Firmware Init Failed\n");
		goto exit;
	}
	LEAVE();
	return status;
exit:
	MOAL_REL_SEMAPHORE(&AddRemoveCardSem);
exit_sem_err:
	LEAVE();
	return status;
}
#endif

#define FW_POLL_TRIES 100
#define FW_RESET_REG  0x0EE
#define FW_RESET_VAL  0x99

/**
 *  @brief This function reload fw
 *
 *  @param handle   A pointer to moal_handle structure
 *
 *  @return        0--success, otherwise failure
 */
static int
woal_reset_and_reload_fw(moal_handle *handle)
{
	int ret = 0, tries = 0;
	t_u32 value = 1;
	t_u32 reset_reg = FW_RESET_REG;
	t_u8 reset_val = FW_RESET_VAL;

	ENTER();
	mlan_pm_wakeup_card(handle->pmlan_adapter);

    /** wait SOC fully wake up */
	for (tries = 0; tries < FW_POLL_TRIES; ++tries) {
		ret = woal_write_reg(handle, reset_reg, 0xba);
		if (ret == MLAN_STATUS_SUCCESS) {
			woal_read_reg(handle, reset_reg, &value);
			if (value == 0xba) {
				PRINTM(MMSG, "FW wake up\n");
				break;
			}
		}
		udelay(1000);
	}
	/* Write register to notify FW */
	if (woal_write_reg(handle, reset_reg, reset_val) != MLAN_STATUS_SUCCESS) {
		PRINTM(MERROR, "Failed to write register.\n");
		ret = -EFAULT;
		goto done;
	}
	/* Poll register around 100 ms */
	for (tries = 0; tries < FW_POLL_TRIES; ++tries) {
		woal_read_reg(handle, reset_reg, &value);
		if (value == 0)
			/* FW is ready */
			break;
		udelay(1000);
	}

	if (value) {
		PRINTM(MERROR, "Failed to poll FW reset register %X=0x%x\n",
		       reset_reg, value);
		ret = -EFAULT;
		goto done;
	}
	/* Download FW */
	ret = woal_request_fw(handle);
	if (ret) {
		ret = -EFAULT;
		goto done;
	}
	PRINTM(MMSG, "FW Reload successfully.");
done:
	LEAVE();
	return ret;
}

/**
 *  @brief This function reload fw
 *
 *  @param handle   A pointer to moal_handle structure
 *
 *  @return        0--success, otherwise failure
 */
static int
woal_reload_fw(moal_handle *handle)
{
	int ret = 0;
	ENTER();
	/* Download FW */
	ret = woal_request_fw(handle);
	if (ret) {
		ret = -EFAULT;
		goto done;
	}
	PRINTM(MMSG, "FW Reload successfully.");
done:
	LEAVE();
	return ret;
}

/**
 *  @brief This function reload fw
 *
 *  @param handle   A pointer to moal_handle structure
 *  @param mode     FW reload mode
 *
 *  @return        0--success, otherwise failure
 */
void
woal_request_fw_reload(moal_handle *handle, t_u8 mode)
{
	int intf_num;
	int ret = 0;
	mlan_ioctl_req *req = NULL;
	mlan_ds_misc_cfg *misc = NULL;
#if defined(WIFI_DIRECT_SUPPORT)
#if defined(STA_SUPPORT) && defined(UAP_SUPPORT)
#if defined(STA_WEXT) || defined(UAP_WEXT)
	t_u8 bss_role = MLAN_BSS_ROLE_STA;
#endif
#endif
#endif /* WIFI_DIRECT_SUPPORT && V14_FEATURE */

	ENTER();

    /** start block IOCTL */
	handle->driver_state = MTRUE;
	if (mode == FW_RELOAD_WITH_EMULATION) {
		fw_reload = FW_RELOAD_WITH_EMULATION;
		PRINTM(MMSG, "FW reload with re-emulation...\n");
		LEAVE();
		return;
	}

    /** detach network interface */
	for (intf_num = 0; intf_num < handle->priv_num; intf_num++) {
		woal_stop_queue(handle->priv[intf_num]->netdev);
		netif_device_detach(handle->priv[intf_num]->netdev);
	}
	handle->fw_reload = MTRUE;
	woal_update_firmware_name(handle);
	if (mode == FW_RELOAD_NO_EMULATION) {
		ret = woal_reload_fw(handle);
	} else if (mode == FW_RELOAD_SDIO_INBAND_RESET)
		ret = woal_reset_and_reload_fw(handle);
	else
		ret = -EFAULT;
	if (ret) {
		PRINTM(MERROR, "FW reload fail\n");
		goto done;
	}
    /** un-block IOCTL */
	handle->fw_reload = MFALSE;
	handle->driver_state = MFALSE;
	/* Restart the firmware */
	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_misc_cfg));
	if (req) {
		misc = (mlan_ds_misc_cfg *)req->pbuf;
		misc->sub_command = MLAN_OID_MISC_WARM_RESET;
		misc->param.fw_reload = MTRUE;
		req->req_id = MLAN_IOCTL_MISC_CFG;
		req->action = MLAN_ACT_SET;
		if (MLAN_STATUS_SUCCESS !=
		    woal_request_ioctl(woal_get_priv(handle, MLAN_BSS_ROLE_ANY),
				       req, MOAL_IOCTL_WAIT)) {
			ret = -EFAULT;
			kfree(req);
			goto done;
		}
		kfree(req);
	}
	handle->hardware_status = HardwareStatusReady;
	/* Reset all interfaces */
	ret = woal_reset_intf(woal_get_priv(handle, MLAN_BSS_ROLE_ANY),
			      MOAL_IOCTL_WAIT, MTRUE);
	/* Initialize private structures */
	for (intf_num = 0; intf_num < handle->priv_num; intf_num++) {
		woal_init_priv(handle->priv[intf_num], MOAL_IOCTL_WAIT);
#if defined(WIFI_DIRECT_SUPPORT)
#if defined(STA_SUPPORT) && defined(UAP_SUPPORT)
#if defined(STA_WEXT) || defined(UAP_WEXT)
		if ((handle->priv[intf_num]->bss_type ==
		     MLAN_BSS_TYPE_WIFIDIRECT) &&
		    (GET_BSS_ROLE(handle->priv[intf_num]) ==
		     MLAN_BSS_ROLE_UAP)) {
			if (MLAN_STATUS_SUCCESS !=
			    woal_bss_role_cfg(handle->priv[intf_num],
					      MLAN_ACT_SET, MOAL_IOCTL_WAIT,
					      &bss_role)) {
				ret = -EFAULT;
				goto done;
			}
		}
#endif /* STA_WEXT || UAP_WEXT */
#endif /* STA_SUPPORT && UAP_SUPPORT */
#endif /* WIFI_DIRECT_SUPPORT && V14_FEATURE */
	}

	/* Enable interfaces */
	for (intf_num = 0; intf_num < handle->priv_num; intf_num++) {
		netif_device_attach(handle->priv[intf_num]->netdev);
		woal_start_queue(handle->priv[intf_num]->netdev);
	}
done:
	LEAVE();
	return;
}

/**
 *  @brief This function initializes module.
 *
 *  @return        MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static int
woal_init_module(void)
{
	int ret = (int)MLAN_STATUS_SUCCESS;
	int index = 0;

	ENTER();

	PRINTM(MMSG, "wlan: Loading MWLAN driver\n");
	/* Init the wlan_private pointer array first */
	for (index = 0; index < MAX_MLAN_ADAPTER; index++)
		m_handle[index] = NULL;
	/* Init mutex */
	MOAL_INIT_SEMAPHORE(&AddRemoveCardSem);

#ifdef CONFIG_OF
	woal_init_from_dev_tree();
#endif

	/* Create workqueue for hang process */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 14)
	/* For kernel less than 2.6.14 name can not be greater than 10
	   characters */
	hang_workqueue = create_workqueue("MOAL_HANG_WORKQ");
#else
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)
	hang_workqueue = alloc_workqueue("MOAL_HANG_WORK_QUEUE",
					 WQ_HIGHPRI | WQ_MEM_RECLAIM |
					 WQ_UNBOUND, 1);
#else
	hang_workqueue = create_workqueue("MOAL_HANG_WORK_QUEUE");
#endif
#endif
	MLAN_INIT_WORK(&hang_work, woal_hang_work_queue);

	/* Register with bus */
	ret = woal_bus_register();
	if (ret == MLAN_STATUS_SUCCESS)
		PRINTM(MMSG, "wlan: Driver loaded successfully\n");
	else
		PRINTM(MMSG, "wlan: Driver loading failed\n");

	LEAVE();
	return ret;
}

/**
 *  @brief This function cleans module
 *
 *  @return        N/A
 */
static void
woal_cleanup_module(void)
{
	moal_handle *handle = NULL;
	int index = 0;
	int i;
#if defined(STA_SUPPORT) && defined(STA_CFG80211)
	unsigned long flags;
#endif

	ENTER();

	PRINTM(MMSG, "wlan: Unloading MWLAN driver\n");
	if (MOAL_ACQ_SEMAPHORE_BLOCK(&AddRemoveCardSem))
		goto exit_sem_err;
	for (index = 0; index < MAX_MLAN_ADAPTER; index++) {
		handle = m_handle[index];
		if (!handle)
			continue;
		if (!handle->priv_num)
			goto exit;
		if (MTRUE == woal_check_driver_status(handle))
			goto exit;

#ifdef SDIO_SUSPEND_RESUME
#ifdef MMC_PM_KEEP_POWER
		if (handle->is_suspended == MTRUE) {
			woal_sdio_resume(&
					 (((struct sdio_mmc_card *)handle->
					   card)->func)->dev);
		}
#endif /* MMC_PM_KEEP_POWER */
#endif /* SDIO_SUSPEND_RESUME */

		/* Unregister all connected radiotap net devices */
		if (handle->mon_if) {
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
			woal_set_net_monitor(handle->mon_if->priv,
					     MOAL_IOCTL_WAIT, MFALSE, 0, NULL);
			if (handle->ioctl_timeout) {
				woal_ioctl_timeout(handle);
				goto exit;
			}
#endif
			netif_device_detach(handle->mon_if->mon_ndev);
			if (handle->mon_if->mon_ndev->reg_state ==
			    NETREG_REGISTERED)
				unregister_netdev(handle->mon_if->mon_ndev);
			handle->mon_if = NULL;
		}

		for (i = 0; i < handle->priv_num; i++) {
#ifdef STA_SUPPORT
			if (GET_BSS_ROLE(handle->priv[i]) == MLAN_BSS_ROLE_STA) {
				if (handle->priv[i]->media_connected == MTRUE) {
					woal_disconnect(handle->priv[i],
							MOAL_IOCTL_WAIT_TIMEOUT,
							NULL,
							DEF_DEAUTH_REASON_CODE);
					if (handle->ioctl_timeout) {
						woal_ioctl_timeout(handle);
						goto exit;
					}
				}
#ifdef STA_CFG80211
				if (IS_STA_CFG80211(cfg80211_wext) &&
				    (handle->priv[i]->bss_type ==
				     MLAN_BSS_TYPE_STA))
					woal_clear_conn_params(handle->priv[i]);
				spin_lock_irqsave(&handle->scan_req_lock,
						  flags);
				if (IS_STA_CFG80211(cfg80211_wext) &&
				    handle->scan_request) {
					woal_cfg80211_scan_done(handle->
								scan_request,
								MTRUE);
					handle->scan_request = NULL;
					handle->scan_priv = NULL;
				}
				spin_unlock_irqrestore(&handle->scan_req_lock,
						       flags);
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 2, 0)
				if (IS_STA_CFG80211(cfg80211_wext) &&
				    handle->priv[i]->sched_scanning) {
					woal_stop_bg_scan(handle->priv[i],
							  MOAL_IOCTL_WAIT_TIMEOUT);
					if (handle->ioctl_timeout) {
						woal_ioctl_timeout(handle);
						goto exit;
					}
					handle->priv[i]->bg_scan_start = MFALSE;
					handle->priv[i]->bg_scan_reported =
						MFALSE;
					cfg80211_sched_scan_stopped(handle->
								    priv[i]->
								    wdev->wiphy
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 12, 0)
								    , 0
#endif
						);
					handle->priv[i]->sched_scanning =
						MFALSE;
				}
#endif
#endif
			}
#endif
#ifdef UAP_SUPPORT
			if (GET_BSS_ROLE(handle->priv[i]) == MLAN_BSS_ROLE_UAP) {
#ifdef MFG_CMD_SUPPORT
				if (mfg_mode != MLAN_INIT_PARA_ENABLED)
#endif
					woal_disconnect(handle->priv[i],
							MOAL_IOCTL_WAIT_TIMEOUT,
							NULL,
							DEF_DEAUTH_REASON_CODE);
				if (handle->ioctl_timeout) {
					woal_ioctl_timeout(handle);
					goto exit;
				}
			}
#endif
#if defined(STA_CFG80211) || defined(UAP_CFG80211)
			woal_clear_all_mgmt_ies(handle->priv[i],
						MOAL_IOCTL_WAIT_TIMEOUT);
			if (handle->ioctl_timeout) {
				woal_ioctl_timeout(handle);
				goto exit;
			}
			woal_flush_tx_stat_queue(handle->priv[i]);
#endif

		}

#ifdef MFG_CMD_SUPPORT
		if (mfg_mode != MLAN_INIT_PARA_ENABLED)
#endif
			woal_set_deep_sleep(woal_get_priv
					    (handle, MLAN_BSS_ROLE_ANY),
					    MOAL_IOCTL_WAIT_TIMEOUT, MFALSE, 0);

#ifdef MFG_CMD_SUPPORT
		if (mfg_mode != MLAN_INIT_PARA_ENABLED)
#endif
			woal_shutdown_fw(woal_get_priv
					 (handle, MLAN_BSS_ROLE_ANY),
					 MOAL_IOCTL_WAIT_TIMEOUT);
		if (handle->ioctl_timeout) {
			woal_ioctl_timeout(handle);
			goto exit;
		}
	}

exit:
	MOAL_REL_SEMAPHORE(&AddRemoveCardSem);
exit_sem_err:
	/* Unregister from bus */
	woal_bus_unregister();
	PRINTM(MMSG, "wlan: Driver unloaded\n");
	if (hang_workqueue) {
		flush_workqueue(hang_workqueue);
		destroy_workqueue(hang_workqueue);
		hang_workqueue = NULL;
	}

	LEAVE();
}

#ifndef MODULE
#ifdef MFG_CMD_SUPPORT
/**
 *  @brief This function handle the mfg_mode from kernel boot command
 *
 *  @param str     buffer for mfg_mode
 *  @return        N/A
 */
static int __init
mfg_mode_setup(char *str)
{
	int val = -1;
	get_option(&str, &val);
	if (val > 0)
		mfg_mode = 1;
	PRINTM(MMSG, "mfg_mode=%d\n", mfg_mode);
	return 1;
}

__setup("mfg_mode=", mfg_mode_setup);
#endif
#endif

module_init(woal_init_module);
module_exit(woal_cleanup_module);

module_param(hw_test, int, 0660);
MODULE_PARM_DESC(hw_test, "0: Disable hardware test; 1: Enable hardware test");
#ifdef CONFIG_OF
module_param(dts_enable, int, 0);
MODULE_PARM_DESC(dts_enable, "0: Disable DTS; 1: Enable DTS");
#endif
module_param(fw_name, charp, 0660);
MODULE_PARM_DESC(fw_name, "Firmware name");
module_param(req_fw_nowait, int, 0);
MODULE_PARM_DESC(req_fw_nowait,
		 "0: Use request_firmware API; 1: Use request_firmware_nowait API");
module_param(fw_reload, int, 0);
MODULE_PARM_DESC(fw_reload,
		 "0: disable fw_reload; 1: enable fw reload feature");
module_param(fw_serial, int, 0);
MODULE_PARM_DESC(fw_serial,
		 "0: support parallel download FW; 1: support serial download FW");
module_param(fw_region, int, 0);
MODULE_PARM_DESC(fw_region, "1: create channel regulatory domain from FW");
module_param(mac_addr, charp, 0660);
MODULE_PARM_DESC(mac_addr, "MAC address");
#ifdef MFG_CMD_SUPPORT
module_param(mfg_mode, int, 0660);
MODULE_PARM_DESC(mfg_mode,
		 "0: Download normal firmware; 1: Download MFG firmware");
#endif /* MFG_CMD_SUPPORT */
module_param(drv_mode, int, 0660);
#if defined(WIFI_DIRECT_SUPPORT)
MODULE_PARM_DESC(drv_mode,
		 "Bit 0: STA; Bit 1: uAP; Bit 2: WIFIDIRECT; Bit 4: NAN");
#else
MODULE_PARM_DESC(drv_mode, "Bit 0: STA; Bit 1: uAP; Bit 4: NAN");
#endif /* WIFI_DIRECT_SUPPORT && V14_FEATURE */
#ifdef STA_SUPPORT
module_param(max_sta_bss, int, 0);
MODULE_PARM_DESC(max_sta_bss, "Number of STA interfaces (1)");
module_param(sta_name, charp, 0);
MODULE_PARM_DESC(sta_name, "STA interface name");
#endif /* STA_SUPPORT */
#ifdef UAP_SUPPORT
module_param(max_uap_bss, int, 0);
MODULE_PARM_DESC(max_uap_bss, "Number of uAP interfaces (1)");
module_param(uap_name, charp, 0);
MODULE_PARM_DESC(uap_name, "uAP interface name");
#endif /* UAP_SUPPORT */
#if defined(WIFI_DIRECT_SUPPORT)
module_param(max_wfd_bss, int, 0);
MODULE_PARM_DESC(max_wfd_bss, "Number of WIFIDIRECT interfaces (1)");
module_param(wfd_name, charp, 0);
MODULE_PARM_DESC(wfd_name, "WIFIDIRECT interface name");
#if defined(STA_CFG80211) && defined(UAP_CFG80211)
module_param(max_vir_bss, int, 0);
MODULE_PARM_DESC(max_vir_bss, "Number of Virtual interfaces (0)");
#endif
#endif /* WIFI_DIRECT_SUPPORT && V14_FEATURE */
module_param(nan_name, charp, 0);
MODULE_PARM_DESC(nan_name, "NAN interface name");
module_param(max_nan_bss, int, 0);
MODULE_PARM_DESC(max_nan_bss, "Number of NAN interfaces (1)");
#ifdef DEBUG_LEVEL1
module_param(drvdbg, uint, 0660);
MODULE_PARM_DESC(drvdbg, "Driver debug");
#endif /* DEBUG_LEVEL1 */
module_param(auto_ds, int, 0660);
MODULE_PARM_DESC(auto_ds,
		 "0: MLAN default; 1: Enable auto deep sleep; 2: Disable auto deep sleep");
module_param(ps_mode, int, 0660);
MODULE_PARM_DESC(ps_mode,
		 "0: MLAN default; 1: Enable IEEE PS mode; 2: Disable IEEE PS mode");
module_param(max_tx_buf, int, 0);
MODULE_PARM_DESC(max_tx_buf, "Maximum Tx buffer size (2048/4096/8192)");

module_param(intmode, int, 0);
MODULE_PARM_DESC(intmode, "0: INT_MODE_SDIO, 1: INT_MODE_GPIO");
module_param(gpiopin, int, 0);
MODULE_PARM_DESC(gpiopin, "255:new GPIO int mode, other vlue: gpio pin number");

#ifdef SDIO_SUSPEND_RESUME
module_param(pm_keep_power, int, 0);
MODULE_PARM_DESC(pm_keep_power, "1: PM keep power; 0: PM no power");
module_param(shutdown_hs, int, 0);
MODULE_PARM_DESC(shutdown_hs,
		 "1: Enable HS when shutdown; 0: No HS when shutdown");
#endif
#if defined(STA_SUPPORT)
module_param(cfg_11d, int, 0);
MODULE_PARM_DESC(cfg_11d,
		 "0: MLAN default; 1: Enable 802.11d; 2: Disable 802.11d");
#endif
module_param(dpd_data_cfg, charp, 0);
MODULE_PARM_DESC(dpd_data_cfg, "DPD data file name");
module_param(init_cfg, charp, 0);
MODULE_PARM_DESC(init_cfg, "Init config file name");
module_param(cal_data_cfg, charp, 0);
MODULE_PARM_DESC(cal_data_cfg, "Calibration data file name");
module_param(txpwrlimit_cfg, charp, 0);
MODULE_PARM_DESC(txpwrlimit_cfg,
		 "Set configuration data of Tx power limitation");
module_param(cntry_txpwr, int, 0);
MODULE_PARM_DESC(cntry_txpwr,
		 "Allow setting tx power table of country; 0: disable (default), 1: enable.");
module_param(init_hostcmd_cfg, charp, 0);
MODULE_PARM_DESC(init_hostcmd_cfg, "Init hostcmd file name");
module_param(cfg80211_wext, int, 0660);
MODULE_PARM_DESC(cfg80211_wext,
#ifdef STA_WEXT
		 "Bit 0: STA WEXT; "
#endif
#ifdef UAP_WEXT
		 "Bit 1: UAP WEXT; "
#endif
#ifdef STA_CFG80211
		 "Bit 2: STA CFG80211; "
#endif
#ifdef UAP_CFG80211
		 "Bit 3: UAP CFG80211;"
#endif
	);
module_param(wq_sched_prio, int, 0);
module_param(wq_sched_policy, int, 0);
MODULE_PARM_DESC(wq_sched_prio, "Priority of work queue");
MODULE_PARM_DESC(wq_sched_policy,
		 "0: SCHED_NORMAL; 1: SCHED_FIFO; 2: SCHED_RR; 3: SCHED_BATCH; 5: SCHED_IDLE");
module_param(rx_work, int, 0);
MODULE_PARM_DESC(rx_work,
		 "0: default; 1: Enable rx_work_queue; 2: Disable rx_work_queue");
#if defined(WIFI_DIRECT_SUPPORT)
#if defined(STA_CFG80211) && defined(UAP_CFG80211)
#if CFG80211_VERSION_CODE >= WIFI_DIRECT_KERNEL_VERSION
module_param(p2p_enh, int, 0);
MODULE_PARM_DESC(p2p_enh, "1: Enable enhanced P2P; 0: Disable enhanced P2P");
#endif
#endif
#endif

#ifdef ANDROID_KERNEL
module_param(wakelock_timeout, int, 0);
MODULE_PARM_DESC(wakelock_timeout, "set wakelock_timeout value (ms)");
#endif

module_param(dev_cap_mask, uint, 0);
MODULE_PARM_DESC(dev_cap_mask, "Device capability mask");

module_param(sdio_rx_aggr, int, 0);
MODULE_PARM_DESC(sdio_rx_aggr,
		 "1: Enable SDIO rx aggr; 0: Disable SDIO rx aggr");

module_param(pmic, int, 0);
MODULE_PARM_DESC(pmic,
		 "1: Send pmic configure cmd to firmware; 0: No pmic configure cmd sent to firmware");

module_param(uap_oper_ctrl, uint, 0);
MODULE_PARM_DESC(uap_oper_ctrl, "0:default; 0x20001:uap restarts on channel 6");

module_param(hs_wake_interval, int, 0660);
MODULE_PARM_DESC(hs_wake_interval,
		 "Host sleep wakeup interval,it will round to nearest multiple dtim*beacon_period in fw");
module_param(indication_gpio, int, 0);
MODULE_PARM_DESC(indication_gpio,
		 "GPIO to indicate wakeup source; high four bits: level for normal wakeup; low four bits: GPIO pin number.");

module_param(indrstcfg, int, 0);
MODULE_PARM_DESC(indrstcfg,
		 "Independent reset configuration; high byte: GPIO pin number; low byte: IR mode");

module_param(fixed_beacon_buffer, int, 0);
MODULE_PARM_DESC(fixed_beacon_buffer,
		 "0: allocate default buffer size; 1: allocate max buffer size.");

#ifdef WIFI_DIRECT_SUPPORT
module_param(GoAgeoutTime, int, 0);
MODULE_PARM_DESC(GoAgeoutTime,
		 "0: use default ageout time; set Go age out time (TU 100ms)");
#endif

module_param(gtk_rekey_offload, int, 0);
MODULE_PARM_DESC(gtk_rekey_offload,
		 "0: disable gtk_rekey_offload; 1: enable gtk_rekey_offload (default); 2: enable gtk_rekey_offload in suspend mode only;");

module_param(multi_dtim, int, 0);
MODULE_PARM_DESC(multi_dtim, "DTIM interval");

module_param(inact_tmo, int, 0);
MODULE_PARM_DESC(inact_tmo, "IEEE ps inactivity timout value");

module_param(napi, int, 0);
MODULE_PARM_DESC(napi, "1: enable napi api; 0: disable napi");

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
module_param(dfs_offload, int, 0);
MODULE_PARM_DESC(dfs_offload, "1: enable dfs offload; 0: disable dfs offload.");
#endif

module_param(drcs_chantime_mode, int, 0);
MODULE_PARM_DESC(drcs_chantime_mode,
		 "0: use default value;Bit31~Bit24:Channel time for channel index0;Bit23~Bit16:mode for channel index0;Bit15~Bit8:Channel time for channel index1;Bit7~Bit0:mode for channel index1; mode:0--PM1,1--Null2Self.");

module_param(roamoffload_in_hs, int, 0);
MODULE_PARM_DESC(roamoffload_in_hs,
		 "1: enable fw roaming only when host suspend; 0: always enable fw roaming.");

MODULE_DESCRIPTION("M-WLAN Driver");
MODULE_AUTHOR("Marvell International Ltd.");
MODULE_VERSION(MLAN_RELEASE_VERSION);
MODULE_LICENSE("GPL");
