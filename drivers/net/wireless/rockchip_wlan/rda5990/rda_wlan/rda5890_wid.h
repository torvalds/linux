#ifndef _RDA5890_WID_H_
#define _RDA5890_WID_H_

#define MAX_STRING_LEN      256
#define MAX_CMD_LEN         MAX_STRING_LEN
#define RDA5890_MAX_WID_LEN    MAX_CMD_LEN - 2

#define MAC_CONNECTED          1
#define MAC_DISCONNECTED       0

#define WID_STATUS_SUCCESS       1

typedef enum
{
    G_SHORT_PREAMBLE = 0,
    G_LONG_PREAMBLE = 1,
    G_AUTO_PREAMBLE = 2
}G_PREAMBLE_T;

/* WLAN Identifiers */
typedef enum {
			WID_NIL                            = -1,
			WID_BSS_TYPE                       = 0x0000,
			WID_CURRENT_TX_RATE                = 0x0001,
			WID_CURRENT_CHANNEL                = 0x0002,
			WID_PREAMBLE                       = 0x0003,
			WID_11G_OPERATING_MODE             = 0x0004,
			WID_STATUS                         = 0x0005,
			WID_11G_PROT_MECH                  = 0x0006,

#ifdef MAC_HW_UNIT_TEST_MODE
			WID_GOTO_SLEEP                     = 0x0007,
#else /* MAC_HW_UNIT_TEST_MODE */
			WID_SCAN_TYPE                      = 0x0007,
#endif /* MAC_HW_UNIT_TEST_MODE */
			WID_PRIVACY_INVOKED                = 0x0008,
			WID_KEY_ID                         = 0x0009,
			WID_QOS_ENABLE                     = 0x000A,
			WID_POWER_MANAGEMENT               = 0x000B,
			WID_802_11I_MODE                   = 0x000C,
			WID_AUTH_TYPE                      = 0x000D,
			WID_SITE_SURVEY                    = 0x000E,
			WID_LISTEN_INTERVAL                = 0x000F,
			WID_DTIM_PERIOD                    = 0x0010,
			WID_ACK_POLICY                     = 0x0011,
			WID_RESET                          = 0x0012,
			WID_PCF_MODE                       = 0x0013,
			WID_CFP_PERIOD                     = 0x0014,
			WID_BCAST_SSID                     = 0x0015,

#ifdef MAC_HW_UNIT_TEST_MODE
			WID_PHY_TEST_PATTERN               = 0x0016,
#else /* MAC_HW_UNIT_TEST_MODE */
			WID_DISCONNECT                     = 0x0016,
#endif /* MAC_HW_UNIT_TEST_MODE */

			WID_READ_ADDR_SDRAM                = 0x0017,
			WID_TX_POWER_LEVEL_11A             = 0x0018,
			WID_REKEY_POLICY                   = 0x0019,
			WID_SHORT_SLOT_ALLOWED             = 0x001A,
			WID_PHY_ACTIVE_REG                 = 0x001B,
			WID_PHY_ACTIVE_REG_VAL             = 0x001C,
			WID_TX_POWER_LEVEL_11B             = 0x001D,
			WID_START_SCAN_REQ                 = 0x001E,
			WID_RSSI                           = 0x001F,
			WID_JOIN_REQ                       = 0x0020,
			WID_ANTENNA_SELECTION              = 0x0021,
			WID_USER_CONTROL_ON_TX_POWER       = 0x0027,
			WID_MEMORY_ACCESS_8BIT             = 0x0029,
			WID_UAPSD_SUPPORT_AP               = 0x002A,

			WID_CURRENT_MAC_STATUS             = 0x0031,
			WID_AUTO_RX_SENSITIVITY            = 0x0032,
			WID_DATAFLOW_CONTROL               = 0x0033,
			WID_SCAN_FILTER                    = 0x0036,
			WID_LINK_LOSS_THRESHOLD            = 0x0037,
			WID_AUTORATE_TYPE                  = 0x0038,
			WID_CCA_THRESHOLD                  = 0x0039,

			WID_802_11H_DFS_MODE               = 0x003B,
			WID_802_11H_TPC_MODE               = 0x003C,

			WID_PHY_REG_ADDR                   = 0x0040,
			WID_PHY_REG_VAL                    = 0x0041,
			WID_PTA_MODE                       = 0x0042,
			WID_TRAP_TEST                      = 0x0043,
			WID_PTA_BLOCK_BT                   = 0x0044,
			WID_NETWORK_INFO_EN          = 0x0045,

			WID_RTS_THRESHOLD                  = 0x1000,
			WID_FRAG_THRESHOLD                 = 0x1001,
			WID_SHORT_RETRY_LIMIT              = 0x1002,
			WID_LONG_RETRY_LIMIT               = 0x1003,
			WID_CFP_MAX_DUR                    = 0x1004,
			WID_PHY_TEST_FRAME_LEN             = 0x1005,
			WID_BEACON_INTERVAL                = 0x1006,
			WID_MEMORY_ACCESS_16BIT            = 0x1008,

			WID_RX_SENSE                       = 0x100B,
			WID_ACTIVE_SCAN_TIME               = 0x100C,
			WID_PASSIVE_SCAN_TIME              = 0x100D,
			WID_SITE_SURVEY_SCAN_TIME          = 0x100E,
			WID_JOIN_TIMEOUT                   = 0x100F,
			WID_AUTH_TIMEOUT                   = 0x1010,
			WID_ASOC_TIMEOUT                   = 0x1011,
			WID_11I_PROTOCOL_TIMEOUT           = 0x1012,
			WID_EAPOL_RESPONSE_TIMEOUT         = 0x1013,
			WID_CCA_BUSY_STATUS                = 0x1014,

			WID_FAILED_COUNT                   = 0x2000,
			WID_RETRY_COUNT                    = 0x2001,
			WID_MULTIPLE_RETRY_COUNT           = 0x2002,
			WID_FRAME_DUPLICATE_COUNT          = 0x2003,
			WID_ACK_FAILURE_COUNT              = 0x2004,
			WID_RECEIVED_FRAGMENT_COUNT        = 0x2005,
			WID_MULTICAST_RECEIVED_FRAME_COUNT = 0x2006,
			WID_FCS_ERROR_COUNT                = 0x2007,
			WID_SUCCESS_FRAME_COUNT            = 0x2008,
			WID_PHY_TEST_PKT_CNT               = 0x2009,
			WID_PHY_TEST_TXD_PKT_CNT           = 0x200A,
			WID_TX_FRAGMENT_COUNT              = 0x200B,
			WID_TX_MULTICAST_FRAME_COUNT       = 0x200C,
			WID_RTS_SUCCESS_COUNT              = 0x200D,
			WID_RTS_FAILURE_COUNT              = 0x200E,
			WID_WEP_UNDECRYPTABLE_COUNT        = 0x200F,
			WID_REKEY_PERIOD                   = 0x2010,
			WID_REKEY_PACKET_COUNT             = 0x2011,
#ifdef MAC_HW_UNIT_TEST_MODE
			WID_Q_ENABLE_INFO                  = 0x2012,
#else /* MAC_HW_UNIT_TEST_MODE */
			WID_802_1X_SERV_ADDR               = 0x2012,
#endif /* MAC_HW_UNIT_TEST_MODE */
			WID_STACK_IP_ADDR                  = 0x2013,
			WID_STACK_NETMASK_ADDR             = 0x2014,
			WID_HW_RX_COUNT                    = 0x2015,
			WID_MEMORY_ADDRESS                 = 0x201E,
			WID_MEMORY_ACCESS_32BIT            = 0x201F,
			WID_RF_REG_VAL                     = 0x2021,
			WID_FIRMWARE_INFO                  = 0x2023,

			WID_SYS_FW_VER                     = 0x2801,
			WID_SYS_DBG_LVL                    = 0x2802,
			WID_SYS_DBG_AREA                   = 0x2803,
			WID_UT_MODE                        = 0x2804,
			WID_UT_TX_LEN                      = 0x2805,
			WID_PTA_CTS_FRAME_LEN              = 0x2806,
			WID_PREASSO_SLEEP                  = 0x2807,

			WID_SSID                           = 0x3000,
			WID_FIRMWARE_VERSION               = 0x3001,
			WID_OPERATIONAL_RATE_SET           = 0x3002,
			WID_BSSID                          = 0x3003,
			WID_WEP_KEY_VALUE0                 = 0x3004,
			WID_WEP_KEY_VALUE1                 = 0x3005,
			WID_WEP_KEY_VALUE2                 = 0x3006,
			WID_WEP_KEY_VALUE3                 = 0x3007,
			WID_802_11I_PSK                    = 0x3008,
			WID_HCCA_ACTION_REQ                = 0x3009,
			WID_802_1X_KEY                     = 0x300A,
			WID_HARDWARE_VERSION               = 0x300B,
			WID_MAC_ADDR                       = 0x300C,
			WID_PHY_TEST_DEST_ADDR             = 0x300D,
			WID_PHY_TEST_STATS                 = 0x300E,
			WID_PHY_VERSION                    = 0x300F,
			WID_SUPP_USERNAME                  = 0x3010,
			WID_SUPP_PASSWORD                  = 0x3011,
			WID_SITE_SURVEY_RESULTS            = 0x3012,
			WID_RX_POWER_LEVEL                 = 0x3013,

			WID_ADD_WEP_KEY                    = 0x3019,
			WID_REMOVE_WEP_KEY                 = 0x301A,
			WID_ADD_PTK                        = 0x301B,
			WID_ADD_RX_GTK                     = 0x301C,
			WID_ADD_TX_GTK                     = 0x301D,
			WID_REMOVE_KEY                     = 0x301E,
			WID_ASSOC_REQ_INFO                 = 0x301F,
			WID_ASSOC_RES_INFO                 = 0x3020,
			WID_UPDATE_RF_SUPPORTED_INFO       = 0x3021,
			WID_COUNTRY_IE                     = 0x3022,

			WID_WAPI_ASSOC_IE                  = 0x3023,
			WID_ADD_WAPI_PTK                   = 0x3024,
			WID_ADD_WAPI_RX_GTK                = 0x3025,
			WID_ADD_WAPI_TX_GTK                = 0x3026,

			WID_CONFIG_HCCA_ACTION_REQ         = 0x4000,
			WID_UAPSD_CONFIG                   = 0x4001,
			WID_UAPSD_STATUS                   = 0x4002,
			WID_WMM_AP_AC_PARAMS               = 0x4003,
			WID_WMM_STA_AC_PARAMS              = 0x4004,
			WID_NEWORK_INFO                    = 0x4005,
			WID_STA_JOIN_INFO                  = 0x4006,
			WID_CONNECTED_STA_LIST             = 0x4007,
			WID_HUT_STATS                      = 0x4082,
			WID_STATISTICS                     = 0x4008,
			WID_MEMORY_DUMP                    = 0x4009,
			WID_LOAD_TRAP_MAP                  = 0x400a,
			WID_AGC_DGC_TBL                    = 0x400b,
			WID_ALL                            = 0x7FFE,
			WID_MAX                            = 0xFFFF
} WID_T;

