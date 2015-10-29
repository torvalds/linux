#ifndef HOST_INT_H
#define HOST_INT_H

#include "coreconfigurator.h"

#define IP_ALEN  4

#define IDLE_MODE	0x00
#define AP_MODE		0x01
#define STATION_MODE	0x02
#define GO_MODE		0x03
#define CLIENT_MODE	0x04
#define ACTION		0xD0
#define PROBE_REQ	0x40
#define PROBE_RESP	0x50

#define ACTION_FRM_IDX				0
#define PROBE_REQ_IDX				1
#define MAX_NUM_STA				9
#define ACTIVE_SCAN_TIME			10
#define PASSIVE_SCAN_TIME			1200
#define MIN_SCAN_TIME				10
#define MAX_SCAN_TIME				1200
#define DEFAULT_SCAN				0
#define USER_SCAN				BIT(0)
#define OBSS_PERIODIC_SCAN			BIT(1)
#define OBSS_ONETIME_SCAN			BIT(2)
#define GTK_RX_KEY_BUFF_LEN			24
#define ADDKEY					0x1
#define REMOVEKEY				0x2
#define DEFAULTKEY				0x4
#define ADDKEY_AP				0x8
#define MAX_NUM_SCANNED_NETWORKS		100
#define MAX_NUM_SCANNED_NETWORKS_SHADOW		130
#define MAX_NUM_PROBED_SSID			10
#define CHANNEL_SCAN_TIME			250

#define TX_MIC_KEY_LEN				8
#define RX_MIC_KEY_LEN				8
#define PTK_KEY_LEN				16

#define TX_MIC_KEY_MSG_LEN			26
#define RX_MIC_KEY_MSG_LEN			48
#define PTK_KEY_MSG_LEN				39

#define PMKSA_KEY_LEN				22
#define ETH_ALEN				6
#define PMKID_LEN				16
#define WILC_MAX_NUM_PMKIDS			16
#define WILC_SUPP_MCS_SET_SIZE			16
#define WILC_ADD_STA_LENGTH			40
#define SCAN_EVENT_DONE_ABORTED
#define NUM_CONCURRENT_IFC			2

struct rf_info {
	u8 link_speed;
	s8 rssi;
	u32 tx_cnt;
	u32 rx_cnt;
	u32 tx_fail_cnt;
};

enum host_if_state {
	HOST_IF_IDLE			= 0,
	HOST_IF_SCANNING		= 1,
	HOST_IF_CONNECTING		= 2,
	HOST_IF_WAITING_CONN_RESP	= 3,
	HOST_IF_CONNECTED		= 4,
	HOST_IF_P2P_LISTEN		= 5,
	HOST_IF_FORCE_32BIT		= 0xFFFFFFFF
};

struct host_if_pmkid {
	u8 bssid[ETH_ALEN];
	u8 pmkid[PMKID_LEN];
};

struct host_if_pmkid_attr {
	u8 numpmkid;
	struct host_if_pmkid pmkidlist[WILC_MAX_NUM_PMKIDS];
};

enum CURRENT_TXRATE {
	AUTORATE	= 0,
	MBPS_1		= 1,
	MBPS_2		= 2,
	MBPS_5_5	= 5,
	MBPS_11		= 11,
	MBPS_6		= 6,
	MBPS_9		= 9,
	MBPS_12		= 12,
	MBPS_18		= 18,
	MBPS_24		= 24,
	MBPS_36		= 36,
	MBPS_48		= 48,
	MBPS_54		= 54
};

struct cfg_param_val {
	u32 flag;
	u8 ht_enable;
	u8 bss_type;
	u8 auth_type;
	u16 auth_timeout;
	u8 power_mgmt_mode;
	u16 short_retry_limit;
	u16 long_retry_limit;
	u16 frag_threshold;
	u16 rts_threshold;
	u16 preamble_type;
	u8 short_slot_allowed;
	u8 txop_prot_disabled;
	u16 beacon_interval;
	u16 dtim_period;
	enum SITESURVEY site_survey_enabled;
	u16 site_survey_scan_time;
	u8 scan_source;
	u16 active_scan_time;
	u16 passive_scan_time;
	enum CURRENT_TXRATE curr_tx_rate;

};

