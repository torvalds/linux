/*
 *****************************************************************************
 *
 * FILE : unifi_native.h
 *
 * PURPOSE : Private header file for unifi driver support to wireless extensions.
 *
 *           UDI = UniFi Debug Interface
 *
 * Copyright (C) 2005-2008 by Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 *
 *****************************************************************************
 */
#ifndef __LINUX_UNIFI_NATIVE_H__
#define __LINUX_UNIFI_NATIVE_H__ 1

#include <linux/kernel.h>
#include <linux/if_arp.h>


/*
 *      scan.c wext.c autojoin.c
 */
/* Structure to hold results of a scan */
typedef struct scan_info {

/*    CSR_MLME_SCAN_INDICATION msi; */

    unsigned char *info_elems;
    int info_elem_length;

} scan_info_t;


#define IE_VECTOR_MAXLEN 1024

#ifdef CSR_SUPPORT_WEXT
/*
 * Structre to hold the wireless network configuration info.
 */
struct wext_config {

    /* Requested channel when setting up an adhoc network */
    int channel;

    /* wireless extns mode: IW_MODE_AUTO, ADHOC, INFRA, MASTER ... MONITOR */
    int mode;

    /* The capabilities of the currently joined network */
    int capability;

    /* The interval between beacons if we create an IBSS */
    int beacon_period;

    /*
    * Power-save parameters
    */
    /* The listen interval to ask for in Associate req. */
    int assoc_listen_interval;
    /* Power-mode to put UniFi into */

    unsigned char desired_ssid[UNIFI_MAX_SSID_LEN];     /* the last ESSID set by SIOCSIWESSID */
    int power_mode;
    /* Whether to wake for broadcast packets (using DTIM interval) */
    int wakeup_for_dtims;

    /* Currently selected WEP Key ID (0..3) */
    int wep_key_id;

    wep_key_t wep_keys[NUM_WEPKEYS];

/*    CSR_AUTHENTICATION_TYPE auth_type; */
    int privacy;

    u32 join_failure_timeout;
    u32 auth_failure_timeout;
    u32 assoc_failure_timeout;

    unsigned char generic_ie[IE_VECTOR_MAXLEN];
    int generic_ie_len;

    struct iw_statistics wireless_stats;


    /* the ESSID we are currently associated to */
    unsigned char current_ssid[UNIFI_MAX_SSID_LEN];
    /* the BSSID we are currently associated to */
    unsigned char current_bssid[6];

    /*
    * IW_AUTH_WPA_VERSION_DISABLED 0x00000001
    * IW_AUTH_WPA_VERSION_WPA      0x00000002
    * IW_AUTH_WPA_VERSION_WPA2     0x00000004
    */
    unsigned char wpa_version;

    /*
     * cipher selection:
    * IW_AUTH_CIPHER_NONE	0x00000001
    * IW_AUTH_CIPHER_WEP40	0x00000002
    * IW_AUTH_CIPHER_TKIP	0x00000004
    * IW_AUTH_CIPHER_CCMP	0x00000008
    * IW_AUTH_CIPHER_WEP104	0x00000010
    */
    unsigned char pairwise_cipher_used;
    unsigned char group_cipher_used;

    unsigned int frag_thresh;
    unsigned int rts_thresh;

    /* U-APSD value, send with Association Request to WMM Enabled APs */
    unsigned char wmm_bss_uapsd_mask;
    /* The WMM capabilities of the selected BSS */
    unsigned int bss_wmm_capabilities;

    /* Flag to prevent a join when the ssid is set */
    int disable_join_on_ssid_set;

    /* Scan info */
#define UNIFI_MAX_SCANS 32
    scan_info_t scan_list[UNIFI_MAX_SCANS];
    int num_scan_info;

    /* Flag on whether non-802.1x packets are allowed out */
/*    CsrWifiRouterPortAction block_controlled_port;*/

    /* Flag on whether we have completed an authenticate/associate process */
    unsigned int flag_associated        : 1;
}; /* struct wext_config */

#endif /* CSR_SUPPORT_WEXT */


/*
 *      wext.c
 */
/*int mlme_set_protection(unifi_priv_t *priv, unsigned char *addr,
                        CSR_PROTECT_TYPE prot, CSR_KEY_TYPE key_type);
*/

