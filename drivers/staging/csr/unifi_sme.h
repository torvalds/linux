/*
 * ***************************************************************************
 *  FILE:     unifi_sme.h
 *
 *  PURPOSE:    SME related definitions.
 *
 *  Copyright (C) 2007-2011 by Cambridge Silicon Radio Ltd.
 *
 *  Refer to LICENSE.txt included with this source code for details on
 *  the license terms.
 *
 * ***************************************************************************
 */
#ifndef __LINUX_UNIFI_SME_H__
#define __LINUX_UNIFI_SME_H__ 1

#include <linux/kernel.h>

#ifdef CSR_SME_USERSPACE
#include "sme_userspace.h"
#endif

#include "csr_wifi_sme_lib.h"

typedef int unifi_data_port_action;

typedef struct unifi_port_cfg
{
    /* TRUE if this port entry is allocated */
    CsrBool in_use;
    CsrWifiRouterCtrlPortAction port_action;
    CsrWifiMacAddress mac_address;
} unifi_port_cfg_t;

#define UNIFI_MAX_CONNECTIONS           8
#define UNIFI_MAX_RETRY_LIMIT           5
#define UF_DATA_PORT_NOT_OVERIDE        0
#define UF_DATA_PORT_OVERIDE            1

typedef struct unifi_port_config
{
    int entries_in_use;
    int overide_action;
    unifi_port_cfg_t port_cfg[UNIFI_MAX_CONNECTIONS];
} unifi_port_config_t;


enum sme_request_status {
    SME_REQUEST_EMPTY,
    SME_REQUEST_PENDING,
    SME_REQUEST_RECEIVED,
    SME_REQUEST_TIMEDOUT,
    SME_REQUEST_CANCELLED,
};

/* Structure to hold a UDI logged signal */
typedef struct {

    /* The current status of the request */
    enum sme_request_status request_status;

    /* The status the SME has passed to us */
    CsrResult reply_status;

    /* SME's reply to a get request */
    CsrWifiSmeVersions versions;
    CsrWifiSmePowerConfig powerConfig;
    CsrWifiSmeHostConfig hostConfig;
    CsrWifiSmeStaConfig staConfig;
    CsrWifiSmeDeviceConfig deviceConfig;
    CsrWifiSmeCoexInfo coexInfo;
    CsrWifiSmeCoexConfig coexConfig;
    CsrWifiSmeMibConfig mibConfig;
    CsrWifiSmeConnectionInfo connectionInfo;
    CsrWifiSmeConnectionConfig connectionConfig;
    CsrWifiSmeConnectionStats connectionStats;


    /* SME's reply to a scan request */
    u16 reply_scan_results_count;
    CsrWifiSmeScanResult* reply_scan_results;

} sme_reply_t;


typedef struct {
    u16 appHandle;
    CsrWifiRouterEncapsulation encapsulation;
    u16 protocol;
    u8 oui[3];
    u8 in_use;
} sme_ma_unidata_ind_filter_t;


CsrWifiRouterCtrlPortAction uf_sme_port_state(unifi_priv_t *priv,
                                          unsigned char *address,
                                          int queue,
                                          u16 interfaceTag);
unifi_port_cfg_t *uf_sme_port_config_handle(unifi_priv_t *priv,
                                            unsigned char *address,
                                            int queue,
                                            u16 interfaceTag);



/* Callback for event logging to SME clients */
void sme_log_event(ul_client_t *client, const u8 *signal, int signal_len,
                   const bulk_data_param_t *bulkdata, int dir);

/* The workqueue task to the set the multicast addresses list */
void uf_multicast_list_wq(struct work_struct *work);

/* The workqueue task to execute the TA module */
void uf_ta_wq(struct work_struct *work);


/*
 * SME blocking helper functions
 */
#ifdef UNIFI_DEBUG
# define sme_complete_request(priv, status)   uf_sme_complete_request(priv, status, __func__)
#else
# define sme_complete_request(priv, status)   uf_sme_complete_request(priv, status, NULL)
#endif

void uf_sme_complete_request(unifi_priv_t *priv, CsrResult reply_status, const char *func);
void uf_sme_cancel_request(unifi_priv_t *priv, CsrResult reply_status);


/*
 * Blocking functions using the SME SYS API.
 */
int sme_sys_suspend(unifi_priv_t *priv);
int sme_sys_resume(unifi_priv_t *priv);


/*
 * Traffic Analysis workqueue jobs
 */
void uf_ta_ind_wq(struct work_struct *work);
void uf_ta_sample_ind_wq(struct work_struct *work);

/*
 * SME config workqueue job
 */
void uf_sme_config_wq(struct work_struct *work);

/*
 * To send M4 read to send IND
 */
void uf_send_m4_ready_wq(struct work_struct *work);

#if (defined(CSR_WIFI_SECURITY_WAPI_ENABLE) && defined(CSR_WIFI_SECURITY_WAPI_SW_ENCRYPTION))
/*
 * To send data pkt to Sme for encryption
 */
void uf_send_pkt_to_encrypt(struct work_struct *work);
#endif