enum cfg_param {
	RETRY_SHORT		= BIT(0),
	RETRY_LONG		= BIT(1),
	FRAG_THRESHOLD		= BIT(2),
	RTS_THRESHOLD		= BIT(3),
	BSS_TYPE		= BIT(4),
	AUTH_TYPE		= BIT(5),
	AUTHEN_TIMEOUT		= BIT(6),
	POWER_MANAGEMENT	= BIT(7),
	PREAMBLE		= BIT(8),
	SHORT_SLOT_ALLOWED	= BIT(9),
	TXOP_PROT_DISABLE	= BIT(10),
	BEACON_INTERVAL		= BIT(11),
	DTIM_PERIOD		= BIT(12),
	SITE_SURVEY		= BIT(13),
	SITE_SURVEY_SCAN_TIME	= BIT(14),
	ACTIVE_SCANTIME		= BIT(15),
	PASSIVE_SCANTIME	= BIT(16),
	CURRENT_TX_RATE		= BIT(17),
	HT_ENABLE		= BIT(18),
};

struct found_net_info {
	u8 au8bssid[6];
	s8 s8rssi;
};

enum scan_event {
	SCAN_EVENT_NETWORK_FOUND	= 0,
	SCAN_EVENT_DONE			= 1,
	SCAN_EVENT_ABORTED		= 2,
	SCAN_EVENT_FORCE_32BIT		= 0xFFFFFFFF
};

enum conn_event {
	CONN_DISCONN_EVENT_CONN_RESP		= 0,
	CONN_DISCONN_EVENT_DISCONN_NOTIF	= 1,
	CONN_DISCONN_EVENT_FORCE_32BIT		= 0xFFFFFFFF
};

enum KEY_TYPE {
	WEP,
	WPARxGtk,
	WPAPtk,
	PMKSA,
};

typedef void (*wilc_scan_result)(enum scan_event, tstrNetworkInfo *,
				  void *, void *);

typedef void (*wilc_connect_result)(enum conn_event,
				     tstrConnectInfo *,
				     u8,
				     tstrDisconnectNotifInfo *,
				     void *);

typedef void (*wilc_remain_on_chan_expired)(void *, u32);
typedef void (*wilc_remain_on_chan_ready)(void *);

struct rcvd_net_info {
	u8 *buffer;
	u32 len;
};

struct hidden_net_info {
	u8  *pu8ssid;
	u8 u8ssidlen;
};

struct hidden_network {
	struct hidden_net_info *pstrHiddenNetworkInfo;
	u8 u8ssidnum;
};

struct user_scan_req {
	wilc_scan_result scan_result;
	void *arg;
	u32 u32RcvdChCount;
	struct found_net_info net_info[MAX_NUM_SCANNED_NETWORKS];
};

struct user_conn_req {
	u8 *pu8bssid;
	u8 *pu8ssid;
	u8 u8security;
	enum AUTHTYPE tenuAuth_type;
	size_t ssid_len;
	u8 *ies;
	size_t ies_len;
	wilc_connect_result conn_result;
	bool ht_capable;
	void *arg;
};

struct drv_handler {
	u32 handler;
};

struct op_mode {
	u32 mode;
};

struct set_mac_addr {
	u8 mac_addr[ETH_ALEN];
};

struct get_mac_addr {
	u8 *mac_addr;
};

struct ba_session_info {
	u8 bssid[ETH_ALEN];
	u8 tid;
	u16 buf_size;
	u16 time_out;
};

struct remain_ch {
	u16 ch;
	u32 u32duration;
	wilc_remain_on_chan_expired expired;
	wilc_remain_on_chan_ready ready;
	void *arg;
	u32 u32ListenSessionID;
};

struct reg_frame {
	bool reg;
	u16 frame_type;
	u8 reg_id;
};

enum p2p_listen_state {
	P2P_IDLE,
	P2P_LISTEN,
	P2P_GRP_FORMATION
};

struct host_if_drv {
	struct user_scan_req usr_scan_req;
	struct user_conn_req usr_conn_req;
	struct remain_ch remain_on_ch;
	u8 remain_on_ch_pending;
	u64 u64P2p_MgmtTimeout;
	u8 u8P2PConnect;

	enum host_if_state hif_state;

	u8 assoc_bssid[ETH_ALEN];
	struct cfg_param_val cfg_values;

	struct semaphore sem_cfg_values;
	struct semaphore sem_test_key_block;
	struct semaphore sem_test_disconn_block;
	struct semaphore sem_get_rssi;
	struct semaphore sem_get_link_speed;
	struct semaphore sem_get_chnl;
	struct semaphore sem_inactive_time;