/*
 * scan.c
 */
/*
void unifi_scan_indication_handler(unifi_priv_t *priv,
                                   const CSR_MLME_SCAN_INDICATION *msg,
                                   const unsigned char *extra,
                                   unsigned int len);
*/
void unifi_clear_scan_table(unifi_priv_t *priv);
scan_info_t *unifi_get_scan_report(unifi_priv_t *priv, int index);


/*
 * Utility functions
 */
const unsigned char *unifi_find_info_element(int id,
                                             const unsigned char *info,
                                             int len);
int unifi_add_info_element(unsigned char *info,
                           int ie_id,
                           const unsigned char *ie_data,
                           int ie_len);

/*
 *      autojoin.c
 */
/* Higher level fns */
int unifi_autojoin(unifi_priv_t *priv, const char *ssid);
/*
int unifi_do_scan(unifi_priv_t *priv, int scantype, CSR_BSS_TYPE bsstype,
                  const char *ssid, int ssid_len);
*/
int unifi_set_powermode(unifi_priv_t *priv);
int unifi_join_ap(unifi_priv_t *priv, scan_info_t *si);
int unifi_join_bss(unifi_priv_t *priv, unsigned char *macaddr);
int unifi_leave(unifi_priv_t *priv);
unsigned int unifi_get_wmm_bss_capabilities(unifi_priv_t *priv,
                                            unsigned char *ie_vector,
                                            int ie_len, int *ap_capabilities);

/*
 * Status and management.
 */
int uf_init_wext_interface(unifi_priv_t *priv);
void uf_deinit_wext_interface(unifi_priv_t *priv);

/*
 * Function to reset UniFi's 802.11 state by sending MLME-RESET.req
 */
int unifi_reset_state(unifi_priv_t *priv, unsigned char *macaddr, unsigned char set_default_mib);


/*
 *      mlme.c
 */
/* Abort an MLME operation - useful in error recovery */
int uf_abort_mlme(unifi_priv_t *priv);

int unifi_mlme_blocking_request(unifi_priv_t *priv, ul_client_t *pcli,
                                CSR_SIGNAL *sig, bulk_data_param_t *data_ptrs,
                                int timeout);
void unifi_mlme_copy_reply_and_wakeup_client(ul_client_t *pcli,
                                             CSR_SIGNAL *signal, int signal_len,
                                             const bulk_data_param_t *bulkdata);

/*
 * Utility functions
 */
const char *lookup_reason_code(int reason);
const char *lookup_result_code(int result);


/*
 *      sme_native.c
 */
int uf_sme_init(unifi_priv_t *priv);
void uf_sme_deinit(unifi_priv_t *priv);
int sme_sys_suspend(unifi_priv_t *priv);
int sme_sys_resume(unifi_priv_t *priv);
int sme_mgt_wifi_on(unifi_priv_t *priv);

/* Callback for event logging to SME clients (unifi_manager) */
void sme_native_log_event(ul_client_t *client,
                          const u8 *sig_packed, int sig_len,
                          const bulk_data_param_t *bulkdata,
                          int dir);

void sme_native_mlme_event_handler(ul_client_t *pcli,
                                   const u8 *sig_packed, int sig_len,
                                   const bulk_data_param_t *bulkdata,
                                   int dir);

/* Task to query statistics from the MIB */
#define UF_SME_STATS_WQ_TIMEOUT     2000    /* in msecs */
void uf_sme_stats_wq(struct work_struct *work);

void uf_native_process_udi_signal(ul_client_t *pcli,
                                  const u8 *packed_signal,
                                  int packed_signal_len,
                                  const bulk_data_param_t *bulkdata, int dir);
#ifdef UNIFI_SNIFF_ARPHRD
/*
 * monitor.c
 */
int uf_start_sniff(unifi_priv_t *priv);
/*
void ma_sniffdata_ind(void *ospriv,
                      const CSR_MA_SNIFFDATA_INDICATION *ind,
                      const bulk_data_param_t *bulkdata);
*/
#endif /* ARPHRD_IEEE80211_PRISM */

#endif /* __LINUX_UNIFI_NATIVE_H__ */