int rda5890_wid_request(struct rda5890_private *priv, 
		char *wid_req, unsigned short wid_req_len,
		char *wid_rsp, unsigned short *wid_rsp_len);

void rda5890_wid_response(struct rda5890_private *priv, 
		char *wid_rsp, unsigned short wid_rsp_len);

void rda5890_wid_status(struct rda5890_private *priv, 
		char *wid_status, unsigned short wid_status_len);


void rda5890_card_to_host(struct rda5890_private *priv, 
		char *packet, unsigned short packet_len);

int rda5890_start_scan(struct rda5890_private *priv);
int rda5890_start_scan_enable_network_info(struct rda5890_private *priv);
int rda5890_start_join(struct rda5890_private *priv);

int rda5890_get_fw_ver(struct rda5890_private *priv, unsigned long *fw_ver);
int rda5890_get_mac_addr(struct rda5890_private *priv, unsigned char *mac_addr);
int rda5890_get_scan_results(struct rda5890_private *priv, 
		struct rda5890_bss_descriptor *bss_desc);
int rda5890_get_bssid(struct rda5890_private *priv, unsigned char *bssid);
int rda5890_set_bssid(struct rda5890_private *priv, unsigned char *bssid);

int rda5890_get_channel(struct rda5890_private *priv, unsigned char *channel);
int rda5890_get_rssi(struct rda5890_private *priv, unsigned char *rssi);