	struct timer_list scan_timer;
	struct timer_list connect_timer;
	struct timer_list remain_on_ch_timer;

	bool IFC_UP;
};

struct add_sta_param {
	u8 au8BSSID[ETH_ALEN];
	u16 u16AssocID;
	u8 u8NumRates;
	const u8 *pu8Rates;
	bool bIsHTSupported;
	u16 u16HTCapInfo;
	u8 u8AmpduParams;
	u8 au8SuppMCsSet[16];
	u16 u16HTExtParams;
	u32 u32TxBeamformingCap;
	u8 u8ASELCap;
	u16 u16FlagsMask;
	u16 u16FlagsSet;
};

s32 host_int_remove_key(struct host_if_drv *hWFIDrv, const u8 *pu8StaAddress);
int host_int_remove_wep_key(struct host_if_drv *wfi_drv, u8 index);
int host_int_set_wep_default_key(struct host_if_drv *hif_drv, u8 index);
int host_int_add_wep_key_bss_sta(struct host_if_drv *hif_drv,
				 const u8 *key, u8 len, u8 index);
int host_int_add_wep_key_bss_ap(struct host_if_drv *hif_drv,
				const u8 *key, u8 len, u8 index, u8 mode,
				enum AUTHTYPE auth_type);
s32 host_int_add_ptk(struct host_if_drv *hWFIDrv, const u8 *pu8Ptk,
		     u8 u8PtkKeylen, const u8 *mac_addr,
		     const u8 *pu8RxMic, const u8 *pu8TxMic,
		     u8 mode, u8 u8Ciphermode, u8 u8Idx);
s32 host_int_get_inactive_time(struct host_if_drv *hWFIDrv, const u8 *mac,
			       u32 *pu32InactiveTime);
s32 host_int_add_rx_gtk(struct host_if_drv *hWFIDrv, const u8 *pu8RxGtk,
			u8 u8GtkKeylen,	u8 u8KeyIdx,
			u32 u32KeyRSClen, const u8 *KeyRSC,
			const u8 *pu8RxMic, const u8 *pu8TxMic,
			u8 mode, u8 u8Ciphermode);
s32 host_int_add_tx_gtk(struct host_if_drv *hWFIDrv, u8 u8KeyLen,
			u8 *pu8TxGtk, u8 u8KeyIdx);
s32 host_int_set_pmkid_info(struct host_if_drv *hWFIDrv,
			    struct host_if_pmkid_attr *pu8PmkidInfoArray);
s32 host_int_get_pmkid_info(struct host_if_drv *hWFIDrv, u8 *pu8PmkidInfoArray,
			    u32 u32PmkidInfoLen);
s32 host_int_set_RSNAConfigPSKPassPhrase(struct host_if_drv *hWFIDrv,
					 u8 *pu8PassPhrase,
					 u8 u8Psklength);
s32 host_int_get_RSNAConfigPSKPassPhrase(struct host_if_drv *hWFIDrv,
					 u8 *pu8PassPhrase, u8 u8Psklength);
s32 host_int_get_MacAddress(struct host_if_drv *hWFIDrv, u8 *pu8MacAddress);
s32 host_int_set_MacAddress(struct host_if_drv *hWFIDrv, u8 *pu8MacAddress);
int host_int_wait_msg_queue_idle(void);
s32 host_int_set_start_scan_req(struct host_if_drv *hWFIDrv, u8 scanSource);
s32 host_int_get_start_scan_req(struct host_if_drv *hWFIDrv, u8 *pu8ScanSource);
s32 host_int_set_join_req(struct host_if_drv *hWFIDrv, u8 *pu8bssid,
			  const u8 *pu8ssid, size_t ssidLen,
			  const u8 *pu8IEs, size_t IEsLen,
			  wilc_connect_result pfConnectResult, void *pvUserArg,
			  u8 u8security, enum AUTHTYPE tenuAuth_type,
			  u8 u8channel, void *pJoinParams);
s32 host_int_flush_join_req(struct host_if_drv *hWFIDrv);
s32 host_int_disconnect(struct host_if_drv *hWFIDrv, u16 u16ReasonCode);
s32 host_int_disconnect_station(struct host_if_drv *hWFIDrv, u8 assoc_id);
s32 host_int_get_assoc_req_info(struct host_if_drv *hWFIDrv,
				u8 *pu8AssocReqInfo,
				u32 u32AssocReqInfoLen);