int sme_mgt_power_config_set(unifi_priv_t *priv, CsrWifiSmePowerConfig *powerConfig);
int sme_mgt_power_config_get(unifi_priv_t *priv, CsrWifiSmePowerConfig *powerConfig);
int sme_mgt_host_config_set(unifi_priv_t *priv, CsrWifiSmeHostConfig *hostConfig);
int sme_mgt_host_config_get(unifi_priv_t *priv, CsrWifiSmeHostConfig *hostConfig);
int sme_mgt_sme_config_set(unifi_priv_t *priv, CsrWifiSmeStaConfig *staConfig, CsrWifiSmeDeviceConfig *deviceConfig);
int sme_mgt_sme_config_get(unifi_priv_t *priv, CsrWifiSmeStaConfig *staConfig, CsrWifiSmeDeviceConfig *deviceConfig);
int sme_mgt_coex_info_get(unifi_priv_t *priv, CsrWifiSmeCoexInfo *coexInfo);
int sme_mgt_packet_filter_set(unifi_priv_t *priv);
int sme_mgt_tspec(unifi_priv_t *priv, CsrWifiSmeListAction action,
                  u32 tid, CsrWifiSmeDataBlock *tspec, CsrWifiSmeDataBlock *tclas);

#ifdef CSR_SUPPORT_WEXT
/*
 * Blocking functions using the SME MGT API.
 */
int sme_mgt_wifi_on(unifi_priv_t *priv);
int sme_mgt_wifi_off(unifi_priv_t *priv);
/*int sme_mgt_set_value_async(unifi_priv_t *priv, unifi_AppValue *app_value);
int sme_mgt_get_value_async(unifi_priv_t *priv, unifi_AppValue *app_value);
int sme_mgt_get_value(unifi_priv_t *priv, unifi_AppValue *app_value);
int sme_mgt_set_value(unifi_priv_t *priv, unifi_AppValue *app_value);
*/
int sme_mgt_coex_config_set(unifi_priv_t *priv, CsrWifiSmeCoexConfig *coexConfig);
int sme_mgt_coex_config_get(unifi_priv_t *priv, CsrWifiSmeCoexConfig *coexConfig);
int sme_mgt_mib_config_set(unifi_priv_t *priv, CsrWifiSmeMibConfig *mibConfig);
int sme_mgt_mib_config_get(unifi_priv_t *priv, CsrWifiSmeMibConfig *mibConfig);

int sme_mgt_connection_info_set(unifi_priv_t *priv, CsrWifiSmeConnectionInfo *connectionInfo);
int sme_mgt_connection_info_get(unifi_priv_t *priv, CsrWifiSmeConnectionInfo *connectionInfo);
int sme_mgt_connection_config_set(unifi_priv_t *priv, CsrWifiSmeConnectionConfig *connectionConfig);
int sme_mgt_connection_config_get(unifi_priv_t *priv, CsrWifiSmeConnectionConfig *connectionConfig);
int sme_mgt_connection_stats_get(unifi_priv_t *priv, CsrWifiSmeConnectionStats *connectionStats);

int sme_mgt_versions_get(unifi_priv_t *priv, CsrWifiSmeVersions *versions);


int sme_mgt_scan_full(unifi_priv_t *priv, CsrWifiSsid *specific_ssid,
                      int num_channels, unsigned char *channel_list);
int sme_mgt_scan_results_get_async(unifi_priv_t *priv,
                                   struct iw_request_info *info,
                                   char *scan_results,
                                   long scan_results_len);
int sme_mgt_disconnect(unifi_priv_t *priv);
int sme_mgt_connect(unifi_priv_t *priv);
int sme_mgt_key(unifi_priv_t *priv, CsrWifiSmeKey *sme_key,
                CsrWifiSmeListAction action);
int sme_mgt_pmkid(unifi_priv_t *priv, CsrWifiSmeListAction action,
                  CsrWifiSmePmkidList *pmkid_list);
int sme_mgt_mib_get(unifi_priv_t *priv,
                    unsigned char *varbind, int *length);
int sme_mgt_mib_set(unifi_priv_t *priv,
                    unsigned char *varbind, int length);
#ifdef CSR_SUPPORT_WEXT_AP
int sme_ap_start(unifi_priv_t *priv,u16 interface_tag,CsrWifiSmeApConfig_t *ap_config);
int sme_ap_stop(unifi_priv_t *priv,u16 interface_tag);
int sme_ap_config(unifi_priv_t *priv,CsrWifiSmeApMacConfig *ap_mac_config, CsrWifiNmeApConfig *group_security_config);
int uf_configure_supported_rates(u8 * supportedRates, u8 phySupportedBitmap);
#endif
int unifi_translate_scan(struct net_device *dev,
                         struct iw_request_info *info,
                         char *current_ev, char *end_buf,
                         CsrWifiSmeScanResult *scan_data,
                         int scan_index);

#endif /* CSR_SUPPORT_WEXT */

int unifi_cfg_power(unifi_priv_t *priv, unsigned char *arg);
int unifi_cfg_power_save(unifi_priv_t *priv, unsigned char *arg);
int unifi_cfg_power_supply(unifi_priv_t *priv, unsigned char *arg);
int unifi_cfg_packet_filters(unifi_priv_t *priv, unsigned char *arg);
int unifi_cfg_wmm_qos_info(unifi_priv_t *priv, unsigned char *arg);
int unifi_cfg_wmm_addts(unifi_priv_t *priv, unsigned char *arg);
int unifi_cfg_wmm_delts(unifi_priv_t *priv, unsigned char *arg);
int unifi_cfg_get_info(unifi_priv_t *priv, unsigned char *arg);
int unifi_cfg_strict_draft_n(unifi_priv_t *priv, unsigned char *arg);
int unifi_cfg_enable_okc(unifi_priv_t *priv, unsigned char *arg);
#ifdef CSR_SUPPORT_WEXT_AP
int unifi_cfg_set_ap_config(unifi_priv_t * priv,unsigned char* arg);
#endif



int convert_sme_error(CsrResult error);


#endif /* __LINUX_UNIFI_SME_H__ */