int rda5890_set_mac_addr(struct rda5890_private *priv, unsigned char *mac_addr);
int rda5890_set_preamble(struct rda5890_private *priv, unsigned char  preamble);

int rda5890_set_ssid(struct rda5890_private *priv, 
		unsigned char *ssid, unsigned char ssid_len);
int rda5890_get_ssid(struct rda5890_private *priv, 
		unsigned char *ssid, unsigned char *ssid_len);
int rda5890_set_imode(struct rda5890_private *priv, unsigned char imode);
int rda5890_set_authtype(struct rda5890_private *priv, unsigned char authtype);
int rda5890_set_listen_interval(struct rda5890_private *priv, unsigned char interval);
int rda5890_set_link_loss_threshold(struct rda5890_private *priv, unsigned char threshold);
int rda5890_set_wepkey(struct rda5890_private *priv, 
		unsigned short index, unsigned char *key, unsigned char key_len);
int rda5890_set_ptk(struct rda5890_private *priv, 
		unsigned char *key, unsigned char key_len);
int rda5890_set_gtk(struct rda5890_private *priv, unsigned char key_id,
		unsigned char *key_rsc, unsigned char key_rsc_len,
		unsigned char *key, unsigned char key_len);

int rda5890_set_pm_mode(struct rda5890_private *priv, unsigned char pm_mode);
int rda5890_set_preasso_sleep(struct rda5890_private *priv, unsigned int preasso_sleep);