s32 host_int_get_assoc_res_info(struct host_if_drv *hWFIDrv,
				u8 *pu8AssocRespInfo,
				u32 u32MaxAssocRespInfoLen,
				u32 *pu32RcvdAssocRespInfoLen);
s32 host_int_get_rx_power_level(struct host_if_drv *hWFIDrv,
				u8 *pu8RxPowerLevel,
				u32 u32RxPowerLevelLen);
int host_int_set_mac_chnl_num(struct host_if_drv *wfi_drv, u8 channel);
s32 host_int_get_host_chnl_num(struct host_if_drv *hWFIDrv, u8 *pu8ChNo);
s32 host_int_get_rssi(struct host_if_drv *hWFIDrv, s8 *ps8Rssi);
s32 host_int_get_link_speed(struct host_if_drv *hWFIDrv, s8 *ps8lnkspd);
s32 host_int_scan(struct host_if_drv *hWFIDrv, u8 u8ScanSource,
		  u8 u8ScanType, u8 *pu8ChnlFreqList,
		  u8 u8ChnlListLen, const u8 *pu8IEs,
		  size_t IEsLen, wilc_scan_result ScanResult,
		  void *pvUserArg, struct hidden_network *pstrHiddenNetwork);
s32 hif_set_cfg(struct host_if_drv *hWFIDrv,
		struct cfg_param_val *pstrCfgParamVal);
s32 hif_get_cfg(struct host_if_drv *hWFIDrv, u16 u16WID, u16 *pu16WID_Value);
s32 host_int_init(struct net_device *dev, struct host_if_drv **phWFIDrv);
s32 host_int_deinit(struct host_if_drv *hWFIDrv);
s32 host_int_add_beacon(struct host_if_drv *hWFIDrv, u32 u32Interval,
			u32 u32DTIMPeriod,
			u32 u32HeadLen,
			u8 *pu8Head,
			u32 u32TailLen,
			u8 *pu8tail);
s32 host_int_del_beacon(struct host_if_drv *hWFIDrv);
s32 host_int_add_station(struct host_if_drv *hWFIDrv,
			 struct add_sta_param *pstrStaParams);
s32 host_int_del_allstation(struct host_if_drv *hWFIDrv,
			    u8 pu8MacAddr[][ETH_ALEN]);
s32 host_int_del_station(struct host_if_drv *hWFIDrv, const u8 *pu8MacAddr);
s32 host_int_edit_station(struct host_if_drv *hWFIDrv,
			  struct add_sta_param *pstrStaParams);
s32 host_int_set_power_mgmt(struct host_if_drv *hWFIDrv,
			    bool bIsEnabled,
			    u32 u32Timeout);
s32 host_int_setup_multicast_filter(struct host_if_drv *hWFIDrv,
				    bool bIsEnabled,
				    u32 u32count);
s32 host_int_setup_ipaddress(struct host_if_drv *hWFIDrv,
			     u8 *pu8IPAddr,
			     u8 idx);
s32 host_int_delBASession(struct host_if_drv *hWFIDrv, char *pBSSID, char TID);
s32 host_int_del_All_Rx_BASession(struct host_if_drv *hWFIDrv,
				  char *pBSSID,
				  char TID);
s32 host_int_get_ipaddress(struct host_if_drv *hWFIDrv, u8 *pu8IPAddr, u8 idx);
s32 host_int_remain_on_channel(struct host_if_drv *hWFIDrv,
			       u32 u32SessionID,
			       u32 u32duration,
			       u16 chan,
			       wilc_remain_on_chan_expired RemainOnChanExpired,
			       wilc_remain_on_chan_ready RemainOnChanReady,
			       void *pvUserArg);
s32 host_int_ListenStateExpired(struct host_if_drv *hWFIDrv, u32 u32SessionID);
s32 host_int_frame_register(struct host_if_drv *hWFIDrv,
			    u16 u16FrameType,
			    bool bReg);
int host_int_set_wfi_drv_handler(struct host_if_drv *address);
int host_int_set_operation_mode(struct host_if_drv *wfi_drv, u32 mode);

static s32 Handle_ScanDone(struct host_if_drv *drvHandler,
			   enum scan_event enuEvent);

void host_int_freeJoinParams(void *pJoinParams);

s32 host_int_get_statistics(struct host_if_drv *hWFIDrv,
			    struct rf_info *pstrStatistics);

#endif