int rda5890_generic_get_str(struct rda5890_private *priv, 
		unsigned short wid, unsigned char *val, unsigned char len);
int rda5890_generic_set_str(struct rda5890_private *priv, 
		unsigned short wid, unsigned char *val, unsigned char len);

int rda5890_generic_set_uchar(struct rda5890_private *priv,
                unsigned short wid, unsigned char val);
int rda5890_generic_get_ushort(struct rda5890_private *priv, 
		unsigned short wid, unsigned short *val);
int rda5890_generic_set_ushort(struct rda5890_private *priv, 
		unsigned short wid, unsigned short val);

int rda5890_generic_set_ulong(struct rda5890_private *priv,
                unsigned short wid, unsigned long val);
int rda5890_generic_get_ulong(struct rda5890_private *priv,
                unsigned short wid, unsigned long *val);

int rda5890_set_core_init(struct rda5890_private *priv, const unsigned int (*data)[2], unsigned char num);
int rda5890_set_core_patch(struct rda5890_private *priv, const unsigned char (*patch)[2], unsigned char num);
int rda5890_set_core_init_polling(struct rda5890_private *priv, const unsigned int (*data)[2], unsigned char num);
int rda5890_set_core_patch_polling(struct rda5890_private *priv, const unsigned char (*patch)[2], unsigned char num);
int rda5890_set_scan_timeout(struct rda5890_private *priv);

int rda5890_set_txrate(struct rda5890_private *priv, unsigned char mbps);
#ifdef GET_SCAN_FROM_NETWORK_INFO

int rda5890_set_scan_complete(struct rda5890_private *priv);
#endif
int rda5890_wid_request_polling(struct rda5890_private *priv, 
		char *wid_req, unsigned short wid_req_len,
		char *wid_rsp, unsigned short *wid_rsp_len);
int rda5890_check_wid_response(char *wid_rsp, unsigned short wid_rsp_len,
		unsigned short wid, char wid_msg_id, 
		char payload_len, char **ptr_payload);
#endif

